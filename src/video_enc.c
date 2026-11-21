/**
 * @file video_enc.c
 * @ingroup media
 * @brief H.264 video encoding (the hardware encoder) into an MP4 file.
 */
#include "video_enc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_h264_enc_single_hw.h"
#include "esp_h264_alloc.h"
#include "driver/ppa.h"
#include "mp4_writer.h"

#define OUT_MAX (512 * 1024)              /**< one encoded frame */
/**
 * @brief Prints the internal RAM free and its largest block (the encoder needs ~52 KB in one piece).
 * @param when  before or after what
 */
static void log_internal(const char *when)
{
    printf("video_enc: internal RAM %s: %u free, largest block %u\n", when,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}


/** @brief An encoder writing one MP4 file. */
struct video_enc {
    int w;                                /**< px */
    int h;                                /**< px */
    int fps;                              /**< frames a second */
    esp_h264_enc_handle_t enc;            /**< the hardware encoder */
    uint8_t *yuv;                         /**< O_UYY_E_VYY, w*h*3/2 */
    uint32_t yuv_size;                    /**< its bytes */
    uint8_t *out;                         /**< the encoded frame */
    uint32_t out_size;                    /**< its room */
    uint32_t pts;                         /**< frames encoded (the next one's number) */
    mp4w_t *mp4;                          /**< the file */
    ppa_client_handle_t ppa;              /**< RGB565 -> YUV 4:2:0 by the PPA (NULL: the CPU loop) */
};

/**
 * @brief * @brief The colour conversion runs on the PPA (its scale-rotate-mirror unit, by DMA): 20 ms
 * a 720x720 frame where the CPU loop below takes 87 ms. Its packed YUV 4:2:0 is the
 * layout the encoder takes. The frames come in as RGB888, not RGB565: the PPA widens
 * 5- and 6-bit RGB565 by shifting alone, so white came out (248, 252, 248) and every
 * white area of the video had a green cast. The CPU loop stays as the fallback.
 *
 * RGB888 (LVGL's: bytes B, G, R) -> the hardware encoder's packed YUV 4:2:0
 * (pre-v3 ESP32-P4 takes only this): even lines "u y y u y y ...", odd lines
 * "v y y v y y ...". One U and one V per 2x2 block, from the block's average
 * colour. BT.601 limited range, as H.264 players expect by default.
 * @param src  the picture
 * @param dst  gets the YUV
 * @param w  px
 * @param h  px
 */
static void rgb888_to_ouyy_evyy(const uint8_t *src, uint8_t *dst, int w, int h)
{
    for (int y = 0; y < h; y += 2) {
        const uint8_t *s0 = src + (size_t)y * w * 3, *s1 = s0 + (size_t)w * 3;
        uint8_t *d0 = dst + (size_t)y * w * 3 / 2, *d1 = d0 + w * 3 / 2;
        for (int x = 0; x < w; x += 2) {
            int rs = 0, gs = 0, bs = 0;
            uint8_t yy[4];
            const uint8_t *px[4] = {s0 + x * 3, s0 + x * 3 + 3, s1 + x * 3, s1 + x * 3 + 3};
            for (int i = 0; i < 4; i++) {
                int b = px[i][0], g = px[i][1], r = px[i][2];
                yy[i] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                rs += r;
                gs += g;
                bs += b;
            }
            rs >>= 2;
            gs >>= 2;
            bs >>= 2;
            int u = ((-38 * rs - 74 * gs + 112 * bs + 128) >> 8) + 128;
            int v = ((112 * rs - 94 * gs - 18 * bs + 128) >> 8) + 128;
            *d0++ = (uint8_t)u;
            *d0++ = yy[0];
            *d0++ = yy[1];
            *d1++ = (uint8_t)v;
            *d1++ = yy[2];
            *d1++ = yy[3];
        }
    }
}

/* (documented in video_enc.h) */
video_enc_t *video_enc_open(const char *path, int width, int height, int fps, int max_frames)
{
    if (width % 16 || height % 16) {
        return NULL;
    }
    log_internal("before export");
    video_enc_t *v = calloc(1, sizeof(*v));
    if (!v) {
        return NULL;
    }
    v->w = width;
    v->h = height;
    v->fps = fps;
    v->yuv = esp_h264_aligned_calloc(64, 1, (uint32_t)width * height * 3 / 2, &v->yuv_size, MALLOC_CAP_SPIRAM);
    v->out = esp_h264_aligned_calloc(64, 1, OUT_MAX, &v->out_size, MALLOC_CAP_SPIRAM);
    esp_h264_enc_cfg_hw_t cfg = {
        .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,
        .gop = (uint8_t)(fps * 2),                         /* a key frame every 2 s: seeking works */
        .fps = (uint8_t)fps,
        .res = {.width = (uint16_t)width, .height = (uint16_t)height},
        .rc = {.bitrate = (uint32_t)width * height * 2, .qp_min = 18, .qp_max = 32},
    };
    if (!v->yuv || !v->out || esp_h264_enc_hw_new(&cfg, &v->enc) != ESP_H264_ERR_OK ||
            esp_h264_enc_open(v->enc) != ESP_H264_ERR_OK) {
        printf("video_enc: H.264 encoder didn't start\n");
        video_enc_abort(v);
        return NULL;
    }
    ppa_client_config_t pc = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
    if (ppa_register_client(&pc, &v->ppa) != ESP_OK) {
        v->ppa = NULL;
        printf("video_enc: no PPA, converting on the CPU\n");
    }
    v->mp4 = mp4w_open(path, width, height, fps, max_frames);
    if (!v->mp4) {
        printf("video_enc: can't create %s\n", path);
        video_enc_abort(v);
        return NULL;
    }
    return v;
}

/**
 * @brief RGB888 (64-byte aligned, a whole number of cache lines: the caller's frame buffer)
 * -> v->yuv, by the PPA; false if it couldn't
 * @param v  the encoder
 * @param rgb888  the picture
 * @return false: it couldn't
 */
static bool ppa_convert(video_enc_t *v, const uint8_t *rgb888)
{
    if (!v->ppa) {
        return false;
    }
    ppa_srm_oper_config_t o = {
        .in = {
            .buffer = rgb888, .pic_w = (uint32_t)v->w, .pic_h = (uint32_t)v->h,
            .block_w = (uint32_t)v->w, .block_h = (uint32_t)v->h, .srm_cm = PPA_SRM_COLOR_MODE_RGB888
        },
        .out = {
            .buffer = v->yuv, .buffer_size = v->yuv_size, .pic_w = (uint32_t)v->w, .pic_h = (uint32_t)v->h,
            .srm_cm = PPA_SRM_COLOR_MODE_YUV420, .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601
        },        /* as the CPU loop: BT.601, limited */
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t err = ppa_do_scale_rotate_mirror(v->ppa, &o);
    if (err != ESP_OK) {
        printf("video_enc: PPA conversion failed (%s), using the CPU\n", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

/* (documented in video_enc.h) */
bool video_enc_frame(video_enc_t *v, const uint8_t *rgb888, bool changed)
{
    if ((changed || v->pts == 0) && !ppa_convert(v, rgb888)) {
        rgb888_to_ouyy_evyy(rgb888, v->yuv, v->w, v->h);
    }
    esp_h264_enc_in_frame_t in = {
        .raw_data = {.buffer = v->yuv, .len = v->yuv_size},
        .pts = v->pts * 1000 / v->fps,
    };
    esp_h264_enc_out_frame_t out = {.raw_data = {.buffer = v->out, .len = v->out_size}};
    esp_h264_err_t err = esp_h264_enc_process(v->enc, &in, &out);
    v->pts++;
    if (err != ESP_H264_ERR_OK) {
        printf("video_enc: encode failed (%d)\n", (int)err);
        return false;
    }
    bool key = out.frame_type == ESP_H264_FRAME_TYPE_IDR || out.frame_type == ESP_H264_FRAME_TYPE_I;
    return mp4w_add_frame(v->mp4, v->out, out.length, key);
}

/**
 * @brief Frees the encoder's buffers, the PPA and the encoder (not the file).
 * @param v  it
 */
static void release(video_enc_t *v)
{
    if (v->ppa) {
        ppa_unregister_client(v->ppa);
    }
    if (v->enc) {
        esp_h264_enc_close(v->enc);
        esp_h264_enc_del(v->enc);
    }
    if (v->yuv) {
        esp_h264_free(v->yuv);
    }
    if (v->out) {
        esp_h264_free(v->out);
    }
    free(v);
}

bool video_enc_close(video_enc_t *v)
{
    if (!v) {
        return false;
    }
    bool ok = mp4w_close(v->mp4);
    v->mp4 = NULL;
    release(v);
    return ok;
}

void video_enc_abort(video_enc_t *v)
{
    if (!v) {
        return;
    }
    if (v->mp4) {
        mp4w_abort(v->mp4);
    }
    release(v);
}

/* (documented in video_enc.h) */
uint32_t video_enc_bytes(const video_enc_t *v)
{
    return v ? mp4w_size(v->mp4) : 0;
}

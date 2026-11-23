/**
 * @file media_core.c
 * @ingroup media
 * @brief The picture and video viewer's decoding, without LVGL (the page: ui_media_view.c).
 *
 * A worker task (core 0) decodes into one of two 720x720 RGB565 buffers in
 * PSRAM while the screen shows the other: it fills the back buffer, flushes it
 * from the cache (LVGL's PPA draw unit reads images by DMA) and hands it over
 * (s_pending); the page's timer shows it and the worker may then fill the
 * other one. The worker never touches LVGL.
 *
 * Pictures: the file is read whole, decoded by the JPEG engine to RGB565 at
 * full size, then scaled down (nearest pixel) to fit the screen.
 * Videos: mp4_reader gives each frame's place; frames go through tinyh264 as
 * Annex-B and come out as I420, converted (and scaled to fit) to RGB565 here.
 * Playback is paced by the frames' times. Decoding (core 0) and converting
 * (core 1, only the rows that changed) overlap (see do_video); when still behind,
 * frames are decoded but not shown, so the video keeps its speed.
 * Seeking decodes from the key frame before the target and shows only the
 * target. tinyh264 takes about 55 KB of internal RAM while a video is open.
 *
 */
#include "media_core.h"
#include "mp4_reader.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "driver/jpeg_decode.h"
#include "esp_h264_dec_sw.h"

#define SCR        MEDIA_SCR            /**< the screen's size, px */
#define BUF_BYTES  (SCR * SCR * 2)      /**< a picture buffer's bytes (RGB565) */
#define NAME_B     256                  /**< a file name's room */
#define PATH_B     512                  /**< a path's room */
#define JPEG_MAX_FILE  (16u * 1024 * 1024)  /**< the largest JPEG file read */
#define JPEG_MAX_OUT   (24u * 1024 * 1024)       /**< decoded RGB565 at full size */
#define VIDEO_MAX_PX   (1920 * 1088)    /**< the largest video picture, px */

/* ------------------------------------------------------------- state */
/* shared with the worker */
static uint16_t *s_buf[2];                    /**< 64-byte aligned, PSRAM, kept once made */
static volatile bool s_running;               /**< the worker runs a job */
static volatile bool s_stop;                  /**< asks the worker to stop */
static volatile int s_pending = -1;           /**< the buffer handed over (-1: none) */
static volatile int s_front = 1;              /**< the buffer shown */
static volatile int s_pub_w[2];               /**< each buffer's picture: width, px */
static volatile int s_pub_h[2];               /**< and height */
static char s_msg[160];                       /**< the worker's message for the page */
static volatile uint32_t s_msg_seq;           /**< changes with each message */
static char s_job_path[PATH_B + NAME_B];      /**< the worker's file */
static int s_job_kind;                        /**< its MEDIA_* */
/* video */
static volatile bool s_v_info;                /**< the video's frames and length are known */
static volatile bool s_play;                  /**< playing */
static volatile bool s_ended;                 /**< it played to the end */
static volatile uint32_t s_v_n;               /**< its frames */
static volatile uint32_t s_v_pos;             /**< the frame shown */
static volatile uint32_t s_v_pos_ms;          /**< its time, ms */
static volatile uint32_t s_v_dur_ms;          /**< the video's length, ms */
static volatile int32_t s_seek = -1;          /**< the frame to seek to (-1: none) */

/* (documented in media_core.h) */
int media_view_kind(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return MEDIA_NONE;
    }
    if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) {
        return MEDIA_IMAGE;
    }
    if (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".m4v")) {
        return MEDIA_VIDEO;
    }
    return MEDIA_NONE;
}


/**
 * @brief The worker's message for the page (and the log).
 * @param[in] fmt  printf format
 * @param[in] ...  its arguments
 */
static void set_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_msg, sizeof(s_msg), fmt, ap);
    va_end(ap);
    printf("media: %s\n", s_msg);
    s_msg_seq++;
}

/**
 * @brief waits until the page has taken the last picture handed over; false: stop
 * @return false: stop
 */
static bool wait_free(void)
{
    while (s_pending >= 0 && !s_stop) {
        vTaskDelay(1);
    }
    return !s_stop;
}

/**
 * @brief The buffer the worker fills (not the one shown).
 * @return it
 */
static uint16_t *back_buf(void)
{
    return s_buf[1 - s_front];
}

/**
 * @brief Hands the back buffer over to the page (flushed from the cache for the PPA's DMA).
 * @param[in] w  the picture's width, px
 * @param[in] h  its height
 */
static void publish(int w, int h)
{
    int k = 1 - s_front;
    esp_cache_msync(s_buf[k], BUF_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);   /* for the PPA's DMA */
    s_pub_w[k] = w;
    s_pub_h[k] = h;
    s_pending = k;
}

/**
 * @brief The size to show a picture at: as it is, or scaled down to fit the screen.
 * @param[in] w  its width, px
 * @param[in] h  its height
 * @param[out] dw  gets the width shown
 * @param[out] dh  gets the height shown
 */
static void fit(int w, int h, int *dw, int *dh)
{
    if (w <= SCR && h <= SCR) {
        *dw = w;
        *dh = h;
    } else if ((int64_t)w * SCR >= (int64_t)h * SCR) {
        *dw = SCR;
        *dh = (int)((int64_t)h * SCR / w);
    } else {
        *dh = SCR;
        *dw = (int)((int64_t)w * SCR / h);
    }
    if (*dw < 1) {
        *dw = 1;
    }
    if (*dh < 1) {
        *dh = 1;
    }
}

static uint16_t s_xmap[SCR];            /**< each output column's source column (make_xmap()) */

/**
 * @brief Each output column's source column (centres), for scaling a row.
 * @param[in] sw  the source's width
 * @param[in] dw  the output's width
 */
static void make_xmap(int sw, int dw)
{
    for (int x = 0; x < dw; x++) {
        s_xmap[x] = (uint16_t)(((int64_t)x * 2 + 1) * sw / (2 * dw));
    }
}

/**
 * @brief An output row's source row (centres).
 * @param[in] y  the output row
 * @param[in] sh  the source's height
 * @param[in] dh  the output's height
 * @return the source row
 */
static inline int src_row(int y, int sh, int dh)
{
    return (int)(((int64_t)y * 2 + 1) * sh / (2 * dh));
}

/**
 * @brief Shows a JPEG: read whole, decoded by the JPEG engine at full size, scaled down to fit.
 * @param[in] path  the file
 */
static void do_jpeg(const char *path)
{
    uint8_t *in = NULL;
    uint16_t *out = NULL;
    jpeg_decoder_handle_t dec = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_msg("Couldn't open the file.");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || (unsigned long)size > JPEG_MAX_FILE) {
        set_msg(size <= 0 ? "The file is empty." : "The picture is too big to show (over 16 MB).");
        goto done;
    }
    jpeg_decode_memory_alloc_cfg_t icfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    size_t in_cap = 0;
    in = jpeg_alloc_decoder_mem((size_t)size, &icfg, &in_cap);
    if (!in) {
        set_msg("Out of memory.");
        goto done;
    }
    if (fread(in, 1, (size_t)size, f) != (size_t)size) {
        set_msg("Couldn't read the file.");
        goto done;
    }
    fclose(f);
    f = NULL;
    if (s_stop) {
        goto done;
    }

    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(in, (uint32_t)size, &info) != ESP_OK || !info.width || !info.height) {
        set_msg("This JPEG can't be read.");
        goto done;
    }
    /* the engine writes whole blocks: 8 or 16 pixels, by the chroma sampling */
    int wb = (info.sample_method == JPEG_DOWN_SAMPLING_YUV444 || info.sample_method == JPEG_DOWN_SAMPLING_GRAY) ? 8 : 16;
    int hb = info.sample_method == JPEG_DOWN_SAMPLING_YUV420 ? 16 : 8;
    uint32_t aw = (info.width + wb - 1) / wb * wb, ah = (info.height + hb - 1) / hb * hb;
    if ((uint64_t)aw * ah * 2 > JPEG_MAX_OUT) {
        set_msg("The picture is too big to show (%ux%u).", (unsigned)info.width, (unsigned)info.height);
        goto done;
    }
    jpeg_decode_memory_alloc_cfg_t ocfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    size_t out_cap = 0;
    out = jpeg_alloc_decoder_mem((size_t)aw * ah * 2, &ocfg, &out_cap);
    jpeg_decode_engine_cfg_t ecfg = {.timeout_ms = 2000};
    if (!out || jpeg_new_decoder_engine(&ecfg, &dec) != ESP_OK) {
        dec = NULL;
        set_msg("Out of memory.");
        goto done;
    }
    jpeg_decode_cfg_t cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,   /* little-endian RGB565, as LVGL's */
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t got = 0;
    esp_err_t err = jpeg_decoder_process(dec, &cfg, in, (uint32_t)size, (uint8_t *)out, (uint32_t)out_cap, &got);
    if (err != ESP_OK) {
        set_msg("This JPEG can't be decoded here (a progressive JPEG?).");
        goto done;
    }
    if (got >= 2 * ah) {                           /* the row length the engine used */
        uint32_t stride = got / 2 / ah;
        if (stride >= info.width) {
            aw = stride;
        }
    }
    int dw, dh;
    fit((int)info.width, (int)info.height, &dw, &dh);
    if (!wait_free()) {
        goto done;
    }
    uint16_t *dst = back_buf();
    make_xmap((int)info.width, dw);
    for (int y = 0; y < dh; y++) {
        const uint16_t *row = out + (size_t)src_row(y, (int)info.height, dh) * aw;
        uint16_t *o = dst + (size_t)y * dw;
        if (dw == (int)info.width) {
            memcpy(o, row, (size_t)dw * 2);
        } else {
            for (int x = 0; x < dw; x++) {
                o[x] = row[s_xmap[x]];
            }
        }
    }
    publish(dw, dh);
done:
    if (f) {
        fclose(f);
    }
    if (dec) {
        jpeg_del_decoder_engine(dec);
    }
    if (out) {
        heap_caps_free(out);
    }
    if (in) {
        heap_caps_free(in);
    }
}

/* ------------------------------------------------------------- H.264 */
static int32_t t_y[256];                      /**< YUV -> RGB: Y's part (BT.601 limited range, x 256) */
static int32_t t_rv[256];                     /**< V's part of red */
static int32_t t_gu[256];                     /**< U's part of green */
static int32_t t_gv[256];                     /**< V's part of green */
static int32_t t_bu[256];                     /**< U's part of blue */

/**
 * @brief Clamps to 0..255.
 * @param[in] v  a level
 * @return it, clamped
 */
static inline int clamp8(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

#define CLIP_OFF 384                          /**< the tables' offset: the sums (>> 8) stay within -384..639 */
static uint16_t c_r[1024];                    /**< (sum >> 8) + CLIP_OFF -> red's bits of an RGB565 pixel, clamped */
static uint16_t c_g[1024];                    /**< the same for green */
static uint16_t c_b[1024];                    /**< the same for blue */

/**
 * @brief Fills the YUV -> RGB565 tables (once).
 */
static void yuv_tables(void)
{
    if (t_y[255]) {
        return;
    }
    for (int i = 0; i < 1024; i++) {
        int v = clamp8(i - CLIP_OFF);
        c_r[i] = (uint16_t)((v & 0xF8) << 8);
        c_g[i] = (uint16_t)((v & 0xFC) << 3);
        c_b[i] = (uint16_t)(v >> 3);
    }
    for (int i = 0; i < 256; i++) {               /* BT.601 limited range, x256 */
        t_y[i] = 298 * (i - 16) + 128;
        t_rv[i] = 409 * (i - 128);
        t_gu[i] = -100 * (i - 128);
        t_gv[i] = -208 * (i - 128);
        t_bu[i] = 516 * (i - 128);
    }
}

/** @brief I420 (W x H planes; the picture is the top-left vw x vh) -> dw x dh RGB565 */
#define PX(c) (uint32_t)(c_r[((c) + rv) >> 8] | c_g[((c) + guv) >> 8] | c_b[((c) + bu) >> 8])

/**
 * @brief chg (1:1 only, else NULL): per pair of rows, the frame number it last changed in;
 * only pairs changed after frame `since` are converted (dst holds frame `since`)
 * @param[in] yp  the Y plane (U and V follow)
 * @param[in] W  the planes' width
 * @param[in] H  their height
 * @param[in] vw  the picture's width
 * @param[in] vh  its height
 * @param[out] dst  gets RGB565
 * @param[in] dw  dst's width
 * @param[in] dh  dst's height
 * @param[in] chg  each pair of rows' last change, or NULL
 * @param[in] since  the frame dst holds
 */
static void i420_to_565(const uint8_t *yp, int W, int H, int vw, int vh, uint16_t *dst, int dw, int dh,
                        const uint32_t *chg, int32_t since)
{
    const uint8_t *up = yp + (size_t)W * H, *vp = up + (size_t)(W / 2) * (H / 2);
    if (dw == vw && dh == vh && !(dw & 1) && !(dh & 1)) {
        /* 1:1 (the exported replays): a 2x2 block at a time, one chroma sum for its
         * four pixels, two pixels per 32-bit write. 130 -> ~40 ms a 720x720 frame;
         * the planes are in PSRAM, so fewer, wider accesses are what counts */
        for (int y = 0; y < dh; y += 2) {
            if (chg && since >= 0 && (int32_t)chg[y / 2] <= since) {
                continue;    /* as it is in dst */
            }
            const uint8_t *y0 = yp + (size_t)y * W, *y1 = y0 + W;
            const uint8_t *ur = up + (size_t)(y / 2) * (W / 2), *vr = vp + (size_t)(y / 2) * (W / 2);
            uint32_t *o0 = (uint32_t *)(dst + (size_t)y * dw), *o1 = (uint32_t *)(dst + (size_t)(y + 1) * dw);
            for (int x = 0; x < dw / 2; x++) {
                int u = ur[x], v = vr[x];
                int rv = t_rv[v] + (CLIP_OFF << 8), guv = t_gu[u] + t_gv[v] + (CLIP_OFF << 8),
                    bu = t_bu[u] + (CLIP_OFF << 8);
                o0[x] = PX(t_y[y0[2 * x]]) | PX(t_y[y0[2 * x + 1]]) << 16;
                o1[x] = PX(t_y[y1[2 * x]]) | PX(t_y[y1[2 * x + 1]]) << 16;
            }
        }
        return;
    }
    make_xmap(vw, dw);
    for (int y = 0; y < dh; y++) {
        int sy = src_row(y, vh, dh);
        const uint8_t *yr = yp + (size_t)sy * W, *ur = up + (size_t)(sy / 2) * (W / 2), *vr = vp + (size_t)(sy / 2) * (W / 2);
        uint16_t *o = dst + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            int sx = s_xmap[x];
            int c = t_y[yr[sx]], u = ur[sx >> 1], v = vr[sx >> 1];
            int r = clamp8((c + t_rv[v]) >> 8);
            int g = clamp8((c + t_gu[u] + t_gv[v]) >> 8);
            int b = clamp8((c + t_bu[u]) >> 8);
            o[x] = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
        }
    }
}

/**
 * @brief one access unit (Annex-B) through the decoder; *frame: a picture that came out
 * @param[in] dec  the decoder
 * @param[in] buf  the access unit
 * @param[in] len  its bytes
 * @param[out] frame  gets a picture, or NULL
 * @return false: the decoder failed
 */
static bool feed(esp_h264_dec_handle_t dec, uint8_t *buf, size_t len, uint8_t **frame)
{
    esp_h264_dec_in_frame_t in = {.raw_data = {.buffer = buf, .len = (uint32_t)len}};
    *frame = NULL;
    while (in.raw_data.len) {
        esp_h264_dec_out_frame_t out;
        memset(&out, 0, sizeof(out));
        if (esp_h264_dec_process(dec, &in, &out) != ESP_H264_ERR_OK) {
            return false;
        }
        if (out.out_size) {
            *frame = out.outbuf;
        }
        if (!in.consume) {
            break;
        }
        in.raw_data.buffer += in.consume;
        in.raw_data.len -= in.consume;
    }
    return true;
}

/**
 * @brief Two cores: this task (core 0) only decodes; the converter task (core 1, where
 * LVGL mostly just copies the picture while a video plays) takes each frame to
 * show straight from the decoder's output, finds the rows that changed (against
 * `prev`, its copy of the last frame it took), converts those to RGB565, waits
 * for the frame's time and hands it to the screen. A 720x720 replay frame takes
 * ~55-60 ms to decode (slower while core 1 also reads PSRAM) and ~50 ms to
 * compare and convert; they overlap instead of adding up. 15 fps allows 67 ms a
 * frame, so decoding alone nearly fills it: about 8 of the 15 frames a second
 * are shown (was 5 with everything on core 0), the rest decoded only.
 *
 * That works because the decoder (tinyh264, from h264bsd) writes each new
 * picture into another buffer than the one before (the reference it predicts
 * from), so frame N stays untouched while N+1 is decoded. Before a decode, the
 * converter must be done unless what it reads is the last frame out. This is
 * checked on the first frames (and all along): if a frame ever comes out in the
 * same buffer as the one before, every decode waits for the converter.
 */
typedef struct {
    const uint8_t *src;                 /**< I420, W x H planes (the decoder's output) */
    uint8_t *prev;                      /**< 1:1 only (else NULL): copy of the last frame taken */
    uint32_t *chg;                      /**< 1:1 only: each pair of rows' last change (frame number) */
    int W;                              /**< the planes' width, px */
    int H;                              /**< their height, px */
    int vw;                             /**< the picture's width (cropped), px */
    int vh;                             /**< its height, px */
    int dw;                             /**< shown at this width, px */
    int dh;                             /**< and height */
    uint32_t pos;                       /**< the frame's number */
    uint32_t pos_ms;                    /**< its time, ms */
    int64_t due;                        /**< when to show it (esp_timer us); 0: at once */
} cv_job_t;

static cv_job_t s_cv;                   /**< the job for the converter */
static SemaphoreHandle_t s_cv_go;             /**< a job is in s_cv */
static SemaphoreHandle_t s_cv_done;           /**< the converter is done reading src */
static volatile bool s_cv_quit;               /**< asks the converter to end */
static volatile bool s_cv_alive;              /**< the converter task runs */

/**
 * @brief The converter task (core 1): I420 -> RGB565 into the back buffer (only the rows that changed, 1:1), then hands it over when it's due.
 * @param[in] arg  unused
 */
static void converter(void *arg)
{
    int32_t buf_seq[2] = {-1, -1};      /* the frame each display buffer holds (-1: none of ours) */
    uint32_t seq = 0;
    bool prev_ok = false;
    while (!s_cv_quit) {
        if (xSemaphoreTake(s_cv_go, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        cv_job_t j = s_cv;
        seq++;
        if (j.prev) {                   /* which pairs of rows changed (a replay: few) */
            const uint8_t *fr = j.src;
            uint8_t *pv = j.prev;
            const size_t W = (size_t)j.W, ys = W * j.H, cs = (W / 2) * (j.H / 2);
            if (!prev_ok) {
                memcpy(pv, fr, ys + 2 * cs);
                for (int r = 0; r < j.H / 2; r++) {
                    j.chg[r] = seq;
                }
                prev_ok = true;
            } else {
                for (int r = 0; r < j.vh / 2; r++) {
                    const size_t yo = 2 * r * W, co = r * (W / 2);
                    if (memcmp(fr + yo, pv + yo, 2 * W) || memcmp(fr + ys + co, pv + ys + co, W / 2) ||
                            memcmp(fr + ys + cs + co, pv + ys + cs + co, W / 2)) {
                        memcpy(pv + yo, fr + yo, 2 * W);
                        memcpy(pv + ys + co, fr + ys + co, W / 2);
                        memcpy(pv + ys + cs + co, fr + ys + cs + co, W / 2);
                        j.chg[r] = seq;
                    }
                }
            }
        }
        if (!wait_free()) {             /* stopping */
            xSemaphoreGive(s_cv_done);
            continue;
        }
        int bk = 1 - s_front;
        i420_to_565(j.src, j.W, j.H, j.vw, j.vh, back_buf(), j.dw, j.dh, j.prev ? j.chg : NULL, buf_seq[bk]);
        buf_seq[bk] = (int32_t)seq;
        xSemaphoreGive(s_cv_done);      /* the decoder may reuse src now */
        if (j.due) {
            int64_t wait = j.due - esp_timer_get_time();
            if (wait > 2000) {
                vTaskDelay(pdMS_TO_TICKS(wait / 1000));
            }
        }
        if (s_stop) {
            continue;
        }
        publish(j.dw, j.dh);
        s_v_pos = j.pos;
        s_v_pos_ms = j.pos_ms;
    }
    s_cv_alive = false;
    vTaskDelete(NULL);
}

/**
 * @brief waits until the converter is done with its frame; false: stop
 * @return false: stop
 */
static bool cv_wait_done(void)
{
    while (xSemaphoreTake(s_cv_done, pdMS_TO_TICKS(20)) != pdTRUE) {
        if (s_stop) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Plays a video: decodes on this core, converts on the other, paced by the frames' times; seeks from the key frame before.
 * @param[in] path  the file
 */
static void do_video(const char *path)
{
    mp4r_t m;
    char err[96];
    uint8_t *inb = NULL, *prev = NULL;
    uint32_t *chg = NULL;
    esp_h264_dec_handle_t dec = NULL;
    bool opened = false, cv = false;
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_msg("Couldn't open the file.");
        return;
    }
    if (!mp4r_open(f, &m, err, sizeof(err))) {
        set_msg("%s", err);
        goto done;
    }
    opened = true;
    if (m.ps_len > 7) {                            /* SPS: start code, NAL header, profile, constraints */
        int profile = m.ps[5], cons = m.ps[6];
        if (profile != 66 && !(cons & 0x40)) {
            set_msg("This video is H.264 %s profile; only Baseline videos play here.",
                    profile == 77 ? "Main" : profile == 100 ? "High" : "a later");
            goto done;
        }
    }
    if (m.width * m.height > VIDEO_MAX_PX) {
        set_msg("The video is too big to play here (%dx%d).", m.width, m.height);
        goto done;
    }
    size_t cap = mp4r_annexb_cap(&m);
    inb = malloc(cap);
    esp_h264_dec_cfg_sw_t dcfg = {.pic_type = ESP_H264_RAW_FMT_I420};
    if (!inb || esp_h264_dec_sw_new(&dcfg, &dec) != ESP_H264_ERR_OK) {
        dec = NULL;
        set_msg("Out of memory.");
        goto done;
    }
    if (esp_h264_dec_open(dec) != ESP_H264_ERR_OK) {
        set_msg("The video decoder didn't start.");
        goto done;
    }
    if (!s_cv_go) {
        s_cv_go = xSemaphoreCreateBinary();
    }
    if (!s_cv_done) {
        s_cv_done = xSemaphoreCreateBinary();
    }
    if (!s_cv_go || !s_cv_done) {
        set_msg("Out of memory.");
        goto done;
    }
    xSemaphoreTake(s_cv_go, 0);                     /* (nothing left from the last video) */
    xSemaphoreTake(s_cv_done, 0);
    s_cv_quit = false;
    s_cv_alive = true;
    if (xTaskCreatePinnedToCore(converter, "media_cv", 4096, NULL, 1, NULL, 1) != pdPASS) {
        s_cv_alive = false;
        set_msg("Out of memory.");
        goto done;
    }
    cv = true;
    yuv_tables();
    uint8_t *fr;
    feed(dec, m.ps, m.ps_len, &fr);                /* SPS, PPS */
    s_v_n = m.n;
    s_v_dur_ms = (uint32_t)((uint64_t)m.duration * 1000 / m.timescale);
    s_v_pos = 0;
    s_v_pos_ms = 0;
    s_v_info = true;

    bool busy = false;                              /* a frame handed over, its done not yet taken */
    const uint8_t *handed = NULL, *last_out = NULL;
    int checked = 0;                                /* frames out so far, up to 3; until then: wait */
    bool sync = true;                               /* wait for the converter before every decode */
    int skipped = 0;
    const int64_t frame_us = m.n > 1 ? (int64_t)((uint64_t)m.duration * 1000000 / m.timescale / m.n) : 66666;

    uint32_t i = 0;
    int32_t target = -1;
    bool rebase = true, shown = false, res_known = false;
    int64_t t0 = 0;
    uint32_t tick0 = 0;
    int W = 0, H = 0, vw = 0, vh = 0, dw = 0, dh = 0;
    while (!s_stop) {
        int32_t sk = s_seek;
        if (sk >= 0) {
            s_seek = -1;
            if ((uint32_t)sk >= m.n) {
                sk = (int32_t)m.n - 1;
            }
            i = mp4r_key_before(&m, (uint32_t)sk);
            target = sk;
            s_ended = false;
        }
        if (target < 0) {
            if (i >= m.n) {                         /* the end: wait for play (from the start) or a seek */
                s_ended = true;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (!s_play) {
                rebase = true;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
        }
        size_t len = mp4r_read_annexb(f, &m, i, inb, cap);
        if (!len) {
            set_msg("Couldn't read frame %u of the video.", (unsigned)i + 1);
            break;
        }
        /* the decode writes into a buffer other than the last one out: safe unless
         * the converter reads an older one */
        if (busy && (sync || handed != last_out)) {
            if (!cv_wait_done()) {
                break;
            }
            busy = false;
        }
        if (!feed(dec, inb, len, &fr)) {
            if (!shown) {
                set_msg("This video can't be decoded here.");
                break;
            }
            fr = NULL;                              /* (a broken frame: go on) */
        }
        if (fr) {
            if (fr == last_out && !sync) {
                sync = true;
                printf("media: the decoder reused its last buffer; converting in step\n");
            } else if (sync && checked < 3 && (fr != last_out || !last_out) && ++checked == 3) {
                sync = false;                       /* 3 frames, each in another buffer */
            }
            last_out = fr;
        }
        if (fr && (target < 0 || (int32_t)i >= target)) {
            if (!res_known) {
                esp_h264_dec_param_sw_handle_t ph;
                esp_h264_resolution_t res = {0};
                if (esp_h264_dec_sw_get_param_hd(dec, &ph) == ESP_H264_ERR_OK) {
                    esp_h264_dec_get_resolution(ph, &res);
                }
                W = res.width ? res.width : m.width;
                H = res.height ? res.height : m.height;
                vw = m.width && m.width <= W ? m.width : W;
                vh = m.height && m.height <= H ? m.height : H;
                fit(vw, vh, &dw, &dh);
                res_known = true;
                if (dw == vw && dh == vh && !(dw & 1) && !(dh & 1)) {
                    prev = heap_caps_malloc((size_t)W * H * 3 / 2, MALLOC_CAP_SPIRAM);
                    chg = calloc((size_t)H / 2, sizeof(uint32_t));
                    if (!prev || !chg) {
                        heap_caps_free(prev);
                        free(chg);
                        prev = NULL;
                        chg = NULL;
                    }
                }
            }
            /* when it's due; behind time (the frames come slower than they last):
             * decode this one only, so the video keeps its speed and shows fewer
             * frames; at least every fourth one is shown. (Skipping already when
             * half a frame late dropped twice as many: decoding runs close to real
             * time, so lateness hovers, and a skip makes the next decode wait for
             * the converter.) */
            int64_t now = esp_timer_get_time(), due = 0;
            bool skip = false;
            if (target < 0) {
                if (rebase) {
                    t0 = now + frame_us;            /* (room for the conversion) */
                    tick0 = m.time[i];
                    rebase = false;
                }
                due = t0 + (int64_t)(m.time[i] - tick0) * 1000000 / m.timescale;
                if (now - due > 300000) {           /* far behind (a stall): carry on from here */
                    t0 = now + frame_us;
                    tick0 = m.time[i];
                    due = t0;
                }
                skip = skipped < 3 && now - due > 2 * frame_us;
            }
            if (skip) {
                skipped++;
            } else {
                skipped = 0;
                if (busy) {                         /* one frame at a time */
                    if (!cv_wait_done()) {
                        break;
                    }
                    busy = false;
                }
                s_cv = (cv_job_t) {
                    .src = fr, .prev = prev, .chg = chg, .W = W, .H = H, .vw = vw, .vh = vh, .dw = dw, .dh = dh,
                    .pos = i, .pos_ms = (uint32_t)((uint64_t)m.time[i] * 1000 / m.timescale), .due = due,
                };
                xSemaphoreGive(s_cv_go);
                busy = true;
                handed = fr;
                shown = true;
            }
        }
        if (target >= 0 && (int32_t)i >= target) {
            target = -1;
            rebase = true;
        }
        i++;
    }
done:
    if (cv) {                                       /* the converter ends before prev goes */
        s_cv_quit = true;
        while (s_cv_alive) {
            vTaskDelay(1);
        }
    }
    if (dec) {
        esp_h264_dec_close(dec);
        esp_h264_dec_del(dec);
    }
    free(inb);
    heap_caps_free(prev);
    free(chg);
    if (opened) {
        mp4r_free(&m);
    }
    fclose(f);
}

/**
 * @brief The worker task (core 0): the picture or the video, then it ends.
 * @param[in] arg  unused
 */
static void worker(void *arg)
{
    if (s_job_kind == MEDIA_IMAGE) {
        do_jpeg(s_job_path);
    } else {
        do_video(s_job_path);
    }
    s_running = false;
    vTaskDelete(NULL);
}


/* ------------------------------------------------------------- the page's side (documented in media_core.h) */
bool media_core_buffers(void)
{
    for (int k = 0; k < 2; k++) {
        if (!s_buf[k]) {
            s_buf[k] = heap_caps_aligned_alloc(64, BUF_BYTES, MALLOC_CAP_SPIRAM);
        }
    }
    return s_buf[0] && s_buf[1];
}

const uint16_t *media_core_buffer(int k)
{
    return s_buf[k];
}

bool media_core_start(const char *path, int kind)
{
    snprintf(s_job_path, sizeof(s_job_path), "%s", path);
    s_job_kind = kind;
    s_stop = false;
    s_pending = -1;
    s_v_info = false;
    s_ended = false;
    s_seek = -1;
    s_play = true;
    s_running = true;
    if (xTaskCreatePinnedToCore(worker, "media", 16384, NULL, 2, NULL, 0) != pdPASS) {
        s_running = false;
        return false;
    }
    return true;
}

void media_core_stop(void)
{
    s_stop = true;
}

bool is_media_core_running(void)
{
    return s_running;
}

int media_core_take_frame(int *w, int *h)
{
    int k = s_pending;
    if (k < 0 || s_stop) {
        return -1;
    }
    *w = s_pub_w[k];
    *h = s_pub_h[k];
    return k;
}

void media_core_frame_shown(int k)
{
    s_front = k;
    s_pending = -1;
}

uint32_t media_core_msg_seq(void)
{
    return s_msg_seq;
}

const char *media_core_msg(void)
{
    return s_msg;
}

void media_core_video(media_video_t *v)
{
    v->is_info = s_v_info;
    v->is_playing = s_play;
    v->is_ended = s_ended;
    v->is_seeking = s_seek >= 0;
    v->frames = s_v_n;
    v->pos = s_v_pos;
    v->pos_ms = s_v_pos_ms;
    v->dur_ms = s_v_dur_ms;
}

void media_core_seek(int32_t frame)
{
    s_seek = frame;
}

void media_core_play_toggle(void)
{
    if (s_ended) {
        s_ended = false;
        s_seek = 0;
        s_play = true;
    } else {
        s_play = !s_play;
    }
}

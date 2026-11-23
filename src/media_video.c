/**
 * @file media_video.c
 * @ingroup media
 * @brief The viewer's videos: mp4_reader gives each frame's place; frames go through tinyh264 as Annex-B and
 *        come out as I420, converted (and scaled to fit) to RGB565 by a converter task, paced by the frames'
 *        times; seeking decodes from the key frame before the target.
 */
#include "media_core.h"
#include "media_internal.h"
#include "mp4_reader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "esp_h264_dec_sw.h"

#define SCR        MEDIA_SCR            /**< the screen's size, px */
#define VIDEO_MAX_PX   (1920 * 1088)    /**< the largest video picture, px */

/* the video's state, for the page (media_core_video()) */
static volatile bool s_v_info;                /**< the video's frames and length are known */
static volatile bool s_play;                  /**< playing */
static volatile bool s_ended;                 /**< it played to the end */
static volatile uint32_t s_v_n;               /**< its frames */
static volatile uint32_t s_v_pos;             /**< the frame shown */
static volatile uint32_t s_v_pos_ms;          /**< its time, ms */
static volatile uint32_t s_v_dur_ms;          /**< the video's length, ms */
static volatile int32_t s_seek = -1;          /**< the frame to seek to (-1: none) */

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
    const uint16_t *xmap = media_make_xmap(vw, dw);
    for (int y = 0; y < dh; y++) {
        int sy = media_src_row(y, vh, dh);
        const uint8_t *yr = yp + (size_t)sy * W, *ur = up + (size_t)(sy / 2) * (W / 2), *vr = vp + (size_t)(sy / 2) * (W / 2);
        uint16_t *o = dst + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            int sx = xmap[x];
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
        if (!media_wait_free()) {             /* stopping */
            xSemaphoreGive(s_cv_done);
            continue;
        }
        int bk = media_back_index();
        i420_to_565(j.src, j.W, j.H, j.vw, j.vh, media_back_buf(), j.dw, j.dh, j.prev ? j.chg : NULL, buf_seq[bk]);
        buf_seq[bk] = (int32_t)seq;
        xSemaphoreGive(s_cv_done);      /* the decoder may reuse src now */
        if (j.due) {
            int64_t wait = j.due - esp_timer_get_time();
            if (wait > 2000) {
                vTaskDelay(pdMS_TO_TICKS(wait / 1000));
            }
        }
        if (is_media_stopping()) {
            continue;
        }
        media_publish(j.dw, j.dh);
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
        if (is_media_stopping()) {
            return false;
        }
    }
    return true;
}

/* (documented in media_internal.h) */
void media_video_run(const char *path)
{
    mp4r_t m;
    char err[96];
    uint8_t *inb = NULL, *prev = NULL;
    uint32_t *chg = NULL;
    esp_h264_dec_handle_t dec = NULL;
    bool opened = false, cv = false;
    FILE *f = fopen(path, "rb");
    if (!f) {
        media_set_msg("Couldn't open the file.");
        return;
    }
    if (!mp4r_open(f, &m, err, sizeof(err))) {
        media_set_msg("%s", err);
        goto done;
    }
    opened = true;
    if (m.ps_len > 7) {                            /* SPS: start code, NAL header, profile, constraints */
        int profile = m.ps[5], cons = m.ps[6];
        if (profile != 66 && !(cons & 0x40)) {
            media_set_msg("This video is H.264 %s profile; only Baseline videos play here.",
                          profile == 77 ? "Main" : profile == 100 ? "High" : "a later");
            goto done;
        }
    }
    if (m.width * m.height > VIDEO_MAX_PX) {
        media_set_msg("The video is too big to play here (%dx%d).", m.width, m.height);
        goto done;
    }
    size_t cap = mp4r_annexb_cap(&m);
    inb = malloc(cap);
    esp_h264_dec_cfg_sw_t dcfg = {.pic_type = ESP_H264_RAW_FMT_I420};
    if (!inb || esp_h264_dec_sw_new(&dcfg, &dec) != ESP_H264_ERR_OK) {
        dec = NULL;
        media_set_msg("Out of memory.");
        goto done;
    }
    if (esp_h264_dec_open(dec) != ESP_H264_ERR_OK) {
        media_set_msg("The video decoder didn't start.");
        goto done;
    }
    if (!s_cv_go) {
        s_cv_go = xSemaphoreCreateBinary();
    }
    if (!s_cv_done) {
        s_cv_done = xSemaphoreCreateBinary();
    }
    if (!s_cv_go || !s_cv_done) {
        media_set_msg("Out of memory.");
        goto done;
    }
    xSemaphoreTake(s_cv_go, 0);                     /* (nothing left from the last video) */
    xSemaphoreTake(s_cv_done, 0);
    s_cv_quit = false;
    s_cv_alive = true;
    if (xTaskCreatePinnedToCore(converter, "media_cv", 4096, NULL, 1, NULL, 1) != pdPASS) {
        s_cv_alive = false;
        media_set_msg("Out of memory.");
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
    while (!is_media_stopping()) {
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
            media_set_msg("Couldn't read frame %u of the video.", (unsigned)i + 1);
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
                media_set_msg("This video can't be decoded here.");
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
                media_fit(vw, vh, &dw, &dh);
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


/* (documented in media_internal.h) */
void media_video_reset(void)
{
    s_v_info = false;
    s_ended = false;
    s_seek = -1;
    s_play = true;
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

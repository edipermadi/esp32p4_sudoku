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
#include "media_internal.h"
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

#define SCR        MEDIA_SCR            /**< the screen's size, px */
#define BUF_BYTES  (SCR * SCR * 2)      /**< a picture buffer's bytes (RGB565) */
#define NAME_B     256                  /**< a file name's room */
#define PATH_B     512                  /**< a path's room */

/* ------------------------------------------------------------- state */
uint16_t media_xmap[MEDIA_SCR];        /**< each output column's source column (media_make_xmap()) */
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
void media_set_msg(const char *fmt, ...)
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
bool media_wait_free(void)
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
uint16_t *media_back_buf(void)
{
    return s_buf[1 - s_front];
}

/**
 * @brief Hands the back buffer over to the page (flushed from the cache for the PPA's DMA).
 * @param[in] w  the picture's width, px
 * @param[in] h  its height
 */
void media_publish(int w, int h)
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
void media_fit(int w, int h, int *dw, int *dh)
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


/* (documented in media_internal.h) */
void media_make_xmap(int sw, int dw)
{
    for (int x = 0; x < dw; x++) {
        media_xmap[x] = (uint16_t)(((int64_t)x * 2 + 1) * sw / (2 * dw));
    }
}


/**
 * @brief The worker task (core 0): the picture or the video, then it ends.
 * @param[in] arg  unused
 */
static void worker(void *arg)
{
    if (s_job_kind == MEDIA_IMAGE) {
        media_jpeg_run(s_job_path);
    } else {
        media_video_run(s_job_path);
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
    media_video_reset();
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

/* (documented in media_internal.h) */
bool is_media_stopping(void)
{
    return s_stop;
}

/* (documented in media_internal.h) */
int media_back_index(void)
{
    return 1 - s_front;
}

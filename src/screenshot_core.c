/**
 * @file screenshot_core.c
 * @ingroup screenshot_core
 * @brief The screen as a JPEG in screenshots/ on the card, without LVGL (the camera buttons: ui_screenshot.c).
 *
 * The frame is copied at once (the screen keeps going); a worker task has the hardware JPEG encoder
 * compress it (by DMA, full colour resolution, quality SS_QUALITY: about 100-200 KB instead of a
 * 1.5 MB uncompressed PNG) and writes it. The encoder gets RGB888, widened from RGB565 here: given
 * RGB565 it widens by shifting alone, and white came out (236, 245, 244), with a green cast.
 */
#include "screenshot_core.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/jpeg_encode.h"
#include "bsp/bsp_board.h"
#include "timezones.h"

#define SS_QUALITY 90                   /**< the screenshot's JPEG quality */
#define SS_OUT_MAX (1024 * 1024)        /**< the JPEG's room, bytes */
#define SS_CORE    0                    /**< the core the task runs on (WORKER_CORE) */

static void ss_task(void *arg);

static uint16_t *ss_frame;                          /**< the copied frame, RGB565 (PSRAM, 64-byte aligned: the encoder reads it by DMA) */
static volatile int ss_state;                       /**< SCREENSHOT_* */
static char ss_path[64];                            /**< the file being written */

/**
 * @brief The screenshot task: the frame widened to RGB888, compressed by the JPEG encoder and
 *        written to ss_path (deleted if it fails half way); ss_state says how it went, then it ends.
 * @param[in] arg  unused
 */
static void ss_task(void *arg)
{
    bool ok = false;
    jpeg_encoder_handle_t enc = NULL;
    jpeg_encode_engine_cfg_t ecfg = {.timeout_ms = 1000};
    jpeg_encode_memory_alloc_cfg_t mcfg = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    size_t out_cap = 0;
    uint8_t *out = jpeg_alloc_encoder_mem(SS_OUT_MAX, &mcfg, &out_cap);
    uint8_t *rgb = heap_caps_aligned_alloc(64, (size_t)SCREENSHOT_W * SCREENSHOT_W * 3, MALLOC_CAP_SPIRAM);
    esp_err_t err = out && rgb ? jpeg_new_encoder_engine(&ecfg, &enc) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        for (size_t i = 0; i < (size_t)SCREENSHOT_W * SCREENSHOT_W; i++) {   /* RGB565 -> bytes B, G, R */
            uint16_t px = ss_frame[i];
            int r = px >> 11, g = (px >> 5) & 0x3F, b = px & 0x1F;
            rgb[i * 3] = (uint8_t)(b << 3 | b >> 2);
            rgb[i * 3 + 1] = (uint8_t)(g << 2 | g >> 4);
            rgb[i * 3 + 2] = (uint8_t)(r << 3 | r >> 2);
        }
        jpeg_encode_cfg_t cfg = {
            .width = SCREENSHOT_W,
            .height = SCREENSHOT_W,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV444,   /* sharp coloured text and lines */
            .image_quality = SS_QUALITY,
        };
        uint32_t len = 0;
        err = jpeg_encoder_process(enc, &cfg, rgb, SCREENSHOT_W * SCREENSHOT_W * 3, out, out_cap, &len);
        if (err == ESP_OK && len > 0) {
            FILE *f = fopen(ss_path, "wb");
            if (f) {
                ok = fwrite(out, 1, len, f) == len;
                ok = fclose(f) == 0 && ok;
                if (!ok) {
                    unlink(ss_path);
                }
            }
        }
    }
    if (err != ESP_OK) {
        printf("screenshot: JPEG encoder: %s\n", esp_err_to_name(err));
    }
    if (enc) {
        jpeg_del_encoder_engine(enc);
    }
    if (out) {
        heap_caps_free(out);
    }
    heap_caps_free(rgb);
    ss_state = ok ? SCREENSHOT_DONE : SCREENSHOT_FAILED;
    vTaskDelete(NULL);
}

/* (documented in screenshot_core.h) */
bool screenshot_start(const uint16_t *fb)
{
    if (ss_state != SCREENSHOT_IDLE || !fb) {
        return false;
    }
    if (!ss_frame) {
        ss_frame = heap_caps_aligned_alloc(64, (size_t)SCREENSHOT_W * SCREENSHOT_W * 2, MALLOC_CAP_SPIRAM);
    }
    if (!ss_frame) {
        return false;
    }
    memcpy(ss_frame, fb, (size_t)SCREENSHOT_W * SCREENSHOT_W * 2);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    mkdir(BSP_SD_MOUNT_POINT "/screenshots", 0775);
    if (CLOCK_VALID(now)) {
        strftime(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/%Y%m%d-%H%M%S.jpg", &tm);
    } else
        snprintf(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/shot-%lu.jpg",
                 (unsigned long)(esp_timer_get_time() / 1000000));
    ss_state = SCREENSHOT_WRITING;
    if (xTaskCreatePinnedToCore(ss_task, "screenshot", 4096, NULL, 2, NULL, SS_CORE) != pdPASS) {
        ss_state = SCREENSHOT_IDLE;
        return false;
    }
    return true;
}

/* (documented in screenshot_core.h) */
int screenshot_state(void)
{
    return ss_state;
}

/* (documented in screenshot_core.h) */
const char *screenshot_path(void)
{
    return ss_path;
}

/* (documented in screenshot_core.h) */
void screenshot_reset(void)
{
    if (ss_state >= SCREENSHOT_DONE) {
        ss_state = SCREENSHOT_IDLE;
    }
}

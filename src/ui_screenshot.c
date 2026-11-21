/**
 * @file ui_screenshot.c
 * @brief The camera buttons (Solved dialog, Replay): the screen as a JPEG in screenshots/ on the card
 *        (was in ui_replay.c).
 */
#include "ui_internal.h"

static void ss_task(void *arg);
static void ss_poll_cb(lv_timer_t *t);

static uint16_t *ss_frame;                          /* 64-byte aligned: the encoder reads it by DMA */
static volatile int ss_state;                       /* 0 idle, 1 writing, 2 done, 3 failed */
static char ss_path[64];
static lv_timer_t *ss_timer;

static void ss_task(void *arg)
{
    bool ok = false;
    jpeg_encoder_handle_t enc = NULL;
    jpeg_encode_engine_cfg_t ecfg = {.timeout_ms = 1000};
    jpeg_encode_memory_alloc_cfg_t mcfg = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    size_t out_cap = 0;
    uint8_t *out = jpeg_alloc_encoder_mem(SS_OUT_MAX, &mcfg, &out_cap);
    uint8_t *rgb = heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_W * 3, MALLOC_CAP_SPIRAM);
    esp_err_t err = out && rgb ? jpeg_new_encoder_engine(&ecfg, &enc) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        for (size_t i = 0; i < (size_t)SCR_W * SCR_W; i++) {   /* RGB565 -> bytes B, G, R */
            uint16_t px = ss_frame[i];
            int r = px >> 11, g = (px >> 5) & 0x3F, b = px & 0x1F;
            rgb[i * 3] = (uint8_t)(b << 3 | b >> 2);
            rgb[i * 3 + 1] = (uint8_t)(g << 2 | g >> 4);
            rgb[i * 3 + 2] = (uint8_t)(r << 3 | r >> 2);
        }
        jpeg_encode_cfg_t cfg = {
            .width = SCR_W,
            .height = SCR_W,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV444,   /* sharp coloured text and lines */
            .image_quality = SS_QUALITY,
        };
        uint32_t len = 0;
        err = jpeg_encoder_process(enc, &cfg, rgb, SCR_W * SCR_W * 3, out, out_cap, &len);
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
    ss_state = ok ? 2 : 3;
    vTaskDelete(NULL);
}

static void ss_poll_cb(lv_timer_t *t)
{
    if (ss_state < 2) {
        return;
    }
    lv_timer_delete(ss_timer);
    ss_timer = NULL;
    char msg[120];
    if (ss_state == 2)
        snprintf(msg, sizeof(msg), "Saved %s. Copy it off with Settings > USB drive.",
                 ss_path + strlen(BSP_SD_MOUNT_POINT) + 1);
    else {
        snprintf(msg, sizeof(msg), "Couldn't save the screenshot.");
    }
    printf("screenshot: %s\n", msg);
    ss_state = 0;
    sudoku_ui_notice(msg, 4000);
}

void screenshot_cb(lv_event_t *e)
{
    if (ss_state) {
        return;    /* (one at a time) */
    }
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("Screenshots go on the microSD card: there's none to write to.", 3000);
        return;
    }
    if (!ss_frame) {
        ss_frame = heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_W * 2, MALLOC_CAP_SPIRAM);
    }
    lv_refr_now(NULL);
    const uint16_t *fb = bsp_display_frame();
    if (!ss_frame || !fb) {
        return;
    }
    memcpy(ss_frame, fb, (size_t)SCR_W * SCR_W * 2);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    mkdir(BSP_SD_MOUNT_POINT "/screenshots", 0775);
    if (now > 1700000000) {
        strftime(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/%Y%m%d-%H%M%S.jpg", &tm);
    } else
        snprintf(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/shot-%lu.jpg",
                 (unsigned long)(esp_timer_get_time() / 1000000));
    ss_state = 1;
    if (xTaskCreatePinnedToCore(ss_task, "screenshot", 4096, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        ss_state = 0;
        return;
    }
    ss_timer = lv_timer_create(ss_poll_cb, 200, NULL);
}

/* ---- accessors: the other UI files reach this file's state through these */
bool is_screenshot_busy(void) { return ss_state != 0; }               /* a screenshot being written */

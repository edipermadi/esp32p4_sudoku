/**
 * @file ui_splash.c
 * @ingroup ui_sudoku
 * @brief The start-up splash screen: extras/splashscreen.png (as a JPEG in splash_data.c, made by
 *        tools/gen_splash.py) decoded by the hardware JPEG decoder and shown while the card and
 *        sudoku.db are read and the screens are built (sudoku_ui_start()).
 */
#include "ui_internal.h"
#include "splash_data.h"
#include "driver/jpeg_decode.h"

static uint8_t *s_px;                   /**< the picture, RGB565 (PSRAM, from the decoder's allocator); NULL: none */
static lv_image_dsc_t s_dsc;            /**< it as an LVGL image */
static lv_obj_t *s_img;                 /**< the image shown */

/* (documented in ui_internal.h) */
lv_obj_t *splash_show(lv_obj_t *parent)
{
    const int64_t t0 = esp_timer_get_time();
    size_t len = 0, in_cap = 0, out_cap = 0;
    const uint8_t *jpg = splash_jpeg(&len);
    /* the decoder reads its input by DMA too: a copy in its own buffer (the JPEG is in flash) */
    jpeg_decode_memory_alloc_cfg_t in_cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    jpeg_decode_memory_alloc_cfg_t out_cfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    uint8_t *in = jpeg_alloc_decoder_mem(len, &in_cfg, &in_cap);
    s_px = jpeg_alloc_decoder_mem((size_t)SPLASH_SIZE * SPLASH_SIZE * 2, &out_cfg, &out_cap);
    jpeg_decoder_handle_t dec = NULL;
    jpeg_decode_engine_cfg_t ecfg = {.timeout_ms = 1000};
    esp_err_t err = in && s_px ? jpeg_new_decoder_engine(&ecfg, &dec) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        memcpy(in, jpg, len);
        jpeg_decode_cfg_t cfg = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,   /* (little-endian RGB565: LVGL's) */
            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
        };
        uint32_t got = 0;
        err = jpeg_decoder_process(dec, &cfg, in, (uint32_t)len, s_px, (uint32_t)out_cap, &got);
        jpeg_del_decoder_engine(dec);
    }
    free(in);
    if (err != ESP_OK) {
        printf("splash: not shown (%s)\n", esp_err_to_name(err));
        free(s_px);
        s_px = NULL;
        return NULL;
    }
    s_dsc = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .w = SPLASH_SIZE,
            .h = SPLASH_SIZE,
            .stride = SPLASH_SIZE * 2,
        },
        .data_size = (uint32_t)SPLASH_SIZE * SPLASH_SIZE * 2,
        .data = s_px,
    };
    s_img = lv_image_create(parent);
    lv_image_set_src(s_img, &s_dsc);
    lv_obj_center(s_img);
    printf("splash: decoded in %u ms\n", (unsigned)((esp_timer_get_time() - t0) / 1000));
    return s_img;
}

/* (documented in ui_internal.h) */
void splash_hide(void)
{
    if (s_img) {
        lv_obj_delete(s_img);
        s_img = NULL;
    }
    if (s_px) {                                     /* (lv_conf.h has no image cache: nothing else points at them) */
        free(s_px);                                 /* 1 MB of PSRAM back */
        s_px = NULL;
    }
}

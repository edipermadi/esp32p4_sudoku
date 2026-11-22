/**
 * @file ui_splash.c
 * @ingroup ui_sudoku
 * @brief The start-up splash screen: extras/splashscreen.png (as a JPEG in splash_data.c, made by
 *        tools/gen_splash.py) decoded by the hardware JPEG decoder and shown while the card and
 *        sudoku.db are read and the screens are built (sudoku_ui_start()), its loading bar filling
 *        as they go.
 *
 * The bar: the picture's is empty (the script erased its painted fill); the fill's watercolour
 * texture (splash_bar()) is shown over it through a box with round ends, as wide as start-up has
 * got. Start-up has no clock of its own to go by, so the steps are weighted by how long they took
 * on the board (2026-11-22): the card 2 %, sudoku.db about half (it creeps while it opens), the
 * screens the rest, each by its own time (SPLASH_BUILD_MS).
 */
#include "ui_internal.h"
#include "splash_data.h"
#include "driver/jpeg_decode.h"

static uint8_t *s_px;                   /**< the picture, RGB565 (PSRAM, from the decoder's allocator); NULL: none */
static lv_image_dsc_t s_dsc;            /**< it as an LVGL image */
static lv_obj_t *s_img;                 /**< the image shown */
static lv_obj_t *s_bar;                 /**< the loading bar's fill: a box with round ends, its width the progress */
static lv_image_dsc_t s_bar_dsc;        /**< the fill's texture as an LVGL image */
static int s_pct;                       /**< the progress shown, 0..100 */

/** @brief The screens apply_theme() builds, in its order, and about how long each took at start-up on
 * the board, ms (2026-11-22): the bar's share of each, from SPLASH_SCREENS_FROM % to 100 %. */
static const uint16_t SPLASH_BUILD_MS[] = {
    176,                                /* game */
    85,                                 /* list */
    57,                                 /* home */
    230,                                /* statistics */
    166,                                /* settings */
    21,                                 /* Wi-Fi */
    51,                                 /* system monitor */
    27,                                 /* files */
    4,                                  /* players */
    7,                                  /* history */
    3,                                  /* gameplays */
    13,                                 /* trophies */
    59,                                 /* the refreshes after them */
};

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
    printf("splash: decoded in %u ms\n", (unsigned)((esp_timer_get_time() - t0) / 1000));
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
    /* the bar's fill: the texture, left-aligned, in a box that clips it to round ends */
    s_bar_dsc = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .w = SPLASH_BAR_W,
            .h = SPLASH_BAR_H,
            .stride = SPLASH_BAR_W * 2,
        },
        .data_size = (uint32_t)SPLASH_BAR_W * SPLASH_BAR_H * 2,
        .data = (const uint8_t *)splash_bar(),
    };
    s_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_pos(s_bar, SPLASH_BAR_X, SPLASH_BAR_Y);
    lv_obj_set_size(s_bar, 0, SPLASH_BAR_H);
    lv_obj_set_style_radius(s_bar, SPLASH_BAR_H / 2, 0);
    lv_obj_set_style_clip_corner(s_bar, true, 0);
    lv_obj_set_scrollbar_mode(s_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *tex = lv_image_create(s_bar);
    lv_image_set_src(tex, &s_bar_dsc);
    lv_obj_set_pos(tex, 0, 0);
    s_pct = -1;
    splash_progress(SPLASH_SHOWN);
    return s_img;
}

/* (documented in ui_internal.h) */
void splash_progress(int pct)
{
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    if (!s_bar || pct == s_pct) {
        return;
    }
    s_pct = pct;
    /* at least as wide as it is tall, so its round ends meet (a dot, not a sliver) */
    int w = SPLASH_BAR_W * pct / 100;
    lv_obj_set_width(s_bar, w < SPLASH_BAR_H ? SPLASH_BAR_H : w);
    lv_refr_now(NULL);                              /* (start-up: the LVGL loop isn't running yet) */
}

/* (documented in ui_internal.h) */
void splash_built(int step)
{
    const int n = (int)(sizeof(SPLASH_BUILD_MS) / sizeof(SPLASH_BUILD_MS[0]));
    int done = 0, all = 0;
    for (int i = 0; i < n; i++) {
        all += SPLASH_BUILD_MS[i];
        done += i < step ? SPLASH_BUILD_MS[i] : 0;
    }
    if (s_bar && step >= 1 && step <= n) {
        splash_progress(SPLASH_SCREENS_FROM + (100 - SPLASH_SCREENS_FROM) * done / all);
    }
}

/* (documented in ui_internal.h) */
void splash_hide(void)
{
    if (s_img) {
        lv_obj_delete(s_img);
        s_img = NULL;
    }
    if (s_bar) {
        lv_obj_delete(s_bar);
        s_bar = NULL;
    }
    if (s_px) {                                     /* (lv_conf.h has no image cache: nothing else points at them) */
        free(s_px);                                 /* 1 MB of PSRAM back */
        s_px = NULL;
    }
}

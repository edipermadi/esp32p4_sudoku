/**
 * @file ui_saver_life.c
 * @ingroup ui_sudoku
 * @brief The Game of Life screen saver: LIFE_N x LIFE_N cells (life_core.c), each a round dot of a
 *        vacuum fluorescent display's blue-green on black, the unlit dots faintly there as on a real one.
 *
 * The picture is a full-screen RGB565 image in PSRAM; each generation repaints only the dots whose
 * glow changed, flushes those rows from the cache (LVGL's PPA draw unit reads it by DMA) and redraws.
 */
#include "ui_internal.h"
#include "life_core.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"

#define LIFE_PX         (SCR_W / LIFE_N)        /**< a cell's size, px (6 on the 720 px screen) */
#define LIFE_ROW_BYTES  (SCR_W * 2)             /**< a row of the picture, bytes */
#define LIFE_BYTES      (SCR_W * SCR_W * 2)     /**< the picture, bytes */
#define LIFE_STEP_MS    125                     /**< a generation each, ms (8 a second) */

static uint16_t *lf_buf;                  /**< the picture (RGB565, 64-byte aligned, PSRAM) */
static uint8_t *lf_shown;                 /**< each cell's glow as drawn (0xFF: not drawn yet) */
static lv_image_dsc_t lf_dsc;             /**< lf_buf as an image */
static lv_obj_t *lf_img;                  /**< the image on the saver's cover */
static lv_timer_t *lf_timer;              /**< a generation each LIFE_STEP_MS */

/**
 * @brief The colour of each glow, RGB565: a VFD's blue-green phosphor, white-hot when just born, an
 *        afterglow when dead, and the unlit dot barely there.
 * @param[in] g  a life_glow_t
 * @return the colour
 */
static uint16_t glow_color(uint8_t g)
{
    static const uint32_t HEX[LIFE_GLOW_COUNT] = {
        0x041411,                                   /* unlit */
        0x072620, 0x0D4237, 0x16705D, 0x24A98C,     /* fading out */
        0x3CF0C8,                                   /* alive */
        0xC4FFF2,                                   /* just born */
    };
    return lv_color_to_u16(lv_color_hex(HEX[g < LIFE_GLOW_COUNT ? g : 0]));
}

/**
 * @brief Paints a cell's dot: 5 x 5 px with the corners off, and a 1 px black gap to the next.
 * @param[in] x  column
 * @param[in] y  row
 * @param[in] c  the colour (RGB565)
 */
static void paint_cell(int x, int y, uint16_t c)
{
    uint16_t *p = lf_buf + (y * LIFE_PX) * SCR_W + x * LIFE_PX;
    for (int r = 0; r < LIFE_PX - 1; r++, p += SCR_W) {
        bool edge = r == 0 || r == LIFE_PX - 2;
        for (int k = 0; k < LIFE_PX - 1; k++) {
            p[k] = edge && (k == 0 || k == LIFE_PX - 2) ? 0 : c;
        }
    }
}

/**
 * @brief Repaints the dots whose glow changed, flushes their rows from the cache and has them redrawn.
 */
static void life_paint(void)
{
    uint16_t col[LIFE_GLOW_COUNT];
    for (int g = 0; g < LIFE_GLOW_COUNT; g++) {
        col[g] = glow_color((uint8_t)g);
    }
    int y0 = LIFE_N, y1 = -1;
    for (int y = 0; y < LIFE_N; y++) {
        for (int x = 0; x < LIFE_N; x++) {
            uint8_t g = life_glow(x, y);
            uint8_t *s = &lf_shown[y * LIFE_N + x];
            if (*s != g) {
                *s = g;
                paint_cell(x, y, col[g]);
                y0 = LV_MIN(y0, y);
                y1 = y;
            }
        }
    }
    if (y1 < 0) {
        return;
    }
    size_t from = (size_t)y0 * LIFE_PX * LIFE_ROW_BYTES, to = (size_t)(y1 + 1) * LIFE_PX * LIFE_ROW_BYTES;
    from &= ~(size_t)63;                            /* (the cache works in 64-byte lines) */
    to = LV_MIN((to + 63) & ~(size_t)63, (size_t)LIFE_BYTES);
    esp_cache_msync((uint8_t *)lf_buf + from, to - from, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    lv_area_t a = {0, (int32_t)(y0 * LIFE_PX), SCR_W - 1, (int32_t)((y1 + 1) * LIFE_PX - 1)};
    lv_obj_invalidate_area(lf_img, &a);
}

/**
 * @brief A generation, and the dots that changed repainted.
 * @param[in] t  the timer
 */
static void life_tick(lv_timer_t *t)
{
    life_step();
    life_paint();
}

/* (documented in ui_internal.h) */
bool saver_life_start(lv_obj_t *parent)
{
    if (!lf_buf) {
        lf_buf = heap_caps_aligned_alloc(64, LIFE_BYTES, MALLOC_CAP_SPIRAM);
    }
    if (!lf_shown) {
        lf_shown = malloc(LIFE_CELLS);
    }
    if (!lf_buf || !lf_shown || !life_seed(esp_random())) {
        saver_life_stop();
        return false;
    }
    memset(lf_buf, 0, LIFE_BYTES);                  /* the gaps between the dots: black */
    memset(lf_shown, 0xFF, LIFE_CELLS);             /* every dot to paint */
    memset(&lf_dsc, 0, sizeof(lf_dsc));
    lf_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    lf_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    lf_dsc.header.w = SCR_W;
    lf_dsc.header.h = SCR_W;
    lf_dsc.header.stride = LIFE_ROW_BYTES;
    lf_dsc.data = (const uint8_t *)lf_buf;
    lf_dsc.data_size = LIFE_BYTES;
    lf_img = lv_image_create(parent);
    lv_obj_set_pos(lf_img, 0, 0);
    lv_obj_set_clickable(lf_img, false);
    lv_image_set_src(lf_img, &lf_dsc);
    life_paint();
    lf_timer = lv_timer_create(life_tick, LIFE_STEP_MS, NULL);
    return true;
}

/* (documented in ui_internal.h) */
void saver_life_stop(void)
{
    if (lf_timer) {
        lv_timer_delete(lf_timer);
        lf_timer = NULL;
    }
    if (lf_img) {
        lv_obj_delete(lf_img);                      /* (before its picture goes) */
        lf_img = NULL;
    }
    heap_caps_free(lf_buf);
    lf_buf = NULL;
    free(lf_shown);
    lf_shown = NULL;
    life_free();
}

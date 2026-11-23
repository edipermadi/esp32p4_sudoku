/**
 * @file ui_saver_rain.c
 * @ingroup ui_sudoku
 * @brief The digital rain screen saver: falling trails of the digits 1 to 9 (rain_core.c) in a vacuum
 *        fluorescent display's blue-green on black, each trail's head white-hot.
 *
 * The digits are drawn once, at the start, into tiles of each brightness (a canvas, then its pixels read
 * as coverage); each step copies the tiles of the cells that changed into the savers' picture
 * (ui_saver_pic.c) and redraws only each column's changed span (30 columns: within LVGL's 32 areas).
 */
#include "ui_internal.h"
#include "rain_core.h"
#include "esp_heap_caps.h"

#define RAIN_PX         (SCR_W / RAIN_COLS)     /**< a cell's size, px (24) */
#define RAIN_TILE       (RAIN_PX * RAIN_PX)     /**< a tile's pixels */
#define RAIN_STEP_MS    50                      /**< a step each, ms (20 a second) */

static uint16_t *rn_buf;                  /**< the picture (saver_pic_start()) */
static uint16_t *rn_tiles;                /**< the tiles: [level-1][digit-1][RAIN_TILE], RGB565 */
static uint8_t *rn_shown;                 /**< each cell as drawn: level << 4 | digit (0xFF: not yet) */
static lv_timer_t *rn_timer;              /**< a step each RAIN_STEP_MS */

/**
 * @brief The colour of each level: the VFD blue-green, dimmer down the trail, the head white-hot.
 * @param[in] level  1..RAIN_HEAD
 * @return the colour
 */
static lv_color_t level_color(int level)
{
    static const uint32_t HEX[RAIN_LEVELS] = {
        0x000000,                                   /* (unlit: not drawn as a tile) */
        0x06221C, 0x0B3D33, 0x146B59, 0x1F9278, 0x2CC09E, 0x3CF0C8,   /* the trail, dim to bright */
        0xC4FFF2,                                   /* the head */
    };
    return lv_color_hex(HEX[level]);
}

/**
 * @brief Draws the digits 1 to 9 once and makes a tile of each in each colour (coverage x colour).
 * @return false if there was no memory for it
 */
static bool make_tiles(void)
{
    static const char *const DIGITS[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    const int32_t w = 9 * RAIN_PX;
    uint16_t *glyphs = heap_caps_aligned_alloc(64, (size_t)w * RAIN_PX * 2, MALLOC_CAP_SPIRAM);   /* (a canvas) */
    rn_tiles = malloc((size_t)RAIN_HEAD * 9 * RAIN_TILE * 2);
    if (!glyphs || !rn_tiles) {
        free(glyphs);
        return false;
    }
    lv_obj_t *cv = lv_canvas_create(lv_layer_sys());   /* (off-screen use only: drawn into, then gone) */
    lv_obj_set_hidden(cv, true);
    lv_canvas_set_buffer(cv, glyphs, w, RAIN_PX, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(cv, &layer);
    const lv_font_t *f = &lv_font_montserrat_22;
    int32_t top = (RAIN_PX - lv_font_get_line_height(f)) / 2;
    for (int d = 0; d < 9; d++) {
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.color = lv_color_white();
        dsc.font = f;
        dsc.text = DIGITS[d];
        dsc.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t a = {d * RAIN_PX, top, d * RAIN_PX + RAIN_PX - 1, RAIN_PX - 1};
        lv_draw_label(&layer, &dsc, &a);
    }
    lv_canvas_finish_layer(cv, &layer);
    lv_obj_delete(cv);
    for (int lv = 1; lv <= RAIN_HEAD; lv++) {
        lv_color_t c = level_color(lv);
        for (int d = 0; d < 9; d++) {
            uint16_t *t = rn_tiles + ((lv - 1) * 9 + d) * RAIN_TILE;
            for (int y = 0; y < RAIN_PX; y++) {
                for (int x = 0; x < RAIN_PX; x++) {
                    uint16_t g = glyphs[y * w + d * RAIN_PX + x];
                    uint32_t cov = ((g >> 5) & 0x3F) * 255u / 63u;    /* white on black: green is coverage */
                    t[y * RAIN_PX + x] = lv_color_to_u16(lv_color_make((uint8_t)(c.red * cov / 255u),
                                                         (uint8_t)(c.green * cov / 255u),
                                                         (uint8_t)(c.blue * cov / 255u)));
                }
            }
        }
    }
    free(glyphs);
    return true;
}

/**
 * @brief Paints a cell: its digit's tile at its level, or black when unlit.
 * @param[in] col  column
 * @param[in] row  row
 * @param[in] level  0..RAIN_HEAD
 * @param[in] digit  1..9
 */
static void paint_cell(int col, int row, uint8_t level, uint8_t digit)
{
    uint16_t *p = rn_buf + (row * RAIN_PX) * SCR_W + col * RAIN_PX;
    const uint16_t *t = level ? rn_tiles + ((level - 1) * 9 + (digit - 1)) * RAIN_TILE : NULL;
    for (int y = 0; y < RAIN_PX; y++, p += SCR_W) {
        if (t) {
            memcpy(p, t + y * RAIN_PX, RAIN_PX * 2);
        } else {
            memset(p, 0, RAIN_PX * 2);
        }
    }
}

/**
 * @brief Repaints the cells that changed, flushes their rows from the cache and redraws each column's
 *        changed span.
 */
static void rain_paint(void)
{
    int32_t y0 = RAIN_ROWS, y1 = -1;
    int32_t span0[RAIN_COLS], span1[RAIN_COLS];
    for (int c = 0; c < RAIN_COLS; c++) {
        span0[c] = RAIN_ROWS;
        span1[c] = -1;
        for (int r = 0; r < RAIN_ROWS; r++) {
            uint8_t d, lv = rain_cell(c, r, &d);
            uint8_t key = lv ? (uint8_t)(lv << 4 | d) : 0;
            uint8_t *s = &rn_shown[r * RAIN_COLS + c];
            if (*s != key) {
                *s = key;
                paint_cell(c, r, lv, d);
                span0[c] = LV_MIN(span0[c], r);
                span1[c] = r;
            }
        }
        y0 = LV_MIN(y0, span0[c]);
        y1 = LV_MAX(y1, span1[c]);
    }
    if (y1 < 0) {
        return;
    }
    saver_pic_flush(y0 * RAIN_PX, (y1 + 1) * RAIN_PX - 1);
    for (int c = 0; c < RAIN_COLS; c++) {
        if (span1[c] >= 0) {
            saver_pic_redraw(c * RAIN_PX, span0[c] * RAIN_PX, (c + 1) * RAIN_PX - 1, (span1[c] + 1) * RAIN_PX - 1);
        }
    }
}

/**
 * @brief A step of the rain, and the cells that changed repainted.
 * @param[in] t  the timer
 */
static void rain_tick(lv_timer_t *t)
{
    rain_step();
    rain_paint();
}

/* (documented in ui_internal.h) */
bool saver_rain_start(lv_obj_t *parent)
{
    if (!rn_shown) {
        rn_shown = malloc(RAIN_CELLS);
    }
    if (!rn_shown || !make_tiles() || !rain_start(esp_random()) || !(rn_buf = saver_pic_start(parent))) {
        saver_rain_stop();
        return false;
    }
    memset(rn_shown, 0, RAIN_CELLS);                /* all unlit, as the black picture is */
    rain_paint();
    rn_timer = lv_timer_create(rain_tick, RAIN_STEP_MS, NULL);
    return true;
}

/* (documented in ui_internal.h) */
void saver_rain_stop(void)
{
    if (rn_timer) {
        lv_timer_delete(rn_timer);
        rn_timer = NULL;
    }
    if (rn_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        rn_buf = NULL;
    }
    free(rn_tiles);
    rn_tiles = NULL;
    free(rn_shown);
    rn_shown = NULL;
    rain_free();
}

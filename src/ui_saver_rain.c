/**
 * @file ui_saver_rain.c
 * @ingroup ui_sudoku
 * @brief The digital rain screen saver: falling trails of the digits 1 to 9 (rain_core.c) on black, each
 *        column in one of 3Blue1Brown's colours, its head nearly white.
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
static uint16_t *rn_tiles;                /**< the tiles: [colour][level-1][digit-1][RAIN_TILE], RGB565 */
static uint16_t *rn_shown;                /**< each cell as drawn: colour << 8 | level << 4 | digit (0: unlit) */
static lv_timer_t *rn_timer;              /**< a step each RAIN_STEP_MS */

/** @brief The columns' colours (3Blue1Brown's), as rain_colour() numbers them. */
static const uint32_t RN_HUE[RAIN_COLOURS] = {SV_BLUE, SV_TEAL, SV_GREEN, SV_YELLOW, SV_GOLD, SV_PINK};

/**
 * @brief The colour of a level in a column's colour: dimmer down the trail, the head nearly white.
 * @param[in] colour  0..RAIN_COLOURS-1
 * @param[in] level  1..RAIN_HEAD
 * @return the colour
 */
static lv_color_t level_color(int colour, int level)
{
    static const uint8_t BRIGHT[RAIN_HEAD] = {28, 56, 97, 148, 204, 255};   /* the trail, dim to bright */
    lv_color_t c = lv_color_hex(RN_HUE[colour]);
    if (level >= RAIN_HEAD) {                       /* the head: 70 % of the way to white */
        return lv_color_make((uint8_t)(c.red + (255 - c.red) * 7 / 10), (uint8_t)(c.green + (255 - c.green) * 7 / 10),
                             (uint8_t)(c.blue + (255 - c.blue) * 7 / 10));
    }
    uint32_t k = BRIGHT[level - 1];
    return lv_color_make((uint8_t)(c.red * k / 255), (uint8_t)(c.green * k / 255), (uint8_t)(c.blue * k / 255));
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
    rn_tiles = malloc((size_t)RAIN_COLOURS * RAIN_HEAD * 9 * RAIN_TILE * 2);   /* (about 435 KB, PSRAM) */
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
    for (int k = 0; k < RAIN_COLOURS * RAIN_HEAD; k++) {
        lv_color_t c = level_color(k / RAIN_HEAD, k % RAIN_HEAD + 1);
        for (int d = 0; d < 9; d++) {
            uint16_t *t = rn_tiles + (k * 9 + d) * RAIN_TILE;
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
 * @brief Paints a cell: its digit's tile at its level in its column's colour, or black when unlit.
 * @param[in] col  column
 * @param[in] row  row
 * @param[in] level  0..RAIN_HEAD
 * @param[in] digit  1..9
 */
static void paint_cell(int col, int row, uint8_t level, uint8_t digit)
{
    uint16_t *p = rn_buf + (row * RAIN_PX) * SCR_W + col * RAIN_PX;
    int k = rain_colour(col) * RAIN_HEAD + level - 1;
    const uint16_t *t = level ? rn_tiles + (k * 9 + (digit - 1)) * RAIN_TILE : NULL;
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
            uint16_t key = lv ? (uint16_t)(rain_colour(c) << 8 | lv << 4 | d) : 0;
            uint16_t *s = &rn_shown[r * RAIN_COLS + c];
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
        rn_shown = malloc(RAIN_CELLS * sizeof(*rn_shown));
    }
    if (!rn_shown || !make_tiles() || !rain_start(esp_random()) || !(rn_buf = saver_pic_start(parent))) {
        saver_rain_stop();
        return false;
    }
    memset(rn_shown, 0, RAIN_CELLS * sizeof(*rn_shown));   /* all unlit, as the black picture is */
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

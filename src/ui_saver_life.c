/**
 * @file ui_saver_life.c
 * @ingroup ui_sudoku
 * @brief The Game of Life screen saver: LIFE_N x LIFE_N cells (life_core.c), each a round dot in
 *        3Blue1Brown's colours on black (blue alive, yellow just born), the unlit dots faintly there.
 *
 * The picture is the savers' full-screen image (ui_saver_pic.c); each generation repaints only the dots
 * whose glow changed, flushes those rows from the cache and redraws them.
 */
#include "ui_internal.h"
#include "life_core.h"

#define LIFE_PX         (SCR_W / LIFE_N)        /**< a cell's size, px (6 on the 720 px screen) */
#define LIFE_STEP_MS    125                     /**< a generation each, ms (8 a second) */

static uint16_t *lf_buf;                  /**< the picture (saver_pic_start()) */
static uint8_t *lf_shown;                 /**< each cell's glow as drawn (0xFF: not drawn yet) */
static lv_timer_t *lf_timer;              /**< a generation each LIFE_STEP_MS */

/**
 * @brief The colour of each glow, RGB565 (3Blue1Brown's palette): blue alive, yellow just born, fading
 *        through deep blue when dead, and the unlit dot barely there.
 * @param[in] g  a life_glow_t
 * @return the colour
 */
static uint16_t glow_color(uint8_t g)
{
    static const uint32_t HEX[LIFE_GLOW_COUNT] = {
        0x0C1417,                                   /* unlit */
        0x0B2A33, 0x124A5A, SV_BLUE_E, SV_BLUE_D,   /* fading out, through deep blue */
        SV_BLUE,                                    /* alive */
        SV_YELLOW,                                  /* just born */
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
    saver_pic_flush(y0 * LIFE_PX, (y1 + 1) * LIFE_PX - 1);
    saver_pic_redraw(0, y0 * LIFE_PX, SCR_W - 1, (y1 + 1) * LIFE_PX - 1);
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
    if (!lf_shown) {
        lf_shown = malloc(LIFE_CELLS);
    }
    if (!lf_shown || !life_seed(esp_random()) || !(lf_buf = saver_pic_start(parent))) {
        saver_life_stop();
        return false;
    }
    memset(lf_shown, 0xFF, LIFE_CELLS);             /* every dot to paint (the gaps stay black) */
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
    if (lf_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        lf_buf = NULL;
    }
    free(lf_shown);
    lf_shown = NULL;
    life_free();
}

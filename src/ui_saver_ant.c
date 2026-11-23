/**
 * @file ui_saver_ant.c
 * @ingroup ui_sudoku
 * @brief The Langton's ant screen saver: the ant (ant_core.c) walking a grid of round dots in 3Blue1Brown's
 *        colours, the unlit dots faintly there, each state of a cell a dot further along blue, teal,
 *        green and yellow, the ant yellow.
 *
 * Each frame the ant takes AN_STEPS steps; the dots they changed are drawn with the savers' pen
 * (ui_saver_pic.c), which redraws only what changed.
 */
#include "ui_internal.h"
#include "ant_core.h"

#define AN_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define AN_STEPS        24                      /**< steps a frame (600 a second) */
#define AN_PX           (SCR_W / ANT_N)         /**< a cell's size, px (6) */
#define AN_UNLIT        0x0C1417                /**< state 0: the dot barely there */
#define AN_LIT          SV_BLUE                 /**< the lit state (two-state rules) */
#define AN_ANT          SV_YELLOW               /**< the ant */

/** @brief The lit states' colours, first to last (rules with more states): a 3Blue1Brown ramp. */
static const uint32_t AN_RAMP[] = {SV_BLUE_E, SV_BLUE, SV_TEAL, SV_GREEN, SV_YELLOW};

static uint16_t *an_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static uint16_t an_col[ANT_STATES_MAX];   /**< each state's colour, RGB565 */
static uint16_t an_ant;                   /**< AN_ANT, RGB565 */
static int an_was;                        /**< the cell the ant was drawn on (-1: none) */
static lv_timer_t *an_timer;              /**< a frame each AN_FRAME_MS */

/** @brief The states' colours for the rule running: unlit, then along AN_RAMP (blue for two states). */
static void make_colors(void)
{
    const int steps = sizeof(AN_RAMP) / sizeof(AN_RAMP[0]) - 1;
    int n = ant_states();
    an_col[0] = lv_color_to_u16(lv_color_hex(AN_UNLIT));
    for (int k = 1; k < n; k++) {
        if (n == 2) {
            an_col[k] = lv_color_to_u16(lv_color_hex(AN_LIT));
            continue;
        }
        int u = (k - 1) * steps * 255 / (n - 2), seg = u / 255;     /* (where along the ramp) */
        seg = seg >= steps ? steps - 1 : seg;
        an_col[k] = saver_pic_mix(AN_RAMP[seg], AN_RAMP[seg + 1], u - seg * 255);
    }
    an_ant = lv_color_to_u16(lv_color_hex(AN_ANT));
}

/**
 * @brief Paints a cell's dot: 5 x 5 px with the corners off (the 1 px gap to the next stays black).
 * @param[in] i  the cell (y * ANT_N + x)
 * @param[in] c  the colour (RGB565)
 */
static void dot(int i, uint16_t c)
{
    int x0 = (i % ANT_N) * AN_PX, y0 = (i / ANT_N) * AN_PX;
    for (int r = 0; r < AN_PX - 1; r++) {
        bool edge = r == 0 || r == AN_PX - 2;
        for (int k = 0; k < AN_PX - 1; k++) {
            if (!(edge && (k == 0 || k == AN_PX - 2))) {
                saver_pen_put(x0 + k, y0 + r, c);
            }
        }
    }
}

/**
 * @brief A cell's dot in its state's colour.
 * @param[in] i  the cell (y * ANT_N + x)
 */
static void cell(int i)
{
    dot(i, an_col[ant_cell(i % ANT_N, i / ANT_N)]);
}

/** @brief Every dot painted (a new run). */
static void paint_all(void)
{
    make_colors();
    for (int i = 0; i < ANT_CELLS; i++) {
        cell(i);
    }
}

/**
 * @brief A frame: the ant's steps, the dots they changed repainted, the ant drawn where it is now.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void ant_tick(lv_timer_t *t)
{
    saver_pen_begin();
    if (an_was >= 0) {
        cell(an_was);                               /* (where the ant was: its cell again) */
    }
    for (int s = 0; s < AN_STEPS; s++) {
        int ch;
        if (ant_step(&ch) || ch < 0) {
            paint_all();                            /* a new run: the grid cleared */
            break;
        }
        cell(ch);
    }
    int x, y;
    ant_pos(&x, &y);
    an_was = y * ANT_N + x;
    dot(an_was, an_ant);
    saver_pen_end();
}

/* (documented in ui_internal.h) */
bool saver_ant_start(lv_obj_t *parent)
{
    if (!ant_start(esp_random()) || !(an_buf = saver_pic_start(parent))) {
        saver_ant_stop();
        return false;
    }
    an_was = -1;
    saver_pen_begin();
    paint_all();
    saver_pen_end();
    an_timer = lv_timer_create(ant_tick, AN_FRAME_MS, NULL);
    return true;
}

/* (documented in ui_internal.h) */
void saver_ant_stop(void)
{
    if (an_timer) {
        lv_timer_delete(an_timer);
        an_timer = NULL;
    }
    if (an_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        an_buf = NULL;
    }
    ant_free();
}

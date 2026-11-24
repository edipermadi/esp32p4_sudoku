/**
 * @file ui_saver_lissajous.c
 * @ingroup ui_sudoku
 * @brief The Lissajous screen saver: an oscilloscope's figure (saver_lissajous_core.c) in 3Blue1Brown's blue
 *        on black, turning as its phase drifts, a yellow dot tracing it with a tail fading into the blue; every so often it shrinks away and grows back with another ratio.
 *
 * Each frame the last frame's figure and beam are painted black and the new ones drawn with the savers'
 * pen (ui_saver_pic.c), which redraws only what changed.
 */
#include "ui_internal.h"
#include "saver_lissajous_core.h"

#define LJ_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define LJ_TAIL         40                      /**< the beam's places drawn, a segment apart (its tail) */
#define LJ_CURVE        SV_BLUE                 /**< the figure: 3Blue1Brown's blue */
#define LJ_BEAM         SV_YELLOW               /**< the dot tracing it: yellow */

static uint16_t *lj_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static int16_t (*lj_was)[2];              /**< the figure's points as last drawn, px (x, y) */
static int lj_was_n;                      /**< its segments (0: none drawn) */
static int16_t lj_beam_was[LJ_TAIL][2];   /**< the beam's places as last drawn */
static uint16_t lj_curve;                 /**< LJ_CURVE, RGB565 */
static uint16_t lj_tail[LJ_TAIL];         /**< the beam's tail by age; [0] the spot, RGB565 */
static lv_timer_t *lj_timer;              /**< a frame each LJ_FRAME_MS */

/**
 * @brief Draws the figure and beam as remembered, in black (erasing them) or in their colours.
 * @param[in] erase  black
 */
static void draw(bool erase)
{
    for (int i = 0; i < lj_was_n; i++) {
        saver_pen_line(lj_was[i][0], lj_was[i][1], lj_was[i + 1][0], lj_was[i + 1][1], 2,
                       erase ? 0 : lj_curve);
    }
    for (int k = LJ_TAIL - 2; k >= 0; k--) {        /* the tail, oldest first, then the spot */
        saver_pen_line(lj_beam_was[k + 1][0], lj_beam_was[k + 1][1], lj_beam_was[k][0], lj_beam_was[k][1], 2,
                       erase ? 0 : lj_tail[k + 1]);
    }
    saver_pen_square(lj_beam_was[0][0] - 2, lj_beam_was[0][1] - 2, 5, erase ? 0 : lj_tail[0]);
}

/**
 * @brief A frame: the figure moves on, the old one is erased and the new drawn, what changed redrawn.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void liss_tick(lv_timer_t *t)
{
    liss_step();
    saver_perf_mark();
    saver_pen_begin();
    if (lj_was_n) {
        draw(true);                                 /* (as last drawn: lj_was, lj_beam_was) */
    }
    lj_was_n = liss_segs();
    for (int i = 0; i <= lj_was_n; i++) {
        int x, y;
        liss_point(i, &x, &y);
        lj_was[i][0] = (int16_t)x;
        lj_was[i][1] = (int16_t)y;
    }
    for (int k = 0; k < LJ_TAIL; k++) {
        int x, y;
        liss_beam(k, &x, &y);
        lj_beam_was[k][0] = (int16_t)x;
        lj_beam_was[k][1] = (int16_t)y;
    }
    draw(false);
    saver_pen_end();
}

/* (documented in ui_internal.h) */
bool saver_lissajous_start(lv_obj_t *parent)
{
    if (!lj_was) {
        lj_was = malloc((LISS_SEGS_MAX + 1) * sizeof(*lj_was));
    }
    if (!lj_was || !(lj_buf = saver_pic_start(parent))) {
        saver_lissajous_stop();
        return false;
    }
    lj_curve = lv_color_to_u16(lv_color_hex(LJ_CURVE));
    for (int k = 0; k < LJ_TAIL; k++) {
        lj_tail[k] = saver_pic_mix(LJ_BEAM, LJ_CURVE, k * 255 / (LJ_TAIL - 1));
    }
    liss_start(esp_random());
    lj_was_n = 0;
    liss_tick(NULL);
    lj_timer = saver_timer_create(liss_tick, LJ_FRAME_MS);
    return true;
}

/* (documented in ui_internal.h) */
void saver_lissajous_stop(void)
{
    if (lj_timer) {
        lv_timer_delete(lj_timer);
        lj_timer = NULL;
    }
    if (lj_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        lj_buf = NULL;
    }
    free(lj_was);
    lj_was = NULL;
    lj_was_n = 0;
}

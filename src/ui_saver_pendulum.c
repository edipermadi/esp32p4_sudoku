/**
 * @file ui_saver_pendulum.c
 * @ingroup ui_sudoku
 * @brief The double pendulum screen saver: a chaotic double pendulum (pendulum_core.c) in a vacuum
 *        fluorescent display's blue-green on black, its lower bob white-hot, drawing a long trail that
 *        fades out behind it.
 *
 * Each frame the last frame's trail, rods and bobs are painted black and the new ones drawn with the
 * savers' pen (ui_saver_pic.c), which redraws only what changed.
 */
#include "ui_internal.h"
#include "pendulum_core.h"

#define PD_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define PD_TRAIL_NEW    0x3CF0C8                /**< the trail, newest */
#define PD_TRAIL_OLD    0x041611                /**< oldest */
#define PD_ROD          0x1F9278                /**< the rods */
#define PD_BOB          0x3CF0C8                /**< the upper bob */
#define PD_TIP          0xC4FFF2                /**< the lower bob: white-hot */
#define PD_SHADES       32                      /**< the trail's colours, newest to oldest */
#define PD_WIDTH        4                       /**< the rods' and the trail's width, px */

static uint16_t *pd_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static int16_t (*pd_trail)[2];            /**< the trail as last drawn, newest first, px */
static int pd_trail_n;                    /**< its places (0: nothing drawn) */
static int16_t pd_bob[3][2];              /**< the pivot, upper bob and lower bob as last drawn, px */
static uint16_t pd_shade[PD_SHADES];      /**< the trail's colours, newest first, RGB565 */
static uint16_t pd_rod, pd_bob_c, pd_tip; /**< PD_ROD, PD_BOB, PD_TIP, RGB565 */
static lv_timer_t *pd_timer;              /**< a frame each PD_FRAME_MS */

/**
 * @brief Draws the trail, rods and bobs as remembered, in black (erasing them) or in their colours.
 * @param[in] erase  black
 */
static void draw(bool erase)
{
    for (int k = pd_trail_n - 2; k >= 0; k--) {     /* oldest first: the newest on top */
        uint16_t c = erase ? 0 : pd_shade[k * PD_SHADES / PEND_TRAIL];
        saver_pen_line(pd_trail[k + 1][0], pd_trail[k + 1][1], pd_trail[k][0], pd_trail[k][1], PD_WIDTH, c);
    }
    if (!pd_trail_n) {
        return;
    }
    saver_pen_line(pd_bob[0][0], pd_bob[0][1], pd_bob[1][0], pd_bob[1][1], PD_WIDTH, erase ? 0 : pd_rod);
    saver_pen_line(pd_bob[1][0], pd_bob[1][1], pd_bob[2][0], pd_bob[2][1], PD_WIDTH, erase ? 0 : pd_rod);
    saver_pen_square(pd_bob[0][0] - 2, pd_bob[0][1] - 2, 5, erase ? 0 : pd_rod);
    saver_pen_square(pd_bob[1][0] - 4, pd_bob[1][1] - 4, 9, erase ? 0 : pd_bob_c);
    saver_pen_square(pd_bob[2][0] - 5, pd_bob[2][1] - 5, 11, erase ? 0 : pd_tip);
}

/**
 * @brief A frame: the pendulum swings on, the old picture is erased and the new drawn, what changed
 *        redrawn.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void pend_tick(lv_timer_t *t)
{
    saver_pen_begin();
    draw(true);
    pend_step();
    pd_trail_n = pend_trail_len();
    for (int k = 0; k < pd_trail_n; k++) {
        float x, y;
        pend_trail(k, &x, &y);
        pd_trail[k][0] = (int16_t)x;
        pd_trail[k][1] = (int16_t)y;
    }
    float x1, y1, x2, y2;
    pend_bobs(&x1, &y1, &x2, &y2);
    pd_bob[0][0] = pd_bob[0][1] = PEND_W / 2;
    pd_bob[1][0] = (int16_t)x1;
    pd_bob[1][1] = (int16_t)y1;
    pd_bob[2][0] = (int16_t)x2;
    pd_bob[2][1] = (int16_t)y2;
    draw(false);
    saver_pen_end();
}

/* (documented in ui_internal.h) */
bool saver_pendulum_start(lv_obj_t *parent)
{
    if (!pd_trail) {
        pd_trail = malloc(PEND_TRAIL * sizeof(*pd_trail));
    }
    if (!pd_trail || !pend_start(esp_random()) || !(pd_buf = saver_pic_start(parent))) {
        saver_pendulum_stop();
        return false;
    }
    for (int k = 0; k < PD_SHADES; k++) {
        int u = k * 255 / (PD_SHADES - 1);
        pd_shade[k] = saver_pic_mix(PD_TRAIL_NEW, PD_TRAIL_OLD, u * u / 255);   /* (bright longer, then fading) */
    }
    pd_rod = lv_color_to_u16(lv_color_hex(PD_ROD));
    pd_bob_c = lv_color_to_u16(lv_color_hex(PD_BOB));
    pd_tip = lv_color_to_u16(lv_color_hex(PD_TIP));
    pd_trail_n = 0;
    pend_tick(NULL);
    pd_timer = lv_timer_create(pend_tick, PD_FRAME_MS, NULL);
    return true;
}

/* (documented in ui_internal.h) */
void saver_pendulum_stop(void)
{
    if (pd_timer) {
        lv_timer_delete(pd_timer);
        pd_timer = NULL;
    }
    if (pd_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        pd_buf = NULL;
    }
    free(pd_trail);
    pd_trail = NULL;
    pd_trail_n = 0;
    pend_free();
}

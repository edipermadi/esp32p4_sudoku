/**
 * @file ui_saver_boids.c
 * @ingroup ui_sudoku
 * @brief The boids screen saver: a flock (boids_core.c) in a vacuum fluorescent display's blue-green on
 *        black, each bird a white-hot dot drawing a trail that fades out behind it.
 *
 * Each frame the last frame's trails are painted black and the new ones drawn with the savers' pen
 * (ui_saver_pic.c), which redraws only what changed: a flock bunched in one place redraws little.
 */
#include "ui_internal.h"
#include "boids_core.h"

#define BD_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define BD_HEAD         0xC4FFF2                /**< a bird: white-hot */
#define BD_TRAIL_NEW    0x3CF0C8                /**< its trail, newest */
#define BD_TRAIL_OLD    0x06221C                /**< oldest */

static uint16_t *bd_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static int16_t (*bd_was)[BOIDS_TRAIL][2]; /**< each bird's trail as last drawn, px (x, y) */
static bool bd_drawn;                     /**< bd_was holds a frame */
static uint16_t bd_col[BOIDS_TRAIL];      /**< a trail segment's colour by age; [0] the head, RGB565 */
static lv_timer_t *bd_timer;              /**< a frame each BD_FRAME_MS */

/** @brief The colours: the head white-hot, the trail from the VFD blue-green fading nearly to black. */
static void make_colors(void)
{
    bd_col[0] = lv_color_to_u16(lv_color_hex(BD_HEAD));
    for (int k = 1; k < BOIDS_TRAIL; k++) {
        bd_col[k] = saver_pic_mix(BD_TRAIL_NEW, BD_TRAIL_OLD, (k - 1) * 255 / (BOIDS_TRAIL - 2));
    }
}

/**
 * @brief Draws every bird's trail as remembered in bd_was, oldest segments first, then the heads;
 *        in black (erasing them) or in their colours.
 * @param[in] erase  black
 */
static void draw_flock(bool erase)
{
    for (int k = BOIDS_TRAIL - 2; k >= 0; k--) {
        uint16_t c = erase ? 0 : bd_col[k + 1];
        for (int i = 0; i < BOIDS_N; i++) {
            saver_pen_line(bd_was[i][k + 1][0], bd_was[i][k + 1][1], bd_was[i][k][0], bd_was[i][k][1], 2, c);
        }
    }
    for (int i = 0; i < BOIDS_N; i++) {
        saver_pen_square(bd_was[i][0][0] - 1, bd_was[i][0][1] - 1, 3, erase ? 0 : bd_col[0]);
    }
}

/**
 * @brief A frame: the flock moves on, the old trails are erased and the new drawn, and what changed
 *        is redrawn.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void boids_tick(lv_timer_t *t)
{
    saver_pen_begin();
    if (bd_drawn) {
        draw_flock(true);
    }
    boids_step();
    for (int i = 0; i < BOIDS_N; i++) {
        for (int k = 0; k < BOIDS_TRAIL; k++) {
            float x, y;
            boids_at(i, k, &x, &y);
            bd_was[i][k][0] = (int16_t)x;
            bd_was[i][k][1] = (int16_t)y;
        }
    }
    draw_flock(false);
    bd_drawn = true;
    saver_pen_end();
}

/* (documented in ui_internal.h) */
bool saver_boids_start(lv_obj_t *parent)
{
    if (!bd_was) {
        bd_was = malloc(BOIDS_N * sizeof(*bd_was));
    }
    if (!bd_was || !boids_start(esp_random()) || !(bd_buf = saver_pic_start(parent))) {
        saver_boids_stop();
        return false;
    }
    make_colors();
    bd_drawn = false;
    boids_tick(NULL);
    bd_timer = lv_timer_create(boids_tick, BD_FRAME_MS, NULL);
    return true;
}

/* (documented in ui_internal.h) */
void saver_boids_stop(void)
{
    if (bd_timer) {
        lv_timer_delete(bd_timer);
        bd_timer = NULL;
    }
    if (bd_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        bd_buf = NULL;
    }
    free(bd_was);
    bd_was = NULL;
    boids_free();
}

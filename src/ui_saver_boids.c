/**
 * @file ui_saver_boids.c
 * @ingroup ui_sudoku
 * @brief The boids screen saver: a flock (boids_core.c) on black, each bird a bright dot in one of three
 *        of 3Blue1Brown's colours (blue, green, yellow) drawing a trail that fades out behind it; flocks come to one colour and fight it out when they meet, a colour
 *        change blending over BD_BLEND frames; red hawks chase the flock and scatter it.
 *
 * Each frame the last frame's trails are painted black and the new ones drawn with the savers' pen
 * (ui_saver_pic.c), which redraws only what changed: a flock bunched in one place redraws little.
 */
#include "ui_internal.h"
#include "boids_core.h"

#define BD_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define BD_BLEND        16                      /**< frames a bird's colour change blends over */
#define BD_HAWK         SV_RED                  /**< a hawk: red */

/** @brief The birds' colours (3Blue1Brown's). */
static const uint32_t BD_COLOUR[BOIDS_COLOURS] = {SV_BLUE, SV_GREEN, SV_YELLOW};

static uint16_t *bd_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static int16_t (*bd_was)[BOIDS_TRAIL][2]; /**< each bird's and hawk's trail as last drawn, px (x, y) */
static uint8_t (*bd_blend)[2];            /**< each bird's colours as last drawn: from, to * BD_BLEND + step */
static bool bd_drawn;                     /**< bd_was holds a frame */
/** @brief The birds' colours: [from][to][blend step][age] (age 0: the head), RGB565. */
static uint16_t (*bd_col)[BOIDS_COLOURS][BD_BLEND][BOIDS_TRAIL];
static uint16_t bd_hawk[BOIDS_TRAIL];     /**< a hawk's colours by age (0: the head), RGB565 */
static lv_timer_t *bd_timer;              /**< a frame each BD_FRAME_MS */

/**
 * @brief Between two colours, 0xRRGGBB.
 * @param[in] a  from
 * @param[in] b  to
 * @param[in] u  how far, 0..1
 * @return the colour, 0xRRGGBB
 */
static uint32_t mix(uint32_t a, uint32_t b, float u)
{
    uint32_t r = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float p = (float)((a >> sh) & 0xFF), q = (float)((b >> sh) & 0xFF);
        r |= (uint32_t)(p + (q - p) * u + 0.5f) << sh;
    }
    return r;
}

/**
 * @brief A trail's colour by age: the head bright, nearly white; the trail its colour, fading nearly to
 *        black.
 * @param[in] c  the colour, 0xRRGGBB
 * @param[in] age  0 (the head) .. BOIDS_TRAIL-1
 * @param[in] white  how white the head is, 0..1
 * @return the colour, RGB565
 */
static uint16_t aged(uint32_t c, int age, float white)
{
    if (!age) {
        return lv_color_to_u16(lv_color_hex(mix(c, 0xFFFFFF, white)));
    }
    float f = (float)(age - 1) / (BOIDS_TRAIL - 2);
    return lv_color_to_u16(lv_color_hex(mix(c, 0x000000, 0.94f * f)));
}

/** @brief The colours: every blend between two birds' colours at every age, and the hawks'. */
static void make_colors(void)
{
    for (int a = 0; a < BOIDS_COLOURS; a++) {
        for (int b = 0; b < BOIDS_COLOURS; b++) {
            for (int s = 0; s < BD_BLEND; s++) {
                uint32_t c = mix(BD_COLOUR[a], BD_COLOUR[b], (float)s / (BD_BLEND - 1));
                for (int k = 0; k < BOIDS_TRAIL; k++) {
                    bd_col[a][b][s][k] = aged(c, k, 0.6f);
                }
            }
        }
    }
    for (int k = 0; k < BOIDS_TRAIL; k++) {
        bd_hawk[k] = aged(BD_HAWK, k, 0.35f);
    }
}

/**
 * @brief A bird's or hawk's colour at an age, as last drawn.
 * @param[in] i  the bird or hawk
 * @param[in] age  0 (the head) .. BOIDS_TRAIL-1
 * @return the colour, RGB565
 */
static uint16_t colour(int i, int age)
{
    if (i >= BOIDS_N) {
        return bd_hawk[age];
    }
    int to = bd_blend[i][1] / BD_BLEND, step = bd_blend[i][1] % BD_BLEND;
    return bd_col[bd_blend[i][0]][to][step][age];
}

/**
 * @brief Draws every bird's and hawk's trail as remembered in bd_was, oldest segments first, then the
 *        heads (the hawks' bigger); in black (erasing them) or in their colours.
 * @param[in] erase  black
 */
static void draw_flock(bool erase)
{
    for (int k = BOIDS_TRAIL - 2; k >= 0; k--) {
        for (int i = 0; i < BOIDS_ALL; i++) {
            saver_pen_line(bd_was[i][k + 1][0], bd_was[i][k + 1][1], bd_was[i][k][0], bd_was[i][k][1],
                           i < BOIDS_N ? 2 : 3, erase ? 0 : colour(i, k + 1));
        }
    }
    for (int i = 0; i < BOIDS_ALL; i++) {
        int size = i < BOIDS_N ? 3 : 7;
        saver_pen_square(bd_was[i][0][0] - size / 2, bd_was[i][0][1] - size / 2, size, erase ? 0 : colour(i, 0));
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
    for (int i = 0; i < BOIDS_ALL; i++) {
        for (int k = 0; k < BOIDS_TRAIL; k++) {
            float x, y;
            boids_at(i, k, &x, &y);
            bd_was[i][k][0] = (int16_t)x;
            bd_was[i][k][1] = (int16_t)y;
        }
        if (i < BOIDS_N) {
            int before, since, now = boids_colour(i, &before, &since);
            int step = since >= BD_BLEND - 1 ? BD_BLEND - 1 : since;
            bd_blend[i][0] = (uint8_t)before;
            bd_blend[i][1] = (uint8_t)(now * BD_BLEND + step);
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
        bd_was = malloc(BOIDS_ALL * sizeof(*bd_was));
    }
    if (!bd_blend) {
        bd_blend = malloc(BOIDS_N * sizeof(*bd_blend));
    }
    if (!bd_col) {
        bd_col = malloc(BOIDS_COLOURS * sizeof(*bd_col));
    }
    if (!bd_was || !bd_blend || !bd_col || !boids_start(esp_random()) || !(bd_buf = saver_pic_start(parent))) {
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
    free(bd_blend);
    free(bd_col);
    bd_was = NULL;
    bd_blend = NULL;
    bd_col = NULL;
    boids_free();
}

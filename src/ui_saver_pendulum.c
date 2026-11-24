/**
 * @file ui_saver_pendulum.c
 * @ingroup ui_sudoku
 * @brief The double pendulum screen saver: PEND_N chaotic double pendulums (saver_pendulum_core.c) started a
 *        hair apart, each in one of 3Blue1Brown's colours (blue, yellow, red); grey
 *        rods. They swing as one, then part. Each lower bob draws a trail coloured by how fast it
 *        went there (its colour's deep shade when slow, glowing to near white when fast) that fades out
 *        behind it.
 *
 * Each frame the last frame's trails, rods and bobs are painted black and the new ones drawn with the
 * savers' pen (ui_saver_pic.c), which redraws only what changed.
 */
#include "ui_internal.h"
#include "saver_pendulum_core.h"

#define PD_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define PD_WIDTH        4                       /**< the rods' and the trails' width, px */
#define PD_SPEEDS       16                      /**< a trail's colours by speed */
#define PD_AGES         32                      /**< and by age */

/** @brief A pendulum's colours: its phosphor's deep shade (slow), full colour, and near white (fast). */
static const uint32_t PD_HUE[PEND_N][3] = {
    {SV_BLUE_E, SV_BLUE, SV_BLUE_A},            /* 3Blue1Brown's blue */
    {SV_YELLOW_E, SV_YELLOW, SV_YELLOW_A},      /* yellow */
    {SV_RED_E, SV_RED, SV_RED_A},               /* red */
};

static uint16_t *pd_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static int16_t (*pd_trail)[PEND_TRAIL][3];  /**< each trail as last drawn, newest first: x, y px, colour level */
static int pd_trail_n;                    /**< its places (0: nothing drawn) */
static int16_t pd_bob[PEND_N][2][2];      /**< each pendulum's upper and lower bob as last drawn, px */
static uint16_t (*pd_shade)[PD_SPEEDS][PD_AGES];   /**< a trail's colours: [pendulum][speed][age], RGB565 */
static uint16_t pd_rod[PEND_N], pd_mid[PEND_N], pd_tip[PEND_N];   /**< rods, upper bobs, lower bobs, RGB565 */
static lv_timer_t *pd_timer;              /**< a frame each PD_FRAME_MS */

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

/** @brief The colours: each trail by speed (deep, full, near white) and age (fading to black). */
static void make_colors(void)
{
    for (int k = 0; k < PEND_N; k++) {
        for (int v = 0; v < PD_SPEEDS; v++) {
            float u = (float)v / (PD_SPEEDS - 1);
            uint32_t c = u < 0.5f ? mix(PD_HUE[k][0], PD_HUE[k][1], u * 2) : mix(PD_HUE[k][1], PD_HUE[k][2], u * 2 - 1);
            for (int a = 0; a < PD_AGES; a++) {
                float f = (float)a / (PD_AGES - 1);
                pd_shade[k][v][a] = lv_color_to_u16(lv_color_hex(mix(c, 0x000000, f * f * 0.97f)));
            }
        }
        pd_rod[k] = lv_color_to_u16(lv_color_hex(SV_GREY));
        pd_mid[k] = lv_color_to_u16(lv_color_hex(PD_HUE[k][1]));
        pd_tip[k] = lv_color_to_u16(lv_color_hex(PD_HUE[k][2]));
    }
}

/**
 * @brief Draws the trails, rods and bobs as remembered, in black (erasing them) or in their colours;
 *        the last pendulum first, so the first is on top.
 * @param[in] erase  black
 */
static void draw(bool erase)
{
    if (!pd_trail_n) {
        return;
    }
    for (int p = PEND_N - 1; p >= 0; p--) {
        const int16_t (*t)[3] = pd_trail[p];
        for (int k = pd_trail_n - 2; k >= 0; k--) { /* oldest first: the newest on top */
            uint16_t c = erase ? 0 : pd_shade[p][t[k][2]][k * PD_AGES / PEND_TRAIL];
            saver_pen_line(t[k + 1][0], t[k + 1][1], t[k][0], t[k][1], PD_WIDTH, c);
        }
    }
    for (int p = PEND_N - 1; p >= 0; p--) {
        saver_pen_line(PEND_W / 2, PEND_W / 2, pd_bob[p][0][0], pd_bob[p][0][1], PD_WIDTH, erase ? 0 : pd_rod[p]);
        saver_pen_line(pd_bob[p][0][0], pd_bob[p][0][1], pd_bob[p][1][0], pd_bob[p][1][1], PD_WIDTH,
                       erase ? 0 : pd_rod[p]);
        saver_pen_square(pd_bob[p][0][0] - 4, pd_bob[p][0][1] - 4, 9, erase ? 0 : pd_mid[p]);
        saver_pen_square(pd_bob[p][1][0] - 5, pd_bob[p][1][1] - 5, 11, erase ? 0 : pd_tip[p]);
    }
    saver_pen_square(PEND_W / 2 - 3, PEND_W / 2 - 3, 7, erase ? 0 : pd_rod[0]);   /* the pivot */
}

/**
 * @brief A frame: the pendulums swing on, the old picture is erased and the new drawn, what changed
 *        redrawn.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void pend_tick(lv_timer_t *t)
{
    pend_step();
    saver_perf_mark();
    saver_pen_begin();
    draw(true);                                     /* (as last drawn: pd_trail, pd_bob not updated yet) */
    pd_trail_n = pend_trail_len();
    for (int p = 0; p < PEND_N; p++) {
        for (int k = 0; k < pd_trail_n; k++) {
            float x, y, f;
            pend_trail(p, k, &x, &y, &f);
            pd_trail[p][k][0] = (int16_t)x;
            pd_trail[p][k][1] = (int16_t)y;
            pd_trail[p][k][2] = (int16_t)(f * (PD_SPEEDS - 1) + 0.5f);
        }
        float x1, y1, x2, y2;
        pend_bobs(p, &x1, &y1, &x2, &y2);
        pd_bob[p][0][0] = (int16_t)x1;
        pd_bob[p][0][1] = (int16_t)y1;
        pd_bob[p][1][0] = (int16_t)x2;
        pd_bob[p][1][1] = (int16_t)y2;
    }
    draw(false);
    saver_pen_end();
}

/* (documented in ui_internal.h) */
bool saver_pendulum_start(lv_obj_t *parent)
{
    if (!pd_trail) {
        pd_trail = malloc(PEND_N * sizeof(*pd_trail));
    }
    if (!pd_shade) {
        pd_shade = malloc(PEND_N * sizeof(*pd_shade));
    }
    if (!pd_trail || !pd_shade || !pend_start(esp_random()) || !(pd_buf = saver_pic_start(parent))) {
        saver_pendulum_stop();
        return false;
    }
    make_colors();
    pd_trail_n = 0;
    pend_tick(NULL);
    pd_timer = saver_timer_create(pend_tick, PD_FRAME_MS);
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
    free(pd_shade);
    pd_trail = NULL;
    pd_shade = NULL;
    pd_trail_n = 0;
    pend_free();
}

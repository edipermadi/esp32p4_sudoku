/**
 * @file saver_lissajous_core.c
 * @ingroup saver_lissajous_core
 * @brief Lissajous figures for the screen saver, without LVGL (drawn by ui_saver_lissajous.c).
 */
#include "saver_lissajous_core.h"
#include <math.h>

#define TURN            6.28318530718f        /**< 2 pi */

/** @brief The ratios a:b shown (coprime, so each closes as one curve). */
static const uint8_t RATIOS[][2] = {
    {1, 2}, {2, 3}, {3, 4}, {3, 5}, {4, 5}, {5, 6}, {1, 3}, {2, 5}, {3, 7}, {4, 7}, {5, 7}, {5, 8}, {6, 7}, {7, 8},
};
#define RATIOS_N        ((int)(sizeof(RATIOS) / sizeof(RATIOS[0])))   /**< how many */

static int s_ratio;                       /**< the ratio shown: RATIOS[s_ratio] */
static float s_phase;                     /**< d, radians */
static float s_beam;                      /**< the beam's place along the figure, of a turn */
static int s_t;                           /**< steps into this ratio */
static uint32_t s_rng = 1;                /**< xorshift32 state (never 0) */

/**
 * @brief The next random number (xorshift32).
 * @return it
 */
static uint32_t rnd(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

/* (documented in saver_lissajous_core.h) */
void liss_start(uint32_t seed)
{
    s_rng = seed ? seed : 0x9E3779B9u;
    s_ratio = (int)(rnd() % RATIOS_N);
    s_phase = (float)(rnd() % 1000) * TURN / 1000;
    s_beam = 0;
    s_t = LISS_FADE;                                /* (it starts full size) */
}

/* (documented in saver_lissajous_core.h) */
void liss_step(void)
{
    s_phase += LISS_DRIFT;
    if (s_phase > TURN) {
        s_phase -= TURN;
    }
    s_beam += LISS_BEAM;
    if (s_beam > 1) {
        s_beam -= 1;
    }
    if (++s_t >= LISS_HOLD) {                       /* shrunk away: another ratio, growing back */
        int r = (int)(rnd() % (RATIOS_N - 1));
        s_ratio = r >= s_ratio ? r + 1 : r;         /* (never the same again) */
        s_t = 0;
    }
}

/* (documented in saver_lissajous_core.h) */
float liss_size(void)
{
    float u = 1;
    if (s_t < LISS_FADE) {
        u = (float)s_t / LISS_FADE;                 /* growing */
    } else if (s_t >= LISS_HOLD - LISS_FADE) {
        u = (float)(LISS_HOLD - 1 - s_t) / LISS_FADE;   /* shrinking */
    }
    return u * u * (3 - 2 * u);                     /* (eased) */
}

/* (documented in saver_lissajous_core.h) */
int liss_segs(void)
{
    int n = LISS_SEGS_PER * (RATIOS[s_ratio][0] + RATIOS[s_ratio][1]);
    return n > LISS_SEGS_MAX ? LISS_SEGS_MAX : n;
}

/**
 * @brief The figure at a place along it.
 * @param[in] t  the place, of a turn (0..1)
 * @param[out] x  gets x, px
 * @param[out] y  gets y, px
 */
static void at(float t, int *x, int *y)
{
    float r = LISS_R * liss_size();
    *x = LISS_W / 2 + (int)lroundf(r * sinf(RATIOS[s_ratio][0] * t * TURN + s_phase));
    *y = LISS_W / 2 + (int)lroundf(r * sinf(RATIOS[s_ratio][1] * t * TURN));
}

/* (documented in saver_lissajous_core.h) */
void liss_point(int i, int *x, int *y)
{
    at((float)i / liss_segs(), x, y);
}

/* (documented in saver_lissajous_core.h) */
void liss_beam(int age, int *x, int *y)
{
    float t = s_beam - (float)age / liss_segs();    /* (back along the figure, a segment an age) */
    at(t < 0 ? t + 1 : t, x, y);
}

/* (documented in saver_lissajous_core.h) */
void liss_ratio(int *a, int *b)
{
    *a = RATIOS[s_ratio][0];
    *b = RATIOS[s_ratio][1];
}

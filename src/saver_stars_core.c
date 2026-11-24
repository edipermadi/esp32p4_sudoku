/**
 * @file saver_stars_core.c
 * @ingroup saver_stars_core
 * @brief A starfield flown through, for the screen saver, without LVGL (drawn by ui_saver_stars.c).
 */
#include "saver_stars_core.h"
#include <math.h>
#include <stdlib.h>

#define HALF            (STARS_W * 0.5f)      /**< the screen's middle, px */
#define SPREAD          2.4f                  /**< stars come back within this of the line of flight */
#define TWO_PI          6.2831853f            /**< a turn, radians */

/** @brief A star. */
typedef struct {
    float x, y, z;                        /**< where it is: across, up, ahead */
    uint8_t tone;                         /**< its colour */
    bool fresh;                           /**< just come back: no streak yet */
} star_t;

static star_t *s_star;                    /**< the stars (NULL: none) */
static uint32_t s_step;                   /**< steps since the start */
static float s_roll;                      /**< the view's roll, radians */
static float s_speed;                     /**< the way flown this step */
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

/**
 * @brief A random number from 0 up to 1.
 * @return it
 */
static float frnd(void)
{
    return (rnd() >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief Where a star shows at a depth, the view rolled.
 * @param[in] s  the star
 * @param[in] z  the depth
 * @param[out] sx  gets x, px
 * @param[out] sy  gets y, px
 */
static void project(const star_t *s, float z, float *sx, float *sy)
{
    float c = cosf(s_roll), n = sinf(s_roll);
    float x = (s->x * c - s->y * n) / z, y = (s->x * n + s->y * c) / z;
    *sx = HALF + x * HALF;
    *sy = HALF - y * HALF;
}

/**
 * @brief A star somewhere new: anywhere within SPREAD of the line of flight (evenly over the disc), at
 *        the given depth.
 * @param[out] s  the star
 * @param[in] z  its depth
 */
static void place(star_t *s, float z)
{
    float r = SPREAD * sqrtf(0.0004f + 0.9996f * frnd()), a = TWO_PI * frnd();   /* (not dead ahead) */
    static const uint8_t TONE_ODDS[16] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 5, 5};
    s->x = r * cosf(a);
    s->y = r * sinf(a);
    s->z = z;
    s->tone = TONE_ODDS[rnd() % 16];
    s->fresh = true;
}

/* (documented in saver_stars_core.h) */
bool stars_start(uint32_t seed)
{
    s_rng = seed ? seed : 0x9E3779B9u;
    if (!s_star) {
        s_star = malloc(STARS_N * sizeof(*s_star));
    }
    if (!s_star) {
        return false;
    }
    s_step = 0;
    s_roll = 0;
    s_speed = STARS_SPEED;
    for (int i = 0; i < STARS_N; i++) {
        place(&s_star[i], STARS_NEAR + (STARS_FAR - STARS_NEAR) * frnd());   /* (through the whole depth) */
    }
    return true;
}

/* (documented in saver_stars_core.h) */
void stars_free(void)
{
    free(s_star);
    s_star = NULL;
}

/* (documented in saver_stars_core.h) */
void stars_step(void)
{
    if (!s_star) {
        return;
    }
    s_step++;
    float w = TWO_PI * (float)(s_step % STARS_WAVE) / STARS_WAVE;
    s_speed = STARS_SPEED * (1.0f - 0.75f * cosf(w) + 0.2f * sinf(2 * w));   /* (about 0.15 .. 1.85 times) */
    s_roll += STARS_ROLL;
    if (s_roll > TWO_PI) {
        s_roll -= TWO_PI;
    }
    for (int i = 0; i < STARS_N; i++) {
        star_t *s = &s_star[i];
        float sx, sy;
        s->fresh = false;
        s->z -= s_speed;
        if (s->z >= STARS_NEAR) {
            project(s, s->z, &sx, &sy);
            if (sx >= -8 && sx < STARS_W + 8 && sy >= -8 && sy < STARS_W + 8) {
                continue;                           /* still to be seen */
            }
        }
        place(s, STARS_FAR);                        /* passed, or gone off the screen: back far away */
    }
}

/* (documented in saver_stars_core.h) */
bool stars_get(int i, int *x0, int *y0, int *x1, int *y1, int *bright, int *tone)
{
    if (!s_star || i < 0 || i >= STARS_N || s_star[i].fresh) {
        return false;
    }
    const star_t *s = &s_star[i];
    float sx, sy, tx, ty;
    project(s, s->z, &sx, &sy);
    project(s, s->z + s_speed * STARS_TRAIL, &tx, &ty);
    *x0 = (int)lroundf(tx);
    *y0 = (int)lroundf(ty);
    *x1 = (int)lroundf(sx);
    *y1 = (int)lroundf(sy);
    float u = 1.0f - (s->z - STARS_NEAR) / (STARS_FAR - STARS_NEAR);   /* 0 far .. 1 near */
    *bright = (int)(255 * u);
    *tone = s->tone;
    return true;
}

/* (documented in saver_stars_core.h) */
float stars_speed(void) { return s_speed; }

/* (documented in saver_stars_core.h) */
float stars_depth(int i) { return s_star && i >= 0 && i < STARS_N ? s_star[i].z : 0; }

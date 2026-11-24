/**
 * @file saver_attractor_core.c
 * @ingroup saver_attractor_core
 * @brief Strange attractors for the screen saver, without LVGL (drawn by ui_saver_attractor.c).
 */
#include "saver_attractor_core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SIN_N           4096                  /**< the sine table's steps (a whole turn) */
#define TEST_N          20000                 /**< points tried when judging a parameter set */
#define TEST_GRID       64                    /**< the coarse grid it is judged on */
#define TEST_SPREAD     500                   /**< its cells the second half must reach to be kept */

static uint8_t *s_count;                  /**< the counts, ATTR_W x ATTR_W (NULL: none) */
static float *s_sin;                      /**< sin over a turn (SIN_N + 1 entries), for speed; with the grid */
static int s_kind;                        /**< attr_kind_t */
static float s_p[4];                      /**< a, b, c, d */
static float s_x, s_y;                    /**< the point */
static float s_cx, s_cy, s_scale;         /**< the shape's centre and its cells per unit */
static int s_box[4];                      /**< x0, y0, x1, y1 on the grid */
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
 * @brief A random number in [lo, hi).
 * @param[in] lo  at least
 * @param[in] hi  less than
 * @return it
 */
static float rnd_f(float lo, float hi)
{
    return lo + (hi - lo) * (float)(rnd() >> 8) / 16777216.0f;
}

/**
 * @brief sin from the table, between its steps (smooth: rounding would make the point fall into a loop).
 * @param[in] a  the angle, radians
 * @return its sine
 */
static inline float fsin(float a)
{
    float f = a * (SIN_N / 6.28318530718f);
    float fl = floorf(f);
    int i = (int)fl & (SIN_N - 1);                  /* (two's complement: negative angles wrap too) */
    return s_sin[i] + (s_sin[i + 1] - s_sin[i]) * (f - fl);
}

/**
 * @brief cos from the table.
 * @param[in] a  the angle, radians
 * @return its cosine
 */
static inline float fcos(float a)
{
    return fsin(a + 1.57079632679f);
}

/** @brief Moves the point on once. */
static inline void step(void)
{
    float x = s_x, y = s_y, a = s_p[0], b = s_p[1], c = s_p[2], d = s_p[3];
    if (s_kind == ATTR_CLIFFORD) {
        s_x = fsin(a * y) + c * fcos(a * x);
        s_y = fsin(b * x) + d * fcos(b * y);
    } else {
        s_x = fsin(a * y) - fcos(b * x);
        s_y = fsin(c * x) - fcos(d * y);
    }
}

/**
 * @brief Whether the current parameters spread the points well: run on, the second half counted on a
 *        coarse grid; and the bounds the points reach, for fitting the shape to the grid.
 * @param[out] bx0  gets the smallest x reached
 * @param[out] by0  gets the smallest y
 * @param[out] bx1  gets the largest x
 * @param[out] by1  gets the largest y
 * @return true if it spreads
 */
static bool spreads(float *bx0, float *by0, float *bx1, float *by1)
{
    static uint8_t seen[TEST_GRID * TEST_GRID / 8];
    memset(seen, 0, sizeof(seen));
    float hx = s_kind == ATTR_CLIFFORD ? 1.0f + fabsf(s_p[2]) : 2.0f;
    float hy = s_kind == ATTR_CLIFFORD ? 1.0f + fabsf(s_p[3]) : 2.0f;
    s_x = 0.1f;
    s_y = 0.1f;
    *bx0 = *by0 = 1e9f;
    *bx1 = *by1 = -1e9f;
    int cells = 0;
    for (int i = 0; i < TEST_N; i++) {
        step();
        if (i < TEST_N / 2) {
            continue;                               /* (settled first: no early spread counted) */
        }
        *bx0 = fminf(*bx0, s_x);
        *bx1 = fmaxf(*bx1, s_x);
        *by0 = fminf(*by0, s_y);
        *by1 = fmaxf(*by1, s_y);
        int gx = (int)((s_x + hx) / (2 * hx) * (TEST_GRID - 1) + 0.5f);
        int gy = (int)((s_y + hy) / (2 * hy) * (TEST_GRID - 1) + 0.5f);
        if (gx < 0 || gy < 0 || gx >= TEST_GRID || gy >= TEST_GRID) {
            continue;
        }
        int k = gy * TEST_GRID + gx;
        if (!(seen[k >> 3] & (1 << (k & 7)))) {
            seen[k >> 3] |= (uint8_t)(1 << (k & 7));
            cells++;
        }
    }
    return cells >= TEST_SPREAD;
}

/* (documented in saver_attractor_core.h) */
void attr_next(void)
{
    float bx0 = -1, by0 = -1, bx1 = 1, by1 = 1;
    bool found = false;
    for (int t = 0; t < ATTR_TRIES && !found; t++) {
        s_kind = (int)(rnd() % ATTR_KINDS);
        for (int k = 0; k < 4; k++) {
            s_p[k] = rnd_f(-3.0f, 3.0f);
        }
        if (s_kind == ATTR_CLIFFORD) {
            s_p[2] = rnd_f(-1.5f, 1.5f);            /* (c, d: the shape's width and height) */
            s_p[3] = rnd_f(-1.5f, 1.5f);
        }
        found = spreads(&bx0, &by0, &bx1, &by1);
    }
    if (!found) {                                   /* none found: a Clifford known to be good */
        s_kind = ATTR_CLIFFORD;
        static const float GOOD[4] = {-1.4f, 1.6f, 1.0f, 0.7f};
        memcpy(s_p, GOOD, sizeof(s_p));
        spreads(&bx0, &by0, &bx1, &by1);
    }
    float w = fmaxf(bx1 - bx0, 1e-3f), h = fmaxf(by1 - by0, 1e-3f);
    s_scale = (ATTR_W - 2 * ATTR_MARGIN) / fmaxf(w, h);
    s_cx = (bx0 + bx1) / 2;
    s_cy = (by0 + by1) / 2;
    s_box[0] = ATTR_W / 2 - (int)(w * s_scale / 2) - 2;
    s_box[2] = ATTR_W / 2 + (int)(w * s_scale / 2) + 2;
    s_box[1] = ATTR_W / 2 - (int)(h * s_scale / 2) - 2;
    s_box[3] = ATTR_W / 2 + (int)(h * s_scale / 2) + 2;
    for (int k = 0; k < 4; k++) {
        s_box[k] = s_box[k] < 0 ? 0 : s_box[k] >= ATTR_W ? ATTR_W - 1 : s_box[k];
    }
    if (s_count) {
        memset(s_count, 0, (size_t)ATTR_W * ATTR_W);
    }
}

/* (documented in saver_attractor_core.h) */
bool attr_start(uint32_t seed)
{
    if (!s_count) {
        s_count = malloc((size_t)ATTR_W * ATTR_W);
        s_sin = malloc((SIN_N + 1) * sizeof(float));
        if (!s_count || !s_sin) {
            attr_free();
            return false;
        }
        for (int i = 0; i <= SIN_N; i++) {          /* (one more: the turn's end, for between) */
            s_sin[i] = sinf((float)i * 6.28318530718f / SIN_N);
        }
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    attr_next();
    return true;
}

/* (documented in saver_attractor_core.h) */
void attr_free(void)
{
    free(s_count);
    free(s_sin);
    s_count = NULL;
    s_sin = NULL;
}

/* (documented in saver_attractor_core.h) */
int attr_plot(int n, uint32_t *dirty, int max_dirty)
{
    if (!s_count) {
        return 0;
    }
    int nd = 0;
    for (int i = 0; i < n; i++) {
        step();
        int x = (int)((s_x - s_cx) * s_scale) + ATTR_W / 2;
        int y = (int)((s_y - s_cy) * s_scale) + ATTR_W / 2;
        if ((unsigned)x >= ATTR_W || (unsigned)y >= ATTR_W) {
            continue;
        }
        s_box[0] = x < s_box[0] ? x : s_box[0];    /* (a point past the sample's bounds: the box grows) */
        s_box[1] = y < s_box[1] ? y : s_box[1];
        s_box[2] = x > s_box[2] ? x : s_box[2];
        s_box[3] = y > s_box[3] ? y : s_box[3];
        uint32_t k = (uint32_t)y * ATTR_W + (uint32_t)x;
        if (s_count[k] < ATTR_MAX) {
            s_count[k]++;
        }
        if (dirty && nd < max_dirty) {
            dirty[nd++] = k;
        }
    }
    return nd;
}

/* (documented in saver_attractor_core.h) */
const uint8_t *attr_counts(void)
{
    return s_count;
}

/* (documented in saver_attractor_core.h) */
void attr_box(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = s_box[0];
    *y0 = s_box[1];
    *x1 = s_box[2];
    *y1 = s_box[3];
}

/* (documented in saver_attractor_core.h) */
int attr_kind(void)
{
    return s_kind;
}

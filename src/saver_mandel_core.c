/**
 * @file saver_mandel_core.c
 * @ingroup saver_mandel_core
 * @brief A zoom into the Mandelbrot set, for the screen saver, without LVGL (drawn by ui_saver_mandel.c).
 */
#include "saver_mandel_core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HALF            (MANDEL_W / 2)        /**< a key's middle, px */
#define START_X         (-0.6f)               /**< level 0's middle */
#define START_STEER     170                   /**< level 0: how far the middle may move (anywhere round the set) */
#define PICKS           16                    /**< the autopilot chooses among this many most detailed spots */
#define FLOAT_ROOM      24.0f                 /**< a pixel at least this many float steps of its point: else the
                                               *   dive is as deep as floats allow */

static uint16_t *s_keys[2];               /**< the two keys: shown, being made (NULL: none) */
static uint32_t s_rng = 1;                /**< xorshift32 state (never 0) */

/* the shown key */
static int s_shown = -1;                  /**< which of s_keys (-1: none yet) */
static float s_cx, s_cy, s_d;             /**< its middle, a pixel's size */
static int s_level;                       /**< its level */
static int s_mx, s_my;                    /**< the next key's middle, from its middle, px */
static bool s_dive_end;                   /**< the next key begins a new dive */

/* the key being made */
static int m_buf;                         /**< which of s_keys */
static float m_cx, m_cy, m_d;             /**< its middle, a pixel's size */
static int m_level;                       /**< its level */
static int m_row;                         /**< rows done */
static bool m_reuse;                      /**< every other pixel from the shown key's middle */
static float m_eps;                       /**< the cycle check's closeness */
static int m_max;                         /**< steps at most */
static volatile int m_ready;              /**< done (set by the worker, cleared by mandel_advance()) */

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

/* (documented in saver_mandel_core.h) */
int mandel_max_iter(int level)
{
    const int n = 160 + 80 * level;
    return n > 4000 ? 4000 : n;
}

/* (documented in saver_mandel_core.h) */
uint16_t mandel_point(float cx, float cy, int max_iter, float eps)
{
    const float qx = cx - 0.25f, q = qx * qx + cy * cy;
    if (q * (q + qx) <= 0.25f * cy * cy || (cx + 1.0f) * (cx + 1.0f) + cy * cy <= 0.0625f) {
        return MANDEL_INSIDE;                       /* (the main cardioid, the big round bulb left of it) */
    }
    float zx = 0, zy = 0, px = 0, py = 0;
    int check = 8, count = 0;
    for (int n = 0; n < max_iter; n++) {
        const float x2 = zx * zx, y2 = zy * zy;
        if (x2 + y2 > MANDEL_BAILOUT) {             /* out: the count, smoothed, on a square-root scale */
            float nu = (float)n + 1.0f - log2f(0.5f * log2f(x2 + y2));
            nu = nu < 0 ? 0 : sqrtf(nu) * MANDEL_TONE;
            return (uint16_t)(nu > 65534.0f ? 65534.0f : nu);
        }
        zy = 2.0f * zx * zy + cy;
        zx = x2 - y2 + cx;
        if (fabsf(zx - px) + fabsf(zy - py) < eps) {
            return MANDEL_INSIDE;                   /* back where it was: a cycle, it never leaves */
        }
        if (++count == check) {                     /* (Brent's: remember z at 8, 16, 32 ... steps) */
            count = 0;
            check *= 2;
            px = zx;
            py = zy;
        }
    }
    return MANDEL_INSIDE;
}

/**
 * @brief The next key to make is a new dive's first: the whole set.
 * @param[in] buf  the key to make it in
 */
static void begin_dive(int buf)
{
    m_buf = buf;
    m_level = 0;
    m_cx = START_X;
    m_cy = 0;
    m_d = MANDEL_SPAN / MANDEL_W;
    m_reuse = false;
}

/** @brief The key's making starts from its first row. */
static void begin_key(void)
{
    m_row = 0;
    m_max = mandel_max_iter(m_level);
    m_eps = m_d * 0.01f;
    __atomic_store_n(&m_ready, 0, __ATOMIC_RELEASE);
}

/* (documented in saver_mandel_core.h) */
bool mandel_start(uint32_t seed)
{
    for (int k = 0; k < 2; k++) {
        if (!s_keys[k]) {
            s_keys[k] = malloc((size_t)MANDEL_W * MANDEL_W * sizeof(uint16_t));
        }
    }
    if (!s_keys[0] || !s_keys[1]) {
        mandel_free();
        return false;
    }
    s_rng = seed ? seed : 1;
    s_shown = -1;
    s_dive_end = false;
    s_mx = s_my = 0;
    begin_dive(0);
    begin_key();
    return true;
}

/* (documented in saver_mandel_core.h) */
void mandel_free(void)
{
    for (int k = 0; k < 2; k++) {
        free(s_keys[k]);
        s_keys[k] = NULL;
    }
    s_shown = -1;
}

/**
 * @brief The shown key's pixels around a spot are all inside: the ring one further out than the spot's own
 *        (up to 2 x 2) pixels, so a dot of outside between them isn't missed.
 * @param[in] up  the shown key's row above the spot (or on it), at the spot's pixels' column 0
 * @param[in] a  the spot's left pixel's column
 * @param[in] b  its right pixel's column (a for an even pixel)
 * @param[in] odd  the spot is between two rows (else on one)
 * @return true if they are
 */
static bool is_inside_around(const uint16_t *up, int a, int b, bool odd)
{
    for (int r = -1; r <= (odd ? 2 : 1); r++) {
        const uint16_t *row = up + r * MANDEL_W;
        if (row[a - 1] != MANDEL_INSIDE || row[b + 1] != MANDEL_INSIDE || row[a] != MANDEL_INSIDE ||
                row[b] != MANDEL_INSIDE) {
            return false;
        }
    }
    return true;
}

/* (documented in saver_mandel_core.h) */
bool mandel_work(int rows)
{
    if (!s_keys[0] || __atomic_load_n(&m_ready, __ATOMIC_ACQUIRE)) {
        return true;
    }
    uint16_t *out = s_keys[m_buf];
    const uint16_t *prev = m_reuse ? s_keys[s_shown] : NULL;
    for (; rows > 0 && m_row < MANDEL_W; rows--, m_row++) {
        const int j = m_row;
        uint16_t *o = out + j * MANDEL_W;
        const float cy = m_cy + (float)(j - HALF) * m_d;
        /* the shown key's rows above and below this one (the same one for an even row): its pixels there are
         * this key's even ones, half as far from its new middle */
        const uint16_t *up = prev ? prev + (HALF / 2 + s_my + j / 2) * MANDEL_W + HALF / 2 + s_mx : NULL;
        const uint16_t *down = up && (j & 1) ? up + MANDEL_W : up;
        for (int i = 0; i < MANDEL_W; i++) {
            if (up) {
                const int a = i / 2, b = (i + 1) / 2;   /* (the same one for an even pixel) */
                if (!(i & 1) && !(j & 1) && up[a] != MANDEL_INSIDE) {
                    o[i] = up[a];                   /* (an inside one is checked again: more steps now) */
                    continue;
                }
                if (up[a] == MANDEL_INSIDE && up[b] == MANDEL_INSIDE && down[a] == MANDEL_INSIDE &&
                        down[b] == MANDEL_INSIDE && is_inside_around(up, a, b, j & 1)) {
                    o[i] = MANDEL_INSIDE;           /* inside all round: inside (the set has no holes) */
                    continue;
                }
            }
            o[i] = mandel_point(m_cx + (float)(i - HALF) * m_d, cy, m_max, m_eps);
        }
    }
    if (m_row < MANDEL_W) {
        return false;
    }
    __atomic_store_n(&m_ready, 1, __ATOMIC_RELEASE);
    return true;
}

/* (documented in saver_mandel_core.h) */
float mandel_progress(void)
{
    return (float)m_row / MANDEL_W;
}

/* (documented in saver_mandel_core.h) */
bool is_mandel_ready(void)
{
    return __atomic_load_n(&m_ready, __ATOMIC_ACQUIRE) != 0;
}

/**
 * @brief The autopilot: where the next key's middle goes, among the shown key's most detailed spots (the
 *        highest counts outside the set) near its middle, one of the PICKS best at random.
 * @param[out] mx  gets how far across, px
 * @param[out] my  gets how far down, px
 */
static void steer(int *mx, int *my)
{
    const uint16_t *key = s_keys[s_shown];
    const int reach = s_level ? MANDEL_STEER : START_STEER;
    int best_v[PICKS], best_x[PICKS], best_y[PICKS], n = 0;
    for (int dy = -reach; dy <= reach; dy += 4) {
        for (int dx = -reach; dx <= reach; dx += 4) {
            const uint16_t v = key[(HALF + dy) * MANDEL_W + HALF + dx];
            if (v == MANDEL_INSIDE) {
                continue;
            }
            int k = n < PICKS ? n++ : PICKS;        /* (kept sorted, the highest first) */
            while (k > 0 && best_v[k - 1] < v) {
                if (k < PICKS) {
                    best_v[k] = best_v[k - 1];
                    best_x[k] = best_x[k - 1];
                    best_y[k] = best_y[k - 1];
                }
                k--;
            }
            if (k < PICKS) {
                best_v[k] = v;
                best_x[k] = dx;
                best_y[k] = dy;
            }
        }
    }
    if (!n) {
        *mx = *my = 0;                              /* (all inside: straight on) */
        return;
    }
    const int k = (int)(rnd() % (uint32_t)n);
    *mx = best_x[k];
    *my = best_y[k];
}

/**
 * @brief Can a key one level deeper than the shown one still be told apart pixel from pixel in floats?
 * @return true if so
 */
static bool is_deeper_ok(void)
{
    float m = fabsf(s_cx) > fabsf(s_cy) ? fabsf(s_cx) : fabsf(s_cy);
    int e;
    frexpf(m > 0.25f ? m : 0.25f, &e);
    return s_level + 1 < MANDEL_LEVELS && s_d * 0.5f >= FLOAT_ROOM * ldexpf(1.0f, e - 24);
}

/* (documented in saver_mandel_core.h) */
void mandel_advance(void)
{
    if (!is_mandel_ready()) {
        return;
    }
    s_shown = m_buf;
    s_cx = m_cx;
    s_cy = m_cy;
    s_d = m_d;
    s_level = m_level;
    s_dive_end = !is_deeper_ok();
    if (s_dive_end) {
        s_mx = s_my = 0;
        begin_dive(1 - s_shown);
    } else {
        steer(&s_mx, &s_my);
        m_buf = 1 - s_shown;
        m_level = s_level + 1;
        m_cx = s_cx + (float)s_mx * s_d;
        m_cy = s_cy + (float)s_my * s_d;
        m_d = s_d * 0.5f;
        m_reuse = true;
    }
    begin_key();
}

/* (documented in saver_mandel_core.h) */
bool is_mandel_shown(void)
{
    return s_shown >= 0;
}

/* (documented in saver_mandel_core.h) */
bool is_mandel_dive_end(void)
{
    return s_dive_end;
}

/* (documented in saver_mandel_core.h) */
int mandel_level(void)
{
    return s_level;
}

/* (documented in saver_mandel_core.h) */
void mandel_frame(uint16_t *out, float p, const uint16_t pal[1024], uint32_t shift, uint16_t inside)
{
    if (s_shown < 0) {
        return;
    }
    static int16_t xm[MANDEL_W];
    static uint16_t line[MANDEL_W];                 /* (a row made here, in fast memory, then copied out) */
    const uint16_t *key = s_keys[s_shown];
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    const float s = exp2f(p), ox = HALF + p * (float)s_mx, oy = HALF + p * (float)s_my;
    for (int x = 0; x < MANDEL_W; x++) {            /* each column's pixel of the key */
        int k = (int)floorf(ox + ((float)x + 0.5f - HALF) / s);
        xm[x] = (int16_t)(k < 0 ? 0 : k >= MANDEL_W ? MANDEL_W - 1 : k);
    }
    int made = -1;                                  /* the key's row in line */
    for (int y = 0; y < MANDEL_W; y++) {
        int k = (int)floorf(oy + ((float)y + 0.5f - HALF) / s);
        k = k < 0 ? 0 : k >= MANDEL_W ? MANDEL_W - 1 : k;
        if (k != made) {                            /* (zoomed in, rows repeat: made once) */
            const uint16_t *row = key + k * MANDEL_W;
            for (int x = 0; x < MANDEL_W; x++) {
                const uint16_t v = row[xm[x]];
                line[x] = v == MANDEL_INSIDE ? inside : pal[(v + shift) & 1023];
            }
            made = k;
        }
        memcpy(out + y * MANDEL_W, line, sizeof(line));
    }
}

/* (documented in saver_mandel_core.h) */
const uint16_t *mandel_key(void)
{
    return s_shown >= 0 ? s_keys[s_shown] : NULL;
}

/* (documented in saver_mandel_core.h) */
void mandel_view(float *cx, float *cy, float *d, int *mx, int *my)
{
    *cx = s_cx;
    *cy = s_cy;
    *d = s_d;
    *mx = s_mx;
    *my = s_my;
}

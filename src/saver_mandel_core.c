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
#define START_X         (-0.75f)              /**< level 0's middle */
#define START_STEER     170                   /**< level 0: how far the middle may move (anywhere round the set) */
#define PICKS           16                    /**< the autopilot chooses among this many most detailed spots */
#define FX_FRAC         123                   /**< fixed point: fraction bits (Q5.123: a sign, 4 integer bits) */
#define REF_ESCAPE      4.0f                  /**< the reference orbit stops once |Z|^2 is past this */
#define REF_N           (MANDEL_MAX_ITER + 1) /**< the reference orbit's Zs at most */

/** @brief A number in signed fixed point, Q5.123: four 32-bit words, the lowest first, two's complement. */
typedef struct {
    uint32_t w[4];                        /**< the words */
} fx_t;

static uint16_t *s_keys[2];               /**< the two keys: shown, being made (NULL: none) */
static uint32_t s_rng = 1;                /**< xorshift32 state (never 0) */

/** @brief A reference orbit's Z: rounded to floats, and what the rounding left off (Z = x + rx, y + ry, to some
 *         48 bits); the four side by side, one cache read a step. */
typedef struct {
    float x, y;                           /**< Z, rounded */
    float rx, ry;                         /**< the rest */
} ref_t;

static ref_t *s_ref;                      /**< the reference orbit, REF_N Zs */

/* the shown key */
static int s_shown = -1;                  /**< which of s_keys (-1: none yet) */
static fx_t s_fx, s_fy;                   /**< its middle, exactly */
static float s_cx, s_cy, s_d;             /**< its middle (roughly), a pixel's size */
static int s_level;                       /**< its level */
static int s_mx, s_my;                    /**< the next key's middle, from its middle, px */
static bool s_dive_end;                   /**< the next key begins a new dive */

/* the key being made */
static int m_buf;                         /**< which of s_keys */
static fx_t m_fx, m_fy;                   /**< its middle, exactly */
static float m_cx, m_cy, m_d;             /**< its middle (roughly), a pixel's size */
static int m_ref_len;                     /**< its reference orbit: Z_0 .. Z_len (-1: not worked out yet) */
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
    return n > MANDEL_MAX_ITER ? MANDEL_MAX_ITER : n;
}

/* ---------------------------------------------------------------- 128-bit fixed point */

/**
 * @brief Is it below 0?
 * @param[in] a  the number
 * @return true if so
 */
static bool is_fx_neg(const fx_t *a)
{
    return (a->w[3] >> 31) != 0;
}

/**
 * @brief Minus it.
 * @param[in] a  the number
 * @return -a
 */
static fx_t fx_neg(fx_t a)
{
    uint32_t carry = 1;
    for (int k = 0; k < 4; k++) {
        const uint32_t v = ~a.w[k] + carry;
        carry = carry && v == 0;
        a.w[k] = v;
    }
    return a;
}

/**
 * @brief A sum.
 * @param[in] a  one
 * @param[in] b  the other
 * @return a + b
 */
static fx_t fx_add(fx_t a, fx_t b)
{
    uint64_t carry = 0;
    for (int k = 0; k < 4; k++) {
        carry += (uint64_t)a.w[k] + b.w[k];
        a.w[k] = (uint32_t)carry;
        carry >>= 32;
    }
    return a;
}

/**
 * @brief A product (the 256-bit product's bits FX_FRAC up, the signs set aside; no overflow check: the orbit
 *        stops well before).
 * @param[in] a  one
 * @param[in] b  the other
 * @return a b
 */
static fx_t fx_mul(fx_t a, fx_t b)
{
    const bool na = is_fx_neg(&a), nb = is_fx_neg(&b);
    a = na ? fx_neg(a) : a;
    b = nb ? fx_neg(b) : b;
    uint32_t p[8] = {0};
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4; j++) {
            carry += (uint64_t)a.w[i] * b.w[j] + p[i + j];
            p[i + j] = (uint32_t)carry;
            carry >>= 32;
        }
        p[i + 4] = (uint32_t)carry;
    }
    fx_t r;                                     /* (123 = 3 words and 27 bits) */
    for (int k = 0; k < 4; k++) {
        r.w[k] = p[k + 3] >> (FX_FRAC - 96) | p[k + 4] << (128 - FX_FRAC);
    }
    return na != nb ? fx_neg(r) : r;
}

/**
 * @brief A whole number times a power of two, in fixed point.
 * @param[in] v  the number (|v| below 2^24)
 * @param[in] e  the power: v 2^e
 * @return it (0 if it is below the fixed point's last bit)
 */
static fx_t fx_from_int(int32_t v, int e)
{
    fx_t r = {{0, 0, 0, 0}};
    uint32_t m = (uint32_t)(v < 0 ? -v : v);
    int sh = FX_FRAC + e;                           /* where m's lowest bit goes */
    if (sh < 0) {
        m = sh > -32 ? m >> -sh : 0;
        sh = 0;
    }
    const int k = sh / 32, b = sh % 32;
    if (k < 4) {
        r.w[k] = m << b;
        if (b && k + 1 < 4) {
            r.w[k + 1] = m >> (32 - b);
        }
    }
    return v < 0 ? fx_neg(r) : r;
}

/**
 * @brief A float in fixed point, exactly.
 * @param[in] f  the float (|f| below 16)
 * @return it
 */
static fx_t fx_from_float(float f)
{
    int e;
    const float m = frexpf(f, &e);                  /* f = m 2^e, |m| in [0.5, 1) */
    return fx_from_int((int32_t)ldexpf(m, 24), e - 24);
}

/**
 * @brief A fixed point number as a float (rounded down to 24 bits or so).
 * @param[in] a  the number
 * @return it
 */
static float fx_to_float(fx_t a)
{
    const bool neg = is_fx_neg(&a);
    a = neg ? fx_neg(a) : a;
    int k = 3;
    while (k > 0 && !a.w[k]) {
        k--;
    }
    float f = ldexpf((float)a.w[k], 32 * k - FX_FRAC);
    if (k > 0) {
        f += ldexpf((float)a.w[k - 1], 32 * (k - 1) - FX_FRAC);
    }
    return neg ? -f : f;
}

/* ---------------------------------------------------------------- points */

/**
 * @brief An escaped point's pixel value: its count smoothed, on a square-root scale.
 * @param[in] n  the step z escaped at
 * @param[in] r2  |z|^2 then
 * @return the value (up to 0xFFFE)
 */
static uint16_t escaped(int n, float r2)
{
    float nu = (float)n + 1.0f - log2f(0.5f * log2f(r2));
    nu = nu < 0 ? 0 : sqrtf(nu) * MANDEL_TONE;
    return (uint16_t)(nu > 65534.0f ? 65534.0f : nu);
}

/**
 * @brief The reference orbit, from 0, in fixed point, kept in s_ref (as floats and their rests): Z_0 = 0, Z_k+1 = Z_k^2 + C, up to
 *        the steps given or until it escapes.
 * @param[in] cx  C's x
 * @param[in] cy  C's y
 * @param[in] max_iter  steps at most
 * @return the last Z kept, len: Z_0 .. Z_len (1 at least)
 */
static int make_ref(fx_t cx, fx_t cy, int max_iter)
{
    fx_t zx = {{0, 0, 0, 0}}, zy = {{0, 0, 0, 0}};
    s_ref[0] = (ref_t) {0, 0, 0, 0};
    int n = 1;
    for (; n <= max_iter; n++) {
        const fx_t x2 = fx_mul(zx, zx), y2 = fx_mul(zy, zy), xy = fx_mul(zx, zy);
        zx = fx_add(fx_add(x2, fx_neg(y2)), cx);
        zy = fx_add(fx_add(xy, xy), cy);
        ref_t *r = &s_ref[n];
        r->x = fx_to_float(zx);
        r->y = fx_to_float(zy);
        r->rx = fx_to_float(fx_add(zx, fx_neg(fx_from_float(r->x))));   /* (the rest the float left off) */
        r->ry = fx_to_float(fx_add(zy, fx_neg(fx_from_float(r->y))));
        if (r->x * r->x + r->y * r->y > REF_ESCAPE) {
            break;
        }
    }
    return n > max_iter ? max_iter : n;
}

/**
 * @brief A point near the reference: its difference from the reference orbit iterated in floats (dz -> 2 Z dz +
 *        dz^2 + dc), z = Z + dz; when z comes nearer 0 than dz is, or the reference runs out, z itself becomes
 *        the difference from the orbit's start (rebasing, so the difference stays small next to Z). z is (Z's
 *        float + dz) + the rest of Z: near a rebase Z's float and dz nearly cancel, which floats add exactly,
 *        and the rest keeps the result as good as the fixed point's (Z's float alone would be off by its
 *        rounding, more than a deep pixel's size).
 * @param[in] dcx  its x minus the reference's
 * @param[in] dcy  its y minus the reference's
 * @param[in] max_iter  steps at most
 * @param[in] len  the reference orbit's last Z (make_ref())
 * @return its pixel value, or MANDEL_INSIDE
 */
static uint16_t point_near(float dcx, float dcy, int max_iter, int len)
{
    float dx = 0, dy = 0;
    int m = 0;
    for (int n = 0; n < max_iter; n++) {
        const float zx0 = s_ref[m].x, zy0 = s_ref[m].y;
        const float ndx = 2.0f * (zx0 * dx - zy0 * dy) + dx * dx - dy * dy + dcx;
        dy = 2.0f * (zx0 * dy + zy0 * dx) + 2.0f * dx * dy + dcy;
        dx = ndx;
        m++;
        const ref_t *r = &s_ref[m];
        const float zx = (r->x + dx) + r->rx, zy = (r->y + dy) + r->ry, r2 = zx * zx + zy * zy;
        if (r2 > MANDEL_BAILOUT) {
            return escaped(n + 1, r2);
        }
        if (r2 < dx * dx + dy * dy || m == len) {
            dx = zx;                                /* (rebased: from Z_0 = 0 again) */
            dy = zy;
            m = 0;
        }
    }
    return MANDEL_INSIDE;
}

/* (documented in saver_mandel_core.h) */
uint16_t mandel_point_near(float cx, float cy, float dcx, float dcy, int max_iter)
{
    if (!s_ref || max_iter > MANDEL_MAX_ITER) {
        return 0;
    }
    return point_near(dcx, dcy, max_iter, make_ref(fx_from_float(cx), fx_from_float(cy), max_iter));
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
        if (x2 + y2 > MANDEL_BAILOUT) {             /* out */
            return escaped(n, x2 + y2);
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
    m_fx = fx_from_float(START_X);
    m_fy = fx_from_int(0, 0);
    m_reuse = false;
}

/** @brief The key's making starts from its first row. */
static void begin_key(void)
{
    m_row = 0;
    m_cx = fx_to_float(m_fx);
    m_cy = fx_to_float(m_fy);
    m_d = ldexpf(1.0f, -(MANDEL_D0_EXP + m_level));   /* (a power of two: the middle moves exactly) */
    m_max = mandel_max_iter(m_level);
    m_eps = m_d * 0.01f;
    m_ref_len = -1;
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
    if (!s_ref) {
        s_ref = malloc(REF_N * sizeof(ref_t));
    }
    if (!s_keys[0] || !s_keys[1] || !s_ref) {
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
    free(s_ref);
    s_ref = NULL;
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
    const bool near = m_level > MANDEL_PLAIN_LEVELS;   /* (deeper: from the reference orbit) */
    if (near && m_ref_len < 0) {
        m_ref_len = make_ref(m_fx, m_fy, m_max);   /* (the key's first call: the orbit at its middle) */
        rows--;
    }
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
            o[i] = near ? point_near((float)(i - HALF) * m_d, (float)(j - HALF) * m_d, m_max, m_ref_len)
                   : mandel_point(m_cx + (float)(i - HALF) * m_d, cy, m_max, m_eps);
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
 * @return false if there's no spot outside the set near the middle (sunk into it: time for a new dive)
 */
static bool steer(int *mx, int *my)
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
    *mx = *my = 0;
    if (!n) {
        return false;
    }
    const int k = (int)(rnd() % (uint32_t)n);
    *mx = best_x[k];
    *my = best_y[k];
    return true;
}

/* (documented in saver_mandel_core.h) */
void mandel_advance(void)
{
    if (!is_mandel_ready()) {
        return;
    }
    s_shown = m_buf;
    s_fx = m_fx;
    s_fy = m_fy;
    s_cx = m_cx;
    s_cy = m_cy;
    s_d = m_d;
    s_level = m_level;
    s_dive_end = s_level + 1 >= MANDEL_LEVELS || !steer(&s_mx, &s_my);
    if (s_dive_end) {
        s_mx = s_my = 0;
        begin_dive(1 - s_shown);
    } else {
        m_buf = 1 - s_shown;
        m_level = s_level + 1;
        const int e = -(MANDEL_D0_EXP + s_level);   /* (the shown key's pixel: 2^e, exactly) */
        m_fx = fx_add(s_fx, fx_from_int(s_mx, e));
        m_fy = fx_add(s_fy, fx_from_int(s_my, e));
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

/**
 * @brief A fixed point number as a double (to the double's 53 bits).
 * @param[in] a  the number
 * @return it
 */
static double fx_to_double(fx_t a)
{
    const bool neg = is_fx_neg(&a);
    a = neg ? fx_neg(a) : a;
    double f = 0;
    for (int k = 0; k < 4; k++) {
        f += ldexp((double)a.w[k], 32 * k - FX_FRAC);
    }
    return neg ? -f : f;
}

/* (documented in saver_mandel_core.h) */
void mandel_middle(double *cx, double *cy)
{
    *cx = fx_to_double(s_fx);
    *cy = fx_to_double(s_fy);
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

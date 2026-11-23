/**
 * @file pendulum_core.c
 * @ingroup pendulum_core
 * @brief Double pendulums for the screen saver, without LVGL (drawn by ui_saver_pendulum.c).
 *
 * Equal masses m and rods L (both 1), gravity G: with the angles a1, a2 from straight down and their
 * speeds w1, w2, and d = a1 - a2, D = 3 - cos 2d,
 *   a1'' = (-3G sin a1 - G sin(a1 - 2 a2) - 2 sin d (w2^2 + w1^2 cos d)) / D
 *   a2'' = 2 sin d (2 w1^2 + 2G cos a1 + w2^2 cos d) / D
 */
#include "pendulum_core.h"
#include <math.h>
#include <stdlib.h>

#define G               9.81f                 /**< gravity (rods of length 1) */

/** @brief A pendulum's state: the angles and their speeds. */
typedef struct {
    float a1, a2, w1, w2;                 /**< radians from straight down, and radians a second */
} pend_t;

/** @brief A trail's place: where the lower bob was, and how fast it went there. */
typedef struct {
    float x, y, fast;                     /**< px, px, 0..1 */
} place_t;

static pend_t s_p[PEND_N];                /**< the pendulums */
static place_t (*s_trail)[PEND_TRAIL];    /**< each lower bob's places, a ring (NULL: none) */
static int s_head;                        /**< the newest place in the rings */
static int s_len;                         /**< places kept, 0..PEND_TRAIL */
static int s_frame;                       /**< frames since they started */
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
 * @brief How a state changes: the angles' speeds and accelerations.
 * @param[in] p  the state
 * @return its rate of change (a1', a2', w1', w2' in the fields a1, a2, w1, w2)
 */
static pend_t deriv(const pend_t *p)
{
    float d = p->a1 - p->a2, sd = sinf(d), cd = cosf(d);
    float den = 3.0f - cosf(2 * d);
    pend_t r;
    r.a1 = p->w1;
    r.a2 = p->w2;
    r.w1 = (-3 * G * sinf(p->a1) - G * sinf(p->a1 - 2 * p->a2) - 2 * sd * (p->w2 * p->w2 + p->w1 * p->w1 * cd)) / den;
    r.w2 = 2 * sd * (2 * p->w1 * p->w1 + 2 * G * cosf(p->a1) + p->w2 * p->w2 * cd) / den;
    return r;
}

/**
 * @brief A state moved on along a rate of change.
 * @param[in] p  the state
 * @param[in] k  the rate
 * @param[in] h  for how long, s
 * @return p + k h
 */
static pend_t add(const pend_t *p, const pend_t *k, float h)
{
    pend_t r = {p->a1 + k->a1 * h, p->a2 + k->a2 * h, p->w1 + k->w1 * h, p->w2 + k->w2 * h};
    return r;
}

/**
 * @brief One Runge-Kutta (RK4) step of PEND_DT.
 * @param[in,out] s  the pendulum
 */
static void rk4(pend_t *s)
{
    const float h = PEND_DT;
    pend_t k1 = deriv(s);
    pend_t t = add(s, &k1, h / 2);
    pend_t k2 = deriv(&t);
    t = add(s, &k2, h / 2);
    pend_t k3 = deriv(&t);
    t = add(s, &k3, h);
    pend_t k4 = deriv(&t);
    s->a1 += h / 6 * (k1.a1 + 2 * k2.a1 + 2 * k3.a1 + k4.a1);
    s->a2 += h / 6 * (k1.a2 + 2 * k2.a2 + 2 * k3.a2 + k4.a2);
    s->w1 += h / 6 * (k1.w1 + 2 * k2.w1 + 2 * k3.w1 + k4.w1);
    s->w2 += h / 6 * (k1.w2 + 2 * k2.w2 + 2 * k3.w2 + k4.w2);
}

/* (documented in pendulum_core.h) */
void pend_bobs(int k, float *x1, float *y1, float *x2, float *y2)
{
    const pend_t *s = &s_p[k];
    *x1 = PEND_W / 2.0f + PEND_ARM * sinf(s->a1);
    *y1 = PEND_W / 2.0f + PEND_ARM * cosf(s->a1);
    *x2 = *x1 + PEND_ARM * sinf(s->a2);
    *y2 = *y1 + PEND_ARM * cosf(s->a2);
}

/**
 * @brief How fast a lower bob goes.
 * @param[in] s  the pendulum
 * @return 0 (still) .. 1 (PEND_FAST or more)
 */
static float bob_fast(const pend_t *s)
{
    float vx = s->w1 * cosf(s->a1) + s->w2 * cosf(s->a2), vy = s->w1 * sinf(s->a1) + s->w2 * sinf(s->a2);
    float v = sqrtf(vx * vx + vy * vy) / PEND_FAST;
    return v > 1 ? 1 : v;
}

/** @brief Every lower bob's place now added to its trail. */
static void keep(void)
{
    s_head = (s_head + 1) % PEND_TRAIL;
    for (int k = 0; k < PEND_N; k++) {
        float x1, y1;
        place_t *p = &s_trail[k][s_head];
        pend_bobs(k, &x1, &y1, &p->x, &p->y);
        p->fast = bob_fast(&s_p[k]);
    }
    if (s_len < PEND_TRAIL) {
        s_len++;
    }
}

/* (documented in pendulum_core.h) */
void pend_set(float a1, float a2)
{
    for (int k = 0; k < PEND_N; k++) {
        s_p[k].a1 = a1 + k * PEND_NUDGE;
        s_p[k].a2 = a2;
        s_p[k].w1 = s_p[k].w2 = 0;
    }
    s_len = 0;
    s_frame = 0;
    if (s_trail) {
        keep();
    }
}

/** @brief A random high start: the upper rods well up (90 to 170 degrees either way), the lower anywhere. */
static void pend_random(void)
{
    float a1 = rnd_f(1.57f, 2.97f) * (rnd() & 1 ? 1.0f : -1.0f);
    pend_set(a1, rnd_f(-3.14159f, 3.14159f));
}

/* (documented in pendulum_core.h) */
bool pend_start(uint32_t seed)
{
    if (!s_trail) {
        s_trail = malloc(PEND_N * sizeof(*s_trail));
        if (!s_trail) {
            return false;
        }
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    pend_random();
    return true;
}

/* (documented in pendulum_core.h) */
void pend_free(void)
{
    free(s_trail);
    s_trail = NULL;
    s_len = 0;
}

/* (documented in pendulum_core.h) */
bool pend_step(void)
{
    if (!s_trail) {
        return false;
    }
    if (++s_frame >= PEND_RUN) {
        pend_random();
        return true;
    }
    for (int i = 1; i <= PEND_SUBSTEPS; i++) {
        for (int k = 0; k < PEND_N; k++) {
            rk4(&s_p[k]);
        }
        if (i % (PEND_SUBSTEPS / PEND_PER_FRAME) == 0) {
            keep();
        }
    }
    return false;
}

/* (documented in pendulum_core.h) */
int pend_trail_len(void)
{
    return s_len;
}

/* (documented in pendulum_core.h) */
void pend_trail(int k, int age, float *x, float *y, float *fast)
{
    const place_t *p = &s_trail[k][((s_head - age) % PEND_TRAIL + PEND_TRAIL) % PEND_TRAIL];
    *x = p->x;
    *y = p->y;
    *fast = p->fast;
}

/* (documented in pendulum_core.h) */
float pend_energy(int k)
{
    const pend_t *s = &s_p[k];
    float cd = cosf(s->a1 - s->a2);
    float kin = s->w1 * s->w1 + 0.5f * s->w2 * s->w2 + s->w1 * s->w2 * cd;    /* (m = L = 1) */
    float pot = -G * (2 * cosf(s->a1) + cosf(s->a2));
    return kin + pot;
}

/* (documented in pendulum_core.h) */
void pend_angles(int k, float *a1, float *a2)
{
    *a1 = s_p[k].a1;
    *a2 = s_p[k].a2;
}

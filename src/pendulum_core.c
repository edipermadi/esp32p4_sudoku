/**
 * @file pendulum_core.c
 * @ingroup pendulum_core
 * @brief A double pendulum for the screen saver, without LVGL (drawn by ui_saver_pendulum.c).
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

/** @brief The swing's state: the angles and their speeds. */
typedef struct {
    float a1, a2, w1, w2;                 /**< radians from straight down, and radians a second */
} pend_t;

static pend_t s;                          /**< the swing */
static float (*s_trail)[2];               /**< the lower bob's places, a ring (NULL: none) */
static int s_head;                        /**< the newest place in the ring */
static int s_len;                         /**< places kept, 0..PEND_TRAIL */
static int s_frame;                       /**< frames since it started */
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
 * @brief How the state changes: the angles' speeds and accelerations.
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

/** @brief One Runge-Kutta (RK4) step of PEND_DT. */
static void rk4(void)
{
    const float h = PEND_DT;
    pend_t k1 = deriv(&s);
    pend_t t = add(&s, &k1, h / 2);
    pend_t k2 = deriv(&t);
    t = add(&s, &k2, h / 2);
    pend_t k3 = deriv(&t);
    t = add(&s, &k3, h);
    pend_t k4 = deriv(&t);
    s.a1 += h / 6 * (k1.a1 + 2 * k2.a1 + 2 * k3.a1 + k4.a1);
    s.a2 += h / 6 * (k1.a2 + 2 * k2.a2 + 2 * k3.a2 + k4.a2);
    s.w1 += h / 6 * (k1.w1 + 2 * k2.w1 + 2 * k3.w1 + k4.w1);
    s.w2 += h / 6 * (k1.w2 + 2 * k2.w2 + 2 * k3.w2 + k4.w2);
}

/* (documented in pendulum_core.h) */
void pend_bobs(float *x1, float *y1, float *x2, float *y2)
{
    *x1 = PEND_W / 2 + PEND_ARM * sinf(s.a1);
    *y1 = PEND_W / 2 + PEND_ARM * cosf(s.a1);
    *x2 = *x1 + PEND_ARM * sinf(s.a2);
    *y2 = *y1 + PEND_ARM * cosf(s.a2);
}

/** @brief The lower bob's place now added to the trail. */
static void keep(void)
{
    float x1, y1, x2, y2;
    pend_bobs(&x1, &y1, &x2, &y2);
    s_head = (s_head + 1) % PEND_TRAIL;
    s_trail[s_head][0] = x2;
    s_trail[s_head][1] = y2;
    if (s_len < PEND_TRAIL) {
        s_len++;
    }
}

/* (documented in pendulum_core.h) */
void pend_set(float a1, float a2)
{
    s.a1 = a1;
    s.a2 = a2;
    s.w1 = s.w2 = 0;
    s_len = 0;
    s_frame = 0;
    if (s_trail) {
        keep();
    }
}

/**
 * @brief A random high start: the upper rod well up (90 to 170 degrees either way), the lower anywhere.
 */
static void pend_random(void)
{
    float a1 = rnd_f(1.57f, 2.97f) * (rnd() & 1 ? 1.0f : -1.0f);
    pend_set(a1, rnd_f(-3.14159f, 3.14159f));
}

/* (documented in pendulum_core.h) */
bool pend_start(uint32_t seed)
{
    if (!s_trail) {
        s_trail = malloc(PEND_TRAIL * sizeof(*s_trail));
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
        rk4();
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
void pend_trail(int age, float *x, float *y)
{
    int i = ((s_head - age) % PEND_TRAIL + PEND_TRAIL) % PEND_TRAIL;
    *x = s_trail[i][0];
    *y = s_trail[i][1];
}

/* (documented in pendulum_core.h) */
float pend_energy(void)
{
    float cd = cosf(s.a1 - s.a2);
    float kin = s.w1 * s.w1 + 0.5f * s.w2 * s.w2 + s.w1 * s.w2 * cd;    /* (m = L = 1) */
    float pot = -G * (2 * cosf(s.a1) + cosf(s.a2));
    return kin + pot;
}

/* (documented in pendulum_core.h) */
void pend_angles(float *a1, float *a2)
{
    *a1 = s.a1;
    *a2 = s.a2;
}

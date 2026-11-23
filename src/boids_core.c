/**
 * @file boids_core.c
 * @ingroup boids_core
 * @brief A flock of boids for the screen saver, without LVGL (drawn by ui_saver_boids.c).
 */
#include "boids_core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W_SEPARATE      0.05f                 /**< how hard a bird keeps apart */
#define W_ALIGN         0.05f                 /**< how hard it matches its neighbours' heading */
#define W_COHERE        0.0008f               /**< how hard it heads for their middle */
#define W_EDGE          0.35f                 /**< how hard it turns away from an edge */
#define W_WANDER        0.12f                 /**< how much it wanders, at most */

/** @brief A bird. */
typedef struct {
    float vx, vy;                         /**< its velocity, px a step */
    float tx[BOIDS_TRAIL], ty[BOIDS_TRAIL];   /**< where it was, the newest first */
} boid_t;

static boid_t *s_b;                       /**< the flock (NULL: none) */
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

/* (documented in boids_core.h) */
bool boids_start(uint32_t seed)
{
    if (!s_b) {
        s_b = malloc(BOIDS_N * sizeof(*s_b));
        if (!s_b) {
            return false;
        }
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < BOIDS_N; i++) {
        boid_t *b = &s_b[i];
        float x = rnd_f(BOIDS_EDGE, BOIDS_W - BOIDS_EDGE), y = rnd_f(BOIDS_EDGE, BOIDS_W - BOIDS_EDGE);
        float a = rnd_f(0, 6.2831853f), v = rnd_f(BOIDS_SPEED_MIN, BOIDS_SPEED_MAX);
        b->vx = cosf(a) * v;
        b->vy = sinf(a) * v;
        for (int k = 0; k < BOIDS_TRAIL; k++) {
            b->tx[k] = x;
            b->ty[k] = y;
        }
    }
    return true;
}

/* (documented in boids_core.h) */
void boids_free(void)
{
    free(s_b);
    s_b = NULL;
}

/* (documented in boids_core.h) */
void boids_step(void)
{
    if (!s_b) {
        return;
    }
    static float nvx[BOIDS_N], nvy[BOIDS_N];
    for (int i = 0; i < BOIDS_N; i++) {
        const boid_t *b = &s_b[i];
        float x = b->tx[0], y = b->ty[0];
        float sx = 0, sy = 0, ax = 0, ay = 0, cx = 0, cy = 0;
        int n = 0;
        for (int j = 0; j < BOIDS_N; j++) {
            if (j == i) {
                continue;
            }
            float dx = s_b[j].tx[0] - x, dy = s_b[j].ty[0] - y;
            float d2 = dx * dx + dy * dy;
            if (d2 > BOIDS_SEE * BOIDS_SEE) {
                continue;
            }
            n++;
            ax += s_b[j].vx;
            ay += s_b[j].vy;
            cx += dx;
            cy += dy;
            if (d2 < BOIDS_APART * BOIDS_APART && d2 > 1e-4f) {
                float d = sqrtf(d2);
                sx -= dx / d * (BOIDS_APART - d);   /* (harder the nearer) */
                sy -= dy / d * (BOIDS_APART - d);
            }
        }
        float vx = b->vx, vy = b->vy;
        if (n) {
            vx += sx * W_SEPARATE + (ax / n - vx) * W_ALIGN + cx / n * W_COHERE;
            vy += sy * W_SEPARATE + (ay / n - vy) * W_ALIGN + cy / n * W_COHERE;
        }
        vx += rnd_f(-W_WANDER, W_WANDER);
        vy += rnd_f(-W_WANDER, W_WANDER);
        if (x < BOIDS_EDGE) {                       /* turn back from the edges */
            vx += W_EDGE;
        } else if (x > BOIDS_W - BOIDS_EDGE) {
            vx -= W_EDGE;
        }
        if (y < BOIDS_EDGE) {
            vy += W_EDGE;
        } else if (y > BOIDS_W - BOIDS_EDGE) {
            vy -= W_EDGE;
        }
        float v = sqrtf(vx * vx + vy * vy);
        if (v < 1e-4f) {
            vx = BOIDS_SPEED_MIN;
            vy = 0;
        } else if (v > BOIDS_SPEED_MAX || v < BOIDS_SPEED_MIN) {
            float k = (v > BOIDS_SPEED_MAX ? BOIDS_SPEED_MAX : BOIDS_SPEED_MIN) / v;
            vx *= k;
            vy *= k;
        }
        nvx[i] = vx;
        nvy[i] = vy;
    }
    for (int i = 0; i < BOIDS_N; i++) {             /* all moved at once, from the same picture */
        boid_t *b = &s_b[i];
        b->vx = nvx[i];
        b->vy = nvy[i];
        memmove(b->tx + 1, b->tx, (BOIDS_TRAIL - 1) * sizeof(float));
        memmove(b->ty + 1, b->ty, (BOIDS_TRAIL - 1) * sizeof(float));
        b->tx[0] = fminf(fmaxf(b->tx[1] + b->vx, 0.0f), BOIDS_W - 1.0f);
        b->ty[0] = fminf(fmaxf(b->ty[1] + b->vy, 0.0f), BOIDS_W - 1.0f);
    }
}

/* (documented in boids_core.h) */
void boids_at(int i, int age, float *x, float *y)
{
    *x = s_b ? s_b[i].tx[age] : 0;
    *y = s_b ? s_b[i].ty[age] : 0;
}

/* (documented in boids_core.h) */
void boids_vel(int i, float *vx, float *vy)
{
    *vx = s_b ? s_b[i].vx : 0;
    *vy = s_b ? s_b[i].vy : 0;
}

/**
 * @file boids_core.c
 * @ingroup boids_core
 * @brief A flock of boids, their colours and the hawks chasing them, for the screen saver, without LVGL
 *        (drawn by ui_saver_boids.c).
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
#define W_FLEE          0.9f                  /**< how hard a bird flees a hawk (at the hawk; less further) */
#define W_CHASE         0.18f                 /**< how hard a hawk turns to its bird */
#define ADOPT_ONE_IN    40                    /**< a step, a bird takes a neighbour's colour one time in this */
#define MUTATE_ONE_IN   30000                 /**< a step, a bird takes up another colour one time in this */

/** @brief A bird or hawk. */
typedef struct {
    float vx, vy;                         /**< its velocity, px a step */
    float tx[BOIDS_TRAIL], ty[BOIDS_TRAIL];   /**< where it was, the newest first */
    uint8_t colour, before;               /**< a bird's colour, and the one before */
    uint16_t since;                       /**< steps since its colour changed (stops at 65535) */
} boid_t;

static boid_t *s_b;                       /**< the birds, then the hawks (NULL: none) */
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
 * @brief A bird's colour changes.
 * @param[in,out] b  the bird
 * @param[in] c  its new colour
 */
static void recolour(boid_t *b, int c)
{
    if (c != b->colour) {
        b->before = b->colour;
        b->colour = (uint8_t)c;
        b->since = 0;
    }
}

/* (documented in boids_core.h) */
bool boids_start(uint32_t seed)
{
    if (!s_b) {
        s_b = malloc(BOIDS_ALL * sizeof(*s_b));
        if (!s_b) {
            return false;
        }
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < BOIDS_ALL; i++) {
        boid_t *b = &s_b[i];
        float x = rnd_f(BOIDS_EDGE, BOIDS_W - BOIDS_EDGE), y = rnd_f(BOIDS_EDGE, BOIDS_W - BOIDS_EDGE);
        float a = rnd_f(0, 6.2831853f), v = i < BOIDS_N ? rnd_f(BOIDS_SPEED_MIN, BOIDS_SPEED_MAX) : BOIDS_SPEED_HAWK;
        b->vx = cosf(a) * v;
        b->vy = sinf(a) * v;
        for (int k = 0; k < BOIDS_TRAIL; k++) {
            b->tx[k] = x;
            b->ty[k] = y;
        }
        b->colour = b->before = (uint8_t)(rnd() % BOIDS_COLOURS);
        b->since = 65535;
    }
    return true;
}

/* (documented in boids_core.h) */
void boids_free(void)
{
    free(s_b);
    s_b = NULL;
}

/**
 * @brief Turns a velocity back from the field's edges.
 * @param[in] x  where, px
 * @param[in] y  px
 * @param[in,out] vx  the velocity's x
 * @param[in,out] vy  its y
 */
static void edges(float x, float y, float *vx, float *vy)
{
    if (x < BOIDS_EDGE) {
        *vx += W_EDGE;
    } else if (x > BOIDS_W - BOIDS_EDGE) {
        *vx -= W_EDGE;
    }
    if (y < BOIDS_EDGE) {
        *vy += W_EDGE;
    } else if (y > BOIDS_W - BOIDS_EDGE) {
        *vy -= W_EDGE;
    }
}

/**
 * @brief Keeps a speed within limits.
 * @param[in,out] vx  the velocity's x
 * @param[in,out] vy  its y
 * @param[in] lo  at least, px a step
 * @param[in] hi  at most
 */
static void limit(float *vx, float *vy, float lo, float hi)
{
    float v = sqrtf(*vx * *vx + *vy * *vy);
    if (v < 1e-4f) {
        *vx = lo;
        *vy = 0;
    } else if (v > hi || v < lo) {
        float k = (v > hi ? hi : lo) / v;
        *vx *= k;
        *vy *= k;
    }
}

/**
 * @brief A bird's new velocity: the three rules, fleeing hawks, wander, edges, speed; and its colour.
 * @param[in] i  the bird
 * @param[out] nvx  gets the velocity's x
 * @param[out] nvy  gets its y
 */
static void bird(int i, float *nvx, float *nvy)
{
    boid_t *b = &s_b[i];
    float x = b->tx[0], y = b->ty[0];
    float sx = 0, sy = 0, ax = 0, ay = 0, cx = 0, cy = 0;
    int n = 0, pick = -1;
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
        if (rnd() % n == 0) {
            pick = j;                               /* (a neighbour at random, all equally likely) */
        }
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
    bool afraid = false;
    for (int h = BOIDS_N; h < BOIDS_ALL; h++) {     /* away from the hawks it sees */
        float dx = x - s_b[h].tx[0], dy = y - s_b[h].ty[0];
        float d = sqrtf(dx * dx + dy * dy);
        if (d < BOIDS_FEAR && d > 1e-3f) {
            vx += dx / d * W_FLEE * (1 - d / BOIDS_FEAR) * 3;
            vy += dy / d * W_FLEE * (1 - d / BOIDS_FEAR) * 3;
            afraid = true;
        }
    }
    vx += rnd_f(-W_WANDER, W_WANDER);
    vy += rnd_f(-W_WANDER, W_WANDER);
    edges(x, y, &vx, &vy);
    limit(&vx, &vy, BOIDS_SPEED_MIN, afraid ? BOIDS_SPEED_FLEE : BOIDS_SPEED_MAX);
    *nvx = vx;
    *nvy = vy;
    if (pick >= 0 && rnd() % ADOPT_ONE_IN == 0) {   /* a neighbour's colour */
        recolour(b, s_b[pick].colour);
    }
    if (rnd() % MUTATE_ONE_IN == 0) {               /* another colour, now and then */
        recolour(b, (b->colour + 1 + (int)(rnd() % (BOIDS_COLOURS - 1))) % BOIDS_COLOURS);
    }
    if (b->since < 65535) {
        b->since++;
    }
}

/**
 * @brief A hawk's new velocity: towards the bird nearest it, edges, its speed.
 * @param[in] h  the hawk
 * @param[out] nvx  gets the velocity's x
 * @param[out] nvy  gets its y
 */
static void hawk(int h, float *nvx, float *nvy)
{
    const boid_t *k = &s_b[h];
    float x = k->tx[0], y = k->ty[0], best = 1e12f, tx = 0, ty = 0;
    for (int j = 0; j < BOIDS_N; j++) {
        float dx = s_b[j].tx[0] - x, dy = s_b[j].ty[0] - y, d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            tx = dx;
            ty = dy;
        }
    }
    float d = sqrtf(best), vx = k->vx, vy = k->vy;
    if (d > 1e-3f) {
        vx += tx / d * BOIDS_SPEED_HAWK * W_CHASE;
        vy += ty / d * BOIDS_SPEED_HAWK * W_CHASE;
    }
    vx += rnd_f(-W_WANDER, W_WANDER);
    vy += rnd_f(-W_WANDER, W_WANDER);
    edges(x, y, &vx, &vy);
    limit(&vx, &vy, BOIDS_SPEED_HAWK, BOIDS_SPEED_HAWK);
    *nvx = vx;
    *nvy = vy;
}

/* (documented in boids_core.h) */
void boids_step(void)
{
    if (!s_b) {
        return;
    }
    static float nvx[BOIDS_ALL], nvy[BOIDS_ALL];
    for (int i = 0; i < BOIDS_ALL; i++) {
        if (i < BOIDS_N) {
            bird(i, &nvx[i], &nvy[i]);
        } else {
            hawk(i, &nvx[i], &nvy[i]);
        }
    }
    for (int i = 0; i < BOIDS_ALL; i++) {           /* all moved at once, from the same picture */
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

/* (documented in boids_core.h) */
int boids_colour(int i, int *before, int *since)
{
    if (before) {
        *before = s_b ? s_b[i].before : 0;
    }
    if (since) {
        *since = s_b ? s_b[i].since : 0;
    }
    return s_b ? s_b[i].colour : 0;
}

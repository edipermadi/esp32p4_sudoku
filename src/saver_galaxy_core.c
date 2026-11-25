/**
 * @file saver_galaxy_core.c
 * @ingroup saver_galaxy_core
 * @brief Two galaxies colliding, for the screen saver, without LVGL (drawn by ui_saver_galaxy.c).
 */
#include "saver_galaxy_core.h"
#include <math.h>
#include <stdlib.h>

#define HALF            (GALAXY_W * 0.5f)     /**< the view's middle, px */
#define SOFT_STAR       0.15f                 /**< a core's pull on a star, softened within this */
#define SOFT_CORE       0.30f                 /**< the cores' pull on each other, softened within this */
#define R_IN            0.20f                 /**< a disk's stars from this ... */
#define R_OUT           1.00f                 /**< ... out to this */
#define START_APART     5.0f                  /**< the cores this far apart at the start */
#define FRICTION        0.20f                 /**< the cores' slowing (a share of their speed apart, a unit of time) ... */
#define FRICTION_NEAR   3.0f                  /**< ... nearer than this, growing as they close */
#define SUBSTEPS        2                     /**< leapfrog steps a frame */
#define DT              0.006f                /**< a step, units of time */
#define TILT            0.95f                 /**< the view: looking down at the orbit's plane from this far round, radians */
#define TURN            0.0012f               /**< the view's turn a frame, radians */
#define PI_F            3.1415927f            /**< pi */

/** @brief A star: where it is, how it moves (3D), its colour. */
typedef struct {
    float x, y, z;                        /**< position */
    float vx, vy, vz;                     /**< velocity */
    uint8_t tone;                         /**< its colour (galaxy 0: 0..2, galaxy 1: 3..5, inner to outer) */
} star_t;

static star_t *s_star;                    /**< the stars (NULL: none) */
static float s_core[2][3];                /**< the cores' positions */
static float s_corev[2][3];               /**< and velocities */
static float s_t;                         /**< the time since the start */
static float s_turn;                      /**< the view's turn */
static uint32_t s_rng = 1;                /**< xorshift32 state (never 0) */

/**
 * @brief A random number from 0 up to 1 (xorshift32).
 * @return it
 */
static float frnd(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return (x >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief A galaxy's disk: its stars on circular orbits round its core (as fast as the softened pull asks), in a
 *        plane tilted from the cores' orbit.
 * @param[in] g  the galaxy, 0 or 1
 * @param[in] incl  the disk's tilt from the orbit's plane, radians
 * @param[in] node  which way it's tilted, radians
 * @param[in] spin  1: round the same way as the cores' orbit, -1: against it
 */
static void build_disk(int g, float incl, float node, float spin)
{
    /* the disk's two directions: the orbit plane's x and y, tilted by incl about the line at node */
    const float cn = cosf(node), sn = sinf(node), ci = cosf(incl), si = sinf(incl);
    const float e1[3] = {cn, sn, 0}, e2[3] = {-sn * ci, cn * ci, si};
    for (int k = 0; k < GALAXY_PER; k++) {
        star_t *s = &s_star[g * GALAXY_PER + k];
        const float r = R_IN + (R_OUT - R_IN) * frnd(), a = 2.0f * PI_F * frnd();
        const float ca = cosf(a), sa = sinf(a);
        const float r2 = r * r + SOFT_STAR * SOFT_STAR, v = sqrtf(r * r / (r2 * sqrtf(r2)));
        s->x = s_core[g][0] + r * (ca * e1[0] + sa * e2[0]);
        s->y = s_core[g][1] + r * (ca * e1[1] + sa * e2[1]);
        s->z = s_core[g][2] + r * (ca * e1[2] + sa * e2[2]);
        s->vx = s_corev[g][0] + spin * v * (-sa * e1[0] + ca * e2[0]);
        s->vy = s_corev[g][1] + spin * v * (-sa * e1[1] + ca * e2[1]);
        s->vz = s_corev[g][2] + spin * v * (-sa * e1[2] + ca * e2[2]);
        s->tone = (uint8_t)(g * 3 + (r < 0.45f ? 0 : r < 0.75f ? 1 : 2));
    }
}

/* (documented in saver_galaxy_core.h) */
bool galaxy_start(uint32_t seed)
{
    if (!s_star) {
        s_star = malloc(GALAXY_STARS * sizeof(star_t));
    }
    if (!s_star) {
        return false;
    }
    s_rng = (seed * 0x9E3779B1u) | 1u;              /* (scrambled: small seeds start xorshift with tiny numbers) */
    for (int k = 0; k < 4; k++) {
        frnd();
    }
    /* the cores: on a parabola (total mass 2) coming closest at q, START_APART apart now, still coming in */
    const float q = 0.8f + 0.5f * frnd(), p = 2.0f * q;
    const float f = -acosf(2.0f * q / START_APART - 1.0f);
    const float vr = sqrtf(2.0f / p) * sinf(f), vt = sqrtf(2.0f / p) * (1.0f + cosf(f));
    const float rel[3] = {START_APART * cosf(f), START_APART * sinf(f), 0};
    const float relv[3] = {vr * cosf(f) - vt * sinf(f), vr * sinf(f) + vt * cosf(f), 0};
    for (int c = 0; c < 3; c++) {
        s_core[0][c] = -0.5f * rel[c];
        s_core[1][c] = 0.5f * rel[c];
        s_corev[0][c] = -0.5f * relv[c];
        s_corev[1][c] = 0.5f * relv[c];
    }
    /* galaxy 0 near the orbit's plane, turning with the orbit (long tails); galaxy 1 any tilt, mostly with it */
    build_disk(0, 0.6f * frnd(), 2.0f * PI_F * frnd(), 1.0f);
    build_disk(1, 1.6f * frnd(), 2.0f * PI_F * frnd(), frnd() < 0.7f ? 1.0f : -1.0f);
    s_t = 0;
    s_turn = 2.0f * PI_F * frnd();
    return true;
}

/* (documented in saver_galaxy_core.h) */
void galaxy_free(void)
{
    free(s_star);
    s_star = NULL;
}

/**
 * @brief A core's pull at a point (softened gravity, mass 1).
 * @param[in] p  the point
 * @param[in] c  the core
 * @param[in] soft  the softening
 * @param[in,out] a  the pull added to it
 */
static void pull(const float p[3], const float c[3], float soft, float a[3])
{
    const float dx = c[0] - p[0], dy = c[1] - p[1], dz = c[2] - p[2];
    const float r2 = dx * dx + dy * dy + dz * dz + soft * soft, k = 1.0f / (r2 * sqrtf(r2));
    a[0] += dx * k;
    a[1] += dy * k;
    a[2] += dz * k;
}

/** @brief One leapfrog step (drift half, kick, drift half): the cores, then the stars. */
static void leapfrog(void)
{
    const float h = 0.5f * DT;
    for (int g = 0; g < 2; g++) {
        for (int c = 0; c < 3; c++) {
            s_core[g][c] += h * s_corev[g][c];
        }
    }
    /* the cores: each other's pull, and friction on their speed apart when they're close */
    float a[3] = {0, 0, 0};
    pull(s_core[0], s_core[1], SOFT_CORE, a);
    const float dx = s_core[1][0] - s_core[0][0], dy = s_core[1][1] - s_core[0][1], dz = s_core[1][2] - s_core[0][2];
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    const float fr = d < FRICTION_NEAR ? FRICTION * (1.0f - d / FRICTION_NEAR) : 0;
    for (int c = 0; c < 3; c++) {
        const float rv = s_corev[1][c] - s_corev[0][c];
        s_corev[0][c] += DT * (a[c] + 0.5f * fr * rv);
        s_corev[1][c] += DT * (-a[c] - 0.5f * fr * rv);
        s_core[0][c] += h * s_corev[0][c];
        s_core[1][c] += h * s_corev[1][c];
    }
    /* the stars: drifted to mid-step, pulled by both cores there (the cores at mid-step too), drifted on */
    float mid[2][3];
    for (int g = 0; g < 2; g++) {
        for (int c = 0; c < 3; c++) {
            mid[g][c] = s_core[g][c] - h * s_corev[g][c];
        }
    }
    for (int i = 0; i < GALAXY_STARS; i++) {
        star_t *s = &s_star[i];
        float p[3] = {s->x + h * s->vx, s->y + h * s->vy, s->z + h * s->vz}, f[3] = {0, 0, 0};
        pull(p, mid[0], SOFT_STAR, f);
        pull(p, mid[1], SOFT_STAR, f);
        s->vx += DT * f[0];
        s->vy += DT * f[1];
        s->vz += DT * f[2];
        s->x = p[0] + h * s->vx;
        s->y = p[1] + h * s->vy;
        s->z = p[2] + h * s->vz;
    }
}

/* (documented in saver_galaxy_core.h) */
void galaxy_step(void)
{
    if (!s_star) {
        return;
    }
    for (int k = 0; k < SUBSTEPS; k++) {
        leapfrog();
    }
    s_t += SUBSTEPS * DT;
    s_turn += TURN;
}

/* (documented in saver_galaxy_core.h) */
bool is_galaxy_done(void)
{
    return s_t >= GALAXY_END;
}

/**
 * @brief A point as it shows: turned with the view about the orbit's axis, then seen from TILT above the plane.
 * @param[in] p  the point
 * @param[out] x  gets where, px
 * @param[out] y  gets where, px
 * @return false if it's off the view
 */
static bool project(const float p[3], int *x, int *y)
{
    const float ct = cosf(s_turn), st = sinf(s_turn);
    const float u = p[0] * ct - p[1] * st, w = p[0] * st + p[1] * ct;
    const float v = w * cosf(TILT) + p[2] * sinf(TILT);
    *x = (int)(HALF + u * GALAXY_SCALE);
    *y = (int)(HALF - v * GALAXY_SCALE);
    return *x >= 0 && *x < GALAXY_W - 1 && *y >= 0 && *y < GALAXY_W - 1;
}

/* (documented in saver_galaxy_core.h) */
bool galaxy_get(int i, int *x, int *y, int *tone)
{
    if (!s_star || i < 0 || i >= GALAXY_STARS) {
        return false;
    }
    const star_t *s = &s_star[i];
    const float p[3] = {s->x, s->y, s->z};
    *tone = s->tone;
    return project(p, x, y);
}

/* (documented in saver_galaxy_core.h) */
bool galaxy_core(int k, int *x, int *y)
{
    return s_star && k >= 0 && k < 2 && project(s_core[k], x, y);
}

/* (documented in saver_galaxy_core.h) */
float galaxy_time(void)
{
    return s_t;
}

/* (documented in saver_galaxy_core.h) */
float galaxy_separation(void)
{
    const float dx = s_core[1][0] - s_core[0][0], dy = s_core[1][1] - s_core[0][1], dz = s_core[1][2] - s_core[0][2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* (documented in saver_galaxy_core.h) */
float galaxy_radius(int i)
{
    const star_t *s = &s_star[i];
    const float *c = s_core[i / GALAXY_PER];
    const float dx = s->x - c[0], dy = s->y - c[1], dz = s->z - c[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

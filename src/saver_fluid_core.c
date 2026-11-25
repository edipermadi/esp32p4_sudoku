/**
 * @file saver_fluid_core.c
 * @ingroup saver_fluid_core
 * @brief Coloured liquids stirred and mixing, for the screen saver, without LVGL (drawn by ui_saver_fluid.c).
 */
#include "saver_fluid_core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define N               FLUID_N
#define S               (FLUID_N + 2)         /**< a row, the walls' cells included */
#define IX(i, j)        ((i) + S * (j))       /**< a cell's place: i across, j down, 1..N inside */
#define CELLS           (S * S)               /**< cells, the walls' included */
#define SPOON_R         7                     /**< a spoon's reach, cells */
#define SPOON_GRIP      0.35f                 /**< how much of the spoon's speed the water near it takes a step */
#define POUR            0.70f                 /**< dye poured a step, at the spoon's middle */
#define SWIRL           0.10f                 /**< vorticity confinement's strength */
#define TWO_PI          6.2831853f            /**< a turn, radians */

/** @brief A spoon: where it goes (a Lissajous path), and its pouring's rhythm. */
typedef struct {
    float cx, cy, ax, ay;                 /**< its path's middle and reach, cells */
    float fx, fy, px, py;                 /**< its path's rates (radians a step) and phases */
    float pour_f, pour_p;                 /**< its pouring's rate and phase */
} spoon_t;

static float *s_mem;                      /**< all the fields, in one block (NULL: none) */
static float *s_u, *s_v, *s_u0, *s_v0;    /**< the flow, across and down (cells a step); the step's copies */
static float *s_p, *s_div;                /**< the pressure, the flow's divergence (also the curl) */
static float *s_dye[FLUID_SPOONS];        /**< each dye's amount */
static float *s_tmp;                      /**< a dye's copy while it moves */
static float *s_fwd, *s_back;             /**< MacCormack's forward and round-trip guesses */
static uint32_t *s_col[2];                /**< the cells' colours, 0xRRGGBB, N x N: drawn, next */
static int s_front;                       /**< which of s_col is drawn */
static volatile int s_fresh;              /**< the drawn ones haven't been drawn yet */
static uint32_t s_colour[FLUID_SPOONS];   /**< each dye's colour */
static spoon_t s_spoon[FLUID_SPOONS];     /**< the spoons */
static uint32_t s_t;                      /**< steps since the start */
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

/* (documented in saver_fluid_core.h) */
bool fluid_start(uint32_t seed, const uint32_t colours[FLUID_SPOONS])
{
    const size_t fields = 10 + FLUID_SPOONS;
    if (!s_mem) {
        s_mem = malloc(fields * CELLS * sizeof(float));
        s_col[0] = malloc(2 * (size_t)N * N * sizeof(uint32_t));
        s_col[1] = s_col[0] ? s_col[0] + N * N : NULL;
    }
    if (!s_mem || !s_col[0]) {
        fluid_free();
        return false;
    }
    memset(s_mem, 0, fields * CELLS * sizeof(float));
    memset(s_col[0], 0, 2 * (size_t)N * N * sizeof(uint32_t));
    float *f = s_mem;
    s_u = f;
    s_v = f + CELLS;
    s_u0 = f + 2 * CELLS;
    s_v0 = f + 3 * CELLS;
    s_p = f + 4 * CELLS;
    s_div = f + 5 * CELLS;
    s_tmp = f + 6 * CELLS;
    s_fwd = f + 7 * CELLS;
    s_back = f + 8 * CELLS;
    for (int k = 0; k < FLUID_SPOONS; k++) {
        s_dye[k] = f + (9 + k) * CELLS;
        s_colour[k] = colours[k];
    }
    s_rng = seed ? seed : 1;
    const float tilt = frnd() - 0.5f;               /* full, in FLUID_SPOONS bands of colour, a little slanted */
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            const float at = ((float)i + tilt * (float)(j - N / 2)) * FLUID_SPOONS / N;   /* (in bands) */
            const float band = (float)N / FLUID_SPOONS;   /* (a band's width, cells) */
            float t = 0;
            for (int k = 0; k < FLUID_SPOONS; k++) {   /* (soft edges, a cell wide: no stairs) */
                const float lo = k ? (at - (float)k) * band + 0.5f : 1.0f;
                const float hi = k < FLUID_SPOONS - 1 ? ((float)(k + 1) - at) * band + 0.5f : 1.0f;
                float w = lo < hi ? lo : hi;
                w = w < 0 ? 0 : w > 1 ? 1 : w;
                s_dye[k][IX(i, j)] = w;
                t += w;
            }
            for (int k = 0; t > 0 && k < FLUID_SPOONS; k++) {
                s_dye[k][IX(i, j)] /= t;
            }
        }
    }
    for (int k = 0; k < FLUID_SPOONS; k++) {    /* paths round the tank, each its own rates */
        spoon_t *sp = &s_spoon[k];
        sp->cx = N * (0.35f + 0.3f * frnd());
        sp->cy = N * (0.35f + 0.3f * frnd());
        sp->ax = N * (0.18f + 0.12f * frnd());
        sp->ay = N * (0.18f + 0.12f * frnd());
        sp->fx = 0.014f + 0.016f * frnd();
        sp->fy = 0.014f + 0.016f * frnd();
        sp->px = TWO_PI * frnd();
        sp->py = TWO_PI * frnd();
        sp->pour_f = 0.006f + 0.006f * frnd();
        sp->pour_p = TWO_PI * frnd();
    }
    s_t = 0;
    s_front = 0;
    s_fresh = 0;
    return true;
}

/* (documented in saver_fluid_core.h) */
void fluid_free(void)
{
    free(s_mem);
    s_mem = NULL;
    free(s_col[0]);
    s_col[0] = s_col[1] = NULL;
}

/**
 * @brief The walls: nothing flows through them (the flow across a wall mirrored), the rest copied out to them.
 * @param[in] b  1: the flow across (u), 2: down (v), 0: anything else
 * @param[in,out] x  the field
 */
static void walls(int b, float *x)
{
    for (int i = 1; i <= N; i++) {
        x[IX(0, i)] = b == 1 ? -x[IX(1, i)] : x[IX(1, i)];
        x[IX(N + 1, i)] = b == 1 ? -x[IX(N, i)] : x[IX(N, i)];
        x[IX(i, 0)] = b == 2 ? -x[IX(i, 1)] : x[IX(i, 1)];
        x[IX(i, N + 1)] = b == 2 ? -x[IX(i, N)] : x[IX(i, N)];
    }
    x[IX(0, 0)] = 0.5f * (x[IX(1, 0)] + x[IX(0, 1)]);
    x[IX(0, N + 1)] = 0.5f * (x[IX(1, N + 1)] + x[IX(0, N)]);
    x[IX(N + 1, 0)] = 0.5f * (x[IX(N, 0)] + x[IX(N + 1, 1)]);
    x[IX(N + 1, N + 1)] = 0.5f * (x[IX(N, N + 1)] + x[IX(N + 1, N)]);
}

/**
 * @brief Moves a field along the flow: each cell takes what was where its water was a step ago (bilinear).
 * @param[in] b  the walls' kind (walls())
 * @param[out] d  gets the field moved
 * @param[in] d0  the field
 * @param[in] u  the flow across
 * @param[in] v  the flow down
 */
static void advect(int b, float *d, const float *d0, const float *u, const float *v)
{
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            float x = (float)i - u[IX(i, j)], y = (float)j - v[IX(i, j)];
            x = x < 0.5f ? 0.5f : x > N + 0.5f ? N + 0.5f : x;
            y = y < 0.5f ? 0.5f : y > N + 0.5f ? N + 0.5f : y;
            const int i0 = (int)x, j0 = (int)y;
            const float s1 = x - (float)i0, t1 = y - (float)j0, s0 = 1 - s1, t0 = 1 - t1;
            d[IX(i, j)] = s0 * (t0 * d0[IX(i0, j0)] + t1 * d0[IX(i0, j0 + 1)]) +
                          s1 * (t0 * d0[IX(i0 + 1, j0)] + t1 * d0[IX(i0 + 1, j0 + 1)]);
        }
    }
    walls(b, d);
}

/**
 * @brief Moves a dye along the flow, keeping its edges sharp (MacCormack): moved forward, the result moved back,
 *        half the round trip's error put right; where that overshoots the cells it came from, the plain forward
 *        move instead (clamping there leaves streaks).
 * @param[in,out] d  the dye
 */
static void advect_sharp(float *d)
{
    memcpy(s_tmp, d, CELLS * sizeof(float));
    advect(0, s_fwd, s_tmp, s_u, s_v);
    advect(0, s_back, s_fwd, s_u0, s_v0);           /* (s_u0, s_v0: the flow reversed, fluid_step()) */
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            float x = (float)i - s_u[IX(i, j)], y = (float)j - s_v[IX(i, j)];
            x = x < 0.5f ? 0.5f : x > N + 0.5f ? N + 0.5f : x;
            y = y < 0.5f ? 0.5f : y > N + 0.5f ? N + 0.5f : y;
            const int i0 = (int)x, j0 = (int)y;
            const float a = s_tmp[IX(i0, j0)], b = s_tmp[IX(i0 + 1, j0)], c = s_tmp[IX(i0, j0 + 1)];
            const float e = s_tmp[IX(i0 + 1, j0 + 1)];
            const float lo = fminf(fminf(a, b), fminf(c, e)), hi = fmaxf(fmaxf(a, b), fmaxf(c, e));
            const float v = s_fwd[IX(i, j)] + 0.5f * (s_tmp[IX(i, j)] - s_back[IX(i, j)]);
            d[IX(i, j)] = v < lo || v > hi ? s_fwd[IX(i, j)] : v;   /* (overshot: the plain way, no streaks) */
        }
    }
    walls(0, d);
}

/** @brief Makes the flow incompressible: the pressure that its divergence asks for, and its slope taken off. */
static void project(void)
{
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            s_div[IX(i, j)] = -0.5f * (s_u[IX(i + 1, j)] - s_u[IX(i - 1, j)] + s_v[IX(i, j + 1)] - s_v[IX(i, j - 1)]);
        }
    }
    walls(0, s_div);
    walls(0, s_p);                                  /* (last step's pressure: a good start) */
    for (int k = 0; k < FLUID_PRESSURE_PASSES; k++) {
        for (int j = 1; j <= N; j++) {
            for (int i = 1; i <= N; i++) {
                s_p[IX(i, j)] = (s_div[IX(i, j)] + s_p[IX(i - 1, j)] + s_p[IX(i + 1, j)] + s_p[IX(i, j - 1)] +
                                 s_p[IX(i, j + 1)]) * 0.25f;
            }
        }
        walls(0, s_p);
    }
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            s_u[IX(i, j)] -= 0.5f * (s_p[IX(i + 1, j)] - s_p[IX(i - 1, j)]);
            s_v[IX(i, j)] -= 0.5f * (s_p[IX(i, j + 1)] - s_p[IX(i, j - 1)]);
        }
    }
    walls(1, s_u);
    walls(2, s_v);
}

/** @brief Vorticity confinement: a push round each eddy, from where it turns less towards where it turns most. */
static void swirl(void)
{
    float *w = s_div;                               /* (the curl, in the divergence's room for now) */
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            w[IX(i, j)] = 0.5f * (s_v[IX(i + 1, j)] - s_v[IX(i - 1, j)] - s_u[IX(i, j + 1)] + s_u[IX(i, j - 1)]);
        }
    }
    walls(0, w);
    for (int j = 2; j < N; j++) {
        for (int i = 2; i < N; i++) {
            const float gx = 0.5f * (fabsf(w[IX(i + 1, j)]) - fabsf(w[IX(i - 1, j)]));
            const float gy = 0.5f * (fabsf(w[IX(i, j + 1)]) - fabsf(w[IX(i, j - 1)]));
            const float len = sqrtf(gx * gx + gy * gy) + 1e-5f, c = w[IX(i, j)];
            s_u[IX(i, j)] += SWIRL * (gy / len) * c;
            s_v[IX(i, j)] -= SWIRL * (gx / len) * c;
        }
    }
}

/**
 * @brief Where a spoon is at a step.
 * @param[in] sp  the spoon
 * @param[in] t  the step
 * @param[out] x  gets across, cells
 * @param[out] y  gets down, cells
 */
static void spoon_at(const spoon_t *sp, float t, float *x, float *y)
{
    *x = sp->cx + sp->ax * sinf(sp->fx * t + sp->px);
    *y = sp->cy + sp->ay * sinf(sp->fy * t + sp->py);
}

/** @brief The spoons: the water near each pulled along with it, its dye poured in pulses. */
static void stir(void)
{
    for (int k = 0; k < FLUID_SPOONS; k++) {
        const spoon_t *sp = &s_spoon[k];
        float x, y, x0, y0;
        spoon_at(sp, (float)s_t, &x, &y);
        spoon_at(sp, (float)s_t - 1.0f, &x0, &y0);
        const float vx = x - x0, vy = y - y0;
        float pour = sinf(sp->pour_f * (float)s_t + sp->pour_p);
        pour = pour > -0.2f ? POUR * (pour + 0.2f) / 1.2f : 0;   /* (on a while, off a short while) */
        const int ci = (int)x, cj = (int)y;
        for (int j = cj - SPOON_R; j <= cj + SPOON_R; j++) {
            for (int i = ci - SPOON_R; i <= ci + SPOON_R; i++) {
                if (i < 1 || i > N || j < 1 || j > N) {
                    continue;
                }
                const float dx = (float)i - x, dy = (float)j - y, r2 = dx * dx + dy * dy;
                if (r2 > SPOON_R * SPOON_R) {
                    continue;
                }
                const float g = expf(-r2 * (6.0f / (SPOON_R * SPOON_R)));
                s_u[IX(i, j)] += g * SPOON_GRIP * (vx - s_u[IX(i, j)]);
                s_v[IX(i, j)] += g * SPOON_GRIP * (vy - s_v[IX(i, j)]);
                s_dye[k][IX(i, j)] += g * pour;
            }
        }
    }
}

/* (documented in saver_fluid_core.h) */
void fluid_step(void)
{
    if (!s_mem) {
        return;
    }
    stir();
    swirl();
    project();
    memcpy(s_u0, s_u, CELLS * sizeof(float));
    memcpy(s_v0, s_v, CELLS * sizeof(float));
    advect(1, s_u, s_u0, s_u0, s_v0);
    advect(2, s_v, s_v0, s_u0, s_v0);
    project();
    for (int c = 0; c < CELLS; c++) {               /* (the flow reversed, for the dyes' way back) */
        s_u0[c] = -s_u[c];
        s_v0[c] = -s_v[c];
    }
    for (int k = 0; k < FLUID_SPOONS; k++) {
        advect_sharp(s_dye[k]);
    }
    for (int c = 0; c < CELLS; c++) {               /* at most a cell full: fresh dye pushes the others out */
        float t = 0;
        for (int k = 0; k < FLUID_SPOONS; k++) {
            s_dye[k][c] = s_dye[k][c] > 0 ? s_dye[k][c] : 0;
            t += s_dye[k][c];
        }
        if (t > 1.0f) {
            for (int k = 0; k < FLUID_SPOONS; k++) {
                s_dye[k][c] /= t;
            }
        }
    }
    s_t++;
}

/* (documented in saver_fluid_core.h) */
bool fluid_publish(void)
{
    if (!s_mem || __atomic_load_n(&s_fresh, __ATOMIC_ACQUIRE)) {
        return false;                               /* (the last ones aren't drawn yet) */
    }
    uint32_t *col = s_col[1 - s_front];
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            float t = 0, r = 0, g = 0, b = 0;
            for (int k = 0; k < FLUID_SPOONS; k++) {
                const float a = s_dye[k][IX(i, j)];
                t += a;
                r += a * (float)(s_colour[k] >> 16 & 0xFF);
                g += a * (float)(s_colour[k] >> 8 & 0xFF);
                b += a * (float)(s_colour[k] & 0xFF);
            }
            uint32_t c = 0;
            if (t > 1e-4f) {                        /* the dyes' colours averaged, as bright as there's dye */
                const float s = (t > 1.0f ? 1.0f : t) / t;
                c = (uint32_t)(r * s) << 16 | (uint32_t)(g * s) << 8 | (uint32_t)(b * s);
            }
            col[(j - 1) * N + i - 1] = c;
        }
    }
    s_front = 1 - s_front;
    __atomic_store_n(&s_fresh, 1, __ATOMIC_RELEASE);
    return true;
}

/* (documented in saver_fluid_core.h) */
bool is_fluid_fresh(void)
{
    return __atomic_load_n(&s_fresh, __ATOMIC_ACQUIRE) != 0;
}

/* (documented in saver_fluid_core.h) */
bool fluid_frame(uint16_t *out, int bright)
{
    if (!s_mem || !is_fluid_fresh()) {
        return false;
    }
    static int16_t x0[FLUID_DRAWN];                 /* each drawn column's cell, and how far to the next (0..256) */
    static uint16_t xf[FLUID_DRAWN];
    static uint16_t line[FLUID_SCREEN];
    static int32_t col[N][3];                       /* a drawn row, between two rows of cells */
    for (int x = 0; x < FLUID_DRAWN; x++) {
        float g = ((float)x + 0.5f) * N / FLUID_DRAWN - 0.5f;
        g = g < 0 ? 0 : g > N - 1 ? N - 1 : g;
        x0[x] = (int16_t)(g >= N - 1 ? N - 2 : (int)g);
        xf[x] = (uint16_t)((g - (float)x0[x]) * 256.0f);
    }
    const uint32_t *cells = s_col[s_front];
    for (int y = 0; y < FLUID_DRAWN; y++) {
        float g = ((float)y + 0.5f) * N / FLUID_DRAWN - 0.5f;
        g = g < 0 ? 0 : g > N - 1 ? N - 1 : g;
        const int j = g >= N - 1 ? N - 2 : (int)g, f = (int)((g - (float)j) * 256.0f);
        const uint32_t *a = cells + j * N, *b = a + N;
        for (int i = 0; i < N; i++) {               /* down first: between the two rows */
            for (int c = 0; c < 3; c++) {
                const int sh = 16 - 8 * c, p = (int)(a[i] >> sh & 0xFF), q = (int)(b[i] >> sh & 0xFF);
                col[i][c] = p * 256 + (q - p) * f;  /* (x256) */
            }
        }
        for (int x = 0; x < FLUID_DRAWN; x++) {     /* then across */
            const int i = x0[x], w = xf[x];
            int rgb[3];
            for (int c = 0; c < 3; c++) {
                rgb[c] = (((col[i][c] * 256 + (col[i + 1][c] - col[i][c]) * w) >> 16) * bright) >> 8;
            }
            const uint16_t px = (uint16_t)((rgb[0] >> 3) << 11 | (rgb[1] >> 2) << 5 | rgb[2] >> 3);
            line[2 * x] = line[2 * x + 1] = px;
        }
        memcpy(out + (2 * y) * FLUID_SCREEN, line, sizeof(line));
        memcpy(out + (2 * y + 1) * FLUID_SCREEN, line, sizeof(line));
    }
    __atomic_store_n(&s_fresh, 0, __ATOMIC_RELEASE);
    return true;
}

/* (documented in saver_fluid_core.h) */
float fluid_divergence(void)
{
    float most = 0;
    for (int j = 2; j < N; j++) {
        for (int i = 2; i < N; i++) {
            const float d = fabsf(0.5f * (s_u[IX(i + 1, j)] - s_u[IX(i - 1, j)] + s_v[IX(i, j + 1)] - s_v[IX(i, j - 1)]));
            most = d > most ? d : most;
        }
    }
    return most;
}

/* (documented in saver_fluid_core.h) */
float fluid_dye(int k)
{
    float sum = 0;
    for (int j = 1; j <= N; j++) {
        for (int i = 1; i <= N; i++) {
            sum += s_dye[k][IX(i, j)];
        }
    }
    return sum;
}

/* (documented in saver_fluid_core.h) */
float fluid_speed(void)
{
    float most = 0;
    for (int c = 0; c < CELLS; c++) {
        const float s = sqrtf(s_u[c] * s_u[c] + s_v[c] * s_v[c]);
        most = s > most ? s : most;
    }
    return most;
}

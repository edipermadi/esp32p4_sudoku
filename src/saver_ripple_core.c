/**
 * @file saver_ripple_core.c
 * @ingroup saver_ripple_core
 * @brief Raindrops rippling a pool, for the screen saver, without LVGL (drawn by ui_saver_ripple.c).
 */
#include "saver_ripple_core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W               RIPPLE_W
#define S               (RIPPLE_W + 2)        /**< a row, the pool's sides included (held at 0: waves bounce) */
#define IX(x, y)        ((x) + 1 + S * ((y) + 1))   /**< a cell's place, x and y 0..W-1 */
#define TILE            30                    /**< the floor's tiles, cells (60 px) */
#define JOINT           2                     /**< the joints between them, cells */
#define BEND            5                     /**< a slope bends the view 1/2^this of it, cells */
#define GLINT_FROM      120                   /**< slopes towards the light past this glint ... */
#define GLINT_FULL      900                   /**< ... fully white from this */
#define SHADE_FULL      2400                  /**< slopes away from it darken, a third at most from this */
#define PI_F            3.1415927f            /**< pi */

static int16_t *s_heights;                /**< the heights' memory, both fields (NULL: none) */
static int16_t *s_h[2];                   /**< the heights: now, a step ago (swapped each step, within s_heights) */
static uint16_t *s_floor;                 /**< the pool's floor, W x W RGB565 */
static uint16_t *s_pic[2];                /**< the pictures, W x W RGB565: drawn, next */
static int s_front;                       /**< which of s_pic is drawn */
static volatile int s_fresh;              /**< the drawn one hasn't been drawn yet */
static uint16_t s_glint;                  /**< the glints' colour, RGB565 */
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
 * @brief 0xRRGGBB as RGB565.
 * @param[in] c  the colour
 * @return it
 */
static uint16_t to565(uint32_t c)
{
    return (uint16_t)((c >> 19 & 0x1F) << 11 | (c >> 10 & 0x3F) << 5 | (c >> 3 & 0x1F));
}

/**
 * @brief A colour between two, RGB565.
 * @param[in] a  from
 * @param[in] b  to
 * @param[in] u  how far, 0 (a) .. 256 (b)
 * @return it
 */
static uint16_t mix565(uint16_t a, uint16_t b, int u)
{
    const int ar = a >> 11, ag = a >> 5 & 0x3F, ab = a & 0x1F;
    const int r = ar + (((b >> 11) - ar) * u >> 8), g = ag + (((b >> 5 & 0x3F) - ag) * u >> 8);
    const int bl = ab + (((b & 0x1F) - ab) * u >> 8);
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* (documented in saver_ripple_core.h) */
bool ripple_start(uint32_t seed, const uint32_t colours[RIPPLE_TONES])
{
    if (!s_heights) {
        s_heights = calloc(2 * (size_t)S * S, sizeof(int16_t));
        s_floor = malloc(3 * (size_t)W * W * sizeof(uint16_t));
        s_pic[0] = s_floor ? s_floor + W * W : NULL;
        s_pic[1] = s_floor ? s_floor + 2 * W * W : NULL;
    }
    if (!s_heights || !s_floor) {
        ripple_free();
        return false;
    }
    s_h[0] = s_heights;
    s_h[1] = s_heights + S * S;
    memset(s_heights, 0, 2 * (size_t)S * S * sizeof(int16_t));
    memset(s_pic[0], 0, 2 * (size_t)W * W * sizeof(uint16_t));
    s_rng = seed ? seed : 1;
    const uint16_t tile = to565(colours[0]), joint = to565(colours[1]);
    s_glint = to565(colours[2]);
    uint8_t shade[(W / TILE + 1) * (W / TILE + 1)];  /* (each tile a little lighter or darker) */
    for (size_t k = 0; k < sizeof(shade); k++) {
        shade[k] = (uint8_t)(rnd() % 48);
    }
    for (int y = 0; y < W; y++) {
        for (int x = 0; x < W; x++) {
            const bool j = x % TILE < JOINT || y % TILE < JOINT;
            const int k = (y / TILE) * (W / TILE + 1) + x / TILE;
            s_floor[y * W + x] = j ? joint : mix565(tile, 0, shade[k]);
        }
    }
    s_front = 0;
    s_fresh = 0;
    return true;
}

/* (documented in saver_ripple_core.h) */
void ripple_free(void)
{
    free(s_heights);                                /* (not s_h[0]: the steps swap it into the block's middle) */
    s_heights = s_h[0] = s_h[1] = NULL;
    free(s_floor);
    s_floor = s_pic[0] = s_pic[1] = NULL;
}

/* (documented in saver_ripple_core.h) */
void ripple_drop(int x, int y, int r, int depth)
{
    if (!s_h[0]) {
        return;
    }
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            const float d = sqrtf((float)(dx * dx + dy * dy));
            if (d > (float)r || x + dx < 1 || x + dx >= W - 1 || y + dy < 1 || y + dy >= W - 1) {
                continue;
            }
            const int v = s_h[0][IX(x + dx, y + dy)] - (int)((float)depth * 0.5f * (1.0f + cosf(PI_F * d / (float)r)));
            s_h[0][IX(x + dx, y + dy)] = (int16_t)(v < -32000 ? -32000 : v);
        }
    }
}

/* (documented in saver_ripple_core.h) */
void ripple_step(void)
{
    if (!s_h[0]) {
        return;
    }
    if (rnd() % RIPPLE_DROP_EVERY == 0) {           /* a raindrop: small, somewhere */
        ripple_drop((int)(rnd() % W), (int)(rnd() % W), 2 + (int)(rnd() % 3), 700 + (int)(rnd() % 900));
    }
    if (rnd() % RIPPLE_STONE_EVERY == 0) {          /* now and then a stone: a big splash */
        ripple_drop(W / 6 + (int)(rnd() % (W * 2 / 3)), W / 6 + (int)(rnd() % (W * 2 / 3)), 9, 6000);
    }
    ripple_waves();
}

/* (documented in saver_ripple_core.h) */
void ripple_waves(void)
{
    if (!s_h[0]) {
        return;
    }
    const int16_t *c = s_h[0];
    int16_t *p = s_h[1];                            /* (a step ago: becomes the next step) */
    for (int y = 0; y < W; y++) {
        for (int x = 0; x < W; x++) {
            const int i = IX(x, y);
            int n = ((c[i - 1] + c[i + 1] + c[i - S] + c[i + S]) >> 1) - p[i];
            n -= n >> RIPPLE_DAMP;
            p[i] = (int16_t)(n < -32000 ? -32000 : n > 32000 ? 32000 : n);
        }
    }
    s_h[1] = s_h[0];
    s_h[0] = p;
}

/* (documented in saver_ripple_core.h) */
bool ripple_publish(void)
{
    if (!s_h[0] || __atomic_load_n(&s_fresh, __ATOMIC_ACQUIRE)) {
        return false;                               /* (the last one isn't drawn yet) */
    }
    const int16_t *h = s_h[0];
    uint16_t *pic = s_pic[1 - s_front];
    for (int y = 0; y < W; y++) {
        for (int x = 0; x < W; x++) {
            const int i = IX(x, y), sx = h[i + 1] - h[i - 1], sy = h[i + S] - h[i - S];
            int fx = x + (sx >> BEND), fy = y + (sy >> BEND);   /* the floor, bent by the slope */
            fx = fx < 0 ? 0 : fx >= W ? W - 1 : fx;
            fy = fy < 0 ? 0 : fy >= W ? W - 1 : fy;
            uint16_t c = s_floor[fy * W + fx];
            const int light = -(sx + sy);           /* (the light from the top left) */
            if (light > GLINT_FROM) {
                const int u = (light - GLINT_FROM) * 256 / (GLINT_FULL - GLINT_FROM);
                c = mix565(c, s_glint, u > 256 ? 256 : u);
            } else if (light < 0) {
                const int u = -light * 85 / SHADE_FULL;
                c = mix565(c, 0, u > 85 ? 85 : u);
            }
            pic[y * W + x] = c;
        }
    }
    s_front = 1 - s_front;
    __atomic_store_n(&s_fresh, 1, __ATOMIC_RELEASE);
    return true;
}

/* (documented in saver_ripple_core.h) */
bool is_ripple_fresh(void)
{
    return __atomic_load_n(&s_fresh, __ATOMIC_ACQUIRE) != 0;
}

/* (documented in saver_ripple_core.h) */
bool ripple_frame(uint16_t *out)
{
    if (!s_h[0] || !is_ripple_fresh()) {
        return false;
    }
    static uint16_t line[RIPPLE_SCREEN];
    const uint16_t *pic = s_pic[s_front];
    for (int y = 0; y < W; y++) {
        const uint16_t *row = pic + y * W;
        for (int x = 0; x < W; x++) {
            line[2 * x] = line[2 * x + 1] = row[x];
        }
        memcpy(out + (2 * y) * RIPPLE_SCREEN, line, sizeof(line));
        memcpy(out + (2 * y + 1) * RIPPLE_SCREEN, line, sizeof(line));
    }
    __atomic_store_n(&s_fresh, 0, __ATOMIC_RELEASE);
    return true;
}

/* (documented in saver_ripple_core.h) */
int ripple_height(int x, int y)
{
    return s_h[0] ? s_h[0][IX(x, y)] : 0;
}

/* (documented in saver_ripple_core.h) */
double ripple_energy(void)
{
    double e = 0;
    for (int y = 0; s_h[0] && y < W; y++) {
        for (int x = 0; x < W; x++) {
            const double v = s_h[0][IX(x, y)];
            e += v * v;
        }
    }
    return e;
}

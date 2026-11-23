/**
 * @file life_core.c
 * @ingroup life_core
 * @brief Conway's Game of Life for the screen saver, without LVGL (drawn by ui_saver_life.c).
 */
#include "life_core.h"
#include <stdlib.h>
#include <string.h>

static uint8_t *s_cur;                    /**< the cells alive now (0/1), row by row; NULL: no board */
static uint8_t *s_next;                   /**< the next generation being worked out */
static uint8_t *s_glow;                   /**< each cell's life_glow_t */
static uint64_t s_hist[LIFE_PERIOD_MAX];  /**< the last generations' hashes (a ring) */
static int s_hist_n;                      /**< hashes in s_hist, up to LIFE_PERIOD_MAX */
static int s_hist_at;                     /**< where the next goes */
static uint32_t s_gen;                    /**< generations since the seed */
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
 * @brief Takes the board's memory if it isn't taken yet.
 * @return false if there was none
 */
static bool board_alloc(void)
{
    if (s_cur) {
        return true;
    }
    s_cur = calloc(LIFE_CELLS, 1);
    s_next = calloc(LIFE_CELLS, 1);
    s_glow = calloc(LIFE_CELLS, 1);
    if (!s_cur || !s_next || !s_glow) {
        life_free();
        return false;
    }
    return true;
}

/**
 * @brief The board's hash (FNV-1a over the cells, 8 at a time).
 * @return it
 */
static uint64_t board_hash(void)
{
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < LIFE_CELLS; i += 8) {
        uint8_t b = 0;
        for (int k = 0; k < 8; k++) {
            b = (uint8_t)(b << 1 | s_cur[i + k]);
        }
        h = (h ^ b) * 1099511628211ULL;
    }
    return h;
}

/** @brief Forgets the generations seen (after a seed or a clear). */
static void history_reset(void)
{
    s_hist_n = 0;
    s_hist_at = 0;
    s_gen = 0;
}

/**
 * @brief Whether the board is one of the last generations again (and keeps it in the history).
 * @return true if it is: the board has settled
 */
static bool history_repeats(void)
{
    uint64_t h = board_hash();
    for (int i = 0; i < s_hist_n; i++) {
        if (s_hist[i] == h) {
            return true;
        }
    }
    s_hist[s_hist_at] = h;
    s_hist_at = (s_hist_at + 1) % LIFE_PERIOD_MAX;
    if (s_hist_n < LIFE_PERIOD_MAX) {
        s_hist_n++;
    }
    return false;
}

/* (documented in life_core.h) */
void life_free(void)
{
    free(s_cur);
    free(s_next);
    free(s_glow);
    s_cur = s_next = s_glow = NULL;
}

/* (documented in life_core.h) */
bool life_clear(void)
{
    if (!board_alloc()) {
        return false;
    }
    memset(s_cur, 0, LIFE_CELLS);
    memset(s_glow, LIFE_GLOW_OFF, LIFE_CELLS);
    history_reset();
    return true;
}

/* (documented in life_core.h) */
bool life_seed(uint32_t seed)
{
    if (!board_alloc()) {
        return false;
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < LIFE_CELLS; i++) {
        uint8_t alive = rnd() % 100 < LIFE_DENSITY;
        if (alive) {
            s_glow[i] = LIFE_GLOW_BORN;
        } else if (s_cur[i]) {
            s_glow[i] = LIFE_GLOW_FADE4;            /* the old board fades out under the new one */
        }
        s_cur[i] = alive;
    }
    history_reset();
    history_repeats();                              /* (the seed is the first generation seen) */
    return true;
}

/* (documented in life_core.h) */
bool life_step(void)
{
    if (!s_cur) {
        return false;
    }
    for (int y = 0; y < LIFE_N; y++) {
        const uint8_t *up = s_cur + ((y + LIFE_N - 1) % LIFE_N) * LIFE_N;
        const uint8_t *row = s_cur + y * LIFE_N;
        const uint8_t *dn = s_cur + ((y + 1) % LIFE_N) * LIFE_N;
        uint8_t *out = s_next + y * LIFE_N;
        for (int x = 0; x < LIFE_N; x++) {
            int l = x ? x - 1 : LIFE_N - 1, r = x < LIFE_N - 1 ? x + 1 : 0;
            int n = up[l] + up[x] + up[r] + row[l] + row[r] + dn[l] + dn[x] + dn[r];
            out[x] = n == 3 || (n == 2 && row[x]);
        }
    }
    for (int i = 0; i < LIFE_CELLS; i++) {
        uint8_t was = s_cur[i], is = s_next[i];
        if (is) {
            s_glow[i] = was ? LIFE_GLOW_ALIVE : LIFE_GLOW_BORN;
        } else if (was) {
            s_glow[i] = LIFE_GLOW_FADE4;
        } else if (s_glow[i] > LIFE_GLOW_OFF) {
            s_glow[i]--;                            /* (a dead cell is at most FADE4: fades on down) */
        }
    }
    uint8_t *t = s_cur;
    s_cur = s_next;
    s_next = t;
    s_gen++;
    if (history_repeats() || s_gen >= LIFE_GEN_MAX) {
        life_seed(rnd());
        return true;
    }
    return false;
}

/* (documented in life_core.h) */
uint8_t life_glow(int x, int y)
{
    return s_glow ? s_glow[y * LIFE_N + x] : LIFE_GLOW_OFF;
}

/* (documented in life_core.h) */
bool is_life_alive(int x, int y)
{
    return s_cur && s_cur[y * LIFE_N + x];
}

/* (documented in life_core.h) */
void life_set(int x, int y, bool alive)
{
    if (!s_cur) {
        return;
    }
    int i = y * LIFE_N + x;
    if (alive && !s_cur[i]) {
        s_glow[i] = LIFE_GLOW_BORN;
    } else if (!alive && s_cur[i]) {
        s_glow[i] = LIFE_GLOW_FADE4;
    }
    s_cur[i] = alive;
}

/* (documented in life_core.h) */
int life_population(void)
{
    int n = 0;
    for (int i = 0; s_cur && i < LIFE_CELLS; i++) {
        n += s_cur[i];
    }
    return n;
}

/* (documented in life_core.h) */
uint32_t life_generation(void)
{
    return s_gen;
}

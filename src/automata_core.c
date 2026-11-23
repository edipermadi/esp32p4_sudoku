/**
 * @file automata_core.c
 * @ingroup automata_core
 * @brief Cellular automata (and Langton's ant) for the screen savers, without LVGL (drawn by
 *        ui_saver_automata.c).
 */
#include "automata_core.h"
#include <stdlib.h>
#include <string.h>

#define RANGE_MAX       3                     /**< a neighbourhood's range at most */
#define NB_MAX          ((2 * RANGE_MAX + 1) * (2 * RANGE_MAX + 1) - 1)   /**< neighbours at most */
#define FOREST_GROW     200                   /**< ground grows a tree one generation in this */
#define FOREST_STRIKE   40000                 /**< a tree is struck by lightning one generation in this */

/** @brief A rule. */
typedef struct {
    const char *name;                     /**< its name and notation */
    uint8_t kind;                         /**< auto_kind_t */
    uint8_t states;                       /**< its states */
    uint8_t range;                        /**< the neighbourhood's range (cyclic, excitable) */
    uint8_t threshold;                    /**< neighbours needed (cyclic, excitable) */
    uint16_t born;                        /**< bit n: born with n live neighbours (Generations; B3 is 0x0008) */
    uint16_t survive;                     /**< bit n: stays alive with n (Generations) */
    uint8_t density;                      /**< % of the cells alive at the start (0: every state at random) */
    uint8_t patch;                        /**< the start only in a square this many cells across (0: everywhere) */
    uint16_t ms;                          /**< a generation each, ms */
    uint32_t run;                         /**< generations (the ant: steps) before it starts again */
    bool settles;                         /**< starts again when the board repeats within AUTO_SETTLE */
    uint8_t quiet;                        /**< starts again when fewer cells than this are alive (state 1) */
    const char *turns;                    /**< Langton's ant: a letter a state, L or R */
    uint8_t steps;                        /**< Langton's ant: its steps a generation */
} rule_t;

static const rule_t RULES[AUTO_R_COUNT] = {
    [AUTO_R_LIFE] = {"Game of Life  B3/S23", AUTO_GENERATIONS, 2, 1, 0, 0x0008, 0x000C, 30, 0, 125, 3000, true, 0, NULL, 0},
    [AUTO_R_BRAIN] = {"Brian's Brain  B2/S/C3", AUTO_GENERATIONS, 3, 1, 0, 0x0004, 0x0000, 12, 0, 100, 5000, false, 12, NULL, 0},
    [AUTO_R_ANT] = {"Langton's ant  RL", AUTO_TURMITE, 2, 0, 0, 0, 0, 0, 0, 40, 60000, false, 0, "RL", 24},
    [AUTO_R_CYCLIC] = {"Cyclic spirals  R3/T5/C8", AUTO_CYCLIC, 8, 3, 5, 0, 0, 0, 0, 100, 900, false, 0, NULL, 0},
    [AUTO_R_RPS] = {"Rock, paper, scissors  R1/T3/C3", AUTO_CYCLIC, 3, 1, 3, 0, 0, 0, 0, 100, 900, false, 0, NULL, 0},
    [AUTO_R_EXCITABLE] = {"Excitable medium", AUTO_EXCITABLE, 10, 1, 1, 0, 0, 0, 0, 100, 900, false, 0, NULL, 0},
    [AUTO_R_DAY_NIGHT] = {"Day & Night  B3678/S34678", AUTO_GENERATIONS, 2, 1, 0, 0x01C8, 0x01D8, 50, 0, 100, 900, false, 0, NULL, 0},
    [AUTO_R_SEEDS] = {"Seeds  B2/S", AUTO_GENERATIONS, 2, 1, 0, 0x0004, 0x0000, 40, 8, 100, 900, false, 0, NULL, 0},
    [AUTO_R_MAZE] = {"Maze  B3/S12345", AUTO_GENERATIONS, 2, 1, 0, 0x0008, 0x003E, 35, 16, 100, 900, false, 0, NULL, 0},
    [AUTO_R_STAR_WARS] = {"Star Wars  S345/B2/C4", AUTO_GENERATIONS, 4, 1, 0, 0x0004, 0x0038, 30, 0, 100, 900, false, 0, NULL, 0},
    [AUTO_R_FOREST] = {"Forest fire", AUTO_FOREST, 3, 1, 0, 0, 0, 50, 0, 100, 900, false, 0, NULL, 0},
};

static uint8_t *s_cur;                    /**< each cell's state, row by row (NULL: no board) */
static uint8_t *s_next;                   /**< the next generation being worked out */
static uint8_t *s_key;                    /**< each cell's key for drawing */
static int s_rule;                        /**< the rule running */
static uint32_t s_gen;                    /**< generations (the ant: steps) since the run began */
static int8_t s_nb[NB_MAX][2];            /**< the running rule's neighbourhood: dx, dy */
static int s_nbn;                         /**< its neighbours */
static int s_ax, s_ay, s_adir;            /**< the ant: its cell, and its heading (0 up, 1 right, 2 down, 3 left) */
static uint64_t s_hist[AUTO_SETTLE];      /**< a settling rule's last boards' hashes (a ring) */
static int s_hist_n;                      /**< hashes in s_hist */
static int s_hist_at;                     /**< where the next goes */
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

/** @brief The running rule's neighbourhood: a square of its range (just the 8 around, for most rules). */
static void neighbourhood(void)
{
    int r = RULES[s_rule].range;
    s_nbn = 0;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx || dy) {
                s_nb[s_nbn][0] = (int8_t)dx;
                s_nb[s_nbn][1] = (int8_t)dy;
                s_nbn++;
            }
        }
    }
}

/**
 * @brief A cell's neighbours in a state.
 * @param[in] x  column
 * @param[in] y  row
 * @param[in] st  the state
 * @return how many
 */
static int count(int x, int y, uint8_t st)
{
    int n = 0;
    for (int k = 0; k < s_nbn; k++) {
        int xx = x + s_nb[k][0], yy = y + s_nb[k][1];
        xx += xx < 0 ? AUTO_N : xx >= AUTO_N ? -AUTO_N : 0;   /* (the edges wrap around) */
        yy += yy < 0 ? AUTO_N : yy >= AUTO_N ? -AUTO_N : 0;
        n += s_cur[yy * AUTO_N + xx] == st;
    }
    return n;
}

/**
 * @brief A cell's key from its state, its state before and its key before.
 * @param[in] st  its state now
 * @param[in] was  its state before
 * @param[in] key  its key before
 * @return its key now
 */
static uint8_t key_of(uint8_t st, uint8_t was, uint8_t key)
{
    int kind = RULES[s_rule].kind;
    if (kind == AUTO_CYCLIC || kind == AUTO_TURMITE) {
        return (uint8_t)(AUTO_FADE + st);           /* (every state its colour; the ant's 0 unlit, as AUTO_FADE) */
    }
    if (st) {
        if (kind == AUTO_GENERATIONS && st == 1 && was != 1) {
            return AUTO_FADE;                       /* just born */
        }
        return (uint8_t)(AUTO_FADE + st);
    }
    if (key >= AUTO_FADE) {
        return AUTO_FADE - 1;                       /* just dead: fading out */
    }
    return key ? key - 1 : 0;
}

/**
 * @brief The board's hash (FNV-1a over the states).
 * @return it
 */
static uint64_t board_hash(void)
{
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < AUTO_CELLS; i++) {
        h = (h ^ s_cur[i]) * 1099511628211ULL;
    }
    return h;
}

/**
 * @brief Whether the board is one of the last AUTO_SETTLE again (and keeps it in the history).
 * @return true if it is: settled
 */
static bool settled(void)
{
    uint64_t h = board_hash();
    for (int i = 0; i < s_hist_n; i++) {
        if (s_hist[i] == h) {
            return true;
        }
    }
    s_hist[s_hist_at] = h;
    s_hist_at = (s_hist_at + 1) % AUTO_SETTLE;
    if (s_hist_n < AUTO_SETTLE) {
        s_hist_n++;
    }
    return false;
}

/* (documented in automata_core.h) */
void auto_set_rule(int rule)
{
    s_rule = rule >= 0 && rule < AUTO_R_COUNT ? rule : AUTO_R_LIFE;
    const rule_t *r = &RULES[s_rule];
    neighbourhood();
    s_gen = 0;
    s_hist_n = s_hist_at = 0;
    s_ax = s_ay = AUTO_N / 2;
    s_adir = 0;
    if (!s_cur) {
        return;
    }
    int lo = r->patch ? (AUTO_N - r->patch) / 2 : 0, hi = r->patch ? lo + r->patch : AUTO_N;
    for (int y = 0; y < AUTO_N; y++) {
        for (int x = 0; x < AUTO_N; x++) {
            int i = y * AUTO_N + x;
            uint8_t st = 0;
            if (r->kind != AUTO_TURMITE && x >= lo && x < hi && y >= lo && y < hi) {
                st = r->density ? (uint8_t)(rnd() % 100 < r->density) : (uint8_t)(rnd() % r->states);
            }
            uint8_t was = s_cur[i];
            s_cur[i] = st;
            s_key[i] = key_of(st, r->kind == AUTO_GENERATIONS ? 1 : was, s_key[i]);   /* (no "just born" mark) */
        }
    }
    if (r->kind == AUTO_TURMITE) {
        s_key[s_ay * AUTO_N + s_ax] = AUTO_KEY_ANT;
    }
}

/* (documented in automata_core.h) */
bool auto_start(uint32_t seed, int rule)
{
    if (!s_cur) {
        s_cur = calloc(AUTO_CELLS, 1);
        s_next = calloc(AUTO_CELLS, 1);
        s_key = calloc(AUTO_CELLS, 1);
        if (!s_cur || !s_next || !s_key) {
            auto_free();
            return false;
        }
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    auto_set_rule(rule);
    return true;
}

/* (documented in automata_core.h) */
void auto_free(void)
{
    free(s_cur);
    free(s_next);
    free(s_key);
    s_cur = s_next = s_key = NULL;
}

/* (documented in automata_core.h) */
void auto_clear(void)
{
    if (s_cur) {
        memset(s_cur, 0, AUTO_CELLS);
        memset(s_key, RULES[s_rule].kind == AUTO_TURMITE ? AUTO_FADE : 0, AUTO_CELLS);
    }
    s_gen = 0;
    s_hist_n = s_hist_at = 0;
    s_ax = s_ay = AUTO_N / 2;
    s_adir = 0;
}

/* (documented in automata_core.h) */
void auto_set(int x, int y, int st)
{
    if (s_cur) {
        int i = y * AUTO_N + x;
        s_cur[i] = (uint8_t)st;
        s_key[i] = key_of((uint8_t)st, (uint8_t)st, s_key[i]);
    }
}

/**
 * @brief A cell's next state (every kind but the ant's).
 * @param[in] x  column
 * @param[in] y  row
 * @return its state next generation
 */
static uint8_t next_state(int x, int y)
{
    const rule_t *r = &RULES[s_rule];
    uint8_t st = s_cur[y * AUTO_N + x];
    switch (r->kind) {
    case AUTO_CYCLIC: {
        uint8_t up = (uint8_t)((st + 1) % r->states);
        return count(x, y, up) >= r->threshold ? up : st;
    }
    case AUTO_EXCITABLE:
        if (st) {
            return (uint8_t)((st + 1) % r->states);
        }
        return count(x, y, 1) >= r->threshold ? 1 : 0;
    case AUTO_FOREST:
        if (st == 2) {
            return 0;                               /* burnt out */
        }
        if (st == 1) {
            return count(x, y, 2) || rnd() % FOREST_STRIKE == 0 ? 2 : 1;
        }
        return rnd() % FOREST_GROW == 0 ? 1 : 0;
    default:                                        /* Generations */
        if (st == 0) {
            return (r->born >> count(x, y, 1)) & 1 ? 1 : 0;
        }
        if (st == 1 && ((r->survive >> count(x, y, 1)) & 1)) {
            return 1;
        }
        return (uint8_t)((st + 1) % r->states);     /* dying (2 states: dead) */
    }
}

/** @brief Langton's ant's steps for a generation: turn by the cell, move it on a state, step forward. */
static void ant_steps(void)
{
    static const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    const rule_t *r = &RULES[s_rule];
    for (int k = 0; k < r->steps; k++) {
        int i = s_ay * AUTO_N + s_ax;
        uint8_t st = s_cur[i];
        s_adir = (s_adir + (r->turns[st] == 'R' ? 1 : 3)) & 3;
        s_cur[i] = (uint8_t)((st + 1) % r->states);
        s_key[i] = key_of(s_cur[i], st, s_key[i]);
        s_ax = (s_ax + DX[s_adir] + AUTO_N) % AUTO_N;
        s_ay = (s_ay + DY[s_adir] + AUTO_N) % AUTO_N;
        s_gen++;
    }
    s_key[s_ay * AUTO_N + s_ax] = AUTO_KEY_ANT;
}

/* (documented in automata_core.h) */
bool auto_step(void)
{
    if (!s_cur) {
        return false;
    }
    const rule_t *r = &RULES[s_rule];
    if (s_gen >= r->run) {
        auto_set_rule(s_rule);
        return true;
    }
    if (r->kind == AUTO_TURMITE) {
        ant_steps();
        return false;
    }
    int alive = 0;
    bool any = false;
    for (int y = 0; y < AUTO_N; y++) {
        for (int x = 0; x < AUTO_N; x++) {
            uint8_t n = next_state(x, y);
            s_next[y * AUTO_N + x] = n;
            any |= n != 0;
            alive += n == 1;
        }
    }
    for (int i = 0; i < AUTO_CELLS; i++) {
        s_key[i] = key_of(s_next[i], s_cur[i], s_key[i]);
    }
    uint8_t *t = s_cur;
    s_cur = s_next;
    s_next = t;
    s_gen++;
    bool dies = r->kind == AUTO_GENERATIONS || r->kind == AUTO_EXCITABLE;
    if ((dies && !any) || alive < r->quiet || (r->settles && settled())) {
        auto_set_rule(s_rule);                      /* died out, quiet or settled: a new start */
        return true;
    }
    return false;
}

/* (documented in automata_core.h) */
int auto_state(int x, int y)
{
    return s_cur ? s_cur[y * AUTO_N + x] : 0;
}

/* (documented in automata_core.h) */
int auto_key(int x, int y)
{
    return s_key ? s_key[y * AUTO_N + x] : 0;
}

/* (documented in automata_core.h) */
void auto_ant(int *x, int *y)
{
    *x = s_ax;
    *y = s_ay;
}

/* (documented in automata_core.h) */
int auto_rule(void)
{
    return s_rule;
}

/* (documented in automata_core.h) */
const char *auto_rule_name(int rule)
{
    return RULES[rule >= 0 && rule < AUTO_R_COUNT ? rule : AUTO_R_LIFE].name;
}

/* (documented in automata_core.h) */
int auto_kind(void)
{
    return RULES[s_rule].kind;
}

/* (documented in automata_core.h) */
int auto_states(void)
{
    return RULES[s_rule].states;
}

/* (documented in automata_core.h) */
int auto_step_ms(void)
{
    return RULES[s_rule].ms;
}

/* (documented in automata_core.h) */
uint32_t auto_generation(void)
{
    return s_gen;
}

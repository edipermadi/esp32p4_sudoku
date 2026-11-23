/**
 * @file ant_core.c
 * @ingroup ant_core
 * @brief Langton's ant for the screen saver, without LVGL (drawn by ui_saver_ant.c).
 */
#include "ant_core.h"
#include <stdlib.h>
#include <string.h>

/** @brief The rules a run picks from (the classic first, and the most often). */
static const char *const RULES[] = {
    "RL", "RL", "RL",                       /* the classic: chaos, then a highway */
    "LLRR",                                 /* grows symmetric, like a brain */
    "LRRRRRLLR",                            /* fills squares */
    "RRLLLRLLLRRR",                         /* grows a triangle */
    "LLRRRLRLRLLR",                         /* a twisting highway */
    "RLR",                                  /* chaos that keeps growing */
};
#define RULES_N         ((int)(sizeof(RULES) / sizeof(RULES[0])))   /**< how many */

static uint8_t *s_grid;                   /**< each cell's state (NULL: none) */
static const char *s_rule;                /**< the rule running */
static int s_states;                      /**< its states */
static int s_x, s_y, s_dir;               /**< the ant: its cell, and its heading (0 up, 1 right, 2 down, 3 left) */
static uint32_t s_steps;                  /**< steps since the run began */
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

/* (documented in ant_core.h) */
void ant_set_rule(const char *rule)
{
    s_rule = rule;
    s_states = (int)strlen(rule);
    s_states = s_states < 2 ? 2 : s_states > ANT_STATES_MAX ? ANT_STATES_MAX : s_states;
    if (s_grid) {
        memset(s_grid, 0, ANT_CELLS);
    }
    s_x = s_y = ANT_N / 2;
    s_dir = 0;
    s_steps = 0;
}

/* (documented in ant_core.h) */
bool ant_start(uint32_t seed)
{
    if (!s_grid) {
        s_grid = malloc(ANT_CELLS);
        if (!s_grid) {
            return false;
        }
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    ant_set_rule(RULES[rnd() % RULES_N]);
    return true;
}

/* (documented in ant_core.h) */
void ant_free(void)
{
    free(s_grid);
    s_grid = NULL;
}

/* (documented in ant_core.h) */
bool ant_step(int *changed)
{
    if (!s_grid) {
        *changed = -1;
        return false;
    }
    if (s_steps >= ANT_RUN) {
        ant_set_rule(RULES[rnd() % RULES_N]);
        *changed = -1;
        return true;
    }
    int i = s_y * ANT_N + s_x;
    uint8_t st = s_grid[i];
    s_dir = (s_dir + (s_rule[st] == 'R' ? 1 : 3)) & 3;
    s_grid[i] = (uint8_t)((st + 1) % s_states);
    *changed = i;
    static const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    s_x = (s_x + DX[s_dir] + ANT_N) % ANT_N;
    s_y = (s_y + DY[s_dir] + ANT_N) % ANT_N;
    s_steps++;
    return false;
}

/* (documented in ant_core.h) */
uint8_t ant_cell(int x, int y)
{
    return s_grid ? s_grid[y * ANT_N + x] : 0;
}

/* (documented in ant_core.h) */
void ant_pos(int *x, int *y)
{
    *x = s_x;
    *y = s_y;
}

/* (documented in ant_core.h) */
const char *ant_rule(void)
{
    return s_rule;
}

/* (documented in ant_core.h) */
int ant_states(void)
{
    return s_states;
}

/* (documented in ant_core.h) */
uint32_t ant_steps(void)
{
    return s_steps;
}

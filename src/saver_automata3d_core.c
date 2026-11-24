/**
 * @file saver_automata3d_core.c
 * @ingroup saver_automata3d_core
 * @brief 3D cellular automata for the screen saver, without LVGL (drawn by ui_saver_automata3d.c).
 *
 * The 26 neighbours' live count is a 3 x 3 x 3 box sum, worked out in three passes of three (along x, then
 * y, then z) less the cell itself: some 10 additions a cell rather than 26.
 */
#include "saver_automata3d_core.h"
#include <stdlib.h>
#include <string.h>

#define PLANE           (A3_N * A3_N)         /**< cells in a z plane */

/** @brief A rule. */
typedef struct {
    const char *name;                     /**< its name and notation */
    uint8_t states;                       /**< its states */
    bool moore;                           /**< the 26 neighbours around (else the 6 face to face) */
    uint32_t survive;                     /**< bit n: stays alive with n live neighbours */
    uint32_t born;                        /**< bit n: born with n */
    uint8_t density;                      /**< % of the start's cells alive */
    uint8_t patch;                        /**< the start: a cube this many cells across in the middle */
    uint16_t ms;                          /**< a generation each, ms */
    uint16_t run;                         /**< generations before it stops ... */
    uint16_t hold;                        /**< ... and generations it then stands still before the next rule */
} rule3_t;

/** @brief Bits lo..hi set. */
#define BITS(lo, hi)    ((uint32_t)((((uint64_t)1 << ((hi) + 1)) - 1) & ~(((uint64_t)1 << (lo)) - 1)))
/** @brief Bit n. */
#define BIT(n)          ((uint32_t)1 << (n))

static const rule3_t RULES[A3_R_COUNT] = {
    [A3_R_CRYSTAL] = {"Crystal growth  0-6/1,3/2/VN", 2, false, BITS(0, 6), BIT(1) | BIT(3), 100, 1, 500, 14, 16},
    [A3_R_DIAMOND] = {"Diamond growth  5-6/1-3/7/VN", 7, false, BITS(5, 6), BITS(1, 3), 100, 1, 400, 200, 0},
    [A3_R_PYRO] = {"Pyroclastic  4-7/6-8/10/M", 10, true, BITS(4, 7), BITS(6, 8), 40, 12, 250, 300, 0},
    [A3_R_BUILDER] = {"Builder 1  2,6,9/4,6,8-9/10/M", 10, true, BIT(2) | BIT(6) | BIT(9), BIT(4) | BIT(6) | BITS(8, 9), 30, 10, 250, 300, 0},
};

static uint8_t *s_cur;                    /**< each cell's state (NULL: no cube) */
static uint8_t *s_next;                   /**< the next generation being worked out */
static uint8_t *s_a;                      /**< box sums, along the way */
static uint8_t *s_b;                      /**< box sums, along the way */
static int s_rule;                        /**< the rule running */
static uint32_t s_gen;                    /**< generations since the run began */
static int s_pop;                         /**< cells not dead */
static int s_still;                       /**< generations in a row without a change */
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

/** @brief The running rule's random start: its patch of noise in the middle (one cell for a patch of 1). */
static void seed_start(void)
{
    const rule3_t *r = &RULES[s_rule];
    int lo = (A3_N - r->patch) / 2, hi = lo + r->patch;
    memset(s_cur, 0, A3_CELLS);
    s_pop = 0;
    for (int z = lo; z < hi; z++) {
        for (int y = lo; y < hi; y++) {
            for (int x = lo; x < hi; x++) {
                if (rnd() % 100 < r->density) {
                    s_cur[(z * A3_N + y) * A3_N + x] = 1;
                    s_pop++;
                }
            }
        }
    }
    s_gen = 0;
    s_still = 0;
}

/* (documented in saver_automata3d_core.h) */
bool a3_start(uint32_t seed, int rule)
{
    s_rng = seed ? seed : 1;
    if (!s_cur) {
        s_cur = malloc(A3_CELLS);
        s_next = malloc(A3_CELLS);
        s_a = malloc(A3_CELLS);
        s_b = malloc(A3_CELLS);
    }
    if (!s_cur || !s_next || !s_a || !s_b) {
        a3_free();
        return false;
    }
    a3_set_rule(rule);
    return true;
}

/* (documented in saver_automata3d_core.h) */
void a3_free(void)
{
    free(s_cur);
    free(s_next);
    free(s_a);
    free(s_b);
    s_cur = s_next = s_a = s_b = NULL;
}

/* (documented in saver_automata3d_core.h) */
void a3_set_rule(int rule)
{
    s_rule = rule >= 0 && rule < A3_R_COUNT ? rule : 0;
    if (s_cur) {
        seed_start();
    }
}

/* (documented in saver_automata3d_core.h) */
void a3_clear(void)
{
    if (s_cur) {
        memset(s_cur, 0, A3_CELLS);
    }
    s_pop = 0;
    s_gen = 0;
    s_still = 0;
}

/* (documented in saver_automata3d_core.h) */
void a3_set(int x, int y, int z, int st)
{
    if (!s_cur || (unsigned)x >= A3_N || (unsigned)y >= A3_N || (unsigned)z >= A3_N) {
        return;
    }
    uint8_t *c = &s_cur[(z * A3_N + y) * A3_N + x];
    s_pop += (st != 0) - (*c != 0);
    *c = (uint8_t)st;
}

/** @brief Each cell's live neighbours (state 1) into s_a: the running rule's neighbourhood, walls dead. */
static void count_live(void)
{
    uint8_t *l = s_b;
    for (int i = 0; i < A3_CELLS; i++) {
        l[i] = s_cur[i] == 1;
    }
    if (!RULES[s_rule].moore) {                     /* the 6 face to face */
        for (int z = 0, i = 0; z < A3_N; z++) {
            for (int y = 0; y < A3_N; y++) {
                for (int x = 0; x < A3_N; x++, i++) {
                    s_a[i] = (uint8_t)((x > 0 ? l[i - 1] : 0) + (x < A3_N - 1 ? l[i + 1] : 0) +
                                       (y > 0 ? l[i - A3_N] : 0) + (y < A3_N - 1 ? l[i + A3_N] : 0) +
                                       (z > 0 ? l[i - PLANE] : 0) + (z < A3_N - 1 ? l[i + PLANE] : 0));
                }
            }
        }
        return;
    }
    for (int i = 0; i < A3_CELLS; i++) {            /* along x: s_b -> s_a */
        int x = i % A3_N;
        s_a[i] = (uint8_t)(l[i] + (x > 0 ? l[i - 1] : 0) + (x < A3_N - 1 ? l[i + 1] : 0));
    }
    for (int i = 0; i < A3_CELLS; i++) {            /* along y: s_a -> s_b */
        int y = (i / A3_N) % A3_N;
        s_b[i] = (uint8_t)(s_a[i] + (y > 0 ? s_a[i - A3_N] : 0) + (y < A3_N - 1 ? s_a[i + A3_N] : 0));
    }
    for (int i = 0; i < A3_CELLS; i++) {            /* along z: s_b -> s_a, less the cell itself */
        int z = i / PLANE;
        s_a[i] = (uint8_t)(s_b[i] + (z > 0 ? s_b[i - PLANE] : 0) + (z < A3_N - 1 ? s_b[i + PLANE] : 0) -
                           (s_cur[i] == 1));
    }
}

/* (documented in saver_automata3d_core.h) */
bool a3_step(void)
{
    if (!s_cur) {
        return false;
    }
    const rule3_t *r = &RULES[s_rule];
    if (s_gen >= r->run + r->hold || s_pop == 0 || s_still >= A3_STILL) {
        seed_start();                               /* the same rule again, from a new start */
        return true;
    }
    if (s_gen >= r->run) {                          /* run its time: standing still a while */
        s_gen++;
        return false;
    }
    count_live();
    int pop = 0, changed = 0;
    for (int i = 0; i < A3_CELLS; i++) {
        uint8_t s = s_cur[i], n;
        if (s == 0) {
            n = (r->born >> s_a[i]) & 1;
        } else if (s == 1) {
            n = (r->survive >> s_a[i]) & 1 ? 1 : r->states > 2 ? 2 : 0;
        } else {
            n = s + 1 < r->states ? s + 1 : 0;
        }
        s_next[i] = n;
        pop += n != 0;
        changed += n != s;
    }
    uint8_t *sw = s_cur;
    s_cur = s_next;
    s_next = sw;
    s_pop = pop;
    s_still = changed ? 0 : s_still + 1;
    s_gen++;
    return false;
}

/* (documented in saver_automata3d_core.h) */
int a3_state(int x, int y, int z)
{
    if (!s_cur || (unsigned)x >= A3_N || (unsigned)y >= A3_N || (unsigned)z >= A3_N) {
        return 0;
    }
    return s_cur[(z * A3_N + y) * A3_N + x];
}

/* (documented in saver_automata3d_core.h) */
const uint8_t *a3_cells(void) { return s_cur; }
/* (documented in saver_automata3d_core.h) */
int a3_rule(void) { return s_rule; }
/* (documented in saver_automata3d_core.h) */
const char *a3_rule_name(int rule) { return rule >= 0 && rule < A3_R_COUNT ? RULES[rule].name : ""; }
/* (documented in saver_automata3d_core.h) */
int a3_states(void) { return RULES[s_rule].states; }
/* (documented in saver_automata3d_core.h) */
int a3_population(void) { return s_pop; }
/* (documented in saver_automata3d_core.h) */
int a3_step_ms(void) { return RULES[s_rule].ms; }
/* (documented in saver_automata3d_core.h) */
uint32_t a3_generation(void) { return s_gen; }

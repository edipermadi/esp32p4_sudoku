/**
 * @file store_stats.cpp
 * @brief The statistics kept in RAM: best time, fewest mistakes and solves per built-in puzzle (sudoku.db has
 *        them all), read from the old NVS keys once.
 */
#include "store.h"
#include "store_internal.h"
#include <Preferences.h>
#include <string.h>
#include "puzzles.h"

/** best time per built-in puzzle, s (0 = unsolved, capped at 65535) */
static uint16_t s_best[PUZZLE_COUNT];
/** Per-puzzle statistics in RAM (sudoku.db has them all). */
typedef struct {
    uint8_t fewest[PUZZLE_COUNT];   /**< STORE_MISTAKES_UNKNOWN = not known */
    uint8_t solves[PUZZLE_COUNT];       /**< times solved, capped at 255 */
} store_stat_t;
static store_stat_t s_stat;             /**< the statistics */

/* (documented in store_internal.h) */
void store_stats_init(void)
{
    store_reset_stats();
    Preferences prefs;
    if (!prefs.begin("sudoku", true)) {
        return;    /* read only; absent on a fresh board */
    }
    if (prefs.getBytesLength("best") == sizeof(s_best)) {
        prefs.getBytes("best", s_best, sizeof(s_best));
    }
    if (prefs.getBytesLength("stat") == sizeof(s_stat)) {
        prefs.getBytes("stat", &s_stat, sizeof(s_stat));
    } else {
        for (int i = 0; i < PUZZLE_COUNT; i++) {
            s_stat.solves[i] = s_best[i] ? 1 : 0;
        }
    }
    prefs.end();
}

/* (documented in store.h) */
uint32_t store_best_time(uint16_t id)
{
    return id < PUZZLE_COUNT ? s_best[id] : 0;
}

/* (documented in store.h) */
bool store_record_solve(uint16_t id, uint32_t seconds, uint32_t mistakes)
{
    if (id >= PUZZLE_COUNT) {
        return false;
    }
    if (seconds == 0) {
        seconds = 1;    /* 0 means "unsolved" */
    }
    if (seconds > 0xFFFF) {
        seconds = 0xFFFF;
    }
    if (mistakes > 254) {
        mistakes = 254;
    }

    if (s_stat.solves[id] < 255) {
        s_stat.solves[id]++;
    }
    if (s_stat.fewest[id] == STORE_MISTAKES_UNKNOWN || mistakes < s_stat.fewest[id]) {
        s_stat.fewest[id] = (uint8_t)mistakes;
    }
    if (s_best[id] != 0 && s_best[id] <= seconds) {
        return false;
    }
    s_best[id] = (uint16_t)seconds;
    return true;
}

/* (documented in store.h) */
void store_merge_stats(const uint16_t *best, const uint8_t *fewest, const uint8_t *solves)
{
    for (int i = 0; i < PUZZLE_COUNT; i++) {
        if (!best[i]) {
            continue;
        }
        if (!s_best[i] || best[i] < s_best[i]) {
            s_best[i] = best[i];
        }
        if (fewest[i] != STORE_MISTAKES_UNKNOWN &&
                (s_stat.fewest[i] == STORE_MISTAKES_UNKNOWN || fewest[i] < s_stat.fewest[i])) {
            s_stat.fewest[i] = fewest[i];
        }
        if (solves[i] > s_stat.solves[i]) {
            s_stat.solves[i] = solves[i];
        }
    }
}

/* (documented in store.h) */
void store_reset_stats(void)
{
    memset(s_best, 0, sizeof(s_best));
    memset(s_stat.fewest, STORE_MISTAKES_UNKNOWN, sizeof(s_stat.fewest));
    memset(s_stat.solves, 0, sizeof(s_stat.solves));
}

/* (documented in store.h) */
void store_copy_stats(uint16_t *best, uint8_t *fewest, uint8_t *solves)
{
    memcpy(best, s_best, sizeof(s_best));
    memcpy(fewest, s_stat.fewest, sizeof(s_stat.fewest));
    memcpy(solves, s_stat.solves, sizeof(s_stat.solves));
}

/* (documented in store.h) */
uint8_t store_fewest_mistakes(uint16_t id)
{
    return id < PUZZLE_COUNT ? s_stat.fewest[id] : STORE_MISTAKES_UNKNOWN;
}

/* (documented in store.h) */
uint8_t store_solve_count(uint16_t id)
{
    return id < PUZZLE_COUNT ? s_stat.solves[id] : 0;
}

/* (documented in store.h) */
uint16_t store_solved_count(uint16_t first, uint16_t count)
{
    uint16_t n = 0;
    for (uint16_t i = first; i < first + count && i < PUZZLE_COUNT; i++) {
        if (s_best[i]) {
            n++;
        }
    }
    return n;
}

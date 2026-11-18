#pragma once
/*
 * The game's data in a SQLite database on the microSD card: /sdcard/sudoku.db.
 * Statistics, the game in progress and the settings are kept here (nothing
 * is written to NVS any more, except Wi-Fi's network by wifi_mgr.cpp):
 *
 *   solves(id, puzzle, level, number, seconds, mistakes, solved_at)
 *       one row per solve; solved_at is Unix time (UTC), NULL when the clock
 *       wasn't set (no Wi-Fi/NTP yet)
 *   puzzle_stats(puzzle, level, number, best_seconds, fewest_mistakes, solves)
 *       per puzzle, updated with every solve; fewest_mistakes NULL = solved
 *       before mistakes were counted
 *   level_stats (view): per level solved / fastest / average / mistakes
 *   saved_game(slot = 1, puzzle, level, number, cells, notes, elapsed_ms,
 *              mistakes, saved_at)
 *       the one unfinished game; cells = 81 digits row by row (0 = empty,
 *       givens included), notes = 81 x uint16 little-endian (bit d = mark d)
 *   settings(key, value): dark_mode, sound, screen_off_minutes, time_zone,
 *       last_level, and migrated_from_nvs (set once the values the firmware
 *       used to keep in NVS have been copied in)
 *
 * The Statistics screen reads it with stats_db_query(); the other screens use
 * the RAM copy in store.h, which is merged with puzzle_stats once the database
 * is open (stats_db_take_cache()). Without a card, solves stay in RAM only.
 *
 * All database and card work runs in one worker task; calls here queue
 * commands or pick up finished results, except stats_db_suspend(), which waits.
 */
#include <stdbool.h>
#include <stdint.h>
#include "puzzles.h"
#include "store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATS_MAX_PUZZLES 100            /* rows per level in a query result */

typedef struct {
    uint16_t solved, perfect, known;     /* known: puzzles with a mistake count */
    uint32_t solves, time_sum, fastest, miss_sum;
} stats_level_t;

typedef struct {
    uint16_t number;                     /* 1..100 within the level */
    uint16_t best;                       /* seconds */
    uint8_t  fewest;                     /* STORE_MISTAKES_UNKNOWN = not known */
    uint16_t solves;
} stats_puzzle_t;

typedef struct {
    int level;                           /* -1 = all levels, else collection index */
    stats_level_t levels[PUZZLE_COLLECTION_COUNT];
    stats_level_t total;
    int n_puzzles;                       /* rows in puzzles (level >= 0 only) */
    stats_puzzle_t puzzles[STATS_MAX_PUZZLES];
} stats_view_t;

/* Mounts the card and opens (or creates) the database, merges in the
 * statistics the firmware kept in NVS before (read only, idempotent), copies
 * the current settings and game in on first use, then loads puzzle_stats for
 * stats_db_take_cache() and the settings for stats_db_take_prefs(). Call once
 * the card has been probed, from the LVGL task (or before LVGL runs). */
void stats_db_start(void);
bool stats_db_ready(void);
/* Waits until the open started by stats_db_start() has finished (or failed). */
bool stats_db_wait_open(uint32_t timeout_ms);
/* After opening: the settings and saved game from sudoku.db, on top of
 * what the board had (store_get_prefs()); true once, when they are new. */
bool stats_db_take_prefs(store_prefs_t *out);

/* Settings and the game in progress (queued; dropped without a database). */
void stats_db_set_int(const char *key, int value);
void stats_db_set_text(const char *key, const char *value);
void stats_db_save_game(const saved_game_t *g);   /* NULL: no game in progress */

/* Logs a finished puzzle and updates its puzzle_stats row. */
void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes);

/* Asks for the Statistics screen's data (level -1 = all levels). */
void stats_db_query(int level);
/* Copies the newest query result into out; false if none arrived since the last call. */
bool stats_db_take_view(stats_view_t *out);
/* After opening: copies puzzle_stats out as PUZZLE_COUNT arrays (see
 * store_merge_stats()); true once, when they are new. */
bool stats_db_take_cache(uint16_t *best, uint8_t *fewest, uint8_t *solves);

/* Closes the database and unmounts the card (before formatting); waits up
 * to a few seconds. stats_db_resume() mounts and opens it again. */
void stats_db_suspend(void);
void stats_db_resume(void);

/* One line for Settings, e.g. "Stats in sudoku.db: 12 solves". */
void stats_db_status(char *buf, int len);

#ifdef __cplusplus
}
#endif

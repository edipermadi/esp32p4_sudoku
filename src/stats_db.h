#pragma once
/*
 * The game's data in a SQLite database on the microSD card: /sdcard/sudoku.db.
 * Statistics, the game in progress and the settings are kept here (nothing
 * is written to NVS any more, except Wi-Fi's network by wifi_mgr.cpp):
 *
 *   solves(id, puzzle, level, number, seconds, mistakes, finished_at, started_at)
 *       one row per solve; started_at = when the puzzle was opened (or
 *       restarted), finished_at = when it was solved; Unix time (UTC), NULL
 *       when the clock wasn't set (no Wi-Fi/NTP yet)
 *   puzzle_stats(puzzle, level, number, best_seconds, fewest_mistakes, solves)
 *       per puzzle, updated with every solve; fewest_mistakes NULL = solved
 *       before mistakes were counted
 *   level_stats (view): per level solved / fastest / average / mistakes
 *   saved_game(slot = 1, puzzle, level, number, cells, notes, elapsed_ms,
 *              mistakes, saved_at, started_at)    saved after every move
 *       the one unfinished game; cells = 81 digits row by row (0 = empty,
 *       givens included), notes = 81 x uint16 little-endian (bit d = mark d)
 *   settings(key, value): dark_mode, sound, screen_off_minutes, time_zone,
 *       last_level, and migrated_from_nvs (set when the database is first
 *       filled: from what the board has, or, after a format, with just the
 *       current settings and game)
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

/* Mounts the card and opens (or creates) the database. A new database (no
 * sudoku.db on the card yet) is a skeleton: the tables, the current settings
 * and the game in progress, no statistics. A database made by a build from
 * before the settings moved here gets the board's statistics merged in once. Then loads puzzle_stats for stats_db_take_cache() and the
 * settings for stats_db_take_prefs(). Call once the card has been probed. */
void stats_db_start(void);
/* After formatting: a new, empty sudoku.db holding only the current settings
 * and the game in progress (no statistics). Also call store_reset_stats(). */
void stats_db_start_fresh(void);
bool stats_db_ready(void);
/* Waits until the open started by stats_db_start() has finished (or failed). */
bool stats_db_wait_open(uint32_t timeout_ms);
/* After opening: the settings and saved game from sudoku.db, on top of
 * what the board had (store_get_prefs()); true once, when they are new. */
bool stats_db_take_prefs(store_prefs_t *out);

/* Settings and the game in progress (queued; dropped without a database). */
void stats_db_set_int(const char *key, int value);
void stats_db_set_text(const char *key, const char *value);
/* Cheap to call after every move: only the newest game is written, so a
 * burst of moves never queues up behind the card. */
void stats_db_save_game(const saved_game_t *g);   /* NULL: no game in progress */

/* Logs a finished puzzle (finished now) and updates its puzzle_stats row.
 * started_at: Unix time it was opened, 0 = unknown. */
void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes, int64_t started_at);

/* Asks for the Statistics screen's data (level -1 = all levels). */
void stats_db_query(int level);
/* Copies the newest query result into out; false if none arrived since the last call. */
bool stats_db_take_view(stats_view_t *out);
/* After opening: copies puzzle_stats out as PUZZLE_COUNT arrays (see
 * store_merge_stats()); true once, when they are new. */
bool stats_db_take_cache(uint16_t *best, uint8_t *fewest, uint8_t *solves);

/* Closes the database and unmounts the card (before formatting); waits up
 * to a few seconds. stats_db_start_fresh() mounts and starts a new one. */
void stats_db_suspend(void);

/* One line for Settings, e.g. "Stats in sudoku.db: 12 solves". */
void stats_db_status(char *buf, int len);

#ifdef __cplusplus
}
#endif

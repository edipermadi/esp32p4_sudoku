#pragma once
/*
 * The game's data in a SQLite database on the microSD card: /sdcard/sudoku.db.
 * Statistics, the game in progress and the settings are kept here (nothing
 * is written to NVS any more, except Wi-Fi's network by wifi_mgr.cpp):
 *
 *   players(id, name, created_at)
 *       who plays; settings.current_player is the one playing now. Each
 *       player has their own statistics and game in progress.
 *   solves(id, player_id, puzzle, level, number, seconds, mistakes,
 *          finished_at, started_at, hints, moves)
 *       one row per solve; started_at = when the puzzle was opened (or
 *       restarted), finished_at = when it was solved; Unix time (UTC), NULL
 *       when the clock wasn't set (no Wi-Fi/NTP yet)
 *   puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)
 *       per puzzle, updated with every solve; fewest_mistakes NULL = solved
 *       before mistakes were counted
 *   level_stats (view): per player and level: solved / fastest / average / mistakes
 *   saved_game(player_id, puzzle, level, number, cells, notes, elapsed_ms,
 *              mistakes, saved_at, started_at, hints, moves)    saved after every move
 *       the one unfinished game; cells = 81 digits row by row (0 = empty,
 *       givens included), notes = 81 x uint16 little-endian (bit d = mark d)
 *   generated(id, level, givens, created_at): puzzles made on the board;
 *       their puzzle id elsewhere is 2000 + id, level "Generated Easy" etc.
 *   settings(key, value): dark_mode, sound, screen_off_minutes, brightness, volume, time_zone,
 *       last_level, current_player, and migrated_from_nvs (set when the database is first
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
    int32_t player_id;                   /* whose statistics these are */
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
void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes, uint32_t hints,
                           int64_t started_at, const game_move_t *moves, int nmoves);
/* The move log to save with the next stats_db_save_game() of that puzzle
 * (copied). And the log sudoku.db had for the saved game it loaded last (at
 * start-up or a player switch): count, puzzle = which game it belongs to. */
void stats_db_set_game_moves(uint16_t puzzle, const game_move_t *moves, int n);
int stats_db_copy_saved_moves(uint16_t *puzzle, game_move_t *out, int max);

/* A solve with its move log, for the Replay screen (asked for by solves.id). */
typedef struct {
    int64_t solve_id;
    bool found;                          /* false: no such solve, or no log recorded */
    uint16_t puzzle;
    uint32_t seconds, mistakes, hints;
    int64_t finished_at;
    char player[STORE_NAME_LEN];
    int n;
    game_move_t moves[GAME_MOVES_MAX];   /* only the first n are copied */
} stats_replay_t;
void stats_db_query_replay(int64_t solve_id);
bool stats_db_take_replay(stats_replay_t *out);

/* Asks for the Statistics screen's data (level -1 = all levels). */
void stats_db_query(int level);
/* Copies the newest query result into out; false if none arrived since the last call. */
bool stats_db_take_view(stats_view_t *out);
/* After opening: copies puzzle_stats out as PUZZLE_COUNT arrays (see
 * store_merge_stats()); true once, when they are new. */
bool stats_db_take_cache(uint16_t *best, uint8_t *fewest, uint8_t *solves);

/* ---------------------------------------------------------------- players */
#define STATS_MAX_PLAYERS 16

typedef struct {
    int32_t id;
    char name[STORE_NAME_LEN];
    uint16_t solved;                     /* puzzles solved */
    uint32_t solves;                     /* solves logged */
} stats_player_t;

typedef struct {
    int32_t id;                          /* who plays now */
    char name[STORE_NAME_LEN];
    bool switched;                       /* another player: game below + stats cache */
    saved_game_t game;                   /* their game in progress (switched only) */
} stats_player_change_t;

/* Asks for the player list (stats_db_take_players()). */
void stats_db_list_players(void);
/* Adds a player and switches to them; renames one; switches to one. Results
 * come back through stats_db_take_player_change() (and the stats cache, the
 * player list, or stats_db_take_message() for a refusal). The game in
 * progress is saved for the player it belongs to before a switch. */
void stats_db_add_player(const char *name);
void stats_db_rename_player(int32_t id, const char *name);
void stats_db_switch_player(int32_t id);
/* Deletes a player with their solves, statistics and game in progress (not
 * the last one). Deleting the current player switches to the first one left. */
void stats_db_delete_player(int32_t id);
/* Newest player list: count, or -1 if none arrived since the last call. */
int stats_db_take_players(stats_player_t *out);   /* out: STATS_MAX_PLAYERS entries */
bool stats_db_take_player_change(stats_player_change_t *out);
/* A message for the user (e.g. "name taken"); false if none. */
bool stats_db_take_message(char *buf, int len);

/* ---------------------------------------------------------------- history */
/* Days are local dates (the time zone in settings); solves finished before
 * the clock was set have no date and don't appear. */
typedef struct {
    int year, month;                     /* 0 = nothing dated yet */
    uint8_t count[31];                   /* solves + games started, per day (index day-1) */
} stats_month_t;

#define STATS_MAX_DAY_ROWS 40
typedef struct {
    char player[STORE_NAME_LEN];
    uint16_t puzzle;
    bool in_progress;                    /* a saved game started that day, not solved yet */
    int64_t started_at, finished_at;     /* Unix time, 0 = unknown */
    uint32_t seconds;                    /* time taken (in progress: so far) */
    uint16_t mistakes;
    uint16_t hints;                      /* cells filled with Fill */
    int64_t solve_id;                    /* solves.id, 0 for a game in progress */
    bool has_moves;                      /* a move log was recorded: it can be replayed */
} stats_day_row_t;

typedef struct {
    int year, month, day;
    int n, total;                        /* rows here / rows that day (n <= STATS_MAX_DAY_ROWS) */
    stats_day_row_t rows[STATS_MAX_DAY_ROWS];   /* oldest first */
} stats_day_t;

/* Which days of a month had games (all players). year 0: the most recent
 * month that has any. */
void stats_db_query_month(int year, int month);
bool stats_db_take_month(stats_month_t *out);
/* Who played what on a day. */
void stats_db_query_day(int year, int month, int day);
bool stats_db_take_day(stats_day_t *out);

/* A page of the solves that have a move log, newest first (Gameplays screen):
 * rows offset..offset+n-1 of total. */
typedef struct {
    int offset, n, total;
    stats_day_row_t rows[STATS_MAX_DAY_ROWS];
} stats_plays_t;
/* Puzzles made on the board (generator.h): stores one and returns its id in
 * the generated table (puzzle id = PUZZLE_GEN_BASE + it), or -1 without a
 * database; reads one back. Both wait for the database (milliseconds). */
int stats_db_gen_add(int level, const uint8_t givens[81]);
bool stats_db_gen_get(int id, uint8_t givens[81], int *level);

void stats_db_query_plays(int offset);
bool stats_db_take_plays(stats_plays_t *out);

/* Deletes sudoku.db and builds a new skeleton with what store.h holds now
 * (call store_factory_reset() first). The result comes back like an open:
 * stats_db_take_prefs(). */
void stats_db_factory_reset(void);

/* Closes the database and unmounts the card (before formatting); waits up
 * to a few seconds. stats_db_start_fresh() mounts and starts a new one. */
void stats_db_suspend(void);

/* One line for Settings, e.g. "Stats in sudoku.db: 12 solves". */
void stats_db_status(char *buf, int len);

#ifdef __cplusplus
}
#endif

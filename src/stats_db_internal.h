/**
 * @file stats_db_internal.h
 * @ingroup stats_db
 * @brief What the parts of stats_db share (private to them; the game uses stats_db.h).
 *
 * The database is worked on by one task, the worker (stats_db.c): the game's calls queue a command
 * (cmd_t) and the worker runs the part's sdb_do_ function, or a call waits for its answer (s_gen).
 * The parts:
 *
 * | File                     | What                                                        |
 * |--------------------------|-------------------------------------------------------------|
 * | stats_db.c               | the worker, opening and closing, the calls shared           |
 * | stats_db_schema.c        | the tables, and upgrades                                    |
 * | stats_db_prefs.c         | settings                                                    |
 * | stats_db_players.c       | players: list, add, rename, delete, switch                  |
 * | stats_db_games.c         | unfinished games and their moves                            |
 * | stats_db_replay.c        | a solve's replay loaded                                     |
 * | stats_db_solves.c        | logging a solve; statistics per level                       |
 * | stats_db_kinds.c         | puzzle kinds and level names                                |
 * | stats_db_generated.c     | generated puzzles                                           |
 * | stats_db_daily.c         | the daily puzzle's status and streak                        |
 * | stats_db_rank.c          | daily rank, leaderboard, head-to-head                       |
 * | stats_db_achievements.c  | achievements                                                |
 * | stats_db_import.c        | collections imported from the card (parsing: import_parse.c) |
 * | stats_db_saved.c         | saved collections, and their export                         |
 * | stats_db_history.c       | month, day, plays                                           |
 * | stats_db_weekly.c        | the weekly challenge, this week / last week                 |
 * | stats_db_insights.c      | mistake insights                                            |
 * | stats_db_records.c       | personal records                                            |
 * | stats_db_export.c        | CSV export, backup                                          |
 * | stats_db_small.c         | bookmarks, seen tips, time attack, techniques learned       |
 * | stats_db_sql.c           | SQL from the host (devctl sql)                              |
 *
 * The state each part keeps is static in it; the others reach the worker's (the database, the
 * lock, the player) through the functions below. All sdb_do_ functions and the ones that use the
 * database run on the worker.
 */
#pragma once
#include "stats_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sqlite/sqlite3.h"
#include "bsp/bsp_board.h"
#include "store.h"
#include "generator.h"
#include "sdx.h"
#include "timezones.h"

/** @brief Why saving or importing is refused while the clock isn't set (each collection is dated). */
#define SDB_NO_CLOCK "The clock isn't set yet: collections are dated, so connect to Wi-Fi first (Settings > Network)."

/**
 * @addtogroup stats_db
 * @{
 */

#define DB_PATH BSP_SD_MOUNT_POINT "/sudoku.db"   /**< the database file */

/** @brief PUZZLE_COUNT-sized arrays, as in store.h. */
typedef struct {
    uint16_t best[PUZZLE_COUNT];        /**< best time, seconds (0: not solved) */
    uint8_t fewest[PUZZLE_COUNT];       /**< fewest mistakes (STORE_MISTAKES_UNKNOWN: not known) */
    uint8_t solves[PUZZLE_COUNT];       /**< times solved */
} stats_arrays_t;

/** @brief CMD_OPEN: what the board has (freed by the worker). */
typedef struct {
    stats_arrays_t stats;       /**< first use: copied into puzzle_stats */
    store_prefs_t prefs;        /**< first use: copied in; base for stats_db_take_prefs() */
    bool fresh;                 /**< after a format / empty card: no statistics, settings only */
} open_seed_t;

/** @brief A command for the worker (cmd_t::type). */
typedef enum {
    CMD_OPEN,                   /**< open the database (stats_db_start()) */
    CMD_SOLVE,                  /**< log a solve */
    CMD_QUERY,                  /**< the Statistics screen's data */
    CMD_SUSPEND,                /**< close the database and unmount the card */
    CMD_SET,                    /**< store a setting */
    CMD_GAME,                   /**< write the newest saved game */
    CMD_PLAYERS,                /**< the player list */
    CMD_ADD_PLAYER,             /**< add a player */
    CMD_RENAME_PLAYER,          /**< rename a player */
    CMD_SWITCH_PLAYER,          /**< switch players */
    CMD_DELETE_PLAYER,          /**< delete a player */
    CMD_MONTH,                  /**< History: a month */
    CMD_DAY,                    /**< History: a day */
    CMD_RESET,                  /**< factory reset */
    CMD_REPLAY,                 /**< a solve with its moves */
    CMD_PLAYS,                  /**< a Gameplays page */
    CMD_GEN_ADD,                /**< store a generated puzzle (waited for) */
    CMD_GEN_GET,                /**< read a generated puzzle (waited for) */
    CMD_DAILY,                  /**< the daily puzzle's status (waited for) */
    CMD_RANK,                   /**< the daily rank (waited for) */
    CMD_BOARD,                  /**< a daily leaderboard */
    CMD_ACH,                    /**< the achievements */
    CMD_GAME_SPILL,             /**< write a game pushed out of the save slot */
    CMD_CLEAR_GAME,             /**< drop an unfinished game */
    CMD_LIST_GAMES,             /**< the unfinished games (waited for) */
    CMD_LOAD_GAME,              /**< load an unfinished game (waited for) */
    CMD_RECORDS,                /**< the personal records */
    CMD_PRANK,                  /**< head-to-head on a puzzle (waited for) */
    CMD_EXPORT,                 /**< CSV export */
    CMD_WEEK,                   /**< a weekly challenge */
    CMD_PLAYER_COLOR,           /**< set a player's colour */
    CMD_PLAYER_AVATAR,          /**< set a player's avatar */
    CMD_IMPORT,                 /**< import puzzles/ */
    CMD_IMPORT_FILE,            /**< import one file */
    CMD_LIST_IMPORTS,           /**< the imported collections (waited for) */
    CMD_SOLVED_IN,              /**< solved puzzles in a range (waited for) */
    CMD_GEN_SOURCE,             /**< an imported puzzle's collection (waited for) */
    CMD_LEARNED,                /**< a Learn practice finished */
    CMD_INSIGHTS,               /**< the mistake insights */
    CMD_WEEKSUM,                /**< the week's summary */
    CMD_BM_GET,                 /**< is a puzzle bookmarked (waited for) */
    CMD_BM_SET,                 /**< set or clear a bookmark */
    CMD_BM_LIST,                /**< the bookmarks (waited for) */
    CMD_SEEN_GET,               /**< has something been seen (waited for) */
    CMD_SEEN_SET,               /**< mark something seen */
    CMD_ATTACK,                 /**< log a time-attack run */
    CMD_ATTACK_BOARD,           /**< the time-attack leaderboard (waited for) */
    CMD_SAVED_LIST,             /**< the saved collections (waited for) */
    CMD_SAVED_ADD,              /**< add to a saved collection (waited for) */
    CMD_SAVED_REMOVE,           /**< take out of a saved collection (waited for) */
    CMD_SOLVED_LIST,            /**< solved puzzles in a list (waited for) */
    CMD_SAVED_EXPORT,           /**< export a saved collection (waited for) */
    CMD_COLL_DELETE,            /**< delete a collection (waited for) */
    CMD_BACKUP,                 /**< back up the database */
    CMD_SQL,                    /**< devctl's SQL (waited for) */
} cmd_type_t;

/**
 * @brief CMD_GAME_SPILL: a game whose last save was still waiting when another game was saved
 *        (the slot holds one game): written on its own (freed by the worker).
 */
typedef struct {
    saved_game_t g;             /**< the game */
    int n;                      /**< its moves */
    game_move_t moves[];        /**< the move log */
} game_spill_t;

/** @brief A command for the worker: its type and arguments (copied into the queue). */
typedef struct {
    cmd_type_t type;            /**< what to do */
    uint16_t puzzle;            /**< the puzzle (CMD_SOLVE, CMD_CLEAR_GAME, the bookmark commands) */
    uint32_t seconds;           /**< CMD_SOLVE: the time taken */
    uint32_t mistakes;          /**< CMD_SOLVE, CMD_ATTACK: mistakes made */
    uint32_t hints;             /**< CMD_SOLVE: cells filled with Fill */
    uint32_t flags;             /**< CMD_SOLVE: GAME_FLAG_* */
    int64_t when;               /**< CMD_SOLVE: finished, Unix time, 0 = unknown */
    int64_t started;            /**< CMD_SOLVE: started, Unix time, 0 = unknown */
    uint8_t *blob;              /**< CMD_SOLVE: packed move log; CMD_IMPORT_FILE: the path (malloc'd, freed by the worker) */
    int blob_bytes;             /**< its size */
    int64_t solve_id;           /**< CMD_REPLAY */
    int level;                  /**< CMD_QUERY */
    int year;                   /**< CMD_MONTH, CMD_DAY */
    int month;                  /**< CMD_MONTH, CMD_DAY */
    int day;                    /**< CMD_DAY */
    open_seed_t *seed;          /**< CMD_OPEN */
    char key[24];               /**< CMD_SET */
    bool is_text;               /**< CMD_SET: text holds the value (else ival) */
    int64_t ival;               /**< CMD_SET value; player id for the player commands; CMD_IMPORT_FILE: its ticket */
    char text[STORE_TZ_LEN];    /**< CMD_SET text; player name */
    game_spill_t *spill;        /**< CMD_GAME_SPILL */
} cmd_t;

/**
 * @brief CMD_GEN_ADD / CMD_GEN_GET and the other waited-for commands: one call at a time, from
 *        the UI task, which waits (sdb_gen_call()); arguments in, results out.
 */
typedef struct {
    int level;                   /**< gen_level_t */
    int id;                      /**< generated.id (or a collection's id) */
    int variant;                 /**< sudoku_variant_t */
    uint8_t givens[81];          /**< the givens */
    uint8_t regions[81];         /**< a Jigsaw puzzle's regions */
    bool has_regions;            /**< a Jigsaw puzzle: regions is its map */
    char cages[STATS_CAGES_LEN]; /**< a Killer puzzle's cages, "" = none */
    char marks[82];              /**< an Even-Odd / Consecutive puzzle's marks, "" = none */
    char thermos[STATS_THERMOS_LEN];   /**< a Thermo puzzle's thermometers, "" = none */
    char own[STATS_IMPORT_NAME]; /**< CMD_GEN_ADD: its source ("My puzzles", "Received"), numbered within it; "" none */
    char day[12];                /**< daily: 'YYYY-MM-DD', "" otherwise */
    bool ok;                     /**< the call worked */
    bool solved_today;           /**< CMD_DAILY result */
    int streak;                  /**< CMD_DAILY result */
    int rank;                    /**< CMD_RANK result */
    int of;                      /**< CMD_RANK result */
    uint16_t puzzle;             /**< CMD_LOAD_GAME: which one */
    saved_game_t game;           /**< CMD_LOAD_GAME result */
    stats_h2h_t h2h;             /**< CMD_PRANK result */
    int nimports;                /**< CMD_LIST_IMPORTS result */
    stats_import_t imports[STATS_MAX_IMPORTS];   /**< CMD_LIST_IMPORTS result */
    int count;                   /**< CMD_SOLVED_IN: puzzle .. puzzle + count - 1 */
    int nsolved;                 /**< CMD_SOLVED_IN, CMD_SOLVED_LIST result */
    uint8_t solved_bits[16];     /**< CMD_SOLVED_IN, CMD_SOLVED_LIST result */
    char source[STATS_IMPORT_NAME];   /**< CMD_GEN_SOURCE result */
    int number;                  /**< CMD_GEN_SOURCE result; CMD_GEN_ADD's number in own */
    int nids;                    /**< CMD_BM_LIST result */
    uint16_t ids[STATS_MAX_BOOKMARKS];   /**< CMD_BM_LIST result */
    bool flag;                   /**< CMD_BM_GET / CMD_SEEN_GET result */
    char what[16];               /**< CMD_SEEN_GET */
    int nattack;                 /**< CMD_ATTACK_BOARD result */
    stats_attack_row_t attack[STATS_ATTACK_ROWS];   /**< CMD_ATTACK_BOARD result */
    int ngames;                  /**< CMD_LIST_GAMES result */
    stats_game_row_t games[STATS_MAX_GAMES];   /**< CMD_LIST_GAMES result */
    int nsaved;                  /**< CMD_SAVED_LIST result */
    stats_saved_t saved[STATS_MAX_SAVED];   /**< CMD_SAVED_LIST result */
    char name[STATS_IMPORT_NAME];     /**< CMD_SAVED_ADD: the collection (puzzle: the puzzle) */
    char why[96];                     /**< CMD_SAVED_ADD: why not */
    int nlist;                   /**< CMD_SOLVED_LIST: list[0..nlist-1] -> solved_bits, nsolved */
    uint16_t list[128];          /**< CMD_SOLVED_LIST: the puzzles */
} gen_call_t;

/** A player's colour in SQL: the one chosen, else one by id (p: the players table's alias). */
#define PLAYER_COLOR(p) "COALESCE(" p ".color, (" p ".id - 1) % 8)"

/** Every dated event, as SQL: solves by when they finished, unfinished games by when they
 *  started. t = Unix time, local day as 'YYYY-MM-DD'. */
#define HISTORY_EVENTS \
    "SELECT s.finished_at AS t, s.started_at AS started, s.finished_at AS finished, p.name AS player," \
    "       s.puzzle, s.seconds, s.mistakes, 0 AS in_progress, s.hints, s.id AS solve_id," \
    "       s.moves IS NOT NULL AS has_moves, s.flags" \
    "  FROM solves s JOIN players p ON p.id = s.player_id WHERE s.finished_at IS NOT NULL" \
    " UNION ALL " \
    "SELECT g.started_at, g.started_at, NULL, p.name, g.puzzle, g.elapsed_ms / 1000, g.mistakes, 1, g.hints, 0, 0," \
    "       g.flags" \
    "  FROM saved_game g JOIN players p ON p.id = g.player_id WHERE g.started_at IS NOT NULL"

/** SQL: puzzles made on the board of a variant (daily ones included: they can be any variant). */
#define OF_VARIANT(v) "puzzle IN (SELECT 2000 + id FROM generated WHERE variant = " #v ")"

/** @brief An achievement's definition (the list: stats_db_achievements.c). */
typedef struct {
    const char *key;                    /**< achievements.key */
    const char *title;                  /**< shown */
    const char *desc;                   /**< shown */
    int32_t goal;                       /**< the value to reach */
    uint8_t unit;                       /**< ach_unit_t */
    const char *sql;                    /**< one number; NULL = longest daily streak */
} ach_def_t;

#define IMPORT_DIR BSP_SD_MOUNT_POINT "/puzzles"   /**< where imports come from */

/**
 * .sdx: a collection with any variant, one puzzle per line, fields split by '|' (the variants' own
 * texts use ';', ',' and ':', never '|'):
 *
 * @verbatim
   variant|level|givens|regions|cages|marks|thermos
   @endverbatim
 *
 * variant: its name ("Classic", "X-Sudoku", "Killer", ...: variant_name()) or number; level:
 * "Easy", "Medium", "Hard", "Expert" or 0-3; givens: 81 digits row by row, 0 or '.' empty (Mini
 * 6x6: the top-left 6 x 6; Kids 4x4: the top-left 4 x 4); regions: Jigsaw's 81 digits 0-8; cages,
 * marks, thermos: as in the generated table (sudoku_cages_text(), sudoku_marks_text() (Sandwich:
 * its 18 sums), sudoku_thermos_text()); the fields a variant doesn't use are empty, trailing ones
 * may be left out. '#' starts a comment line. A line of just 81 digits is a Classic puzzle, so a
 * plain .sdm reads as .sdx too. This is the header line an exported file starts with.
 */
#define SDX_HEADER "# esp32p4_sudoku collection: variant|level|givens|regions|cages|marks|thermos"

/** The call the worker is answering (the worker fills it, sdb_gen_reply() wakes the caller). */
#define s_gen (*sdb_gen())

/* ---- stats_db.c */
/**
 * @brief The database.
 * @return its handle (NULL: not open)
 */
sqlite3 *sdb_db(void);
/**
 * @brief The lock on the results waiting to be taken.
 * @return it (NULL: not started)
 */
SemaphoreHandle_t sdb_lock(void);
/**
 * @brief Who is playing.
 * @return players.id (0: nobody yet)
 */
int32_t sdb_player(void);
/**
 * @brief Sets who is playing (the worker only).
 * @param[in] id  players.id
 */
void sdb_set_player(int32_t id);
/**
 * @brief Sets the solves counted.
 * @param[in] n  all players' solves; -1 unknown
 */
void sdb_set_solves(int n);
/** @brief Counts one more solve (if the count is known). */
void sdb_solve_counted(void);
/**
 * @brief Whether the worker has been started (stats_db_start()).
 * @return true: its queue exists
 */
bool is_sdb_started(void);
/**
 * @brief The call to the worker that is waited for (s_gen).
 * @return it (NULL: not started)
 */
gen_call_t *sdb_gen(void);
/** @brief Wakes the caller waiting on s_gen: the worker has answered. */
void sdb_gen_reply(void);
/**
 * @brief Runs SQL without results; an error is logged.
 * @param[in] sql  one or more statements
 * @return false on an error
 */
bool sdb_exec(const char *sql);
/**
 * @brief Runs a waited-for command (arguments and results in s_gen) and waits for the worker (up
 *        to 3 s).
 * @param[in] type  the command
 * @return false: no database, timed out, or the call failed (s_gen.ok)
 */
bool sdb_gen_call(cmd_type_t type);
/**
 * @brief Leaves a message for the user (stats_db_take_message()).
 * @param[in] msg  the message
 */
void sdb_post_message(const char *msg);
/**
 * @brief A result buffer in PSRAM (internal RAM is kept for Wi-Fi and the video encoder), made the
 *        first time.
 * @param[in,out] p     where the buffer is kept
 * @param[in] size  its size
 * @return it, or NULL if out of memory
 */
void *sdb_psram_buf(void **p, size_t size);
/**
 * @brief One number from a query.
 * @param[in] sql  the query
 * @return the first column of the first row; -1 for none, NULL or an error
 */
int sdb_query_int(const char *sql);
/**
 * @brief Queues a command for the worker (waits up to 100 ms for room).
 * @param[in] c  the command (copied)
 * @return false: not started, or the queue is full
 */
bool sdb_send(const cmd_t *c);
/**
 * @brief Steps a statement that returns no rows, then resets it; an error is logged.
 * @param[in] st    the statement
 * @param[in] what  what it does, for the log
 * @return false on an error
 */
bool sdb_step_done(sqlite3_stmt *st, const char *what);

/* ---- stats_db_schema.c */
/**
 * @brief Creates the tables missing and brings older ones up to date.
 * @return false on an error
 */
bool sdb_schema_create(void);

/* ---- stats_db_prefs.c */
/**
 * @brief Hands the settings read to the game (stats_db_take_prefs()).
 * @param[in] p  the settings
 */
void sdb_prefs_post(const store_prefs_t *p);
/**
 * @brief The settings: what p holds, overridden by whatever sudoku.db holds.
 * @param[in,out] p  in: the board's settings; out: the database's on top
 */
void sdb_load_prefs(store_prefs_t *p);
/**
 * @brief First use of this database: copies in the settings and game the board has.
 * @param[in] p  the board's settings
 */
void sdb_migrate_prefs(const store_prefs_t *p);
/**
 * @brief Stores a setting.
 * @param[in] key      the settings key
 * @param[in] is_text  text holds the value (else ival)
 * @param[in] ival     a number value
 * @param[in] text     a text value
 */
void sdb_set_setting(const char *key, bool is_text, int64_t ival, const char *text);

/* ---- stats_db_players.c */
/**
 * @brief The name of who is playing.
 * @return it
 */
const char *sdb_player_name(void);
/**
 * @brief CMD_ADD_PLAYER: adds a player and switches to them.
 * @param[in] raw  the name as typed
 */
void sdb_do_add(const char *raw);
/**
 * @brief CMD_DELETE_PLAYER: deletes a player with everything of theirs (not the last one).
 * @param[in] id  players.id
 */
void sdb_do_delete(int32_t id);
/** @brief CMD_PLAYERS: the player list (stats_db_take_players()). */
void sdb_do_list(void);
/**
 * @brief CMD_RENAME_PLAYER: renames a player.
 * @param[in] id   players.id
 * @param[in] raw  the name as typed
 */
void sdb_do_rename(int32_t id, const char *raw);
/**
 * @brief CMD_SWITCH_PLAYER: switches to a player, saving the game in progress for the one before.
 * @param[in] id  players.id
 */
void sdb_do_switch(int32_t id);
/**
 * @brief Picks who is playing: settings.current_player if it still exists, else the first player;
 *        a database without players gets one.
 * @param[in] name  the name for that first player (NULL: "Player 1")
 */
void sdb_select_current_player(const char *name);

/* ---- stats_db_games.c */
/** @brief Drops a save of the game that is queued and not written yet. */
void sdb_game_forget(void);
/**
 * @brief The current player's unfinished game of a puzzle is gone.
 * @param[in] puzzle  the puzzle id
 */
void sdb_clear_game(uint16_t puzzle);
/** @brief CMD_LIST_GAMES: the current player's unfinished games (s_gen.games). */
void sdb_do_list_games(void);
/**
 * @brief CMD_REPLAY: one solve's move log and details, for the Replay screen.
 * @param[in] id  solves.id
 */
void sdb_do_replay(int64_t id);
/** @brief Writes the newest queued game, if any (for the player it belongs to). */
void sdb_flush_game(void);
/**
 * @brief The current player's unfinished game played last.
 * @param[out] g  gets it; g->id = STORE_NO_GAME if none
 */
void sdb_load_game(saved_game_t *g);
/**
 * @brief The current player's unfinished game of a puzzle; its moves go to the loaded-moves
 *        buffer (stats_db_copy_saved_moves()).
 * @param[out] g       gets it; g->id = STORE_NO_GAME if none
 * @param[in] puzzle  the puzzle id (STORE_NO_GAME: the one played last)
 */
void sdb_load_game_of(saved_game_t *g, uint16_t puzzle);
/**
 * @brief Saves one of the current player's unfinished games (the newest of them now); keeps
 *        STATS_MAX_GAMES, dropping the one played longest ago.
 * @param[in] g       the game
 * @param[in] moves   its move log
 * @param[in] nmoves  its length
 */
void sdb_save_game(const saved_game_t *g, const game_move_t *moves, int nmoves);

/* ---- stats_db_solves.c */
/**
 * @brief Prepares the statements a solve uses.
 * @return false on an error
 */
bool sdb_solves_prepare(void);
/** @brief Frees those statements (before the database is closed). */
void sdb_solves_finalize(void);
/**
 * @brief CMD_QUERY: the Statistics screen's data (stats_db_take_view()).
 * @param[in] level  -1 = all levels, a collection index, or STATS_LEVEL_KIND()
 */
void sdb_do_query(int level);
/**
 * @brief CMD_SOLVE: logs a solve, updates puzzle_stats and checks the achievements.
 * @param[in] c  the command
 */
void sdb_do_solve(const cmd_t *c);
/** @brief The current player's puzzle_stats into arrays for the RAM copy (stats_db_take_cache()). */
void sdb_load_cache(void);
/**
 * @brief Merges the board's statistics (from before the database) into puzzle_stats, once.
 * @param[in] a  the board's arrays
 */
void sdb_merge_legacy(const stats_arrays_t *a);

/* ---- stats_db_kinds.c */
/**
 * @brief Binds a puzzle's id (?1), level name (?2) and number in it (?3) to a statement.
 * @param[in] st  the statement
 * @param[in] id  the puzzle id
 */
void sdb_bind_puzzle(sqlite3_stmt *st, uint16_t id);
/**
 * @brief A generated puzzle's kind and level from its level name: "X-Sudoku Hard" -> KIND_X,
 *        GEN_HARD.
 * @param[in] name   the level name
 * @param[out] kind   gets the stats_kind_t
 * @param[out] level  gets the gen_level_t
 * @return false for any other level name
 */
bool sdb_kind_level_of(const char *name, int *kind, int *level);

/* ---- stats_db_generated.c */
/** @brief CMD_DAILY: the daily puzzle of s_gen.day for the current player (stats_db_daily_status()). */
void sdb_do_daily(void);
/** @brief CMD_GEN_ADD: stores the puzzle in s_gen (stats_db_gen_add()). */
void sdb_do_gen_add(void);
/** @brief CMD_GEN_GET: reads the puzzle s_gen.id into s_gen (stats_db_gen_get()). */
void sdb_do_gen_get(void);
/**
 * @brief A generated puzzle's level, and whether it is a daily one.
 * @param[in] gid      generated.id
 * @param[out] daily    gets whether it's a daily puzzle
 * @param[out] variant  gets its sudoku_variant_t
 * @return its gen_level_t, -1 if there's no such puzzle
 */
int sdb_gen_level_of(int gid, bool *daily, int *variant);
/**
 * @brief An imported puzzle's collection and number (worker).
 * @param[in] gid     generated.id
 * @param[out] name    gets the collection's name
 * @param[out] number  gets its number in it
 * @return false if it isn't one
 */
bool sdb_gen_source_of(int gid, char name[STATS_IMPORT_NAME], int *number);

/* ---- stats_db_rank.c */
/**
 * @brief CMD_BOARD: a day's daily leaderboard (stats_db_take_board()).
 * @param[in] day  'YYYY-MM-DD'
 */
void sdb_do_board(const char *day);
/** @brief CMD_PRANK: head-to-head on s_gen.puzzle (stats_db_puzzle_rank()). */
void sdb_do_puzzle_rank(void);
/** @brief CMD_RANK: the current player's place on s_gen.day's leaderboard. */
void sdb_do_rank(void);
/**
 * @brief One number from a query on the current player (?1).
 * @param[in] sql  the query
 * @return the number; 0 for none or an error
 */
int sdb_query_int_player(const char *sql);

/* ---- stats_db_achievements.c */
/**
 * @brief An achievement's definition.
 * @param[in] i  0 .. sdb_ach_count() - 1
 * @return it
 */
const ach_def_t *sdb_ach(int i);
/**
 * @brief How many achievements there are.
 * @return at most STATS_ACH_MAX
 */
int sdb_ach_count(void);
/** @brief The achievements already earned by every player, recorded quietly (at open). */
void sdb_ach_backfill(void);
/**
 * @brief Works out a player's progress, caches it (achievement_progress) and records the
 *        achievements reached and not recorded yet.
 * @param[in] player    players.id
 * @param[in] announce  they go to stats_db_take_unlocked() and get the time now; false (first
 *                  fill): recorded quietly with no time
 */
void sdb_ach_sync(int32_t player, bool announce);
/**
 * @brief When each achievement was unlocked.
 * @param[in] player  players.id
 * @param[out] when    gets sdb_ach_count() times: Unix time, 1 = unlocked (time unknown), 0 = not yet
 */
void sdb_ach_unlocked(int32_t player, int64_t *when);
/**
 * @brief The progress of every achievement for a player.
 * @param[in] player  players.id
 * @param[out] val     gets sdb_ach_count() values
 */
void sdb_ach_values(int32_t player, int32_t *val);
/** @brief CMD_ACH: the current player's achievements (stats_db_take_achievements()). */
void sdb_do_achievements(void);
/**
 * @brief The longest run of days in a row with the daily puzzle solved.
 * @param[in] player  players.id
 * @return the days
 */
int sdb_longest_daily_streak(int32_t player);

/* ---- stats_db_import.c */
/** @brief CMD_IMPORT: imports the files in puzzles/ (stats_db_import_puzzles()). */
void sdb_do_import(void);
/**
 * @brief CMD_IMPORT_FILE: imports one file (stats_db_import_file()).
 * @param[in] path    the file (the command's own copy)
 * @param[in] ticket  its ticket, for stats_db_import_result()
 */
void sdb_do_import_file(const char *path, uint32_t ticket);
/** @brief CMD_LIST_IMPORTS: the imported collections (s_gen.imports). */
void sdb_do_list_imports(void);
/** @brief CMD_SOLVED_IN: the current player's solved puzzles in a range (s_gen.solved_bits). */
void sdb_do_solved_in(void);

/* ---- stats_db_saved.c */
/** @brief CMD_COLL_DELETE: a whole collection, saved (s_gen.id > 0) or imported (s_gen.name). */
void sdb_do_coll_delete(void);
/** @brief CMD_SAVED_ADD: puts s_gen.puzzle in the collection s_gen.name. */
void sdb_do_saved_add(void);
/** @brief CMD_SAVED_EXPORT: collection s_gen.id to puzzles/\<name\>.sdx; the result in s_gen.why. */
void sdb_do_saved_export(void);
/** @brief CMD_SAVED_LIST: the saved collections (s_gen.saved). */
void sdb_do_saved_list(void);
/** @brief CMD_SAVED_REMOVE: takes s_gen.puzzle out of collection s_gen.id. */
void sdb_do_saved_remove(void);
/** @brief CMD_SOLVED_LIST: the current player's solved puzzles in s_gen.list. */
void sdb_do_solved_list(void);

/* ---- stats_db_history.c */
/**
 * @brief CMD_DAY: who played what on a day (stats_db_take_day()).
 * @param[in] year   the year
 * @param[in] month  1..12
 * @param[in] day    1..31
 */
void sdb_do_day(int year, int month, int day);
/** @brief CMD_INSIGHTS: the mistake insights (stats_db_take_insights()). */
void sdb_do_insights(void);
/**
 * @brief CMD_MONTH: which days of a month had games (stats_db_take_month()).
 * @param[in] year   the year; 0: the most recent month that has any
 * @param[in] month  1..12
 */
void sdb_do_month(int year, int month);
/**
 * @brief CMD_PLAYS: a page of recorded solves, newest first (Gameplays).
 * @param[in] offset  the first row
 */
void sdb_do_plays(int offset);
/**
 * @brief CMD_WEEK: a weekly challenge (stats_db_take_week()).
 * @param[in] monday  its Monday, 'YYYY-MM-DD'
 */
void sdb_do_week(const char *monday);
/** @brief CMD_WEEKSUM: the current player's week (stats_db_take_weeksum()). */
void sdb_do_weeksum(void);

/* ---- stats_db_records.c */
/** @brief CMD_RECORDS: the current player's records (stats_db_take_records()). */
void sdb_do_records(void);
/**
 * @brief A player's records.
 * @param[in] player  players.id
 * @param[out] R       gets them
 */
void sdb_records_fill(int32_t player, stats_records_t *R);

/* ---- stats_db_export.c */
/** @brief CMD_BACKUP: a copy of the database in backup/ (stats_db_backup()). */
void sdb_do_backup(void);
/** @brief CMD_EXPORT: the CSV files in export/ (stats_db_export_csv()). */
void sdb_do_export(void);

/* ---- stats_db_small.c */
/**
 * @brief CMD_LEARNED: records a finished Learn practice (a technique's first may unlock an
 *        achievement).
 * @param[in] technique  Learn's list index 0..9
 */
void sdb_do_learned(int technique);
/**
 * @brief The small reads (waited for): bookmarks, seen, the time-attack board.
 * @param[in] type  CMD_BM_GET, CMD_BM_LIST, CMD_SEEN_GET or CMD_ATTACK_BOARD
 */
void sdb_do_small_get(cmd_type_t type);
/**
 * @brief The small writes: a bookmark, seen, a time-attack run.
 * @param[in] c  the command (CMD_BM_SET, CMD_SEEN_SET or CMD_ATTACK)
 */
void sdb_do_small_set(const cmd_t *c);

/* ---- stats_db_sql.c */
/** @brief CMD_SQL: runs devctl's SQL (stats_db_sql()). */
void sdb_do_sql(void);

/** @} */

/**
 * @file stats_db.h
 * @ingroup stats_db
 * @brief The game's data in a SQLite database on the microSD card (/sdcard/sudoku.db).
 */
#pragma once
/**
 * @defgroup stats_db Database
 * @brief The game's data in a SQLite database on the microSD card: /sdcard/sudoku.db.
 *
 * Statistics, the game in progress and the settings are kept here (nothing is written to NVS any
 * more, except Wi-Fi's network by wifi_mgr.cpp). The tables:
 *
 * @verbatim
   players(id, name, created_at)
       who plays; settings.current_player is the one playing now. Each
       player has their own statistics and game in progress.
   solves(id, player_id, puzzle, level, number, seconds, mistakes,
          finished_at, started_at, hints, moves)
       one row per solve; started_at = when the puzzle was opened (or
       restarted), finished_at = when it was solved; Unix time (UTC), NULL
       when the clock wasn't set (no Wi-Fi/NTP yet)
   puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)
       per puzzle, updated with every solve; fewest_mistakes NULL = solved
       before mistakes were counted
   level_stats (view): per player and level: solved / fastest / average / mistakes
   saved_game(player_id, puzzle, level, number, cells, notes, elapsed_ms,
              mistakes, saved_at, started_at, hints, moves, seq)    saved after every move
       unfinished games, up to STATS_MAX_GAMES per player (the one played
       longest ago goes); seq orders them, the highest is the one played
       last; cells = 81 digits row by row (0 = empty, givens included),
       notes = 81 x uint16 little-endian (bit d = mark d)
   generated(id, level, givens, created_at, day, variant, regions, cages, marks, thermos): puzzles
       made on the board; their puzzle id elsewhere is 2000 + id, level
       "Generated Easy", "X-Sudoku Easy", "Mini Easy", "Hyper Easy" or
       "Jigsaw Easy" etc. by variant (generator.h: 0 classic, 1 X-Sudoku,
       2 mini 6x6, 3 Hyper, 4 Jigsaw). day = the date of a daily puzzle
       (level "Daily Easy" etc.). regions = a Jigsaw puzzle's region map,
       81 digits 0-8 row by row; cages = a Killer puzzle's cages,
       "sum:cell,cell,...;sum:..." (sudoku_cages_text()); marks = an Even-Odd
       or Consecutive puzzle's marks (sudoku_marks_text()); thermos = a
       Thermo puzzle's thermometers (sudoku_thermos_text()); NULL for the others.
       A Sandwich puzzle's 18 sums are in marks (sudoku_sandwich_text()).
       Imported puzzles (puzzles/ on the card) are here too, as Classic:
       source = their collection, number = their place in it.
   imported(id, name, file, first_id, count, created_at): the collections
       imported from puzzles/ (up to 100 puzzles each; a bigger file makes
       several): generated ids first_id .. first_id + count - 1
   saved_collections(id, name, created_at) and saved_puzzles(collection_id,
       puzzle, number, added_at): collections the players make on the board
       (the game screen's Save button) from generated and typed-in puzzles;
       number = the puzzle's place in the collection, from 1. A collection
       goes when its last puzzle is taken out.
   achievements(player_id, key, unlocked_at): achievements each player has
       unlocked (see stats_db_query_achievements()); unlocked_at NULL =
       found already done when the table was first filled, or clock not set
   settings(key, value): dark_mode, sound, screen_off_minutes, brightness, volume, assist, time_zone,
       last_level, current_player, and migrated_from_nvs (set when the database is first
       filled: from what the board has, or, after a format, with just the
       current settings and game)
   @endverbatim
 *
 * The Statistics screen reads it with stats_db_query(); the other screens use the RAM copy in
 * store.h, which is merged with puzzle_stats once the database is open (stats_db_take_cache()).
 * Without a card, solves stay in RAM only.
 *
 * All database and card work runs in one worker task; calls here queue commands or pick up
 * finished results, except stats_db_suspend(), which waits. The code is split by subject:
 * stats_db_internal.h lists the files.
 * @{
 */
#include <stdbool.h>
#include <stdint.h>
#include "puzzles.h"
#include "store.h"
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATS_MAX_PUZZLES 100            /**< rows per level in a query result */
#define STATS_RECENT      40             /**< newest solves for the progress chart */

/** @brief The statistics of one level (or of all the collections, or a kind and level). */
typedef struct {
    uint16_t solved;                     /**< puzzles solved */
    uint16_t perfect;                    /**< puzzles solved without a mistake */
    uint16_t known;                      /**< puzzles with a mistake count */
    uint32_t solves;                     /**< solves logged */
    uint32_t time_sum;                   /**< their times added up, seconds */
    uint32_t fastest;                    /**< the fastest, seconds */
    uint32_t miss_sum;                   /**< their mistakes added up */
} stats_level_t;

/** @brief A puzzle's row in a level's statistics. */
typedef struct {
    uint16_t number;                     /**< 1..100 within the level */
    uint16_t best;                       /**< best time, seconds */
    uint8_t  fewest;                     /**< fewest mistakes; STORE_MISTAKES_UNKNOWN = not known */
    uint16_t solves;                     /**< times solved */
} stats_puzzle_t;

/**
 * @brief Statistics rows after the 16 collections: puzzles made on the board, by kind.
 *
 * Their solves are logged with level "\<prefix\> \<Easy|Medium|Hard|Expert\>".
 */
typedef enum {
    KIND_NEW = 0,                        /**< generated Classic puzzles */
    KIND_DAILY,                          /**< daily puzzles (any variant) */
    KIND_X,                              /**< X-Sudoku */
    KIND_HYPER,                          /**< Hyper */
    KIND_JIGSAW,                         /**< Jigsaw */
    KIND_KILLER,                         /**< Killer */
    KIND_KNIGHT,                         /**< Anti-Knight */
    KIND_EVENODD,                        /**< Even-Odd */
    KIND_CONSEC,                         /**< Consecutive */
    KIND_THERMO,                         /**< Thermo */
    KIND_SANDWICH,                       /**< Sandwich */
    KIND_MINI,                           /**< Mini 6x6 */
    KIND_KIDS,                           /**< Kids 4x4 */
    KIND_KING,                           /**< Anti-King */
    KIND_NONCONSEC,                      /**< Non-Consecutive */
    KIND_DISJOINT,                       /**< Disjoint Groups */
    KIND_ASTERISK,                       /**< Asterisk */
    KIND_KROPKI,                         /**< Kropki */
    KIND_XV,                             /**< XV */
    KIND_GT,                             /**< Greater-Than */
    KIND_QUAD,                           /**< Quadruple */
    KIND_ARROW,                          /**< Arrow */
    KIND_RENBAN,                         /**< Renban */
    KIND_WHISPER,                        /**< German Whispers */
    KIND_SKY,                            /**< Skyscraper */
    KIND_LK,                             /**< Little Killer */
    KIND_PALINDROME,                     /**< Palindrome */
    KIND_REGIONSUM,                      /**< Region Sum Lines */
    KIND_XSUM,                           /**< X-Sums */
    KIND_ZIPPER,                         /**< Zipper Lines */
    KIND_FRAME,                          /**< Frame */
    KIND_BETWEEN,                        /**< Between Lines */
    KIND_ENTROPIC,                       /**< Entropic Lines */
    KIND_MODULAR,                        /**< Modular Lines */
    KIND_CENTERDOT,                      /**< Center Dot */
    KIND_OUTSIDE,                        /**< Outside Sudoku */
    KIND_ROSSINI,                        /**< Rossini */
    KIND_DIAGNC,                         /**< Diagonal Non-Consecutive */
    KIND_DUTCH,                          /**< Dutch Whispers */
    KIND_PARITY,                         /**< Parity Lines */
    KIND_SLOW,                           /**< Slow Thermo */
    KIND_FORTRESS,                       /**< Fortress */
    KIND_ARGYLE,                         /**< Argyle */
    KIND_NABNER,                         /**< Nabner */
    KIND_NROOMS,                         /**< Numbered Rooms */
    KIND_SKYSUM,                         /**< Sum Skyscrapers */
    KIND_LOCKOUT,                        /**< Lockout Lines */
    KIND_SUMLESS,                        /**< Sumless Killer */
    KIND_DARROW,                         /**< Double Arrow */
    KIND_DIAGCONSEC,                     /**< Diagonal Consecutive */
    KIND_TEN,                            /**< 10 Lines */
    KIND_GIRANDOLA,                      /**< Girandola */
    KIND_159,                            /**< 159 Sudoku */
    KIND_PRODUCT,                        /**< Product Killer */
    KIND_REGIONS,                        /**< Extra Regions */
    KIND_ANTIDIAG,                       /**< Anti-Diagonal */
    KIND_PILL,                           /**< Pill Arrows */
    KIND_PSKY,                           /**< Odd/Even Skyscrapers */
    KIND_CHESS,                          /**< Chess Sudoku */
    KIND_MAGIC,                          /**< Magic Square */
    KIND_AVG,                            /**< Average Lines */
    KIND_FLATMATES,                      /**< Dutch Flatmates */
    KIND_MATHRAX,                        /**< Mathrax */
    KIND_PSYM,                           /**< Point Symmetry */
    KIND_B9,                             /**< Before Nine */
    KIND_QUADRO,                         /**< Quadro */
    KIND_AQ9,                            /**< Anti-Queen 9s */
    KIND_N9,                             /**< Next to Nine */
    KIND_CLONE,                          /**< Clone Sudoku */
    KIND_S9,                             /**< Search Nine */
    KIND_BF,                             /**< Battlefield */
    KIND_COUNT                           /**< how many kinds */
} stats_kind_t;
/**
 * @brief A kind's name on the Statistics screen.
 * @param[in] kind  a stats_kind_t
 * @return "New puzzles", "Daily puzzle", "X-Sudoku", ...
 */
const char *stats_kind_name(int kind);
/**
 * @brief A kind's prefix in level names ("X-Sudoku Hard").
 * @param[in] kind  a stats_kind_t
 * @return "Generated", "Daily", "X-Sudoku", ...
 */
const char *stats_kind_prefix(int kind);
/**
 * @brief The kind of a generated puzzle of a variant.
 * @param[in] variant  a sudoku_variant_t
 * @param[in] daily    it's a daily puzzle
 * @return its stats_kind_t (daily ones: KIND_DAILY)
 */
int stats_kind_of(int variant, bool daily);
#define STATS_LEVEL_KIND(k) (PUZZLE_COLLECTION_COUNT + (k))   /**< stats_db_query() level of a kind */

/** @brief The Statistics screen's data: what stats_db_query() asks for. */
typedef struct {
    int level;                           /**< -1 = all levels, collection index, or STATS_LEVEL_KIND() */
    int32_t player_id;                   /**< whose statistics these are */
    stats_level_t levels[PUZZLE_COLLECTION_COUNT];   /**< each collection */
    stats_level_t total;                 /**< the collections */
    stats_level_t kinds[KIND_COUNT][GEN_LEVELS];   /**< puzzles made on the board, by kind and level */
    int n_puzzles;                       /**< rows in puzzles (level >= 0 only) */
    stats_puzzle_t puzzles[STATS_MAX_PUZZLES];   /**< the level's puzzles */
    /** progress chart: how many of recent[] are used */
    int n_recent;
    /** progress chart: seconds of this player's newest solves at the level (all levels: any
     *  puzzle, generated ones included), oldest first */
    uint16_t recent[STATS_RECENT];
} stats_view_t;

/**
 * @brief Mounts the card and opens (or creates) the database.
 *
 * A new database (no sudoku.db on the card yet) is a skeleton: the tables, the current settings
 * and the game in progress, no statistics. A database made by a build from before the settings
 * moved here gets the board's statistics merged in once. Then loads puzzle_stats for
 * stats_db_take_cache() and the settings for stats_db_take_prefs(). Call once the card has been
 * probed.
 */
void stats_db_start(void);
/**
 * @brief After formatting: a new, empty sudoku.db holding only the current settings and the game
 *        in progress (no statistics). Also call store_reset_stats().
 */
void stats_db_start_fresh(void);
/**
 * @brief Whether the database is open.
 * @return true: open and usable
 */
bool is_stats_db_ready(void);
/**
 * @brief Whether the worker is on a command, or has some waiting (a write, an import, an export,
 *        a backup ...).
 * @return true: busy
 */
bool is_stats_db_busy(void);
/**
 * @brief Waits until the open started by stats_db_start() has finished (or failed).
 * @param[in] timeout_ms  how long to wait at most
 * @return false: timed out
 */
bool stats_db_wait_open(uint32_t timeout_ms);
/**
 * @brief The open started by stats_db_start() is still under way (neither open nor failed yet).
 * @return true if so
 */
bool is_stats_db_opening(void);
/**
 * @brief After opening: the settings and saved game from sudoku.db, on top of what the board had
 *        (store_get_prefs()).
 * @param[out] out  gets them
 * @return true once, when they are new
 */
bool stats_db_take_prefs(store_prefs_t *out);

/**
 * @brief Stores a number setting (queued; dropped without a database).
 * @param[in] key    the settings key
 * @param[in] value  its value
 */
void stats_db_set_int(const char *key, int64_t value);
/**
 * @brief Stores a text setting (queued; dropped without a database).
 * @param[in] key    the settings key
 * @param[in] value  its value
 */
void stats_db_set_text(const char *key, const char *value);
#define STATS_MAX_GAMES 5                /**< unfinished games a player keeps */
/**
 * @brief Saves an unfinished game (g->id = the puzzle) with its move log (copied).
 *
 * Cheap to call after every move: only the newest save of a game is written, so a burst of moves
 * never queues up behind the card. A player keeps up to STATS_MAX_GAMES; saving one more drops
 * the one played longest ago (and says so through stats_db_take_message()).
 * @param[in] g       the game
 * @param[in] moves   its move log
 * @param[in] nmoves  its length (at most GAME_MOVES_MAX are kept)
 */
void stats_db_save_game(const saved_game_t *g, const game_move_t *moves, int nmoves);
/**
 * @brief The current player's unfinished game of that puzzle is gone (solved, restarted,
 *        discarded).
 * @param[in] puzzle  the puzzle id
 */
void stats_db_clear_game(uint16_t puzzle);

/** @brief An unfinished game in stats_db_list_games()'s list. */
typedef struct {
    uint16_t puzzle;                     /**< the puzzle id */
    uint32_t elapsed_ms;                 /**< time played */
    uint16_t mistakes;                   /**< mistakes made */
    uint16_t hints;                      /**< cells filled with Fill */
    uint8_t filled;                      /**< cells with a digit, givens included */
    int64_t started_at;                  /**< Unix time, 0 = unknown */
    int64_t saved_at;                    /**< Unix time, 0 = unknown */
} stats_game_row_t;
/**
 * @brief The current player's unfinished games, played last first. Waits for the database.
 * @param[out] out  gets up to STATS_MAX_GAMES rows
 * @return how many; -1 without a database
 */
int stats_db_list_games(stats_game_row_t *out);
/**
 * @brief One of them, with its move log (then store_load_game_moves() picks the log up). Waits
 *        for the database.
 * @param[in] puzzle  the puzzle id
 * @param[out] out     gets the game
 * @return false if there's no such game
 */
bool stats_db_load_game(uint16_t puzzle, saved_game_t *out);

/**
 * @brief Logs a finished puzzle (finished now) and updates its puzzle_stats row.
 * @param[in] puzzle      the puzzle id
 * @param[in] seconds     the time taken
 * @param[in] mistakes    mistakes made
 * @param[in] hints       cells filled with Fill
 * @param[in] started_at  Unix time it was opened, 0 = unknown
 * @param[in] moves       its move log
 * @param[in] nmoves      its length
 * @param[in] flags       GAME_FLAG_* (store.h)
 */
void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes, uint32_t hints,
                           int64_t started_at, const game_move_t *moves, int nmoves, uint32_t flags);
/**
 * @brief The current player's week (Monday to Sunday, local time) and the one before: [0] this
 *        week, [1] last week. Needs the clock (solves' finished_at).
 */
typedef struct {
    int n[2];                            /**< puzzles solved */
    uint32_t best[2];                    /**< best time, seconds (0: none) */
    uint32_t avg[2];                     /**< average time, seconds (0: none) */
    uint16_t mist10[2];                  /**< mistakes per puzzle, x10 */
    uint32_t hints[2];                   /**< cells filled with Fill */
} stats_weeksum_t;
/** @brief Asks for the week's summary (stats_db_take_weeksum()). */
void stats_db_query_weeksum(void);
/**
 * @brief The week's summary asked for.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_weeksum(stats_weeksum_t *out);

#define STATS_INSIGHT_SOLVES 300         /**< solves stats_insights_t looks at */
/**
 * @brief Mistake insights (Statistics): from the move logs of the current player's last
 *        STATS_INSIGHT_SOLVES solves (9x9 ones).
 *
 * A solved game's last board is its solution, so a digit placed by hand that isn't the cell's
 * last one was a mistake.
 */
typedef struct {
    int solves;                          /**< solves looked at (with a move log) */
    uint32_t placed;                     /**< digits placed by hand */
    uint32_t wrong;                      /**< of them, wrong */
    uint16_t by_digit[10];               /**< wrong placements of each digit (1..9) */
    uint16_t by_box[9];                  /**< in each 3x3 box (0 top left .. 8 bottom right) */
    uint16_t pair[10][10];               /**< [placed][belonged]: which digit went where another belongs */
} stats_insights_t;
/** @brief Asks for the mistake insights (stats_db_take_insights()). */
void stats_db_query_insights(void);
/**
 * @brief The mistake insights asked for.
 * @param[out] out  gets them
 * @return false if none arrived since the last call
 */
bool stats_db_take_insights(stats_insights_t *out);

/**
 * @brief The current player finished a Learn practice (placed the digit its technique leads to).
 *        May unlock achievements (as a solve does).
 * @param[in] technique  Learn's list index 0..9
 */
void stats_db_learned(int technique);

#define STATS_MAX_BOOKMARKS 64           /**< bookmarks a player keeps */
/**
 * @brief Whether the current player bookmarked a puzzle (the star on the game screen).
 * @param[in] puzzle  the puzzle id
 * @return true: bookmarked
 */
bool is_stats_db_bookmarked(uint16_t puzzle);
/**
 * @brief Sets or clears the current player's bookmark on a puzzle.
 * @param[in] puzzle  the puzzle id
 * @param[in] on      bookmarked or not
 */
void stats_db_set_bookmark(uint16_t puzzle, bool on);
/**
 * @brief The current player's bookmarks (Puzzles > Starred), newest first.
 * @param[out] ids  gets the puzzle ids
 * @param[in] max  their room
 * @return how many; -1 without a database
 */
int stats_db_bookmarks(uint16_t *ids, int max);

/**
 * @brief Whether the current player has seen something once.
 * @param[in] what  its name, e.g. "tour" (the game screen's tour)
 * @return true: seen
 */
bool is_stats_db_seen(const char *what);
/**
 * @brief Marks something as seen (or not) by the current player.
 * @param[in] what  its name, e.g. "tour"
 * @param[in] seen  seen or not
 */
void stats_db_set_seen(const char *what, bool seen);

#define STATS_ATTACK_ROWS 8              /**< rows on the time-attack leaderboard */
/** @brief A row of the time-attack leaderboard: a player's best run. */
typedef struct {
    char name[STORE_NAME_LEN];           /**< the player */
    int best;                            /**< their best run: Easy puzzles solved in 10 minutes */
    int runs;                            /**< runs finished */
    bool me;                             /**< the current player */
} stats_attack_row_t;
/**
 * @brief Time attack: logs a finished run (Easy puzzles solved in 10 minutes).
 * @param[in] solved    puzzles solved
 * @param[in] mistakes  mistakes made
 */
void stats_db_record_attack(int solved, int mistakes);
/**
 * @brief The time-attack leaderboard: each player's best run, best first.
 * @param[out] rows  gets the rows
 * @param[in] max   their room
 * @return how many
 */
int stats_db_attack_board(stats_attack_row_t *rows, int max);
/**
 * @brief The log sudoku.db had for the saved game it loaded last (at start-up, a player switch or
 *        stats_db_load_game()).
 * @param[out] puzzle  gets which game it belongs to
 * @param[out] out     gets the moves
 * @param[in] max     their room
 * @return how many moves
 */
int stats_db_copy_saved_moves(uint16_t *puzzle, game_move_t *out, int max);

/** @brief A solve with its move log, for the Replay screen (asked for by solves.id). */
typedef struct {
    int64_t solve_id;                    /**< solves.id */
    bool found;                          /**< false: no such solve, or no log recorded */
    uint16_t puzzle;                     /**< the puzzle id */
    uint32_t seconds;                    /**< the time taken */
    uint32_t mistakes;                   /**< mistakes made */
    uint32_t hints;                      /**< cells filled with Fill */
    int64_t finished_at;                 /**< Unix time, 0 = unknown */
    char player[STORE_NAME_LEN];         /**< who solved it */
    int n;                               /**< moves */
    game_move_t moves[GAME_MOVES_MAX];   /**< only the first n are copied */
} stats_replay_t;
/**
 * @brief Asks for a solve with its move log (stats_db_take_replay()).
 * @param[in] solve_id  solves.id
 */
void stats_db_query_replay(int64_t solve_id);
/**
 * @brief The solve asked for.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_replay(stats_replay_t *out);

/**
 * @brief Asks for the Statistics screen's data (stats_db_take_view()).
 * @param[in] level  -1 = all levels, a collection index, or STATS_LEVEL_KIND()
 */
void stats_db_query(int level);
/**
 * @brief Copies the newest query result.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_view(stats_view_t *out);
/**
 * @brief After opening: copies puzzle_stats out as PUZZLE_COUNT arrays (see
 *        store_merge_stats()).
 * @param[out] best    best times
 * @param[out] fewest  fewest mistakes
 * @param[out] solves  times solved
 * @return true once, when they are new
 */
bool stats_db_take_cache(uint16_t *best, uint8_t *fewest, uint8_t *solves);

/* ---------------------------------------------------------------- players */
#define STATS_MAX_PLAYERS 16             /**< players at most */

/** @brief A row of the player list. */
typedef struct {
    int32_t id;                          /**< players.id */
    char name[STORE_NAME_LEN];           /**< the name */
    uint8_t color;                       /**< the player's colour, 0..STATS_PLAYER_COLORS-1 */
    uint16_t solved;                     /**< puzzles solved */
    uint32_t solves;                     /**< solves logged */
    uint16_t avatar;                     /**< 1..AVATAR_COUNT (avatars.h); 0 = none */
} stats_player_t;

/** @brief Who plays now, after an add, rename or switch. */
typedef struct {
    int32_t id;                          /**< who plays now */
    char name[STORE_NAME_LEN];           /**< their name */
    bool switched;                       /**< another player: game below + stats cache */
    saved_game_t game;                   /**< their game in progress (switched only) */
} stats_player_change_t;

/** Players have a colour each (shown by their name on leaderboards); a new one gets the next one
 *  round. */
#define STATS_PLAYER_COLORS 8
/**
 * @brief Sets a player's colour.
 * @param[in] id     players.id
 * @param[in] color  0..STATS_PLAYER_COLORS-1
 */
void stats_db_set_player_color(int32_t id, int color);
/**
 * @brief Sets a player's avatar.
 * @param[in] id      players.id
 * @param[in] avatar  1..AVATAR_COUNT (avatars.h), 0 = none
 */
void stats_db_set_player_avatar(int32_t id, int avatar);
/** @brief Asks for the player list (stats_db_take_players()). */
void stats_db_list_players(void);
/**
 * @brief Adds a player and switches to them.
 *
 * Results come back through stats_db_take_player_change() (and the stats cache, the player list,
 * or stats_db_take_message() for a refusal). The game in progress is saved for the player it
 * belongs to before a switch.
 * @param[in] name  the name
 */
void stats_db_add_player(const char *name);
/**
 * @brief Renames a player (results as stats_db_add_player()).
 * @param[in] id    players.id
 * @param[in] name  the new name
 */
void stats_db_rename_player(int32_t id, const char *name);
/**
 * @brief Switches to a player (results as stats_db_add_player()).
 * @param[in] id  players.id
 */
void stats_db_switch_player(int32_t id);
/**
 * @brief Deletes a player with their solves, statistics and game in progress (not the last one).
 *        Deleting the current player switches to the first one left.
 * @param[in] id  players.id
 */
void stats_db_delete_player(int32_t id);
/**
 * @brief The newest player list.
 * @param[out] out  gets STATS_MAX_PLAYERS entries at most
 * @return how many, or -1 if none arrived since the last call
 */
int stats_db_take_players(stats_player_t *out);
/**
 * @brief Who plays now, after an add, rename or switch.
 * @param[out] out  gets it
 * @return false if nothing changed since the last call
 */
bool stats_db_take_player_change(stats_player_change_t *out);
/**
 * @brief A message for the user (e.g. "name taken").
 * @param[out] buf  gets it
 * @param[in] len  its size
 * @return false if none
 */
bool stats_db_take_message(char *buf, int len);

/* ---------------------------------------------------------------- history */
/**
 * @brief Which days of a month had games (all players).
 *
 * Days are local dates (the time zone in settings); solves finished before the clock was set have
 * no date and don't appear.
 */
typedef struct {
    int year;                            /**< 0 = nothing dated yet */
    int month;                           /**< 1..12 */
    uint8_t count[31];                   /**< solves + games started, per day (index day-1) */
} stats_month_t;

#define STATS_MAX_DAY_ROWS 40            /**< rows of a day, or of a Gameplays page */
/** @brief A game on a day: a solve, or a game in progress. */
typedef struct {
    char player[STORE_NAME_LEN];         /**< who played */
    uint16_t puzzle;                     /**< the puzzle id */
    bool in_progress;                    /**< a saved game started that day, not solved yet */
    int64_t started_at;                  /**< Unix time, 0 = unknown */
    int64_t finished_at;                 /**< Unix time, 0 = unknown */
    uint32_t seconds;                    /**< time taken (in progress: so far) */
    uint16_t mistakes;                   /**< mistakes made */
    uint16_t hints;                      /**< cells filled with Fill */
    int64_t solve_id;                    /**< solves.id, 0 for a game in progress */
    bool has_moves;                      /**< a move log was recorded: it can be replayed */
    uint32_t flags;                      /**< GAME_FLAG_* (store.h): the no-notes challenge, techniques hinted */
} stats_day_row_t;

/** @brief Who played what on a day. */
typedef struct {
    int year;                            /**< the day */
    int month;                           /**< ... */
    int day;                             /**< ... */
    int n;                               /**< rows here */
    int total;                           /**< rows that day (n <= STATS_MAX_DAY_ROWS) */
    stats_day_row_t rows[STATS_MAX_DAY_ROWS];   /**< oldest first */
} stats_day_t;

/**
 * @brief Asks which days of a month had games (stats_db_take_month()).
 * @param[in] year   the year; 0: the most recent month that has any
 * @param[in] month  1..12
 */
void stats_db_query_month(int year, int month);
/**
 * @brief The month asked for.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_month(stats_month_t *out);
/**
 * @brief Asks who played what on a day (stats_db_take_day()).
 * @param[in] year   the year
 * @param[in] month  1..12
 * @param[in] day    1..31
 */
void stats_db_query_day(int year, int month, int day);
/**
 * @brief The day asked for.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_day(stats_day_t *out);

/** @brief A page of the solves that have a move log, newest first (Gameplays screen). */
typedef struct {
    int offset;                          /**< the first row's place */
    int n;                               /**< rows here: offset..offset+n-1 */
    int total;                           /**< rows in all */
    stats_day_row_t rows[STATS_MAX_DAY_ROWS];   /**< the rows */
} stats_plays_t;
#define STATS_CAGES_LEN 1024             /**< a Killer puzzle's cages, as text */
/**
 * @brief Stores a puzzle made on the board (generator.h). Waits for the database (milliseconds).
 * @param[in] variant  a sudoku_variant_t
 * @param[in] level    a gen_level_t
 * @param[in] givens   the givens, 81 cells
 * @param[in] regions  a Jigsaw puzzle's region map (81), else NULL
 * @param[in] cages    a Killer puzzle's cages (sudoku_cages_text()), else NULL
 * @param[in] marks    the variant's marks (sudoku_marks_text()), else NULL
 * @param[in] thermos  a Thermo puzzle's thermometers (sudoku_thermos_text()), else NULL
 * @param[in] day      a daily puzzle's date, else NULL
 * @return its id in the generated table (puzzle id = PUZZLE_GEN_BASE + it), or -1 without a
 *         database
 */
int stats_db_gen_add(int variant, int level, const uint8_t givens[81], const uint8_t *regions, const char *cages,
                     const char *marks, const char *thermos, const char *day);
#define STATS_THERMOS_LEN 400            /**< a Thermo puzzle's thermometers, as text */
/**
 * @brief Reads a puzzle made on the board back. Waits for the database (milliseconds).
 * @param[in] id       its id in the generated table
 * @param[out] givens   gets the givens, 81 cells
 * @param[out] regions  (may be NULL) only filled for a Jigsaw puzzle
 * @param[out] cages    (STATS_CAGES_LEN, may be NULL) a Killer puzzle's cages ("" otherwise)
 * @param[out] marks    (82 bytes, may be NULL) the marks ("" unless a variant with marks)
 * @param[out] thermos  (STATS_THERMOS_LEN, may be NULL) the thermometers ("" unless Thermo)
 * @param[out] variant  gets its sudoku_variant_t
 * @param[out] level    gets its gen_level_t
 * @param[out] day      gets a daily puzzle's date, "" if not a daily
 * @return false if there's no such puzzle
 */
bool stats_db_gen_get(int id, uint8_t givens[81], uint8_t *regions, char *cages, char *marks, char *thermos,
                      int *variant, int *level, char day[12]);
/**
 * @brief The daily puzzle of a day. Waits for the database.
 * @param[in] day           'YYYY-MM-DD'
 * @param[out] id            gets its id in the generated table (0 = not made yet)
 * @param[out] solved_today  gets whether the current player solved it
 * @param[out] streak        gets their streak of days in a row with the daily solved (ending today, or
 *                      yesterday if not yet today)
 * @return false without a database
 */
bool stats_db_daily_status(const char *day, int *id, bool *solved_today, int *streak);
/**
 * @brief The current player's place on that day's leaderboard (best time; ties: fewer mistakes,
 *        then fewer hints). Waits for the database.
 * @param[in] day   'YYYY-MM-DD'
 * @param[out] rank  gets the place, 1..; 0 = not solved
 * @param[out] of    gets how many players solved it
 * @return false without a database
 */
bool stats_db_daily_rank(const char *day, int *rank, int *of);

/** @brief Head-to-head on a puzzle (stats_db_puzzle_rank()). */
typedef struct {
    int rank;                            /**< the current player's place (best times; 0 = not solved) */
    int of;                              /**< how many solved it */
    bool second;                         /**< the current player leads: leader is the runner-up */
    char leader[STORE_NAME_LEN];         /**< the leader ("" if nobody else solved it) */
    uint32_t leader_seconds;             /**< their best time */
} stats_h2h_t;
/**
 * @brief Head-to-head on any puzzle: the current player's place among everyone who solved it, how
 *        many did, and the leader (when the current player leads, the runner-up instead). Waits for
 *        the database.
 * @param[in] puzzle  the puzzle id
 * @param[out] out     gets it
 * @return false without a database
 */
bool stats_db_puzzle_rank(uint16_t puzzle, stats_h2h_t *out);

/** @brief An earlier solve of the same puzzle under another name (stats_db_solved_twin()). */
typedef struct {
    bool found;                          /**< there is one */
    uint16_t puzzle;                     /**< the puzzle solved then (another id) */
    int64_t finished_at;                 /**< when, Unix time UTC; 0 = unknown */
} stats_twin_t;
/**
 * @brief Has the current player solved this puzzle before under another name: another puzzle with the same
 *        givens turned, mirrored or renumbered (the same canonical hash, canon_core.h)? The latest such
 *        solve. Waits for the database.
 * @param[in] puzzle  the puzzle id
 * @param[out] out     gets the solve
 * @return true if there is one (false too without a database, or for a variant without a hash)
 */
bool stats_db_solved_twin(uint16_t puzzle, stats_twin_t *out);

/* ---------------------------------------------------------------- daily leaderboard */
/** @brief A player's row on a daily puzzle's leaderboard: their best solve of it. */
typedef struct {
    char player[STORE_NAME_LEN];         /**< the player */
    int32_t player_id;                   /**< players.id */
    bool solved;                         /**< solved it */
    uint32_t seconds;                    /**< best time */
    uint16_t mistakes;                   /**< of that solve */
    uint16_t hints;                      /**< of that solve */
    uint16_t solves;                     /**< times solved */
    uint8_t color;                       /**< the player's colour */
    uint32_t flags;                      /**< of the best solve: GAME_FLAG_* (store.h) */
} stats_board_row_t;

/**
 * @brief Everyone's best solve of one day's daily puzzle (whenever it was solved), fastest first;
 *        players who haven't solved it come last.
 */
typedef struct {
    char day[12];                        /**< 'YYYY-MM-DD' */
    bool exists;                         /**< that day's puzzle has been made (opened by someone) */
    int level;                           /**< gen_level_t of the puzzle */
    int variant;                         /**< sudoku_variant_t of the puzzle */
    int n;                               /**< rows */
    int solved;                          /**< how many of them solved it */
    stats_board_row_t rows[STATS_MAX_PLAYERS];   /**< the rows */
} stats_board_t;
/**
 * @brief Asks for a day's leaderboard (stats_db_take_board()).
 * @param[in] day  'YYYY-MM-DD'
 */
void stats_db_query_board(const char *day);
/**
 * @brief The leaderboard asked for.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_board(stats_board_t *out);

/* ---------------------------------------------------------------- imported puzzles */
#define STATS_MAX_IMPORTS 24             /**< imported collections listed at most */
#define STATS_IMPORT_NAME 28             /**< a collection's name */
/** @brief An imported collection. */
typedef struct {
    int id;                              /**< imported.id (devctl's collection id) */
    char name[STATS_IMPORT_NAME];        /**< its name */
    int first_gid;                       /**< generated.id of its first puzzle (puzzle id: + PUZZLE_GEN_BASE) */
    int count;                           /**< its puzzles */
    int64_t created_at;                  /**< when it was imported, Unix time UTC; 0 = not known (older ones) */
} stats_import_t;
/**
 * @brief Imports the files in puzzles/ on the card.
 *
 * .opensudoku (OpenSudoku's XML), .sdm / .txt (a puzzle per line, 81 digits, 0 or . = empty) and
 * .sdx (this board's export of a saved collection: any variant; see stats_db_import.c). Each
 * becomes a collection (up to 100 puzzles; more: "name 2", "name 3" ...), checked for exactly one
 * solution and rated by the grader (generator.h) where the file gives no level (.sdm, .txt,
 * .opensudoku: always). A file already imported is skipped. The result comes back as a message;
 * stats_db_take_imports_changed() says to reload the list.
 */
void stats_db_import_puzzles(void);
/**
 * @brief Imports one file of those kinds anywhere on the card: the same, but just that file
 *        (Files' preview).
 * @param[in] path  a full path
 * @return its ticket, for stats_db_import_result() (0: not started: no database)
 */
uint32_t stats_db_import_file(const char *path);
/** @brief How a stats_db_import_file() went (stats_db_import_result()). */
typedef struct {
    uint32_t ticket;                     /**< the import's ticket (stats_db_import_file()'s answer) */
    int got;                             /**< puzzles imported; 0: none had one solution; -1: it couldn't be
                                          *   (a collection of that name?); -2: that file is imported already */
    int bad;                             /**< puzzles left out (not exactly one solution) */
    char msg[128];                       /**< what happened, as the notice on the board says it */
} stats_import_result_t;
/**
 * @brief A file import's result, once it has finished (devctl "import" waits for it). The last few
 *        are kept, so two imports at once (one from devctl, one from the screen) each find their own.
 * @param[in] ticket  stats_db_import_file()'s answer
 * @param[out] out  gets it
 * @return false: not finished yet (or long gone)
 */
bool stats_db_import_result(uint32_t ticket, stats_import_result_t *out);
/**
 * @brief Whether an import changed the collections (once).
 * @return true: reload the list
 */
bool stats_db_take_imports_changed(void);
/**
 * @brief The imported collections (waits for the database).
 * @param[out] out  gets STATS_MAX_IMPORTS at most
 * @return how many; -1 without a database
 */
int stats_db_list_imports(stats_import_t *out);

#define STATS_MAX_GEN_ROWS 4000           /**< stats_db_gen_rows(): puzzles made on the board or imported, at most */
/** @brief A puzzle made on the board or imported, with what its name is made of (stats_db_gen_rows()). */
typedef struct {
    uint16_t id;                         /**< its puzzle id (PUZZLE_GEN_BASE + generated.id) */
    uint8_t variant;                     /**< sudoku_variant_t */
    int8_t level;                        /**< gen_level_t, -1 unknown */
    int number;                          /**< its number in its source (imported, typed in); 0 none */
    char source[STATS_IMPORT_NAME];      /**< its collection (imported) or own source ("My puzzles"); "" none */
    char day[11];                        /**< a daily puzzle's date, 'YYYY-MM-DD'; "" otherwise */
} stats_gen_row_t;
/**
 * @brief Every puzzle made on the board or imported (newest first, STATS_MAX_GEN_ROWS at most), for the
 *        Puzzles screen's search. Waits for the database.
 * @param[out] rows  gets them (the database's buffer: good until the next call)
 * @return how many, -1 without a database
 */
int stats_db_gen_rows(const stats_gen_row_t **rows);
/**
 * @brief The current player's solved puzzles among ids first .. first + count - 1. Waits for the
 *        database.
 * @param[in] first   the first puzzle id
 * @param[in] count   how many
 * @param[out] solved  gets bit i set for each solved (count bits)
 * @return how many are solved
 */
int stats_db_solved_in(uint16_t first, int count, uint8_t *solved);
#define STATS_OWN_SOURCE "My puzzles"    /**< the collection puzzles typed in go to */
/**
 * @brief Stores a puzzle typed in on the board (Classic), like an imported one, in the collection
 *        STATS_OWN_SOURCE, numbered from 1.
 * @param[in] givens  the givens, 81 cells
 * @param[in] level   a gen_level_t
 * @param[out] number  gets its number in the collection
 * @return its generated.id, or -1
 */
int stats_db_gen_add_own(const uint8_t givens[81], int level, int *number);
/**
 * @brief The same for any variant, kept under source (numbered within it: "Received #3").
 * @param[in] source   the collection
 * @param[in] variant  a sudoku_variant_t
 * @param[in] level    a gen_level_t
 * @param[in] givens   the givens, 81 cells
 * @param[in] regions  a Jigsaw puzzle's region map, else NULL
 * @param[in] cages    a Killer puzzle's cages, else NULL
 * @param[in] marks    the variant's marks, else NULL
 * @param[in] thermos  a Thermo puzzle's thermometers, else NULL
 * @param[out] number   gets its number in the collection
 * @return its generated.id, or -1
 */
int stats_db_gen_add_from(const char *source, int variant, int level, const uint8_t givens[81],
                          const uint8_t *regions, const char *cages, const char *marks, const char *thermos, int *number);
/**
 * @brief An imported puzzle's collection and number.
 * @param[in] gid     its generated.id
 * @param[out] name    gets the collection's name
 * @param[out] number  gets its number in it
 * @return false if it isn't one
 */
bool stats_db_gen_source(int gid, char name[STATS_IMPORT_NAME], int *number);

/* ---------------------------------------------------------------- saved collections */
#define STATS_MAX_SAVED   16             /**< collections */
#define STATS_SAVED_MAX   100            /**< puzzles in one */
/**
 * @brief A collection made on the board: the game screen's Save puts the puzzle shown (one made
 *        on the board: generated or typed in) in a collection, by name.
 */
typedef struct {
    int id;                              /**< saved_collections.id */
    char name[STATS_IMPORT_NAME];        /**< its name */
    int count;                           /**< its puzzles */
    int64_t created_at;                  /**< when it was made, Unix time UTC; 0 = not known (older ones) */
    uint16_t puzzles[STATS_SAVED_MAX];   /**< in their order (number 1, 2, ...) */
} stats_saved_t;
/**
 * @brief All of them, oldest first (waits for the database).
 * @param[out] out  gets STATS_MAX_SAVED at most
 * @return how many; -1 without a database
 */
int stats_db_list_saved(stats_saved_t *out);
/**
 * @brief Puts a puzzle at the end of the collection called name, made if there's none (waits).
 * @param[in] name    the collection
 * @param[in] puzzle  the puzzle id
 * @param[out] why     gets why not, on false (full, too many collections, no database)
 * @param[in] why_n   its size
 * @return false: not added
 */
bool stats_db_saved_add(const char *name, uint16_t puzzle, char *why, size_t why_n);
/**
 * @brief Takes a puzzle out of a collection; the collection goes if that was its last (waits).
 * @param[in] collection  saved_collections.id
 * @param[in] puzzle      the puzzle id
 * @return false on an error
 */
bool stats_db_saved_remove(int collection, uint16_t puzzle);
/**
 * @brief Deletes a whole saved collection. Its puzzles stay on the board (solves, History,
 *        Gameplays keep them). Waits.
 * @param[in] collection  saved_collections.id
 * @return false on an error
 */
bool stats_db_saved_delete(int collection);
/**
 * @brief Deletes an imported collection (its file may be imported again). Its puzzles stay on the
 *        board. Waits.
 * @param[in] name  the collection's name
 * @return false on an error
 */
bool stats_db_import_delete(const char *name);
/**
 * @brief Writes a collection to the card as puzzles/\<its name\>.sdx (the format Import puzzles
 *        reads back, variants and all: see stats_db_import.c). Waits.
 * @param[in] collection  saved_collections.id
 * @param[out] msg         gets what happened
 * @param[in] msg_n       its size
 * @return false on an error
 */
bool stats_db_saved_export(int collection, char *msg, size_t msg_n);
/**
 * @brief The current player's solved puzzles among ids[0..n-1]. Waits for the database.
 * @param[in] ids     the puzzle ids
 * @param[in] n       how many (at most 128)
 * @param[out] solved  gets bit i set for each solved
 * @return how many are solved
 */
int stats_db_solved_list(const uint16_t *ids, int n, uint8_t *solved);

/* ---------------------------------------------------------------- weekly challenge */
/** @brief A player's row in the weekly challenge. */
typedef struct {
    char player[STORE_NAME_LEN];         /**< the player */
    int32_t player_id;                   /**< players.id */
    uint8_t color;                       /**< the player's colour */
    uint8_t days;                        /**< daily puzzles of the week solved */
    uint32_t total;                      /**< their best times added up, seconds */
    uint16_t best[7];                    /**< best time per day (Monday first), 0 = not solved */
} stats_week_row_t;
/**
 * @brief The daily puzzles of a week (Monday to Sunday) added up: per player, how many of the 7
 *        they solved and the sum of their best times on those; ranked by days solved (more
 *        first), then total time.
 */
typedef struct {
    char monday[12];                     /**< 'YYYY-MM-DD' */
    int n;                               /**< rows */
    stats_week_row_t rows[STATS_MAX_PLAYERS];   /**< the rows */
} stats_week_t;
/**
 * @brief Asks for a week (stats_db_take_week()).
 * @param[in] monday  its Monday, 'YYYY-MM-DD'
 */
void stats_db_query_week(const char *monday);
/**
 * @brief The week asked for.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_week(stats_week_t *out);

/* ---------------------------------------------------------------- achievements */
/** A fixed list (stats_db_achievements.c), each with the player's progress toward it. They are
 *  checked after every solve; newly unlocked ones come back through stats_db_take_unlocked().
 *  Achievements already earned when this firmware first opens a database are recorded quietly.
 *  This is the list's room. */
#define STATS_ACH_MAX 40

/** @brief How an achievement's value and goal are shown. */
typedef enum {
    ACH_UNIT_COUNT = 0,                  /**< a count */
    ACH_UNIT_MINUTES,                    /**< minutes */
    ACH_UNIT_DAYS                        /**< days */
} ach_unit_t;

/** @brief An achievement, with the player's progress. */
typedef struct {
    const char *title;                   /**< static string */
    const char *desc;                    /**< static string */
    int32_t value;                       /**< progress (capped at goal) */
    int32_t goal;                        /**< the goal */
    uint8_t unit;                        /**< ach_unit_t: how to show value / goal */
    bool unlocked;                       /**< unlocked */
    int64_t unlocked_at;                 /**< Unix time, 0 = unknown */
} stats_ach_t;

/** @brief A player's achievements. */
typedef struct {
    int32_t player_id;                   /**< players.id */
    int n;                               /**< achievements */
    int unlocked;                        /**< of them, unlocked */
    stats_ach_t a[STATS_ACH_MAX];        /**< the list */
} stats_achievements_t;

/** @brief Asks for the current player's achievements (stats_db_take_achievements()). */
void stats_db_query_achievements(void);
/**
 * @brief The achievements asked for.
 * @param[out] out  gets them
 * @return false if none arrived since the last call
 */
bool stats_db_take_achievements(stats_achievements_t *out);
/**
 * @brief Titles of achievements the last solve unlocked, comma-separated.
 * @param[out] buf    gets them
 * @param[in] len    its size
 * @param[out] count  gets how many
 * @return false if none
 */
bool stats_db_take_unlocked(char *buf, int len, int *count);

/* ---------------------------------------------------------------- personal records */
/** @brief How a record's value is shown. */
typedef enum {
    REC_TIME = 0,                        /**< a time, seconds */
    REC_COUNT,                           /**< a count */
    REC_DAYS,                            /**< days */
    REC_MINUTES                          /**< minutes */
} rec_unit_t;
#define STATS_MAX_RECORDS 96             /**< records at most (each holder is in PSRAM or static: room for more variants) */
/** @brief A personal record. */
typedef struct {
    const char *title;                   /**< static string */
    uint8_t unit;                        /**< rec_unit_t */
    bool has;                            /**< false: nothing yet */
    int32_t value;                       /**< seconds, a count, days or minutes */
    uint16_t puzzle;                     /**< the puzzle it was set on, STORE_NO_GAME = none */
    int64_t when;                        /**< when it was set (Unix time), 0 = unknown */
} stats_record_t;
/** @brief The current player's bests (Trophies > Records). */
typedef struct {
    int32_t player_id;                   /**< players.id */
    int n;                               /**< records */
    stats_record_t r[STATS_MAX_RECORDS]; /**< the list */
} stats_records_t;
/** @brief Asks for the current player's records (stats_db_take_records()). */
void stats_db_query_records(void);
/**
 * @brief The records asked for.
 * @param[out] out  gets them
 * @return false if none arrived since the last call
 */
bool stats_db_take_records(stats_records_t *out);

/* ---------------------------------------------------------------- CSV export */
/**
 * @brief Writes export/solves.csv, export/achievements.csv and export/records.csv on the card
 *        (all players), for a spreadsheet; the result comes back as a message
 *        (stats_db_take_message()).
 */
void stats_db_export_csv(void);

/**
 * @brief A copy of sudoku.db as backup/sudoku_\<unix time\>.db on the card (consistent while in
 *        use); the result comes back as a message.
 */
void stats_db_backup(void);

/* ---------------------------------------------------------------- SQL (devctl) */
/** @brief What stats_db_sql() did. */
typedef struct {
    bool ok;                            /**< no error */
    int rows;                           /**< rows returned, all statements */
    int changes;                        /**< rows inserted, updated or deleted */
    bool truncated;                     /**< out was too small for all of it */
    char err[160];                      /**< !ok: SQLite's message, or what else went wrong */
} stats_sql_result_t;
/**
 * @brief Runs SQL (one or more statements, any kind) on sudoku.db, on the database's own task, and
 *        waits for it (a statement runs 20 s at most). For devctl's sql command; not for the UI.
 *
 * out gets, per statement with columns, a "C" line with the names and an "R" line per row:
 * @verbatim
   C<tab>name<tab>name...
   R<tab>value<tab>value...
   @endverbatim
 * Values are escaped (backslash, tab, newline, carriage return), NULL is \\N and a blob x'hex'.
 * A transaction left open is rolled back (an error). Changes bypass the RAM copies the screens
 * use: they show after a restart.
 * @param[in] sql  the statements
 * @param[out] out  gets the result lines
 * @param[in] cap  its size
 * @param[out] r    gets what happened
 * @return false on an error (r->err says)
 */
bool stats_db_sql(const char *sql, char *out, size_t cap, stats_sql_result_t *r);

/**
 * @brief Asks for a page of Gameplays (stats_db_take_plays()).
 * @param[in] offset  the first row
 */
void stats_db_query_plays(int offset);
/**
 * @brief The page asked for.
 * @param[out] out  gets it
 * @return false if none arrived since the last call
 */
bool stats_db_take_plays(stats_plays_t *out);

/**
 * @brief Deletes sudoku.db and builds a new skeleton with what store.h holds now (call
 *        store_factory_reset() first). The result comes back like an open: stats_db_take_prefs().
 */
void stats_db_factory_reset(void);

/**
 * @brief Closes the database and unmounts the card (before formatting); waits up to a few
 *        seconds. stats_db_start_fresh() mounts and starts a new one.
 */
void stats_db_suspend(void);

/**
 * @brief One line for Settings, e.g. "Stats in sudoku.db: 12 solves".
 * @param[out] buf  gets it
 * @param[in] len  its size
 */
void stats_db_status(char *buf, int len);

#ifdef __cplusplus
}
#endif

/** @} */

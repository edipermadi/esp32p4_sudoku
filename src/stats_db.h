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
 *              mistakes, saved_at, started_at, hints, moves, seq)    saved after every move
 *       unfinished games, up to STATS_MAX_GAMES per player (the one played
 *       longest ago goes); seq orders them, the highest is the one played
 *       last; cells = 81 digits row by row (0 = empty, givens included),
 *       notes = 81 x uint16 little-endian (bit d = mark d)
 *   generated(id, level, givens, created_at, day, variant, regions, cages, marks, thermos): puzzles
 *       made on the board; their puzzle id elsewhere is 2000 + id, level
 *       "Generated Easy", "X-Sudoku Easy", "Mini Easy", "Hyper Easy" or
 *       "Jigsaw Easy" etc. by variant (generator.h: 0 classic, 1 X-Sudoku,
 *       2 mini 6x6, 3 Hyper, 4 Jigsaw). day = the date of a daily puzzle
 *       (level "Daily Easy" etc.). regions = a Jigsaw puzzle's region map,
 *       81 digits 0-8 row by row; cages = a Killer puzzle's cages,
 *       "sum:cell,cell,...;sum:..." (sudoku_cages_text()); marks = an Even-Odd
 *       or Consecutive puzzle's marks (sudoku_marks_text()); thermos = a
 *       Thermo puzzle's thermometers (sudoku_thermos_text()); NULL for the others.
 *       A Sandwich puzzle's 18 sums are in marks (sudoku_sandwich_text()).
 *       Imported puzzles (puzzles/ on the card) are here too, as Classic:
 *       source = their collection, number = their place in it.
 *   imported(id, name, file, first_id, count, created_at): the collections
 *       imported from puzzles/ (up to 100 puzzles each; a bigger file makes
 *       several): generated ids first_id .. first_id + count - 1
 *   saved_collections(id, name, created_at) and saved_puzzles(collection_id,
 *       puzzle, number, added_at): collections the players make on the board
 *       (the game screen's Save button) from generated and typed-in puzzles;
 *       number = the puzzle's place in the collection, from 1. A collection
 *       goes when its last puzzle is taken out.
 *   achievements(player_id, key, unlocked_at): achievements each player has
 *       unlocked (see stats_db_query_achievements()); unlocked_at NULL =
 *       found already done when the table was first filled, or clock not set
 *   settings(key, value): dark_mode, sound, screen_off_minutes, brightness, volume, assist, time_zone,
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
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATS_MAX_PUZZLES 100            /* rows per level in a query result */
#define STATS_RECENT      40             /* newest solves for the progress chart */

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

/* Statistics rows after the 16 collections: puzzles made on the board, by
 * kind. Their solves are logged with level "<prefix> <Easy|Medium|Hard|Expert>". */
typedef enum { KIND_NEW = 0, KIND_DAILY, KIND_X, KIND_HYPER, KIND_JIGSAW, KIND_KILLER, KIND_KNIGHT, KIND_EVENODD,
               KIND_CONSEC, KIND_THERMO, KIND_SANDWICH, KIND_MINI, KIND_KIDS, KIND_KING, KIND_NONCONSEC,
               KIND_DISJOINT, KIND_ASTERISK, KIND_COUNT } stats_kind_t;
const char *stats_kind_name(int kind);     /* "New puzzles", "Daily puzzle", "X-Sudoku", ... */
const char *stats_kind_prefix(int kind);   /* "Generated", "Daily", "X-Sudoku", ... */
/* the kind of a generated puzzle of a variant (daily ones: KIND_DAILY) */
int stats_kind_of(int variant, bool daily);
#define STATS_LEVEL_KIND(k) (PUZZLE_COLLECTION_COUNT + (k))   /* stats_db_query() level of a kind */

typedef struct {
    int level;                           /* -1 = all levels, collection index, or STATS_LEVEL_KIND() */
    int32_t player_id;                   /* whose statistics these are */
    stats_level_t levels[PUZZLE_COLLECTION_COUNT];
    stats_level_t total;                 /* the collections */
    stats_level_t kinds[KIND_COUNT][GEN_LEVELS];   /* puzzles made on the board, by kind and level */
    int n_puzzles;                       /* rows in puzzles (level >= 0 only) */
    stats_puzzle_t puzzles[STATS_MAX_PUZZLES];
    /* progress chart: seconds of this player's newest solves at the level
     * (all levels: any puzzle, generated ones included), oldest first */
    int n_recent;
    uint16_t recent[STATS_RECENT];
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
/* Saves an unfinished game (g->id = the puzzle) with its move log (copied).
 * Cheap to call after every move: only the newest save of a game is written,
 * so a burst of moves never queues up behind the card. A player keeps up to
 * STATS_MAX_GAMES; saving one more drops the one played longest ago (and
 * says so through stats_db_take_message()). */
#define STATS_MAX_GAMES 5
void stats_db_save_game(const saved_game_t *g, const game_move_t *moves, int nmoves);
/* The current player's unfinished game of that puzzle is gone (solved, restarted, discarded). */
void stats_db_clear_game(uint16_t puzzle);

/* The current player's unfinished games, played last first (up to
 * STATS_MAX_GAMES); -1 without a database. Waits for the database. */
typedef struct {
    uint16_t puzzle;
    uint32_t elapsed_ms;
    uint16_t mistakes, hints;
    uint8_t filled;                      /* cells with a digit, givens included */
    int64_t started_at, saved_at;        /* Unix time, 0 = unknown */
} stats_game_row_t;
int stats_db_list_games(stats_game_row_t *out);
/* One of them, with its move log (then store_load_game_moves() picks the log
 * up). False if there's no such game. Waits for the database. */
bool stats_db_load_game(uint16_t puzzle, saved_game_t *out);

/* Logs a finished puzzle (finished now) and updates its puzzle_stats row.
 * started_at: Unix time it was opened, 0 = unknown. */
void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes, uint32_t hints,
                           int64_t started_at, const game_move_t *moves, int nmoves, uint32_t flags);
/* The current player's week (Monday to Sunday, local time) and the one before:
 * [0] this week, [1] last week. Needs the clock (solves' finished_at). */
typedef struct {
    int n[2];                            /* puzzles solved */
    uint32_t best[2], avg[2];            /* seconds (0: none) */
    uint16_t mist10[2];                  /* mistakes per puzzle, x10 */
    uint32_t hints[2];
} stats_weeksum_t;
void stats_db_query_weeksum(void);
bool stats_db_take_weeksum(stats_weeksum_t *out);

/* Mistake insights (Statistics): from the move logs of the current player's last
 * STATS_INSIGHT_SOLVES solves (9x9 ones). A solved game's last board is its solution,
 * so a digit placed by hand that isn't the cell's last one was a mistake. */
#define STATS_INSIGHT_SOLVES 300
typedef struct {
    int solves;                          /* solves looked at (with a move log) */
    uint32_t placed, wrong;              /* digits placed by hand; of them, wrong */
    uint16_t by_digit[10];               /* wrong placements of each digit (1..9) */
    uint16_t by_box[9];                  /* in each 3x3 box (0 top left .. 8 bottom right) */
    uint16_t pair[10][10];               /* [placed][belonged]: which digit went where another belongs */
} stats_insights_t;
void stats_db_query_insights(void);
bool stats_db_take_insights(stats_insights_t *out);

/* The current player finished a Learn practice (placed the digit its technique leads
 * to): technique = Learn's list index 0..9. May unlock achievements (as a solve does). */
void stats_db_learned(int technique);

/* Bookmarks (the star on the game screen; Puzzles > Starred), per player.
 * stats_db_bookmarks: newest first, at most max; -1 without a database. */
#define STATS_MAX_BOOKMARKS 64
bool stats_db_bookmarked(uint16_t puzzle);
void stats_db_set_bookmark(uint16_t puzzle, bool on);
int stats_db_bookmarks(uint16_t *ids, int max);

/* Things the current player has seen once ("tour": the game screen's tour) */
bool stats_db_seen(const char *what);
void stats_db_set_seen(const char *what, bool seen);

/* Time attack: a finished run (Easy puzzles solved in 10 minutes), and the
 * leaderboard: each player's best run, best first */
#define STATS_ATTACK_ROWS 8
typedef struct {
    char name[STORE_NAME_LEN];
    int best, runs;
    bool me;
} stats_attack_row_t;
void stats_db_record_attack(int solved, int mistakes);
int stats_db_attack_board(stats_attack_row_t *rows, int max);
/* The log sudoku.db had for the saved game it loaded last (at start-up, a
 * player switch or stats_db_load_game()): count, puzzle = which game it belongs to. */
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
    uint8_t color;                       /* the player's colour, 0..STATS_PLAYER_COLORS-1 */
    uint16_t solved;                     /* puzzles solved */
    uint32_t solves;                     /* solves logged */
    uint16_t avatar;                     /* 1..AVATAR_COUNT (avatars.h); 0 = none */
} stats_player_t;

typedef struct {
    int32_t id;                          /* who plays now */
    char name[STORE_NAME_LEN];
    bool switched;                       /* another player: game below + stats cache */
    saved_game_t game;                   /* their game in progress (switched only) */
} stats_player_change_t;

/* Players have a colour each (shown by their name on leaderboards); a new one
 * gets the next one round. */
#define STATS_PLAYER_COLORS 8
void stats_db_set_player_color(int32_t id, int color);
/* ... and an avatar each, 0 = none (1..AVATAR_COUNT, avatars.h) */
void stats_db_set_player_avatar(int32_t id, int avatar);
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
    uint32_t flags;                      /* GAME_FLAG_* (store.h): the no-notes challenge, techniques hinted */
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
/* regions: a Jigsaw puzzle's region map (81), else NULL; cages: a Killer
 * puzzle's cages (sudoku_cages_text()), else NULL; day: a daily puzzle's date, else NULL */
#define STATS_CAGES_LEN 1024
int stats_db_gen_add(int variant, int level, const uint8_t givens[81], const uint8_t *regions, const char *cages,
                     const char *marks, const char *thermos, const char *day);
/* day "" if not a daily; regions (may be NULL) only filled for a Jigsaw
 * puzzle, cages (STATS_CAGES_LEN, may be NULL) for a Killer one ("" otherwise) */
/* marks: 82 bytes, may be NULL ("" unless Even-Odd / Consecutive); thermos:
 * STATS_THERMOS_LEN, may be NULL ("" unless Thermo) */
#define STATS_THERMOS_LEN 400
bool stats_db_gen_get(int id, uint8_t givens[81], uint8_t *regions, char *cages, char *marks, char *thermos,
                      int *variant, int *level, char day[12]);
/* Daily puzzle of day ('YYYY-MM-DD'): id in the generated table (0 = not
 * made yet), whether the current player solved it, and their streak of
 * days in a row with the daily solved (ending today, or yesterday if not
 * yet today). False without a database. Waits for the database. */
bool stats_db_daily_status(const char *day, int *id, bool *solved_today, int *streak);
/* The current player's place on that day's leaderboard (best time; ties:
 * fewer mistakes, then fewer hints): rank 1.., of = players who solved it;
 * rank 0 = not solved. False without a database. Waits for the database. */
bool stats_db_daily_rank(const char *day, int *rank, int *of);

/* Head-to-head on any puzzle: the current player's place among everyone who
 * solved it (best times; rank 0 = not solved), how many did, and the leader
 * (name, best time; when the current player leads, the runner-up instead,
 * second = true; leader "" if nobody else solved it). Waits for the database. */
typedef struct {
    int rank, of;
    bool second;
    char leader[STORE_NAME_LEN];
    uint32_t leader_seconds;
} stats_h2h_t;
bool stats_db_puzzle_rank(uint16_t puzzle, stats_h2h_t *out);

/* ---------------------------------------------------------------- daily leaderboard */
/* Everyone's best solve of one day's daily puzzle (whenever it was solved),
 * fastest first; players who haven't solved it come last. */
typedef struct {
    char player[STORE_NAME_LEN];
    int32_t player_id;
    bool solved;
    uint32_t seconds;                    /* best time */
    uint16_t mistakes, hints;            /* of that solve */
    uint16_t solves;                     /* times solved */
    uint8_t color;                       /* the player's colour */
    uint32_t flags;                      /* of the best solve: GAME_FLAG_* (store.h) */
} stats_board_row_t;

typedef struct {
    char day[12];                        /* 'YYYY-MM-DD' */
    bool exists;                         /* that day's puzzle has been made (opened by someone) */
    int level;                           /* gen_level_t of the puzzle */
    int variant;                         /* sudoku_variant_t of the puzzle */
    int n, solved;                       /* rows, and how many of them solved it */
    stats_board_row_t rows[STATS_MAX_PLAYERS];
} stats_board_t;
void stats_db_query_board(const char *day);
bool stats_db_take_board(stats_board_t *out);

/* ---------------------------------------------------------------- imported puzzles */
/* Files in puzzles/ on the card: .opensudoku (OpenSudoku's XML), .sdm /
 * .txt (a puzzle per line, 81 digits, 0 or . = empty) and .sdx (this board's
 * export of a saved collection: any variant; see stats_db.c). Each becomes a
 * collection (up to 100 puzzles; more: "name 2", "name 3" ...), checked for
 * exactly one solution and rated by the grader (generator.h) where the file
 * gives no level (.sdm, .txt, .opensudoku: always). A file already imported is skipped. The result comes
 * back as a message; stats_db_take_imports_changed() says to reload the list. */
#define STATS_MAX_IMPORTS 24
#define STATS_IMPORT_NAME 28
typedef struct {
    char name[STATS_IMPORT_NAME];
    int first_gid;                       /* generated.id of its first puzzle (puzzle id: + PUZZLE_GEN_BASE) */
    int count;
} stats_import_t;
void stats_db_import_puzzles(void);
/* One file of those kinds anywhere on the card (a full path): the same, but
 * just that file (Files' preview). */
void stats_db_import_file(const char *path);
bool stats_db_take_imports_changed(void);
/* The imported collections (waits for the database); -1 without one. */
int stats_db_list_imports(stats_import_t *out);
/* The current player's solved puzzles among ids first .. first + count - 1:
 * bit i of solved (count bits). How many. Waits for the database. */
int stats_db_solved_in(uint16_t first, int count, uint8_t *solved);
/* A puzzle typed in on the board (Classic): stored like an imported one, in
 * the collection STATS_OWN_SOURCE, numbered from 1. Its generated.id, or -1. */
#define STATS_OWN_SOURCE "My puzzles"
int stats_db_gen_add_own(const uint8_t givens[81], int level, int *number);
/* The same for any variant, kept under source (numbered within it: "Received #3"). */
int stats_db_gen_add_from(const char *source, int variant, int level, const uint8_t givens[81],
                          const uint8_t *regions, const char *cages, const char *marks, const char *thermos, int *number);
/* An imported puzzle's collection and number; false if it isn't one. */
bool stats_db_gen_source(int gid, char name[STATS_IMPORT_NAME], int *number);

/* ---------------------------------------------------------------- saved collections */
/* Collections made on the board: the game screen's Save puts the puzzle shown
 * (one made on the board: generated or typed in) in a collection, by name. */
#define STATS_MAX_SAVED   16             /* collections */
#define STATS_SAVED_MAX   100            /* puzzles in one */
typedef struct {
    int id;
    char name[STATS_IMPORT_NAME];
    int count;
    uint16_t puzzles[STATS_SAVED_MAX];   /* in their order (number 1, 2, ...) */
} stats_saved_t;
/* All of them, oldest first (waits for the database); -1 without one. */
int stats_db_list_saved(stats_saved_t *out);
/* Puts puzzle at the end of the collection called name, made if there's none
 * (waits). false: why says (full, too many collections, no database). */
bool stats_db_saved_add(const char *name, uint16_t puzzle, char *why, size_t why_n);
/* Takes puzzle out of collection id; the collection goes if that was its last (waits). */
bool stats_db_saved_remove(int collection, uint16_t puzzle);
/* Deletes a whole collection: a saved one (by id) or an imported one (by name; its
 * file may be imported again). Its puzzles stay on the board (solves, History,
 * Gameplays keep them). Waits. */
bool stats_db_saved_delete(int collection);
bool stats_db_import_delete(const char *name);
/* Writes collection id to the card as puzzles/<its name>.sdx (the format Import
 * puzzles reads back, variants and all: see stats_db.c); msg says what happened.
 * Waits. */
bool stats_db_saved_export(int collection, char *msg, size_t msg_n);
/* The current player's solved puzzles among ids[0..n-1] (n <= 128): bit i of
 * solved. How many. Waits for the database. */
int stats_db_solved_list(const uint16_t *ids, int n, uint8_t *solved);

/* ---------------------------------------------------------------- weekly challenge */
/* The daily puzzles of a week (Monday to Sunday) added up: per player, how
 * many of the 7 they solved and the sum of their best times on those;
 * ranked by days solved (more first), then total time. */
typedef struct {
    char player[STORE_NAME_LEN];
    int32_t player_id;
    uint8_t color;
    uint8_t days;                        /* daily puzzles of the week solved */
    uint32_t total;                      /* their best times added up, seconds */
    uint16_t best[7];                    /* best time per day (Monday first), 0 = not solved */
} stats_week_row_t;
typedef struct {
    char monday[12];                     /* 'YYYY-MM-DD' */
    int n;
    stats_week_row_t rows[STATS_MAX_PLAYERS];
} stats_week_t;
void stats_db_query_week(const char *monday);
bool stats_db_take_week(stats_week_t *out);

/* ---------------------------------------------------------------- achievements */
/* A fixed list (stats_db.c), each with the player's progress toward it.
 * They are checked after every solve; newly unlocked ones come back through
 * stats_db_take_unlocked(). Achievements already earned when this firmware
 * first opens a database are recorded quietly. */
#define STATS_ACH_MAX 40

typedef enum { ACH_UNIT_COUNT = 0, ACH_UNIT_MINUTES, ACH_UNIT_DAYS } ach_unit_t;

typedef struct {
    const char *title, *desc;            /* static strings */
    int32_t value, goal;                 /* progress (value capped at goal) */
    uint8_t unit;                        /* ach_unit_t: how to show value / goal */
    bool unlocked;
    int64_t unlocked_at;                 /* Unix time, 0 = unknown */
} stats_ach_t;

typedef struct {
    int32_t player_id;
    int n, unlocked;
    stats_ach_t a[STATS_ACH_MAX];
} stats_achievements_t;

/* the current player's achievements */
void stats_db_query_achievements(void);
bool stats_db_take_achievements(stats_achievements_t *out);
/* Titles of achievements the last solve unlocked, comma-separated; false if none. */
bool stats_db_take_unlocked(char *buf, int len, int *count);

/* ---------------------------------------------------------------- personal records */
/* The current player's bests (Trophies > Records). */
typedef enum { REC_TIME = 0, REC_COUNT, REC_DAYS, REC_MINUTES } rec_unit_t;
#define STATS_MAX_RECORDS 28
typedef struct {
    const char *title;                   /* static string */
    uint8_t unit;                        /* rec_unit_t */
    bool has;                            /* false: nothing yet */
    int32_t value;                       /* seconds, a count, days or minutes */
    uint16_t puzzle;                     /* the puzzle it was set on, STORE_NO_GAME = none */
    int64_t when;                        /* when it was set (Unix time), 0 = unknown */
} stats_record_t;
typedef struct {
    int32_t player_id;
    int n;
    stats_record_t r[STATS_MAX_RECORDS];
} stats_records_t;
void stats_db_query_records(void);
bool stats_db_take_records(stats_records_t *out);

/* ---------------------------------------------------------------- CSV export */
/* Writes export/solves.csv, export/achievements.csv and export/records.csv on
 * the card (all players), for a spreadsheet; the result comes back as a
 * message (stats_db_take_message()). */
void stats_db_export_csv(void);

/* A copy of sudoku.db as backup/sudoku_<unix time>.db on the card (consistent
 * while in use); the result comes back as a message. */
void stats_db_backup(void);

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

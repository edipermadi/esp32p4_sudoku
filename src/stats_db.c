/*
 * The game's database on the microSD card (see stats_db.h).
 */
#include "stats_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "sqlite/sqlite3.h"
#include "bsp/bsp_board.h"
#include "store.h"
#include "generator.h"

static const char *TAG = "stats_db";

#define DB_PATH BSP_SD_MOUNT_POINT "/sudoku.db"

typedef struct {                /* PUZZLE_COUNT-sized arrays, as in store.h */
    uint16_t best[PUZZLE_COUNT];
    uint8_t fewest[PUZZLE_COUNT];
    uint8_t solves[PUZZLE_COUNT];
} stats_arrays_t;

typedef struct {                /* CMD_OPEN: what the board has (freed by the worker) */
    stats_arrays_t stats;       /* first use: copied into puzzle_stats */
    store_prefs_t prefs;        /* first use: copied in; base for stats_db_take_prefs() */
    bool fresh;                 /* after a format / empty card: no statistics, settings only */
} open_seed_t;

typedef enum {
    CMD_OPEN, CMD_SOLVE, CMD_QUERY, CMD_SUSPEND, CMD_SET, CMD_GAME,
    CMD_PLAYERS, CMD_ADD_PLAYER, CMD_RENAME_PLAYER, CMD_SWITCH_PLAYER, CMD_DELETE_PLAYER,
    CMD_MONTH, CMD_DAY, CMD_RESET, CMD_REPLAY, CMD_PLAYS, CMD_GEN_ADD, CMD_GEN_GET, CMD_DAILY,
    CMD_RANK, CMD_BOARD, CMD_ACH,
} cmd_type_t;
typedef struct {
    cmd_type_t type;
    uint16_t puzzle;
    uint32_t seconds, mistakes, hints;
    int64_t when;               /* CMD_SOLVE: finished, Unix time, 0 = unknown */
    int64_t started;            /* CMD_SOLVE: started, Unix time, 0 = unknown */
    uint8_t *blob;              /* CMD_SOLVE: packed move log (malloc'd, freed by the worker) */
    int blob_bytes;
    int64_t solve_id;           /* CMD_REPLAY */
    int level;                  /* CMD_QUERY */
    int year, month, day;       /* CMD_MONTH, CMD_DAY */
    open_seed_t *seed;          /* CMD_OPEN */
    char key[24];               /* CMD_SET */
    bool is_text;
    int ival;                   /* CMD_SET value; player id for the player commands */
    char text[STORE_TZ_LEN];    /* CMD_SET text; player name */
} cmd_t;

typedef enum { ST_OFF, ST_OPENING, ST_READY, ST_NOT_FAT, ST_ERROR } db_state_t;

static QueueHandle_t s_q;
static SemaphoreHandle_t s_suspended, s_opened, s_lock;  /* s_lock guards the results below */
static volatile db_state_t s_state = ST_OFF;
static volatile int s_solves = -1;            /* rows in solves (all players), -1 unknown */
static sqlite3 *s_db;
static sqlite3_stmt *s_merge, *s_solve_upsert, *s_solve_insert;
static int32_t s_player;                      /* worker: who is playing (players.id) */
static char s_player_name[STORE_NAME_LEN];

/* results handed to the LVGL task */
static stats_view_t *s_view;                  /* PSRAM */
static bool s_view_new;
static stats_arrays_t *s_cache;               /* set after open / switch, taken once */
static store_prefs_t s_prefs_out;
static bool s_prefs_new;
static stats_player_t s_players[STATS_MAX_PLAYERS];
static int s_players_n;
static bool s_players_new;
static stats_player_change_t s_change;
static bool s_change_new;
static char s_message[96];
static stats_month_t s_month;
static bool s_month_new;
static stats_day_t s_day;
static bool s_day_new;
/* the newest game to save (CMD_GAME just says "there is one") */
static saved_game_t s_game_next;
static bool s_game_next_has, s_game_dirty;
/* move logs (PSRAM): the newest one to save with s_game_next, and the one
 * load_game() found for the current player's saved game */
static game_move_t *s_moves_next, *s_moves_loaded;
static int s_moves_next_n, s_moves_loaded_n;
static uint16_t s_moves_next_puzzle = STORE_NO_GAME, s_moves_loaded_puzzle = STORE_NO_GAME;
static stats_replay_t *s_replay;     /* CMD_REPLAY result */
static bool s_replay_new;
static stats_plays_t *s_plays;       /* CMD_PLAYS result (PSRAM) */
/* CMD_GEN_ADD / CMD_GEN_GET: one call at a time, from the UI task, which waits */
static struct {
    int level, id, variant;
    uint8_t givens[81];
    uint8_t regions[81];
    bool has_regions;            /* a Jigsaw puzzle: regions is its map */
    char day[12];                /* daily: 'YYYY-MM-DD', "" otherwise */
    bool ok;
    bool solved_today;           /* CMD_DAILY results */
    int streak;
    int rank, of;                /* CMD_RANK results */
} s_gen;
static SemaphoreHandle_t s_gen_done;
static bool s_plays_new;
static stats_board_t *s_board;       /* CMD_BOARD result (PSRAM, like the ones below) */
static bool s_board_new;
static stats_achievements_t *s_ach;  /* CMD_ACH result */
static bool s_ach_new;
static char s_unlocked[160];         /* titles unlocked by the last solve */
static int s_unlocked_n;

/* a result buffer in PSRAM (internal RAM is kept for Wi-Fi and the video encoder) */
static void *psram_buf(void **p, size_t size)
{
    if (!*p) *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM);
    return *p;
}

static bool moves_bufs(void)
{
    size_t sz = sizeof(game_move_t) * GAME_MOVES_MAX;
    if (!s_moves_next) s_moves_next = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!s_moves_loaded) s_moves_loaded = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!s_replay) s_replay = heap_caps_calloc(1, sizeof(*s_replay), MALLOC_CAP_SPIRAM);
    return s_moves_next && s_moves_loaded && s_replay;
}

/* ------------------------------------------------------------- worker side */

static bool exec(const char *sql)
{
    char *err = NULL;
    if (sqlite3_exec(s_db, sql, NULL, NULL, &err) != SQLITE_OK) {
        ESP_LOGE(TAG, "%s: %s", sql, err ? err : "?");
        printf("stats_db: SQL error: %s\n", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}

/* table layouts (user_version 9), shared by SCHEMA and the upgrade below */
#define T_PLAYERS \
    "players("                                                                  \
    "  id INTEGER PRIMARY KEY,"                                                 \
    "  name TEXT NOT NULL UNIQUE COLLATE NOCASE,"                               \
    "  created_at INTEGER)"                 /* Unix time UTC; NULL = clock not set */
#define T_SOLVES \
    "solves("                                                                   \
    "  id INTEGER PRIMARY KEY,"                                                 \
    "  player_id INTEGER NOT NULL REFERENCES players(id),"                      \
    "  puzzle INTEGER NOT NULL,"            /* 0..1599, as in the firmware */  \
    "  level TEXT NOT NULL,"                /* collection, e.g. 'Gnome 2 Hard' */ \
    "  number INTEGER NOT NULL,"            /* 1..100 within the level */       \
    "  seconds INTEGER NOT NULL,"                                               \
    "  mistakes INTEGER NOT NULL,"                                              \
    "  finished_at INTEGER,"                /* Unix time UTC; NULL = clock not set */ \
    "  started_at INTEGER,"                 /* when it was opened (or restarted) */ \
    "  hints INTEGER NOT NULL DEFAULT 0,"   /* cells filled with Fill (the solution) */ \
    "  moves BLOB)"                         /* move log (moves.h); NULL = not recorded */
#define T_PUZZLE_STATS \
    "puzzle_stats("                                                             \
    "  player_id INTEGER NOT NULL REFERENCES players(id),"                      \
    "  puzzle INTEGER NOT NULL,"                                                \
    "  level TEXT NOT NULL,"                                                    \
    "  number INTEGER NOT NULL,"                                                \
    "  best_seconds INTEGER NOT NULL,"                                          \
    "  fewest_mistakes INTEGER,"            /* NULL = solved before mistakes were counted */ \
    "  solves INTEGER NOT NULL,"                                                \
    "  PRIMARY KEY (player_id, puzzle))"
#define T_SAVED_GAME \
    "saved_game("                           /* one unfinished game per player */ \
    "  player_id INTEGER PRIMARY KEY REFERENCES players(id),"                   \
    "  puzzle INTEGER NOT NULL,"                                                \
    "  level TEXT NOT NULL,"                                                    \
    "  number INTEGER NOT NULL,"                                                \
    "  cells TEXT NOT NULL,"                /* 81 digits, row by row, 0 = empty, givens included */ \
    "  notes BLOB NOT NULL,"                /* 81 x uint16 LE, bit d = pencil mark d */ \
    "  elapsed_ms INTEGER NOT NULL,"                                            \
    "  mistakes INTEGER NOT NULL,"                                              \
    "  saved_at INTEGER,"                   /* Unix time UTC; NULL = clock not set */ \
    "  started_at INTEGER,"                                                     \
    "  hints INTEGER NOT NULL DEFAULT 0,"                                       \
    "  moves BLOB)"                         /* move log so far (moves.h) */

static const char SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS " T_PLAYERS ";"
    "CREATE TABLE IF NOT EXISTS " T_SOLVES ";"
    "CREATE TABLE IF NOT EXISTS " T_PUZZLE_STATS ";"
    "CREATE TABLE IF NOT EXISTS " T_SAVED_GAME ";"
    "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value);"
    "CREATE TABLE IF NOT EXISTS achievements("           /* unlocked achievements (ACH below) */
    "  player_id INTEGER NOT NULL REFERENCES players(id),"
    "  key TEXT NOT NULL,"
    "  unlocked_at INTEGER,"                             /* Unix time; NULL = found done at first fill / no clock */
    "  PRIMARY KEY (player_id, key));"
    "CREATE TABLE IF NOT EXISTS generated("              /* puzzles made on the board */
    "  id INTEGER PRIMARY KEY,"                          /* puzzle id = 2000 + id */
    "  level INTEGER NOT NULL,"                          /* 0 Easy, 1 Medium, 2 Hard, 3 Expert */
    "  givens TEXT NOT NULL,"                            /* 81 digits row by row, 0 = empty */
    "  created_at INTEGER,"
    "  day TEXT,"                                        /* daily puzzle: its date 'YYYY-MM-DD', else NULL */
    "  variant INTEGER NOT NULL DEFAULT 0,"              /* 0 classic, 1 X-Sudoku, 2 mini 6x6, 3 Hyper, 4 Jigsaw */
    "  regions TEXT);";                                  /* Jigsaw: 81 digits 0-8, the region of each cell */

/* after the upgrade: indexes and the view, always in their current form */
static const char SCHEMA_POST[] =
    "CREATE INDEX IF NOT EXISTS solves_puzzle ON solves(puzzle);"
    "CREATE INDEX IF NOT EXISTS solves_player ON solves(player_id, puzzle);"
    "DROP INDEX IF EXISTS puzzle_stats_level;"
    "CREATE INDEX puzzle_stats_level ON puzzle_stats(player_id, level, number);"
    "DROP VIEW IF EXISTS level_stats;"
    "CREATE VIEW level_stats AS"
    "  SELECT p.name AS player, s.level, COUNT(*) AS solved, SUM(s.solves) AS solves,"
    "         MIN(s.best_seconds) AS fastest_seconds,"
    "         CAST(ROUND(AVG(s.best_seconds)) AS INTEGER) AS average_seconds,"
    "         ROUND(AVG(s.fewest_mistakes), 1) AS average_mistakes,"
    "         SUM(s.fewest_mistakes = 0) AS without_mistakes"
    "  FROM puzzle_stats s JOIN players p ON p.id = s.player_id"
    "  GROUP BY s.player_id, s.level ORDER BY s.player_id, MIN(s.puzzle);"
    "PRAGMA user_version = 9;";

/* MIN() of SQL is NULL if either side is: keep the known value then */
#define FEWEST_MERGED "COALESCE(MIN(fewest_mistakes, excluded.fewest_mistakes), fewest_mistakes, excluded.fewest_mistakes)"

static const char SQL_MERGE[] =       /* idempotent */
    "INSERT INTO puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)"
    " VALUES(?7, ?1, ?2, ?3, ?4, ?5, ?6)"
    " ON CONFLICT(player_id, puzzle) DO UPDATE SET best_seconds = MIN(best_seconds, excluded.best_seconds),"
    "   fewest_mistakes = " FEWEST_MERGED ", solves = MAX(solves, excluded.solves)";

static const char SQL_SOLVE_UPSERT[] =
    "INSERT INTO puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)"
    " VALUES(?6, ?1, ?2, ?3, ?4, ?5, 1)"
    " ON CONFLICT(player_id, puzzle) DO UPDATE SET best_seconds = MIN(best_seconds, excluded.best_seconds),"
    "   fewest_mistakes = " FEWEST_MERGED ", solves = solves + 1";

static const char SQL_SOLVE_INSERT[] =
    "INSERT INTO solves(player_id, puzzle, level, number, seconds, mistakes, finished_at, started_at, hints, moves)"
    " VALUES(?8, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?9, ?10)";

const char *const STATS_KIND_NAMES[KIND_COUNT] = {"New puzzles", "Daily puzzle", "X-Sudoku", "Hyper", "Jigsaw",
                                                  "Mini 6x6"};
const char *const STATS_KIND_PREFIX[KIND_COUNT] = {"Generated", "Daily", "X-Sudoku", "Hyper", "Jigsaw", "Mini"};

int stats_kind_of(int variant, bool daily)
{
    if (daily) return KIND_DAILY;
    switch (variant) {
    case VAR_X: return KIND_X;
    case VAR_HYPER: return KIND_HYPER;
    case VAR_JIGSAW: return KIND_JIGSAW;
    case VAR_MINI: return KIND_MINI;
    default: return KIND_NEW;
    }
}

/* "X-Sudoku Hard" -> KIND_X, GEN_HARD; false for any other level name */
static bool kind_level_of(const char *name, int *kind, int *level)
{
    for (int k = 0; name && k < KIND_COUNT; k++) {
        size_t n = strlen(STATS_KIND_PREFIX[k]);
        if (strncmp(name, STATS_KIND_PREFIX[k], n) != 0 || name[n] != ' ') continue;
        for (int l = 0; l < GEN_LEVELS; l++) {
            if (strcmp(name + n + 1, GEN_LEVEL_NAMES[l]) == 0) {
                *kind = k;
                *level = l;
                return true;
            }
        }
    }
    return false;
}

static int query_int(const char *sql);
static int gen_level_of(int gid, bool *daily, int *variant);
static void ach_sync(int32_t player, bool announce);
static void ach_backfill(void);

static void bind_puzzle(sqlite3_stmt *st, uint16_t id)
{
    sqlite3_bind_int(st, 1, id);
    if (puzzle_is_generated(id)) {               /* level "Generated Hard", "X-Sudoku Hard", "Mini Hard"; number = generated.id */
        char lv[24];
        bool daily = false;
        int variant = VAR_CLASSIC;
        int g = gen_level_of(id - PUZZLE_GEN_BASE, &daily, &variant);
        snprintf(lv, sizeof(lv), "%s %s", STATS_KIND_PREFIX[stats_kind_of(variant, daily)],
                 g >= 0 && g < GEN_LEVELS ? GEN_LEVEL_NAMES[g] : "");
        sqlite3_bind_text(st, 2, lv, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, id - PUZZLE_GEN_BASE);
        return;
    }
    int c = puzzle_collection_of(id);
    sqlite3_bind_text(st, 2, puzzle_collections[c].name, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, id - puzzle_collections[c].first + 1);
}

static bool step_done(sqlite3_stmt *st, const char *what)
{
    int rc = sqlite3_step(st);
    sqlite3_reset(st);
    if (rc != SQLITE_DONE) {
        printf("stats_db: %s failed: %s\n", what, sqlite3_errmsg(s_db));
        return false;
    }
    return true;
}

static int query_int(const char *sql)
{
    int v = -1;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
            v = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    return v;
}

static void set_setting(const char *key, bool is_text, int ival, const char *text)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "INSERT INTO settings(key, value) VALUES(?1, ?2)"
                           " ON CONFLICT(key) DO UPDATE SET value = excluded.value", -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    if (is_text) sqlite3_bind_text(st, 2, text, -1, SQLITE_STATIC);
    else sqlite3_bind_int(st, 2, ival);
    step_done(st, "setting");
    sqlite3_finalize(st);
}

static void save_game(const saved_game_t *g, const game_move_t *moves, int nmoves)
{
    sqlite3_stmt *st;
    if (!g) {
        if (sqlite3_prepare_v2(s_db, "DELETE FROM saved_game WHERE player_id = ?1", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, s_player);
            step_done(st, "clear game");
            sqlite3_finalize(st);
        }
        return;
    }
    char cells[82];
    uint8_t notes[162];
    for (int i = 0; i < 81; i++) {
        cells[i] = (char)('0' + (g->val[i] <= 9 ? g->val[i] : 0));
        notes[2 * i] = g->notes[i] & 0xFF;
        notes[2 * i + 1] = g->notes[i] >> 8;
    }
    cells[81] = 0;
    time_t now = time(NULL);
    if (sqlite3_prepare_v2(s_db,
            "INSERT OR REPLACE INTO saved_game(player_id, puzzle, level, number, cells, notes, elapsed_ms,"
            " mistakes, saved_at, started_at, hints, moves)"
            " VALUES(?10, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?11, ?12)",
            -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    bind_puzzle(st, g->id);
    sqlite3_bind_text(st, 4, cells, 81, SQLITE_STATIC);
    sqlite3_bind_blob(st, 5, notes, sizeof(notes), SQLITE_STATIC);
    sqlite3_bind_int64(st, 6, g->elapsed_ms);
    sqlite3_bind_int(st, 7, g->mistakes);
    if (now > 1700000000) sqlite3_bind_int64(st, 8, now);
    else sqlite3_bind_null(st, 8);
    if (g->started_at) sqlite3_bind_int64(st, 9, g->started_at);
    else sqlite3_bind_null(st, 9);
    sqlite3_bind_int(st, 10, s_player);
    sqlite3_bind_int(st, 11, g->hints);
    uint8_t *blob = NULL;
    if (moves && nmoves > 0) blob = heap_caps_malloc((size_t)nmoves * MOVE_BLOB_BYTES, MALLOC_CAP_SPIRAM);
    if (blob) sqlite3_bind_blob(st, 12, blob, (int)moves_pack(moves, nmoves, blob), SQLITE_STATIC);
    else sqlite3_bind_null(st, 12);
    step_done(st, "save game");
    sqlite3_finalize(st);
    free(blob);
}

/* the current player's unfinished game; g->id = STORE_NO_GAME if none */
static void load_game(saved_game_t *g)
{
    memset(g, 0, sizeof(*g));
    g->id = STORE_NO_GAME;
    sqlite3_stmt *st;
    int nmoves = 0;
    if (sqlite3_prepare_v2(s_db, "SELECT puzzle, cells, notes, elapsed_ms, mistakes, started_at, hints, moves FROM saved_game"
                                 " WHERE player_id = ?1", -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int(st, 1, s_player);
    if (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        const char *cells = (const char *)sqlite3_column_text(st, 1);
        const uint8_t *notes = (const uint8_t *)sqlite3_column_blob(st, 2);
        if (puzzle_id_valid(id) && cells && strlen(cells) == 81 && notes &&
            sqlite3_column_bytes(st, 2) == 162) {
            g->id = id;
            for (int i = 0; i < 81; i++) {
                g->val[i] = (cells[i] >= '0' && cells[i] <= '9') ? cells[i] - '0' : 0;
                g->notes[i] = notes[2 * i] | (notes[2 * i + 1] << 8);
            }
            g->elapsed_ms = (uint32_t)sqlite3_column_int64(st, 3);
            g->mistakes = (uint16_t)sqlite3_column_int(st, 4);
            g->started_at = sqlite3_column_int64(st, 5);      /* NULL reads as 0 = unknown */
            g->hints = (uint16_t)sqlite3_column_int(st, 6);
            const uint8_t *mb = (const uint8_t *)sqlite3_column_blob(st, 7);
            if (mb && moves_bufs()) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                nmoves = moves_unpack(mb, sqlite3_column_bytes(st, 7), s_moves_loaded, GAME_MOVES_MAX);
                xSemaphoreGive(s_lock);
            }
        }
    }
    sqlite3_finalize(st);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_moves_loaded_n = nmoves;
    s_moves_loaded_puzzle = g->id;
    xSemaphoreGive(s_lock);
}

/* seed's prefs, overridden by whatever sudoku.db holds */
static void load_prefs(store_prefs_t *p)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "SELECT key, value FROM settings", -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *k = (const char *)sqlite3_column_text(st, 0);
            if (!k) continue;
            int v = sqlite3_column_int(st, 1);
            const char *t = (const char *)sqlite3_column_text(st, 1);
            if (!strcmp(k, "dark_mode")) p->dark_mode = v != 0;
            else if (!strcmp(k, "sound")) p->sound = v != 0;
            else if (!strcmp(k, "screen_off_minutes")) p->screen_timeout_min = v < 0 ? 0 : (v > 255 ? 255 : v);
            else if (!strcmp(k, "brightness")) p->brightness = v < 10 ? 10 : (v > 100 ? 100 : v);
            else if (!strcmp(k, "volume")) p->volume = v < 0 ? 0 : (v > 100 ? 100 : v);
            else if (!strcmp(k, "assist")) p->assist = v >= ASSIST_OFF && v <= ASSIST_SOLUTION ? v : ASSIST_RULES;
            else if (!strcmp(k, "time_zone") && t) snprintf(p->timezone, sizeof(p->timezone), "%s", t);
            else if (!strcmp(k, "last_level") && t) {
                for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
                    if (!strcmp(puzzle_collections[c].name, t)) p->last_collection = c;
                }
            }
        }
        sqlite3_finalize(st);
    }
    load_game(&p->game);
    p->player_id = s_player;
    snprintf(p->player_name, sizeof(p->player_name), "%s", s_player_name);
}

static bool has_column(const char *table, const char *col)
{
    bool found = false;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "SELECT 1 FROM pragma_table_info(?1) WHERE name = ?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, col, -1, SQLITE_STATIC);
        found = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    return found;
}

/* Databases made by earlier builds: CREATE ... IF NOT EXISTS keeps their old
 * tables, so bring them up to date (user_version 1-5 -> 6). Their data
 * becomes player 1's. */
static bool upgrade_schema(void)
{
    bool ok = true;
    exec("DROP VIEW IF EXISTS level_stats");           /* refers to tables rebuilt below */
    if (!has_column("solves", "finished_at") && has_column("solves", "solved_at")) {
        ok &= exec("ALTER TABLE solves RENAME COLUMN solved_at TO finished_at");
    }
    if (!has_column("solves", "started_at")) ok &= exec("ALTER TABLE solves ADD COLUMN started_at INTEGER");
    if (!has_column("saved_game", "started_at")) ok &= exec("ALTER TABLE saved_game ADD COLUMN started_at INTEGER");
    if (!has_column("solves", "hints")) ok &= exec("ALTER TABLE solves ADD COLUMN hints INTEGER NOT NULL DEFAULT 0");
    if (!has_column("saved_game", "hints")) ok &= exec("ALTER TABLE saved_game ADD COLUMN hints INTEGER NOT NULL DEFAULT 0");
    if (!has_column("solves", "moves")) ok &= exec("ALTER TABLE solves ADD COLUMN moves BLOB");
    if (!has_column("saved_game", "moves")) ok &= exec("ALTER TABLE saved_game ADD COLUMN moves BLOB");
    if (!has_column("generated", "day")) ok &= exec("ALTER TABLE generated ADD COLUMN day TEXT");
    if (!has_column("generated", "variant")) ok &= exec("ALTER TABLE generated ADD COLUMN variant INTEGER NOT NULL DEFAULT 0");
    if (!has_column("generated", "regions")) ok &= exec("ALTER TABLE generated ADD COLUMN regions TEXT");

    bool old_solves = !has_column("solves", "player_id");
    bool old_stats = !has_column("puzzle_stats", "player_id");
    bool old_game = !has_column("saved_game", "player_id");
    if (!(old_solves || old_stats || old_game)) return ok;
    ok &= exec("BEGIN");
    ok &= exec("INSERT OR IGNORE INTO players(id, name) VALUES(1, 'Player 1')");
    if (old_solves) ok &= exec("ALTER TABLE solves ADD COLUMN player_id INTEGER NOT NULL DEFAULT 1");
    if (old_stats) {
        ok &= exec("ALTER TABLE puzzle_stats RENAME TO puzzle_stats_v3");
        ok &= exec("CREATE TABLE " T_PUZZLE_STATS);
        ok &= exec("INSERT INTO puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)"
                   " SELECT 1, puzzle, level, number, best_seconds, fewest_mistakes, solves FROM puzzle_stats_v3");
        ok &= exec("DROP TABLE puzzle_stats_v3");
    }
    if (old_game) {
        ok &= exec("ALTER TABLE saved_game RENAME TO saved_game_v3");
        ok &= exec("CREATE TABLE " T_SAVED_GAME);
        ok &= exec("INSERT INTO saved_game(player_id, puzzle, level, number, cells, notes, elapsed_ms, mistakes,"
                   " saved_at, started_at, hints) SELECT 1, puzzle, level, number, cells, notes, elapsed_ms,"
                   " mistakes, saved_at, started_at, hints FROM saved_game_v3");
        ok &= exec("DROP TABLE saved_game_v3");
    }
    if (ok) {
        exec("COMMIT");
        printf("stats_db: upgraded: existing statistics and game now belong to \"Player 1\"\n");
    } else {
        exec("ROLLBACK");
    }
    return ok;
}

static bool player_name_of(int32_t id, char *out, size_t n)
{
    bool found = false;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "SELECT name FROM players WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            snprintf(out, n, "%s", (const char *)sqlite3_column_text(st, 0));
            found = true;
        }
        sqlite3_finalize(st);
    }
    return found;
}

/* new player; returns its id, 0 if the name is taken, -1 on error */
static int32_t insert_player(const char *name)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "INSERT INTO players(name, created_at) VALUES(?1, ?2)", -1, &st, NULL) != SQLITE_OK) {
        return -1;
    }
    time_t now = time(NULL);
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (now > 1700000000) sqlite3_bind_int64(st, 2, now);
    else sqlite3_bind_null(st, 2);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) return (int32_t)sqlite3_last_insert_rowid(s_db);
    return (rc & 0xFF) == SQLITE_CONSTRAINT ? 0 : -1;
}

/* who is playing: settings.current_player if it still exists, else the
 * first player; a database without players gets one named `name` */
static void select_current_player(const char *name)
{
    int32_t id = query_int("SELECT p.id FROM players p JOIN settings s ON s.key = 'current_player'"
                           " AND p.id = s.value");
    if (id <= 0) id = query_int("SELECT MIN(id) FROM players");
    if (id <= 0) id = insert_player(name && name[0] ? name : "Player 1");
    if (id <= 0) id = 1;
    s_player = id;
    if (!player_name_of(id, s_player_name, sizeof(s_player_name))) {
        snprintf(s_player_name, sizeof(s_player_name), "Player 1");
    }
    set_setting("current_player", false, id, NULL);
}

/* first use of this database: copy in the settings and game the board has */
static void migrate_prefs(const store_prefs_t *p)
{
    exec("BEGIN");
    set_setting("dark_mode", false, p->dark_mode, NULL);
    set_setting("sound", false, p->sound, NULL);
    set_setting("screen_off_minutes", false, p->screen_timeout_min, NULL);
    set_setting("brightness", false, p->brightness, NULL);
    set_setting("volume", false, p->volume, NULL);
    set_setting("assist", false, p->assist, NULL);
    set_setting("time_zone", true, 0, p->timezone);
    set_setting("last_level", true, 0, puzzle_collections[p->last_collection < PUZZLE_COLLECTION_COUNT
                                                          ? p->last_collection : 0].name);
    save_game(puzzle_id_valid(p->game.id) ? &p->game : NULL, NULL, 0);
    set_setting("migrated_from_nvs", false, 1, NULL);
    exec("COMMIT");
}

static void db_close(void)
{
    sqlite3_finalize(s_merge);
    sqlite3_finalize(s_solve_upsert);
    sqlite3_finalize(s_solve_insert);
    s_merge = s_solve_upsert = s_solve_insert = NULL;
    if (s_db) sqlite3_close(s_db);
    s_db = NULL;
}

static void merge_legacy(const stats_arrays_t *a)
{
    exec("BEGIN");
    for (uint16_t i = 0; i < PUZZLE_COUNT; i++) {
        if (!a->best[i]) continue;
        bind_puzzle(s_merge, i);
        sqlite3_bind_int(s_merge, 4, a->best[i]);
        if (a->fewest[i] == STORE_MISTAKES_UNKNOWN) sqlite3_bind_null(s_merge, 5);
        else sqlite3_bind_int(s_merge, 5, a->fewest[i]);
        sqlite3_bind_int(s_merge, 6, a->solves[i] ? a->solves[i] : 1);
        sqlite3_bind_int(s_merge, 7, s_player);
        step_done(s_merge, "merge");
    }
    exec("COMMIT");
}

/* the current player's puzzle_stats -> arrays for the RAM copy */
static void load_cache(void)
{
    stats_arrays_t *a = calloc(1, sizeof(*a));
    if (!a) return;
    memset(a->fewest, STORE_MISTAKES_UNKNOWN, sizeof(a->fewest));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "SELECT puzzle, best_seconds, fewest_mistakes, solves FROM puzzle_stats"
                                 " WHERE player_id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_player);
        while (sqlite3_step(st) == SQLITE_ROW) {
            int id = sqlite3_column_int(st, 0);
            if (id < 0 || id >= PUZZLE_COUNT) continue;
            int best = sqlite3_column_int(st, 1);
            int solves = sqlite3_column_int(st, 3);
            a->best[id] = best < 1 ? 1 : (best > 0xFFFF ? 0xFFFF : best);
            if (sqlite3_column_type(st, 2) != SQLITE_NULL) {
                int m = sqlite3_column_int(st, 2);
                a->fewest[id] = m > 254 ? 254 : (m < 0 ? 0 : m);
            }
            a->solves[id] = solves > 255 ? 255 : (solves < 1 ? 1 : solves);
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    free(s_cache);
    s_cache = a;
    xSemaphoreGive(s_lock);
}

static void do_list(void)
{
    stats_player_t list[STATS_MAX_PLAYERS];
    int n = 0;
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db,
            "SELECT p.id, p.name,"
            "  (SELECT COUNT(*) FROM puzzle_stats s WHERE s.player_id = p.id),"
            "  (SELECT COUNT(*) FROM solves v WHERE v.player_id = p.id)"
            " FROM players p ORDER BY p.id", -1, &st, NULL) == SQLITE_OK) {
        while (n < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) {
            stats_player_t *p = &list[n++];
            p->id = sqlite3_column_int(st, 0);
            snprintf(p->name, sizeof(p->name), "%s", (const char *)sqlite3_column_text(st, 1));
            p->solved = sqlite3_column_int(st, 2);
            p->solves = sqlite3_column_int(st, 3);
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_players, list, sizeof(stats_player_t) * n);
    s_players_n = n;
    s_players_new = true;
    xSemaphoreGive(s_lock);
}

static void post_message(const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_message, sizeof(s_message), "%s", msg);
    xSemaphoreGive(s_lock);
}

static void post_change(bool switched)
{
    static stats_player_change_t ch;
    memset(&ch, 0, sizeof(ch));
    ch.id = s_player;
    snprintf(ch.name, sizeof(ch.name), "%s", s_player_name);
    ch.switched = switched;
    if (switched) load_game(&ch.game);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_change = ch;
    s_change_new = true;
    xSemaphoreGive(s_lock);
}

static void do_open_inner(open_seed_t *seed);

static void do_open(open_seed_t *seed)
{
    do_open_inner(seed);
    free(seed);
    xSemaphoreGive(s_opened);
}

static void do_open_inner(open_seed_t *seed)
{
    if (s_db) return;
    s_state = ST_OPENING;
    esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        s_state = err == ESP_ERR_NOT_SUPPORTED ? ST_NOT_FAT : ST_ERROR;
        printf("stats_db: card not mounted (%s)\n", esp_err_to_name(err));
        return;
    }
    bool existed;
    {   /* An earlier build left a small all-zero sudoku.db behind (see the
         * read note in sqlite3_esp.c). It is not a database: remove it. */
        struct stat st;
        if (stat(DB_PATH, &st) == 0 && st.st_size > 0 && st.st_size < 100) {
            unlink(DB_PATH);
            unlink(DB_PATH "-journal");
        }
        existed = stat(DB_PATH, &st) == 0 && st.st_size > 0;
    }
    /* No sudoku.db yet (an empty card): start a skeleton like after a format,
     * with the current settings, player and game but no statistics. */
    if (seed && !existed) seed->fresh = true;
    if (sqlite3_open_v2(DB_PATH, &s_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        printf("stats_db: open failed: %s\n", s_db ? sqlite3_errmsg(s_db) : "no memory");
        db_close();
        s_state = ST_ERROR;
        return;
    }
    if (!exec(SCHEMA) || !upgrade_schema() || !exec(SCHEMA_POST) ||
        sqlite3_prepare_v2(s_db, SQL_MERGE, -1, &s_merge, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(s_db, SQL_SOLVE_UPSERT, -1, &s_solve_upsert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(s_db, SQL_SOLVE_INSERT, -1, &s_solve_insert, NULL) != SQLITE_OK) {
        printf("stats_db: setup failed: %s\n", sqlite3_errmsg(s_db));
        db_close();
        s_state = ST_ERROR;
        return;
    }
    select_current_player(seed ? seed->prefs.player_name : NULL);
    if (seed) {
        /* once per database (the marker is written with the settings); a
         * database made after a format must not get the old statistics back */
        if (query_int("SELECT COUNT(*) FROM settings WHERE key = 'migrated_from_nvs'") <= 0) {
            if (!seed->fresh) merge_legacy(&seed->stats);
            migrate_prefs(&seed->prefs);
            printf("stats_db: new database: %s\n", seed->fresh
                   ? "current settings and player stored, statistics start fresh"
                   : "statistics, settings and game copied in from the board");
        }
        store_prefs_t p = seed->prefs;
        load_prefs(&p);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_prefs_out = p;
        s_prefs_new = true;
        xSemaphoreGive(s_lock);
    }
    load_cache();
    ach_backfill();                                 /* earned before this firmware: recorded quietly */
    s_solves = query_int("SELECT COUNT(*) FROM solves");
    int players = query_int("SELECT COUNT(*) FROM players");
    s_state = ST_READY;
    printf("stats_db: %s open, %d player%s, playing as \"%s\", %d solves logged\n", DB_PATH,
           players, players == 1 ? "" : "s", s_player_name, s_solves);
}

static void do_solve(const cmd_t *c)
{
    if (!s_db) return;
    exec("BEGIN");
    bind_puzzle(s_solve_insert, c->puzzle);
    sqlite3_bind_int64(s_solve_insert, 4, c->seconds);
    sqlite3_bind_int64(s_solve_insert, 5, c->mistakes);
    if (c->when) sqlite3_bind_int64(s_solve_insert, 6, c->when);
    else sqlite3_bind_null(s_solve_insert, 6);
    if (c->started) sqlite3_bind_int64(s_solve_insert, 7, c->started);
    else sqlite3_bind_null(s_solve_insert, 7);
    sqlite3_bind_int(s_solve_insert, 8, s_player);
    sqlite3_bind_int64(s_solve_insert, 9, c->hints);
    if (c->blob) sqlite3_bind_blob(s_solve_insert, 10, c->blob, c->blob_bytes, SQLITE_STATIC);
    else sqlite3_bind_null(s_solve_insert, 10);
    bool ok = step_done(s_solve_insert, "insert solve");
    bind_puzzle(s_solve_upsert, c->puzzle);
    sqlite3_bind_int64(s_solve_upsert, 4, c->seconds ? c->seconds : 1);
    sqlite3_bind_int64(s_solve_upsert, 5, c->mistakes);
    sqlite3_bind_int(s_solve_upsert, 6, s_player);
    ok = step_done(s_solve_upsert, "update puzzle_stats") && ok;
    if (ok && exec("COMMIT")) {
        if (s_solves >= 0) s_solves++;
        ach_sync(s_player, true);                  /* new achievements: stats_db_take_unlocked() */
    } else {
        exec("ROLLBACK");
    }
}

static void do_query(int level)
{
    static stats_view_t *vp;                  /* worker-side scratch (PSRAM) */
    if (!psram_buf((void **)&vp, sizeof(*vp)) || !psram_buf((void **)&s_view, sizeof(*s_view))) return;
    stats_view_t *const v = vp;
    memset(v, 0, sizeof(*v));
    v->level = level;
    v->player_id = s_player;

    /* per level: same aggregates the screen shows (closed database: empty result) */
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db,
            "SELECT level, COUNT(*), TOTAL(fewest_mistakes = 0), COUNT(fewest_mistakes),"
            "       TOTAL(solves), TOTAL(best_seconds), MIN(best_seconds), TOTAL(fewest_mistakes)"
            " FROM puzzle_stats WHERE player_id = ?1 GROUP BY level", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_player);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *name = (const char *)sqlite3_column_text(st, 0);
            int c = 0, kind, klevel;
            while (c < PUZZLE_COLLECTION_COUNT && strcmp(puzzle_collections[c].name, name ? name : "") != 0) c++;
            stats_level_t *l;
            if (c < PUZZLE_COLLECTION_COUNT) l = &v->levels[c];
            else if (kind_level_of(name, &kind, &klevel)) l = &v->kinds[kind][klevel];   /* made on the board */
            else continue;                                /* a level this firmware doesn't have */
            l->solved = sqlite3_column_int(st, 1);
            l->perfect = (uint16_t)sqlite3_column_int(st, 2);
            l->known = sqlite3_column_int(st, 3);
            l->solves = (uint32_t)sqlite3_column_int64(st, 4);
            l->time_sum = (uint32_t)sqlite3_column_int64(st, 5);
            l->fastest = sqlite3_column_int(st, 6);
            l->miss_sum = (uint32_t)sqlite3_column_int64(st, 7);
        }
        sqlite3_finalize(st);
    }
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        stats_level_t *l = &v->levels[c], *t = &v->total;
        t->solved += l->solved;
        t->perfect += l->perfect;
        t->known += l->known;
        t->solves += l->solves;
        t->time_sum += l->time_sum;
        t->miss_sum += l->miss_sum;
        if (l->fastest && (!t->fastest || l->fastest < t->fastest)) t->fastest = l->fastest;
    }

    /* one level: its solved puzzles */
    if (s_db && level >= 0 && level < PUZZLE_COLLECTION_COUNT &&
        sqlite3_prepare_v2(s_db,
            "SELECT number, best_seconds, fewest_mistakes, solves FROM puzzle_stats"
            " WHERE player_id = ?1 AND level = ?2 ORDER BY number", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_player);
        sqlite3_bind_text(st, 2, puzzle_collections[level].name, -1, SQLITE_STATIC);
        while (v->n_puzzles < STATS_MAX_PUZZLES && sqlite3_step(st) == SQLITE_ROW) {
            stats_puzzle_t *p = &v->puzzles[v->n_puzzles++];
            p->number = sqlite3_column_int(st, 0);
            int best = sqlite3_column_int(st, 1);
            p->best = best > 0xFFFF ? 0xFFFF : best;
            if (sqlite3_column_type(st, 2) == SQLITE_NULL) {
                p->fewest = STORE_MISTAKES_UNKNOWN;
            } else {
                int m = sqlite3_column_int(st, 2);
                p->fewest = m > 254 ? 254 : m;
            }
            int n = sqlite3_column_int(st, 3);
            p->solves = n > 0xFFFF ? 0xFFFF : n;
        }
        sqlite3_finalize(st);
    }

    /* progress chart: the newest solves at this level (a kind: its levels; all levels: any), oldest first */
    char like[24];
    bool coll = level >= 0 && level < PUZZLE_COLLECTION_COUNT;
    bool kind = level >= PUZZLE_COLLECTION_COUNT && level < STATS_LEVEL_KIND(KIND_COUNT);
    if (kind) snprintf(like, sizeof(like), "%s %%", STATS_KIND_PREFIX[level - PUZZLE_COLLECTION_COUNT]);
    if (s_db && sqlite3_prepare_v2(s_db, coll
                ? "SELECT seconds FROM solves WHERE player_id = ?1 AND level = ?3 ORDER BY id DESC LIMIT ?2"
                : kind ? "SELECT seconds FROM solves WHERE player_id = ?1 AND level LIKE ?3 ORDER BY id DESC LIMIT ?2"
                : "SELECT seconds FROM solves WHERE player_id = ?1 ORDER BY id DESC LIMIT ?2",
                -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_player);
        sqlite3_bind_int(st, 2, STATS_RECENT);
        if (coll) sqlite3_bind_text(st, 3, puzzle_collections[level].name, -1, SQLITE_STATIC);
        if (kind) sqlite3_bind_text(st, 3, like, -1, SQLITE_TRANSIENT);
        uint16_t newest_first[STATS_RECENT];
        int n = 0;
        while (n < STATS_RECENT && sqlite3_step(st) == SQLITE_ROW) {
            int64_t sec = sqlite3_column_int64(st, 0);
            newest_first[n++] = (uint16_t)(sec > 0xFFFF ? 0xFFFF : (sec < 1 ? 1 : sec));
        }
        sqlite3_finalize(st);
        for (int i = 0; i < n; i++) v->recent[i] = newest_first[n - 1 - i];
        v->n_recent = n;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *s_view = *v;
    s_view_new = true;
    xSemaphoreGive(s_lock);
}

/* writes the newest queued game, if any (for the player it belongs to) */
static void flush_game(void)
{
    static saved_game_t g;                    /* worker-side copies */
    static game_move_t *m;
    if (!m) m = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool dirty = s_game_dirty, has = s_game_next_has;
    g = s_game_next;
    if (has && m && s_moves_next && s_moves_next_puzzle == g.id) {
        n = s_moves_next_n;
        memcpy(m, s_moves_next, sizeof(game_move_t) * n);
    }
    s_game_dirty = false;
    xSemaphoreGive(s_lock);
    if (dirty && s_db) save_game(has ? &g : NULL, m, n);
}

static void do_switch(int32_t id)
{
    if (!s_db) return;
    char name[STORE_NAME_LEN];
    if (!player_name_of(id, name, sizeof(name))) {
        post_message("That player no longer exists.");
        do_list();
        return;
    }
    flush_game();                             /* the old player's last moves */
    s_player = id;
    snprintf(s_player_name, sizeof(s_player_name), "%s", name);
    set_setting("current_player", false, id, NULL);
    load_cache();
    post_change(true);
    do_list();
}

/* trims and checks a name typed on the keyboard; false if unusable */
static bool clean_name(const char *in, char *out, size_t n)
{
    while (*in && isspace((unsigned char)*in)) in++;
    snprintf(out, n, "%s", in);
    size_t len = strlen(out);
    while (len && isspace((unsigned char)out[len - 1])) out[--len] = 0;
    return len > 0;
}

static void do_add(const char *raw)
{
    if (!s_db) return;
    char name[STORE_NAME_LEN], msg[96];
    if (!clean_name(raw, name, sizeof(name))) {
        post_message("Type a name first.");
        return;
    }
    int32_t id = insert_player(name);
    if (id == 0) {
        snprintf(msg, sizeof(msg), "There is already a player called \"%s\".", name);
        post_message(msg);
        return;
    }
    if (id < 0) {
        post_message("Couldn't add the player.");
        return;
    }
    do_switch(id);                            /* a new player starts playing */
}

static void do_rename(int32_t id, const char *raw)
{
    if (!s_db) return;
    char name[STORE_NAME_LEN], msg[96];
    if (!clean_name(raw, name, sizeof(name))) {
        post_message("Type a name first.");
        return;
    }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "UPDATE players SET name = ?1 WHERE id = ?2", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if ((rc & 0xFF) == SQLITE_CONSTRAINT) {
        snprintf(msg, sizeof(msg), "There is already a player called \"%s\".", name);
        post_message(msg);
    } else if (rc == SQLITE_DONE && id == s_player) {
        snprintf(s_player_name, sizeof(s_player_name), "%s", name);
        post_change(false);
    }
    do_list();
}

static void do_delete(int32_t id)
{
    if (!s_db) return;
    char name[STORE_NAME_LEN], msg[96];
    if (!player_name_of(id, name, sizeof(name))) {
        do_list();
        return;
    }
    if (query_int("SELECT COUNT(*) FROM players") <= 1) {
        post_message("The only player can't be deleted.");
        return;
    }
    bool current = id == s_player;
    if (current) flush_game();                /* nothing pending may land after the delete */
    static const char *const sql[] = {
        "DELETE FROM solves WHERE player_id = ?1",
        "DELETE FROM puzzle_stats WHERE player_id = ?1",
        "DELETE FROM saved_game WHERE player_id = ?1",
        "DELETE FROM achievements WHERE player_id = ?1",
        "DELETE FROM players WHERE id = ?1",
    };
    bool ok = exec("BEGIN");
    for (size_t i = 0; ok && i < sizeof(sql) / sizeof(sql[0]); i++) {
        sqlite3_stmt *st;
        ok = sqlite3_prepare_v2(s_db, sql[i], -1, &st, NULL) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_int(st, 1, id);
            ok = step_done(st, "delete player");
            sqlite3_finalize(st);
        }
    }
    if (!ok) {
        exec("ROLLBACK");
        post_message("Couldn't delete the player.");
        return;
    }
    exec("COMMIT");
    s_solves = query_int("SELECT COUNT(*) FROM solves");
    snprintf(msg, sizeof(msg), "Deleted %s.", name);
    post_message(msg);
    printf("stats_db: deleted player \"%s\"\n", name);
    if (current) {
        s_player = 0;                         /* gone: any player is "another" now */
        do_switch(query_int("SELECT MIN(id) FROM players"));
    } else {
        do_list();
    }
}

/* Every dated event: solves by when they finished, unfinished games by when
 * they started. t = Unix time, local day as 'YYYY-MM-DD'. */
#define HISTORY_EVENTS \
    "SELECT s.finished_at AS t, s.started_at AS started, s.finished_at AS finished, p.name AS player," \
    "       s.puzzle, s.seconds, s.mistakes, 0 AS in_progress, s.hints, s.id AS solve_id," \
    "       s.moves IS NOT NULL AS has_moves" \
    "  FROM solves s JOIN players p ON p.id = s.player_id WHERE s.finished_at IS NOT NULL" \
    " UNION ALL " \
    "SELECT g.started_at, g.started_at, NULL, p.name, g.puzzle, g.elapsed_ms / 1000, g.mistakes, 1, g.hints, 0, 0" \
    "  FROM saved_game g JOIN players p ON p.id = g.player_id WHERE g.started_at IS NOT NULL"

static void do_month(int year, int month)
{
    static stats_month_t m;
    memset(&m, 0, sizeof(m));
    sqlite3_stmt *st;
    char ym[8] = "";
    if (s_db && year == 0) {                  /* the latest month with anything in it */
        if (sqlite3_prepare_v2(s_db, "SELECT strftime('%Y-%m', MAX(t), 'unixepoch', 'localtime') FROM ("
                                     HISTORY_EVENTS ")", -1, &st, NULL) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
                sscanf((const char *)sqlite3_column_text(st, 0), "%d-%d", &year, &month);
            }
            sqlite3_finalize(st);
        }
    }
    if (s_db && year > 0) {
        m.year = year;
        m.month = month;
        snprintf(ym, sizeof(ym), "%04d-%02d", year, month);
        if (sqlite3_prepare_v2(s_db,
                "SELECT CAST(strftime('%d', t, 'unixepoch', 'localtime') AS INTEGER), COUNT(*) FROM ("
                HISTORY_EVENTS ") WHERE strftime('%Y-%m', t, 'unixepoch', 'localtime') = ?1 GROUP BY 1",
                -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, ym, -1, SQLITE_STATIC);
            while (sqlite3_step(st) == SQLITE_ROW) {
                int d = sqlite3_column_int(st, 0), n = sqlite3_column_int(st, 1);
                if (d >= 1 && d <= 31) m.count[d - 1] = n > 255 ? 255 : n;
            }
            sqlite3_finalize(st);
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_month = m;
    s_month_new = true;
    xSemaphoreGive(s_lock);
}

/* ------------------------------------------------------------- generated puzzles */

static int gen_level_of(int gid, bool *daily, int *variant)
{
    int level = -1;
    *daily = false;
    *variant = VAR_CLASSIC;
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db, "SELECT level, day, variant FROM generated WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, gid);
        if (sqlite3_step(st) == SQLITE_ROW) {
            level = sqlite3_column_int(st, 0);
            *daily = sqlite3_column_type(st, 1) != SQLITE_NULL;
            *variant = sqlite3_column_int(st, 2);
        }
        sqlite3_finalize(st);
    }
    return level;
}

static void do_gen_add(void)
{
    s_gen.ok = false;
    sqlite3_stmt *st;
    char cells[82];
    for (int i = 0; i < 81; i++) cells[i] = (char)('0' + (s_gen.givens[i] <= 9 ? s_gen.givens[i] : 0));
    cells[81] = 0;
    time_t now = time(NULL);
    char reg[82];
    for (int i = 0; i < 81; i++) reg[i] = (char)('0' + (s_gen.regions[i] <= 8 ? s_gen.regions[i] : 0));
    reg[81] = 0;
    if (s_db && sqlite3_prepare_v2(s_db, "INSERT INTO generated(level, givens, created_at, day, variant, regions)"
                                         " VALUES(?1, ?2, ?3, ?4, ?5, ?6)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_gen.level);
        sqlite3_bind_int(st, 5, s_gen.variant);
        if (s_gen.has_regions) sqlite3_bind_text(st, 6, reg, 81, SQLITE_STATIC);
        else sqlite3_bind_null(st, 6);
        sqlite3_bind_text(st, 2, cells, 81, SQLITE_STATIC);
        if (s_gen.day[0]) sqlite3_bind_text(st, 4, s_gen.day, -1, SQLITE_STATIC);
        else sqlite3_bind_null(st, 4);
        if (now > 1700000000) sqlite3_bind_int64(st, 3, now);
        else sqlite3_bind_null(st, 3);
        if (sqlite3_step(st) == SQLITE_DONE) {
            s_gen.id = (int)sqlite3_last_insert_rowid(s_db);
            s_gen.ok = true;
        }
        sqlite3_finalize(st);
    }
    xSemaphoreGive(s_gen_done);
}

static void do_gen_get(void)
{
    s_gen.ok = false;
    sqlite3_stmt *st;
    s_gen.has_regions = false;
    if (s_db && sqlite3_prepare_v2(s_db, "SELECT level, givens, day, variant, regions FROM generated WHERE id = ?1",
                                   -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_gen.id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *g = (const char *)sqlite3_column_text(st, 1);
            const char *day = (const char *)sqlite3_column_text(st, 2);
            snprintf(s_gen.day, sizeof(s_gen.day), "%s", day ? day : "");
            if (g && strlen(g) == 81) {
                s_gen.level = sqlite3_column_int(st, 0);
                s_gen.variant = sqlite3_column_int(st, 3);
                for (int i = 0; i < 81; i++) s_gen.givens[i] = (g[i] >= '0' && g[i] <= '9') ? g[i] - '0' : 0;
                const char *r = (const char *)sqlite3_column_text(st, 4);
                if (r && strlen(r) == 81) {
                    for (int i = 0; i < 81; i++) s_gen.regions[i] = (r[i] >= '0' && r[i] <= '8') ? r[i] - '0' : 0;
                    s_gen.has_regions = true;
                }
                s_gen.ok = true;
            }
        }
        sqlite3_finalize(st);
    }
    xSemaphoreGive(s_gen_done);
}

/* 'YYYY-MM-DD' of the day before (in place) */
static void prev_day(char *d)
{
    struct tm tm = {0};
    if (sscanf(d, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_mday -= 1;
    tm.tm_hour = 12;                              /* clear of DST changes */
    mktime(&tm);                                  /* normalises day 0 to the month before */
    strftime(d, 11, "%Y-%m-%d", &tm);
}

/* today's daily puzzle (s_gen.day) for the current player: its id (0 = not
 * made yet), solved today, and the streak of days in a row with the daily
 * solved, ending today or yesterday */
static void do_daily(void)
{
    s_gen.id = 0;
    s_gen.solved_today = false;
    s_gen.streak = 0;
    s_gen.ok = s_db != NULL;
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db, "SELECT id FROM generated WHERE day = ?1 ORDER BY id LIMIT 1",
                                   -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, s_gen.day, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) s_gen.id = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (s_db && sqlite3_prepare_v2(s_db,
            "SELECT DISTINCT g.day FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id"
            " WHERE s.player_id = ?1 AND g.day IS NOT NULL AND g.day <= ?2 ORDER BY g.day DESC",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_player);
        sqlite3_bind_text(st, 2, s_gen.day, -1, SQLITE_STATIC);
        char want[12];
        snprintf(want, sizeof(want), "%s", s_gen.day);
        bool first = true;
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *d = (const char *)sqlite3_column_text(st, 0);
            if (!d) break;
            if (first) {
                first = false;
                s_gen.solved_today = strcmp(d, want) == 0;
                if (!s_gen.solved_today) prev_day(want);   /* not yet today: a streak can end yesterday */
            }
            if (strcmp(d, want) != 0) break;
            s_gen.streak++;
            prev_day(want);
        }
        sqlite3_finalize(st);
    }
    xSemaphoreGive(s_gen_done);
}

/* ------------------------------------------------------------- daily rank and leaderboard */

/* everyone's best solve of the daily puzzle of s_gen.day (or *board's day), fastest first */
#define BOARD_SQL \
    "SELECT p.id, p.name, b.best, b.mistakes, b.hints, b.n FROM players p LEFT JOIN (" \
    "  SELECT player_id, MIN(seconds) AS best, mistakes, hints, COUNT(*) AS n FROM solves" \
    "   WHERE puzzle = ?1 GROUP BY player_id) b ON b.player_id = p.id" \
    " ORDER BY b.best IS NULL, b.best, b.mistakes, b.hints, p.id"

/* the daily puzzle of a day: its generated.id (0 = not made) and level */
static int daily_id_of(const char *day, int *level)
{
    int id = 0;
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db, "SELECT id, level FROM generated WHERE day = ?1 ORDER BY id LIMIT 1",
                                   -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, day, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            id = sqlite3_column_int(st, 0);
            if (level) *level = sqlite3_column_int(st, 1);
        }
        sqlite3_finalize(st);
    }
    return id;
}

static void board_fill(stats_board_t *b, const char *day)
{
    memset(b, 0, sizeof(*b));
    snprintf(b->day, sizeof(b->day), "%s", day);
    int gid = daily_id_of(day, &b->level);
    b->exists = gid > 0;
    sqlite3_stmt *st;
    if (!s_db || sqlite3_prepare_v2(s_db, BOARD_SQL, -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(st, 1, gid > 0 ? PUZZLE_GEN_BASE + gid : -1);
    while (b->n < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) {
        stats_board_row_t *r = &b->rows[b->n++];
        r->player_id = sqlite3_column_int(st, 0);
        const char *name = (const char *)sqlite3_column_text(st, 1);
        snprintf(r->player, sizeof(r->player), "%s", name ? name : "");
        r->solved = sqlite3_column_type(st, 2) != SQLITE_NULL;
        if (!r->solved) continue;
        b->solved++;
        r->seconds = (uint32_t)sqlite3_column_int64(st, 2);
        r->mistakes = (uint16_t)sqlite3_column_int(st, 3);
        r->hints = (uint16_t)sqlite3_column_int(st, 4);
        r->solves = (uint16_t)sqlite3_column_int(st, 5);
    }
    sqlite3_finalize(st);
}

static void do_board(const char *day)
{
    static stats_board_t *b;
    if (!psram_buf((void **)&b, sizeof(*b)) || !psram_buf((void **)&s_board, sizeof(*s_board))) return;
    board_fill(b, day);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *s_board = *b;
    s_board_new = true;
    xSemaphoreGive(s_lock);
}

/* the current player's place on s_gen.day's leaderboard */
static void do_rank(void)
{
    static stats_board_t *b;
    s_gen.rank = s_gen.of = 0;
    s_gen.ok = s_db != NULL && psram_buf((void **)&b, sizeof(*b));
    if (s_gen.ok) {
        board_fill(b, s_gen.day);
        s_gen.of = b->solved;
        for (int i = 0; i < b->n; i++) {
            if (b->rows[i].player_id == s_player && b->rows[i].solved) s_gen.rank = i + 1;
        }
    }
    xSemaphoreGive(s_gen_done);
}

/* ------------------------------------------------------------- achievements */
/*
 * Each one is a number worked out from the player's solves (?1 = player id)
 * and a goal; it is unlocked once the number reaches the goal. The streak
 * ones are worked out in C (longest run of days with the daily solved).
 * The keys are stored in the achievements table: don't rename them.
 */
#define HARD_OR_MORE "(level LIKE '%Hard' OR level LIKE '%Expert')"
#define CLASSIC_9X9  "(level NOT LIKE 'Mini %' AND level NOT LIKE 'X-Sudoku %' AND level NOT LIKE 'Hyper %'" \
                     " AND level NOT LIKE 'Jigsaw %')"
#define DAILY_DAYS \
    "SELECT DISTINCT g.day FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id" \
    " WHERE s.player_id = ?1 AND g.day IS NOT NULL ORDER BY g.day"

typedef struct {
    const char *key, *title, *desc;
    int32_t goal;
    uint8_t unit;                       /* ach_unit_t */
    const char *sql;                    /* one number; NULL = longest daily streak */
} ach_def_t;

static const ach_def_t ACH[] = {
    {"solve_1", "First steps", "Solve a puzzle.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1"},
    {"solve_10", "Getting the hang of it", "Solve 10 puzzles.", 10, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1"},
    {"solve_50", "Regular", "Solve 50 puzzles.", 50, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1"},
    {"solve_100", "Centurion", "Solve 100 puzzles.", 100, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1"},
    {"clean_1", "Clean sheet", "Solve a puzzle without a mistake or a hint.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND mistakes = 0 AND hints = 0"},
    {"clean_25", "Steady hand", "Solve 25 puzzles without a mistake or a hint.", 25, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND mistakes = 0 AND hints = 0"},
    {"hard_clean", "Hard and clean", "Solve a Hard, Very Hard or Expert puzzle without a mistake or a hint.", 1,
     ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND mistakes = 0 AND hints = 0 AND " HARD_OR_MORE},
    {"expert", "Expert", "Solve an Expert puzzle (New puzzle or the Sunday daily).", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND level LIKE '%Expert'"},
    {"quick", "Quick thinker", "Solve a 9x9 puzzle in under 5 minutes.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND seconds < 300 AND level NOT LIKE 'Mini %'"},
    {"fast_hard", "Speed demon", "Solve a Hard or harder puzzle in under 10 minutes, without hints.", 1,
     ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND seconds < 600 AND hints = 0 AND " HARD_OR_MORE},
    {"x_sudoku", "Crossed lines", "Solve an X-Sudoku.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND level LIKE 'X-Sudoku %'"},
    {"mini", "Small wonder", "Solve a Mini 6x6.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND level LIKE 'Mini %'"},
    {"variety", "Variety", "Solve a Classic, an X-Sudoku and a Mini 6x6.", 3, ACH_UNIT_COUNT,
     "SELECT EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " CLASSIC_9X9 ")"
     " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND level LIKE 'X-Sudoku %')"
     " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND level LIKE 'Mini %')"},
    {"hyper", "Window cleaner", "Solve a Hyper Sudoku.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND level LIKE 'Hyper %'"},
    {"jigsaw", "Puzzle pieces", "Solve a Jigsaw Sudoku.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND level LIKE 'Jigsaw %'"},
    {"variety_5", "Collector", "Solve all five kinds: Classic, X-Sudoku, Hyper, Jigsaw and Mini 6x6.", 5,
     ACH_UNIT_COUNT,
     "SELECT EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " CLASSIC_9X9 ")"
     " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND level LIKE 'X-Sudoku %')"
     " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND level LIKE 'Hyper %')"
     " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND level LIKE 'Jigsaw %')"
     " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND level LIKE 'Mini %')"},
    {"fresh_10", "Fresh off the press", "Solve 10 puzzles made with New puzzle.", 10, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id"
     " WHERE s.player_id = ?1 AND g.day IS NULL"},
    {"daily_10", "Daily regular", "Solve the daily puzzle on 10 different days.", 10, ACH_UNIT_DAYS,
     "SELECT COUNT(*) FROM (" DAILY_DAYS ")"},
    {"streak_3", "On a roll", "Solve the daily puzzle 3 days in a row.", 3, ACH_UNIT_DAYS, NULL},
    {"streak_7", "Week streak", "Solve the daily puzzle 7 days in a row.", 7, ACH_UNIT_DAYS, NULL},
    {"streak_30", "Monthly habit", "Solve the daily puzzle 30 days in a row.", 30, ACH_UNIT_DAYS, NULL},
    {"explorer", "Explorer", "Solve a puzzle in each of the 16 built-in collections.", PUZZLE_COLLECTION_COUNT,
     ACH_UNIT_COUNT,
     "SELECT COUNT(DISTINCT level) FROM puzzle_stats WHERE player_id = ?1 AND puzzle < 2000"},
    {"collection", "Completionist", "Solve all 100 puzzles of one built-in collection.", 100, ACH_UNIT_COUNT,
     "SELECT COALESCE(MAX(n), 0) FROM (SELECT COUNT(*) AS n FROM puzzle_stats"
     " WHERE player_id = ?1 AND puzzle < 2000 GROUP BY level)"},
    {"marathon", "Marathon", "Spend 10 hours solving puzzles (solve times added up).", 600, ACH_UNIT_MINUTES,
     "SELECT CAST(TOTAL(seconds) / 60 AS INTEGER) FROM solves WHERE player_id = ?1"},
    {"night_owl", "Night owl", "Finish a puzzle between midnight and 5 am.", 1, ACH_UNIT_COUNT,
     "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND finished_at IS NOT NULL"
     " AND CAST(strftime('%H', finished_at, 'unixepoch', 'localtime') AS INTEGER) < 5"},
};
#define ACH_COUNT ((int)(sizeof(ACH) / sizeof(ACH[0])))
_Static_assert(sizeof(ACH) / sizeof(ACH[0]) <= STATS_ACH_MAX, "raise STATS_ACH_MAX");

/* days since 1970-01-01 of 'YYYY-MM-DD' (proleptic Gregorian), or INT32_MIN */
static int32_t day_number(const char *d)
{
    int y, m, dd;
    if (!d || sscanf(d, "%d-%d-%d", &y, &m, &dd) != 3) return INT32_MIN;
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* the longest run of days in a row with the daily puzzle solved */
static int longest_daily_streak(int32_t player)
{
    sqlite3_stmt *st;
    int best = 0, run = 0;
    int32_t prev = INT32_MIN;
    if (!s_db || sqlite3_prepare_v2(s_db, DAILY_DAYS, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, player);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int32_t d = day_number((const char *)sqlite3_column_text(st, 0));
        if (d == INT32_MIN) continue;
        run = prev != INT32_MIN && d == prev + 1 ? run + 1 : 1;
        if (run > best) best = run;
        prev = d;
    }
    sqlite3_finalize(st);
    return best;
}

/* progress of every achievement for a player */
static void ach_values(int32_t player, int32_t *val)
{
    int streak = -1;
    for (int i = 0; i < ACH_COUNT; i++) {
        val[i] = 0;
        if (!ACH[i].sql) {
            if (streak < 0) streak = longest_daily_streak(player);
            val[i] = streak;
            continue;
        }
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(s_db, ACH[i].sql, -1, &st, NULL) != SQLITE_OK) {
            printf("stats_db: achievement %s: %s\n", ACH[i].key, sqlite3_errmsg(s_db));
            continue;
        }
        sqlite3_bind_int(st, 1, player);
        if (sqlite3_step(st) == SQLITE_ROW) val[i] = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
}

/* when each achievement was unlocked: 1 = unlocked (time unknown), 0 = not yet */
static void ach_unlocked(int32_t player, int64_t *when)
{
    for (int i = 0; i < ACH_COUNT; i++) when[i] = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "SELECT key, unlocked_at FROM achievements WHERE player_id = ?1", -1, &st,
                           NULL) != SQLITE_OK) return;
    sqlite3_bind_int(st, 1, player);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(st, 0);
        for (int i = 0; key && i < ACH_COUNT; i++) {
            if (strcmp(ACH[i].key, key) != 0) continue;
            int64_t t = sqlite3_column_type(st, 1) == SQLITE_NULL ? 0 : sqlite3_column_int64(st, 1);
            when[i] = t > 1 ? t : 1;
        }
    }
    sqlite3_finalize(st);
}

/* Records the achievements a player has reached and not recorded yet. With
 * announce, they go to stats_db_take_unlocked() and get the time now; without
 * (first fill), they are recorded quietly with no time. */
static void ach_sync(int32_t player, bool announce)
{
    if (!s_db) return;
    int32_t val[ACH_COUNT];
    int64_t when[ACH_COUNT];
    ach_values(player, val);
    ach_unlocked(player, when);
    time_t now = time(NULL);
    char titles[sizeof(s_unlocked)] = "";
    int n = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "INSERT OR IGNORE INTO achievements(player_id, key, unlocked_at) VALUES(?1, ?2, ?3)",
                           -1, &st, NULL) != SQLITE_OK) return;
    for (int i = 0; i < ACH_COUNT; i++) {
        if (when[i] || val[i] < ACH[i].goal) continue;
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, player);
        sqlite3_bind_text(st, 2, ACH[i].key, -1, SQLITE_STATIC);
        if (announce && now > 1700000000) sqlite3_bind_int64(st, 3, now);
        else sqlite3_bind_null(st, 3);
        if (!step_done(st, "achievement")) continue;
        if (!announce) continue;
        size_t len = strlen(titles);
        snprintf(titles + len, sizeof(titles) - len, "%s%s", n ? ", " : "", ACH[i].title);
        n++;
    }
    sqlite3_finalize(st);
    if (n) {
        printf("stats_db: unlocked %s\n", titles);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_unlocked, sizeof(s_unlocked), "%s", titles);
        s_unlocked_n = n;
        xSemaphoreGive(s_lock);
    }
}

/* the achievements already earned by every player, recorded quietly (at open) */
static void ach_backfill(void)
{
    int32_t ids[STATS_MAX_PLAYERS];
    int n = 0;
    sqlite3_stmt *st;
    if (!s_db || sqlite3_prepare_v2(s_db, "SELECT id FROM players", -1, &st, NULL) != SQLITE_OK) return;
    while (n < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) ids[n++] = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    for (int i = 0; i < n; i++) ach_sync(ids[i], false);
}

static void do_achievements(void)
{
    static stats_achievements_t *ap;
    if (!psram_buf((void **)&ap, sizeof(*ap)) || !psram_buf((void **)&s_ach, sizeof(*s_ach))) return;
    stats_achievements_t *const a = ap;
    memset(a, 0, sizeof(*a));
    a->player_id = s_player;
    if (s_db) {
        ach_sync(s_player, false);                 /* nothing missed (e.g. a restored sudoku.db) */
        int32_t val[ACH_COUNT];
        int64_t when[ACH_COUNT];
        ach_values(s_player, val);
        ach_unlocked(s_player, when);
        for (int i = 0; i < ACH_COUNT; i++) {
            stats_ach_t *x = &a->a[a->n++];
            x->title = ACH[i].title;
            x->desc = ACH[i].desc;
            x->goal = ACH[i].goal;
            x->unit = ACH[i].unit;
            x->unlocked = when[i] != 0;
            x->unlocked_at = when[i] > 1 ? when[i] : 0;
            x->value = x->unlocked || val[i] > ACH[i].goal ? ACH[i].goal : val[i];
            a->unlocked += x->unlocked;
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *s_ach = *a;
    s_ach_new = true;
    xSemaphoreGive(s_lock);
}

/* a page of recorded solves (newest first), for the Gameplays screen */
static void do_plays(int offset)
{
    static stats_plays_t *p;
    if (!p) p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM);
    if (!s_plays) s_plays = heap_caps_malloc(sizeof(*s_plays), MALLOC_CAP_SPIRAM);
    if (!p || !s_plays) return;
    memset(p, 0, sizeof(*p));
    p->offset = offset < 0 ? 0 : offset;
    sqlite3_stmt *st;
    if (s_db) {
        p->total = query_int("SELECT COUNT(*) FROM solves WHERE moves IS NOT NULL");
        if (p->total < 0) p->total = 0;
        if (sqlite3_prepare_v2(s_db,
                "SELECT s.started_at, s.finished_at, p.name, s.puzzle, s.seconds, s.mistakes, s.hints, s.id"
                " FROM solves s JOIN players p ON p.id = s.player_id WHERE s.moves IS NOT NULL"
                " ORDER BY s.id DESC LIMIT ?1 OFFSET ?2", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, STATS_MAX_DAY_ROWS);
            sqlite3_bind_int(st, 2, p->offset);
            while (sqlite3_step(st) == SQLITE_ROW && p->n < STATS_MAX_DAY_ROWS) {
                stats_day_row_t *r = &p->rows[p->n++];
                r->started_at = sqlite3_column_int64(st, 0);
                r->finished_at = sqlite3_column_int64(st, 1);
                const char *name = (const char *)sqlite3_column_text(st, 2);
                snprintf(r->player, sizeof(r->player), "%s", name ? name : "");
                r->puzzle = sqlite3_column_int(st, 3);
                r->seconds = (uint32_t)sqlite3_column_int64(st, 4);
                r->mistakes = sqlite3_column_int(st, 5);
                r->hints = sqlite3_column_int(st, 6);
                r->solve_id = sqlite3_column_int64(st, 7);
                r->has_moves = true;
            }
            sqlite3_finalize(st);
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *s_plays = *p;
    s_plays_new = true;
    xSemaphoreGive(s_lock);
}

/* one solve's move log and details, for the Replay screen */
static void do_replay(int64_t id)
{
    if (!moves_bufs()) return;
    static stats_replay_t *r;
    if (!r) r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM);
    if (!r) return;
    memset(r, 0, offsetof(stats_replay_t, moves));
    r->solve_id = id;
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db,
            "SELECT s.puzzle, s.seconds, s.mistakes, s.hints, s.moves, p.name, s.finished_at"
            " FROM solves s JOIN players p ON p.id = s.player_id WHERE s.id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            int pz = sqlite3_column_int(st, 0);
            const uint8_t *mb = (const uint8_t *)sqlite3_column_blob(st, 4);
            if (puzzle_id_valid(pz) && mb) {
                r->found = true;
                r->puzzle = (uint16_t)pz;
                r->seconds = (uint32_t)sqlite3_column_int64(st, 1);
                r->mistakes = (uint32_t)sqlite3_column_int(st, 2);
                r->hints = (uint32_t)sqlite3_column_int(st, 3);
                r->n = moves_unpack(mb, sqlite3_column_bytes(st, 4), r->moves, GAME_MOVES_MAX);
                const char *name = (const char *)sqlite3_column_text(st, 5);
                snprintf(r->player, sizeof(r->player), "%s", name ? name : "");
                r->finished_at = sqlite3_column_int64(st, 6);
            }
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_replay, r, offsetof(stats_replay_t, moves) + sizeof(game_move_t) * (size_t)r->n);
    s_replay_new = true;
    xSemaphoreGive(s_lock);
}

static void do_day(int year, int month, int day)
{
    static stats_day_t d;
    memset(&d, 0, sizeof(d));
    d.year = year;
    d.month = month;
    d.day = day;
    char ymd[12];
    snprintf(ymd, sizeof(ymd), "%04d-%02d-%02d", year, month, day);
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db,
            "SELECT started, finished, player, puzzle, seconds, mistakes, in_progress, hints, solve_id, has_moves FROM ("
            HISTORY_EVENTS ") WHERE date(t, 'unixepoch', 'localtime') = ?1 ORDER BY t",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, ymd, -1, SQLITE_STATIC);
        while (sqlite3_step(st) == SQLITE_ROW) {
            d.total++;
            if (d.n >= STATS_MAX_DAY_ROWS) continue;
            stats_day_row_t *r = &d.rows[d.n++];
            r->started_at = sqlite3_column_int64(st, 0);
            r->finished_at = sqlite3_column_int64(st, 1);
            snprintf(r->player, sizeof(r->player), "%s", (const char *)sqlite3_column_text(st, 2));
            r->puzzle = sqlite3_column_int(st, 3);
            r->seconds = (uint32_t)sqlite3_column_int64(st, 4);
            r->mistakes = sqlite3_column_int(st, 5);
            r->in_progress = sqlite3_column_int(st, 6) != 0;
            r->hints = sqlite3_column_int(st, 7);
            r->solve_id = sqlite3_column_int64(st, 8);
            r->has_moves = sqlite3_column_int(st, 9) != 0;
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_day = d;
    s_day_new = true;
    xSemaphoreGive(s_lock);
}

static void do_reset(open_seed_t *seed)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_game_dirty = false;                     /* a queued save of the old game is void */
    xSemaphoreGive(s_lock);
    db_close();
    if (bsp_sdcard_mount() == ESP_OK) {
        unlink(DB_PATH "-journal");
        int r = unlink(DB_PATH);
        printf("stats_db: factory reset: sudoku.db %s\n", r == 0 ? "deleted" : "was not there");
    }
    s_state = ST_OFF;
    s_player = 0;
    s_solves = -1;
    if (seed) seed->fresh = true;
    do_open(seed);                            /* a new skeleton (also frees the seed) */
}

static void worker(void *arg)
{
    sqlite3_initialize();
    cmd_t c;
    for (;;) {
        if (xQueueReceive(s_q, &c, portMAX_DELAY) != pdTRUE) continue;
        switch (c.type) {
        case CMD_OPEN:
            do_open(c.seed);
            break;
        case CMD_SET:
            if (s_db) set_setting(c.key, c.is_text, c.ival, c.text);
            break;
        case CMD_GAME:
            flush_game();
            break;
        case CMD_SOLVE:
            do_solve(&c);
            free(c.blob);
            break;
        case CMD_QUERY:
            do_query(c.level);
            break;
        case CMD_PLAYERS:
            do_list();
            break;
        case CMD_ADD_PLAYER:
            do_add(c.text);
            break;
        case CMD_RENAME_PLAYER:
            do_rename(c.ival, c.text);
            break;
        case CMD_SWITCH_PLAYER:
            if (c.ival != s_player) do_switch(c.ival);
            break;
        case CMD_DELETE_PLAYER:
            do_delete(c.ival);
            break;
        case CMD_MONTH:
            do_month(c.year, c.month);
            break;
        case CMD_RESET:
            do_reset(c.seed);
            break;
        case CMD_DAY:
            do_day(c.year, c.month, c.day);
            break;
        case CMD_REPLAY:
            do_replay(c.solve_id);
            break;
        case CMD_PLAYS:
            do_plays(c.ival);
            break;
        case CMD_GEN_ADD:
            do_gen_add();
            break;
        case CMD_GEN_GET:
            do_gen_get();
            break;
        case CMD_DAILY:
            do_daily();
            break;
        case CMD_RANK:
            do_rank();
            break;
        case CMD_BOARD:
            do_board(c.text);
            break;
        case CMD_ACH:
            do_achievements();
            break;
        case CMD_SUSPEND:
            flush_game();
            db_close();
            bsp_sdcard_unmount();
            s_state = ST_OFF;
            xSemaphoreGive(s_suspended);
            break;
        }
    }
}

/* ------------------------------------------------------------- API */

static bool send(const cmd_t *c)
{
    return s_q && xQueueSend(s_q, c, pdMS_TO_TICKS(100)) == pdTRUE;
}

static void start(bool fresh);

void stats_db_start(void)
{
    start(false);
}

void stats_db_start_fresh(void)
{
    start(true);
}

static void start(bool fresh)
{
    if (!s_q) {
        s_q = xQueueCreate(16, sizeof(cmd_t));
        s_suspended = xSemaphoreCreateBinary();
        s_gen_done = xSemaphoreCreateBinary();
        s_opened = xSemaphoreCreateBinary();
        s_lock = xSemaphoreCreateMutex();
        /* SQLite wants a deep stack (parser, B-tree balancing) */
        if (!s_q || !s_suspended || !s_opened || !s_lock ||
            xTaskCreate(worker, "stats_db", 16384, NULL, 3, NULL) != pdPASS) {
            s_state = ST_ERROR;
            return;
        }
    }
    if (s_state == ST_READY || s_state == ST_OPENING) return;
    /* what the board knows so far (old NVS values + this session) */
    open_seed_t *seed = malloc(sizeof(*seed));
    if (seed) {
        memset(seed, 0, sizeof(*seed));
        seed->fresh = fresh;
        if (!fresh) store_copy_stats(seed->stats.best, seed->stats.fewest, seed->stats.solves);
        store_get_prefs(&seed->prefs);
    }
    cmd_t c = {.type = CMD_OPEN, .seed = seed};
    xSemaphoreTake(s_opened, 0);                          /* clear a stale give */
    s_state = ST_OPENING;
    if (!send(&c)) {
        free(seed);
        s_state = ST_ERROR;
        xSemaphoreGive(s_opened);
    }
}

bool stats_db_wait_open(uint32_t timeout_ms)
{
    if (!s_opened) return false;
    if (xSemaphoreTake(s_opened, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    return s_state == ST_READY;
}

bool stats_db_take_prefs(store_prefs_t *out)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_prefs_new;
    if (fresh) *out = s_prefs_out;
    s_prefs_new = false;
    xSemaphoreGive(s_lock);
    return fresh;
}

void stats_db_set_int(const char *key, int value)
{
    cmd_t c = {.type = CMD_SET, .is_text = false, .ival = value};
    snprintf(c.key, sizeof(c.key), "%s", key);
    send(&c);
}

void stats_db_set_text(const char *key, const char *value)
{
    cmd_t c = {.type = CMD_SET, .is_text = true};
    snprintf(c.key, sizeof(c.key), "%s", key);
    snprintf(c.text, sizeof(c.text), "%s", value);
    send(&c);
}

void stats_db_save_game(const saved_game_t *g)
{
    if (!s_lock || !s_q) return;                          /* no database this session */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_game_next_has = g != NULL;
    if (g) s_game_next = *g;
    bool queued = s_game_dirty;
    s_game_dirty = true;
    xSemaphoreGive(s_lock);
    if (!queued) {
        cmd_t c = {.type = CMD_GAME};
        if (!send(&c)) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_game_dirty = false;
            xSemaphoreGive(s_lock);
        }
    }
}

bool stats_db_ready(void)
{
    return s_state == ST_READY;
}

void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes, uint32_t hints,
                           int64_t started_at, const game_move_t *moves, int nmoves)
{
    uint8_t *blob = NULL;
    int blob_bytes = 0;
    if (moves && nmoves > 0) {
        blob = heap_caps_malloc((size_t)nmoves * MOVE_BLOB_BYTES, MALLOC_CAP_SPIRAM);
        if (blob) blob_bytes = (int)moves_pack(moves, nmoves, blob);
    }
    time_t now = time(NULL);
    cmd_t c = {
        .type = CMD_SOLVE,
        .puzzle = puzzle,
        .seconds = seconds,
        .mistakes = mistakes,
        .hints = hints,
        .blob = blob,
        .blob_bytes = blob_bytes,
        .when = now > 1700000000 ? (int64_t)now : 0,      /* only once NTP has set the clock */
        .started = started_at,
    };
    if (!send(&c)) free(blob);
}

void stats_db_set_game_moves(uint16_t puzzle, const game_move_t *moves, int n)
{
    if (!s_lock || !moves_bufs()) return;
    if (n > GAME_MOVES_MAX) n = GAME_MOVES_MAX;
    if (n < 0 || !moves) n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (n) memcpy(s_moves_next, moves, sizeof(game_move_t) * n);
    s_moves_next_n = n;
    s_moves_next_puzzle = puzzle;
    xSemaphoreGive(s_lock);
}

int stats_db_copy_saved_moves(uint16_t *puzzle, game_move_t *out, int max)
{
    *puzzle = STORE_NO_GAME;
    if (!s_lock || !s_moves_loaded) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_moves_loaded_n < max ? s_moves_loaded_n : max;
    if (n > 0) memcpy(out, s_moves_loaded, sizeof(game_move_t) * n);
    *puzzle = s_moves_loaded_puzzle;
    xSemaphoreGive(s_lock);
    return n;
}

/* runs a generated-puzzle command and waits for the worker (up to 3 s) */
static bool gen_call(cmd_type_t type)
{
    if (!stats_db_ready() || !s_gen_done) return false;
    xSemaphoreTake(s_gen_done, 0);                         /* clear a stale give */
    cmd_t c = {.type = type};
    if (!send(&c)) return false;
    return xSemaphoreTake(s_gen_done, pdMS_TO_TICKS(3000)) == pdTRUE && s_gen.ok;
}

int stats_db_gen_add(int variant, int level, const uint8_t givens[81], const uint8_t *regions, const char *day)
{
    s_gen.variant = variant;
    s_gen.level = level;
    memcpy(s_gen.givens, givens, 81);
    s_gen.has_regions = regions != NULL;
    if (regions) memcpy(s_gen.regions, regions, 81);
    snprintf(s_gen.day, sizeof(s_gen.day), "%s", day ? day : "");
    return gen_call(CMD_GEN_ADD) ? s_gen.id : -1;
}

bool stats_db_gen_get(int id, uint8_t givens[81], uint8_t *regions, int *variant, int *level, char day[12])
{
    s_gen.id = id;
    if (!gen_call(CMD_GEN_GET)) return false;
    memcpy(givens, s_gen.givens, 81);
    if (regions && s_gen.has_regions) memcpy(regions, s_gen.regions, 81);
    if (variant) *variant = s_gen.variant;
    if (level) *level = s_gen.level;
    if (day) snprintf(day, 12, "%s", s_gen.day);
    return true;
}

bool stats_db_daily_status(const char *day, int *id, bool *solved_today, int *streak)
{
    snprintf(s_gen.day, sizeof(s_gen.day), "%s", day);
    if (!gen_call(CMD_DAILY)) return false;
    *id = s_gen.id;
    *solved_today = s_gen.solved_today;
    *streak = s_gen.streak;
    return true;
}

bool stats_db_daily_rank(const char *day, int *rank, int *of)
{
    snprintf(s_gen.day, sizeof(s_gen.day), "%s", day);
    if (!gen_call(CMD_RANK)) return false;
    *rank = s_gen.rank;
    *of = s_gen.of;
    return true;
}

void stats_db_query_board(const char *day)
{
    cmd_t c = {.type = CMD_BOARD};
    snprintf(c.text, sizeof(c.text), "%s", day ? day : "");
    send(&c);
}

bool stats_db_take_board(stats_board_t *out)
{
    if (!s_lock || !s_board) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_board_new;
    if (fresh) *out = *s_board;
    s_board_new = false;
    xSemaphoreGive(s_lock);
    return fresh;
}

void stats_db_query_achievements(void)
{
    cmd_t c = {.type = CMD_ACH};
    send(&c);
}

bool stats_db_take_achievements(stats_achievements_t *out)
{
    if (!s_lock || !s_ach) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_ach_new;
    if (fresh) *out = *s_ach;
    s_ach_new = false;
    xSemaphoreGive(s_lock);
    return fresh;
}

bool stats_db_take_unlocked(char *buf, int len, int *count)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool any = s_unlocked_n > 0;
    if (any) {
        snprintf(buf, len, "%s", s_unlocked);
        if (count) *count = s_unlocked_n;
    }
    s_unlocked_n = 0;
    xSemaphoreGive(s_lock);
    return any;
}

void stats_db_query_plays(int offset)
{
    cmd_t c = {.type = CMD_PLAYS, .ival = offset};
    send(&c);
}

bool stats_db_take_plays(stats_plays_t *out)
{
    if (!s_lock || !s_plays) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_plays_new;
    if (fresh) {
        *out = *s_plays;
        s_plays_new = false;
    }
    xSemaphoreGive(s_lock);
    return fresh;
}

void stats_db_query_replay(int64_t solve_id)
{
    cmd_t c = {.type = CMD_REPLAY, .solve_id = solve_id};
    send(&c);
}

bool stats_db_take_replay(stats_replay_t *out)
{
    if (!s_lock || !s_replay) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_replay_new;
    if (fresh) {
        size_t head = offsetof(stats_replay_t, moves);
        memcpy(out, s_replay, head + sizeof(game_move_t) * (size_t)s_replay->n);
        s_replay_new = false;
    }
    xSemaphoreGive(s_lock);
    return fresh;
}

void stats_db_query(int level)
{
    cmd_t c = {.type = CMD_QUERY, .level = level};
    send(&c);
}

bool stats_db_take_view(stats_view_t *out)
{
    if (!s_lock || !s_view) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_view_new;
    if (fresh) *out = *s_view;
    s_view_new = false;
    xSemaphoreGive(s_lock);
    return fresh;
}

bool stats_db_take_cache(uint16_t *best, uint8_t *fewest, uint8_t *solves)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stats_arrays_t *a = s_cache;
    s_cache = NULL;
    xSemaphoreGive(s_lock);
    if (!a) return false;
    memcpy(best, a->best, sizeof(a->best));
    memcpy(fewest, a->fewest, sizeof(a->fewest));
    memcpy(solves, a->solves, sizeof(a->solves));
    free(a);
    return true;
}

void stats_db_list_players(void)
{
    cmd_t c = {.type = CMD_PLAYERS};
    send(&c);
}

void stats_db_add_player(const char *name)
{
    cmd_t c = {.type = CMD_ADD_PLAYER};
    snprintf(c.text, sizeof(c.text), "%s", name);
    send(&c);
}

void stats_db_rename_player(int32_t id, const char *name)
{
    cmd_t c = {.type = CMD_RENAME_PLAYER, .ival = id};
    snprintf(c.text, sizeof(c.text), "%s", name);
    send(&c);
}

void stats_db_switch_player(int32_t id)
{
    cmd_t c = {.type = CMD_SWITCH_PLAYER, .ival = id};
    send(&c);
}

void stats_db_delete_player(int32_t id)
{
    cmd_t c = {.type = CMD_DELETE_PLAYER, .ival = id};
    send(&c);
}

int stats_db_take_players(stats_player_t *out)
{
    if (!s_lock) return -1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_players_new ? s_players_n : -1;
    if (n > 0) memcpy(out, s_players, sizeof(stats_player_t) * n);
    s_players_new = false;
    xSemaphoreGive(s_lock);
    return n;
}

bool stats_db_take_player_change(stats_player_change_t *out)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_change_new;
    if (fresh) *out = s_change;
    s_change_new = false;
    xSemaphoreGive(s_lock);
    return fresh;
}

bool stats_db_take_message(char *buf, int len)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool any = s_message[0] != 0;
    if (any) snprintf(buf, len, "%s", s_message);
    s_message[0] = 0;
    xSemaphoreGive(s_lock);
    return any;
}

void stats_db_query_month(int year, int month)
{
    cmd_t c = {.type = CMD_MONTH, .year = year, .month = month};
    send(&c);
}

bool stats_db_take_month(stats_month_t *out)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_month_new;
    if (fresh) *out = s_month;
    s_month_new = false;
    xSemaphoreGive(s_lock);
    return fresh;
}

void stats_db_query_day(int year, int month, int day)
{
    cmd_t c = {.type = CMD_DAY, .year = year, .month = month, .day = day};
    send(&c);
}

bool stats_db_take_day(stats_day_t *out)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_day_new;
    if (fresh) *out = s_day;
    s_day_new = false;
    xSemaphoreGive(s_lock);
    return fresh;
}

void stats_db_factory_reset(void)
{
    if (!s_q) return;
    open_seed_t *seed = malloc(sizeof(*seed));
    if (seed) {
        memset(seed, 0, sizeof(*seed));
        seed->fresh = true;
        store_get_prefs(&seed->prefs);        /* the defaults, after store_factory_reset() */
    }
    cmd_t c = {.type = CMD_RESET, .seed = seed};
    xSemaphoreTake(s_opened, 0);
    s_state = ST_OPENING;
    if (!send(&c)) {
        free(seed);
        s_state = ST_ERROR;
    }
}

void stats_db_suspend(void)
{
    if (!s_q) return;
    xSemaphoreTake(s_suspended, 0);                       /* clear a stale give */
    cmd_t c = {.type = CMD_SUSPEND};
    if (send(&c)) xSemaphoreTake(s_suspended, pdMS_TO_TICKS(5000));
}

void stats_db_status(char *buf, int len)
{
    switch (s_state) {
    case ST_READY:
        snprintf(buf, len, "Saving to sudoku.db: %d solve%s logged", s_solves, s_solves == 1 ? "" : "s");
        break;
    case ST_OPENING: snprintf(buf, len, "Opening sudoku.db..."); break;
    case ST_NOT_FAT: snprintf(buf, len, "No FAT32 volume: format it to keep game data"); break;
    case ST_ERROR:   snprintf(buf, len, "Can't use the card for game data"); break;
    case ST_OFF:
    default:         snprintf(buf, len, "No card: game data is lost at power-off"); break;
    }
}

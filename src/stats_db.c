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
#include "sqlite/sqlite3.h"
#include "bsp/bsp_board.h"
#include "store.h"

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
    CMD_MONTH, CMD_DAY, CMD_RESET,
} cmd_type_t;
typedef struct {
    cmd_type_t type;
    uint16_t puzzle;
    uint32_t seconds, mistakes;
    int64_t when;               /* CMD_SOLVE: finished, Unix time, 0 = unknown */
    int64_t started;            /* CMD_SOLVE: started, Unix time, 0 = unknown */
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
static stats_view_t s_view;
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

/* table layouts (user_version 4), shared by SCHEMA and the upgrade below */
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
    "  started_at INTEGER)"                 /* when it was opened (or restarted) */
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
    "  started_at INTEGER)"

static const char SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS " T_PLAYERS ";"
    "CREATE TABLE IF NOT EXISTS " T_SOLVES ";"
    "CREATE TABLE IF NOT EXISTS " T_PUZZLE_STATS ";"
    "CREATE TABLE IF NOT EXISTS " T_SAVED_GAME ";"
    "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value);";

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
    "PRAGMA user_version = 4;";

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
    "INSERT INTO solves(player_id, puzzle, level, number, seconds, mistakes, finished_at, started_at)"
    " VALUES(?8, ?1, ?2, ?3, ?4, ?5, ?6, ?7)";

static void bind_puzzle(sqlite3_stmt *st, uint16_t id)
{
    int c = puzzle_collection_of(id);
    sqlite3_bind_int(st, 1, id);
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

static void save_game(const saved_game_t *g)
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
            " mistakes, saved_at, started_at) VALUES(?10, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
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
    step_done(st, "save game");
    sqlite3_finalize(st);
}

/* the current player's unfinished game; g->id = STORE_NO_GAME if none */
static void load_game(saved_game_t *g)
{
    memset(g, 0, sizeof(*g));
    g->id = STORE_NO_GAME;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "SELECT puzzle, cells, notes, elapsed_ms, mistakes, started_at FROM saved_game"
                                 " WHERE player_id = ?1", -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int(st, 1, s_player);
    if (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        const char *cells = (const char *)sqlite3_column_text(st, 1);
        const uint8_t *notes = (const uint8_t *)sqlite3_column_blob(st, 2);
        if (id >= 0 && id < PUZZLE_COUNT && cells && strlen(cells) == 81 && notes &&
            sqlite3_column_bytes(st, 2) == 162) {
            g->id = id;
            for (int i = 0; i < 81; i++) {
                g->val[i] = (cells[i] >= '0' && cells[i] <= '9') ? cells[i] - '0' : 0;
                g->notes[i] = notes[2 * i] | (notes[2 * i + 1] << 8);
            }
            g->elapsed_ms = (uint32_t)sqlite3_column_int64(st, 3);
            g->mistakes = (uint16_t)sqlite3_column_int(st, 4);
            g->started_at = sqlite3_column_int64(st, 5);      /* NULL reads as 0 = unknown */
        }
    }
    sqlite3_finalize(st);
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
 * tables, so bring them up to date (user_version 1-3 -> 4). Their data
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
                   " saved_at, started_at) SELECT 1, puzzle, level, number, cells, notes, elapsed_ms, mistakes,"
                   " saved_at, started_at FROM saved_game_v3");
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
    set_setting("time_zone", true, 0, p->timezone);
    set_setting("last_level", true, 0, puzzle_collections[p->last_collection < PUZZLE_COLLECTION_COUNT
                                                          ? p->last_collection : 0].name);
    save_game(p->game.id < PUZZLE_COUNT ? &p->game : NULL);
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
    bool ok = step_done(s_solve_insert, "insert solve");
    bind_puzzle(s_solve_upsert, c->puzzle);
    sqlite3_bind_int64(s_solve_upsert, 4, c->seconds ? c->seconds : 1);
    sqlite3_bind_int64(s_solve_upsert, 5, c->mistakes);
    sqlite3_bind_int(s_solve_upsert, 6, s_player);
    ok = step_done(s_solve_upsert, "update puzzle_stats") && ok;
    if (ok && exec("COMMIT")) {
        if (s_solves >= 0) s_solves++;
    } else {
        exec("ROLLBACK");
    }
}

static void do_query(int level)
{
    static stats_view_t v;                    /* worker-side scratch */
    memset(&v, 0, sizeof(v));
    v.level = level;
    v.player_id = s_player;

    /* per level: same aggregates the screen shows (closed database: empty result) */
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db,
            "SELECT level, COUNT(*), TOTAL(fewest_mistakes = 0), COUNT(fewest_mistakes),"
            "       TOTAL(solves), TOTAL(best_seconds), MIN(best_seconds), TOTAL(fewest_mistakes)"
            " FROM puzzle_stats WHERE player_id = ?1 GROUP BY level", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_player);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *name = (const char *)sqlite3_column_text(st, 0);
            int c = 0;
            while (c < PUZZLE_COLLECTION_COUNT && strcmp(puzzle_collections[c].name, name ? name : "") != 0) c++;
            if (c == PUZZLE_COLLECTION_COUNT) continue;   /* a level this firmware doesn't have */
            stats_level_t *l = &v.levels[c];
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
        stats_level_t *l = &v.levels[c], *t = &v.total;
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
        while (v.n_puzzles < STATS_MAX_PUZZLES && sqlite3_step(st) == SQLITE_ROW) {
            stats_puzzle_t *p = &v.puzzles[v.n_puzzles++];
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

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_view = v;
    s_view_new = true;
    xSemaphoreGive(s_lock);
}

/* writes the newest queued game, if any (for the player it belongs to) */
static void flush_game(void)
{
    static saved_game_t g;                    /* worker-side copy */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool dirty = s_game_dirty, has = s_game_next_has;
    g = s_game_next;
    s_game_dirty = false;
    xSemaphoreGive(s_lock);
    if (dirty && s_db) save_game(has ? &g : NULL);
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
    "       s.puzzle, s.seconds, s.mistakes, 0 AS in_progress" \
    "  FROM solves s JOIN players p ON p.id = s.player_id WHERE s.finished_at IS NOT NULL" \
    " UNION ALL " \
    "SELECT g.started_at, g.started_at, NULL, p.name, g.puzzle, g.elapsed_ms / 1000, g.mistakes, 1" \
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
            "SELECT started, finished, player, puzzle, seconds, mistakes, in_progress FROM ("
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

void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes, int64_t started_at)
{
    time_t now = time(NULL);
    cmd_t c = {
        .type = CMD_SOLVE,
        .puzzle = puzzle,
        .seconds = seconds,
        .mistakes = mistakes,
        .when = now > 1700000000 ? (int64_t)now : 0,      /* only once NTP has set the clock */
        .started = started_at,
    };
    send(&c);
}

void stats_db_query(int level)
{
    cmd_t c = {.type = CMD_QUERY, .level = level};
    send(&c);
}

bool stats_db_take_view(stats_view_t *out)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fresh = s_view_new;
    if (fresh) *out = s_view;
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

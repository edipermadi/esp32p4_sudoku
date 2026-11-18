/*
 * Statistics database on the microSD card (see stats_db.h).
 */
#include "stats_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    bool fresh;                 /* after a format: no statistics, settings only */
} open_seed_t;

typedef enum { CMD_OPEN, CMD_SOLVE, CMD_QUERY, CMD_SUSPEND, CMD_SET, CMD_GAME } cmd_type_t;
typedef struct {
    cmd_type_t type;
    uint16_t puzzle;
    uint32_t seconds, mistakes;
    int64_t when;               /* CMD_SOLVE: finished, Unix time, 0 = unknown */
    int64_t started;            /* CMD_SOLVE: started, Unix time, 0 = unknown */
    int level;                  /* CMD_QUERY */
    open_seed_t *seed;          /* CMD_OPEN */
    char key[24];               /* CMD_SET */
    bool is_text;
    int ival;
    char text[STORE_TZ_LEN];
} cmd_t;

typedef enum { ST_OFF, ST_OPENING, ST_READY, ST_NOT_FAT, ST_ERROR } db_state_t;

static QueueHandle_t s_q;
static SemaphoreHandle_t s_suspended, s_opened, s_lock;  /* s_lock guards the results below */
static volatile db_state_t s_state = ST_OFF;
static volatile int s_solves = -1;            /* rows in solves, -1 unknown */
static sqlite3 *s_db;
static sqlite3_stmt *s_merge, *s_solve_upsert, *s_solve_insert;

/* results handed to the LVGL task */
static stats_view_t s_view;
static bool s_view_new;
static stats_arrays_t *s_cache;               /* set after open, taken once */
static store_prefs_t s_prefs_out;
static bool s_prefs_new;
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

static const char SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS solves("
    "  id INTEGER PRIMARY KEY,"
    "  puzzle INTEGER NOT NULL,"          /* 0..1599, as in the firmware */
    "  level TEXT NOT NULL,"              /* collection, e.g. 'Gnome 2 Hard' */
    "  number INTEGER NOT NULL,"          /* 1..100 within the level */
    "  seconds INTEGER NOT NULL,"
    "  mistakes INTEGER NOT NULL,"
    "  finished_at INTEGER,"              /* Unix time UTC; NULL = clock not set */
    "  started_at INTEGER"                /* when it was opened (or restarted) */
    ");"
    "CREATE INDEX IF NOT EXISTS solves_puzzle ON solves(puzzle);"
    "CREATE TABLE IF NOT EXISTS puzzle_stats("
    "  puzzle INTEGER PRIMARY KEY,"
    "  level TEXT NOT NULL,"
    "  number INTEGER NOT NULL,"
    "  best_seconds INTEGER NOT NULL,"
    "  fewest_mistakes INTEGER,"          /* NULL = solved before mistakes were counted */
    "  solves INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS puzzle_stats_level ON puzzle_stats(level, number);"
    "CREATE VIEW IF NOT EXISTS level_stats AS"
    "  SELECT level, COUNT(*) AS solved, SUM(solves) AS solves,"
    "         MIN(best_seconds) AS fastest_seconds,"
    "         CAST(ROUND(AVG(best_seconds)) AS INTEGER) AS average_seconds,"
    "         ROUND(AVG(fewest_mistakes), 1) AS average_mistakes,"
    "         SUM(fewest_mistakes = 0) AS without_mistakes"
    "  FROM puzzle_stats GROUP BY level ORDER BY MIN(puzzle);"
    "CREATE TABLE IF NOT EXISTS saved_game("
    "  slot INTEGER PRIMARY KEY CHECK (slot = 1),"
    "  puzzle INTEGER NOT NULL,"
    "  level TEXT NOT NULL,"
    "  number INTEGER NOT NULL,"
    "  cells TEXT NOT NULL,"              /* 81 digits, row by row, 0 = empty, givens included */
    "  notes BLOB NOT NULL,"              /* 81 x uint16 LE, bit d = pencil mark d */
    "  elapsed_ms INTEGER NOT NULL,"
    "  mistakes INTEGER NOT NULL,"
    "  saved_at INTEGER,"                 /* Unix time UTC; NULL = clock not set */
    "  started_at INTEGER"
    ");"
    "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value);"
    "PRAGMA user_version = 3;";

/* MIN() of SQL is NULL if either side is: keep the known value then */
#define FEWEST_MERGED "COALESCE(MIN(fewest_mistakes, excluded.fewest_mistakes), fewest_mistakes, excluded.fewest_mistakes)"

static const char SQL_MERGE[] =       /* idempotent: safe to run at every start */
    "INSERT INTO puzzle_stats(puzzle, level, number, best_seconds, fewest_mistakes, solves)"
    " VALUES(?1, ?2, ?3, ?4, ?5, ?6)"
    " ON CONFLICT(puzzle) DO UPDATE SET best_seconds = MIN(best_seconds, excluded.best_seconds),"
    "   fewest_mistakes = " FEWEST_MERGED ", solves = MAX(solves, excluded.solves)";

static const char SQL_SOLVE_UPSERT[] =
    "INSERT INTO puzzle_stats(puzzle, level, number, best_seconds, fewest_mistakes, solves)"
    " VALUES(?1, ?2, ?3, ?4, ?5, 1)"
    " ON CONFLICT(puzzle) DO UPDATE SET best_seconds = MIN(best_seconds, excluded.best_seconds),"
    "   fewest_mistakes = " FEWEST_MERGED ", solves = solves + 1";

static const char SQL_SOLVE_INSERT[] =
    "INSERT INTO solves(puzzle, level, number, seconds, mistakes, finished_at, started_at)"
    " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)";

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
        if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
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
    if (!g) {
        exec("DELETE FROM saved_game");
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
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db,
            "INSERT OR REPLACE INTO saved_game(slot, puzzle, level, number, cells, notes, elapsed_ms, mistakes,"
            " saved_at, started_at) VALUES(1, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)", -1, &st, NULL) != SQLITE_OK) {
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
    step_done(st, "save game");
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
    p->game.id = STORE_NO_GAME;
    if (sqlite3_prepare_v2(s_db, "SELECT puzzle, cells, notes, elapsed_ms, mistakes, started_at FROM saved_game"
                                 " WHERE slot = 1",
                           -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            int id = sqlite3_column_int(st, 0);
            const char *cells = (const char *)sqlite3_column_text(st, 1);
            const uint8_t *notes = (const uint8_t *)sqlite3_column_blob(st, 2);
            if (id >= 0 && id < PUZZLE_COUNT && cells && strlen(cells) == 81 && notes &&
                sqlite3_column_bytes(st, 2) == 162) {
                saved_game_t *g = &p->game;
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
 * tables, so bring the columns up to date (user_version 1-2 -> 3). */
static bool upgrade_schema(void)
{
    bool ok = true;
    if (!has_column("solves", "finished_at") && has_column("solves", "solved_at")) {
        ok &= exec("ALTER TABLE solves RENAME COLUMN solved_at TO finished_at");
    }
    if (!has_column("solves", "started_at")) ok &= exec("ALTER TABLE solves ADD COLUMN started_at INTEGER");
    if (!has_column("saved_game", "started_at")) ok &= exec("ALTER TABLE saved_game ADD COLUMN started_at INTEGER");
    return ok;
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
        step_done(s_merge, "merge");
    }
    exec("COMMIT");
}

/* puzzle_stats -> arrays for the RAM copy */
static void load_cache(void)
{
    stats_arrays_t *a = calloc(1, sizeof(*a));
    if (!a) return;
    memset(a->fewest, STORE_MISTAKES_UNKNOWN, sizeof(a->fewest));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(s_db, "SELECT puzzle, best_seconds, fewest_mistakes, solves FROM puzzle_stats",
                           -1, &st, NULL) == SQLITE_OK) {
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
     * with the current settings and game but no statistics. */
    if (seed && !existed) seed->fresh = true;
    if (sqlite3_open_v2(DB_PATH, &s_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        printf("stats_db: open failed: %s\n", s_db ? sqlite3_errmsg(s_db) : "no memory");
        db_close();
        s_state = ST_ERROR;
        return;
    }
    if (!exec(SCHEMA) || !upgrade_schema() ||
        sqlite3_prepare_v2(s_db, SQL_MERGE, -1, &s_merge, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(s_db, SQL_SOLVE_UPSERT, -1, &s_solve_upsert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(s_db, SQL_SOLVE_INSERT, -1, &s_solve_insert, NULL) != SQLITE_OK) {
        printf("stats_db: setup failed: %s\n", sqlite3_errmsg(s_db));
        db_close();
        s_state = ST_ERROR;
        return;
    }
    if (seed) {
        /* once per database (the marker is written with the settings); a
         * database made after a format must not get the old statistics back */
        if (query_int("SELECT COUNT(*) FROM settings WHERE key = 'migrated_from_nvs'") <= 0) {
            if (!seed->fresh) merge_legacy(&seed->stats);
            migrate_prefs(&seed->prefs);
            printf("stats_db: new database: %s\n", seed->fresh
                   ? "current settings stored, statistics start fresh"
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
    int puzzles = query_int("SELECT COUNT(*) FROM puzzle_stats");
    s_state = ST_READY;
    printf("stats_db: %s open, %d solved puzzles, %d solves logged\n", DB_PATH, puzzles, s_solves);
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
    bool ok = step_done(s_solve_insert, "insert solve");
    bind_puzzle(s_solve_upsert, c->puzzle);
    sqlite3_bind_int64(s_solve_upsert, 4, c->seconds ? c->seconds : 1);
    sqlite3_bind_int64(s_solve_upsert, 5, c->mistakes);
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

    /* per level: same aggregates the screen shows (closed database: empty result) */
    sqlite3_stmt *st;
    if (s_db && sqlite3_prepare_v2(s_db,
            "SELECT level, COUNT(*), TOTAL(fewest_mistakes = 0), COUNT(fewest_mistakes),"
            "       TOTAL(solves), TOTAL(best_seconds), MIN(best_seconds), TOTAL(fewest_mistakes)"
            " FROM puzzle_stats GROUP BY level", -1, &st, NULL) == SQLITE_OK) {
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
            " WHERE level = ?1 ORDER BY number", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, puzzle_collections[level].name, -1, SQLITE_STATIC);
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
        case CMD_GAME: {
            static saved_game_t g;               /* worker-side copy of the newest game */
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool has = s_game_next_has;
            g = s_game_next;
            s_game_dirty = false;
            xSemaphoreGive(s_lock);
            if (s_db) save_game(has ? &g : NULL);
            break;
        }
        case CMD_SOLVE:
            do_solve(&c);
            break;
        case CMD_QUERY:
            do_query(c.level);
            break;
        case CMD_SUSPEND:
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

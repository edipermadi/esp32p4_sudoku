/**
 * @file stats_db.c
 * @brief The game's database on the microSD card (see stats_db.h): the worker task, opening and closing,
 *        and the calls every part shares. The parts are listed in stats_db_internal.h.
 */
#include "stats_db_internal.h"

static const char *TAG = "stats_db";          /**< the log's tag */

/** @brief Where the database is. */
typedef enum {
    ST_OFF,                                     /**< not started, or closed */
    ST_OPENING,                                 /**< being opened */
    ST_READY,                                   /**< open */
    ST_NOT_FAT,                                 /**< the card isn't FAT: no database */
    ST_ERROR                                    /**< it couldn't be opened */
} db_state_t;

static QueueHandle_t s_q;                       /**< the worker's commands */
static SemaphoreHandle_t s_suspended;           /**< given when the database is closed (stats_db_suspend()) */
static SemaphoreHandle_t s_opened;              /**< given when an open has finished (stats_db_wait_open()) */
static SemaphoreHandle_t s_lock;                /**< guards the results handed to the UI */
static volatile db_state_t s_state = ST_OFF;    /**< where the database is */
static volatile bool s_working;                 /**< the worker is on a command (is_stats_db_busy()) */
static volatile int s_solves = -1;              /**< rows in solves (all players), -1 unknown */
static sqlite3 *s_db;                           /**< the database (NULL: not open) */
static int32_t s_player;                        /**< worker: who is playing (players.id) */
static char s_message[96];                      /**< a message for the user (stats_db_take_message()) */
static gen_call_t *s_genp;                      /**< the waited-for call (PSRAM: internal RAM is tight), made with s_gen_done */
static SemaphoreHandle_t s_gen_done;            /**< given when the worker has answered it */

/* (documented in stats_db_internal.h) */
void *sdb_psram_buf(void **p, size_t size)
{
    if (!*p) {
        *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM);
    }
    return *p;
}

/* ------------------------------------------------------------- worker side */

/* (documented in stats_db_internal.h) */
bool sdb_exec(const char *sql)
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

/* (documented in stats_db_internal.h) */
bool sdb_step_done(sqlite3_stmt *st, const char *what)
{
    int rc = sqlite3_step(st);
    sqlite3_reset(st);
    if (rc != SQLITE_DONE) {
        printf("stats_db: %s failed: %s\n", what, sqlite3_errmsg(s_db));
        return false;
    }
    return true;
}

/* (documented in stats_db_internal.h) */
int sdb_query_int(const char *sql)
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

/** @brief Closes the database (its statements first). */
static void db_close(void)
{
    sdb_solves_finalize();
    if (s_db) {
        sqlite3_close(s_db);
    }
    s_db = NULL;
}

/* (documented in stats_db_internal.h) */
void sdb_post_message(const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_message, sizeof(s_message), "%s", msg);
    xSemaphoreGive(s_lock);
}

static void do_open_inner(open_seed_t *seed);

/**
 * @brief CMD_OPEN: opens the database, then frees the seed and wakes stats_db_wait_open().
 * @param[in] seed  what the board has (NULL: a reopen)
 */
static void do_open(open_seed_t *seed)
{
    do_open_inner(seed);
    free(seed);
    xSemaphoreGive(s_opened);
}

/**
 * @brief Opens (or creates) the database: the tables, the player, the settings and the cache.
 * @param[in] seed  what the board has (NULL: a reopen)
 */
static void do_open_inner(open_seed_t *seed)
{
    if (s_db) {
        return;
    }
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
    if (seed && !existed) {
        seed->fresh = true;
    }
    if (sqlite3_open_v2(DB_PATH, &s_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        printf("stats_db: open failed: %s\n", s_db ? sqlite3_errmsg(s_db) : "no memory");
        db_close();
        s_state = ST_ERROR;
        return;
    }
    if (!sdb_schema_create() || !sdb_solves_prepare()) {
        printf("stats_db: setup failed: %s\n", sqlite3_errmsg(s_db));
        db_close();
        s_state = ST_ERROR;
        return;
    }
    sdb_select_current_player(seed ? seed->prefs.player_name : NULL);
    if (seed) {
        /* once per database (the marker is written with the settings); a
         * database made after a format must not get the old statistics back */
        if (sdb_query_int("SELECT COUNT(*) FROM settings WHERE key = 'migrated_from_nvs'") <= 0) {
            if (!seed->fresh) {
                sdb_merge_legacy(&seed->stats);
            }
            sdb_migrate_prefs(&seed->prefs);
            printf("stats_db: new database: %s\n", seed->fresh
                   ? "current settings and player stored, statistics start fresh"
                   : "statistics, settings and game copied in from the board");
        }
        store_prefs_t p = seed->prefs;
        sdb_load_prefs(&p);
        sdb_prefs_post(&p);
    }
    sdb_load_cache();
    sdb_ach_backfill();                                 /* earned before this firmware: recorded quietly */
    s_solves = sdb_query_int("SELECT COUNT(*) FROM solves");
    int players = sdb_query_int("SELECT COUNT(*) FROM players");
    s_state = ST_READY;
    printf("stats_db: %s open, %d player%s, playing as \"%s\", %d solves logged\n", DB_PATH,
           players, players == 1 ? "" : "s", sdb_player_name(), s_solves);
}

_Static_assert(GAME_FLAG_NO_NOTES == 1048576 && TECH_X_WING == 10 && TECH_SWORDFISH == 11 && TECH_XY_WING == 12,
               "the achievements' SQL above uses these numbers");

/**
 * @brief CMD_RESET: deletes sudoku.db and opens a new skeleton.
 * @param[in] seed  what the board has now
 */
static void do_reset(open_seed_t *seed)
{
    sdb_game_forget();                            /* a queued save of the old game is void */
    db_close();
    if (bsp_sdcard_mount() == ESP_OK) {
        unlink(DB_PATH "-journal");
        int r = unlink(DB_PATH);
        printf("stats_db: factory reset: sudoku.db %s\n", r == 0 ? "deleted" : "was not there");
    }
    s_state = ST_OFF;
    s_player = 0;
    s_solves = -1;
    if (seed) {
        seed->fresh = true;
    }
    do_open(seed);                            /* a new skeleton (also frees the seed) */
}

/**
 * @brief The worker task: runs the queued commands, one at a time.
 * @param[in] arg  unused
 */
static void worker(void *arg)
{
    sqlite3_initialize();
    cmd_t c;
    for (;;) {
        s_working = false;
        if (xQueueReceive(s_q, &c, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_working = true;
        switch (c.type) {
        case CMD_OPEN:
            do_open(c.seed);
            break;
        case CMD_SET:
            if (s_db) {
                sdb_set_setting(c.key, c.is_text, c.ival, c.text);
            }
            break;
        case CMD_LEARNED:
            sdb_do_learned(c.ival);
            break;
        case CMD_INSIGHTS:
            sdb_do_insights();
            break;
        case CMD_WEEKSUM:
            sdb_do_weeksum();
            break;
        case CMD_BM_GET:
        case CMD_BM_LIST:
        case CMD_SEEN_GET:
        case CMD_ATTACK_BOARD:
            sdb_do_small_get(c.type);
            break;
        case CMD_BM_SET:
        case CMD_SEEN_SET:
        case CMD_ATTACK:
            sdb_do_small_set(&c);
            break;
        case CMD_GAME:
            sdb_flush_game();
            break;
        case CMD_SOLVE:
            sdb_do_solve(&c);
            free(c.blob);
            break;
        case CMD_QUERY:
            sdb_do_query(c.level);
            break;
        case CMD_PLAYERS:
            sdb_do_list();
            break;
        case CMD_ADD_PLAYER:
            sdb_do_add(c.text);
            break;
        case CMD_RENAME_PLAYER:
            sdb_do_rename(c.ival, c.text);
            break;
        case CMD_SWITCH_PLAYER:
            if (c.ival != s_player) {
                sdb_do_switch(c.ival);
            }
            break;
        case CMD_DELETE_PLAYER:
            sdb_do_delete(c.ival);
            break;
        case CMD_MONTH:
            sdb_do_month(c.year, c.month);
            break;
        case CMD_RESET:
            do_reset(c.seed);
            break;
        case CMD_DAY:
            sdb_do_day(c.year, c.month, c.day);
            break;
        case CMD_REPLAY:
            sdb_do_replay(c.solve_id);
            break;
        case CMD_PLAYS:
            sdb_do_plays(c.ival);
            break;
        case CMD_GEN_ADD:
            sdb_do_gen_add();
            break;
        case CMD_GEN_GET:
            sdb_do_gen_get();
            break;
        case CMD_DAILY:
            sdb_do_daily();
            break;
        case CMD_RANK:
            sdb_do_rank();
            break;
        case CMD_BOARD:
            sdb_do_board(c.text);
            break;
        case CMD_ACH:
            sdb_do_achievements();
            break;
        case CMD_GAME_SPILL:
            if (s_db && c.spill) {
                sdb_save_game(&c.spill->g, c.spill->moves, c.spill->n);
            }
            free(c.spill);
            break;
        case CMD_CLEAR_GAME:
            sdb_clear_game(c.puzzle);
            break;
        case CMD_LIST_GAMES:
            sdb_flush_game();
            sdb_do_list_games();
            break;
        case CMD_LOAD_GAME:
            sdb_flush_game();
            sdb_load_game_of(&s_gen.game, s_gen.puzzle);
            s_gen.ok = s_db != NULL && s_gen.game.id != STORE_NO_GAME;
            xSemaphoreGive(s_gen_done);
            break;
        case CMD_RECORDS:
            sdb_do_records();
            break;
        case CMD_PRANK:
            sdb_do_puzzle_rank();
            break;
        case CMD_TWIN:
            sdb_do_twin();
            break;
        case CMD_GEN_ROWS:
            sdb_do_gen_rows();
            break;
        case CMD_EXPORT:
            sdb_do_export();
            break;
        case CMD_BACKUP:
            sdb_do_backup();
            break;
        case CMD_SQL:
            sdb_do_sql();
            break;
        case CMD_WEEK:
            sdb_do_week(c.text);
            break;
        case CMD_IMPORT_FILE:
            sdb_do_import_file((const char *)c.blob, (uint32_t)c.ival);
            free(c.blob);
            break;
        case CMD_IMPORT:
            sdb_do_import();
            break;
        case CMD_LIST_IMPORTS:
            sdb_do_list_imports();
            break;
        case CMD_SOLVED_IN:
            sdb_do_solved_in();
            break;
        case CMD_SAVED_LIST:
            sdb_do_saved_list();
            break;
        case CMD_SAVED_ADD:
            sdb_do_saved_add();
            break;
        case CMD_SAVED_REMOVE:
            sdb_do_saved_remove();
            break;
        case CMD_SOLVED_LIST:
            sdb_do_solved_list();
            break;
        case CMD_SAVED_EXPORT:
            sdb_do_saved_export();
            break;
        case CMD_COLL_DELETE:
            sdb_do_coll_delete();
            break;
        case CMD_GEN_SOURCE:
            s_gen.ok = s_db != NULL && sdb_gen_source_of(s_gen.id, s_gen.source, &s_gen.number);
            xSemaphoreGive(s_gen_done);
            break;
        case CMD_PLAYER_COLOR:
            if (s_db) {
                sqlite3_stmt *st;
                if (sqlite3_prepare_v2(s_db, "UPDATE players SET color = ?1 WHERE id = ?2", -1, &st, NULL) == SQLITE_OK) {
                    sqlite3_bind_int(st, 1, c.level);
                    sqlite3_bind_int(st, 2, c.ival);
                    sdb_step_done(st, "player colour");
                    sqlite3_finalize(st);
                }
                sdb_do_list();
            }
            break;
        case CMD_PLAYER_AVATAR:
            if (s_db) {
                sqlite3_stmt *st;
                if (sqlite3_prepare_v2(s_db, "UPDATE players SET avatar = ?1 WHERE id = ?2", -1, &st, NULL) == SQLITE_OK) {
                    if (c.level) {
                        sqlite3_bind_int(st, 1, c.level);
                    } else {
                        sqlite3_bind_null(st, 1);
                    }
                    sqlite3_bind_int(st, 2, c.ival);
                    sdb_step_done(st, "player avatar");
                    sqlite3_finalize(st);
                }
                sdb_do_list();
            }
            break;
        case CMD_SUSPEND:
            sdb_flush_game();
            db_close();
            bsp_sdcard_unmount();
            s_state = ST_OFF;
            xSemaphoreGive(s_suspended);
            break;
        }
    }
}

/* ------------------------------------------------------------- API */

/* (documented in stats_db_internal.h) */
bool sdb_send(const cmd_t *c)
{
    return s_q && xQueueSend(s_q, c, pdMS_TO_TICKS(100)) == pdTRUE;
}

static void start(bool fresh);

/* (documented in stats_db.h) */
void stats_db_start(void)
{
    start(false);
}

/* (documented in stats_db.h) */
void stats_db_start_fresh(void)
{
    start(true);
}

/**
 * @brief Starts the worker (once) and queues an open.
 * @param[in] fresh  a new, empty database (after formatting)
 */
static void start(bool fresh)
{
    if (!s_q) {
        s_q = xQueueCreate(16, sizeof(cmd_t));
        s_suspended = xSemaphoreCreateBinary();
        s_genp = heap_caps_calloc(1, sizeof(gen_call_t), MALLOC_CAP_SPIRAM);
        if (s_genp) {
            s_gen_done = xSemaphoreCreateBinary();
        }
        s_opened = xSemaphoreCreateBinary();
        s_lock = xSemaphoreCreateMutex();
        /* SQLite wants a deep stack (parser, B-tree balancing). Core 0: at priority 3
         * it would hold up LVGL (priority 1) on core 1 during every query. Unpinned
         * would not stay so: on the P4 a task is pinned to the core it is on when it
         * first uses the FPU, and created from setup() that is core 1. */
        if (!s_q || !s_suspended || !s_opened || !s_lock ||
                xTaskCreatePinnedToCore(worker, "stats_db", 16384, NULL, 3, NULL, 0) != pdPASS) {
            s_state = ST_ERROR;
            return;
        }
    }
    if (s_state == ST_READY || s_state == ST_OPENING) {
        return;
    }
    /* what the board knows so far (old NVS values + this session) */
    open_seed_t *seed = malloc(sizeof(*seed));
    if (seed) {
        memset(seed, 0, sizeof(*seed));
        seed->fresh = fresh;
        if (!fresh) {
            store_copy_stats(seed->stats.best, seed->stats.fewest, seed->stats.solves);
        }
        store_get_prefs(&seed->prefs);
    }
    cmd_t c = {.type = CMD_OPEN, .seed = seed};
    xSemaphoreTake(s_opened, 0);                          /* clear a stale give */
    s_state = ST_OPENING;
    if (!sdb_send(&c)) {
        free(seed);
        s_state = ST_ERROR;
        xSemaphoreGive(s_opened);
    }
}

/* (documented in stats_db.h) */
bool stats_db_wait_open(uint32_t timeout_ms)
{
    if (!s_opened) {
        return false;
    }
    if (xSemaphoreTake(s_opened, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }
    return s_state == ST_READY;
}

/* (documented in stats_db.h) */
bool is_stats_db_opening(void)
{
    return s_state == ST_OPENING;
}

/* (documented in stats_db.h) */
bool is_stats_db_ready(void)
{
    return s_state == ST_READY;
}

/* (documented in stats_db.h) */
bool is_stats_db_busy(void)
{
    return s_working || (s_q && uxQueueMessagesWaiting(s_q) > 0);
}

/* (documented in stats_db.h) */
void stats_db_record_solve(uint16_t puzzle, uint32_t seconds, uint32_t mistakes, uint32_t hints,
                           int64_t started_at, const game_move_t *moves, int nmoves, uint32_t flags)
{
    uint8_t *blob = NULL;
    int blob_bytes = 0;
    if (moves && nmoves > 0) {
        blob = heap_caps_malloc((size_t)nmoves * MOVE_BLOB_BYTES, MALLOC_CAP_SPIRAM);
        if (blob) {
            blob_bytes = (int)moves_pack(moves, nmoves, blob);
        }
    }
    time_t now = time(NULL);
    cmd_t c = {
        .type = CMD_SOLVE,
        .puzzle = puzzle,
        .seconds = seconds,
        .mistakes = mistakes,
        .hints = hints,
        .flags = flags,
        .blob = blob,
        .blob_bytes = blob_bytes,
        .when = now > 1700000000 ? (int64_t)now : 0,      /* only once NTP has set the clock */
        .started = started_at,
    };
    if (!sdb_send(&c)) {
        free(blob);
    }
}

/* (documented in stats_db_internal.h) */
bool sdb_gen_call(cmd_type_t type)
{
    if (!is_stats_db_ready() || !s_gen_done) {
        return false;
    }
    xSemaphoreTake(s_gen_done, 0);                         /* clear a stale give */
    cmd_t c = {.type = type};
    if (!sdb_send(&c)) {
        return false;
    }
    return xSemaphoreTake(s_gen_done, pdMS_TO_TICKS(3000)) == pdTRUE && s_gen.ok;
}

/* (documented in stats_db.h) */
bool stats_db_take_message(char *buf, int len)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool any = s_message[0] != 0;
    if (any) {
        snprintf(buf, len, "%s", s_message);
    }
    s_message[0] = 0;
    xSemaphoreGive(s_lock);
    return any;
}

/* (documented in stats_db.h) */
void stats_db_factory_reset(void)
{
    if (!s_q) {
        return;
    }
    open_seed_t *seed = malloc(sizeof(*seed));
    if (seed) {
        memset(seed, 0, sizeof(*seed));
        seed->fresh = true;
        store_get_prefs(&seed->prefs);        /* the defaults, after store_factory_reset() */
    }
    cmd_t c = {.type = CMD_RESET, .seed = seed};
    xSemaphoreTake(s_opened, 0);
    s_state = ST_OPENING;
    if (!sdb_send(&c)) {
        free(seed);
        s_state = ST_ERROR;
    }
}

/* (documented in stats_db.h) */
void stats_db_suspend(void)
{
    if (!s_q) {
        return;
    }
    xSemaphoreTake(s_suspended, 0);                       /* clear a stale give */
    cmd_t c = {.type = CMD_SUSPEND};
    if (sdb_send(&c)) {
        xSemaphoreTake(s_suspended, pdMS_TO_TICKS(5000));
    }
}

/* (documented in stats_db.h) */
void stats_db_status(char *buf, int len)
{
    switch (s_state) {
    case ST_READY:
        snprintf(buf, len, "Saving to sudoku.db: %d solve%s logged", s_solves, s_solves == 1 ? "" : "s");
        break;
    case ST_OPENING:
        snprintf(buf, len, "Opening sudoku.db...");
        break;
    case ST_NOT_FAT:
        snprintf(buf, len, "No FAT32 volume: format it to keep game data");
        break;
    case ST_ERROR:
        snprintf(buf, len, "Can't use the card for game data");
        break;
    case ST_OFF:
    default:
        snprintf(buf, len, "No card: game data is lost at power-off");
        break;
    }
}

/* (in stats_db_internal.h) */
sqlite3 *sdb_db(void)
{
    return s_db;
}

/* (documented in stats_db_internal.h) */
SemaphoreHandle_t sdb_lock(void)
{
    return s_lock;
}

/* (documented in stats_db_internal.h) */
int32_t sdb_player(void)
{
    return s_player;
}

/* (documented in stats_db_internal.h) */
void sdb_set_player(int32_t id)
{
    s_player = id;
}

/* (documented in stats_db_internal.h) */
void sdb_set_solves(int n)
{
    s_solves = n;
}

/* (documented in stats_db_internal.h) */
void sdb_solve_counted(void)
{
    if (s_solves >= 0) {
        s_solves++;
    }
}

/* (documented in stats_db_internal.h) */
bool is_sdb_started(void)
{
    return s_q != NULL;
}

/* (documented in stats_db_internal.h) */
gen_call_t *sdb_gen(void)
{
    return s_genp;
}

/* (documented in stats_db_internal.h) */
void sdb_gen_reply(void)
{
    xSemaphoreGive(s_gen_done);
}

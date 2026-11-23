/**
 * @file stats_db_games.c
 * @brief Unfinished games and their moves: saving, loading, listing, clearing (a solve's replay: stats_db_replay.c).
 */
#include "stats_db_internal.h"

static bool moves_bufs(void);

static saved_game_t s_game_next;        /**< the newest game to save (CMD_GAME just says "there is one") */
static bool s_game_next_has;            /**< s_game_next holds a game */
static bool s_game_dirty;               /**< it's queued and not written yet */
static game_move_t *s_moves_next;       /**< the move log to save with s_game_next (PSRAM) */
static game_move_t *s_moves_loaded;     /**< the one sdb_load_game_of() found (PSRAM) */
static int s_moves_next_n;              /**< s_moves_next's length */
static int s_moves_loaded_n;            /**< s_moves_loaded's length */
static uint16_t s_moves_next_puzzle = STORE_NO_GAME;     /**< the game s_moves_next belongs to */
static uint16_t s_moves_loaded_puzzle = STORE_NO_GAME;   /**< the game s_moves_loaded belongs to */

/* (documented in stats_db_internal.h) */
void sdb_clear_game(uint16_t puzzle)
{
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "DELETE FROM saved_game WHERE player_id = ?1 AND puzzle = ?2", -1, &st,
                                       NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int(st, 2, puzzle);
        sdb_step_done(st, "clear game");
        sqlite3_finalize(st);
    }
}

/* (documented in stats_db_internal.h) */
void sdb_save_game(const saved_game_t *g, const game_move_t *moves, int nmoves)
{
    if (!g) {
        return;    /* no game: nothing to save (sdb_clear_game removes one) */
    }
    sqlite3_stmt *st;
    char cells[82];
    uint8_t notes[162];
    for (int i = 0; i < 81; i++) {
        cells[i] = (char)('0' + (g->val[i] <= 9 ? g->val[i] : 0));
        notes[2 * i] = g->notes[i] & 0xFF;
        notes[2 * i + 1] = g->notes[i] >> 8;
    }
    cells[81] = 0;
    time_t now = time(NULL);
    if (sqlite3_prepare_v2(sdb_db(),
                           "INSERT OR REPLACE INTO saved_game(player_id, puzzle, level, number, cells, notes, elapsed_ms,"
                           " mistakes, saved_at, started_at, hints, moves, seq, colors, flags)"
                           " VALUES(?10, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?11, ?12,"
                           "  (SELECT COALESCE(MAX(seq), 0) + 1 FROM saved_game WHERE player_id = ?10), ?13, ?14)",
                           -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sdb_bind_puzzle(st, g->id);
    sqlite3_bind_text(st, 4, cells, 81, SQLITE_STATIC);
    sqlite3_bind_blob(st, 5, notes, sizeof(notes), SQLITE_STATIC);
    sqlite3_bind_int64(st, 6, g->elapsed_ms);
    sqlite3_bind_int(st, 7, g->mistakes);
    if (now > 1700000000) {
        sqlite3_bind_int64(st, 8, now);
    } else {
        sqlite3_bind_null(st, 8);
    }
    if (g->started_at) {
        sqlite3_bind_int64(st, 9, g->started_at);
    } else {
        sqlite3_bind_null(st, 9);
    }
    sqlite3_bind_int(st, 10, sdb_player());
    sqlite3_bind_int(st, 11, g->hints);
    sqlite3_bind_int64(st, 14, g->flags);
    bool colored = false;
    for (int i = 0; i < 81; i++) {
        colored |= g->colors[i] != 0;
    }
    if (colored) {
        sqlite3_bind_blob(st, 13, g->colors, 81, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(st, 13);
    }
    uint8_t *blob = NULL;
    if (moves && nmoves > 0) {
        blob = heap_caps_malloc((size_t)nmoves * MOVE_BLOB_BYTES, MALLOC_CAP_SPIRAM);
    }
    if (blob) {
        sqlite3_bind_blob(st, 12, blob, (int)moves_pack(moves, nmoves, blob), SQLITE_STATIC);
    } else {
        sqlite3_bind_null(st, 12);
    }
    bool ok = sdb_step_done(st, "save game");
    sqlite3_finalize(st);
    free(blob);
    if (ok && sqlite3_prepare_v2(sdb_db(), "DELETE FROM saved_game WHERE player_id = ?1 AND puzzle NOT IN"
                                 " (SELECT puzzle FROM saved_game WHERE player_id = ?1 ORDER BY seq DESC LIMIT ?2)",
                                 -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int(st, 2, STATS_MAX_GAMES);
        if (sdb_step_done(st, "trim games") && sqlite3_changes(sdb_db()) > 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Up to %d unfinished games are kept: the one played longest ago was "
                     "dropped.", STATS_MAX_GAMES);
            sdb_post_message(msg);
        }
        sqlite3_finalize(st);
    }
}

/* (documented in stats_db_internal.h) */
void sdb_load_game_of(saved_game_t *g, uint16_t puzzle)
{
    memset(g, 0, sizeof(*g));
    g->id = STORE_NO_GAME;
    sqlite3_stmt *st;
    int nmoves = 0;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT puzzle, cells, notes, elapsed_ms, mistakes, started_at, hints, moves, colors,"
                           " flags FROM saved_game"
                           " WHERE player_id = ?1 AND (?2 < 0 OR puzzle = ?2) ORDER BY seq DESC LIMIT 1",
                           -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int(st, 1, sdb_player());
    sqlite3_bind_int(st, 2, puzzle == STORE_NO_GAME ? -1 : puzzle);
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
            g->flags = (uint32_t)sqlite3_column_int64(st, 9);
            const uint8_t *col = (const uint8_t *)sqlite3_column_blob(st, 8);
            if (col && sqlite3_column_bytes(st, 8) == 81) {
                for (int i = 0; i < 81; i++) {
                    g->colors[i] = col[i] <= CELL_COLORS ? col[i] : 0;
                }
            }
            const uint8_t *mb = (const uint8_t *)sqlite3_column_blob(st, 7);
            if (mb && moves_bufs()) {
                xSemaphoreTake(sdb_lock(), portMAX_DELAY);
                nmoves = moves_unpack(mb, sqlite3_column_bytes(st, 7), s_moves_loaded, GAME_MOVES_MAX);
                xSemaphoreGive(sdb_lock());
            }
        }
    }
    sqlite3_finalize(st);
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    s_moves_loaded_n = nmoves;
    s_moves_loaded_puzzle = g->id;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db_internal.h) */
void sdb_load_game(saved_game_t *g)
{
    sdb_load_game_of(g, STORE_NO_GAME);
}

/* (documented in stats_db_internal.h) */
void sdb_flush_game(void)
{
    static saved_game_t g;                    /* worker-side copies */
    static game_move_t *m;
    if (!m) {
        m = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    }
    int n = 0;
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool dirty = s_game_dirty, has = s_game_next_has;
    g = s_game_next;
    if (has && m && s_moves_next && s_moves_next_puzzle == g.id) {
        n = s_moves_next_n;
        memcpy(m, s_moves_next, sizeof(game_move_t) * n);
    }
    s_game_dirty = false;
    xSemaphoreGive(sdb_lock());
    if (dirty && has && sdb_db()) {
        sdb_save_game(&g, m, n);
    }
}

/* ------------------------------------------------------------- unfinished games */

/* (documented in stats_db_internal.h) */
void sdb_do_list_games(void)
{
    s_gen.ngames = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT puzzle, elapsed_ms, mistakes, hints, cells, started_at, saved_at"
                                       " FROM saved_game WHERE player_id = ?1 ORDER BY seq DESC LIMIT ?2",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int(st, 2, STATS_MAX_GAMES);
        while (s_gen.ngames < STATS_MAX_GAMES && sqlite3_step(st) == SQLITE_ROW) {
            int id = sqlite3_column_int(st, 0);
            if (!puzzle_id_valid(id)) {
                continue;
            }
            stats_game_row_t *r = &s_gen.games[s_gen.ngames++];
            r->puzzle = (uint16_t)id;
            r->elapsed_ms = (uint32_t)sqlite3_column_int64(st, 1);
            r->mistakes = (uint16_t)sqlite3_column_int(st, 2);
            r->hints = (uint16_t)sqlite3_column_int(st, 3);
            const char *cells = (const char *)sqlite3_column_text(st, 4);
            r->filled = 0;
            for (int i = 0; cells && cells[i] && i < 81; i++) {
                r->filled += cells[i] > '0' && cells[i] <= '9';
            }
            r->started_at = sqlite3_column_int64(st, 5);
            r->saved_at = sqlite3_column_int64(st, 6);
        }
        sqlite3_finalize(st);
    }
    s_gen.ok = sdb_db() != NULL;
    sdb_gen_reply();
}

/* (documented in stats_db.h) */
void stats_db_save_game(const saved_game_t *g, const game_move_t *moves, int nmoves)
{
    if (!sdb_lock() || !is_sdb_started() || !g || !moves_bufs()) {
        return;    /* no database this session */
    }
    if (nmoves > GAME_MOVES_MAX) {
        nmoves = GAME_MOVES_MAX;
    }
    if (nmoves < 0 || !moves) {
        nmoves = 0;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    if (s_game_dirty && s_game_next_has && s_game_next.id != g->id) {
        /* another game's last save hasn't been written yet: it goes on its own */
        int n = s_moves_next_puzzle == s_game_next.id ? s_moves_next_n : 0;
        game_spill_t *sp = heap_caps_malloc(sizeof(*sp) + sizeof(game_move_t) * (size_t)n, MALLOC_CAP_SPIRAM);
        if (sp) {
            sp->g = s_game_next;
            sp->n = n;
            if (n) {
                memcpy(sp->moves, s_moves_next, sizeof(game_move_t) * (size_t)n);
            }
            cmd_t c = {.type = CMD_GAME_SPILL, .spill = sp};
            if (!sdb_send(&c)) {
                free(sp);
            }
        }
    }
    s_game_next_has = true;
    s_game_next = *g;
    if (nmoves) {
        memcpy(s_moves_next, moves, sizeof(game_move_t) * (size_t)nmoves);
    }
    s_moves_next_n = nmoves;
    s_moves_next_puzzle = g->id;
    bool queued = s_game_dirty;
    s_game_dirty = true;
    xSemaphoreGive(sdb_lock());
    if (!queued) {
        cmd_t c = {.type = CMD_GAME};
        if (!sdb_send(&c)) {
            xSemaphoreTake(sdb_lock(), portMAX_DELAY);
            s_game_dirty = false;
            xSemaphoreGive(sdb_lock());
        }
    }
}

/* (documented in stats_db.h) */
void stats_db_clear_game(uint16_t puzzle)
{
    if (!sdb_lock() || !is_sdb_started() || puzzle == STORE_NO_GAME) {
        return;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    if (s_game_dirty && s_game_next.id == puzzle) {
        s_game_dirty = false;    /* its pending save is moot */
    }
    xSemaphoreGive(sdb_lock());
    cmd_t c = {.type = CMD_CLEAR_GAME, .puzzle = puzzle};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
int stats_db_list_games(stats_game_row_t *out)
{
    if (!sdb_gen()) {
        return -1;    /* no database this session */
    }
    if (!sdb_gen_call(CMD_LIST_GAMES)) {
        return -1;
    }
    memcpy(out, s_gen.games, sizeof(stats_game_row_t) * (size_t)s_gen.ngames);
    return s_gen.ngames;
}

/* (documented in stats_db.h) */
bool stats_db_load_game(uint16_t puzzle, saved_game_t *out)
{
    if (!sdb_gen()) {
        return false;    /* no database this session */
    }
    s_gen.puzzle = puzzle;
    if (!sdb_gen_call(CMD_LOAD_GAME)) {
        return false;
    }
    *out = s_gen.game;
    return true;
}

/* (documented in stats_db.h) */
int stats_db_copy_saved_moves(uint16_t *puzzle, game_move_t *out, int max)
{
    *puzzle = STORE_NO_GAME;
    if (!sdb_lock() || !s_moves_loaded) {
        return 0;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    int n = s_moves_loaded_n < max ? s_moves_loaded_n : max;
    if (n > 0) {
        memcpy(out, s_moves_loaded, sizeof(game_move_t) * n);
    }
    *puzzle = s_moves_loaded_puzzle;
    xSemaphoreGive(sdb_lock());
    return n;
}

/* (documented in stats_db_internal.h) */
void sdb_game_forget(void)
{
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    s_game_dirty = false;
    xSemaphoreGive(sdb_lock());
}

/**
 * @brief The move log buffers (PSRAM), made on first use.
 * @return false: no memory
 */
static bool moves_bufs(void)
{
    size_t sz = sizeof(game_move_t) * GAME_MOVES_MAX;
    if (!s_moves_next) {
        s_moves_next = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    }
    if (!s_moves_loaded) {
        s_moves_loaded = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    }
    return s_moves_next && s_moves_loaded;
}

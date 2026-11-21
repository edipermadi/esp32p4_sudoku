/**
 * @file stats_db_solves.c
 * @brief Solves: logging one, the statistics per level, and the legacy statistics merged in.
 */
#include "stats_db_internal.h"

static sqlite3_stmt *s_merge;                 /**< SQL_MERGE, prepared */
static sqlite3_stmt *s_solve_upsert;          /**< SQL_SOLVE_UPSERT, prepared */
static sqlite3_stmt *s_solve_insert;          /**< SQL_SOLVE_INSERT, prepared */
static stats_view_t *s_view;                  /**< CMD_QUERY result, handed to the LVGL task (PSRAM) */
static bool s_view_new;                       /**< s_view is new */
static stats_arrays_t *s_cache;               /**< puzzle_stats for the RAM copy: set after open / switch, taken once */

/** SQL: the fewer mistakes (MIN() of SQL is NULL if either side is: keep the known value then) */
#define FEWEST_MERGED "COALESCE(MIN(fewest_mistakes, excluded.fewest_mistakes), fewest_mistakes, excluded.fewest_mistakes)"

/** The board's statistics into puzzle_stats (idempotent). */
static const char SQL_MERGE[] =
    "INSERT INTO puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)"
    " VALUES(?7, ?1, ?2, ?3, ?4, ?5, ?6)"
    " ON CONFLICT(player_id, puzzle) DO UPDATE SET best_seconds = MIN(best_seconds, excluded.best_seconds),"
    "   fewest_mistakes = " FEWEST_MERGED ", solves = MAX(solves, excluded.solves)";

/** A solve into its puzzle_stats row. */
static const char SQL_SOLVE_UPSERT[] =
    "INSERT INTO puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)"
    " VALUES(?6, ?1, ?2, ?3, ?4, ?5, 1)"
    " ON CONFLICT(player_id, puzzle) DO UPDATE SET best_seconds = MIN(best_seconds, excluded.best_seconds),"
    "   fewest_mistakes = " FEWEST_MERGED ", solves = solves + 1";

/** A solve's row in solves. */
static const char SQL_SOLVE_INSERT[] =
    "INSERT INTO solves(player_id, puzzle, level, number, seconds, mistakes, finished_at, started_at, hints, moves,"
    " flags) VALUES(?8, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?9, ?10, ?11)";

/* (documented in stats_db_internal.h) */
void sdb_merge_legacy(const stats_arrays_t *a)
{
    sdb_exec("BEGIN");
    for (uint16_t i = 0; i < PUZZLE_COUNT; i++) {
        if (!a->best[i]) {
            continue;
        }
        sdb_bind_puzzle(s_merge, i);
        sqlite3_bind_int(s_merge, 4, a->best[i]);
        if (a->fewest[i] == STORE_MISTAKES_UNKNOWN) {
            sqlite3_bind_null(s_merge, 5);
        } else {
            sqlite3_bind_int(s_merge, 5, a->fewest[i]);
        }
        sqlite3_bind_int(s_merge, 6, a->solves[i] ? a->solves[i] : 1);
        sqlite3_bind_int(s_merge, 7, sdb_player());
        sdb_step_done(s_merge, "merge");
    }
    sdb_exec("COMMIT");
}

/* (documented in stats_db_internal.h) */
void sdb_load_cache(void)
{
    stats_arrays_t *a = calloc(1, sizeof(*a));
    if (!a) {
        return;
    }
    memset(a->fewest, STORE_MISTAKES_UNKNOWN, sizeof(a->fewest));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT puzzle, best_seconds, fewest_mistakes, solves FROM puzzle_stats"
                           " WHERE player_id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        while (sqlite3_step(st) == SQLITE_ROW) {
            int id = sqlite3_column_int(st, 0);
            if (id < 0 || id >= PUZZLE_COUNT) {
                continue;
            }
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
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    free(s_cache);
    s_cache = a;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db_internal.h) */
void sdb_do_solve(const cmd_t *c)
{
    if (!sdb_db()) {
        return;
    }
    sdb_exec("BEGIN");
    sdb_bind_puzzle(s_solve_insert, c->puzzle);
    sqlite3_bind_int64(s_solve_insert, 4, c->seconds);
    sqlite3_bind_int64(s_solve_insert, 5, c->mistakes);
    if (c->when) {
        sqlite3_bind_int64(s_solve_insert, 6, c->when);
    } else {
        sqlite3_bind_null(s_solve_insert, 6);
    }
    if (c->started) {
        sqlite3_bind_int64(s_solve_insert, 7, c->started);
    } else {
        sqlite3_bind_null(s_solve_insert, 7);
    }
    sqlite3_bind_int(s_solve_insert, 8, sdb_player());
    sqlite3_bind_int64(s_solve_insert, 9, c->hints);
    if (c->blob) {
        sqlite3_bind_blob(s_solve_insert, 10, c->blob, c->blob_bytes, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(s_solve_insert, 10);
    }
    sqlite3_bind_int64(s_solve_insert, 11, c->flags);
    bool ok = sdb_step_done(s_solve_insert, "insert solve");
    sdb_bind_puzzle(s_solve_upsert, c->puzzle);
    sqlite3_bind_int64(s_solve_upsert, 4, c->seconds ? c->seconds : 1);
    sqlite3_bind_int64(s_solve_upsert, 5, c->mistakes);
    sqlite3_bind_int(s_solve_upsert, 6, sdb_player());
    ok = sdb_step_done(s_solve_upsert, "update puzzle_stats") && ok;
    if (ok && sdb_exec("COMMIT")) {
        sdb_solve_counted();
        sdb_ach_sync(sdb_player(), true);                  /* new achievements: stats_db_take_unlocked() */
    } else {
        sdb_exec("ROLLBACK");
    }
}

/* (documented in stats_db_internal.h) */
void sdb_do_query(int level)
{
    static stats_view_t *vp;                  /* worker-side scratch (PSRAM) */
    if (!sdb_psram_buf((void **)&vp, sizeof(*vp)) || !sdb_psram_buf((void **)&s_view, sizeof(*s_view))) {
        return;
    }
    stats_view_t *const v = vp;
    memset(v, 0, sizeof(*v));
    v->level = level;
    v->player_id = sdb_player();

    /* per level: same aggregates the screen shows (closed database: empty result) */
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT level, COUNT(*), TOTAL(fewest_mistakes = 0), COUNT(fewest_mistakes),"
                                       "       TOTAL(solves), TOTAL(best_seconds), MIN(best_seconds), TOTAL(fewest_mistakes)"
                                       " FROM puzzle_stats WHERE player_id = ?1 GROUP BY level", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *name = (const char *)sqlite3_column_text(st, 0);
            int c = 0, kind, klevel;
            while (c < PUZZLE_COLLECTION_COUNT && strcmp(puzzle_collection(c)->name, name ? name : "") != 0) {
                c++;
            }
            stats_level_t *l;
            if (c < PUZZLE_COLLECTION_COUNT) {
                l = &v->levels[c];
            } else if (sdb_kind_level_of(name, &kind, &klevel)) {
                l = &v->kinds[kind][klevel];    /* made on the board */
            } else {
                continue;    /* a level this firmware doesn't have */
            }
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
        if (l->fastest && (!t->fastest || l->fastest < t->fastest)) {
            t->fastest = l->fastest;
        }
    }

    /* one level: its solved puzzles */
    if (sdb_db() && level >= 0 && level < PUZZLE_COLLECTION_COUNT &&
            sqlite3_prepare_v2(sdb_db(),
                               "SELECT number, best_seconds, fewest_mistakes, solves FROM puzzle_stats"
                               " WHERE player_id = ?1 AND level = ?2 ORDER BY number", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_text(st, 2, puzzle_collection(level)->name, -1, SQLITE_STATIC);
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
    if (kind) {
        snprintf(like, sizeof(like), "%s %%", stats_kind_prefix(level - PUZZLE_COLLECTION_COUNT));
    }
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), coll
                                       ? "SELECT seconds FROM solves WHERE player_id = ?1 AND level = ?3 ORDER BY id DESC LIMIT ?2"
                                       : kind ? "SELECT seconds FROM solves WHERE player_id = ?1 AND level LIKE ?3 ORDER BY id DESC LIMIT ?2"
                                       : "SELECT seconds FROM solves WHERE player_id = ?1 ORDER BY id DESC LIMIT ?2",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int(st, 2, STATS_RECENT);
        if (coll) {
            sqlite3_bind_text(st, 3, puzzle_collection(level)->name, -1, SQLITE_STATIC);
        }
        if (kind) {
            sqlite3_bind_text(st, 3, like, -1, SQLITE_TRANSIENT);
        }
        uint16_t newest_first[STATS_RECENT];
        int n = 0;
        while (n < STATS_RECENT && sqlite3_step(st) == SQLITE_ROW) {
            int64_t sec = sqlite3_column_int64(st, 0);
            newest_first[n++] = (uint16_t)(sec > 0xFFFF ? 0xFFFF : (sec < 1 ? 1 : sec));
        }
        sqlite3_finalize(st);
        for (int i = 0; i < n; i++) {
            v->recent[i] = newest_first[n - 1 - i];
        }
        v->n_recent = n;
    }

    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *s_view = *v;
    s_view_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db.h) */
void stats_db_query(int level)
{
    cmd_t c = {.type = CMD_QUERY, .level = level};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_view(stats_view_t *out)
{
    if (!sdb_lock() || !s_view) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_view_new;
    if (fresh) {
        *out = *s_view;
    }
    s_view_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db.h) */
bool stats_db_take_cache(uint16_t *best, uint8_t *fewest, uint8_t *solves)
{
    if (!sdb_lock()) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    stats_arrays_t *a = s_cache;
    s_cache = NULL;
    xSemaphoreGive(sdb_lock());
    if (!a) {
        return false;
    }
    memcpy(best, a->best, sizeof(a->best));
    memcpy(fewest, a->fewest, sizeof(a->fewest));
    memcpy(solves, a->solves, sizeof(a->solves));
    free(a);
    return true;
}

/* (documented in stats_db_internal.h) */
bool sdb_solves_prepare(void)
{
    sqlite3 *db = sdb_db();
    return sqlite3_prepare_v2(db, SQL_MERGE, -1, &s_merge, NULL) == SQLITE_OK &&
           sqlite3_prepare_v2(db, SQL_SOLVE_UPSERT, -1, &s_solve_upsert, NULL) == SQLITE_OK &&
           sqlite3_prepare_v2(db, SQL_SOLVE_INSERT, -1, &s_solve_insert, NULL) == SQLITE_OK;
}

/* (documented in stats_db_internal.h) */
void sdb_solves_finalize(void)
{
    sqlite3_finalize(s_merge);
    sqlite3_finalize(s_solve_upsert);
    sqlite3_finalize(s_solve_insert);
    s_merge = s_solve_upsert = s_solve_insert = NULL;
}

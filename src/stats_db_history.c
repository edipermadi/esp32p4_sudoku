/**
 * @file stats_db_history.c
 * @brief History: a month, a day, the plays list (the weekly challenge: stats_db_weekly.c; insights:
 *        stats_db_insights.c).
 */
#include "stats_db_internal.h"

static stats_month_t s_month;        /**< CMD_MONTH result */
static bool s_month_new;             /**< s_month is new */
static stats_day_t s_day;            /**< CMD_DAY result */
static bool s_day_new;               /**< s_day is new */
static stats_plays_t *s_plays;       /**< CMD_PLAYS result (PSRAM) */
static bool s_plays_new;             /**< s_plays is new */

/* (documented in stats_db_internal.h) */
void sdb_do_month(int year, int month)
{
    static stats_month_t m;
    memset(&m, 0, sizeof(m));
    sqlite3_stmt *st;
    char ym[24] = "";                             /* "YYYY-MM" (room for any int, for the compiler) */
    if (sdb_db() && year == 0) {                  /* the latest month with anything in it */
        if (sqlite3_prepare_v2(sdb_db(), "SELECT strftime('%Y-%m', MAX(t), 'unixepoch', 'localtime') FROM ("
                               HISTORY_EVENTS ")", -1, &st, NULL) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
                sscanf((const char *)sqlite3_column_text(st, 0), "%d-%d", &year, &month);
            }
            sqlite3_finalize(st);
        }
    }
    if (sdb_db() && year > 0) {
        m.year = year;
        m.month = month;
        snprintf(ym, sizeof(ym), "%04d-%02d", year, month);
        if (sqlite3_prepare_v2(sdb_db(),
                               "SELECT CAST(strftime('%d', t, 'unixepoch', 'localtime') AS INTEGER), COUNT(*) FROM ("
                               HISTORY_EVENTS ") WHERE strftime('%Y-%m', t, 'unixepoch', 'localtime') = ?1 GROUP BY 1",
                               -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, ym, -1, SQLITE_STATIC);
            while (sqlite3_step(st) == SQLITE_ROW) {
                int d = sqlite3_column_int(st, 0), n = sqlite3_column_int(st, 1);
                if (d >= 1 && d <= 31) {
                    m.count[d - 1] = n > 255 ? 255 : n;
                }
            }
            sqlite3_finalize(st);
        }
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    s_month = m;
    s_month_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db_internal.h) */
void sdb_do_plays(int offset)
{
    static stats_plays_t *p;
    if (!p) {
        p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM);
    }
    if (!s_plays) {
        s_plays = heap_caps_malloc(sizeof(*s_plays), MALLOC_CAP_SPIRAM);
    }
    if (!p || !s_plays) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->offset = offset < 0 ? 0 : offset;
    sqlite3_stmt *st;
    if (sdb_db()) {
        p->total = sdb_query_int("SELECT COUNT(*) FROM solves WHERE moves IS NOT NULL");
        if (p->total < 0) {
            p->total = 0;
        }
        if (sqlite3_prepare_v2(sdb_db(),
                               "SELECT s.started_at, s.finished_at, p.name, s.puzzle, s.seconds, s.mistakes, s.hints, s.id, s.flags"
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
                r->flags = (uint32_t)sqlite3_column_int64(st, 8);
            }
            sqlite3_finalize(st);
        }
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *s_plays = *p;
    s_plays_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db_internal.h) */
void sdb_do_day(int year, int month, int day)
{
    static stats_day_t d;
    memset(&d, 0, sizeof(d));
    d.year = year;
    d.month = month;
    d.day = day;
    char ymd[12];
    snprintf(ymd, sizeof(ymd), "%04d-%02d-%02d", year, month, day);
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT started, finished, player, puzzle, seconds, mistakes, in_progress, hints, solve_id, has_moves, flags"
                                       " FROM ("
                                       HISTORY_EVENTS ") WHERE date(t, 'unixepoch', 'localtime') = ?1 ORDER BY t",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, ymd, -1, SQLITE_STATIC);
        while (sqlite3_step(st) == SQLITE_ROW) {
            d.total++;
            if (d.n >= STATS_MAX_DAY_ROWS) {
                continue;
            }
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
            r->flags = (uint32_t)sqlite3_column_int64(st, 10);
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    s_day = d;
    s_day_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db.h) */
void stats_db_query_plays(int offset)
{
    cmd_t c = {.type = CMD_PLAYS, .ival = offset};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_plays(stats_plays_t *out)
{
    if (!sdb_lock() || !s_plays) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_plays_new;
    if (fresh) {
        *out = *s_plays;
        s_plays_new = false;
    }
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db.h) */
void stats_db_query_month(int year, int month)
{
    cmd_t c = {.type = CMD_MONTH, .year = year, .month = month};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_month(stats_month_t *out)
{
    if (!sdb_lock()) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_month_new;
    if (fresh) {
        *out = s_month;
    }
    s_month_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db.h) */
void stats_db_query_day(int year, int month, int day)
{
    cmd_t c = {.type = CMD_DAY, .year = year, .month = month, .day = day};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_day(stats_day_t *out)
{
    if (!sdb_lock()) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_day_new;
    if (fresh) {
        *out = s_day;
    }
    s_day_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

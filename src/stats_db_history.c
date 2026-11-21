/**
 * @file stats_db_history.c
 * @brief History: a month, a day, the plays list, a week, and insights.
 */
#include "stats_db_internal.h"

static stats_month_t s_month;        /**< CMD_MONTH result */
static bool s_month_new;             /**< s_month is new */
static stats_day_t s_day;            /**< CMD_DAY result */
static bool s_day_new;               /**< s_day is new */
static stats_plays_t *s_plays;       /**< CMD_PLAYS result (PSRAM) */
static bool s_plays_new;             /**< s_plays is new */
static stats_insights_t *s_ins;      /**< CMD_INSIGHTS result (PSRAM) */
static stats_weeksum_t s_wks;        /**< CMD_WEEKSUM result */
static bool s_wks_new;               /**< s_wks is new */
static bool s_ins_new;               /**< s_ins is new */

/* (documented in stats_db_internal.h) */
void sdb_do_weeksum(void)
{
    stats_weeksum_t w;
    memset(&w, 0, sizeof(w));
    sqlite3_stmt *st;
    /* a week = its Monday: 'weekday 0' moves to the Sunday that ends it, '-6 days' back to Monday */
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT wk != date('now', 'localtime', 'weekday 0', '-6 days'), COUNT(*), MIN(seconds), AVG(seconds),"
                                       "       AVG(mistakes) * 10, TOTAL(hints) FROM ("
                                       "  SELECT date(finished_at, 'unixepoch', 'localtime', 'weekday 0', '-6 days') AS wk, seconds, mistakes,"
                                       "         hints FROM solves WHERE player_id = ?1 AND finished_at IS NOT NULL)"
                                       " WHERE wk >= date('now', 'localtime', 'weekday 0', '-13 days') GROUP BY 1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        while (sqlite3_step(st) == SQLITE_ROW) {
            int k = sqlite3_column_int(st, 0) ? 1 : 0;
            w.n[k] = sqlite3_column_int(st, 1);
            w.best[k] = (uint32_t)sqlite3_column_int64(st, 2);
            w.avg[k] = (uint32_t)(sqlite3_column_double(st, 3) + 0.5);
            w.mist10[k] = (uint16_t)(sqlite3_column_double(st, 4) + 0.5);
            w.hints[k] = (uint32_t)sqlite3_column_int64(st, 5);
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    s_wks = w;
    s_wks_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db_internal.h) */
void sdb_do_insights(void)
{
    static stats_insights_t *ip;
    static game_move_t *mv;
    if (!sdb_psram_buf((void **)&ip, sizeof(*ip)) || !sdb_psram_buf((void **)&s_ins, sizeof(*s_ins)) ||
            !sdb_psram_buf((void **)&mv, sizeof(game_move_t) * GAME_MOVES_MAX)) {
        return;
    }
    stats_insights_t *const in = ip;
    memset(in, 0, sizeof(*in));
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT s.moves FROM solves s LEFT JOIN generated g ON s.puzzle = 2000 + g.id"
                                       " WHERE s.player_id = ?1 AND s.moves IS NOT NULL AND COALESCE(g.variant, 0) != 2"   /* (not 6x6) */
                                       " ORDER BY s.id DESC LIMIT ?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int(st, 2, STATS_INSIGHT_SOLVES);
        while (sqlite3_step(st) == SQLITE_ROW) {
            int n = moves_unpack((const uint8_t *)sqlite3_column_blob(st, 0), sqlite3_column_bytes(st, 0), mv,
                                 GAME_MOVES_MAX);
            if (n <= 0) {
                continue;
            }
            uint8_t last[81] = {0};                 /* the solved board (the cells the player filled) */
            for (int i = 0; i < n; i++) {
                last[mv[i].cell] = mv[i].digit;
            }
            in->solves++;
            for (int i = 0; i < n; i++) {
                const game_move_t *m = &mv[i];
                if (m->kind != MOVE_HAND || !m->digit || !last[m->cell]) {
                    continue;    /* by hand, a digit */
                }
                in->placed++;
                if (m->digit == last[m->cell]) {
                    continue;
                }
                in->wrong++;
                in->by_digit[m->digit]++;
                in->by_box[(m->cell / 27) * 3 + (m->cell % 9) / 3]++;
                in->pair[m->digit][last[m->cell]]++;
            }
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *s_ins = *in;
    s_ins_new = true;
    xSemaphoreGive(sdb_lock());
}

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

/* ------------------------------------------------------------- weekly challenge */

static stats_week_t *s_week;         /**< CMD_WEEK result (PSRAM) */
static bool s_week_new;              /**< s_week is new */

/* (documented in stats_db_internal.h) */
void sdb_do_week(const char *monday)
{
    static stats_week_t *w;
    if (!sdb_psram_buf((void **)&w, sizeof(*w)) || !sdb_psram_buf((void **)&s_week, sizeof(*s_week))) {
        return;
    }
    memset(w, 0, sizeof(*w));
    snprintf(w->monday, sizeof(w->monday), "%s", monday);
    char days[7][12];
    for (int i = 0; i < 7; i++) {                   /* Monday .. Sunday */
        snprintf(days[i], sizeof(days[i]), "%s", monday);
        for (int k = 0; k < i; k++) {               /* (prev_day backwards; forwards by hand) */
            struct tm tm = {0};
            sscanf(days[i], "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            tm.tm_mday += 1;
            tm.tm_hour = 12;
            mktime(&tm);
            strftime(days[i], sizeof(days[i]), "%Y-%m-%d", &tm);
        }
    }
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT id, name, " PLAYER_COLOR("players") " FROM players ORDER BY id", -1,
                                       &st, NULL) == SQLITE_OK) {
        while (w->n < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) {
            stats_week_row_t *r = &w->rows[w->n++];
            r->player_id = sqlite3_column_int(st, 0);
            const char *n = (const char *)sqlite3_column_text(st, 1);
            snprintf(r->player, sizeof(r->player), "%s", n ? n : "");
            r->color = (uint8_t)sqlite3_column_int(st, 2);
        }
        sqlite3_finalize(st);
    }
    /* each player's best on each day's daily puzzle */
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT s.player_id, g.day, MIN(s.seconds) FROM solves s JOIN generated g"
                                       " ON s.puzzle = 2000 + g.id WHERE g.day >= ?1 AND g.day <= ?2"
                                       " GROUP BY s.player_id, g.day", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, days[0], -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, days[6], -1, SQLITE_STATIC);
        while (sqlite3_step(st) == SQLITE_ROW) {
            int32_t pid = sqlite3_column_int(st, 0);
            const char *d = (const char *)sqlite3_column_text(st, 1);
            int secs = sqlite3_column_int(st, 2);
            for (int i = 0; i < w->n; i++) {
                if (w->rows[i].player_id != pid) {
                    continue;
                }
                for (int k = 0; d && k < 7; k++) {
                    if (strcmp(d, days[k]) != 0) {
                        continue;
                    }
                    w->rows[i].best[k] = (uint16_t)(secs > 0xFFFF ? 0xFFFF : (secs < 1 ? 1 : secs));
                    w->rows[i].days++;
                    w->rows[i].total += (uint32_t)secs;
                }
            }
        }
        sqlite3_finalize(st);
    }
    for (int i = 1; i < w->n; i++) {                /* more days first, then less time */
        stats_week_row_t x = w->rows[i];
        int j = i - 1;
        while (j >= 0 && (w->rows[j].days < x.days || (w->rows[j].days == x.days && w->rows[j].total > x.total))) {
            w->rows[j + 1] = w->rows[j];
            j--;
        }
        w->rows[j + 1] = x;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *s_week = *w;
    s_week_new = true;
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
void stats_db_query_weeksum(void)
{
    cmd_t c = {.type = CMD_WEEKSUM};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_weeksum(stats_weeksum_t *out)
{
    if (!sdb_lock()) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_wks_new;
    if (fresh) {
        *out = s_wks;
    }
    s_wks_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db.h) */
void stats_db_query_insights(void)
{
    cmd_t c = {.type = CMD_INSIGHTS};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_insights(stats_insights_t *out)
{
    if (!sdb_lock() || !s_ins) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_ins_new;
    if (fresh) {
        *out = *s_ins;
    }
    s_ins_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db.h) */
void stats_db_query_week(const char *monday)
{
    cmd_t c = {.type = CMD_WEEK};
    snprintf(c.text, sizeof(c.text), "%s", monday ? monday : "");
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_week(stats_week_t *out)
{
    if (!sdb_lock() || !s_week) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_week_new;
    if (fresh) {
        *out = *s_week;
    }
    s_week_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
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

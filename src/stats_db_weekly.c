/**
 * @file stats_db_weekly.c
 * @brief The weekly challenge (Trophies > Weekly): a week's daily puzzles added up per player, and Home's this week / last week.
 */
#include "stats_db_internal.h"

static stats_weeksum_t s_wks;        /**< CMD_WEEKSUM result */
static bool s_wks_new;               /**< s_wks is new */
static stats_week_t *s_week;         /**< CMD_WEEK result (PSRAM) */
static bool s_week_new;              /**< s_week is new */

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

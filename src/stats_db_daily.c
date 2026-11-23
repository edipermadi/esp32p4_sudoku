/**
 * @file stats_db_daily.c
 * @brief The daily puzzle's status: today's puzzle, whether it is solved, the streak.
 */
#include "stats_db_internal.h"

/* this file's own functions, in any order */
static void prev_day(char *d);

/**
 * @brief The day before a date, in place.
 * @param[in,out] d  'YYYY-MM-DD'
 */
static void prev_day(char *d)
{
    struct tm tm = {0};
    if (sscanf(d, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
        return;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_mday -= 1;
    tm.tm_hour = 12;                              /* clear of DST changes */
    mktime(&tm);                                  /* normalises day 0 to the month before */
    strftime(d, 11, "%Y-%m-%d", &tm);
}

/* (documented in stats_db_internal.h) */
void sdb_do_daily(void)
{
    s_gen.id = 0;
    s_gen.solved_today = false;
    s_gen.streak = 0;
    s_gen.ok = sdb_db() != NULL;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT id FROM generated WHERE day = ?1 ORDER BY id LIMIT 1",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, s_gen.day, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            s_gen.id = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT DISTINCT g.day FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id"
                                       " WHERE s.player_id = ?1 AND g.day IS NOT NULL AND g.day <= ?2 ORDER BY g.day DESC",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_text(st, 2, s_gen.day, -1, SQLITE_STATIC);
        char want[12];
        snprintf(want, sizeof(want), "%s", s_gen.day);
        bool first = true;
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *d = (const char *)sqlite3_column_text(st, 0);
            if (!d) {
                break;
            }
            if (first) {
                first = false;
                s_gen.solved_today = strcmp(d, want) == 0;
                if (!s_gen.solved_today) {
                    prev_day(want);    /* not yet today: a streak can end yesterday */
                }
            }
            if (strcmp(d, want) != 0) {
                break;
            }
            s_gen.streak++;
            prev_day(want);
        }
        sqlite3_finalize(st);
    }
    sdb_gen_reply();
}

/* (documented in stats_db.h) */
bool stats_db_daily_status(const char *day, int *id, bool *solved_today, int *streak)
{
    if (!sdb_gen()) {
        return false;    /* no database this session */
    }
    snprintf(s_gen.day, sizeof(s_gen.day), "%s", day);
    if (!sdb_gen_call(CMD_DAILY)) {
        return false;
    }
    *id = s_gen.id;
    *solved_today = s_gen.solved_today;
    *streak = s_gen.streak;
    return true;
}

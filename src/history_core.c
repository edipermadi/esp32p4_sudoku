/**
 * @file history_core.c
 * @ingroup history_core
 * @brief The History screen's data, without LVGL: the month asked for, its days' counts, the day
 *        picked and its games.
 */
#include "history_core.h"
#include "replay_video.h"
#include "timezones.h"
#include <string.h>
#include <time.h>

static int hz_year;                       /**< the month asked for / shown: its year (0: not yet) */
static int hz_month;                      /**< ... its month */
static bool hz_latest_pending;            /**< asked for "the latest month"; show what comes back */
static uint8_t hz_count[31];              /**< games per day of the month shown (badges) */
static int hz_sel_year;                   /**< the day picked: its year (0: none) */
static int hz_sel_month;                  /**< ... its month */
static int hz_sel_day;                    /**< ... its day */
static int64_t hz_row_solve[STATS_MAX_DAY_ROWS];   /**< each row's solve to replay (0: none) */

/**
 * @brief Asks sudoku.db for a month's games.
 * @param[in] year   the year (0: the latest month that has any)
 * @param[in] month  1..12
 */
static void ask_month(int year, int month)
{
    hz_year = year;
    hz_month = month;
    stats_db_query_month(year, month);
}

/* (documented in history_core.h) */
bool history_today(int *year, int *month, int *day)
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) {
        return false;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    *year = tm.tm_year + 1900;
    *month = tm.tm_mon + 1;
    *day = tm.tm_mday;
    return true;
}

/* (documented in history_core.h) */
void history_ask_current(int year, int month)
{
    hz_latest_pending = false;
    ask_month(year, month);
}

/* (documented in history_core.h) */
void history_ask_latest(void)
{
    hz_latest_pending = true;                       /* no clock: open on the latest month played */
    ask_month(0, 0);
}

/* (documented in history_core.h) */
void history_follow(int year, int month)
{
    if (is_stats_db_ready() && !hz_latest_pending && (year != hz_year || month != hz_month)) {
        ask_month(year, month);
    }
}

/* (documented in history_core.h) */
bool history_month_asked(int *year, int *month)
{
    *year = hz_year;
    *month = hz_month;
    return hz_year != 0;
}

/* (documented in history_core.h) */
const stats_month_t *history_take_month(bool *jump)
{
    static stats_month_t m;
    *jump = false;
    if (!stats_db_take_month(&m)) {
        return NULL;
    }
    if (hz_latest_pending && m.year) {
        *jump = true;
        hz_year = m.year;
        hz_month = m.month;
    }
    hz_latest_pending = false;
    if (m.year != hz_year || m.month != hz_month) {
        return NULL;
    }
    memcpy(hz_count, m.count, sizeof(hz_count));
    return &m;
}

/* (documented in history_core.h) */
uint8_t history_day_count(int day)
{
    return day >= 1 && day <= 31 ? hz_count[day - 1] : 0;
}

/* (documented in history_core.h) */
void history_pick(int year, int month, int day)
{
    hz_sel_year = year;
    hz_sel_month = month;
    hz_sel_day = day;
    if (year && is_stats_db_ready()) {
        stats_db_query_day(year, month, day);
    }
}

/* (documented in history_core.h) */
bool is_history_picked(int year, int month, int day)
{
    return hz_sel_year == year && hz_sel_month == month && hz_sel_day == day;
}

/* (documented in history_core.h) */
const stats_day_t *history_take_day(void)
{
    static stats_day_t d;
    if (!stats_db_take_day(&d)) {
        return NULL;
    }
    for (int i = 0; i < d.n; i++) {
        const stats_day_row_t *r = &d.rows[i];
        hz_row_solve[i] = !r->in_progress && r->has_moves && r->solve_id ? r->solve_id : 0;   /* tap to watch it again */
    }
    return &d;
}

/* (documented in history_core.h) */
int64_t history_row_solve(int i)
{
    return i >= 0 && i < STATS_MAX_DAY_ROWS ? hz_row_solve[i] : 0;
}

/* (documented in history_core.h) */
void history_open_row(int i)
{
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !hz_row_solve[i]) {
        return;
    }
    replay_set_want_export(false);
    stats_db_query_replay(hz_row_solve[i]);
}

/**
 * @file trophies_view.c
 * @ingroup trophies_view
 * @brief The day on the Daily tab and the week on the Weekly tab: moving them, and asking sudoku.db for them.
 */
#include "trophies_view.h"
#include "trophies_dates.h"
#include <stdio.h>
#include <string.h>
#include "stats_db.h"

static char tr_monday[12];                /**< week on the Weekly tab, "" = this week */
static char tr_day[12];                   /**< day on the leaderboard, "" = today */

/* (documented in trophies_view.h) */
void trophies_reset_days(void)
{
    tr_day[0] = 0;
    tr_monday[0] = 0;
}

/* (documented in trophies_view.h) */
bool trophies_ask_week(void)
{
    char now[12];
    if (!trophies_this_monday(now)) {
        return false;
    }
    if (!tr_monday[0] || strcmp(tr_monday, now) > 0) {
        snprintf(tr_monday, sizeof(tr_monday), "%s", now);
    }
    stats_db_query_week(tr_monday);
    return true;
}

/* (documented in trophies_view.h) */
void trophies_ask_board(const char *today)
{
    if (!tr_day[0] || strcmp(tr_day, today) > 0) {
        snprintf(tr_day, sizeof(tr_day), "%s", today);
    }
    stats_db_query_board(tr_day);
}

/* (documented in trophies_view.h) */
void trophies_step_day(const char *today, int delta)
{
    if (!tr_day[0]) {
        snprintf(tr_day, sizeof(tr_day), "%s", today);
    }
    trophies_day_shift(tr_day, delta);
    if (strcmp(tr_day, today) > 0) {
        snprintf(tr_day, sizeof(tr_day), "%s", today);
    }
    stats_db_query_board(tr_day);
}

/* (documented in trophies_view.h) */
void trophies_step_week(int delta)
{
    char now[12];
    if (!trophies_this_monday(now)) {
        return;
    }
    if (!tr_monday[0]) {
        snprintf(tr_monday, sizeof(tr_monday), "%s", now);
    }
    trophies_day_shift(tr_monday, 7 * delta);
    if (strcmp(tr_monday, now) > 0) {
        snprintf(tr_monday, sizeof(tr_monday), "%s", now);
    }
    stats_db_query_week(tr_monday);
}

/* (documented in trophies_view.h) */
const char *trophies_day_shown(void)
{
    return tr_day;
}

/* (documented in trophies_view.h) */
const char *trophies_week_shown(void)
{
    return tr_monday;
}

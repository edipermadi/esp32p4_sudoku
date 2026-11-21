/**
 * @file trophies_dates.c
 * @ingroup trophies_dates
 * @brief Date arithmetic on 'YYYY-MM-DD' dates: a day moved, this week's Monday.
 */
#include "trophies_dates.h"
#include <stdio.h>
#include <time.h>
#include "timezones.h"

/* (documented in trophies_dates.h) */
void trophies_day_shift(char *day, int delta)
{
    struct tm tm = {0};
    if (sscanf(day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
        return;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_mday += delta;
    tm.tm_hour = 12;                                    /* clear of DST changes */
    mktime(&tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
}

/* (documented in trophies_dates.h) */
bool trophies_this_monday(char day[12])
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) {
        return false;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_mday -= (tm.tm_wday + 6) % 7;
    tm.tm_hour = 12;
    mktime(&tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    return true;
}

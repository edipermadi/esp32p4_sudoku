/**
 * @file daily_core.c
 * @ingroup daily_core
 * @brief The daily puzzle: which one today is, its seed and its name, without LVGL (the button and the
 *        Trophies tab: ui_newpuzzle.c, ui_trophies_daily.c).
 */
#include "daily_core.h"
#include <stdio.h>
#include <time.h>
#include "timezones.h"

/**
 * @brief One puzzle a day, the same for every player: made from the date (its seed),
 * with a variant and level by weekday (DAILY_*): Mon Classic Easy, Tue Mini
 * Medium, Wed X-Sudoku Medium, Thu Hyper Hard, Fri Killer Medium, Sat Jigsaw
 * Hard, Sun Classic Expert. Stored like any generated puzzle, with its date
 * (generated.day): a day's puzzle, once made, stays what it is. The streak
 * counts days in a row with the daily solved (stats_db).
 */
static const uint8_t DAILY_LEVEL[7] = {GEN_EXPERT, GEN_EASY, GEN_MEDIUM, GEN_MEDIUM, GEN_HARD, GEN_MEDIUM, GEN_HARD};
/** each weekday's daily variant (Sunday first) */
static const uint8_t DAILY_VARIANT[7] = {VAR_CLASSIC, VAR_CLASSIC, VAR_MINI, VAR_X, VAR_HYPER, VAR_KILLER, VAR_JIGSAW};

/* (documented in daily_core.h) */
bool daily_today(char day[12], int *level, int *variant)
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) {
        return false;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    *level = DAILY_LEVEL[tm.tm_wday];
    if (variant) {
        *variant = DAILY_VARIANT[tm.tm_wday];
    }
    return true;
}

/* (documented in daily_core.h) */
void daily_kind_text(int variant, int level, char *buf, size_t n)
{
    const char *lv = level >= 0 && level < GEN_LEVELS ? gen_level_name(level) : "";
    if (variant > VAR_CLASSIC && variant < VAR_COUNT) {
        snprintf(buf, n, "%s %s", variant_name(variant), lv);
    } else {
        snprintf(buf, n, "%s", lv);
    }
}

/* (documented in daily_core.h) */
uint32_t daily_seed(const char *day)                 /* FNV-1a of the date */
{
    uint32_t h = 2166136261u;
    for (const char *p = day; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h ? h : 1;
}

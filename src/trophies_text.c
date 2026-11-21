/**
 * @file trophies_text.c
 * @ingroup trophies_text
 * @brief Texts the Trophies screen shows: an achievement's progress, the unlock toast.
 */
#include "trophies_text.h"
#include <stdio.h>

/* (documented in trophies_text.h) */
void trophies_ach_progress_text(const stats_ach_t *a, char *buf, size_t n)
{
    switch (a->unit) {
    case ACH_UNIT_MINUTES:
        if (a->value >= 60)
            snprintf(buf, n, "%ld h %ld min / %ld h", (long)(a->value / 60), (long)(a->value % 60),
                     (long)(a->goal / 60));
        else {
            snprintf(buf, n, "%ld min / %ld h", (long)a->value, (long)(a->goal / 60));
        }
        break;
    case ACH_UNIT_DAYS:
        snprintf(buf, n, "%ld / %ld days", (long)a->value, (long)a->goal);
        break;
    default:
        snprintf(buf, n, "%ld / %ld", (long)a->value, (long)a->goal);
        break;
    }
}

/* (documented in trophies_text.h) */
bool trophies_take_unlocked(char *text, size_t n)
{
    char titles[160];
    int count = 0;
    if (!stats_db_take_unlocked(titles, sizeof(titles), &count)) {
        return false;
    }
    snprintf(text, n, "%s unlocked:\n%s", count == 1 ? "Achievement" : "Achievements", titles);
    return true;
}

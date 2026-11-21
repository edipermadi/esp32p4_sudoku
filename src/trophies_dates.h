/**
 * @file trophies_dates.h
 * @ingroup trophies_dates
 * @brief Date arithmetic on 'YYYY-MM-DD' dates: a day moved, this week's Monday.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup trophies_dates Dates
 * @ingroup trophies_core
 * @brief Date arithmetic on 'YYYY-MM-DD' dates: a day moved, this week's Monday.
 * @{
 */

/**
 * @brief Moves a date by some days, in place.
 * @param day    'YYYY-MM-DD'
 * @param delta  days (negative: back)
 */
void trophies_day_shift(char *day, int delta);
/**
 * @brief The Monday of this week (local time).
 * @param day  gets it, 'YYYY-MM-DD'
 * @return false while the clock isn't set
 */
bool trophies_this_monday(char day[12]);

/** @} */

#ifdef __cplusplus
}
#endif

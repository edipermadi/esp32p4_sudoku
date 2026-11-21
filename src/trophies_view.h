/**
 * @file trophies_view.h
 * @ingroup trophies_view
 * @brief The day on the Daily tab and the week on the Weekly tab: moving them, and asking sudoku.db for them.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup trophies_view Day and week shown
 * @ingroup trophies_core
 * @brief The day on the Daily tab and the week on the Weekly tab: moving them, and asking sudoku.db for them.
 * @{
 */

/** @brief The Daily tab back on today and the Weekly one on this week (the screen is opened). */
void trophies_reset_days(void);
/**
 * @brief The day on the Daily tab.
 * @return 'YYYY-MM-DD', "" = today (not asked for yet)
 */
const char *trophies_day_shown(void);
/**
 * @brief The week on the Weekly tab.
 * @return its Monday, 'YYYY-MM-DD', "" = this week (not asked for yet)
 */
const char *trophies_week_shown(void);
/**
 * @brief Asks for the week shown (this week if none, or if it's in the future).
 * @return false while the clock isn't set (nothing asked)
 */
bool trophies_ask_week(void);
/**
 * @brief Asks for the day shown's leaderboard (today if none, or if it's in the future).
 * @param today  today's date, 'YYYY-MM-DD'
 */
void trophies_ask_board(const char *today);
/**
 * @brief Moves the day shown (never past today) and asks for its leaderboard.
 * @param today  today's date, 'YYYY-MM-DD'
 * @param delta  days (negative: back)
 */
void trophies_step_day(const char *today, int delta);
/**
 * @brief Moves the week shown (never past this week) and asks for it; nothing while the clock
 *        isn't set.
 * @param delta  weeks (negative: back)
 */
void trophies_step_week(int delta);

/** @} */

#ifdef __cplusplus
}
#endif

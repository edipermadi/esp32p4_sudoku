/**
 * @file history_core.h
 * @ingroup history_core
 * @brief The History screen's data, without LVGL (the screen: ui_history.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup history_core History core
 * @ingroup topic_history
 * @brief What the History screen shows, apart from drawing it: which month is asked for, how many
 *        games each of its days had, the day picked and its games, from sudoku.db.
 *
 * Days are local dates; solves from before the clock was set (Wi-Fi) have no date and don't
 * appear. Without the clock the screen opens on the latest month that has any games
 * (history_ask_latest()).
 * @{
 */

/** @name Months
 *  @{ */
/**
 * @brief Today's date (for the calendar).
 * @param year   gets the year
 * @param month  gets the month, 1..12
 * @param day    gets the day, 1..31
 * @return false while the clock isn't set
 */
bool history_today(int *year, int *month, int *day);
/**
 * @brief Asks for a month's games (the screen is opened on it).
 * @param year   the year
 * @param month  1..12
 */
void history_ask_current(int year, int month);
/** @brief Asks for the latest month that has any games (no clock: the screen opens on it). */
void history_ask_latest(void);
/**
 * @brief The calendar shows a month: asks for it if it isn't the one asked for (its arrows; not
 *        while the latest month is still awaited, nor without sudoku.db).
 * @param year   the year shown
 * @param month  the month shown
 */
void history_follow(int year, int month);
/**
 * @brief The month asked for (to show it again after a rebuild).
 * @param year   gets the year
 * @param month  gets the month
 * @return false if none has been yet
 */
bool history_month_asked(int *year, int *month);
/**
 * @brief The month sudoku.db answered with, if it's the one asked for; its days' counts are kept.
 * @param jump  gets true if it is the latest month asked for (history_ask_latest()): show it
 * @return it, or NULL (none arrived, or another month's)
 */
const stats_month_t *history_take_month(bool *jump);
/**
 * @brief How many games a day of the month shown had (its badge).
 * @param day  1..31
 * @return how many
 */
uint8_t history_day_count(int day);
/** @} */

/** @name The day picked
 *  @{ */
/**
 * @brief Picks a day, and asks for its games (with sudoku.db).
 * @param year   the year (0: none picked)
 * @param month  1..12
 * @param day    1..31
 */
void history_pick(int year, int month, int day);
/**
 * @brief Whether a day is the one picked.
 * @param year   the year
 * @param month  1..12
 * @param day    1..31
 * @return true: picked
 */
bool is_history_picked(int year, int month, int day);
/**
 * @brief The day sudoku.db answered with; its rows' replays are kept.
 * @return it, or NULL if none arrived since the last call
 */
const stats_day_t *history_take_day(void);
/**
 * @brief A row's solve, if it can be replayed (solved, with a move log).
 * @param i  the row
 * @return solves.id, 0 if it can't
 */
int64_t history_row_solve(int i);
/**
 * @brief Opens a row's replay (asks sudoku.db for it; the Replay screen opens on the answer).
 * @param i  the row
 */
void history_open_row(int i);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

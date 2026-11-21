/**
 * @file trophies_results.h
 * @ingroup trophies_results
 * @brief sudoku.db's answers for the Trophies screen, taken into buffers of their own (PSRAM).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup trophies_results Results
 * @ingroup trophies_core
 * @brief sudoku.db's answers for the Trophies screen, taken into buffers of their own (PSRAM).
 * @{
 */

/**
 * @brief Makes the achievements' and leaderboard's buffers (PSRAM), the first time.
 * @return false if out of memory (take nothing this time)
 */
bool trophies_bufs(void);
/**
 * @brief The achievements sudoku.db answered with (any player's: check player_id).
 * @return them, or NULL if none arrived since the last call
 */
const stats_achievements_t *trophies_take_achievements(void);
/**
 * @brief The leaderboard of the day shown (trophies_day_shown()).
 * @return it, or NULL if none arrived (or it was another day's)
 */
const stats_board_t *trophies_take_board(void);
/**
 * @brief The records sudoku.db answered with (any player's: check player_id).
 * @return them, or NULL if none arrived since the last call
 */
const stats_records_t *trophies_take_records(void);
/**
 * @brief The week shown (trophies_week_shown()).
 * @return it, or NULL if none arrived (or it was another week)
 */
const stats_week_t *trophies_take_week(void);

/** @} */

#ifdef __cplusplus
}
#endif

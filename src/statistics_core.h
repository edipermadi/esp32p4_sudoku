/**
 * @file statistics_core.h
 * @ingroup statistics_core
 * @brief The Statistics screen's data, without LVGL (the screen: ui_stats.c).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup statistics_core Statistics core
 * @ingroup topic_statistics
 * @brief What the Statistics screen shows, apart from drawing it.
 *
 * The screen draws a stats_view_t (stats_db.h): normally the answer to a query on sudoku.db; without
 * the database, the same built from this session's statistics in RAM. A row of the list picks
 * what: all levels, a collection, or a kind of puzzle made on the board. The progress chart shows
 * the newest solves (STATS_RECENT) with an average of the last AVG_WIN.
 * @{
 */

#define AVG_WIN 5                         /**< the chart's average: of the last this many solves */

/**
 * @brief Makes the view's buffer (PSRAM: internal RAM is tight).
 * @return false if out of memory
 */
bool statistics_init(void);
/**
 * @brief Picks a row of the list.
 * @param[in] row  0 all levels, 1.. collection row-1, then the kinds (STATS_LEVEL_KIND())
 */
void statistics_select(int row);
/**
 * @brief The row picked.
 * @return it (0: all levels)
 */
int statistics_sel(void);
/**
 * @brief Asks for the row picked's view: from sudoku.db (it comes later: statistics_take()), or,
 *        without it, built from RAM at once.
 * @return true if asked of sudoku.db; false: the view is ready (from RAM)
 */
bool statistics_ask(void);
/**
 * @brief Takes sudoku.db's answer as the view shown.
 * @param[in] v  the answer
 */
void statistics_take(const stats_view_t *v);
/**
 * @brief The view shown.
 * @return it
 */
const stats_view_t *statistics_view(void);
/**
 * @brief Whether the view shown came from sudoku.db (else from RAM).
 * @return true: sudoku.db
 */
bool is_statistics_from_db(void);
/**
 * @brief A kind's four levels added up (fastest: the lowest).
 * @param[in] v  the view
 * @param[in] k  the kind (stats_kind_t)
 * @param[out] t  gets the total
 */
void statistics_kind_total(const stats_view_t *v, int k, stats_level_t *t);
/**
 * @brief The average of the fewest mistakes per puzzle, as text: "1.4", or "-" when nothing is known.
 * @param[out] buf  gets it
 * @param[in] n    its size
 * @param[in] st   the level
 */
void statistics_avg_mistakes(char *buf, size_t n, const stats_level_t *st);

/** @name The progress chart
 *  @{ */
/**
 * @brief A scale step that gives at most 5 lines up to a time.
 * @param[in] max  the longest time, seconds
 * @return the step, seconds
 */
uint32_t statistics_chart_step(uint32_t max);
/**
 * @brief The trailing average of up to AVG_WIN solves ending at one.
 * @param[in] v  the view
 * @param[in] i  the solve (0: oldest shown)
 * @return seconds
 */
uint32_t statistics_chart_avg(const stats_view_t *v, int i);
/**
 * @brief The fastest of the solves shown.
 * @param[in] v  the view
 * @return seconds
 */
uint32_t statistics_fastest(const stats_view_t *v);
/**
 * @brief The newest AVG_WIN solves against the first AVG_WIN shown.
 * @param[in] v      the view
 * @param[out] first  gets the first ones' average, seconds
 * @param[out] last   gets the newest ones' average, seconds
 * @param[out] pct    gets how much faster the newest are, % (negative: slower)
 * @return false with fewer than 2 x AVG_WIN solves shown
 */
bool statistics_trend(const stats_view_t *v, uint32_t *first, uint32_t *last, int *pct);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

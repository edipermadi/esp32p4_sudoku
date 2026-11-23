/**
 * @file daily_core.h
 * @ingroup daily_core
 * @brief The daily puzzle: which one today is, its seed and its name, without LVGL.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup daily_core Daily puzzle core
 * @ingroup topic_puzzles
 * @brief Which puzzle is today's: its date, variant and level by weekday, and the seed it is made from.
 * @{
 */

/**
 * @brief Today's daily puzzle: its date (local time), level and variant.
 * @param[out] day      gets the date, "YYYY-MM-DD"
 * @param[out] level    gets its level (gen_level_t)
 * @param[out] variant  gets its variant (sudoku_variant_t), or NULL
 * @return false while the clock isn't set
 */
bool daily_today(char day[12], int *level, int *variant);

/**
 * @brief The daily puzzle's seed: FNV-1a of its date, so every board makes the same puzzle that day.
 * @param[in] day  the date, "YYYY-MM-DD"
 * @return the seed (never 0: 0 means a random one to the generator)
 */
uint32_t daily_seed(const char *day);

/**
 * @brief "Hard", "X-Sudoku Medium": a daily puzzle's level, with its variant unless Classic.
 * @param[in] variant  sudoku_variant_t
 * @param[in] level    gen_level_t
 * @param[out] buf     gets it
 * @param[in] n        its size
 */
void daily_kind_text(int variant, int level, char *buf, size_t n);

/** @} */

#ifdef __cplusplus
}
#endif

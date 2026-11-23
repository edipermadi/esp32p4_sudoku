/**
 * @file game_core.h
 * @ingroup game_core
 * @brief The game's rules check, without LVGL (the game screen: ui_game.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup game_core Game core
 * @ingroup topic_game
 * @brief What the game screen works out apart from drawing it: which digits break the puzzle's rules.
 * @{
 */

/**
 * @brief Marks every filled cell whose digit breaks a rule of the puzzle: a repeat in a unit, and each variant's
 *        own rules (marks, cages, lines, outside clues) as far as the digits already in can tell.
 * @param[in] geo   the puzzle's geometry (sudoku_geom(), or a Jigsaw's)
 * @param[in] val   81 digits, 0 = empty
 * @param[out] out  81 flags: true = the digit breaks a rule
 * @return how many cells are marked
 */
int game_conflicts(const sudoku_geom_t *geo, const uint8_t *val, bool *out);

/** @} */

#ifdef __cplusplus
}
#endif

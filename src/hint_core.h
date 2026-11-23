/**
 * @file hint_core.h
 * @ingroup hint_core
 * @brief A hint's words and highlights, without LVGL (the card: ui_hints.c).
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
 * @defgroup hint_core Hint core
 * @ingroup topic_game
 * @brief What a hint says and which cells it highlights, from the step the hint engine found
 *        (sudoku_next_step()).
 * @{
 */

/**
 * @brief A hint's text, its highlights and its button.
 * @param[in] geo       the puzzle's board
 * @param[in] h         the step
 * @param[in] stage     1: the nudge (where to look), 2: the reason and the cell
 * @param[in] is_nudge  Hints: Nudge only (the nudge doesn't offer More)
 * @param[out] text     gets the hint's text
 * @param[in] text_n    its size (640 is enough)
 * @param[out] mark     gets 81 highlights: 0, 1 = where to look, 2 = the cell
 * @return the button's label: "More", "Place it" or "Erase it"
 */
const char *hint_explain(const sudoku_geom_t *geo, const sudoku_step_t *h, int stage, bool is_nudge, char *text,
                         size_t text_n, uint8_t *mark);

/** @} */

#ifdef __cplusplus
}
#endif

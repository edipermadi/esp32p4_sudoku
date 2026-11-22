/**
 * @file puzzles_solved.h
 * @ingroup puzzles_solved
 * @brief Solved counts on the Puzzles screen: per collection, and the collection shown's puzzles.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup puzzles_solved Solved
 * @ingroup puzzles_core
 * @brief The current player's solved puzzles: the built-in collections from the board's RAM, the
 *        imported and saved ones from sudoku.db (which also says which of the shown one's are solved).
 * @{
 */

/**
 * @brief How many of a collection's puzzles are solved.
 * @param[in] c      the collection
 * @param[in] shown  the collection shown: if c is it (imported or saved), which are solved is kept
 *               (is_puzzles_solved())
 * @return how many
 */
uint16_t puzzles_solved_count(int c, int shown);
/**
 * @brief Whether a puzzle of the collection shown (imported or saved) is solved, as
 *        puzzles_solved_count() found.
 * @param[in] i  its place, 0..
 * @return true: solved
 */
bool is_puzzles_solved(int i);
/**
 * @brief The collection panel's numbers: puzzles solved, and (built-in collections, whose best
 *        times the board keeps) the fastest and the times added up.
 * @param[in] c     the collection shown (after puzzles_solved_count() on it)
 * @param[out] n     gets how many are solved
 * @param[out] best  gets the fastest best time, seconds (0: none known)
 * @param[out] sum   gets the best times added up, seconds
 */
void puzzles_panel(int c, uint16_t *n, uint32_t *best, uint32_t *sum);

/** @} */

#ifdef __cplusplus
}
#endif

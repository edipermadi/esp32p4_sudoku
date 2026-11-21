/**
 * @file puzzles_starred.h
 * @ingroup puzzles_starred
 * @brief The current player's starred puzzles (bookmarks), for the Puzzles screen.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup puzzles_starred Starred
 * @ingroup puzzles_core
 * @brief The current player's bookmarks (the star on the game screen), newest first, from sudoku.db.
 * @{
 */

/**
 * @brief Reads the starred puzzles.
 * @return how many; -1 without sudoku.db
 */
int puzzles_load_starred(void);
/**
 * @brief The starred puzzles read.
 * @return how many
 */
int puzzles_starred_count(void);
/**
 * @brief A starred puzzle.
 * @param i  0 .. puzzles_starred_count() - 1
 * @return its id
 */
uint16_t puzzles_starred_id(int i);
/**
 * @brief Takes a puzzle's star off.
 * @param i   its place in the list read
 * @param id  gets its id
 * @return false if there's no such place
 */
bool puzzles_unstar(int i, uint16_t *id);

/** @} */

#ifdef __cplusplus
}
#endif

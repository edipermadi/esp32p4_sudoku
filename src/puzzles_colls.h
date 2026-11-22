/**
 * @file puzzles_colls.h
 * @ingroup puzzles_colls
 * @brief The player's collections on the Puzzles screen: deleting one, exporting one, taking a puzzle
 *        out of one.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup puzzles_colls Collections' actions
 * @ingroup puzzles_core
 * @brief What the Saved tab does to an imported or saved collection in sudoku.db.
 * @{
 */

/**
 * @brief Deletes an imported or saved collection (its puzzles stay on the board: solves, History and
 *        Gameplays keep them; an imported one's file can be imported again).
 * @param[in] c     the collection
 * @param[out] name  gets its name
 * @param[in] n     its size
 * @return false if sudoku.db couldn't do it
 */
bool puzzles_delete_coll(int c, char *name, size_t n);
/**
 * @brief Writes a saved collection to the card (puzzles/\<name\>.sdx).
 * @param[in] c    the collection
 * @param[out] msg  gets what happened
 * @param[in] n    its size
 * @return false if it isn't a saved collection (nothing done)
 */
bool puzzles_export(int c, char *msg, size_t n);
/**
 * @brief Takes a puzzle out of a saved collection (the collection goes with its last).
 * @param[in] c    the collection
 * @param[in] i    the puzzle's place
 * @param[out] msg  gets what happened ("Easy #3 taken out of Mine.")
 * @param[in] n    its size
 * @return -1: not a saved collection's puzzle (nothing done); 0: sudoku.db couldn't; 1: done
 */
int puzzles_take_out(int c, int i, char *msg, size_t n);

/** @} */

#ifdef __cplusplus
}
#endif

/**
 * @file puzzles_search.h
 * @ingroup puzzles_search
 * @brief The Puzzles screen's search: the puzzles whose name or variant has what was typed.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup puzzles_search Search
 * @ingroup puzzles_core
 * @brief Every puzzle (built in, made on the board, imported, typed in) matched against a search by its name,
 *        variant and level (search_core.h); the right pane shows the first PUZZLES_SEARCH_MAX.
 * @{
 */

#define PUZZLES_SEARCH_MAX 100          /**< results shown at most (a collection's worth of rows) */

/**
 * @brief Searches every puzzle and shows the results (the search mode on): made on the board or imported
 *        first, newest first, then the built-in ones.
 * @param[in] query  what was typed (words, any order)
 * @return how many puzzles match (may be more than are kept)
 */
int puzzles_search_run(const char *query);
/** @brief The search mode off: the right pane shows its collection again. */
void puzzles_search_off(void);
/**
 * @brief Is the right pane showing search results?
 * @return true if so
 */
bool is_puzzles_search(void);
/**
 * @brief The search last run.
 * @return its text ("" none)
 */
const char *puzzles_search_query(void);
/**
 * @brief How many puzzles matched the last search.
 * @return the count (more than puzzles_search_count() when there were too many to keep)
 */
int puzzles_search_total(void);
/**
 * @brief How many results are kept (and shown).
 * @return 0..PUZZLES_SEARCH_MAX
 */
int puzzles_search_count(void);
/**
 * @brief A result.
 * @param[in] i  0..puzzles_search_count()-1
 * @return its puzzle id
 */
uint16_t puzzles_search_id(int i);
/**
 * @brief Has the current player solved a result? (Read with the search: sudoku.db.)
 * @param[in] i  0..puzzles_search_count()-1
 * @return true if solved
 */
bool is_puzzles_search_solved(int i);

/** @} */

#ifdef __cplusplus
}
#endif

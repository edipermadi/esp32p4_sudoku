/**
 * @file collections.h
 * @ingroup collections
 * @brief The Puzzles screen's collections, as data: the declarations only, without LVGL (defined in
 *        ui_common.c and ui_game_tools.c).
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
 * @defgroup collections Collections
 * @brief The collections, in the order the Puzzles screen lists them: the built-in ones
 *        (PUZZLE_COLLECTION_COUNT), then the ones imported from the card, then the player's saved
 *        ones (sudoku.db). The built-in and imported ones are runs of puzzle ids.
 * @{
 */

/**
 * @brief The collections listed.
 * @return how many
 */
int coll_total(void);
/**
 * @brief Which saved collection a collection is.
 * @param c  the collection
 * @return its index in ui_saved() (\>= 0), or \< 0 for a built-in or imported one
 */
int coll_saved_index(int c);
/**
 * @brief The Puzzles tab a collection is on.
 * @param c  the collection
 * @return 0 Built-in, 1 Saved (imported and saved)
 */
int coll_tab(int c);
/**
 * @brief A collection's name.
 * @param c  the collection
 * @return it
 */
const char *coll_name(int c);
/**
 * @brief A built-in or imported collection's first puzzle id (its puzzles are a run).
 * @param c  the collection
 * @return the id
 */
uint16_t coll_first(int c);
/**
 * @brief A collection's puzzles.
 * @param c  the collection
 * @return how many
 */
int coll_size(int c);
/**
 * @brief A puzzle of a collection.
 * @param c  the collection
 * @param i  0 .. coll_size() - 1
 * @return its id
 */
uint16_t coll_puzzle(int c, int i);
/**
 * @brief Where a puzzle is in a collection.
 * @param c   the collection
 * @param id  the puzzle
 * @return its place, -1: not there
 */
int coll_find(int c, uint16_t id);
/**
 * @brief The collection of a puzzle (built-in or imported).
 * @param id  the puzzle
 * @return the collection, -1 for others
 */
int coll_of(uint16_t id);
/** @brief Reads the imported and saved collections from sudoku.db again (after one comes or goes). */
void imports_reload(void);
/**
 * @brief The imported collections.
 * @return how many
 */
int ui_nimports(void);
/**
 * @brief The imported collection a puzzle is in, and its number there.
 * @param id      the puzzle
 * @param number  gets its number (may be NULL)
 * @return the imported collection, -1: none
 */
int import_of(uint16_t id, int *number);
/**
 * @brief The saved collections.
 * @return them (ui_nsaved())
 */
const stats_saved_t *ui_saved(void);
/**
 * @brief The saved collections.
 * @return how many
 */
int ui_nsaved(void);
/**
 * @brief A puzzle's name: "Easy #12", "X-Sudoku Hard", "Collection #3" ...
 * @param id   the puzzle
 * @param buf  gets it
 * @param n    its size
 */
void puzzle_name(uint16_t id, char *buf, size_t n);
/**
 * @brief A puzzle made on the board: its givens, variant and level.
 * @param id       the puzzle
 * @param givens   gets the givens (81 cells; may be NULL)
 * @param variant  gets its sudoku_variant_t (may be NULL)
 * @param level    gets its gen_level_t (may be NULL)
 * @return false if it isn't known
 */
bool gen_lookup(uint16_t id, uint8_t *givens, int *variant, int *level);

/** @} */

#ifdef __cplusplus
}
#endif

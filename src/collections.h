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
 * @ingroup topic_puzzles
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
 * @param[in] c  the collection
 * @return its index in ui_saved() (\>= 0), or \< 0 for a built-in or imported one
 */
int coll_saved_index(int c);
/** @name The Puzzles screen's tabs, in their order on the screen (coll_tab())
 *  @{ */
#define COLL_TAB_BUILTIN  0             /**< the built-in collections */
#define COLL_TAB_SAVED    1             /**< the player's own, made on the board (Save) */
#define COLL_TAB_IMPORTED 2             /**< the ones imported from the card */
#define COLL_TABS         3             /**< how many */
/** @} */
/**
 * @brief The Puzzles tab a collection is on.
 * @param[in] c  the collection
 * @return COLL_TAB_BUILTIN, COLL_TAB_SAVED (made on the board) or COLL_TAB_IMPORTED (from the card)
 */
int coll_tab(int c);
/**
 * @brief A collection's name.
 * @param[in] c  the collection
 * @return it
 */
const char *coll_name(int c);
/**
 * @brief When a collection was saved (made on the board) or imported.
 * @param[in] c  the collection
 * @return Unix time UTC; 0 for a built-in one, or one from before collections were dated
 */
int64_t coll_created(int c);
/**
 * @brief A built-in or imported collection's first puzzle id (its puzzles are a run).
 * @param[in] c  the collection
 * @return the id
 */
uint16_t coll_first(int c);
/**
 * @brief A collection's puzzles.
 * @param[in] c  the collection
 * @return how many
 */
int coll_size(int c);
/**
 * @brief A puzzle of a collection.
 * @param[in] c  the collection
 * @param[in] i  0 .. coll_size() - 1
 * @return its id
 */
uint16_t coll_puzzle(int c, int i);
/**
 * @brief Where a puzzle is in a collection.
 * @param[in] c   the collection
 * @param[in] id  the puzzle
 * @return its place, -1: not there
 */
int coll_find(int c, uint16_t id);
/**
 * @brief The collection of a puzzle (built-in or imported).
 * @param[in] id  the puzzle
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
 * @param[in] id      the puzzle
 * @param[out] number  gets its number (may be NULL)
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
 * @param[in] id   the puzzle
 * @param[out] buf  gets it
 * @param[in] n    its size
 */
void puzzle_name(uint16_t id, char *buf, size_t n);
/**
 * @brief A puzzle made on the board: its givens, variant and level.
 * @param[in] id       the puzzle
 * @param[out] givens   gets the givens (81 cells; may be NULL)
 * @param[out] variant  gets its sudoku_variant_t (may be NULL)
 * @param[out] level    gets its gen_level_t (may be NULL)
 * @return false if it isn't known
 */
bool gen_lookup(uint16_t id, uint8_t *givens, int *variant, int *level);

/** @} */

#ifdef __cplusplus
}
#endif

/**
 * @file puzzles_select.h
 * @ingroup puzzles_select
 * @brief What the Puzzles screen shows: the tab, the collection, Starred.
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup puzzles_select Selection
 * @ingroup puzzles_core
 * @brief The tab shown (0 Built-in, 1 Saved: imported and saved), the collection shown, the one last
 *        shown in each tab, and whether the right pane shows the starred puzzles instead.
 * @{
 */

/**
 * @brief The tab shown.
 * @return 0 Built-in, 1 Saved
 */
int puzzles_tab(void);
/**
 * @brief Shows a tab (puzzles_settle() then picks its collection).
 * @param t  0 Built-in, 1 Saved
 */
void puzzles_set_tab(int t);
/**
 * @brief The collection shown.
 * @return it (collections.h)
 */
int puzzles_coll(void);
/**
 * @brief Shows a collection.
 * @param c  the collection
 */
void puzzles_set_coll(int c);
/**
 * @brief Whether the right pane shows the starred puzzles.
 * @return true: Starred
 */
bool is_puzzles_starred(void);
/**
 * @brief The right pane shows the starred puzzles, or the collection again.
 * @param on  true: Starred
 */
void puzzles_set_starred(bool on);
/**
 * @brief Makes the collection shown fit the tab shown: the tab's last, or its first (and remembers it
 *        as the tab's last).
 * @return false: the tab has no collection (the Saved tab with nothing in it)
 */
bool puzzles_settle(void);
/** @brief A saved or imported collection was deleted: the Saved tab starts at its first again. */
void puzzles_forget_saved(void);
/**
 * @brief Shows a collection and its tab (the one the puzzle just played is in).
 * @param c  the collection
 */
void puzzles_follow(int c);

/** @} */

#ifdef __cplusplus
}
#endif

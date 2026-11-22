/**
 * @file ui_list_part.h
 * @ingroup ui_list
 * @brief What the Puzzles screen's files share. Only they include it.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ui_list Puzzles screen
 * @ingroup topic_puzzles
 * @brief Puzzles (Home): the collections on the left (tabs: Built-in, Saved), the one picked on the
 *        right as a grid of numbers (built-in) or a list of rows (Saved), or the starred puzzles.
 *
 * | File               | What's in it |
 * |--------------------|--------------|
 * | ui_list.c          | the screen: the title row, the tabs, the collection column, the panel (name, solved, fastest, average), the grid of numbers; refreshing it |
 * | ui_list_rows.c     | the right pane's rows: a saved or imported collection's puzzles (Saved, Imported tabs), and the row look the starred list uses too |
 * | ui_list_starred.c  | Starred: the player's bookmarks in the right pane, unstarred from there |
 * | ui_list_manage.c   | the Saved and Imported tabs' Export and Delete of the collection shown |
 *
 * What it shows and does, apart from drawing it: @ref puzzles_core.
 * @{
 */

/** @name The rows (ui_list_rows.c)
 *  @{ */
/**
 * @brief Builds the right pane's list of rows (hidden).
 * @param[in] scr  the screen
 * @return it
 */
lv_obj_t *list_rows_build(lv_obj_t *scr);
/** @brief Empties the rows (and scrolls back to the top). */
void list_rows_clear(void);
/**
 * @brief A line of text instead of rows (nothing there, or why not).
 * @param[in] text  the text
 */
void list_rows_note(const char *text);
/**
 * @brief A row: number, puzzle name, a coloured line under it and an optional icon button on the
 *        right.
 * @param[in] i       its place (shown from 1; the taps' user data)
 * @param[in] id      the puzzle
 * @param[in] sub     the line under the name
 * @param[in] sub_c   its colour
 * @param[in] cb      a tap on the row
 * @param[in] act     the button's icon (NULL: none)
 * @param[in] act_c   its colour
 * @param[in] act_cb  a tap on it
 */
void list_add_row(int i, uint16_t id, const char *sub, lv_color_t sub_c, lv_event_cb_t cb, const char *act,
                  lv_color_t act_c, lv_event_cb_t act_cb);
/** @} */

/** @name Starred (ui_list_starred.c)
 *  @{ */
/**
 * @brief Fills the rows with the starred puzzles (newest first), or says why there are none.
 * @return how many; -1 without sudoku.db
 */
int list_fill_starred(void);
/** @} */

/* (the collection's actions, Export and Delete: ui_internal.h, for the ⋮ menu) */

/** @} */

#ifdef __cplusplus
}
#endif

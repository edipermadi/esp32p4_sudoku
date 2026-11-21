/**
 * @file ui_file.h
 * @ingroup ui_file
 * @brief The Files screen (Home > Files).
 */
#pragma once
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ui_file Files screen
 * @ingroup topic_files
 * @brief The microSD card's folders and files, with their size and date (Home > Files).
 *
 * Tap a folder to open it; each row has Rename (on-screen keyboard) and Delete (asks first; into
 * the Recycle bin, where items can be restored or deleted for good). The game's database
 * (sudoku.db and SQLite's journal files next to it) is protected: it can't be renamed or deleted,
 * and nothing can be renamed to its names. Folders are listed by a worker task on core 0. A tap on
 * a JPEG or MP4 opens it in the viewer (ui_media_view.h); a tap on an .sdm, .sdx or .opensudoku
 * puzzle file previews its puzzles on a board, with Close and Import. What the page does on the
 * card: @ref files_core.
 * @{
 */

/**
 * @brief Builds the page into an empty screen. Can be called again after a theme change (the
 *        caller deletes the old screen).
 * @param scr      the screen
 * @param back_cb  runs on the back button
 */
void file_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb);

/**
 * @brief The page is loaded or left: the folder read afresh, or the dialogs and viewers closed.
 * @param shown  true: loaded
 */
void file_ui_set_shown(bool shown);

/** @} */

#ifdef __cplusplus
}
#endif

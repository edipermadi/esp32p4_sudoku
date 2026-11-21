#pragma once
/*
 * Files screen (Home > Files): the microSD card's folders and
 * files, with their size and date. Tap a folder to open it; each row has
 * Rename (on-screen keyboard) and Delete (asks first; folders only when
 * empty). The game's database (sudoku.db and SQLite's journal files next to
 * it) is protected: it can't be renamed or deleted, and nothing can be renamed
 * to its names. Folders are listed by a worker task on core 0. A tap on a
 * JPEG or MP4 opens it in the viewer (ui_media_view.h); a tap on an .sdm, .sdx or
 * .opensudoku puzzle file previews its puzzles on a board, with Close and Import.
 */
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the page into scr (an empty screen). back_cb runs on the back button.
 * Can be called again after a theme change (the caller deletes the old screen). */
void file_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb);

/* Call when the page is loaded (true) or left (false). */
void file_ui_set_shown(bool shown);

#ifdef __cplusplus
}
#endif

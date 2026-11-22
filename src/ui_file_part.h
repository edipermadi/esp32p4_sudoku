/**
 * @file ui_file_part.h
 * @ingroup ui_file
 * @brief The Files page's files: what they share. Only they include it.
 *
 * | File               | What's in it |
 * |--------------------|--------------|
 * | ui_file.c          | the screen around the parts (the back button, the title), the poll for a finished read or purge, the widgets the parts share |
 * | ui_file_list.c     | the list (a row per entry with its buttons), the path, the count, the top bar's Refresh, Up and Empty; a tap on a row |
 * | ui_file_dialog.c   | the dialog: rename with a keyboard, delete into the Recycle bin, delete for good, empty the bin |
 * | ui_file_preview.c  | a puzzle file's preview: its puzzles on a board, Close and Import |
 *
 * The rest of the UI reaches the page through ui_file.h; what it does on the card: files_core.h.
 */
#pragma once
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup ui_file
 *  @{ */

/** @brief What the dialog asks. */
enum {
    DLG_RENAME,                         /**< a new name */
    DLG_TRASH,                          /**< delete: into the Recycle bin */
    DLG_PURGE,                          /**< delete a bin item for good */
    DLG_EMPTY                           /**< empty the bin */
};

/** @name The widgets the parts share (ui_file.c)
 *  @{ */
/**
 * @brief The page.
 * @return its screen
 */
lv_obj_t *file_ui_screen(void);
/**
 * @brief A label.
 * @param[in] parent  its parent
 * @param[in] font    its font
 * @param[in] color   its colour
 * @param[in] txt     its text
 * @return it
 */
lv_obj_t *file_ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt);
/**
 * @brief A row without a look of its own: full width, as tall as what's in it, items centred.
 * @param[in] parent  its parent
 * @return it
 */
lv_obj_t *file_ui_row(lv_obj_t *parent);
/**
 * @brief A small square button with an icon (a row's Rename, Delete, Restore; Up, Refresh).
 * @param[in] parent  its parent
 * @param[in] sym     the icon (and any text)
 * @param[in] color   its colour
 * @param[in] cb      runs on a tap
 * @param[in] user    the tap's user data
 * @return it
 */
lv_obj_t *file_ui_icon_btn(lv_obj_t *parent, const char *sym, lv_color_t color, lv_event_cb_t cb, void *user);
/**
 * @brief A rounded button with text (a dialog's Cancel / OK; the preview's Close / Import).
 * @param[in] parent  its parent
 * @param[in] txt     its text
 * @param[in] bg      its colour
 * @param[in] fg      the text's colour
 * @param[in] cb      runs on a tap
 * @return it
 */
lv_obj_t *file_ui_pill_btn(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb);
/** @} */

/** @name The list (ui_file_list.c)
 *  @{ */
/**
 * @brief Builds the top bar's Refresh, Empty and Up, the path, the count and the list.
 * @param[in] scr  the page
 */
void file_list_build(lv_obj_t *scr);
/** @brief Reads the folder shown again (the worker: files_folder.c), with "Reading..." meanwhile. */
void file_list_rescan(void);
/** @brief Shows the folder read: the path, the count, a row per entry with its buttons. */
void file_list_show(void);
/**
 * @brief Puts a word in the count's place ("Deleting...", "Name taken").
 * @param[in] text  the text
 */
void file_list_count_text(const char *text);
/** @} */

/** @name The dialog (ui_file_dialog.c)
 *  @{ */
/** @brief Builds the dialog (on the top layer, hidden): a title, a message, a name field, Cancel / OK, a keyboard. */
void file_dialog_build(void);
/** @brief Deletes the dialog (before the page is built again). */
void file_dialog_destroy(void);
/**
 * @brief Opens the dialog for an entry.
 * @param[in] idx   the entry (-1 for DLG_EMPTY)
 * @param[in] mode  DLG_RENAME, DLG_TRASH, DLG_PURGE or DLG_EMPTY
 */
void file_dialog_open(int idx, int mode);
/** @brief Hides the dialog (its name field emptied). */
void file_dialog_close(void);
/** @} */

/** @name The puzzle file preview (ui_file_preview.c)
 *  @{ */
/**
 * @brief Opens a puzzle file's preview: its name, what's in it, the board with \< \>, what Import
 *        does, Close and Import.
 * @param[in] i  the entry
 */
void file_preview_open(int i);
/** @brief Closes the preview and frees the file read. */
void file_preview_close(void);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

/**
 * @file ui_file_dialog.c
 * @ingroup ui_file
 * @brief Files: the dialog (on the top layer): rename with a keyboard, delete into the Recycle bin,
 *        delete a bin item for good, empty the bin.
 */
#include "ui_file_part.h"
#include "files_core.h"
#include "ui_palette.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_dlg;                 /**< the dialog's dimmed layer */
static lv_obj_t *s_dlg_title;           /**< its title */
static lv_obj_t *s_dlg_msg;             /**< its message (orange: an error) */
static lv_obj_t *s_ta;                  /**< the new name (rename) */
static lv_obj_t *s_kb;                  /**< the keyboard (rename) */
static lv_obj_t *s_dlg_ok;              /**< its OK button */
static int s_dlg_idx = -1;              /**< the entry it's about (-1: none) */
static int s_dlg_mode;                  /**< which: DLG_RENAME, DLG_TRASH, DLG_PURGE or DLG_EMPTY */

/**
 * @brief Deletes for good (a bin item, or everything in the bin), with "Deleting..." meanwhile.
 * @param[in] name  the item's name in the bin; NULL: Empty
 */
static void job_start(const char *name)
{
    if (files_purge(name) != FILES_PURGE_BUSY) {
        file_list_count_text("Deleting...");
    }
}

/* ------------------------------------------------------------- dialogs */
/* (documented in ui_file_part.h) */
void file_dialog_close(void)
{
    if (!s_dlg) {
        return;
    }
    lv_obj_set_hidden(s_dlg, true);
    lv_textarea_set_text(s_ta, "");
    s_dlg_idx = -1;
}

/**
 * @brief Shows why not in the dialog, in orange (it stays open).
 * @param[in] msg  the reason
 */
static void dlg_error(const char *msg)
{
    lv_label_set_text(s_dlg_msg, msg);
    lv_obj_set_style_text_color(s_dlg_msg, C_ORANGE, 0);
}

/** @brief The rename dialog's OK: the new name checked, then the rename (an error stays in the dialog). */
static void do_rename(void)
{
    if (s_dlg_idx < 0 || s_dlg_idx >= files_count()) {
        return;
    }
    const char *old = files_entry(s_dlg_idx)->name;
    const char *name = lv_textarea_get_text(s_ta);
    if (strcmp(name, old) == 0) {                   /* unchanged */
        file_dialog_close();
        return;
    }
    const char *why = files_bad_name(name, old);
    if (why[0]) {
        dlg_error(why);
        return;
    }
    char msg[96];
    if (!files_rename(old, name, msg, sizeof(msg))) {
        dlg_error(msg);
        return;
    }
    file_dialog_close();
    file_list_rescan();
}

/** @brief The delete dialog's OK: the entry into the Recycle bin (an error stays in the dialog). */
static void do_trash(void)
{
    if (s_dlg_idx < 0 || s_dlg_idx >= files_count()) {
        return;
    }
    const files_entry_t *e = files_entry(s_dlg_idx);
    if (is_files_protected(e->name) || (is_files_at_root() && !strcmp(e->name, FILES_TRASH_NAME))) {   /* (no button) */
        file_dialog_close();
        return;
    }
    char msg[96];
    if (!files_trash(e->name, msg, sizeof(msg))) {
        dlg_error(msg);
        return;
    }
    file_dialog_close();
    file_list_rescan();
}

/** @brief The delete-for-good dialog's OK: one bin item, for good. */
static void do_purge(void)
{
    if (s_dlg_idx < 0 || s_dlg_idx >= files_count()) {
        return;
    }
    char name[FILES_NAME_MAX];
    strlcpy(name, files_entry(s_dlg_idx)->name, sizeof(name));
    file_dialog_close();
    job_start(name);
}

/**
 * @brief The dialog's Cancel.
 * @param[in] e  the event
 */
static void on_dlg_cancel(lv_event_t *e) { file_dialog_close(); }

/**
 * @brief The dialog's OK: what it asked (rename, into the bin, for good, empty the bin).
 * @param[in] e  the event
 */
static void on_dlg_ok(lv_event_t *e)
{
    switch (s_dlg_mode) {
    case DLG_RENAME:
        do_rename();
        break;
    case DLG_TRASH:
        do_trash();
        break;
    case DLG_PURGE:
        do_purge();
        break;
    case DLG_EMPTY:
        file_dialog_close();
        job_start(NULL);
        break;
    }
}

/**
 * @brief The keyboard's Enter (rename) and its close key (cancel).
 * @param[in] e  the event
 */
static void on_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        do_rename();
    } else if (code == LV_EVENT_CANCEL) {
        file_dialog_close();
    }
}

/* (documented in ui_file_part.h) */
void file_dialog_build(void)
{
    s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dlg);
    lv_obj_set_size(s_dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_70, 0);
    lv_obj_set_clickable(s_dlg, true);              /* block touches to the page */
    lv_obj_move_background(s_dlg);

    lv_obj_t *panel = lv_obj_create(s_dlg);
    lv_obj_set_size(panel, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);

    s_dlg_title = file_ui_label(panel, &lv_font_montserrat_24, C_TEXT, "");
    lv_obj_set_width(s_dlg_title, LV_PCT(100));
    lv_label_set_long_mode(s_dlg_title, LV_LABEL_LONG_MODE_WRAP);
    s_dlg_msg = file_ui_label(panel, &lv_font_montserrat_18, C_MUTED, "");
    lv_obj_set_width(s_dlg_msg, LV_PCT(100));
    lv_label_set_long_mode(s_dlg_msg, LV_LABEL_LONG_MODE_WRAP);

    s_ta = lv_textarea_create(panel);
    lv_obj_set_width(s_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, FILES_NAME_MAX - 1);
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_ta, C_TEXT, 0);
    lv_obj_set_style_bg_color(s_ta, C_BG, 0);
    lv_obj_set_style_border_color(s_ta, C_CARD2, 0);
    lv_obj_set_style_border_color(s_ta, C_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(s_ta, 12, 0);
    lv_obj_set_style_pad_all(s_ta, 14, 0);
    lv_obj_set_style_bg_color(s_ta, C_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(s_ta, C_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    lv_obj_t *btns = file_ui_row(panel);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btns, 14, 0);
    file_ui_pill_btn(btns, "Cancel", C_CARD2, C_TEXT, on_dlg_cancel);
    s_dlg_ok = file_ui_pill_btn(btns, "OK", C_ACCENT, lv_color_white(), on_dlg_ok);

    s_kb = lv_keyboard_create(s_dlg);
    lv_obj_set_size(s_kb, LV_PCT(100), 330);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_kb, s_ta);
    lv_obj_set_style_bg_color(s_kb, C_BG, 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb, 0, 0);
    lv_obj_set_style_pad_all(s_kb, 8, 0);
    lv_obj_set_style_pad_bottom(s_kb, 30, 0);       /* clear of the bezel */
    lv_obj_set_style_pad_gap(s_kb, 8, 0);
    lv_obj_set_style_bg_color(s_kb, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, C_CARD, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_kb, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_kb, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, C_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_22, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb, on_kb, LV_EVENT_ALL, NULL);

    lv_obj_set_hidden(s_dlg, true);
}

/* (documented in ui_file_part.h) */
void file_dialog_open(int idx, int mode)
{
    int count = files_count();
    if (mode != DLG_EMPTY && (idx < 0 || idx >= count)) {
        return;
    }
    const files_entry_t *e = mode != DLG_EMPTY ? files_entry(idx) : NULL;
    bool rename = mode == DLG_RENAME;
    s_dlg_idx = idx;
    s_dlg_mode = mode;
    lv_obj_set_style_text_color(s_dlg_msg, C_MUTED, 0);
    lv_obj_t *ok_lbl = lv_obj_get_child(s_dlg_ok, 0);
    if (mode == DLG_EMPTY) {
        lv_label_set_text(s_dlg_title, "Empty the Recycle bin?");
        lv_label_set_text_fmt(s_dlg_msg, "%d item%s, deleted for good. This can't be undone.", count, count == 1 ? "" : "s");
        lv_label_set_text(ok_lbl, "Empty");
        lv_obj_set_style_bg_color(s_dlg_ok, C_RED, 0);
    } else if (mode == DLG_PURGE) {
        char orig[FILES_PATH_MAX];
        files_trash_decode(e->name, orig, sizeof(orig));
        lv_label_set_text_fmt(s_dlg_title, "Delete %s for good?", orig);
        lv_label_set_text(s_dlg_msg, e->dir ? "The folder and everything in it. This can't be undone."
                          : "This can't be undone.");
        lv_label_set_text(ok_lbl, "Delete");
        lv_obj_set_style_bg_color(s_dlg_ok, C_RED, 0);
    } else if (rename) {
        lv_label_set_text_fmt(s_dlg_title, "Rename %s", e->dir ? "folder" : "file");
        lv_label_set_text(s_dlg_msg, "The new name, in the same folder.");
        lv_textarea_set_text(s_ta, e->name);
        lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
        lv_label_set_text(ok_lbl, "Rename");
        lv_obj_set_style_bg_color(s_dlg_ok, C_ACCENT, 0);
    } else {
        lv_label_set_text_fmt(s_dlg_title, "Delete %s?", e->name);
        lv_label_set_text(s_dlg_msg, e->dir ? "The folder and what's in it go to the Recycle bin: restore them "
                          "from there, or empty it to free the space."
                          : "It goes to the Recycle bin: restore it from there, or empty it to "
                          "free the space.");
        lv_label_set_text(ok_lbl, "Delete");
        lv_obj_set_style_bg_color(s_dlg_ok, C_RED, 0);
    }
    lv_obj_set_hidden(s_ta, !rename);
    lv_obj_set_hidden(s_kb, !rename);
    lv_obj_set_hidden(s_dlg, false);
}
/* (documented in ui_file_part.h) */
void file_dialog_destroy(void)
{
    if (s_dlg) {
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
}

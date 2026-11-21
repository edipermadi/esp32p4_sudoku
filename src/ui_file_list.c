/**
 * @file ui_file_list.c
 * @ingroup ui_file
 * @brief Files: the list (a row per entry with its buttons), the path, the count, and the top bar's
 *        Refresh, Up and Empty; a tap on a row opens a folder, a picture or video, or a puzzle file.
 */
#include "ui_file_part.h"
#include "files_core.h"
#include "ui_palette.h"
#include "ui_media_view.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(font_icons_24)       /* Montserrat 24 + FontAwesome icons, among them the lock */
#define SYMBOL_LOCK "\xEF\x80\xA3"   /**< FontAwesome U+F023 */

static lv_obj_t *s_path_lbl;            /**< the folder shown */
static lv_obj_t *s_count_lbl;           /**< "12 items", "Reading...", "Deleting..." */
static lv_obj_t *s_list;                /**< the entries, a card that scrolls */
static lv_obj_t *s_up;                  /**< Up (not in the root) */
static lv_obj_t *s_empty_btn;           /**< Empty (in the Recycle bin) */

/* (documented in ui_file_part.h) */
void file_list_count_text(const char *text)
{
    lv_label_set_text(s_count_lbl, text);
}

/* (documented in ui_file_part.h) */
void file_list_rescan(void)
{
    files_scan_t r = files_scan();
    if (r == FILES_SCAN_NOT_NOW) {
        return;
    }
    lv_label_set_text(s_count_lbl, r == FILES_SCAN_STARTED ? "Reading..." : "Out of memory");
}

/**
 * @brief Restore: a bin item back where it was (why not: in the count's place).
 * @param idx  the entry
 */
static void do_restore(int idx)
{
    if (idx < 0 || idx >= files_count()) {
        return;
    }
    switch (files_restore(files_entry(idx)->name)) {
    case FILES_RESTORED:
        file_list_rescan();
        break;
    case FILES_RESTORE_TAKEN:
        lv_label_set_text(s_count_lbl, "Name taken");
        break;
    case FILES_RESTORE_FAILED:
        lv_label_set_text(s_count_lbl, "Couldn't restore");
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- the list */
/**
 * @brief A row's Rename (not while the folder is read again: the worker is rewriting the entries).
 * @param e  the event (user data: the entry)
 */
static void on_rename(lv_event_t *e)
{
    if (!is_files_scanning() && !is_files_purging()) {
        file_dialog_open((int)(intptr_t)lv_event_get_user_data(e), DLG_RENAME);
    }
}

/**
 * @brief A row's Delete: into the Recycle bin, or in the bin for good (asks first).
 * @param e  the event (user data: the entry)
 */
static void on_delete(lv_event_t *e)
{
    if (!is_files_scanning() && !is_files_purging()) {
        file_dialog_open((int)(intptr_t)lv_event_get_user_data(e), is_files_in_trash() ? DLG_PURGE : DLG_TRASH);
    }
}

/**
 * @brief A bin item's Restore.
 * @param e  the event (user data: the entry)
 */
static void on_restore(lv_event_t *e)
{
    if (!is_files_scanning() && !is_files_purging()) {
        do_restore((int)(intptr_t)lv_event_get_user_data(e));
    }
}

/**
 * @brief Empty (in the bin): asks first.
 * @param e  the event
 */
static void on_empty(lv_event_t *e)
{
    if (!is_files_scanning() && !is_files_purging() && files_count() > 0) {
        file_dialog_open(-1, DLG_EMPTY);
    }
}

/**
 * @brief A picture or video: the viewer, with the folder's others for Prev / Next.
 * @param i  the entry
 */
static void open_media(int i)
{
    static const char *names[FILES_MAX_ENTRIES];
    int n = 0, at = 0;
    for (int k = 0; k < files_count(); k++) {
        const files_entry_t *e = files_entry(k);
        if (e->dir || !media_view_kind(e->name)) {
            continue;
        }
        if (k == i) {
            at = n;
        }
        names[n++] = e->name;
    }
    if (n) {
        media_view_open(file_ui_screen(), files_path(), names, n, at);
    }
}

/**
 * @brief A tap on a row: a folder opens, a picture or video plays, a puzzle file is previewed.
 * @param e  the event (user data: the entry)
 */
static void on_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= files_count() || is_files_scanning() || is_files_purging() || is_files_in_trash()) {
        return;
    }
    const files_entry_t *en = files_entry(i);
    if (!en->dir) {
        if (media_view_kind(en->name)) {
            open_media(i);
        } else if (is_files_puzzle_file(en->name)) {
            file_preview_open(i);    /* .sdm / .sdx: the preview */
        }
        return;
    }
    if (!files_enter(en->name)) {
        return;
    }
    file_list_rescan();
}

/**
 * @brief Up: the folder above.
 * @param e  the event
 */
static void on_up(lv_event_t *e)
{
    if (is_files_at_root() || is_files_scanning()) {
        return;
    }
    files_up();
    file_list_rescan();
}

/**
 * @brief Refresh: the folder read again.
 * @param e  the event
 */
static void on_refresh(lv_event_t *e) { file_list_rescan(); }

/* (documented in ui_file_part.h) */
void file_list_show(void)
{
    lv_obj_clean(s_list);
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    int count = files_count();
    lv_label_set_text(s_path_lbl, is_files_in_trash() ? "Recycle bin" : files_path());
    lv_obj_set_hidden(s_up, is_files_at_root());
    lv_obj_set_hidden(s_empty_btn, !is_files_in_trash() || count <= 0);

    if (count < 0) {
        lv_label_set_text(s_count_lbl, "");
        lv_obj_t *l = file_ui_label(s_list, &lv_font_montserrat_20, C_MUTED,
                                    "The card can't be read: no card, not FAT, or lent to a computer as a USB drive.");
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_pad_all(l, 12, 0);
        return;
    }
    if (is_files_more()) {
        lv_label_set_text_fmt(s_count_lbl, "First %d items", FILES_MAX_ENTRIES);
    } else {
        lv_label_set_text_fmt(s_count_lbl, "%d item%s", count, count == 1 ? "" : "s");
    }
    if (count == 0) {
        lv_obj_t *l = file_ui_label(s_list, &lv_font_montserrat_20, C_MUTED,
                                    is_files_in_trash() ? "The Recycle bin is empty." : "This folder is empty.");
        lv_obj_set_style_pad_all(l, 12, 0);
        return;
    }
    for (int i = 0; i < count; i++) {
        const files_entry_t *e = files_entry(i);
        bool prot = is_files_protected(e->name);
        bool bin = is_files_at_root() && e->dir && !strcmp(e->name, FILES_TRASH_NAME);   /* the Recycle bin itself */
        bool trashed = is_files_in_trash();            /* an item in it: Restore, Delete for good */

        lv_obj_t *r = lv_obj_create(s_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(r, 10, 0);
        lv_obj_set_style_pad_hor(r, 12, 0);
        lv_obj_set_style_pad_column(r, 14, 0);
        lv_obj_set_style_radius(r, 12, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(r, C_CARD2, 0);
        lv_obj_set_style_border_width(r, i < count - 1 ? 1 : 0, 0);
        int media = e->dir ? MEDIA_NONE : media_view_kind(e->name);
        bool puzzles = !e->dir && is_files_puzzle_file(e->name);
        if ((e->dir || media || puzzles) && !trashed) {
            /* a folder, picture/video or puzzle file opens on a tap
                                                                 (not in the bin) */
            lv_obj_set_clickable(r, true);
            lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_add_event_cb(r, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }

        lv_obj_t *ic = file_ui_label(r, &lv_font_montserrat_24, bin ? C_ORANGE : (e->dir ? C_ACCENT : C_MUTED),
                                     bin ? LV_SYMBOL_TRASH : e->dir ? LV_SYMBOL_DIRECTORY
                                     : media == MEDIA_IMAGE ? LV_SYMBOL_IMAGE : media == MEDIA_VIDEO ? LV_SYMBOL_VIDEO
                                     : puzzles ? LV_SYMBOL_LIST : LV_SYMBOL_FILE);
        lv_obj_set_width(ic, 30);

        lv_obj_t *col = lv_obj_create(r);
        lv_obj_remove_style_all(col);
        lv_obj_set_clickable(col, false);          /* a tap on the text is the row's */
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 2, 0);
        char shown[FILES_PATH_MAX];
        if (bin) {
            strlcpy(shown, "Recycle bin", sizeof(shown));
        } else if (trashed) {
            files_trash_decode(e->name, shown, sizeof(shown));    /* where it was */
        } else {
            strlcpy(shown, e->name, sizeof(shown));
        }
        lv_obj_t *nm = file_ui_label(col, &lv_font_montserrat_20, C_TEXT, shown);
        lv_obj_set_width(nm, LV_PCT(100));
        lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);

        char sub[96] = "", when[32] = "", size[16];
        if (e->mtime > 0) {
            struct tm tm;
            localtime_r(&e->mtime, &tm);
            if (tm.tm_year + 1900 >= 2000) {
                strftime(when, sizeof(when), "%d %b %Y, %H:%M", &tm);
            }
        }
        if (prot) {
            files_fmt_size(size, sizeof(size), e->size);
            snprintf(sub, sizeof(sub), "%s  -  the game's database: protected", size);
        } else if (bin) {
            snprintf(sub, sizeof(sub), "Deleted files and folders: restore them, or delete them for good");
        } else if (e->dir) {
            snprintf(sub, sizeof(sub), "Folder%s%s", when[0] ? "  -  " : "", when);
        } else {
            files_fmt_size(size, sizeof(size), e->size);
            snprintf(sub, sizeof(sub), "%s%s%s", size, when[0] ? "  -  " : "", when);
        }
        lv_obj_t *sl = file_ui_label(col, &lv_font_montserrat_16, prot ? C_ORANGE : C_MUTED, sub);
        lv_obj_set_width(sl, LV_PCT(100));
        lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_DOTS);

        if (prot) {
            lv_obj_t *lk = file_ui_label(r, &font_icons_24, C_ORANGE, SYMBOL_LOCK);
            lv_obj_set_style_pad_hor(lk, 16, 0);
        } else if (bin) {
            /* (opens on a tap; not renamed or deleted itself) */
        } else if (trashed) {
            file_ui_icon_btn(r, LV_SYMBOL_UPLOAD, C_TEXT, on_restore, (void *)(intptr_t)i);   /* Restore */
            file_ui_icon_btn(r, LV_SYMBOL_TRASH, C_RED, on_delete, (void *)(intptr_t)i);      /* for good */
        } else {
            file_ui_icon_btn(r, LV_SYMBOL_EDIT, C_TEXT, on_rename, (void *)(intptr_t)i);
            file_ui_icon_btn(r, LV_SYMBOL_TRASH, C_RED, on_delete, (void *)(intptr_t)i);
        }
    }
}

/* (documented in ui_file_part.h) */
void file_list_build(lv_obj_t *scr)
{
    lv_obj_t *refresh = file_ui_icon_btn(scr, LV_SYMBOL_REFRESH, C_TEXT, on_refresh, NULL);
    lv_obj_set_size(refresh, 60, 52);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -12, 10);
    s_empty_btn = file_ui_icon_btn(scr, LV_SYMBOL_TRASH "  Empty", C_RED, on_empty, NULL);   /* in the bin */
    lv_obj_set_size(s_empty_btn, 130, 52);
    lv_obj_align(s_empty_btn, LV_ALIGN_TOP_RIGHT, -206, 10);
    lv_obj_set_hidden(s_empty_btn, true);
    s_up = file_ui_icon_btn(scr, LV_SYMBOL_UP "  Up", C_TEXT, on_up, NULL);
    lv_obj_set_size(s_up, 110, 52);
    lv_obj_align(s_up, LV_ALIGN_TOP_RIGHT, -84, 10);
    lv_obj_set_hidden(s_up, true);

    /* where we are, and how many */
    s_path_lbl = file_ui_label(scr, &lv_font_montserrat_18, C_MUTED, files_path());
    lv_obj_set_width(s_path_lbl, 470);
    lv_label_set_long_mode(s_path_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s_path_lbl, 44, 80);
    s_count_lbl = file_ui_label(scr, &lv_font_montserrat_18, C_MUTED, "");
    lv_obj_set_width(s_count_lbl, 150);
    lv_obj_set_style_text_align(s_count_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_count_lbl, 720 - 44 - 150, 80);

    /* the entries: one card that scrolls */
    s_list = lv_obj_create(scr);
    lv_obj_set_pos(s_list, 40, 112);
    lv_obj_set_size(s_list, 720 - 80, 720 - 112 - 20);
    lv_obj_set_style_bg_color(s_list, C_CARD, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 16, 0);
    lv_obj_set_style_pad_all(s_list, 8, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
}

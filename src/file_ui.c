/*
 * Files page (see file_ui.h).
 *
 * A folder is read by a short worker task (core 0, away from LVGL): stat() of
 * each entry walks the FAT directory, which adds up in a folder of hundreds of
 * replays. The page polls for the result. Rename and delete are single calls
 * and run on the spot. Everything goes through the POSIX file API on the card's
 * FAT volume (BSP_SD_MOUNT_POINT), which stats_db keeps mounted.
 */
#include "file_ui.h"
#include "ui_palette.h"
#include "bsp/bsp_board.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#define MAX_ENTRIES  200             /* listed per folder (sorted: folders, then A-Z) */
#define NAME_MAX_B   256             /* FAT long names: up to 255 bytes of UTF-8 */
#define PATH_MAX_B   512
#define POLL_MS      150

LV_FONT_DECLARE(font_icons_24)       /* Montserrat 24 + FontAwesome icons, among them the lock */
#define SYMBOL_LOCK "\xEF\x80\xA3"   /* FontAwesome U+F023 */

typedef struct {
    char name[NAME_MAX_B];
    bool dir;
    uint32_t size;
    time_t mtime;
} entry_t;

/* The game's database and SQLite's files next to it, in the card's root.
 * Deleting one loses data; a stray file renamed to a journal's name would be
 * "rolled back" into the database at the next open and corrupt it. */
static const char *const PROTECTED[] = {"sudoku.db", "sudoku.db-journal", "sudoku.db-wal", "sudoku.db-shm"};

static lv_obj_t *s_scr, *s_path_lbl, *s_count_lbl, *s_list, *s_up;
static lv_timer_t *s_timer;
static bool s_shown;
static char s_path[PATH_MAX_B] = BSP_SD_MOUNT_POINT;

/* scan worker */
static entry_t *s_ent;               /* MAX_ENTRIES, PSRAM */
static volatile int s_n;             /* entries found; -1: the folder couldn't be opened */
static volatile bool s_more;         /* more than MAX_ENTRIES */
static volatile bool s_busy, s_done;
static char s_scan_path[PATH_MAX_B];

/* dialogs (top layer): rename with a keyboard, delete confirmation */
static lv_obj_t *s_dlg, *s_dlg_title, *s_dlg_msg, *s_ta, *s_kb, *s_dlg_ok;
static int s_dlg_idx = -1;
static bool s_dlg_rename;

/* ------------------------------------------------------------- helpers */
static bool at_root(void)
{
    return strcmp(s_path, BSP_SD_MOUNT_POINT) == 0;
}

static bool is_protected_name(const char *name)
{
    if (!at_root()) return false;
    for (size_t i = 0; i < sizeof(PROTECTED) / sizeof(PROTECTED[0]); i++) {
        if (strcasecmp(name, PROTECTED[i]) == 0) return true;   /* FAT ignores case */
    }
    return false;
}

static void fmt_size(char *buf, size_t n, uint32_t b)
{
    if (b < 1024) {
        snprintf(buf, n, "%u B", (unsigned)b);
    } else if (b < 1024 * 1024) {
        snprintf(buf, n, "%u.%u KB", (unsigned)(b / 1024), (unsigned)(b % 1024 * 10 / 1024));
    } else {
        uint32_t k = b / 1024;
        snprintf(buf, n, "%u.%u MB", (unsigned)(k / 1024), (unsigned)(k % 1024 * 10 / 1024));
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *plain_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *icon_btn(lv_obj_t *parent, const char *sym, lv_color_t color, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 56, 48);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, C_CARD2, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_center(label(b, &lv_font_montserrat_20, color, sym));
    return b;
}

static lv_obj_t *pill_btn(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 56);
    lv_obj_set_style_pad_hor(b, 26, 0);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(b, &lv_font_montserrat_20, fg, txt));
    return b;
}

/* ------------------------------------------------------------- scanning */
static int entry_cmp(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;  /* folders first */
    return strcasecmp(x->name, y->name);
}

static void scan_task(void *arg)
{
    int n = 0;
    bool more = false;
    DIR *d = opendir(s_scan_path);
    if (!d) {
        n = -1;
    } else {
        struct dirent *de;
        char full[PATH_MAX_B + NAME_MAX_B];
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            if (n >= MAX_ENTRIES) {
                more = true;
                break;
            }
            entry_t *e = &s_ent[n];
            strlcpy(e->name, de->d_name, sizeof(e->name));
            e->dir = de->d_type == DT_DIR;
            e->size = 0;
            e->mtime = 0;
            snprintf(full, sizeof(full), "%s/%s", s_scan_path, de->d_name);
            struct stat st;
            if (stat(full, &st) == 0) {
                e->dir = S_ISDIR(st.st_mode);
                e->size = (uint32_t)st.st_size;
                e->mtime = st.st_mtime;
            }
            n++;
        }
        closedir(d);
        qsort(s_ent, (size_t)n, sizeof(entry_t), entry_cmp);
    }
    s_n = n;
    s_more = more;
    s_done = true;
    s_busy = false;
    vTaskDelete(NULL);
}

static void rescan(void)
{
    if (s_busy || !s_ent) return;
    strlcpy(s_scan_path, s_path, sizeof(s_scan_path));
    s_busy = true;
    s_done = false;
    lv_label_set_text(s_count_lbl, "Reading...");
    if (xTaskCreatePinnedToCore(scan_task, "files", 6144, NULL, 2, NULL, 0) != pdPASS) {
        s_busy = false;
        lv_label_set_text(s_count_lbl, "Out of memory");
    }
}

/* ------------------------------------------------------------- dialogs */
static void dlg_close(void)
{
    if (!s_dlg) return;
    lv_obj_set_hidden(s_dlg, true);
    lv_textarea_set_text(s_ta, "");
    s_dlg_idx = -1;
}

static void dlg_error(const char *msg)
{
    lv_label_set_text(s_dlg_msg, msg);
    lv_obj_set_style_text_color(s_dlg_msg, C_ORANGE, 0);
}

/* Why a new name can't be used ("" if it can). */
static const char *bad_name(const char *name, const char *old)
{
    size_t len = strlen(name);
    if (len == 0) return "Type a name.";
    if (len >= NAME_MAX_B) return "That name is too long.";
    if (!strcmp(name, ".") || !strcmp(name, "..")) return "That name can't be used.";
    if (strpbrk(name, "\\/:*?\"<>|")) return "A name can't have \\ / : * ? \" < > |";
    if (name[0] == ' ' || name[len - 1] == ' ' || name[len - 1] == '.')
        return "A name can't start with a space or end with a space or a dot.";
    if (is_protected_name(name)) return "That name belongs to the game's database.";
    if (strcasecmp(name, old) != 0) {             /* (only the case changes: the same file) */
        char full[PATH_MAX_B + NAME_MAX_B];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", s_path, name);
        if (stat(full, &st) == 0) return "Something here already has that name.";
    }
    return "";
}

static void do_rename(void)
{
    if (s_dlg_idx < 0 || s_dlg_idx >= s_n) return;
    const char *old = s_ent[s_dlg_idx].name;
    const char *name = lv_textarea_get_text(s_ta);
    if (strcmp(name, old) == 0) {                   /* unchanged */
        dlg_close();
        return;
    }
    const char *why = bad_name(name, old);
    if (why[0]) {
        dlg_error(why);
        return;
    }
    char from[PATH_MAX_B + NAME_MAX_B], to[PATH_MAX_B + NAME_MAX_B];
    snprintf(from, sizeof(from), "%s/%s", s_path, old);
    snprintf(to, sizeof(to), "%s/%s", s_path, name);
    if (rename(from, to) != 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Couldn't rename it (%s).", strerror(errno));
        dlg_error(msg);
        return;
    }
    printf("files: renamed %s -> %s\n", from, to);
    dlg_close();
    rescan();
}

static void do_delete(void)
{
    if (s_dlg_idx < 0 || s_dlg_idx >= s_n) return;
    const entry_t *e = &s_ent[s_dlg_idx];
    if (is_protected_name(e->name)) {               /* (no Delete button on it; belt and braces) */
        dlg_close();
        return;
    }
    char full[PATH_MAX_B + NAME_MAX_B];
    snprintf(full, sizeof(full), "%s/%s", s_path, e->name);
    int r = e->dir ? rmdir(full) : unlink(full);
    if (r != 0) {
        int err = errno;
        dlg_error(e->dir && (err == ENOTEMPTY || err == EEXIST || err == EACCES)
                      ? "Only an empty folder can be deleted: delete what's in it first."
                      : strerror(err));
        return;
    }
    printf("files: deleted %s\n", full);
    dlg_close();
    rescan();
}

static void on_dlg_cancel(lv_event_t *e) { dlg_close(); }
static void on_dlg_ok(lv_event_t *e)
{
    if (s_dlg_rename) do_rename();
    else do_delete();
}

static void on_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) do_rename();
    else if (code == LV_EVENT_CANCEL) dlg_close();
}

static void build_dialog(void)
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

    s_dlg_title = label(panel, &lv_font_montserrat_24, C_TEXT, "");
    lv_obj_set_width(s_dlg_title, LV_PCT(100));
    lv_label_set_long_mode(s_dlg_title, LV_LABEL_LONG_MODE_WRAP);
    s_dlg_msg = label(panel, &lv_font_montserrat_18, C_MUTED, "");
    lv_obj_set_width(s_dlg_msg, LV_PCT(100));
    lv_label_set_long_mode(s_dlg_msg, LV_LABEL_LONG_MODE_WRAP);

    s_ta = lv_textarea_create(panel);
    lv_obj_set_width(s_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, NAME_MAX_B - 1);
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_ta, C_TEXT, 0);
    lv_obj_set_style_bg_color(s_ta, C_BG, 0);
    lv_obj_set_style_border_color(s_ta, C_CARD2, 0);
    lv_obj_set_style_border_color(s_ta, C_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(s_ta, 12, 0);
    lv_obj_set_style_pad_all(s_ta, 14, 0);
    lv_obj_set_style_bg_color(s_ta, C_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(s_ta, C_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    lv_obj_t *btns = plain_row(panel);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btns, 14, 0);
    pill_btn(btns, "Cancel", C_CARD2, C_TEXT, on_dlg_cancel);
    s_dlg_ok = pill_btn(btns, "OK", C_ACCENT, lv_color_white(), on_dlg_ok);

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

static void dlg_open(int idx, bool rename)
{
    if (idx < 0 || idx >= s_n) return;
    const entry_t *e = &s_ent[idx];
    s_dlg_idx = idx;
    s_dlg_rename = rename;
    lv_obj_set_style_text_color(s_dlg_msg, C_MUTED, 0);
    lv_obj_t *ok_lbl = lv_obj_get_child(s_dlg_ok, 0);
    if (rename) {
        lv_label_set_text_fmt(s_dlg_title, "Rename %s", e->dir ? "folder" : "file");
        lv_label_set_text(s_dlg_msg, "The new name, in the same folder.");
        lv_textarea_set_text(s_ta, e->name);
        lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
        lv_label_set_text(ok_lbl, "Rename");
        lv_obj_set_style_bg_color(s_dlg_ok, C_ACCENT, 0);
    } else {
        lv_label_set_text_fmt(s_dlg_title, "Delete %s?", e->name);
        lv_label_set_text(s_dlg_msg, e->dir ? "The folder must be empty. This can't be undone."
                                            : "This can't be undone.");
        lv_label_set_text(ok_lbl, "Delete");
        lv_obj_set_style_bg_color(s_dlg_ok, C_RED, 0);
    }
    lv_obj_set_hidden(s_ta, !rename);
    lv_obj_set_hidden(s_kb, !rename);
    lv_obj_set_hidden(s_dlg, false);
}

/* ------------------------------------------------------------- the list */
/* (not while the folder is read again: the worker is rewriting s_ent) */
static void on_rename(lv_event_t *e)
{
    if (!s_busy) dlg_open((int)(intptr_t)lv_event_get_user_data(e), true);
}

static void on_delete(lv_event_t *e)
{
    if (!s_busy) dlg_open((int)(intptr_t)lv_event_get_user_data(e), false);
}

static void on_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_n || !s_ent[i].dir || s_busy) return;
    size_t len = strlen(s_path);
    if (len + 1 + strlen(s_ent[i].name) >= sizeof(s_path)) return;
    snprintf(s_path + len, sizeof(s_path) - len, "/%s", s_ent[i].name);
    rescan();
}

static void on_up(lv_event_t *e)
{
    if (at_root() || s_busy) return;
    char *slash = strrchr(s_path, '/');
    if (slash && slash != s_path) *slash = 0;
    rescan();
}

static void on_refresh(lv_event_t *e) { rescan(); }

static void show_list(void)
{
    lv_obj_clean(s_list);
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    lv_label_set_text(s_path_lbl, s_path);
    lv_obj_set_hidden(s_up, at_root());

    if (s_n < 0) {
        lv_label_set_text(s_count_lbl, "");
        lv_obj_t *l = label(s_list, &lv_font_montserrat_20, C_MUTED,
                            "The card can't be read: no card, not FAT, or lent to a computer as a USB drive.");
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_pad_all(l, 12, 0);
        return;
    }
    if (s_more) {
        lv_label_set_text_fmt(s_count_lbl, "First %d items", MAX_ENTRIES);
    } else {
        lv_label_set_text_fmt(s_count_lbl, "%d item%s", s_n, s_n == 1 ? "" : "s");
    }
    if (s_n == 0) {
        lv_obj_t *l = label(s_list, &lv_font_montserrat_20, C_MUTED, "This folder is empty.");
        lv_obj_set_style_pad_all(l, 12, 0);
        return;
    }
    for (int i = 0; i < s_n; i++) {
        const entry_t *e = &s_ent[i];
        bool prot = is_protected_name(e->name);

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
        lv_obj_set_style_border_width(r, i < s_n - 1 ? 1 : 0, 0);
        if (e->dir) {                               /* a folder opens on a tap */
            lv_obj_set_clickable(r, true);
            lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_add_event_cb(r, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }

        lv_obj_t *ic = label(r, &lv_font_montserrat_24, e->dir ? C_ACCENT : C_MUTED,
                             e->dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_obj_set_width(ic, 30);

        lv_obj_t *col = lv_obj_create(r);
        lv_obj_remove_style_all(col);
        lv_obj_set_clickable(col, false);          /* a tap on the text is the row's */
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 2, 0);
        lv_obj_t *nm = label(col, &lv_font_montserrat_20, C_TEXT, e->name);
        lv_obj_set_width(nm, LV_PCT(100));
        lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);

        char sub[96] = "", when[32] = "", size[16];
        if (e->mtime > 0) {
            struct tm tm;
            localtime_r(&e->mtime, &tm);
            if (tm.tm_year + 1900 >= 2000) strftime(when, sizeof(when), "%d %b %Y, %H:%M", &tm);
        }
        if (prot) {
            fmt_size(size, sizeof(size), e->size);
            snprintf(sub, sizeof(sub), "%s  -  the game's database: protected", size);
        } else if (e->dir) {
            snprintf(sub, sizeof(sub), "Folder%s%s", when[0] ? "  -  " : "", when);
        } else {
            fmt_size(size, sizeof(size), e->size);
            snprintf(sub, sizeof(sub), "%s%s%s", size, when[0] ? "  -  " : "", when);
        }
        lv_obj_t *sl = label(col, &lv_font_montserrat_16, prot ? C_ORANGE : C_MUTED, sub);
        lv_obj_set_width(sl, LV_PCT(100));
        lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_DOTS);

        if (prot) {
            lv_obj_t *lk = label(r, &font_icons_24, C_ORANGE, SYMBOL_LOCK);
            lv_obj_set_style_pad_hor(lk, 16, 0);
        } else {
            icon_btn(r, LV_SYMBOL_EDIT, C_TEXT, on_rename, (void *)(intptr_t)i);
            icon_btn(r, LV_SYMBOL_TRASH, C_RED, on_delete, (void *)(intptr_t)i);
        }
    }
}

static void poll(lv_timer_t *t)
{
    if (s_done) {
        s_done = false;
        show_list();
    }
}

/* ------------------------------------------------------------- build */
void file_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb)
{
    if (s_timer) {                                  /* rebuilt on a theme change */
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_dlg) {
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
    if (!s_ent) s_ent = heap_caps_calloc(MAX_ENTRIES, sizeof(entry_t), MALLOC_CAP_SPIRAM);
    s_scr = scr;

    /* top bar, as on the other Settings pages */
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_pos(back, 12, 10);
    lv_obj_set_size(back, 150, 52);
    lv_obj_set_style_bg_color(back, C_CARD, 0);
    lv_obj_set_style_bg_color(back, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(back, &lv_font_montserrat_18, C_TEXT, LV_SYMBOL_LEFT "  Home"));
    lv_obj_set_pos(label(scr, &lv_font_montserrat_28, C_TEXT, "Files"), 180, 20);

    lv_obj_t *refresh = icon_btn(scr, LV_SYMBOL_REFRESH, C_TEXT, on_refresh, NULL);
    lv_obj_set_size(refresh, 60, 52);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -12, 10);
    s_up = icon_btn(scr, LV_SYMBOL_UP "  Up", C_TEXT, on_up, NULL);
    lv_obj_set_size(s_up, 110, 52);
    lv_obj_align(s_up, LV_ALIGN_TOP_RIGHT, -84, 10);
    lv_obj_set_hidden(s_up, true);

    /* where we are, and how many */
    s_path_lbl = label(scr, &lv_font_montserrat_18, C_MUTED, s_path);
    lv_obj_set_width(s_path_lbl, 470);
    lv_label_set_long_mode(s_path_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s_path_lbl, 44, 80);
    s_count_lbl = label(scr, &lv_font_montserrat_18, C_MUTED, "");
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

    build_dialog();
    s_timer = lv_timer_create(poll, POLL_MS, NULL);
    if (s_shown) rescan();                          /* (a rebuild while open: list again) */
    else lv_timer_pause(s_timer);
}

void file_ui_set_shown(bool shown)
{
    if (!s_timer) return;
    s_shown = shown;
    if (shown) {
        lv_timer_resume(s_timer);
        rescan();                                   /* the folder last open, read afresh */
    } else {
        lv_timer_pause(s_timer);
        dlg_close();
    }
}

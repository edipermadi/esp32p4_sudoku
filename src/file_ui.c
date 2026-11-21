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
#include "media_view.h"
#include "ui_palette.h"
#include "stats_db.h"
#include "generator.h"
#include "sdx.h"
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
LV_FONT_DECLARE(font_digits_44)      /* the game's board digits */
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
enum { DLG_RENAME, DLG_TRASH, DLG_PURGE, DLG_EMPTY };
static int s_dlg_mode;

/* Recycle bin: deleting moves things into TRASH_DIR, named after where they were
 * ("replays/a.mp4" -> "replays%2Fa.mp4", "%" -> "%25"; "2~" in front if that name is
 * taken), so Restore can put them back. Deleting there, or Empty, removes them for good
 * (by the job task: a folder can hold many files). */
#define TRASH_NAME ".trash"
#define TRASH_DIR  BSP_SD_MOUNT_POINT "/" TRASH_NAME
static lv_obj_t *s_empty_btn;
static volatile bool s_job_busy, s_job_done;
static char s_job_path[PATH_MAX_B + NAME_MAX_B];   /* "" = every item in the bin */
static char s_job_msg[96];

/* ------------------------------------------------------------- helpers */
static bool at_root(void)
{
    return strcmp(s_path, BSP_SD_MOUNT_POINT) == 0;
}

static bool in_trash(void)
{
    return strcmp(s_path, TRASH_DIR) == 0;
}

/* "replays/a.mp4" -> "replays%2Fa.mp4"; false if it doesn't fit a FAT name */
static bool trash_encode(const char *rel, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = rel; *p; p++) {
        const char *put = *p == '/' ? "%2F" : (*p == '%' ? "%25" : NULL);
        size_t need = put ? 3 : 1;
        if (k + need >= n || k + need > 240) return false;   /* (room for a "99~" in front) */
        if (put) memcpy(out + k, put, 3);
        else out[k] = *p;
        k += need;
    }
    out[k] = 0;
    return k > 0;
}

/* the bin's name of an item -> where it was, relative to the card's root */
static void trash_decode(const char *name, char *out, size_t n)
{
    const char *p = name;
    while (*p >= '0' && *p <= '9') p++;
    if (p != name && *p == '~') name = p + 1;      /* a "2~" added for a taken name */
    size_t k = 0;
    for (p = name; *p && k + 1 < n; p++) {
        if (p[0] == '%' && p[1] == '2' && (p[2] == 'F' || p[2] == 'f')) { out[k++] = '/'; p += 2; }
        else if (p[0] == '%' && p[1] == '2' && p[2] == '5') { out[k++] = '%'; p += 2; }
        else out[k++] = *p;
    }
    out[k] = 0;
}

/* deletes path, and everything in it if it is a folder (job task) */
static int rm_rf(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char *child = malloc(strlen(path) + strlen(de->d_name) + 2);
            if (!child) break;
            sprintf(child, "%s/%s", path, de->d_name);
            rm_rf(child);
            free(child);
        }
        closedir(d);
    }
    return rmdir(path);
}

static void job_task(void *arg)
{
    int n = 0, failed = 0;
    if (s_job_path[0]) {                           /* one item */
        if (rm_rf(s_job_path) == 0) n++; else failed++;
    } else {                                        /* Empty: everything in the bin */
        DIR *d = opendir(TRASH_DIR);
        if (d) {
            struct dirent *de;
            static char p[PATH_MAX_B + NAME_MAX_B];
            while ((de = readdir(d)) != NULL) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                snprintf(p, sizeof(p), "%s/%s", TRASH_DIR, de->d_name);
                if (rm_rf(p) == 0) n++; else failed++;
            }
            closedir(d);
        }
    }
    if (failed) snprintf(s_job_msg, sizeof(s_job_msg), "Deleted %d; %d couldn't be.", n, failed);
    else s_job_msg[0] = 0;
    printf("files: deleted for good: %d (%d failed)\n", n, failed);
    s_job_done = true;
    s_job_busy = false;
    vTaskDelete(NULL);
}

static void job_start(const char *path)
{
    if (s_job_busy) return;
    strlcpy(s_job_path, path ? path : "", sizeof(s_job_path));
    s_job_busy = true;
    lv_label_set_text(s_count_lbl, "Deleting...");
    if (xTaskCreatePinnedToCore(job_task, "files_rm", 8192, NULL, 2, NULL, 0) != pdPASS) s_job_busy = false;
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

/* moves the entry into the Recycle bin */
static void do_trash(void)
{
    if (s_dlg_idx < 0 || s_dlg_idx >= s_n) return;
    const entry_t *e = &s_ent[s_dlg_idx];
    if (is_protected_name(e->name) || (at_root() && !strcmp(e->name, TRASH_NAME))) {   /* (no button) */
        dlg_close();
        return;
    }
    char rel[PATH_MAX_B + NAME_MAX_B], enc[NAME_MAX_B], from[PATH_MAX_B + NAME_MAX_B], to[PATH_MAX_B + NAME_MAX_B];
    snprintf(rel, sizeof(rel), "%s/%s", s_path + strlen(BSP_SD_MOUNT_POINT) + (at_root() ? 0 : 1), e->name);
    if (rel[0] == '/') memmove(rel, rel + 1, strlen(rel));
    if (!trash_encode(rel, enc, sizeof(enc))) {
        dlg_error("Its path is too long for the Recycle bin.");
        return;
    }
    mkdir(TRASH_DIR, 0775);                         /* (already there: fine) */
    snprintf(from, sizeof(from), "%s/%s", s_path, e->name);
    struct stat st;
    snprintf(to, sizeof(to), "%s/%s", TRASH_DIR, enc);
    for (int k = 2; stat(to, &st) == 0 && k < 100; k++) {   /* that name is taken in the bin */
        snprintf(to, sizeof(to), "%s/%d~%s", TRASH_DIR, k, enc);
    }
    if (rename(from, to) != 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Couldn't move it to the Recycle bin (%s).", strerror(errno));
        dlg_error(msg);
        return;
    }
    printf("files: %s -> %s\n", from, to);
    dlg_close();
    rescan();
}

/* puts a bin item back where it was (making the folders on the way if they're gone) */
static void do_restore(int idx)
{
    if (idx < 0 || idx >= s_n) return;
    char rel[PATH_MAX_B], from[PATH_MAX_B + NAME_MAX_B], to[PATH_MAX_B + NAME_MAX_B];
    trash_decode(s_ent[idx].name, rel, sizeof(rel));
    snprintf(from, sizeof(from), "%s/%s", TRASH_DIR, s_ent[idx].name);
    snprintf(to, sizeof(to), "%s/%s", BSP_SD_MOUNT_POINT, rel);
    const char *base = strrchr(rel, '/') ? strrchr(rel, '/') + 1 : rel;
    if (base == rel && is_protected_name(rel)) return;   /* (a root name of the database: never) */
    struct stat st;
    if (stat(to, &st) == 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Can't restore: something is already at %s.", rel);
        printf("files: %s\n", msg);
        lv_label_set_text(s_count_lbl, "Name taken");
        return;
    }
    for (char *p = to + strlen(BSP_SD_MOUNT_POINT) + 1; (p = strchr(p, '/')) != NULL; p++) {
        *p = 0;                                     /* its folders, if they were deleted since */
        mkdir(to, 0775);
        *p = '/';
    }
    if (rename(from, to) != 0) {
        printf("files: restore %s failed (%s)\n", rel, strerror(errno));
        lv_label_set_text(s_count_lbl, "Couldn't restore");
        return;
    }
    printf("files: restored %s\n", to);
    rescan();
}

static void do_purge(void)                          /* one bin item, for good */
{
    if (s_dlg_idx < 0 || s_dlg_idx >= s_n) return;
    char p[PATH_MAX_B + NAME_MAX_B];
    snprintf(p, sizeof(p), "%s/%s", TRASH_DIR, s_ent[s_dlg_idx].name);
    dlg_close();
    job_start(p);
}

static void on_dlg_cancel(lv_event_t *e) { dlg_close(); }
static void on_dlg_ok(lv_event_t *e)
{
    switch (s_dlg_mode) {
    case DLG_RENAME: do_rename(); break;
    case DLG_TRASH:  do_trash(); break;
    case DLG_PURGE:  do_purge(); break;
    case DLG_EMPTY:  dlg_close(); job_start(NULL); break;
    }
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

static void dlg_open(int idx, int mode)
{
    if (mode != DLG_EMPTY && (idx < 0 || idx >= s_n)) return;
    const entry_t *e = mode != DLG_EMPTY ? &s_ent[idx] : NULL;
    bool rename = mode == DLG_RENAME;
    s_dlg_idx = idx;
    s_dlg_mode = mode;
    lv_obj_set_style_text_color(s_dlg_msg, C_MUTED, 0);
    lv_obj_t *ok_lbl = lv_obj_get_child(s_dlg_ok, 0);
    if (mode == DLG_EMPTY) {
        lv_label_set_text(s_dlg_title, "Empty the Recycle bin?");
        lv_label_set_text_fmt(s_dlg_msg, "%d item%s, deleted for good. This can't be undone.", s_n, s_n == 1 ? "" : "s");
        lv_label_set_text(ok_lbl, "Empty");
        lv_obj_set_style_bg_color(s_dlg_ok, C_RED, 0);
    } else if (mode == DLG_PURGE) {
        char orig[PATH_MAX_B];
        trash_decode(e->name, orig, sizeof(orig));
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

/* ------------------------------------------------------------- the list */
/* (not while the folder is read again: the worker is rewriting s_ent) */
static void on_rename(lv_event_t *e)
{
    if (!s_busy && !s_job_busy) dlg_open((int)(intptr_t)lv_event_get_user_data(e), DLG_RENAME);
}

static void on_delete(lv_event_t *e)
{
    if (!s_busy && !s_job_busy) dlg_open((int)(intptr_t)lv_event_get_user_data(e), in_trash() ? DLG_PURGE : DLG_TRASH);
}

static void on_restore(lv_event_t *e)
{
    if (!s_busy && !s_job_busy) do_restore((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_empty(lv_event_t *e)
{
    if (!s_busy && !s_job_busy && s_n > 0) dlg_open(-1, DLG_EMPTY);
}

/* ------------------------------------------------------------- puzzle file preview */
/* A tap on an .sdm, .sdx or .opensudoku file: its puzzles one at a time on a board (< > to step
 * through them), what's in it, and Close / Import (stats_db_import_file: the same
 * import as Settings > Data > Import puzzles, for this file, wherever it is). The file
 * is read here, on the spot: they are small (a line per puzzle). */
#define PV_MAX_BYTES (512 * 1024)                /* read at most this much of a file */
#define PV_MAX       2000                        /* puzzles listed (an import takes as many) */
#define PV_BOARD     432                         /* px: 48 per cell (9x9), 72 (6x6) */
static lv_obj_t *s_pv, *s_pv_board, *s_pv_pos, *s_pv_prev, *s_pv_next;
static char *s_pv_buf;                           /* the file, split into lines (PSRAM) */
static char **s_pv_line;                         /* its puzzles' lines */
static int s_pv_n, s_pv_at;
static char s_pv_path[PATH_MAX_B + NAME_MAX_B];
static sudoku_geom_t *s_pv_geom;                 /* the puzzle shown: its board ... */
static uint8_t s_pv_givens[81];                  /* ... and digits */
static int s_pv_var, s_pv_lv;                    /* its variant, level (-1: none given) */
static bool s_pv_shown;                          /* a puzzle could be drawn */

static bool ends_with_ci(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

static bool is_puzzle_file(const char *name)
{
    return ends_with_ci(name, ".sdm") || ends_with_ci(name, ".sdx") || ends_with_ci(name, ".opensudoku");
}

/* one line (sdx.h: an .sdx line, or just 81 digits as in .sdm) into variant, level, givens and
 * its board; false if it isn't a puzzle. The same reading as the import's, without the
 * one-solution check. */
static bool pv_parse(const char *src, int *var, int *lv, uint8_t g[81], sudoku_geom_t *geom)
{
    static char line[2048];
    snprintf(line, sizeof(line), "%s", src);
    sdx_puzzle_t p;
    if (!sdx_parse(line, &p) || !sdx_geom(&p, geom)) return false;
    *var = p.variant;
    *lv = p.level;
    memcpy(g, p.givens, 81);
    return true;
}

static void pv_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int w, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = w;
    d.p1.x = x1; d.p1.y = y1;
    d.p2.x = x2; d.p2.y = y2;
    lv_draw_line(layer, &d);
}

/* the board: cells (the X-Sudoku diagonals and Hyper windows tinted), the givens, thin lines
 * between cells and thick ones between boxes (Jigsaw: regions); Killer: the cages, dashed in
 * the accent colour, with their sums */
static void pv_draw_cb(lv_event_t *e)
{
    if (!s_pv_shown) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const sudoku_geom_t *g = s_pv_geom;
    int n = g->n, cs = PV_BOARD / n;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = n == 9 ? &lv_font_montserrat_28 : &font_digits_44;   /* (6x6: the game's own digits) */
    ld.color = C_TEXT;
    ld.align = LV_TEXT_ALIGN_CENTER;
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int cell = r * 9 + c;
            lv_area_t ca = {a.x1 + c * cs, a.y1 + r * cs, a.x1 + (c + 1) * cs - 1, a.y1 + (r + 1) * cs - 1};
            rd.bg_color = sudoku_geom_tinted(g, cell) ? C_CELL_PEER : (s_pv_givens[cell] ? C_CELL_GIVEN : C_CELL);
            lv_draw_rect(layer, &rd, &ca);
            if (s_pv_givens[cell]) {
                char t[2] = {(char)('0' + s_pv_givens[cell]), 0};
                lv_area_t ta = ca;
                ta.y1 += (cs - lv_font_get_line_height(ld.font)) / 2;
                ld.text = t;
                ld.text_local = 1;
                lv_draw_label(layer, &ld, &ta);
            }
        }
    }
    /* Killer cages: a dashed line just inside each cage edge, the sum in its first cell */
    if (s_pv_var == VAR_KILLER) {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = C_ACCENT;
        d.width = 1;
        d.dash_width = 4;
        d.dash_gap = 3;
        const int in = 4;
        for (int r = 0; r < n; r++) {
            for (int c = 0; c < n; c++) {
                int cell = r * 9 + c, k = g->cage_of[cell];
                if (k == CAGE_NONE) continue;
                int x1 = a.x1 + c * cs, y1 = a.y1 + r * cs, x2 = x1 + cs - 1, y2 = y1 + cs - 1;
                bool top = r == 0 || g->cage_of[cell - 9] != k, bot = r == n - 1 || g->cage_of[cell + 9] != k;
                bool lef = c == 0 || g->cage_of[cell - 1] != k, rig = c == n - 1 || g->cage_of[cell + 1] != k;
                int xa = lef ? x1 + in : x1, xb = rig ? x2 - in : x2, ya = top ? y1 + in : y1, yb = bot ? y2 - in : y2;
                if (top) { d.p1.x = xa; d.p1.y = ya; d.p2.x = xb; d.p2.y = ya; lv_draw_line(layer, &d); }
                if (bot) { d.p1.x = xa; d.p1.y = yb; d.p2.x = xb; d.p2.y = yb; lv_draw_line(layer, &d); }
                if (lef) { d.p1.x = xa; d.p1.y = ya; d.p2.x = xa; d.p2.y = yb; lv_draw_line(layer, &d); }
                if (rig) { d.p1.x = xb; d.p1.y = ya; d.p2.x = xb; d.p2.y = yb; lv_draw_line(layer, &d); }
                if (g->cage_cell[k][0] == cell) {   /* the top-left cell: the sum */
                    char t[4];
                    snprintf(t, sizeof(t), "%d", g->cage_sum[k]);
                    lv_draw_label_dsc_t sd;
                    lv_draw_label_dsc_init(&sd);
                    sd.font = &lv_font_montserrat_12;
                    sd.color = C_ACCENT;
                    sd.text = t;
                    sd.text_local = 1;
                    lv_area_t sa = {x1 + in + 2, y1 + in + 1, x2, y2};
                    lv_draw_label(layer, &sd, &sa);
                }
            }
        }
    }
    /* lines: thin between cells, thick between boxes / regions and round the edge */
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int cell = r * 9 + c, x = a.x1 + c * cs, y = a.y1 + r * cs;
            if (c < n - 1) {
                bool thick = g->region[cell] != g->region[cell + 1];
                pv_line(layer, x + cs, y, x + cs, y + cs, thick ? 3 : 1, thick ? C_GRID_THICK : C_GRID_THIN);
            }
            if (r < n - 1) {
                bool thick = g->region[cell] != g->region[cell + 9];
                pv_line(layer, x, y + cs, x + cs, y + cs, thick ? 3 : 1, thick ? C_GRID_THICK : C_GRID_THIN);
            }
        }
    }
    int e2 = a.x1 + n * cs, f2 = a.y1 + n * cs;
    pv_line(layer, a.x1, a.y1 + 1, e2, a.y1 + 1, 3, C_GRID_THICK);
    pv_line(layer, a.x1, f2 - 1, e2, f2 - 1, 3, C_GRID_THICK);
    pv_line(layer, a.x1 + 1, a.y1, a.x1 + 1, f2, 3, C_GRID_THICK);
    pv_line(layer, e2 - 1, a.y1, e2 - 1, f2, 3, C_GRID_THICK);
}

static void pv_show(int at)
{
    if (s_pv_n <= 0) return;
    s_pv_at = (at + s_pv_n) % s_pv_n;
    s_pv_shown = pv_parse(s_pv_line[s_pv_at], &s_pv_var, &s_pv_lv, s_pv_givens, s_pv_geom);
    int n = s_pv_shown ? s_pv_geom->n : 9;
    int side = n * (PV_BOARD / n);
    lv_obj_set_size(s_pv_board, side, side);
    lv_obj_set_x(s_pv_board, (720 - side) / 2);
    lv_obj_invalidate(s_pv_board);
    char t[96];
    snprintf(t, sizeof(t), "%d / %d  -  %s%s%s", s_pv_at + 1, s_pv_n, variant_name(s_pv_var),
             s_pv_lv >= 0 ? " " : "", s_pv_lv >= 0 ? gen_level_name(s_pv_lv) : "");
    lv_label_set_text(s_pv_pos, t);
    lv_obj_set_state(s_pv_prev, LV_STATE_DISABLED, s_pv_n < 2);
    lv_obj_set_state(s_pv_next, LV_STATE_DISABLED, s_pv_n < 2);
}

static void pv_close(void)
{
    if (!s_pv) return;
    lv_obj_delete_async(s_pv);
    s_pv = NULL;
    heap_caps_free(s_pv_buf);
    heap_caps_free(s_pv_line);
    s_pv_buf = NULL;
    s_pv_line = NULL;
    s_pv_n = 0;
}

static void on_pv_close(lv_event_t *e) { pv_close(); }
static void on_pv_step(lv_event_t *e) { pv_show(s_pv_at + (int)(intptr_t)lv_event_get_user_data(e)); }

static void on_pv_import(lv_event_t *e)
{
    stats_db_import_file(s_pv_path);                /* (the result comes back as a notice) */
    pv_close();
}

/* the file read and its lines sorted out: the puzzles, what kinds, how many lines aren't one */
static int pv_load(const char *path, char *summary, size_t n)
{
    FILE *f = fopen(path, "r");
    s_pv_buf = heap_caps_malloc(PV_MAX_BYTES + 1, MALLOC_CAP_SPIRAM);
    s_pv_line = heap_caps_malloc(PV_MAX * sizeof(char *), MALLOC_CAP_SPIRAM);
    if (!s_pv_geom) s_pv_geom = heap_caps_malloc(sizeof(sudoku_geom_t), MALLOC_CAP_SPIRAM);
    if (!f || !s_pv_buf || !s_pv_line || !s_pv_geom) {
        if (f) fclose(f);
        snprintf(summary, n, f ? "Out of memory." : "Couldn't open the file.");
        return 0;
    }
    size_t len = fread(s_pv_buf, 1, PV_MAX_BYTES, f);
    bool cut = !feof(f);
    fclose(f);
    s_pv_buf[len] = 0;
    int kinds[VAR_COUNT] = {0}, bad = 0;
    if (ends_with_ci(path, ".opensudoku")) {        /* OpenSudoku's XML: each data="..." (or '...') is a Classic one */
        for (char *q = s_pv_buf; s_pv_n < PV_MAX && (q = strstr(q, "data=")) != NULL; q += 5) {
            char quote = q[5];
            if (quote != '"' && quote != '\'') continue;
            char *d = q + 6, *end = strchr(d, quote);
            if (!end) break;                        /* (cut off at the end) */
            int v, lv;
            uint8_t g[81];
            *end = 0;
            if (end - d == 81 && pv_parse(d, &v, &lv, g, s_pv_geom)) {
                s_pv_line[s_pv_n++] = d;
                kinds[v]++;
            } else {
                bad++;
            }
            q = end;
        }
    }
    for (char *p = s_pv_buf; !ends_with_ci(path, ".opensudoku") && *p && s_pv_n < PV_MAX;) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        else if (cut) break;                        /* (a line cut off at the end) */
        size_t l = strlen(p);
        while (l && (p[l - 1] == '\r' || p[l - 1] == ' ' || p[l - 1] == '\t')) p[--l] = 0;
        if (l && p[0] != '#') {
            int v, lv;
            uint8_t g[81];
            if (pv_parse(p, &v, &lv, g, s_pv_geom)) {
                s_pv_line[s_pv_n++] = p;
                kinds[v]++;
            } else {
                bad++;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    /* "42 puzzles: 30 Classic, 12 Killer" (+ the lines left out) */
    int o = snprintf(summary, n, "%d puzzle%s%s", s_pv_n, s_pv_n == 1 ? "" : "s", s_pv_n ? ": " : "");
    for (int v = 0, first = 1; v < VAR_COUNT && o < (int)n; v++) {
        if (!kinds[v]) continue;
        o += snprintf(summary + o, n - o, "%s%d %s", first ? "" : ", ", kinds[v], variant_name(v));
        first = 0;
    }
    if (bad && o < (int)n) o += snprintf(summary + o, n - o, "  -  %d %s that %s", bad,
                                         ends_with_ci(path, ".opensudoku") ? (bad == 1 ? "entry" : "entries")
                                                                           : (bad == 1 ? "line" : "lines"),
                                         bad == 1 ? "isn't a puzzle" : "aren't puzzles");
    if ((cut || s_pv_n == PV_MAX) && o < (int)n) snprintf(summary + o, n - o, "  -  the first %d shown", s_pv_n);
    return s_pv_n;
}

static void pv_open(int i)
{
    pv_close();
    snprintf(s_pv_path, sizeof(s_pv_path), "%s/%s", s_path, s_ent[i].name);
    char summary[160];
    pv_load(s_pv_path, summary, sizeof(summary));

    s_pv = lv_obj_create(lv_layer_top());           /* the whole screen: nothing under it takes taps */
    lv_obj_remove_style_all(s_pv);
    lv_obj_set_size(s_pv, 720, 720);
    lv_obj_set_style_bg_color(s_pv, C_BG, 0);
    lv_obj_set_style_bg_opa(s_pv, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_pv, true);
    lv_obj_t *t = label(s_pv, &lv_font_montserrat_22, C_TEXT, s_ent[i].name);
    lv_obj_set_width(t, 720 - 32);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(t, 16, 14);
    lv_obj_t *sm = label(s_pv, &lv_font_montserrat_16, C_MUTED, summary);
    lv_obj_set_width(sm, 720 - 32);
    lv_label_set_long_mode(sm, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(sm, 16, 48);

    s_pv_board = lv_obj_create(s_pv);
    lv_obj_remove_style_all(s_pv_board);
    lv_obj_set_pos(s_pv_board, (720 - PV_BOARD) / 2, 84);
    lv_obj_set_size(s_pv_board, PV_BOARD, PV_BOARD);
    lv_obj_add_event_cb(s_pv_board, pv_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* under it: < 3 / 42 - Killer Hard > */
    s_pv_prev = icon_btn(s_pv, LV_SYMBOL_LEFT, C_TEXT, on_pv_step, (void *)(intptr_t)-1);
    lv_obj_set_pos(s_pv_prev, (720 - PV_BOARD) / 2, 84 + PV_BOARD + 12);
    s_pv_next = icon_btn(s_pv, LV_SYMBOL_RIGHT, C_TEXT, on_pv_step, (void *)(intptr_t)1);
    lv_obj_set_pos(s_pv_next, (720 + PV_BOARD) / 2 - 56, 84 + PV_BOARD + 12);
    lv_obj_set_style_opa(s_pv_prev, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_pv_next, LV_OPA_40, LV_STATE_DISABLED);
    s_pv_pos = label(s_pv, &lv_font_montserrat_18, C_TEXT, "");
    lv_obj_set_size(s_pv_pos, PV_BOARD - 2 * 56 - 16, lv_font_get_line_height(&lv_font_montserrat_18));   /* one line */
    lv_obj_set_style_text_align(s_pv_pos, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_pv_pos, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s_pv_pos, (720 - PV_BOARD) / 2 + 56 + 8, 84 + PV_BOARD + 12 + 13);

    /* what Import does (or why it can't) */
    bool db = stats_db_ready();
    lv_obj_t *note = label(s_pv, &lv_font_montserrat_14, C_MUTED,
        !db ? "Importing needs sudoku.db: insert the card before switching on."
        : !s_pv_n ? "Nothing here to import."
        : "Import adds them to Puzzles > Saved as a collection named after the file (each checked for exactly "
          "one solution; more than 100 make several).");
    lv_obj_set_width(note, 720 - 32);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note, 16, 84 + PV_BOARD + 12 + 48 + 10);

    lv_obj_t *row = lv_obj_create(s_pv);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 720 - 32, 56);
    lv_obj_set_pos(row, 16, 720 - 16 - 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_t *cl = pill_btn(row, LV_SYMBOL_CLOSE "  Close", C_CARD2, C_TEXT, on_pv_close);
    lv_obj_set_width(cl, 300);
    lv_obj_t *im = pill_btn(row, LV_SYMBOL_DOWNLOAD "  Import", C_ACCENT, lv_color_white(), on_pv_import);
    lv_obj_set_width(im, 300);
    lv_obj_set_style_opa(im, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_state(im, LV_STATE_DISABLED, !db || !s_pv_n);

    s_pv_shown = false;
    if (s_pv_n) pv_show(0);
    else lv_label_set_text(s_pv_pos, "No puzzles");
}

/* a picture or video: the viewer, with the folder's others for Prev / Next */
static void open_media(int i)
{
    static const char *names[MAX_ENTRIES];
    int n = 0, at = 0;
    for (int k = 0; k < s_n; k++) {
        if (s_ent[k].dir || !media_view_kind(s_ent[k].name)) continue;
        if (k == i) at = n;
        names[n++] = s_ent[k].name;
    }
    if (n) media_view_open(s_scr, s_path, names, n, at);
}

static void on_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_n || s_busy || s_job_busy || in_trash()) return;
    if (!s_ent[i].dir) {
        if (media_view_kind(s_ent[i].name)) open_media(i);
        else if (is_puzzle_file(s_ent[i].name)) pv_open(i);   /* .sdm / .sdx: the preview */
        return;
    }
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
    lv_label_set_text(s_path_lbl, in_trash() ? "Recycle bin" : s_path);
    lv_obj_set_hidden(s_up, at_root());
    lv_obj_set_hidden(s_empty_btn, !in_trash() || s_n <= 0);

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
        lv_obj_t *l = label(s_list, &lv_font_montserrat_20, C_MUTED,
                            in_trash() ? "The Recycle bin is empty." : "This folder is empty.");
        lv_obj_set_style_pad_all(l, 12, 0);
        return;
    }
    for (int i = 0; i < s_n; i++) {
        const entry_t *e = &s_ent[i];
        bool prot = is_protected_name(e->name);
        bool bin = at_root() && e->dir && !strcmp(e->name, TRASH_NAME);   /* the Recycle bin itself */
        bool trashed = in_trash();                  /* an item in it: Restore, Delete for good */

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
        int media = e->dir ? MEDIA_NONE : media_view_kind(e->name);
        bool puzzles = !e->dir && is_puzzle_file(e->name);
        if ((e->dir || media || puzzles) && !trashed) {   /* a folder, picture/video or puzzle file opens on a tap
                                                             (not in the bin) */
            lv_obj_set_clickable(r, true);
            lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_add_event_cb(r, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }

        lv_obj_t *ic = label(r, &lv_font_montserrat_24, bin ? C_ORANGE : (e->dir ? C_ACCENT : C_MUTED),
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
        char shown[PATH_MAX_B];
        if (bin) strlcpy(shown, "Recycle bin", sizeof(shown));
        else if (trashed) trash_decode(e->name, shown, sizeof(shown));   /* where it was */
        else strlcpy(shown, e->name, sizeof(shown));
        lv_obj_t *nm = label(col, &lv_font_montserrat_20, C_TEXT, shown);
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
        } else if (bin) {
            snprintf(sub, sizeof(sub), "Deleted files and folders: restore them, or delete them for good");
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
        } else if (bin) {
            /* (opens on a tap; not renamed or deleted itself) */
        } else if (trashed) {
            icon_btn(r, LV_SYMBOL_UPLOAD, C_TEXT, on_restore, (void *)(intptr_t)i);   /* Restore */
            icon_btn(r, LV_SYMBOL_TRASH, C_RED, on_delete, (void *)(intptr_t)i);      /* for good */
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
    if (s_job_done) {                               /* deleted for good: read the bin again */
        s_job_done = false;
        rescan();
    }
}

/* ------------------------------------------------------------- build */
void file_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb)
{
    media_view_close();                             /* (it lives on the old screen) */
    pv_close();
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
    s_empty_btn = icon_btn(scr, LV_SYMBOL_TRASH "  Empty", C_RED, on_empty, NULL);   /* in the bin */
    lv_obj_set_size(s_empty_btn, 130, 52);
    lv_obj_align(s_empty_btn, LV_ALIGN_TOP_RIGHT, -206, 10);
    lv_obj_set_hidden(s_empty_btn, true);
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
        media_view_close();
        pv_close();
    }
}

/* Files is scanning a folder or deleting / restoring (devctl genbench waits for it) */
bool files_busy(void) { return s_busy || s_job_busy; }

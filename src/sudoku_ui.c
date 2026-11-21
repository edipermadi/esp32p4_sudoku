/*
 * Sudoku UI for a 720x720 touch screen (LVGL 9).
 *
 * Screens:
 *   - home: Players, Continue (the game in progress), Puzzles, Statistics,
 *     Gameplays and Settings, in a column or, when they don't fit, a grid;
 *     a power button (top right) turns the board off (deep sleep until RESET).
 *   - gameplays: every recorded solve, newest first, tap one to replay it.
 *   - list: a title row with a Starred toggle and a context menu (New puzzle,
 *     Enter, Learn), then two panes: the collections in a scrolling column on
 *     the left (Built-in | Saved tabs above it), the selected collection's 10x10
 *     grid of puzzles (or rows, Saved; or the starred puzzles) on the right.
 *     Solved puzzles are green, the game in progress is orange.
 *   - statistics: per level and per puzzle best times, mistakes, solves,
 *     read from sudoku.db on the microSD card (stats_db.c).
 *   - history: a calendar of the days anyone played; tap a day for who
 *     played what (from sudoku.db, opened from Home); tap a solve to
 *     replay it.
 *   - replay: a solve's move log played back over the givens, with play /
 *     pause, step, start / end, a slider and 1x-8x speed (also from the
 *     Solved dialog). Every digit change is logged (moves.h): number pad,
 *     Fill, Erase, Undo.
 *   - players: who is playing; switch, or (the context menu) add, rename, pick
 *     an avatar (an emoji, avatars.c) or delete (statistics and the game in
 *     progress are per player, in sudoku.db).
 *   - settings: dark mode, sound effects, screen timeout, Wi-Fi, time zone,
 *     formatting the microSD card (FAT32, one partition), and lending it to a
 *     computer as a USB drive (to back up or restore sudoku.db).
 *   - wifi: on/off, nearby networks, password keyboard (wifi_ui.c).
 *   - system monitor: heap, flash, microSD, CPU temperature and load, LVGL
 *     stats and the task list (sysmon_ui.c; Settings > Data).
 *   - files: the microSD card's folders and files, rename and delete; the
 *     database is protected (file_ui.c; Settings > Data).
 *
 * Clock: with Wi-Fi connected the time comes from NTP (wifi_mgr.cpp starts
 * SNTP) and the home screen shows it in the chosen time zone.
 *   - game: the 9x9 board, a 1..9 number pad on the right, a timer at the
 *     top, and Undo / Erase / Notes / Restart / Fill at the bottom. Puzzles
 *     made on the board can also be X-Sudoku (the diagonals are tinted and
 *     count as units), a 6x6 Mini board (2x3 boxes, a 1..6 pad) or a 4x4 Kids
 *     board (2x2 boxes, a 1..4 pad, big digits); the board
 *     and pad are laid out for the puzzle's variant (generator.h) when it opens. Fill puts
 *     the solution's digit in the selected cell and counts as a hint. Conflicting
 *     digits turn red; a complete board without conflicts is solved (every
 *     embedded puzzle has exactly one solution).
 *
 * Sounds: a tick on every touch, a buzz when a placed digit breaks a rule,
 * a chime when all nine of a digit are placed, a fanfare when solved.
 *
 * Screen saver: after the time chosen in Settings without a touch, the game pauses, the screen goes
 * black and the backlight turns off. The next tap only wakes it. While dark the board saves power
 * (bsp_power_save): the panel controller sleeps, the CPU runs slower and loop() polls less often.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static lv_obj_t *pin_pad(lv_obj_t *parent, lv_event_cb_t cb);
static void pin_dots(lv_obj_t *const *dots, int n, lv_color_t on);
static void pin_dots_make(lv_obj_t *parent, lv_obj_t **dots);
static void lock_tick(lv_timer_t *t);
static void lock_open(void);
static void lock_key_cb(lv_event_t *e);
static void lock_swipe_cb(lv_event_t *e);
static void lock_show(bool force);
static void pin_setup_close(bool done);
static void pin_setup_key_cb(lv_event_t *e);
static void pin_setup_cancel_cb(lv_event_t *e);
static void pin_setup_open(bool from_dd);
static void saver_clock_tick(lv_timer_t *t);
static void saver_clock_stop(void);
static void saver_wake_cb(lv_event_t *e);
static void saver_sleep(bool lock_now);
static void saver_cb(lv_timer_t *t);
static void touch_sound_cb(lv_event_t *e);

static lv_obj_t *s_saver;   /* black cover on the system layer while the screen is off */

/* ============================================================= start */

void notice_del_cb(lv_timer_t *t)
{
    lv_obj_delete((lv_obj_t *)lv_timer_get_user_data(t));
}

void sudoku_ui_notice(const char *text, uint32_t ms)
{
    /* on the top layer so it stays visible across screen changes */
    lv_obj_t *n = lv_obj_create(lv_layer_top());
    lv_obj_set_size(n, SCR_W - 40, LV_SIZE_CONTENT);
    lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(n, C_NOTICE, 0);
    lv_obj_set_style_border_color(n, C_ORANGE, 0);
    lv_obj_set_style_border_width(n, 2, 0);
    lv_obj_set_style_radius(n, 10, 0);
    lv_obj_set_style_pad_all(n, 14, 0);
    lv_obj_set_scrollable(n, false);
    lv_obj_set_clickable(n, false);
    lv_obj_t *l = make_label(n, &lv_font_montserrat_18, C_TEXT);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
    lv_timer_t *t = lv_timer_create(notice_del_cb, ms, n);
    lv_timer_set_repeat_count(t, 1);
}

const char *sudoku_ui_io_busy(void)
{
    const char *why = settings_card_busy();
    if (why) return why;
    if (replay_exporting()) return "an MP4 video is being exported";
    if (screenshot_busy()) return "a screenshot is being saved";
    if (files_busy()) return "Files is working on the card";
    if (stats_db_busy()) return "sudoku.db is busy (saving, an import, an export or a backup)";
    return NULL;
}

/* A full-screen panel over everything but the screen saver, taking every touch, while devctl
 * genbench runs (it pumps the UI meanwhile, so the panel shows and updates) */
static lv_obj_t *busy_ovl, *busy_text;

void sudoku_ui_busy(const char *text)
{
    if (!text) {
        if (busy_ovl) lv_obj_delete(busy_ovl);
        busy_ovl = busy_text = NULL;
        return;
    }
    if (!busy_ovl) {
        busy_ovl = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(busy_ovl);
        lv_obj_set_size(busy_ovl, SCR_W, SCR_W);
        lv_obj_set_style_bg_color(busy_ovl, C_BG, 0);
        lv_obj_set_style_bg_opa(busy_ovl, LV_OPA_COVER, 0);
        lv_obj_set_clickable(busy_ovl, true);       /* (the taps stop here) */
        lv_obj_t *t = make_label(busy_ovl, &lv_font_montserrat_28, C_TEXT);
        lv_label_set_text(t, LV_SYMBOL_SETTINGS "  Benchmarking");
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -60);
        busy_text = make_label(busy_ovl, &lv_font_montserrat_20, C_MUTED);
        lv_obj_set_width(busy_text, SCR_W - 80);
        lv_obj_set_style_text_align(busy_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(busy_text, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_align(busy_text, LV_ALIGN_CENTER, 0, 20);
    }
    lv_label_set_text(busy_text, text);
}

static lv_obj_t *lk_ovl, *lk_box, *lk_time, *lk_date, *lk_hint, *lk_dots[4], *lk_pad;
static lv_timer_t *lk_timer;
static char lk_entry[5];
static int lk_tries;
static uint32_t lk_wait_until;
static int32_t lk_press_y;

/* a 3 x 4 keypad: 1-9, (blank), 0, backspace; cb gets the key in user data ('0'..'9', '<') */
static lv_obj_t *pin_pad(lv_obj_t *parent, lv_event_cb_t cb)
{
    static const char KEYS[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', ' ', '0', '<'};
    lv_obj_t *pad = lv_obj_create(parent);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 3 * 110 + 2 * 12, 4 * 72 + 3 * 12);
    for (int i = 0; i < 12; i++) {
        if (KEYS[i] == ' ') continue;
        char t[4] = {KEYS[i], 0};
        lv_obj_t *b = make_btn(pad, (i % 3) * 122, (i / 3) * 84, 110, 72, KEYS[i] == '<' ? LV_SYMBOL_BACKSPACE : t,
                               &lv_font_montserrat_28, cb, (void *)(intptr_t)KEYS[i]);
        lv_obj_set_style_bg_color(b, C_CARD2, 0);
    }
    return pad;
}

static void pin_dots(lv_obj_t *const *dots, int n, lv_color_t on)
{
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(dots[i], i < n ? on : C_CARD2, 0);
    }
}

static void pin_dots_make(lv_obj_t *parent, lv_obj_t **dots)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 4 * 22 + 3 * 18, 22);
    for (int i = 0; i < 4; i++) {
        dots[i] = lv_obj_create(row);
        lv_obj_remove_style_all(dots[i]);
        lv_obj_set_size(dots[i], 22, 22);
        lv_obj_set_pos(dots[i], i * 40, 0);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dots[i], C_CARD2, 0);
    }
}

static void lock_tick(lv_timer_t *t)
{
    if (!lk_ovl) return;
    time_t now = time(NULL);
    char hm[8] = "--:--", date[48] = "";
    if (CLOCK_VALID(now)) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(hm, sizeof(hm), "%H:%M", &tm);
        strftime(date, sizeof(date), "%A, %d %B", &tm);
    }
    lv_label_set_text(lk_time, hm);
    lv_label_set_text(lk_date, date);
    if (lk_pad) {                                   /* the wait after too many wrong PINs */
        int32_t left = (int32_t)(lk_wait_until - lv_tick_get());
        bool waiting = lk_wait_until && left > 0;
        lv_obj_set_state(lk_pad, LV_STATE_DISABLED, waiting);
        lv_obj_set_style_opa(lk_pad, waiting ? LV_OPA_40 : LV_OPA_COVER, 0);
        if (waiting) lv_label_set_text_fmt(lk_hint, "Too many tries: wait %d s", (int)(left / 1000) + 1);
        else if (lk_wait_until) {
            lk_wait_until = 0;
            lv_label_set_text(lk_hint, "Enter the PIN");
        }
    }
}

static void lock_open(void)   /* unlock: remove the lock screen */
{
    if (lk_timer) lv_timer_delete(lk_timer);
    lk_timer = NULL;
    if (lk_ovl) lv_obj_delete_async(lk_ovl);
    lk_ovl = lk_box = lk_pad = NULL;
}

static void lock_key_cb(lv_event_t *e)
{
    char k = (char)(intptr_t)lv_event_get_user_data(e);
    if (lk_wait_until) return;
    size_t n = strlen(lk_entry);
    if (k == '<') {
        if (n) lk_entry[n - 1] = 0;
    } else if (n < 4) {
        lk_entry[n] = k;
        lk_entry[n + 1] = 0;
    }
    n = strlen(lk_entry);
    pin_dots(lk_dots, (int)n, C_ACCENT);
    if (n < 4) return;
    if (!strcmp(lk_entry, store_lock_pin())) {      /* right: back to where it was */
        lk_tries = 0;
        lock_open();
        return;
    }
    lk_entry[0] = 0;                                /* wrong */
    pin_dots(lk_dots, 4, C_RED);
    sfx_play(SFX_ERROR);
    if (++lk_tries >= LOCK_TRIES) {
        lk_tries = 0;
        lk_wait_until = lv_tick_get() + LOCK_WAIT_MS;
        if (!lk_wait_until) lk_wait_until = 1;
    } else {
        lv_label_set_text_fmt(lk_hint, "Wrong PIN, try again (%d left)", LOCK_TRIES - lk_tries);
    }
}

/* Swipe: the time follows the finger up; far enough, and it's unlocked */
static void lock_swipe_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        lk_press_y = p.y;
        break;
    case LV_EVENT_PRESSING: {
        int32_t dy = p.y - lk_press_y;
        lv_obj_set_style_translate_y(lk_box, dy < 0 ? dy : 0, 0);
        lv_obj_set_style_opa(lk_box, dy < 0 ? (lv_opa_t)LV_MAX(60, 255 + dy) : LV_OPA_COVER, 0);
        break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (lk_press_y - p.y >= LOCK_SWIPE_PX) {
            lock_open();
        } else {
            lv_obj_set_style_translate_y(lk_box, 0, 0);
            lv_obj_set_style_opa(lk_box, LV_OPA_COVER, 0);
        }
        break;
    default:
        break;
    }
}

/* the lock screen, under the screen saver's black cover (lv_layer_sys);
 * force (the lock button on Home): even with the setting Off, as Swipe */
static void lock_show(bool force)
{
    uint8_t mode = store_lock_mode();
    if (mode == LOCK_OFF && force) mode = LOCK_SWIPE;
    if (mode == LOCK_OFF || lk_ovl) return;
    lk_ovl = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(lk_ovl);
    lv_obj_set_size(lk_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(lk_ovl, C_BG, 0);
    lv_obj_set_style_bg_opa(lk_ovl, LV_OPA_COVER, 0);
    lv_obj_set_clickable(lk_ovl, true);             /* nothing below gets a touch */
    lv_obj_set_scrollable(lk_ovl, false);

    lk_box = lv_obj_create(lk_ovl);                 /* the time and who's playing */
    lv_obj_remove_style_all(lk_box);
    lv_obj_set_size(lk_box, LV_PCT(100), 220);
    lv_obj_align(lk_box, LV_ALIGN_TOP_MID, 0, mode == LOCK_PIN ? 30 : 150);
    lv_obj_set_flex_flow(lk_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lk_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(lk_box, 8, 0);
    lv_obj_set_clickable(lk_box, false);
    lv_obj_t *icon = make_label(lk_box, &font_icons_24, C_MUTED);
    lv_label_set_text(icon, SYMBOL_LOCK);
    lk_time = make_label(lk_box, &font_timer_36, C_TEXT);   /* (its own size: scaled, it would blur) */
    lk_date = make_label(lk_box, &lv_font_montserrat_20, C_MUTED);
    lv_obj_t *who = make_label(lk_box, &font_icons_20, C_MUTED);
    lv_label_set_text_fmt(who, SYMBOL_USER "  %s", store_player_name());

    lk_hint = make_label(lk_ovl, &lv_font_montserrat_20, C_TEXT);
    lk_entry[0] = 0;
    lk_pad = NULL;
    if (mode == LOCK_PIN) {
        lv_label_set_text(lk_hint, "Enter the PIN");
        lv_obj_align(lk_hint, LV_ALIGN_TOP_MID, 0, 250);
        lv_obj_t *dots_row;
        pin_dots_make(lk_ovl, lk_dots);
        dots_row = lv_obj_get_parent(lk_dots[0]);
        lv_obj_align(dots_row, LV_ALIGN_TOP_MID, 0, 290);
        lk_pad = pin_pad(lk_ovl, lock_key_cb);
        lv_obj_align(lk_pad, LV_ALIGN_TOP_MID, 0, 340);
    } else {
        lv_label_set_text(lk_hint, LV_SYMBOL_UP "\nSwipe up to unlock");
        lv_obj_set_style_text_align(lk_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lk_hint, LV_ALIGN_BOTTOM_MID, 0, -70);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_PRESS_LOST, NULL);
    }
    lock_tick(NULL);
    lk_timer = lv_timer_create(lock_tick, 1000, NULL);
}

/* ---- Settings: the lock and its PIN (st_lock_dd, st_pin_btn: with the other settings widgets) */
static lv_obj_t *ps_ovl, *ps_hint, *ps_dots[4];
static char ps_first[5], ps_entry[5];
static bool ps_from_dd;                             /* opened by choosing PIN (cancel: back to the old mode) */

static void pin_setup_close(bool done)
{
    if (ps_ovl) lv_obj_delete_async(ps_ovl);
    ps_ovl = NULL;
    if (!done && ps_from_dd && settings_lock_dd()) lv_dropdown_set_selected(settings_lock_dd(), store_lock_mode());
    if (settings_pin_btn()) lv_obj_set_state(settings_pin_btn(), LV_STATE_DISABLED, store_lock_mode() != LOCK_PIN);
}

static void pin_setup_key_cb(lv_event_t *e)
{
    char k = (char)(intptr_t)lv_event_get_user_data(e);
    size_t n = strlen(ps_entry);
    if (k == '<') {
        if (n) ps_entry[n - 1] = 0;
    } else if (n < 4) {
        ps_entry[n] = k;
        ps_entry[n + 1] = 0;
    }
    n = strlen(ps_entry);
    pin_dots(ps_dots, (int)n, C_ACCENT);
    if (n < 4) return;
    if (!ps_first[0]) {                             /* once more, to be sure */
        snprintf(ps_first, sizeof(ps_first), "%s", ps_entry);
        ps_entry[0] = 0;
        pin_dots(ps_dots, 0, C_ACCENT);
        lv_label_set_text(ps_hint, "Enter it again");
        return;
    }
    if (strcmp(ps_first, ps_entry) != 0) {
        ps_first[0] = ps_entry[0] = 0;
        pin_dots(ps_dots, 4, C_RED);
        sfx_play(SFX_ERROR);
        lv_label_set_text(ps_hint, "They didn't match: enter a new PIN");
        return;
    }
    store_set_lock_pin(ps_entry);
    store_set_lock_mode(LOCK_PIN);
    pin_setup_close(true);
    sudoku_ui_notice("PIN set: the screen locks with it after the screen timeout.", 3000);
}

static void pin_setup_cancel_cb(lv_event_t *e)
{
    pin_setup_close(false);
}

static void pin_setup_open(bool from_dd)
{
    if (ps_ovl) return;
    ps_from_dd = from_dd;
    ps_first[0] = ps_entry[0] = 0;
    ps_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ps_ovl);
    lv_obj_set_size(ps_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ps_ovl, C_BG, 0);
    lv_obj_set_style_bg_opa(ps_ovl, LV_OPA_COVER, 0);
    lv_obj_set_clickable(ps_ovl, true);
    lv_obj_t *t = make_label(ps_ovl, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Lock screen PIN");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
    ps_hint = make_label(ps_ovl, &lv_font_montserrat_20, C_MUTED);
    lv_label_set_text(ps_hint, "Enter a new 4-digit PIN");
    lv_obj_align(ps_hint, LV_ALIGN_TOP_MID, 0, 100);
    pin_dots_make(ps_ovl, ps_dots);
    lv_obj_align(lv_obj_get_parent(ps_dots[0]), LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_t *pad = pin_pad(ps_ovl, pin_setup_key_cb);
    lv_obj_align(pad, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_t *c = make_btn(ps_ovl, 0, 0, 240, 56, "Cancel", &lv_font_montserrat_20, pin_setup_cancel_cb, NULL);
    lv_obj_set_style_bg_color(c, C_CARD2, 0);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -30);
}

void lock_dd_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(settings_lock_dd());
    if (sel == LOCK_PIN) {                          /* a PIN first */
        pin_setup_open(true);
        return;
    }
    store_set_lock_mode((uint8_t)sel);
    lv_obj_set_state(settings_pin_btn(), LV_STATE_DISABLED, true);
}

void pin_change_cb(lv_event_t *e)
{
    pin_setup_open(false);
}
static lv_obj_t *sv_clock, *sv_time, *sv_date;
static lv_timer_t *sv_timer;
static uint32_t sv_since, sv_moved;

static void saver_clock_tick(lv_timer_t *t)
{
    if (!s_saver || !sv_clock) return;
    if (lv_tick_elaps(sv_since) >= SAVER_CLOCK_MIN * 60u * 1000u) {   /* enough: dark */
        lv_obj_delete(sv_clock);
        sv_clock = NULL;
        lv_timer_delete(sv_timer);
        sv_timer = NULL;
        lv_refr_now(NULL);
        bsp_power_save(true);                       /* light off, panel asleep, CPU slower */
        return;
    }
    time_t now = time(NULL);
    char hm[8] = "--:--", date[48] = "";
    if (CLOCK_VALID(now)) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(hm, sizeof(hm), "%H:%M", &tm);
        strftime(date, sizeof(date), "%a %d %b", &tm);
    }
    lv_label_set_text(sv_time, hm);
    lv_label_set_text(sv_date, date);
    if (!t || lv_tick_elaps(sv_moved) >= 60000) {  /* somewhere else each minute */
        sv_moved = lv_tick_get();
        lv_obj_update_layout(sv_clock);
        int32_t w = lv_obj_get_width(sv_clock), h = lv_obj_get_height(sv_clock);
        lv_obj_set_pos(sv_clock, 20 + (int32_t)(esp_random() % (uint32_t)LV_MAX(1, SCR_W - 40 - w)),
                       20 + (int32_t)(esp_random() % (uint32_t)LV_MAX(1, SCR_W - 40 - h)));
    }
}

static void saver_clock_stop(void)
{
    if (sv_timer) lv_timer_delete(sv_timer);
    sv_timer = NULL;
    sv_clock = NULL;                                /* (a child of s_saver: goes with it) */
}

static void saver_wake_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        saver_clock_stop();
        bsp_power_save(false);                      /* CPU and panel back (if it was dark) */
        bsp_display_brightness_set(effective_brightness());
    } else if (s_saver) {
        /* removed only once the finger is up, so the waking tap reaches nothing below */
        lv_obj_delete_async(s_saver);
        s_saver = NULL;
    }
}

static void saver_sleep(bool lock_now)
{
    if (lv_screen_active() == screen_game() && game_id() != STORE_NO_GAME && !game_solved() && !game_paused()) {
        set_paused(true);   /* stops the clock, hides the board, saves the game */
    }
    lock_show(lock_now);                /* (if on) under the black cover: what the waking tap shows */
    s_saver = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_saver);
    lv_obj_set_size(s_saver, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_saver, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_saver, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_saver, true);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_PRESS_LOST, NULL);
    if (store_saver_clock() && !lock_now) {        /* dim clock first (not for "lock now") */
        sv_clock = lv_obj_create(s_saver);
        lv_obj_remove_style_all(sv_clock);
        lv_obj_set_size(sv_clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(sv_clock, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sv_clock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(sv_clock, LV_OBJ_FLAG_CLICKABLE);
        sv_time = make_label(sv_clock, &font_timer_36, lv_color_hex(0x9AA0AA));
        sv_date = make_label(sv_clock, &lv_font_montserrat_20, lv_color_hex(0x6B7280));
        sv_since = lv_tick_get();
        saver_clock_tick(NULL);
        sv_timer = lv_timer_create(saver_clock_tick, 1000, NULL);
        lv_refr_now(NULL);
        bsp_display_brightness_set(SAVER_CLOCK_LIGHT);
        return;
    }
    lv_refr_now(NULL);                  /* panel shows black before the light goes off */
    bsp_power_save(true);               /* light off, panel asleep, CPU slower */
}

int sudoku_ui_fill(const uint8_t *cells, const uint8_t *digits, int n, char *why, size_t why_n)
{
    if (lv_screen_active() != screen_game() || !game_geo()) {
        snprintf(why, why_n, "no game on screen");
        return -1;
    }
    if (game_solved() || game_paused()) {
        snprintf(why, why_n, game_solved() ? "already solved" : "paused");
        return -1;
    }
    for (int k = 0; k < n; k++) {                   /* all of them first */
        int i = cells[k], d = digits[k];
        if (i > 80 || !game_geo()->used[i]) {
            snprintf(why, why_n, "cell %d isn't on the board", i);
            return -1;
        }
        if (game_given()[i]) {
            snprintf(why, why_n, "cell %d is a given", i);
            return -1;
        }
        if (d < 1 || d > game_geo()->n) {
            snprintf(why, why_n, "digit %d for cell %d: 1..%d", d, i, game_geo()->n);
            return -1;
        }
    }
    bool notes = game_notes_mode();
    game_set_notes_mode(false);                           /* digits, not notes */
    int placed = 0;
    for (int k = 0; k < n && !game_solved(); k++) {
        if (game_b()->val[cells[k]] == digits[k]) continue;   /* (a second tap would clear it) */
        game_set_sel(cells[k]);
        enter_digit(digits[k]);                     /* as from the pad */
        placed++;
    }
    game_set_notes_mode(notes);
    if (!game_solved()) {
        game_set_sel(-1);
        refresh_board();
    }
    why[0] = 0;
    return placed;
}

void sudoku_ui_screen_name(char *buf, size_t n)
{
    static const struct { lv_obj_t *(*scr)(void); const char *name; } S[] = {
        {screen_home, "home"}, {screen_list, "list"}, {screen_game, "game"}, {screen_stats, "stats"},
        {screen_settings, "settings"}, {screen_wifi, "wifi"}, {screen_profile, "players"}, {screen_history, "history"},
        {screen_sysmon, "sysmon"}, {screen_files, "files"}, {screen_plays, "gameplays"}, {screen_trophy, "trophies"},
        {screen_replay, "replay"}, {screen_edit, "edit"},
    };
    lv_obj_t *act = lv_screen_active();
    const char *name = "other";
    for (size_t i = 0; i < sizeof(S) / sizeof(S[0]); i++) {
        if (S[i].scr() && S[i].scr() == act) name = S[i].name;
    }
    int len = snprintf(buf, n, "%s", name);
    if (act == screen_home() && home_menu()) {                /* which page of the menu: home1, home2 */
        int page = (int)((lv_obj_get_scroll_x(home_menu()) + HOME_GRID_W / 2) / HOME_GRID_W) + 1;
        len += snprintf(buf + len, n - (size_t)len, "%d", page);
    }
    /* a dialog or overlay: anything screen-sized on the top layer (not the Gibberlink icon, a notice) */
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        if (!lv_obj_is_hidden(c) && lv_obj_get_width(c) >= SCR_W - 20 && lv_obj_get_height(c) >= SCR_W - 20) {
            snprintf(buf + len, n - (size_t)len, "+dialog");
            break;
        }
    }
}

bool sudoku_ui_state(sudoku_ui_state_t *st, char board[82])
{
    if (lv_screen_active() != screen_game() || !game_geo()) return false;
    memset(st, 0, sizeof(*st));
    if (game_practice() >= 0) snprintf(st->name, sizeof(st->name), "Learn practice");
    else if (game_id() != STORE_NO_GAME) puzzle_name(game_id(), st->name, sizeof(st->name));
    st->variant = game_var() >= 0 && game_var() < VAR_COUNT ? variant_name(game_var()) : "?";
    st->size = game_geo()->n;
    st->cells = game_geo()->ncells;
    for (int i = 0; i < 81; i++) {
        int v = game_b()->val[i];
        if (!game_geo()->used[i]) board[i] = '#';
        else if (!v) board[i] = '.';
        else board[i] = game_given()[i] ? (char)('0' + v) : (char)('a' + v - 1);
        if (game_geo()->used[i] && v) st->filled++;
    }
    board[81] = 0;
    st->solved = game_solved();
    st->paused = game_paused();
    st->practice = game_practice() >= 0;
    st->attack = game_attack();
    st->seconds = (game_elapsed_ms() + 500) / 1000;
    st->mistakes = game_mistakes();
    st->hints = game_hints();
    return true;
}

void sudoku_ui_screen_off(void)
{
    if (!s_saver) saver_sleep(false);
}

/* the lock button on Home: the screen off now, and locked when it wakes */
void lock_now_cb(lv_event_t *e)
{
    if (!s_saver) saver_sleep(true);
}

static void saver_cb(lv_timer_t *t)
{
    if (!s_saver && ui_sleep_min() &&
        lv_display_get_inactive_time(NULL) >= (uint32_t)ui_sleep_min() * 60 * 1000) saver_sleep(false);
}

static void touch_sound_cb(lv_event_t *e)
{
    if (s_saver) return;   /* waking tap is silent */
    sfx_play(SFX_TOUCH);
}

esp_err_t sudoku_ui_start(void)
{
    game_set_undo(heap_caps_calloc(UNDO_MAX, sizeof(board_t), MALLOC_CAP_SPIRAM));
    game_set_redo(heap_caps_calloc(UNDO_MAX, sizeof(board_t), MALLOC_CAP_SPIRAM));
    if (!game_undo() || !game_redo()) return ESP_ERR_NO_MEM;
    sudoku_geom_init();                             /* before the generator task can use the tables */
    game_set_geo(sudoku_geom(VAR_CLASSIC));
    game_set_geo_buf(heap_caps_malloc(sizeof(*game_geo_buf()), MALLOC_CAP_SPIRAM));
    ui_set_gen_cache(heap_caps_calloc(GEN_CACHE, sizeof(gen_cache_t), MALLOC_CAP_SPIRAM));
    stats_set_view_p(heap_caps_calloc(1, sizeof(stats_view_t), MALLOC_CAP_SPIRAM));
    if (!game_geo_buf() || !ui_gen_cache() || !stats_view_p()) return ESP_ERR_NO_MEM;
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(NULL);
#endif

    /* splash while the card and sudoku.db are read (a second or two) */
    lv_obj_t *boot = lv_screen_active();
    lv_obj_set_style_bg_color(boot, lv_color_hex(ui_palette_dark()->bg), 0);
    lv_obj_set_style_bg_opa(boot, LV_OPA_COVER, 0);
    lv_obj_t *splash = lv_label_create(boot);
    lv_label_set_text(splash, "Sudoku\n\nStarting...");
    lv_obj_set_style_text_align(splash, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(splash, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(splash, lv_color_hex(ui_palette_dark()->text), 0);
    lv_obj_center(splash);
    lv_refr_now(NULL);

    /* The settings and the game in progress live in sudoku.db, so the card
     * comes first. Card setup also has to happen before Wi-Fi starts (they
     * share the SDMMC host, see the microSD section). */
    sd_probe_card();
    if (sd_present()) {
        char sz[24];
        fmt_gb(sz, sizeof(sz), sd_bytes());
        printf("microSD: \"%s\", %s\n", sd_name(), sz);
        stats_db_start();
        if (stats_db_wait_open(4000)) {
            store_prefs_t prefs;
            if (stats_db_take_prefs(&prefs)) store_apply_prefs(&prefs);
            static uint16_t c_best[PUZZLE_COUNT];
            static uint8_t c_fewest[PUZZLE_COUNT], c_solves[PUZZLE_COUNT];
            if (stats_db_take_cache(c_best, c_fewest, c_solves)) {
                store_reset_stats();                /* sudoku.db is the record: show exactly what it has */
                store_merge_stats(c_best, c_fewest, c_solves);
            }
        }
    } else {
        printf("microSD: no card\n");
    }
    if (sd_present() && usb_msc_start() != ESP_OK) {   /* empty drive until Connect; tells us a computer is there */
        printf("usb_msc: USB didn't start\n");
    }
    wifi_mgr_start();               /* reconnects to the saved network if Wi-Fi was left on */

    sfx_set_enabled(store_sound_enabled());
    gibberlink_set_enabled(store_gibberlink());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(effective_brightness());
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        lv_indev_add_event_cb(in, touch_sound_cb, LV_EVENT_PRESSED, NULL);
    }
    tz_apply(store_timezone());
    list_set_coll(store_last_collection());
    list_set_tab(coll_tab(list_coll()));
    ui_set_sleep_min(store_screen_timeout_min());
    apply_theme(store_dark_mode());
    lv_obj_delete(splash);

    game_set_last_tick(lv_tick_get());
    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(saver_cb, 1000, NULL);
    lv_timer_create(clock_cb, 1000, NULL);
    lv_timer_create(night_check, 20000, NULL);         /* night mode: dark and dimmer at night */
    lv_timer_create(sd_poll_cb, 100, NULL);
    clock_cb(NULL);
    return ESP_OK;
}

/* ---- accessors: the other UI files reach this file's state through these */
lv_obj_t *ui_saver(void) { return s_saver; }

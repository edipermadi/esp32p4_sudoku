/*
 * Sudoku UI for a 720x720 touch screen (LVGL 9).
 *
 * Screens:
 *   - home: Continue (the game in progress), Puzzles and Settings.
 *   - list: a scrolling row of collections at the top, a 10x10 grid of
 *     puzzles below it.
 *     Solved puzzles are green, the game in progress is orange.
 *   - statistics: per level and per puzzle best times, mistakes, solves.
 *   - settings: dark mode, sound effects, screen timeout, Wi-Fi, time zone,
 *     and formatting the microSD card (FAT32, one partition).
 *   - wifi: on/off, nearby networks, password keyboard (wifi_ui.c).
 *
 * Clock: with Wi-Fi connected the time comes from NTP (wifi_mgr.cpp starts
 * SNTP) and the home screen shows it in the chosen time zone.
 *   - game: the 9x9 board, a 1..9 number pad on the right, a timer at the
 *     top, and Undo / Erase / Notes / Restart at the bottom. Conflicting
 *     digits turn red; a complete board without conflicts is solved (every
 *     embedded puzzle has exactly one solution).
 *
 * Sounds: a tick on every touch, a buzz when a placed digit breaks a rule,
 * a chime when all nine of a digit are placed, a fanfare when solved.
 *
 * Screen saver: after the time chosen in Settings without a touch, the game pauses, the screen goes
 * black and the backlight turns off. The next tap only wakes it.
 */
#include "sudoku_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <lvgl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "puzzles.h"
#include "store.h"
#include "sfx.h"
#include "bsp/bsp_board.h"
#include "ui_palette.h"
#include "wifi_mgr.h"
#include "wifi_ui.h"
#include "timezones.h"

/* "Screen off" dropdown choices, in minutes (0 = never) */
static const uint8_t SLEEP_MIN[] = {1, 2, 5, 10, 15, 30, 0};
#define SLEEP_OPTIONS "1 min\n2 min\n5 min\n10 min\n15 min\n30 min\nNever"
#define SLEEP_COUNT   (sizeof(SLEEP_MIN) / sizeof(SLEEP_MIN[0]))

LV_FONT_DECLARE(font_digits_44)   /* " 0-9", grid digits */
LV_FONT_DECLARE(font_timer_36)    /* " 0-9:", game timer */

/* ------------------------------------------------------------- palette */
/* palette_t and the C_* colour macros are in ui_palette.h (shared with wifi_ui.c) */
static const palette_t PAL_DARK = {
    .bg = 0x101218, .card = 0x1B1E27, .card2 = 0x2A2F3D, .text = 0xEDEFF4, .muted = 0x8B92A5,
    .accent = 0x4F8CFF, .green = 0x3DD68C, .orange = 0xFFA94D, .red = 0xFF6B6B,
    .grid_thick = 0x8B92A5, .grid_thin = 0x3A3F4E,
    .cell = 0x1B1E27, .cell_given = 0x22262F, .cell_peer = 0x272D3D, .cell_same = 0x2F3F63,
    .cell_sel = 0x3A5BA0, .cell_bad = 0x4A2329, .cell_solved = 0x173A2A, .digit_user = 0x7FB0FF,
    .list_solved = 0x1E7A4F, .list_active = 0xA0621C, .notice = 0x5A2A1A, .scrim_opa = LV_OPA_60,
};

static const palette_t PAL_LIGHT = {
    .bg = 0xF2F3F7, .card = 0xFFFFFF, .card2 = 0xE2E5EC, .text = 0x1B1E27, .muted = 0x6B7285,
    .accent = 0x2F6FE0, .green = 0x1E9E62, .orange = 0xD9822B, .red = 0xD93A3A,
    .grid_thick = 0x3A3F4E, .grid_thin = 0xC5CAD6,
    .cell = 0xFFFFFF, .cell_given = 0xEEF0F4, .cell_peer = 0xE3E9F5, .cell_same = 0xC9D8F5,
    .cell_sel = 0x9DBBF2, .cell_bad = 0xF8D7DA, .cell_solved = 0xD4F1E2, .digit_user = 0x2F6FE0,
    .list_solved = 0x2FA36B, .list_active = 0xD98A2B, .notice = 0xFFE8D6, .scrim_opa = LV_OPA_40,
};

const palette_t *ui_pal = &PAL_DARK;
#define P ui_pal
static bool s_dark = true;
static uint8_t s_sleep_min = 5;       /* screen timeout, 0 = never */

/* ------------------------------------------------------------- geometry */
#define SCR_W       720
#define COLL_W      252     /* left-hand collection column (list, statistics) */
#define PANE_X      (12 + COLL_W + 12)
#define PANE_W      (SCR_W - PANE_X - 12)   /* 432 */
#define TOP_H       60
#define CELL        64
#define GRID_X      8
#define GRID_Y      64
#define GRID_SIZE   604     /* 4 + 3 * (3*64 + 2*2) + 2*4 + 4 */
#define BOX_SIZE    196     /* 3*64 + 2*2 */
#define PAD_X       (GRID_X + GRID_SIZE + 8)
#define PAD_W       (SCR_W - PAD_X - 8)
#define PAD_H       62
#define PAD_GAP     5
#define BOT_Y       (GRID_Y + GRID_SIZE + 6)
#define BOT_H       (SCR_W - BOT_Y - 6)

#define SAVE_DELAY_MS    80      /* flash write after the screen has changed */
#define UNDO_MAX         128
#define MAX_PER_COLL     100

static const char *const DIGITS[10] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

/* ------------------------------------------------------------- game state */
typedef struct {
    uint8_t  val[81];
    uint16_t notes[81];
} board_t;

static uint16_t g_id = STORE_NO_GAME;
static uint8_t  g_given[81];
static board_t  g_b;
static bool     g_conflict[81];
static int      g_sel = -1;
static bool     g_notes_mode;
static bool     g_solved;
static bool     g_paused;
static uint32_t g_elapsed_ms;
static uint16_t g_mistakes;      /* digits placed that broke a rule (undo doesn't remove them) */
static uint32_t g_last_tick;

static board_t  g_undo[UNDO_MAX];
static int      g_undo_head, g_undo_len;

/* ------------------------------------------------------------- widgets */
static lv_obj_t *scr_home, *scr_list, *scr_stats, *scr_settings, *scr_wifi, *scr_game;

/* home screen */
static lv_obj_t *h_total, *h_continue, *h_continue_sub, *h_clock, *h_date;

/* statistics screen: row 0 of the left column is "All levels" */
#define X_ROWS (PUZZLE_COLLECTION_COUNT + 1)
static lv_obj_t *x_btn[X_ROWS], *x_name_lbl[X_ROWS], *x_cnt_lbl[X_ROWS];
static lv_obj_t *x_title, *x_sum1, *x_sum2, *x_table, *x_empty;
static int       x_sel;          /* 0 = all levels, 1.. = collection x_sel-1 */

/* settings screen */
static lv_obj_t *st_theme_sw, *st_sound_sw, *st_sleep_dd, *st_wifi_lbl, *st_tz_dd;
static char      st_tz_txt[48];     /* closed dropdown shows just the IANA name */
static lv_obj_t *st_sd_btn;

/* list screen */
static lv_obj_t *l_total, *l_strip, *l_arrow_l, *l_arrow_r;
static lv_obj_t *l_coll_btn[PUZZLE_COLLECTION_COUNT], *l_coll_cnt[PUZZLE_COLLECTION_COUNT];
static lv_obj_t *l_coll_name[PUZZLE_COLLECTION_COUNT];
static lv_obj_t *l_name, *l_stats, *l_btnm;
static int       l_coll;
static char      l_labels[MAX_PER_COLL][4];
static const char *l_map[MAX_PER_COLL + MAX_PER_COLL / 10 + 1];
static uint16_t  l_pending_id;

/* game screen */
static lv_obj_t *g_cells[81], *g_cell_lbl[81];
static lv_obj_t *g_pad[9], *g_pad_cnt[9];
static lv_obj_t *g_title, *g_best, *g_timer, *g_timer_icon, *g_status, *g_notes_btn;
static lv_obj_t *g_pause_ovl, *g_win_ovl, *g_win_time, *g_win_best, *g_win_miss;

static void show_list(void);
static void open_puzzle(uint16_t id);

/* ============================================================= helpers */

static void fmt_time(char *buf, size_t n, uint32_t s)
{
    if (s >= 3600) {
        snprintf(buf, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    } else {
        snprintf(buf, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

static void puzzle_name(uint16_t id, char *buf, size_t n)
{
    int c = puzzle_collection_of(id);
    snprintf(buf, n, "%s #%u", puzzle_collections[c].name, (unsigned)(id - puzzle_collections[c].first + 1));
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                          const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, C_CARD, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (text) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_obj_center(l);
    }
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* ============================================================= sudoku rules */

static bool is_peer(int a, int b)
{
    int ra = a / 9, ca = a % 9, rb = b / 9, cb = b % 9;
    return ra == rb || ca == cb || (ra / 3 == rb / 3 && ca / 3 == cb / 3);
}

/* Marks every filled cell that shares a row, column or box with the same digit. */
static int compute_conflicts(void)
{
    int n = 0;
    memset(g_conflict, 0, sizeof(g_conflict));
    for (int i = 0; i < 81; i++) {
        if (!g_b.val[i]) continue;
        for (int j = i + 1; j < 81; j++) {
            if (g_b.val[j] == g_b.val[i] && is_peer(i, j)) {
                g_conflict[i] = g_conflict[j] = true;
            }
        }
    }
    for (int i = 0; i < 81; i++) n += g_conflict[i];
    return n;
}

static int filled_count(void)
{
    int n = 0;
    for (int i = 0; i < 81; i++) n += g_b.val[i] != 0;
    return n;
}

static bool has_progress(void)
{
    for (int i = 0; i < 81; i++) {
        if ((g_b.val[i] && !g_given[i]) || g_b.notes[i]) return true;
    }
    return false;
}

/* ============================================================= persistence */

/*
 * The game in progress is saved only when leaving the game screen, when
 * pausing, and (as a best time) when solved. Writing flash stalls PSRAM
 * access long enough to blink the MIPI-DSI panel, so the write happens a
 * moment after the screen has already changed, where a blink is not seen.
 */
static void commit_cb(lv_timer_t *t)
{
    store_commit_game();
}

static void save_current(void)
{
    if (g_id == STORE_NO_GAME || g_solved) return;
    const saved_game_t *s = store_saved_game();
    if (!has_progress() && s->id != g_id) return;   /* only looked at it: keep the slot as is */
    saved_game_t sg;
    sg.id = g_id;
    memcpy(sg.val, g_b.val, sizeof(sg.val));
    memcpy(sg.notes, g_b.notes, sizeof(sg.notes));
    sg.elapsed_ms = g_elapsed_ms;
    sg.mistakes = g_mistakes;
    store_set_game(&sg);
    lv_timer_t *t = lv_timer_create(commit_cb, SAVE_DELAY_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/* ============================================================= game view */

static void update_timer_label(void)
{
    char buf[16];
    fmt_time(buf, sizeof(buf), g_elapsed_ms / 1000);
    if (strcmp(lv_label_get_text(g_timer), buf) != 0) lv_label_set_text(g_timer, buf);
    lv_obj_set_style_text_color(g_timer, g_solved ? C_GREEN : (g_paused ? C_MUTED : C_TEXT), 0);
}

static void update_header(void)
{
    char name[40], best[24], buf[48];
    puzzle_name(g_id, name, sizeof(name));
    lv_label_set_text(g_title, name);
    uint32_t b = store_best_time(g_id);
    if (b) {
        fmt_time(best, sizeof(best), b);
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  Solved  -  best %s", best);
        lv_obj_set_style_text_color(g_best, C_GREEN, 0);
    } else {
        snprintf(buf, sizeof(buf), "Not solved yet");
        lv_obj_set_style_text_color(g_best, C_MUTED, 0);
    }
    lv_label_set_text(g_best, buf);
    lv_label_set_text(g_timer_icon, g_solved ? LV_SYMBOL_OK : (g_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE));
    lv_obj_set_style_text_color(g_timer_icon, g_solved ? C_GREEN : C_MUTED, 0);
    update_timer_label();
}

static void refresh_board(void)
{
    int sv = g_sel >= 0 ? g_b.val[g_sel] : 0;
    for (int i = 0; i < 81; i++) {
        lv_color_t bg = g_given[i] ? C_CELL_GIVEN : C_CELL;
        if (g_solved) {
            bg = C_CELL_SOLVED;
        } else if (g_sel >= 0) {
            if (i == g_sel) bg = C_CELL_SEL;
            else if (sv && g_b.val[i] == sv) bg = C_CELL_SAME;
            else if (is_peer(i, g_sel)) bg = C_CELL_PEER;
        }
        if (!g_solved && g_conflict[i] && !g_given[i] && i != g_sel) bg = C_CELL_BAD;
        lv_obj_set_style_bg_color(g_cells[i], bg, 0);

        lv_color_t fg = g_given[i] ? C_TEXT : C_DIGIT_USER;
        if (g_solved) fg = g_given[i] ? C_TEXT : C_GREEN;
        else if (g_conflict[i]) fg = C_RED;
        lv_obj_set_style_text_color(g_cell_lbl[i], fg, 0);

        const char *t = DIGITS[g_b.val[i]];
        if (strcmp(lv_label_get_text(g_cell_lbl[i]), t) != 0) lv_label_set_text_static(g_cell_lbl[i], t);
        lv_obj_invalidate(g_cells[i]);   /* notes are custom-drawn */
    }

    /* number pad: digits left to place; a finished digit is dimmed */
    int count[10] = {0};
    for (int i = 0; i < 81; i++) count[g_b.val[i]]++;
    for (int d = 1; d <= 9; d++) {
        int left = 9 - count[d];
        lv_label_set_text_fmt(g_pad_cnt[d - 1], "%d", left > 0 ? left : 0);
        lv_obj_set_style_opa(g_pad[d - 1], left <= 0 ? LV_OPA_40 : LV_OPA_COVER, 0);
    }

    /* status line */
    int bad = 0;
    for (int i = 0; i < 81; i++) bad += g_conflict[i];
    if (g_solved) {
        char t[16];
        fmt_time(t, sizeof(t), g_elapsed_ms / 1000);
        lv_label_set_text_fmt(g_status, LV_SYMBOL_OK "  Solved in %s", t);
        lv_obj_set_style_text_color(g_status, C_GREEN, 0);
    } else if (bad) {
        lv_label_set_text_fmt(g_status, LV_SYMBOL_WARNING "  %d conflicting", bad);
        lv_obj_set_style_text_color(g_status, C_RED, 0);
    } else {
        lv_label_set_text_fmt(g_status, "Filled %d / 81", filled_count());
        lv_obj_set_style_text_color(g_status, C_MUTED, 0);
    }
}

static uint16_t s_solve_id;
static uint32_t s_solve_secs, s_solve_mistakes;

static void solve_commit_cb(lv_timer_t *t)
{
    store_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes);
    if (store_saved_game()->id == s_solve_id) store_clear_game();
}

static void on_solved(void)
{
    g_solved = true;
    g_paused = false;
    sfx_play(SFX_SOLVED);
    g_sel = -1;
    uint32_t secs = (g_elapsed_ms + 500) / 1000;
    uint32_t prev = store_best_time(g_id);
    uint32_t rec = secs ? (secs > 0xFFFF ? 0xFFFF : secs) : 1;   /* as store_record_solve() stores it */
    bool best = !prev || rec < prev;
    s_solve_id = g_id;
    s_solve_secs = secs;
    s_solve_mistakes = g_mistakes;
    lv_timer_t *ct = lv_timer_create(solve_commit_cb, SAVE_DELAY_MS, NULL);   /* after the dialog is up */
    lv_timer_set_repeat_count(ct, 1);

    char t[16], b[16];
    fmt_time(t, sizeof(t), secs);
    lv_label_set_text(g_win_time, t);
    if (best && prev) {
        fmt_time(b, sizeof(b), prev);
        lv_label_set_text_fmt(g_win_best, "New best!  (was %s)", b);
        lv_obj_set_style_text_color(g_win_best, C_ORANGE, 0);
    } else if (best) {
        lv_label_set_text(g_win_best, "First solve!");
        lv_obj_set_style_text_color(g_win_best, C_ORANGE, 0);
    } else {
        fmt_time(b, sizeof(b), prev);
        lv_label_set_text_fmt(g_win_best, "Best time %s", b);
        lv_obj_set_style_text_color(g_win_best, C_MUTED, 0);
    }
    if (g_mistakes == 0) {
        lv_label_set_text(g_win_miss, "No mistakes");
        lv_obj_set_style_text_color(g_win_miss, C_GREEN, 0);
    } else {
        lv_label_set_text_fmt(g_win_miss, "%u mistake%s", (unsigned)g_mistakes, g_mistakes == 1 ? "" : "s");
        lv_obj_set_style_text_color(g_win_miss, C_MUTED, 0);
    }
    lv_obj_set_hidden(g_pause_ovl, true);
    lv_obj_set_hidden(g_win_ovl, false);
    update_header();
}

static void after_move(void)
{
    compute_conflicts();
    bool full = filled_count() == 81;
    bool ok = true;
    for (int i = 0; i < 81; i++) ok &= !g_conflict[i];
    if (full && ok) on_solved();
    refresh_board();
}

static void push_undo(void)
{
    g_undo[g_undo_head] = g_b;
    g_undo_head = (g_undo_head + 1) % UNDO_MAX;
    if (g_undo_len < UNDO_MAX) g_undo_len++;
}

static bool can_edit(void)
{
    return g_sel >= 0 && !g_given[g_sel] && !g_solved && !g_paused;
}

static void enter_digit(int d)
{
    if (!can_edit()) return;
    int i = g_sel;
    bool placed = false;
    if (g_notes_mode) {
        if (g_b.val[i]) return;
        push_undo();
        g_b.notes[i] ^= (uint16_t)(1u << d);
    } else {
        push_undo();
        if (g_b.val[i] == d) {
            g_b.val[i] = 0;
        } else {
            g_b.val[i] = (uint8_t)d;
            g_b.notes[i] = 0;
            placed = true;
            for (int j = 0; j < 81; j++) {           /* digit placed: drop it from peers' notes */
                if (j != i && is_peer(i, j)) g_b.notes[j] &= (uint16_t)~(1u << d);
            }
        }
    }
    after_move();
    if (!placed || g_solved) return;          /* on_solved() plays its own sound */

    if (g_conflict[i]) {
        if (g_mistakes < 0xFFFF) g_mistakes++;
        sfx_play(SFX_ERROR);
        return;
    }
    int n = 0;
    bool clean = true;
    for (int j = 0; j < 81; j++) {
        if (g_b.val[j] == d) {
            n++;
            clean &= !g_conflict[j];
        }
    }
    if (n == 9 && clean) sfx_play(SFX_DIGIT_DONE);
}

/* ------------------------------------------------------------- events */

static void cell_click_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    g_sel = i;
    refresh_board();
}

static void cell_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_b.val[i] || !g_b.notes[i]) return;
    lv_obj_t *cell = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(cell, &co);
    int sv = g_sel >= 0 ? g_b.val[g_sel] : 0;

    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &lv_font_montserrat_16;
    d.align = LV_TEXT_ALIGN_CENTER;
    for (int n = 1; n <= 9; n++) {
        if (!(g_b.notes[i] & (1u << n))) continue;
        int r = (n - 1) / 3, c = (n - 1) % 3;
        lv_area_t a = {
            .x1 = co.x1 + 1 + c * 21, .y1 = co.y1 + 2 + r * 21,
            .x2 = co.x1 + 1 + c * 21 + 20, .y2 = co.y1 + 2 + r * 21 + 20,
        };
        d.color = n == sv ? C_ACCENT : C_MUTED;
        d.text = DIGITS[n];
        lv_draw_label(layer, &d, &a);
    }
}

static void pad_click_cb(lv_event_t *e)
{
    enter_digit((int)(intptr_t)lv_event_get_user_data(e));
}

static void erase_cb(lv_event_t *e)
{
    if (!can_edit()) return;
    if (!g_b.val[g_sel] && !g_b.notes[g_sel]) return;
    push_undo();
    g_b.val[g_sel] = 0;
    g_b.notes[g_sel] = 0;
    after_move();
}

static void undo_cb(lv_event_t *e)
{
    if (g_solved || g_paused || g_undo_len == 0) return;
    g_undo_head = (g_undo_head + UNDO_MAX - 1) % UNDO_MAX;
    g_undo_len--;
    g_b = g_undo[g_undo_head];
    after_move();
}

static void notes_cb(lv_event_t *e)
{
    g_notes_mode = lv_obj_has_state(g_notes_btn, LV_STATE_CHECKED);
}

static void set_paused(bool p)
{
    if (g_solved) p = false;
    g_paused = p;
    lv_obj_set_hidden(g_pause_ovl, !p);
    update_header();
    if (p) save_current();
}

static void pause_cb(lv_event_t *e)
{
    set_paused(!g_paused);
}

static void reset_game(void)
{
    memcpy(g_b.val, g_given, sizeof(g_b.val));
    memset(g_b.notes, 0, sizeof(g_b.notes));
    g_elapsed_ms = 0;
    g_mistakes = 0;
    g_undo_len = g_undo_head = 0;
    g_sel = -1;
    g_solved = false;
    lv_obj_set_hidden(g_win_ovl, true);
    set_paused(false);
    compute_conflicts();
    refresh_board();
}

static void restart_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (store_saved_game()->id == g_id) store_clear_game();
    reset_game();
    lv_msgbox_close_async(mbox);
}

static void mbox_close_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

static lv_obj_t *make_msgbox(const char *title, const char *text)
{
    lv_obj_t *m = lv_msgbox_create(NULL);
    lv_msgbox_add_title(m, title);
    lv_msgbox_add_text(m, text);
    lv_obj_set_width(m, 520);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, 0);
    return m;
}

static void msgbox_button(lv_obj_t *m, const char *text, lv_event_cb_t cb, bool primary)
{
    lv_obj_t *b = lv_msgbox_add_footer_button(m, text);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 52);
    if (!primary) lv_obj_set_style_bg_color(b, C_CARD, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, m);
}

static void restart_cb(lv_event_t *e)
{
    if (!g_solved && !has_progress()) {
        reset_game();
        return;
    }
    lv_obj_t *m = make_msgbox("Restart puzzle?", "All entries and the timer will be reset.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Restart", restart_confirm_cb, true);
}

static void back_cb(lv_event_t *e)
{
    save_current();
    show_list();
}

static void win_next_cb(lv_event_t *e)
{
    int c = puzzle_collection_of(g_id);
    const puzzle_collection_t *pc = &puzzle_collections[c];
    uint16_t next = g_id + 1;
    if (next >= pc->first + pc->count) next = pc->first;
    open_puzzle(next);
}

static void win_list_cb(lv_event_t *e)
{
    show_list();
}

static void win_hide_cb(lv_event_t *e)
{
    /* tap outside the card: look at the solved board */
    if (lv_event_get_target_obj(e) == g_win_ovl) lv_obj_set_hidden(g_win_ovl, true);
}

static void tick_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    bool running = g_id != STORE_NO_GAME && lv_screen_active() == scr_game && !g_solved && !g_paused;
    if (running) {
        g_elapsed_ms += now - g_last_tick;
        update_timer_label();
    }
    g_last_tick = now;
}

static void start_game(uint16_t id, bool resume)
{
    g_id = id;
    puzzle_load(id, g_given);
    reset_game();
    const saved_game_t *s = store_saved_game();
    if (resume && s->id == id) {
        memcpy(g_b.val, s->val, sizeof(g_b.val));
        memcpy(g_b.notes, s->notes, sizeof(g_b.notes));
        for (int i = 0; i < 81; i++) {
            if (g_given[i]) g_b.val[i] = g_given[i];   /* givens are never editable */
            if (g_b.val[i] > 9) g_b.val[i] = 0;
        }
        g_elapsed_ms = s->elapsed_ms;
        g_mistakes = s->mistakes;
    }
    g_notes_mode = false;
    lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
    g_last_tick = lv_tick_get();
    compute_conflicts();
    refresh_board();
    update_header();
    lv_screen_load(scr_game);
}

/* ------------------------------------------------------------- build */

static void build_game(void)
{
    scr_game = make_screen();

    /* top bar */
    make_btn(scr_game, 8, 6, 104, 48, LV_SYMBOL_LEFT "  List", &lv_font_montserrat_18, back_cb, NULL);
    g_title = make_label(scr_game, &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(g_title, 126, 6);
    g_best = make_label(scr_game, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(g_best, 126, 36);

    lv_obj_t *tb = make_btn(scr_game, SCR_W - 8 - 200, 6, 200, 48, NULL, NULL, pause_cb, NULL);
    lv_obj_set_style_bg_opa(tb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, LV_STATE_PRESSED);
    g_timer_icon = make_label(tb, &lv_font_montserrat_20, C_MUTED);
    lv_obj_align(g_timer_icon, LV_ALIGN_LEFT_MID, 12, 0);
    g_timer = make_label(tb, &font_timer_36, C_TEXT);
    lv_obj_align(g_timer, LV_ALIGN_RIGHT_MID, -6, 0);

    /* board: thick-line background, 9 boxes in thin-line colour, 81 cells */
    lv_obj_t *grid = lv_obj_create(scr_game);
    lv_obj_remove_style_all(grid);
    lv_obj_set_pos(grid, GRID_X, GRID_Y);
    lv_obj_set_size(grid, GRID_SIZE, GRID_SIZE);
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    for (int b = 0; b < 9; b++) {
        lv_obj_t *box = lv_obj_create(grid);
        lv_obj_remove_style_all(box);
        lv_obj_set_pos(box, 4 + (b % 3) * (BOX_SIZE + 4), 4 + (b / 3) * (BOX_SIZE + 4));
        lv_obj_set_size(box, BOX_SIZE, BOX_SIZE);
        lv_obj_set_style_bg_color(box, C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(box, false);
        lv_obj_set_clickable(box, false);
    }
    for (int i = 0; i < 81; i++) {
        int r = i / 9, c = i % 9;
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, 4 + (c / 3) * (BOX_SIZE + 4) + (c % 3) * (CELL + 2),
                             4 + (r / 3) * (BOX_SIZE + 4) + (r % 3) * (CELL + 2));
        lv_obj_set_size(cell, CELL, CELL);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_clickable(cell, true);
        lv_obj_set_scrollable(cell, false);
        lv_obj_add_event_cb(cell, cell_click_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cell, cell_draw_cb, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)i);
        lv_obj_t *l = make_label(cell, &font_digits_44, C_TEXT);
        lv_obj_center(l);
        g_cells[i] = cell;
        g_cell_lbl[i] = l;
    }

    /* pause cover */
    g_pause_ovl = lv_obj_create(scr_game);
    lv_obj_remove_style_all(g_pause_ovl);
    lv_obj_set_pos(g_pause_ovl, GRID_X, GRID_Y);
    lv_obj_set_size(g_pause_ovl, GRID_SIZE, GRID_SIZE);
    lv_obj_set_style_bg_color(g_pause_ovl, C_CARD, 0);
    lv_obj_set_style_bg_opa(g_pause_ovl, LV_OPA_COVER, 0);
    lv_obj_set_clickable(g_pause_ovl, true);
    lv_obj_set_hidden(g_pause_ovl, true);
    lv_obj_add_event_cb(g_pause_ovl, pause_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = make_label(g_pause_ovl, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(pl, LV_SYMBOL_PAUSE "  Paused");
    lv_obj_align(pl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t *pl2 = make_label(g_pause_ovl, &lv_font_montserrat_18, C_MUTED);
    lv_label_set_text(pl2, "Tap to resume");
    lv_obj_align(pl2, LV_ALIGN_CENTER, 0, 24);

    /* number pad */
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *b = make_btn(scr_game, PAD_X, GRID_Y + 1 + (d - 1) * (PAD_H + PAD_GAP), PAD_W, PAD_H,
                               DIGITS[d], &lv_font_montserrat_28, pad_click_cb, (void *)(intptr_t)d);
        g_pad[d - 1] = b;
        g_pad_cnt[d - 1] = make_label(b, &lv_font_montserrat_12, C_MUTED);
        lv_obj_align(g_pad_cnt[d - 1], LV_ALIGN_TOP_RIGHT, -6, 4);
    }

    /* bottom bar */
    int bx = 8, bw = 118;
    make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_LEFT "  Undo", &lv_font_montserrat_16, undo_cb, NULL);
    bx += bw + 8;
    make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_BACKSPACE "  Erase", &lv_font_montserrat_16, erase_cb, NULL);
    bx += bw + 8;
    g_notes_btn = make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_EDIT "  Notes", &lv_font_montserrat_16, NULL, NULL);
    lv_obj_set_checkable(g_notes_btn, true);
    lv_obj_add_event_cb(g_notes_btn, notes_cb, LV_EVENT_VALUE_CHANGED, NULL);
    bx += bw + 8;
    make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_REFRESH "  Restart", &lv_font_montserrat_16, restart_cb, NULL);
    bx += bw + 8;
    g_status = make_label(scr_game, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(g_status, SCR_W - 8 - bx);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_status, bx, BOT_Y + (BOT_H - 22) / 2);

    /* solved dialog */
    g_win_ovl = lv_obj_create(scr_game);
    lv_obj_remove_style_all(g_win_ovl);
    lv_obj_set_size(g_win_ovl, SCR_W, SCR_W);
    lv_obj_set_style_bg_color(g_win_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_win_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(g_win_ovl, true);
    lv_obj_set_hidden(g_win_ovl, true);
    lv_obj_add_event_cb(g_win_ovl, win_hide_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(g_win_ovl);
    lv_obj_set_size(card, 460, 330);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_GREEN, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_scrollable(card, false);

    lv_obj_t *wt = make_label(card, &lv_font_montserrat_28, C_GREEN);
    lv_label_set_text(wt, LV_SYMBOL_OK "  Solved!");
    lv_obj_align(wt, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(tl, "Time to solve");
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 62);
    g_win_time = make_label(card, &font_timer_36, C_TEXT);
    lv_obj_align(g_win_time, LV_ALIGN_TOP_MID, 0, 86);
    g_win_best = make_label(card, &lv_font_montserrat_18, C_MUTED);
    lv_obj_align(g_win_best, LV_ALIGN_TOP_MID, 0, 136);
    g_win_miss = make_label(card, &lv_font_montserrat_18, C_MUTED);
    lv_obj_align(g_win_miss, LV_ALIGN_TOP_MID, 0, 164);

    lv_obj_t *b1 = make_btn(card, 0, 0, 196, 60, LV_SYMBOL_LIST "  Puzzles", &lv_font_montserrat_18, win_list_cb, NULL);
    lv_obj_align(b1, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(b1, C_CARD2, 0);
    lv_obj_t *b2 = make_btn(card, 0, 0, 196, 60, "Next  " LV_SYMBOL_RIGHT, &lv_font_montserrat_18, win_next_cb, NULL);
    lv_obj_align(b2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(b2, C_ACCENT, 0);
}

/* ============================================================= list screen */

static void list_btnm_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) return;
    uint16_t id = puzzle_collections[l_coll].first + base->id1;
    bool solved = store_best_time(id) != 0;
    bool active = store_saved_game()->id == id;
    if (!solved && !active) return;
    if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *f = lv_draw_task_get_fill_dsc(t);
        if (lv_buttonmatrix_get_selected_button(base->obj) == base->id1 &&
            lv_obj_has_state(base->obj, LV_STATE_PRESSED)) {
            return;   /* keep the pressed colour */
        }
        f->color = active ? C_LIST_ACTIVE : C_LIST_SOLVED;
    } else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_LABEL) {
        lv_draw_label_dsc_t *l = lv_draw_task_get_label_dsc(t);
        l->color = lv_color_white();
    }
}

static void refresh_list(void)
{
    const puzzle_collection_t *pc = &puzzle_collections[l_coll];
    uint16_t total = 0;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        const puzzle_collection_t *p = &puzzle_collections[c];
        uint16_t n = store_solved_count(p->first, p->count);
        total += n;
        lv_label_set_text_fmt(l_coll_cnt[c], "%u/%u", (unsigned)n, (unsigned)p->count);
        /* selected row is accent blue: white text there in both themes */
        bool sel = c == l_coll;
        lv_obj_set_state(l_coll_btn[c], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(l_coll_name[c], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(l_coll_cnt[c], sel ? lv_color_white()
                                                   : (n == p->count ? C_GREEN : C_MUTED), 0);
        lv_obj_set_style_text_opa(l_coll_cnt[c], sel ? LV_OPA_80 : LV_OPA_COVER, 0);
    }
    lv_label_set_text_fmt(l_total, "%u of %u solved", (unsigned)total, (unsigned)PUZZLE_COUNT);

    /* collection panel */
    uint32_t sum = 0, best = 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < pc->count; i++) {
        uint32_t s = store_best_time(pc->first + i);
        if (!s) continue;
        n++;
        sum += s;
        if (!best || s < best) best = s;
    }
    lv_label_set_text(l_name, pc->name);
    if (n) {
        char b[16], a[16];
        fmt_time(b, sizeof(b), best);
        fmt_time(a, sizeof(a), sum / n);
        lv_label_set_text_fmt(l_stats, "%u / %u solved   -   fastest %s   -   average %s",
                              (unsigned)n, (unsigned)pc->count, b, a);
    } else {
        lv_label_set_text_fmt(l_stats, "0 / %u solved", (unsigned)pc->count);
    }

    /* puzzle numbers 1..count, 10 per row */
    int m = 0;
    uint16_t cnt = pc->count > MAX_PER_COLL ? MAX_PER_COLL : pc->count;
    for (int i = 0; i < cnt; i++) {
        if (i && i % 10 == 0) l_map[m++] = "\n";
        snprintf(l_labels[i], sizeof(l_labels[i]), "%d", i + 1);
        l_map[m++] = l_labels[i];
    }
    l_map[m] = "";
    lv_buttonmatrix_set_map(l_btnm, l_map);
}

/* < and > at the ends of the collection row: shown while more is off screen that way */
static void list_update_arrows(void)
{
    lv_obj_set_hidden(l_arrow_l, lv_obj_get_scroll_left(l_strip) <= 0);
    lv_obj_set_hidden(l_arrow_r, lv_obj_get_scroll_right(l_strip) <= 0);
}

static void strip_scroll_cb(lv_event_t *e)
{
    list_update_arrows();
}

static void strip_arrow_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);   /* -1 left, +1 right */
    lv_obj_scroll_by_bounded(l_strip, -dir * (SCR_W / 2), 0, LV_ANIM_ON);
}

/* scrolls the collection row so the selected one is visible */
static void list_reveal_coll(lv_anim_enable_t anim)
{
    lv_obj_update_layout(scr_list);
    lv_obj_scroll_to_view(l_coll_btn[l_coll], anim);
    list_update_arrows();
}

static void show_list(void)
{
    if (g_id != STORE_NO_GAME) {
        l_coll = puzzle_collection_of(g_id);
        store_set_last_collection((uint8_t)l_coll);
    }
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    lv_screen_load(scr_list);
}

static void coll_click_cb(lv_event_t *e)
{
    l_coll = (int)(intptr_t)lv_event_get_user_data(e);
    store_set_last_collection((uint8_t)l_coll);
    refresh_list();
    list_reveal_coll(LV_ANIM_ON);
}

static void refresh_home(void)
{
    uint16_t total = 0;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        total += store_solved_count(puzzle_collections[c].first, puzzle_collections[c].count);
    }
    lv_label_set_text_fmt(h_total, "%u of %u puzzles solved", (unsigned)total, (unsigned)PUZZLE_COUNT);

    const saved_game_t *s = store_saved_game();
    lv_obj_set_hidden(h_continue, s->id == STORE_NO_GAME);
    if (s->id != STORE_NO_GAME) {
        char name[40], t[16];
        puzzle_name(s->id, name, sizeof(name));
        fmt_time(t, sizeof(t), s->elapsed_ms / 1000);
        lv_label_set_text_fmt(h_continue_sub, "%s  -  %s", name, t);
    }
}

static void show_home(void)
{
    refresh_home();
    lv_screen_load(scr_home);
}

static void home_cb(lv_event_t *e)
{
    show_home();
}

static void puzzles_cb(lv_event_t *e)
{
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    lv_screen_load(scr_list);
}

static void settings_cb(lv_event_t *e)
{
    lv_label_set_text(st_wifi_lbl, wifi_ui_summary());
    lv_screen_load(scr_settings);
}

static void wifi_open_cb(lv_event_t *e)
{
    lv_screen_load(scr_wifi);
    wifi_ui_set_shown(true);
}

static void wifi_back_cb(lv_event_t *e)
{
    wifi_ui_set_shown(false);
    settings_cb(e);
}

/* "Asia/Dubai  UTC+4\n..." for the time zone dropdown (built once) */
static const char *tz_dropdown_options(void)
{
    static char *opts;
    if (opts) return opts;
    size_t n = 0;
    for (int i = 0; i < tz_count(); i++) n += strlen(tz_get(i)->name) + 16;
    opts = (char *)malloc(n);
    if (!opts) return "UTC";
    char *p = opts;
    for (int i = 0; i < tz_count(); i++) {
        const tz_entry_t *z = tz_get(i);
        const char *sp = strstr(z->label, "  ");          /* label: "UTC+4  Dubai" */
        int off_len = sp ? (int)(sp - z->label) : 0;
        if (strcmp(z->name, "UTC") == 0) off_len = 0;
        p += sprintf(p, "%s%s%.*s%s", z->name, off_len ? "  " : "", off_len, z->label,
                     i < tz_count() - 1 ? "\n" : "");
    }
    return opts;
}

static void tz_show_selected(void)
{
    const tz_entry_t *z = tz_get((int)lv_dropdown_get_selected(st_tz_dd));
    snprintf(st_tz_txt, sizeof(st_tz_txt), "%s", z->name);
    lv_dropdown_set_text(st_tz_dd, st_tz_txt);
}

static void tz_dd_cb(lv_event_t *e)
{
    const tz_entry_t *z = tz_get((int)lv_dropdown_get_selected(st_tz_dd));
    store_set_timezone(z->name);
    tz_apply(z->name);
    tz_show_selected();
}

/* home clock and the Settings Wi-Fi line; runs every second */
static void clock_cb(lv_timer_t *t)
{
    static char last_clock[8], last_date[64];
    lv_obj_t *act = lv_screen_active();
    if (act == scr_settings) {
        const char *w = wifi_ui_summary();
        if (strcmp(lv_label_get_text(st_wifi_lbl), w) != 0) lv_label_set_text(st_wifi_lbl, w);
        return;
    }
    if (act != scr_home) return;

    char c[8], d[64];
    time_t now = time(NULL);
    bool valid = now > 1700000000;                         /* set by NTP */
    if (valid) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(c, sizeof(c), "%H:%M", &tm);
        strftime(d, sizeof(d), "%A, %d %B", &tm);
    } else {
        wifi_status_t ws;
        wifi_mgr_get_status(&ws);
        c[0] = 0;
        snprintf(d, sizeof(d), "%s", ws.state == WIFI_ST_CONNECTED ? "Getting the time..."
                                   : "Connect Wi-Fi in Settings to show the time");
    }
    if (strcmp(c, last_clock) != 0 || lv_obj_is_hidden(h_clock) == valid) {
        strcpy(last_clock, c);
        lv_label_set_text(h_clock, c);
        lv_obj_set_hidden(h_clock, !valid);
    }
    if (strcmp(d, last_date) != 0) {
        strcpy(last_date, d);
        lv_label_set_text(h_date, d);
    }
}

/* ------------------------------------------------------------- statistics */

typedef struct {
    uint16_t solved, perfect, known;     /* known: solves with a mistake count */
    uint32_t solves, time_sum, fastest, miss_sum;
} level_stat_t;

static void add_puzzle_stat(level_stat_t *st, uint16_t id)
{
    uint32_t t = store_best_time(id);
    if (!t) return;
    st->solved++;
    st->solves += store_solve_count(id);
    st->time_sum += t;
    if (!st->fastest || t < st->fastest) st->fastest = t;
    uint8_t m = store_fewest_mistakes(id);
    if (m != STORE_MISTAKES_UNKNOWN) {
        st->known++;
        st->miss_sum += m;
        if (m == 0) st->perfect++;
    }
}

static void level_stat(int c, level_stat_t *st)
{
    memset(st, 0, sizeof(*st));
    const puzzle_collection_t *pc = &puzzle_collections[c];
    for (uint16_t i = 0; i < pc->count; i++) add_puzzle_stat(st, pc->first + i);
}

/* "1.4", or "-" when nothing is known */
static void fmt_avg_mistakes(char *buf, size_t n, const level_stat_t *st)
{
    if (!st->known) {
        snprintf(buf, n, "-");
        return;
    }
    uint32_t x10 = (st->miss_sum * 10 + st->known / 2) / st->known;
    snprintf(buf, n, "%lu.%lu", (unsigned long)(x10 / 10), (unsigned long)(x10 % 10));
}

static void fmt_stat_times(char *fast, char *avg, size_t n, const level_stat_t *st)
{
    if (st->solved) {
        fmt_time(fast, n, st->fastest);
        fmt_time(avg, n, st->time_sum / st->solved);
    } else {
        snprintf(fast, n, "-");
        snprintf(avg, n, "-");
    }
}

static void stats_summary(const level_stat_t *st, uint16_t count)
{
    char f[16], a[16], m[16];
    fmt_stat_times(f, a, sizeof(f), st);
    fmt_avg_mistakes(m, sizeof(m), st);
    lv_label_set_text_fmt(x_sum1, "Solved %u of %u   -   %u without mistakes   -   %lu solves",
                          (unsigned)st->solved, (unsigned)count, (unsigned)st->perfect,
                          (unsigned long)st->solves);
    lv_label_set_text_fmt(x_sum2, "Fastest %s   -   average %s   -   mistakes %s per puzzle", f, a, m);
}

static void table_columns(int n, const int32_t *w, const char *const *head)
{
    lv_table_set_column_count(x_table, n);
    for (int c = 0; c < n; c++) {
        lv_table_set_column_width(x_table, c, w[c]);
        lv_table_set_cell_value(x_table, 0, c, head[c]);
    }
}

static void refresh_stats(void)
{
    level_stat_t all, st;
    memset(&all, 0, sizeof(all));
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        level_stat(c, &st);
        lv_label_set_text_fmt(x_cnt_lbl[c + 1], "%u/%u", (unsigned)st.solved, (unsigned)puzzle_collections[c].count);
    }
    for (uint16_t i = 0; i < PUZZLE_COUNT; i++) add_puzzle_stat(&all, i);
    lv_label_set_text_fmt(x_cnt_lbl[0], "%u/%u", (unsigned)all.solved, (unsigned)PUZZLE_COUNT);
    for (int r = 0; r < X_ROWS; r++) {
        bool sel = r == x_sel;
        lv_obj_set_state(x_btn[r], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(x_name_lbl[r], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(x_cnt_lbl[r], sel ? lv_color_white() : C_MUTED, 0);
    }

    char f[16], a[16], m[16];
    lv_obj_scroll_to_y(x_table, 0, LV_ANIM_OFF);
    if (x_sel == 0) {
        /* one row per level */
        lv_label_set_text(x_title, "All levels");
        stats_summary(&all, PUZZLE_COUNT);
        static const int32_t w[] = {140, 66, 72, 72, 82};   /* sums to PANE_W */
        static const char *const h[] = {"Level", "Solved", "Fastest", "Average", "Mistakes"};
        lv_table_set_row_count(x_table, PUZZLE_COLLECTION_COUNT + 1);
        table_columns(5, w, h);
        for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
            level_stat(c, &st);
            fmt_stat_times(f, a, sizeof(f), &st);
            fmt_avg_mistakes(m, sizeof(m), &st);
            lv_table_set_cell_value(x_table, c + 1, 0, puzzle_collections[c].name);
            lv_table_set_cell_value_fmt(x_table, c + 1, 1, "%u", (unsigned)st.solved);
            lv_table_set_cell_value(x_table, c + 1, 2, f);
            lv_table_set_cell_value(x_table, c + 1, 3, a);
            lv_table_set_cell_value(x_table, c + 1, 4, m);
        }
        lv_obj_set_hidden(x_table, false);
        lv_obj_set_hidden(x_empty, true);
        return;
    }

    /* one row per solved puzzle of the level */
    int c = x_sel - 1;
    const puzzle_collection_t *pc = &puzzle_collections[c];
    level_stat(c, &st);
    lv_label_set_text(x_title, pc->name);
    stats_summary(&st, pc->count);
    static const int32_t w[] = {90, 130, 110, 102};     /* sums to PANE_W */
    static const char *const h[] = {"Puzzle", "Best time", "Mistakes", "Solved"};
    lv_table_set_row_count(x_table, st.solved + 1);
    table_columns(4, w, h);
    uint32_t row = 1;
    for (uint16_t i = 0; i < pc->count; i++) {
        uint16_t id = pc->first + i;
        uint32_t t = store_best_time(id);
        if (!t) continue;
        uint8_t mk = store_fewest_mistakes(id);
        fmt_time(f, sizeof(f), t);
        lv_table_set_cell_value_fmt(x_table, row, 0, "#%u", (unsigned)(i + 1));
        lv_table_set_cell_value(x_table, row, 1, f);
        if (mk == STORE_MISTAKES_UNKNOWN) lv_table_set_cell_value(x_table, row, 2, "-");
        else lv_table_set_cell_value_fmt(x_table, row, 2, "%u", (unsigned)mk);
        lv_table_set_cell_value_fmt(x_table, row, 3, "%u", (unsigned)store_solve_count(id));
        row++;
    }
    lv_obj_set_hidden(x_table, st.solved == 0);
    lv_obj_set_hidden(x_empty, st.solved != 0);
}

static void stats_cb(lv_event_t *e)
{
    refresh_stats();
    lv_screen_load(scr_stats);
}

static void stats_row_cb(lv_event_t *e)
{
    x_sel = (int)(intptr_t)lv_event_get_user_data(e);
    refresh_stats();
}

/* header row in muted text on a darker band; numbers right-aligned */
static void stats_table_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) return;
    uint32_t row = base->id1, col = base->id2;
    if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *fd = lv_draw_task_get_fill_dsc(t);
        fd->color = row == 0 ? C_CARD2 : C_CARD;
    } else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_LABEL) {
        lv_draw_label_dsc_t *ld = lv_draw_task_get_label_dsc(t);
        if (row == 0) ld->color = C_MUTED;
        if (col > 0) ld->align = LV_TEXT_ALIGN_RIGHT;
    }
}

static void sound_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(st_sound_sw, LV_STATE_CHECKED);
    sfx_set_enabled(on);
    store_set_sound_enabled(on);
    if (on) sfx_play(SFX_TOUCH);   /* the tick for this press was skipped while muted */
}

static void sleep_dd_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(st_sleep_dd);
    if (sel >= SLEEP_COUNT) return;
    s_sleep_min = SLEEP_MIN[sel];
    store_set_screen_timeout_min(s_sleep_min);
}

/* ------------------------------------------------------------- microSD format */
/* The card work runs in a short-lived task; sd_poll_cb (LVGL timer) follows
 * its state and drives the dialogs. Only one job at a time.
 *
 * The card shares the SDMMC host with the Wi-Fi link, and initialising a card
 * (400 kHz) disturbs that link badly enough that ESP-Hosted restarts the
 * board. So the card is detected at boot, before Wi-Fi starts (sd_poll_cb
 * starts Wi-Fi when that is done), and formatting then only transfers data at
 * the link's own 40 MHz. A card inserted later can only be detected while
 * Wi-Fi has not been started since boot. */
typedef enum { SD_IDLE, SD_BOOT_PROBING, SD_BOOT_DONE, SD_PROBING, SD_FOUND, SD_NO_CARD,
               SD_FORMATTING, SD_DONE, SD_FAILED } sd_state_t;
typedef enum { SD_JOB_BOOT, SD_JOB_PROBE, SD_JOB_FORMAT } sd_job_t;
static volatile sd_state_t s_sd_state = SD_IDLE;
static volatile bool s_sd_present;
static sd_state_t s_sd_seen = SD_IDLE;
static uint64_t   s_sd_bytes;
static char       s_sd_name[8];
static esp_err_t  s_sd_err;
static lv_obj_t  *s_sd_busy;          /* "Formatting..." box */

static void sd_task(void *arg)
{
    sd_job_t job = (sd_job_t)(intptr_t)arg;
    esp_err_t err;
    switch (job) {
    case SD_JOB_BOOT:
    case SD_JOB_PROBE:
        err = bsp_sdcard_probe(&s_sd_bytes, s_sd_name);
        s_sd_present = err == ESP_OK;
        s_sd_state = job == SD_JOB_BOOT ? SD_BOOT_DONE : (s_sd_present ? SD_FOUND : SD_NO_CARD);
        break;
    case SD_JOB_FORMAT:
        err = bsp_sdcard_check();                  /* still the card found earlier? */
        if (err == ESP_OK) err = bsp_sdcard_format_fat32();
        s_sd_err = err;
        s_sd_state = err == ESP_OK ? SD_DONE : SD_FAILED;
        break;
    }
    vTaskDelete(NULL);
}

static bool sd_run(sd_job_t job)
{
    s_sd_state = job == SD_JOB_BOOT ? SD_BOOT_PROBING : job == SD_JOB_PROBE ? SD_PROBING : SD_FORMATTING;
    if (xTaskCreate(sd_task, "sd_card", 8192, (void *)(intptr_t)job, 4, NULL) != pdPASS) {
        s_sd_state = SD_IDLE;
        return false;
    }
    return true;
}

static void fmt_gb(char *buf, size_t n, uint64_t bytes)
{
    uint32_t tenths = (uint32_t)((bytes + 50000000ULL) / 100000000ULL);   /* GB as printed on cards */
    snprintf(buf, n, "%lu.%lu GB", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
}

static void sd_btn_text(const char *t, bool enabled)
{
    if (!st_sd_btn) return;
    lv_label_set_text(lv_obj_get_child(st_sd_btn, 0), t);
    lv_obj_set_state(st_sd_btn, LV_STATE_DISABLED, !enabled);
}

static void sd_format_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (!sd_run(SD_JOB_FORMAT)) {
        sudoku_ui_notice("Could not start formatting (out of memory).", 5000);
        return;
    }
    s_sd_busy = make_msgbox("Formatting microSD card...", "Don't remove the card. This takes a few seconds.");
}

static void sd_format_cancel_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    s_sd_state = SD_IDLE;
}

static void sd_format_cb(lv_event_t *e)
{
    if (s_sd_state == SD_BOOT_PROBING) {
        sudoku_ui_notice("Still looking for a microSD card. Try again in a moment.", 3000);
        return;
    }
    if (s_sd_state != SD_IDLE) return;
    if (s_sd_present) {
        s_sd_state = SD_FOUND;                     /* found at boot: straight to the question */
    } else if (!wifi_mgr_radio_started()) {
        if (sd_run(SD_JOB_PROBE)) sd_btn_text("Checking...", false);
    } else {
        sudoku_ui_notice("No microSD card was found at start-up. Insert the card and restart the board "
                         "(Wi-Fi shares the card's controller, so it can't be detected while Wi-Fi runs).", 8000);
    }
}

static void sd_poll_cb(lv_timer_t *t)
{
    sd_state_t st = s_sd_state;
    if (st == s_sd_seen) return;
    s_sd_seen = st;
    char sz[24], text[200];
    switch (st) {
    case SD_BOOT_DONE:
        if (s_sd_present) {
            fmt_gb(sz, sizeof(sz), s_sd_bytes);
            printf("microSD: \"%s\", %s\n", s_sd_name, sz);
        } else {
            printf("microSD: no card\n");
        }
        wifi_mgr_start();                          /* now the card is done with the 400 kHz clock */
        s_sd_state = s_sd_seen = SD_IDLE;
        break;
    case SD_FOUND: {
        sd_btn_text("Format", true);
        fmt_gb(sz, sizeof(sz), s_sd_bytes);
        snprintf(text, sizeof(text),
                 "Card \"%s\", %s.\nEverything on it will be erased. It gets one FAT32 "
                 "partition using the whole card.", s_sd_name, sz);
        lv_obj_t *m = make_msgbox("Format microSD card?", text);
        msgbox_button(m, "Cancel", sd_format_cancel_cb, false);
        msgbox_button(m, "Format", sd_format_confirm_cb, true);
        break;
    }
    case SD_NO_CARD:
        sd_btn_text("Format", true);
        sudoku_ui_notice("No microSD card found. Insert one and try again.", 5000);
        s_sd_state = s_sd_seen = SD_IDLE;
        break;
    case SD_FORMATTING:
        sd_btn_text("Formatting...", false);
        break;
    case SD_DONE:
    case SD_FAILED:
        if (s_sd_busy) {
            lv_msgbox_close(s_sd_busy);
            s_sd_busy = NULL;
        }
        sd_btn_text("Format", true);
        if (st == SD_DONE) {
            fmt_gb(sz, sizeof(sz), s_sd_bytes);
            snprintf(text, sizeof(text), "microSD card formatted: FAT32, one %s partition.", sz);
        } else if (s_sd_err == ESP_ERR_TIMEOUT || s_sd_err == ESP_ERR_INVALID_CRC ||
                   s_sd_err == ESP_ERR_INVALID_RESPONSE) {
            s_sd_present = false;
            snprintf(text, sizeof(text), "The microSD card isn't answering. If it was swapped, restart the "
                                         "board with the card inserted.");
        } else if (s_sd_err == ESP_ERR_INVALID_SIZE) {
            snprintf(text, sizeof(text), "Format failed: the card is too small for FAT32.");
        } else {
            snprintf(text, sizeof(text), "Format failed (%s). The card may need formatting on a computer.",
                     esp_err_to_name(s_sd_err));
        }
        sudoku_ui_notice(text, 6000);
        s_sd_state = s_sd_seen = SD_IDLE;
        break;
    default:
        break;
    }
}

static void build_game(void);
static void build_list(void);
static void build_home(void);
static void build_stats(void);
static void build_settings(void);
static void build_wifi(void);

/* Switches palette and LVGL theme, then rebuilds every screen around the
 * current state (all of which lives outside the widgets) and shows the
 * rebuilt copy of the screen that was open (home at start-up). */
static void apply_theme(bool dark)
{
    s_dark = dark;
    P = dark ? &PAL_DARK : &PAL_LIGHT;
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, C_ACCENT, C_ORANGE, dark, &lv_font_montserrat_16));

    lv_obj_t *old_home = scr_home, *old_list = scr_list, *old_settings = scr_settings, *old_game = scr_game;
    lv_obj_t *old_stats = scr_stats, *old_wifi = scr_wifi;
    lv_obj_t *act = lv_screen_active();

    build_game();
    build_list();
    build_home();
    build_stats();
    build_settings();
    build_wifi();
    if (g_id != STORE_NO_GAME) {
        lv_obj_set_state(g_notes_btn, LV_STATE_CHECKED, g_notes_mode);
        lv_obj_set_hidden(g_pause_ovl, !g_paused);
        compute_conflicts();
        refresh_board();
        update_header();
    }
    refresh_list();
    refresh_home();
    refresh_stats();
    lv_obj_t *to = act && act == old_game     ? scr_game
                 : act && act == old_stats    ? scr_stats
                 : act && act == old_wifi     ? scr_wifi
                 : act && act == old_list     ? scr_list
                 : act && act == old_settings ? scr_settings
                 : scr_home;
    lv_screen_load(to);

    /* the old screens may be running this very event: delete them later */
    if (old_home) lv_obj_delete_async(old_home);
    if (old_list) lv_obj_delete_async(old_list);
    if (old_settings) lv_obj_delete_async(old_settings);
    if (old_stats) lv_obj_delete_async(old_stats);
    if (old_wifi) lv_obj_delete_async(old_wifi);
    if (old_game) lv_obj_delete_async(old_game);
}

static void theme_sw_cb(lv_event_t *e)
{
    bool dark = lv_obj_has_state(st_theme_sw, LV_STATE_CHECKED);
    store_set_dark_mode(dark);
    apply_theme(dark);
}

static void continue_cb(lv_event_t *e)
{
    uint16_t id = store_saved_game()->id;
    if (id != STORE_NO_GAME) start_game(id, true);
}

static void discard_confirm_cb(lv_event_t *e)
{
    store_clear_game();
    start_game(l_pending_id, false);
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

static void resume_other_cb(lv_event_t *e)
{
    start_game(store_saved_game()->id, true);
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

static void open_puzzle(uint16_t id)
{
    const saved_game_t *s = store_saved_game();
    if (s->id == STORE_NO_GAME || s->id == id) {
        start_game(id, s->id == id);
        return;
    }
    char cur[40], next[40], t[16], text[200];
    puzzle_name(s->id, cur, sizeof(cur));
    puzzle_name(id, next, sizeof(next));
    fmt_time(t, sizeof(t), s->elapsed_ms / 1000);
    snprintf(text, sizeof(text), "%s is in progress (%s).\nStarting %s will discard it.", cur, t, next);
    l_pending_id = id;
    lv_obj_t *m = make_msgbox("Unfinished game", text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Resume", resume_other_cb, false);
    msgbox_button(m, "Discard", discard_confirm_cb, true);
}

static void btnm_cb(lv_event_t *e)
{
    uint32_t idx = lv_buttonmatrix_get_selected_button(l_btnm);
    if (idx == LV_BUTTONMATRIX_BUTTON_NONE) return;
    open_puzzle(puzzle_collections[l_coll].first + (uint16_t)idx);
}

/* Left-hand column card shared by the list and statistics screens. */
static lv_obj_t *make_coll_column(lv_obj_t *parent)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_pos(list, 12, 76);
    lv_obj_set_size(list, COLL_W, SCR_W - 76 - 12);
    lv_obj_set_style_bg_color(list, C_CARD, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 12, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    return list;
}

/* One row of that column: name on the left, a small count on the right. */
static lv_obj_t *make_coll_btn(lv_obj_t *list, const char *name, lv_obj_t **name_lbl, lv_obj_t **cnt_lbl,
                               lv_event_cb_t cb, int user)
{
    lv_obj_t *b = lv_button_create(list);
    lv_obj_set_size(b, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    *name_lbl = make_label(b, &lv_font_montserrat_16, C_TEXT);
    lv_label_set_text(*name_lbl, name);
    lv_obj_align(*name_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    *cnt_lbl = make_label(b, &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(*cnt_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)user);
    return b;
}

static void build_list(void)
{
    scr_list = make_screen();

    make_btn(scr_list, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_list, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Puzzles");
    lv_obj_set_pos(t, 150, 20);
    l_total = make_label(scr_list, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(l_total, LV_ALIGN_TOP_RIGHT, -16, 28);

    /* collections: one row of chips (name + solved count), scrolls sideways */
    lv_obj_t *strip = lv_obj_create(scr_list);
    l_strip = strip;
    lv_obj_remove_style_all(strip);
    /* between the < > arrows (8 + 40 + 4 on each side), so chips never pass under them */
    lv_obj_set_pos(strip, 52, 72);
    lv_obj_set_size(strip, SCR_W - 2 * 52, 62);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(strip, 0, 0);
    lv_obj_set_style_pad_column(strip, 8, 0);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        lv_obj_t *b = lv_button_create(strip);
        lv_obj_set_size(b, LV_SIZE_CONTENT, 58);
        lv_obj_set_style_bg_color(b, C_CARD, 0);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_pad_hor(b, 16, 0);
        lv_obj_set_style_pad_ver(b, 0, 0);
        lv_obj_set_style_pad_row(b, 1, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        l_coll_name[c] = make_label(b, &lv_font_montserrat_16, C_TEXT);
        lv_label_set_text(l_coll_name[c], puzzle_collections[c].name);
        l_coll_cnt[c] = make_label(b, &lv_font_montserrat_14, C_MUTED);
        lv_obj_add_event_cb(b, coll_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)c);
        l_coll_btn[c] = b;
    }

    lv_obj_add_event_cb(strip, strip_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(strip, strip_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *a = make_btn(scr_list, i ? SCR_W - 8 - 40 : 8, 72, 40, 58,
                               i ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT, &lv_font_montserrat_20,
                               strip_arrow_cb, (void *)(intptr_t)(i ? 1 : -1));
        lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(a, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(lv_obj_get_child(a, 0), C_ACCENT, 0);
        if (i) l_arrow_r = a;
        else l_arrow_l = a;
    }

    /* puzzles of the selected collection: full width */
    l_name = make_label(scr_list, &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(l_name, 16, 144);
    l_stats = make_label(scr_list, &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(l_stats, LV_ALIGN_TOP_RIGHT, -16, 150);

    l_btnm = lv_buttonmatrix_create(scr_list);
    lv_obj_set_pos(l_btnm, 12, 180);
    lv_obj_set_size(l_btnm, SCR_W - 24, SCR_W - 180 - 24);   /* cells about 65 x 46 */
    lv_obj_set_style_bg_opa(l_btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l_btnm, 0, 0);
    lv_obj_set_style_pad_all(l_btnm, 0, 0);
    lv_obj_set_style_pad_gap(l_btnm, 5, 0);
    lv_obj_set_style_bg_color(l_btnm, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(l_btnm, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(l_btnm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(l_btnm, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(l_btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(l_btnm, 6, LV_PART_ITEMS);
    lv_obj_set_send_draw_task_events(l_btnm, true);
    lv_obj_add_event_cb(l_btnm, list_btnm_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(l_btnm, btnm_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* attribution */
    lv_obj_t *src = make_label(scr_list, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(src, "Puzzles: opensudoku.gitlab.io");
    lv_obj_align(src, LV_ALIGN_BOTTOM_RIGHT, -14, -2);
}

/* big menu button: icon + title, optional muted subtitle below */
static lv_obj_t *home_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **sub)
{
    lv_obj_t *b = make_btn(parent, 0, 0, 440, 92, NULL, NULL, cb, NULL);
    lv_obj_t *l = make_label(b, &lv_font_montserrat_24, C_TEXT);
    lv_label_set_text(l, text);
    if (sub) {
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -13);
        *sub = make_label(b, &lv_font_montserrat_16, C_MUTED);
        lv_obj_align(*sub, LV_ALIGN_CENTER, 0, 19);
    } else {
        lv_obj_center(l);
    }
    return b;
}

static void build_home(void)
{
    scr_home = make_screen();

    h_clock = make_label(scr_home, &font_timer_36, C_TEXT);
    lv_obj_align(h_clock, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_hidden(h_clock, true);
    h_date = make_label(scr_home, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(h_date, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *col = lv_obj_create(scr_home);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 440, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 18, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 40);   /* clear of the clock */

    lv_obj_t *t = make_label(col, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Sudoku");
    h_total = make_label(col, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_style_margin_bottom(h_total, 22, 0);

    /* hidden when there is no game in progress; the flex column closes up */
    h_continue = home_btn(col, LV_SYMBOL_PLAY "  Continue", continue_cb, &h_continue_sub);
    lv_obj_set_style_bg_color(h_continue, C_LIST_ACTIVE, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_continue, 0), lv_color_white(), 0);
    lv_obj_set_style_text_color(h_continue_sub, lv_color_white(), 0);
    lv_obj_set_style_text_opa(h_continue_sub, LV_OPA_80, 0);

    home_btn(col, LV_SYMBOL_LIST "  Puzzles", puzzles_cb, NULL);
    home_btn(col, LV_SYMBOL_BARS "  Statistics", stats_cb, NULL);
    home_btn(col, LV_SYMBOL_SETTINGS "  Settings", settings_cb, NULL);

    lv_obj_t *src = make_label(scr_home, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(src, "Puzzles: opensudoku.gitlab.io");
    lv_obj_align(src, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void build_stats(void)
{
    scr_stats = make_screen();

    make_btn(scr_stats, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_stats, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Statistics");
    lv_obj_set_pos(t, 150, 20);

    lv_obj_t *list = make_coll_column(scr_stats);
    x_btn[0] = make_coll_btn(list, "All levels", &x_name_lbl[0], &x_cnt_lbl[0], stats_row_cb, 0);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        x_btn[c + 1] = make_coll_btn(list, puzzle_collections[c].name, &x_name_lbl[c + 1], &x_cnt_lbl[c + 1],
                                     stats_row_cb, c + 1);
    }

    int px = PANE_X, pw = PANE_W;
    x_title = make_label(scr_stats, &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(x_title, px + 4, 80);
    x_sum1 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum1, px + 4, 110);
    x_sum2 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum2, px + 4, 130);

    x_table = lv_table_create(scr_stats);
    lv_obj_set_pos(x_table, px, 158);
    lv_obj_set_size(x_table, pw, SCR_W - 158 - 48);
    lv_obj_set_style_bg_color(x_table, C_CARD, 0);
    lv_obj_set_style_border_width(x_table, 0, 0);
    lv_obj_set_style_radius(x_table, 12, 0);
    lv_obj_set_style_pad_all(x_table, 0, 0);
    lv_obj_set_style_clip_corner(x_table, true, 0);
    lv_obj_set_style_text_font(x_table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_text_color(x_table, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(x_table, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(x_table, C_CARD, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(x_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(x_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_color(x_table, C_BG, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(x_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(x_table, 9, LV_PART_ITEMS);
    lv_obj_set_send_draw_task_events(x_table, true);
    lv_obj_add_event_cb(x_table, stats_table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);

    x_empty = make_label(scr_stats, &lv_font_montserrat_18, C_MUTED);
    lv_label_set_text(x_empty, "No puzzles solved in this level yet.");
    lv_obj_set_pos(x_empty, px + 4, 180);

    lv_obj_t *note = make_label(scr_stats, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(note, "Mistake: a placed digit that broke a rule. Per puzzle: the fewest in any solve.\n"
                            "\"-\": solved before mistakes were counted.");
    lv_obj_set_width(note, pw - 4);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(note, px + 4, SCR_W - 40);
}

/* one settings row: a card with a title + hint on the left; returns the card
 * so the caller can put a control on its right */
static lv_obj_t *settings_row(lv_obj_t *parent, const char *title, const char *hint)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, LV_PCT(100), 84);
    lv_obj_set_style_bg_color(r, C_CARD, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 12, 0);
    lv_obj_set_style_pad_hor(r, 22, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_t *l = make_label(r, &lv_font_montserrat_20, C_TEXT);
    lv_label_set_text(l, title);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, -12);
    lv_obj_t *h = make_label(r, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(h, hint);
    lv_obj_align(h, LV_ALIGN_LEFT_MID, 0, 16);
    return r;
}

static lv_obj_t *settings_switch(lv_obj_t *row, bool on, lv_event_cb_t cb)
{
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 84, 44);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_state(sw, LV_STATE_CHECKED, on);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static void build_settings(void)
{
    scr_settings = make_screen();

    make_btn(scr_settings, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_settings, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Settings");
    lv_obj_set_pos(t, 150, 20);

    lv_obj_t *col = lv_obj_create(scr_settings);
    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, 40, 84);
    lv_obj_set_size(col, SCR_W - 80, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 12, 0);

    lv_obj_t *r = settings_row(col, "Dark mode", "Light text on a dark background");
    st_theme_sw = settings_switch(r, s_dark, theme_sw_cb);

    r = settings_row(col, "Sound effects", "Taps, mistakes, finished digits, solved");
    st_sound_sw = settings_switch(r, sfx_enabled(), sound_sw_cb);

    r = settings_row(col, "Screen off after", "Without a touch. The game pauses; tap to wake.");
    st_sleep_dd = lv_dropdown_create(r);
    lv_obj_set_size(st_sleep_dd, 150, 52);
    lv_obj_align(st_sleep_dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options_static(st_sleep_dd, SLEEP_OPTIONS);
    lv_obj_set_style_text_font(st_sleep_dd, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(st_sleep_dd, C_TEXT, 0);
    lv_obj_set_style_bg_color(st_sleep_dd, C_CARD2, 0);
    lv_obj_set_style_border_width(st_sleep_dd, 0, 0);
    lv_obj_set_style_shadow_width(st_sleep_dd, 0, 0);
    lv_obj_set_style_radius(st_sleep_dd, 8, 0);
    lv_obj_set_style_pad_hor(st_sleep_dd, 16, 0);
    lv_obj_set_style_pad_ver(st_sleep_dd, 15, 0);
    lv_obj_t *dl = lv_dropdown_get_list(st_sleep_dd);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(dl, C_TEXT, 0);
    lv_obj_set_style_text_line_space(dl, 22, 0);   /* finger-sized rows */
    lv_obj_set_style_bg_color(dl, C_CARD2, 0);
    lv_obj_set_style_border_width(dl, 0, 0);
    lv_obj_set_style_radius(dl, 8, 0);
    lv_obj_set_style_max_height(dl, 480, 0);
    lv_obj_set_style_bg_color(dl, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(dl, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
    uint32_t sel = 0;
    while (sel < SLEEP_COUNT - 1 && SLEEP_MIN[sel] != s_sleep_min) sel++;
    lv_dropdown_set_selected(st_sleep_dd, sel);
    lv_obj_add_event_cb(st_sleep_dd, sleep_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Wi-Fi: the whole card opens the Wi-Fi screen */
    r = settings_row(col, "Wi-Fi", "For the clock (time from the internet)");
    lv_obj_set_clickable(r, true);
    lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, wifi_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *chev = make_label(r, &lv_font_montserrat_20, C_MUTED);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    st_wifi_lbl = make_label(r, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(st_wifi_lbl, 220);
    lv_obj_set_style_text_align(st_wifi_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(st_wifi_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(st_wifi_lbl, wifi_ui_summary());
    lv_obj_align(st_wifi_lbl, LV_ALIGN_RIGHT_MID, -32, 0);

    /* time zone: IANA names; opens upwards (bottom of the page) */
    r = settings_row(col, "Time zone", "For the clock on the home screen");
    st_tz_dd = lv_dropdown_create(r);
    lv_obj_set_size(st_tz_dd, 330, 52);
    lv_obj_align(st_tz_dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options_static(st_tz_dd, tz_dropdown_options());
    lv_dropdown_set_dir(st_tz_dd, LV_DIR_TOP);
    lv_dropdown_set_symbol(st_tz_dd, LV_SYMBOL_UP);
    lv_obj_set_style_text_font(st_tz_dd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st_tz_dd, C_TEXT, 0);
    lv_obj_set_style_bg_color(st_tz_dd, C_CARD2, 0);
    lv_obj_set_style_border_width(st_tz_dd, 0, 0);
    lv_obj_set_style_shadow_width(st_tz_dd, 0, 0);
    lv_obj_set_style_radius(st_tz_dd, 8, 0);
    lv_obj_set_style_pad_hor(st_tz_dd, 14, 0);
    lv_obj_set_style_pad_ver(st_tz_dd, 16, 0);
    dl = lv_dropdown_get_list(st_tz_dd);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dl, C_TEXT, 0);
    lv_obj_set_style_text_line_space(dl, 22, 0);
    lv_obj_set_style_bg_color(dl, C_CARD2, 0);
    lv_obj_set_style_border_width(dl, 0, 0);
    lv_obj_set_style_radius(dl, 8, 0);
    lv_obj_set_style_max_height(dl, 520, 0);
    lv_obj_set_style_bg_color(dl, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(dl, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_dropdown_set_selected(st_tz_dd, (uint32_t)tz_find(store_timezone()));
    tz_show_selected();
    lv_obj_add_event_cb(st_tz_dd, tz_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    r = settings_row(col, "microSD card", "Erase it: FAT32, one partition, whole card");
    st_sd_btn = make_btn(r, 0, 0, 160, 52, "Format", &lv_font_montserrat_18, sd_format_cb, NULL);
    lv_obj_set_style_bg_color(st_sd_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_sd_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_sd_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    if (s_sd_state == SD_FORMATTING) sd_btn_text("Formatting...", false);
}

static void build_wifi(void)
{
    scr_wifi = make_screen();
    wifi_ui_create(scr_wifi, wifi_back_cb);
}

/* ============================================================= start */

static void notice_del_cb(lv_timer_t *t)
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

/* ============================================================= screen saver */

static lv_obj_t *s_saver;   /* black cover on the system layer while the screen is off */

static void saver_wake_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        bsp_display_brightness_set(100);
    } else if (s_saver) {
        /* removed only once the finger is up, so the waking tap reaches nothing below */
        lv_obj_delete_async(s_saver);
        s_saver = NULL;
    }
}

static void saver_sleep(void)
{
    if (lv_screen_active() == scr_game && g_id != STORE_NO_GAME && !g_solved && !g_paused) {
        set_paused(true);   /* stops the clock, hides the board, saves the game */
    }
    s_saver = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_saver);
    lv_obj_set_size(s_saver, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_saver, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_saver, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_saver, true);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_refr_now(NULL);                  /* panel shows black before the light goes off */
    bsp_display_brightness_set(0);
}

static void saver_cb(lv_timer_t *t)
{
    if (!s_saver && s_sleep_min &&
        lv_display_get_inactive_time(NULL) >= (uint32_t)s_sleep_min * 60 * 1000) saver_sleep();
}

static void touch_sound_cb(lv_event_t *e)
{
    if (s_saver) return;   /* waking tap is silent */
    sfx_play(SFX_TOUCH);
}

esp_err_t sudoku_ui_start(void)
{
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(NULL);
#endif

    sfx_set_enabled(store_sound_enabled());
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        lv_indev_add_event_cb(in, touch_sound_cb, LV_EVENT_PRESSED, NULL);
    }

    tz_apply(store_timezone());
    /* microSD detection first, then Wi-Fi (sd_poll_cb starts it; see the
     * microSD section). Wi-Fi reconnects to the saved network if left on. */
    if (!sd_run(SD_JOB_BOOT)) wifi_mgr_start();

    l_coll = store_last_collection();
    s_sleep_min = store_screen_timeout_min();
    apply_theme(store_dark_mode());

    g_last_tick = lv_tick_get();
    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(saver_cb, 1000, NULL);
    lv_timer_create(clock_cb, 1000, NULL);
    lv_timer_create(sd_poll_cb, 200, NULL);
    clock_cb(NULL);
    return ESP_OK;
}

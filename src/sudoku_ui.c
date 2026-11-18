/*
 * Sudoku UI for a 720x720 touch screen (LVGL 9).
 *
 * Screens:
 *   - home: Players, Continue (the game in progress), Puzzles, Statistics,
 *     Gameplays and Settings, in a column or, when they don't fit, a grid;
 *     a power button (top right) turns the board off (deep sleep until RESET).
 *   - gameplays: every recorded solve, newest first, tap one to replay it.
 *   - list: a scrolling row of collections at the top, a 10x10 grid of
 *     puzzles below it.
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
 *   - players: who is playing; switch, add or rename (statistics and the
 *     game in progress are per player, in sudoku.db).
 *   - settings: dark mode, sound effects, screen timeout, Wi-Fi, time zone,
 *     formatting the microSD card (FAT32, one partition), and lending it to a
 *     computer as a USB drive (to back up or restore sudoku.db).
 *   - wifi: on/off, nearby networks, password keyboard (wifi_ui.c).
 *
 * Clock: with Wi-Fi connected the time comes from NTP (wifi_mgr.cpp starts
 * SNTP) and the home screen shows it in the chosen time zone.
 *   - game: the 9x9 board, a 1..9 number pad on the right, a timer at the
 *     top, and Undo / Erase / Notes / Restart / Fill at the bottom. Puzzles
 *     made on the board can also be X-Sudoku (the diagonals are tinted and
 *     count as units) or a 6x6 Mini board (2x3 boxes, a 1..6 pad); the board
 *     and pad are laid out for the puzzle's variant (generator.h) when it opens. Fill puts
 *     the solution's digit in the selected cell and counts as a hint. Conflicting
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
#include "stats_db.h"
#include "usb_msc.h"
#include "generator.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "video_enc.h"
#include <sys/stat.h>
#include "moves.h"
#include "esp_heap_caps.h"

/* "Screen off" dropdown choices, in minutes (0 = never) */
static const uint8_t SLEEP_MIN[] = {1, 2, 5, 10, 15, 30, 0};
#define SLEEP_OPTIONS "1 min\n2 min\n5 min\n10 min\n15 min\n30 min\nNever"
#define SLEEP_COUNT   (sizeof(SLEEP_MIN) / sizeof(SLEEP_MIN[0]))

LV_FONT_DECLARE(font_digits_44)   /* " 0-9", grid digits */
LV_FONT_DECLARE(font_timer_36)    /* " 0-9:", game timer */
LV_FONT_DECLARE(font_icons_24)    /* the two icons below + Montserrat 24 as fallback */
LV_FONT_DECLARE(font_icons_20)    /* the same at 20 px + Montserrat 20 */
#define SYMBOL_USER     "\xEF\x80\x87"   /* FontAwesome U+F007 */
#define SYMBOL_CALENDAR "\xEF\x81\xB3"   /* FontAwesome U+F073 */
#define SYMBOL_INFO     "\xEF\x81\x9A"   /* FontAwesome U+F05A */

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
#define GRID_X      8
#define GRID_Y      64
#define GRID_SIZE   604     /* 9x9: 4 + 3 * (3*64 + 2*2) + 2*4 + 4 (board_layout) */
#define PAD_X       (GRID_X + GRID_SIZE + 8)
#define PAD_W       (SCR_W - PAD_X - 8)
#define PAD_H       62
#define PAD_GAP     5
#define BOT_Y       (GRID_Y + GRID_SIZE + 6)
#define BOT_H       (SCR_W - BOT_Y - 6)

#define SAVE_DELAY_MS    80      /* solve bookkeeping once the Solved! dialog is up */
#define UNDO_MAX         128
#define MAX_PER_COLL     100

static const char *const DIGITS[10] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

/* ------------------------------------------------------------- game state */
typedef struct {
    uint8_t  val[81];
    uint16_t notes[81];
} board_t;

static uint16_t g_id = STORE_NO_GAME;
static int      g_var = VAR_CLASSIC;     /* the puzzle's variant (generator.h) */
static const sudoku_geom_t *g_geo;       /* and its geometry: units, peers, size */
static uint8_t  g_given[81];
static board_t  g_b;
static bool     g_conflict[81];
static int      g_sel = -1;
static bool     g_notes_mode;
static bool     g_solved;
static bool     g_paused;
static uint32_t g_elapsed_ms;
static uint16_t g_mistakes;      /* digits placed that broke a rule (undo doesn't remove them) */
static uint16_t g_hints;         /* cells filled with Fill (undo doesn't remove them either) */
static uint8_t  g_sol[81];       /* the solution, for Fill */
static game_move_t *g_moves;     /* move log of this attempt (moves.h), PSRAM, GAME_MOVES_MAX */
static uint8_t  h_mark[81];      /* hint highlight: 0, 1 = where to look, 2 = the cell */
static int      h_step;          /* 0 = no hint open, 1 = nudge shown, 2 = reason shown */
static sudoku_step_t h_info;
static lv_obj_t *h_card, *h_text, *h_act;
static void hint_drop(void);
static int      g_nmoves;
static bool     g_sol_ok;
static int64_t  g_started_at;    /* Unix time the puzzle was opened/restarted, 0 = not known yet */
static uint32_t g_started_tick;  /* lv_tick_get() then, to work it out once NTP sets the clock;
                                    0 = not this session (resumed with no known start) */
static uint32_t g_last_tick;

static board_t *g_undo;          /* UNDO_MAX boards, PSRAM (31 KB) */
static int      g_undo_head, g_undo_len;

/* ------------------------------------------------------------- widgets */
static lv_obj_t *scr_home, *scr_list, *scr_stats, *scr_settings, *scr_wifi, *scr_profile, *scr_history, *scr_game;
static lv_obj_t *scr_plays;               /* Gameplays: recorded solves to replay */

/* home screen */
#define HOME_COL_W      440      /* menu button, one column */
#define HOME_GRID_W     600      /* menu width as a grid (two per row) */
#define HOME_BTN_H      80
#define HOME_GAP        14
#define HOME_MENU_MAX_H 470      /* room under the clock, title and count */
static lv_obj_t *h_menu;
static int h_menu_grid;                   /* 1 = grid, 0 = column, -1 = not laid out */
static lv_obj_t *h_total, *h_continue, *h_continue_sub, *h_clock, *h_date, *h_player, *h_daily_sub, *h_daily;

/* history screen */
static lv_obj_t *hz_cal, *hz_title, *hz_list;
static lv_calendar_date_t hz_marked[31];  /* highlighted days (the calendar keeps the pointer) */
static int hz_year, hz_month;             /* month asked for / shown, 0 = not yet */
static uint8_t hz_count[31];              /* games per day of the highlighted month (badges) */
static lv_calendar_date_t hz_sel;         /* the day tapped (its list is shown), year 0 = none */
static bool hz_latest_pending;            /* asked for "the latest month"; show what comes back */

/* players screen */
static lv_obj_t *p_list, *p_add, *p_note;
static stats_player_t p_players[STATS_MAX_PLAYERS];
static int p_count = -1;                 /* last list from sudoku.db, -1 = none yet */

/* statistics screen: row 0 of the left column is "All levels" */
#define X_ROWS (PUZZLE_COLLECTION_COUNT + 1)
static lv_obj_t *x_btn[X_ROWS], *x_name_lbl[X_ROWS], *x_cnt_lbl[X_ROWS];
static lv_obj_t *x_title, *x_sum1, *x_sum2, *x_table, *x_empty, *x_player_dd;
static bool      x_from_db;               /* what the info popup says the numbers come from */
static char      x_player_txt[STORE_NAME_LEN + 8];    /* closed dropdown: icon + name */
static int       x_sel;          /* 0 = all levels, 1.. = collection x_sel-1 */

/* settings screen */
static lv_obj_t *st_theme_sw, *st_sound_sw, *st_sleep_dd, *st_wifi_lbl, *st_tz_dd;
static lv_obj_t *st_bright_sl, *st_bright_lbl, *st_vol_sl, *st_vol_lbl;
static char      st_tz_txt[48];     /* closed dropdown shows just the IANA name */
static lv_obj_t *st_sd_btn, *st_sd_hint;
static lv_obj_t *u_ovl;                   /* "USB drive" panel while the computer has the card */
static lv_obj_t *st_usb_btn, *st_usb_hint;
static bool u_reloading;                  /* card back / reset: waiting for sudoku.db to (re)open */
static bool u_reload_home;                /* ... then show Home (factory reset) */

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
static lv_obj_t *g_grid, *g_boxes[9], *g_cells[81], *g_cell_lbl[81];
static lv_obj_t *g_pad[9], *g_pad_cnt[9];
static int       g_layout_var = -1;     /* variant the board and pad are laid out for */
static lv_obj_t *g_title, *g_best, *g_timer, *g_timer_icon, *g_status, *g_notes_btn;
static lv_obj_t *g_pause_ovl, *g_win_ovl, *g_win_time, *g_win_best, *g_win_miss, *g_win_hint;
static lv_obj_t *g_hint_cnt;     /* hints used, on the Fill button */

static void show_list(void);
static void open_puzzle(uint16_t id);
static void render_players(void);
static void usb_finish(void);
static void usb_btn_update(void);
static void history_poll(void);
static void history_cb(lv_event_t *e);
static void apply_loaded_prefs(void);
static void win_replay_cb(lv_event_t *e);
static void plays_poll(void);
static void apply_theme(bool dark);

/* ============================================================= helpers */

static void fmt_time(char *buf, size_t n, uint32_t s)
{
    if (s >= 3600) {
        snprintf(buf, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    } else {
        snprintf(buf, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

/* ------------------------------------------------------------- generated puzzles */
/* The puzzles made on the board live in sudoku.db (stats_db_gen_*); the last
 * few are cached here, and without a card that's the only place they are. */
#define GEN_CACHE 24
static struct {
    uint16_t id;                        /* 0 = free slot */
    uint8_t variant;                    /* sudoku_variant_t */
    uint8_t level;
    uint8_t givens[81];
    char day[12];                       /* daily puzzle's date, "" otherwise */
} s_gen_cache[GEN_CACHE];
static int s_gen_cache_next;
static uint16_t s_gen_ram_next = PUZZLE_GEN_RAM_BASE;

static void gen_cache_put(uint16_t id, int variant, int level, const uint8_t givens[81], const char *day)
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id) return;
    }
    s_gen_cache[s_gen_cache_next].id = id;
    s_gen_cache[s_gen_cache_next].variant = (uint8_t)variant;
    s_gen_cache[s_gen_cache_next].level = (uint8_t)level;
    memcpy(s_gen_cache[s_gen_cache_next].givens, givens, 81);
    snprintf(s_gen_cache[s_gen_cache_next].day, sizeof(s_gen_cache[0].day), "%s", day ? day : "");
    s_gen_cache_next = (s_gen_cache_next + 1) % GEN_CACHE;
}

/* givens, variant, level and daily date of a generated puzzle (any may be NULL); false if unknown */
static bool gen_lookup_day(uint16_t id, uint8_t *givens, int *variant, int *level, char day[12])
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id) {
            if (givens) memcpy(givens, s_gen_cache[i].givens, 81);
            if (variant) *variant = s_gen_cache[i].variant;
            if (level) *level = s_gen_cache[i].level;
            if (day) snprintf(day, 12, "%s", s_gen_cache[i].day);
            return true;
        }
    }
    if (id >= PUZZLE_GEN_RAM_BASE) return false;          /* RAM-only, and gone */
    uint8_t g[81];
    int lv, var = VAR_CLASSIC;
    char d[12];
    if (!stats_db_gen_get(id - PUZZLE_GEN_BASE, g, &var, &lv, d)) return false;
    if (var < 0 || var >= VAR_COUNT) var = VAR_CLASSIC;
    gen_cache_put(id, var, lv, g, d);
    if (givens) memcpy(givens, g, 81);
    if (variant) *variant = var;
    if (level) *level = lv;
    if (day) snprintf(day, 12, "%s", d);
    return true;
}

static bool gen_lookup(uint16_t id, uint8_t *givens, int *variant, int *level)
{
    return gen_lookup_day(id, givens, variant, level, NULL);
}

/* variant of any puzzle: the built-in ones are classic */
static int puzzle_variant(uint16_t id)
{
    int v = VAR_CLASSIC;
    if (puzzle_is_generated(id)) gen_lookup(id, NULL, &v, NULL);
    return v;
}

/* the givens of any puzzle, built-in or generated */
static bool puzzle_get(uint16_t id, uint8_t cells[81])
{
    if (!puzzle_is_generated(id)) {
        if (id >= PUZZLE_COUNT) return false;
        puzzle_load(id, cells);
        return true;
    }
    return gen_lookup(id, cells, NULL, NULL);
}

static void puzzle_name(uint16_t id, char *buf, size_t n)
{
    if (puzzle_is_generated(id)) {
        int lv = -1, var = VAR_CLASSIC;
        char day[12] = "";
        gen_lookup_day(id, NULL, &var, &lv, day);
        if (day[0]) {                                     /* "Daily Hard, 18 Nov" */
            struct tm tm = {0};
            char when[16] = "";
            if (sscanf(day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                strftime(when, sizeof(when), "%d %b", &tm);
            }
            snprintf(buf, n, "Daily %s, %s", lv >= 0 && lv < GEN_LEVELS ? GEN_LEVEL_NAMES[lv] : "", when);
            return;
        }
        unsigned num = id >= PUZZLE_GEN_RAM_BASE ? (unsigned)(id - PUZZLE_GEN_RAM_BASE + 1)
                                                 : (unsigned)(id - PUZZLE_GEN_BASE);
        const char *kind = var == VAR_X ? "X-Sudoku" : (var == VAR_MINI ? "Mini" : "Generated");
        snprintf(buf, n, "%s %s #%u", kind, lv >= 0 && lv < GEN_LEVELS ? GEN_LEVEL_NAMES[lv] : "", num);
        return;
    }
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

/* ------------------------------------------------------------- justified text */
/*
 * LVGL labels can't justify text, so this widget lays the words out itself:
 * every line of a paragraph but the last is spread to the full width (left
 * and right edges flush); the last line, and a line of one word, stay left.
 * '\n' ends a paragraph. Font, colour and line spacing come from the styles
 * (inherited, like a label). Words are measured glyph by glyph (UTF-8).
 * Height follows the text (LV_SIZE_CONTENT). Every dialog (make_msgbox) uses it.
 */
typedef struct {
    char *text;
    int32_t width;                      /* content width the height was worked out for */
} just_t;

#define JUST_MAX_WORDS 64               /* per line */

/* the UTF-8 character at s[*i] (moves *i past it); 0 at n */
static uint32_t just_utf8_next(const char *s, int n, int *i)
{
    if (*i >= n) return 0;
    uint8_t c = (uint8_t)s[(*i)++];
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = more == 3 ? (c & 0x07u) : more == 2 ? (c & 0x0Fu) : more == 1 ? (c & 0x1Fu) : c;
    while (more-- > 0 && *i < n) cp = (cp << 6) | ((uint8_t)s[(*i)++] & 0x3Fu);
    return cp;
}

static int32_t just_word_w(const lv_font_t *f, const char *s, int n, int32_t letter_space)
{
    int32_t w = 0;
    int i = 0;
    uint32_t cp = just_utf8_next(s, n, &i);
    while (cp) {
        uint32_t next = just_utf8_next(s, n, &i);
        w += lv_font_get_glyph_width(f, cp, next) + letter_space;
        cp = next;
    }
    return w;
}

/* lays the text out in width w; with layer, draws it from co's top left. Returns the line count. */
static int just_run(lv_obj_t *obj, int32_t w, lv_layer_t *layer, const lv_area_t *co)
{
    const just_t *j = lv_obj_get_user_data(obj);
    if (!j || !j->text) return 0;
    const lv_font_t *f = lv_obj_get_style_text_font(obj, 0);
    int32_t ls = lv_obj_get_style_text_letter_space(obj, 0);
    int32_t line_h = lv_font_get_line_height(f) + lv_obj_get_style_text_line_space(obj, 0);
    int32_t space_w = lv_font_get_glyph_width(f, ' ', 0) + ls;
    lv_draw_label_dsc_t d;
    if (layer) {
        lv_draw_label_dsc_init(&d);
        d.font = f;
        d.color = lv_obj_get_style_text_color(obj, 0);
        d.opa = lv_obj_get_style_text_opa(obj, 0);
        d.letter_space = ls;
    }
    const char *p = j->text;
    int lines = 0;
    while (*p) {
        const char *ws[JUST_MAX_WORDS];
        int32_t ww[JUST_MAX_WORDS], sum = 0;
        int wl[JUST_MAX_WORDS], nw = 0;
        while (*p == ' ') p++;
        while (*p && *p != '\n') {                  /* words while they fit */
            const char *e = p;
            while (*e && *e != ' ' && *e != '\n') e++;
            int32_t wd = just_word_w(f, p, (int)(e - p), ls);
            if (nw && (sum + nw * space_w + wd > w || nw == JUST_MAX_WORDS)) break;
            ws[nw] = p;
            wl[nw] = (int)(e - p);
            ww[nw] = wd;
            sum += wd;
            nw++;
            p = e;
            while (*p == ' ') p++;
        }
        bool last = !*p || *p == '\n';
        if (layer) {
            int32_t x = 0, spare = w - sum;
            for (int k = 0; k < nw; k++) {
                int32_t gap = last || nw < 2 ? k * space_w : spare * k / (nw - 1);
                lv_area_t a = {.x1 = co->x1 + x + gap, .y1 = co->y1 + lines * line_h};
                a.x2 = a.x1 + ww[k] + 8;            /* slack: the word never wraps */
                a.y2 = a.y1 + line_h - 1;
                d.text = ws[k];
                d.text_length = (uint32_t)wl[k];
                lv_draw_label(layer, &d, &a);
                x += ww[k];
            }
        }
        lines++;
        if (*p == '\n') p++;
    }
    return lines;
}

static void just_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    just_t *j = lv_obj_get_user_data(obj);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_GET_SELF_SIZE: {
        lv_point_t *pt = lv_event_get_param(e);
        const lv_font_t *f = lv_obj_get_style_text_font(obj, 0);
        int32_t lsp = lv_obj_get_style_text_line_space(obj, 0);
        int n = just_run(obj, lv_obj_get_content_width(obj), NULL, NULL);
        int32_t h = n ? n * (lv_font_get_line_height(f) + lsp) - lsp : 0;
        if (pt->y < h) pt->y = h;
        break;
    }
    case LV_EVENT_SIZE_CHANGED:                     /* a new width: new line breaks, new height */
        if (j && j->width != lv_obj_get_content_width(obj)) {
            j->width = lv_obj_get_content_width(obj);
            lv_obj_refresh_self_size(obj);
        }
        break;
    case LV_EVENT_DRAW_MAIN_END: {
        lv_area_t co;
        lv_obj_get_content_coords(obj, &co);
        just_run(obj, lv_area_get_width(&co), lv_event_get_layer(e), &co);
        break;
    }
    case LV_EVENT_DELETE:
        if (j) {
            lv_free(j->text);
            lv_free(j);
        }
        lv_obj_set_user_data(obj, NULL);
        break;
    default:
        break;
    }
}

static void just_set_text(lv_obj_t *obj, const char *text)
{
    just_t *j = lv_obj_get_user_data(obj);
    if (!j) return;
    lv_free(j->text);
    j->text = lv_strdup(text ? text : "");
    lv_obj_refresh_self_size(obj);
    lv_obj_invalidate(obj);
}

/* justified text, full width of its parent (font and colour as for make_label) */
static lv_obj_t *make_just_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    if (font) lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    just_t *j = lv_malloc_zeroed(sizeof(just_t));
    lv_obj_set_user_data(obj, j);
    lv_obj_add_event_cb(obj, just_event_cb, LV_EVENT_ALL, NULL);
    just_set_text(obj, text);
    return obj;
}

/* ============================================================= sudoku rules */

/* cells a and b share a unit of the puzzle being played (row, column, box, diagonal) */
static bool is_peer(int a, int b)
{
    return sudoku_geom_peer(g_geo, a, b);
}

/* Marks every filled cell that shares a row, column, box (or diagonal) with the same digit. */
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

/* Does the unit hold 1-n, each once? */
static bool unit_complete(int u)
{
    uint16_t seen = 0;
    for (int k = 0; k < g_geo->n; k++) {
        uint8_t v = g_b.val[g_geo->unit[u][k]];
        if (!v || (seen & (1u << v))) return false;
        seen |= (uint16_t)(1u << v);
    }
    return true;
}

/* How many of cell i's units (row, column, box, diagonals) were just completed. */
static int units_completed_by(int i)
{
    int n = 0;
    for (int k = 0; k < g_geo->ncu[i]; k++) n += unit_complete(g_geo->cu[i][k]);
    return n;
}

/* the cell's colour with nothing selected: X-Sudoku tints its two diagonals */
static lv_color_t cell_base_bg(const sudoku_geom_t *geo, int i, bool given)
{
    lv_color_t bg = given ? C_CELL_GIVEN : C_CELL;
    return sudoku_geom_on_diag(geo, i) ? lv_color_mix(C_ORANGE, bg, 40) : bg;
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

#define CLOCK_VALID(t) ((t) > 1700000000)      /* set by NTP */

static void mark_started(void)
{
    time_t now = time(NULL);
    g_started_at = CLOCK_VALID(now) ? (int64_t)now : 0;
    g_started_tick = lv_tick_get();
    if (!g_started_tick) g_started_tick = 1;
}

/* Start time, worked out from uptime if the clock was set after the puzzle
 * was opened (Wi-Fi connects a few seconds after start-up). */
static int64_t game_started_at(void)
{
    time_t now = time(NULL);
    if (!g_started_at && g_started_tick && CLOCK_VALID(now)) {
        g_started_at = (int64_t)now - (int64_t)((lv_tick_get() - g_started_tick) / 1000);
    }
    return g_started_at;
}

/*
 * The game in progress is saved (to sudoku.db, via store.h) after every move,
 * when leaving the game screen and when pausing; statistics when solved.
 * stats_db only writes the newest game, so saving often costs nothing here.
 */
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
    sg.hints = g_hints;
    sg.started_at = game_started_at();
    store_set_game(&sg);
    store_set_game_moves(g_moves, g_nmoves);
    store_commit_game();
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
        if (!g_geo->used[i]) continue;
        lv_color_t bg = cell_base_bg(g_geo, i, g_given[i]);
        if (g_solved) {
            bg = C_CELL_SOLVED;
        } else if (g_sel >= 0) {
            if (i == g_sel) bg = C_CELL_SEL;
            else if (sv && g_b.val[i] == sv) bg = C_CELL_SAME;
            else if (is_peer(i, g_sel)) bg = C_CELL_PEER;
        }
        if (h_step && !g_solved && h_mark[i]) {
            bg = h_mark[i] == 2 ? (h_info.kind == STEP_WRONG ? C_CELL_BAD : C_CELL_SEL) : C_CELL_SAME;
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
    for (int d = 1; d <= g_geo->n; d++) {
        int left = g_geo->n - count[d];
        lv_label_set_text_fmt(g_pad_cnt[d - 1], "%d", left > 0 ? left : 0);
        lv_obj_set_style_opa(g_pad[d - 1], left <= 0 ? LV_OPA_40 : LV_OPA_COVER, 0);
    }

    if (g_hints) lv_label_set_text_fmt(g_hint_cnt, "%u", (unsigned)g_hints);
    lv_obj_set_hidden(g_hint_cnt, g_hints == 0);

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
        lv_label_set_text_fmt(g_status, "Filled %d / %d", filled_count(), g_geo->ncells);
        lv_obj_set_style_text_color(g_status, C_MUTED, 0);
    }
}

static uint16_t s_solve_id;
static uint32_t s_solve_secs, s_solve_mistakes, s_solve_hints;
static int64_t  s_solve_started;

static void solve_commit_cb(lv_timer_t *t)
{
    store_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes);
    stats_db_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes, s_solve_hints,
                          s_solve_started, g_moves, g_nmoves);               /* finished: now */
    if (store_saved_game()->id == s_solve_id) store_clear_game();
    char day[12] = "";
    if (puzzle_is_generated(s_solve_id) && gen_lookup_day(s_solve_id, NULL, NULL, NULL, day) && day[0]) {
        int id, streak;
        bool solved;
        if (stats_db_daily_status(day, &id, &solved, &streak) && solved) {   /* after the solve above (queued first) */
            char msg[64];
            snprintf(msg, sizeof(msg), "Daily puzzle solved! Streak: %d day%s", streak, streak == 1 ? "" : "s");
            sudoku_ui_notice(msg, 4000);
        }
    }
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
    s_solve_hints = g_hints;
    s_solve_started = game_started_at();
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
    if (g_hints == 0) {
        lv_label_set_text(g_win_hint, "No hints");
        lv_obj_set_style_text_color(g_win_hint, C_GREEN, 0);
    } else {
        lv_label_set_text_fmt(g_win_hint, "%u hint%s used", (unsigned)g_hints, g_hints == 1 ? "" : "s");
        lv_obj_set_style_text_color(g_win_hint, C_ORANGE, 0);
    }
    if (!stats_db_ready()) {
        sudoku_ui_notice("No sudoku.db on a microSD card: this result is kept only until the board is "
                         "switched off.", 6000);
    }
    lv_obj_set_hidden(g_pause_ovl, true);
    lv_obj_set_hidden(g_win_ovl, false);
    update_header();
}

static void after_move(void)
{
    hint_drop();                                    /* the board changed: an open hint no longer fits */
    compute_conflicts();
    bool full = filled_count() == g_geo->ncells;
    bool ok = true;
    for (int i = 0; i < 81; i++) ok &= !g_conflict[i];
    if (full && ok) on_solved();
    refresh_board();
    save_current();                                 /* every move (not once solved) */
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

static void place_digit(int i, int d);
static void placed_sounds(int i, int d);

/* one entry in the move log (a cell's digit changed) */
static void record_move(int cell, int digit, move_kind_t kind)
{
    if (!g_moves) g_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    if (!g_moves || g_nmoves >= GAME_MOVES_MAX) return;
    g_moves[g_nmoves++] = (game_move_t){.t_ms = g_elapsed_ms, .cell = (uint8_t)cell, .digit = (uint8_t)digit,
                                        .kind = (uint8_t)kind};
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
            record_move(i, 0, MOVE_HAND);
        } else {
            place_digit(i, d);
            record_move(i, d, MOVE_HAND);
            placed = true;
        }
    }
    after_move();
    if (!placed || g_solved) return;          /* on_solved() plays its own sound */

    if (g_conflict[i]) {
        if (g_mistakes < 0xFFFF) g_mistakes++;
        sfx_play(SFX_ERROR);
        return;
    }
    placed_sounds(i, d);
}

/* Fill: the selected cell gets its digit from the solution; counts as a hint */
static void fill_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    if (!g_sol_ok) {
        sudoku_ui_notice("Fill isn't available: this puzzle has no solution.", 3000);
        return;
    }
    if (g_sel < 0 || g_given[g_sel]) {
        sudoku_ui_notice("Tap an empty cell first, then Fill.", 2500);
        return;
    }
    int i = g_sel, d = g_sol[i];
    if (g_b.val[i] == d) {
        sudoku_ui_notice("That cell is already right.", 2000);
        return;
    }
    push_undo();
    place_digit(i, d);
    record_move(i, d, MOVE_FILL);
    if (g_hints < 0xFFFF) g_hints++;
    after_move();
    if (!g_solved) placed_sounds(i, d);
}

/* a digit placed in cell i: its notes go, and peers lose that note */
static void place_digit(int i, int d)
{
    g_b.val[i] = (uint8_t)d;
    g_b.notes[i] = 0;
    for (int j = 0; j < 81; j++) {
        if (j != i && is_peer(i, j)) g_b.notes[j] &= (uint16_t)~(1u << d);
    }
}

/* chimes for a digit d placed in cell i without a conflict */
static void placed_sounds(int i, int d)
{
    if (g_conflict[i]) return;
    int n = 0;
    bool clean = true;
    for (int j = 0; j < 81; j++) {
        if (g_b.val[j] == d) {
            n++;
            clean &= !g_conflict[j];
        }
    }
    if (n == g_geo->n && clean) sfx_play(SFX_DIGIT_DONE);
    int units = units_completed_by(i);              /* row / column / box / diagonal now complete */
    if (units) sfx_play(units > 1 ? SFX_UNITS_DONE : SFX_UNIT_DONE);
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

    /* three marks per row: 3 rows on a 9x9 board, 2 (centred) on a 6x6 one */
    int size = lv_area_get_width(&co), pitch = size / 3, rows = (g_geo->n + 2) / 3;
    int x0 = 1, y0 = rows == 3 ? 2 : (size - rows * pitch) / 2;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = size >= 80 ? &lv_font_montserrat_22 : &lv_font_montserrat_16;
    d.align = LV_TEXT_ALIGN_CENTER;
    for (int n = 1; n <= g_geo->n; n++) {
        if (!(g_b.notes[i] & (1u << n))) continue;
        int r = (n - 1) / 3, c = (n - 1) % 3;
        lv_area_t a = {
            .x1 = co.x1 + x0 + c * pitch, .y1 = co.y1 + y0 + r * pitch,
            .x2 = co.x1 + x0 + c * pitch + pitch - 1, .y2 = co.y1 + y0 + r * pitch + pitch - 1,
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
    if (g_b.val[g_sel]) record_move(g_sel, 0, MOVE_ERASE);
    g_b.val[g_sel] = 0;
    g_b.notes[g_sel] = 0;
    after_move();
}

static void undo_cb(lv_event_t *e)
{
    if (g_solved || g_paused || g_undo_len == 0) return;
    g_undo_head = (g_undo_head + UNDO_MAX - 1) % UNDO_MAX;
    g_undo_len--;
    uint8_t before[81];
    memcpy(before, g_b.val, sizeof(before));
    g_b = g_undo[g_undo_head];
    for (int i = 0; i < 81; i++) {
        if (g_b.val[i] != before[i]) record_move(i, g_b.val[i], MOVE_UNDO);
    }
    after_move();
}

static void notes_cb(lv_event_t *e)
{
    g_notes_mode = lv_obj_has_state(g_notes_btn, LV_STATE_CHECKED);
}

static void set_paused(bool p)
{
    if (g_solved) p = false;
    if (p) {
        hint_drop();
        refresh_board();
    }
    g_paused = p;
    lv_obj_set_hidden(g_pause_ovl, !p);
    update_header();
    if (p) save_current();
}

/* ------------------------------------------------------------- hints */
/*
 * Hint: the next step a person would take (generator.c, sudoku_next_step),
 * in two steps: first where to look, then (More) the cell and why, then
 * Place it. A wrong digit is pointed out first. Each hint opened counts once.
 */
static void unit_name(int u, char *buf, size_t n)
{
    if (u < 9) snprintf(buf, n, "row %d", u + 1);
    else if (u < 18) snprintf(buf, n, "column %d", u - 9 + 1);
    else if (u < 27) snprintf(buf, n, "box %d", u - 18 + 1);
    else snprintf(buf, n, "the %s diagonal", u == UNIT_DIAG_MAIN ? "top-left" : "top-right");   /* from that corner */
}

/* "row, column or box" (and diagonal on X-Sudoku) */
static const char *units_word(void)
{
    return g_geo->diag ? "row, column, box or diagonal" : "row, column or box";
}

static void mark_unit(int u, uint8_t v)
{
    for (int k = 0; k < g_geo->n; k++) {
        int c = sudoku_unit_cell(g_var, u, k);
        if (c >= 0 && h_mark[c] < v) h_mark[c] = v;
    }
}

/* closes the hint without redrawing (the caller does) */
static void hint_drop(void)
{
    if (!h_step) return;
    h_step = 0;
    memset(h_mark, 0, sizeof(h_mark));
    lv_obj_set_hidden(h_card, true);
}

static void hint_render(void)
{
    const sudoku_step_t *h = &h_info;
    const sudoku_tech_t *t = &h->tech;
    char text[320], u1[24], u2[24], tech[180] = "";
    int r = h->cell / 9 + 1, c = h->cell % 9 + 1;
    memset(h_mark, 0, sizeof(h_mark));

    /* where to look: the technique's area if one is needed, else the single's */
    if (h->kind == STEP_WRONG || h->kind == STEP_NAKED_SINGLE || h->kind == STEP_NONE) h_mark[h->cell] = 1;
    if (h->kind == STEP_HIDDEN_SINGLE && !t->kind) mark_unit(h->unit, 1);
    if (t->kind) {
        mark_unit(t->unit, 1);
        unit_name(t->unit, u1, sizeof(u1));
        unit_name(t->unit2 >= 0 ? t->unit2 : t->unit, u2, sizeof(u2));
        switch (t->kind) {
        case TECH_POINTING:
            snprintf(tech, sizeof(tech), "In %s, %d can only be in %s, so no other cell of %s can be %d. ",
                     u1, t->digit, u2, u2, t->digit);
            break;
        case TECH_CLAIMING:
            snprintf(tech, sizeof(tech), "In %s, %d can only be in %s, so no other cell of %s can be %d. ",
                     u1, t->digit, u2, u2, t->digit);
            break;
        case TECH_NAKED_PAIR:
            snprintf(tech, sizeof(tech), "In %s, two cells can only be %d or %d, so no other cell there can. ",
                     u1, t->digit, t->digit2);
            break;
        case TECH_HIDDEN_PAIR:
            snprintf(tech, sizeof(tech), "In %s, %d and %d fit only two cells, so those two can't be anything else. ",
                     u1, t->digit, t->digit2);
            break;
        }
    }

    if (h_step == 1) {
        char un[24];
        switch (h->kind) {
        case STEP_WRONG:
            snprintf(text, sizeof(text), "Something's not right: check the highlighted cell.");
            break;
        case STEP_HIDDEN_SINGLE:
            if (t->kind) snprintf(text, sizeof(text), "Look at %s: it narrows down where a digit can go.", u1);
            else {
                unit_name(h->unit, un, sizeof(un));
                snprintf(text, sizeof(text), "Look at %s: one digit has only one place left there.", un);
            }
            break;
        case STEP_NAKED_SINGLE:
            if (t->kind) snprintf(text, sizeof(text), "Look at %s, then at the highlighted cell.", u1);
            else snprintf(text, sizeof(text), "Look at the highlighted cell: only one digit fits.");
            break;
        default:
            snprintf(text, sizeof(text), "No simple step here: this needs advanced techniques. More shows one cell.");
            break;
        }
        lv_label_set_text(lv_obj_get_child(h_act, 0), "More");
    } else {
        h_mark[h->cell] = 2;
        char un[24];
        switch (h->kind) {
        case STEP_WRONG:
            snprintf(text, sizeof(text), "The %d at row %d, column %d is wrong: it isn't part of the solution.",
                     h->digit, r, c);
            break;
        case STEP_HIDDEN_SINGLE:
            unit_name(h->unit, un, sizeof(un));
            snprintf(text, sizeof(text), "%s%s%d can only go here in %s: its other empty cells already see a %d "
                     "in their %s.", tech, t->kind ? "That leaves one place: " : "",
                     h->digit, un, h->digit, units_word());
            break;
        case STEP_NAKED_SINGLE:
            snprintf(text, sizeof(text), "%sOnly %d fits here (row %d, column %d): every other digit is already "
                     "in its %s%s.", tech, h->digit, r, c, units_word(), t->kind ? ", or ruled out above" : "");
            break;
        default:
            snprintf(text, sizeof(text), "The highlighted cell (row %d, column %d) is %d.", r, c, h->digit);
            break;
        }
        lv_label_set_text(lv_obj_get_child(h_act, 0), h->kind == STEP_WRONG ? "Erase it" : "Place it");
    }
    lv_label_set_text(h_text, text);

    /* the card at the bottom, or at the top when the bottom row matters */
    bool bottom_row = false;
    for (int k = 0; k < g_geo->n; k++) bottom_row |= h_mark[(g_geo->n - 1) * 9 + k] != 0;
    lv_obj_set_y(h_card, bottom_row ? 4 : SCR_W - 96);
    lv_obj_set_hidden(h_card, false);
    refresh_board();
}

static void hint_act_cb(lv_event_t *e)
{
    if (!h_step || g_solved || g_paused) return;
    if (h_step == 1) {
        h_step = 2;
        hint_render();
        return;
    }
    int i = h_info.cell, d = h_info.digit;
    bool wrong = h_info.kind == STEP_WRONG;
    push_undo();
    if (wrong) {
        record_move(i, 0, MOVE_ERASE);
        g_b.val[i] = 0;
    } else {
        place_digit(i, d);
        record_move(i, d, MOVE_FILL);
    }
    g_sel = i;
    after_move();                                   /* closes the hint */
    if (!wrong && !g_solved) placed_sounds(i, d);
}

static void hint_close_cb(lv_event_t *e)
{
    hint_drop();
    refresh_board();
}

static void hint_cb(lv_event_t *e)
{
    if (g_solved || g_paused || !g_sol_ok) return;
    if (h_step) {                                   /* already open: same as More / Place it */
        hint_act_cb(e);
        return;
    }
    if (!sudoku_next_step(g_var, g_b.val, g_sol, &h_info) || h_info.cell < 0) return;
    if (g_hints < 0xFFFF) g_hints++;               /* every hint opened counts once */
    refresh_board();                                /* the hint count on the Fill button */
    save_current();
    h_step = 1;
    hint_render();
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
    g_hints = 0;
    g_nmoves = 0;                                   /* a new attempt, a new log */
    hint_drop();
    mark_started();                                 /* opened or restarted: a new attempt */
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
    /* justified (left and right edges flush); NULL: the caller fills the content */
    if (text) make_just_text(lv_msgbox_get_content(m), NULL, C_TEXT, text);
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
    /* LVGL makes the footer LV_DPI_DEF / 3 (~43 px) tall, less than these
     * 52 px buttons, which then spill over the text and past the box edge */
    lv_obj_t *foot = lv_msgbox_get_footer(m);
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(foot, 10, 0);
    lv_obj_set_style_pad_bottom(foot, 18, 0);                  /* room under the buttons */
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

static void gen_start(int variant, int level);

static void win_next_cb(lv_event_t *e)
{
    if (puzzle_is_generated(g_id)) {             /* another one of the same variant and level */
        int lv = GEN_MEDIUM, var = VAR_CLASSIC;
        gen_lookup(g_id, NULL, &var, &lv);
        gen_start(var, lv);
        return;
    }
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

/*
 * Board layout for a variant: boxes and cells inside the GRID_SIZE square at
 * (GRID_X, GRID_Y), centred, with 4 px lines between boxes and 2 px between
 * cells. 9x9: 64 px cells; 6x6 (2x3 boxes): 97 px cells. Cells not on the
 * board (6x6) and boxes it doesn't have are hidden.
 */
#define LINE_THICK 4
#define LINE_THIN  2

static void board_layout(const sudoku_geom_t *geo, lv_obj_t *grid, lv_obj_t *const *boxes, lv_obj_t *const *cells)
{
    const int n = geo->n, bw = geo->box_w, bh = geo->box_h;
    const int across = n / bw, down = n / bh;                  /* boxes */
    int cw = (GRID_SIZE - (across + 1) * LINE_THICK - across * (bw - 1) * LINE_THIN) / n;
    int ch = (GRID_SIZE - (down + 1) * LINE_THICK - down * (bh - 1) * LINE_THIN) / n;
    int cs = cw < ch ? cw : ch;
    int box_w = bw * cs + (bw - 1) * LINE_THIN, box_h = bh * cs + (bh - 1) * LINE_THIN;
    int w = (across + 1) * LINE_THICK + across * box_w, h = (down + 1) * LINE_THICK + down * box_h;
    lv_obj_set_pos(grid, GRID_X + (GRID_SIZE - w) / 2, GRID_Y + (GRID_SIZE - h) / 2);
    lv_obj_set_size(grid, w, h);
    for (int b = 0; b < 9; b++) {
        bool on = b < across * down;
        lv_obj_set_hidden(boxes[b], !on);
        if (!on) continue;
        lv_obj_set_pos(boxes[b], LINE_THICK + (b % across) * (box_w + LINE_THICK),
                       LINE_THICK + (b / across) * (box_h + LINE_THICK));
        lv_obj_set_size(boxes[b], box_w, box_h);
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_set_hidden(cells[i], !geo->used[i]);
        if (!geo->used[i]) continue;
        int r = i / 9, c = i % 9;
        lv_obj_set_pos(cells[i], LINE_THICK + (c / bw) * (box_w + LINE_THICK) + (c % bw) * (cs + LINE_THIN),
                       LINE_THICK + (r / bh) * (box_h + LINE_THICK) + (r % bh) * (cs + LINE_THIN));
        lv_obj_set_size(cells[i], cs, cs);
    }
}

/* the game screen's board and number pad (1..n, as tall as the board) for g_var */
static void game_layout(void)
{
    if (!g_geo) g_geo = sudoku_geom(g_var);
    if (g_layout_var == g_var) return;
    g_layout_var = g_var;
    board_layout(g_geo, g_grid, g_boxes, g_cells);
    const int n = g_geo->n, span = GRID_SIZE - 6;              /* 598: the 9 x 62 px pad, as before */
    const int h = (span - (n - 1) * PAD_GAP) / n;
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *b = g_pad[d - 1];
        lv_obj_set_hidden(b, d > n);
        if (d > n) continue;
        lv_obj_set_pos(b, PAD_X, GRID_Y + 1 + (d - 1) * (h + PAD_GAP) + (span - n * h - (n - 1) * PAD_GAP) / 2);
        lv_obj_set_size(b, PAD_W, h);
    }
}

static void start_game(uint16_t id, bool resume)
{
    uint8_t given[81];
    if (!puzzle_get(id, given)) {
        sudoku_ui_notice("This puzzle isn't available (it was made on the board, and sudoku.db is missing).", 4000);
        return;
    }
    g_id = id;
    g_var = puzzle_variant(id);
    g_geo = sudoku_geom(g_var);
    game_layout();
    for (int i = 0; i < 81; i++) {                  /* only the board's cells, digits 1..n */
        if (!g_geo->used[i] || given[i] > g_geo->n) given[i] = 0;
    }
    memcpy(g_given, given, sizeof(g_given));
    g_sol_ok = sudoku_solve(g_var, g_given, g_sol);
    reset_game();
    const saved_game_t *s = store_saved_game();
    if (resume && s->id == id) {
        memcpy(g_b.val, s->val, sizeof(g_b.val));
        memcpy(g_b.notes, s->notes, sizeof(g_b.notes));
        for (int i = 0; i < 81; i++) {
            if (g_given[i]) g_b.val[i] = g_given[i];   /* givens are never editable */
            if (g_b.val[i] > g_geo->n || !g_geo->used[i]) g_b.val[i] = 0;
            if (!g_geo->used[i]) g_b.notes[i] = 0;
            g_b.notes[i] &= g_geo->all;
        }
        g_elapsed_ms = s->elapsed_ms;
        g_mistakes = s->mistakes;
        g_hints = s->hints;
        const game_move_t *sm;
        int sn = store_game_moves(&sm);
        if (sn > 0 && (g_moves || (g_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX,
                                                              MALLOC_CAP_SPIRAM)))) {
            memcpy(g_moves, sm, sizeof(game_move_t) * sn);
            g_nmoves = sn;
        }
        g_started_at = s->started_at;               /* from when it was first opened */
        if (!g_started_at) g_started_tick = 0;      /* unknown, and not worked out from this session */
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

    lv_obj_t *hb = make_btn(scr_game, SCR_W - 8 - 200 - 8 - 92, 6, 92, 48, "Hint", &lv_font_montserrat_18, hint_cb, NULL);
    lv_obj_set_style_text_color(lv_obj_get_child(hb, 0), C_ACCENT, 0);

    lv_obj_t *tb = make_btn(scr_game, SCR_W - 8 - 200, 6, 200, 48, NULL, NULL, pause_cb, NULL);
    lv_obj_set_style_bg_opa(tb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, LV_STATE_PRESSED);
    g_timer_icon = make_label(tb, &lv_font_montserrat_20, C_MUTED);
    lv_obj_align(g_timer_icon, LV_ALIGN_LEFT_MID, 12, 0);
    g_timer = make_label(tb, &font_timer_36, C_TEXT);
    lv_obj_align(g_timer, LV_ALIGN_RIGHT_MID, -6, 0);

    /* board: thick-line background, the boxes in thin-line colour, the cells
     * (placed by game_layout() for the puzzle's variant) */
    lv_obj_t *grid = lv_obj_create(scr_game);
    g_grid = grid;
    lv_obj_remove_style_all(grid);
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    for (int b = 0; b < 9; b++) {
        lv_obj_t *box = lv_obj_create(grid);
        lv_obj_remove_style_all(box);
        lv_obj_set_style_bg_color(box, C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(box, false);
        lv_obj_set_clickable(box, false);
        g_boxes[b] = box;
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
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
    int bx = 8, bw = 100;
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
    lv_obj_t *fb = make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_EYE_OPEN "  Fill", &lv_font_montserrat_16,
                            fill_cb, NULL);
    g_hint_cnt = make_label(fb, &lv_font_montserrat_12, C_ORANGE);     /* hints used */
    lv_obj_align(g_hint_cnt, LV_ALIGN_TOP_RIGHT, -5, 2);
    lv_obj_set_hidden(g_hint_cnt, true);
    bx += bw + 8;
    g_status = make_label(scr_game, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(g_status, SCR_W - 8 - bx);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_status, bx, BOT_Y + (BOT_H - 20) / 2);

    /* hint card: text, then More / Place it, and close (moved to the top by hint_render if needed) */
    h_card = lv_obj_create(scr_game);
    lv_obj_set_size(h_card, SCR_W - 16, 92);
    lv_obj_set_pos(h_card, 8, SCR_W - 96);
    lv_obj_set_style_bg_color(h_card, C_CARD, 0);
    lv_obj_set_style_border_color(h_card, C_ACCENT, 0);
    lv_obj_set_style_border_width(h_card, 2, 0);
    lv_obj_set_style_radius(h_card, 12, 0);
    lv_obj_set_style_pad_all(h_card, 10, 0);
    lv_obj_set_scrollable(h_card, false);
    h_text = make_label(h_card, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(h_text, SCR_W - 16 - 20 - 130 - 66);
    lv_label_set_long_mode(h_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(h_text, LV_ALIGN_LEFT_MID, 2, 0);
    h_act = make_btn(h_card, 0, 0, 122, 64, "More", &lv_font_montserrat_18, hint_act_cb, NULL);
    lv_obj_set_style_bg_color(h_act, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_act, 0), lv_color_white(), 0);
    lv_obj_align(h_act, LV_ALIGN_RIGHT_MID, -66, 0);
    lv_obj_t *hx = make_btn(h_card, 0, 0, 60, 64, LV_SYMBOL_CLOSE, &lv_font_montserrat_22, hint_close_cb, NULL);
    lv_obj_set_style_bg_color(hx, C_CARD2, 0);
    lv_obj_align(hx, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_hidden(h_card, true);
    h_step = 0;

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
    g_win_hint = make_label(card, &lv_font_montserrat_18, C_MUTED);
    lv_obj_align(g_win_hint, LV_ALIGN_TOP_MID, 0, 190);

    lv_obj_t *b1 = make_btn(card, 0, 0, 128, 60, LV_SYMBOL_LIST "  Puzzles", &lv_font_montserrat_16, win_list_cb, NULL);
    lv_obj_align(b1, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(b1, C_CARD2, 0);
    lv_obj_t *b3 = make_btn(card, 0, 0, 128, 60, LV_SYMBOL_PLAY "  Replay", &lv_font_montserrat_16, win_replay_cb, NULL);
    lv_obj_align(b3, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(b3, C_CARD2, 0);
    lv_obj_t *b2 = make_btn(card, 0, 0, 128, 60, "Next  " LV_SYMBOL_RIGHT, &lv_font_montserrat_16, win_next_cb, NULL);
    lv_obj_align(b2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(b2, C_ACCENT, 0);

    g_layout_var = -1;                              /* new widgets: lay them out for the current puzzle */
    game_layout();
}

/* ============================================================= replay */
/*
 * Plays a move log (moves.h) back over the puzzle's givens. The board at
 * step k is the givens plus moves 0..k-1, so any point can be shown at once:
 * step back and forth, jump to the start or end, or drag the slider. Playing
 * follows the game clock (pauses longer than RP_MAX_GAP_MS are shortened),
 * at 1x, 2x, 4x or 8x. Digits from Fill are orange, the last move is
 * highlighted, and digits that break a rule are red.
 */
#define RP_MAX_GAP_MS 2000
static const uint8_t RP_SPEEDS[] = {1, 2, 4, 8};

static lv_obj_t *scr_replay, *rp_return;            /* the screen, and where Back goes */
static lv_obj_t *rp_cells[81], *rp_lbl[81];
static const sudoku_geom_t *rp_geo;                 /* the replayed puzzle's variant */
static lv_obj_t *rp_play_lbl, *rp_speed_lbl, *rp_slider, *rp_pos, *rp_title, *rp_sub;
static lv_timer_t *rp_timer;
static uint8_t rp_given[81];
static game_move_t *rp_moves;                       /* PSRAM, GAME_MOVES_MAX */
static uint32_t *rp_vt;                             /* when each move plays, gaps shortened */
static int rp_n, rp_k;
static uint32_t rp_clock, rp_last_tick;
static bool rp_playing;
static int rp_speed;
static stats_replay_t *rp_loaded;                   /* from sudoku.db (History) */

static void rp_render(void)
{
    uint8_t val[81];
    uint8_t kind[81];
    memcpy(val, rp_given, sizeof(val));
    memset(kind, MOVE_HAND, sizeof(kind));
    for (int i = 0; i < rp_k; i++) {
        val[rp_moves[i].cell] = rp_moves[i].digit;
        kind[rp_moves[i].cell] = rp_moves[i].kind;
    }
    int last = rp_k ? rp_moves[rp_k - 1].cell : -1;
    for (int i = 0; i < 81; i++) {
        if (!rp_geo->used[i]) continue;
        bool bad = false;
        for (int j = 0; j < 81 && val[i] && !bad; j++) bad = j != i && val[j] == val[i] && sudoku_geom_peer(rp_geo, i, j);
        lv_color_t bg = cell_base_bg(rp_geo, i, rp_given[i]);
        if (i == last) bg = C_CELL_SEL;
        lv_obj_set_style_bg_color(rp_cells[i], bg, 0);
        lv_color_t fg = rp_given[i] ? C_TEXT : (kind[i] == MOVE_FILL ? C_ORANGE : C_DIGIT_USER);
        if (bad && !rp_given[i]) fg = C_RED;
        lv_obj_set_style_text_color(rp_lbl[i], fg, 0);
        lv_label_set_text_static(rp_lbl[i], DIGITS[val[i] <= 9 ? val[i] : 0]);
    }
    char now[16], total[16];
    fmt_time(now, sizeof(now), (rp_k ? rp_moves[rp_k - 1].t_ms : 0) / 1000);
    fmt_time(total, sizeof(total), (rp_n ? rp_moves[rp_n - 1].t_ms : 0) / 1000);
    lv_label_set_text_fmt(rp_pos, "Move %d / %d   %s / %s", rp_k, rp_n, now, total);
    lv_slider_set_value(rp_slider, rp_k, LV_ANIM_OFF);
    lv_label_set_text(rp_play_lbl, rp_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void rp_seek(int k, bool keep_playing)
{
    rp_k = k < 0 ? 0 : (k > rp_n ? rp_n : k);
    rp_clock = rp_k ? rp_vt[rp_k - 1] : 0;
    if (!keep_playing) rp_playing = false;
    rp_render();
}

static void rp_timer_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get(), dt = now - rp_last_tick;
    rp_last_tick = now;
    if (!rp_playing) return;
    rp_clock += dt * RP_SPEEDS[rp_speed];
    int k = rp_k;
    while (k < rp_n && rp_vt[k] <= rp_clock) k++;
    if (k == rp_n) rp_playing = false;
    if (k != rp_k || !rp_playing) {
        rp_k = k;
        rp_render();
    }
}

static void rp_btn_cb(lv_event_t *e)
{
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: rp_seek(0, false); break;                         /* start */
    case 1: rp_seek(rp_k - 1, false); break;                  /* one back */
    case 2:                                                   /* play / pause */
        if (!rp_playing && rp_k >= rp_n) rp_seek(0, false);   /* at the end: from the start */
        rp_playing = !rp_playing;
        rp_last_tick = lv_tick_get();
        rp_render();
        break;
    case 3: rp_seek(rp_k + 1, false); break;                  /* one forward */
    case 4: rp_seek(rp_n, false); break;                      /* end */
    case 5:                                                   /* speed */
        rp_speed = (rp_speed + 1) % (int)sizeof(RP_SPEEDS);
        lv_label_set_text_fmt(rp_speed_lbl, "%ux", RP_SPEEDS[rp_speed]);
        break;
    }
}

static void rp_slider_cb(lv_event_t *e)
{
    rp_seek(lv_slider_get_value(rp_slider), rp_playing);
}

static void rp_close(void)
{
    if (rp_timer) lv_timer_delete(rp_timer);
    rp_timer = NULL;
    lv_screen_load(rp_return ? rp_return : scr_home);
    lv_obj_delete_async(scr_replay);
    scr_replay = NULL;
}

static void rp_back_cb(lv_event_t *e)
{
    rp_close();
}

/* Loads a log for puzzle `id`: the givens, the moves and their play times. */
static bool rp_prepare(uint16_t id, const game_move_t *moves, int n)
{
    if (!rp_moves) rp_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    if (!rp_vt) rp_vt = heap_caps_malloc(sizeof(uint32_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    if (!rp_moves || !rp_vt) return false;
    if (n > GAME_MOVES_MAX) n = GAME_MOVES_MAX;
    if (n > 0 && moves != rp_moves) memcpy(rp_moves, moves, sizeof(game_move_t) * n);
    rp_n = n;
    uint32_t prev_t = 0, vt = 0;
    for (int i = 0; i < n; i++) {
        uint32_t gap = rp_moves[i].t_ms >= prev_t ? rp_moves[i].t_ms - prev_t : 0;
        vt += gap > RP_MAX_GAP_MS ? RP_MAX_GAP_MS : gap;
        rp_vt[i] = vt;
        prev_t = rp_moves[i].t_ms;
    }
    if (!puzzle_get(id, rp_given)) return false;
    rp_geo = sudoku_geom(puzzle_variant(id));
    return true;
}

/* Builds the Replay screen (on the default display) for the prepared log. */
static lv_obj_t *rp_build(uint16_t id, const char *sub)
{
    lv_obj_t *scr = make_screen();
    make_btn(scr, 8, 6, 104, 48, LV_SYMBOL_LEFT "  Back", &lv_font_montserrat_18, rp_back_cb, NULL);
    char name[40];
    puzzle_name(id, name, sizeof(name));
    rp_title = make_label(scr, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text_fmt(rp_title, "Replay  -  %s", name);
    lv_obj_set_pos(rp_title, 126, 6);
    rp_sub = make_label(scr, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(rp_sub, sub);
    lv_obj_set_pos(rp_sub, 126, 36);

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_t *boxes[9];
    for (int b = 0; b < 9; b++) {
        lv_obj_t *box = lv_obj_create(grid);
        lv_obj_remove_style_all(box);
        lv_obj_set_style_bg_color(box, C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(box, false);
        boxes[b] = box;
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(cell, false);
        rp_cells[i] = cell;
        rp_lbl[i] = make_label(cell, &font_digits_44, C_TEXT);
        lv_obj_center(rp_lbl[i]);
    }
    board_layout(rp_geo, grid, boxes, rp_cells);

    /* controls down the right: start, back, play, forward, end, speed */
    static const char *const SYM[] = {LV_SYMBOL_PREV, LV_SYMBOL_LEFT, LV_SYMBOL_PLAY, LV_SYMBOL_RIGHT,
                                      LV_SYMBOL_NEXT, "1x"};
    const int bh = 92, gap = (GRID_SIZE - 6 * bh) / 5;
    for (int i = 0; i < 6; i++) {
        lv_obj_t *b = make_btn(scr, PAD_X, GRID_Y + i * (bh + gap), PAD_W, bh, SYM[i],
                               &lv_font_montserrat_28, rp_btn_cb, (void *)(intptr_t)i);
        if (i == 2) {
            lv_obj_set_style_bg_color(b, C_ACCENT, 0);
            rp_play_lbl = lv_obj_get_child(b, 0);
            lv_obj_set_style_text_color(rp_play_lbl, lv_color_white(), 0);
        }
        if (i == 5) {
            rp_speed_lbl = lv_obj_get_child(b, 0);
            lv_obj_set_style_text_font(rp_speed_lbl, &lv_font_montserrat_22, 0);
            lv_label_set_text_fmt(rp_speed_lbl, "%ux", RP_SPEEDS[rp_speed]);
        }
    }

    /* bottom: slider + position */
    rp_slider = lv_slider_create(scr);
    lv_obj_set_size(rp_slider, 340, 12);
    lv_obj_set_pos(rp_slider, 24, BOT_Y + (BOT_H - 12) / 2);
    lv_slider_set_range(rp_slider, 0, rp_n > 0 ? rp_n : 1);
    lv_obj_set_style_bg_color(rp_slider, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_ext_click_area(rp_slider, 16);
    lv_obj_add_event_cb(rp_slider, rp_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    rp_pos = make_label(scr, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(rp_pos, SCR_W - 8 - 390);
    lv_obj_set_style_text_align(rp_pos, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(rp_pos, 390, BOT_Y + (BOT_H - 20) / 2);

    return scr;
}

/* Opens the Replay screen for a log of puzzle `id`; sub = the line under the title. */
static void open_replay(uint16_t id, const game_move_t *moves, int n, const char *sub)
{
    if (!rp_prepare(id, moves, n)) return;
    if (scr_replay) rp_close();
    rp_return = lv_screen_active();
    scr_replay = rp_build(id, sub);
    rp_playing = false;
    rp_seek(0, false);
    rp_last_tick = lv_tick_get();
    rp_timer = lv_timer_create(rp_timer_cb, 40, NULL);
    lv_screen_load(scr_replay);
    if (n == 0) sudoku_ui_notice("No moves were recorded for this game.", 3000);
}

/* the game just solved (from the Solved dialog) */
static void win_replay_cb(lv_event_t *e)
{
    char t[16], sub[96];
    fmt_time(t, sizeof(t), s_solve_secs);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             store_player_name(), t, (unsigned)g_hints, g_hints == 1 ? "" : "s");
    open_replay(g_id, g_moves, g_moves ? g_nmoves : 0, sub);
}

/* a solve from History: ask sudoku.db, history_poll() opens it */
static int64_t hz_row_solve[STATS_MAX_DAY_ROWS];

static bool rp_want_export;                 /* the replay asked for is to be exported, not shown */
static bool rx_busy;                        /* an export is running */
static void export_start(const stats_replay_t *r);

static void history_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !hz_row_solve[i]) return;
    rp_want_export = false;
    stats_db_query_replay(hz_row_solve[i]);
}

static void history_replay_poll(void)
{
    if (!rp_loaded) rp_loaded = heap_caps_malloc(sizeof(*rp_loaded), MALLOC_CAP_SPIRAM);
    if (!rp_loaded || !stats_db_take_replay(rp_loaded)) return;
    lv_obj_t *act = lv_screen_active();
    if (act != scr_history && act != scr_plays) return;      /* left meanwhile */
    if (rx_busy) return;
    if (!rp_loaded->found) {
        sudoku_ui_notice("This solve has no recorded moves.", 3000);
        return;
    }
    if (rp_want_export) {
        rp_want_export = false;
        export_start(rp_loaded);
        return;
    }
    char t[16], sub[96];
    fmt_time(t, sizeof(t), rp_loaded->seconds);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             rp_loaded->player, t, (unsigned)rp_loaded->hints, rp_loaded->hints == 1 ? "" : "s");
    open_replay(rp_loaded->puzzle, rp_loaded->moves, rp_loaded->n, sub);
}

/* ------------------------------------------------------------- export as MP4 */
/*
 * The Replay screen played into a video: it is built on a second, invisible
 * LVGL display (rendered into rx_fb in memory, never shown), stepped through
 * the game frame by frame, and every frame goes through the hardware H.264
 * encoder into an MP4 on the card (video_enc.c, mp4_writer.c). The video
 * holds the start for RX_INTRO_MS, plays the moves at RX_SPEED x the replay
 * timeline (pauses already shortened to RP_MAX_GAP_MS), then holds the solved
 * board for RX_OUTRO_MS. A dialog on the real screen shows the progress.
 */
#define RX_FPS        15
#define RX_SPEED      2
#define RX_INTRO_MS   1000
#define RX_OUTRO_MS   3000
#define RX_DIR        BSP_SD_MOUNT_POINT "/replays"

static lv_display_t *rx_disp;
static uint16_t *rx_fb;
static video_enc_t *rx_enc;
static lv_timer_t *rx_timer;
static int rx_frame, rx_frames, rx_last_k;
static bool rx_last_playing, rx_cancel;
static lv_obj_t *rx_ovl, *rx_bar, *rx_lbl;
static char rx_path[128];

static void rx_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    lv_display_flush_ready(d);                      /* the picture stays in rx_fb */
}

static void rx_cancel_cb(lv_event_t *e)
{
    rx_cancel = true;
}

static void rx_finish(bool ok)
{
    if (rx_timer) lv_timer_delete(rx_timer);
    rx_timer = NULL;
    uint32_t bytes = video_enc_bytes(rx_enc);
    if (ok) ok = video_enc_close(rx_enc);
    else video_enc_abort(rx_enc);
    rx_enc = NULL;
    if (rx_disp) lv_display_delete(rx_disp);       /* and the replay screen built on it */
    rx_disp = NULL;
    heap_caps_free(rx_fb);
    rx_fb = NULL;
    if (rx_ovl) lv_obj_delete(rx_ovl);
    rx_ovl = NULL;
    rx_busy = false;
    char msg[200];
    if (ok) {
        const char *name = strrchr(rx_path, '/');
        snprintf(msg, sizeof(msg), "Saved replays%s (%.1f MB). Copy it to a computer with Settings > USB drive.",
                 name ? name : "", bytes / 1048576.0);
        printf("export: %s, %u bytes, %d frames\n", rx_path, (unsigned)bytes, rx_frames);
    } else {
        snprintf(msg, sizeof(msg), rx_cancel ? "Export cancelled." : "Export failed: couldn't write the video.");
    }
    sudoku_ui_notice(msg, ok ? 6000 : 4000);
}

/* replay position at video frame f: moves shown, and whether it "plays" */
static int rx_k_at(int f, bool *playing)
{
    int64_t t = (int64_t)f * 1000 / RX_FPS - RX_INTRO_MS;
    *playing = false;
    if (t < 0 || rp_n == 0) return 0;
    uint32_t clock = (uint32_t)(t * RX_SPEED);
    int k = 0;
    while (k < rp_n && rp_vt[k] <= clock) k++;
    *playing = k < rp_n;
    return k;
}

static void rx_timer_cb(lv_timer_t *t)
{
    if (rx_cancel || !stats_db_ready()) {          /* cancelled, or the card went away */
        rx_finish(false);
        return;
    }
    uint32_t t0 = lv_tick_get();
    do {
        bool playing;
        int k = rx_k_at(rx_frame, &playing);
        bool changed = rx_frame == 0 || k != rx_last_k || playing != rx_last_playing;
        if (changed) {
            rp_k = k;
            rp_playing = playing;
            rp_render();
            lv_refr_now(rx_disp);                  /* into rx_fb */
            rx_last_k = k;
            rx_last_playing = playing;
        }
        if (!video_enc_frame(rx_enc, rx_fb, changed)) {
            rx_finish(false);
            return;
        }
        rx_frame++;
    } while (rx_frame < rx_frames && lv_tick_elaps(t0) < 30);   /* keep the progress dialog alive */

    if (rx_frame >= rx_frames) {
        rx_finish(true);
        return;
    }
    int pct = rx_frame * 100 / rx_frames;
    lv_bar_set_value(rx_bar, pct, LV_ANIM_OFF);
    /* LVGL's printf has no %f (LV_SPRINTF_USE_FLOAT off): tenths of a MB by hand */
    uint32_t tenths = (uint32_t)(((uint64_t)video_enc_bytes(rx_enc) * 10 + 524288) / 1048576);
    lv_label_set_text_fmt(rx_lbl, "Frame %d of %d   -   %u.%u MB", rx_frame, rx_frames,
                          (unsigned)(tenths / 10), (unsigned)(tenths % 10));
}

/* file name from the player, puzzle and when it was solved */
static void rx_name(const stats_replay_t *r, char *out, size_t n)
{
    char raw[96], name[40], when[20] = "";
    puzzle_name(r->puzzle, name, sizeof(name));
    if (r->finished_at) {
        time_t tt = (time_t)r->finished_at;
        struct tm tm;
        localtime_r(&tt, &tm);
        strftime(when, sizeof(when), "%Y%m%d-%H%M", &tm);
    } else {
        snprintf(when, sizeof(when), "solve%lld", (long long)r->solve_id);
    }
    snprintf(raw, sizeof(raw), "%s_%s_%s", r->player, name, when);
    size_t j = 0;
    for (size_t i = 0; raw[i] && j + 1 < n; i++) {   /* letters, digits, - and _ only */
        char c = raw[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) c = (c == ' ' || c == '#') ? '-' : 0;
        if (!c || (c == '-' && j && out[j - 1] == '-')) continue;
        out[j++] = c;
    }
    out[j] = 0;
}

static void export_start(const stats_replay_t *r)
{
    if (rx_busy) return;
    if (!stats_db_ready()) {
        sudoku_ui_notice("Can't export: no sudoku.db on a microSD card.", 3000);
        return;
    }
    if (!rp_prepare(r->puzzle, r->moves, r->n)) {
        printf("export: puzzle %u not available (%d moves)\n", (unsigned)r->puzzle, r->n);
        sudoku_ui_notice("Can't export: this puzzle couldn't be loaded.", 3000);
        return;
    }
    uint32_t play_ms = rp_n ? rp_vt[rp_n - 1] / RX_SPEED : 0;
    rx_frames = (int)((RX_INTRO_MS + play_ms + RX_OUTRO_MS) * RX_FPS / 1000) + 1;

    char file[80];
    rx_name(r, file, sizeof(file));
    mkdir(RX_DIR, 0775);                            /* fine if it exists */
    snprintf(rx_path, sizeof(rx_path), RX_DIR "/%s.mp4", file);

    rx_fb = heap_caps_aligned_alloc(64, SCR_W * SCR_W * 2, MALLOC_CAP_SPIRAM);
    rx_enc = rx_fb ? video_enc_open(rx_path, SCR_W, SCR_W, RX_FPS, rx_frames + 8) : NULL;
    if (!rx_enc) {
        printf("export: %s didn't open (frame buffer %s)\n", rx_path, rx_fb ? "ok" : "no memory");
        heap_caps_free(rx_fb);
        rx_fb = NULL;
        sudoku_ui_notice("Can't export: the video encoder or the file on the card couldn't be opened.", 4000);
        return;
    }

    /* the Replay screen on an invisible display that renders into rx_fb */
    lv_display_t *main_disp = lv_display_get_default();
    rx_disp = lv_display_create(SCR_W, SCR_W);
    lv_display_set_color_format(rx_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(rx_disp, rx_fb, NULL, SCR_W * SCR_W * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(rx_disp, rx_flush_cb);
    lv_display_set_theme(rx_disp, lv_display_get_theme(main_disp));
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(rx_disp);           /* no FPS/CPU box in the video */
#endif
    lv_display_set_default(rx_disp);
    char t[16], sub[96];
    fmt_time(t, sizeof(t), r->seconds);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             r->player, t, (unsigned)r->hints, r->hints == 1 ? "" : "s");
    lv_obj_t *scr = rp_build(r->puzzle, sub);
    lv_screen_load(scr);
    lv_display_set_default(main_disp);

    /* progress dialog on the real screen */
    rx_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(rx_ovl);
    lv_obj_set_size(rx_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(rx_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rx_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(rx_ovl, true);                   /* nothing else while it runs */
    lv_obj_t *card = lv_obj_create(rx_ovl);
    lv_obj_set_size(card, 520, 250);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(tl, LV_SYMBOL_VIDEO "  Exporting video");
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *fl = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(fl, 470);
    lv_label_set_long_mode(fl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(fl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(fl, "replays/%s.mp4", file);
    lv_obj_align(fl, LV_ALIGN_TOP_MID, 0, 42);
    rx_bar = lv_bar_create(card);
    lv_obj_set_size(rx_bar, 460, 16);
    lv_bar_set_range(rx_bar, 0, 100);
    lv_obj_set_style_bg_color(rx_bar, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rx_bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(rx_bar, LV_ALIGN_TOP_MID, 0, 80);
    rx_lbl = make_label(card, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(rx_lbl, "Starting...");
    lv_obj_align(rx_lbl, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_t *cb = make_btn(card, 0, 0, 200, 52, "Cancel", &lv_font_montserrat_18, rx_cancel_cb, NULL);
    lv_obj_set_style_bg_color(cb, C_CARD2, 0);
    lv_obj_align(cb, LV_ALIGN_BOTTOM_MID, 0, 0);

    rx_busy = true;
    rx_cancel = false;
    rx_frame = 0;
    rx_last_k = -1;
    rx_last_playing = false;
    rx_timer = lv_timer_create(rx_timer_cb, 1, NULL);
    printf("export: %s, %d frames\n", rx_path, rx_frames);
}

/* ------------------------------------------------------------- new puzzle (generator) */
/*
 * New puzzle: pick a level, a worker task runs the generator (generator.c)
 * while a dialog says so, then the puzzle is stored (sudoku.db, or RAM
 * without a card) and opened like any other.
 */
static lv_obj_t *gen_box;                           /* the "Generating..." dialog */
static lv_timer_t *gen_timer;
static volatile bool gen_done;
static int gen_var_req;                             /* variant being made (generator.h) */
static int gen_level_req, gen_level_got;
static uint8_t gen_givens[81];
static uint32_t gen_seed;                           /* 0 = random */
static char gen_day[12];                            /* making a daily puzzle: its date */

static void gen_task(void *arg)
{
    gen_level_t got = (gen_level_t)gen_level_req;
    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    sudoku_generate(gen_var_req, (gen_level_t)gen_level_req, gen_seed ? gen_seed : esp_random(), gen_givens, &got);
    int clues = 0;
    for (int i = 0; i < 81; i++) clues += gen_givens[i] != 0;
    printf("generator: %s %s asked, %s made, %d clues, %u ms\n", VARIANT_NAMES[gen_var_req],
           GEN_LEVEL_NAMES[gen_level_req], GEN_LEVEL_NAMES[got], clues,
           (unsigned)(esp_timer_get_time() / 1000 - t0));
    gen_level_got = got;
    gen_done = true;
    vTaskDelete(NULL);
}

static void gen_poll_cb(lv_timer_t *t)
{
    if (!gen_done) return;
    lv_timer_delete(gen_timer);
    gen_timer = NULL;
    if (gen_box) lv_msgbox_close(gen_box);
    gen_box = NULL;
    uint16_t id;
    int gid = stats_db_ready() ? stats_db_gen_add(gen_var_req, gen_level_got, gen_givens, gen_day[0] ? gen_day : NULL)
                               : -1;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = s_gen_ram_next++;                       /* no card: until power-off */
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
    }
    gen_cache_put(id, gen_var_req, gen_level_got, gen_givens, gen_day);
    if (gen_level_got != gen_level_req && !gen_day[0]) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Couldn't make a %s %s quickly; this one is %s.", GEN_LEVEL_NAMES[gen_level_req],
                 gen_var_req == VAR_CLASSIC ? "one" : VARIANT_NAMES[gen_var_req], GEN_LEVEL_NAMES[gen_level_got]);
        sudoku_ui_notice(msg, 4000);
    }
    open_puzzle(id);
}

/* seed 0 = a random puzzle; a daily one (always classic) has its date as the seed and day set */
static void gen_start_ex(int variant, int level, uint32_t seed, const char *day)
{
    if (gen_timer) return;                           /* one at a time */
    gen_var_req = variant >= 0 && variant < VAR_COUNT ? variant : VAR_CLASSIC;
    gen_level_req = level;
    gen_seed = seed;
    snprintf(gen_day, sizeof(gen_day), "%s", day ? day : "");
    gen_done = false;
    char text[80];
    if (day) snprintf(text, sizeof(text), "Making today's %s puzzle...", GEN_LEVEL_NAMES[level]);
    else if (gen_var_req == VAR_CLASSIC) snprintf(text, sizeof(text), "Making a new %s puzzle...", GEN_LEVEL_NAMES[level]);
    else snprintf(text, sizeof(text), "Making a new %s %s puzzle...", GEN_LEVEL_NAMES[level], VARIANT_NAMES[gen_var_req]);
    gen_box = make_msgbox(LV_SYMBOL_REFRESH "  New puzzle", text);
    if (xTaskCreate(gen_task, "gen", 16384, NULL, 2, NULL) != pdPASS) {
        lv_msgbox_close(gen_box);
        gen_box = NULL;
        sudoku_ui_notice("Couldn't start the generator.", 3000);
        return;
    }
    gen_timer = lv_timer_create(gen_poll_cb, 50, NULL);
}

static void gen_start(int variant, int level)
{
    gen_start_ex(variant, level, 0, NULL);
}

/* ------------------------------------------------------------- daily puzzle */
/*
 * One puzzle a day, the same for every player: made from the date (its seed),
 * at a level by weekday: Mon/Tue Easy, Wed/Thu Medium, Fri/Sat Hard, Sun
 * Expert. Stored like any generated puzzle, with its date (generated.day);
 * the streak counts days in a row with the daily solved (stats_db).
 */
static const uint8_t DAILY_LEVEL[7] = {GEN_EXPERT, GEN_EASY, GEN_EASY, GEN_MEDIUM, GEN_MEDIUM, GEN_HARD, GEN_HARD};

/* today's date and level; false while the clock isn't set */
static bool daily_today(char day[12], int *level, int *wday)
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) return false;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    *level = DAILY_LEVEL[tm.tm_wday];
    if (wday) *wday = tm.tm_wday;
    return true;
}

static uint32_t daily_seed(const char *day)          /* FNV-1a of the date */
{
    uint32_t h = 2166136261u;
    for (const char *p = day; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h ? h : 1;
}

static void daily_sub_text(char *buf, size_t n);

/* the Daily puzzle button: its line, and greyed out (not tappable) until
 * the clock is set (Wi-Fi connected, time fetched) */
static void daily_btn_update(void)
{
    char ds[80];
    daily_sub_text(ds, sizeof(ds));
    lv_label_set_text(h_daily_sub, ds);
    time_t now = time(NULL);
    lv_obj_set_state(h_daily, LV_STATE_DISABLED, !CLOCK_VALID(now));
}

/* the Daily puzzle line on the home screen */
static void daily_sub_text(char *buf, size_t n)
{
    char day[12];
    int level, id, streak;
    bool solved;
    if (!daily_today(day, &level, NULL)) {
        snprintf(buf, n, "Needs the date: connect Wi-Fi in Settings");
        return;
    }
    if (!stats_db_daily_status(day, &id, &solved, &streak)) {
        snprintf(buf, n, "Today: %s", GEN_LEVEL_NAMES[level]);
        return;
    }
    if (solved) snprintf(buf, n, LV_SYMBOL_OK "  Solved today  -  streak %d", streak);
    else if (streak) snprintf(buf, n, "Today: %s  -  streak %d", GEN_LEVEL_NAMES[level], streak);
    else snprintf(buf, n, "Today: %s", GEN_LEVEL_NAMES[level]);
}

static void daily_cb(lv_event_t *e)
{
    char day[12];
    int level, id = 0, streak = 0;
    bool solved = false;
    if (!daily_today(day, &level, NULL)) {
        sudoku_ui_notice("The daily puzzle needs today's date: connect Wi-Fi (Settings) so the clock is set.", 4000);
        return;
    }
    stats_db_daily_status(day, &id, &solved, &streak);
    if (!id) {                                        /* RAM-only (no card): look in the cache */
        for (int i = 0; i < GEN_CACHE; i++) {
            if (s_gen_cache[i].id && !strcmp(s_gen_cache[i].day, day)) id = s_gen_cache[i].id - PUZZLE_GEN_BASE;
        }
    }
    if (id) {
        if (solved) sudoku_ui_notice("You've solved today's puzzle; playing it again doesn't change the streak.", 3500);
        open_puzzle((uint16_t)(PUZZLE_GEN_BASE + id));
        return;
    }
    gen_start_ex(VAR_CLASSIC, level, daily_seed(day), day);
}

static lv_obj_t *gen_pick;                          /* the level dialog */
static lv_obj_t *gen_pick_desc;                     /* its line about the variant chosen */
static int gen_pick_var = VAR_CLASSIC;              /* variant chosen there (kept until power-off) */

static const char *const GEN_VAR_DESC[VAR_COUNT] = {
    "9x9: rows, columns and 3x3 boxes hold 1-9 once each.",
    "9x9, and the two tinted diagonals also hold 1-9 once each.",
    "6x6 with 2x3 boxes: rows, columns and boxes hold 1-6 once each.",
};

static void gen_pick_close(void)
{
    if (gen_pick) lv_msgbox_close_async(gen_pick);
    gen_pick = NULL;
}

static void gen_level_cb(lv_event_t *e)
{
    int level = (int)(intptr_t)lv_event_get_user_data(e);
    gen_pick_close();
    gen_start(gen_pick_var, level);
}

static void gen_var_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target_obj(e);
    uint32_t sel = lv_buttonmatrix_get_selected_button(bm);
    if (sel == LV_BUTTONMATRIX_BUTTON_NONE || sel >= VAR_COUNT) return;
    gen_pick_var = (int)sel;
    just_set_text(gen_pick_desc, GEN_VAR_DESC[gen_pick_var]);
}

static void gen_cancel_cb(lv_event_t *e)
{
    gen_pick_close();
}

/* a tap on the dimmed background (not on the dialog) closes it too */
static void gen_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) gen_pick_close();
}

static void new_puzzle_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("New puzzle", NULL);
    gen_pick = m;
    lv_obj_set_width(m, 600);
    make_just_text(lv_msgbox_get_content(m), NULL, C_TEXT,
                   "A new puzzle made on the board, with exactly one solution. Easy and Medium need only "
                   "singles; Hard needs pairs or locked candidates; Expert needs more than that.");

    /* the variant: Classic, X-Sudoku or Mini 6x6 (one of them checked) */
    static const char *const VAR_MAP[] = {"Classic", "X-Sudoku", "Mini 6x6", ""};
    lv_obj_t *vm = lv_buttonmatrix_create(lv_msgbox_get_content(m));
    lv_buttonmatrix_set_map(vm, VAR_MAP);
    lv_buttonmatrix_set_button_ctrl_all(vm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(vm, true);
    lv_buttonmatrix_set_button_ctrl(vm, (uint32_t)gen_pick_var, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(vm, LV_PCT(100), 64);
    lv_obj_set_style_pad_all(vm, 0, 0);
    lv_obj_set_style_pad_column(vm, 8, 0);
    lv_obj_set_style_margin_top(vm, 14, 0);
    lv_obj_set_style_border_width(vm, 0, 0);
    lv_obj_set_style_bg_opa(vm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(vm, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(vm, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_text_color(vm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(vm, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(vm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(vm, gen_var_cb, LV_EVENT_VALUE_CHANGED, NULL);
    gen_pick_desc = make_just_text(lv_msgbox_get_content(m), &lv_font_montserrat_16, C_MUTED,
                                   GEN_VAR_DESC[gen_pick_var]);
    lv_obj_set_style_margin_top(gen_pick_desc, 8, 0);

    /* the four levels: big buttons in two rows */
    lv_obj_t *grid = lv_obj_create(lv_msgbox_get_content(m));
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_top(grid, 16, 0);
    for (int lv = 0; lv < GEN_LEVELS; lv++) {
        lv_obj_t *b = make_btn(grid, 0, 0, 0, 72, GEN_LEVEL_NAMES[lv], &lv_font_montserrat_22, gen_level_cb,
                               (void *)(intptr_t)lv);
        lv_obj_set_width(b, LV_PCT(48));
        lv_obj_set_style_bg_color(b, C_ACCENT, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_white(), 0);
    }

    msgbox_button(m, "Cancel", gen_cancel_cb, false);
    lv_obj_t *backdrop = lv_obj_get_parent(m);         /* the modal's dimmed layer */
    if (backdrop && backdrop != lv_layer_top()) lv_obj_add_event_cb(backdrop, gen_backdrop_cb, LV_EVENT_CLICKED, NULL);
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
    if (g_id != STORE_NO_GAME && !puzzle_is_generated(g_id)) {
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

/* Home menu: a column of full-width buttons while they fit between the
 * header and the bottom of the screen, otherwise a grid of two per row. */
static void home_layout(void)
{
    int n = 0;
    uint32_t cnt = lv_obj_get_child_count(h_menu);
    for (uint32_t i = 0; i < cnt; i++) n += !lv_obj_is_hidden(lv_obj_get_child(h_menu, i));
    int grid = n * HOME_BTN_H + (n - 1) * HOME_GAP > HOME_MENU_MAX_H;
    if (grid == h_menu_grid) return;
    h_menu_grid = grid;
    lv_obj_set_flex_flow(h_menu, grid ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(h_menu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_set_width(lv_obj_get_child(h_menu, i), grid ? (HOME_GRID_W - HOME_GAP) / 2 : HOME_COL_W);
    }
}

static void refresh_home(void)
{
    uint16_t total = 0;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        total += store_solved_count(puzzle_collections[c].first, puzzle_collections[c].count);
    }
    lv_label_set_text_fmt(h_total, "%u of %u puzzles solved", (unsigned)total, (unsigned)PUZZLE_COUNT);
    lv_label_set_text_fmt(h_player, "Playing as %s", store_player_name());

    const saved_game_t *s = store_saved_game();
    lv_obj_set_hidden(h_continue, s->id == STORE_NO_GAME);
    if (s->id != STORE_NO_GAME) {
        char name[40], t[16];
        puzzle_name(s->id, name, sizeof(name));
        fmt_time(t, sizeof(t), s->elapsed_ms / 1000);
        lv_label_set_text_fmt(h_continue_sub, "%s  -  %s", name, t);
    }
    daily_btn_update();
    home_layout();
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
        char sd[64];
        stats_db_status(sd, sizeof(sd));
        if (strcmp(lv_label_get_text(st_sd_hint), sd) != 0) lv_label_set_text(st_sd_hint, sd);
        usb_btn_update();
        return;
    }
    if (act != scr_home) return;

    char c[8], d[64];
    time_t now = time(NULL);
    bool valid = now > 1700000000;                         /* set by NTP */
    /* the Daily puzzle line depends on the date: redo it when the clock gets
     * set (Wi-Fi) and at midnight */
    static char daily_key[12] = "?";
    char key[12] = "";
    if (valid) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(key, sizeof(key), "%Y-%m-%d", &tm);
    }
    if (strcmp(key, daily_key) != 0) {
        strcpy(daily_key, key);
        daily_btn_update();
    }
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

/* The screen draws a stats_view_t (stats_db.h): normally the answer to a
 * query on sudoku.db; without the database, the same built from RAM. */
typedef stats_level_t level_stat_t;

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

/* fallback when there is no sudoku.db: this session's RAM statistics */
static void view_from_ram(int level, stats_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->level = level;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        const puzzle_collection_t *pc = &puzzle_collections[c];
        for (uint16_t i = 0; i < pc->count; i++) {
            add_puzzle_stat(&v->levels[c], pc->first + i);
            add_puzzle_stat(&v->total, pc->first + i);
            if (c != level || !store_best_time(pc->first + i) || v->n_puzzles >= STATS_MAX_PUZZLES) continue;
            stats_puzzle_t *p = &v->puzzles[v->n_puzzles++];
            p->number = i + 1;
            p->best = (uint16_t)store_best_time(pc->first + i);
            p->fewest = store_fewest_mistakes(pc->first + i);
            p->solves = store_solve_count(pc->first + i);
        }
    }
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

/* the player dropdown on Statistics: everyone from sudoku.db (the Players
 * screen's list), the closed box shows an icon and who is playing */
static void stats_player_dd_update(void)
{
    if (!x_player_dd) return;
    static char opts[STATS_MAX_PLAYERS * STORE_NAME_LEN];
    bool db = stats_db_ready() && p_count > 0;
    int sel = 0;
    opts[0] = 0;
    if (db) {
        size_t len = 0;
        for (int i = 0; i < p_count; i++) {
            len += snprintf(opts + len, sizeof(opts) - len, "%s%s", i ? "\n" : "", p_players[i].name);
            if (p_players[i].id == store_player_id()) sel = i;
        }
    } else {
        snprintf(opts, sizeof(opts), "%s", store_player_name());
    }
    lv_dropdown_set_options(x_player_dd, opts);
    lv_dropdown_set_selected(x_player_dd, sel);
    snprintf(x_player_txt, sizeof(x_player_txt), SYMBOL_USER "  %s", store_player_name());
    lv_dropdown_set_text(x_player_dd, x_player_txt);
    lv_obj_set_state(x_player_dd, LV_STATE_DISABLED, !db || p_count < 2);
}

static void stats_player_dd_cb(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(x_player_dd);
    if ((int)i < p_count && p_players[i].id != store_player_id()) {
        stats_db_switch_player(p_players[i].id);   /* statistics reload when the switch is done */
    }
}

static stats_view_t x_view;

static void stats_select_rows(void)
{
    for (int r = 0; r < X_ROWS; r++) {
        bool sel = r == x_sel;
        lv_obj_set_state(x_btn[r], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(x_name_lbl[r], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(x_cnt_lbl[r], sel ? lv_color_white() : C_MUTED, 0);
    }
}

static void render_stats(const stats_view_t *v, bool from_db)
{
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        lv_label_set_text_fmt(x_cnt_lbl[c + 1], "%u/%u", (unsigned)v->levels[c].solved,
                              (unsigned)puzzle_collections[c].count);
    }
    lv_label_set_text_fmt(x_cnt_lbl[0], "%u/%u", (unsigned)v->total.solved, (unsigned)PUZZLE_COUNT);
    stats_player_dd_update();
    stats_select_rows();
    x_from_db = from_db;

    char f[16], a[16], m[16];
    lv_obj_scroll_to_y(x_table, 0, LV_ANIM_OFF);
    if (v->level < 0) {
        /* one row per level */
        lv_label_set_text(x_title, "All levels");
        stats_summary(&v->total, PUZZLE_COUNT);
        static const int32_t w[] = {140, 66, 72, 72, 82};   /* sums to PANE_W */
        static const char *const h[] = {"Level", "Solved", "Fastest", "Average", "Mistakes"};
        lv_table_set_row_count(x_table, PUZZLE_COLLECTION_COUNT + 1);
        table_columns(5, w, h);
        for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
            const level_stat_t *st = &v->levels[c];
            fmt_stat_times(f, a, sizeof(f), st);
            fmt_avg_mistakes(m, sizeof(m), st);
            lv_table_set_cell_value(x_table, c + 1, 0, puzzle_collections[c].name);
            lv_table_set_cell_value_fmt(x_table, c + 1, 1, "%u", (unsigned)st->solved);
            lv_table_set_cell_value(x_table, c + 1, 2, f);
            lv_table_set_cell_value(x_table, c + 1, 3, a);
            lv_table_set_cell_value(x_table, c + 1, 4, m);
        }
        lv_obj_set_hidden(x_table, false);
        lv_obj_set_hidden(x_empty, true);
        return;
    }

    /* one row per solved puzzle of the level */
    const puzzle_collection_t *pc = &puzzle_collections[v->level];
    lv_label_set_text(x_title, pc->name);
    stats_summary(&v->levels[v->level], pc->count);
    static const int32_t w[] = {90, 130, 110, 102};     /* sums to PANE_W */
    static const char *const h[] = {"Puzzle", "Best time", "Mistakes", "Solved"};
    lv_table_set_row_count(x_table, v->n_puzzles + 1);
    table_columns(4, w, h);
    for (int r = 0; r < v->n_puzzles; r++) {
        const stats_puzzle_t *p = &v->puzzles[r];
        fmt_time(f, sizeof(f), p->best);
        lv_table_set_cell_value_fmt(x_table, r + 1, 0, "#%u", (unsigned)p->number);
        lv_table_set_cell_value(x_table, r + 1, 1, f);
        if (p->fewest == STORE_MISTAKES_UNKNOWN) lv_table_set_cell_value(x_table, r + 1, 2, "-");
        else lv_table_set_cell_value_fmt(x_table, r + 1, 2, "%u", (unsigned)p->fewest);
        lv_table_set_cell_value_fmt(x_table, r + 1, 3, "%u", (unsigned)p->solves);
    }
    lv_obj_set_hidden(x_table, v->n_puzzles == 0);
    lv_obj_set_hidden(x_empty, v->n_puzzles != 0);
}

/* the (i) button next to the title: what the numbers mean */
static void stats_info_cb(lv_event_t *e)
{
    static char text[640];
    snprintf(text, sizeof(text), "%s\n\n"
             "Mistake: a placed digit that broke a row, column or box rule. Undo doesn't take it back.\n\n"
             "Per puzzle: the best time, the fewest mistakes in any solve, and how many times it was "
             "solved. \"-\" means mistakes weren't counted yet when it was solved.\n\n"
             "Per level: puzzles solved, the fastest and the average of the best times, and the "
             "average of the fewest mistakes.",
             x_from_db ? "These are the statistics of the player chosen at the top right, from sudoku.db "
                         "on the microSD card."
                       : "No sudoku.db: insert a FAT32 microSD card before switching on to keep statistics. "
                         "This shows what the board has in memory.");
    lv_obj_t *m = make_msgbox("About these statistics", text);
    lv_obj_set_width(m, 600);
    msgbox_button(m, "OK", mbox_close_cb, true);
}

/* Asks sudoku.db for the selected view (drawn when the answer arrives, see
 * sd_poll_cb); without the database, draws from RAM at once. */
static void refresh_stats(void)
{
    stats_select_rows();
    if (stats_db_ready()) {
        stats_db_query(x_sel - 1);
    } else {
        view_from_ram(x_sel - 1, &x_view);
        render_stats(&x_view, false);
    }
}

static void stats_cb(lv_event_t *e)
{
    if (stats_db_ready()) stats_db_list_players();   /* fresh list for the player dropdown */
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

/* brightness and volume: applied while the slider moves, saved when it's let go */
static void bright_sl_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(st_bright_sl);
    bsp_display_brightness_set(v);
    lv_label_set_text_fmt(st_bright_lbl, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) store_set_brightness((uint8_t)v);
}

static void vol_sl_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(st_vol_sl);
    bsp_audio_set_volume(v);
    lv_label_set_text_fmt(st_vol_lbl, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        store_set_volume((uint8_t)v);
        sfx_play(SFX_TOUCH);                       /* hear the new level */
    }
}

/* a slider + its value on the right of a settings row */
static lv_obj_t *settings_slider(lv_obj_t *row, int min, int max, int value, lv_event_cb_t cb, lv_obj_t **lbl)
{
    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_size(sl, 220, 14);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);              /* a knob a finger can find */
    lv_obj_set_ext_click_area(sl, 24);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_RELEASED, NULL);
    *lbl = make_label(row, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(*lbl, 56);
    lv_obj_set_style_text_align(*lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(*lbl, "%d%%", value);
    lv_obj_align(*lbl, LV_ALIGN_RIGHT_MID, -250, 0);
    return sl;
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
 * board. So the card is detected at boot, before Wi-Fi starts (see
 * sudoku_ui_start()), and formatting then only transfers data at the link's
 * own 40 MHz. A card inserted later can only be detected while
 * Wi-Fi has not been started since boot. */
typedef enum { SD_IDLE, SD_PROBING, SD_FOUND, SD_NO_CARD, SD_FORMATTING, SD_DONE, SD_FAILED } sd_state_t;
typedef enum { SD_JOB_PROBE, SD_JOB_FORMAT } sd_job_t;
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
    case SD_JOB_PROBE:
        err = bsp_sdcard_probe(&s_sd_bytes, s_sd_name);
        s_sd_present = err == ESP_OK;
        s_sd_state = s_sd_present ? SD_FOUND : SD_NO_CARD;
        break;
    case SD_JOB_FORMAT:
        stats_db_suspend();                        /* close sudoku.db, unmount */
        err = bsp_sdcard_check();                  /* still the card found earlier? */
        if (err == ESP_OK) err = bsp_sdcard_format_fat32();
        s_sd_err = err;
        if (err == ESP_OK) stats_db_start_fresh(); /* new sudoku.db: current settings, no statistics */
        s_sd_state = err == ESP_OK ? SD_DONE : SD_FAILED;
        break;
    }
    vTaskDelete(NULL);
}

static bool sd_run(sd_job_t job)
{
    s_sd_state = job == SD_JOB_PROBE ? SD_PROBING : SD_FORMATTING;
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
    /* sudoku.db results */
    static stats_view_t view;
    if (stats_db_take_view(&view) && view.level == x_sel - 1) {
        x_view = view;
        render_stats(&x_view, true);
    }
    static uint16_t c_best[PUZZLE_COUNT];
    static uint8_t c_fewest[PUZZLE_COUNT], c_solves[PUZZLE_COUNT];
    if (stats_db_take_cache(c_best, c_fewest, c_solves)) {
        store_reset_stats();                               /* sudoku.db -> RAM copy (this player's) */
        store_merge_stats(c_best, c_fewest, c_solves);
        lv_obj_t *act = lv_screen_active();
        if (act == scr_home) refresh_home();
        else if (act == scr_list) refresh_list();
        else if (act == scr_stats) refresh_stats();
    }

    if (u_ovl && (usb_msc_ejected() || usb_msc_unplugged())) usb_finish();   /* eject, or cable out */
    static store_prefs_t prefs;
    if (u_reloading && stats_db_take_prefs(&prefs)) {
        /* sudoku.db is back (perhaps a restored copy, or rebuilt): take everything from it */
        u_reloading = false;
        store_apply_prefs(&prefs);
        apply_loaded_prefs();
    }
    if (scr_history) history_poll();
    if (scr_plays) plays_poll();
    history_replay_poll();                                 /* a solve tapped in History or Gameplays */
    static stats_player_change_t ch;
    if (stats_db_take_player_change(&ch)) {
        store_set_player(ch.id, ch.name);
        if (ch.switched) {
            store_set_game(&ch.game);                      /* their game in progress (RAM only) */
            store_load_game_moves();                       /* and its move log */
            g_id = STORE_NO_GAME;                          /* the game screen held someone else's */
            char msg[64];
            snprintf(msg, sizeof(msg), "Now playing: %s", ch.name);
            sudoku_ui_notice(msg, 2500);
        }
        refresh_home();
        render_players();
        if (lv_screen_active() == scr_stats) refresh_stats();   /* now this player's */
        else stats_player_dd_update();
    }
    static stats_player_t list[STATS_MAX_PLAYERS];
    int n = stats_db_take_players(list);
    if (n >= 0) {
        memcpy(p_players, list, sizeof(stats_player_t) * n);
        p_count = n;
        render_players();
        stats_player_dd_update();
    }
    char msg[96];
    if (stats_db_take_message(msg, sizeof(msg))) sudoku_ui_notice(msg, 4000);

    sd_state_t st = s_sd_state;
    if (st == s_sd_seen) return;
    s_sd_seen = st;
    char sz[24], text[320];
    switch (st) {
    case SD_FOUND: {
        sd_btn_text("Format", true);
        stats_db_start();                          /* card found after boot: use it for stats too */
        fmt_gb(sz, sizeof(sz), s_sd_bytes);
        snprintf(text, sizeof(text),
                 "Card \"%s\", %s.\nEverything on it will be erased, including sudoku.db with your "
                 "statistics and solve history. It gets one FAT32 partition using the whole card, "
                 "and a new sudoku.db with your current settings and the game in progress.", s_sd_name, sz);
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
            store_reset_stats();                   /* match the new, empty sudoku.db */
            lv_obj_t *act = lv_screen_active();
            if (act == scr_home) refresh_home();
            else if (act == scr_list) refresh_list();
            else if (act == scr_stats) refresh_stats();
            fmt_gb(sz, sizeof(sz), s_sd_bytes);
            snprintf(text, sizeof(text), "microSD card formatted: FAT32, one %s partition. New sudoku.db "
                                         "with your current settings; statistics start fresh.", sz);
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

/* Everything after the settings changed under the UI (USB restore, factory
 * reset): sound, clock, timeout, level, and all screens redrawn. */
static void apply_loaded_prefs(void)
{
    g_id = STORE_NO_GAME;
    p_count = -1;
    sfx_set_enabled(store_sound_enabled());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(store_brightness());
    tz_apply(store_timezone());
    s_sleep_min = store_screen_timeout_min();
    l_coll = store_last_collection();
    apply_theme(store_dark_mode());                    /* redraws every screen with it */
    if (u_reload_home) {
        u_reload_home = false;
        refresh_home();
        lv_screen_load(scr_home);
    }
}

/* ------------------------------------------------------------- factory reset */

static void factory_reset_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    printf("factory reset\n");
    wifi_mgr_forget();                             /* network gone (Wi-Fi's NVS keys) */
    wifi_mgr_set_enabled(false);
    store_factory_reset();                         /* defaults, no stats, old NVS keys erased */
    u_reload_home = true;
    if (stats_db_ready()) {
        u_reloading = true;                        /* sd_poll_cb applies the rebuilt sudoku.db */
        stats_db_factory_reset();
        sudoku_ui_notice("Factory reset: rebuilding sudoku.db...", 3000);
    } else {
        apply_loaded_prefs();
        sudoku_ui_notice("Factory reset done. (No sudoku.db on a card to erase.)", 4000);
    }
}

static void factory_reset_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Factory reset?",
        "Settings go back to the defaults and the Wi-Fi network is forgotten. sudoku.db is erased: "
        "all players, statistics, solve history and games in progress. A new, empty sudoku.db is "
        "made. This can't be undone; to keep a copy, back up sudoku.db first (USB drive).");
    lv_obj_set_width(m, 600);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Reset", factory_reset_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

/* ------------------------------------------------------------- power off */

static void power_off_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    printf("power off\n");
    /* black first, so nothing half-drawn shows while the light goes out */
    lv_obj_t *black = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(black);
    lv_obj_set_size(black, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(black, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(black, LV_OPA_COVER, 0);
    lv_refr_now(NULL);
    bsp_display_brightness_set(0);
    vTaskDelay(pdMS_TO_TICKS(150));                /* the confirm tap's tick finishes */
    stats_db_suspend();                            /* queued writes done, sudoku.db closed, card unmounted */
    bsp_power_off();                               /* deep sleep until RESET */
}

static void power_off_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Turn off?",
        "The screen, sound and Wi-Fi turn off and the board goes into deep sleep. "
        "Your game in progress is kept. Press the RESET button on the board to turn it back on.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_POWER "  Turn off", power_off_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

/* ------------------------------------------------------------- USB drive */

static void usb_finish(void)
{
    if (!u_ovl) return;
    usb_msc_set_enabled(false);                    /* the computer lets go first */
    lv_obj_delete_async(u_ovl);
    u_ovl = NULL;
    u_reloading = true;                            /* sd_poll_cb applies what sudoku.db has */
    stats_db_start();                              /* remount + reopen (a new skeleton if it was deleted) */
    sudoku_ui_notice("Card back on the board: reopening sudoku.db...", 2500);
}

/* Connect only works with a computer on the OTG port: grey it out otherwise */
static void usb_btn_update(void)
{
    if (!st_usb_btn) return;
    bool host = usb_msc_host_connected();
    lv_obj_set_state(st_usb_btn, LV_STATE_DISABLED, !(s_sd_present && host));
    const char *hint = !s_sd_present ? "No microSD card"
                     : host ? "Back up or restore sudoku.db on a computer"
                     : "Plug the USB OTG port (not UART) into a computer";
    if (strcmp(lv_label_get_text(st_usb_hint), hint) != 0) lv_label_set_text(st_usb_hint, hint);
}

static void usb_done_cb(lv_event_t *e)
{
    usb_finish();
}

static void usb_connect_cb(lv_event_t *e)
{
    if (!s_sd_present) {
        sudoku_ui_notice("No microSD card. Insert one before switching on.", 4000);
        return;
    }
    if (s_sd_state != SD_IDLE) return;             /* formatting */
    if (!usb_msc_host_connected()) return;         /* greyed out anyway (see usb_btn_update) */
    stats_db_suspend();                            /* save, close sudoku.db, unmount: the card is free */
    if (usb_msc_set_enabled(true) != ESP_OK) {
        stats_db_start();
        sudoku_ui_notice("Couldn't start the USB drive.", 4000);
        return;
    }
    /* full screen, above everything: nothing may write the card meanwhile */
    u_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(u_ovl);
    lv_obj_set_size(u_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(u_ovl, C_BG, 0);
    lv_obj_set_style_bg_opa(u_ovl, LV_OPA_COVER, 0);
    lv_obj_set_clickable(u_ovl, true);
    lv_obj_set_flex_flow(u_ovl, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(u_ovl, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(u_ovl, 50, 0);
    lv_obj_set_style_pad_row(u_ovl, 26, 0);
    lv_obj_t *t = make_label(u_ovl, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, LV_SYMBOL_USB "  USB drive");
    lv_obj_t *txt = make_label(u_ovl, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(txt, LV_PCT(100));
    lv_label_set_long_mode(txt, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(txt, "The microSD card is now a drive on the computer connected to the board's "
                           "USB OTG port.\n\nCopy sudoku.db to back it up, or put a backup in its place "
                           "to restore it.");
    lv_obj_t *hint = make_label(u_ovl, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, "Eject the drive on the computer when you're done; the game takes the card "
                            "back by itself, also if the USB cable is unplugged. Done does the same, but "
                            "only tap it once copying has finished.");
    lv_obj_t *d = make_btn(u_ovl, 0, 0, 220, 60, "Done", &lv_font_montserrat_20, usb_done_cb, NULL);
    lv_obj_set_style_bg_color(d, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(d, 0), lv_color_white(), 0);
}

/* ------------------------------------------------------------- history */

static void fmt_clock(char *buf, size_t n, int64_t t)
{
    if (!t) {
        snprintf(buf, n, "?");
        return;
    }
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    strftime(buf, n, "%H:%M", &tm);
}

static void history_hint(const char *text)
{
    lv_obj_clean(hz_list);
    lv_obj_t *l = make_label(hz_list, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
}

static void render_day(const stats_day_t *d)
{
    struct tm tm = {.tm_year = d->year - 1900, .tm_mon = d->month - 1, .tm_mday = d->day, .tm_hour = 12};
    mktime(&tm);                                   /* fills in the weekday */
    char date[48];
    strftime(date, sizeof(date), "%A, %d %B %Y", &tm);
    lv_label_set_text(hz_title, date);
    if (!d->n) {
        history_hint("Nobody played that day.");
        return;
    }
    lv_obj_clean(hz_list);
    for (int i = 0; i < d->n; i++) {
        const stats_day_row_t *r = &d->rows[i];
        lv_obj_t *card = lv_obj_create(hz_list);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_CARD, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_hor(card, 16, 0);
        lv_obj_set_style_pad_ver(card, 10, 0);
        lv_obj_set_style_pad_row(card, 4, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollable(card, false);

        char name[40], took[16], start[8], end[8], line[128];
        puzzle_name(r->puzzle, name, sizeof(name));
        fmt_time(took, sizeof(took), r->seconds);
        fmt_clock(start, sizeof(start), r->started_at);
        fmt_clock(end, sizeof(end), r->finished_at);
        lv_obj_t *who = make_label(card, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(who, "%s   -   %s", r->player, name);
        lv_obj_t *what = make_label(card, &lv_font_montserrat_14, r->in_progress ? C_ORANGE : C_MUTED);
        if (r->in_progress) {
            snprintf(line, sizeof(line), "Started %s   -   in progress, %s so far   -   %u mistake%s",
                     start, took, (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s");
        } else {
            snprintf(line, sizeof(line), "Solved %s in %s   -   %u mistake%s   -   started %s",
                     end, took, (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s", start);
        }
        if (r->hints) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   %u hint%s",
                     (unsigned)r->hints, r->hints == 1 ? "" : "s");
        }
        lv_label_set_text(what, line);
        hz_row_solve[i] = 0;
        if (!r->in_progress && r->has_moves && r->solve_id) {     /* tap to watch it again */
            hz_row_solve[i] = r->solve_id;
            lv_obj_set_clickable(card, true);
            lv_obj_set_style_bg_color(card, C_CARD2, LV_STATE_PRESSED);
            lv_obj_add_event_cb(card, history_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *rp = make_label(card, &lv_font_montserrat_14, C_ACCENT);
            lv_label_set_text(rp, LV_SYMBOL_PLAY "  Tap to replay");
        }
    }
    if (d->total > d->n) {
        char more[48];
        snprintf(more, sizeof(more), "... and %d more", d->total - d->n);
        lv_obj_t *l = make_label(hz_list, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(l, more);
    }
}

static void render_month(const stats_month_t *m)
{
    memcpy(hz_count, m->count, sizeof(hz_count));
    int n = 0;
    for (int d = 1; d <= 31; d++) {
        if (!m->count[d - 1]) continue;
        hz_marked[n].year = m->year;
        hz_marked[n].month = m->month;
        hz_marked[n].day = d;
        n++;
    }
    lv_calendar_set_highlighted_dates(hz_cal, hz_marked, n);
}

/* The tapped day: solid accent fill, white number (the calendar itself only
 * marks it faintly, and only on highlighted days). Plus a badge with the
 * number of games at the top right of each highlighted day. */
static void history_cal_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_task_type_t type = lv_draw_task_get_type(t);
    if (type != LV_DRAW_TASK_TYPE_LABEL && type != LV_DRAW_TASK_TYPE_FILL) return;
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) return;
    lv_obj_t *bm = lv_event_get_current_target_obj(e);
    uint32_t id = base->id1;
    if (id < 7 || lv_buttonmatrix_has_button_ctrl(bm, id, LV_BUTTONMATRIX_CTRL_DISABLED)) return;  /* names, other months */
    int day = atoi(lv_buttonmatrix_get_button_text(bm, id));
    if (day < 1 || day > 31) return;

    const lv_calendar_date_t *sh = lv_calendar_get_showed_date(hz_cal);
    bool selected = hz_sel.year == sh->year && hz_sel.month == sh->month && hz_sel.day == day;
    if (type == LV_DRAW_TASK_TYPE_FILL) {
        if (selected) {
            lv_draw_fill_dsc_t *f = lv_draw_task_get_fill_dsc(t);
            f->color = C_ACCENT;
            f->opa = LV_OPA_COVER;
        }
        return;
    }
    if (selected) lv_draw_task_get_label_dsc(t)->color = lv_color_white();

    if (!lv_buttonmatrix_has_button_ctrl(bm, id, LV_BUTTONMATRIX_CTRL_CUSTOM_2)) return;  /* highlighted */
    if (!hz_count[day - 1]) return;

    char txt[4];
    if (hz_count[day - 1] > 99) snprintf(txt, sizeof(txt), "99");
    else snprintf(txt, sizeof(txt), "%u", (unsigned)hz_count[day - 1]);
    lv_area_t a;                                    /* the day number's text area */
    lv_draw_task_get_area(t, &a);
    int w = txt[1] ? 24 : 18, h = 18;
    lv_area_t pill = {.x1 = a.x2 + 3, .y1 = a.y1 - 10, .x2 = a.x2 + 3 + w - 1, .y2 = a.y1 - 10 + h - 1};

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = C_ORANGE;
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(base->layer, &rd, &pill);
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = &lv_font_montserrat_12;
    ld.color = lv_color_white();
    ld.align = LV_TEXT_ALIGN_CENTER;
    ld.text = txt;
    ld.text_local = 1;                              /* copied: txt is on the stack */
    lv_area_t ta = pill;
    ta.y1 += (h - lv_font_get_line_height(ld.font)) / 2;
    lv_draw_label(base->layer, &ld, &ta);
}

static void history_ask_month(int year, int month)
{
    hz_year = year;
    hz_month = month;
    stats_db_query_month(year, month);
}

static void history_day_cb(lv_event_t *e)
{
    lv_calendar_date_t d;
    if (lv_calendar_get_pressed_date(hz_cal, &d) != LV_RESULT_OK || !d.year) return;
    hz_sel = d;
    lv_obj_invalidate(lv_calendar_get_btnmatrix(hz_cal));
    if (!stats_db_ready()) return;
    stats_db_query_day(d.year, d.month, d.day);
}

static void history_cb(lv_event_t *e)
{
    lv_label_set_text(hz_title, "Tap a day to see who played what");
    hz_sel.year = 0;                                /* nothing picked yet */
    if (!stats_db_ready()) {
        history_hint("No sudoku.db: insert a FAT32 microSD card before switching on to keep a history.");
    } else {
        history_hint("Highlighted days had games; the badge says how many. Solves from before the "
                     "clock was set (Wi-Fi) "
                     "have no date and don't appear.");
        time_t now = time(NULL);
        if (CLOCK_VALID(now)) {
            struct tm tm;
            localtime_r(&now, &tm);
            lv_calendar_set_today_date(hz_cal, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            lv_calendar_set_month_shown(hz_cal, tm.tm_year + 1900, tm.tm_mon + 1);
            hz_latest_pending = false;
            history_ask_month(tm.tm_year + 1900, tm.tm_mon + 1);
        } else {
            hz_latest_pending = true;                  /* no clock: open on the latest month played */
            history_ask_month(0, 0);
        }
    }
    lv_screen_load(scr_history);
}

static void history_back_cb(lv_event_t *e)
{
    show_home();
}

/* results from sudoku.db, and the month arrows (called from sd_poll_cb) */
static void history_poll(void)
{
    static stats_month_t m;
    if (stats_db_take_month(&m)) {
        if (hz_latest_pending && m.year) {
            lv_calendar_set_month_shown(hz_cal, m.year, m.month);
            hz_year = m.year;
            hz_month = m.month;
        }
        hz_latest_pending = false;
        if (m.year == hz_year && m.month == hz_month) render_month(&m);
    }
    static stats_day_t d;
    if (stats_db_take_day(&d)) render_day(&d);
    if (lv_screen_active() == scr_history && stats_db_ready() && !hz_latest_pending) {
        const lv_calendar_date_t *sh = lv_calendar_get_showed_date(hz_cal);
        if (sh->year != hz_year || sh->month != hz_month) history_ask_month(sh->year, sh->month);
    }
}

static void build_history(void)
{
    scr_history = make_screen();
    make_btn(scr_history, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Back", &lv_font_montserrat_18,
             history_back_cb, NULL);
    lv_obj_t *t = make_label(scr_history, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "History");
    lv_obj_set_pos(t, 150, 20);

    hz_cal = lv_calendar_create(scr_history);
    lv_obj_set_pos(hz_cal, 12, 72);
    lv_obj_set_size(hz_cal, SCR_W - 24, 380);
    lv_calendar_add_header_arrow(hz_cal);
    lv_obj_set_style_text_font(hz_cal, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(hz_cal, C_CARD, 0);
    lv_obj_set_style_border_width(hz_cal, 0, 0);
    lv_obj_set_style_radius(hz_cal, 12, 0);
    lv_obj_t *bm = lv_calendar_get_btnmatrix(hz_cal);
    lv_obj_set_style_text_color(bm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_send_draw_task_events(bm, true);
    lv_obj_add_event_cb(bm, history_cal_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(hz_cal, history_day_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (hz_year) lv_calendar_set_month_shown(hz_cal, hz_year, hz_month);   /* theme rebuild */

    hz_title = make_label(scr_history, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(hz_title, 16, 462);
    lv_label_set_text(hz_title, "Tap a day to see who played what");

    hz_list = lv_obj_create(scr_history);
    lv_obj_remove_style_all(hz_list);
    lv_obj_set_pos(hz_list, 12, 494);
    lv_obj_set_size(hz_list, SCR_W - 24, SCR_W - 494 - 10);
    lv_obj_set_flex_flow(hz_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hz_list, 8, 0);
    lv_obj_set_scroll_dir(hz_list, LV_DIR_VER);
}

/* ------------------------------------------------------------- gameplays */
/*
 * Every solve with a move log, newest first, a page of STATS_MAX_DAY_ROWS at a
 * time from sudoku.db. Tap one to replay it (the Replay screen; Back returns
 * here).
 */
static lv_obj_t *pl_list, *pl_count, *pl_newer, *pl_older;
static int pl_offset, pl_total;
static int64_t pl_row_solve[STATS_MAX_DAY_ROWS];

static void plays_hint(const char *text)
{
    lv_obj_clean(pl_list);
    lv_obj_t *l = make_label(pl_list, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
}

static void plays_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !pl_row_solve[i] || rx_busy) return;
    rp_want_export = false;
    stats_db_query_replay(pl_row_solve[i]);
}

static void plays_export_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !pl_row_solve[i] || rx_busy) return;
    rp_want_export = true;
    stats_db_query_replay(pl_row_solve[i]);
}

static void plays_pager_update(void)
{
    bool newer = pl_offset > 0, older = pl_offset + STATS_MAX_DAY_ROWS < pl_total;
    lv_obj_set_state(pl_newer, LV_STATE_DISABLED, !newer);
    lv_obj_set_state(pl_older, LV_STATE_DISABLED, !older);
    lv_obj_set_hidden(pl_newer, !newer && !older);
    lv_obj_set_hidden(pl_older, !newer && !older);
}

static void render_plays(const stats_plays_t *p)
{
    pl_offset = p->offset;
    pl_total = p->total;
    if (p->total == 0) {
        lv_label_set_text(pl_count, "");
        plays_hint("No recorded games yet. Solve a puzzle and it shows up here, ready to replay.");
    } else {
        lv_label_set_text_fmt(pl_count, "%d-%d of %d", p->offset + 1, p->offset + p->n, p->total);
        lv_obj_clean(pl_list);
    }
    for (int i = 0; i < p->n; i++) {
        const stats_day_row_t *r = &p->rows[i];
        pl_row_solve[i] = r->solve_id;
        lv_obj_t *card = lv_obj_create(pl_list);
        lv_obj_set_size(card, LV_PCT(100), 76);
        lv_obj_set_style_bg_color(card, C_CARD, 0);
        lv_obj_set_style_bg_color(card, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_hor(card, 16, 0);
        lv_obj_set_scrollable(card, false);
        lv_obj_add_event_cb(card, plays_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        char name[40], took[16], when[40], line[128];
        puzzle_name(r->puzzle, name, sizeof(name));
        fmt_time(took, sizeof(took), r->seconds);
        if (r->finished_at) {
            time_t tt = (time_t)r->finished_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%a %d %b %Y, %H:%M", &tm);
        } else {
            snprintf(when, sizeof(when), "Date unknown");
        }
        lv_obj_t *who = make_label(card, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(who, "%s   -   %s", r->player, name);
        lv_obj_align(who, LV_ALIGN_LEFT_MID, 0, -13);
        lv_obj_t *what = make_label(card, &lv_font_montserrat_14, C_MUTED);
        snprintf(line, sizeof(line), "%s   -   %s   -   %u mistake%s", when, took,
                 (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s");
        if (r->hints) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   %u hint%s", (unsigned)r->hints, r->hints == 1 ? "" : "s");
        }
        lv_label_set_text(what, line);
        lv_obj_align(what, LV_ALIGN_LEFT_MID, 0, 14);
        lv_obj_t *play = make_label(card, &lv_font_montserrat_24, C_ACCENT);
        lv_label_set_text(play, LV_SYMBOL_PLAY);
        lv_obj_align(play, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *mp4 = make_btn(card, 0, 0, 96, 52, LV_SYMBOL_DOWNLOAD "  MP4", &lv_font_montserrat_16,
                                 plays_export_cb, (void *)(intptr_t)i);   /* save as a video */
        lv_obj_set_style_bg_color(mp4, C_CARD2, 0);
        lv_obj_align(mp4, LV_ALIGN_RIGHT_MID, -44, 0);
    }
    lv_obj_scroll_to_y(pl_list, 0, LV_ANIM_OFF);
    plays_pager_update();
}

static void plays_ask(int offset)
{
    if (!stats_db_ready()) {
        lv_label_set_text(pl_count, "");
        plays_hint("No sudoku.db: insert a FAT32 microSD card before switching on to keep recorded games.");
        pl_total = 0;
        plays_pager_update();
        return;
    }
    stats_db_query_plays(offset < 0 ? 0 : offset);
}

static void plays_page_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);   /* -1 newer, +1 older */
    plays_ask(pl_offset + dir * STATS_MAX_DAY_ROWS);
}

static void plays_cb(lv_event_t *e)
{
    plays_ask(0);
    lv_screen_load(scr_plays);
}

/* results from sudoku.db (called from sd_poll_cb) */
static void plays_poll(void)
{
    static stats_plays_t *p;
    if (!p) p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM);
    if (p && stats_db_take_plays(p)) render_plays(p);
}

static void build_plays(void)
{
    scr_plays = make_screen();
    make_btn(scr_plays, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_plays, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Gameplays");
    lv_obj_set_pos(t, 150, 20);
    pl_count = make_label(scr_plays, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(pl_count, LV_ALIGN_TOP_RIGHT, -16, 28);
    lv_label_set_text(pl_count, "");

    pl_list = lv_obj_create(scr_plays);
    lv_obj_remove_style_all(pl_list);
    lv_obj_set_pos(pl_list, 12, 74);
    lv_obj_set_size(pl_list, SCR_W - 24, SCR_W - 74 - 72);
    lv_obj_set_flex_flow(pl_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pl_list, 8, 0);
    lv_obj_set_scroll_dir(pl_list, LV_DIR_VER);

    pl_newer = make_btn(scr_plays, 12, SCR_W - 62, 200, 52, LV_SYMBOL_LEFT "  Newer", &lv_font_montserrat_18,
                        plays_page_cb, (void *)(intptr_t)-1);
    pl_older = make_btn(scr_plays, SCR_W - 12 - 200, SCR_W - 62, 200, 52, "Older  " LV_SYMBOL_RIGHT,
                        &lv_font_montserrat_18, plays_page_cb, (void *)(intptr_t)1);
    lv_obj_set_style_text_opa(lv_obj_get_child(pl_newer, 0), LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(lv_obj_get_child(pl_older, 0), LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_hidden(pl_newer, true);
    lv_obj_set_hidden(pl_older, true);
    plays_hint("Loading...");
}

/* ------------------------------------------------------------- players */

/* name dialog (add or rename), on the top layer with an on-screen keyboard */
static lv_obj_t *nd_ovl, *nd_ta;
static int32_t nd_rename_id;             /* 0 = add a player */

static void nd_close(void)
{
    if (!nd_ovl) return;
    lv_obj_delete_async(nd_ovl);
    nd_ovl = nd_ta = NULL;
}

static void nd_ok(void)
{
    const char *name = lv_textarea_get_text(nd_ta);
    if (nd_rename_id) stats_db_rename_player(nd_rename_id, name);
    else stats_db_add_player(name);
    nd_close();
}

static void nd_kb_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) nd_ok();
    else if (code == LV_EVENT_CANCEL) nd_close();
}

static void nd_btn_cb(lv_event_t *e)
{
    if ((intptr_t)lv_event_get_user_data(e)) nd_ok();
    else nd_close();
}

static void nd_open(int32_t rename_id, const char *current)
{
    nd_close();
    nd_rename_id = rename_id;
    nd_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(nd_ovl);
    lv_obj_set_size(nd_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(nd_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(nd_ovl, LV_OPA_70, 0);
    lv_obj_set_clickable(nd_ovl, true);              /* block touches to the page */

    lv_obj_t *panel = lv_obj_create(nd_ovl);
    lv_obj_set_size(panel, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);
    lv_obj_t *t = make_label(panel, &lv_font_montserrat_24, C_TEXT);
    lv_label_set_text(t, rename_id ? "Rename player" : "New player");

    nd_ta = lv_textarea_create(panel);
    lv_obj_set_width(nd_ta, LV_PCT(100));
    lv_textarea_set_one_line(nd_ta, true);
    lv_textarea_set_max_length(nd_ta, STORE_NAME_LEN - 1);
    lv_textarea_set_placeholder_text(nd_ta, "Name");
    lv_textarea_set_text(nd_ta, current ? current : "");
    lv_obj_set_style_text_font(nd_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(nd_ta, C_TEXT, 0);
    lv_obj_set_style_text_color(nd_ta, C_MUTED, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(nd_ta, C_BG, 0);
    lv_obj_set_style_border_color(nd_ta, C_ACCENT, 0);
    lv_obj_set_style_radius(nd_ta, 10, 0);
    lv_obj_add_state(nd_ta, LV_STATE_FOCUSED);

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *cancel = make_btn(row, 0, 0, 150, 52, "Cancel", &lv_font_montserrat_18, nd_btn_cb, (void *)0);
    lv_obj_set_style_bg_color(cancel, C_CARD2, 0);
    lv_obj_t *ok = make_btn(row, 0, 0, 150, 52, rename_id ? "Rename" : "Add", &lv_font_montserrat_18,
                            nd_btn_cb, (void *)1);
    lv_obj_set_style_bg_color(ok, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ok, 0), lv_color_white(), 0);

    lv_obj_t *kb = lv_keyboard_create(nd_ovl);
    lv_obj_set_size(kb, LV_PCT(100), 330);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, nd_ta);
    lv_obj_set_style_bg_color(kb, C_BG, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 8, 0);
    lv_obj_set_style_pad_bottom(kb, 30, 0);
    lv_obj_set_style_pad_gap(kb, 8, 0);
    lv_obj_set_style_bg_color(kb, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, C_CARD, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_22, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, nd_kb_cb, LV_EVENT_ALL, NULL);
}

static void player_row_cb(lv_event_t *e)
{
    int32_t id = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (id && id != store_player_id()) stats_db_switch_player(id);
}

static void player_rename_cb(lv_event_t *e)
{
    nd_open((int32_t)(intptr_t)lv_event_get_user_data(e), store_player_name());
}

static int32_t p_delete_id;

static void player_delete_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (p_delete_id) stats_db_delete_player(p_delete_id);
    p_delete_id = 0;
}

static void player_delete_cb(lv_event_t *e)
{
    int32_t id = (int32_t)(intptr_t)lv_event_get_user_data(e);
    const stats_player_t *pl = NULL;
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == id) pl = &p_players[i];
    }
    if (!pl) return;
    p_delete_id = id;
    char title[48], text[256];
    snprintf(title, sizeof(title), "Delete %s?", pl->name);
    snprintf(text, sizeof(text),
             "Their statistics (%u solved puzzle%s), solve history (%lu solve%s) and game in progress "
             "are erased from sudoku.db. This can't be undone.%s",
             (unsigned)pl->solved, pl->solved == 1 ? "" : "s",
             (unsigned long)pl->solves, pl->solves == 1 ? "" : "s",
             id == store_player_id() ? "\nAnother player takes over." : "");
    lv_obj_t *m = make_msgbox(title, text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_TRASH "  Delete", player_delete_confirm_cb, true);
    lv_obj_t *del = lv_obj_get_child(lv_msgbox_get_footer(m), -1);
    lv_obj_set_style_bg_color(del, C_RED, 0);
}

static void player_add_cb(lv_event_t *e)
{
    if (stats_db_ready()) nd_open(0, NULL);
}

/* one row per player; without sudoku.db just who the board thinks plays */
static void render_players(void)
{
    if (!p_list) return;
    lv_obj_clean(p_list);
    bool db = stats_db_ready() && p_count >= 0;
    int n = db ? p_count : 1;
    for (int i = 0; i < n; i++) {
        stats_player_t ram = {.id = store_player_id()};
        if (!db) snprintf(ram.name, sizeof(ram.name), "%s", store_player_name());
        const stats_player_t *pl = db ? &p_players[i] : &ram;
        bool cur = !db || pl->id == store_player_id();

        lv_obj_t *r = lv_obj_create(p_list);
        lv_obj_set_size(r, LV_PCT(100), 80);
        lv_obj_set_style_bg_color(r, C_CARD, 0);
        lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(r, cur ? 2 : 0, 0);
        lv_obj_set_style_border_color(r, C_ACCENT, 0);
        lv_obj_set_style_radius(r, 12, 0);
        lv_obj_set_style_pad_hor(r, 22, 0);
        lv_obj_set_scrollable(r, false);
        lv_obj_set_clickable(r, !cur);
        lv_obj_add_event_cb(r, player_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)pl->id);

        lv_obj_t *nm = make_label(r, &lv_font_montserrat_22, C_TEXT);
        lv_label_set_text(nm, pl->name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, -12);
        lv_obj_t *sub = make_label(r, &lv_font_montserrat_14, cur ? C_ACCENT : C_MUTED);
        if (cur && db) lv_label_set_text_fmt(sub, "Playing now   -   %u solved, %lu solves", (unsigned)pl->solved,
                                            (unsigned long)pl->solves);
        else if (cur) lv_label_set_text(sub, "Playing now");
        else lv_label_set_text_fmt(sub, "%u solved, %lu solves   -   tap to play", (unsigned)pl->solved,
                                   (unsigned long)pl->solves);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 0, 16);
        int right = 0;                           /* buttons from the right edge */
        if (db && n > 1) {                       /* the only player can't be deleted */
            lv_obj_t *del = make_btn(r, 0, 0, 56, 48, LV_SYMBOL_TRASH, &lv_font_montserrat_18,
                                     player_delete_cb, (void *)(intptr_t)pl->id);
            lv_obj_set_style_bg_color(del, C_CARD2, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(del, 0), C_RED, 0);
            lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
            right = 56 + 10;
        }
        if (cur && db) {
            lv_obj_t *rn = make_btn(r, 0, 0, 130, 48, LV_SYMBOL_EDIT "  Rename", &lv_font_montserrat_16,
                                    player_rename_cb, (void *)(intptr_t)pl->id);
            lv_obj_set_style_bg_color(rn, C_CARD2, 0);
            lv_obj_align(rn, LV_ALIGN_RIGHT_MID, -right, 0);
        }
    }
    lv_obj_set_state(p_add, LV_STATE_DISABLED, !stats_db_ready());
    lv_label_set_text(p_note, stats_db_ready()
        ? "Statistics and the game in progress are kept per player in sudoku.db."
        : "No sudoku.db: insert a FAT32 microSD card before switching on to keep players.");
}

static void profile_cb(lv_event_t *e)
{
    render_players();
    if (stats_db_ready()) stats_db_list_players();      /* fresh counts; drawn when they arrive */
    lv_screen_load(scr_profile);
}

static void build_profile(void)
{
    scr_profile = make_screen();
    make_btn(scr_profile, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_profile, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Players");
    lv_obj_set_pos(t, 150, 20);
    p_add = make_btn(scr_profile, SCR_W - 12 - 200, 10, 200, 52, LV_SYMBOL_PLUS "  Add player",
                     &lv_font_montserrat_18, player_add_cb, NULL);
    lv_obj_set_style_bg_opa(p_add, LV_OPA_50, LV_STATE_DISABLED);

    p_list = lv_obj_create(scr_profile);
    lv_obj_remove_style_all(p_list);
    lv_obj_set_pos(p_list, 40, 84);
    lv_obj_set_size(p_list, SCR_W - 80, SCR_W - 84 - 56);
    lv_obj_set_flex_flow(p_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p_list, 12, 0);
    lv_obj_set_scroll_dir(p_list, LV_DIR_VER);

    p_note = make_label(scr_profile, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(p_note, SCR_W - 80);
    lv_label_set_long_mode(p_note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(p_note, 44, SCR_W - 44);
    render_players();
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
    lv_obj_t *old_stats = scr_stats, *old_wifi = scr_wifi, *old_profile = scr_profile, *old_history = scr_history;
    lv_obj_t *old_plays = scr_plays;
    lv_obj_t *act = lv_screen_active();

    build_game();
    build_list();
    build_home();
    build_stats();
    build_settings();
    build_wifi();
    build_profile();
    build_history();
    build_plays();
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
                 : act && act == old_profile  ? scr_profile
                 : act && act == old_history  ? scr_history
                 : act && act == old_plays    ? scr_plays
                 : act && act == old_list     ? scr_list
                 : act && act == old_settings ? scr_settings
                 : scr_home;
    lv_screen_load(to);
    if (to == scr_plays) plays_ask(pl_offset);         /* fill the rebuilt list */

    /* the old screens may be running this very event: delete them later */
    if (old_home) lv_obj_delete_async(old_home);
    if (old_list) lv_obj_delete_async(old_list);
    if (old_settings) lv_obj_delete_async(old_settings);
    if (old_stats) lv_obj_delete_async(old_stats);
    if (old_wifi) lv_obj_delete_async(old_wifi);
    if (old_profile) lv_obj_delete_async(old_profile);
    if (old_history) lv_obj_delete_async(old_history);
    if (old_plays) lv_obj_delete_async(old_plays);
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
    lv_obj_t *nb = make_btn(scr_list, 290, 10, 190, 52, LV_SYMBOL_PLUS "  New puzzle", &lv_font_montserrat_18,
                            new_puzzle_cb, NULL);
    lv_obj_set_style_bg_color(nb, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(nb, 0), lv_color_white(), 0);

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
    lv_obj_t *b = make_btn(parent, 0, 0, HOME_COL_W, HOME_BTN_H, NULL, NULL, cb, NULL);
    lv_obj_t *l = make_label(b, &lv_font_montserrat_24, C_TEXT);
    lv_label_set_text(l, text);
    if (sub) {
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -12);
        *sub = make_label(b, &lv_font_montserrat_16, C_MUTED);
        lv_obj_set_width(*sub, LV_PCT(90));
        lv_obj_set_style_text_align(*sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(*sub, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(*sub, LV_ALIGN_CENTER, 0, 17);
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

    lv_obj_t *pw = make_btn(scr_home, 0, 0, 64, 52, LV_SYMBOL_POWER, &lv_font_montserrat_24,
                            power_off_cb, NULL);
    lv_obj_align(pw, LV_ALIGN_TOP_RIGHT, -12, 10);

    lv_obj_t *col = lv_obj_create(scr_home);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, HOME_GRID_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 40);   /* clear of the clock */

    lv_obj_t *t = make_label(col, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Sudoku");
    h_total = make_label(col, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_style_margin_bottom(h_total, 22, 0);

    /* the menu: one column, or a grid when they don't all fit (home_layout) */
    h_menu = lv_obj_create(col);
    lv_obj_remove_style_all(h_menu);
    lv_obj_set_size(h_menu, HOME_GRID_W, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(h_menu, HOME_GAP, 0);
    lv_obj_set_style_pad_column(h_menu, HOME_GAP, 0);
    h_menu_grid = -1;                                   /* not laid out yet */

    /* who is playing, first */
    lv_obj_t *pb = home_btn(h_menu, SYMBOL_USER "  Players", profile_cb, &h_player);   /* sub: "Playing as ..." */
    lv_obj_set_style_text_font(lv_obj_get_child(pb, 0), &font_icons_24, 0);

    /* hidden when there is no game in progress; the flex column closes up */
    h_continue = home_btn(h_menu, LV_SYMBOL_PLAY "  Continue", continue_cb, &h_continue_sub);
    lv_obj_set_style_bg_color(h_continue, C_LIST_ACTIVE, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_continue, 0), lv_color_white(), 0);
    lv_obj_set_style_text_color(h_continue_sub, lv_color_white(), 0);
    lv_obj_set_style_text_opa(h_continue_sub, LV_OPA_80, 0);

    home_btn(h_menu, LV_SYMBOL_LIST "  Puzzles", puzzles_cb, NULL);
    h_daily = home_btn(h_menu, LV_SYMBOL_BELL "  Daily puzzle", daily_cb, &h_daily_sub);
    lv_obj_set_style_bg_opa(h_daily, LV_OPA_40, LV_STATE_DISABLED);       /* greyed out until the clock is set */
    lv_obj_set_style_text_opa(h_daily, LV_OPA_40, LV_STATE_DISABLED);
    home_btn(h_menu, LV_SYMBOL_BARS "  Statistics", stats_cb, NULL);
    lv_obj_t *hb = home_btn(h_menu, SYMBOL_CALENDAR "  History", history_cb, NULL);   /* the calendar of games */
    lv_obj_set_style_text_font(lv_obj_get_child(hb, 0), &font_icons_24, 0);
    home_btn(h_menu, LV_SYMBOL_VIDEO "  Gameplays", plays_cb, NULL);
    home_btn(h_menu, LV_SYMBOL_SETTINGS "  Settings", settings_cb, NULL);

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
    lv_obj_t *info = make_btn(scr_stats, 300, 10, 52, 52, SYMBOL_INFO, &font_icons_24, stats_info_cb, NULL);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(info, 0), C_ACCENT, 0);

    lv_obj_t *list = make_coll_column(scr_stats);
    x_btn[0] = make_coll_btn(list, "All levels", &x_name_lbl[0], &x_cnt_lbl[0], stats_row_cb, 0);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        x_btn[c + 1] = make_coll_btn(list, puzzle_collections[c].name, &x_name_lbl[c + 1], &x_cnt_lbl[c + 1],
                                     stats_row_cb, c + 1);
    }

    int px = PANE_X, pw = PANE_W;
    /* top bar, right: whose statistics, as a dropdown */
    x_player_dd = lv_dropdown_create(scr_stats);
    lv_obj_set_pos(x_player_dd, SCR_W - 12 - 250, 10);
    lv_obj_set_size(x_player_dd, 250, 52);
    lv_obj_set_style_text_font(x_player_dd, &font_icons_20, 0);
    lv_obj_set_style_text_color(x_player_dd, C_TEXT, 0);
    lv_obj_set_style_bg_color(x_player_dd, C_CARD, 0);
    lv_obj_set_style_border_width(x_player_dd, 0, 0);
    lv_obj_set_style_shadow_width(x_player_dd, 0, 0);
    lv_obj_set_style_radius(x_player_dd, 8, 0);
    lv_obj_set_style_pad_hor(x_player_dd, 14, 0);
    lv_obj_set_style_pad_ver(x_player_dd, 15, 0);
    lv_obj_t *pdl = lv_dropdown_get_list(x_player_dd);
    lv_obj_set_style_text_font(pdl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pdl, C_TEXT, 0);
    lv_obj_set_style_text_line_space(pdl, 22, 0);
    lv_obj_set_style_bg_color(pdl, C_CARD2, 0);
    lv_obj_set_style_border_width(pdl, 0, 0);
    lv_obj_set_style_radius(pdl, 8, 0);
    lv_obj_set_style_max_height(pdl, 480, 0);
    lv_obj_set_style_bg_color(pdl, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(pdl, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_add_event_cb(x_player_dd, stats_player_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
    stats_player_dd_update();
    x_title = make_label(scr_stats, &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(x_title, px + 4, 80);
    x_sum1 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum1, px + 4, 110);
    x_sum2 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum2, px + 4, 130);

    x_table = lv_table_create(scr_stats);
    lv_obj_set_pos(x_table, px, 158);
    lv_obj_set_size(x_table, pw, SCR_W - 158 - 12);
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

}

/* one settings row: a card with a title + hint on the left; returns the card
 * so the caller can put a control on its right */
static lv_obj_t *settings_row(lv_obj_t *parent, const char *title, const char *hint)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, LV_PCT(100), 76);
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
    lv_obj_set_size(col, SCR_W - 80, SCR_W - 84);         /* more rows than fit: scrolls */
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 10, 0);
    lv_obj_set_style_pad_bottom(col, 16, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);

    lv_obj_t *r = settings_row(col, "Dark mode", "Light text on a dark background");
    st_theme_sw = settings_switch(r, s_dark, theme_sw_cb);

    r = settings_row(col, "Brightness", "Screen backlight");
    st_bright_sl = settings_slider(r, 10, 100, store_brightness(), bright_sl_cb, &st_bright_lbl);

    r = settings_row(col, "Sound effects", "Taps, mistakes, finished digits, solved");
    st_sound_sw = settings_switch(r, sfx_enabled(), sound_sw_cb);

    r = settings_row(col, "Volume", "Speaker loudness");
    st_vol_sl = settings_slider(r, 0, 100, store_volume(), vol_sl_cb, &st_vol_lbl);

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

    r = settings_row(col, "microSD card", "");
    st_sd_hint = lv_obj_get_child(r, 1);           /* status of sudoku.db, see clock_cb */
    char sd[64];
    stats_db_status(sd, sizeof(sd));
    lv_label_set_text(st_sd_hint, sd);
    st_sd_btn = make_btn(r, 0, 0, 160, 52, "Format", &lv_font_montserrat_18, sd_format_cb, NULL);
    lv_obj_set_style_bg_color(st_sd_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_sd_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_sd_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    if (s_sd_state == SD_FORMATTING) sd_btn_text("Formatting...", false);

    r = settings_row(col, "USB drive", "");
    st_usb_hint = lv_obj_get_child(r, 1);
    st_usb_btn = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_USB "  Connect", &lv_font_montserrat_18, usb_connect_cb, NULL);
    lv_obj_set_style_bg_color(st_usb_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_usb_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(st_usb_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_usb_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    usb_btn_update();

    r = settings_row(col, "Factory reset", "Settings, Wi-Fi network, players and statistics");
    lv_obj_t *fr = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_WARNING "  Reset", &lv_font_montserrat_18,
                            factory_reset_cb, NULL);
    lv_obj_set_style_bg_color(fr, C_RED, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(fr, 0), lv_color_white(), 0);
    lv_obj_align(fr, LV_ALIGN_RIGHT_MID, 0, 0);
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
        bsp_display_brightness_set(store_brightness());
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
    g_undo = heap_caps_calloc(UNDO_MAX, sizeof(board_t), MALLOC_CAP_SPIRAM);
    if (!g_undo) return ESP_ERR_NO_MEM;
    sudoku_geom_init();                             /* before the generator task can use the tables */
    g_geo = sudoku_geom(VAR_CLASSIC);
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(NULL);
#endif

    /* splash while the card and sudoku.db are read (a second or two) */
    lv_obj_t *boot = lv_screen_active();
    lv_obj_set_style_bg_color(boot, lv_color_hex(PAL_DARK.bg), 0);
    lv_obj_set_style_bg_opa(boot, LV_OPA_COVER, 0);
    lv_obj_t *splash = lv_label_create(boot);
    lv_label_set_text(splash, "Sudoku\n\nStarting...");
    lv_obj_set_style_text_align(splash, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(splash, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(splash, lv_color_hex(PAL_DARK.text), 0);
    lv_obj_center(splash);
    lv_refr_now(NULL);

    /* The settings and the game in progress live in sudoku.db, so the card
     * comes first. Card setup also has to happen before Wi-Fi starts (they
     * share the SDMMC host, see the microSD section). */
    s_sd_present = bsp_sdcard_probe(&s_sd_bytes, s_sd_name) == ESP_OK;
    if (s_sd_present) {
        char sz[24];
        fmt_gb(sz, sizeof(sz), s_sd_bytes);
        printf("microSD: \"%s\", %s\n", s_sd_name, sz);
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
    if (s_sd_present && usb_msc_start() != ESP_OK) {   /* empty drive until Connect; tells us a computer is there */
        printf("usb_msc: USB didn't start\n");
    }
    wifi_mgr_start();               /* reconnects to the saved network if Wi-Fi was left on */

    sfx_set_enabled(store_sound_enabled());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(store_brightness());
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        lv_indev_add_event_cb(in, touch_sound_cb, LV_EVENT_PRESSED, NULL);
    }
    tz_apply(store_timezone());
    l_coll = store_last_collection();
    s_sleep_min = store_screen_timeout_min();
    apply_theme(store_dark_mode());
    lv_obj_delete(splash);

    g_last_tick = lv_tick_get();
    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(saver_cb, 1000, NULL);
    lv_timer_create(clock_cb, 1000, NULL);
    lv_timer_create(sd_poll_cb, 100, NULL);
    clock_cb(NULL);
    return ESP_OK;
}

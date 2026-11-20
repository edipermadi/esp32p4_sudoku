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
 * black and the backlight turns off. The next tap only wakes it.
 */
#include "sudoku_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
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
#include "sysmon_ui.h"
#include "file_ui.h"
#include "timezones.h"
#include "stats_db.h"
#include "usb_msc.h"
#include "generator.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "video_enc.h"
#include "gibberlink.h"
#include "driver/jpeg_encode.h"
#include <sys/stat.h>
#include <unistd.h>
#include "moves.h"
#include "sdx.h"
#include "avatars.h"
#include "qrcode/qrcodegen.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_heap_caps.h"

/* Background work (generator, Learn, the puzzle check, firmware update, card
 * format) runs on core 0: LVGL runs in loopTask on core 1, and core 0 otherwise
 * only has the system's small tasks. Unpinned tasks would not stay so: on the
 * ESP32-P4 a task is pinned to the core it is on when it first uses the FPU. */
#define WORKER_CORE 0

/* "Screen off" dropdown choices, in minutes (0 = never) */
static const uint8_t SLEEP_MIN[] = {1, 2, 5, 10, 15, 30, 0};
#define SLEEP_OPTIONS "1 min\n2 min\n5 min\n10 min\n15 min\n30 min\nNever"
#define SLEEP_COUNT   (sizeof(SLEEP_MIN) / sizeof(SLEEP_MIN[0]))

LV_FONT_DECLARE(font_digits_44)   /* " 0-9", grid digits */
LV_FONT_DECLARE(font_digits_52)   /* the same, larger (Settings > Display > Digit size) */
LV_FONT_DECLARE(font_digits_96)   /* the same, for the Kids 4x4 board's big cells */
#define DIGIT_FONT (store_big_digits() ? &font_digits_52 : &font_digits_44)
#define DIGIT_FONT_N(n) ((n) == 4 ? &font_digits_96 : DIGIT_FONT)   /* for a board of side n */
LV_FONT_DECLARE(font_timer_36)    /* " 0-9:", game timer */
LV_FONT_DECLARE(font_icons_24)    /* the two icons below + Montserrat 24 as fallback */
LV_FONT_DECLARE(font_icons_20)    /* the same at 20 px + Montserrat 20 */
#define SYMBOL_USER     "\xEF\x80\x87"   /* FontAwesome U+F007 */
#define SYMBOL_CALENDAR "\xEF\x81\xB3"   /* FontAwesome U+F073 */
#define SYMBOL_INFO     "\xEF\x81\x9A"   /* FontAwesome U+F05A */
#define SYMBOL_TROPHY   "\xEF\x82\x91"   /* FontAwesome U+F091 */
#define SYMBOL_LOCK     "\xEF\x80\xA3"   /* FontAwesome U+F023 */
#define SYMBOL_STAR     "\xEF\x80\x85"   /* FontAwesome U+F005 */
#define SYMBOL_CHART    "\xEF\x88\x81"   /* FontAwesome U+F201 (chart-line) */
#define SYMBOL_BULB     "\xEF\x83\xAB"   /* FontAwesome U+F0EB (lightbulb) */
#define SYMBOL_QR       "\xEF\x80\xA9"   /* FontAwesome U+F029 (qrcode) */
#define SYMBOL_CAMERA   "\xEF\x80\xB0"   /* FontAwesome U+F030 (camera): screenshots */
#define SYMBOL_MORE     "\xEF\x85\x82"   /* FontAwesome U+F142 (ellipsis-v): a context menu (font_icons_24 only) */

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
static palette_t s_pal;                   /* the active scheme with the chosen accent (palette_build) */

/* accent colours (Settings): dark-mode and light-mode shades; 0 = the schemes' own blue */
static const uint32_t ACCENTS[ACCENT_COUNT][2] = {
    {0x4F8CFF, 0x2F6FE0}, {0x2DD4BF, 0x0D9488}, {0x4ADE80, 0x16A34A}, {0xA78BFA, 0x7C3AED}, {0xF472B6, 0xDB2777},
};
static const char *const ACCENT_NAMES = "Blue\nTeal\nGreen\nPurple\nPink";

/* Night mode (Settings > Display): between the night hours, dark mode and the backlight
 * at most the night brightness. Only the switch-over changes things (night_check), so
 * dark mode or brightness changed by hand during the night stay as they are until morning.
 * s_night: the night look is on now. */
static bool s_night;
static lv_obj_t *s_saver;   /* black cover on the system layer while the screen is off */

/* the backlight for "on": the day's setting, or less at night */
static int effective_brightness(void)
{
    int b = store_brightness();
    if (s_night && store_night_brightness() < b) b = store_night_brightness();
    return b;
}

static uint32_t mix_hex(uint32_t a, uint32_t b, int pct)   /* pct % of a over b */
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (uint32_t)((ca * pct + cb * (100 - pct)) / 100) << sh;
    }
    return out;
}

/* the dark or light scheme, with the accent and the cell highlights made from it */
static void palette_build(bool dark, int accent)
{
    s_pal = dark ? PAL_DARK : PAL_LIGHT;
    if (accent > 0 && accent < ACCENT_COUNT) {
        uint32_t a = ACCENTS[accent][dark ? 0 : 1];
        s_pal.accent = a;
        s_pal.cell_sel = mix_hex(a, s_pal.cell, dark ? 55 : 45);
        s_pal.cell_same = mix_hex(a, s_pal.cell, 25);
        s_pal.cell_peer = mix_hex(a, s_pal.cell, dark ? 8 : 10);
        s_pal.digit_user = dark ? mix_hex(a, 0xFFFFFF, 75) : a;
    }
    if (store_cvd()) {
        /* Colour-blind friendly (Settings > Display): the Okabe-Ito colours. Red and green
         * look alike to most colour-blind people; vermillion (mistakes), bluish green (solved,
         * complete) and yellow-orange (in progress) stay apart, and apart from the blues,
         * in lightness as well as hue. */
        if (dark) {
            s_pal.red = 0xFF8A3D;         s_pal.green = 0x3CC9A7;     s_pal.orange = 0xF2B638;
            s_pal.cell_bad = 0x55300F;    s_pal.cell_solved = 0x113D36;
            s_pal.list_solved = 0x0E7D66; s_pal.list_active = 0xA87C10;
        } else {
            s_pal.red = 0xD55E00;         s_pal.green = 0x00876A;     s_pal.orange = 0xC98A00;
            s_pal.cell_bad = 0xFBDCC4;    s_pal.cell_solved = 0xCDEFE6;
            s_pal.list_solved = 0x009E73; s_pal.list_active = 0xE69F00;
        }
    }
    ui_pal = &s_pal;
}

/* the players' colours (by their names on leaderboards) */
static lv_color_t player_color(int k)
{
    static const uint32_t C[STATS_PLAYER_COLORS] = {0x3E7BFA, 0xE5484D, 0x30A46C, 0xF5A524,
                                                    0x9E6CF0, 0xE93D82, 0x12A594, 0x8B7355};
    return lv_color_hex(C[(k % STATS_PLAYER_COLORS + STATS_PLAYER_COLORS) % STATS_PLAYER_COLORS]);
}

/* a small round dot in a player's colour */
static lv_obj_t *player_dot(lv_obj_t *parent, int color, int size)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, size, size);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, player_color(color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_clickable(d, false);
    return d;
}
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
#define TOOL_Y      62      /* the game's second row: ?, Hint, pause and timer */
#define GRID_Y      116     /* the board, under that row */
#define GRID_SIZE   552     /* 9x9: 57 px cells (board_layout) */
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
    uint8_t  colors[81];         /* cell colours, 0 = none, 1..CELL_COLORS (long press a cell) */
} board_t;

static uint16_t g_id = STORE_NO_GAME;
static int      g_var = VAR_CLASSIC;     /* the puzzle's variant (generator.h) */
static const sudoku_geom_t *g_geo;       /* and its geometry: units, peers, size */
static sudoku_geom_t *g_geo_buf;         /* PSRAM: a Jigsaw puzzle's geometry (its regions) */
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
static uint32_t g_flags;         /* GAME_FLAG_* (store.h): the no-notes challenge, techniques the hints showed */
static lv_obj_t *g_auto_btn, *g_win_badge, *g_win_share;
static uint8_t  g_sol[81];       /* the solution, for Fill */
static game_move_t *g_moves;     /* move log of this attempt (moves.h), PSRAM, GAME_MOVES_MAX */
static uint8_t  h_mark[81];      /* hint highlight: 0, 1 = where to look, 2 = the cell */
static int      h_step;          /* 0 = no hint open, 1 = nudge shown, 2 = reason shown */
static sudoku_step_t h_info;
static int      g_practice = -1;  /* Learn: the technique being practised (LEARN_*), -1 = a real game */
/* Time attack (Home): Easy puzzles one after another for 10 minutes. Its games
 * aren't kept (no Continue); its solves count as usual. */
#define ATK_MS (10u * 60u * 1000u)
static bool     g_attack;         /* this game is part of a run */
static uint32_t atk_used_ms;      /* the run's earlier puzzles */
static int      atk_solved, atk_mistakes;
static uint16_t atk_last;
static bool     g_starred;        /* the puzzle is bookmarked (the star in the top bar) */
static lv_obj_t *g_star_btn;
static lv_obj_t *g_save_btn;      /* put it in a saved collection (generated and typed-in puzzles) */
static lv_obj_t *tour_ovl;        /* the game screen's tour, while it's up */
static void atk_open_next(void);
static void atk_finish(void);
static void tour_maybe(void);
static bool hint_nudge(void);
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
static board_t *g_redo;          /* undone boards, newest last, PSRAM; any new edit empties it */
static int      g_redo_len;
static int      g_lock;          /* digit-first: the digit locked on the pad (long press), 0 = none */

/* ------------------------------------------------------------- widgets */
static lv_obj_t *scr_home, *scr_list, *scr_stats, *scr_settings, *scr_wifi, *scr_profile, *scr_history, *scr_game;
static lv_obj_t *scr_sysmon, *scr_files;
static lv_obj_t *scr_plays;               /* Gameplays: recorded solves to replay */
static lv_obj_t *scr_trophy;              /* Trophies: achievements, daily leaderboard */

/* home screen */
#define HOME_COL_W      440      /* menu button, one column */
#define HOME_GRID_W     600      /* menu width as a grid (two per row) */
#define HOME_BTN_H      80
#define HOME_GAP        14
#define HOME_MENU_MAX_H 470      /* room under the clock, title and count */
static lv_obj_t *h_menu;                  /* the pages, side by side: swipe between them */
static int h_menu_grid;                   /* layout signature (home_layout), -1 = not laid out */
#define HOME_PAGES_MAX 3
static lv_obj_t *h_page[HOME_PAGES_MAX], *h_dots;
static lv_obj_t *h_items[16];             /* the menu buttons, in order */
static int h_nitems, h_npages;
static lv_obj_t *h_total, *h_continue, *h_continue_sub, *h_clock, *h_date, *h_player, *h_daily_sub, *h_daily;
static lv_obj_t *h_player_title, *h_player_avatar;   /* the Players button's title: the avatar, or the icon */
static void home_player_update(void);

/* history screen */
static lv_obj_t *hz_cal, *hz_title, *hz_list;
static lv_calendar_date_t hz_marked[31];  /* highlighted days (the calendar keeps the pointer) */
static int hz_year, hz_month;             /* month asked for / shown, 0 = not yet */
static uint8_t hz_count[31];              /* games per day of the highlighted month (badges) */
static lv_calendar_date_t hz_sel;         /* the day tapped (its list is shown), year 0 = none */
static bool hz_latest_pending;            /* asked for "the latest month"; show what comes back */

/* players screen */
static lv_obj_t *p_list, *p_menu_btn, *p_note;
static stats_player_t p_players[STATS_MAX_PLAYERS];
static int p_count = -1;                 /* last list from sudoku.db, -1 = none yet */

/* a player in that list, by id (non-zero) or else by name (names are unique, ignoring case);
 * NULL: not there (yet) */
static const stats_player_t *player_find(int32_t id, const char *name)
{
    for (int i = 0; i < p_count; i++) {
        if (id ? p_players[i].id == id : (name && !strcasecmp(p_players[i].name, name))) return &p_players[i];
    }
    return NULL;
}

/* A player's avatar shrunk to size px or, without one, a dot (dot px) of their colour, centred
 * in a size x size box that takes no taps. color < 0: the colour from the player list. */
static lv_obj_t *player_badge(lv_obj_t *parent, const stats_player_t *pl, int color, int size, int dot)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    if (color < 0) color = pl ? pl->color : 0;
    if (pl && pl->avatar && pl->avatar <= AVATAR_COUNT) {
        lv_obj_t *im = lv_image_create(box);
        lv_image_set_src(im, &avatar_images[pl->avatar - 1]);
        if (size < AVATAR_SIZE) lv_image_set_scale(im, (uint32_t)(256 * size / AVATAR_SIZE));
        lv_obj_center(im);
    } else {
        lv_obj_center(player_dot(box, color, dot));
    }
    return box;
}

/* statistics screen: row 0 of the left column is "All levels" */
#define X_ROWS (PUZZLE_COLLECTION_COUNT + 1 + KIND_COUNT)   /* all, collections, kinds made on the board */
static lv_obj_t *x_btn[X_ROWS], *x_name_lbl[X_ROWS], *x_cnt_lbl[X_ROWS];
static lv_obj_t *x_title, *x_sum1, *x_sum2, *x_table, *x_empty, *x_player_dd;
static lv_obj_t *x_chart, *x_plot, *x_trend, *x_chart_btn;   /* progress chart (instead of the table) */
static bool      x_chart_mode;            /* chart shown (kept until power-off) */
static bool      x_from_db;               /* what the info popup says the numbers come from */
static char      x_player_txt[STORE_NAME_LEN + 8];    /* closed dropdown: icon + name */
static lv_obj_t *x_player_av;                         /* ... or the avatar left of the name */
static int       x_sel;          /* 0 = all levels, 1.. = collection x_sel-1, then the kinds (STATS_LEVEL_KIND) */

/* settings screen */
static lv_obj_t *st_theme_sw, *st_sound_sw, *st_sleep_dd, *st_wifi_lbl, *st_tz_dd;
static lv_obj_t *st_wifi_eye;               /* shows the network's name (hidden until tapped) */
static lv_obj_t *st_lock_dd, *st_pin_btn;   /* the lock screen: Off / Swipe / PIN, and Change PIN */
static void lock_dd_cb(lv_event_t *e);
static void pin_change_cb(lv_event_t *e);
static void night_check(lv_timer_t *t);
static void apply_theme(bool dark);
static lv_obj_t *st_night_bright_lbl;

static void game_layout(void);

static void big_digits_dd_cb(lv_event_t *e);

static void left_hand_sw_cb(lv_event_t *e)
{
    store_set_left_hand(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    game_layout();                                  /* the game screen moves its board and pad now */
}

static void no_notes_sw_cb(lv_event_t *e)
{
    store_set_no_notes(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));   /* (games started from now) */
}

static void cvd_sw_cb(lv_event_t *e)
{
    store_set_cvd(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    apply_theme(s_dark);                            /* every screen again, in the new colours */
}

static void night_sw_cb(lv_event_t *e)
{
    store_set_night_mode(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    night_check(NULL);                              /* at once (also off: back to the day look) */
}

static lv_obj_t *st_night_from, *st_night_to;

static void night_hours_cb(lv_event_t *e)
{
    store_set_night_hours((uint8_t)lv_dropdown_get_selected(st_night_from),
                          (uint8_t)lv_dropdown_get_selected(st_night_to));
    night_check(NULL);
}

static void night_bright_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(st_night_bright_lbl, "%d%%", v);
    if (s_night) bsp_display_brightness_set(v < store_brightness() ? v : store_brightness());   /* see it now */
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) store_set_night_brightness((uint8_t)v);
}

static void saver_clock_sw_cb(lv_event_t *e)
{
    store_set_saver_clock(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}
static bool      st_wifi_reveal;            /* name shown; hidden again each time Settings opens */
static lv_obj_t *st_bright_sl, *st_bright_lbl, *st_vol_sl, *st_vol_lbl, *st_assist_dd;
static char      st_tz_txt[48];     /* closed dropdown shows just the IANA name */
static lv_obj_t *st_sd_btn, *st_sd_hint;
static lv_obj_t *u_ovl;                   /* "USB drive" panel while the computer has the card */
static lv_obj_t *st_usb_btn, *st_usb_hint;
static bool u_reloading;                  /* card back / reset: waiting for sudoku.db to (re)open */
static bool u_reload_home;                /* ... then show Home (factory reset) */

/* list screen */
#define L_ICON_W  52                        /* the title row's icon buttons: Starred (a toggle) and the menu */
#define L_PANE_Y  72                        /* the two panes, under the title row: collections left, puzzles right */
#define L_TAB_H   48                        /* Built-in | Saved, over the collection column */
#define L_GRID_Y  (L_PANE_Y + 62)           /* the right pane's grid or rows, under name and stats */
static lv_obj_t *l_total, *l_colls;
#define L_COLLS (PUZZLE_COLLECTION_COUNT + STATS_MAX_IMPORTS + STATS_MAX_SAVED)   /* built-in, imported, saved */
static lv_obj_t *l_coll_btn[L_COLLS], *l_coll_cnt[L_COLLS];
static lv_obj_t *l_coll_name[L_COLLS];
static uint8_t   l_solved_bits[16];       /* an imported or saved collection's solved puzzles (sudoku.db) */
/* collections imported from puzzles/ on the card (stats_db_import_puzzles) */
static stats_import_t s_imports[STATS_MAX_IMPORTS];
static int s_nimports;
/* collections made on the board (the game screen's Save), after the imported ones */
static stats_saved_t *s_saved;            /* STATS_MAX_SAVED of them, PSRAM */
static int s_nsaved;
static int g_from_coll = -1;              /* the collection the game was opened from (Next, back to the list) */
/* Puzzles' tabs: 0 the built-in collections, 1 the player's (imported and saved) */
static int l_tab;
static int l_tab_coll[2] = {0, -1};       /* the collection last shown in each tab */
static lv_obj_t *l_tab_btn[2], *l_empty, *l_export, *l_del;
static lv_obj_t *l_list;                  /* the Saved tab: a collection's puzzles as rows */
static lv_obj_t *l_name, *l_stats, *l_btnm;
static int       l_coll;
static char      l_labels[MAX_PER_COLL][4];
static const char *l_map[MAX_PER_COLL + MAX_PER_COLL / 10 + 1];
static uint16_t  l_pending_id;

/* game screen */
static lv_obj_t *g_grid, *g_boxes[9], *g_cells[81], *g_cell_lbl[81];
static lv_obj_t *g_clues[18];                       /* Sandwich: the sums by the rows and columns */
static lv_obj_t *g_pad[9], *g_pad_cnt[9];
static int       g_layout_var = -1;     /* variant the board and pad are laid out for */
static bool      g_layout_left;         /* ... and on which side the pad is (Left-handed) */
static lv_obj_t *g_title, *g_best, *g_timer, *g_timer_icon, *g_status, *g_notes_btn;
static lv_obj_t *g_pause_ovl, *g_win_ovl, *g_win_time, *g_win_best, *g_win_miss, *g_win_hint;
static lv_obj_t *g_hint_cnt;     /* hints used, on the Fill button */
static lv_obj_t *g_win_h2h;      /* Solved dialog: how others did on this puzzle */
static lv_obj_t *g_win_h2h_row;  /* ... in a row with the leader's avatar */
static lv_obj_t *g_undo_btn, *g_redo_btn;
static lv_obj_t *g_hint_btn, *g_bulb_btn, *g_timer_btn, *g_fill_btn;   /* (the tour points at them) */

static void show_list(void);
static void open_puzzle(uint16_t id);
static void list_fill_colls(void);
static void list_fill_rows(int lc);
static void list_reveal_coll(lv_anim_enable_t anim);
static void refresh_list(void);
static void nd_open_ex(const char *title, const char *current, int max_len, const char *ok_text,
                       void (*done)(const char *name));
/* Learn: the techniques there are practice puzzles for */
enum { LEARN_HIDDEN = 0, LEARN_NAKED, LEARN_POINTING, LEARN_CLAIMING, LEARN_NAKED_PAIR, LEARN_HIDDEN_PAIR,
       LEARN_NAKED_TRIPLE, LEARN_X_WING, LEARN_SWORDFISH, LEARN_XY_WING,
       LEARN_CAGE, LEARN_THERMO, LEARN_SANDWICH, LEARN_CONSEC, LEARN_COUNT };   /* (the last four: variant rules) */
static const char *const LEARN_NAMES[LEARN_COUNT] = {"Hidden single", "Naked single", "Pointing", "Claiming",
                                                     "Naked pair", "Hidden pair", "Naked triple", "X-Wing",
                                                     "Swordfish", "XY-Wing", "Killer cage", "Thermometer",
                                                     "Sandwich clue", "Consecutive bars"};
static void daily_kind_text(int variant, int level, char *buf, size_t n);
/* the current player's unfinished games in sudoku.db (most recent first), refreshed with the list and home */
static stats_game_row_t s_games[STATS_MAX_GAMES];
static int s_ngames;
static void games_refresh(void);
static bool game_in_progress(uint16_t id);
static void render_players(void);
static void usb_finish(void);
static void usb_btn_update(void);
static void history_poll(void);
static void history_cb(lv_event_t *e);
static void apply_loaded_prefs(void);
static void win_replay_cb(lv_event_t *e);
static void screenshot_cb(lv_event_t *e);
static void plays_poll(void);
static void trophy_poll(void);
static void lock_now_cb(lv_event_t *e);
static void trophy_toast_poll(void);
static void tr_ask(void);
static void build_trophies(void);
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

/* "st", "nd", "rd" or "th" after n */
static const char *ordinal(int n)
{
    int t = n % 100;
    if (t >= 11 && t <= 13) return "th";
    return n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
}

/* ------------------------------------------------------------- generated puzzles */
/* The puzzles made on the board live in sudoku.db (stats_db_gen_*); the last
 * few are cached here, and without a card that's the only place they are. */
#define GEN_CACHE 24
typedef struct {
    uint16_t id;                        /* 0 = free slot */
    uint8_t variant;                    /* sudoku_variant_t */
    uint8_t level;
    uint8_t regions[81];                /* Jigsaw: the region map */
    char cages[STATS_CAGES_LEN];        /* Killer: the cages (sudoku_cages_text()), "" = none */
    char marks[82];                     /* Even-Odd / Consecutive: the marks (sudoku_marks_text()), "" = none */
    char thermos[STATS_THERMOS_LEN];    /* Thermo: the thermometers (sudoku_thermos_text()), "" = none */
    char source[STATS_IMPORT_NAME];     /* a puzzle typed in: its collection ("My puzzles"), "" = none */
    int number;                         /* and its number there */
    uint8_t givens[81];
    char day[12];                       /* daily puzzle's date, "" otherwise */
} gen_cache_t;
static gen_cache_t *s_gen_cache;          /* GEN_CACHE entries, PSRAM */
static int s_gen_cache_next;
static uint16_t s_gen_ram_next = PUZZLE_GEN_RAM_BASE;

static void gen_cache_put(uint16_t id, int variant, int level, const uint8_t givens[81], const uint8_t *regions,
                          const char *cages, const char *marks, const char *thermos, const char *day)
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id) return;
    }
    s_gen_cache[s_gen_cache_next].id = id;
    if (regions) memcpy(s_gen_cache[s_gen_cache_next].regions, regions, 81);
    else memset(s_gen_cache[s_gen_cache_next].regions, 0, 81);
    snprintf(s_gen_cache[s_gen_cache_next].cages, STATS_CAGES_LEN, "%s", cages ? cages : "");
    snprintf(s_gen_cache[s_gen_cache_next].marks, 82, "%s", marks ? marks : "");
    snprintf(s_gen_cache[s_gen_cache_next].thermos, STATS_THERMOS_LEN, "%s", thermos ? thermos : "");
    s_gen_cache[s_gen_cache_next].source[0] = 0;
    s_gen_cache[s_gen_cache_next].number = 0;
    s_gen_cache[s_gen_cache_next].variant = (uint8_t)variant;
    s_gen_cache[s_gen_cache_next].level = (uint8_t)level;
    memcpy(s_gen_cache[s_gen_cache_next].givens, givens, 81);
    snprintf(s_gen_cache[s_gen_cache_next].day, sizeof(s_gen_cache[0].day), "%s", day ? day : "");
    s_gen_cache_next = (s_gen_cache_next + 1) % GEN_CACHE;
}

static void gen_cache_source(uint16_t id, const char *source, int number)
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id != id) continue;
        snprintf(s_gen_cache[i].source, sizeof(s_gen_cache[i].source), "%s", source);
        s_gen_cache[i].number = number;
    }
}

/* a typed-in puzzle's collection and number from the cache (not imported ones: import_of) */
static bool gen_own_name(uint16_t id, char *buf, size_t n)
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id && s_gen_cache[i].source[0]) {
            snprintf(buf, n, "%s #%d", s_gen_cache[i].source, s_gen_cache[i].number);
            return true;
        }
    }
    return false;
}

/* givens, variant, level and daily date of a generated puzzle (any may be NULL); false if unknown */
static bool gen_lookup_full(uint16_t id, uint8_t *givens, uint8_t *regions, char *cages, char *marks, char *thermos,
                            int *variant, int *level, char day[12])
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id) {
            if (givens) memcpy(givens, s_gen_cache[i].givens, 81);
            if (regions) memcpy(regions, s_gen_cache[i].regions, 81);
            if (cages) snprintf(cages, STATS_CAGES_LEN, "%s", s_gen_cache[i].cages);
            if (marks) snprintf(marks, 82, "%s", s_gen_cache[i].marks);
            if (thermos) snprintf(thermos, STATS_THERMOS_LEN, "%s", s_gen_cache[i].thermos);
            if (variant) *variant = s_gen_cache[i].variant;
            if (level) *level = s_gen_cache[i].level;
            if (day) snprintf(day, 12, "%s", s_gen_cache[i].day);
            return true;
        }
    }
    if (id >= PUZZLE_GEN_RAM_BASE) return false;          /* RAM-only, and gone */
    uint8_t g[81], reg[81] = {0};
    static char *cg;                                     /* STATS_CAGES_LEN, PSRAM (LVGL task only) */
    if (!cg && !(cg = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) return false;
    int lv, var = VAR_CLASSIC;
    char d[12];
    char mk[82] = "";
    static char th[STATS_THERMOS_LEN];                   /* (LVGL task only) */
    th[0] = 0;
    if (!stats_db_gen_get(id - PUZZLE_GEN_BASE, g, reg, cg, mk, th, &var, &lv, d)) return false;
    if (var < 0 || var >= VAR_COUNT) var = VAR_CLASSIC;
    gen_cache_put(id, var, lv, g, reg, cg, mk, th, d);
    if (var == VAR_CLASSIC && !d[0]) {              /* typed in on the board ("My puzzles #3")? */
        char src[STATS_IMPORT_NAME];
        int num;
        if (stats_db_gen_source(id - PUZZLE_GEN_BASE, src, &num)) gen_cache_source(id, src, num);
    }
    if (marks) snprintf(marks, 82, "%s", mk);
    if (thermos) snprintf(thermos, STATS_THERMOS_LEN, "%s", th);
    if (givens) memcpy(givens, g, 81);
    if (regions) memcpy(regions, reg, 81);
    if (cages) snprintf(cages, STATS_CAGES_LEN, "%s", cg);
    if (variant) *variant = var;
    if (level) *level = lv;
    if (day) snprintf(day, 12, "%s", d);
    return true;
}

static bool gen_lookup_day(uint16_t id, uint8_t *givens, int *variant, int *level, char day[12])
{
    return gen_lookup_full(id, givens, NULL, NULL, NULL, NULL, variant, level, day);
}

static bool gen_lookup(uint16_t id, uint8_t *givens, int *variant, int *level)
{
    return gen_lookup_full(id, givens, NULL, NULL, NULL, NULL, variant, level, NULL);
}

/* variant of any puzzle: the built-in ones are classic */
static int puzzle_variant(uint16_t id)
{
    int v = VAR_CLASSIC;
    if (puzzle_is_generated(id)) gen_lookup(id, NULL, &v, NULL);
    return v;
}

/* The board of any puzzle: a fixed variant's geometry, or for a Jigsaw
 * puzzle its regions built into buf. *variant (may be NULL) is set too. */
static const sudoku_geom_t *puzzle_geom(uint16_t id, sudoku_geom_t *buf, int *variant)
{
    int v = VAR_CLASSIC;
    uint8_t reg[81];
    static char *cages;                             /* STATS_CAGES_LEN, PSRAM (LVGL task only) */
    if (!cages && !(cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) return sudoku_geom(VAR_CLASSIC);
    cages[0] = 0;
    char marks[82] = "";
    static char thermos[STATS_THERMOS_LEN];         /* (LVGL task only) */
    thermos[0] = 0;
    if (puzzle_is_generated(id)) gen_lookup_full(id, NULL, reg, cages, marks, thermos, &v, NULL, NULL);
    if (variant) *variant = v;
    if (v == VAR_JIGSAW && buf && sudoku_geom_jigsaw(reg, buf)) return buf;
    if (v == VAR_KILLER && buf && sudoku_geom_killer_text(cages, buf)) return buf;
    if ((v == VAR_EVENODD || v == VAR_CONSEC) && buf && sudoku_geom_marks(v, marks, buf)) return buf;
    if (v == VAR_THERMO && buf && sudoku_geom_thermo_text(thermos, buf)) return buf;
    if (v == VAR_SANDWICH && buf && sudoku_geom_sandwich_text(marks, buf)) return buf;   /* (its sums are in marks) */
    return sudoku_geom(v);                          /* (a broken map or cages: a classic board) */
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

/* the imported collection a puzzle is in (-1: none) and its number there */
static int import_of(uint16_t id, int *number)
{
    int gid = (int)id - PUZZLE_GEN_BASE;
    for (int k = 0; puzzle_is_generated(id) && id < PUZZLE_GEN_RAM_BASE && k < s_nimports; k++) {
        if (gid >= s_imports[k].first_gid && gid < s_imports[k].first_gid + s_imports[k].count) {
            if (number) *number = gid - s_imports[k].first_gid + 1;
            return k;
        }
    }
    return -1;
}

/* the list's collections: the built-in ones, then the imported ones, then the saved ones */
static int coll_total(void) { return PUZZLE_COLLECTION_COUNT + s_nimports + s_nsaved; }
static int coll_saved_index(int c) { return c - PUZZLE_COLLECTION_COUNT - s_nimports; }   /* >= 0: a saved one */
static int coll_tab(int c) { return c < PUZZLE_COLLECTION_COUNT ? 0 : 1; }
static const char *coll_name(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) return puzzle_collections[c].name;
    if (coll_saved_index(c) < 0) return s_imports[c - PUZZLE_COLLECTION_COUNT].name;
    return s_saved[coll_saved_index(c)].name;
}
/* built-in and imported collections are runs of ids */
static uint16_t coll_first(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) return puzzle_collections[c].first;
    if (coll_saved_index(c) < 0) return (uint16_t)(PUZZLE_GEN_BASE + s_imports[c - PUZZLE_COLLECTION_COUNT].first_gid);
    return s_saved[coll_saved_index(c)].count ? s_saved[coll_saved_index(c)].puzzles[0] : 0;
}
static int coll_size(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) return puzzle_collections[c].count;
    if (coll_saved_index(c) < 0) return s_imports[c - PUZZLE_COLLECTION_COUNT].count;
    return s_saved[coll_saved_index(c)].count;
}
/* puzzle i (from 0) of collection c */
static uint16_t coll_puzzle(int c, int i)
{
    int k = c >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(c) : -1;
    return k >= 0 ? s_saved[k].puzzles[i] : (uint16_t)(coll_first(c) + i);
}
/* where puzzle id is in collection c, -1: not there */
static int coll_find(int c, uint16_t id)
{
    if (c < 0 || c >= coll_total()) return -1;
    for (int i = 0; i < coll_size(c); i++) {
        if (coll_puzzle(c, i) == id) return i;
    }
    return -1;
}
/* the collection of a puzzle (built-in or imported); -1 for others */
static int coll_of(uint16_t id)
{
    if (!puzzle_is_generated(id)) return id < PUZZLE_COUNT ? puzzle_collection_of(id) : -1;
    int k = import_of(id, NULL);
    return k < 0 ? -1 : PUZZLE_COLLECTION_COUNT + k;
}

static void imports_reload(void)
{
    int n = stats_db_ready() ? stats_db_list_imports(s_imports) : -1;
    s_nimports = n < 0 ? 0 : n;
    if (!s_saved) s_saved = heap_caps_calloc(STATS_MAX_SAVED, sizeof(stats_saved_t), MALLOC_CAP_SPIRAM);
    int m = s_saved && stats_db_ready() ? stats_db_list_saved(s_saved) : -1;
    s_nsaved = m < 0 ? 0 : m;
}

static void puzzle_name(uint16_t id, char *buf, size_t n)
{
    int num, k = import_of(id, &num);
    if (k >= 0) {                                   /* imported: "Collection #12" */
        snprintf(buf, n, "%s #%d", s_imports[k].name, num);
        return;
    }
    if (puzzle_is_generated(id)) {                  /* (looked up first, so the cache knows its source) */
        gen_lookup(id, NULL, NULL, NULL);
        if (gen_own_name(id, buf, n)) return;       /* typed in: "My puzzles #3" */
    }
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
            char kind[32];
            daily_kind_text(var, lv, kind, sizeof(kind));
            snprintf(buf, n, "Daily %s, %s", kind, when);
            return;
        }
        unsigned num = id >= PUZZLE_GEN_RAM_BASE ? (unsigned)(id - PUZZLE_GEN_RAM_BASE + 1)
                                                 : (unsigned)(id - PUZZLE_GEN_BASE);
        snprintf(buf, n, "%s %s #%u", STATS_KIND_PREFIX[stats_kind_of(var, false)],
                 lv >= 0 && lv < GEN_LEVELS ? GEN_LEVEL_NAMES[lv] : "", num);
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

/* ------------------------------------------------------------- icon buttons, context menus */
/* an icon button of a title row (a toggle, or a menu's): accent with a white icon while on */
static void icon_btn_set(lv_obj_t *b, bool on, lv_color_t off_c)
{
    lv_obj_set_state(b, LV_STATE_CHECKED, on);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), on ? lv_color_white() : off_c, 0);
}

/* A context menu: a card of icon rows under its button (right edges lined up), on the
 * top layer; a tap anywhere else, or on the button again, closes it. An item's cb gets
 * a NULL event. */
typedef struct {
    const char *icon, *text;
    void (*cb)(lv_event_t *e);
    bool disabled;                      /* shown dimmed, not tappable */
    bool danger;                        /* red (Delete) */
} ctx_item_t;
static lv_obj_t *ctx_menu, *ctx_btn;    /* the open menu's backdrop, and its button */

static void ctx_menu_close(void)
{
    if (!ctx_menu) return;
    lv_obj_delete_async(ctx_menu);
    ctx_menu = NULL;
    if (ctx_btn) icon_btn_set(ctx_btn, false, C_TEXT);
    ctx_btn = NULL;
}

static void ctx_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) ctx_menu_close();
}

static void ctx_item_cb(lv_event_t *e)
{
    void (*cb)(lv_event_t *) = (void (*)(lv_event_t *))lv_event_get_user_data(e);
    ctx_menu_close();
    cb(NULL);
}

/* the menu's card, w wide, under btn (any open menu closes first); its content is the caller's */
static lv_obj_t *ctx_card_open(lv_obj_t *btn, int w)
{
    ctx_menu_close();
    ctx_menu = lv_obj_create(lv_layer_top());       /* the backdrop: invisible, catches taps outside */
    lv_obj_remove_style_all(ctx_menu);
    lv_obj_set_size(ctx_menu, SCR_W, SCR_W);
    lv_obj_set_clickable(ctx_menu, true);
    lv_obj_add_event_cb(ctx_menu, ctx_backdrop_cb, LV_EVENT_CLICKED, NULL);
    ctx_btn = btn;
    icon_btn_set(btn, true, C_TEXT);

    lv_area_t a;
    lv_obj_get_coords(btn, &a);
    lv_obj_t *card = lv_obj_create(ctx_menu);
    lv_obj_set_pos(card, a.x2 + 1 - w, a.y2 + 7);
    lv_obj_set_size(card, w, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_CARD2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_offset_y(card, 4, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(card, false);
    return card;
}

/* opens btn's menu (or closes it, when it is open) */
static void ctx_menu_toggle(lv_obj_t *btn, const ctx_item_t *items, int n)
{
    bool was = ctx_menu && ctx_btn == btn;
    ctx_menu_close();
    if (was) return;
    lv_obj_t *card = ctx_card_open(btn, 260);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = make_btn(card, 0, 0, LV_PCT(100), 56, NULL, NULL, items[i].disabled ? NULL : ctx_item_cb,
                               (void *)items[i].cb);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_clickable(b, !items[i].disabled);
        if (items[i].disabled) lv_obj_set_style_opa(b, LV_OPA_40, 0);
        lv_obj_t *ic = make_label(b, &lv_font_montserrat_20, items[i].danger ? C_RED : C_ACCENT);   /* own column */
        lv_label_set_text(ic, items[i].icon);
        lv_obj_set_width(ic, 32);
        lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *l = make_label(b, &lv_font_montserrat_18, items[i].danger ? C_RED : C_TEXT);
        lv_label_set_text(l, items[i].text);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8 + 32 + 12, 0);
    }
}

/* ------------------------------------------------------------- dialog text */
/* Text that wraps to its parent's width, left-aligned (dialogs). Font: NULL
 * = the parent's (inherited, like a label's). */
static lv_obj_t *make_wrap_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
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
    for (int i = 0; i < 81; i++) {                  /* Even-Odd: a digit of the wrong kind */
        if (g_b.val[i] && !(g_geo->allow[i] >> g_b.val[i] & 1)) g_conflict[i] = true;
    }
    for (int i = 0; g_geo->consec_on && i < 81; i++) {   /* Consecutive: a bar and not 1 apart, or 1 apart and no bar */
        for (int k = 0; k < 2; k++) {
            int o = k ? i + 9 : i + 1, bit = k ? CONSEC_DOWN : CONSEC_RIGHT;
            if ((k == 0 && i % 9 == 8) || o > 80 || !g_b.val[i] || !g_b.val[o]) continue;
            bool near = abs(g_b.val[i] - g_b.val[o]) == 1;
            if (near != ((g_geo->consec[i] & bit) != 0)) g_conflict[i] = g_conflict[o] = true;
        }
    }
    for (int lu = 0; g_geo->sandwich && lu < 18; lu++) {   /* Sandwich: between the 1 and the 9, over the sum or short */
        int cells[9], p1 = -1, p9 = -1;
        for (int k = 0; k < 9; k++) {
            cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9);
            if (g_b.val[cells[k]] == 1) p1 = k;
            if (g_b.val[cells[k]] == 9) p9 = k;
        }
        if (p1 < 0 || p9 < 0) continue;
        int a = p1 < p9 ? p1 : p9, b = p1 < p9 ? p9 : p1, sum = 0, empty = 0;
        for (int k = a + 1; k < b; k++) {
            sum += g_b.val[cells[k]];
            empty += !g_b.val[cells[k]];
        }
        if (sum > g_geo->sandwich_sum[lu] || (!empty && sum != g_geo->sandwich_sum[lu])) {
            for (int k = a; k <= b; k++) if (g_b.val[cells[k]]) g_conflict[cells[k]] = true;
        }
    }
    for (int t = 0; t < g_geo->nthermos; t++) {     /* Thermo: two digits along it that don't rise */
        for (int i = 1; i < g_geo->thermo_n[t]; i++) {
            int a = g_geo->thermo_cell[t][i - 1], b = g_geo->thermo_cell[t][i];
            if (g_b.val[a] && g_b.val[b] && g_b.val[b] <= g_b.val[a]) g_conflict[a] = g_conflict[b] = true;
        }
    }
    for (int k = 0; k < g_geo->ncages; k++) {       /* Killer: a cage over its sum, or full and short of it */
        int sum = 0, filled = 0, n = g_geo->cage_n[k];
        for (int i = 0; i < n; i++) {
            int c = g_geo->cage_cell[k][i];
            sum += g_b.val[c];
            filled += g_b.val[c] != 0;
        }
        if (sum > g_geo->cage_sum[k] || (filled == n && sum != g_geo->cage_sum[k])) {
            for (int i = 0; i < n; i++) {
                int c = g_geo->cage_cell[k][i];
                if (g_b.val[c]) g_conflict[c] = true;
            }
        }
    }
    for (int i = 0; i < 81; i++) n += g_conflict[i];
    return n;
}

/* Is cell i's digit wrong by the Mistake check setting (Settings)? Off:
 * nothing; Rules: it breaks a rule (g_conflict); Solution: also a digit
 * that isn't the solution's. Shown red; placing one buzzes. */
static bool digit_wrong(int i)
{
    if (!g_b.val[i]) return false;
    switch (store_assist()) {
    case ASSIST_OFF:
        return false;
    case ASSIST_SOLUTION:
        return g_conflict[i] || (!g_given[i] && g_sol_ok && g_b.val[i] != g_sol[i]);
    default:
        return g_conflict[i];
    }
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

/* the colours a cell can be painted (1..CELL_COLORS) */
static lv_color_t cell_color(int k)
{
    static const uint32_t C[CELL_COLORS] = {0xE5484D, 0xF5A524, 0xE2D33C, 0x30A46C, 0x3E7BFA, 0x9E6CF0};
    return lv_color_hex(C[(k - 1) % CELL_COLORS]);
}

/* the cell's colour with nothing selected: X-Sudoku tints its two diagonals */
static lv_color_t cell_base_bg(const sudoku_geom_t *geo, int i, bool given)
{
    lv_color_t bg = given ? C_CELL_GIVEN : C_CELL;
    if (geo->parity[i] == PARITY_EVEN) bg = lv_color_mix(C_GRID_THIN, bg, 150);   /* Even-Odd: even cells shaded */
    return sudoku_geom_tinted(geo, i) ? lv_color_mix(C_ORANGE, bg, 40) : bg;
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
        if ((g_b.val[i] && !g_given[i]) || g_b.notes[i] || g_b.colors[i]) return true;
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
    if (g_id == STORE_NO_GAME || g_solved || g_practice >= 0 || g_attack) return;   /* (practice, time attack: not kept) */
    const saved_game_t *s = store_saved_game();
    if (!has_progress() && s->id != g_id) return;   /* only looked at it: keep the slot as is */
    saved_game_t sg;
    sg.id = g_id;
    memcpy(sg.val, g_b.val, sizeof(sg.val));
    memcpy(sg.notes, g_b.notes, sizeof(sg.notes));
    memcpy(sg.colors, g_b.colors, sizeof(sg.colors));
    sg.elapsed_ms = g_elapsed_ms;
    sg.mistakes = g_mistakes;
    sg.hints = g_hints;
    sg.flags = g_flags;
    sg.started_at = game_started_at();
    store_set_game(&sg);
    store_set_game_moves(g_moves, g_nmoves);
    store_commit_game();
}

/* ============================================================= game view */

static void update_timer_label(void)
{
    char buf[16];
    if (g_attack) {                                 /* the run's time left (always shown) */
        uint32_t used = atk_used_ms + g_elapsed_ms;
        uint32_t left = used >= ATK_MS ? 0 : ATK_MS - used;
        fmt_time(buf, sizeof(buf), (left + 999) / 1000);
        if (strcmp(lv_label_get_text(g_timer), buf) != 0) lv_label_set_text(g_timer, buf);
        lv_obj_set_style_text_color(g_timer, g_paused ? C_MUTED : (left < 60000 ? C_ORANGE : C_TEXT), 0);
        return;
    }
    fmt_time(buf, sizeof(buf), g_elapsed_ms / 1000);
    if (!store_show_timer() && !g_solved) snprintf(buf, sizeof(buf), "--:--");   /* Settings > Show timer off */
    if (strcmp(lv_label_get_text(g_timer), buf) != 0) lv_label_set_text(g_timer, buf);
    lv_obj_set_style_text_color(g_timer, g_solved ? C_GREEN : (g_paused ? C_MUTED : C_TEXT), 0);
}

static void update_header(void)
{
    char name[40], best[24], buf[48];
    puzzle_name(g_id, name, sizeof(name));
    if (g_practice >= 0) snprintf(name, sizeof(name), "Practice: %s", LEARN_NAMES[g_practice]);
    if (g_attack) {
        char n2[40];
        snprintf(n2, sizeof(n2), "Time attack: %s", name);
        snprintf(name, sizeof(name), "%s", n2);
    }
    lv_label_set_text(g_title, name);
    /* the star: puzzles that stay (not Learn's, nor ones kept only in RAM) */
    bool can_star = g_practice < 0 && stats_db_ready() && g_id < PUZZLE_GEN_RAM_BASE;
    lv_obj_set_hidden(g_star_btn, !can_star);
    lv_obj_set_style_text_color(lv_obj_get_child(g_star_btn, 0), g_starred ? C_ORANGE : C_MUTED, 0);
    /* Save: a puzzle made on the board (not an imported one: it has its collection) */
    bool can_save = can_star && !g_attack && puzzle_is_generated(g_id) && import_of(g_id, NULL) < 0;
    bool saved = false;
    for (int k = 0; can_save && k < s_nsaved; k++) saved |= coll_find(PUZZLE_COLLECTION_COUNT + s_nimports + k, g_id) >= 0;
    lv_obj_set_hidden(g_save_btn, !can_save);
    lv_obj_set_style_text_color(lv_obj_get_child(g_save_btn, 0), saved ? C_ACCENT : C_MUTED, 0);
    uint32_t b = store_best_time(g_id);
    if (g_attack) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE "  %d solved in this run", atk_solved);
        lv_obj_set_style_text_color(g_best, C_ORANGE, 0);
    } else if (b) {
        fmt_time(best, sizeof(best), b);
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  Best %s", best);
        lv_obj_set_style_text_color(g_best, C_GREEN, 0);
    } else {
        snprintf(buf, sizeof(buf), "Not solved yet");
        lv_obj_set_style_text_color(g_best, C_MUTED, 0);
    }
    lv_label_set_text(g_best, buf);
    bool done = g_solved && !g_attack;              /* (a run's end isn't a solve: the bolt) */
    lv_label_set_text(g_timer_icon, g_attack && g_solved ? LV_SYMBOL_CHARGE
                                    : done ? LV_SYMBOL_OK : (g_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE));
    lv_obj_set_style_text_color(g_timer_icon, done ? C_GREEN : (g_attack && g_solved ? C_ORANGE : C_MUTED), 0);
    update_timer_label();
}

static void refresh_board(void)
{
    int sv = g_lock ? g_lock : (g_sel >= 0 ? g_b.val[g_sel] : 0);   /* same-digit highlight */
    for (int i = 0; i < 81; i++) {
        if (!g_geo->used[i]) continue;
        lv_color_t bg = cell_base_bg(g_geo, i, g_given[i]);
        if (g_b.colors[i]) bg = lv_color_mix(cell_color(g_b.colors[i]), bg, 110);   /* painted by the player */
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
        bool wrong = !g_solved && digit_wrong(i);
        if (wrong && !g_given[i] && i != g_sel) bg = C_CELL_BAD;
        lv_obj_set_style_bg_color(g_cells[i], bg, 0);

        lv_color_t fg = g_given[i] ? C_TEXT : C_DIGIT_USER;
        if (g_solved) fg = g_given[i] ? C_TEXT : C_GREEN;
        else if (wrong) fg = C_RED;
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
        lv_obj_set_style_opa(g_pad[d - 1], left <= 0 && d != g_lock ? LV_OPA_40 : LV_OPA_COVER, 0);
        bool locked = d == g_lock;                  /* digit-first: the locked digit is accent blue */
        lv_obj_set_state(g_pad[d - 1], LV_STATE_CHECKED, locked);
        lv_obj_set_style_text_color(lv_obj_get_child(g_pad[d - 1], 0), locked ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(g_pad_cnt[d - 1], locked ? lv_color_white() : C_MUTED, 0);
    }
    lv_obj_set_style_opa(g_undo_btn, g_undo_len ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(g_redo_btn, g_redo_len ? LV_OPA_COVER : LV_OPA_40, 0);

    if (g_hints) lv_label_set_text_fmt(g_hint_cnt, "%u", (unsigned)g_hints);
    lv_obj_set_hidden(g_hint_cnt, g_hints == 0);

    /* status line */
    int bad = 0;
    for (int i = 0; i < 81; i++) bad += digit_wrong(i);
    bool full = filled_count() == g_geo->ncells;
    if (g_solved) {
        char t[16];
        fmt_time(t, sizeof(t), g_elapsed_ms / 1000);
        lv_label_set_text_fmt(g_status, LV_SYMBOL_OK "  Solved in %s", t);
        lv_obj_set_style_text_color(g_status, C_GREEN, 0);
    } else if (bad) {
        lv_label_set_text_fmt(g_status, LV_SYMBOL_WARNING "  %d %s", bad,
                              store_assist() == ASSIST_SOLUTION ? "wrong" : "conflicting");
        lv_obj_set_style_text_color(g_status, C_RED, 0);
    } else if (full) {                              /* Mistake check off: something is wrong somewhere */
        lv_label_set_text(g_status, "All filled, not solved yet");
        lv_obj_set_style_text_color(g_status, C_ORANGE, 0);
    } else if (g_lock) {
        lv_label_set_text_fmt(g_status, "Placing %d: tap cells", g_lock);
        lv_obj_set_style_text_color(g_status, C_ACCENT, 0);
    } else {
        lv_label_set_text_fmt(g_status, "Filled %d / %d", filled_count(), g_geo->ncells);
        lv_obj_set_style_text_color(g_status, C_MUTED, 0);
    }
}

static uint16_t s_solve_id;
static uint32_t s_solve_secs, s_solve_mistakes, s_solve_hints, s_solve_flags;
static int64_t  s_solve_started;

static void solve_commit_cb(lv_timer_t *t)
{
    store_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes);
    stats_db_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes, s_solve_hints,
                          s_solve_started, g_moves, g_nmoves, s_solve_flags);   /* finished: now */
    /* head-to-head: where this leaves the player among everyone who solved it (after the solve above) */
    stats_h2h_t h;
    char line[96] = "";
    if (stats_db_ready() && stats_db_puzzle_rank(s_solve_id, &h) && h.rank) {
        char t[16];
        fmt_time(t, sizeof(t), h.leader_seconds);
        if (!h.leader[0]) snprintf(line, sizeof(line), "Nobody else has solved this one yet");
        else if (h.second) snprintf(line, sizeof(line), SYMBOL_STAR "  1st of %d: ahead of %s (%s)", h.of, h.leader, t);
        else snprintf(line, sizeof(line), "%d%s of %d   -   %s leads with %s", h.rank, ordinal(h.rank), h.of, h.leader, t);
    }
    lv_label_set_text(g_win_h2h, line);
    while (lv_obj_get_child_count(g_win_h2h_row) > 1) lv_obj_delete(lv_obj_get_child(g_win_h2h_row, 0));
    const stats_player_t *lead = line[0] && h.leader[0] ? player_find(0, h.leader) : NULL;
    if (lead && lead->avatar && lead->avatar <= AVATAR_COUNT) {   /* the one ahead (or behind): their avatar */
        lv_obj_t *b = player_badge(g_win_h2h_row, lead, -1, 26, 8);
        lv_obj_move_to_index(b, 0);
    }
    char day[12] = "";
    if (puzzle_is_generated(s_solve_id) && gen_lookup_day(s_solve_id, NULL, NULL, NULL, day) && day[0]) {
        int id, streak;
        bool solved;
        if (stats_db_daily_status(day, &id, &solved, &streak) && solved) {   /* after the solve above (queued first) */
            char msg[128];
            int len = snprintf(msg, sizeof(msg), "Daily puzzle solved! Streak: %d day%s.", streak,
                               streak == 1 ? "" : "s");
            int rank = 0, of = 0;
            if (stats_db_daily_rank(day, &rank, &of) && rank && of > 1) {
                snprintf(msg + len, sizeof(msg) - len, " You're %d%s of %d today (Trophies > Daily).", rank,
                         ordinal(rank), of);
            }
            sudoku_ui_notice(msg, 5000);
        }
    }
}

/* ------------------------------------------------------------- confetti */
/*
 * A burst of paper confetti over the solved board before the Solved! dialog: CF_N
 * pieces explode out of the middle of the screen (mostly upwards), air drag slows
 * them, then gravity has them flutter down and away, spinning (drawn as a width
 * that pulses) and fading out at the end. One overlay on the top
 * layer draws them all (its DRAW_MAIN event), redrawn by a timer; it also holds
 * touches back until the dialog is up.
 */
#define CF_N        90
#define CF_MS       2200                /* then the dialog */
#define CF_FADE_MS  400

typedef struct {
    float x, y, vx, vy, spin, spin_v, sway, sway_v;
    uint8_t w, h, color;
} cf_piece_t;

static const uint32_t CF_COLORS[] = {0xE53935, 0xFB8C00, 0xFDD835, 0x43A047, 0x1E88E5, 0x8E24AA, 0xEC407A, 0x00ACC1};
static cf_piece_t *cf_p;
static lv_obj_t *cf_ovl;
static lv_timer_t *cf_timer;
static uint32_t cf_t0, cf_last;
static void (*cf_done)(void);

static float cf_rand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}

static void cf_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    uint32_t age = lv_tick_elaps(cf_t0);
    lv_opa_t opa = age + CF_FADE_MS < CF_MS ? LV_OPA_COVER
                                            : (lv_opa_t)(LV_OPA_COVER * (CF_MS - LV_MIN(age, CF_MS)) / CF_FADE_MS);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 2;
    d.bg_opa = opa;
    for (int i = 0; i < CF_N; i++) {
        const cf_piece_t *p = &cf_p[i];
        if (p->y > SCR_W + 30 || p->x < -30 || p->x > SCR_W + 30) continue;
        int32_t w = (int32_t)(p->w * fabsf(cosf(p->spin))) + 2;   /* turning over */
        int32_t x = (int32_t)(p->x + 10.0f * sinf(p->sway)), y = (int32_t)p->y;
        lv_area_t a = {x - w / 2, y - p->h / 2, x + (w - 1) / 2, y + (p->h - 1) / 2};
        d.bg_color = lv_color_hex(CF_COLORS[p->color]);
        lv_draw_rect(layer, &d, &a);
    }
}

static void cf_stop(void)
{
    if (cf_timer) lv_timer_delete(cf_timer);
    cf_timer = NULL;
    if (cf_ovl) lv_obj_delete(cf_ovl);
    cf_ovl = NULL;
    free(cf_p);
    cf_p = NULL;
}

static void cf_tick(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    float dt = (float)lv_tick_diff(now, cf_last) / 1000.0f;
    cf_last = now;
    if (dt > 0.1f) dt = 0.1f;                       /* (a slow frame doesn't throw them) */
    for (int i = 0; i < CF_N; i++) {
        cf_piece_t *p = &cf_p[i];
        p->vy += 1300.0f * dt;                      /* gravity, px/s^2 */
        float drag = 1.0f - 2.5f * dt;              /* air: the burst slows fast, then it's a fall */
        p->vx *= drag;
        p->vy *= drag;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->spin += p->spin_v * dt;
        p->sway += p->sway_v * dt;
    }
    lv_obj_invalidate(cf_ovl);
    if (lv_tick_elaps(cf_t0) >= CF_MS) {
        void (*done)(void) = cf_done;
        cf_stop();
        if (done) done();
    }
}

/* starts the confetti; done runs when it's over (at once if it can't start) */
static void confetti(void (*done)(void))
{
    cf_stop();
    cf_p = malloc(sizeof(cf_piece_t) * CF_N);
    if (!cf_p) {
        if (done) done();
        return;
    }
    for (int i = 0; i < CF_N; i++) {
        cf_piece_t *p = &cf_p[i];
        /* the burst, from the middle (tuned so the highest just reach the top edge and
         * nearly all have fallen off the bottom by the end) */
        float ang = cf_rand(0, 6.2832f), speed = cf_rand(400, 1300);
        p->x = SCR_W / 2 + cf_rand(-12, 12);
        p->y = SCR_W / 2 + cf_rand(-12, 12);
        p->vx = speed * cosf(ang);
        p->vy = speed * sinf(ang) - cf_rand(250, 600);   /* mostly upwards */
        p->spin = cf_rand(0, 6.28f);
        p->spin_v = cf_rand(6, 14);
        p->sway = cf_rand(0, 6.28f);
        p->sway_v = cf_rand(3, 6);
        p->w = (uint8_t)cf_rand(9, 15);
        p->h = (uint8_t)cf_rand(12, 20);
        p->color = (uint8_t)(esp_random() % (sizeof(CF_COLORS) / sizeof(CF_COLORS[0])));
    }
    cf_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(cf_ovl);
    lv_obj_set_size(cf_ovl, SCR_W, SCR_W);
    lv_obj_set_clickable(cf_ovl, true);             /* no taps until the dialog is up */
    lv_obj_add_event_cb(cf_ovl, cf_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    cf_done = done;
    cf_t0 = cf_last = lv_tick_get();
    cf_timer = lv_timer_create(cf_tick, 16, NULL);
}

/* the Solved! dialog, after the confetti; the solve is saved once it's up */
static void win_show(void)
{
    if (lv_screen_active() == scr_game && g_solved) {   /* (the dialog only where it belongs) */
        lv_obj_set_hidden(g_pause_ovl, true);
        lv_obj_set_hidden(g_win_ovl, false);
    }
    lv_timer_t *ct = lv_timer_create(solve_commit_cb, SAVE_DELAY_MS, NULL);
    lv_timer_set_repeat_count(ct, 1);
}

static void on_solved(void)
{
    g_solved = true;
    g_lock = 0;
    g_paused = false;
    sfx_play(SFX_SOLVED);
    g_sel = -1;
    if (g_practice >= 0) {                          /* Learn: not recorded */
        sudoku_ui_notice("Solved! Practice games aren't saved or counted: Learn has more techniques.", 4000);
        update_header();
        return;
    }
    if (g_attack) {                                 /* Time attack: counted, and straight on to the next */
        uint32_t secs = (g_elapsed_ms + 500) / 1000;
        store_record_solve(g_id, secs, g_mistakes);
        stats_db_record_solve(g_id, secs, g_mistakes, g_hints, game_started_at(), g_moves, g_nmoves, g_flags);
        atk_used_ms += g_elapsed_ms;
        g_elapsed_ms = 0;
        atk_solved++;
        atk_mistakes += g_mistakes;
        char msg[64];
        snprintf(msg, sizeof(msg), "Solved! %d in this run. Next one...", atk_solved);
        sudoku_ui_notice(msg, 1500);
        update_header();
        lv_timer_t *nt = lv_timer_create((lv_timer_cb_t)atk_open_next, 900, NULL);
        lv_timer_set_repeat_count(nt, 1);
        return;
    }
    uint32_t secs = (g_elapsed_ms + 500) / 1000;
    uint32_t prev = store_best_time(g_id);
    uint32_t rec = secs ? (secs > 0xFFFF ? 0xFFFF : secs) : 1;   /* as store_record_solve() stores it */
    bool best = !prev || rec < prev;
    s_solve_id = g_id;
    s_solve_secs = secs;
    s_solve_mistakes = g_mistakes;
    s_solve_hints = g_hints;
    s_solve_flags = g_flags;
    s_solve_started = game_started_at();

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
    lv_label_set_text(g_win_h2h, "");                 /* (solve_commit_cb fills it in) */
    while (lv_obj_get_child_count(g_win_h2h_row) > 1) lv_obj_delete(lv_obj_get_child(g_win_h2h_row, 0));
    if (g_hints == 0) {
        lv_label_set_text(g_win_hint, "No hints");
        lv_obj_set_style_text_color(g_win_hint, C_GREEN, 0);
    } else {
        lv_label_set_text_fmt(g_win_hint, "%u hint%s used", (unsigned)g_hints, g_hints == 1 ? "" : "s");
        lv_obj_set_style_text_color(g_win_hint, C_ORANGE, 0);
    }
    lv_obj_set_hidden(g_win_badge, !(g_flags & GAME_FLAG_NO_NOTES));
    char day[12] = "";                              /* a daily puzzle: its result can be shared */
    lv_obj_set_hidden(g_win_share, !(puzzle_is_generated(g_id) && gen_lookup_day(g_id, NULL, NULL, NULL, day) &&
                                     day[0]));
    if (!stats_db_ready()) {
        sudoku_ui_notice("No sudoku.db on a microSD card: this result is kept only until the board is "
                         "switched off.", 6000);
    }
    lv_obj_set_hidden(g_pause_ovl, true);
    update_header();
    confetti(win_show);                             /* then the dialog (win_show) */
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

static void push_undo_keep_redo(void)
{
    g_undo[g_undo_head] = g_b;
    g_undo_head = (g_undo_head + 1) % UNDO_MAX;
    if (g_undo_len < UNDO_MAX) g_undo_len++;
}

/* before every edit: the board goes on the undo ring, and redo is gone */
static void push_undo(void)
{
    push_undo_keep_redo();
    g_redo_len = 0;
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

    /* a mistake: breaks a rule (with Mistake check on Solution: or isn't the
     * solution's digit). Counted even with the check off, just not shown. */
    bool wrong = g_conflict[i] || (store_assist() == ASSIST_SOLUTION && g_sol_ok && d != g_sol[i]);
    if (wrong) {
        if (g_mistakes < 0xFFFF) g_mistakes++;
        if (store_assist() != ASSIST_OFF) sfx_play(SFX_ERROR);
        return;
    }
    placed_sounds(i, d);
}

/* Fill: the selected cell gets its digit from the solution; counts as a hint */
static void fill_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    if (g_attack) {
        sudoku_ui_notice("Fill and Hint are off in a time attack.", 2500);
        return;
    }
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

static void cell_long_cb(lv_event_t *e);
static void rules_first_time(void);

static void cell_click_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    g_sel = i;
    if (g_lock && !g_given[i]) {                    /* digit-first: the locked digit goes in (or its note) */
        enter_digit(g_lock);
        return;
    }
    refresh_board();
}

/* ---- the cell menu (long press a cell): paint it, and on Killer the cage's digit sets */
static lv_obj_t *cm_ovl;
static int cm_cell;

static void cm_close(void)
{
    if (cm_ovl) lv_obj_delete_async(cm_ovl);
    cm_ovl = NULL;
}

static void cm_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) cm_close();
}

static void cm_color_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    int i = cm_cell;
    cm_close();
    if (g_solved || g_paused || i < 0 || g_b.colors[i] == k) return;
    push_undo();
    g_b.colors[i] = (uint8_t)k;
    after_move();                                   /* redraws and saves (colours aren't in the move log) */
}

/* the sets of different digits that make up a cage's sum and hold the digits
 * already in it, as text ("1+5+9   2+4+9 ..."); how many sets */
static int cage_sets_text(int k, char *buf, size_t n)
{
    int size = g_geo->cage_n[k], sum = g_geo->cage_sum[k], count = 0;
    uint16_t have = 0;
    for (int j = 0; j < size; j++) {
        int v = g_b.val[g_geo->cage_cell[k][j]];
        if (v) have |= (uint16_t)(1u << v);
    }
    size_t len = 0;
    buf[0] = 0;
    for (uint16_t m = 2; m < 0x400; m += 2) {       /* digit sets as bits 1..9 */
        if (__builtin_popcount(m) != size || (m & have) != have) continue;
        int s = 0;
        for (int d = 1; d <= 9; d++) s += (m >> d & 1) * d;
        if (s != sum) continue;
        count++;
        if (len + 24 >= n) continue;                /* (the count still goes on) */
        len += snprintf(buf + len, n - len, "%s", count > 1 ? "     " : "");
        bool first = true;
        for (int d = 1; d <= 9; d++) {
            if (!(m >> d & 1)) continue;
            len += snprintf(buf + len, n - len, first ? "%d" : "+%d", d);
            first = false;
        }
    }
    return count;
}

static void cell_long_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    cm_close();
    cm_cell = i;
    cm_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(cm_ovl);
    lv_obj_set_size(cm_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cm_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cm_ovl, LV_OPA_30, 0);
    lv_obj_set_clickable(cm_ovl, true);
    lv_obj_add_event_cb(cm_ovl, cm_backdrop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *card = lv_obj_create(cm_ovl);
    lv_obj_set_size(card, 520, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_scrollable(card, false);
    /* above the cell's row when it's low on the board, else below it */
    lv_area_t co;
    lv_obj_get_coords(g_cells[i], &co);
    lv_obj_align(card, co.y1 > SCR_W / 2 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, co.y1 > SCR_W / 2 ? 70 : -20);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text_fmt(t, "Row %d, column %d: colour", i / 9 + 1, i % 9 + 1);
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    for (int k = 0; k <= CELL_COLORS; k++) {        /* none, then the colours */
        lv_obj_t *b = make_btn(row, 0, 0, 58, 56, k ? NULL : LV_SYMBOL_CLOSE, &lv_font_montserrat_20, cm_color_cb,
                               (void *)(intptr_t)k);
        if (k) lv_obj_set_style_bg_color(b, cell_color(k), 0);
        else lv_obj_set_style_bg_color(b, C_CARD2, 0);
        if (g_b.colors[i] == k) {
            lv_obj_set_style_border_color(b, C_TEXT, 0);
            lv_obj_set_style_border_width(b, 3, 0);
        }
    }
    int k = g_geo->cage_of[i];
    if (k != CAGE_NONE) {                           /* Killer helper */
        char sets[240];
        int n = cage_sets_text(k, sets, sizeof(sets));
        lv_obj_t *h = make_label(card, &lv_font_montserrat_16, C_MUTED);
        lv_label_set_text_fmt(h, "Cage: %d in %d cells, %d way%s%s", g_geo->cage_sum[k], g_geo->cage_n[k], n,
                              n == 1 ? "" : "s", n ? ":" : " (check the digits in it)");
        if (n) {
            lv_obj_t *l = make_label(card, &lv_font_montserrat_20, C_TEXT);
            lv_obj_set_width(l, LV_PCT(100));
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
            lv_label_set_text(l, sets);
        }
    }
}

static void cell_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_b.val[i] || !g_b.notes[i]) return;
    lv_obj_t *cell = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(cell, &co);
    int sv = g_lock ? g_lock : (g_sel >= 0 ? g_b.val[g_sel] : 0);

    /* three marks per row: 3 rows on a 9x9 board, 2 (centred) on a 6x6 one; 4x4: two by two */
    const int cols = g_geo->n == 4 ? 2 : 3;
    int size = lv_area_get_width(&co), pitch = size / cols, rows = (g_geo->n + cols - 1) / cols;
    int x0 = 1, y0 = rows == 3 ? 2 : (size - rows * pitch) / 2;
    int vpitch = pitch;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    const bool big = store_big_digits();            /* (Digit size: Large) */
    d.font = size >= 80 ? (big ? &lv_font_montserrat_24 : &lv_font_montserrat_22)
                        : (big ? &lv_font_montserrat_18 : &lv_font_montserrat_16);
    if (g_geo->ncages) {                            /* Killer: smaller, below the cage sum in the corner */
        d.font = &lv_font_montserrat_12;
        y0 = 13;
        vpitch = (size - 15) / 3;
    }
    d.align = LV_TEXT_ALIGN_CENTER;
    for (int n = 1; n <= g_geo->n; n++) {
        if (!(g_b.notes[i] & (1u << n))) continue;
        int r = (n - 1) / cols, c = (n - 1) % cols;
        lv_area_t a = {
            .x1 = co.x1 + x0 + c * pitch, .y1 = co.y1 + y0 + r * vpitch,
            .x2 = co.x1 + x0 + c * pitch + pitch - 1, .y2 = co.y1 + y0 + r * vpitch + vpitch - 1,
        };
        d.color = n == sv ? C_ACCENT : C_MUTED;
        d.text = DIGITS[n];
        lv_draw_label(layer, &d, &a);
    }
}

/* A short tap enters the digit in the selected cell; while a digit is
 * locked (digit-first) it moves the lock to that digit, or unlocks it. */
static void pad_click_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_lock) {
        g_lock = g_lock == d ? 0 : d;
        refresh_board();
        return;
    }
    enter_digit(d);
}

/* A long press locks the digit: then each tap on a cell places it there. */
static void pad_long_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    g_lock = g_lock == d ? 0 : d;
    refresh_board();
}

/* Auto notes: every empty cell gets all the digits that still fit there as
 * pencil marks (replacing its notes). Undo takes it back. */
static void auto_notes_cb(lv_event_t *e)
{
    if (g_solved || g_paused || (g_flags & GAME_FLAG_NO_NOTES)) return;
    uint16_t notes[81];
    int changed = 0;
    for (int k = 0; k < g_geo->ncells; k++) {
        int c = g_geo->cells[k];
        notes[c] = g_b.notes[c];
        if (g_b.val[c]) continue;
        uint16_t m = g_geo->all;
        for (int p = 0; p < g_geo->npeers[c]; p++) m &= (uint16_t)~(1u << g_b.val[g_geo->peers[c][p]]);
        notes[c] = m;
        changed += m != g_b.notes[c];
    }
    if (!changed) {
        sudoku_ui_notice("The notes already show every digit that fits.", 2500);
        return;
    }
    push_undo();
    for (int k = 0; k < g_geo->ncells; k++) g_b.notes[g_geo->cells[k]] = notes[g_geo->cells[k]];
    after_move();
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
    if (g_redo_len == UNDO_MAX) {                   /* full: the oldest undone board goes */
        memmove(g_redo, g_redo + 1, sizeof(board_t) * (UNDO_MAX - 1));
        g_redo_len--;
    }
    g_redo[g_redo_len++] = g_b;                     /* for Redo */
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

/* puts back what the last Undo took away (until the next edit) */
static void redo_cb(lv_event_t *e)
{
    if (g_solved || g_paused || g_redo_len == 0) return;
    push_undo_keep_redo();
    uint8_t before[81];
    memcpy(before, g_b.val, sizeof(before));
    g_b = g_redo[--g_redo_len];
    for (int i = 0; i < 81; i++) {
        if (g_b.val[i] != before[i]) record_move(i, g_b.val[i], MOVE_REDO);
    }
    after_move();
}

static void notes_cb(lv_event_t *e)
{
    if (g_flags & GAME_FLAG_NO_NOTES) {             /* (the button is off in the challenge anyway) */
        lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
        return;
    }
    g_notes_mode = lv_obj_has_state(g_notes_btn, LV_STATE_CHECKED);
}

/* the no-notes challenge: Notes and Auto off (dimmed) for this game */
static void challenge_ui(void)
{
    bool on = (g_flags & GAME_FLAG_NO_NOTES) != 0;
    if (on) {
        g_notes_mode = false;
        lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
    }
    lv_obj_set_state(g_notes_btn, LV_STATE_DISABLED, on);
    if (g_auto_btn) lv_obj_set_state(g_auto_btn, LV_STATE_DISABLED, on);
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
    else if (u < 27) snprintf(buf, n, "%s %d", g_geo->variant == VAR_JIGSAW ? "region" : "box", u - 18 + 1);
    else if (u < UNIT_WINDOW) snprintf(buf, n, "the %s diagonal", u == UNIT_DIAG_MAIN ? "top-left" : "top-right");
    else {                                          /* Hyper windows */
        static const char *const W[4] = {"top-left", "top-right", "bottom-left", "bottom-right"};
        snprintf(buf, n, "the %s window", W[(u - UNIT_WINDOW) & 3]);
    }
}

/* "row, column or box" (and diagonal, window, or region for a Jigsaw) */
static const char *units_word(void)
{
    if (g_geo->diag) return "row, column, box or diagonal";
    if (g_geo->windows) return "row, column, box or window";
    if (g_geo->variant == VAR_JIGSAW) return "row, column or region";
    if (g_geo->ncages) return "row, column, box or cage";
    if (g_geo->variant == VAR_KNIGHT) return "row, column, box or a knight's move away";
    if (g_geo->nthermos) return "row, column or box (or the thermometer)";
    return "row, column or box";
}

/* "1, 4 or 7" from a candidate bit mask */
static void digit_list(int mask, const char *last_word, char *buf, size_t n)
{
    int ds[9], k = 0;
    for (int d = 1; d <= 9; d++) if (mask >> d & 1) ds[k++] = d;
    size_t len = 0;
    buf[0] = 0;
    for (int i = 0; i < k; i++) {
        len += snprintf(buf + len, n - len, "%s%d", i == 0 ? "" : (i == k - 1 ? " " : ", "), ds[i]);
        if (i == k - 2 && len < n) len += snprintf(buf + len, n - len, " %s", last_word);
    }
}

static void mark_unit(int u, uint8_t v)
{
    for (int k = 0; k < g_geo->n; k++) {
        int c = sudoku_unit_cell(g_geo, u, k);
        if (c >= 0 && h_mark[c] < v) h_mark[c] = v;
    }
}

/* closes the hint without redrawing (the caller does) */
/* Settings > Game > Hints: Nudge only: where to look, nothing more (Learn keeps the full hint) */
static bool hint_nudge(void)
{
    return store_hint_nudge() && g_practice < 0;
}

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
    char text[360], u1[48], u2[24], tech[260] = "";
    int r = h->cell / 9 + 1, c = h->cell % 9 + 1;
    memset(h_mark, 0, sizeof(h_mark));

    /* where to look: the technique's area if one is needed, else the single's */
    if (h->kind == STEP_WRONG || h->kind == STEP_NAKED_SINGLE || h->kind == STEP_NONE) h_mark[h->cell] = 1;
    if (h->kind == STEP_HIDDEN_SINGLE && !t->kind) mark_unit(h->unit, 1);
    if (t->kind == TECH_SANDWICH) {                 /* Sandwich: the row or column */
        mark_unit(t->unit, 1);
        unit_name(t->unit, u1, sizeof(u1));
        snprintf(tech, sizeof(tech), "In %s the digits between the 1 and the 9 add up to %d: that leaves the 1 and "
                 "the 9 fewer places there. ", u1, t->unit >= 0 && t->unit < 18 ? g_geo->sandwich_sum[t->unit] : 0);
    } else if (t->kind == TECH_THERMO) {            /* Thermo: the thermometer */
        int k = t->unit;
        for (int i = 0; k >= 0 && k < g_geo->nthermos && i < g_geo->thermo_n[k]; i++) h_mark[g_geo->thermo_cell[k][i]] = 1;
        int c = t->cell1 >= 0 ? t->cell1 : 0;
        snprintf(u1, sizeof(u1), "this thermometer");
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        snprintf(tech, sizeof(tech), "Along the thermometer the digits rise from the bulb, so row %d, column %d can "
                 "only be %s. ", c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_CONSEC) {            /* Consecutive: the cell and its neighbours */
        int c = t->unit;
        if (c >= 0 && c < 81) {
            h_mark[c] = 1;
            if (c % 9 > 0) h_mark[c - 1] = 1;
            if (c % 9 < 8) h_mark[c + 1] = 1;
            if (c > 8) h_mark[c - 9] = 1;
            if (c < 72) h_mark[c + 9] = 1;
        }
        snprintf(u1, sizeof(u1), "row %d, column %d", c / 9 + 1, c % 9 + 1);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        snprintf(tech, sizeof(tech), "The bars around row %d, column %d (and where there are none) leave it only %s. ",
                 c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_CAGE) {              /* Killer: the cage's cells */
        int k = t->unit;
        for (int i = 0; k >= 0 && k < g_geo->ncages && i < g_geo->cage_n[k]; i++) h_mark[g_geo->cage_cell[k][i]] = 1;
        int c0 = k >= 0 && k < g_geo->ncages ? g_geo->cage_cell[k][0] : 0;
        snprintf(u1, sizeof(u1), "the %d cage", k >= 0 && k < g_geo->ncages ? g_geo->cage_sum[k] : 0);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        snprintf(tech, sizeof(tech), "The cage at row %d, column %d adds up to %d: with what's in it and around "
                 "it, its empty cells can only be %s. ", c0 / 9 + 1, c0 % 9 + 1,
                 k >= 0 && k < g_geo->ncages ? g_geo->cage_sum[k] : 0, digs);
    } else if (t->kind == TECH_NAKED_TRIPLE || t->kind == TECH_X_WING || t->kind == TECH_SWORDFISH ||
               t->kind == TECH_XY_WING) {
        for (int i = 0; i < t->ncells; i++) h_mark[t->cells[i]] = 1;   /* the cells that make the pattern */
        char digs[24];
        if (t->kind == TECH_NAKED_TRIPLE) {
            unit_name(t->unit, u1, sizeof(u1));
            digit_list(t->digit, "or", digs, sizeof(digs));
            snprintf(tech, sizeof(tech), "In %s, three cells can only be %s between them, so no other cell "
                     "there can be any of those. ", u1, digs);
        } else if (t->kind == TECH_XY_WING) {
            int p = t->cells[0], a = t->cells[1], b = t->cells[2], z = t->digit;
            int x = __builtin_ctz((unsigned)(t->digit2 & t->cell1)), y = __builtin_ctz((unsigned)(t->digit2 & t->cell2));
            snprintf(u1, sizeof(u1), "the three highlighted cells");
            snprintf(tech, sizeof(tech), "Row %d, column %d can only be %d or %d. If it's %d, row %d, column %d is %d; "
                     "if it's %d, row %d, column %d is %d. Either way one of them is %d, so no cell that sees both "
                     "can be %d. ", p / 9 + 1, p % 9 + 1, x, y, x, a / 9 + 1, a % 9 + 1, z, y, b / 9 + 1, b % 9 + 1,
                     z, z, z);
        } else {                                    /* X-Wing, Swordfish: base lines, cover lines */
            bool rows = t->unit < 9;
            const char *base = rows ? "rows" : "columns", *cov = rows ? "columns" : "rows";
            int lines[3] = {t->unit, t->unit2, t->unit3}, nb = t->kind == TECH_X_WING ? 2 : 3;
            char bl[24] = "", cl[24] = "";
            size_t lb = 0, lc = 0;
            for (int i = 0; i < nb; i++) {
                lb += snprintf(bl + lb, sizeof(bl) - lb, "%s%d", i == 0 ? "" : (i == nb - 1 ? " and " : ", "),
                               lines[i] % 9 + 1);
            }
            for (int j = 0, k = 0; j < 9; j++) {
                if (!(t->digit2 >> j & 1)) continue;
                lc += snprintf(cl + lc, sizeof(cl) - lc, "%s%d", k == 0 ? "" : (k == nb - 1 ? " and " : ", "), j + 1);
                k++;
            }
            snprintf(u1, sizeof(u1), "%s %s", base, bl);
            snprintf(tech, sizeof(tech), "In %s %s, %d can only be in %s %s. %s %s must put their %ds in those %s, "
                     "so no other cell of %s %s can be %d. ", base, bl, t->digit, cov, cl,
                     nb == 2 ? "Both" : "All three", base, t->digit, cov, cov, cl, t->digit);
        }
    } else if (t->kind) {
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
            snprintf(text, sizeof(text), hint_nudge() ? "No simple step here: this needs advanced techniques."
                                                       : "No simple step here: this needs advanced techniques. More shows one cell.");
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
    lv_obj_set_hidden(h_act, h_step == 1 && hint_nudge());   /* Settings > Game > Hints: Nudge only */

    /* the card at the bottom, or at the top when the bottom row matters */
    /* as tall as its text needs (a technique and its single take 4-6 lines), at least the
     * buttons' height; at the bottom (growing upwards) unless it would cover a highlighted
     * cell there, then at the top (growing downwards) */
    lv_obj_update_layout(h_text);
    int32_t ch = lv_obj_get_height(h_text) + 28;
    if (ch < 92) ch = 92;
    lv_obj_set_height(h_card, ch);
    bool covered = false;
    for (int i = 0; i < 81 && !covered; i++) {
        if (!h_mark[i] || !g_cells[i]) continue;
        lv_area_t co;
        lv_obj_get_coords(g_cells[i], &co);
        covered = co.y2 > SCR_W - 4 - ch;
    }
    lv_obj_set_y(h_card, covered ? 4 : SCR_W - 4 - ch);
    lv_obj_set_hidden(h_card, false);
    refresh_board();
}

static void hint_act_cb(lv_event_t *e)
{
    if (!h_step || g_solved || g_paused) return;
    if (h_step == 1 && hint_nudge()) return;
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
        if (g_practice >= 0) stats_db_learned(g_practice);   /* Learn: this technique practised (Graduate) */
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
    if (g_attack) {
        sudoku_ui_notice("Fill and Hint are off in a time attack.", 2500);
        return;
    }
    if (h_step) {                                   /* already open: same as More / Place it */
        if (hint_nudge()) return;                   /* (a nudge goes no further) */
        hint_act_cb(e);
        return;
    }
    if (!sudoku_next_step(g_geo, g_b.val, g_sol, &h_info) || h_info.cell < 0) return;
    if (g_hints < 0xFFFF) g_hints++;               /* every hint opened counts once */
    if (h_info.tech.kind > 0 && h_info.tech.kind < 16) g_flags |= 1u << h_info.tech.kind;   /* (achievements) */
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
    memset(g_b.colors, 0, sizeof(g_b.colors));
    g_elapsed_ms = 0;
    g_mistakes = 0;
    g_hints = 0;
    g_flags = store_no_notes() ? GAME_FLAG_NO_NOTES : 0;   /* (Settings > Game: the no-notes challenge) */
    g_nmoves = 0;                                   /* a new attempt, a new log */
    hint_drop();
    mark_started();                                 /* opened or restarted: a new attempt */
    g_undo_len = g_undo_head = 0;
    g_redo_len = 0;
    g_lock = 0;
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
    /* left-aligned, wrapped; NULL: the caller fills the content */
    if (text) make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT, text);
    lv_obj_set_width(m, 520);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, 0);
    return m;
}

static void msgbox_button(lv_obj_t *m, const char *text, lv_event_cb_t cb, bool primary)
{
    lv_obj_t *b = lv_msgbox_add_footer_button(m, text);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 52);
    if (!primary) {                                 /* Cancel, Close ...: grey, not the dialog's own white */
        lv_obj_set_style_bg_color(b, C_CARD2, 0);
        lv_obj_set_style_text_color(b, C_TEXT, 0);
    }
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

static void show_home(void);

static void atk_stop_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    g_attack = false;                               /* (not recorded: the run wasn't finished) */
    g_solved = true;
    show_home();
}

static void atk_keep_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    set_paused(false);
}

static void back_cb(lv_event_t *e)
{
    if (g_attack && !g_solved) {
        set_paused(true);
        lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Stop the run?", "A time attack only counts when its 10 "
                                  "minutes are up. Puzzles solved so far still count as solves.");
        msgbox_button(m, "Keep going", atk_keep_cb, false);
        msgbox_button(m, "Stop", atk_stop_cb, true);
        return;
    }
    save_current();
    show_list();
}

static void gen_start(int variant, int level);

static void win_next_cb(lv_event_t *e)
{
    if (g_from_coll >= PUZZLE_COLLECTION_COUNT && coll_saved_index(g_from_coll) >= 0) {   /* a saved collection */
        int i = coll_find(g_from_coll, g_id);
        if (i >= 0) {
            open_puzzle(coll_puzzle(g_from_coll, (i + 1) % coll_size(g_from_coll)));
            return;
        }
    }
    int ic = coll_of(g_id);
    if (ic >= PUZZLE_COLLECTION_COUNT) {         /* imported: the next one of its collection */
        uint16_t first = coll_first(ic), next = g_id + 1;
        if (next >= first + coll_size(ic)) next = first;
        open_puzzle(next);
        return;
    }
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
    bool running = g_id != STORE_NO_GAME && lv_screen_active() == scr_game && !g_solved && !g_paused && !tour_ovl;
    if (running) {
        g_elapsed_ms += now - g_last_tick;
        update_timer_label();
        if (g_attack && atk_used_ms + g_elapsed_ms >= ATK_MS) atk_finish();
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

/* Killer: each cage outlined with a dashed line just inside its cells, and
 * its sum small in its top-left cell (cells: the grid's children 9..89) */
static void board_cages_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    const int in = 4;                               /* the outline's inset from the cell edge */
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = C_MUTED;
    ld.width = 1;
    ld.dash_width = 4;
    ld.dash_gap = 3;
    for (int i = 0; i < 81; i++) {
        int k = geo->cage_of[i];
        if (k == CAGE_NONE) continue;
        lv_area_t a;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        int r = i / 9, c = i % 9;
        bool up = r > 0 && geo->cage_of[i - 9] == k, down = r < 8 && geo->cage_of[i + 9] == k;
        bool left = c > 0 && geo->cage_of[i - 1] == k, right = c < 8 && geo->cage_of[i + 1] == k;
        /* an edge on the cage's border is drawn inset; along a shared edge
         * the lines run on to the next cell so the outline stays joined */
        int x1 = left ? a.x1 - 1 : a.x1 + in, x2 = right ? a.x2 + 1 : a.x2 - in;
        int y1 = up ? a.y1 - 1 : a.y1 + in, y2 = down ? a.y2 + 1 : a.y2 - in;
        bool label = geo->cage_cell[k][0] == i;
        if (!up) {
            ld.p1.x = label ? a.x1 + in + 18 : x1;      /* room for the sum */
            ld.p1.y = ld.p2.y = a.y1 + in;
            ld.p2.x = x2;
            if (ld.p1.x < ld.p2.x) lv_draw_line(layer, &ld);
        }
        if (!down) {
            ld.p1.x = x1;
            ld.p2.x = x2;
            ld.p1.y = ld.p2.y = a.y2 - in;
            lv_draw_line(layer, &ld);
        }
        if (!left) {
            ld.p1.x = ld.p2.x = a.x1 + in;
            ld.p1.y = label ? a.y1 + in + 13 : y1;
            ld.p2.y = y2;
            if (ld.p1.y < ld.p2.y) lv_draw_line(layer, &ld);
        }
        if (!right) {
            ld.p1.x = ld.p2.x = a.x2 - in;
            ld.p1.y = y1;
            ld.p2.y = y2;
            lv_draw_line(layer, &ld);
        }
        if (label) {
            char sum[4];
            snprintf(sum, sizeof(sum), "%d", geo->cage_sum[k]);
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = sum;
            d.text_local = 1;
            lv_area_t la = {.x1 = a.x1 + 2, .y1 = a.y1 + 1, .x2 = a.x1 + 26, .y2 = a.y1 + 15};
            lv_draw_label(layer, &d, &la);
        }
    }
}

/* Over the cells: Jigsaw's thick lines between regions, Killer's cages (the
 * grid's children: the 9 boxes, then the 81 cells) */
/* Even-Odd: a ring in each odd cell (even ones are shaded, cell_base_bg);
 * Consecutive: a bar across the line between two cells 1 apart */
static void board_marks_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    for (int i = 0; i < 81; i++) {
        lv_area_t a, b;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        if (geo->parity[i] == PARITY_ODD) {
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_TRANSP;
            d.border_color = C_MUTED;
            d.border_width = 2;
            d.border_opa = LV_OPA_70;
            d.radius = LV_RADIUS_CIRCLE;
            lv_area_t r = {.x1 = a.x1 + 5, .y1 = a.y1 + 5, .x2 = a.x2 - 5, .y2 = a.y2 - 5};
            lv_draw_rect(layer, &d, &r);
        }
        if (!geo->consec_on) continue;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = C_ORANGE;
        d.bg_opa = LV_OPA_COVER;
        d.radius = 3;
        d.border_color = C_CARD;
        d.border_width = 1;
        if (i % 9 < 8 && (geo->consec[i] & CONSEC_RIGHT)) {          /* upright bar on the line to the right */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 1), &b);
            int cx = (a.x2 + b.x1) / 2, cy = (a.y1 + a.y2) / 2;
            lv_area_t r = {.x1 = cx - 4, .x2 = cx + 4, .y1 = cy - 12, .y2 = cy + 12};
            lv_draw_rect(layer, &d, &r);
        }
        if (i / 9 < 8 && (geo->consec[i] & CONSEC_DOWN)) {           /* flat bar on the line below */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 9), &b);
            int cx = (a.x1 + a.x2) / 2, cy = (a.y2 + b.y1) / 2;
            lv_area_t r = {.x1 = cx - 12, .x2 = cx + 12, .y1 = cy - 4, .y2 = cy + 4};
            lv_draw_rect(layer, &d, &r);
        }
    }
}

/* Thermo: each thermometer as a wide, soft grey line through its cells from
 * a round bulb (translucent: the digits show through) */
static void board_thermos_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int t = 0; t < geo->nthermos; t++) {
        lv_area_t a;
        int px = 0, py = 0, cs = 0;
        for (int i = 0; i < geo->thermo_n[t]; i++) {
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->thermo_cell[t][i]), &a);
            int x = (a.x1 + a.x2) / 2, y = (a.y1 + a.y2) / 2;
            cs = lv_area_get_width(&a);
            if (i) {
                lv_draw_line_dsc_t ld;
                lv_draw_line_dsc_init(&ld);
                ld.p1.x = px; ld.p1.y = py; ld.p2.x = x; ld.p2.y = y;
                ld.width = cs * 3 / 10;
                ld.color = C_MUTED;
                ld.opa = LV_OPA_40;
                ld.round_start = ld.round_end = 1;
                lv_draw_line(layer, &ld);
            }
            px = x;
            py = y;
        }
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->thermo_cell[t][0]), &a);   /* the bulb */
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = C_MUTED;
        d.bg_opa = LV_OPA_40;
        d.radius = LV_RADIUS_CIRCLE;
        int r = cs * 4 / 10;
        lv_area_t b = {.x1 = (a.x1 + a.x2) / 2 - r, .y1 = (a.y1 + a.y2) / 2 - r,
                       .x2 = (a.x1 + a.x2) / 2 + r, .y2 = (a.y1 + a.y2) / 2 + r};
        lv_draw_rect(layer, &d, &b);
    }
}

static void board_regions_draw_cb(lv_event_t *e)
{
    lv_obj_t *grid = lv_event_get_current_target_obj(e);
    const sudoku_geom_t *geo = lv_obj_get_user_data(grid);
    if (geo && geo->nthermos) board_thermos_draw(grid, geo, lv_event_get_layer(e));
    if (geo && geo->ncages) board_cages_draw(grid, geo, lv_event_get_layer(e));
    if (geo && (geo->consec_on || geo->variant == VAR_EVENODD)) board_marks_draw(grid, geo, lv_event_get_layer(e));
    if (!geo || geo->variant != VAR_JIGSAW) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = C_GRID_THICK;
    d.bg_opa = LV_OPA_COVER;
    for (int i = 0; i < 81; i++) {
        lv_area_t a, b, r;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        if (i % 9 < 8 && geo->region[i] != geo->region[i + 1]) {          /* border on the right */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 1), &b);
            r = (lv_area_t){.x1 = a.x2 - 1, .x2 = b.x1 + 1, .y1 = a.y1 - LINE_THIN, .y2 = a.y2 + LINE_THIN};
            lv_draw_rect(layer, &d, &r);
        }
        if (i / 9 < 8 && geo->region[i] != geo->region[i + 9]) {          /* border below */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 9), &b);
            r = (lv_area_t){.x1 = a.x1 - LINE_THIN, .x2 = a.x2 + LINE_THIN, .y1 = a.y2 - 1, .y2 = b.y1 + 1};
            lv_draw_rect(layer, &d, &r);
        }
    }
}

/* clues: 18 labels on the board's screen for a Sandwich's sums (rows, then
 * columns), hidden for the other variants; may be NULL */
static void board_layout(const sudoku_geom_t *geo, lv_obj_t *grid, lv_obj_t *const *boxes, lv_obj_t *const *cells,
                         lv_obj_t *const *clues, int x0)
{
    lv_obj_set_user_data(grid, (void *)geo);
    for (int i = 0; clues && i < 18; i++) lv_obj_set_hidden(clues[i], !geo->sandwich);
    if (geo->variant == VAR_JIGSAW) {
        /* evenly spaced cells on the thin-line colour in a thick frame; the
         * region borders are drawn over them (board_regions_draw_cb) */
        const int cs = (GRID_SIZE - 2 * LINE_THICK - 8 * LINE_THIN) / 9;
        const int w = 2 * LINE_THICK + 9 * cs + 8 * LINE_THIN;
        lv_obj_set_pos(grid, x0 + (GRID_SIZE - w) / 2, GRID_Y + (GRID_SIZE - w) / 2);
        lv_obj_set_size(grid, w, w);
        lv_obj_set_style_bg_color(grid, C_GRID_THIN, 0);
        lv_obj_set_style_border_color(grid, C_GRID_THICK, 0);
        lv_obj_set_style_border_width(grid, LINE_THICK, 0);
        for (int b = 0; b < 9; b++) lv_obj_set_hidden(boxes[b], true);
        for (int i = 0; i < 81; i++) {
            lv_obj_set_hidden(cells[i], false);
            lv_obj_set_pos(cells[i], LINE_THICK + (i % 9) * (cs + LINE_THIN), LINE_THICK + (i / 9) * (cs + LINE_THIN));
            lv_obj_set_size(cells[i], cs, cs);
        }
        lv_obj_invalidate(grid);
        return;
    }
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    const int n = geo->n, bw = geo->box_w, bh = geo->box_h;
    const int across = n / bw, down = n / bh;                  /* boxes */
    const int band = geo->sandwich ? 36 : 0;                   /* Sandwich: room for the sums, top and left */
    const int size = GRID_SIZE - band;
    int cw = (size - (across + 1) * LINE_THICK - across * (bw - 1) * LINE_THIN) / n;
    int ch = (size - (down + 1) * LINE_THICK - down * (bh - 1) * LINE_THIN) / n;
    int cs = cw < ch ? cw : ch;
    int box_w = bw * cs + (bw - 1) * LINE_THIN, box_h = bh * cs + (bh - 1) * LINE_THIN;
    int w = (across + 1) * LINE_THICK + across * box_w, h = (down + 1) * LINE_THICK + down * box_h;
    const int gx = x0 + band + (size - w) / 2, gy = GRID_Y + band + (size - h) / 2;
    lv_obj_set_pos(grid, gx, gy);
    lv_obj_set_size(grid, w, h);
    for (int i = 0; geo->sandwich && clues && i < 18; i++) {   /* the sums: left of the rows, above the columns */
        int k = i % 9;
        int mid = LINE_THICK + (k / 3) * (box_w + LINE_THICK) + (k % 3) * (cs + LINE_THIN) + cs / 2;
        lv_label_set_text_fmt(clues[i], "%d", geo->sandwich_sum[i]);
        lv_obj_set_size(clues[i], band - 6, 24);
        if (i < 9) lv_obj_set_pos(clues[i], gx - band, gy + mid - 12);
        else lv_obj_set_pos(clues[i], gx + mid - (band - 6) / 2, gy - band + 4);
    }
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
    const bool left = store_left_hand();           /* Settings > Game > Left-handed: the pad on the left */
    if (g_layout_var == g_var && g_layout_left == left && g_var != VAR_JIGSAW && g_var != VAR_KILLER &&
        g_var != VAR_EVENODD && g_var != VAR_CONSEC && g_var != VAR_THERMO && g_var != VAR_SANDWICH) {
        lv_obj_invalidate(g_grid);
        return;
    }
    lv_obj_invalidate(g_grid);                      /* every Jigsaw / Killer has its own regions / cages */
    g_layout_var = g_var;
    g_layout_left = left;
    const int board_x = left ? SCR_W - 8 - GRID_SIZE : GRID_X, pad_x = left ? 8 : PAD_X;
    board_layout(g_geo, g_grid, g_boxes, g_cells, g_clues, board_x);
    for (int i = 0; i < 81; i++) {                  /* (4x4: big digits) */
        if (g_cell_lbl[i]) lv_obj_set_style_text_font(g_cell_lbl[i], DIGIT_FONT_N(g_geo->n), 0);
    }
    lv_obj_set_x(g_pause_ovl, board_x);
    const int n = g_geo->n, span = GRID_SIZE - 6;              /* 598: the 9 x 62 px pad, as before */
    const int h = (span - (n - 1) * PAD_GAP) / n;
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *b = g_pad[d - 1];
        lv_obj_set_hidden(b, d > n);
        if (d > n) continue;
        lv_obj_set_pos(b, pad_x, GRID_Y + 1 + (d - 1) * (h + PAD_GAP) + (span - n * h - (n - 1) * PAD_GAP) / 2);
        lv_obj_set_size(b, PAD_W, h);
    }
}

static void start_game(uint16_t id, bool resume)
{
    g_practice = -1;                                /* (learn_open sets it after) */
    g_attack = false;                               /* (atk_open_next sets it after) */
    uint8_t given[81];
    if (!puzzle_get(id, given)) {
        sudoku_ui_notice("This puzzle isn't available (it was made on the board, and sudoku.db is missing).", 4000);
        return;
    }
    g_id = id;
    g_geo = puzzle_geom(id, g_geo_buf, &g_var);
    game_layout();
    for (int i = 0; i < 81; i++) {                  /* only the board's cells, digits 1..n */
        if (!g_geo->used[i] || given[i] > g_geo->n) given[i] = 0;
    }
    memcpy(g_given, given, sizeof(g_given));
    g_sol_ok = sudoku_solve(g_geo, g_given, g_sol);
    reset_game();
    const saved_game_t *s = store_saved_game();
    if (resume && s->id == id) {
        memcpy(g_b.val, s->val, sizeof(g_b.val));
        memcpy(g_b.notes, s->notes, sizeof(g_b.notes));
        for (int i = 0; i < 81; i++) g_b.colors[i] = s->colors[i] <= CELL_COLORS ? s->colors[i] : 0;
        for (int i = 0; i < 81; i++) {
            if (g_given[i]) g_b.val[i] = g_given[i];   /* givens are never editable */
            if (g_b.val[i] > g_geo->n || !g_geo->used[i]) g_b.val[i] = 0;
            if (!g_geo->used[i]) g_b.notes[i] = 0;
            g_b.notes[i] &= g_geo->all;
        }
        g_elapsed_ms = s->elapsed_ms;
        g_mistakes = s->mistakes;
        g_hints = s->hints;
        g_flags = s->flags;                         /* (a game keeps the challenge it started with) */
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
    challenge_ui();
    g_last_tick = lv_tick_get();
    compute_conflicts();
    refresh_board();
    g_starred = id < PUZZLE_GEN_RAM_BASE && stats_db_bookmarked(id);
    update_header();
    lv_screen_load(scr_game);
    rules_first_time();
    tour_maybe();
}

/* ------------------------------------------------------------- the tour */
/*
 * A first look at the game screen: the board, the pad, the tools, Hint, the
 * top bar and the timer, one at a time, each lit up with the rest dimmed. Once
 * per player (sudoku.db: seen 'tour'), the first time a puzzle is opened;
 * Settings > Game > Game screen tour shows it again. The clock stands still.
 */
#define TOUR_STEPS 6
static int tour_step;

static void tour_area(int step, lv_area_t *a)
{
    lv_obj_t *o[4] = {NULL};
    switch (step) {
    case 0: o[0] = g_grid; break;
    case 1: o[0] = g_pad[0]; o[1] = g_pad[8]; break;
    case 2: o[0] = g_undo_btn; o[1] = g_fill_btn; break;
    case 3: o[0] = g_hint_btn; break;
    case 4: o[0] = g_bulb_btn; o[1] = lv_obj_is_hidden(g_star_btn) ? g_hint_btn : g_star_btn; break;
    default: o[0] = g_timer_btn; break;
    }
    lv_obj_get_coords(o[0], a);
    for (int i = 1; i < 4 && o[i]; i++) {
        lv_area_t b;
        lv_obj_get_coords(o[i], &b);
        if (b.x1 < a->x1) a->x1 = b.x1;
        if (b.y1 < a->y1) a->y1 = b.y1;
        if (b.x2 > a->x2) a->x2 = b.x2;
        if (b.y2 > a->y2) a->y2 = b.y2;
    }
    lv_area_increase(a, 4, 4);
}

static void tour_show(int step);

static void tour_end(void)
{
    if (!tour_ovl) return;
    lv_obj_delete(tour_ovl);
    tour_ovl = NULL;
    stats_db_set_seen("tour", true);
    g_last_tick = lv_tick_get();                    /* (the clock goes on from now) */
}

static void tour_next_cb(lv_event_t *e)
{
    if (tour_step + 1 >= TOUR_STEPS) tour_end();
    else tour_show(tour_step + 1);
}

static void tour_skip_cb(lv_event_t *e)
{
    tour_end();
}

static void tour_dim(int x1, int y1, int x2, int y2)
{
    if (x2 <= x1 || y2 <= y1) return;
    lv_obj_t *r = lv_obj_create(tour_ovl);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, x1, y1);
    lv_obj_set_size(r, x2 - x1, y2 - y1);
    lv_obj_set_style_bg_color(r, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_60, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
}

static void tour_show(int step)
{
    static const char *const TITLE[TOUR_STEPS] = {"The board", "The number pad", "Tools", "Hint", "The top bar",
                                                  "The timer"};
    static const char *const TEXT[TOUR_STEPS] = {
        "Tap a cell, then a digit to put it there. Hold a cell to paint it a colour (in Killer: to see what its "
        "cage can hold).",
        "Puts the digit in the selected cell; the small number is how many are still to place. Hold a digit to "
        "light it up everywhere, then tap cells to place it.",
        "Undo and Redo; Erase a cell; Notes puts digits in as pencil marks; Auto adds all the marks at once; "
        "Restart; Fill puts the right digit in the selected cell (it counts as a hint).",
        "Shows where to look next. More then shows the cell and why, and Place it puts the digit in.",
        "The light bulb: this puzzle's rules. Share: the puzzle as a QR code. The star keeps it under Puzzles > "
        "Starred. List goes back to the puzzles: the game is saved after every move.",
        "Tap the timer to pause: the board is hidden until you go on. That's all - enjoy the puzzle!",
    };
    tour_step = step;
    if (!tour_ovl) {
        tour_ovl = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(tour_ovl);
        lv_obj_set_size(tour_ovl, SCR_W, SCR_W);
        lv_obj_add_flag(tour_ovl, LV_OBJ_FLAG_CLICKABLE);   /* nothing under it takes a tap */
        lv_obj_set_scrollable(tour_ovl, false);
    }
    lv_obj_clean(tour_ovl);
    lv_area_t a;
    tour_area(step, &a);
    tour_dim(0, 0, SCR_W, a.y1);                    /* everything but the target, dimmed */
    tour_dim(0, a.y2 + 1, SCR_W, SCR_W);
    tour_dim(0, a.y1, a.x1, a.y2 + 1);
    tour_dim(a.x2 + 1, a.y1, SCR_W, a.y2 + 1);
    lv_obj_t *ring = lv_obj_create(tour_ovl);
    lv_obj_remove_style_all(ring);
    lv_obj_set_pos(ring, a.x1, a.y1);
    lv_obj_set_size(ring, lv_area_get_width(&a), lv_area_get_height(&a));
    lv_obj_set_style_border_color(ring, C_ACCENT, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_radius(ring, 8, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(tour_ovl);
    lv_obj_set_width(card, 560);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text_fmt(t, "%s", TITLE[step]);
    const char *text = TEXT[step];
    if (step == 3 && store_hint_nudge()) text = "Shows where to look next (Settings > Game > Hints: Nudge only).";
    lv_obj_t *x = make_wrap_text(card, &lv_font_montserrat_18, C_TEXT, text);
    lv_obj_set_width(x, LV_PCT(100));
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 52);
    lv_obj_t *n = make_label(row, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text_fmt(n, "%d of %d", step + 1, TOUR_STEPS);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *nb = make_btn(row, 0, 0, 140, 52, step + 1 < TOUR_STEPS ? "Next  " LV_SYMBOL_RIGHT : "Done",
                            &lv_font_montserrat_18, tour_next_cb, NULL);
    lv_obj_set_style_bg_color(nb, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(nb, 0), lv_color_white(), 0);
    lv_obj_align(nb, LV_ALIGN_RIGHT_MID, 0, 0);
    if (step + 1 < TOUR_STEPS) {
        lv_obj_t *sb = make_btn(row, 0, 0, 110, 52, "Skip", &lv_font_montserrat_18, tour_skip_cb, NULL);
        lv_obj_set_style_bg_color(sb, C_CARD2, 0);
        lv_obj_align(sb, LV_ALIGN_RIGHT_MID, -150, 0);
    }
    lv_obj_update_layout(card);
    int h = lv_obj_get_height(card), y;
    if (a.y1 >= h + 16) y = a.y1 - h - 10;          /* above the target, or below it, or over its lower part */
    else if (SCR_W - a.y2 >= h + 16) y = a.y2 + 10;
    else y = SCR_W - h - 80;
    lv_obj_set_pos(card, (SCR_W - 560) / 2, y);
}

static void tour_check_cb(lv_timer_t *t)
{
    if (tour_ovl || lv_screen_active() != scr_game || g_practice >= 0 || g_attack || g_solved || !stats_db_ready())
        return;
    lv_obj_t *top = lv_layer_top();                 /* the rules card, a notice: another time */
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        if (!lv_obj_is_hidden(lv_obj_get_child(top, (int32_t)i))) return;
    }
    if (stats_db_seen("tour")) return;
    hint_drop();
    tour_show(0);
}

static void tour_maybe(void)
{
    lv_timer_t *t = lv_timer_create(tour_check_cb, 500, NULL);   /* (after Learn / Time attack set their flags) */
    lv_timer_set_repeat_count(t, 1);
}

/* ------------------------------------------------------------- time attack */

static void atk_open_next(void)
{
    if (!g_attack) return;                          /* stopped meanwhile */
    const puzzle_collection_t *pc = &puzzle_collections[0];   /* Easy */
    uint16_t id;
    int tries = 0;
    do {
        id = (uint16_t)(pc->first + esp_random() % pc->count);
    } while (++tries < 20 && (id == atk_last || id == store_saved_game()->id));   /* (not the Continue game) */
    atk_last = id;
    uint32_t used = atk_used_ms;
    start_game(id, false);
    g_attack = true;
    atk_used_ms = used;
    g_flags = 0;                                    /* (no challenges in a run) */
    challenge_ui();
    update_header();
}

static void atk_start(void)
{
    atk_used_ms = 0;
    atk_solved = atk_mistakes = 0;
    atk_last = STORE_NO_GAME;
    g_attack = true;
    atk_open_next();
}

static void atk_board_fill(lv_obj_t *c)
{
    stats_attack_row_t rows[STATS_ATTACK_ROWS];
    int n = stats_db_attack_board(rows, STATS_ATTACK_ROWS);
    if (n <= 0) {
        make_wrap_text(c, &lv_font_montserrat_18, C_MUTED, n < 0 ? "The leaderboard needs sudoku.db on a microSD card."
                                                                  : "No runs yet: be the first on the leaderboard.");
        return;
    }
    lv_obj_t *hd = make_label(c, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(hd, "Leaderboard: best run");
    for (int i = 0; i < n; i++) {
        lv_obj_t *r = lv_obj_create(c);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), 36);
        lv_color_t col = rows[i].me ? C_ACCENT : C_TEXT;
        lv_obj_t *l = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text_fmt(l, "%d.", i + 1);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(player_badge(r, player_find(0, rows[i].name), -1, 28, 10), LV_ALIGN_LEFT_MID, 28, 0);
        lv_obj_t *nm = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text(nm, rows[i].name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 64, 0);
        lv_obj_t *v = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text_fmt(v, "%d solved   (%d run%s)", rows[i].best, rows[i].runs, rows[i].runs == 1 ? "" : "s");
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void atk_go_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    atk_start();
}

static void atk_home_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    show_home();
}

/* Home > Time attack: the rules, the leaderboard, Start */
static void attack_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Time attack", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_t *x = make_wrap_text(c, &lv_font_montserrat_18, C_TEXT, "Solve as many Easy puzzles as you can in 10 "
                                 "minutes: the next one opens as soon as one is solved. Hint and Fill are off; pausing "
                                 "stops the clock and hides the board. Solves count as usual.");
    lv_obj_set_style_margin_bottom(x, 10, 0);
    atk_board_fill(c);
    msgbox_button(m, "Close", mbox_close_cb, false);
    msgbox_button(m, "Start", atk_go_cb, true);
}

/* the 10 minutes are up: the run's result, recorded, and the leaderboard */
static void atk_finish(void)
{
    g_solved = true;                                /* (g_attack stays: the header and 0:00 stay too) */                                /* (the board can't be changed now) */
    hint_drop();
    stats_db_record_attack(atk_solved, atk_mistakes);
    sfx_play(SFX_SOLVED);
    char t[96];
    snprintf(t, sizeof(t), "Time's up! You solved %d Easy puzzle%s, with %d mistake%s.", atk_solved,
             atk_solved == 1 ? "" : "s", atk_mistakes, atk_mistakes == 1 ? "" : "s");
    lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Time attack", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_t *x = make_wrap_text(c, &lv_font_montserrat_20, C_TEXT, t);
    lv_obj_set_style_margin_bottom(x, 10, 0);
    atk_board_fill(c);                              /* (the run above is queued first: it's in) */
    msgbox_button(m, "Home", atk_home_cb, false);
    msgbox_button(m, "Again", atk_go_cb, true);
    update_header();
}

/* ------------------------------------------------------------- rules card */
/*
 * The ? button in the game's top bar: the rules of the puzzle's variant, with
 * a small drawn example. Shown by itself the first time a variant (other
 * than Classic) is played (store_rules_seen()).
 */
typedef struct {
    uint8_t w, h;                       /* cells */
    uint8_t bw, bh;                     /* boxes drawn with thick lines (0 = none) */
    const char *digits;                 /* w*h: '1'..'9' or '.' */
    const char *tint;                   /* w*h: '1' = tinted (X, Hyper), may be NULL */
    const char *regions;                /* w*h region letters: thick lines between them (Jigsaw), may be NULL */
    const char *parity;                 /* w*h: 'e' shaded, 'o' ring, may be NULL */
    const char *cross;                  /* w*h: 'x' = can't be the digit (Anti-Knight), may be NULL */
    const uint8_t *bars;                /* pairs of cells with a bar, ends with 0xFF, may be NULL */
    const uint8_t *thermo;              /* cells from the bulb, ends with 0xFF, may be NULL */
    const uint8_t *cages;               /* per cage: sum, cells..., 0xFE; ends with 0xFF; may be NULL */
    int8_t left_clue;                   /* Sandwich: a sum left of the first row, -1 = none */
} rules_pic_t;

static const uint8_t PIC_BARS[] = {0, 1, 0xFF};    /* 4|5: a bar; 5 3 and 3 7: none */
static const uint8_t PIC_THERMO[] = {0, 1, 2, 3, 0xFF};
static const uint8_t PIC_CAGES[] = {11, 0, 1, 0xFE, 8, 2, 3, 6, 0xFE, 0xFF};   /* 9 + 2 = 11, and an empty 8 */

static const rules_pic_t RULES_PIC[VAR_COUNT] = {
    /* w, h, box w, box h, digits, tint, regions, parity, cross, bars, thermo, cages, sandwich clue */
    [VAR_CLASSIC] = {3, 3, 3, 3, "591763842", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_X] = {5, 5, 0, 0, "1...." ".2..." "..3.." "...4." "....5", "1000101010001000101010001", NULL, NULL, NULL, NULL,
               NULL, NULL, -1},
    [VAR_MINI] = {6, 4, 3, 2, "123456" "456123" "......" "......", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_HYPER] = {5, 5, 0, 0, "....." ".123." ".456." ".789." ".....", "0000001110011100111000000", NULL, NULL, NULL, NULL,
                   NULL, NULL, -1},
    [VAR_JIGSAW] = {5, 5, 0, 0, "....." "....." "....." "....." ".....", NULL, "aabbb" "aabbc" "adccc" "ddcee" "dddee",
                    NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_KILLER] = {4, 2, 0, 0, "92.." "....", NULL, NULL, NULL, NULL, NULL, NULL, PIC_CAGES, -1},
    [VAR_KNIGHT] = {5, 5, 0, 0, "....." "....." "..5.." "....." ".....", NULL, NULL, NULL,
                    ".x.x." "x...x" "....." "x...x" ".x.x.", NULL, NULL, NULL, -1},
    [VAR_EVENODD] = {4, 1, 0, 0, "4.7.", NULL, NULL, "e.o.", NULL, NULL, NULL, NULL, -1},
    [VAR_CONSEC] = {4, 1, 0, 0, "4537", NULL, NULL, NULL, NULL, PIC_BARS, NULL, NULL, -1},
    [VAR_THERMO] = {4, 1, 0, 0, "2458", NULL, NULL, NULL, NULL, NULL, PIC_THERMO, NULL, -1},
    [VAR_SANDWICH] = {6, 1, 0, 0, "614329", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 9},
    [VAR_KIDS] = {4, 4, 2, 2, "1234" "3412" "2143" "4321", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
};

static const char *const RULES_TEXT[VAR_COUNT] = {
    [VAR_CLASSIC] = "Fill the grid so that every row, every column and every 3x3 box holds the digits 1 to 9, each "
                    "once. The given digits can't be changed.",
    [VAR_X] = "Classic rules, and the two long diagonals (tinted) also hold 1 to 9, each once.",
    [VAR_MINI] = "A 6x6 grid: every row, column and 2x3 box holds 1 to 6, each once.",
    [VAR_HYPER] = "Classic rules, and the four tinted 3x3 windows also hold 1 to 9, each once.",
    [VAR_JIGSAW] = "Classic rules, but the boxes are irregular regions of nine cells, outlined with thick lines. "
                   "Every region holds 1 to 9, each once.",
    [VAR_KILLER] = "Classic rules, plus cages (dashed outlines): the digits in a cage add up to the small number "
                   "in its corner, and no digit repeats in a cage. Hold a cell to see which digits can make up "
                   "its cage.",
    [VAR_KNIGHT] = "Classic rules, and two cells a chess knight's move apart never hold the same digit: the 5 "
                   "here means none of the crossed cells can be 5.",
    [VAR_EVENODD] = "Classic rules, and some cells are marked: a shaded cell holds an even digit (2, 4, 6, 8), a "
                    "cell with a ring an odd one (1, 3, 5, 7, 9).",
    [VAR_CONSEC] = "Classic rules, and a bar joins two side-by-side cells whose digits are 1 apart (4 and 5). "
                   "Where there is no bar, they are not 1 apart (5 and 3, 3 and 7).",
    [VAR_THERMO] = "Classic rules, and along each thermometer the digits rise from the round bulb: every digit "
                   "is bigger than the one before it.",
    [VAR_SANDWICH] = "Classic rules, and the number by each row (left) and column (top) is the sum of the digits "
                     "between that line's 1 and 9, wherever they are: here 4 + 3 + 2 = 9.",
    [VAR_KIDS] = "A small 4x4 grid to start with: every row, every column and every 2x2 box holds 1, 2, 3 and 4, "
                 "each once.",
};

static void rules_pic_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    const rules_pic_t *p = lv_obj_get_user_data(obj);
    if (!p) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const int cs = 40, w = p->w * cs, h = p->h * cs;
    const int x0 = (co.x1 + co.x2) / 2 - w / 2, y0 = (co.y1 + co.y2) / 2 - h / 2;
    lv_draw_rect_dsc_t r;
    lv_draw_line_dsc_t l;
    for (int i = 0; i < p->w * p->h; i++) {        /* cells */
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_draw_rect_dsc_init(&r);
        lv_color_t bg = C_CELL;
        if (p->tint && p->tint[i] == '1') bg = lv_color_mix(C_ORANGE, bg, 40);
        if (p->parity && p->parity[i] == 'e') bg = lv_color_mix(C_GRID_THIN, bg, 150);
        r.bg_color = bg;
        r.border_color = C_GRID_THIN;
        r.border_width = 1;
        lv_area_t a = {.x1 = x, .y1 = y, .x2 = x + cs, .y2 = y + cs};
        lv_draw_rect(layer, &r, &a);
    }
    lv_draw_line_dsc_init(&l);                      /* thick lines: boxes or regions, and the frame */
    l.color = C_GRID_THICK;
    l.width = 3;
    for (int i = 0; i < p->w * p->h; i++) {
        int c = i % p->w, rr = i / p->w, x = x0 + c * cs, y = y0 + rr * cs;
        bool right = c + 1 < p->w && (p->regions ? p->regions[i] != p->regions[i + 1] : p->bw && (c + 1) % p->bw == 0);
        bool down = rr + 1 < p->h && (p->regions ? p->regions[i] != p->regions[i + p->w] : p->bh && (rr + 1) % p->bh == 0);
        if (right) { l.p1.x = l.p2.x = x + cs; l.p1.y = y; l.p2.y = y + cs; lv_draw_line(layer, &l); }
        if (down) { l.p1.y = l.p2.y = y + cs; l.p1.x = x; l.p2.x = x + cs; lv_draw_line(layer, &l); }
    }
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_TRANSP;
    r.border_color = C_GRID_THICK;
    r.border_width = 3;
    lv_area_t fr = {.x1 = x0 - 1, .y1 = y0 - 1, .x2 = x0 + w + 1, .y2 = y0 + h + 1};
    lv_draw_rect(layer, &r, &fr);
    if (p->thermo) {                                /* a thermometer under the digits */
        lv_draw_line_dsc_init(&l);
        l.color = C_MUTED;
        l.opa = LV_OPA_40;
        l.width = cs * 3 / 10;
        l.round_start = l.round_end = 1;
        for (int k = 1; p->thermo[k] != 0xFF; k++) {
            int a = p->thermo[k - 1], b = p->thermo[k];
            l.p1.x = x0 + (a % p->w) * cs + cs / 2; l.p1.y = y0 + (a / p->w) * cs + cs / 2;
            l.p2.x = x0 + (b % p->w) * cs + cs / 2; l.p2.y = y0 + (b / p->w) * cs + cs / 2;
            lv_draw_line(layer, &l);
        }
        lv_draw_rect_dsc_init(&r);
        r.bg_color = C_MUTED;
        r.bg_opa = LV_OPA_40;
        r.radius = LV_RADIUS_CIRCLE;
        int b = p->thermo[0], cx = x0 + (b % p->w) * cs + cs / 2, cy = y0 + (b / p->w) * cs + cs / 2, rad = cs * 4 / 10;
        lv_area_t a = {.x1 = cx - rad, .y1 = cy - rad, .x2 = cx + rad, .y2 = cy + rad};
        lv_draw_rect(layer, &r, &a);
    }
    for (int i = 0; p->parity && i < p->w * p->h; i++) {   /* odd rings */
        if (p->parity[i] != 'o') continue;
        lv_draw_rect_dsc_init(&r);
        r.bg_opa = LV_OPA_TRANSP;
        r.border_color = C_MUTED;
        r.border_width = 2;
        r.radius = LV_RADIUS_CIRCLE;
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_area_t a = {.x1 = x + 4, .y1 = y + 4, .x2 = x + cs - 4, .y2 = y + cs - 4};
        lv_draw_rect(layer, &r, &a);
    }
    for (int k = 0; p->bars && p->bars[k] != 0xFF; k += 2) {   /* consecutive bars */
        int a = p->bars[k], b = p->bars[k + 1];
        lv_draw_rect_dsc_init(&r);
        r.bg_color = C_ORANGE;
        r.radius = 3;
        int x = x0 + (a % p->w + 1) * cs, y = y0 + (a / p->w) * cs + cs / 2;
        (void)b;
        lv_area_t ar = {.x1 = x - 4, .y1 = y - 11, .x2 = x + 4, .y2 = y + 11};
        lv_draw_rect(layer, &r, &ar);
    }
    if (p->cages) {                                 /* dashed cages with their sums */
        const uint8_t *q = p->cages;
        while (*q != 0xFF) {
            int sum = *q++, first = *q;
            uint8_t in[16];
            int n = 0;
            while (*q != 0xFE) in[n++] = *q++;
            q++;
            lv_draw_line_dsc_init(&l);
            l.color = C_MUTED;
            l.width = 1;
            l.dash_width = 4;
            l.dash_gap = 3;
            for (int j = 0; j < n; j++) {
                int c = in[j], cx = c % p->w, cy = c / p->w, x = x0 + cx * cs, y = y0 + cy * cs;
                bool up = false, dn = false, lf = false, rt = false;
                for (int k = 0; k < n; k++) {
                    up |= in[k] == c - p->w;
                    dn |= in[k] == c + p->w;
                    lf |= cx > 0 && in[k] == c - 1;
                    rt |= cx < p->w - 1 && in[k] == c + 1;
                }
                if (!up) { l.p1.x = x + 4; l.p2.x = x + cs - 4; l.p1.y = l.p2.y = y + 4; lv_draw_line(layer, &l); }
                if (!dn) { l.p1.x = x + 4; l.p2.x = x + cs - 4; l.p1.y = l.p2.y = y + cs - 4; lv_draw_line(layer, &l); }
                if (!lf) { l.p1.y = y + 4; l.p2.y = y + cs - 4; l.p1.x = l.p2.x = x + 4; lv_draw_line(layer, &l); }
                if (!rt) { l.p1.y = y + 4; l.p2.y = y + cs - 4; l.p1.x = l.p2.x = x + cs - 4; lv_draw_line(layer, &l); }
            }
            char t[4];
            snprintf(t, sizeof(t), "%d", sum);
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = t;
            d.text_local = 1;
            lv_area_t la = {.x1 = x0 + (first % p->w) * cs + 3, .y1 = y0 + (first / p->w) * cs + 2,
                            .x2 = x0 + (first % p->w) * cs + 30, .y2 = y0 + (first / p->w) * cs + 16};
            lv_draw_label(layer, &d, &la);
        }
    }
    if (p->left_clue >= 0) {                        /* Sandwich: the sum left of the row */
        char t[4];
        snprintf(t, sizeof(t), "%d", p->left_clue);
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_22;
        d.color = C_ACCENT;
        d.align = LV_TEXT_ALIGN_RIGHT;
        d.text = t;
        d.text_local = 1;
        lv_area_t a = {.x1 = x0 - 50, .y1 = y0 + (cs - 24) / 2, .x2 = x0 - 10, .y2 = y0 + cs};
        lv_draw_label(layer, &d, &a);
    }
    for (int i = 0; i < p->w * p->h; i++) {         /* digits and crosses */
        char t[2] = {0, 0};
        lv_color_t col = C_TEXT;
        if (p->digits[i] >= '1' && p->digits[i] <= '9') t[0] = p->digits[i];
        else if (p->cross && p->cross[i] == 'x') { t[0] = 'x'; col = C_RED; }
        if (!t[0]) continue;
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_22;
        d.color = col;
        d.align = LV_TEXT_ALIGN_CENTER;
        d.text = t;
        d.text_local = 1;
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_area_t a = {.x1 = x, .y1 = y + (cs - 24) / 2, .x2 = x + cs, .y2 = y + cs};
        lv_draw_label(layer, &d, &a);
    }
}

static void rules_show(int variant)
{
    if (variant < 0 || variant >= VAR_COUNT) variant = VAR_CLASSIC;
    char title[40];
    snprintf(title, sizeof(title), "%s rules", VARIANT_NAMES[variant]);
    lv_obj_t *m = make_msgbox(title, NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *pic = lv_obj_create(lv_msgbox_get_content(m));
    lv_obj_remove_style_all(pic);
    const rules_pic_t *p = &RULES_PIC[variant];
    lv_obj_set_size(pic, LV_PCT(100), p->h * 40 + 16);
    lv_obj_set_user_data(pic, (void *)p);
    lv_obj_add_event_cb(pic, rules_pic_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_t *t = make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT, RULES_TEXT[variant]);
    lv_obj_set_style_margin_top(t, 10, 0);
    msgbox_button(m, "OK", mbox_close_cb, true);
}

static void rules_cb(lv_event_t *e)
{
    rules_show(g_var);
}

/* the first time a variant is played: its rules */
static void rules_first_time(void)
{
    uint32_t seen = store_rules_seen();
    if (g_var == VAR_CLASSIC || (seen >> g_var & 1)) return;
    store_set_rules_seen(seen | (1u << g_var));
    rules_show(g_var);
}

/* ------------------------------------------------------------- share (QR code) */
/*
 * The Share button: the puzzle as a QR code to scan with a phone. A Classic
 * (or imported) puzzle is the usual 81-digit string, givens row by row with 0
 * for empty, which most Sudoku apps can import; a variant is its name, the
 * givens and its layout, in the text forms sudoku.db keeps them in.
 */
static uint8_t *qr_code;                            /* qrcodegen_BUFFER_LEN_MAX each, PSRAM */
static uint8_t *qr_temp;

/* the text in the code; false if it doesn't fit buf */
static bool share_text(char *buf, size_t n)
{
    const sudoku_geom_t *G = g_geo;
    char giv[82];
    int k = 0;
    for (int i = 0; i < G->ncells; i++) giv[k++] = (char)('0' + g_given[G->cells[i]]);   /* (6x6: its 36 cells) */
    giv[k] = 0;
    if (g_var == VAR_CLASSIC) return (size_t)snprintf(buf, n, "%s", giv) < n;
    int len = snprintf(buf, n, "%s %s", VARIANT_NAMES[g_var], giv);
    if (len < 0 || (size_t)len >= n) return false;
    static char extra[STATS_CAGES_LEN];
    extra[0] = 0;
    const char *key = NULL;
    switch (g_var) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) extra[i] = (char)('0' + G->region[i]);
        extra[81] = 0;
        key = "regions";
        break;
    case VAR_KILLER: key = sudoku_cages_text(G, extra, sizeof(extra)) ? "cages" : NULL; break;
    case VAR_EVENODD:
    case VAR_CONSEC: key = sudoku_marks_text(G, extra, sizeof(extra)) ? "marks" : NULL; break;
    case VAR_THERMO: key = sudoku_thermos_text(G, extra, sizeof(extra)) ? "thermos" : NULL; break;
    case VAR_SANDWICH: key = sudoku_sandwich_text(G, extra, sizeof(extra)) ? "sums" : NULL; break;
    default: break;
    }
    if (key) len += snprintf(buf + len, n - (size_t)len, " %s=%s", key, extra);
    return (size_t)len < n;
}

/* the code: black modules on white (whatever the theme), with a quiet zone of 3 */
static void qr_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    int size = qrcodegen_getSize(qr_code);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    int w = lv_area_get_width(&co), h = lv_area_get_height(&co), side = w < h ? w : h;
    int mod = side / (size + 6);                    /* whole pixels per module, the code and its margin fitting */
    if (mod < 1) mod = 1;
    int x0 = co.x1 + (w - mod * size) / 2, y0 = co.y1 + (h - mod * size) / 2;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_white();
    d.radius = 6;
    lv_area_t bg = {.x1 = x0 - 3 * mod, .y1 = y0 - 3 * mod, .x2 = x0 + (size + 3) * mod - 1, .y2 = y0 + (size + 3) * mod - 1};
    lv_draw_rect(layer, &d, &bg);                   /* white, with the 3-module quiet zone */
    d.radius = 0;
    d.bg_color = lv_color_black();
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size;) {                /* a row's runs of dark modules as one rectangle each */
            if (!qrcodegen_getModule(qr_code, x, y)) { x++; continue; }
            int x1 = x;
            while (x < size && qrcodegen_getModule(qr_code, x, y)) x++;
            lv_area_t a = {.x1 = x0 + x1 * mod, .y1 = y0 + y * mod, .x2 = x0 + x * mod - 1, .y2 = y0 + (y + 1) * mod - 1};
            lv_draw_rect(layer, &d, &a);
        }
    }
}

/* a QR code of text in a dialog, the note under it, then the text itself */
static lv_obj_t *qr_dialog(const char *title, const char *text, const char *note)
{
    if (!qr_code) qr_code = heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_SPIRAM);
    if (!qr_temp) qr_temp = heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_SPIRAM);
    if (!qr_code || !qr_temp ||
        !qrcodegen_encodeText(text, qr_temp, qr_code, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true)) {
        return NULL;
    }
    lv_obj_t *m = make_msgbox(title, NULL);
    lv_obj_set_width(m, 560);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_t *qr = lv_obj_create(c);
    lv_obj_remove_style_all(qr);
    lv_obj_set_size(qr, LV_PCT(100), 340);
    lv_obj_add_event_cb(qr, qr_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    make_wrap_text(c, &lv_font_montserrat_16, C_MUTED, note);
    lv_obj_t *t = make_wrap_text(c, &lv_font_montserrat_12, C_TEXT, text);
    lv_obj_set_style_margin_top(t, 6, 0);
    msgbox_button(m, "Close", mbox_close_cb, false);
    return m;
}

/* Solved dialog of a daily puzzle: the result as a QR code, for a phone (and a message to friends) */
static void win_share_cb(lv_event_t *e)
{
    char day[12] = "", kind[48] = "", t[16];
    int var = 0, lv = 0;
    if (!puzzle_is_generated(s_solve_id) || !gen_lookup_day(s_solve_id, NULL, &var, &lv, day) || !day[0]) return;
    daily_kind_text(var, lv, kind, sizeof(kind));
    fmt_time(t, sizeof(t), s_solve_secs);
    char hints[24];
    if (s_solve_hints) snprintf(hints, sizeof(hints), "%u hint%s", (unsigned)s_solve_hints, s_solve_hints == 1 ? "" : "s");
    else snprintf(hints, sizeof(hints), "no hints");
    static char text[256];
    snprintf(text, sizeof(text), "Sudoku daily %s (%s)\n%s: solved in %s, %u mistake%s, %s%s\nESP32-P4 Sudoku",
             day, kind, store_player_name(), t, (unsigned)s_solve_mistakes, s_solve_mistakes == 1 ? "" : "s", hints,
             (s_solve_flags & GAME_FLAG_NO_NOTES) ? ", no notes" : "");
    if (!qr_dialog(SYMBOL_QR "  Today's result", text,
                   "Scan it with a phone to send it to friends: the day, the puzzle, your time and mistakes.")) {
        sudoku_ui_notice("Couldn't make a QR code of the result.", 3000);
    }
}

static void sv_changed(void);

/* ------------------------------------------------------------- send a puzzle by sound */
/*
 * Share > Send by sound plays the puzzle on screen as Gibberlink (ggwave) messages;
 * another board with Gibberlink on (Settings > Sound) takes it in, checks it has one
 * solution, keeps it in Puzzles > Saved > "Received" and offers to play it.
 *
 * The puzzle is its .sdx line (stats_db.c): variant|level|givens|regions|cages|marks|
 * thermos, the variant and level as numbers (level "-": the receiver rates it), the
 * givens shortened (a run of 1-26 empty cells is one letter, a-z), trailing empty
 * fields left out. A message carries at most 140 bytes, so the line goes in parts:
 *
 *   SU$pz <id> <i>/<n> <part>*<crc>
 *
 * id: 2 hex digits, the same in all parts of one puzzle; crc: CRC-16/CCITT-FALSE of
 * the text after "SU$" (gibberlink.h). A Classic one is one part (about 6 s); a
 * Killer one three or four. The receiver plays no answer to a part (devctl "pz").
 */
#define PZ_ID        "SU"                         /* our Gibberlink sender id for puzzles */
#define PZ_PART      110                          /* characters of the line per message */
#define PZ_MAX_PARTS 16
#define PZ_TIMEOUT_MS 90000                       /* a puzzle's parts must all come within this */

static uint16_t pz_crc16(const char *p, size_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)((uint8_t)*p++ << 8);
        for (int k = 0; k < 8; k++) c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}

/* the puzzle on screen as that line (sdx_format, the givens packed); its length, 0 if it doesn't fit */
static int pz_line(char *buf, size_t n)
{
    const sudoku_geom_t *G = g_geo;
    int v = VAR_CLASSIC, lv = -1;
    if (puzzle_is_generated(g_id)) gen_lookup(g_id, NULL, &v, &lv);
    static char extra[STATS_CAGES_LEN];
    char regions[82] = "";
    const char *cages = "", *marks = "", *thermos = "";
    extra[0] = 0;
    switch (g_var) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) regions[i] = (char)('0' + G->region[i]);
        regions[81] = 0;
        break;
    case VAR_KILLER:   if (sudoku_cages_text(G, extra, sizeof(extra))) cages = extra; break;
    case VAR_EVENODD:
    case VAR_CONSEC:   if (sudoku_marks_text(G, extra, sizeof(extra))) marks = extra; break;
    case VAR_SANDWICH: if (sudoku_sandwich_text(G, extra, sizeof(extra))) marks = extra; break;
    case VAR_THERMO:   if (sudoku_thermos_text(G, extra, sizeof(extra))) thermos = extra; break;
    default: break;
    }
    int len = sdx_format(g_var, lv >= 0 && lv < GEN_LEVELS ? lv : -1, g_given, regions, cages, marks, thermos, true,
                         buf, n);
    return len < 0 ? 0 : len;
}

static lv_timer_t *pz_send_timer;

static void pz_send_poll(lv_timer_t *t)
{
    if (gibberlink_sending()) return;
    lv_timer_delete(pz_send_timer);
    pz_send_timer = NULL;
    sudoku_ui_notice("Sent by sound.", 2500);
}

static void pz_send_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (pz_send_timer || gibberlink_sending()) {
        sudoku_ui_notice("Still sending the last one.", 2500);
        return;
    }
    static char line[STATS_CAGES_LEN + 400];
    int len = pz_line(line, sizeof(line));
    int parts = (len + PZ_PART - 1) / PZ_PART;
    if (!len || parts > PZ_MAX_PARTS) {
        sudoku_ui_notice("This puzzle is too long to send by sound.", 3000);
        return;
    }
    unsigned id = esp_random() & 0xFF;
    for (int i = 0; i < parts; i++) {
        char body[160], msg[176];
        int k = len - i * PZ_PART < PZ_PART ? len - i * PZ_PART : PZ_PART;
        snprintf(body, sizeof(body), "pz %02X %d/%d %.*s", id, i + 1, parts, k, line + i * PZ_PART);
        snprintf(msg, sizeof(msg), PZ_ID "$%s*%04X", body, pz_crc16(body, strlen(body)));
        if (!gibberlink_send(msg)) {
            sudoku_ui_notice("Couldn't send it: no memory for the sound.", 3000);
            return;
        }
    }
    char name[48], msg[200];
    puzzle_name(g_id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "Sending %s by sound: %d part%s, about %d s. On the other board, Gibberlink must be "
             "on (Settings > Sound).", name, parts, parts == 1 ? "" : "s", (len + 20 * parts) / 16 + parts + 1);
    sudoku_ui_notice(msg, 5000);
    pz_send_timer = lv_timer_create(pz_send_poll, 500, NULL);
}

/* ---- receiving: the parts gathered (devctl "pz"), then checked off the LVGL task */
static struct {
    int id, n;                                    /* id -1: nothing coming */
    uint32_t got;                                 /* bit i-1: part i is here */
    uint32_t start;                               /* lv_tick of the first part */
    char part[PZ_MAX_PARTS][PZ_PART + 1];
} pz_rx = {.id = -1};

/* the received puzzle, while it's checked: the fields of its line and the result */
typedef struct {
    int variant, level;                           /* level -1: rate it */
    uint8_t givens[81];
    char regions[82], cages[STATS_CAGES_LEN], marks[82], thermos[STATS_THERMOS_LEN];
    sudoku_geom_t geom;
    volatile int graded;                          /* sudoku_grade: -1 not one solution; -2 still checking */
} pz_got_t;
static pz_got_t *pz_got;                          /* PSRAM */
static lv_timer_t *pz_check_timer;
static TaskHandle_t pz_check_task;

static void pz_check_fn(void *arg)
{
    pz_got->graded = sudoku_grade(&pz_got->geom, pz_got->givens);
    vTaskSuspend(NULL);                           /* (deleted by pz_check_poll) */
}

static uint16_t pz_play_id;                       /* the one just received (its dialog's Play) */

static void pz_play_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    open_puzzle(pz_play_id);
}

static void pz_check_poll(lv_timer_t *t)
{
    if (pz_got->graded == -2 || eTaskGetState(pz_check_task) != eSuspended) return;
    vTaskDeleteWithCaps(pz_check_task);
    pz_check_task = NULL;
    lv_timer_delete(pz_check_timer);
    pz_check_timer = NULL;
    pz_got_t *p = pz_got;
    if (p->graded < 0) {
        sudoku_ui_notice("A puzzle came by sound, but it doesn't have exactly one solution: not kept.", 4000);
        return;
    }
    int level = p->level >= 0 ? p->level : p->graded;
    if (level < 0 || level >= GEN_LEVELS) level = GEN_EXPERT;
    const uint8_t *regions = p->variant == VAR_JIGSAW ? p->geom.region : NULL;
    const char *cages = p->cages[0] ? p->cages : NULL, *marks = p->marks[0] ? p->marks : NULL;
    const char *thermos = p->thermos[0] ? p->thermos : NULL;
    int gid = stats_db_ready() ? stats_db_gen_add(p->variant, level, p->givens, regions, cages, marks, thermos, NULL)
                               : -1;
    uint16_t id;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = s_gen_ram_next++;                    /* no card: until power-off */
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
    }
    gen_cache_put(id, p->variant, level, p->givens, regions, cages, marks, thermos, NULL);
    char why[96] = "", text[200];
    bool kept = gid > 0 && stats_db_saved_add("Received", id, why, sizeof(why));
    if (kept) sv_changed();                       /* (Puzzles' column: "Received" may be new) */
    snprintf(text, sizeof(text), "%s %s came by sound%s.", VARIANT_NAMES[p->variant], GEN_LEVEL_NAMES[level],
             kept ? ": it's in Puzzles > Saved > Received" : gid > 0 ? "" : " (kept until power-off: no card)");
    pz_play_id = id;
    lv_obj_t *m = make_msgbox(LV_SYMBOL_AUDIO "  Puzzle received", text);
    msgbox_button(m, "Later", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_PLAY "  Play", pz_play_cb, true);
}

/* the whole line: its fields read (sdx_parse), the board made, the check started; why on failure */
static bool pz_take(char *line, char *why, size_t n)
{
    if (pz_check_timer) { snprintf(why, n, "busy with the last one"); return false; }
    if (!pz_got && !(pz_got = heap_caps_calloc(1, sizeof(*pz_got), MALLOC_CAP_SPIRAM))) {
        snprintf(why, n, "no memory");
        return false;
    }
    pz_got_t *p = pz_got;
    sdx_puzzle_t sp;
    if (!sdx_parse(line, &sp)) { snprintf(why, n, "it doesn't read as a puzzle"); return false; }
    p->variant = sp.variant;
    p->level = sp.level;
    memcpy(p->givens, sp.givens, 81);
    snprintf(p->regions, sizeof(p->regions), "%s", sp.regions);
    snprintf(p->cages, sizeof(p->cages), "%s", sp.cages);
    snprintf(p->marks, sizeof(p->marks), "%s", sp.marks);
    snprintf(p->thermos, sizeof(p->thermos), "%s", sp.thermos);
    if (!sdx_geom(&sp, &p->geom)) { snprintf(why, n, "its layout doesn't read"); return false; }
    p->graded = -2;
    if (xTaskCreatePinnedToCoreWithCaps(pz_check_fn, "pzcheck", 16384, NULL, 2, &pz_check_task, WORKER_CORE,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        snprintf(why, n, "no memory for the check");
        return false;
    }
    pz_check_timer = lv_timer_create(pz_check_poll, 50, NULL);
    sudoku_ui_notice("A puzzle came by sound: checking it...", 2000);
    return true;
}

bool sudoku_ui_puzzle_part(const char *args, char *out, size_t n)
{
    unsigned id;
    int i, cnt, off = 0;
    while (*args == ' ') args++;
    if (sscanf(args, "%x %d/%d %n", &id, &i, &cnt, &off) < 3 || !off || cnt < 1 || cnt > PZ_MAX_PARTS || i < 1 ||
        i > cnt) {
        snprintf(out, n, "ERR pz: pz <id> <i>/<n> <part>");
        return false;
    }
    const char *data = args + off;
    if (strlen(data) > PZ_PART) {
        snprintf(out, n, "ERR pz: a part is %d characters at most", PZ_PART);
        return false;
    }
    if (pz_rx.id != (int)id || pz_rx.n != cnt || lv_tick_elaps(pz_rx.start) > PZ_TIMEOUT_MS) {   /* a new one */
        pz_rx.id = (int)id;
        pz_rx.n = cnt;
        pz_rx.got = 0;
        pz_rx.start = lv_tick_get();
    }
    snprintf(pz_rx.part[i - 1], sizeof(pz_rx.part[0]), "%s", data);
    pz_rx.got |= 1u << (i - 1);
    int have = __builtin_popcount(pz_rx.got);
    if (have < cnt) {
        char msg[64];
        snprintf(msg, sizeof(msg), LV_SYMBOL_AUDIO "  Puzzle by sound: part %d of %d", have, cnt);
        sudoku_ui_notice(msg, 3000);
        snprintf(out, n, "OK pz %d/%d", have, cnt);
        return true;
    }
    static char line[PZ_MAX_PARTS * PZ_PART + 1];
    line[0] = 0;
    for (int k = 0; k < cnt; k++) strlcat(line, pz_rx.part[k], sizeof(line));
    pz_rx.id = -1;
    char why[64];
    if (!pz_take(line, why, sizeof(why))) {
        char msg[120];
        snprintf(msg, sizeof(msg), "A puzzle came by sound, but %s.", why);
        sudoku_ui_notice(msg, 4000);
        snprintf(out, n, "ERR pz: %s", why);
        return false;
    }
    snprintf(out, n, "OK pz done");
    return true;
}

static void share_cb(lv_event_t *e)
{
    if (g_id == STORE_NO_GAME) return;
    static char text[STATS_CAGES_LEN + 128];
    char name[48], title[64];
    puzzle_name(g_id, name, sizeof(name));
    snprintf(title, sizeof(title), SYMBOL_QR "  %s", name);
    lv_obj_t *m = share_text(text, sizeof(text))
        ? qr_dialog(title, text, g_var == VAR_CLASSIC ? "Scan it with a phone: the givens row by row, 0 = empty. Most "
                                                        "Sudoku apps can import this. Or send it by sound to another "
                                                        "board (Gibberlink on there)."
                                                      : "Scan it with a phone: the variant, the givens row by row "
                                                        "(0 = empty) and its layout. Or send it by sound to another "
                                                        "board (Gibberlink on there).")
        : NULL;
    if (!m) {
        sudoku_ui_notice("Couldn't make a QR code of this puzzle.", 3000);
        return;
    }
    msgbox_button(m, LV_SYMBOL_AUDIO "  Send by sound", pz_send_cb, true);   /* (another board, by Gibberlink) */
}

/* ------------------------------------------------------------- bookmarks */

/* ------------------------------------------------------------- save to a collection */
/*
 * The game screen's Save (generated and typed-in puzzles): a dialog lists the saved
 * collections, ticked where the puzzle is; a tap puts it in or takes it out, and
 * "New collection..." asks for a name (the keyboard) and puts it there. They show in
 * Puzzles > Saved. A collection goes when its last puzzle is taken out.
 */
static lv_obj_t *sv_box;

static void sv_open(void);

static void sv_close_cb(lv_event_t *e)
{
    if (sv_box) lv_msgbox_close_async(sv_box);
    sv_box = NULL;
}

/* after a change: the collections again, a new or gone one in Puzzles' row */
static void sv_changed(void)
{
    int before = s_nsaved;
    imports_reload();
    if (s_nsaved != before) list_fill_colls();
    refresh_list();
    update_header();
}

static void sv_toggle_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k < 0 || k >= s_nsaved) return;
    char name[STATS_IMPORT_NAME], msg[96], why[96];
    snprintf(name, sizeof(name), "%s", s_saved[k].name);
    if (coll_find(PUZZLE_COLLECTION_COUNT + s_nimports + k, g_id) >= 0) {
        bool last = s_saved[k].count == 1;
        stats_db_saved_remove(s_saved[k].id, g_id);
        snprintf(msg, sizeof(msg), last ? "Taken out of %s (it was the last: the collection is gone)."
                                        : "Taken out of %s.", name);
    } else if (stats_db_saved_add(name, g_id, why, sizeof(why))) {
        snprintf(msg, sizeof(msg), "Saved to %s (Puzzles > Saved).", name);
    } else {
        snprintf(msg, sizeof(msg), "%s", why);
    }
    sv_changed();
    sudoku_ui_notice(msg, 2500);
    sv_close_cb(NULL);
    sv_open();                                      /* (the ticks again) */
}

static void sv_new_done(const char *text)
{
    char name[STATS_IMPORT_NAME];
    while (*text == ' ') text++;
    snprintf(name, sizeof(name), "%s", text);
    for (size_t l = strlen(name); l && name[l - 1] == ' ';) name[--l] = 0;
    if (!name[0]) {
        sudoku_ui_notice("A collection needs a name.", 2500);
        return;
    }
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT + s_nimports; c++) {
        if (!strcasecmp(coll_name(c), name)) {
            sudoku_ui_notice("That name is taken by a built-in or imported collection.", 3000);
            return;
        }
    }
    char msg[96], why[96];
    if (stats_db_saved_add(name, g_id, why, sizeof(why))) snprintf(msg, sizeof(msg), "Saved to %s (Puzzles > Saved).", name);
    else snprintf(msg, sizeof(msg), "%s", why);
    sv_changed();
    sudoku_ui_notice(msg, 2500);
}

static void sv_new_cb(lv_event_t *e)
{
    sv_close_cb(NULL);
    nd_open_ex("New collection", "", STATS_IMPORT_NAME - 1, "Save", sv_new_done);
}

static void sv_open(void)
{
    lv_obj_t *m = make_msgbox(LV_SYMBOL_SAVE "  Save to a collection", NULL);
    sv_box = m;
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_style_max_height(c, 400, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    if (!s_nsaved) make_wrap_text(c, NULL, C_MUTED, "No collections yet: make one.");
    for (int k = 0; k < s_nsaved; k++) {
        bool in = coll_find(PUZZLE_COLLECTION_COUNT + s_nimports + k, g_id) >= 0;
        lv_obj_t *b = make_btn(c, 0, 0, 0, 56, NULL, NULL, sv_toggle_cb, (void *)(intptr_t)k);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_style_bg_color(b, in ? C_ACCENT : C_CARD2, 0);
        lv_obj_t *l = make_label(b, &lv_font_montserrat_20, in ? lv_color_white() : C_TEXT);
        lv_label_set_text_fmt(l, "%s%s", in ? LV_SYMBOL_OK "  " : "", s_saved[k].name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *n = make_label(b, &lv_font_montserrat_16, in ? lv_color_white() : C_MUTED);
        lv_label_set_text_fmt(n, "%d", s_saved[k].count);
        lv_obj_align(n, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    if (s_nsaved < STATS_MAX_SAVED) {
        lv_obj_t *nb = make_btn(c, 0, 0, 0, 56, LV_SYMBOL_PLUS "  New collection...", &lv_font_montserrat_20,
                                sv_new_cb, NULL);
        lv_obj_set_width(nb, LV_PCT(100));
        lv_obj_set_style_bg_color(nb, C_CARD2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(nb, 0), C_ACCENT, 0);
    }
    msgbox_button(m, "Close", sv_close_cb, false);
}

static void save_cb(lv_event_t *e)
{
    if (g_practice >= 0 || !puzzle_is_generated(g_id) || g_id >= PUZZLE_GEN_RAM_BASE) return;
    if (!stats_db_ready()) {
        sudoku_ui_notice("Collections are kept in sudoku.db on a microSD card: there's none.", 3000);
        return;
    }
    imports_reload();                               /* (current: another player may have changed them) */
    sv_open();
}

static void star_cb(lv_event_t *e)
{
    if (g_practice >= 0 || g_id >= PUZZLE_GEN_RAM_BASE) return;
    g_starred = !g_starred;
    stats_db_set_bookmark(g_id, g_starred);
    update_header();
    sudoku_ui_notice(g_starred ? "Starred: it's under Puzzles > Starred." : "No longer starred.", 2000);
}

/* ------------------------------------------------------------- build */

static void build_game(void)
{
    scr_game = make_screen();

    /* top bar */
    make_btn(scr_game, 8, 6, 104, 48, LV_SYMBOL_LEFT "  List", &lv_font_montserrat_18, back_cb, NULL);
    g_title = make_label(scr_game, &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(g_title, 126, 6);
    /* the whole line to the right edge, one line ("Daily X-Sudoku Medium, 18 Nov" fits; longer ends in ...) */
    lv_obj_set_size(g_title, SCR_W - 8 - 126, lv_font_get_line_height(&lv_font_montserrat_22));
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_MODE_DOTS);
    /* second row: ? and Hint on the left, the pause button and timer on the right */
    lv_obj_t *qb = make_btn(scr_game, 8, TOOL_Y, 52, 48, SYMBOL_BULB, &font_icons_24, rules_cb,
                            NULL);                   /* the variant's rules */
    lv_obj_set_style_text_color(lv_obj_get_child(qb, 0), C_ACCENT, 0);
    g_bulb_btn = qb;
    g_best = make_label(scr_game, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(g_best, 126, 36);

    lv_obj_t *hb = make_btn(scr_game, 8 + 52 + 8, TOOL_Y, 120, 48, LV_SYMBOL_EYE_OPEN "  Hint", &lv_font_montserrat_18,
                            hint_cb, NULL);
    lv_obj_set_style_text_color(lv_obj_get_child(hb, 0), C_ACCENT, 0);
    g_hint_btn = hb;

    lv_obj_t *shb = make_btn(scr_game, 8 + 52 + 8 + 120 + 8, TOOL_Y, 130, 48, SYMBOL_QR "  Share", &font_icons_20,
                             share_cb, NULL);       /* the puzzle as a QR code */
    lv_obj_set_style_text_color(lv_obj_get_child(shb, 0), C_ACCENT, 0);
    g_star_btn = make_btn(scr_game, 8 + 52 + 8 + 120 + 8 + 130 + 8, TOOL_Y, 52, 48, SYMBOL_STAR, &font_icons_24,
                          star_cb, NULL);           /* bookmark it (Puzzles > Starred) */
    g_save_btn = make_btn(scr_game, 8 + 52 + 8 + 120 + 8 + 130 + 8 + 52 + 8, TOOL_Y, 52, 48, LV_SYMBOL_SAVE,
                          &lv_font_montserrat_22, save_cb, NULL);   /* into a collection (Puzzles > Saved) */
    lv_obj_t *tb = make_btn(scr_game, SCR_W - 8 - 200, TOOL_Y, 200, 48, NULL, NULL, pause_cb, NULL);
    lv_obj_set_style_bg_opa(tb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, LV_STATE_PRESSED);
    g_timer_btn = tb;
    g_timer_icon = make_label(tb, &lv_font_montserrat_20, C_MUTED);
    lv_obj_align(g_timer_icon, LV_ALIGN_LEFT_MID, 12, 0);
    g_timer = make_label(tb, &font_timer_36, C_TEXT);
    lv_obj_align(g_timer, LV_ALIGN_RIGHT_MID, -6, 0);

    /* board: thick-line background, the boxes in thin-line colour, the cells
     * (placed by game_layout() for the puzzle's variant) */
    lv_obj_t *grid = lv_obj_create(scr_game);
    g_grid = grid;
    lv_obj_remove_style_all(grid);
    lv_obj_add_event_cb(grid, board_regions_draw_cb, LV_EVENT_DRAW_POST_END, NULL);   /* Jigsaw borders */
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
    for (int i = 0; i < 18; i++) {
        g_clues[i] = make_label(scr_game, &lv_font_montserrat_20, C_ACCENT);
        lv_obj_set_style_text_align(g_clues[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_hidden(g_clues[i], true);
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_clickable(cell, true);
        lv_obj_set_scrollable(cell, false);
        lv_obj_add_event_cb(cell, cell_click_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cell, cell_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);   /* colours, cage sums */
        lv_obj_add_event_cb(cell, cell_draw_cb, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)i);
        lv_obj_t *l = make_label(cell, DIGIT_FONT, C_TEXT);
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
                               DIGITS[d], &lv_font_montserrat_28, NULL, NULL);
        lv_obj_add_event_cb(b, pad_click_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)d);
        lv_obj_add_event_cb(b, pad_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)d);   /* lock it */
        g_pad[d - 1] = b;
        g_pad_cnt[d - 1] = make_label(b, &lv_font_montserrat_12, C_MUTED);
        lv_obj_align(g_pad_cnt[d - 1], LV_ALIGN_TOP_RIGHT, -6, 4);
    }

    /* bottom bar: seven buttons across (the status line is in the top bar) */
    const int gap = 6, bw = (SCR_W - 16 - 6 * gap) / 7;
    int bx = 8;
    g_undo_btn = make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_LEFT "  Undo", &lv_font_montserrat_16, undo_cb,
                          NULL);
    bx += bw + gap;
    g_redo_btn = make_btn(scr_game, bx, BOT_Y, bw, BOT_H, "Redo  " LV_SYMBOL_RIGHT, &lv_font_montserrat_16, redo_cb,
                          NULL);
    bx += bw + gap;
    make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_BACKSPACE "  Erase", &lv_font_montserrat_16, erase_cb, NULL);
    bx += bw + gap;
    g_notes_btn = make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_EDIT "  Notes", &lv_font_montserrat_16, NULL, NULL);
    lv_obj_set_checkable(g_notes_btn, true);
    lv_obj_add_event_cb(g_notes_btn, notes_cb, LV_EVENT_VALUE_CHANGED, NULL);
    bx += bw + gap;
    g_auto_btn = make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_LIST "  Auto", &lv_font_montserrat_16,
                          auto_notes_cb, NULL);
    lv_obj_set_style_opa(g_auto_btn, LV_OPA_40, LV_STATE_DISABLED);   /* (the no-notes challenge) */
    lv_obj_set_style_opa(g_notes_btn, LV_OPA_40, LV_STATE_DISABLED);
    bx += bw + gap;
    make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_REFRESH "  Restart", &lv_font_montserrat_16, restart_cb, NULL);
    bx += bw + gap;
    lv_obj_t *fb = make_btn(scr_game, bx, BOT_Y, bw, BOT_H, LV_SYMBOL_OK "  Fill", &lv_font_montserrat_16,
                            fill_cb, NULL);
    g_fill_btn = fb;
    g_hint_cnt = make_label(fb, &lv_font_montserrat_12, C_ORANGE);     /* hints used */
    lv_obj_align(g_hint_cnt, LV_ALIGN_TOP_RIGHT, -5, 2);
    lv_obj_set_hidden(g_hint_cnt, true);
    /* status: top bar, right-aligned on the line under the title, up to the Hint button */
    g_status = make_label(scr_game, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(g_status, SCR_W - 8 - 126);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_status, 126, 36);

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
    lv_obj_set_size(card, 480, 362);
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
    g_win_h2h_row = lv_obj_create(card);            /* head-to-head: filled in a moment later */
    lv_obj_remove_style_all(g_win_h2h_row);
    lv_obj_set_size(g_win_h2h_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_win_h2h_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_win_h2h_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_win_h2h_row, 8, 0);
    lv_obj_set_clickable(g_win_h2h_row, false);
    lv_obj_align(g_win_h2h_row, LV_ALIGN_TOP_MID, 0, 218);
    g_win_h2h = make_label(g_win_h2h_row, &font_icons_20, C_ACCENT);   /* (with a leader's avatar: 26 + 8 less) */
    lv_obj_set_style_max_width(g_win_h2h, 440 - 34, 0);
    lv_obj_set_height(g_win_h2h, lv_font_get_line_height(&font_icons_20));   /* one line */
    lv_label_set_long_mode(g_win_h2h, LV_LABEL_LONG_MODE_DOTS);
    g_win_badge = make_label(card, &lv_font_montserrat_16, C_ACCENT);   /* the no-notes challenge */
    lv_label_set_text(g_win_badge, LV_SYMBOL_EDIT "  No-notes challenge: done without notes");
    lv_obj_align(g_win_badge, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_hidden(g_win_badge, true);
    lv_obj_t *shot = make_btn(card, 0, 0, 56, 48, SYMBOL_CAMERA, &font_icons_20, screenshot_cb, NULL);
    lv_obj_set_style_bg_color(shot, C_CARD2, 0);    /* a screenshot of the solved board */
    lv_obj_align(shot, LV_ALIGN_TOP_LEFT, 0, 0);
    g_win_share = make_btn(card, 0, 0, 56, 48, SYMBOL_QR, &font_icons_20, win_share_cb, NULL);   /* daily: share */
    lv_obj_set_style_bg_color(g_win_share, C_CARD2, 0);
    lv_obj_align(g_win_share, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_hidden(g_win_share, true);

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
static sudoku_geom_t *rp_geo_buf;                   /* PSRAM: a replayed Jigsaw's regions */
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
        lv_label_set_text_fmt(rp_speed_lbl, LV_SYMBOL_PLAY "  Speed %ux", RP_SPEEDS[rp_speed]);
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
    if (!rp_geo_buf) rp_geo_buf = heap_caps_malloc(sizeof(*rp_geo_buf), MALLOC_CAP_SPIRAM);
    rp_geo = puzzle_geom(id, rp_geo_buf, NULL);
    return true;
}

/* Builds the Replay screen (on the default display) for the prepared log. */
/* ------------------------------------------------------------- screenshot */
/*
 * The camera buttons (Solved dialog, Replay): the screen as a JPEG in screenshots/ on
 * the card. The frame is copied at once (the screen keeps going); a worker task on
 * core 0 has the hardware JPEG encoder compress it (by DMA, full colour resolution,
 * quality 90: about 100-200 KB instead of a 1.5 MB uncompressed PNG) and writes it.
 * The encoder gets RGB888, widened from RGB565 here: given RGB565 it widens by
 * shifting alone, and white came out (236, 245, 244), with a green cast.
 */
#define SS_QUALITY 90
#define SS_OUT_MAX (1024 * 1024)
static uint16_t *ss_frame;                          /* 64-byte aligned: the encoder reads it by DMA */
static volatile int ss_state;                       /* 0 idle, 1 writing, 2 done, 3 failed */
static char ss_path[64];
static lv_timer_t *ss_timer;

static void ss_task(void *arg)
{
    bool ok = false;
    jpeg_encoder_handle_t enc = NULL;
    jpeg_encode_engine_cfg_t ecfg = {.timeout_ms = 1000};
    jpeg_encode_memory_alloc_cfg_t mcfg = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    size_t out_cap = 0;
    uint8_t *out = jpeg_alloc_encoder_mem(SS_OUT_MAX, &mcfg, &out_cap);
    uint8_t *rgb = heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_W * 3, MALLOC_CAP_SPIRAM);
    esp_err_t err = out && rgb ? jpeg_new_encoder_engine(&ecfg, &enc) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        for (size_t i = 0; i < (size_t)SCR_W * SCR_W; i++) {   /* RGB565 -> bytes B, G, R */
            uint16_t px = ss_frame[i];
            int r = px >> 11, g = (px >> 5) & 0x3F, b = px & 0x1F;
            rgb[i * 3] = (uint8_t)(b << 3 | b >> 2);
            rgb[i * 3 + 1] = (uint8_t)(g << 2 | g >> 4);
            rgb[i * 3 + 2] = (uint8_t)(r << 3 | r >> 2);
        }
        jpeg_encode_cfg_t cfg = {
            .width = SCR_W,
            .height = SCR_W,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV444,   /* sharp coloured text and lines */
            .image_quality = SS_QUALITY,
        };
        uint32_t len = 0;
        err = jpeg_encoder_process(enc, &cfg, rgb, SCR_W * SCR_W * 3, out, out_cap, &len);
        if (err == ESP_OK && len > 0) {
            FILE *f = fopen(ss_path, "wb");
            if (f) {
                ok = fwrite(out, 1, len, f) == len;
                ok = fclose(f) == 0 && ok;
                if (!ok) unlink(ss_path);
            }
        }
    }
    if (err != ESP_OK) printf("screenshot: JPEG encoder: %s\n", esp_err_to_name(err));
    if (enc) jpeg_del_encoder_engine(enc);
    if (out) heap_caps_free(out);
    heap_caps_free(rgb);
    ss_state = ok ? 2 : 3;
    vTaskDelete(NULL);
}

static void ss_poll_cb(lv_timer_t *t)
{
    if (ss_state < 2) return;
    lv_timer_delete(ss_timer);
    ss_timer = NULL;
    char msg[120];
    if (ss_state == 2) snprintf(msg, sizeof(msg), "Saved %s. Copy it off with Settings > USB drive.",
                                ss_path + strlen(BSP_SD_MOUNT_POINT) + 1);
    else snprintf(msg, sizeof(msg), "Couldn't save the screenshot.");
    printf("screenshot: %s\n", msg);
    ss_state = 0;
    sudoku_ui_notice(msg, 4000);
}

static void screenshot_cb(lv_event_t *e)
{
    if (ss_state) return;                           /* (one at a time) */
    if (!stats_db_ready()) {
        sudoku_ui_notice("Screenshots go on the microSD card: there's none to write to.", 3000);
        return;
    }
    if (!ss_frame) ss_frame = heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_W * 2, MALLOC_CAP_SPIRAM);
    lv_refr_now(NULL);
    const uint16_t *fb = bsp_display_frame();
    if (!ss_frame || !fb) return;
    memcpy(ss_frame, fb, (size_t)SCR_W * SCR_W * 2);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    mkdir(BSP_SD_MOUNT_POINT "/screenshots", 0775);
    if (now > 1700000000) strftime(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/%Y%m%d-%H%M%S.jpg", &tm);
    else snprintf(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/shot-%lu.jpg",
                  (unsigned long)(esp_timer_get_time() / 1000000));
    ss_state = 1;
    if (xTaskCreatePinnedToCore(ss_task, "screenshot", 4096, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        ss_state = 0;
        return;
    }
    ss_timer = lv_timer_create(ss_poll_cb, 200, NULL);
}

/* ------------------------------------------------------------- mistake insights */
static lv_obj_t *ins_box, *ins_body;
static lv_timer_t *ins_timer;

static void ins_close_cb(lv_event_t *e)
{
    if (ins_timer) lv_timer_delete(ins_timer);
    ins_timer = NULL;
    if (ins_box) lv_msgbox_close_async(ins_box);
    ins_box = ins_body = NULL;
}

static lv_obj_t *ins_heading(const char *text)
{
    lv_obj_t *l = make_label(ins_body, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text(l, text);
    lv_obj_set_style_margin_top(l, 14, 0);
    return l;
}

static void ins_show(const stats_insights_t *in)
{
    lv_obj_clean(ins_body);
    char buf[160];
    if (in->solves == 0 || in->placed == 0) {
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED,
                       "Nothing to look at yet: insights come from the move logs of solved 9x9 puzzles.");
        return;
    }
    snprintf(buf, sizeof(buf), "From your last %d solve%s: %lu of the %lu digits you placed by hand were wrong "
             "(%lu.%lu %%).", in->solves, in->solves == 1 ? "" : "s", (unsigned long)in->wrong,
             (unsigned long)in->placed, (unsigned long)(in->wrong * 100 / in->placed),
             (unsigned long)(in->wrong * 1000 / in->placed % 10));
    make_wrap_text(ins_body, &lv_font_montserrat_18, C_TEXT, buf);
    if (!in->wrong) {
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_GREEN, "Not one wrong digit. Nothing to work on here!");
        return;
    }

    /* which digits: a bar each */
    ins_heading("Digits you put in the wrong place");
    int maxd = 1;
    for (int d = 1; d <= 9; d++) if (in->by_digit[d] > maxd) maxd = in->by_digit[d];
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *row = lv_obj_create(ins_body);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 26);
        lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(n, "%d", d);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, in->by_digit[d] ? 4 + 420 * in->by_digit[d] / maxd : 2, 16);
        lv_obj_set_style_bg_color(bar, in->by_digit[d] == maxd ? C_RED : C_ORANGE, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_t *c = make_label(row, &lv_font_montserrat_16, C_MUTED);
        lv_label_set_text_fmt(c, "%u", (unsigned)in->by_digit[d]);
        lv_obj_align(c, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    /* where: the nine boxes, the busier the redder */
    ins_heading("Where on the board");
    int maxb = 1;
    for (int b = 0; b < 9; b++) if (in->by_box[b] > maxb) maxb = in->by_box[b];
    lv_obj_t *g = lv_obj_create(ins_body);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, 3 * 76 + 2 * 6, 3 * 56 + 2 * 6);
    for (int b = 0; b < 9; b++) {
        lv_obj_t *c = lv_obj_create(g);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 76, 56);
        lv_obj_set_pos(c, (b % 3) * 82, (b / 3) * 62);
        lv_obj_set_style_radius(c, 6, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        int pct = in->by_box[b] * 100 / maxb;
        lv_obj_set_style_bg_color(c, lv_color_hex(mix_hex(ui_pal->red, ui_pal->card2, pct * 85 / 100)), 0);
        lv_obj_t *l = make_label(c, &lv_font_montserrat_18, pct > 55 ? lv_color_white() : C_TEXT);
        lv_label_set_text_fmt(l, "%u", (unsigned)in->by_box[b]);
        lv_obj_center(l);
    }

    /* mix-ups: the most frequent digit-for-digit swaps */
    ins_heading("Mix-ups");
    uint16_t done[10][10] = {{0}};
    for (int k = 0; k < 3; k++) {
        int bp = 0, bb = 0, bn = 0;
        for (int p2 = 1; p2 <= 9; p2++) {
            for (int q = 1; q <= 9; q++) {
                if (!done[p2][q] && in->pair[p2][q] > bn) { bn = in->pair[p2][q]; bp = p2; bb = q; }
            }
        }
        if (!bn) break;
        done[bp][bb] = 1;
        snprintf(buf, sizeof(buf), "A %d where a %d belongs: %d time%s", bp, bb, bn, bn == 1 ? "" : "s");
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, buf);
    }
}

static void ins_poll_cb(lv_timer_t *t)
{
    static stats_insights_t in;                     /* (~250 bytes: off the LVGL stack) */
    if (!stats_db_take_insights(&in)) return;
    lv_timer_delete(ins_timer);
    ins_timer = NULL;
    if (ins_body) ins_show(&in);
}

static void insights_cb(lv_event_t *e)
{
    if (ins_box) return;
    lv_obj_t *m = make_msgbox(SYMBOL_BULB "  Mistake insights", NULL);
    ins_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    ins_body = lv_msgbox_get_content(m);
    lv_obj_set_height(ins_body, 520);
    lv_obj_set_flex_flow(ins_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ins_body, 4, 0);
    lv_obj_set_scroll_dir(ins_body, LV_DIR_VER);
    make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, "Reading your move logs...");
    msgbox_button(m, "Close", ins_close_cb, false);
    if (!stats_db_ready()) {
        lv_obj_clean(ins_body);
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, "This needs sudoku.db on a microSD card.");
        return;
    }
    stats_db_query_insights();
    ins_timer = lv_timer_create(ins_poll_cb, 100, NULL);
}

static lv_obj_t *rp_shot;                           /* the Replay screen's camera (hidden in exported videos) */

static lv_obj_t *rp_build(uint16_t id, const char *sub, const char *player)
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
    const stats_player_t *who = player_find(0, player);
    if (who && who->avatar && who->avatar <= AVATAR_COUNT) {   /* whose game: their avatar first */
        lv_obj_set_pos(player_badge(scr, who, -1, 22, 8), 126, 32);
        lv_obj_set_pos(rp_sub, 126 + 22 + 6, 36);
    } else {
        lv_obj_set_pos(rp_sub, 126, 36);
    }

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_add_event_cb(grid, board_regions_draw_cb, LV_EVENT_DRAW_POST_END, NULL);   /* Jigsaw borders */
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
        rp_lbl[i] = make_label(cell, DIGIT_FONT_N(rp_geo->n), C_TEXT);
        lv_obj_center(rp_lbl[i]);
    }
    lv_obj_t *clues[18];
    for (int i = 0; i < 18; i++) {
        clues[i] = make_label(scr, &lv_font_montserrat_20, C_ACCENT);
        lv_obj_set_style_text_align(clues[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    board_layout(rp_geo, grid, boxes, rp_cells, clues, GRID_X);

    /* second row (as the game's): where the replay is on the left, the speed on the right */
    rp_pos = make_label(scr, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(rp_pos, SCR_W - 8 - 200 - 8 - 16 - 64);
    rp_shot = make_btn(scr, SCR_W - 8 - 200 - 8 - 56, TOOL_Y, 56, 48, SYMBOL_CAMERA, &font_icons_20,
                       screenshot_cb, NULL);       /* a screenshot of this moment */
    lv_label_set_long_mode(rp_pos, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(rp_pos, 16, TOOL_Y + (48 - 24) / 2);
    lv_obj_t *sp = make_btn(scr, SCR_W - 8 - 200, TOOL_Y, 200, 48, NULL, &lv_font_montserrat_20, rp_btn_cb,
                            (void *)(intptr_t)5);
    rp_speed_lbl = make_label(sp, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_center(rp_speed_lbl);
    lv_label_set_text_fmt(rp_speed_lbl, LV_SYMBOL_PLAY "  Speed %ux", RP_SPEEDS[rp_speed]);

    /* controls down the right, as tall as the board: start, back, play, forward, end */
    static const char *const SYM[] = {LV_SYMBOL_PREV, LV_SYMBOL_LEFT, LV_SYMBOL_PLAY, LV_SYMBOL_RIGHT,
                                      LV_SYMBOL_NEXT};
    const int gap = 8, bh = (GRID_SIZE - 4 * gap) / 5;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = make_btn(scr, PAD_X, GRID_Y + i * (bh + gap), PAD_W, bh, SYM[i],
                               &lv_font_montserrat_28, rp_btn_cb, (void *)(intptr_t)i);
        if (i == 2) {
            lv_obj_set_style_bg_color(b, C_ACCENT, 0);
            rp_play_lbl = lv_obj_get_child(b, 0);
            lv_obj_set_style_text_color(rp_play_lbl, lv_color_white(), 0);
        }
    }

    /* bottom: the slider, the full width */
    rp_slider = lv_slider_create(scr);
    lv_obj_set_size(rp_slider, SCR_W - 64, 14);
    lv_obj_set_pos(rp_slider, 32, BOT_Y + (BOT_H - 14) / 2);
    lv_slider_set_range(rp_slider, 0, rp_n > 0 ? rp_n : 1);
    lv_obj_set_style_bg_color(rp_slider, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_ext_click_area(rp_slider, 16);
    lv_obj_add_event_cb(rp_slider, rp_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return scr;
}

/* Opens the Replay screen for a log of puzzle `id`; sub = the line under the title. */
static void open_replay(uint16_t id, const game_move_t *moves, int n, const char *sub, const char *player)
{
    if (!rp_prepare(id, moves, n)) return;
    if (scr_replay) rp_close();
    rp_return = lv_screen_active();
    scr_replay = rp_build(id, sub, player);
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
    open_replay(g_id, g_moves, g_moves ? g_nmoves : 0, sub, store_player_name());
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
    open_replay(rp_loaded->puzzle, rp_loaded->moves, rp_loaded->n, sub, rp_loaded->player);
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
static uint8_t *rx_fb;              /* RGB888 (see video_enc.c: RGB565 gave the video a green cast) */
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

    rx_fb = heap_caps_aligned_alloc(64, SCR_W * SCR_W * 3, MALLOC_CAP_SPIRAM);
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
    lv_display_set_color_format(rx_disp, LV_COLOR_FORMAT_RGB888);   /* (the encoder input: video_enc.c) */
    lv_display_set_buffers(rx_disp, rx_fb, NULL, SCR_W * SCR_W * 3, LV_DISPLAY_RENDER_MODE_FULL);
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
    lv_obj_t *scr = rp_build(r->puzzle, sub, r->player);
    lv_obj_set_hidden(rp_shot, true);               /* (not in the video) */
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
static sudoku_geom_t *gen_board;                    /* the puzzle's board (Jigsaw regions, Killer cages), PSRAM */
static char *gen_cages;                             /* STATS_CAGES_LEN, PSRAM */
static char gen_marks[82];
static char *gen_thermos;                           /* STATS_THERMOS_LEN, PSRAM */
static uint32_t gen_seed;                           /* 0 = random */
static char gen_day[12];                            /* making a daily puzzle: its date */

static void gen_task(void *arg)
{
    gen_level_t got = (gen_level_t)gen_level_req;
    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    sudoku_generate(gen_var_req, (gen_level_t)gen_level_req, gen_seed ? gen_seed : esp_random(), gen_givens,
                    gen_board, &got);
    gen_cages[0] = 0;
    if (gen_var_req == VAR_KILLER) sudoku_cages_text(gen_board, gen_cages, STATS_CAGES_LEN);
    gen_marks[0] = 0;
    if (gen_var_req == VAR_EVENODD || gen_var_req == VAR_CONSEC) sudoku_marks_text(gen_board, gen_marks, 82);
    if (gen_var_req == VAR_SANDWICH) sudoku_sandwich_text(gen_board, gen_marks, 82);
    gen_thermos[0] = 0;
    if (gen_var_req == VAR_THERMO) sudoku_thermos_text(gen_board, gen_thermos, STATS_THERMOS_LEN);
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
    const uint8_t *regions = gen_var_req == VAR_JIGSAW ? gen_board->region : NULL;
    const char *cages = gen_var_req == VAR_KILLER ? gen_cages : NULL;
    const char *marks = gen_marks[0] ? gen_marks : NULL;
    const char *thermos = gen_thermos[0] ? gen_thermos : NULL;
    int gid = stats_db_ready() ? stats_db_gen_add(gen_var_req, gen_level_got, gen_givens, regions, cages, marks,
                                                  thermos, gen_day[0] ? gen_day : NULL)
                               : -1;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = s_gen_ram_next++;                       /* no card: until power-off */
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
    }
    gen_cache_put(id, gen_var_req, gen_level_got, gen_givens, regions, cages, marks, thermos, gen_day);
    if (gen_level_got != gen_level_req && !gen_day[0]) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Couldn't make a %s %s quickly; this one is %s.", GEN_LEVEL_NAMES[gen_level_req],
                 gen_var_req == VAR_CLASSIC ? "one" : VARIANT_NAMES[gen_var_req], GEN_LEVEL_NAMES[gen_level_got]);
        sudoku_ui_notice(msg, 4000);
    }
    open_puzzle(id);
}

/* seed 0 = a random puzzle; a daily one (always classic) has its date as the seed and day set */
static lv_timer_t *learn_timer;                     /* (Learn: its variant lessons use the generator too) */

static void gen_start_ex(int variant, int level, uint32_t seed, const char *day)
{
    if (gen_timer || learn_timer) return;            /* one at a time */
    if (!gen_board && !(gen_board = heap_caps_malloc(sizeof(*gen_board), MALLOC_CAP_SPIRAM))) return;
    if (!gen_cages && !(gen_cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) return;
    if (!gen_thermos && !(gen_thermos = heap_caps_malloc(STATS_THERMOS_LEN, MALLOC_CAP_SPIRAM))) return;
    gen_var_req = variant >= 0 && variant < VAR_COUNT ? variant : VAR_CLASSIC;
    gen_level_req = level;
    gen_seed = seed;
    snprintf(gen_day, sizeof(gen_day), "%s", day ? day : "");
    gen_done = false;
    char text[80];
    if (day) {
        char kind[32];
        daily_kind_text(gen_var_req, level, kind, sizeof(kind));
        snprintf(text, sizeof(text), "Making today's %s puzzle...", kind);
    }
    else if (gen_var_req == VAR_CLASSIC) snprintf(text, sizeof(text), "Making a new %s puzzle...", GEN_LEVEL_NAMES[level]);
    else snprintf(text, sizeof(text), "Making a new %s %s puzzle...", GEN_LEVEL_NAMES[level], VARIANT_NAMES[gen_var_req]);
    gen_box = make_msgbox(LV_SYMBOL_REFRESH "  New puzzle", text);
    if (xTaskCreatePinnedToCore(gen_task, "gen", 16384, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
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
 * with a variant and level by weekday (DAILY_*): Mon Classic Easy, Tue Mini
 * Medium, Wed X-Sudoku Medium, Thu Hyper Hard, Fri Killer Medium, Sat Jigsaw
 * Hard, Sun Classic Expert. Stored like any generated puzzle, with its date
 * (generated.day): a day's puzzle, once made, stays what it is. The streak
 * counts days in a row with the daily solved (stats_db).
 */
static const uint8_t DAILY_LEVEL[7] = {GEN_EXPERT, GEN_EASY, GEN_MEDIUM, GEN_MEDIUM, GEN_HARD, GEN_MEDIUM, GEN_HARD};
static const uint8_t DAILY_VARIANT[7] = {VAR_CLASSIC, VAR_CLASSIC, VAR_MINI, VAR_X, VAR_HYPER, VAR_KILLER, VAR_JIGSAW};

/* today's date, level and variant; false while the clock isn't set */
static bool daily_today_v(char day[12], int *level, int *variant)
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) return false;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    *level = DAILY_LEVEL[tm.tm_wday];
    if (variant) *variant = DAILY_VARIANT[tm.tm_wday];
    return true;
}

static bool daily_today(char day[12], int *level, int *wday)
{
    (void)wday;
    return daily_today_v(day, level, NULL);
}

/* "Hard", "X-Sudoku Medium": a daily puzzle's level, with its variant unless Classic */
static void daily_kind_text(int variant, int level, char *buf, size_t n)
{
    const char *lv = level >= 0 && level < GEN_LEVELS ? GEN_LEVEL_NAMES[level] : "";
    if (variant > VAR_CLASSIC && variant < VAR_COUNT) snprintf(buf, n, "%s %s", VARIANT_NAMES[variant], lv);
    else snprintf(buf, n, "%s", lv);
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
    char day[12], kind[32];
    int level, variant, id, streak;
    bool solved;
    if (!daily_today_v(day, &level, &variant)) {
        snprintf(buf, n, "Needs the date: connect Wi-Fi in Settings");
        return;
    }
    if (!stats_db_daily_status(day, &id, &solved, &streak)) {
        daily_kind_text(variant, level, kind, sizeof(kind));
        snprintf(buf, n, "Today: %s", kind);
        return;
    }
    if (id) gen_lookup((uint16_t)(PUZZLE_GEN_BASE + id), NULL, &variant, &level);   /* as it was made */
    daily_kind_text(variant, level, kind, sizeof(kind));
    if (solved) snprintf(buf, n, LV_SYMBOL_OK "  Solved today  -  streak %d", streak);
    else if (streak) snprintf(buf, n, "Today: %s  -  streak %d", kind, streak);
    else snprintf(buf, n, "Today: %s", kind);
}

static void daily_cb(lv_event_t *e)
{
    char day[12];
    int level, variant, id = 0, streak = 0;
    bool solved = false;
    if (!daily_today_v(day, &level, &variant)) {
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
    gen_start_ex(variant, level, daily_seed(day), day);
}

static lv_obj_t *gen_pick;                          /* the level dialog */
static lv_obj_t *gen_pick_desc;                     /* its line about the variant chosen */
static int gen_pick_var = VAR_CLASSIC;              /* variant chosen there (kept until power-off) */

/* the variants in the New puzzle dialog, in button order */
static const uint8_t GEN_VAR_ORDER[VAR_COUNT] = {VAR_CLASSIC, VAR_X, VAR_HYPER, VAR_JIGSAW, VAR_KILLER, VAR_MINI,
                                                 VAR_KNIGHT, VAR_EVENODD, VAR_CONSEC, VAR_THERMO, VAR_SANDWICH,
                                                 VAR_KIDS};
static const char *const GEN_VAR_DESC[VAR_COUNT] = {
    [VAR_CLASSIC] = "9x9: rows, columns and 3x3 boxes hold 1-9 once each.",
    [VAR_X] = "9x9, and the two tinted diagonals also hold 1-9 once each.",
    [VAR_MINI] = "6x6 with 2x3 boxes: rows, columns and boxes hold 1-6 once each.",
    [VAR_HYPER] = "9x9, and the four tinted 3x3 windows also hold 1-9 once each.",
    [VAR_JIGSAW] = "9x9 with nine irregular regions (thick borders) instead of boxes; each holds 1-9 once.",
    [VAR_KILLER] = "9x9 with cages (dashed): a cage's digits add up to its sum, none repeated. Few or no givens.",
    [VAR_KNIGHT] = "9x9, and two cells a chess knight's move apart never hold the same digit.",
    [VAR_EVENODD] = "9x9: shaded cells hold an even digit, cells with a ring an odd one.",
    [VAR_CONSEC] = "9x9: a bar joins every two side-by-side cells whose digits are 1 apart; no bar, they aren't.",
    [VAR_THERMO] = "9x9 with thermometers: along each one, the digits rise from the round bulb.",
    [VAR_SANDWICH] = "9x9: the number by each row and column is the sum of the digits between its 1 and its 9.",
    [VAR_KIDS] = "4x4 for kids, with big digits: rows, columns and 2x2 boxes hold 1-4 once each. Easy or Medium.",
};
static lv_obj_t *gen_lvl_btn[GEN_LEVELS];          /* the level buttons (Kids 4x4: Hard and Expert off) */

/* Hard and Expert need techniques a 4x4 board never calls for: off for Kids 4x4 */
static void gen_levels_update(void)
{
    for (int lv = 0; lv < GEN_LEVELS; lv++) {
        if (gen_lvl_btn[lv]) lv_obj_set_state(gen_lvl_btn[lv], LV_STATE_DISABLED, gen_pick_var == VAR_KIDS && lv >= GEN_HARD);
    }
}

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
    gen_pick_var = GEN_VAR_ORDER[sel];
    lv_label_set_text(gen_pick_desc, GEN_VAR_DESC[gen_pick_var]);
    gen_levels_update();
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
    make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT,
                   "A new puzzle made on the board, with exactly one solution. Easy and Medium need only "
                   "singles; Hard needs pairs or locked candidates; Expert needs more than that.");

    /* the variant: Classic, X-Sudoku or Mini 6x6 (one of them checked) */
    static const char *const VAR_MAP[] = {"Classic", "X-Sudoku", "Hyper", "\n", "Jigsaw", "Killer", "Mini 6x6", "\n",
                                          "Anti-Knight", "Even-Odd", "Consecutive", "\n", "Thermo", "Sandwich", "Kids 4x4", ""};
    lv_obj_t *vm = lv_buttonmatrix_create(lv_msgbox_get_content(m));
    lv_buttonmatrix_set_map(vm, VAR_MAP);
    lv_buttonmatrix_set_button_ctrl_all(vm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(vm, true);
    for (uint32_t i = 0; i < VAR_COUNT; i++) {
        if (GEN_VAR_ORDER[i] == gen_pick_var) lv_buttonmatrix_set_button_ctrl(vm, i, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    lv_obj_set_size(vm, LV_PCT(100), 226);          /* rows of three */
    lv_obj_set_style_pad_row(vm, 8, 0);
    lv_obj_set_style_pad_all(vm, 0, 0);
    lv_obj_set_style_pad_column(vm, 8, 0);
    lv_obj_set_style_margin_top(vm, 14, 0);
    lv_obj_set_style_border_width(vm, 0, 0);
    lv_obj_set_style_bg_opa(vm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(vm, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(vm, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_text_color(vm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(vm, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(vm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(vm, gen_var_cb, LV_EVENT_VALUE_CHANGED, NULL);
    gen_pick_desc = make_wrap_text(lv_msgbox_get_content(m), &lv_font_montserrat_16, C_MUTED,
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
        /* a star per step: Easy one, Expert four */
        char label[48] = "";
        for (int k = 0; k <= lv; k++) strcat(label, SYMBOL_STAR);
        strcat(label, "  ");
        strcat(label, GEN_LEVEL_NAMES[lv]);
        lv_obj_t *b = make_btn(grid, 0, 0, 0, 60, label, &font_icons_24, gen_level_cb, (void *)(intptr_t)lv);
        lv_obj_set_width(b, LV_PCT(48));
        lv_obj_set_style_bg_color(b, C_ACCENT, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_white(), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_STATE_DISABLED);
        gen_lvl_btn[lv] = b;
    }
    gen_levels_update();

    msgbox_button(m, "Cancel", gen_cancel_cb, false);
    lv_obj_t *backdrop = lv_obj_get_parent(m);         /* the modal's dimmed layer */
    if (backdrop && backdrop != lv_layer_top()) lv_obj_add_event_cb(backdrop, gen_backdrop_cb, LV_EVENT_CLICKED, NULL);
}

/* ------------------------------------------------------------- learn (practice a technique) */
/*
 * Learn (Puzzles screen): pick a technique; a worker task walks built-in
 * puzzles forward with the hint engine (sudoku_next_step) until the next
 * step needs that technique, and the game opens at that point as a practice
 * game, with the hint card on the technique. Practice games aren't saved,
 * recorded or counted.
 */
static const char *const LEARN_DESC[LEARN_COUNT] = {
    "A digit has only one place left in a row, column or box.",
    "A cell has only one digit left that fits.",
    "In a box, a digit's places all lie on one line: the rest of that line can't have it.",
    "On a line, a digit's places all lie in one box: the rest of that box can't have it.",
    "Two cells of a unit can only be the same two digits: no other cell there can be either.",
    "Two digits fit only the same two cells of a unit: those cells can't be anything else.",
    "Three cells of a unit can only be the same three digits between them: no other cell there can.",
    "In two rows a digit fits only the same two columns: it can't be elsewhere in those columns.",
    "The same with three rows and three columns (or three columns and three rows).",
    "A cell that is x or y sees an x-or-z and a y-or-z cell: one of those is z, so what sees both isn't.",
    "A cage's sum allows only some sets of digits: with what's around it, its cells narrow down.",
    "Digits rise from the bulb: each cell lies between what the cells before and after it can be.",
    "The sum between a row's 1 and 9 leaves the 1 and the 9 only a few places in that row or column.",
    "A bar: the two cells differ by 1. No bar: they don't. Both narrow down a cell's digits.",
};

/* the variant lessons: a new puzzle of that variant (made on the spot, kept in RAM only) */
static int learn_variant(int target)
{
    switch (target) {
    case LEARN_CAGE:     return VAR_KILLER;
    case LEARN_THERMO:   return VAR_THERMO;
    case LEARN_SANDWICH: return VAR_SANDWICH;
    case LEARN_CONSEC:   return VAR_CONSEC;
    }
    return -1;
}

static int learn_variant_tech(int target)
{
    switch (target) {
    case LEARN_CAGE:     return TECH_CAGE;
    case LEARN_THERMO:   return TECH_THERMO;
    case LEARN_SANDWICH: return TECH_SANDWICH;
    case LEARN_CONSEC:   return TECH_CONSEC;
    }
    return TECH_NONE;
}

/* the techniques that sudoku_step_with() finds a lesson position for (the newer, rarer ones:
 * the hint engine seldom reaches them first, so any position where they open a single will do) */
static int learn_tech(int target)
{
    switch (target) {
    case LEARN_NAKED_TRIPLE: return TECH_NAKED_TRIPLE;
    case LEARN_X_WING:       return TECH_X_WING;
    case LEARN_SWORDFISH:    return TECH_SWORDFISH;
    case LEARN_XY_WING:      return TECH_XY_WING;
    }
    return TECH_NONE;
}

static lv_obj_t *learn_box, *learn_wait;
static lv_timer_t *learn_timer;
static volatile int learn_state;                    /* 0 searching, 1 found, 2 nothing found */
static int learn_target;
static uint16_t learn_id;
static uint8_t learn_vals[81];
static sudoku_step_t learn_step;

/* does this step need the technique (for a naked single: a cell with one candidate is enough) */
static bool learn_match(int target, const sudoku_step_t *st)
{
    switch (target) {
    case LEARN_HIDDEN: return st->kind == STEP_HIDDEN_SINGLE && !st->tech.kind;
    case LEARN_NAKED: return st->kind == STEP_NAKED_SINGLE && !st->tech.kind;
    case LEARN_POINTING: return st->tech.kind == TECH_POINTING;
    case LEARN_CLAIMING: return st->tech.kind == TECH_CLAIMING;
    case LEARN_NAKED_PAIR: return st->tech.kind == TECH_NAKED_PAIR;
    case LEARN_HIDDEN_PAIR: return st->tech.kind == TECH_HIDDEN_PAIR;
    }
    return false;
}

/* a naked single on the board: a cell only one digit fits (singles are found
 * as hidden ones first, so the hint engine seldom names this one itself) */
static bool learn_find_naked(const uint8_t vals[81], const uint8_t sol[81], sudoku_step_t *st)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    for (int c = 0; c < 81; c++) {
        if (vals[c]) continue;
        uint16_t m = 0x3FE;
        for (int p = 0; p < G->npeers[c]; p++) m &= (uint16_t)~(1u << vals[G->peers[c][p]]);
        if (__builtin_popcount(m) == 1 && __builtin_ctz(m) == sol[c]) {
            memset(st, 0, sizeof(*st));
            st->kind = STEP_NAKED_SINGLE;
            st->cell = c;
            st->digit = sol[c];
            st->unit = -1;
            return true;
        }
    }
    return false;
}

/* a variant lesson's puzzle, for learn_poll_cb to put in the RAM cache */
static uint8_t learn_givens[81];
static sudoku_geom_t *learn_board;                  /* PSRAM */
static char *learn_cages, *learn_thermos;           /* PSRAM */
static char learn_marks[82];

static int learn_variant_find(int target)
{
    static uint8_t vals[81], sol[81];
    const int var = learn_variant(target), tech = learn_variant_tech(target);
    for (int tries = 0; tries < 6; tries++) {      /* most puzzles have one early; a few seconds each at most */
        gen_level_t got;
        if (!sudoku_generate(var, GEN_MEDIUM, esp_random(), learn_givens, learn_board, &got)) continue;
        if (!sudoku_solve(learn_board, learn_givens, sol)) continue;
        memcpy(vals, learn_givens, 81);
        for (int step = 0; step < 81; step++) {
            sudoku_step_t st;
            if (!sudoku_next_step(learn_board, vals, sol, &st) || st.cell < 0) break;
            if (st.tech.kind == tech) {
                memcpy(learn_vals, vals, 81);
                learn_step = st;
                learn_cages[0] = learn_marks[0] = learn_thermos[0] = 0;
                if (var == VAR_KILLER) sudoku_cages_text(learn_board, learn_cages, STATS_CAGES_LEN);
                if (var == VAR_CONSEC) sudoku_marks_text(learn_board, learn_marks, sizeof(learn_marks));
                if (var == VAR_SANDWICH) sudoku_sandwich_text(learn_board, learn_marks, sizeof(learn_marks));
                if (var == VAR_THERMO) sudoku_thermos_text(learn_board, learn_thermos, STATS_THERMOS_LEN);
                return 1;
            }
            vals[st.cell] = (uint8_t)st.digit;      /* on to the next step */
        }
    }
    return 2;
}

static void learn_task(void *arg)
{
    static uint8_t vals[81], sol[81];
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    const int target = learn_target;
    if (learn_variant(target) >= 0) {
        learn_state = learn_variant_find(target);
        vTaskDelete(NULL);
        return;
    }
    /* the harder collections first for the harder techniques */
    uint32_t r = esp_random();
    int found = 0;
    for (int tries = 0; tries < 2000 && !found; tries++) {     /* (pairs are the rarest: ~200 puzzles) */
        int c = (int)((r + (uint32_t)tries * 7) % PUZZLE_COLLECTION_COUNT);
        if (target >= LEARN_POINTING && strstr(puzzle_collections[c].name, "Easy")) continue;
        uint16_t id = (uint16_t)(puzzle_collections[c].first + (esp_random() % puzzle_collections[c].count));
        puzzle_load(id, vals);
        if (!sudoku_solve(G, vals, sol)) continue;
        sudoku_step_t st, w;
        const int tech = learn_tech(target);
        for (int step = 0; step < 81 && !found; step++) {
            if (target == LEARN_NAKED && step >= 8 && learn_find_naked(vals, sol, &st)) found = 1;
            else if (!sudoku_next_step(G, vals, sol, &st) || st.cell < 0) break;
            else if (tech && (st.tech.kind || st.kind == STEP_NONE) && sudoku_step_with(G, vals, sol, tech, &w)) {
                st = w;                             /* stuck on singles, and this technique opens one */
                found = 1;
            } else if (!tech && learn_match(target, &st)) found = 1;
            if (found) {
                learn_id = id;
                memcpy(learn_vals, vals, 81);
                learn_step = st;
                break;
            }
            vals[st.cell] = (uint8_t)st.digit;      /* on to the next step */
        }
    }
    learn_state = found ? 1 : 2;
    vTaskDelete(NULL);
}

static void learn_poll_cb(lv_timer_t *t)
{
    if (learn_state == 0) return;
    lv_timer_delete(learn_timer);
    learn_timer = NULL;
    if (learn_wait) lv_msgbox_close(learn_wait);
    learn_wait = NULL;
    if (learn_state != 1) {
        sudoku_ui_notice("Couldn't find an example just now: try again.", 3000);
        return;
    }
    /* the puzzle, at that point, as a practice game */
    int var = learn_variant(learn_target);
    if (var >= 0) {                                 /* made just now: kept in RAM only, as without a card */
        learn_id = s_gen_ram_next++;
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
        gen_cache_put(learn_id, var, GEN_MEDIUM, learn_givens, NULL, var == VAR_KILLER ? learn_cages : NULL,
                      learn_marks[0] ? learn_marks : NULL, learn_thermos[0] ? learn_thermos : NULL, NULL);
    }
    start_game(learn_id, false);
    g_practice = learn_target;
    g_flags = 0;                                    /* (practice: never the no-notes challenge) */
    challenge_ui();
    uint8_t given[81];
    puzzle_get(learn_id, given);
    for (int i = 0; i < 81; i++) {
        if (!given[i]) g_b.val[i] = learn_vals[i];
    }
    g_undo_len = g_undo_head = 0;
    g_nmoves = 0;
    compute_conflicts();
    update_header();
    h_info = learn_step;                            /* the hint card on the technique */
    h_step = 1;
    hint_render();
}

static void learn_pick_cb(lv_event_t *e)
{
    int target = (int)(intptr_t)lv_event_get_user_data(e);
    if (learn_box) lv_msgbox_close_async(learn_box);
    learn_box = NULL;
    if (learn_timer) return;
    char text[120];
    if (learn_variant(target) >= 0) {
        if (gen_timer) return;                      /* New puzzle is using the generator (one at a time) */
        if (!learn_board && !(learn_board = heap_caps_malloc(sizeof(*learn_board), MALLOC_CAP_SPIRAM))) return;
        if (!learn_cages && !(learn_cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) return;
        if (!learn_thermos && !(learn_thermos = heap_caps_malloc(STATS_THERMOS_LEN, MALLOC_CAP_SPIRAM))) return;
        snprintf(text, sizeof(text), "Making a %s puzzle where the next step uses its rule...",
                 VARIANT_NAMES[learn_variant(target)]);
    } else {
        snprintf(text, sizeof(text), "Finding a puzzle where the next step is a %s...", LEARN_NAMES[target]);
    }
    learn_target = target;
    learn_state = 0;
    learn_wait = make_msgbox(LV_SYMBOL_EYE_OPEN "  Learn", text);
    /* 16 KB: the variant lessons run the generator (as gen_task); 8 KB overflowed */
    if (xTaskCreatePinnedToCore(learn_task, "learn", 16384, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        lv_msgbox_close(learn_wait);
        learn_wait = NULL;
        return;
    }
    learn_timer = lv_timer_create(learn_poll_cb, 50, NULL);
}

static void learn_close_cb(lv_event_t *e)
{
    if (learn_box) lv_msgbox_close_async(learn_box);
    learn_box = NULL;
}

static void learn_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Learn a technique", NULL);
    learn_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_height(c, 500);                      /* ten techniques: the list scrolls */
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    make_wrap_text(c, &lv_font_montserrat_16, C_MUTED,
                   "Pick one: a puzzle opens where it's the next step, with the hint showing where to look. "
                   "Practice games aren't saved or counted.");
    for (int i = 0; i < LEARN_COUNT; i++) {
        if (i == LEARN_CAGE) {                      /* the variants' own rules: a heading over them */
            lv_obj_t *hd = make_wrap_text(c, &lv_font_montserrat_16, C_MUTED,
                                          "Variant rules: a new puzzle of that kind is made (a few seconds).");
            lv_obj_set_style_margin_top(hd, 14, 0);
        }
        lv_obj_t *row = make_btn(c, 0, 0, 0, 86, NULL, NULL, learn_pick_cb, (void *)(intptr_t)i);   /* 2 lines */
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_style_bg_color(row, C_CARD2, 0);
        lv_obj_set_style_margin_top(row, 8, 0);
        lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text(n, LEARN_NAMES[i]);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 14, 8);
        lv_obj_t *d = make_label(row, &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_width(d, 580);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);   /* (the descriptions take up to two lines) */
        lv_label_set_text(d, LEARN_DESC[i]);
        lv_obj_align(d, LV_ALIGN_TOP_LEFT, 14, 36);
    }
    msgbox_button(m, "Close", learn_close_cb, false);
}

/* ------------------------------------------------------------- enter a puzzle by hand */
/*
 * Enter (Puzzles screen): a blank 9x9 board and a 1-9 pad to type in a
 * Classic puzzle from a book or a newspaper. Check & play: it needs exactly
 * one solution (the solver counts them); then it's graded, saved as "My
 * puzzles #n" (sudoku.db, like an imported one; RAM only without a card)
 * and opened like any other.
 */
static lv_obj_t *scr_edit, *ed_cells[81], *ed_lbl[81], *ed_status;
static uint8_t ed_val[81];
static int ed_sel = -1;
static uint16_t s_own_ram_next = 1;                 /* RAM-only numbering without a card */

static void ed_refresh(void)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int filled = 0, bad = 0;
    for (int i = 0; i < 81; i++) {
        bool clash = false;
        for (int p = 0; ed_val[i] && p < G->npeers[i]; p++) clash |= ed_val[G->peers[i][p]] == ed_val[i];
        filled += ed_val[i] != 0;
        bad += clash;
        lv_color_t bg = i == ed_sel ? C_CELL_SEL : (ed_sel >= 0 && sudoku_geom_peer(G, i, ed_sel) ? C_CELL_PEER : C_CELL);
        if (clash) bg = C_CELL_BAD;
        lv_obj_set_style_bg_color(ed_cells[i], bg, 0);
        lv_obj_set_style_text_color(ed_lbl[i], clash ? C_RED : C_TEXT, 0);
        lv_label_set_text_static(ed_lbl[i], DIGITS[ed_val[i]]);
    }
    if (bad) {
        lv_label_set_text_fmt(ed_status, LV_SYMBOL_WARNING "  %d digits break a rule", bad);
        lv_obj_set_style_text_color(ed_status, C_RED, 0);
    } else {
        lv_label_set_text_fmt(ed_status, "%d given%s  -  tap a cell, then a digit", filled, filled == 1 ? "" : "s");
        lv_obj_set_style_text_color(ed_status, C_MUTED, 0);
    }
}

static void ed_cell_cb(lv_event_t *e)
{
    ed_sel = (int)(intptr_t)lv_event_get_user_data(e);
    ed_refresh();
}

static void ed_digit_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (ed_sel < 0) return;
    ed_val[ed_sel] = ed_val[ed_sel] == d ? 0 : (uint8_t)d;   /* the same digit again clears it */
    if (ed_val[ed_sel] && ed_sel < 80) ed_sel++;     /* on to the next cell, as when typing */
    ed_refresh();
}

static void ed_close(void)
{
    lv_screen_load(scr_list);
    if (scr_edit) lv_obj_delete_async(scr_edit);
    scr_edit = NULL;
}

static void ed_cancel_cb(lv_event_t *e)
{
    ed_close();
}

static void ed_erase_cb(lv_event_t *e)
{
    if (ed_sel >= 0) ed_val[ed_sel] = 0;
    ed_refresh();
}

static void ed_clear_cb(lv_event_t *e)
{
    memset(ed_val, 0, sizeof(ed_val));
    ed_sel = 0;
    ed_refresh();
}

/* the check runs in its own task: the solver recurses a level per empty cell (the UI task's stack is small) */
static volatile int ed_check_state;                 /* 0 idle, 1 checking, 2 done */
static volatile int ed_check_n, ed_check_level;
static uint8_t ed_check_cells[81];
static lv_timer_t *ed_check_timer;
static TaskHandle_t ed_check_handle;                /* its stack in PSRAM (internal RAM is short) */

static void ed_check_task(void *arg)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int n = sudoku_count_solutions(G, ed_check_cells, 2);
    ed_check_level = n == 1 ? sudoku_grade(G, ed_check_cells) : -1;
    ed_check_n = n;
    ed_check_state = 2;
    vTaskSuspend(NULL);                             /* deleted by ed_check_done (a PSRAM-stack task can't delete itself) */
}

static void ed_check_done(lv_timer_t *t)
{
    if (ed_check_state != 2 || eTaskGetState(ed_check_handle) != eSuspended) return;
    vTaskDeleteWithCaps(ed_check_handle);
    ed_check_handle = NULL;
    lv_timer_delete(ed_check_timer);
    ed_check_timer = NULL;
    ed_check_state = 0;
    if (!scr_edit) return;                          /* (cancelled meanwhile) */
    ed_refresh();
    if (ed_check_n == 0) {
        sudoku_ui_notice("This puzzle has no solution: check the digits (red ones break a rule).", 4000);
        return;
    }
    if (ed_check_n > 1) {
        sudoku_ui_notice("This puzzle has more than one solution: some digits are missing.", 4000);
        return;
    }
    int level = ed_check_level;
    if (level < 0 || level >= GEN_LEVELS) level = GEN_EXPERT;
    int number = 0;
    int gid = stats_db_ready() ? stats_db_gen_add_own(ed_check_cells, level, &number) : -1;
    uint16_t id;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = s_gen_ram_next++;                       /* no card: until power-off */
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
        number = s_own_ram_next++;
    }
    gen_cache_put(id, VAR_CLASSIC, level, ed_check_cells, NULL, NULL, NULL, NULL, NULL);
    gen_cache_source(id, STATS_OWN_SOURCE, number);
    char msg[96];
    snprintf(msg, sizeof(msg), "Saved as %s #%d: one solution, rated %s.", STATS_OWN_SOURCE, number,
             GEN_LEVEL_NAMES[level]);
    sudoku_ui_notice(msg, 3500);
    if (scr_edit) lv_obj_delete_async(scr_edit);
    scr_edit = NULL;
    open_puzzle(id);
}

static void ed_play_cb(lv_event_t *e)
{
    if (ed_check_state) return;
    int filled = 0;
    for (int i = 0; i < 81; i++) filled += ed_val[i] != 0;
    if (filled < 17) {                              /* no Sudoku with fewer has just one solution */
        sudoku_ui_notice("A puzzle needs at least 17 digits to have just one solution.", 4000);
        return;
    }
    memcpy(ed_check_cells, ed_val, 81);
    ed_check_state = 1;
    if (xTaskCreatePinnedToCoreWithCaps(ed_check_task, "edcheck", 16384, NULL, 2, &ed_check_handle, WORKER_CORE,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        ed_check_state = 0;
        sudoku_ui_notice("Not enough memory to check the puzzle right now.", 3000);
        return;
    }
    lv_label_set_text(ed_status, "Checking...");
    lv_obj_set_style_text_color(ed_status, C_MUTED, 0);
    ed_check_timer = lv_timer_create(ed_check_done, 50, NULL);
}

static void enter_cb(lv_event_t *e)
{
    memset(ed_val, 0, sizeof(ed_val));
    ed_sel = 0;
    scr_edit = make_screen();
    make_btn(scr_edit, 8, 6, 120, 48, LV_SYMBOL_CLOSE "  Cancel", &lv_font_montserrat_18, ed_cancel_cb, NULL);
    lv_obj_t *t = make_label(scr_edit, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(t, "Enter a puzzle");
    lv_obj_set_pos(t, 142, 6);
    ed_status = make_label(scr_edit, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(ed_status, 142, 36);
    lv_obj_t *go = make_btn(scr_edit, SCR_W - 8 - 220, TOOL_Y, 220, 48, LV_SYMBOL_PLAY "  Check & play",
                            &lv_font_montserrat_18, ed_play_cb, NULL);
    lv_obj_set_style_bg_color(go, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(go, 0), lv_color_white(), 0);
    lv_obj_t *er = make_btn(scr_edit, 8, TOOL_Y, 130, 48, LV_SYMBOL_BACKSPACE "  Erase", &lv_font_montserrat_16,
                            ed_erase_cb, NULL);
    lv_obj_set_style_bg_color(er, C_CARD2, 0);
    lv_obj_t *cl = make_btn(scr_edit, 8 + 130 + 8, TOOL_Y, 130, 48, LV_SYMBOL_TRASH "  Clear", &lv_font_montserrat_16,
                            ed_clear_cb, NULL);
    lv_obj_set_style_bg_color(cl, C_CARD2, 0);

    lv_obj_t *grid = lv_obj_create(scr_edit);
    lv_obj_remove_style_all(grid);
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_t *boxes[9];
    for (int b = 0; b < 9; b++) {
        boxes[b] = lv_obj_create(grid);
        lv_obj_remove_style_all(boxes[b]);
        lv_obj_set_style_bg_color(boxes[b], C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(boxes[b], LV_OPA_COVER, 0);
        lv_obj_set_scrollable(boxes[b], false);
        lv_obj_set_clickable(boxes[b], false);
    }
    for (int i = 0; i < 81; i++) {
        ed_cells[i] = lv_obj_create(grid);
        lv_obj_remove_style_all(ed_cells[i]);
        lv_obj_set_style_bg_opa(ed_cells[i], LV_OPA_COVER, 0);
        lv_obj_set_scrollable(ed_cells[i], false);
        lv_obj_set_clickable(ed_cells[i], true);
        lv_obj_add_event_cb(ed_cells[i], ed_cell_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        ed_lbl[i] = make_label(ed_cells[i], &font_digits_44, C_TEXT);
        lv_obj_center(ed_lbl[i]);
    }
    board_layout(sudoku_geom(VAR_CLASSIC), grid, boxes, ed_cells, NULL, GRID_X);
    const int span = GRID_SIZE - 6, h = (span - 8 * PAD_GAP) / 9;
    for (int d = 1; d <= 9; d++) {
        make_btn(scr_edit, PAD_X, GRID_Y + 1 + (d - 1) * (h + PAD_GAP), PAD_W, h, DIGITS[d], &lv_font_montserrat_28,
                 ed_digit_cb, (void *)(intptr_t)d);
    }
    lv_obj_t *hint = make_label(scr_edit, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(hint, SCR_W - 16);
    lv_label_set_text(hint, "Type in a Classic puzzle's given digits, then Check & play: it needs exactly one solution.");
    lv_obj_set_pos(hint, 8, BOT_Y + (BOT_H - 18) / 2);
    ed_refresh();
    lv_screen_load(scr_edit);
}

/* ============================================================= list screen */

/* the unfinished games, from sudoku.db (without it: just the one in RAM) */
static void games_refresh(void)
{
    int n = stats_db_ready() ? stats_db_list_games(s_games) : -1;
    s_ngames = n < 0 ? 0 : n;
}

static bool game_in_progress(uint16_t id)
{
    if (store_saved_game()->id == id) return true;
    for (int i = 0; i < s_ngames; i++) {
        if (s_games[i].puzzle == id) return true;
    }
    return false;
}

static void list_btnm_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) return;
    if (l_coll >= coll_total() || (int)base->id1 >= coll_size(l_coll)) {   /* a place past the end: nothing */
        if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) lv_draw_task_get_fill_dsc(t)->opa = LV_OPA_TRANSP;
        else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_BORDER) lv_draw_task_get_border_dsc(t)->opa = LV_OPA_TRANSP;
        return;
    }
    uint16_t id = coll_puzzle(l_coll, (int)base->id1);
    bool solved = l_coll < PUZZLE_COLLECTION_COUNT ? store_best_time(id) != 0
                                                   : (base->id1 < 128 && (l_solved_bits[base->id1 >> 3] >> (base->id1 & 7) & 1));
    bool active = game_in_progress(id);
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

/* ---- the Saved tab's rows */
static void list_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= coll_size(l_coll)) return;
    g_from_coll = l_coll;
    open_puzzle(coll_puzzle(l_coll, i));
}

/* a saved collection's puzzle, taken out (Save on the game screen puts it back) */
static void list_row_del_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int k = l_coll >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(l_coll) : -1;
    if (k < 0 || i < 0 || i >= coll_size(l_coll)) return;
    char name[48], msg[128], cname[STATS_IMPORT_NAME];
    uint16_t id = coll_puzzle(l_coll, i);
    puzzle_name(id, name, sizeof(name));
    snprintf(cname, sizeof(cname), "%s", s_saved[k].name);
    bool last = s_saved[k].count == 1;
    if (!stats_db_saved_remove(s_saved[k].id, id)) {
        sudoku_ui_notice("Couldn't change sudoku.db.", 2500);
        return;
    }
    snprintf(msg, sizeof(msg), last ? "%s taken out of %s: it was the last, so the collection is gone."
                                    : "%s taken out of %s.", name, cname);
    sv_changed();
    list_reveal_coll(LV_ANIM_OFF);
    sudoku_ui_notice(msg, 3000);
}

/* one row of the right pane's list: number, puzzle name, a coloured line under it and
 * an optional icon button on the right (act) */
static void list_add_row(int i, uint16_t id, const char *sub, lv_color_t sub_c, lv_event_cb_t cb,
                         const char *act, lv_color_t act_c, lv_event_cb_t act_cb)
{
    lv_obj_t *r = lv_obj_create(l_list);
    lv_obj_set_size(r, LV_PCT(100), 68);
    lv_obj_set_style_bg_color(r, C_CARD, 0);
    lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_set_style_pad_ver(r, 0, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *num = make_label(r, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text_fmt(num, "%d", i + 1);
    lv_obj_set_width(num, 36);
    lv_obj_align(num, LV_ALIGN_LEFT_MID, 0, 0);
    char name[48];
    puzzle_name(id, name, sizeof(name));
    const int w = PANE_W - 28 - 40 - (act ? 60 : 0);
    lv_obj_t *nm = make_label(r, &lv_font_montserrat_20, C_TEXT);
    lv_label_set_text(nm, name);
    lv_obj_set_width(nm, w);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 40, -12);
    lv_obj_t *sl = make_label(r, &lv_font_montserrat_14, sub_c);
    lv_label_set_text(sl, sub);
    lv_obj_set_width(sl, w);
    lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 40, 14);
    if (act) {
        lv_obj_t *d = make_btn(r, 0, 0, 52, 44, act, strcmp(act, SYMBOL_STAR) ? &lv_font_montserrat_20 : &font_icons_20,
                               act_cb, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(d, C_CARD2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(d, 0), act_c, 0);
        lv_obj_align(d, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void list_fill_rows(int lc)
{
    lv_obj_clean(l_list);
    lv_obj_scroll_to_y(l_list, 0, LV_ANIM_OFF);
    bool saved = lc >= PUZZLE_COLLECTION_COUNT && coll_saved_index(lc) >= 0;
    for (int i = 0; i < coll_size(lc) && i < 128; i++) {
        uint16_t id = coll_puzzle(lc, i);
        char sub[96];
        int v = VAR_CLASSIC, lv = 0;
        gen_lookup(id, NULL, &v, &lv);
        bool solved = l_solved_bits[i >> 3] >> (i & 7) & 1, active = game_in_progress(id);
        snprintf(sub, sizeof(sub), "%s %s   -   %s", v >= 0 && v < VAR_COUNT ? VARIANT_NAMES[v] : "",
                 lv >= 0 && lv < GEN_LEVELS ? GEN_LEVEL_NAMES[lv] : "",
                 solved ? "Solved" : active ? "In progress" : "Not solved yet");
        list_add_row(i, id, sub, solved ? C_GREEN : active ? C_ORANGE : C_MUTED, list_row_cb,
                     saved ? LV_SYMBOL_TRASH : NULL, C_RED, list_row_del_cb);   /* take it out of the collection */
    }
}

/* ---- Starred (the star in the title row, a toggle): the player's bookmarks in the
 * right pane, newest first, instead of the collection's puzzles */
static bool      l_starred;
static lv_obj_t *l_star_btn, *l_new_btn;
static uint16_t  l_star_ids[STATS_MAX_BOOKMARKS];
static int       l_nstar;

static void star_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= l_nstar) return;
    open_puzzle(l_star_ids[i]);
}

/* the row's star: unstarred, the row goes */
static void star_row_off_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= l_nstar) return;
    uint16_t id = l_star_ids[i];
    stats_db_set_bookmark(id, false);
    if (id == g_id) {                               /* (the game's own star) */
        g_starred = false;
        update_header();
    }
    char name[48], msg[80];
    puzzle_name(id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "%s unstarred.", name);
    refresh_list();
    sudoku_ui_notice(msg, 2000);
}

static void list_fill_starred(void)
{
    lv_obj_clean(l_list);
    lv_obj_scroll_to_y(l_list, 0, LV_ANIM_OFF);
    int n = stats_db_bookmarks(l_star_ids, STATS_MAX_BOOKMARKS);
    l_nstar = n < 0 ? 0 : n;
    lv_label_set_text(l_name, "Starred");
    lv_label_set_text_fmt(l_stats, n == 1 ? "1 puzzle" : "%d puzzles", l_nstar);
    if (n <= 0) {
        lv_obj_t *t = make_label(l_list, &lv_font_montserrat_18, C_MUTED);
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(t, 40, 0);
        lv_label_set_text(t, n < 0 ? "Stars need sudoku.db on a microSD card."
                                   : "No starred puzzles yet: tap the star next to Share on the game screen to keep "
                                     "one here.");
    }
    for (int i = 0; i < l_nstar; i++) {
        uint16_t id = l_star_ids[i];
        uint32_t b = store_best_time(id);
        char sub[48], t[16];
        if (b) {
            fmt_time(t, sizeof(t), b);
            snprintf(sub, sizeof(sub), "Solved   -   best %s", t);
        } else {
            snprintf(sub, sizeof(sub), game_in_progress(id) ? "In progress" : "Not solved yet");
        }
        list_add_row(i, id, sub, b ? C_GREEN : game_in_progress(id) ? C_ORANGE : C_MUTED, star_row_cb,
                     SYMBOL_STAR, C_ORANGE, star_row_off_cb);
    }
}

static void list_set_starred(bool on)
{
    l_starred = on;
    icon_btn_set(l_star_btn, on, C_ORANGE);
}

static void list_star_cb(lv_event_t *e)
{
    list_set_starred(!l_starred);
    refresh_list();
    if (!l_starred) list_reveal_coll(LV_ANIM_OFF);
}

/* ---- deleting the collection shown (Saved tab) */
static void list_del_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (l_coll < PUZZLE_COLLECTION_COUNT || l_coll >= coll_total()) return;
    int k = coll_saved_index(l_coll);
    char name[STATS_IMPORT_NAME], msg[96];
    snprintf(name, sizeof(name), "%s", coll_name(l_coll));
    bool ok = k >= 0 ? stats_db_saved_delete(s_saved[k].id) : stats_db_import_delete(name);
    snprintf(msg, sizeof(msg), ok ? "%s deleted." : "Couldn't delete %s.", name);
    imports_reload();
    list_fill_colls();
    l_coll = PUZZLE_COLLECTION_COUNT;               /* the Saved tab's first, if any */
    l_tab_coll[1] = -1;
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    update_header();                                /* (the game screen's Save icon) */
    sudoku_ui_notice(msg, 2500);
}

static void list_del_cb(lv_event_t *e)
{
    if (l_coll < PUZZLE_COLLECTION_COUNT || l_coll >= coll_total()) return;
    bool imported = coll_saved_index(l_coll) < 0;
    int n = coll_size(l_coll);
    char title[64], text[256];
    snprintf(title, sizeof(title), "Delete %s?", coll_name(l_coll));
    char what[48];
    if (n == 1) snprintf(what, sizeof(what), "Its puzzle stays on the board: its");
    else snprintf(what, sizeof(what), "Its %d puzzles stay on the board: their", n);
    snprintf(text, sizeof(text), "The collection goes%s. %s solves, History and Gameplays are kept%s.",
             imported ? " (its file can be imported again)" : "", what,
             imported ? "" : n == 1 ? ", and Save can put it in a collection again"
                                    : ", and Save can put them in a collection again");
    lv_obj_t *m = make_msgbox(title, text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_TRASH "  Delete", list_del_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

static void refresh_list(void)
{
    games_refresh();
    if (l_coll >= coll_total()) l_coll = 0;
    if (coll_tab(l_coll) != l_tab) {               /* the tab shown decides: its last, or its first */
        int c = l_tab_coll[l_tab];
        if (c < 0 || c >= coll_total() || coll_tab(c) != l_tab) c = l_tab ? PUZZLE_COLLECTION_COUNT : 0;
        if (c < coll_total()) l_coll = c;
    }
    bool none = coll_tab(l_coll) != l_tab;          /* the Saved tab, with nothing in it */
    for (int i = 0; i < 2; i++) {
        lv_obj_set_state(l_tab_btn[i], LV_STATE_CHECKED, i == l_tab);
        lv_obj_set_style_text_color(lv_obj_get_child(l_tab_btn[i], 0), i == l_tab ? lv_color_white() : C_TEXT, 0);
    }
    lv_obj_set_hidden(l_empty, !none);
    lv_obj_set_hidden(l_btnm, none);
    lv_obj_set_hidden(l_name, none);
    lv_obj_set_hidden(l_stats, none);
    if (!none) l_tab_coll[l_tab] = l_coll;
    bool exportable = !none && coll_saved_index(l_coll) >= 0 && l_coll >= PUZZLE_COLLECTION_COUNT;
    bool listed = !none && l_tab == 1;              /* the Saved tab: rows, not numbers */
    lv_obj_set_hidden(l_export, !exportable);       /* a saved collection: to the card, for Import puzzles */
    lv_obj_set_hidden(l_del, !listed);              /* saved or imported: delete the collection */
    lv_obj_set_width(l_name, PANE_W - 4 - (listed ? 52 + 8 : 0) - (exportable ? 130 + 8 : 0));
    lv_obj_set_hidden(l_btnm, none || listed);
    lv_obj_set_hidden(l_list, !listed);
    if (l_starred) {                                /* the right pane shows Starred: no collection's things */
        lv_obj_set_hidden(l_empty, true);
        lv_obj_set_hidden(l_btnm, true);
        lv_obj_set_hidden(l_export, true);
        lv_obj_set_hidden(l_del, true);
        lv_obj_set_hidden(l_name, false);
        lv_obj_set_hidden(l_stats, false);
        lv_obj_set_hidden(l_list, false);
        lv_obj_set_width(l_name, PANE_W - 4);
    }
    const int lc = l_coll, lsize = coll_size(lc);
    uint16_t total = 0;
    for (int c = 0; c < coll_total(); c++) {
        int size = coll_size(c), k = coll_saved_index(c);
        lv_obj_set_hidden(l_coll_btn[c], coll_tab(c) != l_tab);
        uint16_t n = c < PUZZLE_COLLECTION_COUNT ? store_solved_count(coll_first(c), (uint16_t)size)
                     : k >= 0 ? (uint16_t)stats_db_solved_list(s_saved[k].puzzles, size, c == lc ? l_solved_bits : NULL)
                              : (uint16_t)stats_db_solved_in(coll_first(c), size, c == lc ? l_solved_bits : NULL);
        if (c < PUZZLE_COLLECTION_COUNT) total += n;
        lv_label_set_text_fmt(l_coll_cnt[c], "%u/%u", (unsigned)n, (unsigned)size);
        /* selected row is accent blue: white text there in both themes */
        bool sel = c == l_coll && !none && !l_starred;
        lv_obj_set_state(l_coll_btn[c], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(l_coll_name[c], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(l_coll_cnt[c], sel ? lv_color_white()
                                                   : (n == size ? C_GREEN : C_MUTED), 0);
        lv_obj_set_style_text_opa(l_coll_cnt[c], sel ? LV_OPA_80 : LV_OPA_COVER, 0);
    }
    lv_label_set_text_fmt(l_total, "%u of %u solved", (unsigned)total, (unsigned)PUZZLE_COUNT);
    lv_obj_invalidate(l_colls);                     /* (rows hidden or shown: no stale scrollbar left) */

    /* collection panel (best times: the built-in ones, which the board keeps in RAM) */
    uint32_t sum = 0, best = 0;
    uint16_t n = 0;
    for (int i = 0; lc < PUZZLE_COLLECTION_COUNT && i < lsize; i++) {
        uint32_t s = store_best_time(coll_first(lc) + i);
        if (!s) continue;
        n++;
        sum += s;
        if (!best || s < best) best = s;
    }
    if (lc >= PUZZLE_COLLECTION_COUNT) {
        for (int i = 0; i < lsize && i < 128; i++) n += l_solved_bits[i >> 3] >> (i & 7) & 1;
    }
    if (listed && !l_starred) list_fill_rows(lc);   /* (after the counts: l_solved_bits is lc's) */
    lv_label_set_text_fmt(l_name, "%s%s", coll_name(lc),
                          lc >= PUZZLE_COLLECTION_COUNT && coll_saved_index(lc) < 0 ? "  (imported)" : "");
    if (n && best) {
        char b[16], a[16];
        fmt_time(b, sizeof(b), best);
        fmt_time(a, sizeof(a), sum / n);
        lv_label_set_text_fmt(l_stats, "%u / %u solved   -   fastest %s   -   average %s",
                              (unsigned)n, (unsigned)lsize, b, a);
    } else {
        lv_label_set_text_fmt(l_stats, "%u / %u solved", (unsigned)n, (unsigned)lsize);
    }

    /* puzzle numbers 1..count, 10 per row; always all 100 places (the ones past count
     * disabled and not drawn: list_btnm_draw_cb), so a small collection's buttons are as
     * big as everywhere else (hidden ones would give their room to the others) */
    int m = 0;
    int cnt = lsize > MAX_PER_COLL ? MAX_PER_COLL : lsize;
    for (int i = 0; i < MAX_PER_COLL; i++) {
        if (i && i % 10 == 0) l_map[m++] = "\n";
        if (i < cnt) snprintf(l_labels[i], sizeof(l_labels[i]), "%d", i + 1);
        else snprintf(l_labels[i], sizeof(l_labels[i]), " ");   /* ("" would end the map) */
        l_map[m++] = l_labels[i];
    }
    l_map[m] = "";
    lv_buttonmatrix_set_map(l_btnm, l_map);
    lv_buttonmatrix_clear_button_ctrl_all(l_btnm, LV_BUTTONMATRIX_CTRL_DISABLED);
    for (int i = cnt; i < MAX_PER_COLL; i++) {
        lv_buttonmatrix_set_button_ctrl(l_btnm, (uint32_t)i, LV_BUTTONMATRIX_CTRL_DISABLED);
    }
    if (l_starred) list_fill_starred();            /* (its name and count over the collection's) */
}

/* scrolls the collection column so the selected one is visible */
static void list_reveal_coll(lv_anim_enable_t anim)
{
    lv_obj_update_layout(scr_list);
    if (l_coll < coll_total() && coll_tab(l_coll) == l_tab) lv_obj_scroll_to_view(l_coll_btn[l_coll], anim);
}

static void show_list(void)
{
    int gc = g_id != STORE_NO_GAME ? coll_of(g_id) : -1;
    if (gc < 0 && g_id != STORE_NO_GAME && coll_find(g_from_coll, g_id) >= 0) gc = g_from_coll;   /* a saved one */
    if (gc >= 0) {                                  /* the collection of the puzzle just played */
        l_coll = gc;
        l_tab = coll_tab(gc);
        store_set_last_collection((uint8_t)l_coll);  /* (built-in ones only) */
    }
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    lv_screen_load(scr_list);
}

static void coll_click_cb(lv_event_t *e)
{
    l_coll = (int)(intptr_t)lv_event_get_user_data(e);
    list_set_starred(false);
    store_set_last_collection((uint8_t)l_coll);
    refresh_list();
    list_reveal_coll(LV_ANIM_ON);
}

/* Saved tab: the collection shown, written to the card (puzzles/<name>.sdx) */
static void list_export_cb(lv_event_t *e)
{
    int k = l_coll >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(l_coll) : -1;
    if (k < 0) return;
    char msg[96];
    stats_db_saved_export(s_saved[k].id, msg, sizeof(msg));
    sudoku_ui_notice(msg, 4000);
}

static void list_tab_cb(lv_event_t *e)
{
    int t = (int)(intptr_t)lv_event_get_user_data(e);
    if (t == l_tab && !l_starred) return;
    list_set_starred(false);
    l_tab = t;
    lv_obj_scroll_to_y(l_colls, 0, LV_ANIM_OFF);
    refresh_list();                                 /* (takes the tab's last collection) */
    list_reveal_coll(LV_ANIM_OFF);
}

/* the dots under the menu (small circles, the page shown in the accent colour) */
static void home_dots(void)
{
    if (!h_dots) return;
    lv_obj_set_hidden(h_dots, h_npages < 2);
    if (h_npages < 2) return;
    int cur = (int)((lv_obj_get_scroll_x(h_menu) + HOME_GRID_W / 2) / HOME_GRID_W);
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_t *d = lv_obj_get_child(h_dots, p);
        lv_obj_set_hidden(d, p >= h_npages);
        lv_obj_set_style_bg_color(d, p == cur ? C_ACCENT : C_CARD2, 0);
    }
}

static void home_scroll_cb(lv_event_t *e)
{
    home_dots();
}

/* Home menu: a column of full-width buttons while they fit between the header and the
 * bottom of the screen; otherwise a grid of two per row; and when even that doesn't fit,
 * pages of the grid side by side, swiped between, with dots under them. */
static void home_layout(void)
{
    lv_obj_t *vis[16];
    int n = 0;
    for (int i = 0; i < h_nitems; i++) if (!lv_obj_is_hidden(h_items[i])) vis[n++] = h_items[i];
    const int rows = (HOME_MENU_MAX_H + HOME_GAP) / (HOME_BTN_H + HOME_GAP);   /* that fit under the header */
    bool grid = n > rows;
    int per_page = grid ? rows * 2 : rows;
    int npages = (n + per_page - 1) / per_page;
    if (npages > HOME_PAGES_MAX) npages = HOME_PAGES_MAX;
    int sig = (grid ? 1000 : 0) + n * 10 + npages;      /* (the same layout: nothing to do) */
    if (sig == h_menu_grid) return;
    h_menu_grid = sig;
    h_npages = npages;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_set_flex_flow(h_page[p], grid ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(h_page[p], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_hidden(h_page[p], p >= npages);
    }
    for (int i = 0; i < h_nitems; i++) {               /* hidden ones wait on page 1 */
        if (lv_obj_is_hidden(h_items[i])) lv_obj_set_parent(h_items[i], h_page[0]);
    }
    for (int k = 0; k < n; k++) {
        int p = k / per_page;
        if (p >= npages) p = npages - 1;
        lv_obj_set_parent(vis[k], h_page[p]);
        lv_obj_move_to_index(vis[k], -1);              /* (in order on its page) */
        lv_obj_set_width(vis[k], grid ? (HOME_GRID_W - HOME_GAP) / 2 : HOME_COL_W);
    }
    lv_obj_set_scrollable(h_menu, npages > 1);
    lv_obj_update_layout(h_menu);
    if (lv_obj_get_scroll_x(h_menu) > (npages - 1) * HOME_GRID_W) lv_obj_scroll_to_x(h_menu, 0, LV_ANIM_OFF);
    home_dots();
}

/* ------------------------------------------------------------- the week (Home) */
/* The "puzzles solved" line also says how this week is going; a tap compares it
 * with last week (stats_db_query_weeksum: needs the clock, so only once it's set). */
static stats_weeksum_t h_week;
static bool h_week_ok;
static unsigned h_total_n;

static void home_total_text(void)
{
    char t[16], line[128];
    int len = snprintf(line, sizeof(line), "%u of %u puzzles solved", h_total_n, (unsigned)PUZZLE_COUNT);
    if (h_week_ok) {
        len += snprintf(line + len, sizeof(line) - len, "   -   this week: %d", h_week.n[0]);
        if (h_week.n[0]) {
            fmt_time(t, sizeof(t), h_week.best[0]);
            len += snprintf(line + len, sizeof(line) - len, ", best %s", t);
        }
        snprintf(line + len, sizeof(line) - len, "  " LV_SYMBOL_RIGHT);
    }
    lv_label_set_text(h_total, line);
}

static void week_row(lv_obj_t *parent, const char *name, const char *a, const char *b, bool head)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 34);
    const lv_font_t *f = &lv_font_montserrat_18;
    lv_obj_t *l = make_label(r, f, head ? C_MUTED : C_TEXT);
    lv_label_set_text(l, name);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *x = make_label(r, f, head ? C_MUTED : C_TEXT);
    lv_label_set_text(x, a);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, -150, 0);
    lv_obj_t *y = make_label(r, f, C_MUTED);
    lv_label_set_text(y, b);
    lv_obj_align(y, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void week_cb(lv_event_t *e)
{
    if (!h_week_ok) return;
    const stats_weeksum_t *w = &h_week;
    lv_obj_t *m = make_msgbox(SYMBOL_CALENDAR "  This week", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    char a[24], b[24];
    week_row(c, "", "This week", "Last week", true);
    snprintf(a, sizeof(a), "%d", w->n[0]); snprintf(b, sizeof(b), "%d", w->n[1]);
    week_row(c, "Solved", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) fmt_time(o, 24, w->best[k]); else snprintf(o, 24, "-");
    }
    week_row(c, "Fastest", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) fmt_time(o, 24, w->avg[k]); else snprintf(o, 24, "-");
    }
    week_row(c, "Average time", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) snprintf(o, 24, "%u.%u", w->mist10[k] / 10, w->mist10[k] % 10); else snprintf(o, 24, "-");
    }
    week_row(c, "Mistakes per puzzle", a, b, false);
    snprintf(a, sizeof(a), "%lu", (unsigned long)w->hints[0]); snprintf(b, sizeof(b), "%lu", (unsigned long)w->hints[1]);
    week_row(c, "Hints", a, b, false);
    char verdict[128] = "";
    if (w->n[0] && w->n[1]) {
        int dm = (int)w->mist10[1] - (int)w->mist10[0];
        int dt = (int)w->avg[1] - (int)w->avg[0];
        char t[16];
        fmt_time(t, sizeof(t), (uint32_t)(dt < 0 ? -dt : dt));
        snprintf(verdict, sizeof(verdict), "%s%d.%d %s mistakes per puzzle, and %s %s on average than last week.",
                 "", (dm < 0 ? -dm : dm) / 10, (dm < 0 ? -dm : dm) % 10, dm >= 0 ? "fewer" : "more",
                 t, dt >= 0 ? "faster" : "slower");
    } else if (w->n[0]) {
        snprintf(verdict, sizeof(verdict), "Nothing solved last week to compare with.");
    } else {
        snprintf(verdict, sizeof(verdict), "Nothing solved yet this week: the week starts on Monday.");
    }
    lv_obj_t *v = make_wrap_text(c, &lv_font_montserrat_18, C_ACCENT, verdict);
    lv_obj_set_style_margin_top(v, 10, 0);
    msgbox_button(m, "Close", mbox_close_cb, false);
}

/* Home's Players button: the avatar of who plays now (from the player list; asked for once
 * when there isn't one yet), else the person icon */
static void home_player_update(void)
{
    if (!h_player_title) return;
    static bool asked;
    int av = 0;
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == store_player_id() && p_players[i].avatar <= AVATAR_COUNT) av = p_players[i].avatar;
    }
    if (p_count < 0 && !asked && stats_db_ready()) {
        asked = true;
        stats_db_list_players();                    /* (render_players calls back here when it comes) */
    }
    lv_obj_set_hidden(h_player_avatar, !av);
    if (av) lv_image_set_src(h_player_avatar, &avatar_images[av - 1]);
    lv_label_set_text(h_player_title, av ? "Players" : SYMBOL_USER "  Players");
}

static void refresh_home(void)
{
    uint16_t total = 0;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        total += store_solved_count(puzzle_collections[c].first, puzzle_collections[c].count);
    }
    h_total_n = total;
    home_total_text();
    if (stats_db_ready() && time(NULL) > 1700000000) stats_db_query_weeksum();   /* (clock_cb picks it up) */
    lv_label_set_text_fmt(h_player, "Playing as %s", store_player_name());
    home_player_update();

    const saved_game_t *s = store_saved_game();
    games_refresh();
    /* the game played last: the one in RAM if there is one, else sudoku.db's newest */
    uint16_t last = s->id != STORE_NO_GAME ? s->id : (s_ngames ? s_games[0].puzzle : STORE_NO_GAME);
    uint32_t ms = s->id != STORE_NO_GAME ? s->elapsed_ms : (s_ngames ? s_games[0].elapsed_ms : 0);
    int others = 0;
    for (int i = 0; i < s_ngames; i++) others += s_games[i].puzzle != last;
    /* Continue stays in its place: greyed out (like Daily puzzle without the clock) when
     * there is nothing to continue, so the menu doesn't shift around */
    bool any = last != STORE_NO_GAME;
    lv_obj_set_state(h_continue, LV_STATE_DISABLED, !any);
    lv_obj_set_style_bg_color(h_continue, any ? C_LIST_ACTIVE : C_CARD, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_continue, 0), any ? lv_color_white() : C_TEXT, 0);
    lv_obj_set_style_text_color(h_continue_sub, any ? lv_color_white() : C_MUTED, 0);
    if (any) {
        char name[40], t[16];
        puzzle_name(last, name, sizeof(name));
        fmt_time(t, sizeof(t), ms / 1000);
        if (others) lv_label_set_text_fmt(h_continue_sub, "%s  -  %s   (+%d more)", name, t, others);
        else lv_label_set_text_fmt(h_continue_sub, "%s  -  %s", name, t);
    } else {
        lv_label_set_text(h_continue_sub, "No game in progress");
    }
    daily_btn_update();
    if (stats_db_ready()) stats_db_query_achievements();   /* the Trophies line (trophy_poll) */
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

static void st_select_tab(int tab);

/* The Wi-Fi row's text: the state, or once connected the network's name,
 * which stays hidden ("Connected  ******") until the eye button shows it, so
 * a photo or screenshot of Settings doesn't give it away. */
static void st_wifi_update(void)
{
    wifi_status_t ws;
    wifi_mgr_get_status(&ws);
    bool connected = ws.state == WIFI_ST_CONNECTED;
    const char *w = connected && !st_wifi_reveal ? "Connected  ******" : wifi_ui_summary();
    if (strcmp(lv_label_get_text(st_wifi_lbl), w) != 0) lv_label_set_text(st_wifi_lbl, w);
    lv_obj_set_hidden(st_wifi_eye, !connected);
    lv_label_set_text(lv_obj_get_child(st_wifi_eye, 0), st_wifi_reveal ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void st_wifi_eye_cb(lv_event_t *e)
{
    st_wifi_reveal = !st_wifi_reveal;
    st_wifi_update();
}

static void settings_cb(lv_event_t *e)
{
    st_wifi_reveal = false;                         /* the network's name hidden again */
    st_wifi_update();
    st_select_tab(0);                               /* Network */
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
        st_wifi_update();
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
        if (valid && stats_db_ready()) stats_db_query_weeksum();   /* the clock is set, or a new day */
    }
    if (stats_db_take_weeksum(&h_week)) {
        h_week_ok = valid;
        home_total_text();
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

/* a player's avatar (an image) or, without one, a dot of their colour; not tappable */
static lv_obj_t *player_avatar_obj(lv_obj_t *parent, const stats_player_t *pl)
{
    lv_obj_t *o;
    if (pl && pl->avatar && pl->avatar <= AVATAR_COUNT) {
        o = lv_image_create(parent);
        lv_image_set_src(o, &avatar_images[pl->avatar - 1]);
    } else {
        o = lv_obj_create(parent);                  /* the dot, centred in the avatar's place */
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, AVATAR_SIZE, AVATAR_SIZE);
        lv_obj_center(player_dot(o, pl ? pl->color : 0, 20));
    }
    lv_obj_set_clickable(o, false);
    return o;
}

/* the player dropdown on Statistics: everyone from sudoku.db (the Players
 * screen's list), with their avatars; the closed box shows who is playing */
#define X_DD_PAD   14                                 /* the dropdown's left padding, without an avatar */
#define X_DD_LS    22                                 /* the list's line space */
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

    /* the closed box: the avatar of who plays now left of the name, or the person icon */
    const stats_player_t *cur = db ? &p_players[sel] : NULL;
    bool av = cur && cur->avatar && cur->avatar <= AVATAR_COUNT;
    if (x_player_av) lv_obj_delete(x_player_av);
    x_player_av = NULL;
    if (av) {
        x_player_av = player_avatar_obj(x_player_dd, cur);
        lv_obj_align(x_player_av, LV_ALIGN_LEFT_MID, -(AVATAR_SIZE + 10), 0);   /* (in the padding) */
    }
    lv_obj_set_style_pad_left(x_player_dd, av ? X_DD_PAD + AVATAR_SIZE + 10 : X_DD_PAD, 0);
    snprintf(x_player_txt, sizeof(x_player_txt), av ? "%s" : SYMBOL_USER "  %s", store_player_name());
    lv_dropdown_set_text(x_player_dd, x_player_txt);

    /* the open list: room for the avatars left of the names (stats_player_list_draw_cb) */
    lv_obj_set_style_pad_left(lv_dropdown_get_list(x_player_dd), db ? 12 + AVATAR_SIZE + 12 : 12, 0);
    lv_obj_set_state(x_player_dd, LV_STATE_DISABLED, !db || p_count < 2);
}

/* the open list's avatars (or dots), each left of its name. Drawn after the list (DRAW_POST, after the
 * dropdown's own: the selected line's highlight is drawn then too, and would cover children). */
static void stats_player_list_draw_cb(lv_event_t *e)
{
    if (!(stats_db_ready() && p_count > 0)) return;
    lv_obj_t *pdl = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(pdl, &a);
    int fh = lv_font_get_line_height(&lv_font_montserrat_20);
    int x = a.x1 + 12, y0 = a.y1 + lv_obj_get_style_pad_top(pdl, 0) - lv_obj_get_scroll_y(pdl);
    for (int i = 0; i < p_count; i++) {
        const stats_player_t *pl = &p_players[i];
        int y = y0 + i * (fh + X_DD_LS) + (fh - AVATAR_SIZE) / 2;
        if (pl->avatar && pl->avatar <= AVATAR_COUNT) {
            lv_draw_image_dsc_t d;
            lv_draw_image_dsc_init(&d);
            d.src = &avatar_images[pl->avatar - 1];
            lv_area_t im = {x, y, x + AVATAR_SIZE - 1, y + AVATAR_SIZE - 1};
            lv_draw_image(layer, &d, &im);
        } else {                                    /* their colour's dot */
            lv_draw_rect_dsc_t r;
            lv_draw_rect_dsc_init(&r);
            r.bg_color = player_color(pl->color);
            r.radius = LV_RADIUS_CIRCLE;
            int o = (AVATAR_SIZE - 20) / 2;
            lv_area_t dot = {x + o, y + o, x + o + 19, y + o + 19};
            lv_draw_rect(layer, &r, &dot);
        }
    }
}

static void stats_player_dd_cb(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(x_player_dd);
    if ((int)i < p_count && p_players[i].id != store_player_id()) {
        stats_db_switch_player(p_players[i].id);   /* statistics reload when the switch is done */
    }
}

static stats_view_t *x_view_p;           /* PSRAM (internal RAM is tight): the view shown */
#define x_view (*x_view_p)

static void stats_select_rows(void)
{
    for (int r = 0; r < X_ROWS; r++) {
        bool sel = r == x_sel;
        lv_obj_set_state(x_btn[r], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(x_name_lbl[r], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(x_cnt_lbl[r], sel ? lv_color_white() : C_MUTED, 0);
    }
}

static void chart_render(const stats_view_t *v, bool from_db);

/* a kind's four levels added up (fastest: the lowest) */
static void kind_total(const stats_view_t *v, int k, stats_level_t *t)
{
    memset(t, 0, sizeof(*t));
    for (int l = 0; l < GEN_LEVELS; l++) {
        const stats_level_t *x = &v->kinds[k][l];
        t->solved += x->solved;
        t->perfect += x->perfect;
        t->known += x->known;
        t->solves += x->solves;
        t->time_sum += x->time_sum;
        t->miss_sum += x->miss_sum;
        if (x->fastest && (!t->fastest || x->fastest < t->fastest)) t->fastest = x->fastest;
    }
}

static void render_stats(const stats_view_t *v, bool from_db)
{
    chart_render(v, from_db);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        lv_label_set_text_fmt(x_cnt_lbl[c + 1], "%u/%u", (unsigned)v->levels[c].solved,
                              (unsigned)puzzle_collections[c].count);
    }
    lv_label_set_text_fmt(x_cnt_lbl[0], "%u/%u", (unsigned)v->total.solved, (unsigned)PUZZLE_COUNT);
    stats_level_t kt[KIND_COUNT];
    for (int k = 0; k < KIND_COUNT; k++) {          /* kinds made on the board: puzzles solved */
        kind_total(v, k, &kt[k]);
        lv_label_set_text_fmt(x_cnt_lbl[1 + PUZZLE_COLLECTION_COUNT + k], "%u", (unsigned)kt[k].solved);
    }
    stats_player_dd_update();
    stats_select_rows();
    x_from_db = from_db;

    char f[16], a[16], m[16];
    lv_obj_scroll_to_y(x_table, 0, LV_ANIM_OFF);
    if (v->level < 0) {
        /* one row per level */
        lv_label_set_text(x_title, "All levels");
        stats_summary(&v->total, PUZZLE_COUNT);
        static const int32_t w[] = {120, 70, 72, 86, 84};   /* sums to PANE_W; every header on one line */
        static const char *const h[] = {"Level", "Solved", "Fastest", "Average", "Mistakes"};
        lv_table_set_row_count(x_table, PUZZLE_COLLECTION_COUNT + KIND_COUNT + 1);
        table_columns(5, w, h);
        for (int c = 0; c < PUZZLE_COLLECTION_COUNT + KIND_COUNT; c++) {   /* collections, then kinds */
            bool coll = c < PUZZLE_COLLECTION_COUNT;
            const level_stat_t *st = coll ? &v->levels[c] : &kt[c - PUZZLE_COLLECTION_COUNT];
            fmt_stat_times(f, a, sizeof(f), st);
            fmt_avg_mistakes(m, sizeof(m), st);
            lv_table_set_cell_value(x_table, c + 1, 0, coll ? puzzle_collections[c].name
                                                            : STATS_KIND_NAMES[c - PUZZLE_COLLECTION_COUNT]);
            lv_table_set_cell_value_fmt(x_table, c + 1, 1, "%u", (unsigned)st->solved);
            lv_table_set_cell_value(x_table, c + 1, 2, f);
            lv_table_set_cell_value(x_table, c + 1, 3, a);
            lv_table_set_cell_value(x_table, c + 1, 4, m);
        }
        lv_obj_set_hidden(x_table, x_chart_mode);
        lv_obj_set_hidden(x_empty, true);
        return;
    }

    if (v->level >= PUZZLE_COLLECTION_COUNT) {     /* a kind made on the board: one row per level */
        int k = v->level - PUZZLE_COLLECTION_COUNT;
        lv_label_set_text(x_title, STATS_KIND_NAMES[k]);
        const level_stat_t *t = &kt[k];
        fmt_stat_times(f, a, sizeof(f), t);
        fmt_avg_mistakes(m, sizeof(m), t);
        lv_label_set_text_fmt(x_sum1, "Solved %u puzzle%s   -   %u without mistakes   -   %lu solves",
                              (unsigned)t->solved, t->solved == 1 ? "" : "s", (unsigned)t->perfect,
                              (unsigned long)t->solves);
        lv_label_set_text_fmt(x_sum2, "Fastest %s   -   average %s   -   mistakes %s per puzzle", f, a, m);
        static const int32_t w[] = {120, 70, 72, 86, 84};
        static const char *const h[] = {"Level", "Solved", "Fastest", "Average", "Mistakes"};
        lv_table_set_row_count(x_table, GEN_LEVELS + 1);
        table_columns(5, w, h);
        for (int l = 0; l < GEN_LEVELS; l++) {
            const level_stat_t *st = &v->kinds[k][l];
            fmt_stat_times(f, a, sizeof(f), st);
            fmt_avg_mistakes(m, sizeof(m), st);
            lv_table_set_cell_value(x_table, l + 1, 0, GEN_LEVEL_NAMES[l]);
            lv_table_set_cell_value_fmt(x_table, l + 1, 1, "%u", (unsigned)st->solved);
            lv_table_set_cell_value(x_table, l + 1, 2, f);
            lv_table_set_cell_value(x_table, l + 1, 3, a);
            lv_table_set_cell_value(x_table, l + 1, 4, m);
        }
        lv_label_set_text_fmt(x_empty, from_db ? "No %s solved yet." : "%s needs sudoku.db on a microSD card.",
                              STATS_KIND_NAMES[k]);
        lv_obj_set_hidden(x_table, x_chart_mode || t->solves == 0);
        lv_obj_set_hidden(x_empty, x_chart_mode || t->solves != 0);
        return;
    }

    /* one row per solved puzzle of the level */
    lv_label_set_text(x_empty, "No puzzles solved in this level yet.");
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
    lv_obj_set_hidden(x_table, x_chart_mode || v->n_puzzles == 0);
    lv_obj_set_hidden(x_empty, x_chart_mode || v->n_puzzles != 0);
}

/* ------------------------------------------------------------- progress chart */
/*
 * The Chart button on Statistics swaps the table for a chart of the player's
 * newest solves at the selected level (STATS_RECENT, oldest on the left):
 * a dot per solve and an orange line for the average of the last 5, with
 * minutes up the side. Below it, how the newest 5 compare with the first 5.
 */
#define AVG_WIN 5
#define PLOT_L  58                      /* room for the time labels */
#define PLOT_R  14
#define PLOT_T  14
#define PLOT_B  44                      /* room for the legend */

/* a scale step that gives at most 5 lines up to max seconds */
static uint32_t chart_step(uint32_t max)
{
    static const uint32_t STEPS[] = {30, 60, 120, 300, 600, 900, 1200, 1800, 3600, 7200};
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); i++) {
        if (max <= STEPS[i] * 5) return STEPS[i];
    }
    return (max / 5 / 3600 + 1) * 3600;
}

/* trailing average of up to AVG_WIN solves ending at i */
static uint32_t chart_avg(const stats_view_t *v, int i)
{
    int from = i - AVG_WIN + 1 < 0 ? 0 : i - AVG_WIN + 1;
    uint32_t sum = 0;
    for (int k = from; k <= i; k++) sum += v->recent[k];
    return sum / (uint32_t)(i - from + 1);
}

static void plot_text(lv_layer_t *layer, const char *text, lv_color_t color, int x1, int y1, int x2,
                      lv_text_align_t align)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &lv_font_montserrat_14;
    d.color = color;
    d.align = align;
    d.text = text;
    d.text_local = 1;                               /* copied: text is on the stack */
    lv_area_t a = {.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y1 + 18};
    lv_draw_label(layer, &d, &a);
}

static void plot_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t color, int width, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    d.color = color;
    d.width = width;
    d.opa = opa;
    d.round_start = d.round_end = 1;
    lv_draw_line(layer, &d);
}

static void plot_dot(lv_layer_t *layer, int x, int y, int r, lv_color_t color)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {.x1 = x - r, .y1 = y - r, .x2 = x + r, .y2 = y + r};
    lv_draw_rect(layer, &d, &a);
}

static void plot_draw_cb(lv_event_t *e)
{
    const stats_view_t *v = &x_view;
    if (v->n_recent < 2) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(x_plot, &co);
    int left = co.x1 + PLOT_L, right = co.x2 - PLOT_R, top = co.y1 + PLOT_T, bottom = co.y2 - PLOT_B;
    uint32_t max = 0;
    for (int i = 0; i < v->n_recent; i++) {
        if (v->recent[i] > max) max = v->recent[i];
    }
    uint32_t step = chart_step(max), lines = (max + step - 1) / step, top_s = lines * step;
    if (!top_s) top_s = step;

    /* scale: a line per step, labelled in minutes */
    char buf[16];
    for (uint32_t k = 0; k <= lines; k++) {
        int y = bottom - (int)((int64_t)(bottom - top) * k * step / top_s);
        plot_line(layer, left, y, right, y, C_CARD2, 1, LV_OPA_COVER);
        fmt_time(buf, sizeof(buf), k * step);
        plot_text(layer, buf, C_MUTED, co.x1, y - 9, left - 8, LV_TEXT_ALIGN_RIGHT);
    }

    /* the solves: dots joined by a faint line, then the average on top */
    const int n = v->n_recent;
    int px = 0, py = 0, ax = 0, ay = 0;
    for (int i = 0; i < n; i++) {
        int x = left + (right - left) * i / (n - 1);
        int y = bottom - (int)((int64_t)(bottom - top) * v->recent[i] / top_s);
        if (i) plot_line(layer, px, py, x, y, C_ACCENT, 2, LV_OPA_40);
        px = x;
        py = y;
    }
    for (int i = 0; i < n; i++) {
        int x = left + (right - left) * i / (n - 1);
        int y = bottom - (int)((int64_t)(bottom - top) * v->recent[i] / top_s);
        plot_dot(layer, x, y, 4, C_ACCENT);
    }
    for (int i = 0; i < n; i++) {
        int x = left + (right - left) * i / (n - 1);
        int y = bottom - (int)((int64_t)(bottom - top) * chart_avg(v, i) / top_s);
        if (i) plot_line(layer, ax, ay, x, y, C_ORANGE, 4, LV_OPA_COVER);
        ax = x;
        ay = y;
    }

    /* legend under the plot */
    int ly = co.y2 - 26;
    plot_dot(layer, left + 6, ly + 9, 5, C_ACCENT);
    plot_text(layer, "Each solve", C_MUTED, left + 18, ly, left + 140, LV_TEXT_ALIGN_LEFT);
    plot_line(layer, left + 150, ly + 9, left + 176, ly + 9, C_ORANGE, 4, LV_OPA_COVER);
    plot_text(layer, "Average of the last 5", C_MUTED, left + 186, ly, right, LV_TEXT_ALIGN_LEFT);
}

static void chart_render(const stats_view_t *v, bool from_db)
{
    lv_obj_set_hidden(x_chart, !x_chart_mode);
    lv_label_set_text(lv_obj_get_child(x_chart_btn, 0), x_chart_mode ? LV_SYMBOL_LIST "  Table"
                                                                     : SYMBOL_CHART "  Chart");
    if (!x_chart_mode) return;
    char text[200];
    const int n = v->n_recent;
    if (!from_db) {
        snprintf(text, sizeof(text), "The chart needs sudoku.db on a microSD card: it is drawn from the "
                                     "solves logged there.");
    } else if (n < 2) {
        snprintf(text, sizeof(text), "Solve at least two puzzles%s to see a chart.",
                 v->level < 0 ? "" : " at this level");
    } else {
        uint32_t fastest = 0xFFFFFFFF;
        for (int i = 0; i < n; i++) {
            if (v->recent[i] < fastest) fastest = v->recent[i];
        }
        char f[16], a1[16], a2[16];
        fmt_time(f, sizeof(f), fastest);
        int len = snprintf(text, sizeof(text), "Your last %d solve%s%s, oldest on the left. Fastest %s.", n,
                           n == 1 ? "" : "s", v->level < 0 ? "" : " at this level", f);
        if (n >= 2 * AVG_WIN) {                         /* newest 5 against the first 5 shown */
            uint32_t first = 0, last = 0;
            for (int i = 0; i < AVG_WIN; i++) {
                first += v->recent[i];
                last += v->recent[n - AVG_WIN + i];
            }
            first /= AVG_WIN;
            last /= AVG_WIN;
            fmt_time(a1, sizeof(a1), first);
            fmt_time(a2, sizeof(a2), last);
            int pct = first ? (int)(((int64_t)first - (int64_t)last) * 100 / (int64_t)first) : 0;
            if (pct > 0) {
                snprintf(text + len, sizeof(text) - len, "\nThe newest 5 average %s, the first 5 %s: "
                         "%d%% faster.", a2, a1, pct);
            } else if (pct < 0) {
                snprintf(text + len, sizeof(text) - len, "\nThe newest 5 average %s, the first 5 %s: "
                         "%d%% slower.", a2, a1, -pct);
            } else {
                snprintf(text + len, sizeof(text) - len, "\nThe newest 5 average %s, the same as the first 5.", a2);
            }
        }
    }
    lv_label_set_text(x_trend, text);
    lv_obj_set_hidden(x_plot, !from_db || n < 2);
    lv_obj_invalidate(x_plot);
}

static void chart_btn_cb(lv_event_t *e)
{
    x_chart_mode = !x_chart_mode;
    render_stats(&x_view, x_from_db);
}

/* the (i) button next to the title: what the numbers mean */
static void stats_info_cb(lv_event_t *e)
{
    static char text[640];
    snprintf(text, sizeof(text), "%s\n\n"
             "Mistake: a placed digit that broke a row, column or box rule (with Mistake check on Solution, "
             "also one that wasn't the solution's). Undo doesn't take it back.\n\n"
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

static void gibberlink_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    store_set_gibberlink(on);
    gibberlink_set_enabled(on);
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
    int nb = store_night_brightness();
    bsp_display_brightness_set(s_night && store_night_mode() && nb < v ? nb : v);   /* (at night: at most that) */
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

/* ------------------------------------------------------------- firmware update (from the card) */
/*
 * Settings > Data > Firmware update: update.bin in the card's root (the
 * sketch's .ino.bin from the Arduino IDE's Export Compiled Binary, or the
 * build folder) goes into the other app slot (two 12.5 MiB slots, ota_0 /
 * ota_1). esp_ota_end() checks the whole image (format, chip, SHA-256)
 * before the board is told to start it, so a broken file changes nothing.
 * The new firmware starts on probation (the bootloader's rollback): it is
 * kept once the screens are up and sudoku.db has opened (sudoku_ui_mark_ok());
 * if it crashes before that, the next restart goes back to the old one.
 * Afterwards update.bin is renamed update.done.
 */
#define FW_FILE  BSP_SD_MOUNT_POINT "/update.bin"
#define FW_DONE  BSP_SD_MOUNT_POINT "/update.done"
#define FW_CHUNK (16 * 1024)

static lv_obj_t *st_fw_btn, *st_fw_hint;
static lv_obj_t *fw_ovl, *fw_bar, *fw_lbl;
static lv_timer_t *fw_timer;
static volatile int fw_state;                       /* 0 writing, 1 done, 2 failed */
static volatile uint32_t fw_done_bytes, fw_size;
static char fw_err[96];

/* update.bin's app description (its build date, version); false if it isn't an ESP app image */
static bool fw_file_desc(esp_app_desc_t *d, uint32_t *size)
{
    struct stat st;
    if (stat(FW_FILE, &st) != 0 || st.st_size < (off_t)(sizeof(esp_image_header_t) + 256)) return false;
    FILE *f = fopen(FW_FILE, "rb");
    if (!f) return false;
    esp_image_header_t h;
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == ESP_IMAGE_HEADER_MAGIC && h.chip_id == ESP_CHIP_ID_ESP32P4;
    /* the app description is the start of the first segment */
    ok = ok && fseek(f, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), SEEK_SET) == 0 &&
         fread(d, sizeof(*d), 1, f) == 1 && d->magic_word == ESP_APP_DESC_MAGIC_WORD;
    fclose(f);
    if (ok && size) *size = (uint32_t)st.st_size;
    return ok;
}

/* a build's ID: the start of its ELF file's SHA-256 (as a crash log prints it). (The app description's
 * date is when the Arduino core's libraries were built: the same for every sketch build.) */
static void fw_build_id(const esp_app_desc_t *d, char out[9])
{
    for (int i = 0; i < 4; i++) snprintf(out + 2 * i, 3, "%02x", d->app_elf_sha256[i]);
}

/* the Firmware update row: what's running, and what update.bin is */
static void fw_row_update(void)
{
    if (!st_fw_hint) return;
    char run_id[9], new_id[9];
    fw_build_id(esp_app_get_description(), run_id);
    esp_app_desc_t d;
    uint32_t size = 0;
    bool have = stats_db_ready() && fw_file_desc(&d, &size);   /* (the card is mounted with sudoku.db open) */
    if (have) {
        fw_build_id(&d, new_id);
        lv_label_set_text_fmt(st_fw_hint, "update.bin: build %s, %u KB%s", new_id, (unsigned)(size / 1024),
                              strcmp(new_id, run_id) ? "" : " (the one running)");
    } else {
        lv_label_set_text_fmt(st_fw_hint, "Running build %s; put update.bin on the card", run_id);
    }
    lv_obj_set_state(st_fw_btn, LV_STATE_DISABLED, !have);
}

static void fw_task(void *arg)
{
    uint8_t *buf = heap_caps_malloc(FW_CHUNK, MALLOC_CAP_SPIRAM);
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    FILE *f = fopen(FW_FILE, "rb");
    esp_ota_handle_t h = 0;
    esp_err_t err = !buf ? ESP_ERR_NO_MEM : !part ? ESP_ERR_NOT_FOUND : !f ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (err == ESP_OK) err = esp_ota_begin(part, fw_size, &h);   /* erases what the image needs */
    while (err == ESP_OK) {
        size_t n = fread(buf, 1, FW_CHUNK, f);
        if (!n) break;
        err = esp_ota_write(h, buf, n);
        fw_done_bytes += (uint32_t)n;
    }
    if (f) fclose(f);
    if (h) {
        esp_err_t end = esp_ota_end(h);             /* the whole image checked (format, chip, hash) */
        if (err == ESP_OK) err = end;
    }
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
    if (err == ESP_OK) {
        remove(FW_DONE);
        rename(FW_FILE, FW_DONE);                   /* not again at the next look */
        printf("update: %u bytes into %s, starting it\n", (unsigned)fw_done_bytes, part->label);
    } else {
        snprintf(fw_err, sizeof(fw_err), "Update failed (%s): the running firmware stays.", esp_err_to_name(err));
        printf("update: %s\n", fw_err);
    }
    heap_caps_free(buf);
    fw_state = err == ESP_OK ? 1 : 2;
    vTaskDelete(NULL);
}

static void fw_poll_cb(lv_timer_t *t)
{
    if (fw_size) lv_bar_set_value(fw_bar, (int32_t)((uint64_t)fw_done_bytes * 100 / fw_size), LV_ANIM_OFF);
    lv_label_set_text_fmt(fw_lbl, "%u of %u KB", (unsigned)(fw_done_bytes / 1024), (unsigned)(fw_size / 1024));
    if (fw_state == 0) return;
    lv_timer_delete(fw_timer);
    fw_timer = NULL;
    if (fw_state == 2) {
        lv_obj_delete(fw_ovl);
        fw_ovl = NULL;
        sudoku_ui_notice(fw_err, 6000);
        fw_row_update();
        return;
    }
    lv_label_set_text(fw_lbl, "Done: restarting into the new firmware...");
    lv_refr_now(NULL);
    stats_db_suspend();                             /* sudoku.db closed, the card unmounted */
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static void fw_go_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    esp_app_desc_t d;
    uint32_t size;
    if (fw_timer || !fw_file_desc(&d, &size)) return;
    fw_size = size;
    fw_done_bytes = 0;
    fw_state = 0;
    fw_ovl = lv_obj_create(lv_layer_top());          /* nothing else while it writes */
    lv_obj_remove_style_all(fw_ovl);
    lv_obj_set_size(fw_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(fw_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fw_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(fw_ovl, true);
    lv_obj_t *card = lv_obj_create(fw_ovl);
    lv_obj_set_size(card, 520, 200);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(tl, LV_SYMBOL_DOWNLOAD "  Updating the firmware");
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 6);
    fw_bar = lv_bar_create(card);
    lv_obj_set_size(fw_bar, 460, 16);
    lv_bar_set_range(fw_bar, 0, 100);
    lv_obj_set_style_bg_color(fw_bar, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fw_bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(fw_bar, LV_ALIGN_TOP_MID, 0, 60);
    fw_lbl = make_label(card, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(fw_lbl, "Starting...");
    lv_obj_align(fw_lbl, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_t *w = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(w, "Don't switch off or take the card out.");
    lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 126);
    if (xTaskCreatePinnedToCore(fw_task, "fwupdate", 8192, NULL, 3, NULL, WORKER_CORE) != pdPASS) {
        lv_obj_delete(fw_ovl);
        fw_ovl = NULL;
        return;
    }
    fw_timer = lv_timer_create(fw_poll_cb, 200, NULL);
}

static void fw_update_cb(lv_event_t *e)
{
    esp_app_desc_t d;
    uint32_t size;
    if (!fw_file_desc(&d, &size)) {
        sudoku_ui_notice("No usable update.bin on the card.", 3000);
        fw_row_update();
        return;
    }
    char run_id[9], new_id[9];
    fw_build_id(esp_app_get_description(), run_id);
    fw_build_id(&d, new_id);
    char text[360];
    snprintf(text, sizeof(text),
             "Running: build %s.\nupdate.bin: build %s, %u KB%s.\n\nIt is written to the board's other "
             "firmware slot and checked before the board restarts into it. If the new firmware doesn't start up "
             "properly, the board goes back to this one by itself.",
             run_id, new_id, (unsigned)(size / 1024), strcmp(new_id, run_id) ? "" : " (the same build)");
    lv_obj_t *m = make_msgbox("Update the firmware?", text);
    lv_obj_set_width(m, 600);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_DOWNLOAD "  Update", fw_go_cb, true);
}

void sudoku_ui_mark_ok(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        printf("update: the new firmware started fine: kept\n");
    }
}

static void timer_sw_cb(lv_event_t *e)
{
    store_set_show_timer(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}

static void import_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("Importing needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Importing (each puzzle is checked for a single solution)...", 3000);
    stats_db_import_puzzles();                      /* the result comes back as a message */
}

static void export_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("Export needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Exporting...", 1500);
    stats_db_export_csv();                          /* the result comes back as a message */
}

static void accent_dd_cb(lv_event_t *e)
{
    store_set_accent((uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e)));
    apply_theme(s_dark);                            /* every screen again, in the new colour */
}

static void assist_dd_cb(lv_event_t *e)
{
    store_set_assist((uint8_t)lv_dropdown_get_selected(st_assist_dd));   /* the game redraws when shown */
}

/* a dropdown on the right of a settings row, styled like the others */
static lv_obj_t *settings_dropdown(lv_obj_t *row, const char *options, uint32_t sel, int width, lv_event_cb_t cb)
{
    lv_obj_t *dd = lv_dropdown_create(row);
    lv_obj_set_size(dd, width, 52);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options_static(dd, options);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(dd, C_TEXT, 0);
    lv_obj_set_style_bg_color(dd, C_CARD2, 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_shadow_width(dd, 0, 0);
    lv_obj_set_style_radius(dd, 8, 0);
    lv_obj_set_style_pad_hor(dd, 16, 0);
    lv_obj_set_style_pad_ver(dd, 15, 0);
    lv_obj_t *dl = lv_dropdown_get_list(dd);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(dl, C_TEXT, 0);
    lv_obj_set_style_text_line_space(dl, 22, 0);   /* finger-sized rows */
    lv_obj_set_style_bg_color(dl, C_CARD2, 0);
    lv_obj_set_style_border_width(dl, 0, 0);
    lv_obj_set_style_radius(dl, 8, 0);
    lv_obj_set_style_max_height(dl, 480, 0);
    lv_obj_set_style_bg_color(dl, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(dl, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_dropdown_set_selected(dd, sel);
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return dd;
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
    if (xTaskCreatePinnedToCore(sd_task, "sd_card", 8192, (void *)(intptr_t)job, 4, NULL, WORKER_CORE) != pdPASS) {
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
    static stats_view_t *view;                             /* PSRAM */
    if (!view) view = heap_caps_malloc(sizeof(*view), MALLOC_CAP_SPIRAM);
    if (view && stats_db_take_view(view) && view->level == x_sel - 1) {
        x_view = *view;
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
    if (stats_db_take_imports_changed()) {                 /* new collections: Puzzles' column and pane again */
        list_fill_colls();                                 /* (reloads them; not a rebuild of every screen:
                                                              two copies of them all left internal RAM to the
                                                              SD card's DMA buffers short, and a Files rescan
                                                              then failed) */
        refresh_list();
        update_header();                                   /* (the game screen's Save icon) */
    }
    trophy_poll();
    trophy_toast_poll();
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
        if (lv_screen_active() == scr_trophy) tr_ask();          /* now this player's */
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
    gibberlink_set_enabled(store_gibberlink());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(effective_brightness());
    tz_apply(store_timezone());
    s_sleep_min = store_screen_timeout_min();
    l_coll = store_last_collection();
    l_tab = coll_tab(l_coll);
    s_night = false;                                   /* (re-checked just below, from these settings) */
    apply_theme(store_dark_mode());                    /* redraws every screen with it */
    night_check(NULL);
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
        lv_obj_t *wr = lv_obj_create(card);            /* who: the avatar (or colour dot), name, puzzle */
        lv_obj_remove_style_all(wr);
        lv_obj_set_size(wr, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(wr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(wr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(wr, 8, 0);
        lv_obj_set_clickable(wr, false);
        player_badge(wr, player_find(0, r->player), -1, 28, 10);
        lv_obj_t *who = make_label(wr, &lv_font_montserrat_18, C_TEXT);
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
        if (r->flags & GAME_FLAG_NO_NOTES) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   no notes");
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
        lv_obj_align(player_badge(card, player_find(0, r->player), -1, AVATAR_SIZE, 12), LV_ALIGN_LEFT_MID, -4, 0);
        lv_obj_t *who = make_label(card, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(who, "%s   -   %s", r->player, name);
        lv_obj_align(who, LV_ALIGN_LEFT_MID, 44, -13);
        lv_obj_t *what = make_label(card, &lv_font_montserrat_14, C_MUTED);
        snprintf(line, sizeof(line), "%s   -   %s   -   %u mistake%s", when, took,
                 (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s");
        if (r->hints) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   %u hint%s", (unsigned)r->hints, r->hints == 1 ? "" : "s");
        }
        if (r->flags & GAME_FLAG_NO_NOTES) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   no notes");
        }
        lv_label_set_text(what, line);
        lv_obj_align(what, LV_ALIGN_LEFT_MID, 44, 14);
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

/* ============================================================= trophies */
/*
 * Trophies (home screen), four tabs:
 *   Achievements: the current player's list (stats_db.c): unlocked ones with
 *     a trophy and the date, the others with a lock and their progress.
 *   Daily: the leaderboard of one day's daily puzzle, today first (< > for
 *     other days): everyone's best time, fastest first, then who hasn't
 *     solved it.
 *   Weekly: a week's daily puzzles added up per player: days solved, then
 *     total time (< > for other weeks).
 *   Records: the current player's bests: fastest solves (overall, clean, and
 *     per kind), most solves in a day, clean solves in a row, longest daily
 *     streak, puzzles solved, time spent.
 * Achievements a solve unlocks pop up at the top of the screen (trophy_toast).
 */
#define TR_TABS 4                         /* Achievements, Daily, Weekly, Records */
#define TR_WEEK 2
#define TR_RECORDS 3
#define TR_TAB_Y  72                      /* the tab row, under the title */
#define TR_TAB_H  52
#define TR_VIEW_Y (TR_TAB_Y + TR_TAB_H + 12)   /* the open tab's content */
static lv_obj_t *tr_tab[TR_TABS], *tr_view[TR_TABS];
static lv_obj_t *tr_ach_count, *tr_ach_list, *tr_rec_list;
static lv_obj_t *tr_week_lbl, *tr_week_prev, *tr_week_next, *tr_week_list;
static char tr_monday[12];                /* week on the Weekly tab, "" = this week */
static lv_obj_t *tr_day_lbl, *tr_day_prev, *tr_day_next, *tr_board_list, *tr_play_btn;
static int tr_tab_sel;
static char tr_day[12];                   /* day on the leaderboard, "" = today */
static lv_obj_t *h_trophy_sub;            /* home button line: "7 of 22 achievements" */
static void notice_del_cb(lv_timer_t *t);

static void tr_hint(lv_obj_t *list, const char *text)
{
    lv_obj_clean(list);
    lv_obj_t *l = make_label(list, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_top(l, 20, 0);
    lv_label_set_text(l, text);
}

static lv_obj_t *tr_card(lv_obj_t *list, int h)
{
    lv_obj_t *card = lv_obj_create(list);
    lv_obj_set_size(card, LV_PCT(100), h);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_hor(card, 16, 0);
    lv_obj_set_style_pad_ver(card, 0, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, false);
    return card;
}

/* "3 / 10", "2 h 5 min / 10 h", "4 / 7 days" */
static void ach_progress_text(const stats_ach_t *a, char *buf, size_t n)
{
    switch (a->unit) {
    case ACH_UNIT_MINUTES:
        if (a->value >= 60) snprintf(buf, n, "%ld h %ld min / %ld h", (long)(a->value / 60), (long)(a->value % 60),
                                     (long)(a->goal / 60));
        else snprintf(buf, n, "%ld min / %ld h", (long)a->value, (long)(a->goal / 60));
        break;
    case ACH_UNIT_DAYS:
        snprintf(buf, n, "%ld / %ld days", (long)a->value, (long)a->goal);
        break;
    default:
        snprintf(buf, n, "%ld / %ld", (long)a->value, (long)a->goal);
        break;
    }
}

static void ach_card(const stats_ach_t *a)
{
    lv_obj_t *card = tr_card(tr_ach_list, 80);
    lv_obj_t *icon = make_label(card, &font_icons_24, a->unlocked ? C_ORANGE : C_MUTED);
    lv_label_set_text(icon, a->unlocked ? SYMBOL_TROPHY : SYMBOL_LOCK);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_20, a->unlocked ? C_TEXT : C_MUTED);
    lv_label_set_text(t, a->title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 44, -13);
    lv_obj_t *d = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(d, SCR_W - 24 - 32 - 44 - 190);
    lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(d, a->desc);
    lv_obj_align(d, LV_ALIGN_LEFT_MID, 44, 14);

    lv_obj_t *r = make_label(card, &lv_font_montserrat_16, a->unlocked ? C_GREEN : C_MUTED);
    lv_obj_set_width(r, 176);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_RIGHT, 0);
    if (a->unlocked) {
        char when[24] = "Unlocked";
        if (a->unlocked_at) {
            time_t tt = (time_t)a->unlocked_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%d %b %Y", &tm);
        }
        lv_label_set_text(r, when);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (a->goal > 1) {
        char prog[32];
        ach_progress_text(a, prog, sizeof(prog));
        lv_label_set_text(r, prog);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, -12);
        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_size(bar, 150, 10);
        lv_bar_set_range(bar, 0, a->goal);
        lv_bar_set_value(bar, a->value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, C_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, C_ACCENT, LV_PART_INDICATOR);
        lv_obj_align(bar, LV_ALIGN_RIGHT_MID, 0, 14);
    } else {
        lv_label_set_text(r, "Not yet");
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void render_achievements(const stats_achievements_t *a)
{
    lv_label_set_text_fmt(tr_ach_count, "%d of %d unlocked", a->unlocked, a->n);
    lv_obj_clean(tr_ach_list);
    for (int pass = 0; pass < 2; pass++) {             /* unlocked first, then the rest */
        for (int i = 0; i < a->n; i++) {
            if (a->a[i].unlocked == (pass == 0)) ach_card(&a->a[i]);
        }
    }
    lv_obj_scroll_to_y(tr_ach_list, 0, LV_ANIM_OFF);
}

/* day moved by delta days ('YYYY-MM-DD', in place) */
static void day_shift(char *day, int delta)
{
    struct tm tm = {0};
    if (sscanf(day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_mday += delta;
    tm.tm_hour = 12;                                    /* clear of DST changes */
    mktime(&tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
}

static bool tr_today(char day[12])
{
    int level;
    return daily_today(day, &level, NULL);
}

static void render_board(const stats_board_t *b)
{
    char today[12] = "";
    bool clock = tr_today(today);
    bool is_today = clock && !strcmp(b->day, today);
    struct tm tm = {0};
    char date[32] = "";
    if (sscanf(b->day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_hour = 12;
        mktime(&tm);                                     /* fills in the weekday */
        strftime(date, sizeof(date), "%a %d %b %Y", &tm);
    }
    if (b->exists && b->level >= 0 && b->level < GEN_LEVELS) {
        char kind[32];
        daily_kind_text(b->variant, b->level, kind, sizeof(kind));
        lv_label_set_text_fmt(tr_day_lbl, "%s%s  -  %s", is_today ? "Today, " : "", date, kind);
    } else {
        lv_label_set_text_fmt(tr_day_lbl, "%s%s", is_today ? "Today, " : "", date);
    }
    lv_obj_set_state(tr_day_next, LV_STATE_DISABLED, is_today);
    bool me_solved = false;
    for (int i = 0; i < b->n; i++) me_solved |= b->rows[i].player_id == store_player_id() && b->rows[i].solved;
    lv_obj_set_hidden(tr_play_btn, !is_today || me_solved);

    if (!b->exists || b->solved == 0) {
        tr_hint(tr_board_list, !b->exists ? (is_today ? "Nobody has played today's puzzle yet. Be the first!"
                                                      : "Nobody played the daily puzzle on this day.")
                                          : (is_today ? "Nobody has solved today's puzzle yet. Be the first!"
                                                      : "Nobody solved this day's puzzle."));
        return;
    }
    lv_obj_clean(tr_board_list);
    int rank = 0;
    for (int i = 0; i < b->n; i++) {
        const stats_board_row_t *r = &b->rows[i];
        bool me = r->player_id == store_player_id();
        lv_obj_t *card = tr_card(tr_board_list, r->solved ? 76 : 56);
        if (me) {
            lv_obj_set_style_border_color(card, C_ACCENT, 0);
            lv_obj_set_style_border_width(card, 2, 0);
        }
        if (!r->solved) {
            lv_obj_align(player_badge(card, player_find(r->player_id, r->player), r->color, 32, 12),
                         LV_ALIGN_LEFT_MID, 50, 0);
            lv_obj_t *n = make_label(card, &lv_font_montserrat_18, C_MUTED);
            lv_label_set_text_fmt(n, "%s   -   not solved yet", r->player);
            lv_obj_align(n, LV_ALIGN_LEFT_MID, 96, 0);
            continue;
        }
        rank++;
        lv_obj_t *pos = make_label(card, rank == 1 ? &font_icons_24 : &lv_font_montserrat_24,
                                   rank == 1 ? C_ORANGE : C_MUTED);
        if (rank == 1) lv_label_set_text(pos, SYMBOL_STAR);
        else lv_label_set_text_fmt(pos, "%d", rank);
        lv_obj_set_width(pos, 40);
        lv_obj_set_style_text_align(pos, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(pos, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(player_badge(card, player_find(r->player_id, r->player), r->color, AVATAR_SIZE, 12),
                     LV_ALIGN_LEFT_MID, 48, 0);         /* their avatar (or colour dot) */
        lv_obj_t *n = make_label(card, &lv_font_montserrat_20, C_TEXT);
        lv_label_set_text_fmt(n, "%s%s", r->player, me ? "  (you)" : "");
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 96, -13);
        char line[96];
        snprintf(line, sizeof(line), "%d%s   -   %u mistake%s   -   %u hint%s", rank, ordinal(rank),
                 (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s", (unsigned)r->hints, r->hints == 1 ? "" : "s");
        if (r->flags & GAME_FLAG_NO_NOTES) {        /* the no-notes challenge: its badge */
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   no notes");
        }
        if (r->solves > 1) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   solved %u times", (unsigned)r->solves);
        }
        lv_obj_t *sub = make_label(card, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(sub, line);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 96, 14);
        char t[16];
        fmt_time(t, sizeof(t), r->seconds);
        lv_obj_t *tl = make_label(card, &font_timer_36, rank == 1 ? C_ORANGE : C_TEXT);
        lv_label_set_text(tl, t);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_scroll_to_y(tr_board_list, 0, LV_ANIM_OFF);
}

/* Monday of this week ('YYYY-MM-DD'); false while the clock isn't set */
static bool this_monday(char day[12])
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) return false;
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_mday -= (tm.tm_wday + 6) % 7;
    tm.tm_hour = 12;
    mktime(&tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    return true;
}

static void render_week(const stats_week_t *w)
{
    char now[12] = "", date[24] = "";
    bool clock = this_monday(now);
    struct tm tm = {0};
    if (sscanf(w->monday, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_hour = 12;
        mktime(&tm);
        strftime(date, sizeof(date), "%d %b %Y", &tm);
    }
    bool current = clock && !strcmp(w->monday, now);
    lv_label_set_text_fmt(tr_week_lbl, "%s %s", current ? "This week, from" : "Week of", date);
    lv_obj_set_state(tr_week_next, LV_STATE_DISABLED, current);
    lv_obj_clean(tr_week_list);
    int shown = 0, rank = 0;
    for (int i = 0; i < w->n; i++) {
        const stats_week_row_t *r = &w->rows[i];
        if (!r->days) continue;
        shown++;
        rank++;
        bool me = r->player_id == store_player_id();
        lv_obj_t *card = tr_card(tr_week_list, 84);
        if (me) {
            lv_obj_set_style_border_color(card, C_ACCENT, 0);
            lv_obj_set_style_border_width(card, 2, 0);
        }
        lv_obj_t *pos = make_label(card, rank == 1 ? &font_icons_24 : &lv_font_montserrat_24, rank == 1 ? C_ORANGE : C_MUTED);
        if (rank == 1) lv_label_set_text(pos, SYMBOL_STAR);
        else lv_label_set_text_fmt(pos, "%d", rank);
        lv_obj_set_width(pos, 40);
        lv_obj_set_style_text_align(pos, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(pos, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(player_badge(card, player_find(r->player_id, r->player), r->color, AVATAR_SIZE, 12),
                     LV_ALIGN_LEFT_MID, 48, 0);         /* their avatar (or colour dot) */
        lv_obj_t *n = make_label(card, &lv_font_montserrat_20, C_TEXT);
        lv_label_set_text_fmt(n, "%s%s   -   %d of 7 days", r->player, me ? "  (you)" : "", r->days);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 96, -16);
        static const char *const DAYS[7] = {"M", "T", "W", "T", "F", "S", "S"};
        for (int d = 0; d < 7; d++) {               /* which days: a square each, green if solved */
            lv_obj_t *sq = lv_obj_create(card);
            lv_obj_remove_style_all(sq);
            lv_obj_set_size(sq, 26, 22);
            lv_obj_set_style_radius(sq, 5, 0);
            lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(sq, r->best[d] ? C_GREEN : C_CARD2, 0);
            lv_obj_align(sq, LV_ALIGN_LEFT_MID, 96 + d * 30, 16);
            lv_obj_t *dl = make_label(sq, &lv_font_montserrat_12, r->best[d] ? lv_color_white() : C_MUTED);
            lv_label_set_text(dl, DAYS[d]);
            lv_obj_center(dl);
        }
        char t[16];
        fmt_time(t, sizeof(t), r->total);
        lv_obj_t *tl = make_label(card, &font_timer_36, rank == 1 ? C_ORANGE : C_TEXT);
        lv_label_set_text(tl, t);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    if (!shown) tr_hint(tr_week_list, current ? "No daily puzzle solved this week yet." : "No daily puzzle was solved that week.");
    lv_obj_scroll_to_y(tr_week_list, 0, LV_ANIM_OFF);
}

static void tr_week_cb(lv_event_t *e)
{
    char now[12];
    if (!this_monday(now)) return;
    if (!tr_monday[0]) snprintf(tr_monday, sizeof(tr_monday), "%s", now);
    day_shift(tr_monday, 7 * (int)(intptr_t)lv_event_get_user_data(e));
    if (strcmp(tr_monday, now) > 0) snprintf(tr_monday, sizeof(tr_monday), "%s", now);
    stats_db_query_week(tr_monday);
}

static void render_records(const stats_records_t *R)
{
    lv_obj_clean(tr_rec_list);
    for (int i = 0; i < R->n; i++) {
        const stats_record_t *r = &R->r[i];
        lv_obj_t *card = tr_card(tr_rec_list, 72);
        lv_obj_t *t = make_label(card, &lv_font_montserrat_18, r->has ? C_TEXT : C_MUTED);
        lv_label_set_text(t, r->title);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, -12);
        /* what it was set on, and when */
        char detail[96] = "", when[24] = "";
        if (r->when) {
            time_t tt = (time_t)r->when;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%d %b %Y", &tm);
        }
        if (!r->has) snprintf(detail, sizeof(detail), "Not yet");
        else if (r->puzzle != STORE_NO_GAME) {
            char name[40];
            puzzle_name(r->puzzle, name, sizeof(name));
            snprintf(detail, sizeof(detail), "%s%s%s", name, when[0] ? "   -   " : "", when);
        } else if (when[0]) {
            snprintf(detail, sizeof(detail), "%s", when);
        }
        lv_obj_t *d = make_label(card, &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_width(d, SCR_W - 24 - 32 - 190);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(d, detail);
        lv_obj_align(d, LV_ALIGN_LEFT_MID, 0, 13);
        char v[24] = "-";
        if (r->has) {
            switch (r->unit) {
            case REC_TIME: fmt_time(v, sizeof(v), (uint32_t)r->value); break;
            case REC_DAYS: snprintf(v, sizeof(v), "%ld day%s", (long)r->value, r->value == 1 ? "" : "s"); break;
            case REC_MINUTES:
                if (r->value >= 60) snprintf(v, sizeof(v), "%ld h %02ld min", (long)(r->value / 60), (long)(r->value % 60));
                else snprintf(v, sizeof(v), "%ld min", (long)r->value);
                break;
            default: snprintf(v, sizeof(v), "%ld", (long)r->value); break;
            }
        }
        lv_obj_t *vl = make_label(card, r->unit == REC_TIME && r->has ? &font_timer_36 : &lv_font_montserrat_24,
                                  r->has ? (i == 0 ? C_ORANGE : C_TEXT) : C_MUTED);
        lv_label_set_text(vl, v);
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_scroll_to_y(tr_rec_list, 0, LV_ANIM_OFF);
}

/* asks sudoku.db for what the open tab shows */
static void tr_ask(void)
{
    if (!stats_db_ready()) {
        tr_hint(tr_rec_list, "Records are worked out from the solves in sudoku.db: insert a FAT32 microSD card "
                             "before switching on.");
        tr_hint(tr_week_list, "The weekly challenge needs sudoku.db on a microSD card.");
        lv_label_set_text(tr_ach_count, "");
        tr_hint(tr_ach_list, "Achievements are worked out from the solves in sudoku.db: insert a FAT32 microSD "
                             "card before switching on.");
        tr_hint(tr_board_list, "The leaderboard needs sudoku.db on a microSD card.");
        lv_label_set_text(tr_day_lbl, "");
        lv_obj_set_hidden(tr_play_btn, true);
        return;
    }
    if (tr_tab_sel == 0) {
        stats_db_query_achievements();
        return;
    }
    if (tr_tab_sel == TR_RECORDS) {
        stats_db_query_records();
        return;
    }
    if (tr_tab_sel == TR_WEEK) {
        char now[12];
        if (!this_monday(now)) {
            lv_label_set_text(tr_week_lbl, "");
            tr_hint(tr_week_list, "The weekly challenge needs today's date: connect Wi-Fi (Settings) so the clock "
                                  "is set.");
            return;
        }
        if (!tr_monday[0] || strcmp(tr_monday, now) > 0) snprintf(tr_monday, sizeof(tr_monday), "%s", now);
        stats_db_query_week(tr_monday);
        return;
    }
    char today[12];
    if (!tr_today(today)) {
        lv_label_set_text(tr_day_lbl, "");
        lv_obj_set_state(tr_day_next, LV_STATE_DISABLED, true);
        lv_obj_set_state(tr_day_prev, LV_STATE_DISABLED, true);
        lv_obj_set_hidden(tr_play_btn, true);
        tr_hint(tr_board_list, "The daily leaderboard needs today's date: connect Wi-Fi (Settings) so the "
                               "clock is set.");
        return;
    }
    lv_obj_set_state(tr_day_prev, LV_STATE_DISABLED, false);
    if (!tr_day[0] || strcmp(tr_day, today) > 0) snprintf(tr_day, sizeof(tr_day), "%s", today);
    stats_db_query_board(tr_day);
}

static void tr_select_tab(int tab)
{
    tr_tab_sel = tab;
    for (int i = 0; i < TR_TABS; i++) {
        lv_obj_set_state(tr_tab[i], LV_STATE_CHECKED, i == tab);
        lv_obj_set_style_text_color(lv_obj_get_child(tr_tab[i], 0), i == tab ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(tr_view[i], i != tab);
    }
    tr_ask();
}

static void tr_tab_cb(lv_event_t *e)
{
    tr_select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

static void tr_day_cb(lv_event_t *e)
{
    char today[12];
    if (!tr_today(today)) return;
    if (!tr_day[0]) snprintf(tr_day, sizeof(tr_day), "%s", today);
    day_shift(tr_day, (int)(intptr_t)lv_event_get_user_data(e));
    if (strcmp(tr_day, today) > 0) snprintf(tr_day, sizeof(tr_day), "%s", today);
    stats_db_query_board(tr_day);
}

static void trophy_cb(lv_event_t *e)
{
    tr_day[0] = 0;                                       /* the leaderboard opens on today */
    tr_monday[0] = 0;                                    /* and the weekly one on this week */
    tr_select_tab(tr_tab_sel);
    lv_screen_load(scr_trophy);
}

/* sudoku.db answers for the Trophies screen and the home button line */
static void trophy_poll(void)
{
    static stats_achievements_t *a;                      /* PSRAM: internal RAM is tight */
    static stats_board_t *b;
    if (!a) a = heap_caps_malloc(sizeof(*a), MALLOC_CAP_SPIRAM);
    if (!b) b = heap_caps_malloc(sizeof(*b), MALLOC_CAP_SPIRAM);
    if (!a || !b) return;
    if (stats_db_take_achievements(a)) {
        if (h_trophy_sub) lv_label_set_text_fmt(h_trophy_sub, "%d of %d achievements", a->unlocked, a->n);
        if (scr_trophy && a->player_id == store_player_id()) render_achievements(a);
    }
    if (stats_db_take_board(b) && scr_trophy && !strcmp(b->day, tr_day)) render_board(b);
    static stats_records_t *r;
    if (!r) r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM);
    if (r && stats_db_take_records(r) && scr_trophy && r->player_id == store_player_id()) render_records(r);
    static stats_week_t *w;
    if (!w) w = heap_caps_malloc(sizeof(*w), MALLOC_CAP_SPIRAM);
    if (w && stats_db_take_week(w) && scr_trophy && !strcmp(w->monday, tr_monday)) render_week(w);
}

static void build_trophies(void)
{
    scr_trophy = make_screen();
    make_btn(scr_trophy, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_trophy, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Trophies");
    lv_obj_set_pos(t, 150, 20);
    static const char *const TABS[TR_TABS] = {SYMBOL_TROPHY "  Achievements", SYMBOL_STAR "  Daily",
                                              SYMBOL_CALENDAR "  Weekly", SYMBOL_CHART "  Records"};
    /* the tabs: a row across the page under the title, each as wide as its label needs
     * with room either side (Achievements is the long one); 232 + 136 + 154 + 150 + 3 x 8 = 696 */
    static const int TAB_W[TR_TABS] = {232, 136, 154, 150};
    const int tab_gap = 8;
    for (int i = 0, x = 12; i < TR_TABS; x += TAB_W[i++] + tab_gap) {
        tr_tab[i] = make_btn(scr_trophy, x, TR_TAB_Y, TAB_W[i], TR_TAB_H, TABS[i], &font_icons_20, tr_tab_cb,
                             (void *)(intptr_t)i);
    }

    /* achievements */
    lv_obj_t *av = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(av);
    lv_obj_set_pos(av, 12, TR_VIEW_Y);
    lv_obj_set_size(av, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[0] = av;
    tr_ach_count = make_label(av, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_pos(tr_ach_count, 4, 4);
    tr_ach_list = lv_obj_create(av);
    lv_obj_remove_style_all(tr_ach_list);
    lv_obj_set_pos(tr_ach_list, 0, 36);
    lv_obj_set_size(tr_ach_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12 - 36);
    lv_obj_set_flex_flow(tr_ach_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_ach_list, 8, 0);
    lv_obj_set_scroll_dir(tr_ach_list, LV_DIR_VER);

    /* daily leaderboard: < day > and the list */
    lv_obj_t *bv = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(bv);
    lv_obj_set_pos(bv, 12, TR_VIEW_Y);
    lv_obj_set_size(bv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[1] = bv;
    tr_day_prev = make_btn(bv, 0, 0, 64, 52, LV_SYMBOL_LEFT, &lv_font_montserrat_22, tr_day_cb, (void *)(intptr_t)-1);
    tr_day_next = make_btn(bv, SCR_W - 24 - 64, 0, 64, 52, LV_SYMBOL_RIGHT, &lv_font_montserrat_22, tr_day_cb,
                           (void *)(intptr_t)1);
    lv_obj_set_style_text_opa(lv_obj_get_child(tr_day_next, 0), LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(lv_obj_get_child(tr_day_prev, 0), LV_OPA_30, LV_STATE_DISABLED);
    tr_day_lbl = make_label(bv, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(tr_day_lbl, SCR_W - 24 - 2 * 72);
    lv_obj_set_style_text_align(tr_day_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tr_day_lbl, LV_ALIGN_TOP_MID, 0, 14);
    tr_board_list = lv_obj_create(bv);
    lv_obj_remove_style_all(tr_board_list);
    lv_obj_set_pos(tr_board_list, 0, 64);
    lv_obj_set_size(tr_board_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12 - 64 - 64);
    lv_obj_set_flex_flow(tr_board_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_board_list, 8, 0);
    lv_obj_set_scroll_dir(tr_board_list, LV_DIR_VER);
    tr_play_btn = make_btn(bv, 0, 0, 300, 52, LV_SYMBOL_BELL "  Play today's puzzle", &lv_font_montserrat_18,
                           daily_cb, NULL);
    lv_obj_set_style_bg_color(tr_play_btn, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(tr_play_btn, 0), lv_color_white(), 0);
    lv_obj_align(tr_play_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_hidden(tr_play_btn, true);

    /* weekly: < week > and the list */
    lv_obj_t *wv = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(wv);
    lv_obj_set_pos(wv, 12, TR_VIEW_Y);
    lv_obj_set_size(wv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[TR_WEEK] = wv;
    tr_week_prev = make_btn(wv, 0, 0, 64, 52, LV_SYMBOL_LEFT, &lv_font_montserrat_22, tr_week_cb, (void *)(intptr_t)-1);
    tr_week_next = make_btn(wv, SCR_W - 24 - 64, 0, 64, 52, LV_SYMBOL_RIGHT, &lv_font_montserrat_22, tr_week_cb,
                            (void *)(intptr_t)1);
    lv_obj_set_style_text_opa(lv_obj_get_child(tr_week_next, 0), LV_OPA_30, LV_STATE_DISABLED);
    tr_week_lbl = make_label(wv, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(tr_week_lbl, SCR_W - 24 - 2 * 72);
    lv_obj_set_style_text_align(tr_week_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tr_week_lbl, LV_ALIGN_TOP_MID, 0, 14);
    tr_week_list = lv_obj_create(wv);
    lv_obj_remove_style_all(tr_week_list);
    lv_obj_set_pos(tr_week_list, 0, 64);
    lv_obj_set_size(tr_week_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12 - 64);
    lv_obj_set_flex_flow(tr_week_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_week_list, 8, 0);
    lv_obj_set_scroll_dir(tr_week_list, LV_DIR_VER);
    tr_hint(tr_week_list, "Loading...");

    /* records */
    lv_obj_t *rv = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(rv);
    lv_obj_set_pos(rv, 12, TR_VIEW_Y);
    lv_obj_set_size(rv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[TR_RECORDS] = rv;
    tr_rec_list = lv_obj_create(rv);
    lv_obj_remove_style_all(tr_rec_list);
    lv_obj_set_size(tr_rec_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    lv_obj_set_flex_flow(tr_rec_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_rec_list, 8, 0);
    lv_obj_set_scroll_dir(tr_rec_list, LV_DIR_VER);
    tr_hint(tr_rec_list, "Loading...");

    for (int i = 0; i < TR_TABS; i++) {
        lv_obj_set_state(tr_tab[i], LV_STATE_CHECKED, i == tr_tab_sel);
        lv_obj_set_style_text_color(lv_obj_get_child(tr_tab[i], 0), i == tr_tab_sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(tr_view[i], i != tr_tab_sel);
    }
    lv_label_set_text(tr_ach_count, "");
    tr_hint(tr_ach_list, "Loading...");
    tr_hint(tr_board_list, "Loading...");
}

/* "Achievement unlocked" at the top of the screen, a moment after the Solved dialog */
static char s_toast_text[200];

static void trophy_toast_cb(lv_timer_t *t)
{
    lv_obj_t *n = lv_obj_create(lv_layer_top());
    lv_obj_set_size(n, SCR_W - 80, LV_SIZE_CONTENT);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(n, C_CARD, 0);
    lv_obj_set_style_border_color(n, C_ORANGE, 0);
    lv_obj_set_style_border_width(n, 2, 0);
    lv_obj_set_style_radius(n, 14, 0);
    lv_obj_set_style_pad_all(n, 14, 0);
    lv_obj_set_style_shadow_width(n, 24, 0);
    lv_obj_set_style_shadow_opa(n, LV_OPA_40, 0);
    lv_obj_set_scrollable(n, false);
    lv_obj_set_clickable(n, false);
    lv_obj_t *icon = make_label(n, &font_icons_24, C_ORANGE);
    lv_label_set_text(icon, SYMBOL_TROPHY);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *l = make_label(n, &lv_font_montserrat_18, C_TEXT);
    lv_obj_set_width(l, SCR_W - 80 - 28 - 52);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, s_toast_text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 48, 0);
    sfx_play(SFX_DIGIT_DONE);
    lv_timer_t *del = lv_timer_create(notice_del_cb, 5000, n);
    lv_timer_set_repeat_count(del, 1);
}

static void trophy_toast_poll(void)
{
    char titles[160];
    int n = 0;
    if (!stats_db_take_unlocked(titles, sizeof(titles), &n)) return;
    snprintf(s_toast_text, sizeof(s_toast_text), "%s unlocked:\n%s", n == 1 ? "Achievement" : "Achievements", titles);
    lv_timer_t *t = lv_timer_create(trophy_toast_cb, 1200, NULL);   /* after the fanfare starts */
    lv_timer_set_repeat_count(t, 1);
    if (h_trophy_sub && stats_db_ready()) stats_db_query_achievements();   /* the home line */
}

/* ------------------------------------------------------------- players */

/* name dialog (add or rename), on the top layer with an on-screen keyboard */
static lv_obj_t *nd_ovl, *nd_ta;
static int32_t nd_rename_id;             /* 0 = add a player */
static void (*nd_done)(const char *name); /* not a player's name: who takes it */

static void nd_close(void)
{
    if (!nd_ovl) return;
    lv_obj_delete_async(nd_ovl);
    nd_ovl = nd_ta = NULL;
}

static void nd_ok(void)
{
    const char *name = lv_textarea_get_text(nd_ta);
    if (nd_done) nd_done(name);
    else if (nd_rename_id) stats_db_rename_player(nd_rename_id, name);
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

/* the name dialog: a title, a text box with a keyboard, Cancel and ok_text; done gets the name
 * (NULL: a player's, added or renamed: nd_open) */
static void nd_open_ex(const char *title, const char *current, int max_len, const char *ok_text,
                       void (*done)(const char *name))
{
    nd_close();
    nd_done = done;
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
    lv_label_set_text(t, title);

    nd_ta = lv_textarea_create(panel);
    lv_obj_set_width(nd_ta, LV_PCT(100));
    lv_textarea_set_one_line(nd_ta, true);
    lv_textarea_set_max_length(nd_ta, (uint32_t)max_len);
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
    lv_obj_t *ok = make_btn(row, 0, 0, 150, 52, ok_text, &lv_font_montserrat_18,
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

static void nd_open(int32_t rename_id, const char *current)
{
    nd_open_ex(rename_id ? "Rename player" : "New player", current, STORE_NAME_LEN - 1, rename_id ? "Rename" : "Add",
               NULL);
    nd_rename_id = rename_id;
}

static void player_row_cb(lv_event_t *e)
{
    int32_t id = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (id && id != store_player_id()) stats_db_switch_player(id);
}

static void player_rename_cb(lv_event_t *e)
{
    if (stats_db_ready()) nd_open(store_player_id(), store_player_name());   /* who plays now */
}

static int32_t p_delete_id;

static void player_delete_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (p_delete_id) stats_db_delete_player(p_delete_id);
    p_delete_id = 0;
}

/* the menu's Delete: who plays now (another takes over); to delete someone else, switch first */
static void player_delete_cb(lv_event_t *e)
{
    int32_t id = store_player_id();
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

/* who plays now, in the list from sudoku.db (NULL: not there yet) */
static stats_player_t *player_current(void)
{
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == store_player_id()) return &p_players[i];
    }
    return NULL;
}

/* ---- the avatar picker: the menu's card turns into it. ~1900 emoji: one object draws
 * the rows in view (av_grid_draw_cb) and a tap is worked out from where it was; a row
 * of the groups' first emoji above it jumps to them. */
enum { AV_CELL = 56, AV_COLS = 8 };
static lv_obj_t *av_scroll, *av_grid, *av_group_btn[AVATAR_GROUPS];
static int av_cur;                                  /* the player's, 0 = none */
static int av_row0[AVATAR_GROUPS + 1];              /* each group starts a row: its first; [n] = all rows */

static int av_group_end(int g) { return g + 1 < AVATAR_GROUPS ? avatar_group_first[g + 1] : AVATAR_COUNT; }

static void av_rows_init(void)
{
    for (int g = 0, r = 0; g <= AVATAR_GROUPS; g++) {
        av_row0[g] = r;
        if (g < AVATAR_GROUPS) r += (av_group_end(g) - avatar_group_first[g] + AV_COLS - 1) / AV_COLS;
    }
}

/* the group of a row */
static int av_row_group(int row)
{
    int g = 0;
    while (g + 1 < AVATAR_GROUPS && av_row0[g + 1] <= row) g++;
    return g;
}

static void player_avatar_set(int k)
{
    stats_player_t *pl = player_current();
    ctx_menu_close();
    av_scroll = av_grid = NULL;
    if (!pl) return;
    stats_db_set_player_avatar(pl->id, k);           /* the list comes back with it */
    pl->avatar = (uint16_t)k;
    render_players();
}

static void av_none_cb(lv_event_t *e)
{
    player_avatar_set(0);
}

static void av_grid_click_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(av_grid, &a);
    int col = (p.x - a.x1) / AV_CELL, row = (p.y - a.y1) / AV_CELL;
    if (col < 0 || col >= AV_COLS || row < 0 || row >= av_row0[AVATAR_GROUPS]) return;
    int g = av_row_group(row), k = avatar_group_first[g] + (row - av_row0[g]) * AV_COLS + col;
    if (k < av_group_end(g)) player_avatar_set(k + 1);   /* (not the empty end of a group's last row) */
}

static void av_grid_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(av_grid, &a);
    int top = lv_obj_get_scroll_y(av_scroll), h = lv_obj_get_height(av_scroll);
    int r0 = top / AV_CELL - 1, r1 = (top + h) / AV_CELL + 1;   /* just the rows in view */
    if (r0 < 0) r0 = 0;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    for (int r = r0; r <= r1 && r < av_row0[AVATAR_GROUPS]; r++) {
        int g = av_row_group(r);
        for (int c = 0; c < AV_COLS; c++) {
            int k = avatar_group_first[g] + (r - av_row0[g]) * AV_COLS + c;
            if (k >= av_group_end(g)) break;
            lv_area_t cell = {a.x1 + c * AV_CELL, a.y1 + r * AV_CELL, a.x1 + (c + 1) * AV_CELL - 1,
                              a.y1 + (r + 1) * AV_CELL - 1};
            if (k + 1 == av_cur) {                   /* the one they have */
                lv_draw_rect_dsc_t rd;
                lv_draw_rect_dsc_init(&rd);
                rd.bg_opa = LV_OPA_TRANSP;
                rd.border_color = C_ACCENT;
                rd.border_width = 3;
                rd.radius = 12;
                lv_draw_rect(layer, &rd, &cell);
            }
            int o = (AV_CELL - AVATAR_SIZE) / 2;
            lv_area_t im = {cell.x1 + o, cell.y1 + o, cell.x1 + o + AVATAR_SIZE - 1, cell.y1 + o + AVATAR_SIZE - 1};
            d.src = &avatar_images[k];
            lv_draw_image(layer, &d, &im);
        }
    }
}

/* the group at the top of the view, checked in the row of groups */
static void av_scroll_cb(lv_event_t *e)
{
    int g = av_row_group((lv_obj_get_scroll_y(av_scroll) + AV_CELL / 2) / AV_CELL);
    for (int i = 0; i < AVATAR_GROUPS; i++) lv_obj_set_state(av_group_btn[i], LV_STATE_CHECKED, i == g);
}

static void av_group_cb(lv_event_t *e)
{
    int g = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_scroll_to_y(av_scroll, av_row0[g] * AV_CELL, LV_ANIM_ON);
}

/* the menu's Avatar, for who plays now */
static void player_avatar_cb(lv_event_t *e)
{
    stats_player_t *pl = player_current();
    if (!pl) return;
    av_cur = pl->avatar <= AVATAR_COUNT ? pl->avatar : 0;
    av_rows_init();
    const int inner = AV_COLS * AV_CELL, card_h = SCR_W - 68 - 16;
    lv_obj_t *card = ctx_card_open(p_menu_btn, inner + 2 * 6 + 2);
    lv_obj_set_height(card, card_h);

    /* the title and None */
    lv_obj_t *top = lv_obj_create(card);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, inner, 44);
    lv_obj_t *t = make_label(top, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text_fmt(t, "Avatar for %s", pl->name);
    lv_obj_set_width(t, inner - 120);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_t *none = make_btn(top, 0, 0, 104, 40, LV_SYMBOL_CLOSE "  None", &lv_font_montserrat_16, av_none_cb, NULL);
    lv_obj_set_style_bg_color(none, C_CARD2, 0);
    if (!av_cur) {
        lv_obj_set_style_border_color(none, C_ACCENT, 0);
        lv_obj_set_style_border_width(none, 2, 0);
    }
    lv_obj_align(none, LV_ALIGN_RIGHT_MID, 0, 0);

    /* the groups: each one's first emoji */
    lv_obj_t *gr = lv_obj_create(card);
    lv_obj_remove_style_all(gr);
    lv_obj_set_size(gr, inner, 48);
    lv_obj_set_flex_flow(gr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int g = 0; g < AVATAR_GROUPS; g++) {
        lv_obj_t *b = make_btn(gr, 0, 0, 46, 46, NULL, NULL, av_group_cb, (void *)(intptr_t)g);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_CHECKED);   /* (not the accent: the emoji's colours) */
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_t *im = lv_image_create(b);
        lv_image_set_src(im, &avatar_images[avatar_group_first[g]]);
        lv_obj_center(im);
        av_group_btn[g] = b;
    }
    lv_obj_t *line = lv_obj_create(card);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, inner, 1);
    lv_obj_set_style_bg_color(line, C_CARD2, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    /* the emoji */
    av_scroll = lv_obj_create(card);
    lv_obj_remove_style_all(av_scroll);
    lv_obj_set_width(av_scroll, inner);
    lv_obj_set_flex_grow(av_scroll, 1);
    lv_obj_set_scroll_dir(av_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(av_scroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(av_scroll, av_scroll_cb, LV_EVENT_SCROLL, NULL);
    av_grid = lv_obj_create(av_scroll);
    lv_obj_remove_style_all(av_grid);
    lv_obj_set_size(av_grid, inner, av_row0[AVATAR_GROUPS] * AV_CELL);
    lv_obj_set_scrollable(av_grid, false);
    lv_obj_set_clickable(av_grid, true);
    lv_obj_add_event_cb(av_grid, av_grid_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(av_grid, av_grid_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(card);
    if (av_cur) {                                    /* to theirs */
        int g = 0;
        while (g + 1 < AVATAR_GROUPS && avatar_group_first[g + 1] <= av_cur - 1) g++;
        lv_obj_scroll_to_y(av_scroll, (av_row0[g] + (av_cur - 1 - avatar_group_first[g]) / AV_COLS) * AV_CELL,
                           LV_ANIM_OFF);
    }
    av_scroll_cb(NULL);
}

/* the title row's context menu: Add, Rename and Avatar (who plays now), Delete (who plays now: not
 * the only one) */
static void player_menu_cb(lv_event_t *e)
{
    bool db = stats_db_ready() && p_count >= 0;
    const ctx_item_t items[] = {
        {LV_SYMBOL_PLUS, "Add player", player_add_cb, !db},
        {LV_SYMBOL_EDIT, "Rename player", player_rename_cb, !db},
        {LV_SYMBOL_IMAGE, "Avatar", player_avatar_cb, !db || !player_current()},
        {LV_SYMBOL_TRASH, "Delete player", player_delete_cb, !db || p_count < 2, true},
    };
    ctx_menu_toggle(p_menu_btn, items, (int)(sizeof(items) / sizeof(items[0])));
}

/* one row per player; without sudoku.db just who the board thinks plays */
static void player_color_cb(lv_event_t *e)
{
    int32_t id = (int32_t)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == id) {
            stats_db_set_player_color(id, p_players[i].color + 1);   /* the list comes back with it */
            p_players[i].color = (uint8_t)((p_players[i].color + 1) % STATS_PLAYER_COLORS);
        }
    }
    render_players();
}

static void render_players(void)
{
    home_player_update();                           /* (the avatar on Home's Players button) */
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

        /* their avatar (on a circle of their colour) or, without one, a dot of it: tap it for the next colour */
        int av = db && pl->avatar <= AVATAR_COUNT ? pl->avatar : 0;
        lv_obj_t *cb = make_btn(r, 0, 0, 56, 56, NULL, NULL, db ? player_color_cb : NULL, (void *)(intptr_t)pl->id);
        lv_obj_set_style_radius(cb, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(cb, player_color(db ? pl->color : 0), 0);
        lv_obj_set_style_bg_opa(cb, av ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(cb, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_align(cb, LV_ALIGN_LEFT_MID, -10, 0);
        if (av) {
            lv_obj_t *im = lv_image_create(cb);
            lv_image_set_src(im, &avatar_images[av - 1]);
            lv_obj_center(im);
        } else {
            lv_obj_center(player_dot(cb, db ? pl->color : 0, 22));
        }
        lv_obj_t *nm = make_label(r, &lv_font_montserrat_22, C_TEXT);
        lv_label_set_text(nm, pl->name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 58, -12);
        lv_obj_t *sub = make_label(r, &lv_font_montserrat_14, cur ? C_ACCENT : C_MUTED);
        if (cur && db) lv_label_set_text_fmt(sub, "Playing now   -   %u solved, %lu solves", (unsigned)pl->solved,
                                            (unsigned long)pl->solves);
        else if (cur) lv_label_set_text(sub, "Playing now");
        else lv_label_set_text_fmt(sub, "%u solved, %lu solves   -   tap to play", (unsigned)pl->solved,
                                   (unsigned long)pl->solves);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 58, 16);
    }
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
    p_menu_btn = make_btn(scr_profile, SCR_W - 12 - 52, 10, 52, 52, SYMBOL_MORE, &font_icons_24, player_menu_cb,
                          NULL);                    /* Add, Rename, Delete */

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
static void files_open_cb(lv_event_t *e);
static void sysmon_open_cb(lv_event_t *e);
static void build_stats(void);
static void build_settings(void);
static void build_wifi(void);
static void build_sysmon(void);
static void build_files(void);

/* Switches palette and LVGL theme, then rebuilds every screen around the
 * current state (all of which lives outside the widgets) and shows the
 * rebuilt copy of the screen that was open (home at start-up). */
static void apply_theme(bool dark)
{
    s_dark = dark;
    palette_build(dark, store_accent());
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, C_ACCENT, C_ORANGE, dark, &lv_font_montserrat_16));

    lv_obj_t *old_home = scr_home, *old_list = scr_list, *old_settings = scr_settings, *old_game = scr_game;
    lv_obj_t *old_stats = scr_stats, *old_wifi = scr_wifi, *old_profile = scr_profile, *old_history = scr_history;
    lv_obj_t *old_plays = scr_plays, *old_trophy = scr_trophy, *old_sysmon = scr_sysmon, *old_files = scr_files;
    lv_obj_t *act = lv_screen_active();

    build_game();
    build_list();
    build_home();
    build_stats();
    build_settings();
    build_wifi();
    build_sysmon();
    build_files();
    build_profile();
    build_history();
    build_plays();
    build_trophies();
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
                 : act && act == old_sysmon   ? scr_sysmon
                 : act && act == old_files    ? scr_files
                 : act && act == old_profile  ? scr_profile
                 : act && act == old_history  ? scr_history
                 : act && act == old_plays    ? scr_plays
                 : act && act == old_trophy   ? scr_trophy
                 : act && act == old_list     ? scr_list
                 : act && act == old_settings ? scr_settings
                 : scr_home;
    lv_screen_load(to);
    if (to == scr_plays) plays_ask(pl_offset);         /* fill the rebuilt list */
    if (to == scr_trophy) tr_ask();

    /* the old screens may be running this very event: delete them later */
    if (old_home) lv_obj_delete_async(old_home);
    if (old_list) lv_obj_delete_async(old_list);
    if (old_settings) lv_obj_delete_async(old_settings);
    if (old_stats) lv_obj_delete_async(old_stats);
    if (old_wifi) lv_obj_delete_async(old_wifi);
    if (old_sysmon) lv_obj_delete_async(old_sysmon);
    if (old_files) lv_obj_delete_async(old_files);
    if (old_profile) lv_obj_delete_async(old_profile);
    if (old_history) lv_obj_delete_async(old_history);
    if (old_plays) lv_obj_delete_async(old_plays);
    if (old_trophy) lv_obj_delete_async(old_trophy);
    if (old_game) lv_obj_delete_async(old_game);
}

static void theme_sw_cb(lv_event_t *e)
{
    bool dark = lv_obj_has_state(st_theme_sw, LV_STATE_CHECKED);
    store_set_dark_mode(dark);
    apply_theme(dark);
}

/* Resumes one of the current player's unfinished games (from RAM, or loaded
 * from sudoku.db with its move log). False if it isn't there any more. */
static bool resume_game(uint16_t id)
{
    if (store_saved_game()->id != id) {
        saved_game_t g;
        if (!stats_db_ready() || !stats_db_load_game(id, &g)) return false;
        store_set_game(&g);                              /* the one being played now */
        store_load_game_moves();
    }
    start_game(id, true);
    return true;
}

/* ---- the Unfinished games dialog (Continue with more than one) */
static lv_obj_t *ug_box;

static void ug_close(void)
{
    if (ug_box) lv_msgbox_close_async(ug_box);
    ug_box = NULL;
}

static void ug_close_cb(lv_event_t *e)
{
    ug_close();
}

static void ug_resume_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    ug_close();
    if (!resume_game(id)) sudoku_ui_notice("That game isn't in sudoku.db any more.", 3000);
}

static void ug_open(void);

static void ug_discard_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    if (store_saved_game()->id == id) store_clear_game();
    else stats_db_clear_game(id);
    if (g_id == id && !g_solved) g_id = STORE_NO_GAME;   /* the game screen held it */
    char name[40], msg[80];
    puzzle_name(id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "Discarded %s.", name);
    sudoku_ui_notice(msg, 2500);
    ug_close();
    refresh_home();
    if (s_ngames > 1) ug_open();                         /* the rest */
}

static void ug_open(void)
{
    ug_close();
    lv_obj_t *m = make_msgbox("Unfinished games", NULL);
    ug_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_t *content = lv_msgbox_get_content(m);
    for (int i = 0; i < s_ngames; i++) {
        const stats_game_row_t *r = &s_games[i];
        lv_obj_t *row = make_btn(content, 0, 0, 0, 76, NULL, NULL, ug_resume_cb, (void *)(uintptr_t)r->puzzle);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_style_bg_color(row, C_CARD2, 0);
        lv_obj_set_style_margin_bottom(row, 8, 0);
        char name[40], t[16], when[24] = "", line[96];
        puzzle_name(r->puzzle, name, sizeof(name));
        fmt_time(t, sizeof(t), r->elapsed_ms / 1000);
        if (r->saved_at) {
            time_t tt = (time_t)r->saved_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "   -   %a %d %b, %H:%M", &tm);
        }
        int var = VAR_CLASSIC;
        int cells = puzzle_is_generated(r->puzzle) && gen_lookup(r->puzzle, NULL, &var, NULL)
                        ? (var == VAR_MINI ? 36 : var == VAR_KIDS ? 16 : 81) : 81;
        snprintf(line, sizeof(line), "%s   -   %d / %d filled%s", t, r->filled, cells, when);
        lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text(n, name);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 14, -13);
        lv_obj_t *sub = make_label(row, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(sub, line);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 14, 14);
        lv_obj_t *del = make_btn(row, 0, 0, 56, 56, LV_SYMBOL_TRASH, &lv_font_montserrat_20, ug_discard_cb,
                                 (void *)(uintptr_t)r->puzzle);
        lv_obj_set_style_bg_color(del, C_CARD, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(del, 0), C_RED, 0);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    msgbox_button(m, "Close", ug_close_cb, false);
}

static void continue_cb(lv_event_t *e)
{
    games_refresh();
    if (s_ngames > 1) {                                  /* pick one */
        ug_open();
        return;
    }
    uint16_t id = store_saved_game()->id != STORE_NO_GAME ? store_saved_game()->id
                                                          : (s_ngames ? s_games[0].puzzle : STORE_NO_GAME);
    if (id != STORE_NO_GAME && !resume_game(id)) sudoku_ui_notice("That game isn't in sudoku.db any more.", 3000);
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
        if (s->id != id && stats_db_ready() && resume_game(id)) return;   /* an unfinished game in sudoku.db */
        start_game(id, s->id == id);
        return;
    }
    if (stats_db_ready()) {
        /* sudoku.db keeps several unfinished games: the current one is saved,
         * this one is resumed if it was started before, else begun */
        if (!resume_game(id)) start_game(id, false);
        return;
    }
    /* without a card there's one slot: resume or discard the game in it */
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
    if ((int)idx >= coll_size(l_coll)) return;
    g_from_coll = l_coll;
    open_puzzle(coll_puzzle(l_coll, (int)idx));
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

/* ---- the title row's context menu (the vertical dots): New puzzle, Enter, Learn */
static void new_menu_cb(lv_event_t *e)
{
    static const ctx_item_t ITEMS[] = {
        {LV_SYMBOL_PLUS, "New puzzle", new_puzzle_cb},
        {LV_SYMBOL_KEYBOARD, "Enter puzzle", enter_cb},      /* type one in from a book */
        {LV_SYMBOL_EYE_OPEN, "Learn", learn_cb},             /* practise a technique */
    };
    ctx_menu_toggle(l_new_btn, ITEMS, (int)(sizeof(ITEMS) / sizeof(ITEMS[0])));
}

/* the rows of the collection column (name + solved count), one per collection; again
 * when a collection comes or goes (Save) */
static void list_fill_colls(void)
{
    lv_obj_clean(l_colls);
    memset(l_coll_btn, 0, sizeof(l_coll_btn));
    imports_reload();                               /* the imported collections follow the built-in ones */
    for (int c = 0; c < coll_total(); c++) {
        l_coll_btn[c] = make_coll_btn(l_colls, coll_name(c), &l_coll_name[c], &l_coll_cnt[c], coll_click_cb, c);
        lv_obj_set_size(l_coll_name[c], COLL_W - 12 - 24 - 58,      /* one line, room for "100/100" */
                        lv_font_get_line_height(&lv_font_montserrat_16));
        lv_label_set_long_mode(l_coll_name[c], LV_LABEL_LONG_MODE_DOTS);
    }
}

static void build_list(void)
{
    scr_list = make_screen();

    make_btn(scr_list, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_list, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Puzzles");
    lv_obj_set_pos(t, 150, 20);
    l_total = make_label(scr_list, &lv_font_montserrat_16, C_MUTED);
    /* right of the title: the solved count, Starred (a toggle: the starred puzzles in the
     * right pane) and the context menu (New puzzle, Enter: type one in from a book, Learn) */
    l_new_btn = make_btn(scr_list, SCR_W - 12 - L_ICON_W, 10, L_ICON_W, 52, SYMBOL_MORE, &font_icons_24,
                         new_menu_cb, NULL);
    l_star_btn = make_btn(scr_list, SCR_W - 12 - 2 * L_ICON_W - 8, 10, L_ICON_W, 52, SYMBOL_STAR, &font_icons_24,
                          list_star_cb, NULL);
    icon_btn_set(l_star_btn, l_starred, C_ORANGE);
    lv_obj_align(l_total, LV_ALIGN_TOP_RIGHT, -(12 + 2 * L_ICON_W + 8 + 14), 28);

    /* left pane: the tabs (the built-in collections, or the player's: imported and saved),
     * and under them that tab's collections in a column that scrolls */
    static const char *const TAB[2] = {"Built-in", "Saved"};
    const int tw = (COLL_W - 6) / 2;
    for (int i = 0; i < 2; i++) {
        l_tab_btn[i] = make_btn(scr_list, 12 + i * (COLL_W - tw), L_PANE_Y, tw, L_TAB_H, TAB[i],
                                &lv_font_montserrat_16, list_tab_cb, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(l_tab_btn[i], C_CARD, 0);
        lv_obj_set_style_bg_color(l_tab_btn[i], C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_radius(l_tab_btn[i], 10, 0);
    }
    l_colls = make_coll_column(scr_list);
    lv_obj_set_pos(l_colls, 12, L_PANE_Y + L_TAB_H + 8);
    lv_obj_set_size(l_colls, COLL_W, SCR_W - (L_PANE_Y + L_TAB_H + 8) - 12);
    lv_obj_set_scroll_dir(l_colls, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l_colls, LV_SCROLLBAR_MODE_AUTO);
    list_fill_colls();

    /* right pane: the selected collection's name and stats, Export / Delete, its puzzles */
    l_name = make_label(scr_list, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_long_mode(l_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l_name, PANE_X + 4, L_PANE_Y + 2);
    lv_obj_set_width(l_name, PANE_W - 4);
    l_stats = make_label(scr_list, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(l_stats, PANE_X + 4, L_PANE_Y + 34);
    lv_obj_set_width(l_stats, PANE_W - 4);
    lv_label_set_long_mode(l_stats, LV_LABEL_LONG_MODE_DOTS);
    l_export = make_btn(scr_list, 0, 0, 130, 40, LV_SYMBOL_UPLOAD "  Export", &lv_font_montserrat_16, list_export_cb,
                        NULL);                      /* a saved collection to puzzles/<name>.sdx */
    lv_obj_align(l_export, LV_ALIGN_TOP_RIGHT, -12 - 52 - 8, L_PANE_Y - 4);
    l_del = make_btn(scr_list, 0, 0, 52, 40, LV_SYMBOL_TRASH, &lv_font_montserrat_18, list_del_cb, NULL);
    lv_obj_set_style_bg_color(l_del, C_CARD, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(l_del, 0), C_RED, 0);
    lv_obj_align(l_del, LV_ALIGN_TOP_RIGHT, -12, L_PANE_Y - 4);
    lv_obj_set_hidden(l_del, true);
    lv_obj_set_style_text_color(lv_obj_get_child(l_export, 0), C_ACCENT, 0);
    lv_obj_set_hidden(l_export, true);

    l_btnm = lv_buttonmatrix_create(scr_list);
    lv_obj_set_pos(l_btnm, PANE_X, L_GRID_Y);
    lv_obj_set_size(l_btnm, PANE_W, SCR_W - L_GRID_Y - 24);   /* cells about 39 x 45; the credit below */
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

    /* the Saved tab: the collection's puzzles as rows (name, variant, level, solved) */
    l_list = lv_obj_create(scr_list);
    lv_obj_remove_style_all(l_list);
    lv_obj_set_pos(l_list, PANE_X, L_GRID_Y);
    lv_obj_set_size(l_list, PANE_W, SCR_W - L_GRID_Y - 24);
    lv_obj_set_flex_flow(l_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l_list, 6, 0);
    lv_obj_set_scroll_dir(l_list, LV_DIR_VER);
    lv_obj_set_hidden(l_list, true);

    /* the Saved tab with nothing in it */
    l_empty = make_label(scr_list, &lv_font_montserrat_20, C_MUTED);
    lv_obj_set_width(l_empty, PANE_W - 24);
    lv_label_set_long_mode(l_empty, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l_empty, "Nothing saved yet.\n\nOpen a puzzle made with New puzzle or Enter and tap "
                               LV_SYMBOL_SAVE " next to the star to put it in a collection of your own. "
                               "Collections imported from the card (Settings > Data > Import puzzles) are here "
                               "too.");
    lv_obj_set_pos(l_empty, PANE_X + 12, L_GRID_Y + 20);
    lv_obj_set_hidden(l_empty, true);

    /* attribution */
    lv_obj_t *src = make_label(scr_list, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(src, "Puzzles: opensudoku.gitlab.io");
    lv_obj_align(src, LV_ALIGN_BOTTOM_RIGHT, -14, -2);
}

/* big menu button: icon + title, optional muted subtitle below */
static lv_obj_t *home_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **sub)
{
    lv_obj_t *b = make_btn(parent, 0, 0, HOME_COL_W, HOME_BTN_H, NULL, NULL, cb, NULL);
    if (h_nitems < (int)(sizeof(h_items) / sizeof(h_items[0]))) h_items[h_nitems++] = b;   /* (the menu's order) */
    lv_obj_t *l = make_label(b, &lv_font_montserrat_24, C_TEXT);
    lv_label_set_text(l, text);
    if (sub) {
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -12);
        *sub = make_label(b, &lv_font_montserrat_16, C_MUTED);
        lv_obj_set_size(*sub, LV_PCT(90), lv_font_get_line_height(&lv_font_montserrat_16));   /* one line: "..." */
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
    lv_obj_t *lk = make_btn(scr_home, 0, 0, 64, 52, SYMBOL_LOCK, &font_icons_24, lock_now_cb, NULL);
    lv_obj_align(lk, LV_ALIGN_TOP_RIGHT, -12 - 64 - 10, 10);   /* left of the power button: lock now */

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
    lv_obj_set_clickable(h_total, true);                 /* the week, compared with the last (week_cb) */
    lv_obj_set_ext_click_area(h_total, 12);
    lv_obj_add_event_cb(h_total, week_cb, LV_EVENT_CLICKED, NULL);

    /* the menu: one column, a grid, or pages of the grid to swipe (home_layout) */
    h_menu = lv_obj_create(col);
    lv_obj_remove_style_all(h_menu);
    lv_obj_set_size(h_menu, HOME_GRID_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(h_menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(h_menu, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(h_menu, LV_SCROLL_SNAP_START);
    lv_obj_set_scroll_one(h_menu, true);                /* a swipe moves one page */
    lv_obj_set_scrollbar_mode(h_menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(h_menu, home_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        h_page[p] = lv_obj_create(h_menu);
        lv_obj_remove_style_all(h_page[p]);
        lv_obj_set_size(h_page[p], HOME_GRID_W, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(h_page[p], HOME_GAP, 0);
        lv_obj_set_style_pad_column(h_page[p], HOME_GAP, 0);
        lv_obj_set_scrollable(h_page[p], false);
    }
    h_menu_grid = -1;                                   /* not laid out yet */
    h_nitems = 0;
    lv_obj_t *const menu = h_page[0];                   /* (home_layout sorts them onto the pages) */

    /* who is playing, first */
    lv_obj_t *pb = home_btn(menu, SYMBOL_USER "  Players", profile_cb, &h_player);   /* sub: "Playing as ..." */
    /* the title in a row with the player's avatar (home_player_update): the icon without one */
    h_player_title = lv_obj_get_child(pb, 0);
    lv_obj_set_style_text_font(h_player_title, &font_icons_24, 0);
    lv_obj_t *tr = lv_obj_create(pb);
    lv_obj_remove_style_all(tr);
    lv_obj_set_size(tr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tr, 10, 0);
    lv_obj_set_clickable(tr, false);
    lv_obj_align(tr, LV_ALIGN_CENTER, 0, -12);
    h_player_avatar = lv_image_create(tr);
    lv_obj_set_parent(h_player_title, tr);
    lv_obj_align(h_player_title, LV_ALIGN_DEFAULT, 0, 0);
    home_player_update();

    /* greyed out when there is no game in progress (refresh_home) */
    h_continue = home_btn(menu, LV_SYMBOL_PLAY "  Continue", continue_cb, &h_continue_sub);
    lv_obj_set_style_bg_opa(h_continue, LV_OPA_40, LV_STATE_DISABLED);    /* as Daily puzzle's */
    lv_obj_set_style_text_opa(h_continue, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(h_continue, C_LIST_ACTIVE, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_continue, 0), lv_color_white(), 0);
    lv_obj_set_style_text_color(h_continue_sub, lv_color_white(), 0);
    lv_obj_set_style_text_opa(h_continue_sub, LV_OPA_80, 0);

    home_btn(menu, LV_SYMBOL_LIST "  Puzzles", puzzles_cb, NULL);
    h_daily = home_btn(menu, LV_SYMBOL_BELL "  Daily puzzle", daily_cb, &h_daily_sub);
    lv_obj_set_style_bg_opa(h_daily, LV_OPA_40, LV_STATE_DISABLED);       /* greyed out until the clock is set */
    lv_obj_set_style_text_opa(h_daily, LV_OPA_40, LV_STATE_DISABLED);
    home_btn(menu, LV_SYMBOL_BARS "  Statistics", stats_cb, NULL);
    lv_obj_t *hb = home_btn(menu, SYMBOL_CALENDAR "  History", history_cb, NULL);   /* the calendar of games */
    lv_obj_set_style_text_font(lv_obj_get_child(hb, 0), &font_icons_24, 0);
    home_btn(menu, LV_SYMBOL_VIDEO "  Gameplays", plays_cb, NULL);
    lv_obj_t *tb = home_btn(menu, SYMBOL_TROPHY "  Trophies", trophy_cb, &h_trophy_sub);   /* achievements, leaderboard */
    lv_obj_set_style_text_font(lv_obj_get_child(tb, 0), &font_icons_24, 0);
    lv_label_set_text(h_trophy_sub, stats_db_ready() ? "Achievements and daily leaderboard" : "Needs a microSD card");
    home_btn(menu, LV_SYMBOL_CHARGE "  Time attack", attack_cb, NULL);              /* 10 minutes of Easy puzzles */
    home_btn(menu, LV_SYMBOL_DIRECTORY "  Files", files_open_cb, NULL);              /* the microSD card */
    home_btn(menu, LV_SYMBOL_SETTINGS "  Settings", settings_cb, NULL);              /* (the second page) */
    lv_obj_t *sb = home_btn(menu, SYMBOL_CHART "  System", sysmon_open_cb, NULL);    /* system monitor */
    lv_obj_set_style_text_font(lv_obj_get_child(sb, 0), &font_icons_24, 0);

    h_dots = lv_obj_create(scr_home);                    /* page dots (home_dots) */
    lv_obj_remove_style_all(h_dots);
    lv_obj_set_size(h_dots, LV_SIZE_CONTENT, 12);
    lv_obj_set_flex_flow(h_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(h_dots, 12, 0);
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_t *d = lv_obj_create(h_dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 12, 12);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
    lv_obj_align(h_dots, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_set_hidden(h_dots, true);

    lv_obj_t *src = make_label(scr_home, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(src, "Puzzles: opensudoku.gitlab.io");
    lv_obj_align(src, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/* the Statistics screen, top down: title and (i); player, Chart; levels | table */
#define X_ROW_Y   72                                /* the row under the title */
#define X_BODY_Y  (X_ROW_Y + 52 + 12)               /* the level column and the pane */

static void build_stats(void)
{
    scr_stats = make_screen();

    make_btn(scr_stats, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_stats, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Statistics");
    lv_obj_set_pos(t, 150, 20);
    lv_obj_t *info = make_btn(scr_stats, SCR_W - 12 - 52, 10, 52, 52, SYMBOL_INFO, &font_icons_24, stats_info_cb,
                              NULL);                 /* top right, on the title's line */
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(info, 0), C_ACCENT, 0);

    lv_obj_t *list = make_coll_column(scr_stats);
    lv_obj_set_pos(list, 12, X_BODY_Y);
    lv_obj_set_size(list, COLL_W, SCR_W - X_BODY_Y - 12);
    x_btn[0] = make_coll_btn(list, "All levels", &x_name_lbl[0], &x_cnt_lbl[0], stats_row_cb, 0);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        x_btn[c + 1] = make_coll_btn(list, puzzle_collections[c].name, &x_name_lbl[c + 1], &x_cnt_lbl[c + 1],
                                     stats_row_cb, c + 1);
    }
    for (int k = 0; k < KIND_COUNT; k++) {          /* made on the board (the list scrolls) */
        int r = 1 + PUZZLE_COLLECTION_COUNT + k;
        x_btn[r] = make_coll_btn(list, STATS_KIND_NAMES[k], &x_name_lbl[r], &x_cnt_lbl[r], stats_row_cb, r);
    }

    int px = PANE_X, pw = PANE_W;
    /* top bar, right: whose statistics, as a dropdown */
    x_player_dd = lv_dropdown_create(scr_stats);
    x_player_av = NULL;                                /* (a rebuild: the old one went with the old screen) */
    lv_obj_set_pos(x_player_dd, 12, X_ROW_Y);          /* whose statistics: first in the row */
    lv_obj_set_size(x_player_dd, 320, 52);
    lv_obj_set_style_text_font(x_player_dd, &font_icons_20, 0);
    lv_obj_set_style_text_color(x_player_dd, C_TEXT, 0);
    lv_obj_set_style_bg_color(x_player_dd, C_CARD, 0);
    lv_obj_set_style_border_width(x_player_dd, 0, 0);
    lv_obj_set_style_shadow_width(x_player_dd, 0, 0);
    lv_obj_set_style_radius(x_player_dd, 8, 0);
    lv_obj_set_style_pad_hor(x_player_dd, X_DD_PAD, 0);
    lv_obj_set_style_pad_ver(x_player_dd, 15, 0);
    lv_obj_t *pdl = lv_dropdown_get_list(x_player_dd);
    lv_obj_set_style_text_font(pdl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pdl, C_TEXT, 0);
    lv_obj_set_style_text_line_space(pdl, X_DD_LS, 0);
    lv_obj_set_style_pad_ver(pdl, 14, 0);
    lv_obj_add_event_cb(pdl, stats_player_list_draw_cb, LV_EVENT_DRAW_POST, NULL);
    lv_obj_set_style_bg_color(pdl, C_CARD2, 0);
    lv_obj_set_style_border_width(pdl, 0, 0);
    lv_obj_set_style_radius(pdl, 8, 0);
    lv_obj_set_style_max_height(pdl, 480, 0);
    lv_obj_set_style_bg_color(pdl, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(pdl, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_add_event_cb(x_player_dd, stats_player_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
    stats_player_dd_update();
    x_title = make_label(scr_stats, &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(x_title, px + 4, X_BODY_Y + 4);
    x_sum1 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum1, px + 4, X_BODY_Y + 34);
    x_sum2 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum2, px + 4, X_BODY_Y + 54);

    x_table = lv_table_create(scr_stats);
    lv_obj_set_pos(x_table, px, X_BODY_Y + 82);
    lv_obj_set_size(x_table, pw, SCR_W - X_BODY_Y - 82 - 12);
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
    lv_obj_set_pos(x_empty, px + 4, X_BODY_Y + 104);

    /* Chart / Table switch (top right of the pane) and the chart card in the table's place */
    lv_obj_t *ib = make_btn(scr_stats, SCR_W - 12 - 160 - 10 - 170, X_ROW_Y, 170, 52, SYMBOL_BULB "  Insights",
                            &font_icons_20, insights_cb, NULL);   /* mistake insights, from the move logs */
    lv_obj_set_style_text_color(lv_obj_get_child(ib, 0), C_ACCENT, 0);
    x_chart_btn = make_btn(scr_stats, SCR_W - 12 - 160, X_ROW_Y, 160, 52, SYMBOL_CHART "  Chart", &font_icons_20,
                           chart_btn_cb, NULL);
    lv_obj_set_style_text_color(lv_obj_get_child(x_chart_btn, 0), C_ACCENT, 0);
    x_chart = lv_obj_create(scr_stats);
    lv_obj_set_pos(x_chart, px, X_BODY_Y + 82);
    lv_obj_set_size(x_chart, pw, SCR_W - X_BODY_Y - 82 - 12);
    lv_obj_set_style_bg_color(x_chart, C_CARD, 0);
    lv_obj_set_style_border_width(x_chart, 0, 0);
    lv_obj_set_style_radius(x_chart, 12, 0);
    lv_obj_set_style_pad_all(x_chart, 12, 0);
    lv_obj_set_scrollable(x_chart, false);
    lv_obj_set_flex_flow(x_chart, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(x_chart, 10, 0);
    x_plot = lv_obj_create(x_chart);
    lv_obj_remove_style_all(x_plot);
    lv_obj_set_size(x_plot, LV_PCT(100), 320);       /* (the trend line under it) */
    lv_obj_add_event_cb(x_plot, plot_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    x_trend = make_label(x_chart, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(x_trend, LV_PCT(100));
    lv_label_set_long_mode(x_trend, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_hidden(x_chart, true);

}

/* one settings row: a card with a title + hint on the left; returns the card
 * so the caller can put a control on its right */
/* ---- Settings tabs */
enum { ST_NETWORK = 0, ST_DISPLAY, ST_SOUND, ST_GAME, ST_DATA, ST_TABS };
#define ST_TAB_Y  72
#define ST_TAB_H  52
#define ST_PANE_Y (ST_TAB_Y + ST_TAB_H + 12)
static lv_obj_t *st_tab_btn[ST_TABS], *st_pane[ST_TABS];
static int st_tab;                                  /* the open tab; Settings opens on Network */

static void fw_row_update(void);

static void st_select_tab(int tab)
{
    st_tab = tab;
    if (tab == ST_DATA) fw_row_update();            /* (a new update.bin since: copied over USB)  */
    for (int i = 0; i < ST_TABS; i++) {
        lv_obj_set_state(st_tab_btn[i], LV_STATE_CHECKED, i == tab);
        lv_obj_set_style_text_color(lv_obj_get_child(st_tab_btn[i], 0), i == tab ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(st_pane[i], i != tab);
    }
    lv_obj_scroll_to_y(st_pane[tab], 0, LV_ANIM_OFF);
}

static void st_tab_cb(lv_event_t *e)
{
    st_select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

/* Night mode: switches the night look on or off when the night hours start or end
 * (every 20 s, and at once when its settings change). Waits while a dialog is open on
 * the top layer (a theme change rebuilds the screens under it), for 5 minutes at most. */
static void night_check(lv_timer_t *t)
{
    static int waited;
    bool want = false;
    time_t now = time(NULL);
    if (store_night_mode() && now > 1700000000) {   /* (needs the clock: Wi-Fi) */
        struct tm tm;
        localtime_r(&now, &tm);
        int h = tm.tm_hour, a = store_night_start(), b = store_night_end();
        want = a != b && (a < b ? (h >= a && h < b) : (h >= a || h < b));   /* (22-7 spans midnight) */
    }
    if (want == s_night) {
        waited = 0;
        return;
    }
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; t && waited < 15 && i < lv_obj_get_child_count(top); i++) {
        if (!lv_obj_is_hidden(lv_obj_get_child(top, (int32_t)i))) {
            waited++;
            return;                                 /* try again at the next check */
        }
    }
    waited = 0;
    s_night = want;
    bool dark = want || store_dark_mode();
    if (dark != s_dark) apply_theme(dark);
    if (!s_saver) bsp_display_brightness_set(effective_brightness());   /* (off stays off) */
    printf("night mode: %s\n", want ? "on" : "off");
}

/* an open dropdown list (time zone) doesn't stay up over another screen */
static void st_close_lists_cb(lv_event_t *e)
{
    if (st_tz_dd) lv_dropdown_close(st_tz_dd);
}

static void hint_mode_dd_cb(lv_event_t *e)
{
    store_set_hint_nudge(lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1);
}

static void tour_again_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("The tour needs sudoku.db on a microSD card.", 3000);
        return;
    }
    stats_db_set_seen("tour", false);
    sudoku_ui_notice("The tour shows the next time you open a puzzle.", 3000);
}

static void big_digits_dd_cb(lv_event_t *e)
{
    store_set_big_digits(lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1);
    for (int i = 0; i < 81; i++) {                  /* the game's board now (Replay: when it opens) */
        if (g_cell_lbl[i]) lv_obj_set_style_text_font(g_cell_lbl[i], DIGIT_FONT_N(g_geo ? g_geo->n : 9), 0);
        if (g_cells[i]) lv_obj_invalidate(g_cells[i]);   /* (the pencil marks too) */
    }
}

static void sysmon_open_cb(lv_event_t *e)
{
    lv_screen_load(scr_sysmon);
    sysmon_ui_set_shown(true);
}

static void sysmon_back_cb(lv_event_t *e)
{
    sysmon_ui_set_shown(false);
    lv_screen_load(scr_home);                       /* (opened from the Home menu) */
}

static void files_open_cb(lv_event_t *e)
{
    lv_screen_load(scr_files);
    file_ui_set_shown(true);
}

static void files_back_cb(lv_event_t *e)
{
    file_ui_set_shown(false);
    lv_screen_load(scr_home);                       /* (opened from the Home menu) */
}

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
    lv_obj_set_width(h, 400);                       /* clear of the button / switch on the right */
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_DOTS);
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
    lv_obj_add_event_cb(scr_settings, st_close_lists_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);

    make_btn(scr_settings, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_settings, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Settings");
    lv_obj_set_pos(t, 150, 20);

    /* the tabs, under the title (Network first, and the one Settings opens on) */
    static const char *const TABS[ST_TABS] = {LV_SYMBOL_WIFI "  Network", LV_SYMBOL_IMAGE "  Display",
                                              LV_SYMBOL_VOLUME_MAX "  Sound", LV_SYMBOL_EDIT "  Game",
                                              LV_SYMBOL_SD_CARD "  Data"};
    const int gap = 8, tw = (SCR_W - 24 - (ST_TABS - 1) * gap) / ST_TABS;
    for (int i = 0; i < ST_TABS; i++) {
        st_tab_btn[i] = make_btn(scr_settings, 12 + i * (tw + gap), ST_TAB_Y, tw, ST_TAB_H, TABS[i],
                                 &lv_font_montserrat_16, st_tab_cb, (void *)(intptr_t)i);
        /* each tab's rows: a scrolling column */
        lv_obj_t *pane = lv_obj_create(scr_settings);
        lv_obj_remove_style_all(pane);
        lv_obj_set_pos(pane, 40, ST_PANE_Y);
        lv_obj_set_size(pane, SCR_W - 80, SCR_W - ST_PANE_Y);
        lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(pane, 10, 0);
        lv_obj_set_style_pad_bottom(pane, 16, 0);
        lv_obj_set_scroll_dir(pane, LV_DIR_VER);
        st_pane[i] = pane;
    }
    lv_obj_t *col = st_pane[ST_DISPLAY];

    lv_obj_t *r = settings_row(col, "Dark mode", "Light text on a dark background");
    st_theme_sw = settings_switch(r, s_dark, theme_sw_cb);

    r = settings_row(col, "Accent colour", "Buttons, the selected cell and your digits");
    settings_dropdown(r, ACCENT_NAMES, store_accent(), 150, accent_dd_cb);

    r = settings_row(col, "Colour-blind friendly", "Orange-red and blue-green in place of red and green");
    settings_switch(r, store_cvd(), cvd_sw_cb);

    r = settings_row(col, "Digit size", "The board's digits and pencil marks");
    settings_dropdown(r, "Normal\nLarge", store_big_digits() ? 1 : 0, 150, big_digits_dd_cb);

    r = settings_row(col, "Brightness", "Screen backlight");
    st_bright_sl = settings_slider(r, 10, 100, store_brightness(), bright_sl_cb, &st_bright_lbl);

    r = settings_row(col, "Screen off after", "Without a touch. The game pauses; tap to wake.");
    uint32_t sel = 0;
    while (sel < SLEEP_COUNT - 1 && SLEEP_MIN[sel] != s_sleep_min) sel++;
    st_sleep_dd = settings_dropdown(r, SLEEP_OPTIONS, sel, 150, sleep_dd_cb);

    r = settings_row(col, "Lock screen", "After the screen timeout: swipe up, or a PIN");
    st_lock_dd = settings_dropdown(r, "Off\nSwipe\nPIN", store_lock_mode(), 150, lock_dd_cb);

    r = settings_row(col, "Lock PIN", "4 digits; RESET starts the board unlocked");
    st_pin_btn = make_btn(r, 0, 0, 150, 52, LV_SYMBOL_EDIT "  Change", &lv_font_montserrat_18, pin_change_cb, NULL);
    lv_obj_set_style_bg_color(st_pin_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_pin_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(st_pin_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_pin_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_state(st_pin_btn, LV_STATE_DISABLED, store_lock_mode() != LOCK_PIN);

    r = settings_row(col, "Screen-saver clock", "A dim clock for 10 min before the screen goes dark");
    settings_switch(r, store_saver_clock(), saver_clock_sw_cb);

    r = settings_row(col, "Night mode", "Dark and dimmer at night (needs the clock: Wi-Fi)");
    settings_switch(r, store_night_mode(), night_sw_cb);

    static char hours[24 * 6];                      /* "00:00\n01:00\n...23:00" */
    if (!hours[0]) {
        size_t len = 0;
        for (int h = 0; h < 24; h++) len += snprintf(hours + len, sizeof(hours) - len, "%s%02d:00", h ? "\n" : "", h);
    }
    r = settings_row(col, "Night hours", "From - to, local time");
    st_night_to = settings_dropdown(r, hours, store_night_end(), 110, night_hours_cb);
    st_night_from = settings_dropdown(r, hours, store_night_start(), 110, night_hours_cb);
    lv_obj_align(st_night_from, LV_ALIGN_RIGHT_MID, -150, 0);
    lv_obj_t *to = make_label(r, &lv_font_montserrat_18, C_MUTED);
    lv_label_set_text(to, "-");
    lv_obj_align(to, LV_ALIGN_RIGHT_MID, -126, 0);

    r = settings_row(col, "Night brightness", "At most the day's brightness");
    settings_slider(r, 10, 100, store_night_brightness(), night_bright_cb, &st_night_bright_lbl);

    col = st_pane[ST_SOUND];
    r = settings_row(col, "Sound effects", "Taps, mistakes, finished digits, solved");
    st_sound_sw = settings_switch(r, sfx_enabled(), sound_sw_cb);

    r = settings_row(col, "Volume", "Speaker loudness");
    st_vol_sl = settings_slider(r, 0, 100, store_volume(), vol_sl_cb, &st_vol_lbl);

    r = settings_row(col, "Gibberlink", "Remote control by sound (ggwave): the mics listen");
    settings_switch(r, store_gibberlink(), gibberlink_sw_cb);

    col = st_pane[ST_GAME];
    r = settings_row(col, "Show timer", "Off: hidden while you play (times are still kept)");
    settings_switch(r, store_show_timer(), timer_sw_cb);

    r = settings_row(col, "Mistake check", "Red digits: none, rule breakers, or any wrong digit");
    st_assist_dd = settings_dropdown(r, "Off\nRules\nSolution", store_assist(), 150, assist_dd_cb);

    r = settings_row(col, "No-notes challenge", "New games: Notes and Auto off; solves get a badge");
    settings_switch(r, store_no_notes(), no_notes_sw_cb);

    r = settings_row(col, "Left-handed", "The number pad on the left of the board");
    settings_switch(r, store_left_hand(), left_hand_sw_cb);

    r = settings_row(col, "Hints", "Nudge only: where to look, without More and Place it");
    settings_dropdown(r, "Full\nNudge only", store_hint_nudge() ? 1 : 0, 170, hint_mode_dd_cb);

    r = settings_row(col, "Game screen tour", "A first look at the board and its buttons");
    lv_obj_t *tb = make_btn(r, 0, 0, 170, 52, "Show again", &lv_font_montserrat_18, tour_again_cb, NULL);
    lv_obj_set_style_bg_color(tb, C_CARD2, 0);          /* (like the dropdowns) */
    lv_obj_set_style_text_color(lv_obj_get_child(tb, 0), C_ACCENT, 0);
    lv_obj_align(tb, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Wi-Fi: the whole card opens the Wi-Fi screen */
    col = st_pane[ST_NETWORK];
    r = settings_row(col, "Wi-Fi", "For the clock (time from the internet)");
    lv_obj_set_clickable(r, true);
    lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, wifi_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *chev = make_label(r, &lv_font_montserrat_20, C_MUTED);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    /* the eye: show or hide the network's name (its own tap, not the row's) */
    st_wifi_eye = make_btn(r, 0, 0, 48, 44, LV_SYMBOL_EYE_OPEN, &lv_font_montserrat_20, st_wifi_eye_cb, NULL);
    lv_obj_set_style_bg_color(st_wifi_eye, C_CARD2, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(st_wifi_eye, 0), C_ACCENT, 0);
    lv_obj_align(st_wifi_eye, LV_ALIGN_RIGHT_MID, -30, 0);
    st_wifi_lbl = make_label(r, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(st_wifi_lbl, 200);
    lv_obj_set_style_text_align(st_wifi_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(st_wifi_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(st_wifi_lbl, "");
    lv_obj_align(st_wifi_lbl, LV_ALIGN_RIGHT_MID, -86, 0);
    st_wifi_update();

    /* time zone: IANA names; the list opens downwards */
    r = settings_row(col, "Time zone", "For the clock on the home screen");
    st_tz_dd = lv_dropdown_create(r);
    lv_obj_set_size(st_tz_dd, 330, 52);
    lv_obj_align(st_tz_dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options_static(st_tz_dd, tz_dropdown_options());
    lv_obj_set_style_text_font(st_tz_dd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st_tz_dd, C_TEXT, 0);
    lv_obj_set_style_bg_color(st_tz_dd, C_CARD2, 0);
    lv_obj_set_style_border_width(st_tz_dd, 0, 0);
    lv_obj_set_style_shadow_width(st_tz_dd, 0, 0);
    lv_obj_set_style_radius(st_tz_dd, 8, 0);
    lv_obj_set_style_pad_hor(st_tz_dd, 14, 0);
    lv_obj_set_style_pad_ver(st_tz_dd, 16, 0);
    lv_obj_t *dl = lv_dropdown_get_list(st_tz_dd);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dl, C_TEXT, 0);
    lv_obj_set_style_text_line_space(dl, 22, 0);
    lv_obj_set_style_bg_color(dl, C_CARD2, 0);
    lv_obj_set_style_border_width(dl, 0, 0);
    lv_obj_set_style_radius(dl, 8, 0);
    lv_obj_set_style_max_height(dl, 440, 0);       /* down to near the bottom of the screen */
    lv_obj_set_style_bg_color(dl, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(dl, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_dropdown_set_selected(st_tz_dd, (uint32_t)tz_find(store_timezone()));
    tz_show_selected();
    lv_obj_add_event_cb(st_tz_dd, tz_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    col = st_pane[ST_DATA];
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

    r = settings_row(col, "Firmware update", "");
    st_fw_hint = lv_obj_get_child(r, 1);
    st_fw_btn = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_DOWNLOAD "  Update", &lv_font_montserrat_18, fw_update_cb, NULL);
    lv_obj_set_style_bg_color(st_fw_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_fw_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(st_fw_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_fw_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    fw_row_update();

    r = settings_row(col, "Import puzzles", "From puzzles/ on the card: .opensudoku, .sdm, .sdx");
    lv_obj_t *im = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_DOWNLOAD "  Import", &lv_font_montserrat_18, import_cb, NULL);
    lv_obj_set_style_bg_color(im, C_CARD2, 0);
    lv_obj_align(im, LV_ALIGN_RIGHT_MID, 0, 0);

    r = settings_row(col, "Export to CSV", "Solves, achievements, records: export/ on the card");
    lv_obj_t *ex = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_SAVE "  Export", &lv_font_montserrat_18, export_cb, NULL);
    lv_obj_set_style_bg_color(ex, C_CARD2, 0);
    lv_obj_align(ex, LV_ALIGN_RIGHT_MID, 0, 0);

    r = settings_row(col, "Factory reset", "Settings, Wi-Fi network, players and statistics");
    lv_obj_t *fr = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_WARNING "  Reset", &lv_font_montserrat_18,
                            factory_reset_cb, NULL);
    lv_obj_set_style_bg_color(fr, C_RED, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(fr, 0), lv_color_white(), 0);
    lv_obj_align(fr, LV_ALIGN_RIGHT_MID, 0, 0);

    st_select_tab(st_tab);                          /* (a rebuild keeps the tab that was open) */
}

static void build_wifi(void)
{
    scr_wifi = make_screen();
    wifi_ui_create(scr_wifi, wifi_back_cb);
}

static void build_sysmon(void)
{
    scr_sysmon = make_screen();
    sysmon_ui_create(scr_sysmon, sysmon_back_cb);
}

static void build_files(void)
{
    scr_files = make_screen();
    file_ui_create(scr_files, files_back_cb);
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

/* ------------------------------------------------------------- lock screen */
/*
 * Settings > Display > Lock screen: with Swipe or PIN, the screen comes back
 * from its timeout locked: the time, the date and who is playing, and a
 * swipe up (or the 4-digit PIN) to get back. It sits on the system layer,
 * under the black cover of the screen saver, so the waking tap only shows
 * it. After 5 wrong PINs the keypad waits 30 s. Not security: RESET starts
 * the board unlocked (also the way back in with a forgotten PIN).
 */
#define LOCK_SWIPE_PX   140                 /* how far up a swipe has to go */
#define LOCK_TRIES      5
#define LOCK_WAIT_MS    30000

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
    if (!done && ps_from_dd && st_lock_dd) lv_dropdown_set_selected(st_lock_dd, store_lock_mode());
    if (st_pin_btn) lv_obj_set_state(st_pin_btn, LV_STATE_DISABLED, store_lock_mode() != LOCK_PIN);
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

static void lock_dd_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(st_lock_dd);
    if (sel == LOCK_PIN) {                          /* a PIN first */
        pin_setup_open(true);
        return;
    }
    store_set_lock_mode((uint8_t)sel);
    lv_obj_set_state(st_pin_btn, LV_STATE_DISABLED, true);
}

static void pin_change_cb(lv_event_t *e)
{
    pin_setup_open(false);
}


/*
 * Screen-saver clock (Settings > Display): at the timeout the black cover
 * shows a dim clock and the date instead, at a new place every minute (no
 * burn-in), with the backlight low; after SAVER_CLOCK_MIN minutes the light
 * goes off as without it. A tap wakes it the same way.
 */
#define SAVER_CLOCK_MIN     10
#define SAVER_CLOCK_LIGHT   12              /* % backlight while it shows */
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
        bsp_display_brightness_set(0);
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
        bsp_display_brightness_set(effective_brightness());
    } else if (s_saver) {
        /* removed only once the finger is up, so the waking tap reaches nothing below */
        lv_obj_delete_async(s_saver);
        s_saver = NULL;
    }
}

static void saver_sleep(bool lock_now)
{
    if (lv_screen_active() == scr_game && g_id != STORE_NO_GAME && !g_solved && !g_paused) {
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
    bsp_display_brightness_set(0);
}

int sudoku_ui_fill(const uint8_t *cells, const uint8_t *digits, int n, char *why, size_t why_n)
{
    if (lv_screen_active() != scr_game || !g_geo) {
        snprintf(why, why_n, "no game on screen");
        return -1;
    }
    if (g_solved || g_paused) {
        snprintf(why, why_n, g_solved ? "already solved" : "paused");
        return -1;
    }
    for (int k = 0; k < n; k++) {                   /* all of them first */
        int i = cells[k], d = digits[k];
        if (i > 80 || !g_geo->used[i]) {
            snprintf(why, why_n, "cell %d isn't on the board", i);
            return -1;
        }
        if (g_given[i]) {
            snprintf(why, why_n, "cell %d is a given", i);
            return -1;
        }
        if (d < 1 || d > g_geo->n) {
            snprintf(why, why_n, "digit %d for cell %d: 1..%d", d, i, g_geo->n);
            return -1;
        }
    }
    bool notes = g_notes_mode;
    g_notes_mode = false;                           /* digits, not notes */
    int placed = 0;
    for (int k = 0; k < n && !g_solved; k++) {
        if (g_b.val[cells[k]] == digits[k]) continue;   /* (a second tap would clear it) */
        g_sel = cells[k];
        enter_digit(digits[k]);                     /* as from the pad */
        placed++;
    }
    g_notes_mode = notes;
    if (!g_solved) {
        g_sel = -1;
        refresh_board();
    }
    why[0] = 0;
    return placed;
}

bool sudoku_ui_state(sudoku_ui_state_t *st, char board[82])
{
    if (lv_screen_active() != scr_game || !g_geo) return false;
    memset(st, 0, sizeof(*st));
    if (g_practice >= 0) snprintf(st->name, sizeof(st->name), "Learn practice");
    else if (g_id != STORE_NO_GAME) puzzle_name(g_id, st->name, sizeof(st->name));
    st->variant = g_var >= 0 && g_var < VAR_COUNT ? VARIANT_NAMES[g_var] : "?";
    st->size = g_geo->n;
    st->cells = g_geo->ncells;
    for (int i = 0; i < 81; i++) {
        int v = g_b.val[i];
        if (!g_geo->used[i]) board[i] = '#';
        else if (!v) board[i] = '.';
        else board[i] = g_given[i] ? (char)('0' + v) : (char)('a' + v - 1);
        if (g_geo->used[i] && v) st->filled++;
    }
    board[81] = 0;
    st->solved = g_solved;
    st->paused = g_paused;
    st->practice = g_practice >= 0;
    st->attack = g_attack;
    st->seconds = (g_elapsed_ms + 500) / 1000;
    st->mistakes = g_mistakes;
    st->hints = g_hints;
    return true;
}

void sudoku_ui_screen_off(void)
{
    if (!s_saver) saver_sleep(false);
}

/* the lock button on Home: the screen off now, and locked when it wakes */
static void lock_now_cb(lv_event_t *e)
{
    if (!s_saver) saver_sleep(true);
}

static void saver_cb(lv_timer_t *t)
{
    if (!s_saver && s_sleep_min &&
        lv_display_get_inactive_time(NULL) >= (uint32_t)s_sleep_min * 60 * 1000) saver_sleep(false);
}

static void touch_sound_cb(lv_event_t *e)
{
    if (s_saver) return;   /* waking tap is silent */
    sfx_play(SFX_TOUCH);
}

esp_err_t sudoku_ui_start(void)
{
    g_undo = heap_caps_calloc(UNDO_MAX, sizeof(board_t), MALLOC_CAP_SPIRAM);
    g_redo = heap_caps_calloc(UNDO_MAX, sizeof(board_t), MALLOC_CAP_SPIRAM);
    if (!g_undo || !g_redo) return ESP_ERR_NO_MEM;
    sudoku_geom_init();                             /* before the generator task can use the tables */
    g_geo = sudoku_geom(VAR_CLASSIC);
    g_geo_buf = heap_caps_malloc(sizeof(*g_geo_buf), MALLOC_CAP_SPIRAM);
    s_gen_cache = heap_caps_calloc(GEN_CACHE, sizeof(gen_cache_t), MALLOC_CAP_SPIRAM);
    x_view_p = heap_caps_calloc(1, sizeof(stats_view_t), MALLOC_CAP_SPIRAM);
    if (!g_geo_buf || !s_gen_cache || !x_view_p) return ESP_ERR_NO_MEM;
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
    gibberlink_set_enabled(store_gibberlink());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(effective_brightness());
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        lv_indev_add_event_cb(in, touch_sound_cb, LV_EVENT_PRESSED, NULL);
    }
    tz_apply(store_timezone());
    l_coll = store_last_collection();
    l_tab = coll_tab(l_coll);
    s_sleep_min = store_screen_timeout_min();
    apply_theme(store_dark_mode());
    lv_obj_delete(splash);

    g_last_tick = lv_tick_get();
    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(saver_cb, 1000, NULL);
    lv_timer_create(clock_cb, 1000, NULL);
    lv_timer_create(night_check, 20000, NULL);         /* night mode: dark and dimmer at night */
    lv_timer_create(sd_poll_cb, 100, NULL);
    clock_cb(NULL);
    return ESP_OK;
}

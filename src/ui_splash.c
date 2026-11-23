/**
 * @file ui_splash.c
 * @ingroup ui_sudoku
 * @brief The start-up screen: a loading bar and, under it, what start-up is doing (reading the card,
 *        opening sudoku.db, building each screen), shown while sudoku_ui_start() runs.
 *
 * Start-up has no clock of its own to go by, so the steps are weighted by how long they took on the
 * board (2026-11-22): the card 2 %, sudoku.db about half (the bar creeps while it opens), the screens
 * the rest, each by its own time (SPLASH_BUILD_MS).
 */
#include "ui_internal.h"

static lv_obj_t *s_box;                 /**< the bar and its line of text (NULL: not shown) */
static lv_obj_t *s_bar;                 /**< the loading bar */
static lv_obj_t *s_what;                /**< what start-up is doing */
static int s_pct;                       /**< the progress shown, 0..100 */

/** @brief The screens apply_theme() builds, in its order: their names for the line under the bar, and about
 * how long each took at start-up on the board, ms (2026-11-22): the bar's share of each, from
 * SPLASH_SCREENS_FROM % to 100 %. */
static const struct {
    const char *name;                   /**< "Building the <name>" */
    uint16_t ms;                        /**< its time */
} SPLASH_BUILD[] = {
    {"game screen", 176},
    {"puzzle list", 85},
    {"home screen", 57},
    {"statistics", 230},
    {"settings", 166},
    {"Wi-Fi screen", 21},
    {"system monitor", 51},
    {"file manager", 27},
    {"players", 4},
    {"history", 7},
    {"gameplays", 3},
    {"trophies", 13},
    {"screens' contents", 59},          /* (the refreshes after them) */
};
#define SPLASH_BUILD_N ((int)(sizeof(SPLASH_BUILD) / sizeof(SPLASH_BUILD[0])))   /**< the steps */

/* (documented in ui_internal.h) */
lv_obj_t *splash_show(lv_obj_t *parent)
{
    const palette_t *p = ui_palette_dark();
    s_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_box);
    lv_obj_set_size(s_box, 420, LV_SIZE_CONTENT);
    lv_obj_center(s_box);
    lv_obj_set_flex_flow(s_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_box, 16, 0);

    s_bar = lv_bar_create(s_box);
    lv_obj_set_size(s_bar, LV_PCT(100), 14);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(p->card2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(p->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(s_bar, 0, 0);    /* (drawn at once: no LVGL loop yet to animate it) */

    s_what = lv_label_create(s_box);
    lv_obj_set_width(s_what, LV_PCT(100));
    lv_obj_set_style_text_align(s_what, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_what, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_what, lv_color_hex(p->muted), 0);
    lv_label_set_text(s_what, "Starting");
    s_pct = -1;
    splash_progress(SPLASH_SHOWN);
    return s_box;
}

/* (documented in ui_internal.h) */
void splash_progress(int pct)
{
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    if (!s_bar || pct == s_pct) {
        return;
    }
    s_pct = pct;
    lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
    lv_refr_now(NULL);                              /* (start-up: the LVGL loop isn't running yet) */
}

/* (documented in ui_internal.h) */
void splash_status(const char *what)
{
    if (!s_what) {
        return;
    }
    lv_label_set_text(s_what, what);
    lv_refr_now(NULL);
}

/* (documented in ui_internal.h) */
void splash_built(int step)
{
    if (!s_bar || step < 0 || step > SPLASH_BUILD_N) {
        return;                                     /* (a theme change later: no start-up screen) */
    }
    int done = 0, all = 0;
    for (int i = 0; i < SPLASH_BUILD_N; i++) {
        all += SPLASH_BUILD[i].ms;
        done += i < step ? SPLASH_BUILD[i].ms : 0;
    }
    if (step < SPLASH_BUILD_N) {                    /* the next one, being built now */
        lv_label_set_text_fmt(s_what, "Building the %s", SPLASH_BUILD[step].name);
    } else {
        lv_label_set_text(s_what, "Ready");
    }
    s_pct = -1;                                     /* (redrawn for the text, whatever the bar) */
    splash_progress(SPLASH_SCREENS_FROM + (100 - SPLASH_SCREENS_FROM) * done / all);
}

/* (documented in ui_internal.h) */
void splash_hide(void)
{
    if (s_box) {
        lv_obj_delete(s_box);
        s_box = s_bar = s_what = NULL;
    }
}

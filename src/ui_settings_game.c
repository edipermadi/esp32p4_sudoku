/**
 * @file ui_settings_game.c
 * @ingroup settings
 * @brief Settings > Game: timer, mistake check, no-notes challenge, left-handed, hints, tour.
 *
 * Most of them are read by the game screen when it next draws; Left-handed moves the board and pad now.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void timer_sw_cb(lv_event_t *e);
static void assist_dd_cb(lv_event_t *e);
static void no_notes_sw_cb(lv_event_t *e);
static void left_hand_sw_cb(lv_event_t *e);
static void hint_mode_dd_cb(lv_event_t *e);
static void tour_again_cb(lv_event_t *e);

static lv_obj_t *st_assist_dd;                      /**< the Mistake check dropdown */

/**
 * @brief Show timer: saved (the time is kept either way).
 * @param e  the LVGL event (its target is the control)
 */
static void timer_sw_cb(lv_event_t *e)
{
    store_set_show_timer(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}

/**
 * @brief Mistake check (Off, Rules, Solution): saved; the game redraws when shown.
 * @param e  the LVGL event (its target is the control)
 */
static void assist_dd_cb(lv_event_t *e)
{
    store_set_assist((uint8_t)lv_dropdown_get_selected(st_assist_dd));   /* the game redraws when shown */
}

/**
 * @brief No-notes challenge: saved, for games started from now.
 * @param e  the LVGL event (its target is the control)
 */
static void no_notes_sw_cb(lv_event_t *e)
{
    store_set_no_notes(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));   /* (games started from now) */
}

/**
 * @brief Left-handed: saved, and the game screen's board and pad swap sides now.
 * @param e  the LVGL event (its target is the control)
 */
static void left_hand_sw_cb(lv_event_t *e)
{
    store_set_left_hand(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    game_layout();                                  /* the game screen moves its board and pad now */
}

/**
 * @brief Hints (Full, Nudge only): saved.
 * @param e  the LVGL event (its target is the control)
 */
static void hint_mode_dd_cb(lv_event_t *e)
{
    store_set_hint_nudge(lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1);
}

/**
 * @brief Game screen tour, Show again: shown the next time a puzzle opens.
 * @param e  the LVGL event (its target is the control)
 */
static void tour_again_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("The tour needs sudoku.db on a microSD card.", 3000);
        return;
    }
    stats_db_set_seen("tour", false);
    sudoku_ui_notice("The tour shows the next time you open a puzzle.", 3000);
}

/* (documented in ui_settings.h) */
void settings_tab_game(lv_obj_t *pane)
{
    lv_obj_t *col = pane;
    lv_obj_t *r;
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
}

/**
 * @file ui_screens.c
 * @brief The screens: their registry (screen_home() and the others), the dark or light look applied by rebuilding them all, and the screens other modules draw (Wi-Fi, System monitor, Files).
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void sysmon_back_cb(lv_event_t *e);
static void files_back_cb(lv_event_t *e);

static bool s_dark = true;                          /**< the dark look is on (dark mode, or night mode now) */
static lv_obj_t *scr_home;                          /**< the Home screen */
static lv_obj_t *scr_list;                          /**< the Puzzles screen */
static lv_obj_t *scr_stats;                         /**< the Statistics screen */
static lv_obj_t *scr_settings;                      /**< the Settings screen */
static lv_obj_t *scr_wifi;                          /**< the Wi-Fi screen */
static lv_obj_t *scr_profile;                       /**< the Players screen */
static lv_obj_t *scr_history;                       /**< the History screen */
static lv_obj_t *scr_game;                          /**< the game screen */
static lv_obj_t *scr_sysmon;                        /**< the system monitor screen */
static lv_obj_t *scr_files;                         /**< the file browser screen */

/**
 * @brief The system monitor's back button: Home.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void sysmon_back_cb(lv_event_t *e)
{
    sysmon_ui_set_shown(false);
    lv_screen_load(scr_home);                       /* (opened from the Home menu) */
}

/**
 * @brief The file browser's back button: Home.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void files_back_cb(lv_event_t *e)
{
    file_ui_set_shown(false);
    lv_screen_load(scr_home);                       /* (opened from the Home menu) */
}

/**
 * @brief Builds the Wi-Fi screen (ui_wifi.c).
 */
void build_wifi(void)
{
    scr_wifi = make_screen();
    wifi_ui_create(scr_wifi, wifi_back_cb);
}

/**
 * @brief Builds the system monitor screen (ui_sysmon.c).
 */
void build_sysmon(void)
{
    scr_sysmon = make_screen();
    sysmon_ui_create(scr_sysmon, sysmon_back_cb);
}

/**
 * @brief Builds the file browser screen (ui_file.c).
 */
void build_files(void)
{
    scr_files = make_screen();
    file_ui_create(scr_files, files_back_cb);
}

/**
 * @brief Switches to the dark or light look and rebuilds every screen in it.
 *
 * Builds the palette and LVGL theme, rebuilds every screen around the current state (none of which lives
 * in the widgets), shows the new copy of the screen that was open (Home at start-up) and deletes the old
 * screens later (this may be running in one of their events).
 * @param[in] dark  the dark look (dark mode, or night mode now)
 */
void apply_theme(bool dark)
{
    s_dark = dark;
    palette_build(dark, store_accent());
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, C_ACCENT, C_ORANGE, dark, &lv_font_montserrat_16));

    lv_obj_t *old_home = scr_home, *old_list = scr_list, *old_settings = scr_settings, *old_game = scr_game;
    lv_obj_t *old_stats = scr_stats, *old_wifi = scr_wifi, *old_profile = scr_profile, *old_history = scr_history;
    lv_obj_t *old_plays = screen_plays(), *old_trophy = screen_trophy(), *old_sysmon = scr_sysmon, *old_files = scr_files;
    lv_obj_t *act = lv_screen_active();

    build_game();
    splash_built(1);                                /* (start-up: the loading bar) */
    build_list();
    splash_built(2);
    build_home();
    splash_built(3);
    build_stats();
    splash_built(4);
    build_settings();
    splash_built(5);
    build_wifi();
    splash_built(6);
    build_sysmon();
    splash_built(7);
    build_files();
    splash_built(8);
    build_profile();
    splash_built(9);
    build_history();
    splash_built(10);
    build_plays();
    splash_built(11);
    build_trophies();
    splash_built(12);
    if (game_id() != STORE_NO_GAME) {
        lv_obj_set_state(game_notes_btn(), LV_STATE_CHECKED, is_game_notes_mode());
        lv_obj_set_hidden(game_pause_ovl(), !is_game_paused());
        compute_conflicts();
        refresh_board();
        update_header();
    }
    refresh_list();
    refresh_home();
    refresh_stats();
    splash_built(13);
    lv_obj_t *to = act && act == old_game     ? scr_game
                   : act && act == old_stats    ? scr_stats
                   : act && act == old_wifi     ? scr_wifi
                   : act && act == old_sysmon   ? scr_sysmon
                   : act && act == old_files    ? scr_files
                   : act && act == old_profile  ? scr_profile
                   : act && act == old_history  ? scr_history
                   : act && act == old_plays    ? screen_plays()
                   : act && act == old_trophy   ? screen_trophy()
                   : act && act == old_list     ? scr_list
                   : act && act == old_settings ? scr_settings
                   : scr_home;
    lv_screen_load(to);
    if (to == screen_plays()) {
        plays_ask(gameplays_offset());    /* fill the rebuilt list */
    }
    if (to == screen_trophy()) {
        tr_ask();
    }

    /* the old screens may be running this very event: delete them later */
    if (old_home) {
        lv_obj_delete_async(old_home);
    }
    if (old_list) {
        lv_obj_delete_async(old_list);
    }
    if (old_settings) {
        lv_obj_delete_async(old_settings);
    }
    if (old_stats) {
        lv_obj_delete_async(old_stats);
    }
    if (old_wifi) {
        lv_obj_delete_async(old_wifi);
    }
    if (old_sysmon) {
        lv_obj_delete_async(old_sysmon);
    }
    if (old_files) {
        lv_obj_delete_async(old_files);
    }
    if (old_profile) {
        lv_obj_delete_async(old_profile);
    }
    if (old_history) {
        lv_obj_delete_async(old_history);
    }
    if (old_plays) {
        lv_obj_delete_async(old_plays);
    }
    if (old_trophy) {
        lv_obj_delete_async(old_trophy);
    }
    if (old_game) {
        lv_obj_delete_async(old_game);
    }
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @return the dark look is on (dark mode, or night mode now) */
bool is_ui_dark(void) { return s_dark; }

/** @return the Home screen */
lv_obj_t *screen_home(void) { return scr_home; }

/** @brief Sets the Home screen. @param[in] v  the value */
void screen_set_home(lv_obj_t *v) { scr_home = v; }

/** @return the Puzzles screen */
lv_obj_t *screen_list(void) { return scr_list; }

/** @brief Sets the Puzzles screen. @param[in] v  the value */
void screen_set_list(lv_obj_t *v) { scr_list = v; }

/** @return the Statistics screen */
lv_obj_t *screen_stats(void) { return scr_stats; }

/** @brief Sets the Statistics screen. @param[in] v  the value */
void screen_set_stats(lv_obj_t *v) { scr_stats = v; }

/** @return the Settings screen */
lv_obj_t *screen_settings(void) { return scr_settings; }

/** @return the Wi-Fi screen */
lv_obj_t *screen_wifi(void) { return scr_wifi; }

/** @return the Players screen */
lv_obj_t *screen_profile(void) { return scr_profile; }

/** @brief Sets the Players screen. @param[in] v  the value */
void screen_set_profile(lv_obj_t *v) { scr_profile = v; }

/** @return the History screen */
lv_obj_t *screen_history(void) { return scr_history; }

/** @brief Sets the History screen. @param[in] v  the value */
void screen_set_history(lv_obj_t *v) { scr_history = v; }

/** @return the game screen */
lv_obj_t *screen_game(void) { return scr_game; }

/** @brief Sets the game screen. @param[in] v  the value */
void screen_set_game(lv_obj_t *v) { scr_game = v; }

/** @return the system monitor screen */
lv_obj_t *screen_sysmon(void) { return scr_sysmon; }

/** @return the file browser screen */
lv_obj_t *screen_files(void) { return scr_files; }
/* (documented in ui_settings.h) */
void screen_set_settings(lv_obj_t *v) { scr_settings = v; }

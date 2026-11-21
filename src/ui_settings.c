/**
 * @file ui_settings.c
 * @ingroup settings
 * @brief Settings screen: the frame around the tabs, and what belongs to no tab.
 *
 * The title, the tab bar and a pane per tab (build_settings()), the rows' look (settings_row() and its
 * controls), and what isn't one tab's:
 * - the theme: apply_theme() rebuilds every screen in the new palette;
 * - settings reloaded from sudoku.db (settings_reload(), apply_loaded_prefs());
 * - the poll of sudoku.db's results for the whole UI (sd_poll_cb());
 * - power off (power_off_cb());
 * - the dark screen while the flash is written (sudoku_ui_flash_busy() and friends);
 * - the screens' registry (screen_home() and the other accessors).
 *
 * Each tab is a file of its own (see ui_settings.h). Part of the ui_sudoku.c split.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void power_off_confirm_cb(lv_event_t *e);
static void st_tab_cb(lv_event_t *e);
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

static bool u_reloading;                            /**< card back / reset: waiting for sudoku.db to (re)open */
static bool u_reload_home;                          /**< ... then show Home (factory reset) */
/* the Settings screen: its tabs (ST_*) and their panes */
static lv_obj_t *st_tab_btn[ST_TABS];               /**< the tab buttons */
static lv_obj_t *st_pane[ST_TABS];                  /**< each tab's pane (a scrolling column of rows) */
static int st_tab;                                  /**< the open tab; Settings opens on Network */

/* The screen is dark while the flash is written: any flash erase or write keeps the display
 * from its frame buffer (PSRAM) for a moment, and the panel flickers cyan (even sector by sector).
 * Backlight off and the panel asleep, it shows nothing instead. (Also the avatars' install.) */
static lv_obj_t *fb_ovl;                            /**< the card over everything, while busy */
static lv_timer_t *fb_keep;                         /**< keeps the screen timeout away meanwhile */
static bool fb_dark;                                /**< the screen is dark for the flash write */

/**
 * @brief Keeps the screen timeout from firing while the flash is written.
 * @param t  the timer
 */
static void fb_keep_cb(lv_timer_t *t) { lv_display_trigger_activity(NULL); }

/* (documented in ui_sudoku.h) */
void sudoku_ui_flash_busy(const char *title, const char *text)
{
    if (fb_ovl) {
        return;
    }
    fb_ovl = lv_obj_create(lv_layer_top());          /* nothing else meanwhile */
    lv_obj_remove_style_all(fb_ovl);
    lv_obj_set_size(fb_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(fb_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fb_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(fb_ovl, true);
    lv_obj_t *card = lv_obj_create(fb_ovl);
    lv_obj_set_size(card, 520, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(tl, title);
    lv_obj_t *m = make_label(card, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_label_set_text(m, text);
    lv_obj_t *w = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(w, "Don't switch off or take the card out.");
    fb_keep = lv_timer_create(fb_keep_cb, 1000, NULL);
}

/**
 * @brief Turns the screen off (backlight, then the panel asleep) for the flash write.
 *
 * Writing the flash keeps the display from its frame buffer, so the panel would flicker cyan.
 */
void sudoku_ui_flash_dark(void)
{
    if (fb_dark) {
        return;
    }
    fb_dark = true;
    lv_refr_now(NULL);
    bsp_display_brightness_set(0);
    bsp_display_sleep(true);
}

/**
 * @brief Removes the card and, if it was dark, wakes the panel and restores the backlight.
 */
void sudoku_ui_flash_done(void)
{
    if (fb_ovl) {
        lv_obj_delete(fb_ovl);
    }
    fb_ovl = NULL;
    if (fb_keep) {
        lv_timer_delete(fb_keep);
    }
    fb_keep = NULL;
    if (!fb_dark) {
        return;
    }
    fb_dark = false;
    lv_refr_now(NULL);                              /* (drawn without the card before the light comes on) */
    bsp_display_sleep(false);
    bsp_display_brightness_set(effective_brightness());
}

/* (documented in ui_settings.h) */
lv_obj_t *settings_dropdown(lv_obj_t *row, const char *options, uint32_t sel, int width, lv_event_cb_t cb)
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

/**
 * @brief The UI's poll of sudoku.db's results (an LVGL timer).
 *
 * Takes what the database task finished: statistics, the RAM copy of a player's results, players, imports,
 * messages, reloaded settings; and follows the Data tab's card work (settings_data_poll_usb(),
 * settings_data_poll_card()).
 * @param t  the timer
 */
void sd_poll_cb(lv_timer_t *t)
{
    /* sudoku.db results */
    static stats_view_t *view;                             /* PSRAM */
    if (!view) {
        view = heap_caps_malloc(sizeof(*view), MALLOC_CAP_SPIRAM);
    }
    if (view && stats_db_take_view(view) && view->level == statistics_sel() - 1) {
        stats_take_view(view);
    }
    static uint16_t c_best[PUZZLE_COUNT];
    static uint8_t c_fewest[PUZZLE_COUNT], c_solves[PUZZLE_COUNT];
    if (stats_db_take_cache(c_best, c_fewest, c_solves)) {
        store_reset_stats();                               /* sudoku.db -> RAM copy (this player's) */
        store_merge_stats(c_best, c_fewest, c_solves);
        lv_obj_t *act = lv_screen_active();
        if (act == scr_home) {
            refresh_home();
        } else if (act == scr_list) {
            refresh_list();
        } else if (act == scr_stats) {
            refresh_stats();
        }
    }

    settings_data_poll_usb();                              /* the card back from the computer? */
    static store_prefs_t prefs;
    if (u_reloading && stats_db_take_prefs(&prefs)) {
        /* sudoku.db is back (perhaps a restored copy, or rebuilt): take everything from it */
        u_reloading = false;
        store_apply_prefs(&prefs);
        apply_loaded_prefs();
    }
    if (scr_history) {
        history_poll();
    }
    if (screen_plays()) {
        plays_poll();
    }
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
            game_set_id(STORE_NO_GAME);                          /* the game screen held someone else's */
            char msg[64];
            snprintf(msg, sizeof(msg), "Now playing: %s", ch.name);
            sudoku_ui_notice(msg, 2500);
        }
        refresh_home();
        render_players();
        if (lv_screen_active() == screen_trophy()) {
            tr_ask();    /* now this player's */
        }
        if (lv_screen_active() == scr_stats) {
            refresh_stats();    /* now this player's */
        } else {
            stats_player_dd_update();
        }
    }
    static stats_player_t list[STATS_MAX_PLAYERS];
    int n = stats_db_take_players(list);
    if (n >= 0) {
        players_take(list, n);
        stats_player_dd_update();
    }
    char msg[96];
    if (stats_db_take_message(msg, sizeof(msg))) {
        sudoku_ui_notice(msg, 4000);
    }

    settings_data_poll_card();                             /* Format's steps (Settings > Data) */
}

/* (documented in ui_settings.h) */
void settings_reload(bool from_db, bool to_home)
{
    if (to_home) {
        u_reload_home = true;
    }
    if (from_db) {
        u_reloading = true;
    } else {
        apply_loaded_prefs();
    }
}

/**
 * @brief Applies settings that changed under the UI (a restored sudoku.db, a factory reset).
 *
 * Sound, volume, brightness, time zone, screen timeout, the collection shown, then every screen
 * rebuilt in the theme (apply_theme()) and night mode decided again; Home after a factory reset.
 */
void apply_loaded_prefs(void)
{
    game_set_id(STORE_NO_GAME);
    players_set_count(-1);
    sfx_set_enabled(is_store_sound_enabled());
    gibberlink_set_enabled(is_store_gibberlink());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(effective_brightness());
    tz_apply(store_timezone());
    ui_set_sleep_min(store_screen_timeout_min());
    puzzles_set_coll(store_last_collection());
    puzzles_set_tab(coll_tab(puzzles_coll()));
    night_forget();                                    /* (re-checked just below, from these settings) */
    apply_theme(is_store_dark_mode());                    /* redraws every screen with it */
    night_check(NULL);
    if (u_reload_home) {
        u_reload_home = false;
        refresh_home();
        lv_screen_load(scr_home);
    }
}

/* ------------------------------------------------------------- power off */

/**
 * @brief Turn off, confirmed: black, backlight off, then device_power_off() (never returns).
 * @param e  the event (its user data is the dialog)
 */
static void power_off_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    /* black first, so nothing half-drawn shows while the light goes out */
    lv_obj_t *black = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(black);
    lv_obj_set_size(black, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(black, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(black, LV_OPA_COVER, 0);
    lv_refr_now(NULL);
    bsp_display_brightness_set(0);
    vTaskDelay(pdMS_TO_TICKS(150));                /* the confirm tap's tick finishes */
    device_power_off();                            /* sudoku.db closed, then deep sleep until RESET */
}

/**
 * @brief Home's power button: asks before turning the board off.
 * @param e  the LVGL event (its target is the control)
 */
void power_off_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Turn off?",
                              "The screen, sound and Wi-Fi turn off and the board goes into deep sleep. "
                              "Your game in progress is kept. Press the RESET button on the board to turn it back on.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_POWER "  Turn off", power_off_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

/**
 * @brief Shows a tab of Settings and hides the others.
 * @param tab  ST_NETWORK .. ST_DATA
 */
void st_select_tab(int tab)
{
    st_tab = tab;
    if (tab == ST_DATA) {
        settings_data_shown();    /* (a new update.bin since: copied over USB) */
    }
    for (int i = 0; i < ST_TABS; i++) {
        lv_obj_set_state(st_tab_btn[i], LV_STATE_CHECKED, i == tab);
        lv_obj_set_style_text_color(lv_obj_get_child(st_tab_btn[i], 0), i == tab ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(st_pane[i], i != tab);
    }
    lv_obj_scroll_to_y(st_pane[tab], 0, LV_ANIM_OFF);
}

/**
 * @brief A tab button: shows its tab.
 * @param e  the event (its user data is the tab)
 */
static void st_tab_cb(lv_event_t *e)
{
    st_select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

/**
 * @brief The system monitor's back button: Home.
 * @param e  the LVGL event (its target is the control)
 */
static void sysmon_back_cb(lv_event_t *e)
{
    sysmon_ui_set_shown(false);
    lv_screen_load(scr_home);                       /* (opened from the Home menu) */
}

/**
 * @brief The file browser's back button: Home.
 * @param e  the LVGL event (its target is the control)
 */
static void files_back_cb(lv_event_t *e)
{
    file_ui_set_shown(false);
    lv_screen_load(scr_home);                       /* (opened from the Home menu) */
}

/* (documented in ui_settings.h) */
lv_obj_t *settings_row(lv_obj_t *parent, const char *title, const char *hint)
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

/* (documented in ui_settings.h) */
lv_obj_t *settings_switch(lv_obj_t *row, bool on, lv_event_cb_t cb)
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

/**
 * @brief Builds the Settings screen: title, tab bar, a pane per tab, each tab's rows.
 *
 * Called at start-up and by apply_theme(); a rebuild keeps the tab that was open.
 */
void build_settings(void)
{
    scr_settings = make_screen();

    make_btn(scr_settings, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_settings, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Settings");
    lv_obj_set_pos(t, 150, 20);

    /* the tabs, under the title (Network first, and the one Settings opens on) */
    static const char *const TABS[ST_TABS] = {LV_SYMBOL_WIFI "  Network", LV_SYMBOL_IMAGE "  Display",
                                              LV_SYMBOL_VOLUME_MAX "  Sound", LV_SYMBOL_EDIT "  Game",
                                              LV_SYMBOL_SD_CARD "  Data"
                                             };
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
    settings_tab_network(st_pane[ST_NETWORK]);
    settings_tab_display(st_pane[ST_DISPLAY]);
    settings_tab_sound(st_pane[ST_SOUND]);
    settings_tab_game(st_pane[ST_GAME]);
    settings_tab_data(st_pane[ST_DATA]);

    st_select_tab(st_tab);                          /* (a rebuild keeps the tab that was open) */
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

/* (documented in ui_settings.h) */
lv_obj_t *settings_slider(lv_obj_t *row, int min, int max, int value, lv_event_cb_t cb, lv_obj_t **lbl)
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

/**
 * @brief Switches to the dark or light look and rebuilds every screen in it.
 *
 * Builds the palette and LVGL theme, rebuilds every screen around the current state (none of which lives
 * in the widgets), shows the new copy of the screen that was open (Home at start-up) and deletes the old
 * screens later (this may be running in one of their events).
 * @param dark  the dark look (dark mode, or night mode now)
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
/** @brief Sets the Home screen. @param v  the value */
void screen_set_home(lv_obj_t *v) { scr_home = v; }
/** @return the Puzzles screen */
lv_obj_t *screen_list(void) { return scr_list; }
/** @brief Sets the Puzzles screen. @param v  the value */
void screen_set_list(lv_obj_t *v) { scr_list = v; }
/** @return the Statistics screen */
lv_obj_t *screen_stats(void) { return scr_stats; }
/** @brief Sets the Statistics screen. @param v  the value */
void screen_set_stats(lv_obj_t *v) { scr_stats = v; }
/** @return the Settings screen */
lv_obj_t *screen_settings(void) { return scr_settings; }
/** @return the Wi-Fi screen */
lv_obj_t *screen_wifi(void) { return scr_wifi; }
/** @return the Players screen */
lv_obj_t *screen_profile(void) { return scr_profile; }
/** @brief Sets the Players screen. @param v  the value */
void screen_set_profile(lv_obj_t *v) { scr_profile = v; }
/** @return the History screen */
lv_obj_t *screen_history(void) { return scr_history; }
/** @brief Sets the History screen. @param v  the value */
void screen_set_history(lv_obj_t *v) { scr_history = v; }
/** @return the game screen */
lv_obj_t *screen_game(void) { return scr_game; }
/** @brief Sets the game screen. @param v  the value */
void screen_set_game(lv_obj_t *v) { scr_game = v; }
/** @return the system monitor screen */
lv_obj_t *screen_sysmon(void) { return scr_sysmon; }
/** @return the file browser screen */
lv_obj_t *screen_files(void) { return scr_files; }

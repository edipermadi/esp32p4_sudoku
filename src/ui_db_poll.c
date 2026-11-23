/**
 * @file ui_db_poll.c
 * @brief The UI's poll of sudoku.db's results (statistics, players, imports, messages), and settings reloaded from it.
 */
#include "ui_settings.h"

static bool u_reloading;                            /**< card back / reset: waiting for sudoku.db to (re)open */
static bool u_reload_home;                          /**< ... then show Home (factory reset) */

/**
 * @brief The UI's poll of sudoku.db's results (an LVGL timer).
 *
 * Takes what the database task finished: statistics, the RAM copy of a player's results, players, imports,
 * messages, reloaded settings; and follows the Data tab's card work (settings_data_poll_usb(),
 * settings_data_poll_card()).
 * @param[in] t  the timer
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
        if (act == screen_home()) {
            refresh_home();
        } else if (act == screen_list()) {
            refresh_list();
        } else if (act == screen_stats()) {
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
    if (screen_history()) {
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
        if (lv_screen_active() == screen_stats()) {
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
        lv_screen_load(screen_home());
    }
}

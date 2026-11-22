/**
 * @file ui_sudoku.c
 * @ingroup ui_sudoku
 * @brief The UI's start-up (sudoku_ui_start()): the card and sudoku.db read, the settings applied,
 *        the screens built, the timers started. The rest of the UI is a file per screen
 *        (ui_*.c, sharing ui_internal.h); what serial control asks of it: ui_remote.c.
 *
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
 *   - wifi: on/off, nearby networks, password keyboard (ui_wifi.c).
 *   - system monitor: heap, flash, microSD, CPU temperature and load, LVGL
 *     stats and the task list (ui_sysmon.c; Settings > Data).
 *   - files: the microSD card's folders and files, rename and delete; the
 *     database is protected (ui_file.c; Settings > Data).
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

/**
 * @brief Every touch: a tick (not the waking tap).
 * @param[in] e  the event
 */
static void touch_sound_cb(lv_event_t *e)
{
    if (ui_saver()) {
        return;    /* waking tap is silent */
    }
    sfx_play(SFX_TOUCH);
}

/* (documented in ui_sudoku.h) */
esp_err_t sudoku_ui_start(void)
{
    game_set_undo(heap_caps_calloc(UNDO_MAX, sizeof(board_t), MALLOC_CAP_SPIRAM));
    game_set_redo(heap_caps_calloc(UNDO_MAX, sizeof(board_t), MALLOC_CAP_SPIRAM));
    if (!game_undo() || !game_redo()) {
        return ESP_ERR_NO_MEM;
    }
    sudoku_geom_init();                             /* before the generator task can use the tables */
    game_set_geo(sudoku_geom(VAR_CLASSIC));
    game_set_geo_buf(heap_caps_malloc(sizeof(*game_geo_buf()), MALLOC_CAP_SPIRAM));
    ui_set_gen_cache(heap_caps_calloc(GEN_CACHE, sizeof(gen_cache_t), MALLOC_CAP_SPIRAM));
    if (!game_geo_buf() || !ui_gen_cache() || !statistics_init()) {
        return ESP_ERR_NO_MEM;
    }
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(NULL);
#endif

    /* the splash screen while the card and sudoku.db are read and the screens built (two seconds or
     * so); just text if the picture can't be decoded */
    lv_obj_t *boot = lv_screen_active();
    lv_obj_set_style_bg_color(boot, lv_color_hex(ui_palette_dark()->bg), 0);
    lv_obj_set_style_bg_opa(boot, LV_OPA_COVER, 0);
    lv_obj_t *splash = splash_show(boot);
    const bool picture = splash != NULL;
    if (!picture) {
        splash = lv_label_create(boot);
        lv_label_set_text(splash, "Sudoku\n\nStarting...");
        lv_obj_set_style_text_align(splash, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(splash, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(splash, lv_color_hex(ui_palette_dark()->text), 0);
        lv_obj_center(splash);
    }
    lv_refr_now(NULL);

    /* The settings and the game in progress live in sudoku.db, so the card
     * comes first. Card setup also has to happen before Wi-Fi starts (they
     * share the SDMMC host, see the microSD section). */
    sd_probe_card();
    splash_progress(SPLASH_CARD);
    if (is_sd_present()) {
        char sz[24];
        fmt_gb(sz, sizeof(sz), sd_bytes());
        printf("microSD: \"%s\", %s\n", sd_name(), sz);
        stats_db_start();
        /* up to 4 s, in short waits: the loading bar creeps toward SPLASH_DB_OPEN meanwhile (about a
         * second, on the board), easing off so it never gets there before the database does */
        const int64_t t0 = esp_timer_get_time();
        bool open = false;
        for (int64_t ms = 0; ms < 4000; ms = (esp_timer_get_time() - t0) / 1000) {
            if (stats_db_wait_open(40)) {
                open = true;
                break;
            }
            if (!is_stats_db_opening()) {
                break;                              /* (failed; or just opened: is_stats_db_ready() says) */
            }
            splash_progress(SPLASH_CARD + (int)((SPLASH_DB_OPEN - SPLASH_CARD) * ms / (ms + 200)));
        }
        if (open || is_stats_db_ready()) {
            store_prefs_t prefs;
            if (stats_db_take_prefs(&prefs)) {
                store_apply_prefs(&prefs);
            }
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
    if (is_sd_present() && usb_msc_start() != ESP_OK) {   /* empty drive until Connect; tells us a computer is there */
        printf("usb_msc: USB didn't start\n");
    }
    wifi_mgr_start();               /* reconnects to the saved network if Wi-Fi was left on */

    sfx_set_enabled(is_store_sound_enabled());
    gibberlink_set_enabled(is_store_gibberlink());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(effective_brightness());
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        lv_indev_add_event_cb(in, touch_sound_cb, LV_EVENT_PRESSED, NULL);
    }
    tz_apply(store_timezone());
    puzzles_set_coll(store_last_collection());
    puzzles_set_tab(coll_tab(puzzles_coll()));
    ui_set_sleep_min(store_screen_timeout_min());
    splash_progress(SPLASH_SCREENS_FROM);
    apply_theme(is_store_dark_mode());              /* (the loading bar moves on with each screen: splash_built()) */
    if (picture) {
        splash_hide();                              /* (its pixels freed too) */
    } else {
        lv_obj_delete(splash);
    }

    game_set_last_tick(lv_tick_get());
    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(saver_cb, 1000, NULL);
    lv_timer_create(clock_cb, 1000, NULL);
    lv_timer_create(night_check, 20000, NULL);         /* night mode: dark and dimmer at night */
    lv_timer_create(sd_poll_cb, 100, NULL);
    clock_cb(NULL);
    return ESP_OK;
}

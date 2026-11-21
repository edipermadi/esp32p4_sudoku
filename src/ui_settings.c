/* sudoku_ui.c split: Settings, and its card and firmware actions: update, microSD format, factory reset, power off, USB drive. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void left_hand_sw_cb(lv_event_t *e);
static void no_notes_sw_cb(lv_event_t *e);
static void cvd_sw_cb(lv_event_t *e);
static void night_sw_cb(lv_event_t *e);
static void night_hours_cb(lv_event_t *e);
static void night_bright_cb(lv_event_t *e);
static void saver_clock_sw_cb(lv_event_t *e);
static void wifi_open_cb(lv_event_t *e);
static const char *tz_dropdown_options(void);
static void tz_show_selected(void);
static void tz_dd_cb(lv_event_t *e);
static void gibberlink_sw_cb(lv_event_t *e);
static void sound_sw_cb(lv_event_t *e);
static void bright_sl_cb(lv_event_t *e);
static void vol_sl_cb(lv_event_t *e);
static lv_obj_t *settings_slider(lv_obj_t *row, int min, int max, int value, lv_event_cb_t cb, lv_obj_t **lbl);
static bool fw_file_desc(esp_app_desc_t *d, uint32_t *size);
static void fw_build_id(const esp_app_desc_t *d, char out[9]);
static void fw_row_update(void);
static void fw_task(void *arg);
static void fw_poll_cb(lv_timer_t *t);
static void fw_go_cb(lv_event_t *e);
static void fw_update_cb(lv_event_t *e);
static void timer_sw_cb(lv_event_t *e);
static void import_cb(lv_event_t *e);
static void export_cb(lv_event_t *e);
static void backup_cb(lv_event_t *e);
static void accent_dd_cb(lv_event_t *e);
static void assist_dd_cb(lv_event_t *e);
static lv_obj_t *settings_dropdown(lv_obj_t *row, const char *options, uint32_t sel, int width, lv_event_cb_t cb);
static void sleep_dd_cb(lv_event_t *e);
static void sd_task(void *arg);
static bool sd_run(sd_job_t job);
static void sd_btn_text(const char *t, bool enabled);
static void sd_format_confirm_cb(lv_event_t *e);
static void sd_format_cancel_cb(lv_event_t *e);
static void sd_format_cb(lv_event_t *e);
static void factory_reset_confirm_cb(lv_event_t *e);
static void factory_reset_cb(lv_event_t *e);
static void power_off_confirm_cb(lv_event_t *e);
static void usb_done_cb(lv_event_t *e);
static void usb_connect_cb(lv_event_t *e);
static void theme_sw_cb(lv_event_t *e);
static void st_tab_cb(lv_event_t *e);
static void st_close_lists_cb(lv_event_t *e);
static void hint_mode_dd_cb(lv_event_t *e);
static void tour_again_cb(lv_event_t *e);
static void sysmon_back_cb(lv_event_t *e);
static void files_back_cb(lv_event_t *e);
static lv_obj_t *settings_row(lv_obj_t *parent, const char *title, const char *hint);
static lv_obj_t *settings_switch(lv_obj_t *row, bool on, lv_event_cb_t cb);

/* "Screen off" dropdown choices, in minutes (0 = never) */
static const uint8_t SLEEP_MIN[] = {1, 2, 5, 10, 15, 30, 0};
static const char *const ACCENT_NAMES = "Blue\nTeal\nGreen\nPurple\nPink";

/* Night mode (Settings > Display): between the night hours, dark mode and the backlight
 * at most the night brightness. Only the switch-over changes things (night_check), so
 * dark mode or brightness changed by hand during the night stay as they are until morning.
 * s_night: the night look is on now. */
static bool s_night;
static bool s_dark = true;
static uint8_t s_sleep_min = 5;       /* screen timeout, 0 = never */

static lv_obj_t *scr_home, *scr_list, *scr_stats, *scr_settings, *scr_wifi, *scr_profile, *scr_history, *scr_game;
static lv_obj_t *scr_sysmon, *scr_files;

/* settings screen */
static lv_obj_t *st_theme_sw, *st_sound_sw, *st_sleep_dd, *st_wifi_lbl, *st_tz_dd;
static lv_obj_t *st_wifi_eye;               /* shows the network's name (hidden until tapped) */
static lv_obj_t *st_lock_dd, *st_pin_btn;   /* the lock screen: Off / Swipe / PIN, and Change PIN */
static lv_obj_t *st_night_bright_lbl;

static lv_obj_t *st_night_from, *st_night_to;
static lv_obj_t *st_bright_sl, *st_bright_lbl, *st_vol_sl, *st_vol_lbl, *st_assist_dd;
static char      st_tz_txt[48];     /* closed dropdown shows just the IANA name */
static lv_obj_t *st_sd_btn, *st_sd_hint;
static lv_obj_t *u_ovl;                   /* "USB drive" panel while the computer has the card */
static lv_obj_t *st_usb_btn, *st_usb_hint;
static bool u_reloading;                  /* card back / reset: waiting for sudoku.db to (re)open */
static bool u_reload_home;                /* ... then show Home (factory reset) */
static lv_obj_t *st_tab_btn[ST_TABS], *st_pane[ST_TABS];
static int st_tab;                                  /* the open tab; Settings opens on Network */

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

static void backup_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("Backup needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Backing up...", 1500);
    stats_db_backup();                              /* the result comes back as a message */
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
static volatile sd_state_t s_sd_state = SD_IDLE;
static volatile bool s_sd_present;
static sd_state_t s_sd_seen = SD_IDLE;
static uint64_t   s_sd_bytes;
static char       s_sd_name[8];

/* is a card there: its size and name (at start-up, before Wi-Fi) */
void sd_probe_card(void)
{
    s_sd_present = bsp_sdcard_probe(&s_sd_bytes, s_sd_name) == ESP_OK;
}
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

void fmt_gb(char *buf, size_t n, uint64_t bytes)
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

void sd_poll_cb(lv_timer_t *t)
{
    /* sudoku.db results */
    static stats_view_t *view;                             /* PSRAM */
    if (!view) view = heap_caps_malloc(sizeof(*view), MALLOC_CAP_SPIRAM);
    if (view && stats_db_take_view(view) && view->level == stats_sel() - 1) {
        stats_take_view(view);
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
    if (screen_plays()) plays_poll();
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
        if (lv_screen_active() == screen_trophy()) tr_ask();          /* now this player's */
        if (lv_screen_active() == scr_stats) refresh_stats();   /* now this player's */
        else stats_player_dd_update();
    }
    static stats_player_t list[STATS_MAX_PLAYERS];
    int n = stats_db_take_players(list);
    if (n >= 0) {
        players_take(list, n);
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
void apply_loaded_prefs(void)
{
    game_set_id(STORE_NO_GAME);
    players_set_count(-1);
    sfx_set_enabled(store_sound_enabled());
    gibberlink_set_enabled(store_gibberlink());
    bsp_audio_set_volume(store_volume());
    bsp_display_brightness_set(effective_brightness());
    tz_apply(store_timezone());
    s_sleep_min = store_screen_timeout_min();
    list_set_coll(store_last_collection());
    list_set_tab(coll_tab(list_coll()));
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

void power_off_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Turn off?",
        "The screen, sound and Wi-Fi turn off and the board goes into deep sleep. "
        "Your game in progress is kept. Press the RESET button on the board to turn it back on.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_POWER "  Turn off", power_off_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

/* ------------------------------------------------------------- USB drive */

void usb_finish(void)
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
void usb_btn_update(void)
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

void st_select_tab(int tab)
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
void night_check(lv_timer_t *t)
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
    if (!ui_saver()) bsp_display_brightness_set(effective_brightness());   /* (off stays off) */
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

void big_digits_dd_cb(lv_event_t *e)
{
    store_set_big_digits(lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1);
    for (int i = 0; i < 81; i++) {                  /* the game's board now (Replay: when it opens) */
        if (game_cell_lbl()[i]) lv_obj_set_style_text_font(game_cell_lbl()[i], DIGIT_FONT_N(game_geo() ? game_geo()->n : 9), 0);
        if (game_cells()[i]) lv_obj_invalidate(game_cells()[i]);   /* (the pencil marks too) */
    }
}

static void sysmon_back_cb(lv_event_t *e)
{
    sysmon_ui_set_shown(false);
    lv_screen_load(scr_home);                       /* (opened from the Home menu) */
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

void build_settings(void)
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

    r = settings_row(col, "Back up database", "A copy of sudoku.db: backup/ on the card");
    lv_obj_t *bk = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_SAVE "  Back up", &lv_font_montserrat_18, backup_cb, NULL);
    lv_obj_set_style_bg_color(bk, C_CARD2, 0);
    lv_obj_align(bk, LV_ALIGN_RIGHT_MID, 0, 0);

    r = settings_row(col, "Factory reset", "Settings, Wi-Fi network, players and statistics");
    lv_obj_t *fr = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_WARNING "  Reset", &lv_font_montserrat_18,
                            factory_reset_cb, NULL);
    lv_obj_set_style_bg_color(fr, C_RED, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(fr, 0), lv_color_white(), 0);
    lv_obj_align(fr, LV_ALIGN_RIGHT_MID, 0, 0);

    st_select_tab(st_tab);                          /* (a rebuild keeps the tab that was open) */
}

void build_wifi(void)
{
    scr_wifi = make_screen();
    wifi_ui_create(scr_wifi, wifi_back_cb);
}

void build_sysmon(void)
{
    scr_sysmon = make_screen();
    sysmon_ui_create(scr_sysmon, sysmon_back_cb);
}

void build_files(void)
{
    scr_files = make_screen();
    file_ui_create(scr_files, files_back_cb);
}

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

static void wifi_open_cb(lv_event_t *e)
{
    lv_screen_load(scr_wifi);
    wifi_ui_set_shown(true);
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

/* Switches palette and LVGL theme, then rebuilds every screen around the
 * current state (all of which lives outside the widgets) and shows the
 * rebuilt copy of the screen that was open (home at start-up). */
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
        lv_obj_set_state(game_notes_btn(), LV_STATE_CHECKED, game_notes_mode());
        lv_obj_set_hidden(game_pause_ovl(), !game_paused());
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
    if (to == screen_plays()) plays_ask(plays_offset());         /* fill the rebuilt list */
    if (to == screen_trophy()) tr_ask();

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

/* ---- accessors: the other UI files reach this file's state through these */
const uint8_t *sleep_minutes(void) { return SLEEP_MIN; }
bool ui_night(void) { return s_night; }
uint8_t ui_sleep_min(void) { return s_sleep_min; }
void ui_set_sleep_min(uint8_t v) { s_sleep_min = v; }
lv_obj_t *screen_home(void) { return scr_home; }
void screen_set_home(lv_obj_t *v) { scr_home = v; }
lv_obj_t *screen_list(void) { return scr_list; }
void screen_set_list(lv_obj_t *v) { scr_list = v; }
lv_obj_t *screen_stats(void) { return scr_stats; }
void screen_set_stats(lv_obj_t *v) { scr_stats = v; }
lv_obj_t *screen_settings(void) { return scr_settings; }
lv_obj_t *screen_wifi(void) { return scr_wifi; }
lv_obj_t *screen_profile(void) { return scr_profile; }
void screen_set_profile(lv_obj_t *v) { scr_profile = v; }
lv_obj_t *screen_history(void) { return scr_history; }
void screen_set_history(lv_obj_t *v) { scr_history = v; }
lv_obj_t *screen_game(void) { return scr_game; }
void screen_set_game(lv_obj_t *v) { scr_game = v; }
lv_obj_t *screen_sysmon(void) { return scr_sysmon; }
lv_obj_t *screen_files(void) { return scr_files; }
lv_obj_t *settings_wifi_lbl(void) { return st_wifi_lbl; }
lv_obj_t *settings_wifi_eye(void) { return st_wifi_eye; }
lv_obj_t *settings_lock_dd(void) { return st_lock_dd; }
lv_obj_t *settings_pin_btn(void) { return st_pin_btn; }
lv_obj_t *settings_sd_hint(void) { return st_sd_hint; }
bool ui_sd_present(void) { return s_sd_present; }
uint64_t ui_sd_bytes(void) { return s_sd_bytes; }
const char *ui_sd_name(void) { return s_sd_name; }

/* card work Settings is doing: a firmware update, probing or formatting the card, the card
 * lent to a computer (USB drive); NULL when none (devctl genbench) */
const char *settings_card_busy(void)
{
    if (fw_timer) return "a firmware update is being written";
    if (s_sd_state == SD_PROBING || s_sd_state == SD_FORMATTING) return "the microSD card is being set up or formatted";
    if (u_ovl) return "the card is lent to a computer (USB drive)";
    return NULL;
}

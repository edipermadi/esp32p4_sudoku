/**
 * @file ui_settings_data.c
 * @ingroup settings
 * @brief Settings > Data: microSD card, USB drive, firmware update, import, export, backup, factory reset.
 *
 * The rows and their dialogs. What they do to the board (the card format task, the firmware write, the
 * factory reset, lending the card over USB) is in the settings core (settings_core.h); this file follows those through their
 * state (sd_state(), fw_update_state()) and shows the results.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void fw_row_update(void);
static void fw_poll_cb(lv_timer_t *t);
static void fw_start_cb(lv_timer_t *t);
static void fw_go_cb(lv_event_t *e);
static void fw_update_cb(lv_event_t *e);
static void sd_btn_text(const char *t, bool enabled);
static void sd_format_confirm_cb(lv_event_t *e);
static void sd_format_cancel_cb(lv_event_t *e);
static void sd_format_cb(lv_event_t *e);
static void usb_done_cb(lv_event_t *e);
static void usb_connect_cb(lv_event_t *e);
static void import_cb(lv_event_t *e);
static void export_cb(lv_event_t *e);
static void backup_cb(lv_event_t *e);
static void factory_reset_confirm_cb(lv_event_t *e);
static void factory_reset_cb(lv_event_t *e);

static lv_obj_t *st_sd_btn;                         /**< the microSD row's Format button */
static lv_obj_t *st_sd_hint;                        /**< the microSD row's hint: sudoku.db's status */
static sd_state_t s_sd_seen = SD_IDLE;              /**< the card state settings_data_poll_card has shown */
static lv_obj_t *s_sd_busy;                         /**< "Formatting..." box */
static lv_obj_t *u_ovl;                             /**< "USB drive" panel while the computer has the card */
static lv_obj_t *st_usb_btn;                        /**< the USB drive row's Connect button */
static lv_obj_t *st_usb_hint;                       /**< the USB drive row's hint */
static lv_obj_t *st_fw_btn;                         /**< the Firmware update row's Update button */
static lv_obj_t *st_fw_hint;                        /**< the Firmware update row's hint: the builds */
static lv_timer_t *fw_timer;                        /**< the card's wait, then following the write */

/**
 * @brief The Firmware update row: the running build, and update.bin's if there is one.
 *
 * Update is greyed out without a usable update.bin.
 */
static void fw_row_update(void)
{
    if (!st_fw_hint) {
        return;
    }
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

/**
 * @brief Follows the firmware write: restarts into it when done, or brings the screen back with the error.
 * @param t  the timer
 */
static void fw_poll_cb(lv_timer_t *t)
{
    fw_state_t st = fw_update_state();
    if (st == FW_WRITING) {
        return;
    }
    lv_timer_delete(fw_timer);
    fw_timer = NULL;
    if (st == FW_FAILED) {
        sudoku_ui_flash_done();
        sudoku_ui_notice(fw_update_error(), 6000);
        fw_row_update();
        return;
    }
    fw_restart_into_new();                          /* (the screen comes back with the new firmware) */
}

/**
 * @brief After the card has been on screen a moment: the screen dark, then the write starts.
 * @param t  the timer (its user data is update.bin's size)
 */
static void fw_start_cb(lv_timer_t *t)
{
    sudoku_ui_flash_dark();
    if (!fw_update_start((uint32_t)(uintptr_t)lv_timer_get_user_data(t))) {
        fw_timer = NULL;
        sudoku_ui_flash_done();
        sudoku_ui_notice("Couldn't start the update.", 4000);
        return;
    }
    fw_timer = lv_timer_create(fw_poll_cb, 200, NULL);
}

/**
 * @brief Update, confirmed: the card that says the screen goes dark, then (fw_start_cb()) the write.
 * @param e  the event (its user data is the dialog)
 */
static void fw_go_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    esp_app_desc_t d;
    uint32_t size;
    if (fw_timer || !fw_file_desc(&d, &size)) {
        return;
    }
    char text[160];
    snprintf(text, sizeof(text), "The screen goes dark while the firmware is written (about %u seconds), then "
             "the board restarts into it by itself.", (unsigned)(size / (100 * 1024)) + 5);
    sudoku_ui_flash_busy(LV_SYMBOL_DOWNLOAD "  Updating the firmware", text);
    fw_timer = lv_timer_create(fw_start_cb, 3000, (void *)(uintptr_t)size);
    lv_timer_set_repeat_count(fw_timer, 1);
}

/**
 * @brief The Update button: asks first, with the running build and update.bin's.
 * @param e  the LVGL event (its target is the control)
 */
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

/**
 * @brief The microSD row's button: its text, and whether it can be tapped.
 * @param t        the text
 * @param enabled  false: greyed out
 */
static void sd_btn_text(const char *t, bool enabled)
{
    if (!st_sd_btn) {
        return;
    }
    lv_label_set_text(lv_obj_get_child(st_sd_btn, 0), t);
    lv_obj_set_state(st_sd_btn, LV_STATE_DISABLED, !enabled);
}

/**
 * @brief Format, confirmed: the card task starts, and a "Formatting..." box shows.
 * @param e  the event (its user data is the dialog)
 */
static void sd_format_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (!sd_run(SD_JOB_FORMAT)) {
        sudoku_ui_notice("Could not start formatting (out of memory).", 5000);
        return;
    }
    s_sd_busy = make_msgbox("Formatting microSD card...", "Don't remove the card. This takes a few seconds.");
}

/**
 * @brief Format, cancelled.
 * @param e  the event (its user data is the dialog)
 */
static void sd_format_cancel_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    sd_set_state(SD_IDLE);
}

/**
 * @brief The Format button: looks for a card if none was found at start-up, then asks.
 *
 * A card can only be looked for while Wi-Fi hasn't started (they share the SDMMC host).
 * @param e  the LVGL event (its target is the control)
 */
static void sd_format_cb(lv_event_t *e)
{
    if (sd_state() != SD_IDLE) {
        return;
    }
    if (sd_present()) {
        sd_set_state(SD_FOUND);                    /* found at boot: straight to the question */
    } else if (!wifi_mgr_radio_started()) {
        if (sd_run(SD_JOB_PROBE)) {
            sd_btn_text("Checking...", false);
        }
    } else {
        sudoku_ui_notice("No microSD card was found at start-up. Insert the card and restart the board "
                         "(Wi-Fi shares the card's controller, so it can't be detected while Wi-Fi runs).", 8000);
    }
}

/**
 * @brief Takes the card back from the computer: the USB drive panel goes, sudoku.db reopens.
 */
void usb_finish(void)
{
    if (!u_ovl) {
        return;
    }
    usb_drive_return();                            /* the computer lets go; sudoku.db reopened */
    lv_obj_delete_async(u_ovl);
    u_ovl = NULL;
    settings_reload(true, false);                  /* sd_poll_cb applies what sudoku.db has */
    sudoku_ui_notice("Card back on the board: reopening sudoku.db...", 2500);
}

/**
 * @brief The USB drive row: Connect only with a card and a computer on the OTG port.
 */
void usb_btn_update(void)
{
    if (!st_usb_btn) {
        return;
    }
    bool host = usb_msc_host_connected();
    lv_obj_set_state(st_usb_btn, LV_STATE_DISABLED, !(sd_present() && host));
    const char *hint = !sd_present() ? "No microSD card"
                       : host ? "Back up or restore sudoku.db on a computer"
                       : "Plug the USB OTG port (not UART) into a computer";
    if (strcmp(lv_label_get_text(st_usb_hint), hint) != 0) {
        lv_label_set_text(st_usb_hint, hint);
    }
}

/**
 * @brief The USB drive panel's Done: takes the card back.
 * @param e  the LVGL event (its target is the control)
 */
static void usb_done_cb(lv_event_t *e)
{
    usb_finish();
}

/**
 * @brief Connect: lends the card to the computer and covers the screen with the USB drive panel.
 * @param e  the LVGL event (its target is the control)
 */
static void usb_connect_cb(lv_event_t *e)
{
    if (!sd_present()) {
        sudoku_ui_notice("No microSD card. Insert one before switching on.", 4000);
        return;
    }
    if (sd_state() != SD_IDLE) {
        return;    /* formatting */
    }
    if (!usb_msc_host_connected()) {
        return;    /* greyed out anyway (see usb_btn_update) */
    }
    if (usb_drive_lend() != ESP_OK) {              /* sudoku.db closed, the card to the computer */
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

/**
 * @brief Import puzzles from puzzles/ on the card (the result comes as a message).
 * @param e  the LVGL event (its target is the control)
 */
static void import_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("Importing needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Importing (each puzzle is checked for a single solution)...", 3000);
    stats_db_import_puzzles();                      /* the result comes back as a message */
}

/**
 * @brief Export to CSV in export/ on the card (the result comes as a message).
 * @param e  the LVGL event (its target is the control)
 */
static void export_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("Export needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Exporting...", 1500);
    stats_db_export_csv();                          /* the result comes back as a message */
}

/**
 * @brief Back up sudoku.db into backup/ on the card (the result comes as a message).
 * @param e  the LVGL event (its target is the control)
 */
static void backup_cb(lv_event_t *e)
{
    if (!stats_db_ready()) {
        sudoku_ui_notice("Backup needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Backing up...", 1500);
    stats_db_backup();                              /* the result comes back as a message */
}

/**
 * @brief Factory reset, confirmed: settings, Wi-Fi and sudoku.db reset, then Home.
 * @param e  the event (its user data is the dialog)
 */
static void factory_reset_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (device_factory_reset()) {
        settings_reload(true, true);               /* sd_poll_cb applies the rebuilt sudoku.db, then Home */
        sudoku_ui_notice("Factory reset: rebuilding sudoku.db...", 3000);
    } else {
        settings_reload(false, true);
        sudoku_ui_notice("Factory reset done. (No sudoku.db on a card to erase.)", 4000);
    }
}

/**
 * @brief The Reset button: asks first (it can't be undone).
 * @param e  the LVGL event (its target is the control)
 */
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

/**
 * @brief Card work Settings is doing, for devctl genbench to wait for.
 * @return a few words (a firmware update, formatting, the USB drive), or NULL when none
 */
const char *settings_card_busy(void)
{
    if (fw_timer) {
        return "a firmware update is being written";
    }
    if (sd_state() == SD_PROBING || sd_state() == SD_FORMATTING) {
        return "the microSD card is being set up or formatted";
    }
    if (u_ovl) {
        return "the card is lent to a computer (USB drive)";
    }
    return NULL;
}

/* (documented in ui_settings.h) */
void settings_data_poll_card(void)
{
    sd_state_t st = sd_state();
    if (st == s_sd_seen) {
        return;
    }
    s_sd_seen = st;
    char sz[24], text[320];
    switch (st) {
    case SD_FOUND: {
        sd_btn_text("Format", true);
        stats_db_start();                          /* card found after boot: use it for stats too */
        fmt_gb(sz, sizeof(sz), sd_bytes());
        snprintf(text, sizeof(text),
                 "Card \"%s\", %s.\nEverything on it will be erased, including sudoku.db with your "
                 "statistics and solve history. It gets one FAT32 partition using the whole card, "
                 "and a new sudoku.db with your current settings and the game in progress.", sd_name(), sz);
        lv_obj_t *m = make_msgbox("Format microSD card?", text);
        msgbox_button(m, "Cancel", sd_format_cancel_cb, false);
        msgbox_button(m, "Format", sd_format_confirm_cb, true);
        break;
    }
    case SD_NO_CARD:
        sd_btn_text("Format", true);
        sudoku_ui_notice("No microSD card found. Insert one and try again.", 5000);
        sd_set_state(s_sd_seen = SD_IDLE);
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
            if (act == screen_home()) {
                refresh_home();
            } else if (act == screen_list()) {
                refresh_list();
            } else if (act == screen_stats()) {
                refresh_stats();
            }
            fmt_gb(sz, sizeof(sz), sd_bytes());
            snprintf(text, sizeof(text), "microSD card formatted: FAT32, one %s partition. New sudoku.db "
                     "with your current settings; statistics start fresh.", sz);
        } else if (sd_last_error() == ESP_ERR_TIMEOUT || sd_last_error() == ESP_ERR_INVALID_CRC ||
                   sd_last_error() == ESP_ERR_INVALID_RESPONSE) {
            sd_forget();
            snprintf(text, sizeof(text), "The microSD card isn't answering. If it was swapped, restart the "
                     "board with the card inserted.");
        } else if (sd_last_error() == ESP_ERR_INVALID_SIZE) {
            snprintf(text, sizeof(text), "Format failed: the card is too small for FAT32.");
        } else {
            snprintf(text, sizeof(text), "Format failed (%s). The card may need formatting on a computer.",
                     esp_err_to_name(sd_last_error()));
        }
        sudoku_ui_notice(text, 6000);
        sd_set_state(s_sd_seen = SD_IDLE);
        break;
    default:
        break;
    }
}

/* (documented in ui_settings.h) */
void settings_tab_data(lv_obj_t *pane)
{
    lv_obj_t *col = pane;
    lv_obj_t *r;
    r = settings_row(col, "microSD card", "");
    st_sd_hint = lv_obj_get_child(r, 1);           /* status of sudoku.db, see clock_cb */
    char sd[64];
    stats_db_status(sd, sizeof(sd));
    lv_label_set_text(st_sd_hint, sd);
    st_sd_btn = make_btn(r, 0, 0, 160, 52, "Format", &lv_font_montserrat_18, sd_format_cb, NULL);
    lv_obj_set_style_bg_color(st_sd_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_sd_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_sd_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    if (sd_state() == SD_FORMATTING) {
        sd_btn_text("Formatting...", false);
    }


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
}

/* (documented in ui_settings.h) */
void settings_data_shown(void)
{
    fw_row_update();
}

/* (documented in ui_settings.h) */
void settings_data_poll_usb(void)
{
    if (u_ovl && (usb_msc_ejected() || usb_msc_unplugged())) {
        usb_finish();
    }
}

/* ---- accessors */
/** @return the microSD row's hint (sudoku.db's status) */
lv_obj_t *settings_sd_hint(void) { return st_sd_hint; }

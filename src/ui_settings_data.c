/**
 * @file ui_settings_data.c
 * @ingroup settings
 * @brief Settings > Data: the tab's rows; import, export, backup and factory reset (the card's row: ui_settings_card.c;
 *        USB drive: ui_settings_usb.c; firmware update: ui_settings_update.c).
 *
 * The rows and their dialogs. What they do to the board (the card format task, the firmware write, the
 * factory reset, lending the card over USB) is in the settings core (settings_core.h); this file follows those through their
 * state (sd_state(), fw_update_state()) and shows the results.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void import_cb(lv_event_t *e);
static void export_cb(lv_event_t *e);
static void backup_cb(lv_event_t *e);
static void factory_reset_confirm_cb(lv_event_t *e);
static void factory_reset_cb(lv_event_t *e);


/**
 * @brief Import puzzles from puzzles/ on the card (the result comes as a message).
 * @param[in] e  the LVGL event (its target is the control)
 */
static void import_cb(lv_event_t *e)
{
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("Importing needs sudoku.db on a microSD card.", 3000);
        return;
    }
    if (!CLOCK_VALID(time(NULL))) {                 /* (each collection is dated: the import refuses) */
        sudoku_ui_notice("Importing needs the clock: connect to Wi-Fi first (Settings > Network).", 3500);
        return;
    }
    sudoku_ui_notice("Importing (each puzzle is checked for a single solution)...", 3000);
    stats_db_import_puzzles();                      /* the result comes back as a message */
}

/**
 * @brief Export to CSV in export/ on the card (the result comes as a message).
 * @param[in] e  the LVGL event (its target is the control)
 */
static void export_cb(lv_event_t *e)
{
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("Export needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Exporting...", 1500);
    stats_db_export_csv();                          /* the result comes back as a message */
}

/**
 * @brief Back up sudoku.db into backup/ on the card (the result comes as a message).
 * @param[in] e  the LVGL event (its target is the control)
 */
static void backup_cb(lv_event_t *e)
{
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("Backup needs sudoku.db on a microSD card.", 3000);
        return;
    }
    sudoku_ui_notice("Backing up...", 1500);
    stats_db_backup();                              /* the result comes back as a message */
}

/**
 * @brief Factory reset, confirmed: settings, Wi-Fi and sudoku.db reset, then Home.
 * @param[in] e  the event (its user data is the dialog)
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
 * @param[in] e  the LVGL event (its target is the control)
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

/* (documented in ui_settings.h) */
void settings_tab_data(lv_obj_t *pane)
{
    lv_obj_t *col = pane;
    lv_obj_t *r;
    settings_card_row(col);                         /* (ui_settings_card.c) */
    settings_usb_row(col);                          /* (ui_settings_usb.c) */
    settings_update_row(col);                       /* (ui_settings_update.c) */

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

/**
 * @brief Card work Settings is doing, for devctl genbench to wait for.
 * @return a few words (a firmware update, formatting, the USB drive), or NULL when none
 */
const char *settings_card_busy(void)
{
    if (is_settings_update_busy()) {
        return "a firmware update is being written";
    }
    if (sd_state() == SD_PROBING || sd_state() == SD_FORMATTING) {
        return "the microSD card is being set up or formatted";
    }
    if (is_settings_usb_lent()) {
        return "the card is lent to a computer (USB drive)";
    }
    return NULL;
}

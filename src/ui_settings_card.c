/**
 * @file ui_settings_card.c
 * @ingroup settings
 * @brief Settings > Data > microSD card: sudoku.db's status, Format and its steps.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void sd_btn_text(const char *t, bool enabled);
static void sd_format_confirm_cb(lv_event_t *e);
static void sd_format_cancel_cb(lv_event_t *e);
static void sd_format_cb(lv_event_t *e);

static lv_obj_t *st_sd_btn;                         /**< the microSD row's Format button */
static lv_obj_t *st_sd_hint;                        /**< the microSD row's hint: sudoku.db's status */
static sd_state_t s_sd_seen = SD_IDLE;              /**< the card state settings_data_poll_card has shown */
static lv_obj_t *s_sd_busy;                         /**< "Formatting..." box */

/**
 * @brief The microSD row's button: its text, and whether it can be tapped.
 * @param[in] t        the text
 * @param[in] enabled  false: greyed out
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
 * @param[in] e  the event (its user data is the dialog)
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
 * @param[in] e  the event (its user data is the dialog)
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
 * @param[in] e  the LVGL event (its target is the control)
 */
static void sd_format_cb(lv_event_t *e)
{
    if (sd_state() != SD_IDLE) {
        return;
    }
    if (is_sd_present()) {
        sd_set_state(SD_FOUND);                    /* found at boot: straight to the question */
    } else if (!is_wifi_mgr_radio_started()) {
        if (sd_run(SD_JOB_PROBE)) {
            sd_btn_text("Checking...", false);
        }
    } else {
        sudoku_ui_notice("No microSD card was found at start-up. Insert the card and restart the board "
                         "(Wi-Fi shares the card's controller, so it can't be detected while Wi-Fi runs).", 8000);
    }
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

/* ---- accessors */
/** @return the microSD row's hint (sudoku.db's status) */
lv_obj_t *settings_sd_hint(void) { return st_sd_hint; }

/* (documented in ui_settings.h) */
void settings_card_row(lv_obj_t *col)
{
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
}

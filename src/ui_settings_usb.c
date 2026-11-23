/**
 * @file ui_settings_usb.c
 * @ingroup settings
 * @brief Settings > Data > USB drive: the card lent to a computer, the panel meanwhile, the card back.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void usb_done_cb(lv_event_t *e);
static void usb_connect_cb(lv_event_t *e);

static lv_obj_t *u_ovl;                             /**< "USB drive" panel while the computer has the card */
static lv_obj_t *st_usb_btn;                        /**< the USB drive row's Connect button */
static lv_obj_t *st_usb_hint;                       /**< the USB drive row's hint */

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
    bool host = is_usb_msc_host_connected();
    lv_obj_set_state(st_usb_btn, LV_STATE_DISABLED, !(is_sd_present() && host));
    const char *hint = !is_sd_present() ? "No microSD card"
                       : host ? "Back up or restore sudoku.db on a computer"
                       : "Plug the USB OTG port (not UART) into a computer";
    if (strcmp(lv_label_get_text(st_usb_hint), hint) != 0) {
        lv_label_set_text(st_usb_hint, hint);
    }
}

/**
 * @brief The USB drive panel's Done: takes the card back.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void usb_done_cb(lv_event_t *e)
{
    usb_finish();
}

/**
 * @brief Connect: lends the card to the computer and covers the screen with the USB drive panel.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void usb_connect_cb(lv_event_t *e)
{
    if (!is_sd_present()) {
        sudoku_ui_notice("No microSD card. Insert one before switching on.", 4000);
        return;
    }
    if (sd_state() != SD_IDLE) {
        return;    /* formatting */
    }
    if (!is_usb_msc_host_connected()) {
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

/* (documented in ui_settings.h) */
void settings_data_poll_usb(void)
{
    if (u_ovl && (is_usb_msc_ejected() || is_usb_msc_unplugged())) {
        usb_finish();
    }
}

/* (documented in ui_settings.h) */
void settings_usb_row(lv_obj_t *col)
{
    lv_obj_t *r;
    r = settings_row(col, "USB drive", "");
    st_usb_hint = lv_obj_get_child(r, 1);
    st_usb_btn = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_USB "  Connect", &lv_font_montserrat_18, usb_connect_cb, NULL);
    lv_obj_set_style_bg_color(st_usb_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_usb_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(st_usb_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_usb_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    usb_btn_update();
}

/* (documented in ui_settings.h) */
bool is_settings_usb_lent(void)
{
    return u_ovl != NULL;
}

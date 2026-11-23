/**
 * @file ui_wifi_dialog.c
 * @brief The Wi-Fi page's password dialog: a network tapped asks for its password on the keyboard, then joins it.
 */
#include "ui_wifi.h"
#include "ui_wifi_part.h"
#include "ui_palette.h"
#include <stdio.h>
#include <string.h>

static void on_dlg_cancel(lv_event_t *e);
static void on_dlg_join(lv_event_t *e);

/* this file's own functions, in any order */
static void on_kb(lv_event_t *e);
static void on_eye(lv_event_t *e);

/* the password dialog */
static lv_obj_t *s_dlg;                         /**< the password dialog (hidden when not used) */
static lv_obj_t *s_dlg_title;                   /**< its title: "Join <network>" */
static lv_obj_t *s_ta;                          /**< the password */
static lv_obj_t *s_eye;                         /**< show / hide it */
static char s_dlg_ssid[33];             /**< the network the dialog joins */

/* (documented in ui_wifi_part.h) */
void wifi_dlg_close(void)
{
    if (s_dlg) {
        lv_textarea_set_text(s_ta, "");             /* don't leave the password in RAM */
        lv_obj_set_hidden(s_dlg, true);
    }
}

/* (documented in ui_wifi_part.h) */
void wifi_dlg_join(void)
{
    const char *pass = lv_textarea_get_text(s_ta);
    if (strlen(pass) < 8) {                         /* WPA2/3 minimum */
        lv_label_set_text_fmt(s_dlg_title, "Join %s\nPassword needs 8+ characters", s_dlg_ssid);
        return;
    }
    wifi_mgr_connect(s_dlg_ssid, pass);
    wifi_dlg_close();
}

/**
 * @brief The keyboard: its Enter joins, its close cancels.
 * @param[in] e  the event
 */
static void on_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        wifi_dlg_join();
    } else if (code == LV_EVENT_CANCEL) {
        wifi_dlg_close();
    }
}

/**
 * @brief Shows or hides the password.
 * @param[in] e  the event
 */
static void on_eye(lv_event_t *e)
{
    bool hide = !lv_textarea_get_password_mode(s_ta);
    lv_textarea_set_password_mode(s_ta, hide);
    lv_label_set_text(lv_obj_get_child(s_eye, 0), hide ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

/* (documented in ui_wifi_part.h) */
void wifi_dlg_build(void)
{
    /* On the top layer so the page's scrolling doesn't move it (the screen
     * saver is on the system layer, above it). */
    s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dlg);
    lv_obj_set_size(s_dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_70, 0);
    lv_obj_set_clickable(s_dlg, true);              /* block touches to the page */
    lv_obj_move_background(s_dlg);

    lv_obj_t *panel = lv_obj_create(s_dlg);
    lv_obj_set_size(panel, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_set_style_pad_row(panel, 16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_text_color(panel, C_TEXT, 0);

    s_dlg_title = lv_label_create(panel);
    lv_obj_set_width(s_dlg_title, LV_PCT(100));
    lv_label_set_long_mode(s_dlg_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(s_dlg_title, &lv_font_montserrat_24, 0);

    lv_obj_t *row = wifi_plain_row(panel);
    lv_obj_set_style_pad_column(row, 12, 0);
    s_ta = lv_textarea_create(row);
    lv_obj_set_flex_grow(s_ta, 1);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_password_mode(s_ta, true);
    lv_textarea_set_max_length(s_ta, 63);
    lv_textarea_set_placeholder_text(s_ta, "Password");
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_ta, C_TEXT, 0);
    lv_obj_set_style_text_color(s_ta, C_MUTED, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(s_ta, C_BG, 0);
    lv_obj_set_style_border_color(s_ta, C_CARD2, 0);
    lv_obj_set_style_border_color(s_ta, C_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(s_ta, 12, 0);
    lv_obj_set_style_pad_all(s_ta, 14, 0);
    lv_obj_set_style_bg_color(s_ta, C_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(s_ta, C_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    s_eye = wifi_round_btn(row, LV_SYMBOL_EYE_OPEN, on_eye);
    lv_obj_set_size(s_eye, 60, 60);

    lv_obj_t *btns = wifi_plain_row(panel);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btns, 14, 0);
    wifi_pill_btn(btns, "Cancel", false, on_dlg_cancel);
    wifi_pill_btn(btns, "Join", true, on_dlg_join);

    lv_obj_t *kb = lv_keyboard_create(s_dlg);
    lv_obj_set_size(kb, LV_PCT(100), 330);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_set_style_bg_color(kb, C_BG, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 8, 0);
    lv_obj_set_style_pad_bottom(kb, 30, 0);         /* clear of the bezel */
    lv_obj_set_style_pad_gap(kb, 8, 0);
    lv_obj_set_style_bg_color(kb, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, C_CARD, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
    /* control keys (backspace, ABC/abc, OK, arrows) are CHECKED buttons, and the light
     * theme's checked-key style sets near-black text: override it for that state too */
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_22, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, on_kb, LV_EVENT_ALL, NULL);

    lv_obj_set_hidden(s_dlg, true);
}

/* (documented in ui_wifi_part.h) */
void wifi_dlg_open(const char *ssid)
{
    strlcpy(s_dlg_ssid, ssid, sizeof(s_dlg_ssid));
    lv_label_set_text_fmt(s_dlg_title, "Join %s", ssid);
    lv_textarea_set_text(s_ta, "");
    lv_textarea_set_password_mode(s_ta, true);
    lv_label_set_text(lv_obj_get_child(s_eye, 0), LV_SYMBOL_EYE_OPEN);
    lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
    lv_obj_set_hidden(s_dlg, false);
}

/**
 * @brief Cancel: the dialog closes.
 * @param[in] e  the event
 */
static void on_dlg_cancel(lv_event_t *e) { wifi_dlg_close(); }

/**
 * @brief Join: the network joined with the password typed.
 * @param[in] e  the event
 */
static void on_dlg_join(lv_event_t *e)   { wifi_dlg_join(); }

/* (documented in ui_wifi_part.h) */
void wifi_dlg_drop(void)
{
    if (s_dlg) {
        lv_textarea_set_text(s_ta, "");
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
}

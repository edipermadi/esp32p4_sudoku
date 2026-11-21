/**
 * @file ui_wifi.c
 * @brief The Wi-Fi page: networks, connecting, forgetting.
 *
 * Wi-Fi page (see ui_wifi.h). Polls wifi_mgr every 400 ms while shown.
 *
 */
#include "ui_wifi.h"
#include "wifi_mgr.h"
#include "ui_palette.h"
#include <stdio.h>
#include <string.h>

#define MAX_ROWS 32                     /**< networks listed at most */

static lv_obj_t *s_scr;                         /**< the page's screen */
static lv_obj_t *s_body;                        /**< its scrolling column */
static lv_obj_t *s_switch;                      /**< Wi-Fi on/off */
static lv_obj_t *s_state_icon;                  /**< the status card's icon */
static lv_obj_t *s_state_lbl;                   /**< its main line (the network, "Off", ...) */
static lv_obj_t *s_state_sub;                   /**< its second line (IP, signal, why it failed) */
static lv_obj_t *s_forget;                      /**< Forget (the saved network) */
static lv_obj_t *s_list_hdr;                    /**< the networks' header row */
static lv_obj_t *s_scan_lbl;                    /**< "Scanning..." */
static lv_obj_t *s_list;                        /**< the networks found */
static lv_obj_t *s_refresh;                     /**< scan again */
static lv_timer_t *s_timer;             /**< the status poll, every 400 ms while shown */
static wifi_ap_t s_aps[MAX_ROWS];       /**< the networks listed */
static int s_aps_n;                     /**< how many */
static uint32_t s_shown_scan = UINT32_MAX;  /**< the scan shown (wifi_status_t.scan_serial) */
static wifi_state_t s_shown_state = (wifi_state_t)-1;  /**< the state shown */
static char s_shown_ssid[33];           /**< the network shown */

/* the password dialog */
static lv_obj_t *s_dlg;                         /**< the password dialog (hidden when not used) */
static lv_obj_t *s_dlg_title;                   /**< its title: "Join <network>" */
static lv_obj_t *s_ta;                          /**< the password */
static lv_obj_t *s_eye;                         /**< show / hide it */
static char s_dlg_ssid[33];             /**< the network the dialog joins */

static char s_summary[48];              /**< wifi_ui_summary()'s text */

/**
 * @brief A transparent full-width row.
 * @param parent  where
 * @return it
 */
static lv_obj_t *plain_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

/**
 * @brief A card (rounded, the card colour, a column).
 * @param parent  where
 * @return it
 */
static lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_all(c, 18, 0);
    lv_obj_set_style_pad_row(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_style_text_color(c, C_TEXT, 0);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_20, 0);
    return c;
}

/**
 * @brief A rounded text button.
 * @param parent  where
 * @param txt  its text
 * @param primary  in the accent colour
 * @param cb  a tap
 * @return it
 */
static lv_obj_t *pill_btn(lv_obj_t *parent, const char *txt, bool primary, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 56);
    lv_obj_set_style_pad_hor(b, 26, 0);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, primary ? C_ACCENT : C_CARD2, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, C_TEXT, 0);
    lv_obj_center(l);
    return b;
}

/**
 * @brief A round icon button.
 * @param parent  where
 * @param sym  its icon
 * @param cb  a tap
 * @return it
 */
static lv_obj_t *round_btn(lv_obj_t *parent, const char *sym, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 52, 52);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, C_CARD2, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, C_TEXT, 0);
    lv_obj_center(l);
    return b;
}

/**
 * @brief A signal's colour: strong green, fair the text's, weak muted.
 * @param rssi  dBm
 * @return the colour
 */
static lv_color_t signal_color(int rssi)
{
    return rssi >= -60 ? C_GREEN : (rssi >= -75 ? C_TEXT : C_MUTED);
}

/**
 * @brief Hides or shows an object.
 * @param o  it
 * @param hidden  hidden
 */
static void set_hidden(lv_obj_t *o, bool hidden)
{
    lv_obj_set_hidden(o, hidden);
}

/**
 * @brief Closes the password dialog (and clears the password).
 */
static void dlg_close(void)
{
    if (s_dlg) {
        lv_textarea_set_text(s_ta, "");             /* don't leave the password in RAM */
        lv_obj_set_hidden(s_dlg, true);
    }
}

/**
 * @brief Joins the dialog's network with the password typed (8 characters at least).
 */
static void dlg_join(void)
{
    const char *pass = lv_textarea_get_text(s_ta);
    if (strlen(pass) < 8) {                         /* WPA2/3 minimum */
        lv_label_set_text_fmt(s_dlg_title, "Join %s\nPassword needs 8+ characters", s_dlg_ssid);
        return;
    }
    wifi_mgr_connect(s_dlg_ssid, pass);
    dlg_close();
}

/**
 * @brief The dialog's Cancel.
 * @param e  the event
 */
static void on_dlg_cancel(lv_event_t *e) { dlg_close(); }
/**
 * @brief The dialog's Join.
 * @param e  the event
 */
static void on_dlg_join(lv_event_t *e)   { dlg_join(); }

/**
 * @brief The keyboard: its Enter joins, its close cancels.
 * @param e  the event
 */
static void on_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        dlg_join();
    } else if (code == LV_EVENT_CANCEL) {
        dlg_close();
    }
}

/**
 * @brief Shows or hides the password.
 * @param e  the event
 */
static void on_eye(lv_event_t *e)
{
    bool hide = !lv_textarea_get_password_mode(s_ta);
    lv_textarea_set_password_mode(s_ta, hide);
    lv_label_set_text(lv_obj_get_child(s_eye, 0), hide ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

/**
 * @brief Builds the password dialog (hidden) on the top layer.
 */
static void build_dialog(void)
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

    lv_obj_t *row = plain_row(panel);
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
    s_eye = round_btn(row, LV_SYMBOL_EYE_OPEN, on_eye);
    lv_obj_set_size(s_eye, 60, 60);

    lv_obj_t *btns = plain_row(panel);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btns, 14, 0);
    pill_btn(btns, "Cancel", false, on_dlg_cancel);
    pill_btn(btns, "Join", true, on_dlg_join);

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

/**
 * @brief Opens the password dialog for a network.
 * @param ssid  the network
 */
static void dlg_open(const char *ssid)
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
 * @brief The Wi-Fi switch.
 * @param e  the event
 */
static void on_switch(lv_event_t *e)
{
    wifi_mgr_set_enabled(lv_obj_has_state(s_switch, LV_STATE_CHECKED));
}

/**
 * @brief Scans again.
 * @param e  the event
 */
static void on_refresh(lv_event_t *e) { wifi_mgr_scan(); }
/**
 * @brief Forgets the saved network.
 * @param e  the event
 */
static void on_forget(lv_event_t *e)  { wifi_mgr_forget(); }

/**
 * @brief A network tapped: joins it (the saved one, an open one) or asks for its password.
 * @param e  the event (user data: its row)
 */
static void on_ap(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_aps_n) {
        return;
    }
    wifi_status_t st;
    wifi_mgr_get_status(&st);
    const wifi_ap_t *ap = &s_aps[i];
    if (st.state == WIFI_ST_CONNECTED && strcmp(st.ssid, ap->ssid) == 0) {
        return;                                     /* already on it */
    }
    if (!ap->secure || strcmp(ap->ssid, st.saved_ssid) == 0) {
        wifi_mgr_connect(ap->ssid, "");             /* open, or remembered password */
    } else {
        dlg_open(ap->ssid);
    }
}

/**
 * @brief Lists the networks found (or why none: off, scanning).
 * @param st  Wi-Fi now
 */
static void rebuild_list(const wifi_status_t *st)
{
    lv_obj_clean(s_list);
    s_aps_n = wifi_mgr_get_aps(s_aps, MAX_ROWS);
    if (!st->enabled) {
        return;
    }
    if (s_aps_n == 0) {
        lv_obj_t *l = lv_label_create(s_list);
        lv_label_set_text(l, st->scanning ? "Looking for networks..." : "No networks found");
        lv_obj_set_style_text_color(l, C_MUTED, 0);
        lv_obj_set_style_pad_all(l, 12, 0);
        return;
    }
    for (int i = 0; i < s_aps_n; i++) {
        const wifi_ap_t *ap = &s_aps[i];
        bool cur = strcmp(ap->ssid, st->ssid) == 0 &&
                   (st->state == WIFI_ST_CONNECTED || st->state == WIFI_ST_CONNECTING);

        lv_obj_t *b = lv_obj_create(s_list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(b, 16, 0);
        lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_set_style_pad_column(b, 16, 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(b, C_CARD2, 0);
        lv_obj_set_style_border_width(b, i < s_aps_n - 1 ? 1 : 0, 0);
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, on_ap, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *icon = lv_label_create(b);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(icon, signal_color(ap->rssi), 0);

        lv_obj_t *name = lv_label_create(b);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(name, ap->ssid);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(name, cur ? C_ACCENT : C_TEXT, 0);

        lv_obj_t *info = lv_label_create(b);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s%s  %d dBm",
                 cur ? (st->state == WIFI_ST_CONNECTED ? LV_SYMBOL_OK "  " : "...  ") : "",
                 ap->secure ? "Secured" : "Open", ap->rssi);
        lv_label_set_text(info, buf);
        lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(info, C_MUTED, 0);
    }
}

/**
 * @brief The status card: the network, the state, the IP or why it failed; the switch and Forget.
 * @param st  Wi-Fi now
 */
static void show_status(const wifi_status_t *st)
{
    char sub[64] = "";
    const char *main_txt;
    char main_buf[64];
    lv_color_t icon_c = C_MUTED;
    switch (st->state) {
    case WIFI_ST_OFF:
        main_txt = "Wi-Fi is off";
        break;
    case WIFI_ST_STARTING:
        main_txt = "Starting Wi-Fi...";
        break;
    case WIFI_ST_NO_RADIO:
        main_txt = st->fail_reason;
        icon_c = C_ORANGE;
        break;
    case WIFI_ST_IDLE:
        main_txt = "Not connected";
        snprintf(sub, sizeof(sub), "Pick a network below");
        break;
    case WIFI_ST_CONNECTING:
        snprintf(main_buf, sizeof(main_buf), "Connecting to %s...", st->ssid);
        main_txt = main_buf;
        icon_c = C_TEXT;
        break;
    case WIFI_ST_CONNECTED:
        snprintf(main_buf, sizeof(main_buf), "%s", st->ssid);
        main_txt = main_buf;
        snprintf(sub, sizeof(sub), "Connected  |  %s  |  %d dBm", st->ip, st->rssi);
        icon_c = signal_color(st->rssi);
        break;
    case WIFI_ST_FAILED:
    default:
        snprintf(main_buf, sizeof(main_buf), "%s", st->ssid);
        main_txt = main_buf;
        snprintf(sub, sizeof(sub), "%s", st->fail_reason);
        icon_c = C_ORANGE;
        break;
    }
    lv_label_set_text(s_state_lbl, main_txt);
    lv_label_set_text(s_state_sub, sub);
    set_hidden(s_state_sub, sub[0] == 0);
    lv_obj_set_style_text_color(s_state_icon, icon_c, 0);
    lv_obj_set_style_text_color(s_state_sub, st->state == WIFI_ST_FAILED ? C_ORANGE : C_MUTED, 0);
    lv_obj_set_style_text_color(s_state_lbl, C_TEXT, 0);
    set_hidden(s_forget, !st->saved_ssid[0] || !st->enabled);

    if (lv_obj_has_state(s_switch, LV_STATE_CHECKED) != st->enabled) {
        lv_obj_set_state(s_switch, LV_STATE_CHECKED, st->enabled);
    }
    set_hidden(s_list_hdr, !st->enabled);
    set_hidden(s_list, !st->enabled);
    set_hidden(s_refresh, !st->enabled);
    lv_label_set_text(s_scan_lbl, st->scanning ? "Scanning..." : "");
}

/**
 * @brief The page's poll: the status, and the list when a scan or the state changed.
 * @param t  the timer
 */
static void tick(lv_timer_t *t)
{
    wifi_status_t st;
    wifi_mgr_get_status(&st);
    show_status(&st);
    if (st.scan_serial != s_shown_scan || st.state != s_shown_state ||
            strcmp(st.ssid, s_shown_ssid) != 0) {
        s_shown_scan = st.scan_serial;
        s_shown_state = st.state;
        strlcpy(s_shown_ssid, st.ssid, sizeof(s_shown_ssid));
        rebuild_list(&st);
    }
}

/* (documented in ui_wifi.h) */
void wifi_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb)
{
    /* rebuilt on a theme change: drop the previous page's dialog and timer */
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_dlg) {
        lv_textarea_set_text(s_ta, "");
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
    s_shown_scan = UINT32_MAX;
    s_shown_state = (wifi_state_t)-1;

    s_scr = scr;

    /* top bar, as on the other screens */
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_pos(back, 12, 10);
    lv_obj_set_size(back, 150, 52);
    lv_obj_set_style_bg_color(back, C_CARD, 0);
    lv_obj_set_style_bg_color(back, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Settings");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(bl, C_TEXT, 0);
    lv_obj_center(bl);
    lv_obj_t *h = lv_label_create(scr);
    lv_label_set_text(h, "Wi-Fi");
    lv_obj_set_style_text_font(h, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(h, C_TEXT, 0);
    lv_obj_set_pos(h, 180, 20);
    s_refresh = round_btn(scr, LV_SYMBOL_REFRESH, on_refresh);
    lv_obj_align(s_refresh, LV_ALIGN_TOP_RIGHT, -12, 10);

    /* scrolling body */
    s_body = lv_obj_create(scr);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_pos(s_body, 0, 72);
    lv_obj_set_size(s_body, 720, 720 - 72);
    lv_obj_set_style_pad_hor(s_body, 40, 0);
    lv_obj_set_style_pad_top(s_body, 10, 0);
    lv_obj_set_style_pad_bottom(s_body, 24, 0);
    lv_obj_set_style_pad_row(s_body, 10, 0);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);

    /* on/off + current connection */
    lv_obj_t *c = card(s_body);
    lv_obj_t *row = plain_row(c);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, "Wi-Fi");
    s_switch = lv_switch_create(row);
    lv_obj_set_size(s_switch, 84, 44);
    lv_obj_set_style_bg_color(s_switch, C_CARD2, 0);
    lv_obj_set_style_bg_color(s_switch, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_switch, on_switch, LV_EVENT_VALUE_CHANGED, NULL);

    row = plain_row(c);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    s_state_icon = lv_label_create(row);
    lv_label_set_text(s_state_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(s_state_icon, &lv_font_montserrat_28, 0);
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    s_state_lbl = lv_label_create(col);
    lv_obj_set_width(s_state_lbl, LV_PCT(100));
    lv_label_set_long_mode(s_state_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(s_state_lbl, &lv_font_montserrat_20, 0);
    s_state_sub = lv_label_create(col);
    lv_obj_set_width(s_state_sub, LV_PCT(100));
    lv_label_set_long_mode(s_state_sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(s_state_sub, &lv_font_montserrat_16, 0);
    s_forget = pill_btn(row, "Forget", false, on_forget);

    /* nearby networks */
    s_list_hdr = plain_row(s_body);
    lv_obj_set_flex_align(s_list_hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(s_list_hdr, 8, 0);
    lv_obj_set_style_margin_top(s_list_hdr, 6, 0);
    l = lv_label_create(s_list_hdr);
    lv_label_set_text(l, "NETWORKS");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, C_MUTED, 0);
    s_scan_lbl = lv_label_create(s_list_hdr);
    lv_label_set_text(s_scan_lbl, "");
    lv_obj_set_style_text_font(s_scan_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_scan_lbl, C_MUTED, 0);

    s_list = card(s_body);
    lv_obj_set_style_pad_all(s_list, 8, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);

    build_dialog();
    s_timer = lv_timer_create(tick, 400, NULL);
    lv_timer_pause(s_timer);
}

/* (documented in ui_wifi.h) */
void wifi_ui_set_shown(bool shown)
{
    if (shown) {
        wifi_status_t st;
        wifi_mgr_get_status(&st);
        if (st.enabled) {
            wifi_mgr_scan();
        }
        s_shown_scan = UINT32_MAX;                  /* force a list rebuild */
        lv_timer_resume(s_timer);
        tick(NULL);
    } else {
        lv_timer_pause(s_timer);
        dlg_close();
    }
}

/* (documented in ui_wifi.h) */
const char *wifi_ui_summary(void)
{
    wifi_status_t st;
    wifi_mgr_get_status(&st);
    switch (st.state) {
    case WIFI_ST_OFF:
        return "Off";
    case WIFI_ST_STARTING:
        return "Starting...";
    case WIFI_ST_NO_RADIO:
        return "Unavailable";
    case WIFI_ST_IDLE:
        return "Not connected";
    case WIFI_ST_CONNECTING:
        snprintf(s_summary, sizeof(s_summary), "Connecting...");
        return s_summary;
    case WIFI_ST_CONNECTED:
        snprintf(s_summary, sizeof(s_summary), "%s", st.ssid);
        return s_summary;
    case WIFI_ST_FAILED:
    default:
        return "Not connected";
    }
}

/**
 * @file ui_settings_network.c
 * @ingroup settings
 * @brief Settings > Network: Wi-Fi and the time zone.
 *
 * The Wi-Fi row opens the Wi-Fi screen (ui_wifi.c); the eye on it shows the network's name. The time
 * zone is one of the built-in IANA zones (timezones.h), applied at once.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void wifi_open_cb(lv_event_t *e);
static const char *tz_dropdown_options(void);
static void tz_show_selected(void);
static void tz_dd_cb(lv_event_t *e);
static void st_close_lists_cb(lv_event_t *e);

static lv_obj_t *st_wifi_lbl;                       /**< the network's name */
static lv_obj_t *st_wifi_eye;                       /**< shows the network's name (hidden until tapped) */
static lv_obj_t *st_tz_dd;                          /**< the time zone dropdown */
static char st_tz_txt[48];                          /**< the closed dropdown shows just the IANA name */

/**
 * @brief The Wi-Fi row: opens the Wi-Fi screen.
 * @param e  the LVGL event (its target is the control)
 */
static void wifi_open_cb(lv_event_t *e)
{
    lv_screen_load(screen_wifi());
    wifi_ui_set_shown(true);
}

/**
 * @brief The time zone dropdown's options, "Asia/Dubai  UTC+4" per line (built once).
 * @return the options, one per zone in timezones.h's order
 */
static const char *tz_dropdown_options(void)
{
    static char *opts;
    if (opts) {
        return opts;
    }
    size_t n = 0;
    for (int i = 0; i < tz_count(); i++) {
        n += strlen(tz_get(i)->name) + 16;
    }
    opts = (char *)malloc(n);
    if (!opts) {
        return "UTC";
    }
    char *p = opts;
    for (int i = 0; i < tz_count(); i++) {
        const tz_entry_t *z = tz_get(i);
        const char *sp = strstr(z->label, "  ");          /* label: "UTC+4  Dubai" */
        int off_len = sp ? (int)(sp - z->label) : 0;
        if (strcmp(z->name, "UTC") == 0) {
            off_len = 0;
        }
        p += sprintf(p, "%s%s%.*s%s", z->name, off_len ? "  " : "", off_len, z->label,
                     i < tz_count() - 1 ? "\n" : "");
    }
    return opts;
}

/**
 * @brief Shows just the zone's IANA name on the closed dropdown.
 */
static void tz_show_selected(void)
{
    const tz_entry_t *z = tz_get((int)lv_dropdown_get_selected(st_tz_dd));
    snprintf(st_tz_txt, sizeof(st_tz_txt), "%s", z->name);
    lv_dropdown_set_text(st_tz_dd, st_tz_txt);
}

/**
 * @brief A time zone was picked: saved and applied at once.
 * @param e  the LVGL event (its target is the control)
 */
static void tz_dd_cb(lv_event_t *e)
{
    const tz_entry_t *z = tz_get((int)lv_dropdown_get_selected(st_tz_dd));
    store_set_timezone(z->name);
    tz_apply(z->name);
    tz_show_selected();
}

/**
 * @brief Closes an open dropdown list (the time zone's) as Settings goes away.
 * @param e  the screen's unload event
 */
static void st_close_lists_cb(lv_event_t *e)
{
    if (st_tz_dd) {
        lv_dropdown_close(st_tz_dd);
    }
}

/* (documented in ui_settings.h) */
void settings_tab_network(lv_obj_t *pane)
{
    lv_obj_t *col = pane;
    lv_obj_t *r;
    /* Wi-Fi: the whole card opens the Wi-Fi screen */
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

    /* an open dropdown list (time zone) doesn't stay up over another screen */
    lv_obj_add_event_cb(lv_obj_get_screen(pane), st_close_lists_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
}

/* ---- accessors */
/** @return the Wi-Fi row's label (the network's name) */
lv_obj_t *settings_wifi_lbl(void) { return st_wifi_lbl; }
/** @return the Wi-Fi row's eye button */
lv_obj_t *settings_wifi_eye(void) { return st_wifi_eye; }

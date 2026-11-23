/**
 * @file ui_settings.c
 * @ingroup settings
 * @brief Settings screen: the title, the tab bar and a pane per tab (build_settings()), the rows' look
 *        (settings_row() and its controls), and Home's power off.
 *
 * Each tab is a file of its own (see ui_settings.h). Elsewhere: the screens and the theme (ui_screens.c),
 * sudoku.db's poll and settings reloaded (ui_db_poll.c), the dark screen while the flash is written
 * (ui_flash_busy.c).
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void power_off_confirm_cb(lv_event_t *e);
static void st_tab_cb(lv_event_t *e);



/* the Settings screen: its tabs (ST_*) and their panes */
static lv_obj_t *st_tab_btn[ST_TABS];               /**< the tab buttons */
static lv_obj_t *st_pane[ST_TABS];                  /**< each tab's pane (a scrolling column of rows) */
static int st_tab;                                  /**< the open tab; Settings opens on Network */


/* (documented in ui_settings.h) */
lv_obj_t *settings_dropdown(lv_obj_t *row, const char *options, uint32_t sel, int width, lv_event_cb_t cb)
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

/* ------------------------------------------------------------- power off */

/**
 * @brief Turn off, confirmed: black, backlight off, then device_power_off() (never returns).
 * @param[in] e  the event (its user data is the dialog)
 */
static void power_off_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    /* black first, so nothing half-drawn shows while the light goes out */
    lv_obj_t *black = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(black);
    lv_obj_set_size(black, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(black, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(black, LV_OPA_COVER, 0);
    lv_refr_now(NULL);
    bsp_display_brightness_set(0);
    vTaskDelay(pdMS_TO_TICKS(150));                /* the confirm tap's tick finishes */
    device_power_off();                            /* sudoku.db closed, then deep sleep until RESET */
}

/**
 * @brief Home's power button: asks before turning the board off.
 * @param[in] e  the LVGL event (its target is the control)
 */
void power_off_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Turn off?",
                              "The screen, sound and Wi-Fi turn off and the board goes into deep sleep. "
                              "Your game in progress is kept. Press the RESET button on the board to turn it back on.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_POWER "  Turn off", power_off_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

/**
 * @brief Shows a tab of Settings and hides the others.
 * @param[in] tab  ST_NETWORK .. ST_DATA
 */
void st_select_tab(int tab)
{
    st_tab = tab;
    if (tab == ST_DATA) {
        settings_data_shown();    /* (a new update.bin since: copied over USB) */
    }
    for (int i = 0; i < ST_TABS; i++) {
        lv_obj_set_state(st_tab_btn[i], LV_STATE_CHECKED, i == tab);
        lv_obj_set_style_text_color(lv_obj_get_child(st_tab_btn[i], 0), i == tab ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(st_pane[i], i != tab);
    }
    lv_obj_scroll_to_y(st_pane[tab], 0, LV_ANIM_OFF);
}

/**
 * @brief A tab button: shows its tab.
 * @param[in] e  the event (its user data is the tab)
 */
static void st_tab_cb(lv_event_t *e)
{
    st_select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

/* (documented in ui_settings.h) */
lv_obj_t *settings_row(lv_obj_t *parent, const char *title, const char *hint)
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

/* (documented in ui_settings.h) */
lv_obj_t *settings_switch(lv_obj_t *row, bool on, lv_event_cb_t cb)
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

/**
 * @brief Builds the Settings screen: title, tab bar, a pane per tab, each tab's rows.
 *
 * Called at start-up and by apply_theme(); a rebuild keeps the tab that was open.
 */
void build_settings(void)
{
    lv_obj_t *scr_settings = make_screen();
    screen_set_settings(scr_settings);

    make_btn(scr_settings, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_settings, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Settings");
    lv_obj_set_pos(t, 150, 20);

    /* the tabs, under the title (Network first, and the one Settings opens on) */
    static const char *const TABS[ST_TABS] = {LV_SYMBOL_WIFI "  Network", LV_SYMBOL_IMAGE "  Display",
                                              LV_SYMBOL_VOLUME_MAX "  Sound", LV_SYMBOL_EDIT "  Game",
                                              LV_SYMBOL_SD_CARD "  Data"
                                             };
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
    settings_tab_network(st_pane[ST_NETWORK]);
    settings_tab_display(st_pane[ST_DISPLAY]);
    settings_tab_sound(st_pane[ST_SOUND]);
    settings_tab_game(st_pane[ST_GAME]);
    settings_tab_data(st_pane[ST_DATA]);

    st_select_tab(st_tab);                          /* (a rebuild keeps the tab that was open) */
}

/* (documented in ui_settings.h) */
lv_obj_t *settings_slider(lv_obj_t *row, int min, int max, int value, lv_event_cb_t cb, lv_obj_t **lbl)
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

/* (documented in ui_internal.h) */
void settings_tick(void)
{
    st_wifi_update();
    char sd[64];
    stats_db_status(sd, sizeof(sd));
    if (strcmp(lv_label_get_text(settings_sd_hint()), sd) != 0) {
        lv_label_set_text(settings_sd_hint(), sd);
    }
    usb_btn_update();
}

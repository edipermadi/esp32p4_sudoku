/**
 * @file ui_settings_display.c
 * @ingroup settings
 * @brief Settings > Display: theme, brightness, screen timeout, lock screen, night mode.
 *
 * Dark mode, the accent colour and colour-blind colours rebuild every screen (apply_theme()). Brightness
 * and the screen timeout apply at once. Night mode (night_check()) switches to the dark look and a dimmer
 * backlight between its hours.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void theme_sw_cb(lv_event_t *e);
static void accent_dd_cb(lv_event_t *e);
static void cvd_sw_cb(lv_event_t *e);
static void bright_sl_cb(lv_event_t *e);
static void sleep_dd_cb(lv_event_t *e);
static void saver_dd_cb(lv_event_t *e);
static void night_sw_cb(lv_event_t *e);
static void night_hours_cb(lv_event_t *e);
static void night_bright_cb(lv_event_t *e);

/* "Screen off" dropdown choices, in minutes (0 = never) */
static const uint8_t SLEEP_MIN[] = {1, 2, 5, 10, 15, 30, 0}; /**< Screen off choices, in minutes (0 = never) */
/** The Screen saver dropdown's choices in order (store_saver_t), as its options list them */
static const uint8_t SAVER_ORDER[SAVER_MODE_COUNT] = {SAVER_MODE_DARK, SAVER_MODE_CLOCK, SAVER_MODE_ANALOG, SAVER_MODE_LIFE,
                                                      SAVER_MODE_RAIN, SAVER_MODE_ATTRACTOR, SAVER_MODE_BOIDS, SAVER_MODE_LISSAJOUS,
                                                      SAVER_MODE_PENDULUM, SAVER_MODE_ANT
                                                     };
/** The accent colours, as dropdown options */
static const char *const ACCENT_NAMES = "Blue\nTeal\nGreen\nPurple\nPink";
static uint8_t s_sleep_min = 5;                     /**< screen timeout, 0 = never */

/* Night mode: between the night hours, dark mode and the backlight at most the night brightness.
 * Only the switch-over changes things (night_check), so dark mode or brightness changed by hand
 * during the night stay as they are until morning. s_night: the night look is on now. */
static bool s_night;                                /**< the night look is on now */

static lv_obj_t *st_theme_sw;                       /**< the Dark mode switch */
static lv_obj_t *st_sleep_dd;                       /**< the Screen off dropdown */
static lv_obj_t *st_lock_dd;                        /**< the Lock screen dropdown: Off / Swipe / PIN */
static lv_obj_t *st_pin_btn;                        /**< the Lock PIN row's Change button */
static lv_obj_t *st_bright_sl;                      /**< the Brightness slider */
static lv_obj_t *st_bright_lbl;                     /**< the Brightness value */
/** The night hours and the night brightness's value */
static lv_obj_t *st_night_from;                     /**< the night hours: from */
static lv_obj_t *st_night_to;                       /**< the night hours: to */
static lv_obj_t *st_night_bright_lbl;               /**< the Night brightness value */

/**
 * @brief Dark mode: saved, and every screen rebuilt in it.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void theme_sw_cb(lv_event_t *e)
{
    bool dark = lv_obj_has_state(st_theme_sw, LV_STATE_CHECKED);
    store_set_dark_mode(dark);
    apply_theme(dark);
}

/**
 * @brief Accent colour: saved, and every screen rebuilt in it.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void accent_dd_cb(lv_event_t *e)
{
    store_set_accent((uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e)));
    apply_theme(is_ui_dark());                            /* every screen again, in the new colour */
}

/**
 * @brief Colour-blind friendly colours: saved, and every screen rebuilt in them.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void cvd_sw_cb(lv_event_t *e)
{
    store_set_cvd(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    apply_theme(is_ui_dark());                            /* every screen again, in the new colours */
}

/**
 * @brief Digit size: saved, and the game's board redrawn with it.
 * @param[in] e  the LVGL event (its target is the control)
 */
void big_digits_dd_cb(lv_event_t *e)
{
    store_set_big_digits(lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1);
    for (int i = 0; i < 81; i++) {                  /* the game's board now (Replay: when it opens) */
        if (game_cell_lbl()[i]) {
            lv_obj_set_style_text_font(game_cell_lbl()[i], DIGIT_FONT_N(game_geo() ? game_geo()->n : 9), 0);
        }
        if (game_cells()[i]) {
            lv_obj_invalidate(game_cells()[i]);    /* (the pencil marks too) */
        }
    }
}

/**
 * @brief Brightness: applied while the slider moves (at night, at most the night brightness), saved when let go.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void bright_sl_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(st_bright_sl);
    int nb = store_night_brightness();
    bsp_display_brightness_set(s_night && is_store_night_mode() && nb < v ? nb : v);   /* (at night: at most that) */
    lv_label_set_text_fmt(st_bright_lbl, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        store_set_brightness((uint8_t)v);
    }
}

/**
 * @brief Screen off after: the timeout, saved.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void sleep_dd_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(st_sleep_dd);
    if (sel >= SLEEP_COUNT) {
        return;
    }
    s_sleep_min = SLEEP_MIN[sel];
    store_set_screen_timeout_min(s_sleep_min);
}

/**
 * @brief Screen saver: Dark, Digital clock, Analog clock, Life, Rain, Attractor, Boids, Lissajous, Pendulum or Langton's ant, saved (used by the next screen timeout).
 * @param[in] e  the LVGL event (its target is the dropdown)
 */
static void saver_dd_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if (sel < SAVER_MODE_COUNT) {
        store_set_saver_mode(SAVER_ORDER[sel]);
    }
}

/**
 * @brief Night mode: switches the night look on or off as the night hours start and end.
 *
 * Runs every 20 s, and at once when its settings change. Waits while a dialog is open on the top layer
 * (a theme change rebuilds the screens under it), for 5 minutes at most.
 * @param[in] t  the timer, or NULL when called directly (then it doesn't wait for dialogs)
 */
void night_check(lv_timer_t *t)
{
    static int waited;
    bool want = false;
    time_t now = time(NULL);
    if (is_store_night_mode() && now > 1700000000) {   /* (needs the clock: Wi-Fi) */
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
    bool dark = want || is_store_dark_mode();
    if (dark != is_ui_dark()) {
        apply_theme(dark);
    }
    if (!ui_saver()) {
        bsp_display_brightness_set(effective_brightness());    /* (off stays off) */
    }
    printf("night mode: %s\n", want ? "on" : "off");
}

/**
 * @brief Night mode on or off: saved, and checked at once.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void night_sw_cb(lv_event_t *e)
{
    store_set_night_mode(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
    night_check(NULL);                              /* at once (also off: back to the day look) */
}

/**
 * @brief Night hours (from, to): saved, and checked at once.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void night_hours_cb(lv_event_t *e)
{
    store_set_night_hours((uint8_t)lv_dropdown_get_selected(st_night_from),
                          (uint8_t)lv_dropdown_get_selected(st_night_to));
    night_check(NULL);
}

/**
 * @brief Night brightness: shown at once if it's night now, saved when let go.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void night_bright_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(st_night_bright_lbl, "%d%%", v);
    if (s_night) {
        bsp_display_brightness_set(v < store_brightness() ? v : store_brightness());    /* see it now */
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        store_set_night_brightness((uint8_t)v);
    }
}

/* (documented in ui_settings.h) */
void settings_tab_display(lv_obj_t *pane)
{
    lv_obj_t *col = pane;

    lv_obj_t *r = settings_row(col, "Dark mode", "Light text on a dark background");
    st_theme_sw = settings_switch(r, is_ui_dark(), theme_sw_cb);

    r = settings_row(col, "Accent colour", "Buttons, the selected cell and your digits");
    settings_dropdown(r, ACCENT_NAMES, store_accent(), 150, accent_dd_cb);

    r = settings_row(col, "Colour-blind friendly", "Orange-red and blue-green in place of red and green");
    settings_switch(r, is_store_cvd(), cvd_sw_cb);

    r = settings_row(col, "Digit size", "The board's digits and pencil marks");
    settings_dropdown(r, "Normal\nLarge", is_store_big_digits() ? 1 : 0, 150, big_digits_dd_cb);

    r = settings_row(col, "Brightness", "Screen backlight");
    st_bright_sl = settings_slider(r, 10, 100, store_brightness(), bright_sl_cb, &st_bright_lbl);

    r = settings_row(col, "Screen off after", "Without a touch. The game pauses; tap to wake.");
    uint32_t sel = 0;
    while (sel < SLEEP_COUNT - 1 && SLEEP_MIN[sel] != s_sleep_min) {
        sel++;
    }
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

    r = settings_row(col, "Screen saver", "Until a touch; Dark saves power");
    uint32_t saver_sel = 0;
    while (saver_sel < SAVER_MODE_COUNT - 1 && SAVER_ORDER[saver_sel] != store_saver_mode()) {
        saver_sel++;
    }
    settings_dropdown(r, "Dark\nDigital clock\nAnalog clock\nLife\nRain\nAttractor\nBoids\nLissajous\nPendulum\nLangton's ant", saver_sel, 200, saver_dd_cb);

    r = settings_row(col, "Night mode", "Dark and dimmer at night (needs the clock: Wi-Fi)");
    settings_switch(r, is_store_night_mode(), night_sw_cb);

    static char hours[24 * 6];                      /* "00:00\n01:00\n...23:00" */
    if (!hours[0]) {
        size_t len = 0;
        for (int h = 0; h < 24; h++) {
            len += snprintf(hours + len, sizeof(hours) - len, "%s%02d:00", h ? "\n" : "", h);
        }
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
}

/* (documented in ui_settings.h) */
void night_forget(void)
{
    s_night = false;
}

/* ---- accessors */
/** @return the Screen off choices, in minutes (0 = never) */
const uint8_t *sleep_minutes(void) { return SLEEP_MIN; }
/** @return the night look is on now */
bool is_ui_night(void) { return s_night; }
/** @return the screen timeout in minutes (0 = never) */
uint8_t ui_sleep_min(void) { return s_sleep_min; }
/** @brief Sets the screen timeout in minutes (0 = never). @param[in] v  the value */
void ui_set_sleep_min(uint8_t v) { s_sleep_min = v; }
/** @return the Lock screen dropdown */
lv_obj_t *settings_lock_dd(void) { return st_lock_dd; }
/** @return the Lock PIN row's Change button */
lv_obj_t *settings_pin_btn(void) { return st_pin_btn; }

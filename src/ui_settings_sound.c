/**
 * @file ui_settings_sound.c
 * @ingroup settings
 * @brief Settings > Sound: sound effects, volume, Gibberlink.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void sound_sw_cb(lv_event_t *e);
static void vol_sl_cb(lv_event_t *e);
static void gibberlink_sw_cb(lv_event_t *e);

static lv_obj_t *st_sound_sw;                       /**< the Sound effects switch */
static lv_obj_t *st_vol_sl;                         /**< the Volume slider */
static lv_obj_t *st_vol_lbl;                        /**< the Volume value */

/**
 * @brief Sound effects on or off: applied and saved (switched on: the tap is heard).
 * @param[in] e  the LVGL event (its target is the control)
 */
static void sound_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(st_sound_sw, LV_STATE_CHECKED);
    sfx_set_enabled(on);
    store_set_sound_enabled(on);
    if (on) {
        sfx_play(SFX_TOUCH);    /* the tick for this press was skipped while muted */
    }
}

/**
 * @brief Volume: applied while the slider moves, saved (and a tick played) when let go.
 * @param[in] e  the LVGL event (its target is the control)
 */
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

/**
 * @brief Gibberlink (remote control by sound): saved, and the microphones start or stop listening.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void gibberlink_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    store_set_gibberlink(on);
    gibberlink_set_enabled(on);
}

/* (documented in ui_settings.h) */
void settings_tab_sound(lv_obj_t *pane)
{
    lv_obj_t *col = pane;
    lv_obj_t *r;
    r = settings_row(col, "Sound effects", "Taps, mistakes, finished digits, solved");
    st_sound_sw = settings_switch(r, is_sfx_enabled(), sound_sw_cb);

    r = settings_row(col, "Volume", "Speaker loudness");
    st_vol_sl = settings_slider(r, 0, 100, store_volume(), vol_sl_cb, &st_vol_lbl);

    r = settings_row(col, "Gibberlink", "Remote control by sound (ggwave): the mics listen");
    settings_switch(r, is_store_gibberlink(), gibberlink_sw_cb);
}

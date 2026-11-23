/**
 * @file ui_pin_setup.c
 * @ingroup ui_lock
 * @brief Settings > Display > Lock screen: choosing PIN, or Change PIN, asks for a new PIN twice on a keypad.
 */
#include "ui_internal.h"
#include "lock_core.h"

/* this file's own functions, in any order */
static void pin_setup_close(bool done);
static void pin_setup_key_cb(lv_event_t *e);
static void pin_setup_cancel_cb(lv_event_t *e);
static void pin_setup_open(bool from_dd);

static lv_obj_t *ps_ovl;                  /**< the PIN setup screen (NULL: not shown) */
static lv_obj_t *ps_hint;                 /**< what to do */
static lv_obj_t *ps_dots[4];              /**< the PIN's dots */
static bool ps_from_dd;                   /**< opened by choosing PIN (cancel: back to the old mode) */

/**
 * @brief Closes the PIN setup screen.
 * @param[in] done  a PIN was set (else: cancelled; opened from the dropdown, it goes back to the old mode)
 */
static void pin_setup_close(bool done)
{
    if (ps_ovl) {
        lv_obj_delete_async(ps_ovl);
    }
    ps_ovl = NULL;
    if (!done && ps_from_dd && settings_lock_dd()) {
        lv_dropdown_set_selected(settings_lock_dd(), store_lock_mode());
    }
    if (settings_pin_btn()) {
        lv_obj_set_state(settings_pin_btn(), LV_STATE_DISABLED, store_lock_mode() != LOCK_PIN);
    }
}

/**
 * @brief A key on the PIN setup screen's pad: the PIN typed twice, then set.
 * @param[in] e  the event (user data: the key)
 */
static void pin_setup_key_cb(lv_event_t *e)
{
    pin_setup_t r = pin_setup_key((char)(intptr_t)lv_event_get_user_data(e));
    pin_dots(ps_dots, pin_setup_len(), C_ACCENT);
    switch (r) {
    case PIN_SETUP_MORE:
        break;
    case PIN_SETUP_AGAIN:                           /* once more, to be sure */
        lv_label_set_text(ps_hint, "Enter it again");
        break;
    case PIN_SETUP_MISMATCH:
        pin_dots(ps_dots, 4, C_RED);
        sfx_play(SFX_ERROR);
        lv_label_set_text(ps_hint, "They didn't match: enter a new PIN");
        break;
    case PIN_SETUP_SET:
        pin_setup_close(true);
        sudoku_ui_notice("PIN set: the screen locks with it after the screen timeout.", 3000);
        break;
    }
}

/**
 * @brief The PIN setup screen's Cancel.
 * @param[in] e  the event
 */
static void pin_setup_cancel_cb(lv_event_t *e)
{
    pin_setup_close(false);
}

/**
 * @brief Opens the PIN setup screen.
 * @param[in] from_dd  opened by choosing PIN in the dropdown
 */
static void pin_setup_open(bool from_dd)
{
    if (ps_ovl) {
        return;
    }
    ps_from_dd = from_dd;
    pin_setup_begin();
    ps_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ps_ovl);
    lv_obj_set_size(ps_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ps_ovl, C_BG, 0);
    lv_obj_set_style_bg_opa(ps_ovl, LV_OPA_COVER, 0);
    lv_obj_set_clickable(ps_ovl, true);
    lv_obj_t *t = make_label(ps_ovl, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Lock screen PIN");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
    ps_hint = make_label(ps_ovl, &lv_font_montserrat_20, C_MUTED);
    lv_label_set_text(ps_hint, "Enter a new 4-digit PIN");
    lv_obj_align(ps_hint, LV_ALIGN_TOP_MID, 0, 100);
    pin_dots_make(ps_ovl, ps_dots);
    lv_obj_align(lv_obj_get_parent(ps_dots[0]), LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_t *pad = pin_pad(ps_ovl, pin_setup_key_cb);
    lv_obj_align(pad, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_t *c = make_btn(ps_ovl, 0, 0, 240, 56, "Cancel", &lv_font_montserrat_20, pin_setup_cancel_cb, NULL);
    lv_obj_set_style_bg_color(c, C_CARD2, 0);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -30);
}

/**
 * @brief Settings' Lock screen dropdown: Off, Swipe, or PIN (a PIN set first).
 * @param[in] e  the event
 */
void lock_dd_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(settings_lock_dd());
    if (sel == LOCK_PIN) {                          /* a PIN first */
        pin_setup_open(true);
        return;
    }
    store_set_lock_mode((uint8_t)sel);
    lv_obj_set_state(settings_pin_btn(), LV_STATE_DISABLED, true);
}

/**
 * @brief Settings' Change PIN.
 * @param[in] e  the event
 */
void pin_change_cb(lv_event_t *e)
{
    pin_setup_open(false);
}

/**
 * @file ui_flash_busy.c
 * @brief While the flash is written (a firmware update, the avatars' install): a card over everything, then the screen dark so the panel doesn't flicker.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void fb_keep_cb(lv_timer_t *t);

/* The screen is dark while the flash is written: any flash erase or write keeps the display
 * from its frame buffer (PSRAM) for a moment, and the panel flickers cyan (even sector by sector).
 * Backlight off and the panel asleep, it shows nothing instead. (Also the avatars' install.) */
static lv_obj_t *fb_ovl;                            /**< the card over everything, while busy */
static lv_timer_t *fb_keep;                         /**< keeps the screen timeout away meanwhile */
static bool fb_dark;                                /**< the screen is dark for the flash write */

/**
 * @brief Keeps the screen timeout from firing while the flash is written.
 * @param[in] t  the timer
 */
static void fb_keep_cb(lv_timer_t *t) { lv_display_trigger_activity(NULL); }

/* (documented in ui_sudoku.h) */
void sudoku_ui_flash_busy(const char *title, const char *text)
{
    if (fb_ovl) {
        return;
    }
    fb_ovl = lv_obj_create(lv_layer_top());          /* nothing else meanwhile */
    lv_obj_remove_style_all(fb_ovl);
    lv_obj_set_size(fb_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(fb_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fb_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(fb_ovl, true);
    lv_obj_t *card = lv_obj_create(fb_ovl);
    lv_obj_set_size(card, 520, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(tl, title);
    lv_obj_t *m = make_label(card, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_label_set_text(m, text);
    lv_obj_t *w = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(w, "Don't switch off or take the card out.");
    fb_keep = lv_timer_create(fb_keep_cb, 1000, NULL);
}

/**
 * @brief Turns the screen off (backlight, then the panel asleep) for the flash write.
 *
 * Writing the flash keeps the display from its frame buffer, so the panel would flicker cyan.
 */
void sudoku_ui_flash_dark(void)
{
    if (fb_dark) {
        return;
    }
    fb_dark = true;
    lv_refr_now(NULL);
    bsp_display_brightness_set(0);
    bsp_display_sleep(true);
}

/**
 * @brief Removes the card and, if it was dark, wakes the panel and restores the backlight.
 */
void sudoku_ui_flash_done(void)
{
    if (fb_ovl) {
        lv_obj_delete(fb_ovl);
    }
    fb_ovl = NULL;
    if (fb_keep) {
        lv_timer_delete(fb_keep);
    }
    fb_keep = NULL;
    if (!fb_dark) {
        return;
    }
    fb_dark = false;
    lv_refr_now(NULL);                              /* (drawn without the card before the light comes on) */
    bsp_display_sleep(false);
    bsp_display_brightness_set(effective_brightness());
}

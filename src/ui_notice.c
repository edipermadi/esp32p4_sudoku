/**
 * @file ui_notice.c
 * @ingroup ui_sudoku
 * @brief Notices: a banner at the bottom of the screen for a while, or one that stays while something
 *        runs (a file coming from the computer).
 */
#include "ui_internal.h"

static lv_obj_t *s_progress;                        /**< the progress notice shown (NULL: none) */

/**
 * @brief Removes a notice when its time is up.
 * @param[in] t  the one-shot timer (user data: the notice)
 */
void notice_del_cb(lv_timer_t *t)
{
    lv_obj_delete((lv_obj_t *)lv_timer_get_user_data(t));
}

/**
 * @brief The notice banner, on the top layer (no timer yet); its label is its only child.
 * @param[in] text  its text
 * @return it
 */
static lv_obj_t *notice_make(const char *text)
{
    /* on the top layer so it stays visible across screen changes */
    lv_obj_t *n = lv_obj_create(lv_layer_top());
    lv_obj_set_size(n, SCR_W - 40, LV_SIZE_CONTENT);
    lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(n, C_NOTICE, 0);
    lv_obj_set_style_border_color(n, C_ORANGE, 0);
    lv_obj_set_style_border_width(n, 2, 0);
    lv_obj_set_style_radius(n, 10, 0);
    lv_obj_set_style_pad_all(n, 14, 0);
    lv_obj_set_scrollable(n, false);
    lv_obj_set_clickable(n, false);
    lv_obj_t *l = make_label(n, &lv_font_montserrat_18, C_TEXT);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
    return n;
}

/* (documented in ui_sudoku.h) */
void sudoku_ui_notice(const char *text, uint32_t ms)
{
    lv_obj_t *n = notice_make(text);
    lv_timer_t *t = lv_timer_create(notice_del_cb, ms, n);
    lv_timer_set_repeat_count(t, 1);
}

/* (documented in ui_sudoku.h) */
void sudoku_ui_progress(const char *text)
{
    if (s_progress) {
        lv_label_set_text(lv_obj_get_child(s_progress, 0), text);
    } else {
        s_progress = notice_make(text);
    }
    lv_refr_now(NULL);                              /* (shown now: the caller may hold the UI meanwhile) */
}

/* (documented in ui_sudoku.h) */
void sudoku_ui_progress_done(const char *text, uint32_t ms)
{
    if (!s_progress) {
        sudoku_ui_notice(text, ms);
        return;
    }
    lv_label_set_text(lv_obj_get_child(s_progress, 0), text);
    lv_timer_t *t = lv_timer_create(notice_del_cb, ms, s_progress);
    lv_timer_set_repeat_count(t, 1);
    s_progress = NULL;
}

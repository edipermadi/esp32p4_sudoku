/**
 * @file ui_file.c
 * @ingroup ui_file
 * @brief The Files page: the screen around its parts (the back button, the title), the poll for a
 *        finished read or purge, and the widgets the parts share. The parts: ui_file_part.h; what
 *        the page does on the card: files_core.h.
 */
#include "ui_file.h"
#include "ui_file_part.h"
#include "files_core.h"
#include "ui_media_view.h"
#include "ui_palette.h"

#define POLL_MS      150                /**< how often the page looks for a finished read or purge */


static lv_obj_t *s_scr;                 /**< the page */
static lv_timer_t *s_timer;             /**< poll(), while the page is shown */
static bool s_shown;                    /**< the page is shown */

/* (documented in ui_file_part.h) */
lv_obj_t *file_ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* (documented in ui_file_part.h) */
lv_obj_t *file_ui_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

/* (documented in ui_file_part.h) */
lv_obj_t *file_ui_icon_btn(lv_obj_t *parent, const char *sym, lv_color_t color, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 56, 48);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, C_CARD2, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_center(file_ui_label(b, &lv_font_montserrat_20, color, sym));
    return b;
}

/* (documented in ui_file_part.h) */
lv_obj_t *file_ui_pill_btn(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 56);
    lv_obj_set_style_pad_hor(b, 26, 0);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(file_ui_label(b, &lv_font_montserrat_20, fg, txt));
    return b;
}

/* (documented in ui_file_part.h) */
lv_obj_t *file_ui_screen(void)
{
    return s_scr;
}

/**
 * @brief The page's timer: a finished read is shown; after a purge, the bin is read again.
 * @param t  the timer
 */
static void poll(lv_timer_t *t)
{
    if (files_take_scan()) {
        file_list_show();
    }
    if (files_take_purged()) {                      /* deleted for good: read the bin again */
        file_list_rescan();
    }
}

void file_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb)
{
    media_view_close();                             /* (it lives on the old screen) */
    file_preview_close();
    if (s_timer) {                                  /* rebuilt on a theme change */
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    file_dialog_destroy();
    files_init();
    s_scr = scr;

    /* top bar, as on the other Settings pages */
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_pos(back, 12, 10);
    lv_obj_set_size(back, 150, 52);
    lv_obj_set_style_bg_color(back, C_CARD, 0);
    lv_obj_set_style_bg_color(back, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(file_ui_label(back, &lv_font_montserrat_18, C_TEXT, LV_SYMBOL_LEFT "  Home"));
    lv_obj_set_pos(file_ui_label(scr, &lv_font_montserrat_28, C_TEXT, "Files"), 180, 20);

    file_list_build(scr);                           /* the top bar's buttons, the path, the count, the list */

    file_dialog_build();
    s_timer = lv_timer_create(poll, POLL_MS, NULL);
    if (s_shown) {
        file_list_rescan();    /* (a rebuild while open: list again) */
    } else {
        lv_timer_pause(s_timer);
    }
}

void file_ui_set_shown(bool shown)
{
    if (!s_timer) {
        return;
    }
    s_shown = shown;
    if (shown) {
        lv_timer_resume(s_timer);
        file_list_rescan();                                   /* the folder last open, read afresh */
    } else {
        lv_timer_pause(s_timer);
        file_dialog_close();
        media_view_close();
        file_preview_close();
    }
}

/**
 * @brief Whether Files is reading a folder or deleting for good (devctl genbench waits for it).
 * @return true: busy
 */
bool is_files_busy(void) { return is_files_scanning() || is_files_purging(); }

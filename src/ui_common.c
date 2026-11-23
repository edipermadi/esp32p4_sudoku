/**
 * @file ui_common.c
 * @brief What every screen shares: widgets, small helpers, dialogs (the palette: ui_palette.c; context menus:
 *        ui_ctx_menu.c; players shown small: ui_player_badge.c; the puzzle cache: puzzles_lookup.c).
 */
#include "ui_internal.h"

/* ------------------------------------------------------------- widgets */

/* ============================================================= helpers */

/* (documented in ui_internal.h) */
void fmt_time(char *buf, size_t n, uint32_t s)
{
    if (s >= 3600) {
        snprintf(buf, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    } else {
        snprintf(buf, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

/* (documented in ui_internal.h) */
const char *ordinal(int n)
{
    int t = n % 100;
    if (t >= 11 && t <= 13) {
        return "th";
    }
    return n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
}

/* (documented in ui_internal.h) */
lv_obj_t *make_tab_strip(lv_obj_t *parent, const char *const *map, int checked)
{
    lv_obj_t *tabs = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(tabs, map);
    lv_buttonmatrix_set_button_ctrl_all(tabs, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(tabs, true);
    lv_buttonmatrix_set_button_ctrl(tabs, (uint32_t)checked, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(tabs, LV_PCT(100), 44);
    lv_obj_set_style_pad_all(tabs, 0, 0);
    lv_obj_set_style_pad_column(tabs, 8, 0);
    lv_obj_set_style_margin_top(tabs, 12, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(tabs, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS);   /* a tab: its name, underlined when shown */
    lv_obj_set_style_shadow_width(tabs, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tabs, C_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_border_side(tabs, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_width(tabs, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_color(tabs, C_GRID_THIN, LV_PART_ITEMS);
    lv_obj_set_style_radius(tabs, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(tabs, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tabs, 4, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tabs, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    return tabs;
}

/* (documented in ui_internal.h) */
lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                   const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, C_CARD, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (text) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_obj_center(l);
    }
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

/* (documented in ui_internal.h) */
lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

/* (documented in ui_internal.h) */
lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}


/* ------------------------------------------------------------- dialog text */
/* (documented in ui_internal.h) */
lv_obj_t *make_wrap_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    if (font) {
        lv_obj_set_style_text_font(l, font, 0);
    }
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

/* ------------------------------------------------------------- dialogs */
/* (documented in ui_internal.h) */
void mbox_close_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}
/* (documented in ui_internal.h) */
lv_obj_t *make_msgbox(const char *title, const char *text)
{
    lv_obj_t *m = lv_msgbox_create(NULL);
    lv_msgbox_add_title(m, title);
    /* left-aligned, wrapped; NULL: the caller fills the content */
    if (text) {
        make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT, text);
    }
    lv_obj_set_width(m, 520);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, 0);
    return m;
}
/* (documented in ui_internal.h) */
void msgbox_button(lv_obj_t *m, const char *text, lv_event_cb_t cb, bool primary)
{
    lv_obj_t *b = lv_msgbox_add_footer_button(m, text);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 52);
    if (!primary) {                                 /* Cancel, Close ...: grey, not the dialog's own white */
        lv_obj_set_style_bg_color(b, C_CARD2, 0);
        lv_obj_set_style_text_color(b, C_TEXT, 0);
    }
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, m);
    /* LVGL makes the footer LV_DPI_DEF / 3 (~43 px) tall, less than these
     * 52 px buttons, which then spill over the text and past the box edge */
    lv_obj_t *foot = lv_msgbox_get_footer(m);
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(foot, 10, 0);
    lv_obj_set_style_pad_bottom(foot, 18, 0);                  /* room under the buttons */
}

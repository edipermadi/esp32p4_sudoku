/**
 * @file ui_ctx_menu.c
 * @brief Icon buttons and the ⋮ context menus: a card of icon rows under its button, closed by a tap beside it.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void ctx_backdrop_cb(lv_event_t *e);
static void ctx_item_cb(lv_event_t *e);

static lv_obj_t *ctx_menu;              /**< the open menu's backdrop (NULL: none) */
static lv_obj_t *ctx_btn;               /**< its button */

/* ------------------------------------------------------------- icon buttons, context menus */
/* (documented in ui_internal.h) */
void icon_btn_set(lv_obj_t *b, bool on, lv_color_t off_c)
{
    lv_obj_set_state(b, LV_STATE_CHECKED, on);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), on ? lv_color_white() : off_c, 0);
}

/* (documented in ui_internal.h) */
void ctx_menu_close(void)
{
    if (!ctx_menu) {
        return;
    }
    lv_obj_delete_async(ctx_menu);
    ctx_menu = NULL;
    if (ctx_btn) {
        icon_btn_set(ctx_btn, false, C_TEXT);
    }
    ctx_btn = NULL;
}

/**
 * @brief A tap beside the menu closes it.
 * @param[in] e  the event
 */
static void ctx_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) {
        ctx_menu_close();
    }
}

/**
 * @brief A menu item tapped: the menu closes and the item's action runs.
 * @param[in] e  the event (user data: the item's action)
 */
static void ctx_item_cb(lv_event_t *e)
{
    void (*cb)(lv_event_t *) = (void (*)(lv_event_t *))lv_event_get_user_data(e);
    ctx_menu_close();
    cb(NULL);
}

/* (documented in ui_internal.h) */
lv_obj_t *ctx_card_open(lv_obj_t *btn, int w)
{
    ctx_menu_close();
    ctx_menu = lv_obj_create(lv_layer_top());       /* the backdrop: invisible, catches taps outside */
    lv_obj_remove_style_all(ctx_menu);
    lv_obj_set_size(ctx_menu, SCR_W, SCR_W);
    lv_obj_set_clickable(ctx_menu, true);
    lv_obj_add_event_cb(ctx_menu, ctx_backdrop_cb, LV_EVENT_CLICKED, NULL);
    ctx_btn = btn;
    icon_btn_set(btn, true, C_TEXT);

    lv_area_t a;
    lv_obj_get_coords(btn, &a);
    lv_obj_t *card = lv_obj_create(ctx_menu);
    lv_obj_set_pos(card, LV_MAX(8, a.x2 + 1 - w), a.y2 + 7);   /* (a button on the left: from the edge) */
    lv_obj_set_size(card, w, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_CARD2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_offset_y(card, 4, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(card, false);
    return card;
}

/* (documented in ui_internal.h) */
void ctx_menu_toggle(lv_obj_t *btn, const ctx_item_t *items, int n)
{
    bool was = ctx_menu && ctx_btn == btn;
    ctx_menu_close();
    if (was) {
        return;
    }
    lv_obj_t *card = ctx_card_open(btn, 260);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = make_btn(card, 0, 0, LV_PCT(100), 56, NULL, NULL, items[i].disabled ? NULL : ctx_item_cb,
                               (void *)items[i].cb);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_clickable(b, !items[i].disabled);
        if (items[i].disabled) {
            lv_obj_set_style_opa(b, LV_OPA_40, 0);
        }
        lv_obj_t *ic = make_label(b, &font_icons_20, items[i].danger ? C_RED : C_ACCENT);   /* own column; LVGL's
                                                                     symbols and a few FontAwesome ones (Montserrat 20 fallback) */
        lv_label_set_text(ic, items[i].icon);
        lv_obj_set_width(ic, 32);
        lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *l = make_label(b, &lv_font_montserrat_18, items[i].danger ? C_RED : C_TEXT);
        lv_label_set_text(l, items[i].text);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8 + 32 + 12, 0);
    }
}

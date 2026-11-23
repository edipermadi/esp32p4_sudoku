/**
 * @file ui_avatar_picker.c
 * @ingroup ui_players
 * @brief The avatar picker (Players > ⋮ > Avatar): every emoji in a scrolling grid, a row of group buttons, None.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void player_avatar_set(int k);
static void av_none_cb(lv_event_t *e);
static void av_grid_click_cb(lv_event_t *e);
static void av_grid_draw_cb(lv_event_t *e);
static void av_scroll_cb(lv_event_t *e);
static void av_group_cb(lv_event_t *e);

static lv_obj_t *av_scroll;               /**< the picker's emoji, scrolling */
static lv_obj_t *av_grid;                 /**< ... drawn on this */
static lv_obj_t *av_group_btn[AVATAR_GROUPS];   /**< the groups' buttons (each one's first emoji) */
static int av_cur;                        /**< the player's avatar, 0 = none */

/**
 * @brief The picker's choice: who plays now gets it, the picker closes.
 * @param[in] k  1..AVATAR_COUNT, 0 = none
 */
static void player_avatar_set(int k)
{
    ctx_menu_close();
    av_scroll = av_grid = NULL;
    if (players_set_avatar(k)) {
        render_players();
    }
}

/**
 * @brief The picker's None.
 * @param[in] e  the event
 */
static void av_none_cb(lv_event_t *e)
{
    player_avatar_set(0);
}

/**
 * @brief A tap on the picker's emoji: that one.
 * @param[in] e  the event
 */
static void av_grid_click_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(av_grid, &a);
    int k = players_av_at((p.y - a.y1) / AV_CELL, (p.x - a.x1) / AV_CELL);
    if (k >= 0) {
        player_avatar_set(k + 1);    /* (not the empty end of a group's last row) */
    }
}

/**
 * @brief Draws the picker's emoji (just the rows in view), the player's own outlined.
 * @param[in] e  the draw event
 */
static void av_grid_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(av_grid, &a);
    int top = lv_obj_get_scroll_y(av_scroll), h = lv_obj_get_height(av_scroll);
    int r0 = top / AV_CELL - 1, r1 = (top + h) / AV_CELL + 1;   /* just the rows in view */
    if (r0 < 0) {
        r0 = 0;
    }
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    for (int r = r0; r <= r1 && r < players_av_rows(); r++) {
        for (int c = 0; c < AV_COLS; c++) {
            int k = players_av_at(r, c);
            if (k < 0) {
                break;
            }
            lv_area_t cell = {a.x1 + c * AV_CELL, a.y1 + r * AV_CELL, a.x1 + (c + 1) * AV_CELL - 1,
                              a.y1 + (r + 1) * AV_CELL - 1
                             };
            if (k + 1 == av_cur) {                   /* the one they have */
                lv_draw_rect_dsc_t rd;
                lv_draw_rect_dsc_init(&rd);
                rd.bg_opa = LV_OPA_TRANSP;
                rd.border_color = C_ACCENT;
                rd.border_width = 3;
                rd.radius = 12;
                lv_draw_rect(layer, &rd, &cell);
            }
            int o = (AV_CELL - AVATAR_SIZE) / 2;
            lv_area_t im = {cell.x1 + o, cell.y1 + o, cell.x1 + o + AVATAR_SIZE - 1, cell.y1 + o + AVATAR_SIZE - 1};
            d.src = avatar_image(k);
            lv_draw_image(layer, &d, &im);
        }
    }
}

/**
 * @brief The group at the top of the view, checked in the row of groups.
 * @param[in] e  the event (may be NULL)
 */
static void av_scroll_cb(lv_event_t *e)
{
    int g = players_av_row_group((lv_obj_get_scroll_y(av_scroll) + AV_CELL / 2) / AV_CELL);
    for (int i = 0; i < AVATAR_GROUPS; i++) {
        lv_obj_set_state(av_group_btn[i], LV_STATE_CHECKED, i == g);
    }
}

/**
 * @brief A group's button: scrolls to it.
 * @param[in] e  the event (user data: the group)
 */
static void av_group_cb(lv_event_t *e)
{
    int g = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_scroll_to_y(av_scroll, players_av_row_first(g) * AV_CELL, LV_ANIM_ON);
}

/* (documented in ui_internal.h) */
void player_avatar_cb(lv_event_t *e)
{
    const stats_player_t *pl = players_current();
    if (!pl) {
        return;
    }
    av_cur = pl->avatar <= AVATAR_COUNT ? pl->avatar : 0;
    players_av_layout(AV_COLS);
    const int inner = AV_COLS * AV_CELL, card_h = SCR_W - 68 - 16;
    lv_obj_t *card = ctx_card_open(players_menu_btn(), inner + 2 * 6 + 2);
    lv_obj_set_height(card, card_h);

    /* the title and None */
    lv_obj_t *top = lv_obj_create(card);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, inner, 44);
    lv_obj_t *t = make_label(top, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text_fmt(t, "Avatar for %s", pl->name);
    lv_obj_set_width(t, inner - 120);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_t *none = make_btn(top, 0, 0, 104, 40, LV_SYMBOL_CLOSE "  None", &lv_font_montserrat_16, av_none_cb, NULL);
    lv_obj_set_style_bg_color(none, C_CARD2, 0);
    if (!av_cur) {
        lv_obj_set_style_border_color(none, C_ACCENT, 0);
        lv_obj_set_style_border_width(none, 2, 0);
    }
    lv_obj_align(none, LV_ALIGN_RIGHT_MID, 0, 0);

    /* the groups: each one's first emoji */
    lv_obj_t *gr = lv_obj_create(card);
    lv_obj_remove_style_all(gr);
    lv_obj_set_size(gr, inner, 48);
    lv_obj_set_flex_flow(gr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int g = 0; g < AVATAR_GROUPS; g++) {
        lv_obj_t *b = make_btn(gr, 0, 0, 46, 46, NULL, NULL, av_group_cb, (void *)(intptr_t)g);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_CHECKED);   /* (not the accent: the emoji's colours) */
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_t *im = lv_image_create(b);
        lv_image_set_src(im, avatar_image(avatar_group_first(g)));
        lv_obj_center(im);
        av_group_btn[g] = b;
    }
    lv_obj_t *line = lv_obj_create(card);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, inner, 1);
    lv_obj_set_style_bg_color(line, C_CARD2, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    /* the emoji */
    av_scroll = lv_obj_create(card);
    lv_obj_remove_style_all(av_scroll);
    lv_obj_set_width(av_scroll, inner);
    lv_obj_set_flex_grow(av_scroll, 1);
    lv_obj_set_scroll_dir(av_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(av_scroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(av_scroll, av_scroll_cb, LV_EVENT_SCROLL, NULL);
    av_grid = lv_obj_create(av_scroll);
    lv_obj_remove_style_all(av_grid);
    lv_obj_set_size(av_grid, inner, players_av_rows() * AV_CELL);
    lv_obj_set_scrollable(av_grid, false);
    lv_obj_set_clickable(av_grid, true);
    lv_obj_add_event_cb(av_grid, av_grid_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(av_grid, av_grid_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(card);
    if (av_cur) {                                    /* to theirs */
        lv_obj_scroll_to_y(av_scroll, players_av_row_of(av_cur - 1) * AV_CELL, LV_ANIM_OFF);
    }
    av_scroll_cb(NULL);
}

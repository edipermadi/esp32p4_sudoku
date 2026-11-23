/**
 * @file ui_game_colors.c
 * @ingroup ui_game
 * @brief A cell long pressed: its colour menu (and, in a Killer cage, the sets of digits that make its sum).
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void cm_close(void);
static void cm_backdrop_cb(lv_event_t *e);
static void cm_color_cb(lv_event_t *e);
static int cage_sets_text(int k, char *buf, size_t n);

/* ---- the cell menu (long press a cell): paint it, and on Killer the cage's digit sets */
static lv_obj_t *cm_ovl;                 /**< the cell menu (NULL: closed) */
static int cm_cell;                      /**< its cell */

/* (documented in ui_internal.h) */
void cell_long_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused()) {
        return;
    }
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    cm_close();
    cm_cell = i;
    cm_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(cm_ovl);
    lv_obj_set_size(cm_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cm_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cm_ovl, LV_OPA_30, 0);
    lv_obj_set_clickable(cm_ovl, true);
    lv_obj_add_event_cb(cm_ovl, cm_backdrop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *card = lv_obj_create(cm_ovl);
    lv_obj_set_size(card, 520, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_scrollable(card, false);
    /* above the cell's row when it's low on the board, else below it */
    lv_area_t co;
    lv_obj_get_coords(game_cells()[i], &co);
    lv_obj_align(card, co.y1 > SCR_W / 2 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, co.y1 > SCR_W / 2 ? 70 : -20);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text_fmt(t, "Row %d, column %d: colour", i / 9 + 1, i % 9 + 1);
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    for (int k = 0; k <= CELL_COLORS; k++) {        /* none, then the colours */
        lv_obj_t *b = make_btn(row, 0, 0, 58, 56, k ? NULL : LV_SYMBOL_CLOSE, &lv_font_montserrat_20, cm_color_cb,
                               (void *)(intptr_t)k);
        if (k) {
            lv_obj_set_style_bg_color(b, cell_color(k), 0);
        } else {
            lv_obj_set_style_bg_color(b, C_CARD2, 0);
        }
        if (game_board()->colors[i] == k) {
            lv_obj_set_style_border_color(b, C_TEXT, 0);
            lv_obj_set_style_border_width(b, 3, 0);
        }
    }
    int k = game_geo()->cage_of[i];
    if (k != CAGE_NONE && game_geo()->cage_sum[k]) {   /* Killer helper (Sumless: no sum to help with) */
        char sets[240];
        int n = cage_sets_text(k, sets, sizeof(sets));
        lv_obj_t *h = make_label(card, &lv_font_montserrat_16, C_MUTED);
        lv_label_set_text_fmt(h, "Cage: %d in %d cells, %d way%s%s", game_geo()->cage_sum[k], game_geo()->cage_n[k], n,
                              n == 1 ? "" : "s", n ? ":" : " (check the digits in it)");
        if (n) {
            lv_obj_t *l = make_label(card, &lv_font_montserrat_20, C_TEXT);
            lv_obj_set_width(l, LV_PCT(100));
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
            lv_label_set_text(l, sets);
        }
    }
}

/**
 * @brief Closes the cell menu.
 */
static void cm_close(void)
{
    if (cm_ovl) {
        lv_obj_delete_async(cm_ovl);
    }
    cm_ovl = NULL;
}

/**
 * @brief A tap beside the cell menu closes it.
 * @param[in] e  the event
 */
static void cm_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) {
        cm_close();
    }
}

/**
 * @brief A colour picked in the cell menu: the cell painted (undo takes it back).
 * @param[in] e  the event (user data: the colour)
 */
static void cm_color_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    int i = cm_cell;
    cm_close();
    if (is_game_solved() || is_game_paused() || i < 0 || game_board()->colors[i] == k) {
        return;
    }
    push_undo();
    game_board()->colors[i] = (uint8_t)k;
    after_move();                                   /* redraws and saves (colours aren't in the move log) */
}

/**
 * @brief the sets of different digits that make up a cage's sum and hold the digits
 * already in it, as text ("1+5+9   2+4+9 ..."); how many sets
 * @param[in] k  the cage
 * @param[out] buf  gets the text
 * @param[in] n  its size
 * @return how many sets
 */
static int cage_sets_text(int k, char *buf, size_t n)
{
    int size = game_geo()->cage_n[k], sum = game_geo()->cage_sum[k], count = 0;
    uint16_t have = 0;
    for (int j = 0; j < size; j++) {
        int v = game_board()->val[game_geo()->cage_cell[k][j]];
        if (v) {
            have |= (uint16_t)(1u << v);
        }
    }
    size_t len = 0;
    buf[0] = 0;
    for (uint16_t m = 2; m < 0x400; m += 2) {       /* digit sets as bits 1..9 */
        if (__builtin_popcount(m) != size || (m & have) != have) {
            continue;
        }
        int s = 0;
        for (int d = 1; d <= 9; d++) {
            s += (m >> d & 1) * d;
        }
        if (s != sum) {
            continue;
        }
        count++;
        if (len + 24 >= n) {
            continue;    /* (the count still goes on) */
        }
        len += snprintf(buf + len, n - len, "%s", count > 1 ? "     " : "");
        bool first = true;
        for (int d = 1; d <= 9; d++) {
            if (!(m >> d & 1)) {
                continue;
            }
            len += snprintf(buf + len, n - len, first ? "%d" : "+%d", d);
            first = false;
        }
    }
    return count;
}

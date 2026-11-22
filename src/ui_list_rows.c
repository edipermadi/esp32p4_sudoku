/**
 * @file ui_list_rows.c
 * @ingroup ui_list
 * @brief The Puzzles screen's right pane as rows: a saved or imported collection's puzzles (Saved tab:
 *        variant, level, solved; a saved one's can be taken out), and the row look Starred uses too.
 */
#include "ui_internal.h"
#include "ui_list_part.h"
#include "puzzles_core.h"

static lv_obj_t *l_list;                  /**< the rows */

/* (documented in ui_list_part.h) */
lv_obj_t *list_rows_build(lv_obj_t *scr)
{
    l_list = lv_obj_create(scr);
    lv_obj_remove_style_all(l_list);
    lv_obj_set_pos(l_list, PANE_X, L_GRID_Y);
    lv_obj_set_size(l_list, PANE_W, SCR_W - L_GRID_Y - 24);
    lv_obj_set_flex_flow(l_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l_list, 6, 0);
    lv_obj_set_scroll_dir(l_list, LV_DIR_VER);
    lv_obj_set_hidden(l_list, true);
    return l_list;
}

/* (documented in ui_list_part.h) */
void list_rows_clear(void)
{
    lv_obj_clean(l_list);
    lv_obj_scroll_to_y(l_list, 0, LV_ANIM_OFF);
}

/* (documented in ui_list_part.h) */
void list_rows_note(const char *text)
{
    lv_obj_t *t = make_label(l_list, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(t, 40, 0);
    lv_label_set_text(t, text);
}

/**
 * @brief A row of the collection: its puzzle, to play.
 * @param[in] e  the event (user data: the row)
 */
static void list_row_cb(lv_event_t *e)
{
    int lc = puzzles_coll();
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= coll_size(lc)) {
        return;
    }
    game_set_from_coll(lc);
    open_puzzle(coll_puzzle(lc, i));
}

/**
 * @brief A saved collection's row's trash: the puzzle taken out (Save on the game screen puts it
 *        back).
 * @param[in] e  the event (user data: the row)
 */
static void list_row_del_cb(lv_event_t *e)
{
    char msg[128];
    int r = puzzles_take_out(puzzles_coll(), (int)(intptr_t)lv_event_get_user_data(e), msg, sizeof(msg));
    if (r < 0) {
        return;
    }
    if (r == 0) {
        sudoku_ui_notice("Couldn't change sudoku.db.", 2500);
        return;
    }
    sv_changed();
    list_reveal_coll(LV_ANIM_OFF);
    sudoku_ui_notice(msg, 3000);
}

/* (documented in ui_list_part.h) */
void list_add_row(int i, uint16_t id, const char *sub, lv_color_t sub_c, lv_event_cb_t cb,
                  const char *act, lv_color_t act_c, lv_event_cb_t act_cb)
{
    lv_obj_t *r = lv_obj_create(l_list);
    lv_obj_set_size(r, LV_PCT(100), 68);
    lv_obj_set_style_bg_color(r, C_CARD, 0);
    lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_set_style_pad_ver(r, 0, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *num = make_label(r, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text_fmt(num, "%d", i + 1);
    lv_obj_set_width(num, 36);
    lv_obj_align(num, LV_ALIGN_LEFT_MID, 0, 0);
    char name[48];
    puzzle_name(id, name, sizeof(name));
    const int w = PANE_W - 28 - 40 - (act ? 60 : 0);
    lv_obj_t *nm = make_label(r, &lv_font_montserrat_20, C_TEXT);
    lv_label_set_text(nm, name);
    lv_obj_set_width(nm, w);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 40, -12);
    lv_obj_t *sl = make_label(r, &lv_font_montserrat_14, sub_c);
    lv_label_set_text(sl, sub);
    lv_obj_set_width(sl, w);
    lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 40, 14);
    if (act) {
        lv_obj_t *d = make_btn(r, 0, 0, 52, 44, act, strcmp(act, SYMBOL_STAR) ? &lv_font_montserrat_20 : &font_icons_20,
                               act_cb, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(d, C_CARD2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(d, 0), act_c, 0);
        lv_obj_align(d, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

/**
 * @brief Fills the rows with a collection's puzzles: variant, level, solved / in progress / not yet;
 *        a saved collection's with a trash button.
 * @param[in] lc  the collection (after puzzles_solved_count() on it)
 */
void list_fill_rows(int lc)
{
    list_rows_clear();
    bool saved = lc >= PUZZLE_COLLECTION_COUNT && coll_saved_index(lc) >= 0;
    for (int i = 0; i < coll_size(lc) && i < 128; i++) {
        uint16_t id = coll_puzzle(lc, i);
        char sub[96];
        int v = VAR_CLASSIC, lv = 0;
        gen_lookup(id, NULL, &v, &lv);
        bool solved = is_puzzles_solved(i), active = is_game_in_progress(id);
        snprintf(sub, sizeof(sub), "%s %s   -   %s", v >= 0 && v < VAR_COUNT ? variant_name(v) : "",
                 lv >= 0 && lv < GEN_LEVELS ? gen_level_name(lv) : "",
                 solved ? "Solved" : active ? "In progress" : "Not solved yet");
        list_add_row(i, id, sub, solved ? C_GREEN : active ? C_ORANGE : C_MUTED, list_row_cb,
                     saved ? LV_SYMBOL_TRASH : NULL, C_RED, list_row_del_cb);   /* take it out of the collection */
    }
}

/**
 * @file ui_hints.c
 * @ingroup ui_game
 * @brief The hint card: the next step, its words (hint_core.c) and its highlights.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */

static uint8_t  h_mark[81];      /**< hint highlight: 0, 1 = where to look, 2 = the cell */

/* ------------------------------------------------------------- hints */
/* closes the hint without redrawing (the caller does) */
/* (documented in ui_internal.h) */
bool is_hint_nudge(void)
{
    return is_store_hint_nudge() && game_practice() < 0;
}

/* (documented in ui_internal.h) */
void hint_drop(void)
{
    if (!hint_step()) {
        return;
    }
    hint_set_step(0);
    memset(h_mark, 0, sizeof(h_mark));
    lv_obj_set_hidden(hint_card(), true);
}

/* (documented in ui_internal.h) */
void hint_render(void)
{
    char text[640];
    const char *act = hint_explain(game_geo(), hint_info(), hint_step(), is_hint_nudge(), text, sizeof(text), h_mark);
    lv_label_set_text(lv_obj_get_child(hint_act(), 0), act);
    lv_label_set_text(hint_text(), text);
    lv_obj_set_hidden(hint_act(), hint_step() == 1 && is_hint_nudge());   /* Settings > Game > Hints: Nudge only */

    /* the card at the bottom, or at the top when the bottom row matters */
    /* as tall as its text needs (a technique and its single take 4-6 lines), at least the
     * buttons' height; at the bottom (growing upwards) unless it would cover a highlighted
     * cell there, then at the top (growing downwards) */
    lv_obj_update_layout(hint_text());
    int32_t ch = lv_obj_get_height(hint_text()) + 28;
    if (ch < 92) {
        ch = 92;
    }
    lv_obj_set_height(hint_card(), ch);
    bool covered = false;
    for (int i = 0; i < 81 && !covered; i++) {
        if (!h_mark[i] || !game_cells()[i]) {
            continue;
        }
        lv_area_t co;
        lv_obj_get_coords(game_cells()[i], &co);
        covered = co.y2 > SCR_W - 4 - ch;
    }
    lv_obj_set_y(hint_card(), covered ? 4 : SCR_W - 4 - ch);
    lv_obj_set_hidden(hint_card(), false);
    refresh_board();
}


/* ---- accessors: the other UI files reach this file's state through these */
const uint8_t *hint_mark(void) { return h_mark; }

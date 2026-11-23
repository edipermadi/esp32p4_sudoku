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


/* ------------------------------------------------------------- the card's buttons */
/* (documented in ui_internal.h) */
void hint_act_cb(lv_event_t *e)
{
    if (!hint_step() || is_game_solved() || is_game_paused()) {
        return;
    }
    if (hint_step() == 1 && is_hint_nudge()) {
        return;
    }
    if (hint_step() == 1) {
        hint_set_step(2);
        hint_render();
        return;
    }
    int i = hint_info()->cell, d = hint_info()->digit;
    bool wrong = hint_info()->kind == STEP_WRONG;
    push_undo();
    if (wrong) {
        record_move(i, 0, MOVE_ERASE);
        game_board()->val[i] = 0;
    } else {
        place_digit(i, d);
        record_move(i, d, MOVE_FILL);
        if (game_practice() >= 0) {
            stats_db_learned(game_practice());    /* Learn: this technique practised (Graduate) */
        }
    }
    game_set_sel(i);
    after_move();                                   /* closes the hint */
    if (!wrong && !is_game_solved()) {
        placed_sounds(i, d);
    }
}

/* (documented in ui_internal.h) */
void hint_close_cb(lv_event_t *e)
{
    hint_drop();
    refresh_board();
}

/* (documented in ui_internal.h) */
void hint_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused() || !is_game_sol_ok()) {
        return;
    }
    if (is_game_attack()) {
        sudoku_ui_notice("Fill and Hint are off in a time attack.", 2500);
        return;
    }
    if (hint_step()) {                                   /* already open: same as More / Place it */
        if (is_hint_nudge()) {
            return;    /* (a nudge goes no further) */
        }
        hint_act_cb(e);
        return;
    }
    sudoku_step_t st;
    if (!sudoku_next_step(game_geo(), game_b()->val, game_sol(), &st) || st.cell < 0) {
        return;
    }
    hint_set_info(&st);
    if (game_hints() < 0xFFFF) {
        game_set_hints(game_hints() + 1);    /* every hint opened counts once */
    }
    if (hint_info()->tech.kind > 0 && hint_info()->tech.kind < 16) {
        game_set_flags(game_flags() | 1u << hint_info()->tech.kind);    /* (achievements) */
    }
    refresh_board();                                /* the hint count on the Fill button */
    save_current();
    hint_set_step(1);
    hint_render();
}

/* ---- accessors: the other UI files reach this file's state through these */
const uint8_t *hint_mark(void) { return h_mark; }

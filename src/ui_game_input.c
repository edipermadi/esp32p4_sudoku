/**
 * @file ui_game_input.c
 * @ingroup ui_game
 * @brief The game screen's input: a cell tapped, the number pad (and its long press: digit-first), Notes, Auto notes, Erase, Undo, Redo, Fill.
 */
#include "ui_internal.h"

/* (documented in ui_internal.h) */
void cell_click_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused()) {
        return;
    }
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    game_set_sel(i);
    if (game_lock() && !game_given()[i]) {                    /* digit-first: the locked digit goes in (or its note) */
        enter_digit(game_lock());
        return;
    }
    refresh_board();
}

/* (documented in ui_internal.h) */
void pad_click_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (game_lock()) {
        game_set_lock(game_lock() == d ? 0 : d);
        refresh_board();
        return;
    }
    enter_digit(d);
}

/* (documented in ui_internal.h) */
void pad_long_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused()) {
        return;
    }
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    game_set_lock(game_lock() == d ? 0 : d);
    refresh_board();
}

/* (documented in ui_internal.h) */
void auto_notes_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused() || (game_flags() & GAME_FLAG_NO_NOTES)) {
        return;
    }
    uint16_t notes[81];
    int changed = 0;
    for (int k = 0; k < game_geo()->ncells; k++) {
        int c = game_geo()->cells[k];
        notes[c] = game_board()->notes[c];
        if (game_board()->val[c]) {
            continue;
        }
        uint16_t m = game_geo()->all;
        for (int p = 0; p < game_geo()->npeers[c]; p++) {
            m &= (uint16_t)~(1u << game_board()->val[game_geo()->peers[c][p]]);
        }
        notes[c] = m;
        changed += m != game_board()->notes[c];
    }
    if (!changed) {
        sudoku_ui_notice("The notes already show every digit that fits.", 2500);
        return;
    }
    push_undo();
    for (int k = 0; k < game_geo()->ncells; k++) {
        game_board()->notes[game_geo()->cells[k]] = notes[game_geo()->cells[k]];
    }
    after_move();
}

/* (documented in ui_internal.h) */
void erase_cb(lv_event_t *e)
{
    if (!can_edit()) {
        return;
    }
    if (!game_board()->val[game_sel()] && !game_board()->notes[game_sel()]) {
        return;
    }
    push_undo();
    if (game_board()->val[game_sel()]) {
        record_move(game_sel(), 0, MOVE_ERASE);
    }
    game_board()->val[game_sel()] = 0;
    game_board()->notes[game_sel()] = 0;
    after_move();
}

/* (documented in ui_internal.h) */
void undo_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused() || game_hist()->len == 0) {
        return;
    }
    uint8_t before[81];
    memcpy(before, game_board()->val, sizeof(before));
    game_history_undo(game_hist(), game_board());
    for (int i = 0; i < 81; i++) {
        if (game_board()->val[i] != before[i]) {
            record_move(i, game_board()->val[i], MOVE_UNDO);
        }
    }
    after_move();
}

/* (documented in ui_internal.h) */
void redo_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused() || game_hist()->redo_len == 0) {
        return;
    }
    uint8_t before[81];
    memcpy(before, game_board()->val, sizeof(before));
    game_history_redo(game_hist(), game_board());
    for (int i = 0; i < 81; i++) {
        if (game_board()->val[i] != before[i]) {
            record_move(i, game_board()->val[i], MOVE_REDO);
        }
    }
    after_move();
}

/* (documented in ui_internal.h) */
void notes_cb(lv_event_t *e)
{
    if (game_flags() & GAME_FLAG_NO_NOTES) {             /* (the button is off in the challenge anyway) */
        lv_obj_remove_state(game_notes_btn(), LV_STATE_CHECKED);
        return;
    }
    game_set_notes_mode(lv_obj_has_state(game_notes_btn(), LV_STATE_CHECKED));
}

/* (documented in ui_internal.h) */
void fill_cb(lv_event_t *e)
{
    if (is_game_solved() || is_game_paused()) {
        return;
    }
    if (is_game_attack()) {
        sudoku_ui_notice("Fill and Hint are off in a time attack.", 2500);
        return;
    }
    if (!is_game_sol_ok()) {
        sudoku_ui_notice("Fill isn't available: this puzzle has no solution.", 3000);
        return;
    }
    if (game_sel() < 0 || game_given()[game_sel()]) {
        sudoku_ui_notice("Tap an empty cell first, then Fill.", 2500);
        return;
    }
    int i = game_sel(), d = game_sol()[i];
    if (game_board()->val[i] == d) {
        sudoku_ui_notice("That cell is already right.", 2000);
        return;
    }
    push_undo();
    place_digit(i, d);
    record_move(i, d, MOVE_FILL);
    if (game_hints() < 0xFFFF) {
        game_set_hints(game_hints() + 1);
    }
    after_move();
    if (!is_game_solved()) {
        placed_sounds(i, d);
    }
}

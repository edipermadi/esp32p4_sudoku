/**
 * @file ui_continue_open.c
 * @ingroup ui_continue
 * @brief open_puzzle(): a puzzle opened while another game may be in progress.
 */
#include "ui_internal.h"
#include "ui_continue_part.h"

static uint16_t l_pending_id;           /**< the puzzle to open once another game in progress is dealt with */

/**
 * @brief Another game in progress: discard it and start the new one.
 * @param[in] e  the event (user data: the dialog)
 */
static void discard_confirm_cb(lv_event_t *e)
{
    store_clear_game();
    start_game(l_pending_id, false);
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

/**
 * @brief Another game in progress: resume that one instead.
 * @param[in] e  the event (user data: the dialog)
 */
static void resume_other_cb(lv_event_t *e)
{
    start_game(store_saved_game()->id, true);
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

/**
 * @brief Without a card there's one slot: the "Unfinished game" dialog asks whether to resume the game in it or
 *        discard it for this one.
 * @param[in] id  the puzzle to open
 */
static void ask_one_slot(uint16_t id)
{
    const saved_game_t *s = store_saved_game();
    char cur[40], next[40], t[16], text[200];
    puzzle_name(s->id, cur, sizeof(cur));
    puzzle_name(id, next, sizeof(next));
    fmt_time(t, sizeof(t), s->elapsed_ms / 1000);
    snprintf(text, sizeof(text), "%s is in progress (%s).\nStarting %s will discard it.", cur, t, next);
    l_pending_id = id;
    lv_obj_t *m = make_msgbox("Unfinished game", text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Resume", resume_other_cb, false);
    msgbox_button(m, "Discard", discard_confirm_cb, true);
}

/* (documented in ui_internal.h) */
void open_puzzle(uint16_t id)
{
    const saved_game_t *s = store_saved_game();
    if (s->id == STORE_NO_GAME || s->id == id) {
        if (s->id != id && is_stats_db_ready() && continue_resume(id)) {
            return;    /* an unfinished game in sudoku.db */
        }
        start_game(id, s->id == id);
        return;
    }
    if (is_stats_db_ready()) {
        /* sudoku.db keeps several unfinished games: the current one is saved,
         * this one is resumed if it was started before, else begun */
        if (!continue_resume(id)) {
            start_game(id, false);
        }
        return;
    }
    ask_one_slot(id);
}

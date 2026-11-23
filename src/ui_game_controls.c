/**
 * @file ui_game_controls.c
 * @ingroup ui_game
 * @brief The game screen's controls: the ⋮ menu (star, restart, ...), Back, Pause and the clock's tick.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void star_cb(lv_event_t *e);
static void restart_confirm_cb(lv_event_t *e);
static void atk_stop_cb(lv_event_t *e);
static void atk_keep_cb(lv_event_t *e);

/* (documented in ui_internal.h) */
void game_menu_cb(lv_event_t *e)
{
    const ctx_item_t items[] = {
        {SYMBOL_BULB, "Rules", rules_cb, false, false},
        {SYMBOL_QR, "Share", share_cb, game_id() == STORE_NO_GAME, false},
        {SYMBOL_STAR, is_game_starred() ? "Unstar" : "Star", star_cb, !is_game_starable(), false},
        /* made on the board only (built-in and imported puzzles have their collection) */
        {LV_SYMBOL_SAVE, is_game_in_saved() ? "Saved: collections" : "Save to collection", save_cb, !is_game_saveable(), false},
        {
            LV_SYMBOL_LIST, "Auto notes", auto_notes_cb,        /* all the marks at once (Undo takes them back) */
            is_game_solved() || (game_flags() & GAME_FLAG_NO_NOTES) != 0, false
        },
        {LV_SYMBOL_REFRESH, "Restart", restart_cb, false, false},   /* (asks first once there's progress) */
    };
    ctx_menu_toggle(game_bulb_btn(), items, (int)(sizeof(items) / sizeof(items[0])));
}

/**
 * @brief Star: bookmarks the puzzle (Puzzles > Starred), or unstars it.
 * @param[in] e  the event
 */
static void star_cb(lv_event_t *e)
{
    if (game_practice() >= 0 || game_id() >= PUZZLE_GEN_RAM_BASE) {
        return;
    }
    game_set_starred(!is_game_starred());
    stats_db_set_bookmark(game_id(), is_game_starred());
    update_header();
    sudoku_ui_notice(is_game_starred() ? "Starred: it's under Puzzles > Starred." : "No longer starred.", 2000);
}

/* ------------------------------------------------------------- restart, back, the clock */
/**
 * @brief Restart confirmed: the game back to its givens.
 * @param[in] e  the event (user data: the dialog)
 */
static void restart_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (store_saved_game()->id == game_id()) {
        store_clear_game();
    }
    reset_game();
    lv_msgbox_close_async(mbox);
}

/* (documented in ui_internal.h) */
void restart_cb(lv_event_t *e)
{
    if (!is_game_solved() && !has_progress()) {
        reset_game();
        return;
    }
    lv_obj_t *m = make_msgbox("Restart puzzle?", "All entries and the timer will be reset.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Restart", restart_confirm_cb, true);
}

/**
 * @brief The time attack's Stop: the run ends (not recorded).
 * @param[in] e  the event (user data: the dialog)
 */
static void atk_stop_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    game_set_attack(false);                               /* (not recorded: the run wasn't finished) */
    game_set_solved(true);
    show_home();
}

/**
 * @brief The time attack's Keep going: back to the puzzle.
 * @param[in] e  the event (user data: the dialog)
 */
static void atk_keep_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    set_paused(false);
}

/* (documented in ui_internal.h) */
void back_cb(lv_event_t *e)
{
    if (is_game_attack() && !is_game_solved()) {
        set_paused(true);
        lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Stop the run?", "A time attack only counts when its 10 "
                                  "minutes are up. Puzzles solved so far still count as solves.");
        msgbox_button(m, "Keep going", atk_keep_cb, false);
        msgbox_button(m, "Stop", atk_stop_cb, true);
        return;
    }
    save_current();
    show_list();
}

/* (documented in ui_internal.h) */
void tick_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    bool running = game_id() != STORE_NO_GAME && lv_screen_active() == screen_game() && !is_game_solved() && !is_game_paused() && !is_tour_open();
    if (running) {
        game_set_elapsed_ms(game_elapsed_ms() + (now - game_last_tick()));
        update_timer_label();
        if (is_game_attack() && attack_used_ms() + game_elapsed_ms() >= ATK_MS) {
            atk_finish();
        }
    }
    game_set_last_tick(now);
}

/* (documented in ui_internal.h) */
void pause_cb(lv_event_t *e)
{
    set_paused(!is_game_paused());
}

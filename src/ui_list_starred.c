/**
 * @file ui_list_starred.c
 * @ingroup ui_list
 * @brief The Puzzles screen's Starred: the player's bookmarks in the right pane, newest first, instead
 *        of the collection's puzzles (the ⋮ menu's Starred puzzles / All puzzles toggles it).
 */
#include "ui_internal.h"
#include "continue_core.h"
#include "ui_list_part.h"
#include "puzzles_core.h"

/**
 * @brief A starred row: its puzzle, to play.
 * @param[in] e  the event (user data: the row)
 */
static void star_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= puzzles_starred_count()) {
        return;
    }
    open_puzzle(puzzles_starred_id(i));
}

/**
 * @brief A row's star: unstarred, the row goes.
 * @param[in] e  the event (user data: the row)
 */
static void star_row_off_cb(lv_event_t *e)
{
    uint16_t id;
    if (!puzzles_unstar((int)(intptr_t)lv_event_get_user_data(e), &id)) {
        return;
    }
    if (id == game_id()) {                               /* (the game's own star) */
        game_set_starred(false);
        update_header();
    }
    char name[48], msg[80];
    puzzle_name(id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "%s unstarred.", name);
    refresh_list();
    sudoku_ui_notice(msg, 2000);
}

/* (documented in ui_list_part.h) */
int list_fill_starred(void)
{
    list_rows_clear();
    int n = puzzles_load_starred();
    if (n <= 0) {
        list_rows_note(n < 0 ? "Stars need sudoku.db on a microSD card."
                       : "No starred puzzles yet: use the menu's Star on the game screen to keep "
                       "one here.");
    }
    for (int i = 0; i < puzzles_starred_count(); i++) {
        uint16_t id = puzzles_starred_id(i);
        uint32_t b = store_best_time(id);
        char sub[48], t[16];
        if (b) {
            fmt_time(t, sizeof(t), b);
            snprintf(sub, sizeof(sub), "Solved   -   best %s", t);
        } else {
            snprintf(sub, sizeof(sub), is_continue_in_progress(id) ? "In progress" : "Not solved yet");
        }
        list_add_row(i, id, sub, b ? C_GREEN : is_continue_in_progress(id) ? C_ORANGE : C_MUTED, star_row_cb,
                     SYMBOL_STAR, C_ORANGE, star_row_off_cb);
    }
    return n;
}

/**
 * @brief The ⋮ menu's Starred puzzles / All puzzles: the right pane shows the starred ones, or the
 *        collection again.
 * @param[in] e  the event
 */
void list_star_cb(lv_event_t *e)
{
    puzzles_set_starred(!is_puzzles_starred());
    refresh_list();
    if (!is_puzzles_starred()) {
        list_reveal_coll(LV_ANIM_OFF);
    }
}

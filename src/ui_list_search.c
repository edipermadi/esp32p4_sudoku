/**
 * @file ui_list_search.c
 * @ingroup ui_list
 * @brief The Puzzles screen's search: the magnifier next to the tabs opens a box to type in; the puzzles
 *        whose name, variant or level has every word fill the right pane, as Starred does (the data:
 *        puzzles_search.h). A tab or a collection tapped goes back to the collection.
 */
#include "ui_internal.h"
#include "continue_core.h"
#include "ui_list_part.h"
#include "puzzles_core.h"
#include "puzzles_search.h"

/**
 * @brief A result row: its puzzle, to play.
 * @param[in] e  the event (user data: the row)
 */
static void search_row_cb(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < puzzles_search_count()) {
        open_puzzle(puzzles_search_id(i));
    }
}

/**
 * @brief The search box's Search: the results, or (nothing typed) the collection again.
 * @param[in] text  what was typed
 */
static void search_done(const char *text)
{
    while (*text == ' ') {
        text++;
    }
    if (!*text) {
        puzzles_search_off();
    } else {
        puzzles_set_starred(false);
        puzzles_search_run(text);
    }
    refresh_list();
}

/* (documented in ui_list_part.h) */
void list_search_cb(lv_event_t *e)
{
    (void)e;
    nd_open_ex("Search puzzles", puzzles_search_query(), 32, "Search", search_done);
}

/* (documented in ui_list_part.h) */
void list_fill_search(void)
{
    list_rows_clear();
    if (!puzzles_search_count()) {
        list_rows_note("Nothing matches. Try a collection (Gnome 2), a variant (Killer, X-Sudoku), a level (Hard) "
                       "or a number (#14), or several words together.");
    }
    for (int i = 0; i < puzzles_search_count(); i++) {
        const uint16_t id = puzzles_search_id(i);
        int v = VAR_CLASSIC, lv = -1;
        if (puzzle_is_generated(id)) {
            gen_lookup(id, NULL, &v, &lv);
        }
        const bool solved = is_puzzles_search_solved(i), active = is_continue_in_progress(id);
        char sub[96];
        snprintf(sub, sizeof(sub), "%s%s%s   -   %s", v >= 0 && v < VAR_COUNT ? variant_name(v) : "",
                 lv >= 0 && lv < GEN_LEVELS ? " " : "", lv >= 0 && lv < GEN_LEVELS ? gen_level_name(lv) : "",
                 solved ? "Solved" : active ? "In progress" : "Not solved yet");
        list_add_row(i, id, sub, solved ? C_GREEN : active ? C_ORANGE : C_MUTED, search_row_cb, NULL, C_MUTED, NULL);
    }
}

/**
 * @file ui_list_manage.c
 * @ingroup ui_list
 * @brief The Puzzles screen's Saved and Imported tabs: Export and Delete of the collection shown; Delete of any
 *        collection for serial control (sudoku_ui_coll_delete()).
 */
#include "ui_internal.h"
#include "ui_list_part.h"
#include "puzzles_core.h"

static char s_del_name[STATS_IMPORT_NAME];      /**< the Delete dialog's collection: its name (for the notice) ... */
static int s_del_tab;                           /**< ... its tab (COLL_TAB_SAVED or COLL_TAB_IMPORTED) ... */
static int s_del_id;                            /**< ... and its id (coll_id()) */

/**
 * @brief The Delete dialog's Delete: the collection it was opened for deleted (by id: the one shown
 *        may have changed meanwhile, e.g. devctl's colldel or an import finishing), its tab back
 *        at its first if that was the one shown.
 * @param[in] e  the event (user data: the dialog)
 */
static void list_del_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    char msg[96], notice[112];
    bool ok = sudoku_ui_coll_delete(s_del_tab, s_del_id, msg, sizeof(msg));
    if (ok) {
        snprintf(notice, sizeof(notice), "%s.", msg);
    } else if (!strncmp(msg, "no ", 3)) {           /* (gone meanwhile: devctl's colldel) */
        snprintf(notice, sizeof(notice), "%s was deleted already.", s_del_name);
    } else {
        snprintf(notice, sizeof(notice), "Couldn't delete %s: %s.", s_del_name, msg);
    }
    sudoku_ui_notice(notice, 2500);
}

/* (documented in ui_internal.h) */
void list_coll_actions(bool *can_export, bool *can_delete)
{
    const int lc = puzzles_coll();
    *can_delete = !is_puzzles_starred() && puzzles_tab() != COLL_TAB_BUILTIN && lc >= PUZZLE_COLLECTION_COUNT &&
                  lc < coll_total() && coll_tab(lc) == puzzles_tab();   /* (not a tab with nothing in it) */
    *can_export = *can_delete && coll_saved_index(lc) >= 0;
}

/* (documented in ui_internal.h) */
void list_del_cb(lv_event_t *e)
{
    bool can_export, can_delete;
    list_coll_actions(&can_export, &can_delete);
    int lc = puzzles_coll();
    if (!can_delete) {
        return;
    }
    bool imported = coll_saved_index(lc) < 0;
    int n = coll_size(lc);
    snprintf(s_del_name, sizeof(s_del_name), "%s", coll_name(lc));   /* (what the dialog is about) */
    s_del_tab = imported ? COLL_TAB_IMPORTED : COLL_TAB_SAVED;
    s_del_id = coll_id(lc);
    char title[64], text[256];
    snprintf(title, sizeof(title), "Delete %s?", coll_name(lc));
    char what[48];
    if (n == 1) {
        snprintf(what, sizeof(what), "Its puzzle stays on the board: its");
    } else {
        snprintf(what, sizeof(what), "Its %d puzzles stay on the board: their", n);
    }
    snprintf(text, sizeof(text), "The collection goes%s. %s solves, History and Gameplays are kept%s.",
             imported ? " (its file can be imported again)" : "", what,
             imported ? "" : n == 1 ? ", and Save can put it in a collection again"
             : ", and Save can put them in a collection again");
    lv_obj_t *m = make_msgbox(title, text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_TRASH "  Delete", list_del_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

/* (documented in ui_sudoku.h) */
bool sudoku_ui_coll_delete(int tab, int id, char *msg, size_t n)
{
    if (!is_stats_db_ready()) {
        snprintf(msg, n, "no sudoku.db");
        return false;
    }
    const int c = tab == COLL_TAB_SAVED || tab == COLL_TAB_IMPORTED ? coll_find_id(tab, id) : -1;
    if (c < 0) {
        snprintf(msg, n, "no %s collection %d", tab == COLL_TAB_SAVED ? "saved" : "imported", id);
        return false;
    }
    /* the collection shown: kept, by name (its number moves when one before it goes) */
    const int shown = puzzles_coll();
    char shown_name[STATS_IMPORT_NAME] = "";
    bool shown_saved = false;
    if (shown >= PUZZLE_COLLECTION_COUNT && shown < coll_total() && shown != c) {
        snprintf(shown_name, sizeof(shown_name), "%s", coll_name(shown));
        shown_saved = coll_saved_index(shown) >= 0;
    }
    char gone[STATS_IMPORT_NAME];
    bool ok = puzzles_delete_coll(c, gone, sizeof(gone));
    imports_reload();
    list_fill_colls();
    puzzles_forget_saved();                         /* (the tab's first, unless ...) */
    if (shown < PUZZLE_COLLECTION_COUNT) {
        puzzles_set_coll(shown);                    /* (a built-in one stays as it was) */
    }
    for (int k = PUZZLE_COLLECTION_COUNT; shown_name[0] && k < coll_total(); k++) {
        if ((coll_saved_index(k) >= 0) == shown_saved && !strcmp(coll_name(k), shown_name)) {
            puzzles_set_coll(k);
            break;
        }
    }
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    update_header();                                /* (the game screen's Save icon) */
    snprintf(msg, n, ok ? "%s deleted" : "couldn't delete %s", gone);
    return ok;
}

/* (documented in ui_internal.h) */
void list_export_cb(lv_event_t *e)
{
    bool can_export, can_delete;
    list_coll_actions(&can_export, &can_delete);
    if (!can_export) {
        return;
    }
    char msg[96];
    if (puzzles_export(puzzles_coll(), msg, sizeof(msg))) {
        sudoku_ui_notice(msg, 4000);
    }
}

/**
 * @file ui_list_manage.c
 * @ingroup ui_list
 * @brief The Puzzles screen's Saved tab: Export and Delete of the collection shown.
 */
#include "ui_internal.h"
#include "ui_list_part.h"
#include "puzzles_core.h"

/**
 * @brief The Delete dialog's Delete: the collection deleted, the Saved tab back at its first.
 * @param e  the event (user data: the dialog)
 */
static void list_del_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    int lc = puzzles_coll();
    if (lc < PUZZLE_COLLECTION_COUNT || lc >= coll_total()) {
        return;
    }
    char name[STATS_IMPORT_NAME], msg[96];
    bool ok = puzzles_delete_coll(lc, name, sizeof(name));
    snprintf(msg, sizeof(msg), ok ? "%s deleted." : "Couldn't delete %s.", name);
    imports_reload();
    list_fill_colls();
    puzzles_forget_saved();
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    update_header();                                /* (the game screen's Save icon) */
    sudoku_ui_notice(msg, 2500);
}

/* (documented in ui_list_part.h) */
void list_del_cb(lv_event_t *e)
{
    int lc = puzzles_coll();
    if (lc < PUZZLE_COLLECTION_COUNT || lc >= coll_total()) {
        return;
    }
    bool imported = coll_saved_index(lc) < 0;
    int n = coll_size(lc);
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

/* (documented in ui_list_part.h) */
void list_export_cb(lv_event_t *e)
{
    char msg[96];
    if (puzzles_export(puzzles_coll(), msg, sizeof(msg))) {
        sudoku_ui_notice(msg, 4000);
    }
}

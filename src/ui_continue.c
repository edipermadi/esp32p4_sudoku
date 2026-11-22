/**
 * @file ui_continue.c
 * @ingroup ui_continue
 * @brief Home's Continue button: its line, its tap, and a game resumed.
 */
#include "ui_internal.h"
#include "ui_continue_part.h"
#include "continue_core.h"

/* (documented in ui_continue_part.h) */
bool continue_resume(uint16_t id)
{
    if (!continue_load(id)) {
        return false;
    }
    start_game(id, true);
    return true;
}

/* (documented in ui_internal.h) */
void continue_line_update(lv_obj_t *btn, lv_obj_t *sub)
{
    continue_refresh();
    uint16_t last;
    uint32_t ms;
    int others;
    /* Continue stays in its place: greyed out (like Daily puzzle without the clock) when
     * there is nothing to continue, so the menu doesn't shift around */
    const bool any = continue_last(&last, &ms, &others);
    lv_obj_set_state(btn, LV_STATE_DISABLED, !any);
    lv_obj_set_style_bg_color(btn, any ? C_LIST_ACTIVE : C_CARD, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), any ? lv_color_white() : C_TEXT, 0);
    lv_obj_set_style_text_color(sub, any ? lv_color_white() : C_MUTED, 0);
    if (!any) {
        lv_label_set_text(sub, "No game in progress");
        return;
    }
    char name[40], t[16];
    puzzle_name(last, name, sizeof(name));
    fmt_time(t, sizeof(t), ms / 1000);
    if (others) {
        lv_label_set_text_fmt(sub, "%s  -  %s   (+%d more)", name, t, others);
    } else {
        lv_label_set_text_fmt(sub, "%s  -  %s", name, t);
    }
}

/* (documented in ui_internal.h) */
void continue_cb(lv_event_t *e)
{
    continue_refresh();
    if (continue_ngames() > 1) {                    /* pick one */
        continue_games_open();
        return;
    }
    uint16_t id;
    uint32_t ms;
    int others;
    if (continue_last(&id, &ms, &others) && !continue_resume(id)) {
        sudoku_ui_notice("That game isn't in sudoku.db any more.", 3000);
    }
}

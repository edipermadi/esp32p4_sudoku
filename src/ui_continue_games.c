/**
 * @file ui_continue_games.c
 * @ingroup ui_continue
 * @brief The Unfinished games dialog (Continue with more than one): each game, to resume or discard.
 */
#include "ui_internal.h"
#include "ui_continue_part.h"
#include "continue_core.h"

static lv_obj_t *ug_box;                /**< the Unfinished games dialog */

/**
 * @brief Closes it.
 */
static void ug_close(void)
{
    if (ug_box) {
        lv_msgbox_close_async(ug_box);
    }
    ug_box = NULL;
}

/**
 * @brief Its Close.
 * @param[in] e  the event
 */
static void ug_close_cb(lv_event_t *e)
{
    ug_close();
}

/**
 * @brief An unfinished game tapped: resumed.
 * @param[in] e  the event (user data: the puzzle)
 */
static void ug_resume_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    ug_close();
    if (!continue_resume(id)) {
        sudoku_ui_notice("That game isn't in sudoku.db any more.", 3000);
    }
}

/**
 * @brief An unfinished game's Discard: gone, and the dialog again with the rest (if there are more than one).
 * @param[in] e  the event (user data: the puzzle)
 */
static void ug_discard_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    continue_discard(id);
    if (game_id() == id && !is_game_solved()) {
        game_set_id(STORE_NO_GAME);                 /* the game screen held it */
    }
    char name[40], msg[80];
    puzzle_name(id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "Discarded %s.", name);
    sudoku_ui_notice(msg, 2500);
    ug_close();
    refresh_home();                                 /* (Continue's line; it reads the list again) */
    if (continue_ngames() > 1) {
        continue_games_open();                      /* the rest */
    }
}

/**
 * @brief A game's row: its name, its time, how much is filled and when it was played last, and Discard.
 * @param[in] parent  the dialog's content
 * @param[in] r  the game
 */
static void ug_row(lv_obj_t *parent, const stats_game_row_t *r)
{
    lv_obj_t *row = make_btn(parent, 0, 0, 0, 76, NULL, NULL, ug_resume_cb, (void *)(uintptr_t)r->puzzle);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_bg_color(row, C_CARD2, 0);
    lv_obj_set_style_margin_bottom(row, 8, 0);
    char name[40], t[16], when[32] = "", line[96];
    puzzle_name(r->puzzle, name, sizeof(name));
    fmt_time(t, sizeof(t), r->elapsed_ms / 1000);
    if (r->saved_at) {
        time_t tt = (time_t)r->saved_at;
        struct tm tm;
        localtime_r(&tt, &tm);
        strftime(when, sizeof(when), "   -   %a %d %b, %H:%M", &tm);
    }
    snprintf(line, sizeof(line), "%s   -   %d / %d filled%s", t, r->filled, continue_cells(r->puzzle), when);
    lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text(n, name);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 14, -13);
    lv_obj_t *sub = make_label(row, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(sub, line);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 14, 14);
    lv_obj_t *del = make_btn(row, 0, 0, 56, 56, LV_SYMBOL_TRASH, &lv_font_montserrat_20, ug_discard_cb,
                             (void *)(uintptr_t)r->puzzle);
    lv_obj_set_style_bg_color(del, C_CARD, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(del, 0), C_RED, 0);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, -10, 0);
}

/* (documented in ui_continue_part.h) */
void continue_games_open(void)
{
    ug_close();
    lv_obj_t *m = make_msgbox("Unfinished games", NULL);
    ug_box = m;
    lv_obj_set_width(m, 640);
    for (int i = 0; i < continue_ngames(); i++) {
        ug_row(lv_msgbox_get_content(m), &continue_games()[i]);
    }
    msgbox_button(m, "Close", ug_close_cb, false);
}

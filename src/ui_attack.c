/**
 * @file ui_attack.c
 * @ingroup ui_game
 * @brief Time attack: as many Easy puzzles as possible in 10 minutes, its leaderboard and the run's end.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void atk_start(void);
static void atk_board_fill(lv_obj_t *c);
static void atk_go_cb(lv_event_t *e);
static void atk_home_cb(lv_event_t *e);

static uint32_t atk_used_ms;      /**< the run's earlier puzzles */
static int      atk_solved;       /**< puzzles solved in the run */
static int      atk_mistakes;     /**< the run's mistakes */
static uint16_t atk_last;         /**< the run's last puzzle (the next one is another) */

/* ------------------------------------------------------------- time attack */

/* (documented in ui_internal.h) */
void atk_open_next(void)
{
    if (!is_game_attack()) {
        return;    /* stopped meanwhile */
    }
    const puzzle_collection_t *pc = puzzle_collection(0);   /* Easy */
    uint16_t id;
    int tries = 0;
    do {
        id = (uint16_t)(pc->first + esp_random() % pc->count);
    } while (++tries < 20 && (id == atk_last || id == store_saved_game()->id));   /* (not the Continue game) */
    atk_last = id;
    uint32_t used = atk_used_ms;
    start_game(id, false);
    game_set_attack(true);
    atk_used_ms = used;
    game_set_flags(0);                                    /* (no challenges in a run) */
    challenge_ui();
    update_header();
}

/**
 * @brief Starts a time attack run: the clock, the counts, the first puzzle.
 */
static void atk_start(void)
{
    atk_used_ms = 0;
    atk_solved = atk_mistakes = 0;
    atk_last = STORE_NO_GAME;
    game_set_attack(true);
    atk_open_next();
}

/**
 * @brief The time attack leaderboard into a card.
 * @param[in] c  the card
 */
static void atk_board_fill(lv_obj_t *c)
{
    stats_attack_row_t rows[STATS_ATTACK_ROWS];
    int n = stats_db_attack_board(rows, STATS_ATTACK_ROWS);
    if (n <= 0) {
        make_wrap_text(c, &lv_font_montserrat_18, C_MUTED, n < 0 ? "The leaderboard needs sudoku.db on a microSD card."
                       : "No runs yet: be the first on the leaderboard.");
        return;
    }
    lv_obj_t *hd = make_label(c, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(hd, "Leaderboard: best run");
    for (int i = 0; i < n; i++) {
        lv_obj_t *r = lv_obj_create(c);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), 36);
        lv_color_t col = rows[i].me ? C_ACCENT : C_TEXT;
        lv_obj_t *l = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text_fmt(l, "%d.", i + 1);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(player_badge(r, player_find(0, rows[i].name), -1, 28, 10), LV_ALIGN_LEFT_MID, 28, 0);
        lv_obj_t *nm = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text(nm, rows[i].name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 64, 0);
        lv_obj_t *v = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text_fmt(v, "%d solved   (%d run%s)", rows[i].best, rows[i].runs, rows[i].runs == 1 ? "" : "s");
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

/**
 * @brief Start (the time attack's dialog).
 * @param[in] e  the event (user data: the dialog)
 */
static void atk_go_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    atk_start();
}

/**
 * @brief Back to Home (the time attack's dialog).
 * @param[in] e  the event (user data: the dialog)
 */
static void atk_home_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    show_home();
}

/* (documented in ui_internal.h) */
void attack_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Time attack", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_t *x = make_wrap_text(c, &lv_font_montserrat_18, C_TEXT, "Solve as many Easy puzzles as you can in 10 "
                                 "minutes: the next one opens as soon as one is solved. Hint and Fill are off; pausing "
                                 "stops the clock and hides the board. Solves count as usual.");
    lv_obj_set_style_margin_bottom(x, 10, 0);
    atk_board_fill(c);
    msgbox_button(m, "Close", mbox_close_cb, false);
    msgbox_button(m, "Start", atk_go_cb, true);
}

/* (documented in ui_internal.h) */
void atk_finish(void)
{
    game_set_solved(true);                                /* (g_attack stays: the header and 0:00 stay too) */                                /* (the board can't be changed now) */
    hint_drop();
    stats_db_record_attack(atk_solved, atk_mistakes);
    sfx_play(SFX_SOLVED);
    char t[96];
    snprintf(t, sizeof(t), "Time's up! You solved %d Easy puzzle%s, with %d mistake%s.", atk_solved,
             atk_solved == 1 ? "" : "s", atk_mistakes, atk_mistakes == 1 ? "" : "s");
    lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Time attack", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_t *x = make_wrap_text(c, &lv_font_montserrat_20, C_TEXT, t);
    lv_obj_set_style_margin_bottom(x, 10, 0);
    atk_board_fill(c);                              /* (the run above is queued first: it's in) */
    msgbox_button(m, "Home", atk_home_cb, false);
    msgbox_button(m, "Again", atk_go_cb, true);
    update_header();
}

/* ---- accessors: the other UI files reach this file's state through these */
uint32_t attack_used_ms(void) { return atk_used_ms; }
void attack_set_used_ms(uint32_t v) { atk_used_ms = v; }
int attack_solved(void) { return atk_solved; }
void attack_set_solved(int v) { atk_solved = v; }
int attack_mistakes(void) { return atk_mistakes; }
void attack_set_mistakes(int v) { atk_mistakes = v; }

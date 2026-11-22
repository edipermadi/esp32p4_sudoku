/**
 * @file ui_trophies_daily.c
 * @ingroup ui_trophies
 * @brief Trophies, the Daily tab: a daily puzzle's leaderboard, a day at a time (< day >), and Play today's puzzle.
 */
#include "ui_internal.h"
#include "ui_trophies_tab.h"
#include "trophies_core.h"

static lv_obj_t *tr_day_lbl;               /**< the day shown, and its puzzle */
static lv_obj_t *tr_day_prev;              /**< < the day before */
static lv_obj_t *tr_day_next;              /**< > the day after (not past today) */
static lv_obj_t *tr_board_list;            /**< the leaderboard's cards */
static lv_obj_t *tr_play_btn;              /**< Play today's puzzle (today, not solved by you yet) */

/**
 * @brief Today's date, as the daily puzzle has it.
 * @param[out] day  gets it, 'YYYY-MM-DD'
 * @return false while the clock isn't set
 */
static bool tr_today(char day[12])
{
    int level;
    return daily_today(day, &level, NULL);
}

/**
 * @brief \< \>: the day before or after, and its leaderboard.
 * @param[in] e  the event (user data: -1 or 1)
 */
static void tr_day_cb(lv_event_t *e)
{
    char today[12];
    if (!tr_today(today)) {
        return;
    }
    trophies_step_day(today, (int)(intptr_t)lv_event_get_user_data(e));
}

/* (documented in ui_trophies_tab.h) */
void tr_daily_render(const stats_board_t *b)
{
    char today[12] = "";
    bool clock = tr_today(today);
    bool is_today = clock && !strcmp(b->day, today);
    struct tm tm = {0};
    char date[32] = "";
    if (sscanf(b->day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_hour = 12;
        mktime(&tm);                                     /* fills in the weekday */
        strftime(date, sizeof(date), "%a %d %b %Y", &tm);
    }
    if (b->exists && b->level >= 0 && b->level < GEN_LEVELS) {
        char kind[32];
        daily_kind_text(b->variant, b->level, kind, sizeof(kind));
        lv_label_set_text_fmt(tr_day_lbl, "%s%s  -  %s", is_today ? "Today, " : "", date, kind);
    } else {
        lv_label_set_text_fmt(tr_day_lbl, "%s%s", is_today ? "Today, " : "", date);
    }
    lv_obj_set_state(tr_day_next, LV_STATE_DISABLED, is_today);
    bool me_solved = false;
    for (int i = 0; i < b->n; i++) {
        me_solved |= b->rows[i].player_id == store_player_id() && b->rows[i].solved;
    }
    lv_obj_set_hidden(tr_play_btn, !is_today || me_solved);

    if (!b->exists || b->solved == 0) {
        tr_hint(tr_board_list, !b->exists ? (is_today ? "Nobody has played today's puzzle yet. Be the first!"
                                             : "Nobody played the daily puzzle on this day.")
                : (is_today ? "Nobody has solved today's puzzle yet. Be the first!"
                   : "Nobody solved this day's puzzle."));
        return;
    }
    lv_obj_clean(tr_board_list);
    int rank = 0;
    for (int i = 0; i < b->n; i++) {
        const stats_board_row_t *r = &b->rows[i];
        bool me = r->player_id == store_player_id();
        lv_obj_t *card = tr_card(tr_board_list, r->solved ? 76 : 56);
        if (me) {
            lv_obj_set_style_border_color(card, C_ACCENT, 0);
            lv_obj_set_style_border_width(card, 2, 0);
        }
        if (!r->solved) {
            lv_obj_align(player_badge(card, player_find(r->player_id, r->player), r->color, 32, 12),
                         LV_ALIGN_LEFT_MID, 50, 0);
            lv_obj_t *n = make_label(card, &lv_font_montserrat_18, C_MUTED);
            lv_label_set_text_fmt(n, "%s   -   not solved yet", r->player);
            lv_obj_align(n, LV_ALIGN_LEFT_MID, 96, 0);
            continue;
        }
        rank++;
        lv_obj_t *pos = make_label(card, rank == 1 ? &font_icons_24 : &lv_font_montserrat_24,
                                   rank == 1 ? C_ORANGE : C_MUTED);
        if (rank == 1) {
            lv_label_set_text(pos, SYMBOL_STAR);
        } else {
            lv_label_set_text_fmt(pos, "%d", rank);
        }
        lv_obj_set_width(pos, 40);
        lv_obj_set_style_text_align(pos, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(pos, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(player_badge(card, player_find(r->player_id, r->player), r->color, AVATAR_SIZE, 12),
                     LV_ALIGN_LEFT_MID, 48, 0);         /* their avatar (or colour dot) */
        lv_obj_t *n = make_label(card, &lv_font_montserrat_20, C_TEXT);
        lv_label_set_text_fmt(n, "%s%s", r->player, me ? "  (you)" : "");
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 96, -13);
        char line[96];
        snprintf(line, sizeof(line), "%d%s   -   %u mistake%s   -   %u hint%s", rank, ordinal(rank),
                 (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s", (unsigned)r->hints, r->hints == 1 ? "" : "s");
        if (r->flags & GAME_FLAG_NO_NOTES) {        /* the no-notes challenge: its badge */
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   no notes");
        }
        if (r->solves > 1) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   solved %u times", (unsigned)r->solves);
        }
        lv_obj_t *sub = make_label(card, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(sub, line);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 96, 14);
        char t[16];
        fmt_time(t, sizeof(t), r->seconds);
        lv_obj_t *tl = make_label(card, &font_timer_36, rank == 1 ? C_ORANGE : C_TEXT);
        lv_label_set_text(tl, t);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_scroll_to_y(tr_board_list, 0, LV_ANIM_OFF);
}

/* (documented in ui_trophies_tab.h) */
lv_obj_t *tr_daily_build(lv_obj_t *scr)
{
    lv_obj_t *bv = lv_obj_create(scr);
    lv_obj_remove_style_all(bv);
    lv_obj_set_pos(bv, 12, TR_VIEW_Y);
    lv_obj_set_size(bv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_day_prev = make_btn(bv, 0, 0, 64, 52, LV_SYMBOL_LEFT, &lv_font_montserrat_22, tr_day_cb, (void *)(intptr_t)-1);
    tr_day_next = make_btn(bv, SCR_W - 24 - 64, 0, 64, 52, LV_SYMBOL_RIGHT, &lv_font_montserrat_22, tr_day_cb,
                           (void *)(intptr_t)1);
    lv_obj_set_style_text_opa(lv_obj_get_child(tr_day_next, 0), LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(lv_obj_get_child(tr_day_prev, 0), LV_OPA_30, LV_STATE_DISABLED);
    tr_day_lbl = make_label(bv, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(tr_day_lbl, SCR_W - 24 - 2 * 72);
    lv_obj_set_style_text_align(tr_day_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tr_day_lbl, LV_ALIGN_TOP_MID, 0, 14);
    tr_board_list = lv_obj_create(bv);
    lv_obj_remove_style_all(tr_board_list);
    lv_obj_set_pos(tr_board_list, 0, 64);
    lv_obj_set_size(tr_board_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12 - 64 - 64);
    lv_obj_set_flex_flow(tr_board_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_board_list, 8, 0);
    lv_obj_set_scroll_dir(tr_board_list, LV_DIR_VER);
    tr_play_btn = make_btn(bv, 0, 0, 300, 52, LV_SYMBOL_BELL "  Play today's puzzle", &lv_font_montserrat_18,
                           daily_cb, NULL);
    lv_obj_set_style_bg_color(tr_play_btn, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(tr_play_btn, 0), lv_color_white(), 0);
    lv_obj_align(tr_play_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_hidden(tr_play_btn, true);
    tr_hint(tr_board_list, "Loading...");
    return bv;
}

/* (documented in ui_trophies_tab.h) */
void tr_daily_ask(void)
{
    char today[12];
    if (!tr_today(today)) {
        lv_label_set_text(tr_day_lbl, "");
        lv_obj_set_state(tr_day_next, LV_STATE_DISABLED, true);
        lv_obj_set_state(tr_day_prev, LV_STATE_DISABLED, true);
        lv_obj_set_hidden(tr_play_btn, true);
        tr_hint(tr_board_list, "The daily leaderboard needs today's date: connect Wi-Fi (Settings) so the "
                "clock is set.");
        return;
    }
    lv_obj_set_state(tr_day_prev, LV_STATE_DISABLED, false);
    trophies_ask_board(today);
}

/* (documented in ui_trophies_tab.h) */
void tr_daily_no_db(void)
{
    tr_hint(tr_board_list, "The leaderboard needs sudoku.db on a microSD card.");
    lv_label_set_text(tr_day_lbl, "");
    lv_obj_set_hidden(tr_play_btn, true);
}

/**
 * @file ui_trophies_weekly.c
 * @ingroup ui_trophies
 * @brief Trophies, the Weekly tab: a week's daily puzzles added up per player (< week >).
 */
#include "ui_internal.h"
#include "ui_trophies_tab.h"
#include "trophies_core.h"

static lv_obj_t *tr_week_lbl;              /**< the week shown */
static lv_obj_t *tr_week_prev;             /**< < the week before */
static lv_obj_t *tr_week_next;             /**< > the week after (not past this week) */
static lv_obj_t *tr_week_list;             /**< the players' cards */

/**
 * @brief \< \>: the week before or after.
 * @param e  the event (user data: -1 or 1)
 */
static void tr_week_cb(lv_event_t *e)
{
    trophies_step_week((int)(intptr_t)lv_event_get_user_data(e));
}

/* (documented in ui_trophies_tab.h) */
void tr_weekly_render(const stats_week_t *w)
{
    char now[12] = "", date[24] = "";
    bool clock = trophies_this_monday(now);
    struct tm tm = {0};
    if (sscanf(w->monday, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_hour = 12;
        mktime(&tm);
        strftime(date, sizeof(date), "%d %b %Y", &tm);
    }
    bool current = clock && !strcmp(w->monday, now);
    lv_label_set_text_fmt(tr_week_lbl, "%s %s", current ? "This week, from" : "Week of", date);
    lv_obj_set_state(tr_week_next, LV_STATE_DISABLED, current);
    lv_obj_clean(tr_week_list);
    int shown = 0, rank = 0;
    for (int i = 0; i < w->n; i++) {
        const stats_week_row_t *r = &w->rows[i];
        if (!r->days) {
            continue;
        }
        shown++;
        rank++;
        bool me = r->player_id == store_player_id();
        lv_obj_t *card = tr_card(tr_week_list, 84);
        if (me) {
            lv_obj_set_style_border_color(card, C_ACCENT, 0);
            lv_obj_set_style_border_width(card, 2, 0);
        }
        lv_obj_t *pos = make_label(card, rank == 1 ? &font_icons_24 : &lv_font_montserrat_24, rank == 1 ? C_ORANGE : C_MUTED);
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
        lv_label_set_text_fmt(n, "%s%s   -   %d of 7 days", r->player, me ? "  (you)" : "", r->days);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 96, -16);
        static const char *const DAYS[7] = {"M", "T", "W", "T", "F", "S", "S"};
        for (int d = 0; d < 7; d++) {               /* which days: a square each, green if solved */
            lv_obj_t *sq = lv_obj_create(card);
            lv_obj_remove_style_all(sq);
            lv_obj_set_size(sq, 26, 22);
            lv_obj_set_style_radius(sq, 5, 0);
            lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(sq, r->best[d] ? C_GREEN : C_CARD2, 0);
            lv_obj_align(sq, LV_ALIGN_LEFT_MID, 96 + d * 30, 16);
            lv_obj_t *dl = make_label(sq, &lv_font_montserrat_12, r->best[d] ? lv_color_white() : C_MUTED);
            lv_label_set_text(dl, DAYS[d]);
            lv_obj_center(dl);
        }
        char t[16];
        fmt_time(t, sizeof(t), r->total);
        lv_obj_t *tl = make_label(card, &font_timer_36, rank == 1 ? C_ORANGE : C_TEXT);
        lv_label_set_text(tl, t);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    if (!shown) {
        tr_hint(tr_week_list, current ? "No daily puzzle solved this week yet." : "No daily puzzle was solved that week.");
    }
    lv_obj_scroll_to_y(tr_week_list, 0, LV_ANIM_OFF);
}

/* (documented in ui_trophies_tab.h) */
lv_obj_t *tr_weekly_build(lv_obj_t *scr)
{
    lv_obj_t *wv = lv_obj_create(scr);
    lv_obj_remove_style_all(wv);
    lv_obj_set_pos(wv, 12, TR_VIEW_Y);
    lv_obj_set_size(wv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_week_prev = make_btn(wv, 0, 0, 64, 52, LV_SYMBOL_LEFT, &lv_font_montserrat_22, tr_week_cb, (void *)(intptr_t)-1);
    tr_week_next = make_btn(wv, SCR_W - 24 - 64, 0, 64, 52, LV_SYMBOL_RIGHT, &lv_font_montserrat_22, tr_week_cb,
                            (void *)(intptr_t)1);
    lv_obj_set_style_text_opa(lv_obj_get_child(tr_week_next, 0), LV_OPA_30, LV_STATE_DISABLED);
    tr_week_lbl = make_label(wv, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(tr_week_lbl, SCR_W - 24 - 2 * 72);
    lv_obj_set_style_text_align(tr_week_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tr_week_lbl, LV_ALIGN_TOP_MID, 0, 14);
    tr_week_list = lv_obj_create(wv);
    lv_obj_remove_style_all(tr_week_list);
    lv_obj_set_pos(tr_week_list, 0, 64);
    lv_obj_set_size(tr_week_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12 - 64);
    lv_obj_set_flex_flow(tr_week_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_week_list, 8, 0);
    lv_obj_set_scroll_dir(tr_week_list, LV_DIR_VER);
    tr_hint(tr_week_list, "Loading...");
    return wv;
}

/* (documented in ui_trophies_tab.h) */
void tr_weekly_ask(void)
{
    if (!trophies_ask_week()) {
        lv_label_set_text(tr_week_lbl, "");
        tr_hint(tr_week_list, "The weekly challenge needs today's date: connect Wi-Fi (Settings) so the clock "
                "is set.");
    }
}

/* (documented in ui_trophies_tab.h) */
void tr_weekly_no_db(void)
{
    tr_hint(tr_week_list, "The weekly challenge needs sudoku.db on a microSD card.");
}

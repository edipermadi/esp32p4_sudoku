/* ui_sudoku.c split: Trophies: achievements, leaderboards, records. See ui_sudoku.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void tr_hint(lv_obj_t *list, const char *text);
static lv_obj_t *tr_card(lv_obj_t *list, int h);
static void ach_progress_text(const stats_ach_t *a, char *buf, size_t n);
static void ach_card(const stats_ach_t *a);
static void render_achievements(const stats_achievements_t *a);
static void day_shift(char *day, int delta);
static bool tr_today(char day[12]);
static void render_board(const stats_board_t *b);
static bool this_monday(char day[12]);
static void render_week(const stats_week_t *w);
static void tr_week_cb(lv_event_t *e);
static void render_records(const stats_records_t *R);
static void tr_select_tab(int tab);
static void tr_tab_cb(lv_event_t *e);
static void tr_day_cb(lv_event_t *e);
static void trophy_toast_cb(lv_timer_t *t);

static lv_obj_t *scr_trophy;              /* Trophies: achievements, daily leaderboard */
static lv_obj_t *tr_tab[TR_TABS], *tr_view[TR_TABS];
static lv_obj_t *tr_ach_count, *tr_ach_list, *tr_rec_list;
static lv_obj_t *tr_week_lbl, *tr_week_prev, *tr_week_next, *tr_week_list;
static char tr_monday[12];                /* week on the Weekly tab, "" = this week */
static lv_obj_t *tr_day_lbl, *tr_day_prev, *tr_day_next, *tr_board_list, *tr_play_btn;
static int tr_tab_sel;
static char tr_day[12];                   /* day on the leaderboard, "" = today */
static lv_obj_t *h_trophy_sub;            /* home button line: "7 of 22 achievements" */

static void tr_hint(lv_obj_t *list, const char *text)
{
    lv_obj_clean(list);
    lv_obj_t *l = make_label(list, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_top(l, 20, 0);
    lv_label_set_text(l, text);
}

static lv_obj_t *tr_card(lv_obj_t *list, int h)
{
    lv_obj_t *card = lv_obj_create(list);
    lv_obj_set_size(card, LV_PCT(100), h);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_hor(card, 16, 0);
    lv_obj_set_style_pad_ver(card, 0, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, false);
    return card;
}

/* "3 / 10", "2 h 5 min / 10 h", "4 / 7 days" */
static void ach_progress_text(const stats_ach_t *a, char *buf, size_t n)
{
    switch (a->unit) {
    case ACH_UNIT_MINUTES:
        if (a->value >= 60)
            snprintf(buf, n, "%ld h %ld min / %ld h", (long)(a->value / 60), (long)(a->value % 60),
                     (long)(a->goal / 60));
        else {
            snprintf(buf, n, "%ld min / %ld h", (long)a->value, (long)(a->goal / 60));
        }
        break;
    case ACH_UNIT_DAYS:
        snprintf(buf, n, "%ld / %ld days", (long)a->value, (long)a->goal);
        break;
    default:
        snprintf(buf, n, "%ld / %ld", (long)a->value, (long)a->goal);
        break;
    }
}

static void ach_card(const stats_ach_t *a)
{
    lv_obj_t *card = tr_card(tr_ach_list, 80);
    lv_obj_t *icon = make_label(card, &font_icons_24, a->unlocked ? C_ORANGE : C_MUTED);
    lv_label_set_text(icon, a->unlocked ? SYMBOL_TROPHY : SYMBOL_LOCK);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_20, a->unlocked ? C_TEXT : C_MUTED);
    lv_label_set_text(t, a->title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 44, -13);
    lv_obj_t *d = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(d, SCR_W - 24 - 32 - 44 - 190);
    lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(d, a->desc);
    lv_obj_align(d, LV_ALIGN_LEFT_MID, 44, 14);

    lv_obj_t *r = make_label(card, &lv_font_montserrat_16, a->unlocked ? C_GREEN : C_MUTED);
    lv_obj_set_width(r, 176);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_RIGHT, 0);
    if (a->unlocked) {
        char when[24] = "Unlocked";
        if (a->unlocked_at) {
            time_t tt = (time_t)a->unlocked_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%d %b %Y", &tm);
        }
        lv_label_set_text(r, when);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (a->goal > 1) {
        char prog[32];
        ach_progress_text(a, prog, sizeof(prog));
        lv_label_set_text(r, prog);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, -12);
        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_size(bar, 150, 10);
        lv_bar_set_range(bar, 0, a->goal);
        lv_bar_set_value(bar, a->value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, C_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, C_ACCENT, LV_PART_INDICATOR);
        lv_obj_align(bar, LV_ALIGN_RIGHT_MID, 0, 14);
    } else {
        lv_label_set_text(r, "Not yet");
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void render_achievements(const stats_achievements_t *a)
{
    lv_label_set_text_fmt(tr_ach_count, "%d of %d unlocked", a->unlocked, a->n);
    lv_obj_clean(tr_ach_list);
    for (int pass = 0; pass < 2; pass++) {             /* unlocked first, then the rest */
        for (int i = 0; i < a->n; i++) {
            if (a->a[i].unlocked == (pass == 0)) {
                ach_card(&a->a[i]);
            }
        }
    }
    lv_obj_scroll_to_y(tr_ach_list, 0, LV_ANIM_OFF);
}

/* day moved by delta days ('YYYY-MM-DD', in place) */
static void day_shift(char *day, int delta)
{
    struct tm tm = {0};
    if (sscanf(day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
        return;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_mday += delta;
    tm.tm_hour = 12;                                    /* clear of DST changes */
    mktime(&tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
}

static bool tr_today(char day[12])
{
    int level;
    return daily_today(day, &level, NULL);
}

static void render_board(const stats_board_t *b)
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

/* Monday of this week ('YYYY-MM-DD'); false while the clock isn't set */
static bool this_monday(char day[12])
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) {
        return false;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_mday -= (tm.tm_wday + 6) % 7;
    tm.tm_hour = 12;
    mktime(&tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    return true;
}

static void render_week(const stats_week_t *w)
{
    char now[12] = "", date[24] = "";
    bool clock = this_monday(now);
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

static void tr_week_cb(lv_event_t *e)
{
    char now[12];
    if (!this_monday(now)) {
        return;
    }
    if (!tr_monday[0]) {
        snprintf(tr_monday, sizeof(tr_monday), "%s", now);
    }
    day_shift(tr_monday, 7 * (int)(intptr_t)lv_event_get_user_data(e));
    if (strcmp(tr_monday, now) > 0) {
        snprintf(tr_monday, sizeof(tr_monday), "%s", now);
    }
    stats_db_query_week(tr_monday);
}

static void render_records(const stats_records_t *R)
{
    lv_obj_clean(tr_rec_list);
    for (int i = 0; i < R->n; i++) {
        const stats_record_t *r = &R->r[i];
        lv_obj_t *card = tr_card(tr_rec_list, 72);
        lv_obj_t *t = make_label(card, &lv_font_montserrat_18, r->has ? C_TEXT : C_MUTED);
        lv_label_set_text(t, r->title);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, -12);
        /* what it was set on, and when */
        char detail[96] = "", when[24] = "";
        if (r->when) {
            time_t tt = (time_t)r->when;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%d %b %Y", &tm);
        }
        if (!r->has) {
            snprintf(detail, sizeof(detail), "Not yet");
        } else if (r->puzzle != STORE_NO_GAME) {
            char name[40];
            puzzle_name(r->puzzle, name, sizeof(name));
            snprintf(detail, sizeof(detail), "%s%s%s", name, when[0] ? "   -   " : "", when);
        } else if (when[0]) {
            snprintf(detail, sizeof(detail), "%s", when);
        }
        lv_obj_t *d = make_label(card, &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_width(d, SCR_W - 24 - 32 - 190);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(d, detail);
        lv_obj_align(d, LV_ALIGN_LEFT_MID, 0, 13);
        char v[24] = "-";
        if (r->has) {
            switch (r->unit) {
            case REC_TIME:
                fmt_time(v, sizeof(v), (uint32_t)r->value);
                break;
            case REC_DAYS:
                snprintf(v, sizeof(v), "%ld day%s", (long)r->value, r->value == 1 ? "" : "s");
                break;
            case REC_MINUTES:
                if (r->value >= 60) {
                    snprintf(v, sizeof(v), "%ld h %02ld min", (long)(r->value / 60), (long)(r->value % 60));
                } else {
                    snprintf(v, sizeof(v), "%ld min", (long)r->value);
                }
                break;
            default:
                snprintf(v, sizeof(v), "%ld", (long)r->value);
                break;
            }
        }
        lv_obj_t *vl = make_label(card, r->unit == REC_TIME && r->has ? &font_timer_36 : &lv_font_montserrat_24,
                                  r->has ? (i == 0 ? C_ORANGE : C_TEXT) : C_MUTED);
        lv_label_set_text(vl, v);
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_scroll_to_y(tr_rec_list, 0, LV_ANIM_OFF);
}

/* asks sudoku.db for what the open tab shows */
void tr_ask(void)
{
    if (!is_stats_db_ready()) {
        tr_hint(tr_rec_list, "Records are worked out from the solves in sudoku.db: insert a FAT32 microSD card "
                "before switching on.");
        tr_hint(tr_week_list, "The weekly challenge needs sudoku.db on a microSD card.");
        lv_label_set_text(tr_ach_count, "");
        tr_hint(tr_ach_list, "Achievements are worked out from the solves in sudoku.db: insert a FAT32 microSD "
                "card before switching on.");
        tr_hint(tr_board_list, "The leaderboard needs sudoku.db on a microSD card.");
        lv_label_set_text(tr_day_lbl, "");
        lv_obj_set_hidden(tr_play_btn, true);
        return;
    }
    if (tr_tab_sel == 0) {
        stats_db_query_achievements();
        return;
    }
    if (tr_tab_sel == TR_RECORDS) {
        stats_db_query_records();
        return;
    }
    if (tr_tab_sel == TR_WEEK) {
        char now[12];
        if (!this_monday(now)) {
            lv_label_set_text(tr_week_lbl, "");
            tr_hint(tr_week_list, "The weekly challenge needs today's date: connect Wi-Fi (Settings) so the clock "
                    "is set.");
            return;
        }
        if (!tr_monday[0] || strcmp(tr_monday, now) > 0) {
            snprintf(tr_monday, sizeof(tr_monday), "%s", now);
        }
        stats_db_query_week(tr_monday);
        return;
    }
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
    if (!tr_day[0] || strcmp(tr_day, today) > 0) {
        snprintf(tr_day, sizeof(tr_day), "%s", today);
    }
    stats_db_query_board(tr_day);
}

static void tr_select_tab(int tab)
{
    tr_tab_sel = tab;
    for (int i = 0; i < TR_TABS; i++) {
        lv_obj_set_state(tr_tab[i], LV_STATE_CHECKED, i == tab);
        lv_obj_set_style_text_color(lv_obj_get_child(tr_tab[i], 0), i == tab ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(tr_view[i], i != tab);
    }
    tr_ask();
}

static void tr_tab_cb(lv_event_t *e)
{
    tr_select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

static void tr_day_cb(lv_event_t *e)
{
    char today[12];
    if (!tr_today(today)) {
        return;
    }
    if (!tr_day[0]) {
        snprintf(tr_day, sizeof(tr_day), "%s", today);
    }
    day_shift(tr_day, (int)(intptr_t)lv_event_get_user_data(e));
    if (strcmp(tr_day, today) > 0) {
        snprintf(tr_day, sizeof(tr_day), "%s", today);
    }
    stats_db_query_board(tr_day);
}

void trophy_cb(lv_event_t *e)
{
    tr_day[0] = 0;                                       /* the leaderboard opens on today */
    tr_monday[0] = 0;                                    /* and the weekly one on this week */
    tr_select_tab(tr_tab_sel);
    lv_screen_load(scr_trophy);
}

/* sudoku.db answers for the Trophies screen and the home button line */
void trophy_poll(void)
{
    static stats_achievements_t *a;                      /* PSRAM: internal RAM is tight */
    static stats_board_t *b;
    if (!a) {
        a = heap_caps_malloc(sizeof(*a), MALLOC_CAP_SPIRAM);
    }
    if (!b) {
        b = heap_caps_malloc(sizeof(*b), MALLOC_CAP_SPIRAM);
    }
    if (!a || !b) {
        return;
    }
    if (stats_db_take_achievements(a)) {
        if (h_trophy_sub) {
            lv_label_set_text_fmt(h_trophy_sub, "%d of %d achievements", a->unlocked, a->n);
        }
        if (scr_trophy && a->player_id == store_player_id()) {
            render_achievements(a);
        }
    }
    if (stats_db_take_board(b) && scr_trophy && !strcmp(b->day, tr_day)) {
        render_board(b);
    }
    static stats_records_t *r;
    if (!r) {
        r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM);
    }
    if (r && stats_db_take_records(r) && scr_trophy && r->player_id == store_player_id()) {
        render_records(r);
    }
    static stats_week_t *w;
    if (!w) {
        w = heap_caps_malloc(sizeof(*w), MALLOC_CAP_SPIRAM);
    }
    if (w && stats_db_take_week(w) && scr_trophy && !strcmp(w->monday, tr_monday)) {
        render_week(w);
    }
}

void build_trophies(void)
{
    scr_trophy = make_screen();
    make_btn(scr_trophy, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_trophy, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Trophies");
    lv_obj_set_pos(t, 150, 20);
    static const char *const TABS[TR_TABS] = {SYMBOL_TROPHY "  Achievements", SYMBOL_STAR "  Daily",
                                              SYMBOL_CALENDAR "  Weekly", SYMBOL_CHART "  Records"
                                             };
    /* the tabs: a row across the page under the title, each as wide as its label needs
     * with room either side (Achievements is the long one); 232 + 136 + 154 + 150 + 3 x 8 = 696 */
    static const int TAB_W[TR_TABS] = {232, 136, 154, 150};
    const int tab_gap = 8;
    for (int i = 0, x = 12; i < TR_TABS; x += TAB_W[i++] + tab_gap) {
        tr_tab[i] = make_btn(scr_trophy, x, TR_TAB_Y, TAB_W[i], TR_TAB_H, TABS[i], &font_icons_20, tr_tab_cb,
                             (void *)(intptr_t)i);
    }

    /* achievements */
    lv_obj_t *av = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(av);
    lv_obj_set_pos(av, 12, TR_VIEW_Y);
    lv_obj_set_size(av, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[0] = av;
    tr_ach_count = make_label(av, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_pos(tr_ach_count, 4, 4);
    tr_ach_list = lv_obj_create(av);
    lv_obj_remove_style_all(tr_ach_list);
    lv_obj_set_pos(tr_ach_list, 0, 36);
    lv_obj_set_size(tr_ach_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12 - 36);
    lv_obj_set_flex_flow(tr_ach_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_ach_list, 8, 0);
    lv_obj_set_scroll_dir(tr_ach_list, LV_DIR_VER);

    /* daily leaderboard: < day > and the list */
    lv_obj_t *bv = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(bv);
    lv_obj_set_pos(bv, 12, TR_VIEW_Y);
    lv_obj_set_size(bv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[1] = bv;
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

    /* weekly: < week > and the list */
    lv_obj_t *wv = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(wv);
    lv_obj_set_pos(wv, 12, TR_VIEW_Y);
    lv_obj_set_size(wv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[TR_WEEK] = wv;
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

    /* records */
    lv_obj_t *rv = lv_obj_create(scr_trophy);
    lv_obj_remove_style_all(rv);
    lv_obj_set_pos(rv, 12, TR_VIEW_Y);
    lv_obj_set_size(rv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_view[TR_RECORDS] = rv;
    tr_rec_list = lv_obj_create(rv);
    lv_obj_remove_style_all(tr_rec_list);
    lv_obj_set_size(tr_rec_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    lv_obj_set_flex_flow(tr_rec_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_rec_list, 8, 0);
    lv_obj_set_scroll_dir(tr_rec_list, LV_DIR_VER);
    tr_hint(tr_rec_list, "Loading...");

    for (int i = 0; i < TR_TABS; i++) {
        lv_obj_set_state(tr_tab[i], LV_STATE_CHECKED, i == tr_tab_sel);
        lv_obj_set_style_text_color(lv_obj_get_child(tr_tab[i], 0), i == tr_tab_sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(tr_view[i], i != tr_tab_sel);
    }
    lv_label_set_text(tr_ach_count, "");
    tr_hint(tr_ach_list, "Loading...");
    tr_hint(tr_board_list, "Loading...");
}

/* "Achievement unlocked" at the top of the screen, a moment after the Solved dialog */
static char s_toast_text[200];

static void trophy_toast_cb(lv_timer_t *t)
{
    lv_obj_t *n = lv_obj_create(lv_layer_top());
    lv_obj_set_size(n, SCR_W - 80, LV_SIZE_CONTENT);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(n, C_CARD, 0);
    lv_obj_set_style_border_color(n, C_ORANGE, 0);
    lv_obj_set_style_border_width(n, 2, 0);
    lv_obj_set_style_radius(n, 14, 0);
    lv_obj_set_style_pad_all(n, 14, 0);
    lv_obj_set_style_shadow_width(n, 24, 0);
    lv_obj_set_style_shadow_opa(n, LV_OPA_40, 0);
    lv_obj_set_scrollable(n, false);
    lv_obj_set_clickable(n, false);
    lv_obj_t *icon = make_label(n, &font_icons_24, C_ORANGE);
    lv_label_set_text(icon, SYMBOL_TROPHY);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *l = make_label(n, &lv_font_montserrat_18, C_TEXT);
    lv_obj_set_width(l, SCR_W - 80 - 28 - 52);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, s_toast_text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 48, 0);
    sfx_play(SFX_DIGIT_DONE);
    lv_timer_t *del = lv_timer_create(notice_del_cb, 5000, n);
    lv_timer_set_repeat_count(del, 1);
}

void trophy_toast_poll(void)
{
    char titles[160];
    int n = 0;
    if (!stats_db_take_unlocked(titles, sizeof(titles), &n)) {
        return;
    }
    snprintf(s_toast_text, sizeof(s_toast_text), "%s unlocked:\n%s", n == 1 ? "Achievement" : "Achievements", titles);
    lv_timer_t *t = lv_timer_create(trophy_toast_cb, 1200, NULL);   /* after the fanfare starts */
    lv_timer_set_repeat_count(t, 1);
    if (h_trophy_sub && is_stats_db_ready()) {
        stats_db_query_achievements();    /* the home line */
    }
}

/* ---- accessors: the other UI files reach this file's state through these */
lv_obj_t *screen_trophy(void) { return scr_trophy; }
lv_obj_t *home_trophy_sub(void) { return h_trophy_sub; }
void home_set_trophy_sub(lv_obj_t *v) { h_trophy_sub = v; }

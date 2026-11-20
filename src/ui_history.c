/* sudoku_ui.c split: History and Gameplays. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void history_row_cb(lv_event_t *e);
static void fmt_clock(char *buf, size_t n, int64_t t);
static void history_hint(const char *text);
static void render_day(const stats_day_t *d);
static void render_month(const stats_month_t *m);
static void history_cal_draw_cb(lv_event_t *e);
static void history_ask_month(int year, int month);
static void history_day_cb(lv_event_t *e);
static void history_back_cb(lv_event_t *e);
static void plays_hint(const char *text);
static void plays_row_cb(lv_event_t *e);
static void plays_export_cb(lv_event_t *e);
static void plays_pager_update(void);
static void render_plays(const stats_plays_t *p);
static void plays_page_cb(lv_event_t *e);

static lv_obj_t *scr_plays;               /* Gameplays: recorded solves to replay */

/* history screen */
static lv_obj_t *hz_cal, *hz_title, *hz_list;
static lv_calendar_date_t hz_marked[31];  /* highlighted days (the calendar keeps the pointer) */
static int hz_year, hz_month;             /* month asked for / shown, 0 = not yet */
static uint8_t hz_count[31];              /* games per day of the highlighted month (badges) */
static lv_calendar_date_t hz_sel;         /* the day tapped (its list is shown), year 0 = none */
static bool hz_latest_pending;            /* asked for "the latest month"; show what comes back */

/* a solve from History: ask sudoku.db, history_poll() opens it */
static int64_t hz_row_solve[STATS_MAX_DAY_ROWS];

/* ------------------------------------------------------------- history */

static void fmt_clock(char *buf, size_t n, int64_t t)
{
    if (!t) {
        snprintf(buf, n, "?");
        return;
    }
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    strftime(buf, n, "%H:%M", &tm);
}

static void history_hint(const char *text)
{
    lv_obj_clean(hz_list);
    lv_obj_t *l = make_label(hz_list, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
}

static void render_day(const stats_day_t *d)
{
    struct tm tm = {.tm_year = d->year - 1900, .tm_mon = d->month - 1, .tm_mday = d->day, .tm_hour = 12};
    mktime(&tm);                                   /* fills in the weekday */
    char date[48];
    strftime(date, sizeof(date), "%A, %d %B %Y", &tm);
    lv_label_set_text(hz_title, date);
    if (!d->n) {
        history_hint("Nobody played that day.");
        return;
    }
    lv_obj_clean(hz_list);
    for (int i = 0; i < d->n; i++) {
        const stats_day_row_t *r = &d->rows[i];
        lv_obj_t *card = lv_obj_create(hz_list);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_CARD, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_hor(card, 16, 0);
        lv_obj_set_style_pad_ver(card, 10, 0);
        lv_obj_set_style_pad_row(card, 4, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollable(card, false);

        char name[40], took[16], start[8], end[8], line[128];
        puzzle_name(r->puzzle, name, sizeof(name));
        fmt_time(took, sizeof(took), r->seconds);
        fmt_clock(start, sizeof(start), r->started_at);
        fmt_clock(end, sizeof(end), r->finished_at);
        lv_obj_t *wr = lv_obj_create(card);            /* who: the avatar (or colour dot), name, puzzle */
        lv_obj_remove_style_all(wr);
        lv_obj_set_size(wr, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(wr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(wr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(wr, 8, 0);
        lv_obj_set_clickable(wr, false);
        player_badge(wr, player_find(0, r->player), -1, 28, 10);
        lv_obj_t *who = make_label(wr, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(who, "%s   -   %s", r->player, name);
        lv_obj_t *what = make_label(card, &lv_font_montserrat_14, r->in_progress ? C_ORANGE : C_MUTED);
        if (r->in_progress) {
            snprintf(line, sizeof(line), "Started %s   -   in progress, %s so far   -   %u mistake%s",
                     start, took, (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s");
        } else {
            snprintf(line, sizeof(line), "Solved %s in %s   -   %u mistake%s   -   started %s",
                     end, took, (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s", start);
        }
        if (r->hints) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   %u hint%s",
                     (unsigned)r->hints, r->hints == 1 ? "" : "s");
        }
        if (r->flags & GAME_FLAG_NO_NOTES) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   no notes");
        }
        lv_label_set_text(what, line);
        hz_row_solve[i] = 0;
        if (!r->in_progress && r->has_moves && r->solve_id) {     /* tap to watch it again */
            hz_row_solve[i] = r->solve_id;
            lv_obj_set_clickable(card, true);
            lv_obj_set_style_bg_color(card, C_CARD2, LV_STATE_PRESSED);
            lv_obj_add_event_cb(card, history_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *rp = make_label(card, &lv_font_montserrat_14, C_ACCENT);
            lv_label_set_text(rp, LV_SYMBOL_PLAY "  Tap to replay");
        }
    }
    if (d->total > d->n) {
        char more[48];
        snprintf(more, sizeof(more), "... and %d more", d->total - d->n);
        lv_obj_t *l = make_label(hz_list, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(l, more);
    }
}

static void render_month(const stats_month_t *m)
{
    memcpy(hz_count, m->count, sizeof(hz_count));
    int n = 0;
    for (int d = 1; d <= 31; d++) {
        if (!m->count[d - 1]) continue;
        hz_marked[n].year = m->year;
        hz_marked[n].month = m->month;
        hz_marked[n].day = d;
        n++;
    }
    lv_calendar_set_highlighted_dates(hz_cal, hz_marked, n);
}

/* The tapped day: solid accent fill, white number (the calendar itself only
 * marks it faintly, and only on highlighted days). Plus a badge with the
 * number of games at the top right of each highlighted day. */
static void history_cal_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_task_type_t type = lv_draw_task_get_type(t);
    if (type != LV_DRAW_TASK_TYPE_LABEL && type != LV_DRAW_TASK_TYPE_FILL) return;
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) return;
    lv_obj_t *bm = lv_event_get_current_target_obj(e);
    uint32_t id = base->id1;
    if (id < 7 || lv_buttonmatrix_has_button_ctrl(bm, id, LV_BUTTONMATRIX_CTRL_DISABLED)) return;  /* names, other months */
    int day = atoi(lv_buttonmatrix_get_button_text(bm, id));
    if (day < 1 || day > 31) return;

    const lv_calendar_date_t *sh = lv_calendar_get_showed_date(hz_cal);
    bool selected = hz_sel.year == sh->year && hz_sel.month == sh->month && hz_sel.day == day;
    if (type == LV_DRAW_TASK_TYPE_FILL) {
        if (selected) {
            lv_draw_fill_dsc_t *f = lv_draw_task_get_fill_dsc(t);
            f->color = C_ACCENT;
            f->opa = LV_OPA_COVER;
        }
        return;
    }
    if (selected) lv_draw_task_get_label_dsc(t)->color = lv_color_white();

    if (!lv_buttonmatrix_has_button_ctrl(bm, id, LV_BUTTONMATRIX_CTRL_CUSTOM_2)) return;  /* highlighted */
    if (!hz_count[day - 1]) return;

    char txt[4];
    if (hz_count[day - 1] > 99) snprintf(txt, sizeof(txt), "99");
    else snprintf(txt, sizeof(txt), "%u", (unsigned)hz_count[day - 1]);
    lv_area_t a;                                    /* the day number's text area */
    lv_draw_task_get_area(t, &a);
    int w = txt[1] ? 24 : 18, h = 18;
    lv_area_t pill = {.x1 = a.x2 + 3, .y1 = a.y1 - 10, .x2 = a.x2 + 3 + w - 1, .y2 = a.y1 - 10 + h - 1};

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = C_ORANGE;
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(base->layer, &rd, &pill);
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = &lv_font_montserrat_12;
    ld.color = lv_color_white();
    ld.align = LV_TEXT_ALIGN_CENTER;
    ld.text = txt;
    ld.text_local = 1;                              /* copied: txt is on the stack */
    lv_area_t ta = pill;
    ta.y1 += (h - lv_font_get_line_height(ld.font)) / 2;
    lv_draw_label(base->layer, &ld, &ta);
}

static void history_ask_month(int year, int month)
{
    hz_year = year;
    hz_month = month;
    stats_db_query_month(year, month);
}

static void history_day_cb(lv_event_t *e)
{
    lv_calendar_date_t d;
    if (lv_calendar_get_pressed_date(hz_cal, &d) != LV_RESULT_OK || !d.year) return;
    hz_sel = d;
    lv_obj_invalidate(lv_calendar_get_btnmatrix(hz_cal));
    if (!stats_db_ready()) return;
    stats_db_query_day(d.year, d.month, d.day);
}

void history_cb(lv_event_t *e)
{
    lv_label_set_text(hz_title, "Tap a day to see who played what");
    hz_sel.year = 0;                                /* nothing picked yet */
    if (!stats_db_ready()) {
        history_hint("No sudoku.db: insert a FAT32 microSD card before switching on to keep a history.");
    } else {
        history_hint("Highlighted days had games; the badge says how many. Solves from before the "
                     "clock was set (Wi-Fi) "
                     "have no date and don't appear.");
        time_t now = time(NULL);
        if (CLOCK_VALID(now)) {
            struct tm tm;
            localtime_r(&now, &tm);
            lv_calendar_set_today_date(hz_cal, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            lv_calendar_set_month_shown(hz_cal, tm.tm_year + 1900, tm.tm_mon + 1);
            hz_latest_pending = false;
            history_ask_month(tm.tm_year + 1900, tm.tm_mon + 1);
        } else {
            hz_latest_pending = true;                  /* no clock: open on the latest month played */
            history_ask_month(0, 0);
        }
    }
    lv_screen_load(screen_history());
}

static void history_back_cb(lv_event_t *e)
{
    show_home();
}

/* results from sudoku.db, and the month arrows (called from sd_poll_cb) */
void history_poll(void)
{
    static stats_month_t m;
    if (stats_db_take_month(&m)) {
        if (hz_latest_pending && m.year) {
            lv_calendar_set_month_shown(hz_cal, m.year, m.month);
            hz_year = m.year;
            hz_month = m.month;
        }
        hz_latest_pending = false;
        if (m.year == hz_year && m.month == hz_month) render_month(&m);
    }
    static stats_day_t d;
    if (stats_db_take_day(&d)) render_day(&d);
    if (lv_screen_active() == screen_history() && stats_db_ready() && !hz_latest_pending) {
        const lv_calendar_date_t *sh = lv_calendar_get_showed_date(hz_cal);
        if (sh->year != hz_year || sh->month != hz_month) history_ask_month(sh->year, sh->month);
    }
}

void build_history(void)
{
    screen_set_history(make_screen());
    make_btn(screen_history(), 12, 10, 120, 52, LV_SYMBOL_LEFT "  Back", &lv_font_montserrat_18,
             history_back_cb, NULL);
    lv_obj_t *t = make_label(screen_history(), &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "History");
    lv_obj_set_pos(t, 150, 20);

    hz_cal = lv_calendar_create(screen_history());
    lv_obj_set_pos(hz_cal, 12, 72);
    lv_obj_set_size(hz_cal, SCR_W - 24, 380);
    lv_calendar_add_header_arrow(hz_cal);
    lv_obj_set_style_text_font(hz_cal, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(hz_cal, C_CARD, 0);
    lv_obj_set_style_border_width(hz_cal, 0, 0);
    lv_obj_set_style_radius(hz_cal, 12, 0);
    lv_obj_t *bm = lv_calendar_get_btnmatrix(hz_cal);
    lv_obj_set_style_text_color(bm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_send_draw_task_events(bm, true);
    lv_obj_add_event_cb(bm, history_cal_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(hz_cal, history_day_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (hz_year) lv_calendar_set_month_shown(hz_cal, hz_year, hz_month);   /* theme rebuild */

    hz_title = make_label(screen_history(), &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(hz_title, 16, 462);
    lv_label_set_text(hz_title, "Tap a day to see who played what");

    hz_list = lv_obj_create(screen_history());
    lv_obj_remove_style_all(hz_list);
    lv_obj_set_pos(hz_list, 12, 494);
    lv_obj_set_size(hz_list, SCR_W - 24, SCR_W - 494 - 10);
    lv_obj_set_flex_flow(hz_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hz_list, 8, 0);
    lv_obj_set_scroll_dir(hz_list, LV_DIR_VER);
}

/* ------------------------------------------------------------- gameplays */
/*
 * Every solve with a move log, newest first, a page of STATS_MAX_DAY_ROWS at a
 * time from sudoku.db. Tap one to replay it (the Replay screen; Back returns
 * here).
 */
static lv_obj_t *pl_list, *pl_count, *pl_newer, *pl_older;
static int pl_offset, pl_total;
static int64_t pl_row_solve[STATS_MAX_DAY_ROWS];

static void plays_hint(const char *text)
{
    lv_obj_clean(pl_list);
    lv_obj_t *l = make_label(pl_list, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
}

static void plays_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !pl_row_solve[i] || replay_busy()) return;
    replay_set_want_export(false);
    stats_db_query_replay(pl_row_solve[i]);
}

static void plays_export_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !pl_row_solve[i] || replay_busy()) return;
    replay_set_want_export(true);
    stats_db_query_replay(pl_row_solve[i]);
}

static void plays_pager_update(void)
{
    bool newer = pl_offset > 0, older = pl_offset + STATS_MAX_DAY_ROWS < pl_total;
    lv_obj_set_state(pl_newer, LV_STATE_DISABLED, !newer);
    lv_obj_set_state(pl_older, LV_STATE_DISABLED, !older);
    lv_obj_set_hidden(pl_newer, !newer && !older);
    lv_obj_set_hidden(pl_older, !newer && !older);
}

static void render_plays(const stats_plays_t *p)
{
    pl_offset = p->offset;
    pl_total = p->total;
    if (p->total == 0) {
        lv_label_set_text(pl_count, "");
        plays_hint("No recorded games yet. Solve a puzzle and it shows up here, ready to replay.");
    } else {
        lv_label_set_text_fmt(pl_count, "%d-%d of %d", p->offset + 1, p->offset + p->n, p->total);
        lv_obj_clean(pl_list);
    }
    for (int i = 0; i < p->n; i++) {
        const stats_day_row_t *r = &p->rows[i];
        pl_row_solve[i] = r->solve_id;
        lv_obj_t *card = lv_obj_create(pl_list);
        lv_obj_set_size(card, LV_PCT(100), 76);
        lv_obj_set_style_bg_color(card, C_CARD, 0);
        lv_obj_set_style_bg_color(card, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_hor(card, 16, 0);
        lv_obj_set_scrollable(card, false);
        lv_obj_add_event_cb(card, plays_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        char name[40], took[16], when[40], line[128];
        puzzle_name(r->puzzle, name, sizeof(name));
        fmt_time(took, sizeof(took), r->seconds);
        if (r->finished_at) {
            time_t tt = (time_t)r->finished_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%a %d %b %Y, %H:%M", &tm);
        } else {
            snprintf(when, sizeof(when), "Date unknown");
        }
        lv_obj_align(player_badge(card, player_find(0, r->player), -1, AVATAR_SIZE, 12), LV_ALIGN_LEFT_MID, -4, 0);
        lv_obj_t *who = make_label(card, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(who, "%s   -   %s", r->player, name);
        lv_obj_align(who, LV_ALIGN_LEFT_MID, 44, -13);
        lv_obj_t *what = make_label(card, &lv_font_montserrat_14, C_MUTED);
        snprintf(line, sizeof(line), "%s   -   %s   -   %u mistake%s", when, took,
                 (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s");
        if (r->hints) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   %u hint%s", (unsigned)r->hints, r->hints == 1 ? "" : "s");
        }
        if (r->flags & GAME_FLAG_NO_NOTES) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   no notes");
        }
        lv_label_set_text(what, line);
        lv_obj_align(what, LV_ALIGN_LEFT_MID, 44, 14);
        lv_obj_t *play = make_label(card, &lv_font_montserrat_24, C_ACCENT);
        lv_label_set_text(play, LV_SYMBOL_PLAY);
        lv_obj_align(play, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *mp4 = make_btn(card, 0, 0, 96, 52, LV_SYMBOL_DOWNLOAD "  MP4", &lv_font_montserrat_16,
                                 plays_export_cb, (void *)(intptr_t)i);   /* save as a video */
        lv_obj_set_style_bg_color(mp4, C_CARD2, 0);
        lv_obj_align(mp4, LV_ALIGN_RIGHT_MID, -44, 0);
    }
    lv_obj_scroll_to_y(pl_list, 0, LV_ANIM_OFF);
    plays_pager_update();
}

void plays_ask(int offset)
{
    if (!stats_db_ready()) {
        lv_label_set_text(pl_count, "");
        plays_hint("No sudoku.db: insert a FAT32 microSD card before switching on to keep recorded games.");
        pl_total = 0;
        plays_pager_update();
        return;
    }
    stats_db_query_plays(offset < 0 ? 0 : offset);
}

static void plays_page_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);   /* -1 newer, +1 older */
    plays_ask(pl_offset + dir * STATS_MAX_DAY_ROWS);
}

/* results from sudoku.db (called from sd_poll_cb) */
void plays_poll(void)
{
    static stats_plays_t *p;
    if (!p) p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM);
    if (p && stats_db_take_plays(p)) render_plays(p);
}

void build_plays(void)
{
    scr_plays = make_screen();
    make_btn(scr_plays, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_plays, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Gameplays");
    lv_obj_set_pos(t, 150, 20);
    pl_count = make_label(scr_plays, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(pl_count, LV_ALIGN_TOP_RIGHT, -16, 28);
    lv_label_set_text(pl_count, "");

    pl_list = lv_obj_create(scr_plays);
    lv_obj_remove_style_all(pl_list);
    lv_obj_set_pos(pl_list, 12, 74);
    lv_obj_set_size(pl_list, SCR_W - 24, SCR_W - 74 - 72);
    lv_obj_set_flex_flow(pl_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pl_list, 8, 0);
    lv_obj_set_scroll_dir(pl_list, LV_DIR_VER);

    pl_newer = make_btn(scr_plays, 12, SCR_W - 62, 200, 52, LV_SYMBOL_LEFT "  Newer", &lv_font_montserrat_18,
                        plays_page_cb, (void *)(intptr_t)-1);
    pl_older = make_btn(scr_plays, SCR_W - 12 - 200, SCR_W - 62, 200, 52, "Older  " LV_SYMBOL_RIGHT,
                        &lv_font_montserrat_18, plays_page_cb, (void *)(intptr_t)1);
    lv_obj_set_style_text_opa(lv_obj_get_child(pl_newer, 0), LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(lv_obj_get_child(pl_older, 0), LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_hidden(pl_newer, true);
    lv_obj_set_hidden(pl_older, true);
    plays_hint("Loading...");
}

static void history_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !hz_row_solve[i]) return;
    replay_set_want_export(false);
    stats_db_query_replay(hz_row_solve[i]);
}

/* ---- accessors: the other UI files reach this file's state through these */
lv_obj_t *screen_plays(void) { return scr_plays; }
int plays_offset(void) { return pl_offset; }

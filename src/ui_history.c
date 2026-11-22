/**
 * @file ui_history.c
 * @ingroup ui_history
 * @brief History (Home): a calendar of the days that had games (a badge says how many), and the
 *        games of the day tapped, each solve with a move log replayable. The data: history_core.h.
 */
#include "ui_internal.h"
#include "history_core.h"

/**
 * @defgroup ui_history History screen
 * @ingroup topic_history
 * @brief The History screen: the calendar and a day's games (Gameplays: ui_gameplays.c).
 * @{
 */

/* this file's own functions, in any order */
static void history_row_cb(lv_event_t *e);
static void fmt_clock(char *buf, size_t n, int64_t t);
static void history_hint(const char *text);
static void render_day(const stats_day_t *d);
static void render_month(const stats_month_t *m);
static void history_cal_draw_cb(lv_event_t *e);
static void history_day_cb(lv_event_t *e);
static void history_back_cb(lv_event_t *e);

static lv_obj_t *hz_cal;                  /**< the calendar */
static lv_obj_t *hz_title;                /**< the day picked, or what to do */
static lv_obj_t *hz_list;                 /**< its games */
static lv_calendar_date_t hz_marked[31];  /**< highlighted days (the calendar keeps the pointer) */

/**
 * @brief A Unix time as the local "HH:MM".
 * @param[out] buf  gets it ("?" for 0: not known)
 * @param[in] n    its size
 * @param[in] t    the time
 */
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

/**
 * @brief Replaces the day's list with a line of text (what to do, nothing that day, why not).
 * @param[in] text  the text
 */
static void history_hint(const char *text)
{
    lv_obj_clean(hz_list);
    lv_obj_t *l = make_label(hz_list, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
}

/**
 * @brief Shows a day: its date, and a card per game (who, the puzzle, solved or in progress, the
 *        times, mistakes, hints, no notes); a solve with a move log can be tapped to replay it.
 * @param[in] d  the day
 */
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
        if (history_row_solve(i)) {                     /* tap to watch it again */
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

/**
 * @brief Highlights a month's days that had games on the calendar.
 * @param[in] m  the month
 */
static void render_month(const stats_month_t *m)
{
    int n = 0;
    for (int d = 1; d <= 31; d++) {
        if (!m->count[d - 1]) {
            continue;
        }
        hz_marked[n].year = m->year;
        hz_marked[n].month = m->month;
        hz_marked[n].day = d;
        n++;
    }
    lv_calendar_set_highlighted_dates(hz_cal, hz_marked, n);
}

/**
 * @brief The calendar's drawing: the day picked with a solid accent fill and a white number (the
 *        calendar itself only marks it faintly, and only on highlighted days), and a badge with
 *        the number of games at the top right of each highlighted day.
 * @param[in] e  the draw task event
 */
static void history_cal_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_task_type_t type = lv_draw_task_get_type(t);
    if (type != LV_DRAW_TASK_TYPE_LABEL && type != LV_DRAW_TASK_TYPE_FILL) {
        return;
    }
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) {
        return;
    }
    lv_obj_t *bm = lv_event_get_current_target_obj(e);
    uint32_t id = base->id1;
    if (id < 7 || lv_buttonmatrix_has_button_ctrl(bm, id, LV_BUTTONMATRIX_CTRL_DISABLED)) {
        return;    /* names, other months */
    }
    int day = atoi(lv_buttonmatrix_get_button_text(bm, id));
    if (day < 1 || day > 31) {
        return;
    }

    const lv_calendar_date_t *sh = lv_calendar_get_showed_date(hz_cal);
    bool selected = is_history_picked(sh->year, sh->month, day);
    if (type == LV_DRAW_TASK_TYPE_FILL) {
        if (selected) {
            lv_draw_fill_dsc_t *f = lv_draw_task_get_fill_dsc(t);
            f->color = C_ACCENT;
            f->opa = LV_OPA_COVER;
        }
        return;
    }
    if (selected) {
        lv_draw_task_get_label_dsc(t)->color = lv_color_white();
    }

    if (!lv_buttonmatrix_has_button_ctrl(bm, id, LV_BUTTONMATRIX_CTRL_CUSTOM_2)) {
        return;    /* highlighted */
    }
    uint8_t count = history_day_count(day);
    if (!count) {
        return;
    }

    char txt[4];
    if (count > 99) {
        snprintf(txt, sizeof(txt), "99");
    } else {
        snprintf(txt, sizeof(txt), "%u", (unsigned)count);
    }
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

/**
 * @brief A day tapped on the calendar: picked (drawn so), and its games asked for.
 * @param[in] e  the event
 */
static void history_day_cb(lv_event_t *e)
{
    lv_calendar_date_t d;
    if (lv_calendar_get_pressed_date(hz_cal, &d) != LV_RESULT_OK || !d.year) {
        return;
    }
    history_pick(d.year, d.month, d.day);
    lv_obj_invalidate(lv_calendar_get_btnmatrix(hz_cal));
}

/**
 * @brief Home's History button: the screen, on this month (no clock: the latest month played),
 *        nothing picked yet.
 * @param[in] e  the event
 */
void history_cb(lv_event_t *e)
{
    lv_label_set_text(hz_title, "Tap a day to see who played what");
    history_pick(0, 0, 0);                          /* nothing picked yet */
    if (!is_stats_db_ready()) {
        history_hint("No sudoku.db: insert a FAT32 microSD card before switching on to keep a history.");
    } else {
        history_hint("Highlighted days had games; the badge says how many. Solves from before the "
                     "clock was set (Wi-Fi) "
                     "have no date and don't appear.");
        int y, m, d;
        if (history_today(&y, &m, &d)) {
            lv_calendar_set_today_date(hz_cal, y, m, d);
            lv_calendar_set_month_shown(hz_cal, y, m);
            history_ask_current(y, m);
        } else {
            history_ask_latest();
        }
    }
    lv_screen_load(screen_history());
}

/**
 * @brief Back: Home.
 * @param[in] e  the event
 */
static void history_back_cb(lv_event_t *e)
{
    show_home();
}

/** @brief sudoku.db's answers, and the month arrows (from the UI's poll). */
void history_poll(void)
{
    bool jump;
    const stats_month_t *m = history_take_month(&jump);
    if (jump) {
        lv_calendar_set_month_shown(hz_cal, m->year, m->month);
    }
    if (m) {
        render_month(m);
    }
    const stats_day_t *d = history_take_day();
    if (d) {
        render_day(d);
    }
    if (lv_screen_active() == screen_history()) {
        const lv_calendar_date_t *sh = lv_calendar_get_showed_date(hz_cal);
        history_follow(sh->year, sh->month);
    }
}

/** @brief Builds the screen: Back, the title, the calendar, the day's title and list. */
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
    int y, m;
    if (history_month_asked(&y, &m)) {
        lv_calendar_set_month_shown(hz_cal, y, m);    /* theme rebuild */
    }

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

/**
 * @brief A replayable game tapped: its replay (the Replay screen opens on sudoku.db's answer).
 * @param[in] e  the event (user data: the row)
 */
static void history_row_cb(lv_event_t *e)
{
    history_open_row((int)(intptr_t)lv_event_get_user_data(e));
}

/** @} */

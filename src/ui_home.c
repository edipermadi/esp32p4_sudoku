/**
 * @file ui_home.c
 * @brief Home: the week, the clock, the menu (Continue's button: ui_continue.c).
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void daily_btn_update(void);
static void home_total_text(void);
static void week_row(lv_obj_t *parent, const char *name, const char *a, const char *b, bool head);
static void week_cb(lv_event_t *e);
static void puzzles_cb(lv_event_t *e);
static void settings_cb(lv_event_t *e);
static void stats_cb(lv_event_t *e);
static void plays_cb(lv_event_t *e);
static void profile_cb(lv_event_t *e);
static lv_obj_t *home_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **sub);

static lv_obj_t *h_menu;                  /**< the pages, side by side: swipe between them */
static lv_obj_t *h_page[HOME_PAGES_MAX];  /**< the menu's pages */
static lv_obj_t *h_dots;                  /**< the page dots */
static lv_obj_t *h_total;                 /**< "19 of 1600 puzzles solved" (and the week) */
static lv_obj_t *h_continue;              /**< Continue */
static lv_obj_t *h_continue_sub;          /**< its line: the game in progress */
static lv_obj_t *h_clock;                 /**< the clock */
static lv_obj_t *h_date;                  /**< the date (or why there's no clock) */
static lv_obj_t *h_player;                /**< Players' line: who plays */
static lv_obj_t *h_daily_sub;             /**< Daily puzzle's line */
static lv_obj_t *h_daily;                 /**< Daily puzzle */
static lv_obj_t *h_player_title;          /**< the Players button's title */
static lv_obj_t *h_player_avatar;         /**< its avatar (else the icon) */
static bool      st_wifi_reveal;            /**< name shown; hidden again each time Settings opens */

/* ------------------------------------------------------------- the week (Home) */
/** @brief The "puzzles solved" line also says how this week is going; a tap compares it
 * with last week (stats_db_query_weeksum: needs the clock, so only once it's set). */
static stats_weeksum_t h_week;
static bool h_week_ok;                  /**< h_week holds this week and last (once the clock is set) */
static unsigned h_total_n;              /**< puzzles solved */

/**
 * @brief The "puzzles solved" line: the count, and how this week is going.
 */
static void home_total_text(void)
{
    char t[16], line[128];
    int len = snprintf(line, sizeof(line), "%u of %u puzzles solved", h_total_n, (unsigned)PUZZLE_COUNT);
    if (h_week_ok) {
        len += snprintf(line + len, sizeof(line) - len, "   -   this week: %d", h_week.n[0]);
        if (h_week.n[0]) {
            fmt_time(t, sizeof(t), h_week.best[0]);
            len += snprintf(line + len, sizeof(line) - len, ", best %s", t);
        }
        snprintf(line + len, sizeof(line) - len, "  " LV_SYMBOL_RIGHT);
    }
    lv_label_set_text(h_total, line);
}

/**
 * @brief A row of the week's comparison.
 * @param[in] parent  where
 * @param[in] name  the row's name
 * @param[in] a  this week
 * @param[in] b  last week
 * @param[in] head  the header row
 */
static void week_row(lv_obj_t *parent, const char *name, const char *a, const char *b, bool head)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 34);
    const lv_font_t *f = &lv_font_montserrat_18;
    lv_obj_t *l = make_label(r, f, head ? C_MUTED : C_TEXT);
    lv_label_set_text(l, name);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *x = make_label(r, f, head ? C_MUTED : C_TEXT);
    lv_label_set_text(x, a);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, -150, 0);
    lv_obj_t *y = make_label(r, f, C_MUTED);
    lv_label_set_text(y, b);
    lv_obj_align(y, LV_ALIGN_RIGHT_MID, 0, 0);
}

/**
 * @brief The solved line tapped: this week against last.
 * @param[in] e  the event
 */
static void week_cb(lv_event_t *e)
{
    if (!h_week_ok) {
        return;
    }
    const stats_weeksum_t *w = &h_week;
    lv_obj_t *m = make_msgbox(SYMBOL_CALENDAR "  This week", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    char a[24], b[24];
    week_row(c, "", "This week", "Last week", true);
    snprintf(a, sizeof(a), "%d", w->n[0]);
    snprintf(b, sizeof(b), "%d", w->n[1]);
    week_row(c, "Solved", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) {
            fmt_time(o, 24, w->best[k]);
        } else {
            snprintf(o, 24, "-");
        }
    }
    week_row(c, "Fastest", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) {
            fmt_time(o, 24, w->avg[k]);
        } else {
            snprintf(o, 24, "-");
        }
    }
    week_row(c, "Average time", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) {
            snprintf(o, 24, "%u.%u", w->mist10[k] / 10, w->mist10[k] % 10);
        } else {
            snprintf(o, 24, "-");
        }
    }
    week_row(c, "Mistakes per puzzle", a, b, false);
    snprintf(a, sizeof(a), "%lu", (unsigned long)w->hints[0]);
    snprintf(b, sizeof(b), "%lu", (unsigned long)w->hints[1]);
    week_row(c, "Hints", a, b, false);
    char verdict[128] = "";
    if (w->n[0] && w->n[1]) {
        int dm = (int)w->mist10[1] - (int)w->mist10[0];
        int dt = (int)w->avg[1] - (int)w->avg[0];
        char t[16];
        fmt_time(t, sizeof(t), (uint32_t)(dt < 0 ? -dt : dt));
        snprintf(verdict, sizeof(verdict), "%s%d.%d %s mistakes per puzzle, and %s %s on average than last week.",
                 "", (dm < 0 ? -dm : dm) / 10, (dm < 0 ? -dm : dm) % 10, dm >= 0 ? "fewer" : "more",
                 t, dt >= 0 ? "faster" : "slower");
    } else if (w->n[0]) {
        snprintf(verdict, sizeof(verdict), "Nothing solved last week to compare with.");
    } else {
        snprintf(verdict, sizeof(verdict), "Nothing solved yet this week: the week starts on Monday.");
    }
    lv_obj_t *v = make_wrap_text(c, &lv_font_montserrat_18, C_ACCENT, verdict);
    lv_obj_set_style_margin_top(v, 10, 0);
    msgbox_button(m, "Close", mbox_close_cb, false);
}

/* (documented in ui_internal.h) */
void home_player_update(void)
{
    if (!h_player_title) {
        return;
    }
    static bool asked;
    int av = 0;
    for (int i = 0; i < players_count(); i++) {
        if (players_all()[i].id == store_player_id() && players_all()[i].avatar <= AVATAR_COUNT) {
            av = players_all()[i].avatar;
        }
    }
    if (players_count() < 0 && !asked && is_stats_db_ready()) {
        asked = true;
        stats_db_list_players();                    /* (render_players calls back here when it comes) */
    }
    lv_obj_set_hidden(h_player_avatar, !av);
    if (av) {
        lv_image_set_src(h_player_avatar, avatar_image(av - 1));
    }
    lv_label_set_text(h_player_title, av ? "Players" : SYMBOL_USER "  Players");
}

/* (documented in ui_internal.h) */
void refresh_home(void)
{
    uint16_t total = 0;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        total += store_solved_count(puzzle_collection(c)->first, puzzle_collection(c)->count);
    }
    h_total_n = total;
    home_total_text();
    if (is_stats_db_ready() && time(NULL) > 1700000000) {
        stats_db_query_weeksum();    /* (clock_cb picks it up) */
    }
    lv_label_set_text_fmt(h_player, "Playing as %s", store_player_name());
    home_player_update();

    continue_line_update(h_continue, h_continue_sub);   /* (the unfinished games read again) */
    daily_btn_update();
    if (is_stats_db_ready()) {
        stats_db_query_achievements();    /* the Trophies line (trophy_poll) */
    }
    home_layout();
}

/* (documented in ui_internal.h) */
void show_home(void)
{
    refresh_home();
    lv_screen_load(screen_home());
}

/* (documented in ui_internal.h) */
void home_cb(lv_event_t *e)
{
    show_home();
}

/**
 * @brief Home > Puzzles.
 * @param[in] e  the event
 */
static void puzzles_cb(lv_event_t *e)
{
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    lv_screen_load(screen_list());
}

/* (documented in ui_internal.h) */
void st_wifi_update(void)
{
    wifi_status_t ws;
    wifi_mgr_get_status(&ws);
    bool connected = ws.state == WIFI_ST_CONNECTED;
    const char *w = connected && !st_wifi_reveal ? "Connected  ******" : wifi_ui_summary();
    if (strcmp(lv_label_get_text(settings_wifi_lbl()), w) != 0) {
        lv_label_set_text(settings_wifi_lbl(), w);
    }
    lv_obj_set_hidden(settings_wifi_eye(), !connected);
    lv_label_set_text(lv_obj_get_child(settings_wifi_eye(), 0), st_wifi_reveal ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

/* (documented in ui_internal.h) */
void st_wifi_eye_cb(lv_event_t *e)
{
    st_wifi_reveal = !st_wifi_reveal;
    st_wifi_update();
}

/**
 * @brief Home > Settings (the Network tab, the network's name hidden).
 * @param[in] e  the event
 */
static void settings_cb(lv_event_t *e)
{
    st_wifi_reveal = false;                         /* the network's name hidden again */
    st_wifi_update();
    st_select_tab(0);                               /* Network */
    lv_screen_load(screen_settings());
}

/* (documented in ui_internal.h) */
void wifi_back_cb(lv_event_t *e)
{
    wifi_ui_set_shown(false);
    settings_cb(e);
}

/* (documented in ui_internal.h) */
void clock_cb(lv_timer_t *t)
{
    static char last_clock[8], last_date[64];
    lv_obj_t *act = lv_screen_active();
    if (act == screen_settings()) {
        st_wifi_update();
        char sd[64];
        stats_db_status(sd, sizeof(sd));
        if (strcmp(lv_label_get_text(settings_sd_hint()), sd) != 0) {
            lv_label_set_text(settings_sd_hint(), sd);
        }
        usb_btn_update();
        return;
    }
    if (act != screen_home()) {
        return;
    }

    char c[8], d[64];
    time_t now = time(NULL);
    bool valid = now > 1700000000;                         /* set by NTP */
    /* the Daily puzzle line depends on the date: redo it when the clock gets
     * set (Wi-Fi) and at midnight */
    static char daily_key[12] = "?";
    char key[12] = "";
    if (valid) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(key, sizeof(key), "%Y-%m-%d", &tm);
    }
    if (strcmp(key, daily_key) != 0) {
        strcpy(daily_key, key);
        daily_btn_update();
        if (valid && is_stats_db_ready()) {
            stats_db_query_weeksum();    /* the clock is set, or a new day */
        }
    }
    if (stats_db_take_weeksum(&h_week)) {
        h_week_ok = valid;
        home_total_text();
    }
    if (valid) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(c, sizeof(c), "%H:%M", &tm);
        strftime(d, sizeof(d), "%A, %d %B", &tm);
    } else {
        wifi_status_t ws;
        wifi_mgr_get_status(&ws);
        c[0] = 0;
        snprintf(d, sizeof(d), "%s", ws.state == WIFI_ST_CONNECTED ? "Getting the time..."
                 : "Connect Wi-Fi in Settings to show the time");
    }
    if (strcmp(c, last_clock) != 0 || lv_obj_is_hidden(h_clock) == valid) {
        strcpy(last_clock, c);
        lv_label_set_text(h_clock, c);
        lv_obj_set_hidden(h_clock, !valid);
    }
    if (strcmp(d, last_date) != 0) {
        strcpy(last_date, d);
        lv_label_set_text(h_date, d);
    }
}

/**
 * @brief big menu button: icon + title, optional muted subtitle below
 * @param[in] parent  where
 * @param[in] text  the title
 * @param[in] cb  a tap
 * @param[out] sub  gets the subtitle, or NULL for none
 * @return the button
 */
static lv_obj_t *home_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **sub)
{
    lv_obj_t *b = make_btn(parent, 0, 0, HOME_COL_W, HOME_BTN_H, NULL, NULL, cb, NULL);
    if (home_nitems() < (int)(sizeof(lv_obj_t *[16]) / sizeof(home_items()[0]))) {   /* (the menu's order) */
        home_set_items_at(home_nitems(), b);
        home_set_nitems(home_nitems() + 1);
    }
    lv_obj_t *l = make_label(b, &lv_font_montserrat_24, C_TEXT);
    lv_label_set_text(l, text);
    if (sub) {
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -12);
        *sub = make_label(b, &lv_font_montserrat_16, C_MUTED);
        lv_obj_set_size(*sub, LV_PCT(90), lv_font_get_line_height(&lv_font_montserrat_16));   /* one line: "..." */
        lv_obj_set_style_text_align(*sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(*sub, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(*sub, LV_ALIGN_CENTER, 0, 17);
    } else {
        lv_obj_center(l);
    }
    return b;
}

/* (documented in ui_internal.h) */
void build_home(void)
{
    screen_set_home(make_screen());

    h_clock = make_label(screen_home(), &font_timer_36, C_TEXT);
    lv_obj_align(h_clock, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_hidden(h_clock, true);
    h_date = make_label(screen_home(), &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(h_date, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *pw = make_btn(screen_home(), 0, 0, 64, 52, LV_SYMBOL_POWER, &lv_font_montserrat_24,
                            power_off_cb, NULL);
    lv_obj_align(pw, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_obj_t *lk = make_btn(screen_home(), 0, 0, 64, 52, SYMBOL_LOCK, &font_icons_24, lock_now_cb, NULL);
    lv_obj_align(lk, LV_ALIGN_TOP_RIGHT, -12 - 64 - 10, 10);   /* left of the power button: lock now */

    lv_obj_t *col = lv_obj_create(screen_home());
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, HOME_GRID_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 40);   /* clear of the clock */

    lv_obj_t *t = make_label(col, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Sudoku");
    h_total = make_label(col, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_style_margin_bottom(h_total, 22, 0);
    lv_obj_set_clickable(h_total, true);                 /* the week, compared with the last (week_cb) */
    lv_obj_set_ext_click_area(h_total, 12);
    lv_obj_add_event_cb(h_total, week_cb, LV_EVENT_CLICKED, NULL);

    /* the menu: one column, a grid, or pages of the grid to swipe (home_layout) */
    h_menu = lv_obj_create(col);
    lv_obj_remove_style_all(h_menu);
    lv_obj_set_size(h_menu, HOME_GRID_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(h_menu, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(h_menu, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(h_menu, LV_SCROLL_SNAP_START);
    lv_obj_set_scroll_one(h_menu, true);                /* a swipe moves one page */
    lv_obj_set_scrollbar_mode(h_menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(h_menu, home_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        h_page[p] = lv_obj_create(h_menu);
        lv_obj_remove_style_all(h_page[p]);
        lv_obj_set_size(h_page[p], HOME_GRID_W, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(h_page[p], HOME_GAP, 0);
        lv_obj_set_style_pad_column(h_page[p], HOME_GAP, 0);
        lv_obj_set_scrollable(h_page[p], false);
    }
    home_set_menu_grid(-1);                                   /* not laid out yet */
    home_set_nitems(0);
    lv_obj_t *const menu = h_page[0];                   /* (home_layout sorts them onto the pages) */

    /* who is playing, first */
    lv_obj_t *pb = home_btn(menu, SYMBOL_USER "  Players", profile_cb, &h_player);   /* sub: "Playing as ..." */
    /* the title in a row with the player's avatar (home_player_update): the icon without one */
    h_player_title = lv_obj_get_child(pb, 0);
    lv_obj_set_style_text_font(h_player_title, &font_icons_24, 0);
    lv_obj_t *tr = lv_obj_create(pb);
    lv_obj_remove_style_all(tr);
    lv_obj_set_size(tr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tr, 10, 0);
    lv_obj_set_clickable(tr, false);
    lv_obj_align(tr, LV_ALIGN_CENTER, 0, -12);
    h_player_avatar = lv_image_create(tr);
    lv_obj_set_parent(h_player_title, tr);
    lv_obj_align(h_player_title, LV_ALIGN_DEFAULT, 0, 0);
    home_player_update();

    /* greyed out when there is no game in progress (refresh_home) */
    h_continue = home_btn(menu, LV_SYMBOL_PLAY "  Continue", continue_cb, &h_continue_sub);
    lv_obj_set_style_bg_opa(h_continue, LV_OPA_40, LV_STATE_DISABLED);    /* as Daily puzzle's */
    lv_obj_set_style_text_opa(h_continue, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(h_continue, C_LIST_ACTIVE, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_continue, 0), lv_color_white(), 0);
    lv_obj_set_style_text_color(h_continue_sub, lv_color_white(), 0);
    lv_obj_set_style_text_opa(h_continue_sub, LV_OPA_80, 0);

    home_btn(menu, LV_SYMBOL_LIST "  Puzzles", puzzles_cb, NULL);
    h_daily = home_btn(menu, LV_SYMBOL_BELL "  Daily puzzle", daily_cb, &h_daily_sub);
    lv_obj_set_style_bg_opa(h_daily, LV_OPA_40, LV_STATE_DISABLED);       /* greyed out until the clock is set */
    lv_obj_set_style_text_opa(h_daily, LV_OPA_40, LV_STATE_DISABLED);
    home_btn(menu, LV_SYMBOL_BARS "  Statistics", stats_cb, NULL);
    lv_obj_t *hb = home_btn(menu, SYMBOL_CALENDAR "  History", history_cb, NULL);   /* the calendar of games */
    lv_obj_set_style_text_font(lv_obj_get_child(hb, 0), &font_icons_24, 0);
    home_btn(menu, LV_SYMBOL_VIDEO "  Gameplays", plays_cb, NULL);
    lv_obj_t *trophy_sub = NULL;
    lv_obj_t *tb = home_btn(menu, SYMBOL_TROPHY "  Trophies", trophy_cb, &trophy_sub);   /* achievements, leaderboard */
    home_set_trophy_sub(trophy_sub);
    lv_obj_set_style_text_font(lv_obj_get_child(tb, 0), &font_icons_24, 0);
    lv_label_set_text(home_trophy_sub(), is_stats_db_ready() ? "Achievements and daily leaderboard" : "Needs a microSD card");
    home_btn(menu, LV_SYMBOL_CHARGE "  Time attack", attack_cb, NULL);              /* 10 minutes of Easy puzzles */
    home_btn(menu, LV_SYMBOL_DIRECTORY "  Files", files_open_cb, NULL);              /* the microSD card */
    home_btn(menu, LV_SYMBOL_SETTINGS "  Settings", settings_cb, NULL);              /* (the second page) */
    lv_obj_t *sb = home_btn(menu, SYMBOL_CHART "  System", sysmon_open_cb, NULL);    /* system monitor */
    lv_obj_set_style_text_font(lv_obj_get_child(sb, 0), &font_icons_24, 0);

    h_dots = lv_obj_create(screen_home());                    /* page dots (home_dots) */
    lv_obj_remove_style_all(h_dots);
    lv_obj_set_size(h_dots, LV_SIZE_CONTENT, 12);
    lv_obj_set_flex_flow(h_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(h_dots, 12, 0);
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_t *d = lv_obj_create(h_dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 12, 12);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
    lv_obj_align(h_dots, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_set_hidden(h_dots, true);

    lv_obj_t *src = make_label(screen_home(), &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(src, "Puzzles: opensudoku.gitlab.io");
    lv_obj_align(src, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/** @brief the Daily puzzle button: its line, and greyed out (not tappable) until
 * the clock is set (Wi-Fi connected, time fetched) */
static void daily_btn_update(void)
{
    char ds[80];
    daily_sub_text(ds, sizeof(ds));
    lv_label_set_text(h_daily_sub, ds);
    time_t now = time(NULL);
    lv_obj_set_state(h_daily, LV_STATE_DISABLED, !CLOCK_VALID(now));
}


/**
 * @brief Home > Statistics.
 * @param[in] e  the event
 */
static void stats_cb(lv_event_t *e)
{
    if (is_stats_db_ready()) {
        stats_db_list_players();    /* fresh list for the player dropdown */
    }
    refresh_stats();
    lv_screen_load(screen_stats());
}

/**
 * @brief Home > Gameplays.
 * @param[in] e  the event
 */
static void plays_cb(lv_event_t *e)
{
    plays_ask(0);
    lv_screen_load(screen_plays());
}

/**
 * @brief Home > Players.
 * @param[in] e  the event
 */
static void profile_cb(lv_event_t *e)
{
    render_players();
    if (is_stats_db_ready()) {
        stats_db_list_players();    /* fresh counts; drawn when they arrive */
    }
    lv_screen_load(screen_profile());
}

/* (documented in ui_internal.h) */
void sysmon_open_cb(lv_event_t *e)
{
    lv_screen_load(screen_sysmon());
    sysmon_ui_set_shown(true);
}

/* (documented in ui_internal.h) */
void files_open_cb(lv_event_t *e)
{
    lv_screen_load(screen_files());
    file_ui_set_shown(true);
}

/* ---- accessors: the other UI files reach this file's state through these */
lv_obj_t *home_menu(void) { return h_menu; }
lv_obj_t *const *home_page(void) { return h_page; }
lv_obj_t *home_dots_obj(void) { return h_dots; }

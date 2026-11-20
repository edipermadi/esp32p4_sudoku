/* sudoku_ui.c split: Home: the week, Continue and unfinished games, building the menu. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void home_total_text(void);
static void week_row(lv_obj_t *parent, const char *name, const char *a, const char *b, bool head);
static void week_cb(lv_event_t *e);
static void puzzles_cb(lv_event_t *e);
static void settings_cb(lv_event_t *e);
static bool resume_game(uint16_t id);
static void ug_close(void);
static void ug_close_cb(lv_event_t *e);
static void ug_resume_cb(lv_event_t *e);
static void ug_discard_cb(lv_event_t *e);
static void ug_open(void);
static void continue_cb(lv_event_t *e);
static void discard_confirm_cb(lv_event_t *e);
static void resume_other_cb(lv_event_t *e);
static lv_obj_t *home_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **sub);

/* ------------------------------------------------------------- the week (Home) */
/* The "puzzles solved" line also says how this week is going; a tap compares it
 * with last week (stats_db_query_weeksum: needs the clock, so only once it's set). */
static stats_weeksum_t h_week;
static bool h_week_ok;
static unsigned h_total_n;

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

static void week_cb(lv_event_t *e)
{
    if (!h_week_ok) return;
    const stats_weeksum_t *w = &h_week;
    lv_obj_t *m = make_msgbox(SYMBOL_CALENDAR "  This week", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    char a[24], b[24];
    week_row(c, "", "This week", "Last week", true);
    snprintf(a, sizeof(a), "%d", w->n[0]); snprintf(b, sizeof(b), "%d", w->n[1]);
    week_row(c, "Solved", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) fmt_time(o, 24, w->best[k]); else snprintf(o, 24, "-");
    }
    week_row(c, "Fastest", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) fmt_time(o, 24, w->avg[k]); else snprintf(o, 24, "-");
    }
    week_row(c, "Average time", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) snprintf(o, 24, "%u.%u", w->mist10[k] / 10, w->mist10[k] % 10); else snprintf(o, 24, "-");
    }
    week_row(c, "Mistakes per puzzle", a, b, false);
    snprintf(a, sizeof(a), "%lu", (unsigned long)w->hints[0]); snprintf(b, sizeof(b), "%lu", (unsigned long)w->hints[1]);
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

/* Home's Players button: the avatar of who plays now (from the player list; asked for once
 * when there isn't one yet), else the person icon */
void home_player_update(void)
{
    if (!h_player_title) return;
    static bool asked;
    int av = 0;
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == store_player_id() && p_players[i].avatar <= AVATAR_COUNT) av = p_players[i].avatar;
    }
    if (p_count < 0 && !asked && stats_db_ready()) {
        asked = true;
        stats_db_list_players();                    /* (render_players calls back here when it comes) */
    }
    lv_obj_set_hidden(h_player_avatar, !av);
    if (av) lv_image_set_src(h_player_avatar, &avatar_images[av - 1]);
    lv_label_set_text(h_player_title, av ? "Players" : SYMBOL_USER "  Players");
}

void refresh_home(void)
{
    uint16_t total = 0;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        total += store_solved_count(puzzle_collections[c].first, puzzle_collections[c].count);
    }
    h_total_n = total;
    home_total_text();
    if (stats_db_ready() && time(NULL) > 1700000000) stats_db_query_weeksum();   /* (clock_cb picks it up) */
    lv_label_set_text_fmt(h_player, "Playing as %s", store_player_name());
    home_player_update();

    const saved_game_t *s = store_saved_game();
    games_refresh();
    /* the game played last: the one in RAM if there is one, else sudoku.db's newest */
    uint16_t last = s->id != STORE_NO_GAME ? s->id : (s_ngames ? s_games[0].puzzle : STORE_NO_GAME);
    uint32_t ms = s->id != STORE_NO_GAME ? s->elapsed_ms : (s_ngames ? s_games[0].elapsed_ms : 0);
    int others = 0;
    for (int i = 0; i < s_ngames; i++) others += s_games[i].puzzle != last;
    /* Continue stays in its place: greyed out (like Daily puzzle without the clock) when
     * there is nothing to continue, so the menu doesn't shift around */
    bool any = last != STORE_NO_GAME;
    lv_obj_set_state(h_continue, LV_STATE_DISABLED, !any);
    lv_obj_set_style_bg_color(h_continue, any ? C_LIST_ACTIVE : C_CARD, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_continue, 0), any ? lv_color_white() : C_TEXT, 0);
    lv_obj_set_style_text_color(h_continue_sub, any ? lv_color_white() : C_MUTED, 0);
    if (any) {
        char name[40], t[16];
        puzzle_name(last, name, sizeof(name));
        fmt_time(t, sizeof(t), ms / 1000);
        if (others) lv_label_set_text_fmt(h_continue_sub, "%s  -  %s   (+%d more)", name, t, others);
        else lv_label_set_text_fmt(h_continue_sub, "%s  -  %s", name, t);
    } else {
        lv_label_set_text(h_continue_sub, "No game in progress");
    }
    daily_btn_update();
    if (stats_db_ready()) stats_db_query_achievements();   /* the Trophies line (trophy_poll) */
    home_layout();
}

void show_home(void)
{
    refresh_home();
    lv_screen_load(scr_home);
}

void home_cb(lv_event_t *e)
{
    show_home();
}

static void puzzles_cb(lv_event_t *e)
{
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    lv_screen_load(scr_list);
}

/* The Wi-Fi row's text: the state, or once connected the network's name,
 * which stays hidden ("Connected  ******") until the eye button shows it, so
 * a photo or screenshot of Settings doesn't give it away. */
void st_wifi_update(void)
{
    wifi_status_t ws;
    wifi_mgr_get_status(&ws);
    bool connected = ws.state == WIFI_ST_CONNECTED;
    const char *w = connected && !st_wifi_reveal ? "Connected  ******" : wifi_ui_summary();
    if (strcmp(lv_label_get_text(st_wifi_lbl), w) != 0) lv_label_set_text(st_wifi_lbl, w);
    lv_obj_set_hidden(st_wifi_eye, !connected);
    lv_label_set_text(lv_obj_get_child(st_wifi_eye, 0), st_wifi_reveal ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void st_wifi_eye_cb(lv_event_t *e)
{
    st_wifi_reveal = !st_wifi_reveal;
    st_wifi_update();
}

static void settings_cb(lv_event_t *e)
{
    st_wifi_reveal = false;                         /* the network's name hidden again */
    st_wifi_update();
    st_select_tab(0);                               /* Network */
    lv_screen_load(scr_settings);
}

void wifi_open_cb(lv_event_t *e)
{
    lv_screen_load(scr_wifi);
    wifi_ui_set_shown(true);
}

void wifi_back_cb(lv_event_t *e)
{
    wifi_ui_set_shown(false);
    settings_cb(e);
}

/* "Asia/Dubai  UTC+4\n..." for the time zone dropdown (built once) */
const char *tz_dropdown_options(void)
{
    static char *opts;
    if (opts) return opts;
    size_t n = 0;
    for (int i = 0; i < tz_count(); i++) n += strlen(tz_get(i)->name) + 16;
    opts = (char *)malloc(n);
    if (!opts) return "UTC";
    char *p = opts;
    for (int i = 0; i < tz_count(); i++) {
        const tz_entry_t *z = tz_get(i);
        const char *sp = strstr(z->label, "  ");          /* label: "UTC+4  Dubai" */
        int off_len = sp ? (int)(sp - z->label) : 0;
        if (strcmp(z->name, "UTC") == 0) off_len = 0;
        p += sprintf(p, "%s%s%.*s%s", z->name, off_len ? "  " : "", off_len, z->label,
                     i < tz_count() - 1 ? "\n" : "");
    }
    return opts;
}

void tz_show_selected(void)
{
    const tz_entry_t *z = tz_get((int)lv_dropdown_get_selected(st_tz_dd));
    snprintf(st_tz_txt, sizeof(st_tz_txt), "%s", z->name);
    lv_dropdown_set_text(st_tz_dd, st_tz_txt);
}

void tz_dd_cb(lv_event_t *e)
{
    const tz_entry_t *z = tz_get((int)lv_dropdown_get_selected(st_tz_dd));
    store_set_timezone(z->name);
    tz_apply(z->name);
    tz_show_selected();
}

/* home clock and the Settings Wi-Fi line; runs every second */
void clock_cb(lv_timer_t *t)
{
    static char last_clock[8], last_date[64];
    lv_obj_t *act = lv_screen_active();
    if (act == scr_settings) {
        st_wifi_update();
        char sd[64];
        stats_db_status(sd, sizeof(sd));
        if (strcmp(lv_label_get_text(st_sd_hint), sd) != 0) lv_label_set_text(st_sd_hint, sd);
        usb_btn_update();
        return;
    }
    if (act != scr_home) return;

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
        if (valid && stats_db_ready()) stats_db_query_weeksum();   /* the clock is set, or a new day */
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

/* Resumes one of the current player's unfinished games (from RAM, or loaded
 * from sudoku.db with its move log). False if it isn't there any more. */
static bool resume_game(uint16_t id)
{
    if (store_saved_game()->id != id) {
        saved_game_t g;
        if (!stats_db_ready() || !stats_db_load_game(id, &g)) return false;
        store_set_game(&g);                              /* the one being played now */
        store_load_game_moves();
    }
    start_game(id, true);
    return true;
}

/* ---- the Unfinished games dialog (Continue with more than one) */
static lv_obj_t *ug_box;

static void ug_close(void)
{
    if (ug_box) lv_msgbox_close_async(ug_box);
    ug_box = NULL;
}

static void ug_close_cb(lv_event_t *e)
{
    ug_close();
}

static void ug_resume_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    ug_close();
    if (!resume_game(id)) sudoku_ui_notice("That game isn't in sudoku.db any more.", 3000);
}

static void ug_discard_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    if (store_saved_game()->id == id) store_clear_game();
    else stats_db_clear_game(id);
    if (g_id == id && !g_solved) g_id = STORE_NO_GAME;   /* the game screen held it */
    char name[40], msg[80];
    puzzle_name(id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "Discarded %s.", name);
    sudoku_ui_notice(msg, 2500);
    ug_close();
    refresh_home();
    if (s_ngames > 1) ug_open();                         /* the rest */
}

static void ug_open(void)
{
    ug_close();
    lv_obj_t *m = make_msgbox("Unfinished games", NULL);
    ug_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_t *content = lv_msgbox_get_content(m);
    for (int i = 0; i < s_ngames; i++) {
        const stats_game_row_t *r = &s_games[i];
        lv_obj_t *row = make_btn(content, 0, 0, 0, 76, NULL, NULL, ug_resume_cb, (void *)(uintptr_t)r->puzzle);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_style_bg_color(row, C_CARD2, 0);
        lv_obj_set_style_margin_bottom(row, 8, 0);
        char name[40], t[16], when[24] = "", line[96];
        puzzle_name(r->puzzle, name, sizeof(name));
        fmt_time(t, sizeof(t), r->elapsed_ms / 1000);
        if (r->saved_at) {
            time_t tt = (time_t)r->saved_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "   -   %a %d %b, %H:%M", &tm);
        }
        int var = VAR_CLASSIC;
        int cells = puzzle_is_generated(r->puzzle) && gen_lookup(r->puzzle, NULL, &var, NULL)
                        ? (var == VAR_MINI ? 36 : var == VAR_KIDS ? 16 : 81) : 81;
        snprintf(line, sizeof(line), "%s   -   %d / %d filled%s", t, r->filled, cells, when);
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
    msgbox_button(m, "Close", ug_close_cb, false);
}

static void continue_cb(lv_event_t *e)
{
    games_refresh();
    if (s_ngames > 1) {                                  /* pick one */
        ug_open();
        return;
    }
    uint16_t id = store_saved_game()->id != STORE_NO_GAME ? store_saved_game()->id
                                                          : (s_ngames ? s_games[0].puzzle : STORE_NO_GAME);
    if (id != STORE_NO_GAME && !resume_game(id)) sudoku_ui_notice("That game isn't in sudoku.db any more.", 3000);
}

static void discard_confirm_cb(lv_event_t *e)
{
    store_clear_game();
    start_game(l_pending_id, false);
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

static void resume_other_cb(lv_event_t *e)
{
    start_game(store_saved_game()->id, true);
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

void open_puzzle(uint16_t id)
{
    const saved_game_t *s = store_saved_game();
    if (s->id == STORE_NO_GAME || s->id == id) {
        if (s->id != id && stats_db_ready() && resume_game(id)) return;   /* an unfinished game in sudoku.db */
        start_game(id, s->id == id);
        return;
    }
    if (stats_db_ready()) {
        /* sudoku.db keeps several unfinished games: the current one is saved,
         * this one is resumed if it was started before, else begun */
        if (!resume_game(id)) start_game(id, false);
        return;
    }
    /* without a card there's one slot: resume or discard the game in it */
    char cur[40], next[40], t[16], text[200];
    puzzle_name(s->id, cur, sizeof(cur));
    puzzle_name(id, next, sizeof(next));
    fmt_time(t, sizeof(t), s->elapsed_ms / 1000);
    snprintf(text, sizeof(text), "%s is in progress (%s).\nStarting %s will discard it.", cur, t, next);
    l_pending_id = id;
    lv_obj_t *m = make_msgbox("Unfinished game", text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Resume", resume_other_cb, false);
    msgbox_button(m, "Discard", discard_confirm_cb, true);
}

/* big menu button: icon + title, optional muted subtitle below */
static lv_obj_t *home_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **sub)
{
    lv_obj_t *b = make_btn(parent, 0, 0, HOME_COL_W, HOME_BTN_H, NULL, NULL, cb, NULL);
    if (h_nitems < (int)(sizeof(h_items) / sizeof(h_items[0]))) h_items[h_nitems++] = b;   /* (the menu's order) */
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

void build_home(void)
{
    scr_home = make_screen();

    h_clock = make_label(scr_home, &font_timer_36, C_TEXT);
    lv_obj_align(h_clock, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_hidden(h_clock, true);
    h_date = make_label(scr_home, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(h_date, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *pw = make_btn(scr_home, 0, 0, 64, 52, LV_SYMBOL_POWER, &lv_font_montserrat_24,
                            power_off_cb, NULL);
    lv_obj_align(pw, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_obj_t *lk = make_btn(scr_home, 0, 0, 64, 52, SYMBOL_LOCK, &font_icons_24, lock_now_cb, NULL);
    lv_obj_align(lk, LV_ALIGN_TOP_RIGHT, -12 - 64 - 10, 10);   /* left of the power button: lock now */

    lv_obj_t *col = lv_obj_create(scr_home);
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
    h_menu_grid = -1;                                   /* not laid out yet */
    h_nitems = 0;
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
    lv_obj_t *tb = home_btn(menu, SYMBOL_TROPHY "  Trophies", trophy_cb, &h_trophy_sub);   /* achievements, leaderboard */
    lv_obj_set_style_text_font(lv_obj_get_child(tb, 0), &font_icons_24, 0);
    lv_label_set_text(h_trophy_sub, stats_db_ready() ? "Achievements and daily leaderboard" : "Needs a microSD card");
    home_btn(menu, LV_SYMBOL_CHARGE "  Time attack", attack_cb, NULL);              /* 10 minutes of Easy puzzles */
    home_btn(menu, LV_SYMBOL_DIRECTORY "  Files", files_open_cb, NULL);              /* the microSD card */
    home_btn(menu, LV_SYMBOL_SETTINGS "  Settings", settings_cb, NULL);              /* (the second page) */
    lv_obj_t *sb = home_btn(menu, SYMBOL_CHART "  System", sysmon_open_cb, NULL);    /* system monitor */
    lv_obj_set_style_text_font(lv_obj_get_child(sb, 0), &font_icons_24, 0);

    h_dots = lv_obj_create(scr_home);                    /* page dots (home_dots) */
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

    lv_obj_t *src = make_label(scr_home, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(src, "Puzzles: opensudoku.gitlab.io");
    lv_obj_align(src, LV_ALIGN_BOTTOM_MID, 0, -8);
}

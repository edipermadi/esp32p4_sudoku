/**
 * @file ui_trophies.c
 * @ingroup ui_trophies
 * @brief Trophies: the screen around the tabs (title, tab bar), asking for and drawing what the
 *        open tab shows, and the "Achievement unlocked" toast. The tabs: ui_trophies_*.c; the data:
 *        trophies_core.h.
 */
#include "ui_internal.h"
#include "trophies_core.h"
#include "ui_trophies_tab.h"

/* this file's own functions, in any order */
static bool tr_clock_tabs(void);
static void tr_select_tab(int tab);
static void tr_tab_cb(lv_event_t *e);
static void trophy_toast_cb(lv_timer_t *t);

static lv_obj_t *scr_trophy;              /**< the Trophies screen */
static lv_obj_t *tr_tab[TR_TABS];         /**< the tab bar's buttons */
static lv_obj_t *tr_view[TR_TABS];        /**< each tab's pane (its file builds it) */
static int tr_tab_sel;                    /**< the tab shown, kept between visits */
static lv_obj_t *h_trophy_sub;            /**< Home's Trophies button line: "7 of 22 achievements" */

/* (documented in ui_trophies_tab.h) */
void tr_hint(lv_obj_t *list, const char *text)
{
    lv_obj_clean(list);
    lv_obj_t *l = make_label(list, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_top(l, 20, 0);
    lv_label_set_text(l, text);
}

/* (documented in ui_trophies_tab.h) */
lv_obj_t *tr_card(lv_obj_t *list, int h)
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

/** @brief Asks sudoku.db for what the open tab shows (without it: each tab's hint). */
void tr_ask(void)
{
    if (!is_stats_db_ready()) {
        tr_records_no_db();
        tr_weekly_no_db();
        tr_ach_no_db();
        tr_daily_no_db();
        return;
    }
    if (tr_tab_sel == 0) {
        tr_ach_ask();
        return;
    }
    if (tr_tab_sel == TR_RECORDS) {
        tr_records_ask();
        return;
    }
    if (tr_tab_sel == TR_WEEK) {
        tr_weekly_ask();
        return;
    }
    tr_daily_ask();
}

/**
 * @brief The Daily and Weekly tabs need today's date: greyed out (not tappable) until the clock is
 *        set (Wi-Fi connected, time fetched), like Home's Daily puzzle button; the page then opens
 *        on Achievements.
 * @return true if they changed
 */
static bool tr_clock_tabs(void)
{
    bool clock = CLOCK_VALID(time(NULL));
    if (!clock && (tr_tab_sel == 1 || tr_tab_sel == TR_WEEK)) {
        tr_tab_sel = 0;
    }
    if (lv_obj_has_state(tr_tab[1], LV_STATE_DISABLED) == !clock) {
        return false;
    }
    lv_obj_set_state(tr_tab[1], LV_STATE_DISABLED, !clock);
    lv_obj_set_state(tr_tab[TR_WEEK], LV_STATE_DISABLED, !clock);
    return true;
}

/**
 * @brief Shows a tab (its button checked, its pane shown) and asks for what it shows.
 * @param[in] tab  0 Achievements, 1 Daily, TR_WEEK, TR_RECORDS
 */
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

/**
 * @brief A tab's button: that tab.
 * @param[in] e  the event (user data: the tab)
 */
static void tr_tab_cb(lv_event_t *e)
{
    tr_select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

/**
 * @brief Home's Trophies button: the screen, on the tab shown last (Daily on today, Weekly on this
 *        week).
 * @param[in] e  the event
 */
void trophy_cb(lv_event_t *e)
{
    trophies_reset_days();                               /* the leaderboard opens on today, the weekly one on this week */
    tr_clock_tabs();
    tr_select_tab(tr_tab_sel);
    lv_screen_load(scr_trophy);
}

/** @brief sudoku.db's answers for the Trophies screen and Home's button line (from the UI's poll). */
void trophy_poll(void)
{
    if (scr_trophy) {
        tr_clock_tabs();                                 /* the clock was just set: Daily and Weekly open up */
    }
    if (!trophies_bufs()) {
        return;
    }
    const stats_achievements_t *a = trophies_take_achievements();
    if (a) {
        if (h_trophy_sub) {
            lv_label_set_text_fmt(h_trophy_sub, "%d of %d achievements", a->unlocked, a->n);
        }
        if (scr_trophy && a->player_id == store_player_id()) {
            tr_ach_render(a);
        }
    }
    const stats_board_t *b = trophies_take_board();
    if (b && scr_trophy) {
        tr_daily_render(b);
    }
    const stats_records_t *r = trophies_take_records();
    if (r && scr_trophy && r->player_id == store_player_id()) {
        tr_records_render(r);
    }
    const stats_week_t *w = trophies_take_week();
    if (w && scr_trophy) {
        tr_weekly_render(w);
    }
}

/** @brief Builds the screen: the title, the tab bar and each tab's pane. */
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
    for (int i = 1; i <= TR_WEEK; i++) {                 /* Daily, Weekly: greyed out until the clock is set */
        lv_obj_set_style_bg_opa(tr_tab[i], LV_OPA_40, LV_STATE_DISABLED);
        lv_obj_set_style_text_opa(tr_tab[i], LV_OPA_40, LV_STATE_DISABLED);
    }

    tr_view[0] = tr_ach_build(scr_trophy);               /* each tab's pane, in its file */
    tr_view[1] = tr_daily_build(scr_trophy);
    tr_view[TR_WEEK] = tr_weekly_build(scr_trophy);
    tr_view[TR_RECORDS] = tr_records_build(scr_trophy);

    tr_clock_tabs();
    for (int i = 0; i < TR_TABS; i++) {
        lv_obj_set_state(tr_tab[i], LV_STATE_CHECKED, i == tr_tab_sel);
        lv_obj_set_style_text_color(lv_obj_get_child(tr_tab[i], 0), i == tr_tab_sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(tr_view[i], i != tr_tab_sel);
    }
}

static char s_toast_text[200];            /**< the toast's text: "Achievement unlocked:" and the titles */

/**
 * @brief "Achievement unlocked" at the top of the screen, a moment after the Solved dialog, with a
 *        chime; gone after 5 s.
 * @param[in] t  the one-shot timer
 */
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

/** @brief Achievements the last solve unlocked: the toast after the fanfare starts, and Home's line asked again. */
void trophy_toast_poll(void)
{
    if (!trophies_take_unlocked(s_toast_text, sizeof(s_toast_text))) {
        return;
    }
    lv_timer_t *t = lv_timer_create(trophy_toast_cb, 1200, NULL);   /* after the fanfare starts */
    lv_timer_set_repeat_count(t, 1);
    if (h_trophy_sub && is_stats_db_ready()) {
        stats_db_query_achievements();    /* the home line */
    }
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @brief The Trophies screen. @return it (NULL before build_trophies()) */
lv_obj_t *screen_trophy(void) { return scr_trophy; }
/** @brief Home's Trophies button line. @return it (NULL before Home is built) */
lv_obj_t *home_trophy_sub(void) { return h_trophy_sub; }
/** @brief Sets Home's Trophies button line (Home builds it). @param[in] v the label */
void home_set_trophy_sub(lv_obj_t *v) { h_trophy_sub = v; }

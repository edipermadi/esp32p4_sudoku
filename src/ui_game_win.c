/**
 * @file ui_game_win.c
 * @ingroup ui_game
 * @brief The Solved dialog and its confetti: the result, best time, head-to-head, the solve recorded.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static float cf_rand(float lo, float hi);
static void cf_draw_cb(lv_event_t *e);
static void cf_stop(void);
static void cf_tick(lv_timer_t *t);
static void win_show(void);
static void solve_commit_cb(lv_timer_t *t);
static void win_next_cb(lv_event_t *e);
static void win_list_cb(lv_event_t *e);
static void win_hide_cb(lv_event_t *e);
static void win_share_cb(lv_event_t *e);

static lv_obj_t *g_win_badge;    /**< Solved dialog: the no-notes challenge badge */
static lv_obj_t *g_win_share;    /**< Solved dialog: Share (a daily puzzle) */
static lv_obj_t *g_win_ovl;              /**< the Solved dialog */
static lv_obj_t *g_win_time;             /**< its time */
static lv_obj_t *g_win_best;             /**< its best time line */
static lv_obj_t *g_win_miss;             /**< its mistakes line */
static lv_obj_t *g_win_hint;             /**< its hints line */
static lv_obj_t *g_win_h2h;      /**< Solved dialog: how others did on this puzzle */
static lv_obj_t *g_win_h2h_row;  /**< ... in a row with the leader's avatar */
/* the solve, recorded a moment after the Solved dialog is up (SAVE_DELAY_MS) */
static uint16_t s_solve_id;              /**< the puzzle */
static uint32_t s_solve_secs;            /**< its time, s */
static uint32_t s_solve_mistakes;        /**< its mistakes */
static uint32_t s_solve_hints;           /**< its hints */
static uint32_t s_solve_flags;           /**< its GAME_FLAG_* */
static int64_t  s_solve_started;         /**< when it was started, Unix time */
/** @brief The confetti's colours. */
static const uint32_t CF_COLORS[] = {0xE53935, 0xFB8C00, 0xFDD835, 0x43A047, 0x1E88E5, 0x8E24AA, 0xEC407A, 0x00ACC1};
static cf_piece_t *cf_p;                 /**< the pieces (CF_N) */
static lv_obj_t *cf_ovl;                 /**< the overlay drawing them (NULL: none) */
static void (*cf_done)(void);           /**< runs when the confetti is over */
static lv_timer_t *cf_timer;             /**< its redraw */
static uint32_t cf_t0;                   /**< when it started, lv_tick_get() */
static uint32_t cf_last;                 /**< the last frame */

/**
 * @brief A random number.
 * @param[in] lo  from
 * @param[in] hi  to
 * @return lo..hi
 */
static float cf_rand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}

/**
 * @brief Draws the confetti.
 * @param[in] e  the draw event
 */
static void cf_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    uint32_t age = lv_tick_elaps(cf_t0);
    lv_opa_t opa = age + CF_FADE_MS < CF_MS ? LV_OPA_COVER
                   : (lv_opa_t)(LV_OPA_COVER * (CF_MS - LV_MIN(age, CF_MS)) / CF_FADE_MS);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 2;
    d.bg_opa = opa;
    for (int i = 0; i < CF_N; i++) {
        const cf_piece_t *p = &cf_p[i];
        if (p->y > SCR_W + 30 || p->x < -30 || p->x > SCR_W + 30) {
            continue;
        }
        int32_t w = (int32_t)(p->w * fabsf(cosf(p->spin))) + 2;   /* turning over */
        int32_t x = (int32_t)(p->x + 10.0f * sinf(p->sway)), y = (int32_t)p->y;
        lv_area_t a = {x - w / 2, y - p->h / 2, x + (w - 1) / 2, y + (p->h - 1) / 2};
        d.bg_color = lv_color_hex(CF_COLORS[p->color]);
        lv_draw_rect(layer, &d, &a);
    }
}

/**
 * @brief Stops the confetti (its overlay and timer go).
 */
static void cf_stop(void)
{
    if (cf_timer) {
        lv_timer_delete(cf_timer);
    }
    cf_timer = NULL;
    if (cf_ovl) {
        lv_obj_delete(cf_ovl);
    }
    cf_ovl = NULL;
    free(cf_p);
    cf_p = NULL;
}

/**
 * @brief Moves the confetti a frame; at the end it stops and cf_done runs.
 * @param[in] t  the timer
 */
static void cf_tick(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    float dt = (float)lv_tick_diff(now, cf_last) / 1000.0f;
    cf_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;    /* (a slow frame doesn't throw them) */
    }
    for (int i = 0; i < CF_N; i++) {
        cf_piece_t *p = &cf_p[i];
        p->vy += 1300.0f * dt;                      /* gravity, px/s^2 */
        float drag = 1.0f - 2.5f * dt;              /* air: the burst slows fast, then it's a fall */
        p->vx *= drag;
        p->vy *= drag;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->spin += p->spin_v * dt;
        p->sway += p->sway_v * dt;
    }
    lv_obj_invalidate(cf_ovl);
    if (lv_tick_elaps(cf_t0) >= CF_MS) {
        void (*done)(void) = cf_done;
        cf_stop();
        if (done) {
            done();
        }
    }
}

/* (documented in ui_internal.h) */
void confetti(void (*done)(void))
{
    cf_stop();
    cf_p = malloc(sizeof(cf_piece_t) * CF_N);
    if (!cf_p) {
        if (done) {
            done();
        }
        return;
    }
    for (int i = 0; i < CF_N; i++) {
        cf_piece_t *p = &cf_p[i];
        /* the burst, from the middle (tuned so the highest just reach the top edge and
         * nearly all have fallen off the bottom by the end) */
        float ang = cf_rand(0, 6.2832f), speed = cf_rand(400, 1300);
        p->x = SCR_W / 2 + cf_rand(-12, 12);
        p->y = SCR_W / 2 + cf_rand(-12, 12);
        p->vx = speed * cosf(ang);
        p->vy = speed * sinf(ang) - cf_rand(250, 600);   /* mostly upwards */
        p->spin = cf_rand(0, 6.28f);
        p->spin_v = cf_rand(6, 14);
        p->sway = cf_rand(0, 6.28f);
        p->sway_v = cf_rand(3, 6);
        p->w = (uint8_t)cf_rand(9, 15);
        p->h = (uint8_t)cf_rand(12, 20);
        p->color = (uint8_t)(esp_random() % (sizeof(CF_COLORS) / sizeof(CF_COLORS[0])));
    }
    cf_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(cf_ovl);
    lv_obj_set_size(cf_ovl, SCR_W, SCR_W);
    lv_obj_set_clickable(cf_ovl, true);             /* no taps until the dialog is up */
    lv_obj_add_event_cb(cf_ovl, cf_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    cf_done = done;
    cf_t0 = cf_last = lv_tick_get();
    cf_timer = lv_timer_create(cf_tick, 16, NULL);
}

/** @brief the Solved! dialog, after the confetti; the solve is saved once it's up */
static void win_show(void)
{
    if (lv_screen_active() == screen_game() && is_game_solved()) {   /* (the dialog only where it belongs) */
        lv_obj_set_hidden(game_pause_ovl(), true);
        lv_obj_set_hidden(g_win_ovl, false);
    }
    lv_timer_t *ct = lv_timer_create(solve_commit_cb, SAVE_DELAY_MS, NULL);
    lv_timer_set_repeat_count(ct, 1);
}

/**
 * @brief Records the solve (SAVE_DELAY_MS after the Solved dialog is up), and asks how the others did.
 * @param[in] t  the timer
 */
static void solve_commit_cb(lv_timer_t *t)
{
    store_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes);
    stats_db_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes, s_solve_hints,
                          s_solve_started, game_moves(), game_nmoves(), s_solve_flags);   /* finished: now */
    if (store_saved_game()->id == s_solve_id) {
        store_clear_game();                         /* no longer unfinished (its slot held the board before the last move) */
    }
    /* head-to-head: where this leaves the player among everyone who solved it (after the solve above) */
    stats_h2h_t h;
    char line[96] = "";
    if (is_stats_db_ready() && stats_db_puzzle_rank(s_solve_id, &h) && h.rank) {
        char t[16];
        fmt_time(t, sizeof(t), h.leader_seconds);
        if (!h.leader[0]) {
            snprintf(line, sizeof(line), "Nobody else has solved this one yet");
        } else if (h.second) {
            snprintf(line, sizeof(line), SYMBOL_STAR "  1st of %d: ahead of %s (%s)", h.of, h.leader, t);
        } else {
            snprintf(line, sizeof(line), "%d%s of %d   -   %s leads with %s", h.rank, ordinal(h.rank), h.of, h.leader, t);
        }
    }
    lv_label_set_text(g_win_h2h, line);
    while (lv_obj_get_child_count(g_win_h2h_row) > 1) {
        lv_obj_delete(lv_obj_get_child(g_win_h2h_row, 0));
    }
    const stats_player_t *lead = line[0] && h.leader[0] ? player_find(0, h.leader) : NULL;
    if (lead && lead->avatar && lead->avatar <= AVATAR_COUNT) {   /* the one ahead (or behind): their avatar */
        lv_obj_t *b = player_badge(g_win_h2h_row, lead, -1, 26, 8);
        lv_obj_move_to_index(b, 0);
    }
    char day[12] = "";
    if (puzzle_is_generated(s_solve_id) && gen_lookup_day(s_solve_id, NULL, NULL, NULL, day) && day[0]) {
        int id, streak;
        bool solved;
        if (stats_db_daily_status(day, &id, &solved, &streak) && solved) {   /* after the solve above (queued first) */
            char msg[128];
            int len = snprintf(msg, sizeof(msg), "Daily puzzle solved! Streak: %d day%s.", streak,
                               streak == 1 ? "" : "s");
            int rank = 0, of = 0;
            if (stats_db_daily_rank(day, &rank, &of) && rank && of > 1) {
                snprintf(msg + len, sizeof(msg) - len, " You're %d%s of %d today (Trophies > Daily).", rank,
                         ordinal(rank), of);
            }
            sudoku_ui_notice(msg, 5000);
        }
    }
}

/**
 * @brief Solved dialog: Next (the next puzzle of the collection it came from).
 * @param[in] e  the event
 */
static void win_next_cb(lv_event_t *e)
{
    if (game_from_coll() >= PUZZLE_COLLECTION_COUNT && coll_saved_index(game_from_coll()) >= 0) {   /* a saved collection */
        int i = coll_find(game_from_coll(), game_id());
        if (i >= 0) {
            open_puzzle(coll_puzzle(game_from_coll(), (i + 1) % coll_size(game_from_coll())));
            return;
        }
    }
    int ic = coll_of(game_id());
    if (ic >= PUZZLE_COLLECTION_COUNT) {         /* imported: the next one of its collection */
        uint16_t first = coll_first(ic), next = game_id() + 1;
        if (next >= first + coll_size(ic)) {
            next = first;
        }
        open_puzzle(next);
        return;
    }
    if (puzzle_is_generated(game_id())) {             /* another one of the same variant and level */
        int lv = GEN_MEDIUM, var = VAR_CLASSIC;
        gen_lookup(game_id(), NULL, &var, &lv);
        gen_start(var, lv);
        return;
    }
    int c = puzzle_collection_of(game_id());
    const puzzle_collection_t *pc = puzzle_collection(c);
    uint16_t next = game_id() + 1;
    if (next >= pc->first + pc->count) {
        next = pc->first;
    }
    open_puzzle(next);
}

/**
 * @brief Solved dialog: back to the list.
 * @param[in] e  the event
 */
static void win_list_cb(lv_event_t *e)
{
    show_list();
}

/**
 * @brief A tap beside the Solved dialog's card hides it, to look at the solved board.
 * @param[in] e  the event
 */
static void win_hide_cb(lv_event_t *e)
{
    /* tap outside the card: look at the solved board */
    if (lv_event_get_target_obj(e) == g_win_ovl) {
        lv_obj_set_hidden(g_win_ovl, true);
    }
}

/**
 * @brief Solved dialog of a daily puzzle: the result as a QR code, for a phone (and a message to friends)
 * @param[in] e  the event
 */
static void win_share_cb(lv_event_t *e)
{
    char day[12] = "", kind[48] = "", t[16];
    int var = 0, lv = 0;
    if (!puzzle_is_generated(s_solve_id) || !gen_lookup_day(s_solve_id, NULL, &var, &lv, day) || !day[0]) {
        return;
    }
    daily_kind_text(var, lv, kind, sizeof(kind));
    fmt_time(t, sizeof(t), s_solve_secs);
    char hints[24];
    if (s_solve_hints) {
        snprintf(hints, sizeof(hints), "%u hint%s", (unsigned)s_solve_hints, s_solve_hints == 1 ? "" : "s");
    } else {
        snprintf(hints, sizeof(hints), "no hints");
    }
    static char text[256];
    snprintf(text, sizeof(text), "Sudoku daily %s (%s)\n%s: solved in %s, %u mistake%s, %s%s\nESP32-P4 Sudoku",
             day, kind, store_player_name(), t, (unsigned)s_solve_mistakes, s_solve_mistakes == 1 ? "" : "s", hints,
             (s_solve_flags & GAME_FLAG_NO_NOTES) ? ", no notes" : "");
    if (!qr_dialog(SYMBOL_QR "  Today's result", text,
                   "Scan it with a phone to send it to friends: the day, the puzzle, your time and mistakes.")) {
        sudoku_ui_notice("Couldn't make a QR code of the result.", 3000);
    }
}

/* (documented in ui_internal.h) */
void win_replay_cb(lv_event_t *e)
{
    char t[16], sub[96];
    fmt_time(t, sizeof(t), s_solve_secs);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             store_player_name(), t, (unsigned)game_hints(), game_hints() == 1 ? "" : "s");
    open_replay(game_id(), game_moves(), game_moves() ? game_nmoves() : 0, sub, store_player_name());
}

/* (documented in ui_internal.h) */
void win_prepare(void)
{
    uint32_t secs = (game_elapsed_ms() + 500) / 1000;
    uint32_t prev = store_best_time(game_id());
    uint32_t rec = secs ? (secs > 0xFFFF ? 0xFFFF : secs) : 1;   /* as store_record_solve() stores it */
    bool best = !prev || rec < prev;
    s_solve_id = game_id();
    s_solve_secs = secs;
    s_solve_mistakes = game_mistakes();
    s_solve_hints = game_hints();
    s_solve_flags = game_flags();
    s_solve_started = game_started_at();

    char t[16], b[16];
    fmt_time(t, sizeof(t), secs);
    lv_label_set_text(g_win_time, t);
    if (best && prev) {
        fmt_time(b, sizeof(b), prev);
        lv_label_set_text_fmt(g_win_best, "New best!  (was %s)", b);
        lv_obj_set_style_text_color(g_win_best, C_ORANGE, 0);
    } else if (best) {
        lv_label_set_text(g_win_best, "First solve!");
        lv_obj_set_style_text_color(g_win_best, C_ORANGE, 0);
    } else {
        fmt_time(b, sizeof(b), prev);
        lv_label_set_text_fmt(g_win_best, "Best time %s", b);
        lv_obj_set_style_text_color(g_win_best, C_MUTED, 0);
    }
    if (game_mistakes() == 0) {
        lv_label_set_text(g_win_miss, "No mistakes");
        lv_obj_set_style_text_color(g_win_miss, C_GREEN, 0);
    } else {
        lv_label_set_text_fmt(g_win_miss, "%u mistake%s", (unsigned)game_mistakes(), game_mistakes() == 1 ? "" : "s");
        lv_obj_set_style_text_color(g_win_miss, C_MUTED, 0);
    }
    lv_label_set_text(g_win_h2h, "");                 /* (solve_commit_cb fills it in) */
    while (lv_obj_get_child_count(g_win_h2h_row) > 1) {
        lv_obj_delete(lv_obj_get_child(g_win_h2h_row, 0));
    }
    if (game_hints() == 0) {
        lv_label_set_text(g_win_hint, "No hints");
        lv_obj_set_style_text_color(g_win_hint, C_GREEN, 0);
    } else {
        lv_label_set_text_fmt(g_win_hint, "%u hint%s used", (unsigned)game_hints(), game_hints() == 1 ? "" : "s");
        lv_obj_set_style_text_color(g_win_hint, C_ORANGE, 0);
    }
    lv_obj_set_hidden(g_win_badge, !(game_flags() & GAME_FLAG_NO_NOTES));
    char day[12] = "";                              /* a daily puzzle: its result can be shared */
    lv_obj_set_hidden(g_win_share, !(puzzle_is_generated(game_id()) && gen_lookup_day(game_id(), NULL, NULL, NULL, day) &&
                                     day[0]));
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("No sudoku.db on a microSD card: this result is kept only until the board is "
                         "switched off.", 6000);
    }
    lv_obj_set_hidden(game_pause_ovl(), true);
    update_header();
    confetti(win_show);                             /* then the dialog (win_show) */
}

/* (documented in ui_internal.h) */
void win_build(void)
{
    g_win_ovl = lv_obj_create(screen_game());
    lv_obj_remove_style_all(g_win_ovl);
    lv_obj_set_size(g_win_ovl, SCR_W, SCR_W);
    lv_obj_set_style_bg_color(g_win_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_win_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(g_win_ovl, true);
    lv_obj_set_hidden(g_win_ovl, true);
    lv_obj_add_event_cb(g_win_ovl, win_hide_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(g_win_ovl);
    lv_obj_set_size(card, 480, 362);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_GREEN, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_scrollable(card, false);

    lv_obj_t *wt = make_label(card, &lv_font_montserrat_28, C_GREEN);
    lv_label_set_text(wt, LV_SYMBOL_OK "  Solved!");
    lv_obj_align(wt, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(tl, "Time to solve");
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 62);
    g_win_time = make_label(card, &font_timer_36, C_TEXT);
    lv_obj_align(g_win_time, LV_ALIGN_TOP_MID, 0, 86);
    g_win_best = make_label(card, &lv_font_montserrat_18, C_MUTED);
    lv_obj_align(g_win_best, LV_ALIGN_TOP_MID, 0, 136);
    g_win_miss = make_label(card, &lv_font_montserrat_18, C_MUTED);
    lv_obj_align(g_win_miss, LV_ALIGN_TOP_MID, 0, 164);
    g_win_hint = make_label(card, &lv_font_montserrat_18, C_MUTED);
    lv_obj_align(g_win_hint, LV_ALIGN_TOP_MID, 0, 190);
    g_win_h2h_row = lv_obj_create(card);            /* head-to-head: filled in a moment later */
    lv_obj_remove_style_all(g_win_h2h_row);
    lv_obj_set_size(g_win_h2h_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_win_h2h_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_win_h2h_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_win_h2h_row, 8, 0);
    lv_obj_set_clickable(g_win_h2h_row, false);
    lv_obj_align(g_win_h2h_row, LV_ALIGN_TOP_MID, 0, 218);
    g_win_h2h = make_label(g_win_h2h_row, &font_icons_20, C_ACCENT);   /* (with a leader's avatar: 26 + 8 less) */
    lv_obj_set_style_max_width(g_win_h2h, 440 - 34, 0);
    lv_obj_set_height(g_win_h2h, lv_font_get_line_height(&font_icons_20));   /* one line */
    lv_label_set_long_mode(g_win_h2h, LV_LABEL_LONG_MODE_DOTS);
    g_win_badge = make_label(card, &lv_font_montserrat_16, C_ACCENT);   /* the no-notes challenge */
    lv_label_set_text(g_win_badge, LV_SYMBOL_EDIT "  No-notes challenge: done without notes");
    lv_obj_align(g_win_badge, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_hidden(g_win_badge, true);
    lv_obj_t *shot = make_btn(card, 0, 0, 56, 48, SYMBOL_CAMERA, &font_icons_20, screenshot_cb, NULL);
    lv_obj_set_style_bg_color(shot, C_CARD2, 0);    /* a screenshot of the solved board */
    lv_obj_align(shot, LV_ALIGN_TOP_LEFT, 0, 0);
    g_win_share = make_btn(card, 0, 0, 56, 48, SYMBOL_QR, &font_icons_20, win_share_cb, NULL);   /* daily: share */
    lv_obj_set_style_bg_color(g_win_share, C_CARD2, 0);
    lv_obj_align(g_win_share, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_hidden(g_win_share, true);

    lv_obj_t *b1 = make_btn(card, 0, 0, 128, 60, LV_SYMBOL_LIST "  Puzzles", &lv_font_montserrat_16, win_list_cb, NULL);
    lv_obj_align(b1, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(b1, C_CARD2, 0);
    lv_obj_t *b3 = make_btn(card, 0, 0, 128, 60, LV_SYMBOL_PLAY "  Replay", &lv_font_montserrat_16, win_replay_cb, NULL);
    lv_obj_align(b3, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(b3, C_CARD2, 0);
    lv_obj_t *b2 = make_btn(card, 0, 0, 128, 60, "Next  " LV_SYMBOL_RIGHT, &lv_font_montserrat_16, win_next_cb, NULL);
    lv_obj_align(b2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(b2, C_ACCENT, 0);
}

/* (documented in ui_internal.h) */
void win_hide(void)
{
    lv_obj_set_hidden(g_win_ovl, true);
}

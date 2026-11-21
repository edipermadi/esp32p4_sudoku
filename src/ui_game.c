/* sudoku_ui.c split: the game: rules check, saving, the board view, confetti, input events, building its screen. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static bool is_peer(int a, int b);
static bool digit_wrong(int i);
static bool unit_complete(int u);
static int units_completed_by(int i);
static lv_color_t cell_color(int k);
static int filled_count(void);
static int64_t game_started_at(void);
static void solve_commit_cb(lv_timer_t *t);
static float cf_rand(float lo, float hi);
static void cf_draw_cb(lv_event_t *e);
static void cf_stop(void);
static void cf_tick(lv_timer_t *t);
static void confetti(void (*done)(void));
static void win_show(void);
static void on_solved(void);
static void after_move(void);
static void push_undo_keep_redo(void);
static void push_undo(void);
static bool can_edit(void);
static void record_move(int cell, int digit, move_kind_t kind);
static void fill_cb(lv_event_t *e);
static void place_digit(int i, int d);
static void placed_sounds(int i, int d);
static void cell_click_cb(lv_event_t *e);
static void cm_close(void);
static void cm_backdrop_cb(lv_event_t *e);
static void cm_color_cb(lv_event_t *e);
static int cage_sets_text(int k, char *buf, size_t n);
static void cell_long_cb(lv_event_t *e);
static void cell_draw_cb(lv_event_t *e);
static void pad_click_cb(lv_event_t *e);
static void pad_long_cb(lv_event_t *e);
static void auto_notes_cb(lv_event_t *e);
static void erase_cb(lv_event_t *e);
static void undo_cb(lv_event_t *e);
static void redo_cb(lv_event_t *e);
static void notes_cb(lv_event_t *e);
static void hint_act_cb(lv_event_t *e);
static void hint_close_cb(lv_event_t *e);
static void hint_cb(lv_event_t *e);
static void pause_cb(lv_event_t *e);
static void win_next_cb(lv_event_t *e);
static void win_list_cb(lv_event_t *e);
static void win_hide_cb(lv_event_t *e);
static void win_share_cb(lv_event_t *e);
static void star_cb(lv_event_t *e);
static void game_menu_cb(lv_event_t *e);

static const char *const DIGITS[10] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

static uint16_t g_id = STORE_NO_GAME;
static int      g_var = VAR_CLASSIC;     /* the puzzle's variant (generator.h) */
static const sudoku_geom_t *g_geo;       /* and its geometry: units, peers, size */
static sudoku_geom_t *g_geo_buf;         /* PSRAM: a Jigsaw puzzle's geometry (its regions) */
static uint8_t  g_given[81];
static board_t  g_b;
static bool     g_conflict[81];
static int      g_sel = -1;
static bool     g_notes_mode;
static bool     g_solved;
static bool     g_paused;
static uint32_t g_elapsed_ms;
static uint16_t g_mistakes;      /* digits placed that broke a rule (undo doesn't remove them) */
static uint16_t g_hints;         /* cells filled with Fill (undo doesn't remove them either) */
static uint32_t g_flags;         /* GAME_FLAG_* (store.h): the no-notes challenge, techniques the hints showed */
static lv_obj_t *g_win_badge, *g_win_share;
static uint8_t  g_sol[81];       /* the solution, for Fill */
static game_move_t *g_moves;     /* move log of this attempt (moves.h), PSRAM, GAME_MOVES_MAX */
static int      h_step;          /* 0 = no hint open, 1 = nudge shown, 2 = reason shown */
static sudoku_step_t h_info;
static int      g_practice = -1;  /* Learn: the technique being practised (LEARN_*), -1 = a real game */
static bool     g_attack;         /* this game is part of a run */
static bool     g_starred;        /* the puzzle is bookmarked (Star in the ⋮ menu) */
static bool     g_can_star;       /* ... and can be (not Learn's, nor one kept only in RAM) */
static lv_obj_t *g_title_star;    /* a small star after the title while it is starred */
static lv_obj_t *g_save_btn;      /* put it in a saved collection (generated and typed-in puzzles) */
static lv_obj_t *h_card, *h_text, *h_act;
static int      g_nmoves;
static bool     g_sol_ok;
static int64_t  g_started_at;    /* Unix time the puzzle was opened/restarted, 0 = not known yet */
static uint32_t g_started_tick;  /* lv_tick_get() then, to work it out once NTP sets the clock;
                                    0 = not this session (resumed with no known start) */
static uint32_t g_last_tick;

static board_t *g_undo;          /* UNDO_MAX boards, PSRAM (31 KB) */
static int      g_undo_head, g_undo_len;
static board_t *g_redo;          /* undone boards, newest last, PSRAM; any new edit empties it */
static int      g_redo_len;
static int      g_lock;          /* digit-first: the digit locked on the pad (long press), 0 = none */
static int g_from_coll = -1;              /* the collection the game was opened from (Next, back to the list) */

/* game screen */
static lv_obj_t *g_grid, *g_boxes[9], *g_cells[81], *g_cell_lbl[81];
static lv_obj_t *g_clues[18];                       /* Sandwich: the sums by the rows and columns */
static lv_obj_t *g_pad[9], *g_pad_cnt[9];
static int       g_layout_var = -1;     /* variant the board and pad are laid out for */
static lv_obj_t *g_title, *g_best, *g_timer, *g_timer_icon, *g_status, *g_notes_btn;
static lv_obj_t *g_pause_ovl, *g_win_ovl, *g_win_time, *g_win_best, *g_win_miss, *g_win_hint;
static lv_obj_t *g_hint_cnt;     /* hints used, on the Fill button */
static lv_obj_t *g_win_h2h;      /* Solved dialog: how others did on this puzzle */
static lv_obj_t *g_win_h2h_row;  /* ... in a row with the leader's avatar */
static lv_obj_t *g_undo_btn, *g_redo_btn;
static lv_obj_t *g_hint_btn, *g_bulb_btn, *g_timer_btn, *g_fill_btn;   /* (the tour points at them;
                                                                         g_bulb_btn: the ⋮ menu) */

/* ============================================================= sudoku rules */

/* cells a and b share a unit of the puzzle being played (row, column, box, diagonal) */
static bool is_peer(int a, int b)
{
    return sudoku_geom_peer(g_geo, a, b);
}

/* Marks every filled cell that shares a row, column, box (or diagonal) with the same digit. */
int compute_conflicts(void)
{
    int n = 0;
    memset(g_conflict, 0, sizeof(g_conflict));
    for (int i = 0; i < 81; i++) {
        if (!g_b.val[i]) continue;
        for (int j = i + 1; j < 81; j++) {
            if (g_b.val[j] == g_b.val[i] && is_peer(i, j)) {
                g_conflict[i] = g_conflict[j] = true;
            }
        }
    }
    for (int i = 0; i < 81; i++) {                  /* Even-Odd: a digit of the wrong kind */
        /* (only the marked cells: Greater-Than narrows allow[] too, from the chains of signs, for the
         * solver; a digit out of that range breaks no sign by itself until its neighbours are in) */
        if (g_b.val[i] && g_geo->parity[i] && !(g_geo->allow[i] >> g_b.val[i] & 1)) g_conflict[i] = true;
    }
    for (int i = 0; g_geo->consec_on && i < 81; i++) {   /* Consecutive: a bar and not 1 apart, or 1 apart and no bar */
        for (int k = 0; k < 2; k++) {
            int o = k ? i + 9 : i + 1, bit = k ? CONSEC_DOWN : CONSEC_RIGHT;
            if ((k == 0 && i % 9 == 8) || o > 80 || !g_b.val[i] || !g_b.val[o]) continue;
            bool near = abs(g_b.val[i] - g_b.val[o]) == 1;
            if (near != ((g_geo->consec[i] & bit) != 0)) g_conflict[i] = g_conflict[o] = true;
        }
    }
    for (int lu = 0; g_geo->sandwich && lu < 18; lu++) {   /* Sandwich: between the 1 and the 9, over the sum or short */
        int cells[9], p1 = -1, p9 = -1;
        for (int k = 0; k < 9; k++) {
            cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9);
            if (g_b.val[cells[k]] == 1) p1 = k;
            if (g_b.val[cells[k]] == 9) p9 = k;
        }
        if (p1 < 0 || p9 < 0) continue;
        int a = p1 < p9 ? p1 : p9, b = p1 < p9 ? p9 : p1, sum = 0, empty = 0;
        for (int k = a + 1; k < b; k++) {
            sum += g_b.val[cells[k]];
            empty += !g_b.val[cells[k]];
        }
        if (sum > g_geo->sandwich_sum[lu] || (!empty && sum != g_geo->sandwich_sum[lu])) {
            for (int k = a; k <= b; k++) if (g_b.val[cells[k]]) g_conflict[cells[k]] = true;
        }
    }
    for (int t = 0; t < g_geo->nthermos; t++) {     /* Thermo: two digits along it that don't rise */
        for (int i = 1; i < g_geo->thermo_n[t]; i++) {
            int a = g_geo->thermo_cell[t][i - 1], b = g_geo->thermo_cell[t][i];
            if (g_b.val[a] && g_b.val[b] && g_b.val[b] <= g_b.val[a]) g_conflict[a] = g_conflict[b] = true;
        }
    }
    for (int i = 0; g_geo->edge_kind && i < 81; i++) {   /* Kropki, XV, Greater-Than: two digits the mark (or none) rules out */
        for (int k = 0; k < 2; k++) {
            int o = k ? i + 9 : i + 1, m = k ? EDGE_DOWN(g_geo->edge[i]) : EDGE_RIGHT(g_geo->edge[i]);
            if ((k == 0 && i % 9 == 8) || o > 80 || !g_b.val[i] || !g_b.val[o]) continue;
            if (!sudoku_edge_ok(g_geo->edge_kind, m, g_b.val[i], g_b.val[o])) g_conflict[i] = g_conflict[o] = true;
        }
    }
    for (int q = 0; q < g_geo->nquads; q++) {       /* Quadruple: no room left for the circle's digits */
        int empty, at = g_geo->quad_at[q];
        if (sudoku_quad_missing(g_geo, q, g_b.val, &empty) > empty) {
            const int cells[4] = {at, at + 1, at + 9, at + 10};
            for (int k = 0; k < 4; k++) if (g_b.val[cells[k]]) g_conflict[cells[k]] = true;
        }
    }
    for (int k = 0; k < g_geo->ncages; k++) {       /* Killer: a cage over its sum, or full and short of it */
        int sum = 0, filled = 0, n = g_geo->cage_n[k];
        for (int i = 0; i < n; i++) {
            int c = g_geo->cage_cell[k][i];
            sum += g_b.val[c];
            filled += g_b.val[c] != 0;
        }
        if (sum > g_geo->cage_sum[k] || (filled == n && sum != g_geo->cage_sum[k])) {
            for (int i = 0; i < n; i++) {
                int c = g_geo->cage_cell[k][i];
                if (g_b.val[c]) g_conflict[c] = true;
            }
        }
    }
    for (int i = 0; i < 81; i++) n += g_conflict[i];
    return n;
}

/* Is cell i's digit wrong by the Mistake check setting (Settings)? Off:
 * nothing; Rules: it breaks a rule (g_conflict); Solution: also a digit
 * that isn't the solution's. Shown red; placing one buzzes. */
static bool digit_wrong(int i)
{
    if (!g_b.val[i]) return false;
    switch (store_assist()) {
    case ASSIST_OFF:
        return false;
    case ASSIST_SOLUTION:
        return g_conflict[i] || (!g_given[i] && g_sol_ok && g_b.val[i] != g_sol[i]);
    default:
        return g_conflict[i];
    }
}

/* Does the unit hold 1-n, each once? */
static bool unit_complete(int u)
{
    uint16_t seen = 0;
    for (int k = 0; k < g_geo->n; k++) {
        uint8_t v = g_b.val[g_geo->unit[u][k]];
        if (!v || (seen & (1u << v))) return false;
        seen |= (uint16_t)(1u << v);
    }
    return true;
}

/* How many of cell i's units (row, column, box, diagonals) were just completed. */
static int units_completed_by(int i)
{
    int n = 0;
    for (int k = 0; k < g_geo->ncu[i]; k++) n += unit_complete(g_geo->cu[i][k]);
    return n;
}

/* the colours a cell can be painted (1..CELL_COLORS) */
static lv_color_t cell_color(int k)
{
    static const uint32_t C[CELL_COLORS] = {0xE5484D, 0xF5A524, 0xE2D33C, 0x30A46C, 0x3E7BFA, 0x9E6CF0};
    return lv_color_hex(C[(k - 1) % CELL_COLORS]);
}

/* the cell's colour with nothing selected: X-Sudoku tints its two diagonals */
lv_color_t cell_base_bg(const sudoku_geom_t *geo, int i, bool given)
{
    lv_color_t bg = given ? C_CELL_GIVEN : C_CELL;
    if (geo->parity[i] == PARITY_EVEN) bg = lv_color_mix(C_GRID_THIN, bg, 150);   /* Even-Odd: even cells shaded */
    return sudoku_geom_tinted(geo, i) ? lv_color_mix(C_ORANGE, bg, 40) : bg;
}

static int filled_count(void)
{
    int n = 0;
    for (int i = 0; i < 81; i++) n += g_b.val[i] != 0;
    return n;
}

bool has_progress(void)
{
    for (int i = 0; i < 81; i++) {
        if ((g_b.val[i] && !g_given[i]) || g_b.notes[i] || g_b.colors[i]) return true;
    }
    return false;
}

/* Start time, worked out from uptime if the clock was set after the puzzle
 * was opened (Wi-Fi connects a few seconds after start-up). */
static int64_t game_started_at(void)
{
    time_t now = time(NULL);
    if (!g_started_at && g_started_tick && CLOCK_VALID(now)) {
        g_started_at = (int64_t)now - (int64_t)((lv_tick_get() - g_started_tick) / 1000);
    }
    return g_started_at;
}

/*
 * The game in progress is saved (to sudoku.db, via store.h) after every move,
 * when leaving the game screen and when pausing; statistics when solved.
 * stats_db only writes the newest game, so saving often costs nothing here.
 */
void save_current(void)
{
    if (g_id == STORE_NO_GAME || g_solved || g_practice >= 0 || g_attack) return;   /* (practice, time attack: not kept) */
    const saved_game_t *s = store_saved_game();
    if (!has_progress() && s->id != g_id) return;   /* only looked at it: keep the slot as is */
    saved_game_t sg;
    sg.id = g_id;
    memcpy(sg.val, g_b.val, sizeof(sg.val));
    memcpy(sg.notes, g_b.notes, sizeof(sg.notes));
    memcpy(sg.colors, g_b.colors, sizeof(sg.colors));
    sg.elapsed_ms = g_elapsed_ms;
    sg.mistakes = g_mistakes;
    sg.hints = g_hints;
    sg.flags = g_flags;
    sg.started_at = game_started_at();
    store_set_game(&sg);
    store_set_game_moves(g_moves, g_nmoves);
    store_commit_game();
}

/* ============================================================= game view */

void update_timer_label(void)
{
    char buf[16];
    if (g_attack) {                                 /* the run's time left (always shown) */
        uint32_t used = attack_used_ms() + g_elapsed_ms;
        uint32_t left = used >= ATK_MS ? 0 : ATK_MS - used;
        fmt_time(buf, sizeof(buf), (left + 999) / 1000);
        if (strcmp(lv_label_get_text(g_timer), buf) != 0) lv_label_set_text(g_timer, buf);
        lv_obj_set_style_text_color(g_timer, g_paused ? C_MUTED : (left < 60000 ? C_ORANGE : C_TEXT), 0);
        return;
    }
    fmt_time(buf, sizeof(buf), g_elapsed_ms / 1000);
    if (!store_show_timer() && !g_solved) snprintf(buf, sizeof(buf), "--:--");   /* Settings > Show timer off */
    if (strcmp(lv_label_get_text(g_timer), buf) != 0) lv_label_set_text(g_timer, buf);
    lv_obj_set_style_text_color(g_timer, g_solved ? C_GREEN : (g_paused ? C_MUTED : C_TEXT), 0);
}

void update_header(void)
{
    char name[40], best[24], buf[48];
    puzzle_name(g_id, name, sizeof(name));
    if (g_practice >= 0) snprintf(name, sizeof(name), "Practice: %s", learn_names()[g_practice]);
    if (g_attack) {
        char n2[40];
        snprintf(n2, sizeof(n2), "Time attack: %s", name);
        snprintf(name, sizeof(name), "%s", n2);
    }
    lv_label_set_text(g_title, name);
    /* the star: puzzles that stay (not Learn's, nor ones kept only in RAM) */
    bool can_star = g_practice < 0 && stats_db_ready() && g_id < PUZZLE_GEN_RAM_BASE;
    g_can_star = can_star;
    lv_obj_set_hidden(g_title_star, !(can_star && g_starred));   /* right after the title's text */
    lv_obj_update_layout(g_title);
    lv_obj_align_to(g_title_star, g_title, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    /* Save: a puzzle made on the board (not an imported one: it has its collection) */
    bool can_save = can_star && !g_attack && puzzle_is_generated(g_id) && import_of(g_id, NULL) < 0;
    bool saved = false;
    for (int k = 0; can_save && k < ui_nsaved(); k++) saved |= coll_find(PUZZLE_COLLECTION_COUNT + ui_nimports() + k, g_id) >= 0;
    lv_obj_set_hidden(g_save_btn, !can_save);
    lv_obj_set_style_text_color(lv_obj_get_child(g_save_btn, 0), saved ? C_ACCENT : C_MUTED, 0);
    uint32_t b = store_best_time(g_id);
    if (g_attack) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE "  %d solved in this run", attack_solved());
        lv_obj_set_style_text_color(g_best, C_ORANGE, 0);
    } else if (b) {
        fmt_time(best, sizeof(best), b);
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  Best %s", best);
        lv_obj_set_style_text_color(g_best, C_GREEN, 0);
    } else {
        snprintf(buf, sizeof(buf), "Not solved yet");
        lv_obj_set_style_text_color(g_best, C_MUTED, 0);
    }
    lv_label_set_text(g_best, buf);
    bool done = g_solved && !g_attack;              /* (a run's end isn't a solve: the bolt) */
    lv_label_set_text(g_timer_icon, g_attack && g_solved ? LV_SYMBOL_CHARGE
                                    : done ? LV_SYMBOL_OK : (g_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE));
    lv_obj_set_style_text_color(g_timer_icon, done ? C_GREEN : (g_attack && g_solved ? C_ORANGE : C_MUTED), 0);
    update_timer_label();
}

void refresh_board(void)
{
    int sv = g_lock ? g_lock : (g_sel >= 0 ? g_b.val[g_sel] : 0);   /* same-digit highlight */
    for (int i = 0; i < 81; i++) {
        if (!g_geo->used[i]) continue;
        lv_color_t bg = cell_base_bg(g_geo, i, g_given[i]);
        if (g_b.colors[i]) bg = lv_color_mix(cell_color(g_b.colors[i]), bg, 110);   /* painted by the player */
        if (g_solved) {
            bg = C_CELL_SOLVED;
        } else if (g_sel >= 0) {
            if (i == g_sel) bg = C_CELL_SEL;
            else if (sv && g_b.val[i] == sv) bg = C_CELL_SAME;
            else if (is_peer(i, g_sel)) bg = C_CELL_PEER;
        }
        if (h_step && !g_solved && hint_mark()[i]) {
            bg = hint_mark()[i] == 2 ? (h_info.kind == STEP_WRONG ? C_CELL_BAD : C_CELL_SEL) : C_CELL_SAME;
        }
        bool wrong = !g_solved && digit_wrong(i);
        if (wrong && !g_given[i] && i != g_sel) bg = C_CELL_BAD;
        lv_obj_set_style_bg_color(g_cells[i], bg, 0);

        lv_color_t fg = g_given[i] ? C_TEXT : C_DIGIT_USER;
        if (g_solved) fg = g_given[i] ? C_TEXT : C_GREEN;
        else if (wrong) fg = C_RED;
        lv_obj_set_style_text_color(g_cell_lbl[i], fg, 0);

        const char *t = DIGITS[g_b.val[i]];
        if (strcmp(lv_label_get_text(g_cell_lbl[i]), t) != 0) lv_label_set_text_static(g_cell_lbl[i], t);
        lv_obj_invalidate(g_cells[i]);   /* notes are custom-drawn */
    }

    /* number pad: digits left to place; a finished digit is dimmed */
    int count[10] = {0};
    for (int i = 0; i < 81; i++) count[g_b.val[i]]++;
    for (int d = 1; d <= g_geo->n; d++) {
        int left = g_geo->n - count[d];
        lv_label_set_text_fmt(g_pad_cnt[d - 1], "%d", left > 0 ? left : 0);
        lv_obj_set_style_opa(g_pad[d - 1], left <= 0 && d != g_lock ? LV_OPA_40 : LV_OPA_COVER, 0);
        bool locked = d == g_lock;                  /* digit-first: the locked digit is accent blue */
        lv_obj_set_state(g_pad[d - 1], LV_STATE_CHECKED, locked);
        lv_obj_set_style_text_color(lv_obj_get_child(g_pad[d - 1], 0), locked ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(g_pad_cnt[d - 1], locked ? lv_color_white() : C_MUTED, 0);
    }
    lv_obj_set_style_opa(g_undo_btn, g_undo_len ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(g_redo_btn, g_redo_len ? LV_OPA_COVER : LV_OPA_40, 0);

    if (g_hints) lv_label_set_text_fmt(g_hint_cnt, "%u", (unsigned)g_hints);
    lv_obj_set_hidden(g_hint_cnt, g_hints == 0);

    /* status line */
    int bad = 0;
    for (int i = 0; i < 81; i++) bad += digit_wrong(i);
    bool full = filled_count() == g_geo->ncells;
    if (g_solved) {
        char t[16];
        fmt_time(t, sizeof(t), g_elapsed_ms / 1000);
        lv_label_set_text_fmt(g_status, LV_SYMBOL_OK "  Solved in %s", t);
        lv_obj_set_style_text_color(g_status, C_GREEN, 0);
    } else if (bad) {
        lv_label_set_text_fmt(g_status, LV_SYMBOL_WARNING "  %d %s", bad,
                              store_assist() == ASSIST_SOLUTION ? "wrong" : "conflicting");
        lv_obj_set_style_text_color(g_status, C_RED, 0);
    } else if (full) {                              /* Mistake check off: something is wrong somewhere */
        lv_label_set_text(g_status, "All filled, not solved yet");
        lv_obj_set_style_text_color(g_status, C_ORANGE, 0);
    } else if (g_lock) {
        lv_label_set_text_fmt(g_status, "Placing %d: tap cells", g_lock);
        lv_obj_set_style_text_color(g_status, C_ACCENT, 0);
    } else {
        lv_label_set_text_fmt(g_status, "Filled %d / %d", filled_count(), g_geo->ncells);
        lv_obj_set_style_text_color(g_status, C_MUTED, 0);
    }
}

static uint16_t s_solve_id;
static uint32_t s_solve_secs, s_solve_mistakes, s_solve_hints, s_solve_flags;
static int64_t  s_solve_started;

static void solve_commit_cb(lv_timer_t *t)
{
    store_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes);
    stats_db_record_solve(s_solve_id, s_solve_secs, s_solve_mistakes, s_solve_hints,
                          s_solve_started, g_moves, g_nmoves, s_solve_flags);   /* finished: now */
    /* head-to-head: where this leaves the player among everyone who solved it (after the solve above) */
    stats_h2h_t h;
    char line[96] = "";
    if (stats_db_ready() && stats_db_puzzle_rank(s_solve_id, &h) && h.rank) {
        char t[16];
        fmt_time(t, sizeof(t), h.leader_seconds);
        if (!h.leader[0]) snprintf(line, sizeof(line), "Nobody else has solved this one yet");
        else if (h.second) snprintf(line, sizeof(line), SYMBOL_STAR "  1st of %d: ahead of %s (%s)", h.of, h.leader, t);
        else snprintf(line, sizeof(line), "%d%s of %d   -   %s leads with %s", h.rank, ordinal(h.rank), h.of, h.leader, t);
    }
    lv_label_set_text(g_win_h2h, line);
    while (lv_obj_get_child_count(g_win_h2h_row) > 1) lv_obj_delete(lv_obj_get_child(g_win_h2h_row, 0));
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

static const uint32_t CF_COLORS[] = {0xE53935, 0xFB8C00, 0xFDD835, 0x43A047, 0x1E88E5, 0x8E24AA, 0xEC407A, 0x00ACC1};
static cf_piece_t *cf_p;
static lv_obj_t *cf_ovl;
static lv_timer_t *cf_timer;
static uint32_t cf_t0, cf_last;
static void (*cf_done)(void);

static float cf_rand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}

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
        if (p->y > SCR_W + 30 || p->x < -30 || p->x > SCR_W + 30) continue;
        int32_t w = (int32_t)(p->w * fabsf(cosf(p->spin))) + 2;   /* turning over */
        int32_t x = (int32_t)(p->x + 10.0f * sinf(p->sway)), y = (int32_t)p->y;
        lv_area_t a = {x - w / 2, y - p->h / 2, x + (w - 1) / 2, y + (p->h - 1) / 2};
        d.bg_color = lv_color_hex(CF_COLORS[p->color]);
        lv_draw_rect(layer, &d, &a);
    }
}

static void cf_stop(void)
{
    if (cf_timer) lv_timer_delete(cf_timer);
    cf_timer = NULL;
    if (cf_ovl) lv_obj_delete(cf_ovl);
    cf_ovl = NULL;
    free(cf_p);
    cf_p = NULL;
}

static void cf_tick(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    float dt = (float)lv_tick_diff(now, cf_last) / 1000.0f;
    cf_last = now;
    if (dt > 0.1f) dt = 0.1f;                       /* (a slow frame doesn't throw them) */
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
        if (done) done();
    }
}

/* starts the confetti; done runs when it's over (at once if it can't start) */
static void confetti(void (*done)(void))
{
    cf_stop();
    cf_p = malloc(sizeof(cf_piece_t) * CF_N);
    if (!cf_p) {
        if (done) done();
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

/* the Solved! dialog, after the confetti; the solve is saved once it's up */
static void win_show(void)
{
    if (lv_screen_active() == screen_game() && g_solved) {   /* (the dialog only where it belongs) */
        lv_obj_set_hidden(g_pause_ovl, true);
        lv_obj_set_hidden(g_win_ovl, false);
    }
    lv_timer_t *ct = lv_timer_create(solve_commit_cb, SAVE_DELAY_MS, NULL);
    lv_timer_set_repeat_count(ct, 1);
}

static void on_solved(void)
{
    g_solved = true;
    g_lock = 0;
    g_paused = false;
    sfx_play(SFX_SOLVED);
    g_sel = -1;
    if (g_practice >= 0) {                          /* Learn: not recorded */
        sudoku_ui_notice("Solved! Practice games aren't saved or counted: Learn has more techniques.", 4000);
        update_header();
        return;
    }
    if (g_attack) {                                 /* Time attack: counted, and straight on to the next */
        uint32_t secs = (g_elapsed_ms + 500) / 1000;
        store_record_solve(g_id, secs, g_mistakes);
        stats_db_record_solve(g_id, secs, g_mistakes, g_hints, game_started_at(), g_moves, g_nmoves, g_flags);
        attack_set_used_ms(attack_used_ms() + (g_elapsed_ms));
        g_elapsed_ms = 0;
        attack_set_solved(attack_solved() + 1);
        attack_set_mistakes(attack_mistakes() + (g_mistakes));
        char msg[64];
        snprintf(msg, sizeof(msg), "Solved! %d in this run. Next one...", attack_solved());
        sudoku_ui_notice(msg, 1500);
        update_header();
        lv_timer_t *nt = lv_timer_create((lv_timer_cb_t)atk_open_next, 900, NULL);
        lv_timer_set_repeat_count(nt, 1);
        return;
    }
    uint32_t secs = (g_elapsed_ms + 500) / 1000;
    uint32_t prev = store_best_time(g_id);
    uint32_t rec = secs ? (secs > 0xFFFF ? 0xFFFF : secs) : 1;   /* as store_record_solve() stores it */
    bool best = !prev || rec < prev;
    s_solve_id = g_id;
    s_solve_secs = secs;
    s_solve_mistakes = g_mistakes;
    s_solve_hints = g_hints;
    s_solve_flags = g_flags;
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
    if (g_mistakes == 0) {
        lv_label_set_text(g_win_miss, "No mistakes");
        lv_obj_set_style_text_color(g_win_miss, C_GREEN, 0);
    } else {
        lv_label_set_text_fmt(g_win_miss, "%u mistake%s", (unsigned)g_mistakes, g_mistakes == 1 ? "" : "s");
        lv_obj_set_style_text_color(g_win_miss, C_MUTED, 0);
    }
    lv_label_set_text(g_win_h2h, "");                 /* (solve_commit_cb fills it in) */
    while (lv_obj_get_child_count(g_win_h2h_row) > 1) lv_obj_delete(lv_obj_get_child(g_win_h2h_row, 0));
    if (g_hints == 0) {
        lv_label_set_text(g_win_hint, "No hints");
        lv_obj_set_style_text_color(g_win_hint, C_GREEN, 0);
    } else {
        lv_label_set_text_fmt(g_win_hint, "%u hint%s used", (unsigned)g_hints, g_hints == 1 ? "" : "s");
        lv_obj_set_style_text_color(g_win_hint, C_ORANGE, 0);
    }
    lv_obj_set_hidden(g_win_badge, !(g_flags & GAME_FLAG_NO_NOTES));
    char day[12] = "";                              /* a daily puzzle: its result can be shared */
    lv_obj_set_hidden(g_win_share, !(puzzle_is_generated(g_id) && gen_lookup_day(g_id, NULL, NULL, NULL, day) &&
                                     day[0]));
    if (!stats_db_ready()) {
        sudoku_ui_notice("No sudoku.db on a microSD card: this result is kept only until the board is "
                         "switched off.", 6000);
    }
    lv_obj_set_hidden(g_pause_ovl, true);
    update_header();
    confetti(win_show);                             /* then the dialog (win_show) */
}

static void after_move(void)
{
    hint_drop();                                    /* the board changed: an open hint no longer fits */
    compute_conflicts();
    bool full = filled_count() == g_geo->ncells;
    bool ok = true;
    for (int i = 0; i < 81; i++) ok &= !g_conflict[i];
    if (full && ok) on_solved();
    refresh_board();
    save_current();                                 /* every move (not once solved) */
}

static void push_undo_keep_redo(void)
{
    g_undo[g_undo_head] = g_b;
    g_undo_head = (g_undo_head + 1) % UNDO_MAX;
    if (g_undo_len < UNDO_MAX) g_undo_len++;
}

/* before every edit: the board goes on the undo ring, and redo is gone */
static void push_undo(void)
{
    push_undo_keep_redo();
    g_redo_len = 0;
}

static bool can_edit(void)
{
    return g_sel >= 0 && !g_given[g_sel] && !g_solved && !g_paused;
}

/* one entry in the move log (a cell's digit changed) */
static void record_move(int cell, int digit, move_kind_t kind)
{
    if (!g_moves) g_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    if (!g_moves || g_nmoves >= GAME_MOVES_MAX) return;
    g_moves[g_nmoves++] = (game_move_t){.t_ms = g_elapsed_ms, .cell = (uint8_t)cell, .digit = (uint8_t)digit,
                                        .kind = (uint8_t)kind};
}

void enter_digit(int d)
{
    if (!can_edit()) return;
    int i = g_sel;
    bool placed = false;
    if (g_notes_mode) {
        if (g_b.val[i]) return;
        push_undo();
        g_b.notes[i] ^= (uint16_t)(1u << d);
    } else {
        push_undo();
        if (g_b.val[i] == d) {
            g_b.val[i] = 0;
            record_move(i, 0, MOVE_HAND);
        } else {
            place_digit(i, d);
            record_move(i, d, MOVE_HAND);
            placed = true;
        }
    }
    after_move();
    if (!placed || g_solved) return;          /* on_solved() plays its own sound */

    /* a mistake: breaks a rule (with Mistake check on Solution: or isn't the
     * solution's digit). Counted even with the check off, just not shown. */
    bool wrong = g_conflict[i] || (store_assist() == ASSIST_SOLUTION && g_sol_ok && d != g_sol[i]);
    if (wrong) {
        if (g_mistakes < 0xFFFF) g_mistakes++;
        if (store_assist() != ASSIST_OFF) sfx_play(SFX_ERROR);
        return;
    }
    placed_sounds(i, d);
}

/* Fill: the selected cell gets its digit from the solution; counts as a hint */
static void fill_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    if (g_attack) {
        sudoku_ui_notice("Fill and Hint are off in a time attack.", 2500);
        return;
    }
    if (!g_sol_ok) {
        sudoku_ui_notice("Fill isn't available: this puzzle has no solution.", 3000);
        return;
    }
    if (g_sel < 0 || g_given[g_sel]) {
        sudoku_ui_notice("Tap an empty cell first, then Fill.", 2500);
        return;
    }
    int i = g_sel, d = g_sol[i];
    if (g_b.val[i] == d) {
        sudoku_ui_notice("That cell is already right.", 2000);
        return;
    }
    push_undo();
    place_digit(i, d);
    record_move(i, d, MOVE_FILL);
    if (g_hints < 0xFFFF) g_hints++;
    after_move();
    if (!g_solved) placed_sounds(i, d);
}

/* a digit placed in cell i: its notes go, and peers lose that note */
static void place_digit(int i, int d)
{
    g_b.val[i] = (uint8_t)d;
    g_b.notes[i] = 0;
    for (int j = 0; j < 81; j++) {
        if (j != i && is_peer(i, j)) g_b.notes[j] &= (uint16_t)~(1u << d);
    }
}

/* chimes for a digit d placed in cell i without a conflict */
static void placed_sounds(int i, int d)
{
    if (g_conflict[i]) return;
    int n = 0;
    bool clean = true;
    for (int j = 0; j < 81; j++) {
        if (g_b.val[j] == d) {
            n++;
            clean &= !g_conflict[j];
        }
    }
    if (n == g_geo->n && clean) sfx_play(SFX_DIGIT_DONE);
    int units = units_completed_by(i);              /* row / column / box / diagonal now complete */
    if (units) sfx_play(units > 1 ? SFX_UNITS_DONE : SFX_UNIT_DONE);
}

static void cell_click_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    g_sel = i;
    if (g_lock && !g_given[i]) {                    /* digit-first: the locked digit goes in (or its note) */
        enter_digit(g_lock);
        return;
    }
    refresh_board();
}

/* ---- the cell menu (long press a cell): paint it, and on Killer the cage's digit sets */
static lv_obj_t *cm_ovl;
static int cm_cell;

static void cm_close(void)
{
    if (cm_ovl) lv_obj_delete_async(cm_ovl);
    cm_ovl = NULL;
}

static void cm_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) cm_close();
}

static void cm_color_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    int i = cm_cell;
    cm_close();
    if (g_solved || g_paused || i < 0 || g_b.colors[i] == k) return;
    push_undo();
    g_b.colors[i] = (uint8_t)k;
    after_move();                                   /* redraws and saves (colours aren't in the move log) */
}

/* the sets of different digits that make up a cage's sum and hold the digits
 * already in it, as text ("1+5+9   2+4+9 ..."); how many sets */
static int cage_sets_text(int k, char *buf, size_t n)
{
    int size = g_geo->cage_n[k], sum = g_geo->cage_sum[k], count = 0;
    uint16_t have = 0;
    for (int j = 0; j < size; j++) {
        int v = g_b.val[g_geo->cage_cell[k][j]];
        if (v) have |= (uint16_t)(1u << v);
    }
    size_t len = 0;
    buf[0] = 0;
    for (uint16_t m = 2; m < 0x400; m += 2) {       /* digit sets as bits 1..9 */
        if (__builtin_popcount(m) != size || (m & have) != have) continue;
        int s = 0;
        for (int d = 1; d <= 9; d++) s += (m >> d & 1) * d;
        if (s != sum) continue;
        count++;
        if (len + 24 >= n) continue;                /* (the count still goes on) */
        len += snprintf(buf + len, n - len, "%s", count > 1 ? "     " : "");
        bool first = true;
        for (int d = 1; d <= 9; d++) {
            if (!(m >> d & 1)) continue;
            len += snprintf(buf + len, n - len, first ? "%d" : "+%d", d);
            first = false;
        }
    }
    return count;
}

static void cell_long_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    cm_close();
    cm_cell = i;
    cm_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(cm_ovl);
    lv_obj_set_size(cm_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cm_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cm_ovl, LV_OPA_30, 0);
    lv_obj_set_clickable(cm_ovl, true);
    lv_obj_add_event_cb(cm_ovl, cm_backdrop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *card = lv_obj_create(cm_ovl);
    lv_obj_set_size(card, 520, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_scrollable(card, false);
    /* above the cell's row when it's low on the board, else below it */
    lv_area_t co;
    lv_obj_get_coords(g_cells[i], &co);
    lv_obj_align(card, co.y1 > SCR_W / 2 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, co.y1 > SCR_W / 2 ? 70 : -20);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text_fmt(t, "Row %d, column %d: colour", i / 9 + 1, i % 9 + 1);
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    for (int k = 0; k <= CELL_COLORS; k++) {        /* none, then the colours */
        lv_obj_t *b = make_btn(row, 0, 0, 58, 56, k ? NULL : LV_SYMBOL_CLOSE, &lv_font_montserrat_20, cm_color_cb,
                               (void *)(intptr_t)k);
        if (k) lv_obj_set_style_bg_color(b, cell_color(k), 0);
        else lv_obj_set_style_bg_color(b, C_CARD2, 0);
        if (g_b.colors[i] == k) {
            lv_obj_set_style_border_color(b, C_TEXT, 0);
            lv_obj_set_style_border_width(b, 3, 0);
        }
    }
    int k = g_geo->cage_of[i];
    if (k != CAGE_NONE) {                           /* Killer helper */
        char sets[240];
        int n = cage_sets_text(k, sets, sizeof(sets));
        lv_obj_t *h = make_label(card, &lv_font_montserrat_16, C_MUTED);
        lv_label_set_text_fmt(h, "Cage: %d in %d cells, %d way%s%s", g_geo->cage_sum[k], g_geo->cage_n[k], n,
                              n == 1 ? "" : "s", n ? ":" : " (check the digits in it)");
        if (n) {
            lv_obj_t *l = make_label(card, &lv_font_montserrat_20, C_TEXT);
            lv_obj_set_width(l, LV_PCT(100));
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
            lv_label_set_text(l, sets);
        }
    }
}

static void cell_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_b.val[i] || !g_b.notes[i]) return;
    lv_obj_t *cell = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(cell, &co);
    int sv = g_lock ? g_lock : (g_sel >= 0 ? g_b.val[g_sel] : 0);

    /* three marks per row: 3 rows on a 9x9 board, 2 (centred) on a 6x6 one; 4x4: two by two */
    const int cols = g_geo->n == 4 ? 2 : 3;
    int size = lv_area_get_width(&co), pitch = size / cols, rows = (g_geo->n + cols - 1) / cols;
    int x0 = 1, y0 = rows == 3 ? 2 : (size - rows * pitch) / 2;
    int vpitch = pitch;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    const bool big = store_big_digits();            /* (Digit size: Large) */
    d.font = size >= 80 ? (big ? &lv_font_montserrat_24 : &lv_font_montserrat_22)
                        : (big ? &lv_font_montserrat_18 : &lv_font_montserrat_16);
    if (g_geo->ncages) {                            /* Killer: smaller, below the cage sum in the corner */
        d.font = &lv_font_montserrat_12;
        y0 = 13;
        vpitch = (size - 15) / 3;
    }
    d.align = LV_TEXT_ALIGN_CENTER;
    for (int n = 1; n <= g_geo->n; n++) {
        if (!(g_b.notes[i] & (1u << n))) continue;
        int r = (n - 1) / cols, c = (n - 1) % cols;
        lv_area_t a = {
            .x1 = co.x1 + x0 + c * pitch, .y1 = co.y1 + y0 + r * vpitch,
            .x2 = co.x1 + x0 + c * pitch + pitch - 1, .y2 = co.y1 + y0 + r * vpitch + vpitch - 1,
        };
        d.color = n == sv ? C_ACCENT : C_MUTED;
        d.text = DIGITS[n];
        lv_draw_label(layer, &d, &a);
    }
}

/* A short tap enters the digit in the selected cell; while a digit is
 * locked (digit-first) it moves the lock to that digit, or unlocks it. */
static void pad_click_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_lock) {
        g_lock = g_lock == d ? 0 : d;
        refresh_board();
        return;
    }
    enter_digit(d);
}

/* A long press locks the digit: then each tap on a cell places it there. */
static void pad_long_cb(lv_event_t *e)
{
    if (g_solved || g_paused) return;
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    g_lock = g_lock == d ? 0 : d;
    refresh_board();
}

/* Auto notes: every empty cell gets all the digits that still fit there as
 * pencil marks (replacing its notes). Undo takes it back. */
static void auto_notes_cb(lv_event_t *e)
{
    if (g_solved || g_paused || (g_flags & GAME_FLAG_NO_NOTES)) return;
    uint16_t notes[81];
    int changed = 0;
    for (int k = 0; k < g_geo->ncells; k++) {
        int c = g_geo->cells[k];
        notes[c] = g_b.notes[c];
        if (g_b.val[c]) continue;
        uint16_t m = g_geo->all;
        for (int p = 0; p < g_geo->npeers[c]; p++) m &= (uint16_t)~(1u << g_b.val[g_geo->peers[c][p]]);
        notes[c] = m;
        changed += m != g_b.notes[c];
    }
    if (!changed) {
        sudoku_ui_notice("The notes already show every digit that fits.", 2500);
        return;
    }
    push_undo();
    for (int k = 0; k < g_geo->ncells; k++) g_b.notes[g_geo->cells[k]] = notes[g_geo->cells[k]];
    after_move();
}

static void erase_cb(lv_event_t *e)
{
    if (!can_edit()) return;
    if (!g_b.val[g_sel] && !g_b.notes[g_sel]) return;
    push_undo();
    if (g_b.val[g_sel]) record_move(g_sel, 0, MOVE_ERASE);
    g_b.val[g_sel] = 0;
    g_b.notes[g_sel] = 0;
    after_move();
}

static void undo_cb(lv_event_t *e)
{
    if (g_solved || g_paused || g_undo_len == 0) return;
    if (g_redo_len == UNDO_MAX) {                   /* full: the oldest undone board goes */
        memmove(g_redo, g_redo + 1, sizeof(board_t) * (UNDO_MAX - 1));
        g_redo_len--;
    }
    g_redo[g_redo_len++] = g_b;                     /* for Redo */
    g_undo_head = (g_undo_head + UNDO_MAX - 1) % UNDO_MAX;
    g_undo_len--;
    uint8_t before[81];
    memcpy(before, g_b.val, sizeof(before));
    g_b = g_undo[g_undo_head];
    for (int i = 0; i < 81; i++) {
        if (g_b.val[i] != before[i]) record_move(i, g_b.val[i], MOVE_UNDO);
    }
    after_move();
}

/* puts back what the last Undo took away (until the next edit) */
static void redo_cb(lv_event_t *e)
{
    if (g_solved || g_paused || g_redo_len == 0) return;
    push_undo_keep_redo();
    uint8_t before[81];
    memcpy(before, g_b.val, sizeof(before));
    g_b = g_redo[--g_redo_len];
    for (int i = 0; i < 81; i++) {
        if (g_b.val[i] != before[i]) record_move(i, g_b.val[i], MOVE_REDO);
    }
    after_move();
}

static void notes_cb(lv_event_t *e)
{
    if (g_flags & GAME_FLAG_NO_NOTES) {             /* (the button is off in the challenge anyway) */
        lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
        return;
    }
    g_notes_mode = lv_obj_has_state(g_notes_btn, LV_STATE_CHECKED);
}

/* the no-notes challenge: Notes and Auto off (dimmed) for this game */
void challenge_ui(void)
{
    bool on = (g_flags & GAME_FLAG_NO_NOTES) != 0;
    if (on) {
        g_notes_mode = false;
        lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
    }
    lv_obj_set_state(g_notes_btn, LV_STATE_DISABLED, on);   /* (Auto notes: dimmed in the ⋮ menu) */
}

void set_paused(bool p)
{
    if (g_solved) p = false;
    if (p) {
        hint_drop();
        refresh_board();
    }
    g_paused = p;
    lv_obj_set_hidden(g_pause_ovl, !p);
    update_header();
    if (p) save_current();
}

/* ------------------------------------------------------------- build */

void build_game(void)
{
    screen_set_game(make_screen());

    /* top bar */
    make_btn(screen_game(), 8, 6, 104, 48, LV_SYMBOL_LEFT "  List", &lv_font_montserrat_18, back_cb, NULL);
    g_title = make_label(screen_game(), &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(g_title, 126, 6);
    /* as wide as its text, up to the right edge less room for the star after it; one line
     * ("Daily X-Sudoku Medium, 18 Nov" fits; longer ends in ...) */
    lv_obj_set_size(g_title, LV_SIZE_CONTENT, lv_font_get_line_height(&lv_font_montserrat_22));
    lv_obj_set_style_max_width(g_title, SCR_W - 8 - 126 - 30, 0);
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_MODE_DOTS);
    g_title_star = make_label(screen_game(), &font_icons_20, C_ORANGE);
    lv_label_set_text(g_title_star, SYMBOL_STAR);
    lv_obj_set_hidden(g_title_star, true);
    /* second row: icons for the ⋮ menu (rules, Share, Star, Auto notes, Restart), Hint, Undo, Redo,
     * Erase and Notes, then Fill and Save; the pause button and timer on the right. (No bottom bar:
     * the board goes down to the bottom edge.) */
    const int ib = 52, ig = 8;                      /* icon buttons, and the gap between them */
    int tx = 8;
    lv_obj_t *qb = make_btn(screen_game(), tx, TOOL_Y, ib, 48, SYMBOL_MORE, &font_icons_24, game_menu_cb, NULL);
    g_bulb_btn = qb;
    g_best = make_label(screen_game(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(g_best, 126, 36);
    tx += ib + ig;
    lv_obj_t *hb = make_btn(screen_game(), tx, TOOL_Y, ib, 48, LV_SYMBOL_EYE_OPEN, &font_icons_24, hint_cb, NULL);
    lv_obj_set_style_text_color(lv_obj_get_child(hb, 0), C_ACCENT, 0);
    g_hint_btn = hb;
    tx += ib + ig;
    g_undo_btn = make_btn(screen_game(), tx, TOOL_Y, ib, 48, SYMBOL_UNDO, &font_icons_24, undo_cb, NULL);
    tx += ib + ig;
    g_redo_btn = make_btn(screen_game(), tx, TOOL_Y, ib, 48, SYMBOL_REDO, &font_icons_24, redo_cb, NULL);
    tx += ib + ig;
    make_btn(screen_game(), tx, TOOL_Y, ib, 48, LV_SYMBOL_BACKSPACE, &font_icons_24, erase_cb, NULL);
    tx += ib + ig;
    g_notes_btn = make_btn(screen_game(), tx, TOOL_Y, ib, 48, LV_SYMBOL_EDIT, &font_icons_24, NULL, NULL);
    lv_obj_set_checkable(g_notes_btn, true);
    lv_obj_add_event_cb(g_notes_btn, notes_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_opa(g_notes_btn, LV_OPA_40, LV_STATE_DISABLED);   /* (the no-notes challenge) */
    /* the pencil white on the accent while on (the label takes the button's colour) */
    lv_obj_remove_local_style_prop(lv_obj_get_child(g_notes_btn, 0), LV_STYLE_TEXT_COLOR, 0);
    lv_obj_set_style_text_color(g_notes_btn, C_TEXT, 0);
    lv_obj_set_style_text_color(g_notes_btn, lv_color_white(), LV_STATE_CHECKED);
    tx += ib + ig;
    lv_obj_t *fb = make_btn(screen_game(), tx, TOOL_Y, 96, 48, LV_SYMBOL_OK "  Fill", &lv_font_montserrat_16,
                            fill_cb, NULL);
    g_fill_btn = fb;
    g_hint_cnt = make_label(fb, &lv_font_montserrat_12, C_ORANGE);     /* hints used */
    lv_obj_align(g_hint_cnt, LV_ALIGN_TOP_RIGHT, -5, 2);
    lv_obj_set_hidden(g_hint_cnt, true);
    tx += 96 + ig;
    g_save_btn = make_btn(screen_game(), tx, TOOL_Y, ib, 48, LV_SYMBOL_SAVE,
                          &lv_font_montserrat_22, save_cb, NULL);   /* into a collection (Puzzles > Saved) */
    lv_obj_t *tb = make_btn(screen_game(), SCR_W - 8 - 176, TOOL_Y, 176, 48, NULL, NULL, pause_cb, NULL);
    lv_obj_set_style_bg_opa(tb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, LV_STATE_PRESSED);
    g_timer_btn = tb;
    g_timer_icon = make_label(tb, &lv_font_montserrat_20, C_MUTED);
    lv_obj_align(g_timer_icon, LV_ALIGN_LEFT_MID, 12, 0);
    g_timer = make_label(tb, &font_timer_36, C_TEXT);
    lv_obj_align(g_timer, LV_ALIGN_RIGHT_MID, -6, 0);

    /* board: thick-line background, the boxes in thin-line colour, the cells
     * (placed by game_layout() for the puzzle's variant) */
    lv_obj_t *grid = lv_obj_create(screen_game());
    g_grid = grid;
    lv_obj_remove_style_all(grid);
    lv_obj_add_event_cb(grid, board_regions_draw_cb, LV_EVENT_DRAW_POST_END, NULL);   /* Jigsaw borders */
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    for (int b = 0; b < 9; b++) {
        lv_obj_t *box = lv_obj_create(grid);
        lv_obj_remove_style_all(box);
        lv_obj_set_style_bg_color(box, C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(box, false);
        lv_obj_set_clickable(box, false);
        g_boxes[b] = box;
    }
    for (int i = 0; i < 18; i++) {
        g_clues[i] = make_label(screen_game(), &lv_font_montserrat_20, C_ACCENT);
        lv_obj_set_style_text_align(g_clues[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_hidden(g_clues[i], true);
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_clickable(cell, true);
        lv_obj_set_scrollable(cell, false);
        lv_obj_add_event_cb(cell, cell_click_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cell, cell_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);   /* colours, cage sums */
        lv_obj_add_event_cb(cell, cell_draw_cb, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)i);
        lv_obj_t *l = make_label(cell, DIGIT_FONT, C_TEXT);
        lv_obj_center(l);
        g_cells[i] = cell;
        g_cell_lbl[i] = l;
    }

    /* pause cover */
    g_pause_ovl = lv_obj_create(screen_game());
    lv_obj_remove_style_all(g_pause_ovl);
    lv_obj_set_pos(g_pause_ovl, GRID_X, GRID_Y);
    lv_obj_set_size(g_pause_ovl, GAME_GRID_SIZE, GAME_GRID_SIZE);
    lv_obj_set_style_bg_color(g_pause_ovl, C_CARD, 0);
    lv_obj_set_style_bg_opa(g_pause_ovl, LV_OPA_COVER, 0);
    lv_obj_set_clickable(g_pause_ovl, true);
    lv_obj_set_hidden(g_pause_ovl, true);
    lv_obj_add_event_cb(g_pause_ovl, pause_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = make_label(g_pause_ovl, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(pl, LV_SYMBOL_PAUSE "  Paused");
    lv_obj_align(pl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t *pl2 = make_label(g_pause_ovl, &lv_font_montserrat_18, C_MUTED);
    lv_label_set_text(pl2, "Tap to resume");
    lv_obj_align(pl2, LV_ALIGN_CENTER, 0, 24);

    /* number pad */
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *b = make_btn(screen_game(), GAME_PAD_X, GRID_Y + 1 + (d - 1) * (PAD_H + PAD_GAP), GAME_PAD_W, PAD_H,
                               DIGITS[d], &lv_font_montserrat_28, NULL, NULL);
        lv_obj_add_event_cb(b, pad_click_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)d);
        lv_obj_add_event_cb(b, pad_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)d);   /* lock it */
        g_pad[d - 1] = b;
        g_pad_cnt[d - 1] = make_label(b, &lv_font_montserrat_12, C_MUTED);
        lv_obj_align(g_pad_cnt[d - 1], LV_ALIGN_TOP_RIGHT, -6, 4);
    }

    /* status: top bar, right-aligned on the line under the title, up to the Hint button */
    g_status = make_label(screen_game(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(g_status, SCR_W - 8 - 126);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_status, 126, 36);

    /* hint card: text, then More / Place it, and close (moved to the top by hint_render if needed) */
    h_card = lv_obj_create(screen_game());
    lv_obj_set_size(h_card, SCR_W - 16, 92);
    lv_obj_set_pos(h_card, 8, SCR_W - 96);
    lv_obj_set_style_bg_color(h_card, C_CARD, 0);
    lv_obj_set_style_border_color(h_card, C_ACCENT, 0);
    lv_obj_set_style_border_width(h_card, 2, 0);
    lv_obj_set_style_radius(h_card, 12, 0);
    lv_obj_set_style_pad_all(h_card, 10, 0);
    lv_obj_set_scrollable(h_card, false);
    h_text = make_label(h_card, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(h_text, SCR_W - 16 - 20 - 130 - 66);
    lv_label_set_long_mode(h_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(h_text, LV_ALIGN_LEFT_MID, 2, 0);
    h_act = make_btn(h_card, 0, 0, 122, 64, "More", &lv_font_montserrat_18, hint_act_cb, NULL);
    lv_obj_set_style_bg_color(h_act, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(h_act, 0), lv_color_white(), 0);
    lv_obj_align(h_act, LV_ALIGN_RIGHT_MID, -66, 0);
    lv_obj_t *hx = make_btn(h_card, 0, 0, 60, 64, LV_SYMBOL_CLOSE, &lv_font_montserrat_22, hint_close_cb, NULL);
    lv_obj_set_style_bg_color(hx, C_CARD2, 0);
    lv_obj_align(hx, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_hidden(h_card, true);
    h_step = 0;

    /* solved dialog */
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

    g_layout_var = -1;                              /* new widgets: lay them out for the current puzzle */
    game_layout();
}

static void hint_act_cb(lv_event_t *e)
{
    if (!h_step || g_solved || g_paused) return;
    if (h_step == 1 && hint_nudge()) return;
    if (h_step == 1) {
        h_step = 2;
        hint_render();
        return;
    }
    int i = h_info.cell, d = h_info.digit;
    bool wrong = h_info.kind == STEP_WRONG;
    push_undo();
    if (wrong) {
        record_move(i, 0, MOVE_ERASE);
        g_b.val[i] = 0;
    } else {
        place_digit(i, d);
        record_move(i, d, MOVE_FILL);
        if (g_practice >= 0) stats_db_learned(g_practice);   /* Learn: this technique practised (Graduate) */
    }
    g_sel = i;
    after_move();                                   /* closes the hint */
    if (!wrong && !g_solved) placed_sounds(i, d);
}

static void hint_close_cb(lv_event_t *e)
{
    hint_drop();
    refresh_board();
}

static void hint_cb(lv_event_t *e)
{
    if (g_solved || g_paused || !g_sol_ok) return;
    if (g_attack) {
        sudoku_ui_notice("Fill and Hint are off in a time attack.", 2500);
        return;
    }
    if (h_step) {                                   /* already open: same as More / Place it */
        if (hint_nudge()) return;                   /* (a nudge goes no further) */
        hint_act_cb(e);
        return;
    }
    if (!sudoku_next_step(g_geo, g_b.val, g_sol, &h_info) || h_info.cell < 0) return;
    if (g_hints < 0xFFFF) g_hints++;               /* every hint opened counts once */
    if (h_info.tech.kind > 0 && h_info.tech.kind < 16) g_flags |= 1u << h_info.tech.kind;   /* (achievements) */
    refresh_board();                                /* the hint count on the Fill button */
    save_current();
    h_step = 1;
    hint_render();
}

static void pause_cb(lv_event_t *e)
{
    set_paused(!g_paused);
}

/* a cell\'s digit, set directly (Learn: the lesson\'s position) */
void game_put_value(int i, uint8_t v)
{
    g_b.val[i] = v;
}

void reset_game(void)
{
    memcpy(g_b.val, g_given, sizeof(g_b.val));
    memset(g_b.notes, 0, sizeof(g_b.notes));
    memset(g_b.colors, 0, sizeof(g_b.colors));
    g_elapsed_ms = 0;
    g_mistakes = 0;
    g_hints = 0;
    g_flags = store_no_notes() ? GAME_FLAG_NO_NOTES : 0;   /* (Settings > Game: the no-notes challenge) */
    g_nmoves = 0;                                   /* a new attempt, a new log */
    hint_drop();
    mark_started();                                 /* opened or restarted: a new attempt */
    g_undo_len = 0;
    g_undo_head = 0;
    g_redo_len = 0;
    g_lock = 0;
    g_sel = -1;
    g_solved = false;
    lv_obj_set_hidden(g_win_ovl, true);
    set_paused(false);
    compute_conflicts();
    refresh_board();
}

static void win_next_cb(lv_event_t *e)
{
    if (g_from_coll >= PUZZLE_COLLECTION_COUNT && coll_saved_index(g_from_coll) >= 0) {   /* a saved collection */
        int i = coll_find(g_from_coll, g_id);
        if (i >= 0) {
            open_puzzle(coll_puzzle(g_from_coll, (i + 1) % coll_size(g_from_coll)));
            return;
        }
    }
    int ic = coll_of(g_id);
    if (ic >= PUZZLE_COLLECTION_COUNT) {         /* imported: the next one of its collection */
        uint16_t first = coll_first(ic), next = g_id + 1;
        if (next >= first + coll_size(ic)) next = first;
        open_puzzle(next);
        return;
    }
    if (puzzle_is_generated(g_id)) {             /* another one of the same variant and level */
        int lv = GEN_MEDIUM, var = VAR_CLASSIC;
        gen_lookup(g_id, NULL, &var, &lv);
        gen_start(var, lv);
        return;
    }
    int c = puzzle_collection_of(g_id);
    const puzzle_collection_t *pc = puzzle_collection(c);
    uint16_t next = g_id + 1;
    if (next >= pc->first + pc->count) next = pc->first;
    open_puzzle(next);
}

static void win_list_cb(lv_event_t *e)
{
    show_list();
}

static void win_hide_cb(lv_event_t *e)
{
    /* tap outside the card: look at the solved board */
    if (lv_event_get_target_obj(e) == g_win_ovl) lv_obj_set_hidden(g_win_ovl, true);
}

void start_game(uint16_t id, bool resume)
{
    g_practice = -1;                                /* (learn_open sets it after) */
    g_attack = false;                               /* (atk_open_next sets it after) */
    uint8_t given[81];
    if (!puzzle_get(id, given)) {
        sudoku_ui_notice("This puzzle isn't available (it was made on the board, and sudoku.db is missing).", 4000);
        return;
    }
    g_id = id;
    g_geo = puzzle_geom(id, g_geo_buf, &g_var);
    game_layout();
    for (int i = 0; i < 81; i++) {                  /* only the board's cells, digits 1..n */
        if (!g_geo->used[i] || given[i] > g_geo->n) given[i] = 0;
    }
    memcpy(g_given, given, sizeof(g_given));
    g_sol_ok = sudoku_solve(g_geo, g_given, g_sol);
    reset_game();
    const saved_game_t *s = store_saved_game();
    if (resume && s->id == id) {
        memcpy(g_b.val, s->val, sizeof(g_b.val));
        memcpy(g_b.notes, s->notes, sizeof(g_b.notes));
        for (int i = 0; i < 81; i++) g_b.colors[i] = s->colors[i] <= CELL_COLORS ? s->colors[i] : 0;
        for (int i = 0; i < 81; i++) {
            if (g_given[i]) g_b.val[i] = g_given[i];   /* givens are never editable */
            if (g_b.val[i] > g_geo->n || !g_geo->used[i]) g_b.val[i] = 0;
            if (!g_geo->used[i]) g_b.notes[i] = 0;
            g_b.notes[i] &= g_geo->all;
        }
        g_elapsed_ms = s->elapsed_ms;
        g_mistakes = s->mistakes;
        g_hints = s->hints;
        g_flags = s->flags;                         /* (a game keeps the challenge it started with) */
        const game_move_t *sm;
        int sn = store_game_moves(&sm);
        if (sn > 0 && (g_moves || (g_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX,
                                                              MALLOC_CAP_SPIRAM)))) {
            memcpy(g_moves, sm, sizeof(game_move_t) * sn);
            g_nmoves = sn;
        }
        g_started_at = s->started_at;               /* from when it was first opened */
        if (!g_started_at) g_started_tick = 0;      /* unknown, and not worked out from this session */
    }
    g_notes_mode = false;
    lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
    challenge_ui();
    g_last_tick = lv_tick_get();
    compute_conflicts();
    refresh_board();
    g_starred = id < PUZZLE_GEN_RAM_BASE && stats_db_bookmarked(id);
    update_header();
    lv_screen_load(screen_game());
    rules_first_time();
    tour_maybe();
}

/* Solved dialog of a daily puzzle: the result as a QR code, for a phone (and a message to friends) */
static void win_share_cb(lv_event_t *e)
{
    char day[12] = "", kind[48] = "", t[16];
    int var = 0, lv = 0;
    if (!puzzle_is_generated(s_solve_id) || !gen_lookup_day(s_solve_id, NULL, &var, &lv, day) || !day[0]) return;
    daily_kind_text(var, lv, kind, sizeof(kind));
    fmt_time(t, sizeof(t), s_solve_secs);
    char hints[24];
    if (s_solve_hints) snprintf(hints, sizeof(hints), "%u hint%s", (unsigned)s_solve_hints, s_solve_hints == 1 ? "" : "s");
    else snprintf(hints, sizeof(hints), "no hints");
    static char text[256];
    snprintf(text, sizeof(text), "Sudoku daily %s (%s)\n%s: solved in %s, %u mistake%s, %s%s\nESP32-P4 Sudoku",
             day, kind, store_player_name(), t, (unsigned)s_solve_mistakes, s_solve_mistakes == 1 ? "" : "s", hints,
             (s_solve_flags & GAME_FLAG_NO_NOTES) ? ", no notes" : "");
    if (!qr_dialog(SYMBOL_QR "  Today's result", text,
                   "Scan it with a phone to send it to friends: the day, the puzzle, your time and mistakes.")) {
        sudoku_ui_notice("Couldn't make a QR code of the result.", 3000);
    }
}

/* the ⋮ menu of the second row: the variant's rules, Share (a QR code), Star (Puzzles > Starred), Auto notes,
 * Restart */
static void game_menu_cb(lv_event_t *e)
{
    const ctx_item_t items[] = {
        {SYMBOL_BULB, "Rules", rules_cb, false},
        {SYMBOL_QR, "Share", share_cb, g_id == STORE_NO_GAME},
        {SYMBOL_STAR, g_starred ? "Unstar" : "Star", star_cb, !g_can_star},
        {LV_SYMBOL_LIST, "Auto notes", auto_notes_cb,        /* all the marks at once (Undo takes them back) */
         g_solved || (g_flags & GAME_FLAG_NO_NOTES) != 0},
        {LV_SYMBOL_REFRESH, "Restart", restart_cb, false},   /* (asks first once there's progress) */
    };
    ctx_menu_toggle(g_bulb_btn, items, (int)(sizeof(items) / sizeof(items[0])));
}

static void star_cb(lv_event_t *e)
{
    if (g_practice >= 0 || g_id >= PUZZLE_GEN_RAM_BASE) return;
    g_starred = !g_starred;
    stats_db_set_bookmark(g_id, g_starred);
    update_header();
    sudoku_ui_notice(g_starred ? "Starred: it's under Puzzles > Starred." : "No longer starred.", 2000);
}

/* the game just solved (from the Solved dialog) */
void win_replay_cb(lv_event_t *e)
{
    char t[16], sub[96];
    fmt_time(t, sizeof(t), s_solve_secs);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             store_player_name(), t, (unsigned)g_hints, g_hints == 1 ? "" : "s");
    open_replay(g_id, g_moves, g_moves ? g_nmoves : 0, sub, store_player_name());
}

/* ---- accessors: the other UI files reach this file's state through these */
const char *const *ui_digits(void) { return DIGITS; }
uint16_t game_id(void) { return g_id; }
void game_set_id(uint16_t v) { g_id = v; }
int game_var(void) { return g_var; }
const sudoku_geom_t *game_geo(void) { return g_geo; }
void game_set_geo(const sudoku_geom_t *v) { g_geo = v; }
const sudoku_geom_t *game_geo_buf(void) { return g_geo_buf; }
void game_set_geo_buf(sudoku_geom_t *v) { g_geo_buf = v; }
const uint8_t *game_given(void) { return g_given; }
const board_t *game_b(void) { return &g_b; }
int game_sel(void) { return g_sel; }
void game_set_sel(int v) { g_sel = v; }
bool game_notes_mode(void) { return g_notes_mode; }
void game_set_notes_mode(bool v) { g_notes_mode = v; }
bool game_solved(void) { return g_solved; }
void game_set_solved(bool v) { g_solved = v; }
bool game_paused(void) { return g_paused; }
uint32_t game_elapsed_ms(void) { return g_elapsed_ms; }
void game_set_elapsed_ms(uint32_t v) { g_elapsed_ms = v; }
uint16_t game_mistakes(void) { return g_mistakes; }
uint16_t game_hints(void) { return g_hints; }
uint32_t game_flags(void) { return g_flags; }
void game_set_flags(uint32_t v) { g_flags = v; }
int hint_step(void) { return h_step; }
void hint_set_step(int v) { h_step = v; }
const sudoku_step_t *hint_info(void) { return &h_info; }
void hint_set_info(const sudoku_step_t *v) { h_info = *v; }
int game_practice(void) { return g_practice; }
void game_set_practice(int v) { g_practice = v; }
bool game_attack(void) { return g_attack; }
void game_set_attack(bool v) { g_attack = v; }
bool game_starred(void) { return g_starred; }
void game_set_starred(bool v) { g_starred = v; }
lv_obj_t *hint_card(void) { return h_card; }
lv_obj_t *hint_text(void) { return h_text; }
lv_obj_t *hint_act(void) { return h_act; }
int game_nmoves(void) { return g_nmoves; }
void game_set_nmoves(int v) { g_nmoves = v; }
int64_t game_start_stamp(void) { return g_started_at; }
void game_set_start_stamp(int64_t v) { g_started_at = v; }
uint32_t game_started_tick(void) { return g_started_tick; }
void game_set_started_tick(uint32_t v) { g_started_tick = v; }
uint32_t game_last_tick(void) { return g_last_tick; }
void game_set_last_tick(uint32_t v) { g_last_tick = v; }
const board_t *game_undo(void) { return g_undo; }
void game_set_undo(board_t *v) { g_undo = v; }
int game_undo_head(void) { return g_undo_head; }
void game_set_undo_head(int v) { g_undo_head = v; }
int game_undo_len(void) { return g_undo_len; }
void game_set_undo_len(int v) { g_undo_len = v; }
const board_t *game_redo(void) { return g_redo; }
void game_set_redo(board_t *v) { g_redo = v; }
int game_from_coll(void) { return g_from_coll; }
void game_set_from_coll(int v) { g_from_coll = v; }
lv_obj_t *game_grid(void) { return g_grid; }
lv_obj_t *const *game_boxes(void) { return g_boxes; }
lv_obj_t *const *game_cells(void) { return g_cells; }
lv_obj_t *const *game_cell_lbl(void) { return g_cell_lbl; }
lv_obj_t *const *game_clues(void) { return g_clues; }
lv_obj_t *const *game_pad(void) { return g_pad; }
int game_layout_var(void) { return g_layout_var; }
void game_set_layout_var(int v) { g_layout_var = v; }
lv_obj_t *game_notes_btn(void) { return g_notes_btn; }
lv_obj_t *game_pause_ovl(void) { return g_pause_ovl; }
lv_obj_t *game_undo_btn(void) { return g_undo_btn; }
lv_obj_t *game_hint_btn(void) { return g_hint_btn; }
lv_obj_t *game_bulb_btn(void) { return g_bulb_btn; }
lv_obj_t *game_timer_btn(void) { return g_timer_btn; }
lv_obj_t *game_fill_btn(void) { return g_fill_btn; }

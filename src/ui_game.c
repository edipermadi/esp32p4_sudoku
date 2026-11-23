/**
 * @file ui_game.c
 * @ingroup ui_game
 * @brief The game screen: the rules check, saving, the board, confetti, input, building the screen.
 */
#include "ui_internal.h"
#include "learn_core.h"
#include "game_core.h"

/* this file's own functions, in any order */
static void restart_confirm_cb(lv_event_t *e);
static void atk_stop_cb(lv_event_t *e);
static void atk_keep_cb(lv_event_t *e);
static bool is_peer(int a, int b);
static bool is_digit_wrong(int i);
static bool is_unit_complete(int u);
static int units_completed_by(int i);
static int filled_count(void);
static void on_solved(void);
static void cell_draw_cb(lv_event_t *e);
static void hint_act_cb(lv_event_t *e);
static void hint_close_cb(lv_event_t *e);
static void hint_cb(lv_event_t *e);
static void pause_cb(lv_event_t *e);
static void star_cb(lv_event_t *e);
static void game_menu_cb(lv_event_t *e);

/** @brief A digit as text ("" for 0). */
static const char *const DIGITS[10] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

static uint16_t g_id = STORE_NO_GAME;   /**< the puzzle played (STORE_NO_GAME: none) */
static int      g_var = VAR_CLASSIC;     /**< the puzzle's variant (generator.h) */
static const sudoku_geom_t *g_geo;       /**< and its geometry: units, peers, size */
static sudoku_geom_t *g_geo_buf;         /**< PSRAM: a Jigsaw puzzle's geometry (its regions) */
static uint8_t  g_given[81];     /**< the givens, 0 = empty */
static board_t  g_b;             /**< the board */
static bool     g_conflict[81];  /**< a digit breaking a rule (compute_conflicts()) */
static int      g_sel = -1;      /**< the selected cell, -1: none */
static bool     g_notes_mode;    /**< the pad writes pencil marks */
static bool     g_solved;        /**< solved */
static bool     g_paused;        /**< paused */
static uint32_t g_elapsed_ms;    /**< the game's clock, ms (pauses excluded) */
static uint16_t g_mistakes;      /**< digits placed that broke a rule (undo doesn't remove them) */
static uint16_t g_hints;         /**< cells filled with Fill (undo doesn't remove them either) */
static uint32_t g_flags;         /**< GAME_FLAG_* (store.h): the no-notes challenge, techniques the hints showed */
static uint8_t  g_sol[81];       /**< the solution, for Fill */
static game_move_t *g_moves;     /**< move log of this attempt (moves.h), PSRAM, GAME_MOVES_MAX */
static int      h_step;          /**< 0 = no hint open, 1 = nudge shown, 2 = reason shown */
static sudoku_step_t h_info;     /**< the hint shown: its step */
static int      g_practice = -1;  /**< Learn: the lesson being practised (learn_core.h), -1 = a real game */
static bool     g_attack;         /**< this game is part of a run */
static bool     g_starred;        /**< the puzzle is bookmarked (Star in the ⋮ menu) */
static bool     g_can_star;       /**< ... and can be (not Learn's, nor one kept only in RAM) */
static lv_obj_t *g_title_star;    /**< a small star after the title while it is starred */
static bool     g_can_save;       /**< the ⋮ menu's Save applies: a puzzle made on the board (generated, typed in) */
static bool     g_is_saved;       /**< ... and it's in a saved collection already */
static lv_obj_t *h_card;          /**< the hint's card */
static lv_obj_t *h_text;          /**< its text */
static lv_obj_t *h_act;           /**< its button (More, Place it, OK) */
static int      g_nmoves;         /**< moves in g_moves */
static bool     g_sol_ok;         /**< g_sol holds the solution */
static int64_t  g_started_at;    /**< Unix time the puzzle was opened/restarted, 0 = not known yet */
static uint32_t g_started_tick;  /**< lv_tick_get() then, to work it out once NTP sets the clock;
                                    0 = not this session (resumed with no known start) */
static uint32_t g_last_tick;     /**< the clock's last tick, lv_tick_get() */

static game_history_t g_hist;    /**< undo and redo (game_core.h): UNDO_MAX boards each, PSRAM (31 KB) */
static int      g_lock;          /**< digit-first: the digit locked on the pad (long press), 0 = none */
static int g_from_coll = -1;              /**< the collection the game was opened from (Next, back to the list) */

/* game screen */
static lv_obj_t *g_grid;                 /**< the board */
static lv_obj_t *g_boxes[9];             /**< its boxes */
static lv_obj_t *g_cells[81];            /**< its cells */
static lv_obj_t *g_cell_lbl[81];         /**< their digits (pencil marks are drawn) */
static lv_obj_t *g_clues[BOARD_CLUES];              /**< the clues outside the grid (Sandwich, Skyscraper, Little Killer) */
static lv_obj_t *g_pad[9];               /**< the number pad's keys */
static lv_obj_t *g_pad_cnt[9];           /**< each key's count left */
static int       g_layout_var = -1;     /**< variant the board and pad are laid out for */
static lv_obj_t *g_title;                /**< the title: the puzzle's name */
static lv_obj_t *g_best;                 /**< the best time (or "Not solved yet") */
static lv_obj_t *g_timer;                /**< the clock */
static lv_obj_t *g_timer_icon;           /**< its icon */
static lv_obj_t *g_status;               /**< "Filled 40 / 81", "Solved in ..." */
static lv_obj_t *g_notes_btn;            /**< Notes */
static lv_obj_t *g_pause_ovl;            /**< the pause cover */
static lv_obj_t *g_hint_cnt;     /**< hints used, on the Fill button */
static lv_obj_t *g_undo_btn;             /**< Undo */
static lv_obj_t *g_redo_btn;             /**< Redo */
/* (the tour points at these) */
static lv_obj_t *g_hint_btn;             /**< Hint */
static lv_obj_t *g_bulb_btn;             /**< the ⋮ menu */
static lv_obj_t *g_timer_btn;            /**< the timer (pause) */
static lv_obj_t *g_fill_btn;             /**< Fill */

/* ============================================================= sudoku rules */

/**
 * @brief Cells a and b share a unit of the puzzle being played (row, column, box, diagonal).
 * @param[in] a  a cell
 * @param[in] b  another
 * @return true if so
 */
static bool is_peer(int a, int b)
{
    return is_sudoku_geom_peer(g_geo, a, b);
}

/* (documented in ui_internal.h) */
int compute_conflicts(void)
{
    return game_conflicts(g_geo, g_b.val, g_conflict);
}

/**
 * @brief Is cell i's digit wrong by the Mistake check setting (Settings)? Off:
 * nothing; Rules: it breaks a rule (g_conflict); Solution: also a digit
 * that isn't the solution's. Shown red; placing one buzzes.
 * @param[in] i  the cell
 * @return true if wrong
 */
static bool is_digit_wrong(int i)
{
    if (!g_b.val[i]) {
        return false;
    }
    switch (store_assist()) {
    case ASSIST_OFF:
        return false;
    case ASSIST_SOLUTION:
        return g_conflict[i] || (!g_given[i] && g_sol_ok && g_b.val[i] != g_sol[i]);
    default:
        return g_conflict[i];
    }
}

/**
 * @brief Does the unit hold 1-n, each once?
 * @param[in] u  the unit
 * @return true if so
 */
static bool is_unit_complete(int u)
{
    uint16_t seen = 0;
    for (int k = 0; k < g_geo->n; k++) {
        uint8_t v = g_b.val[g_geo->unit[u][k]];
        if (!v || (seen & (1u << v))) {
            return false;
        }
        seen |= (uint16_t)(1u << v);
    }
    return true;
}

/**
 * @brief How many of cell i's units (row, column, box, diagonals) were just completed.
 * @param[in] i  the cell
 * @return how many
 */
static int units_completed_by(int i)
{
    int n = 0;
    for (int k = 0; k < g_geo->ncu[i]; k++) {
        n += is_unit_complete(g_geo->cu[i][k]);
    }
    return n;
}

/* (documented in ui_internal.h) */
lv_color_t cell_base_bg(const sudoku_geom_t *geo, int i, bool given)
{
    lv_color_t bg = given ? C_CELL_GIVEN : C_CELL;
    if (geo->parity[i] == PARITY_EVEN) {
        bg = lv_color_mix(C_GRID_THIN, bg, 150);    /* Even-Odd: even cells shaded */
    } else if (geo->parity[i] == PARITY_FORT) {
        bg = lv_color_mix(C_GRID_THIN, bg, 190);    /* Fortress: grey cells, a little darker */
    }
    return is_sudoku_geom_tinted(geo, i) ? lv_color_mix(C_ORANGE, bg, 40) : bg;
}

/**
 * @brief The digits on the board.
 * @return how many
 */
static int filled_count(void)
{
    int n = 0;
    for (int i = 0; i < 81; i++) {
        n += g_b.val[i] != 0;
    }
    return n;
}

/* (documented in ui_internal.h) */
bool has_progress(void)
{
    for (int i = 0; i < 81; i++) {
        if ((g_b.val[i] && !g_given[i]) || g_b.notes[i] || g_b.colors[i]) {
            return true;
        }
    }
    return false;
}

/* (documented in ui_internal.h) */
void save_current(void)
{
    if (g_id == STORE_NO_GAME || g_solved || g_practice >= 0 || g_attack) {
        return;    /* (practice, time attack: not kept) */
    }
    const saved_game_t *s = store_saved_game();
    if (!has_progress() && s->id != g_id) {
        return;    /* only looked at it: keep the slot as is */
    }
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

/* (documented in ui_internal.h) */
void update_timer_label(void)
{
    char buf[16];
    if (g_attack) {                                 /* the run's time left (always shown) */
        uint32_t used = attack_used_ms() + g_elapsed_ms;
        uint32_t left = used >= ATK_MS ? 0 : ATK_MS - used;
        fmt_time(buf, sizeof(buf), (left + 999) / 1000);
        if (strcmp(lv_label_get_text(g_timer), buf) != 0) {
            lv_label_set_text(g_timer, buf);
        }
        lv_obj_set_style_text_color(g_timer, g_paused ? C_MUTED : (left < 60000 ? C_ORANGE : C_TEXT), 0);
        return;
    }
    fmt_time(buf, sizeof(buf), g_elapsed_ms / 1000);
    if (!is_store_show_timer() && !g_solved) {
        snprintf(buf, sizeof(buf), "--:--");    /* Settings > Show timer off */
    }
    if (strcmp(lv_label_get_text(g_timer), buf) != 0) {
        lv_label_set_text(g_timer, buf);
    }
    lv_obj_set_style_text_color(g_timer, g_solved ? C_GREEN : (g_paused ? C_MUTED : C_TEXT), 0);
}

/* (documented in ui_internal.h) */
void update_header(void)
{
    char base[40], name[64], best[24], buf[48];
    puzzle_name(g_id, base, sizeof(base));
    if (g_practice >= 0) {
        snprintf(base, sizeof(base), "Practice: %s", learn_name(g_practice));
    }
    snprintf(name, sizeof(name), "%s%s", g_attack ? "Time attack: " : "", base);
    lv_label_set_text(g_title, name);
    /* the star: puzzles that stay (not Learn's, nor ones kept only in RAM) */
    bool can_star = g_practice < 0 && is_stats_db_ready() && g_id < PUZZLE_GEN_RAM_BASE;
    g_can_star = can_star;
    lv_obj_set_hidden(g_title_star, !(can_star && g_starred));   /* right after the title's text */
    lv_obj_update_layout(g_title);
    lv_obj_align_to(g_title_star, g_title, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    /* Save: a puzzle made on the board (not an imported one: it has its collection) */
    bool can_save = can_star && !g_attack && puzzle_is_generated(g_id) && import_of(g_id, NULL) < 0;
    bool saved = false;
    for (int k = 0; can_save && k < ui_nsaved(); k++) {
        saved |= coll_find(PUZZLE_COLLECTION_COUNT + ui_nimports() + k, g_id) >= 0;
    }
    g_can_save = can_save;                          /* (the ⋮ menu's Save) */
    g_is_saved = saved;
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

/* (documented in ui_internal.h) */
void refresh_board(void)
{
    int sv = g_lock ? g_lock : (g_sel >= 0 ? g_b.val[g_sel] : 0);   /* same-digit highlight */
    for (int i = 0; i < 81; i++) {
        if (!g_geo->used[i]) {
            continue;
        }
        lv_color_t bg = cell_base_bg(g_geo, i, g_given[i]);
        if (g_b.colors[i]) {
            bg = lv_color_mix(cell_color(g_b.colors[i]), bg, 110);    /* painted by the player */
        }
        if (g_solved) {
            bg = C_CELL_SOLVED;
        } else if (g_sel >= 0) {
            if (i == g_sel) {
                bg = C_CELL_SEL;
            } else if (sv && g_b.val[i] == sv) {
                bg = C_CELL_SAME;
            } else if (is_peer(i, g_sel)) {
                bg = C_CELL_PEER;
            }
        }
        if (h_step && !g_solved && hint_mark()[i]) {
            bg = hint_mark()[i] == 2 ? (h_info.kind == STEP_WRONG ? C_CELL_BAD : C_CELL_SEL) : C_CELL_SAME;
        }
        bool wrong = !g_solved && is_digit_wrong(i);
        if (wrong && !g_given[i] && i != g_sel) {
            bg = C_CELL_BAD;
        }
        lv_obj_set_style_bg_color(g_cells[i], bg, 0);

        lv_color_t fg = g_given[i] ? C_TEXT : C_DIGIT_USER;
        if (g_solved) {
            fg = g_given[i] ? C_TEXT : C_GREEN;
        } else if (wrong) {
            fg = C_RED;
        }
        lv_obj_set_style_text_color(g_cell_lbl[i], fg, 0);

        const char *t = DIGITS[g_b.val[i]];
        if (strcmp(lv_label_get_text(g_cell_lbl[i]), t) != 0) {
            lv_label_set_text_static(g_cell_lbl[i], t);
        }
        lv_obj_invalidate(g_cells[i]);   /* notes are custom-drawn */
    }

    /* number pad: digits left to place; a finished digit is dimmed */
    int count[10] = {0};
    for (int i = 0; i < 81; i++) {
        count[g_b.val[i]]++;
    }
    for (int d = 1; d <= g_geo->n; d++) {
        int left = g_geo->n - count[d];
        lv_label_set_text_fmt(g_pad_cnt[d - 1], "%d", left > 0 ? left : 0);
        lv_obj_set_style_opa(g_pad[d - 1], left <= 0 && d != g_lock ? LV_OPA_40 : LV_OPA_COVER, 0);
        bool locked = d == g_lock;                  /* digit-first: the locked digit is accent blue */
        lv_obj_set_state(g_pad[d - 1], LV_STATE_CHECKED, locked);
        lv_obj_set_style_text_color(lv_obj_get_child(g_pad[d - 1], 0), locked ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(g_pad_cnt[d - 1], locked ? lv_color_white() : C_MUTED, 0);
    }
    lv_obj_set_style_opa(g_undo_btn, g_hist.len ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(g_redo_btn, g_hist.redo_len ? LV_OPA_COVER : LV_OPA_40, 0);

    if (g_hints) {
        lv_label_set_text_fmt(g_hint_cnt, "%u", (unsigned)g_hints);
    }
    lv_obj_set_hidden(g_hint_cnt, g_hints == 0);

    /* status line */
    int bad = 0;
    for (int i = 0; i < 81; i++) {
        bad += is_digit_wrong(i);
    }
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



/**
 * @brief The puzzle is solved: the clock stops, the fanfare, confetti, then the Solved dialog; the solve is recorded.
 */
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
    win_prepare();
}

/* (documented in ui_internal.h) */
void enter_digit(int d)
{
    if (!can_edit()) {
        return;
    }
    int i = g_sel;
    bool placed = false;
    if (g_notes_mode) {
        if (g_b.val[i]) {
            return;
        }
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
    if (!placed || g_solved) {
        return;    /* on_solved() plays its own sound */
    }

    /* a mistake: breaks a rule (with Mistake check on Solution: or isn't the
     * solution's digit). Counted even with the check off, just not shown. */
    bool wrong = g_conflict[i] || (store_assist() == ASSIST_SOLUTION && g_sol_ok && d != g_sol[i]);
    if (wrong) {
        if (g_mistakes < 0xFFFF) {
            g_mistakes++;
        }
        if (store_assist() != ASSIST_OFF) {
            sfx_play(SFX_ERROR);
        }
        return;
    }
    placed_sounds(i, d);
}


/**
 * @brief Draws a cell's pencil marks.
 * @param[in] e  the draw event (user data: the cell)
 */
static void cell_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_b.val[i] || !g_b.notes[i]) {
        return;
    }
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
    const bool big = is_store_big_digits();            /* (Digit size: Large) */
    d.font = size >= 80 ? (big ? &lv_font_montserrat_24 : &lv_font_montserrat_22)
             : (big ? &lv_font_montserrat_18 : &lv_font_montserrat_16);
    if (g_geo->ncages) {                            /* Killer: smaller, below the cage sum in the corner */
        d.font = &lv_font_montserrat_12;
        y0 = 13;
        vpitch = (size - 15) / 3;
    }
    d.align = LV_TEXT_ALIGN_CENTER;
    for (int n = 1; n <= g_geo->n; n++) {
        if (!(g_b.notes[i] & (1u << n))) {
            continue;
        }
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

/* (documented in ui_internal.h) */
void challenge_ui(void)
{
    bool on = (g_flags & GAME_FLAG_NO_NOTES) != 0;
    if (on) {
        g_notes_mode = false;
        lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
    }
    lv_obj_set_state(g_notes_btn, LV_STATE_DISABLED, on);   /* (Auto notes: dimmed in the ⋮ menu) */
}

/* (documented in ui_internal.h) */
void set_paused(bool p)
{
    if (g_solved) {
        p = false;
    }
    if (p) {
        hint_drop();
        refresh_board();
    }
    g_paused = p;
    lv_obj_set_hidden(g_pause_ovl, !p);
    update_header();
    if (p) {
        save_current();
    }
}

/* ------------------------------------------------------------- build */

/* (documented in ui_internal.h) */
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
    /* (Save, into a collection: the ⋮ menu) */
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
    for (int i = 0; i < BOARD_CLUES; i++) {
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

    win_build();                                    /* the Solved dialog (ui_game_win.c) */

    g_layout_var = -1;                              /* new widgets: lay them out for the current puzzle */
    game_layout();
}

/**
 * @brief The hint card's button: More (the reason), Place it, or OK.
 * @param[in] e  the event
 */
static void hint_act_cb(lv_event_t *e)
{
    if (!h_step || g_solved || g_paused) {
        return;
    }
    if (h_step == 1 && is_hint_nudge()) {
        return;
    }
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
        if (g_practice >= 0) {
            stats_db_learned(g_practice);    /* Learn: this technique practised (Graduate) */
        }
    }
    g_sel = i;
    after_move();                                   /* closes the hint */
    if (!wrong && !g_solved) {
        placed_sounds(i, d);
    }
}

/**
 * @brief Closes the hint card.
 * @param[in] e  the event
 */
static void hint_close_cb(lv_event_t *e)
{
    hint_drop();
    refresh_board();
}

/**
 * @brief Hint: where to look; again: why, then the digit.
 * @param[in] e  the event
 */
static void hint_cb(lv_event_t *e)
{
    if (g_solved || g_paused || !g_sol_ok) {
        return;
    }
    if (g_attack) {
        sudoku_ui_notice("Fill and Hint are off in a time attack.", 2500);
        return;
    }
    if (h_step) {                                   /* already open: same as More / Place it */
        if (is_hint_nudge()) {
            return;    /* (a nudge goes no further) */
        }
        hint_act_cb(e);
        return;
    }
    if (!sudoku_next_step(g_geo, g_b.val, g_sol, &h_info) || h_info.cell < 0) {
        return;
    }
    if (g_hints < 0xFFFF) {
        g_hints++;    /* every hint opened counts once */
    }
    if (h_info.tech.kind > 0 && h_info.tech.kind < 16) {
        g_flags |= 1u << h_info.tech.kind;    /* (achievements) */
    }
    refresh_board();                                /* the hint count on the Fill button */
    save_current();
    h_step = 1;
    hint_render();
}

/**
 * @brief The timer: pauses or resumes.
 * @param[in] e  the event
 */
static void pause_cb(lv_event_t *e)
{
    set_paused(!g_paused);
}

/* (documented in ui_internal.h) */
void game_put_value(int i, uint8_t v)
{
    g_b.val[i] = v;
}

/* (documented in ui_internal.h) */
void reset_game(void)
{
    memcpy(g_b.val, g_given, sizeof(g_b.val));
    memset(g_b.notes, 0, sizeof(g_b.notes));
    memset(g_b.colors, 0, sizeof(g_b.colors));
    g_elapsed_ms = 0;
    g_mistakes = 0;
    g_hints = 0;
    g_flags = is_store_no_notes() ? GAME_FLAG_NO_NOTES : 0;   /* (Settings > Game: the no-notes challenge) */
    g_nmoves = 0;                                   /* a new attempt, a new log */
    hint_drop();
    mark_started();                                 /* opened or restarted: a new attempt */
    game_history_clear(&g_hist);
    g_lock = 0;
    g_sel = -1;
    g_solved = false;
    win_hide();
    set_paused(false);
    compute_conflicts();
    refresh_board();
}

/* (documented in ui_internal.h) */
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
        if (!g_geo->used[i] || given[i] > g_geo->n) {
            given[i] = 0;
        }
    }
    memcpy(g_given, given, sizeof(g_given));
    g_sol_ok = sudoku_solve(g_geo, g_given, g_sol);
    reset_game();
    const saved_game_t *s = store_saved_game();
    if (resume && s->id == id) {
        memcpy(g_b.val, s->val, sizeof(g_b.val));
        memcpy(g_b.notes, s->notes, sizeof(g_b.notes));
        for (int i = 0; i < 81; i++) {
            g_b.colors[i] = s->colors[i] <= CELL_COLORS ? s->colors[i] : 0;
        }
        for (int i = 0; i < 81; i++) {
            if (g_given[i]) {
                g_b.val[i] = g_given[i];    /* givens are never editable */
            }
            if (g_b.val[i] > g_geo->n || !g_geo->used[i]) {
                g_b.val[i] = 0;
            }
            if (!g_geo->used[i]) {
                g_b.notes[i] = 0;
            }
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
        if (!g_started_at) {
            g_started_tick = 0;    /* unknown, and not worked out from this session */
        }
    }
    g_notes_mode = false;
    lv_obj_remove_state(g_notes_btn, LV_STATE_CHECKED);
    challenge_ui();
    g_last_tick = lv_tick_get();
    compute_conflicts();
    refresh_board();
    g_starred = id < PUZZLE_GEN_RAM_BASE && is_stats_db_bookmarked(id);
    update_header();
    lv_screen_load(screen_game());
    rules_first_time();
    tour_maybe();
}

/**
 * @brief the ⋮ menu of the second row: the variant's rules, Share (a QR code), Star (Puzzles > Starred), Save
 * (into a collection: Puzzles > Saved), Auto notes, Restart
 * @param[in] e  the event
 */
static void game_menu_cb(lv_event_t *e)
{
    const ctx_item_t items[] = {
        {SYMBOL_BULB, "Rules", rules_cb, false, false},
        {SYMBOL_QR, "Share", share_cb, g_id == STORE_NO_GAME, false},
        {SYMBOL_STAR, g_starred ? "Unstar" : "Star", star_cb, !g_can_star, false},
        /* made on the board only (built-in and imported puzzles have their collection) */
        {LV_SYMBOL_SAVE, g_is_saved ? "Saved: collections" : "Save to collection", save_cb, !g_can_save, false},
        {
            LV_SYMBOL_LIST, "Auto notes", auto_notes_cb,        /* all the marks at once (Undo takes them back) */
            g_solved || (g_flags & GAME_FLAG_NO_NOTES) != 0, false
        },
        {LV_SYMBOL_REFRESH, "Restart", restart_cb, false, false},   /* (asks first once there's progress) */
    };
    ctx_menu_toggle(g_bulb_btn, items, (int)(sizeof(items) / sizeof(items[0])));
}

/**
 * @brief Star: bookmarks the puzzle (Puzzles > Starred), or unstars it.
 * @param[in] e  the event
 */
static void star_cb(lv_event_t *e)
{
    if (g_practice >= 0 || g_id >= PUZZLE_GEN_RAM_BASE) {
        return;
    }
    g_starred = !g_starred;
    stats_db_set_bookmark(g_id, g_starred);
    update_header();
    sudoku_ui_notice(g_starred ? "Starred: it's under Puzzles > Starred." : "No longer starred.", 2000);
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
board_t *game_board(void) { return &g_b; }
game_history_t *game_hist(void) { return &g_hist; }
int game_lock(void) { return g_lock; }
void game_set_lock(int v) { g_lock = v; }
void game_set_hints(uint16_t v) { g_hints = v; }
const uint8_t *game_sol(void) { return g_sol; }
bool is_game_sol_ok(void) { return g_sol_ok; }
int game_sel(void) { return g_sel; }
void game_set_sel(int v) { g_sel = v; }
bool is_game_notes_mode(void) { return g_notes_mode; }
void game_set_notes_mode(bool v) { g_notes_mode = v; }
bool is_game_solved(void) { return g_solved; }
void game_set_solved(bool v) { g_solved = v; }
bool is_game_paused(void) { return g_paused; }
uint32_t game_elapsed_ms(void) { return g_elapsed_ms; }
void game_set_elapsed_ms(uint32_t v) { g_elapsed_ms = v; }
uint16_t game_mistakes(void) { return g_mistakes; }
uint16_t game_hints(void) { return g_hints; }
uint32_t game_flags(void) { return g_flags; }
void game_set_flags(uint32_t v) { g_flags = v; }
const game_move_t *game_moves(void) { return g_moves; }
int hint_step(void) { return h_step; }
void hint_set_step(int v) { h_step = v; }
const sudoku_step_t *hint_info(void) { return &h_info; }
void hint_set_info(const sudoku_step_t *v) { h_info = *v; }
int game_practice(void) { return g_practice; }
void game_set_practice(int v) { g_practice = v; }
bool is_game_attack(void) { return g_attack; }
void game_set_attack(bool v) { g_attack = v; }
bool is_game_starred(void) { return g_starred; }
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
const board_t *game_undo(void) { return g_hist.undo; }
void game_set_undo(board_t *v) { g_hist.undo = v; }
int game_undo_head(void) { return g_hist.head; }
void game_set_undo_head(int v) { g_hist.head = v; }
int game_undo_len(void) { return g_hist.len; }
void game_set_undo_len(int v) { g_hist.len = v; }
const board_t *game_redo(void) { return g_hist.redo; }
void game_set_redo(board_t *v) { g_hist.redo = v; }
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

/* ------------------------------------------------------------- restart, back, the clock */
/**
 * @brief Restart confirmed: the game back to its givens.
 * @param[in] e  the event (user data: the dialog)
 */
static void restart_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (store_saved_game()->id == game_id()) {
        store_clear_game();
    }
    reset_game();
    lv_msgbox_close_async(mbox);
}

/* (documented in ui_internal.h) */
void restart_cb(lv_event_t *e)
{
    if (!is_game_solved() && !has_progress()) {
        reset_game();
        return;
    }
    lv_obj_t *m = make_msgbox("Restart puzzle?", "All entries and the timer will be reset.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Restart", restart_confirm_cb, true);
}

/**
 * @brief The time attack's Stop: the run ends (not recorded).
 * @param[in] e  the event (user data: the dialog)
 */
static void atk_stop_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    game_set_attack(false);                               /* (not recorded: the run wasn't finished) */
    game_set_solved(true);
    show_home();
}

/**
 * @brief The time attack's Keep going: back to the puzzle.
 * @param[in] e  the event (user data: the dialog)
 */
static void atk_keep_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    set_paused(false);
}

/* (documented in ui_internal.h) */
void back_cb(lv_event_t *e)
{
    if (is_game_attack() && !is_game_solved()) {
        set_paused(true);
        lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Stop the run?", "A time attack only counts when its 10 "
                                  "minutes are up. Puzzles solved so far still count as solves.");
        msgbox_button(m, "Keep going", atk_keep_cb, false);
        msgbox_button(m, "Stop", atk_stop_cb, true);
        return;
    }
    save_current();
    show_list();
}

/* (documented in ui_internal.h) */
void tick_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    bool running = game_id() != STORE_NO_GAME && lv_screen_active() == screen_game() && !is_game_solved() && !is_game_paused() && !is_tour_open();
    if (running) {
        game_set_elapsed_ms(game_elapsed_ms() + (now - game_last_tick()));
        update_timer_label();
        if (is_game_attack() && attack_used_ms() + game_elapsed_ms() >= ATK_MS) {
            atk_finish();
        }
    }
    game_set_last_tick(now);
}

/* (documented in ui_internal.h) */
void mark_started(void)
{
    time_t now = time(NULL);
    game_set_start_stamp(CLOCK_VALID(now) ? (int64_t)now : 0);
    game_set_started_tick(lv_tick_get());
    if (!game_started_tick()) {
        game_set_started_tick(1);
    }
}

/* (documented in ui_internal.h) */
int64_t game_started_at(void)
{
    time_t now = time(NULL);
    if (!g_started_at && g_started_tick && CLOCK_VALID(now)) {
        g_started_at = (int64_t)now - (int64_t)((lv_tick_get() - g_started_tick) / 1000);
    }
    return g_started_at;
}

/* (documented in ui_internal.h) */
void after_move(void)
{
    hint_drop();                                    /* the board changed: an open hint no longer fits */
    compute_conflicts();
    bool full = filled_count() == g_geo->ncells;
    bool ok = true;
    for (int i = 0; i < 81; i++) {
        ok &= !g_conflict[i];
    }
    if (full && ok) {
        on_solved();
    }
    refresh_board();
    save_current();                                 /* every move (not once solved) */
}

/* (documented in ui_internal.h) */
lv_color_t cell_color(int k)
{
    static const uint32_t C[CELL_COLORS] = {0xE5484D, 0xF5A524, 0xE2D33C, 0x30A46C, 0x3E7BFA, 0x9E6CF0};
    return lv_color_hex(C[(k - 1) % CELL_COLORS]);
}

/* (documented in ui_internal.h) */
void push_undo(void)
{
    game_history_push(&g_hist, &g_b, false);
}

/* (documented in ui_internal.h) */
bool can_edit(void)
{
    return g_sel >= 0 && !g_given[g_sel] && !g_solved && !g_paused;
}

/* (documented in ui_internal.h) */
void place_digit(int i, int d)
{
    g_b.val[i] = (uint8_t)d;
    g_b.notes[i] = 0;
    for (int j = 0; j < 81; j++) {
        if (j != i && is_peer(i, j)) {
            g_b.notes[j] &= (uint16_t)~(1u << d);
        }
    }
}

/* (documented in ui_internal.h) */
void placed_sounds(int i, int d)
{
    if (g_conflict[i]) {
        return;
    }
    int n = 0;
    bool clean = true;
    for (int j = 0; j < 81; j++) {
        if (g_b.val[j] == d) {
            n++;
            clean &= !g_conflict[j];
        }
    }
    if (n == g_geo->n && clean) {
        sfx_play(SFX_DIGIT_DONE);
    }
    int units = units_completed_by(i);              /* row / column / box / diagonal now complete */
    if (units) {
        sfx_play(units > 1 ? SFX_UNITS_DONE : SFX_UNIT_DONE);
    }
}

/* (documented in ui_internal.h) */
void record_move(int cell, int digit, move_kind_t kind)
{
    if (!g_moves) {
        g_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    }
    if (!g_moves || g_nmoves >= GAME_MOVES_MAX) {
        return;
    }
    g_moves[g_nmoves++] = (game_move_t) {
        .t_ms = g_elapsed_ms, .cell = (uint8_t)cell, .digit = (uint8_t)digit,
        .kind = (uint8_t)kind
    };
}

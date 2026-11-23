/**
 * @file ui_game_board.c
 * @ingroup ui_game
 * @brief The game screen drawn: each cell's digit, notes and colours (the same-digit and peer highlights, wrong digits, hints), the header and the timer.
 */
#include "ui_internal.h"
#include "learn_core.h"

/* this file's own functions, in any order */
static bool is_digit_wrong(int i);
static bool is_unit_complete(int u);

/**
 * @brief Is cell i's digit wrong by the Mistake check setting (Settings)? Off:
 * nothing; Rules: it breaks a rule (game_conflict_marks()); Solution: also a digit
 * that isn't the solution's. Shown red; placing one buzzes.
 * @param[in] i  the cell
 * @return true if wrong
 */
static bool is_digit_wrong(int i)
{
    if (!game_b()->val[i]) {
        return false;
    }
    switch (store_assist()) {
    case ASSIST_OFF:
        return false;
    case ASSIST_SOLUTION:
        return game_conflict_marks()[i] || (!game_given()[i] && is_game_sol_ok() && game_b()->val[i] != game_sol()[i]);
    default:
        return game_conflict_marks()[i];
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
    for (int k = 0; k < game_geo()->n; k++) {
        uint8_t v = game_b()->val[game_geo()->unit[u][k]];
        if (!v || (seen & (1u << v))) {
            return false;
        }
        seen |= (uint16_t)(1u << v);
    }
    return true;
}

/* (documented in ui_internal.h) */
int units_completed_by(int i)
{
    int n = 0;
    for (int k = 0; k < game_geo()->ncu[i]; k++) {
        n += is_unit_complete(game_geo()->cu[i][k]);
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

/* (documented in ui_internal.h) */
lv_color_t cell_color(int k)
{
    static const uint32_t C[CELL_COLORS] = {0xE5484D, 0xF5A524, 0xE2D33C, 0x30A46C, 0x3E7BFA, 0x9E6CF0};
    return lv_color_hex(C[(k - 1) % CELL_COLORS]);
}

/* (documented in ui_internal.h) */
void refresh_board(void)
{
    int sv = game_lock() ? game_lock() : (game_sel() >= 0 ? game_b()->val[game_sel()] : 0);   /* same-digit highlight */
    for (int i = 0; i < 81; i++) {
        if (!game_geo()->used[i]) {
            continue;
        }
        lv_color_t bg = cell_base_bg(game_geo(), i, game_given()[i]);
        if (game_b()->colors[i]) {
            bg = lv_color_mix(cell_color(game_b()->colors[i]), bg, 110);    /* painted by the player */
        }
        if (is_game_solved()) {
            bg = C_CELL_SOLVED;
        } else if (game_sel() >= 0) {
            if (i == game_sel()) {
                bg = C_CELL_SEL;
            } else if (sv && game_b()->val[i] == sv) {
                bg = C_CELL_SAME;
            } else if (is_peer(i, game_sel())) {
                bg = C_CELL_PEER;
            }
        }
        if (hint_step() && !is_game_solved() && hint_mark()[i]) {
            bg = hint_mark()[i] == 2 ? (hint_info()->kind == STEP_WRONG ? C_CELL_BAD : C_CELL_SEL) : C_CELL_SAME;
        }
        bool wrong = !is_game_solved() && is_digit_wrong(i);
        if (wrong && !game_given()[i] && i != game_sel()) {
            bg = C_CELL_BAD;
        }
        lv_obj_set_style_bg_color(game_cells()[i], bg, 0);

        lv_color_t fg = game_given()[i] ? C_TEXT : C_DIGIT_USER;
        if (is_game_solved()) {
            fg = game_given()[i] ? C_TEXT : C_GREEN;
        } else if (wrong) {
            fg = C_RED;
        }
        lv_obj_set_style_text_color(game_cell_lbl()[i], fg, 0);

        const char *t = ui_digits()[game_b()->val[i]];
        if (strcmp(lv_label_get_text(game_cell_lbl()[i]), t) != 0) {
            lv_label_set_text_static(game_cell_lbl()[i], t);
        }
        lv_obj_invalidate(game_cells()[i]);   /* notes are custom-drawn */
    }

    /* number pad: digits left to place; a finished digit is dimmed */
    int count[10] = {0};
    for (int i = 0; i < 81; i++) {
        count[game_b()->val[i]]++;
    }
    for (int d = 1; d <= game_geo()->n; d++) {
        int left = game_geo()->n - count[d];
        lv_label_set_text_fmt(game_pad_cnt()[d - 1], "%d", left > 0 ? left : 0);
        lv_obj_set_style_opa(game_pad()[d - 1], left <= 0 && d != game_lock() ? LV_OPA_40 : LV_OPA_COVER, 0);
        bool locked = d == game_lock();                  /* digit-first: the locked digit is accent blue */
        lv_obj_set_state(game_pad()[d - 1], LV_STATE_CHECKED, locked);
        lv_obj_set_style_text_color(lv_obj_get_child(game_pad()[d - 1], 0), locked ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(game_pad_cnt()[d - 1], locked ? lv_color_white() : C_MUTED, 0);
    }
    lv_obj_set_style_opa(game_undo_btn(), game_hist()->len ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(game_redo_btn(), game_hist()->redo_len ? LV_OPA_COVER : LV_OPA_40, 0);

    if (game_hints()) {
        lv_label_set_text_fmt(game_hint_cnt_lbl(), "%u", (unsigned)game_hints());
    }
    lv_obj_set_hidden(game_hint_cnt_lbl(), game_hints() == 0);

    /* status line */
    int bad = 0;
    for (int i = 0; i < 81; i++) {
        bad += is_digit_wrong(i);
    }
    bool full = filled_count() == game_geo()->ncells;
    if (is_game_solved()) {
        char t[16];
        fmt_time(t, sizeof(t), game_elapsed_ms() / 1000);
        lv_label_set_text_fmt(game_status_lbl(), LV_SYMBOL_OK "  Solved in %s", t);
        lv_obj_set_style_text_color(game_status_lbl(), C_GREEN, 0);
    } else if (bad) {
        lv_label_set_text_fmt(game_status_lbl(), LV_SYMBOL_WARNING "  %d %s", bad,
                              store_assist() == ASSIST_SOLUTION ? "wrong" : "conflicting");
        lv_obj_set_style_text_color(game_status_lbl(), C_RED, 0);
    } else if (full) {                              /* Mistake check off: something is wrong somewhere */
        lv_label_set_text(game_status_lbl(), "All filled, not solved yet");
        lv_obj_set_style_text_color(game_status_lbl(), C_ORANGE, 0);
    } else if (game_lock()) {
        lv_label_set_text_fmt(game_status_lbl(), "Placing %d: tap cells", game_lock());
        lv_obj_set_style_text_color(game_status_lbl(), C_ACCENT, 0);
    } else {
        lv_label_set_text_fmt(game_status_lbl(), "Filled %d / %d", filled_count(), game_geo()->ncells);
        lv_obj_set_style_text_color(game_status_lbl(), C_MUTED, 0);
    }
}

/* (documented in ui_internal.h) */
void cell_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (game_b()->val[i] || !game_b()->notes[i]) {
        return;
    }
    lv_obj_t *cell = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(cell, &co);
    int sv = game_lock() ? game_lock() : (game_sel() >= 0 ? game_b()->val[game_sel()] : 0);

    /* three marks per row: 3 rows on a 9x9 board, 2 (centred) on a 6x6 one; 4x4: two by two */
    const int cols = game_geo()->n == 4 ? 2 : 3;
    int size = lv_area_get_width(&co), pitch = size / cols, rows = (game_geo()->n + cols - 1) / cols;
    int x0 = 1, y0 = rows == 3 ? 2 : (size - rows * pitch) / 2;
    int vpitch = pitch;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    const bool big = is_store_big_digits();            /* (Digit size: Large) */
    d.font = size >= 80 ? (big ? &lv_font_montserrat_24 : &lv_font_montserrat_22)
             : (big ? &lv_font_montserrat_18 : &lv_font_montserrat_16);
    if (game_geo()->ncages && (game_geo()->cage_sum[0] || game_geo()->cage_prod[0])) {   /* Killer: below the cage sum */
        d.font = &lv_font_montserrat_12;
        y0 = 13;
        vpitch = (size - 15) / 3;
    }
    d.align = LV_TEXT_ALIGN_CENTER;
    for (int n = 1; n <= game_geo()->n; n++) {
        if (!(game_b()->notes[i] & (1u << n))) {
            continue;
        }
        int r = (n - 1) / cols, c = (n - 1) % cols;
        lv_area_t a = {
            .x1 = co.x1 + x0 + c * pitch, .y1 = co.y1 + y0 + r * vpitch,
            .x2 = co.x1 + x0 + c * pitch + pitch - 1, .y2 = co.y1 + y0 + r * vpitch + vpitch - 1,
        };
        d.color = n == sv ? C_ACCENT : C_MUTED;
        d.text = ui_digits()[n];
        lv_draw_label(layer, &d, &a);
    }
}

/* (documented in ui_internal.h) */
void update_header(void)
{
    char base[40], name[64], best[24], buf[48];
    puzzle_name(game_id(), base, sizeof(base));
    if (game_practice() >= 0) {
        snprintf(base, sizeof(base), "Practice: %s", learn_name(game_practice()));
    }
    snprintf(name, sizeof(name), "%s%s", is_game_attack() ? "Time attack: " : "", base);
    lv_label_set_text(game_title_lbl(), name);
    /* the star: puzzles that stay (not Learn's, nor ones kept only in RAM) */
    bool can_star = game_practice() < 0 && is_stats_db_ready() && game_id() < PUZZLE_GEN_RAM_BASE;
    lv_obj_set_hidden(game_title_star(), !(can_star && is_game_starred()));   /* right after the title's text */
    lv_obj_update_layout(game_title_lbl());
    lv_obj_align_to(game_title_star(), game_title_lbl(), LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    /* Save: a puzzle made on the board (not an imported one: it has its collection) */
    bool can_save = can_star && !is_game_attack() && puzzle_is_generated(game_id()) && import_of(game_id(), NULL) < 0;
    bool saved = false;
    for (int k = 0; can_save && k < ui_nsaved(); k++) {
        saved |= coll_find(PUZZLE_COLLECTION_COUNT + ui_nimports() + k, game_id()) >= 0;
    }
    game_set_save_state(can_star, can_save, saved);   /* (the ⋮ menu's Star and Save) */
    uint32_t b = store_best_time(game_id());
    if (is_game_attack()) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE "  %d solved in this run", attack_solved());
        lv_obj_set_style_text_color(game_best_lbl(), C_ORANGE, 0);
    } else if (b) {
        fmt_time(best, sizeof(best), b);
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  Best %s", best);
        lv_obj_set_style_text_color(game_best_lbl(), C_GREEN, 0);
    } else {
        snprintf(buf, sizeof(buf), "Not solved yet");
        lv_obj_set_style_text_color(game_best_lbl(), C_MUTED, 0);
    }
    lv_label_set_text(game_best_lbl(), buf);
    bool done = is_game_solved() && !is_game_attack();              /* (a run's end isn't a solve: the bolt) */
    lv_label_set_text(game_timer_icon(), is_game_attack() && is_game_solved() ? LV_SYMBOL_CHARGE
                      : done ? LV_SYMBOL_OK : (is_game_paused() ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE));
    lv_obj_set_style_text_color(game_timer_icon(), done ? C_GREEN : (is_game_attack() && is_game_solved() ? C_ORANGE : C_MUTED), 0);
    update_timer_label();
}

/* (documented in ui_internal.h) */
void update_timer_label(void)
{
    char buf[16];
    if (is_game_attack()) {                                 /* the run's time left (always shown) */
        uint32_t used = attack_used_ms() + game_elapsed_ms();
        uint32_t left = used >= ATK_MS ? 0 : ATK_MS - used;
        fmt_time(buf, sizeof(buf), (left + 999) / 1000);
        if (strcmp(lv_label_get_text(game_timer_lbl()), buf) != 0) {
            lv_label_set_text(game_timer_lbl(), buf);
        }
        lv_obj_set_style_text_color(game_timer_lbl(), is_game_paused() ? C_MUTED : (left < 60000 ? C_ORANGE : C_TEXT), 0);
        return;
    }
    fmt_time(buf, sizeof(buf), game_elapsed_ms() / 1000);
    if (!is_store_show_timer() && !is_game_solved()) {
        snprintf(buf, sizeof(buf), "--:--");    /* Settings > Show timer off */
    }
    if (strcmp(lv_label_get_text(game_timer_lbl()), buf) != 0) {
        lv_label_set_text(game_timer_lbl(), buf);
    }
    lv_obj_set_style_text_color(game_timer_lbl(), is_game_solved() ? C_GREEN : (is_game_paused() ? C_MUTED : C_TEXT), 0);
}

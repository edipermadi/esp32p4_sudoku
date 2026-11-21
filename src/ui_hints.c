/* sudoku_ui.c split: hints (the next step and its explanation) and the game screen tour. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void unit_name(int u, char *buf, size_t n);
static const char *units_word(void);
static void digit_list(int mask, const char *last_word, char *buf, size_t n);
static void mark_unit(int u, uint8_t v);
static void restart_confirm_cb(lv_event_t *e);
static void atk_stop_cb(lv_event_t *e);
static void atk_keep_cb(lv_event_t *e);
static void board_cages_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_marks_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_thermos_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void lk_clue_draw_cb(lv_event_t *e);
static void tour_area(int step, lv_area_t *a);
static void tour_end(void);
static void tour_next_cb(lv_event_t *e);
static void tour_skip_cb(lv_event_t *e);
static void tour_dim(int x1, int y1, int x2, int y2);
static void tour_show(int step);
static void tour_check_cb(lv_timer_t *t);

static uint8_t  h_mark[81];      /* hint highlight: 0, 1 = where to look, 2 = the cell */
static lv_obj_t *tour_ovl;        /* the game screen's tour, while it's up */
static bool      g_layout_left;         /* ... and on which side the pad is (Left-handed) */

/* ------------------------------------------------------------- hints */
/*
 * Hint: the next step a person would take (generator.c, sudoku_next_step),
 * in two steps: first where to look, then (More) the cell and why, then
 * Place it. A wrong digit is pointed out first. Each hint opened counts once.
 */
static void unit_name(int u, char *buf, size_t n)
{
    if (u < 9) snprintf(buf, n, "row %d", u + 1);
    else if (u < 18) snprintf(buf, n, "column %d", u - 9 + 1);
    else if (u < 27) snprintf(buf, n, "%s %d", game_geo()->variant == VAR_JIGSAW ? "region" : "box", u - 18 + 1);
    else if (u < UNIT_WINDOW) snprintf(buf, n, "the %s diagonal", u == UNIT_DIAG_MAIN ? "top-left" : "top-right");
    else if (u < UNIT_GROUP) {                      /* Hyper windows */
        static const char *const W[4] = {"top-left", "top-right", "bottom-left", "bottom-right"};
        snprintf(buf, n, "the %s window", W[(u - UNIT_WINDOW) & 3]);
    } else if (u < UNIT_ASTERISK) {                 /* Disjoint groups: by the place in the box */
        static const char *const SPOT[9] = {"top-left", "top", "top-right", "left", "centre", "right",
                                         "bottom-left", "bottom", "bottom-right"};
        snprintf(buf, n, "the %s cells of the boxes", SPOT[(u - UNIT_GROUP) % 9]);
    } else {
        snprintf(buf, n, "the asterisk");
    }
}

/* "row, column or box" (and diagonal, window, or region for a Jigsaw) */
static const char *units_word(void)
{
    if (game_geo()->diag) return "row, column, box or diagonal";
    if (game_geo()->windows) return "row, column, box or window";
    if (game_geo()->variant == VAR_JIGSAW) return "row, column or region";
    if (game_geo()->ncages) return "row, column, box or cage";
    if (game_geo()->variant == VAR_KNIGHT) return "row, column, box or a knight's move away";
    if (game_geo()->variant == VAR_KING) return "row, column, box or a touching cell";
    if (game_geo()->variant == VAR_DISJOINT) return "row, column, box or group";
    if (game_geo()->variant == VAR_ASTERISK) return "row, column, box or the asterisk";
    if (game_geo()->nthermos) return game_geo()->line_kind == LINE_THERMO ? "row, column or box (or the thermometer)"
                                     : "row, column or box (or the line)";
    return "row, column or box";
}

/* "1, 4 or 7" from a candidate bit mask */
static void digit_list(int mask, const char *last_word, char *buf, size_t n)
{
    int ds[9], k = 0;
    for (int d = 1; d <= 9; d++) if (mask >> d & 1) ds[k++] = d;
    size_t len = 0;
    buf[0] = 0;
    for (int i = 0; i < k; i++) {
        len += snprintf(buf + len, n - len, "%s%d", i == 0 ? "" : (i == k - 1 ? " " : ", "), ds[i]);
        if (i == k - 2 && len < n) len += snprintf(buf + len, n - len, " %s", last_word);
    }
}

static void mark_unit(int u, uint8_t v)
{
    for (int k = 0; k < game_geo()->n; k++) {
        int c = sudoku_unit_cell(game_geo(), u, k);
        if (c >= 0 && h_mark[c] < v) h_mark[c] = v;
    }
}

/* closes the hint without redrawing (the caller does) */
/* Settings > Game > Hints: Nudge only: where to look, nothing more (Learn keeps the full hint) */
bool hint_nudge(void)
{
    return store_hint_nudge() && game_practice() < 0;
}

void hint_drop(void)
{
    if (!hint_step()) return;
    hint_set_step(0);
    memset(h_mark, 0, sizeof(h_mark));
    lv_obj_set_hidden(hint_card(), true);
}

void hint_render(void)
{
    const sudoku_step_t *h = hint_info();
    const sudoku_tech_t *t = &h->tech;
    char text[360], u1[48], u2[24], tech[260] = "";
    int r = h->cell / 9 + 1, c = h->cell % 9 + 1;
    memset(h_mark, 0, sizeof(h_mark));

    /* where to look: the technique's area if one is needed, else the single's */
    if (h->kind == STEP_WRONG || h->kind == STEP_NAKED_SINGLE || h->kind == STEP_NONE) h_mark[h->cell] = 1;
    if (h->kind == STEP_HIDDEN_SINGLE && !t->kind) mark_unit(h->unit, 1);
    if (t->kind == TECH_SANDWICH) {                 /* Sandwich: the row or column */
        mark_unit(t->unit, 1);
        unit_name(t->unit, u1, sizeof(u1));
        snprintf(tech, sizeof(tech), "In %s the digits between the 1 and the 9 add up to %d: that leaves the 1 and "
                 "the 9 fewer places there. ", u1, t->unit >= 0 && t->unit < 18 ? game_geo()->sandwich_sum[t->unit] : 0);
    } else if (t->kind == TECH_SKY || t->kind == TECH_LK) {   /* Skyscraper: a clue's line; Little Killer: a diagonal */
        int k = t->unit, c = t->cell1 >= 0 ? t->cell1 : 0;
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        if (t->kind == TECH_SKY && k >= 0 && k < 18) {
            mark_unit(k, 1);
            unit_name(k, u1, sizeof(u1));
            snprintf(tech, sizeof(tech), "From the clue of %s, %d buildings are seen: of the ways its empty cells can "
                     "be filled, only those leave row %d, column %d %s. ", u1, game_geo()->sky[k], c / 9 + 1, c % 9 + 1,
                     digs);
        } else if (k >= 0 && k < game_geo()->nlk) {
            for (int i = 0; i < game_geo()->lk_n[k]; i++) h_mark[game_geo()->lk_cell[k][i]] = 1;
            snprintf(u1, sizeof(u1), "this diagonal");
            snprintf(tech, sizeof(tech), "This diagonal adds up to %d, so row %d, column %d can only be %s. ",
                     game_geo()->lk_sum[k], c / 9 + 1, c % 9 + 1, digs);
        }
    } else if (t->kind == TECH_LINE) {              /* Arrow, Renban, Whispers: the line */
        int k = t->unit, c = t->cell1 >= 0 ? t->cell1 : 0;
        for (int i = 0; k >= 0 && k < game_geo()->nthermos && i < game_geo()->thermo_n[k]; i++) h_mark[game_geo()->thermo_cell[k][i]] = 1;
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        int lk = game_geo()->line_kind;
        snprintf(u1, sizeof(u1), "this %s", lk == LINE_ARROW ? "arrow" : "line");
        snprintf(tech, sizeof(tech), "%s, so row %d, column %d can only be %s. ",
                 lk == LINE_ARROW ? "The arrow's shaft adds up to its circle" :
                 lk == LINE_RENBAN ? "The digits on this line are a run of consecutive digits" :
                 "Along this line, neighbours are 5 or more apart",
                 c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_THERMO) {            /* Thermo: the thermometer */
        int k = t->unit;
        for (int i = 0; k >= 0 && k < game_geo()->nthermos && i < game_geo()->thermo_n[k]; i++) h_mark[game_geo()->thermo_cell[k][i]] = 1;
        int c = t->cell1 >= 0 ? t->cell1 : 0;
        snprintf(u1, sizeof(u1), "this thermometer");
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        snprintf(tech, sizeof(tech), "Along the thermometer the digits rise from the bulb, so row %d, column %d can "
                 "only be %s. ", c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_CONSEC) {            /* Consecutive: the cell and its neighbours */
        int c = t->unit;
        if (c >= 0 && c < 81) {
            h_mark[c] = 1;
            if (c % 9 > 0) h_mark[c - 1] = 1;
            if (c % 9 < 8) h_mark[c + 1] = 1;
            if (c > 8) h_mark[c - 9] = 1;
            if (c < 72) h_mark[c + 9] = 1;
        }
        snprintf(u1, sizeof(u1), "row %d, column %d", c / 9 + 1, c % 9 + 1);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        if (game_geo()->variant == VAR_NONCONSEC)
            snprintf(tech, sizeof(tech), "Its neighbours can't be 1 apart from row %d, column %d, which leaves it "
                     "only %s. ", c / 9 + 1, c % 9 + 1, digs);
        else
            snprintf(tech, sizeof(tech), "The bars around row %d, column %d (and where there are none) leave it "
                     "only %s. ", c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_EDGE || t->kind == TECH_QUAD) {   /* the marks around a cell, or a circle */
        int c = t->unit;
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        snprintf(u1, sizeof(u1), "row %d, column %d", c / 9 + 1, c % 9 + 1);
        if (t->kind == TECH_QUAD && t->unit2 >= 0 && t->unit2 < game_geo()->nquads) {
            int at = game_geo()->quad_at[t->unit2];
            h_mark[at] = h_mark[at + 1] = h_mark[at + 9] = h_mark[at + 10] = 1;
            char need[8] = "";
            for (int k = 0; k < game_geo()->quad_n[t->unit2]; k++) need[k] = (char)('0' + game_geo()->quad_dig[t->unit2][k]);
            snprintf(tech, sizeof(tech), "The circle with %s needs those digits among its four cells, which leaves "
                     "row %d, column %d only %s. ", need, c / 9 + 1, c % 9 + 1, digs);
        } else if (c >= 0 && c < 81) {
            h_mark[c] = 1;
            if (c % 9 > 0) h_mark[c - 1] = 1;
            if (c % 9 < 8) h_mark[c + 1] = 1;
            if (c > 8) h_mark[c - 9] = 1;
            if (c < 72) h_mark[c + 9] = 1;
            int ek = game_geo()->edge_kind;
            snprintf(tech, sizeof(tech), "%s around row %d, column %d%s leave it only %s. ",
                     ek == EDGE_KROPKI ? "The dots" : ek == EDGE_XV ? "The X and V marks" : "The signs",
                     c / 9 + 1, c % 9 + 1, ek == EDGE_GT ? "" : " (and where there are none)", digs);
        }
    } else if (t->kind == TECH_CAGE) {              /* Killer: the cage's cells */
        int k = t->unit;
        for (int i = 0; k >= 0 && k < game_geo()->ncages && i < game_geo()->cage_n[k]; i++) h_mark[game_geo()->cage_cell[k][i]] = 1;
        int c0 = k >= 0 && k < game_geo()->ncages ? game_geo()->cage_cell[k][0] : 0;
        snprintf(u1, sizeof(u1), "the %d cage", k >= 0 && k < game_geo()->ncages ? game_geo()->cage_sum[k] : 0);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
        }
        snprintf(tech, sizeof(tech), "The cage at row %d, column %d adds up to %d: with what's in it and around "
                 "it, its empty cells can only be %s. ", c0 / 9 + 1, c0 % 9 + 1,
                 k >= 0 && k < game_geo()->ncages ? game_geo()->cage_sum[k] : 0, digs);
    } else if (t->kind == TECH_NAKED_TRIPLE || t->kind == TECH_X_WING || t->kind == TECH_SWORDFISH ||
               t->kind == TECH_XY_WING) {
        for (int i = 0; i < t->ncells; i++) h_mark[t->cells[i]] = 1;   /* the cells that make the pattern */
        char digs[24];
        if (t->kind == TECH_NAKED_TRIPLE) {
            unit_name(t->unit, u1, sizeof(u1));
            digit_list(t->digit, "or", digs, sizeof(digs));
            snprintf(tech, sizeof(tech), "In %s, three cells can only be %s between them, so no other cell "
                     "there can be any of those. ", u1, digs);
        } else if (t->kind == TECH_XY_WING) {
            int p = t->cells[0], a = t->cells[1], b = t->cells[2], z = t->digit;
            int x = __builtin_ctz((unsigned)(t->digit2 & t->cell1)), y = __builtin_ctz((unsigned)(t->digit2 & t->cell2));
            snprintf(u1, sizeof(u1), "the three highlighted cells");
            snprintf(tech, sizeof(tech), "Row %d, column %d can only be %d or %d. If it's %d, row %d, column %d is %d; "
                     "if it's %d, row %d, column %d is %d. Either way one of them is %d, so no cell that sees both "
                     "can be %d. ", p / 9 + 1, p % 9 + 1, x, y, x, a / 9 + 1, a % 9 + 1, z, y, b / 9 + 1, b % 9 + 1,
                     z, z, z);
        } else {                                    /* X-Wing, Swordfish: base lines, cover lines */
            bool rows = t->unit < 9;
            const char *base = rows ? "rows" : "columns", *cov = rows ? "columns" : "rows";
            int lines[3] = {t->unit, t->unit2, t->unit3}, nb = t->kind == TECH_X_WING ? 2 : 3;
            char bl[24] = "", cl[24] = "";
            size_t lb = 0, lc = 0;
            for (int i = 0; i < nb; i++) {
                lb += snprintf(bl + lb, sizeof(bl) - lb, "%s%d", i == 0 ? "" : (i == nb - 1 ? " and " : ", "),
                               lines[i] % 9 + 1);
            }
            for (int j = 0, k = 0; j < 9; j++) {
                if (!(t->digit2 >> j & 1)) continue;
                lc += snprintf(cl + lc, sizeof(cl) - lc, "%s%d", k == 0 ? "" : (k == nb - 1 ? " and " : ", "), j + 1);
                k++;
            }
            snprintf(u1, sizeof(u1), "%s %s", base, bl);
            snprintf(tech, sizeof(tech), "In %s %s, %d can only be in %s %s. %s %s must put their %ds in those %s, "
                     "so no other cell of %s %s can be %d. ", base, bl, t->digit, cov, cl,
                     nb == 2 ? "Both" : "All three", base, t->digit, cov, cov, cl, t->digit);
        }
    } else if (t->kind) {
        mark_unit(t->unit, 1);
        unit_name(t->unit, u1, sizeof(u1));
        unit_name(t->unit2 >= 0 ? t->unit2 : t->unit, u2, sizeof(u2));
        switch (t->kind) {
        case TECH_POINTING:
            snprintf(tech, sizeof(tech), "In %s, %d can only be in %s, so no other cell of %s can be %d. ",
                     u1, t->digit, u2, u2, t->digit);
            break;
        case TECH_CLAIMING:
            snprintf(tech, sizeof(tech), "In %s, %d can only be in %s, so no other cell of %s can be %d. ",
                     u1, t->digit, u2, u2, t->digit);
            break;
        case TECH_NAKED_PAIR:
            snprintf(tech, sizeof(tech), "In %s, two cells can only be %d or %d, so no other cell there can. ",
                     u1, t->digit, t->digit2);
            break;
        case TECH_HIDDEN_PAIR:
            snprintf(tech, sizeof(tech), "In %s, %d and %d fit only two cells, so those two can't be anything else. ",
                     u1, t->digit, t->digit2);
            break;
        }
    }

    if (hint_step() == 1) {
        char un[24];
        switch (h->kind) {
        case STEP_WRONG:
            snprintf(text, sizeof(text), "Something's not right: check the highlighted cell.");
            break;
        case STEP_HIDDEN_SINGLE:
            if (t->kind) snprintf(text, sizeof(text), "Look at %s: it narrows down where a digit can go.", u1);
            else {
                unit_name(h->unit, un, sizeof(un));
                snprintf(text, sizeof(text), "Look at %s: one digit has only one place left there.", un);
            }
            break;
        case STEP_NAKED_SINGLE:
            if (t->kind) snprintf(text, sizeof(text), "Look at %s, then at the highlighted cell.", u1);
            else snprintf(text, sizeof(text), "Look at the highlighted cell: only one digit fits.");
            break;
        default:
            snprintf(text, sizeof(text), hint_nudge() ? "No simple step here: this needs advanced techniques."
                                                       : "No simple step here: this needs advanced techniques. More shows one cell.");
            break;
        }
        lv_label_set_text(lv_obj_get_child(hint_act(), 0), "More");
    } else {
        h_mark[h->cell] = 2;
        char un[24];
        switch (h->kind) {
        case STEP_WRONG:
            snprintf(text, sizeof(text), "The %d at row %d, column %d is wrong: it isn't part of the solution.",
                     h->digit, r, c);
            break;
        case STEP_HIDDEN_SINGLE:
            unit_name(h->unit, un, sizeof(un));
            snprintf(text, sizeof(text), "%s%s%d can only go here in %s: its other empty cells already see a %d "
                     "in their %s.", tech, t->kind ? "That leaves one place: " : "",
                     h->digit, un, h->digit, units_word());
            break;
        case STEP_NAKED_SINGLE:
            snprintf(text, sizeof(text), "%sOnly %d fits here (row %d, column %d): every other digit is already "
                     "in its %s%s.", tech, h->digit, r, c, units_word(), t->kind ? ", or ruled out above" : "");
            break;
        default:
            snprintf(text, sizeof(text), "The highlighted cell (row %d, column %d) is %d.", r, c, h->digit);
            break;
        }
        lv_label_set_text(lv_obj_get_child(hint_act(), 0), h->kind == STEP_WRONG ? "Erase it" : "Place it");
    }
    lv_label_set_text(hint_text(), text);
    lv_obj_set_hidden(hint_act(), hint_step() == 1 && hint_nudge());   /* Settings > Game > Hints: Nudge only */

    /* the card at the bottom, or at the top when the bottom row matters */
    /* as tall as its text needs (a technique and its single take 4-6 lines), at least the
     * buttons' height; at the bottom (growing upwards) unless it would cover a highlighted
     * cell there, then at the top (growing downwards) */
    lv_obj_update_layout(hint_text());
    int32_t ch = lv_obj_get_height(hint_text()) + 28;
    if (ch < 92) ch = 92;
    lv_obj_set_height(hint_card(), ch);
    bool covered = false;
    for (int i = 0; i < 81 && !covered; i++) {
        if (!h_mark[i] || !game_cells()[i]) continue;
        lv_area_t co;
        lv_obj_get_coords(game_cells()[i], &co);
        covered = co.y2 > SCR_W - 4 - ch;
    }
    lv_obj_set_y(hint_card(), covered ? 4 : SCR_W - 4 - ch);
    lv_obj_set_hidden(hint_card(), false);
    refresh_board();
}

static void restart_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (store_saved_game()->id == game_id()) store_clear_game();
    reset_game();
    lv_msgbox_close_async(mbox);
}

void mbox_close_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

lv_obj_t *make_msgbox(const char *title, const char *text)
{
    lv_obj_t *m = lv_msgbox_create(NULL);
    lv_msgbox_add_title(m, title);
    /* left-aligned, wrapped; NULL: the caller fills the content */
    if (text) make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT, text);
    lv_obj_set_width(m, 520);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, 0);
    return m;
}

void msgbox_button(lv_obj_t *m, const char *text, lv_event_cb_t cb, bool primary)
{
    lv_obj_t *b = lv_msgbox_add_footer_button(m, text);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 52);
    if (!primary) {                                 /* Cancel, Close ...: grey, not the dialog's own white */
        lv_obj_set_style_bg_color(b, C_CARD2, 0);
        lv_obj_set_style_text_color(b, C_TEXT, 0);
    }
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, m);
    /* LVGL makes the footer LV_DPI_DEF / 3 (~43 px) tall, less than these
     * 52 px buttons, which then spill over the text and past the box edge */
    lv_obj_t *foot = lv_msgbox_get_footer(m);
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(foot, 10, 0);
    lv_obj_set_style_pad_bottom(foot, 18, 0);                  /* room under the buttons */
}

void restart_cb(lv_event_t *e)
{
    if (!game_solved() && !has_progress()) {
        reset_game();
        return;
    }
    lv_obj_t *m = make_msgbox("Restart puzzle?", "All entries and the timer will be reset.");
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, "Restart", restart_confirm_cb, true);
}

static void atk_stop_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    game_set_attack(false);                               /* (not recorded: the run wasn't finished) */
    game_set_solved(true);
    show_home();
}

static void atk_keep_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    set_paused(false);
}

void back_cb(lv_event_t *e)
{
    if (game_attack() && !game_solved()) {
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

void tick_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    bool running = game_id() != STORE_NO_GAME && lv_screen_active() == screen_game() && !game_solved() && !game_paused() && !tour_ovl;
    if (running) {
        game_set_elapsed_ms(game_elapsed_ms() + (now - game_last_tick()));
        update_timer_label();
        if (game_attack() && attack_used_ms() + game_elapsed_ms() >= ATK_MS) atk_finish();
    }
    game_set_last_tick(now);
}

/* Killer: each cage outlined with a dashed line just inside its cells, and
 * its sum small in its top-left cell (cells: the grid's children 9..89) */
static void board_cages_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    const int in = 4;                               /* the outline's inset from the cell edge */
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = C_MUTED;
    ld.width = 1;
    ld.dash_width = 4;
    ld.dash_gap = 3;
    for (int i = 0; i < 81; i++) {
        int k = geo->cage_of[i];
        if (k == CAGE_NONE) continue;
        lv_area_t a;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        int r = i / 9, c = i % 9;
        bool up = r > 0 && geo->cage_of[i - 9] == k, down = r < 8 && geo->cage_of[i + 9] == k;
        bool left = c > 0 && geo->cage_of[i - 1] == k, right = c < 8 && geo->cage_of[i + 1] == k;
        /* an edge on the cage's border is drawn inset; along a shared edge
         * the lines run on to the next cell so the outline stays joined */
        int x1 = left ? a.x1 - 1 : a.x1 + in, x2 = right ? a.x2 + 1 : a.x2 - in;
        int y1 = up ? a.y1 - 1 : a.y1 + in, y2 = down ? a.y2 + 1 : a.y2 - in;
        bool label = geo->cage_cell[k][0] == i;
        if (!up) {
            ld.p1.x = label ? a.x1 + in + 18 : x1;      /* room for the sum */
            ld.p1.y = ld.p2.y = a.y1 + in;
            ld.p2.x = x2;
            if (ld.p1.x < ld.p2.x) lv_draw_line(layer, &ld);
        }
        if (!down) {
            ld.p1.x = x1;
            ld.p2.x = x2;
            ld.p1.y = ld.p2.y = a.y2 - in;
            lv_draw_line(layer, &ld);
        }
        if (!left) {
            ld.p1.x = ld.p2.x = a.x1 + in;
            ld.p1.y = label ? a.y1 + in + 13 : y1;
            ld.p2.y = y2;
            if (ld.p1.y < ld.p2.y) lv_draw_line(layer, &ld);
        }
        if (!right) {
            ld.p1.x = ld.p2.x = a.x2 - in;
            ld.p1.y = y1;
            ld.p2.y = y2;
            lv_draw_line(layer, &ld);
        }
        if (label) {
            char sum[4];
            snprintf(sum, sizeof(sum), "%d", geo->cage_sum[k]);
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = sum;
            d.text_local = 1;
            lv_area_t la = {.x1 = a.x1 + 2, .y1 = a.y1 + 1, .x2 = a.x1 + 26, .y2 = a.y1 + 15};
            lv_draw_label(layer, &d, &la);
        }
    }
}

/* Over the cells: Jigsaw's thick lines between regions, Killer's cages (the
 * grid's children: the 9 boxes, then the 81 cells) */
/* Even-Odd: a ring in each odd cell (even ones are shaded, cell_base_bg);
 * Consecutive: a bar across the line between two cells 1 apart */
static void board_marks_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    for (int i = 0; i < 81; i++) {
        lv_area_t a, b;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        if (geo->parity[i] == PARITY_ODD) {
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_TRANSP;
            d.border_color = C_MUTED;
            d.border_width = 2;
            d.border_opa = LV_OPA_70;
            d.radius = LV_RADIUS_CIRCLE;
            lv_area_t r = {.x1 = a.x1 + 5, .y1 = a.y1 + 5, .x2 = a.x2 - 5, .y2 = a.y2 - 5};
            lv_draw_rect(layer, &d, &r);
        }
        if (!geo->consec_on) continue;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = C_ORANGE;
        d.bg_opa = LV_OPA_COVER;
        d.radius = 3;
        d.border_color = C_CARD;
        d.border_width = 1;
        if (i % 9 < 8 && (geo->consec[i] & CONSEC_RIGHT)) {          /* upright bar on the line to the right */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 1), &b);
            int cx = (a.x2 + b.x1) / 2, cy = (a.y1 + a.y2) / 2;
            lv_area_t r = {.x1 = cx - 4, .x2 = cx + 4, .y1 = cy - 12, .y2 = cy + 12};
            lv_draw_rect(layer, &d, &r);
        }
        if (i / 9 < 8 && (geo->consec[i] & CONSEC_DOWN)) {           /* flat bar on the line below */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 9), &b);
            int cx = (a.x1 + a.x2) / 2, cy = (a.y2 + b.y1) / 2;
            lv_area_t r = {.x1 = cx - 12, .x2 = cx + 12, .y1 = cy - 4, .y2 = cy + 4};
            lv_draw_rect(layer, &d, &r);
        }
    }
}

/* Kropki: a white or black dot; XV: an X or a V; Greater-Than: a chevron whose point
 * faces the smaller digit; centred on (cx, cy), on a line between two cells (down: the
 * line below a cell, else the one to its right). Also the rules card's picture. */
void draw_edge_mark(lv_layer_t *layer, int kind, int m, bool down, int cx, int cy)
{
    if (kind == EDGE_KROPKI) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_opa = LV_OPA_COVER;
        d.bg_color = m == MARK_WHITE ? lv_color_white() : lv_color_hex(0x2B2F36);
        d.border_color = m == MARK_WHITE ? lv_color_hex(0x2B2F36) : lv_color_white();
        d.border_width = m == MARK_WHITE ? 2 : 1;
        lv_area_t r = {.x1 = cx - 7, .y1 = cy - 7, .x2 = cx + 7, .y2 = cy + 7};
        lv_draw_rect(layer, &d, &r);
    } else if (kind == EDGE_XV) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = 4;
        d.bg_color = C_CARD;
        d.bg_opa = LV_OPA_COVER;
        lv_area_t r = {.x1 = cx - 9, .y1 = cy - 10, .x2 = cx + 9, .y2 = cy + 10};
        lv_draw_rect(layer, &d, &r);
        lv_draw_label_dsc_t l;
        lv_draw_label_dsc_init(&l);
        l.text = m == MARK_X ? "X" : "V";
        l.font = &lv_font_montserrat_16;
        l.color = C_ORANGE;
        l.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t t = {.x1 = cx - 9, .y1 = cy - 9, .x2 = cx + 9, .y2 = cy + 10};
        lv_draw_label(layer, &l, &t);
    } else if (kind == EDGE_GT) {
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = C_ORANGE;
        l.width = 3;
        l.round_start = l.round_end = 1;
        int s = m == MARK_LESS ? -1 : 1;            /* the point toward the smaller: left / up for LESS */
        if (!down) {
            l.p1.x = cx - 4 * s; l.p1.y = cy - 7; l.p2.x = cx + 4 * s; l.p2.y = cy;
            lv_draw_line(layer, &l);
            l.p1.x = cx - 4 * s; l.p1.y = cy + 7;
            lv_draw_line(layer, &l);
        } else {
            l.p1.x = cx - 7; l.p1.y = cy - 4 * s; l.p2.x = cx; l.p2.y = cy + 4 * s;
            lv_draw_line(layer, &l);
            l.p1.x = cx + 7;
            lv_draw_line(layer, &l);
        }
    }
}

/* Quadruple: a circle centred on (cx, cy) with its 1-4 digits, two to a line */
void draw_quad_circle(lv_layer_t *layer, int cx, int cy, const uint8_t *dig, int n)
{
    const int rad = 16;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = C_CARD;
    d.bg_opa = LV_OPA_COVER;
    d.border_color = C_ORANGE;
    d.border_width = 2;
    lv_area_t r = {.x1 = cx - rad, .y1 = cy - rad, .x2 = cx + rad, .y2 = cy + rad};
    lv_draw_rect(layer, &d, &r);
    char txt[8];
    int p = 0;
    for (int k = 0; k < n && k < 4; k++) {
        if (k == 2) txt[p++] = '\n';
        txt[p++] = (char)('0' + dig[k]);
    }
    txt[p] = 0;
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.text = txt;
    l.text_local = 1;
    l.font = &lv_font_montserrat_12;
    l.color = C_TEXT;
    l.align = LV_TEXT_ALIGN_CENTER;
    l.line_space = -2;
    int h = n > 2 ? 26 : 14;
    lv_area_t t = {.x1 = cx - rad, .y1 = cy - h / 2, .x2 = cx + rad, .y2 = cy + h / 2 + 1};
    lv_draw_label(layer, &l, &t);
}

static void board_edges_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int i = 0; i < 81; i++) {
        for (int k = 0; k < 2; k++) {               /* the line to the right, the one below */
            int m = k ? EDGE_DOWN(geo->edge[i]) : EDGE_RIGHT(geo->edge[i]), o = k ? i + 9 : i + 1;
            if (!m || o > 80 || (!k && i % 9 == 8)) continue;
            lv_area_t a, b;
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + o), &b);
            int cx = k ? (a.x1 + a.x2) / 2 : (a.x2 + b.x1) / 2, cy = k ? (a.y2 + b.y1) / 2 : (a.y1 + a.y2) / 2;
            draw_edge_mark(layer, geo->edge_kind, m, k, cx, cy);
        }
    }
}

static void board_quads_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int q = 0; q < geo->nquads; q++) {
        lv_area_t a, d;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->quad_at[q]), &a);
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->quad_at[q] + 10), &d);
        draw_quad_circle(layer, (a.x2 + d.x1) / 2, (a.y2 + d.y1) / 2, geo->quad_dig[q], geo->quad_n[q]);
    }
}

/* A line through cell centres pts[0..n-1] (cs: the cell size), as its kind is drawn, under the
 * digits (translucent): LINE_THERMO a wide soft grey line from a round bulb; LINE_ARROW a grey
 * ring (the circle) and a thin shaft from its edge with an arrowhead at the end; LINE_RENBAN a
 * wide purple band; LINE_WHISPER a narrow green line. Also the rules card's picture. */
void draw_line_path(lv_layer_t *layer, int kind, const lv_point_t *pts, int n, int cs)
{
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.round_start = ld.round_end = 1;
    int from = 0;
    lv_point_t start = pts[0];
    switch (kind) {
    case LINE_ARROW:
        ld.color = C_MUTED;
        ld.opa = LV_OPA_80;
        ld.width = 4;
        if (n > 1) {                                /* the shaft starts at the circle's edge */
            int dx = pts[1].x - pts[0].x, dy = pts[1].y - pts[0].y, r = cs * 4 / 10;
            int len = (int)sqrtf((float)(dx * dx + dy * dy));
            if (len) { start.x = pts[0].x + dx * r / len; start.y = pts[0].y + dy * r / len; }
        }
        break;
    case LINE_RENBAN:
        ld.color = lv_color_hex(0xA66BE0);
        ld.opa = LV_OPA_40;
        ld.width = cs * 35 / 100;
        break;
    case LINE_WHISPER:
        ld.color = lv_color_hex(0x2EAD5B);
        ld.opa = LV_OPA_70;
        ld.width = cs * 12 / 100;
        break;
    default:
        ld.color = C_MUTED;
        ld.opa = LV_OPA_40;
        ld.width = cs * 3 / 10;
    }
    for (int i = from + 1; i < n; i++) {
        ld.p1.x = i == 1 ? start.x : pts[i - 1].x;
        ld.p1.y = i == 1 ? start.y : pts[i - 1].y;
        ld.p2.x = pts[i].x;
        ld.p2.y = pts[i].y;
        lv_draw_line(layer, &ld);
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    int r = cs * 4 / 10;
    lv_area_t b = {.x1 = pts[0].x - r, .y1 = pts[0].y - r, .x2 = pts[0].x + r, .y2 = pts[0].y + r};
    if (kind == LINE_THERMO) {                      /* the bulb */
        d.bg_color = C_MUTED;
        d.bg_opa = LV_OPA_40;
        lv_draw_rect(layer, &d, &b);
    } else if (kind == LINE_ARROW) {                /* the circle, and the arrowhead at the shaft's end */
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = C_MUTED;
        d.border_opa = LV_OPA_80;
        d.border_width = 3;
        lv_draw_rect(layer, &d, &b);
        if (n > 1) {
            int dx = pts[n - 1].x - pts[n - 2].x, dy = pts[n - 1].y - pts[n - 2].y;
            float len = sqrtf((float)(dx * dx + dy * dy));
            if (len > 0) {
                float ux = dx / len, uy = dy / len, h = cs * 0.22f;
                for (int s = -1; s <= 1; s += 2) {
                    ld.p1.x = pts[n - 1].x;
                    ld.p1.y = pts[n - 1].y;
                    ld.p2.x = (int32_t)(pts[n - 1].x - ux * h - s * uy * h * 0.8f);
                    ld.p2.y = (int32_t)(pts[n - 1].y - uy * h + s * ux * h * 0.8f);
                    lv_draw_line(layer, &ld);
                }
            }
        }
    }
}

/* the board's lines (Thermo, Arrow, Renban, Whispers) through the cells' centres */
static void board_thermos_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int t = 0; t < geo->nthermos; t++) {
        lv_point_t pts[9];
        int cs = 0;
        for (int i = 0; i < geo->thermo_n[t]; i++) {
            lv_area_t a;
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->thermo_cell[t][i]), &a);
            pts[i].x = (a.x1 + a.x2) / 2;
            pts[i].y = (a.y1 + a.y2) / 2;
            cs = lv_area_get_width(&a);
        }
        draw_line_path(layer, geo->line_kind, pts, geo->thermo_n[t], cs);
    }
}

void board_regions_draw_cb(lv_event_t *e)
{
    lv_obj_t *grid = lv_event_get_current_target_obj(e);
    const sudoku_geom_t *geo = lv_obj_get_user_data(grid);
    if (geo && geo->nthermos) board_thermos_draw(grid, geo, lv_event_get_layer(e));
    if (geo && geo->ncages) board_cages_draw(grid, geo, lv_event_get_layer(e));
    if (geo && (geo->consec_on || geo->variant == VAR_EVENODD)) board_marks_draw(grid, geo, lv_event_get_layer(e));
    if (geo && geo->edge_kind) board_edges_draw(grid, geo, lv_event_get_layer(e));
    if (geo && geo->nquads) board_quads_draw(grid, geo, lv_event_get_layer(e));
    if (!geo || geo->variant != VAR_JIGSAW) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = C_GRID_THICK;
    d.bg_opa = LV_OPA_COVER;
    for (int i = 0; i < 81; i++) {
        lv_area_t a, b, r;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        if (i % 9 < 8 && geo->region[i] != geo->region[i + 1]) {          /* border on the right */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 1), &b);
            r = (lv_area_t){.x1 = a.x2 - 1, .x2 = b.x1 + 1, .y1 = a.y1 - LINE_THIN, .y2 = a.y2 + LINE_THIN};
            lv_draw_rect(layer, &d, &r);
        }
        if (i / 9 < 8 && geo->region[i] != geo->region[i + 9]) {          /* border below */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 9), &b);
            r = (lv_area_t){.x1 = a.x1 - LINE_THIN, .x2 = a.x2 + LINE_THIN, .y1 = a.y2 - 1, .y2 = b.y1 + 1};
            lv_draw_rect(layer, &d, &r);
        }
    }
}

/* Little Killer: a small arrow along direction dir (LK_*) from (x, y), about len long */
void draw_lk_arrow(lv_layer_t *layer, int x, int y, int dir, int len)
{
    int dx = dir == LK_DOWN_LEFT ? -1 : 1, dy = dir == LK_UP_RIGHT ? -1 : 1;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = C_ACCENT;
    l.width = 2;
    l.round_start = l.round_end = 1;
    int ex = x + dx * len, ey = y + dy * len, h = len / 2 + 1;
    l.p1.x = x; l.p1.y = y; l.p2.x = ex; l.p2.y = ey;
    lv_draw_line(layer, &l);
    l.p1.x = ex; l.p1.y = ey;
    l.p2.x = ex - dx * h; l.p2.y = ey;
    lv_draw_line(layer, &l);
    l.p2.x = ex; l.p2.y = ey - dy * h;
    lv_draw_line(layer, &l);
}

/* a Little Killer clue's label draws its arrow, toward its diagonal's first cell */
static void lk_clue_draw_cb(lv_event_t *e)
{
    lv_obj_t *l = lv_event_get_current_target_obj(e);
    int dir = (int)(intptr_t)lv_obj_get_user_data(l) - 1;
    if (dir < 0) return;
    lv_area_t a;
    lv_obj_get_coords(l, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    int dx = dir == LK_DOWN_LEFT ? -1 : 1, dy = dir == LK_UP_RIGHT ? -1 : 1;
    draw_lk_arrow(lv_event_get_layer(e), cx + dx * 11, cy + dy * 6, dir, 7);
}

/* clues: BOARD_CLUES labels on the board's screen for a Sandwich's sums, a Skyscraper's clues (rows, then
 * columns) or a Little Killer's sums, hidden for the other variants; may be NULL */
void board_layout_at(const sudoku_geom_t *geo, lv_obj_t *grid, lv_obj_t *const *boxes, lv_obj_t *const *cells,
                     lv_obj_t *const *clues, int x0, int y0, int size_all)
{
    lv_obj_set_user_data(grid, (void *)geo);
    const bool clued = geo->sandwich || geo->skyscraper || geo->nlk;   /* clues outside the grid */
    for (int i = 0; clues && i < BOARD_CLUES; i++) lv_obj_set_hidden(clues[i], true);
    if (geo->variant == VAR_JIGSAW) {
        /* evenly spaced cells on the thin-line colour in a thick frame; the
         * region borders are drawn over them (board_regions_draw_cb) */
        const int cs = (size_all - 2 * LINE_THICK - 8 * LINE_THIN) / 9;
        const int w = 2 * LINE_THICK + 9 * cs + 8 * LINE_THIN;
        lv_obj_set_pos(grid, x0 + (size_all - w) / 2, y0 + (size_all - w) / 2);
        lv_obj_set_size(grid, w, w);
        lv_obj_set_style_bg_color(grid, C_GRID_THIN, 0);
        lv_obj_set_style_border_color(grid, C_GRID_THICK, 0);
        lv_obj_set_style_border_width(grid, LINE_THICK, 0);
        for (int b = 0; b < 9; b++) lv_obj_set_hidden(boxes[b], true);
        for (int i = 0; i < 81; i++) {
            lv_obj_set_hidden(cells[i], false);
            lv_obj_set_pos(cells[i], LINE_THICK + (i % 9) * (cs + LINE_THIN), LINE_THICK + (i / 9) * (cs + LINE_THIN));
            lv_obj_set_size(cells[i], cs, cs);
        }
        lv_obj_invalidate(grid);
        return;
    }
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    const int n = geo->n, bw = geo->box_w, bh = geo->box_h;
    const int across = n / bw, down = n / bh;                  /* boxes */
    const int band = clued ? 36 : 0;           /* Sandwich, Skyscraper, Little Killer: room for the clues, top and left */
    const int size = size_all - band;
    int cw = (size - (across + 1) * LINE_THICK - across * (bw - 1) * LINE_THIN) / n;
    int ch = (size - (down + 1) * LINE_THICK - down * (bh - 1) * LINE_THIN) / n;
    int cs = cw < ch ? cw : ch;
    int box_w = bw * cs + (bw - 1) * LINE_THIN, box_h = bh * cs + (bh - 1) * LINE_THIN;
    int w = (across + 1) * LINE_THICK + across * box_w, h = (down + 1) * LINE_THICK + down * box_h;
    const int gx = x0 + band + (size - w) / 2, gy = y0 + band + (size - h) / 2;
    lv_obj_set_pos(grid, gx, gy);
    lv_obj_set_size(grid, w, h);
    for (int i = 0; (geo->sandwich || geo->skyscraper) && clues && i < 18; i++) {   /* left of the rows, above the columns */
        int k = i % 9, v = geo->sandwich ? geo->sandwich_sum[i] : geo->sky[i];
        int mid = LINE_THICK + (k / 3) * (box_w + LINE_THICK) + (k % 3) * (cs + LINE_THIN) + cs / 2;
        lv_obj_set_user_data(clues[i], NULL);
        lv_obj_set_style_text_font(clues[i], &lv_font_montserrat_20, 0);
        lv_label_set_text_fmt(clues[i], "%d", v);
        lv_obj_set_size(clues[i], band - 6, 24);
        if (i < 9) lv_obj_set_pos(clues[i], gx - band, gy + mid - 12);
        else lv_obj_set_pos(clues[i], gx + mid - (band - 6) / 2, gy - band + 4);
        lv_obj_set_hidden(clues[i], geo->skyscraper && !v);         /* (a Skyscraper line without a clue) */
    }
    for (int k = 0; geo->nlk && clues && k < geo->nlk; k++) {   /* Little Killer: a sum on the line before its diagonal */
        int first = geo->lk_cell[k][0], top = geo->lk_from_top[k], i = top ? first % 9 : first / 9;
        int spot = i + (geo->lk_dir[k] == LK_DOWN_RIGHT ? 0 : 1);   /* the line before / after that column or row */
        int at = spot < 9 ? LINE_THICK + (spot / 3) * (box_w + LINE_THICK) + (spot % 3) * (cs + LINE_THIN) - 1
                          : LINE_THICK + 2 * (box_w + LINE_THICK) + box_w + 1;
        lv_obj_t *l = clues[(top ? 10 : 0) + spot];   /* (one label per place) */
        if (!lv_obj_has_flag(l, LV_OBJ_FLAG_USER_1)) {             /* (the arrow: drawn by the label, once set up) */
            lv_obj_add_flag(l, LV_OBJ_FLAG_USER_1);
            lv_obj_add_event_cb(l, lk_clue_draw_cb, LV_EVENT_DRAW_POST, NULL);
        }
        lv_obj_set_user_data(l, (void *)(intptr_t)(1 + geo->lk_dir[k]));
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_label_set_text_fmt(l, "%d", geo->lk_sum[k]);
        lv_obj_set_size(l, band - 4, 20);
        if (top) lv_obj_set_pos(l, gx + at - (band - 4) / 2 - (geo->lk_dir[k] == LK_DOWN_RIGHT ? 6 : -6), gy - band + 2);
        else lv_obj_set_pos(l, gx - band + 1, gy + at - 10 - (geo->lk_dir[k] == LK_DOWN_RIGHT ? 6 : -6));
        lv_obj_set_hidden(l, false);
    }
    for (int b = 0; b < 9; b++) {
        bool on = b < across * down;
        lv_obj_set_hidden(boxes[b], !on);
        if (!on) continue;
        lv_obj_set_pos(boxes[b], LINE_THICK + (b % across) * (box_w + LINE_THICK),
                       LINE_THICK + (b / across) * (box_h + LINE_THICK));
        lv_obj_set_size(boxes[b], box_w, box_h);
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_set_hidden(cells[i], !geo->used[i]);
        if (!geo->used[i]) continue;
        int r = i / 9, c = i % 9;
        lv_obj_set_pos(cells[i], LINE_THICK + (c / bw) * (box_w + LINE_THICK) + (c % bw) * (cs + LINE_THIN),
                       LINE_THICK + (r / bh) * (box_h + LINE_THICK) + (r % bh) * (cs + LINE_THIN));
        lv_obj_set_size(cells[i], cs, cs);
    }
}

void board_layout(const sudoku_geom_t *geo, lv_obj_t *grid, lv_obj_t *const *boxes, lv_obj_t *const *cells,
                  lv_obj_t *const *clues, int x0)
{
    board_layout_at(geo, grid, boxes, cells, clues, x0, GRID_Y, GRID_SIZE);
}

/* the game screen's board and number pad (1..n, as tall as the board) for g_var */
void game_layout(void)
{
    if (!game_geo()) game_set_geo(sudoku_geom(game_var()));
    const bool left = store_left_hand();           /* Settings > Game > Left-handed: the pad on the left */
    if (game_layout_var() == game_var() && g_layout_left == left && game_var() != VAR_JIGSAW && game_var() != VAR_KILLER &&
        game_var() != VAR_EVENODD && game_var() != VAR_CONSEC && game_var() != VAR_THERMO && game_var() != VAR_SANDWICH &&
        game_var() != VAR_KROPKI && game_var() != VAR_XV && game_var() != VAR_GT && game_var() != VAR_QUAD &&
        game_var() != VAR_ARROW && game_var() != VAR_RENBAN && game_var() != VAR_WHISPER &&
        game_var() != VAR_SKY && game_var() != VAR_LK) {
        lv_obj_invalidate(game_grid());
        return;
    }
    lv_obj_invalidate(game_grid());                      /* every Jigsaw / Killer has its own regions / cages */
    game_set_layout_var(game_var());
    g_layout_left = left;
    const int board_x = left ? SCR_W - 8 - GAME_GRID_SIZE : GRID_X, pad_x = left ? 8 : GAME_PAD_X;
    board_layout_at(game_geo(), game_grid(), game_boxes(), game_cells(), game_clues(), board_x, GRID_Y, GAME_GRID_SIZE);
    for (int i = 0; i < 81; i++) {                  /* (4x4: big digits) */
        if (game_cell_lbl()[i]) lv_obj_set_style_text_font(game_cell_lbl()[i], DIGIT_FONT_N(game_geo()->n), 0);
    }
    lv_obj_set_x(game_pause_ovl(), board_x);
    const int n = game_geo()->n, span = GAME_GRID_SIZE - 6;         /* the pad as tall as the board */
    const int h = (span - (n - 1) * PAD_GAP) / n;
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *b = game_pad()[d - 1];
        lv_obj_set_hidden(b, d > n);
        if (d > n) continue;
        lv_obj_set_pos(b, pad_x, GRID_Y + 1 + (d - 1) * (h + PAD_GAP) + (span - n * h - (n - 1) * PAD_GAP) / 2);
        lv_obj_set_size(b, GAME_PAD_W, h);
    }
}
static int tour_step;

static void tour_area(int step, lv_area_t *a)
{
    lv_obj_t *o[4] = {NULL};
    switch (step) {
    case 0: o[0] = game_grid(); break;
    case 1: o[0] = game_pad()[0]; o[1] = game_pad()[8]; break;
    case 2: o[0] = game_undo_btn(); o[1] = game_fill_btn(); break;
    case 3: o[0] = game_hint_btn(); break;
    case 4: o[0] = game_bulb_btn(); break;                       /* the ⋮ menu */
    default: o[0] = game_timer_btn(); break;
    }
    lv_obj_get_coords(o[0], a);
    for (int i = 1; i < 4 && o[i]; i++) {
        lv_area_t b;
        lv_obj_get_coords(o[i], &b);
        if (b.x1 < a->x1) a->x1 = b.x1;
        if (b.y1 < a->y1) a->y1 = b.y1;
        if (b.x2 > a->x2) a->x2 = b.x2;
        if (b.y2 > a->y2) a->y2 = b.y2;
    }
    lv_area_increase(a, 4, 4);
}

static void tour_end(void)
{
    if (!tour_ovl) return;
    lv_obj_delete(tour_ovl);
    tour_ovl = NULL;
    stats_db_set_seen("tour", true);
    game_set_last_tick(lv_tick_get());                    /* (the clock goes on from now) */
}

static void tour_next_cb(lv_event_t *e)
{
    if (tour_step + 1 >= TOUR_STEPS) tour_end();
    else tour_show(tour_step + 1);
}

static void tour_skip_cb(lv_event_t *e)
{
    tour_end();
}

static void tour_dim(int x1, int y1, int x2, int y2)
{
    if (x2 <= x1 || y2 <= y1) return;
    lv_obj_t *r = lv_obj_create(tour_ovl);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, x1, y1);
    lv_obj_set_size(r, x2 - x1, y2 - y1);
    lv_obj_set_style_bg_color(r, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_60, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
}

static void tour_show(int step)
{
    static const char *const TITLE[TOUR_STEPS] = {"The board", "The number pad", "Tools", "Hint", "The top bar",
                                                  "The timer"};
    static const char *const TEXT[TOUR_STEPS] = {
        "Tap a cell, then a digit to put it there. Hold a cell to paint it a colour (in Killer: to see what its "
        "cage can hold).",
        "Puts the digit in the selected cell; the small number is how many are still to place. Hold a digit to "
        "light it up everywhere, then tap cells to place it.",
        "Undo and Redo; Erase a cell; Notes puts digits in as pencil marks; Fill puts the right digit in the "
        "selected cell (it counts as a hint).",
        "Shows where to look next. More then shows the cell and why, and Place it puts the digit in.",
        "The menu: this puzzle's Rules, Share (the puzzle as a QR code), Star (keeps it under Puzzles > "
        "Starred), Auto notes (all the pencil marks at once) and Restart. List goes back to the puzzles: the "
        "game is saved after every move.",
        "Tap the timer to pause: the board is hidden until you go on. That's all - enjoy the puzzle!",
    };
    tour_step = step;
    if (!tour_ovl) {
        tour_ovl = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(tour_ovl);
        lv_obj_set_size(tour_ovl, SCR_W, SCR_W);
        lv_obj_add_flag(tour_ovl, LV_OBJ_FLAG_CLICKABLE);   /* nothing under it takes a tap */
        lv_obj_set_scrollable(tour_ovl, false);
    }
    lv_obj_clean(tour_ovl);
    lv_area_t a;
    tour_area(step, &a);
    tour_dim(0, 0, SCR_W, a.y1);                    /* everything but the target, dimmed */
    tour_dim(0, a.y2 + 1, SCR_W, SCR_W);
    tour_dim(0, a.y1, a.x1, a.y2 + 1);
    tour_dim(a.x2 + 1, a.y1, SCR_W, a.y2 + 1);
    lv_obj_t *ring = lv_obj_create(tour_ovl);
    lv_obj_remove_style_all(ring);
    lv_obj_set_pos(ring, a.x1, a.y1);
    lv_obj_set_size(ring, lv_area_get_width(&a), lv_area_get_height(&a));
    lv_obj_set_style_border_color(ring, C_ACCENT, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_radius(ring, 8, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(tour_ovl);
    lv_obj_set_width(card, 560);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text_fmt(t, "%s", TITLE[step]);
    const char *text = TEXT[step];
    if (step == 3 && store_hint_nudge()) text = "Shows where to look next (Settings > Game > Hints: Nudge only).";
    lv_obj_t *x = make_wrap_text(card, &lv_font_montserrat_18, C_TEXT, text);
    lv_obj_set_width(x, LV_PCT(100));
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 52);
    lv_obj_t *n = make_label(row, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text_fmt(n, "%d of %d", step + 1, TOUR_STEPS);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *nb = make_btn(row, 0, 0, 140, 52, step + 1 < TOUR_STEPS ? "Next  " LV_SYMBOL_RIGHT : "Done",
                            &lv_font_montserrat_18, tour_next_cb, NULL);
    lv_obj_set_style_bg_color(nb, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(nb, 0), lv_color_white(), 0);
    lv_obj_align(nb, LV_ALIGN_RIGHT_MID, 0, 0);
    if (step + 1 < TOUR_STEPS) {
        lv_obj_t *sb = make_btn(row, 0, 0, 110, 52, "Skip", &lv_font_montserrat_18, tour_skip_cb, NULL);
        lv_obj_set_style_bg_color(sb, C_CARD2, 0);
        lv_obj_align(sb, LV_ALIGN_RIGHT_MID, -150, 0);
    }
    lv_obj_update_layout(card);
    int h = lv_obj_get_height(card), y;
    if (a.y1 >= h + 16) y = a.y1 - h - 10;          /* above the target, or below it, or over its lower part */
    else if (SCR_W - a.y2 >= h + 16) y = a.y2 + 10;
    else y = SCR_W - h - 80;
    lv_obj_set_pos(card, (SCR_W - 560) / 2, y);
}

static void tour_check_cb(lv_timer_t *t)
{
    if (tour_ovl || lv_screen_active() != screen_game() || game_practice() >= 0 || game_attack() || game_solved() || !stats_db_ready())
        return;
    lv_obj_t *top = lv_layer_top();                 /* the rules card, a notice: another time */
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        if (!lv_obj_is_hidden(lv_obj_get_child(top, (int32_t)i))) return;
    }
    if (stats_db_seen("tour")) return;
    hint_drop();
    tour_show(0);
}

void tour_maybe(void)
{
    lv_timer_t *t = lv_timer_create(tour_check_cb, 500, NULL);   /* (after Learn / Time attack set their flags) */
    lv_timer_set_repeat_count(t, 1);
}

void mark_started(void)
{
    time_t now = time(NULL);
    game_set_start_stamp(CLOCK_VALID(now) ? (int64_t)now : 0);
    game_set_started_tick(lv_tick_get());
    if (!game_started_tick()) game_set_started_tick(1);
}

/* ---- accessors: the other UI files reach this file's state through these */
const uint8_t *hint_mark(void) { return h_mark; }

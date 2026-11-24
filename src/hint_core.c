/**
 * @file hint_core.c
 * @ingroup hint_core
 * @brief A hint's words and highlights, without LVGL (the card: ui_hints.c).
 */
#include "hint_core.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief A unit's name: "row 3", "column 5", "box 2" ("region 2"), "the top-left diagonal", "the top-left window", ...
 * @param[in] geo  the board
 * @param[in] u  the unit
 * @param[out] buf  gets it
 * @param[in] n  its size
 */
static void unit_name(const sudoku_geom_t *geo, int u, char *buf, size_t n)
{
    if (u < 9) {
        snprintf(buf, n, "row %d", u + 1);
    } else if (u < 18) {
        snprintf(buf, n, "column %d", u - 9 + 1);
    } else if (u < 27) {
        snprintf(buf, n, "%s %d", geo->variant == VAR_JIGSAW ? "region" : "box", u - 18 + 1);
    } else if (u < UNIT_WINDOW) {
        snprintf(buf, n, "the %s diagonal", u == UNIT_DIAG_MAIN ? "top-left" : "top-right");
    } else if (u < UNIT_GROUP) {                    /* Hyper windows */
        static const char *const W[4] = {"top-left", "top-right", "bottom-left", "bottom-right"};
        snprintf(buf, n, "the %s window", W[(u - UNIT_WINDOW) & 3]);
    } else if (u < UNIT_ASTERISK) {                 /* Disjoint groups: by the place in the box */
        static const char *const SPOT[9] = {"top-left", "top", "top-right", "left", "centre", "right",
                                            "bottom-left", "bottom", "bottom-right"
                                           };
        snprintf(buf, n, "the %s cells of the boxes", SPOT[(u - UNIT_GROUP) % 9]);
    } else if (u == UNIT_ASTERISK) {
        snprintf(buf, n, "the asterisk");
    } else if (u == UNIT_GIRANDOLA) {
        snprintf(buf, n, "the pinwheel");
    } else {
        snprintf(buf, n, "the box centres");
    }
}

/**
 * @brief "row, column or box" (and diagonal, window, or region for a Jigsaw)
 * @param[in] geo  the board
 * @return the words
 */
static const char *units_word(const sudoku_geom_t *geo)
{
    if (geo->diag) {
        return "row, column, box or diagonal";
    }
    if (geo->windows) {
        return "row, column, box or window";
    }
    if (geo->variant == VAR_JIGSAW) {
        return "row, column or region";
    }
    if (geo->variant == VAR_REGIONS) {
        return "row, column, box or shaded region";
    }
    if (geo->ncages) {
        return "row, column, box or cage";
    }
    if (geo->variant == VAR_KNIGHT) {
        return "row, column, box or a knight's move away";
    }
    if (geo->variant == VAR_CHESS) {
        return "row, column, box, a knight's move away or a touching cell";
    }
    if (geo->variant == VAR_ARGYLE) {
        return "row, column, box or argyle line";
    }
    if (geo->variant == VAR_KING) {
        return "row, column, box or a touching cell";
    }
    if (geo->variant == VAR_DISJOINT) {
        return "row, column, box or group";
    }
    if (geo->variant == VAR_ASTERISK) {
        return "row, column, box or the asterisk";
    }
    if (geo->variant == VAR_CENTERDOT) {
        return "row, column, box or the box centres";
    }
    if (geo->variant == VAR_GIRANDOLA) {
        return "row, column, box or the pinwheel";
    }
    if (geo->nthermos)
        return geo->line_kind == LINE_THERMO || geo->line_kind == LINE_SLOW
               ? "row, column or box (or the thermometer)"
               : "row, column or box (or the line)";
    return "row, column or box";
}

/**
 * @brief "1, 4 or 7" from a candidate bit mask
 * @param[in] mask  the digits (bit d = d)
 * @param[in] last_word  before the last one: "or", "and"
 * @param[out] buf  gets it
 * @param[in] n  its size
 */
static void digit_list(int mask, const char *last_word, char *buf, size_t n)
{
    int ds[9], k = 0;
    for (int d = 1; d <= 9; d++) {
        if (mask >> d & 1) {
            ds[k++] = d;
        }
    }
    size_t len = 0;
    buf[0] = 0;
    for (int i = 0; i < k; i++) {
        len += snprintf(buf + len, n - len, "%s%d", i == 0 ? "" : (i == k - 1 ? " " : ", "), ds[i]);
        if (i == k - 2 && len < n) {
            len += snprintf(buf + len, n - len, " %s", last_word);
        }
    }
}

/**
 * @brief Highlights a unit's cells (keeping a higher mark already there).
 * @param[in] geo  the board
 * @param[in,out] mark  the highlights
 * @param[in] u  the unit
 * @param[in] v  1 = where to look, 2 = the cell
 */
static void mark_unit(const sudoku_geom_t *geo, uint8_t *mark, int u, uint8_t v)
{
    for (int k = 0; k < geo->n; k++) {
        int c = sudoku_unit_cell(geo, u, k);
        if (c >= 0 && mark[c] < v) {
            mark[c] = v;
        }
    }
}

/* (documented in hint_core.h) */
const char *hint_explain(const sudoku_geom_t *geo, const sudoku_step_t *h, int stage, bool is_nudge, char *text,
                         size_t text_n, uint8_t *mark)
{
    const sudoku_tech_t *t = &h->tech;
    char u1[48], u2[24], tech[260] = "";            /* (text: a technique's sentence and the step's) */
    const char *act;
    int r = h->cell / 9 + 1, c = h->cell % 9 + 1;
    memset(mark, 0, 81);

    /* where to look: the technique's area if one is needed, else the single's */
    if (h->kind == STEP_WRONG || h->kind == STEP_NAKED_SINGLE || h->kind == STEP_NONE) {
        mark[h->cell] = 1;
    }
    if (h->kind == STEP_HIDDEN_SINGLE && !t->kind) {
        mark_unit(geo, mark, h->unit, 1);
    }
    if (t->kind == TECH_SANDWICH) {                 /* Sandwich: the row or column */
        mark_unit(geo, mark, t->unit, 1);
        unit_name(geo, t->unit, u1, sizeof(u1));
        snprintf(tech, sizeof(tech), "In %s the digits between the 1 and the 9 add up to %d: that leaves the 1 and "
                 "the 9 fewer places there. ", u1, t->unit >= 0 && t->unit < 18 ? geo->sandwich_sum[t->unit] : 0);
    } else if (t->kind == TECH_SKY || t->kind == TECH_LK || t->kind == TECH_XSUM || t->kind == TECH_FRAME ||
               t->kind == TECH_OUTSIDE || t->kind == TECH_ROSSINI || t->kind == TECH_NROOM || t->kind == TECH_SKYSUM) {
        int k = t->unit, c = t->cell1 >= 0 ? t->cell1 : 0;
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        if (t->kind == TECH_SKY && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            snprintf(tech, sizeof(tech), "From the clue of %s, %d buildings are seen: of the ways its empty cells can "
                     "be filled, only those leave row %d, column %d %s. ", u1, geo->sky[k], c / 9 + 1, c % 9 + 1,
                     digs);
        } else if (t->kind == TECH_XSUM && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            snprintf(tech, sizeof(tech), "The clue of %s is %d: the first digit from there says how many digits add up "
                     "to it. Of the ways that can work out, only those leave row %d, column %d %s. ", u1,
                     geo->xsum[k], c / 9 + 1, c % 9 + 1, digs);
        } else if (t->kind == TECH_NROOM && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            snprintf(tech, sizeof(tech), "The clue of %s is %d: the first digit from there, X, says the X-th cell holds "
                     "it. Of the first digits that can do that, only those leave row %d, column %d %s. ", u1,
                     geo->nroom[k], c / 9 + 1, c % 9 + 1, digs);
        } else if (t->kind == TECH_SKYSUM && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            snprintf(tech, sizeof(tech), "From the clue of %s, the buildings seen add up to %d: of the ways its empty "
                     "cells can be filled, only those leave row %d, column %d %s. ", u1, geo->sky_sum[k], c / 9 + 1,
                     c % 9 + 1, digs);
        } else if (t->kind == TECH_PSKY && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            const int pc = geo->psky_clue[k];
            const int seen = pc & ~PSKY_EVEN;
            snprintf(tech, sizeof(tech), "From the clue of %s, %d %s building%s seen: of the ways its empty cells "
                     "can be filled, only those leave row %d, column %d %s. ", u1, seen, (pc & PSKY_EVEN) ? "even" : "odd",
                     seen == 1 ? " is" : "s are", c / 9 + 1, c % 9 + 1, digs);
        } else if (t->kind == TECH_FRAME && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            snprintf(tech, sizeof(tech), "The first three digits of %s add up to its clue %d, so row %d, column %d can "
                     "only be %s. ", u1, geo->frame_sum[k], c / 9 + 1, c % 9 + 1, digs);
        } else if (t->kind == TECH_OUTSIDE && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            char set[12] = "";
            for (int d = 1, n = 0; d <= 9; d++) {
                if (geo->outside_set[k] >> d & 1) {
                    n += snprintf(set + n, sizeof(set) - (size_t)n, "%s%d", n ? ", " : "", d);
                }
            }
            snprintf(tech, sizeof(tech), "The clue of %s says %s %s among its first three cells, so row %d, column %d "
                     "can only be %s. ", u1, set, strchr(set, ',') ? "are" : "is", c / 9 + 1, c % 9 + 1, digs);
        } else if (t->kind == TECH_ROSSINI && k >= 0 && k < 18) {
            mark_unit(geo, mark, k, 1);
            unit_name(geo, k, u1, sizeof(u1));
            int dir = geo->rossini_dir[k];
            snprintf(tech, sizeof(tech), "The first three digits of %s %s, so row %d, column %d can only be %s. ", u1,
                     dir == ROSSINI_UP ? "rise from its arrow's side" : dir == ROSSINI_DOWN ? "fall from its arrow's side"
                     : "neither rise nor fall (it has no arrow)", c / 9 + 1, c % 9 + 1, digs);
        } else if (t->kind == TECH_LK && k >= 0 && k < geo->nlk) {
            for (int i = 0; i < geo->lk_n[k]; i++) {
                mark[geo->lk_cell[k][i]] = 1;
            }
            snprintf(u1, sizeof(u1), "this diagonal");
            snprintf(tech, sizeof(tech), "This diagonal adds up to %d, so row %d, column %d can only be %s. ",
                     geo->lk_sum[k], c / 9 + 1, c % 9 + 1, digs);
        }
    } else if (t->kind == TECH_LINE) {              /* Arrow, Renban, Whispers, Palindrome, Region Sum: the line */
        int k = t->unit, c = t->cell1 >= 0 ? t->cell1 : 0;
        for (int i = 0; k >= 0 && k < geo->nthermos && i < geo->thermo_n[k]; i++) {
            mark[geo->thermo_cell[k][i]] = 1;
        }
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        int lk = geo->line_kind;
        snprintf(u1, sizeof(u1), "this %s", lk == LINE_ARROW || lk == LINE_PILL ? "arrow" : "line");
        snprintf(tech, sizeof(tech), "%s, so row %d, column %d can only be %s. ",
                 lk == LINE_ARROW ? "The arrow's shaft adds up to its circle" :
                 lk == LINE_RENBAN ? "The digits on this line are a run of consecutive digits" :
                 lk == LINE_PALINDROME ? "This line reads the same from both ends" :
                 lk == LINE_REGIONSUM ? "In each box this line passes through, its digits add up to the same" :
                 lk == LINE_ZIPPER ? "Cells the same distance from this line's middle add up to the same total" :
                 lk == LINE_BETWEEN ? "The digits on this line lie between the digits in its two circles" :
                 lk == LINE_ENTROPIC ? "Any three cells in a row on this line hold a low (1-3), a middle (4-6) and a "
                 "high (7-9) digit" :
                 lk == LINE_MODULAR ? "Any three cells in a row on this line hold one of 1 4 7, one of 2 5 8 and one "
                 "of 3 6 9" :
                 lk == LINE_DUTCH ? "Along this line, neighbours are 4 or more apart" :
                 lk == LINE_PARITY ? "Along this line, neighbours are one odd and one even digit" :
                 lk == LINE_NABNER ? "No two digits on this line are the same or 1 apart" :
                 lk == LINE_LOCKOUT ? "The diamonds at this line's ends are 4 or more apart, and the digits between "
                 "them lie outside that range" :
                 lk == LINE_DARROW ? "The two circles at this line's ends add up to the digits between them" :
                 lk == LINE_TEN ? "This line splits into runs of cells that each add up to 10" :
                 lk == LINE_PILL ? "The arrow's shaft adds up to its pill, the two cells read as a two-digit number" :
                 lk == LINE_AVG ? "The circled digit is the average of the other digits on its line" :
                 "Along this line, neighbours are 5 or more apart",
                 c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_THERMO) {            /* Thermo: the thermometer */
        int k = t->unit;
        for (int i = 0; k >= 0 && k < geo->nthermos && i < geo->thermo_n[k]; i++) {
            mark[geo->thermo_cell[k][i]] = 1;
        }
        int c = t->cell1 >= 0 ? t->cell1 : 0;
        snprintf(u1, sizeof(u1), "this thermometer");
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        snprintf(tech, sizeof(tech), "%s, so row %d, column %d can only be %s. ",
                 geo->line_kind == LINE_SLOW
                 ? "Along the thermometer the digits rise or stay the same from the bulb (they rise where two "
                 "neighbours share a row, column or box)" : "Along the thermometer the digits rise from the bulb",
                 c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_MAGIC) {            /* Magic Square: the centre box */
        const int c = t->unit >= 0 && t->unit < 81 ? t->unit : 40;
        for (int k = 0; k < 9; k++) {
            mark[(3 + k / 3) * 9 + 3 + k % 3] = 1;
        }
        snprintf(u1, sizeof(u1), "the centre box");
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        snprintf(tech, sizeof(tech), "The centre box is a magic square: each of its rows, columns and diagonals adds "
                 "up to 15, so row %d, column %d can only be %s. ", c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_ANTIDIAG) {         /* Anti-Diagonal: the diagonal */
        const int dg = t->unit == 1, c = t->cell1 >= 0 ? t->cell1 : 0;
        for (int k = 0; k < 9; k++) {
            mark[dg ? k * 9 + 8 - k : k * 10] = 1;
        }
        snprintf(u1, sizeof(u1), "the %s diagonal", dg ? "top-right" : "top-left");
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        snprintf(tech, sizeof(tech), "A long diagonal holds at most three different digits, and %s has %s already, "
                 "so row %d, column %d is one of them. ", u1, digs, c / 9 + 1, c % 9 + 1);
    } else if (t->kind == TECH_FLATMATES) {        /* Dutch Flatmates: the 5 (or the cell) and the cells above and below */
        const int five = t->unit >= 0 && t->unit < 81 ? t->unit : 0, c = t->cell1 >= 0 && t->cell1 < 81 ? t->cell1 : five;
        mark[five] = mark[c] = 1;
        if (five > 8) {
            mark[five - 9] = 1;
        }
        if (five < 72) {
            mark[five + 9] = 1;
        }
        if (c == five) {                            /* no 5 here: no 1 can be above, no 9 below */
            snprintf(u1, sizeof(u1), "row %d, column %d", c / 9 + 1, c % 9 + 1);
            snprintf(tech, sizeof(tech), "Every 5 needs a 1 right above it or a 9 right below it. Row %d, column %d "
                     "can have neither, so it can't be a 5. ", c / 9 + 1, c % 9 + 1);
        } else {                                    /* the 5's 1 can't come: the 9 below (or the other way) */
            snprintf(u1, sizeof(u1), "the 5 in row %d, column %d", five / 9 + 1, five % 9 + 1);
            const bool below = c > five;
            snprintf(tech, sizeof(tech), "Every 5 needs a 1 right above it or a 9 right below it. For %s, a %d %s "
                     "can't be, so the cell %s it must be %d. ", u1, below ? 1 : 9, below ? "above it" : "below it",
                     below ? "below" : "above", below ? 9 : 1);
        }
    } else if (t->kind == TECH_MATHRAX && t->unit >= 0 && t->unit < geo->nmx) {   /* Mathrax: the circle's cells */
        const int q = t->unit, at = geo->mx_at[q], c = t->cell1 >= 0 && t->cell1 < 81 ? t->cell1 : at;
        mark[at] = mark[at + 1] = mark[at + 9] = mark[at + 10] = 1;
        char lab[8], digs[24] = "", says[48];
        sudoku_mathrax_label(geo->mx_op[q], geo->mx_val[q], lab);
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        switch (geo->mx_op[q]) {
        case MX_SUM:
            snprintf(says, sizeof(says), "add up to %d", geo->mx_val[q]);
            break;
        case MX_DIFF:
            snprintf(says, sizeof(says), "are %d apart", geo->mx_val[q]);
            break;
        case MX_PROD:
            snprintf(says, sizeof(says), "multiply to %d", geo->mx_val[q]);
            break;
        case MX_QUOT:
            snprintf(says, sizeof(says), "are one %d times the other", geo->mx_val[q]);
            break;
        default:
            snprintf(says, sizeof(says), "are all %s", geo->mx_op[q] == MX_EVEN ? "even" : "odd");
            break;
        }
        snprintf(u1, sizeof(u1), "the %s circle", lab);
        snprintf(tech, sizeof(tech), "The %s circle: both diagonal pairs of its four cells %s. What the cell "
                 "across from row %d, column %d can be leaves it only %s. ", lab, says, c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_159) {              /* 159 Sudoku: the cell, and its row's columns 1, 5 and 9 */
        int c = t->unit;
        for (int k = 0; c >= 0 && c < 81 && k < 9; k += 4) {
            mark[c / 9 * 9 + k] = 1;
        }
        if (c >= 0 && c < 81) {
            mark[c] = 1;
        }
        snprintf(u1, sizeof(u1), "row %d", c / 9 + 1);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        snprintf(tech, sizeof(tech), "In row %d, the digit in column 1 is the column of its 1, in column 5 of its 5 "
                 "and in column 9 of its 9, so row %d, column %d can only be %s. ", c / 9 + 1, c / 9 + 1, c % 9 + 1,
                 digs);
    } else if (t->kind == TECH_DIAGNC) {            /* Diagonal Non-Consecutive: the cell and its corners */
        int c = t->unit;
        for (int k = 0; c >= 0 && c < 81 && k < 5; k++) {
            int r = c / 9 + (k == 4 ? 0 : k < 2 ? -1 : 1), q = c % 9 + (k == 4 ? 0 : k % 2 ? 1 : -1);
            if (r >= 0 && r < 9 && q >= 0 && q < 9) {
                mark[r * 9 + q] = 1;
            }
        }
        snprintf(u1, sizeof(u1), "row %d, column %d", c / 9 + 1, c % 9 + 1);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        snprintf(tech, sizeof(tech), geo->variant == VAR_DIAGCONSEC
                 ? "The cells touching row %d, column %d at its corners are 1 apart from it where a mark joins them, "
                 "and not where none does, which leaves it only %s. "
                 : "The cells touching row %d, column %d at its corners can't be 1 apart from it, "
                 "which leaves it only %s. ", c / 9 + 1, c % 9 + 1, digs);
    } else if (t->kind == TECH_CONSEC) {            /* Consecutive: the cell and its neighbours */
        int c = t->unit;
        if (c >= 0 && c < 81) {
            mark[c] = 1;
            if (c % 9 > 0) {
                mark[c - 1] = 1;
            }
            if (c % 9 < 8) {
                mark[c + 1] = 1;
            }
            if (c > 8) {
                mark[c - 9] = 1;
            }
            if (c < 72) {
                mark[c + 9] = 1;
            }
        }
        snprintf(u1, sizeof(u1), "row %d, column %d", c / 9 + 1, c % 9 + 1);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        if (geo->variant == VAR_NONCONSEC)
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
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        snprintf(u1, sizeof(u1), "row %d, column %d", c / 9 + 1, c % 9 + 1);
        if (t->kind == TECH_QUAD && t->unit2 >= 0 && t->unit2 < geo->nquads) {
            int at = geo->quad_at[t->unit2];
            mark[at] = mark[at + 1] = mark[at + 9] = mark[at + 10] = 1;
            char need[8] = "";
            for (int k = 0; k < geo->quad_n[t->unit2]; k++) {
                need[k] = (char)('0' + geo->quad_dig[t->unit2][k]);
            }
            snprintf(tech, sizeof(tech), "The circle with %s needs those digits among its four cells, which leaves "
                     "row %d, column %d only %s. ", need, c / 9 + 1, c % 9 + 1, digs);
        } else if (c >= 0 && c < 81) {
            mark[c] = 1;
            if (c % 9 > 0) {
                mark[c - 1] = 1;
            }
            if (c % 9 < 8) {
                mark[c + 1] = 1;
            }
            if (c > 8) {
                mark[c - 9] = 1;
            }
            if (c < 72) {
                mark[c + 9] = 1;
            }
            int ek = geo->edge_kind;
            snprintf(tech, sizeof(tech), "%s around row %d, column %d%s leave it only %s. ",
                     ek == EDGE_KROPKI ? "The dots" : ek == EDGE_XV ? "The X and V marks" :
                     ek == EDGE_FORT ? "The grey cells (bigger than their white neighbours)" : "The signs",
                     c / 9 + 1, c % 9 + 1, ek == EDGE_GT || ek == EDGE_FORT ? "" : " (and where there are none)", digs);
        }
    } else if (t->kind == TECH_CAGE) {              /* Killer: the cage's cells */
        int k = t->unit;
        for (int i = 0; k >= 0 && k < geo->ncages && i < geo->cage_n[k]; i++) {
            mark[geo->cage_cell[k][i]] = 1;
        }
        int c0 = k >= 0 && k < geo->ncages ? geo->cage_cell[k][0] : 0;
        const bool prod = geo->variant == VAR_PRODUCT;
        const int num = k >= 0 && k < geo->ncages ? (prod ? geo->cage_prod[k] : geo->cage_sum[k]) : 0;
        snprintf(u1, sizeof(u1), "the %d cage", num);
        char digs[24] = "";
        size_t len = 0;
        for (int d = 1; d <= 9; d++) {
            if (t->digit >> d & 1) {
                len += snprintf(digs + len, sizeof(digs) - len, "%s%d", len ? ", " : "", d);
            }
        }
        snprintf(tech, sizeof(tech), "The cage at row %d, column %d %s %d: with what's in it and around "
                 "it, its empty cells can only be %s. ", c0 / 9 + 1, c0 % 9 + 1, prod ? "multiplies to" : "adds up to",
                 num, digs);
    } else if (t->kind == TECH_NAKED_TRIPLE || t->kind == TECH_X_WING || t->kind == TECH_SWORDFISH ||
               t->kind == TECH_XY_WING) {
        for (int i = 0; i < t->ncells; i++) {
            mark[t->cells[i]] = 1;    /* the cells that make the pattern */
        }
        char digs[24];
        if (t->kind == TECH_NAKED_TRIPLE) {
            unit_name(geo, t->unit, u1, sizeof(u1));
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
                if (!(t->digit2 >> j & 1)) {
                    continue;
                }
                lc += snprintf(cl + lc, sizeof(cl) - lc, "%s%d", k == 0 ? "" : (k == nb - 1 ? " and " : ", "), j + 1);
                k++;
            }
            snprintf(u1, sizeof(u1), "%s %s", base, bl);
            snprintf(tech, sizeof(tech), "In %s %s, %d can only be in %s %s. %s %s must put their %ds in those %s, "
                     "so no other cell of %s %s can be %d. ", base, bl, t->digit, cov, cl,
                     nb == 2 ? "Both" : "All three", base, t->digit, cov, cov, cl, t->digit);
        }
    } else if (t->kind) {
        mark_unit(geo, mark, t->unit, 1);
        unit_name(geo, t->unit, u1, sizeof(u1));
        unit_name(geo, t->unit2 >= 0 ? t->unit2 : t->unit, u2, sizeof(u2));
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

    if (stage == 1) {
        char un[24];
        switch (h->kind) {
        case STEP_WRONG:
            snprintf(text, text_n, "Something's not right: check the highlighted cell.");
            break;
        case STEP_HIDDEN_SINGLE:
            if (t->kind) {
                snprintf(text, text_n, "Look at %s: it narrows down where a digit can go.", u1);
            } else {
                unit_name(geo, h->unit, un, sizeof(un));
                snprintf(text, text_n, "Look at %s: one digit has only one place left there.", un);
            }
            break;
        case STEP_NAKED_SINGLE:
            if (t->kind) {
                snprintf(text, text_n, "Look at %s, then at the highlighted cell.", u1);
            } else {
                snprintf(text, text_n, "Look at the highlighted cell: only one digit fits.");
            }
            break;
        default:
            snprintf(text, text_n, is_nudge ? "No simple step here: this needs advanced techniques."
                     : "No simple step here: this needs advanced techniques. More shows one cell.");
            break;
        }
        act = "More";
    } else {
        mark[h->cell] = 2;
        char un[24];
        switch (h->kind) {
        case STEP_WRONG:
            snprintf(text, text_n, "The %d at row %d, column %d is wrong: it isn't part of the solution.",
                     h->digit, r, c);
            break;
        case STEP_HIDDEN_SINGLE:
            unit_name(geo, h->unit, un, sizeof(un));
            snprintf(text, text_n, "%s%s%d can only go here in %s: its other empty cells already see a %d "
                     "in their %s.", tech, t->kind ? "That leaves one place: " : "",
                     h->digit, un, h->digit, units_word(geo));
            break;
        case STEP_NAKED_SINGLE:
            snprintf(text, text_n, "%sOnly %d fits here (row %d, column %d): every other digit is already "
                     "in its %s%s.", tech, h->digit, r, c, units_word(geo), t->kind ? ", or ruled out above" : "");
            break;
        default:
            snprintf(text, text_n, "The highlighted cell (row %d, column %d) is %d.", r, c, h->digit);
            break;
        }
        act = h->kind == STEP_WRONG ? "Erase it" : "Place it";
    }
    return act;
}

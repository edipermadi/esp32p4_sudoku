/*
 * The example puzzles of the variant pages (docs/variants/): one puzzle per variant, from
 * the generator, whose opening position has a single that the classic rules alone don't
 * give, only the variant's own rule (for Classic, Mini 6x6 and Kids 4x4: a hidden single in
 * a box). Everything in a worked example follows from the digits on the board and the
 * rules (is_sudoku_fits), no candidate chains, so it can be written out step by step.
 *
 * Writes the puzzles as an .sdx collection (import it on the board to follow along, and for
 * the screenshots) and prints, per variant, the puzzle, its solution and three steps with
 * the reason every other digit or cell is ruled out.
 *
 *   make -C tests examples            all of them: docs/variant-examples.sdx
 *   tests/variant_examples - kropki   just one variant, printed (nothing written)
 *   tests/variant_examples --json classic
 *                                     one variant's example solved to the end, a single at a
 *                                     time (a box's hidden single first, then a row's or
 *                                     column's, then a naked single), every step with its
 *                                     reasons, as JSON (tools/walkthrough.py draws and writes it)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "generator.h"
#include "sdx.h"

#define STEPS 3

static bool is_same_box(int a, int b, int bh, int bw)
{
    return a / 9 / bh == b / 9 / bh && a % 9 / bw == b % 9 / bw;
}

/* candidates by the classic rules only (row, column, box), against the filled digits */
static bool is_classic_fits(const uint8_t v[81], int c, int d, int n, int bh, int bw)
{
    for (int o = 0; o < 81; o++) {
        if (o == c || v[o] != d) {
            continue;
        }
        if (o / 9 == c / 9 || o % 9 == c % 9 || is_same_box(o, c, bh, bw)) {
            return false;
        }
    }
    return d >= 1 && d <= n && !v[c];
}

/* the units a hidden single can be in: rows, columns, boxes / regions, and the variant's */
static int units_of(const sudoku_geom_t *G, int *u)
{
    int n = 0;
    for (int i = 0; i < G->nunits; i++) {
        u[n++] = G->unit_ids[i];
    }
    return n;
}

static const char *unit_name(const sudoku_geom_t *G, int u, char *b)
{
    if (u < 9) {
        sprintf(b, "row %d", u + 1);
    } else if (u < 18) {
        sprintf(b, "column %d", u - 8);
    } else if (u < 27) {
        sprintf(b, "%s %d", G->variant == VAR_JIGSAW ? "region" : "box", u - 17);
    } else if (u == UNIT_DIAG_MAIN) {
        sprintf(b, "the main diagonal");
    } else if (u == UNIT_DIAG_ANTI) {
        sprintf(b, "the anti-diagonal");
    } else if (u < UNIT_GROUP) {
        sprintf(b, "window %d", u - UNIT_WINDOW + 1);
    } else if (u < UNIT_ASTERISK) {
        sprintf(b, "disjoint group %d", u - UNIT_GROUP + 1);
    } else if (u == UNIT_ASTERISK) {
        sprintf(b, "the asterisk");
    } else if (u == UNIT_GIRANDOLA) {
        sprintf(b, "the pinwheel");
    } else {
        sprintf(b, "the box centres");
    }
    return b;
}

static bool is_in_unit(const sudoku_geom_t *G, int u, int c)
{
    for (int k = 0; k < G->n; k++) {
        if (G->unit[u][k] == c) {
            return true;
        }
    }
    return false;
}

/* why d can't go in c: a filled peer (and through which unit / move), or the variant's rule */
static void why(const sudoku_geom_t *G, const uint8_t v[81], int c, int d, char *b)
{
    int u[64], nu = units_of(G, u);
    char ub[40];
    for (int i = 0; i < nu; i++) {                 /* the same row, column, box or extra unit */
        if (!is_in_unit(G, u[i], c)) {
            continue;
        }
        for (int k = 0; k < G->n; k++) {
            int o = G->unit[u[i]][k];
            if (o != c && v[o] == d) {
                sprintf(b, "r%dc%d is %d (%s)", o / 9 + 1, o % 9 + 1, d, unit_name(G, u[i], ub));
                return;
            }
        }
    }
    for (int k = 0; k < G->nxpeers[c]; k++) {      /* Anti-Knight, Anti-King */
        int o = G->xpeers[c][k];
        if (v[o] == d) {
            sprintf(b, "r%dc%d is %d (%s)", o / 9 + 1, o % 9 + 1, d,
                    G->variant == VAR_KNIGHT || (G->variant == VAR_CHESS && abs(o / 9 - c / 9) + abs(o % 9 - c % 9) == 3)
                    ? "a knight's move away" : G->variant == VAR_ARGYLE ? "on an argyle line with it"
                    : "touching, corner to corner");
            return;
        }
    }
    for (int i = 0; (G->variant == VAR_SUMLESS || G->variant == VAR_REGIONS) && G->cage_of[c] != CAGE_NONE && i < G->cage_n[G->cage_of[c]]; i++) {
        int o = G->cage_cell[G->cage_of[c]][i];     /* Sumless Killer: a cage-mate in another row, column and box */
        if (o != c && v[o] == d) {
            sprintf(b, "r%dc%d is %d (in the same %s)", o / 9 + 1, o % 9 + 1, d,
                    G->variant == VAR_REGIONS ? "shaded region" : "cage");
            return;
        }
    }
    if ((G->parity[c] == PARITY_EVEN || G->parity[c] == PARITY_ODD) && !(G->allow[c] >> d & 1)) {
        sprintf(b, "the cell is %s", G->parity[c] == PARITY_EVEN ? "even" : "odd");
        return;
    }
    sprintf(b, "RULE");
}

/* why() as data: the filled cell that rules d out of c (-1: none) and through what, in words
 * ("row 2", "box 1", "the anti-diagonal", "a knight's move", "even", "RULE") */
static int blocker(const sudoku_geom_t *G, const uint8_t v[81], int c, int d, char *via)
{
    int u[64], nu = units_of(G, u);
    for (int i = 0; i < nu; i++) {
        if (!is_in_unit(G, u[i], c)) {
            continue;
        }
        for (int k = 0; k < G->n; k++) {
            int o = G->unit[u[i]][k];
            if (o != c && v[o] == d) {
                unit_name(G, u[i], via);
                return o;
            }
        }
    }
    for (int k = 0; k < G->nxpeers[c]; k++) {
        int o = G->xpeers[c][k];
        if (v[o] == d) {
            strcpy(via, G->variant == VAR_KNIGHT || (G->variant == VAR_CHESS && abs(o / 9 - c / 9) + abs(o % 9 - c % 9) == 3)
                   ? "a knight's move" : G->variant == VAR_ARGYLE ? "an argyle line"
                   : "a touching cell");
            return o;
        }
    }
    for (int i = 0; (G->variant == VAR_SUMLESS || G->variant == VAR_REGIONS) && G->cage_of[c] != CAGE_NONE && i < G->cage_n[G->cage_of[c]]; i++) {
        int o = G->cage_cell[G->cage_of[c]][i];
        if (o != c && v[o] == d) {
            strcpy(via, G->variant == VAR_REGIONS ? "the same region" : "the same cage");
            return o;
        }
    }
    if ((G->parity[c] == PARITY_EVEN || G->parity[c] == PARITY_ODD) && !(G->allow[c] >> d & 1)) {
        strcpy(via, G->parity[c] == PARITY_EVEN ? "even" : "odd");
        return -1;
    }
    strcpy(via, "RULE");
    return -1;
}

/* the variant's features around a cell, to explain a RULE */
static void context(const sudoku_geom_t *G, const uint8_t v[81], int c)
{
    int r = c / 9, q = c % 9;
    const int nb[4] = {q > 0 ? c - 1 : -1, q < 8 ? c + 1 : -1, r > 0 ? c - 9 : -1, r < 8 ? c + 9 : -1};
    static const char *const SIDE[4] = {"left", "right", "above", "below"};
    if (G->edge_kind || G->consec_on) {
        for (int k = 0; k < 4; k++) {
            if (nb[k] < 0) {
                continue;
            }
            int m;
            if (G->edge_kind)
                m = k == 0 ? EDGE_RIGHT(G->edge[c - 1]) : k == 1 ? EDGE_RIGHT(G->edge[c])
                    : k == 2 ? EDGE_DOWN(G->edge[c - 9]) : EDGE_DOWN(G->edge[c]);
            else
                m = k == 0 ? (G->consec[c - 1] & CONSEC_RIGHT) != 0 : k == 1 ? (G->consec[c] & CONSEC_RIGHT) != 0
                    : k == 2 ? (G->consec[c - 9] & CONSEC_DOWN) != 0 : (G->consec[c] & CONSEC_DOWN) != 0;
            printf("         %s r%dc%d (%c): mark %d\n", SIDE[k], nb[k] / 9 + 1, nb[k] % 9 + 1,
                   v[nb[k]] ? '0' + v[nb[k]] : '.', m);
        }
    }
    if (G->cage_of[c] != CAGE_NONE) {
        int k = G->cage_of[c];
        if (G->cage_prod[k]) {
            printf("         cage product %d:", G->cage_prod[k]);
        } else {
            printf("         cage sum %d:", G->cage_sum[k]);
        }
        for (int i = 0; i < G->cage_n[k]; i++) {
            int o = G->cage_cell[k][i];
            printf(" r%dc%d(%c)", o / 9 + 1, o % 9 + 1, v[o] ? '0' + v[o] : '.');
        }
        printf("\n");
    }
    if (G->thermo_of[c] != THERMO_NONE) {
        int t = G->thermo_of[c];
        printf("         thermometer (bulb first):");
        for (int i = 0; i < G->thermo_n[t]; i++) {
            int o = G->thermo_cell[t][i];
            printf(" r%dc%d(%c)", o / 9 + 1, o % 9 + 1, v[o] ? '0' + v[o] : '.');
        }
        printf("  (this cell is #%d)\n", G->thermo_pos[c] + 1);
    }
    for (int s = 0; s < 4 && G->nquads && G->quad_of[c][s] != QUAD_NONE; s++) {
        int qd = G->quad_of[c][s], at = G->quad_at[qd];
        printf("         circle at r%dc%d-r%dc%d:", at / 9 + 1, at % 9 + 1, at / 9 + 2, at % 9 + 2);
        for (int i = 0; i < G->quad_n[qd]; i++) {
            printf("%d", G->quad_dig[qd][i]);
        }
        printf("  cells: %c%c%c%c\n", v[at] ? '0' + v[at] : '.', v[at + 1] ? '0' + v[at + 1] : '.',
               v[at + 9] ? '0' + v[at + 9] : '.', v[at + 10] ? '0' + v[at + 10] : '.');
    }
    for (int s = 0; s < 4 && G->nmx && G->mx_of[c][s] != MX_NONE; s++) {
        int qd = G->mx_of[c][s], at = G->mx_at[qd];
        char lab[8];
        sudoku_mathrax_label(G->mx_op[qd], G->mx_val[qd], lab);
        printf("         circle %s at r%dc%d-r%dc%d  cells: %c%c%c%c\n", lab, at / 9 + 1, at % 9 + 1, at / 9 + 2,
               at % 9 + 2, v[at] ? '0' + v[at] : '.', v[at + 1] ? '0' + v[at + 1] : '.',
               v[at + 9] ? '0' + v[at + 9] : '.', v[at + 10] ? '0' + v[at + 10] : '.');
    }
    if (G->sandwich) {
        printf("         sandwich: row %d sum %d, column %d sum %d\n", r + 1, G->sandwich_sum[r], q + 1,
               G->sandwich_sum[9 + q]);
    }
}

typedef struct { int cell, digit, unit; bool naked, rule; } single_t;

/* the singles of position v by the rules against the filled digits (is_sudoku_fits); classic:
 * by the classic rules only */
static int singles(const sudoku_geom_t *G, const uint8_t v[81], bool classic, single_t *out)
{
    int n = 0;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k], only = 0, cnt = 0;
        if (v[c]) {
            continue;
        }
        for (int d = 1; d <= G->n; d++) {
            bool ok = classic ? is_classic_fits(v, c, d, G->n, G->box_h, G->box_w) : is_sudoku_fits(G, v, c, d);
            if (ok) {
                only = d;
                cnt++;
            }
        }
        if (cnt == 1)
            out[n++] = (single_t) {c, only, -1, true, false};
    }
    int u[64], nu = classic ? 27 : units_of(G, u);
    if (classic) {
        for (int i = 0; i < 27; i++) {
            u[i] = i;
        }
    }
    for (int i = 0; i < nu; i++) {
        if (classic && u[i] >= 18 + (G->n / G->box_w) * (G->n / G->box_h)) {
            continue;
        }
        if (classic && ((u[i] < 9 && u[i] >= G->n) || (u[i] >= 9 && u[i] < 18 && u[i] - 9 >= G->n))) {
            continue;
        }
        for (int d = 1; d <= G->n; d++) {
            int where = -1, cnt = 0;
            bool placed = false;
            for (int k = 0; k < G->n; k++) {
                int c;
                if (classic) {
                    if (u[i] < 9) {
                        c = u[i] * 9 + k;
                    } else if (u[i] < 18) {
                        c = k * 9 + (u[i] - 9);
                    } else {
                        int b = u[i] - 18, across = G->n / G->box_w;
                        c = ((b / across) * G->box_h + k / G->box_w) * 9 + (b % across) * G->box_w + k % G->box_w;
                    }
                } else {
                    c = G->unit[u[i]][k];
                }
                if (v[c] == d) {
                    placed = true;
                    break;
                }
                bool ok = !v[c] && (classic ? is_classic_fits(v, c, d, G->n, G->box_h, G->box_w) : is_sudoku_fits(G, v, c, d));
                if (ok) {
                    where = c;
                    cnt++;
                }
            }
            if (!placed && cnt == 1)
                out[n++] = (single_t) {where, d, u[i], false, false};
        }
    }
    return n;
}

/* the single s is also a classic one (the same cell and digit, any kind) */
static bool classic_has(const single_t *cl, int ncl, const single_t *s)
{
    for (int i = 0; i < ncl; i++) {
        if (cl[i].cell == s->cell && cl[i].digit == s->digit) {
            return true;
        }
    }
    return false;
}

/* how many reasons of the single are the variant's own rule (RULE), and how many are its
 * extra units / moves / parity (anything but a classic row, column or box) */
static int rule_count(const sudoku_geom_t *G, const uint8_t v[81], const single_t *s, int *special)
{
    char b[96];
    int rules = 0;
    *special = 0;
    if (s->naked) {
        for (int d = 1; d <= G->n; d++) {
            if (d == s->digit) {
                continue;
            }
            why(G, v, s->cell, d, b);
            if (!strcmp(b, "RULE")) {
                rules++;
            } else if (!strstr(b, "(row") && !strstr(b, "(column") && !strstr(b, "(box")) {
                (*special)++;
            }
        }
    } else {
        for (int k = 0; k < G->n; k++) {
            int o = G->unit[s->unit][k];
            if (o == s->cell || v[o]) {
                continue;
            }
            why(G, v, o, s->digit, b);
            if (!strcmp(b, "RULE")) {
                rules++;
            } else if (!strstr(b, "(row") && !strstr(b, "(column") && !strstr(b, "(box")) {
                (*special)++;
            }
        }
    }
    return rules;
}

static void print_step(const sudoku_geom_t *G, const uint8_t v[81], const single_t *s, int i)
{
    char ub[40], b[96];
    printf("   step %d: %s r%dc%d = %d", i, s->naked ? "naked single" : "hidden single", s->cell / 9 + 1,
           s->cell % 9 + 1, s->digit);
    if (!s->naked) {
        printf(" (the only place for %d in %s)", s->digit, unit_name(G, s->unit, ub));
    }
    printf("\n");
    if (s->naked) {
        for (int d = 1; d <= G->n; d++) {
            if (d == s->digit) {
                continue;
            }
            why(G, v, s->cell, d, b);
            printf("      %d: %s\n", d, b);
        }
        context(G, v, s->cell);
    } else {
        for (int k = 0; k < G->n; k++) {
            int o = G->unit[s->unit][k];
            if (o == s->cell) {
                continue;
            }
            if (v[o]) {
                printf("      r%dc%d: has %d\n", o / 9 + 1, o % 9 + 1, v[o]);
                continue;
            }
            why(G, v, o, s->digit, b);
            printf("      r%dc%d: %s\n", o / 9 + 1, o % 9 + 1, b);
            if (!strcmp(b, "RULE")) {
                context(G, v, o);
            }
        }
    }
}

static bool is_plain(int v) { return v == VAR_CLASSIC || v == VAR_MINI || v == VAR_KIDS; }

/* the best opening single of position v for variant var: not a classic one (plain variants:
 * a hidden single in a box), with the fewest reasons that need the variant's rule spelled out
 * (at least one, or an extra unit / move); -1 if none. *score: lower is better */
static int pick(int var, const sudoku_geom_t *G, const uint8_t v[81], single_t *best, int *score)
{
    static single_t s[2048], cl[2048];
    int n = singles(G, v, false, s), ncl = singles(G, v, true, cl), got = -1;
    *score = 1 << 30;
    for (int i = 0; i < n; i++) {
        int special, rules = rule_count(G, v, &s[i], &special), sc;
        if (is_plain(var)) {
            if (s[i].naked || s[i].unit < 18) {
                continue;
            }
            sc = 0;
        } else {
            if (classic_has(cl, ncl, &s[i]) || rules + special == 0) {
                continue;
            }
            sc = (rules > 3 ? 10 * rules : rules) + (s[i].naked ? 0 : 1);
        }
        if (sc < *score) {
            *score = sc;
            *best = s[i];
            got = i;
        }
    }
    return got;
}

/* singles alone (by the rules against the filled digits) solve the puzzle to the end: the
 * walkthrough can explain every step */
static bool singles_solve(const sudoku_geom_t *G, const uint8_t giv[81])
{
    static single_t s[2048];
    uint8_t v[81];
    memcpy(v, giv, 81);
    for (;;) {
        int n = singles(G, v, false, s);
        if (!n) {
            break;
        }
        v[s[0].cell] = (uint8_t)s[0].digit;
    }
    for (int k = 0; k < G->ncells; k++) {
        if (!v[G->cells[k]]) {
            return false;
        }
    }
    return true;
}

static void layout_texts(const sudoku_geom_t *g, char regions[82], char *cages, char *marks, char *thermos, int len)
{
    regions[0] = cages[0] = marks[0] = thermos[0] = 0;
    switch (g->variant) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) {
            regions[i] = (char)('0' + g->region[i]);
        }
        regions[81] = 0;
        break;
    case VAR_KILLER:
    case VAR_SUMLESS:
    case VAR_PRODUCT:
    case VAR_REGIONS:
        sudoku_cages_text(g, cages, len);
        break;
    case VAR_QUAD:
        sudoku_quads_text(g, cages, len);
        break;
    case VAR_MATHRAX:
        sudoku_mathrax_text(g, cages, len);
        break;
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
    case VAR_FORTRESS:
    case VAR_DIAGCONSEC:
        sudoku_marks_text(g, marks, len);
        break;
    case VAR_SANDWICH:
        sudoku_sandwich_text(g, marks, len);
        break;
    case VAR_SKY:
        sudoku_sky_text(g, marks, len);
        break;
    case VAR_XSUM:
        sudoku_xsum_text(g, marks, len);
        break;
    case VAR_NROOMS:
        sudoku_nroom_text(g, marks, len);
        break;
    case VAR_SKYSUM:
        sudoku_skysum_text(g, marks, len);
        break;
    case VAR_B9:
        sudoku_b9_text(g, marks, len);
        break;
    case VAR_N9:
        sudoku_n9_text(g, marks, len);
        break;
    case VAR_CLONE:
        sudoku_clone_text(g, cages, len);
        break;
    case VAR_PSKY:
        sudoku_psky_text(g, marks, len);
        break;
    case VAR_FRAME:
        sudoku_frame_text(g, marks, len);
        break;
    case VAR_OUTSIDE:
        sudoku_outside_text(g, marks, len);
        break;
    case VAR_ROSSINI:
        sudoku_rossini_text(g, marks, len);
        break;
    case VAR_LK:
        sudoku_lk_text(g, cages, len);
        break;
    case VAR_THERMO:
    case VAR_ARROW:
    case VAR_RENBAN:
    case VAR_WHISPER:
    case VAR_PALINDROME:
    case VAR_REGIONSUM:
    case VAR_ZIPPER:
    case VAR_BETWEEN:
    case VAR_ENTROPIC:
    case VAR_MODULAR:
    case VAR_DUTCH:
    case VAR_PARITY:
    case VAR_SLOW:
    case VAR_NABNER:
    case VAR_LOCKOUT:
    case VAR_DARROW:
    case VAR_TEN:
    case VAR_PILL:
    case VAR_AVG:
        sudoku_thermos_text(g, thermos, len);
        break;
    default:
        break;
    }
}

/* a rule's reason for d not going in c, as JSON fields (after "via": "RULE") */
static void rule_json(const sudoku_geom_t *G, const uint8_t v[81], int c, int d)
{
    int why = sudoku_fits_why(G, v, c, d), r = c / 9, q = c % 9;
    const int nb[4] = {q > 0 ? c - 1 : -1, q < 8 ? c + 1 : -1, r > 0 ? c - 9 : -1, r < 8 ? c + 9 : -1};
    switch (why) {
    case FIT_CONSEC:
    case FIT_EDGE:                 /* the neighbour whose mark (or none) rules it out */
        for (int k = 0; k < 4; k++) {
            int o = nb[k];
            if (o < 0 || !v[o]) {
                continue;
            }
            bool first = k == 0 || k == 2;          /* o is left of / above c */
            int a = first ? v[o] : d, b = first ? d : v[o], m;
            int top = first ? o : c;
            if (why == FIT_CONSEC) {
                m = (k < 2 ? G->consec[top] & CONSEC_RIGHT : G->consec[top] & CONSEC_DOWN) != 0;
                if ((abs(a - b) == 1) == (m != 0)) {
                    continue;
                }
            } else {
                m = k < 2 ? EDGE_RIGHT(G->edge[top]) : EDGE_DOWN(G->edge[top]);
                if (is_sudoku_edge_ok(G->edge_kind, m, a, b)) {
                    continue;
                }
            }
            printf(", \"rule\": \"%s\", \"with\": %d, \"mark\": %d, \"cell_first\": %s",
                   why == FIT_CONSEC ? "consec" : G->edge_kind == EDGE_KROPKI ? "kropki" : G->edge_kind == EDGE_XV ? "xv" :
                   G->edge_kind == EDGE_FORT ? "fortress" : "gt",
                   o, m, first ? "false" : "true");
            return;
        }
        printf(", \"rule\": \"marks\"");
        return;
    case FIT_ALLOW: {                               /* Greater-Than: the range from the chains of signs */
        int lo = 0, hi = 0;
        for (int x = 1; x <= 9; x++) {
            if (G->allow[c] >> x & 1) {
                if (!lo) {
                    lo = x;
                }
                hi = x;
            }
        }
        printf(", \"rule\": \"range\", \"lo\": %d, \"hi\": %d", lo, hi);
        return;
    }
    case FIT_THERMO: {
        int t = G->thermo_of[c], p = G->thermo_pos[c];
        if (G->line_kind != LINE_THERMO && G->line_kind != LINE_SLOW) {   /* Arrow, Renban, Whispers, ...: the line (and a neighbour) */
            int o = -1, m = G->thermo_n[t] - 1 - p;
            if ((G->line_kind == LINE_PALINDROME || G->line_kind == LINE_ZIPPER) && m != p && v[G->thermo_cell[t][m]]) {
                o = G->thermo_cell[t][m];             /* (the cell mirroring it) */
            }
            const bool nbr = G->line_kind == LINE_WHISPER || G->line_kind == LINE_DUTCH || G->line_kind == LINE_PARITY;
            for (int k = -1; nbr && k <= 1; k += 2) {   /* the neighbour on it that rules d out */
                int q = p + k, x = q >= 0 && q < G->thermo_n[t] ? v[G->thermo_cell[t][q]] : 0;
                if (x && (G->line_kind == LINE_PARITY ? !((x ^ d) & 1)
                          : abs(x - d) < (G->line_kind == LINE_DUTCH ? 4 : 5))) {
                    o = G->thermo_cell[t][q];
                }
            }
            for (int q = 0; G->line_kind == LINE_NABNER && o < 0 && q < G->thermo_n[t]; q++) {   /* Nabner: any cell on it */
                int x = q != p ? v[G->thermo_cell[t][q]] : 0;
                if (x && abs(x - d) < 2) {
                    o = G->thermo_cell[t][q];
                }
            }
            printf(", \"rule\": \"line\", \"line\": %d, \"pos\": %d, \"with\": %d", t, p, o);
            return;
        }
        int before = p > 0 ? G->thermo_cell[t][p - 1] : -1, after = p + 1 < G->thermo_n[t] ? G->thermo_cell[t][p + 1] : -1;
        /* (Slow Thermo: the same digit is fine where the two don't see each other) */
        const int up = p > 0 ? sudoku_thermo_step(G, t, p) : 1, down = after >= 0 ? sudoku_thermo_step(G, t, p + 1) : 1;
        int o = before >= 0 && v[before] && d < v[before] + up ? before : after;
        printf(", \"rule\": \"thermo\", \"with\": %d, \"after\": %s, \"same_ok\": %s", o, o == before ? "true" : "false",
               (o == before ? up : down) ? "false" : "true");
        return;
    }
    case FIT_CAGE:
        printf(", \"rule\": \"cage\", \"cage\": %d", G->cage_of[c]);
        return;
    case FIT_159:
        printf(", \"rule\": \"159\"");
        return;
    case FIT_QUAD:
        for (int s = 0; s < 4 && G->quad_of[c][s] != QUAD_NONE; s++) {
            int qd = G->quad_of[c][s], empty, miss = sudoku_quad_missing(G, qd, v, &empty);
            if (miss >= empty) {
                printf(", \"rule\": \"quad\", \"quad\": %d", qd);
                return;
            }
        }
        printf(", \"rule\": \"quad\", \"quad\": %d", G->quad_of[c][0]);
        return;
    case FIT_SKY_ROW:
    case FIT_SKY_COL:
        printf(", \"rule\": \"sky\", \"line\": %d", why == FIT_SKY_ROW ? r : 9 + q);
        return;
    case FIT_FRAME_ROW:
    case FIT_FRAME_COL:
        printf(", \"rule\": \"frame\", \"line\": %d", why == FIT_FRAME_ROW ? r : 9 + q);
        return;
    case FIT_OUTSIDE_ROW:
    case FIT_OUTSIDE_COL:
        printf(", \"rule\": \"outside\", \"line\": %d", why == FIT_OUTSIDE_ROW ? r : 9 + q);
        return;
    case FIT_ROSSINI_ROW:
    case FIT_ROSSINI_COL:
        printf(", \"rule\": \"rossini\", \"line\": %d", why == FIT_ROSSINI_ROW ? r : 9 + q);
        return;
    case FIT_DIAGNC:
        for (int k = 0; k < 4; k++) {               /* the corner cell 1 away (marked: not 1 away) */
            int rr = r + (k < 2 ? -1 : 1), cc = q + (k % 2 ? 1 : -1);
            if (rr < 0 || rr > 8 || cc < 0 || cc > 8 || !v[rr * 9 + cc]) {
                continue;
            }
            const bool marked = is_sudoku_dmarked(G, c, k);
            if ((abs(v[rr * 9 + cc] - d) == 1) != marked) {
                printf(", \"rule\": \"diagnc\", \"with\": %d, \"marked\": %s", rr * 9 + cc, marked ? "true" : "false");
                return;
            }
        }
        printf(", \"rule\": \"diagnc\", \"with\": -1");
        return;
    case FIT_XSUM_ROW:
    case FIT_XSUM_COL:
        printf(", \"rule\": \"xsum\", \"line\": %d", why == FIT_XSUM_ROW ? r : 9 + q);
        return;
    case FIT_NROOM_ROW:
    case FIT_NROOM_COL:
        printf(", \"rule\": \"nroom\", \"line\": %d", why == FIT_NROOM_ROW ? r : 9 + q);
        return;
    case FIT_PSKY_ROW:
    case FIT_PSKY_COL:
        printf(", \"rule\": \"psky\", \"line\": %d", why == FIT_PSKY_ROW ? r : 9 + q);
        return;
    case FIT_ANTIDIAG:
        printf(", \"rule\": \"antidiag\"");
        return;
    case FIT_FLATMATES:
        printf(", \"rule\": \"flatmates\"");
        return;
    case FIT_MATHRAX:
        for (int s = 0; s < 4 && G->mx_of[c][s] != MX_NONE; s++) {   /* the circle whose partner rules d out */
            const int qd = G->mx_of[c][s], at = G->mx_at[qd];
            const int across = c == at ? at + 10 : c == at + 10 ? at : c == at + 1 ? at + 9 : at + 1;
            if (v[across] ? !(G->mx_mask[qd][d] >> v[across] & 1) : !G->mx_mask[qd][d]) {
                printf(", \"rule\": \"mathrax\", \"circle\": %d, \"across\": %d", qd, across);
                return;
            }
        }
        printf(", \"rule\": \"mathrax\", \"circle\": %d, \"across\": -1", G->mx_of[c][0]);
        return;
    case FIT_MAGIC:
        printf(", \"rule\": \"magic\"");
        return;
    case FIT_SKYSUM_ROW:
    case FIT_SKYSUM_COL:
        printf(", \"rule\": \"skysum\", \"line\": %d", why == FIT_SKYSUM_ROW ? r : 9 + q);
        return;
    case FIT_B9_ROW:
    case FIT_B9_COL:
        printf(", \"rule\": \"b9\", \"line\": %d", why == FIT_B9_ROW ? r : 9 + q);
        return;
    case FIT_N9_ROW:
    case FIT_N9_COL:
        printf(", \"rule\": \"n9\", \"line\": %d", why == FIT_N9_ROW ? r : 9 + q);
        return;
    case FIT_CLONE:
        printf(", \"rule\": \"clone\", \"with\": %d", G->clone_of[c]);
        return;
    case FIT_QUADRO:
        printf(", \"rule\": \"quadro\"");
        return;
    case FIT_AQ9:
        printf(", \"rule\": \"aq9\"");
        return;
    case FIT_PSYM:
        printf(", \"rule\": \"psym\", \"with\": %d", 80 - c);
        return;
    case FIT_LK:
        for (int s = 0; s < 2; s++) {               /* the diagonal whose sum says no */
            int k = G->lk_of[c][s], sum = 0, empty = 0;
            if (k == LK_NONE) {
                continue;
            }
            for (int j = 0; j < G->lk_n[k]; j++) {
                int o = G->lk_cell[k][j];
                if (o == c) {
                    continue;
                }
                if (v[o]) {
                    sum += v[o];
                } else {
                    empty++;
                }
            }
            int left = G->lk_sum[k] - sum - d;
            if (left < empty || left > 9 * empty) {
                printf(", \"rule\": \"lk\", \"diag\": %d", k);
                return;
            }
        }
        printf(", \"rule\": \"lk\", \"diag\": %d", G->lk_of[c][0] != LK_NONE ? G->lk_of[c][0] : G->lk_of[c][1]);
        return;
    case FIT_SANDWICH_ROW:
    case FIT_SANDWICH_COL:
        printf(", \"rule\": \"sandwich\", \"line\": %d", why == FIT_SANDWICH_ROW ? r : 9 + q);
        return;
    default:
        printf(", \"rule\": \"other\", \"code\": %d, \"fits\": %d", why, is_sudoku_fits(G, v, c, d));
    }
}

/* the board's layout as JSON fields: tinted cells, regions, marks, cages, thermometers, circles, sums */
static void layout_json(const sudoku_geom_t *G)
{
    printf(" \"region\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", G->used[i] ? G->region[i] : -1);
    }
    printf("],\n \"tint\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", is_sudoku_geom_tinted(G, i));
    }
    printf("],\n \"parity\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", G->parity[i]);
    }
    printf("],\n \"consec_on\": %s, \"consec\": [", G->consec_on ? "true" : "false");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", G->consec[i]);
    }
    printf("],\n \"dmark\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", G->dmark[i]);
    }
    printf("],\n \"edge_kind\": %d, \"edge\": [", G->edge_kind);
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", G->edge[i]);
    }
    printf("],\n \"cages\": [");
    for (int k = 0; k < G->ncages; k++) {
        printf("%s{\"sum\": %d, \"prod\": %d, \"cells\": [", k ? ", " : "", G->cage_sum[k], G->cage_prod[k]);
        for (int j = 0; j < G->cage_n[k]; j++) {
            printf("%s%d", j ? "," : "", G->cage_cell[k][j]);
        }
        printf("]}");
    }
    printf("],\n \"line_kind\": %d, \"thermos\": [", G->line_kind);
    for (int t = 0; t < G->nthermos; t++) {
        printf("%s[", t ? ", " : "");
        for (int j = 0; j < G->thermo_n[t]; j++) {
            printf("%s%d", j ? "," : "", G->thermo_cell[t][j]);
        }
        printf("]");
    }
    printf("],\n \"quads\": [");
    for (int qd = 0; qd < G->nquads; qd++) {
        printf("%s{\"at\": %d, \"digits\": [", qd ? ", " : "", G->quad_at[qd]);
        for (int j = 0; j < G->quad_n[qd]; j++) {
            printf("%s%d", j ? "," : "", G->quad_dig[qd][j]);
        }
        printf("]}");
    }
    printf("],\n \"flatmates\": %s, \"mathrax\": [", G->flatmates ? "true" : "false");
    for (int qd = 0; qd < G->nmx; qd++) {
        char lab[8];
        sudoku_mathrax_label(G->mx_op[qd], G->mx_val[qd], lab);
        printf("%s{\"at\": %d, \"op\": %d, \"val\": %d, \"label\": \"%s\"}", qd ? ", " : "", G->mx_at[qd],
               G->mx_op[qd], G->mx_val[qd], lab);
    }
    printf("],\n \"sky\": ");
    if (G->skyscraper) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->sky[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"xsum\": ");
    if (G->xsums) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->xsum[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"nroom\": ");
    if (G->nrooms) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->nroom[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"psky\": ");                     /* (odd: 1..5; even: 16 + 0..4) */
    if (G->psky) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->psky_clue[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"n9\": ");
    if (G->n9) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->n9_set[lu] << 1);   /* (as Outside's: bit d = d) */
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"clone\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", G->clone_pair[i] == CLONE_NONE ? -1 : G->clone_pair[i]);
    }
    printf("],\n \"clone_of\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", G->clone_of[i] == CLONE_NONE ? -1 : G->clone_of[i]);
    }
    printf("]");
    printf(",\n \"psym\": %s, \"b9\": ", G->psym ? "true" : "false");
    if (G->b9) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->b9_sum[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"skysum\": ");
    if (G->skysum) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->sky_sum[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"frame\": ");
    if (G->frame) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->frame_sum[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"outside\": ");
    if (G->outside) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->outside_set[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"rossini\": ");
    if (G->rossini) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->rossini_dir[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n \"lk\": [");
    for (int k = 0; k < G->nlk; k++) {
        printf("%s{\"sum\": %d, \"dir\": %d, \"top\": %s, \"cells\": [", k ? ", " : "", G->lk_sum[k], G->lk_dir[k],
               G->lk_from_top[k] ? "true" : "false");
        for (int j = 0; j < G->lk_n[k]; j++) {
            printf("%s%d", j ? "," : "", G->lk_cell[k][j]);
        }
        printf("]}");
    }
    printf("],\n \"sandwich\": ");
    if (G->sandwich) {
        printf("[");
        for (int lu = 0; lu < 18; lu++) {
            printf("%s%d", lu ? "," : "", G->sandwich_sum[lu]);
        }
        printf("]");
    } else {
        printf("null");
    }
    printf(",\n");
}

/* the example of variant var (as main() picks it), solved to the end as JSON on stdout */
static void walkthrough(int var, const sudoku_geom_t *G, const uint8_t giv[81], int level, uint32_t seed)
{
    uint8_t v[81], sol[81];
    memcpy(v, giv, 81);
    sudoku_solve(G, giv, sol);
    char via[48];
    printf("{\"variant\": \"%s\", \"level\": \"%s\", \"seed\": %u, \"n\": %d, \"box_h\": %d, \"box_w\": %d,\n",
           variant_name(var), gen_level_name(level), (unsigned)seed, G->n, G->box_h, G->box_w);
    printf(" \"givens\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", giv[i]);
    }
    printf("],\n \"solution\": [");
    for (int i = 0; i < 81; i++) {
        printf("%s%d", i ? "," : "", sol[i]);
    }
    printf("],\n");
    layout_json(G);
    printf(" \"steps\": [\n");
    static single_t s[2048];
    for (int step = 0;; step++) {
        int n = singles(G, v, false, s), best = -1, rank = 9;
        for (int i = 0; i < n; i++) {                   /* a box's hidden single, a line's, a naked one */
            int r = s[i].naked ? 2 : s[i].unit >= 18 && s[i].unit < 27 ? 0 : 1;
            if (r < rank) {
                rank = r;
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        single_t *t = &s[best];
        if (sol[t->cell] != t->digit) {
            fprintf(stderr, "a wrong single?\n");
            exit(1);
        }
        char un[40] = "";
        if (!t->naked) {
            unit_name(G, t->unit, un);
        }
        printf("  %s{\"cell\": %d, \"digit\": %d, \"kind\": \"%s\", \"unit\": %d, \"unit_name\": \"%s\", \"area\": [",
               step ? "," : "", t->cell, t->digit, t->naked ? "naked" : "hidden", t->naked ? -1 : t->unit, un);
        bool in[81] = {false};                          /* what the picture tints: the unit, or the cell's units */
        if (!t->naked)
            for (int k = 0; k < G->n; k++) {
                in[G->unit[t->unit][k]] = true;
            } else
            for (int j = 0; j < G->ncu[t->cell]; j++) {
                for (int k = 0; k < G->n; k++) {
                    in[G->unit[G->cu[t->cell][j]][k]] = true;
                }
            }
        for (int i = 0, f = 1; i < 81; i++) {
            if (in[i]) {
                printf("%s%d", f ? "" : ",", i);
                f = 0;
            }
        }
        printf("], \"reasons\": [");
        bool first = true;
        if (t->naked) {                                 /* each other digit: who rules it out */
            for (int d = 1; d <= G->n; d++) {
                if (d == t->digit) {
                    continue;
                }
                int b = blocker(G, v, t->cell, d, via);
                printf("%s{\"digit\": %d, \"by\": %d, \"via\": \"%s\"", first ? "" : ", ", d, b, via);
                if (!strcmp(via, "RULE")) {
                    rule_json(G, v, t->cell, d);
                }
                printf("}");
                first = false;
            }
        } else {                                        /* each other cell of the unit */
            for (int k = 0; k < G->n; k++) {
                int o = G->unit[t->unit][k];
                if (o == t->cell) {
                    continue;
                }
                if (v[o]) {
                    printf("%s{\"cell\": %d, \"filled\": %d}", first ? "" : ", ", o, v[o]);
                } else {
                    int b = blocker(G, v, o, t->digit, via);
                    printf("%s{\"cell\": %d, \"by\": %d, \"via\": \"%s\"", first ? "" : ", ", o, b, via);
                    if (!strcmp(via, "RULE")) {
                        rule_json(G, v, o, t->digit);
                    }
                    printf("}");
                }
                first = false;
            }
        }
        printf("]}\n");
        v[t->cell] = (uint8_t)t->digit;
    }
    int left = 0;
    for (int k = 0; k < G->ncells; k++) {
        left += !v[G->cells[k]];
    }
    printf(" ], \"left\": %d}\n", left);
}

int main(int argc, char **argv)
{
    bool json = argc > 1 && !strcmp(argv[1], "--json");
    if (json) {
        argv[1] = "-";
    }
    const char *out = argc > 1 ? argv[1] : "-";
    const char *only = argc > 2 ? argv[2] : NULL;
    FILE *f = strcmp(out, "-") ? fopen(out, "w") : NULL;
    if (strcmp(out, "-") && !f) {
        perror(out);
        return 1;
    }
    if (f)
        fprintf(f, "# One example puzzle per variant for the variant pages (docs/variants/), made by\n"
                "# tools/variant_examples.c. Import it (Files > puzzles > this file > Import) to follow along.\n");
    sudoku_geom_init();
    static sudoku_geom_t board, best_board;
    for (int var = 0; var < VAR_COUNT; var++) {
        if (only && strncasecmp(only, variant_name(var), strlen(only))) {
            continue;
        }
        uint8_t giv[81], best_giv[81] = {0};
        int best_score = 1 << 30, best_level = GEN_EASY;
        uint32_t best_seed = 0;
        single_t first;
        for (int lv = GEN_EASY; lv <= GEN_MEDIUM && best_score > 2; lv++) {
            for (uint32_t s = 1; s <= 60; s++) {
                gen_level_t got;
                uint32_t seed = 7000 + s * 13;
                if (!sudoku_generate(var, (gen_level_t)lv, seed, giv, &board, &got) || (int)got != lv) {
                    continue;
                }
                single_t one;
                int sc;
                if (pick(var, &board, giv, &one, &sc) < 0 || sc >= best_score) {
                    continue;
                }
                if (!singles_solve(&board, giv)) {
                    continue;    /* (the walkthrough must reach the end) */
                }
                best_score = sc;
                best_seed = seed;
                best_level = lv;
                first = one;
                memcpy(best_giv, giv, 81);
                best_board = board;
                if (sc <= 1) {
                    break;
                }
            }
        }
        if (!best_seed) {
            printf("== %s: no example found\n", variant_name(var));
            continue;
        }
        const sudoku_geom_t *G = &best_board;
        if (json) {
            walkthrough(var, G, best_giv, best_level, best_seed);
            return 0;
        }
        uint8_t sol[81], v[81];
        sudoku_solve(G, best_giv, sol);
        static char cages[1024], marks[1024], thermos[1024];
        char regions[82], line[2400];
        layout_texts(G, regions, cages, marks, thermos, sizeof(cages));
        sdx_format(var, best_level, best_giv, regions, cages, marks, thermos, false, line, sizeof(line));
        if (f) {
            fprintf(f, "%s\n", line);
        }
        printf("== %s (%s, seed %u)\n   sdx: %s\n", variant_name(var), gen_level_name(best_level), (unsigned)best_seed,
               line);
        for (int r = 0; r < G->n; r++) {
            printf("   ");
            for (int c = 0; c < G->n; c++) {
                printf("%c", best_giv[r * 9 + c] ? '0' + best_giv[r * 9 + c] : '.');
            }
            printf("   ");
            for (int c = 0; c < G->n; c++) {
                printf("%d", sol[r * 9 + c]);
            }
            printf("\n");
        }
        memcpy(v, best_giv, 81);
        single_t s = first;
        for (int i = 1; i <= STEPS; i++) {
            print_step(G, v, &s, i);
            v[s.cell] = (uint8_t)s.digit;
            int sc;
            if (pick(var, G, v, &s, &sc) < 0) {        /* then any single, the variant's or not */
                static single_t all[2048];
                int n = singles(G, v, false, all);
                if (!n) {
                    break;
                }
                s = all[0];
                for (int k = 0; k < n; k++) {
                    if (!all[k].naked && all[k].unit >= 18) {
                        s = all[k];
                        break;
                    }
                }
            }
        }
    }
    if (f) {
        fclose(f);
    }
    return 0;
}

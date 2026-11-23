/**
 * @file gen_hints.c
 * @ingroup generator
 * @brief Hints: the next step a person would take on a board, with the technique that shows it.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @brief a hidden single (boxes first: the easiest to spot), or -1
 * @param[in] L  the techniques' state
 * @param[out] digit  gets its digit
 * @param[out] unit  gets its unit
 * @return the cell, or -1
 */
static int find_hidden_single(const logic_t *L, int *digit, int *unit)
{
    const sudoku_geom_t *G = L->G;
    static const int order[5][2] = {{18, 27}, {0, 9}, {9, 18}, {27, 29}, {UNIT_WINDOW, UNIT_WINDOW + 4}};
    for (int o = 0; o < 5; o++) {
        for (int u = order[o][0]; u < order[o][1]; u++) {
            if (!G->active[u]) {
                continue;
            }
            for (int d = 1; d <= G->n; d++) {
                int where = -1, n = 0;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[u][k];
                    if (L->v[c] == d) {
                        n = -1;
                        break;
                    }
                    if (L->cand[c] & (1u << d)) {
                        where = c;
                        n++;
                    }
                }
                if (n == 1) {
                    *digit = d;
                    *unit = u;
                    return where;
                }
            }
        }
    }
    return -1;
}

/**
 * @brief A naked single: a cell with one candidate left.
 * @param[in] L  the techniques' state
 * @param[out] digit  gets its digit
 * @return the cell, or -1
 */
static int find_naked_single(const logic_t *L, int *digit)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (!L->v[c] && gen_popcount(L->cand[c]) == 1) {
            *digit = gen_lowest(L->cand[c]);
            return c;
        }
    }
    return -1;
}

/* (documented in generator.h) */
bool sudoku_next_step(const sudoku_geom_t *G, const uint8_t vals[81], const uint8_t solution[81], sudoku_step_t *out)
{
    memset(out, 0, sizeof(*out));
    out->cell = -1;
    out->unit = -1;
    for (int k = 0; k < G->ncells; k++) {                /* a wrong digit first */
        int c = G->cells[k];
        if (vals[c] && vals[c] != solution[c]) {
            out->kind = STEP_WRONG;
            out->cell = c;
            out->digit = vals[c];
            return true;
        }
    }
    static logic_t L;                                    /* off the (small) LVGL task stack */
    gen_logic_load(&L, G, vals);
    if (!L.empty) {
        return false;    /* solved */
    }
    gen_set_trace(&out->tech);
    for (int round = 0; round < 64 && !L.broken; round++) {
        int d, u, c = find_hidden_single(&L, &d, &u);
        if (c >= 0) {
            out->kind = STEP_HIDDEN_SINGLE;
            out->cell = c;
            out->digit = d;
            out->unit = u;
            gen_set_trace(NULL);
            return true;
        }
        c = find_naked_single(&L, &d);
        if (c >= 0) {
            out->kind = STEP_NAKED_SINGLE;
            out->cell = c;
            out->digit = d;
            gen_set_trace(NULL);
            return true;
        }
        if (!(G->ncages && gen_cage_logic(&L)) && !(G->consec_on && gen_consec_logic(&L)) &&
                !(G->nthermos && gen_thermo_logic(&L)) && !(G->sandwich && gen_sandwich_logic(&L)) &&
                !(G->edge_kind && gen_edge_logic(&L)) && !(G->nquads && gen_quad_logic(&L)) &&
                !(G->skyscraper && gen_sky_logic(&L)) && !(G->nlk && gen_lk_logic(&L)) && !(G->xsums && gen_xsum_logic(&L)) &&
                !(G->nrooms && gen_nroom_logic(&L)) && !(G->skysum && gen_skysum_logic(&L)) &&
                !((G->frame || G->outside || G->rossini) && gen_first3_logic(&L)) &&
                !(G->diag_nc && gen_diagnc_logic(&L)) && !(G->idx159 && gen_159_logic(&L)) &&
                !(G->antidiag && gen_antidiag_logic(&L)) && !(G->psky && gen_psky_logic(&L)) &&
                !gen_locked_candidates(&L) &&
                !gen_pairs(&L) && !gen_naked_triple(&L) && !gen_fish(&L, 2) && !gen_fish(&L, 3) && !gen_xy_wing(&L)) {
            break;                                                  /* the first one is in out->tech */
        }
    }
    gen_set_trace(NULL);
    /* nothing simple: the empty cell with the fewest candidates, from the solution */
    int best = -1, best_n = 10;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c]) {
            continue;
        }
        int n = gen_popcount(L.cand[c]);
        if (n < best_n) {
            best = c;
            best_n = n;
        }
    }
    out->kind = STEP_NONE;
    out->cell = best;
    out->digit = best >= 0 ? solution[best] : 0;
    memset(&out->tech, 0, sizeof(out->tech));
    return best >= 0;
}

/* (documented in generator.h) */
bool sudoku_step_with(const sudoku_geom_t *G, const uint8_t vals[81], const uint8_t solution[81], int tech,
                      sudoku_step_t *out)
{
    static logic_t L;
    memset(out, 0, sizeof(*out));
    out->cell = out->unit = -1;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c] && vals[c] != solution[c]) {
            return false;    /* not from a wrong board */
        }
    }
    gen_logic_load(&L, G, vals);
    if (!L.empty || L.broken) {
        return false;
    }
    int d, u;
    if (find_hidden_single(&L, &d, &u) >= 0 || find_naked_single(&L, &d) >= 0) {
        return false;
    }
    gen_set_trace(&out->tech);
    bool ok = false;
    switch (tech) {
    case TECH_POINTING:
    case TECH_CLAIMING:
        ok = gen_locked_candidates(&L);
        break;
    case TECH_NAKED_PAIR:
    case TECH_HIDDEN_PAIR:
        ok = gen_pairs(&L);
        break;
    case TECH_NAKED_TRIPLE:
        ok = gen_naked_triple(&L);
        break;
    case TECH_X_WING:
        ok = gen_fish(&L, 2);
        break;
    case TECH_SWORDFISH:
        ok = gen_fish(&L, 3);
        break;
    case TECH_XY_WING:
        ok = gen_xy_wing(&L);
        break;
    }
    gen_set_trace(NULL);
    if (!ok || out->tech.kind != tech || L.broken) {
        return false;
    }
    int c = find_hidden_single(&L, &d, &u);
    if (c >= 0) {
        out->kind = STEP_HIDDEN_SINGLE;
        out->unit = u;
    } else if ((c = find_naked_single(&L, &d)) >= 0) {
        out->kind = STEP_NAKED_SINGLE;
    } else {
        return false;                                   /* it narrowed things down, but no single yet */
    }
    out->cell = c;
    out->digit = d;
    return d == solution[c];
}

/* (documented in generator.h) */
int sudoku_unit_cell(const sudoku_geom_t *G, int unit, int k)
{
    if (unit < 0 || unit >= UNITS_MAX || !G->active[unit] || k < 0 || k >= G->n) {
        return -1;
    }
    return G->unit[unit][k];
}

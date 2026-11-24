/**
 * @file gen_grade.c
 * @ingroup generator
 * @brief Human-style grading: the techniques a person would use (singles, locked candidates,
 *        pairs, fish, XY-Wing, and each variant's own), and the level they give.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/** @brief sum of the digits in a candidate mask (bit d = digit d), for the cage logic */
static uint8_t s_mask_sum[1024];

/**
 * @brief Places a digit: the peers lose it as a candidate.
 * @param[in,out] L  the techniques' state
 * @param[in] c  the cell
 * @param[in] d  the digit
 */
static void place(logic_t *L, int c, int d)
{
    const sudoku_geom_t *G = L->G;
    L->v[c] = (uint8_t)d;
    L->cand[c] = 0;
    L->empty--;
    for (int p = 0; p < G->npeers[c]; p++) {
        int o = G->peers[c][p];
        L->cand[o] &= (uint16_t)~(1u << d);
        if (!L->v[o] && !L->cand[o]) {
            L->broken = true;
        }
    }
}

/* (documented in gen_internal.h) */
void gen_logic_load(logic_t *L, const sudoku_geom_t *G, const uint8_t *vals)
{
    memset(L, 0, sizeof(*L));
    L->G = G;
    L->empty = G->ncells;
    for (int k = 0; k < G->ncells; k++) {
        L->cand[G->cells[k]] = G->allow[G->cells[k]];
    }
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c] >= 1 && vals[c] <= G->n) {
            place(L, c, vals[c]);
        }
    }
}

/* (documented in gen_internal.h) */
bool gen_singles(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {           /* naked single */
        int c = G->cells[k];
        if (!L->v[c] && gen_popcount(L->cand[c]) == 1) {
            place(L, c, gen_lowest(L->cand[c]));
            return true;
        }
    }
    for (int i = 0; i < G->nunits; i++) {           /* hidden single */
        int u = G->unit_ids[i];
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
                place(L, where, d);
                return true;
            }
        }
    }
    return false;
}

/** @brief the first technique used while looking for the next step (sudoku_next_step) */
static sudoku_tech_t *s_trace;

/**
 * @brief Notes the first technique used (sudoku_next_step()'s, when s_trace is set).
 * @param[in] kind  TECH_*
 * @param[in] unit  its unit
 * @param[in] unit2  its second unit
 * @param[in] d1  its digit
 * @param[in] d2  its second digit
 * @param[in] c1  its first cell
 * @param[in] c2  its second cell
 */
static void trace(int kind, int unit, int unit2, int d1, int d2, int c1, int c2)
{
    if (!s_trace || s_trace->kind != TECH_NONE) {
        return;
    }
    s_trace->kind = kind;
    s_trace->unit = unit;
    s_trace->unit2 = unit2;
    s_trace->unit3 = -1;
    s_trace->digit = d1;
    s_trace->digit2 = d2;
    s_trace->cell1 = c1;
    s_trace->cell2 = c2;
    s_trace->ncells = 0;
}

/**
 * @brief the same, with a third line and the cells that make the pattern
 * @param[in] kind  TECH_*
 * @param[in] unit  its unit
 * @param[in] unit2  its second unit
 * @param[in] unit3  its third
 * @param[in] d1  its digit
 * @param[in] d2  its second digit
 * @param[in] cells  the cells
 * @param[in] n  how many
 */
static void trace_cells(int kind, int unit, int unit2, int unit3, int d1, int d2, const int *cells, int n)
{
    if (!s_trace || s_trace->kind != TECH_NONE) {
        return;
    }
    trace(kind, unit, unit2, d1, d2, -1, -1);
    s_trace->unit3 = unit3;
    s_trace->ncells = n > 9 ? 9 : n;
    for (int i = 0; i < s_trace->ncells; i++) {
        s_trace->cells[i] = (uint8_t)cells[i];
    }
}

/**
 * @brief Removes candidates from a cell.
 * @param[in,out] L  the techniques' state
 * @param[in] c  the cell
 * @param[in] bits  the candidates (bit d = d)
 * @return true if any went (no candidate left: L->broken)
 */
static bool eliminate(logic_t *L, int c, uint16_t bits)
{
    if (L->v[c] || !(L->cand[c] & bits)) {
        return false;
    }
    L->cand[c] &= (uint16_t)~bits;
    if (!L->cand[c]) {
        L->broken = true;
    }
    return true;
}

/**
 * @brief Product Killer: the product of a set of digits
 * @param[in] m  the digits (bit d)
 * @return their product
 */
static int mask_product(uint16_t m)
{
    int p = 1;
    for (int d = 2; d <= 9; d++) {
        p *= (m >> d & 1) ? d : 1;
    }
    return p;
}

/**
 * @brief Product Killer: tries every way to fill a cage's empty cells with different digits from their
 * candidates that multiply to what's left; marks each digit that fits somewhere
 * @param[in] L  the logic state
 * @param[in] empty  the empty cells
 * @param[in] n  how many
 * @param[in] at  the cell to fill next
 * @param[in] left  what's left of the product
 * @param[in] used  the digits taken
 * @param[in] pick  the digits picked so far, per cell
 * @param[in,out] fits  gets, per cell, the digits of each way that works
 */
static void product_ways(const logic_t *L, const int *empty, int n, int at, int left, uint16_t used, uint8_t *pick,
                         uint16_t *fits)
{
    if (at == n) {
        if (left == 1) {
            for (int i = 0; i < n; i++) {
                fits[i] |= (uint16_t)(1u << pick[i]);
            }
        }
        return;
    }
    const uint16_t m = (uint16_t)(L->cand[empty[at]] & ~used);
    for (int d = 1; d <= 9; d++) {
        if ((m >> d & 1) && left % d == 0) {
            pick[at] = (uint8_t)d;
            product_ways(L, empty, n, at + 1, left / d, (uint16_t)(used | 1u << d), pick, fits);
        }
    }
}

/* (documented in gen_internal.h) */
bool gen_cage_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncages; k++) {
        const bool prod = G->cage_prod[k] != 0;     /* (Product Killer: the digits multiply to it) */
        if (!G->cage_sum[k] && !prod) {
            continue;                               /* (Sumless Killer, Extra Regions: no sum, its cells are only peers) */
        }
        int left = prod ? G->cage_prod[k] : G->cage_sum[k], n = 0, empty[9];
        uint16_t used = 0, avail = 0;
        for (int i = 0; i < G->cage_n[k]; i++) {
            int c = G->cage_cell[k][i];
            if (L->v[c]) {
                left = !prod ? left - L->v[c] : left % L->v[c] ? -1 : left / L->v[c];
                used |= (uint16_t)(1u << L->v[c]);
            } else {
                empty[n++] = c;
                avail |= L->cand[c];
            }
        }
        if (!n) {
            continue;
        }
        if (prod) {                                 /* each cell by every way to fill the cage */
            uint16_t fits[9] = {0};
            uint8_t pick[9];
            if (left > 0) {
                product_ways(L, empty, n, 0, left, used, pick, fits);
            }
            bool changed = false;
            uint16_t allowed = 0;
            for (int i = 0; i < n; i++) {
                changed |= eliminate(L, empty[i], (uint16_t)(L->cand[empty[i]] & ~fits[i]));
                allowed |= fits[i];
            }
            if (changed) {
                trace(TECH_CAGE, k, -1, allowed, 0, -1, -1);
                return true;
            }
            continue;
        }
        avail &= (uint16_t)~used;
        uint16_t allowed = 0;
        for (uint16_t m = avail; m; m = (uint16_t)((m - 1) & avail)) {
            if (gen_popcount(m) != n || (prod ? mask_product(m) : s_mask_sum[m]) != left) {
                continue;
            }
            bool fits = true;
            for (int i = 0; i < n && fits; i++) {
                fits = (L->cand[empty[i]] & m) != 0;
            }
            if (fits) {
                allowed |= m;
            }
        }
        bool changed = false;
        for (int i = 0; i < n; i++) {
            changed |= eliminate(L, empty[i], (uint16_t)(L->cand[empty[i]] & ~allowed));
        }
        if (changed) {
            trace(TECH_CAGE, k, -1, allowed, 0, -1, -1);
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_159_logic(logic_t *L)
{
    for (int r = 0; r < 9; r++) {
        for (int q = 0; q < 9; q++) {
            const int i = r * 9 + q;
            if (L->v[i]) {
                continue;
            }
            uint16_t keep = L->cand[i];
            if (q % 4 == 0) {                       /* column 1, 5 or 9: where it points must be able to take k */
                const int k = q + 1;
                for (int c = 1; c <= 9; c++) {
                    const int t = r * 9 + c - 1;
                    const bool can = t == i ? c == k : L->v[t] ? L->v[t] == k : (L->cand[t] >> k & 1) != 0;
                    keep &= can ? (uint16_t)0xFFFF : (uint16_t)~(1u << c);
                }
            }
            for (int k = 1; k <= 9; k += 4) {       /* holding k: column k must be able to say this column */
                const int idx = r * 9 + k - 1;
                const bool can = idx == i || (L->v[idx] ? L->v[idx] == q + 1 : (L->cand[idx] >> (q + 1) & 1) != 0);
                keep &= can ? (uint16_t)0xFFFF : (uint16_t)~(1u << k);
            }
            if (keep != L->cand[i]) {
                eliminate(L, i, (uint16_t)(L->cand[i] & ~keep));
                trace(TECH_159, i, -1, keep, 0, i, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_consec_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int c = 0; c < 81; c++) {
        if (L->v[c]) {
            continue;
        }
        const int nb[4] = {c % 9 > 0 ? c - 1 : -1, c % 9 < 8 ? c + 1 : -1, c > 8 ? c - 9 : -1, c < 72 ? c + 9 : -1};
        const bool bar[4] = {nb[0] >= 0 && (G->consec[c - 1] & CONSEC_RIGHT), (G->consec[c] & CONSEC_RIGHT) != 0,
                             nb[2] >= 0 && (G->consec[c - 9] & CONSEC_DOWN), (G->consec[c] & CONSEC_DOWN) != 0
                            };
        uint16_t keep = L->cand[c];
        for (int k = 0; k < 4; k++) {
            if (nb[k] < 0) {
                continue;
            }
            uint16_t other = L->v[nb[k]] ? (uint16_t)(1u << L->v[nb[k]]) : L->cand[nb[k]];
            uint16_t ok = 0;
            for (int d = 1; d <= 9; d++) {
                if (!(keep >> d & 1)) {
                    continue;
                }
                uint16_t near = (uint16_t)(((1u << (d + 1)) | (1u << (d - 1))) & 0x3FE);
                if (bar[k] ? (other & near) : (other & ~near & 0x3FE)) {
                    ok |= (uint16_t)(1u << d);
                }
            }
            keep &= ok;
        }
        if (keep != L->cand[c]) {
            eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
            trace(TECH_CONSEC, c, -1, keep, 0, c, -1);
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_edge_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int c = 0; c < 81; c++) {
        if (L->v[c]) {
            continue;
        }
        uint16_t keep = (uint16_t)(L->cand[c] & gen_edge_allow(G, c, L->v, L->cand));
        if (keep != L->cand[c]) {
            eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
            trace(TECH_EDGE, c, -1, keep, 0, c, -1);
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_quad_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int q = 0; q < G->nquads; q++) {
        int at = G->quad_at[q], need[10] = {0}, empty = 0, miss = 0;
        const int cells[4] = {at, at + 1, at + 9, at + 10};
        for (int k = 0; k < G->quad_n[q]; k++) {
            need[G->quad_dig[q][k]]++;
        }
        for (int k = 0; k < 4; k++) {
            int v = L->v[cells[k]];
            if (!v) {
                empty++;
            } else if (need[v]) {
                need[v]--;
            }
        }
        uint16_t mm = 0;
        for (int d = 1; d <= 9; d++) {
            miss += need[d];
            if (need[d]) {
                mm |= (uint16_t)(1u << d);
            }
        }
        if (!miss) {
            continue;
        }
        if (miss > empty) {
            L->broken = true;
            return false;
        }
        for (int k = 0; miss == empty && k < 4; k++) {          /* only its digits fit */
            int c = cells[k];
            if (L->v[c] || !(L->cand[c] & ~mm)) {
                continue;
            }
            uint16_t keep = (uint16_t)(L->cand[c] & mm);
            eliminate(L, c, (uint16_t)(L->cand[c] & ~mm));
            trace(TECH_QUAD, c, q, keep, 0, c, -1);
            return true;
        }
        for (int d = 1; d <= 9; d++) {              /* d has just the places it needs */
            if (!need[d]) {
                continue;
            }
            int where[4], nw = 0;
            for (int k = 0; k < 4; k++) {
                if (!L->v[cells[k]] && (L->cand[cells[k]] >> d & 1)) {
                    where[nw++] = cells[k];
                }
            }
            if (nw < need[d]) {
                L->broken = true;
                return false;
            }
            if (nw != need[d]) {
                continue;
            }
            for (int k = 0; k < nw; k++) {
                int c = where[k];
                if (L->cand[c] == (uint16_t)(1u << d)) {
                    continue;
                }
                eliminate(L, c, (uint16_t)(L->cand[c] & ~(1u << d)));
                trace(TECH_QUAD, c, q, 1 << d, 0, c, -1);
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Skyscraper, Sum Skyscrapers: once a line with a clue has 4 empty cells or fewer, every order of its
 *        missing digits is tried against the clue; candidates no order keeps are taken out.
 * @param[in,out] L      the state
 * @param[in] clues      the 18 clues (0: none)
 * @param[in] is_ok      does a line's digits keep its clue (is_gen_sky_line_ok(), is_gen_skysum_line_ok())
 * @param[in] tech       the technique it is (TECH_SKY, TECH_SKYSUM)
 * @return true if it changed anything
 */
static bool sky_orders_logic(logic_t *L, const uint8_t clues[18], bool (*is_ok)(const uint8_t x[9], int c), int tech)
{
    for (int lu = 0; lu < 18; lu++) {
        int c = clues[lu], cells[9], emp[9], ne = 0, miss[9], nm = 0;
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        uint8_t x[9];
        unsigned have = 0;
        for (int k = 0; k < 9; k++) {
            x[k] = L->v[cells[k]];
            if (x[k]) {
                have |= 1u << x[k];
            } else {
                emp[ne++] = k;
            }
        }
        if (!ne || ne > 4) {
            continue;
        }
        for (int d = 1; d <= 9; d++) {
            if (!(have >> d & 1)) {
                miss[nm++] = d;
            }
        }
        if (nm != ne) {
            continue;
        }
        uint16_t ok[9] = {0};
        int perm[4] = {0, 1, 2, 3}, np = 1;
        for (int f = 2; f <= ne; f++) {
            np *= f;
        }
        for (int p = 0; p < np; p++) {              /* every order of the missing digits */
            int idx[4], used = 0, q = p;
            for (int j = 0; j < ne; j++) {          /* p as a permutation (factorial number system) */
                int r = q % (ne - j), n = 0;
                q /= ne - j;
                for (int t = 0; t < ne; t++) {
                    if (used >> t & 1) {
                        continue;
                    }
                    if (n++ == r) {
                        idx[j] = t;
                        used |= 1 << t;
                        break;
                    }
                }
            }
            bool fits = true;
            for (int j = 0; j < ne && fits; j++) {
                fits = (L->cand[cells[emp[j]]] >> miss[idx[j]] & 1) != 0;
            }
            if (!fits) {
                continue;
            }
            for (int j = 0; j < ne; j++) {
                x[emp[j]] = (uint8_t)miss[idx[j]];
            }
            if (is_ok(x, c)) {
                for (int j = 0; j < ne; j++) {
                    ok[j] |= (uint16_t)(1u << miss[idx[j]]);
                }
            }
            for (int j = 0; j < ne; j++) {
                x[emp[j]] = 0;
            }
        }
        (void)perm;
        for (int j = 0; j < ne; j++) {
            int cell = cells[emp[j]];
            if (L->cand[cell] & ~ok[j]) {
                uint16_t keep = (uint16_t)(L->cand[cell] & ok[j]);
                eliminate(L, cell, (uint16_t)(L->cand[cell] & ~ok[j]));
                trace(tech, lu, -1, keep, 0, cell, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_sky_logic(logic_t *L)
{
    return sky_orders_logic(L, L->G->sky, is_gen_sky_line_ok, TECH_SKY);
}

/* (documented in gen_internal.h) */
bool gen_skysum_logic(logic_t *L)
{
    return sky_orders_logic(L, L->G->sky_sum, is_gen_skysum_line_ok, TECH_SKYSUM);
}

/* (documented in gen_internal.h) */
bool gen_b9_logic(logic_t *L)
{
    return sky_orders_logic(L, L->G->b9_sum, is_gen_b9_line_ok, TECH_B9);
}

/* (documented in gen_internal.h) */
bool gen_quadro_logic(logic_t *L)
{
    for (int tl = 0; tl < 72; tl++) {               /* each 2x2 block, by its top-left cell */
        if (tl % 9 == 8) {
            continue;
        }
        const int four[4] = {tl, tl + 1, tl + 9, tl + 10};
        for (int k = 0; k < 4; k++) {
            const int c = four[k];
            if (L->v[c]) {
                continue;
            }
            bool all_odd = true, all_even = true;   /* the other three: certainly odd, certainly even? */
            for (int j = 0; j < 4; j++) {
                if (j == k) {
                    continue;
                }
                const uint16_t m = L->v[four[j]] ? (uint16_t)(1u << L->v[four[j]]) : L->cand[four[j]];
                all_odd &= !(m & 0x154);
                all_even &= !(m & 0x2AA);
            }
            const uint16_t out = all_odd ? (uint16_t)0x2AA : all_even ? (uint16_t)0x154 : 0;
            if (L->cand[c] & out) {
                eliminate(L, c, (uint16_t)(L->cand[c] & out));
                trace(TECH_QUADRO, tl, -1, L->cand[c], 0, c, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_aq9_logic(logic_t *L)
{
    static const int8_t DR[4] = {-1, -1, 1, 1}, DC[4] = {-1, 1, -1, 1};
    for (int n = 0; n < 81; n++) {                  /* each 9: no 9 along its diagonals */
        if (L->v[n] != 9) {
            continue;
        }
        for (int k = 0; k < 4; k++) {
            for (int r = n / 9 + DR[k], c = n % 9 + DC[k]; r >= 0 && r < 9 && c >= 0 && c < 9; r += DR[k], c += DC[k]) {
                const int o = r * 9 + c;
                if (!L->v[o] && (L->cand[o] >> 9 & 1)) {
                    eliminate(L, o, (uint16_t)(1u << 9));
                    trace(TECH_AQ9, n, -1, L->cand[o], 0, o, -1);
                    return true;
                }
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_n9_logic(logic_t *L)
{
    return sky_orders_logic(L, L->G->n9_set, is_gen_n9_line_ok, TECH_N9);
}

/* (documented in gen_internal.h) */
bool gen_bf_logic(logic_t *L)
{
    return sky_orders_logic(L, L->G->bf_clue, is_gen_bf_line_ok, TECH_BF);
}

/* (documented in gen_internal.h) */
bool gen_clone_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int c = 0; c < 81; c++) {
        const int p = G->clone_of[c];
        if (p == CLONE_NONE || L->v[c]) {
            continue;
        }
        const uint16_t keep = (uint16_t)(L->cand[c] & (L->v[p] ? (uint16_t)(1u << L->v[p]) : L->cand[p]));
        if (L->cand[c] & ~keep) {
            eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
            trace(TECH_CLONE, p, -1, keep, 0, c, -1);
            return true;
        }
    }
    return false;
}

/**
 * @brief Search Nine: can a cell still be (or is it) d, in the techniques' state?
 * @param[in] L  the state
 * @param[in] c  the cell
 * @param[in] d  the digit
 * @return true if so
 */
static bool is_s9_maybe(const logic_t *L, int c, int d)
{
    return L->v[c] ? L->v[c] == d : (L->cand[c] >> d & 1) != 0;
}

/* (documented in gen_internal.h) */
bool gen_s9_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int a = 0; a < 81; a++) {                  /* each arrow */
        const int dir = G->s9_dir[a];
        if (!dir) {
            continue;
        }
        if (!L->v[a]) {                             /* its digit: a distance that reaches a cell that can be 9 */
            uint16_t keep = 0;
            for (int k = 1; k <= 8; k++) {
                const int t = sudoku_s9_cell(a, dir, k);
                if (t >= 0 && is_s9_maybe(L, t, 9)) {
                    keep |= (uint16_t)(1u << k);
                }
            }
            if (L->cand[a] & ~keep) {
                eliminate(L, a, (uint16_t)(L->cand[a] & ~keep));
                trace(TECH_S9, a, S9_STEP_REACH, L->cand[a], 0, a, -1);
                return true;
            }
        } else {                                    /* a digit: the cell that far is the 9 */
            const int t = sudoku_s9_cell(a, dir, L->v[a]);
            if (t >= 0 && !L->v[t] && (L->cand[t] & ~(1u << 9))) {
                eliminate(L, t, (uint16_t)(L->cand[t] & ~(1u << 9)));
                trace(TECH_S9, a, S9_STEP_IS9, L->cand[t], L->v[a], t, -1);
                return true;
            }
        }
        for (int k = 1; k <= 8; k++) {              /* the cells it points away from, or past, or short of */
            for (int back = 0; back < 2; back++) {
                const int way = back ? (dir + 1) % 4 + 1 : dir;   /* (the opposite way: 1<->3, 2<->4) */
                const int c = sudoku_s9_cell(a, way, k);
                if (c < 0 || L->v[c] || !(L->cand[c] >> 9 & 1)) {
                    continue;
                }
                if (back || !is_s9_maybe(L, a, k)) {
                    eliminate(L, c, (uint16_t)(1u << 9));
                    trace(TECH_S9, a, S9_STEP_NOT9, L->cand[c], back ? 0 : k, c, -1);
                    return true;
                }
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_psym_logic(logic_t *L)
{
    for (int c = 0; c < 81; c++) {
        if (L->v[c] || c == 40) {
            continue;
        }
        const int p = 80 - c;
        const uint16_t them = L->v[p] ? (uint16_t)(1u << L->v[p]) : L->cand[p];
        uint16_t keep = 0;
        for (int d = 1; d <= 9; d++) {
            if ((L->cand[c] >> d & 1) && (them >> (10 - d) & 1)) {
                keep |= (uint16_t)(1u << d);
            }
        }
        if (L->cand[c] & ~keep) {
            eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
            trace(TECH_PSYM, p, -1, keep, 0, c, -1);
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_psky_logic(logic_t *L)
{
    return sky_orders_logic(L, L->G->psky_clue, is_gen_psky_line_ok, TECH_PSKY);
}

/* (documented in gen_internal.h) */
bool gen_magic_logic(logic_t *L)
{
    for (int r = 3; r < 6; r++) {
        for (int q = 3; q < 6; q++) {
            const int i = r * 9 + q;
            if (L->v[i]) {
                continue;
            }
            const uint16_t keep = (uint16_t)(L->cand[i] & gen_magic_allow(L->v, L->cand, i));
            if (keep != L->cand[i]) {
                eliminate(L, i, (uint16_t)(L->cand[i] & ~keep));
                trace(TECH_MAGIC, i, -1, keep, 0, i, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_antidiag_logic(logic_t *L)
{
    for (int dg = 0; dg < 2; dg++) {
        uint16_t have = 0;
        for (int k = 0; k < 9; k++) {
            const int o = dg ? k * 9 + 8 - k : k * 10;
            have |= L->v[o] ? (uint16_t)(1u << L->v[o]) : 0;
        }
        if (gen_popcount(have) < 3) {
            continue;
        }
        for (int k = 0; k < 9; k++) {               /* the empty cells: one of the three */
            const int o = dg ? k * 9 + 8 - k : k * 10;
            if (!L->v[o] && (L->cand[o] & ~have)) {
                eliminate(L, o, (uint16_t)(L->cand[o] & ~have));
                trace(TECH_ANTIDIAG, dg, -1, have, 0, o, -1);
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Dutch Flatmates: may a cell still be d (it is, or d is among its candidates)?
 * @param[in] L  the logic state
 * @param[in] c  the cell
 * @param[in] d  the digit
 * @return true if it may
 */
static bool is_logic_open(const logic_t *L, int c, int d)
{
    return L->v[c] ? L->v[c] == d : (L->cand[c] >> d & 1) != 0;
}

/* (documented in gen_internal.h) */
bool gen_flatmates_logic(logic_t *L)
{
    for (int c = 0; c < 81; c++) {
        const int r = c / 9;
        const bool one = r > 0 && is_logic_open(L, c - 9, 1), nine = r < 8 && is_logic_open(L, c + 9, 9);
        if (!L->v[c] && (L->cand[c] >> 5 & 1) && !one && !nine) {
            eliminate(L, c, (uint16_t)(1u << 5));   /* no 1 above, no 9 below: no 5 */
            trace(TECH_FLATMATES, c, -1, L->cand[c], 0, c, -1);
            return true;
        }
        if (L->v[c] != 5) {
            continue;
        }
        const int need = !one ? c + 9 : !nine ? c - 9 : -1;   /* its 1 can't come: the 9 below; and the other way */
        if (need < 0 || need > 80) {
            continue;
        }
        const uint16_t keep = (uint16_t)(1u << (need > c ? 9 : 1));
        if (!L->v[need] && (L->cand[need] & ~keep)) {
            eliminate(L, need, (uint16_t)(L->cand[need] & ~keep));
            trace(TECH_FLATMATES, c, -1, keep, 0, need, -1);
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_mathrax_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int q = 0; q < G->nmx; q++) {
        const int at = G->mx_at[q];
        const int cells[4] = {at, at + 1, at + 9, at + 10}, across[4] = {at + 10, at + 9, at + 1, at};
        for (int k = 0; k < 4; k++) {
            const int c = cells[k], p = across[k];
            if (L->v[c]) {
                continue;
            }
            const uint16_t them = L->v[p] ? (uint16_t)(1u << L->v[p]) : L->cand[p];
            const bool seen = is_sudoku_geom_peer(G, c, p);   /* (same box: not the same digit) */
            uint16_t keep = 0;
            for (int d = 1; d <= 9; d++) {
                const uint16_t w = (uint16_t)(G->mx_mask[q][d] & them & (seen ? ~(1u << d) : 0xFFFF));
                if ((L->cand[c] >> d & 1) && w) {
                    keep |= (uint16_t)(1u << d);
                }
            }
            if (L->cand[c] & ~keep) {
                eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
                trace(TECH_MATHRAX, q, -1, keep, 0, c, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_xsum_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9];
        if (!G->xsum[lu]) {
            continue;
        }
        line_cells(lu, cells);
        for (int k = 0; k < 9; k++) {
            int cell = cells[k];
            if (L->v[cell]) {
                continue;
            }
            uint16_t keep = (uint16_t)(L->cand[cell] & gen_xsum_allow(G, lu, k, L->v, L->cand));
            if (keep != L->cand[cell]) {
                eliminate(L, cell, (uint16_t)(L->cand[cell] & ~keep));
                trace(TECH_XSUM, lu, -1, keep, 0, cell, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_nroom_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9];
        if (!G->nroom[lu]) {
            continue;
        }
        line_cells(lu, cells);
        for (int k = 0; k < 9; k++) {
            int cell = cells[k];
            if (L->v[cell]) {
                continue;
            }
            uint16_t keep = (uint16_t)(L->cand[cell] & gen_nroom_allow(G, lu, k, L->v, L->cand));
            if (keep != L->cand[cell]) {
                eliminate(L, cell, (uint16_t)(L->cand[cell] & ~keep));
                trace(TECH_NROOM, lu, -1, keep, 0, cell, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_first3_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    const int tech = G->frame ? TECH_FRAME : G->outside ? TECH_OUTSIDE : TECH_ROSSINI;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9];
        if (!is_gen_first3_clued(G, lu)) {
            continue;
        }
        line_cells(lu, cells);
        for (int k = 0; k < 3; k++) {
            int cell = cells[k];
            if (L->v[cell]) {
                continue;
            }
            uint16_t keep = (uint16_t)(L->cand[cell] & gen_first3_allow(G, lu, k, L->v, L->cand));
            if (keep != L->cand[cell]) {
                eliminate(L, cell, (uint16_t)(L->cand[cell] & ~keep));
                trace(tech, lu, -1, keep, 0, cell, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_diagnc_logic(logic_t *L)
{
    for (int i = 0; i < 81; i++) {
        if (L->v[i]) {
            continue;
        }
        uint16_t keep = L->cand[i];
        for (int k = 0; k < 4; k++) {               /* d is out when a corner cell can only be d - 1 or d + 1 */
            int r = i / 9 + (k < 2 ? -1 : 1), c = i % 9 + (k % 2 ? 1 : -1);
            if (r < 0 || r > 8 || c < 0 || c > 8) {
                continue;
            }
            int o = r * 9 + c;
            uint16_t m = L->v[o] ? (uint16_t)(1u << L->v[o]) : L->cand[o];
            const bool marked = is_sudoku_dmarked(L->G, i, k);   /* (Diagonal Consecutive: then 1 apart) */
            for (int d = 1; d <= 9 && m; d++) {
                uint16_t near = (uint16_t)(((1u << (d + 1)) | (1u << (d - 1))) & 0x3FE);
                if (marked ? !(m & near) : !(m & ~near)) {
                    keep &= (uint16_t)~(1u << d);
                }
            }
        }
        if (keep != L->cand[i]) {
            eliminate(L, i, (uint16_t)(L->cand[i] & ~keep));
            trace(TECH_DIAGNC, i, -1, keep, 0, i, -1);
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_lk_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->nlk; k++) {
        for (int j = 0; j < G->lk_n[k]; j++) {
            int c = G->lk_cell[k][j], lo = G->lk_sum[k], hi = G->lk_sum[k];
            if (L->v[c]) {
                continue;
            }
            for (int t = 0; t < G->lk_n[k]; t++) {
                int o = G->lk_cell[k][t];
                if (o == c) {
                    continue;
                }
                uint16_t m = L->v[o] ? (uint16_t)(1u << L->v[o]) : L->cand[o];
                if (!m) {
                    return false;
                }
                lo -= gen_highest(m);
                hi -= gen_lowest(m);
            }
            uint16_t keep = 0;
            for (int d = 1; d <= 9; d++) {
                if (d >= lo && d <= hi) {
                    keep |= (uint16_t)(1u << d);
                }
            }
            keep &= L->cand[c];
            if (keep != L->cand[c]) {
                eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
                trace(TECH_LK, k, -1, keep, 0, c, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_thermo_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    if (G->line_kind != LINE_THERMO && G->line_kind != LINE_SLOW) {   /* Arrow, Renban, Whispers, ...: each cell by its line */
        for (int t = 0; t < G->nthermos; t++) {
            for (int p = 0; p < G->thermo_n[t]; p++) {
                int c = G->thermo_cell[t][p];
                if (L->v[c]) {
                    continue;
                }
                uint16_t keep = (uint16_t)(L->cand[c] & gen_line_allow(G, t, p, L->v, L->cand));
                if (keep != L->cand[c]) {
                    eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
                    trace(TECH_LINE, t, -1, keep, 0, c, -1);
                    return true;
                }
            }
        }
        return false;
    }
    for (int t = 0; t < G->nthermos; t++) {
        int n = G->thermo_n[t], lo[9], hi[9];
        for (int i = 0; i < n; i++) {
            int c = G->thermo_cell[t][i];
            uint16_t m = L->v[c] ? (uint16_t)(1u << L->v[c]) : L->cand[c];
            if (!m) {
                return false;
            }
            lo[i] = gen_lowest(m);
            hi[i] = gen_highest(m);
        }
        for (int i = 1; i < n; i++) {               /* (Slow Thermo: the step is 0 where the two don't see each other) */
            if (lo[i] < lo[i - 1] + sudoku_thermo_step(G, t, i)) {
                lo[i] = lo[i - 1] + sudoku_thermo_step(G, t, i);
            }
        }
        for (int i = n - 2; i >= 0; i--) {
            if (hi[i] > hi[i + 1] - sudoku_thermo_step(G, t, i + 1)) {
                hi[i] = hi[i + 1] - sudoku_thermo_step(G, t, i + 1);
            }
        }
        for (int i = 0; i < n; i++) {
            int c = G->thermo_cell[t][i];
            if (L->v[c]) {
                continue;
            }
            uint16_t keep = (uint16_t)(hi[i] >= lo[i] ? (((1u << (hi[i] + 1)) - 1) & ~((1u << lo[i]) - 1)) : 0);
            if (L->cand[c] & ~keep) {
                eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
                trace(TECH_THERMO, t, -1, L->cand[c], 0, c, -1);
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_sandwich_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9], ps[10], pe[10], pbad[10];     /* prefix: sum placed, empty cells, placed 1s/9s */
        uint16_t one = 0, nine = 0;                 /* bit k: cell k can be the 1 (the 9) */
        ps[0] = pe[0] = pbad[0] = 0;
        for (int k = 0; k < 9; k++) {
            int c = cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9), v = L->v[c];
            if (v == 1 || (!v && (L->cand[c] & 2))) {
                one |= (uint16_t)(1u << k);
            }
            if (v == 9 || (!v && (L->cand[c] & 0x200))) {
                nine |= (uint16_t)(1u << k);
            }
            ps[k + 1] = ps[k] + (v != 1 && v != 9 ? v : 0);
            pe[k + 1] = pe[k] + !v;
            pbad[k + 1] = pbad[k] + (v == 1 || v == 9);
        }
        uint16_t can1 = 0, can9 = 0;
        for (int p = 0; p < 9; p++) {
            if (!(one >> p & 1)) {
                continue;
            }
            for (int q = 0; q < 9; q++) {
                if (q == p || !(nine >> q & 1)) {
                    continue;
                }
                int a = p < q ? p : q, b = p < q ? q : p;
                if (pbad[b] - pbad[a + 1]) {
                    continue;    /* a placed 1 or 9 between them */
                }
                int empty = pe[b] - pe[a + 1], left = G->sandwich_sum[lu] - (ps[b] - ps[a + 1]);
                if (left < 0 || !(gen_bread_sums(empty) >> left & 1)) {
                    continue;
                }
                can1 |= (uint16_t)(1u << p);
                can9 |= (uint16_t)(1u << q);
            }
        }
        bool changed = false;
        for (int k = 0; k < 9; k++) {
            int c = cells[k];
            if (L->v[c]) {
                continue;
            }
            uint16_t drop = 0;
            if (!(can1 >> k & 1)) {
                drop |= 2;
            }
            if (!(can9 >> k & 1)) {
                drop |= 0x200;
            }
            changed |= eliminate(L, c, drop);
        }
        if (changed) {
            trace(TECH_SANDWICH, lu, -1, 0, 0, -1, -1);
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_locked_candidates(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    bool changed = false;
    for (int bi = 0; bi < 9 + 4; bi++) {               /* boxes (regions), then Hyper windows */
        int b = bi < 9 ? 18 + bi : UNIT_WINDOW + bi - 9;
        if (!G->active[b]) {
            continue;
        }
        for (int oi = 0; oi < G->nunits; oi++) {
            int other = G->unit_ids[oi];
            if (other == b || (b < 27 && other >= 18 && other < 27)) {
                continue;    /* boxes don't overlap */
            }
            for (int d = 1; d <= G->n; d++) {
                uint16_t m = (uint16_t)(1u << d);
                int in_both = 0, box_only = 0, line_only = 0;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[b][k];
                    if (!(L->cand[c] & m)) {
                        continue;
                    }
                    if (is_in_unit(G, c, other)) {
                        in_both++;
                    } else {
                        box_only++;
                    }
                }
                if (!in_both) {
                    continue;
                }
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[other][k];
                    if (!(L->cand[c] & m)) {
                        continue;
                    }
                    if (!is_in_unit(G, c, b)) {
                        line_only++;
                    }
                }
                if (!box_only && line_only) {                 /* pointing: clear the rest of the line */
                    for (int k = 0; k < G->n; k++) {
                        int c = G->unit[other][k];
                        if (!is_in_unit(G, c, b)) {
                            changed |= eliminate(L, c, m);
                        }
                    }
                    if (changed) {
                        trace(TECH_POINTING, b, other, d, 0, -1, -1);
                    }
                } else if (!line_only && box_only) {          /* claiming: clear the rest of the box */
                    for (int k = 0; k < G->n; k++) {
                        int c = G->unit[b][k];
                        if (!is_in_unit(G, c, other)) {
                            changed |= eliminate(L, c, m);
                        }
                    }
                    if (changed) {
                        trace(TECH_CLAIMING, other, b, d, 0, -1, -1);
                    }
                }
                if (changed) {
                    return true;
                }
            }
        }
    }
    return changed;
}

/* (documented in gen_internal.h) */
bool gen_pairs(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    const int n = G->n;
    for (int ui = 0; ui < G->nunits; ui++) {
        int u = G->unit_ids[ui];
        /* naked pair: two cells with the same two candidates */
        for (int a = 0; a < n; a++) {
            int ca = G->unit[u][a];
            if (L->v[ca] || gen_popcount(L->cand[ca]) != 2) {
                continue;
            }
            for (int b = a + 1; b < n; b++) {
                int cb = G->unit[u][b];
                if (L->v[cb] || L->cand[cb] != L->cand[ca]) {
                    continue;
                }
                bool changed = false;
                for (int k = 0; k < n; k++) {
                    int c = G->unit[u][k];
                    if (c != ca && c != cb) {
                        changed |= eliminate(L, c, L->cand[ca]);
                    }
                }
                if (changed) {
                    int d1 = gen_lowest(L->cand[ca]), d2 = gen_highest(L->cand[ca]);
                    trace(TECH_NAKED_PAIR, u, -1, d1, d2, ca, cb);
                    return true;
                }
            }
        }
        /* hidden pair: two digits that fit only the same two cells */
        uint16_t where[10] = {0};                   /* bit k = unit cell k */
        for (int d = 1; d <= n; d++) {
            for (int k = 0; k < n; k++) {
                if (L->cand[G->unit[u][k]] & (1u << d)) {
                    where[d] |= (uint16_t)(1u << k);
                }
            }
        }
        for (int d1 = 1; d1 <= n; d1++) {
            if (gen_popcount(where[d1]) != 2) {
                continue;
            }
            for (int d2 = d1 + 1; d2 <= n; d2++) {
                if (where[d2] != where[d1]) {
                    continue;
                }
                uint16_t keep = (uint16_t)((1u << d1) | (1u << d2));
                bool changed = false;
                int cells[2], nc = 0;
                for (int k = 0; k < n; k++) {
                    if (!(where[d1] & (1u << k))) {
                        continue;
                    }
                    int c = G->unit[u][k];
                    if (nc < 2) {
                        cells[nc++] = c;
                    }
                    changed |= eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
                }
                if (changed) {
                    trace(TECH_HIDDEN_PAIR, u, -1, d1, d2, cells[0], cells[1]);
                    return true;
                }
            }
        }
    }
    return false;
}

/* ---- advanced techniques: hints and Learn only (sudoku_next_step). Grading leaves
 * them out, so the levels mean what they did. Each relies only on the board's units
 * (rows, columns, boxes, and diagonals / windows / regions where the variant has them),
 * which every variant keeps, so the eliminations are sound for all of them. */

/* (documented in gen_internal.h) */
bool gen_naked_triple(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    const int n = G->n;
    for (int ui = 0; ui < G->nunits; ui++) {
        int u = G->unit_ids[ui], cs[9], m = 0;
        for (int k = 0; k < n; k++) {
            int c = G->unit[u][k], p = gen_popcount(L->cand[c]);
            if (!L->v[c] && p >= 2 && p <= 3) {
                cs[m++] = c;
            }
        }
        for (int a = 0; a < m; a++) {
            for (int b = a + 1; b < m; b++) {
                for (int e = b + 1; e < m; e++) {
                    uint16_t un = (uint16_t)(L->cand[cs[a]] | L->cand[cs[b]] | L->cand[cs[e]]);
                    if (gen_popcount(un) != 3) {
                        continue;
                    }
                    bool changed = false;
                    for (int k = 0; k < n; k++) {
                        int c = G->unit[u][k];
                        if (c != cs[a] && c != cs[b] && c != cs[e]) {
                            changed |= eliminate(L, c, un);
                        }
                    }
                    if (changed) {
                        const int cells[3] = {cs[a], cs[b], cs[e]};
                        trace_cells(TECH_NAKED_TRIPLE, u, -1, -1, un, 0, cells, 3);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/** gen_fish(): the cell at a base line's position (orient: base rows, or base columns) */
#define FISH_CELL(line, pos) (orient ? (pos) * 9 + (line) : (line) * 9 + (pos))

/* (documented in gen_internal.h) */
bool gen_fish(logic_t *L, int size)
{
    const sudoku_geom_t *G = L->G;
    const int n = G->n;
    for (int orient = 0; orient < 2; orient++) {   /* 0: base rows, cover columns; 1: the other way */
        for (int d = 1; d <= n; d++) {
            const uint16_t m = (uint16_t)(1u << d);
            int line[9], nl = 0;
            uint16_t pos[9];
            for (int i = 0; i < n; i++) {
                uint16_t p = 0;
                bool placed = false;
                for (int j = 0; j < n; j++) {
                    int c = FISH_CELL(i, j);
                    if (L->v[c] == d) {
                        placed = true;
                    } else if (L->cand[c] & m) {
                        p |= (uint16_t)(1u << j);
                    }
                }
                int k = gen_popcount(p);
                if (!placed && k >= 2 && k <= size) {
                    line[nl] = i;
                    pos[nl++] = p;
                }
            }
            int pick[3];
            for (pick[0] = 0; pick[0] < nl; pick[0]++) {
                for (pick[1] = pick[0] + 1; pick[1] < nl; pick[1]++) {
                    for (pick[2] = size == 3 ? pick[1] + 1 : nl; pick[2] < (size == 3 ? nl : nl + 1); pick[2]++) {
                        uint16_t cover = (uint16_t)(pos[pick[0]] | pos[pick[1]] | (size == 3 ? pos[pick[2]] : 0));
                        if (gen_popcount(cover) != size) {
                            continue;
                        }
                        bool base[9] = {false};
                        for (int s = 0; s < size; s++) {
                            base[line[pick[s]]] = true;
                        }
                        bool changed = false;
                        for (int j = 0; j < n; j++) {
                            if (!(cover >> j & 1)) {
                                continue;
                            }
                            for (int i = 0; i < n; i++) {
                                if (!base[i]) {
                                    changed |= eliminate(L, FISH_CELL(i, j), m);
                                }
                            }
                        }
                        if (changed) {
                            int cells[9], nc = 0;
                            for (int s = 0; s < size; s++) {
                                for (int j = 0; j < n; j++) {
                                    if (pos[pick[s]] >> j & 1) {
                                        cells[nc++] = FISH_CELL(line[pick[s]], j);
                                    }
                                }
                            }
                            const int u0 = orient * 9;    /* rows are units 0..8, columns 9..17 */
                            trace_cells(size == 2 ? TECH_X_WING : TECH_SWORDFISH, u0 + line[pick[0]],
                                        u0 + line[pick[1]], size == 3 ? u0 + line[pick[2]] : -1, d, cover,
                                        cells, nc);
                            return true;
                        }
                    }
                }
            }
        }
#undef FISH_CELL
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_xy_wing(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {
        int p = G->cells[k];
        uint16_t pm = L->cand[p];
        if (L->v[p] || gen_popcount(pm) != 2) {
            continue;
        }
        for (int i = 0; i < G->npeers[p]; i++) {
            int a = G->peers[p][i];
            uint16_t am = L->cand[a];
            if (L->v[a] || gen_popcount(am) != 2 || gen_popcount(am & pm) != 1) {
                continue;
            }
            uint16_t z = (uint16_t)(am & ~pm);                   /* a is x or z */
            uint16_t want = (uint16_t)((pm & ~am) | z);          /* b must be y or z */
            for (int j = 0; j < G->npeers[p]; j++) {
                int b = G->peers[p][j];
                if (b == a || L->v[b] || L->cand[b] != want) {
                    continue;
                }
                bool changed = false;
                for (int q = 0; q < G->ncells; q++) {
                    int c = G->cells[q];
                    if (c == a || c == b || c == p) {
                        continue;
                    }
                    if (is_sudoku_geom_peer(G, c, a) && is_sudoku_geom_peer(G, c, b)) {
                        changed |= eliminate(L, c, z);
                    }
                }
                if (changed) {
                    const int cells[3] = {p, a, b};
                    bool first = s_trace && s_trace->kind == TECH_NONE;
                    trace_cells(TECH_XY_WING, -1, -1, -1, gen_lowest(z), pm, cells, 3);
                    if (first) {                                 /* the pincers' two digits each */
                        s_trace->cell1 = am;
                        s_trace->cell2 = want;
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
int gen_technique_tier(const sudoku_geom_t *G, const uint8_t givens[81])
{
    static logic_t L;
    gen_logic_load(&L, G, givens);
    int tier = 1;
    while (L.empty && !L.broken) {
        if (gen_singles(&L)) {
            continue;
        }
        if (G->ncages && gen_cage_logic(&L)) {
            continue;    /* Killer: cage sums count as the simple step */
        }
        if (G->consec_on && gen_consec_logic(&L)) {
            continue;    /* Consecutive: so do the bars */
        }
        if (G->nthermos && gen_thermo_logic(&L)) {
            continue;    /* Thermo: and the thermometers */
        }
        if (G->sandwich && gen_sandwich_logic(&L)) {
            continue;    /* Sandwich: and where the 1 and 9 can be */
        }
        if (G->edge_kind && gen_edge_logic(&L)) {
            continue;    /* Kropki, XV, Greater-Than: the marks */
        }
        if (G->nquads && gen_quad_logic(&L)) {
            continue;    /* Quadruple: the circles */
        }
        if (G->skyscraper && gen_sky_logic(&L)) {
            continue;    /* Skyscraper: the clues' lines */
        }
        if (G->nlk && gen_lk_logic(&L)) {
            continue;    /* Little Killer: the diagonals */
        }
        if (G->xsums && gen_xsum_logic(&L)) {
            continue;    /* X-Sums: the clues */
        }
        if (G->nrooms && gen_nroom_logic(&L)) {
            continue;    /* Numbered Rooms: the clues */
        }
        if (G->skysum && gen_skysum_logic(&L)) {
            continue;    /* Sum Skyscrapers: the clues' lines */
        }
        if ((G->frame || G->outside || G->rossini) && gen_first3_logic(&L)) {
            continue;    /* Frame, Outside Sudoku, Rossini: the clues */
        }
        if (G->idx159 && gen_159_logic(&L)) {
            continue;
        }
        if (G->antidiag && gen_antidiag_logic(&L)) {
            continue;
        }
        if (G->flatmates && gen_flatmates_logic(&L)) {
            continue;
        }
        if (G->nmx && gen_mathrax_logic(&L)) {
            continue;    /* Mathrax: the circles */
        }
        if (G->psym && gen_psym_logic(&L)) {
            continue;    /* Point Symmetry: the mirrors */
        }
        if (G->quadro && gen_quadro_logic(&L)) {
            continue;    /* Quadro: the 2x2 blocks */
        }
        if (G->aq9 && gen_aq9_logic(&L)) {
            continue;    /* Anti-Queen 9s: the diagonals */
        }
        if (G->n9 && gen_n9_logic(&L)) {
            continue;    /* Next to Nine: the clues */
        }
        if (G->nclone && gen_clone_logic(&L)) {
            continue;    /* Clone Sudoku: the clones */
        }
        if (G->s9 && gen_s9_logic(&L)) {
            continue;    /* Search Nine: the arrows */
        }
        if (G->bf && gen_bf_logic(&L)) {
            continue;    /* Battlefield: the clues */
        }
        if (G->b9 && gen_b9_logic(&L)) {
            continue;    /* Before Nine: the clues */
        }
        if (G->magic && gen_magic_logic(&L)) {
            continue;
        }
        if (G->psky && gen_psky_logic(&L)) {
            continue;
        }
        if (G->diag_nc && gen_diagnc_logic(&L)) {
            continue;    /* Diagonal Non-Consecutive: the corners */
        }
        if (gen_locked_candidates(&L) || gen_pairs(&L)) {
            tier = 2;
            continue;
        }
        return 3;
    }
    return L.broken ? 3 : tier;
}

/* (documented in gen_internal.h) */
int gen_easy_min_clues(const sudoku_geom_t *G)
{
    if (G->variant == VAR_SUMLESS || G->variant == VAR_REGIONS) {
        return 26;
    }
    if (G->idx159) {
        return 30;
    }
    if (G->ncages) {
        return 8;
    }
    if (G->variant == VAR_NONCONSEC) {
        return 24;
    }
    if (G->variant == VAR_DIAGCONSEC) {
        return 22;    /* the marks say a lot */
    }
    if (G->diag_nc) {
        return 30;
    }
    if (G->edge_kind == EDGE_KROPKI) {
        return 14;    /* every dot is there, and every missing one says something */
    }
    if (G->edge_kind == EDGE_XV) {
        return 22;
    }
    if (G->edge_kind == EDGE_FORT) {
        return 26;
    }
    if (G->edge_kind == EDGE_GT) {
        return 16;
    }
    if (G->nquads || G->nmx) {
        return 28;
    }
    if (G->nthermos && G->line_kind != LINE_THERMO && G->line_kind != LINE_SLOW) {
        return 28;
    }
    if (G->antidiag || G->magic) {
        return 30;
    }
    if (G->psym || G->nclone || G->s9) {
        return 30;
    }
    if (G->skyscraper || G->nlk || G->xsums || G->frame || G->outside || G->rossini || G->nrooms || G->skysum || G->b9 || G->n9 ||
            G->psky || G->bf) {
        return 28;
    }
    if (G->consec_on) {
        return 12;    /* the bars say a lot */
    }
    if (G->variant == VAR_EVENODD) {
        return 30;
    }
    if (G->variant == VAR_KNIGHT || G->variant == VAR_KING || G->variant == VAR_DISJOINT || G->variant == VAR_ARGYLE ||
            G->variant == VAR_CHESS) {
        return 32;
    }
    if (G->variant == VAR_ASTERISK || G->variant == VAR_CENTERDOT || G->variant == VAR_GIRANDOLA) {
        return 34;
    }
    if (G->nthermos) {
        return 28;
    }
    if (G->sandwich) {
        return 24;
    }
    return G->n == 9 ? 36 : G->n == 6 ? 16 : 8;   /* (4x4: half its 16 cells; Medium digs to the minimum, ~5) */
}

/* (documented in generator.h) */
int sudoku_grade(const sudoku_geom_t *G, const uint8_t givens[81])
{
    /* solved by the logic alone (tier 1 or 2): one solution, proven; otherwise an exact count
     * (also for Jigsaw). The same answer as counting first, without the slow count. */
    int tier = gen_technique_tier(G, givens);
    if (tier >= 3 && sudoku_count_solutions(G, givens, 2) != 1) {
        return -1;
    }
    int clues = 0;
    for (int k = 0; k < G->ncells; k++) {
        clues += givens[G->cells[k]] != 0;
    }
    switch (tier) {
    case 1:
        return clues >= gen_easy_min_clues(G) ? GEN_EASY : GEN_MEDIUM;
    case 2:
        return GEN_HARD;
    default:
        return GEN_EXPERT;
    }
}


/* (documented in gen_internal.h) */
void gen_grade_init(void)
{
    for (int m = 0; m < 1024; m++) {
        int sum = 0;
        for (int d = 1; d <= 9; d++) {
            sum += (m >> d & 1) * d;
        }
        s_mask_sum[m] = (uint8_t)sum;
    }
}

/* (documented in gen_internal.h) */
void gen_set_trace(sudoku_tech_t *t)
{
    s_trace = t;
}

/**
 * @file gen_solver.c
 * @ingroup generator
 * @brief Counting and filling: the solver (every variant's rules checked in grid_free()), the
 *        solution count, a random full grid; the rule tables it uses.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Kropki, XV, Greater-Than: [kind][mark][a] = the digits b may be when the left / top
 * cell is a (s_edge_fwd), and the digits a may be when the right / bottom one is b
 * (s_edge_bwd) */
static uint16_t s_edge_fwd[4][3][10], s_edge_bwd[4][3][10];

/* (documented in gen_internal.h) */
void gen_edge_tables(void)
{
    for (int k = 0; k < 4; k++) {
        for (int m = 0; m < 3; m++) {
            for (int a = 1; a <= 9; a++) {
                uint16_t f = 0, b = 0;
                for (int o = 1; o <= 9; o++) {
                    if (is_sudoku_edge_ok(k, m, a, o)) {
                        f |= (uint16_t)(1u << o);
                    }
                    if (is_sudoku_edge_ok(k, m, o, a)) {
                        b |= (uint16_t)(1u << o);
                    }
                }
                s_edge_fwd[k][m][a] = f;
                s_edge_bwd[k][m][a] = b;
            }
        }
    }
}

/* (documented in gen_internal.h) */
uint16_t gen_edge_allow(const sudoku_geom_t *G, int i, const uint8_t *v, const uint16_t *cand)
{
    int k = G->edge_kind;
    uint16_t free = 0x3FE;
    const int nb[4] = {i % 9 > 0 ? i - 1 : -1, i % 9 < 8 ? i + 1 : -1, i > 8 ? i - 9 : -1, i < 72 ? i + 9 : -1};
    const int mk[4] = {nb[0] >= 0 ? EDGE_RIGHT(G->edge[i - 1]) : 0, EDGE_RIGHT(G->edge[i]),
                       nb[2] >= 0 ? EDGE_DOWN(G->edge[i - 9]) : 0, EDGE_DOWN(G->edge[i])
                      };
    for (int j = 0; j < 4; j++) {
        if (nb[j] < 0 || (k == EDGE_GT && !mk[j])) {
            continue;
        }
        bool before = j == 0 || j == 2;             /* the neighbour is left of / above i */
        uint16_t others = v[nb[j]] ? (uint16_t)(1u << v[nb[j]]) : cand ? cand[nb[j]] : 0;
        if (!others) {
            continue;    /* (empty, and no candidates to go by) */
        }
        uint16_t ok = 0;
        for (int w = 1; w <= 9; w++) {
            if (others >> w & 1) {
                ok |= before ? s_edge_fwd[k][mk[j]][w] : s_edge_bwd[k][mk[j]][w];
            }
        }
        free &= ok;
    }
    return free;
}

/* (documented in gen_internal.h) */
uint16_t gen_line_allow(const sudoku_geom_t *G, int t, int p, const uint8_t *v, const uint16_t *cand)
{
    const int n = G->thermo_n[t];
    const uint8_t *cell = G->thermo_cell[t];
#define LMASK(c) ((uint16_t)(v[c] ? (1u << v[c]) : cand ? cand[c] : 0x3FE))
#define LO(m) (__builtin_ctz(m))
#define HI(m) (31 - __builtin_clz(m))
#define RANGE(a, b) ((uint16_t)((a) > (b) || (b) < 1 || (a) > 9 ? 0 : \
                     (((1u << ((b) > 9 ? 10 : (b) + 1)) - 1) & ~((1u << ((a) < 1 ? 1 : (a))) - 1))))
    uint16_t free = 0x3FE;
    switch (G->line_kind) {
    case LINE_THERMO:                               /* above the cell before it, below the one after */
        if (p > 0 && v[cell[p - 1]]) {
            free &= (uint16_t)~((1u << (v[cell[p - 1]] + 1)) - 1);
        }
        if (p + 1 < n && v[cell[p + 1]]) {
            free &= (uint16_t)((1u << v[cell[p + 1]]) - 1);
        }
        if (cand) {
            if (p > 0 && LMASK(cell[p - 1])) {
                free &= (uint16_t)~((1u << (LO(LMASK(cell[p - 1])) + 1)) - 1);
            }
            if (p + 1 < n && LMASK(cell[p + 1])) {
                free &= (uint16_t)((1u << HI(LMASK(cell[p + 1]))) - 1);
            }
        }
        break;
    case LINE_WHISPER:                              /* 5 or more away from each neighbour on it */
        for (int k = -1; k <= 1; k += 2) {
            int q = p + k;
            if (q < 0 || q >= n) {
                continue;
            }
            uint16_t m = LMASK(cell[q]), ok = 0;
            if (!m) {
                continue;
            }
            for (int d = 1; d <= 9; d++) {
                for (int w = 1; w <= 9; w++) {
                    if ((m >> w & 1) && abs(d - w) >= 5) {
                        ok |= (uint16_t)(1u << d);
                        break;
                    }
                }
            }
            free &= ok;
        }
        break;
    case LINE_RENBAN: {                             /* different digits, all within a window of n */
        int lo = 10, hi = 0;
        for (int k = 0; k < n; k++) {
            if (k == p || !v[cell[k]]) {
                continue;
            }
            free &= (uint16_t)~(1u << v[cell[k]]);
            if (v[cell[k]] < lo) {
                lo = v[cell[k]];
            }
            if (v[cell[k]] > hi) {
                hi = v[cell[k]];
            }
        }
        if (hi) {
            free &= RANGE(hi - n + 1, lo + n - 1);
        }
        if (cand) {                                 /* a window that leaves every other cell a candidate */
            uint16_t ok = 0;
            for (int w0 = 1; w0 + n - 1 <= 9; w0++) {
                uint16_t win = RANGE(w0, w0 + n - 1);
                bool fits = true;
                for (int k = 0; k < n && fits; k++) {
                    if (k != p) {
                        fits = (LMASK(cell[k]) & win) != 0;
                    }
                }
                if (fits) {
                    ok |= win;
                }
            }
            free &= ok;
        }
        break;
    }
    case LINE_ARROW: {                              /* the circle (place 0) is the shaft's sum */
        int smin = 0, smax = 0;                     /* the shaft's sum, the place asked about left out */
        for (int k = 1; k < n; k++) {
            if (k == p) {
                continue;
            }
            uint16_t m = LMASK(cell[k]);
            if (!m) {
                return 0;
            }
            smin += LO(m);
            smax += HI(m);
        }
        if (p == 0) {
            free &= RANGE(smin, smax);
        } else {
            uint16_t m = LMASK(cell[0]);
            if (!m) {
                return 0;
            }
            free &= RANGE(LO(m) - smax, HI(m) - smin);
        }
        break;
    }
    }
#undef LMASK
#undef LO
#undef HI
#undef RANGE
    return free;
}

int sudoku_quad_missing(const sudoku_geom_t *g, int q, const uint8_t vals[81], int *empty)
{
    int need[10] = {0}, miss = 0, at = g->quad_at[q];
    for (int k = 0; k < g->quad_n[q]; k++) {
        need[g->quad_dig[q][k]]++;
    }
    const int cells[4] = {at, at + 1, at + 9, at + 10};
    *empty = 0;
    for (int k = 0; k < 4; k++) {
        int v = vals[cells[k]];
        if (!v) {
            (*empty)++;
        } else if (need[v]) {
            need[v]--;
        }
    }
    for (int d = 1; d <= 9; d++) {
        miss += need[d];
    }
    return miss;
}

/* sums of k distinct digits from 2..8: bit s set if s can be made (k 0..7) */
static uint64_t s_bread[8];

/* (documented in gen_internal.h) */
void gen_bread_tables(void)
{
    if (s_bread[0]) {
        return;
    }
    for (int m = 0; m < 128; m++) {                 /* subsets of 2..8 */
        int k = __builtin_popcount(m), sum = 0;
        for (int b = 0; b < 7; b++) {
            if (m >> b & 1) {
                sum += b + 2;
            }
        }
        s_bread[k] |= 1ull << sum;
    }
}

/* ---------------------------------------------------------------- counting / filling */

/* Called now and then from the long loops (each attempt, each clue tried, every 4096 search
 * nodes). A no-op here (and on a PC); the firmware's own gives the idle task a turn, since a
 * puzzle can take more than the task watchdog's 5 s (Greater-Than Expert did, on core 0). */
__attribute__((weak)) void sudoku_gen_yield(void) {}

static uint32_t s_yield_nodes;

typedef struct {
    const sudoku_geom_t *G;
    uint8_t v[81];
    uint16_t um[UNITS_MAX];             /* bit d = digit d used in the unit */
    int8_t units[81][5];                /* the cell's units, -1 terminated */
    uint32_t nodes, node_limit;         /* node_limit 0 = no limit; reached: aborted */
    bool aborted;
    /* Killer: per cage, the digits in it, what's left of its sum, its empty cells */
    uint16_t cage_used[CAGES_MAX];
    int8_t cage_left[CAGES_MAX], cage_empty[CAGES_MAX];
} grid_t;

static void grid_set(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = (uint8_t)d;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) {
        g->um[g->units[i][k]] |= m;
    }
    int c = g->G->cage_of[i];
    if (c != CAGE_NONE) {
        g->cage_used[c] |= m;
        g->cage_left[c] = (int8_t)(g->cage_left[c] - d);
        g->cage_empty[c]--;
    }
}

static void grid_clear(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) {
        g->um[g->units[i][k]] &= (uint16_t)~m;
    }
    int c = g->G->cage_of[i];
    if (c != CAGE_NONE) {
        g->cage_used[c] &= (uint16_t)~m;
        g->cage_left[c] = (int8_t)(g->cage_left[c] + d);
        g->cage_empty[c]++;
    }
}

/* smallest and largest sums of k distinct digits from mask */
static void mask_k_range(uint16_t mask, int k, int *lo, int *hi)
{
    *lo = *hi = 0;
    for (int d = 1, n = 0; d <= 9 && n < k; d++) {
        if (mask >> d & 1) {
            *lo += d;
            n++;
        }
    }
    for (int d = 9, n = 0; d >= 1 && n < k; d--) {
        if (mask >> d & 1) {
            *hi += d;
            n++;
        }
    }
}

/* (documented in gen_internal.h) */
bool is_gen_sky_line_ok(const uint8_t x[9], int c)
{
    int seen = 0, top = 0, k = 0;
    for (; k < 9 && x[k]; k++) {
        if (x[k] > top) {
            top = x[k];
            seen++;
        }
    }
    if (seen > c) {
        return false;
    }
    if (top == 9 || k == 9) {
        return seen == c;    /* the 9 hides the rest; or the line is full */
    }
    int rest = 9 - k, more = 9 - top < rest ? 9 - top : rest;   /* at most this many more seen */
    return seen < c && seen + more >= c;            /* (the 9, still to come, is seen) */
}

/* sudoku_fits_why(): which rule took the digit asked about out of grid_free()'s result (NULL:
 * nobody asks, the usual case: one predictable branch per rule) */
typedef struct { int d, code; } why_t;   /* (one type for both ends: no aliasing surprise) */

static why_t *s_why;

#define WHY(c) do { if (s_why && !s_why->code && !(free >> s_why->d & 1)) s_why->code = (c); } while (0)

static uint16_t grid_free(const grid_t *g, int i)
{
    uint16_t used = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) {
        used |= g->um[g->units[i][k]];
    }
    const sudoku_geom_t *G = g->G;
    uint16_t free = (uint16_t)(G->allow[i] & ~used);
    if (s_why && !s_why->code && !(free >> s_why->d & 1)) {
        s_why->code = (used >> s_why->d & 1) ? FIT_UNIT : FIT_ALLOW;
    }
    for (int k = 0; k < G->nxpeers[i]; k++) {       /* Anti-Knight */
        int v = g->v[G->xpeers[i][k]];
        if (v) {
            free &= (uint16_t)~(1u << v);
        }
    }
    WHY(FIT_XPEER);
    if (G->consec_on && free) {                     /* Consecutive: a bar = differ by 1, none = don't */
        const int nb[4] = {i % 9 > 0 ? i - 1 : -1, i % 9 < 8 ? i + 1 : -1, i > 8 ? i - 9 : -1, i < 72 ? i + 9 : -1};
        const bool bar[4] = {nb[0] >= 0 && (G->consec[i - 1] & CONSEC_RIGHT), (G->consec[i] & CONSEC_RIGHT) != 0,
                             nb[2] >= 0 && (G->consec[i - 9] & CONSEC_DOWN), (G->consec[i] & CONSEC_DOWN) != 0
                            };
        for (int k = 0; k < 4; k++) {
            if (nb[k] < 0 || !g->v[nb[k]]) {
                continue;
            }
            int w = g->v[nb[k]];
            uint16_t near = (uint16_t)(((1u << (w + 1)) | (1u << (w - 1))) & 0x3FE);
            free &= bar[k] ? near : (uint16_t)~near;
        }
    }
    WHY(FIT_CONSEC);
    if (G->edge_kind && free) {
        free &= gen_edge_allow(G, i, g->v, NULL);    /* Kropki, XV, Greater-Than */
    }
    WHY(FIT_EDGE);
    for (int s = 0; s < 4 && free && G->nquads && G->quad_of[i][s] != QUAD_NONE; s++) {
        int empty, miss = sudoku_quad_missing(G, G->quad_of[i][s], g->v, &empty);   /* (i is one of the empty) */
        if (miss > empty) {
            free = 0;    /* Quadruple: not enough room left for its digits */
        } else if (miss == empty) {                 /* every empty cell must take one of them */
            int need[10] = {0}, q = G->quad_of[i][s], at = G->quad_at[q];
            for (int k = 0; k < G->quad_n[q]; k++) {
                need[G->quad_dig[q][k]]++;
            }
            const int cells[4] = {at, at + 1, at + 9, at + 10};
            for (int k = 0; k < 4; k++) {
                if (g->v[cells[k]] && need[g->v[cells[k]]]) {
                    need[g->v[cells[k]]]--;
                }
            }
            uint16_t m = 0;
            for (int d = 1; d <= 9; d++) {
                if (need[d]) {
                    m |= (uint16_t)(1u << d);
                }
            }
            free &= m;
        }
    }
    WHY(FIT_QUAD);
    for (int line = 0; G->skyscraper && free && line < 2; line++) {   /* Skyscraper: its row's, its column's clue */
        int lu = line ? 9 + i % 9 : i / 9, c = G->sky[lu], cells[9];
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        uint8_t x[9];
        for (int k = 0; k < 9; k++) {
            x[k] = g->v[cells[k]];
        }
        int at = line ? i / 9 : i % 9;
        for (int d = 1; d <= 9; d++) {
            if (!(free >> d & 1)) {
                continue;
            }
            x[at] = (uint8_t)d;
            if (!is_gen_sky_line_ok(x, c)) {
                free &= (uint16_t)~(1u << d);
            }
        }
        WHY(line ? FIT_SKY_COL : FIT_SKY_ROW);
    }
    for (int slot = 0; G->nlk && free && slot < 2; slot++) {   /* Little Killer: what its diagonal's sum leaves */
        int k = G->lk_of[i][slot];
        if (k == LK_NONE) {
            continue;
        }
        int sum = 0, empty = 0;
        for (int j = 0; j < G->lk_n[k]; j++) {
            int o = G->lk_cell[k][j];
            if (o == i) {
                continue;
            }
            if (g->v[o]) {
                sum += g->v[o];
            } else {
                empty++;
            }
        }
        int lo = G->lk_sum[k] - sum - 9 * empty, hi = G->lk_sum[k] - sum - empty;
        for (int d = 1; d <= 9; d++) {
            if (d < lo || d > hi) {
                free &= (uint16_t)~(1u << d);
            }
        }
    }
    WHY(FIT_LK);
    if (G->sandwich && free) {                      /* Sandwich: a digit that can't keep a line's sum goes */
        for (int line = 0; line < 2 && free; line++) {
            int lu = line ? 9 + i % 9 : i / 9, ki = line ? i / 9 : i % 9, vals[9], p1 = -1, p9 = -1;
            for (int k = 0; k < 9; k++) {
                int c = line ? k * 9 + i % 9 : (i / 9) * 9 + k;
                vals[k] = c == i ? 0 : g->v[c];
                if (vals[k] == 1) {
                    p1 = k;
                }
                if (vals[k] == 9) {
                    p9 = k;
                }
            }
            if (p1 < 0 && p9 < 0) {
                continue;    /* neither end placed: no 1 / 9 here can be judged yet */
            }
            /* with both ends already placed: what's between them so far */
            int bsum = 0, bempty = 0, ba = -1, bb = -1;
            if (p1 >= 0 && p9 >= 0) {
                ba = p1 < p9 ? p1 : p9;
                bb = p1 < p9 ? p9 : p1;
                for (int k = ba + 1; k < bb; k++) {
                    if (vals[k]) {
                        bsum += vals[k];
                    } else {
                        bempty++;
                    }
                }
            }
            for (int d = 1; d <= 9; d++) {
                if (!(free >> d & 1)) {
                    continue;
                }
                int sum, empty;
                if (d == 1 || d == 9) {                /* this cell becomes an end */
                    int q1 = d == 1 ? ki : p1, q9 = d == 9 ? ki : p9;
                    if (q1 < 0 || q9 < 0) {
                        continue;
                    }
                    int a = q1 < q9 ? q1 : q9, b = q1 < q9 ? q9 : q1;
                    sum = empty = 0;
                    for (int k = a + 1; k < b; k++) {
                        if (vals[k]) {
                            sum += vals[k];
                        } else {
                            empty++;
                        }
                    }
                } else {
                    if (ba < 0) {
                        continue;    /* an end still missing */
                    }
                    sum = bsum;
                    empty = bempty;
                    if (ki > ba && ki < bb) {          /* this cell is between them */
                        sum += d;
                        empty--;
                    }
                }
                int left = G->sandwich_sum[lu] - sum;
                if (left < 0 || !(s_bread[empty] >> left & 1)) {
                    free &= (uint16_t)~(1u << d);
                }
            }
            WHY(line ? FIT_SANDWICH_COL : FIT_SANDWICH_ROW);
        }
    }
    int t = G->thermo_of[i];
    if (t != THERMO_NONE && free) {
        free &= gen_line_allow(G, t, G->thermo_pos[i], g->v, NULL);    /* the lines */
    }
    WHY(FIT_THERMO);
    int c = G->cage_of[i];
    if (c != CAGE_NONE && free) {                    /* Killer: what the cage's sum still allows */
        free &= (uint16_t)~g->cage_used[c];
        int left = g->cage_left[c], others = g->cage_empty[c] - 1;
        uint16_t ok = 0;
        for (int d = 1; d <= 9; d++) {
            if (!(free >> d & 1) || d > left) {
                continue;
            }
            if (others == 0) {
                if (d == left) {
                    ok |= (uint16_t)(1u << d);
                }
                continue;
            }
            int lo, hi;
            mask_k_range((uint16_t)(g->G->all & ~g->cage_used[c] & ~(1u << d)), others, &lo, &hi);
            if (left - d >= lo && left - d <= hi) {
                ok |= (uint16_t)(1u << d);
            }
        }
        free = ok;
    }
    WHY(FIT_CAGE);
    return free;
}

static bool grid_load(grid_t *g, const sudoku_geom_t *G, const uint8_t *cells)
{
    memset(g, 0, sizeof(*g));
    g->G = G;
    memset(g->units, -1, sizeof(g->units));
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], us[5], n = cell_units(G, i, us);
        for (int j = 0; j < n; j++) {
            g->units[i][j] = (int8_t)us[j];
        }
    }
    for (int c = 0; c < G->ncages; c++) {
        g->cage_left[c] = (int8_t)G->cage_sum[c];
        g->cage_empty[c] = (int8_t)G->cage_n[c];
    }
    if (!cells) {
        return true;
    }
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], d = cells[i];
        if (d < 1 || d > G->n) {
            continue;
        }
        if (!(grid_free(g, i) & (1u << d))) {
            return false;
        }
        grid_set(g, i, d);
    }
    return true;
}

/* counts solutions up to limit; with rng, fills g with a random solution */
static int search(grid_t *g, int limit, uint32_t *rng)
{
    const sudoku_geom_t *G = g->G;
    if (!(++s_yield_nodes & 4095)) {
        sudoku_gen_yield();
    }
    if (g->node_limit && ++g->nodes > g->node_limit) {   /* a Jigsaw layout that's hard to fill: give up */
        g->aborted = true;
        return limit;
    }
    int best = -1, best_n = 10;
    uint16_t best_free = 0;
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k];
        if (g->v[i]) {
            continue;
        }
        uint16_t free = grid_free(g, i);
        int n = __builtin_popcount(free);
        if (n < best_n) {
            best = i;
            best_n = n;
            best_free = free;
            if (n <= 1) {
                break;
            }
        }
    }
    if (best < 0) {
        return 1;
    }
    if (best_n == 0) {
        return 0;
    }
    int count = 0;
    uint8_t order[9];
    int no = 0;
    for (int d = 1; d <= G->n; d++) {
        if (best_free & (1u << d)) {
            order[no++] = (uint8_t)d;
        }
    }
    if (rng) {
        for (int i = no - 1; i > 0; i--) {
            int j = (int)(rnd(rng) % (uint32_t)(i + 1));
            uint8_t t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
    }
    for (int k = 0; k < no; k++) {
        int d = order[k];
        grid_set(g, best, d);
        count += search(g, limit - count, rng);
        if (count >= limit) {
            return count;    /* keeps g filled (the solution) */
        }
        grid_clear(g, best, d);
    }
    return count;
}

/* (documented in gen_internal.h) */
int gen_count_bounded(const sudoku_geom_t *G, const uint8_t cells[81], int limit, uint32_t node_limit)
{
    static grid_t g;                                 /* off the caller's stack (one caller at a time) */
    if (!grid_load(&g, G, cells)) {
        return 0;
    }
    g.node_limit = node_limit;
    int n = search(&g, limit, NULL);
    return g.aborted ? -1 : n;
}

int sudoku_count_solutions(const sudoku_geom_t *G, const uint8_t cells[81], int limit)
{
    return gen_count_bounded(G, cells, limit, 0);
}

bool is_sudoku_classic_unique(const uint8_t cells[81])
{
    static grid_t g;                                 /* its own: another task than the generator's */
    if (!grid_load(&g, sudoku_geom(VAR_CLASSIC), cells)) {
        return false;
    }
    g.node_limit = 200000;                           /* (a broken puzzle can't hold the task for long) */
    return search(&g, 2, NULL) == 1 && !g.aborted;
}

bool is_sudoku_unique(const sudoku_geom_t *G, const uint8_t cells[81])
{
    static grid_t g;                                 /* its own (the import's task) */
    if (!grid_load(&g, G, cells)) {
        return false;
    }
    g.node_limit = 400000;
    return search(&g, 2, NULL) == 1 && !g.aborted;
}

bool sudoku_solve(const sudoku_geom_t *G, const uint8_t cells[81], uint8_t out[81])
{
    static grid_t g;                                 /* off the (small) LVGL task stack */
    if (!grid_load(&g, G, cells)) {
        return false;
    }
    if (search(&g, 1, NULL) < 1) {
        return false;
    }
    memcpy(out, g.v, 81);
    return true;
}

int sudoku_fits_why(const sudoku_geom_t *G, const uint8_t vals[81], int cell, int d)
{
    static grid_t g;
    if (cell < 0 || cell > 80 || d < 1 || d > G->n) {
        return FIT_UNIT;
    }
    if (vals[cell]) {
        return FIT_FILLED;
    }
    if (!grid_load(&g, G, vals)) {
        return FIT_BROKEN;
    }
    why_t w = {d, FIT_OK};
    s_why = &w;
    grid_free(&g, cell);
    s_why = NULL;
    return w.code;
}

bool is_sudoku_fits(const sudoku_geom_t *G, const uint8_t vals[81], int cell, int d)
{
    static grid_t g;
    if (cell < 0 || cell > 80 || vals[cell] || d < 1 || d > G->n || !grid_load(&g, G, vals)) {
        return false;
    }
    return (grid_free(&g, cell) >> d & 1) != 0;
}

/* (documented in gen_internal.h) */
bool gen_random_full_grid(const sudoku_geom_t *G, uint8_t out[81], uint32_t *rng, uint32_t node_limit)
{
    static grid_t g;
    grid_load(&g, G, NULL);
    g.node_limit = node_limit;
    int n = search(&g, 1, rng);
    memcpy(out, g.v, 81);
    return n >= 1 && !g.aborted;
}

/* (documented in gen_internal.h) */
uint64_t gen_bread_sums(int k)
{
    return s_bread[k];
}

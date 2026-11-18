/* See generator.h. */
#include "generator.h"
#include <string.h>
#include <stdlib.h>

const char *const GEN_LEVEL_NAMES[GEN_LEVELS] = {"Easy", "Medium", "Hard", "Expert"};
const char *const VARIANT_NAMES[VAR_COUNT] = {"Classic", "X-Sudoku", "Mini 6x6", "Hyper", "Jigsaw"};

/* ---------------------------------------------------------------- geometry */

/* ~25 KB: from malloc, which the sketch sends to PSRAM (internal RAM is
 * kept for Wi-Fi and the video encoder) */
static sudoku_geom_t *s_geom;

/* regions: Jigsaw's region map, NULL = boxes of box_h x box_w */
static void geom_build(sudoku_geom_t *g, int variant, int n, int box_h, int box_w, bool diag, bool windows,
                       const uint8_t *regions)
{
    memset(g, 0, sizeof(*g));
    g->variant = (uint8_t)variant;
    g->n = (uint8_t)n;
    g->box_h = (uint8_t)box_h;
    g->box_w = (uint8_t)box_w;
    g->diag = diag;
    g->windows = windows;
    g->all = (uint16_t)(((1u << (n + 1)) - 1) & ~1u);
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int cell = r * 9 + c;
            g->cells[g->ncells++] = (uint8_t)cell;
            g->used[cell] = true;
        }
    }
    int boxes_across = n / box_w;
    int fill[UNITS_MAX] = {0};
    for (int k = 0; k < g->ncells; k++) {
        int cell = g->cells[k], r = cell / 9, c = cell % 9;
        int ids[5], nid = 0;
        ids[nid++] = r;
        ids[nid++] = 9 + c;
        g->region[cell] = (uint8_t)(regions ? regions[cell] : (r / box_h) * boxes_across + c / box_w);
        ids[nid++] = 18 + g->region[cell];
        if (diag && r == c) ids[nid++] = UNIT_DIAG_MAIN;
        if (diag && r + c == n - 1) ids[nid++] = UNIT_DIAG_ANTI;   /* centre cell: both (5 units) */
        int w = sudoku_geom_window(g, cell);
        if (w >= 0) ids[nid++] = UNIT_WINDOW + w;
        for (int i = 0; i < nid; i++) {
            int u = ids[i];
            g->unit[u][fill[u]++] = (uint8_t)cell;
            g->active[u] = true;
        }
        for (int i = 0; i < nid; i++) g->cu[cell][i] = (uint8_t)ids[i];
        g->ncu[cell] = (uint8_t)nid;
    }
    for (int u = 0; u < UNITS_MAX; u++) {
        if (g->active[u]) g->unit_ids[g->nunits++] = (uint8_t)u;
    }
    for (int k = 0; k < g->ncells; k++) {                   /* peers: share any unit */
        int cell = g->cells[k];
        for (int i = 0; i < g->nunits; i++) {
            int u = g->unit_ids[i];
            bool in = false;
            for (int j = 0; j < n; j++) in |= g->unit[u][j] == cell;
            if (!in) continue;
            for (int j = 0; j < n; j++) {
                int o = g->unit[u][j];
                if (o == cell || sudoku_geom_peer(g, cell, o)) continue;
                g->peer_bits[cell][o >> 5] |= 1u << (o & 31);
                g->peers[cell][g->npeers[cell]++] = (uint8_t)o;
            }
        }
    }
}

void sudoku_geom_init(void)
{
    if (s_geom) return;
    sudoku_geom_t *g = calloc(VAR_COUNT, sizeof(sudoku_geom_t));
    if (!g) abort();                                /* at start-up, with megabytes free */
    geom_build(&g[VAR_CLASSIC], VAR_CLASSIC, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_X], VAR_X, 9, 3, 3, true, false, NULL);
    geom_build(&g[VAR_MINI], VAR_MINI, 6, 2, 3, false, false, NULL);
    geom_build(&g[VAR_HYPER], VAR_HYPER, 9, 3, 3, false, true, NULL);
    g[VAR_JIGSAW] = g[VAR_CLASSIC];                 /* placeholder: a Jigsaw board needs its regions */
    s_geom = g;
}

const sudoku_geom_t *sudoku_geom(int variant)
{
    sudoku_geom_init();
    if (variant < 0 || variant >= VAR_COUNT || variant == VAR_JIGSAW) variant = VAR_CLASSIC;
    return &s_geom[variant];
}

bool sudoku_geom_jigsaw(const uint8_t regions[81], sudoku_geom_t *out)
{
    int count[9] = {0};
    for (int c = 0; c < 81; c++) {
        if (regions[c] > 8) return false;
        count[regions[c]]++;
    }
    for (int i = 0; i < 9; i++) {
        if (count[i] != 9) return false;
    }
    geom_build(out, VAR_JIGSAW, 9, 3, 3, false, false, regions);
    return true;
}

/* the units of a cell (3, 4 or, at the centre of an X board, 5) */
static int cell_units(const sudoku_geom_t *G, int cell, int out[5])
{
    int n = G->ncu[cell];
    for (int i = 0; i < n; i++) out[i] = G->cu[cell][i];
    return n;
}

static bool in_unit(const sudoku_geom_t *G, int cell, int u)
{
    int us[5], n = cell_units(G, cell, us);
    for (int i = 0; i < n; i++) {
        if (us[i] == u) return true;
    }
    return false;
}

static uint32_t rnd(uint32_t *s)        /* xorshift32 */
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* ---------------------------------------------------------------- counting / filling */

typedef struct {
    const sudoku_geom_t *G;
    uint8_t v[81];
    uint16_t um[UNITS_MAX];             /* bit d = digit d used in the unit */
    int8_t units[81][5];                /* the cell's units, -1 terminated */
    uint32_t nodes, node_limit;         /* node_limit 0 = no limit; reached: aborted */
    bool aborted;
} grid_t;

static void grid_set(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = (uint8_t)d;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) g->um[g->units[i][k]] |= m;
}

static void grid_clear(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) g->um[g->units[i][k]] &= (uint16_t)~m;
}

static uint16_t grid_free(const grid_t *g, int i)
{
    uint16_t used = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) used |= g->um[g->units[i][k]];
    return (uint16_t)(g->G->all & ~used);
}

static bool grid_load(grid_t *g, const sudoku_geom_t *G, const uint8_t *cells)
{
    memset(g, 0, sizeof(*g));
    g->G = G;
    memset(g->units, -1, sizeof(g->units));
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], us[5], n = cell_units(G, i, us);
        for (int j = 0; j < n; j++) g->units[i][j] = (int8_t)us[j];
    }
    if (!cells) return true;
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], d = cells[i];
        if (d < 1 || d > G->n) continue;
        if (!(grid_free(g, i) & (1u << d))) return false;
        grid_set(g, i, d);
    }
    return true;
}

/* counts solutions up to limit; with rng, fills g with a random solution */
static int search(grid_t *g, int limit, uint32_t *rng)
{
    const sudoku_geom_t *G = g->G;
    if (g->node_limit && ++g->nodes > g->node_limit) {   /* a Jigsaw layout that's hard to fill: give up */
        g->aborted = true;
        return limit;
    }
    int best = -1, best_n = 10;
    uint16_t best_free = 0;
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k];
        if (g->v[i]) continue;
        uint16_t free = grid_free(g, i);
        int n = __builtin_popcount(free);
        if (n < best_n) {
            best = i; best_n = n; best_free = free;
            if (n <= 1) break;
        }
    }
    if (best < 0) return 1;
    if (best_n == 0) return 0;
    int count = 0;
    uint8_t order[9];
    int no = 0;
    for (int d = 1; d <= G->n; d++) if (best_free & (1u << d)) order[no++] = (uint8_t)d;
    if (rng) {
        for (int i = no - 1; i > 0; i--) {
            int j = (int)(rnd(rng) % (uint32_t)(i + 1));
            uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
        }
    }
    for (int k = 0; k < no; k++) {
        int d = order[k];
        grid_set(g, best, d);
        count += search(g, limit - count, rng);
        if (count >= limit) return count;           /* keeps g filled (the solution) */
        grid_clear(g, best, d);
    }
    return count;
}

/* solutions up to limit, giving up after node_limit search steps (0 = never): -1 then */
static int count_bounded(const sudoku_geom_t *G, const uint8_t cells[81], int limit, uint32_t node_limit)
{
    static grid_t g;                                 /* off the caller's stack (one caller at a time) */
    if (!grid_load(&g, G, cells)) return 0;
    g.node_limit = node_limit;
    int n = search(&g, limit, NULL);
    return g.aborted ? -1 : n;
}

int sudoku_count_solutions(const sudoku_geom_t *G, const uint8_t cells[81], int limit)
{
    return count_bounded(G, cells, limit, 0);
}

bool sudoku_solve(const sudoku_geom_t *G, const uint8_t cells[81], uint8_t out[81])
{
    static grid_t g;                                 /* off the (small) LVGL task stack */
    if (!grid_load(&g, G, cells)) return false;
    if (search(&g, 1, NULL) < 1) return false;
    memcpy(out, g.v, 81);
    return true;
}

/* false if node_limit (0 = none) was reached first */
static bool random_full_grid(const sudoku_geom_t *G, uint8_t out[81], uint32_t *rng, uint32_t node_limit)
{
    static grid_t g;
    grid_load(&g, G, NULL);
    g.node_limit = node_limit;
    int n = search(&g, 1, rng);
    memcpy(out, g.v, 81);
    return n >= 1 && !g.aborted;
}

/* ---------------------------------------------------------------- human-style grading */

typedef struct {
    const sudoku_geom_t *G;
    uint8_t v[81];
    uint16_t cand[81];
    int empty;
    bool broken;
} logic_t;

static void place(logic_t *L, int c, int d)
{
    const sudoku_geom_t *G = L->G;
    L->v[c] = (uint8_t)d;
    L->cand[c] = 0;
    L->empty--;
    for (int p = 0; p < G->npeers[c]; p++) {
        int o = G->peers[c][p];
        L->cand[o] &= (uint16_t)~(1u << d);
        if (!L->v[o] && !L->cand[o]) L->broken = true;
    }
}

static void logic_load(logic_t *L, const sudoku_geom_t *G, const uint8_t *vals)
{
    memset(L, 0, sizeof(*L));
    L->G = G;
    L->empty = G->ncells;
    for (int k = 0; k < G->ncells; k++) L->cand[G->cells[k]] = G->all;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c] >= 1 && vals[c] <= G->n) place(L, c, vals[c]);
    }
}

static bool singles(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {           /* naked single */
        int c = G->cells[k];
        if (!L->v[c] && __builtin_popcount(L->cand[c]) == 1) {
            place(L, c, __builtin_ctz(L->cand[c]));
            return true;
        }
    }
    for (int i = 0; i < G->nunits; i++) {           /* hidden single */
        int u = G->unit_ids[i];
        for (int d = 1; d <= G->n; d++) {
            int where = -1, n = 0;
            for (int k = 0; k < G->n; k++) {
                int c = G->unit[u][k];
                if (L->v[c] == d) { n = -1; break; }
                if (L->cand[c] & (1u << d)) { where = c; n++; }
            }
            if (n == 1) {
                place(L, where, d);
                return true;
            }
        }
    }
    return false;
}

/* the first technique used while looking for the next step (sudoku_next_step) */
static sudoku_tech_t *s_trace;

static void trace(int kind, int unit, int unit2, int d1, int d2, int c1, int c2)
{
    if (!s_trace || s_trace->kind != TECH_NONE) return;
    s_trace->kind = kind;
    s_trace->unit = unit;
    s_trace->unit2 = unit2;
    s_trace->digit = d1;
    s_trace->digit2 = d2;
    s_trace->cell1 = c1;
    s_trace->cell2 = c2;
}

static bool eliminate(logic_t *L, int c, uint16_t bits)
{
    if (L->v[c] || !(L->cand[c] & bits)) return false;
    L->cand[c] &= (uint16_t)~bits;
    if (!L->cand[c]) L->broken = true;
    return true;
}

/* pointing / claiming: a digit confined to one line within a box (or to one
 * box within a line) can't be elsewhere in that line (or box). Lines are
 * rows, columns and, on an X board, the diagonals. */
static bool locked_candidates(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    bool changed = false;
    for (int bi = 0; bi < 9 + 4; bi++) {               /* boxes (regions), then Hyper windows */
        int b = bi < 9 ? 18 + bi : UNIT_WINDOW + bi - 9;
        if (!G->active[b]) continue;
        for (int oi = 0; oi < G->nunits; oi++) {
            int other = G->unit_ids[oi];
            if (other == b || (b < 27 && other >= 18 && other < 27)) continue;   /* boxes don't overlap */
            for (int d = 1; d <= G->n; d++) {
                uint16_t m = (uint16_t)(1u << d);
                int in_both = 0, box_only = 0, line_only = 0;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[b][k];
                    if (!(L->cand[c] & m)) continue;
                    if (in_unit(G, c, other)) in_both++; else box_only++;
                }
                if (!in_both) continue;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[other][k];
                    if (!(L->cand[c] & m)) continue;
                    if (!in_unit(G, c, b)) line_only++;
                }
                if (!box_only && line_only) {                 /* pointing: clear the rest of the line */
                    for (int k = 0; k < G->n; k++) {
                        int c = G->unit[other][k];
                        if (!in_unit(G, c, b)) changed |= eliminate(L, c, m);
                    }
                    if (changed) trace(TECH_POINTING, b, other, d, 0, -1, -1);
                } else if (!line_only && box_only) {          /* claiming: clear the rest of the box */
                    for (int k = 0; k < G->n; k++) {
                        int c = G->unit[b][k];
                        if (!in_unit(G, c, other)) changed |= eliminate(L, c, m);
                    }
                    if (changed) trace(TECH_CLAIMING, other, b, d, 0, -1, -1);
                }
                if (changed) return true;
            }
        }
    }
    return changed;
}

static bool pairs(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    const int n = G->n;
    for (int ui = 0; ui < G->nunits; ui++) {
        int u = G->unit_ids[ui];
        /* naked pair: two cells with the same two candidates */
        for (int a = 0; a < n; a++) {
            int ca = G->unit[u][a];
            if (L->v[ca] || __builtin_popcount(L->cand[ca]) != 2) continue;
            for (int b = a + 1; b < n; b++) {
                int cb = G->unit[u][b];
                if (L->v[cb] || L->cand[cb] != L->cand[ca]) continue;
                bool changed = false;
                for (int k = 0; k < n; k++) {
                    int c = G->unit[u][k];
                    if (c != ca && c != cb) changed |= eliminate(L, c, L->cand[ca]);
                }
                if (changed) {
                    int d1 = __builtin_ctz(L->cand[ca]), d2 = 31 - __builtin_clz(L->cand[ca]);
                    trace(TECH_NAKED_PAIR, u, -1, d1, d2, ca, cb);
                    return true;
                }
            }
        }
        /* hidden pair: two digits that fit only the same two cells */
        uint16_t where[10] = {0};                   /* bit k = unit cell k */
        for (int d = 1; d <= n; d++) {
            for (int k = 0; k < n; k++) {
                if (L->cand[G->unit[u][k]] & (1u << d)) where[d] |= (uint16_t)(1u << k);
            }
        }
        for (int d1 = 1; d1 <= n; d1++) {
            if (__builtin_popcount(where[d1]) != 2) continue;
            for (int d2 = d1 + 1; d2 <= n; d2++) {
                if (where[d2] != where[d1]) continue;
                uint16_t keep = (uint16_t)((1u << d1) | (1u << d2));
                bool changed = false;
                int cells[2], nc = 0;
                for (int k = 0; k < n; k++) {
                    if (!(where[d1] & (1u << k))) continue;
                    int c = G->unit[u][k];
                    if (nc < 2) cells[nc++] = c;
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

/* 1 = singles suffice, 2 = also needs locked candidates / pairs, 3 = more */
static int technique_tier(const sudoku_geom_t *G, const uint8_t givens[81])
{
    static logic_t L;
    logic_load(&L, G, givens);
    int tier = 1;
    while (L.empty && !L.broken) {
        if (singles(&L)) continue;
        if (locked_candidates(&L) || pairs(&L)) {
            tier = 2;
            continue;
        }
        return 3;
    }
    return L.broken ? 3 : tier;
}

/* Easy keeps this many clues or more (the same share of the board for 6x6) */
static int easy_min_clues(const sudoku_geom_t *G)
{
    return G->n == 9 ? 36 : 16;
}

int sudoku_grade(const sudoku_geom_t *G, const uint8_t givens[81])
{
    if (sudoku_count_solutions(G, givens, 2) != 1) return -1;   /* exact, also for Jigsaw */
    int clues = 0;
    for (int k = 0; k < G->ncells; k++) clues += givens[G->cells[k]] != 0;
    switch (technique_tier(G, givens)) {
    case 1: return clues >= easy_min_clues(G) ? GEN_EASY : GEN_MEDIUM;
    case 2: return GEN_HARD;
    default: return GEN_EXPERT;
    }
}

/* ---------------------------------------------------------------- generating */

/* one attempt: clues taken away in random symmetric pairs (a cell and its
 * 180-degree turn) while the solution stays unique; for Easy, stops at a
 * random few clues above the Easy minimum */
static void dig(const sudoku_geom_t *G, const uint8_t full[81], uint8_t out[81], gen_level_t level, uint32_t *rng)
{
    memcpy(out, full, 81);
    const int n = G->n;
    uint8_t order[41];                  /* one cell of each pair (the first half, plus the centre) */
    int no = 0;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k], r = c / 9, col = c % 9;
        int m = (n - 1 - r) * 9 + (n - 1 - col);
        if (c <= m) order[no++] = (uint8_t)c;
    }
    for (int i = no - 1; i > 0; i--) {
        int j = (int)(rnd(rng) % (uint32_t)(i + 1));
        uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
    }
    int clues = G->ncells;
    int stop = level == GEN_EASY ? easy_min_clues(G) + (int)(rnd(rng) % (n == 9 ? 5 : 3)) : 0;
    /* X and 6x6: Medium and Hard keep a clue whose removal would need harder
     * techniques, so most attempts land on the level (digging to the minimum
     * mostly gives Expert there). Classic digs as it always has, so its
     * puzzles for a seed (the daily puzzle) stay the same. */
    int max_tier = 3;
    if (G->variant != VAR_CLASSIC && level == GEN_MEDIUM) max_tier = 1;
    if (G->variant != VAR_CLASSIC && level == GEN_HARD) max_tier = 2;
    /* Jigsaw: a uniqueness check that takes too long keeps the clue (near the
     * minimum, its plain search can take thousands of times longer) */
    const uint32_t node_limit = G->variant == VAR_JIGSAW ? 4000 : 0;
    for (int k = 0; k < no && clues > stop; k++) {
        int a = order[k], b = (n - 1 - a / 9) * 9 + (n - 1 - a % 9);
        uint8_t va = out[a], vb = out[b];
        out[a] = out[b] = 0;
        if (count_bounded(G, out, 2, node_limit) == 1 && (max_tier == 3 || technique_tier(G, out) <= max_tier)) {
            clues -= a == b ? 1 : 2;
        } else {
            out[a] = va;
            out[b] = vb;
        }
    }
}

/* ---------------------------------------------------------------- Jigsaw regions */

/* is region `reg` of the map one connected piece (4-neighbours)? */
static bool region_connected(const uint8_t map[81], int reg)
{
    uint8_t seen[81] = {0}, stack[81];
    int sp = 0, start = -1, total = 0, reached = 0;
    for (int c = 0; c < 81; c++) {
        if (map[c] != reg) continue;
        total++;
        if (start < 0) start = c;
    }
    if (start < 0) return false;
    stack[sp++] = (uint8_t)start;
    seen[start] = 1;
    while (sp) {
        int c = stack[--sp], r = c / 9, k = c % 9;
        reached++;
        const int nb[4] = {k > 0 ? c - 1 : -1, k < 8 ? c + 1 : -1, r > 0 ? c - 9 : -1, r < 8 ? c + 9 : -1};
        for (int i = 0; i < 4; i++) {
            int o = nb[i];
            if (o >= 0 && !seen[o] && map[o] == reg) {
                seen[o] = 1;
                stack[sp++] = (uint8_t)o;
            }
        }
    }
    return reached == total;
}

/* A random Jigsaw map: the 3x3 boxes, reshaped by trading cells across
 * region borders (a cell of A goes to B, a cell of B next to A goes to A)
 * while both regions stay in one piece. */
static void jigsaw_map(uint8_t map[81], uint32_t *rng)
{
    for (int c = 0; c < 81; c++) map[c] = (uint8_t)((c / 27) * 3 + (c % 9) / 3);
    int done = 0;
    for (int tries = 0; tries < 20000 && done < 90; tries++) {
        int a = (int)(rnd(rng) % 81), r = a / 9, k = a % 9;
        int dir = (int)(rnd(rng) % 4);
        int b = dir == 0 ? (k > 0 ? a - 1 : -1) : dir == 1 ? (k < 8 ? a + 1 : -1)
              : dir == 2 ? (r > 0 ? a - 9 : -1) : (r < 8 ? a + 9 : -1);
        if (b < 0 || map[a] == map[b]) continue;
        int A = map[a], B = map[b];
        /* a cell of B (not a) that touches A elsewhere: it goes to A */
        uint8_t cand[81];
        int nc = 0;
        for (int c = 0; c < 81; c++) {
            if (map[c] != B) continue;
            int cr = c / 9, ck = c % 9;
            const int nb[4] = {ck > 0 ? c - 1 : -1, ck < 8 ? c + 1 : -1, cr > 0 ? c - 9 : -1, cr < 8 ? c + 9 : -1};
            for (int i = 0; i < 4; i++) {
                if (nb[i] >= 0 && nb[i] != a && map[nb[i]] == A) {
                    cand[nc++] = (uint8_t)c;
                    break;
                }
            }
        }
        if (!nc) continue;
        int c = cand[rnd(rng) % (uint32_t)nc];
        map[a] = (uint8_t)B;
        map[c] = (uint8_t)A;
        if (region_connected(map, A) && region_connected(map, B)) {
            done++;
        } else {
            map[a] = (uint8_t)A;
            map[c] = (uint8_t)B;
        }
    }
}

/* ---------------------------------------------------------------- generating (all variants) */

bool sudoku_generate(int variant, gen_level_t level, uint32_t seed, uint8_t givens[81], uint8_t regions[81],
                     gen_level_t *got_level)
{
    static sudoku_geom_t *jig;                      /* the Jigsaw board being tried (PSRAM) */
    const bool jigsaw = variant == VAR_JIGSAW;
    if (jigsaw && !jig && !(jig = malloc(sizeof(*jig)))) return false;
    const sudoku_geom_t *G = jigsaw ? jig : sudoku_geom(variant);
    uint32_t rng = seed ? seed : 1;
    int tries = level == GEN_EXPERT ? 400 : (level == GEN_HARD ? 200 : 60);
    if (G->n == 6 && level == GEN_HARD) tries = 2000;   /* rare on 6x6, but an attempt is cheap there */
    uint8_t full[81], cand[81], map[81];
    int best = -1;
    memset(givens, 0, 81);
    for (int t = 0; t < tries; t++) {
        if (jigsaw) {                               /* a new shape every few tries */
            if (t % 4 == 0) {
                jigsaw_map(map, &rng);
                sudoku_geom_jigsaw(map, jig);
            }
            if (!random_full_grid(G, full, &rng, 20000)) {   /* no quick fill: another shape */
                t += 3 - t % 4;
                continue;
            }
        } else {
            random_full_grid(G, full, &rng, 0);
        }
        dig(G, full, cand, level, &rng);
        int g = sudoku_grade(G, cand);
        if (g < 0) continue;
        /* closest to the level asked for, preferring the easier side */
        if (best < 0 || (g <= (int)level && (g > best || best > (int)level)) ||
            (best > (int)level && g < best)) {
            best = g;
            memcpy(givens, cand, 81);
            if (jigsaw && regions) memcpy(regions, G->region, 81);
        }
        if (g == (int)level) break;
    }
    if (got_level) *got_level = best < 0 ? level : (gen_level_t)best;
    return best >= 0;
}

/* ---------------------------------------------------------------- hints */

/* a hidden single (boxes first: the easiest to spot), or -1 */
static int find_hidden_single(const logic_t *L, int *digit, int *unit)
{
    const sudoku_geom_t *G = L->G;
    static const int order[5][2] = {{18, 27}, {0, 9}, {9, 18}, {27, 29}, {UNIT_WINDOW, UNIT_WINDOW + 4}};
    for (int o = 0; o < 5; o++) {
        for (int u = order[o][0]; u < order[o][1]; u++) {
            if (!G->active[u]) continue;
            for (int d = 1; d <= G->n; d++) {
                int where = -1, n = 0;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[u][k];
                    if (L->v[c] == d) { n = -1; break; }
                    if (L->cand[c] & (1u << d)) { where = c; n++; }
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

static int find_naked_single(const logic_t *L, int *digit)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (!L->v[c] && __builtin_popcount(L->cand[c]) == 1) {
            *digit = __builtin_ctz(L->cand[c]);
            return c;
        }
    }
    return -1;
}

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
    logic_load(&L, G, vals);
    if (!L.empty) return false;                          /* solved */
    s_trace = &out->tech;
    for (int round = 0; round < 64 && !L.broken; round++) {
        int d, u, c = find_hidden_single(&L, &d, &u);
        if (c >= 0) {
            out->kind = STEP_HIDDEN_SINGLE;
            out->cell = c;
            out->digit = d;
            out->unit = u;
            s_trace = NULL;
            return true;
        }
        c = find_naked_single(&L, &d);
        if (c >= 0) {
            out->kind = STEP_NAKED_SINGLE;
            out->cell = c;
            out->digit = d;
            s_trace = NULL;
            return true;
        }
        if (!locked_candidates(&L) && !pairs(&L)) break;  /* recorded in out->tech (the first one) */
    }
    s_trace = NULL;
    /* nothing simple: the empty cell with the fewest candidates, from the solution */
    int best = -1, best_n = 10;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c]) continue;
        int n = __builtin_popcount(L.cand[c]);
        if (n < best_n) { best = c; best_n = n; }
    }
    out->kind = STEP_NONE;
    out->cell = best;
    out->digit = best >= 0 ? solution[best] : 0;
    memset(&out->tech, 0, sizeof(out->tech));
    return best >= 0;
}

int sudoku_unit_cell(const sudoku_geom_t *G, int unit, int k)
{
    if (unit < 0 || unit >= UNITS_MAX || !G->active[unit] || k < 0 || k >= G->n) return -1;
    return G->unit[unit][k];
}

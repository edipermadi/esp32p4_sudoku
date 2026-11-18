/* See generator.h. */
#include "generator.h"
#include <string.h>

const char *const GEN_LEVEL_NAMES[GEN_LEVELS] = {"Easy", "Medium", "Hard", "Expert"};

#define EASY_MIN_CLUES 36
#define ALL 0x3FE                       /* candidate bits 1..9 */

/* ---------------------------------------------------------------- tables */

static uint8_t s_units[27][9];          /* rows, columns, boxes */
static uint8_t s_cell_units[81][3];     /* the row, column and box of each cell */
static uint8_t s_peers[81][20];
static bool s_tables;

static void tables(void)
{
    if (s_tables) return;
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 9; j++) {
            s_units[i][j] = (uint8_t)(i * 9 + j);                                   /* row i */
            s_units[9 + i][j] = (uint8_t)(j * 9 + i);                               /* column i */
            s_units[18 + i][j] = (uint8_t)((i / 3 * 3 + j / 3) * 9 + i % 3 * 3 + j % 3);   /* box i */
        }
    }
    for (int c = 0; c < 81; c++) {
        int r = c / 9, k = c % 9;
        s_cell_units[c][0] = (uint8_t)r;
        s_cell_units[c][1] = (uint8_t)(9 + k);
        s_cell_units[c][2] = (uint8_t)(18 + r / 3 * 3 + k / 3);
        int n = 0;
        for (int o = 0; o < 81; o++) {
            if (o == c) continue;
            int orow = o / 9, ok = o % 9;
            if (orow == r || ok == k || (orow / 3 == r / 3 && ok / 3 == k / 3)) s_peers[c][n++] = (uint8_t)o;
        }
    }
    s_tables = true;
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
    uint8_t v[81];
    uint16_t row[9], col[9], box[9];
} grid_t;

#define BOX(i) (((i) / 27) * 3 + ((i) % 9) / 3)

static bool grid_load(grid_t *g, const uint8_t cells[81])
{
    memset(g, 0, sizeof(*g));
    for (int i = 0; i < 81; i++) {
        int d = cells[i];
        if (d < 1 || d > 9) continue;
        uint16_t m = (uint16_t)(1u << d);
        if ((g->row[i / 9] | g->col[i % 9] | g->box[BOX(i)]) & m) return false;
        g->v[i] = (uint8_t)d;
        g->row[i / 9] |= m; g->col[i % 9] |= m; g->box[BOX(i)] |= m;
    }
    return true;
}

/* counts solutions up to limit; with rng, fills g with a random solution */
static int search(grid_t *g, int limit, uint32_t *rng)
{
    int best = -1, best_n = 10;
    uint16_t best_free = 0;
    for (int i = 0; i < 81; i++) {
        if (g->v[i]) continue;
        uint16_t free = (uint16_t)(~(g->row[i / 9] | g->col[i % 9] | g->box[BOX(i)]) & ALL);
        int n = __builtin_popcount(free);
        if (n < best_n) {
            best = i; best_n = n; best_free = free;
            if (n <= 1) break;
        }
    }
    if (best < 0) return 1;
    if (best_n == 0) return 0;
    int r = best / 9, c = best % 9, b = BOX(best), count = 0;
    uint8_t order[9];
    int no = 0;
    for (int d = 1; d <= 9; d++) if (best_free & (1u << d)) order[no++] = (uint8_t)d;
    if (rng) {
        for (int i = no - 1; i > 0; i--) {
            int j = (int)(rnd(rng) % (uint32_t)(i + 1));
            uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
        }
    }
    for (int k = 0; k < no; k++) {
        int d = order[k];
        uint16_t m = (uint16_t)(1u << d);
        g->v[best] = (uint8_t)d;
        g->row[r] |= m; g->col[c] |= m; g->box[b] |= m;
        count += search(g, limit - count, rng);
        if (count >= limit) return count;           /* keeps g filled (the random solution) */
        g->row[r] &= (uint16_t)~m; g->col[c] &= (uint16_t)~m; g->box[b] &= (uint16_t)~m;
    }
    g->v[best] = 0;
    return count;
}

int sudoku_count_solutions(const uint8_t cells[81], int limit)
{
    grid_t g;
    if (!grid_load(&g, cells)) return 0;
    return search(&g, limit, NULL);
}

static void random_full_grid(uint8_t out[81], uint32_t *rng)
{
    grid_t g;
    memset(&g, 0, sizeof(g));
    search(&g, 1, rng);
    memcpy(out, g.v, 81);
}

/* ---------------------------------------------------------------- human-style grading */

typedef struct {
    uint8_t v[81];
    uint16_t cand[81];
    int empty;
    bool broken;
} logic_t;

static void place(logic_t *L, int c, int d)
{
    L->v[c] = (uint8_t)d;
    L->cand[c] = 0;
    L->empty--;
    for (int p = 0; p < 20; p++) {
        int o = s_peers[c][p];
        L->cand[o] &= (uint16_t)~(1u << d);
        if (!L->v[o] && !L->cand[o]) L->broken = true;
    }
}

static bool singles(logic_t *L)
{
    for (int c = 0; c < 81; c++) {                  /* naked single */
        if (!L->v[c] && __builtin_popcount(L->cand[c]) == 1) {
            place(L, c, __builtin_ctz(L->cand[c]));
            return true;
        }
    }
    for (int u = 0; u < 27; u++) {                  /* hidden single */
        for (int d = 1; d <= 9; d++) {
            int where = -1, n = 0;
            for (int k = 0; k < 9; k++) {
                int c = s_units[u][k];
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
 * box within a line) can't be elsewhere in that line (or box) */
static bool locked_candidates(logic_t *L)
{
    bool changed = false;
    for (int b = 18; b < 27; b++) {
        for (int other = 0; other < 18; other++) {
            /* cells shared by box b and line `other` */
            for (int d = 1; d <= 9; d++) {
                uint16_t m = (uint16_t)(1u << d);
                int in_both = 0, box_only = 0, line_only = 0;
                for (int k = 0; k < 9; k++) {
                    int c = s_units[b][k];
                    if (!(L->cand[c] & m)) continue;
                    bool in_line = s_cell_units[c][0] == other || s_cell_units[c][1] == other;
                    if (in_line) in_both++; else box_only++;
                }
                for (int k = 0; k < 9; k++) {
                    int c = s_units[other][k];
                    if (!(L->cand[c] & m)) continue;
                    if (s_cell_units[c][2] != b) line_only++;
                }
                if (!in_both) continue;
                if (!box_only && line_only) {                 /* pointing: clear the rest of the line */
                    for (int k = 0; k < 9; k++) {
                        int c = s_units[other][k];
                        if (s_cell_units[c][2] != b) changed |= eliminate(L, c, m);
                    }
                    if (changed) trace(TECH_POINTING, b, other, d, 0, -1, -1);
                } else if (!line_only && box_only) {          /* claiming: clear the rest of the box */
                    for (int k = 0; k < 9; k++) {
                        int c = s_units[b][k];
                        if (s_cell_units[c][0] != other && s_cell_units[c][1] != other) changed |= eliminate(L, c, m);
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
    for (int u = 0; u < 27; u++) {
        /* naked pair: two cells with the same two candidates */
        for (int a = 0; a < 9; a++) {
            int ca = s_units[u][a];
            if (L->v[ca] || __builtin_popcount(L->cand[ca]) != 2) continue;
            for (int b = a + 1; b < 9; b++) {
                int cb = s_units[u][b];
                if (L->v[cb] || L->cand[cb] != L->cand[ca]) continue;
                bool changed = false;
                for (int k = 0; k < 9; k++) {
                    int c = s_units[u][k];
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
        for (int d = 1; d <= 9; d++) {
            for (int k = 0; k < 9; k++) {
                if (L->cand[s_units[u][k]] & (1u << d)) where[d] |= (uint16_t)(1u << k);
            }
        }
        for (int d1 = 1; d1 <= 9; d1++) {
            if (__builtin_popcount(where[d1]) != 2) continue;
            for (int d2 = d1 + 1; d2 <= 9; d2++) {
                if (where[d2] != where[d1]) continue;
                uint16_t keep = (uint16_t)((1u << d1) | (1u << d2));
                bool changed = false;
                int cells[2], nc = 0;
                for (int k = 0; k < 9; k++) {
                    if (!(where[d1] & (1u << k))) continue;
                    if (nc < 2) cells[nc++] = s_units[u][k];
                    changed |= eliminate(L, s_units[u][k], (uint16_t)(L->cand[s_units[u][k]] & ~keep));
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
static int technique_tier(const uint8_t givens[81])
{
    logic_t L;
    memset(&L, 0, sizeof(L));
    L.empty = 81;
    for (int c = 0; c < 81; c++) L.cand[c] = ALL;
    for (int c = 0; c < 81; c++) {
        if (givens[c]) place(&L, c, givens[c]);
    }
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

int sudoku_grade(const uint8_t givens[81])
{
    tables();
    if (sudoku_count_solutions(givens, 2) != 1) return -1;
    int clues = 0;
    for (int i = 0; i < 81; i++) clues += givens[i] != 0;
    switch (technique_tier(givens)) {
    case 1: return clues >= EASY_MIN_CLUES ? GEN_EASY : GEN_MEDIUM;
    case 2: return GEN_HARD;
    default: return GEN_EXPERT;
    }
}

/* ---------------------------------------------------------------- generating */

/* one attempt: clues taken away in random symmetric pairs while the solution
 * stays unique; for Easy, stops at a random 36..40 clues */
static void dig(const uint8_t full[81], uint8_t out[81], gen_level_t level, uint32_t *rng)
{
    memcpy(out, full, 81);
    uint8_t order[41];
    for (int i = 0; i <= 40; i++) order[i] = (uint8_t)i;   /* cell i and 80 - i (40 = centre) */
    for (int i = 40; i > 0; i--) {
        int j = (int)(rnd(rng) % (uint32_t)(i + 1));
        uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
    }
    int clues = 81;
    int stop = level == GEN_EASY ? EASY_MIN_CLUES + (int)(rnd(rng) % 5) : 0;
    for (int k = 0; k <= 40 && clues > stop; k++) {
        int a = order[k], b = 80 - a;
        uint8_t va = out[a], vb = out[b];
        out[a] = out[b] = 0;
        if (sudoku_count_solutions(out, 2) == 1) {
            clues -= a == b ? 1 : 2;
        } else {
            out[a] = va;
            out[b] = vb;
        }
    }
}

bool sudoku_generate(gen_level_t level, uint32_t seed, uint8_t givens[81], gen_level_t *got_level)
{
    tables();
    uint32_t rng = seed ? seed : 1;
    const int tries = level == GEN_EXPERT ? 400 : (level == GEN_HARD ? 200 : 60);
    uint8_t full[81], cand[81];
    int best = -1;
    for (int t = 0; t < tries; t++) {
        random_full_grid(full, &rng);
        dig(full, cand, level, &rng);
        int g = sudoku_grade(cand);
        if (g < 0) continue;
        /* closest to the level asked for, preferring the easier side */
        if (best < 0 || (g <= (int)level && (g > best || best > (int)level)) ||
            (best > (int)level && g < best)) {
            best = g;
            memcpy(givens, cand, 81);
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
    static const int order[3][2] = {{18, 27}, {0, 9}, {9, 18}};
    for (int o = 0; o < 3; o++) {
        for (int u = order[o][0]; u < order[o][1]; u++) {
            for (int d = 1; d <= 9; d++) {
                int where = -1, n = 0;
                for (int k = 0; k < 9; k++) {
                    int c = s_units[u][k];
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
    for (int c = 0; c < 81; c++) {
        if (!L->v[c] && __builtin_popcount(L->cand[c]) == 1) {
            *digit = __builtin_ctz(L->cand[c]);
            return c;
        }
    }
    return -1;
}

bool sudoku_next_step(const uint8_t vals[81], const uint8_t solution[81], sudoku_step_t *out)
{
    tables();
    memset(out, 0, sizeof(*out));
    out->cell = -1;
    out->unit = -1;
    for (int c = 0; c < 81; c++) {                       /* a wrong digit first */
        if (vals[c] && vals[c] != solution[c]) {
            out->kind = STEP_WRONG;
            out->cell = c;
            out->digit = vals[c];
            return true;
        }
    }
    logic_t L;
    memset(&L, 0, sizeof(L));
    L.empty = 81;
    for (int c = 0; c < 81; c++) L.cand[c] = ALL;
    for (int c = 0; c < 81; c++) {
        if (vals[c]) place(&L, c, vals[c]);
    }
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
    for (int c = 0; c < 81; c++) {
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

int sudoku_unit_cell(int unit, int k)
{
    tables();
    return s_units[unit][k];
}

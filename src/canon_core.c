/**
 * @file canon_core.c
 * @ingroup canon_core
 * @brief A puzzle's canonical hash (canon_core.h).
 */
#include "canon_core.h"
#include "generator.h"
#include <string.h>

/**
 * @brief Is it a line variant (its layout: lines of cells, kept in thermos)?
 * @param[in] variant  sudoku_variant_t
 * @return true if so
 */
static bool is_line_variant(int variant)
{
    switch (variant) {
    case VAR_THERMO:
    case VAR_ARROW:
    case VAR_RENBAN:
    case VAR_WHISPER:
    case VAR_PALINDROME:
    case VAR_WPAL:
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
    case VAR_SEQ:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Does a line's way matter (a bulb, a circle or a pill at its start)? The others read the same backwards.
 * @param[in] variant  sudoku_variant_t
 * @return true if so
 */
static bool is_line_one_way(int variant)
{
    return variant == VAR_THERMO || variant == VAR_SLOW || variant == VAR_ARROW || variant == VAR_PILL ||
           variant == VAR_AVG;
}

/**
 * @brief Is it a variant of marks on cells or between them (kept in marks)?
 * @param[in] variant  sudoku_variant_t
 * @return true if so
 */
static bool is_mark_variant(int variant)
{
    return variant == VAR_KROPKI || variant == VAR_XV || variant == VAR_GT || variant == VAR_CONSEC ||
           variant == VAR_EVENODD || variant == VAR_FORTRESS || variant == VAR_DIAGCONSEC;
}

/**
 * @brief Is it a variant of clues outside the grid (Sandwich, the left and top clues, Little Killer's diagonals)?
 * @param[in] variant  sudoku_variant_t
 * @return true if so
 */
static bool is_clue_variant(int variant)
{
    switch (variant) {
    case VAR_SANDWICH:
    case VAR_SKY:
    case VAR_XSUM:
    case VAR_FRAME:
    case VAR_OUTSIDE:
    case VAR_ROSSINI:
    case VAR_NROOMS:
    case VAR_SKYSUM:
    case VAR_PSKY:
    case VAR_B9:
    case VAR_N9:
    case VAR_BF:
    case VAR_LK:
        return true;
    default:
        return false;
    }
}

/* (documented in canon_core.h) */
uint8_t canon_symmetries(int variant)
{
    if (is_clue_variant(variant) && variant != VAR_SANDWICH && variant != VAR_N9 && variant != VAR_BF) {
        return 0x41;                    /* the clues stay left of the rows and above the columns, read the same way:
                                         * as it is, or mirrored about the main diagonal (rows' clues and columns' swap) */
    }
    if (is_mark_variant(variant) || variant == VAR_SANDWICH || variant == VAR_N9 || variant == VAR_BF || variant == VAR_QUAD ||
            variant == VAR_MATHRAX || variant == VAR_CLONE || variant == VAR_S9 || variant == VAR_BATTEN) {
        return 0xFF;                    /* (marks, circles, clone areas, arrows turned too; a Sandwich sum, the digits
                                         * beside a 9, a Battlefield sum are the same from either end) */
    }
    if (variant == VAR_PILL) {
        return 0x41;                    /* its pill reads left to right or top to bottom: as it is, or about the main
                                         * diagonal (a pill across becomes one down, its tens still first) */
    }
    if (is_line_variant(variant)) {
        return 0xFF;                    /* (its lines turned too: canon_hash_geom()) */
    }
    switch (variant) {                  /* (tests/test_main.c checks these against each board's peers) */
    case VAR_MINI:
    case VAR_MINI8:
        return 0x35;                    /* 2x3 (2x4) boxes: no quarter turns, no diagonal mirrors */
    case VAR_FLATMATES:
        return 0x35;                    /* above and below stay in their column: no quarter turns, no diagonal mirrors */
    case VAR_159:
        return 0x35;                    /* columns stay columns (columns 1, 5, 9 and the column numbers in them) */
    case VAR_JIGSAW:                    /* (its regions turned too: canon_hash_geom()) */
    case VAR_JIG7:
    case VAR_JIG6:
    case VAR_JIG8:
    case VAR_KILLER:                    /* (its cages turned too; and these two's) */
    case VAR_SUMLESS:
    case VAR_REGIONS:
    case VAR_PRODUCT:
    case VAR_CALC:
    case VAR_KROPKI:                    /* (its dots turned too) */
    case VAR_XV:                        /* (its marks turned too) */
        return 0xFF;
    case VAR_CLASSIC:
    case VAR_X:
    case VAR_KIDS:
    case VAR_HYPER:
    case VAR_KNIGHT:
    case VAR_KING:
    case VAR_DISJOINT:
    case VAR_ASTERISK:
    case VAR_CENTERDOT:
    case VAR_ARGYLE:
    case VAR_GIRANDOLA:
    case VAR_CHESS:
    case VAR_ANTIDIAG:
    case VAR_PSYM:                      /* (mirrored through the centre stays mirrored through it) */
    case VAR_NONCONSEC:
    case VAR_DIAGNC:
    case VAR_MAGIC:                     /* (the centre box turns onto itself) */
    case VAR_QUADRO:                    /* (2x2 blocks onto 2x2 blocks) */
    case VAR_AQ9:                       /* (diagonals onto diagonals) */
        return 0xFF;
    default:
        return 0;                       /* (digits that matter, or a layout of its own) */
    }
}

/* (documented in canon_core.h) */
int canon_digits(int variant)
{
    switch (variant) {
    case VAR_PSYM:
        return CANON_PAIRS;
    case VAR_NONCONSEC:
    case VAR_DIAGNC:
    case VAR_MAGIC:
        return CANON_FLIP;
    case VAR_FLATMATES:
        return CANON_FLATMATES;
    case VAR_159:
        return CANON_159;
    case VAR_QUADRO:
        return CANON_PARITY;
    case VAR_AQ9:
        return CANON_FIX9;
    default:
        return CANON_ANY;
    }
}

/* (documented in canon_core.h) */
int canon_cell(int t, int n, int cell)
{
    const int r = cell / 9, c = cell % 9, m = n - 1;
    static const int8_t RR[8][3] = {         /* the new row and column, each as (row, column, m) weights */
        {1, 0, 0}, {0, 1, 0}, {-1, 0, 1}, {0, -1, 1}, {1, 0, 0}, {-1, 0, 1}, {0, 1, 0}, {0, -1, 1}
    };
    static const int8_t CC[8][3] = {
        {0, 1, 0}, {-1, 0, 1}, {0, -1, 1}, {1, 0, 0}, {0, -1, 1}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 1}
    };
    const int nr = RR[t][0] * r + RR[t][1] * c + RR[t][2] * m;
    const int nc = CC[t][0] * r + CC[t][1] * c + CC[t][2] * m;
    return nr * 9 + nc;
}

/* (documented in canon_core.h) */
bool is_canon_layout(int variant)
{
    return is_jigsaw_variant(variant) || variant == VAR_KILLER || variant == VAR_SUMLESS || variant == VAR_REGIONS ||
           variant == VAR_PRODUCT || variant == VAR_CALC || is_mark_variant(variant) || is_line_variant(variant) || is_clue_variant(variant) ||
           variant == VAR_QUAD || variant == VAR_MATHRAX || variant == VAR_CLONE || variant == VAR_S9 ||
           variant == VAR_BATTEN;
}

/**
 * @brief Kropki, XV: the mark between two side-by-side cells (either order).
 * @param[in] G  the board
 * @param[in] a  a cell
 * @param[in] b  the cell beside, above or below it
 * @return MARK_* (0: none)
 */
static int edge_mark(const sudoku_geom_t *G, int a, int b)
{
    const int lo = a < b ? a : b, hi = a < b ? b : a;
    if (G->variant == VAR_CONSEC) {                 /* a bar, or none */
        return (G->consec[lo] & (hi - lo == 1 ? CONSEC_RIGHT : CONSEC_DOWN)) != 0;
    }
    if (G->edge_kind == EDGE_FORT) {
        return 0;                                   /* (Fortress: its grey cells say it all) */
    }
    const int m = hi - lo == 1 ? EDGE_RIGHT(G->edge[lo]) : EDGE_DOWN(G->edge[lo]);
    return G->edge_kind == EDGE_GT && m && a != lo ? 3 - m : m;   /* (a sign seen from the other cell: turned round) */
}

/**
 * @brief Diagonal Consecutive: is there a mark between two cells touching at a corner?
 * @param[in] G  the board
 * @param[in] a  a cell
 * @param[in] b  a cell touching it at a corner
 * @return 1 if so, else 0
 */
static int diag_mark(const sudoku_geom_t *G, int a, int b)
{
    const int lo = a < b ? a : b, hi = a < b ? b : a;
    return (G->dmark[lo] & (hi - lo == 10 ? DMARK_DR : DMARK_DL)) != 0;
}

/**
 * @brief An outside clue (or Sandwich's sum) of a row or column, as a number.
 * @param[in] G  the board
 * @param[in] lu  0..8 a row, 9..17 a column
 * @return it (0: no clue)
 */
static unsigned clue_of(const sudoku_geom_t *G, int lu)
{
    switch (G->variant) {
    case VAR_SANDWICH:
        return G->sandwich_sum[lu];
    case VAR_SKY:
        return G->sky[lu];
    case VAR_XSUM:
        return G->xsum[lu];
    case VAR_FRAME:
        return G->frame_sum[lu];
    case VAR_OUTSIDE:
        return G->outside_set[lu];
    case VAR_ROSSINI:
        return G->rossini_dir[lu];
    case VAR_NROOMS:
        return G->nroom[lu];
    case VAR_SKYSUM:
        return G->sky_sum[lu];
    case VAR_PSKY:
        return G->psky_clue[lu];
    case VAR_N9:
        return G->n9_set[lu];
    case VAR_BF:
        return G->bf_clue[lu];
    default:
        return G->b9_sum[lu];
    }
}

/**
 * @brief Hashes a copy (FNV-1a: the variant, then its bytes).
 * @param[in] variant  sudoku_variant_t
 * @param[in] best  the copy
 * @param[in] len  its bytes
 * @return the hash, never 0
 */
static uint64_t hash_copy(int variant, const uint8_t *best, size_t len)
{
    uint64_t h = 1469598103934665603ULL;
    h = (h ^ (uint8_t)variant) * 1099511628211ULL;
    for (size_t k = 0; k < len; k++) {
        h = (h ^ best[k]) * 1099511628211ULL;
    }
    return h ? h : 1;
}

/**
 * @brief Sorts n entries of w bytes (insertion: a few dozen at most).
 * @param[in,out] e  the entries
 * @param[in] n  how many
 * @param[in] w  each one's bytes (12 at most)
 */
static void sort_entries(uint8_t *e, int n, int w)
{
    for (int a = 1; a < n; a++) {
        for (int b = a; b > 0 && memcmp(e + b * w, e + (b - 1) * w, (size_t)w) < 0; b--) {
            uint8_t x[12];
            memcpy(x, e + b * w, (size_t)w);
            memcpy(e + b * w, e + (b - 1) * w, (size_t)w);
            memcpy(e + (b - 1) * w, x, (size_t)w);
        }
    }
}

/* (documented in canon_core.h) */
uint64_t canon_hash_geom(const sudoku_geom_t *G, const uint8_t givens[81])
{
    const int variant = G->variant;
    if (!is_canon_layout(variant)) {
        return canon_hash(variant, givens);
    }
    const uint8_t sym = canon_symmetries(variant);   /* (Pill Arrows: two of the eight) */
    bool have = false;
    if (is_line_variant(variant)) {
        /* a copy: the givens as they are (thermometers, sums need them), then the lines, each as its length and its
         * cells on the turned board (from its start where its way matters; else whichever way is smaller), the
         * lines sorted (the order they're kept in doesn't matter) */
        const bool one_way = is_line_one_way(variant);
        const int nl = G->nthermos < THERMOS_MAX ? G->nthermos : THERMOS_MAX;
        uint8_t best[81 + THERMOS_MAX * 10], cur[81 + THERMOS_MAX * 10], inv[81], ln[THERMOS_MAX][10];
        const size_t len = 81 + (size_t)nl * 10;
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k);
                inv[src] = (uint8_t)k;          /* where a cell goes */
                cur[k] = givens[src];
            }
            for (int l = 0; l < nl; l++) {
                const int n = G->thermo_n[l];
                uint8_t fwd[9], rev[9];
                for (int j = 0; j < n; j++) {
                    fwd[j] = inv[G->thermo_cell[l][j]];
                    rev[n - 1 - j] = fwd[j];
                }
                memset(ln[l], 0, 10);
                ln[l][0] = (uint8_t)n;
                memcpy(ln[l] + 1, !one_way && memcmp(rev, fwd, (size_t)n) < 0 ? rev : fwd, (size_t)n);
            }
            for (int a = 1; a < nl; a++) {          /* sorted (insertion: 16 lines at most) */
                for (int b = a; b > 0 && memcmp(ln[b], ln[b - 1], 10) < 0; b--) {
                    uint8_t x[10];
                    memcpy(x, ln[b], 10);
                    memcpy(ln[b], ln[b - 1], 10);
                    memcpy(ln[b - 1], x, 10);
                }
            }
            memcpy(cur + 81, ln, (size_t)nl * 10);
            if (!have || memcmp(cur, best, len) < 0) {
                memcpy(best, cur, len);
                have = true;
            }
        }
        uint64_t h = 1469598103934665603ULL;
        h = (h ^ (uint8_t)variant) * 1099511628211ULL;
        for (size_t k = 0; k < len; k++) {
            h = (h ^ best[k]) * 1099511628211ULL;
        }
        return h ? h : 1;
    }
    if (is_mark_variant(variant)) {
        /* a copy: the givens as they are (the marks' meanings need them), then each cell's mark to its right and
         * below, as the turned board has them (Greater-Than's signs seen from the turned board's left or top cell);
         * then, but for Kropki and XV (whose copies were only that much), each cell's own mark (Even-Odd, Fortress)
         * and its corner marks down-right and down-left (Diagonal Consecutive) */
        uint8_t best[81 * 6], cur[81 * 6];
        const size_t len = variant == VAR_KROPKI || variant == VAR_XV ? 81 * 3 : 81 * 6;
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k);
                cur[k] = givens[src];
                cur[81 + k] = (uint8_t)(k % 9 < 8 ? edge_mark(G, src, canon_cell(t, 9, k + 1)) : 0);
                cur[162 + k] = (uint8_t)(k < 72 ? edge_mark(G, src, canon_cell(t, 9, k + 9)) : 0);
                cur[243 + k] = G->parity[src];
                cur[324 + k] = (uint8_t)(k % 9 < 8 && k < 72 ? diag_mark(G, src, canon_cell(t, 9, k + 10)) : 0);
                cur[405 + k] = (uint8_t)(k % 9 > 0 && k < 72 ? diag_mark(G, src, canon_cell(t, 9, k + 8)) : 0);
            }
            if (!have || memcmp(cur, best, len) < 0) {
                memcpy(best, cur, len);
                have = true;
            }
        }
        return hash_copy(variant, best, len);
    }
    if (variant == VAR_CLONE) {
        /* a copy: the givens renumbered in reading order (a cell and its clone the same, whatever digit), then each
         * cell's pair, numbered in reading order from 1 (0: in no area); the two areas' shapes say which cell clones
         * which */
        uint8_t best[162], cur[162];
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            uint8_t name[10] = {0}, lab[CLONE_MAX] = {0};
            int next = 1, nlab = 0;
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k), d = givens[src], p = G->clone_pair[src];
                if (d && !name[d]) {
                    name[d] = (uint8_t)next++;
                }
                cur[k] = d ? name[d] : 0;
                if (p != CLONE_NONE && !lab[p]) {
                    lab[p] = (uint8_t)++nlab;
                }
                cur[81 + k] = p == CLONE_NONE ? 0 : lab[p];
            }
            if (!have || memcmp(cur, best, sizeof(cur)) < 0) {
                memcpy(best, cur, sizeof(cur));
                have = true;
            }
        }
        return hash_copy(variant, best, sizeof(best));
    }
    if (variant == VAR_BATTEN) {
        /* a copy: the givens as they are (odd and even matter), then each corner's mark on the turned board (the
         * block of four cells turned onto it: its top-left source cell's mark) */
        uint8_t best[162], cur[162];
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k);
                cur[k] = givens[src];
                cur[81 + k] = 0;
                if (k % 9 < 8 && k < 72) {
                    int top = 80;
                    const int four[4] = {k, k + 1, k + 9, k + 10};
                    for (int j = 0; j < 4; j++) {
                        const int d = canon_cell(t, 9, four[j]);
                        top = d < top ? d : top;
                    }
                    cur[81 + k] = G->bat[top];
                }
            }
            if (!have || memcmp(cur, best, sizeof(cur)) < 0) {
                memcpy(best, cur, sizeof(cur));
                have = true;
            }
        }
        return hash_copy(variant, best, sizeof(best));
    }
    if (variant == VAR_S9) {
        /* a copy: the givens as they are (an arrow's digit is a distance, the 9 is the 9), then each cell's arrow
         * turned with the board (the way to the turned cell one step along it) */
        uint8_t best[162], cur[162];
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k), dir = G->s9_dir[src], to = sudoku_s9_cell(src, dir, 1);
                cur[k] = givens[src];
                cur[81 + k] = S9_NONE;
                for (int way = S9_UP; to >= 0 && way <= S9_LEFT; way++) {
                    const int n = sudoku_s9_cell(k, way, 1);
                    if (n >= 0 && canon_cell(t, 9, n) == to) {
                        cur[81 + k] = (uint8_t)way;
                    }
                }
            }
            if (!have || memcmp(cur, best, sizeof(cur)) < 0) {
                memcpy(best, cur, sizeof(cur));
                have = true;
            }
        }
        return hash_copy(variant, best, sizeof(best));
    }
    if (variant == VAR_QUAD || variant == VAR_MATHRAX) {
        /* a copy: the givens as they are, then the circles, each as the top-left cell of its four on the turned board
         * and its clue (Quadruple: its digits; Mathrax: its operation and number), sorted */
        const bool quad = variant == VAR_QUAD;
        const int nc = quad ? G->nquads : G->nmx;
        uint8_t best[81 + QUADS_MAX * 6], cur[81 + QUADS_MAX * 6], inv[81];
        const size_t len = 81 + (size_t)nc * 6;
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k);
                inv[src] = (uint8_t)k;
                cur[k] = givens[src];
            }
            uint8_t *e = cur + 81;
            for (int q = 0; q < nc; q++) {
                const int at = quad ? G->quad_at[q] : G->mx_at[q];
                const uint8_t four[4] = {inv[at], inv[at + 1], inv[at + 9], inv[at + 10]};
                uint8_t top = four[0];
                for (int j = 1; j < 4; j++) {
                    top = four[j] < top ? four[j] : top;   /* (the turned block's top-left) */
                }
                memset(e + q * 6, 0, 6);
                e[q * 6] = top;
                if (quad) {
                    e[q * 6 + 1] = G->quad_n[q];
                    memcpy(e + q * 6 + 2, G->quad_dig[q], 4);
                } else {
                    e[q * 6 + 1] = G->mx_op[q];   /* (its diagonal pairs stay diagonal pairs) */
                    e[q * 6 + 2] = G->mx_val[q];
                }
            }
            sort_entries(e, nc, 6);
            if (!have || memcmp(cur, best, len) < 0) {
                memcpy(best, cur, len);
                have = true;
            }
        }
        return hash_copy(variant, best, len);
    }
    if (variant == VAR_LK) {
        /* a copy: the givens as they are, then the diagonals, each as its sum and its cells on the turned board,
         * sorted (the side its clue is on follows from its cells) */
        uint8_t best[81 + LK_MAX * 11], cur[81 + LK_MAX * 11], inv[81];
        const int nd = G->nlk < LK_MAX ? G->nlk : LK_MAX;
        const size_t len = 81 + (size_t)nd * 11;
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k);
                inv[src] = (uint8_t)k;
                cur[k] = givens[src];
            }
            uint8_t *e = cur + 81;
            for (int d = 0; d < nd; d++) {
                memset(e + d * 11, 0xFF, 11);
                e[d * 11] = G->lk_sum[d];
                e[d * 11 + 1] = G->lk_n[d];
                for (int j = 0; j < G->lk_n[d]; j++) {
                    e[d * 11 + 2 + j] = inv[G->lk_cell[d][j]];
                }
                sort_entries(e + d * 11 + 2, G->lk_n[d], 1);   /* (its cells as a set) */
            }
            sort_entries(e, nd, 11);
            if (!have || memcmp(cur, best, len) < 0) {
                memcpy(best, cur, len);
                have = true;
            }
        }
        return hash_copy(variant, best, len);
    }
    if (is_clue_variant(variant)) {
        /* a copy: the givens as they are, then each row's and column's clue (two bytes: Outside Sudoku's are digit
         * sets) as the turned board has them: the line a turned row or column was, its clue with it */
        uint8_t best[81 + 36], cur[81 + 36];
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                cur[k] = givens[canon_cell(t, 9, k)];
            }
            for (int lu = 0; lu < 18; lu++) {
                const int c0 = lu < 9 ? lu * 9 : lu - 9, c1 = lu < 9 ? c0 + 1 : c0 + 9;
                const int s0 = canon_cell(t, 9, c0), s1 = canon_cell(t, 9, c1);
                const unsigned c = clue_of(G, s0 / 9 == s1 / 9 ? s0 / 9 : 9 + s0 % 9);
                cur[81 + 2 * lu] = (uint8_t)(c >> 8);
                cur[81 + 2 * lu + 1] = (uint8_t)c;
            }
            if (!have || memcmp(cur, best, sizeof(cur)) < 0) {
                memcpy(best, cur, sizeof(cur));
                have = true;
            }
        }
        return hash_copy(variant, best, sizeof(best));
    }
    const bool prod = variant == VAR_PRODUCT, calc = variant == VAR_CALC;
    const bool killer = variant == VAR_KILLER || prod || calc;   /* (sums, products: their digits as they are) */
    const bool cages = !is_jigsaw_variant(variant);   /* Killer, Sumless, Product, Extra Regions: cages (CAGE_NONE: none) */
    const int n = G->n;                             /* (Jigsaw 7x7: turned as a 7x7, its cells at the top left) */
    /* a copy: the givens (renumbered in reading order; Killer's and Product Killer's as they are), each cell's region
     * or cage (numbered in reading order from 1; 0: in no cage), then the sums (a byte each) or products (two) in that
     * order (Calcudoku Killer: each cage's operation, then its number in two bytes) */
    uint8_t best[81 * 2 + 3 * CAGES_MAX], cur[81 * 2 + 3 * CAGES_MAX];
    const int len = 81 * 2 + (calc ? 3 * G->ncages : prod ? 2 * G->ncages : killer ? G->ncages : 0);
    for (int t = 0; t < 8; t++) {
        if (!(sym >> t & 1)) {
            continue;
        }
        uint8_t name[10] = {0}, lab[CAGES_MAX];
        int next = 1, nlab = 0;
        memset(lab, 0, sizeof(lab));
        for (int k = 0; k < 81; k++) {      /* reading order of the turned board */
            if (k / 9 >= n || k % 9 >= n) {
                cur[k] = cur[81 + k] = 0;   /* (off a smaller board) */
                continue;
            }
            const int src = canon_cell(t, n, k), d = givens[src];
            if (d && !killer && !name[d]) {
                name[d] = (uint8_t)next++;
            }
            cur[k] = d ? (killer ? (uint8_t)d : name[d]) : 0;
            const int r = cages ? G->cage_of[src] : G->region[src];
            if (cages && r == CAGE_NONE) {
                cur[81 + k] = 0;
            } else {
                if (!lab[r]) {
                    lab[r] = (uint8_t)++nlab;
                }
                cur[81 + k] = lab[r];
            }
        }
        for (int c = 0; killer && c < G->ncages; c++) {   /* each cage's sum or product, by its number in this copy */
            if (lab[c] && calc) {
                const int op = G->cage_op[c];
                const int num = op == MX_PROD ? G->cage_prod[c] : op == MX_SUM ? G->cage_sum[c] : G->cage_dq[c];
                cur[162 + 3 * (lab[c] - 1)] = (uint8_t)op;
                cur[162 + 3 * (lab[c] - 1) + 1] = (uint8_t)(num >> 8);
                cur[162 + 3 * (lab[c] - 1) + 2] = (uint8_t)num;
            } else if (lab[c] && prod) {
                cur[162 + 2 * (lab[c] - 1)] = (uint8_t)(G->cage_prod[c] >> 8);   /* (up to 60480: two bytes) */
                cur[162 + 2 * (lab[c] - 1) + 1] = (uint8_t)G->cage_prod[c];
            } else if (lab[c]) {
                cur[162 + lab[c] - 1] = G->cage_sum[c];
            }
        }
        if (!have || memcmp(cur, best, (size_t)len) < 0) {
            memcpy(best, cur, (size_t)len);
            have = true;
        }
    }
    uint64_t h = 1469598103934665603ULL;    /* FNV-1a: the variant, then the smallest copy */
    h = (h ^ (uint8_t)variant) * 1099511628211ULL;
    for (int k = 0; k < len; k++) {
        h = (h ^ best[k]) * 1099511628211ULL;
    }
    return h ? h : 1;
}

/* (documented in canon_core.h) */
uint64_t canon_hash(int variant, const uint8_t givens[81])
{
    const uint8_t sym = canon_symmetries(variant);
    if (!sym || is_canon_layout(variant)) {
        return 0;                           /* (Jigsaw, Killer: canon_hash_geom(), with their layout) */
    }
    const int n = variant == VAR_MINI ? 6 : variant == VAR_MINI8 ? 8 : variant == VAR_KIDS ? 4 : 9;
    const int mode = canon_digits(variant);
    uint8_t best[81], cur[81];
    bool have = false;
    for (int t = 0; t < 8; t++) {
        if (!(sym >> t & 1)) {
            continue;
        }
        for (int flip = 0; flip < (mode == CANON_FLIP ? 2 : 1); flip++) {   /* (Flip: as it is, and 10 - d) */
            uint8_t name[10] = {0};         /* the renumbering: name[d] = what digit d becomes */
            int next = 1, next_odd = 1, next_even = 2;
            static const uint8_t FREE[] = {2, 3, 4, 6, 7, 8};   /* Flatmates: the digits renumbered, in order */
            const bool upside = t == 2 || t == 5;   /* (the half turn, the top-bottom mirror) */
            const bool sideways = t == 2 || t == 4; /* (the half turn, the left-right mirror: column c to 10 - c) */
            for (int k = 0; k < n * n; k++) {   /* reading order of the turned board */
                const int d = givens[canon_cell(t, n, k / n * 9 + k % n)];
                if (!d) {
                    cur[k] = 0;
                } else if (mode == CANON_FLIP) {
                    cur[k] = (uint8_t)(flip ? 10 - d : d);
                } else if (mode == CANON_159) {
                    cur[k] = (uint8_t)(sideways ? 10 - d : d);
                } else if (mode == CANON_PAIRS && d == 5) {
                    cur[k] = 5;                 /* (its own pair) */
                } else if (mode == CANON_FLATMATES && (d == 1 || d == 5 || d == 9)) {
                    cur[k] = (uint8_t)(upside && d != 5 ? 10 - d : d);   /* (upside down: a 1 above is a 9 below) */
                } else if (mode == CANON_FLATMATES) {
                    if (!name[d]) {
                        name[d] = FREE[next++ - 1];
                    }
                    cur[k] = name[d];
                } else if (mode == CANON_PARITY) {   /* odd ones 1, 3, 5 ...; even ones 2, 4 ... */
                    if (!name[d]) {
                        name[d] = (uint8_t)(d & 1 ? next_odd : next_even);
                        if (d & 1) {
                            next_odd += 2;
                        } else {
                            next_even += 2;
                        }
                    }
                    cur[k] = name[d];
                } else if (mode == CANON_FIX9 && d == 9) {
                    cur[k] = 9;
                } else {
                    if (!name[d]) {             /* Pairs: d's pair takes the next one, and 10 - d its partner */
                        name[d] = (uint8_t)next;
                        if (mode == CANON_PAIRS) {
                            name[10 - d] = (uint8_t)(10 - next);
                        }
                        next++;
                    }
                    cur[k] = name[d];
                }
            }
            if (!have || memcmp(cur, best, (size_t)(n * n)) < 0) {
                memcpy(best, cur, (size_t)(n * n));
                have = true;
            }
        }
    }
    uint64_t h = 1469598103934665603ULL;    /* FNV-1a: the variant, then the smallest copy */
    h = (h ^ (uint8_t)variant) * 1099511628211ULL;
    for (int k = 0; k < n * n; k++) {
        h = (h ^ best[k]) * 1099511628211ULL;
    }
    return h ? h : 1;
}

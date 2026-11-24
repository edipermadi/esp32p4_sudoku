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

/* (documented in canon_core.h) */
uint8_t canon_symmetries(int variant)
{
    if (variant == VAR_PILL) {
        return 0x41;                    /* its pill reads left to right or top to bottom: as it is, or about the main
                                         * diagonal (a pill across becomes one down, its tens still first) */
    }
    if (is_line_variant(variant)) {
        return 0xFF;                    /* (its lines turned too: canon_hash_geom()) */
    }
    switch (variant) {                  /* (tests/test_main.c checks these against each board's peers) */
    case VAR_MINI:
        return 0x35;                    /* 2x3 boxes: no quarter turns, no diagonal mirrors */
    case VAR_FLATMATES:
        return 0x35;                    /* above and below stay in their column: no quarter turns, no diagonal mirrors */
    case VAR_159:
        return 0x35;                    /* columns stay columns (columns 1, 5, 9 and the column numbers in them) */
    case VAR_JIGSAW:                    /* (its regions turned too: canon_hash_geom()) */
    case VAR_KILLER:                    /* (its cages turned too; and these two's) */
    case VAR_SUMLESS:
    case VAR_REGIONS:
    case VAR_PRODUCT:
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
    return variant == VAR_JIGSAW || variant == VAR_KILLER || variant == VAR_SUMLESS || variant == VAR_REGIONS ||
           variant == VAR_PRODUCT || variant == VAR_KROPKI || variant == VAR_XV || is_line_variant(variant);
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
    return hi - lo == 1 ? EDGE_RIGHT(G->edge[lo]) : EDGE_DOWN(G->edge[lo]);
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
    if (variant == VAR_KROPKI || variant == VAR_XV) {
        /* a copy: the givens as they are (a black dot's doubling, a V's 5 need them), then each cell's mark to its
         * right and below, as the turned board has them (a mark joins two cells either way round) */
        uint8_t best[81 * 3], cur[81 * 3];
        for (int t = 0; t < 8; t++) {
            if (!(sym >> t & 1)) {
                continue;
            }
            for (int k = 0; k < 81; k++) {
                const int src = canon_cell(t, 9, k);
                cur[k] = givens[src];
                cur[81 + k] = (uint8_t)(k % 9 < 8 ? edge_mark(G, src, canon_cell(t, 9, k + 1)) : 0);
                cur[162 + k] = (uint8_t)(k < 72 ? edge_mark(G, src, canon_cell(t, 9, k + 9)) : 0);
            }
            if (!have || memcmp(cur, best, sizeof(cur)) < 0) {
                memcpy(best, cur, sizeof(cur));
                have = true;
            }
        }
        uint64_t h = 1469598103934665603ULL;
        h = (h ^ (uint8_t)variant) * 1099511628211ULL;
        for (size_t k = 0; k < sizeof(best); k++) {
            h = (h ^ best[k]) * 1099511628211ULL;
        }
        return h ? h : 1;
    }
    const bool prod = variant == VAR_PRODUCT;
    const bool killer = variant == VAR_KILLER || prod;   /* (sums, products: their digits as they are) */
    const bool cages = variant != VAR_JIGSAW;       /* Killer, Sumless, Product, Extra Regions: cages (CAGE_NONE: none) */
    /* a copy: the givens (renumbered in reading order; Killer's and Product Killer's as they are), each cell's region
     * or cage (numbered in reading order from 1; 0: in no cage), then the sums (a byte each) or products (two) in that
     * order */
    uint8_t best[81 * 2 + 2 * CAGES_MAX], cur[81 * 2 + 2 * CAGES_MAX];
    const int len = 81 * 2 + (prod ? 2 * G->ncages : killer ? G->ncages : 0);
    for (int t = 0; t < 8; t++) {
        if (!(sym >> t & 1)) {
            continue;
        }
        uint8_t name[10] = {0}, lab[CAGES_MAX];
        int next = 1, nlab = 0;
        memset(lab, 0, sizeof(lab));
        for (int k = 0; k < 81; k++) {      /* reading order of the turned board */
            const int src = canon_cell(t, 9, k), d = givens[src];
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
            if (lab[c] && prod) {
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
    const int n = variant == VAR_MINI ? 6 : variant == VAR_KIDS ? 4 : 9;
    const int mode = canon_digits(variant);
    uint8_t best[81], cur[81];
    bool have = false;
    for (int t = 0; t < 8; t++) {
        if (!(sym >> t & 1)) {
            continue;
        }
        for (int flip = 0; flip < (mode == CANON_FLIP ? 2 : 1); flip++) {   /* (Flip: as it is, and 10 - d) */
            uint8_t name[10] = {0};         /* the renumbering: name[d] = what digit d becomes */
            int next = 1;
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

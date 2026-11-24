/**
 * @file canon_core.c
 * @ingroup canon_core
 * @brief A puzzle's canonical hash (canon_core.h).
 */
#include "canon_core.h"
#include "generator.h"
#include <string.h>

/* (documented in canon_core.h) */
uint8_t canon_symmetries(int variant)
{
    switch (variant) {                  /* (tests/test_main.c checks these against each board's peers) */
    case VAR_MINI:
        return 0x35;                    /* 2x3 boxes: no quarter turns, no diagonal mirrors */
    case VAR_FLATMATES:
        return 0x35;                    /* above and below stay in their column: no quarter turns, no diagonal mirrors */
    case VAR_159:
        return 0x35;                    /* columns stay columns (columns 1, 5, 9 and the column numbers in them) */
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
uint64_t canon_hash(int variant, const uint8_t givens[81])
{
    const uint8_t sym = canon_symmetries(variant);
    if (!sym) {
        return 0;
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

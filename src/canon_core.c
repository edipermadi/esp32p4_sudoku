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
        return 0xFF;
    default:
        return 0;                       /* (digits that matter, or a layout of its own) */
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
    uint8_t best[81], cur[81];
    bool have = false;
    for (int t = 0; t < 8; t++) {
        if (!(sym >> t & 1)) {
            continue;
        }
        uint8_t name[10] = {0};             /* the renumbering: name[d] = what digit d becomes */
        int next = 1;
        for (int k = 0; k < n * n; k++) {   /* reading order of the turned board */
            const int d = givens[canon_cell(t, n, k / n * 9 + k % n)];
            if (d && !name[d]) {
                name[d] = (uint8_t)next++;
            }
            cur[k] = d ? name[d] : 0;
        }
        if (!have || memcmp(cur, best, (size_t)(n * n)) < 0) {
            memcpy(best, cur, (size_t)(n * n));
            have = true;
        }
    }
    uint64_t h = 1469598103934665603ULL;    /* FNV-1a: the variant, then the smallest copy */
    h = (h ^ (uint8_t)variant) * 1099511628211ULL;
    for (int k = 0; k < n * n; k++) {
        h = (h ^ best[k]) * 1099511628211ULL;
    }
    return h ? h : 1;
}

/**
 * @file gen_regions.c
 * @ingroup generator
 * @brief Generating a Jigsaw's regions and a Killer's cages for a solution.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---------------------------------------------------------------- Jigsaw regions */

/**
 * @brief is region `reg` of the map one connected piece (4-neighbours)?
 * @param[in] map  the region of each cell
 * @param[in] reg  the region
 * @return true if so
 */
static bool is_region_connected(const uint8_t map[81], int reg)
{
    uint8_t seen[81] = {0}, stack[81];
    int sp = 0, start = -1, total = 0, reached = 0;
    for (int c = 0; c < 81; c++) {
        if (map[c] != reg) {
            continue;
        }
        total++;
        if (start < 0) {
            start = c;
        }
    }
    if (start < 0) {
        return false;
    }
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

/* ---------------------------------------------------------------- Killer cages */

/* (documented in gen_internal.h) */
void gen_jigsaw_map(uint8_t map[81], uint32_t *rng)
{
    for (int c = 0; c < 81; c++) {
        map[c] = (uint8_t)((c / 27) * 3 + (c % 9) / 3);
    }
    int done = 0;
    for (int tries = 0; tries < 20000 && done < 90; tries++) {
        int a = (int)(rnd(rng) % 81), r = a / 9, k = a % 9;
        int dir = (int)(rnd(rng) % 4);
        int b = dir == 0 ? (k > 0 ? a - 1 : -1) : dir == 1 ? (k < 8 ? a + 1 : -1)
                : dir == 2 ? (r > 0 ? a - 9 : -1) : (r < 8 ? a + 9 : -1);
        if (b < 0 || map[a] == map[b]) {
            continue;
        }
        int A = map[a], B = map[b];
        /* a cell of B (not a) that touches A elsewhere: it goes to A */
        uint8_t cand[81];
        int nc = 0;
        for (int c = 0; c < 81; c++) {
            if (map[c] != B) {
                continue;
            }
            int cr = c / 9, ck = c % 9;
            const int nb[4] = {ck > 0 ? c - 1 : -1, ck < 8 ? c + 1 : -1, cr > 0 ? c - 9 : -1, cr < 8 ? c + 9 : -1};
            for (int i = 0; i < 4; i++) {
                if (nb[i] >= 0 && nb[i] != a && map[nb[i]] == A) {
                    cand[nc++] = (uint8_t)c;
                    break;
                }
            }
        }
        if (!nc) {
            continue;
        }
        int c = cand[rnd(rng) % (uint32_t)nc];
        map[a] = (uint8_t)B;
        map[c] = (uint8_t)A;
        if (is_region_connected(map, A) && is_region_connected(map, B)) {
            done++;
        } else {
            map[a] = (uint8_t)A;
            map[c] = (uint8_t)B;
        }
    }
}

/* (documented in gen_internal.h) */
bool gen_killer_cages(const uint8_t full[81], int max_size, uint32_t *rng, sudoku_geom_t *out)
{
    uint8_t cage_of[81], sums[CAGES_MAX], size[CAGES_MAX], order[81];
    uint16_t digits[CAGES_MAX];
    memset(cage_of, CAGE_NONE, sizeof(cage_of));
    for (int i = 0; i < 81; i++) {
        order[i] = (uint8_t)i;
    }
    for (int i = 80; i > 0; i--) {
        int j = (int)(rnd(rng) % (uint32_t)(i + 1));
        uint8_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    int n = 0;
    for (int oi = 0; oi < 81; oi++) {
        int start = order[oi];
        if (cage_of[start] != CAGE_NONE) {
            continue;
        }
        int k = n++, target = 2 + (int)(rnd(rng) % (uint32_t)(max_size - 1));
        uint8_t cells[9];
        int nc = 0;
        cells[nc++] = (uint8_t)start;
        cage_of[start] = (uint8_t)k;
        digits[k] = (uint16_t)(1u << full[start]);
        while (nc < target) {
            uint8_t cand[36];
            int ncand = 0;
            for (int i = 0; i < nc; i++) {
                int c = cells[i], r = c / 9, q = c % 9;
                const int nb[4] = {q > 0 ? c - 1 : -1, q < 8 ? c + 1 : -1, r > 0 ? c - 9 : -1, r < 8 ? c + 9 : -1};
                for (int j = 0; j < 4; j++) {
                    int o = nb[j];
                    if (o >= 0 && cage_of[o] == CAGE_NONE && !(digits[k] >> full[o] & 1)) {
                        cand[ncand++] = (uint8_t)o;
                    }
                }
            }
            if (!ncand) {
                break;
            }
            int o = cand[rnd(rng) % (uint32_t)ncand];
            cells[nc++] = (uint8_t)o;
            cage_of[o] = (uint8_t)k;
            digits[k] |= (uint16_t)(1u << full[o]);
        }
        size[k] = (uint8_t)nc;
    }
    /* lone cells: into a neighbouring cage that can take the digit */
    for (int c = 0; c < 81; c++) {
        int k = cage_of[c];
        if (size[k] != 1) {
            continue;
        }
        int r = c / 9, q = c % 9;
        const int nb[4] = {q > 0 ? c - 1 : -1, q < 8 ? c + 1 : -1, r > 0 ? c - 9 : -1, r < 8 ? c + 9 : -1};
        for (int j = 0; j < 4; j++) {
            int o = nb[j];
            if (o < 0) {
                continue;
            }
            int k2 = cage_of[o];
            if (k2 == k || size[k2] >= max_size + 1 || (digits[k2] >> full[c] & 1)) {
                continue;
            }
            cage_of[c] = (uint8_t)k2;
            size[k2]++;
            digits[k2] |= (uint16_t)(1u << full[c]);
            size[k] = 0;
            break;
        }
    }
    /* renumber (cages emptied above go), top-left first, and add up the sums */
    uint8_t map[CAGES_MAX];
    memset(map, CAGE_NONE, sizeof(map));
    int m = 0;
    for (int c = 0; c < 81; c++) {
        int k = cage_of[c];
        if (map[k] == CAGE_NONE) {
            map[k] = (uint8_t)m;
            sums[m++] = 0;
        }
        cage_of[c] = map[k];
        sums[cage_of[c]] = (uint8_t)(sums[cage_of[c]] + full[c]);
    }
    return gen_geom_killer(cage_of, sums, m, out);
}

/* (documented in gen_internal.h) */
bool gen_sumless_cages(const uint8_t full[81], uint32_t *rng, sudoku_geom_t *out)
{
    if (!gen_killer_cages(full, 4, rng, out)) {
        return false;
    }
    uint8_t cage_of[81], sums[CAGES_MAX] = {0}, map[CAGES_MAX];
    memset(map, CAGE_NONE, sizeof(map));
    int m = 0;
    for (int c = 0; c < 81; c++) {
        cage_of[c] = CAGE_NONE;
        const int k = out->cage_of[c], n = out->cage_n[k];
        bool row = true, col = true, box = true;    /* in one row, column or box: the cage says nothing more */
        for (int i = 1; i < n; i++) {
            int a = out->cage_cell[k][0], b = out->cage_cell[k][i];
            row &= a / 9 == b / 9;
            col &= a % 9 == b % 9;
            box &= out->region[a] == out->region[b];
        }
        if (n < 2 || row || col || box) {
            continue;
        }
        if (map[k] == CAGE_NONE) {
            map[k] = (uint8_t)m++;
        }
        cage_of[c] = map[k];
    }
    return m >= 4 && gen_geom_killer(cage_of, sums, m, out);
}

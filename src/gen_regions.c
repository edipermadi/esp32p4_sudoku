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

/**
 * @brief Reshapes a region map of an n x n board by trading cells across region borders (a cell of A goes to B, a
 *        cell of B next to A goes to A) while both regions stay in one piece; the cells off the board hold 0xFF.
 * @param[in,out] map  the region of each cell
 * @param[in] n  the board's side
 * @param[in] want  the trades to make
 * @param[in,out] rng  the random state
 */
static void jigsaw_reshape(uint8_t map[81], int n, int want, uint32_t *rng)
{
    int done = 0;
    for (int tries = 0; tries < 20000 && done < want; tries++) {
        int a = (int)(rnd(rng) % 81), r = a / 9, k = a % 9;
        int dir = (int)(rnd(rng) % 4);
        if (r >= n || k >= n) {
            continue;
        }
        int b = dir == 0 ? (k > 0 ? a - 1 : -1) : dir == 1 ? (k < n - 1 ? a + 1 : -1)
                : dir == 2 ? (r > 0 ? a - 9 : -1) : (r < n - 1 ? a + 9 : -1);
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
void gen_jigsaw_map(uint8_t map[81], uint32_t *rng)
{
    for (int c = 0; c < 81; c++) {
        map[c] = (uint8_t)((c / 27) * 3 + (c % 9) / 3);
    }
    jigsaw_reshape(map, 9, 90, rng);
}

/**
 * @brief Jigsaw 7x7: the most cells any region of a map has in one row or column (7: a whole line).
 * @param[in] map  the region of each cell
 * @return it
 */
static int jigsaw7_straightest(const uint8_t map[81])
{
    int most = 0;
    for (int reg = 0; reg < 7; reg++) {
        for (int i = 0; i < 7; i++) {
            int row = 0, col = 0;
            for (int j = 0; j < 7; j++) {
                row += map[i * 9 + j] == reg;
                col += map[j * 9 + i] == reg;
            }
            most = row > most ? row : most;
            most = col > most ? col : most;
        }
    }
    return most;
}

/* (documented in gen_internal.h) */
void gen_jigsaw7_map(uint8_t map[81], uint32_t *rng)
{
    for (int tries = 0; tries < 50; tries++) {      /* (the rows reshaped; again while a region stays row-like) */
        for (int c = 0; c < 81; c++) {
            map[c] = c / 9 < 7 && c % 9 < 7 ? (uint8_t)(c / 9) : 0xFF;   /* (the rows; off the board: no region) */
        }
        jigsaw_reshape(map, 7, 150, rng);
        if (jigsaw7_straightest(map) <= 5) {
            break;                                  /* (no region with 6 or 7 cells in one line) */
        }
    }
    for (int c = 0; c < 81; c++) {
        if (map[c] == 0xFF) {
            map[c] = 0;
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

/* (documented in gen_internal.h) */
bool gen_product_cages(const uint8_t full[81], int max_size, uint32_t *rng, sudoku_geom_t *out)
{
    if (!gen_killer_cages(full, max_size, rng, out)) {
        return false;
    }
    uint8_t cage_of[81];
    uint16_t prods[CAGES_MAX];
    memcpy(cage_of, out->cage_of, 81);
    const int n = out->ncages;
    for (int k = 0; k < n; k++) {
        prods[k] = 1;
        for (int i = 0; i < out->cage_n[k]; i++) {
            prods[k] = (uint16_t)(prods[k] * full[out->cage_cell[k][i]]);
        }
    }
    return gen_geom_product(cage_of, prods, n, out);
}

/**
 * @brief Extra Regions: does a cell touch, side by side or at a corner, a cell of another region than this one?
 * @param[in] cage_of  each cell's region (CAGE_NONE: none)
 * @param[in] c  the cell
 * @param[in] mine  this region (its own cells don't count)
 * @return true if so
 */
static bool is_region_near(const uint8_t cage_of[81], int c, int mine)
{
    for (int dr = -1; dr <= 1; dr++) {
        for (int dq = -1; dq <= 1; dq++) {
            const int r = c / 9 + dr, q = c % 9 + dq;
            if (r >= 0 && r < 9 && q >= 0 && q < 9 && cage_of[r * 9 + q] != CAGE_NONE && cage_of[r * 9 + q] != mine) {
                return true;
            }
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
bool gen_extra_regions(const uint8_t full[81], int want, uint32_t *rng, sudoku_geom_t *out)
{
    uint8_t cage_of[81], zero[CAGES_MAX] = {0};
    memset(cage_of, CAGE_NONE, sizeof(cage_of));
    int n = 0;
    for (int tries = 0; tries < 400 && n < want; tries++) {
        int start = (int)(rnd(rng) % 81), target = 5 + (int)(rnd(rng) % 5), nc = 0;
        uint8_t cells[9];
        uint16_t digits = 0;
        /* (a region keeps a cell away from the others, corners too: their tints stay apart) */
        if (cage_of[start] != CAGE_NONE || is_region_near(cage_of, start, n)) {
            continue;
        }
        cells[nc++] = (uint8_t)start;
        digits = (uint16_t)(1u << full[start]);
        cage_of[start] = (uint8_t)n;
        while (nc < target) {
            uint8_t cand[36];
            int ncand = 0;
            for (int i = 0; i < nc; i++) {
                const int c = cells[i], r = c / 9, q = c % 9;
                const int nb[4] = {q > 0 ? c - 1 : -1, q < 8 ? c + 1 : -1, r > 0 ? c - 9 : -1, r < 8 ? c + 9 : -1};
                for (int j = 0; j < 4; j++) {
                    const int o = nb[j];
                    if (o < 0 || cage_of[o] != CAGE_NONE || (digits >> full[o] & 1)) {
                        continue;
                    }
                    if (!is_region_near(cage_of, o, n)) {
                        cand[ncand++] = (uint8_t)o;
                    }
                }
            }
            if (!ncand) {
                break;
            }
            const int o = cand[rnd(rng) % (uint32_t)ncand];
            cells[nc++] = (uint8_t)o;
            cage_of[o] = (uint8_t)n;
            digits |= (uint16_t)(1u << full[o]);
        }
        bool boxes = false, rows = false, cols = false;   /* (in one row, column or box: it says nothing new) */
        for (int i = 1; i < nc; i++) {
            boxes |= out->region[cells[i]] != out->region[cells[0]];
            rows |= cells[i] / 9 != cells[0] / 9;
            cols |= cells[i] % 9 != cells[0] % 9;
        }
        if (nc < 5 || !boxes || !rows || !cols) {
            for (int i = 0; i < nc; i++) {
                cage_of[cells[i]] = CAGE_NONE;
            }
            continue;
        }
        n++;
    }
    if (n < 3 || !gen_geom_killer(cage_of, zero, n, out)) {
        return false;
    }
    out->variant = VAR_REGIONS;
    return true;
}

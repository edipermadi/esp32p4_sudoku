/**
 * @file gen_make.c
 * @ingroup generator
 * @brief Generating: a full grid dug down to a level; Jigsaw regions, Killer cages, lines and the
 *        other variants' decorations made for it.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @brief one attempt: clues taken away in random symmetric gen_pairs (a cell and its
 * 180-degree turn) while the solution stays unique; for Easy, stops at a
 * random few clues above the Easy minimum
 * @param[in] G  the board
 * @param[in] full  the solution
 * @param[out] out  gets the puzzle
 * @param[in] level  the level aimed at
 * @param[in,out] rng  the random state
 */
static void dig(const sudoku_geom_t *G, const uint8_t full[81], uint8_t out[81], gen_level_t level, uint32_t *rng)
{
    memcpy(out, full, 81);
    const int n = G->n;
    uint8_t order[41];                  /* one cell of each pair (the first half, plus the centre) */
    int no = 0;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k], r = c / 9, col = c % 9;
        int m = (n - 1 - r) * 9 + (n - 1 - col);
        if (c <= m) {
            order[no++] = (uint8_t)c;
        }
    }
    for (int i = no - 1; i > 0; i--) {
        int j = (int)(rnd(rng) % (uint32_t)(i + 1));
        uint8_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    int clues = G->ncells;
    int stop = level == GEN_EASY ? gen_easy_min_clues(G) + (int)(rnd(rng) % (n == 9 ? 5 : 3)) : 0;
    if (G->ncages && level == GEN_MEDIUM) {
        stop = 2 + (int)(rnd(rng) % 3);    /* Killer Medium: a couple of givens */
    }
    /* X and 6x6: Medium and Hard keep a clue whose removal would need harder
     * techniques, so most attempts land on the level (digging to the minimum
     * mostly gives Expert there). Classic digs as it always has, so its
     * puzzles for a seed (the daily puzzle) stay the same. */
    int max_tier = 3;
    if (G->variant != VAR_CLASSIC && level == GEN_MEDIUM) {
        max_tier = 1;
    }
    if (G->variant != VAR_CLASSIC && level == GEN_HARD) {
        max_tier = 2;
    }
    /* Jigsaw and Killer: a uniqueness check that takes too long keeps the
     * clue (near the minimum, the plain search can take thousands of times longer) */
    const uint32_t node_limit = G->variant == VAR_CLASSIC ? 0 : G->variant == VAR_JIGSAW ? 4000
                                : G->variant == VAR_BETWEEN ? 1500 : 6000;   /* (Between: its counts are slow, cut them short) */
    for (int k = 0; k < no && clues > stop; k++) {
        sudoku_gen_yield();
        int a = order[k], b = (n - 1 - a / 9) * 9 + (n - 1 - a % 9);
        uint8_t va = out[a], vb = out[b];
        out[a] = out[b] = 0;
        /* (marks and circles: when the logic alone solves it within max_tier, it has one
         * solution, no count needed: Greater-Than's counting is slow. Only for these, so
         * the other variants' puzzles for a seed, the daily ones among them, stay the same.) */
        bool ok;
        /* (and the newest line kinds, below Expert: there the logic never solves the last steps, so
         * it would only add to the count) */
        const bool newest_lines = G->nthermos && (G->line_kind == LINE_ZIPPER || G->line_kind == LINE_BETWEEN ||
                                  G->line_kind == LINE_ENTROPIC || G->line_kind == LINE_MODULAR);
        if (G->edge_kind || G->nquads || (newest_lines && max_tier < 3)) {   /* (count only what the logic can't solve) */
            int tier = gen_technique_tier(G, out);
            ok = tier <= max_tier && (tier < 3 || gen_count_bounded(G, out, 2, node_limit) == 1);
        } else {
            ok = gen_count_bounded(G, out, 2, node_limit) == 1 && (max_tier == 3 || gen_technique_tier(G, out) <= max_tier);
        }
        if (ok) {
            clues -= a == b ? 1 : 2;
        } else {
            out[a] = va;
            out[b] = vb;
        }
    }
}

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

/**
 * @brief A random Jigsaw map: the 3x3 boxes, reshaped by trading cells across
 * region borders (a cell of A goes to B, a cell of B next to A goes to A)
 * while both regions stay in one piece.
 * @param[out] map  gets the region of each cell
 * @param[in,out] rng  the random state
 */
static void jigsaw_map(uint8_t map[81], uint32_t *rng)
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

/* ---------------------------------------------------------------- Killer cages */

/**
 * @brief Random cages over a solved grid: from each cell not in a cage yet (in a
 * random order), a cage grows to 2..max_size cells through neighbours whose
 * digit isn't in it yet. A lone cell left over joins a neighbouring cage
 * that can take its digit, if there is one.
 * @param[in] full  the solution
 * @param[in] max_size  cells per cage at most
 * @param[in,out] rng  the random state
 * @param[out] out  gets the board
 * @return false if the cages don't make one
 */
static bool killer_cages(const uint8_t full[81], int max_size, uint32_t *rng, sudoku_geom_t *out)
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

/* ---------------------------------------------------------------- Thermo thermometers */

/* Thermometers over a solved grid: from random cells, a path grows through
 * neighbours (diagonals too) whose digit is higher, 3 to 6 cells long. */
/* (rule_paths() keeps Dutch Whispers and Parity lines from crossing with these, defined further down) */
static bool is_line_crossing(const uint8_t *path, int n, int a, int b);
static bool is_block_crossed(const uint8_t blocks[81], int a, int b);
static void mark_line_blocks(uint8_t blocks[81], const uint8_t *path, int n);

/**
 * @brief Arrow, Renban, Whispers, Parity: up to want lines drawn over the full grid as random walks (side by
 * side or diagonal, not crossing), each kept only if the grid keeps its rule: an arrow's shaft
 * (1-3 cells) adds up to its circle, a Renban (3-5 cells) is a run of consecutive digits, along a
 * German (Dutch) Whispers line (3-6 cells) neighbours are 5 (4) or more apart, along a Parity line (3-8
 * cells) one odd, one even
 * @param[in] variant  VAR_ARROW, VAR_RENBAN, VAR_WHISPER, VAR_DUTCH or VAR_PARITY
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
static bool rule_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};
    /* (Dutch Whispers, Parity: lines that never cross, as the newer line variants; the older ones' puzzles stay) */
    const bool uncrossed = variant == VAR_DUTCH || variant == VAR_PARITY;
    int n = 0;
    for (int tries = 0; tries < 2000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81);
        int target = variant == VAR_ARROW ? 2 + (int)(rnd(rng) % 3)          /* circle + 1-3 */
                     : variant == VAR_RENBAN ? 3 + (int)(rnd(rng) % 3) : variant == VAR_PARITY ? 3 + (int)(rnd(rng) % 6)
                     : 3 + (int)(rnd(rng) % 4);
        if (used[c] || (variant == VAR_WHISPER && full[c] == 5) || (variant == VAR_ARROW && full[c] < 3)) {
            continue;
        }
        int len = 0, sum = 0, lo = full[c], hi = full[c];
        cells[n][len++] = (uint8_t)c;
        while (len < target) {
            int cur = cells[n][len - 1], r = cur / 9, q = cur % 9, cand[8], nc = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dq = -1; dq <= 1; dq++) {
                    int rr = r + dr, qq = q + dq, o = rr * 9 + qq;
                    if ((!dr && !dq) || rr < 0 || rr > 8 || qq < 0 || qq > 8 || used[o]) {
                        continue;
                    }
                    bool on = false;
                    for (int k = 0; k < len; k++) {
                        on |= cells[n][k] == o;
                    }
                    if (on || (uncrossed && (is_line_crossing(cells[n], len, cur, o) || is_block_crossed(blocks, cur, o)))) {
                        continue;
                    }
                    int x = full[o];
                    bool ok;
                    if (variant == VAR_WHISPER || variant == VAR_DUTCH) {
                        ok = abs(x - full[cur]) >= (variant == VAR_DUTCH ? 4 : 5);
                    } else if (variant == VAR_PARITY) {
                        ok = (x ^ full[cur]) & 1;
                    } else if (variant == VAR_RENBAN) {
                        bool seen = false;
                        for (int k = 0; k < len; k++) {
                            seen |= full[cells[n][k]] == x;
                        }
                        int nlo = x < lo ? x : lo, nhi = x > hi ? x : hi;
                        ok = !seen && nhi - nlo <= target - 1;
                    } else {                        /* Arrow: the shaft stays under the circle */
                        ok = sum + x <= full[c] && (len + 1 < target || sum + x == full[c]);
                    }
                    if (ok) {
                        cand[nc++] = o;
                    }
                }
            }
            if (!nc) {
                break;
            }
            int o = cand[rnd(rng) % (uint32_t)nc];
            cells[n][len++] = (uint8_t)o;
            if (full[o] < lo) {
                lo = full[o];
            }
            if (full[o] > hi) {
                hi = full[o];
            }
            if (len > 1) {
                sum += full[o];
            }
        }
        bool keep = variant == VAR_ARROW ? len >= 2 && sum == full[c]
                    : variant == VAR_RENBAN ? len >= 3 && hi - lo == len - 1 : len >= 3;
        if (!keep) {
            continue;
        }
        for (int k = 0; k < len; k++) {
            used[cells[n][k]] = 1;
        }
        mark_line_blocks(blocks, cells[n], len);
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && gen_geom_lines(variant, cells, lens, n, out);
}

/**
 * @brief a free neighbour of cell cur (side by side or diagonal) for a line: not used by another line,
 * not on this one (on[], its cells so far)
 * @param[in] cur  the cell
 * @param[in] used  the cells on the other lines
 * @param[in] on  this line's cells
 * @param[in] n  how many
 * @param[out] out  gets the neighbours
 * @return how many
 */
static int line_neighbours(int cur, const uint8_t used[81], const uint8_t *on, int n, int out[8])
{
    int r = cur / 9, q = cur % 9, nc = 0;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dq = -1; dq <= 1; dq++) {
            int rr = r + dr, qq = q + dq, o = rr * 9 + qq;
            if ((!dr && !dq) || rr < 0 || rr > 8 || qq < 0 || qq > 8 || used[o]) {
                continue;
            }
            bool taken = false;
            for (int k = 0; k < n; k++) {
                taken |= on[k] == o;
            }
            if (!taken) {
                out[nc++] = o;
            }
        }
    }
    return nc;
}

/**
 * @brief Palindrome: up to want lines (3-9 cells) over the full grid, grown from the middle out, a
 * cell at each end at a time, both holding the same digit (so they never see each other)
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
static bool palindrome_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 2000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 4 + (int)(rnd(rng) % 4);
        if (used[c]) {
            continue;
        }
        uint8_t line[9];                            /* the line so far, its middle first */
        int len = 0, nb[8];
        line[len++] = (uint8_t)c;
        if (target % 2 == 0) {                      /* even: a middle pair, diagonal neighbours with one digit */
            int k = line_neighbours(c, used, line, len, nb), nm = 0, m[8];
            for (int j = 0; j < k; j++) {
                if (full[nb[j]] == full[c]) {
                    m[nm++] = nb[j];
                }
            }
            if (!nm) {
                target--;                           /* none: an odd one */
            } else {
                line[len++] = (uint8_t)m[rnd(rng) % (uint32_t)nm];
            }
        }
        /* grow both ends, a cell at each: a the left end, b the right one (left[] and right[]: the
         * cells added on each side, from the middle out) */
        uint8_t left[5], right[5];
        int nl = 0;
        int a = line[0], b = line[len - 1];
        while (len + 2 * nl + 2 <= target) {          /* (target: 9 at most) */
            uint8_t on[9];
            int non = 0;
            for (int j = 0; j < len; j++) {
                on[non++] = line[j];
            }
            for (int j = 0; j < nl; j++) {
                on[non++] = left[j];
                on[non++] = right[j];
            }
            int na[8], nbb[8], ka = line_neighbours(a, used, on, non, na), kb = line_neighbours(b, used, on, non, nbb);
            int pa[64], pb[64], np = 0;
            for (int i = 0; i < ka; i++) {
                for (int j = 0; j < kb; j++) {
                    if (na[i] != nbb[j] && full[na[i]] == full[nbb[j]]) {
                        pa[np] = na[i];
                        pb[np++] = nbb[j];
                    }
                }
            }
            if (!np) {
                break;
            }
            int pick = (int)(rnd(rng) % (uint32_t)np);
            left[nl] = (uint8_t)pa[pick];
            right[nl++] = (uint8_t)pb[pick];
            a = pa[pick];
            b = pb[pick];
        }
        int total = len + 2 * nl;
        if (total < 3) {
            continue;
        }
        int k = 0;                                  /* end to end: the left side reversed, the middle, the right */
        for (int j = nl - 1; j >= 0; j--) {
            cells[n][k++] = left[j];
        }
        for (int j = 0; j < len; j++) {
            cells[n][k++] = line[j];
        }
        for (int j = 0; j < nl; j++) {
            cells[n][k++] = right[j];
        }
        for (int j = 0; j < total; j++) {
            used[cells[n][j]] = 1;
        }
        lens[n++] = (uint8_t)total;
    }
    return n >= 3 && gen_geom_lines(VAR_PALINDROME, cells, lens, n, out);
}

/**
 * @brief Region Sum Lines: is a line (its cells on the full grid) one: in two boxes or more, the
 * same sum in each?
 * @param[in] full  the solution
 * @param[in] on  its cells
 * @param[in] n  how many
 * @return true if so
 */
static bool is_regionsum_line(const uint8_t full[81], const uint8_t *on, int n)
{
    int box[9], sum[9], nb = 0;
    for (int k = 0; k < n; k++) {
        int b = (on[k] / 27) * 3 + (on[k] % 9) / 3, j = 0;
        while (j < nb && box[j] != b) {
            j++;
        }
        if (j == nb) {
            box[nb] = b;
            sum[nb++] = 0;
        }
        sum[j] += full[on[k]];
    }
    for (int j = 1; j < nb; j++) {
        if (sum[j] != sum[0]) {
            return false;
        }
    }
    return nb >= 2;
}

/**
 * @brief Region Sum Lines: up to want lines over the full grid, random walks of up to 8 cells kept at
 * the longest length (3 or more) where each box they pass through adds up to the same
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
static bool regionsum_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 3000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 4 + (int)(rnd(rng) % 5), len = 0, best = 0, nb[8];
        if (used[c]) {
            continue;
        }
        cells[n][len++] = (uint8_t)c;
        while (len < target) {
            int k = line_neighbours(cells[n][len - 1], used, cells[n], len, nb);
            if (!k) {
                break;
            }
            cells[n][len++] = (uint8_t)nb[rnd(rng) % (uint32_t)k];
            if (len >= 3 && is_regionsum_line(full, cells[n], len)) {
                best = len;
            }
        }
        if (!best) {
            continue;
        }
        for (int k = 0; k < best; k++) {
            used[cells[n][k]] = 1;
        }
        lens[n++] = (uint8_t)best;
    }
    return n >= 3 && gen_geom_lines(VAR_REGIONSUM, cells, lens, n, out);
}

/**
 * @brief Would a diagonal step from a to b cross the line, path (its cells in order)? Two cells corner
 * to corner, joined on the line, make an X with that step.
 * @param[in] path  the line's cells, in order
 * @param[in] n  how many
 * @param[in] a  the step's cell on the line
 * @param[in] b  the cell it goes to
 * @return true if it crosses
 */
static bool is_line_crossing(const uint8_t *path, int n, int a, int b)
{
    if (a / 9 == b / 9 || a % 9 == b % 9) {
        return false;                               /* side by side: nothing to cross */
    }
    const int x = (a / 9) * 9 + b % 9, y = (b / 9) * 9 + a % 9;   /* the other two corners */
    for (int k = 0; k + 1 < n; k++) {
        if ((path[k] == x && path[k + 1] == y) || (path[k] == y && path[k + 1] == x)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Would a diagonal step from a to b cross another line? Lines kept so far mark the 2x2 blocks
 * they cross diagonally (mark_line_blocks()); a diagonal step through a marked block would make an X.
 * @param[in] blocks  per cell, 1: the 2x2 block with that cell at its top left has a line across it
 * @param[in] a  the step's cell on the line
 * @param[in] b  the cell it goes to
 * @return true if it crosses
 */
static bool is_block_crossed(const uint8_t blocks[81], int a, int b)
{
    if (a / 9 == b / 9 || a % 9 == b % 9) {
        return false;
    }
    int r = a / 9 < b / 9 ? a / 9 : b / 9, c = a % 9 < b % 9 ? a % 9 : b % 9;
    return blocks[r * 9 + c] != 0;
}

/**
 * @brief Do two diagonal steps, a to b and c to d, cross each other (the two diagonals of one 2x2 block)?
 * @param[in] a  the first step's cell
 * @param[in] b  where it goes
 * @param[in] c  the second step's cell
 * @param[in] d  where it goes
 * @return true if so
 */
static bool is_steps_crossing(int a, int b, int c, int d)
{
    if (a / 9 == b / 9 || a % 9 == b % 9 || c / 9 == d / 9 || c % 9 == d % 9) {
        return false;
    }
    int k1 = (a / 9 < b / 9 ? a / 9 : b / 9) * 9 + (a % 9 < b % 9 ? a % 9 : b % 9);
    int k2 = (c / 9 < d / 9 ? c / 9 : d / 9) * 9 + (c % 9 < d % 9 ? c % 9 : d % 9);
    return k1 == k2;
}

/**
 * @brief A line kept: the 2x2 blocks it crosses diagonally marked, for is_block_crossed().
 * @param[in,out] blocks  the marks
 * @param[in] path  the line's cells, in order
 * @param[in] n  how many
 */
static void mark_line_blocks(uint8_t blocks[81], const uint8_t *path, int n)
{
    for (int k = 0; k + 1 < n; k++) {
        int a = path[k], b = path[k + 1];
        if (a / 9 != b / 9 && a % 9 != b % 9) {
            int r = a / 9 < b / 9 ? a / 9 : b / 9, c = a % 9 < b % 9 ? a % 9 : b % 9;
            blocks[r * 9 + c] = 1;
        }
    }
}

/**
 * @brief Zipper Lines: up to want lines (3-7 cells) over the full grid, grown from the middle out, a
 * cell at each end at a time, each pair adding up to the line's total (odd: its middle digit; even:
 * the middle pair's sum); a line never crosses itself or another
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
static bool zipper_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 2000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 4 + (int)(rnd(rng) % 4), nb[8];
        if (used[c]) {
            continue;
        }
        uint8_t path[9];                            /* the line so far, end to end */
        int len = 0, total;
        path[len++] = (uint8_t)c;
        if (target % 2 == 0) {                      /* even: a middle pair, its sum the total */
            int k = line_neighbours(c, used, path, len, nb), m = 0;
            for (int j = 0; j < k; j++) {           /* (not across another line) */
                if (!is_block_crossed(blocks, c, nb[j])) {
                    nb[m++] = nb[j];
                }
            }
            if (!m) {
                continue;
            }
            int o = nb[rnd(rng) % (uint32_t)m];
            path[len++] = (uint8_t)o;
            total = full[c] + full[o];
        } else {
            total = full[c];
            if (total < 2) {
                continue;
            }
        }
        while (len + 2 <= target) {                 /* a pair, one cell at each end */
            int a = path[0], b = path[len - 1], na[8], nbb[8];
            int ka = line_neighbours(a, used, path, len, na), kb = line_neighbours(b, used, path, len, nbb);
            int pa[64], pb[64], np = 0;
            for (int i = 0; i < ka; i++) {
                for (int j = 0; j < kb; j++) {
                    if (na[i] != nbb[j] && full[na[i]] + full[nbb[j]] == total &&
                            !is_line_crossing(path, len, a, na[i]) && !is_line_crossing(path, len, b, nbb[j]) &&
                            !is_block_crossed(blocks, a, na[i]) && !is_block_crossed(blocks, b, nbb[j]) &&
                            !is_steps_crossing(a, na[i], b, nbb[j])) {
                        pa[np] = na[i];
                        pb[np++] = nbb[j];
                    }
                }
            }
            if (!np) {
                break;
            }
            int pick = (int)(rnd(rng) % (uint32_t)np);
            memmove(path + 1, path, (size_t)len);
            path[0] = (uint8_t)pa[pick];
            path[++len] = (uint8_t)pb[pick];
            len++;
        }
        if (len < 3) {
            continue;
        }
        for (int j = 0; j < len; j++) {
            cells[n][j] = path[j];
            used[path[j]] = 1;
        }
        mark_line_blocks(blocks, path, len);
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && gen_geom_lines(VAR_ZIPPER, cells, lens, n, out);
}

/**
 * @brief Between Lines: up to want lines over the full grid, from a circle, a random walk of up to 7
 * cells whose digits all lie on one side of it (above it, or below), kept at the longest length
 * whose last cell (the other circle) lies beyond all the cells between; a line never crosses itself
 * or another
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
static bool between_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 2000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 3 + (int)(rnd(rng) % 5), len = 0, best = 0, nb[8];
        if (used[c]) {
            continue;
        }
        /* up: the cells after it are bigger (the far circle the biggest); down: smaller */
        const bool up = full[c] < 5 || (full[c] == 5 && (rnd(rng) & 1));
        int far = full[c];                          /* the farthest digit from the circle so far */
        cells[n][len++] = (uint8_t)c;
        while (len < target) {
            int cur = cells[n][len - 1], k = line_neighbours(cur, used, cells[n], len, nb), m = 0;
            for (int j = 0; j < k; j++) {
                int x = full[nb[j]];
                if ((up ? x > full[c] : x < full[c]) && !is_line_crossing(cells[n], len, cur, nb[j]) && !is_block_crossed(blocks, cur, nb[j])) {
                    nb[m++] = nb[j];
                }
            }
            if (!m) {
                break;
            }
            int o = nb[rnd(rng) % (uint32_t)m], x = full[o];
            cells[n][len++] = (uint8_t)o;
            if (len >= 3 && (up ? x > far : x < far)) {
                best = len;                         /* o as the far circle: every cell between is inside */
            }
            if (up ? x > far : x < far) {
                far = x;
            }
        }
        if (!best) {
            continue;
        }
        for (int j = 0; j < best; j++) {
            used[cells[n][j]] = 1;
        }
        mark_line_blocks(blocks, cells[n], best);
        lens[n++] = (uint8_t)best;
    }
    return n >= 3 && gen_geom_lines(VAR_BETWEEN, cells, lens, n, out);
}

/**
 * @brief Entropic, Modular Lines: up to want lines over the full grid, random walks of 3 to 8 cells
 * through cells whose digit keeps the rule: each of the first three in a group of its own, every one
 * after in the group of the cell three back; a line never crosses itself or another
 * @param[in] variant  VAR_ENTROPIC or VAR_MODULAR
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
static bool group_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    const int kind = variant == VAR_ENTROPIC ? LINE_ENTROPIC : LINE_MODULAR;
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 2000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 3 + (int)(rnd(rng) % 6), len = 0, nb[8];
        if (used[c]) {
            continue;
        }
        cells[n][len++] = (uint8_t)c;
        while (len < target) {
            int cur = cells[n][len - 1], k = line_neighbours(cur, used, cells[n], len, nb), m = 0;
            for (int j = 0; j < k; j++) {
                int g = sudoku_line_group(kind, full[nb[j]]);
                bool ok = len >= 3 ? g == sudoku_line_group(kind, full[cells[n][len - 3]]) : true;
                for (int i = 0; len < 3 && i < len; i++) {
                    ok &= g != sudoku_line_group(kind, full[cells[n][i]]);
                }
                if (ok && !is_line_crossing(cells[n], len, cur, nb[j]) && !is_block_crossed(blocks, cur, nb[j])) {
                    nb[m++] = nb[j];
                }
            }
            if (!m) {
                break;
            }
            cells[n][len++] = (uint8_t)nb[rnd(rng) % (uint32_t)m];
        }
        if (len < 3) {
            continue;
        }
        for (int j = 0; j < len; j++) {
            used[cells[n][j]] = 1;
        }
        mark_line_blocks(blocks, cells[n], len);
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && gen_geom_lines(variant, cells, lens, n, out);
}

/**
 * @brief Thermo: up to want thermometers (3-6 cells) drawn over the full grid as random walks whose digits rise from the bulb;
 *        Slow Thermo (3-7 cells): or stay the same (where a step can: half the time; a solved grid only repeats a
 *        digit in cells that don't see each other)
 * @param[in] slow  Slow Thermo
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  thermometers wanted
 * @param[out] out  gets the board
 * @return false: fewer than 3
 */
static bool thermo_paths(bool slow, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};   /* (Slow: never crossing) */
    int n = 0;
    for (int tries = 0; tries < 400 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 3 + (int)(rnd(rng) % (slow ? 5 : 4));
        if (used[c] || (!slow && full[c] > 9 - (target - 1))) {
            continue;
        }
        int len = 0;
        cells[n][len++] = (uint8_t)c;
        while (len < target) {
            int cur = cells[n][len - 1], r = cur / 9, q = cur % 9, cand[8], nc = 0, same[8], ns = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dq = -1; dq <= 1; dq++) {
                    int rr = r + dr, qq = q + dq, o = rr * 9 + qq;
                    if ((!dr && !dq) || rr < 0 || rr > 8 || qq < 0 || qq > 8 || used[o]) {
                        continue;
                    }
                    bool on = false;
                    for (int k = 0; k < len; k++) {
                        on |= cells[n][k] == o;
                    }
                    on |= slow && (is_line_crossing(cells[n], len, cur, o) || is_block_crossed(blocks, cur, o));
                    if (!on && full[o] > full[cur]) {
                        cand[nc++] = o;
                    }
                    if (!on && slow && full[o] == full[cur]) {
                        same[ns++] = o;
                    }
                }
            }
            if (ns && (!nc || rnd(rng) % 2)) {     /* Slow: the same digit, half the time it can */
                cells[n][len++] = (uint8_t)same[rnd(rng) % (uint32_t)ns];
                continue;
            }
            if (!nc) {
                break;
            }
            cells[n][len++] = (uint8_t)cand[rnd(rng) % (uint32_t)nc];
        }
        if (len < 3) {
            continue;
        }
        for (int k = 0; k < len; k++) {
            used[cells[n][k]] = 1;
        }
        if (slow) {
            mark_line_blocks(blocks, cells[n], len);
        }
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && gen_geom_lines(slow ? VAR_SLOW : VAR_THERMO, cells, lens, n, out);
}

/* ---------------------------------------------------------------- generating (all variants) */

/* (documented in generator.h) */
bool sudoku_generate(int variant, gen_level_t level, uint32_t seed, uint8_t givens[81], sudoku_geom_t *board,
                     gen_level_t *got_level)
{
    static sudoku_geom_t *jig;                      /* a board made with its puzzle, being tried (PSRAM) */
    const bool jigsaw = variant == VAR_JIGSAW, killer = variant == VAR_KILLER;
    const bool marks = variant == VAR_EVENODD || variant == VAR_CONSEC;
    const bool thermo = variant == VAR_THERMO || variant == VAR_SLOW, sandwich = variant == VAR_SANDWICH;
    const bool lines = variant == VAR_ARROW || variant == VAR_RENBAN || variant == VAR_WHISPER || variant == VAR_DUTCH ||
                       variant == VAR_PARITY;
    const bool sky = variant == VAR_SKY, lk = variant == VAR_LK, xsum = variant == VAR_XSUM;
    const bool pal = variant == VAR_PALINDROME, rsum = variant == VAR_REGIONSUM, frame = variant == VAR_FRAME;
    const bool zip = variant == VAR_ZIPPER, betw = variant == VAR_BETWEEN;
    const bool grp = variant == VAR_ENTROPIC || variant == VAR_MODULAR;
    const bool outs = variant == VAR_OUTSIDE, ross = variant == VAR_ROSSINI;
    const bool edges = variant == VAR_KROPKI || variant == VAR_XV || variant == VAR_GT, quad = variant == VAR_QUAD;
    const bool own = jigsaw || killer || marks || thermo || sandwich || edges || quad || lines || sky || lk || xsum ||
                     pal || rsum || frame || zip || betw || grp || outs || ross;
    if (sandwich) {
        gen_bread_tables();
    }
    if (own && !jig && !(jig = malloc(sizeof(*jig)))) {
        return false;
    }
    const sudoku_geom_t *G = own ? jig : sudoku_geom(variant);
    uint32_t rng = seed ? seed : 1;
    int tries = level == GEN_EXPERT ? 400 : (level == GEN_HARD ? 200 : 60);
    if (G->n == 6 && level == GEN_HARD) {
        tries = 2000;    /* rare on 6x6, but an attempt is cheap there */
    }
    if (variant == VAR_SANDWICH && level == GEN_HARD) {
        tries = 40;    /* slow attempts: bound the wait */
    }
    uint8_t full[81], cand[81], map[81];
    int best = -1;
    memset(givens, 0, 81);
    for (int t = 0; t < tries; t++) {
        sudoku_gen_yield();
        if (jigsaw) {                               /* a new shape every few tries */
            if (t % 4 == 0) {
                jigsaw_map(map, &rng);
                sudoku_geom_jigsaw(map, jig);
            }
            if (!gen_random_full_grid(G, full, &rng, 20000)) {   /* no quick fill: another shape */
                t += 3 - t % 4;
                continue;
            }
        } else if (killer) {                        /* a grid, then cages drawn over it */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!killer_cages(full, level >= GEN_HARD ? 5 : 3, &rng, jig)) {
                continue;
            }
        } else if (marks) {                         /* a grid, then its marks */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            char text[82];
            for (int c = 0; c < 81; c++) {
                if (variant == VAR_CONSEC) {
                    int m = 0;
                    if (c % 9 < 8 && abs(full[c] - full[c + 1]) == 1) {
                        m |= CONSEC_RIGHT;
                    }
                    if (c / 9 < 8 && abs(full[c] - full[c + 9]) == 1) {
                        m |= CONSEC_DOWN;
                    }
                    text[c] = (char)('0' + m);
                } else {                            /* about a third of the cells say even or odd */
                    text[c] = rnd(&rng) % 3 ? '.' : (full[c] % 2 ? 'o' : 'e');
                }
            }
            text[81] = 0;
            sudoku_geom_marks(variant, text, jig);
        } else if (edges) {                         /* a grid, then its marks */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            const int ek = variant == VAR_KROPKI ? EDGE_KROPKI : variant == VAR_XV ? EDGE_XV : EDGE_GT;
            char text[82];
            for (int c = 0; c < 81; c++) {
                int m[2] = {0, 0};
                for (int k = 0; k < 2; k++) {
                    int o = k ? c + 9 : c + 1;
                    if ((k == 0 && c % 9 == 8) || o > 80) {
                        continue;
                    }
                    int a = full[c], b = full[o];
                    if (ek == EDGE_GT) {            /* inside a box, every pair */
                        if (k ? (c / 9) % 3 == 2 : (c % 9) % 3 == 2) {
                            continue;
                        }
                        m[k] = a < b ? MARK_LESS : MARK_MORE;
                    } else if (ek == EDGE_KROPKI) {
                        bool white = abs(a - b) == 1, black = a == 2 * b || b == 2 * a;
                        m[k] = white && black ? (rnd(&rng) & 1 ? MARK_WHITE : MARK_BLACK)   /* 1 and 2: either */
                               : white ? MARK_WHITE : black ? MARK_BLACK : MARK_NONE;
                    } else {
                        m[k] = a + b == 5 ? MARK_V : a + b == 10 ? MARK_X : MARK_NONE;
                    }
                }
                text[c] = (char)('0' + m[0] + 3 * m[1]);
            }
            text[81] = 0;
            sudoku_geom_marks(variant, text, jig);
        } else if (quad) {                          /* a grid, then circles at random corners */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint8_t at[QUADS_MAX], n[QUADS_MAX], dig[QUADS_MAX][4];
            int nq = level >= GEN_HARD ? 18 : 14;
            for (int q = 0; q < nq; q++) {
                int a;
                bool dup;
                do {                                /* a corner not used yet */
                    int r = (int)(rnd(&rng) % 8), c = (int)(rnd(&rng) % 8);
                    a = r * 9 + c;
                    dup = false;
                    for (int p = 0; p < q; p++) {
                        dup |= at[p] == a;
                    }
                } while (dup);
                at[q] = (uint8_t)a;
                uint8_t four[4] = {full[a], full[a + 1], full[a + 9], full[a + 10]};
                for (int i = 3; i > 0; i--) {       /* shuffled, then the first 2-4 of them */
                    int j = (int)(rnd(&rng) % (uint32_t)(i + 1));
                    uint8_t t = four[i];
                    four[i] = four[j];
                    four[j] = t;
                }
                n[q] = (uint8_t)(2 + rnd(&rng) % 3);
                memcpy(dig[q], four, 4);
            }
            if (!gen_geom_quad(at, n, (const uint8_t (*)[4])dig, nq, jig)) {
                continue;
            }
        } else if (sky) {                           /* a grid, then some of its 18 clues */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint8_t clues[18];
            int keep = level >= GEN_HARD ? 60 : 75;     /* % of the lines with a clue */
            for (int lu = 0; lu < 18; lu++) {
                int cells[9];
                line_cells(lu, cells);
                clues[lu] = (int)(rnd(&rng) % 100) < keep ? (uint8_t)sky_seen(full, cells) : 0;
            }
            gen_geom_sky(clues, jig);
        } else if (xsum) {                          /* a grid, then some of its 18 clues */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint8_t clues[18];
            int keep = level >= GEN_HARD ? 60 : 75;     /* % of the lines with a clue */
            for (int lu = 0; lu < 18; lu++) {
                clues[lu] = (int)(rnd(&rng) % 100) < keep ? (uint8_t)sudoku_xsum_of(full, lu) : 0;
            }
            gen_geom_xsum(clues, jig);
        } else if (frame) {                         /* a grid, then some of its 18 clues */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint8_t clues[18];
            int keep = level >= GEN_HARD ? 60 : 75;     /* % of the lines with a clue */
            for (int lu = 0; lu < 18; lu++) {
                int cells[9];
                line_cells(lu, cells);
                clues[lu] = (int)(rnd(&rng) % 100) < keep ? (uint8_t)(full[cells[0]] + full[cells[1]] + full[cells[2]]) : 0;
            }
            gen_geom_frame(clues, jig);
        } else if (outs) {                          /* a grid, then 1-3 of each line's first three digits */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint16_t sets[18];
            int keep = level >= GEN_HARD ? 70 : 85;     /* % of the lines with a clue */
            for (int lu = 0; lu < 18; lu++) {
                int cells[9];
                line_cells(lu, cells);
                sets[lu] = 0;
                if ((int)(rnd(&rng) % 100) >= keep) {
                    continue;
                }
                uint8_t three[3] = {full[cells[0]], full[cells[1]], full[cells[2]]};
                for (int i = 2; i > 0; i--) {       /* shuffled, then the first 1-3 of them */
                    int j = (int)(rnd(&rng) % (uint32_t)(i + 1));
                    uint8_t t = three[i];
                    three[i] = three[j];
                    three[j] = t;
                }
                int n = 1 + (int)(rnd(&rng) % 3);
                for (int k = 0; k < n; k++) {
                    sets[lu] |= (uint16_t)(1u << three[k]);
                }
            }
            gen_geom_outside(sets, jig);
        } else if (ross) {                          /* a grid, then every line's arrow (or none) */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint8_t dirs[18];
            for (int lu = 0; lu < 18; lu++) {
                int cells[9];
                line_cells(lu, cells);
                int a = full[cells[0]], b = full[cells[1]], c = full[cells[2]];
                dirs[lu] = a < b && b < c ? ROSSINI_UP : a > b && b > c ? ROSSINI_DOWN : ROSSINI_NONE;
            }
            gen_geom_rossini(dirs, jig);
        } else if (pal || rsum || zip || betw || grp) {   /* a grid, then lines that keep their rule on it */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            int want = level >= GEN_HARD ? 10 : 8;
            bool got = pal ? palindrome_paths(full, &rng, want, jig) : rsum ? regionsum_paths(full, &rng, want, jig)
                       : zip ? zipper_paths(full, &rng, want, jig) : betw ? between_paths(full, &rng, want, jig)
                       : group_paths(variant, full, &rng, want, jig);
            if (!got) {
                continue;
            }
        } else if (lk) {                            /* a grid, then sums along some diagonals */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            bool top[LK_MAX];
            uint8_t idx[LK_MAX], dir[LK_MAX], sum[LK_MAX];
            /* a clue sits on the line between two cells, half a cell before its diagonal's first cell
             * (above the columns: 0..9, left of the rows: 10..19): one clue per place */
            bool spot_used[20] = {false}, dr_used[17] = {false}, ad_used[17] = {false};
            int n = 0, want = level >= GEN_HARD ? 10 : 8;
            for (int tries = 0; tries < 200 && n < want; tries++) {
                bool t = rnd(&rng) & 1;
                int i = (int)(rnd(&rng) % 9), d = t ? (rnd(&rng) & 1 ? LK_DOWN_RIGHT : LK_DOWN_LEFT)
                                                  : (rnd(&rng) & 1 ? LK_DOWN_RIGHT : LK_UP_RIGHT);
                int r = t ? 0 : i, c = t ? i : 0;
                if (!t && i == 0 && d == LK_DOWN_RIGHT) {
                    continue;    /* (that one is the top's) */
                }
                int len = d == LK_DOWN_RIGHT ? 9 - (r > c ? r : c) : d == LK_DOWN_LEFT ? c + 1 : r + 1;
                int line = d == LK_DOWN_RIGHT ? c - r + 8 : r + c;  /* which diagonal */
                bool *used = d == LK_DOWN_RIGHT ? dr_used : ad_used;
                int spot = (t ? 0 : 10) + i + (d == LK_DOWN_RIGHT ? 0 : 1);
                if (len < 3 || spot_used[spot] || used[line]) {
                    continue;
                }
                int s = 0;
                for (int dr = d == LK_UP_RIGHT ? -1 : 1, dc = d == LK_DOWN_LEFT ? -1 : 1;
                        r >= 0 && r < 9 && c >= 0 && c < 9; r += dr, c += dc) {
                    s += full[r * 9 + c];
                }
                spot_used[spot] = used[line] = true;
                top[n] = t;
                idx[n] = (uint8_t)i;
                dir[n] = (uint8_t)d;
                sum[n++] = (uint8_t)s;
            }
            if (!gen_geom_lk(n, top, idx, dir, sum, jig)) {
                continue;
            }
        } else if (lines) {                         /* a grid, then lines that keep their rule on it */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!rule_paths(variant, full, &rng, level >= GEN_HARD ? 10 : 8, jig)) {
                continue;
            }
        } else if (thermo) {                        /* a grid, then thermometers along rising digits */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!thermo_paths(variant == VAR_SLOW, full, &rng, level >= GEN_HARD ? 10 : 7, jig)) {
                continue;
            }
        } else if (sandwich) {                      /* a grid, then its 18 sums */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint8_t sums[18];
            for (int lu = 0; lu < 18; lu++) {
                int p1 = 0, p9 = 0, cells[9];
                for (int k = 0; k < 9; k++) {
                    cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9);
                    if (full[cells[k]] == 1) {
                        p1 = k;
                    }
                    if (full[cells[k]] == 9) {
                        p9 = k;
                    }
                }
                int a = p1 < p9 ? p1 : p9, b = p1 < p9 ? p9 : p1, sum = 0;
                for (int k = a + 1; k < b; k++) {
                    sum += full[cells[k]];
                }
                sums[lu] = (uint8_t)sum;
            }
            gen_geom_sandwich(sums, jig);
        } else if (variant == VAR_KNIGHT || variant == VAR_KING || variant == VAR_NONCONSEC || variant == VAR_DIAGNC) {
            /* not every fill works out quickly: try again */
            if (!gen_random_full_grid(G, full, &rng, 20000)) {
                continue;
            }
        } else {
            gen_random_full_grid(G, full, &rng, 0);
        }
        dig(G, full, cand, level, &rng);
        int g = sudoku_grade(G, cand);
        if (g < 0) {
            continue;
        }
        /* closest to the level asked for, preferring the easier side */
        if (best < 0 || (g <= (int)level && (g > best || best > (int)level)) ||
                (best > (int)level && g < best)) {
            best = g;
            memcpy(givens, cand, 81);
            if (board) {
                *board = *G;
            }
        }
        if (g == (int)level) {
            break;
        }
    }
    if (got_level) {
        *got_level = best < 0 ? level : (gen_level_t)best;
    }
    return best >= 0;
}

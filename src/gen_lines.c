/**
 * @file gen_lines.c
 * @ingroup generator
 * @brief Generating the lines of the line variants for a solution: thermometers, arrows, Renban, Whispers,
 *        palindromes, Region Sum, Zipper, Between, Entropic, Modular, Parity, Slow Thermo.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---------------------------------------------------------------- Thermo thermometers */

/* Thermometers over a solved grid: from random cells, a path grows through
 * neighbours (diagonals too) whose digit is higher, 3 to 6 cells long. */
/* (gen_rule_paths() keeps Dutch Whispers and Parity lines from crossing with these, defined further down) */
static bool is_line_crossing(const uint8_t *path, int n, int a, int b);
static bool is_block_crossed(const uint8_t blocks[81], int a, int b);
static void mark_line_blocks(uint8_t blocks[81], const uint8_t *path, int n);

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

/* (documented in gen_internal.h) */
bool gen_between_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
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
 * @brief Lockout Lines, Double Arrow: does a line's start, its first len cells, keep the rule on the
 * full grid with its last cell as the other end?
 * @param[in] lockout  Lockout (else Double Arrow)
 * @param[in] full  the solution
 * @param[in] on  the line's cells
 * @param[in] len  how many of them
 * @return true if so
 */
static bool is_ends_line(bool lockout, const uint8_t full[81], const uint8_t *on, int len)
{
    const int a = full[on[0]], b = full[on[len - 1]], lo = a < b ? a : b, hi = a < b ? b : a;
    int sum = 0;
    bool outside = true;
    for (int k = 1; k < len - 1; k++) {
        sum += full[on[k]];
        outside &= full[on[k]] < lo || full[on[k]] > hi;
    }
    return lockout ? hi - lo >= 4 && outside : sum == a + b;
}

/* (documented in gen_internal.h) */
bool gen_ends_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    const bool lockout = variant == VAR_LOCKOUT, ten = variant == VAR_TEN, avg = variant == VAR_AVG;
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 3000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = ten ? 3 + (int)(rnd(rng) % 6) : 3 + (int)(rnd(rng) % 4);
        int len = 0, best = 0, nb[8], sum = 0, run = 0;  /* (10 Lines: run, the open run's sum) */
        if (used[c]) {
            continue;
        }
        cells[n][len++] = (uint8_t)c;
        run = full[c];
        while (len < target) {
            int cur = cells[n][len - 1], k = line_neighbours(cur, used, cells[n], len, nb), m = 0;
            if (!lockout && !ten && !avg && sum + (len > 1 ? full[cur] : 0) > 17) {
                break;                              /* (Double Arrow: two circles make 17 at most) */
            }
            for (int j = 0; j < k; j++) {
                if (!is_line_crossing(cells[n], len, cur, nb[j]) && !is_block_crossed(blocks, cur, nb[j]) &&
                        (!ten || run + full[nb[j]] <= 10)) {   /* (10 Lines: the run can't pass 10) */
                    nb[m++] = nb[j];
                }
            }
            if (!m) {
                break;
            }
            int o = nb[rnd(rng) % (uint32_t)m];
            sum += len > 1 ? full[cells[n][len - 1]] : 0;   /* (the cells between: all but the first and o) */
            cells[n][len++] = (uint8_t)o;
            if (ten) {
                run = run + full[o] == 10 ? 0 : run + full[o];
                if (!run) {
                    best = len;                     /* every run so far makes 10 */
                }
            } else if (avg) {                       /* the circle the average of the rest so far */
                int rest = 0;
                for (int j = 1; j < len; j++) {
                    rest += full[cells[n][j]];
                }
                best = len >= 3 && rest == full[c] * (len - 1) ? len : best;
            } else if (len >= 3 && is_ends_line(lockout, full, cells[n], len)) {
                best = len;                         /* o as the other end */
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
    return n >= 3 && gen_geom_lines(variant, cells, lens, n, out);
}

/* (documented in gen_internal.h) */
bool gen_pill_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 3000 && n < want; tries++) {
        const int a = (int)(rnd(rng) % 81), down = (int)(rnd(rng) & 1), b = down ? a + 9 : a + 1;
        if ((down ? a >= 72 : a % 9 == 8) || used[a] || used[b]) {
            continue;
        }
        const int num = 10 * full[a] + full[b];
        if (num > 39) {
            continue;                               /* (a shaft of up to 7 cells, without many 9s) */
        }
        int len = 0, sum = 0, nb[8];
        cells[n][len++] = (uint8_t)a;
        cells[n][len++] = (uint8_t)b;
        while (len < 9 && sum < num) {              /* the shaft, from the pill's second cell */
            const int cur = cells[n][len - 1];
            int k = line_neighbours(cur, used, cells[n], len, nb), m = 0;
            for (int j = 0; j < k; j++) {
                if (!is_line_crossing(cells[n], len, cur, nb[j]) && !is_block_crossed(blocks, cur, nb[j]) &&
                        sum + full[nb[j]] <= num && (len > 2 || nb[j] != a)) {
                    nb[m++] = nb[j];
                }
            }
            if (!m) {
                break;
            }
            const int o = nb[rnd(rng) % (uint32_t)m];
            cells[n][len++] = (uint8_t)o;
            sum += full[o];
        }
        if (sum != num) {
            continue;
        }
        for (int j = 0; j < len; j++) {
            used[cells[n][j]] = 1;
        }
        mark_line_blocks(blocks, cells[n], len);
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && gen_geom_lines(VAR_PILL, cells, lens, n, out);
}

/* (documented in gen_internal.h) */
bool gen_group_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
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

/* (documented in gen_internal.h) */
bool gen_palindrome_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
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

/* (documented in gen_internal.h) */
bool gen_regionsum_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
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

/* (documented in gen_internal.h) */
bool gen_rule_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0}, blocks[81] = {0};
    /* (Dutch Whispers, Parity: lines that never cross, as the newer line variants; the older ones' puzzles stay) */
    const bool uncrossed = variant == VAR_DUTCH || variant == VAR_PARITY || variant == VAR_NABNER || variant == VAR_SEQ;
    int n = 0;
    for (int tries = 0; tries < 2000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81);
        int target = variant == VAR_ARROW ? 2 + (int)(rnd(rng) % 3)          /* circle + 1-3 */
                     : variant == VAR_RENBAN ? 3 + (int)(rnd(rng) % 3) : variant == VAR_PARITY ? 3 + (int)(rnd(rng) % 6)
                     : variant == VAR_NABNER || variant == VAR_SEQ ? 3 + (int)(rnd(rng) % 3) : 3 + (int)(rnd(rng) % 4);
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
                    } else if (variant == VAR_NABNER) {   /* 2 or more away from every digit on it so far */
                        ok = true;
                        for (int k = 0; k < len && ok; k++) {
                            ok = abs(x - full[cells[n][k]]) >= 2;
                        }
                    } else if (variant == VAR_SEQ) {   /* the first step sets it (not 0), the rest keep it */
                        ok = len == 1 ? x != full[c] : x - full[cur] == full[cells[n][1]] - full[c];
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

/* (documented in gen_internal.h) */
bool gen_thermo_paths(bool slow, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
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

/* (documented in gen_internal.h) */
bool gen_zipper_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
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

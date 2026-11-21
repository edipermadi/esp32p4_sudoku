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

/* one attempt: clues taken away in random symmetric gen_pairs (a cell and its
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
    const uint32_t node_limit = G->variant == VAR_CLASSIC ? 0 : G->variant == VAR_JIGSAW ? 4000 : 6000;
    for (int k = 0; k < no && clues > stop; k++) {
        sudoku_gen_yield();
        int a = order[k], b = (n - 1 - a / 9) * 9 + (n - 1 - a % 9);
        uint8_t va = out[a], vb = out[b];
        out[a] = out[b] = 0;
        /* (marks and circles: when the logic alone solves it within max_tier, it has one
         * solution, no count needed: Greater-Than's counting is slow. Only for these, so
         * the other variants' puzzles for a seed, the daily ones among them, stay the same.) */
        bool ok;
        if (G->edge_kind || G->nquads) {            /* (Expert too: count only what the logic can't solve) */
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

/* is region `reg` of the map one connected piece (4-neighbours)? */
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

/* A random Jigsaw map: the 3x3 boxes, reshaped by trading cells across
 * region borders (a cell of A goes to B, a cell of B next to A goes to A)
 * while both regions stay in one piece. */
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

/* Random cages over a solved grid: from each cell not in a cage yet (in a
 * random order), a cage grows to 2..max_size cells through neighbours whose
 * digit isn't in it yet. A lone cell left over joins a neighbouring cage
 * that can take its digit, if there is one. */
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
/* Arrow, Renban, Whispers: up to want lines drawn over the full grid as random walks (side by
 * side or diagonal, not crossing), each kept only if the grid keeps its rule: an arrow's shaft
 * (1-3 cells) adds up to its circle, a Renban (3-5 cells) is a run of consecutive digits, along a
 * Whispers line (3-6 cells) neighbours are 5 or more apart */
static bool rule_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 2000 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81);
        int target = variant == VAR_ARROW ? 2 + (int)(rnd(rng) % 3)          /* circle + 1-3 */
                     : variant == VAR_RENBAN ? 3 + (int)(rnd(rng) % 3) : 3 + (int)(rnd(rng) % 4);
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
                    if (on) {
                        continue;
                    }
                    int x = full[o];
                    bool ok;
                    if (variant == VAR_WHISPER) {
                        ok = abs(x - full[cur]) >= 5;
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
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && gen_geom_lines(variant, cells, lens, n, out);
}

static bool thermo_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 400 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 3 + (int)(rnd(rng) % 4);
        if (used[c] || full[c] > 9 - (target - 1)) {
            continue;
        }
        int len = 0;
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
                    if (!on && full[o] > full[cur]) {
                        cand[nc++] = o;
                    }
                }
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
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && gen_geom_lines(VAR_THERMO, cells, lens, n, out);
}

/* ---------------------------------------------------------------- generating (all variants) */

bool sudoku_generate(int variant, gen_level_t level, uint32_t seed, uint8_t givens[81], sudoku_geom_t *board,
                     gen_level_t *got_level)
{
    static sudoku_geom_t *jig;                      /* a board made with its puzzle, being tried (PSRAM) */
    const bool jigsaw = variant == VAR_JIGSAW, killer = variant == VAR_KILLER;
    const bool marks = variant == VAR_EVENODD || variant == VAR_CONSEC;
    const bool thermo = variant == VAR_THERMO, sandwich = variant == VAR_SANDWICH;
    const bool lines = variant == VAR_ARROW || variant == VAR_RENBAN || variant == VAR_WHISPER;
    const bool sky = variant == VAR_SKY, lk = variant == VAR_LK;
    const bool edges = variant == VAR_KROPKI || variant == VAR_XV || variant == VAR_GT, quad = variant == VAR_QUAD;
    const bool own = jigsaw || killer || marks || thermo || sandwich || edges || quad || lines || sky || lk;
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
            if (!thermo_paths(full, &rng, level >= GEN_HARD ? 10 : 7, jig)) {
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
        } else if (variant == VAR_KNIGHT || variant == VAR_KING || variant == VAR_NONCONSEC) {
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

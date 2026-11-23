/**
 * @file gen_make.c
 * @ingroup generator
 * @brief Generating: a full grid dug down to a level, and the variants' decorations made for it
 *        (Jigsaw regions, Killer cages: gen_regions.c; lines: gen_lines.c).
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
    if (G->variant == VAR_KILLER && level == GEN_MEDIUM) {
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
                                  G->line_kind == LINE_ENTROPIC || G->line_kind == LINE_MODULAR ||
                                  G->line_kind == LINE_NABNER || G->line_kind == LINE_LOCKOUT ||
                                  G->line_kind == LINE_DARROW || G->line_kind == LINE_TEN);
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
                       variant == VAR_PARITY || variant == VAR_NABNER;
    const bool sky = variant == VAR_SKY, lk = variant == VAR_LK, xsum = variant == VAR_XSUM;
    const bool pal = variant == VAR_PALINDROME, rsum = variant == VAR_REGIONSUM, frame = variant == VAR_FRAME;
    const bool zip = variant == VAR_ZIPPER, betw = variant == VAR_BETWEEN;
    const bool ends = variant == VAR_LOCKOUT || variant == VAR_DARROW || variant == VAR_TEN;
    const bool sumless = variant == VAR_SUMLESS, dcons = variant == VAR_DIAGCONSEC;
    const bool grp = variant == VAR_ENTROPIC || variant == VAR_MODULAR;
    const bool outs = variant == VAR_OUTSIDE, ross = variant == VAR_ROSSINI;
    const bool edges = variant == VAR_KROPKI || variant == VAR_XV || variant == VAR_GT, quad = variant == VAR_QUAD;
    const bool fort = variant == VAR_FORTRESS;
    const bool nroom = variant == VAR_NROOMS, skysum = variant == VAR_SKYSUM;
    const bool own = jigsaw || killer || marks || thermo || sandwich || edges || quad || lines || sky || lk || xsum || fort ||
                     pal || rsum || frame || zip || betw || grp || outs || ross || nroom || skysum || ends || sumless || dcons;
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
                gen_jigsaw_map(map, &rng);
                sudoku_geom_jigsaw(map, jig);
            }
            if (!gen_random_full_grid(G, full, &rng, 20000)) {   /* no quick fill: another shape */
                t += 3 - t % 4;
                continue;
            }
        } else if (killer) {                        /* a grid, then cages drawn over it */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!gen_killer_cages(full, level >= GEN_HARD ? 5 : 3, &rng, jig)) {
                continue;
            }
        } else if (sumless) {                       /* a grid, then cages without sums */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!gen_sumless_cages(full, &rng, jig)) {
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
        } else if (dcons) {                         /* a grid, then a mark on every corner pair 1 apart */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            char text[82];
            for (int c = 0; c < 81; c++) {
                int m = 0;
                if (c < 72 && c % 9 < 8 && abs(full[c] - full[c + 10]) == 1) {
                    m |= DMARK_DR;
                }
                if (c < 72 && c % 9 > 0 && abs(full[c] - full[c + 8]) == 1) {
                    m |= DMARK_DL;
                }
                text[c] = (char)('0' + m);
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
        } else if (fort) {                          /* a grid, then grey cells, each bigger than its white neighbours */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            char text[82];
            uint8_t order[81];
            memset(text, '.', 81);
            text[81] = 0;
            for (int c = 0; c < 81; c++) {
                order[c] = (uint8_t)c;
            }
            for (int c = 80; c > 0; c--) {          /* the cells in a random order */
                int j = (int)(rnd(&rng) % (uint32_t)(c + 1));
                uint8_t x = order[c];
                order[c] = order[j];
                order[j] = x;
            }
            /* (turning a white cell grey only drops the signs its grey neighbours had with it: the
             * others stay kept) */
            const int want = 18 + (int)(rnd(&rng) % 7);
            for (int k = 0, got = 0; k < 81 && got < want; k++) {
                const int c = order[k];
                bool ok = true;
                const int nb[4] = {c % 9 ? c - 1 : -1, c % 9 < 8 ? c + 1 : -1, c > 8 ? c - 9 : -1, c < 72 ? c + 9 : -1};
                for (int j = 0; j < 4; j++) {
                    ok &= nb[j] < 0 || text[nb[j]] == 'f' || full[nb[j]] < full[c];
                }
                if (ok) {
                    text[c] = 'f';
                    got++;
                }
            }
            sudoku_geom_marks(VAR_FORTRESS, text, jig);
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
        } else if (nroom || skysum) {               /* a grid, then some of its 18 clues */
            if (!gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0)) {
                continue;
            }
            uint8_t clues[18];
            int keep = level >= GEN_HARD ? 60 : 75;     /* % of the lines with a clue */
            for (int lu = 0; lu < 18; lu++) {
                int c = nroom ? sudoku_nroom_of(full, lu) : sudoku_skysum_of(full, lu);
                clues[lu] = (int)(rnd(&rng) % 100) < keep ? (uint8_t)c : 0;
            }
            if (!(nroom ? gen_geom_nroom(clues, jig) : gen_geom_skysum(clues, jig))) {
                continue;
            }
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
        } else if (pal || rsum || zip || betw || grp || ends) {   /* a grid, then lines that keep their rule on it */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            int want = level >= GEN_HARD ? 10 : 8;
            bool got = pal ? gen_palindrome_paths(full, &rng, want, jig) : rsum ? gen_regionsum_paths(full, &rng, want, jig)
                       : zip ? gen_zipper_paths(full, &rng, want, jig) : betw ? gen_between_paths(full, &rng, want, jig)
                       : ends ? gen_ends_paths(variant, full, &rng, want, jig) : gen_group_paths(variant, full, &rng, want, jig);
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
            if (!gen_rule_paths(variant, full, &rng, level >= GEN_HARD ? 10 : 8, jig)) {
                continue;
            }
        } else if (thermo) {                        /* a grid, then thermometers along rising digits */
            gen_random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!gen_thermo_paths(variant == VAR_SLOW, full, &rng, level >= GEN_HARD ? 10 : 7, jig)) {
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
        } else if (variant == VAR_KNIGHT || variant == VAR_KING || variant == VAR_NONCONSEC || variant == VAR_DIAGNC ||
                   variant == VAR_ARGYLE) {
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

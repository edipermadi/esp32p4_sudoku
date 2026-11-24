/*
 * PC-side tests: the parts of the firmware that are plain C (no LVGL, no ESP-IDF),
 * built with the host's compiler (tests/Makefile, `make -C tests`):
 *
 *   src/gen_*.c       geometry, solver, grader, generator, hints, layout texts
 *   src/puzzles_data.c the 1600 built-in puzzles
 *   src/sdx.c         the puzzle line format (.sdx import / export, Send by sound)
 *   src/<name>_core.c the screens' plain-C halves: the rules check, hints' words, sharing, the daily puzzle
 *
 * Each test prints one line; any failed check prints where and the run ends with a
 * non-zero status. The "golden" values pin what a seed produces, so a change to the
 * generator that would change the daily puzzles (made from the date) shows up here.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "generator.h"
#include "gen_bits.h"
#include "canon_core.h"
#include "search_core.h"
#include "learn_core.h"
#include "puzzles.h"
#include "sdx.h"
#include "game_core.h"
#include "hint_core.h"
#include "share_core.h"
#include "daily_core.h"
#include "import_parse.h"
#include "saver_rain_core.h"
#include "saver_attractor_core.h"
#include "saver_boids_core.h"
#include "saver_lissajous_core.h"
#include "saver_pendulum_core.h"
#include "saver_automata_core.h"
#include "saver_automata3d_core.h"
#include "saver_stars_core.h"

static int s_checks, s_failed;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        s_checks++;                                                                   \
        if (!(cond)) {                                                                \
            s_failed++;                                                               \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                             \
    } while (0)
#define CHECKF(cond, ...)                                                             \
    do {                                                                              \
        s_checks++;                                                                   \
        if (!(cond)) {                                                                \
            s_failed++;                                                               \
            printf("    FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                \
            printf(__VA_ARGS__);                                                      \
            printf("\n");                                                             \
        }                                                                             \
    } while (0)

/* ---------------------------------------------------------------- helpers */

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--) {
        h = (h ^ *p++) * 16777619u;
    }
    return h;
}

/* every active unit of the board holds 1..n once each, and the givens are kept */
static bool is_solved_ok(const sudoku_geom_t *g, const uint8_t givens[81], const uint8_t sol[81])
{
    for (int i = 0; i < g->nunits; i++) {
        int u = g->unit_ids[i];
        unsigned seen = 0;
        for (int k = 0; k < g->n; k++) {
            int c = g->unit[u][k], d = sol[c];
            if (d < 1 || d > g->n || (seen >> d & 1)) {
                return false;
            }
            seen |= 1u << d;
        }
    }
    for (int c = 0; c < 81; c++) {
        if (givens[c] && givens[c] != sol[c]) {
            return false;
        }
        if (!g->used[c] && sol[c]) {
            return false;
        }
    }
    for (int k = 0; k < g->ncells; k++) {           /* peers differ (Anti-Knight, Anti-King too) */
        int c = g->cells[k];
        for (int p = 0; p < g->npeers[c]; p++) {
            if (sol[g->peers[c][p]] == sol[c]) {
                return false;
            }
        }
    }
    for (int k = 0; g->variant == VAR_ARGYLE && k < ARGYLE_LINES; k++) {   /* Argyle: no repeat on a line */
        uint8_t line[9];
        unsigned seen = 0;
        for (int i = 0, n = sudoku_argyle_line(k, line); i < n; i++) {
            if (seen >> sol[line[i]] & 1) {
                return false;
            }
            seen |= 1u << sol[line[i]];
        }
    }
    for (int c = 0; g->variant == VAR_FORTRESS && c < 81; c++) {   /* Fortress: grey beats a white neighbour */
        for (int k = 0; k < 2; k++) {
            int o = k ? c + 9 : c + 1;
            if ((!k && c % 9 == 8) || o > 80 || !g->parity[c] == !g->parity[o]) {
                continue;
            }
            if ((g->parity[c] ? sol[c] < sol[o] : sol[c] > sol[o])) {
                return false;
            }
        }
    }
    for (int c = 0; g->consec_on && c < 81; c++) {  /* Consecutive, Non-Consecutive: bar = 1 apart */
        if (c % 9 < 8 && (abs(sol[c] - sol[c + 1]) == 1) != ((g->consec[c] & CONSEC_RIGHT) != 0)) {
            return false;
        }
        if (c < 72 && (abs(sol[c] - sol[c + 9]) == 1) != ((g->consec[c] & CONSEC_DOWN) != 0)) {
            return false;
        }
    }
    for (int c = 0; g->edge_kind && c < 81; c++) {  /* Kropki, XV, Greater-Than: the marks (and none) */
        if (c % 9 < 8 && !is_sudoku_edge_ok(g->edge_kind, EDGE_RIGHT(g->edge[c]), sol[c], sol[c + 1])) {
            return false;
        }
        if (c < 72 && !is_sudoku_edge_ok(g->edge_kind, EDGE_DOWN(g->edge[c]), sol[c], sol[c + 9])) {
            return false;
        }
    }
    for (int t = 0; t < g->nthermos; t++) {         /* the lines: Thermo, Arrow, Renban, Whispers, Palindrome, Region Sum */
        int n = g->thermo_n[t], lo = 10, hi = 0, sum = 0;
        unsigned seen = 0;
        for (int i = 0; i < n; i++) {
            int x = sol[g->thermo_cell[t][i]];
            if (i && g->line_kind == LINE_THERMO && x <= sol[g->thermo_cell[t][i - 1]]) {
                return false;
            }
            if (i && g->line_kind == LINE_WHISPER && abs(x - sol[g->thermo_cell[t][i - 1]]) < 5) {
                return false;
            }
            if (i && g->line_kind == LINE_DUTCH && abs(x - sol[g->thermo_cell[t][i - 1]]) < 4) {
                return false;
            }
            if (i && g->line_kind == LINE_PARITY && !((x ^ sol[g->thermo_cell[t][i - 1]]) & 1)) {
                return false;
            }
            if (i && g->line_kind == LINE_SLOW && x < sol[g->thermo_cell[t][i - 1]]) {
                return false;
            }
            for (int j = 0; g->line_kind == LINE_NABNER && j < i; j++) {   /* Nabner: every two 2 or more apart */
                if (abs(x - sol[g->thermo_cell[t][j]]) < 2) {
                    return false;
                }
            }
            if (g->line_kind == LINE_RENBAN && (seen >> x & 1)) {
                return false;
            }
            seen |= 1u << x;
            if (x < lo) {
                lo = x;
            }
            if (x > hi) {
                hi = x;
            }
            if (i) {
                sum += x;
            }
        }
        if (g->line_kind == LINE_RENBAN && hi - lo != n - 1) {
            return false;
        }
        if (g->line_kind == LINE_ARROW && sum != sol[g->thermo_cell[t][0]]) {
            return false;
        }
        for (int i = 0; g->line_kind == LINE_PALINDROME && i < n / 2; i++) {
            if (sol[g->thermo_cell[t][i]] != sol[g->thermo_cell[t][n - 1 - i]]) {
                return false;
            }
        }
        for (int i = 0; g->line_kind == LINE_ZIPPER && i < n / 2; i++) {   /* one total: the middle (odd) or the middle pair's */
            int tot = n % 2 ? sol[g->thermo_cell[t][n / 2]] : sol[g->thermo_cell[t][n / 2 - 1]] + sol[g->thermo_cell[t][n / 2]];
            if (sol[g->thermo_cell[t][i]] + sol[g->thermo_cell[t][n - 1 - i]] != tot) {
                return false;
            }
        }
        for (int i = 0; (g->line_kind == LINE_ENTROPIC || g->line_kind == LINE_MODULAR) && i + 2 < n; i++) {
            int seen = 0;                           /* any three in a row: three groups */
            for (int j = i; j < i + 3; j++) {
                seen |= 1 << sudoku_line_group(g->line_kind, sol[g->thermo_cell[t][j]]);
            }
            if (seen != 7) {
                return false;
            }
        }
        if (g->line_kind == LINE_BETWEEN) {         /* strictly between the end circles */
            int a = sol[g->thermo_cell[t][0]], b = sol[g->thermo_cell[t][n - 1]], lo2 = a < b ? a : b, hi2 = a < b ? b : a;
            for (int i = 1; i < n - 1; i++) {
                int x = sol[g->thermo_cell[t][i]];
                if (x <= lo2 || x >= hi2) {
                    return false;
                }
            }
        }
        if (g->line_kind == LINE_AVG) {             /* the circle is the others' average */
            int sum = 0;
            for (int i = 1; i < n; i++) {
                sum += sol[g->thermo_cell[t][i]];
            }
            if (sum != sol[g->thermo_cell[t][0]] * (n - 1)) {
                return false;
            }
        }
        if (g->line_kind == LINE_PILL) {            /* the pill's number is the shaft's sum */
            int sum = 0;
            for (int i = 2; i < n; i++) {
                sum += sol[g->thermo_cell[t][i]];
            }
            if (sum != 10 * sol[g->thermo_cell[t][0]] + sol[g->thermo_cell[t][1]]) {
                return false;
            }
        }
        if (g->line_kind == LINE_TEN) {             /* runs of 10, end to end */
            int run = 0;
            for (int i = 0; i < n; i++) {
                run += sol[g->thermo_cell[t][i]];
                run = run == 10 ? 0 : run;
                if (run > 10) {
                    return false;
                }
            }
            if (run) {
                return false;
            }
        }
        if (g->line_kind == LINE_LOCKOUT || g->line_kind == LINE_DARROW) {   /* the two ends' rule */
            int a = sol[g->thermo_cell[t][0]], b = sol[g->thermo_cell[t][n - 1]], lo2 = a < b ? a : b, hi2 = a < b ? b : a;
            int sum = 0;
            for (int i = 1; i < n - 1; i++) {
                int x = sol[g->thermo_cell[t][i]];
                sum += x;
                if (g->line_kind == LINE_LOCKOUT && x >= lo2 && x <= hi2) {
                    return false;
                }
            }
            if (g->line_kind == LINE_LOCKOUT ? hi2 - lo2 < 4 : sum != a + b) {
                return false;
            }
        }
        if (g->line_kind == LINE_REGIONSUM) {
            int part[9], np;
            bool full[9];
            np = sudoku_regionsum_parts(g, t, sol, part, full);
            for (int j = 1; j < np; j++) {
                if (part[j] != part[0]) {
                    return false;
                }
            }
            if (np < 2) {
                return false;
            }
        }
    }
    for (int lu = 0; (g->outside || g->rossini) && lu < 18; lu++) {   /* Outside, Rossini: the first three */
        int x[3];
        for (int k = 0; k < 3; k++) {
            x[k] = sol[lu < 9 ? lu * 9 + k : k * 9 + lu - 9];
        }
        if (g->outside && (g->outside_set[lu] & ~((1u << x[0]) | (1u << x[1]) | (1u << x[2])))) {
            return false;
        }
        bool up = x[0] < x[1] && x[1] < x[2], down = x[0] > x[1] && x[1] > x[2];
        if (g->rossini && (g->rossini_dir[lu] == ROSSINI_UP ? !up : g->rossini_dir[lu] == ROSSINI_DOWN ? !down : up || down)) {
            return false;
        }
    }
    for (int k = 0; g->magic && k < 3; k++) {       /* Magic Square: the centre box's lines */
        const int r = (3 + k) * 9 + 3, c = 3 * 9 + 3 + k;
        if (sol[r] + sol[r + 1] + sol[r + 2] != 15 || sol[c] + sol[c + 9] + sol[c + 18] != 15) {
            return false;
        }
    }
    if (g->magic && (sol[30] + sol[40] + sol[50] != 15 || sol[32] + sol[40] + sol[48] != 15)) {
        return false;
    }
    for (int dg = 0; g->antidiag && dg < 2; dg++) {   /* Anti-Diagonal: three different digits at most */
        uint16_t have = 0;
        for (int k = 0; k < 9; k++) {
            have |= (uint16_t)(1u << sol[dg ? k * 9 + 8 - k : k * 10]);
        }
        if (__builtin_popcount(have) > 3) {
            return false;
        }
    }
    for (int lu = 0; g->psky && lu < 18; lu++) {    /* Odd/Even Skyscrapers: the clues */
        const int c = g->psky_clue[lu];
        if (c && sudoku_psky_of(sol, lu, (c & PSKY_EVEN) != 0) != (c & ~PSKY_EVEN)) {
            return false;
        }
    }
    for (int r = 0; g->idx159 && r < 9; r++) {     /* 159 Sudoku: column k says where the k is */
        for (int k = 1; k <= 9; k += 4) {
            if (sol[r * 9 + sol[r * 9 + k - 1] - 1] != k) {
                return false;
            }
        }
    }
    for (int c = 0; g->diag_nc && c < 81; c++) {    /* Diagonal (Non-)Consecutive: corners 1 apart just where marked */
        if (c % 9 < 8 && c < 72 && (abs(sol[c] - sol[c + 10]) == 1) != ((g->dmark[c] & DMARK_DR) != 0)) {
            return false;
        }
        if (c % 9 > 0 && c < 72 && (abs(sol[c] - sol[c + 8]) == 1) != ((g->dmark[c] & DMARK_DL) != 0)) {
            return false;
        }
    }
    for (int lu = 0; g->frame && lu < 18; lu++) {   /* Frame: the first three digits' sums */
        int cells[9];
        for (int k = 0; k < 9; k++) {
            cells[k] = lu < 9 ? lu * 9 + k : k * 9 + lu - 9;
        }
        if (g->frame_sum[lu] && sol[cells[0]] + sol[cells[1]] + sol[cells[2]] != g->frame_sum[lu]) {
            return false;
        }
    }
    for (int lu = 0; g->xsums && lu < 18; lu++) {   /* X-Sums: the first X digits' sums */
        if (g->xsum[lu] && sudoku_xsum_of(sol, lu) != g->xsum[lu]) {
            return false;
        }
    }
    for (int lu = 0; g->nrooms && lu < 18; lu++) {   /* Numbered Rooms: the X-th cell's digit */
        int cells[9];
        for (int k = 0; k < 9; k++) {
            cells[k] = lu < 9 ? lu * 9 + k : k * 9 + lu - 9;
        }
        if (g->nroom[lu] && sol[cells[sol[cells[0]] - 1]] != g->nroom[lu]) {
            return false;
        }
    }
    for (int lu = 0; g->skysum && lu < 18; lu++) {   /* Sum Skyscrapers: the buildings seen, added up */
        int sum = 0, top = 0;
        for (int k = 0; g->sky_sum[lu] && k < 9; k++) {
            int x = sol[lu < 9 ? lu * 9 + k : k * 9 + lu - 9];
            if (x > top) {
                top = x;
                sum += x;
            }
        }
        if (g->sky_sum[lu] && sum != g->sky_sum[lu]) {
            return false;
        }
    }
    for (int lu = 0; g->skyscraper && lu < 18; lu++) {   /* Skyscraper: the buildings seen */
        int seen = 0, top = 0;
        for (int k = 0; g->sky[lu] && k < 9; k++) {
            int x = sol[lu < 9 ? lu * 9 + k : k * 9 + lu - 9];
            if (x > top) {
                top = x;
                seen++;
            }
        }
        if (g->sky[lu] && seen != g->sky[lu]) {
            return false;
        }
    }
    for (int k = 0; k < g->nlk; k++) {              /* Little Killer: the diagonals' sums */
        int sum = 0;
        for (int j = 0; j < g->lk_n[k]; j++) {
            sum += sol[g->lk_cell[k][j]];
        }
        if (sum != g->lk_sum[k]) {
            return false;
        }
    }
    for (int q = 0; q < g->nquads; q++) {           /* Quadruple: every circle's digits are there */
        int empty;
        if (sudoku_quad_missing(g, q, sol, &empty) != 0) {
            return false;
        }
    }
    for (int k = 0; k < g->ncages; k++) {           /* Killer: the cage sums */
        int sum = 0;
        for (int j = 0; j < g->cage_n[k]; j++) {
            sum += sol[g->cage_cell[k][j]];
        }
        if (g->cage_sum[k] && sum != g->cage_sum[k]) {   /* (Sumless Killer: none) */
            return false;
        }
        int prod = 1;
        for (int j = 0; j < g->cage_n[k]; j++) {
            prod *= sol[g->cage_cell[k][j]];
        }
        if (g->cage_prod[k] && prod != g->cage_prod[k]) {   /* Product Killer */
            return false;
        }
        for (int i = 0; i < g->cage_n[k]; i++) {   /* and no repeat in a cage */
            for (int j = 0; j < i; j++) {
                if (sol[g->cage_cell[k][i]] == sol[g->cage_cell[k][j]]) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* givens only on the board's cells, digits 1..n */
static bool is_givens_ok(const sudoku_geom_t *g, const uint8_t givens[81])
{
    for (int c = 0; c < 81; c++) {
        if (givens[c] > g->n) {
            return false;
        }
        if (givens[c] && !g->used[c]) {
            return false;
        }
    }
    return true;
}

/* the parts of two boards a puzzle's layout decides */
static bool is_same_layout(const sudoku_geom_t *a, const sudoku_geom_t *b)
{
    return a->variant == b->variant && a->n == b->n && !memcmp(a->region, b->region, 81) &&
           a->ncages == b->ncages && !memcmp(a->cage_of, b->cage_of, 81) &&
           !memcmp(a->cage_sum, b->cage_sum, sizeof(a->cage_sum)) && !memcmp(a->parity, b->parity, 81) &&
           !memcmp(a->consec, b->consec, 81) && a->nthermos == b->nthermos &&
           !memcmp(a->thermo_of, b->thermo_of, 81) && !memcmp(a->thermo_pos, b->thermo_pos, 81) &&
           a->skyscraper == b->skyscraper && !memcmp(a->sky, b->sky, 18) && a->nlk == b->nlk &&
           a->xsums == b->xsums && !memcmp(a->xsum, b->xsum, 18) && a->frame == b->frame &&
           !memcmp(a->frame_sum, b->frame_sum, 18) && a->outside == b->outside &&
           !memcmp(a->outside_set, b->outside_set, sizeof(a->outside_set)) && a->rossini == b->rossini &&
           !memcmp(a->rossini_dir, b->rossini_dir, 18) && a->diag_nc == b->diag_nc && !memcmp(a->dmark, b->dmark, 81) &&
           a->idx159 == b->idx159 && a->antidiag == b->antidiag && a->magic == b->magic && a->psky == b->psky &&
           !memcmp(a->psky_clue, b->psky_clue, 18) && !memcmp(a->cage_prod, b->cage_prod, sizeof(a->cage_prod)) &&
           !memcmp(a->lk_sum, b->lk_sum, a->nlk) && !memcmp(a->lk_dir, b->lk_dir, a->nlk) &&
           !memcmp(a->lk_cell, b->lk_cell, sizeof(a->lk_cell[0]) * a->nlk) &&
           a->line_kind == b->line_kind && a->sandwich == b->sandwich && !memcmp(a->sandwich_sum, b->sandwich_sum, 18) &&
           a->edge_kind == b->edge_kind && !memcmp(a->edge, b->edge, 81) && a->nquads == b->nquads &&
           !memcmp(a->quad_at, b->quad_at, a->nquads) && !memcmp(a->quad_n, b->quad_n, a->nquads) &&
           !memcmp(a->quad_dig, b->quad_dig, sizeof(a->quad_dig[0]) * a->nquads) && a->nrooms == b->nrooms &&
           !memcmp(a->nroom, b->nroom, 18) && a->skysum == b->skysum && !memcmp(a->sky_sum, b->sky_sum, 18);
}

/* the layout texts of a board, as the game keeps them (sudoku_db's generated table) */
static void layout_texts(const sudoku_geom_t *g, char regions[82], char *cages, char *marks, char *thermos, int len)
{
    regions[0] = cages[0] = marks[0] = thermos[0] = 0;
    switch (g->variant) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) {
            regions[i] = (char)('0' + g->region[i]);
        }
        regions[81] = 0;
        break;
    case VAR_KILLER:
    case VAR_SUMLESS:
    case VAR_PRODUCT:
    case VAR_REGIONS:
        sudoku_cages_text(g, cages, len);
        break;
    case VAR_QUAD:
        sudoku_quads_text(g, cages, len);
        break;
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
    case VAR_FORTRESS:
    case VAR_DIAGCONSEC:
        sudoku_marks_text(g, marks, len);
        break;
    case VAR_SANDWICH:
        sudoku_sandwich_text(g, marks, len);
        break;
    case VAR_SKY:
        sudoku_sky_text(g, marks, len);
        break;
    case VAR_XSUM:
        sudoku_xsum_text(g, marks, len);
        break;
    case VAR_NROOMS:
        sudoku_nroom_text(g, marks, len);
        break;
    case VAR_SKYSUM:
        sudoku_skysum_text(g, marks, len);
        break;
    case VAR_PSKY:
        sudoku_psky_text(g, marks, len);
        break;
    case VAR_FRAME:
        sudoku_frame_text(g, marks, len);
        break;
    case VAR_OUTSIDE:
        sudoku_outside_text(g, marks, len);
        break;
    case VAR_ROSSINI:
        sudoku_rossini_text(g, marks, len);
        break;
    case VAR_LK:
        sudoku_lk_text(g, cages, len);
        break;
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
        sudoku_thermos_text(g, thermos, len);
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- tests */

/* canon_core: the symmetry table against every board's peers, the hash the same turned, mirrored and
 * renumbered, and different for a different puzzle */
static void test_canon(void)
{
    for (int v = 0; v < VAR_COUNT; v++) {
        const uint8_t sym = canon_symmetries(v);
        if (!sym) {
            continue;
        }
        const sudoku_geom_t *g = sudoku_geom(v);
        uint8_t real = 0;                           /* the turns that map the board's peers onto themselves */
        for (int t = 0; t < 8; t++) {
            bool ok = true;
            for (int a = 0; a < 81 && ok; a++) {
                for (int b = 0; b < 81 && ok; b++) {
                    if (a == b || a % 9 >= g->n || a / 9 >= g->n || b % 9 >= g->n || b / 9 >= g->n) {
                        continue;
                    }
                    ok = is_sudoku_geom_peer(g, a, b) ==
                         is_sudoku_geom_peer(g, canon_cell(t, g->n, a), canon_cell(t, g->n, b));
                }
            }
            real |= ok ? (uint8_t)(1u << t) : 0;
        }
        CHECKF(sym == real, "%s: canon_symmetries 0x%02x, the board's 0x%02x", variant_name(v), sym, real);
        uint8_t giv[81] = {0}, sol[81];
        gen_level_t got;
        static sudoku_geom_t board;
        if (!sudoku_generate(v, GEN_EASY, 4242u + (uint32_t)v, giv, &board, &got)) {
            continue;
        }
        const uint64_t h = canon_hash(v, giv);
        uint32_t rng = 99u + (uint32_t)v;
        for (int t = 0; t < 8; t++) {               /* every allowed turn, renumbered at random: the same */
            if (!(sym >> t & 1)) {
                continue;
            }
            uint8_t perm[10] = {0}, tr[81] = {0};
            for (int d = 1; d <= g->n; d++) {
                perm[d] = (uint8_t)d;
            }
            for (int d = g->n; d > 1; d--) {
                rng = rng * 1103515245u + 12345u;
                const int j = 1 + (int)(rng >> 16) % d;
                const uint8_t x = perm[d];
                perm[d] = perm[j];
                perm[j] = x;
            }
            for (int c = 0; c < 81; c++) {
                if (c % 9 < g->n && c / 9 < g->n) {
                    tr[canon_cell(t, g->n, c)] = perm[giv[c]];
                }
            }
            CHECKF(canon_hash(v, tr) == h, "%s: turn %d renumbered: another hash", variant_name(v), t);
        }
        CHECK(sudoku_solve(&board, giv, sol));     /* one more given: another puzzle, another hash */
        for (int c = 0; c < 81; c++) {
            if (!giv[c] && c % 9 < g->n && c / 9 < g->n) {
                giv[c] = sol[c];
                break;
            }
        }
        CHECKF(canon_hash(v, giv) != h, "%s: a given more, the same hash", variant_name(v));
    }
    CHECK(canon_hash(VAR_SANDWICH, (const uint8_t[81]) {0}) == 0);   /* (no hash: its clues aren't in the givens) */
    int dup = 0;                                     /* the built-in puzzles: how many are turned copies */
    static uint64_t seen[PUZZLE_COUNT];
    for (int i = 0; i < PUZZLE_COUNT; i++) {
        uint8_t cells[81];
        puzzle_load((uint16_t)i, cells);
        seen[i] = canon_hash(VAR_CLASSIC, cells);
        CHECKF(puzzle_canon((uint16_t)i) == seen[i], "puzzle %d: puzzles_canon.c stale (make -C tests canon)", i);
        for (int j = 0; j < i; j++) {
            dup += seen[j] == seen[i];
        }
    }
    printf("    (built-in puzzles that are another one turned, mirrored or renumbered: %d)\n", dup);
}

/* search_core: every word somewhere, any order, any case */
static void test_search(void)
{
    CHECK(is_search_match("gnome 2 easy", "Gnome 2 Easy #14 Classic"));
    CHECK(is_search_match("EASY gnome", "Gnome 2 Easy #14 Classic"));
    CHECK(is_search_match("  killer   hard ", "Killer Hard #12 Killer Hard"));
    CHECK(is_search_match("#14", "Gnome 2 Easy #14 Classic"));
    CHECK(is_search_match("x-sud", "X-Sudoku Hard #3 X-Sudoku Hard"));
    CHECK(!is_search_match("gnome 3", "Gnome 2 Easy #14 Classic"));   /* (every word must be there) */
    CHECK(!is_search_match("killer expert", "Killer Hard #12 Killer Hard"));
    CHECK(!is_search_match("", "Gnome 2 Easy #14 Classic"));          /* (no words: nothing) */
    CHECK(!is_search_match("   ", "Classic"));
    CHECK(is_search_match("daily", "Daily X-Sudoku Hard, 18 Nov X-Sudoku Hard"));
}

/* gen_bits.h's tables: each entry as the builtins say */
static void test_bits(void)
{
    int bad = 0;
    for (unsigned m = 0; m < 1024; m++) {
        bad += gen_popcount(m) != __builtin_popcount(m);
        bad += m && gen_lowest(m) != __builtin_ctz(m);
        bad += m && gen_highest(m) != 31 - __builtin_clz(m);
    }
    CHECKF(bad == 0, "gen_bits.h: %d entries wrong", bad);
}

static void test_geometry(void)
{
    static const struct { int variant, n, ncells, nunits, peers0; } G[] = {
        {VAR_CLASSIC, 9, 81, 27, 20}, {VAR_X, 9, 81, 29, 26}, {VAR_HYPER, 9, 81, 31, 20},
        {VAR_MINI, 6, 36, 18, 12},     {VAR_KIDS, 4, 16, 12, 7},
        {VAR_KING, 9, 81, 27, 20},     {VAR_NONCONSEC, 9, 81, 27, 20}, {VAR_DISJOINT, 9, 81, 36, 24},
        {VAR_ASTERISK, 9, 81, 28, 20}, {VAR_CENTERDOT, 9, 81, 28, 20}, {VAR_GIRANDOLA, 9, 81, 28, 26},
        {VAR_159, 9, 81, 27, 20}, {VAR_ANTIDIAG, 9, 81, 27, 20},
        {VAR_CHESS, 9, 81, 27, 20},     {VAR_MAGIC, 9, 81, 27, 20},
    };
    for (size_t i = 0; i < sizeof(G) / sizeof(G[0]); i++) {
        const sudoku_geom_t *g = sudoku_geom(G[i].variant);
        CHECKF(g->n == G[i].n, "%s n %d", variant_name(G[i].variant), g->n);
        CHECKF(g->ncells == G[i].ncells, "%s ncells %d", variant_name(G[i].variant), g->ncells);
        CHECKF(g->nunits == G[i].nunits, "%s nunits %d", variant_name(G[i].variant), g->nunits);
        CHECKF(g->npeers[0] == G[i].peers0, "%s peers of cell 0: %d", variant_name(G[i].variant), g->npeers[0]);
        for (int k = 0; k < g->ncells; k++) {       /* boxes / regions: n cells each */
            int c = g->cells[k], cnt = 0;
            for (int j = 0; j < g->ncells; j++) {
                cnt += g->region[g->cells[j]] == g->region[c];
            }
            CHECK(cnt == g->n);
        }
    }
    /* X: the corner sees its diagonal too (6 beyond its box); Hyper: cell (1,1) its window (3 more) */
    CHECKF(sudoku_geom(VAR_HYPER)->npeers[10] == 23, "Hyper peers of cell 10: %d", sudoku_geom(VAR_HYPER)->npeers[10]);
    CHECK(is_sudoku_geom_on_diag(sudoku_geom(VAR_X), 80) && !is_sudoku_geom_on_diag(sudoku_geom(VAR_X), 1));
    CHECK(strcmp(variant_name(VAR_KIDS), "Kids 4x4") == 0);
    /* Anti-King: cell 2 (row 0, column 2) touches (1, 3) diagonally, outside its box */
    /* Chess Sudoku: r5c5 sees 20 in its units and 8 knight's moves (its 4 corners are in its box); r4c4 20, 6 knight's
     * moves outside its box and 3 corners */
    CHECKF(sudoku_geom(VAR_CHESS)->npeers[30] == 29, "Chess Sudoku peers of cell 30: %d", sudoku_geom(VAR_CHESS)->npeers[30]);
    CHECKF(sudoku_geom(VAR_CHESS)->npeers[40] == 28, "Chess Sudoku peers of cell 40: %d", sudoku_geom(VAR_CHESS)->npeers[40]);
    CHECKF(sudoku_geom(VAR_KING)->npeers[2] == 21, "Anti-King peers of cell 2: %d", sudoku_geom(VAR_KING)->npeers[2]);
    CHECK(is_sudoku_geom_peer(sudoku_geom(VAR_KING), 2, 12) && !is_sudoku_geom_peer(sudoku_geom(VAR_CLASSIC), 2, 12));
    /* Disjoint: cell 0's group is the top-left cell of every box */
    CHECK(is_sudoku_geom_peer(sudoku_geom(VAR_DISJOINT), 0, 30) && is_sudoku_geom_peer(sudoku_geom(VAR_DISJOINT), 0, 60));
    /* Asterisk: its cell 13 sees the six others it shares no row, column or box with */
    CHECKF(sudoku_geom(VAR_ASTERISK)->npeers[13] == 26, "Asterisk peers of cell 13: %d",
           sudoku_geom(VAR_ASTERISK)->npeers[13]);
    CHECK(is_sudoku_geom_tinted(sudoku_geom(VAR_ASTERISK), 40) && !is_sudoku_geom_tinted(sudoku_geom(VAR_ASTERISK), 41));
    /* Center Dot: the centre of box 1 (cell 10) sees the eight other centres, four of them new */
    CHECKF(sudoku_geom(VAR_CENTERDOT)->npeers[10] == 24, "Center Dot peers of cell 10: %d",
           sudoku_geom(VAR_CENTERDOT)->npeers[10]);
    CHECK(is_sudoku_geom_peer(sudoku_geom(VAR_CENTERDOT), 10, 70) && !is_sudoku_geom_peer(sudoku_geom(VAR_CLASSIC), 10, 70));
    CHECK(is_sudoku_geom_tinted(sudoku_geom(VAR_CENTERDOT), 40) && !is_sudoku_geom_tinted(sudoku_geom(VAR_CENTERDOT), 41));
    CHECK(sudoku_line_group(LINE_ENTROPIC, 3) == 0 && sudoku_line_group(LINE_ENTROPIC, 4) == 1 &&
          sudoku_line_group(LINE_MODULAR, 4) == 1 && sudoku_line_group(LINE_MODULAR, 9) == 0);
    CHECK(sudoku_geom(VAR_NONCONSEC)->consec_on && strcmp(variant_name(VAR_NONCONSEC), "Non-Consecutive") == 0);
    CHECK(sudoku_geom(VAR_KIDS)->box_w == 2 && sudoku_geom(VAR_KIDS)->box_h == 2);
    /* X-Sums: the first digits a clue allows (1: just 1; 2: none; 3: 2 (2 + 1); 45: just 9) */
    CHECK(sudoku_xsum_firsts(1) == 1u << 1 && sudoku_xsum_firsts(2) == 0 && sudoku_xsum_firsts(3) == 1u << 2);
    CHECK(sudoku_xsum_firsts(45) == 1u << 9 && (sudoku_xsum_firsts(10) >> 3 & 1) && !(sudoku_xsum_firsts(10) >> 1 & 1));
    static sudoku_geom_t b;
    CHECK(!sudoku_geom_xsum_text("2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", &b));   /* (2 can't be made) */
    CHECK(sudoku_geom_xsum_text("1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,45", &b) && b.allow[0] == 1u << 1 &&
          b.allow[8] == 1u << 9);                   /* row 1 starts with 1; column 9 with 9 */
    uint8_t v[81] = {0};
    v[0] = 3;
    v[1] = 4;
    v[2] = 2;
    CHECK(sudoku_xsum_of(v, 0) == 9 && sudoku_xsum_of(v, 9) == 0);
    /* Palindrome: the ends of 0,10,20 hold one digit, so they mustn't see each other (0 and 20 share a box) */
    CHECK(!sudoku_geom_lines_text(VAR_PALINDROME, "0,10,20", &b));
    CHECK(!sudoku_geom_lines_text(VAR_PALINDROME, "0,10,20,30", &b));   /* (10 and 20: one box) */
    CHECK(sudoku_geom_lines_text(VAR_PALINDROME, "2,12,22", &b) && b.line_kind == LINE_PALINDROME);
    memset(v, 0, 81);
    v[2] = 7;                                       /* its first cell: its last is 7 too */
    CHECK(is_sudoku_fits(&b, v, 22, 7) && !is_sudoku_fits(&b, v, 22, 5) && is_sudoku_fits(&b, v, 12, 5));
    /* Region Sum Lines: in one box only isn't a line; two boxes' parts add up the same */
    CHECK(!sudoku_geom_lines_text(VAR_REGIONSUM, "0,1,2", &b));
    CHECK(sudoku_geom_lines_text(VAR_REGIONSUM, "1,2,3,4", &b) && b.line_kind == LINE_REGIONSUM);
    memset(v, 0, 81);
    v[1] = 2;
    v[2] = 6;
    v[3] = 5;
    int part[9];
    bool full[9];
    CHECK(sudoku_regionsum_parts(&b, 0, v, part, full) == 2 && part[0] == 8 && full[0] && part[1] == 5 && !full[1]);
    CHECK(is_sudoku_fits(&b, v, 4, 3) && !is_sudoku_fits(&b, v, 4, 4));
    /* Zipper: 2,12,22 (odd): r1c3 + r3c5 = r2c4; with a 7 in the middle and a 3 at one end, the other is 4 */
    CHECK(sudoku_geom_lines_text(VAR_ZIPPER, "2,12,22", &b) && b.line_kind == LINE_ZIPPER && !(b.allow[12] >> 1 & 1));
    memset(v, 0, 81);
    v[12] = 7;
    v[2] = 3;
    CHECK(is_sudoku_fits(&b, v, 22, 4) && !is_sudoku_fits(&b, v, 22, 5));
    /* Between: circles 0 and 2, r1c2 between them; with a 3 and an 8 at the ends, 4..7 */
    CHECK(!sudoku_geom_lines_text(VAR_BETWEEN, "0,1", &b));
    CHECK(sudoku_geom_lines_text(VAR_BETWEEN, "0,10,2", &b));
    memset(v, 0, 81);
    v[0] = 3;
    v[2] = 8;
    CHECK(is_sudoku_fits(&b, v, 10, 5) && !is_sudoku_fits(&b, v, 10, 9) && !is_sudoku_fits(&b, v, 10, 2));
    memset(v, 0, 81);
    v[0] = 8;
    v[10] = 9;                                      /* the far circle can't put a 9 between them */
    CHECK(!is_sudoku_fits(&b, v, 2, 1));
    /* Entropic: 0,10,20,30: r1c1 a 2 (low), r2c2 a 5 (middle): r3c3 high, r4c4 low like r1c1 */
    CHECK(!sudoku_geom_lines_text(VAR_ENTROPIC, "0,10", &b));
    CHECK(sudoku_geom_lines_text(VAR_ENTROPIC, "0,10,20,30", &b));
    memset(v, 0, 81);
    v[0] = 2;
    v[10] = 5;
    CHECK(is_sudoku_fits(&b, v, 20, 8) && !is_sudoku_fits(&b, v, 20, 4) && !is_sudoku_fits(&b, v, 20, 1));
    CHECK(is_sudoku_fits(&b, v, 30, 3) && !is_sudoku_fits(&b, v, 30, 6) && !is_sudoku_fits(&b, v, 30, 9));
    /* Modular: the same line, r1c1 a 2 (2 5 8), r2c2 a 4 (1 4 7): r3c3 is 3, 6 or 9 */
    CHECK(sudoku_geom_lines_text(VAR_MODULAR, "0,10,20,30", &b));
    v[10] = 4;
    CHECK(is_sudoku_fits(&b, v, 20, 6) && !is_sudoku_fits(&b, v, 20, 7) && is_sudoku_fits(&b, v, 30, 5) &&
          !is_sudoku_fits(&b, v, 30, 3));
    /* Argyle: its lines (8, 5, 8, 5 cells each way); r1c2 and r8c9 share a line (and no row, column or box) */
    uint8_t al[9];
    CHECK(sudoku_argyle_line(0, al) == 8 && al[0] == 1 && al[7] == 71 && sudoku_argyle_line(1, al) == 5 &&
          al[4] == 44 && sudoku_argyle_line(4, al) == 8 && al[0] == 7 && al[7] == 63 && sudoku_argyle_line(7, al) == 5);
    memset(v, 0, 81);
    v[1] = 6;
    CHECK(!is_sudoku_fits(sudoku_geom(VAR_ARGYLE), v, 71, 6) && is_sudoku_fits(sudoku_geom(VAR_CLASSIC), v, 71, 6));
    /* Fortress: r1c1 grey, r1c2 white: r1c1 beats it, never a 1; r1c2 never a 9; two greys: no sign */
    char fm[82];
    memset(fm, '.', 81);
    fm[81] = 0;
    fm[0] = fm[9] = 'f';
    CHECK(sudoku_geom_marks(VAR_FORTRESS, fm, &b) && b.edge_kind == EDGE_FORT && EDGE_RIGHT(b.edge[0]) == MARK_MORE &&
          EDGE_DOWN(b.edge[0]) == MARK_NONE && !(b.allow[0] >> 1 & 1) && !(b.allow[1] >> 9 & 1));
    memset(v, 0, 81);
    v[1] = 6;
    CHECK(is_sudoku_fits(&b, v, 0, 7) && !is_sudoku_fits(&b, v, 0, 5) && is_sudoku_fits(&b, v, 9, 5));
    char fm2[82];
    CHECK(sudoku_marks_text(&b, fm2, sizeof(fm2)) && !strcmp(fm, fm2));
    /* Dutch Whispers: r1c1 a 5 leaves r1c2 1 or 9 (German: nothing); a 3 leaves 7, 8, 9 */
    CHECK(sudoku_geom_lines_text(VAR_DUTCH, "0,1,2", &b) && b.line_kind == LINE_DUTCH && (b.allow[1] >> 5 & 1));
    memset(v, 0, 81);
    v[0] = 5;
    CHECK(is_sudoku_fits(&b, v, 1, 1) && is_sudoku_fits(&b, v, 1, 9) && !is_sudoku_fits(&b, v, 1, 2) &&
          !is_sudoku_fits(&b, v, 1, 8));
    v[0] = 3;
    CHECK(is_sudoku_fits(&b, v, 1, 7) && !is_sudoku_fits(&b, v, 1, 6));
    /* Parity: r1c1 a 4: r1c2 odd, then r1c3 even */
    CHECK(sudoku_geom_lines_text(VAR_PARITY, "0,1,2", &b) && b.line_kind == LINE_PARITY);
    memset(v, 0, 81);
    v[0] = 4;
    CHECK(is_sudoku_fits(&b, v, 1, 7) && !is_sudoku_fits(&b, v, 1, 6));
    v[1] = 7;
    CHECK(is_sudoku_fits(&b, v, 2, 2) && !is_sudoku_fits(&b, v, 2, 3));
    /* Slow Thermo: 18,28,38 crosses from box 1 to box 5 at r3c1-r4c2 (no shared row, column or box: may repeat),
     * then r4c2-r5c3 share box 4 (must rise); so r3c1 is 1-8, r5c3 2-9 */
    CHECK(sudoku_geom_lines_text(VAR_SLOW, "18,28,38", &b) && b.line_kind == LINE_SLOW);
    CHECK(!(b.allow[18] >> 9 & 1) && (b.allow[18] >> 8 & 1) && !(b.allow[38] >> 1 & 1) && (b.allow[38] >> 2 & 1));
    memset(v, 0, 81);
    v[18] = 4;
    CHECK(is_sudoku_fits(&b, v, 28, 4) && is_sudoku_fits(&b, v, 28, 6) && !is_sudoku_fits(&b, v, 28, 3));
    v[28] = 4;
    CHECK(!is_sudoku_fits(&b, v, 38, 4) && is_sudoku_fits(&b, v, 38, 5));
    CHECK(sudoku_geom_lines_text(VAR_THERMO, "18,28,38", &b));   /* (Thermo: rises at every step) */
    memset(v, 0, 81);
    v[18] = 4;
    CHECK(!is_sudoku_fits(&b, v, 28, 4));
    /* Outside: row 1's first three hold 3 and 7; with r1c1 = 3 and r1c2 = 5, r1c3 is 7; 7 isn't further on */
    CHECK(!sudoku_geom_outside_text("1234,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", &b));
    CHECK(sudoku_geom_outside_text("37,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", &b) && !(b.allow[5] >> 7 & 1) &&
          (b.allow[2] >> 7 & 1));
    memset(v, 0, 81);
    v[0] = 3;
    v[1] = 5;
    CHECK(is_sudoku_fits(&b, v, 2, 7) && !is_sudoku_fits(&b, v, 2, 8));
    /* Rossini: row 1 rising: r1c1 at most 7, r1c3 at least 3; with 2 and 6 in r1c1, r1c3, r1c2 is 3 to 5 */
    CHECK(sudoku_geom_rossini_text("1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", &b) && !(b.allow[0] >> 8 & 1) &&
          !(b.allow[2] >> 2 & 1));
    memset(v, 0, 81);
    v[0] = 2;
    v[2] = 6;
    CHECK(is_sudoku_fits(&b, v, 1, 4) && !is_sudoku_fits(&b, v, 1, 7) && !is_sudoku_fits(&b, v, 1, 1));
    /* (no arrow on column 1: 2, 4 down it can't be followed by a 5..9 in r3c1) */
    memset(v, 0, 81);
    v[0] = 2;
    v[9] = 4;
    CHECK(!is_sudoku_fits(&b, v, 18, 7) && is_sudoku_fits(&b, v, 18, 1));
    /* Diagonal Non-Consecutive: a 5 in r2c2 keeps 4 and 6 out of r1c1, not out of r1c2 */
    memset(v, 0, 81);
    v[10] = 5;
    CHECK(!is_sudoku_fits(sudoku_geom(VAR_DIAGNC), v, 0, 4) && is_sudoku_fits(sudoku_geom(VAR_DIAGNC), v, 1, 4) &&
          is_sudoku_fits(sudoku_geom(VAR_CLASSIC), v, 0, 4));
    /* Frame: 6 is 1 + 2 + 3, 24 is 7 + 8 + 9; 5 and 25 can't be made */
    CHECK(!sudoku_geom_frame_text("5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", &b));
    CHECK(sudoku_geom_frame_text("6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,24", &b) && b.allow[0] == 0xE &&
          b.allow[1] == 0xE && b.allow[26] == 0x380);
}

static void test_builtin(void)
{
    uint8_t g[81], sol[81];
    int unique = 0;
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    for (int id = 0; id < PUZZLE_COUNT; id++) {
        puzzle_load((uint16_t)id, g);
        int n = sudoku_count_solutions(G, g, 2);
        CHECKF(n == 1, "puzzle %d has %d solutions", id, n);
        if (n != 1) {
            continue;
        }
        unique++;
        CHECKF(sudoku_solve(G, g, sol) && is_solved_ok(G, g, sol), "puzzle %d: solve", id);
        CHECKF(is_sudoku_classic_unique(g), "puzzle %d: classic_unique", id);
    }
    CHECK(unique == PUZZLE_COUNT);
    CHECK(puzzle_collection_of(0) == 0 && puzzle_collection_of(PUZZLE_COUNT - 1) >= 0);
}

/* lines that cross: two diagonal steps (of one line or two) through the same 2x2 block, making an X */
static bool is_lines_crossing(const sudoku_geom_t *g)
{
    uint8_t blocks[81] = {0};
    for (int t = 0; t < g->nthermos; t++) {
        for (int i = 0; i + 1 < g->thermo_n[t]; i++) {
            int a = g->thermo_cell[t][i], b = g->thermo_cell[t][i + 1];
            if (a / 9 == b / 9 || a % 9 == b % 9) {
                continue;
            }
            int k = (a / 9 < b / 9 ? a / 9 : b / 9) * 9 + (a % 9 < b % 9 ? a % 9 : b % 9);
            if (blocks[k]) {
                return true;
            }
            blocks[k] = 1;
        }
    }
    return false;
}

/* every variant: a puzzle at Easy and Medium (and Hard where it's quick), one solution, its
 * level what the grader says, givens on the board; Kids 4x4 never above Medium */
static void test_generate(void)
{
    static sudoku_geom_t board;
    uint8_t g[81], sol[81];
    for (int v = 0; v < VAR_COUNT; v++) {
        int top = v == VAR_KIDS ? GEN_HARD : (v == VAR_CLASSIC || v == VAR_X || v == VAR_MINI) ? GEN_HARD : GEN_MEDIUM;
        for (int lv = GEN_EASY; lv <= top; lv++) {
            gen_level_t got;
            uint32_t seed = 1000u + (uint32_t)v * 17u + (uint32_t)lv;
            bool ok = sudoku_generate(v, (gen_level_t)lv, seed, g, &board, &got);
            CHECKF(ok, "%s %s: generated", variant_name(v), gen_level_name(lv));
            if (!ok) {
                continue;
            }
            CHECKF(board.variant == v, "%s: board variant %d", variant_name(v), board.variant);
            if (v == VAR_ZIPPER || v == VAR_BETWEEN || v == VAR_ENTROPIC || v == VAR_MODULAR || v == VAR_DUTCH ||
                    v == VAR_PARITY || v == VAR_SLOW || v == VAR_NABNER || v == VAR_LOCKOUT || v == VAR_DARROW ||
                    v == VAR_TEN || v == VAR_PILL || v == VAR_AVG) {
                CHECKF(!is_lines_crossing(&board), "%s %s: two lines cross", variant_name(v), gen_level_name(lv));
            }
            CHECKF(is_givens_ok(&board, g), "%s %s: givens on the board", variant_name(v), gen_level_name(lv));
            CHECKF(sudoku_count_solutions(&board, g, 2) == 1, "%s %s: one solution", variant_name(v),
                   gen_level_name(lv));
            CHECKF(sudoku_solve(&board, g, sol) && is_solved_ok(&board, g, sol), "%s %s: solution valid",
                   variant_name(v), gen_level_name(lv));
            int graded = sudoku_grade(&board, g);
            CHECKF(graded == (int)got, "%s %s: graded %d, generator said %d", variant_name(v),
                   gen_level_name(lv), graded, (int)got);
            if (v == VAR_KIDS)
                CHECKF(got <= GEN_MEDIUM, "Kids 4x4 %s came out %s", gen_level_name(lv),
                       gen_level_name(got));
            if (v != VAR_KIDS && lv < GEN_HARD)
                CHECKF((int)got == lv, "%s %s came out %s", variant_name(v),
                       gen_level_name(lv), gen_level_name(got));
        }
    }
}

/* the same seed, the same puzzle (the daily puzzle is made from the date); golden hashes
 * pin today's generator: if one changes on purpose, every board's daily puzzles change too */
static void test_determinism(void)
{
    static const struct { int variant, level; uint32_t seed, hash; } GOLD[] = {
        {VAR_CLASSIC, GEN_EASY, 20261120u, 0x440f8c01u},     /* (pinned 2026-11-20: make -C tests golden) */
        {VAR_CLASSIC, GEN_HARD, 20261121u, 0x3c983ed9u},
        {VAR_MINI, GEN_EASY, 20261123u, 0xf339879au},
        {VAR_KILLER, GEN_EASY, 20261124u, 0xf22d9532u},
        {VAR_KIDS, GEN_EASY, 42u, 0x0576ce16u},
    };
    static sudoku_geom_t b1, b2;
    for (size_t i = 0; i < sizeof(GOLD) / sizeof(GOLD[0]); i++) {
        uint8_t g1[81], g2[81];
        gen_level_t l1, l2;
        sudoku_generate(GOLD[i].variant, (gen_level_t)GOLD[i].level, GOLD[i].seed, g1, &b1, &l1);
        sudoku_generate(GOLD[i].variant, (gen_level_t)GOLD[i].level, GOLD[i].seed, g2, &b2, &l2);
        CHECKF(!memcmp(g1, g2, 81) && l1 == l2 && is_same_layout(&b1, &b2), "%s seed %u: not the same twice",
               variant_name(GOLD[i].variant), GOLD[i].seed);
        uint8_t key[81 + 81 + 81];                  /* givens, regions, cages: the whole puzzle */
        memcpy(key, g1, 81);
        memcpy(key + 81, b1.region, 81);
        memcpy(key + 162, b1.cage_of, 81);
        uint32_t h = fnv1a(key, sizeof(key));
        if (getenv("PRINT_GOLDEN"))
            printf("    golden %s %s seed %u: 0x%08x\n", variant_name(GOLD[i].variant),
                   gen_level_name(GOLD[i].level), GOLD[i].seed, h);
        else
            CHECKF(h == GOLD[i].hash, "%s %s seed %u: 0x%08x, pinned 0x%08x", variant_name(GOLD[i].variant),
                   gen_level_name(GOLD[i].level), GOLD[i].seed, h, GOLD[i].hash);
    }
}

/* layouts as text and back (what sudoku.db keeps), and whole puzzles through an .sdx line
 * both ways (the export / import, and Send by sound with the givens packed) */
static void test_texts_and_sdx(void)
{
    static sudoku_geom_t board, back;
    static char cages[STATS_CAGES_LEN_T], marks[STATS_CAGES_LEN_T], thermos[STATS_CAGES_LEN_T], line[4096];
    char regions[82];
    for (int v = 0; v < VAR_COUNT; v++) {
        uint8_t g[81];
        gen_level_t got;
        if (!sudoku_generate(v, GEN_EASY, 777u + (uint32_t)v, g, &board, &got)) {
            continue;
        }
        layout_texts(&board, regions, cages, marks, thermos, (int)sizeof(cages));
        for (int pack = 0; pack <= 1; pack++) {
            int len = sdx_format(v, got, g, regions, cages, marks, thermos, pack, line, sizeof(line));
            CHECKF(len > 0, "%s: sdx_format", variant_name(v));
            if (len <= 0) {
                continue;
            }
            sdx_puzzle_t p;
            CHECKF(sdx_parse(line, &p), "%s: sdx_parse (packed %d)", variant_name(v), pack);
            CHECKF(p.variant == v && p.level == (int)got && !memcmp(p.givens, g, 81), "%s: fields back (packed %d)",
                   variant_name(v), pack);
            CHECKF(sdx_geom(&p, &back) && is_same_layout(&board, &back), "%s: layout back (packed %d)",
                   variant_name(v), pack);
            CHECKF(sudoku_count_solutions(&back, p.givens, 2) == 1, "%s: still one solution", variant_name(v));
        }
    }
}

static void test_sdx_edges(void)
{
    char buf[512];
    sdx_puzzle_t p;
    const char *E1 = "379000014060010070080009005435007000090040020000800436900700080040080050850000249";
    /* an .sdm line: Classic, no level; leading spaces and a comment after are fine */
    snprintf(buf, sizeof(buf), "  %s  # Easy #1", E1);
    CHECK(sdx_parse(buf, &p) && p.variant == VAR_CLASSIC && p.level == -1 && p.givens[0] == 3 && p.givens[3] == 0);
    snprintf(buf, sizeof(buf), "%s", E1);
    buf[80] = 0;                                    /* 80 cells */
    CHECK(!sdx_parse(buf, &p));
    snprintf(buf, sizeof(buf), "%s1", E1);          /* 82 */
    CHECK(!sdx_parse(buf, &p));
    /* names or numbers, any case; "-" or nothing: no level */
    snprintf(buf, sizeof(buf), "killer|HARD|%s", E1);
    CHECK(sdx_parse(buf, &p) && p.variant == VAR_KILLER && p.level == GEN_HARD);
    snprintf(buf, sizeof(buf), "11|-|%s", E1);
    CHECK(sdx_parse(buf, &p) && p.variant == VAR_KIDS && p.level == -1);
    snprintf(buf, sizeof(buf), "Kids 4x4||%s", E1);
    CHECK(sdx_parse(buf, &p) && p.variant == VAR_KIDS && p.level == -1);
    snprintf(buf, sizeof(buf), "%d|0|%s", VAR_COUNT, E1);   /* no such variant */
    CHECK(!sdx_parse(buf, &p));
    snprintf(buf, sizeof(buf), "Disjoint Groups|easy|%s", E1);
    CHECK(sdx_parse(buf, &p) && p.variant == VAR_DISJOINT && p.level == GEN_EASY);
    snprintf(buf, sizeof(buf), "non-consecutive|-|%s", E1);
    CHECK(sdx_parse(buf, &p) && p.variant == VAR_NONCONSEC);
    snprintf(buf, sizeof(buf), "Classic|Impossible|%s", E1);
    CHECK(!sdx_parse(buf, &p));
    snprintf(buf, sizeof(buf), "0|0|%sx", E1);      /* a stray character */
    CHECK(!sdx_parse(buf, &p));
    /* packed givens: a-z = 1-26 empty cells, never past 81 */
    snprintf(buf, sizeof(buf), "0|0|zzzc");          /* 26 * 3 + 3 = 81 empty */
    CHECK(sdx_parse(buf, &p) && p.givens[80] == 0);
    snprintf(buf, sizeof(buf), "0|0|zzzd");          /* 82 */
    CHECK(!sdx_parse(buf, &p));
    snprintf(buf, sizeof(buf), "0|0|9zzzb");         /* a digit, then 80 empty */
    CHECK(sdx_parse(buf, &p) && p.givens[0] == 9 && p.givens[1] == 0);
    /* long runs: an empty board, and one given at each end (runs of 26 and more) */
    for (int k = 0; k < 3; k++) {
        uint8_t g[81] = {0};
        if (k >= 1) {
            g[0] = 5;
        }
        if (k == 2) {
            g[80] = 7;
        }
        char giv[82];
        CHECK(sdx_pack_givens(g, giv, sizeof(giv)) > 0);
        snprintf(buf, sizeof(buf), "0|-|%s", giv);
        CHECKF(sdx_parse(buf, &p) && !memcmp(p.givens, g, 81), "long empty runs (%d): %s", k, giv);
    }
    /* pack -> parse, for random givens */
    srand(1);
    for (int t = 0; t < 2000; t++) {
        uint8_t g[81];
        for (int i = 0; i < 81; i++) {
            g[i] = (uint8_t)(rand() % 3 ? 0 : 1 + rand() % 9);
        }
        char giv[82];
        int len = sdx_pack_givens(g, giv, sizeof(giv));
        CHECK(len > 0 && len <= 81);
        snprintf(buf, sizeof(buf), "0|-|%s", giv);
        CHECK(sdx_parse(buf, &p) && !memcmp(p.givens, g, 81));
    }
    /* a Classic puzzle by sound fits one 110-character part (Send by sound) */
    uint8_t g[81];
    puzzle_load(0, g);
    int len = sdx_format(VAR_CLASSIC, -1, g, NULL, NULL, NULL, NULL, true, buf, sizeof(buf));
    CHECKF(len > 0 && len <= 110, "Easy #1 packed: %d characters", len);
    CHECK(sdx_format(VAR_CLASSIC, -1, g, NULL, NULL, NULL, NULL, true, buf, 20) == -1);   /* too small */
    /* a jigsaw with a broken region map doesn't make a board */
    snprintf(buf, sizeof(buf), "Jigsaw|0|%s|%s", E1, "0123");
    static sudoku_geom_t geo;
    CHECK(sdx_parse(buf, &p) && !sdx_geom(&p, &geo));
}

/* the hint engine walks a puzzle to its solution, a right digit each step */
static void test_hints(void)
{
    static sudoku_geom_t board;
    uint8_t vals[81], sol[81];
    struct { int variant, level; uint32_t seed; int builtin; } CASES[] = {
        {VAR_CLASSIC, 0, 0, 0}, {VAR_CLASSIC, 0, 0, 300}, {VAR_CLASSIC, 0, 0, 1599},
        {VAR_X, GEN_MEDIUM, 5, -1}, {VAR_MINI, GEN_HARD, 6, -1}, {VAR_KIDS, GEN_MEDIUM, 7, -1},
        {VAR_KILLER, GEN_EASY, 8, -1}, {VAR_JIGSAW, GEN_EASY, 9, -1},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        const sudoku_geom_t *G;
        if (CASES[i].builtin >= 0) {
            puzzle_load((uint16_t)CASES[i].builtin, vals);
            G = sudoku_geom(VAR_CLASSIC);
        } else {
            gen_level_t got;
            sudoku_generate(CASES[i].variant, (gen_level_t)CASES[i].level, CASES[i].seed, vals, &board, &got);
            G = &board;
        }
        CHECK(sudoku_solve(G, vals, sol));
        int steps = 0;
        sudoku_step_t st;
        bool right = true;
        while (sudoku_next_step(G, vals, sol, &st) && steps < 100) {
            if (st.kind == STEP_WRONG || st.cell < 0 || st.cell > 80 || st.digit != sol[st.cell] || vals[st.cell]) {
                right = false;
                break;
            }
            vals[st.cell] = (uint8_t)st.digit;
            steps++;
        }
        CHECKF(right && !memcmp(vals, sol, 81), "%s: hints reach the solution (%d steps)",
               variant_name(CASES[i].variant), steps);
    }
}

/* ---------------------------------------------------------------- the screens' cores */

#define EXAMPLES "../docs/variant-examples.sdx"   /**< one example puzzle per variant */

/** @brief One example puzzle: its board, givens and solution. */
typedef struct {
    sdx_puzzle_t sp;
    sudoku_geom_t geo;
    uint8_t sol[81];
} example_t;

/* reads the next example from f; false at the end */
static bool example_next(FILE *f, example_t *e)
{
    static char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') {
            continue;
        }
        bool ok = sdx_parse(line, &e->sp) && sdx_geom(&e->sp, &e->geo) && sudoku_solve(&e->geo, e->sp.givens, e->sol);
        CHECKF(ok, "example reads and solves: %.40s", line);
        if (ok) {
            return true;
        }
    }
    return false;
}

/* the rules check: no conflict in the givens or the solution of any variant's example; a digit
 * changed in the solution is marked */
static void test_conflicts(void)
{
    static example_t e;
    bool out[81];
    int n = 0;
    FILE *f = fopen(EXAMPLES, "r");
    CHECK(f != NULL);
    while (f && example_next(f, &e)) {
        const char *name = variant_name(e.sp.variant);
        n++;
        CHECKF(game_conflicts(&e.geo, e.sp.givens, out) == 0, "%s: the givens break no rule", name);
        CHECKF(game_conflicts(&e.geo, e.sol, out) == 0, "%s: the solution breaks no rule", name);
        uint8_t v[81];
        memcpy(v, e.sol, 81);
        int c = e.geo.cells[0];
        v[c] = (uint8_t)(v[c] % e.geo.n + 1);          /* another digit: a repeat in its row */
        CHECKF(game_conflicts(&e.geo, v, out) > 0 && out[c], "%s: a wrong digit is marked", name);
    }
    if (f) {
        fclose(f);
    }
    CHECKF(n == VAR_COUNT, "%d examples, one per variant", n);

    /* a variant's own rule, with no repeat: Killer's cage over its sum */
    const sudoku_geom_t *K = NULL;
    f = fopen(EXAMPLES, "r");
    while (f && example_next(f, &e)) {
        if (e.sp.variant == VAR_KILLER) {
            K = &e.geo;
            break;
        }
    }
    if (f) {
        fclose(f);
    }
    CHECK(K && K->ncages > 0);
    if (K) {
        uint8_t v[81] = {0};
        for (int i = 0; i < K->cage_n[0]; i++) {
            v[K->cage_cell[0][i]] = 9;                  /* (9s alone: over any cage's sum but 9) */
        }
        if (K->cage_n[0] > 1 || K->cage_sum[0] < 9) {
            CHECK(game_conflicts(K, v, out) > 0 && out[K->cage_cell[0][0]]);
        }
    }
}

/* a hint for each example's first step: words, the cell highlighted, the button */
static void test_hint_words(void)
{
    static example_t e;
    char text[640];
    uint8_t mark[81];
    FILE *f = fopen(EXAMPLES, "r");
    CHECK(f != NULL);
    while (f && example_next(f, &e)) {
        const char *name = variant_name(e.sp.variant);
        sudoku_step_t st;
        if (!sudoku_next_step(&e.geo, e.sp.givens, e.sol, &st)) {
            continue;
        }
        const char *act = hint_explain(&e.geo, &st, 1, false, text, sizeof(text), mark);
        CHECKF(text[0] && strcmp(act, "More") == 0, "%s: the nudge", name);
        act = hint_explain(&e.geo, &st, 2, false, text, sizeof(text), mark);
        CHECKF(text[0] && strlen(text) < sizeof(text) - 1, "%s: the reason fits: %s", name, text);
        CHECKF(st.cell >= 0 && st.cell < 81 && mark[st.cell] == 2, "%s: the cell is highlighted", name);
        CHECKF(strcmp(act, st.kind == STEP_WRONG ? "Erase it" : "Place it") == 0, "%s: the button", name);
    }
    if (f) {
        fclose(f);
    }
}

/* Share: the QR text, the .sdx line read back the same, its parts and their CRC */
static void test_share(void)
{
    static example_t e;
    static char text[2048], line[2048], back[2048];
    CHECK(share_crc16("123456789", 9) == 0x29B1);     /* CRC-16/CCITT-FALSE's check value */
    FILE *f = fopen(EXAMPLES, "r");
    CHECK(f != NULL);
    while (f && example_next(f, &e)) {
        const int var = e.sp.variant;
        const char *name = variant_name(var);
        CHECKF(share_text(&e.geo, var, e.sp.givens, text, sizeof(text)), "%s: the QR text fits", name);
        if (var == VAR_CLASSIC) {
            CHECKF(strlen(text) == 81 && strspn(text, "0123456789") == 81, "Classic: 81 digits");
        } else {
            CHECKF(strncmp(text, name, strlen(name)) == 0, "%s: the QR text starts with its name", name);
        }
        int len = share_pz_line(&e.geo, var, e.sp.level, e.sp.givens, line, sizeof(line));
        CHECKF(len > 0, "%s: the line fits", name);
        if (len <= 0) {
            continue;
        }
        /* sent in parts, read back */
        int parts = (len + PZ_PART - 1) / PZ_PART;
        back[0] = 0;
        for (int i = 0; i < parts; i++) {
            char msg[176], body[176];
            share_pz_part(0x1A, i, parts, line, len, msg, sizeof(msg));
            const char *star = strrchr(msg, '*');
            CHECKF(strncmp(msg, PZ_ID "$pz ", 6) == 0 && star, "%s: part %d's frame", name, i + 1);
            if (!star) {
                continue;
            }
            size_t blen = (size_t)(star - (msg + 3));
            memcpy(body, msg + 3, blen);
            body[blen] = 0;
            CHECKF(strtoul(star + 1, NULL, 16) == share_crc16(body, blen), "%s: part %d's CRC", name, i + 1);
            unsigned id;
            int pi, pn;
            const char *data;
            CHECKF(share_pz_part_read(body + 3, &id, &pi, &pn, &data) && id == 0x1A && pi == i + 1 && pn == parts,
                   "%s: part %d reads back", name, i + 1);
            strcat(back, data);
        }
        CHECKF(strcmp(back, line) == 0, "%s: the parts make the line", name);
        sdx_puzzle_t sp;
        sudoku_geom_t g;
        CHECKF(sdx_parse(back, &sp) && sp.variant == var && memcmp(sp.givens, e.sp.givens, 81) == 0 &&
               sdx_geom(&sp, &g), "%s: the line reads back the same", name);
    }
    if (f) {
        fclose(f);
    }
    unsigned id;
    int i, n;
    const char *data;
    CHECK(!share_pz_part_read("1A 3/2 abc", &id, &i, &n, &data));   /* part past the count */
    CHECK(!share_pz_part_read("zz", &id, &i, &n, &data));
}

/* the undo history: undo and redo walk the boards back and forth, an edit drops the redo steps, the ring keeps
 * the newest UNDO_MAX */
static void test_history(void)
{
    static board_t undo[UNDO_MAX], redo[UNDO_MAX];
    game_history_t h = {.undo = undo, .redo = redo};
    board_t b;
    memset(&b, 0, sizeof(b));
    game_history_clear(&h);
    CHECK(!game_history_undo(&h, &b) && !game_history_redo(&h, &b));
    for (int i = 1; i <= 3; i++) {                  /* three edits: cell 0 holds 1, 2, 3 */
        game_history_push(&h, &b, false);
        b.val[0] = (uint8_t)i;
    }
    CHECK(game_history_undo(&h, &b) && b.val[0] == 2);
    CHECK(game_history_undo(&h, &b) && b.val[0] == 1);
    CHECK(game_history_redo(&h, &b) && b.val[0] == 2);
    CHECK(h.len == 2 && h.redo_len == 1);
    game_history_push(&h, &b, false);               /* an edit: redo is gone */
    b.val[0] = 9;
    CHECK(h.redo_len == 0 && !game_history_redo(&h, &b) && b.val[0] == 9);
    CHECK(game_history_undo(&h, &b) && b.val[0] == 2);
    game_history_clear(&h);
    for (int i = 0; i < UNDO_MAX + 10; i++) {       /* more than the ring holds: the newest stay */
        b.val[1] = (uint8_t)(i % 250);
        game_history_push(&h, &b, false);
    }
    CHECK(h.len == UNDO_MAX);
    int n = 0;
    while (game_history_undo(&h, &b)) {
        n++;
    }
    CHECK(n == UNDO_MAX && b.val[1] == (uint8_t)(10 % 250));
}

/* importing: a puzzle per line, OpenSudoku's XML, the cells' text, the file names */
static void test_import_parse(void)
{
    uint8_t c[81];
    const char *P = "53..7....6..195....98....6.8...6...34..8.3..17...2...6.6....28....419..5....8..79";
    CHECK(import_parse_cells(P, c) && c[0] == 5 && c[1] == 3 && c[2] == 0 && c[80] == 9);
    char longer[100];
    snprintf(longer, sizeof(longer), "%s5", P);
    CHECK(!import_parse_cells(longer, c));          /* 82 digits: not a puzzle */
    char bad[100];
    snprintf(bad, sizeof(bad), "%s", P);
    bad[10] = 'x';
    CHECK(!import_parse_cells(bad, c));
    static uint8_t cells[4][81];
    char path[] = "/tmp/sudoku_import_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) {
        FILE *f = fdopen(fd, "w");
        fprintf(f, "# a comment\n  %s\nnot a puzzle\n%s\n", P, P);
        fclose(f);
        CHECK(import_parse_file(path, false, cells, 4) == 2 && cells[1][0] == 5);
        CHECK(import_parse_file(path, false, cells, 1) == 1);          /* at most max */
        f = fopen(path, "w");
        fprintf(f, "<opensudoku><game data=\"%s\"/><game data='%s'/></opensudoku>\n", P, P);
        fclose(f);
        CHECK(import_parse_file(path, true, cells, 4) == 2 && cells[0][80] == 9);
        remove(path);
    }
    CHECK(import_parse_file("/nonexistent/x.sdm", false, cells, 4) == 0);
    CHECK(is_import_name_ends_with("Book.SDM", ".sdm") && !is_import_name_ends_with("sdm", ".sdm.txt"));
}

/* the daily puzzle: its seed is the date's, its name */
static void test_daily(void)
{
    char buf[48];
    CHECK(daily_seed("2026-11-23") == daily_seed("2026-11-23") && daily_seed("2026-11-23") != daily_seed("2026-11-24"));
    CHECK(daily_seed("") != 0);
    daily_kind_text(VAR_CLASSIC, GEN_HARD, buf, sizeof(buf));
    CHECK(strcmp(buf, "Hard") == 0);
    daily_kind_text(VAR_X, GEN_MEDIUM, buf, sizeof(buf));
    CHECK(strcmp(buf, "X-Sudoku Medium") == 0);
}

/* ---------------------------------------------------------------- run */

typedef struct { const char *name; void (*fn)(void); } test_t;

/* Learn: every lesson has a position (two seeds each), and it's the lesson's: the right variant's board, the
 * step's digit the solution's, a single only the variant's rule makes (or the rule's technique); every variant
 * but Classic, Mini and Kids has a lesson */
static void test_learn(void)
{
    static sudoku_geom_t board;
    static learn_found_t f;
    bool has[VAR_COUNT] = {false};
    CHECK(learn_count() > LEARN_RULES);
    for (int k = 0; k < learn_count(); k++) {
        CHECKF(learn_name(k)[0] && learn_desc(k)[0] && learn_group(k) >= 0 && learn_group(k) < LEARN_GROUPS,
               "lesson %d: name, text, tab", k);
        CHECKF((k < LEARN_RULES) == (learn_variant(k) < 0) && (k < LEARN_RULES) == (learn_group(k) == LEARN_TECHNIQUES),
               "%s: technique lessons first", learn_name(k));
        const int var = learn_variant(k);
        if (var >= 0) {
            CHECKF(!has[var], "%s: one lesson per variant", learn_name(k));
            has[var] = true;
        }
        for (uint32_t seed = 1; seed <= 2; seed++) {
            bool ok = learn_find(k, seed * 7919u, &board, &f);
            CHECKF(ok, "%s: found (seed %u)", learn_name(k), seed);
            if (!ok) {
                continue;
            }
            const sudoku_geom_t *G = var >= 0 ? &board : sudoku_geom(VAR_CLASSIC);
            uint8_t sol[81], givens[81];
            if (var >= 0) {
                memcpy(givens, f.givens, 81);
            } else {
                puzzle_load(f.puzzle, givens);
            }
            CHECKF(G->variant == (var >= 0 ? var : VAR_CLASSIC), "%s: the variant's board", learn_name(k));
            CHECKF(sudoku_solve(G, givens, sol) && f.step.cell >= 0 && f.step.cell < 81 && !f.vals[f.step.cell] &&
                   f.step.digit == sol[f.step.cell], "%s: the step is the solution's", learn_name(k));
            for (int c = 0; c < 81; c++) {
                CHECKF(!f.vals[c] || f.vals[c] == sol[c], "%s: the position is the solution's", learn_name(k));
            }
        }
    }
    for (int v = 0; v < VAR_COUNT; v++) {
        CHECKF(has[v] == (v != VAR_CLASSIC && v != VAR_MINI && v != VAR_KIDS), "%s: a lesson", variant_name(v));
    }
}

/* Rain (the screen saver): the columns start staggered and fall, a trail is the head (the brightest) and
 * dimmer cells behind it, every digit is 1..9, a column comes round again, the same seed the same rain */
static void test_rain(void)
{
    CHECK(rain_start(42));
    int above = 0;
    for (int c = 0; c < RAIN_COLS; c++) {
        above += rain_head(c) < 0;
    }
    CHECK(above == RAIN_COLS);                      /* nobody in at the start */
    bool seen_head = false, restarted = false;
    int32_t last[RAIN_COLS];
    for (int c = 0; c < RAIN_COLS; c++) {
        last[c] = rain_head(c);
    }
    for (int step = 0; step < 2000; step++) {
        rain_step();
        for (int c = 0; c < RAIN_COLS; c++) {
            int32_t h = rain_head(c);
            if (h < last[c]) {
                restarted = true;                   /* (back above the top) */
            }
            last[c] = h;
            int heads = 0, lit = 0, prev = -1;
            for (int r = 0; r < RAIN_ROWS; r++) {
                uint8_t d, lv = rain_cell(c, r, &d);
                CHECKF(d >= 1 && d <= 9, "rain: digit %d", d);
                CHECKF(lv <= RAIN_HEAD, "rain: level %d", lv);
                if (lv == RAIN_HEAD) {
                    heads++;
                    CHECKF(h >= 0 && (h >> 8) == r, "rain: the head where the column is (%d, %d)", c, r);
                    CHECKF(prev < 0 || prev < RAIN_HEAD, "rain: brighter towards the head (%d)", c);
                }
                if (lv) {
                    CHECKF(prev <= lv || prev == 0, "rain: a trail brightens down to its head (%d, %d)", c, r);
                    lit++;
                }
                prev = lv;
            }
            CHECKF(heads <= 1 && lit <= RAIN_TRAIL_MAX, "rain: one trail a column (%d)", c);
            seen_head |= heads == 1;
        }
    }
    CHECK(seen_head && restarted);
    uint8_t a[RAIN_COLS], b;                        /* the same seed, the same rain */
    CHECK(rain_start(7));
    for (int s = 0; s < 100; s++) {
        rain_step();
    }
    for (int c = 0; c < RAIN_COLS; c++) {
        rain_cell(c, 10, &a[c]);
    }
    CHECK(rain_start(7));
    for (int s = 0; s < 100; s++) {
        rain_step();
    }
    bool same = true;
    for (int c = 0; c < RAIN_COLS; c++) {
        rain_cell(c, 10, &b);
        same &= a[c] == b;
    }
    CHECK(same);
    rain_free();
    CHECK(rain_cell(0, 0, NULL) == 0);
    rain_step();                                    /* (no rain: nothing) */
}

/* Attractor (the screen saver): every one found spreads over much of its box and stays inside it, every
 * point plotted is listed for drawing, the counts stop at ATTR_MAX, and the same seed gives the same
 * shape */
static void test_attractor(void)
{
    static uint32_t dirty[50000];
    CHECK(attr_start(99));
    int kinds[ATTR_KINDS] = {0};
    for (int a = 0; a < 12; a++) {
        if (a) {
            attr_next();
        }
        kinds[attr_kind()]++;
        int n = attr_plot(50000, dirty, 50000);
        int x0, y0, x1, y1;
        attr_box(&x0, &y0, &x1, &y1);
        CHECKF(x0 >= 0 && y0 >= 0 && x1 < ATTR_W && y1 < ATTR_W && x1 > x0 && y1 > y0, "attractor %d: box", a);
        CHECKF(x1 - x0 > ATTR_W / 3 || y1 - y0 > ATTR_W / 3, "attractor %d: fills the screen (%dx%d)", a, x1 - x0, y1 - y0);
        CHECKF(n > 45000, "attractor %d: nearly every point on the grid (%d)", a, n);
        const uint8_t *c = attr_counts();
        int lit = 0, outside = 0;
        for (int y = 0; y < ATTR_W; y++) {
            for (int x = 0; x < ATTR_W; x++) {
                if (c[y * ATTR_W + x]) {
                    lit++;
                    outside += x < x0 || x > x1 || y < y0 || y > y1;
                }
            }
        }
        CHECKF(lit > 2000, "attractor %d (kind %d): spreads (%d pixels)", a, attr_kind(), lit);
        CHECKF(outside == 0, "attractor %d: inside its box (%d outside)", a, outside);
        bool dirty_lit = true;
        for (int i = 0; i < n; i++) {
            dirty_lit &= c[dirty[i]] > 0;
        }
        CHECKF(dirty_lit, "attractor %d: the points listed are lit", a);
    }
    CHECK(kinds[ATTR_CLIFFORD] > 0 && kinds[ATTR_DEJONG] > 0);
    for (int i = 0; i < 400; i++) {                 /* the counts stop at ATTR_MAX */
        attr_plot(50000, NULL, 0);
    }
    const uint8_t *c = attr_counts();
    int top = 0;
    for (int i = 0; i < ATTR_W * ATTR_W; i++) {
        top = c[i] > top ? c[i] : top;
    }
    CHECK(top == ATTR_MAX);
    attr_next();                                    /* the next one: from nothing */
    top = 0;
    for (int i = 0; i < ATTR_W * ATTR_W; i++) {
        top = c[i] > top ? c[i] : top;
    }
    CHECK(top == 0);
    uint32_t h[2] = {0, 0};                         /* the same seed, the same shape */
    for (int k = 0; k < 2; k++) {
        CHECK(attr_start(5));
        attr_plot(20000, NULL, 0);
        for (int i = 0; i < ATTR_W * ATTR_W; i++) {
            h[k] = h[k] * 31 + attr_counts()[i];
        }
    }
    CHECK(h[0] == h[1]);
    attr_free();
    CHECK(!attr_counts() && attr_plot(10, NULL, 0) == 0);
}

/* Boids (the screen saver): birds and hawks stay on the field at their speeds, the flock lines up (their
 * headings agree more than at the random start) and gathers, neighbours come to share a colour (far
 * more than at the random start), the birds keep away from the hawks, each trail follows its bird, and
 * the same seed gives the same flight */
static float boids_order(void)
{
    float sx = 0, sy = 0;
    for (int i = 0; i < BOIDS_N; i++) {
        float vx, vy;
        boids_vel(i, &vx, &vy);
        float v = sqrtf(vx * vx + vy * vy);
        sx += vx / v;
        sy += vy / v;
    }
    return sqrtf(sx * sx + sy * sy) / BOIDS_N;      /* 0: every way, 1: all one way */
}

static float boids_near(int from, int to)
{
    float sum = 0;
    for (int i = from; i < to; i++) {
        float x, y, best = 1e9f;
        boids_at(i, 0, &x, &y);
        for (int j = 0; j < BOIDS_N; j++) {
            float a, b;
            boids_at(j, 0, &a, &b);
            float d = (a - x) * (a - x) + (b - y) * (b - y);
            if (j != i && d < best) {
                best = d;
            }
        }
        sum += sqrtf(best);
    }
    return sum / (to - from);                       /* the mean distance to the nearest bird */
}

static float boids_same(void)
{
    int pairs = 0, same = 0;
    for (int i = 0; i < BOIDS_N; i++) {
        for (int j = i + 1; j < BOIDS_N; j++) {
            float a, b, c, d;
            boids_at(i, 0, &a, &b);
            boids_at(j, 0, &c, &d);
            if ((a - c) * (a - c) + (b - d) * (b - d) < BOIDS_SEE * BOIDS_SEE) {
                pairs++;
                same += boids_colour(i, NULL, NULL) == boids_colour(j, NULL, NULL);
            }
        }
    }
    return pairs ? (float)same / pairs : 0;         /* neighbours of one colour, of all neighbours */
}

static void test_boids(void)
{
    CHECK(boids_start(3));
    float order0 = boids_order(), near0 = boids_near(0, BOIDS_N), same0 = boids_same();
    bool in = true, speed = true, trail = true, colours = true;
    float hawk_gap = 0;
    int samples = 0;
    for (int s = 0; s < 1500; s++) {
        boids_step();
        for (int i = 0; i < BOIDS_ALL; i++) {
            float x, y, px, py, vx, vy;
            boids_at(i, 0, &x, &y);
            boids_at(i, 1, &px, &py);
            boids_vel(i, &vx, &vy);
            float v = sqrtf(vx * vx + vy * vy);
            in &= x >= 0 && y >= 0 && x <= BOIDS_W - 1 && y <= BOIDS_W - 1;
            speed &= i < BOIDS_N ? v >= BOIDS_SPEED_MIN - 0.01f && v <= BOIDS_SPEED_FLEE + 0.01f
                     : fabsf(v - BOIDS_SPEED_HAWK) < 0.01f;
            trail &= fabsf(x - px) <= BOIDS_SPEED_FLEE + 0.01f && fabsf(y - py) <= BOIDS_SPEED_FLEE + 0.01f;
            if (i < BOIDS_N) {
                int before, since, c = boids_colour(i, &before, &since);
                colours &= c >= 0 && c < BOIDS_COLOURS && before >= 0 && before < BOIDS_COLOURS &&
                           (since == 65535 || before != c);
            }
        }
        if (s >= 500 && s % 10 == 0) {
            hawk_gap += boids_near(BOIDS_N, BOIDS_ALL);
            samples++;
        }
    }
    CHECK(in && speed && trail && colours);
    float order1 = boids_order(), near1 = boids_near(0, BOIDS_N), same1 = boids_same();
    CHECKF(order1 > order0 + 0.1f, "boids: line up (%.2f -> %.2f)", order0, order1);
    CHECKF(near1 < near0, "boids: gather (%.1f -> %.1f px)", near0, near1);
    CHECKF(same1 > 0.6f && same1 > same0 + 0.2f, "boids: neighbours share colours (%.2f -> %.2f)", same0, same1);
    hawk_gap /= samples;
    CHECKF(hawk_gap > near1 * 1.5f, "boids: kept away from the hawks (%.1f px, birds %.1f apart)", hawk_gap, near1);
    float a[2];                                     /* the same seed, the same flight */
    for (int k = 0; k < 2; k++) {
        CHECK(boids_start(11));
        for (int s = 0; s < 200; s++) {
            boids_step();
        }
        float y;
        boids_at(7, 0, &a[k], &y);
    }
    CHECK(a[0] == a[1]);
    boids_free();
    boids_step();                                   /* (no flock: nothing) */
}

/* Lissajous (the screen saver): the figure stays on the screen and closes on itself, its phase drifts,
 * after LISS_HOLD steps it has shrunk to the middle and comes back with another ratio, the beam runs on
 * the figure, and the same seed gives the same figures */
static void test_lissajous(void)
{
    liss_start(8);
    int a0, b0, changes = 0;
    liss_ratio(&a0, &b0);
    bool in = true, closed = true, beam_on = true, small_at_change = true;
    int x0, y0, prev_x0 = -1;
    bool moved = false;
    for (int s = 0; s < LISS_HOLD * 4; s++) {
        float before = liss_size();
        liss_step();
        int a, b;
        liss_ratio(&a, &b);
        if (a != a0 || b != b0) {
            changes++;
            small_at_change &= before < 0.01f && liss_size() < 0.01f;
            a0 = a;
            b0 = b;
        }
        int n = liss_segs();
        in &= n > 0 && n <= LISS_SEGS_MAX;
        for (int i = 0; i <= n; i++) {
            int x, y;
            liss_point(i, &x, &y);
            in &= x >= 0 && y >= 0 && x < LISS_W && y < LISS_W;
        }
        int xe, ye;
        liss_point(0, &x0, &y0);
        liss_point(n, &xe, &ye);
        closed &= abs(x0 - xe) <= 1 && abs(y0 - ye) <= 1;
        if (prev_x0 >= 0 && x0 != prev_x0 && liss_size() > 0.99f) {
            moved = true;
        }
        prev_x0 = x0;
        int bx, by;                                 /* the beam: near some point of the figure */
        liss_beam(0, &bx, &by);
        int best = 1 << 30;
        for (int i = 0; i <= n; i++) {
            int x, y;
            liss_point(i, &x, &y);
            int d = (x - bx) * (x - bx) + (y - by) * (y - by);
            best = d < best ? d : best;
        }
        beam_on &= best <= 15 * 15;
    }
    CHECK(in && closed && beam_on && moved);
    CHECKF(changes >= 3 && small_at_change, "lissajous: %d changes, shrunk at them %d", changes, small_at_change);
    int r[2][2];                                    /* the same seed, the same figures */
    for (int k = 0; k < 2; k++) {
        liss_start(21);
        for (int s = 0; s < LISS_HOLD + 50; s++) {
            liss_step();
        }
        liss_point(17, &r[k][0], &r[k][1]);
    }
    CHECK(r[0][0] == r[1][0] && r[0][1] == r[1][1]);
}

/* Pendulum (the screen saver): each pendulum's energy stays put (RK4, no friction) over minutes, the bobs
 * stay on the screen, the trails are continuous and their speeds 0..1, the pendulums started a hair apart
 * swing as one at first and far apart later (chaos), they start again after PEND_RUN frames, and the
 * same seed gives the same swing */
static void test_pendulum(void)
{
    CHECK(pend_start(4));
    float e0[PEND_N], worst = 0;
    for (int k = 0; k < PEND_N; k++) {
        e0[k] = pend_energy(k);
    }
    bool in = true, smooth = true, speeds = true;
    for (int f = 0; f < PEND_RUN - 1; f++) {
        CHECK(!pend_step());
        for (int k = 0; k < PEND_N; k++) {
            float d = fabsf(pend_energy(k) - e0[k]) / fabsf(e0[k]);
            worst = d > worst ? d : worst;
            float x1, y1, x2, y2;
            pend_bobs(k, &x1, &y1, &x2, &y2);
            in &= x1 >= 0 && y1 >= 0 && x2 >= 0 && y2 >= 0 && x1 < PEND_W && y1 < PEND_W && x2 < PEND_W && y2 < PEND_W;
            float ax, ay, af, bx, by, bf;
            pend_trail(k, 0, &ax, &ay, &af);
            pend_trail(k, 1, &bx, &by, &bf);
            smooth &= fabsf(ax - bx) < 60 && fabsf(ay - by) < 60;
            speeds &= af >= 0 && af <= 1;
        }
    }
    CHECKF(worst < 0.01f, "pendulum: energy drifts %.4f", worst);
    CHECK(in && smooth && speeds && pend_trail_len() == PEND_TRAIL);
    CHECK(pend_step() && pend_trail_len() == 1);    /* a new start, the trails forgotten */
    pend_set(2.5f, 1.0f);
    float a[PEND_N][2];
    for (int f = 0; f < 25; f++) {                  /* (1 s: still as one) */
        pend_step();
    }
    pend_angles(0, &a[0][0], &a[0][1]);
    pend_angles(1, &a[1][0], &a[1][1]);
    CHECKF(fabsf(a[0][1] - a[1][1]) < 0.01f, "pendulum: as one at first (%.4f)", fabsf(a[0][1] - a[1][1]));
    for (int f = 0; f < 750; f++) {                 /* (30 s more) */
        pend_step();
    }
    pend_angles(0, &a[0][0], &a[0][1]);
    pend_angles(1, &a[1][0], &a[1][1]);
    CHECKF(fabsf(a[0][1] - a[1][1]) > 0.3f || fabsf(a[0][0] - a[1][0]) > 0.3f,
           "pendulum: apart later (%.3f vs %.3f)", a[0][1], a[1][1]);
    float b[2];                                     /* the same seed, the same swing */
    for (int k = 0; k < 2; k++) {
        CHECK(pend_start(9));
        for (int f = 0; f < 100; f++) {
            pend_step();
        }
        float y;
        pend_angles(2, &b[k], &y);
    }
    CHECK(b[0] == b[1]);
    pend_free();
    CHECK(!pend_step());
}

/* Automata (the screen savers): Game of Life (a blinker, its settling, a glider across the wrapped corner,
 * a dead cell fading), Brian's Brain (a pair fires the four cells beside it, three are too many, across
 * the edges), Langton's ant (its highway), the other rules' own steps on small patterns, every rule running
 * with its states and keys in range, the cyclic rules keep moving, a run starts again after its length,
 * a rule that dies out starts again, and the same seed gives the same runs */
static void test_automata(void)
{
    CHECK(auto_start(1, AUTO_R_LIFE));              /* Life: a blinker turns, then has settled */
    auto_clear();
    auto_set(10, 10, 1);
    auto_set(11, 10, 1);
    auto_set(12, 10, 1);
    CHECK(!auto_step());
    CHECK(auto_state(11, 9) == 1 && auto_state(11, 11) == 1 && auto_state(10, 10) == 0);
    CHECK(auto_key(11, 9) == AUTO_FADE && auto_key(11, 10) == AUTO_FADE + 1 && auto_key(10, 10) == AUTO_FADE - 1);
    CHECK(!auto_step() && auto_state(10, 10) == 1);
    CHECK(auto_step() && auto_generation() == 0);   /* the same board again: settled, a new start */
    auto_clear();                                   /* a glider (down-right) across the corner, and a lone cell */
    const int gx[] = {1, 2, 0, 1, 2}, gy[] = {0, 1, 2, 2, 2};
    for (int k = 0; k < 5; k++) {
        auto_set((AUTO_N - 3 + gx[k]) % AUTO_N, (AUTO_N - 3 + gy[k]) % AUTO_N, 1);
    }
    auto_set(60, 60, 1);
    for (int g = 0; g < 4 * 6; g++) {               /* 6 cells down and right */
        CHECKF(!auto_step(), "life: glider settled at %d", g);
        if (g < AUTO_FADE) {
            CHECK(auto_key(60, 60) == AUTO_FADE - 1 - g);   /* the lone cell fading out */
        }
    }
    bool glider = true;
    for (int k = 0; k < 5; k++) {
        glider &= auto_state((AUTO_N + 3 + gx[k]) % AUTO_N, (AUTO_N + 3 + gy[k]) % AUTO_N) == 1;
    }
    CHECK(glider);

    auto_set_rule(AUTO_R_BRAIN);                    /* Brian's Brain */
    auto_clear();
    for (int k = 0; k < 6; k++) {                   /* (pairs far apart: enough firing not to be too quiet) */
        auto_set(60 + k * 9, 100, 1);
        auto_set(61 + k * 9, 100, 1);
    }
    auto_set(10, 10, 1);                            /* a pair: the four cells beside both fire */
    auto_set(11, 10, 1);
    auto_set(30, 30, 1);                            /* three in a row: above and below the middle see three */
    auto_set(31, 30, 1);
    auto_set(32, 30, 1);
    auto_set(AUTO_N - 1, 50, 1);                    /* a pair across the left and right edges */
    auto_set(0, 50, 1);
    CHECK(!auto_step());
    CHECK(auto_state(10, 9) == 1 && auto_state(11, 9) == 1 && auto_state(10, 11) == 1 && auto_state(11, 11) == 1);
    CHECK(auto_state(9, 9) == 0 && auto_state(10, 10) == 2 && auto_key(10, 9) == AUTO_FADE);   /* firing: just born */
    CHECK(auto_state(31, 29) == 0 && auto_state(31, 31) == 0);
    CHECK(auto_state(AUTO_N - 1, 49) == 1 && auto_state(0, 49) == 1);
    CHECK(!auto_step() && auto_state(10, 10) == 0 && auto_key(10, 10) == AUTO_FADE - 1);
    auto_clear();
    auto_set(5, 5, 1);                              /* nearly nothing firing: too quiet, a new start */
    CHECK(auto_step());

    auto_set_rule(AUTO_R_ANT);                      /* Langton's ant: chaos, then its highway */
    auto_clear();
    while (auto_generation() < 10400) {
        CHECK(!auto_step());
    }
    int px, py, dx0 = 0, dy0 = 0;
    bool highway = true;
    auto_ant(&px, &py);
    CHECK(auto_key(px, py) == AUTO_KEY_ANT);
    for (int k = 0; k < 5; k++) {
        for (int g = 0; g < 13; g++) {              /* (312 steps: three of the highway's 104) */
            auto_step();
        }
        int x, y;
        auto_ant(&x, &y);
        int dx = (x - px + AUTO_N + AUTO_N / 2) % AUTO_N - AUTO_N / 2, dy = (y - py + AUTO_N + AUTO_N / 2) % AUTO_N - AUTO_N / 2;
        if (k == 0) {
            dx0 = dx;
            dy0 = dy;
        }
        highway &= dx == dx0 && dy == dy0 && abs(dx) == 6 && abs(dy) == 6;
        px = x;
        py = y;
    }
    CHECKF(highway, "ant: the highway (%d, %d)", dx0, dy0);

    auto_set_rule(AUTO_R_DAY_NIGHT);                /* Day & Night: born with 3 */
    auto_clear();
    auto_set(10, 10, 1);
    auto_set(11, 10, 1);
    auto_set(12, 10, 1);
    auto_set(70, 70, 1);
    CHECK(!auto_step());
    CHECK(auto_state(11, 9) == 1 && auto_state(11, 11) == 1 && auto_state(10, 10) == 0 && auto_state(70, 70) == 0);

    auto_set_rule(AUTO_R_SEEDS);                    /* Seeds: every live cell dies at once */
    auto_clear();
    auto_set(40, 40, 1);
    auto_set(41, 40, 1);
    CHECK(!auto_step());
    CHECK(auto_state(40, 40) == 0 && auto_state(40, 39) == 1 && auto_state(41, 41) == 1);
    auto_clear();
    CHECK(auto_step() && auto_rule() == AUTO_R_SEEDS);   /* nothing alive: a new start */

    auto_set_rule(AUTO_R_STAR_WARS);                /* Star Wars: a lone cell dies through 2, 3, then 0 */
    auto_clear();
    auto_set(50, 50, 1);
    for (int k = 0; k < 4; k++) {
        auto_set(90 + k % 2, 90 + k / 2, 1);        /* (a block: stays alive) */
    }
    auto_step();
    CHECK(auto_state(50, 50) == 2);
    auto_step();
    CHECK(auto_state(50, 50) == 3);
    auto_step();
    CHECK(auto_state(50, 50) == 0);

    auto_set_rule(AUTO_R_RPS);                      /* rock, paper, scissors: 3 neighbours a state on, not 2 */
    auto_clear();
    auto_set(20, 20, 1);
    auto_set(21, 20, 1);
    auto_set(22, 20, 1);
    auto_step();
    CHECK(auto_state(21, 21) == 1 && auto_state(20, 21) == 0);

    auto_set_rule(AUTO_R_EXCITABLE);                /* excitable: rest -> excited by a neighbour, then on round */
    auto_clear();
    auto_set(30, 30, 1);
    auto_step();
    CHECK(auto_state(31, 31) == 1 && auto_state(30, 30) == 2);
    for (int g = 0; g < auto_states() - 2; g++) {
        auto_step();
    }
    CHECK(auto_state(30, 30) == 0);

    auto_set_rule(AUTO_R_FOREST);                   /* forest: fire spreads to the trees next to it, burns out */
    auto_clear();
    auto_set(60, 60, 2);
    auto_set(61, 60, 1);
    auto_set(62, 60, 1);
    auto_step();
    CHECK(auto_state(60, 60) == 0 && auto_state(61, 60) == 2 && auto_state(62, 60) == 1);
    auto_step();
    CHECK(auto_state(61, 60) == 0 && auto_state(62, 60) == 2);

    bool in = true;                                 /* every rule runs, states and keys in range */
    float moving[2] = {0, 0};
    static uint8_t before[AUTO_CELLS];
    for (int r = 0; r < AUTO_R_COUNT; r++) {
        auto_set_rule(r);
        for (int g = 0; g < 300; g++) {
            auto_step();
        }
        for (int i = 0; i < AUTO_CELLS; i++) {
            before[i] = (uint8_t)auto_state(i % AUTO_N, i / AUTO_N);
        }
        auto_step();
        int changed = 0;
        for (int i = 0; i < AUTO_CELLS; i++) {
            int x = i % AUTO_N, y = i / AUTO_N, st = auto_state(x, y), k = auto_key(x, y);
            in &= st >= 0 && st < auto_states() && k >= 0 && k < AUTO_KEYS && auto_step_ms() > 0;
            changed += st != before[i];
        }
        if (r == AUTO_R_RPS || r == AUTO_R_CYCLIC) {
            moving[r == AUTO_R_CYCLIC] = (float)changed / AUTO_CELLS;
        }
    }
    CHECK(in);
    CHECKF(moving[0] > 0.1f && moving[1] > 0.05f, "automata: cyclic rules keep moving (%.2f, %.2f)", moving[0], moving[1]);

    auto_set_rule(AUTO_R_CYCLIC);                   /* a run starts again after its length, the same rule */
    int n = 0;
    while (!auto_step() && n < 1000) {
        n++;
    }
    CHECK(n == 900 && auto_rule() == AUTO_R_CYCLIC && auto_generation() == 0);
    int st[2];                                      /* the same seed, the same runs */
    for (int k = 0; k < 2; k++) {
        CHECK(auto_start(33, AUTO_R_STAR_WARS));
        for (int g = 0; g < 50; g++) {
            auto_step();
        }
        st[k] = auto_state(17, 23) * 100 + auto_key(40, 41);
    }
    CHECK(st[0] == st[1]);
    auto_free();
    CHECK(!auto_step() && auto_state(0, 0) == 0);
}

/*
 * The 3D automata (saver_automata3d_core.c): births and survival by the 6 or the 26 neighbours, the walls dead,
 * dying states, a crystal held before it starts again, each rule starting again (itself) within its run,
 * every rule running in range,
 * and the same seed giving the same runs.
 */
static void test_automata3d(void)
{
    const int c = (A3_N - 1) / 2;                   /* (the one cell of a patch of 1) */
    CHECK(a3_start(1, A3_R_CRYSTAL));               /* Crystal growth (VN, B1,3/S0-6): a cell grows its 6 */
    CHECK(a3_population() == 1 && a3_state(c, c, c) == 1);
    CHECK(!a3_step() && a3_population() == 7);
    CHECK(a3_state(c, c, c) == 1 && a3_state(c + 1, c, c) == 1 && a3_state(c, c, c - 1) == 1);
    CHECK(a3_state(c + 1, c + 1, c) == 0);          /* (an edge away: 2 live neighbours, not born) */
    for (int g = 1; g < 14; g++) {                  /* grown for its run, then held */
        CHECK(!a3_step());
    }
    int pop = a3_population();
    CHECK(a3_state(0, c, c) == 0 && pop > 100);     /* (not at the walls yet) */
    bool held = true;
    while (a3_generation() < 14 + 16) {
        held &= !a3_step() && a3_population() == pop;
    }
    CHECK(held);
    CHECK(a3_step() && a3_rule() == A3_R_CRYSTAL && a3_generation() == 0 && a3_population() == 1);   /* again */

    a3_set_rule(A3_R_PYRO);                         /* Pyroclastic (Moore, B6-8/S4-7/C10) */
    a3_clear();
    a3_set(10, 10, 10, 1);                          /* a lone cell: dying through its 8 states */
    const int nb[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (int k = 0; k < 6; k++) {                   /* 6 around an empty cell: it is born */
        a3_set(20 + nb[k][0], 20 + nb[k][1], 20 + nb[k][2], 1);
    }
    a3_set(0, 5, 5, 1);                             /* across the walls: no neighbours (no wrap) */
    a3_set(A3_N - 1, 5, 5, 1);
    CHECK(!a3_step());
    CHECK(a3_state(20, 20, 20) == 1 && a3_state(10, 10, 10) == 2);
    CHECK(a3_state(0, 5, 5) == 2 && a3_state(A3_N - 1, 5, 5) == 2);
    for (int s = 3; s < 10; s++) {
        CHECK(!a3_step() && a3_state(10, 10, 10) == s);
    }
    a3_clear();
    a3_set(10, 10, 10, 9);                          /* the last dying state: dead next, then all died out */
    CHECK(!a3_step() && a3_state(10, 10, 10) == 0 && a3_population() == 0);
    CHECK(a3_step() && a3_rule() == A3_R_PYRO && a3_population() > 0);   /* a new start */

    bool in = true;                                 /* every rule runs, states in range */
    for (int r = 0; r < A3_R_COUNT; r++) {
        a3_set_rule(r);
        CHECK(a3_rule() == r && a3_rule_name(r)[0] && a3_population() > 0 && a3_step_ms() > 0);
        for (int g = 0; g < 60 && !a3_step(); g++) {
            const uint8_t *cell = a3_cells();
            int n = 0;
            for (int i = 0; i < A3_CELLS; i++) {
                in &= cell[i] < a3_states();
                n += cell[i] != 0;
            }
            in &= n == a3_population();
        }
    }
    CHECK(in);
    for (int r = 0; r < A3_R_COUNT; r++) {          /* each starts again within its run, the same rule */
        a3_set_rule(r);
        int g = 0;
        while (!a3_step() && g < 1000) {
            g++;
        }
        CHECKF(g < 1000 && a3_rule() == r && a3_generation() == 0, "3D automata: rule %d after %d", r, g);
    }
    int st[2];                                      /* the same seed, the same runs */
    for (int k = 0; k < 2; k++) {
        CHECK(a3_start(77, A3_R_PYRO));
        for (int g = 0; g < 30; g++) {
            a3_step();
        }
        st[k] = a3_population() * 16 + a3_state(15, 16, 17);
    }
    CHECK(st[0] == st[1]);
    a3_free();
    CHECK(!a3_step() && a3_state(0, 0, 0) == 0 && !a3_cells());
}

/*
 * The starfield (saver_stars_core.c): the stars come nearer by the speed and come back far away once past,
 * their streaks point out from the middle, brightness and colours in range, the speed's waves, and the
 * same seed giving the same flights.
 */
static void test_stars(void)
{
    CHECK(stars_start(5));
    int x0, y0, x1, y1, bright, tone;
    CHECK(!stars_get(0, &x0, &y0, &x1, &y1, &bright, &tone));   /* (just placed: no streak yet) */
    static float z[STARS_N];
    bool in = true, outward = true, nearer = true, back = false;
    float lo = 1e9f, hi = 0;
    for (int f = 0; f < STARS_WAVE; f++) {
        for (int i = 0; i < STARS_N; i++) {
            z[i] = stars_depth(i);
        }
        stars_step();
        float v = stars_speed();
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
        for (int i = 0; i < STARS_N; i++) {
            float d = stars_depth(i);
            in &= d >= STARS_NEAR && d <= STARS_FAR;
            if (d == STARS_FAR) {
                back = true;                        /* passed, back far away */
                continue;
            }
            nearer &= fabsf(z[i] - v - d) < 1e-5f;
            if (stars_get(i, &x0, &y0, &x1, &y1, &bright, &tone)) {
                int r0 = (x0 - STARS_W / 2) * (x0 - STARS_W / 2) + (y0 - STARS_W / 2) * (y0 - STARS_W / 2);
                int r1 = (x1 - STARS_W / 2) * (x1 - STARS_W / 2) + (y1 - STARS_W / 2) * (y1 - STARS_W / 2);
                outward &= r1 + 2 * STARS_W >= r0;  /* (the tail nearer the middle, give or take rounding) */
                in &= bright >= 0 && bright <= 255 && tone >= 0 && tone < STARS_TONES;
            }
        }
    }
    CHECK(in && nearer && back);
    CHECK(outward);
    CHECKF(lo > 0.1f * STARS_SPEED && lo < 0.25f * STARS_SPEED && hi > 1.6f * STARS_SPEED && hi < 2.0f * STARS_SPEED,
           "stars: speed %.4f .. %.4f", lo, hi);
    float d[2];                                     /* the same seed, the same flights */
    for (int k = 0; k < 2; k++) {
        CHECK(stars_start(99));
        for (int f = 0; f < 300; f++) {
            stars_step();
        }
        d[k] = stars_depth(17) + stars_depth(300);
    }
    CHECK(d[0] == d[1]);
    stars_free();
    CHECK(!stars_get(0, &x0, &y0, &x1, &y1, &bright, &tone) && stars_depth(0) == 0);
    stars_step();                                   /* (nothing, with no stars) */
}

int main(int argc, char **argv)
{
    static const test_t TESTS[] = {
        {"bit tables", test_bits},           {"canonical hash", test_canon},       {"search", test_search},           {"geometry", test_geometry},         {"built-in puzzles", test_builtin},
        {"generator", test_generate},        {"determinism (golden)", test_determinism},
        {"layout texts and .sdx", test_texts_and_sdx}, {".sdx edge cases", test_sdx_edges},
        {"hints", test_hints},               {"learn", test_learn},
        {"rules check", test_conflicts},     {"hint words", test_hint_words},
        {"share", test_share},               {"daily puzzle", test_daily},
        {"undo history", test_history},      {"import parsing", test_import_parse},
        {"rain (screen saver)", test_rain},
        {"attractor (screen saver)", test_attractor}, {"boids (screen saver)", test_boids},
        {"lissajous (screen saver)", test_lissajous}, {"pendulum (screen saver)", test_pendulum},
        {"automata (screen saver)", test_automata}, {"3D automata (screen saver)", test_automata3d},
        {"stars (screen saver)", test_stars},
    };
    sudoku_geom_init();
    int failed_tests = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        if (argc > 1 && !strstr(TESTS[i].name, argv[1])) {
            continue;
        }
        int before = s_failed;
        clock_t t0 = clock();
        TESTS[i].fn();
        double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        bool ok = s_failed == before;
        failed_tests += !ok;
        printf("%s  %-24s %7.0f ms\n", ok ? "ok  " : "FAIL", TESTS[i].name, ms);
    }
    printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}

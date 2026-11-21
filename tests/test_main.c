/*
 * PC-side tests: the parts of the firmware that are plain C (no LVGL, no ESP-IDF),
 * built with the host's compiler (tests/Makefile, `make -C tests`):
 *
 *   src/generator.c   geometry, solver, grader, generator, hints, layout texts
 *   src/puzzles_data.c the 1600 built-in puzzles
 *   src/sdx.c         the puzzle line format (.sdx import / export, Send by sound)
 *
 * Each test prints one line; any failed check prints where and the run ends with a
 * non-zero status. The "golden" values pin what a seed produces, so a change to the
 * generator that would change the daily puzzles (made from the date) shows up here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "generator.h"
#include "puzzles.h"
#include "sdx.h"

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
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

/* every active unit of the board holds 1..n once each, and the givens are kept */
static bool solved_ok(const sudoku_geom_t *g, const uint8_t givens[81], const uint8_t sol[81])
{
    for (int i = 0; i < g->nunits; i++) {
        int u = g->unit_ids[i];
        unsigned seen = 0;
        for (int k = 0; k < g->n; k++) {
            int c = g->unit[u][k], d = sol[c];
            if (d < 1 || d > g->n || (seen >> d & 1)) return false;
            seen |= 1u << d;
        }
    }
    for (int c = 0; c < 81; c++) {
        if (givens[c] && givens[c] != sol[c]) return false;
        if (!g->used[c] && sol[c]) return false;
    }
    for (int k = 0; k < g->ncells; k++) {           /* peers differ (Anti-Knight, Anti-King too) */
        int c = g->cells[k];
        for (int p = 0; p < g->npeers[c]; p++) {
            if (sol[g->peers[c][p]] == sol[c]) return false;
        }
    }
    for (int c = 0; g->consec_on && c < 81; c++) {  /* Consecutive, Non-Consecutive: bar = 1 apart */
        if (c % 9 < 8 && (abs(sol[c] - sol[c + 1]) == 1) != ((g->consec[c] & CONSEC_RIGHT) != 0)) return false;
        if (c < 72 && (abs(sol[c] - sol[c + 9]) == 1) != ((g->consec[c] & CONSEC_DOWN) != 0)) return false;
    }
    for (int c = 0; g->edge_kind && c < 81; c++) {  /* Kropki, XV, Greater-Than: the marks (and none) */
        if (c % 9 < 8 && !sudoku_edge_ok(g->edge_kind, EDGE_RIGHT(g->edge[c]), sol[c], sol[c + 1])) return false;
        if (c < 72 && !sudoku_edge_ok(g->edge_kind, EDGE_DOWN(g->edge[c]), sol[c], sol[c + 9])) return false;
    }
    for (int t = 0; t < g->nthermos; t++) {         /* the lines: Thermo, Arrow, Renban, Whispers */
        int n = g->thermo_n[t], lo = 10, hi = 0, sum = 0;
        unsigned seen = 0;
        for (int i = 0; i < n; i++) {
            int x = sol[g->thermo_cell[t][i]];
            if (i && g->line_kind == LINE_THERMO && x <= sol[g->thermo_cell[t][i - 1]]) return false;
            if (i && g->line_kind == LINE_WHISPER && abs(x - sol[g->thermo_cell[t][i - 1]]) < 5) return false;
            if (g->line_kind == LINE_RENBAN && (seen >> x & 1)) return false;
            seen |= 1u << x;
            if (x < lo) lo = x;
            if (x > hi) hi = x;
            if (i) sum += x;
        }
        if (g->line_kind == LINE_RENBAN && hi - lo != n - 1) return false;
        if (g->line_kind == LINE_ARROW && sum != sol[g->thermo_cell[t][0]]) return false;
    }
    for (int q = 0; q < g->nquads; q++) {           /* Quadruple: every circle's digits are there */
        int empty;
        if (sudoku_quad_missing(g, q, sol, &empty) != 0) return false;
    }
    for (int k = 0; k < g->ncages; k++) {           /* Killer: the cage sums */
        int sum = 0;
        for (int j = 0; j < g->cage_n[k]; j++) sum += sol[g->cage_cell[k][j]];
        if (sum != g->cage_sum[k]) return false;
    }
    return true;
}

/* givens only on the board's cells, digits 1..n */
static bool givens_ok(const sudoku_geom_t *g, const uint8_t givens[81])
{
    for (int c = 0; c < 81; c++) {
        if (givens[c] > g->n) return false;
        if (givens[c] && !g->used[c]) return false;
    }
    return true;
}

/* the parts of two boards a puzzle's layout decides */
static bool same_layout(const sudoku_geom_t *a, const sudoku_geom_t *b)
{
    return a->variant == b->variant && a->n == b->n && !memcmp(a->region, b->region, 81) &&
           a->ncages == b->ncages && !memcmp(a->cage_of, b->cage_of, 81) &&
           !memcmp(a->cage_sum, b->cage_sum, sizeof(a->cage_sum)) && !memcmp(a->parity, b->parity, 81) &&
           !memcmp(a->consec, b->consec, 81) && a->nthermos == b->nthermos &&
           !memcmp(a->thermo_of, b->thermo_of, 81) && !memcmp(a->thermo_pos, b->thermo_pos, 81) &&
           a->line_kind == b->line_kind && a->sandwich == b->sandwich && !memcmp(a->sandwich_sum, b->sandwich_sum, 18) &&
           a->edge_kind == b->edge_kind && !memcmp(a->edge, b->edge, 81) && a->nquads == b->nquads &&
           !memcmp(a->quad_at, b->quad_at, a->nquads) && !memcmp(a->quad_n, b->quad_n, a->nquads) &&
           !memcmp(a->quad_dig, b->quad_dig, sizeof(a->quad_dig[0]) * a->nquads);
}

/* the layout texts of a board, as the game keeps them (sudoku_db's generated table) */
static void layout_texts(const sudoku_geom_t *g, char regions[82], char *cages, char *marks, char *thermos, int len)
{
    regions[0] = cages[0] = marks[0] = thermos[0] = 0;
    switch (g->variant) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) regions[i] = (char)('0' + g->region[i]);
        regions[81] = 0;
        break;
    case VAR_KILLER:   sudoku_cages_text(g, cages, len); break;
    case VAR_QUAD:     sudoku_quads_text(g, cages, len); break;
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:       sudoku_marks_text(g, marks, len); break;
    case VAR_SANDWICH: sudoku_sandwich_text(g, marks, len); break;
    case VAR_THERMO:
    case VAR_ARROW:
    case VAR_RENBAN:
    case VAR_WHISPER:  sudoku_thermos_text(g, thermos, len); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- tests */

static void test_geometry(void)
{
    static const struct { int variant, n, ncells, nunits, peers0; } G[] = {
        {VAR_CLASSIC, 9, 81, 27, 20}, {VAR_X, 9, 81, 29, 26}, {VAR_HYPER, 9, 81, 31, 20},
        {VAR_MINI, 6, 36, 18, 12},     {VAR_KIDS, 4, 16, 12, 7},
        {VAR_KING, 9, 81, 27, 20},     {VAR_NONCONSEC, 9, 81, 27, 20}, {VAR_DISJOINT, 9, 81, 36, 24},
        {VAR_ASTERISK, 9, 81, 28, 20},
    };
    for (size_t i = 0; i < sizeof(G) / sizeof(G[0]); i++) {
        const sudoku_geom_t *g = sudoku_geom(G[i].variant);
        CHECKF(g->n == G[i].n, "%s n %d", variant_name(G[i].variant), g->n);
        CHECKF(g->ncells == G[i].ncells, "%s ncells %d", variant_name(G[i].variant), g->ncells);
        CHECKF(g->nunits == G[i].nunits, "%s nunits %d", variant_name(G[i].variant), g->nunits);
        CHECKF(g->npeers[0] == G[i].peers0, "%s peers of cell 0: %d", variant_name(G[i].variant), g->npeers[0]);
        for (int k = 0; k < g->ncells; k++) {       /* boxes / regions: n cells each */
            int c = g->cells[k], cnt = 0;
            for (int j = 0; j < g->ncells; j++) cnt += g->region[g->cells[j]] == g->region[c];
            CHECK(cnt == g->n);
        }
    }
    /* X: the corner sees its diagonal too (6 beyond its box); Hyper: cell (1,1) its window (3 more) */
    CHECKF(sudoku_geom(VAR_HYPER)->npeers[10] == 23, "Hyper peers of cell 10: %d", sudoku_geom(VAR_HYPER)->npeers[10]);
    CHECK(sudoku_geom_on_diag(sudoku_geom(VAR_X), 80) && !sudoku_geom_on_diag(sudoku_geom(VAR_X), 1));
    CHECK(strcmp(variant_name(VAR_KIDS), "Kids 4x4") == 0);
    /* Anti-King: cell 2 (row 0, column 2) touches (1, 3) diagonally, outside its box */
    CHECKF(sudoku_geom(VAR_KING)->npeers[2] == 21, "Anti-King peers of cell 2: %d", sudoku_geom(VAR_KING)->npeers[2]);
    CHECK(sudoku_geom_peer(sudoku_geom(VAR_KING), 2, 12) && !sudoku_geom_peer(sudoku_geom(VAR_CLASSIC), 2, 12));
    /* Disjoint: cell 0's group is the top-left cell of every box */
    CHECK(sudoku_geom_peer(sudoku_geom(VAR_DISJOINT), 0, 30) && sudoku_geom_peer(sudoku_geom(VAR_DISJOINT), 0, 60));
    /* Asterisk: its cell 13 sees the six others it shares no row, column or box with */
    CHECKF(sudoku_geom(VAR_ASTERISK)->npeers[13] == 26, "Asterisk peers of cell 13: %d",
           sudoku_geom(VAR_ASTERISK)->npeers[13]);
    CHECK(sudoku_geom_tinted(sudoku_geom(VAR_ASTERISK), 40) && !sudoku_geom_tinted(sudoku_geom(VAR_ASTERISK), 41));
    CHECK(sudoku_geom(VAR_NONCONSEC)->consec_on && strcmp(variant_name(VAR_NONCONSEC), "Non-Consecutive") == 0);
    CHECK(sudoku_geom(VAR_KIDS)->box_w == 2 && sudoku_geom(VAR_KIDS)->box_h == 2);
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
        if (n != 1) continue;
        unique++;
        CHECKF(sudoku_solve(G, g, sol) && solved_ok(G, g, sol), "puzzle %d: solve", id);
        CHECKF(sudoku_classic_unique(g), "puzzle %d: classic_unique", id);
    }
    CHECK(unique == PUZZLE_COUNT);
    CHECK(puzzle_collection_of(0) == 0 && puzzle_collection_of(PUZZLE_COUNT - 1) >= 0);
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
            if (!ok) continue;
            CHECKF(board.variant == v, "%s: board variant %d", variant_name(v), board.variant);
            CHECKF(givens_ok(&board, g), "%s %s: givens on the board", variant_name(v), gen_level_name(lv));
            CHECKF(sudoku_count_solutions(&board, g, 2) == 1, "%s %s: one solution", variant_name(v),
                   gen_level_name(lv));
            CHECKF(sudoku_solve(&board, g, sol) && solved_ok(&board, g, sol), "%s %s: solution valid",
                   variant_name(v), gen_level_name(lv));
            int graded = sudoku_grade(&board, g);
            CHECKF(graded == (int)got, "%s %s: graded %d, generator said %d", variant_name(v),
                   gen_level_name(lv), graded, (int)got);
            if (v == VAR_KIDS) CHECKF(got <= GEN_MEDIUM, "Kids 4x4 %s came out %s", gen_level_name(lv),
                                      gen_level_name(got));
            if (v != VAR_KIDS && lv < GEN_HARD) CHECKF((int)got == lv, "%s %s came out %s", variant_name(v),
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
        CHECKF(!memcmp(g1, g2, 81) && l1 == l2 && same_layout(&b1, &b2), "%s seed %u: not the same twice",
               variant_name(GOLD[i].variant), GOLD[i].seed);
        uint8_t key[81 + 81 + 81];                  /* givens, regions, cages: the whole puzzle */
        memcpy(key, g1, 81);
        memcpy(key + 81, b1.region, 81);
        memcpy(key + 162, b1.cage_of, 81);
        uint32_t h = fnv1a(key, sizeof(key));
        if (getenv("PRINT_GOLDEN")) printf("    golden %s %s seed %u: 0x%08x\n", variant_name(GOLD[i].variant),
                                           gen_level_name(GOLD[i].level), GOLD[i].seed, h);
        else CHECKF(h == GOLD[i].hash, "%s %s seed %u: 0x%08x, pinned 0x%08x", variant_name(GOLD[i].variant),
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
        if (!sudoku_generate(v, GEN_EASY, 777u + (uint32_t)v, g, &board, &got)) continue;
        layout_texts(&board, regions, cages, marks, thermos, (int)sizeof(cages));
        for (int pack = 0; pack <= 1; pack++) {
            int len = sdx_format(v, got, g, regions, cages, marks, thermos, pack, line, sizeof(line));
            CHECKF(len > 0, "%s: sdx_format", variant_name(v));
            if (len <= 0) continue;
            sdx_puzzle_t p;
            CHECKF(sdx_parse(line, &p), "%s: sdx_parse (packed %d)", variant_name(v), pack);
            CHECKF(p.variant == v && p.level == (int)got && !memcmp(p.givens, g, 81), "%s: fields back (packed %d)",
                   variant_name(v), pack);
            CHECKF(sdx_geom(&p, &back) && same_layout(&board, &back), "%s: layout back (packed %d)",
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
    snprintf(buf, sizeof(buf), "23|0|%s", E1);      /* no variant 23 */
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
        if (k >= 1) g[0] = 5;
        if (k == 2) g[80] = 7;
        char giv[82];
        CHECK(sdx_pack_givens(g, giv, sizeof(giv)) > 0);
        snprintf(buf, sizeof(buf), "0|-|%s", giv);
        CHECKF(sdx_parse(buf, &p) && !memcmp(p.givens, g, 81), "long empty runs (%d): %s", k, giv);
    }
    /* pack -> parse, for random givens */
    srand(1);
    for (int t = 0; t < 2000; t++) {
        uint8_t g[81];
        for (int i = 0; i < 81; i++) g[i] = (uint8_t)(rand() % 3 ? 0 : 1 + rand() % 9);
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

/* ---------------------------------------------------------------- run */

typedef struct { const char *name; void (*fn)(void); } test_t;

int main(int argc, char **argv)
{
    static const test_t TESTS[] = {
        {"geometry", test_geometry},         {"built-in puzzles", test_builtin},
        {"generator", test_generate},        {"determinism (golden)", test_determinism},
        {"layout texts and .sdx", test_texts_and_sdx}, {".sdx edge cases", test_sdx_edges},
        {"hints", test_hints},
    };
    sudoku_geom_init();
    int failed_tests = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        if (argc > 1 && !strstr(TESTS[i].name, argv[1])) continue;
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

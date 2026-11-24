/**
 * @file gen_geometry.c
 * @ingroup generator
 * @brief The puzzle generator's boards: the names, each variant's board (units, peers, marks,
 *        lines, cages); the variants' texts read and written: gen_texts.c.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/** the levels' names */
static const char *const GEN_LEVEL_NAMES[GEN_LEVELS] = {"Easy", "Medium", "Hard", "Expert"};

/** the variants' names, sudoku_variant_t in order */
static const char *const VARIANT_NAMES[VAR_COUNT] = {"Classic", "X-Sudoku", "Mini 6x6", "Hyper", "Jigsaw", "Killer",
                                                     "Anti-Knight", "Even-Odd", "Consecutive", "Thermo", "Sandwich",
                                                     "Kids 4x4", "Anti-King", "Non-Consecutive", "Disjoint Groups",
                                                     "Asterisk", "Kropki", "XV", "Greater-Than", "Quadruple", "Arrow", "Renban",
                                                     "German Whispers", "Skyscraper", "Little Killer", "Palindrome",
                                                     "Region Sum Lines", "X-Sums", "Zipper Lines", "Frame", "Between Lines",
                                                     "Entropic Lines", "Modular Lines", "Center Dot", "Outside Sudoku",
                                                     "Rossini", "Diagonal Non-Consecutive", "Dutch Whispers",
                                                     "Parity Lines", "Slow Thermo", "Fortress", "Argyle",
                                                     "Nabner", "Numbered Rooms", "Sum Skyscrapers",
                                                     "Lockout Lines", "Sumless Killer", "Double Arrow",
                                                     "Diagonal Consecutive", "10 Lines", "Girandola",
                                                     "159 Sudoku", "Product Killer", "Extra Regions", "Anti-Diagonal",
                                                     "Pill Arrows", "Odd/Even Skyscrapers", "Chess Sudoku", "Magic Square",
                                                     "Average Lines", "Dutch Flatmates", "Mathrax",
                                                     "Point Symmetry", "Before Nine", "Quadro", "Anti-Queen 9s",
                                                     "Next to Nine", "Clone Sudoku", "Search Nine", "Battlefield",
                                                     "Sequence Lines", "Battenburg", "Mini 8x8"
                                                    };

const char *gen_level_name(int level) { return GEN_LEVEL_NAMES[level]; }

const char *variant_name(int variant) { return VARIANT_NAMES[variant]; }

/* (documented in generator.h) */
bool is_sudoku_edge_ok(int kind, int mark, int a, int b)
{
    switch (kind) {
    case EDGE_KROPKI: {
        bool white = a - b == 1 || b - a == 1, black = a == 2 * b || b == 2 * a;
        return mark == MARK_WHITE ? white : mark == MARK_BLACK ? black : !white && !black;
    }
    case EDGE_XV:
        return mark == MARK_V ? a + b == 5 : mark == MARK_X ? a + b == 10 : a + b != 5 && a + b != 10;
    case EDGE_GT:
    case EDGE_FORT:                                 /* (Fortress: a grey cell's sides, as signs) */
        return mark == MARK_LESS ? a < b : mark == MARK_MORE ? a > b : true;
    default:
        return true;
    }
}

/* (documented in generator.h) */
int sudoku_argyle_line(int k, uint8_t cells[9])
{
    static const int8_t START[ARGYLE_LINES][2] = {{0, 1}, {0, 4}, {1, 0}, {4, 0}, {0, 7}, {0, 4}, {1, 8}, {4, 8}};
    const int dc = k < 4 ? 1 : -1;                  /* down to the right, then down to the left */
    int n = 0;
    for (int r = START[k][0], c = START[k][1]; r < 9 && c >= 0 && c < 9; r++, c += dc) {
        cells[n++] = (uint8_t)(r * 9 + c);
    }
    return n;
}

/* (documented in generator.h) */
int sudoku_regionsum_parts(const sudoku_geom_t *g, int t, const uint8_t vals[81], int sum[9], bool full[9])
{
    int box[9], n = 0;
    for (int i = 0; i < g->thermo_n[t]; i++) {
        int c = g->thermo_cell[t][i], b = g->region[c], k = 0;
        while (k < n && box[k] != b) {
            k++;
        }
        if (k == n) {
            box[n] = b;
            sum[n] = 0;
            full[n++] = true;
        }
        sum[k] += vals[c];
        full[k] &= vals[c] != 0;
    }
    return n;
}

/* (documented in generator.h) */
int sudoku_xsum_of(const uint8_t vals[81], int lu)
{
    int cells[9], sum = 0;
    line_cells(lu, cells);
    int x = vals[cells[0]];
    for (int k = 0; k < x; k++) {
        if (!vals[cells[k]]) {
            return 0;
        }
        sum += vals[cells[k]];
    }
    return sum;
}

/* (documented in generator.h) */
int sudoku_nroom_of(const uint8_t vals[81], int lu)
{
    int cells[9];
    line_cells(lu, cells);
    int x = vals[cells[0]];
    return x ? vals[cells[x - 1]] : 0;
}

/* (documented in generator.h) */
int sudoku_psky_of(const uint8_t vals[81], int lu, bool even)
{
    int cells[9], n = 0, top = 0;
    line_cells(lu, cells);
    for (int k = 0; k < 9 && top < 9; k++) {
        int x = vals[cells[k]];
        if (!x) {
            return -1;
        }
        if (x > top) {
            top = x;
            n += (x % 2 == 0) == even;
        }
    }
    return n;
}

/* (documented in generator.h) */
int sudoku_skysum_of(const uint8_t vals[81], int lu)
{
    int cells[9], sum = 0, top = 0;
    line_cells(lu, cells);
    for (int k = 0; k < 9 && top < 9; k++) {
        int x = vals[cells[k]];
        if (!x) {
            return 0;
        }
        if (x > top) {
            top = x;
            sum += x;
        }
    }
    return sum;
}

/* (documented in generator.h) */
int sudoku_b9_of(const uint8_t vals[81], int lu)
{
    int cells[9], sum = 0;
    line_cells(lu, cells);
    for (int k = 0; k < 9; k++) {
        int x = vals[cells[k]];
        if (!x) {
            return -1;
        }
        if (x == 9) {
            return sum;
        }
        sum += x;
    }
    return -1;                                      /* (no 9: not a Sudoku line) */
}

/* (documented in generator.h) */
int sudoku_bf_of(const uint8_t vals[81], int lu)
{
    int cells[9];
    line_cells(lu, cells);
    const int x = vals[cells[0]], y = vals[cells[8]];
    if (!x || !y) {
        return -1;
    }
    const int lo = x + y > 9 ? 9 - y : x, hi = x + y > 9 ? x - 1 : 8 - y;   /* the overlap, or the gap */
    int sum = 0;
    for (int k = lo; k <= hi; k++) {
        if (!vals[cells[k]]) {
            return -1;
        }
        sum += vals[cells[k]];
    }
    return sum;
}

/* (documented in generator.h) */
int sudoku_n9_of(const uint8_t vals[81], int lu)
{
    int cells[9];
    line_cells(lu, cells);
    for (int k = 0; k < 9; k++) {
        if (vals[cells[k]] != 9) {
            continue;
        }
        int set = 0;
        for (int j = k - 1; j <= k + 1; j += 2) {   /* the cells beside it in the line */
            if (j < 0 || j > 8) {
                continue;
            }
            const int x = vals[cells[j]];
            if (!x) {
                return 0;
            }
            set |= 1 << (x - 1);
        }
        return set;
    }
    return 0;
}

/* ---------------------------------------------------------------- geometry */

/** @brief ~25 KB: from malloc, which the sketch sends to PSRAM (internal RAM is
 * kept for Wi-Fi and the video encoder) */
static sudoku_geom_t *s_geom;

/**
 * @brief Builds a board: its cells, units, peers and tables.
 * @param[out] g  gets it
 * @param[in] variant  sudoku_variant_t
 * @param[in] n  digits and side: 9, 6 or 4
 * @param[in] box_h  a box's height
 * @param[in] box_w  a box's width
 * @param[in] diag  the diagonals are units
 * @param[in] windows  the Hyper windows are units
 * @param[in] regions  Jigsaw's region map, NULL = boxes of box_h x box_w
 */
static void geom_build(sudoku_geom_t *g, int variant, int n, int box_h, int box_w, bool diag, bool windows,
                       const uint8_t *regions)
{
    memset(g, 0, sizeof(*g));
    g->variant = (uint8_t)variant;
    g->n = (uint8_t)n;
    g->box_h = (uint8_t)box_h;
    g->box_w = (uint8_t)box_w;
    g->diag = diag;
    g->windows = windows;
    memset(g->cage_of, CAGE_NONE, sizeof(g->cage_of));
    memset(g->thermo_of, THERMO_NONE, sizeof(g->thermo_of));
    memset(g->mx_of, MX_NONE, sizeof(g->mx_of));
    memset(g->clone_of, CLONE_NONE, sizeof(g->clone_of));
    memset(g->clone_pair, CLONE_NONE, sizeof(g->clone_pair));
    g->all = (uint16_t)(((1u << (n + 1)) - 1) & ~1u);
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int cell = r * 9 + c;
            g->cells[g->ncells++] = (uint8_t)cell;
            g->used[cell] = true;
        }
    }
    int boxes_across = n / box_w;
    int fill[UNITS_MAX] = {0};
    for (int k = 0; k < g->ncells; k++) {
        int cell = g->cells[k], r = cell / 9, c = cell % 9;
        int ids[5], nid = 0;
        ids[nid++] = r;
        ids[nid++] = 9 + c;
        g->region[cell] = (uint8_t)(regions ? regions[cell] : (r / box_h) * boxes_across + c / box_w);
        ids[nid++] = 18 + g->region[cell];
        if (diag && r == c) {
            ids[nid++] = UNIT_DIAG_MAIN;
        }
        if (diag && r + c == n - 1) {
            ids[nid++] = UNIT_DIAG_ANTI;    /* centre cell: both (5 units) */
        }
        int w = sudoku_geom_window(g, cell);
        if (w >= 0) {
            ids[nid++] = UNIT_WINDOW + w;
        }
        if (variant == VAR_DISJOINT) {
            ids[nid++] = UNIT_GROUP + (r % 3) * 3 + c % 3;
        }
        if (is_sudoku_geom_on_asterisk(g, cell)) {
            ids[nid++] = UNIT_ASTERISK;
        }
        if (is_sudoku_geom_on_centerdot(g, cell)) {
            ids[nid++] = UNIT_CENTERDOT;
        }
        if (is_sudoku_geom_on_girandola(g, cell)) {
            ids[nid++] = UNIT_GIRANDOLA;
        }
        for (int i = 0; i < nid; i++) {
            int u = ids[i];
            g->unit[u][fill[u]++] = (uint8_t)cell;
            g->active[u] = true;
        }
        for (int i = 0; i < nid; i++) {
            g->cu[cell][i] = (uint8_t)ids[i];
        }
        g->ncu[cell] = (uint8_t)nid;
    }
    for (int u = 0; u < UNITS_MAX; u++) {
        if (g->active[u]) {
            g->unit_ids[g->nunits++] = (uint8_t)u;
        }
    }
    for (int k = 0; k < g->ncells; k++) {
        g->allow[g->cells[k]] = g->all;
    }
    for (int k = 0; k < g->ncells; k++) {                   /* peers: share any unit */
        int cell = g->cells[k];
        for (int i = 0; i < g->nunits; i++) {
            int u = g->unit_ids[i];
            bool in = false;
            for (int j = 0; j < n; j++) {
                in |= g->unit[u][j] == cell;
            }
            if (!in) {
                continue;
            }
            for (int j = 0; j < n; j++) {
                int o = g->unit[u][j];
                if (o == cell || is_sudoku_geom_peer(g, cell, o)) {
                    continue;
                }
                g->peer_bits[cell][o >> 5] |= 1u << (o & 31);
                g->peers[cell][g->npeers[cell]++] = (uint8_t)o;
            }
        }
    }
}

/* (documented in generator.h) */
void sudoku_geom_init(void)
{
    gen_bread_tables();                                 /* (Sandwich sums; before any task uses them) */
    if (s_geom) {
        return;
    }
    gen_edge_tables();
    sudoku_geom_t *g = calloc(VAR_COUNT, sizeof(sudoku_geom_t));
    if (!g) {
        abort();    /* at start-up, with megabytes free */
    }
    geom_build(&g[VAR_CLASSIC], VAR_CLASSIC, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_X], VAR_X, 9, 3, 3, true, false, NULL);
    geom_build(&g[VAR_MINI], VAR_MINI, 6, 2, 3, false, false, NULL);
    geom_build(&g[VAR_KIDS], VAR_KIDS, 4, 2, 2, false, false, NULL);   /* 4x4 for kids: 2x2 boxes, digits 1-4 */
    geom_build(&g[VAR_MINI8], VAR_MINI8, 8, 2, 4, false, false, NULL);   /* 8x8: 2x4 boxes, digits 1-8 */
    geom_build(&g[VAR_HYPER], VAR_HYPER, 9, 3, 3, false, true, NULL);
    geom_build(&g[VAR_KNIGHT], VAR_KNIGHT, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_KING], VAR_KING, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_DISJOINT], VAR_DISJOINT, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_ASTERISK], VAR_ASTERISK, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_CENTERDOT], VAR_CENTERDOT, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_GIRANDOLA], VAR_GIRANDOLA, 9, 3, 3, false, false, NULL);
    /* Anti-Knight: a knight's move away is a peer too; Anti-King: a touching cell (the diagonal
     * ones are new; side by side they share a row or column already) */
    static const int8_t KM[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
    static const int8_t KG[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
    g[VAR_CHESS] = g[VAR_CLASSIC];                  /* Chess Sudoku: both (the king's, then the knight's moves) */
    g[VAR_CHESS].variant = VAR_CHESS;
    for (int v = 0; v < 4; v++) {
        sudoku_geom_t *k = &g[v == 0 ? VAR_KNIGHT : v == 1 ? VAR_KING : VAR_CHESS];
        const int8_t (*M)[2] = v == 1 || v == 2 ? KG : KM;
        for (int c = 0; c < 81; c++) {
            for (int m = 0; m < 8; m++) {
                int r = c / 9 + M[m][0], q = c % 9 + M[m][1];
                if (r < 0 || r > 8 || q < 0 || q > 8) {
                    continue;
                }
                int o = r * 9 + q;
                if (is_sudoku_geom_peer(k, c, o)) {
                    continue;    /* (Anti-King: only the diagonal ones are new) */
                }
                k->xpeers[c][k->nxpeers[c]++] = (uint8_t)o;
                k->peer_bits[c][o >> 5] |= 1u << (o & 31);
                k->peers[c][k->npeers[c]++] = (uint8_t)o;
            }
        }
    }
    g[VAR_ARGYLE] = g[VAR_CLASSIC];                 /* Argyle: the cells on a line see each other */
    g[VAR_ARGYLE].variant = VAR_ARGYLE;
    for (int k = 0; k < ARGYLE_LINES; k++) {
        uint8_t line[9];
        const int n = sudoku_argyle_line(k, line);
        sudoku_geom_t *a = &g[VAR_ARGYLE];
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                const int c = line[i], o = line[j];
                if (i != j && !is_sudoku_geom_peer(a, c, o)) {   /* (a cell on two lines: a peer once) */
                    a->xpeers[c][a->nxpeers[c]++] = (uint8_t)o;
                    a->peer_bits[c][o >> 5] |= 1u << (o & 31);
                    a->peers[c][a->npeers[c]++] = (uint8_t)o;
                }
            }
        }
    }
    g[VAR_NONCONSEC] = g[VAR_CLASSIC];              /* Consecutive with no bars: no neighbours 1 apart */
    g[VAR_NONCONSEC].variant = VAR_NONCONSEC;
    g[VAR_NONCONSEC].consec_on = true;
    g[VAR_MAGIC] = g[VAR_CLASSIC];                  /* the centre box a magic square: 5 in the middle, even corners */
    g[VAR_MAGIC].variant = VAR_MAGIC;
    g[VAR_MAGIC].magic = true;
    for (int r = 3; r < 6; r++) {                   /* (every 3x3 magic square of 1-9 is the Lo Shu, turned or flipped) */
        for (int q = 3; q < 6; q++) {
            const bool mid = r == 4 && q == 4, corner = r != 4 && q != 4;
            g[VAR_MAGIC].allow[r * 9 + q] = mid ? (uint16_t)(1u << 5) : corner ? (uint16_t)0x154 : (uint16_t)0x28A;
        }
    }
    g[VAR_QUADRO] = g[VAR_CLASSIC];                 /* every 2x2 block: an odd and an even digit */
    g[VAR_QUADRO].variant = VAR_QUADRO;
    g[VAR_QUADRO].quadro = true;
    g[VAR_AQ9] = g[VAR_CLASSIC];                    /* no two 9s on a diagonal */
    g[VAR_AQ9].variant = VAR_AQ9;
    g[VAR_AQ9].aq9 = true;
    g[VAR_PSYM] = g[VAR_CLASSIC];                   /* cells mirrored through the centre add up to 10 */
    g[VAR_PSYM].variant = VAR_PSYM;
    g[VAR_PSYM].psym = true;
    g[VAR_PSYM].allow[40] = (uint16_t)(1u << 5);    /* (the centre is its own mirror: 5) */
    g[VAR_FLATMATES] = g[VAR_CLASSIC];              /* every 5 with a 1 right above it or a 9 right below */
    g[VAR_FLATMATES].variant = VAR_FLATMATES;
    g[VAR_FLATMATES].flatmates = true;
    g[VAR_ANTIDIAG] = g[VAR_CLASSIC];               /* at most three different digits on each long diagonal */
    g[VAR_ANTIDIAG].variant = VAR_ANTIDIAG;
    g[VAR_ANTIDIAG].antidiag = true;
    g[VAR_159] = g[VAR_CLASSIC];                    /* columns 1, 5, 9 say where the 1, 5, 9 are */
    g[VAR_159].variant = VAR_159;
    g[VAR_159].idx159 = true;
    g[VAR_DIAGNC] = g[VAR_CLASSIC];                 /* no digits 1 apart corner to corner */
    g[VAR_DIAGNC].variant = VAR_DIAGNC;
    g[VAR_DIAGNC].diag_nc = true;
    g[VAR_EVENODD] = g[VAR_CLASSIC];
    g[VAR_EVENODD].variant = VAR_EVENODD;
    g[VAR_CONSEC] = g[VAR_CLASSIC];
    g[VAR_CONSEC].variant = VAR_CONSEC;
    g[VAR_THERMO] = g[VAR_CLASSIC];
    g[VAR_THERMO].variant = VAR_THERMO;
    g[VAR_SANDWICH] = g[VAR_CLASSIC];
    g[VAR_SANDWICH].variant = VAR_SANDWICH;
    g[VAR_JIGSAW] = g[VAR_CLASSIC];                 /* placeholders: these boards come with their puzzle */
    g[VAR_JIGSAW].variant = VAR_JIGSAW;
    g[VAR_KILLER] = g[VAR_CLASSIC];
    g[VAR_KILLER].variant = VAR_KILLER;
    static const uint8_t OWN[] = {VAR_KROPKI, VAR_XV, VAR_GT, VAR_QUAD, VAR_ARROW, VAR_RENBAN, VAR_WHISPER, VAR_SKY,
                                  VAR_LK, VAR_PALINDROME, VAR_REGIONSUM, VAR_XSUM, VAR_ZIPPER, VAR_FRAME, VAR_BETWEEN,
                                  VAR_ENTROPIC, VAR_MODULAR, VAR_OUTSIDE, VAR_ROSSINI, VAR_DUTCH, VAR_PARITY, VAR_SLOW,
                                  VAR_FORTRESS, VAR_NABNER, VAR_NROOMS, VAR_SKYSUM, VAR_LOCKOUT, VAR_SUMLESS, VAR_DARROW,
                                  VAR_DIAGCONSEC, VAR_TEN, VAR_PRODUCT, VAR_REGIONS, VAR_PILL, VAR_PSKY,
                                  VAR_AVG, VAR_MATHRAX, VAR_B9, VAR_N9, VAR_CLONE, VAR_S9, VAR_BF, VAR_SEQ, VAR_BATTEN
                                 };
    for (size_t i = 0; i < sizeof(OWN); i++) {
        g[OWN[i]] = g[VAR_CLASSIC];
        g[OWN[i]].variant = OWN[i];
    }
    gen_grade_init();
    s_geom = g;
}

/* (documented in generator.h) */
const sudoku_geom_t *sudoku_geom(int variant)
{
    sudoku_geom_init();
    if (variant < 0 || variant >= VAR_COUNT || variant == VAR_JIGSAW || variant == VAR_KILLER ||
            variant == VAR_EVENODD || variant == VAR_CONSEC || variant == VAR_THERMO || variant == VAR_SANDWICH ||
            variant == VAR_KROPKI || variant == VAR_XV || variant == VAR_GT || variant == VAR_QUAD ||
            variant == VAR_ARROW || variant == VAR_RENBAN || variant == VAR_WHISPER || variant == VAR_SKY ||
            variant == VAR_LK || variant == VAR_PALINDROME || variant == VAR_REGIONSUM || variant == VAR_XSUM ||
            variant == VAR_ZIPPER || variant == VAR_FRAME || variant == VAR_BETWEEN || variant == VAR_ENTROPIC ||
            variant == VAR_MODULAR || variant == VAR_OUTSIDE || variant == VAR_ROSSINI || variant == VAR_DUTCH ||
            variant == VAR_PARITY || variant == VAR_SLOW || variant == VAR_FORTRESS || variant == VAR_NABNER ||
            variant == VAR_NROOMS || variant == VAR_SKYSUM || variant == VAR_LOCKOUT || variant == VAR_SUMLESS ||
            variant == VAR_DARROW || variant == VAR_DIAGCONSEC || variant == VAR_TEN ||
            variant == VAR_PRODUCT || variant == VAR_REGIONS || variant == VAR_PILL || variant == VAR_PSKY ||
            variant == VAR_AVG || variant == VAR_MATHRAX || variant == VAR_B9 || variant == VAR_N9 ||
            variant == VAR_CLONE || variant == VAR_S9 || variant == VAR_BF || variant == VAR_SEQ ||
            variant == VAR_BATTEN) {
        variant = VAR_CLASSIC;
    }
    return &s_geom[variant];
}

/**
 * @brief The kind of lines a variant has.
 * @param[in] variant  sudoku_variant_t
 * @return LINE_*
 */
static int line_kind_of(int variant)
{
    return variant == VAR_ARROW ? LINE_ARROW : variant == VAR_RENBAN ? LINE_RENBAN :
           variant == VAR_WHISPER ? LINE_WHISPER : variant == VAR_PALINDROME ? LINE_PALINDROME :
           variant == VAR_REGIONSUM ? LINE_REGIONSUM : variant == VAR_ZIPPER ? LINE_ZIPPER :
           variant == VAR_BETWEEN ? LINE_BETWEEN : variant == VAR_ENTROPIC ? LINE_ENTROPIC :
           variant == VAR_MODULAR ? LINE_MODULAR : variant == VAR_DUTCH ? LINE_DUTCH :
           variant == VAR_PARITY ? LINE_PARITY : variant == VAR_SLOW ? LINE_SLOW : variant == VAR_NABNER ? LINE_NABNER :
           variant == VAR_LOCKOUT ? LINE_LOCKOUT : variant == VAR_DARROW ? LINE_DARROW :
           variant == VAR_TEN ? LINE_TEN : variant == VAR_PILL ? LINE_PILL :
           variant == VAR_AVG ? LINE_AVG : variant == VAR_SEQ ? LINE_SEQ : LINE_THERMO;
}

/* (documented in gen_internal.h) */
bool gen_geom_lines(int variant, const uint8_t cells[][9], const uint8_t *lens, int n, sudoku_geom_t *out)
{
    if (n < 1 || n > THERMOS_MAX) {
        return false;
    }
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = (uint8_t)variant;
    out->line_kind = (uint8_t)line_kind_of(variant);
    out->nthermos = (uint8_t)n;
    for (int t = 0; t < n; t++) {
        if (lens[t] < 2 || lens[t] > 9) {
            return false;
        }
        out->thermo_n[t] = lens[t];
        for (int i = 0; i < lens[t]; i++) {
            int c = cells[t][i];
            if (c > 80 || out->thermo_of[c] != THERMO_NONE) {
                return false;
            }
            if (i) {                                     /* a neighbour of the previous cell (diagonals too) */
                int p = cells[t][i - 1], dr = abs(c / 9 - p / 9), dc = abs(c % 9 - p % 9);
                if (dr > 1 || dc > 1) {
                    return false;
                }
            }
            out->thermo_cell[t][i] = (uint8_t)c;
            out->thermo_of[c] = (uint8_t)t;
            out->thermo_pos[c] = (uint8_t)i;
            /* Thermo: the place along it bounds the digit (the bulb's at most 10 - length, and so on);
             * Whispers: no 5 (nothing is 5 or more away from it) */
            if (out->line_kind == LINE_THERMO) {
                out->allow[c] &= (uint16_t)(((1u << (10 - lens[t] + i + 1)) - 1) & ~((1u << (i + 1)) - 1));
            }
            if (out->line_kind == LINE_WHISPER) {
                out->allow[c] &= (uint16_t)~(1u << 5);
            }
        }
        if (out->line_kind == LINE_SLOW) {          /* as Thermo, counting only the steps that must rise */
            int rises = 0, before = 0;
            for (int i = 1; i < lens[t]; i++) {
                rises += sudoku_thermo_step(out, t, i);
            }
            for (int i = 0; i < lens[t]; i++) {
                before += i ? sudoku_thermo_step(out, t, i) : 0;
                int lo = 1 + before, hi = 9 - (rises - before);
                out->allow[cells[t][i]] &= (uint16_t)(((1u << (hi + 1)) - 1) & ~((1u << lo) - 1));
            }
        }
        if (out->line_kind == LINE_PALINDROME) {    /* mirrored cells hold the same digit: they can't see each other */
            for (int i = 0; i < lens[t] / 2; i++) {
                if (is_sudoku_geom_peer(out, cells[t][i], cells[t][lens[t] - 1 - i])) {
                    return false;
                }
            }
        }
        if (out->line_kind == LINE_NABNER && lens[t] > 5) {
            return false;                           /* (no six digits are all 2 or more apart) */
        }
        if ((out->line_kind == LINE_BETWEEN || out->line_kind == LINE_ENTROPIC || out->line_kind == LINE_MODULAR ||
                out->line_kind == LINE_LOCKOUT || out->line_kind == LINE_DARROW) && lens[t] < 3) {
            return false;                           /* (two circles and something between them) */
        }
        if (out->line_kind == LINE_PILL) {          /* the pill: two cells side by side (or one above the other), then a shaft */
            const int a = cells[t][0], b = cells[t][1];
            if (lens[t] < 3 || !((b == a + 1 && a % 9 < 8) || b == a + 9)) {
                return false;
            }
            out->allow[a] &= (uint16_t)0x7E;        /* (the tens: a shaft of 7 at most adds up to 63 at most) */
        }
        if (out->line_kind == LINE_LOCKOUT) {       /* the diamonds 4+ apart always enclose 5: never between them */
            for (int i = 1; i < lens[t] - 1; i++) {
                out->allow[cells[t][i]] &= (uint16_t)~(1u << 5);
            }
        }
        if (out->line_kind == LINE_BETWEEN) {       /* the most cells between the circles in one row, column or box */
            int need = 1;
            for (int i = 1; i < lens[t] - 1; i++) {
                int same[3] = {0, 0, 0};
                for (int j = 1; j < lens[t] - 1; j++) {
                    int a = cells[t][i], b = cells[t][j];
                    same[0] += a / 9 == b / 9;
                    same[1] += a % 9 == b % 9;
                    same[2] += out->region[a] == out->region[b];
                }
                for (int k = 0; k < 3; k++) {
                    need = same[k] > need ? same[k] : need;
                }
            }
            out->between_need[t] = (uint8_t)need;
        }
        if (out->line_kind == LINE_ZIPPER && lens[t] % 2) {   /* odd: the middle is a pair's sum, 2 at least */
            out->allow[cells[t][lens[t] / 2]] &= (uint16_t)~(1u << 1);
        }
        if (out->line_kind == LINE_REGIONSUM) {     /* in two boxes at least */
            bool two = false;
            for (int i = 1; i < lens[t]; i++) {
                two |= out->region[cells[t][i]] != out->region[cells[t][0]];
            }
            if (!two) {
                return false;
            }
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_sandwich(const uint8_t sums[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_SANDWICH;
    out->sandwich = true;
    for (int i = 0; i < 18; i++) {
        if (sums[i] > 35) {
            return false;    /* 2+3+...+8 */
        }
        out->sandwich_sum[i] = sums[i];
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_sky(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_SKY;
    out->skyscraper = true;
    for (int lu = 0; lu < 18; lu++) {
        int c = clues[lu], cells[9];
        if (c > 9) {
            return false;
        }
        out->sky[lu] = (uint8_t)c;
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        /* the k-th building from the clue is at most k + 10 - c (k counted from 0): the rest
         * seen behind it must each be taller; with 1 seen the first is the 9, with 9 seen 1..9 */
        for (int k = 0; k < 9; k++) {
            int hi = k + 10 - c;
            if (hi < 9) {
                out->allow[cells[k]] &= (uint16_t)((1u << (hi + 1)) - 1);
            }
            if (c == 9) {
                out->allow[cells[k]] &= (uint16_t)(1u << (k + 1));
            }
        }
        if (c == 1) {
            out->allow[cells[0]] &= (uint16_t)(1u << 9);
        }
    }
    return true;
}

/**
 * @brief X-Sums: the first digits a clue allows, worked out (sudoku_xsum_firsts() keeps them in a table)
 * @param[in] clue  the clue, 0..45
 * @return bit x = x can be first
 */
static uint16_t xsum_firsts_calc(int clue)
{
    uint16_t m = 0;
    for (int x = 1; x <= 9; x++) {                  /* x, and x - 1 more digits from 1..9 without x */
        int lo = x, hi = x;
        for (int k = 0, d = 1; k < x - 1; d++) {
            if (d != x) {
                lo += d;
                k++;
            }
        }
        for (int k = 0, d = 9; k < x - 1; d--) {
            if (d != x) {
                hi += d;
                k++;
            }
        }
        if (clue >= lo && clue <= hi) {
            m |= (uint16_t)(1u << x);
        }
    }
    return m;
}

/* (documented in generator.h) */
uint16_t sudoku_xsum_firsts(int clue)
{
    static uint16_t table[46];                      /* (the solver asks millions of times a puzzle) */
    static bool made;
    if (!made) {                                    /* (two tasks making it at once write the same values) */
        for (int c = 0; c < 46; c++) {
            table[c] = xsum_firsts_calc(c);
        }
        made = true;
    }
    return clue >= 0 && clue < 46 ? table[clue] : 0;
}

/* (documented in gen_internal.h) */
bool gen_geom_frame(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_FRAME;
    out->frame = true;
    for (int lu = 0; lu < 18; lu++) {
        int c = clues[lu], cells[9];
        if (c && (c < 6 || c > 24)) {               /* three different digits: 1 + 2 + 3 to 7 + 8 + 9 */
            return false;
        }
        out->frame_sum[lu] = (uint8_t)c;
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        uint16_t m = 0;                             /* the digits of the first three: one with two others */
        for (int a = 1; a <= 9; a++) {
            for (int b = 1; b <= 9; b++) {
                int d = c - a - b;
                if (a != b && d >= 1 && d <= 9 && d != a && d != b) {
                    m |= (uint16_t)(1u << a);
                }
            }
        }
        for (int k = 0; k < 3; k++) {
            out->allow[cells[k]] &= m;
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_outside(const uint16_t sets[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_OUTSIDE;
    out->outside = true;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9];
        if ((sets[lu] & ~0x3FEu) || __builtin_popcount(sets[lu]) > 3) {
            return false;
        }
        out->outside_set[lu] = sets[lu];
        line_cells(lu, cells);
        for (int k = 3; k < 9; k++) {               /* its digits are among the first three: not further on */
            out->allow[cells[k]] &= (uint16_t)~sets[lu];
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_rossini(const uint8_t dirs[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_ROSSINI;
    out->rossini = true;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9];
        if (dirs[lu] > ROSSINI_DOWN) {
            return false;
        }
        out->rossini_dir[lu] = dirs[lu];
        if (dirs[lu] == ROSSINI_NONE) {
            continue;
        }
        line_cells(lu, cells);
        for (int k = 0; k < 3; k++) {               /* rising: the k-th is k+1 .. 7+k; falling: the other way */
            int at = dirs[lu] == ROSSINI_UP ? k : 2 - k;
            out->allow[cells[k]] &= (uint16_t)(((1u << (8 + at)) - 1) & ~((1u << (at + 1)) - 1));
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_nroom(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_NROOMS;
    out->nrooms = true;
    for (int lu = 0; lu < 18; lu++) {
        int c = clues[lu], cells[9];
        if (c > 9) {
            return false;
        }
        out->nroom[lu] = (uint8_t)c;
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        /* the first digit X points at the clue's cell: X = 1 only for a clue of 1 (the first cell is it),
         * and never the clue itself otherwise (the X-th cell holds it, not the first) */
        out->allow[cells[0]] &= c == 1 ? (uint16_t)0x3FE : (uint16_t)~((1u << 1) | (1u << c));
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_n9(const uint8_t sets[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_N9;
    out->n9 = true;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9];
        if (__builtin_popcount(sets[lu]) > 2) {
            return false;                           /* (a 9 has two cells beside it at most) */
        }
        out->n9_set[lu] = sets[lu];
        if (__builtin_popcount(sets[lu]) == 1) {    /* one digit beside it: the 9 is at an end of the line */
            line_cells(lu, cells);
            for (int k = 1; k < 8; k++) {
                out->allow[cells[k]] &= (uint16_t)~(1u << 9);
            }
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_clone(int n, const uint8_t cells[][9], const uint8_t *lens, const int8_t *dr, const int8_t *dc,
                    sudoku_geom_t *out)
{
    if (n < 1 || n > CLONE_MAX) {
        return false;
    }
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_CLONE;
    for (int p = 0; p < n; p++) {
        if (lens[p] < 1 || lens[p] > 9 || (!dr[p] && !dc[p])) {
            return false;
        }
        out->clone_n[p] = lens[p];
        out->clone_dr[p] = dr[p];
        out->clone_dc[p] = dc[p];
        for (int k = 0; k < lens[p]; k++) {
            const int a = cells[p][k], r = a / 9 + dr[p], c = a % 9 + dc[p];
            if (a > 80 || r < 0 || r > 8 || c < 0 || c > 8) {
                return false;
            }
            const int b = r * 9 + c;
            if (out->clone_pair[a] != CLONE_NONE || out->clone_pair[b] != CLONE_NONE ||
                    is_sudoku_geom_peer(out, a, b)) {
                return false;                       /* (overlapping, or the two would see each other) */
            }
            out->clone_cell[p][k] = (uint8_t)a;
            out->clone_of[a] = (uint8_t)b;
            out->clone_of[b] = (uint8_t)a;
            out->clone_pair[a] = out->clone_pair[b] = (uint8_t)p;
        }
    }
    out->nclone = (uint8_t)n;
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_s9(const uint8_t dirs[81], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_S9;
    out->s9 = true;
    for (int c = 0; c < 81; c++) {
        if (dirs[c] == S9_NONE) {
            continue;
        }
        if (dirs[c] > S9_LEFT || sudoku_s9_cell(c, dirs[c], 1) < 0) {
            return false;                           /* (not a way, or nothing that way) */
        }
        out->s9_dir[c] = dirs[c];
        uint16_t m = 0;                             /* its digit: how far the 9 is, so never 9, never past the edge */
        for (int k = 1; k <= 8 && sudoku_s9_cell(c, dirs[c], k) >= 0; k++) {
            m |= (uint16_t)(1u << k);
        }
        out->allow[c] &= m;
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_batten(const uint8_t bat[81], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_BATTEN;
    out->batten = true;
    for (int c = 0; c < 81; c++) {
        if (bat[c] && (c % 9 == 8 || c >= 72 || bat[c] > 1)) {
            return false;                           /* (no corner there with four cells round it) */
        }
        out->bat[c] = bat[c];
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_bf(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_BF;
    out->bf = true;
    for (int lu = 0; lu < 18; lu++) {
        if (clues[lu] > 46) {
            return false;                           /* (a sum of 45 at most) */
        }
        out->bf_clue[lu] = clues[lu];
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_b9(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_B9;
    out->b9 = true;
    for (int lu = 0; lu < 18; lu++) {
        int c = clues[lu], cells[9];
        if (c > 36) {
            return false;                           /* (at most the eight digits other than 9: 36) */
        }
        out->b9_sum[lu] = (uint8_t)c;
        if (c) {                                    /* a clue: something comes before the 9 */
            line_cells(lu, cells);
            out->allow[cells[0]] &= (uint16_t)~(1u << 9);
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_skysum(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_SKYSUM;
    out->skysum = true;
    for (int lu = 0; lu < 18; lu++) {
        int c = clues[lu], cells[9];
        if (c && (c < 9 || c > 45)) {
            return false;                           /* (the 9 alone .. 1 + 2 + ... + 9) */
        }
        out->sky_sum[lu] = (uint8_t)c;
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        if (c == 9) {                               /* only the 9 seen: it's first */
            out->allow[cells[0]] &= (uint16_t)(1u << 9);
        } else {
            out->allow[cells[0]] &= (uint16_t)~(1u << 9);   /* a 9 first would be all that's seen */
            if (c - 9 < 9) {                        /* the first is seen: at most the clue less the 9 */
                out->allow[cells[0]] &= (uint16_t)((1u << (c - 9 + 1)) - 1);
            }
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_psky(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_PSKY;
    out->psky = true;
    for (int lu = 0; lu < 18; lu++) {
        const int c = clues[lu], n = c & ~PSKY_EVEN;
        if (c && ((c & ~(PSKY_EVEN | 0xF)) || ((c & PSKY_EVEN) ? n > 4 : n < 1 || n > 5))) {
            return false;                           /* (odd: the 9 at least, 1 3 5 7 9 at most; even: 0..4) */
        }
        out->psky_clue[lu] = (uint8_t)c;
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_xsum(const uint8_t clues[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_XSUM;
    out->xsums = true;
    for (int lu = 0; lu < 18; lu++) {
        int c = clues[lu], cells[9];
        if (c > 45) {
            return false;
        }
        out->xsum[lu] = (uint8_t)c;
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        uint16_t first = sudoku_xsum_firsts(c);     /* the first digit: an X whose X digits can make the clue */
        if (!first) {
            return false;
        }
        out->allow[cells[0]] &= first;
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_lk(int n, const bool *top, const uint8_t *idx, const uint8_t *dir, const uint8_t *sum,
                 sudoku_geom_t *out)
{
    if (n < 1 || n > LK_MAX) {
        return false;
    }
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_LK;
    memset(out->lk_of, LK_NONE, sizeof(out->lk_of));
    for (int k = 0; k < n; k++) {
        if (idx[k] > 8 || dir[k] > LK_UP_RIGHT || (top[k] && dir[k] == LK_UP_RIGHT) ||
                (!top[k] && dir[k] == LK_DOWN_LEFT) || (!top[k] && idx[k] == 0 && dir[k] == LK_DOWN_RIGHT)) {
            return false;
        }
        int r = top[k] ? 0 : idx[k], c = top[k] ? idx[k] : 0, dr = dir[k] == LK_UP_RIGHT ? -1 : 1;
        int dc = dir[k] == LK_DOWN_LEFT ? -1 : 1, len = 0, slot = dir[k] == LK_DOWN_RIGHT ? 0 : 1;
        for (; r >= 0 && r < 9 && c >= 0 && c < 9; r += dr, c += dc) {
            int cell = r * 9 + c;
            if (out->lk_of[cell][slot] != LK_NONE) {
                return false;    /* (a diagonal twice) */
            }
            out->lk_cell[k][len++] = (uint8_t)cell;
            out->lk_of[cell][slot] = (uint8_t)k;
        }
        if (len < 2 || sum[k] < 1 || sum[k] > 9 * len) {
            return false;
        }
        out->lk_n[k] = (uint8_t)len;
        out->lk_dir[k] = dir[k];
        out->lk_sum[k] = sum[k];
        out->lk_from_top[k] = top[k];
    }
    out->nlk = (uint8_t)n;
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_quad(const uint8_t *at, const uint8_t *n, const uint8_t (*dig)[4], int nq, sudoku_geom_t *out)
{
    if (nq > QUADS_MAX) {
        return false;
    }
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_QUAD;
    memset(out->quad_of, QUAD_NONE, sizeof(out->quad_of));
    for (int q = 0; q < nq; q++) {
        if (at[q] > 79 || at[q] % 9 == 8 || n[q] < 1 || n[q] > 4) {
            return false;
        }
        for (int p = 0; p < q; p++) {
            if (at[p] == at[q]) {
                return false;
            }
        }
        out->quad_at[q] = at[q];
        out->quad_n[q] = n[q];
        uint8_t d[4] = {0, 0, 0, 0};
        memcpy(d, dig[q], n[q]);                    /* (the unused places stay 0) */
        for (int i = 0; i < n[q]; i++) {            /* sorted */
            if (d[i] < 1 || d[i] > 9) {
                return false;
            }
            for (int j = i + 1; j < n[q]; j++) {
                if (d[j] < d[i]) {
                    uint8_t t = d[i];
                    d[i] = d[j];
                    d[j] = t;
                }
            }
        }
        int cnt[10] = {0};
        for (int i = 0; i < n[q]; i++) {
            if (++cnt[d[i]] > 2) {
                return false;    /* (a digit fits twice at most) */
            }
        }
        memcpy(out->quad_dig[q], d, 4);
        const int cells[4] = {at[q], at[q] + 1, at[q] + 9, at[q] + 10};
        for (int k = 0; k < 4; k++) {
            int s = 0;
            while (s < 4 && out->quad_of[cells[k]][s] != QUAD_NONE) {
                s++;
            }
            out->quad_of[cells[k]][s] = (uint8_t)q;
        }
    }
    out->nquads = (uint8_t)nq;
    return true;
}

/* (documented in generator.h) */
bool is_sudoku_mathrax_pair_ok(int op, int val, int a, int b)
{
    switch (op) {
    case MX_SUM:
        return a + b == val;
    case MX_DIFF:
        return a - b == val || b - a == val;
    case MX_PROD:
        return a * b == val;
    case MX_QUOT:
        return a == b * val || b == a * val;
    case MX_EVEN:
        return a % 2 == 0 && b % 2 == 0;
    case MX_ODD:
        return a % 2 == 1 && b % 2 == 1;
    default:
        return false;
    }
}

/* (documented in generator.h) */
uint16_t sudoku_mathrax_partners(int op, int val, int d)
{
    uint16_t m = 0;
    for (int e = 1; e <= 9; e++) {
        if (is_sudoku_mathrax_pair_ok(op, val, d, e)) {
            m |= (uint16_t)(1u << e);
        }
    }
    return m;
}

/* (documented in generator.h) */
void sudoku_mathrax_label(int op, int val, char buf[8])
{
    static const char *const SIGN[] = {"", "+", "\xe2\x88\x92", "\xc3\x97", "\xc3\xb7"};   /* +, minus, times, divide */
    if (op == MX_EVEN || op == MX_ODD) {
        snprintf(buf, 8, "%s", op == MX_EVEN ? "E" : "O");
    } else if (op >= MX_SUM && op <= MX_QUOT) {
        snprintf(buf, 8, "%d%s", val, SIGN[op]);
    } else {
        buf[0] = 0;
    }
}

/* (documented in gen_internal.h) */
bool gen_geom_mathrax(const uint8_t *at, const uint8_t *op, const uint8_t *val, int nc, sudoku_geom_t *out)
{
    if (nc > MX_MAX) {
        return false;
    }
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_MATHRAX;
    for (int q = 0; q < nc; q++) {
        bool numbered = op[q] >= MX_SUM && op[q] <= MX_QUOT;
        if (at[q] > 79 || at[q] % 9 == 8 || op[q] < MX_SUM || op[q] > MX_ODD || (numbered && val[q] < 1) ||
                (!numbered && val[q] != 0) || (op[q] == MX_QUOT && val[q] < 2) || (op[q] == MX_DIFF && val[q] > 8)) {
            return false;
        }
        for (int p = 0; p < q; p++) {
            if (at[p] == at[q]) {
                return false;
            }
        }
        out->mx_at[q] = at[q];
        out->mx_op[q] = op[q];
        out->mx_val[q] = val[q];
        for (int d = 1; d <= 9; d++) {
            out->mx_mask[q][d] = sudoku_mathrax_partners(op[q], val[q], d);
        }
        const int cells[4] = {at[q], at[q] + 1, at[q] + 9, at[q] + 10};
        for (int k = 0; k < 4; k++) {
            int s = 0;
            while (s < 4 && out->mx_of[cells[k]][s] != MX_NONE) {
                s++;
            }
            out->mx_of[cells[k]][s] = (uint8_t)q;
        }
    }
    out->nmx = (uint8_t)nc;
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_killer(const uint8_t cage_of[81], const uint8_t *sums, int ncages, sudoku_geom_t *out)
{
    if (ncages < 1 || ncages > CAGES_MAX) {
        return false;
    }
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    const bool sumless = !sums[0];                  /* every sum 0: Sumless Killer */
    out->variant = sumless ? VAR_SUMLESS : VAR_KILLER;
    out->ncages = (uint8_t)ncages;
    memcpy(out->cage_of, cage_of, 81);
    memset(out->cage_n, 0, sizeof(out->cage_n));
    for (int c = 0; c < 81; c++) {                  /* row by row: the first cell is the top-left one */
        int k = cage_of[c];
        if (k == CAGE_NONE) {
            continue;
        }
        if (k >= ncages || out->cage_n[k] >= 9) {
            return false;
        }
        out->cage_cell[k][out->cage_n[k]++] = (uint8_t)c;
    }
    for (int k = 0; k < ncages; k++) {
        int n = out->cage_n[k];
        if (!n || (sumless ? sums[k] != 0 : sums[k] < n * (n + 1) / 2 || sums[k] > n * (19 - n) / 2)) {
            return false;
        }
        out->cage_sum[k] = sums[k];
        for (int i = 0; i < n; i++) {                /* cage-mates are peers */
            for (int j = 0; j < n; j++) {
                int a = out->cage_cell[k][i], b = out->cage_cell[k][j];
                if (a == b || is_sudoku_geom_peer(out, a, b)) {
                    continue;
                }
                out->peer_bits[a][b >> 5] |= 1u << (b & 31);
                out->peers[a][out->npeers[a]++] = (uint8_t)b;
            }
        }
    }
    return true;
}

/* (documented in gen_internal.h) */
bool gen_geom_product(const uint8_t cage_of[81], const uint16_t *prods, int ncages, sudoku_geom_t *out)
{
    static const uint8_t ZERO[CAGES_MAX];
    if (!gen_geom_killer(cage_of, ZERO, ncages, out)) {   /* (the cages, no sums: their cells differ) */
        return false;
    }
    out->variant = VAR_PRODUCT;
    for (int k = 0; k < ncages; k++) {
        if (!prods[k]) {
            return false;
        }
        out->cage_prod[k] = prods[k];
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_jigsaw(const uint8_t regions[81], sudoku_geom_t *out)
{
    int count[9] = {0};
    for (int c = 0; c < 81; c++) {
        if (regions[c] > 8) {
            return false;
        }
        count[regions[c]]++;
    }
    for (int i = 0; i < 9; i++) {
        if (count[i] != 9) {
            return false;
        }
    }
    geom_build(out, VAR_JIGSAW, 9, 3, 3, false, false, regions);
    return true;
}

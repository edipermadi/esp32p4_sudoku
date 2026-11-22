/**
 * @file gen_geometry.c
 * @ingroup generator
 * @brief The puzzle generator's boards: the names, each variant's board (units, peers, marks,
 *        lines, cages), and the variants' texts read and written.
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
                                                     "Parity Lines", "Slow Thermo"
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
        return mark == MARK_LESS ? a < b : mark == MARK_MORE ? a > b : true;
    default:
        return true;
    }
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
    geom_build(&g[VAR_HYPER], VAR_HYPER, 9, 3, 3, false, true, NULL);
    geom_build(&g[VAR_KNIGHT], VAR_KNIGHT, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_KING], VAR_KING, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_DISJOINT], VAR_DISJOINT, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_ASTERISK], VAR_ASTERISK, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_CENTERDOT], VAR_CENTERDOT, 9, 3, 3, false, false, NULL);
    /* Anti-Knight: a knight's move away is a peer too; Anti-King: a touching cell (the diagonal
     * ones are new; side by side they share a row or column already) */
    static const int8_t KM[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
    static const int8_t KG[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
    for (int v = 0; v < 2; v++) {
        sudoku_geom_t *k = &g[v ? VAR_KING : VAR_KNIGHT];
        const int8_t (*M)[2] = v ? KG : KM;
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
    g[VAR_NONCONSEC] = g[VAR_CLASSIC];              /* Consecutive with no bars: no neighbours 1 apart */
    g[VAR_NONCONSEC].variant = VAR_NONCONSEC;
    g[VAR_NONCONSEC].consec_on = true;
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
                                  VAR_ENTROPIC, VAR_MODULAR, VAR_OUTSIDE, VAR_ROSSINI, VAR_DUTCH, VAR_PARITY, VAR_SLOW
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
            variant == VAR_PARITY || variant == VAR_SLOW) {
        variant = VAR_CLASSIC;
    }
    return &s_geom[variant];
}

/* (documented in generator.h) */
bool sudoku_geom_marks(int variant, const char *text, sudoku_geom_t *out)
{
    const int ek = variant == VAR_KROPKI ? EDGE_KROPKI : variant == VAR_XV ? EDGE_XV : variant == VAR_GT ? EDGE_GT : 0;
    if (!text || strlen(text) != 81 || (variant != VAR_EVENODD && variant != VAR_CONSEC && !ek)) {
        return false;
    }
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = (uint8_t)variant;
    out->edge_kind = (uint8_t)ek;
    for (int c = 0; c < 81; c++) {
        char ch = text[c];
        if (ek) {                                   /* the mark to the right + 3 x the one below */
            if (ch < '0' || ch > '8') {
                return false;
            }
            int right = (ch - '0') % 3, down = (ch - '0') / 3;
            if (c % 9 == 8) {
                right = 0;
            }
            if (c / 9 == 8) {
                down = 0;
            }
            out->edge[c] = (uint8_t)(right | down << 2);
        } else if (variant == VAR_EVENODD) {
            if (ch == 'e') {
                out->parity[c] = PARITY_EVEN;
                out->allow[c] = 0x154;                 /* 2 4 6 8 */
            } else if (ch == 'o') {
                out->parity[c] = PARITY_ODD;
                out->allow[c] = 0x2AA;                 /* 1 3 5 7 9 */
            } else if (ch != '.') {
                return false;
            }
        } else {
            if (ch < '0' || ch > '3') {
                return false;
            }
            out->consec[c] = (uint8_t)(ch - '0');
            if (c % 9 == 8) {
                out->consec[c] &= (uint8_t)~CONSEC_RIGHT;
            }
            if (c / 9 == 8) {
                out->consec[c] &= (uint8_t)~CONSEC_DOWN;
            }
        }
    }
    out->consec_on = variant == VAR_CONSEC;
    if (ek == EDGE_GT) {                            /* each cell's range from the chains of signs */
        /* below[c]: the cells c is bigger than, through the signs (one box at a time) */
        uint16_t below[81] = {0};                   /* (bits: the 9 cells of c's box, row by row) */
        for (int c = 0; c < 81; c++) {
            for (int k = 0; k < 2; k++) {
                int o = k ? c + 9 : c + 1, m = k ? EDGE_DOWN(out->edge[c]) : EDGE_RIGHT(out->edge[c]);
                if (!m || o > 80 || (!k && c % 9 == 8) || out->region[c] != out->region[o]) {
                    continue;
                }
                int bc = (c / 9 % 3) * 3 + c % 3, bo = (o / 9 % 3) * 3 + o % 3;   /* places in the box */
                if (m == MARK_MORE) {
                    below[c] |= (uint16_t)(1u << bo);    /* c > o */
                } else {
                    below[o] |= (uint16_t)(1u << bc);    /* o > c */
                }
            }
        }
        for (int round = 0; round < 9; round++) {   /* through the chains (9 cells: 8 steps at most) */
            for (int c = 0; c < 81; c++) {
                int top = c - (c / 9 % 3) * 9 - c % 3;  /* the box's top-left cell */
                for (int p = 0; p < 9; p++) {
                    if (below[c] >> p & 1) {
                        below[c] |= below[top + (p / 3) * 9 + p % 3];
                    }
                }
            }
        }
        for (int c = 0; c < 81; c++) {
            int top = c - (c / 9 % 3) * 9 - c % 3, bc = (c / 9 % 3) * 3 + c % 3, above = 0;
            for (int p = 0; p < 9; p++) {
                above += below[top + (p / 3) * 9 + p % 3] >> bc & 1;
            }
            int lo = 1 + __builtin_popcount(below[c]), hi = 9 - above;
            uint16_t m = 0;
            for (int d = lo; d <= hi; d++) {
                m |= (uint16_t)(1u << d);
            }
            out->allow[c] &= m;
        }
    }
    return true;
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
           variant == VAR_PARITY ? LINE_PARITY : variant == VAR_SLOW ? LINE_SLOW : LINE_THERMO;
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
        if ((out->line_kind == LINE_BETWEEN || out->line_kind == LINE_ENTROPIC || out->line_kind == LINE_MODULAR) &&
                lens[t] < 3) {
            return false;                           /* (two circles and something between them) */
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

/* (documented in generator.h) */
bool sudoku_geom_thermo_text(const char *text, sudoku_geom_t *out)
{
    return sudoku_geom_lines_text(VAR_THERMO, text, out);
}

/* (documented in generator.h) */
bool sudoku_geom_lines_text(int variant, const char *text, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX];
    int n = 0;
    const char *p = text;
    while (p && *p) {
        if (n >= THERMOS_MAX) {
            return false;
        }
        lens[n] = 0;
        for (;;) {
            char *end;
            long c = strtol(p, &end, 10);
            if (end == p || c < 0 || c > 80 || lens[n] >= 9) {
                return false;
            }
            cells[n][lens[n]++] = (uint8_t)c;
            p = end;
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        n++;
        if (*p == ';') {
            p++;
        } else if (*p) {
            return false;
        }
    }
    return gen_geom_lines(variant, cells, lens, n, out);
}

/* (documented in generator.h) */
bool sudoku_thermos_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) {
        return false;
    }
    buf[0] = 0;
    for (int t = 0; t < g->nthermos; t++) {
        for (int i = 0; i < g->thermo_n[t]; i++) {
            int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : (t ? ";" : ""), g->thermo_cell[t][i]);
            if (w < 0 || w >= len - pos) {
                return false;
            }
            pos += w;
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

/* (documented in generator.h) */
bool sudoku_geom_sky_text(const char *text, sudoku_geom_t *out)
{
    uint8_t clues[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        char *end;
        long v = p ? strtol(p, &end, 10) : -1;
        if (!p || end == p || v < 0 || v > 9) {
            return false;
        }
        clues[i] = (uint8_t)v;
        p = end;
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0 && gen_geom_sky(clues, out);
}

/* (documented in generator.h) */
bool sudoku_sky_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->sky[i]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
uint16_t sudoku_xsum_firsts(int clue)
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

/* (documented in generator.h) */
bool sudoku_geom_outside_text(const char *text, sudoku_geom_t *out)
{
    uint16_t sets[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        if (!p) {
            return false;
        }
        sets[i] = 0;
        if (*p == '0') {
            p++;
        } else {
            int n = 0;
            while (*p >= '1' && *p <= '9') {
                uint16_t b = (uint16_t)(1u << (*p - '0'));
                if ((sets[i] & b) || ++n > 3) {
                    return false;
                }
                sets[i] |= b;
                p++;
            }
            if (!n) {
                return false;
            }
        }
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0 && gen_geom_outside(sets, out);
}

/* (documented in generator.h) */
bool sudoku_outside_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        char d[10];
        int k = 0;
        for (int x = 1; x <= 9; x++) {
            if (g->outside_set[i] >> x & 1) {
                d[k++] = (char)('0' + x);
            }
        }
        if (!k) {
            d[k++] = '0';
        }
        d[k] = 0;
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%s", i ? "," : "", d);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
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

/* (documented in generator.h) */
bool sudoku_geom_rossini_text(const char *text, sudoku_geom_t *out)
{
    uint8_t dirs[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        if (!p || *p < '0' || *p > '2') {
            return false;
        }
        dirs[i] = (uint8_t)(*p++ - '0');
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0 && gen_geom_rossini(dirs, out);
}

/* (documented in generator.h) */
bool sudoku_rossini_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->rossini_dir[i]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_frame_text(const char *text, sudoku_geom_t *out)
{
    uint8_t clues[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        char *end;
        long v = p ? strtol(p, &end, 10) : -1;
        if (!p || end == p || v < 0 || v > 24) {
            return false;
        }
        clues[i] = (uint8_t)v;
        p = end;
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0 && gen_geom_frame(clues, out);
}

/* (documented in generator.h) */
bool sudoku_frame_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->frame_sum[i]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
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

/* (documented in generator.h) */
bool sudoku_geom_xsum_text(const char *text, sudoku_geom_t *out)
{
    uint8_t clues[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        char *end;
        long v = p ? strtol(p, &end, 10) : -1;
        if (!p || end == p || v < 0 || v > 45) {
            return false;
        }
        clues[i] = (uint8_t)v;
        p = end;
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0 && gen_geom_xsum(clues, out);
}

/* (documented in generator.h) */
bool sudoku_xsum_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->xsum[i]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
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

/* (documented in generator.h) */
bool sudoku_geom_lk_text(const char *text, sudoku_geom_t *out)
{
    bool top[LK_MAX];
    uint8_t idx[LK_MAX], dir[LK_MAX], sum[LK_MAX];
    int n = 0;
    const char *p = text;
    while (p && *p) {
        if (n >= LK_MAX || (*p != 't' && *p != 'l')) {
            return false;
        }
        top[n] = *p++ == 't';
        char *end;
        long i = strtol(p, &end, 10);
        if (end == p || *end != ',' || i < 0 || i > 8) {
            return false;
        }
        p = end + 1;
        if (*p != 'r' && *p != 'l' && *p != 'u') {
            return false;
        }
        dir[n] = *p == 'r' ? LK_DOWN_RIGHT : *p == 'l' ? LK_DOWN_LEFT : LK_UP_RIGHT;
        p++;
        if (*p++ != ',') {
            return false;
        }
        long s = strtol(p, &end, 10);
        if (end == p || s < 1 || s > 81) {
            return false;
        }
        p = end;
        idx[n] = (uint8_t)i;
        sum[n++] = (uint8_t)s;
        if (*p == ';') {
            p++;
        } else if (*p) {
            return false;
        }
    }
    return n > 0 && gen_geom_lk(n, top, idx, dir, sum, out);
}

/* (documented in generator.h) */
bool sudoku_lk_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) {
        return false;
    }
    buf[0] = 0;
    for (int k = 0; k < g->nlk; k++) {
        int first = g->lk_cell[k][0], i = g->lk_from_top[k] ? first % 9 : first / 9;
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%c%d,%c,%d", k ? ";" : "", g->lk_from_top[k] ? 't' : 'l', i,
                         "rlu"[g->lk_dir[k]], g->lk_sum[k]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_sandwich_text(const char *text, sudoku_geom_t *out)
{
    gen_bread_tables();
    uint8_t sums[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        char *end;
        long v = p ? strtol(p, &end, 10) : -1;
        if (!p || end == p || v < 0 || v > 35) {
            return false;
        }
        sums[i] = (uint8_t)v;
        p = end;
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0 && gen_geom_sandwich(sums, out);
}

/* (documented in generator.h) */
bool sudoku_sandwich_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->sandwich_sum[i]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_marks_text(const sudoku_geom_t *g, char *buf, int len)
{
    if (len < 82) {
        return false;
    }
    for (int c = 0; c < 81; c++) {
        if (g->edge_kind) {
            buf[c] = (char)('0' + EDGE_RIGHT(g->edge[c]) + 3 * EDGE_DOWN(g->edge[c]));
        } else if (g->variant == VAR_CONSEC) {
            buf[c] = (char)('0' + (g->consec[c] & 3));
        } else {
            buf[c] = g->parity[c] == PARITY_EVEN ? 'e' : g->parity[c] == PARITY_ODD ? 'o' : '.';
        }
    }
    buf[81] = 0;
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
bool sudoku_geom_quad_text(const char *text, sudoku_geom_t *out)
{
    uint8_t at[QUADS_MAX], n[QUADS_MAX], dig[QUADS_MAX][4];
    int nq = 0;
    const char *p = text;
    while (p && *p) {
        if (nq >= QUADS_MAX) {
            return false;
        }
        char *end;
        long c = strtol(p, &end, 10);
        if (end == p || *end != ':' || c < 0 || c > 79) {
            return false;
        }
        p = end + 1;
        at[nq] = (uint8_t)c;
        n[nq] = 0;
        memset(dig[nq], 0, 4);
        while (*p >= '1' && *p <= '9') {
            if (n[nq] >= 4) {
                return false;
            }
            dig[nq][n[nq]++] = (uint8_t)(*p++ - '0');
        }
        if (!n[nq]) {
            return false;
        }
        nq++;
        if (*p == ';') {
            p++;
        } else if (*p) {
            return false;
        }
    }
    return nq > 0 && gen_geom_quad(at, n, (const uint8_t (*)[4])dig, nq, out);
}

/* (documented in generator.h) */
bool sudoku_quads_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) {
        return false;
    }
    buf[0] = 0;
    for (int q = 0; q < g->nquads; q++) {
        char d[5] = "";
        for (int k = 0; k < g->quad_n[q]; k++) {
            d[k] = (char)('0' + g->quad_dig[q][k]);
        }
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d:%s", q ? ";" : "", g->quad_at[q], d);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
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
    out->variant = VAR_KILLER;
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
        if (!n || sums[k] < n * (n + 1) / 2 || sums[k] > n * (19 - n) / 2) {
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

/* (documented in generator.h) */
bool sudoku_geom_killer_text(const char *text, sudoku_geom_t *out)
{
    uint8_t cage_of[81], sums[CAGES_MAX];
    memset(cage_of, CAGE_NONE, sizeof(cage_of));
    int n = 0;
    const char *p = text;
    while (p && *p) {
        if (n >= CAGES_MAX) {
            return false;
        }
        char *end;
        long sum = strtol(p, &end, 10);
        if (end == p || *end != ':' || sum < 1 || sum > 45) {
            return false;
        }
        p = end + 1;
        int cells = 0;
        for (;;) {
            long c = strtol(p, &end, 10);
            if (end == p || c < 0 || c > 80 || cage_of[c] != CAGE_NONE) {
                return false;
            }
            cage_of[c] = (uint8_t)n;
            cells++;
            p = end;
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        sums[n++] = (uint8_t)sum;
        if (*p == ';') {
            p++;
        } else if (*p) {
            return false;
        }
    }
    return gen_geom_killer(cage_of, sums, n, out);
}

/* (documented in generator.h) */
bool sudoku_cages_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) {
        return false;
    }
    buf[0] = 0;
    for (int k = 0; k < g->ncages; k++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d:", k ? ";" : "", g->cage_sum[k]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
        for (int i = 0; i < g->cage_n[k]; i++) {
            w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->cage_cell[k][i]);
            if (w < 0 || w >= len - pos) {
                return false;
            }
            pos += w;
        }
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

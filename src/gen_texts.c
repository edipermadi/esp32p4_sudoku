/**
 * @file gen_texts.c
 * @ingroup generator
 * @brief The variants' layouts as text, read and written: marks, cages, circles, lines, outside clues, Little
 *        Killer diagonals (what sudoku.db, .sdx lines and Share keep them as).
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* (documented in generator.h) */
bool sudoku_geom_marks(int variant, const char *text, sudoku_geom_t *out)
{
    const int ek = variant == VAR_KROPKI ? EDGE_KROPKI : variant == VAR_XV ? EDGE_XV : variant == VAR_GT ? EDGE_GT : 0;
    if (!text || strlen(text) != 81 || (variant != VAR_EVENODD && variant != VAR_CONSEC && variant != VAR_FORTRESS &&
                                        variant != VAR_DIAGCONSEC && !ek)) {
        return false;
    }
    if (variant == VAR_DIAGCONSEC) {                /* corner marks; unmarked corners aren't 1 apart */
        *out = *sudoku_geom(VAR_DIAGNC);
        out->variant = VAR_DIAGCONSEC;
        for (int c = 0; c < 81; c++) {
            if (text[c] < '0' || text[c] > '3') {
                return false;
            }
            out->dmark[c] = (uint8_t)(text[c] - '0');
            if (c % 9 == 8 || c >= 72) {
                out->dmark[c] &= (uint8_t)~DMARK_DR;
            }
            if (c % 9 == 0 || c >= 72) {
                out->dmark[c] &= (uint8_t)~DMARK_DL;
            }
        }
        return true;
    }
    *out = *sudoku_geom(VAR_CLASSIC);               /* (made on first use) */
    out->variant = (uint8_t)variant;
    out->edge_kind = (uint8_t)ek;
    if (variant == VAR_FORTRESS) {                  /* the grey cells, then each grey-white side as a sign */
        out->edge_kind = EDGE_FORT;
        for (int c = 0; c < 81; c++) {
            if (text[c] != 'f' && text[c] != '.') {
                return false;
            }
            out->parity[c] = text[c] == 'f' ? PARITY_FORT : PARITY_NONE;
        }
        for (int c = 0; c < 81; c++) {
            int m[2] = {0, 0};
            for (int k = 0; k < 2; k++) {
                const int o = k ? c + 9 : c + 1;
                if ((!k && c % 9 == 8) || o > 80 || !out->parity[c] == !out->parity[o]) {
                    continue;                       /* (two grey or two white: no sign) */
                }
                m[k] = out->parity[c] ? MARK_MORE : MARK_LESS;
                /* a grey cell beats a white one: never 1; a white one next to a grey one: never 9 */
                out->allow[out->parity[c] ? c : o] &= (uint16_t)~(1u << 1);
                out->allow[out->parity[c] ? o : c] &= (uint16_t)~(1u << 9);
            }
            out->edge[c] = (uint8_t)(m[0] | m[1] << 2);
        }
        return true;
    }
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
void sudoku_layout_texts(const sudoku_geom_t *g, char *cages, int cages_len, char marks[82], char *thermos,
                         int thermos_len)
{
    cages[0] = marks[0] = thermos[0] = 0;
    switch (g->variant) {
    case VAR_KILLER:
    case VAR_SUMLESS:
    case VAR_PRODUCT:
    case VAR_REGIONS:
    case VAR_CALC:
        sudoku_cages_text(g, cages, cages_len);
        break;
    case VAR_QUAD:
        sudoku_quads_text(g, cages, cages_len);     /* (where cages go) */
        break;
    case VAR_MATHRAX:
        sudoku_mathrax_text(g, cages, cages_len);   /* (the same) */
        break;
    case VAR_LK:
        sudoku_lk_text(g, cages, cages_len);        /* (the same) */
        break;
    case VAR_SANDWICH:
        sudoku_sandwich_text(g, marks, 82);
        break;
    case VAR_SKY:
        sudoku_sky_text(g, marks, 82);
        break;
    case VAR_XSUM:
        sudoku_xsum_text(g, marks, 82);
        break;
    case VAR_NROOMS:
        sudoku_nroom_text(g, marks, 82);
        break;
    case VAR_SKYSUM:
        sudoku_skysum_text(g, marks, 82);
        break;
    case VAR_B9:
        sudoku_b9_text(g, marks, 82);
        break;
    case VAR_N9:
        sudoku_n9_text(g, marks, 82);
        break;
    case VAR_S9:
        sudoku_s9_text(g, marks, 82);
        break;
    case VAR_BF:
        sudoku_bf_text(g, marks, 82);
        break;
    case VAR_BATTEN:
        sudoku_batten_text(g, marks, 82);
        break;
    case VAR_CLONE:
        sudoku_clone_text(g, cages, cages_len);     /* (where cages go) */
        break;
    case VAR_PSKY:
        sudoku_psky_text(g, marks, 82);
        break;
    case VAR_FRAME:
        sudoku_frame_text(g, marks, 82);
        break;
    case VAR_OUTSIDE:
        sudoku_outside_text(g, marks, 82);
        break;
    case VAR_ROSSINI:
        sudoku_rossini_text(g, marks, 82);
        break;
    default:
        if (g->variant == VAR_EVENODD || g->variant == VAR_CONSEC || g->variant == VAR_DIAGCONSEC || g->edge_kind) {
            sudoku_marks_text(g, marks, 82);
        }
        break;
    }
    if (g->nthermos) {
        sudoku_thermos_text(g, thermos, thermos_len);   /* (any lines) */
    }
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

/**
 * @brief 18 comma-separated numbers (the clues of the rows, then the columns).
 * @param[in] text  the text
 * @param[in] max   the largest a clue may be
 * @param[out] out  gets them
 * @return false if the text isn't 18 numbers 0..max
 */
static bool read_clues18(const char *text, int max, uint8_t out[18])
{
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        char *end;
        long v = p ? strtol(p, &end, 10) : -1;
        if (!p || end == p || v < 0 || v > max) {
            return false;
        }
        out[i] = (uint8_t)v;
        p = end;
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0;
}

/**
 * @brief 18 clues as comma-separated numbers.
 * @param[in] clues  the clues
 * @param[out] buf   gets the text
 * @param[in] len    its size
 * @return false: buf too small
 */
static bool write_clues18(const uint8_t clues[18], char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", clues[i]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_nroom_text(const char *text, sudoku_geom_t *out)
{
    uint8_t clues[18];
    return read_clues18(text, 9, clues) && gen_geom_nroom(clues, out);
}

/* (documented in generator.h) */
bool sudoku_nroom_text(const sudoku_geom_t *g, char *buf, int len)
{
    return write_clues18(g->nroom, buf, len);
}

/* (documented in generator.h) */
bool sudoku_geom_psky_text(const char *text, sudoku_geom_t *out)
{
    uint8_t clues[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        bool even = p && *p == 'e';
        char *end;
        long v = p ? strtol(p + even, &end, 10) : -1;
        if (!p || end == p + even || v < 0 || v > 5) {
            return false;
        }
        clues[i] = (uint8_t)(even ? PSKY_EVEN | v : v);
        p = *end == ',' ? end + 1 : (i == 17 && !*end ? end : NULL);
    }
    return p && !*p && gen_geom_psky(clues, out);
}

/* (documented in generator.h) */
bool sudoku_psky_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        const int c = g->psky_clue[i];
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%s%d", i ? "," : "", (c & PSKY_EVEN) ? "e" : "",
                         c & ~PSKY_EVEN);
        if (w < 0 || pos + w >= len) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_n9_text(const char *text, sudoku_geom_t *out)
{
    uint8_t sets[18];
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
            while (*p >= '1' && *p <= '8') {
                const uint8_t b = (uint8_t)(1u << (*p - '1'));
                if ((sets[i] & b) || ++n > 2) {
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
    return *p == 0 && gen_geom_n9(sets, out);
}

/* (documented in generator.h) */
bool sudoku_n9_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        char d[4];
        int k = 0;
        for (int x = 1; x <= 8; x++) {
            if (g->n9_set[i] >> (x - 1) & 1) {
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

/** @brief Search Nine: each arrow's letter, by S9_* ('.' none) */
static const char S9_LETTERS[] = ".urdl";

/* (documented in generator.h) */
bool sudoku_geom_s9_text(const char *text, sudoku_geom_t *out)
{
    uint8_t dirs[81];
    if (!text || strlen(text) != 81) {
        return false;
    }
    for (int c = 0; c < 81; c++) {
        const char *at = text[c] ? strchr(S9_LETTERS, text[c]) : NULL;
        if (!at) {
            return false;
        }
        dirs[c] = (uint8_t)(at - S9_LETTERS);
    }
    return gen_geom_s9(dirs, out);
}

/* (documented in generator.h) */
bool sudoku_s9_text(const sudoku_geom_t *g, char *buf, int len)
{
    if (len < 82) {
        return false;
    }
    for (int c = 0; c < 81; c++) {
        buf[c] = S9_LETTERS[g->s9_dir[c] <= S9_LEFT ? g->s9_dir[c] : 0];
    }
    buf[81] = 0;
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_clone_text(const char *text, sudoku_geom_t *out)
{
    uint8_t cells[CLONE_MAX][9], lens[CLONE_MAX];
    int8_t dr[CLONE_MAX], dc[CLONE_MAX];
    int n = 0;
    const char *p = text;
    while (p && *p) {
        if (n >= CLONE_MAX) {
            return false;
        }
        lens[n] = 0;
        for (;;) {                                  /* the first area's cells */
            char *end;
            long c = strtol(p, &end, 10);
            if (end == p || c < 0 || c > 80 || lens[n] >= 9) {
                return false;
            }
            cells[n][lens[n]++] = (uint8_t)c;
            p = end;
            if (*p == ',') {
                p++;
            } else {
                break;
            }
        }
        if (*p != '>') {
            return false;
        }
        char *end;
        long r = strtol(p + 1, &end, 10);           /* how far its clone is moved */
        if (end == p + 1 || *end != ',' || r < -8 || r > 8) {
            return false;
        }
        p = end + 1;
        long c = strtol(p, &end, 10);
        if (end == p || c < -8 || c > 8) {
            return false;
        }
        dr[n] = (int8_t)r;
        dc[n] = (int8_t)c;
        n++;
        p = end;
        if (*p == ';') {
            p++;
        } else if (*p) {
            return false;
        }
    }
    return n > 0 && gen_geom_clone(n, (const uint8_t (*)[9])cells, lens, dr, dc, out);
}

/* (documented in generator.h) */
bool sudoku_clone_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) {
        return false;
    }
    buf[0] = 0;
    for (int q = 0; q < g->nclone; q++) {
        for (int k = 0; k < g->clone_n[q]; k++) {
            int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", k ? "," : q ? ";" : "", g->clone_cell[q][k]);
            if (w < 0 || w >= len - pos) {
                return false;
            }
            pos += w;
        }
        int w = snprintf(buf + pos, (size_t)(len - pos), ">%d,%d", g->clone_dr[q], g->clone_dc[q]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_batten_text(const char *text, sudoku_geom_t *out)
{
    uint8_t bat[81];
    if (!text || strlen(text) != 81) {
        return false;
    }
    for (int c = 0; c < 81; c++) {
        if (text[c] != '.' && text[c] != 'b') {
            return false;
        }
        bat[c] = text[c] == 'b';
    }
    return gen_geom_batten(bat, out);
}

/* (documented in generator.h) */
bool sudoku_batten_text(const sudoku_geom_t *g, char *buf, int len)
{
    if (len < 82) {
        return false;
    }
    for (int c = 0; c < 81; c++) {
        buf[c] = g->bat[c] ? 'b' : '.';
    }
    buf[81] = 0;
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_bf_text(const char *text, sudoku_geom_t *out)
{
    uint8_t c[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        if (!p) {
            return false;
        }
        if (*p == '.') {
            c[i] = 0;
            p++;
        } else {
            char *end;
            const long v = strtol(p, &end, 10);
            if (end == p || *p == '-' || *p == '+' || v < 0 || v > 45) {
                return false;
            }
            c[i] = (uint8_t)(v + 1);
            p = end;
        }
        if (i < 17) {
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    return *p == 0 && gen_geom_bf(c, out);
}

/* (documented in generator.h) */
bool sudoku_bf_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        char v[4] = ".";
        if (g->bf_clue[i]) {
            snprintf(v, sizeof(v), "%d", g->bf_clue[i] - 1);
        }
        const int w = snprintf(buf + pos, (size_t)(len - pos), "%s%s", i ? "," : "", v);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_b9_text(const char *text, sudoku_geom_t *out)
{
    uint8_t c[18];
    return read_clues18(text, 36, c) && gen_geom_b9(c, out);
}

/* (documented in generator.h) */
bool sudoku_b9_text(const sudoku_geom_t *g, char *buf, int len)
{
    return write_clues18(g->b9_sum, buf, len);
}

/* (documented in generator.h) */
bool sudoku_geom_skysum_text(const char *text, sudoku_geom_t *out)
{
    uint8_t clues[18];
    return read_clues18(text, 45, clues) && gen_geom_skysum(clues, out);
}

/* (documented in generator.h) */
bool sudoku_skysum_text(const sudoku_geom_t *g, char *buf, int len)
{
    return write_clues18(g->sky_sum, buf, len);
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
        if (g->edge_kind == EDGE_FORT) {
            buf[c] = g->parity[c] == PARITY_FORT ? 'f' : '.';
        } else if (g->edge_kind) {
            buf[c] = (char)('0' + EDGE_RIGHT(g->edge[c]) + 3 * EDGE_DOWN(g->edge[c]));
        } else if (g->variant == VAR_CONSEC) {
            buf[c] = (char)('0' + (g->consec[c] & 3));
        } else if (g->variant == VAR_DIAGCONSEC) {
            buf[c] = (char)('0' + (g->dmark[c] & 3));
        } else {
            buf[c] = g->parity[c] == PARITY_EVEN ? 'e' : g->parity[c] == PARITY_ODD ? 'o' : '.';
        }
    }
    buf[81] = 0;
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

/** Mathrax: each operation's letter in the text, MX_* in order ("x" for times, "/" for divided by) */
static const char MX_CHARS[] = "?+-x/EO";

/* (documented in generator.h) */
bool sudoku_geom_mathrax_text(const char *text, sudoku_geom_t *out)
{
    uint8_t at[MX_MAX], op[MX_MAX], val[MX_MAX];
    int nc = 0;
    const char *p = text;
    while (p && *p) {
        if (nc >= MX_MAX) {
            return false;
        }
        char *end;
        long c = strtol(p, &end, 10);
        if (end == p || *end != ':' || c < 0 || c > 79) {
            return false;
        }
        p = end + 1;
        long v = 0;
        if (*p >= '0' && *p <= '9') {
            v = strtol(p, &end, 10);
            p = end;
        }
        const char *k = *p ? strchr(MX_CHARS + 1, *p) : NULL;
        if (!k || v > 81) {
            return false;
        }
        at[nc] = (uint8_t)c;
        op[nc] = (uint8_t)(k - MX_CHARS);
        val[nc] = (uint8_t)v;
        nc++;
        p++;
        if (*p == ';') {
            p++;
        } else if (*p) {
            return false;
        }
    }
    return nc > 0 && gen_geom_mathrax(at, op, val, nc, out);
}

/* (documented in generator.h) */
bool sudoku_mathrax_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) {
        return false;
    }
    buf[0] = 0;
    for (int q = 0; q < g->nmx; q++) {
        const int op = g->mx_op[q];
        int w = op == MX_EVEN || op == MX_ODD
                ? snprintf(buf + pos, (size_t)(len - pos), "%s%d:%c", q ? ";" : "", g->mx_at[q], MX_CHARS[op])
                : snprintf(buf + pos, (size_t)(len - pos), "%s%d:%d%c", q ? ";" : "", g->mx_at[q], g->mx_val[q],
                           MX_CHARS[op]);
        if (w < 0 || w >= len - pos) {
            return false;
        }
        pos += w;
    }
    return true;
}

/**
 * @brief Reads cages as text, "number:cell,cell,...;..." (sudoku_geom_killer_text()'s form)
 * @param[in] text  the cages
 * @param[in] max  the largest number allowed (a sum: 45; a product: 3024)
 * @param[out] cage_of  gets each cell's cage (CAGE_NONE: none)
 * @param[out] nums  gets each cage's number (CAGES_MAX of them)
 * @param[out] ops  gets each cage's operation, MX_SUM .. MX_QUOT, written after its number ("12x:..."); NULL: none
 *             (Calcudoku Killer's only)
 * @param[out] n  gets how many cages
 * @return false if the text isn't that
 */
static bool read_cages(const char *text, long max, uint8_t cage_of[81], uint16_t *nums, uint8_t *ops, int *n)
{
    memset(cage_of, CAGE_NONE, 81);
    *n = 0;
    const char *p = text;
    while (p && *p) {
        if (*n >= CAGES_MAX) {
            return false;
        }
        char *end;
        long sum = strtol(p, &end, 10);
        if (ops) {                                  /* (Calcudoku Killer: its operation after the number) */
            const char *k = *end && *end != ':' ? strchr(MX_CHARS + 1, *end) : NULL;
            if (!k || k - MX_CHARS > MX_QUOT) {
                return false;
            }
            ops[*n] = (uint8_t)(k - MX_CHARS);
            end++;
        }
        if (end == p || *end != ':' || sum < 0 || sum > max) {     /* (0: Sumless Killer's, Extra Regions') */
            return false;
        }
        p = end + 1;
        for (;;) {
            long c = strtol(p, &end, 10);
            if (end == p || c < 0 || c > 80 || cage_of[c] != CAGE_NONE) {
                return false;
            }
            cage_of[c] = (uint8_t)*n;
            p = end;
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        nums[(*n)++] = (uint16_t)sum;
        if (*p == ';') {
            p++;
        } else if (*p) {
            return false;
        }
    }
    return true;
}

/* (documented in generator.h) */
bool sudoku_geom_killer_text(const char *text, sudoku_geom_t *out)
{
    uint8_t cage_of[81], sums[CAGES_MAX];
    uint16_t nums[CAGES_MAX];
    int n;
    if (!read_cages(text, 45, cage_of, nums, NULL, &n)) {
        return false;
    }
    for (int k = 0; k < n; k++) {
        sums[k] = (uint8_t)nums[k];
    }
    return gen_geom_killer(cage_of, sums, n, out);
}

/* (documented in generator.h) */
bool sudoku_geom_product_text(const char *text, sudoku_geom_t *out)
{
    uint8_t cage_of[81];
    uint16_t nums[CAGES_MAX];
    int n;
    return read_cages(text, 60480, cage_of, nums, NULL, &n) && gen_geom_product(cage_of, nums, n, out);
}

/* (documented in generator.h) */
bool sudoku_geom_calc_text(const char *text, sudoku_geom_t *out)
{
    uint8_t cage_of[81], ops[CAGES_MAX];
    uint16_t nums[CAGES_MAX];
    int n;
    return read_cages(text, 60480, cage_of, nums, ops, &n) && gen_geom_calc(cage_of, ops, nums, n, out);
}

/* (documented in generator.h) */
bool sudoku_geom_regions_text(const char *text, sudoku_geom_t *out)
{
    if (!sudoku_geom_killer_text(text, out) || out->variant != VAR_SUMLESS) {
        return false;                               /* (every number 0) */
    }
    out->variant = VAR_REGIONS;
    return true;
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
        const int op = g->cage_op[k];               /* (Calcudoku Killer: the number, then its operation) */
        const int num = op == MX_PROD || g->variant == VAR_PRODUCT ? g->cage_prod[k] : op == MX_DIFF || op == MX_QUOT
                        ? g->cage_dq[k] : g->cage_sum[k];
        int w = op ? snprintf(buf + pos, (size_t)(len - pos), "%s%d%c:", k ? ";" : "", num, MX_CHARS[op])
                : snprintf(buf + pos, (size_t)(len - pos), "%s%d:", k ? ";" : "", num);
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

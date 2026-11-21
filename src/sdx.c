/* See sdx.h. */
#include "sdx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* a name (any case) or its number; -1: neither */
static int name_index(const char *s, const char *(*name)(int), int n)
{
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(s, name(i))) {
            return i;
        }
    }
    char *end;
    long v = strtol(s, &end, 10);
    return *s && !*end && v >= 0 && v < n ? (int)v : -1;
}

bool sdx_parse(char *line, sdx_puzzle_t *p)
{
    memset(p, 0, sizeof(*p));
    p->regions = p->cages = p->marks = p->thermos = "";
    char *fld[7] = {0};
    int nf = 0;
    if (!strchr(line, '|')) {                       /* just the givens (an .sdm line): Classic, no level */
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (strlen(line) < 81) {
            return false;
        }
        char after = line[81];
        if ((after >= '0' && after <= '9') || after == '.') {
            return false;    /* (more than 81 cells) */
        }
        line[81] = 0;
        for (int i = 0; i < 81; i++) {
            if (line[i] >= 'a' && line[i] <= 'z') {
                return false;    /* (packed givens only in a full line) */
            }
        }
        fld[0] = "Classic";
        fld[1] = "";
        fld[2] = line;
        nf = 3;
    } else {
        for (char *q = line; nf < 7;) {
            fld[nf++] = q;
            char *bar = strchr(q, '|');
            if (!bar) {
                break;
            }
            *bar = 0;
            q = bar + 1;
        }
    }
    for (int i = nf; i < 7; i++) {
        fld[i] = "";
    }
    p->variant = name_index(fld[0], variant_name, VAR_COUNT);
    if (p->variant < 0) {
        return false;
    }
    if (!fld[1][0] || !strcmp(fld[1], "-")) {
        p->level = -1;
    } else if ((p->level = name_index(fld[1], gen_level_name, GEN_LEVELS)) < 0) {
        return false;
    }
    int c = 0;                                      /* digits, 0 or . = one empty cell, a-z = 1-26 of them */
    for (const char *q = fld[2]; *q; q++) {
        if (*q >= '1' && *q <= '9') {
            if (c >= 81) {
                return false;
            }
            p->givens[c++] = (uint8_t)(*q - '0');
        } else if (*q == '0' || *q == '.') {
            if (c >= 81) {
                return false;
            }
            p->givens[c++] = 0;
        } else if (*q >= 'a' && *q <= 'z') {
            int run = *q - 'a' + 1;
            if (c + run > 81) {
                return false;
            }
            while (run--) {
                p->givens[c++] = 0;
            }
        } else {
            return false;
        }
    }
    if (c != 81) {
        return false;
    }
    p->regions = fld[3];
    p->cages = fld[4];
    p->marks = fld[5];
    p->thermos = fld[6];
    return true;
}

bool sdx_geom(const sdx_puzzle_t *p, sudoku_geom_t *out)
{
    switch (p->variant) {
    case VAR_JIGSAW: {
        uint8_t reg[81];
        if (strlen(p->regions) != 81) {
            return false;
        }
        for (int i = 0; i < 81; i++) {
            if (p->regions[i] < '0' || p->regions[i] > '8') {
                return false;
            }
            reg[i] = (uint8_t)(p->regions[i] - '0');
        }
        return sudoku_geom_jigsaw(reg, out);
    }
    case VAR_KILLER:
        return sudoku_geom_killer_text(p->cages, out);
    case VAR_QUAD:
        return sudoku_geom_quad_text(p->cages, out);   /* (its circles go where cages do) */
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
        return strlen(p->marks) == 81 && sudoku_geom_marks(p->variant, p->marks, out);
    case VAR_THERMO:
    case VAR_ARROW:
    case VAR_RENBAN:
    case VAR_WHISPER:
        return sudoku_geom_lines_text(p->variant, p->thermos, out);   /* (lines go in thermos) */
    case VAR_SANDWICH:
        return sudoku_geom_sandwich_text(p->marks, out);
    case VAR_SKY:
        return sudoku_geom_sky_text(p->marks, out);            /* (its clues go in marks) */
    case VAR_LK:
        return sudoku_geom_lk_text(p->cages, out);             /* (its sums where cages go) */
    default: {
        if (p->variant < 0 || p->variant >= VAR_COUNT) {
            return false;
        }
        memcpy(out, sudoku_geom(p->variant), sizeof(*out));
        return true;
    }
    }
}

int sdx_pack_givens(const uint8_t givens[81], char *out, size_t n)
{
    size_t k = 0;
    for (int i = 0; i < 81;) {
        if (k + 1 >= n) {
            return -1;
        }
        if (givens[i]) {
            out[k++] = (char)('0' + givens[i++]);
            continue;
        }
        int run = 0;
        while (i < 81 && !givens[i] && run < 26) {
            run++;
            i++;
        }
        out[k++] = (char)('a' + run - 1);
    }
    out[k] = 0;
    return (int)k;
}

int sdx_format(int variant, int level, const uint8_t givens[81], const char *regions, const char *cages,
               const char *marks, const char *thermos, bool pack, char *out, size_t n)
{
    char giv[82], lv[4] = "-";
    if (pack) {
        if (sdx_pack_givens(givens, giv, sizeof(giv)) < 0) {
            return -1;
        }
    } else {
        for (int i = 0; i < 81; i++) {
            giv[i] = (char)('0' + givens[i]);
        }
        giv[81] = 0;
    }
    if (level >= 0 && level < GEN_LEVELS) {
        snprintf(lv, sizeof(lv), "%d", level);
    }
    int len = snprintf(out, n, "%d|%s|%s|%s|%s|%s|%s", variant, lv, giv, regions ? regions : "", cages ? cages : "",
                       marks ? marks : "", thermos ? thermos : "");
    if (len < 0 || (size_t)len >= n) {
        return -1;
    }
    while (len && out[len - 1] == '|') {
        out[--len] = 0;    /* the empty fields at the end */
    }
    return len;
}

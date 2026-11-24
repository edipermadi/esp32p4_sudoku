/**
 * @file share_core.c
 * @ingroup share_core
 * @brief Sharing a puzzle, without LVGL: the QR code's text, the .sdx line sent by sound, and its parts (the
 *        dialogs: ui_share.c).
 */
#include "share_core.h"
#include <stdio.h>
#include <string.h>
#include "sdx.h"
#include "stats_db.h"

/* (documented in share_core.h) */
bool share_text(const sudoku_geom_t *G, int variant, const uint8_t *givens, char *buf, size_t n)
{
    char giv[82];
    int k = 0;
    for (int i = 0; i < G->ncells; i++) {
        giv[k++] = (char)('0' + givens[G->cells[i]]);    /* (6x6: its 36 cells) */
    }
    giv[k] = 0;
    if (variant == VAR_CLASSIC) {
        return (size_t)snprintf(buf, n, "%s", giv) < n;
    }
    int len = snprintf(buf, n, "%s %s", variant_name(variant), giv);
    if (len < 0 || (size_t)len >= n) {
        return false;
    }
    static char extra[STATS_CAGES_LEN];
    extra[0] = 0;
    const char *key = NULL;
    switch (variant) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) {
            extra[i] = (char)('0' + G->region[i]);
        }
        extra[81] = 0;
        key = "regions";
        break;
    case VAR_KILLER:
    case VAR_SUMLESS:
    case VAR_PRODUCT:
    case VAR_REGIONS:
        key = sudoku_cages_text(G, extra, sizeof(extra)) ? "cages" : NULL;
        break;
    case VAR_QUAD:
        key = sudoku_quads_text(G, extra, sizeof(extra)) ? "circles" : NULL;
        break;
    case VAR_MATHRAX:
        key = sudoku_mathrax_text(G, extra, sizeof(extra)) ? "circles" : NULL;
        break;
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
    case VAR_FORTRESS:
    case VAR_DIAGCONSEC:
        key = sudoku_marks_text(G, extra, sizeof(extra)) ? "marks" : NULL;
        break;
    case VAR_THERMO:
    case VAR_SLOW:
        key = sudoku_thermos_text(G, extra, sizeof(extra)) ? "thermos" : NULL;
        break;
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
    case VAR_NABNER:
    case VAR_LOCKOUT:
    case VAR_DARROW:
    case VAR_TEN:
    case VAR_PILL:
    case VAR_AVG:
        key = sudoku_thermos_text(G, extra, sizeof(extra)) ? "lines" : NULL;
        break;
    case VAR_SANDWICH:
        key = sudoku_sandwich_text(G, extra, sizeof(extra)) ? "sums" : NULL;
        break;
    case VAR_SKY:
        key = sudoku_sky_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_XSUM:
        key = sudoku_xsum_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_NROOMS:
        key = sudoku_nroom_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_SKYSUM:
        key = sudoku_skysum_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_B9:
        key = sudoku_b9_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_PSKY:
        key = sudoku_psky_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_FRAME:
        key = sudoku_frame_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_OUTSIDE:
        key = sudoku_outside_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_ROSSINI:
        key = sudoku_rossini_text(G, extra, sizeof(extra)) ? "arrows" : NULL;
        break;
    case VAR_LK:
        key = sudoku_lk_text(G, extra, sizeof(extra)) ? "diagonals" : NULL;
        break;
    default:
        break;
    }
    if (key) {
        len += snprintf(buf + len, n - (size_t)len, " %s=%s", key, extra);
    }
    return (size_t)len < n;
}

/* (documented in share_core.h) */
int share_pz_line(const sudoku_geom_t *G, int variant, int level, const uint8_t *givens, char *buf, size_t n)
{
    static char extra[STATS_CAGES_LEN];
    char regions[82] = "";
    const char *cages = "", *marks = "", *thermos = "";
    extra[0] = 0;
    switch (variant) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) {
            regions[i] = (char)('0' + G->region[i]);
        }
        regions[81] = 0;
        break;
    case VAR_KILLER:
    case VAR_SUMLESS:
    case VAR_PRODUCT:
    case VAR_REGIONS:
        if (sudoku_cages_text(G, extra, sizeof(extra))) {
            cages = extra;
        }
        break;
    case VAR_QUAD:
        if (sudoku_quads_text(G, extra, sizeof(extra))) {
            cages = extra;
        }
        break;
    case VAR_MATHRAX:
        if (sudoku_mathrax_text(G, extra, sizeof(extra))) {
            cages = extra;
        }
        break;
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
    case VAR_FORTRESS:
    case VAR_DIAGCONSEC:
        if (sudoku_marks_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_SANDWICH:
        if (sudoku_sandwich_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_SKY:
        if (sudoku_sky_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_XSUM:
        if (sudoku_xsum_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_NROOMS:
        if (sudoku_nroom_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_SKYSUM:
        if (sudoku_skysum_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_B9:
        if (sudoku_b9_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_PSKY:
        if (sudoku_psky_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_FRAME:
        if (sudoku_frame_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_OUTSIDE:
        if (sudoku_outside_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_ROSSINI:
        if (sudoku_rossini_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_LK:
        if (sudoku_lk_text(G, extra, sizeof(extra))) {
            cages = extra;
        }
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
        if (sudoku_thermos_text(G, extra, sizeof(extra))) {
            thermos = extra;
        }
        break;
    default:
        break;
    }
    int len = sdx_format(variant, level >= 0 && level < GEN_LEVELS ? level : -1, givens, regions, cages, marks, thermos, true,
                         buf, n);
    return len < 0 ? 0 : len;
}

/* (documented in share_core.h) */
uint16_t share_crc16(const char *p, size_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)((uint8_t)*p++ << 8);
        for (int k = 0; k < 8; k++) {
            c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
        }
    }
    return c;
}

/* (documented in share_core.h) */
void share_pz_part(unsigned id, int i, int parts, const char *line, int len, char *msg, size_t n)
{
    char body[160];
    int k = len - i * PZ_PART < PZ_PART ? len - i * PZ_PART : PZ_PART;
    snprintf(body, sizeof(body), "pz %02X %d/%d %.*s", id & 0xFF, i + 1, parts, k, line + i * PZ_PART);
    snprintf(msg, n, PZ_ID "$%s*%04X", body, share_crc16(body, strlen(body)));
}

/* (documented in share_core.h) */
bool share_pz_part_read(const char *args, unsigned *id, int *i, int *parts, const char **data)
{
    int off = 0;
    while (*args == ' ') {
        args++;
    }
    if (sscanf(args, "%x %d/%d %n", id, i, parts, &off) < 3 || !off || *parts < 1 || *parts > PZ_MAX_PARTS ||
            *i < 1 || *i > *parts) {
        return false;
    }
    *data = args + off;
    return strlen(*data) <= PZ_PART;
}

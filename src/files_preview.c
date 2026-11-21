/**
 * @file files_preview.c
 * @ingroup files_preview
 * @brief A puzzle file's preview: the file read and its puzzles found, one parsed at a time.
 */
#include "files_preview.h"
#include "files_names.h"
#include "sdx.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"

static char *s_pv_buf;                  /**< the previewed file, split into lines (PSRAM) */
static char **s_pv_line;                /**< its puzzles' lines */
static int s_pv_n;                      /**< how many */
static sudoku_geom_t *s_pv_geom;        /**< the board of the puzzle parsed last (PSRAM) */

/* ------------------------------------------------------------- a puzzle file's preview */
/**
 * @brief One line (sdx.h: an .sdx line, or just 81 digits as in .sdm) into variant, level, givens
 *        and its board. The same reading as the import's, without the one-solution check.
 * @param src   the line
 * @param var   gets its sudoku_variant_t
 * @param lv    gets its level (-1: none given)
 * @param g     gets its givens (81 cells)
 * @param geom  gets its board
 * @return false if it isn't a puzzle
 */
static bool pv_parse(const char *src, int *var, int *lv, uint8_t g[81], sudoku_geom_t *geom)
{
    static char line[2048];
    snprintf(line, sizeof(line), "%s", src);
    sdx_puzzle_t p;
    if (!sdx_parse(line, &p) || !sdx_geom(&p, geom)) {
        return false;
    }
    *var = p.variant;
    *lv = p.level;
    memcpy(g, p.givens, 81);
    return true;
}

/* (documented in files_preview.h) */
int files_pv_load(const char *path, char *summary, size_t n)
{
    FILE *f = fopen(path, "r");
    s_pv_buf = heap_caps_malloc(FILES_PV_MAX_BYTES + 1, MALLOC_CAP_SPIRAM);
    s_pv_line = heap_caps_malloc(FILES_PV_MAX * sizeof(char *), MALLOC_CAP_SPIRAM);
    if (!s_pv_geom) {
        s_pv_geom = heap_caps_malloc(sizeof(sudoku_geom_t), MALLOC_CAP_SPIRAM);
    }
    if (!f || !s_pv_buf || !s_pv_line || !s_pv_geom) {
        if (f) {
            fclose(f);
        }
        snprintf(summary, n, f ? "Out of memory." : "Couldn't open the file.");
        return 0;
    }
    size_t len = fread(s_pv_buf, 1, FILES_PV_MAX_BYTES, f);
    bool cut = !feof(f);
    fclose(f);
    s_pv_buf[len] = 0;
    int kinds[VAR_COUNT] = {0}, bad = 0;
    if (is_files_name_ending(path, ".opensudoku")) {        /* OpenSudoku's XML: each data="..." (or '...') is a Classic one */
        for (char *q = s_pv_buf; s_pv_n < FILES_PV_MAX && (q = strstr(q, "data=")) != NULL; q += 5) {
            char quote = q[5];
            if (quote != '"' && quote != '\'') {
                continue;
            }
            char *d = q + 6, *end = strchr(d, quote);
            if (!end) {
                break;    /* (cut off at the end) */
            }
            int v, lv;
            uint8_t g[81];
            *end = 0;
            if (end - d == 81 && pv_parse(d, &v, &lv, g, s_pv_geom)) {
                s_pv_line[s_pv_n++] = d;
                kinds[v]++;
            } else {
                bad++;
            }
            q = end;
        }
    }
    for (char *p = s_pv_buf; !is_files_name_ending(path, ".opensudoku") && *p && s_pv_n < FILES_PV_MAX;) {
        char *nl = strchr(p, '\n');
        if (nl) {
            *nl = 0;
        } else if (cut) {
            break;    /* (a line cut off at the end) */
        }
        size_t l = strlen(p);
        while (l && (p[l - 1] == '\r' || p[l - 1] == ' ' || p[l - 1] == '\t')) {
            p[--l] = 0;
        }
        if (l && p[0] != '#') {
            int v, lv;
            uint8_t g[81];
            if (pv_parse(p, &v, &lv, g, s_pv_geom)) {
                s_pv_line[s_pv_n++] = p;
                kinds[v]++;
            } else {
                bad++;
            }
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    /* "42 puzzles: 30 Classic, 12 Killer" (+ the lines left out) */
    int o = snprintf(summary, n, "%d puzzle%s%s", s_pv_n, s_pv_n == 1 ? "" : "s", s_pv_n ? ": " : "");
    for (int v = 0, first = 1; v < VAR_COUNT && o < (int)n; v++) {
        if (!kinds[v]) {
            continue;
        }
        o += snprintf(summary + o, n - o, "%s%d %s", first ? "" : ", ", kinds[v], variant_name(v));
        first = 0;
    }
    if (bad && o < (int)n)
        o += snprintf(summary + o, n - o, "  -  %d %s that %s", bad,
                      is_files_name_ending(path, ".opensudoku") ? (bad == 1 ? "entry" : "entries")
                      : (bad == 1 ? "line" : "lines"),
                      bad == 1 ? "isn't a puzzle" : "aren't puzzles");
    if ((cut || s_pv_n == FILES_PV_MAX) && o < (int)n) {
        snprintf(summary + o, n - o, "  -  the first %d shown", s_pv_n);
    }
    return s_pv_n;
}

/* (documented in files_preview.h) */
void files_pv_free(void)
{
    heap_caps_free(s_pv_buf);
    heap_caps_free(s_pv_line);
    s_pv_buf = NULL;
    s_pv_line = NULL;
    s_pv_n = 0;
}

/* (documented in files_preview.h) */
int files_pv_count(void)
{
    return s_pv_n;
}

/* (documented in files_preview.h) */
bool files_pv_parse(int at, int *var, int *lv, uint8_t g[81])
{
    return pv_parse(s_pv_line[at], var, lv, g, s_pv_geom);
}

/* (documented in files_preview.h) */
const sudoku_geom_t *files_pv_geom(void)
{
    return s_pv_geom;
}

/* See generator.h. */
#include "generator.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

const char *const GEN_LEVEL_NAMES[GEN_LEVELS] = {"Easy", "Medium", "Hard", "Expert"};
const char *const VARIANT_NAMES[VAR_COUNT] = {"Classic", "X-Sudoku", "Mini 6x6", "Hyper", "Jigsaw", "Killer",
                                              "Anti-Knight", "Even-Odd", "Consecutive", "Thermo", "Sandwich"};

/* sum of the digits in a candidate mask (bit d = digit d), for the cage logic */
static uint8_t s_mask_sum[1024];

/* ---------------------------------------------------------------- geometry */

/* ~25 KB: from malloc, which the sketch sends to PSRAM (internal RAM is
 * kept for Wi-Fi and the video encoder) */
static sudoku_geom_t *s_geom;

/* regions: Jigsaw's region map, NULL = boxes of box_h x box_w */
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
        if (diag && r == c) ids[nid++] = UNIT_DIAG_MAIN;
        if (diag && r + c == n - 1) ids[nid++] = UNIT_DIAG_ANTI;   /* centre cell: both (5 units) */
        int w = sudoku_geom_window(g, cell);
        if (w >= 0) ids[nid++] = UNIT_WINDOW + w;
        for (int i = 0; i < nid; i++) {
            int u = ids[i];
            g->unit[u][fill[u]++] = (uint8_t)cell;
            g->active[u] = true;
        }
        for (int i = 0; i < nid; i++) g->cu[cell][i] = (uint8_t)ids[i];
        g->ncu[cell] = (uint8_t)nid;
    }
    for (int u = 0; u < UNITS_MAX; u++) {
        if (g->active[u]) g->unit_ids[g->nunits++] = (uint8_t)u;
    }
    for (int k = 0; k < g->ncells; k++) g->allow[g->cells[k]] = g->all;
    for (int k = 0; k < g->ncells; k++) {                   /* peers: share any unit */
        int cell = g->cells[k];
        for (int i = 0; i < g->nunits; i++) {
            int u = g->unit_ids[i];
            bool in = false;
            for (int j = 0; j < n; j++) in |= g->unit[u][j] == cell;
            if (!in) continue;
            for (int j = 0; j < n; j++) {
                int o = g->unit[u][j];
                if (o == cell || sudoku_geom_peer(g, cell, o)) continue;
                g->peer_bits[cell][o >> 5] |= 1u << (o & 31);
                g->peers[cell][g->npeers[cell]++] = (uint8_t)o;
            }
        }
    }
}

static void bread_tables(void);

void sudoku_geom_init(void)
{
    bread_tables();                                 /* (Sandwich sums; before any task uses them) */
    if (s_geom) return;
    sudoku_geom_t *g = calloc(VAR_COUNT, sizeof(sudoku_geom_t));
    if (!g) abort();                                /* at start-up, with megabytes free */
    geom_build(&g[VAR_CLASSIC], VAR_CLASSIC, 9, 3, 3, false, false, NULL);
    geom_build(&g[VAR_X], VAR_X, 9, 3, 3, true, false, NULL);
    geom_build(&g[VAR_MINI], VAR_MINI, 6, 2, 3, false, false, NULL);
    geom_build(&g[VAR_HYPER], VAR_HYPER, 9, 3, 3, false, true, NULL);
    geom_build(&g[VAR_KNIGHT], VAR_KNIGHT, 9, 3, 3, false, false, NULL);
    for (int c = 0; c < 81; c++) {                  /* Anti-Knight: a knight's move away is a peer too */
        static const int8_t KM[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
        sudoku_geom_t *k = &g[VAR_KNIGHT];
        for (int m = 0; m < 8; m++) {
            int r = c / 9 + KM[m][0], q = c % 9 + KM[m][1];
            if (r < 0 || r > 8 || q < 0 || q > 8) continue;
            int o = r * 9 + q;
            k->xpeers[c][k->nxpeers[c]++] = (uint8_t)o;
            if (sudoku_geom_peer(k, c, o)) continue;
            k->peer_bits[c][o >> 5] |= 1u << (o & 31);
            k->peers[c][k->npeers[c]++] = (uint8_t)o;
        }
    }
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
    for (int m = 0; m < 1024; m++) {
        int sum = 0;
        for (int d = 1; d <= 9; d++) sum += (m >> d & 1) * d;
        s_mask_sum[m] = (uint8_t)sum;
    }
    s_geom = g;
}

const sudoku_geom_t *sudoku_geom(int variant)
{
    sudoku_geom_init();
    if (variant < 0 || variant >= VAR_COUNT || variant == VAR_JIGSAW || variant == VAR_KILLER ||
        variant == VAR_EVENODD || variant == VAR_CONSEC || variant == VAR_THERMO || variant == VAR_SANDWICH) {
        variant = VAR_CLASSIC;
    }
    return &s_geom[variant];
}

bool sudoku_geom_marks(int variant, const char *text, sudoku_geom_t *out)
{
    if (!text || strlen(text) != 81 || (variant != VAR_EVENODD && variant != VAR_CONSEC)) return false;
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = (uint8_t)variant;
    for (int c = 0; c < 81; c++) {
        char ch = text[c];
        if (variant == VAR_EVENODD) {
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
            if (ch < '0' || ch > '3') return false;
            out->consec[c] = (uint8_t)(ch - '0');
            if (c % 9 == 8) out->consec[c] &= (uint8_t)~CONSEC_RIGHT;
            if (c / 9 == 8) out->consec[c] &= (uint8_t)~CONSEC_DOWN;
        }
    }
    out->consec_on = variant == VAR_CONSEC;
    return true;
}

/* Thermo: a classic board plus thermometers (cells from the bulb) */
static bool geom_thermo(const uint8_t cells[][9], const uint8_t *lens, int n, sudoku_geom_t *out)
{
    if (n < 1 || n > THERMOS_MAX) return false;
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_THERMO;
    out->nthermos = (uint8_t)n;
    for (int t = 0; t < n; t++) {
        if (lens[t] < 2 || lens[t] > 9) return false;
        out->thermo_n[t] = lens[t];
        for (int i = 0; i < lens[t]; i++) {
            int c = cells[t][i];
            if (c > 80 || out->thermo_of[c] != THERMO_NONE) return false;
            if (i) {                                     /* a neighbour of the previous cell (diagonals too) */
                int p = cells[t][i - 1], dr = abs(c / 9 - p / 9), dc = abs(c % 9 - p % 9);
                if (dr > 1 || dc > 1) return false;
            }
            out->thermo_cell[t][i] = (uint8_t)c;
            out->thermo_of[c] = (uint8_t)t;
            out->thermo_pos[c] = (uint8_t)i;
            /* the place along it bounds the digit: the bulb's digit is at most 10 - length, and so on */
            out->allow[c] &= (uint16_t)(((1u << (10 - lens[t] + i + 1)) - 1) & ~((1u << (i + 1)) - 1));
        }
    }
    return true;
}

bool sudoku_geom_thermo_text(const char *text, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX];
    int n = 0;
    const char *p = text;
    while (p && *p) {
        if (n >= THERMOS_MAX) return false;
        lens[n] = 0;
        for (;;) {
            char *end;
            long c = strtol(p, &end, 10);
            if (end == p || c < 0 || c > 80 || lens[n] >= 9) return false;
            cells[n][lens[n]++] = (uint8_t)c;
            p = end;
            if (*p == ',') { p++; continue; }
            break;
        }
        n++;
        if (*p == ';') p++;
        else if (*p) return false;
    }
    return geom_thermo(cells, lens, n, out);
}

bool sudoku_thermos_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) return false;
    buf[0] = 0;
    for (int t = 0; t < g->nthermos; t++) {
        for (int i = 0; i < g->thermo_n[t]; i++) {
            int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : (t ? ";" : ""), g->thermo_cell[t][i]);
            if (w < 0 || w >= len - pos) return false;
            pos += w;
        }
    }
    return true;
}

static bool geom_sandwich(const uint8_t sums[18], sudoku_geom_t *out)
{
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_SANDWICH;
    out->sandwich = true;
    for (int i = 0; i < 18; i++) {
        if (sums[i] > 35) return false;             /* 2+3+...+8 */
        out->sandwich_sum[i] = sums[i];
    }
    return true;
}

bool sudoku_geom_sandwich_text(const char *text, sudoku_geom_t *out)
{
    bread_tables();
    uint8_t sums[18];
    const char *p = text;
    for (int i = 0; i < 18; i++) {
        char *end;
        long v = p ? strtol(p, &end, 10) : -1;
        if (!p || end == p || v < 0 || v > 35) return false;
        sums[i] = (uint8_t)v;
        p = end;
        if (i < 17) {
            if (*p != ',') return false;
            p++;
        }
    }
    return *p == 0 && geom_sandwich(sums, out);
}

bool sudoku_sandwich_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    for (int i = 0; i < 18; i++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->sandwich_sum[i]);
        if (w < 0 || w >= len - pos) return false;
        pos += w;
    }
    return true;
}

/* sums of k distinct digits from 2..8: bit s set if s can be made (k 0..7) */
static uint64_t s_bread[8];

static void bread_tables(void)
{
    if (s_bread[0]) return;
    for (int m = 0; m < 128; m++) {                 /* subsets of 2..8 */
        int k = __builtin_popcount(m), sum = 0;
        for (int b = 0; b < 7; b++) if (m >> b & 1) sum += b + 2;
        s_bread[k] |= 1ull << sum;
    }
}

bool sudoku_marks_text(const sudoku_geom_t *g, char *buf, int len)
{
    if (len < 82) return false;
    for (int c = 0; c < 81; c++) {
        if (g->variant == VAR_CONSEC) buf[c] = (char)('0' + (g->consec[c] & 3));
        else buf[c] = g->parity[c] == PARITY_EVEN ? 'e' : g->parity[c] == PARITY_ODD ? 'o' : '.';
    }
    buf[81] = 0;
    return true;
}

/* Killer: a classic board plus cages (cage_of: cage per cell, CAGE_NONE =
 * none; sums per cage). Cage-mates become peers. False if a cage is empty,
 * has more than 9 cells, or its sum can't be made of distinct digits. */
static bool geom_killer(const uint8_t cage_of[81], const uint8_t *sums, int ncages, sudoku_geom_t *out)
{
    if (ncages < 1 || ncages > CAGES_MAX) return false;
    sudoku_geom_init();
    *out = s_geom[VAR_CLASSIC];
    out->variant = VAR_KILLER;
    out->ncages = (uint8_t)ncages;
    memcpy(out->cage_of, cage_of, 81);
    memset(out->cage_n, 0, sizeof(out->cage_n));
    for (int c = 0; c < 81; c++) {                  /* row by row: the first cell is the top-left one */
        int k = cage_of[c];
        if (k == CAGE_NONE) continue;
        if (k >= ncages || out->cage_n[k] >= 9) return false;
        out->cage_cell[k][out->cage_n[k]++] = (uint8_t)c;
    }
    for (int k = 0; k < ncages; k++) {
        int n = out->cage_n[k];
        if (!n || sums[k] < n * (n + 1) / 2 || sums[k] > n * (19 - n) / 2) return false;
        out->cage_sum[k] = sums[k];
        for (int i = 0; i < n; i++) {                /* cage-mates are peers */
            for (int j = 0; j < n; j++) {
                int a = out->cage_cell[k][i], b = out->cage_cell[k][j];
                if (a == b || sudoku_geom_peer(out, a, b)) continue;
                out->peer_bits[a][b >> 5] |= 1u << (b & 31);
                out->peers[a][out->npeers[a]++] = (uint8_t)b;
            }
        }
    }
    return true;
}

bool sudoku_geom_killer_text(const char *text, sudoku_geom_t *out)
{
    uint8_t cage_of[81], sums[CAGES_MAX];
    memset(cage_of, CAGE_NONE, sizeof(cage_of));
    int n = 0;
    const char *p = text;
    while (p && *p) {
        if (n >= CAGES_MAX) return false;
        char *end;
        long sum = strtol(p, &end, 10);
        if (end == p || *end != ':' || sum < 1 || sum > 45) return false;
        p = end + 1;
        int cells = 0;
        for (;;) {
            long c = strtol(p, &end, 10);
            if (end == p || c < 0 || c > 80 || cage_of[c] != CAGE_NONE) return false;
            cage_of[c] = (uint8_t)n;
            cells++;
            p = end;
            if (*p == ',') { p++; continue; }
            break;
        }
        sums[n++] = (uint8_t)sum;
        if (*p == ';') p++;
        else if (*p) return false;
    }
    return geom_killer(cage_of, sums, n, out);
}

bool sudoku_cages_text(const sudoku_geom_t *g, char *buf, int len)
{
    int pos = 0;
    if (len < 1) return false;
    buf[0] = 0;
    for (int k = 0; k < g->ncages; k++) {
        int w = snprintf(buf + pos, (size_t)(len - pos), "%s%d:", k ? ";" : "", g->cage_sum[k]);
        if (w < 0 || w >= len - pos) return false;
        pos += w;
        for (int i = 0; i < g->cage_n[k]; i++) {
            w = snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", g->cage_cell[k][i]);
            if (w < 0 || w >= len - pos) return false;
            pos += w;
        }
    }
    return true;
}

bool sudoku_geom_jigsaw(const uint8_t regions[81], sudoku_geom_t *out)
{
    int count[9] = {0};
    for (int c = 0; c < 81; c++) {
        if (regions[c] > 8) return false;
        count[regions[c]]++;
    }
    for (int i = 0; i < 9; i++) {
        if (count[i] != 9) return false;
    }
    geom_build(out, VAR_JIGSAW, 9, 3, 3, false, false, regions);
    return true;
}

/* the units of a cell (3, 4 or, at the centre of an X board, 5) */
static int cell_units(const sudoku_geom_t *G, int cell, int out[5])
{
    int n = G->ncu[cell];
    for (int i = 0; i < n; i++) out[i] = G->cu[cell][i];
    return n;
}

static bool in_unit(const sudoku_geom_t *G, int cell, int u)
{
    int us[5], n = cell_units(G, cell, us);
    for (int i = 0; i < n; i++) {
        if (us[i] == u) return true;
    }
    return false;
}

static uint32_t rnd(uint32_t *s)        /* xorshift32 */
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* ---------------------------------------------------------------- counting / filling */

typedef struct {
    const sudoku_geom_t *G;
    uint8_t v[81];
    uint16_t um[UNITS_MAX];             /* bit d = digit d used in the unit */
    int8_t units[81][5];                /* the cell's units, -1 terminated */
    uint32_t nodes, node_limit;         /* node_limit 0 = no limit; reached: aborted */
    bool aborted;
    /* Killer: per cage, the digits in it, what's left of its sum, its empty cells */
    uint16_t cage_used[CAGES_MAX];
    int8_t cage_left[CAGES_MAX], cage_empty[CAGES_MAX];
} grid_t;

static void grid_set(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = (uint8_t)d;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) g->um[g->units[i][k]] |= m;
    int c = g->G->cage_of[i];
    if (c != CAGE_NONE) {
        g->cage_used[c] |= m;
        g->cage_left[c] = (int8_t)(g->cage_left[c] - d);
        g->cage_empty[c]--;
    }
}

static void grid_clear(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) g->um[g->units[i][k]] &= (uint16_t)~m;
    int c = g->G->cage_of[i];
    if (c != CAGE_NONE) {
        g->cage_used[c] &= (uint16_t)~m;
        g->cage_left[c] = (int8_t)(g->cage_left[c] + d);
        g->cage_empty[c]++;
    }
}

/* smallest and largest sums of k distinct digits from mask */
static void mask_k_range(uint16_t mask, int k, int *lo, int *hi)
{
    *lo = *hi = 0;
    for (int d = 1, n = 0; d <= 9 && n < k; d++) if (mask >> d & 1) { *lo += d; n++; }
    for (int d = 9, n = 0; d >= 1 && n < k; d--) if (mask >> d & 1) { *hi += d; n++; }
}

static uint16_t grid_free(const grid_t *g, int i)
{
    uint16_t used = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) used |= g->um[g->units[i][k]];
    const sudoku_geom_t *G = g->G;
    uint16_t free = (uint16_t)(G->allow[i] & ~used);
    for (int k = 0; k < G->nxpeers[i]; k++) {       /* Anti-Knight */
        int v = g->v[G->xpeers[i][k]];
        if (v) free &= (uint16_t)~(1u << v);
    }
    if (G->consec_on && free) {                     /* Consecutive: a bar = differ by 1, none = don't */
        const int nb[4] = {i % 9 > 0 ? i - 1 : -1, i % 9 < 8 ? i + 1 : -1, i > 8 ? i - 9 : -1, i < 72 ? i + 9 : -1};
        const bool bar[4] = {nb[0] >= 0 && (G->consec[i - 1] & CONSEC_RIGHT), (G->consec[i] & CONSEC_RIGHT) != 0,
                             nb[2] >= 0 && (G->consec[i - 9] & CONSEC_DOWN), (G->consec[i] & CONSEC_DOWN) != 0};
        for (int k = 0; k < 4; k++) {
            if (nb[k] < 0 || !g->v[nb[k]]) continue;
            int w = g->v[nb[k]];
            uint16_t near = (uint16_t)(((1u << (w + 1)) | (1u << (w - 1))) & 0x3FE);
            free &= bar[k] ? near : (uint16_t)~near;
        }
    }
    if (G->sandwich && free) {                      /* Sandwich: a digit that can't keep a line's sum goes */
        for (int line = 0; line < 2 && free; line++) {
            int lu = line ? 9 + i % 9 : i / 9, ki = line ? i / 9 : i % 9, vals[9], p1 = -1, p9 = -1;
            for (int k = 0; k < 9; k++) {
                int c = line ? k * 9 + i % 9 : (i / 9) * 9 + k;
                vals[k] = c == i ? 0 : g->v[c];
                if (vals[k] == 1) p1 = k;
                if (vals[k] == 9) p9 = k;
            }
            if (p1 < 0 && p9 < 0) continue;            /* neither end placed: no 1 / 9 here can be judged yet */
            /* with both ends already placed: what's between them so far */
            int bsum = 0, bempty = 0, ba = -1, bb = -1;
            if (p1 >= 0 && p9 >= 0) {
                ba = p1 < p9 ? p1 : p9;
                bb = p1 < p9 ? p9 : p1;
                for (int k = ba + 1; k < bb; k++) {
                    if (vals[k]) bsum += vals[k]; else bempty++;
                }
            }
            for (int d = 1; d <= 9; d++) {
                if (!(free >> d & 1)) continue;
                int sum, empty;
                if (d == 1 || d == 9) {                /* this cell becomes an end */
                    int q1 = d == 1 ? ki : p1, q9 = d == 9 ? ki : p9;
                    if (q1 < 0 || q9 < 0) continue;
                    int a = q1 < q9 ? q1 : q9, b = q1 < q9 ? q9 : q1;
                    sum = empty = 0;
                    for (int k = a + 1; k < b; k++) {
                        if (vals[k]) sum += vals[k]; else empty++;
                    }
                } else {
                    if (ba < 0) continue;              /* an end still missing */
                    sum = bsum;
                    empty = bempty;
                    if (ki > ba && ki < bb) {          /* this cell is between them */
                        sum += d;
                        empty--;
                    }
                }
                int left = G->sandwich_sum[lu] - sum;
                if (left < 0 || !(s_bread[empty] >> left & 1)) free &= (uint16_t)~(1u << d);
            }
        }
    }
    int t = G->thermo_of[i];
    if (t != THERMO_NONE && free) {                 /* Thermo: above the cell before it, below the one after */
        int p = G->thermo_pos[i];
        if (p > 0) {
            int v = g->v[G->thermo_cell[t][p - 1]];
            if (v) free &= (uint16_t)~((1u << (v + 1)) - 1);
        }
        if (p + 1 < G->thermo_n[t]) {
            int v = g->v[G->thermo_cell[t][p + 1]];
            if (v) free &= (uint16_t)((1u << v) - 1);
        }
    }
    int c = G->cage_of[i];
    if (c != CAGE_NONE && free) {                    /* Killer: what the cage's sum still allows */
        free &= (uint16_t)~g->cage_used[c];
        int left = g->cage_left[c], others = g->cage_empty[c] - 1;
        uint16_t ok = 0;
        for (int d = 1; d <= 9; d++) {
            if (!(free >> d & 1) || d > left) continue;
            if (others == 0) {
                if (d == left) ok |= (uint16_t)(1u << d);
                continue;
            }
            int lo, hi;
            mask_k_range((uint16_t)(g->G->all & ~g->cage_used[c] & ~(1u << d)), others, &lo, &hi);
            if (left - d >= lo && left - d <= hi) ok |= (uint16_t)(1u << d);
        }
        free = ok;
    }
    return free;
}

static bool grid_load(grid_t *g, const sudoku_geom_t *G, const uint8_t *cells)
{
    memset(g, 0, sizeof(*g));
    g->G = G;
    memset(g->units, -1, sizeof(g->units));
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], us[5], n = cell_units(G, i, us);
        for (int j = 0; j < n; j++) g->units[i][j] = (int8_t)us[j];
    }
    for (int c = 0; c < G->ncages; c++) {
        g->cage_left[c] = (int8_t)G->cage_sum[c];
        g->cage_empty[c] = (int8_t)G->cage_n[c];
    }
    if (!cells) return true;
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], d = cells[i];
        if (d < 1 || d > G->n) continue;
        if (!(grid_free(g, i) & (1u << d))) return false;
        grid_set(g, i, d);
    }
    return true;
}

/* counts solutions up to limit; with rng, fills g with a random solution */
static int search(grid_t *g, int limit, uint32_t *rng)
{
    const sudoku_geom_t *G = g->G;
    if (g->node_limit && ++g->nodes > g->node_limit) {   /* a Jigsaw layout that's hard to fill: give up */
        g->aborted = true;
        return limit;
    }
    int best = -1, best_n = 10;
    uint16_t best_free = 0;
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k];
        if (g->v[i]) continue;
        uint16_t free = grid_free(g, i);
        int n = __builtin_popcount(free);
        if (n < best_n) {
            best = i; best_n = n; best_free = free;
            if (n <= 1) break;
        }
    }
    if (best < 0) return 1;
    if (best_n == 0) return 0;
    int count = 0;
    uint8_t order[9];
    int no = 0;
    for (int d = 1; d <= G->n; d++) if (best_free & (1u << d)) order[no++] = (uint8_t)d;
    if (rng) {
        for (int i = no - 1; i > 0; i--) {
            int j = (int)(rnd(rng) % (uint32_t)(i + 1));
            uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
        }
    }
    for (int k = 0; k < no; k++) {
        int d = order[k];
        grid_set(g, best, d);
        count += search(g, limit - count, rng);
        if (count >= limit) return count;           /* keeps g filled (the solution) */
        grid_clear(g, best, d);
    }
    return count;
}

/* solutions up to limit, giving up after node_limit search steps (0 = never): -1 then */
static int count_bounded(const sudoku_geom_t *G, const uint8_t cells[81], int limit, uint32_t node_limit)
{
    static grid_t g;                                 /* off the caller's stack (one caller at a time) */
    if (!grid_load(&g, G, cells)) return 0;
    g.node_limit = node_limit;
    int n = search(&g, limit, NULL);
    return g.aborted ? -1 : n;
}

int sudoku_count_solutions(const sudoku_geom_t *G, const uint8_t cells[81], int limit)
{
    return count_bounded(G, cells, limit, 0);
}

bool sudoku_classic_unique(const uint8_t cells[81])
{
    static grid_t g;                                 /* its own: another task than the generator's */
    if (!grid_load(&g, sudoku_geom(VAR_CLASSIC), cells)) return false;
    g.node_limit = 200000;                           /* (a broken puzzle can't hold the task for long) */
    return search(&g, 2, NULL) == 1 && !g.aborted;
}

bool sudoku_unique(const sudoku_geom_t *G, const uint8_t cells[81])
{
    static grid_t g;                                 /* its own (the import's task) */
    if (!grid_load(&g, G, cells)) return false;
    g.node_limit = 400000;
    return search(&g, 2, NULL) == 1 && !g.aborted;
}

bool sudoku_solve(const sudoku_geom_t *G, const uint8_t cells[81], uint8_t out[81])
{
    static grid_t g;                                 /* off the (small) LVGL task stack */
    if (!grid_load(&g, G, cells)) return false;
    if (search(&g, 1, NULL) < 1) return false;
    memcpy(out, g.v, 81);
    return true;
}

/* false if node_limit (0 = none) was reached first */
static bool random_full_grid(const sudoku_geom_t *G, uint8_t out[81], uint32_t *rng, uint32_t node_limit)
{
    static grid_t g;
    grid_load(&g, G, NULL);
    g.node_limit = node_limit;
    int n = search(&g, 1, rng);
    memcpy(out, g.v, 81);
    return n >= 1 && !g.aborted;
}

/* ---------------------------------------------------------------- human-style grading */

typedef struct {
    const sudoku_geom_t *G;
    uint8_t v[81];
    uint16_t cand[81];
    int empty;
    bool broken;
} logic_t;

static void place(logic_t *L, int c, int d)
{
    const sudoku_geom_t *G = L->G;
    L->v[c] = (uint8_t)d;
    L->cand[c] = 0;
    L->empty--;
    for (int p = 0; p < G->npeers[c]; p++) {
        int o = G->peers[c][p];
        L->cand[o] &= (uint16_t)~(1u << d);
        if (!L->v[o] && !L->cand[o]) L->broken = true;
    }
}

static void logic_load(logic_t *L, const sudoku_geom_t *G, const uint8_t *vals)
{
    memset(L, 0, sizeof(*L));
    L->G = G;
    L->empty = G->ncells;
    for (int k = 0; k < G->ncells; k++) L->cand[G->cells[k]] = G->allow[G->cells[k]];
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c] >= 1 && vals[c] <= G->n) place(L, c, vals[c]);
    }
}

static bool singles(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {           /* naked single */
        int c = G->cells[k];
        if (!L->v[c] && __builtin_popcount(L->cand[c]) == 1) {
            place(L, c, __builtin_ctz(L->cand[c]));
            return true;
        }
    }
    for (int i = 0; i < G->nunits; i++) {           /* hidden single */
        int u = G->unit_ids[i];
        for (int d = 1; d <= G->n; d++) {
            int where = -1, n = 0;
            for (int k = 0; k < G->n; k++) {
                int c = G->unit[u][k];
                if (L->v[c] == d) { n = -1; break; }
                if (L->cand[c] & (1u << d)) { where = c; n++; }
            }
            if (n == 1) {
                place(L, where, d);
                return true;
            }
        }
    }
    return false;
}

/* the first technique used while looking for the next step (sudoku_next_step) */
static sudoku_tech_t *s_trace;

static void trace(int kind, int unit, int unit2, int d1, int d2, int c1, int c2)
{
    if (!s_trace || s_trace->kind != TECH_NONE) return;
    s_trace->kind = kind;
    s_trace->unit = unit;
    s_trace->unit2 = unit2;
    s_trace->unit3 = -1;
    s_trace->digit = d1;
    s_trace->digit2 = d2;
    s_trace->cell1 = c1;
    s_trace->cell2 = c2;
    s_trace->ncells = 0;
}

/* the same, with a third line and the cells that make the pattern */
static void trace_cells(int kind, int unit, int unit2, int unit3, int d1, int d2, const int *cells, int n)
{
    if (!s_trace || s_trace->kind != TECH_NONE) return;
    trace(kind, unit, unit2, d1, d2, -1, -1);
    s_trace->unit3 = unit3;
    s_trace->ncells = n > 9 ? 9 : n;
    for (int i = 0; i < s_trace->ncells; i++) s_trace->cells[i] = (uint8_t)cells[i];
}

static bool eliminate(logic_t *L, int c, uint16_t bits)
{
    if (L->v[c] || !(L->cand[c] & bits)) return false;
    L->cand[c] &= (uint16_t)~bits;
    if (!L->cand[c]) L->broken = true;
    return true;
}

/* Killer: each cage's empty cells keep only the digits that appear in some
 * set of distinct digits (none already in the cage) that makes up what's
 * left of its sum, with a digit for every empty cell. */
static bool cage_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncages; k++) {
        int left = G->cage_sum[k], n = 0, empty[9];
        uint16_t used = 0, avail = 0;
        for (int i = 0; i < G->cage_n[k]; i++) {
            int c = G->cage_cell[k][i];
            if (L->v[c]) {
                left -= L->v[c];
                used |= (uint16_t)(1u << L->v[c]);
            } else {
                empty[n++] = c;
                avail |= L->cand[c];
            }
        }
        if (!n) continue;
        avail &= (uint16_t)~used;
        uint16_t allowed = 0;
        for (uint16_t m = avail; m; m = (uint16_t)((m - 1) & avail)) {
            if (__builtin_popcount(m) != n || s_mask_sum[m] != left) continue;
            bool fits = true;
            for (int i = 0; i < n && fits; i++) fits = (L->cand[empty[i]] & m) != 0;
            if (fits) allowed |= m;
        }
        bool changed = false;
        for (int i = 0; i < n; i++) changed |= eliminate(L, empty[i], (uint16_t)(L->cand[empty[i]] & ~allowed));
        if (changed) {
            trace(TECH_CAGE, k, -1, allowed, 0, -1, -1);
            return true;
        }
    }
    return false;
}

/* Consecutive: a cell keeps only the digits that suit every neighbour: with
 * a bar, one of the neighbour's candidates must be 1 away; without, the
 * neighbour must have a candidate that isn't 1 away (a neighbour's digit
 * counts as its only candidate). */
static bool consec_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int c = 0; c < 81; c++) {
        if (L->v[c]) continue;
        const int nb[4] = {c % 9 > 0 ? c - 1 : -1, c % 9 < 8 ? c + 1 : -1, c > 8 ? c - 9 : -1, c < 72 ? c + 9 : -1};
        const bool bar[4] = {nb[0] >= 0 && (G->consec[c - 1] & CONSEC_RIGHT), (G->consec[c] & CONSEC_RIGHT) != 0,
                             nb[2] >= 0 && (G->consec[c - 9] & CONSEC_DOWN), (G->consec[c] & CONSEC_DOWN) != 0};
        uint16_t keep = L->cand[c];
        for (int k = 0; k < 4; k++) {
            if (nb[k] < 0) continue;
            uint16_t other = L->v[nb[k]] ? (uint16_t)(1u << L->v[nb[k]]) : L->cand[nb[k]];
            uint16_t ok = 0;
            for (int d = 1; d <= 9; d++) {
                if (!(keep >> d & 1)) continue;
                uint16_t near = (uint16_t)(((1u << (d + 1)) | (1u << (d - 1))) & 0x3FE);
                if (bar[k] ? (other & near) : (other & ~near & 0x3FE)) ok |= (uint16_t)(1u << d);
            }
            keep &= ok;
        }
        if (keep != L->cand[c]) {
            eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
            trace(TECH_CONSEC, c, -1, keep, 0, c, -1);
            return true;
        }
    }
    return false;
}

/* Thermo: along a thermometer each cell's digit is above the lowest the cell
 * before it can be, and below the highest the cell after it can be. */
static bool thermo_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int t = 0; t < G->nthermos; t++) {
        int n = G->thermo_n[t], lo[9], hi[9];
        for (int i = 0; i < n; i++) {
            int c = G->thermo_cell[t][i];
            uint16_t m = L->v[c] ? (uint16_t)(1u << L->v[c]) : L->cand[c];
            if (!m) return false;
            lo[i] = __builtin_ctz(m);
            hi[i] = 31 - __builtin_clz(m);
        }
        for (int i = 1; i < n; i++) if (lo[i] < lo[i - 1] + 1) lo[i] = lo[i - 1] + 1;
        for (int i = n - 2; i >= 0; i--) if (hi[i] > hi[i + 1] - 1) hi[i] = hi[i + 1] - 1;
        for (int i = 0; i < n; i++) {
            int c = G->thermo_cell[t][i];
            if (L->v[c]) continue;
            uint16_t keep = (uint16_t)(hi[i] >= lo[i] ? (((1u << (hi[i] + 1)) - 1) & ~((1u << lo[i]) - 1)) : 0);
            if (L->cand[c] & ~keep) {
                eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
                trace(TECH_THERMO, t, -1, L->cand[c], 0, c, -1);
                return true;
            }
        }
    }
    return false;
}

/* Sandwich: in a line, the 1 and the 9 can only stand where some placement
 * of the pair leaves cells between them that can make up the clue (counting
 * only how many cells, and the digits 2-8): the 1 and the 9 go from every
 * other cell of the line. */
static bool sandwich_logic(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int lu = 0; lu < 18; lu++) {
        int cells[9], ps[10], pe[10], pbad[10];     /* prefix: sum placed, empty cells, placed 1s/9s */
        uint16_t one = 0, nine = 0;                 /* bit k: cell k can be the 1 (the 9) */
        ps[0] = pe[0] = pbad[0] = 0;
        for (int k = 0; k < 9; k++) {
            int c = cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9), v = L->v[c];
            if (v == 1 || (!v && (L->cand[c] & 2))) one |= (uint16_t)(1u << k);
            if (v == 9 || (!v && (L->cand[c] & 0x200))) nine |= (uint16_t)(1u << k);
            ps[k + 1] = ps[k] + (v != 1 && v != 9 ? v : 0);
            pe[k + 1] = pe[k] + !v;
            pbad[k + 1] = pbad[k] + (v == 1 || v == 9);
        }
        uint16_t can1 = 0, can9 = 0;
        for (int p = 0; p < 9; p++) {
            if (!(one >> p & 1)) continue;
            for (int q = 0; q < 9; q++) {
                if (q == p || !(nine >> q & 1)) continue;
                int a = p < q ? p : q, b = p < q ? q : p;
                if (pbad[b] - pbad[a + 1]) continue;     /* a placed 1 or 9 between them */
                int empty = pe[b] - pe[a + 1], left = G->sandwich_sum[lu] - (ps[b] - ps[a + 1]);
                if (left < 0 || !(s_bread[empty] >> left & 1)) continue;
                can1 |= (uint16_t)(1u << p);
                can9 |= (uint16_t)(1u << q);
            }
        }
        bool changed = false;
        for (int k = 0; k < 9; k++) {
            int c = cells[k];
            if (L->v[c]) continue;
            uint16_t drop = 0;
            if (!(can1 >> k & 1)) drop |= 2;
            if (!(can9 >> k & 1)) drop |= 0x200;
            changed |= eliminate(L, c, drop);
        }
        if (changed) {
            trace(TECH_SANDWICH, lu, -1, 0, 0, -1, -1);
            return true;
        }
    }
    return false;
}

/* pointing / claiming: a digit confined to one line within a box (or to one
 * box within a line) can't be elsewhere in that line (or box). Lines are
 * rows, columns and, on an X board, the diagonals. */
static bool locked_candidates(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    bool changed = false;
    for (int bi = 0; bi < 9 + 4; bi++) {               /* boxes (regions), then Hyper windows */
        int b = bi < 9 ? 18 + bi : UNIT_WINDOW + bi - 9;
        if (!G->active[b]) continue;
        for (int oi = 0; oi < G->nunits; oi++) {
            int other = G->unit_ids[oi];
            if (other == b || (b < 27 && other >= 18 && other < 27)) continue;   /* boxes don't overlap */
            for (int d = 1; d <= G->n; d++) {
                uint16_t m = (uint16_t)(1u << d);
                int in_both = 0, box_only = 0, line_only = 0;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[b][k];
                    if (!(L->cand[c] & m)) continue;
                    if (in_unit(G, c, other)) in_both++; else box_only++;
                }
                if (!in_both) continue;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[other][k];
                    if (!(L->cand[c] & m)) continue;
                    if (!in_unit(G, c, b)) line_only++;
                }
                if (!box_only && line_only) {                 /* pointing: clear the rest of the line */
                    for (int k = 0; k < G->n; k++) {
                        int c = G->unit[other][k];
                        if (!in_unit(G, c, b)) changed |= eliminate(L, c, m);
                    }
                    if (changed) trace(TECH_POINTING, b, other, d, 0, -1, -1);
                } else if (!line_only && box_only) {          /* claiming: clear the rest of the box */
                    for (int k = 0; k < G->n; k++) {
                        int c = G->unit[b][k];
                        if (!in_unit(G, c, other)) changed |= eliminate(L, c, m);
                    }
                    if (changed) trace(TECH_CLAIMING, other, b, d, 0, -1, -1);
                }
                if (changed) return true;
            }
        }
    }
    return changed;
}

static bool pairs(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    const int n = G->n;
    for (int ui = 0; ui < G->nunits; ui++) {
        int u = G->unit_ids[ui];
        /* naked pair: two cells with the same two candidates */
        for (int a = 0; a < n; a++) {
            int ca = G->unit[u][a];
            if (L->v[ca] || __builtin_popcount(L->cand[ca]) != 2) continue;
            for (int b = a + 1; b < n; b++) {
                int cb = G->unit[u][b];
                if (L->v[cb] || L->cand[cb] != L->cand[ca]) continue;
                bool changed = false;
                for (int k = 0; k < n; k++) {
                    int c = G->unit[u][k];
                    if (c != ca && c != cb) changed |= eliminate(L, c, L->cand[ca]);
                }
                if (changed) {
                    int d1 = __builtin_ctz(L->cand[ca]), d2 = 31 - __builtin_clz(L->cand[ca]);
                    trace(TECH_NAKED_PAIR, u, -1, d1, d2, ca, cb);
                    return true;
                }
            }
        }
        /* hidden pair: two digits that fit only the same two cells */
        uint16_t where[10] = {0};                   /* bit k = unit cell k */
        for (int d = 1; d <= n; d++) {
            for (int k = 0; k < n; k++) {
                if (L->cand[G->unit[u][k]] & (1u << d)) where[d] |= (uint16_t)(1u << k);
            }
        }
        for (int d1 = 1; d1 <= n; d1++) {
            if (__builtin_popcount(where[d1]) != 2) continue;
            for (int d2 = d1 + 1; d2 <= n; d2++) {
                if (where[d2] != where[d1]) continue;
                uint16_t keep = (uint16_t)((1u << d1) | (1u << d2));
                bool changed = false;
                int cells[2], nc = 0;
                for (int k = 0; k < n; k++) {
                    if (!(where[d1] & (1u << k))) continue;
                    int c = G->unit[u][k];
                    if (nc < 2) cells[nc++] = c;
                    changed |= eliminate(L, c, (uint16_t)(L->cand[c] & ~keep));
                }
                if (changed) {
                    trace(TECH_HIDDEN_PAIR, u, -1, d1, d2, cells[0], cells[1]);
                    return true;
                }
            }
        }
    }
    return false;
}

/* ---- advanced techniques: hints and Learn only (sudoku_next_step). Grading leaves
 * them out, so the levels mean what they did. Each relies only on the board's units
 * (rows, columns, boxes, and diagonals / windows / regions where the variant has them),
 * which every variant keeps, so the eliminations are sound for all of them. */

/* naked triple: three cells of a unit whose candidates are three digits between them */
static bool naked_triple(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    const int n = G->n;
    for (int ui = 0; ui < G->nunits; ui++) {
        int u = G->unit_ids[ui], cs[9], m = 0;
        for (int k = 0; k < n; k++) {
            int c = G->unit[u][k], p = __builtin_popcount(L->cand[c]);
            if (!L->v[c] && p >= 2 && p <= 3) cs[m++] = c;
        }
        for (int a = 0; a < m; a++) {
            for (int b = a + 1; b < m; b++) {
                for (int e = b + 1; e < m; e++) {
                    uint16_t un = (uint16_t)(L->cand[cs[a]] | L->cand[cs[b]] | L->cand[cs[e]]);
                    if (__builtin_popcount(un) != 3) continue;
                    bool changed = false;
                    for (int k = 0; k < n; k++) {
                        int c = G->unit[u][k];
                        if (c != cs[a] && c != cs[b] && c != cs[e]) changed |= eliminate(L, c, un);
                    }
                    if (changed) {
                        const int cells[3] = {cs[a], cs[b], cs[e]};
                        trace_cells(TECH_NAKED_TRIPLE, u, -1, -1, un, 0, cells, 3);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* X-Wing (size 2) and Swordfish (size 3): in `size` rows a digit's places all lie in
 * the same `size` columns, so it can't be anywhere else in those columns (or the
 * same with rows and columns swapped) */
static bool fish(logic_t *L, int size)
{
    const sudoku_geom_t *G = L->G;
    const int n = G->n;
    for (int orient = 0; orient < 2; orient++) {   /* 0: base rows, cover columns; 1: the other way */
#define FISH_CELL(line, pos) (orient ? (pos) * 9 + (line) : (line) * 9 + (pos))
        for (int d = 1; d <= n; d++) {
            const uint16_t m = (uint16_t)(1u << d);
            int line[9], nl = 0;
            uint16_t pos[9];
            for (int i = 0; i < n; i++) {
                uint16_t p = 0;
                bool placed = false;
                for (int j = 0; j < n; j++) {
                    int c = FISH_CELL(i, j);
                    if (L->v[c] == d) placed = true;
                    else if (L->cand[c] & m) p |= (uint16_t)(1u << j);
                }
                int k = __builtin_popcount(p);
                if (!placed && k >= 2 && k <= size) {
                    line[nl] = i;
                    pos[nl++] = p;
                }
            }
            int pick[3];
            for (pick[0] = 0; pick[0] < nl; pick[0]++) {
                for (pick[1] = pick[0] + 1; pick[1] < nl; pick[1]++) {
                    for (pick[2] = size == 3 ? pick[1] + 1 : nl; pick[2] < (size == 3 ? nl : nl + 1); pick[2]++) {
                        uint16_t cover = (uint16_t)(pos[pick[0]] | pos[pick[1]] | (size == 3 ? pos[pick[2]] : 0));
                        if (__builtin_popcount(cover) != size) continue;
                        bool base[9] = {false};
                        for (int s = 0; s < size; s++) base[line[pick[s]]] = true;
                        bool changed = false;
                        for (int j = 0; j < n; j++) {
                            if (!(cover >> j & 1)) continue;
                            for (int i = 0; i < n; i++) {
                                if (!base[i]) changed |= eliminate(L, FISH_CELL(i, j), m);
                            }
                        }
                        if (changed) {
                            int cells[9], nc = 0;
                            for (int s = 0; s < size; s++) {
                                for (int j = 0; j < n; j++) {
                                    if (pos[pick[s]] >> j & 1) cells[nc++] = FISH_CELL(line[pick[s]], j);
                                }
                            }
                            const int u0 = orient * 9;    /* rows are units 0..8, columns 9..17 */
                            trace_cells(size == 2 ? TECH_X_WING : TECH_SWORDFISH, u0 + line[pick[0]],
                                        u0 + line[pick[1]], size == 3 ? u0 + line[pick[2]] : -1, d, cover,
                                        cells, nc);
                            return true;
                        }
                    }
                }
            }
        }
#undef FISH_CELL
    }
    return false;
}

/* XY-Wing: a pivot that can only be x or y sees one cell that can only be x or z
 * and another that can only be y or z. Whichever the pivot is, one of the two is z,
 * so no cell that sees both of them can be z. */
static bool xy_wing(logic_t *L)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {
        int p = G->cells[k];
        uint16_t pm = L->cand[p];
        if (L->v[p] || __builtin_popcount(pm) != 2) continue;
        for (int i = 0; i < G->npeers[p]; i++) {
            int a = G->peers[p][i];
            uint16_t am = L->cand[a];
            if (L->v[a] || __builtin_popcount(am) != 2 || __builtin_popcount(am & pm) != 1) continue;
            uint16_t z = (uint16_t)(am & ~pm);                   /* a is x or z */
            uint16_t want = (uint16_t)((pm & ~am) | z);          /* b must be y or z */
            for (int j = 0; j < G->npeers[p]; j++) {
                int b = G->peers[p][j];
                if (b == a || L->v[b] || L->cand[b] != want) continue;
                bool changed = false;
                for (int q = 0; q < G->ncells; q++) {
                    int c = G->cells[q];
                    if (c == a || c == b || c == p) continue;
                    if (sudoku_geom_peer(G, c, a) && sudoku_geom_peer(G, c, b)) changed |= eliminate(L, c, z);
                }
                if (changed) {
                    const int cells[3] = {p, a, b};
                    bool first = s_trace && s_trace->kind == TECH_NONE;
                    trace_cells(TECH_XY_WING, -1, -1, -1, __builtin_ctz(z), pm, cells, 3);
                    if (first) {                                 /* the pincers' two digits each */
                        s_trace->cell1 = am;
                        s_trace->cell2 = want;
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

/* 1 = singles suffice, 2 = also needs locked candidates / pairs, 3 = more */
static int technique_tier(const sudoku_geom_t *G, const uint8_t givens[81])
{
    static logic_t L;
    logic_load(&L, G, givens);
    int tier = 1;
    while (L.empty && !L.broken) {
        if (singles(&L)) continue;
        if (G->ncages && cage_logic(&L)) continue;  /* Killer: cage sums count as the simple step */
        if (G->consec_on && consec_logic(&L)) continue;   /* Consecutive: so do the bars */
        if (G->nthermos && thermo_logic(&L)) continue;     /* Thermo: and the thermometers */
        if (G->sandwich && sandwich_logic(&L)) continue;   /* Sandwich: and where the 1 and 9 can be */
        if (locked_candidates(&L) || pairs(&L)) {
            tier = 2;
            continue;
        }
        return 3;
    }
    return L.broken ? 3 : tier;
}

/* Easy keeps this many clues or more (the same share of the board for 6x6;
 * Killer, whose cages carry most of the information: a few) */
static int easy_min_clues(const sudoku_geom_t *G)
{
    if (G->ncages) return 8;
    if (G->consec_on) return 12;                    /* the bars say a lot */
    if (G->variant == VAR_EVENODD) return 30;
    if (G->variant == VAR_KNIGHT) return 32;
    if (G->nthermos) return 28;
    if (G->sandwich) return 24;
    return G->n == 9 ? 36 : 16;
}

int sudoku_grade(const sudoku_geom_t *G, const uint8_t givens[81])
{
    if (sudoku_count_solutions(G, givens, 2) != 1) return -1;   /* exact, also for Jigsaw */
    int clues = 0;
    for (int k = 0; k < G->ncells; k++) clues += givens[G->cells[k]] != 0;
    switch (technique_tier(G, givens)) {
    case 1: return clues >= easy_min_clues(G) ? GEN_EASY : GEN_MEDIUM;
    case 2: return GEN_HARD;
    default: return GEN_EXPERT;
    }
}

/* ---------------------------------------------------------------- generating */

/* one attempt: clues taken away in random symmetric pairs (a cell and its
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
        if (c <= m) order[no++] = (uint8_t)c;
    }
    for (int i = no - 1; i > 0; i--) {
        int j = (int)(rnd(rng) % (uint32_t)(i + 1));
        uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
    }
    int clues = G->ncells;
    int stop = level == GEN_EASY ? easy_min_clues(G) + (int)(rnd(rng) % (n == 9 ? 5 : 3)) : 0;
    if (G->ncages && level == GEN_MEDIUM) stop = 2 + (int)(rnd(rng) % 3);   /* Killer Medium: a couple of givens */
    /* X and 6x6: Medium and Hard keep a clue whose removal would need harder
     * techniques, so most attempts land on the level (digging to the minimum
     * mostly gives Expert there). Classic digs as it always has, so its
     * puzzles for a seed (the daily puzzle) stay the same. */
    int max_tier = 3;
    if (G->variant != VAR_CLASSIC && level == GEN_MEDIUM) max_tier = 1;
    if (G->variant != VAR_CLASSIC && level == GEN_HARD) max_tier = 2;
    /* Jigsaw and Killer: a uniqueness check that takes too long keeps the
     * clue (near the minimum, the plain search can take thousands of times longer) */
    const uint32_t node_limit = G->variant == VAR_CLASSIC ? 0 : G->variant == VAR_JIGSAW ? 4000 : 6000;
    for (int k = 0; k < no && clues > stop; k++) {
        int a = order[k], b = (n - 1 - a / 9) * 9 + (n - 1 - a % 9);
        uint8_t va = out[a], vb = out[b];
        out[a] = out[b] = 0;
        if (count_bounded(G, out, 2, node_limit) == 1 && (max_tier == 3 || technique_tier(G, out) <= max_tier)) {
            clues -= a == b ? 1 : 2;
        } else {
            out[a] = va;
            out[b] = vb;
        }
    }
}

/* ---------------------------------------------------------------- Jigsaw regions */

/* is region `reg` of the map one connected piece (4-neighbours)? */
static bool region_connected(const uint8_t map[81], int reg)
{
    uint8_t seen[81] = {0}, stack[81];
    int sp = 0, start = -1, total = 0, reached = 0;
    for (int c = 0; c < 81; c++) {
        if (map[c] != reg) continue;
        total++;
        if (start < 0) start = c;
    }
    if (start < 0) return false;
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
    for (int c = 0; c < 81; c++) map[c] = (uint8_t)((c / 27) * 3 + (c % 9) / 3);
    int done = 0;
    for (int tries = 0; tries < 20000 && done < 90; tries++) {
        int a = (int)(rnd(rng) % 81), r = a / 9, k = a % 9;
        int dir = (int)(rnd(rng) % 4);
        int b = dir == 0 ? (k > 0 ? a - 1 : -1) : dir == 1 ? (k < 8 ? a + 1 : -1)
              : dir == 2 ? (r > 0 ? a - 9 : -1) : (r < 8 ? a + 9 : -1);
        if (b < 0 || map[a] == map[b]) continue;
        int A = map[a], B = map[b];
        /* a cell of B (not a) that touches A elsewhere: it goes to A */
        uint8_t cand[81];
        int nc = 0;
        for (int c = 0; c < 81; c++) {
            if (map[c] != B) continue;
            int cr = c / 9, ck = c % 9;
            const int nb[4] = {ck > 0 ? c - 1 : -1, ck < 8 ? c + 1 : -1, cr > 0 ? c - 9 : -1, cr < 8 ? c + 9 : -1};
            for (int i = 0; i < 4; i++) {
                if (nb[i] >= 0 && nb[i] != a && map[nb[i]] == A) {
                    cand[nc++] = (uint8_t)c;
                    break;
                }
            }
        }
        if (!nc) continue;
        int c = cand[rnd(rng) % (uint32_t)nc];
        map[a] = (uint8_t)B;
        map[c] = (uint8_t)A;
        if (region_connected(map, A) && region_connected(map, B)) {
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
    for (int i = 0; i < 81; i++) order[i] = (uint8_t)i;
    for (int i = 80; i > 0; i--) {
        int j = (int)(rnd(rng) % (uint32_t)(i + 1));
        uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
    }
    int n = 0;
    for (int oi = 0; oi < 81; oi++) {
        int start = order[oi];
        if (cage_of[start] != CAGE_NONE) continue;
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
                    if (o >= 0 && cage_of[o] == CAGE_NONE && !(digits[k] >> full[o] & 1)) cand[ncand++] = (uint8_t)o;
                }
            }
            if (!ncand) break;
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
        if (size[k] != 1) continue;
        int r = c / 9, q = c % 9;
        const int nb[4] = {q > 0 ? c - 1 : -1, q < 8 ? c + 1 : -1, r > 0 ? c - 9 : -1, r < 8 ? c + 9 : -1};
        for (int j = 0; j < 4; j++) {
            int o = nb[j];
            if (o < 0) continue;
            int k2 = cage_of[o];
            if (k2 == k || size[k2] >= max_size + 1 || (digits[k2] >> full[c] & 1)) continue;
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
    return geom_killer(cage_of, sums, m, out);
}

/* ---------------------------------------------------------------- Thermo thermometers */

/* Thermometers over a solved grid: from random cells, a path grows through
 * neighbours (diagonals too) whose digit is higher, 3 to 6 cells long. */
static bool thermo_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out)
{
    uint8_t cells[THERMOS_MAX][9], lens[THERMOS_MAX], used[81] = {0};
    int n = 0;
    for (int tries = 0; tries < 400 && n < want; tries++) {
        int c = (int)(rnd(rng) % 81), target = 3 + (int)(rnd(rng) % 4);
        if (used[c] || full[c] > 9 - (target - 1)) continue;
        int len = 0;
        cells[n][len++] = (uint8_t)c;
        while (len < target) {
            int cur = cells[n][len - 1], r = cur / 9, q = cur % 9, cand[8], nc = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dq = -1; dq <= 1; dq++) {
                    int rr = r + dr, qq = q + dq, o = rr * 9 + qq;
                    if ((!dr && !dq) || rr < 0 || rr > 8 || qq < 0 || qq > 8 || used[o]) continue;
                    bool on = false;
                    for (int k = 0; k < len; k++) on |= cells[n][k] == o;
                    if (!on && full[o] > full[cur]) cand[nc++] = o;
                }
            }
            if (!nc) break;
            cells[n][len++] = (uint8_t)cand[rnd(rng) % (uint32_t)nc];
        }
        if (len < 3) continue;
        for (int k = 0; k < len; k++) used[cells[n][k]] = 1;
        lens[n++] = (uint8_t)len;
    }
    return n >= 3 && geom_thermo(cells, lens, n, out);
}

/* ---------------------------------------------------------------- generating (all variants) */

bool sudoku_generate(int variant, gen_level_t level, uint32_t seed, uint8_t givens[81], sudoku_geom_t *board,
                     gen_level_t *got_level)
{
    static sudoku_geom_t *jig;                      /* a board made with its puzzle, being tried (PSRAM) */
    const bool jigsaw = variant == VAR_JIGSAW, killer = variant == VAR_KILLER;
    const bool marks = variant == VAR_EVENODD || variant == VAR_CONSEC;
    const bool thermo = variant == VAR_THERMO, sandwich = variant == VAR_SANDWICH;
    const bool own = jigsaw || killer || marks || thermo || sandwich;
    if (sandwich) bread_tables();
    if (own && !jig && !(jig = malloc(sizeof(*jig)))) return false;
    const sudoku_geom_t *G = own ? jig : sudoku_geom(variant);
    uint32_t rng = seed ? seed : 1;
    int tries = level == GEN_EXPERT ? 400 : (level == GEN_HARD ? 200 : 60);
    if (G->n == 6 && level == GEN_HARD) tries = 2000;   /* rare on 6x6, but an attempt is cheap there */
    if (variant == VAR_SANDWICH && level == GEN_HARD) tries = 40;   /* slow attempts: bound the wait */
    uint8_t full[81], cand[81], map[81];
    int best = -1;
    memset(givens, 0, 81);
    for (int t = 0; t < tries; t++) {
        if (jigsaw) {                               /* a new shape every few tries */
            if (t % 4 == 0) {
                jigsaw_map(map, &rng);
                sudoku_geom_jigsaw(map, jig);
            }
            if (!random_full_grid(G, full, &rng, 20000)) {   /* no quick fill: another shape */
                t += 3 - t % 4;
                continue;
            }
        } else if (killer) {                        /* a grid, then cages drawn over it */
            random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!killer_cages(full, level >= GEN_HARD ? 5 : 3, &rng, jig)) continue;
        } else if (marks) {                         /* a grid, then its marks */
            random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            char text[82];
            for (int c = 0; c < 81; c++) {
                if (variant == VAR_CONSEC) {
                    int m = 0;
                    if (c % 9 < 8 && abs(full[c] - full[c + 1]) == 1) m |= CONSEC_RIGHT;
                    if (c / 9 < 8 && abs(full[c] - full[c + 9]) == 1) m |= CONSEC_DOWN;
                    text[c] = (char)('0' + m);
                } else {                            /* about a third of the cells say even or odd */
                    text[c] = rnd(&rng) % 3 ? '.' : (full[c] % 2 ? 'o' : 'e');
                }
            }
            text[81] = 0;
            sudoku_geom_marks(variant, text, jig);
        } else if (thermo) {                        /* a grid, then thermometers along rising digits */
            random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            if (!thermo_paths(full, &rng, level >= GEN_HARD ? 10 : 7, jig)) continue;
        } else if (sandwich) {                      /* a grid, then its 18 sums */
            random_full_grid(sudoku_geom(VAR_CLASSIC), full, &rng, 0);
            uint8_t sums[18];
            for (int lu = 0; lu < 18; lu++) {
                int p1 = 0, p9 = 0, cells[9];
                for (int k = 0; k < 9; k++) {
                    cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9);
                    if (full[cells[k]] == 1) p1 = k;
                    if (full[cells[k]] == 9) p9 = k;
                }
                int a = p1 < p9 ? p1 : p9, b = p1 < p9 ? p9 : p1, sum = 0;
                for (int k = a + 1; k < b; k++) sum += full[cells[k]];
                sums[lu] = (uint8_t)sum;
            }
            geom_sandwich(sums, jig);
        } else if (variant == VAR_KNIGHT) {         /* not every fill works out: try again */
            if (!random_full_grid(G, full, &rng, 20000)) continue;
        } else {
            random_full_grid(G, full, &rng, 0);
        }
        dig(G, full, cand, level, &rng);
        int g = sudoku_grade(G, cand);
        if (g < 0) continue;
        /* closest to the level asked for, preferring the easier side */
        if (best < 0 || (g <= (int)level && (g > best || best > (int)level)) ||
            (best > (int)level && g < best)) {
            best = g;
            memcpy(givens, cand, 81);
            if (board) *board = *G;
        }
        if (g == (int)level) break;
    }
    if (got_level) *got_level = best < 0 ? level : (gen_level_t)best;
    return best >= 0;
}

/* ---------------------------------------------------------------- hints */

/* a hidden single (boxes first: the easiest to spot), or -1 */
static int find_hidden_single(const logic_t *L, int *digit, int *unit)
{
    const sudoku_geom_t *G = L->G;
    static const int order[5][2] = {{18, 27}, {0, 9}, {9, 18}, {27, 29}, {UNIT_WINDOW, UNIT_WINDOW + 4}};
    for (int o = 0; o < 5; o++) {
        for (int u = order[o][0]; u < order[o][1]; u++) {
            if (!G->active[u]) continue;
            for (int d = 1; d <= G->n; d++) {
                int where = -1, n = 0;
                for (int k = 0; k < G->n; k++) {
                    int c = G->unit[u][k];
                    if (L->v[c] == d) { n = -1; break; }
                    if (L->cand[c] & (1u << d)) { where = c; n++; }
                }
                if (n == 1) {
                    *digit = d;
                    *unit = u;
                    return where;
                }
            }
        }
    }
    return -1;
}

static int find_naked_single(const logic_t *L, int *digit)
{
    const sudoku_geom_t *G = L->G;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (!L->v[c] && __builtin_popcount(L->cand[c]) == 1) {
            *digit = __builtin_ctz(L->cand[c]);
            return c;
        }
    }
    return -1;
}

bool sudoku_next_step(const sudoku_geom_t *G, const uint8_t vals[81], const uint8_t solution[81], sudoku_step_t *out)
{
    memset(out, 0, sizeof(*out));
    out->cell = -1;
    out->unit = -1;
    for (int k = 0; k < G->ncells; k++) {                /* a wrong digit first */
        int c = G->cells[k];
        if (vals[c] && vals[c] != solution[c]) {
            out->kind = STEP_WRONG;
            out->cell = c;
            out->digit = vals[c];
            return true;
        }
    }
    static logic_t L;                                    /* off the (small) LVGL task stack */
    logic_load(&L, G, vals);
    if (!L.empty) return false;                          /* solved */
    s_trace = &out->tech;
    for (int round = 0; round < 64 && !L.broken; round++) {
        int d, u, c = find_hidden_single(&L, &d, &u);
        if (c >= 0) {
            out->kind = STEP_HIDDEN_SINGLE;
            out->cell = c;
            out->digit = d;
            out->unit = u;
            s_trace = NULL;
            return true;
        }
        c = find_naked_single(&L, &d);
        if (c >= 0) {
            out->kind = STEP_NAKED_SINGLE;
            out->cell = c;
            out->digit = d;
            s_trace = NULL;
            return true;
        }
        if (!(G->ncages && cage_logic(&L)) && !(G->consec_on && consec_logic(&L)) &&
            !(G->nthermos && thermo_logic(&L)) && !(G->sandwich && sandwich_logic(&L)) && !locked_candidates(&L) &&
            !pairs(&L) && !naked_triple(&L) && !fish(&L, 2) && !fish(&L, 3) && !xy_wing(&L)) {
            break;                                                  /* the first one is in out->tech */
        }
    }
    s_trace = NULL;
    /* nothing simple: the empty cell with the fewest candidates, from the solution */
    int best = -1, best_n = 10;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c]) continue;
        int n = __builtin_popcount(L.cand[c]);
        if (n < best_n) { best = c; best_n = n; }
    }
    out->kind = STEP_NONE;
    out->cell = best;
    out->digit = best >= 0 ? solution[best] : 0;
    memset(&out->tech, 0, sizeof(out->tech));
    return best >= 0;
}

bool sudoku_step_with(const sudoku_geom_t *G, const uint8_t vals[81], const uint8_t solution[81], int tech,
                      sudoku_step_t *out)
{
    static logic_t L;
    memset(out, 0, sizeof(*out));
    out->cell = out->unit = -1;
    for (int k = 0; k < G->ncells; k++) {
        int c = G->cells[k];
        if (vals[c] && vals[c] != solution[c]) return false;     /* not from a wrong board */
    }
    logic_load(&L, G, vals);
    if (!L.empty || L.broken) return false;
    int d, u;
    if (find_hidden_single(&L, &d, &u) >= 0 || find_naked_single(&L, &d) >= 0) return false;
    s_trace = &out->tech;
    bool ok = false;
    switch (tech) {
    case TECH_POINTING:
    case TECH_CLAIMING:     ok = locked_candidates(&L); break;
    case TECH_NAKED_PAIR:
    case TECH_HIDDEN_PAIR:  ok = pairs(&L); break;
    case TECH_NAKED_TRIPLE: ok = naked_triple(&L); break;
    case TECH_X_WING:       ok = fish(&L, 2); break;
    case TECH_SWORDFISH:    ok = fish(&L, 3); break;
    case TECH_XY_WING:      ok = xy_wing(&L); break;
    }
    s_trace = NULL;
    if (!ok || out->tech.kind != tech || L.broken) return false;
    int c = find_hidden_single(&L, &d, &u);
    if (c >= 0) {
        out->kind = STEP_HIDDEN_SINGLE;
        out->unit = u;
    } else if ((c = find_naked_single(&L, &d)) >= 0) {
        out->kind = STEP_NAKED_SINGLE;
    } else {
        return false;                                   /* it narrowed things down, but no single yet */
    }
    out->cell = c;
    out->digit = d;
    return d == solution[c];
}

int sudoku_unit_cell(const sudoku_geom_t *G, int unit, int k)
{
    if (unit < 0 || unit >= UNITS_MAX || !G->active[unit] || k < 0 || k >= G->n) return -1;
    return G->unit[unit][k];
}

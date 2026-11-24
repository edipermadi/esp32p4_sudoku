/**
 * @file gen_solver.c
 * @ingroup generator
 * @brief Counting and filling: the solver (every variant's rules checked in grid_free()), the
 *        solution count, a random full grid; the rule tables it uses.
 */
#include "gen_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Kropki, XV, Greater-Than, Fortress: [kind][mark][a] = the digits b may be when the left / top
 * cell is a (s_edge_fwd), and the digits a may be when the right / bottom one is b
 * (s_edge_bwd) */
static uint16_t s_edge_fwd[EDGE_KINDS][3][10];   /**< [kind][mark][a]: the digits the right / bottom cell may be */
static uint16_t s_edge_bwd[EDGE_KINDS][3][10];   /**< [kind][mark][b]: the digits the left / top cell may be */

/* (documented in gen_internal.h) */
void gen_edge_tables(void)
{
    for (int k = 0; k < EDGE_KINDS; k++) {
        for (int m = 0; m < 3; m++) {
            for (int a = 1; a <= 9; a++) {
                uint16_t f = 0, b = 0;
                for (int o = 1; o <= 9; o++) {
                    if (is_sudoku_edge_ok(k, m, a, o)) {
                        f |= (uint16_t)(1u << o);
                    }
                    if (is_sudoku_edge_ok(k, m, o, a)) {
                        b |= (uint16_t)(1u << o);
                    }
                }
                s_edge_fwd[k][m][a] = f;
                s_edge_bwd[k][m][a] = b;
            }
        }
    }
}

/* (documented in gen_internal.h) */
uint16_t gen_edge_allow(const sudoku_geom_t *G, int i, const uint8_t *v, const uint16_t *cand)
{
    int k = G->edge_kind;
    uint16_t free = 0x3FE;
    const int nb[4] = {i % 9 > 0 ? i - 1 : -1, i % 9 < 8 ? i + 1 : -1, i > 8 ? i - 9 : -1, i < 72 ? i + 9 : -1};
    const int mk[4] = {nb[0] >= 0 ? EDGE_RIGHT(G->edge[i - 1]) : 0, EDGE_RIGHT(G->edge[i]),
                       nb[2] >= 0 ? EDGE_DOWN(G->edge[i - 9]) : 0, EDGE_DOWN(G->edge[i])
                      };
    for (int j = 0; j < 4; j++) {
        if (nb[j] < 0 || ((k == EDGE_GT || k == EDGE_FORT) && !mk[j])) {
            continue;
        }
        bool before = j == 0 || j == 2;             /* the neighbour is left of / above i */
        if (v[nb[j]]) {                             /* (filled, as in the search: its digit's row of the table) */
            free &= before ? s_edge_fwd[k][mk[j]][v[nb[j]]] : s_edge_bwd[k][mk[j]][v[nb[j]]];
            continue;
        }
        uint16_t others = cand ? cand[nb[j]] : 0;
        if (!others) {
            continue;    /* (empty, and no candidates to go by) */
        }
        uint16_t ok = 0;
        for (int w = 1; w <= 9; w++) {
            if (others >> w & 1) {
                ok |= before ? s_edge_fwd[k][mk[j]][w] : s_edge_bwd[k][mk[j]][w];
            }
        }
        free &= ok;
    }
    return free;
}

/** @brief a 10-bit mask's lowest bit, highest bit and count (0 for an empty mask), in RAM: the board's core has no
 *  ctz, clz or popcount instruction (library calls there), and X-Sums, Lockout Lines and Rossini ask millions of
 *  times a puzzle. These three were measured with this RAM copy; the rest of the solver uses gen_bits.h's
 *  tables (in flash), which measured 5-14 % faster on the board than the builtins (2026-11-23). */
static uint8_t s_mask_lo[1024], s_mask_hi[1024], s_mask_n[1024];

/** @brief fills s_mask_lo, s_mask_hi and s_mask_n (once; two tasks doing it at once write the same values) */
static void mask_tables(void)
{
    if (s_mask_n[0x3FE]) {
        return;
    }
    for (int m = 1; m < 1024; m++) {
        int lo = -1, hi = 0, n = 0;
        for (int d = 0; d <= 9; d++) {
            if (m >> d & 1) {
                lo = lo < 0 ? d : lo;
                hi = d;
                n++;
            }
        }
        s_mask_lo[m] = (uint8_t)lo;
        s_mask_hi[m] = (uint8_t)hi;
        s_mask_n[m] = (uint8_t)n;
    }
}

/** @brief the lowest bit of a 10-bit mask, by table (mask_tables() filled) */
#define mask_lo(m) (s_mask_lo[(m) & 0x3FF])
/** @brief the highest bit of a 10-bit mask, by table */
#define mask_hi(m) (s_mask_hi[(m) & 0x3FF])
/** @brief how many bits of a 10-bit mask are set, by table */
#define mask_n(m) (s_mask_n[(m) & 0x3FF])

/** @name gen_line_allow()'s helpers (v, cand: its arguments)
 *  @{ */
#define LMASK(c) ((uint16_t)(v[c] ? (1u << v[c]) : cand ? cand[c] : 0x3FE))   /**< cell c's digits: its digit, else its candidates (all without them) */
#define LO(m) (gen_lowest(m))                       /**< the smallest digit in a mask */
#define HI(m) (gen_highest(m))               /**< the largest digit in a mask */
/** the digits a..b as a mask (clamped to 1..9; empty when a > b) */
#define RANGE(a, b) ((uint16_t)((a) > (b) || (b) < 1 || (a) > 9 ? 0 : \
                     (((1u << ((b) > 9 ? 10 : (b) + 1)) - 1) & ~((1u << ((a) < 1 ? 1 : (a))) - 1))))
/** @} */

/**
 * @brief Entropic, Modular Lines: the digits the cell at a place of a line may hold (gen_line_allow()'s
 * case, kept apart: inside it, the bigger function made every other line kind slower, Between
 * Lines by 15-20 %)
 * @param[in] G     the board
 * @param[in] t     the line
 * @param[in] p     the place on it
 * @param[in] v     the digits (0: empty)
 * @param[in] cand  the candidates of the empty ones, or NULL
 * @return a candidate mask
 */
static __attribute__((noinline)) uint16_t group_line_allow(const sudoku_geom_t *G, int t, int p, const uint8_t *v,
        const uint16_t *cand)
{
    const int n = G->thermo_n[t];
    const uint8_t *cell = G->thermo_cell[t];
    /* places 3 apart share a group, and places 0, 1, 2 (mod 3) take three different ones:
     * per place mod 3, the groups every cell there can still be in (bit g = group g) */
    uint8_t can[3] = {7, 7, 7};
    uint16_t of[3] = {0, 0, 0};                 /* each group's digits */
    for (int d = 1; d <= 9; d++) {
        of[sudoku_line_group(G->line_kind, d)] |= (uint16_t)(1u << d);
    }
    for (int k = 0; k < n; k++) {
        if (k == p) {
            continue;
        }
        uint16_t m = LMASK(cell[k]);
        uint8_t gm = 0;
        for (int g = 0; g < 3; g++) {
            gm |= (m & of[g]) ? (uint8_t)(1u << g) : 0;
        }
        can[k % 3] &= gm;
    }
    const int own = p % 3, o1 = (own + 1) % 3, o2 = (own + 2) % 3;
    uint16_t ok = 0;
    for (int g = 0; g < 3; g++) {               /* a group for this place that leaves the others two */
        if (!(can[own] >> g & 1)) {
            continue;
        }
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                if (a != g && b != g && a != b && (can[o1] >> a & 1) && (can[o2] >> b & 1)) {
                    ok |= of[g];
                }
            }
        }
    }
    return ok;
}

/**
 * @brief Lockout Lines, Double Arrow: the digits the cell at a place of a line may hold (gen_line_allow()'s
 * cases, kept apart like group_line_allow())
 * @param[in] G     the board
 * @param[in] t     the line
 * @param[in] p     the place on it
 * @param[in] v     the digits (0: empty)
 * @param[in] cand  the candidates of the empty ones, or NULL
 * @return a candidate mask
 */
static __attribute__((noinline)) uint16_t ends_line_allow(const sudoku_geom_t *G, int t, int p, const uint8_t *v,
        const uint16_t *cand)
{
    const int n = G->thermo_n[t];
    const uint8_t *cell = G->thermo_cell[t];
    const uint16_t ma = LMASK(cell[0]), mb = LMASK(cell[n - 1]);
    if (!ma || !mb) {
        return 0;
    }
    mask_tables();
    uint16_t ok = 0;
    if (G->line_kind == LINE_LOCKOUT) {             /* diamonds 4+ apart, every digit between outside their range */
        /* a cell between can go outside lo..hi when its smallest digit is below lo or its largest above
         * hi: for each lo, cap[lo] is the most hi can be (below every such largest, of the cells whose
         * smallest isn't below lo) */
        int clo[9], chi[9], nb = 0;
        for (int k = 1; k < n - 1; k++) {
            if (k == p) {
                continue;
            }
            const uint16_t m = LMASK(cell[k]);
            if (!m) {
                return 0;
            }
            clo[nb] = mask_lo(m);
            chi[nb++] = mask_hi(m);
        }
        int cap[10];
        for (int lo = 1; lo <= 9; lo++) {
            cap[lo] = 9;
            for (int j = 0; j < nb; j++) {
                if (clo[j] >= lo && chi[j] - 1 < cap[lo]) {
                    cap[lo] = chi[j] - 1;
                }
            }
        }
        for (int a = 1; a <= 9; a++) {
            if (!(ma >> a & 1)) {
                continue;
            }
            for (int b = 1; b <= 9; b++) {
                int lo = a < b ? a : b, hi = a < b ? b : a;
                if (!(mb >> b & 1) || hi - lo < 4 || hi > cap[lo]) {
                    continue;
                }
                ok |= p == 0 ? (uint16_t)(1u << a) : p == n - 1 ? (uint16_t)(1u << b)
                      : (uint16_t)(RANGE(1, lo - 1) | RANGE(hi + 1, 9));
            }
        }
        return ok;
    }
    /* Double Arrow: the two circles add up to the cells between them */
    int smin = 0, smax = 0;                         /* the cells between, the place asked about left out */
    for (int k = 1; k < n - 1; k++) {
        if (k == p) {
            continue;
        }
        uint16_t m = LMASK(cell[k]);
        if (!m) {
            return 0;
        }
        smin += LO(m);
        smax += HI(m);
    }
    if (p == 0 || p == n - 1) {
        const uint16_t mo = p ? ma : mb;            /* the other circle */
        return RANGE(smin - HI(mo), smax - LO(mo));
    }
    return RANGE(LO(ma) + LO(mb) - smax, HI(ma) + HI(mb) - smin);
}

/**
 * @brief 10 Lines: the running sums (0..9, bit s) a run can be at after adding a digit from m
 * @param[in] from  the running sums before (bit s)
 * @param[in] m  the digits (bit d)
 * @return the running sums after: s + d below 10, or 0 where a run reaches 10
 */
static inline uint16_t ten_step(uint16_t from, uint16_t m)
{
    uint32_t to = 0;                                /* (a plain loop: no ctz instruction on the board's core) */
    for (int d = 1; d <= 9; d++) {
        if (m >> d & 1) {
            const uint32_t sh = (uint32_t)from << d;   /* bit s + d */
            to |= (sh & 0x3FF) | (sh >> 10 & 1);     /* (exactly 10: a new run, sum 0) */
        }
    }
    return (uint16_t)to;
}

/**
 * @brief 10 Lines: the running sums (bit s) from which adding a digit from m lands in one of to
 * @param[in] to  the running sums wanted after (bit s)
 * @param[in] m  the digits (bit d)
 * @return the running sums before
 */
static inline uint16_t ten_back(uint16_t to, uint16_t m)
{
    uint32_t from = 0;
    for (int d = 1; d <= 9; d++) {
        if (m >> d & 1) {
            from |= ((uint32_t)to >> d) & ((1u << (10 - d)) - 1);   /* s + d below 10, and in to */
            from |= (to & 1u) << (10 - d);           /* s + d exactly 10, a new run (sum 0) in to */
        }
    }
    return (uint16_t)from;
}

/* (documented in generator.h) */
uint16_t sudoku_ten_allow(const uint16_t *m, int n, int p)
{
    uint16_t fwd = 1;                               /* (a run starts at the line's start) */
    for (int k = 0; k < (p < 0 ? n : p); k++) {
        fwd = ten_step(fwd, m[k]);
    }
    if (p < 0) {
        return (uint16_t)(fwd & 1);                 /* (and ends at its end) */
    }
    uint16_t back = 1;                              /* the sums after p from which the rest can end at 0 */
    for (int k = n - 1; k > p; k--) {
        back = ten_back(back, m[k]);
    }
    uint16_t ok = 0;
    for (int d = 1; d <= 9; d++) {
        const uint32_t sh = (uint32_t)fwd << d;     /* (ten_step() for the one digit) */
        if ((m[p] >> d & 1) && (((sh & 0x3FF) | (sh >> 10 & 1)) & back)) {
            ok |= (uint16_t)(1u << d);
        }
    }
    return ok;
}

/**
 * @brief Pill Arrows: the digits the cell at a place of an arrow may hold: the pill (places 0 and 1, tens and
 * units) read as a number is the shaft's sum (gen_line_allow()'s case, kept apart like group_line_allow())
 * @param[in] G     the board
 * @param[in] t     the line
 * @param[in] p     the place on it
 * @param[in] v     the digits (0: empty)
 * @param[in] cand  the candidates of the empty ones, or NULL
 * @return a candidate mask
 */
static __attribute__((noinline)) uint16_t pill_line_allow(const sudoku_geom_t *G, int t, int p, const uint8_t *v,
        const uint16_t *cand)
{
    const int n = G->thermo_n[t];
    const uint8_t *cell = G->thermo_cell[t];
    const uint16_t mt = LMASK(cell[0]), mu = LMASK(cell[1]);
    if (!mt || !mu) {
        return 0;
    }
    int smin = 0, smax = 0;                         /* the shaft's sum, the place asked about left out */
    for (int k = 2; k < n; k++) {
        if (k == p) {
            continue;
        }
        const uint16_t m = LMASK(cell[k]);
        if (!m) {
            return 0;
        }
        smin += LO(m);
        smax += HI(m);
    }
    uint16_t ok = 0;
    if (p >= 2) {                                   /* the shaft: what's left of the pill's number */
        return RANGE(10 * LO(mt) + LO(mu) - smax, 10 * HI(mt) + HI(mu) - smin);
    }
    for (int a = 1; a <= 9; a++) {                  /* a pill whose number the shaft can make */
        for (int b = 1; b <= 9; b++) {
            const int num = 10 * a + b;
            if ((mt >> a & 1) && (mu >> b & 1) && num >= smin && num <= smax) {
                ok |= (uint16_t)(1u << (p ? b : a));
            }
        }
    }
    return ok;
}

/* (documented in gen_internal.h) */
uint16_t gen_line_allow(const sudoku_geom_t *G, int t, int p, const uint8_t *v, const uint16_t *cand)
{
    const int n = G->thermo_n[t];
    const uint8_t *cell = G->thermo_cell[t];
    uint16_t free = 0x3FE;
    switch (G->line_kind) {
    case LINE_THERMO:                               /* above the cell before it, below the one after */
    case LINE_SLOW: {                               /* (Slow: or the same, where the two don't see each other) */
        const int up = p > 0 ? sudoku_thermo_step(G, t, p) : 1, down = p + 1 < n ? sudoku_thermo_step(G, t, p + 1) : 1;
        if (p > 0 && v[cell[p - 1]]) {
            free &= (uint16_t)~((1u << (v[cell[p - 1]] + up)) - 1);
        }
        if (p + 1 < n && v[cell[p + 1]]) {
            free &= (uint16_t)((1u << (v[cell[p + 1]] - down + 1)) - 1);
        }
        if (cand) {
            if (p > 0 && LMASK(cell[p - 1])) {
                free &= (uint16_t)~((1u << (LO(LMASK(cell[p - 1])) + up)) - 1);
            }
            if (p + 1 < n && LMASK(cell[p + 1])) {
                free &= (uint16_t)((1u << (HI(LMASK(cell[p + 1])) - down + 1)) - 1);
            }
        }
        break;
    }
    case LINE_PARITY:                               /* the other parity from each neighbour on it */
        for (int k = -1; k <= 1; k += 2) {
            int q = p + k;
            if (q >= 0 && q < n && LMASK(cell[q])) {
                const uint16_t m = LMASK(cell[q]), even = 0x154, odd = 0x2AA;
                free &= (uint16_t)((m & even ? odd : 0) | (m & odd ? even : 0));
            }
        }
        break;
    case LINE_WHISPER:                              /* 5 (Dutch: 4) or more away from each neighbour on it */
    case LINE_DUTCH:
        for (int k = -1; k <= 1; k += 2) {
            int q = p + k;
            if (q < 0 || q >= n) {
                continue;
            }
            const uint16_t m = LMASK(cell[q]);
            if (!m) {
                continue;
            }
            /* d is k or more from some w of m: from the largest (d <= HI - k) or the smallest (d >= LO + k) */
            const int k = G->line_kind == LINE_DUTCH ? 4 : 5;
            free &= (uint16_t)(RANGE(1, HI(m) - k) | RANGE(LO(m) + k, 9));
        }
        break;
    case LINE_NABNER:                               /* 2 or more away from every other digit on the line */
        for (int k = 0; k < n; k++) {
            const uint16_t m = k == p ? 0 : LMASK(cell[k]);
            if (m) {                                /* (2 or more from some w of m: as Whispers' neighbours) */
                free &= (uint16_t)(RANGE(1, HI(m) - 2) | RANGE(LO(m) + 2, 9));
            }
        }
        break;
    case LINE_RENBAN: {                             /* different digits, all within a window of n */
        int lo = 10, hi = 0;
        for (int k = 0; k < n; k++) {
            if (k == p || !v[cell[k]]) {
                continue;
            }
            free &= (uint16_t)~(1u << v[cell[k]]);
            if (v[cell[k]] < lo) {
                lo = v[cell[k]];
            }
            if (v[cell[k]] > hi) {
                hi = v[cell[k]];
            }
        }
        if (hi) {
            free &= RANGE(hi - n + 1, lo + n - 1);
        }
        if (cand) {                                 /* a window that leaves every other cell a candidate */
            uint16_t ok = 0;
            for (int w0 = 1; w0 + n - 1 <= 9; w0++) {
                uint16_t win = RANGE(w0, w0 + n - 1);
                bool fits = true;
                for (int k = 0; k < n && fits; k++) {
                    if (k != p) {
                        fits = (LMASK(cell[k]) & win) != 0;
                    }
                }
                if (fits) {
                    ok |= win;
                }
            }
            free &= ok;
        }
        break;
    }
    case LINE_PALINDROME: {                         /* the same digit as the cell mirroring it */
        int m = n - 1 - p;
        if (m != p && LMASK(cell[m])) {
            free &= LMASK(cell[m]);
        }
        break;
    }
    case LINE_REGIONSUM: {                          /* each box's part adds up to the same total */
        /* per box the line crosses: the range its part can add up to (its cells' digits differ: a
         * box); the cell asked about's own box without it (own_lo, own_hi) */
        int box[9], lo[9], hi[9], e[9], elo[9], ehi[9], nb = 0, own = -1;
        for (int k = 0; k < n; k++) {
            int b = G->region[cell[k]], j = 0;
            while (j < nb && box[j] != b) {
                j++;
            }
            if (j == nb) {
                box[nb] = b;
                lo[nb] = hi[nb] = e[nb] = elo[nb] = ehi[nb] = 0;
                nb++;
            }
            if (k == p) {
                own = j;
                continue;
            }
            uint16_t m = LMASK(cell[k]);
            if (!m) {
                return 0;
            }
            if (v[cell[k]]) {
                lo[j] += v[cell[k]];
                hi[j] += v[cell[k]];
            } else {
                e[j]++;
                elo[j] += LO(m);
                ehi[j] += HI(m);
            }
        }
        int tlo = 0, thi = 81, own_lo = 0, own_hi = 0;
        for (int j = 0; j < nb; j++) {
            int a = e[j] * (e[j] + 1) / 2, b = e[j] * (19 - e[j]) / 2;   /* e distinct digits: at least, at most */
            int l = lo[j] + (elo[j] > a ? elo[j] : a), h = hi[j] + (ehi[j] < b ? ehi[j] : b);
            if (j == own) {
                own_lo = l;
                own_hi = h;
            } else {
                tlo = l > tlo ? l : tlo;
                thi = h < thi ? h : thi;
            }
        }
        if (nb > 1) {
            free &= RANGE(tlo - own_hi, thi - own_lo);
        }
        break;
    }
    case LINE_ZIPPER: {                             /* cells mirrored about the middle: one total */
        /* the totals still possible (bit s = s, 2..18): each other pair's sums, and on an odd line
         * the middle cell's digit; then the cell's digit is a total less its mirror's */
        const int mid = n / 2, odd = n % 2, m = n - 1 - p;
        uint32_t tot = 0x7FFFCu;
        if (odd && p != mid) {
            tot &= LMASK(cell[mid]);
        }
        for (int k = 0; k < n / 2; k++) {
            if (k == p || n - 1 - k == p) {
                continue;
            }
            uint16_t ma = LMASK(cell[k]), mb = LMASK(cell[n - 1 - k]);
            uint32_t sums = 0;
            for (int w = 1; w <= 9; w++) {
                if (ma >> w & 1) {
                    sums |= (uint32_t)mb << w;
                }
            }
            tot &= sums;
        }
        if (odd && p == mid) {
            free &= (uint16_t)(tot & 0x3FE);
        } else {
            uint16_t mw = LMASK(cell[m]), ok = 0;
            for (int w = 1; w <= 9; w++) {
                if (mw >> w & 1) {
                    ok |= (uint16_t)((tot >> w) & 0x3FE);
                }
            }
            free &= ok;
        }
        break;
    }
    case LINE_BETWEEN: {                            /* strictly between the circles at its two ends */
        uint16_t ma = LMASK(cell[0]), mb = LMASK(cell[n - 1]), ok = 0;
        if (!ma || !mb) {
            return 0;
        }
        /* a cell between has a digit inside lo..hi (strictly) when its smallest digit above lo is below hi:
         * for each lo, past[lo] is the most of those over the cells, and hi must be above it */
        int past[10];
        for (int lo = 1; lo <= 9; lo++) {
            past[lo] = lo;
            for (int k = 1; k < n - 1; k++) {
                if (k == p) {
                    continue;
                }
                const uint16_t above = (uint16_t)(LMASK(cell[k]) & ~((2u << lo) - 1));
                const int x = above ? LO(above) : 10;   /* (none above lo: no hi will do) */
                past[lo] = x > past[lo] ? x : past[lo];
            }
        }
        for (int a = 1; a <= 9; a++) {              /* a pair of circles that leaves every cell between a digit */
            for (int b = 1; b <= 9; b++) {
                int lo = a < b ? a : b, hi = a < b ? b : a;
                if (!(ma >> a & 1) || !(mb >> b & 1) || hi - lo - 1 < G->between_need[t] || hi <= past[lo]) {
                    continue;                       /* (room for the cells between that must differ) */
                }
                ok |= p == 0 ? (uint16_t)(1u << a) : p == n - 1 ? (uint16_t)(1u << b) : RANGE(lo + 1, hi - 1);
            }
        }
        free &= ok;
        break;
    }
    case LINE_ENTROPIC:
    case LINE_MODULAR:                              /* any three in a row: one digit of each group */
        free &= group_line_allow(G, t, p, v, cand);
        break;
    case LINE_TEN: {                                /* runs that each add up to 10 */
        uint16_t m[9];
        for (int k = 0; k < n; k++) {
            m[k] = k == p ? 0x3FE : LMASK(cell[k]);
        }
        free &= sudoku_ten_allow(m, n, p);
        break;
    }
    case LINE_AVG: {                                /* the circle (place 0) times the others' count is their sum */
        const int k = n - 1;
        int smin = 0, smax = 0;
        for (int j = 1; j < n; j++) {
            if (j == p) {
                continue;
            }
            const uint16_t m = LMASK(cell[j]);
            if (!m) {
                return 0;
            }
            smin += LO(m);
            smax += HI(m);
        }
        if (p == 0) {                               /* the circle: a digit whose k times the others can make */
            uint16_t ok = 0;
            for (int d = 1; d <= 9; d++) {
                ok |= d * k >= smin && d * k <= smax ? (uint16_t)(1u << d) : 0;
            }
            free &= ok;
        } else {
            const uint16_t m = LMASK(cell[0]);
            if (!m) {
                return 0;
            }
            free &= RANGE(LO(m) * k - smax, HI(m) * k - smin);
        }
        break;
    }
    case LINE_PILL:                                 /* the pill (places 0, 1: tens, units) is the shaft's sum */
        free &= pill_line_allow(G, t, p, v, cand);
        break;
    case LINE_LOCKOUT:                              /* the ends: diamonds (4+ apart), or circles (Double Arrow) */
    case LINE_DARROW:
        free &= ends_line_allow(G, t, p, v, cand);
        break;
    case LINE_ARROW: {                              /* the circle (place 0) is the shaft's sum */
        int smin = 0, smax = 0;                     /* the shaft's sum, the place asked about left out */
        for (int k = 1; k < n; k++) {
            if (k == p) {
                continue;
            }
            uint16_t m = LMASK(cell[k]);
            if (!m) {
                return 0;
            }
            smin += LO(m);
            smax += HI(m);
        }
        if (p == 0) {
            free &= RANGE(smin, smax);
        } else {
            uint16_t m = LMASK(cell[0]);
            if (!m) {
                return 0;
            }
            free &= RANGE(LO(m) - smax, HI(m) - smin);
        }
        break;
    }
    }
#undef LMASK
#undef LO
#undef HI
#undef RANGE
    return free;
}

/* (documented in generator.h) */
int sudoku_quad_missing(const sudoku_geom_t *g, int q, const uint8_t vals[81], int *empty)
{
    int need[10] = {0}, miss = 0, at = g->quad_at[q];
    for (int k = 0; k < g->quad_n[q]; k++) {
        need[g->quad_dig[q][k]]++;
    }
    const int cells[4] = {at, at + 1, at + 9, at + 10};
    *empty = 0;
    for (int k = 0; k < 4; k++) {
        int v = vals[cells[k]];
        if (!v) {
            (*empty)++;
        } else if (need[v]) {
            need[v]--;
        }
    }
    for (int d = 1; d <= 9; d++) {
        miss += need[d];
    }
    return miss;
}

/** @brief sums of k distinct digits from 2..8: bit s set if s can be made (k 0..7) */
static uint64_t s_bread[8];

/* (documented in gen_internal.h) */
void gen_bread_tables(void)
{
    if (s_bread[0]) {
        return;
    }
    for (int m = 0; m < 128; m++) {                 /* subsets of 2..8 */
        int k = gen_popcount(m), sum = 0;
        for (int b = 0; b < 7; b++) {
            if (m >> b & 1) {
                sum += b + 2;
            }
        }
        s_bread[k] |= 1ull << sum;
    }
}

/* ---------------------------------------------------------------- counting / filling */

/** @brief Called now and then from the long loops (each attempt, each clue tried, every 4096 search
 * nodes). A no-op here (and on a PC); the firmware's own gives the idle task a turn, since a
 * puzzle can take more than the task watchdog's 5 s (Greater-Than Expert did, on core 0). */
__attribute__((weak)) void sudoku_gen_yield(void) {}

static uint32_t s_yield_nodes;              /**< search nodes, for sudoku_gen_yield() every 4096 */

/** @brief The search's board: digits, each unit's digits used, and the cages. */
typedef struct {
    const sudoku_geom_t *G;             /**< the board */
    uint8_t v[81];                      /**< the digits, 0 = empty */
    uint16_t um[UNITS_MAX];             /**< bit d = digit d used in the unit */
    int8_t units[81][5];                /**< the cell's units, -1 terminated */
    uint32_t nodes;                     /**< the nodes searched */
    uint32_t node_limit;                /**< at most (0 = no limit); reached: aborted */
    bool aborted;                       /**< the node limit was reached */
    /* Killer: per cage, the digits in it, what's left of its sum, its empty cells */
    uint16_t cage_used[CAGES_MAX];      /**< Killer: each cage's digits */
    int8_t cage_left[CAGES_MAX];        /**< what's left of its sum */
    int8_t cage_empty[CAGES_MAX];       /**< its empty cells */
    uint32_t rules;                     /**< the board's rules beyond its units, R_* (grid_load()): one word to
                                         *   test in grid_free(), not a dozen flags spread over the board (in
                                         *   PSRAM on the board: each a cache miss) */
} grid_t;

/** @name The rules a board has beyond its units (grid_t.rules)
 *  @{ */
#define R_XPEER   (1u << 0)             /**< Anti-Knight, Anti-King: cells that see each other outside the units */
#define R_CONSEC  (1u << 1)             /**< Consecutive, Non-Consecutive */
#define R_EDGE    (1u << 2)             /**< Kropki, XV, Greater-Than */
#define R_QUAD    (1u << 3)             /**< Quadruple */
#define R_SKY     (1u << 4)             /**< Skyscraper */
#define R_XSUM    (1u << 5)             /**< X-Sums */
#define R_FIRST3  (1u << 6)             /**< Frame, Outside Sudoku, Rossini */
#define R_DIAGNC  (1u << 7)             /**< Diagonal Non-Consecutive */
#define R_LK      (1u << 8)             /**< Little Killer */
#define R_SANDWICH (1u << 9)            /**< Sandwich */
#define R_LINE    (1u << 10)            /**< the lines */
#define R_CAGE    (1u << 11)            /**< Killer */
#define R_NROOM   (1u << 12)            /**< Numbered Rooms */
#define R_SKYSUM  (1u << 13)            /**< Sum Skyscrapers */
#define R_159     (1u << 14)            /**< 159 Sudoku */
#define R_ANTIDIAG (1u << 15)           /**< Anti-Diagonal */
#define R_PSKY    (1u << 16)            /**< Odd/Even Skyscrapers */
#define R_MAGIC   (1u << 17)            /**< Magic Square */
#define R_FLATMATES (1u << 18)          /**< Dutch Flatmates */
#define R_MATHRAX (1u << 19)            /**< Mathrax */
/** @} */

/**
 * @brief Puts a digit in a cell: its units and cage take it.
 * @param[in,out] g  the search's board
 * @param[in] i  the cell
 * @param[in] d  the digit
 */
static void grid_set(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = (uint8_t)d;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) {
        g->um[g->units[i][k]] |= m;
    }
    int c = g->G->cage_of[i];
    if (c != CAGE_NONE) {
        g->cage_used[c] |= m;
        g->cage_left[c] = (int8_t)(g->cage_left[c] - d);
        g->cage_empty[c]--;
    }
}

/**
 * @brief Takes a digit out of a cell (undoes grid_set()).
 * @param[in,out] g  the search's board
 * @param[in] i  the cell
 * @param[in] d  the digit
 */
static void grid_clear(grid_t *g, int i, int d)
{
    uint16_t m = (uint16_t)(1u << d);
    g->v[i] = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) {
        g->um[g->units[i][k]] &= (uint16_t)~m;
    }
    int c = g->G->cage_of[i];
    if (c != CAGE_NONE) {
        g->cage_used[c] &= (uint16_t)~m;
        g->cage_left[c] = (int8_t)(g->cage_left[c] + d);
        g->cage_empty[c]++;
    }
}

/**
 * @brief smallest and largest sums of k distinct digits from mask
 * @param[in] mask  the digits (bit d = d)
 * @param[in] k  how many
 * @param[out] lo  gets the smallest sum (0: fewer than k digits)
 * @param[out] hi  gets the largest
 */
static void mask_k_range(uint16_t mask, int k, int *lo, int *hi)
{
    *lo = *hi = 0;
    for (int d = 1, n = 0; d <= 9 && n < k; d++) {
        if (mask >> d & 1) {
            *lo += d;
            n++;
        }
    }
    for (int d = 9, n = 0; d >= 1 && n < k; d--) {
        if (mask >> d & 1) {
            *hi += d;
            n++;
        }
    }
}

/**
 * @brief X-Sums: can a row or column with these digits per place (from the clue) still make clue c?
 * @param[in] m  each place's digits (a filled one: its digit)
 * @param[in] c  the clue
 * @return true: still possible
 */
static bool is_xsum_ok(const uint16_t m[9], int c)
{
    uint16_t xs = (uint16_t)(m[0] & sudoku_xsum_firsts(c));
    for (int x = 1; x <= 9; x++) {
        if (!(xs >> x & 1)) {
            continue;
        }
        if (x == 1) {
            return true;                            /* (c is 1) */
        }
        /* places 1..x-1: other digits than x, adding up to c - x */
        int known = 0, ne = 0, lo = 0, hi = 0;
        uint16_t used = (uint16_t)(1u << x), any = 0;
        bool fits = true;
        for (int k = 1; k < x && fits; k++) {
            uint16_t mk = (uint16_t)(m[k] & ~(1u << x));
            if (!mk) {
                fits = false;
            } else if (!(mk & (mk - 1))) {          /* one digit: placed (or forced) */
                if (used & mk) {
                    fits = false;
                }
                used |= mk;
                known += mask_lo(mk);
            } else {
                ne++;
                any |= mk;
                lo += mask_lo(mk);
                hi += mask_hi(mk);
            }
        }
        if (!fits) {
            continue;
        }
        int dlo, dhi;                               /* ne different digits, none of those placed */
        mask_k_range((uint16_t)(any & ~used), ne, &dlo, &dhi);
        if (ne && mask_n(any & ~used) < ne) {
            continue;
        }
        lo = dlo > lo ? dlo : lo;
        hi = dhi < hi ? dhi : hi;
        if (c - x - known >= lo && c - x - known <= hi) {
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
uint16_t gen_xsum_allow(const sudoku_geom_t *G, int lu, int at, const uint8_t *v, const uint16_t *cand)
{
    int cells[9], c = G->xsum[lu];
    mask_tables();
    line_cells(lu, cells);
    if (at > 0 && v[cells[0]] && at >= v[cells[0]]) {
        return 0x3FE;                               /* past the first X: the clue doesn't reach it */
    }
    uint16_t placed = 0, m[9];
    for (int k = 0; k < 9; k++) {
        placed |= v[cells[k]] ? (uint16_t)(1u << v[cells[k]]) : 0;
    }
    for (int k = 0; k < 9; k++) {
        m[k] = v[cells[k]] ? (uint16_t)(1u << v[cells[k]]) : cand ? cand[cells[k]] : (uint16_t)(0x3FE & ~placed);
    }
    uint16_t free = 0, mine = m[at];
    for (int d = 1; d <= 9; d++) {
        if (!(mine >> d & 1)) {
            continue;
        }
        m[at] = (uint16_t)(1u << d);
        if (is_xsum_ok(m, c)) {
            free |= (uint16_t)(1u << d);
        }
    }
    return free;
}

/**
 * @brief Do a row's or column's first three digits keep its clue (Frame, Outside Sudoku, Rossini)?
 * @param[in] G  the board
 * @param[in] lu  the row or column
 * @param[in] a  the first digit from the clue
 * @param[in] b  the second
 * @param[in] c  the third
 * @return true if so
 */
static inline bool is_first3_ok(const sudoku_geom_t *G, int lu, int a, int b, int c)
{
    if (G->frame && G->frame_sum[lu] && a + b + c != G->frame_sum[lu]) {
        return false;
    }
    if (G->outside && (G->outside_set[lu] & ~((1u << a) | (1u << b) | (1u << c)))) {
        return false;                               /* a digit of the clue not among them */
    }
    if (G->rossini) {
        bool up = a < b && b < c, down = a > b && b > c;
        int dir = G->rossini_dir[lu];
        return dir == ROSSINI_UP ? up : dir == ROSSINI_DOWN ? down : !up && !down;
    }
    return true;
}

/**
 * @brief the digits a..b as a mask (clamped to 1..9; empty when a > b), as gen_line_allow()'s RANGE()
 * @param[in] a  the first
 * @param[in] b  the last
 * @return the mask
 */
static inline uint16_t digits_between(int a, int b)
{
    a = a < 1 ? 1 : a;
    b = b > 9 ? 9 : b;
    return a > b ? 0 : (uint16_t)(((1u << (b + 1)) - 1) & ~((1u << a) - 1));
}

/* (documented in gen_internal.h) */
uint16_t gen_first3_allow(const sudoku_geom_t *G, int lu, int at, const uint8_t *v, const uint16_t *cand)
{
    if (at > 2) {
        return 0x3FE;
    }
    int cells[9];
    line_cells(lu, cells);
    uint16_t placed = 0, m[3];
    for (int k = 0; k < 9; k++) {
        placed |= v[cells[k]] ? (uint16_t)(1u << v[cells[k]]) : 0;
    }
    for (int k = 0; k < 3; k++) {
        m[k] = v[cells[k]] ? (uint16_t)(1u << v[cells[k]]) : cand ? cand[cells[k]] : (uint16_t)(0x3FE & ~placed);
    }
    const int o1 = at == 0 ? 1 : 0, o2 = at == 2 ? 1 : 2;   /* the other two of the first three */
    const int dir = G->rossini && !G->frame && !G->outside ? G->rossini_dir[lu] : ROSSINI_NONE;
    if (dir != ROSSINI_NONE) {                      /* Rossini's arrow: rising (or falling) from the clue, directly */
        if (!m[o1] || !m[o2]) {
            return 0;
        }
        mask_tables();
        const bool up = dir == ROSSINI_UP;
        uint16_t free = 0;
        for (int d = 1; d <= 9; d++) {
            if (!(m[at] >> d & 1)) {
                continue;
            }
            bool fits;
            if (at == 1) {                          /* the middle: a first digit on one side, a third on the other */
                fits = up ? mask_lo(m[0]) < d && d < mask_hi(m[2]) : mask_hi(m[0]) > d && d > mask_lo(m[2]);
            } else if (at == 0) {                   /* the first: a middle digit between it and the third's reach */
                fits = (m[1] & (up ? digits_between(d + 1, mask_hi(m[2]) - 1) : digits_between(mask_lo(m[2]) + 1, d - 1))) != 0;
            } else {                                /* the third: a middle digit between the first's reach and it */
                fits = (m[1] & (up ? digits_between(mask_lo(m[0]) + 1, d - 1) : digits_between(d + 1, mask_hi(m[0]) - 1))) != 0;
            }
            free |= fits ? (uint16_t)(1u << d) : 0;
        }
        return free;
    }
    const int sum = G->frame ? G->frame_sum[lu] : 0;
    uint16_t free = 0;
    for (uint16_t md = m[at]; md; md &= (uint16_t)(md - 1)) {   /* a digit, with two others the other two cells can hold */
        int d = gen_lowest(md);
        for (uint16_t mx = (uint16_t)(m[o1] & ~(1u << d)); mx && !(free >> d & 1); mx &= (uint16_t)(mx - 1)) {
            int x = gen_lowest(mx);
            uint16_t my = (uint16_t)(m[o2] & ~(1u << d) & ~(1u << x));
            if (sum) {                              /* Frame: the third is what's left of the sum, nothing else to check */
                int y = sum - d - x;
                if (y >= 1 && y <= 9 && (my >> y & 1)) {
                    free |= (uint16_t)(1u << d);
                }
                continue;
            }
            for (; my; my &= (uint16_t)(my - 1)) {
                int y = gen_lowest(my), t[3];
                t[at] = d;
                t[o1] = x;
                t[o2] = y;
                if (is_first3_ok(G, lu, t[0], t[1], t[2])) {
                    free |= (uint16_t)(1u << d);
                    break;
                }
            }
        }
    }
    return free;
}

/* (documented in gen_internal.h) */
bool is_gen_sky_line_ok(const uint8_t x[9], int c)
{
    int seen = 0, top = 0, k = 0;
    for (; k < 9 && x[k]; k++) {
        if (x[k] > top) {
            top = x[k];
            seen++;
        }
    }
    if (seen > c) {
        return false;
    }
    if (top == 9 || k == 9) {
        return seen == c;    /* the 9 hides the rest; or the line is full */
    }
    int rest = 9 - k, more = 9 - top < rest ? 9 - top : rest;   /* at most this many more seen */
    return seen < c && seen + more >= c;            /* (the 9, still to come, is seen) */
}

/* (documented in gen_internal.h) */
uint16_t gen_magic_allow(const uint8_t *v, const uint16_t *cand, int i)
{
    /* the centre box's lines through cell i: its row, its column, and the diagonals through it */
    const int r = i / 9 - 3, q = i % 9 - 3;
    int lines[4][3], nl = 0;
    for (int k = 0; k < 3; k++) {
        lines[nl][k] = (r + 3) * 9 + 3 + k;
    }
    nl++;
    for (int k = 0; k < 3; k++) {
        lines[nl][k] = (3 + k) * 9 + q + 3;
    }
    nl++;
    if (r == q) {
        for (int k = 0; k < 3; k++) {
            lines[nl][k] = (3 + k) * 9 + 3 + k;
        }
        nl++;
    }
    if (r + q == 2) {
        for (int k = 0; k < 3; k++) {
            lines[nl][k] = (3 + k) * 9 + 5 - k;
        }
        nl++;
    }
    uint16_t free = 0x3FE;
    for (int l = 0; l < nl; l++) {                  /* a digit with two others (their digits, or candidates) making 15 */
        int o[2], no = 0;
        for (int k = 0; k < 3; k++) {
            if (lines[l][k] != i) {
                o[no++] = lines[l][k];
            }
        }
        const uint16_t ma = v[o[0]] ? (uint16_t)(1u << v[o[0]]) : cand ? cand[o[0]] : 0x3FE;
        const uint16_t mb = v[o[1]] ? (uint16_t)(1u << v[o[1]]) : cand ? cand[o[1]] : 0x3FE;
        uint16_t ok = 0;
        for (int a = 1; a <= 9; a++) {
            const int d0 = 15 - a;
            for (int b = 1; (ma >> a & 1) && b <= 9; b++) {
                const int d = d0 - b;
                if ((mb >> b & 1) && a != b && d >= 1 && d <= 9 && d != a && d != b) {
                    ok |= (uint16_t)(1u << d);
                }
            }
        }
        free &= ok;
    }
    return free;
}

/* (documented in gen_internal.h) */
bool is_gen_psky_line_ok(const uint8_t x[9], int c)
{
    const bool even = (c & PSKY_EVEN) != 0;
    const int want = c & ~PSKY_EVEN;
    int seen = 0, top = 0, k = 0;
    uint16_t placed = 0;
    for (; k < 9 && x[k]; k++) {
        placed |= (uint16_t)(1u << x[k]);
        if (x[k] > top) {
            top = x[k];
            seen += (x[k] % 2 == 0) == even;
        }
    }
    if (seen > want) {
        return false;
    }
    if (top == 9 || k == 9) {
        return seen == want;                        /* (the 9 hides the rest; or the line is full) */
    }
    int more = 0;                                   /* at most: every digit of that parity above top, still to come */
    for (int d = top + 1; d <= 9; d++) {
        more += !(placed >> d & 1) && (d % 2 == 0) == even;
    }
    const int least = even ? 0 : 1;                 /* (the 9, still to come, is seen: odd) */
    return seen + least <= want && want <= seen + more;
}

/* (documented in gen_internal.h) */
bool is_gen_skysum_line_ok(const uint8_t x[9], int c)
{
    int sum = 0, top = 0, k = 0;
    for (; k < 9 && x[k]; k++) {
        if (x[k] > top) {
            top = x[k];
            sum += top;
        }
    }
    if (sum > c) {
        return false;
    }
    if (top == 9 || k == 9) {
        return sum == c;    /* the 9 hides the rest; or the line is full */
    }
    int rest = 9 - k, more = 0;                     /* at most: the tallest still to come, each one seen */
    for (int d = 9, n = 0; d > top && n < rest; d--, n++) {
        more += d;
    }
    return sum + 9 <= c && c <= sum + more;         /* (the 9, still to come, is seen) */
}

/**
 * @brief Can a row's or column's candidates still keep its Numbered Rooms clue: a first digit X whose X-th
 *        cell can hold the clue?
 * @param[in] m  each cell's candidates, from the clue
 * @param[in] c  the clue
 * @return true: still possible
 */
static bool is_nroom_ok(const uint16_t m[9], int c)
{
    for (int x = 1; x <= 9; x++) {
        if (!(m[0] >> x & 1)) {
            continue;
        }
        if (x == 1 ? c == 1 : (x != c && (m[x - 1] >> c & 1))) {   /* (X = 1: the first cell is the clue itself) */
            return true;
        }
    }
    return false;
}

/* (documented in gen_internal.h) */
uint16_t gen_nroom_allow(const sudoku_geom_t *G, int lu, int at, const uint8_t *v, const uint16_t *cand)
{
    int cells[9], c = G->nroom[lu];
    line_cells(lu, cells);
    uint16_t placed = 0, m[9];
    for (int k = 0; k < 9; k++) {
        placed |= v[cells[k]] ? (uint16_t)(1u << v[cells[k]]) : 0;
    }
    for (int k = 0; k < 9; k++) {
        m[k] = v[cells[k]] ? (uint16_t)(1u << v[cells[k]]) : cand ? cand[cells[k]] : (uint16_t)(0x3FE & ~placed);
    }
    uint16_t free = 0;
    for (int d = 1; d <= 9; d++) {
        if (!(m[at] >> d & 1)) {
            continue;
        }
        uint16_t t[9];                              /* d here, and nowhere else on the line */
        for (int k = 0; k < 9; k++) {
            t[k] = k == at ? (uint16_t)(1u << d) : (uint16_t)(m[k] & ~(1u << d));
        }
        if (is_nroom_ok(t, c)) {
            free |= (uint16_t)(1u << d);
        }
    }
    return free;
}

/** @brief sudoku_fits_why(): which rule took the digit asked about out of grid_free()'s result (NULL:
 * nobody asks, the usual case: one predictable branch per rule) */
typedef struct {
    int d;                              /**< the digit asked about */
    int code;                           /**< FIT_*: the first rule that took it out (FIT_OK: none) */
} why_t;                                /* (one type for both ends: no aliasing surprise) */

static why_t *s_why;                    /**< the question, or NULL */

/** a rule (FIT_*) took the digit asked about out of free */
#define WHY(c) do { if (s_why && !s_why->code && !(free >> s_why->d & 1)) s_why->code = (c); } while (0)

/** @brief Product Killer: every set of digits 1-9 (bit d) with its product, sorted by product (is_product_made()) */
static struct {
    uint32_t prod;                                  /**< the product */
    uint16_t mask;                                  /**< the digits */
} s_prod_sets[512];

/** @brief fills s_prod_sets (once; two tasks doing it at once write the same) */
static void prod_sets_make(void)
{
    if (s_prod_sets[511].prod) {
        return;
    }
    static uint32_t prod[512];
    static uint16_t order[512];
    for (int m = 0; m < 512; m++) {
        prod[m] = 1;
        for (int d = 1; d <= 9; d++) {
            prod[m] *= (m >> (d - 1) & 1) ? (uint32_t)d : 1u;
        }
        order[m] = (uint16_t)m;
    }
    for (int i = 1; i < 512; i++) {                 /* (insertion sort: once, 512 sets) */
        uint16_t x = order[i];
        int j = i;
        for (; j > 0 && prod[order[j - 1]] > prod[x]; j--) {
            order[j] = order[j - 1];
        }
        order[j] = x;
    }
    for (int i = 511; i >= 0; i--) {                /* (the last one written last: it says "made") */
        s_prod_sets[i].mask = (uint16_t)(order[i] << 1);
        s_prod_sets[i].prod = prod[order[i]];
    }
}

/**
 * @brief Product Killer: the digits of every set of k different digits from avail that multiply to p
 * @param[in] p  the product
 * @param[in] k  how many digits
 * @param[in] avail  the digits to take from (bit d)
 * @return the digits that are in one such set at least
 */
static uint16_t product_sets(int p, int k, uint16_t avail)
{
    int lo = 0, hi = 512;                           /* the first set with product p */
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (s_prod_sets[mid].prod < (uint32_t)p) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    uint16_t ok = 0;
    for (; lo < 512 && s_prod_sets[lo].prod == (uint32_t)p; lo++) {
        const uint16_t m = s_prod_sets[lo].mask;
        if (!(m & ~avail) && gen_popcount(m) == k) {
            ok |= m;
        }
    }
    return ok;
}

/* (documented in gen_internal.h) */
uint16_t gen_product_allow(const sudoku_geom_t *G, const uint8_t *v, int c, int i)
{
    int p = G->cage_prod[c], empty = 0;
    uint16_t used = 0;
    for (int j = 0; j < G->cage_n[c]; j++) {        /* what the placed digits leave of the product */
        const int x = G->cage_cell[c][j], d = x == i ? 0 : v[x];
        if (!d) {
            empty++;
        } else if (p % d) {
            return 0;
        } else {
            p /= d;
            used |= (uint16_t)(1u << d);
        }
    }
    prod_sets_make();                               /* (empty: i itself among them) */
    return empty ? product_sets(p, empty, (uint16_t)(0x3FE & ~used)) : 0;
}

/**
 * @brief 159 Sudoku: the digits a cell may hold by its row's columns 1, 5 and 9 (the digit in column k is the
 * column of the k): as one of those, where its digit points must take k; holding 1, 5 or 9, its row's column for
 * that digit must point back at it
 * @param[in] v  the digits (0: empty)
 * @param[in] i  the cell
 * @return a candidate mask
 */
static uint16_t idx159_allow(const uint8_t *v, int i)
{
    const int r = i / 9, q = i % 9;
    uint16_t free = 0x3FE;
    if (q % 4 == 0) {                               /* column 1, 5 or 9: says where k is */
        const int k = q + 1;
        int at = -1;
        for (int j = 0; j < 9; j++) {
            at = v[r * 9 + j] == k && r * 9 + j != i ? j : at;
        }
        uint16_t ok = 0;
        for (int c = 1; c <= 9; c++) {
            const int t = r * 9 + c - 1;
            if (at >= 0 ? c - 1 == at : t == i ? c == k : (!v[t] || v[t] == k)) {
                ok |= (uint16_t)(1u << c);
            }
        }
        free &= ok;
    }
    for (int k = 1; k <= 9; k += 4) {               /* holding k: column k must say this column */
        const int idx = r * 9 + k - 1;
        if (idx != i && v[idx] && v[idx] != q + 1) {
            free &= (uint16_t)~(1u << k);
        }
    }
    return free;
}

/**
 * @brief Dutch Flatmates: may a cell still hold a digit (it holds it, or is empty with the digit not yet in
 *        its units)?
 * @param[in] g  the search's board
 * @param[in] c  the cell
 * @param[in] d  the digit
 * @return true if it may
 */
static inline bool is_cell_open(const grid_t *g, int c, int d)
{
    if (g->v[c]) {
        return g->v[c] == d;
    }
    for (int k = 0; k < 5 && g->units[c][k] >= 0; k++) {
        if (g->um[g->units[c][k]] >> d & 1) {
            return false;
        }
    }
    return (g->G->allow[c] >> d & 1) != 0;
}

/**
 * @brief The digits a cell may still take, by every rule of the board.
 * @param[in] g  the search's board
 * @param[in] i  the cell
 * @return them (bit d = d)
 */
static uint16_t grid_free(const grid_t *g, int i)
{
    uint16_t used = 0;
    for (int k = 0; k < 5 && g->units[i][k] >= 0; k++) {
        used |= g->um[g->units[i][k]];
    }
    const sudoku_geom_t *G = g->G;
    uint16_t free = (uint16_t)(G->allow[i] & ~used);
    if (s_why && !s_why->code && !(free >> s_why->d & 1)) {
        s_why->code = (used >> s_why->d & 1) ? FIT_UNIT : FIT_ALLOW;
    }
    const uint32_t rules = g->rules;
    if (!rules) {
        return free;                                /* (Classic, X-Sudoku, Hyper, Jigsaw, ...: the units are all) */
    }
    for (int k = 0; (rules & R_XPEER) && k < G->nxpeers[i]; k++) {   /* Anti-Knight, Anti-King */
        int v = g->v[G->xpeers[i][k]];
        if (v) {
            free &= (uint16_t)~(1u << v);
        }
    }
    WHY(FIT_XPEER);
    if ((rules & R_CONSEC) && free) {               /* Consecutive: a bar = differ by 1, none = don't */
        const int nb[4] = {i % 9 > 0 ? i - 1 : -1, i % 9 < 8 ? i + 1 : -1, i > 8 ? i - 9 : -1, i < 72 ? i + 9 : -1};
        const bool bar[4] = {nb[0] >= 0 && (G->consec[i - 1] & CONSEC_RIGHT), (G->consec[i] & CONSEC_RIGHT) != 0,
                             nb[2] >= 0 && (G->consec[i - 9] & CONSEC_DOWN), (G->consec[i] & CONSEC_DOWN) != 0
                            };
        for (int k = 0; k < 4; k++) {
            if (nb[k] < 0 || !g->v[nb[k]]) {
                continue;
            }
            int w = g->v[nb[k]];
            uint16_t near = (uint16_t)(((1u << (w + 1)) | (1u << (w - 1))) & 0x3FE);
            free &= bar[k] ? near : (uint16_t)~near;
        }
    }
    WHY(FIT_CONSEC);
    if ((rules & R_EDGE) && free) {
        free &= gen_edge_allow(G, i, g->v, NULL);    /* Kropki, XV, Greater-Than */
    }
    WHY(FIT_EDGE);
    for (int s = 0; s < 4 && free && (rules & R_QUAD) && G->quad_of[i][s] != QUAD_NONE; s++) {
        int empty, miss = sudoku_quad_missing(G, G->quad_of[i][s], g->v, &empty);   /* (i is one of the empty) */
        if (miss > empty) {
            free = 0;    /* Quadruple: not enough room left for its digits */
        } else if (miss == empty) {                 /* every empty cell must take one of them */
            int need[10] = {0}, q = G->quad_of[i][s], at = G->quad_at[q];
            for (int k = 0; k < G->quad_n[q]; k++) {
                need[G->quad_dig[q][k]]++;
            }
            const int cells[4] = {at, at + 1, at + 9, at + 10};
            for (int k = 0; k < 4; k++) {
                if (g->v[cells[k]] && need[g->v[cells[k]]]) {
                    need[g->v[cells[k]]]--;
                }
            }
            uint16_t m = 0;
            for (int d = 1; d <= 9; d++) {
                if (need[d]) {
                    m |= (uint16_t)(1u << d);
                }
            }
            free &= m;
        }
    }
    WHY(FIT_QUAD);
    for (int line = 0; (rules & R_SKY) && free && line < 2; line++) {   /* Skyscraper: its row's, its column's clue */
        int lu = line ? 9 + i % 9 : i / 9, c = G->sky[lu], cells[9];
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        uint8_t x[9];
        for (int k = 0; k < 9; k++) {
            x[k] = g->v[cells[k]];
        }
        int at = line ? i / 9 : i % 9;
        for (int d = 1; d <= 9; d++) {
            if (!(free >> d & 1)) {
                continue;
            }
            x[at] = (uint8_t)d;
            if (!is_gen_sky_line_ok(x, c)) {
                free &= (uint16_t)~(1u << d);
            }
        }
        WHY(line ? FIT_SKY_COL : FIT_SKY_ROW);
    }
    for (int line = 0; (rules & R_XSUM) && free && line < 2; line++) {   /* X-Sums: its row's, its column's clue */
        int lu = line ? 9 + i % 9 : i / 9;
        if (G->xsum[lu]) {
            free &= gen_xsum_allow(G, lu, line ? i / 9 : i % 9, g->v, NULL);
        }
        WHY(line ? FIT_XSUM_COL : FIT_XSUM_ROW);
    }
    for (int line = 0; (rules & R_NROOM) && free && line < 2; line++) {   /* Numbered Rooms: its row's, its column's clue */
        int lu = line ? 9 + i % 9 : i / 9;
        if (G->nroom[lu]) {
            free &= gen_nroom_allow(G, lu, line ? i / 9 : i % 9, g->v, NULL);
        }
        WHY(line ? FIT_NROOM_COL : FIT_NROOM_ROW);
    }
    for (int line = 0; (rules & R_SKYSUM) && free && line < 2; line++) {   /* Sum Skyscrapers: its row's, column's clue */
        int lu = line ? 9 + i % 9 : i / 9, c = G->sky_sum[lu], cells[9];
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        uint8_t x[9];
        for (int k = 0; k < 9; k++) {
            x[k] = g->v[cells[k]];
        }
        int at = line ? i / 9 : i % 9;
        for (int d = 1; d <= 9; d++) {
            if (!(free >> d & 1)) {
                continue;
            }
            x[at] = (uint8_t)d;
            if (!is_gen_skysum_line_ok(x, c)) {
                free &= (uint16_t)~(1u << d);
            }
        }
        WHY(line ? FIT_SKYSUM_COL : FIT_SKYSUM_ROW);
    }
    for (int line = 0; (rules & R_PSKY) && free && line < 2; line++) {   /* Odd/Even Skyscrapers: its row's, column's clue */
        int lu = line ? 9 + i % 9 : i / 9, c = G->psky_clue[lu], cells[9];
        if (!c) {
            continue;
        }
        line_cells(lu, cells);
        uint8_t x[9];
        for (int k = 0; k < 9; k++) {
            x[k] = g->v[cells[k]];
        }
        int at = line ? i / 9 : i % 9;
        for (int d = 1; d <= 9; d++) {
            if (!(free >> d & 1)) {
                continue;
            }
            x[at] = (uint8_t)d;
            if (!is_gen_psky_line_ok(x, c)) {
                free &= (uint16_t)~(1u << d);
            }
        }
        WHY(line ? FIT_PSKY_COL : FIT_PSKY_ROW);
    }
    if ((rules & R_MAGIC) && free && i / 9 / 3 == 1 && i % 9 / 3 == 1) {   /* Magic Square: its lines make 15 */
        free &= gen_magic_allow(g->v, NULL, i);
    }
    WHY(FIT_MAGIC);
    if ((rules & R_ANTIDIAG) && free) {             /* Anti-Diagonal: three digits on its diagonal already: one of them */
        const int r = i / 9, q = i % 9;
        for (int dg = 0; dg < 2; dg++) {
            if (dg ? r + q != 8 : r != q) {
                continue;
            }
            uint16_t have = 0;
            for (int k = 0; k < 9; k++) {
                const int o = dg ? k * 9 + 8 - k : k * 10;
                have |= o != i && g->v[o] ? (uint16_t)(1u << g->v[o]) : 0;
            }
            if (gen_popcount(have) >= 3) {
                free &= have;
            }
        }
    }
    WHY(FIT_ANTIDIAG);
    if ((rules & R_FLATMATES) && free) {            /* Dutch Flatmates: every 5 has a 1 above it or a 9 below */
        const int r = i / 9;
        if ((free >> 5 & 1) && !((r > 0 && is_cell_open(g, i - 9, 1)) || (r < 8 && is_cell_open(g, i + 9, 9)))) {
            free &= (uint16_t)~(1u << 5);           /* a 5 here could have neither */
        }
        if (r < 8 && g->v[i + 9] == 5 && !(r < 7 && is_cell_open(g, i + 18, 9))) {
            free &= (uint16_t)(1u << 1);            /* above a 5 whose 9 can't come: its 1 */
        }
        if (r > 0 && g->v[i - 9] == 5 && !(r > 1 && is_cell_open(g, i - 18, 1))) {
            free &= (uint16_t)(1u << 9);            /* below a 5 whose 1 can't come: its 9 */
        }
    }
    WHY(FIT_FLATMATES);
    for (int s = 0; s < 4 && free && (rules & R_MATHRAX) && G->mx_of[i][s] != MX_NONE; s++) {
        const int q = G->mx_of[i][s], at = G->mx_at[q];   /* Mathrax: what its diagonal partner leaves */
        const int across = i == at ? at + 10 : i == at + 10 ? at : i == at + 1 ? at + 9 : at + 1;
        const int w = g->v[across];
        uint16_t m = 0;
        for (int d = 1; d <= 9; d++) {
            if ((free >> d & 1) && (w ? (G->mx_mask[q][d] >> w & 1) : G->mx_mask[q][d] != 0)) {
                m |= (uint16_t)(1u << d);
            }
        }
        free &= m;
    }
    WHY(FIT_MATHRAX);
    for (int line = 0; (rules & R_FIRST3) && free && line < 2; line++) {
        int lu = line ? 9 + i % 9 : i / 9, at = line ? i / 9 : i % 9;   /* its row's, its column's clue */
        if (at < 3 && is_gen_first3_clued(G, lu)) {
            free &= gen_first3_allow(G, lu, at, g->v, NULL);
        }
        WHY(G->frame ? (line ? FIT_FRAME_COL : FIT_FRAME_ROW) : G->outside ? (line ? FIT_OUTSIDE_COL : FIT_OUTSIDE_ROW)
            : (line ? FIT_ROSSINI_COL : FIT_ROSSINI_ROW));
    }
    if ((rules & R_159) && free) {                  /* 159 Sudoku: its row's columns 1, 5, 9 */
        free &= idx159_allow(g->v, i);
    }
    WHY(FIT_159);
    for (int k = 0; (rules & R_DIAGNC) && free && k < 4; k++) {   /* Diagonal Non-Consecutive: the corner cells */
        int r = i / 9 + (k < 2 ? -1 : 1), c = i % 9 + (k % 2 ? 1 : -1);
        int w = r >= 0 && r < 9 && c >= 0 && c < 9 ? g->v[r * 9 + c] : 0;
        if (w) {
            const uint16_t near = (uint16_t)((1u << (w + 1)) | (1u << (w - 1)));
            free &= is_sudoku_dmarked(G, i, k) ? near : (uint16_t)~near;   /* (Diagonal Consecutive: marked, 1 apart) */
        }
    }
    WHY(FIT_DIAGNC);
    for (int slot = 0; (rules & R_LK) && free && slot < 2; slot++) {   /* Little Killer: what its diagonal's sum leaves */
        int k = G->lk_of[i][slot];
        if (k == LK_NONE) {
            continue;
        }
        int sum = 0, empty = 0;
        for (int j = 0; j < G->lk_n[k]; j++) {
            int o = G->lk_cell[k][j];
            if (o == i) {
                continue;
            }
            if (g->v[o]) {
                sum += g->v[o];
            } else {
                empty++;
            }
        }
        int lo = G->lk_sum[k] - sum - 9 * empty, hi = G->lk_sum[k] - sum - empty;
        for (int d = 1; d <= 9; d++) {
            if (d < lo || d > hi) {
                free &= (uint16_t)~(1u << d);
            }
        }
    }
    WHY(FIT_LK);
    if ((rules & R_SANDWICH) && free) {             /* Sandwich: a digit that can't keep a line's sum goes */
        for (int line = 0; line < 2 && free; line++) {
            int lu = line ? 9 + i % 9 : i / 9, ki = line ? i / 9 : i % 9, vals[9], p1 = -1, p9 = -1;
            for (int k = 0; k < 9; k++) {
                int c = line ? k * 9 + i % 9 : (i / 9) * 9 + k;
                vals[k] = c == i ? 0 : g->v[c];
                if (vals[k] == 1) {
                    p1 = k;
                }
                if (vals[k] == 9) {
                    p9 = k;
                }
            }
            if (p1 < 0 && p9 < 0) {
                continue;    /* neither end placed: no 1 / 9 here can be judged yet */
            }
            /* with both ends already placed: what's between them so far */
            int bsum = 0, bempty = 0, ba = -1, bb = -1;
            if (p1 >= 0 && p9 >= 0) {
                ba = p1 < p9 ? p1 : p9;
                bb = p1 < p9 ? p9 : p1;
                for (int k = ba + 1; k < bb; k++) {
                    if (vals[k]) {
                        bsum += vals[k];
                    } else {
                        bempty++;
                    }
                }
            }
            for (int d = 1; d <= 9; d++) {
                if (!(free >> d & 1)) {
                    continue;
                }
                int sum, empty;
                if (d == 1 || d == 9) {                /* this cell becomes an end */
                    int q1 = d == 1 ? ki : p1, q9 = d == 9 ? ki : p9;
                    if (q1 < 0 || q9 < 0) {
                        continue;
                    }
                    int a = q1 < q9 ? q1 : q9, b = q1 < q9 ? q9 : q1;
                    sum = empty = 0;
                    for (int k = a + 1; k < b; k++) {
                        if (vals[k]) {
                            sum += vals[k];
                        } else {
                            empty++;
                        }
                    }
                } else {
                    if (ba < 0) {
                        continue;    /* an end still missing */
                    }
                    sum = bsum;
                    empty = bempty;
                    if (ki > ba && ki < bb) {          /* this cell is between them */
                        sum += d;
                        empty--;
                    }
                }
                int left = G->sandwich_sum[lu] - sum;
                if (left < 0 || !(s_bread[empty] >> left & 1)) {
                    free &= (uint16_t)~(1u << d);
                }
            }
            WHY(line ? FIT_SANDWICH_COL : FIT_SANDWICH_ROW);
        }
    }
    int t = (rules & R_LINE) ? G->thermo_of[i] : THERMO_NONE;
    if (t != THERMO_NONE && free) {
        free &= gen_line_allow(G, t, G->thermo_pos[i], g->v, NULL);    /* the lines */
    }
    WHY(FIT_THERMO);
    int c = (rules & R_CAGE) ? G->cage_of[i] : CAGE_NONE;
    if (c != CAGE_NONE && free) {                    /* Killer: what the cage's sum still allows */
        free &= (uint16_t)~g->cage_used[c];
        if (!G->cage_sum[c]) {                      /* (Sumless Killer, Extra Regions: no repeat, and no sum) */
            if (G->cage_prod[c] && free) {
                free &= gen_product_allow(G, g->v, c, i);   /* (Product Killer: what the product still allows) */
            }
            WHY(FIT_CAGE);
            return free;
        }
        int left = g->cage_left[c], others = g->cage_empty[c] - 1;
        uint16_t ok = 0;
        for (int d = 1; d <= 9; d++) {
            if (!(free >> d & 1) || d > left) {
                continue;
            }
            if (others == 0) {
                if (d == left) {
                    ok |= (uint16_t)(1u << d);
                }
                continue;
            }
            int lo, hi;
            mask_k_range((uint16_t)(g->G->all & ~g->cage_used[c] & ~(1u << d)), others, &lo, &hi);
            if (left - d >= lo && left - d <= hi) {
                ok |= (uint16_t)(1u << d);
            }
        }
        free = ok;
    }
    WHY(FIT_CAGE);
    return free;
}

/**
 * @brief Loads a puzzle into the search's board.
 * @param[out] g  gets it
 * @param[in] G  the board
 * @param[in] cells  the digits, 0 = empty
 * @return false if a given breaks a rule
 */
static bool grid_load(grid_t *g, const sudoku_geom_t *G, const uint8_t *cells)
{
    memset(g, 0, sizeof(*g));
    g->G = G;
    memset(g->units, -1, sizeof(g->units));
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], us[5], n = cell_units(G, i, us);
        for (int j = 0; j < n; j++) {
            g->units[i][j] = (int8_t)us[j];
        }
    }
    bool xpeer = false;
    for (int k = 0; k < G->ncells && !xpeer; k++) {
        xpeer = G->nxpeers[G->cells[k]] != 0;
    }
    g->rules = (xpeer ? R_XPEER : 0) | (G->consec_on ? R_CONSEC : 0) | (G->edge_kind ? R_EDGE : 0) |
               (G->nquads ? R_QUAD : 0) | (G->skyscraper ? R_SKY : 0) | (G->xsums ? R_XSUM : 0) |
               (G->frame || G->outside || G->rossini ? R_FIRST3 : 0) | (G->diag_nc ? R_DIAGNC : 0) |
               (G->nlk ? R_LK : 0) | (G->sandwich ? R_SANDWICH : 0) | (G->nthermos ? R_LINE : 0) |
               (G->ncages ? R_CAGE : 0) | (G->nrooms ? R_NROOM : 0) | (G->skysum ? R_SKYSUM : 0) |
               (G->idx159 ? R_159 : 0) | (G->antidiag ? R_ANTIDIAG : 0) | (G->psky ? R_PSKY : 0) |
               (G->magic ? R_MAGIC : 0) | (G->flatmates ? R_FLATMATES : 0) | (G->nmx ? R_MATHRAX : 0);
    for (int c = 0; c < G->ncages; c++) {
        g->cage_left[c] = (int8_t)G->cage_sum[c];
        g->cage_empty[c] = (int8_t)G->cage_n[c];
    }
    if (!cells) {
        return true;
    }
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k], d = cells[i];
        if (d < 1 || d > G->n) {
            continue;
        }
        if (!(grid_free(g, i) & (1u << d))) {
            return false;
        }
        grid_set(g, i, d);
    }
    return true;
}

/**
 * @brief counts solutions up to limit; with rng, fills g with a random solution
 * @param[in,out] g  the search's board
 * @param[in] limit  stop counting there
 * @param[in,out] rng  a random state, or NULL
 * @return the solutions found, at most limit
 */
static int search(grid_t *g, int limit, uint32_t *rng)
{
    const sudoku_geom_t *G = g->G;
    if (!(++s_yield_nodes & 4095)) {
        sudoku_gen_yield();
    }
    if (g->node_limit && ++g->nodes > g->node_limit) {   /* a Jigsaw layout that's hard to fill: give up */
        g->aborted = true;
        return limit;
    }
    int best = -1, best_n = 10;
    uint16_t best_free = 0;
    for (int k = 0; k < G->ncells; k++) {
        int i = G->cells[k];
        if (g->v[i]) {
            continue;
        }
        uint16_t free = grid_free(g, i);
        int n = gen_popcount(free);
        if (n < best_n) {
            best = i;
            best_n = n;
            best_free = free;
            if (n <= 1) {
                break;
            }
        }
    }
    if (best < 0) {
        return 1;
    }
    if (best_n == 0) {
        return 0;
    }
    int count = 0;
    uint8_t order[9];
    int no = 0;
    for (int d = 1; d <= G->n; d++) {
        if (best_free & (1u << d)) {
            order[no++] = (uint8_t)d;
        }
    }
    if (rng) {
        for (int i = no - 1; i > 0; i--) {
            int j = (int)(rnd(rng) % (uint32_t)(i + 1));
            uint8_t t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
    }
    for (int k = 0; k < no; k++) {
        int d = order[k];
        grid_set(g, best, d);
        count += search(g, limit - count, rng);
        if (count >= limit) {
            return count;    /* keeps g filled (the solution) */
        }
        grid_clear(g, best, d);
    }
    return count;
}

/* (documented in gen_internal.h) */
int gen_count_bounded(const sudoku_geom_t *G, const uint8_t cells[81], int limit, uint32_t node_limit)
{
    static grid_t g;                                 /* off the caller's stack (one caller at a time) */
    if (!grid_load(&g, G, cells)) {
        return 0;
    }
    g.node_limit = node_limit;
    int n = search(&g, limit, NULL);
    return g.aborted ? -1 : n;
}

/* (documented in generator.h) */
int sudoku_count_solutions(const sudoku_geom_t *G, const uint8_t cells[81], int limit)
{
    return gen_count_bounded(G, cells, limit, 0);
}

/* (documented in generator.h) */
bool is_sudoku_classic_unique(const uint8_t cells[81])
{
    static grid_t g;                                 /* its own: another task than the generator's */
    if (!grid_load(&g, sudoku_geom(VAR_CLASSIC), cells)) {
        return false;
    }
    g.node_limit = 200000;                           /* (a broken puzzle can't hold the task for long) */
    return search(&g, 2, NULL) == 1 && !g.aborted;
}

/* (documented in generator.h) */
bool is_sudoku_unique(const sudoku_geom_t *G, const uint8_t cells[81])
{
    static grid_t g;                                 /* its own (the import's task) */
    if (!grid_load(&g, G, cells)) {
        return false;
    }
    g.node_limit = 400000;
    return search(&g, 2, NULL) == 1 && !g.aborted;
}

/* (documented in generator.h) */
bool sudoku_solve(const sudoku_geom_t *G, const uint8_t cells[81], uint8_t out[81])
{
    static grid_t g;                                 /* off the (small) LVGL task stack */
    if (!grid_load(&g, G, cells)) {
        return false;
    }
    if (search(&g, 1, NULL) < 1) {
        return false;
    }
    memcpy(out, g.v, 81);
    return true;
}

/* (documented in generator.h) */
int sudoku_fits_why(const sudoku_geom_t *G, const uint8_t vals[81], int cell, int d)
{
    static grid_t g;
    if (cell < 0 || cell > 80 || d < 1 || d > G->n) {
        return FIT_UNIT;
    }
    if (vals[cell]) {
        return FIT_FILLED;
    }
    if (!grid_load(&g, G, vals)) {
        return FIT_BROKEN;
    }
    why_t w = {d, FIT_OK};
    s_why = &w;
    grid_free(&g, cell);
    s_why = NULL;
    return w.code;
}

/* (documented in generator.h) */
bool is_sudoku_fits(const sudoku_geom_t *G, const uint8_t vals[81], int cell, int d)
{
    static grid_t g;
    if (cell < 0 || cell > 80 || vals[cell] || d < 1 || d > G->n || !grid_load(&g, G, vals)) {
        return false;
    }
    return (grid_free(&g, cell) >> d & 1) != 0;
}

/* (documented in gen_internal.h) */
bool gen_random_full_grid(const sudoku_geom_t *G, uint8_t out[81], uint32_t *rng, uint32_t node_limit)
{
    static grid_t g;
    grid_load(&g, G, NULL);
    g.node_limit = node_limit;
    int n = search(&g, 1, rng);
    memcpy(out, g.v, 81);
    return n >= 1 && !g.aborted;
}

/* (documented in gen_internal.h) */
uint64_t gen_bread_sums(int k)
{
    return s_bread[k];
}

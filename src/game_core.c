/**
 * @file game_core.c
 * @ingroup game_core
 * @brief The game's rules check and undo history, without LVGL (the game screen: ui_game.c).
 */
#include "game_core.h"
#include <stdlib.h>
#include <string.h>

/* (documented in game_core.h) */
int game_conflicts(const sudoku_geom_t *geo, const uint8_t *val, bool *out)
{
    int n = 0;
    memset(out, 0, 81 * sizeof(out[0]));
    for (int i = 0; i < 81; i++) {
        if (!val[i]) {
            continue;
        }
        for (int j = i + 1; j < 81; j++) {
            if (val[j] == val[i] && is_sudoku_geom_peer(geo, i, j)) {
                out[i] = out[j] = true;
            }
        }
    }
    for (int i = 0; i < 81; i++) {                  /* Even-Odd: a digit of the wrong kind */
        /* (only the marked cells: Greater-Than narrows allow[] too, from the chains of signs, for the
         * solver; a digit out of that range breaks no sign by itself until its neighbours are in) */
        if (val[i] && geo->parity[i] && !(geo->allow[i] >> val[i] & 1)) {
            out[i] = true;
        }
    }
    for (int i = 0; geo->consec_on && i < 81; i++) {   /* Consecutive: a bar and not 1 apart, or 1 apart and no bar */
        for (int k = 0; k < 2; k++) {
            int o = k ? i + 9 : i + 1, bit = k ? CONSEC_DOWN : CONSEC_RIGHT;
            if ((k == 0 && i % 9 == 8) || o > 80 || !val[i] || !val[o]) {
                continue;
            }
            bool near = abs(val[i] - val[o]) == 1;
            if (near != ((geo->consec[i] & bit) != 0)) {
                out[i] = out[o] = true;
            }
        }
    }
    for (int lu = 0; geo->sandwich && lu < 18; lu++) {   /* Sandwich: between the 1 and the 9, over the sum or short */
        int cells[9], p1 = -1, p9 = -1;
        for (int k = 0; k < 9; k++) {
            cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9);
            if (val[cells[k]] == 1) {
                p1 = k;
            }
            if (val[cells[k]] == 9) {
                p9 = k;
            }
        }
        if (p1 < 0 || p9 < 0) {
            continue;
        }
        int a = p1 < p9 ? p1 : p9, b = p1 < p9 ? p9 : p1, sum = 0, empty = 0;
        for (int k = a + 1; k < b; k++) {
            sum += val[cells[k]];
            empty += !val[cells[k]];
        }
        if (sum > geo->sandwich_sum[lu] || (!empty && sum != geo->sandwich_sum[lu])) {
            for (int k = a; k <= b; k++) {
                if (val[cells[k]]) {
                    out[cells[k]] = true;
                }
            }
        }
    }
    for (int lu = 0; geo->skyscraper && lu < 18; lu++) {   /* Skyscraper: more seen than the clue, or (full) not it */
        int c = geo->sky[lu], seen = 0, top = 0, k = 0, cells[9];
        if (!c) {
            continue;
        }
        for (int j = 0; j < 9; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
        }
        for (; k < 9 && val[cells[k]]; k++) {
            if (val[cells[k]] > top) {
                top = val[cells[k]];
                seen++;
            }
        }
        if (seen > c || ((k == 9 || top == 9) && seen != c)) {
            for (int j = 0; j < k; j++) {
                out[cells[j]] = true;
            }
        }
    }
    for (int lu = 0; (geo->outside || geo->rossini) && lu < 18; lu++) {   /* Outside, Rossini: the first three */
        int cells[3], x[3], empty = 0;
        for (int j = 0; j < 3; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
            x[j] = val[cells[j]];
            empty += !x[j];
        }
        bool bad = false;
        if (geo->outside) {                       /* more of the clue's digits missing than empty cells left */
            uint16_t have = (uint16_t)((x[0] ? 1u << x[0] : 0) | (x[1] ? 1u << x[1] : 0) | (x[2] ? 1u << x[2] : 0));
            bad = __builtin_popcount(geo->outside_set[lu] & ~have) > empty;
        } else {                                    /* two digits out of the arrow's order, or (full) a run with none */
            int dir = geo->rossini_dir[lu];
            for (int a = 0; a < 3; a++) {
                for (int b = a + 1; b < 3; b++) {
                    if (x[a] && x[b] && ((dir == ROSSINI_UP && x[a] >= x[b]) || (dir == ROSSINI_DOWN && x[a] <= x[b]))) {
                        bad = true;
                    }
                }
            }
            if (dir == ROSSINI_NONE && !empty) {
                bad = (x[0] < x[1] && x[1] < x[2]) || (x[0] > x[1] && x[1] > x[2]);
            }
        }
        for (int j = 0; bad && j < 3; j++) {
            if (x[j]) {
                out[cells[j]] = true;
            }
        }
    }
    for (int c = 0; geo->diag_nc && c < 72; c++) {   /* Diagonal (Non-)Consecutive: corners 1 apart, or (marked) not */
        for (int k = 0; k < 2; k++) {
            int o = k ? c + 8 : c + 10;
            if ((k ? c % 9 == 0 : c % 9 == 8) || !val[c] || !val[o]) {
                continue;
            }
            if ((abs(val[c] - val[o]) == 1) != is_sudoku_dmarked(geo, c, k ? 2 : 3)) {   /* (marked: must be) */
                out[c] = out[o] = true;
            }
        }
    }
    for (int lu = 0; geo->frame && lu < 18; lu++) {   /* Frame: the first three over the clue, or (full) not it */
        int c = geo->frame_sum[lu], cells[9], sum = 0, empty = 0;
        if (!c) {
            continue;
        }
        for (int j = 0; j < 3; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
            sum += val[cells[j]];
            empty += !val[cells[j]];
        }
        if (sum + empty > c || (!empty && sum != c)) {
            for (int j = 0; j < 3; j++) {
                if (val[cells[j]]) {
                    out[cells[j]] = true;
                }
            }
        }
    }
    for (int lu = 0; geo->nrooms && lu < 18; lu++) {   /* Numbered Rooms: the X-th cell holds another digit */
        int c = geo->nroom[lu], cells[9];
        for (int j = 0; j < 9; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
        }
        int x = val[cells[0]];
        if (!c || !x) {
            continue;
        }
        int at = cells[x - 1];
        if (val[at] && val[at] != c) {
            out[cells[0]] = out[at] = true;
        }
    }
    for (int lu = 0; geo->skysum && lu < 18; lu++) {   /* Sum Skyscrapers: more seen than the clue, or (full) not it */
        int c = geo->sky_sum[lu], sum = 0, top = 0, k = 0, cells[9];
        if (!c) {
            continue;
        }
        for (int j = 0; j < 9; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
        }
        for (; k < 9 && val[cells[k]] && top < 9; k++) {
            if (val[cells[k]] > top) {
                top = val[cells[k]];
                sum += top;
            }
        }
        if (sum > c || (top == 9 && sum != c)) {
            for (int j = 0; j < k; j++) {
                out[cells[j]] = true;
            }
        }
    }
    for (int lu = 0; geo->psky && lu < 18; lu++) {   /* Odd/Even Skyscrapers: more of the parity seen, or (to the 9) not it */
        const int c = geo->psky_clue[lu], want = c & ~PSKY_EVEN, even = (c & PSKY_EVEN) != 0;
        int seen = 0, top = 0, k = 0, cells[9];
        if (!c) {
            continue;
        }
        for (int j = 0; j < 9; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
        }
        for (; k < 9 && val[cells[k]] && top < 9; k++) {
            if (val[cells[k]] > top) {
                top = val[cells[k]];
                seen += (top % 2 == 0) == even;
            }
        }
        if (seen > want || (top == 9 && seen != want)) {
            for (int j = 0; j < k; j++) {
                out[cells[j]] = true;
            }
        }
    }
    for (int l = 0; geo->magic && l < 8; l++) {   /* Magic Square: a full row, column or diagonal of the centre box not 15 */
        static const uint8_t LN[8][3] = {{30, 31, 32}, {39, 40, 41}, {48, 49, 50}, {30, 39, 48}, {31, 40, 49},
            {32, 41, 50}, {30, 40, 50}, {32, 40, 48}
        };
        const int a = val[LN[l][0]], b = val[LN[l][1]], c = val[LN[l][2]];
        if (a && b && c && a + b + c != 15) {
            out[LN[l][0]] = out[LN[l][1]] = out[LN[l][2]] = true;
        }
    }
    for (int dg = 0; geo->antidiag && dg < 2; dg++) {   /* Anti-Diagonal: a fourth different digit on a long diagonal */
        uint16_t have = 0;
        for (int k = 0; k < 9; k++) {
            const int o = dg ? k * 9 + 8 - k : k * 10;
            have |= val[o] ? (uint16_t)(1u << val[o]) : 0;
        }
        for (int k = 0; __builtin_popcount(have) > 3 && k < 9; k++) {
            const int o = dg ? k * 9 + 8 - k : k * 10;
            out[o] = out[o] || val[o];
        }
    }
    for (int lu = 0; geo->xsums && lu < 18; lu++) {   /* X-Sums: the first X digits over the clue, or (full) not it */
        int c = geo->xsum[lu], cells[9], sum = 0, empty = 0;
        for (int j = 0; j < 9; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
        }
        int x = val[cells[0]];
        if (!c || !x) {
            continue;
        }
        for (int j = 0; j < x; j++) {
            sum += val[cells[j]];
            empty += !val[cells[j]];
        }
        if (sum + empty > c || (!empty && sum != c)) {
            for (int j = 0; j < x; j++) {
                if (val[cells[j]]) {
                    out[cells[j]] = true;
                }
            }
        }
    }
    for (int d = 0; d < geo->nlk; d++) {          /* Little Killer: a diagonal over its sum, or (full) not it */
        int sum = 0, empty = 0;
        for (int j = 0; j < geo->lk_n[d]; j++) {
            sum += val[geo->lk_cell[d][j]];
            empty += !val[geo->lk_cell[d][j]];
        }
        if (sum + empty > geo->lk_sum[d] || (!empty && sum != geo->lk_sum[d])) {
            for (int j = 0; j < geo->lk_n[d]; j++) {
                if (val[geo->lk_cell[d][j]]) {
                    out[geo->lk_cell[d][j]] = true;
                }
            }
        }
    }
    for (int t = 0; t < geo->nthermos; t++) {     /* the lines: digits that break theirs */
        int n = geo->thermo_n[t];
        const uint8_t *cell = geo->thermo_cell[t];
        switch (geo->line_kind) {
        case LINE_THERMO:                           /* two neighbours along it that don't rise */
        case LINE_SLOW:                             /* ... that fall (a repeat in a row, column or box shows anyway) */
        case LINE_WHISPER:                          /* two neighbours less than 5 apart */
        case LINE_DUTCH:                            /* ... less than 4 apart */
        case LINE_PARITY:                           /* two odd or two even neighbours */
            for (int i = 1; i < n; i++) {
                int a = cell[i - 1], b = cell[i];
                if (!val[a] || !val[b]) {
                    continue;
                }
                const int x = val[a], y = val[b], k = geo->line_kind;
                bool bad = k == LINE_THERMO ? y <= x : k == LINE_SLOW ? y < x : k == LINE_PARITY ? !((x ^ y) & 1)
                           : abs(x - y) < (k == LINE_DUTCH ? 4 : 5);
                if (bad) {
                    out[a] = out[b] = true;
                }
            }
            break;
        case LINE_NABNER:                           /* two digits on it the same or 1 apart */
            for (int i = 0; i < n; i++) {
                for (int j = i + 1; j < n; j++) {
                    int a = val[cell[i]], b = val[cell[j]];
                    if (a && b && abs(a - b) < 2) {
                        out[cell[i]] = out[cell[j]] = true;
                    }
                }
            }
            break;
        case LINE_RENBAN: {                         /* a repeat, or digits too far apart for the line */
            int lo = 10, hi = 0;
            unsigned seen = 0, twice = 0;
            for (int i = 0; i < n; i++) {
                int x = val[cell[i]];
                if (!x) {
                    continue;
                }
                if (seen >> x & 1) {
                    twice |= 1u << x;
                }
                seen |= 1u << x;
                if (x < lo) {
                    lo = x;
                }
                if (x > hi) {
                    hi = x;
                }
            }
            for (int i = 0; i < n; i++) {
                int x = val[cell[i]];
                if (x && (hi - lo > n - 1 || (twice >> x & 1))) {
                    out[cell[i]] = true;
                }
            }
            break;
        }
        case LINE_PALINDROME:                       /* two cells mirroring each other that differ */
        case LINE_WPAL:                             /* (Weak: one odd, one even) */
            for (int i = 0; i < n / 2; i++) {
                int a = cell[i], b = cell[n - 1 - i];
                if (val[a] && val[b] && (geo->line_kind == LINE_WPAL ? ((val[a] ^ val[b]) & 1) : val[a] != val[b])) {
                    out[a] = out[b] = true;
                }
            }
            break;
        case LINE_ZIPPER: {                         /* two full pairs (or a pair and the middle) with different totals */
            int target = n % 2 ? val[cell[n / 2]] : 0;   /* (odd: the middle digit, once it's in) */
            bool bad = false;
            for (int i = 0; i < n / 2; i++) {
                int a = val[cell[i]], b = val[cell[n - 1 - i]];
                if (a && b) {
                    bad |= target && a + b != target;
                    target = a + b;
                } else if (n % 2 && val[cell[n / 2]] && (a ? a : b) >= val[cell[n / 2]]) {
                    bad = true;                     /* (one of the pair already at the middle digit or past it) */
                }
            }
            for (int i = 0; bad && i < n; i++) {
                if (val[cell[i]]) {
                    out[cell[i]] = true;
                }
            }
            break;
        }
        case LINE_ENTROPIC:
        case LINE_MODULAR:                          /* two cells 3 apart in different groups, or closer in one */
            for (int i = 0; i < n; i++) {
                for (int j = i + 1; j < n; j++) {
                    int a = val[cell[i]], b = val[cell[j]];
                    if (!a || !b) {
                        continue;
                    }
                    bool same = sudoku_line_group(geo->line_kind, a) == sudoku_line_group(geo->line_kind, b);
                    if (same != ((j - i) % 3 == 0)) {
                        out[cell[i]] = out[cell[j]] = true;
                    }
                }
            }
            break;
        case LINE_BETWEEN: {                        /* a digit between not strictly between the two circles */
            int a = val[cell[0]], b = val[cell[n - 1]], lo = a < b ? a : b, hi = a < b ? b : a;
            for (int i = 1; a && b && i < n - 1; i++) {
                int x = val[cell[i]];
                if (x && (x <= lo || x >= hi)) {
                    out[cell[i]] = out[cell[0]] = out[cell[n - 1]] = true;
                }
            }
            break;
        }
        case LINE_LOCKOUT: {                        /* diamonds under 4 apart, or a digit between them inside their range */
            int a = val[cell[0]], b = val[cell[n - 1]], lo = a < b ? a : b, hi = a < b ? b : a;
            if (a && b && hi - lo < 4) {
                out[cell[0]] = out[cell[n - 1]] = true;
            }
            for (int i = 1; i < n - 1; i++) {
                int x = val[cell[i]];
                if (x == 5 || (a && b && x && x >= lo && x <= hi)) {   /* (5: inside any two 4+ apart) */
                    out[cell[i]] = true;
                    if (a && b && x != 5) {
                        out[cell[0]] = out[cell[n - 1]] = true;
                    }
                }
            }
            break;
        }
        case LINE_TEN: {                            /* no way left to split it into runs of 10 */
            uint16_t m[9];
            for (int i = 0; i < n; i++) {
                m[i] = val[cell[i]] ? (uint16_t)(1u << val[cell[i]]) : 0x3FE;
            }
            for (int i = 0; !sudoku_ten_allow(m, n, -1) && i < n; i++) {
                out[cell[i]] = val[cell[i]] != 0;
            }
            break;
        }
        case LINE_SEQ: {                            /* two filled digits no single step (not 0) along it can join */
            int first = -1;
            for (int i = 0; i < n; i++) {
                const int x = val[cell[i]];
                if (!x) {
                    continue;
                }
                if (first < 0) {
                    first = i;
                    continue;
                }
                const int dx = x - val[cell[first]], dk = i - first;
                const int s = dx / dk, a = val[cell[first]] - first * s;   /* (the step and start they'd make) */
                const bool bad = !dx || dx % dk || a < 1 || a > 9 || a + (n - 1) * s < 1 || a + (n - 1) * s > 9;
                bool off = bad;
                for (int j = 0; j < n && !off; j++) {   /* every filled digit on that sequence */
                    off = val[cell[j]] && val[cell[j]] != a + j * s;
                }
                if (off) {
                    for (int j = 0; j < n; j++) {
                        out[cell[j]] = out[cell[j]] || val[cell[j]];
                    }
                    break;
                }
            }
            break;
        }
        case LINE_AVG: {                            /* the others can't (or, all filled, don't) average the circle */
            int sum = 0, empty = 0, circle = val[cell[0]];
            for (int i = 1; i < n; i++) {
                sum += val[cell[i]];
                empty += !val[cell[i]];
            }
            const int want = circle * (n - 1);
            if (circle && (sum + empty > want || sum + 9 * empty < want)) {
                for (int i = 0; i < n; i++) {
                    out[cell[i]] = out[cell[i]] || val[cell[i]];
                }
            }
            break;
        }
        case LINE_PILL: {                           /* the shaft over the pill's number, or (all filled) not equal */
            int sum = 0, empty = 0, a = val[cell[0]], b = val[cell[1]];
            for (int i = 2; i < n; i++) {
                sum += val[cell[i]];
                empty += !val[cell[i]];
            }
            if (a && b && (sum + empty > 10 * a + b || (!empty && sum != 10 * a + b))) {
                for (int i = 0; i < n; i++) {
                    out[cell[i]] = out[cell[i]] || val[cell[i]];
                }
            }
            break;
        }
        case LINE_DARROW: {                         /* the cells between over the two circles, or (all filled) not equal */
            int sum = 0, empty = 0, a = val[cell[0]], b = val[cell[n - 1]];
            for (int i = 1; i < n - 1; i++) {
                sum += val[cell[i]];
                empty += !val[cell[i]];
            }
            if (a && b && (sum + empty > a + b || (!empty && sum != a + b))) {
                for (int i = 0; i < n; i++) {
                    if (val[cell[i]]) {
                        out[cell[i]] = true;
                    }
                }
            }
            break;
        }
        case LINE_REGIONSUM: {                      /* two boxes' full parts that differ, or a part over a full one */
            int sum[9], np, target = -1;
            bool full[9], bad = false;
            np = sudoku_regionsum_parts(geo, t, val, sum, full);
            for (int j = 0; j < np; j++) {
                if (full[j]) {
                    bad |= target >= 0 && sum[j] != target;
                    target = sum[j];
                }
            }
            for (int j = 0; j < np && target >= 0; j++) {
                bad |= !full[j] && sum[j] >= target;    /* (an empty cell still adds 1 at least) */
            }
            for (int i = 0; bad && i < n; i++) {
                if (val[cell[i]]) {
                    out[cell[i]] = true;
                }
            }
            break;
        }
        case LINE_ARROW: {                          /* the shaft over its circle, or (all filled) not equal */
            int sum = 0, empty = 0, circle = val[cell[0]];
            for (int i = 1; i < n; i++) {
                sum += val[cell[i]];
                empty += !val[cell[i]];
            }
            if (circle && (sum + empty > circle || (!empty && sum != circle))) {
                for (int i = 0; i < n; i++) {
                    if (val[cell[i]]) {
                        out[cell[i]] = true;
                    }
                }
            }
            break;
        }
        }
    }
    for (int i = 0; geo->edge_kind && i < 81; i++) {   /* Kropki, XV, Greater-Than: two digits the mark (or none) rules out */
        for (int k = 0; k < 2; k++) {
            int o = k ? i + 9 : i + 1, m = k ? EDGE_DOWN(geo->edge[i]) : EDGE_RIGHT(geo->edge[i]);
            if ((k == 0 && i % 9 == 8) || o > 80 || !val[i] || !val[o]) {
                continue;
            }
            if (!is_sudoku_edge_ok(geo->edge_kind, m, val[i], val[o])) {
                out[i] = out[o] = true;
            }
        }
    }
    for (int c = 0; geo->flatmates && c < 81; c++) {   /* Dutch Flatmates: a 5 that can have neither a 1 above nor a 9 below */
        const int r = c / 9, up = r > 0 ? val[c - 9] : -1, down = r < 8 ? val[c + 9] : -1;
        if (val[c] == 5 && up != 0 && up != 1 && down != 0 && down != 9) {
            out[c] = true;
            if (up > 0) {
                out[c - 9] = true;
            }
            if (down > 0) {
                out[c + 9] = true;
            }
        }
    }
    for (int lu = 0; geo->n9 && lu < 18; lu++) {   /* Next to Nine: beside the 9, a digit the clue hasn't, or too few */
        const int want = geo->n9_set[lu] << 1;
        int cells[9], k = 0;
        if (!want) {
            continue;
        }
        for (int j = 0; j < 9; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
        }
        while (k < 9 && val[cells[k]] != 9) {
            k++;
        }
        if (k == 9) {
            continue;
        }
        const int nb = (k > 0) + (k < 8);
        if (nb != __builtin_popcount(want)) {
            out[cells[k]] = true;                   /* (a 9 with the wrong number of cells beside it) */
        }
        for (int j = k - 1; j <= k + 1; j += 2) {
            if (j >= 0 && j < 9 && val[cells[j]] && !(want >> val[cells[j]] & 1)) {
                out[cells[k]] = out[cells[j]] = true;
            }
        }
    }
    for (int c = 0; geo->nclone && c < 81; c++) {   /* Clone Sudoku: a cell and its clone not the same */
        const int p = geo->clone_of[c];
        if (p != CLONE_NONE && val[c] && val[p] && val[c] != val[p]) {
            out[c] = out[p] = true;
        }
    }
    for (int tl = 0; geo->quadro && tl < 72; tl++) {   /* Quadro: a full 2x2 block all odd or all even */
        const int four[4] = {tl, tl + 1, tl + 9, tl + 10};
        int odd = 0, even = 0;
        for (int k = 0; tl % 9 < 8 && k < 4; k++) {
            odd += val[four[k]] && (val[four[k]] & 1);
            even += val[four[k]] && !(val[four[k]] & 1);
        }
        if (odd == 4 || even == 4) {
            for (int k = 0; k < 4; k++) {
                out[four[k]] = true;
            }
        }
    }
    for (int a = 0; geo->aq9 && a < 81; a++) {      /* Anti-Queen 9s: two 9s on one diagonal */
        for (int b = a + 1; val[a] == 9 && b < 81; b++) {
            if (val[b] == 9 && abs(a / 9 - b / 9) == abs(a % 9 - b % 9)) {
                out[a] = out[b] = true;
            }
        }
    }
    for (int a = 0; geo->s9 && a < 81; a++) {      /* Search Nine: an arrow's digit that doesn't reach its 9 */
        const int dir = geo->s9_dir[a], k = val[a];
        if (!dir || !k) {
            continue;
        }
        const int t = k < 9 ? sudoku_s9_cell(a, dir, k) : -1;
        if (t < 0) {
            out[a] = true;                          /* (a 9 in it, or a distance past the edge) */
            continue;
        }
        if (val[t] && val[t] != 9) {
            out[a] = out[t] = true;                 /* that far: no 9 */
        }
        for (int j = 1; j <= 8; j++) {              /* the line's 9 elsewhere, either way along the arrow's line */
            for (int back = 0; back < 2; back++) {
                const int c = sudoku_s9_cell(a, back ? (dir + 1) % 4 + 1 : dir, j);
                if (c >= 0 && c != t && val[c] == 9) {
                    out[a] = out[c] = true;
                }
            }
        }
    }
    for (int c = 0; geo->psym && c <= 40; c++) {   /* Point Symmetry: a mirrored pair not making 10 (the centre: not 5) */
        const int a = val[c], b = val[80 - c];
        if (c == 40 ? a && a != 5 : a && b && a + b != 10) {
            out[c] = out[80 - c] = true;
            out[40] = c == 40 || out[40];
        }
    }
    for (int tl = 0; geo->batten && tl < 72; tl++) {   /* Battenburg: a mark and no checkerboard, or one without a mark */
        const int four[4] = {tl, tl + 1, tl + 9, tl + 10};
        if (tl % 9 == 8) {
            continue;
        }
        for (int a = 0; a < 4; a++) {               /* (marked: two filled cells that already break it) */
            for (int b = a + 1; geo->bat[tl] && b < 4; b++) {
                const int x = val[four[a]], y = val[four[b]];
                if (x && y && ((x & 1) == (y & 1)) != (a + b == 3)) {   /* (diagonal pairs alike, side pairs not) */
                    out[four[a]] = out[four[b]] = true;
                }
            }
        }
        if (!geo->bat[tl] && val[four[0]] && val[four[1]] && val[four[2]] && val[four[3]] &&
                is_sudoku_checker(val[four[0]], val[four[1]], val[four[2]], val[four[3]])) {
            for (int a = 0; a < 4; a++) {
                out[four[a]] = true;
            }
        }
    }
    for (int lu = 0; geo->bf && lu < 18; lu++) {   /* Battlefield: a full line whose overlap (or gap) misses the clue */
        const int s = geo->bf_clue[lu] ? sudoku_bf_of(val, lu) : -1;   /* (-1: a cell it needs still empty) */
        if (s >= 0 && s != geo->bf_clue[lu] - 1) {
            for (int j = 0; j < 9; j++) {
                const int c = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
                out[c] = out[c] || val[c];
            }
        }
    }
    for (int lu = 0; geo->b9 && lu < 18; lu++) {   /* Before Nine: the digits before the 9 over the clue, or (to the 9) not it */
        const int c = geo->b9_sum[lu];
        int cells[9], sum = 0, k = 0;
        if (!c) {
            continue;
        }
        for (int j = 0; j < 9; j++) {
            cells[j] = lu < 9 ? lu * 9 + j : j * 9 + lu - 9;
        }
        for (; k < 9 && val[cells[k]] && val[cells[k]] != 9; k++) {
            sum += val[cells[k]];
        }
        const bool nine = k < 9 && val[cells[k]] == 9;
        if (sum > c || (nine && sum != c)) {
            for (int j = 0; j <= k && j < 9; j++) {
                out[cells[j]] = out[cells[j]] || val[cells[j]];
            }
        }
    }
    for (int q = 0; q < geo->nmx; q++) {          /* Mathrax: a diagonal pair that doesn't make the circle's operation */
        const int at = geo->mx_at[q], cells[4] = {at, at + 1, at + 9, at + 10}, across[4] = {at + 10, at + 9, at + 1, at};
        for (int k = 0; k < 4; k++) {
            const int a = val[cells[k]], b = val[across[k]];
            if (a && (b ? !(geo->mx_mask[q][a] >> b & 1) : !geo->mx_mask[q][a])) {
                out[cells[k]] = true;               /* (a digit no partner suits, or a full pair that's wrong) */
            }
        }
    }
    for (int q = 0; q < geo->nquads; q++) {       /* Quadruple: no room left for the circle's digits */
        int empty, at = geo->quad_at[q];
        if (sudoku_quad_missing(geo, q, val, &empty) > empty) {
            const int cells[4] = {at, at + 1, at + 9, at + 10};
            for (int k = 0; k < 4; k++) {
                if (val[cells[k]]) {
                    out[cells[k]] = true;
                }
            }
        }
    }
    for (int k = 0; k < geo->ncages; k++) {         /* Calcudoku Killer's - and /: a full pair that doesn't make it */
        const int a = geo->cage_cell[k][0], b = geo->cage_cell[k][1];
        if (geo->cage_dq[k] && val[a] && val[b] && !is_calc_pair(geo->cage_op[k], geo->cage_dq[k], val[a], val[b])) {
            out[a] = out[b] = true;
        }
    }
    for (int k = 0; k < geo->ncages; k++) {         /* Product Killer: a digit that doesn't divide, or full and wrong */
        if (!geo->cage_prod[k]) {
            continue;                               /* (Calcudoku Killer: not an x cage) */
        }
        int left = geo->cage_prod[k], filled = 0, n = geo->cage_n[k];
        bool bad = false;
        for (int i = 0; i < n; i++) {
            int d = val[geo->cage_cell[k][i]];
            if (d) {
                filled++;
                bad |= left % d != 0;
                left = left % d ? left : left / d;
            }
        }
        if (bad || (filled == n && left != 1)) {
            for (int i = 0; i < n; i++) {
                if (val[geo->cage_cell[k][i]]) {
                    out[geo->cage_cell[k][i]] = true;
                }
            }
        }
    }
    for (int r = 0; geo->idx159 && r < 9; r++) {   /* 159 Sudoku: a column 1, 5 or 9 pointing at another digit */
        for (int k = 1; k <= 9; k += 4) {
            const int idx = r * 9 + k - 1, c = val[idx];
            for (int j = 0; c && j < 9; j++) {      /* (k where it doesn't point, or another digit where it does) */
                const int x = val[r * 9 + j];
                if (x && ((x == k) != (j == c - 1))) {
                    out[idx] = out[r * 9 + j] = true;
                }
            }
        }
    }
    for (int k = 0; k < geo->ncages; k++) {         /* Killer: a cage over its sum, or full and short of it */
        if (!geo->cage_sum[k]) {
            continue;                               /* (Sumless Killer, Extra Regions; Calcudoku Killer: not a + cage) */
        }
        int sum = 0, filled = 0, n = geo->cage_n[k];
        for (int i = 0; i < n; i++) {
            int c = geo->cage_cell[k][i];
            sum += val[c];
            filled += val[c] != 0;
        }
        if (sum > geo->cage_sum[k] || (filled == n && sum != geo->cage_sum[k])) {
            for (int i = 0; i < n; i++) {
                int c = geo->cage_cell[k][i];
                if (val[c]) {
                    out[c] = true;
                }
            }
        }
    }
    for (int i = 0; i < 81; i++) {
        n += out[i];
    }
    return n;
}

/* (documented in game_core.h) */
void game_history_clear(game_history_t *h)
{
    h->head = h->len = h->redo_len = 0;
}

/* (documented in game_core.h) */
void game_history_push(game_history_t *h, const board_t *b, bool keep_redo)
{
    h->undo[h->head] = *b;
    h->head = (h->head + 1) % UNDO_MAX;
    if (h->len < UNDO_MAX) {
        h->len++;
    }
    if (!keep_redo) {
        h->redo_len = 0;
    }
}

/* (documented in game_core.h) */
bool game_history_undo(game_history_t *h, board_t *b)
{
    if (h->len == 0) {
        return false;
    }
    if (h->redo_len == UNDO_MAX) {                  /* full: the oldest undone board goes */
        memmove(h->redo, h->redo + 1, sizeof(board_t) * (UNDO_MAX - 1));
        h->redo_len--;
    }
    h->redo[h->redo_len++] = *b;                    /* for Redo */
    h->head = (h->head + UNDO_MAX - 1) % UNDO_MAX;
    h->len--;
    *b = h->undo[h->head];
    return true;
}

/* (documented in game_core.h) */
bool game_history_redo(game_history_t *h, board_t *b)
{
    if (h->redo_len == 0) {
        return false;
    }
    game_history_push(h, b, true);
    *b = h->redo[--h->redo_len];
    return true;
}

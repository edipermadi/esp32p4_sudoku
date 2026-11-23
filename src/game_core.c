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
    for (int c = 0; geo->diag_nc && c < 72; c++) {   /* Diagonal Non-Consecutive: corners 1 apart */
        for (int k = 0; k < 2; k++) {
            int o = k ? c + 8 : c + 10;
            if ((k ? c % 9 == 0 : c % 9 == 8) || !val[c] || !val[o]) {
                continue;
            }
            if (abs(val[c] - val[o]) == 1) {
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
            for (int i = 0; i < n / 2; i++) {
                int a = cell[i], b = cell[n - 1 - i];
                if (val[a] && val[b] && val[a] != val[b]) {
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
    for (int k = 0; k < geo->ncages && geo->cage_sum[0]; k++) {   /* Killer: a cage over its sum, or full and short of it */
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

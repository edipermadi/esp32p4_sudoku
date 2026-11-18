/* Backtracking with bitmasks, always on the empty cell with the fewest candidates. */
#include "solver.h"
#include <string.h>

typedef struct {
    uint8_t v[81];
    uint16_t row[9], col[9], box[9];    /* bit d set = digit d used */
} grid_t;

#define BOX_OF(i) (((i) / 27) * 3 + ((i) % 9) / 3)

static bool search(grid_t *g)
{
    int best = -1, best_n = 10;
    uint16_t best_free = 0;
    for (int i = 0; i < 81; i++) {
        if (g->v[i]) continue;
        uint16_t free = (uint16_t)(~(g->row[i / 9] | g->col[i % 9] | g->box[BOX_OF(i)]) & 0x3FE);
        int n = __builtin_popcount(free);
        if (n < best_n) {
            best = i;
            best_n = n;
            best_free = free;
            if (n <= 1) break;
        }
    }
    if (best < 0) return true;                  /* nothing empty: solved */
    if (best_n == 0) return false;
    int r = best / 9, c = best % 9, b = BOX_OF(best);
    for (int d = 1; d <= 9; d++) {
        uint16_t m = (uint16_t)(1u << d);
        if (!(best_free & m)) continue;
        g->v[best] = (uint8_t)d;
        g->row[r] |= m; g->col[c] |= m; g->box[b] |= m;
        if (search(g)) return true;
        g->row[r] &= (uint16_t)~m; g->col[c] &= (uint16_t)~m; g->box[b] &= (uint16_t)~m;
    }
    g->v[best] = 0;
    return false;
}

bool sudoku_solve(const uint8_t cells[81], uint8_t out[81])
{
    static grid_t g;                            /* off the (small) LVGL task stack */
    memset(&g, 0, sizeof(g));
    for (int i = 0; i < 81; i++) {
        int d = cells[i];
        if (d < 1 || d > 9) continue;
        uint16_t m = (uint16_t)(1u << d);
        if ((g.row[i / 9] | g.col[i % 9] | g.box[BOX_OF(i)]) & m) return false;
        g.v[i] = (uint8_t)d;
        g.row[i / 9] |= m; g.col[i % 9] |= m; g.box[BOX_OF(i)] |= m;
    }
    if (!search(&g)) return false;
    memcpy(out, g.v, 81);
    return true;
}

/**
 * @file ui_game_layout.c
 * @ingroup ui_game
 * @brief The game screen's layout: the board, the pad and the buttons placed for the variant and the hand (Left-handed).
 */
#include "ui_internal.h"

static bool      g_layout_left;         /**< ... and on which side the pad is (Left-handed) */

/* (documented in ui_internal.h) */
void board_layout_at(const sudoku_geom_t *geo, lv_obj_t *grid, lv_obj_t *const *boxes, lv_obj_t *const *cells,
                     lv_obj_t *const *clues, int x0, int y0, int size_all)
{
    lv_obj_set_user_data(grid, (void *)geo);
    const bool clued = geo->sandwich || geo->skyscraper || geo->nlk || geo->xsums || geo->frame || geo->outside ||
                       geo->rossini || geo->nrooms || geo->skysum || geo->psky || geo->b9 || geo->n9 || geo->bf;   /* clues outside */
    for (int i = 0; clues && i < BOARD_CLUES; i++) {
        lv_obj_set_hidden(clues[i], true);
    }
    if (is_jigsaw_variant(geo->variant)) {
        /* evenly spaced cells on the thin-line colour in a thick frame; the
         * region borders are drawn over them (board_regions_draw_cb); Jigsaw 7x7: 7 of them a side */
        const int n = geo->n;
        const int cs = (size_all - 2 * LINE_THICK - (n - 1) * LINE_THIN) / n;
        const int w = 2 * LINE_THICK + n * cs + (n - 1) * LINE_THIN;
        lv_obj_set_pos(grid, x0 + (size_all - w) / 2, y0 + (size_all - w) / 2);
        lv_obj_set_size(grid, w, w);
        lv_obj_set_style_bg_color(grid, C_GRID_THIN, 0);
        lv_obj_set_style_border_color(grid, C_GRID_THICK, 0);
        lv_obj_set_style_border_width(grid, LINE_THICK, 0);
        for (int b = 0; b < 9; b++) {
            lv_obj_set_hidden(boxes[b], true);
        }
        for (int i = 0; i < 81; i++) {
            lv_obj_set_hidden(cells[i], !geo->used[i]);
            if (!geo->used[i]) {
                continue;
            }
            lv_obj_set_pos(cells[i], LINE_THICK + (i % 9) * (cs + LINE_THIN), LINE_THICK + (i / 9) * (cs + LINE_THIN));
            lv_obj_set_size(cells[i], cs, cs);
        }
        lv_obj_invalidate(grid);
        return;
    }
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    const int n = geo->n, bw = geo->box_w, bh = geo->box_h;
    const int across = n / bw, down = n / bh;                  /* boxes */
    const int band = clued ? 36 : 0;           /* Sandwich, Skyscraper, Little Killer, X-Sums: room for the clues, top and left */
    const int size = size_all - band;
    int cw = (size - (across + 1) * LINE_THICK - across * (bw - 1) * LINE_THIN) / n;
    int ch = (size - (down + 1) * LINE_THICK - down * (bh - 1) * LINE_THIN) / n;
    int cs = cw < ch ? cw : ch;
    int box_w = bw * cs + (bw - 1) * LINE_THIN, box_h = bh * cs + (bh - 1) * LINE_THIN;
    int w = (across + 1) * LINE_THICK + across * box_w, h = (down + 1) * LINE_THICK + down * box_h;
    const int gx = x0 + band + (size - w) / 2, gy = y0 + band + (size - h) / 2;
    lv_obj_set_pos(grid, gx, gy);
    lv_obj_set_size(grid, w, h);
    for (int i = 0; (geo->sandwich || geo->skyscraper || geo->xsums || geo->frame || geo->outside || geo->rossini ||
                     geo->nrooms || geo->skysum || geo->psky || geo->b9 || geo->n9 || geo->bf) && clues && i < 18; i++) {   /* left of the rows, above the columns */
        int k = i % 9, v = geo->sandwich ? geo->sandwich_sum[i] : geo->skyscraper ? geo->sky[i] : geo->xsums ? geo->xsum[i]
                           : geo->frame ? geo->frame_sum[i] : geo->outside ? geo->outside_set[i]
                           : geo->nrooms ? geo->nroom[i] : geo->skysum ? geo->sky_sum[i] : geo->psky ? geo->psky_clue[i]
                           : geo->b9 ? geo->b9_sum[i] : geo->bf ? geo->bf_clue[i] : geo->n9 ? geo->n9_set[i] << 1   /* (as Outside's: bit d = d) */
                           : geo->rossini_dir[i];
        int mid = LINE_THICK + (k / 3) * (box_w + LINE_THICK) + (k % 3) * (cs + LINE_THIN) + cs / 2;
        char t[12];
        if (geo->outside || geo->n9) {              /* its digits, side by side */
            int n = 0;
            for (int d = 1; d <= 9; d++) {
                if (v >> d & 1) {
                    t[n++] = (char)('0' + d);
                }
            }
            t[n] = 0;
        } else if (geo->rossini) {                  /* an arrow toward the bigger digits */
            snprintf(t, sizeof(t), "%s", v == ROSSINI_NONE ? "" : i < 9 ? (v == ROSSINI_UP ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT)
                     : (v == ROSSINI_UP ? LV_SYMBOL_DOWN : LV_SYMBOL_UP));
        } else {
            snprintf(t, sizeof(t), "%d", geo->psky ? v & ~PSKY_EVEN : geo->bf ? v - 1 : v);   /* (Battlefield: sum + 1) */
        }
        /* Odd/Even Skyscrapers: a ring round a clue of odd buildings, a shaded square behind one of even ones (as
         * Even-Odd's cells); every other clue plain (the labels are shared by every board) */
        const bool ring = geo->psky && v && !(v & PSKY_EVEN), shade = geo->psky && (v & PSKY_EVEN);
        lv_obj_set_style_border_width(clues[i], ring ? 2 : 0, 0);
        lv_obj_set_style_border_color(clues[i], C_MUTED, 0);
        lv_obj_set_style_radius(clues[i], ring ? LV_RADIUS_CIRCLE : 4, 0);
        lv_obj_set_style_bg_opa(clues[i], shade ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(clues[i], C_CARD2, 0);
        lv_obj_set_user_data(clues[i], NULL);
        lv_obj_set_style_text_font(clues[i], geo->outside && strlen(t) > 2 ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);
        lv_label_set_text(clues[i], t);
        lv_obj_set_size(clues[i], geo->psky ? 26 : band - 6, geo->psky ? 26 : 24);   /* (Odd/Even: round rings) */
        if (i < 9) {
            lv_obj_set_pos(clues[i], gx - band, gy + mid - 12);
        } else {
            lv_obj_set_pos(clues[i], gx + mid - (band - 6) / 2, gy - band + 4);
        }
        lv_obj_set_hidden(clues[i], !geo->sandwich && !v);   /* (a line without a clue; Rossini: without an arrow) */
    }
    for (int k = 0; geo->nlk && clues && k < geo->nlk; k++) {   /* Little Killer: a sum on the line before its diagonal */
        int first = geo->lk_cell[k][0], top = geo->lk_from_top[k], i = top ? first % 9 : first / 9;
        int spot = i + (geo->lk_dir[k] == LK_DOWN_RIGHT ? 0 : 1);   /* the line before / after that column or row */
        int at = spot < 9 ? LINE_THICK + (spot / 3) * (box_w + LINE_THICK) + (spot % 3) * (cs + LINE_THIN) - 1
                 : LINE_THICK + 2 * (box_w + LINE_THICK) + box_w + 1;
        lv_obj_t *l = clues[(top ? 10 : 0) + spot];   /* (one label per place) */
        bool has_arrow = false;                                     /* (the arrow: drawn by the label, set up once) */
        for (uint32_t k2 = 0; k2 < lv_obj_get_event_count(l); k2++) {
            has_arrow |= lv_event_dsc_get_cb(lv_obj_get_event_dsc(l, k2)) == lk_clue_draw_cb;
        }
        if (!has_arrow) {
            lv_obj_add_event_cb(l, lk_clue_draw_cb, LV_EVENT_DRAW_POST, NULL);
        }
        lv_obj_set_user_data(l, (void *)(intptr_t)(1 + geo->lk_dir[k]));
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_label_set_text_fmt(l, "%d", geo->lk_sum[k]);
        lv_obj_set_size(l, band - 4, 20);
        if (top) {
            lv_obj_set_pos(l, gx + at - (band - 4) / 2 - (geo->lk_dir[k] == LK_DOWN_RIGHT ? 6 : -6), gy - band + 2);
        } else {
            lv_obj_set_pos(l, gx - band + 1, gy + at - 10 - (geo->lk_dir[k] == LK_DOWN_RIGHT ? 6 : -6));
        }
        lv_obj_set_hidden(l, false);
    }
    for (int b = 0; b < 9; b++) {
        bool on = b < across * down;
        lv_obj_set_hidden(boxes[b], !on);
        if (!on) {
            continue;
        }
        lv_obj_set_pos(boxes[b], LINE_THICK + (b % across) * (box_w + LINE_THICK),
                       LINE_THICK + (b / across) * (box_h + LINE_THICK));
        lv_obj_set_size(boxes[b], box_w, box_h);
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_set_hidden(cells[i], !geo->used[i]);
        if (!geo->used[i]) {
            continue;
        }
        int r = i / 9, c = i % 9;
        lv_obj_set_pos(cells[i], LINE_THICK + (c / bw) * (box_w + LINE_THICK) + (c % bw) * (cs + LINE_THIN),
                       LINE_THICK + (r / bh) * (box_h + LINE_THICK) + (r % bh) * (cs + LINE_THIN));
        lv_obj_set_size(cells[i], cs, cs);
    }
}

/* (documented in ui_internal.h) */
void board_layout(const sudoku_geom_t *geo, lv_obj_t *grid, lv_obj_t *const *boxes, lv_obj_t *const *cells,
                  lv_obj_t *const *clues, int x0)
{
    board_layout_at(geo, grid, boxes, cells, clues, x0, GRID_Y, GRID_SIZE);
}

/* (documented in ui_internal.h) */
void game_layout(void)
{
    if (!game_geo()) {
        game_set_geo(sudoku_geom(game_var()));
    }
    const bool left = is_store_left_hand();           /* Settings > Game > Left-handed: the pad on the left */
    /* (a variant whose board comes with each puzzle, cages, lines, clues: sudoku_geom() has no board of its own
     * for it, and every puzzle is laid out again) */
    const bool own_board = sudoku_geom(game_var())->variant != game_var();
    if (game_layout_var() == game_var() && g_layout_left == left && !own_board) {
        lv_obj_invalidate(game_grid());
        return;
    }
    lv_obj_invalidate(game_grid());                      /* every Jigsaw / Killer has its own regions / cages */
    game_set_layout_var(game_var());
    g_layout_left = left;
    const int board_x = left ? SCR_W - 8 - GAME_GRID_SIZE : GRID_X, pad_x = left ? 8 : GAME_PAD_X;
    board_layout_at(game_geo(), game_grid(), game_boxes(), game_cells(), game_clues(), board_x, GRID_Y, GAME_GRID_SIZE);
    for (int i = 0; i < 81; i++) {                  /* (4x4: big digits) */
        if (game_cell_lbl()[i]) {
            lv_obj_set_style_text_font(game_cell_lbl()[i], DIGIT_FONT_N(game_geo()->n), 0);
        }
    }
    lv_obj_set_x(game_pause_ovl(), board_x);
    const int n = game_geo()->n, span = GAME_GRID_SIZE - 6;         /* the pad as tall as the board */
    const int h = (span - (n - 1) * PAD_GAP) / n;
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *b = game_pad()[d - 1];
        lv_obj_set_hidden(b, d > n);
        if (d > n) {
            continue;
        }
        lv_obj_set_pos(b, pad_x, GRID_Y + 1 + (d - 1) * (h + PAD_GAP) + (span - n * h - (n - 1) * PAD_GAP) / 2);
        lv_obj_set_size(b, GAME_PAD_W, h);
    }
}

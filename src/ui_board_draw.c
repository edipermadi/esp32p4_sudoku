/**
 * @file ui_board_draw.c
 * @ingroup ui_game
 * @brief The board's variant overlays: Killer cages, marks between cells, lines and thermometers, Jigsaw regions, Argyle lines, Little Killer clues, Search Nine arrows, Battenburg marks.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void board_cages_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_edges_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_quads_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_mathrax_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_clone_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_thermos_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_s9_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_batten_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);

/**
 * @brief Killer: each cage outlined with a dashed line just inside its cells, and
 * its sum small in its top-left cell (cells: the grid's children 9..89)
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_cages_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    const int in = 4;                               /* the outline's inset from the cell edge */
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = C_MUTED;
    ld.width = 1;
    ld.dash_width = 4;
    ld.dash_gap = 3;
    for (int i = 0; i < 81; i++) {
        int k = geo->cage_of[i];
        if (k == CAGE_NONE) {
            continue;
        }
        lv_area_t a;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        int r = i / 9, c = i % 9;
        bool up = r > 0 && geo->cage_of[i - 9] == k, down = r < 8 && geo->cage_of[i + 9] == k;
        bool left = c > 0 && geo->cage_of[i - 1] == k, right = c < 8 && geo->cage_of[i + 1] == k;
        /* an edge on the cage's border is drawn inset; along a shared edge
         * the lines run on to the next cell so the outline stays joined */
        int x1 = left ? a.x1 - 1 : a.x1 + in, x2 = right ? a.x2 + 1 : a.x2 - in;
        int y1 = up ? a.y1 - 1 : a.y1 + in, y2 = down ? a.y2 + 1 : a.y2 - in;
        bool label = geo->cage_cell[k][0] == i && (geo->cage_sum[k] || geo->cage_prod[k]);   /* (Sumless Killer: none) */
        const int num = geo->cage_prod[k] ? geo->cage_prod[k] : geo->cage_sum[k];   /* (Product Killer: up to 5 digits) */
        const int ndig = num >= 10000 ? 5 : num >= 1000 ? 4 : num >= 100 ? 3 : 2;
        if (!up) {
            ld.p1.x = label ? a.x1 + in + 4 + 7 * ndig : x1;   /* room for the sum (18 px for two digits) */
            ld.p1.y = ld.p2.y = a.y1 + in;
            ld.p2.x = x2;
            if (ld.p1.x < ld.p2.x) {
                lv_draw_line(layer, &ld);
            }
        }
        if (!down) {
            ld.p1.x = x1;
            ld.p2.x = x2;
            ld.p1.y = ld.p2.y = a.y2 - in;
            lv_draw_line(layer, &ld);
        }
        if (!left) {
            ld.p1.x = ld.p2.x = a.x1 + in;
            ld.p1.y = label ? a.y1 + in + 13 : y1;
            ld.p2.y = y2;
            if (ld.p1.y < ld.p2.y) {
                lv_draw_line(layer, &ld);
            }
        }
        if (!right) {
            ld.p1.x = ld.p2.x = a.x2 - in;
            ld.p1.y = y1;
            ld.p2.y = y2;
            lv_draw_line(layer, &ld);
        }
        if (label) {
            char sum[8];
            snprintf(sum, sizeof(sum), "%d", num);
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = sum;
            d.text_local = 1;
            lv_area_t la = {.x1 = a.x1 + 2, .y1 = a.y1 + 1, .x2 = a.x1 + 6 + 8 * ndig, .y2 = a.y1 + 15};
            lv_draw_label(layer, &d, &la);
        }
    }
}

/* Over the cells: Jigsaw's thick lines between regions, Killer's cages (the
 * grid's children: the 9 boxes, then the 81 cells) */
/**
 * @brief Even-Odd: a ring in each odd cell (even ones are shaded, cell_base_bg);
 * Consecutive: a bar across the line between two cells 1 apart
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_marks_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    for (int i = 0; i < 81; i++) {
        lv_area_t a, b;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        if (geo->parity[i] == PARITY_ODD) {
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_TRANSP;
            d.border_color = C_MUTED;
            d.border_width = 2;
            d.border_opa = LV_OPA_70;
            d.radius = LV_RADIUS_CIRCLE;
            lv_area_t r = {.x1 = a.x1 + 5, .y1 = a.y1 + 5, .x2 = a.x2 - 5, .y2 = a.y2 - 5};
            lv_draw_rect(layer, &d, &r);
        }
        if (!geo->consec_on) {
            continue;
        }
        lv_draw_rect_dsc_init(&d);
        d.bg_color = C_ORANGE;
        d.bg_opa = LV_OPA_COVER;
        d.radius = 3;
        d.border_color = C_CARD;
        d.border_width = 1;
        if (i % 9 < 8 && (geo->consec[i] & CONSEC_RIGHT)) {          /* upright bar on the line to the right */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 1), &b);
            int cx = (a.x2 + b.x1) / 2, cy = (a.y1 + a.y2) / 2;
            lv_area_t r = {.x1 = cx - 4, .x2 = cx + 4, .y1 = cy - 12, .y2 = cy + 12};
            lv_draw_rect(layer, &d, &r);
        }
        if (i / 9 < 8 && (geo->consec[i] & CONSEC_DOWN)) {           /* flat bar on the line below */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 9), &b);
            int cx = (a.x1 + a.x2) / 2, cy = (a.y2 + b.y1) / 2;
            lv_area_t r = {.x1 = cx - 12, .x2 = cx + 12, .y1 = cy - 4, .y2 = cy + 4};
            lv_draw_rect(layer, &d, &r);
        }
    }
}

/* (documented in ui_internal.h) */
void draw_dmark(lv_layer_t *layer, int cx, int cy, bool dl, int half)
{
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = C_ORANGE;
    ld.width = 6;
    ld.round_start = ld.round_end = 1;
    ld.p1.x = dl ? cx + half : cx - half;
    ld.p1.y = cy - half;
    ld.p2.x = dl ? cx - half : cx + half;
    ld.p2.y = cy + half;
    lv_draw_line(layer, &ld);
}

/**
 * @brief Diagonal Consecutive: the marks across the corners
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_dmarks_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int i = 0; i < 72; i++) {
        if (!geo->dmark[i]) {
            continue;
        }
        lv_area_t a;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        const int half = (a.x2 - a.x1) / 7;
        if (geo->dmark[i] & DMARK_DR) {             /* at its bottom-right corner */
            draw_dmark(layer, a.x2, a.y2, false, half);
        }
        if (geo->dmark[i] & DMARK_DL) {             /* at its bottom-left corner */
            draw_dmark(layer, a.x1, a.y2, true, half);
        }
    }
}

/* (documented in ui_internal.h) */
void draw_edge_mark(lv_layer_t *layer, int kind, int m, bool down, int cx, int cy)
{
    if (kind == EDGE_KROPKI) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_opa = LV_OPA_COVER;
        d.bg_color = m == MARK_WHITE ? lv_color_white() : lv_color_hex(0x2B2F36);
        d.border_color = m == MARK_WHITE ? lv_color_hex(0x2B2F36) : lv_color_white();
        d.border_width = m == MARK_WHITE ? 2 : 1;
        lv_area_t r = {.x1 = cx - 7, .y1 = cy - 7, .x2 = cx + 7, .y2 = cy + 7};
        lv_draw_rect(layer, &d, &r);
    } else if (kind == EDGE_XV) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = 4;
        d.bg_color = C_CARD;
        d.bg_opa = LV_OPA_COVER;
        lv_area_t r = {.x1 = cx - 9, .y1 = cy - 10, .x2 = cx + 9, .y2 = cy + 10};
        lv_draw_rect(layer, &d, &r);
        lv_draw_label_dsc_t l;
        lv_draw_label_dsc_init(&l);
        l.text = m == MARK_X ? "X" : "V";
        l.font = &lv_font_montserrat_16;
        l.color = C_ORANGE;
        l.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t t = {.x1 = cx - 9, .y1 = cy - 9, .x2 = cx + 9, .y2 = cy + 10};
        lv_draw_label(layer, &l, &t);
    } else if (kind == EDGE_GT) {
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = C_ORANGE;
        l.width = 3;
        l.round_start = l.round_end = 1;
        int s = m == MARK_LESS ? -1 : 1;            /* the point toward the smaller: left / up for LESS */
        if (!down) {
            l.p1.x = cx - 4 * s;
            l.p1.y = cy - 7;
            l.p2.x = cx + 4 * s;
            l.p2.y = cy;
            lv_draw_line(layer, &l);
            l.p1.x = cx - 4 * s;
            l.p1.y = cy + 7;
            lv_draw_line(layer, &l);
        } else {
            l.p1.x = cx - 7;
            l.p1.y = cy - 4 * s;
            l.p2.x = cx;
            l.p2.y = cy + 4 * s;
            lv_draw_line(layer, &l);
            l.p1.x = cx + 7;
            lv_draw_line(layer, &l);
        }
    }
}

/* (documented in ui_internal.h) */
void draw_quad_circle(lv_layer_t *layer, int cx, int cy, const uint8_t *dig, int n)
{
    const int rad = 16;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = C_CARD;
    d.bg_opa = LV_OPA_COVER;
    d.border_color = C_ORANGE;
    d.border_width = 2;
    lv_area_t r = {.x1 = cx - rad, .y1 = cy - rad, .x2 = cx + rad, .y2 = cy + rad};
    lv_draw_rect(layer, &d, &r);
    char txt[8];
    int p = 0;
    for (int k = 0; k < n && k < 4; k++) {
        if (k == 2) {
            txt[p++] = '\n';
        }
        txt[p++] = (char)('0' + dig[k]);
    }
    txt[p] = 0;
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.text = txt;
    l.text_local = 1;
    l.font = &lv_font_montserrat_12;
    l.color = C_TEXT;
    l.align = LV_TEXT_ALIGN_CENTER;
    l.line_space = -2;
    int h = n > 2 ? 26 : 14;
    lv_area_t t = {.x1 = cx - rad, .y1 = cy - h / 2, .x2 = cx + rad, .y2 = cy + h / 2 + 1};
    lv_draw_label(layer, &l, &t);
}

/**
 * @brief Clone Sudoku: each area outlined with a dashed line just inside its cells (as a cage), and its pair's
 * letter (A, B ...) small in its first cell, the same in both areas of a pair (they're tinted as well)
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_clone_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    const int in = 4;
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = C_MUTED;
    ld.width = 1;
    ld.dash_width = 4;
    ld.dash_gap = 3;
    for (int i = 0; i < 81; i++) {
        const int k = geo->clone_pair[i];
        if (k == CLONE_NONE) {
            continue;
        }
        /* (an area: its pair and its side, so the two areas of a pair side by side are still two) */
        const bool first = geo->clone_of[i] > i;    /* (a pair's shift has one sign for all its cells) */
        const int r = i / 9, c = i % 9;
        const bool up = r > 0 && geo->clone_pair[i - 9] == k && (geo->clone_of[i - 9] > i - 9) == first;
        const bool down = r < 8 && geo->clone_pair[i + 9] == k && (geo->clone_of[i + 9] > i + 9) == first;
        const bool left = c > 0 && geo->clone_pair[i - 1] == k && (geo->clone_of[i - 1] > i - 1) == first;
        const bool right = c < 8 && geo->clone_pair[i + 1] == k && (geo->clone_of[i + 1] > i + 1) == first;
        lv_area_t a;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        const int x1 = left ? a.x1 - 1 : a.x1 + in, x2 = right ? a.x2 + 1 : a.x2 - in;
        const int y1 = up ? a.y1 - 1 : a.y1 + in, y2 = down ? a.y2 + 1 : a.y2 - in;
        bool label = true;                          /* the letter in the area's first cell (reading order) */
        for (int j = 0; j < i && label; j++) {
            label = !(geo->clone_pair[j] == k && (geo->clone_of[j] > j) == first);
        }
        if (!up) {
            ld.p1.x = label ? a.x1 + in + 12 : x1;
            ld.p1.y = ld.p2.y = a.y1 + in;
            ld.p2.x = x2;
            lv_draw_line(layer, &ld);
        }
        if (!down) {
            ld.p1.x = x1;
            ld.p2.x = x2;
            ld.p1.y = ld.p2.y = a.y2 - in;
            lv_draw_line(layer, &ld);
        }
        if (!left) {
            ld.p1.x = ld.p2.x = a.x1 + in;
            ld.p1.y = label ? a.y1 + in + 13 : y1;
            ld.p2.y = y2;
            lv_draw_line(layer, &ld);
        }
        if (!right) {
            ld.p1.x = ld.p2.x = a.x2 - in;
            ld.p1.y = y1;
            ld.p2.y = y2;
            lv_draw_line(layer, &ld);
        }
        if (label) {
            char t[2] = {(char)('A' + k), 0};
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = t;
            d.text_local = 1;
            lv_area_t la = {.x1 = a.x1 + 3, .y1 = a.y1 + 1, .x2 = a.x1 + 16, .y2 = a.y1 + 15};
            lv_draw_label(layer, &d, &la);
        }
    }
}

/* (documented in ui_internal.h) */
void draw_mathrax_circle(lv_layer_t *layer, int cx, int cy, const char *label)
{
    const int rad = 17;                             /* (a little wider than Quadruple's: "72\u00d7" fits) */
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = C_CARD;
    d.bg_opa = LV_OPA_COVER;
    d.border_color = C_ORANGE;                      /* (as Quadruple's: not the accent, which marks the selection) */
    d.border_width = 2;
    lv_area_t r = {.x1 = cx - rad, .y1 = cy - rad, .x2 = cx + rad, .y2 = cy + rad};
    lv_draw_rect(layer, &d, &r);
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.text = label;
    l.text_local = 1;
    l.font = &font_clue_12;
    l.color = C_TEXT;
    l.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t t = {.x1 = cx - rad, .y1 = cy - 7, .x2 = cx + rad, .y2 = cy + 8};
    lv_draw_label(layer, &l, &t);
}

/**
 * @brief Kropki, XV, Greater-Than: each mark on the line between its two cells.
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_edges_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int i = 0; i < 81; i++) {
        for (int k = 0; k < 2; k++) {               /* the line to the right, the one below */
            int m = k ? EDGE_DOWN(geo->edge[i]) : EDGE_RIGHT(geo->edge[i]), o = k ? i + 9 : i + 1;
            if (!m || o > 80 || (!k && i % 9 == 8)) {
                continue;
            }
            lv_area_t a, b;
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + o), &b);
            int cx = k ? (a.x1 + a.x2) / 2 : (a.x2 + b.x1) / 2, cy = k ? (a.y2 + b.y1) / 2 : (a.y1 + a.y2) / 2;
            draw_edge_mark(layer, geo->edge_kind, m, k, cx, cy);
        }
    }
}

/**
 * @brief Quadruple: each circle with its digits, where its four cells meet.
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_quads_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int q = 0; q < geo->nquads; q++) {
        lv_area_t a, d;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->quad_at[q]), &a);
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->quad_at[q] + 10), &d);
        draw_quad_circle(layer, (a.x2 + d.x1) / 2, (a.y2 + d.y1) / 2, geo->quad_dig[q], geo->quad_n[q]);
    }
}

/**
 * @brief Mathrax: each circle with its clue, where its four cells meet.
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_mathrax_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int q = 0; q < geo->nmx; q++) {
        lv_area_t a, d;
        char label[8];
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->mx_at[q]), &a);
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->mx_at[q] + 10), &d);
        sudoku_mathrax_label(geo->mx_op[q], geo->mx_val[q], label);
        draw_mathrax_circle(layer, (a.x2 + d.x1) / 2, (a.y2 + d.y1) / 2, label);
    }
}

/* (documented in ui_internal.h) */
void draw_line_path(lv_layer_t *layer, int kind, const lv_point_t *pts, int n, int cs)
{
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.round_start = ld.round_end = 1;
    int from = 0;
    lv_point_t start = pts[0], end = pts[n - 1];
    switch (kind) {
    case LINE_ARROW:
    case LINE_PILL:                                 /* (Pill: the shaft from the pill's second cell) */
        ld.color = C_MUTED;
        ld.opa = LV_OPA_80;
        ld.width = 4;
        from = kind == LINE_PILL ? 1 : 0;
        if (n > from + 1) {                         /* the shaft starts at the circle's (pill's) edge */
            int dx = pts[from + 1].x - pts[from].x, dy = pts[from + 1].y - pts[from].y, r = cs * 4 / 10;
            int len = (int)sqrtf((float)(dx * dx + dy * dy));
            if (len) {
                start.x = pts[from].x + dx * r / len;
                start.y = pts[from].y + dy * r / len;
            }
        }
        break;
    case LINE_RENBAN:
        ld.color = lv_color_hex(0xA66BE0);
        ld.opa = LV_OPA_40;
        ld.width = cs * 35 / 100;
        break;
    case LINE_WHISPER:
        ld.color = lv_color_hex(0x2EAD5B);
        ld.opa = LV_OPA_70;
        ld.width = cs * 12 / 100;
        break;
    case LINE_DUTCH:
        ld.color = lv_color_hex(0xD9A400);
        ld.opa = LV_OPA_80;
        ld.width = cs * 12 / 100;
        break;
    case LINE_PARITY:
        ld.color = lv_color_hex(0xD94A4A);
        ld.opa = LV_OPA_70;
        ld.width = cs * 12 / 100;
        break;
    case LINE_SEQ:                                  /* (a medium indigo line) */
        ld.color = lv_color_hex(0x3F4FB8);
        ld.opa = LV_OPA_60;
        ld.width = cs * 20 / 100;
        break;
    case LINE_AVG:                                  /* (a teal line from its circle) */
        ld.color = lv_color_hex(0x2E9E8F);
        ld.opa = LV_OPA_80;
        ld.width = 4;
        if (n > 1) {
            int dx = pts[1].x - pts[0].x, dy = pts[1].y - pts[0].y, r = cs * 4 / 10;
            int len = (int)sqrtf((float)(dx * dx + dy * dy));
            if (len) {
                start.x = pts[0].x + dx * r / len;
                start.y = pts[0].y + dy * r / len;
            }
        }
        break;
    case LINE_TEN:
        ld.color = lv_color_hex(0x3FA34D);
        ld.opa = LV_OPA_50;
        ld.width = cs * 20 / 100;
        break;
    case LINE_NABNER:
        ld.color = lv_color_hex(0xA0703C);
        ld.opa = LV_OPA_80;
        ld.width = cs * 12 / 100;
        break;
    case LINE_PALINDROME:
        ld.color = lv_color_hex(0x8A8F98);
        ld.opa = LV_OPA_60;
        ld.width = cs * 20 / 100;
        break;
    case LINE_REGIONSUM:
        ld.color = lv_color_hex(0x3D8BEB);
        ld.opa = LV_OPA_70;
        ld.width = cs * 12 / 100;
        break;
    case LINE_ENTROPIC:
        ld.color = lv_color_hex(0xE8912D);
        ld.opa = LV_OPA_70;
        ld.width = cs * 12 / 100;
        break;
    case LINE_MODULAR:
        ld.color = lv_color_hex(0x1FA5A5);
        ld.opa = LV_OPA_70;
        ld.width = cs * 12 / 100;
        break;
    case LINE_ZIPPER:
        ld.color = lv_color_hex(0xE0679B);
        ld.opa = LV_OPA_70;
        ld.width = cs * 12 / 100;
        break;
    case LINE_BETWEEN:                              /* from circle edge to circle edge */
    case LINE_LOCKOUT:                              /* (from diamond to diamond) */
    case LINE_DARROW:
        ld.color = kind == LINE_DARROW ? lv_color_hex(0x4A7FD9) : C_MUTED;
        ld.opa = LV_OPA_80;
        ld.width = 4;
        for (int e = 0; e < 2 && n > 1; e++) {
            const lv_point_t c = e ? pts[n - 1] : pts[0], nx = e ? pts[n - 2] : pts[1];
            int dx = nx.x - c.x, dy = nx.y - c.y, r = cs * 4 / 10;
            int len = (int)sqrtf((float)(dx * dx + dy * dy));
            if (len) {
                lv_point_t *at = e ? &end : &start;
                at->x = c.x + dx * r / len;
                at->y = c.y + dy * r / len;
            }
        }
        break;
    default:
        ld.color = C_MUTED;
        ld.opa = LV_OPA_40;
        ld.width = cs * 3 / 10;
    }
    for (int i = from + 1; i < n; i++) {
        ld.p1.x = i == from + 1 ? start.x : pts[i - 1].x;
        ld.p1.y = i == from + 1 ? start.y : pts[i - 1].y;
        ld.p2.x = i == n - 1 ? end.x : pts[i].x;
        ld.p2.y = i == n - 1 ? end.y : pts[i].y;
        lv_draw_line(layer, &ld);
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    int r = cs * 4 / 10;
    lv_area_t b = {.x1 = pts[0].x - r, .y1 = pts[0].y - r, .x2 = pts[0].x + r, .y2 = pts[0].y + r};
    if (kind == LINE_THERMO || kind == LINE_SLOW) {   /* the bulb */
        d.bg_color = C_MUTED;
        d.bg_opa = LV_OPA_40;
        lv_draw_rect(layer, &d, &b);
    } else if (kind == LINE_AVG) {                  /* the circle at its start */
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = lv_color_hex(0x2E9E8F);
        d.border_opa = LV_OPA_80;
        d.border_width = 3;
        lv_draw_rect(layer, &d, &b);
    } else if (kind == LINE_LOCKOUT) {              /* a diamond at each end */
        for (int e = 0; e < 2; e++) {
            const lv_point_t c = e ? pts[n - 1] : pts[0];
            const int dx[4] = {0, r, 0, -r}, dy[4] = {-r, 0, r, 0};
            for (int k = 0; k < 4; k++) {
                ld.p1.x = c.x + dx[k];
                ld.p1.y = c.y + dy[k];
                ld.p2.x = c.x + dx[(k + 1) % 4];
                ld.p2.y = c.y + dy[(k + 1) % 4];
                lv_draw_line(layer, &ld);
            }
        }
    } else if (kind == LINE_BETWEEN || kind == LINE_DARROW) {   /* a circle at each end */
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = kind == LINE_DARROW ? lv_color_hex(0x4A7FD9) : C_MUTED;
        d.border_opa = LV_OPA_80;
        d.border_width = 3;
        lv_draw_rect(layer, &d, &b);
        lv_area_t b2 = {.x1 = pts[n - 1].x - r, .y1 = pts[n - 1].y - r, .x2 = pts[n - 1].x + r, .y2 = pts[n - 1].y + r};
        lv_draw_rect(layer, &d, &b2);
    } else if (kind == LINE_ARROW || kind == LINE_PILL) {   /* the circle (or pill), and the arrowhead */
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = C_MUTED;
        d.border_opa = LV_OPA_80;
        d.border_width = 3;
        if (kind == LINE_PILL && n > 1) {           /* a capsule round both of the pill's cells */
            b.x1 = LV_MIN(pts[0].x, pts[1].x) - r;
            b.y1 = LV_MIN(pts[0].y, pts[1].y) - r;
            b.x2 = LV_MAX(pts[0].x, pts[1].x) + r;
            b.y2 = LV_MAX(pts[0].y, pts[1].y) + r;
        }
        lv_draw_rect(layer, &d, &b);
        if (n > 1) {
            int dx = pts[n - 1].x - pts[n - 2].x, dy = pts[n - 1].y - pts[n - 2].y;
            float len = sqrtf((float)(dx * dx + dy * dy));
            if (len > 0) {
                float ux = dx / len, uy = dy / len, h = cs * 0.22f;
                for (int s = -1; s <= 1; s += 2) {
                    ld.p1.x = pts[n - 1].x;
                    ld.p1.y = pts[n - 1].y;
                    ld.p2.x = (int32_t)(pts[n - 1].x - ux * h - s * uy * h * 0.8f);
                    ld.p2.y = (int32_t)(pts[n - 1].y - uy * h + s * ux * h * 0.8f);
                    lv_draw_line(layer, &ld);
                }
            }
        }
    }
}

/**
 * @brief the board's lines (Thermo, Arrow, Renban, Whispers, Palindrome, Region Sum) through the cells' centres
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_thermos_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int t = 0; t < geo->nthermos; t++) {
        lv_point_t pts[9];
        int cs = 0;
        for (int i = 0; i < geo->thermo_n[t]; i++) {
            lv_area_t a;
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + geo->thermo_cell[t][i]), &a);
            pts[i].x = (a.x1 + a.x2) / 2;
            pts[i].y = (a.y1 + a.y2) / 2;
            cs = lv_area_get_width(&a);
        }
        draw_line_path(layer, geo->line_kind, pts, geo->thermo_n[t], cs);
    }
}

/* (documented in ui_internal.h) */
void board_regions_draw_cb(lv_event_t *e)
{
    lv_obj_t *grid = lv_event_get_current_target_obj(e);
    const sudoku_geom_t *geo = lv_obj_get_user_data(grid);
    if (geo && geo->nthermos) {
        board_thermos_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && geo->ncages && geo->variant != VAR_REGIONS) {   /* (Extra Regions: tinted instead) */
        board_cages_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && (geo->consec_on || geo->variant == VAR_EVENODD)) {
        board_marks_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && geo->variant == VAR_DIAGCONSEC) {
        board_dmarks_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && geo->edge_kind && geo->edge_kind != EDGE_FORT) {   /* (Fortress: the grey cells say it all) */
        board_edges_draw(grid, geo, lv_event_get_layer(e));
    }
    for (int k = 0; geo && geo->variant == VAR_ARGYLE && k < ARGYLE_LINES; k++) {
        uint8_t line[9];
        const int n = sudoku_argyle_line(k, line);
        lv_area_t a, b;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + line[0]), &a);
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + line[n - 1]), &b);
        draw_argyle_line(lv_event_get_layer(e), &a, &b, k < ARGYLE_LINES / 2, 3);
    }
    if (geo && geo->nquads) {
        board_quads_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && geo->nmx) {
        board_mathrax_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && geo->nclone) {
        board_clone_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && geo->s9) {
        board_s9_draw(grid, geo, lv_event_get_layer(e));
    }
    if (geo && geo->batten) {
        board_batten_draw(grid, geo, lv_event_get_layer(e));
    }
    if (!geo || !is_jigsaw_variant(geo->variant)) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = C_GRID_THICK;
    d.bg_opa = LV_OPA_COVER;
    for (int i = 0; i < 81; i++) {
        lv_area_t a, b, r;
        lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
        if (!geo->used[i]) {
            continue;                               /* (Jigsaw 7x7: off the board) */
        }
        if (i % 9 < geo->n - 1 && geo->region[i] != geo->region[i + 1]) {   /* border on the right */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 1), &b);
            r = (lv_area_t) {.x1 = a.x2 - 1, .x2 = b.x1 + 1, .y1 = a.y1 - LINE_THIN, .y2 = a.y2 + LINE_THIN};
            lv_draw_rect(layer, &d, &r);
        }
        if (i / 9 < geo->n - 1 && geo->region[i] != geo->region[i + 9]) {   /* border below */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 9), &b);
            r = (lv_area_t) {.x1 = a.x1 - LINE_THIN, .x2 = a.x2 + LINE_THIN, .y1 = a.y2 - 1, .y2 = b.y1 + 1};
            lv_draw_rect(layer, &d, &r);
        }
    }
}

/* (documented in ui_internal.h) */
void draw_argyle_line(lv_layer_t *layer, const lv_area_t *first, const lv_area_t *last, bool down_right, int width)
{
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = C_ACCENT;
    l.opa = LV_OPA_50;
    l.width = width;
    l.round_start = l.round_end = 1;
    l.p1.x = down_right ? first->x1 : first->x2;    /* from the first cell's outer corner to the last one's */
    l.p1.y = first->y1;
    l.p2.x = down_right ? last->x2 : last->x1;
    l.p2.y = last->y2;
    lv_draw_line(layer, &l);
}

/* (documented in ui_internal.h) */
void draw_lk_arrow(lv_layer_t *layer, int x, int y, int dir, int len)
{
    int dx = dir == LK_DOWN_LEFT ? -1 : 1, dy = dir == LK_UP_RIGHT ? -1 : 1;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = C_ACCENT;
    l.width = 2;
    l.round_start = l.round_end = 1;
    int ex = x + dx * len, ey = y + dy * len, h = len / 2 + 1;
    l.p1.x = x;
    l.p1.y = y;
    l.p2.x = ex;
    l.p2.y = ey;
    lv_draw_line(layer, &l);
    l.p1.x = ex;
    l.p1.y = ey;
    l.p2.x = ex - dx * h;
    l.p2.y = ey;
    lv_draw_line(layer, &l);
    l.p2.x = ex;
    l.p2.y = ey - dy * h;
    lv_draw_line(layer, &l);
}

/* (documented in ui_internal.h) */
void draw_batten_mark(lv_layer_t *layer, int cx, int cy, int half)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;
    d.bg_color = C_CARD;                            /* (a thin frame, so it stands off the grid lines) */
    lv_area_t f = {.x1 = cx - half - 2, .y1 = cy - half - 2, .x2 = cx + half + 1, .y2 = cy + half + 1};
    lv_draw_rect(layer, &d, &f);
    for (int q = 0; q < 4; q++) {                   /* pink top-left and bottom-right, yellow the other two */
        d.bg_color = lv_color_hex(q == 0 || q == 3 ? 0xEE7FA6 : 0xF2CD4A);
        const int x = q & 1 ? cx : cx - half, y = q & 2 ? cy : cy - half;
        lv_area_t a = {.x1 = x, .y1 = y, .x2 = x + half - 1, .y2 = y + half - 1};
        lv_draw_rect(layer, &d, &a);
    }
}

/**
 * @brief Battenburg: each mark, at its cell's bottom-right corner
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_batten_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int i = 0; i < 72; i++) {
        if (geo->bat[i]) {
            lv_area_t a, b;
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 10), &b);
            draw_batten_mark(layer, (a.x2 + b.x1) / 2 + 1, (a.y2 + b.y1) / 2 + 1, (a.x2 - a.x1) * 2 / 11);
        }
    }
}

/* (documented in ui_internal.h) */
void draw_s9_arrow(lv_layer_t *layer, const lv_area_t *cell, int dir)
{
    const int dx = dir == S9_RIGHT ? 1 : dir == S9_LEFT ? -1 : 0, dy = dir == S9_DOWN ? 1 : dir == S9_UP ? -1 : 0;
    if (!dx && !dy) {
        return;
    }
    const int cs = cell->x2 - cell->x1, len = cs / 6, head = cs / 12 + 1, in = cs / 22 + 2, off = cs / 5;
    /* the tip by the side it points to, a little inside, off toward that side's right (up, down) or top (left,
     * right) end: clear of the digit in the middle; the shaft back into the cell */
    const int tx = dx > 0 ? cell->x2 - in : dx < 0 ? cell->x1 + in : cell->x2 - off;
    const int ty = dy > 0 ? cell->y2 - in : dy < 0 ? cell->y1 + in : cell->y1 + off;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = C_ACCENT;
    l.width = cs >= 48 ? 3 : 2;
    l.round_start = l.round_end = 1;
    l.p1.x = tx - dx * len;
    l.p1.y = ty - dy * len;
    l.p2.x = tx;
    l.p2.y = ty;
    lv_draw_line(layer, &l);
    for (int side = -1; side <= 1; side += 2) {     /* the head: two strokes back from the tip */
        l.p1.x = tx - dx * head + dy * side * head;
        l.p1.y = ty - dy * head + dx * side * head;
        lv_draw_line(layer, &l);
    }
}

/**
 * @brief Search Nine: each arrow
 * @param[in] grid  the board
 * @param[in] geo  its board
 * @param[in] layer  where
 */
static void board_s9_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer)
{
    for (int i = 0; i < 81; i++) {
        if (geo->s9_dir[i]) {
            lv_area_t a;
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i), &a);
            draw_s9_arrow(layer, &a, geo->s9_dir[i]);
        }
    }
}

/* (documented in ui_internal.h) */
void lk_clue_draw_cb(lv_event_t *e)
{
    lv_obj_t *l = lv_event_get_current_target_obj(e);
    int dir = (int)(intptr_t)lv_obj_get_user_data(l) - 1;
    if (dir < 0) {
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(l, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    int dx = dir == LK_DOWN_LEFT ? -1 : 1, dy = dir == LK_UP_RIGHT ? -1 : 1;
    draw_lk_arrow(lv_event_get_layer(e), cx + dx * 11, cy + dy * 6, dir, 7);
}

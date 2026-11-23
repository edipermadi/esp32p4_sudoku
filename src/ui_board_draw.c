/**
 * @file ui_board_draw.c
 * @ingroup ui_game
 * @brief The board's variant overlays: Killer cages, marks between cells, lines and thermometers, Jigsaw regions, Argyle lines, Little Killer clues.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void board_cages_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_edges_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_quads_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);
static void board_thermos_draw(lv_obj_t *grid, const sudoku_geom_t *geo, lv_layer_t *layer);

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
        bool label = geo->cage_cell[k][0] == i && geo->cage_sum[k];   /* (Sumless Killer: none) */
        if (!up) {
            ld.p1.x = label ? a.x1 + in + 18 : x1;      /* room for the sum */
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
            char sum[4];
            snprintf(sum, sizeof(sum), "%d", geo->cage_sum[k]);
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = sum;
            d.text_local = 1;
            lv_area_t la = {.x1 = a.x1 + 2, .y1 = a.y1 + 1, .x2 = a.x1 + 26, .y2 = a.y1 + 15};
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
        ld.color = C_MUTED;
        ld.opa = LV_OPA_80;
        ld.width = 4;
        if (n > 1) {                                /* the shaft starts at the circle's edge */
            int dx = pts[1].x - pts[0].x, dy = pts[1].y - pts[0].y, r = cs * 4 / 10;
            int len = (int)sqrtf((float)(dx * dx + dy * dy));
            if (len) {
                start.x = pts[0].x + dx * r / len;
                start.y = pts[0].y + dy * r / len;
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
        ld.p1.x = i == 1 ? start.x : pts[i - 1].x;
        ld.p1.y = i == 1 ? start.y : pts[i - 1].y;
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
    } else if (kind == LINE_ARROW) {                /* the circle, and the arrowhead at the shaft's end */
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = C_MUTED;
        d.border_opa = LV_OPA_80;
        d.border_width = 3;
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
    if (geo && geo->ncages) {
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
    if (!geo || geo->variant != VAR_JIGSAW) {
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
        if (i % 9 < 8 && geo->region[i] != geo->region[i + 1]) {          /* border on the right */
            lv_obj_get_coords(lv_obj_get_child(grid, 9 + i + 1), &b);
            r = (lv_area_t) {.x1 = a.x2 - 1, .x2 = b.x1 + 1, .y1 = a.y1 - LINE_THIN, .y2 = a.y2 + LINE_THIN};
            lv_draw_rect(layer, &d, &r);
        }
        if (i / 9 < 8 && geo->region[i] != geo->region[i + 9]) {          /* border below */
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

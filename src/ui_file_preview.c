/**
 * @file ui_file_preview.c
 * @ingroup ui_file
 * @brief Files: a puzzle file's preview: its puzzles one at a time on a board, what's in it, Close and
 *        Import.
 */
#include "ui_file_part.h"
#include "files_core.h"
#include "ui_palette.h"
#include "stats_db.h"
#include "generator.h"
#include "timezones.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(font_digits_44)      /* the game's board digits */

/* A tap on an .sdm, .sdx or .opensudoku file: its puzzles one at a time on a board (< > to step
 * through them), what's in it, and Close / Import (stats_db_import_file: the same
 * import as Settings > Data > Import puzzles, for this file, wherever it is). The file
 * is read here, on the spot: they are small (a line per puzzle). */
#define PV_BOARD     432                         /**< px: 48 per cell (9x9), 72 (6x6) */
static lv_obj_t *s_pv;                           /**< the preview (the whole screen, top layer) */
static lv_obj_t *s_pv_board;                     /**< its board */
static lv_obj_t *s_pv_pos;                       /**< "3 / 42  -  Killer Hard" */
static lv_obj_t *s_pv_prev;                      /**< < */
static lv_obj_t *s_pv_next;                      /**< > */
static int s_pv_at;                              /**< the puzzle shown */
static char s_pv_path[FILES_PATH_MAX + FILES_NAME_MAX];   /**< the file (Import) */
static uint8_t s_pv_givens[81];                  /**< the puzzle shown's digits (its board: files_pv_geom()) */
static int s_pv_var;                             /**< its variant */
static int s_pv_lv;                              /**< its level (-1: none given) */
static bool s_pv_shown;                          /**< a puzzle could be drawn */

/**
 * @brief Draws a line on the preview's board.
 * @param[in] layer  where
 * @param[in] x1     from x
 * @param[in] y1     from y
 * @param[in] x2     to x
 * @param[in] y2     to y
 * @param[in] w      its width
 * @param[in] c      its colour
 */
static void pv_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int w, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = w;
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    lv_draw_line(layer, &d);
}

/**
 * @brief Draws the preview's board: cells (the X-Sudoku diagonals and Hyper windows tinted), the
 *        givens, thin lines between cells and thick ones between boxes (Jigsaw: regions); Killer:
 *        the cages, dashed in the accent colour, with their sums.
 * @param[in] e  the draw event
 */
static void pv_draw_cb(lv_event_t *e)
{
    if (!s_pv_shown) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const sudoku_geom_t *g = files_pv_geom();
    int n = g->n, cs = PV_BOARD / n;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = n == 9 ? &lv_font_montserrat_28 : &font_digits_44;   /* (6x6: the game's own digits) */
    ld.color = C_TEXT;
    ld.align = LV_TEXT_ALIGN_CENTER;
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int cell = r * 9 + c;
            lv_area_t ca = {a.x1 + c * cs, a.y1 + r * cs, a.x1 + (c + 1) * cs - 1, a.y1 + (r + 1) * cs - 1};
            rd.bg_color = is_sudoku_geom_tinted(g, cell) ? C_CELL_PEER : (s_pv_givens[cell] ? C_CELL_GIVEN : C_CELL);
            lv_draw_rect(layer, &rd, &ca);
            if (s_pv_givens[cell]) {
                char t[2] = {(char)('0' + s_pv_givens[cell]), 0};
                lv_area_t ta = ca;
                ta.y1 += (cs - lv_font_get_line_height(ld.font)) / 2;
                ld.text = t;
                ld.text_local = 1;
                lv_draw_label(layer, &ld, &ta);
            }
        }
    }
    /* Killer cages: a dashed line just inside each cage edge, the sum in its first cell */
    if (s_pv_var == VAR_KILLER || s_pv_var == VAR_SUMLESS) {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = C_ACCENT;
        d.width = 1;
        d.dash_width = 4;
        d.dash_gap = 3;
        const int in = 4;
        for (int r = 0; r < n; r++) {
            for (int c = 0; c < n; c++) {
                int cell = r * 9 + c, k = g->cage_of[cell];
                if (k == CAGE_NONE) {
                    continue;
                }
                int x1 = a.x1 + c * cs, y1 = a.y1 + r * cs, x2 = x1 + cs - 1, y2 = y1 + cs - 1;
                bool top = r == 0 || g->cage_of[cell - 9] != k, bot = r == n - 1 || g->cage_of[cell + 9] != k;
                bool lef = c == 0 || g->cage_of[cell - 1] != k, rig = c == n - 1 || g->cage_of[cell + 1] != k;
                int xa = lef ? x1 + in : x1, xb = rig ? x2 - in : x2, ya = top ? y1 + in : y1, yb = bot ? y2 - in : y2;
                if (top) {
                    d.p1.x = xa;
                    d.p1.y = ya;
                    d.p2.x = xb;
                    d.p2.y = ya;
                    lv_draw_line(layer, &d);
                }
                if (bot) {
                    d.p1.x = xa;
                    d.p1.y = yb;
                    d.p2.x = xb;
                    d.p2.y = yb;
                    lv_draw_line(layer, &d);
                }
                if (lef) {
                    d.p1.x = xa;
                    d.p1.y = ya;
                    d.p2.x = xa;
                    d.p2.y = yb;
                    lv_draw_line(layer, &d);
                }
                if (rig) {
                    d.p1.x = xb;
                    d.p1.y = ya;
                    d.p2.x = xb;
                    d.p2.y = yb;
                    lv_draw_line(layer, &d);
                }
                if (g->cage_cell[k][0] == cell && g->cage_sum[k]) {   /* the top-left cell: the sum */
                    char t[4];
                    snprintf(t, sizeof(t), "%d", g->cage_sum[k]);
                    lv_draw_label_dsc_t sd;
                    lv_draw_label_dsc_init(&sd);
                    sd.font = &lv_font_montserrat_12;
                    sd.color = C_ACCENT;
                    sd.text = t;
                    sd.text_local = 1;
                    lv_area_t sa = {x1 + in + 2, y1 + in + 1, x2, y2};
                    lv_draw_label(layer, &sd, &sa);
                }
            }
        }
    }
    /* lines: thin between cells, thick between boxes / regions and round the edge */
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int cell = r * 9 + c, x = a.x1 + c * cs, y = a.y1 + r * cs;
            if (c < n - 1) {
                bool thick = g->region[cell] != g->region[cell + 1];
                pv_line(layer, x + cs, y, x + cs, y + cs, thick ? 3 : 1, thick ? C_GRID_THICK : C_GRID_THIN);
            }
            if (r < n - 1) {
                bool thick = g->region[cell] != g->region[cell + 9];
                pv_line(layer, x, y + cs, x + cs, y + cs, thick ? 3 : 1, thick ? C_GRID_THICK : C_GRID_THIN);
            }
        }
    }
    int e2 = a.x1 + n * cs, f2 = a.y1 + n * cs;
    pv_line(layer, a.x1, a.y1 + 1, e2, a.y1 + 1, 3, C_GRID_THICK);
    pv_line(layer, a.x1, f2 - 1, e2, f2 - 1, 3, C_GRID_THICK);
    pv_line(layer, a.x1 + 1, a.y1, a.x1 + 1, f2, 3, C_GRID_THICK);
    pv_line(layer, e2 - 1, a.y1, e2 - 1, f2, 3, C_GRID_THICK);
}

/**
 * @brief Shows one of the file's puzzles (wraps around), and "3 / 42  -  Killer Hard".
 * @param[in] at  which
 */
static void pv_show(int at)
{
    int count = files_pv_count();
    if (count <= 0) {
        return;
    }
    s_pv_at = (at + count) % count;
    s_pv_shown = files_pv_parse(s_pv_at, &s_pv_var, &s_pv_lv, s_pv_givens);
    int n = s_pv_shown ? files_pv_geom()->n : 9;
    int side = n * (PV_BOARD / n);
    lv_obj_set_size(s_pv_board, side, side);
    lv_obj_set_x(s_pv_board, (720 - side) / 2);
    lv_obj_invalidate(s_pv_board);
    char t[96];
    snprintf(t, sizeof(t), "%d / %d  -  %s%s%s", s_pv_at + 1, count, variant_name(s_pv_var),
             s_pv_lv >= 0 ? " " : "", s_pv_lv >= 0 ? gen_level_name(s_pv_lv) : "");
    lv_label_set_text(s_pv_pos, t);
    lv_obj_set_state(s_pv_prev, LV_STATE_DISABLED, count < 2);
    lv_obj_set_state(s_pv_next, LV_STATE_DISABLED, count < 2);
}

/* (documented in ui_file_part.h) */
void file_preview_close(void)
{
    if (!s_pv) {
        return;
    }
    lv_obj_delete_async(s_pv);
    s_pv = NULL;
    files_pv_free();
}

/**
 * @brief The preview's Close.
 * @param[in] e  the event
 */
static void on_pv_close(lv_event_t *e) { file_preview_close(); }

/**
 * @brief The preview's \< \>: the puzzle before or after.
 * @param[in] e  the event (user data: -1 or 1)
 */
static void on_pv_step(lv_event_t *e) { pv_show(s_pv_at + (int)(intptr_t)lv_event_get_user_data(e)); }

/**
 * @brief The preview's Import: the file imported (stats_db_import_file(); the result comes back
 *        as a notice), the preview closed.
 * @param[in] e  the event
 */
static void on_pv_import(lv_event_t *e)
{
    stats_db_import_file(s_pv_path);                /* (the result comes back as a notice) */
    file_preview_close();
}

/* (documented in ui_file_part.h) */
void file_preview_open(int i)
{
    file_preview_close();
    const char *name = files_entry(i)->name;
    snprintf(s_pv_path, sizeof(s_pv_path), "%s/%s", files_path(), name);
    char summary[160];
    int found = files_pv_load(s_pv_path, summary, sizeof(summary));

    s_pv = lv_obj_create(lv_layer_top());           /* the whole screen: nothing under it takes taps */
    lv_obj_remove_style_all(s_pv);
    lv_obj_set_size(s_pv, 720, 720);
    lv_obj_set_style_bg_color(s_pv, C_BG, 0);
    lv_obj_set_style_bg_opa(s_pv, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_pv, true);
    lv_obj_t *t = file_ui_label(s_pv, &lv_font_montserrat_22, C_TEXT, name);
    lv_obj_set_width(t, 720 - 32);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(t, 16, 14);
    lv_obj_t *sm = file_ui_label(s_pv, &lv_font_montserrat_16, C_MUTED, summary);
    lv_obj_set_width(sm, 720 - 32);
    lv_label_set_long_mode(sm, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(sm, 16, 48);

    s_pv_board = lv_obj_create(s_pv);
    lv_obj_remove_style_all(s_pv_board);
    lv_obj_set_pos(s_pv_board, (720 - PV_BOARD) / 2, 84);
    lv_obj_set_size(s_pv_board, PV_BOARD, PV_BOARD);
    lv_obj_add_event_cb(s_pv_board, pv_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* under it: < 3 / 42 - Killer Hard > */
    s_pv_prev = file_ui_icon_btn(s_pv, LV_SYMBOL_LEFT, C_TEXT, on_pv_step, (void *)(intptr_t)-1);
    lv_obj_set_pos(s_pv_prev, (720 - PV_BOARD) / 2, 84 + PV_BOARD + 12);
    s_pv_next = file_ui_icon_btn(s_pv, LV_SYMBOL_RIGHT, C_TEXT, on_pv_step, (void *)(intptr_t)1);
    lv_obj_set_pos(s_pv_next, (720 + PV_BOARD) / 2 - 56, 84 + PV_BOARD + 12);
    lv_obj_set_style_opa(s_pv_prev, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_pv_next, LV_OPA_40, LV_STATE_DISABLED);
    s_pv_pos = file_ui_label(s_pv, &lv_font_montserrat_18, C_TEXT, "");
    lv_obj_set_size(s_pv_pos, PV_BOARD - 2 * 56 - 16, lv_font_get_line_height(&lv_font_montserrat_18));   /* one line */
    lv_obj_set_style_text_align(s_pv_pos, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_pv_pos, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s_pv_pos, (720 - PV_BOARD) / 2 + 56 + 8, 84 + PV_BOARD + 12 + 13);

    /* what Import does (or why it can't) */
    bool db = is_stats_db_ready(), clock = CLOCK_VALID(time(NULL));   /* (each collection is dated) */
    lv_obj_t *note = file_ui_label(s_pv, &lv_font_montserrat_14, C_MUTED,
                                   !db ? "Importing needs sudoku.db: insert the card before switching on."
                                   : !found ? "Nothing here to import."
                                   : !clock ? "Importing needs the clock: connect to Wi-Fi first (Settings > Network)."
                                   : "Import adds them to Puzzles > Imported as a collection named after the file (each checked for exactly "
                                   "one solution; more than 100 make several).");
    lv_obj_set_width(note, 720 - 32);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note, 16, 84 + PV_BOARD + 12 + 48 + 10);

    lv_obj_t *row = lv_obj_create(s_pv);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 720 - 32, 56);
    lv_obj_set_pos(row, 16, 720 - 16 - 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_t *cl = file_ui_pill_btn(row, LV_SYMBOL_CLOSE "  Close", C_CARD2, C_TEXT, on_pv_close);
    lv_obj_set_width(cl, 300);
    lv_obj_t *im = file_ui_pill_btn(row, LV_SYMBOL_DOWNLOAD "  Import", C_ACCENT, lv_color_white(), on_pv_import);
    lv_obj_set_width(im, 300);
    lv_obj_set_style_opa(im, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_state(im, LV_STATE_DISABLED, !db || !found || !clock);

    s_pv_shown = false;
    if (found) {
        pv_show(0);
    } else {
        lv_label_set_text(s_pv_pos, "No puzzles");
    }
}
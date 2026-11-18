#pragma once
/*
 * Colour schemes shared by the UI files. sudoku_ui.c owns the two palettes
 * and points ui_pal at the active one (Settings -> Dark mode).
 */
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t bg, card, card2, text, muted, accent, green, orange, red;
    uint32_t grid_thick, grid_thin;
    uint32_t cell, cell_given, cell_peer, cell_same, cell_sel, cell_bad, cell_solved, digit_user;
    uint32_t list_solved, list_active, notice, scrim_opa;
} palette_t;

extern const palette_t *ui_pal;

#define C_BG          lv_color_hex(ui_pal->bg)
#define C_CARD        lv_color_hex(ui_pal->card)
#define C_CARD2       lv_color_hex(ui_pal->card2)
#define C_TEXT        lv_color_hex(ui_pal->text)
#define C_MUTED       lv_color_hex(ui_pal->muted)
#define C_ACCENT      lv_color_hex(ui_pal->accent)
#define C_GREEN       lv_color_hex(ui_pal->green)
#define C_ORANGE      lv_color_hex(ui_pal->orange)
#define C_RED         lv_color_hex(ui_pal->red)

#define C_GRID_THICK  lv_color_hex(ui_pal->grid_thick)
#define C_GRID_THIN   lv_color_hex(ui_pal->grid_thin)
#define C_CELL        lv_color_hex(ui_pal->cell)
#define C_CELL_GIVEN  lv_color_hex(ui_pal->cell_given)
#define C_CELL_PEER   lv_color_hex(ui_pal->cell_peer)
#define C_CELL_SAME   lv_color_hex(ui_pal->cell_same)
#define C_CELL_SEL    lv_color_hex(ui_pal->cell_sel)
#define C_CELL_BAD    lv_color_hex(ui_pal->cell_bad)
#define C_CELL_SOLVED lv_color_hex(ui_pal->cell_solved)
#define C_DIGIT_USER  lv_color_hex(ui_pal->digit_user)

#define C_LIST_SOLVED lv_color_hex(ui_pal->list_solved)
#define C_LIST_ACTIVE lv_color_hex(ui_pal->list_active)
#define C_NOTICE      lv_color_hex(ui_pal->notice)

#ifdef __cplusplus
}
#endif

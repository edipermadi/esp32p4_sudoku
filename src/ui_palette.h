/**
 * @file ui_palette.h
 * @brief The colour schemes: what each colour of the palette is for.
 */
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A colour scheme (0xRRGGBB each). The UI owns the two, dark and light (ui_common.c);
 *        ui_palette() is the active one (Settings > Dark mode), with the chosen accent.
 */
typedef struct {
    uint32_t bg;                  /**< the screens' background */
    uint32_t card;                /**< cards, buttons, rows */
    uint32_t card2;               /**< a pressed card; a second level (dropdowns, chips) */
    uint32_t text;                /**< text */
    uint32_t muted;               /**< secondary text */
    uint32_t accent;              /**< the accent (Settings > Accent colour) */
    uint32_t green;               /**< solved, done, good */
    uint32_t orange;              /**< in progress, warnings */
    uint32_t red;                 /**< mistakes, danger */
    uint32_t grid_thick;          /**< the board's box lines */
    uint32_t grid_thin;           /**< the board's cell lines */
    uint32_t cell;                /**< an empty cell */
    uint32_t cell_given;          /**< a given's cell */
    uint32_t cell_peer;           /**< a cell that sees the selected one */
    uint32_t cell_same;           /**< a cell holding the selected digit */
    uint32_t cell_sel;            /**< the selected cell */
    uint32_t cell_bad;            /**< a cell breaking a rule */
    uint32_t cell_solved;         /**< the cells of a solved board */
    uint32_t digit_user;          /**< digits entered */
    uint32_t list_solved;         /**< Puzzles: a solved puzzle's number */
    uint32_t list_active;         /**< Puzzles: the game in progress */
    uint32_t notice;              /**< the notice banner */
    uint32_t scrim_opa;           /**< the dim behind dialogs, lv_opa_t */
} palette_t;

/**
 * @brief The active colour scheme (ui_common.c).
 * @return it
 */
const palette_t *ui_palette(void);

/** @name The active scheme's colours, as lv_color_t
 *  @{ */
#define C_BG          lv_color_hex(ui_palette()->bg)            /**< palette_t.bg */
#define C_CARD        lv_color_hex(ui_palette()->card)          /**< palette_t.card */
#define C_CARD2       lv_color_hex(ui_palette()->card2)         /**< palette_t.card2 */
#define C_TEXT        lv_color_hex(ui_palette()->text)          /**< palette_t.text */
#define C_MUTED       lv_color_hex(ui_palette()->muted)         /**< palette_t.muted */
#define C_ACCENT      lv_color_hex(ui_palette()->accent)        /**< palette_t.accent */
#define C_GREEN       lv_color_hex(ui_palette()->green)         /**< palette_t.green */
#define C_ORANGE      lv_color_hex(ui_palette()->orange)        /**< palette_t.orange */
#define C_RED         lv_color_hex(ui_palette()->red)           /**< palette_t.red */

#define C_GRID_THICK  lv_color_hex(ui_palette()->grid_thick)    /**< palette_t.grid_thick */
#define C_GRID_THIN   lv_color_hex(ui_palette()->grid_thin)     /**< palette_t.grid_thin */
#define C_CELL        lv_color_hex(ui_palette()->cell)          /**< palette_t.cell */
#define C_CELL_GIVEN  lv_color_hex(ui_palette()->cell_given)    /**< palette_t.cell_given */
#define C_CELL_PEER   lv_color_hex(ui_palette()->cell_peer)     /**< palette_t.cell_peer */
#define C_CELL_SAME   lv_color_hex(ui_palette()->cell_same)     /**< palette_t.cell_same */
#define C_CELL_SEL    lv_color_hex(ui_palette()->cell_sel)      /**< palette_t.cell_sel */
#define C_CELL_BAD    lv_color_hex(ui_palette()->cell_bad)      /**< palette_t.cell_bad */
#define C_CELL_SOLVED lv_color_hex(ui_palette()->cell_solved)   /**< palette_t.cell_solved */
#define C_DIGIT_USER  lv_color_hex(ui_palette()->digit_user)    /**< palette_t.digit_user */

#define C_LIST_SOLVED lv_color_hex(ui_palette()->list_solved)   /**< palette_t.list_solved */
#define C_LIST_ACTIVE lv_color_hex(ui_palette()->list_active)   /**< palette_t.list_active */
#define C_NOTICE      lv_color_hex(ui_palette()->notice)        /**< palette_t.notice */
/** @} */

#ifdef __cplusplus
}
#endif

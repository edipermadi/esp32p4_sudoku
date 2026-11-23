/**
 * @file ui_enter_puzzle.c
 * @ingroup ui_game
 * @brief Enter a puzzle: a Classic puzzle typed in from a book, checked for one solution, graded, saved and opened.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void ed_refresh(void);
static void ed_cell_cb(lv_event_t *e);
static void ed_digit_cb(lv_event_t *e);
static void ed_close(void);
static void ed_cancel_cb(lv_event_t *e);
static void ed_erase_cb(lv_event_t *e);
static void ed_clear_cb(lv_event_t *e);
static void ed_check_task(void *arg);
static void ed_check_done(lv_timer_t *t);
static void ed_play_cb(lv_event_t *e);

/* ------------------------------------------------------------- enter a puzzle by hand */
/**
 * @brief Enter (Puzzles screen): a blank 9x9 board and a 1-9 pad to type in a
 * Classic puzzle from a book or a newspaper. Check & play: it needs exactly
 * one solution (the solver counts them); then it's graded, saved as "My
 * puzzles #n" (sudoku.db, like an imported one; RAM only without a card)
 * and opened like any other.
 */
static lv_obj_t *scr_edit;                          /**< the Enter a puzzle screen (NULL: not built) */
static lv_obj_t *ed_cells[81];                      /**< its cells */
static lv_obj_t *ed_lbl[81];                        /**< their digits */
static lv_obj_t *ed_status;                         /**< "30 digits", a conflict, ... */
static uint8_t ed_val[81];              /**< the digits typed, 0 = empty */
static int ed_sel = -1;                 /**< the selected cell, -1: none */
static uint16_t s_own_ram_next = 1;                 /**< RAM-only numbering without a card */

/**
 * @brief Redraws the board typed in: digits, the selection, conflicts, the count.
 */
static void ed_refresh(void)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int filled = 0, bad = 0;
    for (int i = 0; i < 81; i++) {
        bool clash = false;
        for (int p = 0; ed_val[i] && p < G->npeers[i]; p++) {
            clash |= ed_val[G->peers[i][p]] == ed_val[i];
        }
        filled += ed_val[i] != 0;
        bad += clash;
        lv_color_t bg = i == ed_sel ? C_CELL_SEL : (ed_sel >= 0 && is_sudoku_geom_peer(G, i, ed_sel) ? C_CELL_PEER : C_CELL);
        if (clash) {
            bg = C_CELL_BAD;
        }
        lv_obj_set_style_bg_color(ed_cells[i], bg, 0);
        lv_obj_set_style_text_color(ed_lbl[i], clash ? C_RED : C_TEXT, 0);
        lv_label_set_text_static(ed_lbl[i], ui_digits()[ed_val[i]]);
    }
    if (bad) {
        lv_label_set_text_fmt(ed_status, LV_SYMBOL_WARNING "  %d digits break a rule", bad);
        lv_obj_set_style_text_color(ed_status, C_RED, 0);
    } else {
        lv_label_set_text_fmt(ed_status, "%d given%s  -  tap a cell, then a digit", filled, filled == 1 ? "" : "s");
        lv_obj_set_style_text_color(ed_status, C_MUTED, 0);
    }
}

/**
 * @brief A cell tapped: selected.
 * @param[in] e  the event (user data: the cell)
 */
static void ed_cell_cb(lv_event_t *e)
{
    ed_sel = (int)(intptr_t)lv_event_get_user_data(e);
    ed_refresh();
}

/**
 * @brief A digit tapped: into the selected cell (the same digit again clears it), then on to the next cell.
 * @param[in] e  the event (user data: the digit)
 */
static void ed_digit_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (ed_sel < 0) {
        return;
    }
    ed_val[ed_sel] = ed_val[ed_sel] == d ? 0 : (uint8_t)d;   /* the same digit again clears it */
    if (ed_val[ed_sel] && ed_sel < 80) {
        ed_sel++;    /* on to the next cell, as when typing */
    }
    ed_refresh();
}

/**
 * @brief Closes Enter a puzzle (back to Puzzles).
 */
static void ed_close(void)
{
    lv_screen_load(screen_list());
    if (scr_edit) {
        lv_obj_delete_async(scr_edit);
    }
    scr_edit = NULL;
}

/**
 * @brief Cancel.
 * @param[in] e  the event
 */
static void ed_cancel_cb(lv_event_t *e)
{
    ed_close();
}

/**
 * @brief Erase: the selected cell.
 * @param[in] e  the event
 */
static void ed_erase_cb(lv_event_t *e)
{
    if (ed_sel >= 0) {
        ed_val[ed_sel] = 0;
    }
    ed_refresh();
}

/**
 * @brief Clear: all of it.
 * @param[in] e  the event
 */
static void ed_clear_cb(lv_event_t *e)
{
    memset(ed_val, 0, sizeof(ed_val));
    ed_sel = 0;
    ed_refresh();
}

/* the check runs in its own task: the solver recurses a level per empty cell (the UI task's stack is small) */
static volatile int ed_check_state;                 /**< 0 idle, 1 checking, 2 done */
static volatile int ed_check_n;                     /**< the solutions found (at most 2) */
static volatile int ed_check_level;                 /**< its level (gen_level_t), -1: not one solution */
static uint8_t ed_check_cells[81];      /**< the puzzle being checked */
static lv_timer_t *ed_check_timer;      /**< waits for the check */
static TaskHandle_t ed_check_handle;                /**< its stack in PSRAM (internal RAM is short) */

/**
 * @brief The check (its own task): counts the solutions (up to 2), grades a unique puzzle, then waits to be deleted.
 * @param[in] arg  unused
 */
static void ed_check_task(void *arg)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int n = sudoku_count_solutions(G, ed_check_cells, 2);
    ed_check_level = n == 1 ? sudoku_grade(G, ed_check_cells) : -1;
    ed_check_n = n;
    ed_check_state = 2;
    vTaskSuspend(NULL);                             /* deleted by ed_check_done (a PSRAM-stack task can't delete itself) */
}

/**
 * @brief The check is done: the puzzle saved and opened, or why not.
 * @param[in] t  the timer
 */
static void ed_check_done(lv_timer_t *t)
{
    if (ed_check_state != 2 || eTaskGetState(ed_check_handle) != eSuspended) {
        return;
    }
    vTaskDeleteWithCaps(ed_check_handle);
    ed_check_handle = NULL;
    lv_timer_delete(ed_check_timer);
    ed_check_timer = NULL;
    ed_check_state = 0;
    if (!scr_edit) {
        return;    /* (cancelled meanwhile) */
    }
    ed_refresh();
    if (ed_check_n == 0) {
        sudoku_ui_notice("This puzzle has no solution: check the digits (red ones break a rule).", 4000);
        return;
    }
    if (ed_check_n > 1) {
        sudoku_ui_notice("This puzzle has more than one solution: some digits are missing.", 4000);
        return;
    }
    int level = ed_check_level;
    if (level < 0 || level >= GEN_LEVELS) {
        level = GEN_EXPERT;
    }
    int number = 0;
    int gid = is_stats_db_ready() ? stats_db_gen_add_own(ed_check_cells, level, &number) : -1;
    uint16_t id;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = gen_ram_take();                       /* no card: until power-off */
        number = s_own_ram_next++;
    }
    gen_cache_put(id, VAR_CLASSIC, level, ed_check_cells, NULL, NULL, NULL, NULL, NULL);
    gen_cache_source(id, STATS_OWN_SOURCE, number);
    char msg[96];
    snprintf(msg, sizeof(msg), "Saved as %s #%d: one solution, rated %s.", STATS_OWN_SOURCE, number,
             gen_level_name(level));
    sudoku_ui_notice(msg, 3500);
    if (scr_edit) {
        lv_obj_delete_async(scr_edit);
    }
    scr_edit = NULL;
    open_puzzle(id);
}

/**
 * @brief Check & play: the puzzle typed in is checked in the background.
 * @param[in] e  the event
 */
static void ed_play_cb(lv_event_t *e)
{
    if (ed_check_state) {
        return;
    }
    int filled = 0;
    for (int i = 0; i < 81; i++) {
        filled += ed_val[i] != 0;
    }
    if (filled < 17) {                              /* no Sudoku with fewer has just one solution */
        sudoku_ui_notice("A puzzle needs at least 17 digits to have just one solution.", 4000);
        return;
    }
    memcpy(ed_check_cells, ed_val, 81);
    ed_check_state = 1;
    if (xTaskCreatePinnedToCoreWithCaps(ed_check_task, "edcheck", 16384, NULL, 2, &ed_check_handle, WORKER_CORE,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        ed_check_state = 0;
        sudoku_ui_notice("Not enough memory to check the puzzle right now.", 3000);
        return;
    }
    lv_label_set_text(ed_status, "Checking...");
    lv_obj_set_style_text_color(ed_status, C_MUTED, 0);
    ed_check_timer = lv_timer_create(ed_check_done, 50, NULL);
}

/* (documented in ui_internal.h) */
void enter_cb(lv_event_t *e)
{
    memset(ed_val, 0, sizeof(ed_val));
    ed_sel = 0;
    scr_edit = make_screen();
    make_btn(scr_edit, 8, 6, 120, 48, LV_SYMBOL_CLOSE "  Cancel", &lv_font_montserrat_18, ed_cancel_cb, NULL);
    lv_obj_t *t = make_label(scr_edit, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(t, "Enter a puzzle");
    lv_obj_set_pos(t, 142, 6);
    ed_status = make_label(scr_edit, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(ed_status, 142, 36);
    lv_obj_t *go = make_btn(scr_edit, SCR_W - 8 - 220, TOOL_Y, 220, 48, LV_SYMBOL_PLAY "  Check & play",
                            &lv_font_montserrat_18, ed_play_cb, NULL);
    lv_obj_set_style_bg_color(go, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(go, 0), lv_color_white(), 0);
    lv_obj_t *er = make_btn(scr_edit, 8, TOOL_Y, 130, 48, LV_SYMBOL_BACKSPACE "  Erase", &lv_font_montserrat_16,
                            ed_erase_cb, NULL);
    lv_obj_set_style_bg_color(er, C_CARD2, 0);
    lv_obj_t *cl = make_btn(scr_edit, 8 + 130 + 8, TOOL_Y, 130, 48, LV_SYMBOL_TRASH "  Clear", &lv_font_montserrat_16,
                            ed_clear_cb, NULL);
    lv_obj_set_style_bg_color(cl, C_CARD2, 0);

    lv_obj_t *grid = lv_obj_create(scr_edit);
    lv_obj_remove_style_all(grid);
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_t *boxes[9];
    for (int b = 0; b < 9; b++) {
        boxes[b] = lv_obj_create(grid);
        lv_obj_remove_style_all(boxes[b]);
        lv_obj_set_style_bg_color(boxes[b], C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(boxes[b], LV_OPA_COVER, 0);
        lv_obj_set_scrollable(boxes[b], false);
        lv_obj_set_clickable(boxes[b], false);
    }
    for (int i = 0; i < 81; i++) {
        ed_cells[i] = lv_obj_create(grid);
        lv_obj_remove_style_all(ed_cells[i]);
        lv_obj_set_style_bg_opa(ed_cells[i], LV_OPA_COVER, 0);
        lv_obj_set_scrollable(ed_cells[i], false);
        lv_obj_set_clickable(ed_cells[i], true);
        lv_obj_add_event_cb(ed_cells[i], ed_cell_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        ed_lbl[i] = make_label(ed_cells[i], &font_digits_44, C_TEXT);
        lv_obj_center(ed_lbl[i]);
    }
    board_layout(sudoku_geom(VAR_CLASSIC), grid, boxes, ed_cells, NULL, GRID_X);
    const int span = GRID_SIZE - 6, h = (span - 8 * PAD_GAP) / 9;
    for (int d = 1; d <= 9; d++) {
        make_btn(scr_edit, PAD_X, GRID_Y + 1 + (d - 1) * (h + PAD_GAP), PAD_W, h, ui_digits()[d], &lv_font_montserrat_28,
                 ed_digit_cb, (void *)(intptr_t)d);
    }
    lv_obj_t *hint = make_label(scr_edit, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(hint, SCR_W - 16);
    lv_label_set_text(hint, "Type in a Classic puzzle's given digits, then Check & play: it needs exactly one solution.");
    lv_obj_set_pos(hint, 8, BOT_Y + (BOT_H - 18) / 2);
    ed_refresh();
    lv_screen_load(scr_edit);
}

/* ---- accessors: the other UI files reach this file's state through these */
lv_obj_t *screen_edit(void) { return scr_edit; }

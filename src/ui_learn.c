/**
 * @file ui_learn.c
 * @ingroup ui_game
 * @brief Learn (Puzzles > ⋮ > Learn): the lessons in tabs, the search for a lesson's puzzle, the practice game.
 *
 * Pick a lesson: a worker task looks for a position where it's the next step (learn_find(): a built-in puzzle
 * for a technique, a new one, made on the spot, for a variant's rule), and the game opens there as a practice
 * game with the hint card on it. Practice games aren't saved, recorded or counted; a variant lesson's puzzle is
 * kept in RAM only, as without a card.
 */
#include "ui_internal.h"
#include "learn_core.h"

static lv_obj_t *learn_box;             /**< the Learn dialog */
static lv_obj_t *learn_list;            /**< its lessons (the tab shown) */
static int learn_tab;                   /**< the tab shown (kept until power-off) */
static lv_obj_t *learn_wait;            /**< "Finding a puzzle..." */
static lv_timer_t *learn_timer;         /**< polls the search */
static volatile int learn_state;        /**< 0 searching, 1 found, 2 nothing found */
static int learn_target;                /**< the lesson asked for */
static learn_found_t learn_found;       /**< what the search found */
static sudoku_geom_t *learn_board;      /**< a variant lesson's board (PSRAM) */
static char *learn_cages;               /**< its cages, as text (PSRAM) */
static char *learn_thermos;             /**< its lines (PSRAM) */
static char learn_marks[82];            /**< its marks or clues */

/* (documented in ui_internal.h) */
bool is_learn_busy(void)
{
    return learn_timer != NULL;
}

/**
 * @brief The search (core 0): learn_state then says found or not.
 * @param[in] arg  unused
 */
static void learn_task(void *arg)
{
    learn_state = learn_find(learn_target, esp_random(), learn_board, &learn_found) ? 1 : 2;
    vTaskDelete(NULL);
}

/**
 * @brief The lesson's puzzle, at the position found, as a practice game with the hint card on the step.
 */
static void learn_open(void)
{
    const int var = learn_variant(learn_target);
    uint16_t id = learn_found.puzzle;
    if (var >= 0) {                                 /* made just now: kept in RAM only, as without a card */
        id = gen_ram_take();
        sudoku_layout_texts(learn_board, learn_cages, STATS_CAGES_LEN, learn_marks, learn_thermos, STATS_THERMOS_LEN);
        gen_cache_put(id, var, GEN_MEDIUM, learn_found.givens, is_jigsaw_variant(var) ? learn_board->region : NULL,
                      learn_cages[0] ? learn_cages : NULL, learn_marks[0] ? learn_marks : NULL,
                      learn_thermos[0] ? learn_thermos : NULL, NULL);
    }
    start_game(id, false);
    game_set_practice(learn_target);
    game_set_flags(0);                              /* (practice: never the no-notes challenge) */
    challenge_ui();
    uint8_t given[81];
    puzzle_get(id, given);
    for (int i = 0; i < 81; i++) {
        if (!given[i]) {
            game_put_value(i, learn_found.vals[i]);
        }
    }
    game_set_undo_len(0);
    game_set_undo_head(0);
    game_set_nmoves(0);
    compute_conflicts();
    update_header();
    hint_set_info(&learn_found.step);               /* the hint card on the lesson's step */
    hint_set_step(1);
    hint_render();
}

/**
 * @brief Waits for the search: then opens the lesson, or says none was found.
 * @param[in] t  the timer
 */
static void learn_poll_cb(lv_timer_t *t)
{
    if (learn_state == 0) {
        return;
    }
    lv_timer_delete(learn_timer);
    learn_timer = NULL;
    if (learn_wait) {
        lv_msgbox_close(learn_wait);
    }
    learn_wait = NULL;
    if (learn_state != 1) {
        sudoku_ui_notice("Couldn't find an example just now: try again.", 3000);
        return;
    }
    learn_open();
}

/**
 * @brief Closes the Learn dialog.
 */
static void learn_close(void)
{
    if (learn_box) {
        lv_msgbox_close_async(learn_box);
    }
    learn_box = NULL;
    learn_list = NULL;
}

/**
 * @brief A lesson picked: the search starts (on the worker core), with a dialog while it runs.
 * @param[in] e  the event (user data: the lesson)
 */
static void learn_pick_cb(lv_event_t *e)
{
    const int k = (int)(intptr_t)lv_event_get_user_data(e);
    learn_close();
    if (learn_timer || is_gen_busy()) {
        return;    /* (one search at a time; New puzzle or genbench has the generator) */
    }
    char text[120];
    if (learn_variant(k) >= 0) {
        if ((!learn_board && !(learn_board = heap_caps_malloc(sizeof(*learn_board), MALLOC_CAP_SPIRAM))) ||
                (!learn_cages && !(learn_cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) ||
                (!learn_thermos && !(learn_thermos = heap_caps_malloc(STATS_THERMOS_LEN, MALLOC_CAP_SPIRAM)))) {
            return;
        }
        snprintf(text, sizeof(text), "Making a %s puzzle where the next step uses its rule...",
                 variant_name(learn_variant(k)));
    } else {
        snprintf(text, sizeof(text), "Finding a puzzle where the next step is a %s...", learn_name(k));
    }
    learn_target = k;
    learn_state = 0;
    learn_wait = make_msgbox(LV_SYMBOL_EYE_OPEN "  Learn", text);
    /* 16 KB: the variant lessons run the generator (as New puzzle's task); 8 KB overflowed */
    if (xTaskCreatePinnedToCore(learn_task, "learn", 16384, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        lv_msgbox_close(learn_wait);
        learn_wait = NULL;
        return;
    }
    learn_timer = lv_timer_create(learn_poll_cb, 50, NULL);
}

/**
 * @brief The list: the tab's lessons, each its name over what it does, under a line about the tab.
 * @param[in] g  the tab, LEARN_TECHNIQUES .. LEARN_CLUES
 */
static void learn_fill(int g)
{
    learn_tab = g;
    lv_obj_clean(learn_list);
    lv_obj_scroll_to_y(learn_list, 0, LV_ANIM_OFF);
    make_wrap_text(learn_list, &lv_font_montserrat_16, C_MUTED, g == LEARN_TECHNIQUES
                   ? "Pick one: a built-in puzzle opens where it's the next step, with the hint showing where to "
                   "look. Practice games aren't saved or counted."
                   : "A variant's own rule: a new puzzle of that kind is made (a few seconds) and opens where its "
                   "rule gives the next step. Practice games aren't saved or counted.");
    for (int k = 0; k < learn_count(); k++) {
        if (learn_group(k) != g) {
            continue;
        }
        lv_obj_t *row = make_btn(learn_list, 0, 0, 0, 86, NULL, NULL, learn_pick_cb, (void *)(intptr_t)k);   /* 2 lines */
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_style_bg_color(row, C_CARD2, 0);
        lv_obj_set_style_margin_top(row, 8, 0);
        lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text(n, learn_name(k));
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 14, 8);
        lv_obj_t *d = make_label(row, &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_width(d, 580);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);   /* (the descriptions take up to two lines) */
        lv_label_set_text(d, learn_desc(k));
        lv_obj_align(d, LV_ALIGN_TOP_LEFT, 14, 36);
    }
}

/**
 * @brief Another tab tapped.
 * @param[in] e  the event (the tab strip)
 */
static void learn_tab_cb(lv_event_t *e)
{
    uint32_t sel = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (sel != LV_BUTTONMATRIX_BUTTON_NONE && learn_list) {
        learn_fill((int)sel);
    }
}

/**
 * @brief Its Close.
 * @param[in] e  the event
 */
static void learn_close_cb(lv_event_t *e)
{
    learn_close();
}

/* (documented in ui_internal.h) */
void learn_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Learn", NULL);
    learn_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    static const char *map[LEARN_GROUPS + 1];
    for (int g = 0; g < LEARN_GROUPS; g++) {
        map[g] = learn_group_name(g);
    }
    map[LEARN_GROUPS] = "";
    lv_obj_t *tabs = make_tab_strip(c, map, learn_tab);
    lv_obj_set_style_margin_top(tabs, 0, 0);
    lv_obj_add_event_cb(tabs, learn_tab_cb, LV_EVENT_VALUE_CHANGED, NULL);
    learn_list = lv_obj_create(c);                  /* the lessons: they scroll, under the tabs */
    lv_obj_remove_style_all(learn_list);
    lv_obj_set_size(learn_list, LV_PCT(100), 440);
    lv_obj_set_style_margin_top(learn_list, 10, 0);
    lv_obj_set_flex_flow(learn_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(learn_list, LV_DIR_VER);
    learn_fill(learn_tab);
    msgbox_button(m, "Close", learn_close_cb, false);
}

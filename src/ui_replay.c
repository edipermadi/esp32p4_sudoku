/**
 * @file ui_replay.c
 * @ingroup ui_replay
 * @brief The Replay screen: a move log played back on the board, with start / back / play / forward /
 *        end, the speed and a slider; opened from Gameplays, History or the Solved dialog. The
 *        timeline: replay_timeline.h; an export as an MP4: ui_replay_export.c.
 */
#include "ui_internal.h"
#include "ui_replay_part.h"
#include "gameplays_core.h"

static lv_obj_t *scr_replay;                        /**< the screen (NULL: not open) */
static lv_obj_t *rp_return;                         /**< where Back goes */
static lv_obj_t *rp_cells[81];                      /**< the board's cells */
static lv_obj_t *rp_lbl[81];                        /**< their digits */
static sudoku_geom_t *rp_geo_buf;                   /**< a replayed Jigsaw's regions (PSRAM) */
static lv_obj_t *rp_play_lbl;                       /**< play / pause */
static lv_obj_t *rp_speed_lbl;                      /**< "Speed 2x" */
static lv_obj_t *rp_slider;                         /**< the step, dragged */
static lv_obj_t *rp_pos;                            /**< "Move 12 / 40   01:10 / 05:32" */
static lv_obj_t *rp_title;                          /**< "Replay  -  Classic Easy #12" */
static lv_obj_t *rp_sub;                            /**< the line under it */
static lv_obj_t *rp_shot;                           /**< the camera (hidden in exported videos) */
static lv_timer_t *rp_timer;                        /**< plays it on (40 ms) */
static uint32_t rp_last_tick;                       /**< its last run */
static stats_replay_t *rp_loaded;                   /**< a solve from sudoku.db (Gameplays, History; PSRAM) */

/* (documented in ui_replay_part.h) */
bool replay_screen_load(uint16_t id, const game_move_t *moves, int n)
{
    uint8_t given[81];
    if (!puzzle_get(id, given)) {
        return false;
    }
    if (!rp_geo_buf) {
        rp_geo_buf = heap_caps_malloc(sizeof(*rp_geo_buf), MALLOC_CAP_SPIRAM);
    }
    return replay_prepare(given, puzzle_geom(id, rp_geo_buf, NULL), moves, n);
}

/* (documented in ui_replay_part.h) */
void replay_screen_render(void)
{
    uint8_t val[81], kind[81];
    bool bad[81];
    int last = replay_board(val, kind, bad);
    const sudoku_geom_t *geo = replay_geom();
    for (int i = 0; i < 81; i++) {
        if (!geo->used[i]) {
            continue;
        }
        uint8_t given = replay_given(i);
        lv_color_t bg = cell_base_bg(geo, i, given);
        if (i == last) {
            bg = C_CELL_SEL;
        }
        lv_obj_set_style_bg_color(rp_cells[i], bg, 0);
        lv_color_t fg = given ? C_TEXT : (kind[i] == MOVE_FILL ? C_ORANGE : C_DIGIT_USER);
        if (bad[i]) {
            fg = C_RED;
        }
        lv_obj_set_style_text_color(rp_lbl[i], fg, 0);
        lv_label_set_text_static(rp_lbl[i], ui_digits()[val[i] <= 9 ? val[i] : 0]);
    }
    int k = replay_k(), n = replay_n();
    char now[16], total[16];
    fmt_time(now, sizeof(now), replay_ms_at(k) / 1000);
    fmt_time(total, sizeof(total), replay_ms_at(n) / 1000);
    lv_label_set_text_fmt(rp_pos, "Move %d / %d   %s / %s", k, n, now, total);
    lv_slider_set_value(rp_slider, k, LV_ANIM_OFF);
    lv_label_set_text(rp_play_lbl, is_replay_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

/* (documented in ui_replay_part.h) */
lv_obj_t *replay_screen_camera(void)
{
    return rp_shot;
}

/**
 * @brief Goes to a step and draws it.
 * @param k             the step (clamped)
 * @param keep_playing  false: stops
 */
static void rp_seek(int k, bool keep_playing)
{
    replay_seek(k, keep_playing);
    replay_screen_render();
}

/**
 * @brief The 40 ms timer: plays on by the time since its last run.
 * @param t  the timer
 */
static void rp_timer_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get(), dt = now - rp_last_tick;
    rp_last_tick = now;
    if (replay_tick(dt)) {
        replay_screen_render();
    }
}

/**
 * @brief The controls: start, one back, play / pause, one forward, end, speed.
 * @param e  the event (user data: 0..5)
 */
static void rp_btn_cb(lv_event_t *e)
{
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0:
        rp_seek(0, false);
        break;                         /* start */
    case 1:
        rp_seek(replay_k() - 1, false);
        break;                  /* one back */
    case 2:                                                   /* play / pause */
        replay_toggle();
        rp_last_tick = lv_tick_get();
        replay_screen_render();
        break;
    case 3:
        rp_seek(replay_k() + 1, false);
        break;                  /* one forward */
    case 4:
        rp_seek(replay_n(), false);
        break;                      /* end */
    case 5:                                                   /* speed */
        replay_next_speed();
        lv_label_set_text_fmt(rp_speed_lbl, LV_SYMBOL_PLAY "  Speed %ux", (unsigned)replay_speed_x());
        break;
    }
}

/**
 * @brief The slider: that step (playing on if it was).
 * @param e  the event
 */
static void rp_slider_cb(lv_event_t *e)
{
    rp_seek(lv_slider_get_value(rp_slider), is_replay_playing());
}

/** @brief Closes the screen: back where it was opened from. */
static void rp_close(void)
{
    if (rp_timer) {
        lv_timer_delete(rp_timer);
    }
    rp_timer = NULL;
    lv_screen_load(rp_return ? rp_return : screen_home());
    lv_obj_delete_async(scr_replay);
    scr_replay = NULL;
}

/**
 * @brief Back.
 * @param e  the event
 */
static void rp_back_cb(lv_event_t *e)
{
    rp_close();
}

/* (documented in ui_replay_part.h) */
lv_obj_t *replay_screen_build(uint16_t id, const char *sub, const char *player)
{
    lv_obj_t *scr = make_screen();
    make_btn(scr, 8, 6, 104, 48, LV_SYMBOL_LEFT "  Back", &lv_font_montserrat_18, rp_back_cb, NULL);
    char name[40];
    puzzle_name(id, name, sizeof(name));
    rp_title = make_label(scr, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text_fmt(rp_title, "Replay  -  %s", name);
    lv_obj_set_pos(rp_title, 126, 6);
    rp_sub = make_label(scr, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(rp_sub, sub);
    const stats_player_t *who = player_find(0, player);
    if (who && who->avatar && who->avatar <= AVATAR_COUNT) {   /* whose game: their avatar first */
        lv_obj_set_pos(player_badge(scr, who, -1, 22, 8), 126, 32);
        lv_obj_set_pos(rp_sub, 126 + 22 + 6, 36);
    } else {
        lv_obj_set_pos(rp_sub, 126, 36);
    }

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_add_event_cb(grid, board_regions_draw_cb, LV_EVENT_DRAW_POST_END, NULL);   /* Jigsaw borders */
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_t *boxes[9];
    for (int b = 0; b < 9; b++) {
        lv_obj_t *box = lv_obj_create(grid);
        lv_obj_remove_style_all(box);
        lv_obj_set_style_bg_color(box, C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(box, false);
        boxes[b] = box;
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(cell, false);
        rp_cells[i] = cell;
        rp_lbl[i] = make_label(cell, DIGIT_FONT_N(replay_geom()->n), C_TEXT);
        lv_obj_center(rp_lbl[i]);
    }
    lv_obj_t *clues[BOARD_CLUES];
    for (int i = 0; i < BOARD_CLUES; i++) {
        clues[i] = make_label(scr, &lv_font_montserrat_20, C_ACCENT);
        lv_obj_set_style_text_align(clues[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    board_layout(replay_geom(), grid, boxes, rp_cells, clues, GRID_X);

    /* second row (as the game's): where the replay is on the left, the speed on the right */
    rp_pos = make_label(scr, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(rp_pos, SCR_W - 8 - 200 - 8 - 16 - 64);
    rp_shot = make_btn(scr, SCR_W - 8 - 200 - 8 - 56, TOOL_Y, 56, 48, SYMBOL_CAMERA, &font_icons_20,
                       screenshot_cb, NULL);       /* a screenshot of this moment */
    lv_label_set_long_mode(rp_pos, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(rp_pos, 16, TOOL_Y + (48 - 24) / 2);
    lv_obj_t *sp = make_btn(scr, SCR_W - 8 - 200, TOOL_Y, 200, 48, NULL, &lv_font_montserrat_20, rp_btn_cb,
                            (void *)(intptr_t)5);
    rp_speed_lbl = make_label(sp, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_center(rp_speed_lbl);
    lv_label_set_text_fmt(rp_speed_lbl, LV_SYMBOL_PLAY "  Speed %ux", (unsigned)replay_speed_x());

    /* controls down the right, as tall as the board: start, back, play, forward, end */
    static const char *const SYM[] = {LV_SYMBOL_PREV, LV_SYMBOL_LEFT, LV_SYMBOL_PLAY, LV_SYMBOL_RIGHT,
                                      LV_SYMBOL_NEXT
                                     };
    const int gap = 8, bh = (GRID_SIZE - 4 * gap) / 5;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = make_btn(scr, PAD_X, GRID_Y + i * (bh + gap), PAD_W, bh, SYM[i],
                               &lv_font_montserrat_28, rp_btn_cb, (void *)(intptr_t)i);
        if (i == 2) {
            lv_obj_set_style_bg_color(b, C_ACCENT, 0);
            rp_play_lbl = lv_obj_get_child(b, 0);
            lv_obj_set_style_text_color(rp_play_lbl, lv_color_white(), 0);
        }
    }

    /* bottom: the slider, the full width */
    rp_slider = lv_slider_create(scr);
    lv_obj_set_size(rp_slider, SCR_W - 64, 14);
    lv_obj_set_pos(rp_slider, 32, BOT_Y + (BOT_H - 14) / 2);
    lv_slider_set_range(rp_slider, 0, replay_n() > 0 ? replay_n() : 1);
    lv_obj_set_style_bg_color(rp_slider, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_ext_click_area(rp_slider, 16);
    lv_obj_add_event_cb(rp_slider, rp_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return scr;
}

/**
 * @brief Opens the Replay screen for a log of a puzzle.
 * @param id      the puzzle
 * @param moves   the log
 * @param n       its length
 * @param sub     the line under the title
 * @param player  whose game (their avatar goes first)
 */
void open_replay(uint16_t id, const game_move_t *moves, int n, const char *sub, const char *player)
{
    if (!replay_screen_load(id, moves, n)) {
        return;
    }
    if (scr_replay) {
        rp_close();
    }
    rp_return = lv_screen_active();
    scr_replay = replay_screen_build(id, sub, player);
    rp_seek(0, false);
    rp_last_tick = lv_tick_get();
    rp_timer = lv_timer_create(rp_timer_cb, 40, NULL);
    lv_screen_load(scr_replay);
    if (n == 0) {
        sudoku_ui_notice("No moves were recorded for this game.", 3000);
    }
}

/** @brief A solve sudoku.db answered with (Gameplays, History): its replay shown, or exported as an MP4. */
void history_replay_poll(void)
{
    if (!rp_loaded) {
        rp_loaded = heap_caps_malloc(sizeof(*rp_loaded), MALLOC_CAP_SPIRAM);
    }
    if (!rp_loaded || !stats_db_take_replay(rp_loaded)) {
        return;
    }
    lv_obj_t *act = lv_screen_active();
    if (act != screen_history() && act != screen_plays()) {
        return;    /* left meanwhile */
    }
    if (is_replay_busy()) {
        return;
    }
    if (!rp_loaded->found) {
        sudoku_ui_notice("This solve has no recorded moves.", 3000);
        return;
    }
    if (is_replay_want_export()) {
        replay_set_want_export(false);
        replay_export_start(rp_loaded);
        return;
    }
    char t[16], sub[96];
    fmt_time(t, sizeof(t), rp_loaded->seconds);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             rp_loaded->player, t, (unsigned)rp_loaded->hints, rp_loaded->hints == 1 ? "" : "s");
    open_replay(rp_loaded->puzzle, rp_loaded->moves, rp_loaded->n, sub, rp_loaded->player);
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @brief The Replay screen. @return it (NULL: not open) */
lv_obj_t *screen_replay(void) { return scr_replay; }

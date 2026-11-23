/**
 * @file ui_tour.c
 * @ingroup ui_game
 * @brief The game screen's tour: the first time a game opens, each part pointed out in turn.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void tour_area(int step, lv_area_t *a);
static void tour_end(void);
static void tour_next_cb(lv_event_t *e);
static void tour_skip_cb(lv_event_t *e);
static void tour_dim(int x1, int y1, int x2, int y2);
static void tour_show(int step);
static void tour_check_cb(lv_timer_t *t);

static lv_obj_t *tour_ovl;        /**< the game screen's tour, while it's up */
static int tour_step;                    /**< the tour's step shown */

/**
 * @brief The screen area a tour step lights up.
 * @param[in] step  the step
 * @param[out] a  gets it
 */
static void tour_area(int step, lv_area_t *a)
{
    lv_obj_t *o[4] = {NULL};
    switch (step) {
    case 0:
        o[0] = game_grid();
        break;
    case 1:
        o[0] = game_pad()[0];
        o[1] = game_pad()[8];
        break;
    case 2:
        o[0] = game_undo_btn();
        o[1] = game_fill_btn();
        break;
    case 3:
        o[0] = game_hint_btn();
        break;
    case 4:
        o[0] = game_bulb_btn();
        break;                       /* the ⋮ menu */
    default:
        o[0] = game_timer_btn();
        break;
    }
    lv_obj_get_coords(o[0], a);
    for (int i = 1; i < 4 && o[i]; i++) {
        lv_area_t b;
        lv_obj_get_coords(o[i], &b);
        if (b.x1 < a->x1) {
            a->x1 = b.x1;
        }
        if (b.y1 < a->y1) {
            a->y1 = b.y1;
        }
        if (b.x2 > a->x2) {
            a->x2 = b.x2;
        }
        if (b.y2 > a->y2) {
            a->y2 = b.y2;
        }
    }
    lv_area_increase(a, 4, 4);
}

/**
 * @brief Ends the tour (the clock runs again).
 */
static void tour_end(void)
{
    if (!tour_ovl) {
        return;
    }
    lv_obj_delete(tour_ovl);
    tour_ovl = NULL;
    stats_db_set_seen("tour", true);
    game_set_last_tick(lv_tick_get());                    /* (the clock goes on from now) */
}

/**
 * @brief Next (the last step ends the tour).
 * @param[in] e  the event
 */
static void tour_next_cb(lv_event_t *e)
{
    if (tour_step + 1 >= TOUR_STEPS) {
        tour_end();
    } else {
        tour_show(tour_step + 1);
    }
}

/**
 * @brief Skip: ends the tour.
 * @param[in] e  the event
 */
static void tour_skip_cb(lv_event_t *e)
{
    tour_end();
}

/**
 * @brief Dims a rectangle of the screen (the tour's cover around what is lit).
 * @param[in] x1  left, px
 * @param[in] y1  top
 * @param[in] x2  right
 * @param[in] y2  bottom
 */
static void tour_dim(int x1, int y1, int x2, int y2)
{
    if (x2 <= x1 || y2 <= y1) {
        return;
    }
    lv_obj_t *r = lv_obj_create(tour_ovl);
    lv_obj_remove_style_all(r);
    lv_obj_set_pos(r, x1, y1);
    lv_obj_set_size(r, x2 - x1, y2 - y1);
    lv_obj_set_style_bg_color(r, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_60, 0);
    lv_obj_set_clickable(r, false);
}

/**
 * @brief Shows a tour step: what it lights up, its title and text.
 * @param[in] step  the step
 */
static void tour_show(int step)
{
    static const char *const TITLE[TOUR_STEPS] = {"The board", "The number pad", "Tools", "Hint", "The top bar",
                                                  "The timer"
                                                 };
    static const char *const TEXT[TOUR_STEPS] = {
        "Tap a cell, then a digit to put it there. Hold a cell to paint it a colour (in Killer: to see what its "
        "cage can hold).",
        "Puts the digit in the selected cell; the small number is how many are still to place. Hold a digit to "
        "light it up everywhere, then tap cells to place it.",
        "Undo and Redo; Erase a cell; Notes puts digits in as pencil marks; Fill puts the right digit in the "
        "selected cell (it counts as a hint).",
        "Shows where to look next. More then shows the cell and why, and Place it puts the digit in.",
        "The menu: this puzzle's Rules, Share (the puzzle as a QR code), Star (keeps it under Puzzles > "
        "Starred), Auto notes (all the pencil marks at once) and Restart. List goes back to the puzzles: the "
        "game is saved after every move.",
        "Tap the timer to pause: the board is hidden until you go on. That's all - enjoy the puzzle!",
    };
    tour_step = step;
    if (!tour_ovl) {
        tour_ovl = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(tour_ovl);
        lv_obj_set_size(tour_ovl, SCR_W, SCR_W);
        lv_obj_set_clickable(tour_ovl, true);   /* nothing under it takes a tap */
        lv_obj_set_scrollable(tour_ovl, false);
    }
    lv_obj_clean(tour_ovl);
    lv_area_t a;
    tour_area(step, &a);
    tour_dim(0, 0, SCR_W, a.y1);                    /* everything but the target, dimmed */
    tour_dim(0, a.y2 + 1, SCR_W, SCR_W);
    tour_dim(0, a.y1, a.x1, a.y2 + 1);
    tour_dim(a.x2 + 1, a.y1, SCR_W, a.y2 + 1);
    lv_obj_t *ring = lv_obj_create(tour_ovl);
    lv_obj_remove_style_all(ring);
    lv_obj_set_pos(ring, a.x1, a.y1);
    lv_obj_set_size(ring, lv_area_get_width(&a), lv_area_get_height(&a));
    lv_obj_set_style_border_color(ring, C_ACCENT, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_radius(ring, 8, 0);
    lv_obj_set_clickable(ring, false);

    lv_obj_t *card = lv_obj_create(tour_ovl);
    lv_obj_set_width(card, 560);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text_fmt(t, "%s", TITLE[step]);
    const char *text = TEXT[step];
    if (step == 3 && is_store_hint_nudge()) {
        text = "Shows where to look next (Settings > Game > Hints: Nudge only).";
    }
    lv_obj_t *x = make_wrap_text(card, &lv_font_montserrat_18, C_TEXT, text);
    lv_obj_set_width(x, LV_PCT(100));
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 52);
    lv_obj_t *n = make_label(row, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text_fmt(n, "%d of %d", step + 1, TOUR_STEPS);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *nb = make_btn(row, 0, 0, 140, 52, step + 1 < TOUR_STEPS ? "Next  " LV_SYMBOL_RIGHT : "Done",
                            &lv_font_montserrat_18, tour_next_cb, NULL);
    lv_obj_set_style_bg_color(nb, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(nb, 0), lv_color_white(), 0);
    lv_obj_align(nb, LV_ALIGN_RIGHT_MID, 0, 0);
    if (step + 1 < TOUR_STEPS) {
        lv_obj_t *sb = make_btn(row, 0, 0, 110, 52, "Skip", &lv_font_montserrat_18, tour_skip_cb, NULL);
        lv_obj_set_style_bg_color(sb, C_CARD2, 0);
        lv_obj_align(sb, LV_ALIGN_RIGHT_MID, -150, 0);
    }
    lv_obj_update_layout(card);
    int h = lv_obj_get_height(card), y;
    if (a.y1 >= h + 16) {
        y = a.y1 - h - 10;    /* above the target, or below it, or over its lower part */
    } else if (SCR_W - a.y2 >= h + 16) {
        y = a.y2 + 10;
    } else {
        y = SCR_W - h - 80;
    }
    lv_obj_set_pos(card, (SCR_W - 560) / 2, y);
}

/**
 * @brief Starts the tour on the game screen, once per player (not in Learn, a time attack, or a solved game).
 * @param[in] t  the timer
 */
static void tour_check_cb(lv_timer_t *t)
{
    if (tour_ovl || lv_screen_active() != screen_game() || game_practice() >= 0 || is_game_attack() || is_game_solved() || !is_stats_db_ready()) {
        return;
    }
    lv_obj_t *top = lv_layer_top();                 /* the rules card, a notice: another time */
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        if (!lv_obj_is_hidden(lv_obj_get_child(top, (int32_t)i))) {
            return;
        }
    }
    if (is_stats_db_seen("tour")) {
        return;
    }
    hint_drop();
    tour_show(0);
}

/* (documented in ui_internal.h) */
void tour_maybe(void)
{
    lv_timer_t *t = lv_timer_create(tour_check_cb, 500, NULL);   /* (after Learn / Time attack set their flags) */
    lv_timer_set_repeat_count(t, 1);
}

/* (documented in ui_internal.h) */
bool is_tour_open(void)
{
    return tour_ovl != NULL;
}

/**
 * @file ui_gameplays.c
 * @ingroup ui_replay
 * @brief Gameplays (Home): every solve with a move log, newest first, a page of STATS_MAX_DAY_ROWS at
 *        a time from sudoku.db. Tap one to replay it (the Replay screen; Back returns here), or
 *        MP4 to save it as a video. The pages: gameplays_list.h.
 */
#include "ui_internal.h"
#include "gameplays_core.h"

static void plays_hint(const char *text);
static void plays_row_cb(lv_event_t *e);
static void plays_export_cb(lv_event_t *e);
static void plays_pager_update(void);
static void render_plays(const stats_plays_t *p);
static void plays_page_cb(lv_event_t *e);

static lv_obj_t *scr_plays;               /**< the screen */
static lv_obj_t *pl_list;                 /**< the rows */
static lv_obj_t *pl_count;                /**< "1-40 of 120" */
static lv_obj_t *pl_newer;                /**< Newer (the page before) */
static lv_obj_t *pl_older;                /**< Older (the page after) */

/**
 * @brief Replaces the list with a line of text (loading, nothing yet, why not).
 * @param[in] text  the text
 */
static void plays_hint(const char *text)
{
    lv_obj_clean(pl_list);
    lv_obj_t *l = make_label(pl_list, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(l, text);
}

/**
 * @brief A row: its replay.
 * @param[in] e  the event (user data: the row)
 */
static void plays_row_cb(lv_event_t *e)
{
    gameplays_open((int)(intptr_t)lv_event_get_user_data(e), false);
}

/**
 * @brief A row's MP4: the solve exported as a video.
 * @param[in] e  the event (user data: the row)
 */
static void plays_export_cb(lv_event_t *e)
{
    gameplays_open((int)(intptr_t)lv_event_get_user_data(e), true);
}

/** @brief Newer and Older: shown when there's more than a page, greyed out at either end. */
static void plays_pager_update(void)
{
    bool newer = is_gameplays_newer(), older = is_gameplays_older();
    lv_obj_set_state(pl_newer, LV_STATE_DISABLED, !newer);
    lv_obj_set_state(pl_older, LV_STATE_DISABLED, !older);
    lv_obj_set_hidden(pl_newer, !newer && !older);
    lv_obj_set_hidden(pl_older, !newer && !older);
}

/**
 * @brief Shows a page: the count, a card per solve (who, the puzzle, when, the time, mistakes,
 *        hints, no notes) with its replay and MP4 buttons.
 * @param[in] p  the page
 */
static void render_plays(const stats_plays_t *p)
{
    if (p->total == 0) {
        lv_label_set_text(pl_count, "");
        plays_hint("No recorded games yet. Solve a puzzle and it shows up here, ready to replay.");
    } else {
        lv_label_set_text_fmt(pl_count, "%d-%d of %d", p->offset + 1, p->offset + p->n, p->total);
        lv_obj_clean(pl_list);
    }
    for (int i = 0; i < p->n; i++) {
        const stats_day_row_t *r = &p->rows[i];
        lv_obj_t *card = lv_obj_create(pl_list);
        lv_obj_set_size(card, LV_PCT(100), 76);
        lv_obj_set_style_bg_color(card, C_CARD, 0);
        lv_obj_set_style_bg_color(card, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_hor(card, 16, 0);
        lv_obj_set_scrollable(card, false);
        lv_obj_add_event_cb(card, plays_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        char name[40], took[16], when[40], line[128];
        puzzle_name(r->puzzle, name, sizeof(name));
        fmt_time(took, sizeof(took), r->seconds);
        if (r->finished_at) {
            time_t tt = (time_t)r->finished_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%a %d %b %Y, %H:%M", &tm);
        } else {
            snprintf(when, sizeof(when), "Date unknown");
        }
        lv_obj_align(player_badge(card, player_find(0, r->player), -1, AVATAR_SIZE, 12), LV_ALIGN_LEFT_MID, -4, 0);
        lv_obj_t *who = make_label(card, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(who, "%s   -   %s", r->player, name);
        lv_obj_align(who, LV_ALIGN_LEFT_MID, 44, -13);
        lv_obj_t *what = make_label(card, &lv_font_montserrat_14, C_MUTED);
        snprintf(line, sizeof(line), "%s   -   %s   -   %u mistake%s", when, took,
                 (unsigned)r->mistakes, r->mistakes == 1 ? "" : "s");
        if (r->hints) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   %u hint%s", (unsigned)r->hints, r->hints == 1 ? "" : "s");
        }
        if (r->flags & GAME_FLAG_NO_NOTES) {
            size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, "   -   no notes");
        }
        lv_label_set_text(what, line);
        lv_obj_align(what, LV_ALIGN_LEFT_MID, 44, 14);
        lv_obj_t *play = make_label(card, &lv_font_montserrat_24, C_ACCENT);
        lv_label_set_text(play, LV_SYMBOL_PLAY);
        lv_obj_align(play, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *mp4 = make_btn(card, 0, 0, 96, 52, LV_SYMBOL_DOWNLOAD "  MP4", &lv_font_montserrat_16,
                                 plays_export_cb, (void *)(intptr_t)i);   /* save as a video */
        lv_obj_set_style_bg_color(mp4, C_CARD2, 0);
        lv_obj_align(mp4, LV_ALIGN_RIGHT_MID, -44, 0);
    }
    lv_obj_scroll_to_y(pl_list, 0, LV_ANIM_OFF);
    plays_pager_update();
}

/**
 * @brief Asks for a page (without sudoku.db: why not).
 * @param[in] offset  its first row
 */
void plays_ask(int offset)
{
    if (!gameplays_ask(offset)) {
        lv_label_set_text(pl_count, "");
        plays_hint("No sudoku.db: insert a FAT32 microSD card before switching on to keep recorded games.");
        plays_pager_update();
    }
}

/**
 * @brief Newer / Older: the page before or after.
 * @param[in] e  the event (user data: -1 newer, +1 older)
 */
static void plays_page_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);   /* -1 newer, +1 older */
    plays_ask(gameplays_offset() + dir * STATS_MAX_DAY_ROWS);
}

/** @brief sudoku.db's answer, a page, shown (from the UI's poll). */
void plays_poll(void)
{
    const stats_plays_t *p = gameplays_take();
    if (p) {
        render_plays(p);
    }
}

/** @brief Builds the screen: Home, the title, the count, the list, Newer and Older. */
void build_plays(void)
{
    scr_plays = make_screen();
    make_btn(scr_plays, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_plays, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Gameplays");
    lv_obj_set_pos(t, 150, 20);
    pl_count = make_label(scr_plays, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(pl_count, LV_ALIGN_TOP_RIGHT, -16, 28);
    lv_label_set_text(pl_count, "");

    pl_list = lv_obj_create(scr_plays);
    lv_obj_remove_style_all(pl_list);
    lv_obj_set_pos(pl_list, 12, 74);
    lv_obj_set_size(pl_list, SCR_W - 24, SCR_W - 74 - 72);
    lv_obj_set_flex_flow(pl_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pl_list, 8, 0);
    lv_obj_set_scroll_dir(pl_list, LV_DIR_VER);

    pl_newer = make_btn(scr_plays, 12, SCR_W - 62, 200, 52, LV_SYMBOL_LEFT "  Newer", &lv_font_montserrat_18,
                        plays_page_cb, (void *)(intptr_t)-1);
    pl_older = make_btn(scr_plays, SCR_W - 12 - 200, SCR_W - 62, 200, 52, "Older  " LV_SYMBOL_RIGHT,
                        &lv_font_montserrat_18, plays_page_cb, (void *)(intptr_t)1);
    lv_obj_set_style_text_opa(lv_obj_get_child(pl_newer, 0), LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(lv_obj_get_child(pl_older, 0), LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_hidden(pl_newer, true);
    lv_obj_set_hidden(pl_older, true);
    plays_hint("Loading...");
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @brief The Gameplays screen. @return it (NULL before build_plays()) */
lv_obj_t *screen_plays(void) { return scr_plays; }

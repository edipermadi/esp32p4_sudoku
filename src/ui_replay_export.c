/**
 * @file ui_replay_export.c
 * @ingroup ui_replay
 * @brief A replay exported as an MP4: the Replay screen built on a second, invisible LVGL display that
 *        renders into the video's frame buffer, stepped frame by frame, with a progress dialog on the
 *        real screen. The file and the encoder: replay_video.h.
 */
#include "ui_internal.h"
#include "ui_replay_part.h"
#include "gameplays_core.h"

static lv_display_t *rx_disp;           /**< the invisible display (renders into replay_video_fb()) */
static lv_timer_t *rx_timer;            /**< encodes the next frames */
static int rx_last_k;                   /**< the step drawn last */
static bool rx_last_playing;            /**< whether it played then */
static lv_obj_t *rx_ovl;                /**< the progress dialog's layer */
static lv_obj_t *rx_bar;                /**< its bar */
static lv_obj_t *rx_lbl;                /**< "Frame 120 of 600   -   1.2 MB" */

/**
 * @brief The invisible display's flush: nothing to send (the picture stays in the frame buffer).
 * @param d     the display
 * @param area  the area drawn
 * @param px    its pixels
 */
static void rx_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    lv_display_flush_ready(d);                      /* the picture stays in rx_fb */
}

/**
 * @brief The progress dialog's Cancel: the next step ends it.
 * @param e  the event
 */
static void rx_cancel_cb(lv_event_t *e)
{
    replay_video_cancel();
}

/**
 * @brief Ends the export: the file kept (or deleted), the display and dialog gone, and a notice.
 * @param ok  it was made to the end
 */
static void rx_finish(bool ok)
{
    if (rx_timer) {
        lv_timer_delete(rx_timer);
    }
    rx_timer = NULL;
    uint32_t bytes = replay_video_bytes();
    ok = replay_video_end(ok);
    if (rx_disp) {
        lv_display_delete(rx_disp);    /* and the replay screen built on it */
    }
    rx_disp = NULL;
    if (rx_ovl) {
        lv_obj_delete(rx_ovl);
    }
    rx_ovl = NULL;
    replay_video_free();
    char msg[200];
    replay_video_result(ok, bytes, msg, sizeof(msg));
    sudoku_ui_notice(msg, ok ? 6000 : 4000);
}

/**
 * @brief The export's timer: draws and encodes frames for up to 30 ms a run (the progress dialog
 *        stays alive), then shows how far it is.
 * @param t  the timer
 */
static void rx_timer_cb(lv_timer_t *t)
{
    if (is_replay_video_cancelled() || !is_stats_db_ready()) {   /* cancelled, or the card went away */
        rx_finish(false);
        return;
    }
    uint32_t t0 = lv_tick_get();
    do {
        bool playing;
        int f = replay_video_done();
        int k = replay_video_k_at(f, &playing);
        bool changed = f == 0 || k != rx_last_k || playing != rx_last_playing;
        if (changed) {
            replay_set(k, playing);
            replay_screen_render();
            lv_refr_now(rx_disp);                  /* into the frame buffer */
            rx_last_k = k;
            rx_last_playing = playing;
        }
        if (!replay_video_frame(changed)) {
            rx_finish(false);
            return;
        }
    } while (replay_video_done() < replay_video_frames() && lv_tick_elaps(t0) < 30);   /* keep the progress dialog alive */

    int done = replay_video_done(), frames = replay_video_frames();
    if (done >= frames) {
        rx_finish(true);
        return;
    }
    int pct = done * 100 / frames;
    lv_bar_set_value(rx_bar, pct, LV_ANIM_OFF);
    /* LVGL's printf has no %f (LV_SPRINTF_USE_FLOAT off): tenths of a MB by hand */
    uint32_t tenths = (uint32_t)(((uint64_t)replay_video_bytes() * 10 + 524288) / 1048576);
    lv_label_set_text_fmt(rx_lbl, "Frame %d of %d   -   %u.%u MB", done, frames,
                          (unsigned)(tenths / 10), (unsigned)(tenths % 10));
}

/* (documented in ui_replay_part.h) */
void replay_export_start(const stats_replay_t *r)
{
    if (is_replay_busy()) {
        return;
    }
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("Can't export: no sudoku.db on a microSD card.", 3000);
        return;
    }
    if (!replay_screen_load(r->puzzle, r->moves, r->n)) {
        printf("export: puzzle %u not available (%d moves)\n", (unsigned)r->puzzle, r->n);
        sudoku_ui_notice("Can't export: this puzzle couldn't be loaded.", 3000);
        return;
    }
    char name[40], file[80];
    puzzle_name(r->puzzle, name, sizeof(name));
    replay_video_name(r, name, file, sizeof(file));
    if (!replay_video_open(file, SCR_W, SCR_W)) {
        sudoku_ui_notice("Can't export: the video encoder or the file on the card couldn't be opened.", 4000);
        return;
    }

    /* the Replay screen on an invisible display that renders into the frame buffer */
    lv_display_t *main_disp = lv_display_get_default();
    rx_disp = lv_display_create(SCR_W, SCR_W);
    lv_display_set_color_format(rx_disp, LV_COLOR_FORMAT_RGB888);   /* (the encoder input: video_enc.c) */
    lv_display_set_buffers(rx_disp, replay_video_fb(), NULL, SCR_W * SCR_W * 3, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(rx_disp, rx_flush_cb);
    lv_display_set_theme(rx_disp, lv_display_get_theme(main_disp));
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(rx_disp);           /* no FPS/CPU box in the video */
#endif
    lv_display_set_default(rx_disp);
    char t[16], sub[96];
    fmt_time(t, sizeof(t), r->seconds);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             r->player, t, (unsigned)r->hints, r->hints == 1 ? "" : "s");
    lv_obj_t *scr = replay_screen_build(r->puzzle, sub, r->player);
    lv_obj_set_hidden(replay_screen_camera(), true);   /* (not in the video) */
    lv_screen_load(scr);
    lv_display_set_default(main_disp);

    /* progress dialog on the real screen */
    rx_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(rx_ovl);
    lv_obj_set_size(rx_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(rx_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rx_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(rx_ovl, true);                   /* nothing else while it runs */
    lv_obj_t *card = lv_obj_create(rx_ovl);
    lv_obj_set_size(card, 520, 250);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(tl, LV_SYMBOL_VIDEO "  Exporting video");
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *fl = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(fl, 470);
    lv_label_set_long_mode(fl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(fl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(fl, "replays/%s.mp4", file);
    lv_obj_align(fl, LV_ALIGN_TOP_MID, 0, 42);
    rx_bar = lv_bar_create(card);
    lv_obj_set_size(rx_bar, 460, 16);
    lv_bar_set_range(rx_bar, 0, 100);
    lv_obj_set_style_bg_color(rx_bar, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rx_bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(rx_bar, LV_ALIGN_TOP_MID, 0, 80);
    rx_lbl = make_label(card, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(rx_lbl, "Starting...");
    lv_obj_align(rx_lbl, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_t *cb = make_btn(card, 0, 0, 200, 52, "Cancel", &lv_font_montserrat_18, rx_cancel_cb, NULL);
    lv_obj_set_style_bg_color(cb, C_CARD2, 0);
    lv_obj_align(cb, LV_ALIGN_BOTTOM_MID, 0, 0);

    rx_last_k = -1;
    rx_last_playing = false;
    rx_timer = lv_timer_create(rx_timer_cb, 1, NULL);
}

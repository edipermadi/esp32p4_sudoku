/**
 * @file ui_media_view.c
 * @ingroup media
 * @brief The picture and video viewer's page: the picture, the bars, swipes and taps (the decoding:
 *        media_core.c).
 *
 * The page's timer (POLL_MS) shows each picture the worker hands over (media_core_take_frame()), its
 * messages, and a video's slider and time.
 */
#include "ui_media_view.h"
#include "media_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

#define SCR        MEDIA_SCR            /**< the screen's size, px */
#define NAME_B     256                  /**< a file name's room */
#define PATH_B     512                  /**< a path's room */
#define POLL_MS    10                   /**< the page's poll, ms */

/* ------------------------------------------------------------- state */
/* UI side */
static lv_obj_t *s_root;                      /**< the viewer (NULL: closed) */
static lv_obj_t *s_img;                       /**< the picture */
static lv_obj_t *s_status;                    /**< a message over it ("Loading...", why not) */
static lv_obj_t *s_top;                       /**< the top bar */
static lv_obj_t *s_name;                      /**< the file's name */
static lv_obj_t *s_counter;                   /**< "3 / 12" */
static lv_obj_t *s_prev;                      /**< Previous */
static lv_obj_t *s_next;                      /**< Next */
static lv_obj_t *s_vbar;                      /**< the video bar */
static lv_obj_t *s_play_lbl;                  /**< Play / Pause's icon */
static lv_obj_t *s_slider;                    /**< where in the video */
static lv_obj_t *s_time;                      /**< "0:12 / 1:30" */
static lv_timer_t *s_timer;                   /**< the page's poll (POLL_MS) */
static lv_image_dsc_t s_dsc[2];               /**< the two buffers as images */
static char s_dir[PATH_B];                    /**< the folder */
static char (*s_names)[NAME_B];               /**< its viewable files (a copy) */
static int s_n;                               /**< how many */
static int s_idx;                             /**< the one shown */
static int s_want = -1;                       /**< the one to show once the worker is free (-1: none) */
static int s_kind;                            /**< the shown one's MEDIA_* */
static bool s_bars = true;                    /**< the bars are shown (a tap toggles them) */
static bool s_gest;                           /**< this press is a swipe or a drag, not a tap */
static bool s_range_set;                      /**< the slider's range is set for this video */
static uint32_t s_msg_seen;                   /**< the worker's message shown (media_core_msg_seq()) */

/* (documented in ui_media_view.h) */
bool media_view_is_open(void)
{
    return s_root != NULL;
}

/**
 * @brief A time as "m:ss".
 * @param[out] buf  gets it
 * @param[in] n  its size
 * @param[in] ms  the time
 */
static void fmt_ms(char *buf, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(buf, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

/**
 * @brief Shows a message over the picture ("" hides it).
 * @param[in] txt  the message
 */
static void show_status(const char *txt)
{
    lv_label_set_text(s_status, txt);
    lv_obj_set_hidden(s_status, txt[0] == 0);
}

/**
 * @brief Shows or hides the bars (Previous / Next with more than one file, the video bar for a video).
 */
static void apply_bars(void)
{
    lv_obj_set_hidden(s_top, !s_bars);
    lv_obj_set_hidden(s_prev, !s_bars || s_n < 2);
    lv_obj_set_hidden(s_next, !s_bars || s_n < 2);
    lv_obj_set_hidden(s_vbar, !s_bars || s_kind != MEDIA_VIDEO);
}

/**
 * @brief Starts the worker on a file.
 * @param[in] idx  the file
 */
static void start_job(int idx)
{
    s_idx = idx;
    s_kind = media_view_kind(s_names[idx]);
    char path[PATH_B + NAME_B];
    snprintf(path, sizeof(path), "%s/%s", s_dir, s_names[idx]);
    s_range_set = false;
    s_msg_seen = media_core_msg_seq();
    lv_label_set_text(s_name, s_names[idx]);
    lv_label_set_text_fmt(s_counter, "%d / %d", idx + 1, s_n);
    lv_label_set_text(s_time, "");
    lv_slider_set_value(s_slider, 0, LV_ANIM_OFF);
    lv_label_set_text(s_play_lbl, LV_SYMBOL_PAUSE);
    apply_bars();
    show_status("Loading...");
    if (!media_core_start(path, s_kind)) {
        show_status("Out of memory.");
    }
}

/**
 * @brief Shows a file: at once, or once the running worker has stopped.
 * @param[in] idx  the file
 */
static void request(int idx)
{
    if (idx < 0 || idx >= s_n) {
        return;
    }
    lv_image_set_src(s_img, NULL);
    if (is_media_core_running()) {                  /* the last one first stops */
        media_core_stop();
        s_want = idx;
        s_idx = idx;
        lv_label_set_text(s_name, s_names[idx]);
        lv_label_set_text_fmt(s_counter, "%d / %d", idx + 1, s_n);
        show_status("Loading...");
    } else {
        start_job(idx);
    }
}

/**
 * @brief The next or previous file, round.
 * @param[in] step  +1 or -1
 */
static void go(int step)
{
    if (s_n < 2) {
        return;
    }
    request((s_idx + step + s_n) % s_n);
}

/**
 * @brief The page's poll: the next file once the worker stopped, a picture handed over, the worker's message, the video's slider and time.
 * @param[in] t  the timer
 */
static void poll(lv_timer_t *t)
{
    if (s_want >= 0 && !is_media_core_running()) {
        int w = s_want;
        s_want = -1;
        start_job(w);
        return;
    }
    int w, h, k = media_core_take_frame(&w, &h);
    if (k >= 0) {
        lv_image_dsc_t *d = &s_dsc[k];
        d->header.w = (uint32_t)w;
        d->header.h = (uint32_t)h;
        d->header.stride = (uint32_t)w * 2;
        d->data_size = (uint32_t)w * h * 2;
        lv_image_set_src(s_img, d);
        lv_obj_invalidate(s_img);
        media_core_frame_shown(k);
        if (s_msg_seen == media_core_msg_seq()) {
            show_status("");
        }
    }
    if (s_msg_seen != media_core_msg_seq()) {
        s_msg_seen = media_core_msg_seq();
        show_status(media_core_msg());
    }
    media_video_t v;
    media_core_video(&v);
    if (s_kind == MEDIA_VIDEO && v.is_info) {
        if (!s_range_set) {
            lv_slider_set_range(s_slider, 0, v.frames > 1 ? (int32_t)v.frames - 1 : 1);
            s_range_set = true;
        }
        char a[16], b[16];
        uint32_t ms = v.pos_ms;
        if (lv_slider_is_dragged(s_slider)) {
            ms = (uint32_t)((uint64_t)lv_slider_get_value(s_slider) * v.dur_ms / (v.frames ? v.frames : 1));
        } else if (!v.is_seeking) {
            lv_slider_set_value(s_slider, (int32_t)v.pos, LV_ANIM_OFF);
        }
        fmt_ms(a, sizeof(a), ms);
        fmt_ms(b, sizeof(b), v.dur_ms);
        lv_label_set_text_fmt(s_time, "%s / %s", a, b);
        lv_label_set_text(s_play_lbl, v.is_ended ? LV_SYMBOL_REFRESH : (v.is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY));
    }
}

/**
 * @brief Close.
 * @param[in] e  the event
 */
static void on_close(lv_event_t *e) { media_view_close(); }
/**
 * @brief Previous.
 * @param[in] e  the event
 */
static void on_prev(lv_event_t *e) { go(-1); }
/**
 * @brief Next.
 * @param[in] e  the event
 */
static void on_next(lv_event_t *e) { go(1); }

/**
 * @brief Play / Pause (at the end: from the start).
 * @param[in] e  the event
 */
static void on_play(lv_event_t *e)
{
    media_core_play_toggle();
}

/**
 * @brief The slider let go: seeks there.
 * @param[in] e  the event
 */
static void on_slider(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        media_core_seek(lv_slider_get_value(s_slider));
    }
}

/** @brief Swipes by hand (from where the finger went down and came up): LVGL's own
 * gesture events didn't arrive here (the drag went to the screen as a scroll). */
static lv_point_t s_down;

/**
 * @brief The viewer pressed: a swipe goes to the next or previous file, a tap shows or hides the bars.
 * @param[in] e  the event
 */
static void on_root(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (code == LV_EVENT_PRESSED) {
        s_gest = false;
        if (indev) {
            lv_indev_get_point(indev, &s_down);
        }
    } else if (code == LV_EVENT_RELEASED && indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int dx = p.x - s_down.x, dy = p.y - s_down.y;
        if (LV_ABS(dx) > 80 && LV_ABS(dx) > 2 * LV_ABS(dy)) {
            s_gest = true;
            go(dx < 0 ? 1 : -1);                    /* to the left: the next one */
        } else if (LV_ABS(dx) > 20 || LV_ABS(dy) > 20) {
            s_gest = true;                          /* (a drag, not a tap) */
        }
    } else if (code == LV_EVENT_SHORT_CLICKED && !s_gest) {
        s_bars = !s_bars;
        apply_bars();
    }
}

/**
 * @brief A label.
 * @param[in] parent  where
 * @param[in] font  its font
 * @param[in] txt  its text
 * @return it
 */
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, txt);
    return l;
}

/**
 * @brief A bar across the screen (a row; a tap on it doesn't hide the bars).
 * @param[in] parent  where
 * @param[in] h  its height, px
 * @param[in] align  where on the screen
 * @return it
 */
static lv_obj_t *bar(lv_obj_t *parent, int h, lv_align_t align)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, SCR, h);
    lv_obj_align(b, align, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_column(b, 12, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(b, false);
    lv_obj_set_clickable(b, true);                  /* (a tap on a bar doesn't hide the bars) */
    lv_obj_set_gesture_bubble(b, false);
    return b;
}

/**
 * @brief An icon button.
 * @param[in] parent  where
 * @param[in] w  px
 * @param[in] h  px
 * @param[in] sym  its icon
 * @param[in] cb  a tap
 * @return it
 */
static lv_obj_t *btn(lv_obj_t *parent, int w, int h, const char *sym, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x666666), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(b, &lv_font_montserrat_24, sym));
    return b;
}

/* (documented in ui_media_view.h) */
void media_view_open(lv_obj_t *parent, const char *dir, const char *const *names, int count, int index)
{
    if (count <= 0 || index < 0 || index >= count) {
        return;
    }
    media_view_close();
    bool bufs = media_core_buffers();
    for (int k = 0; k < 2; k++) {
        memset(&s_dsc[k], 0, sizeof(s_dsc[k]));
        s_dsc[k].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_dsc[k].header.cf = LV_COLOR_FORMAT_RGB565;
        s_dsc[k].data = (const uint8_t *)media_core_buffer(k);
    }
    s_names = heap_caps_malloc((size_t)count * NAME_B, MALLOC_CAP_SPIRAM);
    if (!bufs || !s_names) {
        free(s_names);
        s_names = NULL;
        printf("media: no memory for the viewer\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        strlcpy(s_names[i], names[i], NAME_B);
    }
    s_n = count;
    strlcpy(s_dir, dir, sizeof(s_dir));
    s_bars = true;

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCR, SCR);
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_root, true);
    lv_obj_set_scrollable(s_root, false);
    lv_obj_add_event_cb(s_root, on_root, LV_EVENT_ALL, NULL);

    s_img = lv_image_create(s_root);
    lv_obj_center(s_img);
    s_status = label(s_root, &lv_font_montserrat_20, "");
    lv_obj_set_width(s_status, 600);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xBBBBBB), 0);
    lv_obj_center(s_status);

    s_top = bar(s_root, 72, LV_ALIGN_TOP_MID);
    btn(s_top, 64, 52, LV_SYMBOL_CLOSE, on_close);
    s_name = label(s_top, &lv_font_montserrat_20, "");
    lv_obj_set_flex_grow(s_name, 1);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_DOTS);
    s_counter = label(s_top, &lv_font_montserrat_18, "");
    lv_obj_set_style_text_color(s_counter, lv_color_hex(0xBBBBBB), 0);

    s_prev = btn(s_root, 60, 110, LV_SYMBOL_LEFT, on_prev);
    lv_obj_align(s_prev, LV_ALIGN_LEFT_MID, 10, 0);
    s_next = btn(s_root, 60, 110, LV_SYMBOL_RIGHT, on_next);
    lv_obj_align(s_next, LV_ALIGN_RIGHT_MID, -10, 0);

    /* the video bar: play, the slider, the time (placed by hand: in a flex row the
     * slider's knob padding pushed the time off the right edge) */
    s_vbar = bar(s_root, 84, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_layout(s_vbar, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(s_vbar, 0, 0);
    lv_obj_t *pb = btn(s_vbar, 72, 56, "", on_play);
    lv_obj_align(pb, LV_ALIGN_LEFT_MID, 12, -4);
    s_play_lbl = lv_obj_get_child(pb, 0);
    s_slider = lv_slider_create(s_vbar);
    lv_obj_set_size(s_slider, 720 - 108 - 140, 10);
    lv_obj_align(s_slider, LV_ALIGN_LEFT_MID, 108, -4);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(0x3A8DFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s_slider, 24);
    lv_obj_set_gesture_bubble(s_slider, false);
    lv_obj_add_event_cb(s_slider, on_slider, LV_EVENT_RELEASED, NULL);
    s_time = label(s_vbar, &lv_font_montserrat_18, "");
    lv_obj_set_width(s_time, 110);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_time, LV_ALIGN_RIGHT_MID, -12, -4);

    s_timer = lv_timer_create(poll, POLL_MS, NULL);
    request(index);
}

/* (documented in ui_media_view.h) */
void media_view_close(void)
{
    if (!s_root) {
        return;
    }
    media_core_stop();                              /* the worker ends on its own */
    s_want = -1;
    if (s_timer) {
        lv_timer_delete(s_timer);
    }
    s_timer = NULL;
    lv_obj_delete(s_root);
    s_root = NULL;
    heap_caps_free(s_names);
    s_names = NULL;
    s_n = 0;
}

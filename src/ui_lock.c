/**
 * @file ui_lock.c
 * @ingroup ui_lock
 * @brief The lock screen (Swipe, or a 4-digit PIN with a keypad) under the screen saver's black cover, and
 *        the keypad it shares with the PIN setup (ui_pin_setup.c). The PIN itself: lock_core.h.
 */
#include "ui_internal.h"
#include "lock_core.h"

/**
 * @defgroup ui_lock Lock screen
 * @ingroup topic_lock
 * @brief The lock screen and the PIN setup screen.
 * @{
 */

static void lock_open(void);

static lv_obj_t *lk_ovl;                  /**< the lock screen (NULL: not shown) */
static lv_obj_t *lk_box;                  /**< the time, the date and who plays (follows a swipe) */
static lv_obj_t *lk_time;                 /**< the time */
static lv_obj_t *lk_date;                 /**< the date */
static lv_obj_t *lk_hint;                 /**< "Enter the PIN", "Swipe up to unlock", ... */
static lv_obj_t *lk_dots[4];              /**< the PIN's dots */
static lv_obj_t *lk_pad;                  /**< the keypad (PIN; NULL: Swipe) */
static lv_timer_t *lk_timer;              /**< the clock and the wait, each second */
static int32_t lk_press_y;                /**< where a swipe started */


/**
 * @brief Each second: the time and date, and the wait after too many wrong PINs.
 * @param[in] t  the timer (NULL: a first call)
 */
static void lock_tick(lv_timer_t *t)
{
    if (!lk_ovl) {
        return;
    }
    time_t now = time(NULL);
    char hm[8] = "--:--", date[48] = "";
    if (CLOCK_VALID(now)) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(hm, sizeof(hm), "%H:%M", &tm);
        strftime(date, sizeof(date), "%A, %d %B", &tm);
    }
    lv_label_set_text(lk_time, hm);
    lv_label_set_text(lk_date, date);
    if (lk_pad) {                                   /* the wait after too many wrong PINs */
        bool ended;
        int32_t left = lock_wait_left(lv_tick_get(), &ended);
        bool waiting = left > 0;
        lv_obj_set_state(lk_pad, LV_STATE_DISABLED, waiting);
        lv_obj_set_style_opa(lk_pad, waiting ? LV_OPA_40 : LV_OPA_COVER, 0);
        if (waiting) {
            lv_label_set_text_fmt(lk_hint, "Too many tries: wait %d s", (int)(left / 1000) + 1);
        } else if (ended) {
            lv_label_set_text(lk_hint, "Enter the PIN");
        }
    }
}

/** @brief Unlocks: the lock screen goes. */
static void lock_open(void)   /* unlock: remove the lock screen */
{
    if (lk_timer) {
        lv_timer_delete(lk_timer);
    }
    lk_timer = NULL;
    if (lk_ovl) {
        lv_obj_delete_async(lk_ovl);
    }
    lk_ovl = lk_box = lk_pad = NULL;
}

/**
 * @brief A key on the lock screen's pad: the PIN checked once it has 4 digits.
 * @param[in] e  the event (user data: the key)
 */
static void lock_key_cb(lv_event_t *e)
{
    lock_key_t r = lock_key((char)(intptr_t)lv_event_get_user_data(e), lv_tick_get());
    if (r == LOCK_KEY_IGNORED) {
        return;
    }
    pin_dots(lk_dots, lock_len(), C_ACCENT);
    if (r == LOCK_KEY_MORE) {
        return;
    }
    if (r == LOCK_KEY_RIGHT) {                      /* right: back to where it was */
        lock_open();
        return;
    }
    pin_dots(lk_dots, 4, C_RED);                    /* wrong */
    sfx_play(SFX_ERROR);
    if (r == LOCK_KEY_WRONG) {
        lv_label_set_text_fmt(lk_hint, "Wrong PIN, try again (%d left)", lock_tries_left());
    }
}

/**
 * @brief Swipe: the time follows the finger up; far enough (LOCK_SWIPE_PX), and it's unlocked.
 * @param[in] e  the press, pressing, release or press-lost event
 */
static void lock_swipe_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        lk_press_y = p.y;
        break;
    case LV_EVENT_PRESSING: {
        int32_t dy = p.y - lk_press_y;
        lv_obj_set_style_translate_y(lk_box, dy < 0 ? dy : 0, 0);
        lv_obj_set_style_opa(lk_box, dy < 0 ? (lv_opa_t)LV_MAX(60, 255 + dy) : LV_OPA_COVER, 0);
        break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (lk_press_y - p.y >= LOCK_SWIPE_PX) {
            lock_open();
        } else {
            lv_obj_set_style_translate_y(lk_box, 0, 0);
            lv_obj_set_style_opa(lk_box, LV_OPA_COVER, 0);
        }
        break;
    default:
        break;
    }
}

/* (documented in ui_internal.h) */
void ui_lock_show(bool force)
{
    int mode = lock_mode_shown(force);
    if (mode == LOCK_OFF || lk_ovl) {
        return;
    }
    lk_ovl = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(lk_ovl);
    lv_obj_set_size(lk_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(lk_ovl, C_BG, 0);
    lv_obj_set_style_bg_opa(lk_ovl, LV_OPA_COVER, 0);
    lv_obj_set_clickable(lk_ovl, true);             /* nothing below gets a touch */
    lv_obj_set_scrollable(lk_ovl, false);

    lk_box = lv_obj_create(lk_ovl);                 /* the time and who's playing */
    lv_obj_remove_style_all(lk_box);
    lv_obj_set_size(lk_box, LV_PCT(100), 220);
    lv_obj_align(lk_box, LV_ALIGN_TOP_MID, 0, mode == LOCK_PIN ? 30 : 150);
    lv_obj_set_flex_flow(lk_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lk_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(lk_box, 8, 0);
    lv_obj_set_clickable(lk_box, false);
    lv_obj_t *icon = make_label(lk_box, &font_icons_24, C_MUTED);
    lv_label_set_text(icon, SYMBOL_LOCK);
    lk_time = make_label(lk_box, &font_timer_36, C_TEXT);   /* (its own size: scaled, it would blur) */
    lk_date = make_label(lk_box, &lv_font_montserrat_20, C_MUTED);
    lv_obj_t *who = make_label(lk_box, &font_icons_20, C_MUTED);
    lv_label_set_text_fmt(who, SYMBOL_USER "  %s", store_player_name());

    lk_hint = make_label(lk_ovl, &lv_font_montserrat_20, C_TEXT);
    lock_begin();
    lk_pad = NULL;
    if (mode == LOCK_PIN) {
        lv_label_set_text(lk_hint, "Enter the PIN");
        lv_obj_align(lk_hint, LV_ALIGN_TOP_MID, 0, 250);
        lv_obj_t *dots_row;
        pin_dots_make(lk_ovl, lk_dots);
        dots_row = lv_obj_get_parent(lk_dots[0]);
        lv_obj_align(dots_row, LV_ALIGN_TOP_MID, 0, 290);
        lk_pad = pin_pad(lk_ovl, lock_key_cb);
        lv_obj_align(lk_pad, LV_ALIGN_TOP_MID, 0, 340);
    } else {
        lv_label_set_text(lk_hint, LV_SYMBOL_UP "\nSwipe up to unlock");
        lv_obj_set_style_text_align(lk_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lk_hint, LV_ALIGN_BOTTOM_MID, 0, -70);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(lk_ovl, lock_swipe_cb, LV_EVENT_PRESS_LOST, NULL);
    }
    lock_tick(NULL);
    lk_timer = lv_timer_create(lock_tick, 1000, NULL);
}

/** @} */

/* (documented in ui_internal.h) */
void pin_dots(lv_obj_t *const *dots, int n, lv_color_t on)
{
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(dots[i], i < n ? on : C_CARD2, 0);
    }
}

/* (documented in ui_internal.h) */
void pin_dots_make(lv_obj_t *parent, lv_obj_t **dots)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 4 * 22 + 3 * 18, 22);
    for (int i = 0; i < 4; i++) {
        dots[i] = lv_obj_create(row);
        lv_obj_remove_style_all(dots[i]);
        lv_obj_set_size(dots[i], 22, 22);
        lv_obj_set_pos(dots[i], i * 40, 0);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dots[i], C_CARD2, 0);
    }
}

/* (documented in ui_internal.h) */
lv_obj_t *pin_pad(lv_obj_t *parent, lv_event_cb_t cb)
{
    static const char KEYS[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', ' ', '0', '<'};
    lv_obj_t *pad = lv_obj_create(parent);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 3 * 110 + 2 * 12, 4 * 72 + 3 * 12);
    for (int i = 0; i < 12; i++) {
        if (KEYS[i] == ' ') {
            continue;
        }
        char t[4] = {KEYS[i], 0};
        lv_obj_t *b = make_btn(pad, (i % 3) * 122, (i / 3) * 84, 110, 72, KEYS[i] == '<' ? LV_SYMBOL_BACKSPACE : t,
                               &lv_font_montserrat_28, cb, (void *)(intptr_t)KEYS[i]);
        lv_obj_set_style_bg_color(b, C_CARD2, 0);
    }
    return pad;
}

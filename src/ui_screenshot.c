/**
 * @file ui_screenshot.c
 * @brief The camera buttons (Solved dialog, Replay): the screen copied and saved as a JPEG in screenshots/ on
 *        the card, then a notice (the encoding and the file: screenshot_core.c).
 */
#include "ui_internal.h"

static void ss_poll_cb(lv_timer_t *t);

static lv_timer_t *ss_timer;                        /**< waits for the task (200 ms) */

/**
 * @brief Waits for the task, then says where the picture went (or that it failed).
 * @param[in] t  the timer
 */
static void ss_poll_cb(lv_timer_t *t)
{
    int st = screenshot_state();
    if (st < SCREENSHOT_DONE) {
        return;
    }
    lv_timer_delete(ss_timer);
    ss_timer = NULL;
    char msg[120];
    if (st == SCREENSHOT_DONE)
        snprintf(msg, sizeof(msg), "Saved %s. Copy it off with Settings > USB drive.",
                 screenshot_path() + strlen(BSP_SD_MOUNT_POINT) + 1);
    else {
        snprintf(msg, sizeof(msg), "Couldn't save the screenshot.");
    }
    printf("screenshot: %s\n", msg);
    screenshot_reset();
    sudoku_ui_notice(msg, 4000);
}

/**
 * @brief A camera button: the screen copied now, written by a task as screenshots/YYYYMMDD-HHMMSS.jpg
 *        (without the clock: shot-\<seconds since start\>.jpg). One at a time; needs the card.
 * @param[in] e  the event
 */
void screenshot_cb(lv_event_t *e)
{
    if (screenshot_state() != SCREENSHOT_IDLE) {
        return;    /* (one at a time) */
    }
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("Screenshots go on the microSD card: there's none to write to.", 3000);
        return;
    }
    lv_refr_now(NULL);
    if (!screenshot_start(bsp_display_frame())) {
        return;
    }
    ss_timer = lv_timer_create(ss_poll_cb, 200, NULL);
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @brief Whether a screenshot is being written (the card is in use). @return true: writing */
bool is_screenshot_busy(void) { return screenshot_state() != SCREENSHOT_IDLE; }

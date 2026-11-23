/**
 * @file screenshot_core.h
 * @ingroup screenshot_core
 * @brief The screen as a JPEG in screenshots/ on the card, without LVGL (the camera buttons: ui_screenshot.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup screenshot_core Screenshot core
 * @ingroup media
 * @brief A copy of the frame, compressed by the hardware JPEG encoder and written by a task on core 0, one at a
 *        time: screenshots/YYYYMMDD-HHMMSS.jpg (without the clock: shot-\<seconds since start\>.jpg).
 * @{
 */

#define SCREENSHOT_W 720                /**< the frame's width and height, px */

/** @brief Where a screenshot is (screenshot_state()). */
enum {
    SCREENSHOT_IDLE = 0,                /**< none under way */
    SCREENSHOT_WRITING,                 /**< being compressed and written */
    SCREENSHOT_DONE,                    /**< written (screenshot_path()) */
    SCREENSHOT_FAILED                   /**< it couldn't be written */
};

/**
 * @brief Copies the frame now and starts writing it.
 * @param[in] fb  the frame shown, RGB565, SCREENSHOT_W x SCREENSHOT_W (bsp_display_frame())
 * @return false: one is still under way, or no frame or no memory
 */
bool screenshot_start(const uint16_t *fb);
/**
 * @brief How the last one went.
 * @return SCREENSHOT_*
 */
int screenshot_state(void);
/**
 * @brief The file being written, or written.
 * @return its path
 */
const char *screenshot_path(void);
/** @brief Done or failed, and said so: ready for the next one. */
void screenshot_reset(void);

/** @} */

#ifdef __cplusplus
}
#endif

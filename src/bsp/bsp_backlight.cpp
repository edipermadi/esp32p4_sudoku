/**
 * @file bsp_backlight.cpp
 * @ingroup bsp
 * @brief The backlight: LEDC PWM on GPIO26, active low.
 */
#include <Arduino.h>
#include "bsp_board.h"

#define BL_FREQ_HZ 5000                 /**< the PWM frequency, Hz */
#define BL_RES_BITS 10                  /**< the PWM resolution, bits */

/* (documented in bsp_board.h) */
extern "C" void bsp_display_brightness_set(int percent)
{
    static bool ready = false;
    if (!ready) {
        ready = ledcAttach(BSP_LCD_BACKLIGHT_GPIO, BL_FREQ_HZ, BL_RES_BITS) &&
                ledcOutputInvert(BSP_LCD_BACKLIGHT_GPIO, true);
        if (!ready) {
            return;
        }
    }
    percent = constrain(percent, 0, 100);
    ledcWrite(BSP_LCD_BACKLIGHT_GPIO, (uint32_t)percent * ((1 << BL_RES_BITS) - 1) / 100);
}

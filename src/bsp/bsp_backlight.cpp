// Backlight on GPIO26: LEDC PWM, active low (GPIO33 is NC on this board - leave it alone).
#include <Arduino.h>
#include "bsp_board.h"

#define BL_FREQ_HZ 5000
#define BL_RES_BITS 10

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

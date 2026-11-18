/*
 * Power off: everything we drive is switched off and held there, then the
 * ESP32-P4 goes into deep sleep with no wake-up source. Only the RESET (EN)
 * button, or removing power, starts it again (a normal boot).
 */
#include "bsp_board.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_rom_sys.h"

/* Drives a pin to a level and keeps it there through deep sleep. The pad hold
 * latches whatever the pad outputs; gpio_config() also takes the pin back from
 * a peripheral (LEDC on the backlight) to plain GPIO output. */
static void drive_and_hold(int gpio, int level)
{
    gpio_hold_dis((gpio_num_t)gpio);
    gpio_set_level((gpio_num_t)gpio, level);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = level ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = level ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)gpio, level);
    gpio_hold_en((gpio_num_t)gpio);
}

void bsp_power_off(void)
{
    drive_and_hold(BSP_LCD_BACKLIGHT_GPIO, 1);   /* active low: off */
    drive_and_hold(BSP_AUDIO_PA_GPIO, 0);        /* speaker amp off */
    drive_and_hold(BSP_LCD_RST_GPIO, 0);         /* panel held in reset */
    drive_and_hold(BSP_HOSTED_RESET_GPIO, 0);    /* ESP32-C6 (Wi-Fi) held in reset */
    esp_rom_delay_us(1000);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    /* On ESP32-P4 before v3.0 the per-pin holds above live in the TOP power
     * domain, which deep sleep switches off: the pins float again and the
     * backlight comes on. The PMU's hold-all latch is in the LP domain and
     * keeps every pad as it is now through deep sleep; ROM boot releases it on
     * RESET. (Flash/PSRAM use dedicated pins; the UART stops here.) */
    esp_rom_printf("deep sleep until RESET\n");
    esp_rom_delay_us(5000);                       /* UART FIFO drained */
    gpio_force_hold_all();
    esp_deep_sleep_start();                       /* does not return */
}

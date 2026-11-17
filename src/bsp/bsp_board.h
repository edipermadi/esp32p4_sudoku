/*
 * Board support for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
 * (4" 720x720 MIPI-DSI IPS panel + GT911 capacitive touch).
 *
 * Pin map / timings taken from Waveshare's published BSP:
 *   github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B
 *   github.com/waveshareteam/waveshare_boards (boards/esp32_p4_wifi6_touch_lcd_4b)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- display */
#define BSP_LCD_H_RES            720
#define BSP_LCD_V_RES            720
#define BSP_LCD_BITS_PER_PIXEL   16      /* RGB565 */
#define BSP_LCD_DSI_LANES        2
#define BSP_LCD_DSI_LANE_MBPS    480
#define BSP_LCD_RST_GPIO         27      /* active low */
#define BSP_LCD_BACKLIGHT_GPIO   26      /* LEDC PWM, active low */
#define BSP_MIPI_DPHY_LDO_CHAN   3       /* LDO_VO3 -> VDD_MIPI_DPHY */
#define BSP_MIPI_DPHY_LDO_MV     2500

/* ------------------------------------------------------------------ I2C */
#define BSP_I2C_PORT             1       /* Arduino Wire uses port 0 */
#define BSP_I2C_SDA_GPIO         7
#define BSP_I2C_SCL_GPIO         8
#define BSP_TOUCH_I2C_HZ         400000

typedef enum {
    BSP_PANEL_UNKNOWN = 0,
    BSP_PANEL_ST7703,
    BSP_PANEL_JD9365,
} bsp_panel_kind_t;

/* Shared I2C bus on GPIO7/8 (touch; also ES8311, ES7210, camera on this board). Created on first call. */
esp_err_t bsp_i2c_init(i2c_master_bus_handle_t *ret_bus);

/* Powers the D-PHY, brings up the DSI link, detects ST7703 vs JD9365 and
 * initialises the panel. Backlight is left off. */
esp_err_t bsp_display_new(esp_lcd_panel_handle_t *ret_panel);
bsp_panel_kind_t bsp_display_panel_kind(void);

/* Backlight brightness 0..100 % (implemented with the Arduino LEDC API). */
void bsp_display_brightness_set(int percent);

/* GT911 touch, polled (INT is not routed to the SoC). */
esp_err_t bsp_touch_init(void);
/* Returns true while touched; fills the first contact point. */
bool bsp_touch_read(uint16_t *x, uint16_t *y);

/* All of the above + LVGL display/input-device registration.
 * After this returns, create your UI and call lv_timer_handler() from loop(). */
esp_err_t bsp_lvgl_start(void);

#ifdef __cplusplus
}
#endif

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

/* ---------------------------------------------------------------- audio */
#define BSP_I2S_MCLK_GPIO        13
#define BSP_I2S_BCLK_GPIO        12
#define BSP_I2S_WS_GPIO          10
#define BSP_I2S_DOUT_GPIO        9       /* SoC -> ES8311 DSDIN */
#define BSP_I2S_DIN_GPIO         11      /* ES7210 mic ADC -> SoC */
#define BSP_AUDIO_PA_GPIO        53      /* NS4150 amp enable, active high */

/* ------------------------------------------------------------- Wi-Fi (C6) */
#define BSP_HOSTED_RESET_GPIO    54      /* ESP32-C6 EN: low = held in reset */

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
/* Panel controller asleep (DCS display off + sleep in) or awake again
 * (sleep out + display on, blocks ~140..270 ms). Turn the backlight off
 * before sleeping it and on only after waking it. */
void bsp_display_sleep(bool sleep);

/* ---------------------------------------------------------------- microSD */
/* SDMMC slot 0 (IO_MUX): CLK 43, CMD 44, D0..D3 = 39..42, no card-detect.
 * The IO rail comes from on-chip LDO VO4 at 3.3 V. The SDMMC host is shared
 * with ESP-Hosted (Wi-Fi via the C6 on slot 1). */
#define BSP_SD_LDO_CHAN          4
#define BSP_SD_LDO_MV            3300

/* Powers the slot's IO rail and starts the SDMMC host. Call at boot, before
 * Wi-Fi starts (same order as the esp32p4_86box project, where both work). */
esp_err_t bsp_sdcard_host_init(void);
/* Detects and initialises the card. Fills total bytes and the card's name
 * (>= 8 chars). Card init runs at 400 kHz, which reprograms the SDMMC clock
 * divider that the Wi-Fi link (ESP-Hosted, slot 1) also uses, so it must not
 * run once Wi-Fi is up: do it at boot, before wifi_mgr_start(). */
esp_err_t bsp_sdcard_probe(uint64_t *bytes, char *name);
/* Is the card found by bsp_sdcard_probe() still answering? (CMD13, no clock change) */
esp_err_t bsp_sdcard_check(void);
/* Erases the card's layout: MBR with one primary FAT32 partition over the
 * whole card (FatFs f_mkfs). Takes seconds; call from a worker task.
 * Unmount first. */
esp_err_t bsp_sdcard_format_fat32(void);

/* Raw sectors of the card found by bsp_sdcard_probe() (the USB drive).
 * Only while it is unmounted: FatFs and a computer must not share it. */
esp_err_t bsp_sdcard_read(void *buf, uint32_t sector, uint32_t count);
esp_err_t bsp_sdcard_write(const void *buf, uint32_t sector, uint32_t count);
uint32_t bsp_sdcard_sector_count(void);
uint32_t bsp_sdcard_sector_size(void);

#define BSP_SD_MOUNT_POINT "/sdcard"
/* Mounts the card's FAT volume at BSP_SD_MOUNT_POINT (POSIX file API).
 * ESP_ERR_NOT_SUPPORTED: the card has no FAT filesystem. */
esp_err_t bsp_sdcard_mount(void);
void bsp_sdcard_unmount(void);

/* GT911 touch, polled (INT is not routed to the SoC). */
esp_err_t bsp_touch_init(void);
/* Returns true while touched; fills the first contact point. */
bool bsp_touch_read(uint16_t *x, uint16_t *y);
/* A simulated touch (serial control, devctl.cpp): while on, LVGL reads this
 * instead of the panel: pressed or not, at x, y. */
void bsp_touch_sim(bool on, bool pressed, uint16_t x, uint16_t y);

/* All of the above + LVGL display/input-device registration.
 * After this returns, create your UI and call lv_timer_handler() from loop(). */
esp_err_t bsp_lvgl_start(void);
/* The panel's frame buffer: what the screen shows, RGB565, BSP_LCD_H_RES x
 * BSP_LCD_V_RES, read back from PSRAM (cache synced); NULL if not available.
 * Call from the LVGL task after lv_refr_now(), so no flush is under way. */
const uint16_t *bsp_display_frame(void);

/* Speaker: I2S0 (TX only, 16-bit stereo) -> ES8311 -> amp. Supported rates:
 * 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000.
 * Turns the amp on when done. */
esp_err_t bsp_audio_init(uint32_t sample_rate);
/* Re-clocks I2S + codec; call only while holding the audio (see acquire). */
esp_err_t bsp_audio_set_sample_rate(uint32_t sample_rate);
/* Exclusive use of the speaker for one audio producer (the MP3 player takes it per track). */
bool bsp_audio_acquire(uint32_t timeout_ms);
void bsp_audio_release(void);
/* Blocking write of interleaved L/R 16-bit frames. */
esp_err_t bsp_audio_write(const int16_t *stereo_frames, size_t frames, uint32_t timeout_ms);
/* 0..100 % (0 = mute, 100 = 0 dB codec gain). */
void bsp_audio_set_volume(int percent);
uint32_t bsp_audio_sample_rate(void);

/* Microphones (ES7210, MIC1 + MIC2) on the same I2S port, at the speaker's
 * sample rate: 16-bit stereo frames (L = MIC1, R = MIC2). Call after
 * bsp_audio_init. */
esp_err_t bsp_mic_start(void);
void bsp_mic_stop(void);
esp_err_t bsp_mic_read(int16_t *stereo_frames, size_t frames, size_t *got_frames, uint32_t timeout_ms);

/* ---------------------------------------------------------------- power */
/* Backlight, speaker amp, panel and Wi-Fi chip off (held through sleep), then
 * deep sleep with no wake-up source: only the RESET button (or a power cycle)
 * starts the board again. Close files / unmount the card first. Never returns. */
void bsp_power_off(void) __attribute__((noreturn));

/* Power saving while the screen is dark: backlight off, panel asleep, CPU
 * clocked down (BSP_SAVE_CPU_MHZ). false: CPU back to full speed, panel awake;
 * the caller then sets the backlight. Call from the LVGL task. */
#define BSP_SAVE_CPU_MHZ 180     /* 90 MHz: the DPI DMA underruns (memory clock follows the CPU) */
void bsp_power_save(bool on);
bool is_bsp_power_saving(void);
/* While power saving: the CPU at full speed for a while (true) or back to BSP_SAVE_CPU_MHZ
 * (false); the panel stays asleep. Does nothing when not saving. (devctl genbench) */
void bsp_power_cpu_full(bool on);

#ifdef __cplusplus
}
#endif

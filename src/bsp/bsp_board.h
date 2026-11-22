/**
 * @file bsp_board.h
 * @ingroup bsp
 * @brief The board's pins and timings, and the BSP's functions.
 *
 * Board support for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
 * (4" 720x720 MIPI-DSI IPS panel + GT911 capacitive touch).
 *
 * Pin map / timings taken from Waveshare's published BSP:
 *   github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B
 *   github.com/waveshareteam/waveshare_boards (boards/esp32_p4_wifi6_touch_lcd_4b)
 *
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
#define BSP_LCD_H_RES            720    /**< the panel's width, px */
#define BSP_LCD_V_RES            720    /**< its height, px */
#define BSP_LCD_BITS_PER_PIXEL   16      /**< RGB565 */
#define BSP_LCD_DSI_LANES        2      /**< MIPI-DSI data lanes */
#define BSP_LCD_DSI_LANE_MBPS    480    /**< each lane's rate, Mbit/s */
#define BSP_LCD_RST_GPIO         27      /**< active low */
#define BSP_LCD_BACKLIGHT_GPIO   26      /**< LEDC PWM, active low */
#define BSP_MIPI_DPHY_LDO_CHAN   3       /**< LDO_VO3 -> VDD_MIPI_DPHY */
#define BSP_MIPI_DPHY_LDO_MV     2500   /**< the D-PHY's supply, mV */

/* ------------------------------------------------------------------ I2C */
#define BSP_I2C_PORT             1       /**< Arduino Wire uses port 0 */
#define BSP_I2C_SDA_GPIO         7      /**< the shared I2C bus: SDA */
#define BSP_I2C_SCL_GPIO         8      /**< SCL */
#define BSP_TOUCH_I2C_HZ         400000 /**< the touch controller's I2C clock, Hz */

/* ---------------------------------------------------------------- audio */
#define BSP_I2S_MCLK_GPIO        13     /**< I2S master clock */
#define BSP_I2S_BCLK_GPIO        12     /**< I2S bit clock */
#define BSP_I2S_WS_GPIO          10     /**< I2S word select (LRCK) */
#define BSP_I2S_DOUT_GPIO        9       /**< SoC -> ES8311 DSDIN */
#define BSP_I2S_DIN_GPIO         11      /**< ES7210 mic ADC -> SoC */
#define BSP_AUDIO_PA_GPIO        53      /**< NS4150 amp enable, active high */

/* ------------------------------------------------------------- Wi-Fi (C6) */
#define BSP_HOSTED_RESET_GPIO    54      /**< ESP32-C6 EN: low = held in reset */

/** @brief The panel this board has (found by its ID). */
typedef enum {
    BSP_PANEL_UNKNOWN = 0,      /**< not found yet */
    BSP_PANEL_ST7703,           /**< ST7703 */
    BSP_PANEL_JD9365,           /**< JD9365DA */
} bsp_panel_kind_t;

/**
 * @brief Shared I2C bus on GPIO7/8 (touch; also ES8311, ES7210, camera on this board). Created on first call.
 * @param[out] ret_bus  gets the bus
 * @return ESP_OK, or why not
 */
esp_err_t bsp_i2c_init(i2c_master_bus_handle_t *ret_bus);

/**
 * @brief Powers the D-PHY, brings up the DSI link, detects ST7703 vs JD9365 and
 * initialises the panel. Backlight is left off.
 * @param[out] ret_panel  gets the panel
 * @return ESP_OK, or why not
 */
esp_err_t bsp_display_new(esp_lcd_panel_handle_t *ret_panel);
/**
 * @brief The panel found by bsp_display_new().
 * @return it
 */
bsp_panel_kind_t bsp_display_panel_kind(void);

/**
 * @brief Backlight brightness 0..100 % (implemented with the Arduino LEDC API).
 * @param[in] percent  0..100
 */
void bsp_display_brightness_set(int percent);
/**
 * @brief Panel controller asleep (DCS display off + sleep in) or awake again
 * (sleep out + display on, blocks ~140..270 ms). Turn the backlight off
 * before sleeping it and on only after waking it.
 * @param[in] sleep  asleep (else awake)
 */
void bsp_display_sleep(bool sleep);

/* ---------------------------------------------------------------- microSD */
/** @brief SDMMC slot 0 (IO_MUX): CLK 43, CMD 44, D0..D3 = 39..42, no card-detect.
 * The IO rail comes from on-chip LDO VO4 at 3.3 V. The SDMMC host is shared
 * with ESP-Hosted (Wi-Fi via the C6 on slot 1). */
#define BSP_SD_LDO_CHAN          4
#define BSP_SD_LDO_MV            3300   /**< the slot's IO rail (LDO_VO4), mV */

/**
 * @brief Powers the slot's IO rail and starts the SDMMC host. Call at boot, before
 * Wi-Fi starts (same order as the esp32p4_86box project, where both work).
 * @return ESP_OK, or why not
 */
esp_err_t bsp_sdcard_host_init(void);
/**
 * @brief Detects and initialises the card. Fills total bytes and the card's name
 * (>= 8 chars). Card init runs at 400 kHz, which reprograms the SDMMC clock
 * divider that the Wi-Fi link (ESP-Hosted, slot 1) also uses, so it must not
 * run once Wi-Fi is up: do it at boot, before wifi_mgr_start().
 * @param[out] bytes  gets the card's size
 * @param[out] name  gets its name
 * @return ESP_OK, or why not
 */
esp_err_t bsp_sdcard_probe(uint64_t *bytes, char *name);
/**
 * @brief Is the card found by bsp_sdcard_probe() still answering? (CMD13, no clock change)
 * @return ESP_OK if it answers
 */
esp_err_t bsp_sdcard_check(void);
/**
 * @brief Erases the card's layout: MBR with one primary FAT32 partition over the
 * whole card (FatFs f_mkfs). Takes seconds; call from a worker task.
 * Unmount first.
 * @return ESP_OK, or why not
 */
esp_err_t bsp_sdcard_format_fat32(void);

/**
 * @brief Raw sectors of the card found by bsp_sdcard_probe() (the USB drive).
 * Only while it is unmounted: FatFs and a computer must not share it.
 * @param[out] buf  gets the sectors
 * @param[in] sector  the first
 * @param[in] count  how many
 * @return ESP_OK, or why not
 */
esp_err_t bsp_sdcard_read(void *buf, uint32_t sector, uint32_t count);
/**
 * @brief Raw sectors written (the USB drive; only while unmounted).
 * @param[in] buf  the sectors
 * @param[in] sector  the first
 * @param[in] count  how many
 * @return ESP_OK, or why not
 */
esp_err_t bsp_sdcard_write(const void *buf, uint32_t sector, uint32_t count);
/**
 * @brief The card's size.
 * @return sectors
 */
uint32_t bsp_sdcard_sector_count(void);
/**
 * @brief The card's sector size.
 * @return bytes
 */
uint32_t bsp_sdcard_sector_size(void);

#define BSP_SD_MOUNT_POINT "/sdcard"    /**< where the card is mounted */
/**
 * @brief Mounts the card's FAT volume at BSP_SD_MOUNT_POINT (POSIX file API).
 * ESP_ERR_NOT_SUPPORTED: the card has no FAT filesystem.
 * @return ESP_OK, ESP_ERR_NOT_SUPPORTED or why not
 */
esp_err_t bsp_sdcard_mount(void);
/**
 * @brief Unmounts the card's volume.
 */
void bsp_sdcard_unmount(void);

/**
 * @brief GT911 touch, polled (INT is not routed to the SoC).
 * @return ESP_OK, or why not
 */
esp_err_t bsp_touch_init(void);
/**
 * @brief Reads the touch.
 * @param[out] x  gets the first contact's x
 * @param[out] y  gets its y
 * @return true while touched
 */
bool bsp_touch_read(uint16_t *x, uint16_t *y);
/**
 * @brief A simulated touch (serial control, devctl.c): while on, LVGL reads this
 * instead of the panel: pressed or not, at x, y.
 * @param[in] on  simulated (else the panel)
 * @param[in] pressed  pressed
 * @param[in] x  px
 * @param[in] y  px
 */
void bsp_touch_sim(bool on, bool pressed, uint16_t x, uint16_t y);

/**
 * @brief All of the above + LVGL display/input-device registration.
 * After this returns, create your UI and call lv_timer_handler() from loop().
 * @return ESP_OK, or why not
 */
esp_err_t bsp_lvgl_start(void);
/**
 * @brief The panel's frame buffer: what the screen shows, RGB565, BSP_LCD_H_RES x
 * BSP_LCD_V_RES, read back from PSRAM (cache synced); NULL if not available.
 * Call from the LVGL task after lv_refr_now(), so no flush is under way.
 * @return the frame buffer, or NULL
 */
const uint16_t *bsp_display_frame(void);

/**
 * @brief Speaker: I2S0 (TX only, 16-bit stereo) -> ES8311 -> amp. Supported rates:
 * 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000.
 * Turns the amp on when done.
 * @param[in] sample_rate  Hz
 * @return ESP_OK, or why not
 */
esp_err_t bsp_audio_init(uint32_t sample_rate);
/**
 * @brief Re-clocks I2S + codec; call only while holding the audio (see acquire).
 * @param[in] sample_rate  Hz
 * @return ESP_OK, or why not
 */
esp_err_t bsp_audio_set_sample_rate(uint32_t sample_rate);
/**
 * @brief Exclusive use of the speaker for one audio producer (the MP3 player takes it per track).
 * @param[in] timeout_ms  how long to wait
 * @return true if it's ours
 */
bool bsp_audio_acquire(uint32_t timeout_ms);
/**
 * @brief Gives the speaker back (bsp_audio_acquire()).
 */
void bsp_audio_release(void);
/**
 * @brief Blocking write of interleaved L/R 16-bit frames.
 * @param[in] stereo_frames  the frames
 * @param[in] frames  how many
 * @param[in] timeout_ms  how long to wait
 * @return ESP_OK, or why not
 */
esp_err_t bsp_audio_write(const int16_t *stereo_frames, size_t frames, uint32_t timeout_ms);
/**
 * @brief Speaker volume.
 * @param[in] percent  0..100 (0 = mute, 100 = 0 dB codec gain)
 */
void bsp_audio_set_volume(int percent);
/**
 * @brief The speaker's sample rate.
 * @return Hz
 */
uint32_t bsp_audio_sample_rate(void);

/**
 * @brief Microphones (ES7210, MIC1 + MIC2) on the same I2S port, at the speaker's
 * sample rate: 16-bit stereo frames (L = MIC1, R = MIC2). Call after
 * bsp_audio_init.
 * @return ESP_OK, or why not
 */
esp_err_t bsp_mic_start(void);
/**
 * @brief Stops the microphones.
 */
void bsp_mic_stop(void);
/**
 * @brief Reads the microphones: 16-bit stereo frames (L = MIC1, R = MIC2).
 * @param[out] stereo_frames  gets them
 * @param[in] frames  room
 * @param[out] got_frames  gets how many came
 * @param[in] timeout_ms  how long to wait
 * @return ESP_OK, or why not
 */
esp_err_t bsp_mic_read(int16_t *stereo_frames, size_t frames, size_t *got_frames, uint32_t timeout_ms);

/* ---------------------------------------------------------------- power */
/** @brief Backlight, speaker amp, panel and Wi-Fi chip off (held through sleep), then
 * deep sleep with no wake-up source: only the RESET button (or a power cycle)
 * starts the board again. Close files / unmount the card first. Never returns. */
void bsp_power_off(void) __attribute__((noreturn));

#define BSP_SAVE_CPU_MHZ 180     /**< the CPU while power saving, MHz (90: the DPI DMA underruns, the memory clock follows the CPU) */
/**
 * @brief Power saving while the screen is dark: backlight off, panel asleep, CPU
 *        clocked down (BSP_SAVE_CPU_MHZ). false: CPU back to full speed, panel awake;
 *        the caller then sets the backlight. Call from the LVGL task.
 * @param[in] on  saving
 */
void bsp_power_save(bool on);
/**
 * @brief Power saving is on.
 * @return true if so
 */
bool is_bsp_power_saving(void);
/**
 * @brief While power saving: the CPU at full speed for a while (true) or back to BSP_SAVE_CPU_MHZ
 * (false); the panel stays asleep. Does nothing when not saving. (devctl genbench)
 * @param[in] on  full speed
 */
void bsp_power_cpu_full(bool on);

#ifdef __cplusplus
}
#endif

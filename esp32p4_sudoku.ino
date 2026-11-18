/*
 * Sudoku for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
 * (720x720 MIPI-DSI + GT911 touch, ES8311 speaker), built with LVGL 9.
 *
 * 1600 puzzles from the OpenSudoku collections (https://opensudoku.gitlab.io/)
 * are compiled into flash (src/puzzles_data.c, made by tools/gen_puzzles.py).
 * Best times and the game in progress are kept in NVS.
 *
 * Arduino IDE settings (Tools menu):
 *   Board:            ESP32P4 Dev Module (esp32 core 3.3.x)
 *   PSRAM:            Enabled                 <- required (LVGL draw buffers)
 *   Flash Size:       32MB (256Mb)
 *   Partition Scheme: 32M Flash (13MB APP/6.75MB SPIFFS)
 *   Chip Variant:     match your chip (most 4B boards ship pre-v3.00 silicon)
 *
 * Libraries: lvgl 9.6.0 in ~/Arduino/libraries/lvgl,
 *            lv_conf.h in ~/Arduino/libraries/lv_conf.h.
 */
#ifndef BOARD_HAS_PSRAM
#error "Enable PSRAM in the Tools menu"
#endif

#include <lvgl.h>
#include "src/bsp/bsp_board.h"
#include "src/store.h"
#include "src/sudoku_ui.h"
#include "src/sfx.h"
#include "src/devctl.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

/* A firmware installed from update.bin (Settings > Data) starts on probation:
 * the Arduino core would keep it at once; instead it is kept once the board
 * has run for a while (loop() below). If it crashes before, the next restart
 * goes back to the previous firmware (the bootloader's rollback). */
extern "C" bool verifyRollbackLater()
{
  return true;
}
#define KEEP_FIRMWARE_AFTER_MS 15000

/* Why the chip last restarted, if it was not a normal power-on/reset. */
static const char *abnormal_reset_text(esp_reset_reason_t r)
{
  switch (r) {
    case ESP_RST_BROWNOUT: return "Restarted after a power dip (brownout). Try a stronger USB supply or lower the volume.";
    case ESP_RST_PANIC:    return "Restarted after a crash (panic). The backtrace is on the serial port.";
    case ESP_RST_INT_WDT:  return "Restarted by the interrupt watchdog.";
    case ESP_RST_TASK_WDT: return "Restarted by the task watchdog.";
    case ESP_RST_WDT:      return "Restarted by a watchdog.";
    default:               return NULL;
  }
}

void setup()
{
  /* malloc() of 64 bytes or more goes to PSRAM (the core default is 4096):
   * LVGL's widgets and SQLite then stay out of the 768 KB internal RAM, which
   * the Wi-Fi link (ESP-Hosted DMA buffers) and the H.264 encoder (one ~52 KB
   * block for MP4 export) need. Wi-Fi, lwIP, USB and FreeRTOS ask for internal
   * RAM explicitly and are not affected. */
  heap_caps_malloc_extmem_enable(64);
  Serial.setRxBufferSize(2048);      /* devctl uploads: 12 data lines between ACKs */
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32-P4 Sudoku");
  esp_reset_reason_t reset = esp_reset_reason();
  Serial.printf("Reset reason: %d\n", (int)reset);

  esp_err_t err = bsp_lvgl_start();
  if (err != ESP_OK) {
    Serial.printf("BSP init failed: %s\n", esp_err_to_name(err));
    while (true) delay(1000);
  }
  Serial.printf("Panel: %s\n", bsp_display_panel_kind() == BSP_PANEL_JD9365 ? "JD9365DA" : "ST7703");

  store_init();
  err = bsp_sdcard_host_init();     /* before Wi-Fi: they share the SDMMC host */
  if (err != ESP_OK) {
    Serial.printf("microSD host init failed: %s\n", esp_err_to_name(err));
  }
  err = sfx_init();
  if (err != ESP_OK) {
    Serial.printf("Speaker init failed, no sound: %s\n", esp_err_to_name(err));
  }
  sudoku_ui_start();

  Serial.println("DEVCTL ready");    /* serial control (devctl.h): screenshots, touches */
  const char *why = abnormal_reset_text(reset);
  if (why) {
    Serial.println(why);
    sudoku_ui_notice(why, 15000);
  }
}

void loop()
{
  static bool kept;
  if (!kept && millis() > KEEP_FIRMWARE_AFTER_MS) {   /* started up and ran fine: this firmware stays */
    kept = true;
    sudoku_ui_mark_ok();
  }
  devctl_poll();                     /* commands from tools/devctl.py, if any */
  uint32_t wait_ms = lv_timer_handler();
  delay(wait_ms < 5 ? 1 : (wait_ms > 20 ? 20 : wait_ms));
}

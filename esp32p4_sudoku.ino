/*
 * Sudoku for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
 * (720x720 MIPI-DSI + GT911 touch), built with LVGL 9.
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

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32-P4 Sudoku");

  esp_err_t err = bsp_lvgl_start();
  if (err != ESP_OK) {
    Serial.printf("BSP init failed: %s\n", esp_err_to_name(err));
    while (true) delay(1000);
  }
  Serial.printf("Panel: %s\n", bsp_display_panel_kind() == BSP_PANEL_JD9365 ? "JD9365DA" : "ST7703");

  store_init();
  sudoku_ui_start();
}

void loop()
{
  uint32_t wait_ms = lv_timer_handler();
  delay(wait_ms < 5 ? 1 : (wait_ms > 20 ? 20 : wait_ms));
}

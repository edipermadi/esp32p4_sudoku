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
 *   Partition Scheme: Custom                <- partitions.csv here: two 4.4 MB app
 *                                              slots and the avatars' own 7 MB
 * The avatars' pixels go into their partition once: from the release's flash files,
 * esptool (docs/building.md), or avatars.bin on the card (extras/avatars.bin).
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
#include "src/ui_sudoku.h"
#include "src/sfx.h"
#include "src/devctl.h"
#include "src/gibberlink.h"
#include "src/avatar_store.h"
#include "src/settings_core.h"
#include "src/stats_db.h"
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
    case ESP_RST_BROWNOUT:
        return "Restarted after a power dip (brownout). Try a stronger USB supply or lower the volume.";
    case ESP_RST_PANIC:
        return "Restarted after a crash (panic). The backtrace is on the serial port.";
    case ESP_RST_INT_WDT:
        return "Restarted by the interrupt watchdog.";
    case ESP_RST_TASK_WDT:
        return "Restarted by the task watchdog.";
    case ESP_RST_WDT:
        return "Restarted by a watchdog.";
    default:
        return NULL;
    }
}

/* loop() runs LVGL and everything the screens do, the sudoku solver's recursion
 * among it (up to ~90 bytes a level, a level per empty cell): 8 KB (the default)
 * overflowed when Continue re-solved a hard Hyper puzzle under a devctl tap. */
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup()
{
    /* malloc() of 4 bytes or more goes to PSRAM (the core default is 4096):
     * LVGL's widgets and SQLite then stay out of the 768 KB internal RAM, which
     * the Wi-Fi link (ESP-Hosted DMA buffers), the SD card's DMA buffers and the
     * H.264 encoder (one ~52 KB block for MP4 export) need. Wi-Fi, lwIP, USB and
     * FreeRTOS ask for internal RAM explicitly and are not affected. (64, then
     * 16, were enough once; with the screens grown, LVGL's many small pieces
     * filled internal RAM: at 16, 4700 blocks of 4-15 bytes, the largest free
     * block 35 KB, and MP4 export failed. At 4: 225 KB free, largest 143 KB, and
     * screen redraws just as fast (devctl bench).) */
    heap_caps_malloc_extmem_enable(4);   /* (more in SRAM, e.g. 256: the SD card and Wi-Fi starve of DMA RAM) */
    Serial.setRxBufferSize(2048);      /* devctl uploads: 12 data lines between ACKs */
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32-P4 Sudoku");
    esp_reset_reason_t reset = esp_reset_reason();
    Serial.printf("Reset reason: %d\n", (int)reset);

    esp_err_t err = bsp_lvgl_start();
    if (err != ESP_OK) {
        Serial.printf("BSP init failed: %s\n", esp_err_to_name(err));
        while (true) {
            delay(1000);
        }
    }
    Serial.printf("Panel: %s\n", bsp_display_panel_kind() == BSP_PANEL_JD9365 ? "JD9365DA" : "ST7703");

    avatars_init();                    /* the avatars' pixels in flash (else blank until installed) */
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

/* The avatars, if they aren't in flash: installed from avatars.bin on the card, once the card is
 * mounted (sudoku.db open), with the screen dark meanwhile (writing the flash makes the panel
 * flicker); it comes back with them. */
static void avatars_poll(void)
{
    static uint32_t next, dark_at;
    static bool tried;
    if (millis() < next) {
        return;
    }
    next = millis() + 500;
    if (!tried && !is_avatars_ready() && is_stats_db_ready()) {
        tried = true;
        if (is_avatars_installable()) {
            sudoku_ui_flash_busy("Installing the avatars",
                                 "From avatars.bin on the card. The screen goes dark meanwhile (about a minute), "
                                 "then comes back with them.");
            dark_at = millis() + 3000;
        }
    }
    if (dark_at && millis() >= dark_at) {
        dark_at = 0;
        sudoku_ui_flash_dark();
        if (!avatars_install()) {
            sudoku_ui_flash_done();
            sudoku_ui_notice("Couldn't install the avatars from avatars.bin.", 5000);
        }
    }
    int st = avatars_install_poll(NULL, NULL);
    if (st == 2 || st == 3) {
        sudoku_ui_flash_done();    /* (the screen is redrawn as it comes back) */
    }
    if (st == 2) {
        sudoku_ui_notice("Avatars installed.", 3000);
    } else if (st == 3) {
        sudoku_ui_notice("Couldn't install the avatars from avatars.bin.", 5000);
    }
}

void loop()
{
    static bool kept;
    if (!kept && millis() > KEEP_FIRMWARE_AFTER_MS) {   /* started up and ran fine: this firmware stays */
        kept = true;
        fw_mark_running_ok();
    }
    devctl_poll();                     /* commands from tools/devctl.py, if any */
    gibberlink_poll();                 /* commands heard as sound (Settings > Sound > Gibberlink) */
    avatars_poll();
    uint32_t wait_ms = lv_timer_handler();
    /* screen dark (bsp_power_save): poll every 50 ms, still quick enough for the waking tap */
    uint32_t max_ms = is_bsp_power_saving() ? 50 : 20;
    delay(wait_ms < 5 ? 1 : (wait_ms > max_ms ? max_ms : wait_ms));
}

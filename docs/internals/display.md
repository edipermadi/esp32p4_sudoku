# Display and the flash

[← Internals](README.md) · [README](../../README.md) · [Features](../features.md) · [Data & storage](../data.md) · [Building](../building.md) · [Development](../development.md) · [Remote control](../devctl.md)

How a frame gets from LVGL to the 720×720 panel, what makes the panel blink, and why the screen goes dark while the flash is written. Most of this was found by measuring on the board; the numbers are kept so they aren't rediscovered the hard way.

## The pipeline

```
LVGL draws (PPA + software)  →  draw buffer (PSRAM)  →  esp_lcd DPI frame buffer (PSRAM)  →  MIPI-DSI, 2 lanes  →  panel
```

- **The panel**: an ST7703 or a JD9365DA (boards ship with either), told apart by its ID at start-up (`src/bsp/bsp_display.c`), each with Waveshare's init sequence. It is a **DPI** (video mode) panel: the P4's DSI controller streams the whole frame buffer to it continuously, by DMA, from PSRAM. One frame buffer (`num_fbs = 1`); copies into it use the 2D-DMA.
- **LVGL** (`src/bsp/bsp_lvgl.c`) renders in partial mode into **two full-screen draw buffers in PSRAM** (720 lines each, RGB565). Its flush copies the drawn area into the DPI frame buffer (`esp_lcd_panel_draw_bitmap()`); the copy's completion callback tells LVGL the buffer is free.
- **The PPA draw unit** (`LV_USE_PPA`, `LV_USE_PPA_IMG` in the shared `lv_conf.h`): fills and image blits go to the P4's Pixel Processing Accelerator.

### Why these choices (measured with devctl `bench`, full-screen redraws, 2026-11-19)

| Choice | Result |
|---|---|
| PPA draw unit on | 6–34 % faster |
| Two software draw units with an OS layer (`LV_OS_FREERTOS`, `LV_DRAW_SW_DRAW_UNIT_CNT 2`) | 14–40 % **slower**, even with the PPA; so `LV_OS_NONE`, one unit |
| Full-screen PSRAM buffers vs 80-line strips | 13–33 % faster |
| Small strips in internal SRAM | slower still: the per-strip overhead of objects and text dominates |

A plain CPU `memcpy` from PSRAM to PSRAM runs at about 46 MB/s at 200 MHz (cache-line latency), so bulk pixel work goes to DMA or the PPA. The shared `lv_conf.h` needs two shims for the PPA with IDF 5.5.5 (`CONFIG_LV_DRAW_BUF_ALIGN`, `esp_cache_get_line_size_by_addr()`); `LV_USE_DRAW_DMA2D` is STM32's, not the P4's.

## What makes the panel blink

The frame buffer lives in PSRAM and the DSI controller must read all of it, every frame, on time. Anything that keeps PSRAM or the display's interrupts away from it for a few milliseconds shows: the whole panel flashes **cyan** for a frame.

| Cause | Why | What the firmware does |
|---|---|---|
| **Writing or erasing the flash** | PSRAM and the flash share the cache; a flash operation stops the cache, so the DMA can't fetch the frame | no flash writes during normal use (settings, games and statistics live on the card, not in NVS); the screen goes dark for the few big writes (below) |
| **Holding interrupts off on core 1** for milliseconds | core 1 serves the display's interrupts | nothing long runs on core 1 with interrupts off; see [Tasks and cores](tasks.md#the-rules) |
| A PSRAM heap walk on core 1 | `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)` walks 28 MB under a lock: 4 ms | System monitor doesn't show PSRAM's largest block; devctl `heap` still does, on demand |
| `uxTaskGetSystemState()` on core 1 | holds the kernel lock: 1.5 ms | the task list is read by a task on core 0 |

Ruled out while hunting (2026-11-20): PSRAM bandwidth (SRAM strips blinked more), the 2D-DMA, the temperature sensor, the SD card's free-space count, the number of dirty areas.

## Flash writes

Two things write the flash in bulk: a **firmware update** (`update.bin` into the other app slot, `src/fw_update.c`) and the **avatars' install** (7 MB into their partition, `src/avatar_store.c`). Erasing sector by sector instead of all at once still flickered, and took twice as long. So, for both (`src/ui_flash_busy.c`):

1. a card says what is happening, for 3 s;
2. **dark**: `sudoku_ui_flash_dark()` turns the backlight off and puts the panel to sleep, and a timer keeps the screen timeout away;
3. the write runs (on core 0); the board looks off;
4. `sudoku_ui_flash_done()` redraws the screen first, then wakes the panel and puts the light back.

Nothing else writes the flash while the UI runs. NVS holds only the Wi-Fi network; Wi-Fi's own writes happen when a network is saved.

## The flash's layout

32 MB on the board, but only the first **16 MB can be mapped**: the core's prebuilt bootloader and IDF use 24-bit flash addressing, so an image or a mapped data partition past `0x1000000` fails (`flash_mmap: Address ... out of range for 24bit flash mapping`). `partitions.csv` therefore keeps two 4.44 MB app slots and the 7 MB `avatars` partition (at `0x8F0000`) below that line. See [Building](../building.md#the-flash-layout).

## Power saving and the panel

When the screen saver goes dark, the panel is put to sleep (DCS display-off, sleep-in) while the DPI stream keeps running, and the CPU drops to 180 MHz; 90 MHz underruns the display's DMA, since the memory clock follows the CPU. See [Start-up and power](startup-power.md#the-screen-saver-and-power-saving).

## Screenshots and video frames

- A **screenshot** copies the DPI frame buffer as it is (`bsp_display_frame()`), then a task on core 0 widens it to RGB888 and JPEG-encodes it in hardware: given RGB565, the encoder widens by shifting alone and white came out (236, 245, 244), green-tinged.
- **MP4 export** renders on a second, invisible LVGL display and converts its frames with the PPA; see [Replay and video](replay-video.md).
- LVGL's performance monitor, when compiled in, shows on every display: it is hidden on the export's.
- devctl screenshots get garbled while the host is busy compiling (the serial reader falls behind); on-board timings aren't affected.

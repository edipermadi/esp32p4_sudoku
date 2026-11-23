# Memory

[← Internals](README.md) · [README](../../README.md) · [Features](../features.md) · [Data & storage](../data.md) · [Building](../building.md) · [Development](../development.md) · [Remote control](../devctl.md)

Where the firmware's memory goes: the ESP32-P4's 768 KB of internal RAM is the tight resource, its 32 MB of PSRAM is plenty. Some hardware can only use internal RAM, and needs it in one piece; everything else is kept out of it.

## Who needs internal RAM

| User | Needs | Notes |
|---|---|---|
| The H.264 encoder (MP4 export) | one block of width / 16 × 1152 bytes: **~52 KB** at 720 px | "No memory for reference frame" when internal RAM is in pieces |
| The microSD card's DMA buffers | DMA-capable internal RAM | a Files rescan once failed (`allocate_dma_buf: not enough mem`) |
| Wi-Fi (ESP-Hosted's SDIO buffers, lwIP) | DMA-capable internal RAM | asks for it itself |
| tinyh264 (playing a video) | **~55 KB** while a video is open | |
| FreeRTOS tasks' stacks, USB, the drivers | internal RAM | asked for explicitly |

## Keeping it free: `malloc()` goes to PSRAM

`setup()` calls `heap_caps_malloc_extmem_enable(4)` first thing: every `malloc()` of **4 bytes or more** goes to PSRAM (the Arduino core's default is 4096). LVGL's widgets (thousands of small objects) and SQLite then stay out of internal RAM. Code that needs internal RAM asks for it with `heap_caps_malloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)`, which the threshold doesn't affect.

The threshold went down in steps as the screens grew:

| Threshold | Internal RAM at Home | What happened |
|---|---|---|
| 4096 (default) | 55 KB free, 17 KB DMA | LVGL and SQLite filled it |
| 64 | 173 KB free, 134 KB DMA | fine, until more screens came |
| 16 | 67 KB free, largest block 31 KB, then 35 KB | 4700 blocks of 4–15 bytes from LVGL: MP4 export failed |
| **4** (now) | **~225 KB free, largest block ~143 KB** | export, Wi-Fi and the card all fine; screen redraws just as fast (devctl `bench`) |

Never "reserve" internal RAM at start-up for later: a 64 KB reserve made ESP-Hosted's buffer allocation fail, and Wi-Fi never came up.

## The big buffers, all in PSRAM

| Buffer | Size | Owner |
|---|---|---|
| The display's DPI frame buffer | 720 × 720 × 2 = 1 MB | esp_lcd (`src/bsp/bsp_display.c`) |
| LVGL's two draw buffers + a rotation buffer | 3 × 1 MB | `src/bsp/bsp_lvgl.c` |
| The viewer's two picture buffers | 2 × 1 MB, kept once made | `src/media_core.c` |
| A JPEG being shown | the file (≤ 16 MB) and its decoded pixels (≤ 24 MB) | `src/media_jpeg.c` |
| MP4 export's frame (RGB888) and the invisible display's draw buffer | ~1.5 MB and more, while exporting | `src/replay_video.c`, `src/ui_replay_export.c` |
| A screenshot's copy and its RGB888 | 1 MB + 1.5 MB, while writing | `src/screenshot_core.c` |
| Undo and redo | 2 × 128 boards (31 KB each side) | `src/ui_sudoku.c` |
| The generated-puzzle cache | 24 puzzles with their layouts | `src/puzzles_lookup.c` |
| Move logs | 2048 moves × 8 bytes (16 KB) each, a few copies | `src/ui_game.c`, `src/store_game.cpp`, `src/stats_db_games.c` |
| `sudoku.db`'s answers (statistics, History, Trophies, Gameplays ...) | a few to a few hundred KB each | `src/stats_db_*.c` (`sdb_psram_buf()`) |
| SQLite's page cache | | `src/sqlite/` |
| The avatars | 1867 × 36 px images | in flash, mapped, not RAM (`src/avatar_store.c`) |

## Rules of thumb

- **Big buffers: `MALLOC_CAP_SPIRAM`** (and 64-byte aligned for anything a DMA or the PPA reads: `heap_caps_aligned_alloc(64, ...)`, and flush the cache after writing it, `esp_cache_msync()`).
- **Don't rebuild every screen as a reaction to data** (`apply_theme()`): for a moment there are two copies of all of them. New collections refresh only the Puzzles screen. See [UI structure](ui-structure.md#the-screens-and-the-theme).
- **Big local arrays go `static`** on the LVGL task: its stack is 16 KB, and the solver's recursion already uses a good part of it.
- **Check with devctl `heap`** (`heap 1`: per region, with `alloc_blocks` and `largest_free_block`), or System monitor > Memory. About 225 KB free and a ~143 KB largest block at Home is normal; if it creeps down, count the main region's `alloc_blocks` before blaming a new feature.
- **Don't walk the PSRAM heap on core 1** (`heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)` takes 4 ms with interrupts held off): the panel blinks. See [Display and the flash](display.md#what-makes-the-panel-blink).

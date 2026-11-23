# Replay and video

[← Internals](README.md) · [README](../../README.md) · [Features](../features.md) · [Data & storage](../data.md) · [Building](../building.md) · [Development](../development.md) · [Remote control](../devctl.md)

How a solved game is played back on the Replay screen, exported as an MP4, and how the Files viewer plays MP4s (and shows JPEGs). Four modules make this up; this page ties them together.

## From a game to a move log

While a game is played, every change of a cell's digit is logged with the game clock and what made it (the pad, Fill, Erase, Undo, Redo) in RAM (`record_move()` in `src/ui_game.c`). The log is saved with the game in progress and, once solved, kept in `solves.moves`: 6 bytes a move, at most 2048 moves ([the format](db/solves.md#the-move-log)). Pencil marks aren't logged.

## The Replay screen

`src/replay_timeline.c` (no LVGL) plays a log back over the puzzle's givens: the board at step *k* is the givens plus moves 0 … *k*−1, so any point shows at once (start, back, forward, end, the slider). Playing follows the game clock at 1×, 2×, 4× or 8×; a pause longer than 2 s between two moves (`RP_MAX_GAP_MS`) plays as 2 s. `src/ui_replay.c` draws it with the game screen's board drawing, the wrong digits in red. A solve is loaded from `sudoku.db` by its id (`stats_db_query_replay()`), from Gameplays, History or the Solved dialog.

## Exporting an MP4

Gameplays > a game's **MP4** button (`src/ui_replay_export.c`, `src/replay_video.c`):

```
the Replay screen on an invisible 720×720 LVGL display (renders into the video's frame buffer, never shown)
  → each frame: RGB888 (720×720)
  → PPA scale-rotate-mirror unit: RGB888 → YUV 4:2:0 (O_UYY_E_VYY), by DMA
  → ESP32-P4 hardware H.264 encoder
  → mp4_writer: an MP4 on the card, replays/<name>.mp4
```

- **The video**: 15 fps (`RX_FPS`), the start held for 1 s, the moves at 2× the replay (pauses already shortened), the solved board held for 3 s. A key frame every 2 s (so seeking works), bit rate width × height × 2, QP 18–32.
- **Only changed frames are converted**: a frame where nothing moved reuses the last conversion.
- **RGB888, not RGB565, into the PPA**: given RGB565 it widens 5- and 6-bit channels by shifting alone, so white became (248, 252, 248) and the video had a green cast. LVGL's RGB888 is bytes B, G, R, which matches the PPA's RGB888 without swapping. Moving the conversion from the CPU to the PPA took an export from 18.8 to 15.4 s.
- **Memory**: the encoder needs one block of internal RAM of width / 16 × 1152 bytes (about 52 KB at 720 px), which is why `malloc()` goes to PSRAM from 4 bytes up ([Memory](memory.md)); with internal RAM in small pieces the export failed with "No memory for reference frame".
- **The writer** (`src/mp4_writer.c`, plain stdio): `ftyp`, then `mdat` with every frame (Annex-B start codes replaced by lengths; SPS and PPS taken from the stream for `avcC`), then `moov` at the end with the sample tables. Built and tested on a PC too.
- A progress dialog shows on the real screen meanwhile; the export runs in the UI task, as many frames as fit in 30 ms per LVGL timer tick, so the dialog keeps updating.

## Playing an MP4 (the Files viewer)

`src/media_core.c`, `src/media_video.c`, `src/ui_media_view.c`:

```
mp4_reader: the moov box → each frame's offset, size, time, key frame; SPS/PPS as Annex-B
  → worker task "media" (core 0): reads a frame, feeds tinyh264 (software H.264 decoder) → I420
  → converter task "media_cv" (core 1): I420 → RGB565, scaled to fit 720×720, only the rows that changed
  → the back buffer, handed to the page (two 720×720 RGB565 buffers in PSRAM)
  → the page's 10 ms timer shows it as an LVGL image
```

- **Two buffers**: the worker fills the one not shown, flushes it from the cache (the PPA draw unit reads images by DMA) and hands it over; only after the page shows it may the worker fill the other. The worker never touches LVGL.
- **Decoding and converting overlap**: the decoder (core 0) works on the next frame while the converter (core 1) converts this one. When still behind, frames are decoded but not shown, so the video keeps its speed.
- **Pacing** follows the frames' times. **Seeking** decodes from the key frame before the target and shows only the target.
- **The colour conversion** uses lookup tables (BT.601 limited range) built once, clamped through a table, and converts only the rows that changed since the buffer was last filled.
- tinyh264 takes about 55 KB of internal RAM while a video is open; videos up to 1920×1088 play.

## Pictures

`src/media_jpeg.c`: a JPEG is read whole (up to 16 MB), decoded by the P4's JPEG engine to RGB565 at full size (up to 24 MB decoded, in PSRAM), then scaled down to fit by nearest pixel into the back buffer. Screenshots are JPEGs written by the same engine in reverse ([Display and the flash](display.md#screenshots-and-video-frames)).

## Files and where they go

| File | Written by | Read by |
|---|---|---|
| `replays/<player>_<puzzle>_<date-time>.mp4` | Gameplays > **MP4** | the Files viewer, a computer |
| `screenshots/YYYYMMDD-HHMMSS.jpg` | the camera buttons | the Files viewer, a computer |

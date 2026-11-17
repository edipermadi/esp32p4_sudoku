# esp32p4_sudoku

A touch-screen Sudoku for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4B** (4" 720×720 MIPI-DSI, GT911 touch), built with LVGL 9.6 as an Arduino sketch.

## Features

- **1600 puzzles in flash**: the 16 OpenSudoku collections from <https://opensudoku.gitlab.io/>. These are Easy, Medium, Hard and Very Hard, plus Gnome Sudoku sets 1–3 at each of those levels. Each puzzle is packed into 41 bytes, about 64 KB in total. Every puzzle was checked to have exactly one solution.
- **Puzzle list**
  - Collections are on the left, each with its solved count.
  - Puzzles 1–100 are on the right. Solved ones are green and the game in progress is orange.
  - Each collection shows its fastest and average solve time.
  - A **Continue** button resumes the game in progress.
- **Game**
  - The selected cell's row, column and box are highlighted, and so is every cell with the same digit.
  - Pencil marks (Notes). Placing a digit removes it from the notes in its row, column and box.
  - Undo (128 steps), Erase and Restart.
  - The number pad shows how many of each digit are still to place. A digit that is complete is dimmed.
- **Solve indicator**
  - Conflicting digits turn red, and the status line counts them.
  - When the board is solved, it turns green and a "Solved!" dialog opens with the time and your best.
- **Time to solve**
  - The timer counts only while the game screen is open. Tap it to pause, which also hides the board.
  - The best time for each puzzle is stored.
- **Saved state**: best times and the game in progress (entries, notes and elapsed time) are kept in NVS. While you play, the game is saved every 10 s and again when you leave it.

## Building

1. Install **esp32 Arduino core 3.3.x**.
2. Install **LVGL 9.6.0** and put `lv_conf.h` at `~/Arduino/libraries/lv_conf.h`. It needs Montserrat 12–28 enabled.
3. Choose these settings in the Tools menu:

   | Setting          | Value                                   |
   |------------------|-----------------------------------------|
   | Board            | ESP32P4 Dev Module                      |
   | PSRAM            | Enabled (required)                      |
   | Flash Size       | 32MB (256Mb)                            |
   | Partition Scheme | 32M Flash (13MB APP/6.75MB SPIFFS)      |
   | Chip Variant     | Match your chip (most 4B boards are pre-v3.00) |

   Or build from the command line:
   ```
   arduino-cli compile -b esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=app13M_data7M_32MB,ChipVariant=prev3 .
   ```

4. Upload through the USB-UART port. Serial output is at 115200 baud.
   ```
   arduino-cli upload -b esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=app13M_data7M_32MB,ChipVariant=prev3 -p /dev/ttyACM0 .
   ```

   Best times and the saved game live in NVS, so they survive a normal upload. Enable "Erase All Flash Before Sketch Upload" to reset them.

## How to play

| Action                          | How                                              |
|---------------------------------|--------------------------------------------------|
| Pick a collection               | Tap it in the left-hand list                     |
| Start or resume a puzzle        | Tap its number, or tap **Continue** at the top   |
| Select a cell                   | Tap it                                           |
| Enter a digit                   | Tap 1–9 on the right. Tap the same digit again to clear it |
| Pencil marks                    | Switch **Notes** on, then tap digits to toggle marks |
| Clear a cell                    | **Erase**                                        |
| Take back a move                | **Undo**                                         |
| Start the puzzle over           | **Restart** (asks first if there is progress)    |
| Pause                           | Tap the timer; tap the board to resume           |
| Back to the list                | **List** (top left); the game is saved           |

The small number on each pad button is how many of that digit are still to place. Red digits break a row, column or box rule. The puzzle is solved when every cell is filled and nothing is red. Each puzzle has exactly one solution, so the solution is always the intended one.

Only one unfinished game is kept. Starting a different puzzle asks whether to resume the saved one or discard it.

## Regenerating data

**Puzzles**: the `.opensudoku` files are in `tools/opensudoku/`. To change the embedded set, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run:
```
python3 tools/gen_puzzles.py
```
This rewrites `src/puzzles_data.c` and `src/puzzles_count.h`. It stops with an error if a puzzle is malformed or doesn't have exactly one solution. The download command for the source files is in the script's header. The list screen shows at most 100 puzzles per collection.

**Fonts**: the grid digits and the timer use Montserrat Medium, converted with lv_font_conv 1.5.2:
```
FONT=~/Arduino/libraries/lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf
npx lv_font_conv --no-compress --no-prefilter --bpp 4 --size 44 --font $FONT \
    -r 0x20,0x30-0x39 --format lvgl --lv-include lvgl.h -o src/fonts/font_digits_44.c
npx lv_font_conv --no-compress --no-prefilter --bpp 4 --size 36 --font $FONT \
    -r 0x20,0x30-0x3A --format lvgl --lv-include lvgl.h -o src/fonts/font_timer_36.c
```

## Layout

```
esp32p4_sudoku.ino      setup/loop
src/sudoku_ui.c         list + game screens
src/store.cpp           NVS: best times, saved game, last collection
src/puzzles.h           puzzle access (unpack, collection lookup)
src/puzzles_data.c      generated puzzle data
src/fonts/              Montserrat 44 px digits / 36 px timer (lv_font_conv)
src/bsp/                display (ST7703 / JD9365), GT911 touch, LVGL port
tools/gen_puzzles.py    regenerates src/puzzles_data.c from tools/opensudoku/
tools/opensudoku/       the downloaded .opensudoku collections
```

To change the embedded puzzles, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run `python3 tools/gen_puzzles.py`.

## Credits

The puzzles come from the [OpenSudoku](https://opensudoku.gitlab.io/) collections, which include puzzles from Gnome Sudoku. The board support code is shared with the `esp32p4_86box` project.

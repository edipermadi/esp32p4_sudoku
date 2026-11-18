# esp32p4_sudoku

A touch-screen Sudoku for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4B** (4" 720×720 MIPI-DSI, GT911 touch, ES8311 speaker), built with LVGL 9.6 as an Arduino sketch.

## Features

- **1600 puzzles in flash**: the 16 OpenSudoku collections from <https://opensudoku.gitlab.io/>. These are Easy, Medium, Hard and Very Hard, plus Gnome Sudoku sets 1–3 at each of those levels. Each puzzle is packed into 41 bytes, about 64 KB in total. Every puzzle was checked to have exactly one solution.
- **Home screen**: the time and date at the top (once Wi-Fi has fetched it), the solved count and big buttons. **Continue** resumes the game in progress and only appears when there is one. **Puzzles**, **Statistics** and **Settings** open those screens.
- **Puzzle list**
  - Collections are in a row along the top, each with its solved count. Swipe the row sideways, or tap the ‹ › arrows at its ends, to see them all. An arrow only shows while there are more collections that way.
  - Puzzles 1–100 fill the screen below it. Solved ones are green and the game in progress is orange.
  - Each collection shows its fastest and average solve time.
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
- **Statistics**
  - A mistake is a placed digit that breaks a row, column or box rule: the ones that buzz and turn red. Undo doesn't take a mistake back. The count is saved with the game in progress and shown in the "Solved!" dialog.
  - **All levels** compares the 16 levels: puzzles solved, fastest and average best time, and average mistakes.
  - Each level lists its solved puzzles with the best time, the fewest mistakes in any solve, and how many times it was solved.
  - Above the table: solved count, puzzles solved without a mistake, total solves, fastest and average time.
  - Puzzles solved before this version show "-" for mistakes and count as solved once.
- **Screen timeout**: after a while without a touch the game pauses, the screen goes black and the backlight turns off, so a still image can't burn in. Tap anywhere to wake it. The waking tap doesn't press anything. Set the timeout under **Settings → Screen off after**: 1, 2, 5 (default), 10, 15 or 30 minutes, or Never.
- **Sound effects**: synthesised on the device, with no audio files. A background task mixes up to four at once.
  - A short tick on every touch.
  - A low buzz when a digit you place breaks a row, column or box rule.
  - A rising chime when all nine of a digit are placed without conflicts.
  - A fanfare when the puzzle is solved.
  - Turn them on or off under **Settings → Sound effects**.
- **Light and dark mode**: switch between colour schemes under **Settings → Dark mode**.
- **Wi-Fi and clock**: **Settings → Wi-Fi** opens a screen with an on/off switch, nearby networks (tap one; secured networks ask for the password on an on-screen keyboard) and **Forget**. Once connected, the time comes from NTP (pool.ntp.org) and the home screen shows it. The board remembers the network and reconnects at start-up. Wi-Fi goes through the board's ESP32-C6 co-processor (ESP-Hosted over SDIO); the Wi-Fi code is shared with the `esp32p4_86box` project.
- **Time zone**: **Settings → Time zone** lists IANA names such as `Asia/Dubai`, with daylight saving where the zone has it.
- **Game data on the microSD card**: statistics, the game in progress and the settings are kept in `sudoku.db`, a SQLite 3 database in the root of a FAT32 microSD card that is inserted at start-up. Nothing is written to the board's flash for them; only the Wi-Fi network and on/off switch stay on the board (NVS). At start-up the board shows "Starting..." while it opens the database (about a second), then builds the screens with its settings. The **Statistics** screen reads the database with SQL queries; the puzzle list, the home screen and "New best!" use a copy in RAM loaded at start-up. Without a card, the game runs on defaults and keeps changes, the game in progress and solves only until power-off; the "Solved!" dialog warns about this and Settings shows it under **microSD card**. Data that earlier versions kept on the board (NVS) is copied in the first time a database is used (statistics are also merged at every start; merging again changes nothing). Open it on a computer with `sqlite3 sudoku.db`:
  - `solves`: every solve, with `puzzle` (0–1599), `level`, `number` (1–100), `seconds`, `mistakes` and `solved_at` (Unix time UTC; NULL until Wi-Fi has set the clock).
  - `puzzle_stats`: per puzzle `best_seconds`, `fewest_mistakes` (NULL if solved before mistakes were counted) and `solves`.
  - `level_stats` (view): per level solved, solves, fastest, average, average mistakes and solves without mistakes.
  - `saved_game`: the unfinished game (one row): `cells` holds 81 digits row by row (0 = empty, givens included), `notes` 81 little-endian 16-bit masks (bit d = pencil mark d), plus `elapsed_ms`, `mistakes` and `saved_at`.
  - `settings`: `dark_mode`, `sound`, `screen_off_minutes`, `time_zone`, `last_level`, and `migrated_from_nvs`.

  ```
  sqlite3 sudoku.db "SELECT level, number, seconds, mistakes, datetime(solved_at, 'unixepoch') FROM solves ORDER BY id DESC LIMIT 10"
  sqlite3 -box sudoku.db "SELECT * FROM level_stats"
  ```

  SQLite 3.54.1 is built in from the official amalgamation (`src/sqlite/`), with a small file layer for ESP-IDF's FAT filesystem. The board doesn't lock the file, so take the card out (or switch off) before opening the database elsewhere.
- **Format microSD card**: **Settings → microSD card → Format** erases the card and gives it a fresh MBR with one primary FAT32 partition spanning the whole card (FatFs `f_mkfs`, cluster size chosen automatically, data area aligned to 4 MiB). It shows the card's name and size in a confirmation dialog first. The game itself doesn't use the card.
  - Insert the card before switching the board on. The card shares its SD controller with the Wi-Fi chip, and setting up a card while Wi-Fi runs breaks the Wi-Fi link and restarts the board. So the card is detected at start-up, before Wi-Fi starts, and a card inserted later is only picked up if Wi-Fi hasn't been switched on since start-up.
- **Settings** (dark mode, sound effects, screen timeout and time zone) are kept in `sudoku.db`; the Wi-Fi network is kept on the board, with the password stored unencrypted in NVS.
- **Saved state**: the game in progress (entries, notes, elapsed time and mistakes) is saved to `sudoku.db` when you leave it or pause. If power is cut mid-game, the moves since the last leave or pause are lost.

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

   Game data is on the microSD card, so uploads don't touch it; the Wi-Fi network survives a normal upload too. "Erase All Flash Before Sketch Upload" forgets the Wi-Fi network (and the old pre-database data).

## How to play

| Action                          | How                                              |
|---------------------------------|--------------------------------------------------|
| Open the puzzle list            | **Puzzles** on the home screen                   |
| Resume the game in progress     | **Continue** on the home screen                  |
| Pick a collection               | Tap it in the row at the top (swipe for more)    |
| Start or resume a puzzle        | Tap its number                                   |
| Select a cell                   | Tap it                                           |
| Enter a digit                   | Tap 1–9 on the right. Tap the same digit again to clear it |
| Pencil marks                    | Switch **Notes** on, then tap digits to toggle marks |
| Clear a cell                    | **Erase**                                        |
| Take back a move                | **Undo**                                         |
| Start the puzzle over           | **Restart** (asks first if there is progress)    |
| Pause                           | Tap the timer; tap the board to resume           |
| Back to the list                | **List** (top left); the game is saved           |
| Back to the home screen         | **Home** (top left of the list, statistics or settings) |
| See solve times and mistakes    | **Statistics** on the home screen; pick a level on the left |
| Sound on/off                    | **Settings → Sound effects**                     |
| Light / dark mode               | **Settings → Dark mode**                         |
| Screen timeout                  | **Settings → Screen off after**                  |
| Connect to Wi-Fi                | **Settings → Wi-Fi**, switch on, tap a network   |
| Time zone                       | **Settings → Time zone**                         |
| Format the microSD card         | **Settings → microSD card → Format**, confirm    |

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

## Changing the colours

Both colour schemes are tables at the top of `src/sudoku_ui.c`: `PAL_DARK` and `PAL_LIGHT`. They set the background, text, grid lines and each cell highlight state.

## Tuning the sounds

The effects are note tables at the top of `src/sfx.c`. Each note has a frequency, a length in ms, a waveform (sine, triangle or square) and a level. `SFX_VOLUME` sets the codec volume, 70 % by default.

## Layout

```
esp32p4_sudoku.ino      setup/loop
src/sudoku_ui.c         home, list, statistics, settings and game screens
src/store.cpp           RAM copy of statistics, saved game and settings; reads the old NVS keys once
src/stats_db.c          sudoku.db on the microSD card: statistics, saved game, settings, queries
src/sqlite/             SQLite 3.54.1 amalgamation + ESP-IDF file layer (sqlite3_esp.c)
src/wifi_mgr.cpp        Wi-Fi worker task (ESP-Hosted / C6), saved network, starts SNTP
src/wifi_ui.c           Wi-Fi screen: networks, password keyboard
src/timezones.c         IANA time zone names -> POSIX TZ rules
src/ui_palette.h        light/dark colour schemes shared by the UI files
src/sfx.c               sound effects: note tables + 4-voice mixer task
src/puzzles.h           puzzle access (unpack, collection lookup)
src/puzzles_data.c      generated puzzle data
src/fonts/              Montserrat 44 px digits / 36 px timer (lv_font_conv)
src/bsp/                display (ST7703 / JD9365), GT911 touch, ES8311 speaker, microSD, LVGL port
tools/gen_puzzles.py    regenerates src/puzzles_data.c from tools/opensudoku/
tools/opensudoku/       the downloaded .opensudoku collections
```

To change the embedded puzzles, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run `python3 tools/gen_puzzles.py`.

## Credits

The puzzles come from the [OpenSudoku](https://opensudoku.gitlab.io/) collections, which include puzzles from Gnome Sudoku. The board support code is shared with the `esp32p4_86box` project.

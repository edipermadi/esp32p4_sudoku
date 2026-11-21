# Development

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

Regenerating the embedded data, changing colours and sounds, and the source layout. How the parts work inside (starting with the [database design](internals/database.md)) is under [Internals](internals/README.md).

## Regenerating data

**Puzzles**: the `.opensudoku` files are in `tools/opensudoku/`. To change the embedded set, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run:
```
python3 tools/gen_puzzles.py
```
This rewrites `src/puzzles_data.c` and `src/puzzles_count.h`. It stops with an error if a puzzle is malformed or doesn't have exactly one solution. The download command for the source files is in the script's header. The list screen shows at most 100 puzzles per collection.

**Fonts**: the grid digits (44 px, and 52 px for Digit size: Large) and the timer use Montserrat Medium, converted with lv_font_conv 1.5.2:
```
FONT=~/Arduino/libraries/lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf
npx lv_font_conv --no-compress --no-prefilter --bpp 4 --size 44 --font $FONT \
    -r 0x20,0x30-0x39 --format lvgl --lv-include lvgl.h -o src/fonts/font_digits_44.c
npx lv_font_conv --no-compress --no-prefilter --bpp 4 --size 36 --font $FONT \
    -r 0x20,0x30-0x3A --format lvgl --lv-include lvgl.h -o src/fonts/font_timer_36.c
npx lv_font_conv --no-compress --no-prefilter --bpp 4 --size 52 --font $FONT \
    -r 0x20,0x30-0x39 --format lvgl --lv-include lvgl.h -o src/fonts/font_digits_52.c
```

## Changing the colours

Both colour schemes are tables at the top of `src/ui_sudoku.c`: `PAL_DARK` and `PAL_LIGHT`. They set the background, text, grid lines and each cell highlight state.

## Tuning the sounds

The effects are note tables at the top of `src/sfx.c`. Each note has a frequency, a length in ms, a waveform (sine, triangle or square) and a level. `SFX_VOLUME` sets the codec volume, 70 % by default.

## Code style

`make format` formats the project's own C and C++ sources, headers and the sketch (`esp32p4_sudoku.ino`) with astyle (`.astylerc`: K&R braces, 4-space indents, every `if` / `for` / `while` / `else` body in braces on its own lines; blocks already on one line, like one-line accessors, stay). `make format-check` lists the files that need it and fails if any do, changing nothing. Vendored code (SQLite, the QR Code generator, ggwave in `src/ggwave/`, `extras/lv_conf.h`) and generated files (puzzles, avatars, fonts) are left out. astyle 3.1 doesn't brace a loop whose body is itself an `if` / `for`; `tools/brace_nested.py` does, between two astyle passes. It also has a bug: a `<<` inside a designated initializer (`.pin_bit_mask = 1ULL << gpio,`) misaligns the following lines, so keep such shifts in parentheses.

## Code documentation

`make doxygen` builds HTML documentation of the firmware's code into `build/doxygen/html/index.html` (Doxygen with Graphviz: `apt install doxygen graphviz`; settings in `Doxyfile`). Every source file, function and structure is there with its code, documented or not; the Settings screen's files (`ui_settings*.c`, `ui_settings.h`, the **Settings screen** topic) are fully documented in Doxygen's `/** ... */` style, with `@brief`, `@param` and `@return`. Vendored and generated code is left out, as in `make format`. Warnings (a documented function missing a parameter, a malformed tag) go to `build/doxygen/warnings.log`, and `make doxygen` prints how many.

## Tests

The parts of the firmware that are plain C run on a PC too: `make -C tests` builds them with the host's compiler (AddressSanitizer and UBSan on) and runs `tests/test_main.c` in a few seconds, no board needed:

| Test | What it checks |
|---|---|
| geometry | each fixed variant's board: cells, units, peers, boxes of n cells (Classic, X-Sudoku, Hyper, Mini 6x6, Kids 4x4) |
| built-in puzzles | all 1600 have exactly one solution, and the solver's is a valid grid |
| generator | every variant at Easy and Medium (Hard for some): one solution, givens on the board, the grader agrees with the level, Kids 4x4 never above Medium |
| determinism (golden) | the same seed makes the same puzzle, and five pinned hashes: daily puzzles are made from the date, so a generator change that alters them fails here |
| layout texts and .sdx | each variant's layout to text and back, and whole puzzles through an `.sdx` line both ways (plain and packed givens) |
| .sdx edge cases | `.sdm` lines, names and numbers, bad lines rejected, packed runs of empty cells, a Classic puzzle in one Send-by-sound part |
| hints | the hint engine walks puzzles of several variants to their solution, a right digit at every step |

`make -C tests examples` remakes [`docs/variant-examples.sdx`](variant-examples.sdx), the example puzzles of the [variant pages](variants/README.md) (`tools/variant_examples.c`: one puzzle per variant whose first steps need its own rule, and those steps printed with their reasons; `tests/variant_examples - kropki` prints one variant). Each page's screenshot is that puzzle opened from Puzzles → Saved → variant-examples after importing the file: `python3 tools/variant_shots.py --port /dev/ttyACM1` takes all 20 (or `... kropki 18` some), closing a rules card if one pops up; Saved must open on that collection (the first one there, or the one shown last). `python3 tools/walkthrough.py --all` (or `... kropki`) solves each page's example to the end, one single at a time (`tests/variant_examples --json`: every ruled-out cell or digit with its reason, down to the variant's rule, from `sudoku_fits_why()`), draws a picture per step with PIL (no board needed; the board's layout as on the device: regions, tints, cages, marks, thermometers, circles, sums) into `docs/screenshots/variants/<page>-walkthrough/`, and writes the steps into the page between its `<!-- walkthrough -->` markers (a "The whole puzzle, step by step" section, added at the end the first time).

`make -C tests golden` prints the golden hashes: after a deliberate change to the generator, pin the new ones in `test_determinism` (and know that every board's daily puzzles change with the firmware). `./tests/sudoku_tests hints` runs only the tests whose name contains "hints". Code that touches LVGL or ESP-IDF (the screens, `sudoku.db`) isn't covered: that is still checked on the board, with devctl.

## Puzzle generation speed

How long **New puzzle** takes on the board, measured with devctl's `genbench` ([Remote control](devctl.md)): `python3 tools/devctl.py --port /dev/ttyACM1 "genbench all all 10"`. Ten puzzles of each variant and level, from fixed seeds (the same every run, so two builds compare puzzle for puzzle), each on the worker core like New puzzle, the CPU at 360 MHz. Measured 2026-11-21 on the Waveshare 4B board (pre-v3 ESP32-P4 silicon), firmware `0ce035d` (the generator as committed there); the whole run takes about 5 minutes.

Each cell: average / slowest time in ms (average givens in brackets). Every one of the 780 puzzles came out at the level asked for. Arrow, Renban, German Whispers, Skyscraper and Little Killer were added later (2026-11-21, the same way). Kids 4x4 has no Hard or Expert.

| Variant | Easy | Medium | Hard | Expert |
|---|---|---|---|---|
| Classic | 5 / 10 (38) | 69 / 172 (27.8) | 441 / 998 (27.5) | 90 / 209 (27.8) |
| X-Sudoku | 5 / 10 (37.7) | 171 / 322 (22.8) | 283 / 507 (21.9) | 235 / 469 (21.2) |
| Mini 6x6 | 0 / 1 (16.2) | 2 / 3 (11.4) | 1052 / 2664 (10.8) | 41 / 125 (11.4) |
| Hyper | 62 / 188 (38.3) | 233 / 459 (21.5) | 343 / 764 (20.6) | 373 / 697 (20.2) |
| Jigsaw | 297 / 1028 (38.1) | 346 / 767 (23.9) | 527 / 867 (23.2) | 1113 / 3147 (22.6) |
| Killer | 46 / 136 (10.8) | 498 / 1179 (4.9) | 1033 / 2147 (9.5) | 1630 / 4316 (8.8) |
| Anti-Knight | 12 / 23 (34.7) | 281 / 453 (19.1) | 442 / 953 (19.9) | 405 / 539 (17) |
| Even-Odd | 6 / 12 (32.2) | 57 / 199 (20.1) | 183 / 374 (20.5) | 192 / 388 (19.4) |
| Consecutive | 19 / 38 (14) | 300 / 486 (6.6) | 639 / 1828 (5.9) | 366 / 587 (3.1) |
| Thermo | 8 / 17 (29) | 86 / 194 (21.4) | 416 / 1431 (16.8) | 331 / 603 (16.8) |
| Sandwich | 297 / 761 (26.2) | 199 / 412 (20.9) | 2348 / 4201 (21.9) | 1987 / 2596 (14.8) |
| Kids 4x4 | 0 / 0 (8.8) | 0 / 0 (4.4) | – | – |
| Anti-King | 7 / 15 (34.6) | 94 / 171 (23.7) | 326 / 1123 (22.7) | 191 / 301 (21.2) |
| Non-Consecutive | 862 / 3058 (25.4) | 909 / 1927 (14.4) | 2908 / 7173 (15) | 1540 / 3076 (11.3) |
| Disjoint Groups | 7 / 18 (33.8) | 43 / 101 (21.3) | 168 / 526 (20.4) | 119 / 311 (20.5) |
| Asterisk | 6 / 12 (35.7) | 93 / 186 (24.8) | 337 / 971 (24.3) | 186 / 412 (23.7) |
| Kropki | 28 / 35 (15.6) | 116 / 146 (2.7) | 352 / 864 (2.4) | 189 / 377 (0) |
| XV | 15 / 20 (23.9) | 136 / 181 (4.6) | 307 / 749 (3.2) | 1743 / 5422 (3.8) |
| Greater-Than | 7 / 10 (18.5) | 24 / 35 (4.2) | 197 / 538 (3.4) | 489 / 751 (3.8) |
| Quadruple | 9 / 13 (29.4) | 72 / 92 (13.3) | 204 / 359 (9) | 600 / 1975 (7.4) |
| Arrow | 9 / 11 (29.6) | 320 / 551 (18.5) | 518 / 1733 (16.2) | 497 / 770 (14.5) |
| Renban | 9 / 19 (30.6) | 174 / 248 (18.8) | 525 / 943 (17) | 477 / 721 (14.9) |
| German Whispers | 12 / 26 (29.7) | 450 / 1265 (14.6) | 1784 / 3302 (13.2) | 1111 / 2311 (11.4) |
| Skyscraper | 38 / 121 (29.9) | 159 / 235 (22.2) | 677 / 1665 (22.8) | 304 / 828 (21.3) |
| Little Killer | 15 / 29 (29.7) | 471 / 692 (20.7) | 1102 / 2132 (18.8) | 1225 / 1638 (16.6) |

Most take well under a second. The slow ones (seconds) are the variants whose rules prune the search least, at the levels where a puzzle has to be dug close to its minimum: Non-Consecutive (Hard up to 7 s), XV Expert (up to 5.4 s), Killer Expert (4.3 s), Sandwich Hard (4.2 s), Jigsaw Expert and Non-Consecutive Easy (3 s). The generator yields now and then (`sudoku_gen_yield`), so even those don't trip the task watchdog (5 s on core 0's idle task), which a 15 s Greater-Than Expert once did. The averages are also `GEN_EXPECT_MS` in `src/ui_newpuzzle.c`, which the New puzzle dialog's progress bar is estimated from: update it with the table. Re-run `genbench` (or just the variant changed: `genbench xv expert 20`) after any generator change and compare; the PC tests check that the puzzles are right, this checks that they're quick.

## The UI files

The UI is one screen (or a few related ones) per file, `src/ui_*.c`, and `ui_sudoku.c` for start-up and the public functions. A function stays `static` in its file unless another file calls it; then it is declared once in `src/ui_internal.h` and defined, without `static`, in its file. Macros and types are all in `ui_internal.h`. When a new screen needs a function from another file, move its declaration there rather than copying it.

Variables are always `static`: only the file that owns one touches it, and the other files call that file's accessors, declared in `ui_internal.h` under each owner. The game's state (`g_*`) belongs to `ui_game.c`, so `ui_hints.c` reads `game_sel()` and writes `game_set_sel(i)`; a screen's widget is `screen_home()`. An accessor of an array or a struct gives a `const` pointer (`game_given()[i]`, `game_b()->val[i]`), so the compiler rejects a write from another file. A change another file needs to make is a function in the owner (`game_put_value`, `players_take`, `stats_take_view`). Add an accessor only when another file needs it.

The other modules work the same way: no `extern` variables in `src/`. Their tables are reached through functions: `variant_name(v)`, `gen_level_name(l)` (generator.c), `stats_kind_name(k)` (stats_db.c), `puzzle_collection(c)`, `puzzle_load(id, cells)` (puzzles_data.c), `avatar_image(k)` (avatars.c), and the active colours through `ui_palette()` (the `C_*` macros). Constant tables follow the rule too: `static` in their owner, read through a function (`ui_digits()`, `learn_names()`, `sleep_minutes()` in the UI), never an `extern`. `tools/gen_puzzles.py` and `tools/gen_avatars.py` write those accessors with the data.

## Internal RAM

The P4's internal RAM (768 KB) is the tight resource; PSRAM (32 MB) is plenty. Some things must have one contiguous block of internal RAM: the hardware H.264 encoder for MP4 export (~52 KB), the microSD card's and the Wi-Fi link's DMA buffers. `setup()` calls `heap_caps_malloc_extmem_enable(4)`, so every `malloc()` of 4 bytes or more goes to PSRAM; code that needs internal RAM asks for it with `heap_caps_malloc(..., MALLOC_CAP_INTERNAL / MALLOC_CAP_DMA)`. With a higher limit (16 before 2026-11-20), LVGL's thousands of tiny allocations stayed internal and cut it into pieces: 99 KB free but no block over 35 KB, and MP4 export failed with "No memory for reference frame". Check with `heap 1` over [devctl](devctl.md) (the main region's `alloc_blocks` and `largest_free_block`): about 225 KB free and a 143 KB largest block at the Home screen is normal. Big buffers of your own go to PSRAM (`MALLOC_CAP_SPIRAM`), and avoid rebuilding every screen at once (`apply_theme`) as a reaction to data changes: two copies of all screens exist for a moment.

## Layout

```
esp32p4_sudoku.ino      setup/loop
src/ui_sudoku.c         the UI's start-up and public functions (ui_sudoku.h), theme, notices, lock screen, screen saver
src/ui_internal.h       what the UI files share: includes, macros, types, and the functions and variables used by more than one
src/ui_common.c         palette, widgets and helpers, the generated-puzzle cache, context menus
src/ui_game.c           the game: rules check, saving, the board, confetti, input, building its screen
src/ui_hints.c          hints and the game screen tour
src/ui_game_tools.c     time attack, the rules card, Share (QR code), Send by sound, save to a collection
src/ui_replay.c         replay, screenshots, mistake insights, MP4 export
src/ui_newpuzzle.c      New puzzle (the generator), the daily puzzle, Learn, Enter a puzzle
src/ui_list.c           the Puzzles screen
src/ui_home.c           Home: the week, Continue and unfinished games, the menu
src/ui_stats.c          Statistics and the progress chart
src/ui_settings.c       the Settings screen around its tabs, the rows' look, the theme, settings reloaded, power off,
                        the dark screen while the flash is written (ui_settings.h lists the tab files)
src/ui_settings_network.c  Settings > Network: Wi-Fi, time zone
src/ui_settings_display.c  Settings > Display: theme, accent, brightness, screen off, lock screen, night mode
src/ui_settings_sound.c    Settings > Sound: sound effects, volume, Gibberlink
src/ui_settings_game.c     Settings > Game: timer, mistake check, no-notes, left-handed, hints, tour
src/ui_settings_data.c     Settings > Data: microSD card, USB drive, firmware update, import, export, backup, factory reset
src/settings_core.h     what those do, without LVGL (a header for the four below)
src/fw_update.c         firmware update from the card: update.bin into the other app slot
src/sd_card.c           the microSD card: probe and format
src/device_ops.c        factory reset, power off
src/usb_drive.c         the card lent to a computer over USB
src/ui_history.c        History and Gameplays
src/ui_trophies.c       Trophies: achievements, leaderboards, records
src/ui_players.c        the name dialog, Players, avatars
src/devctl.cpp          serial control: screenshots and simulated touches (tools/devctl.py)
src/qrcode/             Project Nayuki's QR Code generator (MIT), for Share
src/store.cpp           RAM copy of statistics, saved game and settings; reads the old NVS keys once
src/stats_db.c          sudoku.db on the microSD card: statistics, saved game, settings, queries
src/usb_msc.cpp         the microSD card as a USB drive (TinyUSB MSC)
src/sqlite/             SQLite 3.54.1 amalgamation + ESP-IDF file layer (sqlite3_esp.c)
src/wifi_mgr.cpp        Wi-Fi worker task (ESP-Hosted / C6), saved network, starts SNTP
src/ui_wifi.c           Wi-Fi screen: networks, password keyboard
src/ui_file.c           Files screen: browse, rename and delete on the card; sudoku.db protected; puzzle file preview + Import
src/gibberlink.cpp      remote control by sound: ES7210 mics -> ggwave -> devctl commands, replies by sound
src/ggwave/             ggwave 060aec7 (MIT), data over sound, vendored unchanged
src/ui_media_view.c        the Files viewer: JPEG (hardware decoder), MP4 (tinyh264), full screen
src/mp4_reader.c        MP4 index reader for the viewer: frame offsets, times, key frames, SPS/PPS
src/ui_sysmon.c         system monitor screen: heap, flash, microSD, temperature, tasks, LVGL
src/sysmon_core.c       the system monitor's numbers, without LVGL: CPU load, temperature, memory, storage, the task list, the last hour
src/timezones.c         IANA time zone names -> POSIX TZ rules
src/ui_palette.h        light/dark colour schemes shared by the UI files
src/sfx.c               sound effects: note tables + 4-voice mixer task
src/sdx.c               the puzzle line format (Import puzzles, the Files preview, Send by sound): parse, pack, format; plain C, tested on a PC
src/generator.c         variants (Classic, X-Sudoku, Hyper, Jigsaw, Killer, Mini 6x6, Kids 4x4, Anti-Knight, Anti-King, Even-Odd, Consecutive, Non-Consecutive, Disjoint Groups, Asterisk, Kropki, XV, Greater-Than, Quadruple, Arrow, Renban, German Whispers, Skyscraper, Little Killer, Thermo, Sandwich): generator, grader, solver (Fill) and hints
src/puzzles.h           puzzle access (unpack, collection lookup)
src/puzzles_data.c      generated puzzle data
src/fonts/              Montserrat 44 and 52 px digits / 36 px timer, FontAwesome person, calendar, info, trophy, lock, star, chart, lightbulb, QR code, camera and vertical-dots (menu) icons at 24/20 px (lv_font_conv)
src/avatars.c           the players' avatars: their groups (generated); the pixels are extras/avatars.bin (7 MB)
src/avatar_store.c      the avatars from their own flash partition, and installing avatars.bin from the card
src/bsp/                display (ST7703 / JD9365), GT911 touch, ES8311 speaker, microSD, LVGL port
tools/gen_puzzles.py    regenerates src/puzzles_data.c from tools/opensudoku/
tools/gen_avatars.py    regenerates extras/avatars.bin and src/avatars.c / .h from Noto Color Emoji (Pillow with raqm)
tools/avatars_order.txt the avatars' order (sudoku.db keeps their numbers: only add at the end)
tools/devctl.py         screenshots (PNG) and touches over the serial port
tools/make_flasher_site.sh  the GitHub Pages site: web flasher + docs
tools/make_docs_site.py README.md and docs/*.md as HTML pages (for the site)
docs/screenshots.txt    the script that makes docs/screenshots/*.png
tests/                  PC-side tests: make -C tests (see Tests)
tools/opensudoku/       the downloaded .opensudoku collections
```

To change the embedded puzzles, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run `python3 tools/gen_puzzles.py`.

To regenerate the avatars (a newer Noto Color Emoji, or a different size), run `python3 tools/gen_avatars.py --keep tools/avatars_order.txt`: it needs Pillow with raqm, `NotoColorEmoji.ttf` and Unicode's `emoji-test.txt` (Debian: `fonts-noto-color-emoji`, `unicode-data`), and `--keep` keeps the numbers players already have (new emoji go at the end). It writes `extras/avatars.bin` (the pixels) and a small `src/avatars.c`; `AVATAR_CRC` in `src/avatars.h` changes with the pixels, so boards ignore an older set in flash and need the new `avatars.bin` installed (see [Building](building.md)).

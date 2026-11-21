# Development

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

Regenerating the embedded data, changing colours and sounds, and the source layout.

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

Both colour schemes are tables at the top of `src/sudoku_ui.c`: `PAL_DARK` and `PAL_LIGHT`. They set the background, text, grid lines and each cell highlight state.

## Tuning the sounds

The effects are note tables at the top of `src/sfx.c`. Each note has a frequency, a length in ms, a waveform (sine, triangle or square) and a level. `SFX_VOLUME` sets the codec volume, 70 % by default.

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

`make -C tests golden` prints the golden hashes: after a deliberate change to the generator, pin the new ones in `test_determinism` (and know that every board's daily puzzles change with the firmware). `./tests/sudoku_tests hints` runs only the tests whose name contains "hints". Code that touches LVGL or ESP-IDF (the screens, `sudoku.db`) isn't covered: that is still checked on the board, with devctl.

## The UI files

The UI is one screen (or a few related ones) per file, `src/ui_*.c`, and `sudoku_ui.c` for start-up and the public functions. A function stays `static` in its file unless another file calls it; then it is declared once in `src/ui_internal.h` and defined, without `static`, in its file. Macros and types are all in `ui_internal.h`. When a new screen needs a function from another file, move its declaration there rather than copying it.

Variables are always `static`: only the file that owns one touches it, and the other files call that file's accessors, declared in `ui_internal.h` under each owner. The game's state (`g_*`) belongs to `ui_game.c`, so `ui_hints.c` reads `game_sel()` and writes `game_set_sel(i)`; a screen's widget is `screen_home()`. An accessor of an array or a struct gives a `const` pointer (`game_given()[i]`, `game_b()->val[i]`), so the compiler rejects a write from another file. A change another file needs to make is a function in the owner (`game_put_value`, `players_take`, `stats_take_view`). Add an accessor only when another file needs it.

The other modules work the same way: no `extern` variables in `src/`. Their tables are reached through functions: `variant_name(v)`, `gen_level_name(l)` (generator.c), `stats_kind_name(k)` (stats_db.c), `puzzle_collection(c)`, `puzzle_load(id, cells)` (puzzles_data.c), `avatar_image(k)` (avatars.c), and the active colours through `ui_palette()` (the `C_*` macros). Constant tables follow the rule too: `static` in their owner, read through a function (`ui_digits()`, `learn_names()`, `sleep_minutes()` in the UI), never an `extern`. `tools/gen_puzzles.py` and `tools/gen_avatars.py` write those accessors with the data.

## Internal RAM

The P4's internal RAM (768 KB) is the tight resource; PSRAM (32 MB) is plenty. Some things must have one contiguous block of internal RAM: the hardware H.264 encoder for MP4 export (~52 KB), the microSD card's and the Wi-Fi link's DMA buffers. `setup()` calls `heap_caps_malloc_extmem_enable(4)`, so every `malloc()` of 4 bytes or more goes to PSRAM; code that needs internal RAM asks for it with `heap_caps_malloc(..., MALLOC_CAP_INTERNAL / MALLOC_CAP_DMA)`. With a higher limit (16 before 2026-11-20), LVGL's thousands of tiny allocations stayed internal and cut it into pieces: 99 KB free but no block over 35 KB, and MP4 export failed with "No memory for reference frame". Check with `heap 1` over [devctl](devctl.md) (the main region's `alloc_blocks` and `largest_free_block`): about 225 KB free and a 143 KB largest block at the Home screen is normal. Big buffers of your own go to PSRAM (`MALLOC_CAP_SPIRAM`), and avoid rebuilding every screen at once (`apply_theme`) as a reaction to data changes: two copies of all screens exist for a moment.

## Layout

```
esp32p4_sudoku.ino      setup/loop
src/sudoku_ui.c         the UI's start-up and public functions (sudoku_ui.h), theme, notices, lock screen, screen saver
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
src/ui_settings.c       Settings, firmware update from the card, microSD format, factory reset, power off, USB drive
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
src/wifi_ui.c           Wi-Fi screen: networks, password keyboard
src/file_ui.c           Files screen: browse, rename and delete on the card; sudoku.db protected; puzzle file preview + Import
src/gibberlink.cpp      remote control by sound: ES7210 mics -> ggwave -> devctl commands, replies by sound
src/ggwave/             ggwave 060aec7 (MIT), data over sound, vendored unchanged
src/media_view.c        the Files viewer: JPEG (hardware decoder), MP4 (tinyh264), full screen
src/mp4_reader.c        MP4 index reader for the viewer: frame offsets, times, key frames, SPS/PPS
src/sysmon_ui.c         system monitor screen: heap, flash, microSD, temperature, tasks, LVGL
src/timezones.c         IANA time zone names -> POSIX TZ rules
src/ui_palette.h        light/dark colour schemes shared by the UI files
src/sfx.c               sound effects: note tables + 4-voice mixer task
src/sdx.c               the puzzle line format (Import puzzles, the Files preview, Send by sound): parse, pack, format; plain C, tested on a PC
src/generator.c         variants (Classic, X-Sudoku, Hyper, Jigsaw, Killer, Mini 6x6, Kids 4x4, Anti-Knight, Anti-King, Even-Odd, Consecutive, Non-Consecutive, Disjoint Groups, Asterisk, Kropki, XV, Greater-Than, Quadruple, Thermo, Sandwich): generator, grader, solver (Fill) and hints
src/puzzles.h           puzzle access (unpack, collection lookup)
src/puzzles_data.c      generated puzzle data
src/fonts/              Montserrat 44 and 52 px digits / 36 px timer, FontAwesome person, calendar, info, trophy, lock, star, chart, lightbulb, QR code, camera and vertical-dots (menu) icons at 24/20 px (lv_font_conv)
src/avatars.c           the players' avatars: Noto Color Emoji as 36 px RGB565A8 images (generated, ~7 MB of flash)
src/bsp/                display (ST7703 / JD9365), GT911 touch, ES8311 speaker, microSD, LVGL port
tools/gen_puzzles.py    regenerates src/puzzles_data.c from tools/opensudoku/
tools/gen_avatars.py    regenerates src/avatars.c / .h from Noto Color Emoji (Pillow with raqm)
tools/avatars_order.txt the avatars' order (sudoku.db keeps their numbers: only add at the end)
tools/devctl.py         screenshots (PNG) and touches over the serial port
tools/make_flasher_site.sh  the GitHub Pages site: web flasher + docs
tools/make_docs_site.py README.md and docs/*.md as HTML pages (for the site)
docs/screenshots.txt    the script that makes docs/screenshots/*.png
tests/                  PC-side tests: make -C tests (see Tests)
tools/opensudoku/       the downloaded .opensudoku collections
```

To change the embedded puzzles, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run `python3 tools/gen_puzzles.py`.

To regenerate the avatars (a newer Noto Color Emoji, or a different size), run `python3 tools/gen_avatars.py --keep tools/avatars_order.txt`: it needs Pillow with raqm, `NotoColorEmoji.ttf` and Unicode's `emoji-test.txt` (Debian: `fonts-noto-color-emoji`, `unicode-data`), and `--keep` keeps the numbers players already have (new emoji go at the end). The first build after that compiles the 23 MB `src/avatars.c` (a few minutes); later builds reuse it.

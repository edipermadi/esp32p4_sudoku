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

## Layout

```
esp32p4_sudoku.ino      setup/loop
src/sudoku_ui.c         home, list, statistics, settings and game screens
src/devctl.cpp          serial control: screenshots and simulated touches (tools/devctl.py)
src/qrcode/             Project Nayuki's QR Code generator (MIT), for Share
src/store.cpp           RAM copy of statistics, saved game and settings; reads the old NVS keys once
src/stats_db.c          sudoku.db on the microSD card: statistics, saved game, settings, queries
src/usb_msc.cpp         the microSD card as a USB drive (TinyUSB MSC)
src/sqlite/             SQLite 3.54.1 amalgamation + ESP-IDF file layer (sqlite3_esp.c)
src/wifi_mgr.cpp        Wi-Fi worker task (ESP-Hosted / C6), saved network, starts SNTP
src/wifi_ui.c           Wi-Fi screen: networks, password keyboard
src/file_ui.c           Files screen: browse, rename and delete on the card; sudoku.db protected
src/media_view.c        the Files viewer: JPEG (hardware decoder), MP4 (tinyh264), full screen
src/mp4_reader.c        MP4 index reader for the viewer: frame offsets, times, key frames, SPS/PPS
src/sysmon_ui.c         system monitor screen: heap, flash, microSD, temperature, tasks, LVGL
src/timezones.c         IANA time zone names -> POSIX TZ rules
src/ui_palette.h        light/dark colour schemes shared by the UI files
src/sfx.c               sound effects: note tables + 4-voice mixer task
src/generator.c         variants (Classic, X-Sudoku, Hyper, Jigsaw, Killer, Mini 6x6, Anti-Knight, Even-Odd, Consecutive, Thermo, Sandwich): generator, grader, solver (Fill) and hints
src/puzzles.h           puzzle access (unpack, collection lookup)
src/puzzles_data.c      generated puzzle data
src/fonts/              Montserrat 44 and 52 px digits / 36 px timer, FontAwesome person, calendar, info, trophy, lock, star, chart, lightbulb and QR code icons at 24/20 px (lv_font_conv)
src/bsp/                display (ST7703 / JD9365), GT911 touch, ES8311 speaker, microSD, LVGL port
tools/gen_puzzles.py    regenerates src/puzzles_data.c from tools/opensudoku/
tools/devctl.py         screenshots (PNG) and touches over the serial port
tools/make_flasher_site.sh  the GitHub Pages site: web flasher + docs
tools/make_docs_site.py README.md and docs/*.md as HTML pages (for the site)
docs/screenshots.txt    the script that makes docs/screenshots/*.png
tools/opensudoku/       the downloaded .opensudoku collections
```

To change the embedded puzzles, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run `python3 tools/gen_puzzles.py`.

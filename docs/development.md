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

Both colour schemes are tables at the top of `src/ui_common.c`: `PAL_DARK` and `PAL_LIGHT`. They set the background, text, grid lines and each cell highlight state.

## Tuning the sounds

The effects are note tables at the top of `src/sfx.c`. Each note has a frequency, a length in ms, a waveform (sine, triangle or square) and a level. `SFX_VOLUME` sets the codec volume, 70 % by default.

## Code style

`make format` formats the project's own C and C++ sources, headers and the sketch (`esp32p4_sudoku.ino`) with astyle (`.astylerc`: K&R braces, 4-space indents, every `if` / `for` / `while` / `else` body in braces on its own lines; blocks already on one line, like one-line accessors, stay). `make format-check` lists the files that need it and fails if any do, changing nothing. Vendored code (SQLite, the QR Code generator, ggwave in `src/ggwave/`, `extras/lv_conf.h`) and generated files (puzzles, avatars, fonts) are left out. astyle 3.1 doesn't brace a loop whose body is itself an `if` / `for`; `tools/brace_nested.py` does, between two astyle passes. It also has a bug: a `<<` inside a designated initializer (`.pin_bit_mask = 1ULL << gpio,`) misaligns the following lines, so keep such shifts in parentheses.

## Code documentation

`make doxygen` builds HTML documentation of the firmware's code into `build/doxygen/html/index.html` (each release also puts it online, at https://edipermadi.github.io/esp32p4_sudoku/code/) (Doxygen with Graphviz: `apt install doxygen graphviz`; settings in `Doxyfile`). Every source file, function and structure is there with its code, documented or not. The main page (`src/mainpage.dox`) says what the firmware does, where to start, how it runs, the conventions and the build commands. The **Screens** page (`src/screens.dox`) is a state diagram of every screen and how a tap leads from one to another, drawn by Graphviz from the `@dot` block there: a new screen, or a new way between two, needs a line in it. Under **Topics** (`src/topics.dox`) each screen's UI and core files are bundled: Game, Puzzles, Statistics, History, Gameplays and Replay, Trophies, Players, Files, Settings, System monitor, Lock screen; next to them the UI's start-up, Media and Board support. All of the firmware's own code is documented in Doxygen's `/** ... */` style: every file, function (`@brief`, `@param`, `@return`), structure, member, variable and macro. A new screen's UI and core go under a topic of their own: a `@defgroup` in `topics.dox` and an `@ingroup` line in each. Vendored and generated code is left out, as in `make format`. Warnings (a documented function missing a parameter, a malformed tag) go to `build/doxygen/warnings.log`, and `make doxygen` prints how many.

## Diagrams

The flowcharts in the docs (start-up, opening `sudoku.db`, the screen saver and lock screen, a move, saving, solving, hints, generating, importing, USB lending, Gibberlink, Send by sound, video playback, achievements ...) are Graphviz files in `docs/diagrams/*.dot`, with their rendered `.svg` next to them, committed so GitHub and the docs site show them without Graphviz. After changing a `.dot` file, run `make diagrams` (needs `dot`: `apt install graphviz`) and commit both. A flowchart describes the code: when the flow it shows changes, change it too. The shared look is at the top of each file ([docs/diagrams/README.md](diagrams/README.md)).

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

`make -C tests examples` remakes [`docs/variant-examples.sdx`](variant-examples.sdx), the example puzzles of the [variant pages](variants/README.md) (`tools/variant_examples.c`: one puzzle per variant whose first steps need its own rule, and those steps printed with their reasons; `tests/variant_examples - kropki` prints one variant). Each page's screenshot is that puzzle opened from Puzzles → Imported → variant-examples after importing the file: `python3 tools/variant_shots.py --port /dev/ttyACM1` takes all 20 (or `... kropki 18` some), closing a rules card if one pops up; Imported must open on that collection (the first one there, or the one shown last). `python3 tools/walkthrough.py --all` (or `... kropki`) solves each page's example to the end, one single at a time (`tests/variant_examples --json`: every ruled-out cell or digit with its reason, down to the variant's rule, from `sudoku_fits_why()`), draws a picture per step with PIL (no board needed; the board's layout as on the device: regions, tints, cages, marks, thermometers, circles, sums) into `docs/screenshots/variants/<page>-walkthrough/`, and writes the steps into the page between its `<!-- walkthrough -->` markers (a "The whole puzzle, step by step" section, added at the end the first time).

`make -C tests golden` prints the golden hashes: after a deliberate change to the generator, pin the new ones in `test_determinism` (and know that every board's daily puzzles change with the firmware). `./tests/sudoku_tests hints` runs only the tests whose name contains "hints". Code that touches LVGL or ESP-IDF (the screens, `sudoku.db`) isn't covered: that is still checked on the board, with devctl.

## Puzzle generation speed

How long **New puzzle** takes on the board, measured with devctl's `genbench` ([Remote control](devctl.md)): `python3 tools/devctl.py --port /dev/ttyACM1 "genbench all all 10"`. Ten puzzles of each variant and level, from fixed seeds (the same every run, so two builds compare puzzle for puzzle), each on the worker core like New puzzle, the CPU at 360 MHz. Measured 2026-11-21 on the Waveshare 4B board (pre-v3 ESP32-P4 silicon), firmware `0ce035d` (the generator as committed there); the whole run takes about 5 minutes.

Each cell: average / slowest time in ms (average givens in brackets). Every one of the 780 puzzles came out at the level asked for. Arrow, Renban, German Whispers, Skyscraper and Little Killer were added later (2026-11-21, the same way). Kids 4x4 has no Hard or Expert. Palindrome, Region Sum Lines, X-Sums, Zipper Lines, Frame, Between Lines, Entropic Lines, Modular Lines, Center Dot, Outside Sudoku, Rossini and Diagonal Non-Consecutive were added on 2026-11-22, the same way, and so were Dutch Whispers, Parity Lines and Slow Thermo, then Fortress and Argyle (later that day); Zipper, Between, Entropic and Modular Lines were re-measured once their lines stopped crossing and they dug logic-first (their puzzles changed; Between Hard went from 3.6 s to 0.3 s), and Frame after the solver's rule checks became one bitmask (see below).

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
| Palindrome | 11 / 20 (29.3) | 284 / 449 (15.2) | 905 / 2445 (13) | 756 / 1250 (12.2) |
| Region Sum Lines | 21 / 47 (29.7) | 403 / 810 (20.1) | 1070 / 1731 (18) | 1206 / 1780 (15.4) |
| X-Sums | 72 / 122 (29.7) | 835 / 1605 (11.3) | 3012 / 7914 (14.7) | 2360 / 4324 (11.6) |
| Zipper Lines | 12 / 21 (29.2) | 86 / 97 (15.5) | 186 / 490 (12.6) | 946 / 1088 (12.9) |
| Frame | 15 / 27 (30) | 112 / 230 (15.1) | 641 / 1895 (17.3) | 262 / 790 (13.3) |
| Between Lines | 19 / 30 (29.8) | 88 / 112 (21.2) | 293 / 532 (19.7) | 1672 / 4862 (20.6) |
| Entropic Lines | 15 / 27 (30) | 85 / 129 (18.6) | 223 / 411 (15.4) | 829 / 1731 (15.9) |
| Modular Lines | 13 / 21 (29.7) | 81 / 141 (18.4) | 192 / 387 (16.1) | 905 / 2322 (14.5) |
| Center Dot | 9 / 20 (35.7) | 79 / 179 (26.8) | 333 / 556 (26) | 242 / 595 (24.6) |
| Outside Sudoku | 37 / 68 (30.1) | 117 / 185 (16.4) | 1406 / 3822 (18.2) | 1360 / 4978 (17.7) |
| Rossini | 41 / 96 (30.2) | 292 / 717 (21.9) | 1780 / 4290 (21.4) | 643 / 2608 (20.7) |
| Diagonal Non-Consecutive | 274 / 595 (32.2) | 512 / 823 (16.8) | 798 / 1675 (16.1) | 839 / 1044 (14) |
| Dutch Whispers | 16 / 29 (29.1) | 460 / 1263 (17.2) | 1277 / 1951 (15.5) | 1349 / 3228 (15) |
| Parity Lines | 9 / 11 (30.1) | 187 / 384 (19.7) | 784 / 2131 (18.2) | 754 / 2497 (17.7) |
| Slow Thermo | 11 / 23 (29.9) | 188 / 496 (21.8) | 696 / 1934 (17.6) | 535 / 1216 (17.7) |
| Fortress | 13 / 38 (27.9) | 82 / 148 (19.4) | 212 / 433 (18) | 1005 / 1732 (17.8) |
| Argyle | 673 / 1164 (34.9) | 472 / 753 (21.3) | 758 / 1110 (19) | 944 / 1184 (17.1) |

Most take well under a second. The slow ones (seconds) are the variants whose rules prune the search least, at the levels where a puzzle has to be dug close to its minimum: Non-Consecutive (Hard up to 7 s), X-Sums Hard (up to 7.9 s), Between Lines Expert (up to 4.9 s), XV Expert (up to 5.4 s), Killer Expert (4.3 s), Sandwich Hard (4.2 s), Jigsaw Expert and Non-Consecutive Easy (3 s). The generator yields now and then (`sudoku_gen_yield`), so even those don't trip the task watchdog (5 s on core 0's idle task), which a 15 s Greater-Than Expert once did. The solver's innermost function, `grid_free()` in `src/gen_solver.c`, runs for every cell at every step of every search: keep it lean. Which rules a board has beyond its units is worked out once per search (`grid_t.rules`, a bitmask); `grid_free()` tests that word, not the board's flags (the board is in PSRAM on the board, its flags far apart: a cache miss each), and returns at once for a board without extra rules. With a dozen flags tested instead, Classic got 45 % slower on the board (2026-11-22). Big rule code goes in a function of its own (`group_line_allow()`): inside `gen_line_allow()` the Entropic and Modular case made every other line kind 15-20 % slower. The averages are also `GEN_EXPECT_MS` in `src/ui_newpuzzle.c`, which the New puzzle dialog's progress bar is estimated from: update it with the table. Re-run `genbench` (or just the variant changed: `genbench xv expert 20`) after any generator change and compare; the PC tests check that the puzzles are right, this checks that they're quick.

## The UI files

A screen is `ui_<name>.c` (LVGL) and, for its logic, `<name>_core.c` (plain C). Variables are `static` in their owner and reached through accessors declared in `src/ui_internal.h`; there are no `extern` variables in `src/`. How it all fits together, and how to add a screen: [UI structure](internals/ui-structure.md).

## Internal RAM

The P4's 768 KB of internal RAM is the tight resource: `setup()` sends every `malloc()` of 4 bytes or more to PSRAM, and big buffers of your own go there too (`MALLOC_CAP_SPIRAM`). Check with devctl `heap 1`: about 225 KB free and a 143 KB largest block at Home is normal. Who needs internal RAM, the big buffers and the rules: [Memory](internals/memory.md). What runs on which core: [Tasks and cores](internals/tasks.md).

## Layout

```
esp32p4_sudoku.ino      setup/loop
src/ui_sudoku.c         the UI's start-up: the card and sudoku.db read, the settings applied, the screens and timers
src/ui_notice.c         notices: a banner for a while, or one that stays while something runs (a file from the computer)
src/ui_saver.c          the screen saver: the dim clock, dark, power saving, the waking tap
src/ui_lock.c           the lock screen (Swipe or PIN) and the keypad it shares
src/ui_pin_setup.c      Settings' PIN setup: a new PIN typed twice
src/lock_core.c         the lock's PIN without LVGL: right or wrong, tries, the wait, setting a new one
src/ui_remote.c         what serial control asks of the UI: fill, state, the screen's name, what uses the card, Benchmarking
src/ui_internal.h       what the UI files share: includes, macros, types, and the functions and variables used by more than one
src/ui_common.c         widgets and helpers, dialogs (make_msgbox())
src/ui_palette.c        the colours: dark and light schemes, accents, colour-blind colours; the brightness in effect
src/ui_ctx_menu.c       icon buttons and the ⋮ context menus
src/ui_player_badge.c   a player shown small: colour, dot, avatar
src/ui_game.c           the game: its state, starting and saving it, a move made, building its screen
src/ui_game_board.c     the board drawn: cells, highlights, the header and the timer
src/ui_game_controls.c  the ⋮ menu, Back, Pause, Restart, the clock's tick
src/ui_game_input.c     its input: a cell tapped, the pad (digit-first), Notes, Auto notes, Erase, Undo, Redo, Fill
src/ui_game_colors.c    a cell long pressed: its colour menu (and a Killer cage's digit sets)
src/ui_game_win.c       the Solved dialog and its confetti, the solve recorded, head-to-head
src/ui_game_layout.c    the game screen's layout for the variant and the hand (Left-handed)
src/ui_board_draw.c     the board's variant overlays: cages, marks, lines, regions, outside clues
src/game_core.c         without LVGL: the rules check (which digits break a rule, every variant), the undo history
src/ui_hints.c          the hint card and its buttons
src/ui_tour.c           the game screen's tour
src/hint_core.c         a hint's words and highlights without LVGL, from the hint engine's step
src/ui_attack.c         time attack: the run, its leaderboard, its end
src/ui_rules_card.c     the rules card: a variant's rules with a small picture
src/ui_share.c          Share (QR code), Send and receive by sound (Gibberlink)
src/ui_save_coll.c      the game's Save: in or out of a saved collection, a new collection
src/share_core.c        Share without LVGL: the QR code's text, the .sdx line sent by sound, its parts and CRC
src/ui_replay.c         the Replay screen (ui_replay_part.h)
src/ui_replay_export.c  a replay exported as an MP4: the invisible display, the progress dialog
src/ui_screenshot.c     the camera buttons and their notice
src/screenshot_core.c   the screen copied, compressed by the hardware JPEG encoder and written (a task)
src/ui_insights.c       mistake insights (Statistics)
src/ui_newpuzzle.c      New puzzle's generator: the task, its progress, the benchmark; the daily puzzle's button
src/ui_gen_dialog.c     the New puzzle dialog: variants in four tabs, the levels
src/ui_enter_puzzle.c   Enter a puzzle: typed in, checked for one solution, graded, saved
src/daily_core.c        the daily puzzle without LVGL: today's variant and level, its seed, its name
src/ui_learn.c          Learn: the lessons in tabs, the search for a lesson's puzzle (on the worker core), the practice game
src/learn_core.c        Learn's lessons without LVGL: the ten techniques and a variant rule each, and the search for a position to practise one
src/ui_list.c           Puzzles: the screen, tabs, collection column, panel, grid of numbers, the ⋮ menu (ui_list_part.h)
src/ui_list_rows.c      its rows: a saved or imported collection's puzzles
src/ui_list_starred.c   its Starred: the bookmarks
src/ui_list_manage.c    its Export and Delete of a collection
src/ui_home_menu.c      Home's menu layout: a column, a grid, or pages
src/collections.h       the collections as data (declarations; defined in puzzles_lookup.c)
src/puzzles_lookup.c    any puzzle by its id: the generated-puzzle cache, givens, board, name; the collections
src/puzzles_core.h      Puzzles without LVGL (a header for the four parts below)
src/puzzles_select.c    the tab, the collection shown, Starred
src/puzzles_solved.c    solved counts, the panel's numbers
src/puzzles_starred.c   the starred puzzles
src/puzzles_colls.c     delete or export a collection, take a puzzle out
src/ui_home.c           Home: the clock, the menu, the solved line
src/ui_home_week.c      Home's week: this week on the solved line, and this week against last
src/ui_continue.c       Home's Continue button: its line, its tap, a game resumed (ui_continue_part.h)
src/ui_continue_games.c the Unfinished games dialog: each game, to resume or discard
src/ui_continue_open.c  open_puzzle(): the game in progress, an unfinished one resumed, or a new start (without a card: the "Unfinished game" dialog first)
src/continue_core.c     the unfinished games without LVGL: the list, the one played last, one loaded into RAM or discarded
src/ui_stats.c          Statistics: the levels, the table, the player
src/ui_stats_chart.c    Statistics' progress chart
src/statistics_core.c   Statistics' data without LVGL: the row picked, the view (sudoku.db or RAM), totals, the chart's numbers
src/ui_settings.c       the Settings screen around its tabs, the rows' look, power off (ui_settings.h lists the tab files)
src/ui_screens.c        the screens' registry, the theme (every screen rebuilt), the Wi-Fi, System monitor and Files screens
src/ui_db_poll.c        the UI's poll of sudoku.db's results, settings reloaded from sudoku.db
src/ui_flash_busy.c     the card and the dark screen while the flash is written
src/ui_settings_network.c  Settings > Network: Wi-Fi, time zone
src/ui_settings_display.c  Settings > Display: theme, accent, brightness, screen off, lock screen, night mode
src/ui_settings_sound.c    Settings > Sound: sound effects, volume, Gibberlink
src/ui_settings_game.c     Settings > Game: timer, mistake check, no-notes, left-handed, hints, tour
src/ui_settings_data.c     Settings > Data: the tab, import, export, backup, factory reset
src/ui_settings_card.c     its microSD card row: status, Format
src/ui_settings_usb.c      its USB drive row: the card lent to a computer
src/ui_settings_update.c   its Firmware update row
src/settings_core.h     what those do, without LVGL (a header for the four below)
src/fw_update.c         firmware update from the card: update.bin into the other app slot
src/sd_card.c           the microSD card: probe and format
src/device_ops.c        factory reset, power off
src/usb_drive.c         the card lent to a computer over USB
src/ui_history.c        History: the calendar and a day's games
src/history_core.c      History's data without LVGL: the month asked for, its days' counts, the day picked
src/ui_gameplays.c      Gameplays: the pages of recorded solves
src/gameplays_core.h    Gameplays and Replay without LVGL (a header for the three parts below)
src/gameplays_list.c    the Gameplays pages, opening a row's replay or video
src/replay_timeline.c   a move log played back: the board at any step, play times, speed
src/replay_video.c      a replay as an MP4: the file name, frames, the encoder
src/ui_trophies.c       Trophies: the screen around the tabs, the tab bar, the unlock toast (ui_trophies_tab.h)
src/ui_trophies_achievements.c   its Achievements tab
src/ui_trophies_daily.c          its Daily tab: a daily puzzle's leaderboard
src/ui_trophies_weekly.c         its Weekly tab
src/ui_trophies_records.c        its Records tab
src/trophies_core.h     the Trophies screen's data, without LVGL (a header for the four parts below)
src/trophies_dates.c    date arithmetic: a day moved, this week's Monday
src/trophies_view.c     the day and week shown: moving them, asking sudoku.db
src/trophies_results.c  sudoku.db's answers for the screen, taken (PSRAM buffers)
src/trophies_text.c     an achievement's progress, the unlock toast's text
src/ui_players.c        Players: the rows, the ⋮ menu
src/ui_name_dialog.c    the name dialog (add or rename a player; the first start)
src/ui_avatar_picker.c  the avatar picker
src/players_core.c      Players' data without LVGL calls: the list, switching, colours, avatars, add, rename, delete
src/devctl.c            serial control: screenshots, simulated touches, files, SQL (tools/devctl.py)
src/devctl_touch.c      devctl: tap, long, swipe, press, move, release
src/devctl_screen.c     devctl: shot, bench, screenoff
src/devctl_files.c      devctl: ls, get, put, mv, rm, mkdir
src/devctl_game.c       devctl: state, fill
src/devctl_genbench.c   devctl: genbench
src/devctl_sql.c        devctl: sql
src/devctl_clock.c      devctl: clock (Wi-Fi connected? time set?)
src/devctl_stream.c     devctl: data to and from the host (base64 lines, CRC, compression); devctl_internal.h lists them
src/serial_port.cpp     the USB-UART port (Arduino's Serial) as C functions, for devctl.c
src/qrcode/             Project Nayuki's QR Code generator (MIT), for Share
src/store.cpp           RAM copy of the settings; reads the old NVS keys once (store_internal.h for the parts below)
src/store_stats.cpp     RAM copy of the statistics: best time, fewest mistakes, solves per puzzle
src/store_game.cpp      RAM copy of the game in progress and its move log
src/stats_db.c          sudoku.db on the microSD card: the worker task, opening, the calls the parts share
src/stats_db_*.c        its parts, one per subject: schema, prefs, players, games, solves, kinds, generated,
                        daily, rank, achievements, import, saved, history, weekly, insights, replay, records,
                        export, small, sql
                        (stats_db_internal.h lists them; the game uses stats_db.h)
src/usb_msc.cpp         the microSD card as a USB drive (TinyUSB MSC)
src/sqlite/             SQLite 3.54.1 amalgamation + ESP-IDF file layer (sqlite3_esp.c)
src/wifi_mgr.cpp        Wi-Fi worker task (ESP-Hosted / C6), saved network, starts SNTP
src/ui_wifi.c           Wi-Fi screen: status, networks (ui_wifi_part.h)
src/ui_wifi_dialog.c    its password dialog
src/ui_file.c           Files screen: the page around its parts, the poll, shared widgets (ui_file_part.h)
src/ui_file_list.c      its list, path, count; Refresh, Up, Empty; a tap on a row
src/ui_file_dialog.c    its dialog: rename, delete into the Recycle bin, delete for good, empty the bin
src/ui_file_preview.c   its puzzle file preview + Import
src/files_core.h        what Files does on the card, without LVGL (a header for the four parts below)
src/files_folder.c      the folder shown, reading it (a worker task), its entries
src/files_names.c       protected names, a new name checked, rename, sizes, file types
src/files_trash.c       the Recycle bin: trash, restore, delete for good (a task)
src/files_preview.c     a puzzle file read, its puzzles parsed
src/gibberlink.cpp      remote control by sound: ES7210 mics -> ggwave -> devctl commands, replies by sound
src/ggwave/             ggwave 060aec7 (MIT), data over sound, vendored unchanged
src/ui_media_view.c     the Files viewer's page: the picture, the bars, swipes and taps
src/media_core.c        its decoding without LVGL: two buffers, the worker task, the page's API (media_internal.h for the parts below)
src/media_jpeg.c        a picture: JPEG (hardware decoder), scaled to fit
src/media_video.c       a video: MP4 through tinyh264, converted to RGB565 by a second task, paced, seeking
src/mp4_reader.c        MP4 index reader for the viewer: frame offsets, times, key frames, SPS/PPS
src/mp4_writer.c        MP4 writer for replay export: H.264 frames in, the index (moov) written at the end
src/video_enc.c         RGB888 frames -> the hardware H.264 encoder (colours converted by the PPA) -> mp4_writer
src/ui_sysmon.c         system monitor screen: the frame around its tabs, the refresh, the history timer (ui_sysmon_tab.h lists the tab files)
src/ui_sysmon_system.c   System monitor > System: chip, uptime, temperature, CPU load, network; CPU, temperature and Wi-Fi graphs
src/ui_sysmon_memory.c   System monitor > Memory: internal RAM, PSRAM, DMA; the free RAM graph
src/ui_sysmon_storage.c  System monitor > Storage: firmware, flash, NVS, microSD
src/ui_sysmon_graphics.c System monitor > Graphics: LVGL frame rate, draw times, objects, timers
src/ui_sysmon_tasks.c    System monitor > Tasks: the FreeRTOS task table, sortable
src/sysmon_core.h       the system monitor's numbers, without LVGL (a header for the seven parts below)
src/sysmon_chip.c       reset reason, uptime, chip, Ethernet MAC, task count
src/sysmon_cpu.c        CPU load per core, the temperature sensor
src/sysmon_memory.c     the heaps
src/sysmon_storage.c    flash, NVS, the microSD card's free space
src/sysmon_tasks.c      the FreeRTOS task list: the core-0 reader, CPU shares, sorting
src/sysmon_history.c    the last hour, a sample every 10 s
src/sysmon_format.c     sizes as text (B, KB, MB, GB)
src/timezones.c         IANA time zone names -> POSIX TZ rules
src/ui_palette.h        light/dark colour schemes shared by the UI files
src/sfx.c               sound effects: note tables + 4-voice mixer task
src/sdx.c               the puzzle line format (Import puzzles, the Files preview, Send by sound): parse, pack, format; plain C, tested on a PC
src/import_parse.c      the other puzzle files imported (a puzzle per line, OpenSudoku's XML); plain C, tested on a PC
src/gen_*.c             the puzzle generator (gen_internal.h, split by what it does: gen_geometry.c boards, gen_texts.c the layouts as text, gen_solver.c the rules and search, gen_grade.c the techniques, gen_make.c generating, gen_regions.c Jigsaw regions and Killer cages, gen_lines.c the line variants' lines, gen_hints.c hints), for the variants (Classic, X-Sudoku, Hyper, Jigsaw, Killer, Mini 6x6, Kids 4x4, Anti-Knight, Anti-King, Even-Odd, Consecutive, Non-Consecutive, Disjoint Groups, Asterisk, Kropki, XV, Greater-Than, Quadruple, Arrow, Renban, German Whispers, Skyscraper, Little Killer, Thermo, Sandwich): generator, grader, solver (Fill) and hints
src/puzzles.h           puzzle access (unpack, collection lookup)
src/puzzles_data.c      generated puzzle data
src/fonts/              Montserrat 44 and 52 px digits / 36 px timer, FontAwesome person, calendar, info, trophy, lock, star, chart, lightbulb, QR code, camera, vertical-dots (menu), undo, redo and 3x3-grid (the Built-in tab) icons at 24/20 px (lv_font_conv)
src/avatars.c           the players' avatars: their groups (generated); the pixels are extras/avatars.bin (7 MB)
src/splash_data.c / .h  the start-up splash screen as a JPEG (generated from extras/splashscreen.png)
src/avatar_store.c      the avatars from their own flash partition, and installing avatars.bin from the card
src/bsp/                display (ST7703 / JD9365), GT911 touch, ES8311 speaker, microSD, LVGL port
tools/gen_puzzles.py    regenerates src/puzzles_data.c from tools/opensudoku/
tools/gen_avatars.py    regenerates extras/avatars.bin and src/avatars.c / .h from Noto Color Emoji (Pillow with raqm)
tools/gen_splash.py     regenerates src/splash_data.c / .h from extras/splashscreen.png (Pillow)
tools/avatars_order.txt the avatars' order (sudoku.db keeps their numbers: only add at the end)
tools/devctl.py         screenshots (PNG) and touches over the serial port
tools/make_flasher_site.sh  the GitHub Pages site: web flasher + docs + code docs (Doxygen)
tools/make_docs_site.py README.md and docs/*.md as HTML pages (for the site)
docs/screenshots.txt    the script that makes docs/screenshots/*.png
tests/                  PC-side tests: make -C tests (see Tests)
tools/opensudoku/       the downloaded .opensudoku collections
```

To change the embedded puzzles, edit `COLLECTIONS` in `tools/gen_puzzles.py` and run `python3 tools/gen_puzzles.py`.

To regenerate the avatars (a newer Noto Color Emoji, or a different size), run `python3 tools/gen_avatars.py --keep tools/avatars_order.txt`: it needs Pillow with raqm, `NotoColorEmoji.ttf` and Unicode's `emoji-test.txt` (Debian: `fonts-noto-color-emoji`, `unicode-data`), and `--keep` keeps the numbers players already have (new emoji go at the end). It writes `extras/avatars.bin` (the pixels) and a small `src/avatars.c`; `AVATAR_CRC` in `src/avatars.h` changes with the pixels, so boards ignore an older set in flash and need the new `avatars.bin` installed (see [Building](building.md)).

**Splash screen**: the picture shown at start-up is `extras/splashscreen.png` (720×720). After changing it, run `python3 tools/gen_splash.py`: it writes `src/splash_data.c` (the picture as a JPEG, about 150 KB of the firmware) and `src/splash_data.h`. The board decodes it with the hardware JPEG decoder (`src/ui_splash.c`, about 35 ms), shows it while the card and `sudoku.db` are read and the screens built (about two seconds), then frees the pixels. Its **Loading...** bar shows how far start-up has got: the script erases the bar's painted fill and keeps the fill's watercolour texture apart (also in `splash_data.c`), and the board shows as much of it as start-up has done, rounded at the ends: 5 % when the picture appears, 8 % once the card is looked at, creeping toward 55 % while `sudoku.db` opens, then each screen's share as `apply_theme()` builds it (`splash_built()`, weighted by how long each took on the board; `SPLASH_BUILD_MS` in `src/ui_splash.c`, in `apply_theme()`'s order: a new screen there needs a line in both). The bar's place in the picture is a constant in `tools/gen_splash.py` (`BAR`, or `--bar`): a new picture with its bar elsewhere needs it changed. Drawing the bar costs about 0.25 s in all, while `sudoku.db` opens on its own task, so start-up takes no longer. That decoder turns YCbCr into RGB as video does ("limited range"), so the script writes the JPEG's YCbCr squeezed into that range; with a normal JPEG the light colours came out white. If the picture can't be decoded, the board shows "Starting..." as text instead.

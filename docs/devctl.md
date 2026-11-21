# Screenshots and remote control

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

The firmware takes commands on the USB-UART port (`src/devctl.c`): it can send a screenshot of exactly what the screen shows (the panel's frame buffer, dialogs and notices included) and play touches as if a finger did them (taps, long presses, swipes). `tools/devctl.py` drives it from a computer and saves the screenshots as PNG files (it needs `pip install pyserial`):

```
python3 tools/devctl.py --port /dev/ttyACM0 shot home.png
python3 tools/devctl.py --port /dev/ttyACM0 "tap 513 308; wait 400; shot puzzles.png"
python3 tools/devctl.py --port /dev/ttyACM0 --script docs/screenshots.txt
```

- Commands: `tap X Y`, `long X Y [MS]`, `swipe X1 Y1 X2 Y2 [MS]`, `press X Y` / `move X Y` / `release`, `wait MS`, `screenoff` (as the screen timeout: black, with the lock screen under it if that's on; the next tap wakes it), `shot FILE.png [--blur X1,Y1,X2,Y2 ...]`, `heap [1]` (free internal, DMA and PSRAM memory), `bench [N]` (redraws the whole screen shown N times, default 30, and prints the time per frame: for comparing rendering settings), `genbench VARIANT|all [LEVEL|all] [N]` (times the puzzle generator: N puzzles, default 5, of each variant and level from fixed seeds, with the CPU at full speed even if the screen is dark; prints average, min and max ms, how many came out at the level asked for and the clues; a **Benchmarking** panel blocks the screen meanwhile, and it refuses while a puzzle is being solved (the game screen open), being generated, or while the microSD card is in use (an MP4 export, a screenshot, Files, a firmware update, formatting, the USB drive, a `sudoku.db` save, import, export or backup); e.g. `genbench greater expert 10`, `genbench all`), `fill CELL:DIGIT ...` and `state` (below), `screen` (where the board is: `home1`, `home2`, `list`, `game`, `stats`, `sysmon` ..., with `+dialog` when a dialog is open), `expect NAME ...` (a script stops unless the board is on one of those screens: put one after each step that changes screen, so a tap or swipe that didn't land never sends the taps after it somewhere else), `clock` (Wi-Fi's state and whether the time is set, e.g. `wifi=connected time=set 2026-11-21 17:43:05`: the daily puzzle and Trophies' Daily and Weekly tabs need it), `waitclock [S]` (a script waits until the time is set, default 60 s, or stops), `ping`. Coordinates are screen pixels (0–719) from the top left. Several commands on the command line are separated by `;`; a script has one per line (`#` starts a comment).
- Opening the port restarts the board (its auto-reset circuit), so the tool waits for the `DEVCTL ready` line it prints at start-up and every run starts on a fresh Home screen, which keeps screenshot scripts repeatable. `--no-wait` skips the wait.
- The tool then switches the port to `--baud` (921600 by default) for the transfer; a screenshot takes well under a second. It is sent run-length coded (720 × 720 RGB565, runs of up to 256 equal pixels), then compressed (zlib), as numbered base64 lines with a CRC, so log lines from the firmware can't spoil it; a garbled one is asked for again.
- `docs/screenshots.txt` makes the pictures in the [README](../README.md) and in these docs (29 of them, about 2 minutes; `tour.png` is taken by hand): it only looks at screens (opens Easy #1 without playing it, opens New puzzle and Learn and cancels them, changes nothing) and writes `docs/screenshots/*.png`. It expects one recorded solve, for Gameplays and the replay (the Home menu always has its two pages); the board's other state (players, solves, the date) shows in the pictures as it is.
- The protocol, for other tools: send a command line, wait for `OK <command>` (or `ERR ...`). `shot` answers `SHOT BEGIN 720 720`, lines `S <n> <base64>`, `SHOT END <bytes> <crc32 hex>`, then `OK shot`.

The wire protocol (lines, answers, the screenshot and file formats, a minimal client) is in [devctl-protocol.md](devctl-protocol.md).

## Files on the microSD card over serial

The same port and tool reach the card's files, without taking the card out or using the USB drive (the game keeps running):

```
python3 tools/devctl.py --port /dev/ttyACM1 ls                       # the card's root
python3 tools/devctl.py --port /dev/ttyACM1 "ls puzzles"
python3 tools/devctl.py --port /dev/ttyACM1 "get sudoku.db backup.db"   # a backup
python3 tools/devctl.py --port /dev/ttyACM1 "mkdir puzzles; put mine.sdm puzzles/mine.sdm"
python3 tools/devctl.py --port /dev/ttyACM1 "put build/esp32p4_sudoku.ino.bin update.bin"   # then Settings > Data > Firmware update
python3 tools/devctl.py --port /dev/ttyACM1 "mv 'old name.txt' new.txt; rm new.txt"
```

- `ls [DIR]`, `get REMOTE [LOCAL]` (LOCAL defaults to the file's name), `put LOCAL [REMOTE]` (REMOTE defaults to LOCAL's name in the root; the board shows a toast while it receives the file, then whether it worked), `mv FROM TO` (TO must not exist), `rm PATH` (a file or an empty folder), `mkdir PATH`. Paths start at the card's root; quote the ones with spaces. No `.` or `..` parts.
- Transfers are compressed (zlib, both ways) and run at about 50 KB/s of compressed data: text and `sudoku.db` go 2–8× faster than that (a 160 KB `sudoku.db` in half a second), while a firmware image, which hardly compresses, still takes about a minute. The screen doesn't respond while one runs. Each is checked with a CRC-32: a download that arrives garbled is not saved, and an upload is written to `REMOTE.part`, which only replaces `REMOTE` once its CRC matches (a broken upload leaves the old file as it was).
- `sudoku.db` is open while the board runs, so it can be downloaded but not replaced, renamed or deleted over serial (to restore a backup, use **Settings → Data → USB drive**, which closes it first). A download is a copy of the moment: taken while a game is being saved, it may miss that save.
- `fill` enters a batch of digits in the game on screen, as if typed on the pad: `fill 1:9 4:2 13:6` or `fill (1,9), (4,2), (13,6)` (any separators: the numbers after `fill` go in pairs). Cells are 0–80, row by row (0 is the top left, 8 the top right, 9 the start of the second row); digits 1–9 (1–6 on Mini). Each one is an ordinary move (undo, the replay, sounds, mistakes), and the last one finishes the puzzle if it's solved, so a whole puzzle takes one command (`OK fill 45`: how many were entered). It's all or nothing: with no game on screen, a solved or paused one, a given, a cell off the board or a digit out of range, nothing is entered and the answer says why (`ERR fill: cell 1 is a given`). A cell that already holds its digit is skipped (a second tap would clear it). A line holds up to 81 pairs.
- `state` reads the game on screen as text, so a script needn't read the screenshot:

  ```
  STATE name="Easy #15" variant=Classic size=9 solved=0 paused=0 filled=38/81 time=2 mistakes=0 hints=0
  BOARD i147b83......3917.37...6..4..9..2.31.4.....9.83.6..2..7..5...19.9526......39.754.
  ```

  `BOARD` is 81 characters, row by row, numbered as for `fill`: `1`–`9` a given, `a`–`i` a digit entered (`a` = 1 ... `i` = 9), `.` empty, `#` a cell not on the board (Mini 6×6 uses 36 of the 81, Kids 4×4 16). `STATE` adds `practice=1` (Learn) or `attack=1` (Time attack) for those games. With no game on screen: `ERR state: no game on screen`.

## SQL on sudoku.db

```
python3 tools/devctl.py --port /dev/ttyACM1 --sql "SELECT id, name FROM players"
python3 tools/devctl.py --port /dev/ttyACM1 --mode csv --sql "SELECT * FROM solves" > solves.csv
python3 tools/devctl.py --port /dev/ttyACM1 --sqlite
```

- `--sql "..."` runs one or more statements (`;` between them) on the board's `sudoku.db` and prints each result (repeat `--sql` for more; the exit code is 1 if one failed). `--sqlite` opens a console like `sqlite3`'s: a statement ends with `;` and may span lines, the arrow keys bring back earlier ones (kept in `~/.devctl_sqlite_history`), and `.tables`, `.schema [NAME]`, `.indexes [TABLE]`, `.mode box|list|csv|tabs`, `.help` and `.quit` (or Ctrl-D) work. `--mode` picks the output: `box` (default; cells cut at 60 characters), `list` (`|` between values), `csv`, `tabs`. NULL shows as `NULL` in a box and empty in the others; a blob as `x'<hex>'`.
- The board runs it on the database's own task (the one that does all of `sudoku.db`'s work), so it never collides with a save. A statement gets 20 seconds, then it's interrupted; a result returns up to 256 KB (`LIMIT` the rest); the SQL itself can be about 3000 bytes. The screen doesn't respond while a statement runs.
- Writes work (`UPDATE`, `DELETE`, `CREATE TEMP TABLE` ...), but the screens keep their own copies of the statistics and settings, so a change shows after a restart (or, for statistics, a player switch). A transaction left open (`BEGIN` without `COMMIT`) is rolled back at the end, so the game's own saves can't get stuck behind it. Back up first (**Settings → Data → Back up database**, or `get sudoku.db`).
- SQL is serial only: it doesn't work over sound.

- The touch commands, `wait`, `fill`, `state`, `pz`, `ping`, `heap` and `screenoff` also work as sound, with no cable: see Gibberlink in [Home, settings & system](features-settings.md) (**Settings → Sound → Gibberlink**).
- On the board, the commands are lines on the serial port (`src/devctl.h` has the protocol): a download comes as base64 lines like a screenshot; an upload sends base64 lines with an `ACK` from the board after every 12, so its 2 KB receive buffer never overflows.

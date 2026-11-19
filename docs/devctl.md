# Screenshots and remote control

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

The firmware takes commands on the USB-UART port (`src/devctl.cpp`): it can send a screenshot of exactly what the screen shows (the panel's frame buffer, dialogs and notices included) and play touches as if a finger did them (taps, long presses, swipes). `tools/devctl.py` drives it from a computer and saves the screenshots as PNG files (it needs `pip install pyserial`):

```
python3 tools/devctl.py --port /dev/ttyACM0 shot home.png
python3 tools/devctl.py --port /dev/ttyACM0 "tap 513 308; wait 400; shot puzzles.png"
python3 tools/devctl.py --port /dev/ttyACM0 --script docs/screenshots.txt
```

- Commands: `tap X Y`, `long X Y [MS]`, `swipe X1 Y1 X2 Y2 [MS]`, `press X Y` / `move X Y` / `release`, `wait MS`, `screenoff` (as the screen timeout: black, with the lock screen under it if that's on; the next tap wakes it), `shot FILE.png [--blur X1,Y1,X2,Y2 ...]`, `heap [1]` (free internal, DMA and PSRAM memory), `ping`. Coordinates are screen pixels (0–719) from the top left. Several commands on the command line are separated by `;`; a script has one per line (`#` starts a comment).
- Opening the port restarts the board (its auto-reset circuit), so the tool waits for the `DEVCTL ready` line it prints at start-up and every run starts on a fresh Home screen, which keeps screenshot scripts repeatable. `--no-wait` skips the wait.
- The tool then switches the port to `--baud` (921600 by default) for the transfer; a screenshot takes about 2 seconds. It is sent run-length coded (720 × 720 RGB565, runs of up to 256 equal pixels) as numbered base64 lines with a CRC, so log lines from the firmware can't spoil it; a garbled one is asked for again.
- `docs/screenshots.txt` makes the pictures in the [README](../README.md): it only looks at screens (opens Easy #1 without playing it, changes nothing) and writes `docs/screenshots/*.png`.
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

- `ls [DIR]`, `get REMOTE [LOCAL]` (LOCAL defaults to the file's name), `put LOCAL [REMOTE]` (REMOTE defaults to LOCAL's name in the root), `mv FROM TO` (TO must not exist), `rm PATH` (a file or an empty folder), `mkdir PATH`. Paths start at the card's root; quote the ones with spaces. No `.` or `..` parts.
- Transfers run at about 50 KB/s (a 2.2 MB firmware takes about 45 s); the screen doesn't respond while one runs. Each is checked with a CRC-32: a download that arrives garbled is not saved, and an upload is written to `REMOTE.part`, which only replaces `REMOTE` once its CRC matches (a broken upload leaves the old file as it was).
- `sudoku.db` is open while the board runs, so it can be downloaded but not replaced, renamed or deleted over serial (to restore a backup, use **Settings → Data → USB drive**, which closes it first). A download is a copy of the moment: taken while a game is being saved, it may miss that save.
- On the board, the commands are lines on the serial port (`src/devctl.h` has the protocol): a download comes as base64 lines like a screenshot; an upload sends base64 lines with an `ACK` from the board after every 12, so its 2 KB receive buffer never overflows.

# devctl serial protocol

`devctl` is the board's remote control over its USB-UART port: screenshots, touches, and files on the microSD card. It's built for documentation and testing. `tools/devctl.py` is the ready-made client. This page describes the wire protocol, for writing a client of your own or for debugging one.

- Firmware: `src/devctl.cpp`. The summary in `src/devctl.h` should match this page.
- Commands run in the LVGL task (`devctl_poll()` from `loop()`), one at a time. The UI doesn't run while a command is busy, except where the command lets it (`wait`, touches).

## Connection

| | |
|---|---|
| Port | The **UART** USB-C port (CH343 bridge, `/dev/ttyACM*` on Linux, VID:PID `1a86:55d3`). Not the USB OTG port. |
| Settings | 115200 baud at boot, 8N1, no flow control |
| Reset | Opening the port pulses DTR/RTS, and the board's auto-reset circuit **restarts it**. Expect a fresh boot every time the port is opened. Set DTR and RTS **low before opening** (in pyserial: `dtr = rts = False` on an unopened `Serial()`): if they stay asserted, as pyserial and many terminals do by default, the board is held in reset or download mode. |
| Ready | After boot the board prints the line `DEVCTL ready`. Commands sent before it are lost. Wait about 1 s more for the first screen to settle. With Wi-Fi, allow about 12 s if the clock must be set (time on Home, the daily puzzle). |

The port also carries the firmware's log (`printf`, ESP-IDF `E (...)` lines, crash dumps). These can show up between protocol lines at any time. A client must skip lines it doesn't expect, and never assume the next line is the answer.

## Framing

- **Host → board:** one command per line, ended by `\n` (`\r` is ignored). Up to 255 characters. Arguments are separated by single spaces. Bytes outside printable ASCII (32–126) are dropped, so line noise left by the reset is harmless. Sending a bare `\n` first is a good way to clear any half line.
- **Board → host:** lines ended by `\n`.
- **Answers:** every command ends with exactly one final line:
  - `OK <command>` once it's done (for some commands, with extra fields, see below), or
  - `ERR <reason>` if it failed or wasn't understood (`ERR unknown: <line>` for an unknown command).

  Send the next command only after the final line. The board reads its input between commands, and its receive buffer is 2 KB.
- **Numbers:** decimal, except CRC-32 values: 8 hex digits, lowercase from the board.
- **CRC-32:** the usual one (zlib / PNG / Ethernet, polynomial `0xEDB88320` reflected, init and final XOR `0xFFFFFFFF`), as Python's `zlib.crc32`.
- **Base64:** standard alphabet with `+` `/` and `=` padding.

## Commands

### Session

| Command | Answer | Notes |
|---|---|---|
| `ping` | `OK pong` | |
| `heap [1]` | `HEAP internal <free>, largest <block>, min ever <n>; dma <free>, largest <block>; psram <free>, largest <block>`, then `OK heap` | Free memory in bytes. With `1`, ESP-IDF's per-region summary follows (`heap_caps_print_heap_info`). Internal RAM is the scarce one: the MP4 export needs one free block of about 52 KB. |
| `baud <n>` | `OK baud <n>` | Sent at the old speed. The board then switches its port to `n` (≥ 9600). Switch the host side too, wait about 100 ms, flush the input, and `ping` to check. `tools/devctl.py` uses 921600. The speed lasts until the next reset. |

### Touch and screen

Coordinates are screen pixels, 0–719, from the top left. Values outside are clamped. While a command touches, the real touch panel is ignored; the panel takes over again after the release.

| Command | What it does |
|---|---|
| `tap <x> <y>` | Presses for 120 ms, then releases. |
| `long <x> <y> [ms]` | Presses for `ms` (default 1000), then releases. |
| `swipe <x1> <y1> <x2> <y2> [ms]` | Presses at the start, moves in steps of about 20 ms over `ms` (default 300), then releases at the end. |
| `press <x> <y>` | Puts a finger down (for gestures by hand). |
| `move <x> <y>` | Moves it, still pressed. |
| `release [<x> <y>]` | Lifts it, at that point (default 0 0). |
| `wait <ms>` | Lets the UI run for `ms`: animations, timers, and database results arriving. |
| `state` | The game on screen: `STATE name="<name>" variant=<v> size=<9\|6> solved=<0\|1> paused=<0\|1> filled=<n>/<cells> time=<s> mistakes=<n> hints=<n>` (plus ` practice=1` / ` attack=1`), then `BOARD <81 characters>` (row by row: `1`-`9` given, `a`-`i` entered, `.` empty, `#` not on the board), then `OK state`; or `ERR state: no game on screen`. Over Gibberlink the answer is one line: `OK state s<solved> <81 characters>`. |
| `fill <cell> <digit> ...` | Enters digits in the game on screen as if typed: cells 0–80 row by row, digits 1–9; the numbers after `fill` are taken in pairs, any separators (`fill 12:5 13:7`, `fill (12,5),(13,7)`). All or nothing. Answers `OK fill <entered>` or `ERR fill: <why>`. Up to 81 pairs (lines up to 511 characters). |
| `screenoff` | Turns the screen off as its timeout does: black, with the lock screen under it if that's on. The next tap wakes it. |
| `shot` | A screenshot (below). |

Every release is followed by 150 ms of UI time before the `OK`, so the tap's effect has started. Use `wait` for anything slower, such as a screen change with an animation (about 400 ms) or a database query.

### Screenshot

`shot` sends what the panel shows (its frame buffer), dialogs and notices included:

```
SHOT BEGIN 720 720
S 0 <base64>
S 1 <base64>
...
SHOT END <bytes> <crc32>
OK shot
```

- **`S` lines:** each carries up to 96 bytes (128 base64 characters), numbered from 0. Put the bytes back together in order of `n`.
- **`SHOT END`:** `<bytes>` is the total byte count and `<crc32>` covers all of them. If a log line cut into an `S` line, the CRC doesn't match: ask again.
- **Pixel format:** the bytes are run-length coded RGB565 pixels, row by row from the top left. Each run is three bytes, `[count − 1] [pixel low byte] [pixel high byte]`, for 1–256 equal pixels.
- **Decoding:** RGB565 is `RRRRRGGG GGGBBBBB` (high byte first when read as a 16-bit value). For 8-bit colour, `r8 = r5<<3 | r5>>2`, `g8 = g6<<2 | g6>>4`, `b8 = b5<<3 | b5>>2`. The runs add up to exactly 720 × 720 pixels.
- **Cost:** the size depends on the screen: flat areas code small, photos and gradients large. At 921600 baud a typical screen takes a few seconds.

If the display has no frame buffer, the answer is `ERR shot: no frame buffer`, with no `OK`.

### Files on the microSD card

**Paths** start at the card's root (`puzzles/mine.sdm`; a leading `/` is ignored, and `""` or `/` is the root). A path is one word: write a space, `%`, a control byte, or any non-ASCII byte as `%XX` (hex, of the UTF-8 bytes), for example `old%20name.txt`. The board decodes any `%XX`. The command line drops non-ASCII bytes, so they must be coded. `.`, `..` and empty parts are refused (`ERR <cmd>: no '.', '..' or empty parts in a path`). Names are FAT long names and case-insensitive.

**`sudoku.db`** (and its `-journal`) is open while the board runs, so `put`, `mv` and `rm` on it answer `ERR <cmd>: sudoku.db is open (use Settings > USB drive)`. `get` works. The copy is of that moment, and may miss a save that is still being written.

#### `ls [<dir>]`

```
L d 0 replays
L f 73728 sudoku.db
OK ls
```

One line per entry, in the card's order: `L <d|f> <size> <name>`. Folders have size 0. In names the board codes spaces, `%` and control bytes as `%XX`; other bytes (UTF-8) come as they are.

#### `get <path>`

```
FILE BEGIN <size>
F 0 <base64>
F 1 <base64>
...
FILE END <bytes> <crc32>
OK get
```

The same framing as a screenshot, but with `F` lines and the raw file bytes (no run-length coding). An empty file sends no `F` lines and ends with `FILE END 0 00000000`. If the file can't be opened: `ERR get: can't read <path> (<reason>)`.

#### `put <path> <size> <crc32>`

Uploads `<size>` bytes whose CRC-32 is `<crc32>` (hex).

```
host:  put puzzles/mine.sdm 5000 1a2b3c4d
board: READY
host:  D <base64 of up to 96 bytes>      × 12
board: ACK 1152
host:  D <base64>                        × 12
board: ACK 2304
...
host:  D <base64>                        (the last ones)
board: OK put 5000
```

- **Data:** after `READY`, send `D <base64>` lines, each up to 96 bytes decoded (128 characters). Shorter lines are fine anywhere; a longer one is refused.
- **Pacing:** after every 12 data lines the board answers `ACK <bytes received>`. Wait for it before sending the next 12. That keeps at most about 1.6 KB in flight, inside the 2 KB receive buffer, while the card is written. No `ACK` follows the batch that completes the file: the board answers `OK put <size>` instead.
- **Safety:** the data goes to `<path>.part`. Only when all bytes are in and the CRC matches does it replace `<path>`, so a failed upload leaves the old file untouched.
- **Errors:** they answer `ERR put: <reason>`, and the `.part` file is deleted. The board then reads and drops input until 300 ms of quiet, so leftover data lines aren't taken as commands. The reasons are:
  - `timed out waiting for data` (5 s with no line)
  - `expected a data line`
  - `bad data line` (not base64, or more than the size)
  - `write failed (card full?)`
  - `CRC mismatch`
  - `couldn't rename the .part file`
  - `can't write <path>.part (<reason>)` (no such folder, for example), which comes instead of `READY`
- **Folders:** `put` doesn't make them. Use `mkdir` first.
- **Speed:** about 50 KB/s at 921600 baud.

#### `mv <from> <to>`, `rm <path>`, `mkdir <path>`

| Command | Answer | Notes |
|---|---|---|
| `mv <from> <to>` | `OK mv` | Renames or moves within the card. `<to>` must not exist (`ERR mv: <to> exists (rm it first)`). |
| `rm <path>` | `OK rm` | Deletes a file, or a folder that is empty. |
| `mkdir <path>` | `OK mkdir` | Makes one folder; its parent must exist. |

Failures answer `ERR <cmd>: <path>: <reason>` with the system's reason, for example `No such file or directory`.

## A minimal client

```python
import serial, time, zlib, base64

s = serial.Serial()
s.port, s.baudrate, s.timeout = "/dev/ttyACM1", 115200, 0.5
s.dtr = s.rts = False          # set before opening: held asserted, they keep the board in reset
s.open()                       # (the open itself still restarts it)
while b"DEVCTL ready" not in s.readline():
    pass
time.sleep(1)

def cmd(line, final):
    s.write((line + "\n").encode())
    while True:
        l = s.readline().decode(errors="replace").strip()
        if l.startswith(final) or l.startswith("ERR"):
            return l

print(cmd("ping", "OK"))                       # OK pong
print(cmd("tap 513 308", "OK"))                # Home > Puzzles

data = b"hello\n"
print(cmd("put hello.txt %d %08x" % (len(data), zlib.crc32(data)), "READY"))
print(cmd("D " + base64.b64encode(data).decode(), "OK put"))
```

`tools/devctl.py` adds the speed switch, screenshot decoding to PNG (with `--blur` for private areas), downloads, paced uploads, scripts (`--script`, one command per line, `#` comments) and retries on garbled screenshots. Run `python3 tools/devctl.py -h` for its usage.

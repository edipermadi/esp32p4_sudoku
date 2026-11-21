#!/usr/bin/env python3
"""
Drive the board over its USB-UART port: screenshots (PNG) and touches, for
documentation and testing. The firmware side is src/devctl.c.

    python3 tools/devctl.py [--port /dev/ttyACM0] [--baud 921600] COMMAND...
    python3 tools/devctl.py --script docs/shots.txt

Commands (on the command line, or one per line in a script; # starts a comment):
    tap X Y                 a short tap
    long X Y [MS]           a long press (default 1000 ms)
    swipe X1 Y1 X2 Y2 [MS]  a drag (default 300 ms)
    press X Y | move X Y | release [X Y]
    wait MS                 let the UI run
    screenoff               turn the screen off as its timeout does (the next tap wakes it)
    state                   the game on screen as text: a STATE line (name, variant,
                            solved, time, mistakes ...) and a BOARD line, 81 characters
                            row by row: 1-9 given, a-i entered (a = 1), . empty, # off the board
    fill CELL:DIGIT ...     enter digits in the game shown, as if typed (cells 0-80, row by
                            row; any separators: "fill 12:5 13:7" or "fill (12,5), (13,7)");
                            all or nothing: a given, a cell off the board or no game -> ERR
    heap [1]                free memory (internal, DMA, PSRAM); 1: per heap region too
    bench [N]               redraw the whole screen N times (default 30), print the time per frame
    genbench VARIANT|all [LEVEL|all] [N]
                            time the puzzle generator: N puzzles (default 5) of each variant and
                            level, the same seeds every run, the CPU at full speed even with the
                            screen dark; names, numbers or the start of a name ("greater", "kids")
    shot FILE.png [--blur X1,Y1,X2,Y2 ...]
                            save the screen as a PNG; --blur pixelates those
                            rectangles first (e.g. a network name)
    screen                  where the board is: home1, home2, list, game, stats, settings, sysmon,
                            players, trophies, ... with +dialog when a dialog is open
    expect NAME ...         (scripts) stop unless the board is on one of those screens
    clock                   Wi-Fi's state and whether the time is set: "wifi=connected time=set
                            2026-11-21 17:43:05" (local time) or "wifi=connecting time=unset"
    waitclock [S]           (scripts) wait until the time is set (default 60 s), else stop
    ping

Files on the microSD card (paths from the card's root, e.g. puzzles/a.sdm):
    ls [DIR]                list a folder (the root by default)
    get REMOTE [LOCAL]      download (LOCAL defaults to the file's name, here)
    put LOCAL [REMOTE]      upload (REMOTE defaults to LOCAL's name, in the root);
                            it lands as REMOTE.part, renamed once its CRC checks out
    mv FROM TO              rename or move (TO must not exist)
    rm PATH                 delete a file or an empty folder
    mkdir PATH              make a folder
sudoku.db is open while the board runs: get works, put/mv/rm on it don't
(use Settings > Data > USB drive). Paths with spaces: quote them.

SQL on sudoku.db, on the board (its database task runs it; 20 s at most a statement):
    python3 tools/devctl.py --sql "SELECT * FROM players"      one or more statements, then exit
    python3 tools/devctl.py --sqlite                           a console: statements end with ';'
                                                               (several lines are fine), and
    .tables  .schema [NAME]  .indexes  .mode box|list|csv|tabs  .help  .quit
--mode picks how results print (box by default). Writes work too, but the screens keep their
own copies of statistics and settings: changes show after a restart (or a player switch). A
BEGIN without its COMMIT is rolled back at the end of the statement batch.

    python3 tools/devctl.py --port /dev/ttyACM1 "put build/esp32p4_sudoku.ino.bin update.bin"
    python3 tools/devctl.py --port /dev/ttyACM1 "get sudoku.db backup.db; ls puzzles"

Opening the port resets the board (its auto-reset circuit), so a run starts
from a freshly booted Home screen: the tool waits for "DEVCTL ready" first
(--no-wait skips that, for a board that didn't reset). Then it switches to
--baud for the transfers, which go compressed (zlib) both ways: screenshots, files and SQL.
Needs pyserial (pip install pyserial).

Coordinates are screen pixels, 0..719, from the top left.
"""
import argparse
import base64
import csv
import io
import os
import shlex
import sqlite3
import unicodedata
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("devctl.py needs pyserial: pip install pyserial")

BOOT_BAUD = 115200


class Board:
    def __init__(self, port, baud, wait_boot, verbose):
        self.verbose = verbose
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = BOOT_BAUD
        self.ser.timeout = 0.5
        self.ser.dtr = False                       # (the open still pulses them: the board resets)
        self.ser.rts = False
        self.ser.open()
        if wait_boot:
            self.wait_for("DEVCTL ready", 30)
            time.sleep(1.0)                        # the first screen settles
        self.ser.write(b"\n")                     # ends any half line the reset left behind
        time.sleep(0.05)
        if baud != BOOT_BAUD:
            self.command("baud %d" % baud, expect="OK baud")
            self.ser.baudrate = baud
            time.sleep(0.1)
            self.ser.reset_input_buffer()
            self.command("ping", expect="OK pong")

    def readline(self, timeout):
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            b = self.ser.readline()
            if not b:
                continue
            buf += b
            if buf.endswith(b"\n"):
                line = buf.decode("utf-8", "replace").rstrip("\r\n")
                return line
        return None

    def wait_for(self, text, timeout, fail=None):
        end = time.time() + timeout
        while time.time() < end:
            line = self.readline(end - time.time())
            if line is None:
                break
            if self.verbose:
                print("  <", line)
            if text in line:
                return line
            if fail and line.startswith(fail):
                sys.exit(line)
        sys.exit("timed out waiting for %r (is the board on %s?)" % (text, self.ser.port))

    # ---- files on the card

    def ls(self, path):
        self.ser.write(("ls %s\n" % enc(path)).encode())
        rows = []
        end = time.time() + 20
        while time.time() < end:
            line = self.readline(10)
            if line is None:
                break
            if line.startswith("L "):
                _, kind, size, name = line.split(" ", 3)
                rows.append((kind, int(size), dec(name)))
            elif line.startswith("OK ls"):
                for kind, size, name in sorted(rows, key=lambda r: (r[0] != "d", r[2].lower())):
                    print("%-4s %10s  %s" % ("dir" if kind == "d" else "", "" if kind == "d" else size, name))
                return
            elif line.startswith("ERR"):
                sys.exit(line)
        sys.exit("ls: no answer")

    def get(self, remote, local):
        self.ser.write(("get %s z\n" % enc(remote)).encode())   # z: sent compressed
        begin = self.wait_for("FILE BEGIN", 20, fail="ERR")
        chunks, end_line = {}, None
        t0 = time.time()
        while True:
            line = self.readline(10)
            if line is None:
                sys.exit("get: the transfer stopped")
            if line.startswith("F "):
                parts = line.split(" ", 2)
                if len(parts) == 3 and parts[1].isdigit():
                    try:
                        chunks[int(parts[1])] = base64.b64decode(parts[2])
                    except Exception:
                        pass                       # a log line got in the way: the CRC will tell
            elif line.startswith("FILE END"):
                end_line = line
            elif line.startswith("OK get"):
                break
            elif self.verbose:
                print("  <", line)
        _, _, size, crc = end_line.split()
        data = unpack(b"".join(chunks[i] for i in sorted(chunks)), begin.endswith(" z"))
        if data is None or len(data) != int(size) or zlib.crc32(data) != int(crc, 16):
            sys.exit("get: %s arrived garbled (try again)" % remote)
        with open(local, "wb") as f:
            f.write(data)
        dt = max(time.time() - t0, 1e-3)
        print("got %s -> %s, %d bytes, %d compressed (%.0f KB/s)" % (remote, local, len(data),
                                                                   sum(len(c) for c in chunks.values()), len(data) / 1024 / dt))

    def put(self, local, remote, window=12, chunk=96):
        with open(local, "rb") as f:
            data = f.read()
        packed = zlib.compress(data, 9)              # sent compressed: the board inflates it
        self.ser.write(("put %s %d %08x z %d\n" % (enc(remote), len(data), zlib.crc32(data), len(packed))).encode())
        self.wait_for("READY", 20, fail="ERR")
        t0 = time.time()
        lines = [packed[i:i + chunk] for i in range(0, len(packed), chunk)]
        for i in range(0, len(lines), window):
            batch = lines[i:i + window]
            self.ser.write(b"".join(b"D " + base64.b64encode(c) + b"\n" for c in batch))
            if i + window < len(lines):
                self.wait_for("ACK", 20, fail="ERR")
            if sys.stdout.isatty():
                print("\r  %d / %d bytes (compressed)" % (min((i + window) * chunk, len(packed)), len(packed)),
                      end="", flush=True)
        if sys.stdout.isatty():
            print()
        self.wait_for("OK put", 60, fail="ERR")
        dt = max(time.time() - t0, 1e-3)
        print("put %s -> %s, %d bytes, %d compressed (%.0f KB/s)" % (local, remote, len(data), len(packed),
                                                                  len(data) / 1024 / dt))

    def command(self, cmd, expect=None, timeout=20):
        if self.verbose:
            print("  >", cmd)
        self.ser.write((cmd + "\n").encode())
        word = cmd.split()[0]
        return self.wait_for(expect or ("OK " + word), timeout)

    def shot(self, path, blur=(), tries=3):
        for attempt in range(tries):
            self.ser.write(b"shot z\n")                # z: sent compressed
            begin = self.wait_for("SHOT BEGIN", 10)
            chunks = {}
            end_line = None
            deadline = time.time() + 120
            while time.time() < deadline:
                line = self.readline(10)
                if line is None:
                    break
                if line.startswith("S "):
                    parts = line.split(" ", 2)
                    if len(parts) == 3 and parts[1].isdigit():
                        try:
                            chunks[int(parts[1])] = base64.b64decode(parts[2])
                        except Exception:
                            pass                   # a log line got in the way: the CRC will tell
                elif line.startswith("SHOT END"):
                    end_line = line
                elif line.startswith("OK shot"):
                    break
                elif self.verbose:
                    print("  <", line)
            if end_line:
                _, _, size, crc = end_line.split()
                data = unpack(b"".join(chunks[i] for i in sorted(chunks)), begin.endswith(" z"))
                if data is not None and len(data) == int(size) and zlib.crc32(data) == int(crc, 16):
                    rgb = decode_rle(data, 720, 720)
                    for box in blur:
                        rgb = pixelate(rgb, 720, 720, box)
                    write_png(path, rgb, 720, 720)
                    print("saved", path)
                    return
            print("screenshot garbled, retrying (%d)" % (attempt + 1))
        sys.exit("couldn't get a clean screenshot")


def unpack(data, compressed):
    """A transfer's bytes as sent: inflated if they came compressed; None if that fails."""
    if not compressed:
        return data
    try:
        return zlib.decompress(data)
    except zlib.error:
        return None                                 # (a line lost: the caller says it's garbled)


# ---- SQL on the board's sudoku.db

def sql_unescape(field):
    if field == "\\N":
        return None
    out, i = [], 0
    while i < len(field):
        c = field[i]
        if c == "\\" and i + 1 < len(field):
            out.append({"t": "\t", "n": "\n", "r": "\r"}.get(field[i + 1], field[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def board_sql(board, text, timeout=90):
    """Runs text on the board: [(columns, rows), ...] per statement with columns, the totals
    (rows, changes, truncated), or an error message (then the results so far, too)."""
    if board.verbose:
        print("  > sql", text)
    raw = text.encode()
    coded = base64.b64encode(zlib.compress(raw, 9)).decode()   # sent compressed, the result comes so too
    if len(coded) > 4070 or len(raw) > 65536:      # (the board's line holds 4096: longer is cut off)
        return [], None, "the SQL is too long for the board (%d bytes; compressed, it must fit in 3 KB)" % len(raw)
    board.ser.write(("sql z %d %s\n" % (len(raw), coded)).encode())
    sets, end, packed, sql_end = [], time.time() + timeout, {}, None

    def take(line):
        if line.startswith("SQL C\t") or line == "SQL C":
            sets.append(([sql_unescape(f) for f in line[6:].split("\t")], []))
        elif line.startswith("SQL R"):
            row = [sql_unescape(f) for f in line[6:].split("\t")] if len(line) > 5 else []
            if not sets:
                sets.append(([""] * len(row), []))
            sets[-1][1].append(row)

    while time.time() < end:
        line = board.readline(end - time.time())
        if line is None:
            break
        if line.startswith("Q "):                   # the result, compressed
            parts = line.split(" ", 2)
            if len(parts) == 3 and parts[1].isdigit():
                try:
                    packed[int(parts[1])] = base64.b64decode(parts[2])
                except Exception:
                    pass
            continue
        if line.startswith("SQL END"):
            sql_end = line.split()
            data = unpack(b"".join(packed[i] for i in sorted(packed)), True)
            if data is None or len(data) != int(sql_end[2]) or zlib.crc32(data) != int(sql_end[3], 16):
                return sets, None, "the result arrived garbled (try again)"
            for l in data.decode("utf-8", "replace").split("\n"):
                take("SQL " + l)
            continue
        if line.startswith("SQL BEGIN"):
            continue
        if line.startswith(("SQL C", "SQL R")):     # (a board that answers uncompressed)
            take(line)
        elif line.startswith("OK sql"):
            w = line.split()
            return sets, (int(w[2]), int(w[3]), "truncated" in w), None
        elif line.startswith("ERR sql"):
            return sets, None, line[len("ERR sql: "):]
        elif board.verbose:
            print("  <", line)
    return sets, None, "no answer from the board (timed out)"


def cols_of(s):
    """Terminal columns of s: wide (CJK, emoji) characters take two."""
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 0 if unicodedata.combining(c) else 1 for c in s)


def pad(s, w, right=False):
    gap = " " * max(0, w - cols_of(s))
    return gap + s if right else s + gap


def show_value(v, width=None):
    s = "NULL" if v is None else v
    s = s.replace("\n", "\\n").replace("\t", "\\t") if width else s
    return s[:width - 1] + "…" if width and len(s) > width else s


def print_result(cols, rows, mode, out=sys.stdout):
    if mode == "csv":
        w = csv.writer(out, lineterminator="\n")
        w.writerow(cols)
        w.writerows([["" if v is None else v for v in r] for r in rows])
    elif mode == "tabs":
        print("\t".join(cols), file=out)
        for r in rows:
            print("\t".join("" if v is None else v for v in r), file=out)
    elif mode == "list":
        print("|".join(cols), file=out)
        for r in rows:
            print("|".join("" if v is None else v for v in r), file=out)
    else:                                           # box, as sqlite3 -box (cells cut at 60)
        cells = [[show_value(v, 60) for v in r] for r in rows]
        wid = [max([cols_of(c)] + [cols_of(r[i]) for r in cells]) for i, c in enumerate(cols)]
        line = lambda l, m, r: l + m.join("─" * (w + 2) for w in wid) + r
        print(line("┌", "┬", "┐"), file=out)
        print("│" + "│".join(" %s " % pad(c, w) for w, c in zip(wid, cols)) + "│", file=out)
        print(line("├", "┼", "┤"), file=out)
        for r in cells:
            print("│" + "│".join(" %s " % pad(v, w, v.lstrip("-").replace(".", "", 1).isdigit())
                                 for w, v in zip(wid, r)) + "│", file=out)
        print(line("└", "┴", "┘"), file=out)


def run_sql(board, text, mode):
    """Runs text and prints what came back; False on an error."""
    sets, totals, err = board_sql(board, text)
    for cols, rows in sets:
        print_result(cols, rows, mode)
    if err:
        print("Error: %s" % err, file=sys.stderr)
        return False
    rows, changes, truncated = totals
    if truncated:
        print("(the result was cut off: the board returns up to 256 KB; add a LIMIT)", file=sys.stderr)
    if changes:
        print("(%d row%s changed; changes to sudoku.db show on the screens after a restart)"
              % (changes, "" if changes == 1 else "s"), file=sys.stderr)
    return True


SQLITE_HELP = """Statements end with ';' and may span lines. Dot commands:
  .tables              the tables and views
  .schema [NAME]       CREATE statements (all, or those whose name matches NAME, % and _ as in LIKE)
  .indexes [TABLE]     the indexes
  .mode box|list|csv|tabs
  .help                this
  .quit / .exit        leave (Ctrl-D too)"""


def sqlite_console(board, mode):
    try:
        import readline
        hist = os.path.expanduser("~/.devctl_sqlite_history")
        try:
            readline.read_history_file(hist)
        except OSError:
            pass
    except ImportError:
        readline = None
    print("sudoku.db on the board (%s). .help for help, .quit to leave." % board.ser.port)
    buf = ""
    try:
        while True:
            try:
                line = input("sudoku.db> " if not buf else "      ...> ")
            except EOFError:
                print()
                break
            except KeyboardInterrupt:
                print()
                buf = ""
                continue
            if not buf and line.strip().startswith("."):
                words = line.split()
                cmd, arg = words[0], words[1] if len(words) > 1 else None
                q = arg.replace("'", "''") if arg else None
                if cmd in (".quit", ".exit"):
                    break
                elif cmd == ".help":
                    print(SQLITE_HELP)
                elif cmd == ".mode":
                    if arg in ("box", "list", "csv", "tabs"):
                        mode = arg
                    else:
                        print("modes: box list csv tabs (now %s)" % mode)
                elif cmd == ".tables":
                    sets, _, err = board_sql(board, "SELECT name FROM sqlite_schema WHERE type IN ('table', 'view') "
                                                    "AND name NOT LIKE 'sqlite_%' ORDER BY name")
                    print(err or "  ".join(r[0] for _, rows in sets for r in rows))
                elif cmd == ".schema":
                    where = " AND name LIKE '%s'" % q if q else ""
                    sets, _, err = board_sql(board, "SELECT sql || ';' FROM sqlite_schema WHERE sql IS NOT NULL"
                                                    "%s ORDER BY type DESC, name" % where)
                    print(err or "\n".join(r[0] for _, rows in sets for r in rows))
                elif cmd == ".indexes":
                    where = " AND tbl_name LIKE '%s'" % q if q else ""
                    sets, _, err = board_sql(board, "SELECT name FROM sqlite_schema WHERE type = 'index'%s "
                                                    "ORDER BY name" % where)
                    print(err or "\n".join(r[0] for _, rows in sets for r in rows))
                else:
                    print("unknown: %s (.help)" % cmd)
                continue
            buf += line + "\n"
            if sqlite3.complete_statement(buf):
                if buf.strip():
                    run_sql(board, buf, mode)
                buf = ""
    finally:
        if readline:
            try:
                readline.write_history_file(hist)
            except OSError:
                pass


def enc(path):
    """a path as one word for the board: %XX for spaces, '%' and other unusual bytes"""
    out = []
    for b in path.encode("utf-8"):
        out.append("%%%02X" % b if b <= 32 or b == 37 or b >= 127 else chr(b))
    return "".join(out)


def dec(word):
    out, i, raw = bytearray(), 0, word.encode("latin-1", "replace")
    while i < len(raw):
        if raw[i] == 37 and i + 3 <= len(raw):
            try:
                out.append(int(raw[i + 1:i + 3], 16))
                i += 3
                continue
            except ValueError:
                pass
        out.append(raw[i])
        i += 1
    return out.decode("utf-8", "replace")


def decode_rle(data, w, h):
    """[count-1][lo][hi] runs of RGB565 -> RGB888 bytes"""
    out = bytearray(w * h * 3)
    o = 0
    for i in range(0, len(data) - 2, 3):
        run = data[i] + 1
        v = data[i + 1] | data[i + 2] << 8
        r = (v >> 11) & 31
        g = (v >> 5) & 63
        b = v & 31
        px = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        out[o:o + 3 * run] = px * run
        o += 3 * run
    if o != len(out):
        raise ValueError("screenshot size mismatch: %d of %d pixels" % (o // 3, w * h))
    return bytes(out)


def pixelate(rgb, w, h, box, block=14):
    """the rectangle (x1, y1, x2, y2) as coarse blocks of their average colour: unreadable"""
    x1, y1, x2, y2 = (max(0, min(v, lim)) for v, lim in zip(box, (w, h, w, h)))
    out = bytearray(rgb)
    for by in range(y1, y2, block):
        for bx in range(x1, x2, block):
            ys, xs = range(by, min(by + block, y2)), range(bx, min(bx + block, x2))
            n = len(ys) * len(xs)
            acc = [0, 0, 0]
            for y in ys:
                for x in xs:
                    o = (y * w + x) * 3
                    acc[0] += rgb[o]; acc[1] += rgb[o + 1]; acc[2] += rgb[o + 2]
            px = bytes(v // n for v in acc)
            for y in ys:
                o = (y * w + xs.start) * 3
                out[o:o + 3 * len(xs)] = px * len(xs)
    return bytes(out)


def write_png(path, rgb, w, h):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def run(board, words):
    if not words:
        return
    cmd = words[0]
    if cmd == "shot":
        if len(words) < 2:
            sys.exit("shot needs a file name")
        blur = []
        rest = words[2:]
        if rest and rest[0] == "--blur":
            for spec in rest[1:]:
                try:
                    x1, y1, x2, y2 = (int(v) for v in spec.split(","))
                except ValueError:
                    sys.exit("--blur takes X1,Y1,X2,Y2 rectangles: %s" % spec)
                blur.append((x1, y1, x2, y2))
        elif rest:
            sys.exit("unknown shot option: %s" % " ".join(rest))
        board.shot(words[1], blur)
    elif cmd == "ls":
        board.ls(words[1] if len(words) > 1 else "")
    elif cmd == "get":
        if len(words) < 2:
            sys.exit("get REMOTE [LOCAL]")
        board.get(words[1], words[2] if len(words) > 2 else os.path.basename(words[1].rstrip("/")))
    elif cmd == "put":
        if len(words) < 2:
            sys.exit("put LOCAL [REMOTE]")
        board.put(words[1], words[2] if len(words) > 2 else os.path.basename(words[1]))
    elif cmd in ("mv", "rm", "mkdir"):
        need = 3 if cmd == "mv" else 2
        if len(words) < need:
            sys.exit("%s needs %d path%s" % (cmd, need - 1, "s" if need > 2 else ""))
        board.ser.write((" ".join([cmd] + [enc(w) for w in words[1:need]]) + "\n").encode())
        board.wait_for("OK " + cmd, 20, fail="ERR")
        print("ok:", " ".join(words[:need]))
    elif cmd == "heap":
        board.ser.write((" ".join(words) + "\n").encode())
        end = time.time() + 10
        while time.time() < end:
            line = board.readline(5)
            if line is None:
                break
            if line.startswith("OK heap"):
                break
            if line.startswith("HEAP") or len(words) > 1:
                print(line)
    elif cmd == "bench":
        board.ser.write((" ".join(words) + "\n").encode())
        end = time.time() + 60
        while time.time() < end:
            line = board.readline(30)
            if line is None or line.startswith("OK bench"):
                break
            if line.startswith("BENCH"):
                print(line)
    elif cmd == "genbench":
        board.ser.write((" ".join(words) + "\n").encode())
        while True:                                 # minutes for "all": no overall limit
            line = board.readline(120)              # (one puzzle takes seconds at most)
            if line is None:
                sys.exit("timed out waiting for genbench (is the board on %s?)" % board.ser.port)
            if line.startswith("OK genbench"):
                break
            if line.startswith("ERR"):
                sys.exit(line)
            if line.startswith("GENBENCH"):
                print(line, flush=True)
    elif cmd in ("screen", "expect"):
        # expect NAME ...: stop the run unless the board is on one of those screens (devctl "screen":
        # home1, home2, list, game, stats, settings, sysmon, players, trophies, ... "+dialog" when a
        # dialog is open), so a missed tap or swipe never sends the taps after it somewhere else
        board.ser.write(b"screen\n")
        line = board.wait_for("OK screen", 10, fail="ERR")
        where = line.split()[2] if len(line.split()) > 2 else "?"
        if cmd == "screen":
            print(where)
        elif where not in words[1:]:
            sys.exit("expected %s, but the board is on %s: stopping before any more taps" % (" or ".join(words[1:]), where))
    elif cmd in ("clock", "waitclock"):
        # clock: Wi-Fi and the time; waitclock [S]: wait (default 60 s) until the time is set, else stop
        # the run (the daily puzzle and Trophies' Daily / Weekly tabs need it)
        limit = time.time() + (float(words[1]) if cmd == "waitclock" and len(words) > 1 else 60)
        while True:
            board.ser.write(b"clock\n")
            line = board.wait_for("OK clock", 10, fail="ERR")
            info = line.split(" ", 2)[2] if len(line.split(" ", 2)) > 2 else ""
            if cmd == "clock":
                print(info)
                break
            if "time=set" in info:
                break
            if time.time() > limit:
                sys.exit("the clock isn't set (%s): stopping" % info)
            time.sleep(1)
    elif cmd == "state":
        board.ser.write(b"state\n")
        end = time.time() + 10
        while time.time() < end:
            line = board.readline(5)
            if line is None:
                break
            if line.startswith("ERR"):
                sys.exit(line)
            if line.startswith("OK state"):
                break
            if line.startswith(("STATE", "BOARD")):
                print(line)
    elif cmd == "fill":
        board.ser.write((" ".join(words) + "\n").encode())
        line = board.wait_for("OK fill", 30, fail="ERR")
        print("filled %s cell%s" % (line.split()[2], "" if line.split()[2] == "1" else "s"))
    elif cmd == "ping":                             # (it answers "OK pong")
        board.command("ping", expect="OK pong")
    elif cmd in ("tap", "long", "swipe", "press", "move", "release", "wait", "screenoff"):
        timeout = 20 + (int(words[1]) / 1000 if cmd == "wait" and len(words) > 1 else 0)
        board.command(" ".join(words), timeout=timeout)
    else:
        sys.exit("unknown command: %s" % " ".join(words))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=921600, help="speed for the transfers (default 921600)")
    ap.add_argument("--script", help="a file of commands, one per line")
    ap.add_argument("--no-wait", action="store_true", help="don't wait for the board to boot")
    ap.add_argument("--sql", action="append", help="SQL to run on sudoku.db (several: repeat --sql)")
    ap.add_argument("--sqlite", action="store_true", help="a SQL console on sudoku.db")
    ap.add_argument("--mode", default="box", choices=("box", "list", "csv", "tabs"), help="how SQL results print")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("commands", nargs="*", help="commands; several are separated by ';'")
    args = ap.parse_args()

    board = Board(args.port, args.baud, not args.no_wait, args.verbose)
    lines = []
    if args.script:
        with open(args.script) as f:
            lines += [l.split("#", 1)[0].strip() for l in f]
    if args.commands:
        lines += [part.strip() for part in " ".join(args.commands).split(";")]
    for line in lines:
        if line:
            run(board, shlex.split(line))
    ok = True
    for text in args.sql or []:
        ok = run_sql(board, text, args.mode) and ok
    if args.sqlite:
        sqlite_console(board, args.mode)
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()

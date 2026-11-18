#!/usr/bin/env python3
"""
Drive the board over its USB-UART port: screenshots (PNG) and touches, for
documentation and testing. The firmware side is src/devctl.cpp.

    python3 tools/devctl.py [--port /dev/ttyACM0] [--baud 921600] COMMAND...
    python3 tools/devctl.py --script docs/shots.txt

Commands (on the command line, or one per line in a script; # starts a comment):
    tap X Y                 a short tap
    long X Y [MS]           a long press (default 1000 ms)
    swipe X1 Y1 X2 Y2 [MS]  a drag (default 300 ms)
    press X Y | move X Y | release [X Y]
    wait MS                 let the UI run
    shot FILE.png [--blur X1,Y1,X2,Y2 ...]
                            save the screen as a PNG; --blur pixelates those
                            rectangles first (e.g. a network name)
    ping

Opening the port resets the board (its auto-reset circuit), so a run starts
from a freshly booted Home screen: the tool waits for "DEVCTL ready" first
(--no-wait skips that, for a board that didn't reset). Then it switches to
--baud for the transfers. Needs pyserial (pip install pyserial).

Coordinates are screen pixels, 0..719, from the top left.
"""
import argparse
import base64
import os
import shlex
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

    def wait_for(self, text, timeout):
        end = time.time() + timeout
        while time.time() < end:
            line = self.readline(end - time.time())
            if line is None:
                break
            if self.verbose:
                print("  <", line)
            if text in line:
                return line
        sys.exit("timed out waiting for %r (is the board on %s?)" % (text, self.ser.port))

    def command(self, cmd, expect=None, timeout=20):
        if self.verbose:
            print("  >", cmd)
        self.ser.write((cmd + "\n").encode())
        word = cmd.split()[0]
        return self.wait_for(expect or ("OK " + word), timeout)

    def shot(self, path, blur=(), tries=3):
        for attempt in range(tries):
            self.ser.write(b"shot\n")
            self.wait_for("SHOT BEGIN", 10)
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
                data = b"".join(chunks[i] for i in sorted(chunks))
                if len(data) == int(size) and zlib.crc32(data) == int(crc, 16):
                    rgb = decode_rle(data, 720, 720)
                    for box in blur:
                        rgb = pixelate(rgb, 720, 720, box)
                    write_png(path, rgb, 720, 720)
                    print("saved", path)
                    return
            print("screenshot garbled, retrying (%d)" % (attempt + 1))
        sys.exit("couldn't get a clean screenshot")


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
    elif cmd in ("tap", "long", "swipe", "press", "move", "release", "wait", "ping"):
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


if __name__ == "__main__":
    main()

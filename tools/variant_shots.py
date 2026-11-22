#!/usr/bin/env python3
"""
The screenshots of the variant pages (docs/variants/): each variant's example puzzle opened
on the board, from Puzzles > Imported > variant-examples, saved as
docs/screenshots/variants/<page>.png.

    python3 tools/variant_shots.py --port /dev/ttyACM1              all 20
    python3 tools/variant_shots.py --port /dev/ttyACM1 kropki 18    just these (page names or numbers)

First, once: import docs/variant-examples.sdx on the board (copy it to puzzles/ on the card,
e.g. devctl "put docs/variant-examples.sdx puzzles/variant-examples.sdx", then Files > puzzles >
variant-examples.sdx > Import). Imported must open on that collection: it does when it's the
first one there, or the one shown last. Nothing is played: a puzzle only opened isn't saved
as a game. A rules card that pops up (the first time a variant is opened) is closed with OK.

Every screenshot is a fresh devctl session (opening the port restarts the board), so a
list that scrolled differently than planned is found out (the puzzle's number, from devctl
"state") and the next session taps where it should have.
"""
import argparse
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import devctl  # noqa: E402  (tools/devctl.py: the serial connection, commands and screenshots)

# the pages, in the order of docs/variant-examples.sdx (variant-examples #1 .. #20)
PAGES = ["classic", "x-sudoku", "mini-6x6", "hyper", "jigsaw", "killer", "anti-knight", "even-odd",
         "consecutive", "thermo", "sandwich", "kids-4x4", "anti-king", "non-consecutive", "disjoint-groups",
         "asterisk", "kropki", "xv", "greater-than", "quadruple", "arrow", "renban", "german-whispers",
         "skyscraper", "little-killer", "palindrome", "region-sum-lines", "x-sums",
         "zipper-lines", "frame", "between-lines", "entropic-lines", "modular-lines", "center-dot",
         "outside-sudoku", "rossini", "diagonal-non-consecutive", "dutch-whispers", "parity-lines", "slow-thermo"]

ROW0, ROW_H = 168, 74          # the collection's list: the first row's centre, the row pitch (pixels)
FIT = 6                        # rows that fit before the list has to scroll


def read_png(path):
    """width, height and the RGB rows of a PNG as devctl writes it (8-bit RGB, no filters)"""
    data = open(path, "rb").read()
    w, h = struct.unpack(">II", data[16:24])
    idat, pos = b"", 8
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        if kind == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return w, h, [raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h)]


def ok_button_y(path):
    """the y of a dialog's blue OK button down the middle of the screen (x = 360), or None"""
    w, h, rows = read_png(path)
    x = w // 2 * 3
    ys = [y for y in range(h) if rows[y][x + 2] > 200 and rows[y][x] < 90 and rows[y][x + 1] < 150]
    return (ys[0] + ys[-1]) // 2 if ys else None


def screen(board):
    board.ser.write(b"screen\n")
    return board.wait_for("OK screen", 10).split()[2]


def puzzle_name(board):
    """the name of the game on screen (devctl "state"), or None"""
    board.ser.write(b"state\n")
    name = None
    while True:
        line = board.readline(10)
        if line is None:
            return None
        if line.startswith("STATE"):
            m = re.search(r'name="([^"]*)"', line)
            name = m.group(1) if m else None
        if line.startswith("OK state"):
            return name
        if line.startswith("ERR"):
            return None


def open_example(args, k, extra):
    """a fresh session on the game of variant-examples #k; (board, name, number opened, tap's dy)"""
    b = devctl.Board(args.port, args.baud, True, args.verbose)
    b.command("wait 1000")                                      # (the first screen settles)
    b.command("tap 206 355"); b.command("wait 900")             # Home > Puzzles
    b.command("tap 148 96"); b.command("wait 900")              # the Imported tab: the collection shown
    scroll = max(0, k - FIT) * ROW_H
    left = scroll
    while left > 0:                                             # slow swipes, up to 5 rows each
        step = min(left, 5 * ROW_H)
        b.command("swipe 480 650 480 %d 1500" % (650 - step)); b.command("wait 800")
        left -= step
    y = ROW0 + (k - 1) * ROW_H - scroll + extra
    for dy in (0, -ROW_H // 2, ROW_H // 2):                     # (the gap between two rows: half a row off)
        b.command("tap 480 %d" % (y + dy)); b.command("wait 1500")
        name = puzzle_name(b)
        m = re.search(r"#(\d+)$", name or "")
        if m:
            if not name.startswith(args.collection):
                sys.exit("opened %r, not from %s: make it the collection Saved opens on" % (name, args.collection))
            return b, name, int(m.group(1)), dy
    sys.exit("no puzzle opened at y %d (screen: %s)" % (y, screen(b)))


def shoot(args, k, out):
    extra = 0                                                   # learnt: how far off the scroll was
    for _ in range(3):
        b, name, got, dy = open_example(args, k, extra)
        if got == k:
            break
        extra += (k - got) * ROW_H + dy                         # the same scroll next session: tap there
        b.ser.close()
    else:
        sys.exit("couldn't open %s #%d" % (args.collection, k))
    if screen(b).endswith("+dialog"):                           # the variant's rules, the first time
        tmp = out + ".dialog.png"
        b.shot(tmp)
        oy = ok_button_y(tmp)
        if oy is None:
            sys.exit("a dialog without an OK button over the game: see " + tmp)
        os.remove(tmp)
        b.command("tap 360 %d" % oy); b.command("wait 700")
    if screen(b) != "game":
        sys.exit("not on the game screen: " + screen(b))
    b.shot(out)
    print("%s #%d -> %s" % (args.collection, k, out), flush=True)
    b.command("tap 60 30"); b.command("wait 600")               # back to the list (nothing to save)
    b.ser.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--out", default=os.path.join("docs", "screenshots", "variants"), help="folder for the PNGs")
    ap.add_argument("--collection", default="variant-examples", help="the imported collection's name")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("variants", nargs="*", help="page names (kropki) or numbers 1-%d; none: all" % len(PAGES))
    args = ap.parse_args()
    todo = []
    for v in args.variants or [str(i + 1) for i in range(len(PAGES))]:
        k = int(v) if v.isdigit() else (PAGES.index(v) + 1 if v in PAGES else 0)
        if not 1 <= k <= len(PAGES):
            sys.exit("unknown variant: %s (one of: %s, or 1-%d)" % (v, ", ".join(PAGES), len(PAGES)))
        todo.append(k)
    os.makedirs(args.out, exist_ok=True)
    for k in todo:
        shoot(args, k, os.path.join(args.out, PAGES[k - 1] + ".png"))


if __name__ == "__main__":
    main()

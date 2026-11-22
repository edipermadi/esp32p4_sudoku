#!/usr/bin/env python3
"""
The screenshots of the variant pages (docs/variants/): each variant's example puzzle opened
on the board, from Puzzles > Imported > variant-examples, saved as
docs/screenshots/variants/<page>.png.

    python3 tools/variant_shots.py --port /dev/ttyACM1              all of them
    python3 tools/variant_shots.py --port /dev/ttyACM1 kropki 18    just these (page names or numbers)

First, once: import docs/variant-examples.sdx on the board (devctl "import docs/variant-examples.sdx").
Each puzzle is brought up with devctl "open imported <id> <n>" (the collection's id from "colls"),
the way a tap on it in the list would, all in one session. Nothing is played: a puzzle only opened isn't saved as a
game. A rules card that pops up (the first time a variant is opened) is closed with OK.
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import devctl  # noqa: E402  (tools/devctl.py: the serial connection, commands and screenshots)

# the pages, in the order of docs/variant-examples.sdx (variant-examples #1, #2, ...)
PAGES = ["classic", "x-sudoku", "mini-6x6", "hyper", "jigsaw", "killer", "anti-knight", "even-odd",
         "consecutive", "thermo", "sandwich", "kids-4x4", "anti-king", "non-consecutive", "disjoint-groups",
         "asterisk", "kropki", "xv", "greater-than", "quadruple", "arrow", "renban", "german-whispers",
         "skyscraper", "little-killer", "palindrome", "region-sum-lines", "x-sums",
         "zipper-lines", "frame", "between-lines", "entropic-lines", "modular-lines", "center-dot",
         "outside-sudoku", "rossini", "diagonal-non-consecutive", "dutch-whispers", "parity-lines", "slow-thermo",
         "fortress", "argyle"]


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


def collection_id(b, name):
    """the id of the imported collection called name (devctl "colls")"""
    b.ser.write(b"colls\n")
    found = None
    while True:
        line = b.readline(10)
        if line is None or line.startswith("ERR"):
            sys.exit("colls: %s" % line)
        if line.startswith("COLL imported "):
            _, _, cid, _, coded = line.split(" ", 4)
            found = int(cid) if devctl.dec(coded) == name else found
        if line.startswith("OK colls"):
            if found is None:
                sys.exit("no imported collection called %s: import docs/variant-examples.sdx first" % name)
            return found


def shoot(args, b, cid, k, out):
    """puzzle k of the collection brought up on the game screen (devctl "open"), its rules card closed, saved"""
    b.ser.write(("open imported %d %d\n" % (cid, k)).encode())
    name = b.wait_for("OK open", 30, fail="ERR").split(" ", 2)[2]
    if not name.endswith("#%d" % k):
        sys.exit("opened %r, not %s #%d" % (name, args.collection, k))
    b.command("wait 1200")
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
    b = devctl.Board(args.port, args.baud, True, args.verbose)  # (one session: opening the port restarts the board)
    b.command("wait 1500")                                      # (the first screen settles)
    cid = collection_id(b, args.collection)
    for k in todo:
        shoot(args, b, cid, k, os.path.join(args.out, PAGES[k - 1] + ".png"))
    b.ser.close()


if __name__ == "__main__":
    main()

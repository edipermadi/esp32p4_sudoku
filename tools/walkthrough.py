#!/usr/bin/env python3
"""
A variant page's full walkthrough: its example puzzle (docs/variant-examples.sdx) solved to the
end one single at a time, a picture per step drawn with PIL (no board needed). Each step gets a page of
its own, docs/variants/<page>/step-NN.md, with links to the step before and after and back to the
variant page; the variant page gets the start picture and a list of the steps, linking those pages,
between the markers

    <!-- walkthrough -->
    <!-- /walkthrough -->

(added as a section "The whole puzzle, step by step" at the end of the page when it has none).

    python3 tools/walkthrough.py classic        one page: docs/variants/classic.md, its steps in
                                                docs/variants/classic/, its pictures in
                                                docs/screenshots/variants/classic-walkthrough/
    python3 tools/walkthrough.py --all          every variant page

The steps come from tests/variant_examples --json (tools/variant_examples.c, "make -C tests
examples" builds it): a box's (or region's) hidden single whenever there is one, else one in a
row, column or the variant's own units, else a naked single, each with the reason every other cell
or digit is ruled out, down to the variant's rule (a cage's sum, a missing dot, a thermometer...).

In a picture: the unit the step looks at is tinted yellow (for a naked single: the cell's row,
column and box), the new digit is on green, the digits that rule cells out have an orange ring,
and the empty cells they rule out a red cross. Givens are on grey, digits from earlier steps blue;
the variant's own marks are drawn as on the board.
"""
import argparse
import json
import os
import re
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = [
    os.path.expanduser("~/.cache/Espressif/ComponentManager/service_d92d8f1e/lvgl__lvgl_9.2.0_2409fdc8/scripts/"
                       "built_in_font/Montserrat-Medium.ttf"),   # the board's digits are Montserrat Medium
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]
# page -> the variant's name, as tests/variant_examples takes it
PAGES = {"classic": "Classic", "x-sudoku": "X-Sudoku", "mini-6x6": "Mini", "hyper": "Hyper", "jigsaw": "Jigsaw",
         "killer": "Killer", "anti-knight": "Anti-Knight", "even-odd": "Even-Odd", "consecutive": "Consecutive",
         "thermo": "Thermo", "sandwich": "Sandwich", "kids-4x4": "Kids", "anti-king": "Anti-King",
         "non-consecutive": "Non-Consecutive", "disjoint-groups": "Disjoint", "asterisk": "Asterisk",
         "kropki": "Kropki", "xv": "XV", "greater-than": "Greater", "quadruple": "Quadruple", "arrow": "Arrow",
         "renban": "Renban", "german-whispers": "German", "skyscraper": "Skyscraper", "little-killer": "Little",
         "palindrome": "Palindrome", "region-sum-lines": "Region", "x-sums": "X-Sums", "zipper-lines": "Zipper",
         "frame": "Frame", "between-lines": "Between", "entropic-lines": "Entropic", "modular-lines": "Modular",
         "center-dot": "Center", "outside-sudoku": "Outside", "rossini": "Rossini",
         "diagonal-non-consecutive": "Diagonal", "dutch-whispers": "Dutch", "parity-lines": "Parity",
         "slow-thermo": "Slow", "fortress": "Fortress", "argyle": "Argyle", "nabner": "Nabner",
         "numbered-rooms": "Numbered", "sum-skyscrapers": "Sum", "lockout-lines": "Lockout",
         "sumless-killer": "Sumless", "double-arrow": "Double", "diagonal-consecutive": "Diagonal C",
         "10-lines": "10", "girandola": "Girandola", "159-sudoku": "159", "product-killer": "Product",
         "extra-regions": "Extra", "anti-diagonal": "Anti-D", "pill-arrows": "Pill", "odd-even-skyscrapers": "Odd",
         "chess-sudoku": "Chess", "magic-square": "Magic", "average-lines": "Average",
         "dutch-flatmates": "Dutch F", "mathrax": "Mathrax", "point-symmetry": "Point", "before-nine": "Before",
         "quadro": "Quadro", "anti-queen-9s": "Anti-Q"}

CELL, THICK, THIN, PAD, BAND = 44, 3, 1, 6, 30
BG, LINE, LINE_THIN = (247, 245, 249), (52, 56, 64), (200, 204, 212)
CELL_BG, GIVEN_BG, GIVEN_FG = (255, 255, 255), (238, 241, 246), (27, 31, 36)
TINT_BG, EVEN_BG = (255, 228, 205), (218, 222, 230)       # X / Hyper / Asterisk cells; Even-Odd's even ones
FORT_BG, ARGYLE = (204, 209, 218), (149, 183, 240)         # Fortress' grey cells; Argyle's lines (the board's, on white)
ARGYLE_STARTS = [((0, 1), 1), ((0, 4), 1), ((1, 0), 1), ((4, 0), 1), ((0, 7), -1), ((0, 4), -1), ((1, 8), -1), ((4, 8), -1)]
PLACED_FG = (43, 111, 224)                                  # digits from earlier steps
UNIT_BG = (255, 244, 190)                                   # the unit the step looks at
NEW_BG, NEW_FG = (205, 242, 212), (22, 120, 50)             # the step's digit
RING, CROSS, ORANGE = (230, 126, 34), (214, 48, 49), (230, 126, 34)
MUTED, THERMO, SUM_FG = (120, 126, 138), (196, 200, 207), (43, 111, 224)
ARROW, RENBAN, WHISPER = (140, 146, 158), (222, 200, 245), (120, 205, 150)   # (the board's, on white)
PALINDROME, REGIONSUM, ZIPPER = (178, 181, 187), (119, 173, 241), (234, 149, 185)
ENTROPIC, MODULAR = (239, 178, 108), (98, 192, 192)
DUTCH, PARITY = (228, 190, 64), (230, 128, 128)
NABNER = (196, 160, 118)                                    # Nabner's lines (the board's brown, on white)
AVG = (46, 158, 143)                                        # Average Lines (the board's teal)
DARROW = (74, 127, 217)                                     # Double Arrow's lines and circles (the board's blue)
TEN = (159, 209, 166)                                       # 10 Lines' green band (the board's, on white)
GROUP_NAMES = {8: ("low (1-3)", "middle (4-6)", "high (7-9)"), 9: ("one of 3 6 9", "one of 1 4 7", "one of 2 5 8")}


def line_group(kind, v):
    """Entropic (8): 1-3, 4-6, 7-9; Modular (9): the remainder mod 3"""
    return (v - 1) // 3 if kind == 8 else v % 3
RIGHT, DOWN = 0, 1
WINDOWS = ["top-left", "top-right", "bottom-left", "bottom-right"]
SPOTS = ["top-left", "top", "top-right", "left", "centre", "right", "bottom-left", "bottom", "bottom-right"]


def rc(c):
    return "r%dc%d" % (c // 9 + 1, c % 9 + 1)


ORD = ("first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth")


def listing(items, word="and"):
    items = list(items)
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " " + word + " " + items[-1]


def a_n(v):
    return "an %d" % v if v == 8 else "a %d" % v


def needs(unit):
    """the verb for a unit's name: the box centres need, row 3 needs"""
    return "need" if unit.endswith("centres") else "needs"


def has(unit):
    """the box centres have, row 3 has"""
    return "have" if unit.endswith("centres") else "has"


def cap(t):
    return t[0].upper() + t[1:]


def unit_words(u):
    """the tool's unit name, as a page says it"""
    if u.startswith("window "):
        return "the %s window" % WINDOWS[int(u.split()[1]) - 1]
    if u.startswith("disjoint group "):
        return "the %s group" % SPOTS[int(u.split()[2]) - 1]
    return u


# ------------------------------------------------------------------ the reasons, in words

def rule_words(d, vals, c, v, r):
    """why v can't go in c by the variant's rule r (a reason from the tool), as a phrase"""
    rule = r["rule"]
    w = vals[r["with"]] if r.get("with", -1) >= 0 else 0
    at = rc(r["with"]) if r.get("with", -1) >= 0 else ""
    n = d["n"]
    if rule == "consec":
        near = [x for x in (w - 1, w + 1) if 1 <= x <= n]
        if r["mark"]:
            return "it has a bar to the %d in %s, so it's %s" % (w, at, listing(map(str, near), "or"))
        return "it has no bar to the %d in %s, so it isn't %s" % (w, at, listing(map(str, near), "or"))
    if rule == "kropki":
        near = [x for x in (w - 1, w + 1) if 1 <= x <= 9]
        dbl = [x for x in (w * 2, w // 2 if w % 2 == 0 else 0) if 1 <= x <= 9]
        if r["mark"] == 1:
            return "a white dot joins it to the %d in %s: it's %s" % (w, at, listing(map(str, near), "or"))
        if r["mark"] == 2:
            return "a black dot joins it to the %d in %s: it's %s" % (w, at, listing(map(str, dbl), "or"))
        return "there's no dot between it and the %d in %s: it isn't %s" % (
            w, at, listing(map(str, sorted(set(near + dbl))), "or"))
    if rule == "xv":
        if r["mark"] == 1:
            return "a V joins it to the %d in %s: it's %d" % (w, at, 5 - w)
        if r["mark"] == 2:
            return "an X joins it to the %d in %s: it's %d" % (w, at, 10 - w)
        bad = [x for x in (5 - w, 10 - w) if 1 <= x <= 9 and x != w]
        return "there's no mark between it and the %d in %s: it isn't %s" % (w, at, listing(map(str, bad), "or"))
    if rule == "fortress":                                  # Fortress: a grey cell beats its white neighbours
        if d["parity"][c] == 3:
            return "it's grey, so it's bigger than the white %d in %s next to it" % (w, at)
        return "it's white, so it's smaller than the grey %d in %s next to it" % (w, at)
    if rule == "gt":
        smaller = (r["mark"] == 1) == r["cell_first"]       # LESS: the left / top one is the smaller
        return "the sign says it's %s than the %d in %s" % ("smaller" if smaller else "bigger", w, at)
    if rule == "range" and (d.get("outside") or d.get("rossini")):
        r_, c_ = c // 9, c % 9
        for lu, at in ((r_, c_), (9 + c_, r_)):
            name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
            if d.get("outside") and at >= 3 and d["outside"][lu] >> v & 1:
                return "the clue of %s puts its %d among the first three cells, so not here" % (name, v)
            if d.get("rossini") and at < 3 and d["rossini"][lu]:
                up = d["rossini"][lu] == 1
                side = "the left" if lu < 9 else "the top"
                k = at if up else 2 - at
                return ("the first three digits of %s %s from %s, so its %s cell is %d to %d" % (
                    name, "rise" if up else "fall", side, ("first", "second", "third")[at], k + 1, k + 7))
    if rule == "range":
        if d.get("psym") and c == 40:                       # Point Symmetry: the centre is its own mirror
            return "it's the centre, its own mirror: only 5 + 5 makes 10"
        if d.get("b9") and v == 9:                          # Before Nine: a clue says something comes before the 9
            return "it's the first cell after a clue: a 9 first would make the clue 0"
        if d.get("sky"):                                    # Skyscraper: the clue keeps it below a height
            if r["lo"] == r["hi"]:
                return "a clue of its row or column makes it %s (see the rules above)" % a_n(r["lo"])
            return "it's near a clue of its row or column, which keeps it at most %d" % r["hi"]
        if d.get("frame"):                                  # Frame: what three different digits can make
            r_, c_ = c // 9, c % 9
            clues = [d["frame"][r_]] if c_ < 3 and d["frame"][r_] else []
            clues += [d["frame"][9 + c_]] if r_ < 3 and d["frame"][9 + c_] else []
            for k in clues:
                ok = sorted({a for a in range(1, 10) for b in range(1, 10) if a != b and 1 <= k - a - b <= 9
                             and k - a - b not in (a, b)})
                if v not in ok:
                    bad = [x for x in range(1, 10) if x not in ok]
                    what = ("anything but %s" % listing(map(str, bad), "or") if len(ok) > len(bad) else
                            "only %s" % listing(map(str, ok), "and"))
                    return ("it's among the first three digits after a clue of %d, and three different digits adding up "
                            "to %d can hold %s" % (k, k, what))
        if d.get("nroom"):                                  # Numbered Rooms: a first digit can't be 1, or the clue
            r_, c_ = c // 9, c % 9
            clues = [d["nroom"][r_]] if c_ == 0 and d["nroom"][r_] else []
            clues += [d["nroom"][9 + c_]] if r_ == 0 and d["nroom"][9 + c_] else []
            for k in clues:
                if v == 1 and k != 1:
                    return "it's the first cell after a clue of %d: a 1 there would make the clue itself 1" % k
                if v == k:
                    return ("it's the first cell after a clue of %d: a %d there would point at another cell to hold "
                            "the %d, while it's here" % (k, k, k))
        if d.get("skysum"):                                 # Sum Skyscrapers: the first building is seen
            r_, c_ = c // 9, c % 9
            clues = [d["skysum"][r_]] if c_ == 0 and d["skysum"][r_] else []
            clues += [d["skysum"][9 + c_]] if r_ == 0 and d["skysum"][9 + c_] else []
            for k in clues:
                if k == 9:
                    return "it's the first cell after a clue of 9: only the 9 is seen there, so it's first"
                if v == 9:
                    return "it's the first cell after a clue of %d: a 9 first would be all that's seen, 9" % k
                if v > k - 9:
                    return ("it's the first cell after a clue of %d: it's seen, and so is the 9 later, so it's at "
                            "most %d" % (k, k - 9))
        if d.get("xsum"):                                   # X-Sums: a clue allows only some first digits
            if r["lo"] == r["hi"]:
                return "it's the first cell after a clue, and that clue can only start with %s" % a_n(r["lo"])
            return "it's the first cell after a clue, and that clue can only start with %d to %d" % (r["lo"], r["hi"])
        if d.get("edge_kind") == 4:                         # Fortress: a grey cell is never 1, a white one by it never 9
            if d["parity"][c] == 3:
                return "it's grey, bigger than a white cell next to it, so it isn't 1"
            return "it's next to a grey cell, which is bigger, so it isn't 9"
        if d.get("line_kind") == 3 and v == 5:              # German Whispers: no 5 on a line
            return "it's on a green line, and nothing is 5 or more away from a 5"
        if d.get("line_kind") == 17:                        # Pill Arrows: the pill's tens
            return "it's a pill's first digit, its tens: a shaft of up to 7 cells adds up to 63 at most"
        if d.get("line_kind") == 14 and v == 5:             # Lockout: never a 5 between the diamonds
            return "it's between the diamonds of a lockout line, and 5 is inside any two digits 4 or more apart"
        if d["thermos"]:
            return "its place on its thermometer keeps it between %d and %d" % (r["lo"], r["hi"])
        return "the chains of signs in its box keep it between %d and %d" % (r["lo"], r["hi"])
    if rule == "thermo":                                    # (Slow Thermo, where the two don't see each other: or the same)
        same = r.get("same_ok")
        if r["after"]:
            return "it comes after the %d in %s on its thermometer, so it's %s" % (
                w, at, "%d or more" % w if same else "above %d" % w)
        return "it comes before the %d in %s on its thermometer, so it's %s" % (
            w, at, "%d or less" % w if same else "below %d" % w)
    if rule == "cage" and d["cages"][r["cage"]].get("prod"):   # Product Killer: the digits multiply to it
        cage = d["cages"][r["cage"]]
        p = cage["prod"]
        filled = [vals[x] for x in cage["cells"] if vals[x]]
        empty = [x for x in cage["cells"] if not vals[x] and x != c]
        has = " (with %s already in it)" % listing(map(str, filled)) if filled else ""
        left = p
        for x in filled:
            left //= x
        if v in filled:
            return "the cage of %d%s has a %d already" % (p, has, v)
        if left % v:
            return "the cage of %d%s leaves %d to make, and %d doesn't go into it" % (p, has, left, v) if filled \
                else "the cage's digits multiply to %d, and %d doesn't go into it" % (p, v)
        if not empty:
            return "the cage of %d%s needs %d there" % (p, has, left)
        other = "cell" if len(empty) == 1 else "%d cells" % len(empty)
        return "in the cage of %d%s, a %d would leave %d for its other %s, which different digits can't make" % (
            p, has, v, left // v, other)
    if rule == "159":                                       # 159 Sudoku: a row's columns 1, 5, 9
        row, col = c // 9, c % 9
        line = [vals[row * 9 + j] for j in range(9)]
        if col % 4 == 0:
            k = col + 1
            if k in line and line.index(k) != v - 1:
                return "it's column %d of its row, the column of the row's %d, and that %d is in column %d: it's %d" % (
                    col + 1, k, k, line.index(k) + 1, line.index(k) + 1)
            t = line[v - 1] if v - 1 != col else None
            if t:
                return "it's column %d of its row: a %d would put the row's %d in column %d, which holds %d" % (
                    col + 1, v, k, v, t)
        if v in (1, 5, 9) and line[v - 1] and v - 1 != col:
            return "column %d of its row says the row's %d is in column %d" % (v, v, line[v - 1])
        return "its row's columns 1, 5 and 9 leave no room for %s here" % a_n(v)
    if rule == "cage":
        cage = d["cages"][r["cage"]]
        filled = [vals[x] for x in cage["cells"] if vals[x]]
        empty = [x for x in cage["cells"] if not vals[x] and x != c]
        left = cage["sum"] - sum(filled) - v
        has = " (with %s already in it)" % listing(map(str, filled)) if filled else ""
        if v in filled:
            return "the cage of %d%s has a %d already" % (cage["sum"], has, v)
        if not empty:
            return "the cage of %d%s needs %d there" % (cage["sum"], has, cage["sum"] - sum(filled))
        free = sorted(x for x in range(1, 10) if x not in filled and x != v)
        k = len(empty)
        lo, hi = sum(free[:k]), sum(free[-k:])
        other = "cell" if k == 1 else "%d cells" % k
        if left < lo:
            return "in the cage of %d%s, a %d would leave %d for its other %s, too little" % (
                cage["sum"], has, v, left, other)
        if left > hi:
            return "in the cage of %d%s, a %d would leave %d for its other %s, too much" % (
                cage["sum"], has, v, left, other)
        return "the cage of %d%s can't be made up with a %d there" % (cage["sum"], has, v)
    if rule == "quad":
        q = d["quads"][r["quad"]]
        cells = [q["at"], q["at"] + 1, q["at"] + 9, q["at"] + 10]
        need = list(q["digits"])
        for x in cells:
            if vals[x] in need:
                need.remove(vals[x])
        empty = sum(1 for x in cells if not vals[x])
        circle = "".join(map(str, q["digits"]))
        if empty == 1:
            return "the circle %s still needs %s in its one empty cell" % (circle, listing(map(str, need)))
        return "the circle %s still needs %s in its %d empty cells, so they take nothing else" % (
            circle, listing(map(str, need)), empty)
    if rule == "line":
        cells = d["thermos"][r["line"]]
        kind = d.get("line_kind", 0)
        if kind == 3:                                       # German Whispers
            return "it's next to the %d in %s on a green line, and %d is less than 5 away from it" % (w, at, v)
        if kind == 10:                                      # Dutch Whispers
            return "it's next to the %d in %s on an amber line, and %d is less than 4 away from it" % (w, at, v)
        if kind == 11:                                      # Parity Lines
            return "it's next to the %d in %s on a red line, and %d is %s too" % (w, at, v, "odd" if v % 2 else "even")
        if kind == 13:                                      # Nabner: any digit on the line, not just a neighbour
            if w == v:
                return "the %d in %s is on its brown line already, and no digit repeats on it" % (w, at)
            return "the %d in %s is on its brown line too, and %d is 1 away from it" % (w, at, v)
        if kind == 14:                                      # Lockout: diamonds 4+ apart, the cells between outside them
            ends = (cells[0], cells[-1])
            a, b = vals[ends[0]], vals[ends[1]]
            if c in ends:
                other = vals[ends[1] if c == ends[0] else ends[0]]
                if other and abs(v - other) < 4:
                    return "it's a diamond, and %s is less than 4 away from the %d in the other diamond" % (a_n(v), other)
                inner = [vals[x] for x in cells[1:-1] if vals[x]]
                if other and inner:
                    return ("it's a diamond, and with %s and the %d in the other, %s between them wouldn't be outside "
                            "their range" % (a_n(v), other, listing(map(str, inner))))
                return "it's a diamond, and with %s there its line's cells couldn't all lie outside the diamonds' range" % (
                    a_n(v))
            if v == 5:
                return "it's between the diamonds of a lockout line, and 5 is inside any two digits 4 or more apart"
            if a and b:
                lo, hi = min(a, b), max(a, b)
                side = ("above %d" % hi if lo == 1 else "below %d" % lo if hi == 9
                        else "below %d or above %d" % (lo, hi))
                return "it's between the diamonds %d and %d, so it's %s" % (lo, hi, side)
            return "it's between the diamonds of its line, and %s couldn't lie outside their range" % a_n(v)
        if kind == 16:                                      # 10 Lines: runs of 10
            i = cells.index(c)
            left = [vals[x] for x in cells[:i]]
            if all(left):
                run = 0
                for x in left:
                    run = 0 if run + x == 10 else run + x
                if run + v > 10:
                    return ("on its green line the run before it adds up to %d, and %s would take it past 10" %
                            (run, a_n(v)) if run else "with %s there, the rest of its green line couldn't split "
                            "into runs of 10" % a_n(v))
            return "with %s there, its green line couldn't split into runs that each add up to 10" % a_n(v)
        if kind == 18:                                      # Average Lines: the circle is the others' average
            i, k = cells.index(c), len(cells) - 1
            circle = vals[cells[0]]
            others = [vals[x] for x in cells[1:] if x != c]
            gaps = sum(1 for x in cells[1:] if x != c and not vals[x])
            if i == 0:
                if not gaps:
                    return ("it's the circle: the line's %d other digits add up to %d, so their average is %s" % (
                        k, sum(others), "%d" % (sum(others) // k) if sum(others) % k == 0 else "no whole digit"))
                return "it's the circle, and %d times %d is out of reach of the line's other %d cells" % (k, v, k)
            if circle and not gaps:
                return "the circle %d times its %d other cells is %d, and the rest make %d, so it's %d" % (
                    circle, k, circle * k, sum(others), circle * k - sum(others))
            if circle:
                return "the circle %d times its %d other cells is %d, and %s here wouldn't leave room for the rest" % (
                    circle, k, circle * k, a_n(v))
            return "with %s here, the circle couldn't be the line's average" % a_n(v)
        if kind == 17:                                      # Pill Arrows: the pill's number is the shaft's sum
            i = cells.index(c)
            tens, units = vals[cells[0]], vals[cells[1]]
            shaft = [vals[x] for x in cells[2:] if x != c]
            gaps = sum(1 for x in cells[2:] if x != c and not vals[x])
            known = sum(shaft)
            if i == 0:
                return ("it's the pill's first digit, its tens: with %s there the pill would be %d or more, more than "
                        "its shaft of %d cells can make" % (a_n(v), 10 * v + 1, len(cells) - 2)) if 10 * v > 9 * (
                    len(cells) - 2) else "it's the pill's first digit, its tens, and with %s there the shaft can't " \
                    "make the pill's number" % a_n(v)
            if i == 1:
                if tens and not gaps:
                    return "it's the pill's second digit: the shaft adds up to %d, so the pill is %d" % (known, known)
                return "it's the pill's second digit, and with %s there the shaft can't make the pill's number" % a_n(v)
            if tens and units and not gaps:
                return "the pill reads %d and the rest of the shaft makes %d, so it's %d" % (
                    10 * tens + units, known, 10 * tens + units - known)
            if tens and units:
                return "the pill reads %d, and %s on its shaft wouldn't leave room for the rest" % (
                    10 * tens + units, a_n(v))
            return "with %s on its shaft, the pill couldn't read as the shaft's sum" % a_n(v)
        if kind == 15:                                      # Double Arrow: the two circles add up to the cells between
            ends = (cells[0], cells[-1])
            inner = [x for x in cells[1:-1] if x != c]
            known = sum(vals[x] for x in inner)
            gaps = sum(1 for x in inner if not vals[x])
            if c in ends:
                other = vals[ends[1] if c == ends[0] else ends[0]]
                if other and not gaps:
                    return "it's a circle: the cells between add up to %d and the other circle is %d, so it's %d" % (
                        known, other, known - other)
                return "it's a circle, and with %s there the two circles couldn't add up to the cells between them" % (
                    a_n(v))
            a, b = vals[ends[0]], vals[ends[1]]
            if a and b and not gaps:
                return "the circles %d and %d add up to %d, and the other cells between make %d, so it's %d" % (
                    a, b, a + b, known, a + b - known)
            if a and b:
                return "the circles %d and %d add up to %d, and %s between them wouldn't leave room for the rest" % (
                    a, b, a + b, a_n(v))
            return "with %s there, the cells between the circles couldn't add up to the two circles" % a_n(v)
        if kind == 4:                                       # Palindrome: the cell mirroring it
            return "it mirrors the %d in %s on its grey line, which reads the same from both ends" % (w, at)
        if kind == 6:                                       # Zipper: mirrored cells add up to the line's total
            n = len(cells)
            i = cells.index(c)
            m = cells[n - 1 - i]
            tot = None
            if n % 2 and vals[cells[n // 2]]:
                tot = vals[cells[n // 2]]
            for k in range(n // 2):
                a, b = cells[k], cells[n - 1 - k]
                if c not in (a, b) and vals[a] and vals[b]:
                    tot = vals[a] + vals[b]
            if n % 2 and c == cells[n // 2]:
                return "it's the middle of its pink line, whose pairs add up to %s" % (tot if tot else "something else")
            if tot is not None and vals[m]:
                return "it pairs with the %d in %s on its pink line, whose pairs add up to %d: it's %d" % (
                    vals[m], rc(m), tot, tot - vals[m])
            if tot is not None:
                return "its pink line's pairs add up to %d, and %s would need a %d with it" % (tot, a_n(v), tot - v)
            return "with %s there, its pink line's pairs couldn't all add up to the same" % a_n(v)
        if kind in (8, 9):                                  # Entropic, Modular: groups repeat every 3 cells
            colour, names = ("orange" if kind == 8 else "teal"), GROUP_NAMES[kind]
            i = cells.index(c)
            same = [x for k, x in enumerate(cells) if k != i and (k - i) % 3 == 0 and vals[x]]
            if same:
                x = same[0]
                return "it's %d cells from the %d in %s along its %s line, so it's %s like it" % (
                    abs(cells.index(x) - i), vals[x], rc(x), colour, names[line_group(kind, vals[x])])
            near = sorted({line_group(kind, vals[x]) for k, x in enumerate(cells)
                           if (k - i) % 3 != 0 and vals[x] and abs(k - i) < 3})
            if near:
                have = listing([("a %s" if kind == 8 else "%s") % names[g] for g in near], "and")
                return "next to it on its %s line there's %s%s already, and three in a row need one of each" % (
                    colour, have, " digit" if kind == 8 else "")
            return "with %s there, its %s line couldn't hold one of each group in every three in a row" % (
                a_n(v), colour)
        if kind == 7:                                       # Between Lines: strictly between the circles
            ends = (cells[0], cells[-1])
            if c in ends:
                other = ends[1] if c == ends[0] else ends[0]
                inner = [vals[x] for x in cells[1:-1] if vals[x]]
                if vals[other] and inner:
                    return ("it's a circle, and its line already has %s between it and the %d in %s: %s there "
                            "wouldn't leave them strictly between" % (listing(map(str, inner)), vals[other],
                                                                      rc(other), a_n(v)))
                return "it's a circle, and with %s there its line's cells couldn't all be between the circles" % a_n(v)
            a, b = vals[ends[0]], vals[ends[1]]
            if a and b:
                lo, hi = min(a, b), max(a, b)
                return "it's on the line between the circles %d and %d, so it's %d to %d" % (lo, hi, lo + 1, hi - 1)
            known = a or b
            if known:
                return ("it's on a line from the circle %d in %s: %s couldn't be strictly between that and the other "
                        "circle" % (known, rc(ends[0] if a else ends[1]), a_n(v)))
            return "it's on a line between two circles, and %s couldn't be strictly between them" % a_n(v)
        if kind == 5:                                       # Region Sum Lines: each box's part, the same sum
            parts = {}
            for x in cells:
                parts.setdefault(d["region"][x], []).append(x)
            mine = d["region"][c]
            own = [vals[x] for x in parts[mine] if vals[x] and x != c] + [v]
            own_empty = sum(1 for x in parts[mine] if not vals[x] and x != c)
            full = [sum(vals[x] for x in p) for b, p in parts.items() if b != mine and all(vals[x] for x in p)]
            if full:
                t = full[0]
                if not own_empty and sum(own) != t:
                    return ("its blue line adds up to %d in another box, and %s would make its part in this box %d" %
                            (t, a_n(v), sum(own)))
                if sum(own) + own_empty > t:
                    return ("its blue line adds up to %d in another box, and %s would take its part in this box past "
                            "that" % (t, a_n(v)))
            return "with %s there, its blue line couldn't add up to the same in each box it passes through" % a_n(v)
        if kind == 2:                                       # Renban
            have = sorted(vals[x] for x in cells if vals[x] and x != c)
            if v in have:
                return "its purple line already has a %d" % v
            return ("its purple line of %d cells already has %s: with %s the digits couldn't be a run of %d" %
                    (len(cells), listing(map(str, have)), a_n(v), len(cells)))
        circle, shaft = cells[0], cells[1:]                 # Arrow
        if c == circle:
            filled = [vals[x] for x in shaft if vals[x]]
            empty = sum(1 for x in shaft if not vals[x])
            lo, hi = sum(filled) + empty, sum(filled) + 9 * empty
            if not empty:
                return "it's an arrow's circle, and its shaft adds up to %d" % sum(filled)
            if v < lo:
                return "it's an arrow's circle, and its shaft makes at least %d" % lo
            return "it's an arrow's circle, and its shaft makes at most %d" % hi
        others = [x for x in shaft if x != c]
        filled = sum(vals[x] for x in others if vals[x])
        empty = sum(1 for x in others if not vals[x])
        if vals[circle]:
            return ("it's on the shaft of the arrow whose circle is %d (in %s): %s would take the shaft to %s" %
                    (vals[circle], rc(circle), a_n(v), "%d" % (filled + v) if not empty else
                     "at least %d" % (filled + v + empty)))
        return "it's on an arrow's shaft, which adds up to one digit: %s would take it past 9" % a_n(v)
    if rule == "sky":
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        clue = d["sky"][lu]
        line = [lu * 9 + k for k in range(9)] if lu < 9 else [k * 9 + lu - 9 for k in range(9)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        seen, top, k = 0, 0, 0
        while k < 9 and x[k]:
            if x[k] > top:
                top, seen = x[k], seen + 1
            k += 1
        side = "the left" if lu < 9 else "the top"
        shown = [x[j] for j in range(k) if x[j] > max(x[:j] or [0])]
        if top != 9 and k < 9 and seen >= clue:
            return ("seen from %s of %s, %s there would leave %s seen and the 9 still to come, "
                    "more than its clue %d" % (side, name, a_n(v), listing(map(str, shown)), clue))
        if seen > clue:
            return "seen from %s of %s, %d buildings would already show with %s there, more than its clue %d" % (
                side, name, seen, a_n(v), clue)
        if top == 9 or k == 9:
            return "seen from %s of %s, exactly %d buildings would show with %s there, not its clue %d" % (
                side, name, seen, a_n(v), clue)
        return "seen from %s of %s, %s there would leave too few buildings to make its clue %d" % (
            side, name, a_n(v), clue)
    if rule == "frame":
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        clue = d["frame"][lu]
        line = [lu * 9 + k for k in range(3)] if lu < 9 else [k * 9 + lu - 9 for k in range(3)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        got, empty = sum(x), x.count(0)
        if not empty:
            return "the first three digits of %s add up to its clue %d, and with %s there they'd make %d" % (
                name, clue, a_n(v), got)
        need = clue - got
        if empty == 1:
            why = ("more than 9" if need > 9 else "less than 1" if need < 1 else
                   "and %s is among them already" % a_n(need))
            return ("the first three digits of %s add up to its clue %d: with %s there, its other empty cell would "
                    "need %d, %s" % (name, clue, a_n(v), need, why))
        return ("the first three digits of %s add up to its clue %d: with %s there, its other two cells would need %d "
                "between them, which two different digits not there yet can't make" % (name, clue, a_n(v), need))
    if rule == "outside":
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        need = [x for x in range(1, 10) if d["outside"][lu] >> x & 1]
        line = [lu * 9 + k for k in range(3)] if lu < 9 else [k * 9 + lu - 9 for k in range(3)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        miss = [n for n in need if n not in x]
        empty = x.count(0)
        return "%s's clue puts %s among its first three cells: with %s here, there'd be no room left for %s" % (
            cap(name), listing(map(str, need)), a_n(v), listing(map(str, miss)))
    if rule == "rossini":
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        side = "the left" if lu < 9 else "the top"
        dr = d["rossini"][lu]
        what = ("rise from %s" % side if dr == 1 else "fall from %s" % side if dr == 2
                else "neither rise nor fall (%s has no arrow)" % name)
        return "the first three digits of %s %s, and %s here can't keep that" % (name, what, a_n(v))
    if rule == "diagnc":
        if r.get("with", -1) >= 0 and r.get("marked"):
            return "a mark joins it to the %d in %s at a corner, and %d isn't 1 away from it" % (w, at, v)
        if r.get("with", -1) >= 0:
            if d["variant"] == "Diagonal Consecutive":
                return "it touches the %d in %s at a corner with no mark between them, and %d is 1 away from it" % (
                    w, at, v)
            return "it touches the %d in %s at a corner, and %d is 1 away from it" % (w, at, v)
        if d["variant"] == "Diagonal Consecutive":
            return "the cells touching it at its corners, marked or not, leave no room for %s" % a_n(v)
        return "a cell touching it at a corner would have to be 1 away from %s" % a_n(v)
    if rule == "nroom":
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        clue = d["nroom"][lu]
        line = [lu * 9 + k for k in range(9)] if lu < 9 else [k * 9 + lu - 9 for k in range(9)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        if c == line[0]:
            if v == 1:
                return "%s's clue %d can't start with 1: that would make the clue itself 1" % (cap(name), clue)
            o = line[v - 1]
            if x[v - 1] and x[v - 1] != clue:
                return ("with %s first, the %s cell of %s would have to hold its clue %d, but it's %d" % (
                    a_n(v), ORD[v - 1], name, clue, x[v - 1]))
            return ("with %s first, the %s cell of %s (%s) would have to hold its clue %d, which can't go there" % (
                a_n(v), ORD[v - 1], name, rc(o), clue))
        if x[0]:
            if v == clue:
                return ("%s starts with %s, so its clue %d is in its %s cell, not here" % (
                    cap(name), a_n(x[0]), clue, ORD[x[0] - 1]))
            return "%s starts with %s, so its %s cell is its clue %d" % (cap(name), a_n(x[0]), ORD[x[0] - 1], clue)
        return ("with %s here, no first digit of %s could point at a cell holding its clue %d" % (a_n(v), name, clue))
    if rule == "psky":                                      # Odd/Even Skyscrapers: the seen ones of the clue's parity
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        clue = d["psky"][lu]
        even, want = bool(clue & 16), clue & 15
        kind = "even" if even else "odd"
        line = [lu * 9 + k for k in range(9)] if lu < 9 else [k * 9 + lu - 9 for k in range(9)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        got, top, k, shown = 0, 0, 0, []
        while k < 9 and x[k]:
            if x[k] > top:
                top = x[k]
                shown.append(x[k])
                got += (x[k] % 2 == 0) == even
            k += 1
        side = "the left" if lu < 9 else "the top"
        par = [s for s in shown if (s % 2 == 0) == even]
        if got > want:
            return "seen from %s of %s, %s would already be %d %s buildings seen, more than its clue %d" % (
                side, name, listing(map(str, par)), got, kind, want)
        if top == 9 or k == 9:
            return "seen from %s of %s, %s there would make %d %s buildings seen, not its clue %d" % (
                side, name, a_n(v), got, kind, want)
        if not even and got + 1 > want:
            return ("seen from %s of %s, %s and the 9 still to come would be more odd buildings than its clue %d" % (
                side, name, listing(map(str, par)) or "nothing", want))
        return "seen from %s of %s, %s there would leave too few %s buildings to see for its clue %d" % (
            side, name, a_n(v), kind, want)
    if rule == "magic":                                     # Magic Square: the centre box's lines make 15
        lines = [[30, 31, 32], [39, 40, 41], [48, 49, 50], [30, 39, 48], [31, 40, 49], [32, 41, 50], [30, 40, 50],
                 [32, 40, 48]]
        for ln in lines:
            if c not in ln:
                continue
            o = [x for x in ln if x != c]
            if vals[o[0]] and vals[o[1]]:
                need = 15 - vals[o[0]] - vals[o[1]]
                return "%s and %s make %d on a line of the centre box, so it's %d to make 15" % (
                    rc(o[0]), rc(o[1]), vals[o[0]] + vals[o[1]], need)
        if c == 40:
            return "it's the middle of the magic square: always 5"
        corner = c in (30, 32, 48, 50)
        if (corner and v % 2) or (not corner and v % 2 == 0):
            return "it's %s of the magic square, which is always %s" % (
                "a corner" if corner else "an edge", "even" if corner else "odd")
        return "with %s there, a line of the centre box couldn't make 15" % a_n(v)
    if rule == "flatmates":                                 # Dutch Flatmates: every 5 has a 1 above or a 9 below
        def why_not(o, want, side):
            """why o (above or below) can't hold want: off the board, another digit, or ruled out"""
            if o < 0 or o > 80:
                return "nothing is %s it" % side
            if vals[o]:
                return "%s %s it is %d" % (rc(o), side, vals[o])
            return "%s %s it can't be %d" % (rc(o), side, want)
        if v == 5:
            return "a 5 needs a 1 right above it or a 9 right below it, and %s, %s" % (
                why_not(c - 9 if c > 8 else -1, 1, "above"), why_not(c + 9 if c < 72 else -1, 9, "below"))
        if c < 72 and vals[c + 9] == 5:                     # right above a 5 whose 9 can't come
            return "the 5 in %s needs a 1 right above it, as %s" % (
                rc(c + 9), why_not(c + 18 if c < 63 else -1, 9, "below"))
        if c > 8 and vals[c - 9] == 5:                      # right below a 5 whose 1 can't come
            return "the 5 in %s needs a 9 right below it, as %s" % (
                rc(c - 9), why_not(c - 18 if c > 17 else -1, 1, "above"))
        return "a 5 nearby needs a 1 above it or a 9 below it"
    if rule == "mathrax":                                   # Mathrax: the pair across the circle can't make its clue
        q = d["mathrax"][r["circle"]]
        lab, op, n, a = q["label"], q["op"], q["val"], r["across"]
        if op in (5, 6):
            return "the %s circle's four cells are all %s" % (lab, "even" if op == 5 else "odd")
        if a < 0 or not vals[a]:
            return "no digit across the %s circle could make its clue with %s" % (lab, a_n(v))
        w = vals[a]
        says = {1: "%d and %d don't add up to %d", 2: "%d and %d aren't %d apart", 3: "%d and %d don't multiply to %d",
                4: "neither of %d and %d is %d times the other"}[op] % (v, w, n)
        return "across the %s circle from the %d in %s: %s" % (lab, w, rc(a), says)
    if rule == "antidiag":                                  # Anti-Diagonal: three different digits on it already
        r_, c_ = c // 9, c % 9
        for dg in (0, 1):
            cells = [k * 10 for k in range(9)] if dg == 0 else [k * 9 + 8 - k for k in range(9)]
            if c not in cells:
                continue
            have = sorted({vals[o] for o in cells if vals[o] and o != c})
            if len(have) >= 3 and v not in have:
                return "its long diagonal holds %s already, three different digits at most" % listing(map(str, have))
        return "a long diagonal can't take a fourth different digit"
    if rule == "quadro":                                    # Quadro: a 2x2 block with three of one parity
        r_, c_ = c // 9, c % 9
        for dr in (0, -1):
            for dc in (0, -1):
                tl = (r_ + dr) * 9 + c_ + dc
                if r_ + dr < 0 or r_ + dr > 7 or c_ + dc < 0 or c_ + dc > 7:
                    continue
                others = [x for x in (tl, tl + 1, tl + 9, tl + 10) if x != c]
                ds = [vals[x] for x in others]
                if all(ds) and len({x % 2 for x in ds} | {v % 2}) == 1:
                    return "%s are %s too, so its 2x2 block would hold no %s digit" % (
                        listing("%d in %s" % (vals[x], rc(x)) for x in others), "odd" if v % 2 else "even",
                        "even" if v % 2 else "odd")
        return "a 2x2 block it's in would be all %s" % ("odd" if v % 2 else "even")
    if rule == "aq9":                                       # Anti-Queen 9s: a 9 on its diagonal
        for o in range(81):
            if vals[o] == 9 and o != c and abs(o // 9 - c // 9) == abs(o % 9 - c % 9):
                return "the 9 in %s is on a diagonal with it" % rc(o)
        return "a 9 is on one of its diagonals"
    if rule == "psym":                                      # Point Symmetry: its mirror through the centre
        if w:
            return "its mirror through the centre, %s, is %d: a %d there wouldn't make 10" % (at, w, v)
        return "its mirror through the centre, %s, can't be %d, which a %d would need" % (at, 10 - v, v)
    if rule == "b9":                                        # Before Nine: the digits before the 9 add up to the clue
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        clue = d["b9"][lu]
        line = [lu * 9 + k for k in range(9)] if lu < 9 else [k * 9 + lu - 9 for k in range(9)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        side = "the left" if lu < 9 else "the top"
        before = []
        for k in range(9):
            if x[k] == 9 or not x[k]:
                break
            before.append(x[k])
        if 9 in x and len(before) == x.index(9):            # the 9 and every cell before it filled
            return "from %s of %s, the digits before the 9 would be %s, adding up to %d, not its clue %d" % (
                side, name, listing(map(str, before)) or "none", sum(before), clue)
        if sum(before) > clue:
            return "from %s of %s, %s would already add up to %d before the 9, more than its clue %d" % (
                side, name, listing(map(str, before)), sum(before), clue)
        return "from %s of %s, %s there leaves no way for the digits before the 9 to add up to its clue %d" % (
            side, name, a_n(v), clue)
    if rule == "skysum":
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        clue = d["skysum"][lu]
        line = [lu * 9 + k for k in range(9)] if lu < 9 else [k * 9 + lu - 9 for k in range(9)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        got, top, k = 0, 0, 0
        while k < 9 and x[k]:
            if x[k] > top:
                top, got = x[k], got + x[k]
            k += 1
        side = "the left" if lu < 9 else "the top"
        shown = [x[j] for j in range(k) if x[j] > max(x[:j] or [0])]
        if got > clue:
            return "seen from %s of %s, %s would already add up to %d, more than its clue %d" % (
                side, name, listing(map(str, shown)), got, clue)
        if top == 9 or k == 9:
            return "seen from %s of %s, %s would add up to %d with %s there, not its clue %d" % (
                side, name, listing(map(str, shown)), got, a_n(v), clue)
        if got + 9 > clue:
            return ("seen from %s of %s, %s and the 9 still to come would add up to more than its clue %d" % (
                side, name, listing(map(str, shown)), clue))
        return "seen from %s of %s, %s there would leave too little to make its clue %d" % (side, name, a_n(v), clue)
    if rule == "xsum":
        lu = r["line"]
        name = "row %d" % (lu + 1) if lu < 9 else "column %d" % (lu - 8)
        clue = d["xsum"][lu]
        line = [lu * 9 + k for k in range(9)] if lu < 9 else [k * 9 + lu - 9 for k in range(9)]
        x = [vals[o] for o in line]
        x[line.index(c)] = v
        side = "the left" if lu < 9 else "the top"
        if not x[0]:
            return "%s's clue %d can't be made up with %s there, whatever its first digit" % (cap(name), clue, a_n(v))
        first = x[:x[0]]
        got, empty = sum(first), first.count(0)
        if c == line[0]:
            lead = "with %s first, the first %d digits of %s would add up to its clue %d" % (a_n(v), v, name, clue)
        else:
            lead = "%s starts with %s, so its first %d digits add up to its clue %d" % (cap(name), a_n(x[0]), x[0],
                                                                                         clue)
        if not empty:
            return "%s, but with %s there they come to %d" % (lead, a_n(v), got) if c != line[0] else \
                "%s, but they come to %d" % (lead, got)
        other = "empty cell" if empty == 1 else "%d empty cells" % empty
        return "%s: with %s there, its other %s would need %d, which can't be made with digits not there yet" % (
            lead, a_n(v), other, clue - got) if c != line[0] else \
            "%s: its other %s would need %d, which can't be made with digits not there yet" % (lead, other, clue - got)
    if rule == "lk":
        diag = d["lk"][r["diag"]]
        filled = [vals[o] for o in diag["cells"] if vals[o] and o != c]
        empty = sum(1 for o in diag["cells"] if not vals[o] and o != c)
        left = diag["sum"] - sum(filled) - v
        has = " (with %s already on it)" % listing(map(str, filled)) if filled else ""
        if not empty:
            return "its diagonal of %d%s needs %d there" % (diag["sum"], has, diag["sum"] - sum(filled))
        other = "cell" if empty == 1 else "%d cells" % empty
        return "on its diagonal of %d%s, %s would leave %d for its other %s, too %s" % (
            diag["sum"], has, a_n(v), left, other, "little" if left < empty else "much")
    if rule == "sandwich":
        line = r["line"]
        name = "row %d" % (line + 1) if line < 9 else "column %d" % (line - 8)
        return "%s's sum is %d: with %s there, the digits between its 1 and 9 can't add up to it" % (
            name, d["sandwich"][line], a_n(v))
    return "the variant's rule says no"


def step_text(d, vals, s):
    c, v = s["cell"], s["digit"]
    if s["kind"] == "hidden":
        unit = unit_words(s["unit_name"])
        groups, rules = {}, {}
        for r in s["reasons"]:
            if "by" not in r:
                continue
            if r["via"] == "RULE":                          # (cells with the same reason: one sentence)
                rules.setdefault(rule_words(d, vals, r["cell"], v, r), []).append(r["cell"])
            else:
                groups.setdefault((r["by"], r["via"]), []).append(r["cell"])
        if not groups and not rules:
            return "%s %s %s, and %s is its last empty cell: **%s = %d**." % (cap(unit), needs(unit), a_n(v), rc(c),
                                                                               rc(c), v)
        out = ["%s %s %s." % (cap(unit), needs(unit), a_n(v))]
        for (by, via), cells in groups.items():
            where = listing(rc(x) for x in cells)
            if via == "a knight's move":
                out.append("%s: the %d in %s is a knight's move away." % (where, v, rc(by)))
            elif via == "an argyle line":
                out.append("%s: the %d in %s is on the same argyle line." % (where, v, rc(by)))
            elif via == "the same cage":
                out.append("%s: the %d in %s is in the same cage." % (where, v, rc(by)))
            elif via == "the same region":
                out.append("%s: the %d in %s is in the same shaded region." % (where, v, rc(by)))
            elif via == "a touching cell":
                out.append("%s: %s the %d in %s at a corner." % (where, "touches" if len(cells) == 1 else "touch",
                                                                    v, rc(by)))
            elif via in ("even", "odd"):
                out.append("%s: %s, so not %d." % (where, "shaded (even)" if via == "even" else "ringed (odd)", v))
            else:
                out.append("%s: %s %s one (%s)." % (where, unit_words(via), has(unit_words(via)), rc(by)))
        out += ["%s: %s." % (listing(rc(x) for x in cells), why) for why, cells in rules.items()]
        out.append("Only **%s** is left: **%s = %d**." % (rc(c), rc(c), v))
        return " ".join(out)
    have, extra, rules = {}, [], {}
    for r in s["reasons"]:
        if r["via"] == "RULE":                              # (digits with the same reason: one sentence)
            rules.setdefault(rule_words(d, vals, c, r["digit"], r), []).append(r["digit"])
        elif r["via"] == "a knight's move":
            extra.append("Not %d: the %d in %s is a knight's move away." % (r["digit"], r["digit"], rc(r["by"])))
        elif r["via"] == "an argyle line":
            extra.append("Not %d: the %d in %s is on the same argyle line." % (r["digit"], r["digit"], rc(r["by"])))
        elif r["via"] == "the same cage":
            extra.append("Not %d: the %d in %s is in the same cage." % (r["digit"], r["digit"], rc(r["by"])))
        elif r["via"] == "the same region":
            extra.append("Not %d: the %d in %s is in the same shaded region." % (r["digit"], r["digit"], rc(r["by"])))
        elif r["via"] == "a touching cell":
            extra.append("Not %d: the %d in %s touches it at a corner." % (r["digit"], r["digit"], rc(r["by"])))
        elif r["via"] in ("even", "odd"):
            extra.append("Not %d: the cell is %s." % (r["digit"], r["via"]))
        else:
            have.setdefault(unit_words(r["via"]), []).append(r["digit"])
    extra += ["Not %s: %s." % (listing(map(str, ds), "or"), why) for why, ds in rules.items()]
    parts = ["%s %s %s" % (via, has(via), listing(str(x) for x in sorted(ds))) for via, ds in have.items()]
    lead = cap("; ".join(parts)) + "." if parts else ""
    return " ".join(x for x in [lead] + extra + ["Only %d is left: **%s = %d**." % (v, rc(c), v)] if x)


# ------------------------------------------------------------------ the pictures

def make_drawer(d, fonts):
    n, band = d["n"], BAND if (d["sandwich"] or d.get("sky") or d.get("lk") or d.get("xsum") or d.get("frame")
                              or d.get("outside") or d.get("rossini") or d.get("nroom") or d.get("skysum") or d.get("psky")
                              or d.get("b9")) else 0
    big, small, tiny = fonts
    size = n * CELL + 2 * PAD + band

    def xy(c):
        return PAD + band + (c % 9) * CELL, PAD + band + (c // 9) * CELL

    def centre(c):
        x, y = xy(c)
        return x + CELL / 2, y + CELL / 2

    def mark_at(c, side):
        x, y = xy(c)
        return (x + CELL, y + CELL / 2) if side == RIGHT else (x + CELL / 2, y + CELL)

    cells = [r * 9 + k for r in range(n) for k in range(n)]
    cage_of = {x: k for k, cage in enumerate(d["cages"]) for x in cage["cells"]}

    def dashed(g, a, b):
        steps = 8
        for i in range(0, steps, 2):
            p0 = (a[0] + (b[0] - a[0]) * i / steps, a[1] + (b[1] - a[1]) * i / steps)
            p1 = (a[0] + (b[0] - a[0]) * (i + 1) / steps, a[1] + (b[1] - a[1]) * (i + 1) / steps)
            g.line([p0, p1], fill=MUTED, width=1)

    def draw(vals, step, path):
        img = Image.new("RGB", (size, size), BG)
        g = ImageDraw.Draw(img)
        area = set(step["area"]) if step else set()
        rings, crosses = set(), set()
        for r in (step or {}).get("reasons", []):
            if r.get("by", -1) >= 0:
                rings.add(r["by"])
            if r.get("with", -1) >= 0:
                rings.add(r["with"])
            if "cell" in r and "by" in r:
                crosses.add(r["cell"])
        for c in cells:                                     # backgrounds
            x, y = xy(c)
            bg = GIVEN_BG if d["givens"][c] else CELL_BG
            if d["tint"][c]:
                bg = TINT_BG
            if d["parity"][c] == 1:
                bg = EVEN_BG
            if d["parity"][c] == 3:                         # Fortress: a grey cell
                bg = FORT_BG
            if c in area:
                bg = UNIT_BG
            if step and c == step["cell"]:
                bg = NEW_BG
            g.rectangle([x, y, x + CELL, y + CELL], fill=bg, outline=LINE_THIN, width=THIN)
        kind = d.get("line_kind", 0)
        for t in d["thermos"]:                              # the lines, under the digits
            pts = [centre(c) for c in t]
            if kind in (0, 12):                             # Thermo, Slow Thermo: a soft grey line from a round bulb
                g.line(pts, fill=THERMO, width=int(CELL * 0.3), joint="curve")
                for px, py in pts:
                    rr = CELL * 0.15
                    g.ellipse([px - rr, py - rr, px + rr, py + rr], fill=THERMO)
                bx, by = pts[0]
                rr = CELL * 0.4
                g.ellipse([bx - rr, by - rr, bx + rr, by + rr], fill=THERMO)
            elif kind == 1:                                 # Arrow: a ring, a shaft from its edge, a head
                (x0, y0), (x1, y1) = pts[0], pts[1]
                ln = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
                r0 = CELL * 0.4
                start = (x0 + (x1 - x0) * r0 / ln, y0 + (y1 - y0) * r0 / ln)
                g.line([start] + pts[1:], fill=ARROW, width=4, joint="curve")
                g.ellipse([x0 - r0, y0 - r0, x0 + r0, y0 + r0], outline=ARROW, width=3)
                (ax, ay), (bx, by) = pts[-2], pts[-1]
                ln = ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5
                ux, uy, h = (bx - ax) / ln, (by - ay) / ln, CELL * 0.22
                for s in (-1, 1):
                    g.line([(bx, by), (bx - ux * h - s * uy * h * 0.8, by - uy * h + s * ux * h * 0.8)], fill=ARROW,
                           width=4)
            elif kind == 18:                                # Average Lines: a teal ring, a line from its edge
                (x0, y0), (x1, y1) = pts[0], pts[1]
                ln = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
                r0 = CELL * 0.4
                g.line([(x0 + (x1 - x0) * r0 / ln, y0 + (y1 - y0) * r0 / ln)] + pts[1:], fill=AVG, width=4,
                       joint="curve")
                g.ellipse([x0 - r0, y0 - r0, x0 + r0, y0 + r0], outline=AVG, width=3)
            elif kind == 17:                                # Pill Arrows: a capsule, a shaft from its edge, a head
                (x0, y0), (x1, y1) = pts[0], pts[1]
                r0 = CELL * 0.4
                (sx, sy), (tx, ty) = pts[1], pts[2]
                ln = ((tx - sx) ** 2 + (ty - sy) ** 2) ** 0.5
                start = (sx + (tx - sx) * r0 / ln, sy + (ty - sy) * r0 / ln)
                g.line([start] + pts[2:], fill=ARROW, width=4, joint="curve")
                g.rounded_rectangle([min(x0, x1) - r0, min(y0, y1) - r0, max(x0, x1) + r0, max(y0, y1) + r0], r0,
                                    outline=ARROW, width=3)
                (ax, ay), (bx, by) = pts[-2], pts[-1]
                ln = ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5
                ux, uy, h = (bx - ax) / ln, (by - ay) / ln, CELL * 0.22
                for s in (-1, 1):
                    g.line([(bx, by), (bx - ux * h - s * uy * h * 0.8, by - uy * h + s * ux * h * 0.8)], fill=ARROW,
                           width=4)
            elif kind == 2:                                 # Renban: a wide purple band
                g.line(pts, fill=RENBAN, width=int(CELL * 0.35), joint="curve")
                for px, py in (pts[0], pts[-1]):
                    rr = CELL * 0.175
                    g.ellipse([px - rr, py - rr, px + rr, py + rr], fill=RENBAN)
            elif kind == 4:                                 # Palindrome: a grey band
                g.line(pts, fill=PALINDROME, width=int(CELL * 0.2), joint="curve")
                for px, py in (pts[0], pts[-1]):
                    rr = CELL * 0.1
                    g.ellipse([px - rr, py - rr, px + rr, py + rr], fill=PALINDROME)
            elif kind == 5:                                 # Region Sum Lines: a narrow blue line
                g.line(pts, fill=REGIONSUM, width=int(CELL * 0.12), joint="curve")
            elif kind in (10, 11):                          # Dutch Whispers, Parity Lines: a narrow amber / red line
                g.line(pts, fill=DUTCH if kind == 10 else PARITY, width=int(CELL * 0.12), joint="curve")
            elif kind == 13:                                # Nabner: a narrow brown line
                g.line(pts, fill=NABNER, width=int(CELL * 0.12), joint="curve")
            elif kind == 16:                                # 10 Lines: a wide soft green band
                g.line(pts, fill=TEN, width=int(CELL * 0.2), joint="curve")
                for px, py in (pts[0], pts[-1]):
                    rr = CELL * 0.1
                    g.ellipse([px - rr, py - rr, px + rr, py + rr], fill=TEN)
            elif kind in (14, 15):                          # Lockout: diamonds; Double Arrow: blue rings
                r0, col = CELL * 0.4, ARROW if kind == 14 else DARROW
                a0 = list(pts)
                for e, (cc, nx) in enumerate(((pts[0], pts[1]), (pts[-1], pts[-2]))):
                    ln = ((nx[0] - cc[0]) ** 2 + (nx[1] - cc[1]) ** 2) ** 0.5
                    k0 = r0 * (0.75 if kind == 14 and nx[0] != cc[0] and nx[1] != cc[1] else 1)
                    p_ = (cc[0] + (nx[0] - cc[0]) * k0 / ln, cc[1] + (nx[1] - cc[1]) * k0 / ln)
                    a0[0 if e == 0 else -1] = p_
                g.line(a0, fill=col, width=4, joint="curve")
                for px, py in (pts[0], pts[-1]):
                    if kind == 14:
                        g.polygon([(px, py - r0), (px + r0, py), (px, py + r0), (px - r0, py)], outline=col, width=3)
                    else:
                        g.ellipse([px - r0, py - r0, px + r0, py + r0], outline=col, width=3)
            elif kind in (8, 9):                            # Entropic, Modular Lines: a narrow orange / teal line
                g.line(pts, fill=ENTROPIC if kind == 8 else MODULAR, width=int(CELL * 0.12), joint="curve")
            elif kind == 6:                                 # Zipper Lines: a narrow pink line
                g.line(pts, fill=ZIPPER, width=int(CELL * 0.12), joint="curve")
            elif kind == 7:                                 # Between Lines: a grey line, a ring at each end
                r0 = CELL * 0.4
                a0 = list(pts)
                for e, (cc, nx) in enumerate(((pts[0], pts[1]), (pts[-1], pts[-2]))):
                    ln = ((nx[0] - cc[0]) ** 2 + (nx[1] - cc[1]) ** 2) ** 0.5
                    p_ = (cc[0] + (nx[0] - cc[0]) * r0 / ln, cc[1] + (nx[1] - cc[1]) * r0 / ln)
                    if e == 0:
                        a0[0] = p_
                    else:
                        a0[-1] = p_
                g.line(a0, fill=ARROW, width=4, joint="curve")
                for px, py in (pts[0], pts[-1]):
                    g.ellipse([px - r0, py - r0, px + r0, py + r0], outline=ARROW, width=3)
            else:                                           # German Whispers: a narrow green line
                g.line(pts, fill=WHISPER, width=int(CELL * 0.12), joint="curve")
        if d["variant"] == "Argyle":                        # its eight diagonal lines, corner to corner
            for (r0, c0), dc in ARGYLE_STARTS:
                k = 0
                while r0 + k < 9 and 0 <= c0 + dc * k < 9:
                    k += 1
                (x1, y1), (x2, y2) = xy(r0 * 9 + c0), xy((r0 + k - 1) * 9 + c0 + dc * (k - 1))
                if dc > 0:
                    g.line([x1, y1, x2 + CELL, y2 + CELL], fill=ARGYLE, width=3)
                else:
                    g.line([x1 + CELL, y1, x2, y2 + CELL], fill=ARGYLE, width=3)
        for c in cells:                                     # thick lines between regions, and the frame
            x, y = xy(c)
            col, row = c % 9, c // 9
            if col == n - 1 or d["region"][c] != d["region"][c + 1]:
                g.line([x + CELL, y - 1, x + CELL, y + CELL + 1], fill=LINE, width=THICK)
            if row == n - 1 or d["region"][c] != d["region"][c + 9]:
                g.line([x - 1, y + CELL, x + CELL + 1, y + CELL], fill=LINE, width=THICK)
            if col == 0:
                g.line([x, y - 1, x, y + CELL + 1], fill=LINE, width=THICK)
            if row == 0:
                g.line([x - 1, y, x + CELL + 1, y], fill=LINE, width=THICK)
        for k, cage in enumerate(d["cages"]):               # dashed cages, their sums at the top left
            if d["variant"] == "Extra Regions":
                continue                                    # (shaded instead: the tint)
            for c in cage["cells"]:
                x, y = xy(c)
                m = 4
                for a, b, o in [((x + m, y + m), (x + CELL - m, y + m), c - 9),
                                ((x + m, y + CELL - m), (x + CELL - m, y + CELL - m), c + 9),
                                ((x + m, y + m), (x + m, y + CELL - m), c - 1 if c % 9 else -1),
                                ((x + CELL - m, y + m), (x + CELL - m, y + CELL - m), c + 1 if c % 9 < 8 else -1)]:
                    if cage_of.get(o) != k:
                        dashed(g, a, b)
            x, y = xy(cage["cells"][0])
            if cage["sum"] or cage.get("prod"):             # (Sumless Killer: none)
                g.text((x + 3, y + 1), str(cage.get("prod") or cage["sum"]), font=tiny, fill=GIVEN_FG)
        for c in cells:                                     # odd rings, the digits, the step's marks
            x, y = xy(c)
            if d["parity"][c] == 2:
                g.ellipse([x + 5, y + 5, x + CELL - 5, y + CELL - 5], outline=MUTED, width=2)
            v = vals[c]
            if v:
                fg = GIVEN_FG if d["givens"][c] else PLACED_FG
                if step and c == step["cell"]:
                    fg = NEW_FG
                dy = 3 if c in cage_of else 1
                g.text((x + CELL / 2, y + CELL / 2 + dy), str(v), font=big, fill=fg, anchor="mm")
            if c in rings:
                g.ellipse([x + 5, y + 5, x + CELL - 5, y + CELL - 5], outline=RING, width=3)
            if c in crosses and not v:
                m = 14
                g.line([x + m, y + m, x + CELL - m, y + CELL - m], fill=CROSS, width=3)
                g.line([x + m, y + CELL - m, x + CELL - m, y + m], fill=CROSS, width=3)
        for c in cells:                                     # marks between cells
            for side in (RIGHT, DOWN):
                if (side == RIGHT and c % 9 == n - 1) or (side == DOWN and c // 9 == n - 1):
                    continue
                cx, cy = mark_at(c, side)
                if d["consec_on"] and d["consec"][c] & (1 if side == RIGHT else 2):
                    w, h = (8, 24) if side == RIGHT else (24, 8)
                    g.rounded_rectangle([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2], 3, fill=ORANGE)
                m = (d["edge"][c] & 3) if side == RIGHT else (d["edge"][c] >> 2 & 3)
                if not m or not d["edge_kind"] or d["edge_kind"] == 4:   # (Fortress: the grey cells, no marks)
                    continue
                if d["edge_kind"] == 1:                     # Kropki: white or black dots
                    rr = 7
                    fill, rim = ((255, 255, 255), (43, 47, 54)) if m == 1 else ((43, 47, 54), (255, 255, 255))
                    g.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=fill, outline=rim, width=2 if m == 1 else 1)
                elif d["edge_kind"] == 2:                   # XV
                    g.rounded_rectangle([cx - 9, cy - 10, cx + 9, cy + 10], 4, fill=CELL_BG)
                    g.text((cx, cy), "V" if m == 1 else "X", font=small, fill=ORANGE, anchor="mm")
                else:                                       # Greater-Than: the point toward the smaller
                    s = -1 if m == 1 else 1
                    if side == RIGHT:
                        pts = [(cx - 4 * s, cy - 7), (cx + 4 * s, cy), (cx - 4 * s, cy + 7)]
                    else:
                        pts = [(cx - 7, cy - 4 * s), (cx, cy + 4 * s), (cx + 7, cy - 4 * s)]
                    g.line(pts, fill=ORANGE, width=3, joint="curve")
        for c in cells:                                     # Diagonal Consecutive: a bar across a marked corner
            m, (x, y), h = d.get("dmark", [0] * 81)[c], xy(c), CELL / 7
            if m & 1:
                g.line([(x + CELL - h, y + CELL - h), (x + CELL + h, y + CELL + h)], fill=ORANGE, width=6)
            if m & 2:
                g.line([(x + h, y + CELL - h), (x - h, y + CELL + h)], fill=ORANGE, width=6)
        for q in d["quads"]:                                # circles where four cells meet
            x, y = xy(q["at"])
            cx, cy, rr = x + CELL, y + CELL, 15
            g.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=CELL_BG, outline=ORANGE, width=2)
            digs = "".join(map(str, q["digits"]))
            txt = digs if len(digs) <= 2 else digs[:2] + "\n" + digs[2:]
            g.multiline_text((cx, cy), txt, font=tiny, fill=GIVEN_FG, anchor="mm", align="center", spacing=0)
        for q in d.get("mathrax", []):                      # Mathrax: a clue where four cells meet
            x, y = xy(q["at"])
            cx, cy, rr = x + CELL, y + CELL, 16
            g.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=CELL_BG, outline=ORANGE, width=2)
            g.text((cx, cy), q["label"], font=tiny, fill=GIVEN_FG, anchor="mm")
        for k in d.get("lk", []):                           # Little Killer: a sum before its diagonal, its arrow
            first, top, dr = k["cells"][0], k["top"], k["dir"]
            i = first % 9 if top else first // 9
            spot = i + (0 if dr == 0 else 1)                # the line before / after that column or row
            dx, dy = (-1 if dr == 1 else 1), (-1 if dr == 2 else 1)
            if top:
                x, y = PAD + band + spot * CELL + (-7 if dr == 0 else 7), PAD + band / 2 - 4
            else:
                x, y = PAD + band / 2 - 4, PAD + band + spot * CELL + (-7 if dr == 0 else 7)
            g.text((x - 3 * dx, y), str(k["sum"]), font=tiny, fill=SUM_FG, anchor="mm")
            ax, ay = x + 7 * dx, y + 8 * dy
            g.line([(ax, ay), (ax + 6 * dx, ay + 6 * dy)], fill=SUM_FG, width=2)
            g.line([(ax + 6 * dx, ay + 6 * dy), (ax + 2 * dx, ay + 6 * dy)], fill=SUM_FG, width=2)
            g.line([(ax + 6 * dx, ay + 6 * dy), (ax + 6 * dx, ay + 2 * dy)], fill=SUM_FG, width=2)
        clues = (d.get("sky") or d.get("xsum") or d.get("frame") or d.get("nroom") or d.get("skysum") or d.get("psky")
                 or d.get("b9"))
        if clues:                                           # Skyscraper, X-Sums, Frame, ...: the clues (0: none drawn)
            for i in range(18):
                if not clues[i]:
                    continue
                pos = (PAD + band / 2, xy(i * 9)[1] + CELL / 2) if i < 9 else (xy(i - 9)[0] + CELL / 2, PAD + band / 2)
                if d.get("psky"):                           # Odd/Even Skyscrapers: odd ringed, even shaded
                    rr = 12
                    box = [pos[0] - rr, pos[1] - rr, pos[0] + rr, pos[1] + rr]
                    if clues[i] & 16:
                        g.rounded_rectangle(box, 4, fill=EVEN_BG)
                    else:
                        g.ellipse(box, outline=MUTED, width=2)
                g.text(pos, str(clues[i] & 15 if d.get("psky") else clues[i]), font=small, fill=SUM_FG, anchor="mm")
        if d.get("outside"):                                # Outside Sudoku: the digits of each clue
            for i in range(18):
                txt = "".join(str(x) for x in range(1, 10) if d["outside"][i] >> x & 1)
                if not txt:
                    continue
                if i < 9:
                    x, y = xy(i * 9)
                    pos = (PAD + band / 2, y + CELL / 2)
                else:
                    x, y = xy(i - 9)
                    pos = (x + CELL / 2, PAD + band / 2)
                g.text(pos, txt, font=tiny if len(txt) > 2 else small, fill=SUM_FG, anchor="mm")
        if d.get("rossini"):                                # Rossini: an arrow toward the bigger digits
            for i in range(18):
                dr = d["rossini"][i]
                if not dr:
                    continue
                if i < 9:
                    x, y = xy(i * 9)
                    cx, cy = PAD + band / 2, y + CELL / 2
                    ux, uy = (1, 0) if dr == 1 else (-1, 0)
                else:
                    x, y = xy(i - 9)
                    cx, cy = x + CELL / 2, PAD + band / 2
                    ux, uy = (0, 1) if dr == 1 else (0, -1)
                h = 8
                g.line([(cx - ux * h, cy - uy * h), (cx + ux * h, cy + uy * h)], fill=SUM_FG, width=3)
                tip = (cx + ux * h, cy + uy * h)
                for sgn in (-1, 1):
                    g.line([tip, (tip[0] - ux * 5 + sgn * uy * 5, tip[1] - uy * 5 + sgn * ux * 5)], fill=SUM_FG, width=3)
        if d["sandwich"]:                                   # the sums: left of the rows, above the columns
            for i in range(9):
                x, y = xy(i * 9)
                g.text((PAD + band / 2, y + CELL / 2), str(d["sandwich"][i]), font=small, fill=SUM_FG, anchor="mm")
                x, y = xy(i)
                g.text((x + CELL / 2, PAD + band / 2), str(d["sandwich"][9 + i]), font=small, fill=SUM_FG,
                       anchor="mm")
        # a few flat colours and antialiased digits: 64 palette entries look the same, at a quarter of the size
        img.convert("P", palette=Image.ADAPTIVE, colors=64).save(path, optimize=True)

    return draw


# ------------------------------------------------------------------ the page

def walk(page, fonts):
    tool = os.path.join(ROOT, "tests", "variant_examples")
    if not os.path.exists(tool):
        subprocess.run(["make", "-C", os.path.join(ROOT, "tests"), "examples"], check=True, stdout=subprocess.DEVNULL)
    d = json.loads(subprocess.run([tool, "--json", PAGES[page]], check=True, capture_output=True, text=True).stdout)
    if d["left"]:
        sys.exit("%s: singles alone leave %d cells: not for a walkthrough" % (d["variant"], d["left"]))
    path = os.path.join(ROOT, "docs", "variants", page + ".md")
    rel = "%s-walkthrough" % page
    img_dir = os.path.join(ROOT, "docs", "screenshots", "variants", rel)
    os.makedirs(img_dir, exist_ok=True)
    for f in os.listdir(img_dir):                           # (an older, longer walkthrough's leftovers)
        if f.endswith(".png"):
            os.remove(os.path.join(img_dir, f))
    draw = make_drawer(d, fonts)
    vals = list(d["givens"])
    draw(vals, None, os.path.join(img_dir, "step-00.png"))
    order = "" if page == "classic" else (", taken in the order a box's single comes first, so not quite the "
                                          "order of the worked example above")
    step_dir = os.path.join(ROOT, "docs", "variants", page)   # a page per step, next to the variant's page
    os.makedirs(step_dir, exist_ok=True)
    for f in os.listdir(step_dir):                          # (an older, longer walkthrough's leftovers)
        if re.match(r"step-\d+\.md$", f):
            os.remove(os.path.join(step_dir, f))
    n = len(d["steps"])
    md = ["<!-- walkthrough -->", "<!-- Written by tools/walkthrough.py %s: edit the script, not this part. -->"
          % page, "",
          "The whole puzzle, from the start to the last cell: %d steps, each one a single (one place left for a "
          "digit, or one digit left for a cell)%s. Each step has a page of its own, with **Previous** and **Next**: "
          "pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, "
          "column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and "
          "the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue."
          % (n, order), "",
          "![The start](../screenshots/variants/%s/step-00.png)" % rel, ""]
    page_text = open(path).read()                           # (the heading the walkthrough is under: the links' anchor)
    heads = re.findall(r"^##+ (.+)$", page_text[:page_text.find("<!-- walkthrough -->")], re.M)
    anchor = re.sub(r"[^\w\- ]", "", (heads[-1] if heads else "The whole puzzle, step by step").strip().lower()).replace(" ", "-")
    back = "[All steps](../%s.md#%s)" % (page, anchor)

    def nav(i):
        """the links at the top and bottom of step i's page"""
        prev = "[← Step %d](step-%02d.md)" % (i - 1, i - 1) if i > 1 else "[← The start](../%s.md#%s)" % (page, anchor)
        nxt = "[Step %d →](step-%02d.md)" % (i + 1, i + 1) if i < n else back
        return "**Step %d of %d** · %s · %s · %s" % (i, n, prev, back, nxt)

    links = []                                              # the steps, as their numbers linked: 1 | 2 | 3 ...
    for i, s in enumerate(d["steps"], 1):
        text = step_text(d, vals, s)                        # (the reasons read the grid before the step)
        vals[s["cell"]] = s["digit"]
        name = "step-%02d.png" % i
        draw(vals, s, os.path.join(img_dir, name))
        title = ("the %d in %s" % (s["digit"], unit_words(s["unit_name"])) if s["kind"] == "hidden"
                 else rc(s["cell"]))
        links.append("[%d](%s/step-%02d.md)" % (i, page, i))
        body = ["# %s, step %d: %s" % (d["variant"], i, title), "", nav(i), "", text, "",
                "![Step %d](../../screenshots/variants/%s/%s)" % (i, rel, name), ""]
        if i == n:
            body += ["Solved.", ""]
        body += [nav(i), ""]
        with open(os.path.join(step_dir, "step-%02d.md" % i), "w") as f:
            f.write("<!-- Written by tools/walkthrough.py %s: edit the script, not this page. -->\n" % page +
                    "\n".join(body))
    # the steps' numbers linked, ten to a row (the first ten as the table's header: Markdown needs one)
    rows = [links[k:k + 10] + [""] * (10 - len(links[k:k + 10])) for k in range(0, len(links), 10)]
    md += ["| " + " | ".join(rows[0]) + " |", "|" + ":-:|" * 10]
    md += ["| " + " | ".join(r) + " |" for r in rows[1:]]
    md += ["", "Solved.", "", "<!-- /walkthrough -->"]
    text = open(path).read()
    if "<!-- walkthrough -->" not in text:
        text = text.rstrip("\n") + ("\n\n## The whole puzzle, step by step\n\n"
                                    "<!-- walkthrough -->\n<!-- /walkthrough -->\n")
    a, b = text.find("<!-- walkthrough -->"), text.find("<!-- /walkthrough -->")
    text = text[:a] + "\n".join(md) + text[b + len("<!-- /walkthrough -->"):]
    open(path, "w").write(text)
    print("%-16s %2d steps -> %s" % (d["variant"], len(d["steps"]), os.path.relpath(path, ROOT)), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pages", nargs="*", help="variant pages: classic, kropki ... (see docs/variants/)")
    ap.add_argument("--all", action="store_true", help="every variant page")
    ap.add_argument("--font", help="a TTF for the digits (default: Montserrat Medium, else DejaVu Sans)")
    args = ap.parse_args()
    pages = list(PAGES) if args.all else args.pages
    if not pages:
        ap.error("name a page, or --all")
    for p in pages:
        if p not in PAGES:
            ap.error("unknown page %s (one of: %s)" % (p, ", ".join(PAGES)))
    fp = args.font or next(f for f in FONTS if os.path.exists(f))
    fonts = (ImageFont.truetype(fp, 28), ImageFont.truetype(fp, 16), ImageFont.truetype(fp, 11))
    for p in pages:
        walk(p, fonts)


if __name__ == "__main__":
    main()

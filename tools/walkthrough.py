#!/usr/bin/env python3
"""
A variant page's full walkthrough: its example puzzle (docs/variant-examples.sdx) solved to the
end one single at a time, a picture per step drawn with PIL (no board needed), and the steps
written into the page between the markers

    <!-- walkthrough -->
    <!-- /walkthrough -->

(added as a section "The whole puzzle, step by step" at the end of the page when it has none).

    python3 tools/walkthrough.py classic        one page: docs/variants/classic.md, its pictures in
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
         "palindrome": "Palindrome", "region-sum-lines": "Region", "x-sums": "X-Sums"}

CELL, THICK, THIN, PAD, BAND = 44, 3, 1, 6, 30
BG, LINE, LINE_THIN = (247, 245, 249), (52, 56, 64), (200, 204, 212)
CELL_BG, GIVEN_BG, GIVEN_FG = (255, 255, 255), (238, 241, 246), (27, 31, 36)
TINT_BG, EVEN_BG = (255, 228, 205), (218, 222, 230)       # X / Hyper / Asterisk cells; Even-Odd's even ones
PLACED_FG = (43, 111, 224)                                  # digits from earlier steps
UNIT_BG = (255, 244, 190)                                   # the unit the step looks at
NEW_BG, NEW_FG = (205, 242, 212), (22, 120, 50)             # the step's digit
RING, CROSS, ORANGE = (230, 126, 34), (214, 48, 49), (230, 126, 34)
MUTED, THERMO, SUM_FG = (120, 126, 138), (196, 200, 207), (43, 111, 224)
ARROW, RENBAN, WHISPER = (140, 146, 158), (222, 200, 245), (120, 205, 150)   # (the board's, on white)
PALINDROME, REGIONSUM = (178, 181, 187), (119, 173, 241)
RIGHT, DOWN = 0, 1
WINDOWS = ["top-left", "top-right", "bottom-left", "bottom-right"]
SPOTS = ["top-left", "top", "top-right", "left", "centre", "right", "bottom-left", "bottom", "bottom-right"]


def rc(c):
    return "r%dc%d" % (c // 9 + 1, c % 9 + 1)


def listing(items, word="and"):
    items = list(items)
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " " + word + " " + items[-1]


def a_n(v):
    return "an %d" % v if v == 8 else "a %d" % v


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
    if rule == "gt":
        smaller = (r["mark"] == 1) == r["cell_first"]       # LESS: the left / top one is the smaller
        return "the sign says it's %s than the %d in %s" % ("smaller" if smaller else "bigger", w, at)
    if rule == "range":
        if d.get("sky"):                                    # Skyscraper: the clue keeps it below a height
            if r["lo"] == r["hi"]:
                return "a clue of its row or column makes it %s (see the rules above)" % a_n(r["lo"])
            return "it's near a clue of its row or column, which keeps it at most %d" % r["hi"]
        if d.get("xsum"):                                   # X-Sums: a clue allows only some first digits
            if r["lo"] == r["hi"]:
                return "it's the first cell after a clue, and that clue can only start with %s" % a_n(r["lo"])
            return "it's the first cell after a clue, and that clue can only start with %d to %d" % (r["lo"], r["hi"])
        if d.get("line_kind") == 3 and v == 5:              # German Whispers: no 5 on a line
            return "it's on a green line, and nothing is 5 or more away from a 5"
        if d["thermos"]:
            return "its place on its thermometer keeps it between %d and %d" % (r["lo"], r["hi"])
        return "the chains of signs in its box keep it between %d and %d" % (r["lo"], r["hi"])
    if rule == "thermo":
        if r["after"]:
            return "it comes after the %d in %s on its thermometer, so it's above %d" % (w, at, w)
        return "it comes before the %d in %s on its thermometer, so it's below %d" % (w, at, w)
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
        if kind == 4:                                       # Palindrome: the cell mirroring it
            return "it mirrors the %d in %s on its grey line, which reads the same from both ends" % (w, at)
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
            return "%s needs %s, and %s is its last empty cell: **%s = %d**." % (cap(unit), a_n(v), rc(c), rc(c), v)
        out = ["%s needs %s." % (cap(unit), a_n(v))]
        for (by, via), cells in groups.items():
            where = listing(rc(x) for x in cells)
            if via == "a knight's move":
                out.append("%s: the %d in %s is a knight's move away." % (where, v, rc(by)))
            elif via == "a touching cell":
                out.append("%s: %s the %d in %s at a corner." % (where, "touches" if len(cells) == 1 else "touch",
                                                                    v, rc(by)))
            elif via in ("even", "odd"):
                out.append("%s: %s, so not %d." % (where, "shaded (even)" if via == "even" else "ringed (odd)", v))
            else:
                out.append("%s: %s has one (%s)." % (where, unit_words(via), rc(by)))
        out += ["%s: %s." % (listing(rc(x) for x in cells), why) for why, cells in rules.items()]
        out.append("Only **%s** is left: **%s = %d**." % (rc(c), rc(c), v))
        return " ".join(out)
    have, extra, rules = {}, [], {}
    for r in s["reasons"]:
        if r["via"] == "RULE":                              # (digits with the same reason: one sentence)
            rules.setdefault(rule_words(d, vals, c, r["digit"], r), []).append(r["digit"])
        elif r["via"] == "a knight's move":
            extra.append("Not %d: the %d in %s is a knight's move away." % (r["digit"], r["digit"], rc(r["by"])))
        elif r["via"] == "a touching cell":
            extra.append("Not %d: the %d in %s touches it at a corner." % (r["digit"], r["digit"], rc(r["by"])))
        elif r["via"] in ("even", "odd"):
            extra.append("Not %d: the cell is %s." % (r["digit"], r["via"]))
        else:
            have.setdefault(unit_words(r["via"]), []).append(r["digit"])
    extra += ["Not %s: %s." % (listing(map(str, ds), "or"), why) for why, ds in rules.items()]
    parts = ["%s has %s" % (via, listing(str(x) for x in sorted(ds))) for via, ds in have.items()]
    lead = cap("; ".join(parts)) + "." if parts else ""
    return " ".join(x for x in [lead] + extra + ["Only %d is left: **%s = %d**." % (v, rc(c), v)] if x)


# ------------------------------------------------------------------ the pictures

def make_drawer(d, fonts):
    n, band = d["n"], BAND if d["sandwich"] or d.get("sky") or d.get("lk") or d.get("xsum") else 0
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
            if c in area:
                bg = UNIT_BG
            if step and c == step["cell"]:
                bg = NEW_BG
            g.rectangle([x, y, x + CELL, y + CELL], fill=bg, outline=LINE_THIN, width=THIN)
        kind = d.get("line_kind", 0)
        for t in d["thermos"]:                              # the lines, under the digits
            pts = [centre(c) for c in t]
            if kind == 0:                                   # Thermo: a soft grey line from a round bulb
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
            else:                                           # German Whispers: a narrow green line
                g.line(pts, fill=WHISPER, width=int(CELL * 0.12), joint="curve")
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
            g.text((x + 3, y + 1), str(cage["sum"]), font=tiny, fill=GIVEN_FG)
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
                if not m or not d["edge_kind"]:
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
        for q in d["quads"]:                                # circles where four cells meet
            x, y = xy(q["at"])
            cx, cy, rr = x + CELL, y + CELL, 15
            g.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=CELL_BG, outline=ORANGE, width=2)
            digs = "".join(map(str, q["digits"]))
            txt = digs if len(digs) <= 2 else digs[:2] + "\n" + digs[2:]
            g.multiline_text((cx, cy), txt, font=tiny, fill=GIVEN_FG, anchor="mm", align="center", spacing=0)
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
        clues = d.get("sky") or d.get("xsum")
        if clues:                                           # Skyscraper, X-Sums: the clues (none: nothing drawn)
            for i in range(9):
                x, y = xy(i * 9)
                if clues[i]:
                    g.text((PAD + band / 2, y + CELL / 2), str(clues[i]), font=small, fill=SUM_FG, anchor="mm")
                x, y = xy(i)
                if clues[9 + i]:
                    g.text((x + CELL / 2, PAD + band / 2), str(clues[9 + i]), font=small, fill=SUM_FG, anchor="mm")
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
    md = ["<!-- walkthrough -->", "<!-- Written by tools/walkthrough.py %s: edit the script, not this part. -->"
          % page, "",
          "The whole puzzle, from the start to the last cell: %d steps, each one a single (one place left for a "
          "digit, or one digit left for a cell)%s. In each picture, the unit the step looks at is tinted yellow "
          "(for a cell with one digit left: its row, column and box), the new digit is on green, the digits that "
          "rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are "
          "on grey, digits from earlier steps in blue." % (len(d["steps"]), order), "",
          "![The start](../screenshots/variants/%s/step-00.png)" % rel, ""]
    for i, s in enumerate(d["steps"], 1):
        text = step_text(d, vals, s)                        # (the reasons read the grid before the step)
        vals[s["cell"]] = s["digit"]
        name = "step-%02d.png" % i
        draw(vals, s, os.path.join(img_dir, name))
        title = ("the %d in %s" % (s["digit"], unit_words(s["unit_name"])) if s["kind"] == "hidden"
                 else rc(s["cell"]))
        md += ["#### Step %d: %s" % (i, title), "", text, "",
               "![Step %d](../screenshots/variants/%s/%s)" % (i, rel, name), ""]
    md += ["Solved.", "", "<!-- /walkthrough -->"]
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

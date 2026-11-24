#!/usr/bin/env python3
"""
The docs' lists of variants, written from the code, so adding a variant can't leave a list behind.

    python3 tools/doc_lists.py           rewrite every generated part of the docs
    python3 tools/doc_lists.py --check   change nothing; list what's out of date (and the hand-written lists
                                         that miss a variant), exit 1 if anything is

A generated part sits between two HTML comments, in a paragraph or a table cell:

    <!-- gen:NAME -->...<!-- /gen:NAME -->

(the docs site drops comments; on GitHub they don't show). NAME says what goes there (GENERATORS below); a
name may be used in several files. Everything between the markers is replaced.

Where the lists come from:
    src/gen_geometry.c      VARIANT_NAMES: the variants, in sudoku_variant_t order (0 = Classic)
    src/ui_gen_dialog.c     GEN_CAT_VARS, GEN_CAT_NAMES: New puzzle's tabs and their variants (GEN_VAR_LABEL: checked
                            only, every button has a label)
    src/learn_core.c        LESSONS: the variants with a lesson of their own
    src/stats_db_kinds.c    STATS_KIND_NAMES: the Statistics screen's rows
    src/stats_db_records.c  REC_FAST: the "Fastest <variant>" records
    src/generator.h         sudoku_variant_t: the VAR_* names, to read the two tables above

Checked, not written (their text is hand-made): docs/variants/README.md's table has a row per variant in order,
docs/features-puzzles.md describes each variant under "Variants by category", and every variant has its page.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = ["README.md", "src/mainpage.dox", "docs/variants/README.md"] + sorted(   # (not the variant pages)
    os.path.relpath(os.path.join(d, f), ROOT)
    for d, _, fs in os.walk(os.path.join(ROOT, "docs")) for f in fs
    if f.endswith(".md") and not os.path.relpath(d, ROOT).startswith("docs/variants"))
MARK = re.compile(r"<!-- gen:([a-z0-9-]+) -->(.*?)<!-- /gen:\1 -->", re.S)


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def c_strings(src, array):
    """the string literals of a C array initializer (adjacent literals joined)"""
    m = re.search(r"\b%s\b[^=]*=\s*\{(.*?)\};" % re.escape(array), src, re.S)
    if not m:
        sys.exit("no %s" % array)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    out = []
    for item in re.split(r",\s*(?=\")", body.strip().rstrip(",")):
        out.append("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', item)))
    return out


# ------------------------------------------------------------------ the code's lists

GEN_H, GEOM_C, DIALOG_C = read("src/generator.h"), read("src/gen_geometry.c"), read("src/ui_gen_dialog.c")
VAR_IDS = re.findall(r"^\s*(VAR_[A-Z0-9_]+)(?:\s*=\s*0)?,", GEN_H[GEN_H.index("typedef enum"):GEN_H.index("VAR_COUNT")],
                     re.M)                          # VAR_CLASSIC, VAR_X, ... (sudoku_variant_t order)
NAMES = c_strings(GEOM_C, "VARIANT_NAMES")          # "Classic", "X-Sudoku", ...
if len(NAMES) != len(VAR_IDS):
    sys.exit("VARIANT_NAMES has %d names, sudoku_variant_t %d variants" % (len(NAMES), len(VAR_IDS)))
ID_OF = {v: i for i, v in enumerate(VAR_IDS)}


def shown(name):
    """a name as the docs write it in prose: 6x6 as 6×6"""
    return re.sub(r"(\d)x(\d)", "\\1×\\2", name)


def page(i):
    """variant i's page under docs/variants/, without .md"""
    return re.sub(r"[^a-z0-9]+", "-", NAMES[i].lower()).strip("-")


def tabs():
    """New puzzle's tabs, in the dialog's order: [(tab name, [variant names])] (the full names: the buttons' own
    labels are cut short to fit four across)"""
    cats = c_strings(DIALOG_C, "GEN_CAT_NAMES")
    m = re.search(r"GEN_CAT_VARS\[[^]]*\]\[[^]]*\]\s*=\s*\{(.*?)\n\};", DIALOG_C, re.S)
    rows = re.findall(r"\{([^{}]*)\}", m.group(1))
    lm = re.search(r"GEN_VAR_LABEL\[[^]]*\]\s*=\s*\{(.*?)\n\};", DIALOG_C, re.S)
    label = {v: s for v, s in re.findall(r"\[(VAR_[A-Z0-9_]+)\]\s*=\s*\"([^\"]*)\"", lm.group(1))}
    out = []
    for name, row in zip(cats, rows):
        ids = [v for v in re.findall(r"VAR_[A-Z0-9_]+", row)]
        missing = [v for v in ids if v not in label]
        if missing:
            sys.exit("no GEN_VAR_LABEL for %s" % ", ".join(missing))
        out.append((name, [shown(NAMES[ID_OF[v]]) for v in ids]))
    return out


def lesson_variants():
    """the variants (VAR_*) with a lesson in learn_core.c's LESSONS"""
    src = read("src/learn_core.c")
    body = src[src.index("LESSONS[] = {"):]
    body = body[:body.index("\n};")]
    return set(re.findall(r",\s*(VAR_[A-Z0-9_]+),\s*TECH_", body))


def stats_rows():
    """the Statistics screen's rows below the collections (STATS_KIND_NAMES)"""
    return c_strings(read("src/stats_db_kinds.c"), "STATS_KIND_NAMES")


def record_variants():
    """the variants with a "Fastest ..." record of their own (REC_FAST's OF_VARIANT(n) rows), as named there"""
    src = read("src/stats_db_records.c")
    return [t for t, _ in re.findall(r"\{\"Fastest ([^\"]*)\",\s*OF_VARIANT\((\d+)\)\}", src)]


# ------------------------------------------------------------------ the text written

def listing(items, word="and"):
    items = list(items)
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " " + word + " " + items[-1]


UNITS = "zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen " \
        "seventeen eighteen nineteen".split()
TENS = "_ _ twenty thirty forty fifty sixty seventy eighty ninety".split()


def words(n):
    """n (0..199) in words: seventy-five"""
    if n >= 100:
        return "a hundred" + (" and " + words(n - 100) if n > 100 else "")
    if n < 20:
        return UNITS[n]
    return TENS[n // 10] + ("-" + UNITS[n % 10] if n % 10 else "")


def links(prefix):
    """every variant but Classic linked, then Classic"""
    return "%s (and [Classic](%sclassic.md))" % (
        listing("[%s](%s%s.md)" % (shown(NAMES[i]), prefix, page(i)) for i in range(1, len(NAMES))), prefix)


def no_lesson():
    have = lesson_variants()
    return [shown(NAMES[i]) for i, v in enumerate(VAR_IDS) if v not in have]


GENERATORS = {
    # counts
    "variant-count": lambda: str(len(NAMES)),                       # 74: every variant, Classic too
    "variants-besides-classic": lambda: "%d variants besides Classic" % (len(NAMES) - 1),   # (Doxygen eats the
                                                    # space after a comment: none follows this one)
    "variant-max": lambda: str(len(NAMES) - 1),                     # the highest variant number
    # lists of variants
    "variant-links-readme": lambda: links("docs/variants/"),
    "variant-links-docs": lambda: "**%s**" % ", ".join(
        "[%s](variants/%s.md)" % (shown(NAMES[i]), page(i)) for i in range(1, len(NAMES))),
    "variant-names": lambda: ", ".join(NAMES),                     # as typed in an .sdx line: Mini 6x6
    "variant-numbers": lambda: ", ".join("%d %s" % (i, shown(n)) for i, n in enumerate(NAMES)),
    "variant-range": lambda: "0 Classic … %d %s" % (len(NAMES) - 1, NAMES[-1]),
    "new-puzzle-tabs": lambda: listing("**%s** (%s)" % (t, ", ".join(b)) for t, b in tabs()),
    "lesson-count": lambda: "%d: all but %s, which only change the grid" % (
        len(NAMES) - len(no_lesson()), listing(no_lesson())),
    "stats-row-count": lambda: words(len(stats_rows())),
    "stats-rows": lambda: listing(
        "**%s**%s" % (r, " (Classic from New puzzle)" if i == 0 else "") for i, r in enumerate(stats_rows())),
    "record-variants": lambda: ", ".join(record_variants()),
}


# ------------------------------------------------------------------ the checks of hand-written lists

def checks():
    """what the hand-written lists miss: one message per problem"""
    out = []
    for i in range(len(NAMES)):
        if not os.path.exists(os.path.join(ROOT, "docs/variants/%s.md" % page(i))):
            out.append("docs/variants/%s.md: no page for %s" % (page(i), NAMES[i]))
    table = re.findall(r"^\| (\d+) \| \[[^]]*\]\(([^)]+)\.md\) \|", read("docs/variants/README.md"), re.M)
    want = [(str(i + 1), page(i)) for i in range(len(NAMES))]
    if table != want:
        got = {p for _, p in table}
        miss = [p for _, p in want if p not in got]
        out.append("docs/variants/README.md: its table isn't one row per variant in order%s" % (
            " (missing: %s)" % ", ".join(miss) if miss else ""))
    puzzles = read("docs/features-puzzles.md")
    cat = puzzles[puzzles.index("## Variants by category"):] if "## Variants by category" in puzzles else puzzles
    for i in range(1, len(NAMES)):
        if not re.search(r"^- \*\*\[[^]]*\]\(variants/%s\.md\)\*\*" % re.escape(page(i)), cat, re.M):
            out.append("docs/features-puzzles.md: no entry for %s under Variants by category" % NAMES[i])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="change nothing; exit 1 if a list is out of date")
    args = ap.parse_args()
    stale, unknown, used = [], [], set()
    for path in DOCS:
        text = read(path)

        def fill(m):
            name = m.group(1)
            if name not in GENERATORS:
                unknown.append("%s: <!-- gen:%s --> isn't a list this script knows" % (path, name))
                return m.group(0)
            used.add(name)
            return "<!-- gen:%s -->%s<!-- /gen:%s -->" % (name, GENERATORS[name](), name)

        new = MARK.sub(fill, text)
        if new != text:
            stale.append(path)
            if not args.check:
                with open(os.path.join(ROOT, path), "w", encoding="utf-8") as f:
                    f.write(new)
    problems = unknown + checks()
    for p in problems:
        print(p)
    if args.check:
        for p in stale:
            print("%s: out of date (python3 tools/doc_lists.py)" % p)
        sys.exit(1 if stale or problems else 0)
    for p in stale:
        print("wrote %s" % p)
    print("%d lists, %d files changed" % (len(used), len(stale)))
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()

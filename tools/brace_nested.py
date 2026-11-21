#!/usr/bin/env python3
"""
Braces the loops and conditions astyle 3.1 leaves without braces: those whose body is
itself an if / for / while (astyle's --add-braces braces only the inner one):

    for (int i = 0; i < 9; i++)          for (int i = 0; i < 9; i++) {
        if (x[i]) {             ->           if (x[i]) {
            y++;                                 y++;
        }                                    }
                                         }

    python3 tools/brace_nested.py FILE...    (rewrites them in place; run astyle after it)

Works on astyle's output (4-space indents, K&R braces), so it runs between two astyle
passes in the Makefile's `format`. Prints the files it changed.
"""
import re
import sys

HEADER = re.compile(r"^(\s*)(?:\}\s*)?(?:else\s+)?(?:if|for|while)\s*\(|^(\s*)(?:\}\s*)?else\s*$")


def header_end(lines, i):
    """The line where the header starting at i closes its parentheses, or None if it isn't
    a header without a body on its line (no "{", no statement after the condition)."""
    if re.match(r"^\s*(?:\}\s*)?else\s*$", lines[i]):
        return i
    depth, j = 0, i
    while j < len(lines):
        code = re.sub(r'"(\\.|[^"\\])*"|\'(\\.|[^\'\\])*\'|/\*.*?\*/|//.*', "", lines[j])
        for k, ch in enumerate(code):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    return j if code[k + 1:].strip() == "" else None
        j += 1
    return None


def block_end(lines, start, indent):
    """The last line of the body statement at lines[start] (indented `indent`): astyle has
    braced it, so it ends at a "}" of the same indent that no "else" follows. None if the
    body isn't shaped like that (then nothing is changed)."""
    for j in range(start, len(lines)):
        line = lines[j]
        if not line.strip():
            continue
        ind = len(line) - len(line.lstrip())
        if ind < len(indent):
            return None                             # (left it without its closing brace)
        if ind == len(indent):
            if line.strip().startswith("}"):
                if re.match(r"^\}\s*else\b", line.strip()):
                    continue                        # } else ... {: the chain goes on
                return j
            if j > start:
                return None                         # another statement at that indent
    return None


def fix(text):
    lines = text.split("\n")
    out_changed = False
    i = 0
    while i < len(lines):
        m = HEADER.match(lines[i])
        if not m:
            i += 1
            continue
        end = header_end(lines, i)
        if end is None or end + 1 >= len(lines):
            i += 1
            continue
        indent = re.match(r"^(\s*)", lines[i]).group(1)
        body = lines[end + 1]
        body_indent = re.match(r"^(\s*)", body).group(1)
        if lines[end].rstrip().endswith("{") or len(body_indent) != len(indent) + 4 or \
           not re.match(r"^\s*(if|for|while)\s*\(", body):
            i += 1
            continue
        last = block_end(lines, end + 1, body_indent)
        if last is None or lines[last].strip() != "}":
            i += 1
            continue
        lines[end] = lines[end].rstrip() + " {"
        lines.insert(last + 1, indent + "}")
        out_changed = True
        i += 1
    return "\n".join(lines), out_changed


def main():
    for path in sys.argv[1:]:
        text = open(path, encoding="utf-8").read()
        new, changed = fix(text)
        while changed:                              # (nested ones: until nothing more)
            text = new
            new, changed = fix(text)
        if text != open(path, encoding="utf-8").read():
            open(path, "w", encoding="utf-8").write(text)
            print("braced:", path)


if __name__ == "__main__":
    main()

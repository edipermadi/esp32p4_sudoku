#!/usr/bin/env python3
"""
Turns README.md and docs/*.md into HTML pages for GitHub Pages:

    python3 tools/make_docs_site.py OUT_DIR [VERSION]

    OUT_DIR/index.html          README.md
    OUT_DIR/<page>.html         docs/<page>.md
    OUT_DIR/screenshots/*.png   docs/screenshots/

tools/make_flasher_site.sh calls it with OUT_DIR = <site>/docs, so the pages sit
next to the web flasher (one level up). Links between the Markdown files become
links between the pages; links to other files in the repository go to GitHub.
Needs markdown-it-py (pip install markdown-it-py==3.0.0): CommonMark, like
GitHub, so 2-space nested lists and fenced code inside list items come out right.
"""
import html
import os
import posixpath
import re
import shutil
import sys

from markdown_it import MarkdownIt

REPO = "https://github.com/edipermadi/esp32p4_sudoku"
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (source, page, title in the menu); the menu's order
PAGES = [
    ("README.md", "index.html", "Overview"),
    ("docs/features.md", "features.html", "Features"),
    ("docs/features-puzzles.md", "features-puzzles.html", "Puzzles"),
    ("docs/features-game.md", "features-game.html", "Playing a game"),
    ("docs/features-progress.md", "features-progress.html", "Players & progress"),
    ("docs/features-settings.md", "features-settings.html", "Home, settings & system"),
    ("docs/how-to-play.md", "how-to-play.html", "How to play"),
    ("docs/data.md", "data.html", "Data & storage"),
    ("docs/building.md", "building.html", "Building"),
    ("docs/development.md", "development.html", "Development"),
    ("docs/devctl.md", "devctl.html", "Remote control"),
    ("docs/devctl-protocol.md", "devctl-protocol.html", "devctl protocol"),
]
PAGE_OF = {src: page for src, page, _ in PAGES}

# the line of links at the top of each docs/*.md (the site has its own menu)
NAV_LINE = re.compile(r"^\[← README\]\(\.\./README\.md\).*\n\n?", re.M)


def slug(text):
    """GitHub's heading anchors: lower case, punctuation dropped, spaces to '-'."""
    s = re.sub(r"[^\w\- ]", "", text.strip().lower())
    return s.replace(" ", "-")


def rewrite(url, src):
    """A link or image path in the Markdown file src, as seen from the page."""
    if re.match(r"^[a-z]+:|^#|^//", url):
        return url
    path, _, frag = url.partition("#")
    target = posixpath.normpath(posixpath.join(posixpath.dirname(src), path)) if path else src
    if target in PAGE_OF:
        out = PAGE_OF[target]
    elif target.startswith("docs/screenshots/"):
        out = target[len("docs/"):]
    else:                                           # any other file: on GitHub
        out = "%s/blob/main/%s" % (REPO, target)
    return out + ("#" + frag if frag else "")


def render(src):
    text = open(os.path.join(HERE, src), encoding="utf-8").read()
    text = NAV_LINE.sub("", text)
    md = MarkdownIt("commonmark", {"html": False, "linkify": False}).enable("table").enable("strikethrough")
    tokens = md.parse(text)
    title = None
    seen = {}
    for i, t in enumerate(tokens):
        if t.type == "heading_open":
            words = tokens[i + 1].content
            if t.tag == "h1" and title is None:
                title = words
            s = slug(words)
            n = seen.get(s, 0)
            seen[s] = n + 1
            t.attrSet("id", s if n == 0 else "%s-%d" % (s, n))
        if t.type == "inline":
            for c in t.children or []:
                if c.type == "link_open":
                    c.attrSet("href", rewrite(c.attrGet("href"), src))
                elif c.type == "image":
                    c.attrSet("src", rewrite(c.attrGet("src"), src))
                    c.attrSet("loading", "lazy")
    body = md.renderer.render(tokens, md.options, {})
    # wide tables scroll sideways on a phone instead of the page
    body = body.replace("<table>", '<div class="table"><table>').replace("</table>", "</table></div>")
    return title or src, body


TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} · Sudoku docs</title>
<meta name="description" content="Documentation for the touch-screen Sudoku on the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B.">
<style>
  :root {{
    --bg: #f6f3f7; --card: #ffffff; --text: #1d1f24; --muted: #646b78; --line: #e4e1e8;
    --accent: #2563eb; --accent-text: #ffffff; --code: #efedf2;
    color-scheme: light dark;
  }}
  @media (prefers-color-scheme: dark) {{
    :root {{
      --bg: #15161a; --card: #1f2127; --text: #eceef2; --muted: #9aa1ad; --line: #30333b;
      --accent: #4f8cff; --accent-text: #0b1020; --code: #2a2d35;
    }}
  }}
  * {{ box-sizing: border-box; }}
  body {{
    margin: 0; background: var(--bg); color: var(--text);
    font: 16px/1.6 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  }}
  .top {{ background: var(--card); border-bottom: 1px solid var(--line); }}
  .top .in {{ max-width: 960px; margin: 0 auto; padding: 12px 16px; display: flex; flex-wrap: wrap; gap: 6px 18px; align-items: center; }}
  .brand {{ font-weight: 700; color: var(--text); text-decoration: none; margin-right: auto; }}
  .brand span {{ color: var(--muted); font-weight: 500; font: 600 0.8rem ui-monospace, SFMono-Regular, Menlo, monospace; margin-left: 6px; }}
  .install {{ background: var(--accent); color: var(--accent-text); text-decoration: none; font-weight: 600; padding: 7px 14px; border-radius: 8px; }}
  nav {{ max-width: 960px; margin: 0 auto; padding: 10px 16px 0; display: flex; flex-wrap: wrap; gap: 6px; }}
  nav a {{ color: var(--muted); text-decoration: none; padding: 5px 10px; border-radius: 7px; font-size: 0.93rem; }}
  nav a:hover {{ color: var(--text); background: var(--card); }}
  nav a.on {{ color: var(--text); background: var(--card); box-shadow: inset 0 0 0 1px var(--line); font-weight: 600; }}
  main {{ max-width: 960px; margin: 0 auto; padding: 8px 16px 48px; }}
  article {{ background: var(--card); border: 1px solid var(--line); border-radius: 14px; padding: 8px 28px 28px; margin-top: 12px; overflow-wrap: anywhere; }}
  @media (max-width: 600px) {{ article {{ padding: 4px 16px 20px; }} }}
  h1 {{ font-size: 1.9rem; line-height: 1.2; letter-spacing: -0.01em; }}
  h2 {{ font-size: 1.3rem; margin-top: 2em; padding-bottom: 6px; border-bottom: 1px solid var(--line); }}
  h3 {{ font-size: 1.08rem; margin-top: 1.6em; }}
  h1, h2, h3 {{ scroll-margin-top: 12px; }}
  a {{ color: var(--accent); }}
  li + li {{ margin-top: 4px; }}
  code {{ font: 0.88em ui-monospace, SFMono-Regular, Menlo, monospace; background: var(--code); padding: 1px 5px; border-radius: 5px; overflow-wrap: anywhere; }}
  pre {{ background: var(--code); padding: 12px 14px; border-radius: 10px; overflow-x: auto; line-height: 1.45; }}
  pre code {{ background: none; padding: 0; overflow-wrap: normal; }}
  .table {{ overflow-x: auto; margin: 1em 0; }}
  table {{ border-collapse: collapse; font-size: 0.94rem; }}
  th, td {{ border: 1px solid var(--line); padding: 6px 10px; text-align: left; vertical-align: top; }}
  th {{ background: var(--bg); }}
  img {{ max-width: 100%; height: auto; border-radius: 10px; border: 1px solid var(--line); }}
  td img {{ min-width: 140px; }}
  footer {{ color: var(--muted); font-size: 0.9rem; margin-top: 24px; }}
</style>
</head>
<body>
<div class="top"><div class="in">
  <a class="brand" href="index.html">Sudoku for ESP32-P4<span>{version}</span></a>
  <a href="{repo}">GitHub</a>
  <a class="install" href="../index.html">Install from the browser</a>
</div></div>
<nav>{nav}</nav>
<main>
<article>
{body}
</article>
<footer>Generated from the repository's Markdown files · <a href="{repo}/blob/main/{src}">{src} on GitHub</a></footer>
</main>
</body>
</html>
"""


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    version = sys.argv[2] if len(sys.argv) > 2 else ""
    os.makedirs(out, exist_ok=True)
    for src, page, _ in PAGES:
        title, body = render(src)
        nav = "".join('<a href="%s"%s>%s</a>' % (p, ' class="on"' if p == page else "", html.escape(t))
                      for _, p, t in PAGES)
        doc = TEMPLATE.format(title=html.escape(title), version=html.escape(version), repo=REPO,
                              nav=nav, body=body, src=src)
        with open(os.path.join(out, page), "w", encoding="utf-8") as f:
            f.write(doc)
    shots = os.path.join(out, "screenshots")
    shutil.rmtree(shots, ignore_errors=True)
    shutil.copytree(os.path.join(HERE, "docs", "screenshots"), shots)
    print("docs: %d pages in %s" % (len(PAGES), out))


if __name__ == "__main__":
    main()

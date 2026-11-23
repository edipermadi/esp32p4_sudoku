#!/usr/bin/env python3
"""
Turns README.md and the Markdown under docs/ into HTML pages for GitHub Pages:

    python3 tools/make_docs_site.py OUT_DIR [VERSION]

    OUT_DIR/index.html                  README.md
    OUT_DIR/<page>.html                 docs/<page>.md
    OUT_DIR/variants.html               docs/variants/README.md, and
    OUT_DIR/variant-<name>.html         docs/variants/<name>.md (in the order its table links them)
    OUT_DIR/internals.html              docs/internals/README.md, and
    OUT_DIR/internals-<name>.html       docs/internals/<name>.md (likewise)
    OUT_DIR/screenshots/...             docs/screenshots/
    OUT_DIR/diagrams/*.svg              docs/diagrams/ (the flowcharts)
    OUT_DIR/variant-examples.sdx        the variant pages' example puzzles, to download

The top bar links the code docs (OUT_DIR/../code/, Doxygen) when they are there.

The pages are flat (one folder), so every relative path is the same from each. A
section's pages are under one entry of the menu, with a breadcrumb and previous / next
links. Every link between pages, to an anchor or to an image is checked: a broken
one stops the build.

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

# (source, page, title in the menu); the menu's order. A section (a folder with a README.md)
# brings its pages along after it: section(folder, prefix).
MENU = [
    ("README.md", "index.html", "Overview"),
    ("docs/features.md", "features.html", "Features"),
    ("docs/features-puzzles.md", "features-puzzles.html", "Puzzles"),
    ("docs/variants/README.md", "variants.html", "Variants"),
    ("docs/features-game.md", "features-game.html", "Playing a game"),
    ("docs/features-progress.md", "features-progress.html", "Players & progress"),
    ("docs/achievements.md", "achievements.html", "Achievements"),
    ("docs/features-settings.md", "features-settings.html", "Home, settings & system"),
    ("docs/how-to-play.md", "how-to-play.html", "How to play"),
    ("docs/data.md", "data.html", "Data & storage"),
    ("docs/building.md", "building.html", "Building"),
    ("docs/development.md", "development.html", "Development"),
    ("docs/internals/README.md", "internals.html", "Internals"),
    ("docs/internals/db/README.md", "db.html", "Database tables"),
    ("docs/devctl.md", "devctl.html", "Remote control"),
    ("docs/devctl-protocol.md", "devctl-protocol.html", "devctl protocol"),
]
SECTIONS = {"docs/variants/README.md": "variant-", "docs/internals/README.md": "internals-",
            "docs/internals/db/README.md": "db-"}
DOWNLOADS = ["docs/variant-examples.sdx"]
# pages with a diagram load Mermaid (pinned), in the page's light or dark look
MERMAID = """<script type="module">
import mermaid from "https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.esm.min.mjs";
mermaid.initialize({ startOnLoad: true, theme: matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "default" });
</script>
"""          # copied next to the pages, linked there


def section_pages(readme, prefix):
    """A section's pages, in the order its README links them: (source, page)."""
    folder = posixpath.dirname(readme)
    text = open(os.path.join(HERE, readme), encoding="utf-8").read()
    out = []
    for name in re.findall(r"\]\(([\w-]+)\.md(?:#[^)]*)?\)", text):
        src = "%s/%s.md" % (folder, name)
        if name != "README" and os.path.exists(os.path.join(HERE, src)) and src not in [s for s, _ in out]:
            out.append((src, prefix + name + ".html"))
    have = {s for s, _ in out}                      # (pages the README doesn't link: at the end)
    for f in sorted(os.listdir(os.path.join(HERE, folder))):
        src = "%s/%s" % (folder, f)
        if f.endswith(".md") and f != "README.md" and src not in have:
            out.append((src, prefix + f[:-3] + ".html"))
    return out


# every page: (source, page, menu entry it's under, its section's pages or None)
PAGES = []
for src, page, title in MENU:
    PAGES.append((src, page, page, None))
    if src in SECTIONS:
        sub = section_pages(src, SECTIONS[src])
        PAGES += [(s, p, page, sub) for s, p in sub]
PAGE_OF = {src: page for src, page, _, _ in PAGES}
TITLE_OF = {page: title for _, page, title in MENU}

# the line of links at the top of each Markdown page ("[← README](...) · ...", "[← All
# variants](README.md) · ..."): the site has its own menu
NAV_LINE = re.compile(r"\A(#[^\n]*\n\n)\[← [^\n]*\n\n?")


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
    elif target.startswith("docs/screenshots/") or (target.startswith("docs/diagrams/") and target.endswith(".svg")):
        out = target[len("docs/"):]
    elif target in DOWNLOADS:
        out = posixpath.basename(target)
    else:                                           # any other file: on GitHub
        out = "%s/blob/main/%s" % (REPO, target)
    return out + ("#" + frag if frag else "")


def render(src):
    text = open(os.path.join(HERE, src), encoding="utf-8").read()
    text = NAV_LINE.sub(r"\1", text)
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
    # Mermaid diagrams (as GitHub draws them): drawn in the browser, see MERMAID
    body = re.sub(r'<pre><code class="language-mermaid">(.*?)</code></pre>', r'<pre class="mermaid">\1</pre>', body,
                  flags=re.S)
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
  th code, td code {{ overflow-wrap: normal; }}   /* (in a table: wrapped at spaces only, never inside a name) */
  img {{ max-width: 100%; height: auto; border-radius: 10px; border: 1px solid var(--line); }}
  td img {{ min-width: 140px; }}
  footer {{ color: var(--muted); font-size: 0.9rem; margin-top: 24px; }}
  .crumb {{ color: var(--muted); font-size: 0.92rem; margin: 16px 0 -8px; }}
  .crumb a {{ color: var(--muted); }}
  .pager {{ display: flex; justify-content: space-between; gap: 12px; margin-top: 28px; padding-top: 14px; border-top: 1px solid var(--line); }}
  .pager a {{ text-decoration: none; }}
  pre.mermaid {{ background: none; text-align: center; }}
</style>
</head>
<body>
<div class="top"><div class="in">
  <a class="brand" href="index.html">Sudoku for ESP32-P4<span>{version}</span></a>
  <a href="{repo}">GitHub</a>
{code}  <a class="install" href="../index.html">Install from the browser</a>
</div></div>
<nav>{nav}</nav>
<main>
<article>
{crumb}{body}{pager}
</article>
{script}<footer>Generated from the repository's Markdown files · <a href="{repo}/blob/main/{src}">{src} on GitHub</a></footer>
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
    titles, bodies = {}, {}
    for src, page, _, _ in PAGES:
        titles[page], bodies[page] = render(src)
    # the code docs (Doxygen) next to the pages, one level up, when the site has them
    code = ('  <a href="../code/index.html">Code</a>\n'
            if os.path.exists(os.path.join(out, "..", "code", "index.html")) else "")
    for src, page, under, sub in PAGES:
        nav = "".join('<a href="%s"%s>%s</a>' % (p, ' class="on"' if p == under else "", html.escape(t))
                      for _, p, t in MENU)
        crumb = pager = ""
        if sub:                                     # a section's page: where it is, and its neighbours
            crumb = '<p class="crumb"><a href="%s">%s</a> › %s</p>\n' % (
                under, html.escape(TITLE_OF[under]), html.escape(titles[page]))
            pages = [p for _, p in sub]
            k = pages.index(page)
            prev = '<a href="%s">← %s</a>' % (pages[k - 1], html.escape(titles[pages[k - 1]])) if k else "<span></span>"
            nxt = ('<a href="%s">%s →</a>' % (pages[k + 1], html.escape(titles[pages[k + 1]]))
                   if k + 1 < len(pages) else "<span></span>")
            pager = '\n<p class="pager">%s%s</p>' % (prev, nxt)
        doc = TEMPLATE.format(title=html.escape(titles[page]), version=html.escape(version), repo=REPO,
                              code=code, nav=nav, crumb=crumb, body=bodies[page], pager=pager, src=src,
                              script=MERMAID if 'class="mermaid"' in bodies[page] else "")
        with open(os.path.join(out, page), "w", encoding="utf-8") as f:
            f.write(doc)
    shots = os.path.join(out, "screenshots")
    shutil.rmtree(shots, ignore_errors=True)
    shutil.copytree(os.path.join(HERE, "docs", "screenshots"), shots)
    diagrams = os.path.join(out, "diagrams")         # the flowcharts (rendered by make diagrams)
    shutil.rmtree(diagrams, ignore_errors=True)
    shutil.copytree(os.path.join(HERE, "docs", "diagrams"), diagrams,
                    ignore=lambda d, names: [n for n in names if not n.endswith(".svg")])
    for d in DOWNLOADS:
        shutil.copy(os.path.join(HERE, d), os.path.join(out, posixpath.basename(d)))
    check_links(out)
    print("docs: %d pages in %s" % (len(PAGES), out))


def check_links(out):
    """Every local link and image of the pages exists, and so does every #anchor they name."""
    ids = {}
    for _, page, _, _ in PAGES:
        ids[page] = set(re.findall(r' id="([^"]+)"', open(os.path.join(out, page), encoding="utf-8").read()))
    bad = []
    for _, page, _, _ in PAGES:
        text = open(os.path.join(out, page), encoding="utf-8").read()
        for url in re.findall(r'(?:href|src)="([^"]+)"', text):
            if re.match(r"^[a-z]+:|^//", url) or url == "../index.html":
                continue                            # (another site; the web flasher, one level up)
            path, _, frag = html.unescape(url).partition("#")
            target = path or page
            if not os.path.exists(os.path.join(out, target)):
                bad.append("%s: %s (no such file)" % (page, url))
            elif frag and target in ids and frag not in ids[target]:
                bad.append("%s: %s (no such anchor)" % (page, url))
    if bad:
        sys.exit("broken links:\n  " + "\n  ".join(sorted(set(bad))))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's user manual (site/site_manual.py, site/manual/*.html, make_site.py manual_page / manual_site): a contents page and
one page a topic, each with the shared header (its Manual tab current) and the manual's own row of links; every fragment is
a listed topic and every topic has its fragment; the links between the pages land on a page and an anchor that exist;
the README, docs/ and the installer send a user to the site, not to Markdown (the user, 2026-11-23: "only dev stuff on
github, user stuff on site"). Run from the repo root:
  python3 tests/manual_page_test.py"""
import importlib
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "site"))       # (site/ is not a package)
make_site = importlib.import_module("make_site")
site_manual = importlib.import_module("site_manual")

failed = 0


def ok(cond: bool, what: str) -> None:
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


topics = [t for t, _, _ in site_manual.TOPICS]
fragments = sorted(p.stem for p in (ROOT / "site" / "manual").glob("*.html"))
ok(sorted(topics) == fragments, f"every topic has its site/manual/<topic>.html and every fragment is a topic ({len(topics)})")
ok(not (ROOT / "docs" / "manual").exists(), "no docs/manual: the manual's text is the site's")

pages = site_manual.pages()
ok(set(pages) == {"index.html", *(f"{t}.html" for t in topics)}, "the pages: index.html and one a topic")

for a in ("about", "installer", "builder", "editor"):
    head = make_site.shared_header(a, root=(a == "about"))
    ok('>Manual</a>' in head and "manual/" in head, f"the {a}'s header has the Manual tab")
ok('href="../manual/">Manual</a>' in make_site.sdcard_page(), "the SD card page's header has it too")
page = make_site.manual_page(*pages["sound.html"])
ok('<a href="../manual/" aria-current="page">Manual</a>' in page, "a manual page's Manual tab is current")
ok('<nav class="manual"' in page and '<a href="sound.html" aria-current="page">Sound</a>' in page, "and its own row marks the topic")

ids = {name: set(re.findall(r'id="([^"]+)"', body)) for name, (_, body) in pages.items()}
broken = []
for name, (_, body) in pages.items():
    for href in re.findall(r'href="([^"#:]*\.html|[^"#:]*\.html#[^"]+|#[^"]+|\./)"', body):
        target, _, frag = href.partition("#")
        target = target or name
        if target == "./":
            target = "index.html"
        if target not in ids or (frag and frag not in ids[target]):
            broken.append(f"{name}: {href}")
ok(broken == [], f"every link between the manual's pages lands on a page and an anchor that exist {broken[:5]}")
md_text = [f"{n}: {m}" for n, (_, b) in pages.items() for m in re.findall(r'<a href="[a-z-]+\.html[^"]*">([a-z-]+\.md)</a>', b)]
ok(md_text == [], f"no link between the pages reads as a Markdown file {md_text[:3]}")

about = (ROOT / "site" / "site_about.py").read_text(encoding="utf-8")
ok("webapp/manual/getting-back.html" in about and "manual/sound.md" not in about, "the About page links the manual's pages")
inst = (ROOT / "site" / "installer" / "installer.js").read_text(encoding="utf-8")
ok("https://w0ts.github.io/optimist/webapp/manual/getting-back.html" in inst, "the installer's help link is the site's Getting back page")
readme = (ROOT / "README.md").read_text(encoding="utf-8")
ok("w0ts.github.io/optimist/webapp/manual" in readme and "docs/manual" not in readme, "the README points to the manual on the site")

sys.path.insert(0, str(ROOT / "tools"))
manual = importlib.import_module("site_release_manual").build()
ok(len(re.findall(r"^## \d+\. ", manual, re.M)) == len(topics) and "webapp/manual/sequencer.html" in manual,
   "the release manual has a numbered section a topic, each linking its page")

pages_html = {"About": make_site.landing_page(), "SD card": make_site.sdcard_page(), "a manual page": page,
              "the editor": make_site.editor_page()}
ok(all(h.count(make_site.SWITCHER) == 1 and h.index(make_site.SWITCHER) < h.index("</head>") for h in pages_html.values()),
   "every page loads the w0ts project switcher in its head (About, SD card, the manual, the editor)")

with tempfile.TemporaryDirectory() as d:
    make_site.manual_site(Path(d))
    written = sorted(p.name for p in (Path(d) / "webapp" / "manual").iterdir())
    ok(written == sorted(pages), "make_site writes webapp/manual/ (the contents and the topics)")

print(f"manual pages: {'all ok' if not failed else f'{failed} failed'}")
sys.exit(1 if failed else 0)

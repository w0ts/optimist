#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's user guide (web/site_guide.py, web/guide/*.html, make_site.py guide_page / guide_site): a contents page and
one page a topic, each with the shared header (its Guide tab current) and the guide's own row of links; every fragment is
a listed topic and every topic has its fragment; the links between the pages land on a page and an anchor that exist;
the README, docs/ and the installer send a user to the site, not to Markdown (the user, 2026-11-23: "only dev stuff on
github, user stuff on site"). Run from the repo root:
  python3 tests/guide_page_test.py"""
import importlib
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))       # (web/ is not a package)
make_site = importlib.import_module("make_site")
site_guide = importlib.import_module("site_guide")

failed = 0


def ok(cond: bool, what: str) -> None:
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


topics = [t for t, _, _ in site_guide.TOPICS]
fragments = sorted(p.stem for p in (ROOT / "web" / "guide").glob("*.html"))
ok(sorted(topics) == fragments, f"every topic has its web/guide/<topic>.html and every fragment is a topic ({len(topics)})")
ok(not (ROOT / "docs" / "guide").exists(), "no docs/guide: the guide's text is the site's")

pages = site_guide.pages()
ok(set(pages) == {"index.html", *(f"{t}.html" for t in topics)}, "the pages: index.html and one a topic")

for a in ("about", "installer", "builder", "editor"):
    head = make_site.shared_header(a, root=(a == "about"))
    ok('>Guide</a>' in head and "guide/" in head, f"the {a}'s header has the Guide tab")
ok('href="../guide/">Guide</a>' in make_site.sdcard_page(), "the SD card page's header has it too")
page = make_site.guide_page(*pages["sound.html"])
ok('<a href="../guide/" aria-current="page">Guide</a>' in page, "a guide page's Guide tab is current")
ok('<nav class="guide"' in page and '<a href="sound.html" aria-current="page">Sound</a>' in page, "and its own row marks the topic")

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
ok(broken == [], f"every link between the guide's pages lands on a page and an anchor that exist {broken[:5]}")
md_text = [f"{n}: {m}" for n, (_, b) in pages.items() for m in re.findall(r'<a href="[a-z-]+\.html[^"]*">([a-z-]+\.md)</a>', b)]
ok(md_text == [], f"no link between the pages reads as a Markdown file {md_text[:3]}")

about = (ROOT / "web" / "site_about.py").read_text(encoding="utf-8")
ok("webapp/guide/getting-back.html" in about and "guide/sound.md" not in about, "the About page links the guide's pages")
inst = (ROOT / "web" / "installer" / "installer.js").read_text(encoding="utf-8")
ok("https://w0ts.github.io/optimist/webapp/guide/getting-back.html" in inst, "the installer's help link is the site's Getting back page")
readme = (ROOT / "README.md").read_text(encoding="utf-8")
ok("w0ts.github.io/optimist/webapp/guide" in readme and "docs/guide" not in readme, "the README points to the guide on the site")

sys.path.insert(0, str(ROOT / "tools"))
manual = importlib.import_module("site_manual").build()
ok(len(re.findall(r"^## \d+\. ", manual, re.M)) == len(topics) and "webapp/guide/sequencer.html" in manual,
   "the release manual has a numbered section a topic, each linking its page")

pages_html = {"About": make_site.landing_page(), "SD card": make_site.sdcard_page(), "a guide page": page,
              "the editor": make_site.editor_page()}
ok(all(h.count(make_site.SWITCHER) == 1 and h.index(make_site.SWITCHER) < h.index("</head>") for h in pages_html.values()),
   "every page loads the w0ts project switcher in its head (About, SD card, the guide, the editor)")

with tempfile.TemporaryDirectory() as d:
    make_site.guide_site(Path(d))
    written = sorted(p.name for p in (Path(d) / "webapp" / "guide").iterdir())
    ok(written == sorted(pages), "make_site writes webapp/guide/ (the contents and the topics)")

print(f"guide pages: {'all ok' if not failed else f'{failed} failed'}")
sys.exit(1 if failed else 0)

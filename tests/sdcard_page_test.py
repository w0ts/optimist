#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's SD card page (web/site_sdcard.py, make_site.py sdcard_page / sdcard_site): written with the shared header and
a tab of its own (SD card) and a work-in-progress warning at the top; every picture it shows is in assets/sd-card/ and
every one there is shown; it links the order files and the tray (the files exist in hardware/); make_site writes the page
and its pictures, and names the photos still to come. Run from the repo root:
  python3 tests/sdcard_page_test.py"""
import importlib
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))       # (web/ is not a package)
make_site = importlib.import_module("make_site")
site_sdcard = importlib.import_module("site_sdcard")

failed = 0


def ok(cond: bool, what: str) -> None:
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


html = make_site.sdcard_page()
nav = re.search(r'<nav class="site".*?</nav>', html, re.S)
ok(nav is not None, "the page has the site's shared header")
ok(nav is not None and re.search(r'<a href="\.\./sd-card/" aria-current="page">SD card</a>', nav.group(0)) is not None,
   "its tab: SD card, marked current on its own page")
for a in ("installer", "editor", "builder"):
    ok('href="../sd-card/">SD card</a>' in make_site.shared_header(a), f"the {a}'s header has the SD card tab")
ok('href="webapp/sd-card/">SD card</a>' in make_site.landing_page(), "the About page's header has it too")
ok("noindex" not in html, "listed: no noindex")
main = html[html.index("<main>"):]
warn = re.search(r'<div class="wip" role="alert">(.*?)</div>', main, re.S)
ok(warn is not None and main.index(warn.group(0)) < main.index("<h2>") and "work in progress" in warn.group(1).lower(),
   "a work-in-progress warning at the top, before the first heading")

shown = set(re.findall(r'src="\.\./\.\./assets/sd-card/([^"]+)"', html))
files = {p.name for p in (ROOT / "assets" / "sd-card").iterdir()}
ok(shown == set(site_sdcard.PICTURES), f"the page shows every listed picture ({len(shown)})")
ok(files == set(site_sdcard.PICTURES), "assets/sd-card/ holds exactly the listed pictures")
ok(all(re.search(r'<img src="[^"]+" alt="[^"]{20,}"', m) for m in re.findall(r"<img [^>]+>", html)),
   "every picture has an alt text")

for rel in ("sd-board/fab/sd-board-gerbers.zip", "sd-board/fab/bom.csv", "sd-board/fab/cpl.csv", "tray/sd-tray.stl"):
    ok(f"{site_sdcard.RAW}{rel}" in html and (ROOT / "hardware" / rel).is_file(), f"links {rel}, which exists")
for words in ("Both Sides", "Standard", "HASL", "1.6", "J2", "J3", "J12", "SD card mode", "m-card"):
    ok(words in html, f"says {words}")

todo = site_sdcard.todo_photos(html)
ok(len(todo) == html.count(site_sdcard.PHOTO_TODO) and all(todo), f"the photos to come are listed ({len(todo)})")

with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp)
    got = make_site.sdcard_site(out)
    page = out / "webapp" / "sd-card" / "index.html"
    ok(page.is_file() and page.read_text(encoding="utf-8") == html, "make_site writes webapp/sd-card/index.html")
    ok(all((out / "assets" / "sd-card" / n).is_file() for n in site_sdcard.PICTURES), "and its pictures at assets/sd-card/")
    ok(got == todo, "and returns the photos still to come")

sys.exit(1 if failed else 0)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 w0ts
"""The release's manual: one page in the shape fm1-base's tools/make_site.py and tools/manual_html.py take (docs/MANUAL.md
of the app: a title, a contents list, numbered sections), pointing to Optimist's guide on the site.

  site_manual.py OUT.md

The guide lives on the site (https://w0ts.github.io/optimist/webapp/guide/, written from web/guide/*.html by
web/site_guide.py): the manual has a numbered section per guide page, its title and what it covers, with the link to it.
tools/fm1base.py site puts the result in the app's directory for the release site's build; it is not kept in fm1-base's
tree (the site's guide is the one source)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GUIDE_URL = "https://w0ts.github.io/optimist/webapp/guide/"
INTRO = ("Optimist is a modular firmware for the M-VAVE FM-1. Its guide is on the site, one page a topic: the controls, the "
         "shortcuts, the sound, the sequencer, the drums, the effects, MIDI and USB, and getting back. What a build contains "
         "depends on its profile: a page or line whose feature is not built is not shown.")


def build() -> str:
    sys.path.insert(0, str(ROOT / "web"))      # (web/ is not a package: site_guide is imported from its folder)
    import site_guide
    topics = [(site_guide.title_of(t), what, f"{GUIDE_URL}{t}.html") for t, _, what in site_guide.TOPICS]
    contents = [f"{i + 1}. {title}" for i, (title, _, _) in enumerate(topics)]
    body = []
    for i, (title, what, url) in enumerate(topics):
        body += [f"## {i + 1}. {title}", "", f"{what[0].upper()}{what[1:]}: [the page on the site]({url}).", ""]
    return "\n".join(["# Optimist", "", INTRO, "", f"[The guide on the site]({GUIDE_URL})", "", "---", "",
                      "## Contents", "", *contents, "", "---", "", *body]).rstrip() + "\n"


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    Path(args[0]).parent.mkdir(parents=True, exist_ok=True)
    Path(args[0]).write_text(build(), encoding="utf-8")

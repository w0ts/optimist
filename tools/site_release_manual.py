#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 w0ts
"""The release's manual: one page in the shape fm1-base's tools/make_site.py and tools/manual_html.py take (docs/MANUAL.md
of the app: a title, a contents list, numbered sections), pointing to Optimist's manual on the site.

  site_release_manual.py OUT.md

Optimist's manual lives on the site (https://w0ts.github.io/optimist/webapp/manual/, written from web/manual/*.html by
web/site_manual.py): this release manual has a numbered section per page of it, its title and what it covers, with the link to it.
tools/fm1base.py site puts the result in the app's directory for the release site's build; it is not kept in fm1-base's
tree (the site's manual is the one source)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANUAL_URL = "https://w0ts.github.io/optimist/webapp/manual/"
INTRO = ("Optimist is a modular firmware for the M-VAVE FM-1. Its manual is on the site, one page a topic: the controls, the "
         "shortcuts, the sound, the sequencer, the drums, the effects, MIDI and USB, and getting back. What a build contains "
         "depends on its profile: a page or line whose feature is not built is not shown.")


def build() -> str:
    sys.path.insert(0, str(ROOT / "web"))      # (web/ is not a package: site_manual is imported from its folder)
    import site_manual
    topics = [(site_manual.title_of(t), what, f"{MANUAL_URL}{t}.html") for t, _, what in site_manual.TOPICS]
    contents = [f"{i + 1}. {title}" for i, (title, _, _) in enumerate(topics)]
    body = []
    for i, (title, what, url) in enumerate(topics):
        body += [f"## {i + 1}. {title}", "", f"{what[0].upper()}{what[1:]}: [the page on the site]({url}).", ""]
    return "\n".join(["# Optimist", "", INTRO, "", f"[The manual on the site]({MANUAL_URL})", "", "---", "",
                      "## Contents", "", *contents, "", "---", "", *body]).rstrip() + "\n"


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    Path(args[0]).parent.mkdir(parents=True, exist_ok=True)
    Path(args[0]).write_text(build(), encoding="utf-8")

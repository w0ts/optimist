#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The release's manual, from Optimist's own guide (docs/guide/*.md): one page in the shape fm1-base's tools/make_site.py
and tools/manual_html.py take (docs/MANUAL.md of the app: a title, a contents list, numbered sections).

  site_manual.py [GUIDE_DIR] OUT.md

Each guide file is a section (its title the section's, its headings one level down, its third-level headings bold lines:
manual_html.py has three levels); a link to another guide file is a link to its section, a link to a file of the
repository its address on GitHub. tools/fm1base.py site puts the result in the app's directory for the site's build; it
is not kept in fm1-base's tree (the guide is the one source)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GUIDE = ROOT / "docs" / "guide"
REPO = "https://github.com/w0ts/optimist/blob/main/"
ORDER = ["interface.md", "shortcuts.md", "sound.md", "sequencer.md", "drums.md", "effects.md", "midi-usb.md"]
INTRO = ("Optimist is a modular firmware for the M-VAVE FM-1. This manual is the guide that comes with it, one section a "
         "page of it: the controls, the shortcuts, the sound, the sequencer, the drums, the effects, and MIDI and USB. What "
         "a build contains depends on its profile: a page or line whose feature is not built is not shown.")


def slug(title: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")


def retarget(text: str, numbers: dict, where: Path) -> str:
    """links: another guide file -> its section (or its heading); a repository file -> GitHub; the rest as it is"""
    def link(m):
        label, url = m.group(1), m.group(2)
        if re.match(r"[a-z]+://", url) or url.startswith("#"):
            return m.group(0)
        path, _, frag = url.partition("#")
        name = Path(path).name
        if path == name and name in numbers:                     # (a guide file beside this one)
            return f"[{label}](#{slug(frag) if frag else 's' + str(numbers[name])})"
        target = (where.parent / path).resolve()
        try:
            return f"[{label}]({REPO}{target.relative_to(ROOT).as_posix()}{'#' + frag if frag else ''})"
        except ValueError:
            raise SystemExit(f"{where}: the link {url} is outside the repository")
    return re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)


def build(guide: Path = GUIDE) -> str:
    guide = Path(guide)
    files = [guide / n for n in ORDER]
    missing = [f.name for f in files if not f.is_file()]
    if missing:
        raise SystemExit(f"{guide}: no {' '.join(missing)}")
    numbers = {f.name: i + 1 for i, f in enumerate(files)}
    titles = []
    body = []
    for f in files:
        lines = f.read_text(encoding="utf-8").splitlines()
        if not lines or not lines[0].startswith("# "):
            raise SystemExit(f"{f}: the first line is not a title")
        titles.append(lines[0][2:].strip())
        out = [f"## {numbers[f.name]}. {titles[-1]}"]
        fence = False
        for ln in lines[1:]:
            if ln.startswith("```"):
                fence = not fence
            elif not fence and ln.startswith("## "):
                ln = "### " + ln[3:]
            elif not fence and ln.startswith("### "):
                ln = f"**{ln[4:].strip()}**"
            elif not fence and ln.startswith("#"):
                raise SystemExit(f"{f}: a heading this page cannot show: {ln}")
            out.append(retarget(ln, numbers, f) if not fence else ln)
        body += out + [""]
    contents = [f"{i + 1}. {t}" for i, t in enumerate(titles)]
    return "\n".join(["# Optimist", "", INTRO, "", "---", "", "## Contents", "", *contents, "", "---", "", *body]).rstrip() + "\n"


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) not in (1, 2):
        sys.exit(__doc__)
    Path(args[-1]).parent.mkdir(parents=True, exist_ok=True)
    Path(args[-1]).write_text(build(*args[:-1]), encoding="utf-8")

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Copies the web editor and what its page loads from beside itself into a folder: the iOS / macOS Mirror app's bundled editor
(OptimistEditor/ in the app bundle, served by the app's optimistedit: scheme, Sources/Web/EditorSite.swift). Xcode runs it as
a build phase (ios/OptimistMirror/project.yml), so the app always holds the editor of the commit it is built from.
The list is what web/make_site.py puts beside webapp/editor/index.html for the pages the app shows: the editor itself, the shared
style (the editor fetches it for its look) with its font, the icon font and the notices. NOT bundled (the user, 2026-10-29): the
Install, Build and Simulator tabs' modules (installer/, builder/, sim/) and the Simulator's firmware builds; the app has the
Flash tab and the web builder / Simulator as online pages.
    tools/sync_ios_editor.py DEST_DIR"""
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# (source relative to the repository, path in DEST_DIR)
FILES = [
    ("web/editor.html", "index.html"),
    ("web/style/optimist.css", "style/optimist.css"),
    ("web/style/terminus.ttf", "style/terminus.ttf"),
    ("web/fukiai.ttf", "fukiai.ttf"),
    ("web/FUKIAI-LICENSE.txt", "FUKIAI-LICENSE.txt"),
    ("web/TERMINUS-LICENSE.txt", "TERMINUS-LICENSE.txt"),
    ("assets/logo/icon.svg", "icon.svg"),
]


def sync(dest):
    """DEST_DIR holds exactly FILES afterwards (a stale file is removed); returns the paths written"""
    dest = Path(dest)
    if dest.exists():
        shutil.rmtree(dest)
    out = []
    for src, name in FILES:
        target = dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / src, target)
        out.append(target)
    return out


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sync(sys.argv[1])
    print(f"sync_ios_editor: {len(FILES)} files -> {sys.argv[1]}")

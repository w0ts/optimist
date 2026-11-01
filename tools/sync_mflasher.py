#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes the pinned M-Flasher release (tools/mflasher.py: version, file names, sizes, SHA-256, URLs) as JSON for the iOS / macOS
Mirror app's Firmware tab (Xcode runs it as a build phase, ios/OptimistMirror/project.yml, like tools/sync_ios_editor.py): no
copy is committed, so the app's links and checksums are always the ones the builder downloads. The web installer gets the same
data in site.json (web/make_site.py). tests/mflasher_get_test.py checks that all three agree.
    tools/sync_mflasher.py DEST.json"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mflasher  # noqa: E402


def sync(dest):
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(mflasher.site_data(), indent=1) + "\n", encoding="utf-8")
    return dest


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sync(sys.argv[1])
    print(f"sync_mflasher: M-Flasher {mflasher.VERSION} -> {sys.argv[1]}")

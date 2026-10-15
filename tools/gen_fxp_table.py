#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FXP table (editor protocol v13, command 94) in web/editor.html: tests/fxp_table.json -> the block between the
FXP_TABLE markers (the mock device's copy of the firmware's tables).

  gen_fxp_table.py            bring web/editor.html's block up to date
  gen_fxp_table.py --check    exit 1 when it differs (tests)

tests/fxp_table.json is made by the firmware's host test (tests/editor_sync_test.c, built with every FX item:
FXP_WRITE=1 rewrites it), which also fails when the firmware's tables moved away from it.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TABLE = ROOT / "tests" / "fxp_table.json"
EDITOR = ROOT / "web" / "editor.html"
BEGIN = "/* FXP_TABLE BEGIN: tools/gen_fxp_table.py from tests/fxp_table.json (do not edit here) */"
END = "/* FXP_TABLE END */"


def block():
    table = json.loads(TABLE.read_text(encoding="utf-8"))
    return f"{BEGIN}\nconst FXP_TABLE = {json.dumps(table, separators=(',', ':'), sort_keys=True)};\n{END}"


def sync(write=True, path=EDITOR):
    """-> True when the editor holds the table's block (written when write and it did not)"""
    html = path.read_text(encoding="utf-8")
    pat = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.S)
    if not pat.search(html):
        raise SystemExit(f"gen_fxp_table: no FXP_TABLE markers in {path}")
    new = pat.sub(lambda m: block(), html, count=1)
    if new == html:
        return True
    if write:
        path.write_text(new, encoding="utf-8")
    return False


def main(argv):
    if argv[:1] == ["--check"]:
        if not sync(write=False):
            print("gen_fxp_table: web/editor.html's FXP_TABLE block is out of date (python3 tools/gen_fxp_table.py)", file=sys.stderr)
            return 1
        return 0
    if argv:
        print(__doc__, file=sys.stderr)
        return 2
    print("fxp table: web/editor.html " + ("is current" if sync() else "updated"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

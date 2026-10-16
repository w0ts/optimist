#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The firmware's tables the editor's mock device serves (editor protocol v13) in web/editor.html: tests/fxp_table.json (FXP, DLAN) and
tests/scrn_table.json (SCRN), tests/scrs_table.json (SCRS), tests/steplane_table.json (STEPLANE) -> the blocks between the FXP_TABLE / SCRN_TABLE / SCRS_TABLE / STEPLANE_TABLE markers.

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
EDITOR = ROOT / "web" / "editor.html"
# (const, golden): FXP_TABLE from tests/fxp_table.json (the FX owners', the drum lanes' parameters), SCRN_TABLE from tests/scrn_table.json
# (GLOBAL's and SYSTEM's lines: the Optimist UI's, tests/ui_optimist_edscrn.h), SCRS_TABLE from tests/scrs_table.json (the other screens' rows of cards, tests/ui_optimist_edscrs.h), STEPLANE_TABLE from tests/steplane_table.json (v14: the lockable values of a track / drum lane, tests/ui_optimist_edv14.h; data only)
BLOCKS = [("FXP_TABLE", ROOT / "tests" / "fxp_table.json"), ("SCRN_TABLE", ROOT / "tests" / "scrn_table.json"), ("SCRS_TABLE", ROOT / "tests" / "scrs_table.json"),
          ("STEPLANE_TABLE", ROOT / "tests" / "steplane_table.json")]


def markers(name, golden):
    return (f"/* {name} BEGIN: tools/gen_fxp_table.py from tests/{golden.name} (do not edit here) */", f"/* {name} END */")


def block(name, golden):
    begin, end = markers(name, golden)
    table = json.loads(golden.read_text(encoding="utf-8"))
    return f"{begin}\nconst {name} = {json.dumps(table, separators=(',', ':'), sort_keys=True)};\n{end}"


def sync(write=True, path=EDITOR):
    """-> True when the editor holds every table's block (written when write and it did not)"""
    html = path.read_text(encoding="utf-8")
    new = html
    for name, golden in BLOCKS:
        begin, end = markers(name, golden)
        pat = re.compile(re.escape(begin) + r".*?" + re.escape(end), re.S)
        if not pat.search(new):
            raise SystemExit(f"gen_fxp_table: no {name} markers in {path}")
        new = pat.sub(lambda m, n=name, g=golden: block(n, g), new, count=1)
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

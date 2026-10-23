#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Copy one of Clipper's engines (isod89/clipper-fm1 firmware/src/eng_<name>.c) into engines/clipper/: the DSP and the
presets as they are, Clipper's names for ours (the table below), its positional engine_t left out (each port writes
its own, with our designated fields). Run once per engine when it is ported or re-synced; the output is then edited
by hand where the glue differs (see each file's header).
  tools/port_clipper_engine.py CLIPPER_SRC NAME OUT_C"""
import re
import sys
from pathlib import Path

RENAMES = [
    (r"\bP_LD_PIT\b", "P_LEG_PIT"), (r"\bP_LD_FLT\b", "P_LEG_FLT"), (r"\bP_LD_SHP\b", "P_LEG_SHP"),
    (r"\bstatic inline\b", "AINL"), (r"\bhy_saw\(", "osc_saw("),
]


def main(src, name, out):
    text = Path(src, f"eng_{name}.c").read_text()
    head = re.match(r"/\*.*?\*/\n/\*.*?\*/\n", text, re.S)
    if not head:
        sys.exit(f"{name}: no licence and description comments at the top")
    body = text[head.end():]
    cut = body.find("static const engine_t")
    if cut < 0:
        sys.exit(f"{name}: no engine_t")
    body = body[:cut].rstrip() + "\n"
    for pat, rep in RENAMES:
        body = re.sub(pat, rep, body)
    desc = text[len(text[:head.end()].split("*/\n", 1)[0]) + 3:head.end()]
    Path(out).write_text(head.group(0).split("*/\n", 1)[0] + "*/\n" + desc + body)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])

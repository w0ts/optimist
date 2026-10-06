#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 the Optimist contributors
"""The parameter icons: tools/draw_icons.py's drawings round-trip through tools/gen_icons.py.

  - the committed assets/icons.png is exactly what draw_icons.py draws (the script is the source);
  - gen_icons.py reads it back as one icon per name of assets/icons.json, in order, 36 B each;
  - every icon has ink, uses only the four levels, and no two icons are the same.
Run by tests/run_tests.sh."""
import importlib.util
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
fails = 0


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def main():
    try:
        from PIL import Image
    except ImportError:
        print("icons: Pillow missing (pip install pillow)")
        return 1
    draw, gen = module("draw_icons"), module("gen_icons")
    meta = json.loads((ASSETS / "icons.json").read_text())
    names = meta["names"]

    drawn, drawn_names = draw.atlas(ASSETS)
    committed = Image.open(ASSETS / "icons.png").convert("L")
    check(f"assets/icons.png is draw_icons.py's output ({drawn.width}x{drawn.height})",
          drawn_names == names and committed.size == drawn.size and
          committed.tobytes() == drawn.tobytes())
    check("only the four ink levels 0 / 85 / 170 / 255", set(drawn.tobytes()) <= {0, 85, 170, 255})

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        shutil.copy(ASSETS / "icons.json", tmp / "icons.json")
        drawn.save(tmp / "icons.png")
        cell, back_names, cells, note = gen.load(tmp)
        check(f"gen_icons reads {len(cells or [])} icons of {len(names)} names, cell {cell} ({note})",
              cells is not None and back_names == names and len(cells) == len(names) and cell == meta["cell"])
        cells = cells or []
        empty = [n for n, px in zip(names, cells) if not any(px)]
        check(f"every icon has ink{': not ' + ', '.join(empty) if empty else ''}", not empty)
        seen, same = {}, []
        for n, px in zip(names, cells):
            key = tuple(px)
            if key in seen:
                same.append(f"{seen[key]} = {n}")
            seen.setdefault(key, n)
        check(f"no two icons alike{': ' + ', '.join(same) if same else ''}", not same)
        for n, px in zip(names, cells):
            if draw.draw(n) != [px[y * cell:(y + 1) * cell] for y in range(cell)]:
                check(f"icon {n} reads back as drawn", False)
        gen.main(tmp / "icons.h", tmp)
        header = (tmp / "icons.h").read_text()
        m = re.search(r"ICON_DATA\[(\d+)\]\[(\d+)\]", header)
        check("felucca_icons.h: ICON_DATA[86][36], ICON_<NAME> for each name in order",
              m is not None and int(m.group(1)) == len(names) == 86 and int(m.group(2)) == 36 and
              f"#define FELUCCA_ICONS_N {len(names)}" in header and
              re.findall(r"^    (ICON_\w+),$", header, re.M) ==
              ["ICON_" + re.sub(r"[^A-Z0-9]", "_", n.upper()) for n in names])
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

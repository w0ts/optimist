#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 the Optimist contributors
"""Draw Optimist's parameter icons and write the atlas assets/icons.png (the source of truth is this script and
the drawings in tools/icon_drawings.py; the PNG is their output).

  draw_icons.py [ASSETS_DIR]               write ASSETS_DIR/icons.png (default: assets)
  draw_icons.py --sheet OUT.png [SCALE]    a review sheet: every icon enlarged (default 8x) on a grid, named

The atlas follows assets/icons.json (cell, cols, names) and the format tools/gen_icons.py reads: one
cell x cell icon per name, row-major, `cols` cells per row, grey levels 0 / 85 / 170 / 255 for the 2-bit
ink 0 (background) and 1 / 2 / 3 (a third, two thirds, full ink). The firmware tints the ink (icons.c).

Drawn from scratch in 2026-10 (clean room: no earlier icon art was looked at). Style: 12 x 12 px, 1 px strokes
in full ink, the content in the 10 x 10 square inside a 1 px margin; a third of the ink for context (axes,
the inactive part of a shape, guides), two thirds for secondary marks and the odd fill. Families share a
drawing: the five envelope icons are one ADSR with the stage lit; cutoff / reso / keytrack one filter curve;
the waveforms one frame (x 1..10, y 2..9); dist / fold / bits a treated sine; delay / reverb an impulse and
its tail; pitch / transpose / octave / voice the same note head; save / load one tray.

Each icon (tools/icon_drawings.py) is a function of a Canvas, or a text grid: '.' 0, ':' 1, '+' 2, '#' 3.
"""
import importlib.util
import json
import math
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ASSETS = TOOLS.parent / "assets"
CELL = 12
LEVELS = {".": 0, ":": 1, "+": 2, "#": 3}


class Canvas:
    """A cell of 2-bit ink. Marks combine by the larger ink (a stroke over a fill keeps its weight)."""

    def __init__(self, n=CELL):
        self.n = n
        self.px = [[0] * n for _ in range(n)]

    def put(self, x, y, v=3):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.n and 0 <= y < self.n:
            self.px[y][x] = max(self.px[y][x], v)

    def line(self, x0, y0, x1, y1, v=3):
        """Bresenham, both ends included."""
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.put(x0, y0, v)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def poly(self, pts, v=3):
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            self.line(x0, y0, x1, y1, v)

    def dotted(self, x0, y0, x1, y1, v=1, step=2):
        """every `step`-th pixel of a horizontal or vertical line"""
        if x0 == x1:
            for y in range(min(y0, y1), max(y0, y1) + 1, step):
                self.put(x0, y, v)
        else:
            for x in range(min(x0, x1), max(x0, x1) + 1, step):
                self.put(x, y0, v)

    def fill(self, x0, y0, x1, y1, v=3):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, v)

    def rect(self, x0, y0, x1, y1, v=3):
        self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)], v)

    def grid(self, text, x=0, y=0):
        """a bitmap typed as text, '.' : + # = ink 0..3, placed with its top left at (x, y)"""
        rows = [r.strip() for r in text.strip().splitlines()]
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if LEVELS[ch]:
                    self.put(x + i, y + j, LEVELS[ch])

    def ring(self, x, y, d, v=3):
        """a circle of diameter d (4, 6, 8, 10) with its box's top left at (x, y)"""
        self.grid(RINGS[d].replace("#", "#:+#"[v]), x, y)

    def curve(self, f, x0, x1, yc, amp, v=3):
        """y = yc - amp * f(t), t 0..1 over the columns x0..x1, joined as a continuous 1 px stroke"""
        pts = [(x, int(math.floor(yc - amp * f((x - x0) / (x1 - x0)) + 0.5))) for x in range(x0, x1 + 1)]
        self.poly(pts, v)

    def arrow_head(self, x, y, d, v=3, size=2):
        """an open arrow head with its tip at (x, y), pointing d = 'u' 'd' 'l' 'r'"""
        for k in range(1, size + 1):
            if d in "ud":
                yy = y + k if d == "u" else y - k
                self.put(x - k, yy, v)
                self.put(x + k, yy, v)
            else:
                xx = x + k if d == "l" else x - k
                self.put(xx, y - k, v)
                self.put(xx, y + k, v)
        self.put(x, y, v)


RINGS = {
    4: """
        .##.
        #..#
        #..#
        .##.""",
    6: """
        .####.
        #....#
        #....#
        #....#
        #....#
        .####.""",
    8: """
        ..####..
        .#....#.
        #......#
        #......#
        #......#
        #......#
        .#....#.
        ..####..""",
    10: """
        ...####...
        .##....##.
        .#......#.
        #........#
        #........#
        #........#
        #........#
        .#......#.
        .##....##.
        ...####...""",
}


# ---------------------------------------------------------------- output
def _drawings():
    """tools/icon_drawings.py by its path (this script also runs as a module of the tests)"""
    spec = importlib.util.spec_from_file_location("icon_drawings", TOOLS / "icon_drawings.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.ICONS


ICONS = _drawings()


def load_meta(assets):
    meta = json.loads((Path(assets) / "icons.json").read_text())
    return int(meta["cell"]), int(meta.get("cols", 16)), [str(n) for n in meta["names"]]


def draw(name, cell=CELL):
    c = Canvas(cell)
    ICONS[name](c)
    return c.px


def atlas(assets=ASSETS):
    """-> (Pillow L image, names); every name of icons.json must have a drawing here"""
    from PIL import Image
    cell, cols, names = load_meta(assets)
    if cell != CELL:
        raise SystemExit(f"draw_icons: icons.json cell {cell}, these drawings are {CELL}")
    missing = [n for n in names if n not in ICONS]
    if missing:
        raise SystemExit(f"draw_icons: no drawing for {missing}")
    rows = (len(names) + cols - 1) // cols
    img = Image.new("L", (cols * cell, rows * cell), 0)
    for i, n in enumerate(names):
        ox, oy = (i % cols) * cell, (i // cols) * cell
        for y, row in enumerate(draw(n)):
            for x, v in enumerate(row):
                img.putpixel((ox + x, oy + y), v * 85)
    return img, names


def sheet(out, scale=8, assets=ASSETS):
    """every icon at `scale`, ink tinted on black as the firmware draws it, a pixel grid, its name below"""
    from PIL import Image, ImageDraw, ImageFont
    _, cols, names = load_meta(assets)
    cols = 8
    pad, label = 10, 16
    w, h = CELL * scale + 2 * pad, CELL * scale + label + 2 * pad
    rows = (len(names) + cols - 1) // cols
    img = Image.new("RGB", (cols * w, rows * h), (24, 24, 28))
    d = ImageDraw.Draw(img)
    font = ImageFont.load_default()
    tint = (120, 220, 255)
    for i, n in enumerate(names):
        ox, oy = (i % cols) * w + pad, (i // cols) * h + pad
        d.rectangle([ox - 1, oy - 1, ox + CELL * scale, oy + CELL * scale], fill=(0, 0, 0), outline=(70, 70, 80))
        for y, row in enumerate(draw(n)):
            for x, v in enumerate(row):
                col = tuple(c * v // 3 for c in tint)
                d.rectangle([ox + x * scale, oy + y * scale, ox + (x + 1) * scale - 1, oy + (y + 1) * scale - 1],
                            fill=col)
        for k in range(1, CELL):
            g = (40, 40, 46)
            d.line([ox + k * scale - 1, oy, ox + k * scale - 1, oy + CELL * scale - 1], fill=g)
            d.line([ox, oy + k * scale - 1, ox + CELL * scale - 1, oy + k * scale - 1], fill=g)
        d.text((ox, oy + CELL * scale + 4), f"{i} {n}", fill=(220, 220, 220), font=font)
    img.save(out)
    print(f"sheet: {len(names)} icons at {scale}x -> {out}")


def main(argv):
    if argv and argv[0] == "--sheet":
        sheet(argv[1], int(argv[2]) if len(argv) > 2 else 8)
        return
    assets = Path(argv[0]) if argv else ASSETS
    img, names = atlas(assets)
    img.save(assets / "icons.png", optimize=True)
    print(f"icons: {len(names)} drawn -> {assets / 'icons.png'} ({img.width}x{img.height})")


if __name__ == "__main__":
    main(sys.argv[1:])

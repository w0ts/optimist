# SPDX-License-Identifier: CC0-1.0
"""Ground stitching vias for make_board.py, after m-card's stitch() (Julius Vitkauskas, https://github.com/jvitkauskas/m-card,
ec63784, CC0-1.0): a GND via, 0.6 / 0.3 mm, on a 1 mm grid wherever one fits clear of everything and at least `spacing`
from the next ground via, so the two pours are tied together all over. What m-card does not need and this board does:
- none within `fast_clear` of a fast signal's copper (the clock, the data lines): the bottom pour under those lines is
  their solid return path, and a via's hole (0.6 + 0.25 clearance) next to it would cut that path;
- none in a part's box or under a silkscreen text, none nearer than `edge` to the outline.
The grid starts at the lower left and goes up column by column, so the result is the same on every run.
"""
import math

import pcbnew

MM = pcbnew.FromMM
FAST = ("CLK", "CLK_SD", "SD_CLK", "MOSI", "MOSI_SD", "SD_DI", "MISO", "SD_DO", "DAT0")


def _box(bb, grow):
    """a copy of the box grown by `grow` mm (a copy: Inflate works in place and hands back a reference)"""
    box = pcbnew.BOX2I(bb.GetOrigin(), bb.GetSize())
    box.Inflate(MM(grow))
    return box


def _inside(poly, x, y):
    n, hit = len(poly), False
    for i in range(n):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % n]
        if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
            hit = not hit
    return hit


def _edge_dist(poly, x, y):
    best = 1e9
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy or 1.0)))
        best = min(best, math.hypot(x - ax - t * dx, y - ay - t * dy))
    return best


def stitch(board, add_via, outline, spacing=3.0, edge=1.0, gap=0.25, fast_clear=1.5, r=0.3, pitch=1.0):
    """Add the vias with add_via(x, y) (KiCad's page millimetres); returns their places. `outline` is the board's polygon."""
    layers = (pcbnew.F_Cu, pcbnew.B_Cu)
    shapes, fast, boxes = [], [], []
    for fp in board.GetFootprints():
        boxes.append(_box(fp.GetBoundingBox(False), gap + r))
        for p in fp.Pads():
            for layer in layers:
                if p.IsOnLayer(layer):
                    shapes.append(p.GetEffectiveShape(layer))
                    if p.GetNetname() in FAST:
                        fast.append(p.GetEffectiveShape(layer))
    for t in board.GetTracks():
        for layer in layers:
            if t.IsOnLayer(layer):
                shapes.append(t.GetEffectiveShape(layer))
                if t.GetNetname() in FAST:
                    fast.append(t.GetEffectiveShape(layer))
    for d in board.GetDrawings():
        if d.GetClass() == "PCB_TEXT":
            boxes.append(_box(d.GetBoundingBox(), 0.5))
    placed = [(pcbnew.ToMM(t.GetPosition().x), pcbnew.ToMM(t.GetPosition().y)) for t in board.GetTracks()
              if t.GetClass() == "PCB_VIA" and t.GetNetname() == "GND"]
    xs = [p[0] for p in outline]
    ys = [p[1] for p in outline]
    added = []
    x = min(xs) + edge
    while x < max(xs) - edge:
        y = max(ys) - edge                       # (page y runs down: from the board's bottom edge up)
        while y > min(ys) + edge:
            pt = pcbnew.VECTOR2I(MM(x), MM(y))
            ok = _inside(outline, x, y) and _edge_dist(outline, x, y) >= edge
            ok = ok and all(math.hypot(x - a, y - b) >= spacing for a, b in placed)
            ok = ok and not any(bb.Contains(pt) for bb in boxes)
            if ok:
                circle = pcbnew.SHAPE_CIRCLE(pt, MM(r))
                ok = not any(s.Collide(circle, MM(gap)) for s in shapes)
                ok = ok and not any(s.Collide(circle, MM(fast_clear)) for s in fast)
            if ok:
                add_via(x, y)
                placed.append((x, y))
                added.append((round(x, 3), round(y, 3)))
            y -= pitch
        x += pitch
    return added

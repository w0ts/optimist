#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Zakaria Chowdhury (Flowstate, flowstate-fm1 6a8ef32: FONT_L as FONT_S at scale 2)
"""Render the bitmap fonts to a C header (assets/fonts/):
  S  labels / units / status   Latin-1 32..255
  L  large values / titles     32..95 (digits, signs, capitals): S's glyphs drawn at 2x. No bitmap of its
                               own (after Flowstate 6a8ef32, 24.8 KB of flash): a descriptor over S's arrays with
                               scale 2, which gfx.c cv_text draws as 2x2 blocks (nearest neighbour)
Terminus 8x16 (BDF, SIL OFL 1.1). TTF fonts also work through render()
(anti-aliased, tabular figures via the OpenType `tnum` feature).

Glyph format: per glyph an advance width, a bitmap width and an offset; the
bitmap starts FONT_PAD pixels left of the pen position (room for side
bearings); rows top to bottom, 2 pixels per byte (high nibble first),
alpha 0..15 (FONT_BITS 4). A pixel font (every pixel 0 or 15, the PAD
columns empty: Terminus) is FONT_BITS 1: each row only the pixels after
the PAD columns, 8 a byte (MSB first), alpha 15: 3,584 B, not 21,504 B,
the same pixels on screen (tests/font_test.c). A scaled font's h, pad and drawn advance are k times its base
font's; its glyph indices are the base's (the same first character).
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).resolve().parents[1] / "assets" / "fonts"
# (name, file, px, pixel, first, last): pixel fonts are rendered without
# anti-aliasing at their design size
SIZES = [("S", "ter-u16n.bdf", 16, True, 32, 255)]   # Latin-1 (Hügelton needs the umlaut)
# (name, base, scale, first, last): the base font's glyphs drawn scale x scale
SCALED = [("L", "S", 2, 32, 95)]                      # values / titles: digits, signs, capitals
PAD = 2


def render_bdf(path, first, last):
    """BDF bitmap font (Terminus 8x16): exact pixels, cell = the font bounding box."""
    lines = path.read_text(errors="replace").splitlines()
    fbb = next(l for l in lines if l.startswith("FONTBOUNDINGBOX")).split()
    cw, ch, fx, fy = int(fbb[1]), int(fbb[2]), int(fbb[3]), int(fbb[4])
    base = ch + fy                                     # rows above the baseline
    glyphs, i = {}, 0
    while i < len(lines):
        if lines[i].startswith("ENCODING"):
            code = int(lines[i].split()[1])
            j = i
            while not lines[j].startswith("BBX"):
                j += 1
            bw, bh, bx, by = map(int, lines[j].split()[1:5])
            dw = cw
            k = i
            while not lines[k].startswith("BITMAP"):
                if lines[k].startswith("DWIDTH"):
                    dw = int(lines[k].split()[1])
                k += 1
            rows = [int(r, 16) for r in lines[k + 1:k + 1 + bh]]
            nbits = ((bw + 7) // 8) * 8
            cell = [0] * (cw * ch)
            top = base - (by + bh)
            for r, bits in enumerate(rows):
                for c in range(bw):
                    if bits >> (nbits - 1 - c) & 1:
                        x, y = bx + c, top + r
                        if 0 <= x < cw and 0 <= y < ch:
                            cell[y * cw + x] = 15
            glyphs[code] = (dw, cell)
            i = k + bh
        i += 1
    if 0xDC not in glyphs and ord("U") in glyphs:      # Ü for "HÜGELTON": U with dots on its top row
        dw, cell = glyphs[ord("U")]
        cell = cell[:]
        lit = [x for y in range(ch) for x in range(cw) if cell[y * cw + x]]
        if lit:
            top = min(y for y in range(ch) for x in range(cw) if cell[y * cw + x])
            for x in range(cw):
                cell[top * cw + x] = 15 if x in (min(lit), max(lit)) else 0
            for x in range(cw):
                cell[(top + 1) * cw + x] = 0
            glyphs[0xDC] = (dw, cell)
    missing = [chr(c) for c in range(first, min(last, 126) + 1) if c not in glyphs]
    if missing:
        print(f"font {path.name}: no glyph for {''.join(missing)!r} (drawn as '?')")
    out = []
    for c in range(first, last + 1):
        dw, cell = glyphs.get(c, glyphs[ord("?")])
        bw = cw + 2 * PAD                              # keep the PAD convention of the TTF path
        px_ = []
        for y in range(ch):
            px_ += [0] * PAD + cell[y * cw:(y + 1) * cw] + [0] * PAD
        out.append((dw, bw, px_))
    return ch, out


def render(ttf, px, pixel, first, last):
    if ttf.endswith(".bdf"):
        return render_bdf(FONTS / ttf, first, last)
    font = ImageFont.truetype(str(FONTS / ttf), px)
    feats = None if pixel else ["tnum"]
    asc, desc = font.getmetrics()
    top = max(0, font.getbbox("A8|(", features=feats)[1] - 1)
    h = asc + desc - top
    glyphs = []
    for c in range(first, last + 1):
        ch = chr(c)
        adv = int(round(font.getlength(ch, features=feats)))
        bw = adv + 2 * PAD
        img = Image.new("L", (bw, asc + desc), 0)
        ImageDraw.Draw(img).text((PAD, 0), ch, font=font, fill=255, features=feats)
        img = img.crop((0, top, bw, top + h))
        px_ = [(15 if v >= 128 else 0) if pixel else min(15, (v + 8) // 17) for v in img.tobytes()]
        glyphs.append((adv, bw, px_))
    return h, glyphs


def one_bit(h, glyphs):
    """True when every pixel is 0 or 15 and the PAD columns are empty (a pixel font): 1 bit a pixel will do"""
    for _, bw, g in glyphs:
        for y in range(h):
            row = g[y * bw:(y + 1) * bw]
            if any(v not in (0, 15) for v in row) or any(row[:PAD]) or any(row[bw - PAD:]):
                return False
    return True


def pack(h, glyphs, bits):
    """the glyph bitmaps and their offsets. bits 4: rows of bw pixels, 2 a byte (alpha 0..15, high nibble
    first); bits 1: rows of the bw - 2 x PAD pixels after the PAD columns, 8 a byte (MSB first, alpha 15)"""
    data, offs = [], []
    for adv, bw, g in glyphs:
        offs.append(len(data))
        for y in range(h):
            row = g[y * bw:(y + 1) * bw] + [0]
            if bits == 4:
                for x in range(0, bw, 2):
                    data.append((row[x] << 4) | row[x + 1])
                continue
            px_ = row[PAD:bw - PAD]
            for x in range(0, len(px_), 8):
                data.append(sum(0x80 >> i for i, v in enumerate(px_[x:x + 8]) if v))
    return data, offs


def main(out, bits=0):
    """bits: 4 or 1, 0 = 1 when the font allows it (a pixel font: 4.5 times less flash, the same pixels)"""
    lines = [f"/* generated by tools/gen_font.py from {SIZES[0][1]} */",
             "#pragma once", "#include <stdint.h>", f"#define FONT_PAD {PAD}  /* x scale, see below */"]
    made = {}
    for name, ttf, px, pixel, first, last in SIZES:
        h, glyphs = render(ttf, px, pixel, first, last)
        bits = bits or (1 if one_bit(h, glyphs) else 4)
        assert bits == 4 or one_bit(h, glyphs)
        lines += [f"#define FONT_BITS {bits}  /* 1: a bit a pixel (alpha 15), rows after the PAD columns; "
                  "4: alpha 0..15, 2 pixels a byte */", ""]
        data, offs = pack(h, glyphs, bits)
        assert len(data) < 65536
        lines.append(f"static const uint8_t FONT_{name}_DATA[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"static const uint16_t FONT_{name}_OFF[{len(offs)}] = {{" + ", ".join(map(str, offs)) + "};")
        lines.append(f"static const uint8_t FONT_{name}_ADV[{len(glyphs)}] = {{" +
                     ", ".join(str(a) for a, _, _ in glyphs) + "};")
        lines.append(f"static const uint8_t FONT_{name}_BW[{len(glyphs)}] = {{" +
                     ", ".join(str(b) for _, b, _ in glyphs) + "};")
        lines.append(f"static const felucca_font_t FONT_{name} = {{ {h}, {PAD}, {first}, {last}, 1, "
                     f"FONT_{name}_ADV, FONT_{name}_BW, FONT_{name}_OFF, FONT_{name}_DATA }};")
        lines.append("")
        made[name] = (h, first, last, glyphs)
        print(f"font {name}: {ttf} {px}px h {h}, digit adv {glyphs[ord('0') - first][0]}, {len(data)} B")
    for name, base, k, first, last in SCALED:          # no data: the base's arrays, drawn k x k
        h, bfirst, blast, glyphs = made[base]
        assert first == bfirst and last <= blast       # (the same glyph indices)
        lines.append(f"static const felucca_font_t FONT_{name} = {{ {h * k}, {PAD * k}, {first}, {last}, {k}, "
                     f"FONT_{base}_ADV, FONT_{base}_BW, FONT_{base}_OFF, FONT_{base}_DATA }};   /* FONT_{base} x {k} */")
        lines.append("")
        print(f"font {name}: FONT_{base} x {k}, h {h * k}, digit adv {glyphs[ord('0') - first][0] * k}, 0 B")
    Path(out).write_text("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 0)   # (a 2nd argument 4: the alpha format)

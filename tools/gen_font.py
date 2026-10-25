#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 Zakaria Chowdhury (Flowstate, flowstate-fm1 6a8ef32: FONT_L as FONT_S at scale 2)
"""Render the bitmap fonts to a C header (assets/fonts/):
  S  labels / units / status   ASCII 32..126 (no text of the firmware has a byte past 126: those draw as '?',
                               gfx.c glyph; the Latin-1 half was 2,580 B of flash, 2026-10-22)
  L  large values / titles     32..95 (digits, signs, capitals): S's glyphs drawn at 2x. No bitmap of its
                               own (after Flowstate 6a8ef32, 24.8 KB of flash): a descriptor over S's arrays with
                               scale 2, which gfx.c cv_text draws as 2x2 blocks (nearest neighbour)
Terminus 8x16 (BDF, SIL OFL 1.1). TTF fonts also work through render()
(anti-aliased, tabular figures via the OpenType `tnum` feature).

Glyph format: the font's advance width, bitmap width and bytes a glyph takes (monospace: the bitmaps one after
the other, glyph i at i x stride); the bitmap starts FONT_PAD pixels left of the pen position (room for side
bearings); rows top to bottom, 2 pixels per byte (high nibble first),
alpha 0..15 (FONT_BITS 4). A pixel font (every pixel 0 or 15, the PAD
columns empty: Terminus) is FONT_BITS 1: each row only the pixels after
the PAD columns, 8 a byte (MSB first), alpha 15: 3,584 B, not 21,504 B,
the same pixels on screen (tests/font_test.c). A scaled font's h, pad and drawn advance are k times its base
font's; its glyph indices are the base's (the same first character).

The faces (the builder's FONT_* items, tools/builder/registry.py; FELUCCA_FONTS in the environment: their names, in
FACES order, Terminus alone when unset): one face is the header as it always was. Two or more: every face's bitmap
(the first's TAB_RAW_POOL, the others in flash), FONT_S and FONT_L not const (their data pointer follows the face
chosen: gfx.c font_use, SYSTEM > FONT), FONT_N, FONT_DATA, FONT_ID (the face's stable number, FACES' index: what the
settings record keeps) and FONT_NAME. Every face must have Terminus' cell (8x16, the same first / last, the same
format): the screens' layouts are made for it.
"""
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).resolve().parents[1] / "assets" / "fonts"
# the faces a build may carry: (name, file); the index is the face's stable id (the settings record keeps it): append
# only, never reorder (assets/fonts/, LICENSING.md)
FACES = [("TERMINUS", "ter-u16n.bdf"),          # Terminus 8x16 (SIL OFL 1.1), the default
         ("TERM BOLD", "ter-u16b.bdf"),          # Terminus bold 8x16 4.49.1 (SIL OFL 1.1): heavy, the most legible
         ("TAMZEN", "Tamzen8x16r.bdf"),          # thin; Tamzen 8x16 regular (Tamsyn / Tamzen licence: free, use as you see fit)
         ("UNIFONT", "unifont-8x16-ascii.bdf"),  # the classic PC look; GNU Unifont 16.0.04, ASCII (SIL OFL 1.1 / GPL-2.0-or-later)
         ("UNSCII", "unscii-16-ascii.bdf"),      # round, 8-bit computer style; Unscii 16 by Viznut, ASCII (public domain)
         # the Ultimate Oldschool PC Font Pack by VileR (int10h.org, CC BY-SA 4.0: GPL-3.0 one-way compatible),
         # ASCII of the Px437 TTF at 16 px written as BDF, pixel for pixel its Bm437 bitmap strike
         ("IBM VGA", "Px437_IBM_VGA_8x16.bdf"),          # the IBM VGA BIOS text mode: the DOS look
         ("TOSHIBA", "Px437_ToshibaTxL1_8x16.bdf"),      # Toshiba T-series laptops: heavy, slab serifs
         ("COMPAQ", "Px437_CompaqThin_8x16.bdf"),        # Compaq's thin VGA face: one-pixel strokes, PC shapes
         ("NEC APC", "Px437_NEC_APC3_8x16.bdf"),         # NEC APC III: thin, round, wide, a slashed zero
         ("STINGRAY", "Px437_CL_Stingray_8x16.bdf")]     # Cirrus Logic Stingray VGA: the heaviest, tall capitals
FACE_IDS = {n: i for i, (n, _) in enumerate(FACES)}


def faces_built(env=None):
    """-> [(id, name, file)] the faces of this build, in FACES order: FELUCCA_FONTS (names, comma separated), Terminus
    alone when unset; none is an error (every build draws text)"""
    v = (os.environ if env is None else env).get("FELUCCA_FONTS")
    names = ["TERMINUS"] if v is None else [n.strip().upper() for n in v.split(",") if n.strip()]
    bad = [n for n in names if n not in FACE_IDS]
    if bad:
        raise SystemExit(f"gen_font: FELUCCA_FONTS: no face {', '.join(bad)} (one of {', '.join(FACE_IDS)})")
    if not names:
        raise SystemExit("gen_font: FELUCCA_FONTS names no face: a build needs at least one font")
    return [(FACE_IDS[n], n, FACES[FACE_IDS[n]][1]) for n in sorted(set(names), key=FACE_IDS.get)]


# (name, file, px, pixel, first, last): pixel fonts are rendered without
# anti-aliasing at their design size
SIZES = [("S", "ter-u16n.bdf", 16, True, 32, 126)]   # ASCII (the splash is a picture: no umlaut drawn as text)
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
                        elif first <= code <= last:   # (Tamzen's 8x17 boxes: their extra row is empty)
                            raise SystemExit(f"font {path.name}: {chr(code)!r} has a pixel outside the {cw}x{ch} cell")
            glyphs[code] = (dw, cell)
            i = k + bh
        i += 1
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


def face_data(name, ttf, px, pixel, first, last, bits):
    """one face's bitmaps: -> (bits, data, (h, glyphs, adv, bw, stride))"""
    h, glyphs = render(ttf, px, pixel, first, last)
    bits = bits or (1 if one_bit(h, glyphs) else 4)
    assert bits == 4 or one_bit(h, glyphs)
    data, offs = pack(h, glyphs, bits)
    assert len(data) < 65536
    # a monospace font (Terminus: every glyph the same advance and width, the bitmaps one after the other): the
    # descriptor carries the advance, the width and the bytes a glyph takes, no per-glyph tables (gfx.c; 380 B
    # less than the three tables, 2026-10-22). A proportional font would need them back (and gfx.c's reader).
    adv, bw = glyphs[0][0], glyphs[0][1]
    stride = len(data) // len(glyphs)
    if any(a != adv or b != bw for a, b, _ in glyphs) or offs != [i * stride for i in range(len(glyphs))]:
        raise SystemExit(f"gen_font: {ttf} is not monospace: gfx.c draws monospace fonts (per-glyph tables)")
    print(f"font {name}: {ttf} {px}px h {h}, digit adv {glyphs[ord('0') - first][0]}, {len(data)} B")
    return bits, data, (h, glyphs, adv, bw, stride)


def c_array(cname, data, attr=" TAB_RAW_POOL"):
    out = [f"static const uint8_t {cname}[{len(data)}]{attr} = {{"]   # (felucca_tables.h: the pool with TAB_CODED_ALL)
    for i in range(0, len(data), 24):
        out.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    return out + ["};"]


def main(out, bits=0):
    """bits: 4 or 1, 0 = 1 when the font allows it (a pixel font: 4.5 times less flash, the same pixels)"""
    faces = faces_built()
    multi = len(faces) > 1
    lines = [f"/* generated by tools/gen_font.py from {', '.join(f for _, _, f in faces)} */",
             "#pragma once", "#include <stdint.h>", f"#define FONT_PAD {PAD}  /* x scale, see below */",
             "/* TAB_RAW_POOL: felucca_tables.h's (the firmware includes it first: the bitmaps in the pool with TAB_CODED_ALL);",
             " * nothing when this header is read alone (the host tests' font checks) */",
             "#ifndef TAB_RAW_POOL", "#define TAB_RAW_POOL", "#endif"]
    made = {}
    const = "" if multi else "const "                  # (several faces: FONT_S / FONT_L's data follows the face chosen)
    for name, _, px, pixel, first, last in SIZES:
        bits, data, m = face_data(name, faces[0][2], px, pixel, first, last, bits)
        h, glyphs, adv, bw, stride = m
        lines += [f"#define FONT_BITS {bits}  /* 1: a bit a pixel (alpha 15), rows after the PAD columns; "
                  "4: alpha 0..15, 2 pixels a byte */", ""]
        lines += c_array(f"FONT_{name}_DATA", data)
        extra = []
        for j, (_, fname, ffile) in enumerate(faces[1:], 1):   # (the other faces: in flash, the same cell and format)
            b2, d2, m2 = face_data(f"{name}{j} {fname}", ffile, px, pixel, first, last, bits)
            if b2 != bits or m2[0] != h or m2[2:] != m[2:] or len(d2) != len(data):
                raise SystemExit(f"gen_font: {ffile} is not {faces[0][2]}'s cell ({adv}x{h}, {bits}-bit): the screens "
                                 "are laid out for it")
            lines += c_array(f"FONT_{name}{j}_DATA", d2, "")
            extra.append(f"FONT_{name}{j}_DATA")
        lines.append(f"static {const}felucca_font_t FONT_{name} = {{ {h}, {PAD}, {first}, {last}, 1, "
                     f"{adv}, {bw}, {stride}, FONT_{name}_DATA }};")
        lines.append("")
        made[name] = (h, first, last, glyphs, adv, bw, stride)
        if multi:
            assert name == "S" and len(SIZES) == 1          # (FONT_DATA: the small font's faces)
            lines += [f"#define FONT_N {len(faces)}u  /* the faces built (gfx.c font_use: SYSTEM > FONT) */",
                      f"static const uint8_t *const FONT_DATA[FONT_N] = {{ FONT_{name}_DATA, {', '.join(extra)} }};",
                      "static const uint8_t FONT_ID[FONT_N] = { " + ", ".join(str(i) for i, _, _ in faces)
                      + " };   /* the face's stable number (tools/gen_font.py FACES): the settings record's */",
                      "static const char *const FONT_NAME[FONT_N] = { " + ", ".join(f'"{n}"' for _, n, _ in faces)
                      + " };", ""]
        else:
            lines += ["#define FONT_N 1u  /* one face: no choice, nothing to select (gfx.c) */", ""]
    for name, base, k, first, last in SCALED:          # no data: the base's bitmaps, drawn k x k
        h, bfirst, blast, glyphs, adv, bw, stride = made[base]
        assert first == bfirst and last <= blast       # (the same glyph indices)
        lines.append(f"static {const}felucca_font_t FONT_{name} = {{ {h * k}, {PAD * k}, {first}, {last}, {k}, "
                     f"{adv}, {bw}, {stride}, FONT_{base}_DATA }};   /* FONT_{base} x {k} */")
        lines.append("")
        print(f"font {name}: FONT_{base} x {k}, h {h * k}, digit adv {glyphs[ord('0') - first][0] * k}, 0 B")
    Path(out).write_text("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 0)   # (a 2nd argument 4: the alpha format)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Patch data: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0's own FM6 patches,
# as SLOOP 2.4 ships them: isod89/sloop-fm1 v2.4, 8d3823f, tools/gen_fm6_patches.py, GPL-3.0-only). This tool: ours.
"""SLOOP 2.4's FM6 factory patches F1..F8 -> firmware/src/storage/sl24/sl24_fm6.h (FELUCCA_SL24_IMPORT / EXPORT).

  tools/gen_sl24_fm6.py [OUT.h]        (default: firmware/src/storage/sl24/sl24_fm6.h)
  tools/gen_sl24_fm6.py --js           the same records as JS arrays (web/editor.html, the mock device)

A SLOOP 2.4 project keeps an FM6 part's PTCH (F1..F8 these patches, B1..B27 its bank), not the voice: the importer puts
the patch itself into our project (project_t.fm6, a DX7 packed voice), so the part sounds as it did in 2.4; the exporter
names a voice that is one of these exactly by its PTCH. The values are 2.4's, written again here as readable operator
settings (no factory ROM data of any instrument), packed in the generic 128-byte 6-operator record (OP6 first), which is
our eng_fm6.c fm6_pack / fm6_unpack layout too.
"""
import sys
from pathlib import Path


def op(r=(99, 99, 99, 99), l=(99, 99, 99, 0), ol=0, fc=1, ff=0, det=7, mode=0, kvs=0, ams=0, rs=0,
       bp=39, ld=0, rd=0, lc=0, rc=0):
    return dict(r=r, l=l, ol=ol, fc=fc, ff=ff, det=det, mode=mode, kvs=kvs, ams=ams, rs=rs, bp=bp, ld=ld, rd=rd,
                lc=lc, rc=rc)


def voice(name, alg, ops, fb=0, oks=1, pr=(99, 99, 99, 99), pl=(50, 50, 50, 50), lfs=35, lfd=0, lpmd=0, lamd=0,
          lks=1, lfw=0, lpms=3, trnsp=24):
    """OP1..OP6 -> the 155-byte single-voice layout (OP6 first); alg 1..32"""
    assert len(ops) == 6 and 1 <= alg <= 32 and len(name) <= 10
    v = []
    for o in reversed(ops):
        v += list(o["r"]) + list(o["l"]) + [o["bp"], o["ld"], o["rd"], o["lc"], o["rc"], o["rs"], o["ams"],
                                            o["kvs"], o["ol"], o["mode"], o["fc"], o["ff"], o["det"]]
    v += list(pr) + list(pl) + [alg - 1, fb, oks, lfs, lfd, lpmd, lamd, lks, lfw, lpms, trnsp]
    v += [ord(c) for c in name.ljust(10)]
    assert len(v) == 155
    return v


def pack(v):
    """155 -> the 128-byte packed record (eng_fm6.c fm6_pack)"""
    b = []
    for k in range(6):
        o = v[k * 21:k * 21 + 21]
        b += o[0:11]
        b += [(o[11] & 3) | (o[12] & 3) << 2, (o[13] & 7) | (o[20] & 15) << 3, (o[14] & 3) | (o[15] & 7) << 2,
              o[16], (o[17] & 1) | (o[18] & 31) << 1, o[19]]
    b += v[126:135]
    b += [(v[135] & 7) | (v[136] & 1) << 3]
    b += v[137:141]
    b += [(v[141] & 1) | (v[142] & 7) << 1 | (v[143] & 7) << 4, v[144]]
    b += v[145:155]
    assert len(b) == 128 and all(0 <= x < 128 for x in b)
    return b


# 2.4's INIT (an empty bank slot plays it), then F1..F8
INIT = voice("INIT VOICE", 1, [op(ol=99)] + [op() for _ in range(5)], lpms=3)
PATCHES = [
    voice("TINE EP", 5, [
        op(r=(96, 25, 25, 67), l=(99, 75, 0, 0), ol=98, kvs=2, rs=3),
        op(r=(95, 50, 35, 78), l=(99, 75, 0, 0), ol=58, kvs=7, rs=3),
        op(r=(95, 20, 20, 50), l=(99, 95, 0, 0), ol=90, kvs=2, det=8, rs=2),
        op(r=(97, 62, 40, 60), l=(99, 60, 0, 0), ol=68, fc=14, kvs=6, rs=3),
        op(r=(95, 30, 20, 60), l=(99, 90, 0, 0), ol=78, det=6, kvs=1),
        op(r=(95, 40, 30, 60), l=(99, 80, 0, 0), ol=52, kvs=3)],
        fb=3, lfs=34, lfd=33, lfw=4, lpms=2),
    voice("GLASS BELL", 5, [
        op(r=(99, 40, 25, 35), l=(99, 80, 0, 0), ol=96, kvs=1),
        op(r=(99, 35, 25, 35), l=(99, 70, 0, 0), ol=78, fc=3, ff=50, kvs=3),
        op(r=(99, 45, 30, 40), l=(99, 70, 0, 0), ol=84, fc=2, ff=38, det=9, kvs=1),
        op(r=(99, 50, 30, 40), l=(99, 60, 0, 0), ol=70, fc=5, kvs=3),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=78, fc=5, ff=40, det=5),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=60)],
        fb=0, lpms=0),
    voice("ROUND BASS", 5, [
        op(r=(99, 40, 30, 75), l=(99, 85, 70, 0), ol=99, kvs=2),
        op(r=(99, 60, 40, 75), l=(99, 72, 55, 0), ol=80, kvs=5),
        op(r=(99, 55, 40, 75), l=(99, 70, 50, 0), ol=80, det=8, kvs=1),
        op(r=(99, 72, 50, 75), l=(99, 65, 40, 0), ol=78, fc=2, kvs=6),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0),
    voice("BRASS SECT", 5, [
        op(r=(65, 50, 40, 60), l=(99, 92, 90, 0), ol=98, kvs=1),
        op(r=(55, 50, 40, 60), l=(99, 88, 85, 0), ol=80, kvs=3),
        op(r=(62, 50, 40, 60), l=(99, 92, 90, 0), ol=92, det=9, kvs=1),
        op(r=(50, 50, 40, 60), l=(99, 88, 85, 0), ol=78, kvs=3),
        op(r=(62, 50, 40, 60), l=(99, 92, 90, 0), ol=88, det=5, kvs=1),
        op(r=(52, 50, 40, 60), l=(99, 85, 82, 0), ol=72, kvs=2)],
        fb=5, pr=(80, 60, 99, 60), pl=(46, 50, 50, 50), lfs=33, lfd=50, lpmd=6, lfw=4, lpms=3),
    voice("SOFT PAD", 5, [
        op(r=(40, 30, 40, 40), l=(99, 95, 90, 0), ol=92, det=5),
        op(r=(35, 30, 40, 40), l=(85, 90, 80, 0), ol=60, kvs=1),
        op(r=(40, 30, 40, 40), l=(99, 95, 90, 0), ol=92, det=10),
        op(r=(30, 25, 40, 40), l=(80, 90, 85, 0), ol=55, fc=2, kvs=1),
        op(r=(38, 30, 40, 40), l=(99, 95, 90, 0), ol=80, fc=2),
        op(r=(35, 30, 40, 40), l=(85, 90, 85, 0), ol=50)],
        fb=2, lfs=30, lfd=60, lpmd=4, lfw=4, lpms=2),
    voice("WOOD BARS", 5, [
        op(r=(99, 52, 40, 55), l=(99, 0, 0, 0), ol=99, kvs=2, rs=2),
        op(r=(99, 75, 50, 60), l=(99, 0, 0, 0), ol=70, fc=4, kvs=5, rs=3),
        op(r=(99, 70, 50, 60), l=(99, 0, 0, 0), ol=72, fc=4, kvs=3, rs=3),
        op(r=(99, 80, 50, 60), l=(99, 0, 0, 0), ol=45, kvs=3, rs=3),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0),
    voice("DRAWBARS", 32, [
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=90),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=84, fc=0),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=86, fc=2),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=76, fc=3),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=78, fc=4),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=72, fc=1, ff=50)],
        fb=0, lfs=60, lpmd=3, lfw=4, lpms=3),
    voice("NYLON PICK", 5, [
        op(r=(99, 45, 30, 60), l=(99, 60, 0, 0), ol=98, kvs=2, rs=2),
        op(r=(99, 70, 40, 60), l=(99, 40, 0, 0), ol=76, fc=3, kvs=4, rs=2),
        op(r=(99, 60, 40, 60), l=(99, 30, 0, 0), ol=74, fc=2, det=8, kvs=2, rs=2),
        op(r=(99, 75, 40, 60), l=(99, 30, 0, 0), ol=68, kvs=3, rs=2),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0),
]


def rows(b):
    return ",\n".join("    " + ", ".join(f"{x:3d}" for x in b[i:i + 16]) for i in range(0, len(b), 16))


def header():
    L = ["/* SPDX-License-Identifier: GPL-3.0-only */",
         "/* Generated by tools/gen_sl24_fm6.py: SLOOP 2.4's FM6 patches (isod89/sloop-fm1 v2.4, 8d3823f; Felucca 1.0's own,",
         " * Copyright (C) 2026 Leo Kuroshita, Hugelton Instruments; GPL-3.0-only), DX7 packed (128 bytes, OP6 first): its",
         " * INIT (an empty bank slot) and F1..F8 (TINE EP, GLASS BELL, ROUND BASS, BRASS SECT, SOFT PAD, WOOD BARS, DRAWBARS,",
         " * NYLON PICK). Included by sl24_import.c. Do not edit: edit the tool. */",
         "static const uint8_t SL24_FM6_INIT[128] = {", rows(pack(INIT)), "};",
         f"static const uint8_t SL24_FM6_F[{len(PATCHES)}][128] = {{"]
    for v in PATCHES:
        L += ["    {", rows(pack(v)) + "},"]
    L += ["};", ""]
    return "\n".join(L)


def js():
    a = lambda b: "[" + ",".join(str(x) for x in b) + "]"
    print("const SL24_FM6_INIT = " + a(pack(INIT)) + ";")
    print("const SL24_FM6_F = [" + ",\n  ".join(a(pack(v)) for v in PATCHES) + "];")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--js":
        js()
    else:
        out = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent /
                   "firmware/src/storage/sl24/sl24_fm6.h")
        out.write_text(header())
        print(f"sl24 fm6 patches: {len(PATCHES)} -> {out}")

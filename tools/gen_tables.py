#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Fixed-point lookup tables for FELUCCA (no float on the target).

FS = 44100 Hz, control rate = every CTL (32) samples. All curves map a
0..127 parameter value to the unit the DSP needs, plus a display table.
"""
import math
import re
import struct
import sys
from pathlib import Path

FS = 44100
CTL = 32
PITCH_TOP = 2048 - 192   # pitch_inc: PITCH_INC_OCT's first pitch (MIDI 116, in 1/16 semitones)


def arr(name, ctype, vals, per=12, ram=False):
    """ram: TAB_RAM, the table lives in SRAM on the target (.data, or the pool with TAB_IN_POOL; the sample loops)"""
    out = [f"static const {ctype} {name}[{len(vals)}]{' TAB_RAM' if ram else ''} = {{"]
    for i in range(0, len(vals), per):
        out.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    out.append("};")
    return out


def f32(x):
    """x rounded to an IEEE single, as C stores a float"""
    return struct.unpack("f", struct.pack("f", x))[0]


# ---- the smooth tables delta-coded (TAB_CODED): on the target each lives in RAM (the pool, or main RAM: TAB_POOL), filled at boot by
# tab_expand (emitted below, called from main.c) from a flash stream of the differences between consecutive entries
# (order 1) or of the differences of those (order 2), zigzag LEB128 (1 byte for -64..63); SINE as its first quarter
# (257 entries) and its symmetries. Every other build (the host tests, the simulator) keeps the plain arrays.
CODED = []              # (group, name, ctype, n, flags, stream, values)
# the groups and the C condition each is built under: "core" SINE and TANH, in the pool at HEAD already (TAB_RAM), so
# coded at no cost in RAM; "flash" and "fm6" the tables that were in flash: coded only with FELUCCA_TAB_CODED_ALL (the
# builder's TAB_CODED_ALL: 1 the "flash" group, 2 the "fm6" group too, off by default; each then takes its size in the pool, TAB_CPOOL whether or not TAB_IN_POOL,
# which the undo history ring would have had: pool +7.3 KB; measured 2026-10-23 on mots.config: flash -3,2xx B, see
# tools/builder/costs.json)
GROUPS = {"core": None, "flash": "FELUCCA_TAB_CODED_ALL", "fm6": "FELUCCA_ENG_FM6 && FELUCCA_TAB_CODED_ALL >= 2",
          "plaits": "FELUCCA_ENG_PLAITS && FELUCCA_TAB_CODED_ALL",   # (1: the small tables, PLAITS' waveshapers and JUNO's
          "juno": "FELUCCA_ENG_JUNO && FELUCCA_TAB_CODED_ALL",       #  tables when built, the master compressor's and XDLY's
          "mcomp": "FELUCCA_MASTER_COMP && FELUCCA_TAB_CODED_ALL",   #  curves; 2: FM6's too; tools/build.py joins the built
          "xdly": "FELUCCA_INS_XDLY && FELUCCA_TAB_CODED_ALL"}       #  groups' blobs in this order)
CODED_W = {"uint8_t": 1, "int16_t": 2, "uint16_t": 2, "int32_t": 4, "uint32_t": 4, "int64_t": 8}
F_ORDER2 = 2
F_SINE = 4


def zigzag_leb(vals, order):
    """the stream of vals: each value's difference from the prediction (the last value, or twice the last less the
    one before), zigzag, 7 bits a byte, low first, the high bit on every byte but the last"""
    out = bytearray()
    p1 = p2 = 0
    for v in vals:
        d = v - (2 * p1 - p2 if order == 2 else p1)
        assert -(1 << 63) <= d < (1 << 63), (d, order)
        z = (d << 1) ^ (-1 if d < 0 else 0)
        while True:
            b, z = z & 0x7F, z >> 7
            out.append(b | (0x80 if z else 0))
            if not z:
                break
        p2, p1 = p1, v
    return bytes(out)


def unzigzag_leb(stream, n, order):
    """-> (the n values the C decoder recovers, the bytes used): the check that the stream round-trips"""
    out, i, p1, p2 = [], 0, 0, 0
    for _ in range(n):
        z, sh = 0, 0
        while True:
            b = stream[i]
            i += 1
            z |= (b & 0x7F) << sh
            sh += 7
            if not b & 0x80:
                break
        d = (z >> 1) ^ -(z & 1)
        v = d + (2 * p1 - p2 if order == 2 else p1)
        out.append(v)
        p2, p1 = p1, v
    return out, i


def c_table(text, name):
    """the values of the C array NAME[n] = {...}; in text (a table another file defines plain), checked against n"""
    m = re.search(r"\b" + name + r"\[(\d+)\] = \{(.*?)\};", text, re.S)
    assert m, name
    vals = [int(x) for x in re.findall(r"-?\d+", m.group(2))]
    assert len(vals) == int(m.group(1)), (name, len(vals), m.group(1))
    return vals


def carr(name, ctype, vals, per=12, ram=False, group="core", sine=False, dims=None, ext=False):
    """arr, with the table delta-coded on the target (TAB_CODED; sine: the first quarter coded, the rest its
    mirror). The coded tables' order within a group is the stream's. dims: the declaration's dimensions when the
    table is two-dimensional ("[5][257]": vals flat, row after row). ext: a table another header defines plain
    (its generator wraps that in #if !TAB_<name>_CODED): only the coded declaration here, and TAB_<name>_CODED 1
    when it is built coded, else 0"""
    store = vals[:257] if sine else vals
    if sine:
        assert len(vals) == 1024 and all(vals[512 - i] == vals[i] for i in range(1, 256)) \
            and all(vals[512 + i] == -vals[i] for i in range(512)), name
    order, stream = min(((o, zigzag_leb(store, o)) for o in (1, 2)), key=lambda t: len(t[1]))
    got, used = unzigzag_leb(stream, len(store), order)
    assert got == list(store) and used == len(stream), name
    lo, hi = {"uint8_t": (0, 255), "int16_t": (-32768, 32767), "uint16_t": (0, 65535), "int32_t": (-(1 << 31), (1 << 31) - 1),
              "uint32_t": (0, (1 << 32) - 1), "int64_t": (-(1 << 63), (1 << 63) - 1)}[ctype]
    assert all(lo <= v <= hi for v in vals), name
    CODED.append((group, name, ctype, len(vals), (F_ORDER2 if order == 2 else 0) | (F_SINE if sine else 0), stream,
                  list(vals)))
    cond = GROUPS[group]
    place = "TAB_POOL" if group == "core" else "TAB_CPOOL"
    decl = f"static {ctype} {name}{dims or f'[{len(vals)}]'} {place};"
    if ext:
        return [f"#if TAB_CODED{' && ' + cond if cond else ''}", decl, f"#define TAB_{name}_CODED 1", "#else",
                f"#define TAB_{name}_CODED 0", "#endif"]
    return [f"#if TAB_CODED{' && ' + cond if cond else ''}", decl, "#else", *arr(name, ctype, vals, per, ram), "#endif"]


def coded_tables():
    """the streams, the descriptors and the decoder, for main.c (GROUPS: each under its condition)"""
    groups = list(GROUPS.items())
    L = ["", "/* TAB_CODED (tools/gen_tables.py carr): the tables above that TAB_POOL marks, delta-coded in flash and",
         " * expanded into RAM at boot (tab_expand, from main.c before any interrupt; the pool or main RAM: TAB_POOL) */",
         "#if TAB_CODED",
         "typedef struct { void *dst; uint16_t n; uint8_t w; uint8_t f; } tab_desc_t;   /* w 2 4 8; f: 2 order 2, 4 SINE's quarter */"]
    # the decoder's arithmetic: 64-bit only when a built group has a 64-bit table (TAB_WIDE), else 32-bit (smaller)
    wide = [cond for g, cond in groups if any(c[0] == g and CODED_W[c[2]] == 8 for c in CODED)]
    if wide:
        L += [f"#if {' || '.join(f'({c})' for c in wide) if all(wide) else '1'}", "#define TAB_WIDE 1",
              "typedef int64_t tab_v_t;", "typedef uint64_t tab_z_t;", "#else", "#define TAB_WIDE 0",
              "typedef int32_t tab_v_t;", "typedef uint32_t tab_z_t;", "#endif"]
    else:
        L += ["#define TAB_WIDE 0", "typedef int32_t tab_v_t;", "typedef uint32_t tab_z_t;"]
    # a byte table (JUNO's cutoff steps): the decoder's byte store only when a built group has one (TAB_BYTE)
    byte = [cond for g, cond in groups if any(c[0] == g and CODED_W[c[2]] == 1 for c in CODED)]
    L += [f"#if {' || '.join(f'({c})' for c in byte) if byte and all(byte) else ('1' if byte else '0')}",
          "#define TAB_BYTE 1", "#else", "#define TAB_BYTE 0", "#endif"]
    # the streams: on the target they travel in the boot stream (TAB_STREAM: build.py appends the built groups' blobs,
    # in this order, as a block of the LZB stream; main.c decodes it onto the stack and calls tab_expand(s)); a host
    # test of the decoder (TAB_CODED_FORCE) keeps them as arrays. TAB_BLOB_<g>_IN: the group's length when built, else 0
    L += ["#if defined(__PI32V2__) && !defined(TAB_CODED_FORCE)",
          "#define TAB_STREAM 1                            /* the blobs come decoded from the boot stream (build.py tab_blob.bin) */",
          "#else", "#define TAB_STREAM 0", "#endif"]
    blobs = {}
    for g, cond in groups:
        rows = [c for c in CODED if c[0] == g]
        if not rows:
            continue
        stream = b"".join(c[5] for c in rows)
        blobs[g] = stream
        if cond:
            L.append(f"#if {cond}")
        L += [f"#define TAB_BLOB_{g}_LEN {len(stream)}u", "#if !TAB_STREAM",
              f"static const uint8_t TAB_BLOB_{g}[{len(stream)}] = {{"]
        for i in range(0, len(stream), 24):
            L.append("    " + ", ".join(str(b) for b in stream[i:i + 24]) + ",")
        L += ["};", "#endif", f"static const tab_desc_t TAB_DESC_{g}[{len(rows)}] = {{"]
        L += [f"    {{{c[1]}, {c[3]}, {CODED_W[c[2]]}, {c[4]}}}," for c in rows]
        L += ["};", f"#define TAB_BLOB_{g}_IN TAB_BLOB_{g}_LEN"]
        if cond:
            L += ["#else", f"#define TAB_BLOB_{g}_IN 0u", "#endif"]
    L += ["#define TAB_BLOB_TOTAL (" + " + ".join(f"TAB_BLOB_{g}_IN" for g in blobs) + ")",
          "#if TAB_STREAM && defined(TAB_STREAM_LEN)         /* (build.py: the block it put in the boot stream) */",
          "_Static_assert(TAB_STREAM_LEN == TAB_BLOB_TOTAL, \"tools/build.py's tab blob is not this build's set of coded tables\");",
          "#endif"]
    L += ["static void tab_expand_group(const tab_desc_t *t, uint32_t cnt, const uint8_t *s)",
          "{",
          "    for (; cnt; cnt--, t++) {",
          "        tab_v_t v, p1 = 0, p2 = 0;",
          "        uint32_t i, n = t->f & 4u ? 257u : t->n;",
          "        for (i = 0; i < n; i++) {",
          "            tab_z_t z = 0;",
          "            uint32_t sh = 0, b;",
          "            do {",
          "                b = *s++;",
          "                z |= (tab_z_t)(b & 0x7Fu) << sh;",
          "                sh += 7u;",
          "            } while (b & 0x80u);",
          "            v = (tab_v_t)(z >> 1) ^ -(tab_v_t)(z & 1u);",
          "            v += t->f & 2u ? 2 * p1 - p2 : p1;",
          "            p2 = p1;",
          "            p1 = v;",
          "            if (t->w == 2u)",
          "                ((int16_t *)t->dst)[i] = (int16_t)v;",
          "#if TAB_WIDE",
          "            else if (t->w == 8u)",
          "                ((int64_t *)t->dst)[i] = v;",
          "#endif",
          "#if TAB_BYTE",
          "            else if (t->w == 1u)",
          "                ((uint8_t *)t->dst)[i] = (uint8_t)v;",
          "#endif",
          "            else",
          "                ((int32_t *)t->dst)[i] = (int32_t)v;",
          "        }",
          "        if (t->f & 4u) {                         /* SINE: a quarter decoded, the rest its mirror */",
          "            int16_t *q = (int16_t *)t->dst;",
          "            for (i = 1; i < 256u; i++)",
          "                q[512u - i] = q[i];",
          "            for (i = 0; i < 512u; i++)",
          "                q[512u + i] = (int16_t)-q[i];",
          "        }",
          "    }",
          "}",
          "#if TAB_STREAM",
          "static void tab_expand(const uint8_t *s)      /* s: the groups' blobs decoded from the boot stream, TAB_BLOB_TOTAL B */",
          "{"]
    for g, cond in groups:
        rows = [c for c in CODED if c[0] == g]
        if rows:
            L += ([f"#if {cond}"] if cond else []) + \
                [f"    tab_expand_group(TAB_DESC_{g}, {len(rows)}u, s);", f"    s += TAB_BLOB_{g}_LEN;"] + \
                (["#endif"] if cond else [])
    L += ["    (void)s;", "}", "#else", "static void tab_expand(void)", "{"]
    for g, cond in groups:
        rows = [c for c in CODED if c[0] == g]
        if rows:
            L += ([f"#if {cond}"] if cond else []) + [f"    tab_expand_group(TAB_DESC_{g}, {len(rows)}u, TAB_BLOB_{g});"] \
                + (["#endif"] if cond else [])
    L += ["}", "#endif", "#ifdef TAB_CODED_CHECK                    /* (tests/tab_coded_test.c: the plain arrays beside the coded) */"]
    for g, cond in groups:
        rows = [c for c in CODED if c[0] == g]
        if not rows:
            continue
        L += [f"#if {cond}"] if cond else []
        for c in rows:
            L += arr(c[1] + "_RAW", c[2], c[6], 12)
        L += [f"#define TAB_CHECK_{g}(X) " + " ".join(f"X({c[1]})" for c in rows)]
        L += ["#else", f"#define TAB_CHECK_{g}(X)", "#endif"] if cond else []
    L += ["#endif", "#endif"]
    return L, blobs


def fm6_tables():
    """FM6 (eng_fm6.c): Dexed's lookup tables, computed as Dexed computes them at 44.1 kHz with its
    64-sample blocks (MSFA sin.cc, exp2.cc, freqlut.cc, lfo.cc, pitchenv.cc, porta.cpp, dx7note.cc,
    EngineMkI.cpp; Apache License 2.0 / GPL-3.0-or-later, see LICENSING.md), so FM6 renders the same
    samples. Logs in Q24 (1 << 24 = one octave)."""
    N = 64
    L = [f"#define FM6_N {N}", "#define FM6_LG_N 6"]
    ref = []    # the full tables as Dexed has them: host tests only (FM6_REF_TABLES); SLOOP builds the
    # per-sample ones in RAM at boot (eng_fm6.c fm6_tables_init) and figures 2^x / the frequency from
    # FM6_P2A x FM6_P2B, equal to every entry (tests/fm6_tables_test.c): the flash they took (~16 KB) is free
    # sine, Q24 out of a Q24 phase: 1024 points by rotation (sin.cc), the next point for interpolation
    c = math.floor(math.cos(2 * math.pi / 1024) * (1 << 30) + 0.5)
    s = math.floor(math.sin(2 * math.pi / 1024) * (1 << 30) + 0.5)
    u, v, sin_t = 1 << 30, 0, [0] * 1025
    for i in range(512):
        sin_t[i] = (v + 32) >> 6
        sin_t[i + 512] = -((v + 32) >> 6)
        u, v = (u * c - v * s + (1 << 29)) >> 30, (u * s + v * c + (1 << 29)) >> 30
    ref += arr("FM6_SIN_REF", "int32_t", sin_t, 8)
    L += [f"#define FM6_SIN_C {c}", f"#define FM6_SIN_S {s}"]   # the rotation, as sin.cc
    # 2^x: 1024 points of one octave in Q30 (exp2.cc), then 2^31
    y, inc, e = float(1 << 30), math.exp2(1.0 / 1024), []
    for i in range(1024):
        e.append(math.floor(y + 0.5))
        y *= inc
    ref += arr("FM6_EXP2_REF", "uint32_t", e + [1 << 31], 8)
    # 2^(i / 1024) exactly enough (Q62, rounded) as 2^(a / 32) x 2^(b / 1024), i = 32 a + b: every entry of
    # FM6_EXP2 and FM6_FREQ (and MARK I's exponent) is this rounded (the iterated doubles never differ)
    import decimal
    decimal.getcontext().prec = 80
    D = decimal.Decimal
    def q62(x):
        return int((x * (D(2) ** 62) + D("0.5")).to_integral_value(rounding=decimal.ROUND_FLOOR))
    L += arr("FM6_P2A", "uint64_t", [str(q62(D(2) ** (D(a) / D(32)))) + "ull" for a in range(32)], 4)
    L += arr("FM6_P2B", "uint64_t", [str(q62(D(2) ** (D(b) / D(1024)))) + "ull" for b in range(32)], 4)
    L += [f"#define FM6_FREQ_M {(1 << 95) // ((1 << 16) * FS)}ull   /* 2^95 / (2^16 FS): FM6_FREQ = 2^(i / 1024) 2^44 / FS */"]
    # phase increment of a log frequency (freqlut.cc): Q24 phase, 2^20 Hz at the top of the table
    y, inc, fl = float(1 << 44) / FS, math.pow(2, 1.0 / 1024), []
    for i in range(1025):
        fl.append(math.floor(y + 0.5))
        y *= inc
    ref += arr("FM6_FREQ_REF", "int32_t", fl, 8)
    # frequency: coarse ratios, fine (dx7note.cc osc_freq), detune per note x (DET - 7) in Q24 fractions
    L += arr("FM6_COARSE", "int32_t", [-(1 << 24)] + [int(round((1 << 24) * math.log2(k))) for k in range(1, 32)], 8)
    L += carr("FM6_FINE", "int32_t", [math.floor(24204406.323123 * math.log(1 + 0.01 * f) + 0.5) for f in range(128)], 8, group="fm6")
    det = []
    for n in range(128):
        lf = 50857777 + ((1 << 24) // 12) * n                       # Dexed's standard tuning
        r = 0.0209 * math.exp(-0.396 * (f32(float(lf)) / (1 << 24))) / 7
        det.append(round(r * lf * (1 << 24)))
        for d in range(16):                                          # the table gives Dexed's result
            want = int(lf + r * lf * (d - 7)) - lf
            assert lf + ((det[-1] * (d - 7)) >> 24) - lf == want, (n, d)
    L += carr("FM6_DETUNE", "int64_t", det, 6, group="fm6")
    # LFO speeds (lfo.cc): Hz measured on a DX7 (MSFA, Copyright 2012 Google Inc.), as Dexed's phase
    # increments per 64-sample block
    hz = [0.062541, 0.125031, 0.312393, 0.437120, 0.624610, 0.750694, 0.936330, 1.125302, 1.249609, 1.436782,
          1.560915, 1.752081, 1.875117, 2.062494, 2.247191, 2.374451, 2.560492, 2.686728, 2.873976, 2.998950,
          3.188013, 3.369840, 3.500175, 3.682224, 3.812065, 4.000800, 4.186202, 4.310716, 4.501260, 4.623209,
          4.814636, 4.930480, 5.121901, 5.315191, 5.434783, 5.617346, 5.750431, 5.946717, 6.062811, 6.248438,
          6.431695, 6.564264, 6.749460, 6.868132, 7.052186, 7.250580, 7.375719, 7.556294, 7.687577, 7.877738,
          7.993605, 8.181967, 8.372405, 8.504848, 8.685079, 8.810573, 8.986341, 9.122423, 9.300595, 9.500285,
          9.607994, 9.798158, 9.950249, 10.117361, 11.251125, 11.384335, 12.562814, 13.676149, 13.904338,
          15.092062, 16.366612, 16.638935, 17.869907, 19.193858, 19.425019, 20.833333, 21.034918, 22.502250,
          24.003841, 24.260068, 25.746653, 27.173913, 27.578599, 29.052876, 30.693677, 31.191516, 32.658393,
          34.317090, 34.674064, 36.416606, 38.197097, 38.550501, 40.387722, 40.749796, 42.625746, 44.326241,
          44.883303, 46.772685, 48.590865, 49.261084]
    assert len(hz) == 100
    ratio = int(4437500000.0 * N / FS)
    L += carr("FM6_LFO_INC", "uint32_t", [int(h * ratio) for h in hz], 8, group="fm6")
    L += [f"#define FM6_LFO_UNIT {int(N * 25190424 / FS + 0.5)}",
          f"#define FM6_PEG_UNIT {int(N * (1 << 24) / (21.3 * FS) + 0.5)}"]
    # portamento (porta.cpp): pitch steps per block, plain and glissando, for CC 5 = 0..127
    def porta(sps):
        return [int(0.5 + ((1 << 24) // 12) * (sps * math.pow(2.0, -0.062 * i) / FS * N)) for i in range(128)]
    L += carr("FM6_PORTA", "int32_t", porta(2100.0), 8, group="fm6")
    L += carr("FM6_GLISS", "int32_t", porta(1300.0), 8, group="fm6")
    return L + ["#ifdef FM6_REF_TABLES"] + ref + ["#endif"]


def main(path):
    L = ["/* generated by tools/gen_tables.py */", "#pragma once", "#include <stdint.h>",
         f"#define FS {FS}", f"#define CTL {CTL}",
         "/* TAB_RAM: a table the sample loops read, in SRAM on the target, copied from flash at boot (main.c): main",
         " * RAM's .data, or with FELUCCA_TAB_IN_POOL (EXPERIMENTAL) the pool's .pool_init (main RAM is the scarcer;",
         " * whether the FM-1 reads both at one speed is not measured) */",
         "#if defined(__PI32V2__) && defined(FELUCCA_TAB_IN_POOL) && FELUCCA_TAB_IN_POOL",
         '#define TAB_RAM __attribute__((section(".pool_init.tab_ram")))',
         "#elif defined(__PI32V2__)", '#define TAB_RAM __attribute__((section(".data.tab_ram")))',
         "#else", "#define TAB_RAM", "#endif",
         "/* TAB_CODED: the smooth tables (carr below) delta-coded in flash and expanded into the pool at boot (tab_expand",
         " * at the end of this file, main.c); the target only (TAB_CODED_FORCE: a host test of the decoder) */",
         "#if defined(__PI32V2__) || defined(TAB_CODED_FORCE)", "#define TAB_CODED 1", "#else", "#define TAB_CODED 0", "#endif",
         "/* FELUCCA_TAB_CODED_ALL (the builder's TAB_CODED_ALL): the 16 tables that are in flash otherwise coded too, each",
         " * expanded into the pool (TAB_CPOOL, whether or not TAB_IN_POOL: main RAM pays nothing, the undo ring is 7.3 KB",
         " * shorter); off: SINE and TANH_Q15 only, which RAM holds either way */",
         "#ifndef FELUCCA_TAB_CODED_ALL", "#define FELUCCA_TAB_CODED_ALL 0", "#endif",
         "/* where a core coded table is expanded: the pool with FELUCCA_TAB_IN_POOL (as TAB_RAM), else main RAM (.bss; TAB_RAM's",
         " * .data copies were there: the same RAM, the pool untouched); a TAB_CODED_ALL table always the pool */",
         "#if defined(__PI32V2__) && defined(FELUCCA_TAB_IN_POOL) && FELUCCA_TAB_IN_POOL",
         '#define TAB_POOL __attribute__((section(".pool.tab")))', "#else", "#define TAB_POOL", "#endif",
         "#if defined(__PI32V2__)", '#define TAB_CPOOL __attribute__((section(".pool.tabc")))', "#else", "#define TAB_CPOOL",
         "#endif",
         "/* TAB_RAW_POOL: a const table that TAB_CODED_ALL moves from flash to the pool as it is (the small font's bitmaps,",
         " * gen_font.py FONT_S_DATA: no smooth curve for the delta codes): its bytes travel in the boot stream (app.ld .pool_init,",
         " * coded there as .data is: LZ and the range coder) and are copied into the pool at boot, the same values read from RAM;",
         " * off, and on the host, the table stays where it was (round 9, 2026-10-24) */",
         "#if defined(__PI32V2__) && FELUCCA_TAB_CODED_ALL", '#define TAB_RAW_POOL __attribute__((section(".pool_init.raw")))',
         "#else", "#define TAB_RAW_POOL", "#endif", ""]
    L += carr("SINE", "int16_t", [int(round(32767 * math.sin(2 * math.pi * i / 1024))) for i in range(1024)], ram=True,
              sine=True)
    # phase increment for pitch in 1/16 semitone, MIDI 0..127: the top octave (MIDI 116..127.94) and a shift per octave
    # below (pitch_inc). The increments are truncated, so a shift gives each lower one exactly (checked here against
    # the 2,048-entry table this replaced: 8 KB less flash)
    def inc_of(p):
        f = 440.0 * 2 ** ((p / 16 - 69) / 12)
        return min(int(f / FS * 2 ** 32), 0x7FFFFFFF)
    top = [inc_of(PITCH_TOP + k) for k in range(192)]
    for p in range(128 * 16):
        q = p + 64
        assert top[q % 192] >> (10 - q // 192) == inc_of(p), p
    L += carr("PITCH_INC_OCT", "uint32_t", top, 8, group="flash")
    L += ["/* the increment of pitch p (1/16 semitones, 0..2047): the octave's entry shifted down; q / 192 as",
          " * (q / 64) * 171 / 512 (exact for q < 2112; no divide) */",
          "#if defined(__GNUC__) || defined(__clang__)",
          "__attribute__((always_inline))   /* (RAM code calls it: never an XIP copy) */",
          "#endif",
          "static inline uint32_t pitch_inc(int32_t p)",
          "{",
          "    uint32_t q = (uint32_t)p + 64u, o = ((q >> 6) * 171u) >> 9;",
          "    return PITCH_INC_OCT[q - o * 192u] >> (10u - o);",
          "}"]
    # times 1 ms .. 10 s (exponential), env in Q24 (1.0 = 1<<24)
    ms = [1.0 * (10000 ** (v / 127)) for v in range(128)]
    L += carr("TIME_MS_X10", "uint32_t", [int(round(m * 10)) for m in ms], 10, group="flash")
    L += carr("ENV_LIN", "uint32_t", [max(1, int((1 << 24) * CTL / (m * FS / 1000))) for m in ms], 8, group="flash")
    exp_k = []
    for m in ms:
        tau = m / 1000 / 4.6                       # ~99 % of the way after "m" ms
        exp_k.append(max(1, min(65535, int(65536 * (1 - math.exp(-CTL / (tau * FS)))))))
    L += carr("ENV_EXP", "uint16_t", exp_k, group="flash")
    # LFO 0.05 .. 40 Hz, phase increment per control tick
    hz = [0.05 * (800 ** (v / 127)) for v in range(128)]
    L += carr("LFO_HZ_X100", "uint32_t", [int(round(h * 100)) for h in hz], 10, group="flash")
    L += carr("LFO_INC", "uint32_t", [int(h * CTL / FS * 2 ** 32) for h in hz], 8, group="flash")
    # filter cutoff 30 Hz .. 16 kHz
    fc = [30.0 * ((16000 / 30) ** (v / 127)) for v in range(128)]
    L += carr("CUTOFF_HZ", "uint16_t", [int(round(f)) for f in fc], group="flash")
    # trapezoidal (Simper) SVF: g = tan(pi fc / FS), Q12; stable at any cutoff/resonance
    L += carr("SVF_G", "uint16_t", [int(4096 * math.tan(math.pi * min(f, 0.45 * FS) / FS)) for f in fc], group="flash")
    # level: 0 = off, else dB = (v - 112) / 2  (112 = 0 dB, 127 = +7.5 dB)
    db = [None] + [(v - 112) / 2 for v in range(1, 128)]
    L += carr("LEVEL_Q12", "uint16_t", [0] + [int(round(4096 * 10 ** (d / 20))) for d in db[1:]], group="flash")
    L += carr("LEVEL_DB_X10", "int16_t", [-999] + [int(round(d * 10)) for d in db[1:]], group="flash")
    # per-sample exponential decay for drum voices: 5 ms .. 4 s to -60 dB, Q16
    dms = [5.0 * (800 ** (v / 127)) for v in range(128)]
    L += carr("DECAY_K", "uint16_t", [min(65535, int(65536 * math.exp(-6.9 / (m / 1000 * FS)))) for m in dms], group="flash")
    # soft clip curve for |x| in 0..2 (Q12 in, Q15 out): tanh
    L += carr("TANH_Q15", "int16_t", [int(32767 * math.tanh(i / 256 * 2)) for i in range(257)], ram=True)
    # PLAITS' five waveshapers (plaits_tables.h PL_WS, gen_plaits_tables.py: smooth curves, 2,570 B of flash): with
    # TAB_CODED_ALL they travel delta-coded in the boot stream and live in the pool (the "plaits" group, built with the
    # engine); plaits_tables.h keeps the plain table for every other build (#if !TAB_PL_WS_CODED). Measured 2026-10-24
    # on mots.config: -1,919 B of flash for 2,570 B of the pool (the folders PL_FOLD / PL_FOLD2 would give 361 B for 1,028)
    import gen_plaits_tables
    L += carr("PL_WS", "int16_t", [v for row in gen_plaits_tables.shapers() for v in row], group="plaits",
              dims="[5][257]", ext=True)
    # JUNO's cutoff curve (juno_tables.h JUNO_VCF_G1Q_D: the 1,025 byte steps of fm-106's VCF_G1 at every 4th point,
    # gen_juno_tables.py from a source not in this tree, so the values are read from the header as it is): the same way,
    # the "juno" group; -832 B of flash for 1,025 B of the pool on mots.config (2026-10-24)
    juno_h = (Path(__file__).resolve().parents[1] / "firmware/src/engines/juno/juno_tables.h").read_text()
    juno_d = [int(x) for x in re.findall(r"\d+", re.search(r"JUNO_VCF_G1Q_D\[1025\] = \{(.*?)\};", juno_h, re.S).group(1))]
    assert len(juno_d) == 1025, len(juno_d)
    L += carr("JUNO_VCF_G1Q_D", "uint8_t", juno_d, 16, group="juno", ext=True)
    # JUNO's small tables (the cutoff's and the VCA's every-16th values, the resonance and peak steps, the feedback
    # scale, the OTA's saturation, the sub and noise levels: 1,538 B of flash), the master compressor's 2^x curve
    # (master_comp.c MC_EXP2, 260 B) and XDLY's TIME curve (fx_slots.c XD_D, 256 B): the same way, each group built
    # with its feature and TAB_CODED_ALL, the values read from the source as it is (round 9, 2026-10-24: measured on
    # mots.config with the small font in the pool, see tools/builder/costs.json)
    for name, ctype in (("JUNO_VCF_G1Q_A", "uint16_t"), ("JUNO_RES_K_A", "uint16_t"), ("JUNO_RES_K_D", "uint8_t"),
                        ("JUNO_FB_SCALE_T", "uint16_t"), ("JUNO_PEAK_PULL_A", "uint16_t"), ("JUNO_PEAK_PULL_D", "uint8_t"),
                        ("JUNO_OTA_SAT2", "int16_t"), ("JUNO_SUB_LEVEL_J106", "uint16_t"), ("JUNO_NOISE_LEVEL_J106", "uint16_t"),
                        ("JUNO_VCA_HW_A", "uint16_t"), ("JUNO_VCA_HW_D", "uint8_t")):
        L += carr(name, ctype, c_table(juno_h, name), 16, group="juno", ext=True)
    src = Path(__file__).resolve().parents[1] / "firmware/src"
    L += carr("MC_EXP2", "uint32_t", c_table((src / "fx/master_comp/master_comp.c").read_text(), "MC_EXP2"), 16, group="mcomp",
              ext=True)
    L += carr("XD_D", "uint16_t", c_table((src / "fx/fx_slots.c").read_text(), "XD_D"), 16, group="xdly", ext=True)
    L += fm6_tables()
    coded, blobs = coded_tables()
    L += coded
    for g, b in blobs.items():      # (build.py: the built groups' blobs, in GROUPS' order, as a block of the boot stream)
        Path(path).with_name(f"tab_blob_{g}.bin").write_bytes(b)
    Path(path).write_text("\n".join(L) + "\n")
    print(f"tables: {path}")


if __name__ == "__main__":
    main(sys.argv[1])

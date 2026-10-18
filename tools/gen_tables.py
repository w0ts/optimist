#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Fixed-point lookup tables for FELUCCA (no float on the target).

FS = 44100 Hz, control rate = every CTL (32) samples. All curves map a
0..127 parameter value to the unit the DSP needs, plus a display table.
"""
import math
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
    L += arr("FM6_FINE", "int32_t", [math.floor(24204406.323123 * math.log(1 + 0.01 * f) + 0.5) for f in range(128)], 8)
    det = []
    for n in range(128):
        lf = 50857777 + ((1 << 24) // 12) * n                       # Dexed's standard tuning
        r = 0.0209 * math.exp(-0.396 * (f32(float(lf)) / (1 << 24))) / 7
        det.append(round(r * lf * (1 << 24)))
        for d in range(16):                                          # the table gives Dexed's result
            want = int(lf + r * lf * (d - 7)) - lf
            assert lf + ((det[-1] * (d - 7)) >> 24) - lf == want, (n, d)
    L += arr("FM6_DETUNE", "int64_t", det, 6)
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
    L += arr("FM6_LFO_INC", "uint32_t", [int(h * ratio) for h in hz], 8)
    L += [f"#define FM6_LFO_UNIT {int(N * 25190424 / FS + 0.5)}",
          f"#define FM6_PEG_UNIT {int(N * (1 << 24) / (21.3 * FS) + 0.5)}"]
    # portamento (porta.cpp): pitch steps per block, plain and glissando, for CC 5 = 0..127
    def porta(sps):
        return [int(0.5 + ((1 << 24) // 12) * (sps * math.pow(2.0, -0.062 * i) / FS * N)) for i in range(128)]
    L += arr("FM6_PORTA", "int32_t", porta(2100.0), 8)
    L += arr("FM6_GLISS", "int32_t", porta(1300.0), 8)
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
         "#else", "#define TAB_RAM", "#endif", ""]
    L += arr("SINE", "int16_t", [int(round(32767 * math.sin(2 * math.pi * i / 1024))) for i in range(1024)], ram=True)
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
    L += arr("PITCH_INC_OCT", "uint32_t", top, 8)
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
    L += arr("TIME_MS_X10", "uint32_t", [int(round(m * 10)) for m in ms], 10)
    L += arr("ENV_LIN", "uint32_t", [max(1, int((1 << 24) * CTL / (m * FS / 1000))) for m in ms], 8)
    exp_k = []
    for m in ms:
        tau = m / 1000 / 4.6                       # ~99 % of the way after "m" ms
        exp_k.append(max(1, min(65535, int(65536 * (1 - math.exp(-CTL / (tau * FS)))))))
    L += arr("ENV_EXP", "uint16_t", exp_k)
    # LFO 0.05 .. 40 Hz, phase increment per control tick
    hz = [0.05 * (800 ** (v / 127)) for v in range(128)]
    L += arr("LFO_HZ_X100", "uint32_t", [int(round(h * 100)) for h in hz], 10)
    L += arr("LFO_INC", "uint32_t", [int(h * CTL / FS * 2 ** 32) for h in hz], 8)
    # filter cutoff 30 Hz .. 16 kHz
    fc = [30.0 * ((16000 / 30) ** (v / 127)) for v in range(128)]
    L += arr("CUTOFF_HZ", "uint16_t", [int(round(f)) for f in fc])
    # trapezoidal (Simper) SVF: g = tan(pi fc / FS), Q12; stable at any cutoff/resonance
    L += arr("SVF_G", "uint16_t", [int(4096 * math.tan(math.pi * min(f, 0.45 * FS) / FS)) for f in fc])
    # level: 0 = off, else dB = (v - 112) / 2  (112 = 0 dB, 127 = +7.5 dB)
    db = [None] + [(v - 112) / 2 for v in range(1, 128)]
    L += arr("LEVEL_Q12", "uint16_t", [0] + [int(round(4096 * 10 ** (d / 20))) for d in db[1:]])
    L += arr("LEVEL_DB_X10", "int16_t", [-999] + [int(round(d * 10)) for d in db[1:]])
    # per-sample exponential decay for drum voices: 5 ms .. 4 s to -60 dB, Q16
    dms = [5.0 * (800 ** (v / 127)) for v in range(128)]
    L += arr("DECAY_K", "uint16_t", [min(65535, int(65536 * math.exp(-6.9 / (m / 1000 * FS)))) for m in dms])
    # soft clip curve for |x| in 0..2 (Q12 in, Q15 out): tanh
    L += arr("TANH_Q15", "int16_t", [int(32767 * math.tanh(i / 256 * 2)) for i in range(257)], ram=True)
    L += fm6_tables()
    Path(path).write_text("\n".join(L) + "\n")
    print(f"tables: {path}")


if __name__ == "__main__":
    main(sys.argv[1])

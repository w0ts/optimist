#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 3dSam (Clipper)
"""Clipper's factory wavetables (eng_wave.c): 16 tables of 16 frames, each frame stored as its spectrum, the
cosine and sine terms of harmonics 1..128 (Q15). The firmware builds band-limited copies of the selected table
from them (wt_service: one per octave, nothing above 20 kHz), so the tables never alias.

Each frame is defined as a spectrum where a formula gives one (classic waves, pulse widths, additive builds,
formants, resonances, combs), else as a waveform of 512 samples measured with a DFT (sync, FM, fold, phase
distortion, chip steps). Every frame is normalised to a peak of 0.9 (the highest of its band-limited copies: none clips),
so a sweep through a table keeps its level.

Pure Python (no numpy: it runs in the build on any machine); a cache next to the output skips the work when
this script has not changed.
  tools/gen_wavetables.py build/gen/felucca_wavetables.h
From Clipper (isod89/clipper-fm1 tools/gen_wavetables.py, e690672, GPL-3.0-only); Optimist builds 8 of its tables (OPTIMIST)."""
import hashlib
import math
import random
import sys
from pathlib import Path

NH = 128            # harmonics per frame
NF = 16             # frames per table
NW = 512            # waveform points (DFT, the peak measurement)
TWO_PI = 2 * math.pi

COS = [[math.cos(TWO_PI * h * n / NW) for n in range(NW)] for h in range(NH + 1)]
SIN = [[math.sin(TWO_PI * h * n / NW) for n in range(NW)] for h in range(NH + 1)]


def dft(w):
    """waveform (NW points, one cycle) -> [(a_h, b_h)] h = 1..NH: w = sum a cos + b sin"""
    out = []
    for h in range(1, NH + 1):
        c, s = COS[h], SIN[h]
        a = sum(w[n] * c[n] for n in range(NW)) * 2 / NW
        b = sum(w[n] * s[n] for n in range(NW)) * 2 / NW
        out.append((a, b))
    return out


def peak(spec):
    """the highest peak of the frame's band-limited copies (harmonics 1..128, 1..64, .. 1..2: the firmware's
    levels): the fewer harmonics, the bigger the overshoot can be (Gibbs), and no copy may clip"""
    m = 0.0
    for hmax in (128, 64, 32, 16, 8, 4, 2):
        for n in range(NW):
            v = 0.0
            for h, (a, b) in enumerate(spec[:hmax], 1):
                if a or b:
                    v += a * COS[h][n] + b * SIN[h][n]
            m = max(m, abs(v))
    return m


def sines(amps):
    """amplitudes of sine harmonics 1..NH (missing ones 0) -> spectrum"""
    amps = list(amps) + [0.0] * (NH - len(amps))
    return [(0.0, x) for x in amps[:NH]]


def saw_amp(h):
    return (2 / math.pi) / h * (-1) ** (h + 1)


def mix(s1, s2, t):
    return [(a1 + (a2 - a1) * t, b1 + (b2 - b1) * t) for (a1, b1), (a2, b2) in zip(s1, s2)]


# ------------------------------------------------------------------------------------------------- the tables ---
def t_basic():
    """sine -> triangle -> saw -> square -> narrow pulse"""
    sine = sines([1.0])
    tri = sines([(8 / math.pi ** 2) * ((-1) ** ((h - 1) // 2)) / h ** 2 if h % 2 else 0 for h in range(1, NH + 1)])
    saw = sines([saw_amp(h) for h in range(1, NH + 1)])
    sq = sines([(4 / math.pi) / h if h % 2 else 0 for h in range(1, NH + 1)])
    pul = t_pulse_frame(0.1)
    keys = [sine, tri, saw, sq, pul]
    frames = []
    for f in range(NF):
        x = f / (NF - 1) * (len(keys) - 1)
        i = min(int(x), len(keys) - 2)
        frames.append(mix(keys[i], keys[i + 1], x - i))
    return frames


def t_pulse_frame(w):
    return [(2 / (math.pi * h) * math.sin(TWO_PI * h * w), 2 / (math.pi * h) * (1 - math.cos(TWO_PI * h * w)))
            for h in range(1, NH + 1)]


def t_pwm():
    return [t_pulse_frame(0.5 - 0.47 * f / (NF - 1)) for f in range(NF)]


def t_harmonic():
    """additive build-up: harmonic 1, then 1..2 .. up to 1..64 (1/h each), the next one fading in"""
    frames = []
    for f in range(NF):
        n = 2 ** (f * 6 / (NF - 1))                        # 1 .. 64 harmonics, exponential
        frames.append(sines([(1 / h) * min(1.0, max(0.0, n - h + 1)) for h in range(1, NH + 1)]))
    return frames


def t_odd():
    """square built from its odd harmonics, then the even ones come in (square -> saw -> bright)"""
    frames = []
    for f in range(NF):
        t = f / (NF - 1)
        frames.append(sines([(1 / h) * (1.0 if h % 2 else t) * (1 + t * min(h, 16) / 16) for h in range(1, NH + 1)]))
    return frames


def formant_amp(freq, formants):
    a = 0.0
    for fc, bw, g in formants:
        a += g / math.sqrt(1 + ((freq - fc) / (bw / 2)) ** 2)
    return a


def t_vocal():
    """vowels A E I O U (formants after Peterson & Barney), a glottal-ish source (1/h), at a 110 Hz frame"""
    V = [[(730, 90, 1.0), (1090, 110, 0.5), (2440, 160, 0.25)],      # A
         [(530, 60, 1.0), (1840, 100, 0.45), (2480, 160, 0.3)],      # E
         [(270, 60, 1.0), (2290, 100, 0.35), (3010, 200, 0.3)],      # I
         [(570, 80, 1.0), (840, 80, 0.6), (2410, 160, 0.15)],        # O
         [(300, 60, 1.0), (870, 80, 0.35), (2240, 120, 0.1)]]        # U
    f0 = 110.0
    keys = [sines([formant_amp(h * f0, v) / h ** 0.5 for h in range(1, NH + 1)]) for v in V]
    frames = []
    for f in range(NF):
        x = f / (NF - 1) * (len(keys) - 1)
        i = min(int(x), len(keys) - 2)
        frames.append(mix(keys[i], keys[i + 1], x - i))
    return frames


def t_reso():
    """a saw through a resonant low-pass whose peak climbs from the 2nd to the 48th harmonic"""
    frames = []
    for f in range(NF):
        fc = 2 * 24 ** (f / (NF - 1))
        q = 6.0
        amps = []
        for h in range(1, NH + 1):
            r = h / fc
            g = 1 / math.sqrt((1 - r * r) ** 2 + (r / q) ** 2)
            amps.append(saw_amp(h) * g)
        frames.append(sines(amps))
    return frames


def t_sync():
    """a saw hard-synced to the frame's cycle, the slave from 1 to 8 times its master"""
    frames = []
    for f in range(NF):
        k = 1 + 7 * f / (NF - 1)
        frames.append(dft([((n / NW * k) % 1.0) * 2 - 1 for n in range(NW)]))
    return frames


def t_fm():
    """two-operator FM, 1:1, the index from 0 to 8"""
    return [dft([math.sin(TWO_PI * n / NW + 8 * f / (NF - 1) * math.sin(TWO_PI * n / NW)) for n in range(NW)])
            for f in range(NF)]


def t_fm2():
    """FM, 1:2 then 1:3 ratios, index 0 .. 5"""
    frames = []
    for f in range(NF):
        t = f / (NF - 1)
        ratio = 2 if f < NF // 2 else 3
        idx = 5 * t
        frames.append(dft([math.sin(TWO_PI * n / NW + idx * math.sin(ratio * TWO_PI * n / NW)) for n in range(NW)]))
    return frames


def t_fold():
    """a sine through a wavefolder, gain 1 .. 6"""
    return [dft([math.sin(math.pi / 2 * (1 + 5 * f / (NF - 1)) * math.sin(TWO_PI * n / NW)) for n in range(NW)])
            for f in range(NF)]


def t_pd():
    """phase distortion (the CZ's): a cosine read through a bent phase, the bend from none to deep"""
    frames = []
    for f in range(NF):
        d = 0.5 - 0.48 * f / (NF - 1)                        # the knee
        w = []
        for n in range(NW):
            x = n / NW
            p = x / d * 0.5 if x < d else 0.5 + (x - d) / (1 - d) * 0.5
            w.append(-math.cos(TWO_PI * p))
        frames.append(dft(w))
    return frames


def t_digital():
    """a band of harmonics walking up (the PPG's digital sweeps): a window 8 harmonics wide, from 1 to 64"""
    frames = []
    for f in range(NF):
        c = 1 + 63 * (f / (NF - 1)) ** 1.5
        frames.append(sines([math.exp(-((h - c) / 4) ** 2) * (1.0 if h % 3 else 0.6) + (1.0 if h == 1 else 0.0) * 0.5
                             for h in range(1, NH + 1)]))
    return frames


def t_bell():
    """bell-like: the fundamental and a few strong high partials (2, 5.0, 8, 11, 15 ...) brightening"""
    frames = []
    for f in range(NF):
        t = f / (NF - 1)
        amps = [0.0] * NH
        for h, g in ((1, 1.0), (2, 0.6), (5, 0.5 * t + 0.1), (8, 0.4 * t), (11, 0.35 * t), (15, 0.3 * t),
                     (19, 0.25 * t * t), (24, 0.2 * t * t), (31, 0.15 * t * t)):
            amps[h - 1] = g
        frames.append(sines(amps))
    return frames


def t_organ():
    """drawbar registrations: flute 8', + 4', + 2 2/3', full, then percussive tops"""
    regs = [{1: 1}, {1: 1, 2: 0.7}, {1: 1, 2: 0.6, 3: 0.5}, {1: 1, 2: 0.7, 3: 0.6, 4: 0.5},
            {1: 1, 2: 0.8, 3: 0.7, 4: 0.6, 6: 0.5, 8: 0.5}, {1: 0.8, 2: 0.8, 3: 0.8, 4: 0.8, 5: 0.6, 6: 0.6, 8: 0.7},
            {1: 0.6, 3: 1.0, 4: 0.5, 8: 0.8, 12: 0.5, 16: 0.4}]
    keys = [sines([r.get(h, 0.0) for h in range(1, NH + 1)]) for r in regs]
    frames = []
    for f in range(NF):
        x = f / (NF - 1) * (len(keys) - 1)
        i = min(int(x), len(keys) - 2)
        frames.append(mix(keys[i], keys[i + 1], x - i))
    return frames


def t_chip():
    """4-bit stepped waves: a pulse, a staircase triangle, a stepped saw, then coarser and coarser"""
    frames = []
    for f in range(NF):
        steps = 16 - f // 2 * 2 if f < 14 else 2
        kind = f % 3
        w = []
        for n in range(NW):
            x = n / NW
            v = (x * 2 - 1) if kind == 0 else (1 - 4 * abs(x - 0.5)) if kind == 1 else (1.0 if x < 0.25 + f / 64 else -1.0)
            v = round((v + 1) / 2 * (steps - 1)) / (steps - 1) * 2 - 1
            w.append(v)
        frames.append(dft(w))
    return frames


def t_comb():
    """a saw through a comb whose notches slide (a flanger frozen into the table)"""
    frames = []
    for f in range(NF):
        d = 1.5 + 14 * f / (NF - 1)                          # notch spacing in harmonics
        frames.append(sines([saw_amp(h) * abs(math.cos(math.pi * h / d)) for h in range(1, NH + 1)]))
    return frames


def t_twin():
    """a saw, then a second saw an octave, a twelfth, two octaves up joins in (organ-like stacks)"""
    saw = [saw_amp(h) for h in range(1, NH + 1)]
    keys = []
    for k in (1, 2, 3, 4, 6):
        a = list(saw)
        for h in range(1, NH // k + 1):
            a[h * k - 1] += saw_amp(h) * 0.8
        keys.append(sines(a))
    frames = []
    for f in range(NF):
        x = f / (NF - 1) * (len(keys) - 1)
        i = min(int(x), len(keys) - 2)
        frames.append(mix(keys[i], keys[i + 1], x - i))
    return frames


def t_grit():
    """random spectra (seeded), from dark to bright: digital grit for pads and textures"""
    rnd = random.Random(1989)
    frames = []
    for f in range(NF):
        tilt = 1.6 - 1.2 * f / (NF - 1)
        frames.append([(rnd.uniform(-1, 1) / h ** tilt, rnd.uniform(-1, 1) / h ** tilt) for h in range(1, NH + 1)])
    return frames


TABLES = [("BASIC", t_basic), ("PWM", t_pwm), ("HARMON", t_harmonic), ("ODD", t_odd), ("VOCAL", t_vocal),
          ("RESO", t_reso), ("SYNC", t_sync), ("FM", t_fm), ("FM 2:3", t_fm2), ("FOLD", t_fold), ("PD", t_pd),
          ("DIGITL", t_digital), ("BELL", t_bell), ("ORGAN", t_organ), ("CHIP", t_chip), ("COMB", t_comb)]
EXTRA = [("TWIN", t_twin), ("GRIT", t_grit)]          # (spares: not built, kept for a later bank)
# Optimist: 8 of the 16 (the flash: 8 KB a table as spectra; the WAVE engine, firmware/src/engines/clipper/eng_wave.c)
OPTIMIST = ("BASIC", "PWM", "VOCAL", "RESO", "SYNC", "FM", "DIGITL", "BELL")
TABLES = [t for t in TABLES if t[0] in OPTIMIST]


def q15(x):
    return max(-32767, min(32767, int(round(x * 32767))))


def build():
    lines = ["/* generated by tools/gen_wavetables.py: Clipper's factory wavetables, as spectra (eng_wave.c) */",
             "#pragma once", "#include <stdint.h>", "",
             f"#define WT_NFACTORY {len(TABLES)}", f"#define WT_NH {NH}", f"#define WT_NF {NF}",
             "/* [table][frame][harmonic 1..WT_NH][cos, sin], Q15 (the peak of a frame's copies is 0.9) */",
             "static const int16_t WT_SPEC[WT_NFACTORY][WT_NF][WT_NH][2] = {"]
    for name, fn in TABLES:
        frames = fn()
        assert len(frames) == NF
        lines.append(f"    {{   /* {name} */")
        for spec in frames:
            p = peak(spec) or 1.0
            g = 0.9 / p
            vals = []
            for a, b in spec:
                vals += [q15(a * g), q15(b * g)]
            while len(vals) > 2 and vals[-1] == 0 and vals[-2] == 0 and len(vals) > 2:
                vals = vals[:-2]
            lines.append("        {" + ",".join(f"{{{vals[i]},{vals[i + 1]}}}" for i in range(0, len(vals), 2)) + "},")
        lines.append("    },")
    lines.append("};")
    lines.append("static const char *const WT_NAMES[WT_NFACTORY] = {" + ", ".join(f'"{n}"' for n, _ in TABLES) + "};")
    return "\n".join(lines) + "\n"


def main(out):
    out = Path(out)
    key = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()[:16]
    cache = out.with_suffix(".key")
    if out.exists() and cache.exists() and cache.read_text().strip() == key:
        print(f"wavetables: {out} (cached)")
        return
    text = build()
    out.write_text(text)
    cache.write_text(key + "\n")
    print(f"wavetables: {len(TABLES)} tables x {NF} frames x {NH} harmonics -> {out}")


if __name__ == "__main__":
    main(sys.argv[1])

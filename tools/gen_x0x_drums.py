#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# From X0X by Charles Vestal (charlesvestal/fm1-x0x 80b7d40, tools/gen_drum_samples.py, GPL-3.0-only); changed
# for Optimist: the cymbal samples are stored as 8-bit block floating point instead of int16 (a quarter of the
# flash X0X spends: 114 KB instead of 221 KB), and the FX bus's pot curve (not used here) is left out.
"""Generated data for the X0X 909 drum kit (firmware/src/drums/x0x/drum909.c; FELUCCA_DRUM_X909).

Writes two headers (both flash-resident `static const` data):

  x0x_drum_samples.h  the hi-hat, ride and crash PCM (assets/x0x909/*.wav, from ER-99 via 9W9, GPL-3.0) as
                      8-bit block floating point: per 32 samples one shift s (0..8), then each sample is
                      m << s (m int8). Measured against the 16-bit source: 42 dB SNR on each file (X0X keeps
                      int16; 4-bit IMA ADPCM, the sample sets' format, would be 17 dB on these noisy sounds).
                      hh.wav is 24-bit: rounded to 16 bits first (the 909's own cymbals are 6-bit).
                      Smaller forms of the ride + crash (93 KB), measured 2026-10-06 (perf/x0x-drums): 6-bit
                      block floating point 69 KB at 30 dB; IMA ADPCM 44 KB at 18 dB, 22 KB at 22.05 kHz at
                      8 dB; 8-bit at 32 kHz (read back by d9_render_smp's linear interpolation) 66 KB at
                      11-13 dB; a Rice code of these mantissas (lossless) 6 % less. The 6-bit form is the
                      builder's choice X909_CYM 2 (x0x_smp_*_p: four mantissas in three bytes, x0x_smp_*_e6);
                      8-bit stays the default.
  x0x_drum_tables.h   the tanh lookup used by every saturator, and 9W9's EXP pot curves (er99_pots.h:
                      min * (max/min)^(pot/127)) evaluated here, so the device needs no powf and gets the
                      values 9W9 computes with libm.

  tools/gen_x0x_drums.py [build/gen/x0x_drum_samples.h]
The tables header is written next to the samples header.
"""
import math
import struct
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
ASSETS = SRC / "assets" / "x0x909"

# (C name, file). The closed hat plays the open hat's buffer (as in 9W9).
SAMPLES = [("x0x_smp_hh", "hh.wav"), ("x0x_smp_ride", "ride.wav"), ("x0x_smp_crash", "crash.wav")]
BLOCK = 32             # samples per shift (X0X_SMP_BLOCK)

# 9W9's EXP pot ranges (er99_pots.h). One table per distinct (min, max).
EXP_RANGES = [
    ("BD_DECAY", 100.0, 4000.0), ("BD_PITCH", 0.43, 4.7), ("DRIVE", 0.85, 12.0),
    ("SD_TUNE", 130.0, 320.0), ("SD_TONE", 300.0, 4000.0),
    ("LT_TUNE", 50.0, 90.0), ("LT_DECAY", 80.0, 2600.0),
    ("MT_TUNE", 80.0, 125.0), ("MT_DECAY", 70.0, 2200.0),
    ("HT_TUNE", 110.0, 170.0), ("HT_DECAY", 60.0, 2000.0),
    ("RS_TUNE", 150.0, 300.0), ("CP_TUNE", 650.0, 1400.0), ("CP_TAIL", 120.0, 1000.0),
    ("OH_DECAY", 20.0, 1200.0), ("CH_DECAY", 15.0, 300.0), ("CY_DECAY", 100.0, 3000.0),
    ("SMP_PITCH", 0.25, 4.0),
]

TANH_N = 1024          # points over [0, TANH_MAX]; linear interpolation
TANH_MAX = 8.0


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def cfloat(x):
    s = repr(f32(x))
    if "e" not in s and "." not in s:
        s += ".0"
    return s + "f"


def read_wav(path):
    """first channel as a list of int16 (16-bit kept, 24-bit rounded)"""
    d = path.read_bytes()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a WAV")
    i, ch, bits, data = 12, 1, 16, None
    while i + 8 <= len(d):
        cid, sz = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"fmt ":
            ch, bits = struct.unpack("<H", d[i + 10:i + 12])[0], struct.unpack("<H", d[i + 22:i + 24])[0]
        elif cid == b"data":
            data = d[i + 8:i + 8 + sz]
            break                                   # what 9W9's loader does: stop at data
        i += 8 + sz + (sz & 1)
    if data is None:
        raise SystemExit(f"{path}: no data chunk")
    bp = bits // 8 * ch
    out = []
    for k in range(len(data) // bp):
        p = data[k * bp:k * bp + bits // 8]
        if bits == 16:
            v = struct.unpack("<h", p)[0]
        elif bits == 24:
            v24 = int.from_bytes(p, "little", signed=True)
            v = max(-32768, min(32767, int(math.floor(v24 / 256.0 + 0.5))))
        else:
            raise SystemExit(f"{path}: {bits}-bit not handled")
        out.append(v)
    return out, bits


def bfp8(pcm, bits=8):
    """int16 -> (mantissas of `bits` bits, shifts): each block of BLOCK samples at the smallest shift that fits"""
    lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    man, sh = [], []
    for i in range(0, len(pcm), BLOCK):
        blk = pcm[i:i + BLOCK]
        s = 0
        while any(not lo <= (x + (1 << s >> 1)) >> s <= hi for x in blk):
            s += 1
        sh.append(s)
        man += [max(lo, min(hi, (x + (1 << s >> 1)) >> s)) for x in blk]
    return man, sh


def pack6(man):
    """6-bit mantissas, four in three bytes (little-endian: m0 in bits 0..5 ... m3 in 18..23; X909_CYM 2)"""
    m = man + [0] * (-len(man) % 4)
    out = []
    for i in range(0, len(m), 4):
        w = sum((v & 63) << (6 * j) for j, v in enumerate(m[i:i + 4]))
        out += [w & 255, w >> 8 & 255, w >> 16 & 255]
    return out


def snr_db(pcm, man, sh):
    e = sum((x - (m << sh[k // BLOCK])) ** 2 for k, (x, m) in enumerate(zip(pcm, man)))
    s = sum(x * x for x in pcm)
    return 10 * math.log10(s / max(e, 1))


def arr(ctype, name, vals, fmt=str, per=12):
    lines = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        lines.append("    " + ", ".join(fmt(v) for v in vals[i:i + per]) + ",")
    lines.append("};")
    return "\n".join(lines)


def pot_exp(lo, hi, pot):
    """er99_pot_to_value for an EXP pot, in float as 9W9 evaluates it"""
    ratio = f32(f32(hi) / f32(lo))
    t = f32(pot / 127.0)
    return f32(f32(lo) * f32(ratio ** t))


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def main():
    out_s = Path(sys.argv[1]) if len(sys.argv) > 1 else SRC / "build" / "gen" / "x0x_drum_samples.h"
    out_t = out_s.parent / "x0x_drum_tables.h"
    out_s.parent.mkdir(parents=True, exist_ok=True)

    hdr = ["/* Generated by tools/gen_x0x_drums.py -- do not edit.",
           " * Hi-hat, ride and crash from ER-99 (Matthew Cieplak) via 9W9, GPL-3.0.",
           f" * Mono, 44.1 kHz, 8-bit block floating point: sample i = m[i] << e[i / {BLOCK}] (int16 scale). */",
           "#pragma once", "#include <stdint.h>", "", f"#define X0X_SMP_BLOCK {BLOCK}u", ""]
    total, nbytes, notes = 0, 0, []
    for name, fn in SAMPLES:
        pcm, bits = read_wav(ASSETS / fn)
        man, sh = bfp8(pcm)
        total += len(pcm)
        nbytes += len(man) + len(sh)
        notes.append(f"{fn} {snr_db(pcm, man, sh):.1f} dB")
        hdr.append(f"/* {fn}: {len(pcm)} frames, source {bits}-bit, {snr_db(pcm, man, sh):.1f} dB SNR */")
        hdr.append(f"#define {name.upper()}_LEN {len(pcm)}u")
        hdr.append(arr("int8_t", name + "_m", man, per=24))
        hdr.append(arr("uint8_t", name + "_e", sh, per=32))
        if name != "x0x_smp_hh":                  # Optimist: the ride and crash also as 6-bit (X909_CYM 2)
            m6, s6 = bfp8(pcm, 6)
            notes.append(f"{fn} 6-bit {snr_db(pcm, m6, s6):.1f} dB")
            hdr.append(f"/* {fn} as 6-bit block floating point (X909_CYM 2): {snr_db(pcm, m6, s6):.1f} dB SNR */")
            hdr.append(arr("uint8_t", name + "_p", pack6(m6), per=24))
            hdr.append(arr("uint8_t", name + "_e6", s6, per=32))
        hdr.append("")
    write_if_changed(out_s, "\n".join(hdr))

    tab = [f32(math.tanh(TANH_MAX * i / TANH_N)) for i in range(TANH_N + 2)]
    t = ["/* Generated by tools/gen_x0x_drums.py -- do not edit. */",
         "#pragma once", "",
         f"/* tanh(u) at u = i * {TANH_MAX} / {TANH_N}, i = 0..{TANH_N + 1} (one guard point) */",
         f"#define X0X_TANH_N {TANH_N}",
         f"#define X0X_TANH_SCALE {cfloat(TANH_N / TANH_MAX)}",
         arr("float", "x0x_tanh_tab", tab, cfloat, per=6), "",
         "/* 9W9 EXP pots (er99_pots.h): value = min * (max/min)^(pot/127), pot 0..127 */"]
    for i, (n, lo, hi) in enumerate(EXP_RANGES):
        t.append(f"#define X0X_EXP_{n} {i}   /* {lo:g} .. {hi:g} */")
    t.append(f"#define X0X_EXP_COUNT {len(EXP_RANGES)}")
    t.append(f"static const float x0x_pot_exp[{len(EXP_RANGES)}][128] = {{")
    for n, lo, hi in EXP_RANGES:
        vals = [pot_exp(lo, hi, p) for p in range(128)]
        t.append("    { /* " + n + " */")
        for i in range(0, 128, 6):
            t.append("        " + ", ".join(cfloat(v) for v in vals[i:i + 6]) + ",")
        t.append("    },")
    t.append("};")
    t.append("")
    write_if_changed(out_t, "\n".join(t))
    print(f"x0x drums: {total} frames in {nbytes} B (8-bit block float; {', '.join(notes)}) -> {out_s.name}; "
          f"tanh {TANH_N + 2} + {len(EXP_RANGES)} pot curves -> {out_t.name}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the SLOOP HIP-HOP PACK: free sounds for the user sample slots USR1..USR3.

    python tools/gen_hiphop_pack.py            (needs numpy, scipy, soundfile; network once)

Every sound comes from a free library and is CC0 (public domain). Nothing here needs a
credit (the CC BY 3.0 E.PIANO was dropped: only GPL-compatible licences are kept).
See assets/hiphop-pack/CREDITS.txt.

Each sound is a set of zones (one WAV each, mono 16-bit 22050 Hz, the rate of the
slots) written to assets/hiphop-pack/<SOUND>/, plus assets/hiphop-pack/pack.json
(roots, key ranges) that the web editor reads to load a sound into a slot in one click
(tools/fm1_sample_upload.py loads them too). A slot holds about 7.4 s in all.

What makes them sound like a crate-dug record (the boom-bap sound of 1988-1996: a
record sampled into an SP-1200 / MPC60) is done here, not in the firmware: every
zone is retuned to concert pitch, then goes through a "dusty" chain: high-pass,
a warm low shelf, a gentle low-pass (the record and the sampler's filter), tape-style
saturation and a little wow. The slot's own 22 kHz ADPCM adds the rest of the grit.
"""
import json
import math
import os
import sys
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "hiphop-pack"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import shared  # noqa: E402

# the downloaded sources: in a git worktree the main checkout's cache (downloaded once; tools/shared.py)
CACHE = shared.resolve("build/hiphop-src", Path.is_dir, ROOT)[0]
RATE = 22050
SLOT_SAMPLES = (0x14000 - 512) * 2            # 4-bit ADPCM: two samples a byte (sampleio.SLOT_MAX_DATA)
SLOT_BUDGET = int(SLOT_SAMPLES * 0.97)        # (a margin: the editor resamples the WAVs)

RAW = "https://raw.githubusercontent.com/"
VSCO = RAW + "sgossner/VSCO-2-CE/master/"
VCSL = RAW + "sgossner/VCSL/master/"
SONICPI = RAW + "sonic-pi-net/sonic-pi/main/etc/samples/"

NOTE = "C C# D D# E F F# G G# A A# B".split()


def note_name(n):
    return f"{NOTE[n % 12]}{n // 12 - 1}"


def fetch(url):
    """the file at url (cached in build/hiphop-src) -> its path"""
    p = CACHE / urllib.parse.quote(url[len(RAW):], safe="")
    if not p.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        q = url[:len(RAW)] + urllib.parse.quote(url[len(RAW):])
        with urllib.request.urlopen(q, timeout=120) as r:
            data = r.read()
        part = p.with_name(f"{p.name}.{os.getpid()}.part")      # (atomic: a worktree running at the same time)
        part.write_bytes(data)
        os.replace(part, p)
        print(f"  fetched {url[len(RAW):]} ({p.stat().st_size // 1024} KiB)")
    return p


def load(url):
    """-> (mono float64, rate)"""
    x, sr = sf.read(str(fetch(url)), dtype="float64", always_2d=True)
    return x.mean(axis=1), sr


def onset(x, rel=0.03):
    thr = rel * np.max(np.abs(x))
    i = int(np.argmax(np.abs(x) > thr))
    return max(0, i - 32)


def detect_midi(x, sr, guess):
    """the pitch of x (fractional MIDI note), searched within +-8 semitones of guess:
    YIN on 0.25 s from 60 ms after the onset (past the attack)"""
    a = onset(x) + int(0.06 * sr)
    w = x[a:a + int(0.25 * sr)]
    if len(w) < 2048:
        w = x[-int(0.25 * sr):]
    fmin = 440 * 2 ** ((guess - 8 - 69) / 12)
    fmax = 440 * 2 ** ((guess + 8 - 69) / 12)
    W = len(w) // 2
    lo, hi = max(2, int(sr / fmax)), min(W - 2, int(sr / fmin) + 1)
    d = np.array([np.sum((w[:W] - w[t:t + W]) ** 2) for t in range(hi + 2)])
    cm = np.ones_like(d)
    run = np.cumsum(d[1:])
    cm[1:] = d[1:] * np.arange(1, len(d)) / np.where(run > 0, run, 1)
    seg = cm[lo:hi]
    t = lo + int(np.argmin(seg))
    if 1 <= t < len(cm) - 1:
        y0, y1, y2 = cm[t - 1], cm[t], cm[t + 1]
        den = y0 - 2 * y1 + y2
        t = t + (0.5 * (y0 - y2) / den if den else 0)
    return 12 * math.log2(sr / t / 440.0) + 69


def repitch(x, sr, semis):
    """play x semis higher (faster) by interpolation, at sr"""
    if abs(semis) < 1e-4:
        return x
    r = 2 ** (semis / 12)
    t = np.arange(0, len(x) - 1, r)
    return np.interp(t, np.arange(len(x)), x)


def to_rate(x, sr):
    g = math.gcd(int(sr), RATE)
    return signal.resample_poly(x, RATE // g, int(sr) // g)


def source(url, nominal, target):
    """the recording at url, retuned to MIDI note target, at RATE, from its onset. The libraries
    are tuned: the note of the file name is trusted, the measured pitch only trims it (a
    measure further than 0.3 semitone from the name is a wrong measure: mallets, plucks)"""
    x, sr = load(url)
    x = x[onset(x):]
    det = detect_midi(x, sr, nominal)
    m = det if abs(det - nominal) <= 0.3 else nominal
    y = to_rate(repitch(x, sr, target - m), sr)
    print(f"  {url.rsplit('/', 1)[-1]:42s} {nominal:3d} (measured {det:6.2f}) -> {note_name(target):4s} "
          f"({target - m:+.2f} st)")
    return y, m


def dusty(x, lp=9000.0, drive=1.6, warm=2.5, hp=35.0, wow=0.0012, seed=1):
    """the crate-dug chain: HP, low shelf (+warm dB under ~160 Hz), 2-pole LP, tanh, wow"""
    sos = signal.butter(2, hp, "highpass", fs=RATE, output="sos")
    x = signal.sosfilt(sos, x)
    low = signal.sosfilt(signal.butter(1, 160, "lowpass", fs=RATE, output="sos"), x)
    x = x + low * (10 ** (warm / 20) - 1)
    if lp:
        x = signal.sosfilt(signal.butter(2, lp, "lowpass", fs=RATE, output="sos"), x)
    pk = np.max(np.abs(x)) or 1.0
    x = np.tanh(drive * x / pk) / math.tanh(drive)
    if wow:                                       # a slow drift of the speed: a record, a tape
        rng = np.random.default_rng(seed)
        ph = rng.uniform(0, 2 * math.pi)
        n = np.arange(len(x))
        dev = wow * RATE / (2 * math.pi * 0.55) * np.sin(2 * math.pi * 0.55 * n / RATE + ph)
        x = np.interp(n + dev - dev[0], n, x)
    return x


def shape(x, seconds, fade=0.3, attack=0.002):
    """cut to seconds, short fade in, the last fade x length faded out (cosine)"""
    n = min(len(x), int(seconds * RATE))
    x = x[:n].copy()
    a = max(1, int(attack * RATE))
    x[:a] *= np.linspace(0, 1, a)
    f = max(1, int(n * fade))
    x[n - f:] *= 0.5 * (1 + np.cos(np.linspace(0, math.pi, f)))
    return x


def mix(*parts):
    n = max(len(p) for p, _ in parts)
    y = np.zeros(n)
    for p, g in parts:
        y[:len(p)] += g * p
    return y


def write(path, x):
    pk = np.max(np.abs(x)) or 1.0
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), (x / pk * 0.92 * 32767).astype(np.int16), RATE, subtype="PCM_16")


# ------------------------------------------------------------------ sounds ---
# zone: (target root, length s, builder(target) -> float array at RATE)

def midi_of(name):
    """"C#3" -> 49 (C4 = 60)"""
    k = 2 if len(name) > 2 and name[1] == "#" else 1
    return NOTE.index(name[:k]) + (int(name[k:]) + 1) * 12


def pick(base, fmt, names, offset):
    """a builder for target notes from the recording nearest each one: names are the notes of
    the files (fmt.format(name); "D#3:eb3" when the file spells it eb3), offset what to add to
    them for MIDI (C4 = 60)"""
    pool = [(midi_of(n.split(":")[0]) + offset, n.split(":")[-1]) for n in names.split()]

    def make(tgt):
        m, n = min(pool, key=lambda q: (abs(q[0] - tgt), q[0]))
        return source(base + fmt.format(n), m, tgt)[0]
    return make


def section(*layers):
    """layers: (builder, semitones under the target, gain), mixed from their onsets"""
    return lambda tgt: mix(*[(b(tgt - down), g) for b, down, g in layers])


# VSCO-2 CE and VCSL name middle C "C3": their note names are one octave under the MIDI
# names used here (offset 12). Every pick is checked against its measured pitch (source()).
TPT = pick(VSCO, "Brass/Trumpet/stac/Sum_SHTrumpet_stac_{}_v3_rr1.wav", "F2 A2 C3 D#3 F3 G3 A#3 D4 F4 A4 C5", 12)
TBN = pick(VSCO, "Brass/Tenor Trombone/stac/tenortbn_stac_{}_v3_rr1.wav", "F1 A#1 F2 A#2 D3 F3", 12)
VLN = pick(VSCO, "Strings/Violin Section/Spic/VlnEns_Spic_{}_v2_rr1.wav",
           "G2 A2 B2 D3 F#3 A3 C4 E4 G4 B4 D5", 12)
VLA = pick(VSCO, "Strings/Viola Section/spic/Violas_spic_{}_v2_rr1.wav", "C2 E2 G2 B2 D3 F3 A3 C4 E4 G4", 12)
CB = pick(VSCO, "Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_{}_rr1.wav", "E0:E0_v3 A#0:A#0_v3 D1:D1_v1 F#1:F#1_v1 A1:A1_v1 C#2:C#2_v1", 12)
VIB = pick(VCSL, "Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_{}_v3_rr1_Main.wav",
           "F2 A2 C3 E3 G3 B3 D4 F4 A4 C5 E5", 12)
UPR = pick(VCSL, "Chordophones/Zithers/Upright Piano, Knight/Sustains/Player_vl2_rr1_{}.wav",
           "F2 G2 A2 B2 C#3 D#3 F3 G3 A3 B3 C#4 D#4 F4 G4 A4", 12)

SOUNDS = [
    dict(name="BASS", title="Upright bass (pizzicato)",
         desc="A jazz upright bass. Plays two octaves under the keys: F3 on the keyboard is F1.",
         license="CC0 - VSCO-2 Community Edition (Versilian Studios)",
         transpose=24,
         chain=dict(lp=4200, drive=2.0, warm=3.0, wow=0.0),
         zones=[(28, 1.15, CB), (34, 1.15, CB), (38, 1.1, CB), (42, 1.1, CB), (45, 1.1, CB), (49, 1.05, CB)]),
    dict(name="HORNS", title="Horn section stabs",
         desc="Trumpet and trombone an octave apart: the stab of a funk record.",
         license="CC0 - VSCO-2 Community Edition (Versilian Studios)",
         chain=dict(lp=9000, drive=2.2, warm=1.5),
         zones=[(t, 0.8, section((TPT, 0, 1.0), (TBN, 12, 0.75))) for t in (53, 57, 60, 63, 67, 70, 74)]),
    dict(name="STRINGS", title="String stabs",
         desc="Violins and violas, short and hard: the orchestra stab of '90s rap.",
         license="CC0 - VSCO-2 Community Edition (Versilian Studios)",
         chain=dict(lp=9500, drive=1.8, warm=1.5),
         zones=[(t, 0.9, section((VLN, 0, 1.0), (VLA, 12, 0.7))) for t in (55, 59, 62, 66, 69, 72, 76)]),
    dict(name="VIBES", title="Vibraphone",
         desc="Hard mallets, jazz-rap vibes.",
         license="CC0 - VCSL (Versilian Studios)",
         chain=dict(lp=10000, drive=1.3, warm=1.0),
         zones=[(53, 1.8, VIB), (60, 1.8, VIB), (67, 1.8, VIB), (74, 1.7, VIB)]),
    dict(name="UPRIGHT", title="Dusty upright piano",
         desc="An old upright, filtered like a loop off a jazz record.",
         license="CC0 - VCSL (Versilian Studios)",
         chain=dict(lp=6500, drive=1.8, warm=3.0, wow=0.002),
         zones=[(53, 1.8, UPR), (59, 1.8, UPR), (65, 1.8, UPR), (73, 1.7, UPR)]),
]

# The DJ's sounds: one per key range, at their own pitch in the middle of it (CC0, Sonic Pi)
SCRATCH = dict(name="SCRATCH", title="DJ scratches",
               desc="Scratch, backspin, rewind and a finger snap, each on its own keys.",
               license="CC0 - Sonic Pi sample set (freesound.org CC0 recordings)",
               zones=[("vinyl_scratch", 57, 53, 60, 1.6), ("vinyl_backspin", 64, 61, 67, 1.6),
                      ("vinyl_rewind", 70, 68, 73, 2.2), ("perc_snap", 77, 74, 79, 0.5)])


def build_pitched(snd, report):
    d = OUT / snd["dir"]
    zones, total = [], 0
    tr = snd.get("transpose", 0)
    for k, (tgt, sec, make) in enumerate(snd["zones"]):
        x = make(tgt)
        x = shape(dusty(x, seed=k, **snd["chain"]), sec)
        fn = f"{k:02d}_{note_name(tgt).replace('#', 's')}.wav"     # (no '#': the files go in URLs)
        write(d / fn, x)
        total += len(x)
        zones.append(dict(file=f"{snd['dir']}/{fn}", root=tgt + tr))
    report.append(f"{snd['name']:8s} {len(zones)} zones, {total / RATE:.2f} s of {SLOT_SAMPLES / RATE:.2f} s")
    if total > SLOT_BUDGET:
        raise SystemExit(f"{snd['name']}: {total} samples, a slot holds {SLOT_BUDGET}")
    return zones


def build_scratch(snd, report):
    d = OUT / snd["dir"]
    zones, total = [], 0
    for k, (src, root, lo, hi, sec) in enumerate(snd["zones"]):
        x, sr = load(SONICPI + f"{src}.flac")
        x = to_rate(x[onset(x):], sr)
        x = shape(dusty(x, lp=11000, drive=1.4, warm=1.5, wow=0.0, seed=k), sec, fade=0.15)
        fn = f"{k:02d}_{src}.wav"
        write(d / fn, x)
        total += len(x)
        zones.append(dict(file=f"{snd['dir']}/{fn}", root=root,
                          lo=0 if k == 0 else lo, hi=127 if k == len(snd["zones"]) - 1 else hi))
    report.append(f"{snd['name']:8s} {len(zones)} zones, {total / RATE:.2f} s of {SLOT_SAMPLES / RATE:.2f} s")
    if total > SLOT_BUDGET:
        raise SystemExit(f"{snd['name']}: {total} samples, a slot holds {SLOT_BUDGET}")
    return zones


CREDITS = """SLOOP HIP-HOP PACK - free sounds for the user sample slots USR1..USR3
Made by tools/gen_hiphop_pack.py: retuned, cut and processed ("dusty" chain) from:

  BASS      VSCO-2 Community Edition (Versilian Studios), Solo Contrabass pizzicato. CC0 1.0.
  HORNS     VSCO-2 Community Edition, Trumpet and Tenor Trombone staccato. CC0 1.0.
  STRINGS   VSCO-2 Community Edition, Violin and Viola sections spiccato. CC0 1.0.
            https://github.com/sgossner/VSCO-2-CE
  VIBES     VCSL (Versilian Community Sample Library), Vibraphone, hard mallets. CC0 1.0.
  UPRIGHT   VCSL, Upright Piano (Knight). CC0 1.0.  https://github.com/sgossner/VCSL
  SCRATCH   Sonic Pi sample set, CC0 1.0 (freesound.org): vinyl_scratch (hello_flowers),
            vinyl_backspin (il112), vinyl_rewind (TasmanianPower), perc_snap (SoundCollectah).
            https://github.com/sonic-pi-net/sonic-pi/tree/main/etc/samples

Every sound is CC0: no conditions; music you make with any of them is yours.
No sound here comes from a commercial record.
"""


def main():
    report, pack = [], dict(format="sloop-pack", version=1, rate=RATE,
                            default=["UPRIGHT", "BASS", "HORNS"], sounds=[])
    for snd in SOUNDS + [SCRATCH]:
        snd["dir"] = snd["name"].replace(".", "")
    for snd in SOUNDS:
        print(f"{snd['name']}")
        pack["sounds"].append(dict(name=snd["name"], title=snd["title"], desc=snd["desc"],
                                   license=snd["license"], zones=build_pitched(snd, report)))
    print(SCRATCH["name"])
    pack["sounds"].append(dict(name=SCRATCH["name"], title=SCRATCH["title"], desc=SCRATCH["desc"],
                               license=SCRATCH["license"], zones=build_scratch(SCRATCH, report)))
    (OUT / "pack.json").write_text(json.dumps(pack, indent=1) + "\n", encoding="utf-8")
    (OUT / "CREDITS.txt").write_text(CREDITS, encoding="utf-8")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())

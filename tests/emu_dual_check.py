# SPDX-License-Identifier: GPL-3.0-only
"""The same notes give the same samples in the emulator, and a FELUCCA_DUAL build gives the samples of the
single-core build (docs/DUAL-CORE.md 3.1: CPU1's parts are added with integer sums, the output is bit-identical).
Not part of make test (it needs the emulator):

  python3 tests/emu_dual_check.py FIRMWARE.fwsc [REFERENCE.fwsc ...] [MHZ]      (MHZ: 96 by default)

Each package (its ELF next to it) boots fresh and plays, every key pressed and let go on an audio-half boundary
(play_check align): T2 a chord three times (RHODES: a part CPU1 renders in a DUAL=2 build), then T1 one note
(808 BOOM: CPU0) once with its LFO's sample & hold on the pitch (t1s) and three times without. Checks:
  - T1's takes b and c are the same samples (each follows a take of the same note);
  - every take of a REFERENCE is FIRMWARE's, sample for sample (24 bit): the same .config built with DUAL=0, or
    DUAL=2 with DUAL_PARTS=0 (CPU1 up, rendering nothing) or DUAL_PARTS=3 (T1 and T2 on CPU1: CPU0 renders T3 first);
  - a DUAL build's CPU1 did the jobs; no autosave before the takes end; every package rendered as many halves.
FM1_EMU: the emulator checkout (as tests/emu_boot_check.py); EMU_DUAL_KEEP=DIR keeps the takes. Exit 1 on a failure.
At a clock where a build runs over the CPU guard's ceiling (the mots .config at 48 MHz) the guard sheds voices on
the measured load, which the second core lowers: the samples then differ by design.

t1s: an LFO draws rng() at each wrap (sample & hold). T1 and T3 run their LFOs at one rate and phase from boot (their
presets), so they wrap in the same block, and the value T1 holds depends on which draws first. Before the dual build
drew them on CPU0 ahead of the fork (dual.c dual_lfo_draw), each core drew in its render: a DUAL_PARTS=3 build gave
T1 the value the single-core build gives T3 (t1s max |diff| 0.10 from the onset), and with the default parts it was
a race on rng_state, decided by when CPU1 wakes.

The takes end before the first autosave can start (20 s after boot): its flash writes hold the main loop and the key
scan off (~55 ms at 96 MHz: a key pressed as it starts plays 2..4 halves late, the same samples later) and IRQs off
for up to ~10 ms (a DMA half may go unrendered, depending on where the window falls: the free-running LFOs are then
a half apart from a build where it did not). Why this check: smoke-fx's determinism control (two takes of one note,
3.5 s apart) failed on the mots build with max |diff| 0.267: its second take's note started 512 frames late, the
autosave having started 39 ms before the key; the DUAL=2 build itself was deterministic."""
import importlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
BOOT = importlib.import_module("emu_boot_check")                 # (play_check, peeks)

LFO, BLACK1, BLACK2 = 5, 15, 17                                 # the LFO layer button; black keys 1, 2 (T1, T2 in it)
T1_NOTE = "21"                                                  # white key 5
T2_CHORD = "21,25,28"                                           # white keys 5, 7, 9
TAKE_S, GAP_S = 1.0, 0.8                                        # (all within 20 s of boot)
TAKES = ("t2a", "t2b", "t2c", "t1s", "t1a", "t1b", "t1c")
# T1's LFO: S&H (LFO page, KNOB2 4 detents) on the pitch (LFO DEST, KNOB1 20) for its first note, then the depth back
SH_T1 = [f"hold:{LFO}", "run:0.12", f"release:{LFO}", "run:0.4", "click:KNOB2:4", "click:SELECT:1", "click:KNOB1:20"]


def take(name, keys, d):
    """pressed and let go on a half boundary: a key change lands on the same block at any render cost (a release
    mid-half follows the key scan's place among the bursts, which moves with the cost: the tail differs)"""
    return ["align", f"hold:{keys}", f"wav:{TAKE_S}:{d}/{name}.wav", "align", f"release:{keys}", f"run:{GAP_S}"]


def select(black):
    return [f"hold:{LFO}", "run:0.35", f"hold:{black}", "run:0.12", f"release:{black}", "run:0.3", f"release:{LFO}",
            "run:0.3"]


def script(d, dual):
    s = ["run:2", "align", "peek:felucca_dbg:2"] + select(BLACK2)
    s += sum((take(f"t2{k}", T2_CHORD, d) for k in "abc"), []) + ["peek:autosave_ms:1"] + select(BLACK1)
    s += SH_T1 + take("t1s", T1_NOTE, d) + ["click:KNOB1:-20"]
    s += sum((take(f"t1{k}", T1_NOTE, d) for k in "abc"), []) + ["peek:autosave_ms:1"]
    return s + (["peek:fm1_dual_mb:2"] if dual else [])


def name(fw):
    """the package's folder and name (the packages compared often share a name)"""
    return os.path.join(os.path.basename(os.path.dirname(fw)), os.path.basename(fw))


def frames(path):
    with wave.open(path, "rb") as w:
        return w.readframes(w.getnframes()), w.getsampwidth() * w.getnchannels()


def first_diff(a, b):
    """(frame of the first difference or -1, frames compared)"""
    (x, fs), (y, _) = frames(a), frames(b)
    n = min(len(x), len(y))
    if x[:n] == y[:n]:
        return (-1 if len(x) == len(y) else n // fs), n // fs
    i = next(i for i in range(n) if x[i] != y[i])
    return i // fs, n // fs


def sounds(path):
    x, fs = frames(path)
    return any(x[i:i + fs] != bytes(fs) for i in range(0, len(x), fs))


def play(pc, fw, mhz, steps):
    st = tempfile.mkdtemp(prefix="emu-dual-")
    try:
        p = subprocess.run([pc, "--state", st + "/", "--fresh", fw, *steps],
                           env=dict(os.environ, FM1_CPU_MHZ=str(mhz)), capture_output=True, text=True)
    finally:
        shutil.rmtree(st, ignore_errors=True)
    if p.returncode:
        raise RuntimeError(f"play_check failed ({p.returncode}): {(p.stdout + p.stderr).strip()[-400:]}")
    return p.stdout


def check(pc, fw, mhz, d):
    """(DUAL build, halves rendered at the first align, errors)"""
    dual = b"\0fm1_dual_mb\0" in open(fw[:-5] + ".elf", "rb").read()
    out = play(pc, fw, mhz, script(d, dual))
    saves = [int(m.group(1)) for m in re.finditer(r"^\s*autosave_ms @ 0x[0-9a-f]+: (\d+)", out, re.M)]
    v = BOOT.peeks(out)
    errs = []
    if saves != [0, 0]:
        errs.append(f"an autosave before or in the takes (autosave_ms {saves}): the comparison says nothing")
    errs += [f"{t}: silence" for t in TAKES if not sounds(f"{d}/{t}.wav")]
    if dual:
        req, done = v.get("fm1_dual_mb", [0, 0])
        if not (done and done >= req - 1):
            errs.append(f"CPU1 took no jobs: req {req}, done {done}")
    if not errs:
        at, n = first_diff(f"{d}/t1b.wav", f"{d}/t1c.wav")
        if at >= 0:
            errs.append(f"T1: take c differs from take b from frame {at} of {n}")
    return dual, v.get("felucca_dbg", [0, 0])[1], errs


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    mhz = int(args.pop()) if args[-1].isdigit() else 96
    fws = [os.path.abspath(a) for a in args]
    missing = [fw[:-5] + ".elf" for fw in fws if not os.path.exists(fw[:-5] + ".elf")]
    if missing:
        sys.exit("no ELF next to the package: " + ", ".join(missing))
    pc = BOOT.play_check()
    keep = os.environ.get("EMU_DUAL_KEEP")                        # a directory: the takes stay there
    root = os.path.abspath(keep) if keep else tempfile.mkdtemp(prefix="emu-dual-wav-")
    bad = 0
    try:
        dirs, counts = [], []
        for k, fw in enumerate(fws):
            d = os.path.join(root, str(k))
            os.makedirs(d, exist_ok=True)
            dual, count, errs = check(pc, fw, mhz, d)
            if k and count != counts[0]:
                errs.append(f"{count} halves at the first align, {counts[0]} in {name(fws[0])}")
            print(("FAIL" if errs else "ok  ") + f"  {name(fw)} ({'DUAL' if dual else 'single core'}, "
                  f"{mhz} MHz): " + ("; ".join(errs) if errs else "T1's takes b and c, the same samples"))
            bad += bool(errs)
            dirs.append(d)
            counts.append(count)
        for k in range(1, len(dirs) if not bad else 0):
            errs = []
            for t in TAKES:
                at, n = first_diff(f"{dirs[0]}/{t}.wav", f"{dirs[k]}/{t}.wav")
                if at >= 0:
                    errs.append(f"{t} differs from frame {at} of {n}")
            print(("FAIL" if errs else "ok  ") + f"  {name(fws[0])} and reference {k} "
                  f"({name(fws[k])}): " + ("; ".join(errs) if errs else "the same samples, every take"))
            bad += bool(errs)
    except RuntimeError as x:
        print(f"FAIL  {x}")
        bad += 1
    finally:
        if not keep:
            shutil.rmtree(root, ignore_errors=True)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()

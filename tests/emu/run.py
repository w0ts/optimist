#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The emulator smoke checks (make emu-check; BUILDING.md, Tests). Opt-in: they need Rust (cargo) and the emulator
(tests/emu/session.py says which); not part of make test or make gate.

  python3 tests/emu/run.py [FIRMWARE.fwsc] [--only CHECK[,CHECK..]] [--mhz N] [--out DIR] [--list]

FIRMWARE: a package with its ELF beside it (default build/felucca.fwsc, the last build). The checks drive the panel
of SLOOP's UI (FELUCCA_UI 0); a build of the Optimist UI (FELUCCA_UI 1) runs boot, upfm6 (without its preset save)
and timing only (its gestures differ). Each check prints PASS / FAIL lines and works in OUT/<check>/ (default build/emu-check/: its package copy,
flash state, WAVs and screens). Exit 1 on any FAIL (a check that stops on an error counts as one)."""
import argparse
import importlib
import os
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))                       # (the checks import each other: tests/emu first on the path)
common = importlib.import_module("common")
fwfacts = importlib.import_module("fwfacts")

ROOT = HERE.parents[1]
# name: (module, what it covers, which UIs)
CHECKS = {
    "boot": ("check_boot", "a clean boot: no crash record, the boot guard clears, halves/s, late 0, a note", (0, 1)),
    "patterns": ("check_patterns", "launches (end, bar, now), scenes, the song chain, clear, steps and onsets", (0,)),
    "fx": ("check_fx", "FX slots: COMP heard, bypass, slot swap, determinism, restore, the drum inserts", (0,)),
    "persist": ("check_persist", "save, power cycle, load: patterns, scenes, the song, FX", (0,)),
    "scenefx": ("check_scenefx", "F1: a scene stored from the SAVE layer keeps its FX", (0,)),
    "upfm6": ("check_upfm6", "UP_FM6 moved off the SDK VM; 0xE7000..0xE9FFF untouched", (0, 1)),
    "timing": ("check_timing", "tests/emu_boot_check.py: no late half at 48, 96 and 192 MHz", (0, 1)),
}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", nargs="?", default=str(ROOT / "build" / "felucca.fwsc"))
    ap.add_argument("--only", help="the checks to run, comma separated (--list)")
    ap.add_argument("--mhz", type=int, default=96, help="the emulated CPU clock (default 96)")
    ap.add_argument("--out", default=str(ROOT / "build" / "emu-check"))
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args(argv)
    if a.list:
        for k, (_, what, uis) in CHECKS.items():
            print(f"{k:9s} {what}" + ("" if 1 in uis else " (SLOOP's UI only)"))
        return 0
    fw = Path(a.firmware).resolve()
    if not fw.exists() or not fw.with_suffix(".elf").exists():
        print(f"emu-check: no {fw} with its ELF beside it (make build first)", file=sys.stderr)
        return 2
    ui = fwfacts.Facts(fw).ui
    names = [n.strip() for n in a.only.split(",")] if a.only else list(CHECKS)
    bad = [n for n in names if n not in CHECKS]
    if bad:
        print(f"emu-check: no check {', '.join(bad)} (--list)", file=sys.stderr)
        return 2
    print(f"emu-check: {fw} ({'the Optimist UI' if ui else 'SLOOP UI'}), {a.mhz} MHz, out {a.out}", flush=True)
    results, t0 = [], time.time()
    for n in names:
        mod, _, uis = CHECKS[n]
        if ui not in uis:
            print(f"skip  {n}: SLOOP's UI gestures (this build has the Optimist UI)")
            continue
        t = time.time()
        ctx = None
        try:
            ctx = common.Ctx(fw, a.out, mhz=a.mhz, check=n)
            importlib.import_module(mod).run(ctx)
        except Exception as x:                          # (a check that stops is a FAIL, the others still run)
            print(f"FAIL  {n}: stopped: {type(x).__name__}: {x}", flush=True)
            if os.environ.get("EMU_CHECK_TRACE"):
                traceback.print_exc()
            if ctx is None:
                ctx = argparse.Namespace(passed=0, failed=0)
            ctx.failed += 1
        results.append((n, ctx.passed, ctx.failed, time.time() - t))
    print("emu-check summary:")
    for n, p, f, s in results:
        print(f"  {n:9s} {'ok  ' if not f else 'FAIL'}  {p} passed, {f} failed  ({s:.0f} s)")
    fails = sum(f for _, _, f, _ in results)
    print(f"emu-check: {'PASSED' if not fails else 'FAILED'}: {sum(p for _, p, _, _ in results)} passed, {fails} "
          f"failed in {time.time() - t0:.0f} s")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

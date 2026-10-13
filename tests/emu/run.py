#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The emulator smoke checks (make emu-check; BUILDING.md, Tests). Opt-in: they need Rust (cargo) and the emulator
(tests/emu/session.py says which); not part of make test or make gate.

  python3 tests/emu/run.py [FIRMWARE.fwsc] [--only CHECK[,CHECK..]] [--mhz N] [--out DIR] [--jobs N] [--list]

FIRMWARE: a package with its ELF beside it (default build/felucca.fwsc, the last build). The checks drive the panel
of SLOOP's UI (FELUCCA_UI 0); a build of the Optimist UI (FELUCCA_UI 1) runs boot, upfm6 (without its preset save)
and timing only (its gestures differ). Each check prints PASS / FAIL lines and works in OUT/<check>/ (default build/emu-check/: its package copy,
flash state, WAVs and screens). Exit 1 on any FAIL (a check that stops on an error counts as one).

--jobs N: the checks run N at a time, each in a process of its own (its emulator, its OUT/<check>/ with its flash
state and scratch files), its lines buffered and printed whole in the list's order (default: every check at once,
--jobs 1: one after the other in this process). `timing` (late halves at 48 / 96 / 192 MHz) runs alone, after the
others."""
import argparse
import importlib
import os
import subprocess
import sys
import time
import traceback
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))                       # (the checks import each other: tests/emu first on the path)
common = importlib.import_module("common")
fwfacts = importlib.import_module("fwfacts")
session = importlib.import_module("session")

ROOT = HERE.parents[1]
# name: (module, what it covers, which UIs)
CHECKS = {
    "boot": ("check_boot", "a clean boot: no crash record, the boot guard clears, halves/s, late 0, a note", (0, 1)),
    "patterns": ("check_patterns", "launches (end, bar, now), scenes, the song chain, clear, steps and onsets", (0,)),
    "fx": ("check_fx", "FX slots: COMP heard, bypass, slot swap, determinism, restore, the drum inserts", (0,)),
    "insfx": ("check_insfx", "the further inserts (INS_*): listed, transparent at 0, heard, bypassed, back at 0", (0,)),
    "persist": ("check_persist", "save, power cycle, load: patterns, scenes, the song, FX", (0,)),
    "scenefx": ("check_scenefx", "F1: a scene stored from the SAVE layer keeps its FX", (0,)),
    "autosave": ("check_autosave", "stopped, an edit: autosaved once unchanged 10 s (after power-on, after a save); a power cycle keeps it", (0, 1)),
    "upfm6": ("check_upfm6", "UP_FM6 moved off the SDK VM; 0xE7000..0xE9FFF untouched", (0, 1)),
    "timing": ("check_timing", "tests/emu_boot_check.py: no late half at 48, 96 and 192 MHz", (0, 1)),
}


ALONE = ("timing",)     # (timing-sensitive: run by itself once the others are done, never beside another emulator)


def run_one(n, fw, out, mhz):
    """one check in this process, its lines printed as they come -> (passed, failed, seconds)"""
    t = time.time()
    ctx = None
    try:
        ctx = common.Ctx(fw, out, mhz=mhz, check=n)
        importlib.import_module(CHECKS[n][0]).run(ctx)
    except Exception as x:                              # (a check that stops is a FAIL, the others still run)
        print(f"FAIL  {n}: stopped: {type(x).__name__}: {x}", flush=True)
        if os.environ.get("EMU_CHECK_TRACE"):
            traceback.print_exc()
        if ctx is None:
            ctx = argparse.Namespace(passed=0, failed=0)
        ctx.failed += 1
    return ctx.passed, ctx.failed, time.time() - t


def run_child(n, fw, a):
    """one check in a process of its own (its emulator, OUT/<n>/), its output buffered -> (text, passed, failed, s)"""
    t = time.time()
    res = Path(a.out) / f".{n}.result"
    res.unlink(missing_ok=True)
    p = subprocess.run([sys.executable, str(Path(__file__).resolve()), str(fw), "--only", n, "--mhz", str(a.mhz),
                        "--out", a.out, "--child-result", str(res)],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=ROOT, env=a.child_env)
    try:
        passed, failed = (int(v) for v in res.read_text().split()[:2])
        res.unlink()
        return p.stdout, passed, failed, time.time() - t
    except (OSError, ValueError):                       # (the process died before its result: one FAIL)
        return (p.stdout + f"FAIL  {n}: stopped: its process ended (rc {p.returncode}) without a result\n",
                0, 1, time.time() - t)


def run_parallel(names, fw, a):
    """the checks a.jobs at a time (ALONE after the others, by itself), each one's lines printed whole, in order
    -> {name: (passed, failed, seconds)}"""
    # the driver: built once here; the children get its path and never run cargo themselves (a cargo build in one
    # relinks the binary while another starts it: FileNotFoundError, integration57). Not built: each child stops
    # on the missing path, none tries to build it
    try:
        drv = str(session.driver())
    except Exception as x:
        print(f"emu-check: the driver: {type(x).__name__}: {x}", flush=True)
        drv = str(ROOT / "build" / "emu-drv" / "not-built")
    a.child_env = dict(os.environ, **{session.DRIVER_ENV: drv})
    Path(a.out).mkdir(parents=True, exist_ok=True)
    done = {}
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futs = {n: pool.submit(run_child, n, fw, a) for n in names if n not in ALONE}
        for n in names:
            if n in futs:
                text, p, f, s = futs[n].result()
                sys.stdout.write(text)
                sys.stdout.flush()
                done[n] = (p, f, s)
    for n in names:
        if n in ALONE:
            text, p, f, s = run_child(n, fw, a)
            sys.stdout.write(text)
            sys.stdout.flush()
            done[n] = (p, f, s)
    return done


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", nargs="?", default=str(ROOT / "build" / "felucca.fwsc"))
    ap.add_argument("--only", help="the checks to run, comma separated (--list)")
    ap.add_argument("--mhz", type=int, default=96, help="the emulated CPU clock (default 96)")
    ap.add_argument("--out", default=str(ROOT / "build" / "emu-check"))
    ap.add_argument("--jobs", type=int, default=0,
                    help="checks at once, each in its own process (default: all of them, at most the CPU count; "
                         "1: one after the other); timing always runs alone, last")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--child-result", help=argparse.SUPPRESS)     # (run_child: one check, its counts to this file)
    a = ap.parse_args(argv)
    if a.child_result:
        fw = Path(a.firmware).resolve()
        p, f, _ = run_one(a.only, fw, a.out, a.mhz)
        Path(a.child_result).write_text(f"{p} {f}\n")
        return 0
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
    t0 = time.time()
    a.out = str(Path(a.out).resolve())                  # (the checks' processes run in ROOT)
    run_names = [n for n in names if ui in CHECKS[n][2]]
    jobs = a.jobs if a.jobs > 0 else min(len(run_names) or 1, os.cpu_count() or 1)
    a.jobs = jobs
    alone = jobs > 1 and any(n in ALONE for n in run_names)
    print(f"emu-check: {fw} ({'the Optimist UI' if ui else 'SLOOP UI'}), {a.mhz} MHz, out {a.out}, "
          f"{jobs} at once{', timing alone after them' if alone else ''}", flush=True)
    for n in names:
        if n not in run_names:
            print(f"skip  {n}: SLOOP's UI gestures (this build has the Optimist UI)")
    names = run_names
    if jobs == 1:
        done = {n: run_one(n, fw, a.out, a.mhz) for n in names}
    else:
        done = run_parallel(names, fw, a)
    results = [(n, *done[n]) for n in names]
    print("emu-check summary:")
    for n, p, f, s in results:
        print(f"  {n:9s} {'ok  ' if not f else 'FAIL'}  {p} passed, {f} failed  ({s:.0f} s)")
    fails = sum(f for _, _, f, _ in results)
    print(f"emu-check: {'PASSED' if not fails else 'FAILED'}: {sum(p for _, p, _, _ in results)} passed, {fails} "
          f"failed in {time.time() - t0:.0f} s")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

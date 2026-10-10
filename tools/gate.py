#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The integrator's gate (docs/INTEGRATION.md): what must pass before an integration branch is pushed, on this
machine, in one command. `make gate` or `python3 tools/optimist.py gate`.

  python3 tools/gate.py [--config FILE ...] [--set KEY=V ...] [--no-profiles] [--emu [--only CHECKS]]

The steps, each logged to build/gate/<step>.log (the terminal gets one line a step, and the log's tail on a FAIL):
  costs       every builder item has a measured cost (measure_costs.py --missing --check, as CI)
  profile P   each published profile builds (as CI's profiles job; --no-profiles: user-default only)
  config F    each --config FILE builds
  set         user-default with every --set KEY=V builds (one build)
  test        the host tests (optimist.py test: its BLE, user-default and measurement builds, tests/run_tests.sh)
  final       user-default built again last: build/ holds its package and ELF (make flash, make emu-check)
  emu         with --emu: the emulator smoke checks on that build (tests/emu/run.py; make emu-check)
Every step runs, a failed one does not stop the rest (but no emu without a final build). Exit 1 on any FAIL."""
import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OPT = [sys.executable, str(ROOT / "tools" / "optimist.py")]
LOGS = ROOT / "build" / "gate"


def git(*args):
    p = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else ""


def published():
    p = subprocess.run(OPT + ["config", "--published"], cwd=ROOT, capture_output=True, text=True)
    try:
        names = json.loads(p.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        raise SystemExit(f"gate: config --published gave no list: {(p.stdout + p.stderr).strip()[-300:]}")
    return ["user-default"] + [n for n in names if n != "user-default"]


def plan(a):
    """-> [(step name, command)] in the order they run"""
    steps = [("costs", [sys.executable, str(ROOT / "tools" / "builder" / "measure_costs.py"), "--missing",
                        "--check"])]
    for n in (["user-default"] if a.no_profiles else published()):
        steps.append((f"profile {n}", OPT + ["build", "--profile", n]))
    for f in a.config or []:
        steps.append((f"config {Path(f).name}", OPT + ["build", "--config", str(Path(f).resolve())]))
    if a.set:
        steps.append(("set " + " ".join(a.set), OPT + ["build", "--profile", "user-default"] +
                      [x for kv in a.set for x in ("--set", kv)]))
    steps.append(("test", OPT + ["test"]))
    steps.append(("final", OPT + ["build", "--profile", "user-default"]))
    if a.emu:
        steps.append(("emu", [sys.executable, str(ROOT / "tests" / "emu" / "run.py"),
                              str(ROOT / "build" / "felucca.fwsc")] + (["--only", a.only] if a.only else [])))
    return steps


def run_step(i, name, cmd):
    """-> (ok, seconds); the output to build/gate/NN-name.log"""
    log = LOGS / f"{i:02d}-{re.sub(r'[^A-Za-z0-9.]+', '-', name)[:48]}.log"
    t = time.time()
    with open(log, "w") as out:
        out.write("$ " + " ".join(cmd) + "\n")
        out.flush()
        rc = subprocess.call(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
    s = time.time() - t
    print(f"{'ok  ' if rc == 0 else 'FAIL'}  {name:34s} {s:6.0f} s   {log.relative_to(ROOT)}", flush=True)
    if rc:
        tail = log.read_text(errors="replace").splitlines()[-15:]
        print("\n".join("      | " + ln for ln in tail), flush=True)
    return rc == 0, s


def main(argv=None):
    ap = argparse.ArgumentParser(prog="gate", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", action="append", metavar="FILE", help="a .config to build too (repeatable)")
    ap.add_argument("--set", action="append", metavar="KEY=V", help="user-default with this item changed (repeatable: "
                    "one build with all of them)")
    ap.add_argument("--no-profiles", action="store_true", help="build user-default only, not every published profile")
    ap.add_argument("--emu", action="store_true", help="then the emulator smoke checks on the final build")
    ap.add_argument("--only", metavar="CHECKS", help="with --emu: these checks only (tests/emu/run.py --list)")
    a = ap.parse_args(argv)
    for f in a.config or []:
        if not Path(f).is_file():
            print(f"gate: no config file {f}", file=sys.stderr)
            return 2
    LOGS.mkdir(parents=True, exist_ok=True)
    head = git("rev-parse", "--short", "HEAD")
    dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    print(f"gate: {git('rev-parse', '--abbrev-ref', 'HEAD')} {head}" + (" (uncommitted changes: the gate tests "
          "the tree as it is)" if dirty else ""), flush=True)
    t0, fails, final_ok = time.time(), [], False
    for i, (name, cmd) in enumerate(plan(a), 1):
        if name == "emu" and not final_ok:
            print("skip  emu (no final build)")
            fails.append(name)
            continue
        ok, _ = run_step(i, name, cmd)
        final_ok = ok if name == "final" else final_ok
        if not ok:
            fails.append(name)
    took = time.time() - t0
    if fails:
        print(f"gate: FAILED on {head}: {', '.join(fails)} ({took / 60:.1f} min)")
        return 1
    print(f"gate: PASSED on {head}{' (dirty)' if dirty else ''} ({took / 60:.1f} min)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

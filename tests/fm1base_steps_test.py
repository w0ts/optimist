#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The release's update loader (felucca-bsp ldr_core.c, LDR_STEPS from fm1-base's fm1.toml app_steps) clears on a MEMORY
step switch exactly what Optimist's rule says (tools/flash_layout.py switch_clear_ends, docs/SD-MODE-DESIGN.md 9.3):
for every step's release end, the loader built as fm1-base's tools/build.py ldr_step_flags builds it, against every end
an FM-1 may have installed (the classic and the release ones, no head, an end below normal's, an end no step has), the
erased range read back from a simulated flash. Nothing at or past CLEAR_TOP (the kits, presets, settings) is erased.
  python3 tests/fm1base_steps_test.py"""
import os
import shutil
import subprocess
import sys
import tomllib

from builder_path import ROOT
import flash_layout as L

FB = ROOT / "fm1base"
OUT = ROOT / "build" / "fm1base_steps_test"
bad = 0


def check(what, ok):
    global bad
    print(f"{what:<110} {'ok' if ok else 'FAIL'}")
    bad += not ok


def step_flags(conf, end):
    """fm1base/tools/build.py ldr_step_flags, for an app built with FM1B_APP_END=end"""
    steps = [tuple(int(v) for v in row) for row in conf["app_steps"]]
    step, reloc = next((s, r) for e, s, r in steps if e == end)
    lst = " ".join(f"X({e:#x}u,{s},{r:#x}u)" for e, s, r in steps)
    return [f"-DFL_APP_END={end:#x}u", "-DLDR_SLOT_SWITCH=1", f"-DLDR_STEPS(X)={lst}", f"-DLDR_STEP={step}",
            f"-DLDR_RELOC_HI={reloc:#x}u", f"-DLDR_CLEAR_TOP={conf['app_clear_top']:#x}u"]


def main():
    cc = os.environ.get("CC", "cc")
    if not shutil.which(cc) or not (FB / "tests" / "ldr_slot_test.c").is_file():
        print(f"skip: no {cc} or no fm1base checkout")
        return 0
    conf = tomllib.loads((FB / "optimist" / "fm1.toml").read_text())
    check("fm1.toml app_steps: every end an FM-1 may have of ours, with its step and R (flash_layout.known_ends)",
          {int(e): (int(s), int(r)) for e, s, r in conf.get("app_steps", [])} == L.known_ends()
          and conf.get("app_clear_top") == L.CLEAR_TOP)
    olds = [0, 0x5000, *L.known_ends(), 0xC0000, 0xD5000]
    OUT.mkdir(parents=True, exist_ok=True)
    for k in L.ORDER:
        end = L.STEPS[k].release_end
        exe = OUT / f"ldr_{end:x}"
        subprocess.run([cc, "-O1", *step_flags(conf, end), "-o", str(exe), str(FB / "tests" / "ldr_slot_test.c")],
                       check=True, cwd=FB)
        res = subprocess.run([str(exe), "--range", *(hex(o) for o in olds)], capture_output=True, text=True)
        got = {int(a, 16): (int(lo, 16), int(hi, 16)) for a, lo, hi in (ln.split() for ln in res.stdout.split("\n") if ln)}
        want = {o: L.switch_clear_ends(o, end) for o in olds}
        for o in olds:
            if got.get(o) != want[o]:
                print(f"  LAYOUT {k} ({end:#x}) after {o:#x}: loader {got.get(o)}, rule {want[o]}")
        check(f"LAYOUT {k} {L.STEPS[k].key:<5} (release end {end:#x}): the loader's clear is the rule's after each of "
              f"{len(olds)} installed ends", res.returncode == 0 and got == want
              and all(hi <= L.CLEAR_TOP for _, hi in got.values()))
    shutil.rmtree(OUT, ignore_errors=True)
    print(f"fm1base_steps_test: {'FAILED' if bad else 'passed'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

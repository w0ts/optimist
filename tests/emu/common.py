# SPDX-License-Identifier: GPL-3.0-only
"""What every emulator check shares: the run context (the firmware, its facts, the output directory), the PASS /
FAIL lines and the sessions it opens (tests/emu/run.py)."""
import shutil
import time
from pathlib import Path

import fwfacts as F
import panel as PN
import session as S


class Ctx:
    """one check's run: ctx.check(name, ok, detail) prints a PASS / FAIL line; ctx.open() a session on the package"""

    def __init__(self, fw, out, mhz=96, check="", verbose=False):
        self.fw = Path(fw).resolve()
        self.facts = F.Facts(self.fw)
        self.name = check
        self.out = Path(out) / check
        self.mhz, self.verbose = mhz, verbose
        self.passed, self.failed = 0, 0
        self.layout = None
        if self.out.exists():
            shutil.rmtree(self.out)
        (self.out / "shots").mkdir(parents=True)
        self.pkg = S.stage(self.fw, self.out)
        self.t0 = time.time()

    def check(self, name, ok, detail=""):
        ok = bool(ok)
        self.passed += ok
        self.failed += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {self.name}: {name}" + (f" ({detail})" if detail else ""), flush=True)
        return ok

    def info(self, name, detail=""):
        print(f"info  {self.name}: {name}" + (f": {detail}" if detail else ""), flush=True)

    def open(self, fresh=True, settle=3.0):
        """a session on the staged package (its flash state: out/state/, fresh: none) -> (Emu, Panel); the first
        measures the track layout (empty tracks: a fresh boot or a seeded flash)"""
        e = S.Emu(self.pkg, self.out, fresh=fresh, mhz=self.mhz)
        e.run(settle)
        if self.layout is None:                 # (a fresh boot, or a seeded flash with no project: empty tracks)
            self.layout = PN.Layout(e)
        return e, PN.Panel(e, self.facts, self.layout, str(self.out / "shots"))

    def wav(self, name):
        return str(self.out / f"{name}.wav")

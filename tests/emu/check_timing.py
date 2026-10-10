# SPDX-License-Identifier: GPL-3.0-only
"""timing: tests/emu_boot_check.py on the package, at 48, 96 and 192 MHz: a clean audio start, no late half, every half
within its 5805 us, one boot (and CPU1's jobs in a FELUCCA_DUAL build). Its own play_check runs (no flash state
kept: its scratch state goes to the check's directory)."""
import os
import subprocess
import sys

import session as S

CLOCKS = ("48", "96", "192")


def run(ctx):
    env = dict(os.environ, FM1_EMU=str(S.checkout_dir()), TMPDIR=str(ctx.out))   # (its scratch state: here)
    p = subprocess.run([sys.executable, str(S.ROOT / "tests" / "emu_boot_check.py"), str(ctx.fw), "3", *CLOCKS],
                       capture_output=True, text=True, env=env)
    lines = [ln for ln in p.stdout.splitlines() if ln.startswith(("ok", "FAIL"))]
    for ln in lines:
        ok = ln.startswith("ok")
        ctx.check("emu_boot_check " + ln[4:].strip().split(":")[0], ok, ln[4:].strip().split(":", 1)[-1].strip())
    if len(lines) != len(CLOCKS):
        ctx.check("emu_boot_check ran at every clock", False,
                  f"rc {p.returncode}: {(p.stdout + p.stderr).strip()[-300:]}")
    elif p.returncode:
        ctx.check("emu_boot_check exit status", False, f"rc {p.returncode}")

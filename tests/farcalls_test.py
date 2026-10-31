#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/farcalls.py, the build's check for a direct call between RAM code and XIP, on small disassemblies in objdump's
pi32v2 form: a real call from RAM to a function in XIP (and back) fails; a jump table's data word that decodes as a
call (2026-10-31: 'aa ea 04 02' in bk_commit_sec, 'call -2882552' to an address with no code) passes; a call on the same
side passes. Run from the repo root:
  python3 tests/farcalls_test.py"""
import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
farcalls = importlib.import_module("farcalls")
failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def insn(pc, text, raw="00 00 00 00 "):
    return f" {pc:x}:    {raw}\t{text}"


XIP_FN = ["xip_fn:", insn(0x2000100, "r0 = 1"), insn(0x2000102, "return")]
RAM_FN = ["ram_fn:", insn(0x1C00100, "r0 = 1"), insn(0x1C00102, "return")]

real = ["ram_caller:", insn(0x1C00200, "call 4194044 <xip_fn : 2000100 >")] + XIP_FN
ok(farcalls.far_calls("\n".join(real)) == ["0x1c00200 -> 0x2000100"], "a real call from RAM into an XIP function: flagged")
back = ["xip_caller:", insn(0x2000200, "call -4194564 <ram_fn : 1c00100 >")] + RAM_FN
ok(farcalls.far_calls("\n".join(back)) == ["0x2000200 -> 0x1c00100"], "a real call from XIP into a RAM function: flagged")
bare = ["ram_caller:", insn(0x1C00200, "call 4194044")] + XIP_FN
ok(farcalls.far_calls("\n".join(bare)) == ["0x1c00200 -> 0x2000100"], "... also the bare form (label on the next line)")
same = ["ram_caller:", insn(0x1C00200, "call -256 <ram_fn : 1c00100 >")] + RAM_FN
ok(farcalls.far_calls("\n".join(same)) == [], "a call inside RAM: passes")

# a RAM jump table: its word 'aa ea 04 02' (0x0204eaaa) decodes as a call at 0x204eae0 -> 0x1d8eeec, no code there
table = ["bk_commit_sec:", insn(0x204EADC, "tbh [r0]"), "", ".GJTIS1_0_0_:",
         insn(0x204EAE0, "call -2882552", "aa ea 04 02 ")]
ok(farcalls.far_calls("\n".join(table)) == [], "a jump table's word that reads as a call to no code: passes")
# the table's own label is not a function: a call landing on it is not counted
onlabel = ["f:", insn(0x2000300, "call -4194308"), "", ".GJTIS2_0_0_:", insn(0x1C00300, "r0 = 1")]
ok(farcalls.far_calls("\n".join(onlabel)) == [], "a target that is a jump table's label: passes")

print("farcalls_test: " + ("FAILED" if failed else "ok"))
sys.exit(1 if failed else 0)

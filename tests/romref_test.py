#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/romref.py, the check that nothing calls or reads the chip's ROM (0xFFC00000..0xFFCFFFFF), on small
disassemblies in objdump's pi32v2 form: a call into the window fails; a constant in the window loaded into a register
fails when the code then uses that register as an address (a load, a store, a call or a jump through it), and passes
when it stays a number (2026-10-29: pi32v2-llvm keeps -3,230,250 in a register for a range check, 0xFFCEB5D6, never an
address); fm1.toml rom_check_skip still skips a function. Run from the repo root:
  python3 tests/romref_test.py"""
import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
romref = importlib.import_module("romref")
failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def dis(*body, fn="f"):
    return "\n".join([f"{fn}:"] + [f" {0x2000000 + 4 * i:x}:    00 00 00 00 \t{t}" for i, t in enumerate(body)])


ROM = "r0 = 4291737046 <_pi_load+0xEFCE8408 : ffceb5d6 >"
ok(romref.rom_refs(dis("call -4 <rom_fn : ffc01234 >")) == [0xFFC01234], "a call into the ROM window: flagged")
ok(romref.rom_refs(dis(ROM, "r1 = [r0+4]")) == [0xFFCEB5D6], "a ROM constant then read through: flagged")
ok(romref.rom_refs(dis(ROM, "r2 = r2 + 1", "[r0] = r2")) == [0xFFCEB5D6], "... written through: flagged")
ok(romref.rom_refs(dis(ROM, "call r0")) == [0xFFCEB5D6], "... called through: flagged")
ok(romref.rom_refs(dis(ROM, "[sp+160] = r0", "r1 = r1 + r0", "if (r1 < r2) goto 8 <f+0x10 : 2000010 >")) == [],
   "a ROM-valued number kept on the stack and added (pi32v2-llvm's range check): passes")
ok(romref.rom_refs(dis(ROM, "r0 = 5", "r1 = [r0+4]")) == [], "the register written again before the load: passes")
ok(romref.rom_refs(dis(ROM, "r2 = r3", "r2 = r3", "r2 = r3", "r2 = r3", "r1 = [r0+4]")) == [],
   "a use past the window of instructions looked at: passes (a number by then)")
ok(romref.rom_refs(dis("r0 = 12345 <x : 3039 >", "r1 = [r0+4]")) == [], "a constant outside the window: passes")
ok(romref.rom_refs(dis(ROM, "r1 = [r0+4]", fn="vendored"), {"vendored"}) == [], "fm1.toml rom_check_skip: skipped")
ok(romref.rom_refs(dis(ROM) + "\n" + dis("r1 = [r0+4]", fn="g")) == [],
   "the next function's load is not this constant's use")
print("romref: FAILED" if failed else "romref: ok")
sys.exit(1 if failed else 0)

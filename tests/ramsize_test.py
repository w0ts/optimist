#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/ramsize.py on a made-up symbol table: the SRAM's symbols only (not the flash's), each byte counted once (an
alias, a member of llvm's .L_MergedGlobals inside its group), a section's padding and the gaps between sections, the
stacks as their own rows, the owner of a static as its function's file."""
import sys
from pathlib import Path

from tools_path import TOOLS  # (tools/ on sys.path)
import ramsize as R

NM = """01c00000 00000100 t hot_loop
01c00100 00000010 d table
01c00200 00000080 b ring
01c00200 00000080 b ring_alias
01c00280 00000040 b .L_MergedGlobals
01c00280 00000008 b small_a
01c00300 00000100 b pool_buf
02000120 00001000 T flash_only
01c7c000 00000020 b kept_note
"""
SECS = """  [ 2] .ram_text          PROGBITS        01c00000 0bf000 000100 00  AX  0   0  2
  [ 4] .data              PROGBITS        01c00100 0cd920 000010 00  WA  0   0  4
  [ 5] .bss               NOBITS          01c00200 0ce960 0000c8 00  WA  0   0 32
  [ 6] .pool              NOBITS          01c00300 0ce960 000100 00  WA  0   0  4
  [ 7] .noinit            NOBITS          01c7c000 0cf000 000020 00  WA  0   0  4
"""


def main():
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(f"{'ok  ' if ok else 'FAIL'} {what}")
        fails += not ok

    check(Path(R.__file__).resolve() == (TOOLS / "ramsize.py").resolve(), "tools/ramsize.py the one tested")
    syms, secs = R.parse_nm(NM), R.parse_sections(SECS)
    check(len(syms) == 9 and len(secs) == 5, "the nm and readelf lines parsed")
    u = R.unique(syms)
    names = {s[3]: s[1] for s in u}
    check("flash_only" not in names, "the flash's symbols left out")
    check(names.get("ring") == 0x80 and "ring_alias" not in names, "an alias counted once")
    check(names.get("small_a") == 8 and names.get(".L_MergedGlobals") == 0x38,
          "a merged group: its named member, then the rest of the group alone")
    rows = dict(R.table(syms, secs, "section", {}))
    check(rows.get(".bss") == 0xC0 and rows.get(".bss (padding, unnamed)") == 8, ".bss: its symbols and its padding")
    check(rows.get("gap .data .. .bss") == 0x200 - 0x110, "the gap between .data and .bss")
    check(rows.get("main stack (usp)") == 0x6000 and rows.get("interrupt stack (ssp)") == 0x2000, "the stacks as rows")
    check(rows.get(".noinit") == 0x20, "the NOINIT region counted")
    idx = {"hot_loop": "a.c", "fn": "b.c"}
    check(R.owner_of("fn.counter", idx) == "b.c" and R.owner_of("nobody", idx) == "?", "a static's owner: its function's")
    print(f"ramsize_test: {'FAILED' if fails else 'passed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

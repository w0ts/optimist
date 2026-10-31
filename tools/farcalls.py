# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The check for a direct call between RAM code and XIP code (core.h HOT, FAR), over the image's disassembly.

A direct call between the two is out of reach: the linker should refuse it, and this catches one that wrapped instead.
The disassembly is objdump's (pi32v2), and objdump decodes everything in a code section as instructions, jump tables
included. A table's data word can look like a call ('aa ea 04 02', an address, reads as 'call -2882552'): whether it does
depends on where the code lands, so a check that believed it failed at random when the layout moved (2026-10-31,
fm1-next-presets e61dc755: a table in bk_commit_sec read as a call from 0x204eae0 to 0x1d8eeec, where no code sits).
The toolchain writes no data mapping symbols ($d), and a table's end is not marked, so the check goes by the target:
a call goes to the start of a function (a label of the disassembly). Data decoded as a call lands anywhere; a real
call, RAM to XIP or back, lands on a function. The labels of the jump tables themselves (.GJTI...) are not functions.

  python3 tools/farcalls.py felucca.dis     (prints the calls found)"""
import re
import sys

LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")
LABEL = re.compile(r"^([A-Za-z_$.][\w.$]*):\s*$")
CALL = re.compile(r"\bcall (-?\d+)\b")
XIP = 0x02000000                    # code at or above this address is flash (XIP), below it RAM


def function_starts(dis):
    """-> the set of addresses where a function (a label that is not a jump table's) begins"""
    starts, pending = set(), False
    for ln in dis.splitlines():
        if ln[:1] not in (" ", "\t", "") and LABEL.match(ln.strip()):
            pending = not ln.startswith(".GJTI")
            continue
        m = LINE.match(ln)
        if m and pending:
            starts.add(int(m.group(1), 16))
        if m:
            pending = False
    return starts


def far_calls(dis):
    """-> ["0xPC -> 0xTARGET", ...]: the direct calls whose two ends are on different sides (RAM, XIP), the target
    a function. Target = pc + offset + 4 (also for the conditional form 'if (..) { call N }', whose label is
    printed on the next line)."""
    starts, far = function_starts(dis), []
    for ln in dis.splitlines():
        mm = LINE.match(ln)
        c = CALL.search(mm.group(3)) if mm else None
        if not c:
            continue
        pc = int(mm.group(1), 16)
        target = (pc + int(c.group(1)) + 4) & 0xFFFFFFFF
        if (pc >= XIP) != (target >= XIP) and target in starts:
            far.append(f"{pc:#x} -> {target:#x}")
    return far


if __name__ == "__main__":
    print("\n".join(far_calls(open(sys.argv[1]).read())) or "none")

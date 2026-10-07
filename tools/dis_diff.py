#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""compare two objdump -d listings function by function (address-normalised bodies).
usage: tools/dis_diff.py A.dis B.dis (two build/felucca.dis; docs/DSP-SHARED.md: a refactor that keeps every function identical keeps every sample and cycle)   -> functions only in A / only in B / changed (size A -> B, RAM or XIP)"""
import re
import sys

FN = re.compile(r"^([A-Za-z_.$][^ :]*):\s*$")
LN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")


def parse(path):
    funcs, cur = {}, None
    for ln in open(path, errors="replace"):
        m = FN.match(ln)
        if m:
            cur = m.group(1)
            funcs[cur] = [None, 0, []]
            continue
        m = LN.match(ln)
        if m and cur:
            f = funcs[cur]
            if f[0] is None:
                f[0] = int(m.group(1), 16)
            f[1] += len(m.group(2).split())
            ins = re.sub(r"<[^>]*>", "<>", m.group(3))
            ins = re.sub(r"-?\b\d{3,}\b", "N", ins)
            f[2].append(ins.strip())
    return {k: v for k, v in funcs.items() if v[0] is not None and not k.startswith(".")}


def where(a):
    return "RAM" if 0x01c00000 <= a < 0x01c08000 else "XIP" if a >= 0x02000000 else "DAT"


a, b = parse(sys.argv[1]), parse(sys.argv[2])
rows = []
for k in sorted(set(a) | set(b)):
    if k not in b:
        rows.append(f"  - {k:32s} {a[k][1]:6d} -> gone   {where(a[k][0])}")
    elif k not in a:
        rows.append(f"  + {k:32s}   new  -> {b[k][1]:6d} {where(b[k][0])}")
    elif a[k][2] != b[k][2]:
        rows.append(f"  ~ {k:32s} {a[k][1]:6d} -> {b[k][1]:6d} {where(b[k][0])} ({b[k][1] - a[k][1]:+d})")
print("\n".join(rows) if rows else "  (every function identical)")

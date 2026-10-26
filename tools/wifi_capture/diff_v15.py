#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What JieLi's Wi-Fi quick start (the capture image's pass 2) writes that stock V15's radio start-up (boot 2) does not,
and the reverse: both traces' writes as (address, value), aligned (difflib), the differing runs listed with the
writing function on the capture side. tools/wifi_capture/README.md.
  tools/wifi_capture/diff_v15.py <pass2.txt from split.py> <trace_v15.txt> [--until-14000]"""
import collections
import difflib
import sys


def writes_pass2(path):
    out = []
    for line in open(path):
        f = line.split()             # name step tick pc rw addr size value
        if f[4] == "W":
            out.append((int(f[5], 16), int(f[7], 16), f[0]))
    return out


def writes_v15(path, until_bt):
    out = []
    for line in open(path):
        if line.startswith("#"):
            continue
        f = line.split()
        if f[3] != "W":
            continue
        a, v = int(f[4], 16), int(f[6], 16)
        out.append((a, v, f[2]))
    return out


def main() -> int:
    cap = writes_pass2(sys.argv[1])
    v15 = writes_v15(sys.argv[2], "--until-14000" in sys.argv)
    a = [(x[0], x[1]) for x in cap]
    b = [(x[0], x[1]) for x in v15]
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    same = sum(bl.size for bl in sm.get_matching_blocks())
    print(f"capture pass 2: {len(a)} writes; V15 boot 2: {len(b)} writes; in common (aligned): {same}")
    only_cap, only_v15 = collections.Counter(), collections.Counter()
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal":
            continue
        for x in cap[i1:i2]:
            only_cap[(x[2], x[0] >> 8)] += 1
        for x in v15[j1:j2]:
            only_v15[x[0] >> 8] += 1
    print("only in the capture (function, address block):")
    for (fn, blk), n in only_cap.most_common(25):
        print(f"  {n:7} {fn:28} {blk << 8:#07x}")
    print("only in V15 (address block):")
    for blk, n in only_v15.most_common(15):
        print(f"  {n:7} {blk << 8:#07x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

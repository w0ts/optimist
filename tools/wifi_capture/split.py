#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Split a capture trace (diagnose FM1_MMIO_TRACE) into its two passes at wifi_rf_off and count the writes of each by
the function that made them (the image's nm output for the names). tools/wifi_capture/README.md.
  tools/wifi_capture/split.py <trace.txt> <image.syms> [--write-pass2 OUT]"""
import bisect
import collections
import sys


def load_syms(path):
    syms = []
    for line in open(path):
        p = line.split()
        if len(p) == 3 and p[1] in "Tt" and p[0].startswith("02"):
            syms.append((int(p[0], 16), p[2]))
    syms.sort()
    return [a for a, _ in syms], syms


def main() -> int:
    addrs, syms = load_syms(sys.argv[2])
    out = open(sys.argv[4], "w") if len(sys.argv) > 4 and sys.argv[3] == "--write-pass2" else None
    passes = [collections.Counter(), collections.Counter()]
    cur, off = 0, False
    for line in open(sys.argv[1]):
        if line.startswith("#"):
            continue
        f = line.split()
        i = bisect.bisect_right(addrs, int(f[2], 16)) - 1
        name = syms[i][1] if i >= 0 else "?"
        if name == "wifi_rf_off":
            off = True
        elif off:
            cur = 1
        if cur and out:
            out.write(f"{name} {line}")
        if f[3] == "W":
            passes[cur][name] += 1
    for n, c in enumerate(passes):
        print(f"pass {n + 1}: {sum(c.values())} writes")
        for k, v in c.most_common(30):
            print(f"  {v:8} {k}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

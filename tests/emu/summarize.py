#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""emu-pass: one table engine x result from the sweeps' results.json (tests/emu/sweep.py).

  python3 tests/emu/summarize.py RUN_DIR[=LABEL] ...      (Markdown table on stdout)"""
import json
import sys
from pathlib import Path

HALF_US = 5804


def flags(r):
    n, c = r["note"], r["chord"]
    f = []
    if c["peak"] < 0.001 and n["peak"] < 0.001:
        f.append("silent")
    if c["full"] or n["full"]:
        f.append("full-scale")
    if max(c["step"], n["step"]) > 0.5:
        f.append("jump")
    if r["late"]:
        f.append("late")
    if r["boots"] != 1 or r["crash"]:
        f.append("reset")
    return f


def main():
    rows = {}
    for arg in sys.argv[1:]:
        d, _, label = arg.partition("=")
        label = label or Path(d).name
        for r in json.loads((Path(d) / "results.json").read_text()):
            if r.get("a2") or r.get("skip"):
                continue
            if r.get("fault"):
                rows.setdefault(("FAULT", label), []).append(r)
                continue
            rows.setdefault((r["engine"], label), []).append(r)
    print("| Engine | Build | Presets played | Chord peak min..max | Note peak min | Slowest half (us of 5804) | Flags | Faults |")
    print("|---|---|---|---|---|---|---|---|")
    for (eng, label), rs in sorted(rows.items()):
        if eng == "FAULT":
            print(f"| (fault) | {label} | - | - | - | - | - | {len(rs)}: {rs[0]['fault'][:120]} |")
            continue
        fl = sorted({x for r in rs for x in flags(r)})
        cp = [r["chord"]["peak"] for r in rs]
        mu = max(r["max_us"] for r in rs)
        names = ", ".join(r["name"] for r in rs[:3])
        print(f"| {eng} | {label} | {len(rs)} ({names}{', ...' if len(rs) > 3 else ''}) | {min(cp):.3f}..{max(cp):.3f} | "
              f"{min(r['note']['peak'] for r in rs):.3f} | {mu} ({100 * mu // HALF_US}%) | {', '.join(fl) or 'none'} | 0 |")


if __name__ == "__main__":
    main()

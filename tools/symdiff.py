#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What a builder item costs, symbol by symbol: two measurement builds (the defaults, then with KEY=V ...) side by
side, each in its own build directory, and the difference of their symbol tables, largest first, with the firmware
source file that defines each function or object (the one-file build has no file in its symbols: found by name).

  python3 tools/symdiff.py KEY=V [KEY=V ...] [--base KEY=V ...] [--top N] [--region flash|ram]

flash: .text, .rodata and the RAM code sections' copies (what is in the image); ram: .data and .bss.
"""
import argparse
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import configure as C

FLASH = (".text", ".rodata", ".ram_text", ".ram_hot", ".ram_hot2", ".data")    # (.data: its initial values are in the image)
RAM = (".data", ".bss", ".ram_hot2")


def kv(items):
    out = {}
    for s in items or ():
        k, _, v = s.partition("=")
        if k not in C.R.ITEMS or not v.lstrip("-").isdigit():
            raise SystemExit(f"symdiff: {s}: KEY=number, KEY an item of the builder")
        out[k] = int(v)
    return out


def symbols(slot):
    """-> {name: (section, size)} of the slot's build/felucca.elf (objdump -t, through the toolchain)"""
    sys.path.insert(0, str(ROOT / "tools"))
    import toolchain as TC
    elf = (slot / "felucca.elf").relative_to(ROOT).as_posix()
    cmd = TC.resolve().command("common/bin/objdump", ["-t", elf], ROOT)
    text = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True).stdout
    out = {}
    for line in text.splitlines():
        m = re.match(r"^[0-9a-f]+ (.{7}) (\S+)\s+([0-9a-f]+) (\S+)$", line)
        if m and m.group(2).startswith(".") and int(m.group(3), 16):
            out[m.group(4)] = (m.group(2), int(m.group(3), 16))
    return out


def where(name, cache={}):
    """the firmware source that defines name (a function or an object at file scope), else '?'"""
    if not cache:
        for p in (ROOT / "firmware").rglob("*.[ch]"):
            cache[p] = p.read_text(errors="replace")
    pat = re.compile(rf"^[A-Za-z_][^;(=]*\b{re.escape(name)}\s*(\(|\[|=|;)", re.M)
    for p, text in cache.items():
        if pat.search(text):
            return p.relative_to(ROOT / "firmware").as_posix()
    return "?"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("set", nargs="+", help="KEY=V: what the second build changes")
    ap.add_argument("--base", nargs="*", help="KEY=V: what both builds change from the defaults")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--region", choices=("flash", "ram"), default="flash")
    a = ap.parse_args(argv)
    base = dict(C.defaults(), **kv(a.base))
    other = dict(base, **kv(a.set))
    slots = [ROOT / "build" / "slots" / "symdiff-a", ROOT / "build" / "slots" / "symdiff-b"]
    with ThreadPoolExecutor(2) as ex:
        res = list(ex.map(lambda x: C.build(x[0], x[1], measure=True, outdir=x[2]),
                          [(base, "symdiff-a", slots[0]), (other, "symdiff-b", slots[1])]))
    for (ok, _, out), s in zip(res, slots):
        if not ok:
            raise SystemExit(f"symdiff: the build in {s} failed:\n{out[-1500:]}")
    sa, sb = symbols(slots[0]), symbols(slots[1])
    secs = FLASH if a.region == "flash" else RAM
    rows = []
    for n in set(sa) | set(sb):
        x = sa.get(n, ("", 0))[1] if sa.get(n, ("",))[0] in secs else 0
        y = sb.get(n, ("", 0))[1] if sb.get(n, ("",))[0] in secs else 0
        if x != y:
            rows.append((y - x, n, (sb.get(n) or sa.get(n))[0]))
    rows.sort(key=lambda r: -abs(r[0]))
    total = sum(d for d, _, _ in rows)
    by_file = {}
    for d, n, _ in rows:
        f = where(n)
        by_file[f] = by_file.get(f, 0) + d
    print(f"{' '.join(a.set)}: {total:+,} B of {a.region} in symbols ({len(rows)} symbols)\n")
    print("by source file:")
    for f, d in sorted(by_file.items(), key=lambda x: -abs(x[1]))[:25]:
        print(f"  {d:+8,}  {f}")
    print(f"\nlargest {a.top}:")
    for d, n, sec in rows[:a.top]:
        print(f"  {d:+8,}  {n:40s} {sec:10s} {where(n)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

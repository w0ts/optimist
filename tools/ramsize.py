#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Where a build's SRAM goes: an ELF's symbols in the SRAM (0x01C00000 .. the stacks' top) and the NOINIT region, by
section, by owner (the source that defines each symbol, Optimist's firmware/ or fm1-base's) or by symbol, largest
first; each section's padding (its size less its symbols) and the gaps between sections on their own lines. The SRAM
counterpart of tools/symsize.py (flash only), for a release build's ELF (tools/optimist.py build: fm1base/optimist/
build/optimist.elf) or a classic one (build/felucca.elf).

  python3 tools/ramsize.py ELF [--by section|owner|dir|symbol] [--top N] [--nm PATH]

The symbols are read with an nm that knows the ELF's symbol table (llvm-nm; the toolchain's objdump -t works too
through --nm objdump:PATH). Only a symbol's address and size are used: the CPU does not matter."""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRAM = (0x01C00000, 0x01C7C000)                 # the packed region and the stacks (device/app.ld)
NOINIT = (0x01C7C000, 0x01C7FD50)
STACKS = (("main stack (usp)", 0x01C74000, 0x01C7A000), ("interrupt stack (ssp)", 0x01C7A000, 0x01C7C000))   # (app.ld's
# defaults, each with its guard band: an ELF's own _guard0 / _guard1 / _sstack_top instead, stacks_of)
# (fm1-base first: a release build compiles its copies of the radio stacks and the BSP, which firmware/ also holds)
SOURCES = [ROOT / "fm1base" / d for d in ("device", "bt", "ble", "wifi", "link", "bsp", "optimist", "lib", "include")] + [
    ROOT / "firmware"]
DEF = re.compile(r"^[A-Za-z_][^;(={}#]*?\b([A-Za-z_]\w*)\s*(?:[A-Z_][A-Z0-9_]*\s*)*(?:\[|=|;|\(|,)", re.M)   # (a trailing IN_POOL-like macro too)
CLOSE = re.compile(r"^\}\s*([A-Za-z_]\w*)\s*(?:[A-Z_][A-Z0-9_]*\s*)*(?:\[|=|;|\(|,)", re.M)        # (static struct { .. } name;)
ATTR = re.compile(r'__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)|__asm__\s*\("[^"]*"\)')


def find_nm(arg):
    for c in ([arg] if arg else []) + [os.environ.get("NM", ""), "llvm-nm", "/opt/homebrew/opt/llvm/bin/llvm-nm",
                                       "/usr/local/opt/llvm/bin/llvm-nm"]:
        if c and (shutil.which(c) or Path(c).is_file()):
            return c
    raise SystemExit("ramsize: no llvm-nm (--nm PATH, or NM=)")


def parse_nm(text):
    """llvm-nm -S output -> [(addr, size, type, name)]"""
    out = []
    for ln in text.splitlines():
        p = ln.split()
        if len(p) == 4 and re.fullmatch(r"[0-9a-fA-F]+", p[0]) and re.fullmatch(r"[0-9a-fA-F]+", p[1]):
            out.append((int(p[0], 16), int(p[1], 16), p[2], p[3]))
    return out


def stacks_of(syms):
    """the stacks where the ELF's linker script put them (fm1.toml main_stack / irq_stack move them): STACKS"""
    global STACKS
    at = {n: a for a, _, _, n in syms}
    if all(k in at for k in ("_guard0", "_guard1", "_sstack_top")):
        STACKS = (("main stack (usp)", at["_guard0"], at["_guard1"]),
                  ("interrupt stack (ssp)", at["_guard1"], at["_sstack_top"]))


def parse_sections(text):
    """llvm-readelf -S output -> [(name, addr, size)]"""
    out = []
    for ln in text.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)", ln)
        if m:
            out.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    return out


def owners(roots=SOURCES):
    """{name: source path} for every file-scope definition the sources hold (the first file that defines it)"""
    idx = {}
    for r in roots:
        if not r.is_dir():
            continue
        for p in sorted(r.rglob("*.[chS]")):
            if "build" in p.relative_to(r).parts:
                continue
            text = ATTR.sub(" ", p.read_text(errors="replace"))
            for pat in (DEF, CLOSE):
                for m in pat.finditer(text):
                    idx.setdefault(m.group(1), p.relative_to(ROOT).as_posix())
    return idx


def owner_of(name, idx):
    """a symbol's owner: its own name, else a function's static (llvm: func.var), else ?"""
    base = name.split(".")[0]
    return idx.get(name) or idx.get(base) or "?"


def in_ram(a):
    return SRAM[0] <= a < SRAM[1] or NOINIT[0] <= a < NOINIT[1]


def unique(syms):
    """the symbols with each byte counted once: named symbols first (an alias, a member of llvm's .L_MergedGlobals),
    each clipped to the bytes no symbol before it covered; the .L_ locals then for what is left"""
    out, cov = [], []                            # (cov: the covered intervals, merged, sorted)
    for a, n, t, name in sorted(syms, key=lambda s: (s[3].startswith(".L"), s[0], -s[1])):
        if not in_ram(a) or not n:
            continue
        left = n
        for lo, hi in cov:
            if lo < a + n and a < hi:
                left -= min(hi, a + n) - max(lo, a)
        if left > 0:
            out.append((a, left, t, name))
        cov = merge(cov + [(a, a + n)])
    return out


def merge(iv):
    out = []
    for lo, hi in sorted(iv):
        if out and lo <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], hi))
        else:
            out.append((lo, hi))
    return out


def table(syms, secs, by, idx):
    """-> [(key, bytes)] largest first"""
    ram_secs = [s for s in secs if s[2] and in_ram(s[1])]
    tot = {}
    for a, n, _, name in unique(syms):
        sec = next((s[0] for s in ram_secs if s[1] <= a < s[1] + s[2]), "?")
        key = {"section": sec, "symbol": name, "owner": owner_of(name, idx),
               "dir": owner_of(name, idx).rsplit("/", 1)[0]}[by]
        tot[key] = tot.get(key, 0) + n
    if by == "section":                          # each section's own padding, and the gaps between them
        for name, a, n in ram_secs:
            pad = n - tot.get(name, 0)
            if pad:
                tot[name + " (padding, unnamed)"] = pad
        lo = sorted((a, a + n, name) for name, a, n in ram_secs if SRAM[0] <= a < SRAM[1])
        for (_, e, x), (b, _, y) in zip(lo, lo[1:]):
            if b > e:
                tot[f"gap {x} .. {y}"] = b - e
        for nm, a, e in STACKS:
            tot[nm] = e - a
    return sorted(tot.items(), key=lambda x: -x[1])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("--by", choices=("section", "owner", "dir", "symbol"), default="owner")
    ap.add_argument("--top", type=int, default=60)
    ap.add_argument("--nm")
    a = ap.parse_args(argv)
    nm = find_nm(a.nm)
    syms = parse_nm(subprocess.run([nm, "-S", a.elf], capture_output=True, text=True, check=True).stdout)
    stacks_of(syms)
    readelf = str(Path(nm).with_name("llvm-readelf")) if Path(nm).is_absolute() else "llvm-readelf"
    secs = parse_sections(subprocess.run([readelf, "-S", a.elf], capture_output=True, text=True).stdout)
    idx = owners() if a.by in ("owner", "dir") else {}
    rows = table(syms, secs, a.by, idx)
    pend = max((s[1] + s[2] for s in secs if s[0] == ".pool"), default=0)
    print(f"{a.elf}: packed SRAM in use {pend - SRAM[0]:,} B (to the pool's end); free to the stacks "
          f"{STACKS[0][1] - pend:,} B; by {a.by}")
    for k, n in rows[:a.top]:
        print(f"{n:9,}  {k}")
    print(f"{sum(n for _, n in rows):9,}  (all {len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Where a configuration's flash goes: a measurement build of PROFILE (or a .config) in its own build directory, its
symbols summed by the firmware source that defines each (code and data apart), largest first; the size_fns.py list
of each file (SIZE minsize, Os, AUDIO) beside it.

  python3 tools/symsize.py PROFILE|FILE.config [--by dir|file] [--top N]
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import configure as C
import size_fns as SF
import symdiff as S


def kind(f):
    f = f.removeprefix("src/")
    return "SIZE" if f in SF.SIZE_FILES else "AUDIO" if f in SF.AUDIO_FILES else "Os" if f in SF.OS_FILES else "-"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config")
    ap.add_argument("--by", choices=("dir", "file"), default="file")
    ap.add_argument("--top", type=int, default=40)
    a = ap.parse_args(argv)
    p = Path(a.config)
    cfg = C.parse(p.read_text())[0] if p.is_file() else C.load_profile(a.config)[0]
    slot = ROOT / "build" / "slots" / "symsize"
    ok, sizes, out = C.build(cfg, "symsize", measure=True, outdir=slot)
    if not ok:
        raise SystemExit(f"symsize: the build failed:\n{out[-1500:]}")
    syms = S.symbols(slot)
    tot = {}
    for n, (sec, size) in syms.items():
        if sec not in S.FLASH:
            continue
        f = S.where(n)
        key = f.rsplit("/", 1)[0] if a.by == "dir" else f
        code, data = tot.get(key, (0, 0))
        tot[key] = (code + size, data) if sec in (".text", ".ram_text", ".ram_hot", ".ram_hot2") else (code, data + size)
    print(f"{a.config}: flash {sizes['flash']:,} (limit {C.LIMITS['flash']:,}); in symbols {sum(c + d for c, d in tot.values()):,}")
    print(f"{'code':>9} {'data':>9}  kind   source")
    for k, (c, d) in sorted(tot.items(), key=lambda x: -sum(x[1]))[:a.top]:
        print(f"{c:9,} {d:9,}  {kind(k) if a.by == 'file' else '':5s}  {k}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

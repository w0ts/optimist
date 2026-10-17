#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What grew between two commits, symbol by symbol: the same configuration built at OLD and at NEW (each a git
worktree of its own under build/symdiff-wt/, made with this checkout's sdk/ and venv), the difference of the symbol
tables largest first, with the source file defining each symbol (tools/symdiff.py's reading).

  python3 tools/symdiff_commits.py OLD NEW PROFILE|FILE.config [--top N] [--region flash|ram]
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import symdiff as S


def tree(rev):
    """a worktree of rev under build/symdiff-wt/<rev>, its sdk/ and venv this checkout's"""
    sha = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=10", rev], capture_output=True, text=True,
                         check=True).stdout.strip()
    wt = ROOT / "build" / "symdiff-wt" / sha
    if not wt.is_dir():
        subprocess.run(["git", "-C", str(ROOT), "worktree", "add", "-q", "--detach", str(wt), sha], check=True)
    for link, target in (("sdk", ROOT / "sdk"), ("tools/builder/venv", ROOT / "tools" / "builder" / "venv")):
        p = wt / link
        if not p.exists() and target.exists():
            p.symlink_to(target)
    return wt


def build(wt, cfg_text):
    """a measurement build in wt (its own build.py and builder) -> the elf's symbols"""
    cfg = wt / "build" / "symdiff.config"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text(cfg_text)
    py = wt / "tools" / "builder" / "venv" / "bin" / "python"
    env = {k: v for k, v in os.environ.items() if not k.startswith(("FELUCCA_", "OPTIMIST_BUILD_DIR"))}
    r = subprocess.run([str(py), "tools/build.py", "--config", str(cfg), "--measure"], cwd=wt, capture_output=True,
                       text=True, env=env)
    if r.returncode:
        raise SystemExit(f"symdiff_commits: the build in {wt} failed:\n{(r.stdout + r.stderr)[-1500:]}")
    old_root, S.ROOT = S.ROOT, wt                      # (symbols() reads the elf relative to its tree)
    try:
        return S.symbols(wt / "build")
    finally:
        S.ROOT = old_root


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("config", help="a profile name or a .config file (read from NEW's tree)")
    ap.add_argument("--top", type=int, default=50)
    ap.add_argument("--region", choices=("flash", "ram"), default="flash")
    a = ap.parse_args(argv)
    wts = [tree(a.old), tree(a.new)]
    p = Path(a.config)
    text = p.read_text() if p.is_file() else (wts[1] / "config" / "profiles" / f"{a.config}.config").read_text()
    with ThreadPoolExecutor(2) as ex:
        sa, sb = ex.map(lambda w: build(w, text), wts)
    secs = S.FLASH if a.region == "flash" else S.RAM
    rows = []
    for n in set(sa) | set(sb):
        x = sa[n][1] if n in sa and sa[n][0] in secs else 0
        y = sb[n][1] if n in sb and sb[n][0] in secs else 0
        if x != y:
            rows.append((y - x, n, "new" if n not in sa else "gone" if n not in sb else ""))
    rows.sort(key=lambda r: -abs(r[0]))
    by_file = {}
    for d, n, _ in rows:
        f = S.where(n)
        by_file[f] = by_file.get(f, 0) + d
    print(f"{a.config}: {a.old} -> {a.new}: {sum(d for d, _, _ in rows):+,} B of {a.region} in symbols ({len(rows)} symbols)\n")
    print("by source file:")
    for f, d in sorted(by_file.items(), key=lambda x: -abs(x[1]))[:30]:
        print(f"  {d:+8,}  {f}")
    print(f"\nlargest {a.top}:")
    for d, n, tag in rows[:a.top]:
        print(f"  {d:+8,}  {n:40s} {tag:5s} {S.where(n)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

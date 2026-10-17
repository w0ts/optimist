#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The flash, RAM, pool and RAMTEXT of a measurement build on a fast Linux x86-64 host, from this tree's firmware/ and
# tools/ as they are (uncommitted changes included): rsync to HOST:DIR, then tools/build.py --measure there.
#   tools/farm_size.sh [PROFILE] [KEY=V ...]   (the defaults without PROFILE; FARM_HOST, default gpufarm; FARM_DIR,
#                                              default optimist-costs)
# A clone of this repository with `python3 tools/optimist.py setup` done must be at HOST:DIR.
set -e
host=${FARM_HOST:-gpufarm}
dir=${FARM_DIR:-optimist-costs}
root=$(cd "$(dirname "$0")/.." && pwd)
rsync -a --delete "$root/firmware/" "$host:$dir/firmware/"
rsync -a --exclude venv --exclude __pycache__ "$root/tools/" "$host:$dir/tools/"
ssh "$host" "cd $dir && tools/builder/venv/bin/python - $*" <<'EOF'
import sys
sys.path.insert(0, "tools/builder")
import configure as C
args = sys.argv[1:]
cfg = C.load_profile(args.pop(0))[0] if args and "=" not in args[0] else C.defaults()
for s in args:
    k, _, v = s.partition("=")
    cfg[k] = int(v)
ok, sizes, out = C.build(cfg, "farm-size", measure=True, outdir=C.ROOT / "build" / "slots" / "farm-size")
if not ok:
    print(out[-2500:])
    sys.exit(1)
print(" ".join(f"{r} {sizes[r]:,}" for r in ("flash", "ram", "pool", "ramtext")))
EOF

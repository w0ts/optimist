#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copy jank's Launchpad core (github.com/w0ts/jank core/, its docs/launchpad.md "Using the core on another unit") into third_party/jank.
# jank is its source: change it there, then run this (the owner, 2026-11-26: "jank is the source, copied"). Optimist's own files in
# third_party/jank (jank_lpscales.[ch]: the FM-1's scales; jank_fm1.h: the build's switches; SOURCE) are left as they are, and the
# Digitone's Session page and the jank firmware's queue (jank_lpsess*, jank_lpout*) are not taken.
#   tools/sync_jank.sh [JANK_DIR]      JANK_DIR: a jank checkout (default ../jank beside this repository), at the commit wanted
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
src=${1:-$here/../jank}
dst=$here/third_party/jank
[ -d "$src/core" ] || { echo "sync_jank: no jank checkout at $src"; exit 1; }
mkdir -p "$dst"
n=0
for b in jank_lpunit.h jank_lpstr.h jank_lpact.h jank_lpapp_int.h jank_dn2live.h \
         jank_lpx.c jank_lpx.h jank_lpnote.c jank_lpnote.h jank_lpapp.c jank_lpapp.h jank_lpstep.c jank_lpstep.h \
         jank_lpmix.c jank_lpmix.h jank_lptrans.c jank_lptrans.h jank_lpclips.c jank_lpclips.h jank_lpprefs.c jank_lpprefs.h; do
    [ -f "$src/core/$b" ] || { echo "sync_jank: $b is not in $src/core (an older jank?)"; exit 1; }
    cp "$src/core/$b" "$dst/$b"
    n=$((n + 1))
done
for b in jank_lpsess.c jank_lpsess.h jank_lpsessapp.c jank_lpout.c jank_lpout.h; do   # (an older copy's: not used any more)
    rm -f "$dst/$b"
done
rev=$(git -C "$src" rev-parse --short HEAD 2>/dev/null || echo unknown)
dirty=$(git -C "$src" status --porcelain -- core 2>/dev/null | head -1)
printf 'jank %s%s (github.com/w0ts/jank core/), copied by tools/sync_jank.sh; GPL-3.0-only\n' "$rev" "${dirty:+ with uncommitted changes}" > "$dst/SOURCE"
echo "sync_jank: $n files from jank $rev${dirty:+ (uncommitted changes in core/)}"

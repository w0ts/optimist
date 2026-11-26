#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copy jank's Launchpad app (github.com/w0ts/jank core/: jank_lp*, the types of jank_dn2live.h) into third_party/jank. jank is its
# source: change it there, then run this (the owner, 2026-11-26: "jank is the source, copied"). Optimist's own files in
# third_party/jank (jank_lpscales.[ch]: the FM-1's scales; jank_fm1.h: the build's switches; SOURCE) are left as they are.
#   tools/sync_jank.sh [JANK_DIR]      JANK_DIR: a jank checkout (default ../jank beside this repository), at the commit wanted
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
src=${1:-$here/../jank}
dst=$here/third_party/jank
[ -d "$src/core" ] || { echo "sync_jank: no jank checkout at $src"; exit 1; }
mkdir -p "$dst"
n=0
for f in "$src"/core/jank_lp*.c "$src"/core/jank_lp*.h "$src"/core/jank_dn2live.h; do
    b=$(basename "$f")
    case $b in jank_lpscales.c|jank_lpscales.h) continue ;; esac   # (Optimist's own: the FM-1's scales)
    # STOPGAP until jank's core needs no libc header (asked 2026-11-26): the FM-1 has no <string.h> or <stdio.h>; launchpad.c
    # gives the few functions (lp_shim). Every other line is jank's as it is.
    grep -v -E '^#include <(string|stdio)\.h>' "$f" > "$dst/$b"
    n=$((n + 1))
done
rev=$(git -C "$src" rev-parse --short HEAD 2>/dev/null || echo unknown)
dirty=$(git -C "$src" status --porcelain -- core 2>/dev/null | head -1)
printf 'jank %s%s (github.com/w0ts/jank core/), copied by tools/sync_jank.sh; GPL-3.0-only\n' "$rev" "${dirty:+ with uncommitted changes}" > "$dst/SOURCE"
echo "sync_jank: $n files from jank $rev${dirty:+ (uncommitted changes in core/)}"

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Our Link peer (link_host) beside Ableton's library (ref_peer) on this computer, both on 127.0.0.1; compares the
# tempo, the phase (beats modulo 4) and start / stop each sees, at the same host times. docs/LINK-DESIGN.md §4.
#   tools/link/interop.sh <ableton-link checkout, with modules/asio-standalone> [out dir]
# The script: ref_peer starts at 120 BPM, link_host joins 1.5 s later at 90 (it must take 120); ref goes to 133 at
# 4 s, ours to 100 at 7 s (ref must follow), ref starts playing at 10 s. Exit status 1 when a check fails.
# Quit Live and every other Link app first: they join the session, take these tempo changes and start playing
# (2026-10-26: Live, open with Link on, went to 133, 100 and started), and their own timeline moves the phase.
set -e
LINK=${1:?ableton link checkout}
OUT=${2:-worktrees/link-out}
mkdir -p "$OUT"
PLAT=-DLINK_PLATFORM_LINUX=1; [ "$(uname)" = Darwin ] && PLAT=-DLINK_PLATFORM_MACOSX=1
c++ -std=c++17 -O2 -w $PLAT -I"$LINK/include" -I"$LINK/modules/asio-standalone/asio/include" -o "$OUT/ref_peer" tools/link/ref_peer.cpp
cc -std=c11 -O2 -Wall -o "$OUT/link_host" tools/link/link_host.c firmware/src/link/link.c -lm
"$OUT/ref_peer" -bpm 120 -for 14 -at 4:tempo=133 -at 10:play=1 > "$OUT/ref.log" &
REF=$!
sleep 1.5
"$OUT/link_host" -bpm 90 -for 11 -at 7:tempo=100 > "$OUT/ours.log"
wait $REF
# each of our lines against the reference line nearest in time: phase difference in ms of beat time; "settled"
# leaves out 0.1-1 s after each change (el: s since ref started; ours started 1.5 s later, so its 7 s is 8.5)
awk '
FNR == NR { rt[++rn] = $2; rp[rn] = $7; rb[rn] = $4; rpl[rn] = $5; rpe[rn] = $6; next }
{
    while (j < rn && rt[j + 1] <= $2) j++
    if (j < 1 || j >= rn) next
    t = $2; el = (t - rt[1]) / 1e6
    f = (t - rt[j]) / (rt[j + 1] - rt[j]); p2 = rp[j + 1]; if (p2 < rp[j]) p2 += 4   # (across the bar line)
    ph = rp[j] + f * (p2 - rp[j]); if (ph >= 4) ph -= 4
    d = $7 - ph; if (d > 2) d -= 4; if (d < -2) d += 4
    ms = d * 60000 / $4
    n++
    if ($6 + 0 >= 1 && ((el > 2.5 && el < 3.8) || (el > 5 && el < 8.4) || (el > 9.5 && el < 9.9) || el > 10.5)) {
        s++; a = ms < 0 ? -ms : ms; sum += a; if (a > mx) mx = a
        if ((rb[j] - $4) > 0.01 || ($4 - rb[j]) > 0.01) tb++
    }
    if (el > 11.5 && rpl[j] == 1 && $5 == 1) play = 1
    last_peers = $6; last_rpeers = rpe[j]
}
END {
    printf "compared %d lines (%d settled): |phase| mean %.3f ms, max %.3f ms; tempo mismatches %d; playing seen %d; peers ours %d ref %d\n", n, s, s ? sum / s : 0, mx, tb, play, last_peers, last_rpeers
    bad = !s || mx > 1.0 || tb > 0 || !play || last_peers != 1 || last_rpeers != 1
    exit bad
}' "$OUT/ref.log" "$OUT/ours.log"

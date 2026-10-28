#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The section log across firmware versions (MIDI tracks' phase 0; tests/sec_log_compat_test.c): this build's loader
# (NEW) and the previous firmware's (PREV, tests/compat/sec_log_142.c) on each other's logs.
#   tests/sec_log_compat.sh DIR NEW PREV FORMAT   DIR scratch for the images (deleted after), FORMAT 1 or 2: the golden
#                                                 cksum of PREV's generated dump, tests/sec_log_compat_golden.txt's line
set -u
D=$1 NEW=$2 PREV=$3
GOLDEN=$(awk -v f="format$4" '$1 == f {print $2 " " $3}' "$(dirname "$0")/sec_log_compat_golden.txt")
mkdir -p "$D"
bad=0
ok() { printf '%-100s %s\n' "$1" "$2"; [ "$2" = ok ] || bad=1; }
same() { cmp -s "$1" "$2" && echo ok || echo FAIL; }
q() { "$@" >/dev/null || { echo "!! $*"; bad=1; }; }
# an old log (the previous firmware's) loads here identically
q "$PREV" gen "$D/a.img" "$D/a.dump"
got=$(cksum < "$D/a.dump" | awk '{print $1 " " $2}')
[ "$got" = "$GOLDEN" ] && r=ok || r=FAIL
ok "the previous firmware's generated log: its records as the golden ($GOLDEN; got $got)" $r
q "$NEW" load "$D/a.img" "$D/b.dump"
ok "an old log (the previous firmware's: random stores, clears, compactions, restarts) loads here identically" "$(same "$D/a.dump" "$D/b.dump")"
q "$NEW" mdump "$D/a.img" "$D/b.mdump"
q "$NEW" churn "$D/a.img" "$D/c.img" "$D/c.dump"
ok "... compacted here (every record stored again three times): the same records" "$(same "$D/a.dump" "$D/c.dump")"
q "$PREV" load "$D/c.img" "$D/d.dump"
ok "... and the previous firmware reads the log this build compacted identically" "$(same "$D/a.dump" "$D/d.dump")"
# this firmware's log (with the MIDI tracks' ids) in the previous firmware
q "$NEW" gen "$D/e.img" "$D/e.dump"
q "$NEW" mdump "$D/e.img" "$D/e.mdump"
q "$PREV" load "$D/e.img" "$D/f.dump"
ok "this build's log (MIDI ids 142..225 between the others) read by the previous firmware: every record it knows" "$(same "$D/e.dump" "$D/f.dump")"
q "$PREV" churn "$D/e.img" "$D/g.img" "$D/g.dump"
ok "... compacted by the previous firmware: every record it knows kept" "$(same "$D/e.dump" "$D/g.dump")"
q "$NEW" load "$D/g.img" "$D/h.dump"
ok "... and back here: those records the same" "$(same "$D/e.dump" "$D/h.dump")"
q "$NEW" churn "$D/e.img" "$D/i.img" "$D/i.dump"
q "$NEW" mdump "$D/i.img" "$D/i.mdump"
ok "this build's compaction of its own log: the records of 0..141 and of the MIDI ids the same" "$( [ "$(same "$D/e.dump" "$D/i.dump")" = ok ] && same "$D/e.mdump" "$D/i.mdump" || echo FAIL)"
n=$("$NEW" mdump "$D/e.img" "$D/e2.mdump" | awk '{print $1}')
k=$("$NEW" load "$D/e.img" "$D/e2.dump" | awk '{print $1}')
[ "${n:-0}" -gt 10 ] && [ "${k:-0}" -gt 40 ] && r=ok || r=FAIL
ok "... (this build's log holds $k records of 0..141 and $n of the MIDI ids)" $r
rm -rf "$D"
[ $bad = 0 ] && echo "sec log compat test passed" || echo "sec log compat test FAILED"
exit $bad

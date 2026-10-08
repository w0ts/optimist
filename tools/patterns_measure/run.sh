#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The measurements behind docs/PATTERNS-DESIGN.md (per-track patterns and scenes): struct sizes, section records as
# codec A wrote them and as the firmware writes them now (codec B, phase 0b), prototype pattern and scene records,
# and how many fit the section log (sec_log.c, the real code on a simulated NOR) at 8, 12 and 16 sectors, with 16 or
# 32 patterns a track. Read-only on the firmware: the log is compiled from copies whose SLG_IDS (88 since phase 0)
# is set to 24 (before phase 0), 88 (16 patterns a track) or 152 (32 a track).
#   sh tools/patterns_measure/run.sh            (from anywhere; generates build/gen-host when missing)
set -e
cd "$(dirname "$0")/../.."
OUT=build/patterns_measure
mkdir -p "$OUT"
[ -f build/gen-host/felucca_tables.h ] || python3 tools/build.py --host-headers
CC="${CC:-cc}"
grep -q '^#define SLG_IDS 88u' firmware/src/storage/sections/sec_log.c || { echo "sec_log.c: SLG_IDS is not 88u: update run.sh"; exit 1; }
for ids in 24 88 152; do
    sed "s/^#define SLG_IDS 88u/#define SLG_IDS ${ids}u/" firmware/src/storage/sections/sec_log.c > "$OUT/sec_log_ids$ids.c"
done
build() {   # sectors, ids, name
    $CC -O2 -w -Ibuild/gen-host -Ifirmware/src -Itests -I"$OUT" -DSEC_LOG_SECTORS=${1}u -DPM_IDS=$2 -DFELUCCA_MOTION=1 \
        -o "$OUT/$3" tools/patterns_measure/patterns_measure.c -lm
}
build 8 88 pm8_16
build 12 88 pm12_16
build 16 88 pm16_16
build 8 152 pm8_32
build 12 152 pm12_32
build 8 24 pm8_ids24
"$OUT/pm8_16" full
"$OUT/pm12_16" log
"$OUT/pm16_16" log
"$OUT/pm8_32" log
"$OUT/pm12_32" log
"$OUT/pm8_ids24" ids
"$OUT/pm8_16" ids
"$OUT/pm8_32" ids

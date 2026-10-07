#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The measurements behind docs/CLIPS-DESIGN.md (per-track clips and scenes): struct sizes, the sizes of clip and
# scene records made with a prototype codec next to today's section codec (sec_codec.c), and how many fit the
# section log (sec_log.c, the real code on a simulated NOR) at 8, 12 and 16 sectors, with 16 or 32 clips a track.
# Read-only on the firmware: the log is compiled from a copy whose SLG_IDS (24 today, a fixed #define) is raised
# for the clip ids (24 + 4 x 16 = 88, 24 + 4 x 32 = 152).
#   sh tools/clips_measure/run.sh            (from the repo root; generates build/gen-host when missing)
set -e
cd "$(dirname "$0")/../.."
OUT=build/clips
mkdir -p "$OUT"
[ -f build/gen-host/felucca_tables.h ] || python3 tools/build.py --host-headers
CC="${CC:-cc}"
for ids in 24 88 152; do
    sed "s/^#define SLG_IDS 24u.*/#define SLG_IDS ${ids}u/" firmware/src/sec_log.c > "$OUT/sec_log_ids$ids.c"
done
build() {   # sectors, ids, name
    $CC -O2 -w -Ibuild/gen-host -Ifirmware/src -Itests -I"$OUT" -DSEC_LOG_SECTORS=${1}u -DCM_IDS=$2 -DFELUCCA_MOTION=1 \
        -o "$OUT/$3" tools/clips_measure/clips_measure.c -lm
}
build 8 88 cm8_16
build 12 88 cm12_16
build 16 88 cm16_16
build 8 152 cm8_32
build 12 152 cm12_32
build 8 24 cm8_ids24
"$OUT/cm8_16" full
"$OUT/cm12_16" log
"$OUT/cm16_16" log
"$OUT/cm8_32" log
"$OUT/cm12_32" log
"$OUT/cm8_ids24" ids
"$OUT/cm8_32" ids

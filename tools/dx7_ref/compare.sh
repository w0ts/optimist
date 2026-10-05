#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The DX7 engine (firmware/src/dx7_core.c) against msfa as Dexed has it (Apache-2.0), rendered on the
# host: the factory voices (4 notes each) and random voices, sample by sample. Fetches Dexed's msfa at a
# fixed commit into build/dx7_ref (network once), builds it with 32-sample blocks (LG_N 5, as SLOOP).
#   tools/dx7_ref/compare.sh [random voices, default 200]
set -e
cd "$(dirname "$0")/../.."
DEXED=2e182b3db85c09083ab13c8b9b00565ce7d9ff85
OUT=build/dx7_ref
M=$OUT/msfa
mkdir -p "$M" build/gen
python3 tools/gen_dx7_tables.py build/gen/dx7_tables.h
for f in dx7note.cc dx7note.h env.cc env.h lfo.cc lfo.h pitchenv.cc pitchenv.h fm_core.cc fm_core.h fm_op_kernel.cc fm_op_kernel.h exp2.cc exp2.h sin.cc sin.h freqlut.cc freqlut.h synth.h controllers.h porta.cpp porta.h tuning.h aligned_buf.h; do
    [ -f "$M/$f" ] || curl -sfL -o "$M/$f" "https://raw.githubusercontent.com/asb2m10/dexed/$DEXED/Source/msfa/$f"
done
MSFA=f67d41d313b7dc85f6fb99e79e515cc9d208cfff                 # Google's msfa: its bank voice unpacker
for f in patch.cc patch.h; do
    [ -f "$M/$f" ] || curl -sfL -o "$M/$f" "https://raw.githubusercontent.com/google/music-synthesizer-for-android/$MSFA/app/src/main/jni/$f"
done
sed -i.bak 's/const static int LG_N = 6;/const static int LG_N = 5;/' "$M/synth.h"
: > "$OUT/Dexed.h"
printf '#pragma once\nnamespace Tunings { struct Tuning {}; }\n' > "$M/Tunings.h"
printf '#pragma once\nstruct MTSClient;\nstatic inline bool MTS_HasMaster(MTSClient *) { return false; }\nstatic inline double MTS_NoteToFrequency(MTSClient *, int, int) { return 0; }\n' > "$M/libMTSClient.h"
c++ -std=c++17 -O2 -w -I"$OUT" -I"$M" -o "$OUT/ref_render" tools/dx7_ref/ref_render.cc "$M"/dx7note.cc "$M"/env.cc \
    "$M"/exp2.cc "$M"/fm_core.cc "$M"/fm_op_kernel.cc "$M"/freqlut.cc "$M"/lfo.cc "$M"/pitchenv.cc "$M"/sin.cc "$M"/porta.cpp "$M"/patch.cc
cc -O2 -Ibuild/gen -o build/dx7_render tests/dx7_render.c
build/dx7_render dump "$OUT"
NV=$(ls "$OUT"/v*.bin | wc -l)
echo "== factory voices (sample-exact = maxdiff 0)"
python3 tools/dx7_ref/cmp.py "$NV"
echo "== the ROM bank (assets/dx7/rom1a.syx): msfa's UnpackPatch vs dx7_unpack, then the render"
python3 - "$OUT" <<'PY'
import sys
b = open("assets/dx7/rom1a.syx", "rb").read()[6:6 + 4096]
for i in range(32):
    open(f"{sys.argv[1]}/r{i:02d}.bin", "wb").write(b[i * 128:(i + 1) * 128])
PY
DX7_PREFIX=r python3 tools/dx7_ref/cmp.py 32
echo "== random voices, no AMS"
python3 tools/dx7_ref/fuzz.py "${1:-200}" 1 noams
echo "== random voices with AMS (Dexed's exp() as an exp2 table: small differences expected)"
python3 tools/dx7_ref/fuzz.py "${1:-200}" 2 ams

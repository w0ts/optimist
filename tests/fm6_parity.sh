#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# FM6 engine and this test: Kerem Kilic (Melodee, github.com/keremimo/melodee), GPL-3.0-only; ported to SLOOP
# FM6 against Dexed, sample by sample (tests/fm6_parity.py). Needs a Dexed checkout (its Source/
# directory: msfa/, EngineMkI.cpp, EngineOpl.cpp; https://github.com/asb2m10/dexed), and the host
# tables (./build.sh, or python3 tools/gen_tables.py build/gen/felucca_tables.h).
#   DEXED_SRC=~/src/dexed/Source tests/fm6_parity.sh [--quick] [--seed=N]
set -e
cd "$(dirname "$0")/.."
D="${DEXED_SRC:?set DEXED_SRC to the Source directory of a Dexed checkout}"
OUT=build/host
mkdir -p "$OUT"
for NV in 8 16; do   # SLOOP's 8 voices a part (poly), Dexed's 16 (mono: FM6 keeps them)
${CXX:-c++} -std=c++17 -O2 -w -DDEXED_VOICES=$NV -Itests -Itests/dexed_stub -I"$D" -I"$D/msfa" -o "$OUT/dexed_ref$NV" \
    tests/dexed_ref.cc "$D"/msfa/dx7note.cc "$D"/msfa/env.cc "$D"/msfa/pitchenv.cc "$D"/msfa/lfo.cc "$D"/msfa/exp2.cc \
    "$D"/msfa/sin.cc "$D"/msfa/freqlut.cc "$D"/msfa/fm_core.cc "$D"/msfa/fm_op_kernel.cc "$D"/msfa/porta.cpp \
    "$D"/EngineMkI.cpp "$D"/EngineOpl.cpp
done
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fm6_parity" tests/fm6_parity.c -lm
python3 tests/fm6_parity.py "$OUT/dexed_ref8" "$OUT/dexed_ref16" "$OUT/fm6_parity" "$OUT/fm6_parity_runs" "$@"

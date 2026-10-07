#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build A vs build B for a refactor that must not change a sample (docs/DSP-SHARED.md: the shared DSP blocks).
#   sh tests/dsp_ab.sh [BASE_REV]        (default BASE_REV: optimist)
# Exports BASE_REV into build/ab/base, then builds the same host renders from it (A) and from this tree (B) and
# compares what they print:
#   - tests/regress.c in several configurations (the default, the X0X kits, every backported engine and effect,
#     the SLOOP 2.3 switches, SLICE, REV_HALF, the original ANALOG/SUPER of ANALOG2=0): every golden render's
#     hash (GOLDEN_UPDATE into a scratch file) must be the same;
#   - tests/skip_test.c (the FX buses, gain ramps, flips mid-sound) in the same configurations: the same hashes;
#   - tests/x0x_drums_test.c: every X0X 909 / 808 note's WAV, byte for byte.
# Also prints the host CPU of every render, A and B (instructions per sample, regress BUDGET_UPDATE), with the
# renders that moved by more than 2 % (the count is ~1 % run to run): a merge must not make a render slower.
set -e  # (a render that fails its own checks still prints: compared, not judged here)
cd "$(dirname "$0")/.."
BASE=${1:-optimist}
AB=build/ab
rm -rf "$AB"
mkdir -p "$AB/base" "$AB/out"
git archive "$BASE" | tar -x -C "$AB/base"
CC="${CC:-cc} -O2 -w"
X0X_ON="-DFELUCCA_DRUM_X909=1 -DFELUCCA_DRUM_X808=1"
BP_ON="-DFELUCCA_CHANCE=1 -DFELUCCA_KEYLIT=1 -DFELUCCA_QNT_SEQ=1 -DFELUCCA_SPRING=1 -DFELUCCA_BASSPLUS=1 -DFELUCCA_BRIGHT=1 -DFELUCCA_DLY_HALVE=1 -DFELUCCA_MOTION=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_SECTIONS=4"
BP23_ON="-DFELUCCA_MONO_RELEASE=1 -DFELUCCA_SHED_FADE=1 -DFELUCCA_GLIDE=1"
fail=0
for side in base this; do
    root=$AB/base; [ $side = this ] && root=.
    (cd "$root" && python3 tools/build.py --host-headers > /dev/null) || { echo "dsp_ab: host headers ($side) failed"; exit 1; }
done
n=0
for cfg in "" "$X0X_ON" "$BP_ON" "$X0X_ON -DFELUCCA_ENG_ACID=1 -DFELUCCA_GLIDE=1" "$BP23_ON" "-DFELUCCA_ENG_SLICE=1" \
           "-DFELUCCA_REV_HALF=1 -DFELUCCA_SPRING=1" "-DFELUCCA_ANALOG2=0" "-DFELUCCA_USB_AUDIO=1"; do
    n=$((n + 1))
    for side in base this; do
        root=$AB/base; [ $side = this ] && root=.
        o=$AB/out/$side-$n
        if ! $CC -I"$root/build/gen-host" -I"$root/firmware/src" $cfg -o "$o-regress" "$root/tests/regress.c" -lm 2> "$o.cc.log"; then
            echo "dsp_ab: [$n] $side: regress does not build with '$cfg'"; tail -5 "$o.cc.log"; fail=1; continue
        fi
        : > "$o.golden"; : > "$o.cpu"
        GOLDEN_UPDATE=1 BUDGET_UPDATE=1 "$o-regress" "$o.golden" "$o.cpu" > "$o.regress.log" 2>&1 || true
        if $CC -I"$root/build/gen-host" -I"$root/firmware/src" $cfg -o "$o-skip" "$root/tests/skip_test.c" -lm 2>> "$o.cc.log"; then
            "$o-skip" > "$o.skip" 2>&1 || true
        else
            echo "skip_test does not build" > "$o.skip"
        fi
    done
    a=$AB/out/base-$n; b=$AB/out/this-$n
    g=$(grep -cv "^#" "$b.golden" || true)
    if cmp -s "$a.golden" "$b.golden" && cmp -s "$a.skip" "$b.skip"; then
        echo "dsp_ab: [$n] same: $g golden renders, $(wc -l < "$b.skip" | tr -d ' ') skip scenarios ('$cfg')"
    else
        echo "dsp_ab: [$n] DIFFERENT ('$cfg'):"; diff "$a.golden" "$b.golden" | head -20; diff "$a.skip" "$b.skip" | head -10; fail=1
    fi
    # CPU: the renders that moved by more than 2 %
    awk -v tag="[$n]" 'NR == FNR { if ($1 ~ /^cpu\//) a[$1] = $2; next }
        $1 ~ /^cpu\// && ($1 in a) && a[$1] > 0 { r = ($2 - a[$1]) / a[$1]; ta += a[$1]; tb += $2;
            if (r > 0.02 || r < -0.02) printf "dsp_ab: %s cpu %s %d -> %d (%+.1f %%)\n", tag, $1, a[$1], $2, 100 * r }
        END { if (ta) printf "dsp_ab: %s cpu sum of renders %d -> %d (%+.2f %%)\n", tag, ta, tb, 100 * (tb - ta) / ta }' \
        "$a.cpu" "$b.cpu"
done
for side in base this; do
    root=$AB/base; [ $side = this ] && root=.
    mkdir -p "$AB/out/$side-x0x"
    $CC -I"$root/build/gen-host" -I"$root/firmware/src" $X0X_ON -o "$AB/out/$side-x0x_test" "$root/tests/x0x_drums_test.c" -lm
    "$AB/out/$side-x0x_test" "$AB/out/$side-x0x" > "$AB/out/$side-x0x.log" 2>&1 || true
done
if diff -r "$AB/out/base-x0x" "$AB/out/this-x0x" > /dev/null; then
    echo "dsp_ab: X0X kits: $(ls "$AB/out/this-x0x" | wc -l | tr -d ' ') WAVs the same"
else
    echo "dsp_ab: X0X kits: WAVs DIFFERENT"; diff -rq "$AB/out/base-x0x" "$AB/out/this-x0x" | head; fail=1
fi
[ $fail = 0 ] && echo "dsp_ab: A ($BASE) = B (this tree)" || echo "dsp_ab: FAILED"
exit $fail

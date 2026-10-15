#!/bin/bash
# the long mode of tests/audio_hang_stress.c: seeds A..B of each build (plain, two-thread, ASan+UBSan, TSan two-thread)
# and signal-mode runs; logs and a summary in build/hang. usage: tests/audio_hang_batch.sh [A] [B] [FRAMES] [SIGSECS]
A=${1:-1}; B=${2:-50}; F=${3:-20000}; S=${4:-60}
H=build/host; L=build/hang; mkdir -p $H $L
export HGEN=${HGEN:-build/gen-host}
bash tests/audio_hang_build.sh $H/audio_hang_stress || exit 1
bash tests/audio_hang_build.sh $H/audio_hang_dual -DHOST_DUAL=1 || exit 1
bash tests/audio_hang_build.sh $H/audio_hang_asan -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-omit-frame-pointer || exit 1
bash tests/audio_hang_build.sh $H/audio_hang_tsan -DHOST_DUAL=1 -fsanitize=thread || exit 1
: > $L/summary.txt
for s in $(seq $A $B); do
    for b in stress dual asan tsan; do
        f=$F; [ $b = asan ] || [ $b = tsan ] && f=$((F / 4))
        HANG_SEED=$s HANG_FRAMES=$f HANG_MS=60000 TSAN_OPTIONS="halt_on_error=0 exitcode=0" $H/audio_hang_$b > $L/$b-$s.txt 2>&1
        rc=$?
        bad=$(grep -c "runtime error\|ERROR: AddressSanitizer\|WARNING: ThreadSanitizer\|HANG\|STATE:\|STI INSIDE" $L/$b-$s.txt)
        [ $rc != 0 ] || [ "$bad" != 0 ] && echo "$b seed $s rc=$rc findings=$bad" >> $L/summary.txt
    done
done
for k in 1 2 3; do
    HANG_MODE=sig HANG_SECS=$S HANG_SEED=$k $H/audio_hang_stress > $L/sig-$k.txt 2>&1 || echo "sig $k rc=$?" >> $L/summary.txt
    HANG_MODE=sig HANG_SECS=$S HANG_SEED=$k $H/audio_hang_dual > $L/sigdual-$k.txt 2>&1 || echo "sigdual $k rc=$?" >> $L/summary.txt
    HANG_MODE=sig HANG_NEST=1 HANG_SECS=$S HANG_SEED=$k $H/audio_hang_stress > $L/signest-$k.txt 2>&1 || echo "signest $k rc=$?" >> $L/summary.txt
    HANG_MODE=sig HANG_NEST=1 HANG_SECS=$S HANG_SEED=$k $H/audio_hang_dual > $L/signestdual-$k.txt 2>&1 || echo "signestdual $k rc=$?" >> $L/summary.txt
done
echo "batch $A..$B done" >> $L/summary.txt

#!/bin/sh
# Every emu-pass sweep at once (docs/EMU-PASS.md): the six images from tests/emu/farm_build.sh, one sweep each, logs
# under build/emu-sweep/runs/. Run from the repository root with EMU_CHECK_DRIVER set to the driver built against the
# patched emulator (tests/emu/emulator-wide-ops.patch).
cd "$(dirname "$0")/../.." || exit 2
[ -n "$EMU_CHECK_DRIVER" ] || { echo "EMU_CHECK_DRIVER not set (docs/EMU-PASS.md)" >&2; exit 2; }
mkdir -p build/emu-sweep/runs
for b in ref a b c d e; do
    x=""
    [ $b = ref ] && x=--a2
    python3 tests/emu/sweep.py build/emu-sweep/images/$b/build build/emu-sweep/runs/sweep-$b $x > build/emu-sweep/runs/sweep-$b.log 2>&1 &
done
wait

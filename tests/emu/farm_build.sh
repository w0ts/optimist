#!/bin/sh
# Build the emu-pass configurations on the gpu farm, each in its own copy (tools/optimist.py build writes
# build/builder.config, so one tree builds one configuration at a time), and copy each package + ELF back.
#   tests/emu/farm_build.sh [NAME ...]      (default: every one below)
# Run from the repository root; commits first: it ships HEAD. Images land in build/emu-sweep/images/<NAME>/build/.
set -e
W=emu-sweep
SSH="ssh -o ServerAliveInterval=30 -o ServerAliveCountMax=20 gpufarm"
OUT=build/emu-sweep/images
mkdir -p $OUT

# NAME: the --set items on top of the synth profile (DUAL builds parked: OPTIMIST_DUAL_PARK=1, as the emu gate)
sets() {
    case $1 in
    synth) echo "" ;;
    a) echo "DUAL=0 DS_PLAITS=0 MASTER_COMP=0 ENG_PHASE=1 ENG_LOFI=1 ENG_FORMANT=1 ENG_TRIO=1 ENG_CZ=1 ENG_NOISE=1 ENG_DRONE=1 GEN_GRIDS=1" ;;
    b) echo "DUAL=0 DS_PLAITS=0 MASTER_COMP=0 ENG_DRAWBAR=1 ENG_FM6=0 FM6_PK_KEYS=0 FM6_PK_BASS=0 FM6_PK_BELLS=0 FM6_PK_PADS=0 FM6_PK_80S=0 UP_FM6=0 ENG_PLAITS=1 ENG_PROPHET=1 ENG_STRINGS=1 ENG_VPS=1 ENG_WSEQ=1 ENG_SAMPLE=1 ENG_GRAIN=1 SET_STRGS=1" ;;
    c) echo "ENG_FM6=0 FM6_PK_KEYS=0 FM6_PK_BASS=0 FM6_PK_BELLS=0 FM6_PK_PADS=0 FM6_PK_80S=0 UP_FM6=0 ENG_PHYS=0 ENG_ACID=0 ENG_JUNO=0 ENG_TERRAIN=1 ENG_VECTOR=1 ENG_SID=1" ;;
    d) echo "ENG_FM6=0 FM6_PK_KEYS=0 FM6_PK_BASS=0 FM6_PK_BELLS=0 FM6_PK_PADS=0 FM6_PK_80S=0 UP_FM6=0 ENG_PHYS=0 ENG_ACID=0 ENG_JUNO=0 ENG_WAVE=1 DLY_LEN=16384 DLY_ARENA=32" ;;
    e) echo "DUAL=0 MASTER_COMP=0 ENG_FM6=0 FM6_PK_KEYS=0 FM6_PK_BASS=0 FM6_PK_BELLS=0 FM6_PK_PADS=0 FM6_PK_80S=0 UP_FM6=0 ENG_SLICE=1" ;;
    *) echo "unknown $1" >&2; exit 2 ;;
    esac
}

NAMES=${*:-"synth a b c d"}
$SSH "rm -rf ~/optimist/worktrees/$W && mkdir -p ~/optimist/worktrees/$W"
for n in $NAMES; do
    $SSH "mkdir -p ~/optimist/worktrees/$W/$n"
    git archive HEAD | $SSH "tar -x -C ~/optimist/worktrees/$W/$n"
    $SSH "ln -sfn ~/optimist/sdk ~/optimist/worktrees/$W/$n/sdk"
done
for n in $NAMES; do
    args=""
    for kv in $(sets $n); do args="$args --set $kv"; done
    $SSH "cd ~/optimist/worktrees/$W/$n && OPTIMIST_DUAL_PARK=1 python3 tools/optimist.py build --profile synth --name ep-$n $args > ../$n.log 2>&1; echo $n rc=\$?; grep -E 'exact:|rror|OVER|over the' ../$n.log | head -8" &
done
wait
for n in $NAMES; do
    mkdir -p $OUT/$n/build/gen
    scp -q gpufarm:optimist/worktrees/$W/$n.log $OUT/$n/build.log || true
    scp -q gpufarm:optimist/worktrees/$W/$n/build/felucca.fwsc gpufarm:optimist/worktrees/$W/$n/build/felucca.elf $OUT/$n/build/ || echo "$n: no package"
    scp -q gpufarm:optimist/worktrees/$W/$n/build/gen/felucca_config.h $OUT/$n/build/gen/ || true
done

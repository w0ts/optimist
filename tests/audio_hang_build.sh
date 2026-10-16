#!/bin/bash
# build tests/audio_hang_stress.c with the user's sequencer switches (config mots.config's); $1 output, rest extra flags
# (run_tests.sh calls it; by hand: HGEN=build/gen-host bash tests/audio_hang_build.sh build/host/audio_hang_stress)
OUT=$1; shift
MOTS_SEQ="-DFELUCCA_CHORDPLUS=1 -DFELUCCA_PATTERNS=1 -DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -DFELUCCA_CHANCE=1 -DFELUCCA_QNT_SEQ=1 -DFELUCCA_MOTION=1 -DFELUCCA_REC_MODES=1 -DFELUCCA_MOTION_MARK=1 -DFELUCCA_SL24_XSTEP=1 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_TRK_FILT=1 -DFELUCCA_GLIDE=1 -DFELUCCA_INS_FOLD=1 -DFELUCCA_INS_FUZZ=1 -DFELUCCA_INS_WIDTH=1 -DFELUCCA_INS_GATER=1 -DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=1 -DFELUCCA_INS_JCHORUS=1"
${CC:-cc} -O1 -g -w -I"${HGEN:-build/gen-host}" -Ifirmware/src -Ifirmware/hal -DFELUCCA_SECTIONS=8 $MOTS_SEQ "$@" -o "$OUT" tests/audio_hang_stress.c -lm -lpthread

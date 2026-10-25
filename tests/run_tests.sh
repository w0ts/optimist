#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the drum kit, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in build/felucca.dis
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# After an intended change of the sound: GOLDEN_UPDATE=1 sh tests/run_tests.sh, review the diff
# of tests/golden.txt (and of golden_rev_half.txt, the REV_HALF renders), commit it with the change. After an
# intended change of the cost (or a new compiler): BUDGET_UPDATE=1 (rewrites cpu_baseline.txt and
# target_budget.txt). VERBOSE=1: every render.
#
# Parallel: each test with its compile is a unit (job; ( jobin ... ) >"$U.out" 2>&1 & jobbg) run in the background,
# JOBS at once (default: three quarters of the CPUs; JOBS=1: one at a time, as before). A unit's output is kept in
# build/host/jobs/N.out and printed whole, in this file's order, so the log reads as a serial run's. A unit that
# needs another's files waits for it (after N); the slowest start first (early, below). GOLDEN_UPDATE / BUDGET_UPDATE run one at a time (they rewrite the
# shared golden and baseline files). Nothing here is timed by the wall clock: the CPU budgets count instructions
# (regress.c, proc_pid_rusage), the ns lines are for information. JOBS also reaches regress.c (its children at once,
# default 8) when set. A compile that fails stops the run, as before (the units already running finish first).
set -e
cd "$(dirname "$0")/.."
export AC79_SDK="${AC79_SDK:-$(python3 -c 'import sys; sys.path.insert(0, "tools"); import toolchain; print(toolchain.sdk_dir())')}"
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
[ "$(uname)" = Darwin ] || CC="$CC -Itests/linux"   # (<libproc.h>, macOS only: the instruction counts; none elsewhere)
SEC4="-DFELUCCA_SECTIONS=4"   # (the tests of the four project slots in RAM; the log: sec_*_test, sections_test)
fail=0
# the backported features' test (tests/backports_test.c) builds with every switch on (firmware/src/core/backports.h)
BACKPORTS_ON="-DFELUCCA_CHANCE=1 -DFELUCCA_KEYLIT=1 -DFELUCCA_QNT_SEQ=1 -DFELUCCA_SPRING=1 -DFELUCCA_BASSPLUS=1 -DFELUCCA_BRIGHT=1 -DFELUCCA_MOTION=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_ENG_JUNO=1"
# the further insert types (fx/insfx/insfx.c), every one of them (GATER: gater.c); the limited ones CHRS ECHO FLNG in an
# effects memory of 8 KB beside the delay send's 128 KB (fxi_mem.h: 4 CHRS, or 1 ECHO, or 8 FLNG at once)
INSFX_ON="-DFELUCCA_INS_FOLD=1 -DFELUCCA_INS_FUZZ=1 -DFELUCCA_INS_PHASER=1 -DFELUCCA_INS_FLANGER=1 -DFELUCCA_INS_FORMANT=1 -DFELUCCA_INS_WIDTH=1 -DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=1 -DFELUCCA_DLY_ARENA=136 -DFELUCCA_INS_GATER=1 -DFELUCCA_INS_SFOLD=1 -DFELUCCA_INS_RECT=1 -DFELUCCA_INS_HARM=1 -DFELUCCA_INS_RING=1 -DFELUCCA_INS_SUBOCT=1 -DFELUCCA_INS_SLEW=1 -DFELUCCA_INS_TAPE=1 -DFELUCCA_INS_SPIT=1"
# NOISE (FELUCCA_ENG_NOISE) joins the backports test; its own line, not BACKPORTS_ON (engine number 15, beside JUNO's 14)
BACKPORTS_ON="$BACKPORTS_ON -DFELUCCA_ENG_NOISE=1"
# the scene's engines, each a builder item: Clipper's six (engines/clipper/), Mini-Tone's SID (engines/sid/), PROPHET (engines/prophet/); tests/scene_engines_test.c
SCENE_ON="-DFELUCCA_ENG_STRINGS=1 -DFELUCCA_ENG_VPS=1 -DFELUCCA_ENG_TERRAIN=1 -DFELUCCA_ENG_WSEQ=1 -DFELUCCA_ENG_VECTOR=1 -DFELUCCA_ENG_SID=1 -DFELUCCA_ENG_PROPHET=1 -DFELUCCA_ENG_WAVE=1"
# ENV 1 MOD's test (tests/env1_test.c): every engine built, so every engine's cards are rendered
ENV1_ENG="-DFELUCCA_ENG_SLICE=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_ENG_JUNO=1 -DFELUCCA_ENG_NOISE=1 -DFELUCCA_ENG_DRONE=1 -DFELUCCA_ENG_PLAITS=1 $SCENE_ON"
# FM6's voice packs, every one (tools/fm6_packs, firmware/src/engines/fm6/eng_fm6_pk_*.h)
FM6_PACKS_ON="-DFELUCCA_FM6_PK_KEYS=1 -DFELUCCA_FM6_PK_BASS=1 -DFELUCCA_FM6_PK_BELLS=1 -DFELUCCA_FM6_PK_PADS=1 -DFELUCCA_FM6_PK_80S=1"
run() { echo "== $1"; d=$1; shift; "$@" || { fail=1; echo "!! FAILED: $d"; : > "$U.fail"; }; }

# ---- the units (see the top): nu started, np printed; unit N's files $JD/N.out, .rc (its exit status, written last
# by its EXIT trap: there = done), .fail (a run in it failed); its PID in P<N>
JD="$OUT/jobs"
rm -rf "$JD"
mkdir -p "$JD"
if [ -n "${GOLDEN_UPDATE:-}${BUDGET_UPDATE:-}" ]; then
    PAR=1
elif [ -n "${JOBS:-}" ]; then
    PAR=$JOBS
else
    PAR=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    PAR=$((PAR - PAR / 4))
fi
case $PAR in ''|*[!0-9]*|0) echo "JOBS=$PAR: a number of 1 or more"; exit 1 ;; esac
NAP=1
sleep 0.05 2>/dev/null && NAP=0.05
nu=0
np=0
U=
running() {   # -> r: the units started and not done
    r=0; i=$((np + 1))
    while [ $i -le $nu ]; do [ -f "$JD/$i.rc" ] || r=$((r + 1)); i=$((i + 1)); done
}
flush() {     # print the done units in order; a unit that stopped (a compile failed) stops the run
    while [ $np -lt $nu ] && [ -f "$JD/$((np + 1)).rc" ]; do
        np=$((np + 1))
        cat "$JD/$np.out"
        [ ! -f "$JD/$np.fail" ] || fail=1
        rc=$(cat "$JD/$np.rc")
        if [ "$rc" != 0 ]; then
            running; while [ $r -gt 0 ]; do sleep $NAP; running; done
            exit "$rc"
        fi
    done
}
job() {       # before a unit: wait for a free slot, print what is done -> U, the new unit's files
    flush; running
    while [ $r -ge "$PAR" ]; do sleep $NAP; flush; running; done
    nu=$((nu + 1)); U="$JD/$nu"
}
jobin() {     # first in a unit (its subshell): its exit status to $U.rc when it ends, however it ends
    trap 'echo $? > "$U.r"; mv "$U.r" "$U.rc"' EXIT
}
jobbg() { eval "P$nu=$!"; }   # after a unit: its PID
after() {     # in a unit: wait for units N.. (their files), stop if one of them stopped
    for k in "$@"; do
        while [ ! -f "$JD/$k.rc" ]; do sleep $NAP; done
        [ "$(cat "$JD/$k.rc")" = 0 ] || exit 1
    done
}
# The slowest tests (a minute or two each: the BLE emulator runs, the builder, the web pages) start at once, beside
# the units (early NAME: the function NAME, its output kept); their unit prints it in its place (run ... early_out
# NAME). JOBS=1: nothing starts early, early_out runs NAME there.
early() {
    [ "$PAR" -gt 1 ] || return 0
    ( E="$JD/e_$1"; trap 'echo $? > "$E.r"; mv "$E.r" "$E.rc"' EXIT; "$1" ) > "$JD/e_$1.out" 2>&1 &
    eval "E_$1=$!"
    early_names="$early_names $1"
}
early_out() {
    eval "[ -n \"\${E_$1:-}\" ]" || { "$1"; return; }
    while [ ! -f "$JD/e_$1.rc" ]; do sleep $NAP; done
    cat "$JD/e_$1.out"
    return "$(cat "$JD/e_$1.rc")"
}
early_names=
stop_units() {   # on the way out early (^C, a failure in this shell): the units still running, ours only
    i=$((np + 1))
    while [ $i -le $nu ]; do
        [ -f "$JD/$i.rc" ] || eval "kill \$P$i" 2>/dev/null || :
        i=$((i + 1))
    done
    for k in $early_names; do
        [ -f "$JD/e_$k.rc" ] || eval "kill \$E_$k" 2>/dev/null || :
    done
}
trap 'stop_units' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Order: build a profile that links first (make build PROFILE=..., ./build.sh): the target checks read its outputs
# (build/felucca.fwsc, .bin, .dis, loader/ota.bin, gen/felucca_config.h). The host tests compile against their own
# headers, build/gen-host, generated here every run with every sample set (tools/build.py --host-headers), never
# the profile's build/gen: one tests/golden.txt covers every preset on any profile.
for f in build/felucca.fwsc build/felucca.bin build/felucca.dis build/loader/ota.bin; do
    [ -f "$f" ] || { echo "$f missing: build a profile that links first (make build PROFILE=..., or ./build.sh)"; exit 1; }
done
HGEN=build/gen-host
export HGEN
python3 tools/build.py --host-headers || { echo "host headers ($HGEN) failed"; exit 1; }
t_ble_rf() { python3 tests/ble_rf_capture_test.py; }
t_ble_emu() { python3 tests/ble_emu_test.py; }
t_ble_central() { python3 tests/ble_emu_central_test.py; }
t_ble_boot() { python3 tests/ble_emu_boot_test.py; }
t_builder() { python3 tests/builder_test.py; r=$?; python3 tests/builder_core_test.py || r=1; return $r; }
t_web() { node web/test_web.mjs; r=$?; node web/test_builder.mjs || r=1; python3 tests/cloud_build_test.py || r=1; return $r; }
# the simulator (docs/WEB.md): the default configuration's firmware as WebAssembly, run under node
t_sim() {
    local py; py="$(python3 -c 'import sys; sys.path.insert(0, "tools"); import deps; print(deps.venv_python())')" || return 1
    mkdir -p build/sim/test
    "$py" tools/sim.py build --wasm --out build/sim/test/sim.wasm >build/sim/test-build.log 2>&1 || { tail -20 build/sim/test-build.log; return 1; }
    node tests/sim_test.mjs build/sim/test/sim.wasm || return 1
    # (one project moved between two instances: a build with PATTERNS, live editing not needed)
    "$py" tools/sim.py build --wasm --config config/profiles/synths_USBaudio_BTaudio_BTmidi.config --out build/sim/test/sim-pj.wasm >build/sim/test-build-pj.log 2>&1 || { tail -20 build/sim/test-build-pj.log; return 1; }
    node tests/sim_projects_test.mjs build/sim/test/sim-pj.wasm
}
early t_ble_emu
early t_ble_central
command -v node >/dev/null 2>&1 && early t_web
early t_ble_boot
early t_builder
early t_ble_rf

job; ( jobin
    $CC -w -Ifirmware/hal -o "$OUT/encoder_test" tests/encoder_test.c
    run "encoders: first click, direction and reversed transitions" "$OUT/encoder_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -w -Ifirmware/hal -o "$OUT/encoder_fast_test" tests/encoder_fast_test.c
    run "encoders: fast turns at 4 / 2 / 1 scans a state, flicks, glitches (X0X)" "$OUT/encoder_fast_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -w -Ifirmware/hal -o "$OUT/encoder_onerest_test" tests/encoder_onerest_test.c
    run "knobs, one rest state (SLOOP 2.3 test): pauses mid-click, bounce, flicks, parked" "$OUT/encoder_onerest_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -w -Ifirmware/hal -o "$OUT/encoder_felucca_test" tests/encoder_felucca_test.c
    run "knobs (Felucca 1.6 #63 / #126): a still knob chattering, 12-click turns to 300/s, slow bouncy turns, a reversal" "$OUT/encoder_felucca_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -w -Ifirmware/hal -o "$OUT/keys_test" tests/keys_test.c
    run "keys: read with their column (SLOOP 2.3): ~1.6 ms sooner, glitch, bounce, chatter" "$OUT/keys_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -o "$OUT/knob_accel_test" tests/knob_accel_test.c
    run "knob acceleration by turn speed (X0X curve), lists exact" "$OUT/knob_accel_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "divides by a variable: each listed with why it cannot be 0 (a wrong value; the div0 trap is off)" python3 tools/div_audit.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "built for size: every firmware source in one list (main-loop files get minsize, tools/size_fns.py)" python3 tools/size_fns.py --check
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -o "$OUT/flash_slice_test" tests/flash_slice_test.c
    run "sector erase in 1 ms interrupts-off windows (suspend 75h / resume 7Ah; parts that ignore the suspend, show no SUS bit, suspend late or lose progress)" "$OUT/flash_slice_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -o "$OUT/storage_test" tests/storage_test.c
    run "flash storage (A/B, torn writes; SLOOP 2.3's checks: the copy a record was written to, object bounds, whole-header read back)" "$OUT/storage_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -DFELUCCA_UP_FM6=1 -o "$OUT/storage_test_upf" tests/storage_test.c
    run "flash storage with UP_FM6 (OBJ_UPFM6 at 0x95000, every object off the SDK's sectors)" "$OUT/storage_test_upf"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -DCZ_NUSER=8 -DSN_SECTORS=8 -o "$OUT/storage_test_cz" tests/storage_test.c
    run "flash storage, a CZ build without UP_FM6 (OBJ_UPFM6 a number only: never written)" "$OUT/storage_test_cz"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -DCZ_NUSER=8 -DSN_SECTORS=8 -DFELUCCA_UP_FM6=1 -o "$OUT/storage_test_czupf" tests/storage_test.c
    run "flash storage, CZ and UP_FM6 (the shared object number at 0x95000)" "$OUT/storage_test_czupf"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -o "$OUT/upfm6_move_test" tests/upfm6_move_test.c
    $CC -DCZ_NUSER=8 -DSN_SECTORS=8 -o "$OUT/upfm6_move_test_cz" tests/upfm6_move_test.c
    run "UP_FM6 off the SDK VM, with the CZ collection built (CZ_NUSER)" "$OUT/upfm6_move_test_cz"
    run "UP_FM6 off the SDK VM: 0x95000, the move from 0xE7000 / 0xE8000 (read only), the flash map and its guard" "$OUT/upfm6_move_test"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -o "$OUT/recovery_test" tests/recovery_test.c
    run "application USB recovery and boot-loop guard" "$OUT/recovery_test"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -o "$OUT/arranger_test" tests/arranger_test.c
    run "song order, timing, repeats and missing scenes" "$OUT/arranger_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/song_audio_test" tests/song_audio_test.c -lm
    run "song: four simultaneous tracks, scene transition and stop" "$OUT/song_audio_test" "$OUT/song-demo.wav"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    mkdir -p "$OUT/font4"      # the font in its alpha format: the 1-bit one draws the same pixels
    python3 tools/gen_font.py "$OUT/font4/felucca_font.h" 4 >/dev/null
    $CC -I"$HGEN" -Ifirmware/src -o "$OUT/font_test1" tests/font_test.c
    $CC -I"$OUT/font4" -I"$HGEN" -Ifirmware/src -o "$OUT/font_test4" tests/font_test.c
    run "font: the 1-bit pixel font draws what the alpha font drew (every glyph, S and L, clipped)" sh -c "\"$OUT/font_test1\" \"$OUT/font1.bin\" && \"$OUT/font_test4\" \"$OUT/font4.bin\" && cmp \"$OUT/font1.bin\" \"$OUT/font4.bin\""
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -I"$HGEN" -Ifirmware/src -o "$OUT/tab_coded_test" tests/tab_coded_test.c
    run "tables: the delta-coded lookup tables (felucca_tables.h TAB_CODED) decode to the plain arrays, bit for bit" "$OUT/tab_coded_test"
    $CC -I"$HGEN" -Ifirmware/src -o "$OUT/lzb_test" tests/lzb_test.c
    run "boot stream: the LZB decoder (main.c lzb_expand) decodes tools/lzb.py's vectors byte for byte" "$OUT/lzb_test"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/studio_drums_test" tests/studio_drums_test.c -lm
    run "drum lanes, kit audio, metronome, record arm, free take" "$OUT/studio_drums_test" "$OUT/drum-styles.wav"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/seq2_test" tests/seq2_test.c -lm
    run "sequencer 2.0: no drift, ratchets, roll, erase / undo, chords, mute / solo" "$OUT/seq2_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 -o "$OUT/backports_test" tests/backports_test.c -lm
    run "backported features (each switch on): chance, QNT SEQ, spring reverb, BASS+, delay halving, motion, PHYS, ACID" "$OUT/backports_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 "-DFELUCCA_ARENA_BYTES=(2u * ARN_SLOT)" -o "$OUT/backports_arena_test" tests/backports_test.c -lm
    run "the same with an arena of two parts' memory placed by need (core/arena.c): FM6, PHYS, ACID swapped on a part" "$OUT/backports_arena_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 -DFELUCCA_TRK_FILT=1 -o "$OUT/backports_tf_test" tests/backports_test.c -lm
    run "backported features with the track FILTER built (SLOOP 2.4's values after P_E7): motion's stored ids, FILT and COMP" "$OUT/backports_tf_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/master_comp_test" tests/master_comp_test.c -lm
    run "master COMP / LIMIT: the static curve (+-0.5 dB), attack / release / AUTO, bit-exact when off, no sample past CEIL, the project's bytes" "$OUT/master_comp_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_TRK_FILT=1 -o "$OUT/trk_filt_test" tests/trk_filt_test.c -lm
    run "track FILTER (SLOOP 2.4): LP / HP on a part, the drum bus and its sends, the project, cost" "$OUT/trk_filt_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CHORDPLUS=1 -o "$OUT/chordplus_test" tests/chordplus_test.c -lm
    run "CHORD+ (SLOOP 2.4): black-key modifiers, under the finger, recorded as played, STRUM, VLEAD" "$OUT/chordplus_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/trk_filt_test_off" tests/trk_filt_test.c -lm
    run "track FILTER: every FILT at 0, the busy mix sample for sample the build without it" sh -c     "[ \"\$('$OUT/trk_filt_test' hash)\" = \"\$('$OUT/trk_filt_test_off' hash)\" ]"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_NOISE=1 -o "$OUT/noise_test" tests/noise_test.c -lm
    run "NOISE engine (FELUCCA_ENG_NOISE=1): UID 15, every preset audible / bounded / voices free, tests/golden_noise.txt, EDIT values, colour, pitched LFSR / META, seeded, retrigger" "$OUT/noise_test" tests/golden_noise.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_DRONE=1 -o "$OUT/drone_test" tests/drone_test.c -lm
    run "DRONE engine (FELUCCA_ENG_DRONE=1): UID 16, every preset audible / bounded / voices free, tests/golden_drone.txt, level, EDIT values, BEAT's two ears, HOLD, no stale side or latch, 4 voices" "$OUT/drone_test" tests/golden_drone.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_PLAITS=1 -o "$OUT/plaits_test" tests/plaits_test.c -lm
    run "PLAITS engine (FELUCCA_ENG_PLAITS=1): UID 17, every preset audible / bounded / voices free, tests/golden_plaits.txt, level, each model's EDIT values, corners, deterministic, pitch, MODEL switch, 4 voices" "$OUT/plaits_test" tests/golden_plaits.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    # (-ffp-contract=off: PROPHET's float voice as the FM-1's FPU unit computes it, no fused multiply-add: an arm64 host fused them, x86 did not)
    $CC -O2 -w -ffp-contract=off -I"$HGEN" -Ifirmware/src $SCENE_ON -o "$OUT/scene_engines_test" tests/scene_engines_test.c -lm
    run "the scene's engines (Clipper's STRINGS VPS TERRAIN WSEQ VECTOR WAVE, SID, PROPHET; UIDs 18..25): every preset audible / bounded / silent after, tests/golden_scene_engines.txt, level, each EDIT value acts, corners, deterministic, pitch" "$OUT/scene_engines_test" tests/golden_scene_engines.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_PHASE_FOLD=1 -o "$OUT/phase_fold_test" tests/phase_fold_test.c -lm
    run "PHASE FOLD (Felucca-Engines, PHASE_FOLD): E7 FOLD brightens, bounded, no DC, folds less above A4, no click on or off" "$OUT/phase_fold_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_PHASE_FOLD=0 -o "$OUT/phase_fold_test_off" tests/phase_fold_test.c -lm
    run "PHASE FOLD off: E7 empty as before" "$OUT/phase_fold_test_off"
    run "PHASE FOLD 0: the render sample for sample the build without it" sh -c "[ \"\$('$OUT/phase_fold_test' hash)\" = \"\$('$OUT/phase_fold_test_off' hash)\" ]"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_VOICE_OWN=1 -o "$OUT/voice_own_test" tests/voice_own_test.c -lm
    run "VOICE_OWN (JIANT): at the budget a part restarts its own tail, the others' tails ring on" "$OUT/voice_own_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_VOICE_OWN=0 -o "$OUT/voice_own_test_off" tests/voice_own_test.c -lm
    run "VOICE_OWN off: the global victim as before" "$OUT/voice_own_test_off"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    # the three drum synths, each a switch (S the 32 kits, K KAWA, P PLAITS), M the sampled kits, X the X0X kits
    for t in 11110 11010 10110 10010 01110 01010 00110 01000 00100 01100 00101; do
        s=$(echo $t | cut -c1) k=$(echo $t | cut -c2) p=$(echo $t | cut -c3) m=$(echo $t | cut -c4) x=$(echo $t | cut -c5)
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_DRUM_SYNTH=$s -DFELUCCA_DS_KAWA=$k -DFELUCCA_DS_PLAITS=$p -DFELUCCA_DRUM_SAMPLED=$m \
            -DFELUCCA_DRUM_X909=$x -DFELUCCA_DRUM_X808=$x -o "$OUT/ds_xkits_$t" tests/ds_xkits_test.c -lm
        run "kits 47.. (32 kits $s, KAWA $k, PLAITS $p, sampled $m, X0X $x): UIDs, stand-ins, lane sources, re-strikes, RISE, METAL, RING / FM, KPUNC, SCRAK" "$OUT/ds_xkits_$t"
    done
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_CZ=1 -o "$OUT/cz_test" tests/cz_test.c -lm
    run "CZ engine (FELUCCA_ENG_CZ=1): UID 13, every preset audible / bounded / voices free, tests/golden_cz.txt, EDIT values, retrigger" "$OUT/cz_test" tests/golden_cz.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_JUNO=1 -o "$OUT/juno_test" tests/juno_test.c -lm
    run "JUNO engine (FELUCCA_ENG_JUNO=1): UID 14, every preset audible / bounded / voices free, tests/golden_juno.txt, level, EDIT values, 6 voices, retrigger" "$OUT/juno_test" tests/golden_juno.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -o "$OUT/juno_cho_test" tests/juno_cho_test.c
    run "JUNO CHORUS (fx/junocho/juno_cho.h): MIX 0 dry, each MODE stereo / bounded / as before, mid-side, headroom" "$OUT/juno_cho_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    # the reverb (fx.c): at 44.1 kHz, then with REV_HALF (the tank at 22.05 kHz) against those numbers; SPRING beside it
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/reverb_test" tests/reverb_test.c -lm
    run "reverb: decay, level, bands, the tail to exactly 0 and idle" "$OUT/reverb_test" "$OUT/reverb_full.txt"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_REV_HALF=1 -o "$OUT/reverb_test_half" tests/reverb_test.c -lm
    run "reverb at half rate (REV_HALF): RT60 within 5 %, the level below 8 kHz within 1 dB of the full rate's" "$OUT/reverb_test_half" "$OUT/reverb_full.txt"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/delay_test" tests/delay_test.c -lm
    run "delay: the gain of each repeat over FDBK / COLOR, 100 % endless, the loop's saturator, idle again when lowered" "$OUT/delay_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/delay_filter_test" tests/delay_filter_test.c -lm
    run "delay filter: LP HP BP PEAK, 12 / 24 dB, RES (the first echo filtered; the loop never runs away below 100 %), its TLV, FREQ in a project (an older COLOR read as its cutoff), SAVE / LOAD" "$OUT/delay_filter_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/dist_test" tests/dist_test.c -lm
    run "DIST: harmonics rise with DST at any level, loudness held, the bass kept, DST 0 / bypass untouched" "$OUT/dist_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -DFELUCCA_MOTION=1 -o "$OUT/insfx_test" tests/insfx_test.c -lm
    run "further inserts FOLD FUZZ PHSR FLNG FRMT WIDE CHRS ECHO GATER (insfx.c, fxi_kern.c, gater.c): at 0 / in no slot / bypassed bit for bit, silence in silence out, bounded, heard; FOLD FUZZ harmonics, FRMT's vowels, WIDE's side; their TLV, motion ids, the type list" "$OUT/insfx_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_INS_XDLY=1 -DFELUCCA_MOTION=1 -o "$OUT/xdly_test" tests/xdly_test.c -lm
    run "XDLY (xdly.c, fxi.c: the cross-modulated dual delay, one instance): bypassed bit for bit, silence in silence out, the tail ends, bounded at FDBK 127; TIME 1 / TIME 2 repeats, MOD acts, FDBK 127 sustains; the drum bus's L != R; one instance (FULL), the master's slot; TLV, motion id, list, pages, ms; a note's repeats outlast it; its CPU guard cost" "$OUT/xdly_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_INS_GATER=1 -DFELUCCA_MOTION=1 -o "$OUT/gater_test" tests/gater_test.c -lm
    run "GATER (gater.c, built alone): bypassed bit for bit, silence in silence out; every pattern and RATE in phase with the transport from PLAY, the swing, PLAY again; no click (a bounded step) through depth, pattern, rate and a jump; SHAPE's ramps; the drum bus's L = R; stopped it runs on; its TLV, motion id" "$OUT/gater_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=1 -o "$OUT/fxi_test" tests/fxi_test.c -lm
    run "the effects memory (fxi.c, fxi_mem.h: CHRS ECHO, auto 10 KB): placed in the main loop, zeroed; a swap keeps one; no room: FULL and bypassed; FULL while the free units are in no one run; given back after the fade-out; a load in owner order, the same every time; never two lines on a unit; the send lines lent" "$OUT/fxi_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=1 -DFELUCCA_INS_JCHORUS=1 -DFELUCCA_INS_FLANGER=1 -o "$OUT/fxi_jc_test" tests/fxi_test.c -lm
    run "the effects memory with JUNO CHORUS and FLNG: a track and the master each, FULL, given back after the fade, the side, its TLV; both on the chorus send's lent line, evicted with a fade" "$OUT/fxi_jc_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=1 -DFELUCCA_INS_XDLY=1 -DFELUCCA_DLY_LEN=32768u -DFELUCCA_DLY_ARENA=72 -o "$OUT/fxi_xd_test" tests/fxi_test.c -lm
    run "the effects memory, DLY_ARENA 72 KB (the delay send's 64 KB line and one ECHO line): XDLY only on the send's line while it is free, FULL while it is in a slot or holds an ECHO, never both; taken back after the fade, zeroed; clean when it comes back" "$OUT/fxi_xd_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 -DFELUCCA_REV_HALF=1 -o "$OUT/backports_rh_test" tests/backports_test.c -lm
    run "backported features with REV_HALF: the spring reverb in the half-rate ROOM's line" "$OUT/backports_rh_test"
) >"$U.out" 2>&1 & jobbg
# the reverb tanks (builder items REV_ROOM / REV_PLATE / REV_FDN8: fx.c ROOM, reverb_alt.c PLATE and FDN8), each alone
# at both budgets: they ring out to exactly 0 and go idle; their numbers (RT60, echo density, ripple, level, host
# instructions) and renders ($OUT/reverb/<tank>-<setting>-mix|wet.wav) for comparison; with SPRING too (TYPE: the tank
# or SPRING, the switch clears each tank)
mkdir -p "$OUT/reverb"
TANKS=   # (each tank its own tanks-T.tsv while they run in parallel; tanks.tsv, all of them in this order, at the end)
# (FELUCCA_REVERB=1 / 2: registry.h's one-tank switch, that tank alone; tests/reverb_proto.c keys its checks on it)
for rt in "plate:-DFELUCCA_REVERB=1 -DFELUCCA_REV_HALF=0" "plate-half:-DFELUCCA_REVERB=1 -DFELUCCA_REV_HALF=1" \
          "fdn8:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=0" "fdn8-half:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=1" \
          "fdn8-pool:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=0 -DFELUCCA_REV_POOL=1" \
          "fdn8-pool-half:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=1 -DFELUCCA_REV_POOL=1"; do   # (REV_POOL: FDN8's ring twice as long)
    t=${rt%%:*}; f=${rt#*:}; TANKS="$TANKS $t"
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/reverb_proto_$t" tests/reverb_proto.c -lm
        : > "$OUT/reverb/tanks-$t.tsv"
        run "reverb tank $t: rings out to exactly 0, idle; numbers and renders in $OUT/reverb" "$OUT/reverb_proto_$t" "$t" "$OUT/reverb" "$OUT/reverb/tanks-$t.tsv"
    ) >"$U.out" 2>&1 & jobbg
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 $f -o "$OUT/backports_${t}_test" tests/backports_test.c -lm
        run "backported features with the reverb tank $t (SPRING beside it)" "$OUT/backports_${t}_test"
    ) >"$U.out" 2>&1 & jobbg
done
# AIRWIN (builder item REV_AIRWIN: reverb_airwin.c, Airwindows' VerbTiny) in its three rings: alone at both rates (16 /
# 8 KB), beside ROOM in the pool (TYPE picks it), with FDN8 in the pool (32 KB): RT60 as the ROOM's, the level, the
# ring-out to exactly 0 at every SIZE / DAMP, full-scale noise / square / DC then silence (bounded, idle); then its
# numbers and renders beside the other tanks'
for ra in "alone:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_AIRWIN=1" "alone-half:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_AIRWIN=1 -DFELUCCA_REV_HALF=1" \
          "room-pool:-DFELUCCA_REV_AIRWIN=1 -DFELUCCA_REV_POOL=1" "fdn8-pool:-DFELUCCA_REV_FDN8=1 -DFELUCCA_REV_AIRWIN=1 -DFELUCCA_REV_POOL=1"; do
    t=${ra%%:*}; f=${ra#*:}
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/reverb_airwin_$t" tests/reverb_airwin_test.c -lm
        run "reverb AIRWIN ($t): RT60 and level as the ROOM's, rings out to exactly 0, full scale bounded, idle" "$OUT/reverb_airwin_$t"
    ) >"$U.out" 2>&1 & jobbg
done
for rt in "airwin:-DFELUCCA_REVERB=3 -DFELUCCA_REV_HALF=0" "airwin-half:-DFELUCCA_REVERB=3 -DFELUCCA_REV_HALF=1"; do
    t=${rt%%:*}; f=${rt#*:}; TANKS="$TANKS $t"
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/reverb_proto_$t" tests/reverb_proto.c -lm
        : > "$OUT/reverb/tanks-$t.tsv"
        run "reverb tank $t: rings out to exactly 0, idle; numbers and renders in $OUT/reverb" "$OUT/reverb_proto_$t" "$t" "$OUT/reverb" "$OUT/reverb/tanks-$t.tsv"
    ) >"$U.out" 2>&1 & jobbg
done
# REVERB > TYPE (rev_type.c, fx.c rev_bus): the algorithms built, on the device's list; every pair switched at run
# time (a fade, the shared line cleared, the new one from silence, then exactly 0 and idle); projects (saved, older
# ones, an algorithm not built: the first one, MISSING); one built: no TYPE
for rs in "all:-DFELUCCA_REV_PLATE=1 -DFELUCCA_REV_FDN8=1 -DFELUCCA_REV_AIRWIN=1 -DFELUCCA_SPRING=1" \
          "all-half:-DFELUCCA_REV_PLATE=1 -DFELUCCA_REV_FDN8=1 -DFELUCCA_REV_AIRWIN=1 -DFELUCCA_SPRING=1 -DFELUCCA_REV_HALF=1" \
          "room-plate:-DFELUCCA_REV_PLATE=1" "fdn8-spring:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_FDN8=1 -DFELUCCA_SPRING=1" \
          "airwin-spring:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_AIRWIN=1 -DFELUCCA_SPRING=1" \
          "room:" "plate:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_PLATE=1" "spring:-DFELUCCA_REV_ROOM=0 -DFELUCCA_SPRING=1"; do
    t=${rs%%:*}; f=${rs#*:}
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 $f -o "$OUT/reverb_select_$t" tests/reverb_select_test.c -lm
        run "REVERB > TYPE, $t built: the list, every switch clean (fade, cleared, exactly 0, idle), projects, MISSING" "$OUT/reverb_select_$t"
    ) >"$U.out" 2>&1 & jobbg
done
# the performance macros (firmware/src/core/macro.c): with MACROS, ENERGY and motion recording; once without, for the hash
MACROS_ON="-DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -DFELUCCA_MOTION=1"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $MACROS_ON $SEC4 -o "$OUT/macro_test" tests/macro_test.c -lm
    run "performance macros: mapping per engine, storage, ENERGY bands, motion, audible on the mix (build/host/macro-*.wav)" "$OUT/macro_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/macro_test_off" tests/macro_test.c -lm
    run "macros at home: the 4-track mix renders bit-identical with and without the switch" sh -c \
        "[ \"\$('$OUT/macro_test' hash)\" = \"\$('$OUT/macro_test_off' hash)\" ]"
) >"$U.out" 2>&1 & jobbg
# the LFO's RATE synced to the tempo (voice.c lfo_inc, SLOOP 2.6's LFO 2 SYNC): with the macros and without
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $MACROS_ON $SEC4 -o "$OUT/lfo_sync_test" tests/lfo_sync_test.c -lm
    run "LFO RATE below 0: SLOOP 2.6's SYNC note values, free rates as before, with the transport clock (tempo, nudge), stopped at the tempo, PHS retrigger, macros leave it" "$OUT/lfo_sync_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/lfo_sync_test_nomac" tests/lfo_sync_test.c -lm
    run "LFO RATE below 0, without the macros" "$OUT/lfo_sync_test_nomac"
) >"$U.out" 2>&1 & jobbg
# LFO 2 (always built: voice.c lfo2_tick, project.c l2_pack, sl24 import / export)
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/lfo2_test" tests/lfo2_test.c -lm
    run "LFO 2: params, off = nothing, free / synced RATE, PIT FLT SHP AMP maths, POL, S&H without rng(), TRIG, renders, project round trip, old projects" "$OUT/lfo2_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_MACROS=1 -o "$OUT/lfo_slots_test" tests/lfo_slots_test.c -lm
    run "LFO DEST's two slots: the depths of before exactly (sound, projects, user presets byte for byte), the tagged layout, values moved for the block (PAN LVL ATK DEC RES E1..E8), L1RT L1AMT L2RT L2AMT bounded, macros, old locks" "$OUT/lfo_slots_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $ENV1_ENG -o "$OUT/env1_test" tests/env1_test.c -lm
    run "ENV 1 MOD: every engine's four cards (paths, moves, part moves, aliases), at 0 the default's render, each heard, the amplitude wired, the plain path and env_post the same, PIT +-31.5 st, E1 AMT bounded, ENV DEST's slots of before folded (FLT SHP PIT moves, the rest to a free ENV2), ENV2's default DEST A per engine" "$OUT/env1_test"
    rm -rf "$OUT/gen-env1-slice" && mkdir -p "$OUT/gen-env1-slice" && cp "$HGEN"/*.h "$OUT/gen-env1-slice/"   # (SLICE's BREAK: its cards heard)
    FELUCCA_SLICE=1 FELUCCA_SAMPLES_SKIP= python3 tools/gen_samples.py "$OUT/gen-env1-slice/felucca_samples.h" >/dev/null
    $CC -O2 -w -I"$OUT/gen-env1-slice" -Ifirmware/src $ENV1_ENG -o "$OUT/env1_test_slc" tests/env1_test.c -lm
    run "ENV 1 MOD with SLICE's BREAK in the headers: SLICE's four cards (TONE PIT DCAY PTCH) heard" "$OUT/env1_test_slc"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/lfo_slots_test0" tests/lfo_slots_test.c -lm
    run "LFO DEST's two slots without the macros" "$OUT/lfo_slots_test0"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -DFELUCCA_TRK_FILT=1 -DFELUCCA_MASTER_COMP=1 -DFELUCCA_MACROS=1 -o "$OUT/lfo_slots_test_ins" tests/lfo_slots_test.c -lm
    run "LFO DEST's INS1 / INS2 named by every insert type built (FILT COMP FOLD ... ECHO), empty, swapped" "$OUT/lfo_slots_test_ins"
) >"$U.out" 2>&1 & jobbg
BP23_ON="-DFELUCCA_REC_MODES=1 -DFELUCCA_LIGHTS=1"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $BP23_ON -o "$OUT/bp23_test" tests/bp23_test.c -lm
    run "SLOOP 2.3 / X0X 0.10.1 backports (each switch on): no stuck note after a VOICE change, overload fades, REC modes and count-in, glides" "$OUT/bp23_test"
) >"$U.out" 2>&1 & jobbg
SL24_ON="-DFELUCCA_DIV_LONG=1 -DFELUCCA_DLY_DOT=1 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_MOTION=1 -DFELUCCA_QCHAIN=1"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SL24_ON $SEC4 -o "$OUT/sl24_seq_test" tests/sl24_seq_test.c -lm
    run "SLOOP 2.4 sequencer backports (each switch on): DIV 1/2..2BAR, delay 1/8D 1/16D, micro timing, fills, parameter locks (beside motion)" "$OUT/sl24_seq_test"
) >"$U.out" 2>&1 & jobbg
# the automation store (docs/UI-OPTIMIST-DESIGN.md 7 The automation store): the int8 check, a render equal to 2907501's, the store
job; ( jobin
    $CC -O1 -w -I"$HGEN" -Ifirmware/src $SEC4 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_MOTION=1 -DFELUCCA_PLOCK=1 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_TRK_FILT=1 -DFELUCCA_CHORDPLUS=1 -o "$OUT/auto_int8_check" tests/auto_int8_check.c -lm
    run "the automation store's int8 check: every lockable / recordable value (TP[], every engine's EDIT) fits a signed byte" "$OUT/auto_int8_check"
    $CC -O1 -w -I"$HGEN" -Ifirmware/src $SEC4 $ENV1_ENG -DFELUCCA_ENG_FM6=1 -DFELUCCA_MOTION=1 -DFELUCCA_PLOCK=1 -DFELUCCA_TRK_FILT=1 -DFELUCCA_CHORDPLUS=1 -o "$OUT/auto_int8_all" tests/auto_int8_check.c -lm
    run "every engine built: every value of a sound fits the signed byte a user preset keeps it in (upreset.c UPB4)" "$OUT/auto_int8_all"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SL24_ON -DFELUCCA_CHANCE=1 $SEC4 -o "$OUT/auto_render" tests/auto_render.c -lm
    run "the automation store's render: nudges, fills, locks, motion, chance bits, 8 bars: the mix equal to 2907501's (micro timing as before)" "$OUT/auto_render"
    run "the automation store's CPU: a step's events (24 locks + 30 hold events; a full list of 128), instructions a step within its budget" "$OUT/auto_render" cpu
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/auto_test" tests/auto_test.c -lm
    run "the automation store: 128 events, both kinds on one parameter, fills, drum chance, the old forms read and written (MOTN, extras, V1 patterns), V2 and the new extras form, a scene and the autosave, 2.4 export / import, undo, AUTO_GET / AUTO_SET, HOLD, SL24_GET's lost words (v12: motion bit 13, chance the second word; the v11 reply unchanged)" "$OUT/auto_test"
) >"$U.out" 2>&1 & jobbg
# the step conditions alone (FELUCCA_COND, Felucca 1.5.6: the store built for them, no other step extra)
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/cond_test" tests/cond_test.c -lm
    run "step conditions alone: 1:2 2:4 3:3 4:4 over 24 passes, PLAY again from pass 1, a synth step's ratchet silent with it" "$OUT/cond_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/gen_test" tests/gen_test.c -lm
    run "note generators (seq/gen.c): EUCLID = the old writer, REPLACE ADD MASK, WRITE = heard (MIDI out), determinism, chance and conditions on generated notes, GEN_TLV" "$OUT/gen_test"
) >"$U.out" 2>&1 & jobbg
# the drum lanes' motion and locks (seq/auto.h AUTO_LANE, drums/lane_play.c, seq/auto_lane.c), and a build that does not play them
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/lane_auto_test" tests/lane_auto_test.c -lm
    run "drum lane motion and locks: the id block, the old forms leave them out (V2 / the new extras form), heard at the hit (lock, hold, both), the patch never written, STOP / PLAY / a pattern switch let go, fills, the UIs' calls (no lock without the hit), AUTO_SET, the 2.4 lost bit" "$OUT/lane_auto_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DDLA_ON=0 -o "$OUT/lane_auto_old" tests/lane_auto_test.c -lm
    run "drum lane motion and locks read by a build that does not play them (DLA_ON 0): kept through every form, none heard, the render as without them, the track's own events played, MISS: LANE MOTION" "$OUT/lane_auto_old"
) >"$U.out" 2>&1 & jobbg
# slot-relative automation (seq/auto_slot.c, always built since 2026-10-18): locks and motion on an insert slot's own values
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/slot_auto_test" tests/slot_auto_test.c -lm
    run "slot-relative automation (SLOT_AUTO): the ids, EQ3's bands by (slot, parameter), a lock / hold / both heard, STOP lets go, YES, the knob, a swap clears that slot" "$OUT/slot_auto_test"
) >"$U.out" 2>&1 & jobbg
# POLY notes' own ends (seq/ngate.h) beside the drum lanes' events: the stored forms, save / load, patterns, the editor, 2.4
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/ngate_store_test" tests/ngate_store_test.c -lm
    run "POLY notes' own ends + drum lane events: ids apart, events only in the stored forms (capture / apply), an older project, a scene, the autosave, a pattern copied and launched, TRACK_STEP v12, AUTO_GET / SET, the 2.4 lost bit 5, a full list" "$OUT/ngate_store_test"
) >"$U.out" 2>&1 & jobbg
# the drum track's ACCENT lane (core.h DRUM_ACC, drums.c dacc_vel, seq/ngate.h): heard, stored, the editor, 2.4; off; with ENERGY
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drum_accent_test" tests/drum_accent_test.c -lm
    run "drum ACCENT lane: a lane for the step helpers, the velocity it adds, every hit of its step raised (voice, MIDI, ratchets, a lock of the amount, chance), no voice of its own, live input / erase / undo, stored as events (capture / apply, an older project, a scene, the autosave, a pattern), DRUM_STEP bits, the 2.4 lost bit 12" "$OUT/drum_accent_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_DRUM_ACCENT=0 -o "$OUT/drum_accent_off" tests/drum_accent_test.c -lm
    run "drum ACCENT lane in a build without DRUM_ACCENT: kept, stored and edited as ever, not heard" "$OUT/drum_accent_off"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -o "$OUT/drum_accent_energy" tests/drum_accent_test.c -lm
    run "drum ACCENT lane with the ENERGY macro: the band's step keeps its accent" "$OUT/drum_accent_energy"
) >"$U.out" 2>&1 & jobbg
# the drum lanes' LFOs (drums/lane_play.c, core/lfo_slot.h, storage/dlfo_rec.c, sections/dlfo_rec_log.c), and a build that does not play them
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/lane_lfo_test" tests/lane_lfo_test.c -lm
    run "drum lane LFOs: off (no slot acting) costs nothing and renders as none, the value at the hit as figured, two slots, with a lock, synced 1BAR over 8 bars at 2 tempos, restart at PLAY, FADE, stopped, the record (forms, versions, a section, the autosave, an older section), the 2.4 lost bit, the cost" "$OUT/lane_lfo_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DDLFO_ON=0 -o "$OUT/lane_lfo_old" tests/lane_lfo_test.c -lm
    run "drum lane LFOs in a build that does not play them (DLFO_ON 0): kept through the record, never heard (the render as without), MISS: LANE LFO" "$OUT/lane_lfo_old"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_QCHAIN=1 $SEC4 -o "$OUT/sl24_chain_test" tests/sl24_chain_test.c -lm
    run "SLOOP 2.4 quick chain (FELUCCA_QCHAIN): sections in order, each for its bars, looped; STOP ends it" "$OUT/sl24_chain_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/undo_test" tests/undo_test.c -lm
    run "undo history: 300-level chains bit-exact on every track, links, eviction, ring sizing, recording while playing" "$OUT/undo_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_UNDO_HISTORY=0 -o "$OUT/undo_test1" tests/undo_test.c -lm
    run "undo: the single level (FELUCCA_UNDO_HISTORY=0)" "$OUT/undo_test1"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/clock_sync_test" tests/clock_sync_test.c -lm
    run "MIDI clock: follow USB / TRS (SYNC AUTO, jitter, ramps, start / stop / continue / SPP), on-time steps" "$OUT/clock_sync_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/clock_out_test" tests/clock_out_test.c -lm
    run "MIDI clock out: 24 PPQN at 5 tempos, with the sound, FA / FC / F2 + FB, OFF, tempo change, no swing, ext follow" "$OUT/clock_out_test"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drumkit_test" tests/drumkit_test.c -lm
    run "synthesised drum kits: every kit x sound bounded, audible, finite, levels, cost" "$OUT/drumkit_test" "$OUT/drum-kits.wav" "$OUT/drum-kits.txt"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drum_edit_test" tests/drum_edit_test.c -lm
    run "drum lanes: sound editor offsets on a hit, user samples on a lane, other kits' sounds, FUN7 / FUN8 / FUN9 -> FUNA" "$OUT/drum_edit_test" "$OUT"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drum_kits_test" tests/drum_kits_test.c -lm
    run "user drum kits: bank round trip on simulated flash, torn write, USR3 64 KiB, editor cmds 36..42" "$OUT/drum_kits_test"
) >"$U.out" 2>&1 & jobbg
X0X_ON="-DFELUCCA_DRUM_X909=1 -DFELUCCA_DRUM_X808=1"   # the X0X kits (off by default; tools/builder: DRUM_X0X909 / 808)
mkdir -p "$OUT/x0x_drums"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -o "$OUT/x0x_drums_test" tests/x0x_drums_test.c -lm
    run "X0X 909 / 808 kits: every note bounded and ended, levels, velocity, offsets, ratchets, SOUND editor, sends, mute, lanes, projects" "$OUT/x0x_drums_test" "$OUT/x0x_drums"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/kits_sound_test" tests/kits_sound_test.c -lm
    run "every built drum kit makes sound: each lane of each kit, hit alone" "$OUT/kits_sound_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -o "$OUT/kits_sound_x0x" tests/kits_sound_test.c -lm
    run "every built drum kit makes sound, the X0X kits built (drums_mix's glides count the X0X channels)" "$OUT/kits_sound_x0x"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -DFELUCCA_DRUM_SYNTH=0 -DFELUCCA_DS_KAWA=0 -DFELUCCA_DS_PLAITS=0 -DFELUCCA_DRUM_SAMPLED=0 -o "$OUT/kits_sound_x0xonly" tests/kits_sound_test.c -lm
    run "the X0X kits as the only drum source: every sound they have heard, the ones they lack silent (no stand-in)" "$OUT/kits_sound_x0xonly"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_DRUM_X909=1 -DFELUCCA_DRUM_SYNTH=0 -DFELUCCA_DS_KAWA=0 -DFELUCCA_DS_PLAITS=0 -DFELUCCA_DRUM_SAMPLED=0 -o "$OUT/kits_sound_x909only" tests/kits_sound_test.c -lm
    run "the X0X 909 alone: the 808 plays the 909 (its stand-in), every sound the 909 has heard" "$OUT/kits_sound_x909only"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_DRUM_X909=1 -DFELUCCA_X909_CYM=0 -o "$OUT/x0x_drums_test1" tests/x0x_drums_test.c -lm
    run "X0X 909 without its ride and crash samples; the 808 not built: its stand-in" "$OUT/x0x_drums_test1"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -DFELUCCA_X909_CYM=2 -o "$OUT/x0x_drums_test2" tests/x0x_drums_test.c -lm
    run "X0X kits with the 6-bit ride and crash (X909_CYM 2): the same checks, the 6-bit samples read back" "$OUT/x0x_drums_test2"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/x0x_drums_test0" tests/x0x_drums_test.c -lm
    run "X0X kits not built: projects naming them play the stand-ins and keep the kit" "$OUT/x0x_drums_test0"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/drum_sends_test" tests/drum_sends_test.c -lm
    run "drum lane sends: per-lane REV / DLY / CHO, FUNA + drum records (torn writes), DKB3 kits, editor v2" "$OUT/drum_sends_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fx_slots_test" tests/fx_slots_test.c -lm
    run "FX slots: the layout, a type in no slot unheard (the mix with it at 0, sample for sample), the FX record (sections, arena, autosave, keys)" "$OUT/fx_slots_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_TRK_FILT=1 -o "$OUT/fx_slots_tf_test" tests/fx_slots_test.c -lm
    run "FX slots with the track FILTER built (D6: a slot type): a project without a record and a FILTER in use plays it in the slot it silences least" "$OUT/fx_slots_tf_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -o "$OUT/fx_slots_ifx_test" tests/fx_slots_test.c -lm
    run "FX slots with the further inserts built (INS_*): the same layout, renders and records" "$OUT/fx_slots_ifx_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -DFELUCCA_INS_JCHORUS=1 -DFELUCCA_USB_AUDIO=1 -o "$OUT/fx_slots_usb_test" tests/fx_slots_test.c -lm
    run "FX slots with USB audio: the USB input as a track (level, pan, mute, FX on, sends, its insert slots, its record; every insert type on S1 / S2: heard, neutral = dry, the record, the pool FULL)" "$OUT/fx_slots_usb_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/eq3_test" tests/eq3_test.c -lm
    run "EQ3 (insfx/eq3.c): flat = bypass bit for bit, the crossfade in and out, flat sum within 0.01 dB, band isolation, KILL, no click through 0 dB, master L = R, its TLV, its CPU" "$OUT/eq3_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -o "$OUT/fx_rt_test" tests/fx_rt_test.c -lm
    run "FX returns holding any type (fx-pool): lines, a send pinned to its own, swaps, the send through the type 100 % wet times RET, the pool on a return, RET_TLV" "$OUT/fx_rt_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fx_rsend_test" tests/fx_rsend_test.c -lm
    run "the sends between the returns: A -> B, A -> C, B -> C rendered in the order A B C whatever lines they own (chain gains 2x 3x 4x), after RET, ramped, a bus woken, every send 0: no chain, RSND_TLV, an old project at 0" "$OUT/fx_rsend_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -DFELUCCA_CPU_GUARD=1 -o "$OUT/fx_rsend_ifx_test" tests/fx_rsend_test.c -lm
    run "... every further insert built, the CPU guard on" "$OUT/fx_rsend_ifx_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fx_returns_test" tests/fx_returns_test.c -lm
    run "FX returns: CHO / DLY / REV RET at one half halve the wet, at 0 silent with the bus not run (its line cleared, no old tail back), the FX record, PROJECT SAVE / LOAD" "$OUT/fx_returns_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -o "$OUT/fx_returns_cg_test" tests/fx_returns_test.c -lm
    run "FX returns with the CPU guard built: the guard's REVERB fade at REV RET 64 is unity's halved, then off" "$OUT/fx_returns_cg_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_FDN8=1 -DFELUCCA_REV_POOL=1 -o "$OUT/fx_returns_fdn8_test" tests/fx_returns_test.c -lm
    run "FX returns with FDN8 alone in the pool and the CPU guard (the mots profile): 10 minutes, the reverb back after every REV RET 0 and guard level" "$OUT/fx_returns_fdn8_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drum_lanemix_test" tests/drum_lanemix_test.c -lm
    run "drum lanes PAN / MUTE / SOLO: the mix (sample for sample), the lanes' meters, the FX record TLV, PROJECT SAVE / LOAD" "$OUT/drum_lanemix_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -o "$OUT/drum_lanemix_x0x" tests/drum_lanemix_test.c -lm
    run "drum lanes PAN / MUTE / SOLO with the X0X kits built: the same, the X0X channels panned and metered per lane" "$OUT/drum_lanemix_x0x"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/backup_test" tests/backup_test.c -lm
    run "backup / restore: every stored object round trip, torn transfers and commits, an older project migrates" "$OUT/backup_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/punch_test" tests/punch_test.c -lm
    run "punch-in FX: 16 effects, bounded, dry after release, FX-held keys" "$OUT/punch_test" "$OUT/punch-fx.wav"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/editor_sync_test" tests/editor_sync_test.c -lm
    run "editor protocol v9: every track pushed, coalesced, never before a reply; the stream; the meter tap" "$OUT/editor_sync_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_INS_JCHORUS=1 -DFELUCCA_INS_GATER=1 -DFELUCCA_INS_FOLD=1 -DFELUCCA_INS_FUZZ=1 -DFELUCCA_INS_PHASER=1 -DFELUCCA_INS_FLANGER=1 -DFELUCCA_INS_FORMANT=1 -DFELUCCA_INS_WIDTH=1 -DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=1 -DFELUCCA_USB_AUDIO=1 -o "$OUT/editor_fxp_test" tests/editor_sync_test.c -lm
    run "editor protocol v13: every FX owner, slot, type and parameter (every insert type built: EQ3, DYN, GATER, JUNO, the lanes, the returns, the USB input)" "$OUT/editor_fxp_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -o "$OUT/editor_v14_test" tests/editor_sync_test.c -lm
    run "editor protocol v14 with the automation store whole: UNDO (the editor's writes are levels), NOTE (the ring to the audio interrupt, no hanging note), SOLO, STEPLANE off (the editor without the UI: its checks are the Optimist UI test's)" "$OUT/editor_v14_test"
    # the ring to the audio interrupt masks no interrupt: seq/ednote.c has one writer and one reader (no cli / sti, no fm1_irq_*, no IRQ_SAVE)
    if grep -nE "fm1_irq_|IRQ_SAVE|IRQ_RESTORE|__asm__ volatile\(\"(cli|sti)" firmware/src/seq/ednote.c | grep -v "^[0-9]*:\s*\*\|/\*"; then echo "!! FAILED: seq/ednote.c masks an interrupt"; : > "$U.fail"; else echo "ednote.c: no interrupt masked (the ISR path takes the ring only)  ok"; fi
) >"$U.out" 2>&1 & jobbg
# (each variant a unit of its own, 2026-10-19: in one unit they ran one after another, the run's tail on a 112-CPU host)
for off in "-DFELUCCA_ED_UNDO=0" "-DFELUCCA_ED_NOTE=0" "-DFELUCCA_ED_SOLO=0" "-DFELUCCA_ED_STEPLANE=0" "-DFELUCCA_ED_UNDO=0 -DFELUCCA_ED_NOTE=0 -DFELUCCA_ED_SOLO=0 -DFELUCCA_ED_STEPLANE=0"; do
    job; ( jobin
        n=$(echo "$off" | tr -c 'A-Z\n' '_' | tr -s '_')
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 $off -o "$OUT/editor_v14off$n" tests/editor_sync_test.c -lm
        run "editor protocol v14 with items left out ($off): no command, no INFO tag, the rest as before" "$OUT/editor_v14off$n"
    ) >"$U.out" 2>&1 & jobbg
done
for off in "-DFELUCCA_ED_FXP=0" "-DFELUCCA_ED_DLAN=0" "-DFELUCCA_ED_SONG=0" "-DFELUCCA_ED_FAVS=0" "-DFELUCCA_ED_FXP=0 -DFELUCCA_ED_DLAN=0 -DFELUCCA_ED_SONG=0 -DFELUCCA_ED_FAVS=0"; do
    job; ( jobin
        n=$(echo "$off" | tr -c 'A-Z\n' '_' | tr -s '_')
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 $off -o "$OUT/editor_off$n" tests/editor_sync_test.c -lm
        run "editor protocol v13 with items left out ($off): no command, no INFO tag, the rest as before" "$OUT/editor_off$n"
    ) >"$U.out" 2>&1 & jobbg
done
mkdir -p "$OUT/nochord"   # (its screens apart: the STEP page without the chord names)
mkdir -p "$OUT/nb"

mkdir -p "$OUT/menu-noclko"
mkdir -p "$OUT/menu-ble"
# the audio ISR against the main loop while recording live notes (2026-10-15 freeze): fixed seeds, one core and the
# second core as a thread (tests/audio_hang_batch.sh: the long mode, ASan / UBSan / TSan, signal preemption)
job; ( jobin
    CC="$CC" bash tests/audio_hang_build.sh "$OUT/audio_hang_stress"
    CC="$CC" bash tests/audio_hang_build.sh "$OUT/audio_hang_dual" -DHOST_DUAL=1
    run "live recording under random keys and edits, the audio half at random points: no hang, the ISR's state consistent (seed 20261015)" env HANG_FRAMES=6000 "$OUT/audio_hang_stress"
    run "... the second core as a thread, the jobs split at random (seed 7)" env HANG_FRAMES=4000 HANG_SEED=7 "$OUT/audio_hang_dual"
    run "... song mode STOP / live sections applied inside the ISR: no sti there (fx_slots.c IRQ_SAVE), the state as found" env HANG_FRAMES=1500 HANG_SEED=3 "$OUT/audio_hang_stress"
    run "... preempted at any instruction, TIMER5 nesting into the render (HANG_NEST, as the FM-1 showed)" env HANG_MODE=sig HANG_NEST=1 HANG_SECS=4 "$OUT/audio_hang_stress"
) >"$U.out" 2>&1 & jobbg
# the Optimist UI (FELUCCA_UI=1, ui/optimist): rows, keys, the confirm, undo / redo, the layers, TEMPO, SONG, messages,
# fuzz; five switch sets (the last on the real section log with PATTERNS)
mkdir -p "$OUT/optimist"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/ui_optimist_test" tests/ui_optimist_test.c -lm
    run "Optimist UI: every screen's rows, SELECT / ALGORITHM / PRESETS / knobs, YES / NO, the confirm, undo chords, fuzz" "$OUT/ui_optimist_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
# the dual-core build's CPU1 load (system/dual_load.h): none in a single-core build; smoothed as song.cpu_q8, in range,
# OFF when CPU1 is down; SYSTEM's CPU row with it (DUAL_LOAD alone: the readout without the second core's render)
job; ( jobin
    $CC -Ifirmware/src -DFELUCCA_DUAL=0 -o "$OUT/dual_load_off_test" tests/dual_load_test.c
    run "CPU1 load, DUAL=0: nothing built" "$OUT/dual_load_off_test"
    $CC -Ifirmware/src -DFELUCCA_DUAL=2 -o "$OUT/dual_load_test" tests/dual_load_test.c
    run "CPU1 load, DUAL=2: smoothed per audio half as song.cpu_q8, 0..100 %, OFF when down" "$OUT/dual_load_test"
) >"$U.out" 2>&1 & jobbg
mkdir -p "$OUT/optimist-dual"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DDUAL_LOAD=1 $SEC4 -o "$OUT/ui_optimist_dual_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the CPU1 load: SYSTEM's CPU row CLOCK, CPU0, CPU1 (OFF when down)" "$OUT/ui_optimist_dual_test" "$OUT/optimist-dual"
) >"$U.out" 2>&1 & jobbg
mkdir -p "$OUT/optimist-x0x"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $X0X_ON $SEC4 -o "$OUT/ui_optimist_x0x_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the X0X kits: the same; the drum ENV of a LEVEL-only sound (the 909's rim shot)" "$OUT/ui_optimist_x0x_test" "$OUT/optimist-x0x"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BACKPORTS_ON -DFELUCCA_MACROS=1 $SEC4 -o "$OUT/ui_optimist_bp_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the backports (ACID GEN, BRIGHT, BASS+, MOTION, MACRO)" "$OUT/ui_optimist_bp_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BP23_ON -DFELUCCA_CDC=1 -DFELUCCA_TRK_FILT=1 -DFELUCCA_PLOCK=1 $SEC4 -o "$OUT/ui_optimist_bp23_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with LIGHTS, USB SERIAL, the track FILTER row" "$OUT/ui_optimist_bp23_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    mkdir -p "$OUT/optimist-scrn"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BP23_ON -DFELUCCA_CDC=1 -DFELUCCA_BRIGHT=1 -DEDSCRN_GOLDEN=1 $SEC4 -o "$OUT/ui_optimist_scrn_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with LIGHTS, USB SERIAL, BRIGHT: the editor's SCRN lines (GLOBAL, SYSTEM) equal tests/scrn_table.json, the web mock's copy" "$OUT/ui_optimist_scrn_test" "$OUT/optimist-scrn"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    mkdir -p "$OUT/optimist-stl"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=1 -DEDSTL_GOLDEN=1 $SEC4 -o "$OUT/ui_optimist_stl_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the automation store whole: the editor's STEPLANE lists equal the device's lock pages (hp_locks / lock_param) and tests/steplane_table.json, the web mock's data; locks, KIND, the extras, a drum lane's level" "$OUT/ui_optimist_stl_test" "$OUT/optimist-stl"
) >"$U.out" 2>&1 & jobbg
for off in "-DFELUCCA_ED_SCRN=0" "-DFELUCCA_ED_SCRS=0" "-DFELUCCA_ED_SCOPE=0" "-DFELUCCA_ED_SCRN=0 -DFELUCCA_ED_SCRS=0 -DFELUCCA_ED_SCOPE=0" "-DFELUCCA_ED_SOLO=0"; do
    job; ( jobin   # (a unit each, its screens in a folder of its own)
        n=$(echo "$off" | tr -c 'A-Z\n' '_' | tr -s '_')
        mkdir -p "$OUT/optimist-edoff$n"
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BP23_ON -DFELUCCA_CDC=1 -DFELUCCA_BRIGHT=1 -DEDSCRN_GOLDEN=1 $SEC4 $off -o "$OUT/ui_optimist_edoff$n" tests/ui_optimist_test.c -lm
        run "Optimist UI with editor items left out ($off): no command, no INFO tag, every other test as before" "$OUT/ui_optimist_edoff$n" "$OUT/optimist-edoff$n"
    ) >"$U.out" 2>&1 & jobbg
done
job; ( jobin
    mkdir -p "$OUT/optimist-edofftools"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=1 $SEC4 -DFELUCCA_ED_TOOLS=0 -o "$OUT/ui_optimist_edofftools" tests/ui_optimist_test.c -lm
    run "Optimist UI with the editor's TOOLS left out (-DFELUCCA_ED_TOOLS=0)" "$OUT/ui_optimist_edofftools" "$OUT/optimist-edofftools"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 $SEC4 -o "$OUT/ui_optimist_sx_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the step extras (STEP: nudge, chance, fill, locks)" "$OUT/ui_optimist_sx_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 $SEC4 -o "$OUT/ui_optimist_auto_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the automation store whole (STEP: locks and HOLD, the marks under the steps, drum chance, SHIFT + a step)" "$OUT/ui_optimist_auto_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
# STEP's TOOLS and EUCLID pages and the COND card (Felucca 1.5.6: ui/optimist/op_tools.c), with the store whole and alone
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=1 $SEC4 -o "$OUT/ui_optimist_tools_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the sequence tools (TOOLS: REVERSE RANDOM COOK LENGTH, exact note lengths kept, events moved; GEN / GEN OUT: the generators live, MODE, WRITE asks, one undo; the drum kit and a lane; EDIT SHIFT's events; the COND card)" "$OUT/ui_optimist_tools_test" "$OUT/optimist"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=1 $SEC4 -o "$OUT/ui_optimist_tools1_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the sequence tools and the conditions, no other step extra (COND the one EXTRA card)" "$OUT/ui_optimist_tools1_test" "$OUT/optimist"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=1 -DFELUCCA_GEN_GRIDS=1 -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_optimist_grids_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the sequence tools and GRIDS (GEN_GRIDS): the kit's GRIDS DENSITY GEN OUT pages on DR, WRITE, undo; every value's help line" "$OUT/ui_optimist_grids_test" "$OUT/optimist"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=1 -DFELUCCA_GEN_GRIDS=1 -DFELUCCA_GRIDS_EUCLID=1 -DFELUCCA_GRIDS_LFO=1 -DFELUCCA_GRIDS_VEL=1 -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_optimist_gridsx_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with GRIDS, its Euclidean mode, the kit's LFOs and its velocity (GRIDS_EUCLID, GRIDS_LFO, GRIDS_VEL): GRIDS' OUT card, DENSITY's VEL, DR's LFO button opens the kit's LFO, every value's help line" "$OUT/ui_optimist_gridsx_test" "$OUT/optimist"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=0 -DFELUCCA_ED_SEQ=1 $SEC4 -o "$OUT/ui_optimist_tl_only_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the TOOLS page and no note generators (SEQTOOLS without NOTEGEN): STEP PATTERN TOOLS, the editor's TOOLS says so" "$OUT/ui_optimist_tl_only_test" "$OUT/optimist"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=0 -DFELUCCA_NOTEGEN=1 -DFELUCCA_ED_SEQ=1 -DFELUCCA_GEN_GRIDS=1 -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_optimist_gen_only_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the note generators and GRIDS, no TOOLS page (NOTEGEN without SEQTOOLS): STEP PATTERN GEN GEN OUT, the editor's TOOLS has ops 2..9 only" "$OUT/ui_optimist_gen_only_test" "$OUT/optimist"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=0 -DFELUCCA_NOTEGEN=0 -DFELUCCA_ED_SEQ=1 -DFELUCCA_GEN_GRIDS=1 -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_optimist_grids_only_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with GRIDS alone (GEN_GRIDS without NOTEGEN or SEQTOOLS): GEN and GEN OUT with GRIDS the one TYPE, the kit's GRIDS on DR; the editor's TOOLS: ops 6..9, GRIDS only" "$OUT/ui_optimist_grids_only_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_SECTIONS=16 -DFELUCCA_PATTERNS=1 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_REC_MODES=1 -o "$OUT/ui_optimist_song_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the section log and the patterns (SONG, the scenes, the session grid, the layers on the real log)" "$OUT/ui_optimist_song_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_BLE=1 $SEC4 -o "$OUT/ui_optimist_ble_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with BLE built in: SYSTEM > BT MIDI (OFF / MAN / AUTO, the radio told once; no BT AUDIO page), the settings word bits HOLD 21..22, CARDS 23, BLUETOOTH 24, KNOB COLORS 25 do not collide; YES on the BLE row opens the devices list: NONE / LAST pinned, nearby with bars, no wrap, SAVE picks / connects, HOME back, SHIFT + a button FORGET LAST (the modal), Connecting / Pairing / the code / Connected / Failed" "$OUT/ui_optimist_ble_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_BLE=1 -DBLE_CENTRAL=0 $SEC4 -o "$OUT/ui_optimist_blenc_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with BLE and no scanner built in: the devices list is NONE alone" "$OUT/ui_optimist_blenc_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_BLE=1 -DBLE_CENTRAL=3 $SEC4 -o "$OUT/ui_optimist_blemin_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with BLE_CENTRAL=3 (no pairing): the devices list, remembered devices without bonds, a peer that asks to pair held as FAILED: NEEDS PAIRING" "$OUT/ui_optimist_blemin_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_BLE=1 -DFELUCCA_BT_AUDIO=1 $SEC4 -o "$OUT/ui_optimist_btboth_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with BLE and BT_AUDIO built in: SYSTEM > BT MIDI and BT AUDIO, both on at once, each its MODE, connect, timer and devices list, SAVE / SAVE held per page; the settings record keeps both modes and reads an old single MODE" "$OUT/ui_optimist_btboth_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -fmerge-all-constants -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_INS_XDLY=1 -DFELUCCA_MIDI_CLOCK=1 $SEC4 -o "$OUT/ui_optimist_xdly_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with XDLY built in and equal constants merged (as the target's compiler does): GLOBAL > MIDI CLK OFS shows its own value below 0, not XDLY's TIME" "$OUT/ui_optimist_xdly_test" "$OUT/optimist"
) >"$U.out" 2>&1 & jobbg
# PAGE_LINES (the builder's default): the rows with no picture as GLOBAL's lines (tests/ui_optimist_plines.h), with and
# without the help line
mkdir -p "$OUT/optimist-lines"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PAGE_LINES=1 $SEC4 -o "$OUT/ui_optimist_lines_test" tests/ui_optimist_test.c -lm
    run "Optimist UI, PAGE_LINES: a row with no picture as four lines, the pictures, mixer, GLOBAL, PROJECT's slots as before; random frames" "$OUT/ui_optimist_lines_test" "$OUT/optimist-lines"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PAGE_LINES=1 -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_optimist_lines_help_test" tests/ui_optimist_test.c -lm
    run "... with the help line" "$OUT/ui_optimist_lines_help_test" "$OUT/optimist-lines"
) >"$U.out" 2>&1 & jobbg
# the Optimist UI's help line (PARAM_HELP, ui/optimist/op_help.c; tests/ui_optimist_help.h): every settings line and
# PROJECT cell has its line within 232 px, a turn shows it ~2 s; the values still without a line listed
mkdir -p "$OUT/optimist-help"
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_optimist_help_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the help line: GLOBAL / SYSTEM / PROJECT always, a value turned ~2 s; CALIBRATE, factory resets, the risky actions asked" "$OUT/ui_optimist_help_test" "$OUT/optimist-help"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PARAM_HELP=1 $BP23_ON -DFELUCCA_CDC=1 -DFELUCCA_BLE=1 -DDUAL_LOAD=1 -DFELUCCA_MIDI_CLKOUT=1 -DFELUCCA_BRIGHT=1 -DFELUCCA_COND=1 -DFELUCCA_SEQTOOLS=1 -DFELUCCA_NOTEGEN=1 $SEC4 -o "$OUT/ui_optimist_help2_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the help line, REC MODES, LIGHTS, USB SERIAL, BLE, CLOCK OUT, BRIGHT, LFO 2, the seq tools and the CPU1 load: every line has its help" "$OUT/ui_optimist_help2_test" "$OUT/optimist-help"
) >"$U.out" 2>&1 & jobbg
# fx-pool (2026-10-15): every further insert and EQ3 built, the help line on (tests/ui_optimist_test.c fxpool_tests: a return
# holding PHSR, EQ3 on a return and a track)
job; ( jobin
    mkdir -p "$OUT/optimist-fxpool"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PARAM_HELP=1 $INSFX_ON $SEC4 -o "$OUT/ui_optimist_fxpool_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with every further insert and EQ3: the returns' other types (RET AMT, the cards named), EQ3's rows, the help lines" "$OUT/ui_optimist_fxpool_test" "$OUT/optimist-fxpool"
) >"$U.out" 2>&1 & jobbg
# the USB input with every insert type (tests/ui_optimist_usbfx.h): its TYPE list, USB EQ3 / EQ3 F / GATER / JUNO sets, FULL
job; ( jobin
    mkdir -p "$OUT/optimist-usbfx"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PARAM_HELP=1 $INSFX_ON -DFELUCCA_INS_JCHORUS=1 -DFELUCCA_USB_AUDIO=1 $SEC4 -o "$OUT/ui_optimist_usbfx_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with USB audio and every insert type: the USB input's slots list them all, its USB EQ3 / EQ3 F / GATER / JUNO sets, a pooled type FULL, the help lines" "$OUT/ui_optimist_usbfx_test" "$OUT/optimist-usbfx"
) >"$U.out" 2>&1 & jobbg
# the builder's default SYS_DEBUG=0 (tests/ui_optimist_sysdbg.h): no INTERNALS / USB STATS, INFO is CPU LOAD, UNDERRUNS, BATTERY
job; ( jobin
    mkdir -p "$OUT/optimist-nodbg" "$OUT/optimist-nodbg-dual"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PARAM_HELP=1 -DFELUCCA_USB_AUDIO=1 -DFELUCCA_SYS_DEBUG=0 $SEC4 -o "$OUT/ui_optimist_nodbg_test" tests/ui_optimist_test.c -lm
    run "Optimist UI without the SYSTEM debug pages (SYS_DEBUG=0): no INTERNALS / USB STATS, INFO = CPU LOAD, UNDERRUNS (drops in), BATTERY, large values" "$OUT/ui_optimist_nodbg_test" "$OUT/optimist-nodbg"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PARAM_HELP=1 -DFELUCCA_USB_AUDIO=1 -DFELUCCA_SYS_DEBUG=0 -DDUAL_LOAD=1 $SEC4 -o "$OUT/ui_optimist_nodbg_dual_test" tests/ui_optimist_test.c -lm
    run "Optimist UI without the SYSTEM debug pages, DUAL (SYS_DEBUG=0, DUAL_LOAD): INFO = CPU0, CPU1, UNDERRUNS, BATTERY; KNOB 1 the measure of both" "$OUT/ui_optimist_nodbg_dual_test" "$OUT/optimist-nodbg-dual"
) >"$U.out" 2>&1 & jobbg
# the limited insert types on the Optimist UI (tests/ui_optimist_fxi.h): the free count, WAIT, IN USE, FULL (dim), hidden
# when not built, the help lines; CHRS ECHO in effects memories (DLY_ARENA, beside the delay send's 128 KB) that hold 0 / 4
# (32 KB), 1 / 0 (2 KB) and 4 / 1 (8 KB) of them (the last two with the help line)
for m in "-DFELUCCA_INS_CHORUS=0 -DFELUCCA_INS_DELAY=1 -DFELUCCA_DLY_ARENA=160" "-DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=0 -DFELUCCA_DLY_ARENA=130 -DFELUCCA_PARAM_HELP=1" "-DFELUCCA_INS_CHORUS=1 -DFELUCCA_INS_DELAY=1 -DFELUCCA_DLY_ARENA=136 -DFELUCCA_PARAM_HELP=1"; do
    job; ( jobin
        mkdir -p "$OUT/optimist-fxi$nu"
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 $m -o "$OUT/ui_optimist_fxi_test$nu" tests/ui_optimist_test.c -lm
        run "Optimist UI, limited inserts (${m}): the free count, WAIT, IN USE, FULL dim, a type with no instance hidden, the help lines" "$OUT/ui_optimist_fxi_test$nu" "$OUT/optimist-fxi$nu"
    ) >"$U.out" 2>&1 & jobbg
done
# GATER's pages and picture (tests/ui_optimist_gater.h): its row only while a slot holds it
# (a synth track, the drum track), the cards, the pattern's name, RATE as a division, the picture's playhead, the help lines
for m in "-DFELUCCA_INS_GATER=1" "-DFELUCCA_INS_GATER=1 -DFELUCCA_PARAM_HELP=1"; do
    job; ( jobin
        mkdir -p "$OUT/optimist-gater$nu"
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 $m -o "$OUT/ui_optimist_gater_test$nu" tests/ui_optimist_test.c -lm
        run "Optimist UI, GATER (${m}): its FX row only while a slot holds it (T1, the drum track), DEPTH PAT RATE SHAPE, the pattern's name, RATE a division, the page remembered, the picture and its playhead, the help lines" "$OUT/ui_optimist_gater_test$nu" "$OUT/optimist-gater$nu"
    ) >"$U.out" 2>&1 & jobbg
done
# batch D (tests/ui_optimist_layout.h) on a build without DUST: no DUST card anywhere (the mixer, the master FX, the layers)
job; ( jobin
    mkdir -p "$OUT/optimist-nodust"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_FX_DUST=0 -DFELUCCA_PARAM_HELP=1 -o "$OUT/ui_optimist_nodust_test" tests/ui_optimist_test.c -lm
    run "Optimist UI without DUST built: no DUST card on the mixer's MASTER set, the master FX (INS TONE: the built values first) or a layer; the badges, the empty rule" "$OUT/ui_optimist_nodust_test" "$OUT/optimist-nodust"
) >"$U.out" 2>&1 & jobbg
# favorites (tests/ui_optimist_favorites.h): favorites marked and browsed with the presets (SHIFT2 + PRESETS / SELECT) are in the default build above; without them the
# two gestures do nothing and PRESETS walks the whole list
job; ( jobin
    mkdir -p "$OUT/optimist-nofav"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_FAVORITES=0 -DFELUCCA_PARAM_HELP=1 -o "$OUT/ui_optimist_nofav_test" tests/ui_optimist_test.c -lm
    run "Optimist UI without favorites built: SHIFT2 + PRESETS / SELECT do nothing, PRESETS and SHIFT + PRESETS walk the whole list" "$OUT/ui_optimist_nofav_test" "$OUT/optimist-nofav"
) >"$U.out" 2>&1 & jobbg
# the user's own build (tests/profiles/mots-like.config, a copy of config/my-profiles/mots.config of 2026-10-14; the flags are
# the builder's for it, tools/config_flags.py): knob acceleration, DIV_ORDER, no SLICER, the X0X kits, PARAM_HELP, patterns...
job; ( jobin
    mkdir -p "$OUT/optimist-mots"
    MOTS_FLAGS=$(python3 tools/config_flags.py tests/profiles/mots-like.config)
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $MOTS_FLAGS -DFELUCCA_PAGE_LINES=1 -o "$OUT/ui_optimist_mots_test" tests/ui_optimist_test.c -lm
    run "Optimist UI with the mots-like build's flags (tests/profiles/mots-like.config): the whole test, as the user flashes it" "$OUT/ui_optimist_mots_test" "$OUT/optimist-mots"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/vis_tap_test" tests/vis_tap_test.c -lm
    run "the SCOPE's tap: a block's mix copied whole, block aligned over the wrap" "$OUT/vis_tap_test"
) >"$U.out" 2>&1 & jobbg
# the knobs' help lines (FELUCCA_PARAM_HELP, tools/param_help.json; the Optimist UI's coverage: tests/ui_optimist_test.c):
# the table valid, the editor's copy of it up to date, every line unpacks
job; ( jobin
    run "help lines: tools/param_help.json valid, web/editor.html's copy up to date" python3 tools/gen_param_help.py --check
    run "FXP table: tests/fxp_table.json, web/editor.html's mock copy up to date" python3 tools/gen_fxp_table.py --check
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    mkdir -p "$OUT/phpack"
    python3 tools/gen_param_help.py --all "$OUT/phpack/felucca_param_help.h"
    $CC -I"$OUT/phpack" -o "$OUT/param_help_pack_test" tests/param_help_pack_test.c
    run "help lines: every line of tools/param_help.json unpacks (ph_blob.c ph_text) to the table's text, fits PH_LINE, is found" "$OUT/param_help_pack_test"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/soak_test" tests/soak_test.c -lm
    run "soak: ${SOAK_MIN:-10} minutes of random live use (bounded, no hanging voices, idle after stop)" "$OUT/soak_test" "${SOAK_MIN:-10}"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -o "$OUT/upreset_test" tests/upreset_test.c
    run "user presets (UPB4: UP_PUT parser, bank round trip, a byte a value, versions)" "$OUT/upreset_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_FLASH=1 -o "$OUT/ed_user_test" tests/ed_user_test.c -lm
    run "editor cmds 11..21 (samples, user presets): refused while playing (rc 3 / 5, stop first), the flash untouched" "$OUT/ed_user_test"
) >"$U.out" 2>&1 & jobbg

# USB audio (from Melodee; FELUCCA_USB_AUDIO): stream logic, endpoint driver, descriptors, stems
job; ( jobin
    $CC -o "$OUT/usb_audio_test" tests/usb_audio_test.c -lm
    run "USB audio: routing, clock drift and stream recovery" "$OUT/usb_audio_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -o "$OUT/usb_audio_clock_test" tests/usb_audio_clock_test.c -lm
    run "USB audio capture against a drifting I2S clock (the resampler)" "$OUT/usb_audio_clock_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -o "$OUT/usb_audio_driver_test" tests/usb_audio_driver_test.c
    run "USB audio: endpoint lifecycle and packet ownership" "$OUT/usb_audio_driver_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -DFELUCCA_CDC=0 -o "$OUT/usb_serial_dump" tests/usb_serial_test.c && "$OUT/usb_serial_dump" "$OUT/usb_plain.bin" >/dev/null
    $CC -DFELUCCA_CDC=1 -o "$OUT/usb_serial_test" tests/usb_serial_test.c
    run "USB SERIAL (SLOOP 2.4): the console only when on, from the next start; off = a no-console build, byte for byte" "$OUT/usb_serial_test" "$OUT/usb_plain.bin"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "USB descriptors: MIDI, CDC and audio configurations" python3 tests/usb_audio_desc_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "parameter icons: tools/draw_icons.py -> assets/icons.png -> gen_icons.py (86 x 36 B, all distinct)" python3 tests/icons_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/usb_audio_tracks_test" tests/usb_audio_tracks_test.c -lm
    run "USB audio: four isolated track stems through the real mixer" "$OUT/usb_audio_tracks_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/usb_audio_route_test" tests/usb_audio_route_test.c -lm &&
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_DRUM_X909=1 -DFELUCCA_DRUM_X808=1 -o "$OUT/usb_audio_route_x0x" tests/usb_audio_route_test.c -lm
    run "USB AUDIO (SYSTEM): each capture channel's source (MAIN, sums, stems, drum bus, lane taps, OFF), the mix untouched" "$OUT/usb_audio_route_test"
    run "USB AUDIO (SYSTEM) with the X0X kits: a tapped lane of the shared sum, the mix untouched" "$OUT/usb_audio_route_x0x"
) >"$U.out" 2>&1 & jobbg
# the audio path's skips (FELUCCA_SKIP, core.h): built with them and without, the same samples after every
# neutral -> working -> neutral flip; also with the X0X kits, USB audio and the glides built (its files: the unit's
# number $nu in their names, as the four run at once)
skip_same() {
    $CC -O2 -w -I"$HGEN" -Ifirmware/src "$@" -DFELUCCA_SKIP=0 -o "$OUT/skip_test0_$nu" tests/skip_test.c -lm &&
    $CC -O2 -w -I"$HGEN" -Ifirmware/src "$@" -DFELUCCA_SKIP=1 -o "$OUT/skip_test1_$nu" tests/skip_test.c -lm &&
    "$OUT/skip_test0_$nu" > "$OUT/skip_out0_$nu.txt" && "$OUT/skip_test1_$nu" > "$OUT/skip_out1_$nu.txt" &&
    { cat "$OUT/skip_out1_$nu.txt"; diff "$OUT/skip_out0_$nu.txt" "$OUT/skip_out1_$nu.txt"; }
}
job; ( jobin
    run "skips (FELUCCA_SKIP): bit-identical to computing everything, settings flipped mid-sound" skip_same
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "skips with the X0X kits" skip_same $X0X_ON
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "skips with USB audio (the stems)" skip_same -DFELUCCA_USB_AUDIO=1
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "skips with the glides (GLIDE)" skip_same -DFELUCCA_GLIDE=1
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
    run "TRS / USB MIDI parser (SLOOP 2.3: USB flow control, a flood loses nothing, malformed ignored; an FD at the TRS reader)" "$OUT/midi_uart_test"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -o "$OUT/ota_test" tests/ota_test.c
    run "M-UPGRADE entry" "$OUT/ota_test" build/felucca.fwsc
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
    python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
    $CC -o "$OUT/ldr_test" tests/ldr_test.c
    run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
    $CC -DFELUCCA_LAYOUT=1 -o "$OUT/ldr_test_l1" tests/ldr_test.c
    run "update loader of the fake SD mode (LAYOUT 1): what a switch of layout clears, nothing else" "$OUT/ldr_test_l1" --layout
) >"$U.out" 2>&1 & jobbg
job; ( jobin   # the silent placeholder (docs/SD-MODE-DESIGN.md 6b): a part dropped to make memory room
    $CC -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_ENG_FM6=1 -o "$OUT/placeholder_test" tests/placeholder_test.c -lm
    run "a dropped part: silent, its memory idle, NONE, through a save and a load until an engine is picked" "$OUT/placeholder_test"
    $CC -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_ENG_FM6=1 "-DFELUCCA_ARENA_BYTES=ARN_SLOT" -o "$OUT/placeholder_flow_test" tests/placeholder_test.c -lm
    run "memory short (an arena of one part's): refused, the part to drop proposed, dropped, the change made; on a load too, its sound kept" "$OUT/placeholder_flow_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin   # the arena (core/arena.c, docs/SD-MODE-DESIGN.md 6b): first fit and the fixed slots
    $CC -O2 -w -Ifirmware/src -o "$OUT/arena_test" tests/arena_test.c
    run "arena placed by need: reserve, take, reuse, idle reclaimed, refused, what to drop; random switches, some pending" "$OUT/arena_test"
    $CC -O2 -w -Ifirmware/src -DARN_SLOT=1008u -DARN_FIXED=1 -o "$OUT/arena_fixed_test" tests/arena_test.c
    run "arena at fixed slots (the default): the fixed per-part memories' places, never refused" "$OUT/arena_fixed_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin   # the RAMTEXT overlays (core/overlay.c, docs/SD-MODE-DESIGN.md 6): the slots' protocol on the host
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_OVERLAY=1 -DFELUCCA_ENG_STRINGS=1 -o "$OUT/overlay_test" tests/overlay_test.c -lm
    run "overlays: a part plays from its slot only once its engine is there (asked, loaded by the main loop), the same samples as in place, a change while playing, 30000 random changes" "$OUT/overlay_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_OVERLAY=1 -o "$OUT/regress_ovl" tests/regress.c -lm
    run "regression with the overlays (FELUCCA_OVERLAY, the host's slots): the same golden renders (no CPU budget: tests/regress.c's own)" "$OUT/regress_ovl" tests/golden.txt "$OUT/regress_ovl_no_cpu.txt"
    run "ovl_link: the plan (the least RAMTEXT), the linker scripts, the table" python3 tests/ovl_link_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin   # the flash layouts (docs/SD-MODE-DESIGN.md): the numbers agree; the fake SD mode's 7-sector log
    run "flash layouts: fm1_flash_map.h, flash_layout.py, the builder and the package agree; the switch table" python3 tests/flash_layout_test.py
    L1="-DFELUCCA_LAYOUT=1 -DFELUCCA_ENG_SAMPLE=0 -DFELUCCA_DRUM_USR=0 -DFELUCCA_SNAPSHOTS=0 -DFELUCCA_PATTERNS=1 -DSEC_LOG_SECTORS=7u"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=1 $L1 -o "$OUT/patterns_test_l1" tests/patterns_test.c -lm
    run "patterns and scenes in the fake SD mode's log (7 sectors at 0xD0000): store, MEM FULL" "$OUT/patterns_test_l1"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=1 $L1 -o "$OUT/project_scope_test_l1" tests/project_scope_test.c -lm
    run "projects in the fake SD mode's log: apart, MEM FULL across projects" "$OUT/project_scope_test_l1"
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
    run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/midi_expression_test" tests/midi_expression_test.c -lm
    run "MIDI expression: bend, RPN 0, mod wheel, sustain, CC120 / 121 / 123 (USB and TRS)" "$OUT/midi_expression_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/midi_scale_test" tests/midi_scale_test.c -lm
    run "MIDI in through the key layouts: WHITE, SNAP, chords, releases after a key change" "$OUT/midi_scale_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/arp_follow_test" tests/arp_follow_test.c -lm
    run "arp follows TRN, ROOT, SCALE, QNT on held and latched notes (SLOOP #191)" "$OUT/arp_follow_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CHORDPLUS=1 -o "$OUT/arp_follow_cp_test" tests/arp_follow_test.c -lm
    run "arp follows TRN on latched notes, CHORD+ built (SLOOP #191)" "$OUT/arp_follow_cp_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/arp_seq_test" tests/arp_seq_test.c -lm
    run "the arp plays the sequencer's notes as the keys' (in real time, its own grid; the steps' arp not recorded back)" "$OUT/arp_seq_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/midi_ch_test" tests/midi_ch_test.c -lm
    run "MIDI channels per track (SLOOP 2.4 phase 3): defaults, in, keys, OFF, the project round trip" "$OUT/midi_ch_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -DFELUCCA_CDC=1 -o "$OUT/midi_seq_test" tests/midi_seq_test.c -lm
    run "SEQ -> MIDI OUT (SLOOP 2.4): every note ended, STOP, arp, rolls, channel moves, no echo; MIDI IN plays every note" "$OUT/midi_seq_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -DFELUCCA_CDC=1 -DFELUCCA_BLE=1 -o "$OUT/midi_seq_ble_test" tests/midi_seq_test.c -lm
    run "settings word with BLE built in: BLUETOOTH is bit 24, 1 = ON (fresh, older and SLOOP words read OFF), beside the other bits" "$OUT/midi_seq_ble_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -DFELUCCA_CDC=1 -o "$OUT/smute_test" tests/smute_test.c -lm
    run "sequencer mutes (SHIFT held + a key on the mixer): the key map, a muted track / lane plays nothing (sound, MIDI), the tails end, the FX record TLV" "$OUT/smute_test"
) >"$U.out" 2>&1 & jobbg

# BLE MIDI, route C (our own stack, firmware/src/ble/, docs/BLE-STACK.md): built with ASan / UBSan where the compiler has them
BLE_SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
echo 'int main(void) { return 0; }' > "$OUT/ble_san_probe.c"
if ! $CC $BLE_SAN -o "$OUT/ble_san_probe" "$OUT/ble_san_probe.c" 2>/dev/null; then
    BLE_SAN=""
    job; ( jobin
        echo "(this compiler has no ASan / UBSan: the BLE tests build without them)"
    ) >"$U.out" 2>&1 & jobbg
fi
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/ble_prim_test" tests/ble_prim_test.c
    run "BLE primitives: AES-128 (FIPS-197, the Core spec's session key), AES-CCM (its encrypted packets), passkey TK, CRC24, whitening, CSA #1, AA rules" "$OUT/ble_prim_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/ble_stack_test" tests/ble_stack_test.c
    run "BLE stack against a simulated central: advertise, connect, LL procedures, GATT discovery, MIDI both ways, instants, every ending" "$OUT/ble_stack_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -DBLE_LL_ENC=1 -o "$OUT/ble_stack_enc_test" tests/ble_stack_test.c
    run "BLE stack with LL encryption (BLE_LL_ENC=1): the Core spec's encryption sample end to end, a MIC failure" "$OUT/ble_stack_enc_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1 -o "$OUT/ble_stack_smp_test" tests/ble_stack_test.c
    run "BLE stack with SMP legacy Just Works (BLE_SMP_LEGACY=1): a Mac-like pairing, STK, our LTK / EDIV / Rand, the bond on reconnection, a wrong confirm" "$OUT/ble_stack_smp_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -o "$OUT/ble_driver_test" tests/ble_driver_test.c
    run "BLE WL82 driver against an engine with the TX contract of BLE-HW-FACTS §8.2 (bit0 = 1 empty), RX by RXTOG, loss, a slot clock that steps back, TIMER4 wrap, 40 s timeout, the old polarity stalls; a Mac's discovery and CoreMIDI's to MIDI both ways" "$OUT/ble_driver_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1 -DBLE_MIDI_NEED_ENC=1 -o "$OUT/ble_driver_pair_test" tests/ble_driver_test.c
    run "BLE WL82 driver, MIDI behind encryption (BLE_MIDI_NEED_ENC=1): the Mac pairs on Insufficient Authentication, the link encrypted through the driver, MIDI both ways" "$OUT/ble_driver_pair_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -DBLE_DIAG=0 -o "$OUT/ble_driver_nodiag_test" tests/ble_driver_test.c
    run "BLE WL82 driver with BLE_DIAG=0 (no counters, rings or blell compiled): the same connection, discovery, MIDI both ways and timeouts" "$OUT/ble_driver_nodiag_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -DBLE_DIAG=0 -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1 -DBLE_MIDI_NEED_ENC=1 -o "$OUT/ble_driver_pair_nodiag_test" tests/ble_driver_test.c
    run "BLE WL82 driver with BLE_DIAG=0, MIDI behind encryption: the Mac pairs, the link encrypted, MIDI both ways" "$OUT/ble_driver_pair_nodiag_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -DBLE_CENTRAL=1 -DBLE_LL_ENC=1 -DBLE_SMP_LEGACY=1 -o "$OUT/ble_stack_central_test" tests/ble_stack_test.c
    run "BLE stack with the scanner (BLE_CENTRAL=1, with bonding): DEVICES open scans instead of advertising, never while connected, the reports' ring, OFF / ON, a link ending into the scan" "$OUT/ble_stack_central_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -DBLE_CENTRAL=1 -o "$OUT/ble_driver_central_test" tests/ble_driver_test.c
    run "BLE WL82 driver with the scanner built in (BLE_CENTRAL=1): the peripheral path as before (the Mac's whole session)" "$OUT/ble_driver_central_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -o "$OUT/ble_scan_driver_test" tests/ble_scan_driver_test.c
    run "BLE WL82 scanning (BLE-HW-FACTS §21.2): state 1 in the vendor's order, the channel per event, RX by RXBUFnCNTL or RXTOG, the active-scan backoff, no column reads, back to advertising" "$OUT/ble_scan_driver_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -DBLE_DIAG=0 -o "$OUT/ble_scan_driver_nodiag_test" tests/ble_scan_driver_test.c
    run "BLE WL82 scanning with BLE_DIAG=0: the same" "$OUT/ble_scan_driver_nodiag_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/ble_scan_test" tests/ble_scan_test.c
    run "BLE scan table: the AD parser (flags, 128-bit UUIDs, names), BLE-MIDI devices only, names from the scan response, order, ageing, a full table, relative bars" "$OUT/ble_scan_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/ble_central_test" tests/ble_central_test.c
    run "BLE central role against a simulated BLE-MIDI peripheral: our CONNECT_IND, the master's LL procedures and updates, the GATT client (discovery, CCCD, MIDI both ways), SMP as initiator (Insufficient Authentication, Security Request, a bond reused / lost, failures; an iPhone-like peer: Just Works refused -> the passkey pairing on the same link (the encryption pause), refused or unanswered -> NEED_MITM, the authenticated bond, a wrong passkey, no keyboard, a Secure Connections-only peer, the 30 s timeout), ah() on the Core sample, endings" "$OUT/ble_central_test"
) >"$U.out" 2>&1 & jobbg
# BLE_CENTRAL's other level (ble/ble_cfg.h): 3 no pairing (alone and beside BLE_BOND's responder)
job; ( jobin
    $CC -Wextra $BLE_SAN -DBLE_CENTRAL=3 -o "$OUT/ble_central_min_test" tests/ble_central_test.c
    run "BLE central role, BLE_CENTRAL=3 (no pairing, no encryption): a peer that asks for nothing ready, MIDI both ways; Insufficient Authentication and a Security Request (Pairing Not Supported) leave the link with NEED_PAIR, no Pairing Request, no LL_ENC_REQ" "$OUT/ble_central_min_test"
    $CC -Wextra $BLE_SAN -DBLE_CENTRAL=3 -DFELUCCA_BLE_BOND=1 -o "$OUT/ble_central_min_bond_test" tests/ble_central_test.c
    run "BLE central role, BLE_CENTRAL=3 with BLE_BOND=1 (the peripheral's responder kept): the same" "$OUT/ble_central_min_bond_test"
    $CC -Wextra $BLE_SAN -DBLE_CENTRAL=3 -o "$OUT/ble_stack_central_min_test" tests/ble_stack_test.c
    run "BLE stack with the scanner at BLE_CENTRAL=3 (no LL encryption): the peripheral path and the scan as before" "$OUT/ble_stack_central_min_test"
) >"$U.out" 2>&1 & jobbg
# FM-1 to FM-1: two (three) whole stacks, each its own object with BLE_API static as in the firmware's unity build
job; ( jobin
    $CC -Wextra $BLE_SAN -c -DF2F_U=a -o "$OUT/ble_f2f_a.o" tests/ble_f2f_unit.c
    $CC -Wextra $BLE_SAN -c -DF2F_U=b -o "$OUT/ble_f2f_b.o" tests/ble_f2f_unit.c
    $CC -Wextra $BLE_SAN -c -DF2F_U=c -DBLE_MIDI_NEED_ENC=1 -o "$OUT/ble_f2f_c.o" tests/ble_f2f_unit.c
    $CC -Wextra $BLE_SAN -o "$OUT/ble_f2f_test" tests/ble_f2f_test.c "$OUT/ble_f2f_a.o" "$OUT/ble_f2f_b.o" "$OUT/ble_f2f_c.o"
    run "BLE FM-1 to FM-1: two of our stacks, one picks the other: discovery, CCCD, MIDI both ways with no SMP, no encryption, no passkey; against one with BLE_MIDI_NEED_ENC=1 a silent Just Works pairing (no MITM, no passkey)" "$OUT/ble_f2f_test"
    $CC -Wextra $BLE_SAN -c -DF2F_U=a -DBLE_CENTRAL=3 -o "$OUT/ble_f2f_a3.o" tests/ble_f2f_unit.c
    $CC -Wextra $BLE_SAN -DF2F_A_LEVEL=3 -o "$OUT/ble_f2f_l3_test" tests/ble_f2f_test.c "$OUT/ble_f2f_a3.o" "$OUT/ble_f2f_b.o" "$OUT/ble_f2f_c.o"
    run "BLE FM-1 to FM-1, the picking FM-1 at BLE_CENTRAL=3 (no pairing): an FM-1 as shipped ready, MIDI both ways; one with BLE_MIDI_NEED_ENC=1 left with NEEDS PAIRING, nothing of SMP on the air" "$OUT/ble_f2f_l3_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -o "$OUT/ble_central_driver_test" tests/ble_central_driver_test.c
    run "BLE WL82 initiating and master (BLE-HW-FACTS §21.3 / §21.4): state 3 in the vendor's order, the target's ADV_IND, the switch to state 6 in the event IRQ (anchor counter 2 x WinOffset + 4, shortened when the IRQ came late), the stops waiting for the engine, the master's events by the TX rule, its update at instant - 1, no column reads" "$OUT/ble_central_driver_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -Itests/ble_fake -DBLE_DIAG=0 -o "$OUT/ble_central_driver_nodiag_test" tests/ble_central_driver_test.c
    run "BLE WL82 initiating and master with BLE_DIAG=0: the same" "$OUT/ble_central_driver_nodiag_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/ble_store_test" tests/ble_store_test.c
    run "BLE device store: one LAST (a device we connected to), NONE / LAST, bond and identity, FORGET, older / damaged records, the settings object round trip off the SDK's sectors" "$OUT/ble_store_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/ble_midi_test" tests/ble_midi_test.c
    run "BLE-MIDI packets: timestamps and their wrap, running status, real time, SysEx over packets, a 20000-event round trip" "$OUT/ble_midi_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/ble_midi_in_test" tests/ble_midi_in_test.c -lm
    run "BLE-MIDI in, the whole path: CoreMIDI-shaped Write Commands -> ATT -> decoder -> ring -> TIMER5 -> midi_in_q -> the synth's voices; blell's counters; 5050 writes" "$OUT/ble_midi_in_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/ble_vm_test" tests/ble_vm_test.c
    run "BLE RF trims: stock V15's VM read in place (§14: check byte, id / length, last wins, live area, 187's CRC), the copy, VM -> copy -> none" "$OUT/ble_vm_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/bt_coex_test" tests/bt_coex_test.c
    run "BLE MIDI and Bluetooth audio on one radio (ble/rf_share.h): either alone as before; BLE then BR/EDR or BR/EDR then BLE: one radio start-up, no LE init over BLE's window" "$OUT/bt_coex_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/bt_inq_test" tests/bt_inq_test.c
    run "BT Classic inquiry: the sync word's parity (GIAC, stock V15's LAP, the spec's construction), FHS and EIR parsing" "$OUT/bt_inq_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/bt_crypto_test" tests/bt_crypto_test.c
    run "BT Classic / SSP cryptography: SAFER+ (Ar, A'r), E1/E21/E22/E3, P-192 ECDH, SSP f1/f2/f3/g (Core Vol 2 Part G)" "$OUT/bt_crypto_test"
    $CC -Wextra $BLE_SAN -o "$OUT/bt_ssp_test" tests/bt_ssp_test.c
    run "BT Classic pairing (bt_ssp.c): SSP Just Works as the responder against a simulated initiator, and as the initiator against our responder: the link key, SRES, Kc" "$OUT/bt_ssp_test"
    $CC -Wextra -o "$OUT/vol_gesture_test" tests/vol_gesture_test.c
    run "SHIFT + the volume knob (op_volgest.h): 15 % switches the output once a press, the volume held, then taken back with no jump" "$OUT/vol_gesture_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/bt_lmp_test" tests/bt_lmp_test.c
    run "BT Classic probe LMP: the master's link set-up PDUs and the answers to a slave's requests (Core spec Vol 2 Part C)" "$OUT/bt_lmp_test"
    $CC -Wextra $BLE_SAN -o "$OUT/bt_afh_test" tests/bt_afh_test.c
    run "BT Classic audio AFH (bt_afh.c): the channels a headset fails on leave the map (half the mean, 20 stay), the engine's table and map words" "$OUT/bt_afh_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/bt_l2cap_test" tests/bt_l2cap_test.c
    run "BT Classic probe L2CAP / SDP: signalling frames, the search-attribute request, an A2DP sink record read" "$OUT/bt_l2cap_test"
    $CC -Wextra $BLE_SAN -o "$OUT/bt_sdps_test" tests/bt_sdps_test.c
    run "BT audio SDP server: our A2DP source record, the three requests answered, read back by the client's parser" "$OUT/bt_sdps_test"
    $CC -Wextra $BLE_SAN -o "$OUT/bt_avrcp_test" tests/bt_avrcp_test.c
    run "BT audio AVRCP: SetAbsoluteVolume byte for byte, the sink's questions answered, a key accepted" "$OUT/bt_avrcp_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -Wextra $BLE_SAN -o "$OUT/bt_avdtp_test" tests/bt_avdtp_test.c
    run "BT Classic probe AVDTP: commands, a sink endpoint picked, SBC capabilities read and chosen, SET_CONFIGURATION" "$OUT/bt_avdtp_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w $BLE_SAN -Itests/ref/sbc_dec -o "$OUT/bt_sbc_test" tests/bt_sbc_test.c tests/ref/sbc_dec/*.c -lm
    run "BT Classic / A2DP SBC encoder (AOSP's, bt/sbc/) against AOSP's decoder: every frame of two tones decoded, the tones back, as the FM-1 sets the stream" "$OUT/bt_sbc_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "BLE RF capture tool: the VM format, the trace cut into a program (windows, LUT, trims, scan, replay), V15's hash" early_out t_ble_rf
) >"$U.out" 2>&1 & jobbg
# the stack with its WL82 baseband driver in the emulator's BLE engine model, a virtual central end to end (skipped
# without the emulator's diagnose: FM1_BLE_DIAGNOSE or FM1_EMU, fm1-emulator feat/ble-engine)
job; ( jobin
    run "BLE in the emulator: advertise, connect, discover, MIDI both ways, updates, loss, timeout (tests/ble_emu_test.py)" early_out t_ble_emu
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "BLE central in the emulator: DEVICES pick -> initiate, master, pair, discover, MIDI both ways, LAST; reboot -> LAST reconnects (LTK, an RPA resolved by its IRK); NONE leaves; an iPhone-like peripheral: Just Works refused -> passkey typed on the same link (encryption pause) -> authenticated, one connection, LAST with its level, its LTK after a reboot; a wrong passkey: one failure, no loop; the FM-1's late event interrupt, busy engine and a lost CONNECT_IND (tests/ble_emu_central_test.py)" early_out t_ble_central
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "BLE start-ups in the emulator, the full option set (BLE USB_CONSOLE BLE_BOND BLE_CENTRAL BLE_DIAG): BLUETOOTH saved ON -> the UI runs, the watchdog fed, advertising, a central connects; LAST (an iPhone-like RPA, authenticated) there -> reconnected, away -> the search; an engine never idle after a stop -> every stop gives up at its bound, the breadcrumb names it (tests/ble_emu_boot_test.py)" early_out t_ble_boot
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
    run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
    mkdir -p build/tracks_demo
    run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_PLOCK=1 -o "$OUT/hostsim_plock" tests/hostsim.c -lm
    run "recorded lengths as GATE locks (PLOCK): 0.9 / 1.4 / 1.8 steps, the GATE, pattern end, overdub, ratchet, legato, free take, undo" env RECLEN=1 "$OUT/hostsim_plock" 0 0 1 "$OUT/reclen.wav"
    mkdir -p "$OUT/tracks_pl"; run "TRACKS with PLOCK (the recording lengths as before)" env TRACKS="$OUT/tracks_pl" "$OUT/hostsim_plock" 0 0 1 "$OUT/tracks_pl.wav"
    run "live recording under random keys (REC armed, chords, REC QUANTIZE, lengths, tempo, undo): no hang, no loop guard hit" env RECSTRESS=7 BLOCKS=100000 "$OUT/hostsim_plock" 0 0 1 "$OUT/recstress.wav"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SL24_ON -DFELUCCA_CHANCE=1 -o "$OUT/hostsim_sl24" tests/hostsim.c -lm
    run "recorded lengths as GATE locks with MICRO, FILLS, MOTION, CHANCE" env RECLEN=1 "$OUT/hostsim_sl24" 0 0 1 "$OUT/reclen_sl24.wav"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_PLOCK=1 -DFELUCCA_MICRO=1 -o "$OUT/recq_test" tests/recq_test.c -lm
    run "REC QUANTIZE: OFF keeps a late start (nudge) and the length (lock), ON as before, the free take, saved in the project's G_INFO slot" "$OUT/recq_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -w -I"$HGEN" -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
    run "project formats (FUN6 x2 / FUN5 / FUN4 / FUN3 / FUN2 / FUN1 -> FUN7), FM6 voices, capture / apply, autosave" "$OUT/project_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -w -I"$HGEN" -Ifirmware/src -o "$OUT/brt_full" tests/builder_rt_test.c -lm
    $CC -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_FM6=0 -DFELUCCA_ENG_GRAIN=0 -o "$OUT/brt_red" tests/builder_rt_test.c -lm
    run "builder: project full -> reduced (FM6, GRAIN out) -> full keeps the missing engines' parts" sh -c \
        "'$OUT/brt_full' write '$OUT/rtA.bin' && '$OUT/brt_red' reduce '$OUT/rtA.bin' '$OUT/rtB.bin' '$OUT/rtC.bin' && '$OUT/brt_full' check '$OUT/rtA.bin' '$OUT/rtB.bin' '$OUT/rtC.bin'"
) >"$U.out" 2>&1 & jobbg
# missing on this build (firmware/src/storage/miss.c): a project, a song section and a user kit of a full build on a reduced
# one (its sample header made without PIANO); the screens in $OUT/miss
job; ( jobin
    mkdir -p "$OUT/gen_red" "$OUT/miss"
    FELUCCA_SAMPLES_SKIP=PIANO,SCRCH python3 tools/gen_samples.py "$OUT/gen_red/felucca_samples.h" >/dev/null
    MISS_RED="-DFELUCCA_ENG_FM6=0 -DFELUCCA_DRUM_SYNTH=0 -DFELUCCA_DS_KAWA=0 -DFELUCCA_DS_PLAITS=0 -DFELUCCA_FX_DELAY=0 -DFELUCCA_FX_DUST=0 -DFELUCCA_FX_SLICER=0 -DFELUCCA_DRUM_EDIT=0"
    $CC -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_CHANCE=1 -DFELUCCA_ENG_PHYS=1 -o "$OUT/miss_full" tests/missing_test.c -lm
    $CC -w -I"$OUT/gen_red" -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 $MISS_RED -o "$OUT/miss_red" tests/missing_test.c -lm
    run "missing on this build: full -> reduced project, song section, user kit; the message once per item, TOOLS > MISS" sh -c \
        "'$OUT/miss_full' write '$OUT/miss/A.bin' '$OUT/miss/B.bin' '$OUT/miss/K.bin' && '$OUT/miss_red' reduce '$OUT/miss/A.bin' '$OUT/miss/B.bin' '$OUT/miss/K.bin' '$OUT/miss'"
) >"$U.out" 2>&1 & jobbg
# every engine built has a preset on the PRESETS list, every drum source built a kit (tests/preset_cover_test.c; the
# builder's random configurations: tests/builder_test.py): sample headers without any set, without GRAIN's sets
mkdir -p "$OUT/gen_noset" "$OUT/gen_nogr"
FELUCCA_SAMPLES_SKIP=PIANO,BASS,VIBES,HORNS,STRGS,FLUTE,SCRCH,PERC python3 tools/gen_samples.py "$OUT/gen_noset/felucca_samples.h" >/dev/null
FELUCCA_SAMPLES_SKIP=PIANO,VIBES,FLUTE python3 tools/gen_samples.py "$OUT/gen_nogr/felucca_samples.h" >/dev/null
KITS1="-DFELUCCA_DRUM_SYNTH=0 -DFELUCCA_DS_KAWA=0 -DFELUCCA_DS_PLAITS=0 -DFELUCCA_KIT_ACOUSTIC=0 -DFELUCCA_KIT_DEEP=0 -DFELUCCA_KIT_TIGHT=0 -DFELUCCA_KIT_BRIGHT=0"
# (every sample set: COVER_STRICT, a factory preset per engine; sets left out: INIT where none is left)
S1="-DCOVER_STRICT=1"
for c in "$HGEN|$S1" "$HGEN|$S1 -DFELUCCA_ENG_SLICE=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1 $FM6_PACKS_ON" \
         "$HGEN|$S1 $X0X_ON -DFELUCCA_ENG_ACID=1" "$HGEN|$S1 $KITS1" "$HGEN|$S1 $X0X_ON -DFELUCCA_DRUM_SYNTH=0 -DFELUCCA_DS_KAWA=0 -DFELUCCA_DS_PLAITS=0 -DFELUCCA_DRUM_SAMPLED=0" "$OUT/gen_noset|-DFELUCCA_DRUM_SAMPLED=0" \
         "$OUT/gen_nogr|-DFELUCCA_ENG_SLICE=1"; do
    job; ( jobin
        $CC -w -I"${c%%|*}" -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 ${c#*|} -o "$OUT/preset_cover_test$nu" tests/preset_cover_test.c -lm
        run "every engine built has a loadable entry on the PRESETS list (a factory preset, else INIT), every drum source a kit (${c#*|})" sh -c "'$OUT/preset_cover_test$nu' > '$OUT/preset_cover$nu.txt' || { cat '$OUT/preset_cover$nu.txt'; exit 1; }"
    ) >"$U.out" 2>&1 & jobbg
done
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sec_codec_test" tests/sec_codec_test.c -lm
    run "song sections: the record codec (round trips, raw fallback, damaged records, sizes; codec B: load -> store -> load the same bytes, codec A records migrate, fuzz)" "$OUT/sec_codec_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sections_test" tests/sections_test.c -lm
    run "song sections A..P: old slots migrate (cut anywhere), save / load, pending while playing, stage, MEM FULL" "$OUT/sections_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_LIVE_SAVE=1 -o "$OUT/sections_live_test" tests/sections_test.c -lm
    run "song sections with LIVE_SAVE: saved while playing, the waiting ones one a call, back to waiting after a held erase" "$OUT/sections_live_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin   # the SD card (SDCARD): ChaN's driver + Petit FatFs on a card model, real FAT16 / FAT32 images
    gunzip -c tests/fixtures/sd/fat16.img.gz > "$OUT/sd_fat16.img"
    gunzip -c tests/fixtures/sd/fat32.img.gz > "$OUT/sd_fat32.img"
    $CC -O2 -w -Ifirmware/src -o "$OUT/sd_test" tests/sd_test.c firmware/src/storage/sd/pff.c
    run "SD card: SDHC / SDSC / SDv1 / MMC start-up, FAT16 + FAT32 (MBR or none), read, seek, list, write, no card, timeouts, LCD sees only 0xFF; CMD18 + CMD12 and CMD17 blocks by bytes and by a DMA model (polled, no CPU byte during a DMA), CRC16, the card's end, block writes, a file's runs" "$OUT/sd_test" "$OUT/sd_fat16.img" "$OUT/sd_fat32.img"
    rm -f "$OUT/sd_fat16.img" "$OUT/sd_fat32.img"
) >"$U.out" 2>&1 & jobbg
# the screen mirrored (ED_MIRROR, io/editor/ed_mirror.c): the stream through a stand-in USB ring decoded back to the panel after
# every write, a stalled host, the keep-alive; then the Optimist UI's whole run through the encoder, the decoded screen its own
job; ( jobin
    $CC -O2 -w -o "$OUT/mirror_test" tests/mirror_test.c
    run "the screen mirrored, USB and Bluetooth: 3000 random writes decoded to the same panel, long runs, a stalled host falls behind and catches up, the Bluetooth request, a slow radio, the ring overflowing, the keep-alive, a muted write not sent, the meters over USB only" "$OUT/mirror_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DMIRROR_CHECK -o "$OUT/ui_optimist_mirror_test" tests/ui_optimist_test.c -lm
    mkdir -p "$OUT/optimist_mirror"
    run "the screen mirrored: the Optimist UI's whole run (every test, 20000 random frames) encoded and decoded, the same pixels; every catch-up the panel" "$OUT/ui_optimist_mirror_test" "$OUT/optimist_mirror"
) >"$U.out" 2>&1 & jobbg
for x in 0 1; do   # per-track patterns and scenes (FELUCCA_PATTERNS): unit; then a PATTERNS -> no PATTERNS -> PATTERNS round trip
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$x -o "$OUT/patterns_test$x" tests/patterns_test.c -lm
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$x -DFELUCCA_PATTERNS=0 -o "$OUT/patterns_test${x}n" tests/patterns_test.c -lm
        run "patterns and scenes (XSTEP=$x): store, share, copy-on-write, NO FREE PATTERN, old sections converted (cut anywhere), arena, stage, a scene's FX record (an older build's misfiled one moved), MEM FULL" "$OUT/patterns_test$x"
        [ "$x" = 1 ] && $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$x -o "$OUT/project_scope_test" tests/project_scope_test.c -lm && \
            run "projects own their scenes, patterns and song: another project sees none of them, nothing done there touches the first, the arena across a switch, the chain and BARS, a restart, DELETE, MEM FULL across projects" "$OUT/project_scope_test"
        run "patterns (XSTEP=$x) across builds: a build without PATTERNS sees none of its projects (log format 2 elsewhere) and leaves them as they were" sh -c "$OUT/patterns_test$x $OUT/pat$x.img 1 && $OUT/patterns_test${x}n $OUT/pat$x.img 2 && $OUT/patterns_test$x $OUT/pat$x.img 3"
    ) >"$U.out" 2>&1 & jobbg
done
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_MOTION=0 -o "$OUT/patterns_test_m0" tests/patterns_test.c -lm
    run "patterns and scenes without motion recording (MOTION=0)" "$OUT/patterns_test_m0"
) >"$U.out" 2>&1 & jobbg
for f in "" "-DFELUCCA_MOTION=0" "-DFELUCCA_SL24_XSTEP=1 -DFELUCCA_MICRO=1"; do
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/patterns_seq_test$nu" tests/patterns_seq_test.c -lm
        run "pattern launches while playing (${f:-defaults}): end, bar, now, swing, motion, a take, undo, stopped, a live jump, song mode" "$OUT/patterns_seq_test$nu"
    ) >"$U.out" 2>&1 & jobbg
done
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/patterns_ed_test" tests/patterns_ed_test.c -lm
    run "editor cmds 83 / 84 (pattern read / write): chunks of 256 B, checked as the stage decodes, the log or the arena, refused when out of order / of another kind / too long / proj_tmp lent, clear" "$OUT/patterns_ed_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sl24_safety_test" tests/sl24_safety_test.c -lm
    run "started on SLOOP 2.4's flash (FELUCCA_SL24_SAFE): its projects, autosave, FM6 bank, long samples never erased; shown as 2.4's" "$OUT/sl24_safety_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/favorites_test" tests/favorites_test.c -lm
    run "favorites (FELUCCA_FAVORITES): the store (64 factory marks by engine UID + name, 32 user slots), the settings record keeps them last: a record from before reads whole with none, older lengths, flash round trip" "$OUT/favorites_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_USB_AUDIO=1 -o "$OUT/favorites_ua_test" tests/favorites_test.c -lm
    run "favorites with USB audio: the record's USB AUDIO routing last (older records: the default; UAR1's -3 / -6 dB as L+R)" "$OUT/favorites_ua_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_BLE=1 -o "$OUT/favorites_ble_test" tests/favorites_test.c -lm
    run "favorites in a BLE build: a record from before the favorites keeps the BLE address, trims, bond and device store; older BLE lengths" "$OUT/favorites_ble_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -o "$OUT/stepx_test" tests/stepx_test.c
    run "SLOOP 2.4 step extras (stepx.h): 2.4's FUN5 track tail byte for byte, fills, locks, the stored form round trip" "$OUT/stepx_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/stepx_store_test" tests/stepx_store_test.c -lm
    run "SLOOP 2.4 step extras kept with the sections and the autosave (FELUCCA_SL24_XSTEP): save, load, live store, stage, keys" "$OUT/stepx_store_test"
) >"$U.out" 2>&1 & jobbg
for x in "0 0 0" "1 0 0" "1 1 1" "0 1 1" "1 1 0" "1 0 1"; do
    set -- $x
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$1 -DFELUCCA_TRK_FILT=$2 -DFELUCCA_CHORDPLUS=$3 -o "$OUT/sl24_import_test$1$2$3" tests/sl24_import_test.c -lm
        run "a SLOOP 2.4 project imported (golden FUN5 from 2.4's own types), XSTEP=$1 TRK_FILT=$2 CHORDPLUS=$3: values, FILT/STRUM/VLEAD, engines, FM6, kits, extras; LOAD twice" "$OUT/sl24_import_test$1$2$3"
    ) >"$U.out" 2>&1 & jobbg
done
for x in "0 0 0" "1 0 0" "1 1 1" "0 1 1" "1 1 0" "1 0 1"; do   # (as the import's: both switches, each alone)
    set -- $x
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$1 -DFELUCCA_TRK_FILT=$2 -DFELUCCA_CHORDPLUS=$3 -o "$OUT/sl24_export_test$1$2$3" tests/sl24_export_test.c -lm
        run "ours exported for SLOOP 2.4 (SL24_EXPORT), XSTEP=$1 TRK_FILT=$2 CHORDPLUS=$3: golden FUN5 in and out byte for byte but the losses (FILT's slot); ours -> 2.4 -> ours, each loss listed; FM6 voices by PTCH or 2.4's bank; 2.4's settings; FX slots" "$OUT/sl24_export_test$1$2$3"
    ) >"$U.out" 2>&1 & jobbg
done
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -DFELUCCA_TRK_FILT=1 -o "$OUT/sl24_export_ifx_test" tests/sl24_export_test.c -lm
    run "ours exported for SLOOP 2.4 with the further inserts built (INS_*, TRK_FILT): the same, and a further insert heard lost (INSFX)" "$OUT/sl24_export_ifx_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sl24_edimport_test" tests/sl24_edimport_test.c -lm
    run "a SLOOP 2.4 backup file's project through the editor (SL24_EDIMPORT, cmds 90 / 91): chunks and CRCs, the FM6 parts' bank patches, every refusal, 2.4's bank read out, nothing written" "$OUT/sl24_edimport_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sl25_safety_test" tests/sl25_safety_test.c -lm
    run "started on SLOOP 2.5 / 2.6's flash (SL24_SAFE): their settings (88 B + the SYN kits + 2.6's MIDI part) never read as ours, never written over; each firmware reads its own" "$OUT/sl25_safety_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
) >"$U.out" 2>&1 & jobbg
for x in "0 0 0" "1 1 1" "1 0 0"; do
    set -- $x
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$1 -DFELUCCA_TRK_FILT=$2 -DFELUCCA_CHORDPLUS=$3 -o "$OUT/sl26_import_test$1$2$3" tests/sl26_import_test.c -lm
        run "SLOOP 2.5 / 2.6 projects imported (golden FUN5 / FUN6 from their own types), XSTEP=$1 TRK_FILT=$2 CHORDPLUS=$3: 2.5's PHYS / NOISE, FUN6 -> FUN5, LOAD twice; PHYS exported as 2.5's" "$OUT/sl26_import_test$1$2$3"
    ) >"$U.out" 2>&1 & jobbg
done
for x in "0 0 0" "1 1 1"; do
    set -- $x
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$1 -DFELUCCA_TRK_FILT=$2 -DFELUCCA_CHORDPLUS=$3 -DFELUCCA_SL24_EXPORT=1 -o "$OUT/sl27_import_test$1$2$3" tests/sl27_import_test.c -lm
        run "a SLOOP 2.7 project (golden FUN6 from 2.7.1's own types), XSTEP=$1 TRK_FILT=$2 CHORDPLUS=$3: its 16 drum sounds on our lanes with their TUNE / DECAY, held notes as SF_CARRY (never a chance), the arp's modes, LOAD twice; exported and back" "$OUT/sl27_import_test$1$2$3"
    ) >"$U.out" 2>&1 & jobbg
done
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sec_log_test" tests/sec_log_test.c -lm
    run "song sections: the log (restarts, compaction, writes and erases cut, MEM FULL and its reserve; the patterns' ids 24..87 kept, 16 busy codec B sections)" "$OUT/sec_log_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DSLG_V2=1 -DSEC_LOG_SECTORS=8u -o "$OUT/sec_log_v2_test" tests/sec_log_test.c -lm
    run "the log, format 2 (PATTERNS: every record tagged with its project; 8 sectors here, 32 / 20 at 0xA0000 in a build): the same, then two projects apart, a device-wide id, cuts in either, dead marks, MEM FULL across projects" "$OUT/sec_log_v2_test"
) >"$U.out" 2>&1 & jobbg
for s in 6 8 12; do                                  # (SNAPSHOTS 2 / 4 / 8)
    job; ( jobin
        $CC -DSN_SECTORS=${s}u -o "$OUT/snap_store_test$s" tests/snap_store_test.c
        run "snapshots: the flash area of $s sectors (round trips, restarts, a save and a clear cut at every erase and program, FULL, DAMAGED, random)" "$OUT/snap_store_test$s"
    ) >"$U.out" 2>&1 & jobbg
done
for v in "16 0" "16 1" "8 0" "4 0" "4 1"; do
    set -- $v
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$1 -DFELUCCA_MOTION=$2 -o "$OUT/snapshots_test$1_$2" tests/snapshots_test.c -lm
        run "snapshots, FELUCCA_SECTIONS=$1 MOTION=$2: save, change, restart, load: every track, section, song, motion, kit exactly; cuts; BEFORE LOAD; editor export / import" "$OUT/snapshots_test$1_$2"
    ) >"$U.out" 2>&1 & jobbg; eval "SN_$1_$2=$nu"   # (the unit: for "after" below)
done
for v in "16 0" "16 1" "8 0" "4 0" "4 1"; do          # (with SLOOP 2.4's step extras: FELUCCA_SL24_XSTEP, in the stream and in the editor's backup)
    set -- $v
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$1 -DFELUCCA_MOTION=$2 -DFELUCCA_SL24_XSTEP=1 -o "$OUT/snapshots_test$1_${2}x" tests/snapshots_test.c -lm
        run "snapshots with the step extras (XSTEP=1), FELUCCA_SECTIONS=$1 MOTION=$2: the work's and each section's nudges, locks and fills saved, loaded, cut, exported; an older stream; the backup object XSTP" "$OUT/snapshots_test$1_${2}x"
    ) >"$U.out" 2>&1 & jobbg; eval "SNX_$1_$2=$nu"
done
for x in 0 1; do                                     # (PATTERNS: the tracks' pattern sources go with the work)
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=16 -DFELUCCA_MOTION=1 -DFELUCCA_PATTERNS=1 -DFELUCCA_SL24_XSTEP=$x -o "$OUT/snapshots_test_pat$x" tests/snapshots_test.c -lm
        run "snapshots with patterns (XSTEP=$x): the tracks' pattern sources saved with the work (before the first autosave, changed since the last), loaded back" "$OUT/snapshots_test_pat$x" pstate
        run "backup with projects (XSTEP=$x): PJ01.. each project's records as one stream and PNAM backed up, wiped, written back byte for byte; the objects of before restore-only; a cut restore" "$OUT/snapshots_test_pat$x" projects
    ) >"$U.out" 2>&1 & jobbg
done
job; ( jobin
    after $SNX_16_0 $SN_16_0 $SNX_4_0
    run "snapshots across builds: with the step extras (XSTEP) -> without (the records skipped), without -> with (none), 16 sections -> 4" sh -c \
        "'$OUT/snapshots_test16_0x' write '$OUT/snxx.nor' && '$OUT/snapshots_test16_0' read '$OUT/snxx.nor' && '$OUT/snapshots_test4_0x' read '$OUT/snxx.nor' && '$OUT/snapshots_test16_0' write '$OUT/snx0.nor' && '$OUT/snapshots_test16_0x' read '$OUT/snx0.nor'"
) >"$U.out" 2>&1 & jobbg
# the editor's backup with every switch that adds a BK_OBJS entry (ed_backup.c) on at once, the builder's item bits as long
# as the target's: BK_LIST pages, none cut (2026-10: FXSL made the "mots" build's list too long for its one reply)
CFGB=$(python3 -c 'import sys; sys.path.insert(0, "tools/builder"); import registry as R; print((max(i.bit for i in R.ITEMS.values()) + 7) // 7)')
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DBK_ALL_ON=$CFGB "-DFELUCCA_CFG_BITS={[$((CFGB - 1))] = 0}" -DFELUCCA_SECTIONS=16 -DFELUCCA_MOTION=0 \
        -DFELUCCA_SL24_XSTEP=1 -DFELUCCA_UP_FM6=1 -DFELUCCA_NATIVE_BANKS=1 -DFELUCCA_ENG_CZ=1 -o "$OUT/snapshots_test_bkall" tests/snapshots_test.c -lm
    run "backup, every object switch on (16 sections, song, XSTEP, FXSL, UP_FM6, CZ bank, snapshots): BK_LIST in pages, every object, none cut" "$OUT/snapshots_test_bkall"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_FM6=0 -o "$OUT/snapshots_test_nofm6" tests/snapshots_test.c -lm
    after $SN_16_0 $SN_4_0
    run "snapshots across builds: FM6 left out (MISSING, the part keeps it), 16 sections -> 4 (A..D, E..F reported)" sh -c \
        "'$OUT/snapshots_test16_0' write '$OUT/snx.nor' && '$OUT/snapshots_test_nofm6' read '$OUT/snx.nor' && '$OUT/snapshots_test4_0' read '$OUT/snx.nor'"
) >"$U.out" 2>&1 & jobbg
for s in 4 8 16; do
    job; ( jobin
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$s -o "$OUT/motion_sections_test$s" tests/motion_sections_test.c -lm
        run "motion recording with FELUCCA_SECTIONS=$s: record, save, load, plays, power cuts$([ $s = 4 ] || echo ', 4 -> sections, backup, reserve')" "$OUT/motion_sections_test$s"
    ) >"$U.out" 2>&1 & jobbg
done
for m in 0 1; do
    for s in 4 16; do
        job; ( jobin
            $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$s -DFELUCCA_MOTION=$m -o "$OUT/resume_test${m}_$s" tests/resume_test.c -lm
            run "power-on resume (autosave), FELUCCA_MOTION=$m FELUCCA_SECTIONS=$s: every track's engine, preset, values, kit back" "$OUT/resume_test${m}_$s"
        ) >"$U.out" 2>&1 & jobbg
    done
done
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $FM6_PACKS_ON -o "$OUT/fm6_test" tests/fm6_test.c -lm
    run "FM6: DX7 algorithms, voices (INIT VOICE and every pack's), pitch, levels, envelopes, modulation, SysEx in (the ISR side)" "$OUT/fm6_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fm6_ams_test" tests/fm6_ams_test.c -lm
    run "FM6: AMS as Dexed's doubles figure it, every modulation" "$OUT/fm6_ams_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fm6_tables_test" tests/fm6_tables_test.c -lm
    run "FM6: the tables built at boot / figured where read, every entry as Dexed's" "$OUT/fm6_tables_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fm6_sysex_test" tests/fm6_sysex_test.c -lm
    run "FM6: DX7 SysEx in (a voice into the FM6 track, a 32-voice bank as the pick list; bad checksums, no FM6 track; the rest let go)" "$OUT/fm6_sysex_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    FC="$CC -O2 -w -I$HGEN -Ifirmware/src -DFELUCCA_FLASH=1"
    $FC -DFELUCCA_FM6_PK_KEYS=1 -DFELUCCA_FM6_PK_80S=1 -o "$OUT/fm6_compat_w" tests/fm6_compat_test.c -lm
    $FC -DFELUCCA_FM6_PK_80S=1 -o "$OUT/fm6_compat_80s" tests/fm6_compat_test.c -lm
    $FC -DFELUCCA_FM6_PK_BASS=1 -o "$OUT/fm6_compat_bass" tests/fm6_compat_test.c -lm
    $FC -o "$OUT/fm6_compat_none" tests/fm6_compat_test.c -lm
    $FC $FM6_PACKS_ON -o "$OUT/fm6_compat_all" tests/fm6_compat_test.c -lm
    run "FM6 old data and pack ids, no pack: an old R / U VOICE number is INIT VOICE, MOD / M.TIM / C.TIM baked into the inline voice, user preset records" "$OUT/fm6_compat_none"
    run "FM6 old data and pack ids, every pack: the same, no old number lands on a pack voice" "$OUT/fm6_compat_all"
    run "FM6 pack ids: a project with an 80S voice (packs KEYS + 80S) stores a fixed number" "$OUT/fm6_compat_w" write "$OUT/fm6_compat_A.bin"
    run "FM6 pack ids: ... it loads as the same voice in a build with the 80S pack alone (another list index)" "$OUT/fm6_compat_80s" check "$OUT/fm6_compat_A.bin"
    run "FM6 pack ids: ... as INIT VOICE with the voice kept in a build without that pack" "$OUT/fm6_compat_bass" check "$OUT/fm6_compat_A.bin"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "FM6 voice packs: the headers are what tools/fm6_packs/pk_*.py and levels.json make" python3 tools/gen_fm6_packs.py --check
    run "FM6 voice packs, 80 voices, listened to by numbers (tools/fm6_packs_check.py): level -17.5 LUFS as the factory presets, no note over +6 dBFS, none silent, DC, pitch" \
        python3 tools/fm6_packs_check.py --root .
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    if [ -n "$DEXED_SRC" ]; then
        run "FM6 vs Dexed: sample-exact renders (DEXED_SRC)" sh tests/fm6_parity.sh --quick
    else
        echo "== FM6 vs Dexed: skipped (DEXED_SRC=<dexed>/Source to run tests/fm6_parity.sh)"
    fi
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
    mkdir -p build/slicer_demo
    run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    # the shared DSP blocks (docs/DSP-SHARED.md) against the copies they replaced, every engine and kit built
    # (-ffp-contract=off: the float blocks as the FM-1's float units compile them, no fused multiply-add)
    $CC -O2 -w -ffp-contract=off -I"$HGEN" -Ifirmware/src $X0X_ON -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_USB_AUDIO=1 -o "$OUT/dsp_shared_test" tests/dsp_shared_test.c -lm
    run "shared DSP blocks: each the same as every copy it replaced, bit for bit (exhaustive or 2^24 inputs)" "$OUT/dsp_shared_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
    run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -o "$OUT/regress_x0x" tests/regress.c -lm
    run "regression with the X0X kits built: the same goldens, their CPU (cpu/drums/x0x*; BUDGET_UPDATE=1 here keeps them)" "$OUT/regress_x0x" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $FM6_PACKS_ON -o "$OUT/regress_fm6" tests/regress.c -lm
    run "regression with every FM6 voice pack built: the same goldens, each pack voice's CPU (cpu/FM6/pk*: the CPU guard's FM6 cost; BUDGET_UPDATE=1 here keeps them)" "$OUT/regress_fm6" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_TRK_FILT=1 -o "$OUT/regress_tf" tests/regress.c -lm
    run "regression with the track FILTER built (TRK_FILT, at 0 in the default slots): the same goldens, the FILTER inserts' CPU (cpu/fx/part_filt, dbus_filt; BUDGET_UPDATE=1 here keeps them)" "$OUT/regress_tf" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $INSFX_ON -o "$OUT/regress_insfx" tests/regress.c -lm
    run "regression with the further inserts built (INS_*, in no slot by default): the same goldens, their CPU (cpu/fx/part_<t>, dbus_<t>; BUDGET_UPDATE=1 here keeps them)" "$OUT/regress_insfx" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_NOISE=1 -o "$OUT/regress_noise" tests/regress.c -lm
    run "regression with the NOISE engine built (FELUCCA_ENG_NOISE): the same golden renders, NOISE's health, voices and CPU (its renders: tests/golden_noise.txt)" "$OUT/regress_noise" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_DRONE=1 -o "$OUT/regress_drone" tests/regress.c -lm
    run "regression with the DRONE engine built (FELUCCA_ENG_DRONE): the same golden renders, DRONE's health, voices and CPU (its renders: tests/golden_drone.txt)" "$OUT/regress_drone" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_PLAITS=1 -o "$OUT/regress_plaits" tests/regress.c -lm
    run "regression with the PLAITS engine built (FELUCCA_ENG_PLAITS): the same golden renders, PLAITS's health, voices and CPU (its renders: tests/golden_plaits.txt)" "$OUT/regress_plaits" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_CZ=1 -o "$OUT/regress_cz" tests/regress.c -lm
    run "regression with the CZ engine built (FELUCCA_ENG_CZ): the same golden renders, CZ's health and voices (its renders: tests/golden_cz.txt)" "$OUT/regress_cz" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_JUNO=1 -o "$OUT/regress_juno" tests/regress.c -lm
    run "regression with the JUNO engine built (FELUCCA_ENG_JUNO): the same golden renders, JUNO's health, voices and CPU (its renders: tests/golden_juno.txt)" "$OUT/regress_juno" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -o "$OUT/regress_cg" tests/regress.c -lm
    run "regression with the CPU guard built (FELUCCA_CPU_GUARD): the same golden renders (it acts only under overload)" "$OUT/regress_cg" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -o "$OUT/console_parse_test" tests/console_parse_test.c
    run "console: the run-control commands parse (play pause stop rec proj load heavy wdtreset), others fall through" "$OUT/console_parse_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -o "$OUT/dual_guard_test" tests/dual_guard_test.c
    run "second core: only the next posted job runs (check word, part mask), the boot canary, 2ND CORE AUTO / OFF, the boost's P33 steps" "$OUT/dual_guard_test"
    $CC -DDUAL_MODE_MENU=1 -o "$OUT/dual_guard_menu_test" tests/dual_guard_test.c
    run "second core with a 2ND CORE menu: AUTO turns itself OFF after a misread" "$OUT/dual_guard_menu_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O1 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_ENG_ACID=1 -o "$OUT/cpuguard_test" tests/cpuguard_test.c -lm
    run "CPU guard: cost model, prediction, hysteresis, what each level eases, never the bass or lead; the reactive path (late halves among light ones)" "$OUT/cpuguard_test"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $(python3 tools/config_flags.py tests/profiles/mots-like.config) -o "$OUT/overload_stress" tests/overload_stress.c -lm
    run "overload stress (a measurement, docs/CPU-GUARD.md, the reactive path): per-half costs while chords are played, the guard with and without its reactive path" "$OUT/overload_stress" 10
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O1 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_ENG_ACID=1 $INSFX_ON -o "$OUT/cpuguard_ifx_test" tests/cpuguard_test.c -lm
    run "CPU guard with the further inserts built (INS_*; CHRS ECHO counted only with an instance)" "$OUT/cpuguard_ifx_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O1 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_ENG_ACID=1 $INSFX_ON -DFELUCCA_INS_JCHORUS=1 -DFELUCCA_USB_AUDIO=1 -o "$OUT/cpuguard_usb_test" tests/cpuguard_test.c -lm
    run "CPU guard with USB audio: the USB input's insert slots counted (any insert type, EQ3 only engaged, FX off none)" "$OUT/cpuguard_usb_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O1 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_ENG_ACID=1 $X0X_ON -o "$OUT/cpuguard_x0x_test" tests/cpuguard_test.c -lm
    run "CPU guard with the X0X kits: their channels in the model, the drum tails at quality, never a drum shed" "$OUT/cpuguard_x0x_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O1 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_FDN8=1 -DFELUCCA_REV_POOL=1 -o "$OUT/cpuguard_fdn8_test" tests/cpuguard_test.c -lm
    run "CPU guard with FDN8 alone in the pool (the mots profile): the REVERB level fades the tail out and clears its 32 KB line" "$OUT/cpuguard_fdn8_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "CPU guard: its cost model is the one tests/cpu_baseline.txt and costs.json give" python3 tools/builder/cpu_costs.py --check
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -o "$OUT/regress_macros" tests/regress.c -lm
    run "regression with the macros built in, at home (FELUCCA_MACROS, ENERGY): the same golden renders" "$OUT/regress_macros" tests/golden.txt tests/cpu_baseline.txt
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    # REV_HALF changes every render with a reverb send (by design: the tank at 22.05 kHz): its own goldens (the renders
    # without one equal tests/golden.txt's); the CPU against a copy of the baseline (BUDGET_UPDATE=1 keeps the full rate's)
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_REV_HALF=1 -o "$OUT/regress_rh" tests/regress.c -lm
    cp tests/cpu_baseline.txt "$OUT/cpu_baseline_rh.txt"
    run "regression with the reverb at half rate (REV_HALF): tests/golden_rev_half.txt, health, CPU" "$OUT/regress_rh" tests/golden_rev_half.txt "$OUT/cpu_baseline_rh.txt"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/analog2_test" tests/analog2_test.c -lm
    run "ANALOG 2: aliasing, filter response and self-oscillation, zipper, ENV2 (voice.c env2_tick, its slots: the 160 renders of DST2 + AMT2 bit for bit), FLT B (OFF identical, HP, LP, ringing bounded, DEST FLT B) (analog2_test alias / filter / zipper / env2 / fltb)" "$OUT/analog2_test" check
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_A2_LADR=1 -DFELUCCA_A2_OTA=1 -DFELUCCA_A2_DIOD=1 -DFELUCCA_A2_MS20=1 -o "$OUT/a2_models_test" tests/a2_models_test.c -lm
    run "ANALOG 2's filter models LADR OTA DIOD MS20: each FTYP's shape, self-oscillation in tune, full-scale noise at RES 127 bounded, a model switched under a note, FLT 2's MODEL / FTYP cards, FLT B's cards and every model through it bounded (a2_models_test resp / osc / speed)" "$OUT/a2_models_test" check
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/osc2_fine_test" tests/osc2_fine_test.c -lm
    run "ANALOG 2, PHASE: osc 2 keeps DTN to osc 1 under fine TUNE, UNISON detune, glide, LFO pitch" "$OUT/osc2_fine_test"
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ANALOG2=0 -o "$OUT/osc2_fine_a1_test" tests/osc2_fine_test.c -lm
    run "the original ANALOG (FELUCCA_ANALOG2=0): osc 2 keeps DTN to osc 1 the same way" "$OUT/osc2_fine_a1_test"
) >"$U.out" 2>&1 & jobbg
# SLICE (tests/slice_test.c) needs a FELUCCA_SLICE=1 build; the engine is not built by default

job; ( jobin
    run "regression: target cost of the render loops" python3 tests/target_budget.py \
        build/felucca.dis tests/target_budget.txt
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "firmware builder: registry rules, X0X notices, items never offered, profiles, header, fit" early_out t_builder
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "builder flash guard: no flash of a stale or other build, the package's source shown; f builds first, the bar follows" python3 tests/builder_flash_guard_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "build and flash progress: the stage lines, fm1_install's output, one rising percentage, the streaming runner" python3 tests/builder_progress_test.py
) >"$U.out" 2>&1 & jobbg
# (the menu needs Textual: tools/menuconfig makes this venv; BUILDER_VENV, ./tools/builder/venv, in a git worktree the main checkout's)
job; ( jobin
    BPY="$(python3 -c 'import sys; sys.path.insert(0, "tools"); import deps; print(deps.venv_python())')"
    if [ -x "$BPY" ] && "$BPY" -c 'import textual' 2>/dev/null; then
        run "firmware builder menu (headless): an error marks its items' lines at once, CANNOT BUILD kept" "$BPY" tests/builder_menu_test.py
    else
        echo "== builder menu: skipped (no Textual; run tools/menuconfig once to make tools/builder/venv)"
    fi
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "fm1_cpu.py: the console's status parsed, its keys printed by console.c, the 85 % shed level" python3 tests/fm1_cpu_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "doc coverage: every builder item, page, insert type and feature has its words in a user document" python3 tools/doc_coverage.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "optimist.py: the command line, toolchain backends, SDK lookup, emulator launcher" python3 tests/optimist_cli_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "emu-check --jobs: the driver built once by the parent, the checks' processes never run cargo (integration57's race)" python3 tests/emu_driver_once_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "emu-check reads: a byte array at any alignment (session.mem / word, fxkit, panel; the fx check's dins_amt 0xDEADBEEF)" python3 tests/emu_mem_align_test.py
) >"$U.out" 2>&1 & jobbg
job; ( jobin
    run "rescue tool (fm1_rescue.py, from X0X) against a simulated UBOOT FM-1" python3 tests/rescue_test.py
    run "rescue tool with any .fwsc (validation, confirmation flag, fake USB transport)" python3 tests/rescue_any_test.py
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    if command -v node >/dev/null 2>&1; then
        run "web pages: editor protocol, samples, packages, update protocol, the web builder" early_out t_web
    else
        echo "== skip web tests (no node)"
    fi
) >"$U.out" 2>&1 & jobbg

job; ( jobin
    if command -v node >/dev/null 2>&1 && command -v emcc >/dev/null 2>&1; then
        run "the simulator: the firmware as WebAssembly (boot, screen, sound, the editor, backup / restore)" t_sim
    else
        echo "== skip the simulator test (no node or emcc: brew install emscripten)"
    fi
) >"$U.out" 2>&1 & jobbg

while [ $np -lt $nu ]; do   # every unit printed
    flush
    if [ $np -lt $nu ]; then sleep $NAP; fi
done
for t in $TANKS; do cat "$OUT/reverb/tanks-$t.tsv"; done > "$OUT/reverb/tanks.tsv"

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }


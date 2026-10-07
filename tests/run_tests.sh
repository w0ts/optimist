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
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
SEC4="-DFELUCCA_SECTIONS=4"   # (the tests of the four project slots in RAM; the log: sec_*_test, sections_test)
fail=0
# the backported features' test (tests/backports_test.c) builds with every switch on (firmware/src/backports.h)
BACKPORTS_ON="-DFELUCCA_CHANCE=1 -DFELUCCA_KEYLIT=1 -DFELUCCA_QNT_SEQ=1 -DFELUCCA_SPRING=1 -DFELUCCA_BASSPLUS=1 -DFELUCCA_BRIGHT=1 -DFELUCCA_DLY_HALVE=1 -DFELUCCA_MOTION=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1"
run() { echo "== $1"; d=$1; shift; "$@" || { fail=1; echo "!! FAILED: $d"; }; }

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

$CC -w -Ifirmware/hal -o "$OUT/encoder_test" tests/encoder_test.c
run "encoders: first click, direction and reversed transitions" "$OUT/encoder_test"
$CC -w -Ifirmware/hal -o "$OUT/encoder_fast_test" tests/encoder_fast_test.c
run "encoders: fast turns at 4 / 2 / 1 scans a state, flicks, glitches (X0X)" "$OUT/encoder_fast_test"
$CC -w -Ifirmware/hal -DFELUCCA_KNOB_ONEREST=1 -o "$OUT/encoder_onerest_test" tests/encoder_onerest_test.c
run "knobs, one rest state (SLOOP 2.3 test, FELUCCA_KNOB_ONEREST=1): pauses mid-click, bounce, flicks, parked" "$OUT/encoder_onerest_test"
$CC -w -Ifirmware/hal -DFELUCCA_KNOB_ONEREST=1 -o "$OUT/encoder_fast_onerest_test" tests/encoder_fast_test.c
run "knobs, one rest state: X0X's fast turns and flicks (full-cycle detents)" "$OUT/encoder_fast_onerest_test"
$CC -w -Ifirmware/hal -DFELUCCA_KEYS_FAST=0 -o "$OUT/keys_test0" tests/keys_test.c
run "keys: integrating debounce (FELUCCA_KEYS_FAST=0): latency, glitch, bounce, chatter" "$OUT/keys_test0"
$CC -w -Ifirmware/hal -DFELUCCA_KEYS_FAST=1 -o "$OUT/keys_test1" tests/keys_test.c
run "keys: read with their column (SLOOP 2.3, FELUCCA_KEYS_FAST=1): ~1.6 ms sooner, glitch, bounce, chatter" "$OUT/keys_test1"
$CC -o "$OUT/knob_accel_test" tests/knob_accel_test.c
run "knob acceleration by turn speed (X0X curve), lists exact" "$OUT/knob_accel_test"
run "divides by a variable: each listed with why it cannot be 0 (a wrong value; the div0 trap is off)" python3 tools/div_audit.py

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -DFELUCCA_ST_STRICT=1 -o "$OUT/storage_test_strict" tests/storage_test.c
run "flash storage, strict (SLOOP 2.3: the copy a record was written to, object bounds, whole-header read back)" "$OUT/storage_test_strict"

$CC -o "$OUT/recovery_test" tests/recovery_test.c
run "application USB recovery and boot-loop guard" "$OUT/recovery_test"

$CC -o "$OUT/arranger_test" tests/arranger_test.c
run "song order, timing, repeats and missing scenes" "$OUT/arranger_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/song_audio_test" tests/song_audio_test.c -lm
run "song: four simultaneous tracks, scene transition and stop" "$OUT/song_audio_test" "$OUT/song-demo.wav"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/song_ui_test" tests/song_ui_test.c -lm
run "song screen: commands, load (OCT+ twice), display bounds" "$OUT/song_ui_test" "$OUT/song-screen.ppm"

$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/studio_drums_test" tests/studio_drums_test.c -lm
run "drum lanes, kit audio, metronome, record arm, free take" "$OUT/studio_drums_test" "$OUT/drum-styles.wav"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/seq2_test" tests/seq2_test.c -lm
run "sequencer 2.0: no drift, ratchets, roll, erase / undo, ghost / hard, chords, mute / solo" "$OUT/seq2_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 -o "$OUT/backports_test" tests/backports_test.c -lm
run "backported features (each switch on): chance, QNT SEQ, spring reverb, BASS+, delay halving, motion, PHYS, ACID" "$OUT/backports_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/master_comp_test" tests/master_comp_test.c -lm
run "master COMP / LIMIT: the static curve (+-0.5 dB), attack / release / AUTO, bit-exact when off, no sample past CEIL, the project's bytes" "$OUT/master_comp_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_CZ=1 -o "$OUT/cz_test" tests/cz_test.c -lm
run "CZ engine (FELUCCA_ENG_CZ=1): UID 13, every preset audible / bounded / voices free, tests/golden_cz.txt, EDIT values, retrigger" "$OUT/cz_test" tests/golden_cz.txt
# the reverb (fx.c): at 44.1 kHz, then with REV_HALF (the tank at 22.05 kHz) against those numbers; SPRING beside it
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/reverb_test" tests/reverb_test.c -lm
run "reverb: decay, level, bands, the tail to exactly 0 and idle" "$OUT/reverb_test" "$OUT/reverb_full.txt"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_REV_HALF=1 -o "$OUT/reverb_test_half" tests/reverb_test.c -lm
run "reverb at half rate (REV_HALF): RT60 within 5 %, the level below 8 kHz within 1 dB of the full rate's" "$OUT/reverb_test_half" "$OUT/reverb_full.txt"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/delay_test" tests/delay_test.c -lm
run "delay: the gain of each repeat over FDBK / COLOR, 100 % endless, the loop's saturator, idle again when lowered" "$OUT/delay_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/dist_test" tests/dist_test.c -lm
run "DIST: harmonics rise with DST at any level, loudness held, the bass kept, DST 0 / bypass untouched" "$OUT/dist_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 -DFELUCCA_REV_HALF=1 -o "$OUT/backports_rh_test" tests/backports_test.c -lm
run "backported features with REV_HALF: the spring reverb in the half-rate ROOM's line" "$OUT/backports_rh_test"
# the reverb tanks (builder items REV_ROOM / REV_PLATE / REV_FDN8: fx.c ROOM, reverb_alt.c PLATE and FDN8), each alone
# at both budgets: they ring out to exactly 0 and go idle; their numbers (RT60, echo density, ripple, level, host
# instructions) and renders ($OUT/reverb/<tank>-<setting>-mix|wet.wav) for comparison; with SPRING too (TYPE: the tank
# or SPRING, the switch clears each tank)
mkdir -p "$OUT/reverb"
: > "$OUT/reverb/tanks.tsv"
# (FELUCCA_REVERB=1 / 2: registry.h's one-tank switch, that tank alone; tests/reverb_proto.c keys its checks on it)
for rt in "plate:-DFELUCCA_REVERB=1 -DFELUCCA_REV_HALF=0" "plate-half:-DFELUCCA_REVERB=1 -DFELUCCA_REV_HALF=1" \
          "fdn8:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=0" "fdn8-half:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=1" \
          "fdn8-pool:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=0 -DFELUCCA_REV_POOL=1" \
          "fdn8-pool-half:-DFELUCCA_REVERB=2 -DFELUCCA_REV_HALF=1 -DFELUCCA_REV_POOL=1"; do   # (REV_POOL: FDN8's ring twice as long)
    t=${rt%%:*}; f=${rt#*:}
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/reverb_proto_$t" tests/reverb_proto.c -lm
    run "reverb tank $t: rings out to exactly 0, idle; numbers and renders in $OUT/reverb" "$OUT/reverb_proto_$t" "$t" "$OUT/reverb" "$OUT/reverb/tanks.tsv"
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 $f -o "$OUT/backports_${t}_test" tests/backports_test.c -lm
    run "backported features with the reverb tank $t (SPRING beside it)" "$OUT/backports_${t}_test"
done
# REVERB > TYPE (rev_type.c, fx.c rev_bus): the algorithms built, on the device's list; every pair switched at run
# time (a fade, the shared line cleared, the new one from silence, then exactly 0 and idle); projects (saved, older
# ones, an algorithm not built: the first one, MISSING); one built: no TYPE
for rs in "all:-DFELUCCA_REV_PLATE=1 -DFELUCCA_REV_FDN8=1 -DFELUCCA_SPRING=1" "all-half:-DFELUCCA_REV_PLATE=1 -DFELUCCA_REV_FDN8=1 -DFELUCCA_SPRING=1 -DFELUCCA_REV_HALF=1" \
          "room-plate:-DFELUCCA_REV_PLATE=1" "fdn8-spring:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_FDN8=1 -DFELUCCA_SPRING=1" \
          "room:" "plate:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_PLATE=1" "spring:-DFELUCCA_REV_ROOM=0 -DFELUCCA_SPRING=1"; do
    t=${rs%%:*}; f=${rs#*:}
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 $f -o "$OUT/reverb_select_$t" tests/reverb_select_test.c -lm
    run "REVERB > TYPE, $t built: the list, every switch clean (fade, cleared, exactly 0, idle), projects, MISSING" "$OUT/reverb_select_$t"
done
# the performance macros (firmware/src/macro.c): with MACROS, ENERGY and motion recording; once without, for the hash
MACROS_ON="-DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -DFELUCCA_MOTION=1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $MACROS_ON $SEC4 -o "$OUT/macro_test" tests/macro_test.c -lm
run "performance macros: mapping per engine, storage, ENERGY bands, motion, audible on the mix (build/host/macro-*.wav)" "$OUT/macro_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/macro_test_off" tests/macro_test.c -lm
run "macros at home: the 4-track mix renders bit-identical with and without the switch" sh -c \
    "[ \"\$('$OUT/macro_test' hash)\" = \"\$('$OUT/macro_test_off' hash)\" ]"
BP23_ON="-DFELUCCA_MONO_RELEASE=1 -DFELUCCA_ST_STRICT=1 -DFELUCCA_USB_FLOW=1 -DFELUCCA_SHED_FADE=1 -DFELUCCA_REC_MODES=1 -DFELUCCA_LIGHTS=1 -DFELUCCA_GLIDE=1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $BP23_ON -o "$OUT/bp23_test" tests/bp23_test.c -lm
run "SLOOP 2.3 / X0X 0.10.1 backports (each switch on): no stuck note after a VOICE change, overload fades, REC modes and count-in, glides" "$OUT/bp23_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/undo_test" tests/undo_test.c -lm
run "undo history: 300-level chains bit-exact on every track, links, eviction, ring sizing, recording while playing" "$OUT/undo_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_UNDO_HISTORY=0 -o "$OUT/undo_test1" tests/undo_test.c -lm
run "undo: the single level (FELUCCA_UNDO_HISTORY=0)" "$OUT/undo_test1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/clock_sync_test" tests/clock_sync_test.c -lm
run "MIDI clock: follow USB / TRS (SYNC AUTO, jitter, ramps, start / stop / continue / SPP), on-time steps" "$OUT/clock_sync_test"

$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drumkit_test" tests/drumkit_test.c -lm
run "synthesised drum kits: every kit x sound bounded, audible, finite, levels, cost" "$OUT/drumkit_test" "$OUT/drum-kits.wav" "$OUT/drum-kits.txt"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drum_edit_test" tests/drum_edit_test.c -lm
run "drum lanes: sound editor offsets on a hit, user samples on a lane, other kits' sounds, FUN7 / FUN8 / FUN9 -> FUNA" "$OUT/drum_edit_test" "$OUT"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/drum_kits_test" tests/drum_kits_test.c -lm
run "user drum kits: bank round trip on simulated flash, torn write, USR3 64 KiB, editor cmds 36..42" "$OUT/drum_kits_test"
X0X_ON="-DFELUCCA_DRUM_X909=1 -DFELUCCA_DRUM_X808=1"   # the X0X kits (off by default; tools/builder: DRUM_X0X909 / 808)
mkdir -p "$OUT/x0x_drums"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -o "$OUT/x0x_drums_test" tests/x0x_drums_test.c -lm
run "X0X 909 / 808 kits: every note bounded and ended, levels, velocity, offsets, ratchets, SOUND editor, sends, mute, lanes, projects" "$OUT/x0x_drums_test" "$OUT/x0x_drums"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -DFELUCCA_GLIDE=1 -o "$OUT/x0x_drums_glide" tests/x0x_drums_test.c -lm
run "X0X kits with the mixer glides (GLIDE): heard when no other drum voice sounds (drums_mix counts the X0X channels)" "$OUT/x0x_drums_glide"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/kits_sound_test" tests/kits_sound_test.c -lm
run "every built drum kit makes sound: each lane of each kit, hit alone" "$OUT/kits_sound_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -DFELUCCA_GLIDE=1 -o "$OUT/kits_sound_x0x" tests/kits_sound_test.c -lm
run "every built drum kit makes sound, the X0X kits and GLIDE built" "$OUT/kits_sound_x0x"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_DRUM_X909=1 -DFELUCCA_X909_CYM=0 -o "$OUT/x0x_drums_test1" tests/x0x_drums_test.c -lm
run "X0X 909 without its ride and crash samples; the 808 not built: its stand-in" "$OUT/x0x_drums_test1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -DFELUCCA_X909_CYM=2 -o "$OUT/x0x_drums_test2" tests/x0x_drums_test.c -lm
run "X0X kits with the 6-bit ride and crash (X909_CYM 2): the same checks, the 6-bit samples read back" "$OUT/x0x_drums_test2"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/x0x_drums_test0" tests/x0x_drums_test.c -lm
run "X0X kits not built: projects naming them play the stand-ins and keep the kit" "$OUT/x0x_drums_test0"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/drum_sends_test" tests/drum_sends_test.c -lm
run "drum lane sends: per-lane REV / DLY / CHO, FUNA + drum records (torn writes), DKB3 kits, editor v2" "$OUT/drum_sends_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/backup_test" tests/backup_test.c -lm
run "backup / restore: every stored object round trip, torn transfers and commits, an older project migrates" "$OUT/backup_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/punch_test" tests/punch_test.c -lm
run "punch-in FX: 16 effects, bounded, dry after release, FX-held keys" "$OUT/punch_test" "$OUT/punch-fx.wav"

$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/editor_sync_test" tests/editor_sync_test.c -lm
run "editor protocol v9: every track pushed, coalesced, never before a reply; the stream; the meter tap" "$OUT/editor_sync_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/ui_pages_test" tests/ui_pages_test.c -lm
run "live UI: pages, layers (punch, steps, erase, roll, key, mix), holds, drums, REC, fuzz" "$OUT/ui_pages_test" "$OUT"
mkdir -p "$OUT/nochord"   # (its screens apart: the STEP page without the chord names)
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_CHORD_NAMES=0 $SEC4 -o "$OUT/ui_pages_nochord_test" tests/ui_pages_test.c -lm
run "live UI without the chord names (FELUCCA_CHORD_NAMES=0): the STEP page as before" "$OUT/ui_pages_nochord_test" "$OUT/nochord"

$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BACKPORTS_ON -DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_pages_bp_test" tests/ui_pages_test.c -lm
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BP23_ON $SEC4 -o "$OUT/ui_pages_bp23_test" tests/ui_pages_test.c -lm
run "live UI with the SLOOP 2.3 / X0X 0.10.1 switches on (tests/bp23_ui.c: panel table, REC screen, LIGHTS / KEYS / NOTES)" "$OUT/ui_pages_bp23_test" "$OUT"
for m in "-DFELUCCA_FM6_MODERN=0" "-DFELUCCA_FM6_MODERN=0 -DFELUCCA_FM6_OPL=0"; do
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 $m -o "$OUT/ui_pages_fm6m_test" tests/ui_pages_test.c -lm
    mkdir -p "$OUT/fm6m"
    run "live UI, FM6 with fewer ENGINE modes ($m): ENGINE lists those built, one: EDIT 2 hidden; fuzz" "$OUT/ui_pages_fm6m_test" "$OUT/fm6m"
done
run "live UI with every backported switch on (tests/backports_ui.c: chance, played-note keys, reverb type, BASS+, brightness, motion page, ACID GEN) and the help line, fuzz" "$OUT/ui_pages_bp_test" "$OUT"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $X0X_ON -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_pages_x0x_test" tests/ui_pages_test.c -lm
run "live UI with the X0X kits built (their SOUND pages), fuzz" "$OUT/ui_pages_x0x_test" "$OUT"
# the Felucca 1.0.2 / 1.0.3 small options (tests/fel102_ui.c), all on
FEL102_ON="-DFELUCCA_BPM_LOCK=1 -DFELUCCA_DIV_ORDER=1 -DFELUCCA_PUNCH_LATCH=1 -DFELUCCA_MOTION=1 -DFELUCCA_MOTION_MARK=1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $FEL102_ON -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_pages_fel102_test" tests/ui_pages_test.c -lm
run "live UI with the Felucca 1.0.2 / 1.0.3 options on (tests/fel102_ui.c: BPM LOCK, DIV ORDER, PUNCH LATCH, the motion mark), fuzz" "$OUT/ui_pages_fel102_test" "$OUT"

# the knobs' help lines (FELUCCA_PARAM_HELP, tools/param_help.json; tests/param_help_ui.c): every value a knob reaches
# in a build has a line that fits the top bar, in these switch sets; the editor's copy is the table's
run "help lines: tools/param_help.json valid, web/editor.html's copy up to date" python3 tools/gen_param_help.py --check
PH_ALL="$BACKPORTS_ON -DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 $BP23_ON $X0X_ON $FEL102_ON -DFELUCCA_SLICE=1"
for m in "" "$PH_ALL" "-DFELUCCA_ANALOG2=0" "-DFELUCCA_FM6_MODERN=0 -DFELUCCA_FM6_OPL=0" "-DFELUCCA_FX_REVERB=0 -DFELUCCA_FX_DELAY=0 -DFELUCCA_DRUM_EDIT=0"; do
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_PARAM_HELP=1 $SEC4 $m -o "$OUT/param_help_test" tests/param_help_test.c -lm
    mkdir -p "$OUT/help"
    run "help lines: every page, engine and mode, drum sound, FM6 page and live screen has its line; only a knob turning shows it (${m:-defaults})" "$OUT/param_help_test" "$OUT/help"
done

$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/soak_test" tests/soak_test.c -lm
run "soak: ${SOAK_MIN:-10} minutes of random live use (bounded, no hanging voices, idle after stop)" "$OUT/soak_test" "${SOAK_MIN:-10}"

$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets (UP_PUT parser, bank round trip, versions)" "$OUT/upreset_test"

# USB audio (from Melodee; FELUCCA_USB_AUDIO): stream logic, endpoint driver, descriptors, stems
$CC -o "$OUT/usb_audio_test" tests/usb_audio_test.c -lm
run "USB audio: routing, clock drift and stream recovery" "$OUT/usb_audio_test"
for rs in 0 1; do
    $CC -O2 -w -DFELUCCA_UA_RESAMPLE=$rs -o "$OUT/usb_audio_clock_test$rs" tests/usb_audio_clock_test.c -lm
    run "USB audio capture against a drifting I2S clock (FELUCCA_UA_RESAMPLE=$rs)" "$OUT/usb_audio_clock_test$rs"
done
$CC -o "$OUT/usb_audio_driver_test" tests/usb_audio_driver_test.c
run "USB audio: endpoint lifecycle and packet ownership" "$OUT/usb_audio_driver_test"
run "USB descriptors: MIDI, CDC and audio configurations" python3 tests/usb_audio_desc_test.py
run "parameter icons: tools/draw_icons.py -> assets/icons.png -> gen_icons.py (86 x 36 B, all distinct)" python3 tests/icons_test.py
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/usb_audio_tracks_test" tests/usb_audio_tracks_test.c -lm
run "USB audio: four isolated track stems through the real mixer" "$OUT/usb_audio_tracks_test"
# the audio path's skips (FELUCCA_SKIP, core.h): built with them and without, the same samples after every
# neutral -> working -> neutral flip; also with the X0X kits, USB audio and the glides built
skip_same() {
    $CC -O2 -w -I"$HGEN" -Ifirmware/src "$@" -DFELUCCA_SKIP=0 -o "$OUT/skip_test0" tests/skip_test.c -lm &&
    $CC -O2 -w -I"$HGEN" -Ifirmware/src "$@" -DFELUCCA_SKIP=1 -o "$OUT/skip_test1" tests/skip_test.c -lm &&
    "$OUT/skip_test0" > "$OUT/skip_out0.txt" && "$OUT/skip_test1" > "$OUT/skip_out1.txt" &&
    { cat "$OUT/skip_out1.txt"; diff "$OUT/skip_out0.txt" "$OUT/skip_out1.txt"; }
}
run "skips (FELUCCA_SKIP): bit-identical to computing everything, settings flipped mid-sound" skip_same
run "skips with the X0X kits" skip_same $X0X_ON
run "skips with USB audio (the stems)" skip_same -DFELUCCA_USB_AUDIO=1
run "skips with the glides (GLIDE)" skip_same -DFELUCCA_GLIDE=1

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -DFELUCCA_USB_FLOW=1 -DFELUCCA_TRS_NOISE=1 -o "$OUT/midi_uart_flow_test" tests/midi_uart_test.c
run "TRS / USB MIDI parser with USB flow control and TRS_NOISE (SLOOP 2.3: a flood loses nothing, malformed ignored, an FD at the reader)" "$OUT/midi_uart_flow_test"

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc

$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/midi_expression_test" tests/midi_expression_test.c -lm
run "MIDI expression: bend, RPN 0, mod wheel, sustain, CC120 / 121 / 123 (USB and TRS)" "$OUT/midi_expression_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/midi_scale_test" tests/midi_scale_test.c -lm
run "MIDI in through the key layouts: WHITE, SNAP, chords, releases after a key change" "$OUT/midi_scale_test"
run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
mkdir -p build/tracks_demo
run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
$CC -w -I"$HGEN" -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
run "project formats (FUN6 x2 / FUN5 / FUN4 / FUN3 / FUN2 / FUN1 -> FUN7), FM6 voices, capture / apply, autosave" "$OUT/project_test"
$CC -w -I"$HGEN" -Ifirmware/src -o "$OUT/brt_full" tests/builder_rt_test.c -lm
$CC -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_FM6=0 -DFELUCCA_ENG_GRAIN=0 -o "$OUT/brt_red" tests/builder_rt_test.c -lm
run "builder: project full -> reduced (FM6, GRAIN out) -> full keeps the missing engines' parts" sh -c \
    "'$OUT/brt_full' write '$OUT/rtA.bin' && '$OUT/brt_red' reduce '$OUT/rtA.bin' '$OUT/rtB.bin' '$OUT/rtC.bin' && '$OUT/brt_full' check '$OUT/rtA.bin' '$OUT/rtB.bin' '$OUT/rtC.bin'"
$CC -w -I"$HGEN" -Ifirmware/src -DFELUCCA_FM6_MODERN=0 -DFELUCCA_FM6_OPL=0 -o "$OUT/brt_mk1" tests/builder_rt_test.c -lm
run "builder: an FM6 part saved MODERN on a MARK I only build: plays MARK I, ENGINE hidden, saved back as it was" \
    "$OUT/brt_mk1" modes "$OUT/rtA.bin" "$OUT/rtM.bin"
# missing on this build (firmware/src/miss.c): a project, a song section and a user kit of a full build on a reduced
# one (its sample header made without PIANO); the screens in $OUT/miss
mkdir -p "$OUT/gen_red" "$OUT/miss"
FELUCCA_SAMPLES_SKIP=PIANO,SCRCH python3 tools/gen_samples.py "$OUT/gen_red/felucca_samples.h" >/dev/null
MISS_RED="-DFELUCCA_ENG_FM6=0 -DFELUCCA_DRUM_SYNTH=0 -DFELUCCA_FX_DELAY=0 -DFELUCCA_FX_DUST=0 -DFELUCCA_FX_SLICER=0 -DFELUCCA_DRUM_EDIT=0"
$CC -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -DFELUCCA_CHANCE=1 -DFELUCCA_ENG_PHYS=1 -o "$OUT/miss_full" tests/missing_test.c -lm
$CC -w -I"$OUT/gen_red" -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 $MISS_RED -o "$OUT/miss_red" tests/missing_test.c -lm
run "missing on this build: full -> reduced project, song section, user kit; the message once per item, TOOLS > MISS" sh -c \
    "'$OUT/miss_full' write '$OUT/miss/A.bin' '$OUT/miss/B.bin' '$OUT/miss/K.bin' && '$OUT/miss_red' reduce '$OUT/miss/A.bin' '$OUT/miss/B.bin' '$OUT/miss/K.bin' '$OUT/miss'"
# every engine built has a preset on the PRESETS list, every drum source built a kit (tests/preset_cover_test.c; the
# builder's random configurations: tests/builder_test.py): sample headers without any set, without GRAIN's sets
mkdir -p "$OUT/gen_noset" "$OUT/gen_nogr"
FELUCCA_SAMPLES_SKIP=PIANO,BASS,VIBES,HORNS,STRGS,FLUTE,SCRCH,PERC python3 tools/gen_samples.py "$OUT/gen_noset/felucca_samples.h" >/dev/null
FELUCCA_SAMPLES_SKIP=PIANO,VIBES,FLUTE python3 tools/gen_samples.py "$OUT/gen_nogr/felucca_samples.h" >/dev/null
KITS1="-DFELUCCA_DRUM_SYNTH=0 -DFELUCCA_KIT_ACOUSTIC=0 -DFELUCCA_KIT_DEEP=0 -DFELUCCA_KIT_TIGHT=0 -DFELUCCA_KIT_BRIGHT=0"
# (every sample set: COVER_STRICT, a factory preset per engine; sets left out: INIT where none is left)
S1="-DCOVER_STRICT=1"
for c in "$HGEN|$S1" "$HGEN|$S1 -DFELUCCA_ENG_SLICE=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_FM6_VOICES=0" \
         "$HGEN|$S1 $X0X_ON -DFELUCCA_ENG_ACID=1" "$HGEN|$S1 $KITS1" "$OUT/gen_noset|-DFELUCCA_DRUM_SAMPLED=0" \
         "$OUT/gen_nogr|-DFELUCCA_ENG_SLICE=1"; do
    $CC -w -I"${c%%|*}" -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 ${c#*|} -o "$OUT/preset_cover_test" tests/preset_cover_test.c -lm
    run "every engine built has a loadable entry on the PRESETS list (a factory preset, else INIT), every drum source a kit (${c#*|})" sh -c "'$OUT/preset_cover_test' > '$OUT/preset_cover.txt' || { cat '$OUT/preset_cover.txt'; exit 1; }"
done
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sec_codec_test" tests/sec_codec_test.c -lm
run "song sections: the record codec (round trips, raw fallback, damaged records, sizes; codec B: load -> store -> load the same bytes, codec A records migrate, fuzz)" "$OUT/sec_codec_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sections_test" tests/sections_test.c -lm
run "song sections A..P: old slots migrate (cut anywhere), save / load, pending while playing, stage, MEM FULL" "$OUT/sections_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sec_log_test" tests/sec_log_test.c -lm
run "song sections: the log (restarts, compaction, writes and erases cut, MEM FULL and its reserve; the patterns' ids 24..87 kept, 16 busy codec B sections)" "$OUT/sec_log_test"
for s in 6 8 12; do                                  # (SNAPSHOTS 2 / 4 / 8)
    $CC -DSN_SECTORS=${s}u -o "$OUT/snap_store_test$s" tests/snap_store_test.c
    run "snapshots: the flash area of $s sectors (round trips, restarts, a save and a clear cut at every erase and program, FULL, DAMAGED, random)" "$OUT/snap_store_test$s"
done
for v in "16 0" "16 1" "8 0" "4 0" "4 1"; do
    set -- $v
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$1 -DFELUCCA_MOTION=$2 -o "$OUT/snapshots_test$1_$2" tests/snapshots_test.c -lm
    run "snapshots, FELUCCA_SECTIONS=$1 MOTION=$2: save, change, restart, load: every track, section, song, motion, kit exactly; cuts; BEFORE LOAD; editor export / import" "$OUT/snapshots_test$1_$2"
done
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_FM6=0 -o "$OUT/snapshots_test_nofm6" tests/snapshots_test.c -lm
run "snapshots across builds: FM6 left out (MISSING, the part keeps it), 16 sections -> 4 (A..D, E..F reported)" sh -c \
    "'$OUT/snapshots_test16_0' write '$OUT/snx.nor' && '$OUT/snapshots_test_nofm6' read '$OUT/snx.nor' && '$OUT/snapshots_test4_0' read '$OUT/snx.nor'"
for s in 4 8 16; do
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$s -o "$OUT/motion_sections_test$s" tests/motion_sections_test.c -lm
    run "motion recording with FELUCCA_SECTIONS=$s: record, save, load, plays, power cuts$([ $s = 4 ] || echo ', 4 -> sections, backup, reserve')" "$OUT/motion_sections_test$s"
done
for m in 0 1; do
    for s in 4 16; do
        $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$s -DFELUCCA_MOTION=$m -o "$OUT/resume_test${m}_$s" tests/resume_test.c -lm
        run "power-on resume (autosave), FELUCCA_MOTION=$m FELUCCA_SECTIONS=$s: every track's engine, preset, values, kit back" "$OUT/resume_test${m}_$s"
    done
done
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fm6_test" tests/fm6_test.c -lm
run "FM6: DX7 algorithms, voices, pitch, levels, envelopes, modulation" "$OUT/fm6_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fm6_ams_test" tests/fm6_ams_test.c -lm
run "FM6: AMS as Dexed's doubles figure it, every modulation" "$OUT/fm6_ams_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fm6_tables_test" tests/fm6_tables_test.c -lm
run "FM6: the tables built at boot / figured where read, every entry as Dexed's" "$OUT/fm6_tables_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_FM6_MKI_FLASH=1 -o "$OUT/fm6_tables_flash_test" tests/fm6_tables_test.c -lm
run "FM6: MARK I's tables in flash (FELUCCA_FM6_MKI_FLASH), every entry as Dexed's" "$OUT/fm6_tables_flash_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fm6_store_test" tests/fm6_store_test.c -lm
run "FM6: DX7 SysEx in, the user bank at 0xD8000 (an older USR-slot bank moved there), STORE, VOICE U.." "$OUT/fm6_store_test"
if [ -n "$DEXED_SRC" ]; then
    run "FM6 vs Dexed: sample-exact renders (DEXED_SRC)" sh tests/fm6_parity.sh --quick
else
    echo "== FM6 vs Dexed: skipped (DEXED_SRC=<dexed>/Source to run tests/fm6_parity.sh)"
fi
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
mkdir -p build/slicer_demo
run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
# the shared DSP blocks (docs/DSP-SHARED.md) against the copies they replaced, every engine and kit built
# (-ffp-contract=off: the float blocks as the FM-1's float units compile them, no fused multiply-add)
$CC -O2 -w -ffp-contract=off -I"$HGEN" -Ifirmware/src $X0X_ON -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_USB_AUDIO=1 -o "$OUT/dsp_shared_test" tests/dsp_shared_test.c -lm
run "shared DSP blocks: each the same as every copy it replaced, bit for bit (exhaustive or 2^24 inputs)" "$OUT/dsp_shared_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
$CC -O2 -w -I"$HGEN" -Ifirmware/src $X0X_ON -o "$OUT/regress_x0x" tests/regress.c -lm
run "regression with the X0X kits built: the same goldens, their CPU (cpu/drums/x0x*; BUDGET_UPDATE=1 here keeps them)" "$OUT/regress_x0x" tests/golden.txt tests/cpu_baseline.txt
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_FM6_MKI_FLASH=1 -o "$OUT/regress_mkif" tests/regress.c -lm
run "regression with MARK I's tables in flash (FELUCCA_FM6_MKI_FLASH): the same golden renders" "$OUT/regress_mkif" tests/golden.txt tests/cpu_baseline.txt
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_CZ=1 -o "$OUT/regress_cz" tests/regress.c -lm
run "regression with the CZ engine built (FELUCCA_ENG_CZ): the same golden renders, CZ's health and voices (its renders: tests/golden_cz.txt)" "$OUT/regress_cz" tests/golden.txt tests/cpu_baseline.txt
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -o "$OUT/regress_cg" tests/regress.c -lm
run "regression with the CPU guard built (FELUCCA_CPU_GUARD): the same golden renders (it acts only under overload)" "$OUT/regress_cg" tests/golden.txt tests/cpu_baseline.txt
$CC -O1 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_ENG_ACID=1 -o "$OUT/cpuguard_test" tests/cpuguard_test.c -lm
run "CPU guard: cost model, prediction, hysteresis, what each level eases, never the bass or lead" "$OUT/cpuguard_test"
$CC -O1 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CPU_GUARD=1 -DFELUCCA_ENG_ACID=1 $X0X_ON -o "$OUT/cpuguard_x0x_test" tests/cpuguard_test.c -lm
run "CPU guard with the X0X kits: their channels in the model, the drum tails at quality, never a drum shed" "$OUT/cpuguard_x0x_test"
run "CPU guard: its cost model is the one tests/cpu_baseline.txt and costs.json give" python3 tools/builder/cpu_costs.py --check
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -o "$OUT/regress_macros" tests/regress.c -lm
run "regression with the macros built in, at home (FELUCCA_MACROS, ENERGY): the same golden renders" "$OUT/regress_macros" tests/golden.txt tests/cpu_baseline.txt
# REV_HALF changes every render with a reverb send (by design: the tank at 22.05 kHz): its own goldens (the renders
# without one equal tests/golden.txt's); the CPU against a copy of the baseline (BUDGET_UPDATE=1 keeps the full rate's)
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_REV_HALF=1 -o "$OUT/regress_rh" tests/regress.c -lm
cp tests/cpu_baseline.txt "$OUT/cpu_baseline_rh.txt"
run "regression with the reverb at half rate (REV_HALF): tests/golden_rev_half.txt, health, CPU" "$OUT/regress_rh" tests/golden_rev_half.txt "$OUT/cpu_baseline_rh.txt"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/analog2_test" tests/analog2_test.c -lm
run "ANALOG 2: aliasing, filter response and self-oscillation, zipper (analog2_test alias / filter / zipper)" "$OUT/analog2_test" check
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DA2_ENV2_ALWAYS=1 -o "$OUT/analog2_always" tests/analog2_test.c -lm
a2sw() { a=$("$OUT/analog2_test" env2switch) && b=$("$OUT/analog2_always" env2switch) && echo "$a, never skipped: $b" && [ "$a" = "$b" ]; }
run "ANALOG 2: ENV2 DEST skipped at all 0, amounts switched mid-note (held, released): the samples of never skipping" a2sw
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/osc2_fine_test" tests/osc2_fine_test.c -lm
run "ANALOG 2, PHASE: osc 2 keeps DTN to osc 1 under fine TUNE, UNISON detune, glide, LFO pitch" "$OUT/osc2_fine_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ANALOG2=0 -o "$OUT/osc2_fine_a1_test" tests/osc2_fine_test.c -lm
run "the original ANALOG (FELUCCA_ANALOG2=0): osc 2 keeps DTN to osc 1 the same way" "$OUT/osc2_fine_a1_test"
# SLICE (tests/slice_test.c) needs a FELUCCA_SLICE=1 build; the engine is not built by default

run "regression: target cost of the render loops" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
run "firmware builder: registry rules, X0X notices, items never offered, profiles, header, fit" python3 tests/builder_test.py
BPY="${BUILDER_VENV:-tools/builder/venv}/bin/python"   # (the menu needs Textual: tools/menuconfig makes this venv)
if [ -x "$BPY" ] && "$BPY" -c 'import textual' 2>/dev/null; then
    run "firmware builder menu (headless): an error marks its items' lines at once, CANNOT BUILD kept" "$BPY" tests/builder_menu_test.py
else
    echo "== builder menu: skipped (no Textual; run tools/menuconfig once to make tools/builder/venv)"
fi
run "fm1_cpu.py: the console's status parsed, its keys printed by console.c, the 85 % shed level" python3 tests/fm1_cpu_test.py
run "optimist.py: the command line, toolchain backends, SDK lookup, emulator launcher" python3 tests/optimist_cli_test.py
run "rescue tool (fm1_rescue.py, from X0X) against a simulated UBOOT FM-1" python3 tests/rescue_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
else
    echo "== skip web tests (no node)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }

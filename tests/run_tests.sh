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
cd "$(dirname "$0")/.."
export AC79_SDK="${AC79_SDK:-$(python3 -c 'import sys; sys.path.insert(0, "tools"); import toolchain; print(toolchain.sdk_dir())')}"
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
SEC4="-DFELUCCA_SECTIONS=4"   # (the tests of the four project slots in RAM; the log: sec_*_test, sections_test)
fail=0
# the backported features' test (tests/backports_test.c) builds with every switch on (firmware/src/core/backports.h)
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
run "built for size: every firmware source in one list (main-loop files get minsize, tools/size_fns.py)" python3 tools/size_fns.py --check

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -DFELUCCA_ST_STRICT=1 -o "$OUT/storage_test_strict" tests/storage_test.c
run "flash storage, strict (SLOOP 2.3: the copy a record was written to, object bounds, whole-header read back)" "$OUT/storage_test_strict"
$CC -DFELUCCA_UP_FM6=1 -o "$OUT/storage_test_upf" tests/storage_test.c
run "flash storage with UP_FM6 (OBJ_UPFM6 at 0x95000, every object off the SDK's sectors)" "$OUT/storage_test_upf"
$CC -DCZ_NUSER=8 -DSN_SECTORS=8 -o "$OUT/storage_test_cz" tests/storage_test.c
run "flash storage, a CZ build without UP_FM6 (OBJ_UPFM6 a number only: never written)" "$OUT/storage_test_cz"
$CC -DCZ_NUSER=8 -DSN_SECTORS=8 -DFELUCCA_UP_FM6=1 -o "$OUT/storage_test_czupf" tests/storage_test.c
run "flash storage, CZ and UP_FM6 (the shared object number at 0x95000)" "$OUT/storage_test_czupf"
$CC -o "$OUT/upfm6_move_test" tests/upfm6_move_test.c
$CC -DCZ_NUSER=8 -DSN_SECTORS=8 -o "$OUT/upfm6_move_test_cz" tests/upfm6_move_test.c
run "UP_FM6 off the SDK VM, with the CZ collection built (CZ_NUSER)" "$OUT/upfm6_move_test_cz"
run "UP_FM6 off the SDK VM: 0x95000, the move from 0xE7000 / 0xE8000 (read only), the flash map and its guard" "$OUT/upfm6_move_test"

$CC -o "$OUT/recovery_test" tests/recovery_test.c
run "application USB recovery and boot-loop guard" "$OUT/recovery_test"

$CC -o "$OUT/arranger_test" tests/arranger_test.c
run "song order, timing, repeats and missing scenes" "$OUT/arranger_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/song_audio_test" tests/song_audio_test.c -lm
run "song: four simultaneous tracks, scene transition and stop" "$OUT/song_audio_test" "$OUT/song-demo.wav"
mkdir -p "$OUT/font4"      # the font in its alpha format: the 1-bit one draws the same pixels
python3 tools/gen_font.py "$OUT/font4/felucca_font.h" 4 >/dev/null
$CC -I"$HGEN" -Ifirmware/src -o "$OUT/font_test1" tests/font_test.c
$CC -I"$OUT/font4" -I"$HGEN" -Ifirmware/src -o "$OUT/font_test4" tests/font_test.c
run "font: the 1-bit pixel font draws what the alpha font drew (every glyph, S and L, clipped)" sh -c "\"$OUT/font_test1\" \"$OUT/font1.bin\" && \"$OUT/font_test4\" \"$OUT/font4.bin\" && cmp \"$OUT/font1.bin\" \"$OUT/font4.bin\""
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/song_ui_test" tests/song_ui_test.c -lm
run "song screen: commands, load (OCT+ twice), display bounds" "$OUT/song_ui_test" "$OUT/song-screen.ppm"

$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/studio_drums_test" tests/studio_drums_test.c -lm
run "drum lanes, kit audio, metronome, record arm, free take" "$OUT/studio_drums_test" "$OUT/drum-styles.wav"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/seq2_test" tests/seq2_test.c -lm
run "sequencer 2.0: no drift, ratchets, roll, erase / undo, ghost / hard, chords, mute / solo" "$OUT/seq2_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 -o "$OUT/backports_test" tests/backports_test.c -lm
run "backported features (each switch on): chance, QNT SEQ, spring reverb, BASS+, delay halving, motion, PHYS, ACID" "$OUT/backports_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $BACKPORTS_ON $SEC4 -DFELUCCA_TRK_FILT=1 -o "$OUT/backports_tf_test" tests/backports_test.c -lm
run "backported features with the track FILTER built (SLOOP 2.4's values after P_E7): motion's stored ids, FILT and COMP" "$OUT/backports_tf_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/master_comp_test" tests/master_comp_test.c -lm
run "master COMP / LIMIT: the static curve (+-0.5 dB), attack / release / AUTO, bit-exact when off, no sample past CEIL, the project's bytes" "$OUT/master_comp_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_TRK_FILT=1 -o "$OUT/trk_filt_test" tests/trk_filt_test.c -lm
run "track FILTER (SLOOP 2.4): LP / HP on a part, the drum bus and its sends, the project, cost" "$OUT/trk_filt_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_CHORDPLUS=1 -o "$OUT/chordplus_test" tests/chordplus_test.c -lm
run "CHORD+ (SLOOP 2.4): black-key modifiers, under the finger, recorded as played, STRUM, VLEAD" "$OUT/chordplus_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/trk_filt_test_off" tests/trk_filt_test.c -lm
run "track FILTER: every FILT at 0, the busy mix sample for sample the build without it" sh -c     "[ \"\$('$OUT/trk_filt_test' hash)\" = \"\$('$OUT/trk_filt_test_off' hash)\" ]"
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
# AIRWIN (builder item REV_AIRWIN: reverb_airwin.c, Airwindows' VerbTiny) in its three rings: alone at both rates (16 /
# 8 KB), beside ROOM in the pool (TYPE picks it), with FDN8 in the pool (32 KB): RT60 as the ROOM's, the level, the
# ring-out to exactly 0 at every SIZE / DAMP, full-scale noise / square / DC then silence (bounded, idle); then its
# numbers and renders beside the other tanks'
for ra in "alone:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_AIRWIN=1" "alone-half:-DFELUCCA_REV_ROOM=0 -DFELUCCA_REV_AIRWIN=1 -DFELUCCA_REV_HALF=1" \
          "room-pool:-DFELUCCA_REV_AIRWIN=1 -DFELUCCA_REV_POOL=1" "fdn8-pool:-DFELUCCA_REV_FDN8=1 -DFELUCCA_REV_AIRWIN=1 -DFELUCCA_REV_POOL=1"; do
    t=${ra%%:*}; f=${ra#*:}
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/reverb_airwin_$t" tests/reverb_airwin_test.c -lm
    run "reverb AIRWIN ($t): RT60 and level as the ROOM's, rings out to exactly 0, full scale bounded, idle" "$OUT/reverb_airwin_$t"
done
for rt in "airwin:-DFELUCCA_REVERB=3 -DFELUCCA_REV_HALF=0" "airwin-half:-DFELUCCA_REVERB=3 -DFELUCCA_REV_HALF=1"; do
    t=${rt%%:*}; f=${rt#*:}
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/reverb_proto_$t" tests/reverb_proto.c -lm
    run "reverb tank $t: rings out to exactly 0, idle; numbers and renders in $OUT/reverb" "$OUT/reverb_proto_$t" "$t" "$OUT/reverb" "$OUT/reverb/tanks.tsv"
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
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 $f -o "$OUT/reverb_select_$t" tests/reverb_select_test.c -lm
    run "REVERB > TYPE, $t built: the list, every switch clean (fade, cleared, exactly 0, idle), projects, MISSING" "$OUT/reverb_select_$t"
done
# the performance macros (firmware/src/core/macro.c): with MACROS, ENERGY and motion recording; once without, for the hash
MACROS_ON="-DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -DFELUCCA_MOTION=1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $MACROS_ON $SEC4 -o "$OUT/macro_test" tests/macro_test.c -lm
run "performance macros: mapping per engine, storage, ENERGY bands, motion, audible on the mix (build/host/macro-*.wav)" "$OUT/macro_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/macro_test_off" tests/macro_test.c -lm
run "macros at home: the 4-track mix renders bit-identical with and without the switch" sh -c \
    "[ \"\$('$OUT/macro_test' hash)\" = \"\$('$OUT/macro_test_off' hash)\" ]"
BP23_ON="-DFELUCCA_MONO_RELEASE=1 -DFELUCCA_ST_STRICT=1 -DFELUCCA_USB_FLOW=1 -DFELUCCA_SHED_FADE=1 -DFELUCCA_REC_MODES=1 -DFELUCCA_LIGHTS=1 -DFELUCCA_GLIDE=1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $BP23_ON -o "$OUT/bp23_test" tests/bp23_test.c -lm
run "SLOOP 2.3 / X0X 0.10.1 backports (each switch on): no stuck note after a VOICE change, overload fades, REC modes and count-in, glides" "$OUT/bp23_test"
SL24_ON="-DFELUCCA_DIV_LONG=1 -DFELUCCA_DLY_DOT=1 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_MOTION=1 -DFELUCCA_QCHAIN=1"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SL24_ON $SEC4 -o "$OUT/sl24_seq_test" tests/sl24_seq_test.c -lm
run "SLOOP 2.4 sequencer backports (each switch on): DIV 1/2..2BAR, delay 1/8D 1/16D, micro timing, fills, parameter locks (beside motion)" "$OUT/sl24_seq_test"
# the automation store (docs/UI-OPTIMIST-DESIGN.md 6, phase 3): the int8 check, a render equal to 2907501's, the store
$CC -O1 -w -I"$HGEN" -Ifirmware/src $SEC4 -DFELUCCA_ENG_PHYS=1 -DFELUCCA_ENG_ACID=1 -DFELUCCA_ENG_CZ=1 -DFELUCCA_MOTION=1 -DFELUCCA_PLOCK=1 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_TRK_FILT=1 -DFELUCCA_CHORDPLUS=1 -o "$OUT/auto_int8_check" tests/auto_int8_check.c -lm
run "the automation store's int8 check: every lockable / recordable value (TP[], every engine's EDIT) fits a signed byte" "$OUT/auto_int8_check"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SL24_ON -DFELUCCA_CHANCE=1 $SEC4 -o "$OUT/auto_render" tests/auto_render.c -lm
run "the automation store's render: nudges, fills, locks, motion, chance bits, 8 bars: the mix equal to 2907501's (micro timing as before)" "$OUT/auto_render"
run "the automation store's CPU: a step's events (24 locks + 30 hold events; a full list of 128), instructions a step within its budget" "$OUT/auto_render" cpu
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/auto_test" tests/auto_test.c -lm
run "the automation store: 128 events, both kinds on one parameter, fills, drum chance, the old forms read and written (MOTN, extras, V1 patterns), V2 and the new extras form, a scene and the autosave, 2.4 export / import, undo, AUTO_GET / AUTO_SET, HOLD, SL24_GET's lost words (v12: motion bit 13, chance the second word; the v11 reply unchanged)" "$OUT/auto_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SL24_ON $SEC4 -o "$OUT/ui_pages_sl24_test" tests/ui_pages_test.c -lm
run "live UI with the SLOOP 2.4 sequencer switches on (tests/sl24seq_ui.c: nudge, fill conditions, GLO fills, FX bypass on black keys)" "$OUT/ui_pages_sl24_test" "$OUT"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SL24_ON -DFELUCCA_CHANCE=1 $SEC4 -o "$OUT/ui_pages_auto_test" tests/ui_pages_test.c -lm
run "live UI, SLOOP's step automation through the store (tests/sloop_auto_ui.c: locks, nudges, fills, drum chance on steps 1 / 41 / 64, the 128 limit, undo, FOLLOW)" "$OUT/ui_pages_auto_test" "$OUT"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_QCHAIN=1 $SEC4 -o "$OUT/sl24_chain_test" tests/sl24_chain_test.c -lm
run "SLOOP 2.4 quick chain (FELUCCA_QCHAIN): sections in order, each for its bars, looped; STOP ends it" "$OUT/sl24_chain_test"
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
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/fx_slots_test" tests/fx_slots_test.c -lm
run "FX slots: the layout, a type in no slot unheard (the mix with it at 0, sample for sample), the FX record (sections, arena, autosave, keys)" "$OUT/fx_slots_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_TRK_FILT=1 -o "$OUT/fx_slots_tf_test" tests/fx_slots_test.c -lm
run "FX slots with the track FILTER built (D6: a slot type): a project without a record and a FILTER in use plays it in the slot it silences least" "$OUT/fx_slots_tf_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/backup_test" tests/backup_test.c -lm
run "backup / restore: every stored object round trip, torn transfers and commits, an older project migrates" "$OUT/backup_test"
$CC -O1 -Wall -o "$OUT/lane_walk_test" tests/lane_walk_test.c
run "drum lane walk: stop-at-edge stepping (core/lane_walk.h)" "$OUT/lane_walk_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/punch_test" tests/punch_test.c -lm
run "punch-in FX: 16 effects, bounded, dry after release, FX-held keys" "$OUT/punch_test" "$OUT/punch-fx.wav"

$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/editor_sync_test" tests/editor_sync_test.c -lm
run "editor protocol v9: every track pushed, coalesced, never before a reply; the stream; the meter tap" "$OUT/editor_sync_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/ui_pages_test" tests/ui_pages_test.c -lm
run "live UI: pages, layers (punch, steps, erase, roll, key, mix), holds, drums, REC, fuzz" "$OUT/ui_pages_test" "$OUT"
mkdir -p "$OUT/nochord"   # (its screens apart: the STEP page without the chord names)
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_CHORD_NAMES=0 $SEC4 -o "$OUT/ui_pages_nochord_test" tests/ui_pages_test.c -lm
run "live UI without the chord names (FELUCCA_CHORD_NAMES=0): the STEP page as before" "$OUT/ui_pages_nochord_test" "$OUT/nochord"
mkdir -p "$OUT/nb"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_ENG_CZ=1 -DFELUCCA_NATIVE_BANKS=1 $SEC4 -o "$OUT/ui_pages_nb_test" tests/ui_pages_test.c -lm
run "live UI with FELUCCA_NATIVE_BANKS=1 (CZ on): pages, PRESETS, fuzz as before (tests/nbank_test.c: its rows)" "$OUT/ui_pages_nb_test" "$OUT/nb"

$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_DRUM_STEP=1 $SEC4 -o "$OUT/ui_pages_dstep_test" tests/ui_pages_test.c -lm
run "live UI with FELUCCA_DRUM_STEP=1: the TR step sequencer (key -> step, pages, FOLLOW, the sound pick), screens, fuzz" "$OUT/ui_pages_dstep_test" "$OUT"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BACKPORTS_ON -DFELUCCA_MACROS=1 -DFELUCCA_ENERGY=1 -DFELUCCA_PARAM_HELP=1 $SEC4 -o "$OUT/ui_pages_bp_test" tests/ui_pages_test.c -lm
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BP23_ON -DFELUCCA_CDC=1 $SEC4 -o "$OUT/ui_pages_bp23_test" tests/ui_pages_test.c -lm
run "live UI with the SLOOP 2.3 / X0X 0.10.1 switches on (tests/bp23_ui.c: panel table, REC screen, LIGHTS / KEYS / NOTES)" "$OUT/ui_pages_bp23_test" "$OUT"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BP23_ON -DFELUCCA_BRIGHT=1 -DFELUCCA_BASSPLUS=1 -DFELUCCA_CDC=1 $SEC4 -o "$OUT/ui_pages_menu_test" tests/ui_pages_test.c -lm
run "HOME menu in sections (SLOOP 2.4): every screen, SELECT, the knobs per row, SYNC / OUT / IN / channels / USB SERIAL" "$OUT/ui_pages_menu_test" "$OUT"
# the Optimist UI (FELUCCA_UI=1, ui/optimist): rows, keys, the confirm, undo / redo, the layers, TEMPO, SONG, messages,
# fuzz; five switch sets (the last on the real section log with PATTERNS)
mkdir -p "$OUT/optimist"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $SEC4 -o "$OUT/ui_optimist_test" tests/ui_optimist_test.c -lm
run "Optimist UI: every screen's rows, SELECT / ALGORITHM / PRESETS / knobs, YES / NO, the confirm, undo chords, fuzz" "$OUT/ui_optimist_test" "$OUT/optimist"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BACKPORTS_ON -DFELUCCA_MACROS=1 $SEC4 -o "$OUT/ui_optimist_bp_test" tests/ui_optimist_test.c -lm
run "Optimist UI with the backports (ACID GEN, BRIGHT, BASS+, MOTION, MACRO)" "$OUT/ui_optimist_bp_test" "$OUT/optimist"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal $BP23_ON -DFELUCCA_CDC=1 -DFELUCCA_TRK_FILT=1 -DFELUCCA_PLOCK=1 $SEC4 -o "$OUT/ui_optimist_bp23_test" tests/ui_optimist_test.c -lm
run "Optimist UI with LIGHTS, USB SERIAL, the track FILTER row" "$OUT/ui_optimist_bp23_test" "$OUT/optimist"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 $SEC4 -o "$OUT/ui_optimist_sx_test" tests/ui_optimist_test.c -lm
run "Optimist UI with the step extras (STEP: nudge, chance, fill, locks)" "$OUT/ui_optimist_sx_test" "$OUT/optimist"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_CHANCE=1 -DFELUCCA_MOTION=1 $SEC4 -o "$OUT/ui_optimist_auto_test" tests/ui_optimist_test.c -lm
run "Optimist UI with the automation store whole (STEP: locks and HOLD, the marks under the steps, drum chance, HOME + a step)" "$OUT/ui_optimist_auto_test" "$OUT/optimist"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_SECTIONS=16 -DFELUCCA_PATTERNS=1 -DFELUCCA_MICRO=1 -DFELUCCA_FILLS=1 -DFELUCCA_PLOCK=1 -DFELUCCA_REC_MODES=1 -o "$OUT/ui_optimist_song_test" tests/ui_optimist_test.c -lm
run "Optimist UI with the section log and the patterns (SONG, the scenes, the session grid, the layers on the real log)" "$OUT/ui_optimist_song_test" "$OUT/optimist"
mkdir -p "$OUT/vis"   # (the visualiser's screens apart)
$CC -O2 -w -I"$HGEN" -Ifirmware/src -Ifirmware/hal -DFELUCCA_VIS=1 $SEC4 -o "$OUT/ui_pages_vis_test" tests/ui_pages_test.c -lm
run "live UI with the visualiser (FELUCCA_VIS, tests/sl24p5_vis_ui.c): HOME opens it, SELECT the 12 styles, a layer, MASTER 0" "$OUT/ui_pages_vis_test" "$OUT/vis"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_VIS=1 $SEC4 -o "$OUT/vis_tap_test" tests/vis_tap_test.c -lm
run "visualiser tap: a block's mix copied whole, block aligned over the wrap" "$OUT/vis_tap_test"
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
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_FLASH=1 -o "$OUT/ed_user_test" tests/ed_user_test.c -lm
run "editor cmds 11..21 (samples, user presets): refused while playing (rc 3 / 5, stop first), the flash untouched" "$OUT/ed_user_test"

# USB audio (from Melodee; FELUCCA_USB_AUDIO): stream logic, endpoint driver, descriptors, stems
$CC -o "$OUT/usb_audio_test" tests/usb_audio_test.c -lm
run "USB audio: routing, clock drift and stream recovery" "$OUT/usb_audio_test"
for rs in 0 1; do
    $CC -O2 -w -DFELUCCA_UA_RESAMPLE=$rs -o "$OUT/usb_audio_clock_test$rs" tests/usb_audio_clock_test.c -lm
    run "USB audio capture against a drifting I2S clock (FELUCCA_UA_RESAMPLE=$rs)" "$OUT/usb_audio_clock_test$rs"
done
$CC -o "$OUT/usb_audio_driver_test" tests/usb_audio_driver_test.c
run "USB audio: endpoint lifecycle and packet ownership" "$OUT/usb_audio_driver_test"
$CC -DFELUCCA_CDC=0 -o "$OUT/usb_serial_dump" tests/usb_serial_test.c && "$OUT/usb_serial_dump" "$OUT/usb_plain.bin" >/dev/null
$CC -DFELUCCA_CDC=1 -o "$OUT/usb_serial_test" tests/usb_serial_test.c
run "USB SERIAL (SLOOP 2.4): the console only when on, from the next start; off = a no-console build, byte for byte" "$OUT/usb_serial_test" "$OUT/usb_plain.bin"
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
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -o "$OUT/midi_ch_test" tests/midi_ch_test.c -lm
run "MIDI channels per track (SLOOP 2.4 phase 3): defaults, in, keys, OFF, the project round trip" "$OUT/midi_ch_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src $SEC4 -DFELUCCA_CDC=1 -o "$OUT/midi_seq_test" tests/midi_seq_test.c -lm
run "SEQ -> MIDI OUT and IN = CLOCK (SLOOP 2.4): every note ended, STOP, arp, rolls, channel moves, no echo" "$OUT/midi_seq_test"
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
# missing on this build (firmware/src/storage/miss.c): a project, a song section and a user kit of a full build on a reduced
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
for x in 0 1; do   # per-track patterns and scenes (FELUCCA_PATTERNS): unit; then a PATTERNS -> no PATTERNS -> PATTERNS round trip
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$x -o "$OUT/patterns_test$x" tests/patterns_test.c -lm
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$x -DFELUCCA_PATTERNS=0 -o "$OUT/patterns_test${x}n" tests/patterns_test.c -lm
    run "patterns and scenes (XSTEP=$x): store, share, copy-on-write, NO FREE PATTERN, old sections converted (cut anywhere), arena, stage, a scene's FX record (an older build's misfiled one moved), MEM FULL" "$OUT/patterns_test$x"
    run "patterns (XSTEP=$x) across builds: scenes flattened without PATTERNS, a section stored there converted back" sh -c "$OUT/patterns_test$x $OUT/pat$x.img 1 && $OUT/patterns_test${x}n $OUT/pat$x.img 2 && $OUT/patterns_test$x $OUT/pat$x.img 3"
done
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_MOTION=0 -o "$OUT/patterns_test_m0" tests/patterns_test.c -lm
run "patterns and scenes without motion recording (MOTION=0)" "$OUT/patterns_test_m0"
for f in "" "-DFELUCCA_MOTION=0" "-DFELUCCA_SL24_XSTEP=1 -DFELUCCA_MICRO=1"; do
    $CC -O2 -w -I"$HGEN" -Ifirmware/src $f -o "$OUT/patterns_seq_test" tests/patterns_seq_test.c -lm
    run "pattern launches while playing (${f:-defaults}): end, bar, now, swing, motion, a take, undo, stopped, a live jump, song mode" "$OUT/patterns_seq_test"
done
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/patterns_ui_test" tests/patterns_ui_test.c -lm
run "the PATTERN layer (LFO held): launch end / bar / now, tracks, stop, STORE, COPY, CLEAR, DUPLICATE, scene launch (black 10), knobs, tiles, messages, the SAVE layer's changed mark, MISSING for a lost pattern" "$OUT/patterns_ui_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/patterns_ed_test" tests/patterns_ed_test.c -lm
run "editor cmds 83 / 84 (pattern read / write): chunks of 256 B, checked as the stage decodes, the log or the arena, refused when out of order / of another kind / too long / proj_tmp lent, clear" "$OUT/patterns_ed_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sl24_safety_test" tests/sl24_safety_test.c -lm
run "started on SLOOP 2.4's flash (FELUCCA_SL24_SAFE): its projects, autosave, FM6 bank, long samples never erased; shown as 2.4's" "$OUT/sl24_safety_test"
$CC -o "$OUT/stepx_test" tests/stepx_test.c
run "SLOOP 2.4 step extras (stepx.h): 2.4's FUN5 track tail byte for byte, fills, locks, the stored form round trip" "$OUT/stepx_test"
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/stepx_store_test" tests/stepx_store_test.c -lm
run "SLOOP 2.4 step extras kept with the sections and the autosave (FELUCCA_SL24_XSTEP): save, load, live store, stage, keys" "$OUT/stepx_store_test"
for x in "0 0 0" "1 0 0" "1 1 1" "0 1 1" "1 1 0" "1 0 1"; do
    set -- $x
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$1 -DFELUCCA_TRK_FILT=$2 -DFELUCCA_CHORDPLUS=$3 -o "$OUT/sl24_import_test$1$2$3" tests/sl24_import_test.c -lm
    run "a SLOOP 2.4 project imported (golden FUN5 from 2.4's own types), XSTEP=$1 TRK_FILT=$2 CHORDPLUS=$3: values, FILT/STRUM/VLEAD, engines, FM6, kits, extras; LOAD twice" "$OUT/sl24_import_test$1$2$3"
done
for x in "0 0 0" "1 0 0" "1 1 1" "0 1 1" "1 1 0" "1 0 1"; do   # (as the import's: both switches, each alone)
    set -- $x
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SL24_XSTEP=$1 -DFELUCCA_TRK_FILT=$2 -DFELUCCA_CHORDPLUS=$3 -o "$OUT/sl24_export_test$1$2$3" tests/sl24_export_test.c -lm
    run "ours exported for SLOOP 2.4 (SL24_EXPORT), XSTEP=$1 TRK_FILT=$2 CHORDPLUS=$3: golden FUN5 in and out byte for byte but the losses (FILT's slot); ours -> 2.4 -> ours, each loss listed; FM6 voices by PTCH or 2.4's bank; 2.4's settings; FX slots" "$OUT/sl24_export_test$1$2$3"
done
$CC -O2 -w -I"$HGEN" -Ifirmware/src -o "$OUT/sl24_edimport_test" tests/sl24_edimport_test.c -lm
run "a SLOOP 2.4 backup file's project through the editor (SL24_EDIMPORT, cmds 90 / 91): chunks and CRCs, the FM6 parts' bank patches, every refusal, 2.4's bank read out, nothing written" "$OUT/sl24_edimport_test"
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
for v in "16 0" "16 1" "8 0" "4 0" "4 1"; do          # (with SLOOP 2.4's step extras: FELUCCA_SL24_XSTEP, in the stream and in the editor's backup)
    set -- $v
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=$1 -DFELUCCA_MOTION=$2 -DFELUCCA_SL24_XSTEP=1 -o "$OUT/snapshots_test$1_${2}x" tests/snapshots_test.c -lm
    run "snapshots with the step extras (XSTEP=1), FELUCCA_SECTIONS=$1 MOTION=$2: the work's and each section's nudges, locks and fills saved, loaded, cut, exported; an older stream; the backup object XSTP" "$OUT/snapshots_test$1_${2}x"
done
for x in 0 1; do                                     # (PATTERNS: the tracks' pattern sources go with the work)
    $CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_SECTIONS=16 -DFELUCCA_MOTION=1 -DFELUCCA_PATTERNS=1 -DFELUCCA_SL24_XSTEP=$x -o "$OUT/snapshots_test_pat$x" tests/snapshots_test.c -lm
    run "snapshots with patterns (XSTEP=$x): the tracks' pattern sources saved with the work (before the first autosave, changed since the last), loaded back" "$OUT/snapshots_test_pat$x" pstate
done
run "snapshots across builds: with the step extras (XSTEP) -> without (the records skipped), without -> with (none), 16 sections -> 4" sh -c \
    "'$OUT/snapshots_test16_0x' write '$OUT/snxx.nor' && '$OUT/snapshots_test16_0' read '$OUT/snxx.nor' && '$OUT/snapshots_test4_0x' read '$OUT/snxx.nor' && '$OUT/snapshots_test16_0' write '$OUT/snx0.nor' && '$OUT/snapshots_test16_0x' read '$OUT/snx0.nor'"
# the editor's backup with every switch that adds a BK_OBJS entry (ed_backup.c) on at once, the builder's item bits as long
# as the target's: BK_LIST pages, none cut (2026-10: FXSL made the "mots" build's list too long for its one reply)
CFGB=$(python3 -c 'import sys; sys.path.insert(0, "tools/builder"); import registry as R; print((max(i.bit for i in R.ITEMS.values()) + 7) // 7)')
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DBK_ALL_ON=$CFGB "-DFELUCCA_CFG_BITS={[$((CFGB - 1))] = 0}" -DFELUCCA_SECTIONS=16 -DFELUCCA_MOTION=0 \
    -DFELUCCA_SL24_XSTEP=1 -DFELUCCA_UP_FM6=1 -DFELUCCA_NATIVE_BANKS=1 -DFELUCCA_ENG_CZ=1 -o "$OUT/snapshots_test_bkall" tests/snapshots_test.c -lm
run "backup, every object switch on (16 sections, song, XSTEP, FXSL, UP_FM6, CZ bank, snapshots): BK_LIST in pages, every object, none cut" "$OUT/snapshots_test_bkall"
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
$CC -O2 -w -I"$HGEN" -Ifirmware/src -DFELUCCA_ENG_CZ=1 -o "$OUT/nbank_test" tests/nbank_test.c -lm
run "native collections (NATIVE_BANKS): the CZ store (A/B, torn save), PRESETS rows, loads keep FX, cmd 66" "$OUT/nbank_test"
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
# (the menu needs Textual: tools/menuconfig makes this venv; BUILDER_VENV, ./tools/builder/venv, in a git worktree the main checkout's)
BPY="$(python3 -c 'import sys; sys.path.insert(0, "tools"); import deps; print(deps.venv_python())')"
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

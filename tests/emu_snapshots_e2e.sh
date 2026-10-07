#!/bin/sh
# Snapshots end to end on the emulator with its flash kept between runs (fm1-emulator feat/upstream-merge, play_check
# --state). Session 1: three sections A B C and a 3-part song made at the panel, saved to slot 1; everything changed
# (A B overwritten, D added, a 5-part song, other sounds), saved to slot 2; changed again, past the autosave, quit.
# Session 2: restart from the saved flash, load slot 1, save it to slot 3; load slot 2, save it to slot 4. verify.py then
# compares the streams in the flash: slot 3 must be slot 1 byte for byte (but the save counter and the globals a load
# never applies: G_SLOT, G_VIEW..), slot 4 slot 2. Needs a SNAPSHOTS 4 build. Not part of make test (the emulator):
#   tests/emu_snapshots_e2e.sh FIRMWARE.fwsc [OUT_DIR]      (FM1_EMU: the emulator checkout, ~/GitHub/fm1-emulator)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$(dirname "${1:?usage: emu_snapshots_e2e.sh FIRMWARE.fwsc [OUT_DIR]}")" && pwd)/$(basename "$1")
out=${2:-$(mktemp -d)}
mkdir -p "$out"
cd "$out"
echo "out: $(pwd)"
EMU=${FM1_EMU:-$HOME/GitHub/fm1-emulator}
cargo build -q --release --example play_check --manifest-path "$EMU/rust-emulator/Cargo.toml"
P=$EMU/rust-emulator/target/release/examples/play_check
export FM1_CPU_MHZ=96
T="hold:9 run:0.15 release run:0.4"                     # SAVE tapped
GLO="hold:7 run:0.15 release run:0.4"                  # GLO tapped (off the TRACKS page)
sec() { echo "hold:9 run:0.1 hold:9,$1 run:0.25 release run:0.3"; }      # SAVE + a white key: store a section
sec2() { echo "hold:9 run:0.1 hold:9,$1 run:0.25 hold:9 run:0.3 hold:9,$1 run:0.25 release run:0.3"; }   # over a used one
ARM2() { echo "turn:$1:1 run:0.3 turn:$1:1"; }          # a GO knob: arm, then act
SONG="hold:9 run:0.1 hold:9,40 run:0.25 release run:0.5"   # SAVE + the last white key: the SONG screen
mkdir -p out
rm -rf state
echo "== session 1"
$P --state ./state/ --fresh $FW run:3 png:out/s1-boot.png \
  turn:PRESETS:3 run:0.5 $(sec 21) run:2.5 \
  turn:PRESETS:2 run:0.5 $(sec 23) run:2.5 \
  turn:PRESETS:2 run:0.5 $(sec 25) run:2.5 png:out/s1-sections.png \
  $T turn:KNOB4:-1 run:0.3 turn:KNOB1:1 run:0.3 turn:KNOB3:2 run:0.3 png:out/s1-song.png $T run:1 \
  turn:PRESETS:5 run:0.5 \
  $GLO $T $T $T $T png:out/s1-page.png \
  $(ARM2 KNOB4) run:5 png:out/s1-saved1.png hold:8 run:0.15 release run:0.5 turn:KNOB2:-9 run:0.3 png:out/s1-home.png \
  turn:PRESETS:4 run:0.5 $GLO $T $T $T $T $T turn:KNOB1:-20 run:0.3 $(ARM2 KNOB4) run:2 png:out/s1-projA.png \
  hold:8 run:0.15 release run:0.5 turn:PRESETS:3 run:0.5 $GLO $T turn:KNOB1:1 run:0.3 $(ARM2 KNOB4) run:2 \
  hold:8 run:0.15 release run:0.5 \
  turn:PRESETS:2 run:0.5 $(sec 26) run:2.5 \
  $SONG turn:KNOB4:2 run:0.3 turn:KNOB2:1 run:0.3 png:out/s1-song2.png $T run:1 \
  hold:8 run:0.15 release run:0.5 turn:PRESETS:3 run:0.5 \
  $GLO $T $T turn:KNOB1:1 run:0.3 $(ARM2 KNOB4) run:5 png:out/s1-saved2.png \
  turn:PRESETS:2 run:30 png:out/s1-end.png | grep -v ": ok"
echo "== session 2"
$P --state ./state/ $FW run:3 png:out/s2-boot.png \
  $GLO $T $T $T $T png:out/s2-page.png \
  $(ARM2 KNOB2) run:6 png:out/s2-loaded1.png \
  $SONG png:out/s2-song1.png \
  $GLO $T turn:KNOB1:2 run:0.3 $(ARM2 KNOB4) run:5 \
  turn:KNOB1:-1 run:0.3 $(ARM2 KNOB2) run:6 png:out/s2-loaded2.png \
  $SONG png:out/s2-song2.png \
  $GLO $T turn:KNOB1:2 run:0.3 $(ARM2 KNOB4) run:5 png:out/s2-end.png | grep -v ": ok"
cmp -s out/s1-end.png out/s2-boot.png && echo "restart: the screen session 1 ended on" || echo "restart: a different screen"
python3 "$here/emu_snapshots_verify.py" state/*.nor

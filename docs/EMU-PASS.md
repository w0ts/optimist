# Emulator pass, Optimist 9a5636ca (2026-10-23)

Every synth engine's presets played through the emulated WL82 (fm1-emulator), the middle ground between the host
tests and the FM-1. Nothing was flashed to a device. The scripts are in tests/emu (sweep.py, grids.py, summarize.py,
farm_build.sh, the emulator patch); the run logs and images were not kept (2026-10-24: the branch `test/emu-pass` and
its worktree were removed after landing the scripts). The 0.2 release packages (docs/RELEASE-0.2-TEST.md) each pass
the `boot` check as well.

## Result in one paragraph

HEAD 9a5636ca (the LZB-packed boot stream of 032f02a4 / 23be06fb, `lzb_expand` in `fm1_cstart`) **boots in the
emulator**: the `synth` profile passes tests/emu's `boot` check 10/10 (no crash record, boot guard clear, 172.0
halves/s, 0 late halves, UI loop running, a note sounds), so the two reverts were not needed. All 26 engines were
played (six builds): 175 distinct presets, 426 plays counting the engines shared between builds, plus 75 ANALOG 2 filter plays: **no firmware fault, no silent preset, no
full-scale sample, no late half, no reset**. The only faults were two **emulator** gaps (instructions the vendor
compiler emits that the interpreter did not decode), in WAVE and PLAITS; patched locally in the emulator worktree
(`tests/emu/emulator-wide-ops.patch`), after which both engines played clean.

## Setup

| What | Where / value |
|---|---|
| Emulator | `/Users/mots/GitHub/fm1-emulator` at `9b65532` (feat/upstream-merge + feat/nor-suspend) with `tests/emu/emulator-wide-ops.patch` applied (a detached worktree of it, since removed) |
| Driver | tests/emu/drv (`tests/emu/session.py`), built against that worktree: `build/emu-drv` (stock), `build/emu-drv-msub` (+ e1fe), `build/emu-drv-fix2` (+ e1fe + e1d8 /3) |
| Emulated clock | 192 MHz (`FM1_CPU_MHZ=192`, the FM-1's clock; the emulator issues one instruction per cycle, so "us" below is guest time at that rate, not the chip's real timing) |
| Builds | gpu farm, `OPTIMIST_DUAL_PARK=1 python3 tools/optimist.py build --profile synth --set ...` (`tests/emu/farm_build.sh`) |
| Baseline | `firmwares/felucca-1.0.3.fwsc` boots and plays (play_check, 96 MHz: chord rms 0.0106, peak 0.0675) |

The six configurations (all on top of `config/profiles/synth.config`):

| Build | Engines (ENGINES[]) | --set | Flash / RAM |
|---|---|---|---|
| synth | ANALOG DIGITAL FM6 PHYS ACID JUNO | (none; DUAL parked) | 534,632 / 97,796 |
| a | + PHASE LOFI VOICE TRIO CZ NOISE DRONE (13), GEN_GRIDS | DUAL=0 DS_PLAITS=0 MASTER_COMP=0 ENG_PHASE/LOFI/FORMANT/TRIO/CZ/NOISE/DRONE=1 GEN_GRIDS=1 | 546,664 / 92,860 |
| b | ANALOG DIGITAL PHYS ACID JUNO WHEEL SAMPLE GRAIN PLAITS STRINGS VPS WSEQ PROPHET (13) | DUAL=0 DS_PLAITS=0 MASTER_COMP=0 ENG_DRAWBAR=1, FM6 + packs + UP_FM6 off, ENG_PLAITS/PROPHET/STRINGS/VPS/WSEQ/SAMPLE/GRAIN=1 SET_STRGS=1 | 550,544 / 92,492 |
| c | ANALOG DIGITAL TERRAIN VECTOR SID | FM6, PHYS, ACID, JUNO off; ENG_TERRAIN/VECTOR/SID=1 | 557,868 / 96,164 |
| d | ANALOG DIGITAL WAVE | FM6, PHYS, ACID, JUNO off; ENG_WAVE=1 DLY_LEN=16384 DLY_ARENA=32 | 540,892 / 96,132 |
| e | ANALOG DIGITAL SLICE PHYS ACID JUNO | DUAL=0 MASTER_COMP=0, FM6 off, ENG_SLICE=1 | 520,592 / 96,524 |

a and b first overflowed RAM (by 6,420 / 1,028 B; then 596 / 228 B with DUAL=0 DS_PLAITS=0; FX_DJF=0 freed
nothing); MASTER_COMP=0 made them fit, so builds a, b and e run without the master compressor and single core.
d first failed the builder check "the effects memory holds the delay's line: at least 64 KB": WAVE needs
DLY_LEN=16384 with DLY_ARENA=32.

## Boot

| Check (tests/emu/run.py --only boot, synth, 96 MHz) | Result |
|---|---|
| guest runs 3 s, no fault | PASS |
| fm1_crash | magic 0, count 0 |
| boot guard | normal boot, cleared after 34 s |
| audio | 172.00 halves/s, late 0, boots 1, max_us 1169 |
| screen / UI loop | 7,772 lit pixels, ui_frames 238 -> 2164 |
| a note (T1, white key 5) | RMS 0.0174 |

Every one of the six builds also booted to the mixer in its sweep (3 s of guest time before the first PRESETS
detent). The main tree's own `build/felucca.fwsc` (9a5636ca-modified, a DUAL build with CPU1 started) also booted to
the mixer and played a chord (play_check, 96 MHz).

## Engines

Method (tests/emu/sweep.py): fresh boot, track T1, PRESETS one detent at a time through the whole list; per preset a
note (white key 5, 0.7 s, 0.4 s tail) then a chord (white 1, 3, 5, 1.4 s, 0.4 s tail) recorded (24-bit WAV, both
channels); checks: not silent (peak >= 0.001), no sample at full scale, no sample-to-sample step > 0.5, no late
half, one boot and no crash record, no guest fault; max_us reset per preset. Engine and preset names are read from
`trk[0]` and `ENGINES[]` in the ELF. "Slowest half" is the longest audio render of one 256-frame half (5,804 us)
during that engine's presets, at the emulated 192 MHz.

| Engine | Build | Presets played | Chord peak min..max | Note peak min | Slowest half (us of 5804) | Flags | Faults |
|---|---|---|---|---|---|---|---|
| ANALOG | synth (+a b c d e) | 24 (808 BOOM, 808 DIRTY, 808 SLIDE, ...) | 0.033..0.221 | 0.024 | 1722 (29%) | none | 0 |
| DIGITAL | synth (+a b c d e) | 13 (FM BASS, RHODES, DX RHODES, ...) | 0.046..0.207 | 0.022 | 1635 (28%) | none | 0 |
| FM6 | synth, a | 3 (INIT VOICE, INIT WIDE, INIT MONO) | 0.029..0.099 | 0.027 | 1628 (28%) | none | 0 |
| PHYS | synth, a, b, e | 9 (BELL TREE, WOOD MRMBA, PLUCK, ...) | 0.028..0.139 | 0.015 | 3078 (53%) | none | 0 |
| ACID | synth, a, b, e | 4 (ACID LINE, ACID SQR, ACID RAGE, ...) | 0.040..0.071 | 0.044 | 1690 (29%) | none | 0 |
| JUNO | synth, a, b, e | 8 (JUNO STRNG, JUNO PWM PD, JUNO BRASS, ...) | 0.049..0.324 | 0.018 | 1973 (33%) | none | 0 |
| PHASE | a | 5 (CZ BASS, SOFT KEYS, CZ STRING, ...) | 0.062..0.170 | 0.023 | 1561 (26%) | none | 0 |
| LOFI | a | 3 (GB BASS, GAME LEAD, 8BIT ARP) | 0.037..0.067 | 0.037 | 1099 (18%) | none | 0 |
| VOICE | a | 4 (WOW BASS, CHOIR AAH, SOUL OOH, ...) | 0.046..0.058 | 0.022 | 1859 (32%) | none | 0 |
| TRIO | a | 8 (FAT BASS, SAW PAD, SYNC LEAD, ...) | 0.048..0.265 | 0.029 | 1734 (29%) | none | 0 |
| CZ | a | 8 (PD BASS, RESO SWEEP, GLASS BELL, ...) | 0.044..0.193 | 0.003 | 2059 (35%) | none | 0 |
| NOISE | a | 4 (WIND, RAIN, ARCADE, ...) | 0.164..0.295 | 0.061 | 1667 (28%) | none | 0 |
| DRONE | a | 9 (STILL AIR, OPEN FIFTH, SLOW CHAIN, ...) | 0.043..0.177 | 0.028 | 1993 (34%) | none | 0 |
| WHEEL | b | 5 (SOUL ORGAN, GOSPEL, JAZZ ORGAN, ...) | 0.040..0.063 | 0.017 | 1859 (32%) | none | 0 |
| SAMPLE | b | 1 (STRING STB) | 0.074 | 0.033 | 1518 (26%) | none | 0 |
| GRAIN | b | 1 (GRAIN PAD) | 0.167 | 0.052 | 2080 (35%) | none | 0 |
| PLAITS | b | 12 (FOLD BASS, TIDES LEAD, SERGE BELL, ...) | 0.036..0.302 | 0.026 | 2925 (50%) | none | 0 (after the e1d8 patch; 4 emulator faults before) |
| STRINGS | b | 5 (SOLINA, ENSEMBLE, CELLOS, ...) | 0.101..0.163 | 0.049 | 2601 (44%) | none | 0 |
| VPS | b | 6 (VPS LEAD, FORMANT PAD, VPS BASS, ...) | 0.128..0.323 | 0.137 | 1871 (32%) | none | 0 |
| WSEQ | b | 6 (WAVE STEPS, SWEEP PAD, OCTAVE BASS, ...) | 0.094..0.306 | 0.094 | 1734 (29%) | none | 0 |
| PROPHET | b | 7 (P5 BRASS, P5 STRINGS, P5 SYNC LEAD, ...) | 0.020..0.169 | 0.024 | 2545 (43%) | none | 0 |
| TERRAIN | c | 6 (RIPPLE PAD, CRYSTAL, BUMPY BASS, ...) | 0.035..0.347 | 0.036 | 1772 (30%) | none | 0 |
| VECTOR | c | 6 (VS PAD, VS BRASS, DIGI BELLS, ...) | 0.103..0.334 | 0.021 | 1674 (28%) | none | 0 |
| SID | c | 8 (PULSE LEAD, SAW BASS, SYNC LEAD, ...) | 0.038..0.234 | 0.025 | 1882 (32%) | none | 0 |
| WAVE | d | 6 (PPG PAD, WT SWEEP, SYNC BASS, ...) | 0.069..0.207 | 0.062 | 2250 (38%) | none | 0 (after the e1fe patch; 1 emulator fault before) |
| SLICE | e | 4 (BREAK 16, CHOP 8, REVERSE, ...) | 0.091..0.348 | 0.019 | 1720 (29%) | none | 0 |

The per-build table with every build's row is `build/emu-sweep/runs/summary.md`; every preset's line is in
`build/emu-sweep/runs/sweep-<build>.log`. Notes:

- The engines shared between builds gave the same levels in each build (within a few thousandths), so the
  configuration differences (DUAL parked / off, MASTER_COMP) did not change them.
- FM6: only its three INIT voices have their own `trk.preset`; the voice-pack voices after them all read preset 0,
  so the sweep walked past them without playing them (a gap of the method, not a result).
- SAMPLE and GRAIN played one preset each: PRESETS reached only one of each in build b (SET_STRGS the only set
  built); why the others are absent was not looked into.
- CZ's lowest note peak 0.003 and PROPHET's chord 0.020 are quiet but not silent; not listened to.
- The heaviest engines at 192 MHz: PHYS 53 %, PLAITS 50 %, STRINGS 44 %, PROPHET 43 % of a half (one T1 part, a
  3-note chord). This is the emulator's one-instruction-per-cycle time, not a measurement of the chip.
- Only "sounds, bounded, no fault" was checked; whether an engine sounds right was not.

## ANALOG 2 filter models

P_A2FTYP poked to model x 5 + mode on a playing ANALOG preset (sweep.py --a2 / --a2-at), synth build, all five modes
of SVF and the four models (A2_LADR A2_OTA A2_DIOD A2_MS20 = 1 in synth):

| Preset | Result |
|---|---|
| SYN BRASS (cutoff E4 45) | all 25 (model, mode) sound, levels differ per model (chord peak 0.023 DIOD BP .. 0.187 DIOD HP / NOTCH), no flag, slowest half 2033 us (DIOD) vs 1546 us (SVF) |
| REESE (cutoff E4 56) | all 25 sound, chord peak 0.018 (DIOD BP) .. 0.168 (DIOD NOTCH), no flag, slowest half 1308 us |
| 808 BOOM | LP12 / LP24 / NOTCH sound in every model; BP and HP are silent (peak < 0.0005) in every model, SVF included. The preset is a sub-bass; that its content lies far below the filter's band was not measured, so this is noted, not called a fault |

## GRIDS on DR (build a, GEN_GRIDS)

`tests/emu/grids.py build/emu-sweep/images/a/build build/emu-sweep/runs/grids-a --genout 5 --grids 3`: ALGORITHM +3 to DR, PLAY on
the empty pattern: silent (peak 0.0000). STEP, SELECT +5 = GEN OUT, KNOB1: GEN ON: the kit plays (peak 0.148, rms
0.028). Then SELECT -2 landed on DENSITY (4/5), not GRIDS (3/5) as intended; KNOB1 +20 set BD density 127: rms 0.044,
the screen shows the BD row filled. late 0, boots 1, no crash record. GRIDS' X/Y/CHAOS were not turned.

## Emulator gaps found (not firmware faults)

| Instruction | Where | Vendor objdump | Effect before the patch |
|---|---|---|---|
| `e1fe d060` | build d, 0x0203d9b6 (WAVE's wavetable code, inlined near wt_wanted / wt_sin30) | `r13_r12 -= r6 * r0 (s)` (64-bit signed multiply-subtract) | the guest faulted on the PRESETS detent that selects the first WAVE preset (row 37) |
| `e1d8 0365` | build b, 0x020417ca (`pl_spectrum`, PLAITS) | `r7_r6 >>>= r5` (64-bit arithmetic shift right by register) | the 4th, 5th and 6th PLAITS presets each faulted during the note (3 faults; the unpatched run was then stopped) |

`tests/emu/emulator-wide-ops.patch` (against fm1-emulator 9b65532, uncommitted in the emulator worktree) adds both:
e1fe to `MultiplyWide` (acc - product; C set as a subtraction's no-borrow by analogy with e1fc: NOT measured on an
FM-1) and e1d8 x=3 to `ShiftWideRegister` (a count of 64 or more fills with the sign: NOT measured). The emulator's
lib tests pass with it (122). An op scan of all six ELFs (vendor objdump on the farm, `examples/op_scan` of the
patched emulator) reports no undecoded instruction left. `>>>= r` also occurs once in every build in `qratio`
(0x02048246 in synth), which none of the unpatched sweeps reached.

## Repeat a run

```sh
cd /Users/mots/GitHub/optimist
export PATH=$HOME/.rustup/toolchains/stable-aarch64-apple-darwin/bin:$PATH     # cargo is not on PATH
export EMU_DIR=$PWD/emulator/rust-emulator
sh tests/emu/farm_build.sh synth a b c d e            # builds on gpufarm, images to build/emu-sweep/images/<b>/build/
python3 tests/emu/run.py build/emu-sweep/images/synth/build/felucca.fwsc --only boot --out build/emu-sweep/runs/synth-check --jobs 1
git -C emulator apply ../tests/emu/emulator-wide-ops.patch     # (in a worktree of fm1-emulator at 9b65532)
cargo build -q --release --manifest-path build/emu-drv/Cargo.toml --target-dir build/emu-drv-fix2
export EMU_CHECK_DRIVER=$PWD/build/emu-drv-fix2/release/emu-drv
python3 tests/emu/sweep.py build/emu-sweep/images/b/build build/emu-sweep/runs/sweep-b            # one build (~10 min)
python3 tests/emu/sweep.py build/emu-sweep/images/synth/build build/emu-sweep/runs/a2-brass --a2 --a2-only --a2-at 23
python3 tests/emu/grids.py build/emu-sweep/images/a/build build/emu-sweep/runs/grids-a --genout 5 --grids 3
python3 tests/emu/summarize.py build/emu-sweep/runs/sweep-synth=synth build/emu-sweep/runs/sweep-a=a ...
```

## Traps hit

- cargo is not on PATH: `export PATH=$HOME/.rustup/toolchains/stable-aarch64-apple-darwin/bin:$PATH` (Mac); on the farm
  `~/.cargo/bin`.
- The vendor objdump runs natively on the farm (`~/.jieli/jieli-linux-toolchains-*/common/bin/objdump -d
  -mattr=+fprev1`); no Docker needed there.
- tools/optimist.py build writes build/builder.config: farm_build.sh gives each configuration its own copy.
- RAM, not flash, is the limit when adding engines to synth (97,796 of 98,304 B); MASTER_COMP=0 is the lever that
  frees enough (DUAL=0 and DS_PLAITS=0 alone did not; FX_DJF=0 freed nothing measurable).
- WAVE needs a shorter delay: DLY_ARENA=32 alone is refused, DLY_LEN=16384 with it builds.
- tests/emu/panel.py's `page()` (PAGES[ui byte 0]) reads the wrong title on this UI ("ENV" on every STEP page); the
  GRIDS script navigates by SELECT counts measured from screenshots instead.
- PRESETS does not wrap at the end of its list. The runs here walked on to --max 400 detents (the extra ones only read the last preset again); sweep.py now stops when it reads the same preset four times in a row.
- A replayed fault: after a guest fault the sweep reboots and clicks back to the next preset; a fault on the click
  itself (WAVE) repeats on the replay, so the walk stops there (sweep.py now records that and stops).
- System PIL is broken (`from PIL import Image` fails): screenshots were read one by one.

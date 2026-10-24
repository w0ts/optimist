# Emulator pass: every preset through the emulated chip

The middle ground between the host tests and the FM-1: every synth engine's presets played through the emulated WL82
(fm1-emulator) by `tests/emu/sweep.py`, on a few builds that together hold all 26 engines. It checks "sounds, bounded,
no fault" only, not whether an engine sounds right, and the times are the emulator's one instruction a cycle, not the
chip's. The last full pass (2026-10-23, 9a5636ca): no firmware fault, no silent preset, no late half.

## The method

`sweep.py BUILD_DIR OUT_DIR [--a2]` (its docstring has the details): a fresh boot, track T1, PRESETS one detent at a
time through every engine's list; per preset a note and a chord recorded and checked (not silent, no full-scale sample,
no large step, no late half, no reset, no crash record, no guest fault). `--a2` then plays the ANALOG 2 filter models.
`tests/emu/grids.py` drives GRIDS on DR; `tests/emu/summarize.py` merges the runs into one table.

The builds: `tests/emu/farm_build.sh [NAME ...]` builds the configurations it names (`ref`, the reference profile, and
`a` to `e`, each a `--set` list on top of it) on the build host over ssh, and copies each package and ELF to
`build/emu-sweep/images/<NAME>/build/`. It ships HEAD: commit first.

## The emulator patch

The vendor compiler emits two instructions the emulator did not decode: `e1fe` (64-bit signed multiply-subtract, in
WAVE's wavetable code) and `e1d8` with x=3 (64-bit arithmetic shift right by register, in PLAITS and `qratio`).
`tests/emu/emulator-wide-ops.patch` (against fm1-emulator `9b65532`) adds both; their flag and edge behaviour is by
analogy, not measured on an FM-1. Without it WAVE and PLAITS presets fault in the emulator.

## Run it

```sh
export EMU_DIR=$PWD/emulator/fm1-emulator                    # a checkout with tests/emu/emulator-wide-ops.patch applied
sh tests/emu/farm_build.sh ref a b c d e
python3 tests/emu/run.py build/emu-sweep/images/ref/build/felucca.fwsc --only boot --out build/emu-sweep/runs/ref-check --jobs 1
cargo build -q --release --manifest-path build/emu-drv/Cargo.toml --target-dir build/emu-drv-fix
export EMU_CHECK_DRIVER=$PWD/build/emu-drv-fix/release/emu-drv
python3 tests/emu/sweep.py build/emu-sweep/images/b/build build/emu-sweep/runs/sweep-b      # one build, about 10 min
```

Traps:

- `tools/optimist.py build` writes `build/builder.config`, so one tree builds one configuration at a time:
  `farm_build.sh` gives each its own copy.
- RAM is the limit when engines are added; `MASTER_COMP=0` frees enough (the `a`, `b` and `e` sets use it). WAVE needs
  `DLY_LEN=16384` with `DLY_ARENA=32`.
- A fault on the PRESETS detent itself repeats on the replay after the reboot; `sweep.py` records it and stops.

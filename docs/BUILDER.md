# Firmware builder

Pick the engines, FX, drum kits, sample sets and features that go into your FM-1 firmware, with a live flash,
RAM, pool and RAMTEXT budget, then build it. Design and inventory: `docs/BUILDER-DESIGN.md` (sloop-merged);
the memory budget of the full integration: `docs/MEMORY-BUDGET.md`.

All numbers come from the emulator and host builds: there is no real FM-1 here yet.

## Run it

```sh
tools/menuconfig                          # the interactive menu (Textual; it makes a venv on the first run)
tools/menuconfig --profile drum-machine   # the menu, starting from a profile
tools/menuconfig --config my.config       # the menu on a saved configuration
```

Keys: `space` / `enter` toggle (a sized item: its next value), `/` search, `p` profiles, `s` save, `l` load,
`b` build, `e` / `c` expand / collapse all, `q` quit.

The bars show the estimate from the measured deltas (`tools/builder/costs.json`), red with "OVER by n" when a
region overflows; the message panel then names the biggest items of that region. `b` runs the real build
(about 10 s): the exact sizes replace the estimate, and when it fits the package is `build/felucca.fwsc`.

Without the menu (scripts, tests):

```sh
tools/menuconfig --list                               # the registry with the default values
tools/menuconfig --profile fm-va-studio --budget      # estimate
tools/menuconfig --profile fm-va-studio --set FX_PUNCH=0 --write my.config
tools/menuconfig --config my.config --build           # real build: exact sizes, the package if it fits
tools/menuconfig --profile everything-that-fits --fit # drop items (least loss first) until it fits
sh build.sh --config my.config                        # the same build, directly
```

The old environment switches still work and override the `.config` (`FELUCCA_ICONS=0 sh build.sh`).

## What is switchable

`tools/builder/registry.py` is the list; `tools/menuconfig --list` prints it. One level only: a top-level
item and its own options (FM6's modes, the sampled kits, the delay length...); an option is ignored while its
parent is off, and no option depends on another item.

| Group | Items |
|---|---|
| Synth engines | ANALOG 2, DIGITAL, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, FM6, SLICE (at least one) |
| FM6 options | MARK I / MODERN / OPL modes (at least one), black-key editor, DX7 SysEx, factory voices, user bank STORE |
| Drums | drum synth (all synthesised kits: one switch), sampled drums (one switch per kit), sound editor, user samples on lanes, user kits (at least one drum source) |
| Sample sets | PIANO, BASS, VIBES, HORNS, STRINGS, FLUTE, SCRATCH (PERC goes with the sampled kits) |
| FX | DIST, chorus, delay (length), reverb, SLICER (capture), PUNCH (ring), DJ filter, DUST, DUCK |
| MIDI & USB | USB port: CDC console / USB audio (EXPERIMENTAL) / MIDI only; TRS MIDI IN; MIDI clock; MIDI expression |
| UI | boot logo, parameter icons, VIEW ALL overview |
| System | OTA updates, idle, asm kernels (SIMD: EXPERIMENTAL) |
| Experimental | dual core |

Warnings the menu gives: FM6 without its editor and without SysEx is preset-only; sample sets without SAMPLE
or GRAIN play nowhere; OTA off removes the update path; experimental items are emulator-tested only.

### Where an item came from

Items ported from another project carry their provenance (project, author, licence, commit) in the registry;
the menu shows it in the details panel. Items from X0X (`charlesvestal/fm1-x0x`, GPL-3.0-only, by Charles
Vestal) show a NOTICE when selected. What X0X uses only by its author's permission (its break player,
`dsp/breaks*`: no licence) is never offered (`registry.FORBIDDEN`; a `.config` naming it is refused).

## How a switch works

- `firmware/src/registry.h`: the engines as an X-macro list with their permanent UIDs (FUN7 numbers) and
  fallbacks, and the defaults of every switch. SLOOP is one translation unit: an item left out is a constant
  `0` (`ENG_IS()`, `if (FELUCCA_FX_X)`), so its code, tables, RAM and pool go. Pages and cells of a missing FX
  disappear (`params.c` `page_shown`, `cell_built`).
- `tools/builder/configure.py` turns a `.config` into `build/gen/felucca_config.h` (included first by
  `tools/build.py`) and the generators' environment (sample sets, PERC, SLICE's BREAK).
- Stable IDs: projects, user presets and the drum lanes store engine, kit and sample-set UIDs. A part whose
  engine this build leaves out plays the fallback (FM6 -> DIGITAL -> ANALOG, GRAIN -> SAMPLE, the rest ->
  ANALOG) with its defaults, shows `FM6*` on HOME, and keeps the original engine, preset, EDIT values and FM6
  voice: they are written back on save as long as you leave that part's sound alone. A full build then plays
  it as before (`tests/builder_rt_test.c`). A user preset of a missing engine stays in the bank, not loadable.
  A kit not built plays the other source's first kit; a sample set left out keeps its number (empty).
- The firmware reports what it is: editor protocol v6, INFO adds each slot's engine UID, `BUILD` (43) the
  configuration's name, hash and one bit per registry item.

## Budget

`tools/builder/measure_costs.py` builds every item at every non-default value (measurement builds link past the
slot) and writes `costs.json`: the default build's sizes and each item's delta. The deltas add up within about
0.5 %; the menu's build gives the exact figure. Re-run it after a merge.

## Verification

`tools/builder/verify.py [--random N] [--emu]`: every profile and N random configurations (fitted by the
estimate) build, link and fit; no symbol of an item left out stays in the ELF; `tests/regress.c` built with the
configuration renders every present preset bit-identically to the full build's goldens; with `--emu` the
emulator boots each image and the audio stays silent (rms 0). Host tests: `tests/run_tests.sh` (includes the
full -> reduced -> full project round trip).

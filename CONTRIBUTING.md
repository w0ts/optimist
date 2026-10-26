# Contributing

Optimist is firmware for the M-VAVE FM-1, written in C (GPL-3.0-only). There is no plugin API and no scripting: a new
engine, effect, sequencer tool or UI change is C in `firmware/src/`, built into the image. What keeps that manageable
is the **builder**: almost every feature is an *item* with its own switch, off or on per build, so a new piece costs
nothing to anyone who leaves it out.

The tree carries three worked examples, each a builder item that is off in every profile, written as templates (every
line a new piece of that kind must have is marked `[INSERT]`, `[ENGINE]` or `[GEN]`):

| Example | Builder item (bit) | What it is | Files |
|---|---|---|---|
| EXGN | `EX_GAIN` (421) | an insert effect: a gain in dB on the tracks' insert slots | `firmware/src/fx/insfx/exgain.c` |
| SQUARE | `EX_SQUARE` (422) | a synth engine: a pulse, its sub, a detuned pulse, noise, a filter | `firmware/src/engines/square/eng_square.c` |
| WALK, UPDN | `EX_NOTEGEN` (423) | two note generators on STEP's GEN page that choose the notes | `firmware/src/seq/gen_ex.c` |

`grep -rn "EX_GAIN\|EX_SQUARE\|EX_NOTEGEN" firmware tools tests docs` lists every place each is registered; the
recipes below walk through them. `tests/examples_test.c` is their test.

## Where things live

| Path | What |
|---|---|
| `firmware/src/felucca.c` | the one translation unit: it includes every other source in order |
| `firmware/src/core/` | the model: `core.h` (track values `P_*`, globals `G_*`), `params.c` (every value's descriptor, the pages), `voice.c` (voices, envelopes, LFOs), `model.c` (presets, the PRESETS list), `registry.h` (every switch's default, the engine UIDs) |
| `firmware/src/engines/` | the synth engines, one folder each; `engines.c` includes them and builds `ENGINES[]` |
| `firmware/src/dsp/` | the shared fixed-point blocks: oscillators, the SVF, soft clips (`dsp.c`) |
| `firmware/src/fx/` | the effects: `fx.c` (the mix, the sends), `fx_slots.c` (the insert slots, type ids `FXT_*`, their stored form), `insfx/` (the insert types) |
| `firmware/src/seq/` | the sequencer (`seq.c`), the automation store, the note generators (`gen.c`) |
| `firmware/src/drums/` | the drum track: kits, lanes, the drum synths |
| `firmware/src/ui/optimist/` | the user interface, `op_*.c` (below) |
| `firmware/src/io/`, `storage/`, `system/`, `ble/`, `bt/` | USB / MIDI / the web editor's protocol, flash storage, boot and the CPU guard, Bluetooth |
| `firmware/hal/` | the hardware: the only code that touches registers |
| `tools/optimist.py` | the entry point: setup, build, package, test, emulator, web site |
| `tools/builder/` | the builder: `registry.py` (every item), `core.py` (its rules), `costs.json` (measured sizes) |
| `tests/` | host tests: C programs built from the firmware's own sources (`hostsim.c`), run by `tests/run_tests.sh` |
| `web/` | the site: the editor (`editor.html`), the web builder (`builder/`, its data generated), the simulator |
| `docs/` | `BUILDER.md` (every builder item), `guide/` (the user guide), the design notes; `BUILDING.md` is at the root |
| `config/profiles/` | the shipped build profiles (the maintainer's: see "Pull requests") |

## Set up, build, test, try

BUILDING.md has the details (Docker on macOS and Windows, natively on Linux x86-64). In short:

```
python3 tools/optimist.py setup                 # what the build needs: the toolchain or its Docker image, the SDK files
python3 tools/optimist.py build                 # the default profile -> build/felucca.fwsc (make build)
python3 tools/optimist.py build --set EX_SQUARE=1 --measure   # one item changed; --measure links past the app slot
python3 tools/optimist.py test                  # the builds the tests read, then tests/run_tests.sh (make test)
python3 tools/optimist.py test --python         # the Python tests only: no toolchain, no C compiler
python3 tools/optimist.py emu                   # a build in the emulator (make emu; needs Rust)
python3 tools/optimist.py web --sim --no-open   # the site on 127.0.0.1:8766 with a local simulator (Emscripten)
make builder                                    # the builder menu
```

The default profile, `synths_USBaudio_BTaudio_BTmidi`, fills the flash almost to the byte: a build with any example
added to it does not fit the app slot (hence `--measure` above). To hear one, build a profile that leaves something out
(`make builder`, untick an engine), or try it in the emulator or the web simulator. Most work needs no FM-1.

`tests/run_tests.sh` runs everything (a few minutes, in parallel). While working on one piece, build its test alone:

```
python3 tools/build.py --host-headers           # build/gen-host: the generated tables the host tests compile with
mkdir -p build/host
cc -O2 -w -Ibuild/gen-host -Ifirmware/src -Ifirmware/hal -DFELUCCA_EX_GAIN=1 -DFELUCCA_EX_SQUARE=1 \
   -DFELUCCA_NOTEGEN=1 -DFELUCCA_EX_NOTEGEN=1 -DFELUCCA_MOTION=1 -o build/host/examples_test tests/examples_test.c -lm
build/host/examples_test
```

A host test is a C file that includes `tests/hostsim.c` (the firmware's sources, no hardware) and sets the switches it
needs with `-D`. It calls the firmware's functions directly: `trk_note_on` and `mix_block` to render, `ins_put` and
`ins_part` to run an insert, `gen_eff` and `gen_write` for the generators.

## Ids are permanent

Projects, presets and the BUILD report store numbers, so these are never reused or renumbered:

- the **builder bit** of an item (`registry.py`; the retired ones are listed in `registry.RETIRED`);
- an **engine UID** (`registry.h` `ENGINE_LIST`), an **FX type id** (`fx_slots.c` `FXT_*`), a **generator type**
  (`gen.c` `GT_*`), a **TLV id** in a stored record, a **motion id** (`auto.c` `MOT_TAIL` / `MOT_TAIL2`).

Say in an issue what you are adding before you start, so two branches do not take the same numbers. The examples are a
special case: so that every other build stays byte for byte the same, their ids (FX type 29, engine UID 27, generator
types 4 and 5, motion id `AUTO_TAIL2 + 9`) exist only in a build that has them. A real piece added after them takes
the next numbers in every build, and the example's number moves up (it is never stored outside a test).

## Add an insert effect (the example: EXGN)

An insert type runs on a block of samples in a track's insert slot (INSERTS > S1 / S2): one amount a type, 0 is off.
`fx/insfx/exgain.c` is the whole effect: a state an instance, one run function. Then register it:

| Where | What (search for `EX_GAIN` / `EXGN`) |
|---|---|
| `firmware/src/core/registry.h` | `FELUCCA_EX_GAIN` and its default, 0 |
| `firmware/src/core/core.h` | the amount's track value `P_TEXGN` (an insert's amount is not part of a sound or a preset) |
| `firmware/src/core/params.c` | its descriptor: label (the type's name in the TYPE list), format, range, default |
| `firmware/src/fx/fx_slots.c` | the type id `FXT_EXGN`; its bit in `FXT_INSERT` and `FXT_BUILT`; its place in `FXT_ALPHA` (the TYPE list, A to Z); its amount in `FXT_AMT`; an entry in each per-type table (`LID`, `FXT_BUS`); which owners may hold it (`RT_OK`, `MST_OK`, `UT_OK`: EXGN is on the tracks and the drum bus only); its stored form in `fxs_tlv`, `fxs_untlv`, `fxs_untlv_none` and the size `FXT_TLV_SUM`; `INS_NLIST` |
| `firmware/src/fx/insfx/insfx.c` | `#include "exgain.c"`, and a `case FXT_EXGN` in `ifx_w`, `ifx_run`, `ifx_quiet_part` |
| `firmware/src/io/editor/ed_fxs.c` | its entry in `ED_FX_LID` |
| `firmware/src/seq/auto.c` | its motion / lock id in `MOT_TAIL2` |
| `tools/param_help.json` | its help line in each context its knob shows: `FX`, `INSERTS`, `INSERT` |
| `tools/doc_coverage.py` | `FXT_EXGN` in `FX_NAMES` (the type must be named in a user document) |
| `web/test_web.mjs`, `web/editor.html` | the test pins the `FXT_*` enum; a real type also gets its name in the editor's `FXT_NAMES` (the example does not: the web editor does not list it) |

The rules: integers only; no divide by a variable in the audio path (`tools/div_audit.py`, or a line in
`tools/div_audit.txt` saying why the divisor is never 0); at amount 0 and at rest, the block comes out bit for bit;
a change of the amount never clicks. A heavier effect also gets its CPU cost in the guard's model
(`tools/builder/cpu_costs.py`, `firmware/src/system/cpuguard.c`) and a job in `tests/regress.c`; the shapers in
`insfx.c` (`ifx_drive`) and `tapespit.c` are the larger examples, `tests/insfx_test.c` their test.

## Add a synth engine (the example: SQUARE)

An engine renders voices: `note_on` resets a voice, `render` adds `n` samples to `out` at the voice's pitch (`m->inc`)
times the track's envelope (`amp_at`). `engines/square/eng_square.c` is the whole engine: its eight EDIT values, its
presets, its `engine_t`. Then register it:

| Where | What (search for `EX_SQUARE` / `SQUARE`) |
|---|---|
| `firmware/src/core/registry.h` | the switch, and the engine's line `X(uid, NAME, fallback, "NAME")` of `ENGINE_LIST` with `ENG_UID_<NAME>`, `ENG_UID_N` (the fallback is the engine a build without it plays) |
| `firmware/src/engines/engines.c` | `#include` the file under its switch; `ENG_IX_<NAME>` for the tests |
| `firmware/src/core/model.c` | each preset in `BANK`, the PRESETS list, by kind (`tests/preset_cover_test.c` checks) |
| `firmware/src/core/env1_cards.h` | `E1C_<NAME>`: the four ENV 1 MOD cards |
| `firmware/src/core/macro.c`, `firmware/src/ui/optimist/op_effval.c` | the engine's entry in `MAC_BRIGHT` and `EFFV_DEST` (which EDIT value is its cutoff, its shape) |
| `firmware/src/ui/colors.c`, `tools/colors.json` | its colour (a real engine: in `colors.json`, and `COLORS` in `web/editor.html`, which a test holds equal) |
| `firmware/src/ui/optimist/op_names.c` | its 4-letter form in `NM_CELL` when the name is longer |
| `tools/param_help.json` | an `"eng"` group: a help line for every EDIT value |
| `tools/builder/cpu_costs.py`, `tests/cpu_baseline.txt` | its CPU cost, through `tests/regress.c` (a real engine; SQUARE is left out: the guard counts it as an ANALOG voice) |

Read `engines/clipper/eng_strings.c` or `engines/drone/eng_drone.c` next: small engines with a part state and more.
`tests/drone_test.c` is the fuller engine test (a golden render in `tests/golden_<name>.txt`).

## Add a note generator (the examples: WALK and UPDN)

A generator is a step function: for step `i` of a pattern of `len` steps it says whether the step is a hit and may
set its level, ratchet and (with the example's addition, `gen_ev_t.note`) its note. It must be a pure function of its
three values, the step and the slot, so a loop replays it exactly and WRITE writes what was heard. STEP's GEN and GEN
OUT pages, the stored form and WRITE are the framework's (`seq/gen.c`); a generator adds:

| Where | What (search for `EX_NOTEGEN` / `GT_EX`) |
|---|---|
| `firmware/src/seq/gen_ex.c` | the fit and step functions |
| `firmware/src/seq/gen.c` | the type number in the `GT_*` enum (`GT_N` at most 8: three bits in the stored form), its entry in `GEN_TYPES` (name, three values: label, min, max, default, how shown), `gen_ntypes` |
| `firmware/src/core/registry.h` | the switch (an option of `NOTEGEN` in the builder) |
| `tools/param_help.json` | a line for each value label in the `GEN` group (one text per label: a label shared with another type shares its line) |

## Every item: the builder side

| Where | What |
|---|---|
| `tools/builder/registry.py` | `_add(key, flag, label, group, bit, default=0, desc=..., parent=..., tested=..., experimental=..., symbols=...)`: the next free bit, never a retired one; `desc` is the menu's text, at least 40 characters; `symbols` are functions that must be gone from the image when the item is off (`tools/builder/verify.py`); a port of someone's code gets `provenance=` and a `notice` |
| `tools/builder/core.py` | a rule, if the item needs or excludes another (`validate`) |
| `docs/BUILDER.md` | the item's row, its `desc` word for word (`tests/builder_test.py` compares) |
| `web/builder/data/` | regenerated: `python3 tools/export_builder_web.py` |
| `tools/size_fns.py` | the new source in one list: `AUDIO_FILES` for what the sound reaches, `SIZE_FILES` for main-loop code (`--check`) |
| `tests/run_tests.sh` | the test's unit, with its switches |

Before a pull request: `python3 tests/builder_test.py`, `python3 tools/doc_coverage.py`, `python3 tools/size_fns.py
--check`, `python3 tools/div_audit.py`, `python3 tools/gen_param_help.py --check` (the last one fails until
`python3 tools/build.py --host-headers` has brought `web/editor.html`'s copy of the help lines up to date), then the
whole of `python3 tools/optimist.py test`.

## Costs and fitting

The flash, RAM, pool and RAM-code cost of every item is measured, not guessed: `tools/builder/costs.json`, one
measurement build per item value, read by the builder's budget, the menu and the web builder. CI checks it twice:

- the `build` workflow's last step, `python tools/builder/measure_costs.py --missing --check`, fails when an item (or
  a value of it) has no measured cost, or when a check configuration is missing;
- `tests/builder_test.py` (part of `optimist.py test`) checks the same ("every registry and backport item has a
  measured cost", "a real build of every check configuration"), and that the default profile fits.

A new item fails both until it is measured. Adding any item also changes the configuration hash of every build (the
hash covers every item's value), so the measured check configurations, keyed by that hash, are measured again too.
`make costs` (`measure_costs.py --missing`) does exactly what is missing; it needs a working build (Docker on a Mac:
about 20 s a build) and then `costs.json` and `web/builder/data/` are committed together. If you cannot run it, say
so in the pull request: a maintainer measures on the build farm (`make costs-all` re-measures everything, ~440 builds)
and the two checks above stay red until then. Write the cost you measured yourself in the item's `desc` only from a
real build (`build --measure` with and without it). The examples, measured on the default profile at 35cd9415
(`--measure`): EX_GAIN flash +460 B, RAM +304 B (its run function is in RAM, as every insert's), pool +8 B, RAM code
-16 B; EX_SQUARE flash +1,660 B; EX_NOTEGEN flash +736 B.

## The UI, the keys, chords

The interface is `firmware/src/ui/optimist/` (docs/UI-OPTIMIST-DESIGN.md is its design): `optimist.c` the screens'
skeleton, `op_screens.c` and `op_cells.c` the rows and cards, `op_input.c` the buttons, `op_layers.c` and
`op_laydraw.c` the held layers (a page button held: the keys and knobs change job), `op_keys.c` the keys' lights,
`op_step.c` STEP, `op_tools.c` the TOOLS and GEN pages, `op_help.c` the help line. The keyboard's note mapping (scale,
WHITE / ALL / SNAP keys, the octave) is `seq.c` `scale_map` and `kb_pitch`; the chord modes and CHORD+ (the black keys
change the chord: builder item `CHORDPLUS`) are in `seq.c` too; the arpeggiator and note repeat as well. A page or a
value is a descriptor in `params.c` (`PAGES`, the `P_*` table); most screens are built from them, so a new value often
needs no drawing code. `tests/ui_optimist_test.c` walks every screen (names, colours, help lines, the layout); a UI
change runs it. Again: no plugin or layout file, you change the C.

## Pull requests

1. Fork `w0ts/optimist` on GitHub, branch from `main`, one feature per pull request.
2. Keep it behind a builder item, off by default, unless it is a fix. Change no profile in `config/profiles/`: they
   are the maintainer's (a profile you want for yourself lives in the git-ignored `config/my-profiles/`).
3. `python3 tools/optimist.py test` green (or `make test`), less the cost checks if you could not measure; say which.
4. Commit messages as the history has them: `feat: ...`, `fix: ...`, `docs: ...`, `test: ...` (a scope is fine:
   `feat(drums): ...`), the first line saying what changed for a user, the body the details: the builder bit, the ids
   you took, the measured cost, what you tested on the host, in the emulator and on a device.
5. The code is GPL-3.0-only: by sending it you agree to that (LICENSING.md, "Contributions"). Ported code keeps its
   author's copyright and licence header, gets its row in LICENSING.md and the item's `provenance`; only
   GPL-3.0-compatible sources.
6. No error silenced to make a check pass (an ignore, a skip, a looser setting): fix the cause, or ask.

## Where to ask

The issues of [github.com/w0ts/optimist](https://github.com/w0ts/optimist/issues): to propose a piece before writing
it (and claim its ids), to ask how something works, to report a bug (the build, where it happened: simulator,
emulator or FM-1, and how to reproduce it). The repository's only issue form is the cloud build request; open a blank
issue for anything else.

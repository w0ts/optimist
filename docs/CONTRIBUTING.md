# Contributing: adding an engine or an effect

Optimist is one translation unit built from a registry of switchable items. An engine or an effect is a *builder
item*: one flag the builder turns on or off, so its code, tables and RAM are in a build only when it is chosen.
This page lists what a new one touches. For the model behind it see docs/BUILDER.md ("How a switch works"), for
building and testing BUILDING.md.

The two worked examples are real commits, so read their diffs next to this page:

- an **engine**: `60252ddd`, DRONE (engine 16, builder bit 358)
- an **effect** (an insert type): `2a33fe44`, the shaper inserts SFLD, WRAP, RECT, HARM, RING, SUB, SLEW

`git show --stat <hash>` lists every file; most of them are docs, tests and generated data.

## Before you start

1. Build and run the tests once as they are (`python tools/optimist.py test`, BUILDING.md "Tests"). Try the firmware in
   the web simulator (the Simulator tab) first: no hardware is needed for most work.
2. Tell us what you are adding (a GitHub issue) so two people do not claim the same **builder bit** and the same
   **engine UID**. Both are permanent: projects and presets store them, so they are never reused or renumbered
   (`registry.RETIRED` keeps the retired ones).
3. Flash is small. State the flash and RAM cost you measured, as the descriptions in `tools/builder/registry.py` do.

## An engine

Put the code in `firmware/src/engines/<name>/eng_<name>.c`. It describes its parameters (`param_desc_t`, the page
layout, the presets) and renders voices; `engines/drone/eng_drone.c` is a compact, commented example with a header that
documents every page and value.

Then register it. Each line below is something DRONE's commit changed:

| Where | What |
|---|---|
| `firmware/src/core/registry.h` | the `FELUCCA_ENG_<NAME>` default (`#ifndef` / `#define ... 0`), the engine's UID in the `X(...)` list, `ENG_UID_N`, `ENG_UID_<NAME>` and its fallback (the engine that plays when a build leaves yours out) |
| `firmware/src/engines/engines.c` | `#if FELUCCA_ENG_<NAME>` and `#include "<name>/eng_<name>.c"` |
| `tools/builder/registry.py` | `_add("ENG_<NAME>", "FELUCCA_ENG_<NAME>", "<label>", E, <bit>, default=0, desc=...)`: the builder's checkbox, its bit and its help text; the `symbols=` table names the functions the size measurement looks for |
| `tools/param_help.json` | the knob's help line for every value (also the web editor's tooltips) |
| `firmware/src/ui/optimist/op_names.c`, `tools/colors.json` | the engine's short name (`"DRONE\0DRON\0"`) and its colour |
| `docs/BUILDER.md`, `docs/guide/sound.md`, `README.md` | the user-facing description |

Test it:

- a host test of its own (`tests/<name>_test.c`, see `tests/drone_test.c`): every preset is audible and bounded, voices
  are freed, a golden render in `tests/golden_<name>.txt`;
- the regression suite (`tests/regress.c`) builds with your flag, and `tests/run_tests.sh` lists your test (copy
  DRONE's two jobs). After an *intended* sound change, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes;
- the cost files: `tests/cpu_baseline.txt`, `tools/builder/costs.json`. The size model is measured on a fast Linux
  host (`make costs` measures only what is missing, BUILDING.md "The costs"); say in your pull request if you could not
  run it and a maintainer will.

## An effect

Insert effects live in `firmware/src/fx/insfx/insfx.c`. A new insert type is a builder item like an engine, plus the
places that know the list of types:

| Where | What |
|---|---|
| `firmware/src/fx/insfx/insfx.c` | the effect's process function (`ifx_<name>`), its state, its place in the type table |
| `firmware/src/core/registry.h` | `FELUCCA_INS_<NAME>`, default `0` |
| `tools/builder/registry.py` | `_add("INS_<NAME>", ...)` with its bit, description and `symbols=` |
| `firmware/src/fx/fx.c`, `fx_slots.c`, `core/params.c` | the type's name, its slot state size, its page |
| `tools/param_help.json` | help text |
| `tests/insfx_test.c`, `tests/regress.c` | its behaviour: bounded, silent on silence, stable at the extremes |
| `tools/builder/cpu_costs.py`, `firmware/src/system/cpuguard.c` | its CPU cost, for the guard that keeps the audio from running late |

The other FX (reverb, delay, the returns) are larger items under the same scheme (`FX_*` in `registry.py`); read the
"FX" tables in docs/BUILDER.md first.

## Rules the tests enforce

- A build without your item must still compile, load a project that uses it, and say what is missing
  (`MISSING_WARN`, `tests/missing_test.c`): that is why the fallback and the permanent UID matter.
- No hardcoded `0`/`1` for your flag outside the registry: `if (FELUCCA_ENG_<NAME>)` or `#if`.
- No unguarded divisions in the audio path: `tools/div_audit.py` lists them (`tools/div_audit.txt` is the accepted list).
- Say in the pull request what you tested only in the host tests and what on a device.
- Do not silence a failing check with an ignore, a skip or a looser config; fix the cause.

## Open the pull request

Run `python tools/optimist.py test` (or `make gate` before a push). Commit as `feat: <what>` and put the flash and RAM
cost, the builder bit and the engine UID in the message.

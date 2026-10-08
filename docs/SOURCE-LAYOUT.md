# Source layout: firmware/src by domain

Step 0 of the pluggable-UI plan (docs/UI-FEASIBILITY.md section 4.1). `firmware/src` holds about 150 files in one folder.
This document gives each file a folder by domain: one folder per engine, per FX, per drum source, and one each for
the sequencer, storage, the UI, I/O and the system. A feature becomes **a folder plus a builder switch**.

The move is done by a script, `tools/reorg.py`. It can be re-run on the latest `optimist` in minutes, so branches in
flight are not blocked while it is planned.

**What does not change**

- **File names.** Only folders change. A bare reference such as `seq.c:968`, `ui_draw.c` or `project.c:867` in a doc, a
  comment or a commit message stays right.
- **The unity build.** `firmware/src/felucca.c` stays where it is and is still the one translation unit. It includes
  the same files, in the same order, under the same `#if` switches.
- **The firmware itself.** For all 5 profiles, the `.bin` and loader `ota.bin` are byte-identical before and after the
  move (section 6). The `.fwsc` is identical too.
- **`firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`.** They are already folders of their own.
- **No new build system and no runtime plug-ins.**

---

## 1. Target tree

```
firmware/src/
  felucca.c                 the unity build: includes everything below, in order (unchanged)
  core/                     the model and the audio path every build has
    core.h registry.h backports.h backports23.h backports24.h backports24seq.h
    params.c bp_set.c       parameter descriptors, formatting, pages (UI-FEASIBILITY step 4 splits the pages out)
    voice.c audio.c macro.c
  seq/                      sequencer, arranger, motion, undo
    seq.c qnt_seq.c chance.c motion.c undo.c arranger.c arranger.h arranger_scene.c
    seq24.c stepx.h (SLOOP 2.4 step extras) seq_midi.c (note-out sets)
  storage/                  flash objects, projects, user presets, what a load misses
    storage.c project.c upreset.c drum_store.c motion_flash.c motion_proj.c miss.c
    settings_word.c stepx_proj.c nbank.c (the FM6 / CZ native collections)
    sections/               song sections: sections.c sec_codec.c sec_log.c stepx_log.c
    sl24/                   SLOOP 2.4 projects: sl24_import.c sl24_export.c sl24_guard.c
    snapshots/              whole-state snapshots: snapshots.c snap_store.c
  dsp/                      shared DSP building blocks: dsp.c dsp_common.h dsp_float.h x0x_param.h
  engines/                  engines.c (the engine table) preset_trim.h (generated: tools/level_presets.py)
    analog/  eng_analog.c eng_analog2.c       digital/ eng_digital.c      phase/   eng_phase.c
    lofi/    eng_lofi.c                       sample/  eng_sample.c       formant/ eng_formant.c
    trio/    eng_trio.c                       drawbar/ eng_drawbar.c      grain/   eng_grain.c
    super/   eng_super.c                      slice/   eng_slice.c
    fm6/     eng_fm6.c eng_fm6_rom.h fm6_store.c
    cz/      eng_cz.c cz_native.c
    phys/    eng_phys.c phys_dsp.c phys_symp.c
    acid/    eng_acid.c acid_dsp.c bass303.c bass303.h   (was firmware/src/acid/)
  fx/                       fx.c (the buses: DIST, chorus, delay, reverb)
    reverb/ rev_type.c rev_math.c reverb_alt.c reverb_airwin.c   master_comp/ master_comp.c master_comp.h
    slicer/ slicer.c    spring/ spring.c    bassplus/ bassplus.c    punch/ punch.c
  drums/                    drums.c (the GM part, the sampled kits) drum_edit.c drum_sends.c drum_kits.c
    synth/ drum_synth.c
    x0x/   drum_x0x.c x0x_drums.c drum808.[ch] drum909.[ch] drum909_dsp.h   (was firmware/src/x0x/)
  display/                  what every UI and the core screens (OTA, recovery, UBOOT) draw with
    lcd.c lcd_dirty.c gfx.c
  ui/                       shared by any UI: the panel's map and lights, knob acceleration, the boot screen
    panel.c lights.c knob_accel.h splash.c meters.c
    sloop/                  the SLOOP UI (a second UI goes beside it: ui/min/, UI-FEASIBILITY step 5)
      ui.c ui_input.c ui_layers.c ui_draw.c ui_menu.c ui_overview.c ui_song.c ui_studio.c
      ui_drums.c ui_fm6.c macro_ui.c keylit.c bright.c icons.c param_help.c
      ui_colors.c ui_vis.c ui_drumstep.c
  io/                       console.c
    usb/    usb.c usb_audio.c usb_audio_stream.c usb_audio_desc.h
    midi/   midi_control.c midi_uart.c clock_sync.c
    editor/ editor.c ed_backup.c ed_drums.c ed_dsend.c ed_dsrc.c ed_pages.c ed_snap.c ed_status.c
                  ed_macro.c ed_sl24.c ed_steps.c ed_stepx.c ed_sync9.c ed_user.c ed_cz.c
  system/                   boot, main loop, guards, update, the second core, measurements
    main.c libc.c bootguard.h cpuguard.c cpuguard.h cpuguard_costs.h ota.c recovery.c dual.c bench.c simd_probe.c
```

**Choices made, and why**

- **`seq/` and `storage/` are folders of their own, not part of `core/`.** They are large: seq.c alone is 1,750
  lines, and storage plus sections plus snapshots is about 4,000. They also have clear seams. `core/` keeps only what
  every other folder needs: the types, the registry, params, the voices and the audio ISR.
- **`ui/sloop/` now, not `ui/` now and `ui/sloop/` later.** UI-FEASIBILITY step 5 puts the SLOOP UI in `ui/sloop/`.
  Moving it once saves every branch a second rebase. The files any UI needs stay one level up: `ui/` and
  `display/`. Fonts and icons are generated into `build/gen/`, so nothing moves for them. `icons.c` is
  SLOOP's (its 12 x 12 glyphs left of each column label).
- **`ui_fm6.c` and `ui_drums.c` stay with the UI**, not with their engine or drum folder. They are the SLOOP UI's
  pages for FM6 and for the drums. A different UI does not want them, and keeping them in `ui/sloop/` is what lets a
  UI be swapped.
- **The store of an engine goes with the engine** (`fm6_store.c` in `engines/fm6/`). The project format's drum records
  (`drum_store.c`) go with storage, because `project.c` includes them and they are part of the project format.
- **One-file folders** (`fx/spring/`, `engines/lofi/`...) are on purpose. The folder is the unit a builder switch
  names (section 4), and new code for that feature has an obvious place.

## 2. Mapping, file by file

Generated by `python3 tools/reorg.py plan`; `tools/reorg.py` `LAYOUT` is the source of truth. Files added since the first draft
(SLOOP 2.4 ports, the reverbs, the master COMP, the visualiser, the editor's ed_*.c) are placed by the same rules:
a 2.4 feature that has a project format goes with `storage/`, its sequencer part with `seq/`, its editor commands
with `io/editor/`; a reverb algorithm goes to `fx/reverb/`; a switchable module with its own header gets a folder.

| Old (`firmware/src/`) | New (`firmware/src/`) |
|---|---|
| `core.h` | `core/core.h` |
| `registry.h` | `core/registry.h` |
| `backports.h` | `core/backports.h` |
| `backports23.h` | `core/backports23.h` |
| `backports24.h` | `core/backports24.h` |
| `backports24seq.h` | `core/backports24seq.h` |
| `params.c` | `core/params.c` |
| `bp_set.c` | `core/bp_set.c` |
| `voice.c` | `core/voice.c` |
| `audio.c` | `core/audio.c` |
| `macro.c` | `core/macro.c` |
| `seq.c` | `seq/seq.c` |
| `qnt_seq.c` | `seq/qnt_seq.c` |
| `chance.c` | `seq/chance.c` |
| `motion.c` | `seq/motion.c` |
| `undo.c` | `seq/undo.c` |
| `arranger.c` | `seq/arranger.c` |
| `arranger.h` | `seq/arranger.h` |
| `arranger_scene.c` | `seq/arranger_scene.c` |
| `seq24.c` | `seq/seq24.c` |
| `seq_midi.c` | `seq/seq_midi.c` |
| `stepx.h` | `seq/stepx.h` |
| `storage.c` | `storage/storage.c` |
| `project.c` | `storage/project.c` |
| `upreset.c` | `storage/upreset.c` |
| `drum_store.c` | `storage/drum_store.c` |
| `motion_flash.c` | `storage/motion_flash.c` |
| `motion_proj.c` | `storage/motion_proj.c` |
| `miss.c` | `storage/miss.c` |
| `settings_word.c` | `storage/settings_word.c` |
| `stepx_proj.c` | `storage/stepx_proj.c` |
| `nbank.c` | `storage/nbank.c` |
| `sl24_import.c` | `storage/sl24/sl24_import.c` |
| `sl24_export.c` | `storage/sl24/sl24_export.c` |
| `sl24_guard.c` | `storage/sl24/sl24_guard.c` |
| `sections.c` | `storage/sections/sections.c` |
| `sec_codec.c` | `storage/sections/sec_codec.c` |
| `sec_log.c` | `storage/sections/sec_log.c` |
| `stepx_log.c` | `storage/sections/stepx_log.c` |
| `snapshots.c` | `storage/snapshots/snapshots.c` |
| `snap_store.c` | `storage/snapshots/snap_store.c` |
| `engines.c` | `engines/engines.c` |
| `preset_trim.h` | `engines/preset_trim.h` |
| `eng_analog.c` | `engines/analog/eng_analog.c` |
| `eng_analog2.c` | `engines/analog/eng_analog2.c` |
| `eng_digital.c` | `engines/digital/eng_digital.c` |
| `eng_phase.c` | `engines/phase/eng_phase.c` |
| `eng_lofi.c` | `engines/lofi/eng_lofi.c` |
| `eng_sample.c` | `engines/sample/eng_sample.c` |
| `eng_formant.c` | `engines/formant/eng_formant.c` |
| `eng_trio.c` | `engines/trio/eng_trio.c` |
| `eng_drawbar.c` | `engines/drawbar/eng_drawbar.c` |
| `eng_grain.c` | `engines/grain/eng_grain.c` |
| `eng_super.c` | `engines/super/eng_super.c` |
| `eng_fm6.c` | `engines/fm6/eng_fm6.c` |
| `eng_fm6_rom.h` | `engines/fm6/eng_fm6_rom.h` |
| `fm6_store.c` | `engines/fm6/fm6_store.c` |
| `eng_slice.c` | `engines/slice/eng_slice.c` |
| `eng_cz.c` | `engines/cz/eng_cz.c` |
| `cz_native.c` | `engines/cz/cz_native.c` |
| `eng_phys.c` | `engines/phys/eng_phys.c` |
| `phys_dsp.c` | `engines/phys/phys_dsp.c` |
| `phys_symp.c` | `engines/phys/phys_symp.c` |
| `eng_acid.c` | `engines/acid/eng_acid.c` |
| `acid/acid_dsp.c` | `engines/acid/acid_dsp.c` |
| `acid/bass303.c` | `engines/acid/bass303.c` |
| `acid/bass303.h` | `engines/acid/bass303.h` |
| `dsp.c` | `dsp/dsp.c` |
| `dsp_common.h` | `dsp/dsp_common.h` |
| `dsp_float.h` | `dsp/dsp_float.h` |
| `x0x_param.h` | `dsp/x0x_param.h` |
| `fx.c` | `fx/fx.c` |
| `rev_type.c` | `fx/reverb/rev_type.c` |
| `rev_math.c` | `fx/reverb/rev_math.c` |
| `reverb_alt.c` | `fx/reverb/reverb_alt.c` |
| `reverb_airwin.c` | `fx/reverb/reverb_airwin.c` |
| `master_comp.c` | `fx/master_comp/master_comp.c` |
| `master_comp.h` | `fx/master_comp/master_comp.h` |
| `slicer.c` | `fx/slicer/slicer.c` |
| `spring.c` | `fx/spring/spring.c` |
| `bassplus.c` | `fx/bassplus/bassplus.c` |
| `punch.c` | `fx/punch/punch.c` |
| `drums.c` | `drums/drums.c` |
| `drum_edit.c` | `drums/drum_edit.c` |
| `drum_sends.c` | `drums/drum_sends.c` |
| `drum_kits.c` | `drums/drum_kits.c` |
| `drum_synth.c` | `drums/synth/drum_synth.c` |
| `drum_x0x.c` | `drums/x0x/drum_x0x.c` |
| `x0x/x0x_drums.c` | `drums/x0x/x0x_drums.c` |
| `x0x/drum808.c` | `drums/x0x/drum808.c` |
| `x0x/drum808.h` | `drums/x0x/drum808.h` |
| `x0x/drum909.c` | `drums/x0x/drum909.c` |
| `x0x/drum909.h` | `drums/x0x/drum909.h` |
| `x0x/drum909_dsp.h` | `drums/x0x/drum909_dsp.h` |
| `lcd.c` | `display/lcd.c` |
| `lcd_dirty.c` | `display/lcd_dirty.c` |
| `gfx.c` | `display/gfx.c` |
| `panel.c` | `ui/panel.c` |
| `lights.c` | `ui/lights.c` |
| `knob_accel.h` | `ui/knob_accel.h` |
| `splash.c` | `ui/splash.c` |
| `meters.c` | `ui/meters.c` |
| `ui.c` | `ui/sloop/ui.c` |
| `ui_input.c` | `ui/sloop/ui_input.c` |
| `ui_layers.c` | `ui/sloop/ui_layers.c` |
| `ui_draw.c` | `ui/sloop/ui_draw.c` |
| `ui_menu.c` | `ui/sloop/ui_menu.c` |
| `ui_overview.c` | `ui/sloop/ui_overview.c` |
| `ui_song.c` | `ui/sloop/ui_song.c` |
| `ui_studio.c` | `ui/sloop/ui_studio.c` |
| `ui_drums.c` | `ui/sloop/ui_drums.c` |
| `ui_fm6.c` | `ui/sloop/ui_fm6.c` |
| `macro_ui.c` | `ui/sloop/macro_ui.c` |
| `keylit.c` | `ui/sloop/keylit.c` |
| `bright.c` | `ui/sloop/bright.c` |
| `icons.c` | `ui/sloop/icons.c` |
| `param_help.c` | `ui/sloop/param_help.c` |
| `ui_colors.c` | `ui/sloop/ui_colors.c` |
| `ui_vis.c` | `ui/sloop/ui_vis.c` |
| `ui_drumstep.c` | `ui/sloop/ui_drumstep.c` |
| `console.c` | `io/console.c` |
| `usb.c` | `io/usb/usb.c` |
| `usb_audio.c` | `io/usb/usb_audio.c` |
| `usb_audio_stream.c` | `io/usb/usb_audio_stream.c` |
| `usb_audio_desc.h` | `io/usb/usb_audio_desc.h` |
| `midi_control.c` | `io/midi/midi_control.c` |
| `midi_uart.c` | `io/midi/midi_uart.c` |
| `clock_sync.c` | `io/midi/clock_sync.c` |
| `editor.c` | `io/editor/editor.c` |
| `ed_backup.c` | `io/editor/ed_backup.c` |
| `ed_drums.c` | `io/editor/ed_drums.c` |
| `ed_dsend.c` | `io/editor/ed_dsend.c` |
| `ed_dsrc.c` | `io/editor/ed_dsrc.c` |
| `ed_pages.c` | `io/editor/ed_pages.c` |
| `ed_snap.c` | `io/editor/ed_snap.c` |
| `ed_status.c` | `io/editor/ed_status.c` |
| `ed_macro.c` | `io/editor/ed_macro.c` |
| `ed_sl24.c` | `io/editor/ed_sl24.c` |
| `ed_steps.c` | `io/editor/ed_steps.c` |
| `ed_stepx.c` | `io/editor/ed_stepx.c` |
| `ed_sync9.c` | `io/editor/ed_sync9.c` |
| `ed_user.c` | `io/editor/ed_user.c` |
| `ed_cz.c` | `io/editor/ed_cz.c` |
| `main.c` | `system/main.c` |
| `libc.c` | `system/libc.c` |
| `bootguard.h` | `system/bootguard.h` |
| `cpuguard.c` | `system/cpuguard.c` |
| `cpuguard.h` | `system/cpuguard.h` |
| `cpuguard_costs.h` | `system/cpuguard_costs.h` |
| `ota.c` | `system/ota.c` |
| `recovery.c` | `system/recovery.c` |
| `dual.c` | `system/dual.c` |
| `bench.c` | `system/bench.c` |
| `simd_probe.c` | `system/simd_probe.c` |

## 3. What changes where

### 3.1 Includes (in the move commit)

`tools/reorg.py move` resolves every quoted `#include` in `firmware/` and `tests/` against the old tree. It follows the
compiler's own rules: the includer's folder first, then `-Ifirmware/hal -Ifirmware/src`. It then spells the include
for the new tree:

**Rule: a source file names another source file relative to its own folder**, as the flat tree's sibling includes
did. No unit needs an `-I` it did not need before. Some host tests (upreset_test, for one) build without
`-Ifirmware/src`. A first trial spelled cross-folder includes from firmware/src (`"system/cpuguard.h"`), and those
tests stopped compiling.

| Old spelling | New spelling | Example |
|---|---|---|
| a sibling that is still a sibling, or now in a subfolder | relative | engines/fm6/eng_fm6.c: `"eng_fm6_rom.h"`; drums/drums.c: `"x0x/drum_x0x.c"`; engines/acid/eng_acid.c: `"acid_dsp.c"` (was `"acid/acid_dsp.c"`) |
| a file now in another folder | relative, with `../` | felucca.c: `"core/core.h"`; core/core.h: `"../system/cpuguard.h"`; engines/engines.c: `"../dsp/dsp.c"` |
| relative with `../` already | relative, recomputed | loader.c: `"../src/io/usb/usb.c"`; dsp/dsp.c: `"../../hal/fm1_simd.h"`; tests: `"../firmware/src/storage/project.c"` |
| a HAL header by bare name, or a generated one (`build/gen`) | unchanged | `"fm1_flash.h"`, `"felucca_tables.h"` |

**Checks the script makes before it moves anything**

- Every rewritten include must resolve, in the new tree, to the moved copy of the file it resolved to before.
- An unchanged include must not resolve to a different tracked file in the new tree.

The X0X maths and parameter headers (`dsp_float.h`, `x0x_param.h`) are shared by ACID and the X0X kits, so they live in `dsp/`; the float units name them as `../../dsp/dsp_float.h`.

The ACID and X0X float units are built on their own without `-Ifirmware/src` (build.py `ACID_CFLAGS`). They only
include their siblings, so they need no new flag.

### 3.2 The unity build: felucca.c

Only the include spellings change, for example `"libc.c"` → `"system/libc.c"` and `"ui.c"` → `"ui/sloop/ui.c"`.
The order and the `#if` guards stay the same. The same goes for the includes inside engines.c, drums.c, project.c,
seq.c, editor.c, fx.c and the rest.

### 3.3 Tools (the second commit: `tools/reorg.py paths`)

| Tool | Change | Why it matters |
|---|---|---|
| `tools/size_fns.py` | `SIZE_FILES` / `AUDIO_FILES` become paths under firmware/src (`"ui/sloop/ui.c"`, `"engines/fm6/eng_fm6.c"`...) | **Required for byte-identity.** It reads each file by name and **silently skips one it cannot find**. With old names, no function would get `minsize` and the image would grow by several KB with no error |
| `tools/build.py` | the ACID and X0X unit paths (`engines/acid/acid_dsp.c`, `drums/x0x/x0x_drums.c`); `mmio_check` scans `src/` recursively (`rglob`) | without the first, a build with ACID or X0X drums fails; without the second, the "register access: hal/ only" check would quietly stop seeing the moved files |
| `tools/builder/cpu_costs.py` | `registry.h` → `core/registry.h`, writes `system/cpuguard_costs.h` | it reads the registry and writes the guard's model |
| `tools/level_presets.py` | writes `engines/preset_trim.h` | generator output |
| `tools/div_audit.py`, `tools/div_audit.txt` | scans firmware/src recursively, minus `NOT_AUDITED`: the X0X float units' files, which sat in `acid/` and `x0x/`, folders the flat glob never read. The audit's keys `src/<file>:fn:expr` become `src/<folder>/<file>:...` | without it, the audit would miss every moved file, and the test "divides by a variable" fails. Same scope as before: 154 divides |
| `tools/builder/measure_costs.py`, `configure.py`, `verify.py`, `menu.py` | nothing (no source paths; `verify.py` compiles tests with `-Ifirmware/src`, which still holds) | `costs.json` does not change: no cost changes |
| the generators `tools/gen*.py` | comments only; they write to `build/gen/` | |
| `web/test_web.mjs` | `readFileSync("../firmware/src/<new>")` for params.c, eng_fm6.c, editor.c, ed_*.c, drums.c... | the web tests read the firmware's sources |
| `tests/*.c`, `tests/run_tests.sh` | `#include "../firmware/src/<new>"` (in the move commit); comments | `run_tests.sh` already passes `-Ifirmware/src` and `-Ifirmware/hal` |

### 3.4 Text references (the second commit too)

`tools/reorg.py paths` rewrites every `firmware/src/<old path>` in the tools, tests, web tests, docs, LICENSING.md and
the LICENSES/ notes. It also rewrites `src/<old path>` in the few files that name our tree that way (`SRC_REL_FILES`:
div_audit.txt, MIT-DaisySP.txt, MIT-Rings.txt, DUAL-CORE.md). docs/BLE-MIDI-FEASIBILITY.md's `src/...` paths are
Jangada's tree, so they stay.

**It leaves upstream provenance alone.** A path in another project's tree is that project's path, not ours. Examples:
Felucca's `firmware/src/eng_phys.c` in eng_phys.c's header, Melodee's `firmware/src/cz_native.c at v0.11`, and the
`source.files` of each `tools/backports.json` item.

- **Lines that name another project are left as they are and listed**, for review. Such a line has a GitHub URL, a
  repo slug, `at <commit>`, and so on.
- **In a Markdown table row**, only the last cell is rewritten. That is LICENSING.md's "Files" column; the description
  cells cite upstream paths.
- **In backports.json**, item-level `files` are rewritten and `source.files` are not.

### 3.5 Docs (the third commit)

BUILDING.md, OPTIMIST.md, LICENSING.md, docs/*.md and web/EDITOR_PROTOCOL.md get their `firmware/src/...` paths from
`paths`. A pass by hand then covers:

- prose that describes the layout ("firmware/src holds ...");
- BUILDING.md's build-option table (`firmware/src/acid/` → `firmware/src/engines/acid/`);
- this document's own section 6.

Dated studies (UI-FEASIBILITY.md, FM1-SCENE-2026-10.md, HANDOFF-fm1.md) keep their bare `file.c:line` references,
which stay right.

## 4. How a module declares itself

**A feature = a folder + a builder switch.** There is no manifest format and no new build step. The pieces already
exist:

1. **The folder** holds the feature's code: `engines/cz/`, `fx/spring/`, `drums/x0x/`.
2. **The switch** is the registry item that already exists. It points at its folder with one new field: `path` on
   `Item` in `tools/builder/registry.py`, and the same key in a `tools/backports.json` item (backported features,
   such as SPRING or the CZ engine, register from there through `tools/builder/backports.py`):

   ```python
   _add("ENG_FM6", "FELUCCA_ENG_FM6", "FM6 (DX7, bit-exact with Dexed)", E, 9, provenance=MELODEE_FM6, path="engines/fm6")
   _add("DRUM_X0X909", "FELUCCA_DRUM_X909", ..., path="drums/x0x")
   ```
   ```json
   { "switch": "FELUCCA_SPRING", ..., "path": "fx/spring" }
   ```

   `path` is information only: the builder's menu can show it, docs can cite it, and a test can check it. It changes
   neither `felucca_config.h` nor the image.
3. **The include stays behind the switch**, as now. The include is the line in felucca.c (or engines.c, fx.c,
   drums.c...) wrapped in `#if FELUCCA_<SWITCH>`. With the switch at 0 the file is not compiled and its bytes are
   gone. This is already how `measure_costs.py` measures each item.
4. **`tests/builder_test.py` gains one check:** every `path` exists, and every folder under `engines/`, `fx/` and
   `drums/` is either named by an item or listed as always built (`analog`, `sample`, `fx/` itself...). A new
   engine folder without a switch then fails the test, not a review.

**Adding a feature then means:**

- make the folder;
- add one registry item with its `path` and a new stable `bit`;
- add the `#if` include line;
- add the cost with `measure_costs.py --only KEY`.

Nothing else learns the folder's name. The unity build keeps its explicit include order on purpose: the order matters
(felucca.c line 1) and the compiler sees one unit, so a switch at 0 costs 0 bytes.

**Not done, on purpose**

- **No per-folder `module.json`.** It would duplicate the registry, the one source of truth (registry.py's
  docstring).
- **No glob-based include list.** The order is meaningful, and a stray file must never join the build unnoticed.
- **No runtime plug-ins.** They were dropped: a UI or an engine linked in but unused costs its whole size, and the
  default build has no flash to spare (UI-FEASIBILITY section 3).

## 5. Procedure (phase 1)

It runs only after feat/ui-pass, feat/param-help and refactor/dsp-shared have merged into `optimist`.

1. `git switch -c refactor/source-layout optimist`, and bring this document and `tools/reorg.py` onto it.
2. `python3 tools/reorg.py check`. Any new file without a place: add it to `LAYOUT`, then re-run.
3. Baselines on the parent commit: the 5 profiles' `build/felucca.bin`, `build/loader/ota.bin` and `.fwsc`.
4. `python3 tools/reorg.py move`, then **one commit**: pure renames plus include rewrites. Git records them as
   renames (R100 or R9x).
5. `python3 tools/reorg.py paths`, then one commit (tools, tests, web tests, comments). Then the docs commit.
6. Build the 5 profiles and `cmp` them against the baselines. Run `make test PROFILE=<each>`, the goldens,
   `git diff tools/builder/costs.json` (empty) and `node web/test_web.mjs`.
7. Merge into `optimist`, then tell the other branches (below).

### Rebasing a branch across the move

- **Edits to existing files follow the rename.** `git rebase optimist` or `git merge optimist` (merge-ort follows
  renames) applies a branch's changes to `firmware/src/seq.c` onto `firmware/src/seq/seq.c`.
- **A file the branch added** stays at its old place. Move it with `git mv` into its folder, add it to
  `tools/reorg.py` `LAYOUT`, and fix its include line.
- **An include the branch added** with an old bare name, for example `#include "eng_fm6.c"` from a file that is no
  longer its sibling, fails to compile with "file not found". Spell it relative to the includer:
  `"../engines/fm6/eng_fm6.c"` from `core/`, or `"fm6/eng_fm6.c"` from `engines/engines.c`.
- **A new entry in `size_fns.py`** must be a path (`"ui/sloop/new.c"`). An old bare name is silently ignored there,
  which would cost bytes, not an error.

## 6. Proof of byte-identity

**The real run: `optimist` at a986f93 (batch 6: flash diet, native banks), 2026-10-08.** `tools/reorg.py move` and
`paths` were applied; the images before (a986f93) and after were built with `tools/optimist.py build` and compared with
SHA-256.

| Build | felucca.bin | ota.bin | package (.fwsc) |
|---|---|---|---|
| user-default | `53bd0201ec3bbc16...` | `76190668ed1f282b...` | `08d89d5d1f810b9e...` |
| drum-machine | `dad0ac5e2f8643b3...` | `76190668ed1f282b...` | `b26ca61e9299928c...` |
| x0x-drums | `4598966075a50ae4...` | `76190668ed1f282b...` | `7f84fff5b1676d24...` |
| measurement build (every item) | `594c272bffc1b52c...` | n/a | n/a (`felucca.dis` `c4d3df0d21536c12...` too) |

All of them are identical before and after. `everything-that-fits` does not link on a986f93 (RAMTEXT overflowed by
1,200 B, before and after the move alike), so it has no image to compare.

- **Why identity holds:** nothing in the firmware uses `__FILE__`, there is no runtime `assert` (only
  `_Static_assert`), and the build has no `-g`, so include paths reach neither the IR nor the objects. `size_fns.py`
  marks the same functions `minsize` on both sides (604 in user-default, 621 in x0x-drums: the build log prints the same
  line before and after).
- **Tests:** `make test PROFILE=user-default` (the whole of tests/run_tests.sh, the builder tests, the CLI tests and
  `web/test_web.mjs`) prints ALL HOST TESTS PASSED. The goldens and `tools/builder/costs.json` are unchanged.
- **Found by the trials, and fixed in the script:**
  - includes spelled from firmware/src broke host tests built without `-Ifirmware/src` (section 3.1);
  - the divide audit's `src/` keys and its old scope (section 3.3);
  - `size_fns.py --check` read `firmware/src/*.c` only; it now reads the folders, minus the float units (section 3.3);
  - `tests/fm1_cpu_test.py` spelled `"firmware" / "src" / "console.c"` in pieces.

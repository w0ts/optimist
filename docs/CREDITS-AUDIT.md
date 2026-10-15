# Credits and licence audit (2026-10-15)

What Optimist takes from other people's work, where it is, under which licence, and whether it is credited. Base:
local `optimist` at `f1f11fa9`. Method: the file headers (`SPDX`, `Copyright`, "after", "ported", "from"), the git
history (commit subjects naming a port), the builder's provenance records (`tools/builder/registry.py`), the upstream
repositories (local reference clones under `~/GitHub/refs/`, `~/GitHub/melodee`, `~/GitHub/jangada`, and GitHub's API
for Ultramaster KR-106, Open303, schwung-303, ER-99, 9W9, 8W8, the Phazerville Suite). Not a legal opinion: the
points that need a decision are in "Open questions" at the end.

Status: **credited** (headers, LICENSING.md and the credits pages agree), **fixed** (missing or wrong before this
audit, corrected on `docs/credits`), **open** (a decision for the owner), **n/a** (not distributed).

## JUNO (second look)

| Item | Source | Licence | Files | Status |
| --- | --- | --- | --- | --- |
| JUNO voice: DCO, IR3109 / BA662 ladder, HPF, the cutoff DAC routine | fm-106 by PatttF (`github.com/PatttF/fm-106` `be74757`: `fm106_osc.h`, `fm106_vcf.h`, `fm106_engine.h`), a fixed-point port of Ultramaster KR-106 by Kayrock Screenprinting (`github.com/kayrockscreenprinting/ultramaster_kr106`; every commit by Karl LaRocca) | fm-106 GPL-3.0-only (SPDX in every file, GPLv3 `LICENSE`); KR-106 GPL-3.0 (its README and `LICENSE`; no "only" / "or later" stated) | `firmware/src/engines/juno/eng_juno.c`, `juno_dsp.h`, `tests/juno_test.c`, `tests/golden_juno.txt` | **fixed**: the headers named fm-106 but called KR-106 "GPL-3.0-only" (not stated by KR-106), named no KR-106 author and no Roland notice. Neither upstream puts a copyright line in its files, so there is no notice to keep verbatim; the headers now name the authors and say so |
| JUNO tables (`juno_tables.h`, packed by `tools/gen_juno_tables.py`) | fm-106's `fm106_tables.h`, made by its `gen_fm106_data.py` from KR-106's sources: `J106DACHzTable.h` kV4Hz (one Juno-106 voice card, "V4", measured at 93 DAC codes, interpolated in log frequency; by KR-106's author), `KR106VCF_OPTIMIZED.h` (resonance, feedback, peak pull, Padé OTA curve: formulas), `KR106VCA.h` kVCATableHW (a circuit equation solved numerically), `KR106Voice.h` (sub and noise levels "from hardware measurement") | GPL-3.0 | `firmware/src/engines/juno/juno_tables.h`, `tools/gen_juno_tables.py` | **fixed**: the generated header (and the generator, which writes it) now says where each table comes from and that no envelope (ROM) table is used. Regenerated from fm-106 `be74757`: byte-identical to the committed file. The packed form (perf/juno-size, `b4714b79`) keeps the header |
| fm-106's envelope tables (`FM106_ENV_ATK`, `FM106_ENV_DEC_REL`: "exact AttackIncFromSlider lookup table"; KR-106's `KR106ADSR.h`: "ROM data sourced from github.com/ErroneousBosh/j106roms", the IC29 uPD7811G-152 disassembly) | Roland's Juno-106 voice CPU ROM | not stated | none: `gen_juno_tables.py` takes only VCF_G1, RES_K, FB_SCALE, INV_FB_SCALE, PEAK_PULL, OTA_SAT, SUB_LEVEL_J106, NOISE_LEVEL_J106, VCA_HW; JUNO uses the track's own ENV | **not shipped** (verified: `juno_tables.h` holds none of them). See open question 1 |
| The cutoff DAC routine `juno_dac` | KR-106's `KR106VcfFreqJ106.h`: "clean-room reimplementation of the D7811G firmware's VCF cutoff frequency calculation ($04D5–$064E)", via fm-106 `fm106_calc_vcf_freq` | GPL-3.0 | `juno_dsp.h` | credited; open question 1 (an algorithm read from Roland's ROM) |
| JUNO CHORUS | fm-106 `fm106_chorus.h` (MN3009 BBD model), after KR-106 `KR106Chorus.h` | GPL-3.0-only / GPL-3.0 | `firmware/src/fx/junocho/juno_cho.h`, `tests/juno_cho_test.c` | **fixed** (as the engine) |
| Roland's 128 factory patches (fm-106 `fm106_presets.h`, "decoded from hardware SysEx", from KR-106's `KR106_Presets_JUCE.h`) | Roland | not stated | none: the eight JUNO patches are our own | not shipped (verified: `JUNO_PRESETS` in `eng_juno.c`) |
| fm-106's platform (Dinghy by zvenson on Felucca / SLOOP), dual-core worker, UI, arpeggiator, sequencer, FX | fm-106 | GPL-3.0-only | none | not taken |
| "Juno-106", "Roland" | Roland Corporation's marks | trademark | engine name JUNO, patch names "JUNO ...", builder text "Juno-106 voice" | **fixed**: not-affiliated notice in the headers, LICENSING.md, README, OPTIMIST.md, editor footer. The engine's display name JUNO stays: open question 2 |

## Everything else

| Item | Source | Licence | Files | Status |
| --- | --- | --- | --- | --- |
| The FM-1 platform, engines, sequencer, editor, installer, HAL, loader, tools | Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments, through SLOOP (isod89) | GPL-3.0-only | most of `firmware/`, `tools/`, `web/`, `tests/` (126 files carry Leo Kuroshita's line) | credited |
| SLOOP 2.0 .. 2.4 | isod89 (`isod89/sloop-fm1`); PR #45 by Erick Buendia Barrientos (Erbubar23) | GPL-3.0-only | see LICENSING.md rows | credited |
| Felucca 1.0 .. 1.0.3 backports (chance, KEYLIT, QNT SEQ, SPRING, BASS+, motion, PHYS, size_fns, ...) | Felucca | GPL-3.0-only | LICENSING.md rows | credited |
| Felucca 1.5.6 (`9f6a885`): step conditions, sequence tools, FOLD / FUZZ / PHSR / FLNG inserts | Felucca | GPL-3.0-only | `core/backports15.h`, `seq/auto.c`, `ui/optimist/op_tools.c`, `fx/insfx/insfx.c` | **fixed**: credited in the sources, missing from LICENSING.md |
| Felucca 1.6 (`1727537`): favorites; the knob decoder | Felucca (its CREDITS.md credits ChanceTheMaker for favourites) | GPL-3.0-only | `core/favorites.c`; `hal/fm1_input.h` | decoder credited; favorites **fixed** in LICENSING.md |
| Felucca-Engines NOISE (`c46e008`) | Leo Kuroshita | MIT | `engines/noise/eng_noise.c`, `LICENSES/MIT-Felucca-Engines.txt` | credited |
| CrispyZebra (PHASE waveforms) | Leo Kuroshita | GPL-3.0 | `engines/phase/eng_phase.c` | credited |
| Hügelton Sample Pack (`tools/gen_waves.py`, SLICE's BREAK) | Hügelton Instruments | GPL-3.0-only since Felucca 1.0 | `tools/gen_waves.py` | credited |
| Melodee (FM6, USB audio, TRS MIDI, clock, MIDI expression, scales, step length, encoder fix, CZ 0.11, native banks 0.12) | Kerem Kilic (Ellic Studio), ChanceTheMaker; Melodee's notice "Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)" | GPL-3.0-only | LICENSING.md rows | credited; **fixed**: Melodee's notice line added to LICENSING.md's notices |
| Melodee 1.3.1 (`757b65b`): FRMT and WIDE inserts; `tools/fm1_recover.py` ideas in the rescue tool | Kerem Kilic | GPL-3.0-only | `fx/insfx/insfx.c`, `tools/fm1_rescue.py` | **fixed** (LICENSING.md row; the sources already said so) |
| The second core: CPU1 entry, EMU / interrupt banks, mailbox lines, next-job-only check | X0X `47f8e4c` (`hal/fm1_cpu1.h`, `fm1_cpu1.S`) and Melodee (`crt0.S`, `hal/fm1_multicore.h`) | GPL-3.0-only | `hal/fm1_dual.h`, `hal/fm1_dual_guard.h`, `system/dual.c` | **fixed** (LICENSING.md row) |
| CPU1 fault handler | Melodee `hal/fm1_irq.h` `fm1_core1_fault_c` (`dce4960`, Kerem Kilic) | GPL-3.0-only | `hal/fm1_dual.h` | **fixed** (LICENSING.md row) |
| Jangada: SUPER engine (`d43e6ab`), `tsvf_lpbp`, the USB MIDI ring (`0835cc4`), core supply levels, 2ND CORE AUTO / OFF, CPU1 loop in RAM | zednaked (commits as Thiago Gonçalves), `zednaked/jangada`, a Felucca fork | GPL-3.0-only | `engines/super/eng_super.c`, `dsp/dsp.c`, `io/usb/usb.c`, `hal/fm1_supply.h`, `system/dual_mode.h` | **fixed**: named in the sources and the registry, absent from LICENSING.md, README and every credits page; SUPER's header gave no author / commit / licence |
| Flowstate: scaled font, HOME removal (`6a8ef32`), CPU guard (`e62e186`) | Zakaria Chowdhury | GPL-3.0-only | LICENSING.md rows | credited |
| Flowstate: performance macros, ENERGY (`3962560`, `macro.c`, `arrange.c`) | Zakaria Chowdhury | GPL-3.0-only | `core/macro.c`, `ui/sloop/macro_ui.c`, `io/editor/ed_macro.c` | **fixed** (LICENSING.md row; source header was right) |
| X0X: encoder, knob acceleration, LCD, rescue tool, resampler, backlight, delay halving, glides | Charles Vestal | GPL-3.0-only | LICENSING.md rows | credited |
| ACID: TB-303 voice | X0X `80b7d40` `dsp/bass303.*`, from schwung-303 (Charles Vestal and contributors; midilab / jc303 contributors; Dave Mollen, dm-Rat), from Open303 (Copyright (c) 2009 Robin Schmidt) | GPL-3.0 + Open303 MIT | `engines/acid/bass303.c`, `bass303.h` | **fixed**: Open303's MIT notice and licence text were not in the tree (MIT asks for both in copies); schwung-303's copyright lines added. `LICENSES/MIT-Open303.txt` |
| ACID: TB-3PO generator | X0X `seq/tb3po.c`, from schwung-tb3po (GPL-3.0), from the Phazerville Suite's TB-3PO applet: "Copyright (c) 2020, Logarhythm", MIT, modified by djphazer | MIT (applet), GPL-3.0 (the ports) | `engines/acid/eng_acid.c` | **fixed**: was named "by djphazer and contributors, GPL-3.0"; `LICENSES/MIT-TB3PO.txt` |
| X0X 909 / 808 kits | X0X `80b7d40`, from 9W9 / 8W8 (athousanddetails), ER-99 (Matthew Cieplak); sc808 rim (MIT) | GPL-3.0; sc808 MIT | `drums/x0x/`, `tools/gen_x0x_drums.py`, `LICENSES/MIT-sc808.txt` | credited |
| 909 cymbal samples (`assets/x0x909/`) | ER-99 via 9W9 and X0X | GPL-3.0 (as ER-99 publishes them) | `assets/x0x909/` | credited; open question 4. Its README called 9W9 Charles Vestal's (X0X's own wording): clarified |
| DaisySP, Rings (PHYS) | Electrosmith / Emilie Gillet | MIT | `engines/phys/`, `LICENSES/MIT-DaisySP.txt`, `MIT-Rings.txt` | credited |
| MAME uPD933 (CZ native) | Devin Acker; modifications Kerem Kilic | BSD-3-Clause | `engines/cz/cz_native.c`, `LICENSES/BSD-3-Clause-uPD933.txt` | credited |
| Airwindows VerbTiny (AIRWIN reverb) | Chris Johnson | MIT | `fx/reverb/reverb_airwin.c`, `LICENSES/MIT-airwindows.txt` | credited |
| MSFA / Dexed (FM6 synthesis, tables) | Google (MSFA), Pascal Gauthier (Dexed) | Apache-2.0, GPL-3.0-or-later | `engines/fm6/eng_fm6.c`, `tools/gen_tables.py`, `tests/dexed_ref.cc` | credited |
| klattsch (VOICE design reference, no code) | Tony Gies | MIT | credit only | credited |
| renebohne's SLOOP fork (KEYLIT `e2e5099`) | renebohne (René Gern) | GPL-3.0-only | `ui/sloop/keylit.c` | credited |
| FM-1-transporter (UBOOT protocol), jl-uboot-tool (downloaded loader) | kurogedelic; kagaimiq | MIT | `tools/fm1_rescue.py` | credited (jl-uboot-tool's licence as the scene survey reports it, not re-checked) |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` in each package; register names and values in `firmware/hal/` read from its headers (`fm1_supply.h`, `fm1_irq.h`, `fm1_clock.h`, `fm1_flash.h`, ...) | Zhuhai Jieli Technology | Apache-2.0 | packages; `LICENSES/Apache-2.0.txt` | credited; no SDK file in the tree (no file carries a JieLi notice; the HAL restates register facts) |
| The licence texts with a binary | MIT and BSD-3-Clause parts are compiled into the firmware (NOISE, PHYS, AIRWIN, CZ native, ACID's Open303 / TB-3PO, the 808 rim): their notices must go with every copy, binaries included | MIT, BSD-3-Clause | `tools/build.py` `NOTICE_FILES`, `tools/builder/configure.py`, `web/make_site.py`, `.github/workflows/release.yml` | **fixed**: packages, `-ui.zip`, the site and the release shipped only `LICENSES/Apache-2.0.txt`; now every `LICENSES/*.txt` (`tests/optimist_cli_test.py` checks that LICENSING.md names exactly those files) |
| BLE radio start-up values (`firmware/hal/ble_rf_tables_v15.h`, 5,795 B packed) | captured by `tools/ble_rf_capture.py` from M-VAVE's stock FM-1 V15 running in the emulator | not stated by M-VAVE | `firmware/hal/ble_rf_tables_v15.h` (only a `BLE=1` build uses it; `BLE` is experimental, off by default) | **fixed** in LICENSING.md (row; the "Radio" section said the radio is never enabled); open question 3 |
| BLE stack, USB stack, LZ4 block codec (`tools/lz4blk.py`, written from the LZ4 block format spec), loader | Optimist / Felucca's own | GPL-3.0-only | `firmware/src/ble/`, `tools/lz4blk.py` | credited (own work) |
| Three own files with "Copyright (C) 2026 SLOOP" (`hal/fm1_simd.h`, `hal/fm1_dsp_asm.h`, `system/simd_probe.c`, written here on 2026-10-05) | Optimist | GPL-3.0-only | those files | **fixed**: "the Optimist contributors" (SLOOP reads as isod89's project) |
| Samples | Versilian Studios VSCO-2 CE and VCSL (Sam Gossner), Sonic Pi sample set (freesound.org CC0 recordings) | CC0 1.0 | `assets/samples-cc0/` (`ATTRIBUTION.txt`, `CREDITS.txt`), `assets/hiphop-pack/` | credited (the CC BY 3.0 E.PIANO was removed on 2026-10-08) |
| Synthesised kits and waves (`tools/gen_drumkits.py`, `gen_waves.py`, `gen_tables.py`, `gen_samples.py`) | Optimist / Felucca, computed | GPL-3.0-only | generated at build time | credited |
| Fonts | Terminus (Dimitar Toshkov Zhekov), Fukiai (Hügelton Instruments) | SIL OFL 1.1, MIT | `assets/fonts/`, `web/` | credited |
| Parameter icons, splash logo | Optimist's own (`tools/icon_drawings.py`, the splash drawn from its geometry) | GPL-3.0-only | `assets/icons.png`, `tools/draw_icons.py` | credited |
| The FM-1 emulator | Simon Johansson (`simonjohansson/fm1-emulator`), our fork; fetched by `tools/emu.py`, not in the tree | its own | none | credited (README) |
| Python tools' dependencies: Textual, Pillow, mido, python-rtmidi, pyserial; numpy, scipy, soundfile for the sample scripts | their authors | MIT, MIT-CMU (HPND), MIT, MIT, BSD-3-Clause, BSD | `tools/requirements*.txt` | n/a: installed by the user, never distributed (licences from memory, not re-checked) |
| Web editor libraries | none ("one file, no libraries") | | `web/editor.html` | n/a |
| Optimist UI ABOUT / Credits | | | `ui/optimist/op_project.c` (About: VERSION, BUILD, CHIP, the link), `ui/optimist/op_gldraw.c` (Credits) | **fixed**: SYSTEM > Credits added after About (decision 5) |
| SLOOP UI ABOUT page | | | `ui/sloop/ui_menu.c` | **fixed**: now names Melodee, X0X, Flowstate, Jangada, fm-106, KR-106 and points to LICENSING.md (klattsch moved off the screen for the room) |
| Web editor footer | | | `web/editor.html` | **fixed**: Flowstate, Jangada, fm-106 / KR-106, CZ, the LICENSING.md link, the trademark notice |
| Trademarks | Roland (Juno-106, JP-8000, TB-303, TR-808, TR-909), Yamaha (DX7), Casio (CZ), M-VAVE, teenage engineering, Elektron, Akai | | names in engine, kit, preset and format texts | **fixed**: one nominative-use notice in LICENSING.md, README, OPTIMIST.md, the editor |

## Decisions (the owner, 2026-10-15)

1. `juno_dac`: kept, with the note in its header and LICENSING.md.
2. The engine name JUNO: kept, with the not-affiliated notice.
3. `firmware/hal/ble_rf_tables_v15.h`: kept committed; LICENSING.md says plainly where it comes from (register values
   for the radio's start-up, read out of M-VAVE's V15 firmware, no licence stated upstream).
4. The 909 cymbals: kept; LICENSING.md flags their open provenance.
5. Optimist UI: SYSTEM > Credits, a text page after About (`op_gldraw.c` `CRED`, `tests/ui_optimist_glstyle.h`
   `gls_credits`). No builder item: it is part of the Optimist UI.
6. KR-106's wording: kept as written.
7. 9W9: both names credited (Charles Vestal per X0X's `assets/909/README.txt`, athousanddetails per X0X's
   LICENSING.md and GitHub), with the note that the sources disagree.
8. FM6's commit: Melodee `e459da5` is the one the port took (`958569b7` says so; `ebfe60e` is FM6's first commit in
   Melodee, two days before, without the Dexed-exact rendering the port has). The builder registry is corrected;
   LICENSING.md was right.

The questions as they were put:

1. **Juno-106 ROM-derived material.** We ship none of the envelope tables that fm-106 / KR-106 describe as copies of
   the Juno-106 voice CPU's ROM (`FM106_ENV_ATK`, `FM106_ENV_DEC_REL`; KR-106 credits `ErroneousBosh/j106roms` for its
   ROM data): verified. We do ship `juno_dac`, KR-106's reimplementation of the ROM's cutoff routine (its arithmetic,
   from a disassembly), and one voice card's measured cutoff curve. A firmware ROM is a copyrighted program; an
   algorithm and measured facts usually are not, but that is a legal judgement. Choices: keep and note (as now), or
   replace `juno_dac` with our own cutoff mapping from the documented behaviour (the slider, LFO, ENV and key amounts
   on a 14-bit CV); if the envelope tables were ever wanted, regenerate our own curves from the documented RC
   behaviour of the Juno-106 envelope instead of taking ROM copies.
2. **The engine's name "JUNO".** "Juno" is Roland's mark. The headers and notices use it nominatively ("a model of the
   Roland Juno-106"), but the engine, its patches ("JUNO INIT" ...), the builder items `ENG_JUNO` / `INS_JCHORUS` and
   the insert card "JUNO" carry the name itself. Renaming (for example J106 / "J-CHORUS", or a name of our own) changes
   UI strings, registry texts and the goldens' names; not done here.
3. **The BLE radio values from stock V15.** `ble_rf_tables_v15.h` is data taken from M-VAVE's proprietary firmware
   (register writes and tables, 5.8 KB packed), committed to a GPL repository and compiled into BLE builds. M-VAVE
   states no licence for it. It is experimental and off by default. Choices: keep and note (as now); or stop
   committing it and capture it from the user's own V15 at build time only (the tool already can: `FM1_STOCK_FWSC`),
   so that no M-VAVE data is distributed in the source or in released packages.
4. **The 909 cymbal samples.** ER-99 publishes `hh.wav`, `ride.wav`, `crash.wav` under GPL-3.0 but does not say where
   they were recorded. If they were sampled from a TR-909 (whose cymbals are themselves ROM samples), Roland's rights
   may apply. They are in `assets/x0x909/` and in builds with `DRUM_X0X909` (off by default).
5. **Optimist UI ABOUT.** It shows the version, build, chip and repository link but no credits. Adding a credits
   read-out (or a CREDITS page) is a UI change with a new SYSTEM item, not a text edit: not done here.
6. **KR-106's licence version and author.** KR-106 says "GPL v3" without "only" or "or later"; either way it combines
   with GPL-3.0-only. The author name (Karl LaRocca) comes from its commit metadata, the company name (Kayrock
   Screenprinting) from fm-106's README and the GitHub owner; neither repository has a copyright line. KR-106's
   original 1990s ancestor, UltraMaster KR-106, appears only in its preset tools ("Copyright (c) UltraMaster Group,
   LLC. All Rights Reserved"), which fm-106 and Optimist do not use.
7. **9W9's author.** X0X's `assets/909/README.txt` calls 9W9 Charles Vestal's, its LICENSING.md and GitHub say
   athousanddetails. Kept both, with a note.
8. **Melodee FM6 commit.** LICENSING.md cites Melodee `e459da5` for FM6, the builder registry `ebfe60e`; both are
   Melodee commits (the port and a later sync, not re-checked which files each touched).

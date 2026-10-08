# Firmware builder: design and inventory

Read-only design study, 2026-10-05. This note is uncommitted, and no firmware branch or worktree was changed.
All sizes come from scratch builds: `git archive` copies outside the repo, built with
`JIELI_TOOLCHAIN=~/.jieli/toolchain-docker sh build.sh`. A build takes about 8 s on this Mac.

**Goal.** The user picks which engines, FX, drum kits, sample sets and features go into their FM-1 build, and
sees a live flash, RAM and pool budget while picking. The firmware is personal and GPL-3.0: SLOOP-plus, to
become Optimist (`NAMING.md`).

**Status of the integration.** `~/GitHub/sloop-int` (`feat/integration`) is at `66c0f4a`, which has ANALOG 2,
FM6 and melodee-ports merged. It was in the middle of merging `perf/speed-top3` when this was written.
`docs/MEMORY-BUDGET.md` did not exist there yet. When it lands, it supersedes the projections in §1.6.

---

## 0. Summary

- **Flash is overfull already.**
  - `66c0f4a` builds to **585,180 B**. The slot is 581,564 B, so it is **3,616 B over**.
  - The merges still to come add about 6.7 KB, which puts "everything" at about **591.9 KB, ~10.3 KB over**.
  - **Pool overflows with USB audio:** 335,032 + 12,288 = 347,320 B, and the pool has 344,064 B.
  - **RAM is down to about 0.6 KB free** once speed-top3's RAM placement is in.
- **Samples are 54 % of the image.** `SMP_DATA` is 315,854 B. Each sample set costs 20–99 KB, while each synth
  engine costs 1.6–5.1 KB.
- **The switches already work in practice.** Because SLOOP is one translation unit with `static` objects,
  leaving an engine out of `ENGINES[]` and folding every `== &ENG_X` comparison to 0 drops its code, tables,
  RAM and pool. That is how most of the figures below were measured.
  - Removing several items at once came within 84 B of the sum of the single measurements (0.5 %), so the
    costs add up.
- **Stable IDs are almost free.** Freeze today's FUN7 engine numbers, drum-kit numbers and sample-set numbers
  as permanent IDs. A full build then needs no format change, and reduced builds remap when they load.
- **Recommended UX.** One registry (JSON) feeds both a `.config` file with a terminal menu and a static web
  builder page with a live budget bar. Real builds are cheap (~8 s), so a local helper can always confirm the
  exact numbers.
  - The order: start with the CLI, then the web page. GitHub Actions comes only once the repo is public.
- **Bug found while studying this (code reading only, not run).** User presets are not migrated for the
  ANALOG 2 renumbering; see §2.3.

---

## 1. Inventory

### 1.1 How the numbers were obtained

| Tag | Meaning |
|---|---|
| **[M]** | Measured. The item was removed from a scratch copy of `66c0f4a` and rebuilt; the figure is the full build minus the build without the item. The scratch copy's XIP region was enlarged so that the over-slot image still links. Flag items were measured by defining the flag. |
| **[B]** | Branch delta. The branch HEAD minus the base it forked from. The merge base is `a8a03ae` for every branch, and its code is identical to `feat/sloop-plus` HEAD (later commits are docs only). Base: **574,792 B flash, RAM 83,232 B, pool 322,288 B**. |
| **[D]** | Exact data size, from the generated headers (sample byte offsets, table sizes). |
| **[S]** | Estimate from summing ELF symbol sizes (`nm -S`). It misses inlined code and anonymous string literals. Where both exist, [S] agreed with [M] within about 1 % for engines (GRAIN [S] 5,104 vs [M] 5,116). |
| **[P]** | Projection: [B] deltas added on top of [M]. Not built. |

Reference build `66c0f4a`:

| Image | RAM `.data+.bss` | Pool |
|---|---|---|
| 585,180 B (slot 581,564) | 88,448 of 98,304 | 335,032 of 344,064 (build rule: keep ≥ 8,192 spare) |

Removal costs below are **savings**: what you get back by switching the item off. RAM is `.data+.bss`. Pool is
the `.pool` NOLOAD region.

### 1.2 Engines (`ENGINES[]` order = FUN7 engine numbers)

| No. | Engine (UI name) | Files / main symbols | Presets¹ | Flash | RAM | Pool | Depends on / notes |
|---|---|---|---|---|---|---|---|
| 0 | **ANALOG 2** ("ANALOG") | `eng_analog.c`, `eng_analog2.c`; `a2_*` kernels, `ANALOG_PRESETS`, `A2_PX`; pages OSC 2 / SWARM / FLT 2 (`params.c` `#if FELUCCA_ANALOG2`); `P_A2*` parameters (8, just before `P_E0`) | 24 (incl. SUPER's 5 and SYNC SWEEP, LP24, FIFTH) | ~5.2 KB [S] (ablation fails to compile: `project.c` uses `ENG_ANALOG.presets` for SUPER import) | — | — | Default engine; the target of the SUPER import (`proj_trk_from_super`); `voice.c` engine-specific state; the `perf/analog2-asm` asm adds +928 B [B] |
| 1 | **DIGITAL** | `eng_digital.c` | 13 | **2,240** [M] | 0 | 0 | `voice.c` branch |
| 2 | **PHASE** | `eng_phase.c` (`pd_wave`, `pd_setup`) | 5 | **1,616** [M] | 0 | 0 | `voice.c` branch |
| 3 | **LOFI** | `eng_lofi.c` (`WRAM`) | 3 | **2,332** [M] | 0 | 0 | `voice.c` branch |
| 4 | **SAMPLE** | `eng_sample.c` (`sample_*`, `smp_*`, `usr_zone`) | 11 (`SMP_PRESET_TABLE`, generated) | **1,776** [M] (engine only; the sets are separate, §1.5) | 0 | 0 | Sample sets; USR slots 0xA0000..; `seq.c` key mapping; drums use the PERC set |
| 5 | **FORMANT** ("VOICE") | `eng_formant.c` | 4 | **2,864** [M] | 16 | 0 | — |
| 6 | **TRIO** | `eng_trio.c` | 8 | **3,632** [M] | 0 | 0 | `voice.c` branch |
| 7 | **DRAWBAR** ("WHEEL") | `eng_drawbar.c` | 5 | **2,480** [M] | 2,496 | 0 | — |
| 8 | **GRAIN** | `eng_grain.c` (`gr_p` pool) | 3 | **5,116** [M] | 0 | **21,060** | Sample sets or USR slots as its source (`SRC` = set number) |
| 9 | **FM6** | `eng_fm6.c`, `eng_fm6_rom.h`, `fm6_store.c` (user bank in a USR sample slot, magic "FM6B"), `ui_fm6.c` (black-key operator editor), `midi_control.c` hooks | 16 + 32 ROM voices | **29,832** [M] (everything FM6, including the editor) | **12,544** | **11,712** | MIDI expression layer (controllers); project FM6 block (`fm6[]`, `fm6_on`, `fm6_fn`); web editor FM6 tab / .syx import; FM6 asm saves 96 B |
| 9a | ↳ FM6 black-key editor (`FELUCCA_FM6_KEYS`) | `ui_fm6.c` `fm6k_*`, `FMK_*` pages, `SC_FM6K` | — | **6,548** [M] | 416 | 0 | FM6 |
| 10 | **SLICE** (`FELUCCA_SLICE`, off today) | `eng_slice.c` + the BREAK sample | 1 | **+3,900** code [M] + **22,050** BREAK [D] = **~25.9 KB** | +6,000 | 0 | `gen_samples.py` adds BREAK only when the env flag is set. `seq.c` key mapping. Fits only in reduced builds |
| — | (gone) SUPER, DX7 | `eng_super.c` (kept for `!FELUCCA_ANALOG2`), DX7 removed by FM6 | — | SUPER was 1,944 [M on base]; DX7 was 26,824 [M on base] | — | — | Projects map them on import (formats 4–6) |

¹ Preset counts = the preset table's symbol size / 36 B (`preset_t`).

### 1.3 FX and the mix

| FX | Where | Flash | RAM | Pool | Notes |
|---|---|---|---|---|---|
| **DIST** (per-track insert) | `fx.c` `track_dist`, `P_DIST` | **368** [M] | 0 | 0 | Inlined in `mix_part` |
| **Chorus + delay + reverb** (the three send buses) | `fx.c` `fx_buses`; `cho_buf`, `dly_buf`, `rev_line`, `rev_ap`; pages FX / DLY / REV/CHO | **2,324** [M] for all three | **17,680** (`rev_line` 17,368) | **137,164** (`dly_buf` 131,072, `cho_buf` 4,096, `rev_ap` 1,994) | One loop: separate switches cost little extra, but they need the buses' loop split (or `if` constants). The delay line can be sized: `DLY_LEN` 65,536 samples = 1.49 s; half of it = 0.74 s (a 1/4 note above ~81 BPM) and saves 65,536 B of pool |
| **SLICER** (per-track stutter/gate insert) | `slicer.c`, `P_SL*`, SLICER page | **1,376** [M] (audio path; its UI and params stay) | 128 | **32,768** (`sl_buf`) | `slicer_drums` renders the drums: when off, it calls `drums_render` directly |
| **DUST** (master lo-fi / vinyl) | `fx.c` `dust_process`, `G_DUST` | **624** [M] | 32 | 0 | MASTER page |
| **PUNCH** (16 punch-in FX on the mix) | `punch.c`, `punch_ring`; FX layer keys in `seq.c` / `ui_layers.c` | **2,200** [M] (audio path only; `punch_key` and the UI stay, ~0.3–0.6 KB more [S]) | 0 | **65,536** (`punch_ring`; can be sized with `PUNCH_N`) | `tests/punch_test.c` |
| **DJ filter** | `fx.c` `djf_process`, `G_FILT` | **1,168** [M] | 16 | 0 | MASTER page |
| **DUCK** (the kick ducks the parts) | `fx.c` `duck_block`, `G_DUCK` | **140** [M] | 0 | 0 | Needs the drum track |
| Master (volume ramp, knee, low-cut, DC, limiter) | `fx.c` `master_out` | core | — | — | Always on |
| Mix engine | `mix_block` 13,802 B [S] (events, parts, FX inlined) | core | — | — | Always on |

### 1.4 Drum kits

The drum track's kit is `TDRUM->p[P_E0]`: 0..4 are the sampled kits, 5..36 the synthesised kits
(`drums.c` `DRUM_SAMPLED` 5, `DRUM_KITS`).

| Item | Where | Cost | Depends on |
|---|---|---|---|
| **Sampled GM kit** ACOUSTIC (0) + four treatments DEEP / TIGHT / BRIGHT / DUST (1..4) | Samples: PERC set (`gen_samples.py` KIT, CC0 VCSL). Treatments: a few `if`s in `drums_mix` (`drums.c:272`) | PERC set **98,944 B** [D] + treatments ~0.1–0.2 KB [S, est.] | PERC is also the SAMPLE preset "GM KIT" and a GRAIN source; `drums.c:42` finds it by name |
| **32 synthesised kits** (808, 909, 606, 80S, VINTAGE, TRAP, DRILL, BOOMBAP, LO-FI, PHONK, HOUSE, D.HOUSE, TECHNO, MINIMAL, ELECTRO, DISCO, GARAGE, JUNGLE, DUBSTEP, DEMBOW, AMAPIANO, AFRO, LATIN, TRIBAL, SYNTHWV, CHIP, ARCADE, GLITCH, INDUSTR, HYPER, AMBIENT, JAZZ) | `tools/gen_drumkits.py` `KITS` → `build/gen/felucca_drumkits.h` `DS_KITS[32]`; `drum_synth.c`; levels `tools/drumkit_levels.json` | **364 B per kit** [D] (`DS_KITS` 11,648 B) + ~20 B of names and style; synth code ~0.9 KB [S] | Kit order = kit number (stored in projects); `tests/drumkit_test.c`, `drum_level.c` |

(The task brief said ~25 synth kits; the tree has 32.)

### 1.5 Sample sets (`SMP_DATA`, IMA ADPCM, 22.05 kHz)

These are exact byte ranges from `SMP_ZONES` [D]. The set number is stored in projects as SAMPLE's / GRAIN's
`P_E0`; USR1..3 come right after the built-in sets (`SMP_NSETS + k`).

| No. | Set | Bytes | Used by |
|---|---|---|---|
| 0 | PIANO (Steinway, 4 zones) | 44,104 | GRAND PNO, DUSTY PNO, LOFI KEYS; GRAIN |
| 1 | BASS | 39,138 | UP BASS, DEEP BASS |
| 2 | VIBES | 33,078 | VIBES; GRAIN VIBE HAZE |
| 3 | HORNS | 26,462 | HORN STAB |
| 4 | STRGS | 19,844 | STRING STB |
| 5 | FLUTE | 31,422 | LOFI FLUTE; GRAIN FLUTE DUST |
| 6 | SCRCH | 22,862 | SCRATCH |
| 7 | PERC (GM kit, 27 zones) | 98,944 | drum kits 0..4, GM KIT preset |
| — | BREAK (SLICE only) | 22,050 | SLICE |
| | **Total built in** | **315,854** | + `SMP_ZONES` 1,428 B, `SMP_PRESET_TABLE` 396 B |

**Lever outside the app slot.** User sample slots USR1..3 are 3 × 80 KiB at flash 0xA0000..0xDBFFF, outside the
app slot. They are uploaded through the editor (cmds 11–15, `tools/fm1_sample_upload.py`).

- A set left out of the build can still be loaded there as data. PERC (99 KB) does not fit one 80 KiB slot.
- FM6's user bank also lives in a USR slot.

### 1.6 Features

| Feature | Flag today | Files | Flash | RAM | Pool | Depends / conflicts |
|---|---|---|---|---|---|---|
| **USB CDC console** | `FELUCCA_CDC` (1) | `console.c`, `usb.c` CDC | **4,696** [M] | 2,816 | 0 | Excludes USB audio (`#error` in usb-audio) |
| **USB audio** (UAC1, 4 stems in + stereo out; EXPERIMENTAL) | `FELUCCA_USB_AUDIO` (branch) | `usb_audio*.c`, `usb.c` | **−868 vs CDC** [B] → ~**3.3 KB** with no CDC [P] | −848 vs CDC | **+12,288** | Replaces CDC; nested-IRQ render fix |
| **TRS MIDI IN** | `FELUCCA_UART` (0; 1 in midi-clock) | `midi_uart.c`, `hal/fm1_uart.h` | **544** [M] | 160 | 0 | MIDI clock TRS source |
| **MIDI clock in** (SYNC AUTO TRS > USB > INT, sample-accurate) | none (branch) | `seq.c`, `midi_uart.c`, `usb.c` | ~**4.5 KB** [B: 4,936 incl. UART on] | +624 | 0 | The TRS source needs UART |
| **MIDI expression** (bend, mod, sustain, panic, RPN) + MIDI through scales/chords, QNT ALL, step length, encoder fix, MIDI status | none (merged in `66c0f4a`) | `midi_control.c`, `seq.c`, `voice.c` `midi_pitch_tick` | **1,880** [B] whole branch; `midi_*` [S] 2,290 | −1,504 [B] | 0 | FM6 controllers need `MIDI_EXPR_HOOK` / `MIDI_CC_HOOK` |
| **Editor SysEx** (protocol v5) | none | `editor.c` (`ed_*`), `sx_frame` | ~**8.9 KB** [S] | ~3.0 KB [S] | 0 | Web editor, user presets cmds, sample upload, FM6 bank |
| **OTA / M-UPGRADE entry** (+ recovery) | `FELUCCA_OTA` (1) | `ota.c`, `recovery.c` | **13,940** [M] | 5,952 | 0 | Needs `FELUCCA_FLASH`. **Keep on**: it is the update path |
| **FM6 black-key editor** | `FELUCCA_FM6_KEYS` (1) | `ui_fm6.c` | **6,548** [M] | 416 | 0 | FM6 |
| **Overview pages** (VIEW ALL) | none | `ui_overview.c` | **2,364** [M] | 592 | 0 | `G_VIEW` setting |
| **Parameter icons** | `FELUCCA_ICONS` (auto) | `icons.c`, `gen_icons.py`, `ICON_DATA` 3,096 + `ICON_MAP` 1,136 | **5,360** [M] | 0 | 0 | — |
| **Boot splash** (drawn Optimist logo since 2026-10-06; was the SLOOP RLE) | none | `splash.c` (was `SLOOP_SPLASH_RLE` 4,729) | **368** [M] (was 5,088) | 0 | 0 | The logo is drawn from its geometry, no bitmap |
| **Dual core** (EXPERIMENTAL, emulator only) | `FELUCCA_DUAL` (0) | per-core render state | **+1,240 on** vs off [B]; the refactor alone is −400 [B] | +48 | **+6,144** | CPU1 idle between jobs is still to do |
| **SIMD** packed sine (EXPERIMENTAL) | `FELUCCA_SIMD` (0) | `dsp.c` | **−384** [B] | **+4,096** | 0 | Needs `FELUCCA_ASM` |
| **ASM kernels** | `FELUCCA_ASM` (1) | `hal/` inline asm | FM6 −96 [M]; ANALOG 2 +928 [B] | 0 | 0 | Speed, not features |
| **Idle wait** | `FELUCCA_IDLE` (1) | `main.c` | **0** [B] (image identical) | 0 | 0 | — |
| **Speed: RAM placement** (`.ram_hot` audio path, `.ram_hot2` FORMANT/GRAIN/tables, silent-bus skip, tails to zero, CPU clock readout) | none (merging now) | `app.ld`, `fm1_clock.h`, `build.py` | **+2,100** [B] | **+9,488** [B] (`.ram_hot2` 6,400) | 0 | RAMTEXT: `.ram_hot` 28,280 of 32,512 B. Which engines are "hot" should follow the engine selection |
| **Arranger / song** (A–D, song record) | `FELUCCA_ARRANGER` hard-coded 1 | `arranger*.c`, `ui_song.c` | ~1.8 KB [S] | 1.3 KB [S] | — | **The flag is dead: `=0` does not compile** (`ui_layers.c` uses `live_req`, `srec`) |
| Fonts S + L | — | `FONT_S_DATA` 3,584 [D] (1 bit a pixel); FONT_L is FONT_S drawn at scale 2 (no data of its own) | (4.0 KB) | — | — | Not a switch. Done: FONT_L_DATA (24,576) went, −24,736 B measured, pixel-identical (after Flowstate `6a8ef32`); FONT_S 1 bit a pixel, not 4: −17,952 B measured, pixel-identical (tests/font_test.c) |
| UI canvas, LCD | — | `cv_px` | — | — | 59,520 | Core |
| Projects (4 slots + autosave), undo, user presets | — | `project.c`, `upreset.c` | core | `up_bank` 6,160, `proj_tmp` 3,636… | `autosave_buf`, `song_keep` 7,272 | noinit `proj_slot` 14,544 |

**Projected "everything" [P].** This is `66c0f4a` plus the remaining merges: speed +2,100, idle 0, midi-clock
+4,936, usb-audio −868 (with CDC off), dual-core refactor −400 (flag off), analog2-asm +944.

| Budget | Projected | Capacity | Status |
|---|---|---|---|
| Flash | **≈ 591.9 KB** | 581,564 B | about 10.3 KB over |
| RAM | ≈ 88.4 + 9.5 + 0.6 − 0.8 ≈ **97.7 KB** | 98,304 B | |
| Pool | 335,032 + 12,288 = **347,320 B** | 344,064 B | does not fit even before the 8 KB headroom rule |

The builder has to handle all three budgets, plus RAMTEXT.

**Additivity check [M].** GRAIN, TRIO, FORMANT, DUST, DJ filter and the splash were removed together. The
saving was 18,408 B, against a sum of 18,492 B for the single removals: 84 B off. A cost table with a ~1 KB
safety margin is good enough for the live budget, and the real build then gives the exact figure.

### 1.7 Where each item reaches into the code (switch checklist)

| Item | Presets / BANK | Pages / params | Web editor | Tests |
|---|---|---|---|---|
| Engine | Its `*_PRESETS`; `ui.c` `BANK[]` entries (numeric engine index; FM6 / SUPER via `ENG_IX_*`); `preset_trim.h` rows by engine index | EDIT 1/2 (`edit[8]` in `engine_t`), ANALOG 2's 3 pages, FM6K pages | `editor.html` mock `ENG` table (names), FM6 tab, library (by engine **name**: robust) | `golden.txt` `preset/<ENGINE>/..`, `regress.c`, `cpu_baseline.txt`, `target_budget.txt`, `fm6_*`, `analog2_test` |
| Engine coupling outside its file | `voice.c` (`e == &ENG_ANALOG/DIGITAL/LOFI/TRIO/PHASE/SAMPLE`), `seq.c` (SAMPLE, SLICE, FM6 key maps), `params.c` / `project.c` (ANALOG), `fm6_store.c`, `ui_fm6.c`, `ui.c` (FM6) | | | |
| FX | Preset sends (`FX()` in `preset_t`) | FX, SLICER, DLY, REV/CHO, MASTER pages; `P_DIST/CHOR/DLY/REV`, `P_SL*`, `G_D*`, `G_R*`, `G_C*`, `G_DUST/DUCK/FILT` | FX tab | `punch_test`, `slicer_test`, golden `fx/..` |
| Drum kits | `DRUM_KIT_NAMES/STYLES`, kit number in `P_E0` | DRUMS page | Kit list | `drumkit_test`, `studio_drums_test`, `drum_level.c` |
| Sample sets | `SET_PRESETS`, `EXTRA_PRESETS` (`gen_samples.py`) | SAMPLE `SET`, GRAIN `SRC` enum names | Sample tab (USR slots) | golden SAMPLE / GRAIN presets |
| Features | — | SYSTEM page cells (USB/TRS status, clock), GLO pages | INFO version, cmds | `midi_*_test`, `ota_test`, `ldr_test`, `ui_pages_test` |

---

## 2. Stable IDs

### 2.1 What is positional today (and breaks when an item is left out)

| Data | Stored as | Where |
|---|---|---|
| Project track engine | `proj_trk_t.engine`, a u8 index into `ENGINES[]` | `project.c` (FUN7) |
| Project track preset | u8 index into the engine's preset table (display / "from preset" only; the sound is in `p[]`) | `project.c` |
| Drum kit | `p[P_E0]` of the drum track = kit index | `drums.c`, project |
| Sample set (SAMPLE / GRAIN) | `p[P_E0]` = set index, USR = `SMP_NSETS + k` (**shifts when a set is removed**) | `eng_sample.c`, `eng_grain.c` |
| User preset engine | `up_rec_t.engine` index, validity `< NENGINES` | `upreset.c` |
| Preset list | `BANK[] {kind, engine index, name}`; resolved **by name** within the engine | `ui.c` |
| Level trims | `PRESET_TRIM[engine index][preset index]` | `preset_trim.h`, `engines.c` |
| Editor protocol | Engine bytes in DUMP / TRACK / UP_* / PRESET / NAMES = slot index; INFO lists names in slot order | `editor.c`, `web/editor.html` |
| Parameters | `P_*` / `G_*` ids, mapped "by count" (new common ones go just before `P_E0`) | `core.h`, projects, user presets |

### 2.2 IDs: freeze today's numbers

Each item gets a permanent ID that is never reused. **The IDs are today's numbers**, so a full build stays
byte-compatible with FUN7 projects and current user presets.

| Space | ID = | Values |
|---|---|---|
| Engine UID (u8) | FUN7 numbering | ANALOG 0, DIGITAL 1, PHASE 2, LOFI 3, SAMPLE 4, FORMANT 5, TRIO 6, DRAWBAR 7, GRAIN 8, FM6 9, SLICE 10. New engines 11+. Retired: SUPER and DX7. They keep their importers, which handle the older FUN4–6 numbering where SUPER was 9 and DX7/FM6 10 |
| Drum kit UID (u8) | Today's kit number | Sampled 0..4, synth 5..36 in `KITS` order; new kits 37+ (append only; `gen_drumkits.py` gets an explicit `uid` per kit and a test that forbids reordering) |
| Sample set UID (u8) | Today's set number | PIANO 0 … PERC 7, **USR1..3 = 8..10 forever** (so USR stops shifting), BREAK 11 (internal), new sets 12+ |
| Preset UID | (engine UID, preset name) | Already how `BANK` resolves. Projects keep the u8 preset index as a hint; if the name lookup fails, show the engine name |
| FX / feature bits | Bit numbers in a u32 manifest (§3.4) | Reporting only; never stored in projects |

The rules follow from this:

- **Stored formats are configuration-independent.** `project_t`, `up_rec_t`, `P_COUNT` and `G_COUNT` are
  identical in every build. A reduced build never drops a parameter or the FM6 block from the struct, it only
  ignores them.
- The runtime keeps **slot** indices (`trk->eng_req` indexes the filtered `ENGINES[]` in the ISR). The code
  converts at the boundaries, which are project capture/apply, user preset store/load and editor dumps:
  `eng_uid[slot]` and `eng_slot_of(uid)` (a 16-entry table, 0xFF = absent).
- Sample set and kit parameters hold **UIDs**. The knob walks the list of available UIDs instead of `min..max`.
  `proj_apply`'s clamp becomes a "known UID, else fallback" check that **keeps** the stored value.

### 2.3 Fallback and preservation (graceful)

- **Fallback chain** (per item, from the registry):
  - FM6 → DIGITAL → ANALOG
  - GRAIN → SAMPLE → ANALOG
  - FORMANT, TRIO, PHASE, LOFI, DRAWBAR → ANALOG
  - SLICE → SAMPLE
  - A sampled kit with no PERC → 808 (UID 5)
  - A synth kit → its base kit (K808 / K909 / K707 / KCR78 / KFM / KCHIP), else the first kit present
  - A missing sample set → the first set present, else USR1
- **Orphan preservation.** When a project track names an absent engine:
  - It plays the fallback with the fallback's defaults.
  - The UI shows the original name with a mark, e.g. `FM6*`, plus a one-time notice: "FM6 not in this build:
    plays DIGITAL".
  - The track keeps `orphan_uid` and the original `P_E0..P_E7` in a per-part shadow (3 × 17 B).
  - When the project is saved or autosaved and the user has not changed that track's engine or EDIT values,
    the **original UID and values are written back**. Steps, mix and the FM6 voice block are never touched.
  - So a project survives a round trip through a reduced build and plays correctly again on a full build.
  - Kits and sample sets need no shadow, because the parameter keeps the UID itself.
- **User presets.** A record whose engine is absent stays in the bank. The list shows it greyed as `(FM6)` and
  it does not load. Nothing is erased.
- **Migration bug to fix first** (code reading of `66c0f4a`, not run):
  - `upreset.c` has no renumbering. A SLOOP-plus user preset saved on SUPER (engine 9) now validates as engine 9
    (FM6) and loads SUPER's values into FM6. One saved on DX7 (10) fails `engine < NENGINES` and disappears from
    the list.
  - Projects handle this (`proj_from_np` `old_eng`); user presets do not.
  - Proposal: write user banks as `UPB2` (`UP_VER` 2, engine = UID). Read `UPB1` banks with the pre-ANALOG 2
    numbering: 9 → ANALOG on the swarm (`proj_trk_from_super`), 10 → FM6.
  - That is safe because no build with the ANALOG 2 numbering has shipped.
- **Project format.** No new magic is needed for the IDs: FUN7 already stores engine UID = FUN7 number.
  - Optionally, append a 4-byte "written by" manifest hash in the reserved bytes `rsv[3]` + `sel`. This is
    diagnostics only.

### 2.4 Web editor protocol (v6)

- **INFO**: after the existing v5 trailer (`NTRK`, version 5), version 6 appends one UID byte per engine slot.
  v5 editors keep working, because they resolve by name and the wire still uses slot indices.
- **New cmd 34 `BUILD`** returns:
  - the config hash (FNV-32 of the canonical `.config`, 5 × 7 bit) and the profile name (≤ 16 chars);
  - masks: engine UIDs (u16), FX bits (u16), feature bits (u32), kit UIDs (u64), set UIDs (u16);
  - image size, flash free, RAM free and pool free (as built).
- **Avoid cmd 33 for anything new.** Melodee's `AUDIO_STATS` is 33, which collides with our `DRUM_STEP` (33).
  If USB audio stats are ported, use 35.
- **Web editor changes:**
  - Filter the mock `ENG` table by INFO names.
  - Hide the FM6 tab, SLICE and the sample pages when they are absent.
  - Show the BUILD manifest on the device page.
  - The library already stores `engineName`, so patches move between builds.

---

## 3. Switch design

### 3.1 Naming

Keep the `FELUCCA_*` prefix, as decided in `NAMING.md`, so Melodee and SLOOP ports stay easy.

| Group | Flags |
|---|---|
| Engines | `FELUCCA_ENG_ANALOG`, `_DIGITAL`, `_PHASE`, `_LOFI`, `_SAMPLE`, `_FORMANT`, `_TRIO`, `_DRAWBAR`, `_GRAIN`, `_FM6`, `_SLICE`. `FELUCCA_SLICE` becomes an alias. `FELUCCA_ANALOG2` is retired after the integration: ANALOG 2 is the only ANALOG, and the `!FELUCCA_ANALOG2` / `eng_super.c` path can go |
| FX | `FELUCCA_FX_DIST`, `_CHORUS`, `_DELAY`, `_REVERB`, `_SLICER`, `_DUST`, `_PUNCH`, `_DJF`, `_DUCK` |
| Sized options | `FELUCCA_DLY_LEN` (65536 / 32768), `FELUCCA_PUNCH_N`, `FELUCCA_SL_LEN` |
| Features (existing) | `FELUCCA_CDC`, `FELUCCA_USB_AUDIO`, `FELUCCA_UART`, `FELUCCA_OTA`, `FELUCCA_FLASH`, `FELUCCA_ICONS`, `FELUCCA_FM6_KEYS`, `FELUCCA_ASM`, `FELUCCA_SIMD`, `FELUCCA_DUAL`, `FELUCCA_IDLE` |
| Features (new) | `FELUCCA_MIDI_CLOCK`, `FELUCCA_MIDI_EXPR`, `FELUCCA_OVERVIEW`, `FELUCCA_SPLASH`, `FELUCCA_EDITOR` |
| Arranger | `FELUCCA_ARRANGER`: repair the flag or remove it |
| Data (generator-level, no C flag needed) | `FELUCCA_SETS="PIANO BASS …"`, `FELUCCA_KITS="808 909 …"`, `FELUCCA_KIT_SAMPLED` |

### 3.2 Registry and X-macro filtering

- **One source of truth.** `config/registry.json` lists, per item: id / UID, flag, group, label, files,
  `requires` / `conflicts` / `fallback`, and a measured cost (flash / RAM / pool / RAMTEXT).
- **`tools/configure.py` generates three things from it:**
  - `build/gen/felucca_config.h`: every flag as a literal `0` / `1` (or a number);
  - `build/gen/felucca_registry.h`: the X-macro lists;
- **`build.py`** stops passing a hand-kept env-flag list. It passes `-include build/gen/felucca_config.h`
  (the host tests do the same), and `gen_samples.py` / `gen_drumkits.py` read the selected sets and kits.

`felucca_registry.h` (generated, or hand-written once with the flags as inputs):

```c
/* X(uid, sym, flag, fallback_uid) */
#define ENGINE_LIST(X) \
    X(0, ENG_ANALOG,  FELUCCA_ENG_ANALOG,  0) X(1, ENG_DIGITAL, FELUCCA_ENG_DIGITAL, 0) \
    X(2, ENG_PHASE,   FELUCCA_ENG_PHASE,   0) X(3, ENG_LOFI,    FELUCCA_ENG_LOFI,    0) \
    X(4, ENG_SAMPLE,  FELUCCA_ENG_SAMPLE,  0) X(5, ENG_FORMANT, FELUCCA_ENG_FORMANT, 0) \
    X(6, ENG_TRIO,    FELUCCA_ENG_TRIO,    0) X(7, ENG_DRAWBAR, FELUCCA_ENG_DRAWBAR, 0) \
    X(8, ENG_GRAIN,   FELUCCA_ENG_GRAIN,   4) X(9, ENG_FM6,     FELUCCA_ENG_FM6,     1) \
    X(10, ENG_SLICE,  FELUCCA_ENG_SLICE,   4)
#define IF_0(...)
#define IF_1(...) __VA_ARGS__
#define IF(f) CAT(IF_, f)                   /* the flags are literal 0 / 1 */
#define ENG_SLOT(uid, sym, f, fb) IF(f)(&sym,)
#define ENG_UIDS(uid, sym, f, fb) IF(f)(uid,)
static const engine_t *const ENGINES[] = {ENGINE_LIST(ENG_SLOT)};
static const uint8_t ENG_UID[] = {ENGINE_LIST(ENG_UIDS)};
#define NENGINES (sizeof ENGINES / sizeof ENGINES[0])
#define ENG_IS(e, NAME) (FELUCCA_ENG_##NAME && (e) == &ENG_##NAME)   /* folds to 0 when absent */
```

- **Includes.** `engines.c` keeps including every `eng_*.c`. In a unity build an unreferenced `static` engine,
  its tables and its `.bss` / `.pool` buffers are all dropped; this was measured, see §1.2. So **no `#if` is
  needed around the includes.**
  - Exception: a file that will not compile without another component.
- **Coupling sites** become `ENG_IS(e, TRIO)`; this is what the [M] ablations did:
  - `voice.c` (6 comparisons), `seq.c` (SAMPLE / SLICE / FM6), `params.c`, `project.c`;
  - FM6's satellites `fm6_store.c` and `ui_fm6.c` (their entry points check `ENG_IS(.., FM6)`).
  - `project.c`'s direct `ENG_ANALOG.presets` use goes through a helper.
- **BANK**: `{kind, uid, name}`. `bank_resolve()` already looks presets up by name. Absent UIDs are marked and
  skipped by `preset_pos` / `preset_at`. Zero `#if`; ~10 B of strings per absent entry remain (accepted).
- **Kits and sets** are filtered in the generators. The headers emit `DS_KIT_UID[]` / `DS_KIT_FALLBACK[]` and
  `SMP_SET_UID[]`. `DRUM_KIT_NAMES` and the SAMPLE / GRAIN `SET` / `SRC` names are built from those lists.
- **FX**: `if (FELUCCA_FX_DUST) dust_process(..)` in `mix_block`. The constant folds, which is exactly what was
  measured. Inside `fx_buses`, the three buses are kept apart with `if (FELUCCA_FX_CHORUS)` blocks. Pool
  buffers are sized `FELUCCA_FX_DELAY ? DLY_LEN : 1`.
- **Pages**: add `uint8_t need` to `page_t` (a feature / FX bit). `page_shown()` tests it as a constant. The
  page indices stay put, so nothing persisted moves. The parameters stay in `P_*` / `G_*`; their pages only
  disappear.
- **Icons**: keep them all-or-nothing (`FELUCCA_ICONS`). Optional later: `gen_icons.py` keeps only the labels
  of the selected engines (a few hundred bytes).
- **Expected `#if` count.** Only where a dependency truly cannot compile (USB audio vs CDC endpoints, UART HAL,
  dual core, SIMD), so roughly today's ~90 lines. Everything else is constant `if`s and table filtering.

### 3.3 Dependencies (enforced by `configure.py`, mirrored as `#error` in `felucca_config.h`)

- **requires:**
  - GRAIN → (any built-in set) or USR (always present, so a warning only)
  - kits 0..4 → PERC
  - SLICE → BREAK (automatic)
  - FM6_KEYS → FM6
  - FM6 → MIDI_EXPR
  - MIDI_CLOCK TRS source → UART
  - OTA → FLASH
  - SIMD → ASM
  - DUCK → drum track (always present)
  - EDITOR → USB MIDI (always present)
- **conflicts:** USB_AUDIO ⊥ CDC.
- **"Core", not switchable:** mix, master, sequencer, drum track and its synth engine (with ≥ 1 kit),
  projects / undo / autosave, user presets, LCD / UI, fonts, the OTA update path (strongly recommended).
- **Experimental (off in every profile unless chosen):** USB_AUDIO, DUAL, SIMD.

### 3.4 The firmware reports what it is

- `felucca_config.h` defines `FELUCCA_CFG_HASH` and `FELUCCA_CFG_NAME` (the profile), plus masks generated
  from the flags.
- They are reported through the BUILD SysEx (§2.4), the console `info` command (CDC builds), and a GLO >
  SYSTEM > INFO line (`BUILD: DRUM 3F2A`).
- The package name becomes `optimist-<ver>-<profile>-<hash>.fwsc`, so files from different builds can't be
  confused. The installer shows the profile it read from the package metadata.

### 3.5 Tests and goldens per configuration

- **`run_tests.sh CONFIG=…`** builds every host test with `-include build/gen/felucca_config.h`.
- **Goldens stay one superset file**, keyed by name (`preset/<ENGINE>/..`, `kit/..`, `fx/..`, `mix/..`). A
  reduced run checks only the entries whose items are present.
  - **Invariance rule:** every golden of an included item must be **bit-identical** to the full build, because
    leaving an engine out must not change the sound of another.
  - Mix goldens that use an absent engine are skipped. Each profile gets its own mix golden, rendered from a
    profile-specific demo.
- **`ui_pages_test`**: "every preset in BANK exactly once" checks only the resolved entries. The fuzzing runs
  per profile.
- **`project_test`, round trip:**
  - Full build → reduced build → save → full build gives identical bytes for an orphan track.
  - Absent kit and set UIDs are kept.
  - `UPB1` → `UPB2` user preset migration.
- **Size guard:** for each off item, `nm` must show **no** symbol of that item (e.g. no `ENG_GRAIN`, no
  `gr_p`). This catches a stray `&ENG_X` that silently keeps everything.
- **Matrix:**

  | When | Configurations |
  |---|---|
  | Every run | full (dev), the three profiles, "minimal" (ANALOG + synth kits) |
  | Nightly (`tools/measure_costs.py`) | each item off, compile-only, which also refreshes `registry.json` costs |
  | Weekly | a few random valid configs |

- **Budget tables** (`cpu_baseline.txt`, `target_budget.txt`) filter by item. The CPU worst case per profile
  is a new useful number, since fewer engines means less RAMTEXT pressure.

---

## 4. Builder UX

| | A: `.config` + terminal menu | B: web builder (not built) + local helper | C: web builder + GitHub Actions |
|---|---|---|---|
| How | `tools/configure.py` (curses menu on macOS / Linux; numbered prompts on Windows, where Python has no curses), `--profile`, `--list`, `--budget`, `--set FX_PUNCH=n` | A static page reads `registry.json`: groups, toggles, dependency warnings, live bars for flash / RAM / pool / RAMTEXT; exports `.config`. `tools/builder_server.py` (localhost) runs `build.sh` with it and returns the exact sizes and the `.fwsc` | The page dispatches a workflow with the config; the artifact is the `.fwsc` |
| Live budget | From the cost table, instantly; "Build & measure" = real build (~8 s) | Instantly from the table; exact after ~8 s through the helper | Instant estimate; exact after minutes |
| Install | Existing installer (needs a **"local .fwsc" file input**, absent today) | The same, or the helper hands the file to the installer page | Download, then install |
| Needs | Toolchain + Docker + SDK (as now) | Same, plus the helper | A public repo; toolchain and SDK in CI (download and licence to check); secrets. **Repo is local-only by decision** |
| Effort | ~2 days | +2–3 days (page) +1 day (helper) | +1–2 days, once public |
| Pros | Smallest; scriptable; works offline; diffable configs; profiles = files | Best UX: see the trade-offs at once; same registry; shareable `.config` | Builds for people without a toolchain |
| Cons | Text UI; Windows fallback is plain | Two codebases on one registry; a browser cannot run clang, so it needs the helper | Not possible while nothing is pushed; CI flakiness (Docker clang crash) |

**Recommendation.** Do A first, as the engine: the registry, configure, cost table and profiles. Then B, with
the local helper, as the UI on the same registry. C waits for the push decision.

The cost table makes the bar live. The real build is cheap enough that the helper can auto-build ~1 s after
the last click, so the user nearly always sees exact numbers.

**Budget logic.**

- Four bars:

  | Bar | Capacity |
  |---|---|
  | Flash | 581,564 B, plus a safety margin of 1,024 B |
  | RAM | 98,304 B |
  | Pool | 344,064 − 8,192 B |
  | RAMTEXT | 32,512 B |

- Each item shows ±. Unmet dependencies are shown inline.
- "Fit" suggests removals ranked by the user's priorities. **Samples are only suggested when the user marked
  them droppable**, since samples are not trimmed unasked.
- Sized options are sliders: delay length (128 KB / 64 KB), punch ring.

**Profiles** (shipped as `config/profiles/*.config`; the numbers are [P]: the "everything" projection minus
the [M] savings):

| Profile | Contents | Flash | Fits? |
|---|---|---|---|
| **FM + VA studio** | ANALOG 2, FM6 + keys editor, DIGITAL, SAMPLE, TRIO; all sample sets; GM kit + all synth kits; all FX; CDC (or USB audio), TRS, clock, expression, editor / OTA, overview, icons, splash. Off: GRAIN, FORMANT, LOFI, PHASE, DRAWBAR | ≈ 591.9 − 14.4 (GRAIN 5.1, FORMANT 2.9, LOFI 2.3, PHASE 1.6, DRAWBAR 2.5) ≈ **577.5 KB** | **Yes, ~4 KB free**; RAM −2.5 KB, pool −21 KB (GRAIN). With USB audio the pool fits only after the delay is halved |
| **Drum machine** | ANALOG 2 (bass), SAMPLE, GRAIN, LOFI, **SLICE**; all kits; all sample sets; all FX incl. PUNCH / SLICER / DUST / DJF / DUCK; clock, TRS, USB audio or CDC. Off: FM6, DIGITAL, PHASE, FORMANT, TRIO, DRAWBAR | ≈ 591.9 − 29.8 (FM6) − 12.8 (others) + 25.9 (SLICE) ≈ **575.2 KB** | **Yes, ~6 KB free**; FM6 off frees 12.5 KB RAM and 11.7 KB pool, so USB audio fits the pool |
| **Everything that fits** | All features; experiments off; the solver removes by priority until the margin holds | 591.9 KB, so ≥ 11.4 KB must go | **Choice:** (a) splash + icons + overview (12.8 KB, keeps every sound); or (b) FORMANT + LOFI + PHASE + DRAWBAR (9.3 KB) + splash; or (c) one sample set (SCRCH 22.9 KB). The pool needs the delay halved if USB audio is on |
| (dev) **Full / CI** | Everything, XIP enlarged in scratch only (never flashable) | — | For tests and goldens |

---

## 5. Phased plan

**0. Wait for the integration merge** (`feat/integration`).

- Hotspots: `engines.c`, `core.h`, `project.c`, `ui.c` BANK, `editor.c`, `fx.c` and `build.py`.
- Then read `docs/MEMORY-BUDGET.md`, re-run the measurements (the `abl` method of §1.1 as
  `tools/measure_costs.py`), and update §1.

**1. Stable IDs + registry, no behaviour change** (1.5–2 days):

- `felucca_registry.h` X-macros for engines; `ENG_UID[]`, `eng_slot_of()`; `ENG_IS()` at the coupling sites;
  BANK by UID.
- Freeze the kit and set UIDs; `gen_drumkits.py` / `gen_samples.py` get explicit UIDs, and USR moves to 8..10
  fixed.
- Fix the `UPB1` user preset renumbering; repair or remove `FELUCCA_ARRANGER`.
- Acceptance: goldens unchanged, image within ±200 B, all tests green.

**2. Switches** (3–4 days):

- `configure.py` → `felucca_config.h`; `build.py --config`; the generators read the config.
- FX `if`s; page `need`; orphan preservation + fallback + UI mark; INFO v6 + cmd 34 BUILD; web editor
  filtering.
- Per-config tests: invariance goldens, round trip, `nm` guard.

**3. Builder CLI** (~2 days): the menu, profiles, cost table, "fit" solver, package naming, and the
installer's local-file input.

**4. Web builder + local helper** (3–4 days, not built; the terminal builder came first): a web page sharing `registry.json`, and
`tools/builder_server.py`.

**5. Later:**

- RAMTEXT auto-placement: `.ram_hot.<engine>` sections, filled in priority order from the selected engines by
  a generated linker fragment.
- Sized FX buffers in the UI; GitHub Actions; per-profile CPU baselines on hardware.

---

## 6. Risks

- **A stray reference keeps a whole item** in a unity build (one `&ENG_X` not folded). Mitigation: the `nm`
  guard per configuration.
- **Combinations multiply.** Mitigation: profiles + minimal + nightly single-off + random weekly; the
  invariance goldens catch cross-effects.
- **Stored-format drift** if someone makes a struct configuration-dependent. Mitigation: a static assert on
  `sizeof(project_t)` / `sizeof(up_rec_t)` fixed constants, and the round-trip test.
- **Hidden coupling** not yet listed: engine-specific code in `voice.c` / `seq.c`; FM6's bank inside a USR
  slot; GRAIN and SAMPLE reading set numbers; the drum PERC lookup by name; preset trims by engine row.
  These have to move behind `ENG_IS` / UID lookups in phase 1.
- **The cost table goes stale** after every merge. Mitigation: nightly `measure_costs.py`; the menu always
  offers the real build.
- **Budgets other than flash.**
  - Pool overflows with USB audio, and RAM is near full after speed-top3. The builder must show all four
    budgets.
  - RAM placement (`.ram_hot*`) should follow the engine selection, or RAMTEXT may be wasted on absent
    engines.
- **Hardware.** Every number is from the emulator or host build: no real FM-1 yet. The experimental items
  (USB audio, dual core, SIMD) stay opt-in.
- **Dependency on the integration.** Starting before the remaining merges would collide in exactly the files
  phase 1 touches (`engines.c`, `core.h`, `project.c`, `ui.c`, `editor.c`, `fx.c`, `build.py`). Phase 1 should
  be the first commit after the integration is green.

## 7. Reproducing the measurements

Scratch scripts, not part of the repo:

- `b1.sh`: a `git archive` of a branch HEAD + `assets/samples-cc0` + `gen_samples.cache`, then build.
- `abl.sh`: replace `&ENG_X` in `ENGINES[]`; null the other references.
- `abl2.sh`: perl substitutions, e.g. drop a call in `mix_block`, or `#define` a flag.
- `bsyms.py` / `bgroup.py`: `nm -S` by region and group.

The full build needs the XIP region enlarged in the **scratch** `app.ld` (`LENGTH = 0xADFBC`) to link past the
slot. The same approach becomes `tools/measure_costs.py`.

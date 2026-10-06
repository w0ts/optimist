# Pluggable UI for Optimist: feasibility study

Snapshot: `optimist` at `d03f7d3`, read from a temporary detached worktree. Nothing was changed or committed in the firmware repo.
Date: 2026-10-06.

**Evidence tags**

| Tag | Meaning |
|---|---|
| **[M]** | Measured in this study: a real `--measure` build of the default config of `d03f7d3` (`JIELI_TOOLCHAIN=~/.jieli/toolchain-docker`), its ELF, `llvm-nm -S`, and `felucca.dis`. |
| **[S]** | Sum of ELF symbol sizes, grouped by the source file that defines each symbol. The firmware is a unity build (`felucca.c` includes every `.c`), so a callee that the compiler inlines counts toward its caller's file. String literals have no symbol and are not counted: 7,272 B of `.text` has no symbol owner [M]. Expect about ±10 %. |
| **[C]** | Taken from `tools/builder/costs.json` (measured 2026-10-06 06:06 by `measure_costs.py`; its base is 608,240 B, 176 B less than my build). |
| **[X]** | Cross-reference count from a script over comment- and string-stripped sources. It is approximate: a local variable that shares a name with a global can be counted. I removed the false positives I found. |
| **[E]** | Estimate. This is not measured. The reasoning is given with it. |

Scripts and raw outputs, all in this scratchpad: `attr.py` (bytes per file), `groups.py`, `couple.py` → `coupling.txt`, `isr_reach.py`, `nm.txt`, `attr.tsv`, `base/felucca.dis`, `base/sizes.json`, `build-default.log`.

---

## 0. Summary

- **Feasible: go with conditions.** The UI runs only in the main loop. No UI function is reachable from either interrupt handler [M]. The core calls the UI through about 24 identifiers [X]. Most of these calls are status messages and "redraw" flags.
- **The seam to cut is mostly one way.** The UI reaches deeply into the core: 363 core identifiers and 1,888 line references [X]. That is acceptable, because those references become the core API.
- **Pick the UI at build time, in the unity build.** This costs **0 B and 0 cycles** [E, by construction: same translation unit, direct calls]. Runtime switching between UIs that are all linked in is ruled out: the default build is already **26,852 B over** the app slot [M].
- **Flash is the real prize.** The current UI's own code and icons come to **58,195 B** [S]. The large font, which only UI files reference, is 24,576 B more [M]. A minimal performance UI could make the default feature set fit [E]: it could free about 60–75 KB against a 26.9 KB overflow.

---

## 1. How coupled are the UI and the core today?

### 1.1 Build state (default config, every feature) [M]

| Region | Used | Limit | State |
|---|---|---|---|
| Flash app slot | 608,416 | 581,564 | **26,852 over** |
| RAM `.data+.bss` | 95,028 | 98,304 | 3,276 free |
| Pool | 354,848 | 344,064 | 10,784 over (the pool headroom check fails) |
| RAMTEXT | 32,604 | 32,512 | 92 over |
| NOINIT | 12,352 | 15,696 | ok |

`docs/MEMORY-BUDGET.md` measured 585,376 B at `c8608b1`. The tree has grown by about 23 KB since then.

### 1.2 The UI's share of flash and RAM [S]

Bytes by owning file (`groups.py`). "Flash" is XIP `.text` only.

| Part | Files | Flash | RAM | Pool |
|---|---|---|---|---|
| UI logic: input, layers, menu, screens' state | ui.c, ui_input.c, ui_layers.c, ui_menu.c, ui_song.c, ui_studio.c, ui_drums.c, ui_fm6.c, keylit.c, bright.c | 24,853 | 1,274 | 0 |
| UI drawing: pages, graphs, overview | ui_draw.c, ui_overview.c | 28,472 | 1,717 | 0 |
| Icons | icons.c + felucca_icons.h (ICON_DATA 3,096, ICON_MAP 1,160) | 4,870 | 0 | 0 |
| **UI-only subtotal** | | **58,195** | **2,991** | 0 |
| Fonts S + L (shared) | felucca_font.h: FONT_S_DATA 21,504 [M], FONT_L_DATA 24,576 [M] | 47,272 | 0 | 0 |
| Display driver and canvas (shared) | lcd.c, gfx.c, lcd_dirty.c; canvas `cv_px` gfx.c:18 | 2,264 | 1,200 | **59,520** |
| Param tables, pages, formatting | params.c, bp_set.c (TP 1,440, GP 640, PAGES 468, page_desc 506, param_format 760) | 4,832 | 0 | 0 |
| Panel map | panel.c | 310 | 8 | 0 |
| Web editor protocol (for comparison) | editor.c, ed_*.c | 10,337 | 1,629 | 0 |
| Samples | felucca_samples.h | 317,742 | 0 | 0 |
| Core (everything else) | | 114,240 (+27,520 RAMTEXT) | 67,295 | 3,072+ |

**What these numbers mean**

- **UI-only code and icons** are 58,195 B [S]: **9.6 % of the image**, or **20 % of the image without the samples** (290,674 B).
- **With the fonts, the display layer and the param pages:** about 112.6 KB [S], or 18.5 % of the image.
- **RAM:** the UI's own `.bss` is about 3 KB [S]. The screen canvas is **17 % of the pool** (59,520 / 344,064 [M]).
- **Largest UI symbols** [M]:

  | Symbol | Bytes |
  |---|---|
  | `ui_draw` | 24,388 |
  | `ui_input` | 10,606 |
  | `layer_key` | 1,606 |
  | `fm6k_draw_graph` | 1,540 |
  | `graph_dsnd` | 1,276 |
  | `BANK` | 736 |
  | `te_header` | 716 |
  | `panel_setup` | 608 |

- **Measured removals of UI features** [C]:

  | Feature removed | Flash saved | Other |
  |---|---|---|
  | `FM6_KEYS=0` | 9,112 B | |
  | `ICONS=0` | 5,584 B | |
  | `OVERVIEW=0` | 2,504 B | |
  | `LCD_DIRTY=0` | 1,208 B | 640 B of RAM |
  | `OV_ARP=0` | 592 B | |

  `SIZE=0` (no minsize on the UI, the stores and the editor) **costs** 5,920 B. Minsize is therefore already earning its keep on UI code.
- **FONT_L is used only by UI files:** 14 references in ui_draw, ui_input, ui_layers, ui_menu, ui_song and ui_studio [X]. In the unity build an unreferenced `static const` is dropped, so **a UI that does not use the large font drops 24,576 B by itself.** FONT_S must stay: `main.c` (the UBOOT and RESTORED screens), the OTA screens (`felucca.c:194-212`), `recovery.c` and `dual.c` all use it.
- **Not measured: the UI's true linked size.** I did not run an ablation build that compiles the UI out. The edit needed for it, even in the scratch worktree, was refused by the permission system. The [S] numbers miss UI string literals, which are part of the 7,272 unowned `.text` bytes. They also include about 2–4 KB of model code that lives in UI files (section 1.4) [E].

### 1.3 Dependencies, both directions [X]

`couple.py` assigns each identifier to the file that defines it, then counts the cross-group references.

| From → To | Identifiers | Line refs | Kinds (identifiers / refs) |
|---|---|---|---|
| ui → core | 363 | 1,888 | fn 108/640 · var 71/514 · macro 51/296 · enum 133/438 |
| ui → params | 66 | 215 | enum 40/124 · var 11/46 · fn 8/25 |
| ui → display | 25 | 782 | fn 13/407 · macro 9/361 |
| ui → panel | 31 | 148 | enum 23/121 |
| **core → ui** | **24** | **107** | fn 16/74 · var 7/31 · macro 1/2 |
| core → params | 10 | 26 | TP ×11, GP ×6, track_desc, analog2_from_super, bps_pack/unpack |
| editor → ui | 6 | 19 | set_engine, apply_preset, track_select, `ui.force` ×12, sync_reload |
| editor → params | 3 | 3 | TP, GP, DRUM_KIT_DESC (editor.c:355-374) |
| params → core | 157 | 403 | mostly the P_/G_ enums |

**Core state the UI touches** (field-name matches over `->x` / `.x` in UI files; short names such as `note` are ambiguous):

- **`track_t`:** 16 of 69 fields, 271 accesses. The largest are `p` 152, `step` 45, `eng_req` 16, `seq_idx` 14, `dstep` 13, `lvl` 9, `user` 5 and `preset` 5. Read-only views include `peak`, `arp_note`, `seq_notes`, `nheld` and `v[]`.
- **`song_t`:** 9 of 11 fields, 184 accesses: `g` 59, `playing` 43, `sel` 39, `rec` 13, `octave` 11, `seq_mode` 8 and `solo` 7.
- **Writes with the audio IRQ off:** 17 places in UI files (ui_layers 8, ui_input 4, ui_drums 2, ui_studio 2, ui.c 1). These are model mutations that the UI performs itself.

**Core → UI, every site.** This is the part that blocks pluggability.

| What | Where | Count |
|---|---|---|
| `ui_message` / `ui_say` (status text) | project.c:777-1187, sections.c:183-260, upreset.c:347-369, fm6_store.c:211-348, drum_kits.c:230-242, main.c:219,226, dual.c:169 | 51 |
| `ui.force = 1` (full redraw) | arranger_scene.c:27,55 · project.c:815 · sections.c:292 · upreset.c:311,370 · fm6_store.c:223-262 · drum_kits.c:243 · main.c:122,267 · editor.c:421-776 · ed_drums.c · ed_dsend.c | about 30 |
| `sync_reload` (editor RELOAD push, owned by ui.c:25) | arranger_scene.c:26,54 · project.c:814 · sections.c:291 · upreset.c:310 · editor.c:269,294 | 7 |
| Model operations defined in ui.c and called by the core | `apply_preset_to`, `set_engine_of`, `trk_def_engine`/`_preset`, `track_defaults(_steps)`, `param_kept` (ui.c:236-325), called from main.c:107-113, project.c:327,811, upreset.c:299, bench.c | 15 |
| Main-loop entry points | main.c:120 `layers_init`, :121 `go_home`, :138 `sloop_splash`, :182 `panel_setup`, :290/:310 `ui_input`, :292 `ui_leds`, :293 `ui_draw` | 8 |
| UI state read by the core | project.c:867 (autosave waits while `ui.menu`), main.c:287-288 (`ui.page`/`ui.home` crash breadcrumbs), main.c:42 / project.c:986,1055 (`bl_dim`, `BL_DUTY` from bright.c), upreset.c:236 (`up_gen`) | 7 |
| `FELUCCA_VERSION` (defined in ui.c:5) | console.c:186,299, editor.c:387 | 3 |

**UI state in core types, and core logic in UI files**

- **`song_t.seq_mode`** (core.h:332) is written only by UI files and read nowhere else. It is UI state stored in the core struct.
- **`engine_t` carries display metadata:** `page_title` (core.h:206), `edit[8]` labels (:207), `color` (:213), `macro[4]` (the HOME knobs, :214) and `desc()` (:221).
- **The model operations behind the UI's buttons live in UI files**, inlined into `ui_input`. They are `project_new` (ui_input.c:263-290), `edit_param`'s LOAD / SAVE / CLRSEQ / INITSND / NEWPRJ actions (ui_input.c:292-466), `step_edit`, `tracks_edit` and `undo_swap` (ui.c:137). The curated preset list `BANK` and `bank_resolve` are in ui.c:327-436.
- **Evidence of the cost:** `tests/hostsim.c:95` re-implements `apply_preset_to`, because the original is in ui.c.

### 1.4 The audio ISR and the UI [M]

`isr_reach.py` follows direct calls, branches and address references from `fm1_alnk0_irq` and `fm1_timer5_irq` in `felucca.dis`. **211 symbols are reachable, and none of them is a UI or display function.**

- **The one apparent hit is a false positive.** `layer_screen_draw.v` shares its address with `.L_MergedGlobals.5`. The ISR uses that address as a base register, to reach `ua` / `midi_in_src` at +345 (felucca.dis:1113-1117).
- **Indirect calls are not traced.** Through function pointers, the ISR calls only engine hooks (`engine_t`).

**The ISR does carry the panel's interaction model, as data:**

- `keyboard_block` / `key_down` / `key_up` (seq.c:968-1155) decide per key between playing a note, punch-in FX, erase, roll, or forwarding the key to the UI.
- The decision uses layer state that the UI sets:
  - `ly_bit[]` (seq.c:70), `ly_ops_on` (:73), `dyn_bit` (:74) and `ly_lock` (:77);
  - `dl_ui_pick` (drum_edit.c:28).
- Keys that belong to the UI go through the ring `lk_q` (seq.c:89-100). `ui_input.c:637-655` drains it.

There is no call from the ISR into the UI. However, **another UI's key semantics must be expressible with these flags.** An X0X UI's "keys are steps" fits, because `ly_lock` can hold LY_STEP open. A UI that wants a key mode this enum does not have needs a core change in seq.c.

---

## 2. What would the seam look like?

```c
/* core_api.h: what any UI may use (implemented by the core; most of it already exists) */
/* parameters: metadata shared with the web editor (params.c TP/GP + ENGINES[]->edit) */
const param_desc_t *core_param(uint32_t scope, uint32_t id, int16_t **vp);   /* = editor.c ed_desc */
void core_param_set(uint32_t scope, uint32_t id, int32_t v);                 /* clamp, motion, undo, IRQ rules */
void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit);  /* exists */
/* model commands (moved out of ui.c / ui_input.c) */
void core_select_track(uint32_t i);  void core_set_engine(track_t *, uint32_t);
void core_apply_preset(track_t *, uint32_t);  void core_project_new(void);
void core_project_load(uint32_t); void core_project_save(uint32_t);  int core_undo(int redo);
void core_step_edit(...); void core_transport(uint32_t req);           /* transport_req today */
/* read-only views: song, trk[] (const), clk_beat/clk_pos, track_t peaks, seq_idx, voices */
/* events: notifications the core raises, the UI polls (no callbacks: no indirection) */
void core_say(const char *a, const char *b);         /* replaces ui_message / ui_say (51 sites) */
extern volatile uint32_t core_dirty;                 /* replaces ui.force (~30) and sync_reload (7): bits */
/* keyboard routing: a UI declares its layer buttons and which layers it consumes */
void core_keys_config(const uint32_t ly_bit[LY_COUNT], uint32_t dyn_bit[2]);
int  core_ui_key(uint32_t *layer, uint32_t *key, uint32_t *down);   /* drains lk_q */

/* ui_api.h: what each UI provides (called only from main.c) */
void ui_init(void);           /* layers_init + go_home + splash */
void ui_boot_setup(void);     /* the OCT-/OCT+ calibration (panel_setup) */
void ui_frame(void);          /* input + leds + draw, ~60 Hz (main.c:290-293) */
void ui_poll(void);           /* the idle-loop input (main.c:310) */
uint32_t ui_busy(void);       /* autosave holds off (project.c:867 ui.menu) */
```

**Param metadata can stay shared.** The web editor already reads `TP`, `GP`, `ENGINES[]->edit` and `DRUM_KIT_DESC` directly (editor.c:355-374). `track_desc` in params.c:207-216 is a near copy of that code. The split is:

| Stays in the core | Moves to the SLOOP UI |
|---|---|
| params.c:1-347: the descriptors, presets' extras, `param_format` | params.c:349-527: `PAGES`, `page_shown`, `cell_built`, `page_desc` |
| The 5-character labels, which both front ends use | |

Two side effects of the split:

- `tools/size_fns.py` lists params.c in AUDIO_FILES (kept at -Os). Moving the pages into a UI file would let them build at minsize [E: a small saving, a few hundred bytes].
- `engine_t`'s display fields (`page_title`, `color`, `macro`) can stay. A UI that ignores them pays only their bytes in the engine tables: 4 + 2 + 4 bytes per engine [E].

---

## 3. Pluggability styles and their costs

| Style | Flash | RAM | CPU | Verdict |
|---|---|---|---|---|
| **A. Build time: one UI in the unity TU** (`#include` of the chosen UI's file list instead of felucca.c:87-97; builder choice item) | **0 B** [E: same TU, the same direct calls the compiler already makes; `ui_draw`/`ui_input` are not inlined today either] | 0 | 0 | **Recommended** |
| B. Link time: separate object per UI, a table of entry points | A UI in its own TU loses inlining of the core's `static` helpers and needs them given external linkage (AINL helpers in headers still inline): a few hundred bytes to ~2 KB [E]. build.py is unity-only today. | 0 | 0 | No benefit over A here |
| C. Runtime switch: 2+ UIs linked, a `const struct ui_ops` table | the **sum** of the UIs (current 58 KB [S] + the other) + ~40 B table [E] | per-UI state, unless unioned; the canvas is already shared | One indirect call per entry. The idle loop calls `ui_poll` at up to ~10 kHz (TIMER5 wakes, main.c:309-320), so about 10k × ~5-10 cycles ≈ 0.05-0.1 M cycles/s, which is <0.2 % of the main loop at 48 MHz and less at higher clocks [E]. | **Not with the flash we have**: the default build is 26,852 B over [M] |

**Audio ISR:** unaffected in all three styles. It reaches no UI code [M], and the main loop is the only caller of the UI entries. Indirect calls in C would also stay out of the HOT/RAMTEXT rules: UI code is XIP, and only main-loop code calls it (core.h:54-80).

---

## 4. Feasibility verdict

**Feasible.** The UI is main-loop-only, the ISR boundary is already clean, and the core-to-UI direction is small and mechanical. The large UI-to-core direction is the de facto core API.

### 4.1 Incremental steps (each keeps the firmware working and tests green)

| # | Step | Files | Lines [E] | Gate |
|---|---|---|---|---|
| 1 | **Notifications seam.** Add `core_say` + `core_dirty` + `core_reload`. Replace the 51 `ui_message`/`ui_say`, ~30 `ui.force` and 7 `sync_reload` sites. ui.c keeps thin aliases while this happens. | 12 core files + ui.c | ~100 edited | `build.py` sizes within ±64 B, goldens, all `run_tests.sh` |
| 2 | **Model ops out of the UI.** New `model.c` (core) takes `apply_preset_to`, `set_engine_of`, `track_defaults`, `trk_def_*`, `param_kept`, `track_select` (minus its `ui.*` lines), `project_new`, the GLO/SAVE/TOOLS actions from `edit_param`, `undo_swap`, `step_edit`, `tracks_edit`, `BANK`/`bank_resolve`. hostsim.c drops its copy (hostsim.c:95). | ui.c, ui_input.c, felucca.c, hostsim.c, ui_pages_test.c, editor.c | ~400 moved, ~60 edited | the same, plus ui_pages_test's 185 checks |
| 3 | **UI entry interface.** main.c calls only `ui_init`/`ui_boot_setup`/`ui_frame`/`ui_poll`/`ui_busy`. Crash breadcrumbs move to a `ui_dbg` hook. Move `seq_mode` out of `song_t`, and `FELUCCA_VERSION` to a core header. | main.c, project.c, core.h, ui.c, console.c | ~60 | the same |
| 4 | **Split params.c** into core metadata (params.c) and SLOOP pages (`ui/sloop/pages.c`). | params.c, size_fns.py, tests' include lists | ~200 moved | the same |
| 5 | **Build-time selection.** Move the UI include list (felucca.c:82-97 incl. panel.c, knob_accel.h) to `ui/sloop/ui_sloop.c`. Add a `FELUCCA_UI` choice in `tools/builder/registry.py` with a new stable bit. Ship a second, tiny UI (headless or minimal) so the seam is exercised in CI. | felucca.c, registry.py, build.py, run_tests.sh, new `ui/min/` | ~50 + the new UI | builder_test.py, a CI build of each UI |
| 6 | **Keyboard routing contract.** Document `ly_bit`/`ly_lock`/`dyn_bit`/`dl_ui_pick`/`lk_q` as core API (`core_keys_config`, `core_ui_key`). | seq.c, ui_layers.c, ui_input.c | ~40 | ui_pages_test layer cases |

**Total effort** [E]: about 900 lines touched or moved across about 25 files, plus the second UI. Steps 1-4 do not change behaviour, so the audio goldens (`tests/golden.txt`, audio-only, `regress.c`) cannot move. ui_pages_test is the behavioural net for the UI.

### 4.2 Risks

| Risk | Evidence | Mitigation |
|---|---|---|
| **LCD/SPI timing and XIP cache.** UI drawing and font reads are XIP. FM6 and GRAIN render from XIP, not RAMTEXT (core.h:60-66, MEMORY-BUDGET §1). A heavier UI can evict their code and data and cost audio CPU. | core.h comment; not measured per UI | Gate every UI on the emulator CPU budget test (`tests/cpu_baseline.txt`). Keep drawing through gfx.c `cv_*` with lazy redraw. |
| **Main-loop pacing.** `ed_service` and `ui_input` share the 15 ms frame (main.c:309-320). A slow `ui_frame` delays editor replies and key handling, but not the audio. | main.c | A frame-time budget in the API contract. The debug counters already exist (`felucca_dbg`). |
| **Encoder pacing.** Counts accumulate in the TIMER5 ISR (HAL). `panel_enc` + `knob_accel.h` (from X0X) shape them. A UI that polls rarely only sees bigger deltas. | ui_input.c:168-186 | Keep `accel()` in a shared helper. |
| **Shared fonts and strings.** FONT_S is needed by core screens (OTA, UBOOT, recovery); FONT_L is UI-only. The status strings (`"SAVE ERROR"`, ...) move with `core_say` and stay shared. | §1.2 | The font becomes a property of the UI. The builder shows the font cost. |
| **Web editor protocol.** It uses the core metadata plus `set_engine`/`apply_preset`/`track_select` and `ui.force`. | editor.c:355-472 | Step 2 points it at `model.c`. The protocol bytes (v6) do not change. `ED_BUILD` reports the UI bit. |
| **UI tests are bound to SLOOP's UI.** ui_pages_test (931 lines, 185 checks), backports_ui.c (31 checks) and song_ui_test include the SLOOP UI files and stub the core around them. | tests/ui_pages_test.c:22-99 | They stay as the SLOOP UI's tests. Add a small core-commands test that every UI shares. |
| **The ISR's layer model.** A UI with key semantics outside `LY_*` needs a core change. | seq.c:67-100, 968-1155 | Step 6. Extend the enum only when a real UI needs it. |
| **Dead flags.** `FELUCCA_ARRANGER=0` does not compile because `ui_layers.c` uses `live_req` and `srec` (MEMORY-BUDGET §2.3). Optional features are woven through the UI. | docs | Every UI must compile with every registry option. builder_test.py already enumerates profiles. |

### 4.3 What an alternative UI would cost a contributor [E]

| UI | Lines | Flash | Notes |
|---|---|---|---|
| **Minimal performance UI**: track, preset, 4 macros, transport, status | 600-1,200 | 6-15 KB, FONT_S only | It frees about 58 KB (the UI code) + 24.6 KB (FONT_L) − its own size, so roughly **65-75 KB net**. That is more than the 26.9 KB overflow, so the default feature set would fit. It can also shrink the 59.5 KB canvas in the pool, which is over by 10.8 KB. |
| **X0X-style step UI** on the Optimist core | 1,500-2,500 | 15-30 KB | fm1-x0x's `app/ui.c` is 3,154 lines, but it is bound to its own model (`app/x0x.h`, `seq/sequencer.h`: 808/909/303). Its interaction and drawing can be reused; its bindings must be rewritten against core_api.h. Steps on keys already exist in the core (LY_STEP + `ly_lock`). |
| **Felucca-style UI** | about 6,000 to rebind | about the current UI's size | Felucca 1.0.1's ui*.c + params.c are 6,202 lines. 49 per-track parameter enum names differ from Optimist's (`core.h` P_ list diff). It is essentially SLOOP's ancestor, so a port is mostly re-binding, but it is the largest of the three. |

Every contributor would get gfx.c/lcd.c, the fonts, `param_format`, the shared param metadata and the core commands for free. The precedent is X0X's `plat.h` + `ui_init`/`ui_frame`/`ui_say` split (fm1-x0x `app/plat.h`, `app/x0x.h`), which shows the shape works on this hardware.

---

## 5. Recommendation: **go, with conditions**

**Conditions**

- Build-time selection only (style A); no runtime switching while the default build overflows.
- Steps 1-4 must land as zero-behaviour, near-zero-byte refactors, proven by `build.py` sizes, the goldens and ui_pages_test.
- A second UI must ship with the seam, so that the seam is real and not a rename.
- Each UI must pass the emulator CPU budget, because of the XIP cache risk.

**First two concrete steps**

1. **Notifications seam (step 1).**
   - Add `core_say(a, b)`, `core_dirty` bits and `core_reload` in a core header.
   - Replace the 51 `ui_message`/`ui_say`, ~30 `ui.force` and 7 `sync_reload` writes in project.c, sections.c, upreset.c, fm6_store.c, drum_kits.c, arranger_scene.c, dual.c, main.c, editor.c, ed_drums.c and ed_dsend.c.
   - ui.c's `ui_frame` polls them.
   - Gate: the image size within ±64 B of 608,416, and `tests/run_tests.sh` all green.
2. **Model ops out of the UI (step 2).**
   - Create `model.c` (core), included before the UI in felucca.c.
   - Move into it `apply_preset_to`, `set_engine_of`, `track_defaults*`, `trk_def_*`, `param_kept`, `track_select`, `project_new`, the TOOLS/PROJECT actions, `undo_swap` and `BANK`.
   - Point editor.c and `tests/hostsim.c` at it, and delete hostsim's duplicate.
   - Gate: the same as step 1, plus ui_pages_test's 185 checks.

After these two steps, the core no longer names anything in ui*.c except the five entry points. Step 5, the `FELUCCA_UI` builder choice with a headless or minimal UI, then gives the measured flash share that this study could only estimate.

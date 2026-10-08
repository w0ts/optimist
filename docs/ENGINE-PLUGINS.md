# Engine plug-ins: Felucca's design compared with Optimist's modularity plans

Date: 2026-10-06. Read-only: nothing was changed or committed in any repo. Two throw-away toolchain probes were
compiled in this scratchpad (`picprobe/`), never in a repo.

Sources read:
- **Felucca** `~/GitHub/refs/Felucca`, branch `origin/research/flash-savings` at `7d92eaa` (fetched today), 10 commits
  over `main` (`20c275e`), **docs only** (5 new files, 650 lines, Japanese):
  `docs/engine-plugins-route.md` (49 lines), `docs/flash-savings-research.md`, `docs/flash-savings-plan.md`,
  `docs/ble-route.md`, `docs/usb-companion-route.md`. Felucca `main` source was read to check the facts that the
  plug-in doc relies on.
- **Optimist** `~/GitHub/optimist`, branch `optimist`: `docs/BUILDER.md`, `docs/MEMORY-BUDGET.md`,
  `docs/FM1-SCENE-2026-10.md` §6.5, `docs/CPU-GUARD.md`, `docs/BLE-MIDI-FEASIBILITY.md`,
  `tools/builder/costs.json`, `tools/build.py`, `tools/size_fns.py`, `tools/fm1pkg_make.py`, `firmware/src/core/core.h`,
  `registry.h`, `storage.c`, `bootguard.h`, `firmware/hal/fm1_flash.h`, `firmware/loader/ldr_core.c`, the `eng_*.c` files.
- The pluggable-UI study: `scratchpad/ui-feasibility.md`.

**Evidence tags**

| Tag | Meaning |
|---|---|
| [F-M] | Felucca's doc says it measured this (a real build or link with the JieLi toolchain) |
| [F-E] | Felucca's estimate, or a figure whose doc gives no method |
| [F-D] | Felucca's design statement (a proposal, not built) |
| [V] | verified here by reading the source (file:line given) |
| [M] | measured in this study (the toolchain probe) |
| [E] | my estimate; the reasoning is given |

---

## 0. Summary

- **What Felucca proposes [F-D]:** engines installed later into the spare 72 KiB SDK "USR" flash region
  (0xEA000–0xFBFFF). They execute **in place from XIP** and are not copied to RAM. Each engine package (`.feng`) is
  **linked at a fixed address per slot**: there is no relocation and no PIC, and CI builds one image per slot. The host
  passes a versioned function table (`felucca_api_t`) to the plug-in's `init(api)`, which returns an `engine_t *`;
  a version mismatch is refused. Per-part engine state comes from a pool the host lends; plug-ins may not allocate static RAM.
  Engine numbers 32–63 are reserved for plug-ins; a missing one shows `NO ENGINE` and is silent. A boot-loop guard
  disables the last active plug-in.
- **Status: design only.** There is no code and no prototype, and the overhead was not measured. The only figures are
  per-engine sizes (no method given) and a 20–30 KB base-image saving estimate.
- **Against Optimist's build-time switches:** the builder costs **0 B and 0 cycles** per engine left out, is verified
  bit-identical, and needs no ABI. Plug-ins cost the base image a loader, an API table and a frozen ABI. In return,
  users can **install an engine without rebuilding**, and engine code gets **up to ~96 KiB of flash outside the 581,564 B
  app slot**. Plug-ins would run from XIP, but Optimist deliberately runs most engines from RAMTEXT (`core.h:55-69`),
  and the emulator cannot measure that difference.
- **Toolchain, measured here [M]:** JieLi clang 4.0.1 compiles `-fPIC`, but its GNU ld 2.26.51 can neither make a
  shared or PIE object ("File format not recognized") nor resolve `R_PI32V2_GOT16` in a static link ("relocation
  truncated to fit"). `-fropi`/`-frwpi` are rejected. **PIC is not available**, so Felucca's fixed-address link is the
  right call. A fixed-address link with `--emit-relocs` keeps `.rela.text`/`.rela.rodata`, so **relocating at install
  time** (patch the absolute words while writing flash) is a cheaper alternative to one build per slot [M + E].
- **Verdict for Optimist: feasible as a complement, not a replacement. Go only after a host-side ABI prototype.**
  The main risks are, in order:
  1. XIP speed against RAMTEXT (it needs hardware);
  2. freezing an ABI in a fast-moving tree (`engine_t` gained 5 hooks since Felucca's lineage);
  3. per-part RAM: only the light engines fit without a pool lease;
  4. the first test of executing code (not only reading data) through the plaintext XIP window, which needs hardware.
- **Pluggable UIs: use the same mechanism only for small per-engine UI hooks, not for whole UIs.** A UI reaches 363
  core identifiers (ui-feasibility §1.3), which is far too wide for a function table. The ~58 KB UI also does not fit
  the 72 KiB region next to engines. Keep the build-time UI choice.
- **Flash savings that transfer:**
  - extending `minsize` to more main-loop files (Felucca −3,008 B [F-M]);
  - the unused flash regions (0x93000–0x96FFF, 16 KiB; 0xEA000–0xFBFFF, 72 KiB), which are also unused in Optimist [V];
  - "retired sample set → a user slot of the same name";
  - the measured confirmation that `-Oz`, `-fno-inline-functions` and `--gc-sections` are either CPU trades or no-ops
    in a unity build.

  Their icon and font items do not transfer (different assets).
- **BLE:** Felucca **linked** the SDK's `demo_ble`: 280,576 B flash and 39,212 B static RAM [F-M]. This is above our own
  study's 120–200 KiB estimate. The audio-latency impact is **not measured** (the gate they propose).
- **VBUS:** "the FM-1 does not supply VBUS" is marked **確認済 (confirmed)**, but the doc gives no method. VBUS
  *presence* is readable on P33 `P3_LDO5V_CON` bit 5 [F-D, register-level]. Host mode without VBUS is untested
  (their experiment 1).

---

## 1. What Felucca's design proposes (translated and summarised)

Source: `docs/engine-plugins-route.md` @ `7d92eaa`. Its first line: 結論: **成立する** ("conclusion: it works").
This chip executes code straight from flash (XIP), so an engine can be written to free flash and reached through
`engine_t`'s function pointers, with no copy to RAM and no reflash of the firmware. The doc rates the size as
"2.0 class" (a major-version job). Its precondition is a **union of per-part engine state**, the same groundwork as
their 6- or 8-track plan.

### 1.1 API and versioning [F-D]

- **Package `.feng`:** a header (magic, **ABI version**, engine name, **required RAM**, size, CRC) plus the linked image.
- **ABI:**
  - The host passes `felucca_api_t` to the plug-in's `init(api)`. It holds a version number and a function table:
    the DSP helpers, tables such as `SINE`, `voice_amp`, `mulq15`, `clamp`, … and read access to `track_t`.
  - The plug-in returns its `engine_t *`.
  - **If the version differs, the plug-in is refused** (exact match; no minor/compatible scheme is described).
  - The doc notes that "everything is `static` in one TU today, so cutting this boundary *is* the work".
- **The plug-in is self-contained:** "one table + constant data + code". Felucca's `ENGINES[]` is append-only
  ([V] `Felucca/firmware/src/core.h:102`, `engine_t` at `core.h:161-191`).
- **Out of scope:** register access (plug-ins are pure DSP; `hal/` stays the only MMIO owner).

### 1.2 Storage and loading [F-D]

- **Where:**
  - 0xEA000–0xFC000: 72 KiB, the SDK's USR region, unused by Felucca.
  - They state engine sizes: ANALOG 2.3 KB, LOFI 2.1, PHASE 1.6, FM6 5.2, GRAIN 4.9, DRUM 8.5. So "10+ engines" fit.
    These sizes are [F-E]: the doc does not say how they were obtained.
- **XIP, not RAM:**
  - The app runs in the XIP window (0x02000000..).
  - The user-data area (from 0x93000) is visible as plaintext through XIP after `fl_plain_window_init`, so code placed
    there "runs as is".
  - [V] Felucca: `hal/fm1_flash.h:217-226` sets `SFCENC_UNENC_L = FL_XIP(0x93000)`. Optimist has the same call at
    `project.c:1003`.
  - Today that window is only proven for **data** reads (user samples play from it). **Instruction fetch** through it is
    untested by either project [E: very likely the same path through the cache, but not shown].
- **Install:** `ENG_BEGIN/WRITE/END` SysEx commands, cloned from the existing `SMP_BEGIN/WRITE/END`. The web editor
  offers a list of engines.

### 1.3 Relocation and linking [F-D]

- **No relocation, no PIC, no dynamic linker** (listed explicitly under "やらないこと", "won't do").
- Each slot has a fixed link address. The editor picks the image built for a free slot, and **CI builds every engine
  for every slot**.

### 1.4 Calling convention and ISR constraints [F-D]

- **Call range:** XIP (0x02000000..0x020FFFFF) is within the 23-bit `call` range. Calls through function pointers are
  absolute, so range does not matter. [V] Optimist adds the converse: RAM↔XIP is *out* of direct-call range, so
  HOT code reaches XIP code only through pointers, and `build.py` checks it (`core.h:55-59`).
- **The ISR:**
  - It calls the plug-in's `render` and the other hooks through `engine_t`, as it already does for built-in engines.
  - The plug-in calls host helpers through `api->…`, which means **no inlining** of `mulq15`/`clamp` unless the plug-in
    carries its own inline copies [E].
- **CPU:**
  - Plug-ins fall outside `target_budget.py`'s net.
  - The host watches `cpu_q8` and drops voices on overload.
  - The package carries a host-measured instructions-per-sample figure, and the editor warns.
- **Safety:**
  - A broken plug-in crashes the audio ISR.
  - The existing boot-loop guard (two failed boots → UBOOT; [V] Felucca `core.h:326-328`) gains a rule: on the next boot,
    disable the plug-in that was active last (a mark in `.noinit`).

### 1.5 RAM [F-D]

The doc calls RAM "the biggest constraint".
- Engines that live in `voice_t.s[8]` / `ph[3]` work at once.
- Engines with per-part static state are different. Felucca's examples: GRAIN 28 KB, PHYS 51 KB, DRUM 7 KB. For these
  the host lends a per-part engine pool, which is the union of engine states.
- The header declares the RAM a plug-in needs. A plug-in that needs more than the pool is not installed.
- Plug-ins may not allocate static RAM.

### 1.6 Presets and projects [F-D]

- Engine numbers 32–63 are reserved for plug-ins and map to slot k.
- A save that names an engine that is not installed shows `NO ENGINE` and plays silence, like the sample-pack `NO PACK`.
- Factory presets travel inside the package.
- UI: `param_desc_t` labels and enum names are plug-in constants, used by pointer. Parameter icons are picked by label
  (`icons.c`), so they come automatically.

### 1.7 Tests, toolchain, order [F-D]

- **Tests:** the same C is built on the host and goes through the regress goldens and the CPU checks. The ABI header is the contract.
- **Toolchain:** users do not compile. Third parties build with the JieLi toolchain and the ABI header (GPL).
- **Order:**
  1. the union of engine state;
  2. cut the ABI and run one built-in engine (LOFI) "built in but through the API" to fix the contract;
  3. the loader, the slot area, the `ENG_*` protocol and the safe mode;
  4. the editor list and the CI package build;
  5. move some built-in engines out to packages.
- **Flash benefit [F-E]:** moving rarely used engines out (SLICE 10 KB, WHEEL, TRIO, FORMANT, GRAIN, NOISE…) would make
  the base image another 20–30 KB smaller.

### 1.8 Costs and status

| Item | Figure | Tag |
|---|---|---|
| Plug-in region | 72 KiB (0xEA000–0xFC000) | [F-D]; the region's existence: [V] in both trees |
| Engine sizes | 1.6–8.5 KB each | [F-E] (no method stated) |
| Base-image saving | 20–30 KB | [F-E] |
| Loader, API table, safe-mode size | not given | — |
| CPU overhead | not given | — |
| **Status** | **design document only**: no code on the branch (the 10 commits touch only `docs/`) | [V] `git diff --stat main...origin/research/flash-savings` |

---

## 2. Felucca's plug-ins compared with Optimist's build-time switches

How Optimist works today [V]:
- `registry.h:65-95`: the engines are an X-macro with permanent UIDs 0..12 and fallbacks. `ENG_IS()` folds to 0 for
  an engine that is not built.
- Unity build: `felucca.c` includes every `.c`. ACID and the X0X drums are separate TUs with their own float flags
  (`build.py:190-260`).
- A missing engine plays its fallback, keeps its settings and shows `MISSING:` (`BUILDER.md` "How a switch works").

| Axis | Optimist build-time switches | Felucca-style plug-ins (ported to Optimist) |
|---|---|---|
| **Flash, base image** | An engine left out costs 0 B [V: `verify.py` checks that no symbol of a left-out item stays]. Measured deltas, `costs.json` 2026-10-06 18:35: PHASE 1,464 · LOFI 2,164 · DIGITAL 2,076 · WHEEL 2,344 · VOICE 2,724 · TRIO 3,476 · GRAIN 4,992 · ANALOG 6,288 · PHYS 9,896 · ACID 14,396 · SLICE 27,028 · FM6 33,060 [V] | The loader, CRC, `ENG_*` SysEx, safe mode and API table, plus non-inlined API copies of helpers [E: 2–4 KB, by analogy with the SMP_* upload path and a ~30-entry table]. Engines move out of the 581,564 B slot into **up to 96 KiB elsewhere**: 72 KiB at 0xEA000 + 16 KiB at 0x93000 + 8 KiB at 0xE7000, all unused by Optimist [V `fm1_flash.h:37-45`, `storage.c:19-26`] |
| **Flash, per engine** | the engine alone, inlined | The engine plus its own copies of small helpers, plus `param_desc_t`/presets with absolute pointers. Rounded up to the slot granularity (4 KiB erase sectors) [E] |
| **RAM** | `.bss` and pool exact per build. Engines with static state: WHEEL 2,496 RAM, GRAIN 512 + 21,060 pool, FM6 10,460 + 11,712 pool, PHYS 38,664 pool, ACID 1,704 RAM [V costs.json] | Plug-ins cannot own static RAM [F-D]. Two cases: (a) engines that live in `voice_t.s[8]`/`ph[3]` (`core.h:173-174`): PHASE, LOFI, DIGITAL, TRIO; (b) the rest need a **reserved per-part pool lease that costs RAM in every build**, whether a plug-in is installed or not. The pool is already 10,784 B over in the full default build (ui-feasibility §1.1) |
| **RAMTEXT (speed)** | Most engines run from RAM (`.ram_hot`: ANALOG 2, VOICE, DIGITAL, TRIO, PHASE, WHEEL, SAMPLE, LOFI), so UI font reads cannot evict them [V `core.h:55-69`, MEMORY-BUDGET §4]. RAMTEXT is full (32,604 of 32,512 in the full build [V costs.json base]) | XIP only, as FM6 and GRAIN are today. A copy into a RAM window is possible with a fixed-address link, but RAMTEXT has no room. The XIP-versus-RAM cost is **not measurable in the emulator** (it models no cache; MEMORY-BUDGET §4) |
| **CPU in the audio ISR** | `render`/`note_on`/`amp`/`block`/`post` are already indirect calls through `engine_t` [V `core.h:212-241`], with helpers inlined | **No new indirect call per voice** for the hooks. New: helper calls through `api->` (one per call site in the inner loop unless inlined from a header), the XIP fetch cost, and the loss of cross-TU inlining. The plug-in must declare its cost for `cpuguard`, whose model is per engine (`cpuguard_costs.h`, `costs.json` `cpu.engine_pct`) [V] |
| **Safety: corrupt engine** | Not possible: the image is CRC-checked by the loader as a whole | Header magic, ABI version and CRC at boot. But a *valid* plug-in with a bug can hang the ISR: watchdog → bootguard (`bootguard.h:15-26`: 2 failed boots → recovery) [V]. This needs a "last plug-in disabled" mark in `.noinit`; NOINIT has 948 B free (MEMORY-BUDGET §1) |
| **Safety: missing engine** | `MISSING:` warning, fallback engine, settings kept and written back [V BUILDER.md] | The same machinery can be reused: a plug-in UID that is absent is just "not built". It needs a UID range above 12. Today `upreset.c:46` rejects `engine >= ENG_UID_N`, and names come from `ENG_UID_NAME[]`: a plug-in name has to be stored or shown as `ENG 64` |
| **User experience** | Choose in the builder, rebuild (~10 s), reinstall the whole firmware (OTA path). Needs the toolchain (Docker or local) | **Install an engine from the editor, no toolchain, no reflash of the app.** Survives Optimist updates: the loader writes only [0x4000, 0x93000) [V `ldr_core.c:12-13,33-34`]. The stock firmware's restore would wipe it (its package declares the USR region; `fm1pkg_make.py:136`) |
| **Toolchain** | JieLi clang 4.0.1, unity build; nothing special | **No PIC** [M, §2.1]. Fixed-address link per slot (Felucca), or relocation at install time [E]. ABI headers must be self-contained (today engines `#include` the unity TU's statics) |
| **Tests / goldens** | `regress.c` renders every present preset bit-identical to the full build's goldens; `verify.py` builds profiles and random configs [V BUILDER.md §Verification] | Host: compile the plug-in C against the ABI header, same goldens (integer DSP should stay bit-identical [E]). On target: a new class of test, golden renders in the emulator of host + `.oeng` at each slot address, plus ABI-compat tests (old plug-in on new host). Every host change that touches the API needs the ABI version check in CI |
| **Maintenance** | `engine_t` changes freely (Optimist added `vel_own`, `alloc`, `legato`, `mono_key`, `post`; [V `core.h:232-240`]) | `engine_t`, `voice_t`, `vmod_t`, `param_desc_t`, `preset_t` and the `track_t` access become **frozen ABI**. Optimist is still changing them weekly |

### 2.1 Toolchain probe (measured here) [M]

`scratchpad/picprobe/p.c` is a 13-line engine-like function: a static table, a static variable, a call through an
API pointer. It was compiled with `optimist-toolchain:20250324.1` (`/opt/jieli/pi32v2/bin/clang`, "clang version 4.0.1").

| Try | Result |
|---|---|
| `-Os` (no PIC) | 2 × `R_PI32V2_MOV_ABS32`: the 6-byte `rX = imm32` loads of `state` and `TAB` |
| `-Os -fPIC` | compiles. GOT16 relocations through r9 = r15 (a GOT base register). **Calls through a pointer load a two-word descriptor `{entry, GOT}` (`r3_r2 = d[r2]; r15 = r3; call r2`)**: an FDPIC-style convention, incompatible with the host's plain `call rN` on an `engine_t` pointer |
| `-fropi`, `-frwpi` | `unsupported option … for target 'pi32v2'` |
| `ld -shared` / `ld -pie` on the PIC object | `File format not recognized` (GNU ld 2.26.51.20160621) |
| `ld -Ttext=0x020E6000` on the PIC object | `relocation truncated to fit: R_PI32V2_GOT16 against 'state'` |
| `ld -Ttext=0x020E6000` on the plain object | links. `state` (`.bss`) lands at 0x020E636C, i.e. **in flash**. So "no static RAM in plug-ins" must be enforced by the link script (or by giving `.bss` a RAM address) |
| the same with `--emit-relocs` | keeps `.rela.text` (24 B) and `.rela.rodata` (24 B) |

Conclusions:
- PIC is not usable.
- A fixed-address link works.
- A loader that patches `MOV_ABS32` and data `ABS32` words while it programs flash looks feasible. That would mean **one
  build per engine instead of one per slot** [E: only two relocation types were seen in this tiny probe; a real engine
  needs a full list of relocation types].

---

## 3. Could Optimist adopt runtime-loadable engines?

### 3.1 Verdict: feasible as a complement to the builder; prototype the ABI on the host first

**Why it is feasible**
- The ISR already dispatches engines through function pointers [V].
- The simple engines touch almost nothing global: one or a few `t->p[...]` reads (PHASE 1, DIGITAL 1, TRIO 1,
  LOFI 6 `t->` uses), no `song.`, no `trk[` [V, grep].
- Their dependencies are a short list: `mulq15`, `mulq16`, `clamp`, `osc_sine`, `osc_tri`, `sine_i`, `fine_inc`,
  `amp_at`, `fm_ratio_inc`, tables `SINE` and `PITCH_INC`, constants `VOICE_FS`, `P_E0..7` [V].
- The free flash regions exist and are outside every Optimist writer's allow-list today [V `fm1_flash.h:37-56`].
- The missing-engine path (fallback + `MISSING`) already exists.

**Where it fits**
- It complements the builder; it is not a replacement. The builder stays the way to get a 0-overhead image. Plug-ins
  serve users who do not want to rebuild, third-party engines, and engines that no longer fit the app slot.
- Good candidates: the light, `voice_t`-only engines and new engines.
- Leave as built-ins: FM6 (33 KB, ~10 KB RAM tables, its own UI `ui_fm6.c`), ANALOG 2 (asm kernels, RAMTEXT),
  the drums (not an `engine_t`), ACID (float TU).

**Risks, ranked**

| # | Risk | Why | Mitigation |
|---|---|---|---|
| 1 | **XIP speed against RAMTEXT** | Optimist moved engines into RAMTEXT so that UI drawing cannot evict them; a plug-in goes back to XIP. The emulator counts instructions, not cache misses | A hardware A/B: PHASE built-in (RAMTEXT) against PHASE plug-in (XIP), TIMER4-timed renders with the UI drawing |
| 2 | **ABI freeze** | Five `engine_t` hooks were added recently; `vmod_t`/`voice_t` change with features | Put a `size` field first in the API and in `engine_t`, append-only, and refuse major-version changes. Delay the freeze until the queued folder/UI refactor lands (the source reorg, docs/SOURCE-LAYOUT.md, creates the engine folders, the natural ABI boundary) |
| 3 | **RAM for stateful engines** | No static RAM; a pool lease is reserved in every build | Phase 1: `voice_t`-only engines. A lease only when the union-of-states work is done |
| 4 | **Instruction fetch from the plaintext window** | Proven for data only | The first hardware test of the prototype |
| 5 | **Flash writes and audio** | An erase stops audio for ~50 ms (scene doc §3); CPU1 must be parked in RAM during writes (scene doc §3, DUAL-CORE) | Install only when stopped (as autosave does). Reuse the SMP_* write path, which already handles this |
| 6 | **The update loader's record sweep** | `ldr_records_drop` erases any sector in [0x93000, 0xFC000) whose last 256 B hold a valid record ("TA" tag + CRC16) [V `ldr_core.c:91-103`] | The odds of a false match are ~2^-32 per sector [E]; still, keep a sector's last 256 B as padding or the header's CRC |
| 7 | **Bugs in valid plug-ins hang the ISR** | Bootguard counts failed boots, but does not know which plug-in did it | A "plug-in active" mark in `.noinit` before the first render; recovery disables it |
| 8 | **Licensing** | Optimist and Felucca are GPL-3.0-only; a plug-in loaded into the same address space is very likely a covered work [E, not legal advice] | State it in the ABI header; ship sources with packages |

### 3.2 The minimum prototype that would prove it

Do each stage only if the previous one passes.

1. **P0, host only [E: 1–2 days]**
   - Write `optimist_api.h`: version, size, ~10 helpers and tables.
   - Compile `eng_phase.c` (the smallest, 1,464 B flash and 1,096 B RAMTEXT) as **its own TU** against it, the way ACID
     is built today (`build.py:245-250`).
   - Register it as a built-in engine reached through the API.
   - **Gates:** `regress.c` goldens bit-identical for PHASE's presets; the app-size delta measured (the API table +
     PHASE out of the unity TU); the instructions per sample from `bench.c` at 96 MHz compared with built-in PHASE.
2. **P1, emulator [E: 2–3 days]**
   - Link PHASE at a fixed address 0x020E6000 (flash 0xEA000) with a link script that refuses `.data`/`.bss`. Wrap it
     in an `.oeng` header (magic, ABI, size, CRC, the UID it asks for, state bytes ≤ `sizeof s[8]`).
   - Build a host image without PHASE and with a ~300-line loader that, at boot, scans 0xEA000, checks the header, calls
     `init(api)` and puts the engine at UID 64.
   - Make the package or the emulator preload the flash (no SysEx yet).
   - **Gates:** the same audio as the P0 golden, rendered in the emulator; the boot without the plug-in shows MISSING
     and plays the fallback; a corrupt CRC is refused.
   - Check first that the emulator models the SFC plaintext window for code fetch; not checked here.
3. **P2, hardware**
   - Install with `ENG_BEGIN/WRITE/END`, which needs 0xEA000–0xFBFFF added to `FL_STORE_OK`.
   - **Gates:**
     - the plug-in executes through the plaintext XIP window;
     - render time against built-in PHASE in RAMTEXT, with the UI busy (TIMER4);
     - a deliberately hanging plug-in → recovery disables it on the next boot.
4. **Optional:** install-time relocation, so that one `.oeng` fits any slot.

If P0 shows that the API cuts PHASE's speed or costs more than ~3 KB of base flash, stop there. The builder already
covers the flash use case.

### 3.3 What it means for pluggable UIs

- **Whole UIs: no.**
  - The UI study counted 363 core identifiers and 1,888 references from the UI into the core [X in that study]. A
    function table of that width is a second, fragile ABI.
  - The current UI is ~58 KB [S] + FONT_L, more than the 72 KiB region once engines share it.
  - The UI study's conclusion stands: choose the UI at build time (0 B, 0 cycles).
  - A runtime-loaded UI would also need the full app-side seam (`core_api.h`, steps 1-6) *and* the plug-in ABI.
- **Per-engine UI: yes, with the same mechanism.**
  - A plug-in engine brings its own `page_title`, `edit[8]` labels, `desc()` and presets, so it gets the generic EDIT
    pages for free [V `core.h:212-229`].
  - What it cannot get is a custom screen (FM6's `ui_fm6.c`, graphs). That would need an optional, main-loop-only draw
    hook in the API (gfx `cv_*` primitives through the table), which is safe for the audio because the UI never runs in
    the ISR (ui-feasibility §1.4).
  - Order it after the UI refactor's `core_api.h`, so both share one parameter and metadata contract.
- **Shared groundwork:**
  - The queued folder layout (`engines/<name>/`, `ui/common/`, `ui/optimist/`) and the UI study's seam steps 1-2 are
    prerequisites for both.
  - Do them first; they cost nothing at runtime.

---

## 4. The other findings: flash savings, BLE MIDI, VBUS

### 4.1 Flash-savings measurements and whether they apply to Optimist

Felucca's baseline (1.0.1, `20c275e`) [F-M]: image 476,996 B, slot 581,564 B (the same slot as ours), RAM 88,884 of
98,304, POOL 329,952 of 344,064.

| Felucca item | Felucca figure | Applies to Optimist? |
|---|---|---|
| Extend `minsize` (`size_fns.py SIZE_FILES`) to every non-DSP file | **−3,008 B** [F-M], with `target_budget` unchanged | **Partly.** Optimist's `SIZE_FILES` (`tools/size_fns.py:28-31`) lacks usb, midi_*, panel, lcd, gfx, ota, main, libc… [V]. But Felucca's list includes `seq`, `params` and `engines`, which Optimist deliberately keeps in `AUDIO_FILES` (seq.c runs `keyboard_block` in the ISR). Gain on Optimist unmeasured; likely 1–3 KB [E] |
| `FELUCCA_SIZE=0` against minsize | +8,068 B [F-M] | Already confirmed on Optimist: `SIZE=0` costs 5,920 B (ui-feasibility §1.2 [C]) |
| `-Oz` global −15,268; `-fno-inline-functions` −12,724; `-inline-threshold=0` −4,892 | [F-M], but the CPU budget is broken (`slicer_track` +20 %) | Same toolchain, same trade: **do not adopt** without hardware CPU numbers |
| `-fdata-sections --gc-sections`, `-fmerge-all-constants`, `-fno-jump-tables`, `-fshort-enums`, `-fomit-frame-pointer` | 0 B [F-M] (single TU) | Optimist is a unity build too: expect 0 [E] |
| Per-block functions (`track_render`, `events_block`, …) `noinline` | −3 to −6 KB [F-E] | Plausible for Optimist (`events_block` is 8,230 B, MEMORY-BUDGET §2.1); unmeasured |
| Icon cells Huffman-coded | −10,554 B [F-M] | **Little.** Optimist's icons are SLOOP's 86 × 36 B `ICON_DATA` (3,096) + `ICON_MAP` 1,160 [V `build/gen/felucca_icons.h:96`], not Felucca's 20 KB anti-aliased set |
| Font phases 4→2 | −16,945 B [F-M] | No: a different font (SLOOP bitmap `FONT_S`; `FONT_L` already derived from it at 2×, −24,736 B) |
| `FM6_EXP2` 1025→257 points | −3,072 B [F-M arithmetic] | No: Optimist's FM6 (Melodee/Dexed lineage) has no `FM6_EXP2` [V grep] |
| `SINE` as a 257-point quarter wave | −1,536 B [F-E] | Optimist's `SINE` is 2,048 B in **RAM** `.data` (TAB_RAM, MEMORY-BUDGET §1): it would save flash *and* RAM, against a few instructions per lookup. Worth a measurement |
| CDC console off | −5,524 B [F-M] | Already a switch (USB audio replaces CDC) |
| Samples: PERC → DRUM synthesis | −67,812 B [F-M sizes] | Optimist's equivalent is already a switch (`DRUM_SAMPLED` −99,696 B) |
| FLUTE/SAX as a web "sample pack" + **retired set → the user slot with the same name** | −62,844 B [F-M sizes] | **The idea applies.** Optimist keeps the set number of a left-out set but plays it empty (BUILDER.md). Resolving it by name to a USR slot would make "leave a set out, upload it" lossless for old projects |
| 16 kHz storage of the sample sets | −~28.5 KB [F-E] | Optimist's zones carry `rate` [V `eng_sample.c:81,171`], so it works without a code change; it changes the sound |
| **Unused flash regions** 0x93000–0x96FFF (16 KiB) and 0xEA000–0xFBFFF (72 KiB) | layout read from the SDK [F-D] | **Also unused in Optimist** [V `fm1_flash.h:37-45` allow-list; `storage.c:19-26` map]. Optimist also leaves 0xE7000–0xE8FFF free. Felucca's options: grow the app slot +16 KiB (touches the loader and the installer: "last resort"), a 4th user sample slot (+72 KiB), or engine plug-ins |
| zlib/LZMA for samples; 3-bit ADPCM | rejected (RAM for random access; SAX SNR 15 dB) | The same reasoning holds for us |

### 4.2 BLE MIDI (`ble-route.md`, research §5)

**Measured [F-M]:** the SDK's `apps/demo/demo_ble` (BLE only, no Classic, no SDRAM, FreeRTOS, TRANS_DATA, the closest to
BLE MIDI), linked with the JieLi toolchain at the SDK's `-Oz -flto`:
- flash (code + rodata + RAM init) **280,576 B**;
- static RAM **39,212 B** (`.ram0_data` 13,924 + `.ram0_bss` 25,288).

**Estimated or assumed [F-E]:**
- Heap (BT lbuf/pool, RTOS stacks): +20–40 KB.
- What adding it to Felucca would cost: +240–260 KB flash, +60–80 KB RAM.
- The breakdown by name (BT host 40 KB, controller 25 KB+, OS 27 KB, drivers 65 KB, RF 8 KB, ~25 KB unneeded FAT/update)
  is marked "目安" (rough guide).
- **Interrupt latency against the audio ISR: not measured** ("実機で測るまで不明": unknown until measured on
  hardware). Their phase-1 gate: a throw-away `demo_ble` with the BLE MIDI GATT service and a dummy 70 % ISR load every
  2.9 ms; continue only if the render delay stays within 10 % of the ISR period.

**Decisions [F-D]:**
- No BLE in 1.x.
- If ever, a separate "BLE edition" that drops SLICE and CDC and halves the delay.
- A GPL §7 linking exception or separate distribution for the closed bitcode libraries.
- The draft replies close Issue #17 as "not planned".

**For Optimist:**
- Our `docs/BLE-MIDI-FEASIBILITY.md` §1.5 estimated 120–200 KiB of flash and 30–55 KiB of RAM [I]. Felucca's **link
  measurement (280.6 KB, 39.2 KB static + heap)** is the better number and is higher.
- Optimist's user-default has ~0 B flash free (the handoff note said "~140 B"). A BLE build would need the same scale of trade-offs
  or Felucca's companion route.

### 4.3 VBUS and the USB-companion route (`usb-companion-route.md`)

**Hardware facts as written:**

| Statement | Their status | Comment |
|---|---|---|
| **The FM-1 does not output VBUS** | "確認済" (confirmed) | No method, measurement or commit is cited. Treat it as [F, stated confirmed, unsourced] |
| VBUS *presence* readable on P33 `P3_LDO5V_CON` bit 5 (LDO5V_DET); full charge `P3_CHG_READ` bit 0; edge wake `LDO5V_EDGE_WKUP_EN` | Design text, from commit `cd9d0aa` "VBUS readable through P33 (LDO5V_DET) as a hint, not the role" | Register-level; no hardware read is reported. Their "external power" indicator today is inferred from `usb.config` and "should be replaced" |
| CC1/CC2 do not reach the SoC (UFP with Rd) | "前提" (assumption) | A hardware change would be needed for DRP; rejected |
| The PHY has CPU-forced OTG bits (`CPU_IDDIG`, `CPU_AVALID`, `CPU_SESSEND`, `CPU_VBUSVALID`, `HOST_DISC`), D+/D- pull-downs and a line-state read | Design, from `hal/fm1_usb.h` (MUSB-compatible) | **Untested:** "未確認は 1 点", one point unconfirmed: whether CONN rises in a session with `CPU_VBUSVALID` forced and no VBUS (experiment 1) |

**The route [F-D, F-E]:**
- The FM-1 becomes a USB host to a **battery-powered** ESP32-S3 helper (AtomS3/StampS3; the M5StickC family's USB-C is a
  UART bridge and cannot work).
- The helper handles BLE MIDI, Wi-Fi, storage and the microphone.
- The helper is identified by a 16-byte key in the first IN transfer after enumeration.
- One vendor bulk pair carries frames `[len u16][type u8][seq u8][payload][crc16]`.
- **The FM-1 side is estimated at ~10 KB flash and 2–4 KB RAM [F-E].**
- Three half-day experiments gate it: host session without VBUS; enumeration plus the key; a 1 MB bulk loopback with
  the audio ISR running.
- While in host mode, USB MIDI, CDC and UAC on the FM-1 stop (one core).

**For Optimist:**
- This is the only BLE path that fits our flash.
- It costs a helper device, and USB MIDI/audio to the computer during the session.
- The VBUS fact matters to us whatever happens: any host-mode or "power a dongle" idea needs a self-powered peripheral.

---

## 5. Recommendations

1. **Keep the builder as the primary modularity.** It is already exact, 0-overhead and verified.
2. **Consider plug-ins after** the queued folder layout and UI-seam steps 1-2 land. They define `engines/<name>/` and
   `core_api.h`, which a plug-in ABI would reuse.
3. **Run P0** (PHASE as a separate TU through an API header, goldens bit-identical, size and instruction deltas). It is
   cheap and settles the API's CPU and flash price.
4. **Use install-time relocation instead of one build per slot**, after confirming the full list of relocation types on
   a real engine.
5. **Cheap wins to measure regardless of plug-ins:**
   - more `SIZE_FILES` (main-loop files only);
   - `noinline` on per-block functions;
   - `SINE` as a quarter wave (it frees RAM too);
   - the 72 KiB region as a 4th USR sample slot, if plug-ins do not take it;
   - "retired sample set → a USR slot of the same name".
6. **Update the BLE estimate in `docs/BLE-MIDI-FEASIBILITY.md`** with Felucca's linked figure (280,576 B, 39,212 B
   static), and note the VBUS statement as Felucca's unsourced "confirmed".

# SD mode: the flash for code (design, 2026-10-25)

Phases 1 and 2 are built and host-tested (section 8); nothing has run on an FM-1 yet. The plan for turning the FM-1's user-data flash into code space, with the user data on an SD card
([SD-CARD-DESIGN.md](SD-CARD-DESIGN.md)) or, for testing, a "fake SD mode" that simply keeps very little user data.

## 1. Rulings (the user, 2026-10-25)

- **Flash for code.** The freed flash holds code, run in place (XIP): code there costs no RAM.
- **Hot code loaded on the fly** into the fast area (RAMTEXT) depending on what the project uses.
- **A fake SD mode for testing:** one project (maybe two) in flash, **no user samples**, about **4 presets and 4 kits**.
- **The built-in samples stay in flash** for now (no RAM spent on sample streaming yet).
- **Plugins** (code loaded into RAM from the card): later, maybe.

## 2. Why flash and not RAM

Measured (default profile, `tools/builder/costs.json` and the build of 2026-10-25):

| Region | Size | Free today | What fills it |
|---|---|---|---|
| Flash, app slot | 568 KiB | ~2 KiB | code, tables, built-in sounds |
| Flash, user data | ~340 KiB | (fixed areas) | projects, presets, kits, user samples, snapshots |
| RAMTEXT | 32 KB | 6 KB | every engine's and effect's hot DSP code |
| RAM | 96 KB | 5 KB | state |
| Pool | 336 KB | 7 KB | delay lines, punch, canvas, slicer, reverbs |

All 15 measured engines together are 60 KB of flash, all 11 measured effects 15 KB: code is small. RAM is full of
data (delay lines: 238 KB of pool for the effects). Code loaded into RAM costs RAM byte for byte, so it is a loss while
RAM is this full; code in more flash costs nothing but flash.

## 3. The layouts

| Flash | Today | Fake SD mode (test flavour) | Real SD mode |
|---|---|---|---|
| 0x000000-0x003FFF boot | same | same | same |
| app slot | 0x004000-0x092FFF, **568 KiB** | 0x004000-0x0CFFFF, **816 KiB (+248 KiB)** | 0x004000-0x0DFFFF, **880 KiB (+312 KiB)** |
| user data | 0x093000-0x0DFFFF: FM6 voices, sections log, autosave, USR1-3 samples, snapshots, kits (8 KiB), presets (16 KiB) | 0x0D0000-0x0DFFFF, 64 KiB: 1-2 projects + autosave, 4 presets, 4 kits, FM6 voices of those presets | on the card; flash keeps only what boots without a card (section 5) |
| 0x0E0000-0x0E4FFF update staging | same | same | same |
| 0x0E5000-0x0E6FFF drum records | same | same (or into the 64 KiB) | on the card |
| 0x0E7000-0x0FBFFF SDK, never written | same | same | same |
| 0x0FC000-0x0FEFFF settings A/B | same | same | same |
| 0x0FF000 key_mac | same | same | same |

The 64 KiB of the fake mode is a first guess (the sections log is 32 KiB today for all projects; presets 16 KiB and
kits 8 KiB for the full banks); the real sizes come out of the store objects when phase 2 is built.

## 4. What fixes the app slot's end today

Every one of these says 0x93000 (or 0x8DFBC = 0x93000 - 0x4120 + header) and must move together, chosen by the
build's layout:

| Where | What |
|---|---|
| `firmware/hal/fm1_flash_map.h` | `FL_APP_HI`, and the store's ranges (`FL_UPF_*`, `FL_DATA_*`) |
| `firmware/app.ld` | `XIP LENGTH = 0x8DFBC` |
| `tools/build.py` | `LIMITS["flash"]`, the measurement link's replace |
| `firmware/loader/ldr_core.c` | `LDR_APP_HI`, `LDR_REC_LO` (the update records live above the app), the `fl_size != LDR_APP_HI` check |
| `firmware/loader/loader.c` | `FL_RANGE_OK` |
| `firmware/hal/fm1_flash.h` | `fl_plain_window_init`: the plain (unencrypted) XIP window starts at 0x93000 |
| `firmware/src/system/ota.c` | `OTA_FL_LEN` |
| `tools/fm1pkg_make.py` | `FLASH_SIZE` (and its assert below 0x95000) |
| `tools/fm1_rescue.py` | `APP_END` |
| `firmware/src/io/console.c` | `flr`'s range |
| `firmware/sim/` | the emulator's view (flash size is fine; the store map follows `fm1_flash_map.h`) |
| `tests/upfm6_move_test.c`, `tests/ldr_test.c` | the map's checks |

Phase 1 makes `FL_APP_HI` the one source (a `FELUCCA_LAYOUT` value) and derives the rest from it.

## 5. Install, switch, fall back

- **The stock V15 loader writes exactly 0x93000 B.** A first install from stock must be a normal-layout Optimist;
  its own loader can then install a big-layout build. The loader learns a second accepted `flash.bin` size and
  writes past 0x93000 only for a package that says it is big-layout.
- **Switching layouts destroys the user data in the overlap.** Normal to big: the app overwrites the store. The
  installer must say so and ask for a backup first (the web editor's backup, or later the card). Big to normal: the
  store area holds code: the store must start empty (its objects fail their CRC and read as empty, which the store
  already handles; to be checked).
- **The chip's SPL boots the app from the flash head's JLFS entries**, which the loader never writes. Whether the SPL
  checks or maps only the app length the head declares (V15's 0x93000) is **unknown**: if it verifies a CRC over that
  length only, the extra code simply is not verified; if it maps only that length, the big layout fails to boot.
  This is the first thing to test (phase 2, a build that puts a canary function past 0x93000).
- **The plain XIP window** (data above the app read unencrypted) moves to the new app end; the package tool already
  encrypts the whole `flash.bin` with the chip key, so a longer one should encrypt the same way (to be checked).
- **Real SD mode without a card:** settings stay in flash (0xFC000); the working project's autosave and one project
  stay in flash too, so the FM-1 still plays and saves its current project with no card.

## 6. Hot code on the fly (RAMTEXT overlays)

Today every engine's hot code (`HOT`, the sound loop) is copied into RAMTEXT at boot, whether a project uses the engine
or not; RAMTEXT (32 KB) is the real limit for DSP code and the SD card does not change it. Overlays make it per project:

- **Resident:** the mix, the drums' loop, the FX buses, the flash driver (`.ram_text`): what every project needs.
- **Engine slots:** the rest of RAMTEXT split into N slots (4 parts: 4 slots of about 3-4 KB, *estimate*: ANALOG's hot
  code is 4.7 KB, most engines 1-3 KB).
- **Linked N times:** each engine's hot code is linked once per slot address (GNU ld `OVERLAY`), so it runs from any
  slot without position-independent code. That costs flash (an engine's hot code x N, e.g. 4 x 3 KB), which the big
  layout now has.
- **Loaded at project load and at an engine change:** the part's sound stops for the copy (a few KB from XIP flash:
  well under a millisecond, *estimate*), the slot's code is copied, the part's hooks point at it. With the second core
  (`DUAL`) rendering parts 2-3, the copy waits until CPU1 is between blocks.
- **An engine too big for a slot** (FM6, ACID) takes two adjacent slots, or stays resident.

Gain: RAMTEXT holds only the engines the project plays, so more (and bigger) engines can be built in. Cost: a little
flash per engine and a loader of a few hundred bytes.

## 6b. Memory per project (the arena)

With every engine and effect built into the bigger flash, their RAM can no longer be reserved statically: each would
keep its state and lines whether a project uses it or not (the effects alone reserve 238 KB of pool today). Today's
sharing (`part_mem`: one engine's state per part, 38.6 KB; the effects arena `DLY_ARENA`: the delay's line and the
inserts' lines) becomes general:

| Part | What | How |
|---|---|---|
| Resident | the mix, the sequencer, the screen canvas, USB's rings, the RAM flash driver | static, as today |
| Project arena (pool) | each part's engine state, the insert / effect lines, the reverb and delay lines; later the sample preload and stream buffers, plugin code | laid out per project |
| RAMTEXT slots | the hot code of the engines the project plays (section 6) | the same planner |
| Undo history | a fixed reserve of 5 levels, always kept (the user, 2026-10-25), plus what the arena leaves | the planner never lays out into the reserve |

- Every engine and effect declares what it needs: state bytes, line bytes, hot-code bytes, an init.
- At a project load or an engine / effect change (main loop, the part silent for the change) a planner adds the
  needs and lays them out with a bump allocator rebuilt on each change; nothing is allocated or freed while the
  audio runs; the audio reads pointers.
- What does not fit is refused with a MEM message, as MEM FULL is today; nothing is left half loaded. "Fits" means
  with the undo reserve left over: 5 levels (a level of a rewritten 64-step track is about 650 B, `MEMORY-MAP.md`
  2.1, an estimate; the reserve is sized from the largest level `undo.c` can record, so about 4-8 KB).
- First the arena for the state and the lines (measurable alone: the pool most projects never touch), then the
  RAMTEXT overlays, then the card.
- What does not fit (the user, 2026-10-25): refused, and the device proposes what to drop; on a project load the user
  picks one or more items to drop until it fits (rare: every FM-1 has the same RAM). A dropped part becomes a **silent
  placeholder** (the user, 2026-10-25): it keeps its sequence, plays nothing, shown dimmed as having no engine, until
  the user picks an engine for it. On an edit the proposal is the UI's one confirm (red: it replaces a part's sound;
  "DROP T2 FM6, 9.7 KB"); on a load, a list of the candidate parts the keys toggle, a gauge that says when it fits,
  SAVE to confirm.

**Built (phase 3a, 2026-10-25):** `firmware/src/core/arena.c`, the parts' engine memories (`part_mem`: FM6, PHYS,
GRAIN, ACID, PLAITS) placed in it. By default the arena is exactly the old fixed memory (4 x the largest engine's) at
fixed slots: the same room and places, never refused (pool unchanged, +96 B RAM, +620 B flash on the default profile);
`FELUCCA_ARENA_BYTES` smaller places by need (first fit, blocks never move, a part's own block free for its own next
engine, idle blocks reclaimed without asking). The main loop reserves before a part's engine changes (model.c
set_engine_of, project load, user preset load); a refusal records the part, the engine and `arn_drop_for`'s proposal in
`part_mem_refused`; the audio keeps the old engine when a switch finds no room. Not yet: the UI that shows the proposal
and lets the user pick what to drop, a builder item for the arena's size, the engines' and effects' other static state.
Tests: `tests/arena_test.c` (first fit and fixed slots; 300,000 random switches with some left pending), the backports
test in a two-part arena (FM6, PHYS, ACID swapped on a part).

**Built (the silent placeholder, 2026-10-25):** `track_t.silent`; `part_drop` (model.c) puts a part on the first engine
that keeps nothing in the arena (its block idle, taken back when needed), notes into it play nothing (voice.c
trk_note_on), the UI shows NONE in grey with no sound name; a project or section saves its engine byte as
`ENG_UID_SILENT` (0xFE; an older firmware plays its stand-in for an unknown engine) and a load restores the placeholder;
picking an engine or loading a user preset ends it. `tests/placeholder_test.c`. Not yet: the drop picker that calls
part_drop (the confirm on an edit, the list on a load).

**Built (phase 3b, the effects' lines, 2026-10-25):** `firmware/src/fx/fx_arena.c`. With `FELUCCA_FX_ARENA=1` and an
arena placed by need (`FELUCCA_ARENA_BYTES`), the effects' big lines are blocks of the same arena, after the parts': the
chorus, delay and reverb sends' lines (`cho_buf`, `dly_buf`, `rev_line`) and the limited inserts' own room (`fxa_ins`).
The default build keeps them static as before (its image byte-identical; no builder item yet: a measurement build sets
`OPTIMIST_ARENA_BYTES` and `OPTIMIST_FX_ARENA=1`, `tools/build.py`).
- **Used** = a send's type in a master slot, or its line lent to the inserts (or wanted for an owner starved of a type it
  would hold); the own room while any owner holds a limited type. Only those hold a block.
- **Placed** in the main loop (`fxm_poll`, every pass before `fxi_poll`): reserved and taken with the interrupts off,
  zeroed and the bus at rest while the bus is gated (`fxm_off`: `fx_ret_block` runs no bus whose line is not placed,
  `rev_guard` keeps the reverb off), then the gate opened. The largest line first; the lines placed best fit from the
  arena's top (`ARN_TOPS`), the engines first fit from its bottom, so the two cut each other's room up less.
- **No longer used**: gated, given back two audio blocks later (CPU1's kept reverb block, `REV_LATE`, has run by then);
  the own room once the last instance in it has faded out. The audio runs on with the pointer 0 (tested).
- **No room** (the ruling for parts carried over): the effect is **bypassed where it is** (a send's return adds nothing,
  the dry signal passes; a limited insert FULL, as when the effects memory is full), **kept in the project**, shown dim
  (the master slot's card: FULL, struck through), MEM FULL said once, and **placed by itself as soon as there is room**
  (tried every pass, never while a part's refused change waits: a part's sound first). When the free room is only cut
  up, the send lines whose buses are silent are gated, given back and placed again with it, the largest first, if a
  trial on the arena says that makes it fit (nothing heard is lost: a silent bus's line is all zeros).
- **Parts first**: a part's refused change proposes parts only (`ARN_DROPPABLE`); one refused while an effect's line is
  on its way back waits for it (`model.c mem_retry`, `fxm_pending`) and goes through when that makes room.
Default profile (`synths_USBaudio_BTaudio_BTmidi`, measured 2026-10-25): FX_ARENA off: flash, RAM, pool unchanged. An
arena of the same room (38,664 B of engines + the 64 + 4 + 32 KB lines = 141,064 B): flash +2,140 B, RAM +256 B, pool
the same (of which the engines' arena by need alone: +200 B flash, -16 B RAM). A 96 KB arena: pool -42,760 B, the undo
ring 12,136 -> 54,896 B. Not yet: a builder item for the arena (its size, and FX_ARENA), the proposal of an effect to drop
for a part (open: the user's call), the punch ring, the slicer's buffers and the reverbs' small cells (static, as before).
Tests: `tests/fx_arena_test.c` (ROOM; ROOM + FDN8 + SPRING), `tests/arena_test.c` (best fit from the top, never named).

## 7. Phases

| Phase | What | Device risk |
|---|---|---|
| 1 (built) | `FELUCCA_LAYOUT` (builder item `LAYOUT`, bit 410): the app slot's end and the store map from `fm1_flash_map.h` / `tools/flash_layout.py`; the normal layout's package byte-identical from the same app; the normal image +36 B (its updater accepts either layout's package) | none |
| 2 (built) | The **fake SD mode** (`sd-mode-probe` profile): the 816 KiB slot, the squeezed store (a 7-sector project log at 0xD0000: about 5 dense scenes a project; autosave A 0xD7000; FM6 voices 0xD8000; kits and presets unchanged), no user samples / snapshots / CZ collection, the update loader and the rescue tool clearing the other layout's sectors on a switch, the far probe | the user's install: the user data in the overlap is lost; the far probe answers the SPL question |
| 3 | The arena (section 6b), then the RAMTEXT overlays (section 6) | none beyond a normal build |
| 4 | Real SD mode: the user data on the card (`SDCARD`), flash keeps settings + one project | needs the SD mod |
| 5 | Plugins (code from the card into RAM), if ever needed | |

Who does what: Claude writes and host-tests every phase and hands over packages; the user installs them and runs the
checks. Claude never writes to the FM-1.

## 8. Installing the probe (phase 2)

The packages of 2026-10-25 (`worktrees/sd-packages/`, git-ignored): `1-normal-sdcard-branch.fwsc` (the default profile
built from this branch: a normal layout whose updater accepts the bigger package) and `2-sd-mode-probe.fwsc` (layout 1).

1. **User**: backs up with the web editor (projects, presets, kits). Installing layout 1 erases the user samples,
   the snapshots and the old project log; going back to a normal build later erases layout 1's project log.
2. **User**: installs `1-normal-sdcard-branch.fwsc` (any installer). An older Optimist refuses the bigger package
   (-15), so this step comes first.
3. **User**: installs `2-sd-mode-probe.fwsc`.
4. **User**, at the serial console (`tools/fm1_console.py`): `far`. `ok: code and data past 0x93000 work` answers the
   question of section 5; `the constant reads wrong` means the chip does not decrypt (or map) past V15's length:
   install `1-normal-sdcard-branch.fwsc` again (the probe build still updates: nothing it needs is past 0x93000).
5. **User**: saves a project, powers off and on, loads it (the squeezed store), saves a preset.

What was checked before (host only): the probe's package decoded with the chip key (app area to 0xCF25B, the far
section at flash 0x94120, the VM descriptor at 0xD0000); the probe boots to its main screen in the emulator
(`~/GitHub/fm1-emulator` diagnose); the update loader's whole test with a normal device and the probe package, and the
layout switch both ways (`tests/ldr_test.c --layout`); the rescue tool's switch both ways (`tests/rescue_test.py`); the
updater accepting both sizes (`tests/ota_test.c`); the layouts' numbers in agreement (`tests/flash_layout_test.py`);
the patterns and project tests on the 7-sector log.

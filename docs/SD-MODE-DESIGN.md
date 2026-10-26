# SD mode: the flash for code (design, 2026-10-25)

Phases 1 and 2 are built and host-tested (section 8); nothing has run on an FM-1 yet. The plan for turning the FM-1's user-data flash into code space, with the user data on an SD card
([SD-CARD-DESIGN.md](SD-CARD-DESIGN.md)) or, for testing, a "fake SD mode" that simply keeps very little user data.

## 1. Rulings (the user, 2026-10-25)

- **Flash for code.** The freed flash holds code, run in place (XIP): code there costs no RAM.
- **Hot code loaded on the fly** into the fast area (RAMTEXT) depending on what the project uses.
- **A fake SD mode for testing:** **one project** in flash (the user, 2026-10-25: the web app and the iOS app move a
  project to and from the FM-1 over USB, so they keep the others; `PROJ_SLOTS` 1: the PROJECT page shows one slot, the
  editor's project command answers 1 for another, a backup's PJ02..PJ16 are not restored), **no user samples**, about
  **4 presets and 4 kits**.
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

**Built (phase 3b, 2026-10-25; builder item `OVERLAY`, bit 412, needs `LAYOUT` 1):** host-tested and run in the
emulator, not on an FM-1.

- **Marking.** An engine's hot code is `OVL(name)` (it was `HOT`) or `OVLX(name)` (code that runs from XIP without the
  overlays: STRINGS so far); its sound hooks (render, block, amp, post) are a table, `OVL_ENGINE`, the first bytes of
  its code; its `engine_t` names them through `OVL_FN` (0 with the overlays). Without the item every macro is what
  it replaced: the default build is byte for byte the same (but the build time string). Marked: ANALOG (2), DIGITAL,
  PHASE, LOFI, SAMPLE's render, VOICE, TRIO, WHEEL, SUPER (the old ANALOG builds), STRINGS (OVLX).
- **One link per slot instead of `OVERLAY` with copies.** GNU ld cannot link one input section at several addresses;
  `tools/build.py` (with `tools/ovl_link.py`) links the firmware once per part (`NPART` = 3): the engines in the
  slots are one `OVERLAY` statement at slot k's address in link k, nothing else moves. Each engine's code is taken
  from each link (its copy for that slot) and the resident image (`.text`, `.data`, `.ram_text`, `.ram_hot`,
  `.ram_hot2`, `.pool_init`) is compared across the links: a resident reference to an overlay's code fails the
  build. `NOCROSSREFS` refuses one engine calling another's. No position-independent code; measured: a slot's copy
  differs from another slot's only in its hooks' table (the calls are PC-relative).
- **Slot per part, size chosen by the build** (`ovl_link.plan`): 3 slots of S bytes plus every `OVL` engine larger
  than S kept resident (an `OVLX` one larger stays in XIP), S the engine size with the least RAMTEXT (a tie: the
  larger S). Deviation from the sketch: no "two adjacent slots" (a part's slot is its own; a big engine stays
  resident), because slots shared between parts would need a placement that can refuse a project (open question in
  the report). `OPTIMIST_OVL_SLOT=N` forces S for a test build.
- **The table** (`_ovl_rom`, after the far probe's `.text_far` past 0x93000: the big layout only): each engine's
  place (RAMTEXT, XIP, or the slots: its code once per slot). `core/overlay.c` checks it at boot (magic, the slots'
  address and span, each table's tag); refused, the overlay engines play nothing (the console's `ovl`: `ovl_bad 1`).
  If the chip does not map the flash past 0x93000 (the far probe's question), this is what happens.
- **Protocol** (`core/overlay.c`): the audio (`track_render`, either core) calls a part's hooks only while
  `rto_eng[k]` names the part's engine, else the part plays nothing that block (its voices wait) and asks
  (`rto_want`). The main loop (`rto_poll`, every pass and in the wait loop) loads: `rto_eng[k]` = none, the copy,
  then the engine. CPU1 renders only inside CPU0's audio interrupt (fork / join), so it is never mid-block while the
  main loop copies. An engine change needs nothing more: the fade, the switch, the first block asks. At power-on,
  `rto_boot` loads each part's engine before the audio.
- **Measured (2026-10-25).** Today's profiles: the plan finds no gain (ANALOG 2's 5,152 B x 3 slots is more than
  every engine together) and keeps every engine resident: `sd-mode-probe` + OVERLAY: RAMTEXT 26,424 B (26,188
  without; +236 B: the hooks' lookup in the resident render), .text +1,024 B (the loader, the console's ovl), the
  boot stream +138 B, the table 36 B, RAM +112 B. The default profile in the
  normal layout: refused (builder rule, and `build.py`). A build with more small engines (`sd-mode-probe` + PHASE,
  LOFI, VOICE, WHEEL, STRINGS, without PLAITS): without the overlays RAMTEXT overflows by 696 B; with them it
  links, RAMTEXT 32,384 of 32,512 (3 slots of 2,432 B: DIGITAL, WHEEL, VOICE, LOFI, PHASE and STRINGS, which moves
  from XIP into RAM; ANALOG 2 resident), the engines' RAMTEXT 12,448 B instead of 13,588; flash +30,100 B (the table
  and the copies). Copy time: 10 us for DIGITAL's 1,336 B in the emulator (no flash wait model); on the FM-1 an
  estimate of 20-100 us for a 2.4 KB slot (32 ns a flash read, 2026-10-18's tabbench, plus line fills), the
  console's `ovl` prints the real `copy_us_max`.
- **Tests:** `tests/overlay_test.c` (not loaded: silent, no call into the slot, asked, loaded; every overlay engine
  bit for bit from its slot as in place; a change while playing; 30,000 random changes), `tests/regress.c` built
  with the overlays (the 108 goldens unchanged), `tests/ovl_link_test.py` (plan, scripts, table). Emulator: bench 12
  (PHASE on the three parts, CPU1 rendering) with the overlays and without: the same samples for 6 s; with
  `FELUCCA_BENCH_SWAP=1` (each part to DIGITAL and back every phrase, 18 loads in 11 s) the same samples too.

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
set_engine_of, project load, user preset load); a refusal records the part and the engine in
`part_mem_refused`; the audio keeps the old engine when a switch finds no room. Not yet: the UI that shows the proposal
and lets the user pick what to drop, a builder item for the arena's size, the engines' and effects' other static state.
Tests: `tests/arena_test.c` (first fit and fixed slots; 300,000 random switches with some left pending), the backports
test in a two-part arena (FM6, PHYS, ACID swapped on a part).

**Built (the silent placeholder, 2026-10-25):** `track_t.silent`; `part_drop` (model.c) puts a part on the first engine
that keeps nothing in the arena (its block idle, taken back when needed), notes into it play nothing (voice.c
trk_note_on), the UI shows NONE in grey with no sound name; a project or section saves its engine byte as
`ENG_UID_SILENT` (0xFE; an older firmware plays its stand-in for an unknown engine) and a load restores the placeholder;
picking an engine or loading a user preset ends it. `tests/placeholder_test.c`. The drop pickers that call
part_drop: the one confirm on an edit (op_input.c mem_tick), the list on a load (below).

**Built (the load's list, 2026-10-25):** a project load (`project.c proj_apply`, `engines.c part_mem_load_begin`,
`part_mem_load`) records every part it cannot place (`part_mem_refused.lmask`, the engine each wants in `leng`; two
refusals on one load once overwrote each other and the first part kept its old engine with the new sound) and makes
each a silent placeholder with the project's sound; a switch that still finds no room in the audio (`voice.c`, an idle
state taken back by another part's reservation) does the same (`part_mem_unplaced`), never keeping the old engine.
The effects are not placed while a load's refusals wait (`PART_MEM_SHORT`). `model.c mem_load_settle` (each frame)
waits for the parts' switches and the lines on their way back, then places each refused part that fits by itself;
what is still short opens the UI's list (`ui/optimist/op_memlist.c`, docs/UI-OPTIMIST-DESIGN.md 4.13): the parts that
hold memory, then the send lines placed (REV DLY CHO), toggled on the keys; the refused parts are on the keys too,
first: picked, one is left silent (a placeholder) so that the others can be placed (the user, the night of 2026-10-25); `mem_load_fits` tries the commit on the
arena itself (the dropped parts' blocks idle as after their switch, the lines given back, the reservations in the
settle's order, then put back), so FITS on screen is what SAVE gets (`mem_load_commit`, then the settle); HOME
(`mem_load_cancel`) leaves the placeholders. An edit keeps the one confirm. Only with an arena placed by need (the
default's code: -60 B flash, -16 B RAM; with ARENA 96: +2,916 B flash, +32 B RAM, pool the same, measured
2026-10-25 on the default profile). Tests: `tests/placeholder_test.c` (two parts refused on one load, settled by itself,
SAVE, HOME), `tests/fx_arena_test.c` (the parts then REV DLY CHO, an effect dropped from the list; loads in the random
switches: every FITS committed placed), `tests/ui_optimist_memlist.h` (the page).

**Built (phase 3b, the effects' lines, 2026-10-25):** `firmware/src/fx/fx_arena.c`. With `FELUCCA_FX_ARENA=1` and an
arena placed by need (`FELUCCA_ARENA_BYTES`), the effects' big lines are blocks of the same arena, after the parts': the
chorus, delay and reverb sends' lines (`cho_buf`, `dly_buf`, `rev_line`) and the limited inserts' own room (`fxa_ins`).
The default build keeps them static as before (its image byte-identical). The builder's `ARENA` (bit 411, KB; 0 off)
sets both: `FELUCCA_ARENA_KB` -> `FELUCCA_ARENA_BYTES` and `FELUCCA_FX_ARENA` (registry.h); its pool estimate is
`core.py arena_mem` (the arena less the fixed memories it replaces), its code and RAM unmeasured in costs.json yet (a
gpufarm run); a measurement build can also set `OPTIMIST_ARENA_BYTES` and `OPTIMIST_FX_ARENA=1` (`tools/build.py`).
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
- **Parts first, then the effects** (the user, 2026-10-25): a part's refused change proposes the parts first
  (`model.c mem_offer`: parts only), then, in the same confirm, the effects' send lines placed and not
  lent, the largest first ("DROP REV 32 KB", `fxm_offer`); a dropped effect is bypassed as when refused (`fxm_drop`:
  gated, given back two blocks later, FULL, kept in its slot, back by itself once there is room). The inserts' own room
  is not offered (its owners hold their types). A part's change refused while an effect's line is on its way back
  waits for it (`model.c mem_retry`, `fxm_pending`) and goes through when that makes room.
Default profile (`synths_USBaudio_BTaudio_BTmidi`, measured 2026-10-25): FX_ARENA off: flash, RAM, pool unchanged. An
arena of the same room (38,664 B of engines + the 64 + 4 + 32 KB lines = 141,064 B): flash +2,140 B, RAM +256 B, pool
the same (of which the engines' arena by need alone: +200 B flash, -16 B RAM). A 96 KB arena: pool -42,760 B, the undo
ring 12,136 -> 54,896 B; the builder's ARENA 96: flash +2,384 B, RAM +240 B, pool -42,760 B.

**Built (the other buffers, 2026-10-25):** `firmware/src/core/arena_bufs.c` (engines.c `ARB_*`, after the effects'
lines; only with `FELUCCA_FX_ARENA`, the default build byte-identical). The big buffers only some projects use are
blocks of the arena too, held only while the project uses their feature, placed from the arena's top as the lines:

| Buffer | Bytes | Held while | Refused (no room) |
|---|---|---|---|
| each track's SLICER recording (`slicer.c sl_bp`, `FX_SLICER`) | SL_LEN x 2 (8,192) | its SLICER is on STUT, or a repeat still plays | STUT bypassed: the live sound, as OFF; its card FULL, dim, struck through (op_cells.c) |
| WAVE's table: its copies and the frame builder's scratch (`eng_wave.c wt_mip wt_fr wt_fi`, `ENG_WAVE`) | 73,216 | a part plays WAVE (or is switching to it) | the change to WAVE refused as any engine's: the drop proposal, parts first, then the effects; on a load the list page (the table its need) (the night's ruling, 2026-10-25); only if it is lost after that: silent, MEM FULL WAVE |
| the X0X 909's / 808's state (`x0x_drums.c x0x_mem`, `DRUM_X0X909` / `808`) | 5,344 / 3,376 | the kit or a lane plays the machine, or one of its voices sounds | its hits play the stand-in kit (as the notes the machine lacks) |
| the ROOM's diffusers (`fx.c rev_ap`, `REV_ROOM`) | 1,994 | in the reverb's block, after its line (fx_arena.c) | as the reverb |
| FM6's sine table (`eng_fm6.c FM6_SIN`, `ENG_FM6`; 2026-10-26) | 4,100 | a part plays FM6 or is switching to it (set aside with the part's own memory) | the change to FM6 refused as any engine's (the drop proposal); only if lost after that: FM6 silent, MEM FULL FM6 |
| the PUNCH ring (`punch.c punch_ring`, `FX_PUNCH`; 2026-10-26) | PUNCH_N x 2 (32,768 / 65,536) | the project's PUNCH switch is on (GLOBAL > Rec), or an effect still fades out | the punch bypassed (the mix as it is), MEM FULL PUNCH once; FX + a key: let go |

- The main loop only (`arb_poll`, every pass after `fxm_poll`): placed (the interrupts off), zeroed, the feature's state
  reset, then its pointer set (the interrupts off); no longer used: its pointer 0 at once, the block given back two
  audio blocks later (`fxm_tick`); used again meanwhile: the same block, zeroed again.
- No room (WAVE's table aside, above): bypassed where it is, kept in the project, MEM FULL SLICER / WAVE / X0X said once (op_input.c mem_tick,
  `arb_said`), placed by itself once there is room (tried every pass, never while a part's refused change waits or the
  lines are being moved), the largest first. A part's change refused while one of them is on its way back waits for it
  (`fxm_pending`). They are never proposed to drop.
- Left static (no gain): the screen canvas, USB's and Bluetooth's rings, the storage buffers, the undo ring (every project); the
  reverbs' small state (`rv`, `rvj`: a few hundred bytes, in main RAM, read every block).
- Default profile (`synths_USBaudio_BTaudio_BTmidi`, measured 2026-10-25): ARENA off: the image identical but its build
  time. ARENA 96: flash +852 B, RAM +104 B, pool -8,720 B (the X0X machines) against ARENA 96 before; against the
  profile without the arena: flash +3,236 B, RAM +344 B, pool -51,480 B, the undo ring 12,376 -> 63,512 B. With ROOM,
  the SLICER and WAVE too (a measurement build): ARENA 96 pool -159,460 B (436,476 -> 277,016 B: the same pool as without
  them). The builder's estimate (core.py `arena_mem`, `arena_bufs`, `rev_ap`) gives both deltas to the byte.
- Tests: `tests/arena_bufs_test.c` (each buffer placed, used, given back, refused and bypassed, said once, placed by
  itself; a part's change waiting; 20,000 random switches), `tests/fx_arena_test.c` (the ROOM's diffusers in the reverb's
  block; without the SLICER: no other buffer).
Tests: `tests/fx_arena_test.c` (ROOM; ROOM + FDN8 + SPRING), `tests/arena_test.c` (best fit from the top).

**The core and the extras (2026-10-26; the user: "the arena will merge into main, default on; not yet").** The arena is
split so that every build can have it and only the SD card mode pays for the rest:

| | What | Where | Builds |
|---|---|---|---|
| Core | the parts' engine states placed by need (`core/arena.c`, `engines.c part_mem_*`), the edit's one confirm ("DROP T2 FM6", `op_input.c mem_tick`), a load's refused parts offered one by one on that confirm with the project's sound kept (`model.c mem_load_next`) | `FELUCCA_ARENA_KB` | every build ARENA is set in |
| Extras | the effects' lines (`fx/fx_arena.c`), the other buffers (`core/arena_bufs.c`: SLICER, WAVE, X0X, rev_ap), the load's list page (`op_memlist.c`, `model.c mem_load_*`) | `FELUCCA_FX_ARENA`, `FELUCCA_MEM_LIST` | the SD card mode: `FELUCCA_SD_MODE` (registry.h), LAYOUT 1 until the SD card mode's own builder flag; one macro to rewire |

- A load with the core alone: `proj_apply` makes each part it cannot place a silent placeholder (`lmask`), as before; each
  frame `mem_load_next` turns the first into the edit's refusal (`part_mem_refused.keep`): `mem_retry` waits for every
  part's switch (the old blocks free after), places it with the project's sound if it fits now, else the confirm
  proposes what to drop; declined to the end: MEM FULL, the part stays a placeholder; then the next.
- What to propose (both): `arn_drop_for`'s search over every set of parts is gone; `mem_offer` proposes first the parts
  whose memory alone would make room (`engines.c part_mem_frees`: `arn_fits` with that part's block given back, the
  parts dropped before idle by then), then any other, one drop at a time. Two drops that only help together are
  proposed one after the other (the second named once the first is idle).
- Slimmed (measured on the reference profile, 2026-10-26, a core arena of 40 KB: flash over the profile without it):
  the split itself +3,196 -> +740 B (the list page and the load's settle out of the core); the load's settle replaced
  by the edit's wait -152; `arn_drop_for` replaced by `part_mem_frees` -216; `arn_scan` without the sort or the copies
  (`arn_end`: each place tried against every block) -8, the same result (2,000,000 random cases against the old one);
  the idle mask one helper (`arn_idle`); the arena's state one struct (`arn_st_t`: the trials copy it once) +8 in the
  core, -296 B with the extras (all the above).
- Measured (2026-10-26, exact builds): the reference profile `synths_USBaudio_BTaudio_BTmidi` (normal layout, slot
  581,564 B): off flash 581,508, RAM 91,808, pool 328,888; the core (ARENA 24) flash 581,880 (+372, **316 B over the
  slot**), RAM 91,804, pool 314,800 (-14,088: one PHYS part's 12,888 B and two FM6 parts' fit); the core and the extras
  (a measurement build, ARENA 96 with `OPTIMIST_FX_ARENA=1`) flash 588,220, RAM 92,156, pool 277,408. `sd-card-mots`
  (LAYOUT 1, slot 765,696 B): off 743,824 / 106,640 / 313,848; the core alone (`OPTIMIST_ARENA_BYTES=24576`) 744,224 /
  106,652 / 317,352; ARENA 96 (core and extras) 750,484 / 106,960 / 279,960.
- The builder: `ARENA` (KB) is the core in a normal build and the core and the extras in the SD card mode
  (`core.py sd_mode`, `arena_mem`, `arena_blocks`); new sizes 24 .. 48 KB for the core alone (the parts' fixed memory
  is 3 x the largest engine's: 38,664 B with PHYS). Off (0) stays the fixed memories, for a tight profile.
- Tests: `tests/placeholder_test.c` built four ways (fixed; the core: the load's refusals on the confirm; with the list;
  with the extras: the PUNCH switch through a save and a load).

**The PUNCH switch (3a, 2026-10-26; an extra).** The punch-in FX records the mix all the time (a punch needs the past),
so its ring cannot wait for the key: a project setting says the project uses it. `FELUCCA_PUNCH_SW` (registry.h: PUNCH
built and the arena's extras): `punch_sw`, GLOBAL > Rec's PUNCH (TEMPO's REC 2 page with REC MODES), OFF in a new project;
saved in the project's G_INFO slot, bit 1 = ON (bit 0 stays REC QUANTIZE's). The ring is the arena buffer `ARB_PUNCH`
(`core/arena_bufs.c`), held while the switch is on or an effect still fades out; not placed, `punch_process` leaves the
mix as it is and the main loop lets go of FX + a key (`punch_asked`): PUNCH OFF said with the switch off, MEM FULL PUNCH
once when refused. An extra and not the core: the ring is one of the other buffers (their placement, `arb_poll`, is the
extras' code), and without the arena's extras the ring stays static and the punch always there, no switch (a normal
build gains nothing from it: the ring is in its pool either way).
- **Older projects load with PUNCH OFF** (0 in that bit; recommended and built: nothing a project stores uses the punch,
  a live control, so it plays the same; the user turns PUNCH on in the projects played live). A build without the switch
  reads bit 0 only and saves its projects with the bit clear.
- Measured (2026-10-26, `sd-card-mots` with FX_PUNCH): the static ring does not fit there (SRAM overflowed by 17,344 B);
  with ARENA 96 it builds: flash 752,424 (+1,940 over ARENA 96 without PUNCH: the punch's code and the switch), RAM
  107,024, pool 279,960 (the same: the ring only in the arena).
- Tests: `tests/arena_bufs_test.c` (off: no ring, FX + a key let go, PUNCH OFF; on: placed, recorded, REVERSE plays;
  off while it plays: kept until faded out, then given back), `tests/placeholder_test.c` (the extras' build: the bit
  through a save and a load, an older project off).

**FM6's sine table (3b, 2026-10-26; an extra).** `eng_fm6.c FM6_SIN` (1,025 x int32, built at boot until now) is the
arena buffer `ARB_FM6SIN` with the extras: held while a part plays FM6, is switching to it or fades out of it; placed by
the main loop, built there (`fm6_sin_fill`, the interrupts on) before its pointer is set; not there, FM6 is silent
(`fm6_block` skips the controllers and the LFO, `fm6_render` writes nothing: as WAVE while its table is built). WAVE's
table's handling made general (`engines.c part_tab_k`, `part_tab_bytes`, `part_mem_aside`): a change to FM6 sets the
table aside with the part's own memory when no block holds it, and is refused as any engine change when the two do not
fit (`part_mem_frees`: a part proposed first when its block makes room for both); the load's list counts one table for
all the parts of an engine. An extra, not the core: the buffer's placement is the extras' code (`arb_poll`), and the
core alone is already over the reference profile's slot; for a normal build it would give 4 KB of pool back to the undo
history (a later step if a flash round pays for it).
- Measured (2026-10-26): `sd-card-mots` ARENA 96: flash 751,012 (+528), RAM 107,000 (+40), pool 275,864 (-4,096: the
  pool's packing of the 4,100 B array); the reference profile, a measurement build with ARENA 96 and the extras: flash
  588,740 (+520), pool 273,312 (-4,096). The builder's estimate (`core.py arena_bufs`, `FM6_SIN`) gives the pool to the
  byte on `sd-card-mots`.
- Tests: `tests/arena_bufs_test.c` (no part on FM6: no table; FM6 picked: set aside with the part's memory, placed,
  built as the boot's, heard; the tests' arenas grown by the table's 4,104 B), `tests/fx_arena_test.c` (the random
  switches 30,000: the loads' lists rarer with the table).

**The other engines' state (3c, 2026-10-26; the core, and every build).** A survey of every engine's static state
(an all-engines build's symbols, `sd-card-mots` with every `ENG_*`): what each part keeps for its engine in a static
array outside the parts' memory. Moved into `engines.c part_mem_t` (a member of the union each, its accessor after it:
`juno_mem`, `drw_mem`, `cz_mem`, `slc_mem`; zeroed when a part comes to the engine, `part_mem_claim`), so it is held only
by the parts that play the engine, and with ARENA placed by need. None is the union's largest (FM6's 3,904 B a part,
PHYS's 12,888), so the fixed slots do not grow: the static arrays' RAM is simply given back, in every build that has
the engine, ARENA or not:

| Engine | Moved | Bytes (3 parts) | Why it is safe |
|---|---|---|---|
| JUNO | the voices' state, the HPF, the RES tables' cache (`juno_v`, `juno_h`, `juno_rc`) | 1,332 (RAM) | a new voice clears its state; `res1` 0 = the cache built again |
| DRAWBAR | the part's and the voices' state (`drw_t`, `drw_v`) | 2,496 | `init` 0 = a voice set up at its first block |
| CZ | the voices, the tone, the envelope points, the EDIT values (`cz_v`, `cz_tone`, `cz_defs`, `cz_ed`) | 3,120 | `cz_key` stays static (nbank.c clears it for every part) and is cleared at the claim (`cz_forget`): the tone built again |
| SLICE | the voices' reverse windows (`slc_rbuf`) | 3,072 | a note starts with no window (`s[0]` 0x7FFFFFFF) |

Left (with why): DRONE (`drone_pt`, `drone_side`: 852 B, 284 B a part: below what pays for the change), ANALOG 2's
feedback lines (`a2_fb`: 576 B; the default engine, most parts play it: a block each anyway), FM6's EDIT values
(`fm6_ed`, 1,056 B: a project's, kept whatever the part plays) and its SysEx bank (`fm6_pick`, 4,097 B: a transfer
buffer of the editor, not a part's: a later arena client of the main loop, if one is wanted), STRINGS' LFOs (24 B),
FM6's per-part counters (`fm6_pt` etc., small, read by the claim), the voices themselves (`trk`, every engine's shared
`voice_t`, 8.7 KB). The other engines (ANALOG, DIGITAL, PHASE, LOFI, TRIO, FORMANT, SUPER, the Clipper engines, SID,
PROPHET, NOISE, PLAITS, PHYS, GRAIN, ACID, FM6, WAVE) keep their state in the voices or already in the part's memory.
- Measured (2026-10-26, ARENA off): the reference profile RAM 91,792 -> 90,448 (-1,344: JUNO), flash -24; `sd-card-mots`
  RAM 106,640 -> 96,604 (-10,036: JUNO, DRAWBAR, CZ, SLICE), pool the same. The builder (`core.py PART_MEM`) has the four
  sizes; their items' measured RAM in costs.json still counts the arrays until a gpufarm re-measure.
- Tests: the engines' goldens and health (`tests/cz_test.c`, `juno_test.c`, the regressions with every engine built),
  `tests/nbank_test.c` (the CZ collection: the part's tone through `cz_tone(p)`).

## 7. Phases

| Phase | What | Device risk |
|---|---|---|
| 1 (built) | `FELUCCA_LAYOUT` (builder item `LAYOUT`, bit 410): the app slot's end and the store map from `fm1_flash_map.h` / `tools/flash_layout.py`; the normal layout's package byte-identical from the same app; the normal image +36 B (its updater accepts either layout's package) | none |
| 2 (built) | The **fake SD mode** (`sd-mode-probe` profile): the 816 KiB slot, the squeezed store (a 7-sector project log at 0xD0000: about 5 dense scenes a project; autosave A 0xD7000; FM6 voices 0xD8000; kits and presets unchanged), no user samples / snapshots / CZ collection, the update loader and the rescue tool clearing the other layout's sectors on a switch, the far probe | the user's install: the user data in the overlap is lost; the far probe answers the SPL question |
| 3 | The arena (section 6b, built), then the RAMTEXT overlays (section 6, built: `OVERLAY`) | none beyond a normal build |
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

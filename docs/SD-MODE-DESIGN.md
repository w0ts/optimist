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

## 6. Hot code on the fly (RAMTEXT overlays): retired 2026-10-30

Built, then retired with its builder item `OVERLAY` (bit 412): the packed SRAM runs all its code at one speed and
gives what is free to the arena, the slots cost each engine's code three times in flash, and the release build never
linked them. Each engine's builder item places its hot code instead (on, or on with the code in RAM). The design
below is kept as it was.


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
- **The table** (`_ovl_rom`, after the far probe's `.text_far` in the slot's last 64 KB: the big layout only): each engine's
  place (RAMTEXT, XIP, or the slots: its code once per slot). `core/overlay.c` checks it at boot (magic, the slots'
  address and span, each table's tag); refused, the overlay engines play nothing (the console's `ovl`: `ovl_bad 1`).
  If the chip does not map the flash past 0x93000 (the far probe's question, no longer asked before a build: FAR_OK retired 2026-10-29), this is what happens.
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
- After 3a-3c (2026-10-26, exact builds): the reference profile off 581,456 / RAM 90,448 / pool 328,888; the core
  (ARENA 24) 581,840 (+384, **276 B over the slot**) / 90,460 / 314,800; core and extras (ARENA 96, the measurement
  build) 588,716 / 90,852 / 273,312. `sd-card-mots`: off 743,800 / 96,604 / 313,848; the core alone (24 KB) 744,108 /
  96,632 / 317,352 (its parts' fixed memory is 21,072 B: a core arena there pays from 20 KB down); ARENA 96 750,904 /
  96,980 / 275,864.
- The builder: `ARENA` (KB) is the core in a normal build and the core and the extras in the SD card mode
  (`core.py sd_mode`, `arena_mem`, `arena_blocks`); new sizes 24 .. 48 KB for the core alone (the parts' fixed memory
  is 3 x the largest engine's: 38,664 B with PHYS). Off (0) stays the fixed memories, for a tight profile.
- Tests: `tests/placeholder_test.c` built four ways (fixed; the core: the load's refusals on the confirm; with the list;
  with the extras: the PUNCH switch through a save and a load).

**The PUNCH switch (3a, 2026-10-26; an extra).** The punch-in FX records the mix all the time (a punch needs the past),
so its ring cannot wait for the key: a project setting says the project uses it. `FELUCCA_PUNCH_SW` (registry.h: PUNCH
built and the arena's extras): `punch_sw`, GLOBAL > Tune's PUNCH (TEMPO's REC 2 page with REC MODES), OFF in a new project;
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

**Built (the arena in the free RAM, packing, 2026-10-29; the user: "one project/FX memory setting with packing against
fragmentation", "arena first, undo gets the rest", "never a drop-out while it plays"):**
- **One switch.** `ARENA` is on or off (an older configuration's KB reads as on: registry.py WAS_COUNT). On, the arena is
  the SRAM the link leaves free (engines.c `ARN_FREE`, core/arena.c `arn_boot`: the pool's end to the stacks' guard
  band less the build's 8 KiB spare), taken at power-on; it holds the parts' engine states, the effects' lines and the
  other buffers in every ARENA build (registry.h `FELUCCA_FX_ARENA`, the SD card mode's only before). The builder's one
  memory question is whether it links; its notice says what a new project needs of the arena together. Off: the fixed
  memories as before. The host tests: a stand-in of `FELUCCA_ARENA_BYTES` (96 KB by default).
- **The undo history.** It keeps 16 KB after the arena (`ARN_UNDO_KEEP`: a dozen levels at the largest record, 1,353 B,
  far more step edits) and borrows what the arena leaves free at its top (seq/undo.c `undo_yield`, `undo_regrow`): a
  block placed there makes it give way, its oldest levels first (all of them when the ring wraps over the newest); the
  arena is never refused for it.
- **The send lines** are placed best fit from the arena's top as before (no fixed places), the parts first fit from its
  bottom. **The limited inserts' own room** is fm1-next's: one block of the effects memory (`DLY_ARENA`) less the delay's
  line while a limited type is held.
- **Packing** (core/arena.c `arn_pack`, `arn_poll`): a refusal that the free bytes would hold (a part's change, a load's
  part, a send line, the inserts' room, another buffer) plans moves on a copy of the arena, the fewest in this order: the
  send lines and the room up to the highest place when that leaves a larger gap, then the parts' blocks down in address
  order. Only what is quiet moves (`ARN_CAN`, fx_arena.c `arn_can`): a part while it plays nothing (no voice, no note
  queued, no switch), a send line while its bus is silent (gated two blocks first: CPU1's late reverb block has run), the
  room while none of its lines is heard; at a project load every part and the room. A refusal whose plan a sounding
  block blocks stays a refusal (MEM FULL and the drop proposal as before) and the arena is packed once that block is
  quiet (`arn_pk.want`, tried every 32 passes). A move copies the contents (no tail or state lost) in windows of at most
  2 KB and 40 us with the interrupts off (the block period is 726 us), started just after an audio block, then swaps its
  place and pointer with the interrupts off; its target and its client are checked again first.
- **Sizes** in whole 256 B (`ARN_ALIGN`; measured against 64 and 1024 B in the first round).
- **SYSTEM > INFO's RAM bar** (op_project.c `info_ram`): drawn as the CPU bars, RAM with the used / total KB; the arena's
  blocks in use the fill, the undo history's share a dimmer segment after it, the rest free; the bright tick the last
  ~10 s's most in use, the dim one the most since boot (system/overload.h's RAM ring, sampled with the CPU's). Without an
  arena the SRAM in use at link. The stacks: the label amber and STACK LOW when one has had under 1 KB left. Its line
  ARENA MOVE: the longest move / the longest interrupts-off window (us).
- **The stress test** (`tests/arena_frag_test.c` on gpufarm: the jam profile's engines, sends and inserts, `DLY_ARENA` 64;
  20,000 random steps with step edits and undos, 3 seeds summed; fm1-next 1451bf56 against this; at 64 KB the delay's
  line 8192 samples in both). 103,680 B and 104,704 B are the arena the jam profile (crashhist) and sd-card-mots get on
  this firmware (the free SRAM of their builds less the spare and the undo's 16 KB), 179,968 B the dev profile with
  ARENA. fm1-next at those sizes is its arena of that many bytes (its undo ring: what its link leaves free, apart).

| arena | tree | parts + loads + sends refused | of them a packed layout would place | insert lines refused | insert owner-steps unserved | mean free / hidden B | worst hidden B | moves | undo ring mean / least B |
|---|---|---|---|---|---|---|---|---|---|
| 64 KB | fm1-next | 2615 (1612 / 125 / 878) | 368 | 8795 | 51.1 % | 8477 / 2041 | 23176 | 0 | - |
| 64 KB | final, the buffers just over what is in use (ARN_TOPS_HW 1) | 3029 (2011 / 149 / 869) | 647 | 8802 | 51.1 % | 12196 / 3744 | 27392 | 930 | 21080 / 16384 |
| 64 KB | final | 2995 (1992 / 155 / 848) | 315 | 8795 | 51.1 % | 9563 / 1059 | 21504 | 980 | 16409 / 16384 |
| 96 KB | fm1-next | 2693 (1676 / 111 / 906) | 1005 | 2774 | 18.9 % | 11741 / 3261 | 32440 | 0 | - |
| 96 KB | final, the buffers just over what is in use (ARN_TOPS_HW 1) | 2654 (1694 / 133 / 827) | 1291 | 2787 | 18.7 % | 12207 / 4046 | 33280 | 790 | 20563 / 16384 |
| 96 KB | final | 2491 (1516 / 126 / 849) | 572 | 2771 | 18.6 % | 8974 / 1426 | 28416 | 878 | 16414 / 16384 |
| 103680 B (101.2 KB) | fm1-next | 2343 (1376 / 89 / 878) | 1213 | 2732 | 18.4 % | 13583 / 4853 | 35104 | 0 | - |
| 103680 B (101.2 KB) | final, the buffers just over what is in use (ARN_TOPS_HW 1) | 2275 (1300 / 104 / 871) | 1060 | 2729 | 18.4 % | 12892 / 4303 | 33792 | 822 | 19876 / 16384 |
| 103680 B (101.2 KB) | final | 1930 (1025 / 85 / 820) | 426 | 2728 | 18.3 % | 10571 / 1860 | 33024 | 853 | 16627 / 16384 |
| 104704 B (102.2 KB) | fm1-next | 2001 (1129 / 80 / 792) | 772 | 2710 | 18.3 % | 12784 / 3832 | 32064 | 0 | - |
| 104704 B (102.2 KB) | final, the buffers just over what is in use (ARN_TOPS_HW 1) | 1883 (1014 / 83 / 786) | 680 | 2731 | 18.4 % | 12214 / 3859 | 35840 | 640 | 19382 / 16384 |
| 104704 B (102.2 KB) | final | 1910 (1014 / 89 / 807) | 453 | 2729 | 18.3 % | 10978 / 1906 | 33024 | 889 | 16692 / 16384 |
| 128 KB | fm1-next | 868 (186 / 19 / 663) | 235 | 2719 | 18.2 % | 26754 / 11292 | 52648 | 0 | - |
| 128 KB | final, the buffers just over what is in use (ARN_TOPS_HW 1) | 846 (150 / 10 / 686) | 268 | 2712 | 18.3 % | 28698 / 9627 | 48896 | 585 | 31495 / 16384 |
| 128 KB | final | 1592 (1035 / 82 / 475) | 325 | 2713 | 17.6 % | 21508 / 3523 | 47104 | 1108 | 16613 / 16384 |
| 179968 B (175.8 KB) | fm1-next | 0 (0 / 0 / 0) | 0 | 2648 | 17.3 % | 55429 / 17491 | 91344 | 0 | - |
| 179968 B (175.8 KB) | final, the buffers just over what is in use (ARN_TOPS_HW 1) | 11 (0 / 0 / 11) | 11 | 2652 | 17.3 % | 55908 / 17617 | 78848 | 231 | 45259 / 16896 |
| 179968 B (175.8 KB) | final | 0 (0 / 0 / 0) | 0 | 2648 | 17.3 % | 55886 / 10659 | 79104 | 260 | 17892 / 16384 |

(An insert line refused: an owner newly left without one, counted alike in both trees. The undo ring's mean and least
over the steps: the least is its 16 KB. ARN_TOPS_HW 1, the buffers placed just over what is in use so the undo history
borrows more, measured as the third row of each size and left off: more bytes hidden in gaps at every size.)

## 7. Phases

| Phase | What | Device risk |
|---|---|---|
| 1 (built) | `FELUCCA_LAYOUT` (builder item `LAYOUT`, bit 410): the app slot's end and the store map from `fm1_flash_map.h` / `tools/flash_layout.py`; the normal layout's package byte-identical from the same app; the normal image +36 B (its updater accepts either layout's package) | none |
| 2 (built) | The **fake SD mode** (`sd-mode-probe` profile): the 816 KiB slot, the squeezed store (a 7-sector project log at 0xD0000: about 5 dense scenes a project; autosave A 0xD7000; FM6 voices 0xD8000; kits and presets unchanged), no user samples / snapshots / CZ collection, the update loader and the rescue tool clearing the other layout's sectors on a switch, the far probe | the user's install: the user data in the overlap is lost; the far probe answers the SPL question |
| 3 | The arena (section 6b, built), then the RAMTEXT overlays (section 6, built, retired 2026-10-30) | none beyond a normal build |
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

## 9. MEMORY steps by sample slot (proposal 2026-10-30; steps 1-3 of 9.6 built 2026-10-30)

The user's idea (2026-10-30): more choice between MEMORY "normal" and "more flash", chosen by how many user-sample
slots the flash keeps rather than by how many projects. Every number is from the code at `fm1-next` 7cd4d5fe
(file:line) unless marked *estimate*.

**Built (branch `fm1-next-memsteps`, 2026-10-30), for the classic build: 9.6's steps 1, 2 and 3.** The far probe is
gone (`far_probe.c`, the console's `far`, `.text_far`, `far_off`, `FAR_RESERVE`): LAYOUT 1's code limit is its
whole slot, 831,420 B. One table, `tools/flash_layout.py` `STEPS`, generates `firmware/hal/fm1_steps.h` (checked
byte for byte by `tests/flash_layout_test.py`); the other copies (`registry.py` `LAYOUT_STEPS`, the installers' size
lists in `fm1_route.py` / `fm1_install.py` (imported), `web/install_route.js` and `InstallRoute.swift` (checked)) come
from it. S0 is LAYOUT 3, MEMORY 4 "no user samples" (3 stays free for S1); S1 and S0-20 are not built. The release
waits (9.4, last rows): `gen_optimist.py` has `S0_APP_END` and refuses LAYOUT 3, the builder marks it NOT_BUILT for
the release, and fm1-base's side is a proposed branch, `pr/app-end-table` in the PR clone (felucca-bsp's
`LDR_STEPS`, fm1-base's `app_steps`), not wired in.

Corrections to the text below, from the code:

- **One more case in the rule (9.3):** two builds of one step (the classic L1 at 0xD0000, the release's at 0xCE000)
  are the same step: only the old code's tail is cleared (as before), not [E, max(R)), which would have erased the one
  project the two builds share. `switch_clear_ends` and the loader identify a step by its end in `FL_ENDS`.
- **ota.c step 1 takes the classic steps' ends only** (0x93000, 0xB5000, 0xD0000), not the release's 0xCE000: the
  installers' lists say "what Optimist's updater takes"; an fm1-base package was refused later than step 1 on
  2026-10-29 (-16, unexplained), so widening step 1 would not let it through either.
- **fm1-base's `write_sizes`** is not on the branch the app-end PR is based on (`feat/app-end-934e40d`); its fix
  (`app_slot` of the real app_end) waits for the branch that brings it.

### 9.0 What the normal layout's data area really holds

Two facts change the starting point of the idea:

- **"Normal = 2 sample slots + 16 projects" holds only for a build with user samples.** With `PATTERNS` the project log
  is slot USR1 (0xA0000, 20 sectors) only when `ENG_SAMPLE` or `DRUM_USR` is built; without either it is 32 sectors,
  128 KiB, 0xA0000-0xBFFFF (`firmware/src/storage/sections/sec_log.c:29-39`). **Every profile in `config/profiles/`
  builds `ENG_SAMPLE=0`, `DRUM_USR=0`, `SNAPSHOTS=0`, `PATTERNS=1`** (the `.config` files, e.g.
  `synths_USBaudio_BTaudio_BTmidi.config`), so on the user's own builds 0xC0000-0xD7FFF (96 KiB) is unused flash today,
  and so are the old project slots 0x97000-0x9EFFF (32 KiB, `storage.c:55-56`), which a `PATTERNS` build does not read
  (`docs/MEMORY-MAP.md`, section 1).
- **USR3 is 64 KiB, not 80:** it ends at the banks, 0xD8000 (`eng_sample.c:50`), less the snapshots and the CZ
  collection (`eng_sample.c:51`, `registry.h:588`, `registry.h:596`). USR2 is the only full 80 KiB slot left in a
  `PATTERNS` build (`eng_sample.c:59`).

The normal layout's data area, `PATTERNS` on (`fm1_flash_map.h:44-48`, `storage.c:20-31`, `storage.c:55-56`,
`eng_sample.c:41-61`, `sec_log.c:29-39`, `snap_store.c:20-21`):

| Flash | With user samples | Without (every profile today) |
|---|---|---|
| 0x93000-0x94FFF | the app slot's room to grow, free (`storage.c:69-71` asserts it) | same |
| 0x95000-0x96FFF | the presets' FM6 voices (`FL_UPF`, `fm1_flash_map.h:46-47`) | same |
| 0x97000-0x9EFFF | the old project slots, not read with `PATTERNS` | same |
| 0x9F000 | autosave copy A (`storage.c:55`) | same |
| 0xA0000-0xB3FFF | the project log, 20 sectors (USR1) | the project log, 32 sectors, to 0xBFFFF |
| 0xB4000-0xC7FFF | USR2, 80 KiB | log to 0xBFFFF, then free |
| 0xC8000-0xD7FFF | USR3, 64 KiB less snapshots and CZ at its top | free |
| 0xD8000-0xD9FFF | free (the retired FM6 bank); the release: `OBJ_PANEL` | same |
| 0xDA000-0xDFFFF | kits (`storage.c:42`), presets (`storage.c:120`) | same |

Project room: 20 sectors hold about 66 KiB, 32 about 108 KiB (`docs/PROJECTS-DESIGN.md:115`); a busy project is about
6-8 KiB (*estimate*, same line). So 16 project slots is the address limit, not a promise: 20 sectors fit about 8-11 busy
projects, 32 about 13-18 (*estimate*, from those two numbers). A full log says MEM FULL; it loses nothing.

### 9.1 The steps

The app grows up from 0x4000, so every step past normal moves what sits just above 0x93000: the FM6 voices, autosave
copy A and the project log. Each new step stacks them, from the top down, under what it keeps, and the app ends below
them. The old project slots are dropped (`ST_PROJECTS` 0, as LAYOUT 1 does: `storage.c:51-53`).

**One end for both builds.** In LAYOUT 1 the release ends 8 KiB lower than the classic build (0xCE000 against 0xD0000),
because the FM6 voices took 0xD8000, where the release keeps `OBJ_PANEL` (`fm1base/optimist/ui/opt_storage.h:118-133`,
`fm1base/optimist/app.h:30-33`). The new steps keep 0xD8000-0xD9FFF free in Optimist's map (as normal does), put the FM6
voices at the app's end instead, and so give the classic build and the release the **same** end: the release keeps
`OBJ_PANEL` at 0xD8000 (its calibration survives every switch between N, S1 and S0), and own <-> release in the same
step clears nothing. The cost: 8 KiB the classic build could have had (variant: FM6 voices at 0xD8000 in the classic
build only, ends 8 KiB higher, the release 8 KiB lower, as LAYOUT 1).

App slot = end - 0x4000 - 0x120 - 0xF24 (`tools/flash_layout.py:11-12`, `:27-29`; fm1-base the same,
`fm1base/tools/fm1pkg_make.py:27-28`, `:105-106`).

| Step (LAYOUT) | App end (classic / release) | App slot, B (classic / release) | vs normal | Kept above the app | Projects |
|---|---|---|---|---|---|
| **N** normal (0) | 0x93000 / 0x93000 | 581,564 / 581,564 | - | the table above | 16, 20 or 32 sectors |
| **S1** one sample slot (new, 2) | 0xB1000 / 0xB1000 | 704,444 / 704,444 | +122,880 (120 KiB) | FM6 voices 0xB1000-0xB2FFF, autosave A 0xB3000, log 20 sectors 0xB4000-0xC7FFF (USR2's exact place), **USR3 0xC8000-0xD7FFF where it is** (snapshots, CZ at its top as today), 0xD8000 free / `OBJ_PANEL`, kits, presets | 16, 20 sectors (about 66 KiB) |
| **S0** no user samples (new, 3) | 0xB5000 / 0xB5000 | 720,828 / 720,828 | +139,264 (136 KiB) | FM6 voices 0xB5000-0xB6FFF, autosave A 0xB7000, log 32 sectors 0xB8000-0xD7FFF, 0xD8000 free / `OBJ_PANEL`, kits, presets | 16, 32 sectors (about 108 KiB: today's room on every profile) |
| S0-20 (variant of S0) | 0xC1000 / 0xC1000 | 769,980 / 769,980 | +188,416 (184 KiB) | FM6 voices 0xC1000-0xC2FFF, autosave A 0xC3000, log 20 sectors 0xC4000-0xD7FFF, the rest as S0 | 16, 20 sectors (about 66 KiB) |
| **L1** more flash (1, as today) | 0xD0000 / 0xCE000 | 831,420 / 823,228; the classic **code limit today 765,696** (`FAR_RESERVE`, below) | +249,856 / +241,664 | log 7 sectors 0xD0000-0xD6FFF (`sec_log.c:22-28`), autosave A 0xD7000 (`storage.c:52`), FM6 voices 0xD8000 (`fm1_flash_map.h:41-42`); the release's `OBJ_PANEL` 0xCE000-0xCFFFF | 1 (`registry.h:719`) |

Against the user's guesses: S1 is 688 KiB of app slot (guess "about 650"); the step with no user samples is 704 KiB with
today's project room or 752 KiB with the smaller log (guess "about 730"); L1's app slot is 812 KiB of app.bin (816 KiB
is the whole slot [0x4000, 0xD0000)). The guesses counted 80 KiB a slot; the steps also take the 8 KiB room to grow,
the old project slots' 32 KiB and the FM6 voices' old place.

**What each step loses, against normal:** S1: USR1 (already the log) and USR2; USR2's samples, and any project that
plays them reads MISSING. S0: every user sample slot (`SMP_USER_CAP` 0 as LAYOUT 1, `eng_sample.c:55`), the snapshots
and the CZ collection (their area is in USR3: `snap_store.c:27`, `nbank.c:187`). L1: as S0, and 15 projects. Every
step keeps: kits, presets, the settings and autosave copy B (0xFC000-0xFEFFF), the drum records (0xE5000-0xE6FFF).

**Why S0 at 32 sectors and S0-20 as a variant.** S0 at 32 sectors costs the user's profiles nothing at all (they build
no user samples, no snapshots and have 32 sectors today) and gives +136 KiB. S0-20 buys 48 KiB more for about 42 KiB
less project room. S0 (32) ends only 16 KiB above S1, which keeps a 64 KiB sample slot: S1 is the step for someone who
uploads samples, S0 for everyone else.

**The build rules per step** (`core.py:265-273`, `storage.c:46-49` for LAYOUT 1): S1 allows `ENG_SAMPLE`, `DRUM_USR`,
`SNAPSHOTS` and the CZ collection (all inside USR3, as today), needs `PATTERNS`; S0 and S0-20 as LAYOUT 1 (no user
samples, snapshots or CZ collection, `PATTERNS` on). `ENG_SAMPLE` with its built-in sets and every slot empty could be
allowed in S0 (`SMP_USER_CAP` 0 already does it for LAYOUT 1); the rule refuses it today for LAYOUT 1 (`storage.c:46`).

### 9.2 Alignment, the slot boundaries, the far probe

- **4 KiB is enough.** Every end above is a 4 KiB boundary. fm1-base takes any 4 KiB boundary from 0x93000 to 0xE0000
  (`fm1base/tools/build.py:1310`); the loaders erase 4 KiB sectors; the plain XIP window starts at the app's end
  (`fm1_flash.h:253`), and the normal layout's 0x93000 (XIP 0x0208F000, aligned to 4 KiB but not 8 KiB) runs on every
  FM-1. The installed slot's end is read back as the app area head's end rounded up to a sector (`fm1base/bsp/loader/
  ldr_core.c` `ldr_slot_switch`; `tools/fm1_layouts.py:68-77`): the tail after app.bin is 0xF24 B, under a sector, so the
  rounding finds the slot end exactly for any step.
- **The 80 KiB slot boundaries (0xA0000, 0xB4000, 0xC8000) are not good app ends.** An end there leaves no place for
  the FM6 voices (8 KiB) and autosave A (4 KiB) except 0xD8000-0xD9FFF, which is 8 KiB and is the release's
  `OBJ_PANEL`. The steps end 12 KiB below a boundary instead (S1: 0xB1000 = 0xB4000 - 12 KiB), which keeps the
  remaining slot (USR3) at its address: its samples and snapshots survive N <-> S1.
- **The far probe's 64 KiB (`FAR_RESERVE`) is no longer needed.** It held the far probe (`firmware/src/system/far_probe.c`,
  a few words) and the overlays' table; the overlays are retired (section 6), the user ruled on 2026-10-29 "drop the far
  probe, we don't need it" (`tools/flash_layout.py:32-34`), `FAR_OK` is gone (`registry.h:724`), and the release's
  0xCE000 image has run on the user's FM-1 (`registry.py:1180`). Yet `code_limit(1)` still keeps it
  (`flash_layout.py:42-45`): today's LAYOUT 1 code limit is 765,696 B (`costs.json` `limits.flash_by_layout`), **less
  than S0-20's 769,980**. Retiring it (`far_probe.c`, `felucca.c:233-235`, the console's `far`, `build.py:464-475` and
  `:529-537`, `FAR_RESERVE`, `far_off`) gives LAYOUT 1 its whole 831,420 B and frees the new steps from it. (Stale on
  the way: `far_probe.c:2-3` says app offset 0x90000; `far_off()` puts it at 0xBAF00, flash 0xBF020; `registry.py:1188`
  says "about 765 KB".)

### 9.3 What a switch erases or keeps

The new package's own update loader clears, before it writes the app, what the old step kept differently from the new
one. One rule covers every pair: with each step's **relocated-store top** R (the top of what it stores elsewhere than
normal: N 0x93000, S1 0xC8000, S0 and S0-20 0xD8000, L1 0xDA000), a switch from step A to step B writes
[0x4000, E_B) and erases [E_B, max(R_A, R_B)). Classic build (the release the same, but L1 at 0xCE000):

| from \ to | N | S1 | S0 | L1 |
|---|---|---|---|---|
| **N** | writes 0x4000-0x92FFF | writes to 0xB0FFF; clears 0xB1000-0xC7FFF | writes to 0xB4FFF; clears 0xB5000-0xD7FFF | writes to 0xCFFFF; clears 0xD0000-0xD9FFF (as today) |
| **S1** | writes to 0x92FFF; clears 0x93000-0xC7FFF | writes 0x4000-0xB0FFF | writes to 0xB4FFF; clears 0xB5000-0xD7FFF | writes to 0xCFFFF; clears 0xD0000-0xD9FFF |
| **S0** | writes to 0x92FFF; clears 0x93000-0xD7FFF | writes to 0xB0FFF; clears 0xB1000-0xD7FFF | writes 0x4000-0xB4FFF | writes to 0xCFFFF; clears 0xD0000-0xD9FFF |
| **L1** | writes to 0x92FFF; clears 0x93000-0xD9FFF (today 0x93000-0xCFFFF only) | writes to 0xB0FFF; clears 0xB1000-0xD9FFF | writes to 0xB4FFF; clears 0xB5000-0xD9FFF | writes 0x4000-0xCFFFF |

What it means:

- **The projects are lost on every switch between two different steps** (the log moves each time), and so are the
  FM6 voices and autosave copy A. The working project probably comes back from autosave copy B at 0xFE000, which a
  switch never touches: the store writes the other copy each time (`storage.c:331`), so B holds the last or the one
  before (*not tested* across a switch).
- **Kept by N <-> S1:** USR3 and the snapshots (and the CZ collection), the same address and format in both.
- **Kept by every switch:** 0xDA000 and up (kits, presets), 0xE0000 and up (staging, drum records, the SDK's sectors,
  settings, autosave B, key_mac): no clear reaches past 0xD9FFF (asserted in `fm1_layouts.KEEP`, `fm1_layouts.py:28`).
- **The release's `OBJ_PANEL` (0xD8000)** survives N <-> S1 <-> S0; a switch to or from L1 loses it (it moves to 0xCE000).
- **Own <-> release in the same step:** nothing cleared for N, S1, S0 (one end); L1 as today
  (`docs/INSTALL-PATHS.md`, the switch matrix: own-big -> base-sd clears 0xCE000-0xCFFFF).
- **L1 -> N changes from today:** it would also clear L1's squeezed store 0xD0000-0xD9FFF, which normal reads as USR3's
  top and the free banks (today they read as empty: no sample or snapshot magic). Harmless either way; the rule is simpler.

**Backup advice**, shown with every MEMORY change (it is already the item's notice, `registry.py:1145-1148`, to be
reworded per step): before installing another step, **User** backs up with the web editor (Backup: the projects,
presets, kits, the user sample slots and the snapshots, `ed_backup.c:6-12`, `:217-225`) and, for a way back whole, a full flash
backup (`python tools/fm1_anyflash.py backup`); after the install, **User** restores the projects with the editor
(only the slots the new step has: S1 refuses USR1/USR2, S0 and L1 every slot; L1 takes one project).

### 9.4 The code changes

One table drives everything: the steps' (end, R, release end) live in `tools/flash_layout.py` and
`firmware/hal/fm1_flash_map.h`, and `tests/flash_layout_test.py` checks the two agree, as today.

| Where | Function / name | Change |
|---|---|---|
| `firmware/hal/fm1_flash_map.h:28-51` | `FELUCCA_LAYOUT`, `FL_APP_HI`, `FL_UPF_LO/HI`, `FL_DATA_LO` | branches for 2 (S1) and 3 (S0; S0-20 a fourth if wanted); a new `FL_RELOC_HI` (R); a constant list of every step's (end, R) for the loader; `FL_APP_HI_BIG` stays the largest end (0xD0000) |
| `firmware/src/system/ota.c:324` | `ota_stage`, step 1 | `fl_len` must be one of the list's ends (the classic ones and the release's 0xCE000), not two constants; `:342` (the official loader: normal only) stays. Not settled by this: the 2026-10-29 refusal of an fm1-base package was -16 (`docs/INSTALL-PATHS.md:66`), a check after the size check, unexplained, so widening the size check alone does not let fm1-base packages through |
| `firmware/loader/ldr_core.c:148-168` | `ldr_layout_switch` | table-driven: the installed end E_A = the head's end rounded up to a sector; R_A from the list; erase [E_B, max(R_A, R_B)). No head of ours: as normal (clears nothing more, as today); an end not in the list (a newer step): erase [E_B, 0xDA000), the safe side. `LDR_APP_HI` / `LDR_REC_LO` (`:41-45`) and the size check (`:201`) stay |
| `firmware/src/storage/storage.c:46-56`, `:69-71` | `ST_AUTOSAVE_A`, `ST_PROJECTS`, the `#error` rule, the asserts | per step: autosave A 0xB3000 / 0xB7000 (0xC3000), `ST_PROJECTS` 0; the room-to-grow assert (`ST_UPF_SECTOR >= FL_APP_HI + 2 sectors`) becomes `ST_UPF_SECTOR == FL_APP_HI` for the new steps |
| `firmware/src/storage/sections/sec_log.c:22-46` | `SEC_LOG_BASE`, `SEC_LOG_SECTORS` | S1 0xB4000 x 20, S0 0xB8000 x 32 (S0-20 0xC4000 x 20); asserts: below 0xC8000 in S1, below 0xD8000 in S0 |
| `firmware/src/engines/sample/eng_sample.c:52-61` | `SMP_USER_CAP` | S1: slots 0 and 1 hold nothing, slot 2 as today; S0: none (as LAYOUT 1) |
| `firmware/src/core/registry.h:697`, `:719` | `FELUCCA_SD_MODE`, `PROJ_SLOTS` | already `== 1`: the new steps are neither the SD mode nor one project. Every other use of the layout must be checked for "nonzero means big": `tools/build.py:466` (`if layout():` links the far probe and the longer XIP for any nonzero), `core.py:265` (`if cfg.get("LAYOUT"):` applies LAYOUT 1's rule to any nonzero), `firmware/src/io/console.c` (four uses), `ed_backup.c:630`, `op_prjflow.c:116`, `fm1_guard.h` |
| `tools/flash_layout.py` | `APP_HI`, `app_slot`, `code_limit`, `layout_of`, `switch_clear`, `FAR_RESERVE`, `far_off`, `BIG_STORE_HI` | one table {layout: (end, R, release end)}; `code_limit = app_slot` (no far reserve); `switch_clear(old_end, new)` = the rule of 9.3 |
| `tools/build.py:461-475`, `:529-537` | the link, `LIMITS["flash"]` | the XIP length from the table for any layout; the far probe's section and checks go |
| `tools/fm1pkg_make.py:26-28`, `:108-111` | `flash_image`, `FLASH_SIZE` | already takes `app_hi=` and refuses an end no layout has (`layout_of`): nothing more than the table; `--layout` (`:210`) takes the new values |
| `tools/fm1_rescue.py:69-70`, `:207`, `:373` | `APP_END`, `CLEAR_END`, `load_package`, the switch | sizes and clears from the table (it calls `switch_clear` already) |
| `tools/fm1_layouts.py:24-27`, `:48-77`, `:116-124` | `LAYOUTS`, `layout_of`, `installed_end`, `_clear_range` | the steps' rows (own and base), the known ends from the table, `_clear_range` = the 9.3 rule |
| `tools/fm1_route.py:21`, `:107-109`, `:134-136` | `OWN_SLOTS`, the package kind, `loader_takes` | the list of ends |
| `web/install_route.js:12`, `:43-44`, `:60` | `OWN_SLOTS` | the same list (generated from `flash_layout.py`, as `mflasher.json` is from `mflasher.py`, so the three cannot drift) |
| `ios/OptimistMirror/Sources/Flash/InstallRoute.swift:12`, `:56-57`, `:71` | `ownSlots` | the same list; an App Store resubmit |
| `tools/fm1_anyflash.py`, `tools/mflasher.py` | the download-mode plan | nothing of their own: `fm1_anyflash.py` plans through `fm1_layouts.plan` (its `dry_run`'s "up to 0xDA000" line, `:319`, stays true); `mflasher.py` holds no layout |
| `tools/builder/registry.py:1141-1148`, `:1177-1188` | `MEMORY_CHOICES`, `MEMORY`'s notice, `LAYOUT`'s choices and text | MEMORY 3 "one sample slot", 4 "no user samples" (0-2 unchanged: MEMORY is derived, never stored, `core.py:106-116`), listed in size order; LAYOUT choices 2, 3; the notice per step |
| `tools/builder/core.py:106-130`, `:265-273`, `:1216-1217` | `derive_memory`, `set_memory`, the layout rule, the flash cap | MEMORY <-> LAYOUT for 3, 4; the rule per step (9.1); the cap already looks up `flash_by_layout[LAYOUT]` |
| `tools/builder/configure.py:265-274` | `classic_limits` | already one entry per layout of `flash_layout.APP_HI`: follows the table |
| `tools/fm1base.py:169-182`, `:204-222` | `sd_app_end`, `app_end`, `limits` | the release end per layout from the table, not `== 1`; `flash_by_layout` for every layout |
| `fm1base/optimist/gen_optimist.py:153-165`, `fm1base/optimist/fm1.toml:52-58`, `ui/opt_storage.h:118-133`, `app.h:30-33` | `SD_APP_END`, `slot_check`, `app_end` / `app_clear_end`, `OPT_PANEL_*` | the end per layout; `OBJ_PANEL` at 0xD8000 for S1 and S0 (their asserts) |
| `fm1base/bsp/loader/ldr_core.c` `ldr_slot_switch` | felucca-bsp's loader | today: going down it erases only the old code's tail [new end, old end), going up [end, `LDR_CLEAR_END`). Between two new steps that leaves the old step's log where the new one keeps its own: S0 -> S1 would leave 16 of S0's log sectors (valid `SLG1` sectors) inside S1's log area, read as S1's records. It needs the app's list of (end, R) (an `fm1.toml` table) and the 9.3 rule. A change in the submodule |
| `tools/builder/costs.json` `limits`, `tools/builder/measure_costs.py:165`, `:318`, `:451` | costs-all | the limits follow `classic_limits` / `fm1base.limits` on the next costs-all; the LAYOUT choices' own cost (the +36 B kind of section 7) measured for 2 and 3 as for 1 |
| `docs/MEMORY-MAP.md`, `docs/INSTALL-PATHS.md`, this file | | the steps' maps and the switch matrix (INSTALL-PATHS' tables are made by the code and checked, `tests/anyflash_test.py`) |

**The update loader ships per layout, as today.** Each package carries its own loader (`ota.bin`), compiled with the
build's `FELUCCA_LAYOUT` (`tools/build.py` `build_loader`: `-DFELUCCA_LAYOUT=`), which writes exactly its own slot
(`ldr_core.c:201`). The updater that runs (`ota.c`) only checks the size and stages the package's loader; so a new step
needs every running updater to know its size once (step 1), and the clear is always done by the new step's loader,
which knows every older step from its list.

**Tests:** `tests/flash_layout_test.py` (the table in Python and C agree; `layout_of` takes the new ends, its refused
example 0xB7000 at `:58` must change; `switch_clear` for all 16 pairs); `tests/ldr_test.c --layout` (each pair: the
erased sectors exactly, nothing at or above 0xDA000, a resumed install clears the same); `tests/ota_test.c` (every listed
size taken, any other -15, the official loader normal only); `tests/rescue_test.py`, `tests/anyflash_test.py` (the plans,
the generated INSTALL-PATHS tables); `tests/builder_core_test.py`, `tests/builder_menu_test.py`, `tests/builder_test.py`
(MEMORY's five choices, the rules, the caps); `tests/fm1base_test.py` (the release ends, `slot_check`); the sample,
snapshot and patterns tests built per step (`tests/patterns_test.c`, `tests/project_scope_test.c`,
`tests/compat/sec_log_142.c` already take `FELUCCA_LAYOUT`); `ios/OptimistMirror/Tests/FlashTests.swift`;
fm1-base's `tests/ldr_slot_test.c` for each pair of steps.

### 9.5 Risks and the hardware tests

Risks:

- **Data loss is the design:** every switch between steps loses the projects (9.3). A loader that clears the wrong
  range could also take USR3 or the snapshots on N <-> S1. Mitigations: the clears are one rule from one table, the
  host tests check every pair sector by sector, and no clear can reach 0xDA000 (asserted), so kits, presets, the
  settings and the SDK's sectors are never at risk; the RAM flash driver refuses the SDK's sectors anyway
  (`fm1_flash_map.h:71-73`).
- **An older updater refuses a new size (-15),** as with LAYOUT 1 (section 8, step 2): the first install of a new step
  goes over a normal build of the same version.
- **fm1-base's loader before `app_steps`** left stale log sectors between two new steps (9.4): fixed by `app_steps` (9.7.6); the release steps waited for that
  change.
- **"Nonzero means big"** in the build and the builder (9.4): a new step built as if it were LAYOUT 1 (the far probe
  linked, LAYOUT 1's rule) until every use is checked.
- **Projects that play a slot the step drops** read MISSING (S1: USR2; S0: all).
- **Not measured:** that autosave B brings the working project back after a switch; the project room (the 6-8 KiB a
  busy project is an estimate).

The hardware tests. **User** installs every package and runs every check; **Claude never flashes** and writes nothing
to the FM-1. Claude builds the packages and hands over their paths; after each step, the user's full backup file lets
Claude compare the flash with the planned clear on the computer (`fm1_layouts.plan` against the backup), which proves
what was erased and kept without touching the device.

1. **User**: Backup in the web editor; `python tools/fm1_anyflash.py backup` (a full image).
2. **User**: installs a normal build of the new version (its updater knows the new sizes). Expect: as before.
3. **User**: uploads a sample to USR3, saves a snapshot (on a build with them), then installs **S1**. Expect: boots;
   USR3 plays, the snapshot loads (kept); the projects empty; restore the projects from the backup; save one, power
   off and on, load it; save a user preset with FM6 voices, power off and on, load it. **User**: a full backup, for
   the comparison.
4. **User**: installs **S0** over S1. Expect: USR3 and the snapshot gone, the projects empty; restore; the same saves
   as in 3; a full backup.
5. **User**: installs **N** over S0. Expect: an empty data area below 0xD8000, kits and presets kept; restore; a full
   backup.
6. **User**: one switch through download mode (`python tools/fm1_install.py --route download PACKAGE.fwsc`, N -> S0):
   the plan printed first must match 9.3; then the same checks.
7. **User**: one install of an S1 or S0 package from the web installer and one from the Mirror app (the size lists).
8. Later, the release: **User** installs the release's S0 over the classic S0: nothing cleared, the panel calibration
   kept.

Back out of any step: install a normal build of the same version, then restore with the editor; or restore the full
backup (`python tools/fm1_anyflash.py restore BACKUP.bin`).

### 9.6 Recommendation

1. **Retire the far reserve first** (9.2): no new layout, no install risk, LAYOUT 1's code limit 765,696 -> 831,420 B
   (+65,724 B). Today it makes LAYOUT 1 smaller than the step below it would be.
2. **Make the table one source** (9.4: `flash_layout.py`, `fm1_flash_map.h`, step 1's size list, the loader's rule, the
   installers' lists generated) with only N and L1 in it: host-tested, byte-identical normal packages, and the updater
   of every build from then on takes the next steps' sizes.
3. **Add S0, no user samples, at 32 sectors** (LAYOUT 3, MEMORY "no user samples"): +139,264 B (136 KiB) for the
   classic build and the release alike, and on every profile in `config/profiles/` it loses nothing but the switch
   itself (they build no user samples or snapshots and keep today's 32-sector project room).
4. **S1, one sample slot,** only if the user wants to keep uploading samples: +122,880 B, USR3 and the snapshots kept
   from normal. It is only 16 KiB below S0, so it is a choice for samples, not for flash.
5. **S0-20** (+188,416 B, about 42 KiB less project room) only if a build actually needs the 48 KiB more than S0 and
   less than L1 gives.
6. The release's steps after fm1-base's loader takes the table (9.4).

### 9.7 MEMORY as three choices: SD CARD MODE, SAMPLE SLOTS, PROJECTS (design 2026-10-31, for approval; nothing built)

The user, 2026-10-31: *"yes i want one switch for SDCard mode (on/off) and when off, two options: number of sample slot
and numbe of projects. this will allow to control how much flash we get."* And: *"when we flash a different layout, we
warn the user to backup their data first."* Numbers from the code at `fm1-next` 471e84e0 (file:line) unless marked
*estimate*; the grid's ends and rooms are computed from `tools/flash_layout.py`'s own formulas.

**Revised 2026-10-31** on the user's answer: *"if the memory makes two many combination, projects can be 1/2/4/16"*:
PROJECTS offers 1, 2, 4 and 16 (8 dropped). The design's S1 (one slot, 20 sectors, 0xB1000) and S0-20 (0xC1000) of 9.1
are 8-project cells and go with it.

**Ruled 2026-10-31** (the user: *"if 1 and 2 have the same layout and we dont save flash, whats the point ? :) but with
the no sample gains, in think we can have just 2/4/16 projects:) yes allow zero user samples :) i dont use samples. Yes
layout change should warn user."*): **PROJECTS 2, 4 or 16** (1 dropped: it saved no flash; SD card mode keeps its one
project in flash); **SAMPLE SLOTS 0 allows SAMPLE** with its built-in sets (every slot's capacity 0); **a layout change
warns, never refuses** (the builder, the web installer, the terminal flash, the FM-1's own update screen).

**Built (branch `fm1-next-memopts`):** the three items, the old-config mapping, the warnings (9.7.4, 9.7.5) and the
steps of 9.7.8.

#### 9.7.0 Where things stand

- **SD card mode today** is MEMORY 2 = `LAYOUT` 1 + `SDCARD` 1 (`core.py:106-131` `derive_memory` / `set_memory`):
  the L1 flash layout with the card on the LCD bus. Under it: full FatFs (`registry.h` `FELUCCA_SD_FATFS`: SDCARD and
  LAYOUT 1), the card UI (`FELUCCA_SD_UI`, which `#error`s without PATTERNS and SECTIONS 8/16 in LAYOUT 1,
  `registry.h:737-738`), SD_RING (needs ARENA and SAMPLE, `core.py:283-287`), SD_REC (`core.py:288-290`). One project in
  flash (`PROJ_SLOTS`, `registry.h:719`); the card holds the rest.
- **The sample slots today:** 3 slots addressed (`eng_sample.c:41` `SMP_USER_SLOTS`): USR1 0xA0000 and USR2 0xB4000
  (80 KiB each), USR3 0xC8000 (64 KiB less the snapshots and the CZ collection at its top, `eng_sample.c:50-51`).
  With PATTERNS, USR1 is the project log (`eng_sample.c:56-59`): **at most 2 slots with projects, 3 without** (PATTERNS
  off: the registry's default, `PATTERNS` default 0; no project log, the old project slots).
- **Correction to 9.0:** not every profile builds without user samples. `config/profiles/drum-machines.config` names
  neither `ENG_SAMPLE` nor `DRUM_USR`, so it takes their defaults (3 and 1, `registry.py`): normal with 2 user slots
  and a 20-sector log. The other PATTERNS profiles build `ENG_SAMPLE=0`.
- **Project room** (`sec_log.c:337-350`): `SEC_ROOM = (sectors - 1) x 3,568 B`; one worst-case record is kept in
  reserve (`sec_log.c:752`), at most one sector (records never span sectors). A busy project is about 6-8 KiB
  (*estimate*, `PROJECTS-DESIGN.md:115`).

#### 9.7.1 The grid

**The rule that makes every cell:** the kept sample slots stay at their addresses (USR3 alone, or USR2 + USR3), so
samples survive switches; under them, top down, the project log, autosave copy A, the FM6 voices, and the app ends
there (the 9.1 stack): `end = top - (log sectors + 3) x 4 KiB`, top 0xD8000 (no slot; 0xD8000 stays free for the
release's `OBJ_PANEL`), 0xC8000 (USR3) or 0xB4000 (USR2 + USR3). **Log sectors by PROJECTS:** 1 and 2: 7, 4: 11,
16: 32 (about 8 KiB a project after the reserve, 16 the tightest, as today). Three cells are the steps of today
and keep their exact layout: N, S0, L1.

Code room = app.bin's room (`flash_layout.slot_of`). Gain = against normal's 581,564 B. Room = `SEC_ROOM`;
"busy projects" = room less 4 KiB reserve over 6-8 KiB (*estimate*).

| SD | SAMPLE SLOTS | PROJECTS | Step (LAYOUT) | App end (classic / release) | Code room, B | Gain, B | Log | Room | Busy projects (*est.*) |
|---|---|---|---|---|---|---|---|---|---|
| off | 2 (USR2+USR3) | 16 | **N** (0), today's normal | 0x93000 / 0x93000 | 581,564 | 0 | 20 sect. at 0xA0000 | 66.2 KiB | 7-10 |
| off | 2 | 4 | new (6) | 0xA6000 / same | 659,388 | +77,824 | 11 at 0xA9000 | 34.8 KiB | 3-5 |
| off | 2 | 2 | new (7) | 0xAA000 / same | 675,772 | +94,208 | 7 at 0xAD000 | 20.9 KiB | 2 |
| off | 1 (USR3) | 16 | new (2) | 0xA5000 / same | 655,292 | +73,728 | 32 at 0xA8000 | 108.0 KiB | 13-17 |
| off | 1 | 4 | new (4) | 0xBA000 / same | 741,308 | +159,744 | 11 at 0xBD000 | 34.8 KiB | 3-5 |
| off | 1 | 2 | new (5) | 0xBE000 / same | 757,692 | +176,128 | 7 at 0xC1000 | 20.9 KiB | 2 |
| off | 0 | 16 | **S0** (3), built | 0xB5000 / same (not built) | 720,828 | +139,264 | 32 at 0xB8000 | 108.0 KiB | 13-17 |
| off | 0 | 4 | new (8) | 0xCA000 / same | 806,844 | +225,280 | 11 at 0xCD000 | 34.8 KiB | 3-5 |
| off | 0 | 2 | **L1** (1), today's "more flash" | 0xD0000 / 0xCE000 | 831,420 / 823,228 | +249,856 / +241,664 | 7 at 0xD0000 | 20.9 KiB | 2 |
| **on** | (the card) | 1 in flash + the card | **L1** (1) + SDCARD | 0xD0000 / 0xCE000 | 831,420 / 823,228 (less the card's code) | as L1 | 7 at 0xD0000 | 20.9 KiB | 1 slot |
| off, PATTERNS off | 3 | (no projects: the sections) | **N** (0) | 0x93000 | 581,564 | 0 | the old slots 0x97000 | - | - |

Notes on the grid:

- **1 and 2 projects share one layout** in every column. In column 0 the stack would put 1 project (5 sectors) at
  0xD0000, L1's end with another layout (an installed end must name its step, `flash_layout.py:64`), and 2 projects
  (7 sectors) at 0xCE000, the release's L1 end (the same clash, `known_ends`); so column 0 keeps L1 for both, and the
  other columns follow (5 sectors would gain only 8 KiB). So PROJECTS 1 and 2 are one step and differ only in `PROJ_SLOTS`
  (a build flag, no flash moved). **PROJECTS 1 gained no flash over 2**: dropped (ruled above).
  The UI offers 2, 4, 16; SD card mode (the same step) keeps one project in flash, PROJECTS greyed.
- **SAMPLE SLOTS 2 + PROJECTS 16 is normal as it is** (20 sectors: room for 7-10 busy projects, not 16). A 32-sector
  variant would end at 0x91000, 8 KiB *below* normal: not offered. (The dropped SLOTS 2 + PROJECTS 8 would have been
  normal's same log with 40 KiB more code: normal's room to grow and its unread old project slots, 9.0.)
- **9 different layouts** (N, L1, S0 and 6 new) for 12 cells, ends 0x93000, 0xA5000, 0xA6000, 0xAA000, 0xB5000,
  0xBA000, 0xBE000, 0xCA000, 0xD0000; all 4 KiB aligned, all distinct, none 0xCE000. LAYOUT numbers: 2 (it was kept
  for S1, which is gone), 4..8 new; 0, 1, 3 as today.
- **The release** has the same end as the classic build in every new step (they all end below 0xD8000, `OBJ_PANEL`
  stays at 0xD8000, `opt_storage.h` the non-L1 branch); only L1 differs (0xCE000, as today).
- **Build rules per column:** SLOTS 0: no `DRUM_USR`, `SNAPSHOTS`, CZ collection (as S0/L1 today, `storage.c:46-49`);
  `ENG_SAMPLE` with its built-in sets could be allowed (every slot's capacity 0, as `SMP_USER_CAP` already does) —
  **to rule** (today refused). SLOTS 1: USR3 only (`SMP_USER_CAP` 0 for USR1, USR2); snapshots and the CZ collection
  allowed (USR3's top). SLOTS 2: as normal with PATTERNS. Every SD-off cell but the last needs PATTERNS (the log).
  SLOTS > 0 with no sample engine, `DRUM_USR` or `SNAPSHOTS` built: a builder warning (the slots are kept for nothing;
  SLOTS 0 gives the gain), not an error, since the user owns the profiles.

#### 9.7.2 What a switch keeps (the 9.3 rule, unchanged)

Each new step's R (the top of what it moves) is its column's top: 0xD8000, 0xC8000, 0xB4000; N 0x93000, L1 0xDA000. A
switch from A to B writes [0x4000, E_B) and erases [E_B, max(R_A, R_B)). Computed for every pair of the 12 cells:

| from \ to | same column, other PROJECTS | the other sample column (1 <-> 2) | column 0 <-> 1 or 2 | SD card mode <-> 0 slots + 2 projects (one step) |
|---|---|---|---|---|
| projects, autosave A, FM6 voices | **lost** (the log moves) | **lost** | **lost** | **kept** (a 1-project build keeps project 2's records unread) |
| USR3 + snapshots + CZ collection | kept (columns 1, 2) | **kept** | **lost** | kept |
| USR2 | kept (column 2) | lost (column 1 has none) | lost | kept |
| kits, presets, settings, autosave B, drum records | kept | kept | kept | kept |

SD on <-> SLOTS 0 + PROJECTS 2: the same step (L1): the one project kept, as MEMORY 1 <-> 2 today. Own <-> release in
one step: nothing cleared (as 9.3). The working project probably comes back from autosave B (0xFE000), *not tested
across a switch* (9.3).

One trap the rule leaves, harmless by the code read here (*not tested*):

- **N without user samples** has a 32-sector log to 0xBFFFF (`sec_log.c:37-38`); a switch to a column-2 step clears
  only to 0xB4000, so 12 old log sectors stay in USR2's place. A sample slot reads them as empty (no "FSMP" magic,
  `eng_sample.c:43`). The other way, a column-2 step's USR2 samples left at 0xB4000-0xBFFFF under such an N's log: the
  log's sectors without an "SLG1" head. Whether the log erases them before use or treats them as sealed is **to check**
  (`sec_log.c` heal / compaction); if not clean, that N's loader clears to 0xC0000 (its loader knows it builds no
  samples).

#### 9.7.3 STEPS, FL_ENDS, the loaders and the packages out there

- **Every distinct layout is a step** (12): `flash_layout.STEPS` grows 9 rows; `Step` gains nothing (its fields
  already describe a relocated store). `_check_table`'s `assert not s.samples` for relocated steps becomes "samples
  kept only from the column's top up". `ORDER` by size. `fm1_steps.h` `FL_STEPS` / `FL_ENDS` / the per-layout branches
  are generated, so `ldr_layout_switch` (`ldr_core.c:175`) and `ota_step_size` (`ota.c:41-47`) take them with no hand
  edit. `FL_USER_SAMPLES` becomes the slot count (0, 1, 2) and `SMP_USER_CAP`, `sec_log.c`, `storage.c`'s `#error`
  key on it.
- **PROJECTS is a build flag, not a layout:** a new `FELUCCA_PROJ_SLOTS` (1, 2, 4, 16), `registry.h:719`
  `PROJ_SLOTS` from it, bounded by the step's log (a step's own maximum: `FL_PROJ_SLOTS`, already generated).
  `op_prjflow.c:116` uses `FELUCCA_SECTIONS` as the project count outside L1: it takes `PROJ_SLOTS` instead.
  `ed_backup.c:96` (PJ01..PJ16 offered up to `PROJ_SLOTS`), `ed_proj.c:77`, `sections.c:842`, `:1007` already use it.
- **ota.c step 1** must take every new end. Two ways: (a) the generated list (as today, conservative: a size no step has
  is -15 before anything is staged); (b) any 4 KiB boundary in [0x93000, `FL_APP_HI_BIG`], the package's own loader
  checking its exact size (`ldr_core.c:201`), so later steps need no updater change. (b) lets the release's 0xCE000
  past step 1; it is refused later today (-16, unexplained, `INSTALL-PATHS.md:67`), and whether that refusal comes
  before any erase is **not verified**: (a) recommended.
- **Older installed firmwares:** their step 1 takes {0x93000, 0xB5000, 0xD0000} (since memsteps) or fewer. A new
  step's first install goes over a build of the new version in a step they know (normal), as for LAYOUT 1 and S0
  today: the web installer and the Mirror app already say "Optimist's updater takes the layouts ... only"
  (`install_route.js:60`, `InstallRoute.swift:71`): their `OWN_SLOTS` / `ownSlots` lists, generated/checked from the
  table, name the extra hop. Download mode (`fm1_install.py --route download`) takes any step: `fm1_layouts.plan`
  reads the current flash's end and clears by the table. **M-Flasher alone** writes the package and clears nothing
  above it (`INSTALL-PATHS.md:212`): a switch through its own window leaves the old step's log; only our installer's
  planned-image restore does the clear.
- **Packages out there** (N, L1, S0 of every version) keep their ends and stay valid; a new loader recognises them by
  `FL_ENDS`. An old loader (S0-era) installing an old package over a new step: its `ldr_layout_switch` sees an end it
  does not know: above normal it clears [E, 0xDA000), the safe side; at or below, as after normal (`flash_layout.py:144`)
  — which for column 2's ends (0xA6000, 0xAA000, above normal) is the safe side too. No new step ends below 0x93000.

#### 9.7.4 The builder

Three items replace MEMORY in the first group, all fronts (`no_image`, never stored, as MEMORY: `core.py:105-131`)
over the real items `LAYOUT`, `SDCARD` and a new `PROJECTS` (`FELUCCA_PROJ_SLOTS`, bit 447 (444 is MIX_ROUND, retired by fadefix), the next free; 0 = the
step's own count, so an old `.config` without it reads as before: 16, L1 1):

| Item | Values (with the code room shown on each) | Shown |
|---|---|---|
| `SD_CARD_MODE` (bit 445) | off / on | always |
| `SAMPLE_SLOTS` (bit 446) | 0, 1 (USR3, 64 KiB), 2 (USR2 + USR3, 144 KiB); 3 only with PATTERNS off (normal) | SD off |
| `PROJECTS` (447, real) | 1, 2, 4, 16 (1 and 2: the same code room shown; 1 labelled "the same flash as 2") | SD off, PATTERNS on |

- **SD on:** SAMPLE SLOTS and PROJECTS hidden (the card holds samples and projects; flash keeps one project, no slot);
  SD_RING and SD_REC shown under SD CARD MODE (`shown_under`, as today under MEMORY). Each value in the menu and the web
  builder shows its step's code room ("PROJECTS 4: 741,308 B for code") from `LAYOUT_STEPS`, which grows to every
  step's (layout -> slots, projects, room) and stays checked against the table (`tests/flash_layout_test.py`).
- **Old values load:** `MEMORY=0` -> SD off, SLOTS 2, PROJECTS 16; `MEMORY=4` -> off, 0, 16; `MEMORY=1` -> off, 0, 2;
  `MEMORY=2` -> on. `LAYOUT=n` (and `SDCARD`) as written: the three fronts read back from them (`sync_memory`'s
  successor). `MEMORY` stays parsed (`parse`, `core.py:156`) and is no longer dumped or shown; its bit 436 stays
  retired, never reused. The web builder gets the same through `to_json` / `web_api.py` (no copy of the logic in JS
  beyond the table it already reads).
- **The backup notice** (`notice_on_change`, `menu.py:535`, `web_api.py:124`; MEMORY has it today, `registry.py:1160`)
  on all three. Today the notice is one fixed text. New: `core.switch_loss(before, after)` names what the switch
  erases (9.7.2's table) and the notice is that one line, e.g. *"New memory layout: installing it erases the projects
  and sample slot USR2. Back up first: web editor > Backup."* SD card mode on <-> off: no notice (nothing erased).

#### 9.7.5 The backup warning at install (the user's ruling)

One line, plain words, naming what that switch erases, and where to back up. Template:
**"This firmware has another memory layout: installing it erases {the projects / user samples USR2 / USR3 and the
snapshots}. Back up first: web editor > Backup."** Backups: the web editor's Backup tab (`web/editor.html` `#p-backup`,
`ed_backup.c`: the projects as PJ01..PJ16, as many as the build's `PROJ_SLOTS`, presets, kits, the user sample slots,
the snapshots) and a full flash image (`python tools/fm1_anyflash.py backup`). After the switch the user restores; a
build with fewer PROJECTS takes only PJ01..PJnn (the rest stays in the file) and may say MEM FULL on big projects.

| Where | Today | Needs to learn the installed layout |
|---|---|---|
| Web installer (editor's Firmware tab, `install_route.js`) | no layout warning; knows the identity (`runningKind`) and the package's flash.bin length only | the running firmware's app end. New firmwares: one more value in `bk_caps` (`ed_backup.c:664`), `FL_APP_HI >> 12`, and `PROJ_SLOTS` (BK_VERSION 5). Older ones: inferred from `BK_LIST`: the LAYOUT and SDCARD bits of `bk_bits` (`core.py:462` `cfg_bits`: on/off per item), the USR capacities and whether PJ02 is offered: LAYOUT off = N; on with PJ02 = S0; on without = L1. Unknown: warn anyway ("may have another layout") |
| Mirror app's Firmware tab (`InstallRoute.swift`) | the same as the web installer, no layout warning | the same query (an App Store resubmit) |
| Builder (menu, web builder) | MEMORY's `notice_on_change`, one fixed text | nothing (it compares before and after); text per switch (9.7.4) |
| Terminal (`make flash`, `optimist.py` `cmd_flash` -> `flash.preflight`) | download route: `fm1_layouts.plan` prints the erased sectors (it reads the flash); loader route: nothing | loader route: the same query over USB-MIDI before writing; the warning is a `plan.warn` (so `--yes` alone does not pass it, `optimist.py:252-254`) |
| Firmware (ota.c step 1) | no notice | it knows both ends (`FL_APP_HI`, the package's `fl_len`): it can show "NEW LAYOUT: BACK UP" on its screen (`ota_show`) as the install starts. Waiting for a key there would hold the host's transfer (its timeouts not checked): show only. Option: refuse a layout change unless the host's request carries "layout change confirmed", which forces every installer to warn, at the price of refusing older hosts |

#### 9.7.6 The release

- **Now:** N (SLOTS 2 + 16, and normal with PATTERNS off), L1 (SLOTS 0 + PROJECTS 2 at 0xCE000), SD on (L1 + card).
  PROJECTS 2 in L1 changes no flash, only `PROJ_SLOTS`: buildable once the flag exists (*not built*).
- **Built since 2026-11-02 (branch `layouts`):** every step, LAYOUT 0..8, builds for the release. fm1-base's `fm1.toml`
  `app_steps` lists every end an FM-1 may have of ours with its step and R (`flash_layout.known_ends`; `gen_optimist.py`
  `slot_check` refuses a build when the two differ, and takes `FM1B_APP_END` from `STEPS[layout].release_end`), and
  felucca-bsp's loader (`ldr_core.c` `LDR_STEPS`, `ldr_steps_range`) applies the 9.3 rule from it; `OBJ_PANEL` stays at
  0xD8000 in every step but L1 (`app.h`). `tests/fm1base_steps_test.py` builds that loader for each step's end and
  compares what it erases after each of 14 installed ends with `switch_clear_ends`. Not yet run on an FM-1.

#### 9.7.7 Risks and tests

- **Data loss on switching** is the design (9.7.2): mitigated by the one rule from one table, no clear past 0xDA000
  (asserted), the warnings of 9.7.5. The new risk is the count: 9 layouts, 12 x 12 cell pairs; a loader that maps an
  end to the wrong R could take USR3 on a column 1 <-> 2 switch.
- **Install refusals:** every older updater refuses the 6 new sizes (-15): one hop through normal of the new version.
- **Project room shrinks:** PROJECTS 4 and 2 (34.8 / 20.9 KiB) refuse a big restore with MEM FULL; the 6-8 KiB a
  project is an estimate: measuring a real busy project's bytes is part of phase 2.
- **The trap of 9.7.2** (stale log or sample sectors) to check in `sec_log.c` and by a host test.
- **Tests:** `tests/flash_layout_test.py` (the 9 rows, the header, `switch_clear_ends` for all 100 pairs of the 10 known
  ends, the release's 0xCE000 among them, the 9.7.2 table generated and compared); `tests/ldr_test.c --layout` per pair (erased sectors exactly,
  USR3 kept on 1 <-> 2, nothing at or above 0xDA000); `tests/ota_test.c` (every listed size, others -15);
  `tests/anyflash_test.py`, `rescue_test.py` (plans, INSTALL-PATHS' generated tables); `builder_core_test.py`,
  `builder_menu_test.py`, `builder_test.py` (the three fronts, old MEMORY / LAYOUT configs load to the right cells,
  hidden-when-SD, notices per pair, no notice on 1 <-> 2); `sec_log_test.c`, `patterns_test.c`,
  `project_scope_test.c` built per log size (7, 11, 32; 20 for normal with samples) and per `PROJ_SLOTS`; the sample and snapshot tests per
  column; the web installer's and the Mirror's layout read (`install_route` tests, `FlashTests.swift`); fm1-base's
  `ldr_slot_test.c` when the release takes them. Hardware: as 9.5, User installs, Claude never flashes.

**Recommendation:** build the three items and the `PROJECTS` flag first with only today's steps (N, S0, L1: the cells
SLOTS 2/16, 0/16, 0/1-2, SD on) and the install warning; then add steps cell by cell as wanted, 0/4 and 1/16 first. Open for the user: keep PROJECTS 1 in the list (no flash over 2)? allow SAMPLE's
built-in sets with SLOTS 0? the firmware's own notice only, or a refusal without the host's confirmation?

#### 9.7.8 As built (2026-10-31, branch `fm1-next-memopts`)

- **PROJECTS is a front, not a firmware flag:** with 1 dropped every (slots, projects) pair is one step, so LAYOUT alone
  says it; `PROJ_SLOTS` is the step's `FL_PROJ_SLOTS` (`fm1_steps.h`), 1 in SD card mode (LAYOUT 1 + SDCARD). L1 without
  the card now keeps **2** projects (was 1): the user's own profile (LAYOUT 1) gains a second project slot, its layout
  unchanged. Fronts: `SD_MODE` 445, `SMP_SLOTS` 446, `PROJECTS` 447 (`core.set_front`, `derive_fronts`); `MEMORY` 436
  hidden, old lines still load.
- **Steps (`tools/flash_layout.py` STEPS):** 0 N (2/16), 1 L1 (0/2), 3 S0 (0/16), 8 (0/4, 0xCA000), 2 (1/16, 0xA5000),
  4 (1/4, 0xBA000), 5 (1/2, 0xBE000), 6 (2/4, 0xA6000), 7 (2/2, 0xAA000); the numbers of the 9.7.1 table. All nine
  for the release too since 2026-11-02 (fm1-base `fm1.toml` `app_steps`, 9.7.6).
- **Warnings:** the builder (`core.change_notice`), the web installer (`install_route.js` `switchWarning`: the installed
  end from the editor's `bk_caps`, which now ends with the app end / 4 KiB and `PROJ_SLOTS`; an older firmware: known
  only when its LAYOUT bit is off, else "if the FM-1 has another layout"), `make flash` / the builder's f (`flash.py`:
  the last install from this computer, `build/last-install.json`; asks "anyway" when it differs, never refuses), the
  classic firmware's update screen (`ota.c`: NEW MEMORY LAYOUT / PROJECTS ERASED). Not built: the release's update
  screen (fm1-base's updater), the Mirror app's Firmware tab (needs the same `bk_caps` read and an App Store resubmit;
  its `ownSlots` list is updated, checked by `tests/flash_layout_test.py`).
- **Fix on the way:** `ed_user.c` never erases a sample slot with no room (`SMP_USER_CAP` 0): a BEGIN to such a slot
  erased its first sector, which in the new steps is the project log's or the app's.

# Memory map of the FM-1 (flash and RAM), and what the free space is for

Written 2026-10-08 from the sources at optimist d415e60: `firmware/app.ld`, `firmware/src/storage/storage.c` (its
flash map comment), `firmware/src/engines/sample/eng_sample.c` (the user sample slots), `firmware/src/seq/undo.c`,
`tools/build.py` (the limits and checks), and the flash table in docs/SNAPSHOTS.md section 4. For what each feature
costs, see docs/MEMORY-BUDGET.md and docs/BUILDER.md ("Budget").

The FM-1 has two kinds of memory:

- **Flash, 1 MiB**: permanent, survives power-off. It holds the firmware *and* your data, in separate areas.
- **RAM, about 500 KiB**: fast working memory, lost at power-off (except NOINIT, which survives a reset).

## 1. Flash (1 MiB)

```
0x000000 ┌──────────────────────────────┐
         │ boot                  16 KiB │
0x004000 ├──────────────────────────────┤
         │ APP SLOT             568 KiB │  ← the firmware: what the builder sizes
0x093000 ├──────────────────────────────┤
         │ free                   8 KiB │  ← the app slot's room to grow
0x095000 ├──────────────────────────────┤
         │ FM6 voices (UP_FM6)    8 KiB │  ← the user presets' FM6 voices (free without UP_FM6)
0x097000 ├──────────────────────────────┤
         │ YOUR DATA           ~340 KiB │  ← fixed areas, the same whatever you build
         │ (and a few system areas)     │
0x100000 └──────────────────────────────┘
```

| Address | Size | What | Kind |
|---|---|---|---|
| 0x000000–0x003FFF | 16 KiB | boot (before the app) | system |
| 0x004000–0x092FFF | 568 KiB (581,564 B) | **the firmware**: code, built-in samples, fonts, tables; the update loader writes only here | app |
| 0x093000–0x094FFF | 8 KiB | free: the app slot's room to grow (outside the store's allow-list) | — |
| 0x095000–0x096FFF | 8 KiB | **the user presets' FM6 voices** with `UP_FM6` (OBJ_UPFM6, A/B), else free | data |
| 0x097000–0x09EFFF | 32 KiB | **song sections / projects** (the section log, compressed) | data |
| 0x09F000 | 4 KiB | **autosave**, copy A (the working project: Optimist starts where you left it) | data |
| 0x0A0000–0x0B3FFF | 80 KiB | **USR1** user sample slot | data |
| 0x0B4000–0x0C7FFF | 80 KiB | **USR2** user sample slot | data |
| 0x0C8000– … | the rest, see 1.1 | **USR3** user sample slot | data |
| … –0x0D7FFF | (SNAPSHOTS + 4) × 4 KiB | **snapshots** (whole-state slots), from USR3's end | data |
| (just below the snapshots) | 8 KiB | the CZ tone collection, only with NATIVE_BANKS and the CZ engine | data |
| 0x0D8000–0x0D9FFF | 8 KiB | **FM6 user bank** | data |
| 0x0DA000–0x0DBFFF | 8 KiB | **user drum kits** | data |
| 0x0DC000–0x0DFFFF | 16 KiB | **user presets** | data |
| 0x0E0000–0x0E4FFF | 20 KiB | firmware update staging | system |
| 0x0E5000–0x0E6FFF | 8 KiB | **drum records** of the projects | data |
| 0x0E7000 | 4 KiB | retired: `UP_FM6`'s old copy A (before fix/upfm6-off-vm), read once at start to move the voices, **never written** | — |
| 0x0E8000 | 4 KiB | **SDK VM (stock settings, RF calibration): never written** | system |
| 0x0E9000 | 4 KiB | SDK (BTIF) | system |
| 0x0EA000–0x0FBFFF | 72 KiB | the package's SDK "USR" region: unused (never written), and the stock firmware's restore wipes it | — |
| 0x0FC000–0x0FEFFF | 12 KiB | **settings A/B, autosave copy B** | data |
| 0x0FF000 | 4 KiB | SDK (key_mac): never written | system |

**USR1, USR2, USR3** are the three user sample slots: samples loaded from the web editor, played by the SAMPLE engine
and by SLICE (its sources USR1..USR3).

### 1.1 USR3 and the snapshots share 64 KiB

The snapshot area (`SN_SECTORS` = SNAPSHOTS + 4 sectors: one per slot, one for BEFORE LOAD, three spare) is taken from
the end of USR3, and so is the CZ collection (2 sectors) when it is built (`SMP_USR3_END` in eng_sample.c):

| SNAPSHOTS | snapshot area | USR3 left | USR3 left with the CZ collection |
|---|---|---|---|
| 0 | 0 | 64 KiB | 56 KiB |
| 4 (the default) | 32 KiB | 32 KiB | 24 KiB |
| 8 | 48 KiB | 16 KiB | 8 KiB |

A longer USR3 sample written before reads as empty.

### 1.2 What Optimist never writes: the stock firmware's SDK VM

The stock firmware keeps its SDK VM at **0xE8000**: its own settings and the radio calibration, a record log after
the magic `55 AA AA 55` [measured on an FM-1 with stock V15, 2026-10-08: records 106/107/187/108/113/109, all CRCs
valid]. Until fix/upfm6-off-vm, `UP_FM6` kept the FM6 voices in two copies at 0xE7000 and 0xE8000, so a save could
erase that VM. The voices now live at 0x95000–0x96FFF (0x93000–0x94FFF stays free, the app slot's room to grow), and nothing of Optimist writes the SDK's sectors again:

- `firmware/hal/fm1_flash_map.h` holds the map. `FL_NEVER` (0xE7000–0xFBFFF: the retired sector, the SDK VM, BTIF,
  the SDK's USR; 0xFF000–: key_mac) is refused even where an allow-list would say yes, by the store (`storage.c`
  `st_save`, `felucca.c` `st_erase` / `st_prog`) and, for `FL_SDK_SYS` (0xE7000–0xE9FFF and key_mac), by the RAM flash
  driver itself (`fm1_flash.h` `fl_erase4k_ram` / `fl_prog_ram`), the update loader included.
- The build checks it (`_Static_assert` in storage.c, drum_store.c, sec_log.c, snap_store.c, ota.c): every fixed
  object, the drum records, the section log, the snapshot area, the user sample slots and the update staging are
  inside the store's allow-list and off the SDK's sectors. `tests/upfm6_move_test.c` checks every sector of the map.
- **The move**: at start (`storage.c` `st_upf_move`, from `upreset.c` `up_boot`), when 0x95000 / 0x96000 hold no
  valid voices object, the newest valid one of the old copies (0xE7000 or 0xE8000) is copied there. Only an object of
  ours is taken (FELU, type OBJ_UPFM6, the copy it was written to, both CRCs, "UPF6" in it), never the SDK VM. The old
  sectors are only read. An object of ours found at 0xE8000 already took the VM's place: it is left there (erasing it
  restores nothing; the stock firmware rebuilds its VM when it runs).

What each path does at 0x95000–0x96FFF (and at the app's room, 0x93000–0x94FFF):

| Path | Writes there? |
|---|---|
| the update loader (`firmware/loader`, `ldr_core.c`) | writes only the app area [0x4000, 0x93000). Its record sweep (`ldr_records_drop`) erases a sector of [0x93000, 0xFC000) (the app's room included) whose last 256 B hold a valid update record ("TA" tag and CRC16), but never one of 0xE7000–0xE9FFF, and never a sector that starts with a valid object of ours ("FELU" and its header CRC-32): the voices object ends at +0xF08, so 8 of its payload bytes sit where a record would be, and a valid copy is never taken for one (`tests/ldr_test.c`, the sweep). Only a copy cut before its header was written (no valid data) could still match, ~2^-32 |
| the package (`tools/fm1pkg_make.py`) | `flash.bin` is [0, 0x93000) (`FLASH_SIZE`); a larger app area is refused |
| the web installer (`web/fm1ota.js`) and `tools/fm1_install.py` | write nothing themselves: they serve the package to the firmware (step 1: the loader staged at 0xE0000–0xE4FFF, ota.c) and to the loader |
| `tools/fm1_rescue.py` | writes only the 4 KiB sectors of [0x4000, 0x93000) that differ from V15; it reads the whole flash for its backup |
| back to stock V15 | Optimist stages the stock update loader (`usb_hid_ota.bin`) at 0xE0000–0xE4FFF and that loader writes the V15 package: its `flash.bin` is 0x93000 B (its last data byte 0x92DD3); its other entries are the SDK USR area (0xEA000–) and the loader; 699,936 B is the whole package, not the flash image. On the unit measured, 0x93000–0x96FFF read all FF after V15 had run. So the voices likely survive a trip to stock, but this is not guaranteed: the stock SDK may stage its update or place its VM anywhere in the space it sees as free (its package's VM region starts at 0x93000), as for all of Optimist's data |

**SLOOP 2.4 and the SDK VM** [inferred from 2.4's sources, not measured]: 2.4's USR4 user sample slot runs from
0xE7000 to 0xFAFFF, over the SDK VM, BTIF and the SDK's USR area, so a 2.4 user who loaded a long USR4 sample lost the
stock firmware's VM there. That is 2.4's behaviour, not Optimist's. Optimist's guard for 2.4 (`sl24_guard.c`
`st_keep_sample`) only reads that sample's header and marks its sectors as kept: it never writes there.

**The app slot's room to grow**: until now 0x93000–0x96FFF (16 KiB) was free right after the slot. The FM6 voices take
its last 8 KiB (0x95000–0x96FFF), so the slot keeps 8 KiB of contiguous room to grow, 0x93000–0x94FFF; growing past it
means moving the voices (`storage.c` asserts they start 8 KiB past the slot). Growing into the room also means moving
`FLASH_SIZE` (fm1pkg_make.py, which asserts it stays below 0x95000), the loader's `LDR_APP_HI` and the rescue tool's
`APP_END` together. Nothing in `tools/build.py` counted on that room: its budget is the slot itself (`APP_SLOT`, 581,564 B),
and the package, the loader and the rescue tool stop at 0x93000.

## 2. RAM

| Region (app.ld) | Size | What is in it | The part nothing is linked into |
|---|---|---|---|
| **RAMTEXT** 0x01C00000 | 32,512 B | the audio path copied from flash at boot (`.ram_text`, `.ram_hot`): faster than running from flash | spare for more of the audio path |
| **RAM** 0x01C08000 | 96 KiB | the firmware's variables (`.data` + `.bss`) | **the undo history** |
| **POOL** 0x01C20000 | 336 KiB | big zeroed buffers (`.pool`): delay line, reverbs, FM6 voices, USB audio rings, the section stage | the last 8 KiB are always kept free (`build.py`; the reason is not documented); the rest: **the undo history** |
| stacks 0x01C74000 | 32 KiB | the two CPU stacks (user 0x01C74100–0x01C7A000, system 0x01C7A100–0x01C7C000), each after a guard word | fixed |
| **NOINIT** 0x01C7C000 | 15,696 B | survives a reset (not a power-off): the pending arena (sections saved while playing, waiting to be written to flash once the transport stops, since a flash write stops the audio), the panel table and settings, the boot guard, the crash / debug record; with SECTIONS=4 the four project slots | fixed |

### 2.1 Free RAM is the undo history

`undo.c` (FELUCCA_UNDO_HISTORY 1, the default) keeps its ring in the memory nothing else is linked into
(`_undo_*` in app.ld): the pool after `.pool` up to its last 8 KiB, then main RAM after `.bss`. Its size follows the
build: a feature that takes RAM or pool takes undo levels away. A level keeps only the steps that changed (10 bytes
each, plus a small header) [estimate: a whole 64-step track rewritten is about 650 B]. `build.py` refuses a build
whose ring is under 1 KiB (then build FELUCCA_UNDO_HISTORY=0, the single level of SLOOP 2.x); `FELUCCA_UNDO_CAP`
caps it. The builder's Reserve items keep a minimum of it (and of free app flash) so a configuration cannot fill the device to the last byte: `RESERVE_UNDO_KB` makes the estimate warn and `tools/build.py` refuse a ring below it (docs/BUILDER.md, Reserve).

Free **app flash** is not used by anything: it is the room to add features. Your saves never use it.

## 3. Two builds, measured

| Build | Flash app (of 581,564) | Main RAM (of 98,304) | Pool (of 344,064) | RAM code (of 32,512) | Undo ring |
|---|---|---|---|---|---|
| user-default, d415e60 | 578,972 (2,592 free) | 80,728 | 307,376 | 30,744 | ≈ 46 KiB |
| the user's "mots" config, a1b2366 | 499,736 (81,828 free) | 97,240 | 304,396 | 29,512 | ≈ 32.5 KiB |

Undo ring = (98,304 − RAM) + (344,064 − 8,192 − pool).

## 4. In short

- The builder sizes **only the firmware**: the app slot, and the RAM, pool and RAM code it needs.
- **Your data lives in fixed flash areas** (about 340 KiB), the same for every build. The one build setting that
  moves space between them is SNAPSHOTS (and the CZ collection), which trades USR3 sample space for snapshots.
- **Free RAM and pool become undo levels; free app flash is room for features.**

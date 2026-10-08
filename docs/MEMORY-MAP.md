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
         │ free                  16 KiB │  ← the only room the app slot could grow into
0x097000 ├──────────────────────────────┤
         │ YOUR DATA           ~340 KiB │  ← fixed areas, the same whatever you build
         │ (and a few system areas)     │
0x100000 └──────────────────────────────┘
```

| Address | Size | What | Kind |
|---|---|---|---|
| 0x000000–0x003FFF | 16 KiB | boot (before the app) | system |
| 0x004000–0x092FFF | 568 KiB (581,564 B) | **the firmware**: code, built-in samples, fonts, tables; the update loader writes only here | app |
| 0x093000–0x096FFF | 16 KiB | free (outside the store's allow-list) | — |
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
| 0x0E7000–0x0E8FFF | 8 KiB | the user presets' FM6 voices with `UP_FM6`, else free | data |
| 0x0E9000 | 4 KiB | SDK (BTIF) | system |
| 0x0EA000–0x0FBFFF | 72 KiB | the package's SDK "USR" region: unused, and the stock firmware's restore wipes it | — |
| 0x0FC000–0x0FEFFF | 12 KiB | **settings A/B, autosave copy B** | data |
| 0x0FF000 | 4 KiB | SDK (key_mac) | system |

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

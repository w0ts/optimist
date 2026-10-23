# Memory map of the FM-1 (flash and RAM), and the budget

State 2026-10-19. Sources: `firmware/app.ld`, `firmware/hal/fm1_flash_map.h`, `firmware/src/storage/storage.c` (its map
comment), `firmware/src/engines/sample/eng_sample.c`, `firmware/src/seq/undo.c`, `tools/build.py` (limits and checks),
`tools/builder/core.py` (`LIMITS`, `SPARE`, `FIT_ORDER`). What each feature costs: `tools/builder/costs.json` (measured on
a build farm, shown in `docs/BUILDER.md`). This file holds no per-feature sizes: they change every week.

The FM-1 has two kinds of memory:

- **Flash, 1 MiB**: survives power-off. The firmware and your data live in separate areas.
- **RAM, about 500 KiB**: lost at power-off, except NOINIT, which survives a reset.

## 1. Flash (1 MiB)

| Address | Size | What | Kind |
|---|---|---|---|
| 0x000000-0x003FFF | 16 KiB | boot | system |
| 0x004000-0x092FFF | 568 KiB (581,564 B) | **the firmware** (code, built-in samples, fonts, tables); the update loader writes only here | app |
| 0x093000-0x094FFF | 8 KiB | free: the app slot's room to grow | - |
| 0x095000-0x096FFF | 8 KiB | the user presets' FM6 voices with `UP_FM6` (OBJ_UPFM6, A/B), else free | data |
| 0x097000-0x09EFFF | 32 KiB | song sections / projects (the section log, compressed) | data |
| 0x09F000 | 4 KiB | autosave, copy A (the working project) | data |
| 0x0A0000-0x0B3FFF | 80 KiB | USR1 user sample slot | data |
| 0x0B4000-0x0C7FFF | 80 KiB | USR2 user sample slot | data |
| 0x0C8000-... | see 1.1 | USR3 user sample slot, then the snapshots (and the CZ collection) | data |
| 0x0D8000-0x0D9FFF | 8 KiB | the retired FM6 user bank (dropped 2026-10-18; unused) | - |
| 0x0DA000-0x0DBFFF | 8 KiB | user drum kits | data |
| 0x0DC000-0x0DFFFF | 16 KiB | user presets | data |
| 0x0E0000-0x0E4FFF | 20 KiB | firmware update staging | system |
| 0x0E5000-0x0E6FFF | 8 KiB | drum records of the projects | data |
| 0x0E7000 | 4 KiB | retired `UP_FM6` copy A; read once at start to move the voices, **never written** | - |
| 0x0E8000 | 4 KiB | **SDK VM (stock settings, RF calibration): never written** | system |
| 0x0E9000 | 4 KiB | SDK BTIF: never written | system |
| 0x0EA000-0x0FBFFF | 72 KiB | the SDK's USR region: never written; the stock firmware's restore wipes it | - |
| 0x0FC000-0x0FEFFF | 12 KiB | settings A/B, autosave copy B | data |
| 0x0FF000 | 4 KiB | SDK key_mac: never written | system |

Your data is about 340 KiB in fixed areas, the same for every build. The one build setting that moves space between
them is `SNAPSHOTS` (with the CZ collection), which trades USR3 sample space for snapshots.

### 1.1 USR3, the snapshots and the CZ collection

The snapshot area (`SN_SECTORS` = SNAPSHOTS + 4 sectors of 4 KiB: one per slot, one for BEFORE LOAD, three spare) is
carved from the end of USR3, and so is the CZ collection (2 sectors, only with `NATIVE_BANKS` and the CZ engine):
`SMP_USR3_END` in `eng_sample.c`. Without either, USR3 holds 64 KiB.

| SNAPSHOTS | snapshot area | USR3 left | with the CZ collection |
|---|---|---|---|
| 0 | 0 | 64 KiB | 56 KiB |
| 4 (default) | 32 KiB | 32 KiB | 24 KiB |
| 8 | 48 KiB | 16 KiB | 8 KiB |

A longer USR3 sample written before reads as empty.

### 1.2 What Optimist never writes

`firmware/hal/fm1_flash_map.h` holds the map. `FL_NEVER` (0xE7000-0xFBFFF and 0xFF000-) is refused even where an
allow-list says yes: by the store (`storage.c` `st_save`, `felucca.c` `st_erase` / `st_prog`) and, for `FL_SDK_SYS`
(0xE7000-0xE9FFF and key_mac), by the RAM flash driver itself (`fm1_flash.h`), the update loader included. Static asserts
in `storage.c`, `drum_store.c`, `sec_log.c`, `snap_store.c` and `ota.c` check that every fixed object is inside the
store's allow-list and off the SDK's sectors; `tests/upfm6_move_test.c` checks every sector of the map.

The stock firmware keeps its VM at **0xE8000** (its settings and the radio calibration, a record log after the magic
`55 AA AA 55`; seen on an FM-1 with V15). Until `fix/upfm6-off-vm`, `UP_FM6` stored the FM6 voices at 0xE7000 and 0xE8000.
They now live at 0x95000-0x96FFF. At start (`storage.c` `st_upf_move`, from `upreset.c`), when 0x95000 holds no valid
voices object, the newest valid old copy of ours (FELU, OBJ_UPFM6, both CRCs, "UPF6") is copied there; the old sectors
are only read.

Who writes near the app slot: the update loader writes only [0x4000, 0x93000), and its record sweep
(`ldr_records_drop`) never erases 0xE7000-0xE9FFF or a sector that starts with a valid object of ours (`tests/ldr_test.c`);
the package `flash.bin` is [0, 0x93000) (`fm1pkg_make.py` `FLASH_SIZE`); `tools/fm1_rescue.py` writes only the sectors of
[0x4000, 0x93000) that differ from V15; the web installer and `tools/fm1_install.py` write nothing themselves. Back to
stock V15: the stock loader writes V15's 0x93000 B; on the unit measured 0x93000-0x96FFF read all FF afterwards, so the
voices probably survive, but the stock SDK may place its VM anywhere it sees as free.

**SLOOP 2.4** [inferred from its sources]: its USR4 sample slot runs 0xE7000-0xFAFFF, over the SDK VM, BTIF and USR. That is
2.4's behaviour, not ours; our guard for 2.4 (`sl24_guard.c` `st_keep_sample`) only reads and marks sectors as kept.

**The app slot's room to grow** is 0x93000-0x94FFF (8 KiB). Using it means moving `FLASH_SIZE` (`fm1pkg_make.py`, which
asserts it stays below 0x95000), the loader's `LDR_APP_HI` and the rescue tool's `APP_END` together, and moving the voices.

## 2. RAM

RAM and POOL are one contiguous SRAM range (0x01C08000-0x01C74000) split by `app.ld` [the stock firmware places data
across the boundary; no datasheet]. Main RAM is the scarcer. A buffer only the main loop uses can go to the pool with
`IN_POOL` (`core.h`); moving one changes neither flash nor the undo total. Whether the FM-1 reads both at the same speed
is unmeasured (the emulator has no memory timing); the audio loops' tables (`TAB_RAM`) stay in main RAM unless the
EXPERIMENTAL `TAB_IN_POOL` moves them, and the console's `tabbench` times SINE in main RAM against the pool.

| Region (app.ld) | Size | What |
|---|---|---|
| RAMTEXT 0x01C00000 | 32,512 B | the audio path copied from flash at boot: `.ram_text` (flash driver, CPU1 spin loop) and `.ram_hot` (`HOT`: the mix, drums, most engines). Everything the boot copies (`.data`, `.pool_init`, the three RAM code sections) is one LZB stream in the image after `.text` (`tools/lzb.py`: an LZ parse whose bytes an adaptive range coder codes, decoded by `system/lzb.h lzb_expand` from `fm1_cstart` with a 4.6 KB model on the boot stack): 56 KB of copies take 39 KB of flash |
| RAM 0x01C08000 | 96 KiB | `.ram_hot2` (`HOT2`: code that no longer fits RAMTEXT, mostly FX), `.data`, `.bss` |
| POOL 0x01C20000 | 336 KiB (344,064 B) | `.pool_init` (with `TAB_IN_POOL`: the TAB_RAM tables, copied at boot), then `.pool`: the effects memory (the delay line and the inserts' lines, `fx/insfx/fxi_mem.h`), reverbs, engine memory shared per part, USB audio rings, slicer and punch buffers, the screen canvas, the main loop's `IN_POOL` buffers |
| stacks 0x01C74000 | 32 KiB | user 0x01C74100-0x01C7A000 and system 0x01C7A100-0x01C7C000, each after a guard band; CPU1's stack is in the pool |
| NOINIT 0x01C7C000 | 15,696 B | survives a reset: the pending arena (sections saved while playing, written when the transport stops), panel table and settings, boot guard, crash record, with SECTIONS=4 the project slots |

### 2.1 Free RAM is the undo history

`undo.c` (`FELUCCA_UNDO_HISTORY`, default 1) keeps its ring in the memory nothing else is linked into (`_undo_*` in
`app.ld`): the pool after `.pool` up to its last 8 KiB, then main RAM after `.bss`. A feature that takes RAM or pool takes
undo levels away. A level keeps only changed steps (10 B each plus a header) [estimate: a rewritten 64-step track is about
650 B]. The ring is `(98,304 - RAM) + (344,064 - 8,192 - pool)`. `FELUCCA_UNDO_CAP` caps it. The builder shows the ring in its estimate (information; the reserve entries were retired 2026-10-21). Free **app flash** is only room for
features; saves never use it.

### 2.2 Cache RAM (EXPERIMENTAL `CACHE_RAM`, off by default)

The chip has 8 instruction-cache ways at 0x01F20000 and 8 data-cache ways at 0x01F28000, 4 KiB each. V15 keeps one data way
as cache and uses the other seven (0x01F28000-0x01F2F000, 28 KiB) as RAM, as the AC79 SDK does for a flash-only build. With
`CACHE_RAM`, `hal/fm1_cache.h` does the same at start-up (the SDK's sequence, from RAM, before CPU1 and the interrupts),
tests the 28 KiB, and on a pass gives them to the undo ring in place of main RAM's leftover (`.cache_ram` is empty; an
object placed there must check `fm1_cache_ram_ok()`). On a failed test the way masks are put back. The builder offers 2, 4 or 7 ways
(8, 16, 28 KB). **Measured on one FM-1 (2026-10-18, `registry.py` `CACHE_RAM`):** the boot test passes and cache RAM is as
fast as main RAM, but flash data reads over 16 KB are 57 times slower (1.7 us against 32 ns), because flash data goes
through the data cache and only 4 KB is left; so it stays off. Not known: whether DMA or CPU1 can use the memory.

## 3. The budget

**Limits** (`tools/builder/core.py` `LIMITS`, `tools/build.py`): app flash 581,564 B; main RAM 98,304 B; pool 344,064 B
with 8,192 B always kept spare (`SPARE`; the reason is not documented); RAMTEXT 32,512 B. The builder's estimate adds a
margin (`FIT_MARGIN`: flash 1,024 B, RAM 448 B, RAMTEXT 256 B).

**What `tools/build.py` refuses:** an image over the app slot; main RAM over 96 KiB; less than 8,192 B of pool spare;
RAMTEXT over 32,512 B; an undo ring under 1,408 B (then build `FELUCCA_UNDO_HISTORY=0`, the single level of SLOOP 2.x);
a direct call between RAM code and XIP code (`HOT` code calls
other `HOT` code directly and XIP code only through a pointer, `FAR`); a reference to a ROM address. It also prints the
region sizes and the ring on every build.

**Where the bytes go** (shape, not numbers): the built-in sample sets are about half the image (PERC, the GM kit, cannot be
left out; other sets can, with `SAMPLES_SKIP`, and can still be uploaded to USR1-3); then fonts, the UI, FM6, the drums, the
sequencer, the FX buses. The pool is mostly the delay line (default 128 KB), the punch ring (64 KB), the screen canvas
(about 58 KB), the slicer (32 KB), engine memory (shared per part), and USB audio's rings (12 KB). Removing an item
removes its code, RAM and pool together.

**Fitting order** (`FIT_ORDER`, the builder's "fit" drops these, least loss first, until the estimate fits): ENG_SLICE,
then `DLY_LEN` 32768 (0.74 s; saves 64 KB of pool; a 1/4-note delay needs 81 BPM or more), `PUNCH_N` 16384 (saves 32 KB),
`SL_LEN` 2048 (saves 16 KB), the LOFI, FORMANT, PHASE and DRAWBAR engines, the SCRCH, STRGS and HORNS sample sets, and
last `LCD_DIRTY`. PERC is never dropped. Other levers measured earlier (sizes from 2026-10-05, order of magnitude only):
parameter icons off about 5.6 KB; FM6's black-key editor about 5.4 KB; a sample set 20-44 KB; USB audio and the console off
about 2.5 KB of flash and 12 KB of pool; OTA off 12.8 KB (not an option: it is the update path).

**What runs from RAM:** `.ram_hot` holds `mix_block`, the drum mix, the buses, ANALOG 2 with its asm kernels, FORMANT,
DIGITAL, TRIO, PHASE, WHEEL, SAMPLE and LOFI; FM6 and GRAIN run from XIP. The reverb and some FX code that did not fit
RAMTEXT is `HOT2` (main RAM). The emulator has no cache or flash waits, so the placement is judged on the FM-1. An
`FELUCCA_ASM_CHECK` build keeps everything in XIP.

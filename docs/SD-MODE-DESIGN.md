# SD mode: the flash for code (design, 2026-10-25)

Not built. The plan for turning the FM-1's user-data flash into code space, with the user data on an SD card
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

## 7. Phases

| Phase | What | Device risk |
|---|---|---|
| 1 | `FELUCCA_LAYOUT`: one source for the app slot's end and the store map; every place in section 4 derived from it; no change to the normal layout (the image byte-identical) | none |
| 2 | The **fake SD mode** flavour: the big layout (816 KiB app), the compact store (1-2 projects, 4 presets, 4 kits, no user samples, no snapshots), the loader / package / rescue tool accepting it, a canary past 0x93000. The user installs it on an FM-1 whose data is backed up | the user's install: the user data is lost; the SPL question decides it |
| 3 | RAMTEXT overlays (section 6) | none beyond a normal build |
| 4 | Real SD mode: the user data on the card (`SDCARD`), flash keeps settings + one project | needs the SD mod |
| 5 | Plugins (code from the card into RAM), if ever needed | |

Who does what: Claude writes and host-tests every phase and hands over packages; the user installs them and runs the
checks. Claude never writes to the FM-1.

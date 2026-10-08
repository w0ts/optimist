# Snapshots: whole-state slots

A **snapshot** is everything in the current work, self-contained, kept in a persistent slot and recalled later: the
working project, every section, the song chain. The FM-1 has one working project (autosave) and the sections and
song of the section log; a snapshot lets several such complete sessions live side by side and be switched.
Builder item `SNAPSHOTS` (0 / 2 / 4 / 8 slots), firmware `firmware/src/storage/snapshots/snap_store.c` (the flash area) and
`firmware/src/storage/snapshots/snapshots.c` (what goes in, how it comes back), editor commands 54..57 (`firmware/src/io/editor/ed_snap.c`).

## 1. What a snapshot holds

| Part | From | Stored as |
| --- | --- | --- |
| the working project: all tracks (engine, preset, every value, steps), mix, FX sends, the drum kit and the drum record (lanes, sends), motion, the globals (tempo, swing, delay / reverb / chorus, master, drums level) | `proj_capture` (what autosave writes) | one `sec_codec.c` record (`WORK`) |
| every section A..P (A..D with `SECTIONS=4`) | the section log (`SECTIONS` 8 / 16, its records as they are, motion inside), or the four project slots (`SECTIONS=4`, encoded) | one `sec_codec.c` record each (`SEC`, id 0..15) |
| the song chain (up to 64 parts, loop) | `arrangement` | count, loop, 2 spare bytes, (section, bars) per part (`SONG`, the layout of the log's `SNG1`) |

Not in a snapshot: the device settings (palette, panel calibration, brightness, LIGHTS / KEYS): they belong to the
FM-1, not to a piece of music.

**Referenced, not copied:** the user sample slots USR1..3 (80 / 80 / 64 KiB: copying them is impossible), the user
presets, the user kit bank and the FM6 user bank stay global and unchanged. A project never points at a user preset,
a user kit or the FM6 bank (a loaded kit is copied into the drum lanes, an FM6 voice into the project), so the only
references are the USR slots a SAMPLE / GRAIN / SLICE part or a drum lane plays. After a LOAD the existing MISSING
check (`miss.c`) runs over the loaded work: an empty USR slot, an engine, kit or sample set this build leaves out
shows as `MISSING: USR2 T1, PHYS T2` (amber) and in SAVE > TOOLS > MISS. A section is checked when it plays.

## 2. Format

### The stream (format "SNAP" version 1)

```
info  48 B   magic "SNAP", u16 version 1, u16 info length (48: a later version appends), name[12] (ASCII, 0-padded),
             u32 project magic of the saving build ("FUNB"), u32 its builder config hash, u8 its FELUCCA_SECTIONS,
             u8 record count, u16 BPM, u32 sections mask (bit = section id stored), u8 song parts, u8 flags
             (bit 0 some record carries motion), u16 0, u32 save counter, u32 0
records      u8 kind (1 WORK, 2 SEC, 3 SONG), u8 id, u16 length (LE), the bytes
```

All little-endian. The records are `sec_codec.c` records, the format the section log and the backup already use
(build independent: a build reads another build's records, an engine left out plays its fallback with its settings
kept, a motion chunk is skipped by a build without motion). A later version adds record kinds; a reader skips kinds
it does not know.

### The area

The area is `SN_SECTORS` = slots + 4 sectors of 4 KiB at the end of USR3, just below the banks (0xD8000):

```
0xC8000 ......................... USR3 ......................... | snapshot area (SN_SECTORS x 4 KiB) | 0xD8000 banks
```

Each sector is a **part** of one snapshot: a 32-byte header at 0, up to 4,064 payload bytes from offset 32.

```
u32 magic "SNS1", u8 slot, u8 part, u8 parts, u8 1, u32 seq, u32 len (this part), u32 CRC-32 (this part),
u32 total (the stream), u32 CRC-32 (the stream), u32 CRC-32 of the 28 bytes before
```

A snapshot is valid when its part 0 header is valid and every part 1..n-1 is present (same slot, seq, parts, total,
stream CRC) with its CRC, the lengths add up to the total and the whole stream's CRC holds. Sectors need not be
neighbours: any free sector takes any part. `seq` is one counter for the whole area (the highest seen + 1). Per slot
the valid part-0 header with the highest seq wins; every sector not part of a winner is free.

Slots: 0..N-1 the user's, N the **BEFORE LOAD** copy (B on the device).

### Integrity (torn-write safe, like storage.c)

**Save** into slot k: the stream's length is counted first; free sectors are taken (none of a winner, none an
editor import holds), erased, the payload programmed, then the headers of parts 1..n-1, then **part 0's header last**:
that is the commit. Read back; a failed read-back erases the new part 0 (the old version stays the winner). Then the
superseded versions' part-0 sectors are erased, oldest first (housekeeping; a cut there changes nothing).

| cut | state after the next power-on |
| --- | --- |
| while erasing / programming the new sectors | they were free: the slot's old version and every other slot as before |
| before part 0's header is complete | its CRC fails: the old version wins |
| after part 0's header | the new version wins (the old one is a free sector) |
| while erasing old versions | the new version wins |

**Clear** slot k: every part-0 sector of slot k is erased, **oldest seq first**: a cut leaves the newest version valid
(nothing changed) or none, never an older version brought back.

A save or clear never writes a sector of another slot's winner, so a failure damages neither the current work (RAM;
the autosave object is not touched by a save) nor the other slots. **Damaged** (a part-0 header valid, its parts or
CRCs not): the slot shows red DAMAGED, its sectors stay taken until it is cleared or saved over.

**USR3 in the way**: a build without snapshots has a 64 KiB USR3. A sample longer than this build's USR3 reads as
empty (as today), and while its header says its data reaches into the area the area is not used: SAVE says
`USR3 SAMPLE IN THE WAY` (red) until USR3 is erased or loaded again. A sample is never overwritten by a snapshot.

### Compatibility

- A build without `SNAPSHOTS` ignores the area (it is USR3's again there; a long USR3 sample uploaded then
  overwrites snapshots: the backup keeps them).
- A snapshot made by a build with other engines or features loads with the MISSING warnings; the list shows it
  amber (OTHER BUILD) when the saving build's config hash differs.
- Sections the build does not have (a 16-section snapshot on `SECTIONS=8` / 4): loaded up to this build's count,
  the rest reported (`LOADED: I-P SKIPPED`, amber); a song naming a skipped section is not loaded (the default song).
- `SECTIONS=4` loads A..D into the four project slots (written to flash), stores them encoded.
- Records carry the project format through `sec_codec.c` (SEC_V2: FUNB, ENV2 destinations; older records are
  converted as they decode). Versioning: the stream's `version`, the info length, record kinds; the sector header's
  own version byte.

## 3. Size, measured

`sec_codec_test` (tests/run_tests.sh, 2026-10-07): a section or project record is 70 B at power-on, 478 B typical
(16 steps a track, a few edits), 1,097 B on average over 300 random projects, 3,877 B dense (raw); 4,059 B at most
(`SEC_REC_MAX`: raw, a full motion chunk, a drum record). The stream adds 48 B of info and 4 B a record, and a TAIL record when the work has steps past a track's LEN or
FM6 functions on a part that is not FM6 (what a section record leaves out: with it the work comes back byte for byte).
Measured by tests/snapshots_test.c (2026-10-07):

| snapshot | stream | sectors |
| --- | --- | --- |
| power-on, nothing stored | 203 B | 1 |
| typical: 3 sections of 16 steps a track, a 4-part song, the work | 2,105 B (SECTIONS 4: 2,228 B) | 1 |
| 3 dense sections (every value off its default), a song, the work with motion | 3,980 B (SECTIONS 4: 4,119 B) | 1 (2) |
| made at the panel on the emulator: 3 sections, a 3-part song | 728 B | 1 |
| worst case: the section log full (24,976 B of live records), the longest work record, a 64-part song | <= 29.3 KB | 8 |

The worst case is bounded by the section log: it never holds more than `SEC_ROOM` (7 x 3,568 B) of live records,
so no snapshot needs more than 8 sectors (8 x 4,064 = 32,512 B).

## 4. Where: the flash, measured

Measured from `firmware/hal/fm1_flash.h`, `storage.c`, `eng_sample.c` and the package `tools/fm1pkg_make.py`:

| range | size | owner | usable? |
| --- | --- | --- | --- |
| 0x004000..0x092FFF | app slot | the app (581,564 B), the update loader writes only here | never |
| 0x093000..0x094FFF | 8 KiB | free (outside the store's allow-list) | not used: the app slot's room to grow |
| 0x095000..0x096FFF | 8 KiB | the user presets' FM6 voices with `UP_FM6`, else free | kept for UP_FM6 (docs/MEMORY-MAP.md 1.2) |
| 0x097000..0x09EFFF | 32 KiB | section log / project slots | taken |
| 0x09F000 | 4 KiB | autosave copy A | taken |
| 0x0A0000..0x0C7FFF | 160 KiB | USR1, USR2 | taken |
| 0x0C8000..0x0D7FFF | 64 KiB | USR3 | **the snapshot area comes from its end** |
| 0x0D8000..0x0DFFFF | 32 KiB | FM6 bank, user kits, user presets | taken |
| 0x0E0000..0x0E4FFF | 20 KiB | update staging | never |
| 0x0E5000..0x0E6FFF | 8 KiB | drum records | taken |
| 0x0E7000 | 4 KiB | retired (UP_FM6's old copy A) | never |
| 0x0E8000 | 4 KiB | the stock firmware's SDK VM (its settings, the radio calibration) | never |
| 0x0E9000 | 4 KiB | SDK BTIF | never |
| 0x0EA000..0x0FBFFF | 72 KiB | the package's SDK "USR" region, unused by Optimist | not used: an SDK region, and the stock firmware's restore wipes it |
| 0x0FC000..0x0FEFFF | 12 KiB | settings A/B, autosave copy B | taken |
| 0x0FF000 | 4 KiB | key_mac (SDK) | never |

### The trade-off

`SN_SECTORS` = slots + 4: one typical snapshot (1 sector) per slot, one for BEFORE LOAD, three spare so a slot can
be saved again while its old version still stands and a larger snapshot fits.

| SNAPSHOTS | area | USR3 left | typical snapshots that fit | the largest snapshot (8 sectors) |
| --- | --- | --- | --- | --- |
| 0 | 0 | 64 KiB (2.9 s of 4-bit ADPCM at 44.1 kHz) | none | - |
| 2 | 24 KiB (6 sectors) | 40 KiB (1.9 s) | 2 + BEFORE LOAD + 3 spare | does not fit: FULL (red) |
| **4 (default)** | 32 KiB (8 sectors) | 32 KiB (1.5 s) | 4 + BEFORE LOAD + 3 spare | fits an empty area |
| 8 | 48 KiB (12 sectors) | 16 KiB (0.7 s) | 8 + BEFORE LOAD + 3 spare | fits with up to 4 typical ones |

**Default 4**: USR1 and USR2 stay whole and USR3 keeps half; four sessions plus the safety copy cover a set list;
the largest snapshot the FM-1 can make still goes into an empty area. The flash and RAM costs are in costs.json
(builder item SNAPSHOTS) and docs/BUILDER.md.

## 5. Device UI

SAVE family, after PROJECT: the **SNAPSHOT** page. KNOB 1 the slot (1..N, B), KNOB 2 LOAD, KNOB 3 CLEAR, KNOB 4
SAVE: one detent arms (`AGAIN: SAVE`), a second within ~1.5 s acts, as on the USER and PROJECT pages. The graph lists
the slots: the slot, its name, its size; a dot in the status colour: green saved, amber made by another build,
red damaged; EMPTY dim. The bottom line: the free room, red when the current work would not fit.

- **SAVE** (the slot used: the arm is the confirmation of the overwrite). Stopped and quiet only, like autosave (an
  erase stops the audio ~50 ms): `STOP BEFORE SAVE`, `WAIT FOR SILENCE`. The pending sections are written first. The
  name is made from the work: BPM and the sections (`120 ABCD`, `96 A-H`, `120 WORK` with none).
- **LOAD**: replaces the work, the sections and the song. The current state is saved to BEFORE LOAD first (an older
  BEFORE LOAD gives way if the room is short); then the work is applied and autosaved at once. MISSING follows.
- **CLEAR**: the slot is emptied (a damaged one too).

Messages carry the status colour in the top bar: green `SNAP 2 SAVED`, amber `LOADED: I-P SKIPPED`, red
`SNAPSHOTS FULL`, `SNAP 2 DAMAGED`, `SAVE ERROR`.

## 6. Editor (web/editor.html, Projects screen)

A Snapshots section: the list (name, size, state colour, BPM, sections, song), Save to a slot, Load, Clear, Rename,
**Export** to a `.optimist-snap` file and **Import** into a slot (so snapshots are unlimited on the computer).
Protocol: web/EDITOR_PROTOCOL.md "Snapshots (commands 54..57)"; a firmware without them does not answer 54, the
section says so. The full backup (`.optimist-backup`) carries the area as object `SNAP` (kind 6), restored through
the import commands.

## 7. Tests

- `tests/snap_store_test.c`: the area on a simulated NOR: round trip, restarts, a cut at every program and erase of a
  save and a clear (the old version or the new, never a mix, other slots untouched), full, damaged parts, resurrection.
- `tests/snapshots_test.c` (SECTIONS 16 and 4, MOTION 0 and 1): save, change everything, save, power cycle, load:
  every track, section, motion, song and kit exactly back; BEFORE LOAD; a snapshot with an engine this build lacks
  (MISSING); a 16-section snapshot on a 4-section build.
- web/test_web.mjs: the protocol against the mock device, export / import round trip, the backup's SNAP object.
- The emulator with persistent flash (`tests/emu_snapshots_e2e.sh FIRMWARE.fwsc`, fm1-emulator `play_check --state`):
  3 sections and a 3-part song made at the panel, saved to slot 1; A and B stored again at the panel (SAVE + key: AGAIN, the key again), D added,
  a 5-part song, other sounds, saved to slot 2; changed again, past the autosave, quit; restarted from the kept flash,
  slot 1 loaded and saved to slot 3, slot 2 loaded and saved to slot 4. The script reads the streams out of the kept
  flash: slot 3 is slot 1 byte for byte (but the save counter), slot 4 is slot 2 but for the globals a load never
  applies (G_SLOT: the PROJECT page's slot), BEFORE LOAD holds the state before the second load; sections A and B differ
  between slot 1 and slot 2 (the overwrites registered).

# Snapshots: whole-state slots

A **snapshot** keeps the whole current work in a flash slot: the working project, every section (scene) and the song
chain. Several complete sessions can live side by side and be switched. Builder item `SNAPSHOTS` (0 / 2 / 4 / 8 slots,
default 4; 4 -> 0 saves 8,456 B of flash and 400 B of RAM, `tools/builder/costs.json`). Code: `firmware/src/storage/snapshots/snap_store.c` (the flash
area), `snapshots.c` (what goes in, how it comes back), `firmware/src/io/editor/ed_snap.c` (commands 54..57),
`firmware/src/ui/optimist/op_prjflow.c` (the device page).

## What it holds

The stream is a 48-byte info block (`"SNAP"`, version 1, name, saving build's project magic and config hash, BPM,
sections mask, song parts) then records `u8 kind, u8 id, u16 length, bytes`: `WORK` (the working project, a
`sec_codec.c` record), `SEC` 0..15 (the section log's records), `SONG`, `TAIL` / `XSTEP` / `FXR` / `DLFO` (what a
section record leaves out: steps past LEN, 2.4 extras, FX slots, drum-lane LFOs) and `LOG` (patterns, log ids 24..87).
A reader skips kinds it does not know: a snapshot is read by the build that wrote it or a later one, with no promise
towards other firmwares. Not in it: device settings. **Referenced, not copied:** the user sample slots USR1..3, user
presets, kits and the FM6 bank; after a load `miss.c` reports what is empty or left out of this build (`MISSING: USR2 T1`).

## Flash area

`SN_SECTORS` = slots + 4 sectors of 4 KiB at the end of USR3, just below 0xD8000 (USR3 keeps 64 / 40 / 32 / 16 KiB for
0 / 2 / 4 / 8 slots; the flash map is `docs/MEMORY-MAP.md`). Each sector is one **part** of a snapshot: a 32-byte header
(`"SNS1"`, slot, part, parts, seq, part length and CRC-32, stream length and CRC-32, header CRC-32) and up to 4,064 B.
Any free sector takes any part. Slots 0..7 are the user's, slot 8 is **BEFORE LOAD**. Per slot the valid part 0 with the
highest `seq` wins. The 4 spare sectors cover BEFORE LOAD, a re-save while the old version stands, and a larger stream.
The longest stream is 8 sectors (`SN_PARTS`); the section log bounds a full log's snapshot at 29.3 KB (measured
2026-10-07, before the FX, drum-LFO and pattern records joined the stream; not re-measured). A typical one is ~2 KB.

**Power-cut safe**: a save writes the payload into free sectors, then parts 1..n-1's headers, then **part 0's header last**
(the commit), reads back (a failure erases the new part 0), then erases the superseded part 0s oldest first. A clear erases
a slot's part 0s oldest first, so a cut never brings an older version back. Neither touches another slot or the current
work. A valid part 0 with missing or bad parts is **DAMAGED** and keeps its sectors until cleared or saved over.

**Compatibility.** A build without `SNAPSHOTS` ignores the area (USR3 is whole; a long sample uploaded there overwrites
snapshots, the backup keeps them). A USR3 sample that reaches into the area blocks the area: `USR3 SAMPLE IN THE WAY`. A
snapshot from another config hash shows amber (SAVED BY ANOTHER BUILD). Sections this build lacks load up to its count
(`LOADED: I-P NOT IN BUILD`; `E-P SKIPPED` on `SECTIONS=4`); a song naming a skipped section is not loaded.

## Device

PROJECT screen, row **SNAPSHOT**: cards OPERATION (LOAD default, SAVE, DELETE) and ITEM (the slot, B = BEFORE LOAD); the 16
white keys select slot n and run the operation through the confirm, YES runs it on the ITEM; the operation returns to LOAD
after each action. SAVE needs the sequencer stopped and quiet (an erase stops the audio ~50 ms: `STOP FIRST`,
`WAIT FOR SILENCE`); the name comes from the work (`120 ABCD`). LOAD first saves the state to BEFORE LOAD, applies the
snapshot, autosaves at once. Messages: `SNAP 2 SAVED`, `SNAP 2 CLEARED`, `SNAPSHOTS FULL`, `SNAPSHOT DAMAGED`.

## Editor

Snapshots tab (`web/editor.html`): list, Save, Load, Clear, Rename, Export to a `.optimist-snap` file, Import. Commands
54..57 (`web/EDITOR_PROTOCOL.md` "Snapshots"); the full backup carries the area as object `SNAP` (kind 6).

## Tests

`tests/snap_store_test.c` (the area on a simulated NOR, a cut at every program and erase of a save and a clear),
`tests/snapshots_test.c` (save, change, power cycle, load; BEFORE LOAD; missing engine; 16 sections on 4), `web/test_web.mjs`,
and `tests/emu_snapshots_e2e.sh` (emulator checkout needed; not in `tools/optimist.py test`).

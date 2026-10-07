# SLOOP editor protocol (SysEx over USB-MIDI)

The firmware side is `firmware/src/editor.c` (SLOOP is based on Felucca: the frames keep its "FL"
header). Commands 16-26 (user presets and live sync) form protocol v2; commands 27-30 (tracks) form
protocol v3; commands 31-32 (any track's parameters) form protocol v4; command 33 and the extra step,
`INFO` and `TRACK` bytes form protocol v5 (SLOOP 2.0). Commands 50-53 and the sends in `TRACK_CHANGED` form
protocol v7 (Optimist: the kit editor and the sound editor); `INFO` is unchanged, an editor asks them (below).
Commands 54-57 (snapshots: the whole state in a slot, export, import) form protocol v8; asked the same way.
Commands 58-64 (only what changed, for every track, and the status stream with the meters) form protocol v9, asked with
`WATCH` bits 2 and 3 (below, "v9").

**v3 (four tracks):** the device has four tracks: 1..3 are synth parts, 4 is the drum track. One
of them is *selected* (the TRACKS page on the device, or `TRACK`). Every v1 / v2 command acts on the
selected track (its parameters, engine, preset, steps, the user presets it stores or loads); `TRACK`,
`TRACK_MIX`, `TRACK_DUMP` and `TRACK_STEP` reach any track. Command numbers 1-26 are unchanged.

**v4 (any track's parameters):** `TRACK_PARAM` gets or sets a parameter of any track without changing
the selection, and the `TRACK_CHANGED` push follows level, pan and mute of the tracks that are not
selected. The v1-v3 commands are byte for byte as before; v4 is asked for with bit 1 of `WATCH`.

**v5 (SLOOP 2.0):** the drum track has 16 lanes (one sound per white key) with a level and a ratchet per
hit; `DRUM_STEP` reads and writes them. Synth steps carry a level and a ratchet per note. `INFO` ends with
the protocol version (5) and `TRACK` with the solo mask. Every addition is a byte appended at the end of
a reply or a request, so v1-v4 editors keep working (they see the drum lanes as GM notes, below).

## Framing

A request is `F0 7D 46 4C <cmd> <args...> F7`:

- `7D` is the non-commercial SysEx ID.
- `46 4C` is "FL".

Every request gets exactly one reply, with the same header and the same `<cmd>`. Requests
the device does not understand get no reply. Every data byte is 7 bit. While the editor
watches (v2, `WATCH`), the device also sends push frames (cmds 23, 24, 26) at any time.

| Item | Encoding |
| --- | --- |
| value (v14) | 2 bytes, LSB first, holding value + 8192, so the range is -8192..8191. `[lo, hi]`: value = (lo \| hi << 7) − 8192 |
| string | ASCII bytes, ended by a 0 byte |
| scope | 0 = parameter of the selected track (`P_*`, 0..P_COUNT−1); 1 = global parameter (`G_*`, 0..G_COUNT−1) |
| track | 0..3: tracks 1..3 (synth parts), 3 = the drum track |
| engine byte | 0..NENGINES−1; NENGINES = the drum track (it has no engine and no presets) |

The engine parameters are `P_E0..P_E7`: P_COUNT−8 .. P_COUNT−1, and `INFO` gives `P_E0`.
Their meaning, range and names depend on the current engine, so re-read `DESC` for them
after an engine change.

## Commands

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | version string, NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0, then NENGINES engine-name strings, then (v3) NTRK (4), then (v5) the protocol version (5, 6 from v6), then (v6) each engine slot's UID, then tagged blocks (id, length, that many bytes; skip unknown ids): `53 01 caps` live sync (below); older firmware ends after the names / NTRK / the UIDs |
| 2 GET | scope, id | scope, id, v14 |
| 3 SET | scope, id, v14 | scope, id, v14 (the value after clamping). Setting global `G_ENGSEL` (id from DESC label "ENG") changes the engine with its defaults |
| 4 DUMP | — | engine, preset, then P_COUNT × v14 (the selected track), then G_COUNT × v14 (globals) |
| 5 DESC | scope, id | scope, id, fmt, min v14, max v14, def v14, label string, unit string, then for an enum (fmt 8) one name string per value (at most 64; firmware before SLOOP sent at most 16) |
| 6 STEP_GET | index 0..NSTEP−1 | index, n (0..4 notes), note0..note3, time (0 NOTE, 1 TIE, 2 REST), flags (1 accent, 2 slide), vel, then (v5) lvl, hi, rat |
| 7 STEP_SET | index, n, note0..3, time, flags, vel [, lvl, hi, rat (v5)] | same as STEP_GET (after the write). Without the v5 bytes the step's levels and ratchets become 0 |
| 8 PRESET | engine, preset | engine, preset (applies the preset: sound, sends, arp; never the pattern, the mix or the key: `LEVEL PAN MUTE`, `LEN DIV SWG GATE`, `ROOT SCL QNT CHORD` stay) |
| 9 PROJECT | op (0 load, 1 save, 2 query), slot 0..3 | op, slot, used (1/0). Save writes flash: allow ~2 s |

The local Studio build may append status `1` to a PROJECT reply when playback
prevents a load/save. No operation occurred; stop playback and try again.
An absent status byte retains the original reply format.
| 10 NAMES | engine | engine, count, count preset-name strings, then the two edit-page titles |
| 11 SMP_BEGIN | slot 0..2 | slot, rc (0 ok). Erases the slot's header sector: the slot is empty from now on |
| 12 SMP_WRITE | slot, offset (3 × 7 bit, LSB first), pack7 data (≤ 256 bytes) | slot, offset, rc: 0 ok, 1 arguments, 2 erase, 3 write, 4 slot in use (send SMP_BEGIN first). Offset ≥ 512 and a multiple of 256; writes go in increasing order (a write at a 4 KiB boundary erases that sector) |
| 13 SMP_END | slot, pack7 header (480 bytes) | slot, rc: 0 ok, 1 size, 2 header, 3 data CRC, 4 flash, 5 zones |
| 14 SMP_ERASE | slot | slot, rc (erases the whole slot, ~1 s) |
| 15 SMP_INFO | — | slots, slot KiB, then per slot: zone count (0 = empty), name string, data KiB; then (drum kits firmware) per slot its size in KiB |
| 16 UP_LIST | start, count (1..16) | start, count, total slots, then per slot: used (0/1), engine, name string ("" if unused) |
| 17 UP_GET | slot | slot, used, engine, name, P_COUNT × v14, 16 × (note, flags) |
| 18 UP_PUT | slot, engine, name, P_COUNT × v14, 16 × (note, flags) | slot, rc (0 ok, 1 args, 2 flash). Writes flash: allow 1 s |
| 19 UP_STORE | slot, name | slot, rc. Stores the current sound: engine, parameters, the first 16 sequencer steps as the pattern (TIE steps → flag 4) |
| 20 UP_LOAD | slot | slot, rc (0 ok, 1 empty/invalid). Applies it |
| 21 UP_ERASE | slot | slot, rc |
| 22 WATCH | on (0/1; v4: 3 = also `TRACK_CHANGED`; v9: + 4 every track's changes, + 8 the status stream) | the bits granted (v4 firmware: 3 when 3 was asked for; v8 and older answer 3 to 15, v9 15). While on, the device pushes cmds 23, 24, 26 (and 32 with bit 1); with bit 2: 24 and 59..63 instead (v9); with bit 3: 58 |
| 23 CHANGED (push) | — | scope, id, v14 |
| 24 RELOAD (push) | — | engine, preset, then (v3) the selected track |
| 25 PING | — | 0 |
| 26 STEP_CHANGED (push) | — | index, then (v3) the selected track |

| cmd (v3) | Request args | Reply args |
| --- | --- | --- |
| 27 TRACK | — (query), or track (select it) | selected track, NTRK, then per track: engine byte, preset, level v14, mute (0/1), armed (0/1, live recording) |
| 28 TRACK_MIX | track (get), or track, level v14 (0..127), mute (set) | track, level v14, mute. The drum track's level is global `G_DRLVL` (GLO > DRUMS LEVEL); mute is the track's `P_MUTE` |
| 29 TRACK_DUMP | track | track, engine byte, preset, P_COUNT × v14 (that track's parameters; no globals) |
| 30 TRACK_STEP | track, index (get), or track, index, n, note0..3, time, flags, vel [, lvl, hi, rat] (set) | track, index, n, note0..3, time, flags, vel, then (v5) lvl, hi, rat |

| cmd (v4) | Request args | Reply args |
| --- | --- | --- |
| 31 TRACK_PARAM | track, id (get), or track, id, v14 (set); id = `P_*` (0..P_COUNT−1) | track, id, v14 (the value after clamping, as `SET`). The selection does not change; no push about the editor's own write |
| 32 TRACK_CHANGED (push) | — | track, id, v14: `P_LEVEL`, `P_PAN` or `P_MUTE` of a track that is not selected changed on the device (only while `WATCH` was sent with bit 1); v7: also `P_DIST`, `P_CHOR`, `P_DLY`, `P_REV` (the FX page's sends) and `P_FXOFF` (the bypass). An editor ignores ids it does not follow |

| cmd (v5) | Request args | Reply args |
| --- | --- | --- |
| 33 DRUM_STEP | index (get), or index, on (3 bytes), lvl (5 bytes), rat (5 bytes) (set) | index, on (3 bytes), lvl (5 bytes), rat (5 bytes): the drum track's step, whichever track is selected |

**pack7:** groups of up to 7 bytes, each preceded by one byte holding their top bits
(bit j = bit 7 of byte j).

**User sample slot** (80 KiB each; USR3 64 KiB with the banks firmware (72 KiB with the first drum kits firmware); SAMPLE engine sets USR1..USR3; reference uploader
`tools/fm1_sample_upload.py`, slot builder `sampleio.user_slot`; the editor's port of it is
checked byte for byte by `web/test_web.mjs`): header at 0, ADPCM data at 512.

| Offset | Field |
| --- | --- |
| 0 | magic `"FSMP"` (u32 0x504D5346), u16 version 1, u8 zone count 1..16, u8 0 |
| 8 | name, 8 ASCII bytes (0-padded) |
| 16 | u32 data length (bytes), u32 CRC-32 (zlib) of the data, 8 bytes 0 |
| 32 | 16 zones × 28 bytes: u32 off (in the data), n (samples), loop start, loop end, rate (Hz / 44100 × 65536); i16 root × 16 (MIDI note), ADPCM predictor at the loop start; u8 step index at the loop start, lo note, hi note, looped (0/1) |

Data is IMA ADPCM, 4 bit, low nibble first, starting from predictor 0 and step index 0.
All little endian.

`fmt` values (`firmware/src/core.h`):

| Value | Name | Value | Name | Value | Name |
| --- | --- | --- | --- | --- | --- |
| 0 | INT | 5 | CUTOFF | 10 | NOTE |
| 1 | PCT | 6 | DB | 11 | ONOFF |
| 2 | BIPCT | 7 | SEMI | 12 | OCT |
| 3 | TIME | 8 | ENUM | 13 | STEPS |
| 4 | LFOHZ | 9 | BPM | 14 | SWING |
| | | | | 15 | FILT |

v5 formats: **SWING** 0..100 shown as the MPC swing, 50 % (straight) + value / 4 (so 75 % at 100);
**FILT** −64..63: 0 OFF, below 0 a low-pass closing (LP 1..100 %), above 0 a high-pass (HP 1..100 %).
**PCT** is the share of the range: value × 100 / max (rounded).

The editor should show the value with the unit; formatting it exactly like the device does
is not required.

## v2: user presets

A user preset = engine (0..NENGINES−1), name (1..12 chars, ASCII 32..126; the device shows it upper
case), all P_COUNT instrument parameters (v14 each, the same order as `DUMP`), and a 16-step pattern:
16 × (note 0..127 (0 = rest), flags: 1 accent, 2 slide, 4 tie). Loading one applies the engine and
the parameters of the sound; the pattern stored with it is never loaded (changing a sound never changes
the sequence), and the mix, pattern and key parameters stay (as `PRESET`). The slots are numbered 0..31
(the device shows U01..U32).

- `UP_LIST`: count is cut at 16 and at the last slot (start ≥ 32: count 0, no entries).
- `UP_GET` of an empty slot has the same shape with used 0, engine 0, name "" and all values 0.
  Values come back in the current parameter order, inside their ranges.
- `UP_PUT`: rc 1 for a slot ≥ 32, an engine ≥ NENGINES, a name that is empty, longer than 12 or has
  bytes outside 32..126, or a frame that is too short. Values are clamped to their ranges for that
  engine. A note with flag 4 is stored as a tie (note 0); flags on a rest are dropped.
- `UP_STORE`: name "" stores with the automatic name the device uses (engine name + slot number,
  "ANALOG 07"). rc 1 for a bad slot or name.
- rc 2 = the flash write failed or there is no flash; the slot is still changed in RAM until power-off.
- Frames stay below 640 bytes (`UP_PUT` is 5 + 1 + 1 + 13 + 2 × P_COUNT + 32 + 1).

**On the device:** SAVE > USER page: KNOB 1 picks the slot, KNOB 2 LOAD, KNOB 3 ERASE, KNOB 4 SAVE
(one detent arms, a second one within ~1.5 s acts, as PROJECT LOAD / SAVE). SAVE uses the automatic
name. SELECT and the SAVE > PRESETS browser continue past the factory presets into the used user
presets.

**Flash** (`firmware/src/upreset.c`): two storage objects (`OBJ_UPRESET0/1`, A/B sector pairs at
0xDC000..0xDFFFF), 16 records of 192 bytes each, behind a bank header (magic "UPB2" — "UPB1" before ANALOG 2 / FM6, engines renumbered on load — record size,
slot count; a mismatch reads as an empty bank). A record keeps its layout version (mismatch: empty)
and the P_COUNT it was stored with; another count is mapped by count (last 8 values = P_E0..P_E7, the
first ones = P_LEVEL.. in order, missing ones = defaults). P_COUNT was 53 (P_E0 45) until the SLICER
parameters (SLCR, PAT, RATE, DEPTH: ids 45..48) went in just before P_E0: P_COUNT 57, P_E0 49; SLOOP 2.0
added CHORD (id 49): P_COUNT 58, P_E0 50 (and G_COUNT 32: DUST, DUCK, FILT, ROLL, NEW at 27..31). An
editor takes them from `INFO`; older records load with the SLICER off and CHORD off.

## v2: live sync

- `WATCH 1` starts the pushes. Watching ends by itself 3 s after the last request of any kind (send
  `PING` about every 1 s), on a USB reset, and when the host goes away; `WATCH 0` ends it at once.
- **CHANGED** (scope, id, v14): a parameter changed on the device (knob, menu, sequencer edit of a
  `P_*`), not by the editor's own `SET`. Coalesced: each (scope, id) at most every 20 ms, with the
  latest value.
- **RELOAD** (engine, preset): the engine, a preset, a user preset or a project was loaded; re-read
  `DESC` of the engine parameters, `DUMP` and the steps. It is also sent after loads the editor asked
  for with `PROJECT` load and `UP_LOAD`. After the editor's own `PRESET` and `SET` of G_ENGSEL it is not
  (firmware with INFO tag `53 01`, bit 1): the device takes the load as known (the engine, the preset and the
  parameters the load changed), since the editor re-reads `DUMP` after the reply; a device change still
  pending at that moment (a load or another track selected on the device) is pushed as before. Older firmware
  sends `RELOAD` after those too; an editor that skips its own echoes should do so only when bit 1 is clear.
- **WATCH while watching** (firmware with INFO tag `53 01`, bit 0): `WATCH 1` (or 3) while already watching on
  the same USB connection keeps what has not been pushed yet, so a change made just before is still pushed.
  Older firmware takes everything as known again (re-read `DUMP` after a re-`WATCH` there). `WATCH 0`, the 3 s
  timeout and a USB reset end watching; the next `WATCH` starts from the values as they are.
  (Both after Felucca 1.0.2, hugelton/Felucca db70550, #65; the tag is Felucca's.)
- **STEP_CHANGED** (index): a sequencer step changed on the device (record, clear, step edit,
  pattern load); not after the editor's own `STEP_SET`.
- Push frames have the normal header. Accept them at any time, also while waiting for a reply:
  match replies by cmd (23, 24 and 26 are never replies). The device sends at most a few per
  ~5 ms pass, and only when its USB send queue has room, so a push never delays a reply.

## v3: tracks

- The drum track: `DUMP` / `RELOAD` / `TRACK` give the engine byte NENGINES. Its `P_*` values exist
  (the pattern parameters `LEN DIV SWG GATE`, `PAN`, `MUTE` and `P_E0`, the kit, are used; the rest is
  ignored). `PRESET`, `SET` of `G_ENGSEL` and `UP_LOAD` do nothing there (`UP_LOAD` and `UP_STORE` answer
  rc 1). `DESC` of `P_E0` is the enum `KIT`; `P_E1..P_E7` describe engine 0. Its steps hold the 16 drum
  lanes (v5, `DRUM_STEP`); the v1-v4 step commands see them as GM notes (up to 4 per step).
- Selecting a track with `TRACK` does not push `RELOAD` (the editor re-reads `DUMP`, the steps and the
  engine `DESC` itself); selecting one on the device does (`RELOAD` with the new track).
- Pushes are about the selected track only: `CHANGED` (scope 0) and `STEP_CHANGED` refer to it, and
  changes to other tracks (live recording from MIDI into another track, `TRACK_*` writes) push nothing.
- Level and mute are also `P_LEVEL` / `P_MUTE` of the selected track (`SET`); `TRACK_MIX` reaches the
  others. Presets and user presets change a part's sound but keep its mix (`P_LEVEL`, `P_PAN`,
  `P_MUTE`), its pattern parameters (`LEN DIV SWG GATE`) and its key (`ROOT SCL QNT`, `CHORD`).
- Projects (`PROJECT`) save and load all four tracks and the selection (SLOOP 2.0: project format 4,
  "FUN4", with the drum lanes, levels and ratchets; formats 3, 2 and 1 from older firmware are converted
  when loaded, a format 1 project into track 1).
- Older firmware (no NTRK in `INFO`): one instrument; skip the track UI.

## v4: any track's parameters

- **Finding out:** send `WATCH 3`. v4 firmware answers 3; v3 (0.8) firmware answers 1, does not know
  cmds 31 / 32 (no reply) and never pushes `TRACK_CHANGED`. `WATCH 1` behaves exactly as in v2 / v3
  (reply 1, no `TRACK_CHANGED`). Match the `WATCH` reply by bit 0.
- `TRACK_PARAM` clamps like `SET` scope 0: to the range of that parameter; the engine parameters
  `P_E0..P_E7` to the ranges of that track's engine (the drum track: `P_E0` the kit, the others engine 0,
  as `DESC`). A parameter
  with a fixed range (min = max) keeps its value. A track ≥ NTRK or an id ≥ P_COUNT gets no reply.
  For the selected track it is the same as `SET` scope 0. The drum track's level is still `G_DRLVL`
  (`SET` scope 1 or `TRACK_MIX`); its `P_LEVEL` is not used.
- `TRACK_CHANGED` is never about the selected track (its changes stay `CHANGED` scope 0). Coalesced like
  `CHANGED` (each track and id at most every 20 ms, latest value), and not sent for the editor's own
  `TRACK_PARAM` / `TRACK_MIX` writes. After a selection change (`RELOAD`, or the editor's `TRACK`) the
  device takes the current values as known.

## v5: drum lanes, levels, ratchets (SLOOP 2.0)

- **Finding out:** `INFO` ends with 5. Older firmware ends after NTRK (or the engine names): use the
  v1-v4 commands only.
- **Drum lanes** (`firmware/src/drums.c` `LANE_NOTE`), one per white key from F3: 0 kick (36), 1 kick 2
  (35), 2 snare (38), 3 clap (39), 4 closed hat (42), 5 open hat (46), 6 pedal hat (44), 7 rim (37), 8 snare 2 (40),
  9 low tom (43), 10 hi tom (48), 11 crash (49), 12 ride (51), 13 shaker (70), 14 conga (63), 15 cowbell
  (56). A black key plays the lane of the white key left of it.
- **Levels** (2 bits): 0 NORM (as played), 1 GHOST, 2 SOFT, 3 HARD. **Ratchets** (2 bits): 0..3 = x1..x4
  hits in the step.
- **`DRUM_STEP`:** `on` is 16 bits (bit l = lane l), sent as 3 × 7 bits LSB first; `lvl` and `rat` are
  32 bits each (lane l in bits 2l..2l+1), sent as 5 × 7 bits LSB first. A set replaces the whole step;
  the lanes that are off read back with level and ratchet 0. An index ≥ NSTEP gets no reply. Not a push:
  `STEP_CHANGED` with the drum track selected means "re-read `DRUM_STEP` of that index".
- **The drum track through the old commands:** `STEP_GET` / `TRACK_STEP` give its first 4 lanes that are
  on as GM notes (lane order), time NOTE (REST if none), flag 1 (accent) if one of them is HARD, vel 100,
  and lvl / hi / rat 0. A `STEP_SET` / `TRACK_STEP` write puts each note on its nearest lane (GM 35..81,
  `LANE_OF_GM`), level HARD if the accent flag is set, else NORM, ratchet x1; time TIE / REST clears the
  step.
- **Synth steps:** `lvl` holds 2 bits per note (note k in bits 2k..2k+1, the same levels), `rat` 2 bits per
  note (x1..x4); both 8 bits, sent as 7-bit bytes with their top bits in `hi` (bit 0: lvl bit 7, bit 1:
  rat bit 7).
- **`TRACK`** ends with the solo mask (bit per track; GLO + key on the device). A soloed track plays,
  the others are faded out unless soloed too; mute and solo do not change `P_MUTE` of other tracks.
- **The kit** is the drum track's `P_E0`: `DESC` of `P_E0` with the drum track selected is the enum
  `KIT` (34 kits: ORIGINAL..DUST, the GM sample kit and its treatments, then the synthesised kits from
  808). `DESC` of `P_E1..P_E7` there still describes engine 0 (unused).

## Drum lanes and user kits (commands 36..42)

A firmware with the drum switches (`firmware/src/core.h` `FELUCCA_DRUM_EDIT`, `FELUCCA_DRUM_USR`,
`FELUCCA_DRUM_KITS`; `ed_drums.c`) answers these; one without them does not (ask `DRUM_LANES` once with a
short timeout). `INFO` is unchanged (protocol 5). Commands 34 and 35 stay free (34 is meant for the firmware
builder's `BUILD`, 35 for USB audio statistics).

| cmd | Request args | Reply args |
| --- | --- | --- |
| 36 DRUM_LANES | — (get), or pack7 lanes (204 bytes, set) | pack7 lanes (204 bytes), as the device has them now (values clamped) |
| 37 DRUM_LANE | lane 0..15 (get), or lane, pack7 lane (12 bytes, set) | lane, pack7 lane (12 bytes) |
| 38 UKIT_LIST | — | 16, then per slot: used (0/1). No names: a kit is KIT n, its slot (the first drum kits firmware sent a name string after each) |
| 39 UKIT_GET | slot 0..15 | slot, used, pack7 kit (196 bytes; zeros if empty) |
| 40 UKIT_PUT | slot, pack7 kit (196 bytes; used byte 0 = erase) | slot, rc: 0 ok, 1 arguments, 2 flash, 3 the transport plays |
| 41 UKIT_OP | slot, op | slot, op, rc (as UKIT_PUT). op 0 load the kit into the project, 1 erase, 2 store the project's lanes (a name after it, from an older editor, is ignored). op 3 (rename) is gone: rc 1 |
| 42 SMP_READ | slot 0..2, offset (3 × 7 bit), count (2 × 7 bit, ≤ 256) | slot, offset (3 × 7 bit), pack7 bytes of the slot's flash (fewer at its end) |

`SMP_INFO` (15) now ends with each slot's size in KiB: **USR3 holds 64 KiB** (80 for USR1 and USR2); its last
16 KiB are the banks: flash 0xD8000..0xD9FFF the FM6 user bank (header "FM6B", the 4096 bank bytes at 0xD9000;
DX7 bank dumps read and write it, no USR slot is used), 0xDA000..0xDBFFF the user kit bank (A/B). (The first
drum kits firmware reported 72 KiB and kept the FM6 bank in a free USR slot; the device moves such a bank at
its first start.) `SMP_WRITE` / `SMP_END` refuse data past a slot's size; a USR3 written longer before reads
as empty.

**A lane** (12 bytes): 8 signed offsets from the kit's sound (TUNE −24..24 semitones, DECAY, SNAP, CLICK
−64..63, BEND −24..24 semitones, CUT, DRIVE −64..63, LEVEL −24..6 dB; 0 = as the kit; a sampled sound uses
TUNE DECAY CUT LEVEL), the source (0 the project's kit, 1..3 USR1..USR3, 16 + k: kit k's sound for this lane,
k as the drum track's `KIT`; 64 + v: the X0X 909's voice v, BD SD LT MT HT RS CP CH OH CR RD; 80 + s: the X0X
808's sound s, BD SD LT MT HT LC MC HC RS CL MA CP CB CH OH CY; a firmware without that machine plays the
synthesised 909 / 808's sound for the lane, and an older one the project's kit), and the user-sample reference in 3 bytes: hit (the slot's zone) 4 bits, start
10 bits, length 10 bits (1/1024 of the hit; length 0 = 1024, to its end): `r0 = hit << 4 | start >> 6`,
`r1 = (start & 63) << 2 | length >> 8`, `r2 = length & 255`.

**The lanes** (204 bytes, `drum_edit.c` `dlanes_t`, also the end of a FUN8 / FUN9 project; since format 10 a record
of their own with the sends, `drum_store.c`): offsets[16][8], source[16], reference[16][3], the user kit they came
from (1..16, 0 none), 8 bytes reserved (a name before kits lost theirs: written 0, ignored), 3 bytes 0.

**A user kit** (196 bytes, `drum_kits.c` `ukit_t`; the bank's magic "DKB2"): 0xA5 (used), the kit "KIT" means,
source[16], reference[16][3], offsets[16][8], 2 bytes 0. The device stores every lane of a kit with its kit as
the source (16 + k), not 0. The first drum kits firmware had 204 bytes, an 8-byte name after the base (bank
"DKB1"): the device reads such a bank as the new one and writes DKB2 at its next change.

**Version 2: the lanes' sends** (`firmware/src/ed_dsend.c`; every Optimist build since 2026-10, when they became the
drums' only sends). Each lane also has its sends, 3 bytes: REV (0..31; 4 = a lane as its kit has it), DLY (0..31), CHO
(0..31). REV is a signed byte: until 2026-10 −1 meant TRK (the drum track's REV, GLO > DRUMS REV, which is gone); the
device never sends it now and reads a −1 it receives as 4. Version 2 forms
carry them after the version 1 bytes; a firmware without the sends does not answer them (36 v2, 37 v2, 39 v2: no
reply; 40 v2: rc 1), so the editor asks `DRUM_LANES` v2 first and keeps to version 1 without a reply. Send a v2
set only to a device that answered a v2 get (an older firmware would read a long 36 set as version 1).

| cmd | Request args | Reply args |
| --- | --- | --- |
| 36 v2 | 2 (get), or 2, pack7 lanes + sends (204 + 48 bytes; 289 bytes in all) | 2, pack7 lanes + sends (252 bytes) |
| 37 v2 | 0x40 + lane (get), or 0x40 + lane, pack7 lane + sends (12 + 3 bytes) | 0x40 + lane, pack7 (15 bytes) |
| 39 v2 | 0x40 + slot | 0x40 + slot, used, pack7 kit + sends (196 + 48 bytes) |
| 40 v2 | 0x40 + slot, pack7 kit + sends (used 0: erase) | 0x40 + slot, rc (as 40) |

A version 1 set (36, 37) keeps the sends; a version 1 kit write (40) stores the kit with every lane's sends as it is
(REV 4, no DLY / CHO). The global `G_DRREV` (DESC label REV after CH and LVL) is retired: no page, the device ignores
it (a project keeps −1 there; an older project's value becomes its TRK lanes' REV when it loads).

**Kit files** (the editor's export / import): JSON `{"format": "sloop-drumkit", "version": 2, "kit": {base,
lanes: [{ofs[8], src, hit, start, len, snd: {rev, dly, cho}}]}, "slots": {"0".."2": {hdr, data}}}`, `hdr` / `data` the slot's
header (480 bytes) and ADPCM data in base64, as `SMP_READ` gave them: an import writes them back into the
same USR slots (asked first), then the kit into the bank. Version 1 files (no `snd`) load with every lane's sends as it
is; a `rev` of −1 (TRK, files written before 2026-10) loads as 4.

## v7: drum sources, what a lane shows, pages (commands 50..52)

Optimist (`firmware/src/ed_dsrc.c`, `ed_pages.c`). The editor builds its kit editor and its sound editor from these, so
sources, kits and pages a firmware adds (the X0X voices on lanes, a new engine's pages) appear without an editor change.
A firmware without them does not answer: ask `DRUM_SRCS` 0 once with a short timeout (no reply: the editor builds the
source list from the `KIT` names, the kinds by place and name, and uses the sampled rule below); `DRUM_SHOW` is there
exactly when `DRUM_SRCS` is; ask `PAGES` 0 the same way (no reply: the editor's own layout). `INFO` is unchanged.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 50 DRUM_SRCS | start | start, total, n, then n × (src, kind, name string): the SOURCE page's list from entry start (at most 24 a reply: ask again from start + n until total) |
| 51 DRUM_SHOW | lane 0..15 | lane, mask (2 × 7 bit), flags, name string |
| 52 PAGES | start | start, total, n, then n × (family, scope, shown, id0, id1, id2, id3, title string) (at most 24 a reply) |

- **src** is the lane's source byte (0 KIT, 1..3 USR1..USR3, 16 + k kit k, and what a firmware adds after the kits,
  e.g. the X0X voices); 127 = an entry this build steps over (USR1..3 without user samples on lanes): leave it out.
  **kind**: 0 the project's kit, 1 a user sample slot, 2 a sampled kit, 3 a synthesised kit (the drum synth), 4 an X0X
  machine (its kits and voices); + 8: not in this build (a stand-in plays, the value is kept).
- **DRUM_SHOW** answers the SOUND pages' own rule for that lane's source: mask bit i = offset i (TUNE DECAY SNAP CLICK
  BEND CUT DRIVE LEVEL) applies (a sampled sound: TUNE DECAY CUT LEVEL; an X0X model: its own); flags bit 0: TUNE counts
  the model's steps, not semitones; bit 1: a user sample (HIT START LEN apply); then the lane's name as the device shows
  it (CLOSED HAT...). Ask it again after a source or the kit changed.
- **PAGES** is `params.c` `PAGES` in order: family (`FAM_*`: 1 ENV, 2 LFO, 3 FX, 4 SCL, 5 EDIT, 6 GLO, 7 SAVE, 8 ARP, 9
  SEQ, 10 TRK), scope (0 a track's `P_*`, 1 a global `G_*`, 2 the engine's `P_E*`; other values are the device's own
  screens: steps, the drum lanes' SOUND pages, the song...), shown (1: the page is there for the selected track, e.g.
  ANALOG 2's pages on an ANALOG track only), four ids (127 = an empty slot), the title. The engine pages' titles come
  from `NAMES`. Read it again after an engine change or another track was selected.
- **Audition:** the editor plays a lane with a MIDI note (`9n` note 100, then `8n`) on the drum channel (GLO > DRUMS CH;
  CH 0 = off, then channel 16 plays the selected track: only while the drum track is selected), the lane's GM note
  (`LANE_NOTE`). Notes travel between SysEx frames and get no reply; with REC on they are recorded like a keyboard's.

## v7: transport and meters (command 53)

`firmware/src/ed_status.c`. A firmware without it does not answer (ask once, short timeout): the editor shows no
PLAY / STOP, no playhead and no meters.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 53 STATUS | — (read), or op: 1 PLAY, 2 STOP | flags, BPM v14, section, then per track (NTRK): step, peak (2 × 7 bit, always 0: a polled peak is the v8 fallback, which has no meters; v9 streams them, cmd 58) |

- **op** acts as the PLAY / STOP button (`transport_req`): recording, the count-in and an external clock keep their own
  rules (with a clock followed, PLAY starts at its next tick). The reply is the state before the request took effect.
- **flags**: bit 0 playing, bit 1 a track is armed for recording, bit 2 an external clock (USB / TRS) is followed.
- **section**: the live section playing (0 = A), 127 none.
- **step**: the index in that track's pattern of the step playing, 127 while stopped. **peak**: two bytes, always 0 since
  Optimist 0.1: the bytes stay so the reply keeps its length and every editor, older ones included, reads it the same way.
  Polled about every 150 ms while the mixer is shown (the strips' playing step, the piano roll's playhead) by an editor
  without v9; with v9 the `STREAM` push (58) carries the same bytes with real peaks and nothing is polled.
- **Why the 0.1 meters failed** (measured over the emulator's web-MIDI bridge, the firmware before 559f496): the peak
  was `trk[c].peak` / `drums.peak`, a running maximum the audio ISR never lowers, which only the device's TRACKS screen
  cleared (once per UI frame). With any other page on the device the editor read the loudest value since the TRACKS
  screen was last shown: playing a C4 on every beat, 52 polls in a row read 3499 (−7.4 dBFS) for track 1 while the drums
  only stepped up 4469 → 4816 → 5513, and the 30 polls in the 5 s after STOP still read 3499 / 5513. The scale (8192 = 0 dBFS)
  and the 150 ms polling were not the fault; a silent power-on project reads 0. v9 takes the peaks in the main loop once
  per audio half for both readers (`meters.c`), so neither clears the other's and nothing latches.

## v9: only what changed, the status stream, the meters (commands 58..64)

`firmware/src/ed_sync9.c`, `ed_status.c`, `meters.c`. **Finding out:** send `WATCH 15`. A v9 firmware answers 15; a v8 (or
older v4+) one answers 3 and pushes as before (CHANGED / STEP_CHANGED of the selected track, TRACK_CHANGED of the mix), and
the editor keeps polling STATUS, the kit and DUMP. `INFO` is unchanged. Everything ends with `WATCH` (3 s after the last
request, `WATCH 0`, a USB reset); `WATCH` with fewer bits stops what it leaves out (`WATCH 7`: no stream).

**Bit 2, every track's changes.** While on, the device does not send `CHANGED` (23), `STEP_CHANGED` (26) or `TRACK_CHANGED`
(32); `RELOAD` (24) stays the "re-read everything" signal (a load, another track selected on the device). Instead:

| cmd (push) | Bytes |
| --- | --- |
| 59 PARAMS | n × (where, id, v14): where 0..NTRK−1 a track's `P_*` (any track, the selected one too), 127 a global `G_*` |
| 60 STEPS | track, first index, count, then count steps: a synth track's as `TRACK_STEP` gives them after the index (n, note0..3, time, flags, vel, lvl, hi, rat: 11 bytes), the drum track's as `DRUM_STEP` (on 3, lvl 5, rat 5: 13 bytes); a run of changed steps |
| 61 LANE | as the `DRUM_LANE` (37) reply: lane + 0x40 (its 3 send bytes follow: every build since 2026-10), pack7 lane. Re-ask `DRUM_SHOW` when the source moved |
| 62 TRACKS | as the `TRACK` (27) reply: selected, NTRK, per track engine, preset, level, mute, armed; the solo mask (an engine, preset, arm or solo moved) |
| 63 SONG | the sections stored (3 × 7 bit, bit n = section A + n), the song's parts, loop, then a change count of the snapshot list (7 bit: re-read `SN_LIST` when it moved) |

- **How:** the device keeps shadows of what the editor knows (every track's parameters, the globals, a signature per step and
  per drum lane, the engines / presets / arms / solo, the song's and the snapshot list's signatures). Every **20 ms** one scan
  compares them and sends what differs **with its value now**: a key that moves many times inside a window goes out once, with
  its last value (a knob sweep: one entry per window). At most 4 frames a window, each at most 96 bytes.
- **Never before a reply:** a push goes out only while no request waits in the device and its SysEx ring is at most half full
  with it; what does not fit stays different from its shadow and goes in a later window (with its newer value).
- **The editor's own writes** (`SET`, `TRACK_PARAM`, `TRACK_MIX`, `STEP_SET`, `TRACK_STEP`, `DRUM_STEP`, `DRUM_LANE(S)`,
  `UKIT_OP`, its `TRACK` selection) and what it reads (`DUMP`, `TRACK_DUMP`, step reads) are known: nothing echoes back.
- A firmware without a feature sends nothing about it (no drum lanes: no `LANE`).

**Bit 3, the status stream.**

| cmd (push) | Bytes |
| --- | --- |
| 58 STREAM | as the `STATUS` reply (flags, BPM v14, section, per track: step, peak 2 × 7 bit), then the master's peak (2 × 7 bit) |

- At most every **40 ms** (25 Hz), and only when the frame differs from the one before or a peak is not 0: stopped and silent,
  the device sends one frame and then nothing. The first frame comes at once after `WATCH`.
- **Peaks:** each the largest |output| since the frame before (none falls between two frames: `meters.c` takes the ISR's peaks
  once per audio half, 5.8 ms, in the main loop, for the TRACKS screen and the stream separately), >> 2: 0..16383, **8192 =
  0 dBFS** (Q15 full scale). A track's is before the master volume; the master's is the output (the audio buffer the ISR
  rendered last, scanned in the main loop while the stream is on).
- **Ballistics in the editor** (`METER` in `web/editor.html`): the device keeps no meter state; a rise is instant, the bar falls
  24 dB/s, a hold line stays 1.5 s and then falls too; −60..+6 dB on the bar, green, amber above −6 dB, red at 0 dBFS.
  Sample-and-reset on the device + the look in the editor: no aliasing from the frame rate, nothing to tune in the firmware.

| cmd | Request | Reply |
| --- | --- | --- |
| 64 SYNC_STATS | — (read), or 1 (read, then reset) | 5 × 7 bit each: scans, push frames, push bytes, the longest scan (µs), all scans (µs), STREAM frames, audio halves the master meter scanned, halves it missed. A measurement aid |

**Extension points (not implemented): per-track patterns and scenes** (`docs/PATTERNS-DESIGN.md` §7.1, §7.3). The mixer
becomes the session view: 16 pattern slots on each track strip and the scene column A..P on the master (playing green,
queued amber, recording red). Its live state and its changes fit v9 without a breaking change; the commands and the ids
are assigned with the version after v9 (none reserved here):

- **The state, in the stream.** After the master's peak a `STREAM` frame may carry tagged blocks: id, length (0..127),
  that many bytes; an editor skips ids it does not know (`web/editor.html` collects them already, `blocks[id]`; a v9
  firmware sends none). The patterns' state is one such block: per track the pattern playing (0..15, 127 none), the pattern
  queued (127 none) and its launch (0 at the pattern's end, 1 next bar, 2 now, and the steps left until it starts), flags
  (recording, the working copy edited and not stored); then the scene playing and the scene queued with its launch. Being
  part of the frame, a launch, a queue or a recording that starts or ends makes a frame go out like any other change (25 Hz
  at most, nothing while nothing moves).
- **Slot contents, as a dirty push.** The coalesced pushes take one more kind: track, slot, what changed (stored, cleared,
  copied or duplicated into it; its LEN or used state), sent once per window like `PARAMS`; the editor re-reads that slot
  (or the slot list) only when it shows it. Scenes stored or cleared go the same way, or as bytes appended to `SONG` (63):
  every reply's rule, appended, never moved. `STEPS` (60) stays about the pattern each track plays or edits now (the one
  its strip's overview and the Sequence popup show), so its layout does not change.
- `WATCH` gets one more bit for these pushes (as bit 2 and 3 were added), so an editor without patterns asks 15 and sees
  nothing new.

**Measured** (emulator, user-default build, firmware clock, `web/e2e_daw.mjs --emu`): the scan costs ~100 µs per 20 ms window
in the main loop (0.5 % of the time; the longest 724 µs), nothing in the audio ISR and no RAM code; the master meter saw every
audio half (0 missed). A song playing with three knobs swept on the device (~30 detents a second each): 550 B/s of pushes + the
stream's ~600 B/s (24-byte frames at 25 Hz), about 1 % of USB-MIDI full speed; a `PING`'s round trip stayed 5.8–6.0 ms (5.9
idle). Firmware cost (exact build sizes against optimist 242d90d, the five profiles): +3.9 to 4.0 KB flash, +1.88 KB RAM (the shadows: every track's parameters and step
signatures), RAMTEXT unchanged in every profile (everything-that-fits: 32,492 of 32,512, as before).

## Backup and restore (commands 43..48)

`firmware/src/ed_backup.c` (builds with flash). Everything the device stores is an **object**: a 4-letter tag, a kind
and the build switch it needs. Kind 0: a `storage.c` object (A/B sector pair, ≤ 3,840 bytes); kind 1: a user
sample slot, raw (its 480-byte header at 0, its data from 512; or the FM6 user bank: "FM6B" header, the 4,096 bank
bytes at 0x1000); kind 2: the FM6 user bank; kind 3: a song section's record in the section log (`sec_codec.c`,
compressed; flags byte first: bit 0 the raw project follows, bit 1 a drum record follows, bit 2 a motion chunk
follows the flags byte: count (0..64), the PLAY bits, count × 3 bytes (place = track × 64 + step, parameter,
value), and a raw project after a chunk leaves out its magic, size and sum, 12 bytes; bit 3 format 11 (FUNB);
bit 4 with bit 0 (flags & 0x11 = 0x11, 2026-10, docs/PATTERNS-DESIGN.md phase 0b): compressed with codec B steps,
each a mask of its non-zero bytes (bits 0..6 bytes 0..6, bit 7 a second mask byte with bytes 7..9) then those bytes
(codec A: 10 bytes a step; both read; an older firmware takes 0x11 for raw and refuses it on the length); bits 5..7
a later firmware's (refused); at most 4,059 bytes); kind 4: written only, an older backup's project slot (any FUN* format), imported and stored as that
section; kind 5: another record of the section log, raw (the song chain). A new storage area is one more line in
`BK_OBJS`. Today, in this order (a restore writes in the list's order: the drum records before the projects, the
settings record before the song chain it names):

| Tag | What | Kind |
| --- | --- | --- |
| SETT | settings, the learned panel, the song's first 16 parts and the tag of the whole chain (`persist_t`, "PER3") | 0 |
| DLNS | the projects' drum records ("DLS1", `drum_store.c`) | 0 |
| S01..S16 | song sections A–P (FELUCCA_SECTIONS 8 / 16; a build with 8 lists S09..S16 as not in it) | 3 |
| PRJ1..PRJ4 | 8 / 16 sections: an older backup's project slots, written into A–D. 4 sections: the projects A–D ("FUNA", or the older format they were saved in) | 4 / 0 |
| SNG1 | 8 / 16 sections: the whole song chain, up to 64 parts (count, loop, 2 spare bytes, then section and bars of each part) | 5 |
| AUTO | the working project (autosave) | 0 |
| UPR1, UPR2 | user presets 1–16, 17–32 ("UPB2" / "UPB1") | 0 |
| UPF6 | the FM6 voices of the user presets ("UPF6": a used mask and 32 packed voices, 7 bits a byte; FELUCCA_UP_FM6) | 0 |
| UKIT | the user drum kit bank ("DKB3", or "DKB1" converted as it loads; FELUCCA_DRUM_KITS) | 0 |
| FM6B | the FM6 user bank U01–U32 (FELUCCA_FM6_STORE) | 2 |
| USR1..USR3 | the sample slots (one may hold the FM6 user bank) | 1 |
| SNAP | the snapshot area, raw (FELUCCA_SNAPSHOTS; restored through the snapshot import, cmds 54..57) | 6 |

| cmd | Request args | Reply args |
| --- | --- | --- |
| 43 BK_LIST | 1 (the version the editor speaks) | 1, n, switches (2 × 7 bit: bit 0 flash, 1 ANALOG 2, 2 DRUM_EDIT, 3 DRUM_USR, 4 DRUM_KITS, 5 DRUM_SENDS (always 1 since 2026-10), 6 song, 7 USB audio), the project format's magic (4 ASCII), the chunk size (2 × 7 bit: 256), then per object: tag (4 ASCII), kind, flags (bit 0 in this build, 1 has data, 2 written with 45..47, 3 a slot holding the FM6 bank), length (3 × 7 bit), CRC-32 (5 × 7 bit) |
| 44 BK_READ | i, offset (3 × 7 bit) | i, offset, CRC-32 of the chunk (5 × 7 bit), pack7 bytes (≤ 256; fewer at the end) |
| 45 BK_BEGIN | i, length (3 × 7 bit), CRC-32 (5 × 7 bit) | i, rc |
| 46 BK_DATA | i, offset (3 × 7 bit), CRC-32 of the chunk, pack7 bytes (≤ 256, in order) | i, offset, rc |
| 47 BK_COMMIT | i | i, rc |
| 48 BK_END | 1 done / 0 abort | rc (0); done: the device restarts about 150 ms later |

rc: 0 ok, 1 arguments (an unknown object, a sample slot, out of order), 2 CRC (a chunk: send it again; the whole
object at COMMIT), 3 the transport plays, 4 no flash, 5 not in this build (never written), 6 longer than a storage
object, 7 the flash write failed (the old copy stays), 8 at COMMIT: not an object the firmware would load (today's formats
checked in full: a project's size and sum, the settings' calibration a permutation, a bank's shape; older formats by their
magic; FELUCCA_BK_CHECK, after SLOOP 2.3): nothing written.

- **CRC-32** is zlib's (as `SMP_END` and the storage headers). A storage object's CRC is its header's; a sample slot's
  is computed over its length.
- **Reading** a storage object reads its current copy (the valid one with the highest sequence number).
- **Writing**: the first BEGIN opens a session: the device saves what is only in RAM first (live sections, song, the
  working project), then holds the object received in RAM (its own saves wait: they are tried again later). Nothing
  reaches the flash before COMMIT, which checks the length and the CRC and writes the other copy, header last, read
  back: a transfer or a write cut short leaves the old object. A session left for 10 s ends by itself.
- **Sample slots** are read with 44 and written with the sample upload commands (11..13: erase, data, header last;
  a cut upload leaves the slot empty, never half valid); **the FM6 bank** with its DX7 bank SysEx (`F0 43 0n 09 20
  00`), last (it finds its slot after the samples).
- **Done**: `BK_END 1` drops the RAM copies of the projects (none is written back over what was restored), and the
  device restarts: every object is loaded as at power-on, older formats migrated as usual.
- A firmware without these commands does not answer `BK_LIST`: the editor shows no backup.

**The backup file** (`.optimist-backup`): `OPTBKUP` 0x01 (8 bytes), the header's length (u32 LE), the header (JSON:
`{"format": "optimist-backup", "version": 1, "created", "device": {version, proto, switches, magic, bk}, "objects":
[{tag, kind, fm6, len, crc, at}]}`), the objects' bytes (at `at` from the end of the header), then the CRC-32 of
everything before it (u32 LE). Each object is the device's own bytes (its magic and format inside); the editor
refuses a damaged file and leaves out objects the connected device does not have (shown, unticked).

## Snapshots (commands 54..57, protocol v8)

`firmware/src/ed_snap.c` (builds with `FELUCCA_SNAPSHOTS`, docs/SNAPSHOTS.md). A **snapshot** is the whole state: the
working project, every section, the song. The FM-1 keeps them in slots 0..7 (a build shows `FELUCCA_SNAPSHOTS` of
them: 2, 4 or 8) and slot 8, **BEFORE LOAD** (the state just before the last load). The slot numbers are the same in
every build. A firmware without these commands does not answer `SN_LIST`: the editor says it has no snapshots and
everything else works as before. Every addition later is a byte appended at the end of a reply.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 54 SN_LIST | 1 (the version the editor speaks) | 1, slots shown, slots in the format (9), area sectors, free sectors, payload a sector (2 × 7 bit: 4,064), the longest stream (3 × 7 bit), the work fits now (0 / 1), then per slot 0..8: state (0 empty, 1 saved, 2 damaged), shown in this build (0 / 1), size (3 × 7 bit), name (string), BPM (2 × 7 bit), sections (3 × 7 bit, bit n = section A + n), song parts, the saving build's sections, another build (0 / 1), the work has motion (0 / 1) |
| 55 SN_OP | op, slot [, name string] | op, slot, rc, notes. op 0 save the state now (name "" = made from the work: "120 ABCD"), 1 load (the state goes to BEFORE LOAD first), 2 clear, 3 rename (a new version of the slot). notes (a load): bit 0 sections past this build's (8 sections: kept in the log, not playable; 4 sections: skipped), bit 1 the song was not loaded (it names such sections), bit 2 a section the store did not take. Save / load / rename write flash: allow ~5 s |
| 56 SN_READ | slot, offset (3 × 7 bit) | slot, offset, CRC-32 of the chunk (5 × 7 bit), pack7 bytes of the slot's stream (≤ 256; none at its end) |
| 57 SN_WRITE | 0, slot, length (3 × 7 bit), CRC-32 of the stream (5 × 7 bit) | 0, slot, rc (begin: the room taken and erased) |
| | 1, slot, offset (3 × 7 bit), CRC-32 of the chunk (5 × 7 bit), pack7 bytes (≤ 256, in order) | 1, slot, offset, rc |
| | 2, slot | 2, slot, rc (commit: the stream's CRC, the stream parsed, then written; otherwise nothing changes) |
| | 3, slot | 3, slot, 0 (abort) |

rc: 0 ok, 1 arguments, 2 no room (FULL), 3 the transport plays, 4 no flash, 5 the flash write failed (the old
version stays), 6 the slot is damaged, 7 busy, 8 empty, 9 still sounding (wait for silence), 10 not a snapshot this
firmware loads (nothing written), 11 a long USR3 sample of a build without snapshots is in the snapshot area, 12 CRC (a
chunk: send it again; the stream at commit), 13 out of order or no import open. An import is ended by a save, load or
clear on the device and by 10 s without a chunk.

**The stream** (version 1): a 48-byte info: `"SNAP"` (u32 LE 0x50414E53), u16 version, u16 info length (48; a later
version appends), name (12 ASCII bytes, 0-padded), u32 the saving build's project magic (`"FUNB"`), u32 its builder
config hash, u8 its sections, u8 record count, u16 BPM, u32 the sections stored (bit mask), u8 the song's parts, u8
flags (bit 0 the work carries motion), u16 0, u32 a save counter, u32 0. Then records: u8 kind, u8 id, u16 LE length,
the bytes. Kind 1 WORK: the working project as a section record (the format of `BK_SEC` above, motion inside); kind 2
SEC, id 0..15: a section's record; kind 3 SONG: count, loop, 2 spare bytes, (section, bars) per part; kind 4 TAIL, id
0x80 the work's (0..3 a section's with 4 sections): what a section record leaves out (per track: a count, then (step,
its 10 bytes) for the steps past LEN that are not empty; then a mask of the parts that are not FM6 and their 16 FM6
functions each), so the project comes back byte for byte. A reader skips kinds it does not know.

**The file** (`.optimist-snap`): `OPTSNAP` 0x01 (8 bytes), u32 LE the stream's length, the stream, u32 LE CRC-32 of
everything before it.

**The backup** lists the snapshot area as object `SNAP` (kind 6: the area's sectors raw, `SN_SECTORS` × 4 KiB; flag bit
2 clear: not written with 45..47). Each sector is a part of one snapshot: a 32-byte header (u32 `"SNS1"`, u8 slot, u8
part, u8 parts, u8 version 1, u32 seq, u32 this part's length, u32 its CRC-32, u32 the stream's length, u32 the stream's
CRC-32, u32 CRC-32 of the 28 bytes before), then up to 4,064 payload bytes. Per slot the valid part 0 with the highest
seq wins, when every part is there with its CRC. A restore imports each slot's stream with `SN_WRITE` (slots this build
does not show are skipped and reported).

## Notes for the editor

- **One request at a time.** Wait for the reply, about 10–50 ms, before sending the next.
  The device holds only one incoming SysEx frame.
- **Following the device.** With v9 firmware, `WATCH 15` and `PING`: every change is pushed, nothing is polled. With v2..v8
  firmware, `WATCH` and `PING` (above) and a slow `DUMP` / mixer / `STATUS` poll for what is not pushed. Older firmware pushes
  nothing (no reply to `PING`): poll `DUMP` about every 300–500 ms while the page is visible.
- **Port.** The device's MIDI port is named "Felucca" (USB 1209:0001; SLOOP keeps the name so editors
  and installers find it). Updates use the same
  port with other SysEx (the `F0 22 24 35 …` keys, `00 59 …` frames); never send those
  from the editor.
- **Safety.** Only `PROJECT` save, `SN_OP` save / load / clear / rename, `SN_WRITE`, the sample-slot commands, `UP_PUT` / `UP_STORE` / `UP_ERASE`, `UKIT_PUT` / `UKIT_OP`
  (erase, store, rename) and `BK_COMMIT` (and the session's first `BK_BEGIN`, which saves what is in RAM) write flash, and only in
  Felucca's own storage; never the app or the update area.

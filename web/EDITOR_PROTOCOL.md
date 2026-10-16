# Optimist editor protocol (SysEx over USB-MIDI)

The firmware side is `firmware/src/io/editor/editor.c` (Optimist is based on SLOOP, which is based on Felucca: the frames keep its "FL"
header). Commands 16-26 (user presets and live sync) form protocol v2; commands 27-30 (tracks) form
protocol v3; commands 31-32 (any track's parameters) form protocol v4; command 33 and the extra step,
`INFO` and `TRACK` bytes form protocol v5 (SLOOP 2.0). Commands 50-53 and the sends in `TRACK_CHANGED` form
protocol v7 (Optimist: the kit editor and the sound editor); `INFO` is unchanged, an editor asks them (below).
Commands 54-57 (snapshots: the whole state in a slot, export, import) form protocol v8; asked the same way.
Commands 58-64 (only what changed, for every track, and the status stream with the meters) form protocol v9, asked with
`WATCH` bits 2 and 3 (below, "v9"). Commands 86-87 and `INFO` tag `56` (the FX slots) form protocol v10 (below, "v10").
Commands 92-93 (the automation store) added to v10 form protocol v11 (below, "The automation store").
Protocol v12 is v11 plus a flag byte on `SL24_GET` (78) that asks for a second lost word (below, "SLOOP 2.4 export"); `INFO` is unchanged.
Commands 94..101 and `INFO` tags `57`..`5E` (every effect slot of every owner and its parameters; the drum lanes' mix, LFOs and the sequencer mutes; GLOBAL and SYSTEM as the Optimist UI's lines; the song chain; the pattern tools) form protocol v13 (below, "v13: FX owners" .. "v13: sequence tools"); the protocol byte stays.
Each of the eight is a builder item (`ED_FXP` 94 / tag 57, `ED_DLAN` 95 / 58, `ED_SCRN` 96 / 59, `ED_SONG` 97 / 5A, `ED_TOOLS` 98 / 5B, `ED_SCRS` 99 / 5C, `ED_FAVS` 100 / 5D, `ED_SCOPE` 101 / 5E; docs/BUILDER.md): a build that leaves one out sends no INFO tag for it and does not answer its command, so an editor offers a command only when its tag is in INFO (web/editor.html hides the pop-up, tab, button or settings that need it and keeps the rest; its mock serves any subset, `?off=dlan,scope`). The commands that read the Optimist UI (96, 98, 99, 101) are built only with it (UI=1), and 100 only with the favorites.
Commands 102..105 and `INFO` tags `5F`..`62` (undo and redo, notes played from the editor, track and drum-lane solo, a drum lane's step with the device's own list of lockable values) form protocol v14 (below, "v14: undo" .. "v14: step lanes"); the protocol byte stays.
Each of the four is a builder item (`ED_UNDO` 102 / tag 5F, `ED_NOTE` 103 / 60, `ED_SOLO` 104 / 61, `ED_STEPLANE` 105 / 62; docs/BUILDER.md), on by default, and a build that leaves one out sends no INFO tag for it and does not answer its command (an editor offers a command only when its tag is in INFO, as for v13).
v12 also carries each POLY note's own end (2026-10-12, feat/note-gates, below "A note's own end"): four bytes after a synth
step's rat, and the step flag `SF_CARRY` (0x80, which a 7-bit byte cannot hold) on bit 2 of its hi byte.

**A note's own end (v12).** A synth step holds, per note k, its own end: 0 = the step's GATE (as every note before), 1..127
= the note ends k's value / 128 of the step after the step's start (POLY notes recorded over each other with REC QUANTIZE
OFF, `firmware/src/seq/ngate.h`). `STEP_GET`, `STEP_SET` and `TRACK_STEP` replies append e0..e3 after rat (a reply only
grows at its end: an older editor reads what it knew). A write with the four bytes after rat sets them; a write without
them (an older editor) keeps each note's own end where the note at that place is unchanged, else 0. The `STEPS` push (60)
packs steps back to back, so it carries them only for an editor that asks: `WATCH` bit 4 (16; a firmware before v12
grants at most 15), then a synth step is 15 bytes there. The hi byte's bit 2 is `SF_CARRY` (a carry step: its notes
already sounding go on, not struck again; seq.c `seq_carry`); a write with the v5 bytes sets or clears it from that bit.
The automation store never holds an own end in a working list: `AUTO_GET` lists none, `AUTO_SET` refuses their stored ids
(0x78..0x7B, rc 1); they exist only in the stored forms (a section, a pattern record, a snapshot, the autosave), as
step-only events of id 0x78 + k, read back into the steps at a load. A drum lane's id (0x7C..) is never one of them.

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

**LFO DEST** (2026-10, feat/lfo-slots): the four ids that held LFO's depths PIT FLT SHP AMP (13..16) are two slots:
DEST A (enum), AMT A (−128..127), DEST B, AMT B. A DEST: OFF PIT FLT SHP AMP RES PAN LVL ATK DEC E1..E8 CHO DLY REV INS1 INS2 (the sends, the S1 / S2
inserts' amounts), and with LFO2
XRATE XAMT (the other LFO's RATE, its AMTs); `DESC` gives the names and the range of this build. An AMT of 2 is a depth
of 1 of before on PIT FLT SHP, on AMP the same. They are part of a sound (user presets keep them; `UP_GET` / `UP_PUT`
carry the slots, the device stores them as before when they are depths). Not lockable nor recordable as motion yet; an
old lock or motion event on a depth (id 13..16 in a project from before) plays onto the slot on that destination.

**ENV DEST** (2026-10-14, feat/env-dests): ENV and ENV2 each have the same two slots (DST A, AMT A, DST B, AMT B), taken from one list shared by LFO, LFO2, ENV and ENV2 (core.h `LT_*`; `DESC` gives the names of this build). The amplitude is ENV slot A on AMP 127 by default (a new sound, INIT, the presets, an old project). The old relative XRATE / XAMT are the absolute L1 RT, L1 AMT, L2 RT, L2 AMT. The fixed pitch drop that ENV DEST PIT used to give is its own value, DROP (key `PIT` in the web editor's `paramKeys`). ENV2 runs on every engine. A project stores the slots as int8 values in its modulation record (tag 0xD2, with the drum lanes' LFO records); old depth words are written 0 (0 = defaults) and an old project converts from its words. `UP_GET` / `UP_PUT` carry the device's P_COUNT and the slots. **VOICES** (`P_VOICES`, the last parameter id; 1 to the engine's maximum, never AUTO) is a track parameter listed by `DESC`.

**LFO 2** (builds with the builder item LFO2, `FELUCCA_LFO2`): eight track parameters after every other one, the last
ids of a track (P_COUNT−8 .. P_COUNT−1 in those builds; `DESC` names them): RATE (`F_LFOHZ`, −10..127 as LFO's RATE:
below 0 a note value of the tempo), WAVE (SIN TRI SAW SQR S&H), TRIG (NOTE FREE), POL (BI UNI), DEST A, AMT A, DEST B,
AMT B (as LFO DEST's). They are read and set as any track parameter (`GET` / `SET`, `TRACK_PARAM`, the dumps carry
them as they carry P_COUNT values); they are not part of a sound (a user preset does not keep them) and are not
recordable as motion or locks. The synth parts' only (the drum track's are not heard).
Their meaning, range and names depend on the current engine, so re-read `DESC` for them
after an engine change.

## Commands

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | version string, NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0, then NENGINES engine-name strings, then (v3) NTRK (4), then (v5) the protocol version (5, 6 from v6), then (v6) each engine slot's UID, then tagged blocks (id, length, that many bytes; skip unknown ids): `53 01 caps` live sync (below); `52 03 mask scope id` the reverb's algorithms built (builds with the reverb bus, 2026-10: mask bit 0 ROOM, 1 SPRING, 2 PLATE, 3 FDN8; with two or more, FX > REVERB > TYPE is DESC / GET / SET at (scope, id), its value names are the algorithms built; one built: 127 127, no TYPE; no tag (older firmware): the editor shows no Type); `56 len 01 n` + n × (type, kind, amount id, sound id) the FX slots' types built (v10, below); `54 03 mask scope id` the MIDI settings (SLOOP 2.4 phase 3; mask bit 0 the channels, 1 MIDI OUT, 2 MIDI IN built): DESC / GET / SET at (scope, id + n), scope 9: n 0..3 the MIDI channel of tracks 1..3 and of the drums (OFF, 1..16; the project's), 4 MIDI OUT (KEYS, SEQ), 5 MIDI IN (NOTES, CLOCK; both settings of the FM-1); SYNC and VIEW stay the globals 13 and 14, which the device now keeps as settings of the FM-1, not in a project; older firmware ends after the names / NTRK / the UIDs |
| 2 GET | scope, id | scope, id, v14 |
| 3 SET | scope, id, v14 | scope, id, v14 (the value after clamping). Setting global `G_ENGSEL` (id from DESC label "ENG") changes the engine with its defaults |
| 4 DUMP | — | engine, preset, then P_COUNT × v14 (the selected track), then G_COUNT × v14 (globals) |
| 5 DESC | scope, id | scope, id, fmt, min v14, max v14, def v14, label string, unit string, then for an enum (fmt 8) one name string per value (at most 64; firmware before SLOOP sent at most 16) |
| 6 STEP_GET | index 0..NSTEP−1 | index, n (0..4 notes), note0..note3, time (0 NOTE, 1 TIE, 2 REST), flags (1 accent, 2 slide), vel, then (v5) lvl, hi, rat |
| 7 STEP_SET | index, n, note0..3, time, flags, vel [, lvl, hi, rat (v5)] | same as STEP_GET (after the write). Without the v5 bytes the step's levels and ratchets become 0 |
| 8 PRESET | engine, preset | engine, preset (applies the preset: sound, sends, arp; never the pattern, the mix or the key: `LEVEL PAN MUTE`, `LEN DIV SWG GATE`, `ROOT SCL QNT CHORD` stay) |
| 9 PROJECT | op (0 load, 1 save, 2 query), slot 0..3 | op, slot, used (1/0; with FELUCCA_SL24_SAFE 2 = a SLOOP 2.4 project kept under the empty section, 3 = another firmware's, kept; neither is loaded). Save writes flash: allow ~2 s |

The local Studio build may append status `1` to a PROJECT reply when playback
prevents a load/save. No operation occurred; stop playback and try again.
An absent status byte retains the original reply format.
| 10 NAMES | engine | engine, count, count preset-name strings, then the two edit-page titles |
| 11 SMP_BEGIN | slot 0..2 | slot, rc (0 ok, 3 the transport plays: stop first). Erases the slot's header sector: the slot is empty from now on |
| 12 SMP_WRITE | slot, offset (3 × 7 bit, LSB first), pack7 data (≤ 256 bytes) | slot, offset, rc: 0 ok, 1 arguments, 2 erase, 3 write, 4 slot in use (send SMP_BEGIN first), 5 the transport plays (stop first). Offset ≥ 512 and a multiple of 256; writes go in increasing order (a write at a 4 KiB boundary erases that sector) |
| 13 SMP_END | slot, pack7 header (480 bytes) | slot, rc: 0 ok, 1 size, 2 header, 3 data CRC, 4 flash, 5 zones |
| 14 SMP_ERASE | slot | slot, rc (0 ok, 3 the transport plays: stop first; erases the whole slot, ~1 s) |
| 15 SMP_INFO | — | slots, slot KiB, then per slot: zone count (0 = empty), name string, data KiB; then (drum kits firmware) per slot its size in KiB |
| 16 UP_LIST | start, count (1..16) | start, count, total slots, then per slot: used (0/1), engine, name string ("" if unused) |
| 17 UP_GET | slot | slot, used, engine, name, P_COUNT × v14, 16 × (note, flags) |
| 18 UP_PUT | slot, engine, name, P_COUNT × v14, 16 × (note, flags) | slot, rc (0 ok, 1 args, 2 flash, 3 the transport plays: stop first). Writes flash: allow 1 s |
| 19 UP_STORE | slot, name | slot, rc (0 ok, 1 args or the drum track selected, 2 flash, 3 the transport plays). Stores the current sound: engine, parameters, the first 16 sequencer steps as the pattern (TIE steps → flag 4) |
| 20 UP_LOAD | slot | slot, rc (0 ok, 1 empty/invalid). Applies it |
| 21 UP_ERASE | slot | slot, rc (0 ok, 1 args, 2 flash, 3 the transport plays) |
| 22 WATCH | on (0/1; v4: 3 = also `TRACK_CHANGED`; v9: + 4 every track's changes, + 8 the status stream; v12: + 16 the `STEPS` push with each note's own end) | the bits granted (v4 firmware: 3 when 3 was asked for; v8 and older answer 3 to 15, v9 15, v12 31). While on, the device pushes cmds 23, 24, 26 (and 32 with bit 1); with bit 2: 24 and 59..63 instead (v9); with bit 3: 58 |
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
| 32 TRACK_CHANGED (push) | — | track, id, v14: `P_LEVEL`, `P_PAN` or `P_MUTE` of a track that is not selected changed on the device (only while `WATCH` was sent with bit 1); v7: also `P_DIST`, `P_CHOR`, `P_DLY`, `P_REV` (the FX page's sends) and `P_FXOFF` (the bypass); v10: also every FX slot type's amount (`P_TFLT` FILT, `P_TCOMP` CMP, in the builds that have them). An editor ignores ids it does not follow |

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

`fmt` values (`firmware/src/core/core.h`):

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
**LFOHZ** below 0 (the LFO's RATE, −10..127): a note value of the tempo, one LFO cycle each, −1 4BAR, −2 2BAR, −3 1BAR,
−4 1/2, −5 1/4, −6 1/8, −7 8T, −8 1/16, −9 16T, −10 1/32 (SLOOP 2.6's LFO 2 SYNC values 1..10, negated); no unit.

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

**Flash** (`firmware/src/storage/upreset.c`): two storage objects (`OBJ_UPRESET0/1`, A/B sector pairs at
0xDC000..0xDFFFF), 16 records of 216 bytes each (UP_VER 3, 2026-10-14: every value in its own 16-bit word, then the envelopes' 8 modulation-slot words), behind a bank header (magic "UPB3"; "UPB2" (192-byte records) and "UPB1" (before ANALOG 2 / FM6, engines renumbered on load) are read and converted, and the bank is written as UPB3 at the next store; record size,
slot count; a mismatch reads as an empty bank). A record keeps its layout version (mismatch: empty)
and the P_COUNT it was stored with; another count is mapped by count (last 8 values = P_E0..P_E7, the
first ones = P_LEVEL.. in order, missing ones = defaults). P_COUNT was 53 (P_E0 45) until the SLICER
parameters (SLCR, PAT, RATE, DEPTH: ids 45..48) went in just before P_E0: P_COUNT 57, P_E0 49; SLOOP 2.0
added CHORD (id 49): P_COUNT 58, P_E0 50 (and G_COUNT 32: DUST, DUCK, FILT, ROLL, NEW at 27..31). An
editor takes them from `INFO`; older records load with the SLICER off and CHORD off. Optimist's master COMP / LIMIT
(2026-10) made G_COUNT 38: THRS, RATIO, ATK, REL, GAIN, CEIL at 32..37 (the editor finds them by their DESC labels). A
project still stores 32 globals; these six live in its reserved bytes (`project.c` mc_pack), so the format is unchanged.
REC QUANTIZE (QUANT, 38) made G_COUNT 39; the FX returns (2026-10-12) make it 42: CRET, DRET, RRET at 39..41, the
chorus', delay's and reverb's return level (0..127, 127 unity), kept in the project's FX record (`fx_rec.c`, TLV 0x41).

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
- **Drum lanes** (`firmware/src/drums/drums.c` `LANE_NOTE`), one per white key from F3: 0 kick (36), 1 kick 2
  (35), 2 snare (38), 3 clap (39), 4 closed hat (42), 5 open hat (46), 6 pedal hat (44), 7 rim (37), 8 snare 2 (40),
  9 low tom (43), 10 hi tom (48), 11 crash (49), 12 ride (51), 13 shaker (70), 14 conga (63), 15 cowbell
  (56). A black key plays the lane of the white key left of it.
- **Levels** (2 bits): 0 NORM (as played), 1 GHOST, 2 SOFT, 3 HARD. **Ratchets** (2 bits): 0..3 = x1..x4
  hits in the step.
- **`DRUM_STEP`:** `on` is 16 bits (bit l = lane l), sent as 3 × 7 bits LSB first; `lvl` and `rat` are
  32 bits each (lane l in bits 2l..2l+1), sent as 5 × 7 bits LSB first. A set replaces the whole step;
  the lanes that are off read back with level and ratchet 0. An index ≥ NSTEP gets no reply. Not a push:
  `STEP_CHANGED` with the drum track selected means "re-read `DRUM_STEP` of that index".
- **The ACCENT lane** (2026-10-14, `FELUCCA_DRUM_ACCENT`; no version change: the bits were always sent as 0): lane 16,
  a 17th step lane with no sound of its own. Its step raises every hit of that step by the drum track's ACCENT amount
  (the drum track's parameter `P_E1`, 0..127, default 27: a NORM hit at 100 plays 127), scaled by the accent step's own
  level (NORM x1, GHOST x0.42, SOFT x0.72, HARD x1.27), at most velocity 127; MIDI out sends that velocity. In
  `DRUM_STEP` (and `STEPS`) it is bit 16 of `on` (the third byte's bit 2) and bits 32..33 of `lvl` and `rat` (the fifth
  group's bits 4..5). A write without those bits (an older editor) turns the step's accent off. In every stored form
  (projects, sections, patterns, snapshots, backups) it is a step-only event of the drum track's list, param 0x77,
  value the lane's byte (bit 0 on, bits 1..2 level, bits 3..4 ratchet); the working list (`AUTO_GET`) never holds it,
  and `AUTO_SET` refuses it (rc 1).
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

A firmware with the drum switches (`firmware/src/core/core.h` `FELUCCA_DRUM_EDIT`, `FELUCCA_DRUM_USR`,
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

**Version 2: the lanes' sends** (`firmware/src/io/editor/ed_dsend.c`; every Optimist build since 2026-10, when they became the
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

Optimist (`firmware/src/io/editor/ed_dsrc.c`, `ed_pages.c`). The editor builds its kit editor and its sound editor from these, so
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
  the model's steps, not semitones; bit 1: a user sample (HIT START LEN apply); bit 2 (2026-10, backward compatible: older
  firmware sends 0, an older editor ignores it): the lane's sound has no per-sound DIST / COMP (SOUND 3 shows none: an X0X kit's
  or voice's lane, whose channels mix apart), so FX op 3 is ignored for it and the editor hides those knobs; then the lane's name as the device shows
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

`firmware/src/io/editor/ed_status.c`. A firmware without it does not answer (ask once, short timeout): the editor shows no
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
- **Why the 0.1 meters failed** (measured over the emulator's web-MIDI bridge, the firmware before 9558b66): the peak
  was `trk[c].peak` / `drums.peak`, a running maximum the audio ISR never lowers, which only the device's TRACKS screen
  cleared (once per UI frame). With any other page on the device the editor read the loudest value since the TRACKS
  screen was last shown: playing a C4 on every beat, 52 polls in a row read 3499 (−7.4 dBFS) for track 1 while the drums
  only stepped up 4469 → 4816 → 5513, and the 30 polls in the 5 s after STOP still read 3499 / 5513. The scale (8192 = 0 dBFS)
  and the 150 ms polling were not the fault; a silent power-on project reads 0. v9 takes the peaks in the main loop once
  per audio half for both readers (`meters.c`), so neither clears the other's and nothing latches.

## v9: only what changed, the status stream, the meters (commands 58..64)

`firmware/src/io/editor/ed_sync9.c`, `ed_status.c`, `meters.c`. **Finding out:** send `WATCH 15`. A v9 firmware answers 15; a v8 (or
older v4+) one answers 3 and pushes as before (CHANGED / STEP_CHANGED of the selected track, TRACK_CHANGED of the mix), and
the editor keeps polling STATUS, the kit and DUMP. `INFO` is unchanged. Everything ends with `WATCH` (3 s after the last
request, `WATCH 0`, a USB reset); `WATCH` with fewer bits stops what it leaves out (`WATCH 7`: no stream).

**Bit 2, every track's changes.** While on, the device does not send `CHANGED` (23), `STEP_CHANGED` (26) or `TRACK_CHANGED`
(32); `RELOAD` (24) stays the "re-read everything" signal (a load, another track selected on the device). Instead:

| cmd (push) | Bytes |
| --- | --- |
| 59 PARAMS | n × (where, id, v14): where 0..NTRK−1 a track's `P_*` (any track, the selected one too), 127 a global `G_*` |
| 60 STEPS | track, first index, count, then count steps: a synth track's as `TRACK_STEP` gives them after the index (n, note0..3, time, flags, vel, lvl, hi, rat: 11 bytes; with `WATCH` bit 4 (v12) + e0..e3, each note's own end: 15 bytes), the drum track's as `DRUM_STEP` (on 3, lvl 5, rat 5: 13 bytes); a run of changed steps |
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
- **The master's gain reduction** (firmware with MASTER_COMP, `master_comp.c`): after the master's peak, the tagged block
  `47 02 comp limit`: the COMP's and the LIMIT's largest reduction since the frame before, quarter dB each (0..127: up to
  31.75 dB). A firmware without the item sends no block; an editor that does not know id 0x47 skips it (as any block).
  The editor draws it as the GR bar beside the master meter (`grEl`): from the top, the COMP's share neutral, the LIMIT's
  amber; a rise is instant, a fall 30 dB/s; hidden until a frame carries the block.
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
idle). Firmware cost (exact build sizes against optimist bc088b1, the five profiles): +3.9 to 4.0 KB flash, +1.88 KB RAM (the shadows: every track's parameters and step
signatures), RAMTEXT unchanged in every profile (everything-that-fits: 32,492 of 32,512, as before).

## Macros: what plays (command 65)

`firmware/src/io/editor/ed_macro.c`, only in a build with `MACROS` (GLO > MACRO). The editor reads and writes the **authored** values: a
macro moves them inside the audio ISR only (`macro.c`), so DUMP, PARAM and PARAMS never show the moved value. Command 65 says
what plays, computed from the same rows and arithmetic the ISR uses (`mac_effective*`), so the editor keeps no table of its own.
A build without the macros does not answer 65 (no reply: the editor shows the authored values only).

| cmd | Request | Reply |
| --- | --- | --- |
| 65 MACRO | track (0..NTRK-1) | track, COLOR, MOTION, SPACE, ENERGY (4 × v14, each -64..63, 0 = home), n (7 bit), then n entries |

Entries (the reply holds only the parameters a macro actually moves; none at home):

| kind | Bytes | Meaning |
| --- | --- | --- |
| 0 | 0, id, v14 | the track's parameter `P_id` plays as v14 (a drum track has none) |
| 1 | 1, id, v14 | the global `G_id` plays as v14 |
| 2 | 2, 0, sc v14, add v14 | every drum lane's REV send plays as clamp(send × sc / 64 + add, 0, 127); the send is the lane's, or the one in a synth track's steps (a lane's level v is 4v + v / 8) |

The editor asks again for the selected track on a track change and about every 500 ms (the four positions move from the device
or by motion recording), and shows the effective value with an M mark: the knob's value and tick, the slider, the strip's level.
`?mock=1&macros=1` turns the mock device's answer on.

## CZ collection (command 66)

`firmware/src/io/editor/ed_cz.c`, only in a build with `NATIVE_BANKS` and the CZ engine (after Melodee 0.12's command 78,
keremimo/melodee, GPL-3.0-only). The device keeps 26 CZ-1 tones (`nbank.c`, one storage object below the snapshot area); a
CZ track plays slot k as TONE U(k+1), the last 26 values of TONE (its DESC: max + 1 - 26 is U01). A tone is Casio's 144 bytes
(the 128 synthesis bytes of the CZ-1 MIDI layout, then the 16-byte name); on the wire each byte is two nibbles, low first.
The device does not check the bytes (the editor does, with Melodee's `cz_patch_valid`; the playback masks what it indexes
with). The FM6 collection is the FM6 user bank (DX7 SysEx, the FM6 panel). A build without the CZ collection does not
answer 66 (no reply: the editor hides its CZ panel).

| cmd | Request | Reply |
| --- | --- | --- |
| 66 CZ_BANK | 0 (GET), slot 0..25 | 0, slot, rc (0 ok, 1 empty or no such slot), then (rc 0) 288 nibbles |
| 66 CZ_BANK | 1 (PUT), slot, 288 nibbles; or 1, slot alone (erase) | 1, slot, rc: 0 written, 1 arguments (a nibble past 15, a length other than 2 or 290), 2 flash, 3 the transport plays (stop first). Writes flash: allow 1 s |

## Backup and restore (commands 43..48)

`firmware/src/io/editor/ed_backup.c` (builds with flash). Everything the device stores is an **object**: a 4-letter tag, a kind
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
| XSTP | 8 / 16 sections with FELUCCA_SL24_XSTEP: SLOOP 2.4's step extras (nudges, locks, fills) of the sections and the autosave, one raw object of records (u8 id 0..15 a section / 16 the autosave, u16 length, the log record: key, stored form); written back whole at the commit, paired by their key with S01.. and AUTO | 5 |
| FXSL | an older backup's (before 2026-10-14): the FX slots' records of every section and the autosave in one object (u8 id, u8 length, the log record); restored, never read now (listed empty) | 5 |
| FXR1, FXR2 | 8 / 16 sections: the FX slots' records (fx_rec.c) in two raw objects, FXR1 sections A..I (ids 0..8), FXR2 J..P and the autosave (9..16): per record u8 id, u16 length (little-endian), the log record (key, stored form); each written back whole at the commit, paired by its key with S01.. and AUTO; an id outside the object's range refuses it | 5 |
| DLFO | 8 / 16 sections: the drum lanes' LFO records (storage/dlfo_rec.c: each lane's RATE WAVE PHS FADE and two DEST + AMT slots, the lanes not at their defaults) of the sections and the autosave, one raw object of records (u8 id 0..15 a section / 16 the autosave, u8 length, the log record: key, stored form); written back whole at the commit, paired by their key with S01.. and AUTO. A firmware before it does not list it: the editor skips it there, and the sections load with every LFO off | 5 |
| AUTO | the working project (autosave) | 0 |
| UPR1, UPR2 | user presets 1–16, 17–32 ("UPB3"; a backup of "UPB2" / "UPB1" is converted on restore) | 0 |
| UPF6 | the FM6 voices of the user presets ("UPF6": a used mask and 32 packed voices, 7 bits a byte; FELUCCA_UP_FM6) | 0 |
| UKIT | the user drum kit bank ("DKB3", or "DKB1" converted as it loads; FELUCCA_DRUM_KITS) | 0 |
| FM6B | the FM6 user bank U01–U32 (FELUCCA_FM6_STORE) | 2 |
| CZBK | the CZ collection U01–U26 (a used mask, then 26 CZ-1 tones of 144 B; FELUCCA_NATIVE_BANKS with CZ) | 0 |
| USR1..USR3 | the sample slots (one may hold the FM6 user bank) | 1 |
| SNAP | the snapshot area, raw (FELUCCA_SNAPSHOTS; restored through the snapshot import, cmds 54..57) | 6 |

| cmd | Request args | Reply args |
| --- | --- | --- |
| 43 BK_LIST | 4 (the version the editor speaks; 3 and 2 still answered), first (the object to start from) | 4 (3 from an older firmware), n, switches (2 × 7 bit: bit 0 flash, 1 ANALOG 2, 2 DRUM_EDIT, 3 DRUM_USR, 4 DRUM_KITS, 5 DRUM_SENDS (always 1 since 2026-10), 6 song, 7 USB audio), the project format's magic (4 ASCII), the chunk size (2 × 7 bit: 256), first, count, then per object first..first + count − 1: tag (4 ASCII), kind, flags (bit 0 in this build, 1 has data, 2 written with 45..47, 3 a slot holding the FM6 bank), length (3 × 7 bit), CRC-32 (5 × 7 bit); the page that ends the list (first + count = n) ends with what the build holds |
| 44 BK_READ | i, offset (3 × 7 bit) | i, offset, CRC-32 of the chunk (5 × 7 bit), pack7 bytes (≤ 256; fewer at the end) |
| 45 BK_BEGIN | i, length (3 × 7 bit), CRC-32 (5 × 7 bit) | i, rc |
| 46 BK_DATA | i, offset (3 × 7 bit), CRC-32 of the chunk, pack7 bytes (≤ 256, in order) | i, offset, rc |
| 47 BK_COMMIT | i [, 1: version 4, check only] | i, rc |
| 48 BK_END | 1 done / 0 abort | rc (0); done: the device restarts about 150 ms later |

rc: 0 ok, 1 arguments (an unknown object, a sample slot, out of order), 2 CRC (a chunk: send it again; the whole
object at COMMIT), 3 the transport plays, 4 no flash, 5 not in this build (never written), 6 longer than a storage
object, 7 the flash write failed (the old copy stays), 8 at COMMIT: not an object the firmware would load (today's formats
checked in full: a project's size and sum, the settings' calibration a permutation, a bank's shape; older formats by their
magic; after SLOOP 2.3): nothing written.

**Check before writing (version 4).** `BK_COMMIT i 1` runs the same checks as a COMMIT (length, CRC, rc 8) and writes
nothing. A restore sends every storage object (kind 0) through BEGIN / DATA / `COMMIT i 1` first, and writes only when
none is refused: a set holding an object the firmware would not load (rc 8) stops before the first write, not halfway
(the editor's `bkCheckAll`; against a firmware that answers version 3 it is skipped, as a check-only COMMIT would write).
The other kinds (sections, samples, the FM6 bank, snapshots) are not checked this way. The check costs a second
transfer of the storage objects.

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
- **Pages**: a reply is at most 640 bytes (`ed_out.h`: the FM-1's main RAM is full in the larger builds, so the reply
  buffer stays as it is; the wire and WebMIDI take any length). `BK_LIST` v3 sends as many objects as one reply holds with
  room for what the build holds (`BK_PAGE`: 39 objects in 2026-10), and the editor asks again with first = the objects it
  has until it has all n (`bkListAll`); what the build holds comes with the last page. Every switch that adds an object
  on: 42 objects, two pages. An editor speaking v1 / v2 (request 1 or 2, no first) gets the v2 reply (version 2, no first
  and count, the whole list, what the build holds) when the build's list fits one reply, otherwise no reply: it shows no
  backup rather than a part of one. A v2 firmware answers a v3 request with its v2 reply, read as the one page.
- A firmware without these commands does not answer `BK_LIST`: the editor shows no backup.

**The backup file** (`.optimist-backup`): `OPTBKUP` 0x01 (8 bytes), the header's length (u32 LE), the header (JSON:
`{"format": "optimist-backup", "version": 1, "created", "device": {version, proto, switches, magic, bk}, "objects":
[{tag, kind, fm6, len, crc, at}]}`), the objects' bytes (at `at` from the end of the header), then the CRC-32 of
everything before it (u32 LE). Each object is the device's own bytes (its magic and format inside); the editor
refuses a damaged file and leaves out objects the connected device does not have (shown, unticked).

## SLOOP 2.4 export (command 78)

`firmware/src/io/editor/ed_sl24.c` (builds with `FELUCCA_SL24_EXPORT`, default off). A firmware without it does not answer:
the editor shows no button. Nothing is written to flash.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 78 SL24_GET | part, offset (3 × 7 bit) [, flags (v12): 1 = send the second lost word] | part, rc (0 ok, 1 arguments, 2 busy: a restore or an import holds the buffer), length (3 × 7 bit), CRC-32 of the part (5 × 7 bit), lost (2 × 7 bit: bits 0..13), offset, pack7 bytes (≤ 256; none at its end) [, v12 with the flag: lost2 (2 × 7 bit), the reply's last two bytes] |

- **part 0**: the working project as SLOOP 2.4's project (FUN5, 3840 bytes, its FNV-1a sum; `sl24_export.c`), with
  the step extras when the build keeps them. An FM6 voice that is one of 2.4's factory patches F1..F8 exactly (all six
  operators on) is that PTCH; MOD becomes 2.4's MLVL. Engines are numbered as SLOOP 2.5 and 2.6 number them (FUN5 did
  not change in 2.5, and 2.6 reads it): our PHYS is their 10 with its EDIT values; SLOOP 2.4 has no 10 and plays such a
  part as ANALOG (its engine % 10), PHYS's EDIT values on ANALOG's knobs. **lost**, bit 0 an engine SLOOP has not
  (SLICE, ACID, CZ: their fallback SAMPLE, ANALOG, PHASE), 1 an FM6 part (a voice of its own: the closest 2.4 factory patch F1..F8; or M.TIM / C.TIM / an ENGINE
  other than MARK I), 2 FX OFF, 3 ANALOG 2, 4 a drum kit past 2.4's 37 (808), 5 locks on parameters 2.4 has not, 6 the
  drum lanes' record (but their DLY: the level most lanes have is 2.5's drums' delay send, byte 73, which 2.4 ignores), 7 the reverb type / COMP / LIMIT; bit 8 is not a loss: the FM6 voices went into the bank (part 2);
  the FX slots (v10): 9 an amount of an effect in no slot (written 0: 2.4 would play it), 10 COMP (a track's, the drum
  bus's, a drum sound's), 11 a drum sound's DIST, 12 the drum bus's DST CHO DLY REV (9..12: only what is heard here); the automation store (v11): 13 hold
  events (motion), which 2.4 has not (bit 5 also says more than 24 locks a track); **lost2** (v12, bit 0 of the second
  word, which is bit 14 of the build's mask): a step's chance, which 2.4 has not; bit 1 (the mask's bit 15): an LFO RATE
  synced to the tempo (below 0, also in a lock), written as the free rate nearest that note value at the project's tempo
  (2.4 has no sync). A v11 request does not report it. Bit 2 (the mask's bit 16): the drum lanes' motion and locks (the
  automation store's lane ids, below), which 2.4 has not: left out; bit 3 (the mask's bit 17): a drum lane's LFO acting
  (one LFO a lane, the DLFO object below), which 2.4's drums have not: left out. A v11 request gets bits 2 and 3 on bit 13
  (motion). Bit 4 (the mask's bit 18): an FX return below unity. Bit 5 (the mask's bit 19, `SX24_NGATE`): a POLY note's own
  end (above, "A note's own end"): 2.4 has one GATE a step, so the step's own GATE is exported and the note ends there.
  Bit 6 (the mask's bit 20, `SX24_INSORD`, 1048576): a track's FILTER placed before its DIST with both on (S1 FILTER,
  S2 DIST: 2.4 always runs DIST, then FILTER), so the order is lost; S1 DIST, S2 FILTER (the default) is 2.4's order.
  Bit 8 (the mask's bit 22, `SX24_COND`): a step condition (252 COND, below: Felucca 1.5.6), which 2.4 has not: left out (bit
  7 is `SX24_INSFX`, a further insert). A v11 request gets it on bit 13 (motion), as the chance.
  Bit 9 (the mask's bit 23, `SX24_LFO2`): a part's LFO 2 that is not written as
  2.4's LFO: one beside an LFO that acts, or with TRIG FREE, POL UNI, AMP below 0 or a DEST 2.4 has not (2.4 has one
  LFO, NOTE and BI); a LFO 2 alone (the part's LFO DEST off) is written as 2.4's LFO (its RATE, WAVE, its slots as the
  depths). Bit 11 (the mask's bit 25, `SX24_LFODEST`): a slot of a part's LFO DEST that is no depth of 2.4's (another
  DEST, AMP below 0): left out; the slots on PIT FLT SHP AMP are written as the depths (AMT / 2; AMP as it is).
  Bit 12 (the mask's bit 26, `SX24_DACC`): the drum track's ACCENT lane (above, `DRUM_STEP`), which 2.4 and 2.6 have
  not: left out (the hits keep their own levels). The ACCENT amount travels in the drum track's `P_E1` slot, which 2.4
  and 2.6 never read, so a round trip keeps it.

**The second lost word (v12).** The first word has 14 bits and all are used, so the chance cannot have its own bit in
  it. A word appended to every reply would break a v11 editor (it unpacks everything after the offset as data: the two
  bytes would be read as data bytes, and the chunk's offset would run two bytes too far), so the word is sent only when
  asked: the request's fifth byte, flags = 1 (any other flags value, or a fifth byte of 0, is rc 1). The reply is then the v11
  reply with lost2 (2 × 7 bit, low group first) after the pack7 bytes, i.e. as its last two bytes; **lost** bit 13 is the
  motion alone. A request of 4 bytes (a v11 editor) gets exactly the v11 reply, and the chance is reported on bit 13 as before
  (an editor says "motion or chance"). A firmware before v12 answers the 5-byte request with rc 1 (its argument check): the
  v12 editor asks again with 4 bytes, reads the missing second word as 0 and bit 13 as "motion or chance". The editor's
  "left out" list (`S24.LOST`, `S24.LOST2`) has motion and chance as separate entries. Tests: `tests/auto_test.c`
  (firmware side), `web/test_web.mjs` (a v12 editor on a v11 firmware and on a v12 one, a v11 request on a v12 one).
- **part 1**: the settings as SLOOP 2.4's `persist_t` (88 bytes, "PER3": palette, low cut, zoom, the panel table,
  2.4's song order A B C D, its lights word).
- **part 2**: as part 0, but an FM6 voice that is not a factory patch goes into 2.4's FM6 bank (PTCH B1..B27): the
  slot that holds it already, else the first free one (a full bank: the closest factory patch, bit 1).
- **part 3**: that bank, 2.4's object 8 as 2.4 stores it (`fm6_bank_t`, 3472 bytes: "FM6B", version 1, 27 slots, the
  used bits, 27 packed DX7 records): the bank 2.4 left in flash (0xE5000 / 0xE6000, read only) with the voices added,
  else a bank of only those.
- Each read makes the part again from the state now; the editor reads a part again when its CRC moved between chunks.
- The editor saves them in SLOOP 2.4's backup file (`{format: "sloop-backup", version: 1, firmware, date, objects:
  [{id: 0, …}, {id: 1, …}]}`, data base64, CRC-32 zlib; with **FM6 voices into 2.4's bank** ticked, parts 2, 1 and 3,
  the bank as `{id: 8, …}` when bit 8 says a voice went into it): in SLOOP 2.4's editor, BACKUP > Restore makes it
  2.4's working project (and settings, and FM6 bank: it replaces 2.4's); SAVE it there.

## SLOOP 2.4 import (commands 90, 91)

`firmware/src/io/editor/ed_sl24.c` (builds with `FELUCCA_SL24_EDIMPORT`, default off; it needs `FELUCCA_SL24_IMPORT`). A
firmware without it does not answer: the editor shows no button. Nothing is written to flash.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 90 SL24_PUT | 0 (begin), length (3 × 7 bit), CRC-32 (5 × 7 bit) | 0, rc |
| | 1 (data), offset (3 × 7 bit), CRC-32 of the chunk (5 × 7 bit), pack7 bytes (≤ 256, in order) | 1, offset (3 × 7 bit), rc |
| | 2 (commit) | 2, rc (0: imported, the working project now) |
| 91 SL24_BANK | offset (3 × 7 bit) | rc (0 ok, 4 none kept), length (3 × 7 bit: 3472), CRC-32 of it all (5 × 7 bit), offset (3 × 7 bit), pack7 bytes (≤ 256) |

- **The object** (90): a SLOOP 2.4 / 2.5 project (FUN5, 3840 bytes, with its sum) of a SLOOP backup file (its object 0,
  the working project, or 2..5, A..D); a SLOOP 2.6 project (FUN6, 3816 bytes) is made a FUN5 by the editor first
  (`sl26ToFun5`, as the FM-1's `sl24_import.c sl26_to_sl24`: 2.6's ten new parameters but LFO 2's SYNC (it becomes the part's LFO RATE -n, as on the FM-1), its TEXTURE and, on this
  path, the ANALOG filter TYPE left out), then optionally a byte of FM6 parts (bit k: part k's patch follows) and 3 × 128
  bytes: each FM6 part's bank patch (PTCH B1..B27) from the file's object 8, packed DX7. 3840 or 4225 bytes. A part
  on a bank patch the object has not: 2.4's INIT, as 2.4 plays an empty slot. The commit imports it as PROJECT > LOAD
  twice does with a 2.4 project left in flash (`sl24_guard.c sl24_import_buf`): the working project; SAVE keeps it.
- **rc**: 0 ok, 1 arguments / no import begun, 2 CRC (a chunk: send it again; the object at the commit), 3 the
  transport plays (stop first), 6 a length other than 3840 or 4225, 8 not a SLOOP 2.4 project, 9 busy (a restore
  holds the buffer). A begun import holds the project buffer as a restore does: 10 s without a command lets it go.
- **91** reads 2.4's FM6 bank where 2.4 left it in flash (its object 8, 3472 bytes as 2.4 stores it; the current
  valid copy, CRC and layout checked): the editor puts its patches in the free (INIT VOICE) slots of the FM6 user
  bank (the bank's usual write), never over a voice of the user's. rc 4: none kept.

## Snapshots (commands 54..57, protocol v8)

`firmware/src/io/editor/ed_snap.c` (builds with `FELUCCA_SNAPSHOTS`, docs/SNAPSHOTS.md). A **snapshot** is the whole state: the
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

## The automation store (commands 92, 93; v11)

Built with any of `FELUCCA_MOTION`, `FELUCCA_MICRO`, `FELUCCA_FILLS`, `FELUCCA_PLOCK`, `FELUCCA_CHANCE` (the automation
store, firmware/src/seq/auto.h; firmware/src/io/editor/ed_stepx.c; docs/UI-OPTIMIST-DESIGN.md 6 and 11.7): one list of
up to 128 events a track (its working pattern), each 3 bytes: **place** (bits 0..5 the step, bit 6 STEP-ONLY: a lock,
it holds its step; clear: a hold event, motion, it holds until the next event of its parameter or the pattern's
restart), **param** (the stored P_* id, the same in every build; or a pseudo-parameter: 253 NUDGE -32..31 in 1/64 of a
step, 254 FILL 1 fill only / 2 no fill, 255 CHANCE 0..99 %, 252 COND 1..9 = 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4: the step
plays on loop a of every b of its track, `FELUCCA_COND`, Felucca 1.5.6; a build without it keeps the event and plays the
step always; always STEP-ONLY), **value** (a signed byte). The pair
replaces the step extras' commands below (which still answer, on the list). Protocol v11 (v10 is the FX slots, 86 FX and 87 FX_PUSH; v11 = v10 plus AUTO_GET 92 and AUTO_SET 93); INFO unchanged: a firmware
before it does not answer 92 (the editor then keeps to 72..77). 85 stays kept for a push of the patterns.

| Cmd | Request | Reply |
|---|---|---|
| 92 AUTO_GET | track [, first] | track, n (2 x 7 bit), PLAY (the track's hold events play: 0 / 1), first, count, then count x (place, param (2 x 7 bit), v14); at most 64 a reply: ask again from first + count |
| 93 AUTO_SET | track, op, ... | track, op, rc (0 done, 1 refused: an event this track does not take, 2 the list is full), n (2 x 7 bit); op 0 also the value kept (v14) |

AUTO_SET's ops: **0 SET** place, param (2 x 7 bit), v14: the event made or its value changed (a value is clamped to its
parameter's range; a hold event switches its track's PLAY on; a pseudo-parameter at its default, 0 / 0 / 100, removes
the event); **1 DEL** place, param (2 x 7 bit); **2 CLEAR** which (1 the hold events, 2 the step-only ones, 3 all);
**3 PLAY** on (0 / 1). A track takes a step-only event of a lockable value (`FELUCCA_PLOCK`), a hold event of a value
motion records (`FELUCCA_MOTION`), and the pseudo-parameters as step-only events.

**The drum lanes' values** (2026-10-12; no version change: the param was already a stored id). On the drum track (track 3)
only, param **0x7C + (lane << 3 | value)**, 0x7C..0xFB, is lane 0..15's value 0..7: TUNE DECAY SNAP CLICK BEND CUT DRIVE
LEVEL (drum_edit.c DE_*, an order that is stored and never changes), the value its offset as the lane's SOUND page keeps
it (ranges -24..24, -64..63 x 3, -24..24, -64..63 x 2, -24..6; AUTO_SET clamps to them). A step-only event is the lane's
lock on that step (heard by every hit of the lane during the step), a hold event the lane's motion; both are heard at the
lane's hits (a sounding hit keeps the values it started with), over the lane's own values, never written into them. A part
refuses them (rc 1). A firmware before them keeps such events in the list and in every stored form, and plays none.
0xFC stays free; the stored track ids end below 0x77 in every build (0x77: the ACCENT lane's stored events, 0x78..0x7B:
the notes' own ends, neither ever in a working list). The editor's `autoLane(param)` reads an id.

## SLOOP 2.4's step extras (commands 72..77)

Built with `FELUCCA_MICRO`, `FELUCCA_FILLS` or `FELUCCA_PLOCK` (BUILD bits 177..179; firmware/src/io/editor/ed_stepx.c): each
step's nudge, its fill condition and the track's parameter locks, in SLOOP 2.4's model (2.4 has them as its 37..42,
which are our drum commands). A param is Optimist's P_* id. A firmware without them does not reply. Since v11 they read
and write the automation store's list (above): a lock is a step-only event of a value, a nudge and a fill condition
step-only NUDGE / FILL events; a track holds up to 128 events of every kind together (no longer 24 locks).

| Cmd | Request | Reply |
|---|---|---|
| 72 LOCK_GET | track | track, n, then n x (step, param, v14) |
| 73 LOCK_SET | track, step, param [, v14] (no value: delete it) | track, step, param, rc (1 done, 0 not lockable or the list full), v14 kept (clamped to the parameter's range; 0 after a delete) |
| 74 MICRO_GET | track | track, 64 x (nudge + 64): -32..31, in 1/64 of a step |
| 75 MICRO_SET | track, step, nudge + 64 | track, step, nudge + 64 (clamped) |
| 76 FILL_GET | track | track, 64 x condition: 0 normal, 1 fill only, 2 no fill |
| 77 FILL_SET | track, step, condition | track, step, condition |

## Patterns and scenes (commands 79..84)

Built with `FELUCCA_PATTERNS` (firmware/src/io/editor/ed_pat.c; docs/PATTERNS-DESIGN.md): INFO tag `55 02 slots scenes`
(16, the sections built). Each track has 16 pattern slots; a scene (a section) names one pattern a track. A slot value is
7 bits: 0..15 a slot, 127 none, 126 keep (a scene leaves the track playing what it plays), 125 stop (a launch) / a plain
section (in a scene list). No protocol version change; a firmware without them does not reply. The editor shows the
slots in its mixer strips and the scenes on the MASTER strip, polled with PAT_LIST op 0 while the mixer shows.

| Cmd | Request | Reply |
|---|---|---|
| 79 PAT_LIST | op (0 all, 1 the tracks only) | op, changed (bit k: track k's working copy differs from its source), per track: the slot it plays, the launch waiting, when (0 the pattern's end, 1 the next bar, 2 now); op 0 then 4 x 16 LEN (0: empty slot), then 16 scenes x 4 slots (127 x 4: no scene; 125 x 4: a plain section) |
| 80 PAT_LAUNCH | track, slot (127: stop), when | track, slot, rc (0 ok, 1 arguments). Stopped: loaded at once |
| 81 SCENE | op (0 launch: playing on the next bar, stopped at once; 1 store what the tracks play), scene | op, scene, rc (0 ok, 1 arguments or empty, 2 not stored: MEM FULL, no free pattern) |
| 82 PAT_OP | op (0 store the working copy into a, 1 copy a to track2's b, 2 clear a, 3 duplicate into the first free slot), track, a, track2, b | op, rc (0 ok, 1 arguments, 2 not done: MEM FULL, the arena full, a clear while playing) |

| 83 PAT_READ | track, slot, offset (2 x 7 bit) | track, slot, offset (2), total (2: the record's bytes, 0: the slot is empty), pack7 bytes of the record from offset (at most 256) |
| 84 PAT_WRITE | track, slot, offset (2), total (2), pack7 bytes (at most 256) | track, slot, offset (2), rc |

PAT_READ / PAT_WRITE edit a pattern that does not play (the pattern playing is the working copy: the step commands). The
record is the one the log keeps (firmware storage/sections/pat.c; web/editor.html `patRecParse` / `patRecBuild`): a flags
byte (1 a motion chunk follows the header, 2 codec B steps, 4 the drum track, 8 the motion plays, 16 the step extras close
the record, the version in bits 5..7: 0x20 V1, 0x40 V2), LEN DIV SWING GATE, the motion chunk (count, count x 3 bytes),
the step bitmap (LEN bits) and the steps it marks, the extras. V1's chunk holds motion's hold events (at most 64, the
place a step 0..63) and its extras SLOOP 2.4's; a V2 record (phase 3: what V1 cannot hold, a chance, more than 64 hold
events, more than 24 locks) has no extras and its chunk is the automation store's list (above: up to 128 events, the
place with STEP-ONLY). A firmware before V2 reads a V2 pattern as missing. A write sends the chunks in order from offset 0 with the same `total`; the
chunk that completes it checks the record as a launch would decode it and stores it (stopped: in the log; playing: in the
pending arena, written when quiet); total 0 clears the slot (refused while playing). The chunks wait in the device's
`proj_tmp` (also the restore's buffer: a session left for 10 s is given back). A launch waiting for the slot written is
decoded again. rc: 0 ok, 1 not in order or bad arguments, 2 not a pattern of this track (the drum flag, the version bits,
a cut record), 3 busy (a restore or another write holds the buffer: ask again), 4 not written (MEM FULL, the arena full,
a clear while playing). PAT_LIST's "changed" is 0 while the buffer is lent.

85 is kept for a push of the tracks' patterns (PAT_LIST is polled). 86..89 are the FX slots' (v10, below; 88, 89 kept); 90, 91
are SLOOP 2.4's editor import (`SL24_EDIMPORT`).
The backup carries the patterns as PTN1..PTN6 (u8 log id, u16 length, the record, as many as fit each), restored before
the scenes.

## v10: FX slots (commands 86, 87; INFO tag 56)

Four master **slots** (firmware fx/fx_slots.c, io/editor/ed_fxs.c) hold the **send** types (CHO DLY REV), default ---- REV DLY CHO (2026-10-15: R1 R2 R3, the one order of the sends and returns on every screen); each track (T1 T2 T3
DR) has two **insert slots**, S1 then S2, holding an insert type (DIST, FILT or COMP at first; since 2026-10-13 and 14 also FOLD FUZZ PHSR FLNG FRMT WIDE CHRS ECHO GATER DUST JUNO, each a builder item), default S1 DIST, S2 FILT (2026-10-12). A slot is
empty or holds one effect **type**, and a type sits in one slot at most (a track's: in one of its two): loading a type
another slot holds swaps the two slots. The amounts belong to the type, not the slot: a type not heard on a track (no
slot of the track holds the insert, no master slot the send) keeps its amounts. Each track's COMP has its own RATIO
ATK REL; the drum track has send levels REV DLY CHO (0..127, 127 = 100 %) that scale every lane's send of that bus.

`INFO` tag `56 len 01 n` then n × 4 bytes, one per type built: **type id** (stored by the firmware, never reused: 1 DIST,
2 CHO, 3 DLY, 4 REV, 5 COMP (named DYN: a compressor and a limiter), 6 FILT, 7 FOLD, 8 FUZZ, 9 PHSR, 10 FLNG, 11 FRMT, 12 WIDE, 13 CHRS, 14 ECHO, 15 GATER, 16 DUST, 17 JUNO; 0 is an empty slot; the web editor's `FXT_NAMES` lists them), **kind** (0 a send bus, 1 an insert), the **amount id** (a
`P_*`: a synth track's amount and the drum bus's, the drum track's own value: DESC / `TRACK_PARAM` / the v9 `PARAMS` push;
its DESC label names the slot: DST CHO DLY REV CMP FILT), the **sound id** (SOUND 3's value id of a drum sound's amount:
16 REV, 17 DLY, 18 CHO, 19 DST, 20 CMP; 127 none, FILT has no per-sound form). A drum sound's REV DLY CHO are its lane's
sends (`DRUM_LANE` v2, 0..31); its DST and CMP are in `FX` below (0..127). The `PAGES` reply's FX page carries the slots'
amount ids in slot order, SOUND 3 the slots' sound ids.

| Cmd | Request | Reply |
|---|---|---|
| 86 FX | op [args]: 0 get; 1 the four master slots (type ids, the sends only: an insert id, a type twice (its later slot), an id past the table: an empty slot; send the layout after the swap); 2 every track's COMP RATIO ATK REL (the master COMP's value lists: GLO > COMP's DESC names them; clamped); 3 a drum sound's insert: lane 0..15, kind (0 DIST, 1 COMP), amount 0..127 (a kind not built: unchanged); 4 a track's insert slots: track 0..3 (T1 T2 T3 DR), S1, S2 (insert type ids or 0; the same type twice: S2 empty; send the pair after the swap); 5 a track's COMP settings: track, RATIO ATK REL; 6 the drum track's send levels REV DLY CHO (0..127) | op, then the state: 4 master slots (type ids), RATIO ATK REL (T1's; 127 127 127: no COMP built), 16 × the drum sounds' DIST, 16 × their COMP; then (the track insert slots, 2026-10-12; an older editor reads the first 39 bytes) 8 type ids (T1 S1 S2, T2 .., DR), 12 × RATIO ATK REL (T1 .. DR), 3 × the drum send levels REV DLY CHO. A bad op, lane, kind or track: no reply |
| 87 FX_PUSH (push, v9 `WATCH` bit 2) | — | as the FX reply with op 0, when any of it changed on the device (FX > SLOTS, FX > CMP, SOUND 3, a project or snapshot loaded); the editor's own `FX` writes are not echoed |

Not in the `FX` command (2026-10-15): the master's two **insert slots** (type ids stored in the FX record's TLV 0x44), each track's DYN GAIN and CEIL, the drum sounds' two insert slots (TLV 0x46) and the master's three return buses; the editor sees the types in tag 56 and the amounts as parameters. The FX record (`fx_rec.c`, `FXR_VER` 1, at most `FXR_MAX` 288 bytes) is `u8 version, u8 flags (bit 0: the record is of the insert-slot model), u8 slot[4]` (the master sends' type ids), then TLVs `u8 id, u8 length, bytes`. A reader skips a TLV it does not know; a newer version is ignored as a whole. The ids:
- 1..17: one TLV per effect type, the type id as the TLV id (amounts of T1 T2 T3 DR, and a type's own extras, e.g. GATER's PATTERN RATE SHAPE, JUNO's MIX and MODE for the tracks and the master);
- 0x40 (`DLM_TLV`): the drum lanes' PAN, MUTE, SOLO (16 + 4 bytes);
- 0x41 (`FXRET_TLV`): the returns CHO DLY REV, 3 bytes (0..127), only when one is not 127;
- 0x42 (`INS_TLV`): the tracks' S1 S2 type ids (8), each track's COMP RATIO ATK REL (12), the drum track's send levels (3), then each track's DYN GAIN and CEIL (8): 31 bytes, an older reader takes the first 23;
- 0x43 (`SMUTE_TLV`): the sequencer mutes (3 bytes, 4 with the ACCENT lane's);
- 0x44 (`MST_TLV`): the master's S1 S2 type ids, then its amounts for DIST and each further type with no global of its own;
- 0x46 (`LIN_TLV`): the drum sounds' two insert slots, a byte a lane (S1's index, S2's in the high nibble), only when a lane is not S1 DIST, S2 DYN;
- 0x47 (`VCNT_TLV`): the parts' VOICES (0..8, 0 = the engine's maximum), a byte a part, only when one is not 0;
- 0x48 (`UT_TLV`): the USB input track in a USB audio build: LEVEL PAN MUTE FX-ON REV DLY CHO (a byte each, PAN signed), its two slots' types and amounts, then GATER's PATTERN RATE SHAPE and the JUNO chorus' MODE, 15 bytes (11 before 2026-10-15's USB input inserts), only when not every one is the default.
0x45 is not used by the firmware.

The web editor (web/editor.html): the strips' four FX knobs are the slots' amounts, named after them; the drum strip has
a SOUND | BUS switch (the selected sound's sends and inserts, or the drum bus's amounts after its inserts); the MASTER
strip has the four slot pickers and, with COMP built, the CMP card (RATIO ATK REL). Without tag 56 (older firmware) it
shows DST CHO DLY REV as before.

## v13: FX owners (command 94 FXP; INFO tag 57)

`firmware/src/io/editor/ed_fxp.c`. Since the FX rework the firmware has more effect slots than v10's four: two **insert slots**
on every track (T1 T2 T3 DR), on the master and on the USB input, two on each of the 16 drum sounds, and three **returns**
R1 R2 R3 (a send bus CHO DLY REV, or any other effect type, 100 % wet, with a RET level), and each type has its own
parameters (DYN: THRS RATIO ATK REL GAIN CEIL; EQ3: LO MID HI ON FREQ L FREQ H; GATER: DEPTH PAT RATE SHAPE; JUNO: MIX MODE;
the others one amount). FXP reaches all of it, by **owner**, and says what each slot and parameter is (its descriptor is the
device's own, as `DESC`), so an editor draws a slot from the reply and names no type of its own.

`INFO` tag `57 len 01 flags owners`: `flags` bit 0 the USB input is built; `owners` the table's size (25). A firmware without
the tag does not answer 94.

Owners: 0..3 tracks T1 T2 T3 DR, 4 the master, 5 the USB input (slots 0 1, and slot 2: its own values VOLUME PAN MUTE FX ON
REV DLY CHO), 6..8 the returns A B C (slot 0 its type; slot 1, 2026-10-16, its sends to the returns to its right, type 0
always: A's two, labelled B and C, B's one, C, C's none; with the builder item RET_FB each return's two others, A: B C, B: A C,
C: A B, in that order), 9..24 the drum sounds (lanes 0..15; slots 0 1: the types a voice
can hold, its slot's amount only). A type id is the v10 one (`FXT`): 0 empty, 1 DIST, 2 CHO, 3 DLY, 4 REV, 5 DYN, 6 FILT, 7
FOLD, 8 FUZZ, 9 PHSR, 10 FLNG, 11 FRMT, 12 WIDE, 13 CHRS, 14 ECHO, 15 GATER, 16 DUST, 17 JUNO, 18 EQ3; never reused.

| op | Request args | Reply args |
|---|---|---|
| 0 GET | owner | 0, owner, slots n, then per slot: type, parameter count; then the types the owner may hold (5 x 7 bits, bit t = type id t; the empty slot is always allowed); a return: then its send line (0 the chorus line, 1 the delay's, 2 the reverb's: the line a track's `P_CHOR`, `P_DLY`, `P_REV` send feeds; 127 none) |
| 1 TYPE | owner, slot, type | 1, then as op 0. The slot loads the type as the device does (a type the other slot holds swaps the two, amounts with them; on the returns a type another return holds swaps the returns): an editor re-reads the returns after any return changed. A type the owner may not hold: unchanged |
| 2 DESC | owner, slot, i | 2, owner, slot, i, then `DESC`'s body (fmt, min v14, max v14, def v14, label, unit, the enum's names); nothing after i: no such parameter |
| 3 VALS | owner, slot | 3, owner, slot, n, n x v14: every value of the slot |
| 4 SET | owner, slot, i, v14 | 4, owner, slot, i, v14 (clamped to the descriptor's range) |

A bad owner (or USB without the USB input), slot or op: no reply. The parameters of a slot are in the device's order. A
return's first parameter is its line's RET level (the `CRET DRET RRET` global of the line, 0..127 127 unity); a return that
holds a send type has that one only (its own parameters are the global DLY / REV / CHO pages). A return's slot 1 holds its
**sends to the returns to its right** (A -> B, A -> C, B -> C; 0..127 as a track's send, 0 by default, F_PCT), named by
the return each goes to: listed whatever the two returns hold (one to an empty return is kept, unheard); op 1 on it loads
nothing. Left to right only: the device renders the returns A, B, C in that order, so no send comes back (no feedback).
The table's `return.sends` lists each return's (A, B, C). A track's DYN THRS is
`P_TCOMP`, its RATIO .. CEIL the track's own settings; the master's DYN is the `COMP` and `LIMIT` globals. The FX push (87)
goes out too when any setting here moved on the device that no other push covers (EQ bands, DYN's settings, the lanes'
slots, the returns' amounts and sends, the USB input). The parameters' values that are track `P_*` or global `G_*` also arrive in the
v9 `PARAMS` push. The editor's own writes are not echoed.

`tests/fxp_table.json` is the golden table of every owner kind, type and parameter (label, format, range, default, names)
that the firmware's host test checks against its own tables; `tools/gen_fxp_table.py` copies it into web/editor.html (the
mock device's table, `FXP_TABLE`).

## v13: drum lanes' mix (command 95 DLAN; INFO tag 58)

`firmware/src/io/editor/ed_dlan.c`. What the device edits per drum lane that no earlier command reached: the lane's **PAN, MUTE and
SOLO** (drums/drum_mix.c: a muted lane is not heard, a soloed lane silences the others, muted stays muted), its own **LFO** (RATE
WAVE PHS FADE and two DEST / AMT slots on the lane's eight values: drums/lane_play.c), and the **sequencer mutes** (seq/seq_mute.c: the
lane's or the track's steps stop triggering; not the mixer's MUTE). `INFO` tag `58 len 01 groups` (3). Described like FXP: the device's own
descriptors, so an editor names none of them.

| op | Request args | Reply args |
|---|---|---|
| 0 GET | group, lane | 0, group, lane, n (the parameters of it) |
| 2 DESC | group, lane, i | 2, group, lane, i, then `DESC`'s body; nothing after i: no such parameter |
| 3 VALS | group, lane | 3, group, lane, n, n x v14 |
| 4 SET | group, lane, i, v14 | 4, group, lane, i, v14 (clamped) |

Groups: 0 a lane's mix (lane 0..15): PAN (-64..63) MUTE SOLO SEQ MUTE; on a build with the ACCENT lane (the automation store) lane 16:
SEQ MUTE, ACCENT. 1 a lane's LFO (lane 0..15): RATE WAVE PHS FADE DEST AMT DEST2 AMT2 (DEST: OFF, then the lane values TUNE DECAY SNAP CLICK
BEND CUT DRIVE LEVEL). 2 a track's sequencer mute (lane = the track 0..3): SEQ MUTE. A bad group, lane or op: no reply. Any change on the
device goes out in the FX push (87), the editor reads the state again; its own writes are not echoed. The golden table of the
parameters is the `dlane` object of `tests/fxp_table.json`.

## v13: GLOBAL and SYSTEM (command 96 SCRN; INFO tag 59)

`firmware/src/io/editor/ed_scrn.c`; builds with the Optimist UI (FELUCCA_UI 1) only (no tag, no reply otherwise). The settings of the
FM-1 (BPM, SWING, the REC modes, MIDI, the channels, screen, lights, audio, USB, Bluetooth, USB AUDIO's four channels, calibrate, about, the
info read-outs) are not parameters with a descriptor each: the device builds every line from a cell (`op_cells.c` `cell_t`) and turns
it with its own rules. SCRN hands the cells out as the device draws them and turns / does them through the screen's own functions
(`gl_cell`, `gl_turn`, `gl_yes`, op_global.c), so an editor shows the same lines in the same order with the same words and units.

| op | Request args | Reply args |
|---|---|---|
| 0 INFO | screen (0 GLOBAL, 1 SYSTEM) | 0, screen, lines n, pages np |
| 1 LINE | screen, line | 1, screen, line, LINE |
| 2 TURN | screen, line, mode (0 steps as a knob, with acceleration; 1 fine steps, a unit each; 2 the default), v14 steps | 2, screen, line, LINE (after the turn) |
| 3 YES | screen, line, ok (0 may ask a question; 1 does it) | 3, screen, line, rc (1 handled), LINE |
| 4 SAVE | - | 4: the settings into flash (what the device does when SYSTEM is left; once stopped) |

LINE: page, page title (as the device cases it), section (the row's name: TEMPO REC MIDI CHANNELS INFO SCREEN AUDIO KEYS USB USB AUDIO CALIBRATE
ABOUT), kind (0 none, 1 value, 2 read-out, 3 action, 4 enter), label, value text, unit, state (0 plain, 1 warn, 2 error, 3 ok, 4 other colour), danger,
ask (1: the device asks for a YES now), gauge form (1 bar, 2 bipolar bar, 3 few entries, 4 many entries, 5 two entries), gmin v14, gmax v14, gv v14, by-descriptor
(0/1: then fmt, min v14, max v14, def v14), the action's object text. The BLE devices list open on the device, a bad screen or line: no reply.
The help line of a line is `tools/param_help.json`'s `op` group named as its section (`kind: op`, which the editor's copy carries since v13).
`tests/scrn_table.json` is the golden of every line of the host UI test's build; `tools/gen_fxp_table.py` copies it into the editor's mock.

## v13: the song chain (command 97 SONGC; INFO tag 5A)

`firmware/src/io/editor/ed_song.c`; builds with the arranger. The scenes and patterns are SCENE / PAT_* (81, 79..84); this is the order they
play in: PART 1..64 (a scene and a number of bars) and the mode, the rules the Optimist UI's SONG screen has: edited while stopped, INSERT copies the part
after it, DELETE leaves one. Every op answers op 0's reply: 0, rc (0 done, 1 stop first or SONG REC is on, 2 chain full, 3 one part left, 4 a bad argument),
mode (1 SONG, 0 LOOP), playing, scenes built, steps (the chain's longest), count, then count x (scene, bars).

| op | Request args |
|---|---|
| 0 | - (get) |
| 1 | part, scene, bars (1..64) |
| 2 | part (INSERT) |
| 3 | part (DELETE) |
| 4 | mode (0 LOOP, 1 SONG) |
| 5 | - (SAVE: the chain into flash) |

## v13: sequence tools (command 98 TOOLS; INFO tag 5B)

`firmware/src/io/editor/ed_tools.c`; builds with the Optimist UI and `FELUCCA_SEQTOOLS`. The Optimist UI's STEP > TOOLS (REVERSE, RANDOM, COOK, LENGTH
x2 or 1/2) and EUCLID, on the selected track (TRACK, 27 selects it): a drum track's the whole kit, or one lane. The tools run through the device's own
function (`tl_run`, op_tools.c: the UI's YES calls the same), so the steps and their events (locks, motion, chance, conditions, nudges, fills, lane values,
the exact note lengths) move as on the device, one undo level each, and the editor gets a RELOAD. Every reply is: op, rc (0 done, 1 another generator is on,
2 the generator is on: keep or restore first), track, lane, len, hits, steps, rot, on, then two strings: what the device says (`T1 REVERSED`, `DOUBLED: 16` ` STEPS`).

| op | Request args |
|---|---|
| 0 | - (the state) |
| 1 | verb (0 REVERSE, 1 RANDOM, 2 COOK, 3 LENGTH), lane (0..15 or 127: the whole kit / a synth track), half (LENGTH: 1 = 1/2, 0 = x2) |
| 2 | k (0 HITS, 1 STEPS, 2 ROT), value: EUCLID's value, written at once while it is on |
| 3 | lane (127: as it is): EUCLID ON, the generator writes the pattern |
| 4 | EUCLID OFF, the generated steps kept |
| 5 | EUCLID OFF, the pattern as it was given back |

## v13: the device's screens (command 99 SCRS; INFO tag 5C)

`firmware/src/io/editor/ed_scrs.c`; builds with the Optimist UI only (FELUCCA_UI 1; no tag, no reply otherwise). SCRN hands GLOBAL and SYSTEM out as lines; this does it for the
other screens the Optimist UI draws as **rows of four cards**: SOUND (the selected track's pages, a drum lane's), MASTER, PROJECT (the project slots, snapshots, user presets,
TOOLS, SLOOP's import, MEMORY), STEPS, SONG (the scenes, SONG REC, the parts), TEMPO and SCOPE's cards (`ui/optimist/op_screens.c` `screen_t`: rows, name, cell, turn, yes). The device builds each card in its own
screen's functions, and SCRS reads, turns and asks through the same ones, so an editor shows the same pages in the same order with the same names, units, ranges and questions. A page the build
adds, an engine's own rows, a lane's pages (BODY, TONE, ENV, SENDS, KIT, SOURCE for a user sample, the lane's LFO), SONG REC, the scene rows and the MEMORY gauge arrive without an editor change.

`INFO` tag `5C len 03 version mask(2 x 7 bits)`: bit n of the mask = the screen with the device's number n (0 MIX, 1 SOUND, 2 MASTER, 3 PROJECT, 5 STEPS, 6 SONG, 7 TEMPO, 8 SCOPE (its source: `trk`, or `fam` 1 for MASTER); 4 / 9 SYSTEM and GLOBAL are not offered
here: they are SCRN's). MIX has a row for each knob set of each mixer row (MASTER, T1..DR, the 16 lanes, the USB input), named by its code and set ("T1 MIX", "M RETURNS", "SD SENDS"). Every request has the same first five bytes: `op scr trk lane fam`.

| op | Request args (after `scr trk lane fam`) | Reply args |
|---|---|---|
| 0 INFO | - | 0, scr, trk, lane, fam, rows n, the screen's name |
| 1 ROW | row | 1, scr, row, ROW |
| 2 TURN | row, k, mode, v14 steps | 2, scr, row, ROW (after the turn) |
| 3 YES | row, k, ok | 3, scr, row, rc (1 handled), ROW |

`fam` of STEPS is the step held, 0..63 (127 none: PATTERN, TOOLS, EUCLID): the rows are then that step's card pages as the device pages them (STEP: NOTE LEVEL RATCHET LENGTH, a drum step's LEVEL RATCHET TUNE DECAY; EXTRA: CHANCE NUDGE FILL COND; then the lock pages), a step past the pattern's length gets no reply, and a turn edits the step.
`trk` 0..3 is the track the screen is about (SOUND, STEPS), `lane` 0..16 a drum lane picked on the drum track (SOUND then has the lane's pages; 127: none, DR itself), `fam` the page family of
SOUND (127 every row, as the device opens it; 126 an FM6 track's operator rows (a data byte is 7 bits: the device's 0xFF and 0xFE are 127 and 126)). The context is set for the one call and put back: the device's own cursor, track, lane and page do not move.
`mode`: 0 steps (a knob, with its acceleration), 1 fine steps (a unit each), 2 the default (`v14` ignored). YES with `ok` 0 may only ask: the card's `ask` flag is 1 and the row carries the
device's question ("NEW PROJECT?", "CLEAR T1?"); `ok` 1 does it (the device's question times out after 3 s, as on the panel). A bad screen, track or row: no reply.

ROW: the row's family (`FAM_*`, 127 none; SOUND's rows group by it), its flags (bit 0: YES on any card runs the row's operation on its item: a store's row, PROJECT / SNAPSHOT / USER), its name
(as the device titles it), four CELLs, the armed question ("" none). CELL: kind (0 none, 1 value, 2 read-out, 3 action, 4 enter), label, value text, unit, state (0 plain, 1 warn, 2 error, 3 ok,
4 other colour), danger, gauge form (1 bar, 2 bipolar bar, 3 few entries, 4 many entries, 5 two entries), gmin v14, gmax v14, gv v14, by-descriptor 0/1 (then fmt, min v14, max v14, def v14, the
value v14), flags (bit 0 dim, bit 1 the drum track's own value shown on a lane, bit 2 struck through, bits 3..4 the step event mark, bit 5 the card asks), the action's object text.
`tests/scrs_table.json` is the golden of the rows of the host UI test's build (`tests/ui_optimist_edscrs.h`, EDSCRN_WRITE=1 rewrites it); `tools/gen_fxp_table.py` copies it into the editor's
mock (`SCRS_TABLE`).

## v13: the favorites (command 100 FAVS; INFO tag 5D)

`firmware/src/io/editor/ed_fav.c`; builds with `FELUCCA_FAVORITES` (core/favorites.c). The device marks sounds as favorites (SHIFT2 + PRESETS on its panel) and PRESETS can browse the marked
ones only (SHIFT2 + SELECT: the list ALL / FAV). FAVS is that store: a factory sound is marked by its engine's UID and its name, a user preset by its slot, up to 64 factory marks; the marks
are saved with the settings a moment after a change. `INFO` tag `5D len 02 version FAV_N`.

| op | Request args | Reply args |
|---|---|---|
| 0 | - | 0, factory marks, FAV_N, the PRESETS list (0 ALL, 1 FAV), user presets marked |
| 1 | engine | 1, engine, n, the marks of its n presets, 7 to a byte (bit k of the stream: preset k is marked); engine = the number of engines: the user presets |
| 2 | engine, k, on | 2, engine, k, rc (0 as it was, 1 changed, 2 FULL), factory marks |
| 3 | list | 3, list: the PRESETS list the device browses |

A bad op, engine or k: no reply. The engines' own collections (FM6 F01.., CZ Z01..) are not marked.

## v13: the scope (command 101 SCOPE; INFO tag 5E)

`firmware/src/io/editor/ed_scope.c`; builds with the Optimist UI only. The device's oscilloscope (`ui/optimist/op_scope.c`): one ring of the audio (`fx.c` `vis_pcm`) of one source at
a time, drawn trigger-stable (the latest rising zero crossing after a swing below) and scaled to its peak. `SCOPE src` hands out the same trace; `INFO` tag `5E len 03 version n(2 x 7 bits)`
(236 points).

| Request | Reply |
|---|---|
| src: 0 MASTER, 1..4 the tracks T1 T2 T3 DR (a drum lane is DR) | src, shift, peak v14, n (2 x 7 bits), n heights (0..127, 64 the zero line, up is above) |

Asking sets the ring's source as the panel's SCOPE would for the next 2 s, so ask again about every 80 ms to watch; while asked, the mixer's MASTER meter on the device reads that source too.
`shift` is the right shift that fits the peak into the heights (never amplified: a quiet source is a small trace); `peak` is the ring's largest sample (>> 2, at most 8191). A bad src: no reply.
The SCOPE screen's cards (the source's LEVEL PAN FX DRIVE, the master's FILT THRS RATIO GR) are SCRS screen 8.

## v14: undo (command 102 UNDO; INFO tag 5F)

`firmware/src/io/editor/ed_undo.c`. The device's undo and redo (seq/undo.c; the panel's SAVE then HOME is undo, SHIFT then SAVE redo, EDIT + OCT- / OCT+ on SLOOP's UI) for the patterns: steps, the
pattern's LEN and DIV, and the automation store's events (locks, motion, nudges, fills, chance, conditions). UNDO runs the device's own function (`undo_apply`) in the main loop, as the panel does. With this
item built **the editor's own step writes are undo levels too**: `STEP_SET` (7), `TRACK_STEP` (30), `DRUM_STEP` (33), `LOCK_SET` (73), `MICRO_SET` (75), `FILL_SET` (77), `AUTO_SET` (93) and the STEPLANE writes (105) mark
the track first, as a step key on the panel does; writes to the same track within 800 ms are one level (a drag is one undo). A level of a project load or a pattern launch is not kept (the device clears its history there).

`INFO` tag `5F len 02 version flags` (version 1): `flags` bit 0 = a history of many levels (`FELUCCA_UNDO_HISTORY`; else the single level of SLOOP 2.x: depth 0 or 1).

| op | Request args | Reply args |
|---|---|---|
| 0 GET | - | the state |
| 1 UNDO | - | the state (after) |
| 2 REDO | - | the state (after) |

Every reply: `op, rc, done lo, done hi, all lo, all hi, track, can_undo, can_redo`. `rc` 0 done (GET: always 0), 1 nothing to undo (op 1) / to redo (op 2): nothing moved. `done` / `all` are the levels applied and
the levels held (applied plus undone), as 2 x 7 bits (lo, hi). `track` is the track (0..3) of the last undo or redo. `can_undo` / `can_redo` 0 / 1 (an open session that changed something counts as one to undo, though it is
not in `done` until it is closed). After UNDO or REDO that moved something the device sets RELOAD and the v9 pushes (STEPS 60, PARAMS 59) follow as for a change made on the panel: an editor re-reads the pattern
(`TRACK_STEP` / `DRUM_STEP` / `AUTO_GET`) of the track named in `track`. A bad op: no reply. UNDO and REDO are the panel's own functions and refuse exactly what the panel does: nothing to move is rc 1.

## v14: playing notes (command 103 NOTE; INFO tag 60)

`firmware/src/io/editor/ed_note.c`, `firmware/src/seq/ednote.c`. A note on or off for one track, through the device's own note input (`input_on` / `input_off`, seq.c: the path the panel keys and MIDI take, so the arp, the
chord layout, the engine's voice budget with its steal rule, the REC / free-take capture and MIDI OUT KEYS apply as they do to the panel's keys). The audio interrupt takes the notes from a ring of its own (one writer: the main
loop; one reader: the audio interrupt; no interrupt is masked), so nothing in the sound path is shared with the USB side.

`INFO` tag `60 len 02 version quiet` (version 1): `quiet` is the silence in seconds (3) after which the device ends every note the editor holds.

| op | Request args | Reply args |
|---|---|---|
| 0 ON | track, note, vel (1..127) [, dur] | 0, rc, track, note, held |
| 1 OFF | track, note | 1, rc, track, note, held |
| 2 ALL | track (127: every track) | 2, rc, track, 127, held |

`track` 0..2 are the synth tracks, 3 the drum track. A synth track's `note` is a MIDI note 0..127; it goes through the track's layout as a MIDI note does (SCL > KEYS snap, CHORD, TRANSPOSE: `midi_map`). On the drum track `note` is the
**lane** 0..15 (the lane's own sound, as the lane's key plays it; 16 is the ACCENT lane when built, which makes no sound and only records), `vel` becomes the lane's level (`vel_lvl`) and OFF / dur do nothing (a hit is a one-shot).
`dur` (1..127, optional): the note ends by itself after dur x 100 ms. The editor holds at most 8 notes (the device's 8 voices): an ON past 8 ends the oldest one it holds. A repeated ON of a held note retriggers it.
`rc`: 0 done, 1 refused: the ring to the audio interrupt was full (nothing happened; send it again). `held` is the number of notes the editor holds now.
**No hanging notes.** The device ends every note the editor holds when (a) `dur` runs out, (b) the host has sent nothing at all for 3 s (any request counts: a held key needs a `PING` or a repeated ON at least every 2 s),
(c) USB is unconfigured or was reset (cable out), (d) the track changes its sound (the engine panic), (e) the editor sends OFF / ALL. A bad op, track, note or vel: no reply.

## v14: solo and mute (command 104 SOLO; INFO tag 61)

`firmware/src/io/editor/ed_solo.c`. The device's own grammar, no state of its own. **One solo set** across the four tracks, the 16 drum lanes and the USB input: while anything is soloed, every item of those three groups that is not soloed is silent. The returns A B C and the master are never silenced. A soloed lane keeps the drum track heard and only that lane (and the click) sounds in it; every other track and the USB input is silent; a soloed lane that is also muted stays silent. The drum track soloed as a track (GLO + key 8) plays all its lanes unless a lane is soloed too. A track's SOLO is the bit of `song.solo` that GLO + key 5..8 toggles (`trk_silent`); a track's MUTE is `P_MUTE`, as GLO + key 1..4 and `TRACK_MIX` (28) set it; a drum lane's SOLO and MUTE are `drum_mix.c`'s masks (`dlm_set_solo`, `dlm_set_mute`), the same values DLAN (95) group 0 edits; the USB input's SOLO is `SOLO_USB` of `song.solo`, its MUTE is its own MUTE value (`ut.v[UTV_MUTE]`, the mixer's USB row, FX `UT_DESC`). Solo is not saved with a project, except the lanes' mask (it rides in the FX record's lane TLV, as before). `INFO` tag `61 len 02 version flags` (version 3; `flags` bit 0: the build has the USB input, ops 5 and 6 answer).

| op | Request args | Reply args |
|---|---|---|
| 0 GET | - | the state |
| 1 TRACK SOLO | track, on | the state (after) |
| 2 TRACK MUTE | track, on | the state (after) |
| 3 LANE SOLO | lane (0..15), on | the state (after) |
| 4 LANE MUTE | lane (0..15), on | the state (after) |
| 5 USB SOLO (v3) | 0, on | the state (after) |
| 6 USB MUTE (v3) | 0, on | the state (after) |

`on`: 0 off, 1 on, 2 toggle (what the panel key does). The second argument of ops 5 and 6 is ignored (send 0). Every reply: `op, rc, solo, mute, silent, lsolo0, lsolo1, lsolo2, lmute0, lmute1, lmute2, usb`: `rc` 0 done; the track masks (bit n = track n, 0..3): `solo`, `mute`, `silent` (the tracks not heard now:
muted, or something else soloed; the drum track counts as soloed while one of its lanes is); the lane masks as 3 x 7 bits (lane 0..6, 7..13, 14..15). `usb` (v3, the 12th arg; an older reader stops at 11): bit 0 the USB input soloed, bit 1 muted, bit 2 silent (muted, or something else soloed); 0 in a build without the USB input (ops 5 and 6: no reply there). The TRACKS push (62) carries the track solo and mute bits as ever; since v3 it is also pushed when the lane or USB solo changes on the device (its own bytes unchanged: read SOLO 0 for the rest). A bad op, track, lane or on: no reply.

## v14: step lanes (command 105 STEPLANE; INFO tag 62)

`firmware/src/io/editor/ed_steplane.c`; needs the automation store (`FELUCCA_AUTO`; locks need `FELUCCA_PLOCK`). A step seen in the context of a **track and a drum lane**, as the Optimist UI's held step is: the lane's level and
ratchet, the step's extras (chance, condition, nudge, fill) as the one automation store keeps them, and the device's **own list of lockable values** for that track and lane, with the locks and hold events on the step.
Works with either UI (the Optimist UI's list is its lock pages, `lock_pg_ok` / `lock_param` in op_step.c; with SLOOP's UI it is the pages' values that `lock_ok` takes). Writes mark the undo history (ED_UNDO) and take the interrupt off as
`LOCK_SET` does.

`INFO` tag `62 len 03 version a b` (version 1). `a`: bit 0 = the list is the Optimist UI's lock pages (else the pages' lockable values), bit 1 = locks built (`FELUCCA_PLOCK`), bit 2 = a drum lane's values are lockable (TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL),
bit 3 = hold events (motion) built. `b`: bit 0 chance, bit 1 condition, bit 2 nudge, bit 3 fill built (a field of a feature not built reads its default and a SET of it answers rc 1).

Every request starts `op, track, lane`. `track` 0..3. `lane` 0..15 a drum lane, 16 the ACCENT lane (built with `FELUCCA_DRUM_ACCENT`), 127 none; on a synth track `lane` must be 127. A bad op, track or lane (or a short request): no reply.

| op | Request args (after `op track lane`) | Reply args |
|---|---|---|
| 0 LIST | - | 0, track, lane, n (lockable values, at most 127), np (lock pages) |
| 1 DESC | i | 1, track, lane, i, kind, id, page, cell, title (12), fmt, min v14, max v14, def v14, label (8), unit (8), then for an enum (fmt 8) one name (8) per value; nothing after i: no such value |
| 2 STEP | step (0..63) | the state (op 2) |
| 3 SET | step, field, v14 | the state (op 3, after) |
| 4 LOCK | step, i, v14 | the state (op 4, after) |
| 5 UNLOCK | step, i | the state (op 5, after) |
| 6 KIND | step, i | the state (op 6, after) |

**The list** (LIST, DESC): the values the device lets a step lock in this context, in the device's order (pages in the order the device pages them, then a page's cells), no value twice. `kind` 0 = a track parameter
(`id` = its `P_*` id: the `param` of `LOCK_SET` (73) and `AUTO_SET` (93)), 1 = a drum lane's value (`id` = 0..7: TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL, offsets as the lane's sound editor keeps them; stored as the event `AUTO_LANE(lane, id)`, 0x7C + lane x 8 + id).
`page` is the lock page's ordinal (0..np-1), `title` its name as the device shows it ("ENV", "LFO", "BODY"), `cell` the card on it (0..3). `fmt, min, max, def, label, unit, names` are `DESC`'s (5). The list is that of
`(track, lane)`: on the drum track with a lane picked it is the lane's values (+ what the device offers beside them), with lane 127 the drum track's own, with lane 16 the ACCENT amount alone (`P_DACC`; its lock needs the accent hit on the step, as a lane's lock needs the lane's hit: D4).
A value the device does not lock (VCE, GLMOD, PRIO, ALLOC, MUTE, LEN, DIV, SWG, the DEST slots, S1 / S2 TYPE, VOICES, the mixer's, a GO button) is not in the list. Ask LIST again after an engine or kit change.

**The state** (STEP, SET, LOCK, UNLOCK, KIND): `op, rc, track, lane, step, flags, level, ratchet, chance, cond, nudge+64, fill, n, then n x (i, kind, v14)`.
`rc`: 0 done, 1 refused (a bad field, value or value index, or the feature is not built), 2 the event list is full (24 locks a track, the store's 128 events: the device's "NO LOCK LEFT"), 3 no hit: the lane has no hit on that step, so a lock
would never be heard (D4: nothing written), 4 nothing to do (KIND: no event on the step, or the other kind is not allowed there). `flags`: bit 0 = the step has a hit in this context (a drum lane's hit; a synth step with notes),
bit 1 = locks can be written here now (a drum lane: it hits there; a synth step: always), bit 2 = the step is past the pattern's LEN. `level` and `ratchet` are the raw 2-bit codes of `DRUM_STEP` (33) (level: 0 NORM, 1 GHOST, 2 SOFT, 3 HARD; ratchet 0..3 = x1..x4) of the lane;
on a synth step those of its first note (SET writes every note). `chance` 0..100 %, `cond` 0 none, 1..9 = 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4, `nudge+64` the nudge -32..31, `fill` 0 normal, 1 fill only, 2 no fill: the step's, as AUTO_GET lists
them (a drum step has one of each for the whole step, whatever the lane). Then the step's events that are in this context's list: `i` (the DESC index), `kind` 1 = a lock (step only), 2 = a hold event (motion), `v14` its value.
**SET** fields: 0 level (raw 0..3), 1 ratchet (0..3), 2 chance (0..100), 3 cond (0..9), 4 nudge (-32..31), 5 fill (0..2). Level and ratchet need the hit (else rc 3, nothing written); the others are the step's.
**LOCK** writes the lock of value `i` at `v14` (clamped to the value's range) as the device's knob does: a hold event of the value already on the step takes the value instead (kind 2 stays), else a step-only event.
**UNLOCK** drops the step's events of value `i`, both kinds (the device's SHIFT + knob). **KIND** turns the lock into a hold event or back (the device's YES on the cell; a hold needs motion built).
An event of `LOCK_SET` / `AUTO_SET` on the same step is the same event: the editor can mix the commands.

`tests/steplane_table.json` is the golden of the list for a synth track and for the drum track (lane 127, a lane, the ACCENT lane) of the host test's build (`tests/editor_sync_test.c`, `STEPLANE_WRITE=1` rewrites it); `tools/gen_fxp_table.py` copies it into
web/editor.html (`STEPLANE_TABLE`: data only, the mock device does not answer the command).

## Notes for the editor

- **One request at a time.** Wait for the reply, about 10–50 ms, before sending the next.
  The device holds only one incoming SysEx frame.
- **Following the device.** With v9 firmware, `WATCH 15` and `PING`: every change is pushed, nothing is polled. With v2..v8
  firmware, `WATCH` and `PING` (above) and a slow `DUMP` / mixer / `STATUS` poll for what is not pushed. Older firmware pushes
  nothing (no reply to `PING`): poll `DUMP` about every 300–500 ms while the page is visible.
- **Port.** The device's MIDI port is named "Felucca" (USB 1209:0001; Optimist keeps the name so editors
  and installers find it). Updates use the same
  port with other SysEx (the `F0 22 24 35 …` keys, `00 59 …` frames); never send those
  from the editor.
- **Safety.** Only `PROJECT` save, `SN_OP` save / load / clear / rename, `SN_WRITE`, the sample-slot commands, `UP_PUT` / `UP_STORE` / `UP_ERASE`, `UKIT_PUT` / `UKIT_OP`
  (erase, store, rename) and `BK_COMMIT` (and the session's first `BK_BEGIN`, which saves what is in RAM) write flash, and only in
  Felucca's own storage; never the app or the update area.

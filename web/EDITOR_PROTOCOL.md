# Optimist editor protocol (SysEx over USB-MIDI)

The wire format between the web editor (`web/editor.html`) and the firmware (`firmware/src/io/editor/`: `editor.c`, `ed_lite.c`
and one `ed_*.c` per feature). Optimist is based on SLOOP, which is based on Felucca, so the frames keep Felucca's "FL" header
and the device's MIDI port is still named "Felucca" (USB 1209:0001). The protocol byte in `INFO` is 6; every later addition is
an `INFO` tag or a byte appended at the end of a reply, never a new version.

A client offers a command only when the build answers it: the `INFO` tags (below) say which, and a command the build does not
have gets no reply at all (ask once with a short timeout when no tag names it).

## Frames

A request is `F0 7D 46 4C <cmd> <args...> F7` (`7D` the non-commercial SysEx ID, `46 4C` = "FL"). Every data byte is 7 bit.
Every request gets exactly one reply with the same header and `<cmd>`; a request the device does not understand gets none.

- One request at a time: wait for the reply (about 10-50 ms) before the next. The device holds one incoming frame.
- A request with cmd below 11 is cut at 64 bytes. A reply is at most 640 bytes (`ed_out.h`), so long lists come in pages.
- While the editor watches (below) the device also pushes frames at any time; match replies by cmd. Pushes are 24, 59..63, 87.
- Updates of the firmware use the same port with other SysEx (`F0 22 24 35 ...`, `00 59 ...`): never send those from the editor.
- Only `PROJECT` save, the sample-slot commands, `UP_PUT` / `UP_STORE` / `UP_ERASE`, `UKIT_PUT` / `UKIT_OP`, `SN_OP`
  save / load / clear / rename, `SN_WRITE`, `BK_COMMIT` (and the session's first `BK_BEGIN`, which saves what is in RAM) and the pattern
  commands that store (81, 82, 84) write flash, and only Felucca's own storage areas, never the app or the update area.

## Values and formats

| Item | Encoding |
| --- | --- |
| v14 value | 2 bytes, LSB first, value + 8192: `value = (lo \| hi << 7) - 8192`, -8192..8191 |
| string | ASCII bytes, ended by a 0 byte |
| scope | 0 = parameter of the selected track (`P_*`, 0..P_COUNT-1); 1 = global parameter (`G_*`, 0..G_COUNT-1); 9 = the reverb type and MIDI settings (tags 52, 54) |
| track | 0..2 the synth parts, 3 the drum track (NTRK = 4) |
| engine byte | 0..NENGINES-1; NENGINES = the drum track (no engine, no presets) |
| n x 7 bit | an unsigned number in n bytes, LSB first (offsets 3 x 7 bit, CRC-32 5 x 7 bit) |
| pack7 | groups of up to 7 bytes, each preceded by one byte holding their top bits (bit j = bit 7 of byte j) |

`P_E0..P_E7` are the engine's eight parameters (`INFO` gives `P_E0`; their meaning and range follow the track's engine, so read
`DESC` again after an engine change). The ids after `P_E7` up to `P_COUNT-1` (FX amounts, LFO 2, ENV 1 MOD / ENV 2 slots,
VOICES) are not part of a sound; `DESC` names them. Nothing in the editor needs a table of ids of its own: read `DESC`.

`fmt` of a parameter (`firmware/src/core/core.h`): 0 INT, 1 PCT (share of the range), 2 BIPCT, 3 TIME, 4 LFOHZ, 5 CUTOFF, 6 DB,
7 SEMI, 8 ENUM, 9 BPM, 10 NOTE, 11 ONOFF, 12 OCT, 13 STEPS, 14 SWING (0..100 shown as the MPC swing, 50 % + value / 4), 15 FILT
(-64..63: below 0 a low-pass, above 0 a high-pass). LFOHZ below 0 is a note value of the tempo, one cycle each: -1 4BAR, -2 2BAR,
-3 1BAR, -4 1/2, -5 1/4, -6 1/8, -7 8T, -8 1/16, -9 16T, -10 1/32. The editor shows the value with its unit; it need not format
it as the device does.

CRC-32 is zlib's everywhere.

## Builder items and what they answer

The web editor is a set of builder items (`docs/BUILDER.md`). A build that leaves one out sends no `INFO` tag for it and does not
answer its commands; the page hides what needs them and keeps the rest (its mock device serves any subset, `?off=dlan,scope`).

| Item | Commands | INFO tags |
| --- | --- | --- |
| `EDITOR` (base, 212 B) | frames, 1 INFO, 25 PING, 49 BUILD | 64 (lite builds) |
| `BACKUP` | 43..48 backup; with live editing off also 11..15 sample slots and 54..57 snapshots | 64 |
| `LIBRARIAN` | 16..21 user presets, 38..41 user kits (`DRUM_KITS`); with live editing off also 9 PROJECT, 42 SMP_READ, 79..84 patterns | 55 (patterns), 64 bit 1 |
| `ED_LIVE` | everything else: 2..15, 22..24, 27..33, 36, 37, 42, 50..52, 54..57, 59..66, 78..84, 90, 92, 93, 100, 102, 103 | 52..55, 5D, 5F, 60 |
| `ED_MIX` (needs ED_LIVE) | 86 FX, 87 FX_PUSH, 94 FXP, 95 DLAN, 104 SOLO | 56, 57, 58, 61 |
| `ED_SEQ` (needs ED_LIVE) | 97 SONGC (arranger), 98 TOOLS (`SEQTOOLS` or `NOTEGEN`), 105 STEPLANE (automation store) | 5A, 5B, 62 |
| `ED_SCREENS` (needs ED_LIVE) | 96 SCRN, 99 SCRS, 101 SCOPE | 59, 5C, 5E |
| `ED_MIRROR` (live editing or not; default off) | 106 MIRROR and its pushes | 65 |

Other switches: 36..42 need the drum lanes (kits: `DRUM_KITS`), 65 `MACROS`, 66 the CZ collection, 78 / 90 `SL24_EXPORT` / `SL24_EDIMPORT`
(default off), 92 / 93 the automation store, 100 `FAVORITES`, 79..84 `PATTERNS`, 54..57 `SNAPSHOTS`.

**Live editing off** (`ED_LIVE=0`, `ed_lite.c`): `INFO` is a short header with tag `64` (one byte: bit 0 backup only, bit 1 the
librarian). Without the librarian it has no engine, parameter or track (`NENGINES` 0, `P_COUNT` 0) and the build answers
1, 25, 49, 11..15, 43..48, 54..57. With it, `INFO` is the live header without the globals (`G_COUNT` 0) and the build also answers
9, 16..21, 38..42 and 79..84; 36 / 37 (the lanes) are not answered. No `WATCH`, `GET`, `SET`, `DESC`. The page shows the Projects tab (the librarian:
sections, patterns, kits, backup) and the Library tab (user presets).

### Removed

The web editor used none of these and no build answers them (the page still carries the numbers for older firmware):
`STATUS` 53 and its stream `STREAM` 58 (transport, playing steps, meters; `WATCH` bit 3 is never granted), `REC` 107 and its tag 63,
SLOOP 2.4's step extras 72..77 (`LOCK_*`, `MICRO_*`, `FILL_*`: the automation store 92 / 93 has them), the pushes `CHANGED` 23,
`STEP_CHANGED` 26 and `TRACK_CHANGED` 32 (and `WATCH` bits 1 and 3), the 2.4 settings and FM6 bank parts of 78 (parts 1..3, rc 1) and
`SL24_BANK` 91, FM6's user-bank commands. Numbers 34, 35, 85, 88 and 89 are unused (reserved).

## INFO (1) and BUILD (49)

`INFO` request: none. Reply: version string ("FELUCCA <version>"), NENGINES, P_COUNT, G_COUNT, NSTEP, `P_E0`, NENGINES engine-name
strings, NTRK, the protocol byte (6), each engine slot's UID, then tagged blocks `id, length, bytes` (skip unknown ids). The tags:

| Tag | Bytes | Meaning |
| --- | --- | --- |
| 52 | `03 mask scope id` | the reverb algorithms built (bit 0 ROOM, 1 SPRING, 2 PLATE, 3 FDN8, 4 AIRWIN). With two or more, FX > REVERB > TYPE is `DESC` / `GET` / `SET` at (scope, id), its value names the algorithms; one built: `127 127`, no TYPE |
| 53 | `01 caps` | live sync (3: bit 0 a repeated `WATCH` keeps what is not pushed yet, bit 1 no `RELOAD` after the editor's own `PRESET` / `SET` of `G_ENGSEL`) |
| 54 | `03 mask scope id` | MIDI settings (mask bit 0 channels, 1 MIDI OUT, 2 MIDI IN): `DESC` / `GET` / `SET` at (scope 9, id + n): n 0..3 the MIDI channel of tracks 1..3 and the drums (OFF, 1..16; the project's), 4 MIDI OUT (KEYS, SEQ), 5 MIDI IN (NOTES, CLOCK) |
| 55 | `02 slots scenes` | patterns built (16 slots a track, the scenes) |
| 56 | `len 01 n`, n x (type, kind, amount id, sound id) | the FX types built (below, "FX") |
| 57 | `03 01 flags owners` | FXP: flags bit 0 the USB input is built, owners = the table's size |
| 58 | `02 01 groups` | DLAN: groups (3) |
| 59 | `02 01 screens` | SCRN |
| 5A | `01 01` | SONGC |
| 5B | `03 03 p t` | TOOLS, version 3: `p` bit 0 the tools (op 1, `SEQTOOLS`), bit 1 the generators (ops 4..9, `NOTEGEN` or `GEN_GRIDS`), bit 2 EUCLID's own ops 2, 3 (`NOTEGEN`); `t` the generator types built, a bit each (bit 0 EUCLID, 1 RANDOM, 2 PULSE, 3 GRIDS). Version 2 (`01 02`) had all of them but GRIDS |
| 5C | `03 01 mask(2 x 7)` | SCRS: bit n = the screen with the device's number n built |
| 5D | `02 01 FAV_N` | FAVS |
| 5E | `03 01 n(2 x 7)` | SCOPE: n points (236) |
| 5F | `02 01 flags` | UNDO: bit 0 = a history of many levels, else one |
| 60 | `02 01 quiet` | NOTE: seconds of host silence (3) after which the device ends held notes |
| 61 | `02 04 flags` | SOLO, version 4: bit 0 the USB input is built, bit 1 the returns' mute (op 7) |
| 62 | `03 01 a b` | STEPLANE (below) |
| 64 | `01 flags` | lite build: bit 0 backup only, bit 1 the librarian |

`BUILD` (no args): the build's name string (16), its config hash (5 x 7 bit), the item bytes count and bytes (bit n = registry item n
built, 7 a byte). The backup file records it.

## Core commands

| cmd | Request | Reply |
| --- | --- | --- |
| 2 GET | scope, id | scope, id, v14 |
| 3 SET | scope, id, v14 | scope, id, v14 (clamped; a fixed-range value stays). `SET` of `G_ENGSEL` (the global labelled "ENG") changes the engine with its defaults |
| 4 DUMP | - | engine byte, preset, P_COUNT x v14 (the selected track), G_COUNT x v14 (globals) |
| 5 DESC | scope, id | scope, id, then the body: fmt, min v14, max v14, def v14, label (8), unit (8), for an enum (fmt 8) one name (8) per value (at most 64) |
| 6 STEP_GET | index 0..NSTEP-1 | index, then the step (below) |
| 7 STEP_SET | index, step bytes | as STEP_GET, after the write |
| 8 PRESET | engine, preset | engine byte, preset. Applies the sound, sends and arp; never the pattern, the mix or the key (`LEVEL PAN MUTE`, `LEN DIV SWG GATE`, `ROOT SCL QNT CHORD` stay) |
| 9 PROJECT | op (0 load, 1 save, 2 query), slot 0..FELUCCA_SECTIONS-1 (A..P) | op, slot, used (0 empty, 1 used; with `SL24_SAFE` 2 = a SLOOP 2.4 project kept in the slot, 3 = another firmware's; neither is loaded), and a fourth byte 1 when the transport plays and nothing was done (stop first). Save writes flash: allow ~2 s |
| 10 NAMES | engine | engine, count, count preset-name strings (12), the two edit-page titles (8) |
| 22 WATCH | on (bit 0 on, bit 4 note ends) | the bits granted (below) |
| 25 PING | - | 0 |
| 27 TRACK | - (query), or track (select it) | selected, NTRK, per track: engine byte, preset, level v14, mute (0/1), armed (0/1: `song.rec` bit), then the solo mask (bit a track) |
| 28 TRACK_MIX | track (get), or track, level v14 (0..127), mute (set) | track, level v14, mute. The drum track's level is the global `G_DRLVL`; mute is `P_MUTE` |
| 29 TRACK_DUMP | track | track, engine byte, preset, P_COUNT x v14 |
| 30 TRACK_STEP | track, index (get), or track, index, step bytes (set) | track, index, step |
| 31 TRACK_PARAM | track, id (get), or track, id, v14 (set) | track, id, v14 (clamped as `SET`; the selection does not change). A track >= NTRK or id >= P_COUNT: no reply. The drum track's level is `G_DRLVL`; its `P_E0` is the kit |
| 33 DRUM_STEP | index (get), or index, on (3 x 7 bit), lvl (5 x 7 bit), rat (5 x 7 bit) (set) | index, on, lvl, rat: the drum track's step, whichever track is selected |

**A synth step** (`STEP_GET` / `TRACK_STEP` reply after the index): n (0..4 notes), note0..3, time (0 NOTE, 1 TIE, 2 REST), flags
(1 accent, 2 slide; 0x80 is `SF_CARRY`, see hi), vel, lvl, hi, rat, e0..e3. `lvl` holds 2 bits per note (note k in bits 2k..2k+1:
0 NORM, 1 GHOST, 2 SOFT, 3 HARD), `rat` 2 bits per note (0..3 = x1..x4); each is 8 bits sent as 7, their bit 7 in `hi` (bit 0: lvl,
bit 1: rat, bit 2: `SF_CARRY`, a carry step: notes already sounding go on, not struck again). e0..e3 are each note's own end: 0 = the step's
GATE, 1..127 = k / 128 of the step after its start (`seq/ngate.h`). A write may stop after vel (lvl, rat become 0), after rat (the
own ends are kept where the note is unchanged, else 0) or carry all 15 bytes.

**The drum track through STEP_GET / TRACK_STEP:** its first 4 lanes that are on as GM notes (lane order), time NOTE (REST if none), flag 1 if one is HARD, vel 100, lvl / hi / rat 0. A write puts each note on its nearest lane
(`LANE_OF_GM`, GM 35..81), HARD if the accent flag is set; TIE / REST clears the step.

**`DRUM_STEP`:** `on` is 16 bits (bit l = lane l), `lvl` and `rat` 32 bits each (lane l in bits 2l..2l+1). A set replaces the whole step. An index >= NSTEP: no reply.
The drum track has 10 lanes: lanes 0..9 are the device's, 10..15 are sent as zeros and ignored when written (every message keeps the 16-lane width).
Lane 16 is the **ACCENT lane**, a step lane with no sound of its own: bit 16 of `on` (the third byte's bit 2) and bits 32..33 of `lvl` and `rat` (the fifth
group's bits 4..5). It raises every hit of that step by the drum track's ACCENT (`P_E1`, 0..127, default 27) scaled by the accent step's own
level (NORM x1, GHOST x0.42, SOFT x0.72, HARD x1.27), at most velocity 127. A write without those bits turns the accent off.

Lanes (`drums.c` `LANE_NOTE`): one per white key from F3; a black key plays the lane of the white key left of it.

## Sample slots and user presets

`ed_user.c`. **Sample slots** (`SMP_*`): 3 slots, USR1 and USR2 80 KiB, USR3 64 KiB (its last 16 KiB are the kit bank). Reference uploader
`tools/fm1_sample_upload.py`, slot builder `sampleio.user_slot`; the editor's port is checked byte for byte by `web/test_web.mjs`.

| cmd | Request | Reply |
| --- | --- | --- |
| 11 SMP_BEGIN | slot 0..2 | slot, rc (0 ok, 3 the transport plays). Erases the slot's header sector: the slot is empty from now on |
| 12 SMP_WRITE | slot, offset (3 x 7 bit), pack7 data (<= 256 bytes) | slot, offset (3 x 7 bit), rc: 0 ok, 1 arguments, 2 erase, 3 write, 4 slot in use (send `SMP_BEGIN` first), 5 the transport plays. Offset >= 512 and a multiple of 256, in increasing order |
| 13 SMP_END | slot, pack7 header (480 bytes) | slot, rc: 0 ok, 1 size, 2 header, 3 data CRC, 4 flash, 5 zones |
| 14 SMP_ERASE | slot | slot, rc (0 ok, 3 the transport plays). ~1 s |
| 15 SMP_INFO | - | slots, slot KiB, per slot: zone count (0 empty), name (8), data KiB; then per slot its capacity in KiB |
| 42 SMP_READ | slot, offset (3 x 7 bit), count (2 x 7 bit, <= 256) | slot, offset, pack7 bytes of the slot's flash (fewer at its end) |

Slot layout (little endian): header at 0, IMA ADPCM data (4 bit, low nibble first, predictor 0, step index 0) at 512. Header: magic `"FSMP"`, u16 version 1,
u8 zones 1..16, u8 0; name (8, 0-padded) at 8; u32 data length, u32 CRC-32 of the data at 16; 16 zones x 28 bytes at 32: u32 offset, n samples, loop start,
loop end, rate (Hz / 44100 x 65536); i16 root x 16, ADPCM predictor at the loop start; u8 step index at the loop start, lo note, hi note, looped.

**User presets** (need `LIBRARIAN`): 32 slots (0..31, the device shows U01..U32). A preset = engine (127 on the wire = an engine this build leaves out: kept, not loadable), name (1..12 ASCII 32..126), P_COUNT values
(v14, `DUMP` order, clamped to the engine's ranges) and a 16-step pattern, 16 x (note 0..127, 0 = rest; flags 1 accent, 2 slide, 4 tie). Loading applies the engine and sound only.
Frames stay below 640 bytes.

| cmd | Request | Reply |
| --- | --- | --- |
| 16 UP_LIST | start, count (1..16) | start, count, total slots, per slot: used, engine, name ("" if unused) |
| 17 UP_GET | slot | slot, used, engine, name, P_COUNT x v14, 16 x (note, flags); an empty slot: used 0, zeros |
| 18 UP_PUT | slot, engine, name, P_COUNT x v14, 16 x (note, flags) | slot, rc (0 ok, 1 arguments, 2 flash, 3 the transport plays). 1 s |
| 19 UP_STORE | slot, name ("" = the automatic name, "ANALOG 07") | slot, rc (as UP_PUT; 1 also for the drum track selected). Stores the current sound |
| 20 UP_LOAD | slot | slot, rc (0 ok, 1 empty / invalid) |
| 21 UP_ERASE | slot | slot, rc (0 ok, 1 arguments, 2 flash, 3 the transport plays) |

## Live sync

`WATCH` request: bit 0 on, bit 4 = also the note ends in `STEPS`. Reply: the bits granted (0, or 1 + 4 + the 16 asked for). Anything else asked is not granted.
Watching ends by itself 3 s after the last request of any kind (send `PING` about every 1 s), on a USB reset, and with `WATCH 0`.
A repeated `WATCH 1` while watching keeps what has not been pushed yet. While on, the device pushes:

| cmd (push) | Bytes |
| --- | --- |
| 24 RELOAD | engine byte, preset, selected track. The engine, a preset, a user preset or a project was loaded, or another track was selected on the device: re-read `DESC` of the engine parameters, `DUMP` and the steps. Not sent after the editor's own `PRESET` / `SET G_ENGSEL` / `TRACK` (the editor re-reads `DUMP` itself) |
| 59 PARAMS | n x (where, id, v14): where 0..3 a track's `P_*` (any track), 127 a global `G_*` |
| 60 STEPS | track, first index, count, then count steps: a synth step as `TRACK_STEP` gives it from n on (11 bytes; with `WATCH` bit 4 + e0..e3: 15), a drum step as `DRUM_STEP` (on 3, lvl 5, rat 5: 13 bytes); a run of changed steps |
| 61 LANE | as the `DRUM_LANE` v2 reply: 0x40 + lane, pack7 (15 bytes: lane + sends). Re-ask `DRUM_SHOW` when the source moved |
| 62 TRACKS | as the `TRACK` reply |
| 63 SONG | the sections stored (3 x 7 bit, bit n = section A + n), the song's parts, loop, a change count of the snapshot list (re-read `SN_LIST` when it moved) |
| 87 FX_PUSH | as the `FX` reply with op 0 (with `ED_MIX`), and also when FXP / DLAN settings moved on the device |

**How:** the device keeps shadows of what the editor knows. Every **20 ms** one scan compares them and sends what differs with its value now (a knob sweep:
one entry a window), at most 4 frames a window, each at most 96 bytes, only while no request waits and the SysEx ring is at most half full; what does not fit goes in a later window.
The editor's own writes and reads (`SET`, `TRACK_PARAM`, `TRACK_MIX`, step writes, `DRUM_LANE(S)`, `UKIT_OP`, `FX` / `FXP` / `DLAN` writes, `TRACK`, `DUMP`, `TRACK_DUMP`) are known: nothing echoes back.
A push never delays a reply. The editor follows the device with `WATCH`, `PING` and these pushes and polls nothing, except what has no push: the solo of a lane or the USB input (`SOLO` 0), the
undo state, the macros (65) and the patterns (79).

| cmd | Request | Reply |
| --- | --- | --- |
| 64 SYNC_STATS | - (read), or any byte (read, then reset) | 5 fields of 5 x 7 bit: scans, push frames, push bytes, the longest scan (us), all scans (us). A measurement aid |

## Drum lanes and user kits

`ed_drums.c`, `ed_dsend.c` (need the drum switches; 36 / 37 need `ED_LIVE`, 38..42 `LIBRARIAN` or live). The lanes keep the 16-lane wire width (above); the bank is `DKB4`.

| cmd | Request | Reply |
| --- | --- | --- |
| 36 DRUM_LANES | - (get), or pack7 lanes (204 bytes, set) | pack7 lanes (204 bytes), as the device has them (clamped) |
| 37 DRUM_LANE | lane 0..9 (get), or lane, pack7 lane (12 bytes, set) | lane, pack7 lane (12 bytes) |
| 38 UKIT_LIST | - | 16, then per slot used (0/1). No names: a kit is KIT n |
| 39 UKIT_GET | slot 0..15 | slot, used, pack7 kit (196 bytes; zeros if empty) |
| 40 UKIT_PUT | slot, pack7 kit (196 bytes; used 0 = erase) | slot, rc: 0 ok, 1 arguments, 2 flash, 3 the transport plays |
| 41 UKIT_OP | slot, op | slot, op, rc (as 40). op 0 load the kit into the project, 1 erase, 2 store the project's lanes |

**Version 2** (the lanes' sends: REV 0..31, 4 = as the kit has it; DLY 0..31; CHO 0..31; 3 bytes a lane) is asked first with `DRUM_LANES` 2: no reply means no sends (keep to version 1).
A version 1 set keeps the sends; a version 1 kit write stores REV 4, DLY 0, CHO 0. `G_DRREV` is retired.

| cmd | Request | Reply |
| --- | --- | --- |
| 36 v2 | 2 (get), or 2, pack7 lanes + sends (252 bytes) | 2, pack7 (252 bytes) |
| 37 v2 | 0x40 + lane (get), or 0x40 + lane, pack7 lane + sends (15 bytes) | 0x40 + lane, pack7 (15 bytes) |
| 39 v2 | 0x40 + slot | 0x40 + slot, used, pack7 kit + sends (244 bytes) |
| 40 v2 | 0x40 + slot, pack7 kit + sends (used 0 = erase) | 0x40 + slot, rc |

**A lane** (12 bytes): 8 signed offsets from the kit's sound (TUNE -24..24 semitones, DECAY, SNAP, CLICK -64..63, BEND -24..24, CUT, DRIVE -64..63, LEVEL -24..6 dB; 0 = as the kit; a sampled
sound uses TUNE DECAY CUT LEVEL), the source (0 the project's kit, 1..3 USR1..USR3, 16 + k kit k's sound, 64 + v the X0X 909's voice v, 80 + s the X0X 808's sound s; a build without that machine plays the
synthesised sound), and the user-sample reference in 3 bytes: hit 4 bits, start 10 bits, length 10 bits (1/1024 of the hit, 0 = to its end): `r0 = hit << 4 | start >> 6`, `r1 = (start & 63) << 2 | length >> 8`, `r2 = length & 255`.
**The lanes** (204 bytes, `dlanes_t`): offsets[16][8], source[16], reference[16][3], the user kit they came from (1..16, 0 none), 8 reserved, 3 zero.
**A user kit** (196 bytes, `ukit_t`): 0xA5 (used), the kit `KIT` means, source[16], reference[16][3], offsets[16][8], 2 zero; every lane has its kit as the source (16 + k).

**Kit files** (the editor's export / import): JSON `{"format": "sloop-drumkit", "version": 2, "kit": {base, lanes: [{ofs[8], src, hit, start, len, snd: {rev, dly, cho}}]}, "slots": {"0".."2": {hdr, data}}}`; `hdr` / `data` are a slot's header (480 bytes)
and ADPCM data in base64 as `SMP_READ` gave them; an import writes them back into the same USR slots (asked first), then the kit. Version 1 files have no `snd`.

## Drum sources, sounds and pages

`ed_dsrc.c`, `ed_pages.c`. The editor builds its kit editor and sound editor from these; ask `DRUM_SRCS` 0 and `PAGES` 0 once (no reply: the editor uses its own layout). `DRUM_SHOW` is there when `DRUM_SRCS` is.

| cmd | Request | Reply |
| --- | --- | --- |
| 50 DRUM_SRCS | start | start, total, n, then n x (src, kind, name (12)): the SOURCE list from start (at most 24 a reply: ask again from start + n until total) |
| 51 DRUM_SHOW | lane 0..9 | lane, mask (2 x 7 bit), flags, name string |
| 52 PAGES | start | start, total, n, then n x (family, scope, shown, id0..id3, title (8)) (at most 24 a reply) |

- **src** is a lane's source byte (above); 127 = an entry this build steps over (leave it out). **kind**: 0 the project's kit, 1 a user sample slot, 2 a sampled kit, 3 a synthesised kit, 4 an X0X machine; + 8: not in this build (a stand-in plays).
- **DRUM_SHOW:** mask bit i = offset i (TUNE .. LEVEL) applies to this lane's source; flags bit 0 TUNE counts the model's steps, not semitones; bit 1 a user sample (HIT START LEN apply); bit 2 the sound has no per-sound DIST / COMP (hide those knobs; `FX` op 3 ignores it). Ask again after a source or the kit changed.
- **PAGES** is `params.c` `PAGES` in order: family (1 ENV, 2 LFO, 3 FX, 4 SCL, 5 EDIT, 6 GLO, 7 SAVE, 8 ARP, 9 SEQ, 10 TRK), scope (0 a track's `P_*`, 1 a global, 2 the engine's `P_E*`; others are the device's own screens), shown (1: there for the selected track), four ids (127 empty), title. Engine page titles come from `NAMES`. Read again after an engine change or another track was selected.
- **Audition:** play a lane with a MIDI note (`9n` note 100, then `8n`) on the drum channel (GLO > DRUMS CH) at the lane's GM note, or use `NOTE` (103). Notes travel between SysEx frames and get no reply.

## Backup and restore (43..48)

`ed_backup.c` (needs flash and `BACKUP`). Everything stored is an **object**: tag (4 ASCII), kind, and the build switch it needs. Kinds: 0 a storage object (A/B sector pair, <= 3,840 bytes); 1 a user sample slot raw;
2 the FM6 user bank (listed, never in a build now); 3 a song section's record (`sec_codec.c`, compressed); 4 written only, an older backup's project slot imported as that section; 5 another record of the section log, raw; 6 the snapshot area.
A restore writes in the list's order (the drum records before the projects, the settings before the song chain). `BK_OBJS` in `ed_backup.c` is the list; today (a build lists only what it has):

| Tag | What | Kind |
| --- | --- | --- |
| SETT | settings, the learned panel, the song's first 16 parts (PATTERNS: the open project too) | 0 |
| DLNS | the projects' drum records | 0 |
| PJ01..PJ16 | PATTERNS (docs/PROJECTS-DESIGN.md): each project's records as one stream, in id order: u8 log id, u16 length, the record as the log holds it (its state, scenes, patterns, song chain, BARS, sources, extras / FX / LFO records; never a device-wide id). Read in chunks (the log's index lent to that project for the command, the open project's back after it); restored record by record as the chunks arrive, each checked as a load checks it (one the firmware would not load is skipped). Up to the whole log (128 KB) | 7 |
| PNAM | PATTERNS: the project names (u8 id 21, u16 length, the 16 x 12 bytes) | 5 |
| PTN1..PTN6 | the per-track patterns and their state (u8 log id, u16 length, the record; as many as fit each). PATTERNS: listed without data, restored into the open project (an older backup's) | 5 |
| S01..S16 | song sections A-P (`FELUCCA_SECTIONS` 8 or 16; S09..S16 listed as not in a build with 8). PATTERNS: without data, restored into the open project | 3 |
| PRJ1..PRJ4 | an older backup's project slots, written into A-D, never read | 4 (0 with 4 sections) |
| SNG1 | the whole song chain, up to 64 parts (count, loop, 2 spare, then section and bars of each part). PATTERNS: without data (in PJnn), restored into the open project | 5 |
| XSTP | SLOOP 2.4's step extras of the sections and the autosave, one raw object (automation store builds) | 5 |
| FXSL, FXR1, FXR2 | older backups' FX slot records: restored, never read now | 5 |
| FX31..FX33 | the FX slot records (`fx_rec.c`) in three packs, sections A..F, G..L, M..P and the autosave: per record u8 id, u16 length, the log record; each written back whole at the commit, paired by its key with S01.. and AUTO | 5 |
| DLFO, DLF1, DLF2 | the drum lanes' LFO records (`dlfo_rec.c`): DLFO an older backup's (restored only); DLF1 sections A..I, DLF2 J..P and the autosave, u8 id, u16 length, the log record. A firmware before them skips them and the sections load with every lane LFO off | 5 |
| AUTO | the working project (autosave) | 0 |
| UPR1, UPR2 | user presets 1-16, 17-32 ("UPB3"; "UPB2" / "UPB1" are converted on restore) | 0 |
| UKIT | the user drum kit bank ("DKB4"; `DRUM_KITS`) | 0 |
| UPF6 | the FM6 voices of the user presets (`FELUCCA_UP_FM6`) | 0 |
| FM6B | the FM6 user bank: the tag stays so an older backup reads the same; never in a build now, a `BK_BEGIN` of it says rc 5 | 2 |
| CZBK | the CZ collection U01-U26 | 0 |
| USR1..USR3 | the sample slots | 1 |
| SNAP | the snapshot area raw (restored through the snapshot import 54..57) | 6 |

| cmd | Request | Reply |
| --- | --- | --- |
| 43 BK_LIST | version (4; 3 and 2 still answered), first | version (3 from an older firmware), n, switches (2 x 7 bit: bit 0 flash, 1 ANALOG 2, 2 DRUM_EDIT, 3 DRUM_USR, 4 DRUM_KITS, 5 always 1, 6 song, 7 USB audio), the project format's magic (4 ASCII, "FUNB"), the chunk size (2 x 7 bit: 256), first, count, then per object: tag, kind, flags (bit 0 in this build, 1 has data, 2 written with 45..47, 3 a slot holding the FM6 bank), length (3 x 7 bit), CRC-32 (5 x 7 bit); the page that ends the list (first + count = n) ends with what the build holds |
| 44 BK_READ | i, offset (3 x 7 bit) | i, offset, CRC-32 of the chunk (5 x 7 bit), pack7 bytes (<= 256) |
| 45 BK_BEGIN | i, length (3 x 7 bit), CRC-32 (5 x 7 bit) | i, rc |
| 46 BK_DATA | i, offset, CRC-32 of the chunk, pack7 bytes (<= 256, in order) | i, offset, rc |
| 47 BK_COMMIT | i [, 1 = check only] | i, rc |
| 48 BK_END | 1 done / 0 abort | rc (0); done: the device restarts about 150 ms later |

rc: 0 ok, 1 arguments (unknown object, out of order), 2 CRC (a chunk: send it again; the whole object at COMMIT), 3 the transport plays, 4 no flash, 5 not in this build, 6 longer than a storage object, 7 the flash write failed (the old copy stays),
8 at COMMIT: not an object the firmware would load (nothing written).

- **Pages:** `BK_LIST` v3+ sends as many objects as one reply holds (`BK_PAGE`); ask again with first = the objects held until you have all n (`bkListAll`). A v1 / v2 request gets the v2 reply (the whole list) only when it fits one reply, else none.
- **Writing:** the first `BEGIN` opens a session: the device saves what is only in RAM, then holds the object in RAM. Nothing reaches flash before `COMMIT`, which checks length and CRC and writes the other copy, header last, read back. A session left for 10 s ends by itself.
- **Check before writing (v4):** `BK_COMMIT i 1` runs the checks and writes nothing. A restore sends every storage object (kind 0) through `BEGIN` / `DATA` / `COMMIT i 1` first and writes only when none is refused (`bkCheckAll`; skipped against a v3 firmware).
- **Sample slots** are read with 44 and written with 11..13. **Done:** `BK_END 1` drops the RAM copies of the projects and the device restarts, loading every object as at power-on.

**The backup file** (`.optimist-backup`): `OPTBKUP` 0x01 (8 bytes), the header length (u32 LE), the header (JSON `{"format": "optimist-backup", "version": 1, "created", "device": {version, proto, switches, magic, bk}, "objects": [{tag, kind, fm6, len, crc, at}]}`),
the objects' bytes (at `at` from the end of the header), then the CRC-32 of everything before it (u32 LE). Each object is the device's own bytes. The editor refuses a damaged file and leaves out objects the connected device does not have (shown, unticked).
A section's file is a backup file holding its one `Snn ` object (written with `BK_BEGIN` / `DATA` / `COMMIT` and `BK_END 1`); its FX slots, lane LFOs and motion are other objects and do not come with it.

## Snapshots (54..57)

`ed_snap.c` (`SNAPSHOTS`; docs/SNAPSHOTS.md). A snapshot is the whole state: the working project, every section, the song. Slots 0..7 (a build shows 2, 4 or 8) and slot 8, BEFORE LOAD (the state before the last load); the numbers are the same in every build.

| cmd | Request | Reply |
| --- | --- | --- |
| 54 SN_LIST | 1 | 1, slots shown, slots in the format (9), area sectors, free sectors, payload a sector (2 x 7 bit: 4,064), the longest stream (3 x 7 bit), the work fits now (0/1), then per slot 0..8: state (0 empty, 1 saved, 2 damaged), shown (0/1), size (3 x 7 bit), name, BPM (2 x 7 bit), sections (3 x 7 bit), song parts, the saving build's sections, another build (0/1), the work has motion (0/1) |
| 55 SN_OP | op, slot [, name] | op, slot, rc, notes. op 0 save the state now (name "" = made from the work, "120 ABCD"), 1 load (the state goes to BEFORE LOAD first), 2 clear, 3 rename. notes (a load): bit 0 sections past this build's, bit 1 the song was not loaded, bit 2 a section the store did not take. Writes flash: allow ~5 s |
| 56 SN_READ | slot, offset (3 x 7 bit) | slot, offset, CRC-32 of the chunk (5 x 7 bit), pack7 bytes (<= 256; none at its end) |
| 57 SN_WRITE | 0, slot, length (3 x 7 bit), CRC-32 of the stream (5 x 7 bit) | 0, slot, rc (begin) |
| | 1, slot, offset, CRC-32 of the chunk, pack7 bytes (<= 256, in order) | 1, slot, offset, rc |
| | 2, slot | 2, slot, rc (commit: CRC, parse, then write) |
| | 3, slot | 3, slot, 0 (abort) |

rc: 0 ok, 1 arguments, 2 no room, 3 the transport plays, 4 no flash, 5 the flash write failed (the old version stays), 6 the slot is damaged, 7 busy, 8 empty, 9 still sounding (wait for silence), 10 not a snapshot this firmware loads, 11 a long USR3 sample is in the snapshot area,
12 CRC, 13 out of order or no import open. An import is ended by a save, load or clear on the device and by 10 s without a chunk.

**The stream** (version 1): a 48-byte info: `"SNAP"` (u32 LE 0x50414E53), u16 version, u16 info length (48), name (12, 0-padded), u32 the saving build's project magic ("FUNB"), u32 its config hash, u8 its sections, u8 record count, u16 BPM, u32 sections stored (bit mask),
u8 song parts, u8 flags (bit 0 motion), u16 0, u32 save counter, u32 0. Then records: u8 kind, u8 id, u16 LE length, bytes. Kind 1 WORK (the working project as a section record), 2 SEC (id 0..15), 3 SONG (count, loop, 2 spare, (section, bars) per part), 4 TAIL (id 0x80 the work's: what a section record leaves out, so the project comes back byte for byte). A reader skips kinds it does not know.
**The file** (`.optimist-snap`): `OPTSNAP` 0x01 (8 bytes), u32 LE the stream's length, the stream, u32 LE CRC-32 of everything before it.
**In a backup** the area is object `SNAP` (kind 6): each sector holds a 32-byte header (`"SNS1"`, slot, part, parts, version 1, seq, this part's length and CRC-32, the stream's length and CRC-32, CRC-32 of the 28 bytes before) and up to 4,064 payload bytes; per slot the valid part 0 with the highest seq wins. A restore imports each slot's stream with `SN_WRITE`.

## Patterns and scenes (79..84)

`ed_pat.c` (`PATTERNS`; INFO tag 55; docs/PATTERNS-DESIGN.md). Each track has 16 pattern slots; a scene names one pattern a track. A slot value is 7 bits: 0..15 a slot, 127 none, 126 keep, 125 stop (a launch) / a plain section (in a scene list).
The editor shows the slots in its mixer strips and the scenes on the MASTER strip, polled with `PAT_LIST` op 0 while the mixer shows. 85 is unused.

| cmd | Request | Reply |
| --- | --- | --- |
| 79 PAT_LIST | op (0 all, 1 the tracks only) | op, changed (bit k: track k's working copy differs from its source), per track: the slot it plays, the launch waiting, when (0 pattern end, 1 next bar, 2 now); op 0 then 4 x 16 LEN (0 empty slot), 16 scenes x 4 slots (127 x 4 no scene; 125 x 4 a plain section) |
| 80 PAT_LAUNCH | track, slot (127 stop), when | track, slot, rc (0 ok, 1 arguments). Stopped: loaded at once |
| 81 SCENE | op (0 launch, 1 store what the tracks play), scene | op, scene, rc (0 ok, 1 arguments or empty, 2 not stored: MEM FULL) |
| 82 PAT_OP | op (0 store the working copy into a, 1 copy a to track2's b, 2 clear a, 3 duplicate into the first free slot), track, a, track2, b | op, rc (0 ok, 1 arguments, 2 not done: MEM FULL, a clear while playing) |
| 83 PAT_READ | track, slot, offset (2 x 7 bit) | track, slot, offset, total (2 x 7 bit: the record's bytes, 0 = empty), pack7 bytes from offset (<= 256) |
| 84 PAT_WRITE | track, slot, offset, total, pack7 bytes (<= 256) | track, slot, offset, rc |

`PAT_READ` / `PAT_WRITE` edit a pattern that does not play. The record is the section log's (`storage/sections/pat.c`; the page's `patRecParse` / `patRecBuild`): a flags byte (1 a motion chunk follows the header, 2 codec B steps, 4 the drum track, 8 the motion plays, 16 the step extras close the record,
the version in bits 5..7: 0x20 V1, 0x40 V2), LEN DIV SWING GATE, the motion chunk (count, count x 3 bytes), the step bitmap (LEN bits) and the steps it marks, the extras. A V2 record's chunk is the automation store's list (up to 128 events, the place with STEP-ONLY) and has no extras.
A write sends the chunks in order from offset 0 with the same `total`; the chunk that completes it checks and stores the record (stopped: in the log; playing: in the pending arena); total 0 clears the slot (refused while playing). rc: 0 ok, 1 not in order or bad arguments,
2 not a pattern of this track, 3 busy (a restore or another write holds the buffer: ask again), 4 not written (MEM FULL, a clear while playing). A session left for 10 s is given back.

## The automation store (92, 93)

`ed_stepx.c` (`FELUCCA_AUTO`, `seq/auto.h`). One list of up to 128 events a track (its working pattern), each 3 bytes: **place** (bits 0..5 the step, bit 6 STEP-ONLY: a lock, it holds its step; clear: a hold event = motion, held until the next event of its parameter or the pattern's restart),
**param** (a stored `P_*` id, or a pseudo-parameter: 252 COND 1..9 = 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4, 253 NUDGE -32..31 in 1/64 step, 254 FILL 1 fill only / 2 no fill, 255 CHANCE 0..99 %; pseudo-parameters are always STEP-ONLY), **value** (a signed byte).

| cmd | Request | Reply |
| --- | --- | --- |
| 92 AUTO_GET | track [, first] | track, n (2 x 7 bit), PLAY (the track's hold events play: 0/1), first, count, then count x (place, param (2 x 7 bit), v14); at most 64 a reply: ask again from first + count |
| 93 AUTO_SET | track, op, ... | track, op, rc (0 done, 1 refused: an event this track does not take, 2 the list is full), n (2 x 7 bit); op 0 also the value kept (v14) |

Ops: **0 SET** place, param (2 x 7 bit), v14 (clamped to the parameter's range; a hold event switches its track's PLAY on; a pseudo-parameter at its default 0 / 0 / 100 removes the event); **1 DEL** place, param; **2 CLEAR** which (1 the hold events, 2 the step-only ones, 3 all); **3 PLAY** on (0/1).
A track takes a step-only event of a lockable value (`PLOCK`), a hold event of a value motion records (`MOTION`), and the pseudo-parameters.

**Drum lane values** (drum track only): param **0x7C + (lane << 3 | value)**, 0x7C..0xFB, is lane 0..9's value 0..7 (the id space has room for 16) (TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL, ranges as the lane's SOUND page); a step-only event is the lane's lock on that step, a hold event the lane's motion. A synth part refuses them (rc 1).
The stored track ids end below 0x77: 0x77 is the ACCENT lane's stored events and 0x78..0x7B the notes' own ends; both exist only in stored forms (sections, patterns, snapshots, backups), `AUTO_GET` never lists them and `AUTO_SET` refuses them (rc 1).
The page's `autoLane(param)` reads an id. Every write is an undo level (`ED_LIVE`).

## FX (86, 87, 94; INFO tags 56, 57)

`ed_fxs.c`, `ed_fxp.c`, `fx/fx_slots.c` (`ED_MIX`). Four master **send slots** (CHO DLY REV types; default ---- REV DLY CHO), two **insert slots** S1, S2 on each track (T1 T2 T3 DR), the master and the USB input and on each of the 16 drum sounds, and three **returns** A B C
(the device's R1 R2 R3; a send bus or any effect type, 100 % wet, with a RET level). A type sits in one slot of an owner at most (loading a type another slot holds swaps the two). The amounts belong to the type, not the slot.

**Tag 56:** `len 01 n`, then n x 4 bytes, one per type built: **type id** (never reused: 1 DIST, 2 CHO, 3 DLY, 4 REV, 5 COMP (named DYN), 6 FILT, 7 FOLD, 8 FUZZ, 9 PHSR, 10 FLNG, 11 FRMT, 12 WIDE, 13 CHRS, 14 ECHO, 15 GATER, 16 DUST, 17 JUNO, 18 EQ3,
19 SFLD, 20 WRAP (reserved, never built), 21 RECT, 22 HARM, 23 RING, 24 SUBO, 25 SLEW, 26 TAPE, 27 SPIT; 0 an empty slot; `fx_slots.c` `FXT_*`, the page's `FXT_NAMES`), **kind** (0 a send bus, 1 an insert), the **amount id** (a `P_*`; its `DESC` label names the slot), the **sound id**
(SOUND 3's value id of a drum sound's amount: 16 REV, 17 DLY, 18 CHO, 19 DST, 20 CMP; 127 none).

| cmd | Request | Reply |
| --- | --- | --- |
| 86 FX | op [args]: 0 get; 1 the four master slots (type ids, sends only); 2 every track's COMP RATIO ATK REL; 3 a drum sound's insert: lane 0..9, kind (0 DIST, 1 COMP), amount 0..127; 4 a track's insert slots: track, S1, S2 (the same type twice: S2 empty); 5 a track's COMP: track, RATIO ATK REL; 6 the drum send levels REV DLY CHO (0..127) | op, then the state: 4 master slots, RATIO ATK REL of T1 (127 127 127: no COMP), 16 x the drum sounds' DIST, 16 x their COMP, 8 type ids (T1 S1 S2, T2 .., DR), 12 x RATIO ATK REL (T1 .. DR), 3 x the drum send levels. A bad op, lane, kind or track: no reply |
| 87 FX_PUSH | (push, `WATCH`) | as the `FX` reply, op 0 |

**FXP (94)** reaches every slot by **owner** and describes each slot and parameter itself (the device's own descriptors), so an editor names no type. Tag 57: `03 01 flags owners` (flags bit 0 the USB input is built; owners = 19).
Owners: 0..3 tracks T1 T2 T3 DR, 4 the master, 5 the USB input (slots 0 1, and slot 2: its own VOLUME PAN MUTE FX ON REV DLY CHO), 6..8 the returns A B C, 9..18 the drum sounds (lanes 0..9; slots 0 1: only that slot's amount).
A return's slot 0 is its type; slot 1 holds its **sends to the returns to its right** (A: B C, B: C; 0..127, listed whatever the returns hold; type 0 always; op 1 on it loads nothing; no feedback). With the item `RET_FB` each return also has its two others.

| op | Request args | Reply args |
| --- | --- | --- |
| 0 GET | owner | 0, owner, slots n, per slot: type, parameter count; then the types the owner may hold (5 x 7 bits, bit t = type id t); a return then its send line (0 CHO, 1 DLY, 2 REV: the line a track's `P_CHOR` `P_DLY` `P_REV` feeds; 127 none) |
| 1 TYPE | owner, slot, type | 1, then as op 0. A type the other slot holds swaps them (the returns swap too): re-read the returns after any return changed. A type the owner may not hold: unchanged |
| 2 DESC | owner, slot, i | 2, owner, slot, i, the `DESC` body; nothing after i: no such parameter |
| 3 VALS | owner, slot | 3, owner, slot, n, n x v14 |
| 4 SET | owner, slot, i, v14 | 4, owner, slot, i, v14 (clamped) |

A bad owner, slot or op: no reply. A return's first parameter is its RET level (the `CRET DRET RRET` global, 127 unity). A track's DYN THRS is `P_TCOMP` (parameters of `P_*` / `G_*` also arrive in `PARAMS`); the master's DYN is the `COMP` and `LIMIT` globals. The FX push (87) goes out when any slot setting moved on the device
that no other push covers. The editor's own writes are not echoed. The FX record (`fx_rec.c`, TLV ids) is the storage form; read it in the code. `tests/fxp_table.json` is the golden table of every owner kind, type and parameter; `tools/gen_fxp_table.py` copies it into `web/editor.html` (`FXP_TABLE`, the mock).

## Drum lanes' mix (95 DLAN; INFO tag 58)

`ed_dlan.c` (`ED_MIX`). Described like FXP (the device's own descriptors). Ops: 0 GET group, lane -> 0, group, lane, n; 2 DESC group, lane, i -> 2, group, lane, i, the `DESC` body (nothing after i: no such parameter); 3 VALS group, lane -> 3, group, lane, n, n x v14;
4 SET group, lane, i, v14 -> 4, group, lane, i, v14 (clamped). A bad group, lane or op: no reply. Groups: 0 a lane's mix (lane 0..9): PAN (-64..63) MUTE SOLO SEQ MUTE; lane 16 (the ACCENT lane): SEQ MUTE, ACCENT. 1 a lane's LFO (lane 0..9):
RATE WAVE PHS FADE DEST AMT DEST2 AMT2 (DEST: OFF, then TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL). 2 a track's sequencer mute (lane = track 0..3): SEQ MUTE. Changes on the device go out in the FX push (87); re-read the state then. Golden table: the `dlane` object of `tests/fxp_table.json`.

## GLOBAL and SYSTEM (96 SCRN; INFO tag 59)

`ed_scrn.c` (`ED_SCREENS`). The settings of the FM-1 (BPM, SWING, REC modes, MIDI, channels, screen, lights, audio, USB, Bluetooth, USB AUDIO, calibrate, about, read-outs) are cells (`op_cells.c` `cell_t`), not descriptor parameters. SCRN hands the cells out as the device draws them and turns / does them
through the screen's own functions, so an editor shows the same lines, words and units.

| op | Request args | Reply args |
| --- | --- | --- |
| 0 INFO | screen (0 GLOBAL, 1 SYSTEM) | 0, screen, lines n, pages np |
| 1 LINE | screen, line | 1, screen, line, LINE |
| 2 TURN | screen, line, mode (0 steps as a knob with acceleration, 1 fine steps, 2 the default), v14 steps | 2, screen, line, LINE (after) |
| 3 YES | screen, line, ok (0 may ask a question, 1 does it) | 3, screen, line, rc (1 handled), LINE |
| 4 SAVE | - | 4: the settings into flash (what SYSTEM does when left; once stopped) |

LINE: page, page title, section (the row's name: TEMPO REC MIDI CHANNELS INFO SCREEN AUDIO KEYS USB USB AUDIO CALIBRATE ABOUT), kind (0 none, 1 value, 2 read-out, 3 action, 4 enter), label, value text, unit, state (0 plain, 1 warn, 2 error, 3 ok, 4 other colour), danger,
ask (1: the device asks for a YES now), gauge form (1 bar, 2 bipolar bar, 3 few entries, 4 many entries, 5 two entries), gmin v14, gmax v14, gv v14, by-descriptor (0/1: then fmt, min v14, max v14, def v14), the action's object text. A bad screen or line, or the BLE devices list open on the device: no reply.
Help lines: `tools/param_help.json`'s `op` group. Golden: `tests/scrn_table.json`; `tools/gen_fxp_table.py` copies it into the mock.

## The device's screens (99 SCRS; INFO tag 5C)

`ed_scrs.c` (`ED_SCREENS`). The other screens drawn as **rows of four cards** (`ui/optimist/op_screens.c` `screen_t`): SOUND, MASTER, PROJECT, STEPS, SONG, TEMPO, SCOPE and MIX. Tag 5C: `03 01 mask`, bit n = the screen with the device's number n: 0 MIX, 1 SOUND, 2 MASTER, 3 PROJECT, 5 STEPS, 6 SONG, 7 TEMPO, 8 SCOPE
(4 / 9 are GLOBAL / SYSTEM: SCRN's). MIX has a row for each knob set of each mixer row ("T1 MIX", "M RETURNS", "SD SENDS"). Every request starts `op scr trk lane fam`:

| op | Request args (after `scr trk lane fam`) | Reply args |
| --- | --- | --- |
| 0 INFO | - | 0, scr, trk, lane, fam, rows n, the screen's name |
| 1 ROW | row | 1, scr, row, ROW |
| 2 TURN | row, k, mode, v14 steps | 2, scr, row, ROW (after) |
| 3 YES | row, k, ok | 3, scr, row, rc (1 handled), ROW |

`trk` 0..3 the track (SOUND, STEPS), `lane` 0..16 a drum lane picked (127 none), `fam` the page family of SOUND (127 every row; 126 an FM6 track's operator rows) or, for STEPS, the step held 0..63 (127 none: PATTERN, TOOLS, EUCLID; a step past the pattern's length: no reply; a turn edits the step).
The context is set for the one call and put back: the device's own cursor does not move. `mode` as SCRN. YES with `ok` 0 may only ask (the card's `ask` is 1 and the row carries the question); `ok` 1 does it (the question times out after 3 s as on the panel). A bad screen, track or row: no reply.
ROW: family (`FAM_*`, 127 none), flags (bit 0: YES on any card runs the row's operation on its item), name, four CELLs, the armed question ("" none). CELL: kind (0 none, 1 value, 2 read-out, 3 action, 4 enter), label, value text, unit, state, danger, gauge form, gmin v14, gmax v14, gv v14,
by-descriptor 0/1 (then fmt, min, max, def, value, v14 each), flags (bit 0 dim, bit 1 the drum track's own value shown on a lane, bit 2 struck through, bits 3..4 the step event mark, bit 5 the card asks), the action's object text. Golden: `tests/scrs_table.json` (`EDSCRN_WRITE=1` rewrites it).

## The song chain (97 SONGC; INFO tag 5A)

`ed_song.c` (`ED_SEQ`, the arranger). The order scenes and patterns play in: PART 1..64 (a scene and bars) and the mode, with the SONG screen's rules (edited while stopped, INSERT copies the part after it, DELETE leaves one). Every op answers op 0's reply:
`0, rc (0 done, 1 stop first or SONG REC is on, 2 chain full, 3 one part left, 4 a bad argument), mode (1 SONG, 0 LOOP), playing, scenes built, steps (the chain's longest), count, count x (scene, bars)`.
Ops: 0 get; 1 part, scene, bars (1..64); 2 part (INSERT); 3 part (DELETE); 4 mode (0 LOOP, 1 SONG); 5 SAVE (the chain into flash).

## Sequence tools (98 TOOLS; INFO tag 5B)

`ed_tools.c` (`ED_SEQ`, with `SEQTOOLS`, `NOTEGEN` or `GEN_GRIDS`: tag 5B says which; an op of a part not built, or a type not built, has no reply, and without the generators the state's generator fields are all 0). STEP's TOOLS, GEN and GEN OUT pages on the selected track (`TRACK` 27 selects it); a drum track's: the whole kit, or one lane. They run through the device's own `tl_run`, one undo level each, and the editor gets a `RELOAD`.
Every reply: `op, rc (0 done, 1 another generator is on, 2 the generator is on: keep or restore first), track, lane, len, hits, steps, rot, on`, two strings (what the device says, e.g. `T1 REVERSED`), then (version 2) the slot's generator type, mode, on.

| op | Request args |
| --- | --- |
| 0 | - (the state) |
| 1 | verb (0 REVERSE, 1 RANDOM, 2 COOK, 3 LENGTH, 4 QUANTIZE), lane (0..9 or 127: the whole kit / a synth track), half (LENGTH: 1 = 1/2, 0 = x2; QUANTIZE: the grid 1, 2 or 4 a step) |
| 2 | k (0 HITS, 1 STEPS, 2 ROT), value: EUCLID's value, written at once while it is on |
| 3 | lane (127: as it is): EUCLID ON, the generator writes the pattern |
| 4 | EUCLID OFF, the generated steps kept |
| 5 | EUCLID OFF, the pattern as it was given back |
| 6 | type (0 EUCLID, 1 RANDOM, 2 PULSE, 3 GRIDS; one the build has not: no reply): the slot's generator; another type has its values at their defaults |
| 7 | k (0..2), value: the generator's value k (EUCLID HITS STEPS ROT, RANDOM DENSITY SEED ACCENT, PULSE EVERY OFFSET RATCHET), fitted to its range |
| 8 | mode (0 REPLACE, 1 ADD, 2 MASK) |
| 9 | on (0/1), lane (127: as it is): the generator ON / OFF, the pattern untouched (OFF with what plays written: op 4) |

## Step lanes (105 STEPLANE; INFO tag 62)

`ed_steplane.c` (`ED_SEQ`, needs the automation store; locks need `PLOCK`). A step seen in the context of a **track and a drum lane**, as the device's held step is: the lane's level and ratchet, the step's extras (chance, condition, nudge, fill) and the device's **own list of
lockable values** for that track and lane, with the locks and hold events on the step. Writes are undo levels. Tag 62: `03 01 a b`; `a`: bit 0 the list is the device's lock pages, bit 1 locks built, bit 2 a drum lane's values are lockable, bit 3 hold events (motion) built;
`b`: bit 0 chance, bit 1 condition, bit 2 nudge, bit 3 fill built (a field of a feature not built reads its default and a SET answers rc 1).
Every request starts `op, track, lane`; `lane` 0..9 a drum lane, 16 the ACCENT lane, 127 none (a synth track must send 127). A bad op, track or lane: no reply.

| op | Request args (after `op track lane`) | Reply args |
| --- | --- | --- |
| 0 LIST | - | 0, track, lane, n (lockable values, <= 127), np (lock pages) |
| 1 DESC | i | 1, track, lane, i, kind, id, page, cell, title (12), the `DESC` body; nothing after i: no such value |
| 2 STEP | step (0..63) | the state (op 2) |
| 3 SET | step, field, v14 | the state (after) |
| 4 LOCK | step, i, v14 | the state (after) |
| 5 UNLOCK | step, i | the state (after) |
| 6 KIND | step, i | the state (after) |

**The list:** the values the device lets a step lock in this context, in the device's order, none twice. `kind` 0 = a track parameter (`id` = its `P_*` id, the `param` of `AUTO_SET`), 1 = a drum lane's value (`id` 0..7: TUNE .. LEVEL; stored as `AUTO_LANE(lane, id)` = 0x7C + lane x 8 + id);
`page` the lock page's ordinal, `title` its name ("ENV", "LFO", "BODY"), `cell` the card on it (0..3). On the drum track with a lane picked the list is the lane's values, with lane 127 the drum track's own, with lane 16 the ACCENT amount alone. Ask LIST again after an engine or kit change.
**The state:** `op, rc, track, lane, step, flags, level, ratchet, chance, cond, nudge+64, fill, n, n x (i, kind, v14)`. rc: 0 done, 1 refused (a bad field, value or index, or the feature is not built), 2 the event list is full, 3 no hit (the lane has no hit on that step: nothing written), 4 nothing to do (KIND).
`flags`: bit 0 the step has a hit in this context, bit 1 locks can be written here now, bit 2 the step is past the pattern's LEN. `level` / `ratchet` are the raw 2-bit codes of `DRUM_STEP` (a synth step: those of its first note; SET writes every note). `cond` 0 none, 1..9 as the automation store; `nudge+64` -32..31; `fill` 0 normal, 1 fill only, 2 no fill.
The events are those in this context's list: `i` (the DESC index), `kind` 1 a lock, 2 a hold event (motion), `v14` the value. **SET** fields: 0 level, 1 ratchet (need the hit, else rc 3), 2 chance (0..100), 3 cond (0..9), 4 nudge (-32..31), 5 fill (0..2). **LOCK** writes the lock of value `i` at `v14` (a hold event of that value already there takes the value
instead). **UNLOCK** drops both kinds. **KIND** turns a lock into a hold event or back. `AUTO_SET` on the same step is the same event. Golden: `tests/steplane_table.json` (`STEPLANE_WRITE=1`; the mock does not answer the command).

## Favorites (100 FAVS; INFO tag 5D)

`ed_fav.c` (`ED_LIVE` and `FAVORITES`). The device marks sounds as favorites; PRESETS can browse the marked ones. A factory sound is marked by its engine's UID and name (up to 64), a user preset by its slot. A bad op, engine or k: no reply.

| op | Request args | Reply args |
| --- | --- | --- |
| 0 | - | 0, factory marks, FAV_N, the PRESETS list (0 ALL, 1 FAV), user presets marked |
| 1 | engine | 1, engine, n, the marks of its n presets, 7 to a byte (engine = NENGINES: the user presets) |
| 2 | engine, k, on | 2, engine, k, rc (0 as it was, 1 changed, 2 FULL), factory marks |
| 3 | list | 3, list: the PRESETS list the device browses |

## Scope (101 SCOPE; INFO tag 5E)

`ed_scope.c` (`ED_SCREENS`). Request: src (0 MASTER, 1..4 T1 T2 T3 DR). Reply: src, shift (the right shift that fits the peak; never amplified), peak v14 (the ring's largest sample >> 2, at most 8191), n (2 x 7 bits), n heights (0..127, 64 the zero line). The trace is trigger-stable.
Asking sets the ring's source for the next 2 s: ask again about every 80 ms to watch. A bad src: no reply.

## The screen mirrored (106 MIRROR; INFO tag 65)

`ed_mirror.c`, `ed_mirror_enc.h` (`ED_MIRROR`, with live editing or without; the clients are the iPhone app `ios/OptimistMirror` and the editor's Screen tab). While a client asks, every write to the panel
(`display/lcd.c` `lcd_fill`, `lcd_blit`) also goes out as it is drawn; a client applying them in order holds the panel's pixels. The device keeps no frame buffer and sends nothing while no client asks; the mixer's meters go 10 times a second over USB, never over Bluetooth.

Request: bits (bit 0 on, bit 1 the whole panel again). Reply: on, version 1, width (2 x 7 bit), height (2 x 7 bit). The pushes go on the link the last request came on:
USB, or Bluetooth MIDI (with `BLE`; there the FM-1 answers this request only, every other SysEx is ignored).
Mirroring ends 3 s after the last request of any kind (ask again or `PING` about every 1 s), with `106 0`, and when its link goes (a USB reset; no Bluetooth central listening).

**Catching up.** A first on, bit 1, and a link that fell behind (USB: the host read nothing for 30 ms; Bluetooth: the 8 KB send ring was full, the frame dropped) all end the same way once
the link is clear: a `FILL` of the whole panel in black, then every band of the panel drawn again (the device's own panel is not cleared). Until then nothing is sent. At most one catch-up every 250 ms,
and none while PRESETS' popup list is up (the panel under it is not drawn again). A catch-up is 4.7 KB on average, 8.2 KB at most (the UI's host run). Over Bluetooth only whole frames are sent.

Pushes, cmd 106, by op:

| op | Bytes |
| --- | --- |
| 0 FILL | x, y, w, h (2 x 7 bit each), colour (RGB565, 3 x 7 bit) |
| 1 BLIT | x, y, w, h, then runs: the rectangle's pixels row by row |
| 2 MORE | runs: the BLIT before goes on (a BLIT is cut into frames of about 250 bytes) |
| 3 LINK | Bluetooth only, after the reply: the radio's data length (octets a packet), ATT MTU, interval (x 1.25 ms), the central's LL_LENGTH MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime (us), 2 x 7 bit each |

A run: a token `t = n << 3 | s`. `n` 0..14: n + 1 pixels; `n` 15: 16 + (2 x 7 bit after it) pixels. `s` 0..6: the colour in that slot; `s` 7: a colour follows (3 x 7 bit) and
takes slot k, k going 1, 2 .. 6, 1 ... The slots are reset at each BLIT: all black, slot 0 stays black. Reference decoder: `tests/mirror_dec.h`; the UI's whole host run is checked through it (`MIRROR_CHECK`).

## Undo (102 UNDO; INFO tag 5F)

`ed_undo.c` (`ED_LIVE`). The device's undo and redo of the patterns (steps, LEN, DIV, the automation events). The editor's own step writes (`STEP_SET`, `TRACK_STEP`, `DRUM_STEP`, `AUTO_SET`, `STEPLANE`) are undo levels too; writes to the same track within 800 ms are one level.
Ops: 0 GET, 1 UNDO, 2 REDO. Every reply: `op, rc (0 done, 1 nothing to undo / redo), done lo, done hi, all lo, all hi, track, can_undo, can_redo` (counts as 2 x 7 bits; `track` 0..3 of the last undo or redo). After a change `RELOAD` is set and the `STEPS` / `PARAMS` pushes follow:
re-read the pattern of `track`. A bad op: no reply.

## Notes from the editor (103 NOTE; INFO tag 60)

`ed_note.c`, `seq/ednote.c` (`ED_LIVE`). A note on or off for one track through the device's own note input (arp, chord layout, voice budget, REC capture and MIDI OUT apply as for the panel's keys).

| op | Request args | Reply args |
| --- | --- | --- |
| 0 ON | track, note, vel (1..127) [, dur] | 0, rc, track, note, held |
| 1 OFF | track, note | 1, rc, track, note, held |
| 2 ALL | track (127 every track) | 2, rc, track, 127, held |

`track` 0..2 a synth part (`note` a MIDI note 0..127, through the track's layout), 3 the drum track (`note` = the lane 0..9, 16 = ACCENT which only records; `vel` becomes the lane's level; OFF and dur do nothing). `dur` (1..127) ends the note after dur x 100 ms. At most 8 notes are held (an ON past 8 ends the oldest).
rc 0 done, 1 the ring to the audio interrupt was full (send again). The device ends every held note when dur runs out, after 3 s of host silence (any request counts: ping or repeat a held key at least every 2 s), on a USB reset, when the track changes its sound, and on OFF / ALL. A bad op, track, note or vel: no reply.

## Solo and mute (104 SOLO; INFO tag 61)

`ed_solo.c` (`ED_MIX`). The device's own grammar. One solo set across the four tracks, the 16 drum lanes and the USB input: while anything is soloed, every unsoloed item of those groups is silent; the returns and the master never are. A track's SOLO is a bit of `song.solo` (GLO + key 5..8), its MUTE is `P_MUTE`; a lane's are
`drum_mix.c`'s masks (the same values `DLAN` group 0 edits); the USB input's MUTE is the USB owner's MUTE value of `FXP`. Solo is not saved with a project except the lanes' mask.

| op | Request args | Reply args |
| --- | --- | --- |
| 0 GET | - | the state |
| 1 TRACK SOLO | track, on | the state (after) |
| 2 TRACK MUTE | track, on | the state (after) |
| 3 LANE SOLO | lane (0..9), on | the state (after) |
| 4 LANE MUTE | lane (0..9), on | the state (after) |
| 5 USB SOLO | 0, on | the state (after) |
| 7 RET MUTE | ret (0..2 = A B C), on | the state (after) |

`on`: 0 off, 1 on, 2 toggle. State: `op, rc, solo, mute, silent, lsolo0, lsolo1, lsolo2, lmute0, lmute1, lmute2, usb, ret`: the track masks (bit n = track n); `silent` = the tracks not heard now; the lane masks as 3 x 7 bits (lanes 0..6, 7..13, 14..15); `usb` bit 0 soloed, bit 1 muted, bit 2 silent
(0 in a build without the USB input: op 5 gets no reply there); `ret` the returns' mute bits (bit n = return n). A muted return's level is gated at the output of the render and its bus keeps running (a tail is there on unmute; what it sends on to the returns on its right is silent too). The returns have no SOLO.
The `TRACKS` push carries the track bits; a lane, USB or return change made on the device is not pushed (read `SOLO` 0 after a `TRACKS` push, or poll). A bad op, track, lane or on: no reply.

## Macros (65)

`ed_macro.c` (`MACROS`). The editor reads and writes the authored values; a macro moves them inside the audio ISR only. `MACRO track` says what plays: reply `track, COLOR, MOTION, SPACE, ENERGY (4 x v14, each -64..63, 0 = home), n (7 bit), n entries`:
kind 0 = `0, id, v14` the track's `P_id` plays as v14; kind 1 = `1, id, v14` the global `G_id` plays as v14; kind 2 = `2, 0, sc v14, add v14` every drum lane's REV send plays as clamp(send x sc / 64 + add, 0, 127). Only moved values are listed. Ask again for the selected track on a track change and about every 500 ms;
the page shows the effective value with an M mark. `?mock=1&macros=1` turns the mock's answer on.

## CZ collection (66)

`ed_cz.c` (builds with `NATIVE_BANKS` and the CZ engine; after Melodee 0.12's command 78, keremimo/melodee, GPL-3.0-only). 26 CZ-1 tones (`nbank.c`); a CZ track plays slot k as TONE U(k+1). A tone is Casio's 144 bytes (128 synthesis bytes, then the 16-byte name); on the wire each byte is two nibbles, low first. The device does not check the bytes (the editor does, with Melodee's `cz_patch_valid`).

| cmd | Request | Reply |
| --- | --- | --- |
| 66 CZ_BANK | 0 (get), slot 0..25 | 0, slot, rc (0 ok, 1 empty or no such slot), then (rc 0) 288 nibbles |
| 66 CZ_BANK | 1 (put), slot, 288 nibbles; or 1, slot alone (erase) | 1, slot, rc: 0 written, 1 arguments, 2 flash, 3 the transport plays. Writes flash: allow 1 s |

## SLOOP files (78, 90)

`ed_sl24.c`, `storage/sl24/` (`SL24_EXPORT` / `SL24_EDIMPORT`, default off; no flash is written). The compatibility is SLOOP 2.7's. A firmware without them does not answer: the editor shows no button.

**Export (78 SL24_GET):** request `part, offset (3 x 7 bit) [, flags]`; reply `part, rc (0 ok, 1 arguments, 2 busy: a restore or import holds the buffer), length (3 x 7 bit), CRC-32 of the part (5 x 7 bit), lost (2 x 7 bit), offset, pack7 bytes (<= 256; none at its end) [, lost2 (2 x 7 bit)]`.
`flags` 1 asks for the second lost word, sent as the reply's last two bytes (any other flags value is rc 1; without the byte the reply ends after the data). **Part 0** (the only part; 1..3 give rc 1) is the working project as a SLOOP FUN5 (3840 bytes, its FNV-1a sum; `sl24_export.c`) which SLOOP 2.7 reads, with the step extras. The editor
asks for part 0, offset 0 at connect to know the build has the export, and reads a part again when its CRC moved between chunks. The editor saves it as SLOOP 2.7's song file (`{format: "sloop-song", version: 1, name, firmware, date, order: null, objects: [{id: 0, ...}]}`, data base64, CRC-32): in SLOOP's editor, Song & sections > Load a song file.
The drum lanes go onto SLOOP's 16 sounds by their instruments with 2.7's TUNE / DECAY packing; a synth step's `SF_CARRY` is 2.7's held notes; engines are numbered as SLOOP 2.5 / 2.6 do.

**What was lost** (`SX24_*` in `sl24_export.c`; the page's `S24.LOST`, `S24.LOST2`). Word `lost` (bits 0..13): 0 an engine SLOOP has not (SLICE, ACID, CZ: a fallback), 1 an FM6 voice that is not one of SLOOP's factory patches F1..F8, 2 FX OFF, 3 ANALOG 2, 4 a drum kit past 2.4's 37, 5 locks 2.4 has not, 6 drum lanes beyond SLOOP's,
7 the reverb type / COMP / LIMIT, (8 retired), 9 an amount of an effect in no slot, 10 COMP, 11 a drum sound's DIST, 12 the drum bus's DST CHO DLY REV, 13 motion (hold events). Word `lost2` (bit n = `SX24_*` bit 14 + n): 0 a step's chance, 1 an LFO RATE synced to the tempo, 2 the drum lanes' motion and locks,
3 a drum lane's LFO, 4 an FX return below unity, 5 a POLY note's own end, 6 a FILTER before a DIST, 7 a further insert (FOLD .. ECHO, GATER), 8 a step condition, 9 a part's LFO 2 not written as 2.4's LFO, (10 unused), 11 an LFO DEST slot 2.4 has not, 12 the ACCENT lane, 13 the delay filter (not LP 12 dB RES 0).
A request without the flags byte gets bit 13 of `lost` as "motion or chance".

**Import (90 SL24_PUT):**

| Request | Reply |
| --- | --- |
| 0 (begin), length (3 x 7 bit), CRC-32 (5 x 7 bit) | 0, rc |
| 1 (data), offset (3 x 7 bit), CRC-32 of the chunk (5 x 7 bit), pack7 bytes (<= 256, in order) | 1, offset, rc |
| 2 (commit) | 2, rc (0: imported, the working project now) |

The object is a SLOOP 2.7 project of a song file (its object 0) or a backup file (object 0, or 2..5 = A..D): a FUN6 (3816 bytes) which the editor makes a FUN5 first (`sl26ToFun5`), or a FUN5 as it is; exactly 3840 bytes. The FM-1 puts SLOOP's 16 drum sounds on our lanes (`sl27_lanes_in`; more than 10 used: the 10 with the most hits, "DRUMS CUT"),
turns 2.7's held notes into `SF_CARRY`, maps an arp mode we have not to the nearest ("ARP CUT"). The commit imports it as `PROJECT` > LOAD twice does with a 2.4 project in flash (`sl24_import_buf`): the working project; SAVE keeps it.
rc: 0 ok, 1 arguments / no import begun, 2 CRC, 3 the transport plays, 6 a length other than 3840, 8 not a SLOOP project, 9 busy. A begun import holds the project buffer: 10 s without a command lets it go.

## FM6 and DX7 SysEx (no command)

`engines/fm6/eng_fm6.c`, `fm6_sysex.c`. FM6 is the DX7 engine with Dexed's MODERN synthesis only. What the device **receives** on any MIDI channel n (it sends nothing and stores nothing in flash):
- `F0 43 0n 00 01 1B <155 VCED bytes> <checksum> F7`: the voice replaces the voice of the FM6 track (the selected one when it plays FM6, else track n + 1, else the first FM6 track; else `FM6: NO FM6 TRACK`). A wrong checksum: `FM6: BAD CHECKSUM`. The user SAVEs AS on the device to keep it.
- `F0 43 0n 09 20 00 <4096 VMEM bytes> <checksum> F7`: 32 voices become a temporary pick list at the end of that track's PRESETS (RAM, lost at power-off).
- A dump request (`F0 43 2n ..`) gets no answer; a parameter-change message is let go. A frame arriving while the previous waits for the main loop is dropped (the editor waits 120 ms after a voice).

The engine's EDIT value 0 `VOICE` is an enum: `INIT VOICE` then the voice packs the build has (builder items `FM6_PK_KEYS`, `BASS`, `BELLS`, `PADS`, `80S`; 16 voices each); its range and names depend on the build. The editor has no FM6 panel and no bank: a DX7 voice of the library is only auditioned
(it selects FM6 on the track if needed and sends the single voice).

## Notes for the editor

- **Following the device.** `WATCH` (with bit 4) and `PING` about every 1 s; every change is pushed. Without a reply to `WATCH` the firmware is too old to follow: poll `DUMP` about every 300-500 ms while the page is visible.
- **Step automation** (Sequence popup): a step of the track shown, written through `AUTO_GET` / `AUTO_SET` (and, with `ED_SEQ`, `STEPLANE`): CHANCE, COND, NUDGE and FILL are the step-only pseudo-parameter events and a default deletes the event; a lock is the step-only event of a `P_*` id or of a drum sound's value (`AUTO_LANE`).
  The editor keeps the device's drum rule itself (no lock without the lane's hit on the step) and reads the list again after every write: the device's list is the one truth.
- **Safety.** Writes to flash are the commands listed under "Frames".

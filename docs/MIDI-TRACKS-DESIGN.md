# MIDI tracks (design, evaluation, 2026-10-28)

Nothing here is built. This page sets out what it would take to add **4 MIDI-only tracks** (M1..M4) to the release in
**LAYOUT 1** only: the SD card mode and the reduced user flash mode (fm1-base's app slot up to 0xCE000, one project
in flash). A MIDI track has no sound engine. It sequences external gear over MIDI, as a mono, poly or drum track.
The aim is to give the user enough to decide. **M** = measured (how is given), **E** = estimated.

## 1. The user model

- **M1..M4** sit beside T1 T2 T3 DR. SHIFT2 + white keys 5..8 selects one, as white keys 1..4 select T1..DR.
- **A MIDI track's page** (SOUND's place: it has no sound) has CHANNEL (1..16, OFF), MODE (MONO, POLY, DRUM),
  VELOCITY (scale, %), TRANSPOSE, and NOTE LO / HI (MONO / POLY: notes outside the range fold back in by octaves).
  DRUM mode adds a **drum map**: each of the 10 lanes (the black keys) has a MIDI note, GM by default
  (36 38 42 46 ...).
- **The step page** works as on the synth tracks (MONO, POLY: up to 4 notes a step) and as on the drum track (DRUM:
  the lanes). The step page, ratchets, ties, slides, LEN DIV SWING GATE and the 16 pattern slots all work the same way.
- **Mixer strips** M1..M4 come after the USB row: VEL (velocity scale) and MUTE only. They have no level, pan, sends or
  inserts.
- **Output** goes through the existing MIDI OUT path (`seq_midi.c`: per-track note-off bookkeeping, STOP and channel
  changes end what was sent, ISR-safe queue). It does not depend on MIDI OUT = KEYS / SEQ. See question 7.

## 2. The data model

**A separate slim type plus a sequencer core shared with `track_t`**, not NTRK 8. `NTRK` is used 583 times in 90
files. The value 4 is also built into the formats: a scene record's last 4 bytes are its pattern refs
(`pat_scene_enc`, asserted `NTRK == 4`), pattern ids are 24 + 16 x track (< 88), and the editor's tracks are 0..3.

- `seqc_t`, the shared core: the step union (`step_t[64]` / `dstep_t[64]`), the playback state (`seq_abs`, `seq_idx`,
  `seq_notes`, `seq_off`, `seq_noff`, ratchets, `pass`, `org`) and the live-recording state (`rh_*`, `rt_*`). `track_t`
  embeds it, and so does `mtrk_t` with a small parameter block, the drum map and its `mo_set` row.
- `seq_tick`, `seq_step` and `drum_step` work on the core. Where a synth track starts a voice (`trk_note_on`, drum
  voices), a MIDI track calls `seq_out_on` / `seq_out_off` on its own channel. The arp, chords+, QNT SEQ and the
  generators stay on `track_t` in phase 1.
- How tightly the code is tied to `track_t` (M, grep at fm1-next 99c8ce32): `->step` 284 uses in 33 files, `->dstep`
  84 in 25, `t->p[...]` 51 reads in `seq.c` (mostly CHORD, AMODE, ROOT, SCALE), and `trk_index` / `&trk[]` 99 in
  `seq/`. The step-page UI, undo, the codec and the editor reach steps through the track. Giving them a
  `seqc_t *` (`trk_core(k)`, k 0..7) is the bulk of the work.
- Parameter ids: giving `mtrk_t` a full `p[P_COUNT]` (+224 B a track, M) keeps `t->p[P_SLEN]` and friends unchanged in
  the core. Remapping 16 values instead saves about 0.9 KB for all four tracks but touches every read.

## 3. Storage per layout

Both LAYOUT 1 modes keep the **working project in the same 7-sector (28 KiB) section log at 0xD0000** (project 0,
`PROJ_SLOTS` 1). In SD mode a card SAVE writes project 0's records as a stream (u8 id, u16 length, record:
`sd_mode.c sdm_stage_*`), and a card LOAD writes a file's records back into project 0. **The card therefore has no
pattern-log limit for saved copies, but the project in use must fit the 28 KiB log in both modes.** The card does not
get around the id problem either: its stream uses the same u8 ids.

- **Ids.** 24..141 are taken (`SLG_IDS` 142). Proposed: 142..205 the MIDI patterns (142 + 16 x M + slot, as the
  synth patterns' 24 + 16 x track + slot), 206..221 each scene's 4 MIDI refs (a record of its own: appending them to the scene record
  would shift the synth refs an older build reads from its end), and 222..225 the MIDI tracks' working copies (the
  autosave's: `project_t` is already about 4 KB, `proj_tmp` 4,060 B M, and the autosave sector is one 4 KiB copy).
  That makes `SLG_IDS` 226, which still fits a u8.
- **Older firmware.** In the flash log, records past its `SLG_IDS` are skipped and its compaction drops them, so a
  downgrade loses the MIDI tracks and nothing else. A card file or backup is worse: `sdm_feed` rejects the whole file
  at an unknown id ("NOT A PROJECT"), while the editor's PJ01 restore skips the record. **Phase 0** therefore ships
  first: index and keep ids up to 225, and make `sdm_feed` skip an unknown id. This is the same move the patterns made
  (`sec_log.c`: "this must ship long before patterns are written").
- **Room (E, from the codec: head 16 B, flags + LEN DIV SWING GATE 5 B, step bitmap, 10 B a step at most).** A
  pattern is about 90 B (16 mono steps), about 185 B (16 four-note steps) and at most about 670 B (64 steps).
  64 typical MIDI patterns come to 6..12 KB of the 28 KiB log, which today holds "about 5 dense scenes"
  (SD-MODE-DESIGN.md 7). 64 full patterns (about 43 KB) cannot fit. The existing log-full handling applies
  (`MEM FULL`, `NO FREE PATTERN`). Question 4 offers 8 slots a track to halve this.
- **Optional, SD mode only:** keep MIDI patterns in a card file next to the project and read them a bar ahead of a
  launch. This takes them out of the log, but it needs card I/O while playing and does not exist without the card.
  Not recommended for phase 1.

## 4. UI pages (Optimist UI)

| Where | What changes |
|---|---|
| `op_input.c shift_keys` | SHIFT2 + white keys 5..8 selects M1..M4. `song.sel` becomes 0..7 (it is 0..3 now: every `% NTRK` on it needs checking) |
| New `op_midi.c` | The MIDI track page (section 1), the drum map's 10 rows |
| `op_step.c`, `op_stepdraw.c` | Use the selected `seqc_t`. MONO / POLY draws as a synth track, DRUM as the drum track (lane names: the map's notes) |
| `op_mixer.c`, `op_mixdraw.c` | Rows M1..M4 after USB: VEL, MUTE |
| `op_song.c` | Patterns row and scenes table: four more columns (or a second page of four) |
| Header | M1..M4 named as T1..DR |

## 5. Editor protocol

Nothing is needed for the device to work. Backups carry MIDI tracks automatically (PJ01 streams every id). For live
editing: tracks 4..7 in `TRACK` (27), `TRACK_PARAM` (31) and `STEP_GET` / `TRACK_STEP`. Today those give no reply for
a track >= NTRK, so an older client is safe. Behind a new feature name in INFO tag 66 (`opti_miditrk_1`), tag 54 gets
n 6..9 (M1..M4's channels). The web pages for a MIDI track are web work on top (the release builds `ED_SEQ` 0).

## 6. What it costs

| | SD card mode (sd-dev-07efe611) | LAYOUT 1, no card (same less SDCARD SD_REC SD_RING ENG_SAMPLE) |
|---|---|---|
| Flash free in the 823,228 B slot (M) | **143,604 B** (image 679,624) | **187,992 B** (image 635,236) |
| SRAM free of 475,136 (M) | **33,720 B**, of which 8,192 is the kept spare: 25,528 above it (RAM code 53,792, .data+.bss 129,172, pool 258,448) | **66,824 B**: 58,632 above the spare (51,184 / 124,460 / 232,664) |
| 4 MIDI tracks' code (E) | +8..14 KB flash (page, map, mixer rows, storage, editor) | same |
| 4 MIDI tracks' RAM | **4,384 B** M (4 x `mtrk_t` 1,096: steps 896, state, 16 values, drum map, note set); 5,280 B M with a full `p[]`; 3,008 B M at the minimum (stored-form steps, no live recording) | same |
| Index and extras (E) | +672 B `.bss` (84 ids x 8 B in `slg`, 1,232 B M today), about 250 B stack in compaction, about 40 B of per-track arrays | same |
| CPU (M host, E target) | See below | same |

Measured on 2026-10-28, release builds on gpufarm (fm1-base ee42003 with Optimist fm1-next 99c8ce32, FM1B_APP_END
0xCE000, the build's own SRAM line). `track_t` is 2,172 B on the target (`trk` 8,688 B, elf) and 2,168 B on the host.
The prototype structs were compiled in the host build (`sizeof`, arm64; no pointers, so the target should match
within alignment).

**CPU.** Measured on the host: `events_block` alone, instructions per 32-sample block (`proc_pid_rusage`, cc -O2,
200,000 blocks, the release's sequencer switches): 765 stopped, 1,262 playing with empty steps, 1,527 with every
step a 4-note chord on T1..T3 and every lane on DR, and 1,598 with MIDI OUT on as well. That is about **124 a track
playing empty and 208 a track dense with MIDI OUT**. Four MIDI tracks would add about 830 host instructions a block,
26 a sample. At the fitted 1.04..1.27 target per host instruction (costs.json `cpu`), that is 27..33 a sample. If
CPU0 runs at 312 MHz and one instruction is one cycle (neither checked), this is about 0.5 % of CPU0, inside the audio
ISR. A block where all four fire a chord: the whole `events_block` peaked at about 15k host instructions (noisy: one
sample read 51k). That is an upper bound, because it includes the synth voices' starts, which a MIDI track does not
have.

## 7. Risks

- **The core refactor touches the hot path in the ISR.** The goldens and the sequencer tests must come out
  identical before any MIDI code goes in.
- **Log room in 28 KiB:** MIDI patterns compete with scenes and synth patterns, so `MEM FULL` comes sooner.
- **Downgrades:** older builds drop the MIDI records (flash) or refuse the file (card) unless phase 0 shipped first.
- **`song.sel` 0..7:** every `% NTRK` on the selection and every 4-entry UI table has to be checked.
- **Hanging notes on external gear:** these are covered by `mo_set` (STOP, channel change, OFF). A MIDI track muted
  mid-note, or a pattern launched mid-note, needs the same off.
- Nothing has run on an FM-1. The host numbers are instructions, not cycles.

## 8. Phased plan (Claude's effort, host-tested; the user tests on the FM-1)

| Phase | What | Effort (E) |
|---|---|---|
| 0 | `SLG_IDS` 226 indexed and kept, `sdm_feed` / restore skip unknown ids. Ships alone, first | 0.5 day |
| 1 | `seqc_t` core shared by `track_t` and `mtrk_t`; the goldens identical | 2..3 days |
| 2 | M1..M4 play: `events_block` ticks them; channel, mode, range, drum map, VEL, mute; note-off bookkeeping rows 4..7 | 1 day |
| 3 | UI: selection, MIDI page, step page, mixer rows, SONG columns | 2..3 days |
| 4 | Storage: patterns, scene refs, working copies, undo, the card stream | 2 days |
| 5 | Editor: tracks 4..7 behind `opti_miditrk_1`, tag 54 n 6..9 | 1 day (+ web pages) |
| later | Live recording into M tracks, locks / chance / conditions / generators (the core makes them cheap; automation pool about +1.5 KB a track, E from `auto_*` sizes) | |

In all, about 9..11 days for phases 0..5.

## 9. Open questions for the user

1. Only LAYOUT 1 (with and without the card), or the normal layout too later?
2. DRUM mode: one channel per track with a note per lane, or a channel per lane? Is the ACCENT lane a velocity boost,
   as on DR?
3. POLY: are 4 notes a step (the synth tracks' limit) enough?
4. Pattern slots per MIDI track: 16 as the synth tracks, or 8 to save log room (32 ids, about half the bytes)?
5. Live recording into M tracks (panel keys, MIDI IN) in the first version, or later?
6. Locks, chance, conditions, generators, CC lanes on M tracks: first version or later?
7. Do M tracks always send, or only when MIDI OUT is SEQ / BOTH? Which outputs: every MIDI OUT (USB, BLE), or one
   picked per track?
8. With an M track selected, do the panel keys play the external gear live on its channel?
9. Arp on M tracks?
10. Program change (and bank) per pattern or per scene?
11. Mixer: VEL and MUTE only, or SOLO too?
12. An M track and a synth track on the same channel: allowed, warned, or refused?

# Per-track patterns and scenes (PATTERNS)

How patterns and scenes work today. Builder item `PATTERNS` (Sequencer, bit 250, default 0; on in the profiles user-default,
fm-va-studio, drum-machine, x0x-drums and synth; an error with `SECTIONS 4`). The history of the design is in git
(`git log -- docs/PATTERNS-DESIGN.md`).

## 1. The model

- Each of the 4 tracks has **16 pattern slots**. A pattern is the notes of one track: the steps up to LEN, and LEN, DIV,
  SWING, GATE. Its automation travels with it: locks, motion, nudge, fill, chance, condition (the track's list of up to
  128 events, `seq/auto.h` `AUTO_MAX`). The sound, the mix, the key and the arp are not in a pattern.
- A **scene** is a section (A..P) that names one pattern per track (slot 0..15, `PAT_NONE` 0xFF: the track is empty,
  `PAT_KEEP` 0xFE: the track goes on with what it plays) and keeps the sound, the mix and the globals. The song chains
  scenes, unchanged.
- The working copy of each track remembers its source slot (`pat_cur`). Storing a scene refers to the source of a track
  that was not changed (shared, nothing written); a changed track is written into its source when no other scene
  plays it, else into slot s (the scene's letter) or the lowest free slot. A stored scene never changes when another is
  stored. No free slot: `T2: NO FREE PATTERN`, nothing is written. Patterns are written first and the scene record last
  (the commit), so a power cut leaves at most a pattern no scene uses.
- Storing into a slot on purpose (SAVE + key n) overwrites it: every scene that refers to it plays the new content.
  A pattern no scene uses stays until it is cleared.
- Sections of older projects become scenes at the first start of a PATTERNS build (`pat_migrate`: one section at a
  time, cut anywhere and the next start goes on; a section not yet converted still plays).
- Every build with the section log (`SECTIONS 8` or `16`) reads scenes and **flattens** them when it reads them
  (`pat_flatten`, called by `sec_read`): without `PATTERNS` a scene plays as the section it would be, and storing a
  section there writes a plain section over it (its patterns stay in the log). Back on a PATTERNS build that section
  converts again.

## 2. Where it lives

| Thing | Where |
|---|---|
| pattern and scene logic, launch, store, migrate | `firmware/src/storage/sections/pat.c` (included by `sections.c`) |
| scene record (`SEC_SCN` 0x20 in the flags byte), step form shared by sections and patterns | `storage/sections/sec_codec.c` |
| the log | `storage/sections/sec_log.c`: 8 sectors of 4 KiB at 0x97000, records never span a sector, one spare sector for compaction |
| the sequencer side | `seq/seq.c` (`TRK_IDX`, `pat_switch`, the `PAT_*` and `PW_*` constants), `track_t.org` in `core/core.h` |
| device keys | `ui/optimist/op_layers.c` (`pat_layer_key`, `pat_knob`), `op_song.c` (SONG), `op_keys.c` (lights), `op_laydraw.c` (the 4 x 16 grid) |
| editor commands 79..84 | `io/editor/ed_pat.c`, `web/editor.html`, `web/EDITOR_PROTOCOL.md` "Patterns and scenes" |
| snapshots and backup | `snapshots.c` (log records, kind `SNR_LOG`), `ed_backup.c` (objects `PTN1`..`PTN6`) |
| missing patterns | `storage/miss.c`: `MISSING: PAT T2 5` |

### Log ids (the same in every build)

0..15 scenes / sections, 16 the song, 17 the working copies' sources (`SEC_ID_PSTATE`, written with the autosave),
**24..87 the patterns** (`PAT_ID(track, slot)` = 24 + 16 x track + slot). Other ids: 88..104 the sections' and the
autosave's step extras, 105..121 FX records, 122..138 drum lane LFO records (`SLG_IDS` 139). A build keeps the
records of every id it knows, used or not, so switching `PATTERNS` or `SECTIONS` loses nothing. A record of an id past
the known ones is skipped and not kept by a compaction.

### Pattern record (`pat.c`)

`flags` (PF_MOT 1, PF_B 2 codec B, PF_DRUM 4, PF_ON 8 motion PLAY, PF_SX 16, version in bits 5..7), `LEN DIV SWING GATE`,
[automation chunk: n, then n x (place, param, value)], the step bitmap and the steps (the `sec_codec.c` form, codec A
or B, whichever is shorter), [PF_SX]. Version 2 (written today) has the automation store's list (up to 128 events,
step-only bit in `place`) and no PF_SX. Version 1 records (motion chunk of up to 64 hold events, SLOOP 2.4 step extras)
are still read, never written. A build that does not know a version reads the pattern as missing. A track with no
step, no event and no extras costs no record: the scene says "none".

The longest pattern record is `PAT_REC_MAX` and the longest scene `SCN_REC_MAX` (`sec_codec.c`). The log's reserve
(`sec_log.c` `sm_keep`) is four patterns and a scene, each its own record, so the playing scene can always be stored
with every track changed. MEM FULL appears when that reserve would not be left.

## 3. Sequencer

- Each track has an origin: the step index is `(abs - org) % LEN` (`TRK_IDX`), used by the sequencer, recording,
  automation and the macro page. A launched pattern starts at its step 1.
- `pat_launch(track, slot, when)` is the one call keys, SONG and the editor share. Stopped: the pattern loads at once.
  Playing: `pat_service` (main loop) decodes it into the scene stage's track (`sec_stage_p.t[k]`), so there is no
  buffer of its own, and the audio ISR applies it in `pat_switch`:
  - `PW_END` (default): where the playing pattern wraps;
  - `PW_BAR`: at the first step of the next bar;
  - `PW_NOW`: at the next step, origin kept (legato).
- At a switch: one undo level for the track, its take ends, its automation is replaced by the pattern's, then the
  steps, the four values, the extras, the origin; `pat_cur` follows. A switch waits while a live jump is due.
- A scene staged after a launch is read over it and the launch is staged again ("scene A with T1's pattern 1").
  A track the scene keeps takes what it plays into the stage just before the scene applies.
- Song mode: a launch overrides that track until the song's next part, which brings its own patterns. SONG REC records
  scene launches only, not pattern launches.
- The CPU guard has nothing to do with a launch (no engine or voice count changes).

## 4. Device (Optimist UI)

- **LFO held** is the patterns layer (a tap still opens the LFO pages; LFO is SHIFT for the knobs, so the layer has no
  knobs of its own). The 16 white keys are the selected track's 16 slots: playing full, queued blinking, stored dim.
  - white n: launch at the pattern's end; OCT+ held: now. Stopped: loaded at once.
  - SAVE + n: store the working copy into slot n (asked over a used one); SHIFT (OCT-) + n: clear slot n (asked, says
    "CLEAR USED" when a scene plays it; refused while playing: `STOP FIRST`).
  - black keys 1..4 pick the track, 5 stops it at its end, 9 duplicates the working copy into the first free slot,
    10 toggles the scenes (the white keys become scenes A..P: n launches, SAVE + n stores the loop, HOME + n clears).
    Black 6..8 do nothing (they were store, copy and clear).
  - KNOB 1..4 cue the previous / next stored pattern of track k at its end.
  - SAVE + SEQ stores the working copy into the slot it came from, else the first free one.
- **SONG** (SAVE held + SELECT) has a PATTERNS row (each track's playing pattern; a turn cues; the panel is the 4 x 16
  grid; the keys are the layer's) and scene rows (the hot cell's knob edits a track's reference: a slot, KEEP or "-").
  A scene tile shows `*` when its tracks no longer play its patterns as stored (`pat_changed`, compared twice a second).
- Messages: `T2 5 AT END`, `T2 5 NOW`, `LOADED T2 5`, `STORED T2 5`, `CLEARED T2 5`, `T2: NO FREE PATTERN`, `MEM FULL`,
  `STOP TO SAVE MORE` (the arena is full), `MISSING: PAT T2 5`.
- Colour: green playing, amber queued, the track's colour as a band on a stored slot.

## 5. Web editor

With INFO tag 0x55 the mixer strips show the 16 slots of a track above the instrument row and the MASTER strip shows
scenes A..P in the same rows. Click launches (after 300 ms, so a double click is a different gesture), shift-click
launches now, alt-click at the next bar; double click opens the slot in the Sequence popup (the playing pattern edits
the working copy, any other edits the record through PAT_READ / PAT_WRITE); drag copies; Delete clears (confirm). The
editor polls PAT_LIST every 500 ms while the mixer shows. Commands: 79 PAT_LIST, 80 PAT_LAUNCH, 81 SCENE, 82 PAT_OP
(store, copy, clear, duplicate), 83 PAT_READ, 84 PAT_WRITE (chunks of 256 B into `proj_tmp`, the last one commits via
`pat_decode` + `pat_write`; stopped: the log, playing: the pending arena). 85 is free. No protocol version bump; the
details are in `web/EDITOR_PROTOCOL.md`.

## 6. Numbers that matter

- The log is 32 KiB (8 sectors). Measured on 2026-10-07 with codec B (`tools/patterns_measure/run.sh`): all 16 busy
  64-step scenes with their own patterns fit, 62 of 64 slots filled with busy patterns beside 16 busy scenes; the
  largest pattern is 846 B. Not re-measured since the automation store (version 2 records can hold more events).
- Cost of the `PATTERNS` item (`tools/builder/costs.json`, default build): +9,568 B flash, +208 B RAM, +88 B RAMTEXT.
- The pending arena (`sec_pend`, `.noinit`) holds sections and patterns stored while playing; they are written when the
  sequencer is quiet (a flash write stops the audio).

## 7. Tests

`tests/patterns_test.c` (store, sharing, copy-on-write, migration cut at every flash program, the arena, the reserve,
across builds), `tests/patterns_seq_test.c` (END / BAR / NOW, odd lengths, swing phase, automation per track, undo,
song mode), `tests/patterns_ed_test.c` (editor commands), `tests/sec_log_test.c` and `tests/sec_codec_test.c` (ids 24..87,
codec B), `web/test_web.mjs` `editorPatterns`, `web/e2e_daw.mjs`.

## 8. Open limits

- Pattern launches are not recorded by SONG REC.
- The "next bar" launch (`PW_BAR`) is reachable from the editor (alt-click) and `pat_launch`, but on the device
  `pat_layer_key` tests SHIFT (OCT-) before it reaches `pat_launch_key`, so OCT- + n clears instead (flagged, not
  verified on hardware).
- Copy between slots has no device key now (the editor's PAT_OP 1 does it; the drum pattern copies only to itself).
- A non-PATTERNS build does not print "PATTERNS: PLAYED AS SECTIONS" or "MOTION CUT" (never built).
- The editor shows a slot's LEN only, not a step map.

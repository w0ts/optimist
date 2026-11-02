# Per-track patterns and scenes (PATTERNS)

Builder item `PATTERNS` (bit 250, default 0; on in every shipped profile, 2026-10-24; an
error with `SECTIONS 4`). **Everything below is per project** (`docs/PROJECTS-DESIGN.md`): the 16 pattern slots a
track, the scenes A..P and the song belong to the open project; the log is format 2 at 0xA0000. "The device" in this
document reads "the open project".

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
| the log | `storage/sections/sec_log.c`: records never span a sector, one spare sector for compaction; its area and format 2: `docs/PROJECTS-DESIGN.md` section 2 |
| the sequencer side | `seq/seq.c` (`TRK_IDX`, `pat_switch`, the `PAT_*` and `PW_*` constants), `track_t.org` in `core/core.h` |
| device keys | `ui/optimist/op_layers.c` (`pat_layer_key`, `pat_knob`), `op_song.c` (SONG), `op_keys.c` (lights), `op_laydraw.c` (the 4 x 16 grid) |
| editor commands 79..84 | `io/editor/ed_pat.c`, `web/editor.html`, `web/EDITOR_PROTOCOL.md` "Patterns and scenes" |
| snapshots and backup | `snapshots.c` (log records, kind `SNR_LOG`; the live patterns kind `SNR_LIVE` 18), `ed_backup.c` (objects `PTN1`..`PTN6`; each project's live patterns `PL01`..`PL16`) |
| live patterns (issue 142) | `pat.c` (`pat_leave`, `pat_get` / `pat_get_saved`, `pat_swap_isr`, `pat_left_service`, `pat_reload`, `pat_promote_all`), `sec_log.c` (the live scope, `slg_live_ok`, `slg_live_sweep`): section 9 |
| missing patterns | `storage/miss.c`: `MISSING: PAT T2 5` |

### Log ids (the same in every build)

0..15 scenes / sections, 16 the song, 17 the working copies' sources (`SEC_ID_PSTATE`, written with the autosave),
**24..87 the patterns** (`PAT_ID(track, slot)` = 24 + 16 x track + slot). Other ids: 88..104 the sections' and the
autosave's step extras, 105..121 FX records, 122..138 drum lane LFO records; format 2 adds 19 (a project's state), 21
(the project names) and 139..141 (the state's extras, FX and LFO records); 142..225 the MIDI tracks' (reserved),
`SLG_IDS` 226. A build keeps the records of every id it knows, used or not, so switching `PATTERNS` or `SECTIONS` loses
nothing. Format 2 keeps (and never indexes) a record of an id past the known ones; format 1 skips it. **The live
patterns** (section 9) use the pattern ids 24..87 again under the project tag `0x10 | p`.

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

The keys are in `docs/guide/shortcuts.md` ("Saving, patterns, scenes and the song"); the code is `op_layers.c`
`pat_layer_key`. **LFO held** is the patterns layer, the same keys as SONG's PATTERNS row: white n launches (OCT- held:
the next bar, OCT+ held: now), SAVE + n stores, HOME + n clears (the user, 2026-10-20: HOME clears, as elsewhere); black
1..4 pick the track, 5 picks ALL TRACKS (the white keys, SAVE + n and HOME + n then act on every track), 6 stops it, 9 duplicates, 10 shows the scenes; SEQ held + n stores scene n; a black key 1..5 held + white n stores that track's (5: every track's) working pattern in slot n. A launch or stop keeps the outgoing pattern's edits (section 9; *LOSE EDITS?* of 2026-10-31 is gone), the toast says *T1 T3 KEPT*. **HOME + SEQ is RELOAD** (asked: *RELOAD T1?*; the pattern page with ALL TRACKS: *RELOAD ALL?*). Black 7 and 8 do nothing
(store, copy and clear became SAVE + n and HOME + n, the user 2026-10-13). A SONG scene card reads `SCENE B*` when its
tracks no longer play its patterns as saved (`pat_changed`); the grid marks every slot that keeps edits not saved (a
small white square, as the playing one changed).

## 5. Web editor

With INFO tag 0x55 the mixer strips show the 16 slots of a track above the instrument row and the MASTER strip shows
scenes A..P in the same rows. Click launches (after 300 ms, so a double click is a different gesture), shift-click
launches now, alt-click at the next bar; double click opens the slot in the Sequence popup (the playing pattern edits
the working copy, any other edits the record through PAT_READ / PAT_WRITE); drag copies; Delete clears (confirm); S
saves a slot's edits, R reloads it (confirm). A slot that keeps edits not saved reads `12*` (its LEN as it plays). The
editor polls PAT_LIST every 500 ms while the mixer shows. Commands: 79 PAT_LIST (op 0 ends with the live patterns' bits,
3 bytes a track), 80 PAT_LAUNCH, 81 SCENE, 82 PAT_OP (store, copy, clear, duplicate, 4 SAVE, 5 RELOAD), 83 PAT_READ (the
pattern as it plays; track + 4: the saved one), 84 PAT_WRITE (chunks of 256 B into `proj_tmp`, the last one commits via
`pat_decode`: the slot's live pattern (issue 142 decision 4; the slot a track plays takes it, at its end while playing),
track + 4 the saved one (`pat_write`); stopped: the log, playing: the pending arena). 85 is free. No protocol version
bump: an older firmware sends no live bits and the editors offer no SAVE / RELOAD. The Mirror app's Projects tab shows the
same marks and offers Save / Reload with the same question. Details: `web/EDITOR_PROTOCOL.md`.

## 6. Numbers that matter

- Measured on 2026-10-07 in the format-1 log of 32 KiB, with codec B (`tools/patterns_measure/run.sh`): all 16 busy
  64-step scenes with their own patterns fit, 62 of 64 slots filled with busy patterns beside 16 busy scenes; the
  largest pattern is 846 B. Not re-measured since the automation store (version 2 records can hold more events) nor in
  the format-2 log (80 or 128 KiB a device, every project in it).
- Cost of the `PATTERNS` item (`tools/builder/costs.json`): +11,892 B flash, +140 B RAM, +128 B pool, +20 B RAMTEXT.
- The pending arena (`sec_pend`, `.noinit`) holds sections and patterns stored while playing; they are written when the
  sequencer is quiet (a flash write stops the audio).

## 7. Tests

`tests/patterns_test.c` (store, sharing, copy-on-write, migration cut at every flash program, the arena, the reserve,
across builds), `tests/patterns_seq_test.c` (END / BAR / NOW, odd lengths, swing phase, automation per track, undo,
song mode; the live patterns while playing, the ISR's exchange timed), `tests/patterns_ed_test.c` (editor commands, the
live writes, SAVE / RELOAD), `tests/patlive_test.c` (the live patterns stopped, projects, PROJECT SAVE, stale records,
MEM FULL, a power cut at every program), `tests/sec_log_test.c` (the live scope) and `tests/sec_log_compat_test.c` (main
64c1d915's log keeps live records), `tests/sec_codec_test.c` (ids 24..87, codec B), `tests/snapshots_test.c projects`
(PL01, a snapshot's live patterns), `tests/ui_optimist_patlive.h` (RELOAD, the project LOAD question), `web/test_web.mjs`
`editorPatterns`, the Mirror app's `testPatternsLiveSaveReload`, `web/e2e_daw.mjs`.

## 8. Open limits

- Pattern launches are not recorded by SONG REC.
- Copy between slots has no device key now (the editor's PAT_OP 1 does it; the drum pattern copies only to itself).
- A non-PATTERNS build does not print "PATTERNS: PLAYED AS SECTIONS" or "MOTION CUT" (never built).
- The editor shows a slot's LEN only, not a step map.

## 9. Live patterns: every pattern keeps its own edits (issue w0ts/optimist#142, 2026-11-01)

Elektron's way: editing a pattern changes the pattern, and its edits stay with it until it is saved or reloaded, in
flash, across launches, the song, projects and power cycles. The plan: `worktrees/pattern-live-plan/PLAN.md` of
2026-11-01; the user's decisions of 2026-11-01 replace it where they differ (marked "decision").

- **Three versions.** The *working* one (what plays: `trk[k]`, the autosave keeps it), the **live** one (a log record,
  the pattern's id under the tag `0x10 | p`: the edits not saved), the **saved** one (the record of before). A slot plays
  its live record when one stands, else its saved one: `pat_get` (launches, scenes, the song, the preview, the editor's
  PAT_READ); `pat_get_saved` for the `*` marks, a copy's source, snapshots and the PJ backup streams.
- **Leaving a pattern** (a launch, a stop, a scene, the song's next part, another project) writes the outgoing working
  copy as its slot's live record when it differs from what the slot plays (`pat_leave`), drops it when it is the saved
  one again, writes nothing when nothing changed. Stopped: at once (the arena, then the log). Playing: the audio ISR
  **exchanges** the working track with the stage's (`pat_swap_isr`: steps one at a time through one temporary, LEN DIV
  SWING GATE, the list and PLAY bit, the notes' own ends) instead of copying the stage over it, so the stage holds the
  outgoing pattern with no buffer of its own; the main loop writes it (`pat_left_service`, before anything stages again)
  into the arena, and `sections_write` takes it to the log (LIVE_SAVE: one record a call while playing; without it, when
  quiet). For a song part, the leaving tracks are exchanged before `proj_apply`, which leaves their steps, LEN.. and
  lists alone (`pat_apply_keep`). A part or a launch naming the slot a track already plays keeps its edits playing, from
  step 1. A live record put reads a staged scene again (`sec_gen`), so the song reaching a pattern plays its edits.
- **Base stamp.** A live record starts with the sequence number of the saved record it was made from (0: none). One whose
  stamp is not the saved record's now is stale (that slot was saved, cleared or replaced since, here or by an older
  firmware) and is not played; `slg_live_sweep` drops it when the project opens. Every saved write of a slot (SAVE + SEQ,
  SAVE + n, a scene's store, a clear, the editor) drops its live record.
- **A new pattern** (issue 142 points 4 and 6, the user 2026-11-02): creating a pattern asks once, with one question shared by the pattern page (SAVE + n, SAVE + SEQ, a black key held + n on a slot with no pattern) and the SONG scenes' cells (KNOB 1..4 resting on the first pattern not filled yet): *CLONE T1 5?* (YES = SAVE, the default: the working copy stored there), HOME turns it into *EMPTY T1 5?* (YES: an empty pattern, which the track plays; from a scene's cell only its reference is set and the track keeps its own pattern), HOME again or 3 s: nothing created. A track's own, still empty slot and a used slot (*STORE T1 5?*) are as before. A KNOB on an empty SONG scene creates the scene (the loop stored as SAVE + its key does, *SCENE C CREATED*) in place of the *EMPTY SCENE* toast. Code: `op_layers.c` `pat_new_ask` / `pat_new_do`, `op_song.c` `song_scene_make`, `song_ref_step`, `song_ref_flush`.
- **RELOAD = HOME + SEQ** (decision 1; HOME held, SEQ pressed, acting when either is let go with no key used, so SEQ +
  HOME + a key stays the scene's clear on the pattern page; checked against every SEQ and HOME path of `op_combos.c`,
  `op_input.c`, `op_layers.c`, `op_song.c`: free). Asked *RELOAD T1?* (ALL TRACKS: *RELOAD ALL?*); nothing to reload: *NO
  EDITS*. Stopped: the saved pattern at once (an undo level); playing: at the pattern's end, nothing kept.
- **Projects** (decision 2): the live records belong to their project (the tag); another project opened keeps them, and
  they play again when it opens. A LOAD of another project with live edits or a changed work asks *SAVE PROJECT n?*
  first (YES saves everything, then opens; HOME asks the LOAD in red, HOME again cancels: `op_prjflow.c`). **PROJECT SAVE
  saves everything** (decision 3): the tracks left, every live record promoted (the saved record first, then the live
  tombstone: a cut between leaves a stale live record, swept), then the state; RELOAD goes back to that. A save as copies
  the patterns as they play into the target as saved, the source keeps its live ones. DELETE clears the live scope.
- **MEM FULL** (decision 5): a live record never takes the reserve; the log full, it waits in RAM (the arena,
  *MEM FULL: SAVE OR RELOAD*) and is written when room appears; the song never waits; the arena full too: *T1 4 EDITS
  LOST*. A project switch (open, DELETE, save as) with anything left in the arena is refused (*MEM FULL: SAVE OR
  RELOAD*): before 2026-11-01 `proj_quiet` cleared the arena after a failed flush, a silent loss.
- **Backups and snapshots** (decision 6) carry the live patterns as live: `PL01`..`PL16` (kind 7, id 16 + p: a stream of
  (id, length, record without its stamp), restored after the PJ streams, stamped against the saved records then); an
  older firmware knows no PL tag and skips it. A snapshot's kind `SNR_LIVE` (18) after its saved patterns; an older
  build skips it.
- **Compatibility.** An older format-2 build (main 64c1d915 and before) sees a record tagged 16..31 as another project's:
  counted, copied through every compaction as it is, never indexed, never counted in its projects strip
  (`tests/sec_log_compat.sh` against `tests/compat/sec_log_226.c`). Its DELETE or save as leaves the live records
  behind and its pattern saves make them stale: the base stamp drops them here.
- **Out of scope** (decision 7): a scene still sets the sound and the mix.
- **Costs and timing.** RAM: the index's 65 more entries (~520 B pool), the arena's 64 more entries (256 B `.noinit`), the
  room model on the stack (~200 B each). ISR: one track's exchange is 2.3-2.9x the copy it replaces on the host (~207 vs
  73 ns, `tests/patterns_seq_test.c`); on the FM-1 (360 MHz) about 18 us a track, ~70 us with four tracks switching at one
  bar, against an audio half of 5.8 ms: an estimate, not measured on hardware. Flash: a live record is written only when
  an edited pattern is left (page programs, LIVE_SAVE's windows), erases only by compaction.

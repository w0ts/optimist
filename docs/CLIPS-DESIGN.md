# Per-track clips and scenes: design study (CLIPS)

Status: **study for review, nothing built.** Branch `docs/clips-design` from `optimist` (aab7be5), 2026-10-07.

Labels: **[M]** measured (the tool below, or a file and line), **[E]** estimate (how it was made is said), **[P]**
proposal (a choice for the user to accept or change). The open questions are gathered in section 12.

Measurements: `sh tools/clips_measure/run.sh` (host only, no hardware). It builds `tools/clips_measure/clips_measure.c`
against the real `project.c`, `sec_codec.c` and `sec_log.c` (a copy whose `SLG_IDS` is raised; the firmware is not
touched), runs a prototype clip codec on four projects and fills the real log on a simulated NOR. Its output on
aab7be5 is quoted below.

## 0. Summary

- **Model [P].** Each of the 4 tracks has **16 clip slots**. A clip = the steps up to its LEN + LEN, DIV, SWING, GATE
  (the PATTERN page) + **its motion recording** (up to 64 events) + whatever per-step data comes later (chance already
  lives in the step's flags). A **scene** (today's section A..P) = the sounds, mix and globals it keeps today + **one
  clip reference per track**. The song chains scenes, unchanged.
- **Today's behaviour is the special case.** Storing scene X (SAVE + key, as today) writes each track's pattern as a
  clip and the scene refers to those. A track still playing an unedited clip is *referenced*, not copied: scenes share
  clips without the user doing anything.
- **Storage [M].** Clips live in the existing 32 KiB section log as their own records (ids 24..87). A per-step mask
  codec makes the records small: the demo project's scene + its 4 clips take 480 B where its section takes 714 B;
  a busy 64-step project 1,270 B against 2,264 B. **Today's log holds only 6 busy 64-step sections; with clips 16
  busy scenes with their own clips fit, plus 61 busy clips beside 16 scenes.** No growth of the log is needed.
- **RAM [M/E].** No new pattern buffers: the scene stage that already exists (`sec_stage_p`, a 3,640 B project in the
  pool) is the queue for per-track clips too. CLIPS adds about 1.0-1.5 KB of RAM and 0.8 KB of pool [E from measured
  struct sizes]; every build from phase 0 on adds 640 B for the log's larger index [M].
- **Sequencer.** A per-track origin makes a clip start at its step 1 on the bar it was launched; launch at the
  **next bar** (default), at the **clip's end** (OCT- held) or **now, keeping the position** (OCT+ held). The audio
  ISR copies 640 B and a few values at a switch; nothing per sample; the sequencer stays in XIP (no RAM code).
- **Device [P].** **LFO held = the CLIP layer**: the 16 white keys are the selected track's 16 clips (the screen is a
  4 x 16 session grid whose columns are the keys), black keys pick the track and hold the STORE / COPY / CLEAR
  modifiers, KNOB 1..4 cue each track's next clip. SAVE stays the scene layer exactly as today. LFO + HOME locks the
  grid open: that is the session screen.
- **No project format bump.** `project_t` stays FUNB; what changes is the log (scene flag, clip ids), the snapshot
  stream (version 2), the backup (one object `CLP1`) and the editor protocol (v9, commands 58..64).
- **Builder.** `CLIPS` (Sequencer group), needs SECTIONS 8/16; est. **+9 to +13 KB flash**; off = sections exactly as
  today. A **phase 0** in every build (the log keeps clip records; a non-CLIPS build plays scenes flattened) costs
  ~1 KB flash + 640 B RAM [E/M].

## 1. What exists today (the facts the design rests on)

| Thing | Where | Facts [M] |
|---|---|---|
| Pattern | `core.h` `track_t.step[64]` / `dstep[64]` (union), 10 B a step | 640 B a track, resident; `track_t` 1,680 B, `trk[]` 6,720 B |
| Pattern values | `P_SLEN P_SDIV P_SSWING P_SGATE` (params.c:501 "PATTERN" page) | stored with the track's 69 values |
| Step index | `idx = abs % trk_len(t)` (seq.c:1540, also :322 :351, motion.c:220, macro_ui.c:16) | `abs` counts the track's grid from PLAY: tracks of different LEN already loop independently |
| Section | `sec_codec.c`: a whole `project_t` (3,640 B) + drum record (236 B), compressed | demo 698 B, typical 504 B, busy 64-step 2,264 B, dense 2,712 B; raw worst 3,877 B |
| Section log | `sec_log.c`: 8 x 4 KiB at 0x97000, ids 0..15 sections, 16 song, `SLG_IDS 24` | records never span sectors (4,080 B a sector), one spare sector, reserve = one record of `SEC_REC_MAX` 4,059 B |
| Live jump | `section_cue` decodes into `sec_stage_p` (pool); the ISR's `arrangement_apply` → `proj_apply` on the bar | stage = a full `project_t`: four tracks' steps |
| Pending arena | `sec_pend` 8 KiB `.noinit`: sections stored while playing, written when quiet | (a flash write stops the audio) |
| Motion | `motion.c`: 64 events {track<<6\|step, param, value} for the 4 tracks together, 3 B each | `motion_store_t` 200 B; one per project buffer (`motion_proj.c`) |
| Song | `arranger.h`: 64 parts {scene, bars} | settings record + log id 16 (`SNG1`) |
| Undo | `undo.c`: per-track pattern diffs (steps, LEN, DIV) in the free pool/RAM | cleared by `proj_apply` |
| Sequencer code | `events_block` runs from XIP (fx.c:813 `FAR(events_block)`) | its size does not count against RAMTEXT, but the unity build's code generation moves RAMTEXT by tens of bytes (snapshots: +68 B on everything-that-fits) |

Budget (docs/BUILDER.md, 2026-10-07) and what is left [M, from that table]:

| Profile | App free (of 581,564) | RAM free (of 98,304) | Pool free (of 335,872) | RAMTEXT free (of 32,512) |
|---|---|---|---|---|
| user-default | 13,228 | 23,932 | 29,012 | 3,204 |
| fm-va-studio | 20,380 | 8,700 | 14,192 | 5,832 |
| drum-machine | 18,276 | 12,616 | 4,844 | 8,800 |
| everything-that-fits | 13,704 | 20,572 | 8,532 | **20** |
| x0x-drums | 41,588 | 28,528 | 42,388 | 8,644 |

(The pool and RAM "free" are what the undo ring takes today; CLIPS' RAM shortens the undo history, never below
`UNDO_MIN`.)

## 2. Data model and formats

### 2.1 The clip [P]

A clip belongs to one track (a drum clip plays only on the drum track; a synth clip on any synth track: copy
between T1..T3 is allowed). It holds:

- LEN, DIV, SWING, GATE (the PATTERN page's four values): one byte each;
- the steps 1..LEN (`step_t` / `dstep_t` as they are: chance, levels, ratchets, slides travel inside);
- its motion: up to 64 events {step, param, value} (3 B each) and its PLAY bit;
- room for what comes later: an optional chunk list (tag, length, bytes) for per-step data that `step_t` has no
  byte for (micro timing; SLOOP 2.4's locks if they are not motion events; section 9).

Not in a clip: the sound (engine, preset, every other track value), the mix, the key/scale/transpose, the arp. Those
belong to the scene, as they belong to a section today. A clip launched on a track plays with the sound the track has.

Record (a log record, id = 24 + 16 x track + slot):

| Bytes | Field |
|---|---|
| 1 | flags: bit 0 motion chunk, bit 1 codec B, bit 2 drum, bit 3 motion PLAY on, bit 4 extension chunks, bits 5..7 version (1) |
| 4 | LEN, DIV, SWING, GATE |
| 1 + 3n | motion (flag bit 0): n, then n x (step, param, value) |
| ceil(LEN/8) | bitmap of the non-empty steps (an empty step: synth REST, drum no lane, as `sec_codec.c`) |
| per non-empty step | codec A: its 10 bytes; codec B: a 2-byte mask of its non-zero bytes, then those bytes (the encoder keeps the smaller of A and B for the whole clip) |
| (bit 4) | chunks: tag, length, bytes |

Sizes [M] (bytes, without the log's 16-byte record head):

| Project | Track: LEN | codec A | codec B | kept (AB) | + 16 events | + 64 events |
|---|---|---|---|---|---|---|
| power-on | any: 16 | 7 | 7 | 7 | 56 | 200 |
| demo (acid, chords, lead, drums) | T1: 16 | 137 | 79 | 79 | 128 | 272 |
| | T2: 32 (held chords) | 289 | 103 | 103 | 152 | 296 |
| | T3: 12 | 57 | 33 | 33 | 82 | 226 |
| | DR: 16 | 87 | 39 | 39 | 88 | 232 |
| typical (sec_codec_test) | T1..T3: 16 | 87 | 47 | 47 | 96 | 240 |
| | DR: 16 | 167 | 55 | 55 | 104 | 248 |
| busy (64 steps everywhere) | T1 16ths bass | 653 | 411 | 411 | 460 | 604 |
| | T2 held chords | 493 | 197 | 197 | 246 | 390 |
| | T3 8ths lead | 333 | 173 | 173 | 222 | 366 |
| | DR full kit | 653 | 263 | 263 | 312 | 456 |
| dense (64 random full steps) | any | 653 | 781 | 653 | 702 | **846 = the worst clip** |

Codec B round-trips on every case [M]. The worst clip, 846 B, is a fifth of a log record's limit.

### 2.2 The scene [P]

A scene is today's section record with a new flag `SEC_SCN` (0x10 in the flags byte): the body as `sec_body` writes
it **without the steps** (no step bitmap, no steps; LEN DIV SWG GATE left out of the track values: they are the
clip's), followed by **4 clip references** (slot 0..15 of that track; 0xFF = none: the track is empty in this scene).
Motion is not in a scene (it is the clips').

Sizes [M]: power-on 66 B, demo 146 B, typical 100 B, busy 146 B. The bound with every value moved, three FM6 voices
and a drum record is **1,349 B** (computed from the body's layout): a scene never needs the raw fallback.

| Project | Section today | Scene + its 4 clips (with the 5 log heads) |
|---|---|---|
| demo | 714 B | 480 B |
| typical | 520 B | 376 B |
| busy 64-step | 2,280 B | 1,270 B |
| dense worst | 2,728 B | 2,784 B |

### 2.3 Clip count per track: 16 [P]

| | 16 a track (64 clips) | 32 a track (128 clips) |
|---|---|---|
| Log index RAM (every build, today's 10 B an id) [M] | 932 B (+640) | 1,572 B (+1,280) |
| Index with a compact layout (8 B an id: offsets as u16) [E] | ~720 B | ~1,230 B |
| `slg_model_t` on the stack (x3 in `sm_more`) [M] | 780 B (2.3 KB) | 1,292 B (3.9 KB; the user stack is 24,320 B) |
| Typical 16-step clips beside 16 scenes, 32 KiB log [M] | all 64 (live 6,848 B, 27 %) | all 128 (11,072 B, 44 %) |
| Busy 64-step clips (16 events) beside 16 busy scenes, 32 KiB [M] | 61 of 64 | 61 of 128 |
| Same at 48 KiB [M] | 64 of 64 | 109 of 128 |
| Keys | one white key a clip, no bank | a bank gesture (OCT) on top |
| The special case | scene X's own clip = slot X on each track: the grid reads like Ableton's | no such match |

**16**: with busy material the log, not the slot count, runs out first (61 busy clips at 32 KiB whatever the
count), and 16 matches the keys and the 16 scenes. 32 only pays with a bigger log (section 3).

### 2.4 IDs, references, sharing, garbage [P]

- **Log ids**: 0..15 scenes (as sections today), 16 the song, 17 the clip state of the working project (each track's
  clip and modified bit, 8 B, written with the autosave), 18..23 spare, **24..87 the clips** (24 + 16 track + slot).
  `SLG_IDS` 24 -> 88.
- **References**: a scene names a slot per track. A RAM table `scene_ref[16][4]` (64 B) is filled at boot from the
  scene records' last 4 bytes; a clip's reference count is a scan of that table (64 entries), never stored.
- **The working pattern** of each track remembers its source: `clip_cur[k]` (slot, or none) and a **modified** bit,
  set by every pattern edit (every edit already calls `undo_mark`: the hook is there), a motion event, or a change
  of LEN / DIV / SWING / GATE.
- **Storing scene X** (SAVE + key): for each track, unmodified with a source -> refer to it (sharing, no write);
  modified (or no source) -> write the working pattern into a slot: its source when no other scene refers to it,
  else a new slot (slot X if free, else the lowest free); no free slot -> nothing is stored, "T2: NO FREE CLIP".
  This is copy-on-write: a stored scene never changes when another is stored, as sections behave today.
- **Storing into a clip** (CLIP layer, section 6): the working pattern overwrites slot n; every scene that refers to
  n plays the new content (the explicit, linked edit). "AGAIN:" confirms over a used slot, as today's section store.
- **Garbage**: clips are slots the user sees; none is deleted behind their back. A clip no scene refers to stays
  until cleared (the CLIP layer's CLEAR, or SAVE > TOOLS > CLEAN: every unreferenced clip, with a count and a
  confirm). MEM FULL says "CLEAN: n UNUSED" when that would help.
- **Order of writes** (power-cut safety, as sec_log's rules): a scene store writes its new clips first and the scene
  record last (the commit). A cut leaves at most a written clip no scene refers to yet (shown, cleanable), or, when
  the store overwrote its own source slot, scene X with its new pattern under its old sound; never a scene naming
  a missing clip (a missing clip reads as an empty track and the MISSING line says so).

### 2.5 Motion into clips [P]

Motion moves from the project to the clip it was recorded on: an event's step and parameter stay, its track is the
clip's. **Per clip: up to 64 events** (what a whole project holds today).

The working store becomes per track: four lists of up to 64 (`motion_ev_t` 3 B: 4 x 194 B = 776 B against 200 B
today, +576 B RAM [M from the struct sizes]). `motion_step` scans only its own track's list (at most 64, as today's
scan of the shared 64). A clip switch restores the track's patch under its old motion (`motion_restore`, as at PLAY)
and takes the new list.

Non-CLIPS builds keep today's 64-shared store: a flattened scene (section 2.7) keeps the first 64 events in track
order and the MISSING line says "MOTION CUT".

### 2.6 Format: no FUNC [P]

`project_t` (FUNB) is unchanged: the working project, the autosave and a section record keep their layout. What
changes:

| Object | Change | Older firmware |
|---|---|---|
| Section log | flag `SEC_SCN` on records 0..15; ids 24..87 clip records; id 17 clip state | phase 0 builds: read all (section 2.7); pre-phase-0 builds: see section 11, R3 |
| Snapshot stream | version 2: kind 5 `SNR_CLIP` (id 0..63), kind 6 `SNR_CSTATE`; `SNR_SEC` may hold a scene | `snapshots.c:346` accepts `ver >= 1` and skips unknown kinds: phase 0 is needed to read v2 |
| Backup | object `CLP1` (kind 7): every clip record (id, length, bytes); restored before S01..S16; BK_LIST switches bit 8 "log with clip ids" (phase 0), bit 9 "CLIPS" | an editor without CLP1 restores scenes without their clips: they play empty tracks (MISSING says so) |
| Editor protocol | v9: 58..64 (section 7.3) | INFO < 9: the editor shows sections as today |

A FUNC is kept in reserve for a day `project_t` itself must grow; clips take the per-pattern growth instead (their
records are versioned and have chunks), so the "FUN8/FUNB is full" pressure on `step_t` stops being a project-format
problem.

### 2.7 Migration

- **FUNB sections -> scenes (first start of a CLIPS build)** [P]: each legacy section X becomes 4 clip records in
  slot X of each track (only non-empty patterns; an empty track refers to none) and then the scene record replaces
  the section (the commit). One section at a time; cut anywhere, the next start goes on (a section record still
  there is converted again; its clips are rewritten in the same slots). No deduplication here: "your old section C's
  drums are drum clip 3". Space: every measured project shrinks (demo 714 -> 480 B), so the conversion fits any log
  that held the sections, the reserve covers the peak (one section's clips before its old record dies).
  The motion of section X goes into its clips by track (each track's events, at most 64 a clip: no loss).
- **The song** (`SNG1`, settings record): unchanged, it names scenes.
- **Snapshots** v1 (sections) load into a CLIPS build through the same conversion; v2 into a phase 0 non-CLIPS build
  flattened.
- **Backups** S01..S16 from today: legacy section records, converted at the next start like any.
- **The autosave** (FUNB): unchanged; with no id-17 record every track's source is "none, modified" (storing a
  scene writes its clips).
- **CLIPS -> non-CLIPS (a phase 0 build)**: **flattened on read**, nothing rewritten. A scene is decoded, its 4 clips
  are read and put into the `project_t` (`sec_read` assembles; the stage and the load use it): it plays as the
  section it would be. Storing a section there writes a legacy section record over the scene (its clips stay in the
  log, unreferenced). Back on a CLIPS build that section converts again; every scene not stored on the other build
  is as it was. MISSING-style line once: "CLIPS: PLAYED AS SECTIONS".
- **Non-CLIPS -> CLIPS**: the first-start conversion above; lossless.

## 3. Storage

### 3.1 Where [P]: the section log, unchanged in size

Clip records are records of the existing log (`sec_log.c`), with its rules: never spanning a sector, the newest of
an id wins, compaction into the spare sector, tombstones for a clear, the pending arena while playing.

The reserve changes from "one record of 4,059 B" to "**a scene of 1,349 B and four clips of 846 B**", each its own
record (the playing scene can always be stored with four new clips). Modelled on the real `sm_*` code [M]; it costs
about as much as today's reserve.

### 3.2 How many fit [M] (the real log, 16-byte heads, the reserve kept after every store)

| Content | 32 KiB (8 sectors, today) | 48 KiB (12) | 64 KiB (16) |
|---|---|---|---|
| **today**: typical sections 504 B | 16 of 16 | 16 | 16 |
| **today**: demo sections 698 B | 16 of 16 | 16 | 16 |
| **today**: busy 64-step sections 2,264 B | **6 of 16** | 10 | 14 |
| sections with codec B steps, no clips (busy ~1,202 B [E: scene + its clips]) | 16 of 16 | 16 | 16 |
| clips: demo scenes, each its own 4 clips | 16 of 16 | 16 | 16 |
| clips: busy scenes, each its own 4 clips | 16 of 16 | 16 | 16 |
| clips: busy scenes, own clips with 16 motion events each | 15 of 16 | 16 | 16 |
| 16 demo scenes + every slot a typical clip | 64 of 64 (27 % used) | 64 | 64 |
| 16 busy scenes + every slot a busy clip (16 events) | 61 of 64 | 64 | 64 |
| 16 busy scenes + worst-case clips (846 B) | 20 of 64 | 36 | 52 |
| a shared song: 16 busy scenes over 22 busy clips | 39 % used | 24 % | 18 % |

Reading: the sector-packing of large section records is what limits today's log (a 2,264 B section leaves 1.8 KB
of each sector unused). Small clip records pack sectors well. **The 32 KiB log is enough for clips**; the number that
can run out is busy clips beside 16 busy scenes (61).

### 3.3 Grow the log? [P: no, keep 32 KiB]

| Option | Gains | Costs |
|---|---|---|
| 32 KiB (today) | nothing to move; snapshots unchanged (the largest a full log makes stays under 29.3 KB, docs/BUILDER.md) | 61 busy clips + 16 busy scenes, 20 worst-case clips |
| 48 KiB: +16 KiB from USR3 (as snapshots did) | all 64 busy clips; 32/track becomes useful (109 busy) | USR3's sample room 32 -> 16 KiB with SNAPSHOTS 4; the log is no longer contiguous (0x97000 and 0xC8000..): `slg_off` needs a sector map (~100 B flash [E]); a full log's snapshot can pass 32 KiB: SNAPSHOT FULL on the largest states |
| 64 KiB | 52 worst-case clips | USR3 has no sample room left with SNAPSHOTS 4 |

Independent of clips, **codec B for today's sections** (a flag in `sec_codec.c`) takes busy sections from 2,264 to
~1,202 B and the log from 6 to 16 busy sections [M on the estimate]. It is phase 0b: worth doing for every build.

## 4. RAM

### 4.1 What is resident [P]

- The playing patterns: `trk[k].step` as today (no change, nothing moves to pointers).
- The queued clips: **the scene stage that exists** (`sec_stage_p`, 3,640 B, pool; in every SECTIONS 8/16 build).
  Its `t[k]` holds track k's queued clip (steps + the four values). A scene queued fills all four; a clip queued
  later replaces its track in the staged scene ("scene C with drums 5 at the next bar"); a scene queued after clips
  drops them (the scene wins). The ISR copies a staged track into `trk[k]` at its boundary: **double buffering
  without a second buffer**, glitch-free (the copy is 640 B inside the ISR, between two blocks).
- The stage's motion: 4 lists of 64 (776 B) beside it.
- In song mode the stage holds the song's next part (`sec_service`): clip launches are refused there, as live jumps
  are today ("SONG PLAYS") [P, question Q6].

### 4.2 Sizes [M structs / E totals]

| Item | Bytes | Where | Builds |
|---|---|---|---|
| log index `slg`, 24 -> 88 ids | 292 -> 932 (+640) [M] | RAM | every build from phase 0 |
| pending arena's per-id tables (`len`, `off`), 16 -> 88 ids | +288 [E: 4 B an id] | .noinit | every build from phase 0 |
| `slg_model_t` (stack, x3 in `sm_more`) | 268 -> 780 [M] | stack | every build from phase 0 |
| clip state: `clip_cur`, `clip_req`, `when`, modified, origin a track | 4 x 8 = 32 [M] | RAM | CLIPS |
| `scene_ref[16][4]` | 64 [M] | RAM | CLIPS |
| working motion per track (MOTION) | 200 -> 776 (+576) [M] | RAM | CLIPS + MOTION |
| stage motion per track | 776 [M] | pool | CLIPS + MOTION |
| clip stages | **0** (the scene stage) | | |
| **total, CLIPS + MOTION, phase 0 included** | RAM ~1.3 KB, pool ~0.8 KB, noinit ~0.3 KB [E] | | |

Per profile [E]: every profile keeps at least 7 KB of RAM and 4 KB of pool free (fm-va-studio: 8,700 -> ~7.4 KB
RAM; drum-machine: 4,844 -> ~4.0 KB pool). Nothing per profile is close; **RAMTEXT is** (section 4.3).

### 4.3 RAM code

No clip function runs from RAM: the sequencer (`events_block`, `seq_tick`, `motion_step`) is XIP (fx.c:813). The
risk is indirect: the unity build's code generation moves the audio path's RAM code when anything else changes
(SNAPSHOTS: +68 B on everything-that-fits, which has **20 B** left; MOTION's own +60 B). Rules for the build:
the clip code is `noinline` XIP functions called from `events_block`; no AINL helper of the audio path changes
signature; the phase 2 gate builds everything-that-fits with CLIPS=1 and compares `.ram_hot`. If it does not fit,
CLIPS stays off in that profile (section 10.3).

## 5. Sequencer

### 5.1 Per-track position

Today `idx = abs % len` from PLAY. A clip launched on a bar must start at its step 1 there (a 32-step clip launched
on an odd bar would otherwise start in its middle). Each track gets an origin: `idx = (abs - org) % len`, `org` set
to the track's `abs` at the switch (0 at PLAY and at a scene launch, which resets every track as today). Five places
compute `idx` (seq.c:322, :351, :1540, motion.c:220, macro_ui.c:16): one helper `trk_idx(t, abs)`. A bar boundary is
an even step on every division (4 den: 4, 8, 16, 32, 12, 24 a bar), so swing keeps its phase.

### 5.2 Launch timing [P]

| When | Gesture | Starts | Use |
|---|---|---|---|
| **next bar** (default) | the key | its step 1 on the next bar (4/4, as live jumps: `live_block`'s bar) | the normal launch |
| **clip end** | OCT- held | its step 1 when the playing clip wraps | odd lengths, X0X-style "next pattern"; up to LEN steps away |
| **now, legato** | OCT+ held | the next step, at the same position (`org` kept: `idx = (abs - org) % newlen`) | fills, Ableton's legato |
| stopped | the key | loaded at once (as `section_load`) | building |

A launch is staged by the main loop (log read + decode: [E] well under a millisecond for an 846 B record, against
the 42,606 emulator instructions measured for a 524 B *section* decode); the ISR applies it only when staged
(`sec_stage_id`'s protocol, a bit per track), else it waits for the next boundary of the same kind (a "now" launch
then lands a step later).

### 5.3 Independent loops

Nothing new: each track wraps at its own LEN on its own DIV (already the case). A clip brings its LEN and DIV; its
origin keeps it on the bar it started.

### 5.4 Motion, REC, undo, the song, the CPU guard

- **Motion**: at a switch, `motion_restore(t)` (the patch back), then the new clip's list; recording knob moves
  write into the working clip's list (modified).
- **REC**: a take records into the working pattern (modified). A launch on a recording track ends that track's take
  at the switch (today: "a take does not run on into another section", seq.c:1283); the other tracks keep recording.
- **Undo**: a clip switch is an undo level of that track (its pattern before the switch, an `undo_mark` in the ISR
  as recording passes do): EDIT + OCT- brings an edited pattern back after an accidental launch [P]. A scene launch
  clears the history as `proj_apply` does today.
- **Song (arranger)**: unchanged, it chains scenes; `sec_read` assembles scene + clips into the stage; in song mode
  per-track launches are refused (Q6). SONG REC records scene launches only.
- **CPU guard**: a clip launch changes no engine and no voice count: nothing for the guard. A scene launch is
  today's section apply.

### 5.5 ISR cost [E]

Per block with nothing queued: one test of a 4-bit mask (a few cycles). Per step: one subtraction in `trk_idx`.
At a switch: `memcpy` of 640 B + 4 values + a motion list (<= 194 B) + `motion_restore` (75 bits): ~1-2 k cycles once,
less than today's section apply (`proj_apply` clamps 4 x 75 values through descriptors). Gate (phase 2): bench.c
scenario on the emulator, the block's instruction count at a switch and without.

## 6. Device UI

### 6.1 Principles kept

Colour carries meaning only (tools/colors.json): **green** `ok` = playing, **amber** `warn` = queued (about to
change), **red** `err` = recording / will be overwritten, engine colours = which engine a clip plays on (a thin top
band), grey = stored, dark = empty. A layer is a held button; tapped, the button opens its pages as before; held +
HOME locks it open (ui_input.c:806).

### 6.2 The CLIP layer: LFO held [P]

LFO is the only page button that is not a layer today (tap opens the LFO pages; ENV is a layer only on FM6). Held,
it becomes "clips". Its keys, knobs and screen:

| Control | Does |
|---|---|
| white key n | launch clip n of the selected track (next bar); an empty slot: the track stops at the next bar |
| OCT- held + white n | launch at the playing clip's end |
| OCT+ held + white n | launch now, legato |
| black keys 1..4 (F#3 G#3 A#3 C#4, the SEQ layer's page keys) | select track T1, T2, T3, DR (the layer stays; the same as ALGORITHM) |
| black 5 (D#4) | stop the selected track at the next bar |
| black 6 (F#4) held + white n | **STORE** the working pattern into clip n ("AGAIN: 5" within 3 s over a used slot) |
| black 7 (G#4) held + white a, then white b | **COPY** clip a to b (confirm over a used b; a synth clip to another synth track: select it between the two keys) |
| black 8 (A#4) held + white n | **CLEAR** clip n (confirm; "USED BY A C" names the scenes that refer to it, they will play the track empty) |
| black 9 (C#5) | **DUPLICATE** the playing clip into the first free slot and queue it (the variation workflow) |
| black 10 (D#5) held + white n | launch **scene** n (A..P without banks; the SAVE layer keeps its banks) |
| black 11 (F#5) | free (follow actions later, section 8) |
| KNOB 1..4 | track k's next stored clip (turn: cue the previous / next used slot at the next bar) |
| REC, PLAY | as everywhere (REC is not eaten: recording into the working clip goes on) |

Screen while held (the columns are the white keys, left to right; the selected track's row is the one the keys play):

```
 +------------------------------------------------------+
 | clips  T2 DIGITAL                  next bar  mem 41% |   title: layer, track + engine, launch mode, gauge
 |       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 |
 |  T1  [#][=][=][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   [#] green: playing   [=] grey: stored
 | >T2  [=][#][~][=][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   [~] amber band: queued   [ ] dark: empty
 |  T3  [#][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   [*] a playing clip with unsaved edits
 |  DR  [=][*][=][=][=][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   (each cell's top band: the engine / kit colour)
 |  scn  A  B  C  D  e  f  .  .  .  .  .  .  .  .  .  . |   scenes stored (upper case), the playing one green
 |------------------------------------------------------|
 |  (T1 1)      (T2 2>3)     (T3 1)       (DR 2*)       |   KNOB 1..4: each track's clip, "> 3" when queued
 +------------------------------------------------------+
```

Layout [E]: title 24 px, column numbers 12, 4 track rows of 28 px (cells 13 x 24), the scene strip 16, dials 56:
232 px of 240. Drawn in bands (the 124-row canvas limit, ui_song.c:37).

**Locked (LFO + HOME): the session screen.** The same grid, both hands free; any page button unlocks (today's
rule). No separate page or menu is needed.

### 6.3 The SAVE layer (scenes): as today [P]

Unchanged keys: white 1-4 launch the bank's scenes, 5-8 store (with the "AGAIN:" confirm), 13 loop / song, 14 SONG
REC, 16 the song page, SAVE + OCT-/OCT+ the bank. Changes:

- store = section 2.4's rules (clips auto-created or referenced);
- a scene tile whose tracks no longer all play its clips shows a dot ("B•": scene B, changed since);
- the MEM gauge counts clips too ("mem 41% +9": +n stays "scenes of the last stored size, with their new clips");
- white 15 (free today) opens the CLIP layer locked (the session screen from the scene layer).

```
 +------------------------------------------------------+
 | song   mem 41% +9                                    |
 |  [ A ][ B•][ C ][ D ]  [save A][save B][save C][...] |   B playing (green) but changed (dot)
 |  ...                         [loop][rec][grid][chain]|   grid = white 15: the session (locked CLIP layer)
 +------------------------------------------------------+
```

### 6.4 Gestures that conflict, and how they are resolved

| Today | With CLIPS | Resolution |
|---|---|---|
| LFO tap opens the LFO pages | LFO hold = clips | taps still open the pages (`layer_tap`); a hold under 140 ms shows nothing (`SHOW_MS`) |
| two layer buttons held: the first in `LY_*` order wins (seq.c:84) | EDIT is not used inside the clip layer | the modifiers are black keys, not buttons: no priority question |
| SAVE layer queued scene: a **white** top band (ui_layers.c:845) | queued clip: an **amber** band | [P] make both amber (status colour); or keep white for scenes (question Q9) |
| SAVE layer white 15 unused | the session | none |
| SEQ layer knobs DIV SWING LENGTH | now the clip's values | they mark the clip modified |
| X0X UI port: PRESETS cues a pattern | — | that UI calls the same clip API (section 8) |

### 6.5 Messages

"NEXT: T2 5", "T2 5 AT END", "T2 5 NOW", "STORED T2 5", "AGAIN: T2 5", "CLEARED T2 5", "USED BY A C", "T2: NO
FREE CLIP", "MEM FULL", "CLEAN: 7 UNUSED", "SONG PLAYS", "CLIPS: PLAYED AS SECTIONS" (non-CLIPS build).

## 7. Web editor (web/editor.html)

### 7.1 Session grid [P]

On the Mixer screen (the home), above the strips: one column per track (the strip's engine colour as its header
band, as the strips have), 16 rows of clip cells, a scene column on the right (A..P). Rows = slots, so in the
special case scene row X refers to row X across: the grid reads as Ableton's.

```
 Optimist  (o) [>][#] B*  120 bpm                 Mixer  Library  Samples  Projects  Snapshots  Settings
 +--------+------------+------------+------------+------------+---------+
 | slot   | T1 ANALOG  | T2 DIGITAL | T3 FM6     | DRUMS 909  | scene   |
 +--------+------------+------------+------------+------------+---------+
 |  1     | [>] 16 ''' | [ ] 32 ' ' | [>] 12 '.' | [ ] 16 ::: | [>] A   |   [>] green: playing
 |  2     | [ ] 16 ''. | [>] 32 '  '| [ ] 16 '' | [>]*16 ::.| [ ] B   |   * edited, not stored
 |  3     | [~] 64 ''''| [ ]        | [ ]        | [ ] 16 ::: | [ ] C   |   [~] amber, pulsing: queued
 |  4     | [ ]        | [ ]        | [ ]        | [ ] 16 ..: | [ ] D   |   ''' a mini step map
 | ...    |            |            |            |            |         |
 +--------+------------+------------+------------+------------+---------+
 hover a scene: its four clips outlined; click: launch at the next bar (shift: now, alt: at the clip's end)
 drag a clip onto another slot: copy (confirm over a used one); Delete: clear (confirm, names the scenes using it)
 double-click a clip: the piano roll / drum grid popup on it (below)
 [mixer strips, unchanged]
```

### 7.2 Editing a clip

- **The playing clip** (or launched now when stopped): the existing popup edits the working pattern through
  TRACK_STEP / DRUM_STEP exactly as today; the header says "T2 clip 3 *" and gains **Store clip** (= CLIP_OP store)
  and **Store as new** (first free slot).
- **Any other clip** (phase 4b): CLIP_READ into the page, the same roll / grid edits a local copy, **Save** writes it
  with CLIP_WRITE (stopped: at once; playing: into the pending arena, as a store while playing). The record codec
  is 60-80 lines of JS mirroring section 2.1.

### 7.3 Protocol v9 [P]

| cmd | Request | Reply |
|---|---|---|
| 58 CLIP_LIST | op 0 (all) / 1 (state only) | op; 1: per track cur, req, when, modified (16 B). 0: + per track x 16 slots: flags (used, motion, drum), LEN; + 16 scenes x 4 refs (127 none) and the used mask |
| 59 CLIP_LAUNCH | track, slot (127 stop), when (0 bar, 1 end, 2 now) | track, slot, rc (0 ok, 1 args, 2 empty, 3 song plays) |
| 60 SCENE | op (0 launch, 1 store, 2 clear), scene, when | op, scene, rc (store's rc: 4 MEM FULL, 5 no free clip, 6 confirm needed: send again within 3 s) |
| 61 CLIP_OP | op (0 store working -> slot, 1 copy, 2 clear, 3 duplicate, 4 clean unused), track, a, b (track2 for copy) | op, rc, the slot written |
| 62 CLIP_READ | track, slot, offset (2 x 7 bit) | track, slot, offset, total, pack7 bytes (<= 256) |
| 63 CLIP_WRITE | track, slot, offset, total, pack7 bytes; the last chunk commits | track, slot, offset, rc |
| 64 CLIP_CHANGED (push) | — | track, cur, req, when, modified (while WATCH bit 2 is on) |

Versioning and fallback: INFO's protocol version 9 and BK_LIST's switches bit 9 say CLIPS; bit 8 says the log keeps
clip ids (phase 0). Without them the editor shows the sections as today (the Projects tab, STATUS's section). An
older editor on a CLIPS device sees scenes as sections (STATUS, PROJECT) and keeps working; its backups lack `CLP1`
(the restore report names it).

## 8. Pluggable UI: X0X and Felucca (docs/UI-PORTS.md, branch docs/ui-ports)

UI-PORTS ruled per-part pattern banks out as RAM (16 x 640 B a track against ~3 KB free) and mapped X0X's patterns
onto whole-project sections (lossy: a pattern change changes the sound). Clips change both verdicts:

| X0X concept | With clips |
|---|---|
| 16 patterns per part | **direct**: 16 clips a track, in flash; RAM ~0.8 KB for the stage's motion, not 40 KB |
| PRESETS cues the next pattern of this part | `clip_launch(trk, slot, WHEN_END)`: X0X switches at the pattern's end |
| HOME: white key cues a pattern | the same call; "two held: a chain" = a per-track chain (not in this design: a follow-action list a clip, ~20 B a track, later) |
| 303A / 303B, 909 + 808 parts | tracks 1-2 (ACID), the drum track: the two X0X drum parts share one drum clip a bar (lossy, as UI-PORTS says) |
| Song (192 bars, per-part patterns + mutes) | the song chains scenes; each distinct combination of parts' patterns becomes a scene (<= 16; past that, lossy) |
| SAVE + white key: copy the pattern there | CLIP_OP copy |

| Felucca concept | With clips |
|---|---|
| one pattern a track, SEQ > PATTERN page | the working clip; a "PATTERN n" row on that page = launch / store clip n |
| SONG chain of 4 project slots | scenes A..P in the chain (as UI-PORTS says) |
| PHRASES (patterns from user presets) | load into the working clip, then store into a slot |

The clip operations are core commands (UI-PORTS step 2a: "step and pattern commands under the IRQ rule"):
`clip_launch`, `clip_store`, `clip_copy`, `clip_clear`, `clip_dup`, `scene_launch`, `scene_store`, `clip_state(trk)`
in the core, called by Optimist's layers, an X0X UI, a Felucca UI and the editor alike.

## 9. SLOOP 2.4 compatibility risk

What is known (memory notes, 2026-10-06; source not public): 2.4 announces **parameter locks** (hold a step, turn a
knob), **micro timing**, **section chaining** (hold SAVE, tap A B B C), fills, steps up to 2 bars.

| 2.4 feature | Our place for it | Risk |
|---|---|---|
| Section chaining gesture | the song chain (64 parts) + a gesture on the SAVE layer | none for the data |
| Parameter locks | clip motion events {step, param, value} (the user's view: p-locks = motion with another UI), 64 a clip | if 2.4 stores more than 64 a pattern, or values wider than a byte: a clip chunk (tag "PLK1") |
| Micro timing | a clip chunk ("MTM1": a signed byte a step, 64 B) | none: `step_t` and `project_t` stay as they are |
| Steps up to 2 bars | our LEN 64 (4 bars at 1/16) covers it | none |
| Their project format | today's importer (`proj_import`: FUN1..FUNB) converts their section into a scene + its own clips (the migration path) | if 2.4 changes its step layout, the importer maps it; its locks / timing go to the chunks |

Rule kept from the memory note: compare their format first and adopt it where it differs. Clips make that cheap:
their per-pattern data lands in a versioned clip record, not in `project_t`.

## 10. Builder

### 10.1 The item [P]

`CLIPS` in the Sequencer group, values 0 / 1. Constraint (error): CLIPS needs SECTIONS 8 or 16 (the log, the stage).
With MOTION: per-track motion (section 2.5); without: clips carry no motion (records written by a MOTION build keep
their chunk, as sections do today: "every build reads a chunk").

| Interaction | Behaviour |
|---|---|
| SECTIONS 4 | CLIPS refused (error) |
| SECTIONS 8 | scenes A..H, 16 clips a track all the same (the ids do not depend on it) |
| MOTION 0 | clips without motion; a clip's chunk kept on rewrite |
| SNAPSHOTS | stream v2 (`SNR_CLIP`); size bound unchanged (it is the log's) |
| UNDO_HISTORY 0 | the switch is the single level |
| MISSING_WARN | names a missing clip, "MOTION CUT", "CLIPS: PLAYED AS SECTIONS" |
| everything-that-fits | RAMTEXT gate (section 4.3) |

### 10.2 Costs [E now; measured after the build]

Basis: measured deltas of comparable items (costs.json, 2026-10-07): SECTIONS 16 vs 4 +6,884 B (log, codec,
sections, migration); SNAPSHOTS +8,708 B; MOTION +3,420 B (RAMTEXT +60); REC_MODES +1,576 B (a screen and knobs).

| Part | Flash [E] |
|---|---|
| **Phase 0 (every build)**: SLG_IDS 88, scene flag + clip decode in `sec_read`, flatten-on-read, MISSING line | 0.8-1.2 KB |
| clip codec (encode / decode, A and B) | 1.0-1.5 KB |
| store / reference / copy-on-write / clean, migration | 1.5-2.0 KB |
| sequencer: origin, queue, staging, ISR apply, undo level | 0.8-1.2 KB |
| motion per track | 0.5-0.8 KB |
| device UI: CLIP layer + grid, SAVE layer marks | 3.0-4.5 KB |
| editor commands 58..64 | 1.0-1.5 KB |
| snapshots v2, backup CLP1 | 0.5-1.0 KB |
| **CLIPS total (phase 0 not counted)** | **~9-13 KB** |

RAM: section 4.2 (~1.3 KB RAM, 0.8 KB pool, 0.3 KB noinit with phase 0).

### 10.3 Profile defaults [P]

| Profile | App free | CLIPS | Why |
|---|---|---|---|
| user-default | 13,228 | on if the measured cost leaves >= 2 KB; else off until something is trimmed | the main profile, but the fit is tight |
| fm-va-studio | 20,380 | on | room |
| drum-machine | 18,276 | on | patterns are the point; pool 4.0 KB left after |
| everything-that-fits | 13,704 | **off** unless the RAMTEXT gate passes and flash fits beside PARAM_HELP | 20 B of RAM code |
| x0x-drums | 41,588 | on | X0X's per-part patterns |
| default build (no .config) | | off until phase 5 | sections exactly as today |

## 11. Phased build plan

Each phase ends with the firmware working, `tests/run_tests.sh` green, every profile linking and fitting
(`tools/builder/verify.py`), and a commit the user can review. Effort in focused agent-days [E].

| Phase | Content | Gate | Effort |
|---|---|---|---|
| **0** log forward-compatible (every build) | `SLG_IDS` 88 with a compact index (u16 offsets), the arena's per-id tables, `slg_model_t` narrowed (u8 sector, u16 size); `SEC_SCN` decode + clip decode in `sec_read` (flatten on read); MISSING line; BK_LIST bit 8 | sec_log_test with clip ids and cuts at every program; a CLIPS-written log (made by the host test) read and played as sections; RAM +<= 0.7 KB; every golden unchanged | 1.5 |
| **0b** codec B for sections (every build) | a flag in `sec_codec.c`: per-step masks | sec_codec_test: busy sections ~1.2 KB, 16 busy sections fit; old records still read | 0.5 |
| **1** data model (CLIPS=1, no new UI) | clip records, scene records, store rules (2.4), migration, scene launch through the assembled stage, clip-state record, snapshots v2, backup CLP1, `clip_*` core API | a FUNB project / sections / snapshot v1 / backup behave exactly as before on the device (same keys, same sound: render compare); conversion cut anywhere resumes; round trips CLIPS <-> non-CLIPS; MEM FULL and the reserve | 3 |
| **2** sequencer | per-track origin, launch queue (bar / end / now), ISR apply from the stage, per-track motion, REC / undo rules | host sequencer tests (each mode, odd LEN / DIV mixes, swing phase, motion, a take across a switch, undo after a switch); emulator bench: ISR cost at a switch and idle; **RAMTEXT of everything-that-fits with CLIPS=1** | 2.5 |
| **3** device UI | CLIP layer on LFO, the grid, black-key modifiers, KNOB cues, SAVE layer marks and white 15, messages | ui tests (keys -> actions, every message fits 232 px), emulator screenshots for the user's review | 3 |
| **4** web editor | protocol v9, session grid, launch / copy / clear, popups on the playing clip; 4b: CLIP_READ / CLIP_WRITE offline editing | web/test_web.mjs, e2e_daw.mjs against the mock and the emulator; an old editor on a new device, a new editor on an old device | 3 (+1.5 for 4b) |
| **5** builder | the CLIPS item, `costs.json` measured, profiles set, docs (BUILDER.md, EDITOR_PROTOCOL.md, this file's [E] replaced by [M]) | verify.py on every profile and random configurations | 1 |

**Risks**

- R1 **RAMTEXT** on everything-that-fits (20 B left; code generation moves it by tens of bytes): measured at the
  phase 2 gate; fallback: CLIPS off there.
- R2 **Flash on user-default** (13.2 KB free against ~9-13 KB): measured at phase 5; fallback: drop the black-key
  DUPLICATE / scene-launch extras, or leave user-default off.
- R3 **Downgrade to a pre-phase-0 firmware**: today's `slg_scan` seals the rest of a sector at an id >= 24
  (sec_log.c:107), so an older build would lose the later records of each sector holding a clip, and its
  compaction would then drop them. Ship phase 0 first and long before CLIPS; the release notes say "back up
  before going back"; the backup keeps everything.
- R4 **Semantics**: working copy vs live clip edits (Q1) and shared clips edited explicitly; the "*" mark and the
  undo level are the safety net.
- R5 **Log capacity with busy material**: 61 busy clips beside 16 busy scenes at 32 KiB; MEM FULL + CLEAN.
- R6 **SLOOP 2.4's formats** are unknown; chunks are the escape hatch.
- R7 Older editors' backups lack `CLP1`: the restore report names it.

## 12. Open questions for the user

1. **Edits: working copy or live clip?** [P: working copy] Edits change the track's working pattern (marked "*");
   they reach a clip when you store the scene or the clip. A launch away from an edited clip drops the edits, but
   EDIT + OCT- brings them back (the switch is an undo level). The alternative (Ableton: edits go into the clip at
   once) breaks today's "store A, edit, store B" variation workflow, because A would change too.
2. **The CLIP layer on LFO (held)?** Alternatives: none of the other buttons is free as a layer.
3. **Launch timing**: next bar by default, OCT- = at the clip's end, OCT+ = now (legato). Or a device setting?
4. **16 clips a track** (recommended) or 32 with a 48 KiB log?
5. **Log size**: keep 32 KiB (recommended), or 48 KiB from USR3 (USR3 samples 32 -> 16 KiB, a full snapshot can
   pass 32 KiB)?
6. **Clip launches while the song plays** (song mode)? Recommended no, as live jumps today.
7. **A scene that leaves a track alone** (a "keep" reference: the track goes on with what it plays)? Not in this
   design; cheap to add (one value of the reference byte).
8. **Profile defaults** (section 10.3).
9. **Queued colour**: amber for both scenes and clips (status colour), or keep the scenes' white band?
10. **Phase 0b** (codec B for sections) now, for every build, before clips?
11. **SONG REC and clip launches**: record only scene launches (recommended), or turn per-track launches into new
    scenes as it records?

# Per-track patterns and scenes: design (PATTERNS)

Status: **phases 0 and 0b built** (every build, branch `feat/patterns-phase0`, 2026-10-07: section 11.1); phases 1..5
not built. The study was written as "clips" (branch `docs/clips-design`, 5917577); the user's decisions of 2026-10-07
(section 0a) renamed them **patterns** and settled the open questions.

Labels: **[M]** measured (the tool below, a test, a build, or a file and line), **[E]** estimate (how it was made is
said), **[P]** proposal (a choice for the user to accept or change), **[D]** the user's decision (section 0a).

Measurements: `sh tools/patterns_measure/run.sh` (host only, no hardware). It builds
`tools/patterns_measure/patterns_measure.c` against the real `project.c`, `sec_codec.c` and `sec_log.c` (copies whose
`SLG_IDS` is set to 24, 88 or 152; the firmware is not touched), measures section records in codec A (before phase 0b)
and codec B (the firmware since phase 0b), prototype pattern and scene records on four projects (`tests/sec_projects.h`
and the typical one), and fills the real log on a simulated NOR. Its output after phase 0b is quoted below. Flash and
RAM per profile: `tools/optimist.py build --profile P`, the branch against `optimist` (2d600b6).

## 0a. Decisions (the user, 2026-10-07)

| Question | Decision |
|---|---|
| Name | **patterns**, not clips ("clip was just to explain that it is only the notes"): the builder switch `PATTERNS`, the UI word PATTERN, this file `docs/PATTERNS-DESIGN.md` |
| What a pattern is | the notes: the steps up to LEN, LEN, DIV, SWING, GATE (section 2.1) **and its motion recording** (motion is stored with the pattern). The sound and the mix stay in the scene / section |
| Q1 Editing | edits go to a **working copy** that you **STORE** (today's workflow) |
| Q2 The layer | **LFO held** = the PATTERN layer (SEQ held stays step entry; tapping LFO still opens the LFO pages); **LFO + HOME** locks the session grid |
| Q3 Launch timing | default = **when the track's current pattern finishes** (its end). Modifiers: **OCT- = next bar, OCT+ = immediate** [P for the modifiers] |
| Q4 Count | **16 patterns per track** |
| Q5 Log size | keep **32 KiB** (coordinator's default, accepted) |
| Q6 During the song | launches **allowed**; a launch overrides that track **until the song's next part** |
| Q7 "Keep" reference | **yes**: a scene can leave a track's pattern playing |
| Q8 Profile defaults | on in **fm-va-studio, drum-machine, x0x-drums**; **user-default only if the measured cost fits**; **off** in everything-that-fits and the default build (coordinator's default, accepted) |
| Q9 Queued colour | **amber** for anything queued, scenes too (coordinator's default, accepted) |
| Q10 Phase 0b | now, every build: done (section 11.1) |
| Q11 SONG REC | recording pattern launches **later** (scene launches only for now; coordinator's default, accepted) |
| Editor session view | the 16 patterns a track appear **in the mixer channel strips** (16 slots at the top of each strip, above the instrument row: playing green, queued amber, recording red, empty dim; click launches, double-click opens the piano roll / drum grid, drag copies); the **MASTER strip carries the scene column** A..P in the same rows (a row = a scene, as Ableton's Main track). No separate session-grid screen: **the mixer is the session view** (section 7.1; nothing built yet) |
| Editor protocol | a separate agent builds protocol **v9** now (`feat/protocol-v9`) and may use commands 54+. The patterns' commands come in the **next** protocol version after v9; **no command numbers are reserved now** (section 7.3) |

Where the sections below still say [P], the decision above wins; the text has been brought in line with it.

## 0. Summary

- **Model [D].** Each of the 4 tracks has **16 pattern slots**. A pattern = the steps up to its LEN + LEN, DIV,
  SWING, GATE (the PATTERN page) + **its motion recording** (up to 64 events) + whatever per-step data comes later
  (chance already lives in the step's flags). A **scene** (today's section A..P) = the sounds, mix and globals it keeps
  today + **one pattern reference per track** (or "keep": the track goes on with what it plays). The song chains scenes,
  unchanged.
- **Today's behaviour is the special case.** Storing scene X (SAVE + key, as today) writes each track's working
  pattern into a pattern slot and the scene refers to those. A track still playing an unedited stored pattern is
  *referenced*, not copied: scenes share patterns without the user doing anything.
- **Storage [M].** Patterns live in the existing 32 KiB section log as their own records (ids 24..87). The per-step
  mask codec (codec B, built in phase 0b for sections, the same step form for patterns) makes records small: the demo
  project's scene + its 4 patterns take 446 B where its section took 714 B (348 + 16 B since phase 0b); a busy 64-step
  project 1,166 B against 2,280 B (1,088 B since 0b). **Before phase 0b the log held only 6 busy 64-step sections;
  with codec B all 16 fit [M], and with patterns 16 busy scenes with their own patterns fit, plus 62 busy patterns
  beside 16 scenes.** No growth of the log is needed.
- **RAM [M/E].** No new pattern buffers: the scene stage that already exists (`sec_stage_p`, a 3,640 B project in the
  pool) is the queue for per-track patterns too. PATTERNS adds about 1.0-1.5 KB of RAM and 0.8 KB of pool [E from
  measured struct sizes]; phase 0 (every build) added **+448..+464 B RAM** for the log's index of 88 ids [M].
- **Sequencer [D].** A per-track origin makes a pattern start at its step 1 when it is launched; a launch takes effect
  **at the end of the track's current pattern** (default), at the **next bar** (OCT- held) or **now, keeping the
  position** (OCT+ held). The audio ISR copies 640 B and a few values at a switch; nothing per sample; the sequencer
  stays in XIP (no RAM code).
- **Device [D].** **LFO held = the PATTERN layer**: the 16 white keys are the selected track's 16 patterns (the screen
  is a 4 x 16 session grid whose columns are the keys), black keys pick the track and hold the STORE / COPY / CLEAR
  modifiers, KNOB 1..4 cue each track's next pattern. SAVE stays the scene layer exactly as today. LFO + HOME locks
  the grid open: that is the session screen. Anything queued is amber.
- **No project format bump.** `project_t` stays FUNB; what changes is the log (pattern ids: phase 0, built; a scene
  flag: phase 1), the section record's step codec (phase 0b, built), the snapshot stream (version 2), the backup (one
  object `PTN1`) and the editor protocol (the version after v9).
- **Builder.** `PATTERNS` (Sequencer group), needs SECTIONS 8/16; est. **+9 to +13 KB flash**; off = sections exactly
  as today. **Phases 0 + 0b, built, cost +672..+784 B flash and +448..+464 B RAM per profile, RAMTEXT and pool
  unchanged** [M, section 11.1].

## 1. What exists today (the facts the design rests on)

| Thing | Where | Facts [M] |
|---|---|---|
| Pattern | `core.h` `track_t.step[64]` / `dstep[64]` (union), 10 B a step | 640 B a track, resident; `track_t` 1,680 B, `trk[]` 6,720 B |
| Pattern values | `P_SLEN P_SDIV P_SSWING P_SGATE` (params.c:501 "PATTERN" page) | stored with the track's 69 values |
| Step index | `idx = abs % trk_len(t)` (seq.c:1540, also :322 :351, motion.c:220, macro_ui.c:16) | `abs` counts the track's grid from PLAY: tracks of different LEN already loop independently |
| Section | `sec_codec.c`: a whole `project_t` (3,640 B) + drum record (236 B), compressed | codec A (before 0b): demo 698 B, typical 504 B, busy 64-step 2,264 B, dense 2,712 B; codec B (since 0b): demo 348 B, typical 256 B, busy 1,072 B, dense 2,712 B (A kept); raw worst 3,877 B |
| Section log | `sec_log.c`: 8 x 4 KiB at 0x97000, ids 0..15 sections, 16 song, 24..87 patterns (`SLG_IDS` 88 since phase 0; 24 before) | records never span sectors (4,080 B a sector), one spare sector, reserve = one record of `SEC_REC_MAX` 4,059 B |
| Live jump | `section_cue` decodes into `sec_stage_p` (pool); the ISR's `arrangement_apply` → `proj_apply` on the bar | stage = a full `project_t`: four tracks' steps |
| Pending arena | `sec_pend` 8 KiB `.noinit`: sections stored while playing, written when quiet | (a flash write stops the audio) |
| Motion | `motion.c`: 64 events {track<<6\|step, param, value} for the 4 tracks together, 3 B each | `motion_store_t` 200 B; one per project buffer (`motion_proj.c`) |
| Song | `arranger.h`: 64 parts {scene, bars} | settings record + log id 16 (`SNG1`) |
| Undo | `undo.c`: per-track pattern diffs (steps, LEN, DIV) in the free pool/RAM | cleared by `proj_apply` |
| Sequencer code | `events_block` runs from XIP (fx.c:813 `FAR(events_block)`) | its size does not count against RAMTEXT, but the unity build's code generation moves RAMTEXT by tens of bytes (snapshots: +68 B on everything-that-fits) |

Budget after phases 0 + 0b (built on 2d600b6 + this branch, 2026-10-07) [M]:

| Profile | App free (of 581,564) | RAM free (of 98,304) | Pool free (of 335,872) | RAMTEXT free (of 32,512) |
|---|---|---|---|---|
| user-default | 11,836 | 23,484 | 29,012 | 3,204 |
| fm-va-studio | 17,904 | 8,284 | 14,192 | 5,596 |
| drum-machine | 17,284 | 12,200 | 4,844 | 8,800 |
| everything-that-fits | 12,152 | 20,140 | 8,532 | **20** |
| x0x-drums | 40,308 | 28,096 | 42,388 | 8,644 |

(The pool and RAM "free" are what the undo ring takes today; PATTERNS' RAM shortens the undo history, never below
`UNDO_MIN`.)

## 2. Data model and formats

### 2.1 The pattern [D]

A pattern belongs to one track (a drum pattern plays only on the drum track; a synth pattern on any synth track:
copy between T1..T3 is allowed). It holds:

- LEN, DIV, SWING, GATE (the PATTERN page's four values): one byte each;
- the steps 1..LEN (`step_t` / `dstep_t` as they are: chance, levels, ratchets, slides travel inside);
- its motion: up to 64 events {step, param, value} (3 B each) and its PLAY bit;
- room for what comes later: an optional chunk list (tag, length, bytes) for per-step data that `step_t` has no
  byte for (micro timing; SLOOP 2.4's locks if they are not motion events; section 9).

Not in a pattern: the sound (engine, preset, every other track value), the mix, the key/scale/transpose, the arp.
Those belong to the scene, as they belong to a section today. A pattern launched on a track plays with the sound the
track has.

Record (a log record, id = 24 + 16 x track + slot):

| Bytes | Field |
|---|---|
| 1 | flags: bit 0 motion chunk, bit 1 codec B, bit 2 drum, bit 3 motion PLAY on, bit 4 extension chunks, bits 5..7 version (1) |
| 4 | LEN, DIV, SWING, GATE |
| 1 + 3n | motion (flag bit 0): n, then n x (step, param, value) |
| ceil(LEN/8) | bitmap of the non-empty steps (an empty step: synth REST, drum no lane, as `sec_codec.c`) |
| per non-empty step | codec A: its 10 bytes; codec B: **`sec_codec.c`'s step form** (`sec_step_b`: one mask byte for bytes 0..6, bit 7 a second for bytes 7..9, then the non-zero bytes); the encoder keeps the smaller of A and B for the whole pattern |
| (bit 4) | chunks: tag, length, bytes |

Sizes [M] (bytes, without the log's 16-byte record head; codec B as built in phase 0b):

| Project | Track: LEN | codec A | codec B | kept (AB) | + 16 events | + 64 events |
|---|---|---|---|---|---|---|
| power-on | any: 16 | 7 | 7 | 7 | 56 | 200 |
| demo (acid, chords, lead, drums) | T1: 16 | 137 | 79 | 79 | 128 | 272 |
| | T2: 32 (held chords) | 289 | 77 | 77 | 126 | 270 |
| | T3: 12 | 57 | 33 | 33 | 82 | 226 |
| | DR: 16 | 87 | 31 | 31 | 80 | 224 |
| typical (sec_codec_test) | T1..T3: 16 | 87 | 47 | 47 | 96 | 240 |
| | DR: 16 | 167 | 39 | 39 | 88 | 232 |
| busy (64 steps everywhere) | T1 16ths bass | 653 | 411 | 411 | 460 | 604 |
| | T2 held chords | 493 | 157 | 157 | 206 | 350 |
| | T3 8ths lead | 333 | 173 | 173 | 222 | 366 |
| | DR full kit | 653 | 199 | 199 | 248 | 392 |
| dense (64 random full steps) | any | 653 | 781 | 653 | 702 | **846 = the worst pattern** |

Codec B round-trips on every case [M]. The worst pattern, 846 B, is a fifth of a log record's limit.

### 2.2 The scene [P]

A scene is today's section record with a new flag `SEC_SCN` (**0x20** in the flags byte: 0x10 is codec B since phase
0b, and phase 0 makes every build refuse a record with a flag it does not know): the body as `sec_body` writes it
**without the steps** (no step bitmap, no steps; LEN DIV SWG GATE left out of the track values: they are the
pattern's), followed by **4 pattern references** (slot 0..15 of that track; 0xFF = none: the track is empty in this
scene; **0xFE = keep [D]**: the track goes on with what it plays when the scene starts). Motion is not in a scene (it
is the patterns').

Sizes [M]: power-on 66 B, demo 146 B, typical 100 B, busy 146 B. The bound with every value moved, three FM6 voices
and a drum record is **1,349 B** (computed from the body's layout): a scene never needs the raw fallback.

| Project | Section, codec A (before 0b) | Section, codec B (since 0b) | Scene + its 4 patterns (with the 5 log heads) |
|---|---|---|---|
| demo | 714 B | 364 B | 446 B |
| typical | 520 B | 272 B | 360 B |
| busy 64-step | 2,280 B | 1,088 B | 1,166 B |
| dense worst | 2,728 B | 2,728 B | 2,784 B |

(With heads. Codec B already gives sections most of the size win; patterns add the sharing, and the per-track
launches.)

### 2.3 Pattern count per track: 16 [D]

| | 16 a track (64 patterns) | 32 a track (128 patterns) |
|---|---|---|
| Log index RAM (every build since phase 0; 8 B an id: u16 offsets) [M] | **756 B (built; was 292 B for 24 ids)** | 1,268 B |
| `slg_model_t` on the stack (x3 in `sm_more`; u8 sector, u16 size) [M] | **340 B (built; ~1 KB in `sm_more`)** | 532 B |
| Typical 16-step patterns beside 16 scenes, 32 KiB log [M] | all 64 (live 6,592 B, 26 %) | all 128 (10,560 B, 42 %) |
| Busy 64-step patterns (16 events) beside 16 busy scenes, 32 KiB [M] | 62 of 64 | 62 of 128 |
| Same at 48 KiB [M] | 64 of 64 | 110 of 128 |
| Keys | one white key a pattern, no bank | a bank gesture (OCT) on top |
| The special case | scene X's own pattern = slot X on each track: the grid reads like Ableton's | no such match |

**16** (decided): with busy material the log, not the slot count, runs out first (62 busy patterns at 32 KiB
whatever the count), and 16 matches the keys and the 16 scenes.

### 2.4 IDs, references, sharing, garbage [P]

- **Log ids** (built in phase 0): 0..15 scenes (as sections today), 16 the song, 17 the pattern state of the working
  project (each track's pattern and modified bit, 8 B, written with the autosave), 18..23 spare, **24..87 the
  patterns** (24 + 16 track + slot; `SEC_ID_PAT0`). `SLG_IDS` 24 -> 88.
- **References**: a scene names a slot per track (or none, or keep). A RAM table `scene_ref[16][4]` (64 B) is filled
  at boot from the scene records' last 4 bytes; a pattern's reference count is a scan of that table (64 entries),
  never stored.
- **The working copy** [D] of each track remembers its source: `pat_cur[k]` (slot, or none) and a **modified** bit,
  set by every pattern edit (every edit already calls `undo_mark`: the hook is there), a motion event, or a change
  of LEN / DIV / SWING / GATE.
- **Storing scene X** (SAVE + key): for each track, unmodified with a source -> refer to it (sharing, no write);
  modified (or no source) -> write the working copy into a slot: its source when no other scene refers to it,
  else a new slot (slot X if free, else the lowest free); no free slot -> nothing is stored, "T2: NO FREE PATTERN".
  This is copy-on-write: a stored scene never changes when another is stored, as sections behave today.
- **Storing into a pattern** [D] (PATTERN layer, STORE, section 6): the working copy overwrites slot n; every scene
  that refers to n plays the new content (the explicit, linked edit). "AGAIN:" confirms over a used slot, as today's
  section store.
- **Garbage**: patterns are slots the user sees; none is deleted behind their back. A pattern no scene refers to
  stays until cleared (the PATTERN layer's CLEAR, or SAVE > TOOLS > CLEAN: every unreferenced pattern, with a count
  and a confirm). MEM FULL says "CLEAN: n UNUSED" when that would help.
- **Order of writes** (power-cut safety, as sec_log's rules): a scene store writes its new patterns first and the
  scene record last (the commit). A cut leaves at most a written pattern no scene refers to yet (shown, cleanable),
  or, when the store overwrote its own source slot, scene X with its new pattern under its old sound; never a scene
  naming a missing pattern (a missing pattern reads as an empty track and the MISSING line says so).

### 2.5 Motion in patterns [D]

Motion moves from the project to the pattern it was recorded on (the user: motion is stored with the pattern): an
event's step and parameter stay, its track is the pattern's. **Per pattern: up to 64 events** (what a whole project
holds today).

The working store becomes per track: four lists of up to 64 (`motion_ev_t` 3 B: 4 x 194 B = 776 B against 200 B
today, +576 B RAM [M from the struct sizes]). `motion_step` scans only its own track's list (at most 64, as today's
scan of the shared 64). A pattern switch restores the track's patch under its old motion (`motion_restore`, as at
PLAY) and takes the new list.

Non-PATTERNS builds keep today's 64-shared store: a flattened scene (section 2.7) keeps the first 64 events in track
order and the MISSING line says "MOTION CUT".

### 2.6 Format: no FUNC [P]

`project_t` (FUNB) is unchanged: the working project, the autosave and a section record keep their layout. What
changes:

| Object | Change | Older firmware |
|---|---|---|
| Section log | ids 24..87 pattern records, id 17 pattern state (**phase 0, built**); flag `SEC_SCN` 0x20 on records 0..15 (phase 1) | phase 0 builds keep them all (section 11.1); pre-phase-0 builds: section 11, R3 |
| Section record steps | codec B (`SEC_RAW \| SEC_B`, flags & 0x11 = 0x11; **phase 0b, built**) | pre-0b builds refuse the record (raw length check): the section reads as empty there, kept (section 11.1) |
| Snapshot stream | version 2: kind 5 `SNR_PAT` (log id 24..87: a pattern record as the log keeps it), kind 6 `SNR_PSTATE`; `SNR_SEC` may hold a scene | `snapshots.c` accepts `ver >= 1` and skips unknown kinds: a v2 snapshot loads into a non-PATTERNS build without its patterns |
| Backup | object `PTN1` (kind 7): every pattern record (id, length, bytes); restored before S01..S16; BK_LIST switches: "log with pattern ids", "PATTERNS" (bits assigned with the protocol version after v9) | an editor without PTN1 restores scenes without their patterns: they play empty tracks (MISSING says so) |
| Editor protocol | the version after v9 (section 7.3) | older: the editor shows sections as today |

A FUNC is kept in reserve for a day `project_t` itself must grow; patterns take the per-pattern growth instead (their
records are versioned and have chunks), so the "FUN8/FUNB is full" pressure on `step_t` stops being a
project-format problem.

### 2.7 Migration

- **Codec A sections -> codec B (phase 0b, built)**: lazy. Every record written before 0b still reads; a section is
  rewritten in codec B only when it is stored again (the same project, fewer bytes) [M: sec_codec_test, every demo,
  preset and random project: the codec A record loads as the project, stored again it is byte for byte the record a
  fresh store writes]. Snapshots and backups carry the records as they are and restore both forms.
- **FUNB sections -> scenes (first start of a PATTERNS build)** [P]: each legacy section X becomes 4 pattern records
  in slot X of each track (only non-empty patterns; an empty track refers to none) and then the scene record replaces
  the section (the commit). One section at a time; cut anywhere, the next start goes on (a section record still
  there is converted again; its patterns are rewritten in the same slots). No deduplication here: "your old section
  C's drums are drum pattern 3". Space: every measured project shrinks or stays (demo 364 -> 446 B with heads is the
  one that grows: 16 such scenes are 7 KB, far from full), the reserve covers the peak (one section's patterns before
  its old record dies). The motion of section X goes into its patterns by track (each track's events, at most 64 a
  pattern: no loss).
- **The song** (`SNG1`, settings record): unchanged, it names scenes.
- **Snapshots** v1 (sections) load into a PATTERNS build through the same conversion.
- **Backups** S01..S16 from today: legacy section records, converted at the next start like any.
- **The autosave** (FUNB): unchanged; with no id-17 record every track's source is "none, modified" (storing a
  scene writes its patterns).
- **PATTERNS -> non-PATTERNS build of the same release** (phase 1 adds this to every build): **flattened on read**,
  nothing rewritten. A scene is decoded, its 4 patterns are read and put into the `project_t` (`sec_read` assembles;
  the stage and the load use it): it plays as the section it would be. Storing a section there writes a legacy
  section record over the scene (its patterns stay in the log, unreferenced). Back on a PATTERNS build that section
  converts again; every scene not stored on the other build is as it was. MISSING-style line once: "PATTERNS: PLAYED
  AS SECTIONS".
- **PATTERNS -> a phase 0 firmware** (before phase 1): a scene record (flag 0x20) is refused, the section reads as
  empty; the scene and every pattern record stay in the log untouched (phase 0 counts and keeps them). Going back to
  the PATTERNS firmware finds them all. The flatten-on-read was planned for phase 0; it moved to phase 1 because it
  needs the pattern and scene record formats, which phase 1 fixes (section 11.1).
- **Non-PATTERNS -> PATTERNS**: the first-start conversion above; lossless.

## 3. Storage

### 3.1 Where [D]: the section log, unchanged in size

Pattern records are records of the existing log (`sec_log.c`), with its rules: never spanning a sector, the newest of
an id wins, compaction into the spare sector, tombstones for a clear, the pending arena while playing.

The reserve changes from "one record of 4,059 B" to "**a scene of 1,349 B and four patterns of 846 B**", each its own
record (the playing scene can always be stored with four new patterns). Modelled on the real `sm_*` code [M]; it
costs about as much as today's reserve. (Phase 0b does not change the reserve: the longest section record is still
the raw one, 4,059 B with a motion chunk, because a dense project stays raw in either codec [M].)

### 3.2 How many fit [M] (the real log, 16-byte heads, the reserve kept after every store)

| Content | 32 KiB (8 sectors, today) | 48 KiB (12) | 64 KiB (16) |
|---|---|---|---|
| sections, codec A (before 0b): typical 504 B | 16 of 16 | 16 | 16 |
| sections, codec A: demo 698 B | 16 of 16 | 16 | 16 |
| sections, codec A: **busy 64-step 2,264 B** | **6 of 16** | 10 | 14 |
| sections, **codec B (since 0b): busy 64-step 1,072 B** | **16 of 16** | 16 | 16 |
| sections, codec B: the largest that still fit 16 times | 1,344 B each | 2,024 B | 2,024 B |
| patterns: demo scenes, each its own 4 patterns | 16 of 16 | 16 | 16 |
| patterns: busy scenes, each its own 4 patterns | 16 of 16 | 16 | 16 |
| patterns: busy scenes, own patterns with 16 motion events each | 16 of 16 | 16 | 16 |
| 16 demo scenes + every slot a typical pattern | 64 of 64 (26 % used) | 64 | 64 |
| 16 busy scenes + every slot a busy pattern (16 events) | 62 of 64 | 64 | 64 |
| 16 busy scenes + worst-case patterns (846 B) | 20 of 64 | 36 | 52 |
| a shared song: 16 busy scenes over 22 busy patterns | 37 % used | 23 % | 17 % |

Reading: the sector-packing of large section records is what limited the log (a 2,264 B section left 1.8 KB of each
sector unused). Codec B (phase 0b) halves busy sections and the log takes all 16; small pattern records pack sectors
well too. **The 32 KiB log is enough for patterns**; the number that can run out is busy patterns beside 16 busy
scenes (62).

### 3.3 Grow the log? [D: no, keep 32 KiB]

| Option | Gains | Costs |
|---|---|---|
| 32 KiB (today) | nothing to move; snapshots unchanged (the largest a full log makes stays under 29.3 KB, docs/BUILDER.md) | 62 busy patterns + 16 busy scenes, 20 worst-case patterns |
| 48 KiB: +16 KiB from USR3 (as snapshots did) | all 64 busy patterns; 32/track becomes useful (110 busy) | USR3's sample room 32 -> 16 KiB with SNAPSHOTS 4; the log is no longer contiguous (0x97000 and 0xC8000..): `slg_off` needs a sector map (~100 B flash [E]); a full log's snapshot can pass 32 KiB: SNAPSHOT FULL on the largest states |
| 64 KiB | 52 worst-case patterns | USR3 has no sample room left with SNAPSHOTS 4 |

## 4. RAM

### 4.1 What is resident [P]

- The playing patterns: `trk[k].step` as today (no change, nothing moves to pointers).
- The queued patterns: **the scene stage that exists** (`sec_stage_p`, 3,640 B, pool; in every SECTIONS 8/16 build).
  Its `t[k]` holds track k's queued pattern (steps + the four values). A scene queued fills all four; a pattern queued
  later replaces its track in the staged scene ("scene C with drums 5"); a scene queued after patterns drops them
  (the scene wins). The ISR copies a staged track into `trk[k]` at its boundary: **double buffering without a second
  buffer**, glitch-free (the copy is 640 B inside the ISR, between two blocks).
- The stage's motion: 4 lists of 64 (776 B) beside it.
- **In song mode [D]** the stage holds the song's next part (`sec_service`). A pattern launch is allowed: it needs its
  own staging while the stage waits for the next part, so a launch in song mode is staged into **the next part's
  copy** when its boundary comes after the part change, and applied to `trk[k]` directly otherwise (the ISR copies
  from a 640 B + motion buffer for that one track, in the pool: +0.8 KB pool [E]). The launched pattern plays on that
  track **until the song's next part**, which replaces it as today.

### 4.2 Sizes [M structs / E totals]

| Item | Bytes | Where | Builds |
|---|---|---|---|
| log index `slg`, 24 -> 88 ids, u16 offsets | **292 -> 756 (+464) [M, built]** | RAM | every build since phase 0 |
| `slg_model_t` (stack, x3 in `sm_more`), u8 sector, u16 size | **268 -> 340 [M, built]** | stack | every build since phase 0 |
| pending arena's per-id tables (`len`, `off`), 16 -> 88 ids | +288 [E: 4 B an id] | .noinit | PATTERNS (patterns stored while playing; phase 0 needs none: it never writes a pattern) |
| pattern state: `pat_cur`, `pat_req`, `when`, modified, origin a track | 4 x 8 = 32 [M] | RAM | PATTERNS |
| `scene_ref[16][4]` | 64 [M] | RAM | PATTERNS |
| working motion per track (MOTION) | 200 -> 776 (+576) [M] | RAM | PATTERNS + MOTION |
| stage motion per track | 776 [M] | pool | PATTERNS + MOTION |
| song-mode launch buffer (one track) | ~0.8 KB [E] | pool | PATTERNS |
| pattern stages | **0** (the scene stage) | | |
| **total, PATTERNS + MOTION, phase 0 included** | RAM ~1.4 KB, pool ~1.6 KB, noinit ~0.3 KB [E] | | |

Per profile [E]: after phase 0 (measured, section 1) fm-va-studio is the tightest in RAM (8,284 -> ~6.9 KB) and
drum-machine in pool (4,844 -> ~3.2 KB with the song-mode buffer; without it ~4.0 KB): both far above `UNDO_MIN`
(1,024 B of ring: build.py), the undo history shorter. Nothing per profile is close; **RAMTEXT is** (section 4.3).

### 4.3 RAM code

No pattern function runs from RAM: the sequencer (`events_block`, `seq_tick`, `motion_step`) is XIP (fx.c:813). The
risk is indirect: the unity build's code generation moves the audio path's RAM code when anything else changes
(SNAPSHOTS: +68 B on everything-that-fits, which has **20 B** left; MOTION's own +60 B). Phases 0 and 0b left
RAMTEXT unchanged on all five profiles [M]. Rules for the build: the pattern code is `noinline` XIP functions called
from `events_block`; no AINL helper of the audio path changes signature; the phase 2 gate builds
everything-that-fits with PATTERNS=1 and compares `.ram_hot`. PATTERNS is off there anyway [D].

## 5. Sequencer

### 5.1 Per-track position

Today `idx = abs % len` from PLAY. A launched pattern must start at its step 1 (a 32-step pattern launched on an odd
bar would otherwise start in its middle). Each track gets an origin: `idx = (abs - org) % len`, `org` set to the
track's `abs` at the switch (0 at PLAY and at a scene launch, which resets every track as today). Five places compute
`idx` (seq.c:322, :351, :1540, motion.c:220, macro_ui.c:16): one helper `trk_idx(t, abs)`. A bar boundary is an
even step on every division (4 den: 4, 8, 16, 32, 12, 24 a bar), so swing keeps its phase; a pattern's end is a
step boundary of that track (its own DIV), so its swing phase holds too.

### 5.2 Launch timing [D]

| When | Gesture | Starts | Use |
|---|---|---|---|
| **pattern end** (default) [D] | the key | its step 1 when the track's playing pattern wraps (up to LEN steps away; at once when the track plays nothing) | the normal launch, X0X-style "next pattern"; odd lengths keep their phrase |
| **next bar** | OCT- held [P] | its step 1 on the next bar (4/4, as live jumps: `live_block`'s bar) | a long pattern left early, tracks lined up on the bar |
| **now, legato** | OCT+ held [P] | the next step, at the same position (`org` kept: `idx = (abs - org) % newlen`) | fills, Ableton's legato |
| stopped | the key | loaded at once (as `section_load`) | building |

A launch is staged by the main loop (log read + decode: [E] well under a millisecond for an 846 B record, against
the 42,606 emulator instructions measured for a 524 B *section* decode); the ISR applies it only when staged
(`sec_stage_id`'s protocol, a bit per track), else it waits for the next boundary of the same kind (a "now" launch
then lands a step later).

### 5.3 Independent loops

Nothing new: each track wraps at its own LEN on its own DIV (already the case). A pattern brings its LEN and DIV; its
origin keeps it on the step it started.

### 5.4 Motion, REC, undo, the song, the CPU guard

- **Motion**: at a switch, `motion_restore(t)` (the patch back), then the new pattern's list; recording knob moves
  write into the working copy's list (modified).
- **REC**: a take records into the working copy (modified). A launch on a recording track ends that track's take at
  the switch (today: "a take does not run on into another section", seq.c:1283); the other tracks keep recording.
- **Undo**: a pattern switch is an undo level of that track (its pattern before the switch, an `undo_mark` in the
  ISR as recording passes do): EDIT + OCT- brings an edited working copy back after an accidental launch [P]. A scene
  launch clears the history as `proj_apply` does today.
- **Song (arranger) [D]**: unchanged, it chains scenes; `sec_read` assembles scene + patterns into the stage. In song
  mode a pattern launch is allowed and overrides that track until the song's next part (section 4.1). A scene
  reference "keep" (0xFE) leaves the track's pattern playing across the part change. SONG REC records scene launches
  only; recording pattern launches comes later [D].
- **CPU guard**: a pattern launch changes no engine and no voice count: nothing for the guard. A scene launch is
  today's section apply.

### 5.5 ISR cost [E]

Per block with nothing queued: one test of a 4-bit mask (a few cycles). Per step: one subtraction in `trk_idx`.
At a switch: `memcpy` of 640 B + 4 values + a motion list (<= 194 B) + `motion_restore` (75 bits): ~1-2 k cycles once,
less than today's section apply (`proj_apply` clamps 4 x 75 values through descriptors). Gate (phase 2): bench.c
scenario on the emulator, the block's instruction count at a switch and without.

## 6. Device UI

### 6.1 Principles kept

Colour carries meaning only (tools/colors.json): **green** `ok` = playing, **amber** `warn` = queued (about to
change: patterns and scenes alike [D]), **red** `err` = recording / will be overwritten, engine colours = which
engine a pattern plays on (a thin top band), grey = stored, dark = empty. A layer is a held button; tapped, the button
opens its pages as before; held + HOME locks it open (ui_input.c:806).

### 6.2 The PATTERN layer: LFO held [D]

LFO is the only page button that is not a layer today (tap opens the LFO pages; ENV is a layer only on FM6). Held,
it becomes "patterns" (SEQ held stays step entry). Its keys, knobs and screen:

| Control | Does |
|---|---|
| white key n | launch pattern n of the selected track **at the end of its playing pattern** [D]; an empty slot: the track stops there |
| OCT- held + white n | launch at the next bar [P] |
| OCT+ held + white n | launch now, legato [P] |
| black keys 1..4 (F#3 G#3 A#3 C#4, the SEQ layer's page keys) | select track T1, T2, T3, DR (the layer stays; the same as ALGORITHM) |
| black 5 (D#4) | stop the selected track at the end of its pattern |
| black 6 (F#4) held + white n | **STORE** the working copy into pattern n ("AGAIN: 5" within 3 s over a used slot) [D] |
| black 7 (G#4) held + white a, then white b | **COPY** pattern a to b (confirm over a used b; a synth pattern to another synth track: select it between the two keys) |
| black 8 (A#4) held + white n | **CLEAR** pattern n (confirm; "USED BY A C" names the scenes that refer to it, they will play the track empty) |
| black 9 (C#5) | **DUPLICATE** the playing pattern into the first free slot and queue it (the variation workflow) |
| black 10 (D#5) held + white n | launch **scene** n (A..P without banks; the SAVE layer keeps its banks) |
| black 11 (F#5) | free (follow actions later, section 8) |
| KNOB 1..4 | track k's next stored pattern (turn: cue the previous / next used slot) |
| REC, PLAY | as everywhere (REC is not eaten: recording into the working copy goes on) |

Screen while held (the columns are the white keys, left to right; the selected track's row is the one the keys play):

```
 +------------------------------------------------------+
 | patterns  T2 DIGITAL               at end   mem 41%  |   title: layer, track + engine, launch mode, gauge
 |       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 |
 |  T1  [#][=][=][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   [#] green: playing   [=] grey: stored
 | >T2  [=][#][~][=][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   [~] amber band: queued   [ ] dark: empty
 |  T3  [#][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   [*] a playing pattern with unsaved edits
 |  DR  [=][*][=][=][=][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]|   (each cell's top band: the engine / kit colour)
 |  scn  A  B  C  D  e  f  .  .  .  .  .  .  .  .  .  . |   scenes stored (upper case), the playing one green
 |------------------------------------------------------|
 |  (T1 1)      (T2 2>3)     (T3 1)       (DR 2*)       |   KNOB 1..4: each track's pattern, "> 3" when queued
 +------------------------------------------------------+
```

Layout [E]: title 24 px, column numbers 12, 4 track rows of 28 px (cells 13 x 24), the scene strip 16, dials 56:
232 px of 240. Drawn in bands (the 124-row canvas limit, ui_song.c:37).

**Locked (LFO + HOME) [D]: the session screen.** The same grid, both hands free; any page button unlocks (today's
rule). No separate page or menu is needed.

### 6.3 The SAVE layer (scenes): as today [P]

Unchanged keys: white 1-4 launch the bank's scenes, 5-8 store (with the "AGAIN:" confirm), 13 loop / song, 14 SONG
REC, 16 the song page, SAVE + OCT-/OCT+ the bank. Changes:

- store = section 2.4's rules (patterns auto-created or referenced);
- a scene tile whose tracks no longer all play its patterns shows a dot ("B•": scene B, changed since);
- the MEM gauge counts patterns too ("mem 41% +9": +n stays "scenes of the last stored size, with their new patterns");
- a queued scene's band turns amber (white today, ui_layers.c:845) [D];
- white 15 (free today) opens the PATTERN layer locked (the session screen from the scene layer).

```
 +------------------------------------------------------+
 | song   mem 41% +9                                    |
 |  [ A ][ B•][ C ][ D ]  [save A][save B][save C][...] |   B playing (green) but changed (dot)
 |  ...                         [loop][rec][grid][chain]|   grid = white 15: the session (locked PATTERN layer)
 +------------------------------------------------------+
```

### 6.4 Gestures that conflict, and how they are resolved

| Today | With PATTERNS | Resolution |
|---|---|---|
| LFO tap opens the LFO pages | LFO hold = patterns | taps still open the pages (`layer_tap`); a hold under 140 ms shows nothing (`SHOW_MS`) |
| two layer buttons held: the first in `LY_*` order wins (seq.c:84) | EDIT is not used inside the pattern layer | the modifiers are black keys, not buttons: no priority question |
| SAVE layer queued scene: a **white** top band (ui_layers.c:845) | queued pattern: an **amber** band | **both amber [D]** |
| SAVE layer white 15 unused | the session | none |
| SEQ layer knobs DIV SWING LENGTH | now the pattern's values | they mark the working copy modified |
| X0X UI port: PRESETS cues a pattern | — | that UI calls the same pattern API (section 8) |

### 6.5 Messages

"T2 5 AT END", "NEXT BAR: T2 5", "T2 5 NOW", "STORED T2 5", "AGAIN: T2 5", "CLEARED T2 5", "USED BY A C", "T2: NO
FREE PATTERN", "MEM FULL", "CLEAN: 7 UNUSED", "PATTERNS: PLAYED AS SECTIONS" (non-PATTERNS build).

## 7. Web editor (web/editor.html)

### 7.1 The mixer is the session view [D]

Decided by the user (2026-10-07; nothing built yet): the 16 patterns per track appear **in the mixer's channel
strips**, as in Ableton Live's session view. There is **no separate session-grid screen** in the editor: the mixer
IS the session view.

- **Each track strip** gets a column of **16 pattern slots at its top, above the instrument row**. Slot colours as
  on the device (section 6.1): the playing slot **green**, the queued one **amber**, recording **red**, stored ones
  grey with a mini step map and LEN, empty ones **dim**; "*" marks a playing pattern with unsaved edits.
- **Click** a slot: launch it (at the pattern's end, the default [D]; shift: now, alt: next bar, mirroring OCT+ /
  OCT-). **Double-click**: the piano roll / drum grid popup on that pattern (section 7.2). **Drag** onto another slot:
  copy (confirm over a used one). Delete: clear (confirm, names the scenes using it).
- **The MASTER strip carries the scene column**: scene launch buttons **A..P in the same rows as the slots**, so a
  row = a scene, as Ableton's Main track. In the special case (scene X refers to slot X on each track) a scene's
  four patterns sit on its row; hovering a scene outlines the four patterns it refers to wherever they are (and
  marks a "keep" track).

```
 Optimist  (o) [>][#] B*  120 bpm                 Mixer  Library  Samples  Projects  Snapshots  Settings
 +------------+------------+------------+------------+---------+
 | T1 ANALOG  | T2 DIGITAL | T3 FM6     | DRUMS 909  | MASTER  |
 +------------+------------+------------+------------+---------+
 | [>] 16 ''' | [ ] 32 ' ' | [>] 12 '.' | [ ] 16 ::: | [>] A   |   row 1   [>] green: playing
 | [ ] 16 ''. | [>] 32 '  '| [ ] 16 ''  | [>]*16 ::. | [ ] B   |   row 2   * edited, not stored
 | [~] 64 ''''| [ ]        | [ ]        | [ ] 16 ::: | [ ] C   |   row 3   [~] amber: queued
 | [ ]        | [ ]        | [ ]        | [ ] 16 ..: | [ ] D   |   row 4   [ ] dim: empty
 |  ... 16 rows of slots / scenes A..P ...                     |
 +------------+------------+------------+------------+---------+
 | instrument | instrument | instrument | kit        |         |   the strips as today, below the slots
 | sends, fader, mute / solo ...                               |
 +------------+------------+------------+------------+---------+
```

### 7.2 Editing a pattern

- **The playing pattern** (or launched now when stopped): the existing popup edits the working copy through
  TRACK_STEP / DRUM_STEP exactly as today; the header says "T2 pattern 3 *" and gains **Store pattern** (= the store
  command) and **Store as new** (first free slot).
- **Any other pattern** (phase 4b): read the record into the page, the same roll / grid edits a local copy, **Save**
  writes it back (stopped: at once; playing: into the pending arena, as a store while playing). The record codec is
  60-80 lines of JS mirroring section 2.1 (the step form is already in `secTracks`, phase 0b).

### 7.3 Protocol: the version after v9 [D]

A separate agent builds protocol **v9** now (`feat/protocol-v9`) and may use command numbers from 54 up. The patterns'
commands come in the **next** version; their numbers are assigned then, after v9 has taken its own. Nothing is
reserved now. The commands needed:

| Command | Request | Reply |
|---|---|---|
| PAT_LIST | op 0 (all) / 1 (state only) | op; 1: per track cur, req, when, modified (16 B). 0: + per track x 16 slots: flags (used, motion, drum), LEN; + 16 scenes x 4 refs (127 none, 126 keep) and the used mask |
| PAT_LAUNCH | track, slot (127 stop), when (0 end, 1 next bar, 2 now) | track, slot, rc (0 ok, 1 args, 2 empty) |
| SCENE | op (0 launch, 1 store, 2 clear), scene, when | op, scene, rc (store's rc: 4 MEM FULL, 5 no free pattern, 6 confirm needed: send again within 3 s) |
| PAT_OP | op (0 store working copy -> slot, 1 copy, 2 clear, 3 duplicate, 4 clean unused), track, a, b (track2 for copy) | op, rc, the slot written |
| PAT_READ | track, slot, offset (2 x 7 bit) | track, slot, offset, total, pack7 bytes (<= 256) |
| PAT_WRITE | track, slot, offset, total, pack7 bytes; the last chunk commits | track, slot, offset, rc |
| PAT_CHANGED (push) | — | track, cur, req, when, modified (while WATCH's pattern bit is on) |

Versioning and fallback: INFO's protocol version and BK_LIST's switches (bits assigned with that version: "the log
keeps pattern ids" for every build since phase 0, "PATTERNS") say what the device has. Without them the editor shows
the sections as today (the Projects tab, STATUS's section). An older editor on a PATTERNS device sees scenes as
sections (STATUS, PROJECT) and keeps working; its backups lack `PTN1` (the restore report names it).

The backup reader already understands both step codecs of a section record (`secTracks`, phase 0b; web/test_web.mjs).

## 8. Pluggable UI: X0X and Felucca (docs/UI-PORTS.md, branch docs/ui-ports)

UI-PORTS ruled per-part pattern banks out as RAM (16 x 640 B a track against ~3 KB free) and mapped X0X's patterns
onto whole-project sections (lossy: a pattern change changes the sound). Stored patterns change both verdicts:

| X0X concept | With PATTERNS |
|---|---|
| 16 patterns per part | **direct**: 16 patterns a track, in flash; RAM ~0.8 KB for the stage's motion, not 40 KB |
| PRESETS cues the next pattern of this part | `pat_launch(trk, slot, WHEN_END)`: X0X switches at the pattern's end (the default [D]) |
| HOME: white key cues a pattern | the same call; "two held: a chain" = a per-track chain (not in this design: a follow-action list a pattern, ~20 B a track, later) |
| 303A / 303B, 909 + 808 parts | tracks 1-2 (ACID), the drum track: the two X0X drum parts share one drum pattern a bar (lossy, as UI-PORTS says) |
| Song (192 bars, per-part patterns + mutes) | the song chains scenes; each distinct combination of parts' patterns becomes a scene (<= 16; past that, lossy); "keep" references [D] save scenes |
| SAVE + white key: copy the pattern there | PAT_OP copy |

| Felucca concept | With PATTERNS |
|---|---|
| one pattern a track, SEQ > PATTERN page | the working copy; a "PATTERN n" row on that page = launch / store pattern n |
| SONG chain of 4 project slots | scenes A..P in the chain (as UI-PORTS says) |
| PHRASES (patterns from user presets) | load into the working copy, then store into a slot |

The pattern operations are core commands (UI-PORTS step 2a: "step and pattern commands under the IRQ rule"):
`pat_launch`, `pat_store`, `pat_copy`, `pat_clear`, `pat_dup`, `scene_launch`, `scene_store`, `pat_state(trk)` in the
core, called by Optimist's layers, an X0X UI, a Felucca UI and the editor alike.

## 9. SLOOP 2.4 compatibility risk

What is known (memory notes, 2026-10-06; source not public): 2.4 announces **parameter locks** (hold a step, turn a
knob), **micro timing**, **section chaining** (hold SAVE, tap A B B C), fills, steps up to 2 bars.

| 2.4 feature | Our place for it | Risk |
|---|---|---|
| Section chaining gesture | the song chain (64 parts) + a gesture on the SAVE layer | none for the data |
| Parameter locks | pattern motion events {step, param, value} (the user's view: p-locks = motion with another UI), 64 a pattern | if 2.4 stores more than 64 a pattern, or values wider than a byte: a pattern chunk (tag "PLK1") |
| Micro timing | a pattern chunk ("MTM1": a signed byte a step, 64 B) | none: `step_t` and `project_t` stay as they are |
| Steps up to 2 bars | our LEN 64 (4 bars at 1/16) covers it | none |
| Their project format | today's importer (`proj_import`: FUN1..FUNB) converts their section into a scene + its own patterns (the migration path) | if 2.4 changes its step layout, the importer maps it; its locks / timing go to the chunks |

Rule kept from the memory note: compare their format first and adopt it where it differs. Patterns make that cheap:
their per-pattern data lands in a versioned pattern record, not in `project_t`.

## 10. Builder

### 10.1 The item [P]

`PATTERNS` in the Sequencer group, values 0 / 1. Constraint (error): PATTERNS needs SECTIONS 8 or 16 (the log, the
stage). With MOTION: per-track motion (section 2.5); without: patterns carry no motion (records written by a MOTION
build keep their chunk, as sections do today: "every build reads a chunk").

| Interaction | Behaviour |
|---|---|
| SECTIONS 4 | PATTERNS refused (error) |
| SECTIONS 8 | scenes A..H, 16 patterns a track all the same (the ids do not depend on it) |
| MOTION 0 | patterns without motion; a pattern's chunk kept on rewrite |
| SNAPSHOTS | stream v2 (`SNR_PAT`); size bound unchanged (it is the log's) |
| UNDO_HISTORY 0 | the switch is the single level |
| MISSING_WARN | names a missing pattern, "MOTION CUT", "PATTERNS: PLAYED AS SECTIONS" |
| everything-that-fits | off [D] |

### 10.2 Costs [E now; measured after the build]

Basis: measured deltas of comparable items (costs.json, 2026-10-07): SECTIONS 16 vs 4 +6,884 B (log, codec,
sections, migration); SNAPSHOTS +8,708 B; MOTION +3,420 B (RAMTEXT +60); REC_MODES +1,576 B (a screen and knobs).

| Part | Flash [E] |
|---|---|
| **Phases 0 + 0b (every build), built** | **+672..+784 B [M]** (estimated 0.8-1.2 KB for phase 0 alone, with flatten-on-read: that part moved to phase 1) |
| flatten-on-read in every build (phase 1) | 0.4-0.8 KB |
| pattern codec (encode / decode; the step form is sec_codec.c's, built) | 0.6-1.0 KB |
| store / reference / copy-on-write / clean, migration | 1.5-2.0 KB |
| sequencer: origin, queue, staging, ISR apply, undo level, song-mode launches | 1.0-1.5 KB |
| motion per track | 0.5-0.8 KB |
| device UI: PATTERN layer + grid, SAVE layer marks | 3.0-4.5 KB |
| editor commands (the version after v9) | 1.0-1.5 KB |
| snapshots v2, backup PTN1 | 0.5-1.0 KB |
| **PATTERNS total (phases 0, 0b not counted)** | **~9-13 KB** |

RAM: section 4.2 (~1.4 KB RAM, 1.6 KB pool, 0.3 KB noinit beyond phase 0).

### 10.3 Profile defaults [D]

| Profile | App free after phase 0 [M] | PATTERNS | Why |
|---|---|---|---|
| user-default | 11,836 | **on only if the measured cost fits** (phase 5 measures; else off until something is trimmed) | the main profile, but the fit is tight |
| fm-va-studio | 17,904 | **on** | room |
| drum-machine | 17,284 | **on** | patterns are the point |
| everything-that-fits | 12,152 | **off** | 20 B of RAM code |
| x0x-drums | 40,308 | **on** | X0X's per-part patterns |
| default build (no .config) | | **off** | sections exactly as today |

## 11. Phased build plan

Each phase ends with the firmware working, `tests/run_tests.sh` green, every profile linking and fitting
(`tools/builder/verify.py`), and a commit the user can review. Effort in focused agent-days [E].

| Phase | Content | Gate | Effort |
|---|---|---|---|
| **0** log forward-compatible (every build): **built** | `SLG_IDS` 88 with a compact index (u16 offsets), `slg_model_t` narrowed (u8 sector, u16 size); records of an unknown flag refused; ids past 88 skipped instead of sealing; heal undoes a cut compaction it cannot finish (11.1) | sec_log_test with pattern ids, cuts at every program, a later firmware's id; every golden unchanged | done |
| **0b** codec B for sections (every build): **built** | per-step masks (`SEC_RAW \| SEC_B`), A kept where smaller; lazy migration; the editor's backup reader | sec_codec_test: round trips, A records migrate, fuzz; 16 busy sections fit | done |
| **1** data model (PATTERNS=1, no new UI) | pattern records, scene records (`SEC_SCN` 0x20, "keep" 0xFE), store rules (2.4), migration, scene launch through the assembled stage, pattern-state record, **flatten-on-read in every build**, snapshots v2, backup PTN1, the pending arena's per-id tables, `pat_*` core API | a FUNB project / sections / snapshot v1 / backup behave exactly as before on the device (same keys, same sound: render compare); conversion cut anywhere resumes; round trips PATTERNS <-> non-PATTERNS; MEM FULL and the reserve | 3 |
| **2** sequencer | per-track origin, launch queue (end / bar / now), ISR apply from the stage, song-mode launches until the next part, per-track motion, REC / undo rules | host sequencer tests (each mode, odd LEN / DIV mixes, swing phase, motion, a take across a switch, undo after a switch, a launch in song mode); emulator bench: ISR cost at a switch and idle | 2.5 |
| **3** device UI | PATTERN layer on LFO, the grid, black-key modifiers, KNOB cues, SAVE layer marks (amber) and white 15, messages | ui tests (keys -> actions, every message fits 232 px), emulator screenshots for the user's review | 3 |
| **4** web editor | the protocol version after v9, the pattern slots in the mixer strips and the scene column on MASTER (7.1), launch / copy / clear, popups on the playing pattern; 4b: read / write offline editing | web/test_web.mjs, e2e_daw.mjs against the mock and the emulator; an old editor on a new device, a new editor on an old device | 3 (+1.5 for 4b) |
| **5** builder | the PATTERNS item, `costs.json` measured, profiles set [D], docs (BUILDER.md, EDITOR_PROTOCOL.md, this file's [E] replaced by [M]) | verify.py on every profile and random configurations | 1 |

### 11.1 Phases 0 and 0b: what was built (feat/patterns-phase0, 2026-10-07)

**Phase 0, the log (`sec_log.c`)**, every build:

- `SLG_IDS` 24 -> **88**: ids 24..87 (`SEC_ID_PAT0` + 16 x track + slot) are indexed, counted in the MEM gauge and
  the reserve's model, and copied by compaction like sections. A pattern record written by a later PATTERNS build
  is never lost here: before phase 0, `slg_scan` sealed the rest of a sector at an id >= 24 and compaction dropped
  what it had not indexed (R3).
- A record of an id **past 87** (a firmware later still) is skipped and the sector read on (it was sealed: every
  later record of that sector unread). Not indexed, compaction does not keep it.
- **The index** keeps where a record is as a 16-bit offset from the log's base (`slg_at`, `slg_index`): 8 B an id,
  `slg` 292 -> 756 B [M]. `slg_model_t` (the reserve's model, three copies on the stack in `sm_more`) narrowed to a
  byte for the sector and 16 bits for the size: 268 -> 340 B [M].
- **Records with a flag this build does not know** (0x20..0x80) are refused by `sec_decode` (read as empty, kept in
  the log): a later firmware's scene (`SEC_SCN` 0x20) is never read as a section it is not.
- **Heal, hardened.** With 88 ids and more live data the host test found a state the 24-id log could reach too: a
  compaction cut on its last copy, the oldest sector nearly full of live records, the torn copy taking the room the
  rest needed, every other sector live: no sector could be emptied into the head, no spare at the next start, and
  every later write MEM FULL. `slg_heal` now falls back to `slg_unopen`: when every record the index finds in the
  head has a valid copy elsewhere (the head holds copies only: the compaction's originals are still in the oldest),
  the head is erased and the sector before it is the head again: the log as it was before that compaction, which the
  next write runs again. sec_log_test: 12 seeds of the cut tests over all 88 ids, a spare after every start [M].
- **Not in phase 0 (moved to phase 1):** the flatten-on-read of scenes (it needs the pattern and scene formats phase 1
  fixes; until then a scene reads as empty on a phase 0 firmware and is kept, section 2.7); the pending arena's
  per-id tables (phase 0 never writes a pattern); BK_LIST's "pattern ids" switch (with the protocol version after v9).

**Phase 0b, codec B for sections (`sec_codec.c`)**, every build:

- A step is a mask of its non-zero bytes, then those bytes: one mask byte (bytes 0..6; bit 7: a second mask byte
  follows with bytes 7..9: vel, lvl, rat on a synth step; the last levels and ratchets on a drum step), then the
  non-zero bytes. A record uses B when its steps take fewer bytes than A's 10 a step (dense random steps do not: A, or
  raw, as before). Codec A writes byte for byte what the encoder wrote before [M: 2,000 random projects, the old and
  the new encoder, the same hash].
- **The flag is `SEC_RAW | SEC_B` (0x11).** A firmware before 0b takes 0x11 for a raw record and checks the raw
  length, which a compressed record never has: it refuses the record (reads the section as empty, keeps it) and
  never decodes B steps as 10-byte ones [M: 6,000 codec B records, with and without motion chunks, fed to the old
  decoder: 0 taken]. A raw record says 0x01 alone; 0x10 alone is not a record.
- **The reserve is unchanged**: the longest record is still the raw one with a full motion chunk, `SEC_REC_MAX`
  4,059 B (a dense project stays raw in either codec). The MEM gauge needed no change: it counts the records' real
  lengths (`slg_live_bytes`, `sm_more` with the last stored size), now smaller.
- **Migration is lazy**: every codec A record reads; a section is rewritten in B when it is stored again. Snapshots
  (`SNR_SEC`, `SNR_WORK`) and backups (S01..S16) carry the records as they are; an older backup restores (its codec A
  records decode, `bk_commit_sec`), a new one restores on this firmware, and on a firmware before 0b its sections are
  refused (rc 2) rather than misread.
- **The editor's backup reader** (`secTracks`, web/editor.html) reads both forms and refuses unknown flags;
  web/EDITOR_PROTOCOL.md documents the flag. tests/emu_snapshots_verify.py tells compressed from raw by 0x11.

**Sizes [M]** (record bytes without the 16-byte log head; `tools/patterns_measure`, tests/sec_codec_test.c):

| Section | codec A (before) | codec B (now) |
|---|---|---|
| power-on | 70 B | 70 B (no steps: A) |
| demo (acid 16, chords 32, lead 12, drums 16) | 698 B | **348 B** |
| typical (16 steps a track, a few edits) | 504 B | **256 B** |
| busy 64-step | 2,264 B | **1,072 B** |
| dense worst (64 random full steps x 4) | 2,712 B | 2,712 B (A kept) |

**Capacity [M]** (the real log, 32 KiB, the reserve kept): busy 64-step sections **6 -> 16 of 16**; 16 sections now
fit up to 1,344 B each (before: up to ~1.3 KB too, but busy ones were 2,264 B).

**Flash, RAM, pool, RAMTEXT per profile [M]** (`tools/optimist.py build`, optimist 2d600b6 -> this branch):

| Profile | Flash | RAM (.data + .bss) | Pool | RAMTEXT |
|---|---|---|---|---|
| user-default | 568,960 -> 569,728 (+768) | 74,356 -> 74,820 (+464) | 306,860 (=) | 29,308 (=) |
| fm-va-studio | 562,988 -> 563,660 (+672) | 89,572 -> 90,020 (+448) | 321,680 (=) | 26,916 (=) |
| drum-machine | 563,592 -> 564,280 (+688) | 85,656 -> 86,104 (+448) | 331,028 (=) | 23,712 (=) |
| everything-that-fits | 568,628 -> 569,412 (+784) | 77,716 -> 78,164 (+448) | 327,340 (=) | 32,492 (=, 20 B free) |
| x0x-drums | 540,472 -> 541,256 (+784) | 69,744 -> 70,208 (+464) | 293,484 (=) | 23,868 (=) |

The codec is not RAM code (it runs in the main loop: sections stored, staged, loaded); everything-that-fits keeps its
20 B of RAMTEXT.

**Tests [M]**: tests/sec_codec_test.c (demo, typical, busy, dense, every engine x preset on the demo's patterns, 2,000
random projects: load -> store -> load gives the same bytes, codec A records migrate to the same bytes, an older
firmware refuses B; 20,000 mutated B records decoded without a fault, also under `-fsanitize=address,undefined` by
hand; cut records, unknown flags refused), tests/sec_log_test.c (64 pattern records beside 16 sections kept across
restarts and 300 compacting stores; a later firmware's id skipped; 16 busy codec B sections + 16 pattern records
under 300 cut stores, every section decoding to its project after each start, the spare kept), web/test_web.mjs
(codec B records in the backup report), `make test` on the five profiles (all host tests pass, every golden
unchanged; snapshots_test and sections_test store sections with steps, so their byte-exact snapshot and section
round trips run on codec B records), tests/emu_snapshots_e2e.sh on a user-default build: every check PASS, as on
optimist (its sections are made at the panel without steps: codec A records there).

**Risks**

- R1 **RAMTEXT** on everything-that-fits (20 B left; code generation moves it by tens of bytes): PATTERNS is off
  there [D]; phases 0 and 0b left it unchanged [M].
- R2 **Flash on user-default** (11.8 KB free after phase 0 against ~9-13 KB): measured at phase 5; fallback: drop the
  black-key DUPLICATE / scene-launch extras, or leave user-default off [D].
- R3 **Downgrade to a pre-phase-0 firmware**: its `slg_scan` seals the rest of a sector at an id >= 24, so it would
  lose the later records of each sector holding a pattern, and its compaction would then drop them. Phase 0 ships
  first and long before PATTERNS; the release notes say "back up before going back"; the backup keeps everything.
- R3b **Downgrade to a pre-0b firmware** after sections were stored in codec B: those sections read as empty there
  (refused, never misread) and stay in the log; back on a 0b firmware they play. Release notes: back up first.
- R4 **Semantics**: the working copy [D] and shared patterns edited explicitly (STORE into a slot every scene using it
  hears); the "*" mark and the undo level are the safety net.
- R5 **Log capacity with busy material**: 62 busy patterns beside 16 busy scenes at 32 KiB; MEM FULL + CLEAN.
- R6 **SLOOP 2.4's formats** are unknown; chunks are the escape hatch.
- R7 Older editors' backups lack `PTN1`: the restore report names it.

## 12. Open questions

Answered on 2026-10-07 (section 0a): 1 working copy + STORE; 2 LFO held, LFO + HOME locks; 3 at the pattern's end by
default; 4 sixteen a track; 5 keep 32 KiB; 6 launches allowed in the song, until its next part; 7 "keep" yes; 8 the
profile defaults of section 10.3; 9 amber for anything queued; 10 phase 0b now (done); 11 pattern launches in SONG REC
later.

Still open (proposals in the text):

1. The modifiers for the other launch timings: OCT- = next bar, OCT+ = now (legato), as proposed in section 5.2?
2. Song-mode launches: the track's own staging buffer (+0.8 KB pool, section 4.1), or launches in song mode only at
   the pattern's end inside the current part (no buffer: staged in the stage's copy of the next part)?

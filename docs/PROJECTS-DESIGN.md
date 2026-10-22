# Projects own their scenes and patterns (PROJECTS)

User ruling (2026-10-22): **the current state of a project is independent of the patterns and scenes.** Scenes and
patterns belong to a project, not to the device. Before this, a project slot *was* a scene whose steps lived in 16
device-wide pattern slots a track, shared by every scene (`docs/PATTERNS-DESIGN.md`): any pattern operation could change
what a project loaded, and a project whose references were rewritten loaded with empty tracks and said LOADED.

Builder item `PATTERNS` ("per-project patterns and scenes"). Without it nothing changes: a project is a plain section
record with its steps inside, in the old log at 0x97000.

## 1. The model

- The device has **16 projects** (the PROJECT page's keys). A project holds, by itself:
  - its **state**: the working copy as last saved (sound, mix, FX, LFOs, automation, the four tracks' steps);
  - its **scenes** A..P, each naming one of the project's pattern slots a track (or "-", or KEEP);
  - its **patterns**: 16 slots a track;
  - its **song**: the chain of parts (scene, bars), the scenes' BARS, the scene names.
- **One project is open** at a time. PROJECT page LOAD opens a project: its state becomes the working copy, and SONG,
  the pattern layer and the editor show its scenes, patterns and song. Nothing outside the open project is readable
  or writable; nothing done in one project touches another.
- PROJECT page SAVE writes the state (NAME first, as today). Opening another project while the work changed asks
  SAVE first (the existing SAVE / HOME flow; stopped only). NEW PROJECT clears the whole project — state, scenes,
  patterns, song — asked. DELETE on the PROJECT page: the same.
- Inside a project scenes still share the project's pattern slots exactly as `docs/PATTERNS-DESIGN.md` describes
  (copy-on-write when a scene is stored, `T2: NO FREE PATTERN`, launches, SONG). Only the universe shrank to the project.
- The device autosave (the working copy, `OBJ_AUTOSAVE`) stays device-wide: a power cut loses nothing, as today.
- What is on a device today (16 scenes sharing patterns, one song) is **not migrated** (the user: "we can wipe"). The
  old log at 0x97000 is left as it is; a non-PATTERNS build still reads it.

## 2. Storage: one log, every record tagged with its project

`sec_log.c` keeps its design (sectors with a head, records that never span a sector, one spare sector, compaction of
the oldest sector, tombstones). Format 2 ("SLG2" sector magic) changes the record head's two spare bytes:

| head (16 bytes) | |
|---|---|
| magic u16, id u8, state u8 | as before (`"SE"`, the id, 0xFF written / 0x00 done) |
| seq u32, len u16 | as before |
| **proj u8** | 0..15 the project, 0xFF device-wide (the working copy's companions, the project names, the open project) |
| **dead u8** | 0xFF live, 0x00 superseded (programmed once a newer record or a tombstone of the same (proj, id) is done) |
| crc u32 | over id, seq, len, **proj** and the bytes |

- **Area** with `PATTERNS`: `SEC_LOG_BASE` 0xA0000, `SEC_LOG_SECTORS` 32 (128 KiB) when neither `ENG_SAMPLE` nor
  `DRUM_USR` is built, else 20 (80 KiB: user sample slot 1, which the samples lose). The index keeps a record's place
  as sector << 10 | offset / 4 (16 bits, up to 64 sectors).
- **Ids** keep today's numbers, scoped to a project: 0..15 scenes, 16 the song chain (whole, no tag: nothing of it in
  the settings record), 17 the tracks' sources, 18 the scenes' BARS, **19 the state** (a plain section record, the
  steps in it; its extras, FX and LFO records at **139 / 140 / 141**, `SLG_IDS` 142), 24..87 the patterns, 88..103 /
  105..120 / 122..137 the scenes' extras / FX / drum lane LFO records, 104 / 121 / 138 the working copy's (the device
  autosave's companions, written in the open project's scope). Device-wide (proj 0xFF): **21 the project names** (the
  PROJECT page's; scenes have no names). **Which project is open is the settings record's** (`persist_t.open_proj`,
  appended to PER4; a record from before reads as project 1): opening a project must never need room in the log.
- **The index holds the open project** (and the device-wide ids): `slg.at / aseq / alen / tomb[SLG_IDS]` as today, so
  `sections.c`, `pat.c`, the records' logs and the editor keep their calls. Opening a project walks the sectors again
  with the new filter (a few ms stopped). Boot walks once for the sector order (and which projects hold something:
  `slg.pused`, the PROJECT page's strip), then opens the settings' project.
- **Compaction and the reserve** see every project: a record is live when state is 0x00, dead is 0xFF and its CRC
  holds. The walk keeps each sector's live bytes of the *other* projects (`slg.fliv[]`); the room model (`sm_*`) adds
  them to the open project's. Compacting the oldest sector copies the other projects' live records as they are; a
  record superseded by a newer one in the same sector (its dead byte never programmed: a cut) is found by the sector's
  own records and dropped, so nothing comes back. The open project's stale records found by a walk are marked dead.
- The pending arena (`sec_pend`, `.noinit`) is the open project's; it is written (or dropped) before another opens.
- The old log (0x97000, "SLG1", 8 sectors) is neither read nor written by a PATTERNS build. With samples built, user
  sample slot USR1 reads as empty and takes nothing (`eng_sample.c SMP_USER_CAP`); without them, the log also covers
  USR2's room: sample data left there by another build is lost (the user's trade-off, 2026-10-22).
- Fixed on the way (both formats): a log whose every sector held a live record could refuse even a clear (MEM FULL
  forever). A tombstone's write now lets the compaction drop that id's old record instead of copying it
  (`slg_make_room` `drop`; the room model mirrors it): a clear always goes in, and a cut there leaves the id cleared.
- The calls: `project_scope_set(p)` (the index p's, the arena written first; the chain, BARS, sources read),
  `project_open(p)` (+ the state into the work; 0: none, the UI blanks the work), `state_save()`, `project_clear(p)`
  (every record of p: tombstones), `project_slot_used(p)`. Format 1 keeps `project_load / save` as the sections'.

## 3. Device, editor, tests

- PROJECT page: LOAD opens (the work changed: "SAVE PROJECT n?" first — YES saves the open project then opens, HOME
  cancels as every question does (the user, 2026-10-22); `op_prjflow.c PO_SAVE_LOAD`), SAVE saves the state (NAME first; another slot: that project
  is the open one from then on), DELETE clears the whole project, NEW blanks the work and clears the open project
  (stopped). SONG, the pattern layer, the session grid: the open project's. `op_edits.c prj_load_go / prj_save_go`
  are the one way in (the deferred load / save while playing too).
- Open: snapshots carry log ids 17..87 (`snapshots.c SN_LOG0..`), the state (19) and the project names (21) among
  them now: a snapshot restore writes them back. To decide: leave 19 and 21 out of a snapshot.
- Web editor: `PAT_LIST`, launches, `SCENE`, `PAT_OP`, `PAT_READ` / `WRITE` unchanged (the open project). Backup objects
  become per project: `PJ01`..`PJ16` (kind 5 packs of the project's records: u8 id, u16 length, the record), plus the
  device-wide ids in `GLOB`; `S01`..`S16`, `PTN1`..`PTN6`, `SNG1`, `FX31`..`FX33`, `DLF1`/`DLF2`, `XSTP` are listed
  as not in a PATTERNS build (an older backup's are restored into project 1).
- Tests: `tests/sec_log_test.c` (format 2, two projects' records interleaved, a switch, dead marks, compaction across
  projects, cuts at every program), `tests/patterns_test.c` and `tests/sections_test.c` (scoped), `tests/project_scope_test.c`
  (open / save / new / delete, the arena across a switch, a project's load after another's every operation),
  `tests/ui_optimist_test.c` (PROJECT, SONG), the backup tests.

## 4. Numbers that matter

- Usable room: 32 sectors ≈ 108 KiB, 20 ≈ 66 KiB (today 8 ≈ 24 KiB). A busy project (state, 8 scenes, 24 patterns)
  is about 6..8 KiB.
- RAM: the index is unchanged (the open project); `fliv[]` is 4 bytes a sector. Flash cost of the item: measured
  when built (`tools/builder/costs.json`).

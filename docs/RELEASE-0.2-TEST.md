# Optimist 0.2: hardware pass on the FM-1

Nothing here has been run on an FM-1. User flashes, Claude never does. Back the FM-1 up first (README.md, "Getting back" is the way out: OCT- held alone while switching on = OPTIMIST USB RESCUE).

## Emulator pass 2026-10-24

Every checklist line the FM-1 emulator can run was run there (fm1-emulator 9b65532 with tests/emu/emulator-wide-ops.patch, 96 MHz for the checks, 192 MHz for the engine sweeps). It ran on the **42a5aee9 packages** (sha1 A 1a7559fb, B 0dc104a4, C abc69982, D 67a5b33b); the packages in the table below (d0e9fb6b) only had the `boot` check (10/10 each, main session). Each line below carries `emulator: pass (...)`, `emulator: FAIL (...)`, `emulator: not run (...)` or `hardware only (...)`.

| | Lines |
|---|---|
| emulator: pass | 67 (31 of them in part: the rest of the line not run, or by ear / on the FM-1) |
| emulator: FAIL | 0 |
| emulator: measured, not judged | 1 (levels) |
| emulator: not run (could run, no check written) | 9 |
| hardware only | 22 |

Checks (tests/emu, `make emu-check ONLY=...`): `boot`, `autosave`, `upfm6`, `timing` (all four packages: all pass, see section 1) and the new panel checks `project`, `mute`, `inserts`, `stepcards`, `livesave`, `kits`, `grids`, `song`, `chord`, `plock`, `missing` (A, B, C, D: all pass). Engines: tests/emu/sweep.py on A, B, C (244 preset plays, ANALOG in all three; 0 faults, 0 silent, 0 full-scale, 0 late halves, 0 resets); ANALOG 2's models: sweep.py --a2 --a2-at 23 on A, B, C. Logs, WAVs and screenshots: build/emu-sweep/runs/ (git-ignored): check-<pkg>, panel-<pkg>, panel3-<pkg> (inserts with the final bars; missing on C), panel2-B-engines (kits on B; the first runs of inserts on B C, kits on B and missing on C failed on the checks' own bars, fixed in them, not in the firmware), sweep-<A|B|C>, a2-<pkg>.

Found, for the user to decide (no firmware fault seen):

- A as built has no SFLD and no RING insert (the A insert list read from the running firmware: ---- CHRS DST DYN EQ3 FILT FLNG FOLD FUZZ GATER HARM JUNO RECT SUB WIDE XDLY; its felucca_config.h: INS_SFOLD 0, INS_RING 0), though this page said it held them; today's A leaves them out on purpose (section A updated).
- GRIDS VEL OFF plays NORM and HARD hits (gen.c gr_kit_merge: HARD over 192, else NORM), not "all hits HARD" as section 5 says.
- PROJECT SAVE into a slot other than the open project gives that project the work but not the scenes and patterns stored before (sections.c project_scope_set / op_edits.c prj_save_go: the new slot's own, empty ones). The first draft of the `song` check saved scenes A and B as project 9 and found them empty after LOAD 9; they stay with the project that was open. By the code's comment this is meant; it can read as lost scenes.
- PRESETS reaches 6 WAVE presets (ids 1 2 4 6 10 12), not 13; SAMPLE and GRAIN 1 each; FM6 3 (its INIT voices; the voice packs were not reached by the sweep).
- The header's MISSING message names the first missing item and counts the rest ("MISSING: FM6 T1 +1" for FM6 and the X0X 909 kit).

What only the FM-1 can show (the user's list):

1. First boot from the real flash of each package (the LZ / range-coded boot stream, the XIP user presets, the flash chip's timing) and the power-cycle; the emulator boots them all.
2. Everything Bluetooth: BLE MIDI, BT audio (AirPods, XM4), both at once, AFH, pairing, D's `btpage` console, and, new since 42a5aee9: BT MIDI clock sync both ways, four remembered devices for BT audio and BT MIDI, the Mirror app over Bluetooth (INFO / PANEL / NAV).
3. USB to a computer: USB audio, the web editor (Backup, Library), the Screen tab, backup and restore.
4. By ear: each engine's character, ANALOG 2's models (ladder, OTA, diode, MS-20), KAWA / PLAITS sounds, chorus / flanger / TAPE / SPIT quality, DRONE's BEAT, XDLY's feedback clip, no crackle.
5. CPU LOAD and UNDERRUNS on SYSTEM > INFO with PHYS (3 voices), WAVE + drums, the four filter models (the emulator's times are one instruction a cycle, not the chip's).
6. The keys' feel, LEDs and key lights (mute mode's three lights), battery.
7. New since 42a5aee9, never run anywhere: ANALOG 2 FLT B, the UPB4 preset bank, editor protocol 7, the default tempo 111.

## The packages

Packages are in `/Users/mots/GitHub/optimist/build/release-pass/` (git-ignored), all four built from `d0e9fb6b` on `optimist` (today's features, rounds 7 and 8, the Mirror over Bluetooth); each passes the emulator's `boot` check 10/10. Each has a `-ui.zip` beside it (the web editor's sidecar) and its ELF (`*.elf`, for the emulator). The configs are `config/release-pass/*.config`. Slot limits: flash 581,564, RAM 98,304, pool 344,064 (8,192 of it kept spare), RAM code 32,512.

| Package | File | Flash | RAM | Pool | RAM code | sha1 |
|---|---|---|---|---|---|---|
| A: mots | `A-mots.fwsc` | 580,996 | 91,944 | 331,768 | 27,596 | 0e3a806111a4e4d011ceb22d86f2f9763b36e025 |
| B: engines | `B-engines.fwsc` | 519,932 | 84,788 | 298,252 | 31,240 | 049b0c4ae2265c80811abb030a00a2d8a94da33c |
| C: wave | `C-wave.fwsc` | 508,040 | 73,636 | 299,424 | 30,188 | 101d01a81b4df0cc84ec1ca1845c52ad7a643aba |
| D: btdiag (optional) | `D-btdiag.fwsc` | 563,720 | 89,612 | 324,796 | 28,288 | 59f0ca185e9e1003423c04ab32bdb93f4854de94 |

Flash, RAM and pool are the exact build's numbers; "RAM code" is the build's `.ram_text` + `.ram_hot` total, the figure the builder reports as ramtext.

### A: mots (the daily build; also the Bluetooth, USB audio, web screen and inserts package)

`config/release-pass/A-mots.config` is the user's config as it is, with PHASE_FOLD, FM6_SYSEX, INS_SFOLD and INS_RING off.

Holds: engines ANALOG 2 (filter models LADR, OTA, DIOD, MS20), DIGITAL, PHASE, FM6 (5 voice packs, 80S included), PLAITS, PHYS, ACID, JUNO. Drums: the 32 synth kits, KAWA (47), PLAITS (48), X0X 808 and 909, drum EDIT. GRIDS with EUCL, LFO and VEL. BLE MIDI + BT audio + BT_AFH. Screen mirror (ED_MIRROR, USB and Bluetooth). USB audio. Inserts FOLD FUZZ WIDE SUB GATER RECT HARM CHRS JUNO-CHORUS FLNG XDLY (no SFLD, no RING). LIVE_SAVE.

First on an FM-1 in A, never booted on one before: the LZB boot stream (the packed tables decoded at boot, now three blocks), the user preset banks read in place through XIP (storage/upreset.c UP_XIP), the Bluetooth audio queue at 40 packets with slots the packet's length, AFH. If A does not boot: OCT- held alone while switching on is the rescue (README.md, "Getting back").

Effects memory in A is set to 64 KB, which is the delay's whole line: the builder says CHRS, JUNO CHORUS, FLNG and XDLY get room for 0 instances at once, and +9 (CHRS, JUNO, FLNG) or +1 (XDLY) only while the delay is in no master insert slot. So in A the line-based inserts only work with the delay taken out of the master's slots; FOLD, FUZZ, WIDE, SUB and GATER need no line and work as usual. Test CHRS and XDLY in B. (Emulator, 42a5aee9's A: CHRS, JUNO, FLNG and XDLY refused on T1, T2 and T3 with the delay in the master's slots: struck in the TYPE list, KNOB 1 passes over them.)

### B: engines (the other twelve engines, TAPE and SPIT)

`config/release-pass/B-engines.config` = A-mots with: FM6, PHYS, ACID, JUNO, PLAITS, X0X kits, USB audio, BLE, BT audio and BT_AFH off, and these on: SLICE, TRIO, NOISE, DRONE, CZ, STRINGS, VPS, TERRAIN, WSEQ, VECTOR, SID, PROPHET, INS_TAPE, INS_SPIT, effects memory automatic (DLY_ARENA 0).

Holds: engines ANALOG 2 (four models), DIGITAL, PHASE, SLICE, TRIO, NOISE, DRONE, CZ, STRINGS, VPS, TERRAIN, WSEQ, VECTOR, SID, PROPHET. Drums: 32 synth kits, KAWA, PLAITS, drum EDIT; GRIDS with EUCL, LFO, VEL. Inserts FOLD FUZZ WIDE SFLD SUB TAPE SPIT GATER CHRS JUNO-CHORUS FLNG XDLY. Screen mirror over USB only. LIVE_SAVE. No Bluetooth, no USB audio.

Effects memory (builder notice): delay line 64 KB plus 69 KB; CHRS, JUNO CHORUS, FLNG up to 9 at once, XDLY 1.

### C: wave (WAVE, with a short delay line)

`config/release-pass/C-wave.config` = A-mots with: PHYS, ACID, JUNO, PLAITS, DIGITAL, PHASE, X0X kits, XDLY, USB audio, BLE, BT audio and BT_AFH off; WAVE, LOFI, SAMPLE (with the VIBES set), VOICE, WHEEL, GRAIN, INS_RECT, INS_HARM, INS_RING, FM6_PK_80S on; delay line 32 KB (DLY_LEN 16384), effects memory automatic.

Holds: engines ANALOG 2 (four models), FM6 (80s pack only), WAVE, LOFI, SAMPLE, VOICE, WHEEL, GRAIN. Sample set VIBES only (GRAIN's presets use it). Drums: 32 synth kits, KAWA, PLAITS, drum EDIT; GRIDS with EUCL, LFO, VEL. Inserts FOLD FUZZ WIDE SFLD RECT HARM RING SUB GATER CHRS JUNO-CHORUS FLNG. Screen mirror over USB. LIVE_SAVE. No Bluetooth, no USB audio, no XDLY.

Effects memory (builder notice): delay line 32 KB plus 5 KB; CHRS and JUNO CHORUS up to 2, FLNG up to 5 at once.

A first try of C with the 64 KB delay and automatic effects memory overflowed the pool by 89,260 B; WAVE's 73 KB of pool is why the delay is short here.

### D: btdiag (optional, only for the AFH check in section 6)

A without USB audio and without the X0X 909 kit, with the USB serial console and BT_DIAG (btrx, bttrace, btpage commands). The console and USB audio share the USB endpoints, so one excludes the other. With the 909 left in it was 16,560 B over the slot. A BT_DIAG build changes AFH behaviour on purpose: it switches only when told (`btpage afh=1`), so D tests AFH by command, A tests it by ear.

### Where the 26 engines are

| Engine | Package | | Engine | Package |
|---|---|---|---|---|
| ANALOG 2 + LADR OTA DIOD MS20 | A B C D | | TERRAIN | B |
| FM6 | A C D | | WSEQ | B |
| JUNO | A D | | VECTOR | B |
| ACID | A D | | WAVE | C |
| CZ | B | | SID | B |
| PHYS | A D | | PROPHET | B |
| DIGITAL | A B D | | SLICE | B |
| PHASE | A B D | | NOISE | B |
| LOFI | C | | DRONE | B |
| SAMPLE | C | | PLAITS | A D |
| VOICE | C | | STRINGS | B |
| TRIO | B | | VPS | B |
| WHEEL | C | | GRAIN | C |

Nothing was left out.

## How to read the checklist

"Pass" is stated after each line. If a line fails, note the package, the engine or page, what you pressed and what the screen showed. Run each package's checks in this order: boot (1), engines (2), the package's extras (3 to 8). The `emulator:` notes under a line are the 2026-10-24 pass (42a5aee9 packages).

## 1. First boot (every package, first)

No build since commit `032f02a4` (the LZ-packed boot stream, decoded in `fm1_cstart`) and `23be06fb` (the same stream range-coded) has booted on an FM-1. The first boot of each package is itself a test.

- [ ] A: flash it, power-cycle. Pass: the Optimist screen comes up, keys play, no reboot loop within 2 minutes.
  - emulator: pass (`boot` 10/10: no crash record, boot guard cleared at 34 s, 172 halves/s, late 0, a note RMS 0.0174; `autosave` ran 4+ minutes over three power-ups, one boot each, A). The real flash: hardware only.
- [ ] B: same. Pass: as A.
  - emulator: pass (as A: `boot` 10/10, `autosave` 7/7, B).
- [ ] C: same. Pass: as A.
  - emulator: pass (as A: `boot` 10/10, `autosave` 7/7, C). D too (`boot` 10/10, `autosave` 7/7, `upfm6` 8/8).
- [ ] Any package: SYSTEM > INFO shows UNDERRUNS 0 or near it with nothing playing; the header shows no red/amber drop mark at idle.
  - emulator: pass (late halves 0 over 34 s idle in A B C D, felucca_dbg; `timing` 48, 96, 192 MHz: late 0 in all four, the dual core's jobs all done). INFO's counter itself was not read; the chip's timing: hardware only.
- [ ] After each flash, a power-cycle (off, then on) once more. Pass: it boots again from the flash the same way.
  - emulator: pass (`autosave`: the work back as autosaved after a power cycle, nothing written at idle; `upfm6`: A C D, the FM6 user voices moved off the SDK VM once, 0xE7000..0xE9FFF untouched). The real flash chip: hardware only.

If a package does not boot, report: which package (A, B or C); the LEDs (which are lit or blinking); what the screen shows (dark, garbled, a message); and whether OCT- held alone while switching on brings OPTIMIST USB RESCUE. The commits to revert, alone, one at a time, on a fresh branch from `rel/hw-pass`: `23be06fb` (the range coder over the stream) first, rebuild, try; then `032f02a4` (the LZ stream; its link fix is `9f76f17a`). A dead boot on all three packages points at these two. A dead boot on one package only points at that package's items.

## 2. Engines (one block per engine)

For each engine, on a track set to it in the package that holds it (table above):

- [ ] Presets: PRESETS through the first three. Pass: each makes a sound, different from the last, no click or silence.
  - emulator: pass (sweep.py, 192 MHz: every preset PRESETS reaches, SHIFT2 + PRESETS to each engine; A 78, B 119, C 47 presets (ANALOG counted in each); each: a note and a 4-note chord sound, no sample at full scale, no step > 0.5, no late half, no reset; levels per engine below). Clicks by ear: hardware only.
- [ ] Chord: hold a 4-note chord. Pass: all 4 notes sound, release ends them, no stuck note.
  - emulator: pass (sweep.py: a 4-note chord per preset, then 0.4 s after the release; tail RMS at most 0.08 of full scale (WSEQ, TERRAIN pads, long releases); that 4 notes are heard and SID drops one were not measured).
- [ ] P-lock: on the SEQ layer hold a step, turn PRESETS (or ALGORITHM to pick another value, then a knob). Pass: that step sounds different from the others on the next pass and the next step is back to normal.
  - emulator: pass on ANALOG only (`plock`, A B C D: step 5 held, ENV, ATK locked: step 5's note 0.0014 RMS on both passes, steps 1 9 13 0.011..0.021; the track's values untouched). Note: on a held step PRESETS moves the step and ALGORITHM its pitch (design doc 4.2), and EDIT is no lock page on a synth track; ENV FX SCL ARP LFO are. Other engines: not run.
- [ ] LFO 2: LFO button, LFO 2, destination CUT (or an engine value where the engine has no cutoff), depth up, RATE slow. Pass: the sound moves audibly with the LFO.
  - emulator: pass on ANALOG only (`plock`: LFO2 DEST DST A FLT, AMT A 111: a held note's brightness spread 0.25 -> 0.53, A B C D). Other engines: not run.
- [ ] SAVE then LOAD: SAVE the project, change the engine's sound, LOAD it. Pass: the saved sound comes back, same engine, same preset.
  - emulator: pass on ANALOG (`project`: saved through NAME, power cycle, NEW, LOAD: tempo, engine, preset, FLT 2 MODEL and the name back; `livesave`: the sound values back; A B C D). Other engines: not run.
- [ ] Insert: INSERTS > S1 CHRS on the track (or the engine's typical one: FOLD on ACID or PHYS). In A use FOLD, FUZZ or WIDE (see "Effects memory" under A). Pass: audible, no crackle, S1 off removes it.
  - emulator: pass on ANALOG (`inserts`: every type the slot takes changes the waveform past its bar, no full-scale sample, S1 back to ---- gives the dry take sample for sample; A B C D). Crackle by ear: hardware only.

Engine lines (tick the engine once its six checks pass; the six are the list above). Emulator: presets (count) with the chord RMS spread across them and the slowest audio half at 192 MHz (of 5,804 us; one instruction a cycle, not the chip):

- [ ] ANALOG 2 (A, B, C, D; run it fully in A)
  - emulator: pass (24 presets, 7.4 dB, 1,459 us; all six lines above, A B C D).
- [ ] FM6 (A, C, D)
  - emulator: pass (3 presets: its INIT voices; the voice-pack voices not reached by the sweep; 6.8 dB, 1,361 us; A C).
- [ ] JUNO (A, D)
  - emulator: pass (8 presets, 14.2 dB, 1,781 us; A).
- [ ] ACID (A, D)
  - emulator: pass (4 presets, 2.0 dB, 1,302 us; A).
- [ ] PHYS (A, D; heavy: 3 voices of its heaviest preset, note SYSTEM > INFO CPU LOAD and UNDERRUNS)
  - emulator: pass (9 presets, 16.2 dB, 2,640 us, 45 %; A). CPU LOAD with 3 voices: hardware only.
- [ ] DIGITAL (A, B)
  - emulator: pass (13 presets, 5.4 dB, 1,379 us; A B).
- [ ] PHASE (A, B; also the FOLD value on E7)
  - emulator: pass (5 presets, 5.8 dB, 1,417 us; A B). FOLD on E7: not run (PHASE_FOLD is off in today's A).
- [ ] PLAITS (A, D; MODEL, HARMONICS, TIMBRE, MORPH)
  - emulator: pass (12 presets, 10.8 dB, 3,202 us, 55 %; A). The four knobs: not run.
- [ ] CZ (B)
  - emulator: pass (8 presets, 10.7 dB, 2,028 us).
- [ ] SLICE (B)
  - emulator: pass (4 presets, 11.6 dB, 1,666 us).
- [ ] NOISE (B)
  - emulator: pass (4 presets, 15.0 dB, 1,476 us).
- [ ] DRONE (B; BEAT with headphones, HOLD latches the keys; presets must start with HOLD off)
  - emulator: pass (9 presets, 17.7 dB, 1,663 us; 0.4 s after each release the tail RMS is at most 0.012 (none held on); whether the presets start with HOLD off: not read). BEAT: hardware only (headphones).
- [ ] STRINGS (B)
  - emulator: pass (5 presets, 6.6 dB, 2,777 us).
- [ ] VPS (B)
  - emulator: pass (6 presets, 9.9 dB, 1,819 us).
- [ ] TERRAIN (B)
  - emulator: pass (6 presets, 19.3 dB, 1,517 us).
- [ ] WSEQ (B; it follows the song's tempo)
  - emulator: pass (6 presets, 10.6 dB, 1,654 us). Tempo following: not run.
- [ ] VECTOR (B)
  - emulator: pass (6 presets, 13.7 dB, 1,427 us).
- [ ] SID (B; three voices a part: the 4-note chord drops one)
  - emulator: pass (8 presets, 36.1 dB: NOISE ZAP and C64 ARP chords at 0.001 / 0.003 RMS, quiet not silent; 1,433 us). The dropped voice: not measured.
- [ ] PROPHET (B; five voices)
  - emulator: pass (7 presets, 29.9 dB: P5 CLAV 0.002 RMS, P5 SYNC LEAD 0.004; 2,701 us).
- [ ] TRIO (B)
  - emulator: pass (8 presets, 7.6 dB, 1,602 us).
- [ ] WAVE (C; 13 presets; one table shared by every WAVE track, so set two WAVE tracks and see both play the first one's table)
  - emulator: pass (6 presets reached by PRESETS, not 13: ids 1 2 4 6 10 12; 5.7 dB, 2,153 us; the emulator patch's e1fe op runs here). Two WAVE tracks: not run.
- [ ] LOFI (C)
  - emulator: pass (3 presets, 5.5 dB, 1,011 us).
- [ ] SAMPLE (C; the VIBES set)
  - emulator: pass (1 preset reached, STRING STB, 1,253 us).
- [ ] VOICE (C)
  - emulator: pass (4 presets, 6.6 dB, 1,767 us).
- [ ] WHEEL (C)
  - emulator: pass (5 presets, 0.8 dB, 1,797 us).
- [ ] GRAIN (C; presets read the VIBES set)
  - emulator: pass (1 preset reached, GRAIN PAD, 1,772 us).

Two more checks on the heaviest package, C with WAVE: [ ] four notes held on a WAVE track plus the drum track playing: SYSTEM > INFO shows no UNDERRUNS counting up; [ ] SYSTEM > INFO's CPU LOAD with PHYS in A (3 voices) stays under the red mark.
  - emulator: not run (WAVE + drums: no check written); hardware only (CPU LOAD's red mark is the chip's time).

## 3. ANALOG 2 filter models (A, with B and C as a second look)

On an ANALOG track, FLT 2 gets a MODEL card next to FTYP. Each model with RES up to about 3/4, CUT swept slowly, then one key held:

Emulator (sweep.py --a2 on SYN BRASS, A B C, the same in each): every model in LP12 LP24 BP HP NOTCH sounds (chord RMS 0.003..0.018), no full scale, no late half; slowest half SVF 1,418, LADR 1,534, OTA 1,569, DIOD 1,888, MS20 1,534 us. On 808 BOOM (a sub-bass) BP and HP are silent in every model, SVF included.

- [ ] LADR. Pass: a ladder: bass thins as RES rises, ringing in tune at the top. No crackle, no stuck note.
  - emulator: pass (sounds, 5 modes; the character: hardware only, by ear).
- [ ] OTA. Pass: creamy, input stays clean, the resonance saturates, self-oscillation round.
  - emulator: pass (sounds; character by ear: hardware only).
- [ ] DIOD. Pass: squelchy, thin vocal resonance at the cutoff.
  - emulator: pass (sounds; character by ear: hardware only).
- [ ] MS20. Pass: screams at high RES, rings loud; LP BP HP NOTCH each distinct; LP24 steeper.
  - emulator: pass (sounds, the 5 modes at different levels: BP 0.0115, HP 0.0060, LP12 0.0143 chord RMS; screams by ear: hardware only).
- [ ] Each model with 4 notes held and SYSTEM > INFO: CPU LOAD noted (about 3x ANALOG 2's own filter per voice on the host; never measured on the FM-1). Pass: no UNDERRUNS counting up; if it does, say which model.
  - hardware only (CPU LOAD; the emulator's 3-note times above: DIOD the slowest, 1.33x SVF).
- [ ] In B and C (same four models are built): switch one model, SAVE, LOAD. Pass: the model is kept.
  - emulator: pass (`project`: LADR LP24 set in RAM, saved, power cycle, NEW (0), LOAD: 6 again; A B C D).

## 4. Drum kits KAWA and PLAITS (A, B, C)

- [ ] KAWA: drum track, kit 47 (KAWA). Play each lane. Pass: guiro, clap, flammed snare, brush, shaker, hats, ride, toms, cowbell, kick sound distinct, none silent.
  - emulator: pass (`kits`: each lane's preview sounds (peak 0.017..0.073), no two alike; A B C D). Which instrument each is: hardware only, by ear.
- [ ] PLAITS kit: kit 48. Pass: two kicks and two snares (Plaits' models), other lanes the 909's sounds.
  - emulator: pass (`kits`: all 10 lanes sound, none alike; A B C D). Their kind: by ear.
- [ ] Drum EDIT on the PLAITS kit lanes: TUNE, DECAY, CUT, CLICK (kick), SNAP (snare). Pass: each changes the sound as named.
  - emulator: pass for TUNE, DECAY, CUT on lane 1 (`kits`: each changes the preview; A B C D). CLICK, SNAP: not run.
- [ ] A lane of another kit borrows KAWA's or PLAITS' sound. Pass: it plays that sound.
  - emulator: not run.
- [ ] SAVE then LOAD with the KAWA kit set. Pass: the kit comes back.
  - emulator: pass (`kits`: KAWA saved as project 5, PLAITS as 6, LOAD 5: kit 47; A B C D).
- [ ] A: X0X 808 and 909 kits (the user's daily kits) still play all 16 lanes.
  - emulator: pass (`kits`: all 10 lanes of each sound, none alike, A; the drum track has 10 lanes now, not 16; D has the 808).

## 5. GRIDS (A, B, C)

Emulator (`grids`, A B C D): the pattern read step by step from the firmware (gen_dcur, clk_beat).

- [ ] On DR (no lane picked), STEP: pages GRIDS (X, Y, CHAOS) and DENSITY (BD, SD, HH) exist. GEN OUT turns it on and WRITEs. Pass: kick, snare and closed hat play a Grids pattern; X, Y move it audibly.
  - emulator: pass (pages Grids 3/5 and Density 4/5; GEN ON: 15 hits a pass, every pass the same; X +20, Y +20: each another pattern). WRITE: not run.
- [ ] CHAOS up. Pass: the pattern changes each bar; CHAOS 0 repeats exactly.
  - emulator: pass (CHAOS up: 3 different passes of 3; SHIFT + KNOB 3: CHAOS 0, the passes identical).
- [ ] DENSITY BD SD HH. Pass: each part gets denser and thinner with its value.
  - emulator: pass (BD lane 1 0 -> 16 hits, SD lane 2 0 -> 20, HH lane 4 0 -> 24 over 2 passes, down then up).
- [ ] VEL card on DENSITY ON/OFF. Pass: ON gives loud main beats and soft/ghost in-between; OFF plays all hits HARD.
  - emulator: pass for ON (NORM, SOFT and HARD hits); OFF plays NORM and HARD, no soft / ghost: as gen.c says (HARD over 192, else NORM), not "all HARD" as this line says.
- [ ] OUT card: DRUMS / EUCL. In EUCL, X Y CHAOS set the kick/snare/hat lengths (1 to 32), DENSITY the fills. Pass: three Euclidean rhythms with different loop lengths, no accent.
  - emulator: pass for kick and snare (X 16 -> the kick repeats every 5 steps, Y 104 -> the snare every 27, gen.c gr_eu_len); the hat's length and the fills: not run.
- [ ] LFO button on DR: LFO and LFO 2 exist with RATE (free or synced), WAVE, PHS, FADE, two destinations among X, Y, CHAOS, BD, SD, HH. LFO 1 to X and LFO 2 to Y. Pass: the pattern drifts by itself; the values you set stay where they are when the LFO is off; both restart at PLAY.
  - emulator: not run.
- [ ] LFO destinations in EUCL: lengths and fills. Pass: the rhythms' lengths and fills move.
  - emulator: not run.
- [ ] GRIDS on a picked lane or T1 to T3 (GEN TYPE GRIDS, X, Y, DENS). Pass: plays the lane's part.
  - emulator: not run.
- [ ] SAVE then LOAD the project with GRIDS on, OUT EUCL and LFO set. Pass: all come back.
  - emulator: pass for GEN on, EUCL and X (saved as 7, another X as 8, LOAD 7); the LFO: not run.
- [ ] Record motion on GRIDS X while playing. Pass: it replays on the next passes.
  - emulator: not run.

## 6. Bluetooth audio, BLE MIDI and AFH (A; D for the AFH detail)

Every line of this section: hardware only (the emulator has no radio).

Package A. Bluetooth is OFF until switched on: SYSTEM > BT MIDI and SYSTEM > BT AUDIO are two pages, each MODE OFF / MAN / AUTO. The hardware test is commit `1864dabf`'s, as it stands (it used the synth profile, an iPhone BLE MIDI app and AirPods; the XM4 is the headset of the AFH work):

- [ ] (1) BT MIDI MAN, the iPhone's BLE MIDI app connected. Pass: notes both ways, ACTIVITY moves. Then BT AUDIO MAN, SAVE connects the AirPods, play. Pass: audio in the AirPods. — hardware only (radio).
- [ ] (2) While both run. Pass: BT MIDI's STATE stays CONNECTED, no dropped or late notes, no crackles or gaps for a few minutes, no BLE disconnect. — hardware only (radio).
- [ ] (3) The other order: AUDIO first, then MIDI. Pass: the same as (2). — hardware only (radio).
- [ ] (4a) BT MIDI OFF and back while the AirPods play. Pass: the audio does not stop. — hardware only (radio).
- [ ] (4b) BT AUDIO OFF and back while MIDI is connected. Pass: MIDI stays up. — hardware only (radio).
- [ ] (4c) The AUDIO devices list (the 10 s inquiry) open while MIDI is connected. Pass: no BLE drop. — hardware only (radio).
- [ ] (5) Both AUTO, power-cycle. Pass: both come back and reconnect; the old single setting carried over at the first boot. — hardware only (radio).
- [ ] (6) SHIFT + volume still switches the output (Bluetooth and the FM-1's own). — hardware only (radio).
- [ ] BLE MIDI connected while USB audio streams to a computer (A has both). Pass: USB audio to a computer plays while BLE MIDI is connected. — hardware only (radio, USB host).
- [ ] The WH-1000XM4 (the headset that crackled): connect, play a loop for 10 minutes, so the FM-1 warms up (the crackles started as the radio warmed). Pass: no crackle bursts. The XM4's crackles were not deterministic (clean once with nothing changed), so a clean run is weak evidence; a crackle is a finding. — hardware only (radio).
- [ ] New since 42a5aee9: BT MIDI clock sync both ways; four remembered devices for BT audio and BT MIDI. — hardware only (radio).

AFH (commit `33d477ad`, default on, hardware-untested, and `fb79db39` for the table layout): in an ordinary build you can only judge by ear. To see it act, flash D (a BT_DIAG build switches AFH only when told):

- [ ] D: connect the XM4 or AirPods, play. Open the USB serial console (any serial terminal at the FM-1's port; `tools/fm1_console.py` sends read-only commands only and will not send `btpage`). Pass: a per-second line includes `audio_drop=` and `afh=79/0`. — hardware only (radio, USB console).
- [ ] D: type `btpage afh=3` (the map reset to all 79, then on: the switch mechanics alone, the hopping stays the basic one). Pass: the link and the audio survive, the line shows `afh=79/1`. — hardware only.
- [ ] D: type `btpage afh=1` (the map as assessed). Pass: after the next 30 s assessments the used count drops below 79 (the XM4 earlier lost 6 channels in 6 minutes, 7 MHz apart), `/N` counts the switches, `audio_drop` does not climb after the connect. FAIL: the link dies at a switch (commit `d3f50cdb` records the killed link of the first guess; `fb79db39` is the fix). — hardware only.
- [ ] D: `btpage afh=0`. Pass: all 79 again, the audio keeps playing. — hardware only.

These command names and the line format are read from the source (`firmware/src/bt/bt_page.c`), not seen on a running console.

## 7. Web screen, USB audio, live save, inserts (A and B)

- [ ] Screen tab, USB (A, B and C): the web editor's Screen tab shows the FM-1's screen live over USB. Pass: it follows the panel as pressed; the mixer meters move.
  - hardware only (USB to a computer; the emulator has no USB host).
- [ ] Screen over Bluetooth (A only; BT MIDI connected, the iPhone Optimist Mirror app, now with INFO / PANEL / NAV). Pass: screens arrive; the release notes say about 6 KB/s, so a slow page is known, not a failure. Report if it never draws.
  - hardware only (radio).
- [ ] USB audio (A): the FM-1 as a USB audio device on the computer, 4 stems in and stereo out. Pass: audio plays to the computer's input, no dropouts for 5 minutes. (Its audio quality is unchecked, so say what you hear.)
  - hardware only (USB host).
- [ ] LIVE_SAVE (A, B, C): play a pattern, SAVE a user preset and a project. Pass: the audio keeps playing through the save (no STOP BEFORE SAVE), and the saved items load back.
  - emulator: pass (`livesave`, A B C D: SAVE + ENV (U01) and PROJECT SAVE 3 while playing: SAVED, still playing, 172 halves/s, late 0, level 0.0106..0.0110 against 0.0098 playing; both loaded back stopped).
- [ ] XDLY (A: with the delay taken out of the master slots; or note it as unavailable). Pass: TIME 1, TIME 2, MOD, FDBK; high FDBK sustains and grows into a soft clip (past about 95 %).
  - emulator: pass for "unavailable in A" (refused on T1..T3 with the delay in the master's slots) and for XDLY in B (`inserts`: the waveform 0.68 of the dry away; one instance: T1 takes it, T2 and T3 do not). The four knobs and the clip: not run.
- [ ] GATER (A, B, C): RATE, DEPTH, SHAPE, 16 patterns. Pass: tempo-synced gating that restarts with PLAY.
  - emulator: pass for "it gates" (`inserts`: waveform 0.31 from the dry, A B C D); tempo sync, the patterns and the restart: not run.
- [ ] CHRS, JUNO CHORUS, FLNG (B, up to 9 at once; C, 2/2/5): put the same insert on 3 tracks. Pass: the INSERTS list shows how many more fit and the one after the room is gone shows FULL and stays dry.
  - emulator: pass (`inserts`: B CHRS, JUNO, FLNG land on T1 T2 T3; C CHRS and JUNO on T1 T2, not T3 (struck in the list, KNOB 1 passes over it, the slot stays as it was), FLNG on all three; A none). The count shown in the list: screenshot only (panel3-<pkg>/inserts/shots).
- [ ] TAPE (B): highs roll off; SPIT (B): a resonant band jumping at random. Pass: each audibly different from the dry sound, 0 is off.
  - emulator: pass for "different from dry" (`inserts`, B: TAPE waveform 1.08, SPIT 0.72 of the dry); what they sound like and "0 is off": not run.
- [ ] RECT, HARM, RING (C; A dropped them). Pass: each does what its name says on one track.
  - emulator: pass for "changes the sound" (`inserts`, C: RECT 0.75, HARM 0.34, RING 1.22 of the dry); A has RECT and HARM (not RING). By name: by ear.

## 8. The STEP cards (commit c2b638f2)

Emulator (`stepcards`, A B C D, on T1's PATTERN page; ui.cards_t, cards_long read back):

- [ ] On STEP, hold OCT+ (SHIFT2) and turn SELECT. Pass: the page's cards stay up for about 30 s after the turn instead of about 1.5 s, and stay until STEP is left; no octave change when OCT+ is let go.
  - emulator: pass (cards_t 1,766 frames against 90; still up 12 s after the last edit; octave 0 -> 0; a plain SELECT: down within 3 s).
- [ ] While they are up, turn the card knobs (GRIDS X and Y on DR). Pass: the knobs keep editing the cards, they do not fall back to the note picker.
  - emulator: pass on T1's PATTERN (KNOB 3 edits SWING 6 s after: 0 -> 6; in the short time it does not); GRIDS X / Y on DR: not run.
- [ ] A held step. Pass: its cards keep the short time.
  - emulator: pass (a step held and let go: cards down within 3 s while cards_long is on).
- [ ] Leave STEP and come back. Pass: the cards are on the short time again.
  - emulator: pass (cards_long 0 after HOME, SEQ).

## 9. The rest of the 0.2 notes (docs/RELEASE-0.2.md), not covered above

- [ ] Projects own their scenes and patterns: PROJECT LOAD opens a project (asks to SAVE first); SAVE saves everything; DELETE and NEW clear it. Pass: scenes and patterns come back with the project. (Project log format 2: an FM-1 that has old projects boots with none unread; back up first.)
  - emulator: pass (`song`, A B C D: patterns 1 and 2, scenes A and B stored, saved as project 1, power cycle, project 10 saved, LOAD 1: scene A loads steps 1 5, scene B 3 7 11; NEW clears the open project's slot (seen: SLOT 3 IS EMPTY after NEW on the open project 3)). See "Found" above: SAVE into another slot does not take the scenes.
- [ ] SONG tables: SELECT and ALGORITHM navigate, the knobs edit; BARS; edits and clears while playing. Pass: edits take effect without stopping.
  - emulator: not run (only SAVE + scene key and the scene keys, stopped, in `song`).
- [ ] Mute mode (SHIFT + a key) with three key lights; the master too; the track LEDs follow the notes. Pass: SHIFT + key mutes the sequencer input only, the VOLUME card is not struck, a volume turn never unmutes.
  - emulator: pass for the mute (`mute`, A B C D: SHIFT + white 1: smute_trk 1, P_LEVEL unchanged, the pattern silent; VOLUME turned 104 -> 115: still muted and silent; SHIFT + white 1: heard). The key lights, LEDs, the master: hardware only (not read).
- [ ] CHORD+ latch: a black modifier key tapped twice stays on. Pass: the chord modifier stays until tapped again.
  - emulator: pass (`chord`, A B C D: WARM PAD, chord mode on, black 1 tapped twice: cm_lock 1, the chord changes (RMS 0.0138 -> 0.0115); tapped once more: released, the chord as before).
- [ ] Levels aligned: drums and synths at one level; every preset the same loudness; drum VOLUME up to +6 dB. Pass: switching presets through an engine does not jump in level by an obvious amount.
  - emulator: measured, not judged (the 4-note chord's RMS spread per engine in section 2: 0.8 dB WHEEL .. 36 dB SID; above 15 dB: PHYS, NOISE, DRONE, TERRAIN, SID, PROPHET). Loudness by ear and drum VOLUME: hardware only.
- [ ] One list of note values (4BAR to 1/64, straight, dotted, triplet) on ARP RATE, SEQ DIV, DELAY TIME, synced LFOs, GATER, ROLL; arp GATE up to 198 %. Pass: all list the same values in length order.
  - emulator: not run.
- [ ] Scene backports (A): track FILTER resonance (HOME > AUDIO > FILT RESO, 0 to 10, never self-oscillating), drum TUNE on the noise sounds (closed hat, shaker, clap), a track reusing its own released voice first. Pass: each as named.
  - emulator: not run.
- [ ] MISSING warning: load a project from A that uses PHYS or the X0X 909 into B (needs a project in one package and a load in another; SAVE as a backup file in the web editor and restore it in B). Pass: the top bar says MISSING and names the engine or kit; PROJECT > TOOLS > MISSING lists it again; the engine's stand-in plays, nothing stalls.
  - emulator: pass (`missing`, the flash carried instead of a backup file: A's project (FM6, X0X 909) in B: "MISSING: FM6 T1 +1", the stand-in (DIGITAL) plays; B's (TRIO) in A: "MISSING: TRIO T1"; A's (DIGITAL, X0X 909) in C: "MISSING: KIT X0X 909"; B's (TRIO) in D; late 0, one boot). PROJECT > TOOLS > MISSING: not looked at. The backup-file route: hardware only (USB).
- [ ] Web editor over USB in Chrome or Edge: knobs, drum grid, the preset library, Backup tab. Pass: values change on the FM-1 while turning them in the browser (the editor's live editing is not built in A, B or C: ED_LIVE is off in mots; Backup and the librarian are in. Pass for these three packages is Backup and Library only).
  - hardware only (USB host; editor protocol 7 is new since 42a5aee9).
- [ ] Backup of the whole FM-1 to one file and restore. Pass: the file restores with a report of what it skipped.
  - hardware only (USB host).
- [ ] New since 42a5aee9: ANALOG 2 FLT B, the UPB4 preset bank, the default tempo 111.
  - hardware only for this pass (not in the 42a5aee9 packages the emulator ran; FLT B, UPB4 and tempo 111 can run in the emulator in a later pass).

## Reporting

Per failed line: package (A, B, C, D), the line, what happened, what the screen and LEDs showed. For a crash or reboot: the time since boot, what was playing, SYSTEM > INTERNALS if the build has it (A, B and C do not carry SYS_DEBUG; D has the console).

Rerun the emulator pass: `docs/EMU-PASS.md` (the emulator worktree with tests/emu/emulator-wide-ops.patch, the driver, EMU_DUAL=1), each package as BUILD_DIR/felucca.fwsc + felucca.elf + gen/felucca_config.h with BUILD_DIR/../firmware the package's own sources (tests/emu/fwfacts.py reads the P_ ids from them: HEAD's core.h has FLT B's three ids more), then `python3 tests/emu/run.py <pkg> --jobs 1 --only boot,autosave,upfm6,project,mute,inserts,stepcards,livesave,kits,grids,song,chord,plock,missing` (missing needs EMU_OTHER=<another package>), `--only timing` alone, and `python3 tests/emu/sweep.py <BUILD_DIR> <out>` (add `--a2 --a2-only --a2-at 23` for the filter models).

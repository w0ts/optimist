# Optimist 0.2: hardware pass on the FM-1

Nothing here has been run on an FM-1. Every package below was built and size-checked on the build farm (the exact build, not the estimate). Each of the four boots in the FM-1 emulator (tests/emu's `boot` check, 10 of 10 each, 2026-10-24: no crash record, the boot guard clears, no late audio half, a note sounds). User flashes, Claude never does. Back the FM-1 up first (README.md, "Getting back" is the way out: OCT- held alone while switching on = OPTIMIST USB RESCUE).

All four were built from `42a5aee9` on `optimist` (round 6 merged: the XIP user-preset path, the 40-packet Bluetooth audio queue, the coded tables in the boot stream; the ACCENT lane's FX rows, the drum cards' automation display and the insert TYPE popup fixed). Each package's ELF is beside it (`*.elf`, for the emulator). The configs are `config/release-pass/*.config`.

## The packages

Packages are in `/Users/mots/GitHub/optimist/build/release-pass/` (git-ignored). Each has a `-ui.zip` beside it (the web editor's sidecar). Slot limits: flash 581,564, RAM 98,304, pool 344,064 (8,192 of it kept spare), RAM code 32,512.

| Package | File | Flash | RAM | Pool | RAM code | sha1 |
|---|---|---|---|---|---|---|
| A: mots | `A-mots.fwsc` | 580,456 | 94,040 | 333,436 | 28,452 | 1a7559fb7b259e4daa23d5fc68799162be92162a |
| B: engines | `B-engines.fwsc` | 517,500 | 86,852 | 299,324 | 31,056 | 0dc104a4752779f1ec941caa650c565e215aa213 |
| C: wave | `C-wave.fwsc` | 505,688 | 75,700 | 300,496 | 30,004 | abc699821efc1e280422a674e9b95758c71c042a |
| D: btdiag (optional) | `D-btdiag.fwsc` | 561,556 | 91,452 | 322,204 | 28,116 | 67a5b33bb2313221eefd7f37747b11ff4c3fcd83 |

A has 1,108 B of flash to spare and 10,628 B of pool headroom (8,192 required). Flash, RAM and pool are the exact build's numbers; "RAM code" is the build's `.ram_text` + `.ram_hot` total, the figure the builder reports as ramtext.

### A: mots (the daily build; also the Bluetooth, USB audio, web screen and inserts package)

`config/release-pass/A-mots.config` is the maintainer's `config/my-profiles/mots.config` as it is (2026-10-24), every item on. The first build (2026-10-23, 583,092 B) had INS_RECT, INS_HARM, INS_RING and FM6_PK_80S off to fit; round 6 (lossless: the sound side's main-loop functions at size, the delta-coded tables in the boot stream) brought them back.

Holds: engines ANALOG 2 (filter models LADR, OTA, DIOD, MS20), DIGITAL, PHASE, FM6 (5 voice packs, 80S included), PLAITS, PHYS, ACID, JUNO. Drums: the 32 synth kits, KAWA (47), PLAITS (48), X0X 808 and 909, drum EDIT. GRIDS with EUCL, LFO and VEL. BLE MIDI + BT audio + BT_AFH. Screen mirror (ED_MIRROR, USB and Bluetooth). USB audio. Inserts FOLD FUZZ WIDE SFLD SUB GATER RECT HARM RING CHRS JUNO-CHORUS FLNG XDLY. LIVE_SAVE.

First on an FM-1 in A, never booted on one before: the LZB boot stream (the packed tables decoded at boot, now three blocks), the user preset banks read in place through XIP (storage/upreset.c UP_XIP), the Bluetooth audio queue at 40 packets with slots the packet's length, AFH. If A does not boot: OCT- held alone while switching on is the rescue (README.md, "Getting back").

Effects memory in A is set to 64 KB, which is the delay's whole line: the builder says CHRS, JUNO CHORUS, FLNG and XDLY get room for 0 instances at once, and +9 (CHRS, JUNO, FLNG) or +1 (XDLY) only while the delay is in no master insert slot. So in A the line-based inserts only work with the delay taken out of the master's slots; FOLD, FUZZ, WIDE, SFLD, SUB and GATER need no line and work as usual. Test CHRS and XDLY in B.

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

Nothing was left out. A had to drop INS_RECT, INS_HARM, INS_RING and the 80s voice pack; C holds them.

## How to read the checklist

"Pass" is stated after each line. If a line fails, note the package, the engine or page, what you pressed and what the screen showed. Run each package's checks in this order: boot (1), engines (2), the package's extras (3 to 8).

## 1. First boot (every package, first)

No build since commit `032f02a4` (the LZ-packed boot stream, decoded in `fm1_cstart`) and `23be06fb` (the same stream range-coded) has booted on an FM-1. The first boot of each package is itself a test.

- [ ] A: flash it, power-cycle. Pass: the Optimist screen comes up, keys play, no reboot loop within 2 minutes.
- [ ] B: same. Pass: as A.
- [ ] C: same. Pass: as A.
- [ ] Any package: SYSTEM > INFO shows UNDERRUNS 0 or near it with nothing playing; the header shows no red/amber drop mark at idle.
- [ ] After each flash, a power-cycle (off, then on) once more. Pass: it boots again from the flash the same way.

If a package does not boot, report: which package (A, B or C); the LEDs (which are lit or blinking); what the screen shows (dark, garbled, a message); and whether OCT- held alone while switching on brings OPTIMIST USB RESCUE. The commits to revert, alone, one at a time, on a fresh branch from `rel/hw-pass`: `23be06fb` (the range coder over the stream) first, rebuild, try; then `032f02a4` (the LZ stream; its link fix is `9f76f17a`). A dead boot on all three packages points at these two. A dead boot on one package only points at that package's items.

## 2. Engines (one block per engine)

For each engine, on a track set to it in the package that holds it (table above):

- [ ] Presets: PRESETS through the first three. Pass: each makes a sound, different from the last, no click or silence.
- [ ] Chord: hold a 4-note chord. Pass: all 4 notes sound, release ends them, no stuck note.
- [ ] P-lock: on the SEQ layer hold a step, turn PRESETS (or ALGORITHM to pick another value, then a knob). Pass: that step sounds different from the others on the next pass and the next step is back to normal.
- [ ] LFO 2: LFO button, LFO 2, destination CUT (or an engine value where the engine has no cutoff), depth up, RATE slow. Pass: the sound moves audibly with the LFO.
- [ ] SAVE then LOAD: SAVE the project, change the engine's sound, LOAD it. Pass: the saved sound comes back, same engine, same preset.
- [ ] Insert: INSERTS > S1 CHRS on the track (or the engine's typical one: FOLD on ACID or PHYS). In A use FOLD, FUZZ or WIDE (see "Effects memory" under A). Pass: audible, no crackle, S1 off removes it.

Engine lines (tick the engine once its six checks pass; the six are the list above):

- [ ] ANALOG 2 (A, B, C, D; run it fully in A)
- [ ] FM6 (A, C, D)
- [ ] JUNO (A, D)
- [ ] ACID (A, D)
- [ ] PHYS (A, D; heavy: 3 voices of its heaviest preset, note SYSTEM > INFO CPU LOAD and UNDERRUNS)
- [ ] DIGITAL (A, B)
- [ ] PHASE (A, B; also the FOLD value on E7)
- [ ] PLAITS (A, D; MODEL, HARMONICS, TIMBRE, MORPH)
- [ ] CZ (B)
- [ ] SLICE (B)
- [ ] NOISE (B)
- [ ] DRONE (B; BEAT with headphones, HOLD latches the keys; presets must start with HOLD off)
- [ ] STRINGS (B)
- [ ] VPS (B)
- [ ] TERRAIN (B)
- [ ] WSEQ (B; it follows the song's tempo)
- [ ] VECTOR (B)
- [ ] SID (B; three voices a part: the 4-note chord drops one)
- [ ] PROPHET (B; five voices)
- [ ] TRIO (B)
- [ ] WAVE (C; 13 presets; one table shared by every WAVE track, so set two WAVE tracks and see both play the first one's table)
- [ ] LOFI (C)
- [ ] SAMPLE (C; the VIBES set)
- [ ] VOICE (C)
- [ ] WHEEL (C)
- [ ] GRAIN (C; presets read the VIBES set)

Two more checks on the heaviest package, C with WAVE: [ ] four notes held on a WAVE track plus the drum track playing: SYSTEM > INFO shows no UNDERRUNS counting up; [ ] SYSTEM > INFO's CPU LOAD with PHYS in A (3 voices) stays under the red mark.

## 3. ANALOG 2 filter models (A, with B and C as a second look)

On an ANALOG track, FLT 2 gets a MODEL card next to FTYP. Each model with RES up to about 3/4, CUT swept slowly, then one key held:

- [ ] LADR. Pass: a ladder: bass thins as RES rises, ringing in tune at the top. No crackle, no stuck note.
- [ ] OTA. Pass: creamy, input stays clean, the resonance saturates, self-oscillation round.
- [ ] DIOD. Pass: squelchy, thin vocal resonance at the cutoff.
- [ ] MS20. Pass: screams at high RES, rings loud; LP BP HP NOTCH each distinct; LP24 steeper.
- [ ] Each model with 4 notes held and SYSTEM > INFO: CPU LOAD noted (about 3x ANALOG 2's own filter per voice on the host; never measured on the FM-1). Pass: no UNDERRUNS counting up; if it does, say which model.
- [ ] In B and C (same four models are built): switch one model, SAVE, LOAD. Pass: the model is kept.

## 4. Drum kits KAWA and PLAITS (A, B, C)

- [ ] KAWA: drum track, kit 47 (KAWA). Play each lane. Pass: guiro, clap, flammed snare, brush, shaker, hats, ride, toms, cowbell, kick sound distinct, none silent.
- [ ] PLAITS kit: kit 48. Pass: two kicks and two snares (Plaits' models), other lanes the 909's sounds.
- [ ] Drum EDIT on the PLAITS kit lanes: TUNE, DECAY, CUT, CLICK (kick), SNAP (snare). Pass: each changes the sound as named.
- [ ] A lane of another kit borrows KAWA's or PLAITS' sound. Pass: it plays that sound.
- [ ] SAVE then LOAD with the KAWA kit set. Pass: the kit comes back.
- [ ] A: X0X 808 and 909 kits (the user's daily kits) still play all 16 lanes.

## 5. GRIDS (A, B, C)

- [ ] On DR (no lane picked), STEP: pages GRIDS (X, Y, CHAOS) and DENSITY (BD, SD, HH) exist. GEN OUT turns it on and WRITEs. Pass: kick, snare and closed hat play a Grids pattern; X, Y move it audibly.
- [ ] CHAOS up. Pass: the pattern changes each bar; CHAOS 0 repeats exactly.
- [ ] DENSITY BD SD HH. Pass: each part gets denser and thinner with its value.
- [ ] VEL card on DENSITY ON/OFF. Pass: ON gives loud main beats and soft/ghost in-between; OFF plays all hits HARD.
- [ ] OUT card: DRUMS / EUCL. In EUCL, X Y CHAOS set the kick/snare/hat lengths (1 to 32), DENSITY the fills. Pass: three Euclidean rhythms with different loop lengths, no accent.
- [ ] LFO button on DR: LFO and LFO 2 exist with RATE (free or synced), WAVE, PHS, FADE, two destinations among X, Y, CHAOS, BD, SD, HH. LFO 1 to X and LFO 2 to Y. Pass: the pattern drifts by itself; the values you set stay where they are when the LFO is off; both restart at PLAY.
- [ ] LFO destinations in EUCL: lengths and fills. Pass: the rhythms' lengths and fills move.
- [ ] GRIDS on a picked lane or T1 to T3 (GEN TYPE GRIDS, X, Y, DENS). Pass: plays the lane's part.
- [ ] SAVE then LOAD the project with GRIDS on, OUT EUCL and LFO set. Pass: all come back.
- [ ] Record motion on GRIDS X while playing. Pass: it replays on the next passes.

## 6. Bluetooth audio, BLE MIDI and AFH (A; D for the AFH detail)

Package A. Bluetooth is OFF until switched on: SYSTEM > BT MIDI and SYSTEM > BT AUDIO are two pages, each MODE OFF / MAN / AUTO. The hardware test is commit `1864dabf`'s, as it stands (it used the synth profile, an iPhone BLE MIDI app and AirPods; the XM4 is the headset of the AFH work):

- [ ] (1) BT MIDI MAN, the iPhone's BLE MIDI app connected. Pass: notes both ways, ACTIVITY moves. Then BT AUDIO MAN, SAVE connects the AirPods, play. Pass: audio in the AirPods.
- [ ] (2) While both run. Pass: BT MIDI's STATE stays CONNECTED, no dropped or late notes, no crackles or gaps for a few minutes, no BLE disconnect.
- [ ] (3) The other order: AUDIO first, then MIDI. Pass: the same as (2).
- [ ] (4a) BT MIDI OFF and back while the AirPods play. Pass: the audio does not stop.
- [ ] (4b) BT AUDIO OFF and back while MIDI is connected. Pass: MIDI stays up.
- [ ] (4c) The AUDIO devices list (the 10 s inquiry) open while MIDI is connected. Pass: no BLE drop.
- [ ] (5) Both AUTO, power-cycle. Pass: both come back and reconnect; the old single setting carried over at the first boot.
- [ ] (6) SHIFT + volume still switches the output (Bluetooth and the FM-1's own).
- [ ] BLE MIDI connected while USB audio streams to a computer (A has both). Pass: USB audio to a computer plays while BLE MIDI is connected.
- [ ] The WH-1000XM4 (the headset that crackled): connect, play a loop for 10 minutes, so the FM-1 warms up (the crackles started as the radio warmed). Pass: no crackle bursts. The XM4's crackles were not deterministic (clean once with nothing changed), so a clean run is weak evidence; a crackle is a finding.

AFH (commit `33d477ad`, default on, hardware-untested, and `fb79db39` for the table layout): in an ordinary build you can only judge by ear. To see it act, flash D (a BT_DIAG build switches AFH only when told):

- [ ] D: connect the XM4 or AirPods, play. Open the USB serial console (any serial terminal at the FM-1's port; `tools/fm1_console.py` sends read-only commands only and will not send `btpage`). Pass: a per-second line includes `audio_drop=` and `afh=79/0`.
- [ ] D: type `btpage afh=3` (the map reset to all 79, then on: the switch mechanics alone, the hopping stays the basic one). Pass: the link and the audio survive, the line shows `afh=79/1`.
- [ ] D: type `btpage afh=1` (the map as assessed). Pass: after the next 30 s assessments the used count drops below 79 (the XM4 earlier lost 6 channels in 6 minutes, 7 MHz apart), `/N` counts the switches, `audio_drop` does not climb after the connect. FAIL: the link dies at a switch (commit `d3f50cdb` records the killed link of the first guess; `fb79db39` is the fix).
- [ ] D: `btpage afh=0`. Pass: all 79 again, the audio keeps playing.

These command names and the line format are read from the source (`firmware/src/bt/bt_page.c`), not seen on a running console.

## 7. Web screen, USB audio, live save, inserts (A and B)

- [ ] Screen tab, USB (A, B and C): the web editor's Screen tab shows the FM-1's screen live over USB. Pass: it follows the panel as pressed; the mixer meters move.
- [ ] Screen over Bluetooth (A only; BT MIDI connected, the iPhone Optimist Mirror app). Pass: screens arrive; the release notes say about 6 KB/s, so a slow page is known, not a failure. Report if it never draws.
- [ ] USB audio (A): the FM-1 as a USB audio device on the computer, 4 stems in and stereo out. Pass: audio plays to the computer's input, no dropouts for 5 minutes. (Its audio quality is unchecked, so say what you hear.)
- [ ] LIVE_SAVE (A, B, C): play a pattern, SAVE a user preset and a project. Pass: the audio keeps playing through the save (no STOP BEFORE SAVE), and the saved items load back.
- [ ] XDLY (A: with the delay taken out of the master slots; or note it as unavailable). Pass: TIME 1, TIME 2, MOD, FDBK; high FDBK sustains and grows into a soft clip (past about 95 %).
- [ ] GATER (A, B, C): RATE, DEPTH, SHAPE, 16 patterns. Pass: tempo-synced gating that restarts with PLAY.
- [ ] CHRS, JUNO CHORUS, FLNG (B, up to 9 at once; C, 2/2/5): put the same insert on 3 tracks. Pass: the INSERTS list shows how many more fit and the one after the room is gone shows FULL and stays dry.
- [ ] TAPE (B): highs roll off; SPIT (B): a resonant band jumping at random. Pass: each audibly different from the dry sound, 0 is off.
- [ ] RECT, HARM, RING (C; A dropped them). Pass: each does what its name says on one track.

## 8. The STEP cards (commit c2b638f2)

- [ ] On STEP, hold OCT+ (SHIFT2) and turn SELECT. Pass: the page's cards stay up for about 30 s after the turn instead of about 1.5 s, and stay until STEP is left; no octave change when OCT+ is let go.
- [ ] While they are up, turn the card knobs (GRIDS X and Y on DR). Pass: the knobs keep editing the cards, they do not fall back to the note picker.
- [ ] A held step. Pass: its cards keep the short time.
- [ ] Leave STEP and come back. Pass: the cards are on the short time again.

## 9. The rest of the 0.2 notes (docs/RELEASE-0.2.md), not covered above

- [ ] Projects own their scenes and patterns: PROJECT LOAD opens a project (asks to SAVE first); SAVE saves everything; DELETE and NEW clear it. Pass: scenes and patterns come back with the project. (Project log format 2: an FM-1 that has old projects boots with none unread; back up first.)
- [ ] SONG tables: SELECT and ALGORITHM navigate, the knobs edit; BARS; edits and clears while playing. Pass: edits take effect without stopping.
- [ ] Mute mode (SHIFT + a key) with three key lights; the master too; the track LEDs follow the notes. Pass: SHIFT + key mutes the sequencer input only, the VOLUME card is not struck, a volume turn never unmutes.
- [ ] CHORD+ latch: a black modifier key tapped twice stays on. Pass: the chord modifier stays until tapped again.
- [ ] Levels aligned: drums and synths at one level; every preset the same loudness; drum VOLUME up to +6 dB. Pass: switching presets through an engine does not jump in level by an obvious amount.
- [ ] One list of note values (4BAR to 1/64, straight, dotted, triplet) on ARP RATE, SEQ DIV, DELAY TIME, synced LFOs, GATER, ROLL; arp GATE up to 198 %. Pass: all list the same values in length order.
- [ ] Scene backports (A): track FILTER resonance (HOME > AUDIO > FILT RESO, 0 to 10, never self-oscillating), drum TUNE on the noise sounds (closed hat, shaker, clap), a track reusing its own released voice first. Pass: each as named.
- [ ] MISSING warning: load a project from A that uses PHYS or the X0X 909 into B (needs a project in one package and a load in another; SAVE as a backup file in the web editor and restore it in B). Pass: the top bar says MISSING and names the engine or kit; PROJECT > TOOLS > MISSING lists it again; the engine's stand-in plays, nothing stalls.
- [ ] Web editor over USB in Chrome or Edge: knobs, drum grid, the preset library, Backup tab. Pass: values change on the FM-1 while turning them in the browser (the editor's live editing is not built in A, B or C: ED_LIVE is off in mots; Backup and the librarian are in. Pass for these three packages is Backup and Library only).
- [ ] Backup of the whole FM-1 to one file and restore. Pass: the file restores with a report of what it skipped.

## Reporting

Per failed line: package (A, B, C, D), the line, what happened, what the screen and LEDs showed. For a crash or reboot: the time since boot, what was playing, SYSTEM > INTERNALS if the build has it (A, B and C do not carry SYS_DEBUG; D has the console).

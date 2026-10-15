# The Optimist UI: controls, pages and read-outs

This guide covers the Optimist user interface (builder item `UI`, value 1; SLOOP's interface is the default and is described in [OPTIMIST.md](../../OPTIMIST.md)). It is still EXPERIMENTAL. What each button and knob does is read from the source (`firmware/src/ui/optimist`); the design decisions are in [UI-OPTIMIST-DESIGN.md](../UI-OPTIMIST-DESIGN.md). What a build contains depends on its profile: a page or line whose feature is not built is not shown.

## The controls

| Control | What it does |
|---|---|
| ALGORITHM | Picks the track: MASTER, T1, T2, T3, DR, then the drum lanes, on the mixer and every page that has a track. On GLOBAL and SYSTEM it is the one exception: it moves the cursor one line at a time. |
| SELECT | Moves the cursor: a row a detent, stopping at the ends. On the mixer it pages the knob sets of the selected row. |
| PRESETS | The selected track's sound: a synth track's preset, the drum track's kit, a drum lane's own sound. On STEP it scrolls the 16-step window. On GLOBAL and SYSTEM it does nothing (the knobs are the lines). |
| the four knobs | Knob 1 to 4 edit card or line 1 to 4 of the page. A knob turned makes its card the cursor and the help line names it. |
| SAVE | YES. Tapped where YES has nothing to do (the mixer, the pages that are not asking), it opens PROJECT. SAVE then HOME is undo, SHIFT then SAVE is redo. |
| HOME | NO, or back. HOME is only HOME / NO. |
| OCT- held | **SHIFT** (below). |
| OCT+ held | **SHIFT2** (below). |
| LFO held | The fine path: a knob turned edits the value by single units (a pattern length by one). |
| PLAY | Tapped: start / stop. Held: the tempo page (below). |
| GLO | Tapped: GLOBAL; on GLOBAL, SYSTEM; on SYSTEM, GLOBAL. Each keeps its line. |
| SEQ | Tapped: STEP (below). |

### SHIFT is OCT- held

SHIFT replaces the old HOME-held gestures. Everything below needs OCT- held first:

- **SHIFT + a knob:** the value goes back to its default. On a step's p-lock card it drops that cell's lock.
- **SHIFT + a key on the drum track:** picks the drum lane (the key still plays). On STEP, with no step held, the same. The last upper key picks the ACCENT lane.
- **SHIFT + PRESETS:** changes the instrument (an engine on a synth track, the next or previous kind on a lane that is not on a kit). On STEP it makes the window follow the playhead again.
- **SHIFT + a key on the mixer:** toggles the sequencer mute of what the key names: the 16 white keys the 16 drum lanes, the black keys F#, G#, A#, C# the tracks T1, T2, T3, DR, the last upper key the ACCENT lane. A sequencer-muted track or lane plays no steps; keys you play yourself still sound.
- **SHIFT + a step (STEP):** clears that step's p-locks. The notes, nudge, fill, chance and recorded motion stay.
- **SHIFT + SEQ:** asks to clear the selected track's locks, nudges, fills and motion ("Clear T1 locks?").
- **SHIFT + ENV:** INIT of the selected synth track's sound (asked when it has edits). On an FM6 track ENV is a layer button and the next rule applies.
- **SHIFT + a layer button** (FX, EDIT, ARP, SCL, GLO, LFO): locks that layer open; any other button lets it go. The layer's own button held, then SHIFT, does the same, except for FX and EDIT (and ENV on FM6), whose OCT- and OCT+ keep their own jobs.
- **SHIFT + PLAY:** stop, every voice off (ALL SOUND OFF).
- **SHIFT + REC** on a SONG scene row: clears that scene (asked).

### SHIFT2 is OCT+ held

- **SHIFT2 + the volume knob on the mixer** (a track's VOLUME, the drum track's VOLUME, a lane's LEVEL, the ACCENT amount, the USB IN row's VOLUME): toggles that row's mute, once for each hold. There is no MUTE card. Turning a muted row's volume card unmutes it.
- **SHIFT2 + PRESETS:** marks the sound playing a **favorite** (turn right) or not (left). SHIFT2 + SELECT browses only the favorites (right) or every sound (left); the toast says FAVORITES ONLY or ALL SOUNDS. Synth tracks only; not on STEP, MASTER, GLOBAL or SYSTEM, and not while a question is asked. It needs the `FAVORITES` builder item.
- **SHIFT2 + ITEM on PROJECT > USER:** marks that user preset slot a favorite.

### OCT-, OCT+ and both OCT

- **The octave.** OCT- or OCT+ tapped (let go with nothing else used meanwhile) changes the keyboard octave by one, between -3 and +3, with the toast "OCTAVE: n". It acts on release, so holding either is a SHIFT, not an octave change. On STEP the piano roll follows the octave. On the drum track a tap does nothing; OCT- is the lane pick's SHIFT, and OCT+ held makes a hard hit.
- **both OCT** together, tapped and let go with nothing else used: the track and MASTER toggle (back to the track you came from). Not on NAME, under a question, in a layer, or on pages with no track (GLOBAL, SYSTEM, PROJECT, SONG).
- On PROJECT, a tap is the previous or next bank (below). On a held step, OCT- / OCT+ move the step's notes an octave (synth tracks). On the tempo page and GLOBAL's Tempo lines they nudge the clock. With EDIT held they are undo and redo.

### The LED rule

A button's LED glows only where the button does something. The button of the page you are on is lit fully; a button that would do something now is lit dimly (half lit); a button that would do nothing is dark. So LFO is dark on the drum track, OCT- and OCT+ are dark where they have no job, and SAVE is dark on a STEP line that has no YES action. GLO is full on GLOBAL and SYSTEM and dim elsewhere.

## The mixer

The mixer is the first screen. The rows are tracks: MASTER above T1, then T1, T2, T3, DR, the drum track's 16 lanes plus the ACCENT lane, and, in a USB audio build, a USB IN row after them. ALGORITHM walks the rows; the panel shows the selected row's group (MASTER alone, the four tracks, the lanes). Each row has its meter and its sequence. SELECT pages the knob sets of the row; the same order on every row:

- **Mix:** a track VOLUME and PAN; the drum track VOLUME; a lane LEVEL, CUT, DRIVE; the ACCENT lane its ACCENT amount (a velocity).
- **Inserts:** the row's two insert slots, S1 TYPE and amount, S2 TYPE and amount (see [effects.md](effects.md)). MASTER: its two slots, then THRS, RATIO, GAIN, CEIL while DYN is in a slot.
- **Sends:** REV, DLY, CHO (blank where no slot holds that send) and card 4 PAN, on synth tracks, the drum track and the drum lanes. MASTER shows the returns, one card for each send a master slot holds (REV RET, DLY RET, CHO RET), and DUCK where built.
- The USB IN row: VOLUME, PAN, FX ON, its two insert slots, REV, DLY, CHO; its mute is SHIFT2 on VOLUME.

A muted track or lane is **struck through**: a 1 px line (2 in the large face) through its mixer row name, the header badge on its pages ("2 DIGITAL") and its volume card's label and value. Unmuted, nothing is drawn. The sequencer mute (SHIFT + a key) is a different mute from the volume mute (SHIFT2) and is saved with the project. On MASTER the keys are silent. PRESETS on a row changes that row's sound.

## The tempo page: PLAY held

PLAY held past the hold time opens the tempo page; let go and the screen before returns. A short tap of PLAY stays start / stop. The page has TEMPO (BPM, NUDGE, SWING, SYNC), REC (MODE, LENGTH, START, CLICK) and REC 2 (QUANT).

- **BPM** 40 to 240, default 90. **SWING** 0 to 100.
- **NUDGE:** OCT- or OCT+ held makes the clock about 3.9 % slower or faster while held (the display shows -3.9 or +3.9); the BPM value never changes, and nothing happens while an external clock is followed.
- **Tap tempo:** with PLAY held, tap a white key; two taps or more set the tempo.
- **SYNC** (the MIDI clock to follow) is INT, USB, TRS or AUTO (AUTO shows the clock it follows: A:INT, A:USB, A:TRS). On GLOBAL it is CLK IN on the MIDI page.
- **REC:** MODE FREE or TEMPO, LENGTH 1, 2 or 4 bars, START NOTE or COUNT (count-in), CLICK OFF, REC or ON. **QUANT** ON snaps a recorded note to the grid; OFF keeps its start as a nudge and its length as a gate lock.

## STEP

SEQ tapped opens STEP, one screen for synth tracks and the drum track. The 16 white keys are the 16 steps of the window (1-16, 17-32 and so on up to the track length). The window follows the playhead while playing; PRESETS scrolls it and stops the following, SHIFT + PRESETS resumes it.

- **A step key tapped:** sets an empty step or clears a set one, when it is let go. A press held past the hold time, or one during which a knob turned or a value was edited, leaves the step as it was. On the drum track a tapped step cycles normal, hard, ghost, off.
- **A step key held:** the cards are the step's: drums LEVEL and RATCHET; synths NOTE, LEVEL, RATCHET, LENGTH. Several held steps edit together. SELECT pages the cards: the step, its extras (CHANCE, NUDGE, FILL, COND, as built), then the sound pages as p-locks. SAVE sets the fill condition and HOME clears the held step.
- **A step held and a page button** (ENV, LFO, FX, SCL, ARP): that page's cells become the step's events. A turn writes a step-only event (a p-lock, shown by a padlock mark; a held motion value shows an arrow). YES toggles the hot cell between step-only and held. SHIFT + the knob drops it; SHIFT + the step clears all its p-locks. Not lockable: LFO DEST, VOICE, ALLOC, PRIO, GLMODE, MUTE, INSERTS S1 and S2, and a drum lane's SENDS.
- With no step held the cards are the track's PATTERN row, TOOLS and EUCLID. EUCLID is a note generator with ON / OFF; turning it OFF asks to keep or restore the pattern. TOOLS ask first.
- **SEQ held + keys:** a synth's notes become the next steps'. SEQ tapped again: the keys play with the screen up; again: steps.
- **OCT+** held on the drum track makes a hard hit; on a held synth step, OCT- / OCT+ move its notes an octave.

## PROJECT

PROJECT is where storage lives. A storage row (PROJECT, SNAPSHOT, USER) has up to four cards: **BANK** (only for a store of more than 16 slots), the operation (**LOAD**, the default, **SAVE** or **DELETE**), **ITEM** (the slot; the knob walks all of them and the bank follows) and, on PROJECT, **NEW** (a blank project, touching no slot). A build whose project slots are kept in RAM has no DELETE.

- The 16 white keys are the 16 slots of the current bank: key n selects item n and runs the operation on it, through a confirmation. YES runs it on the item the knob is on. A free slot has nothing to load or clear: a toast, no question. The operation returns to LOAD after each action, when the row changes or the screen is left, so a stray key never saves.
- **OCT- or OCT+ tapped** on a row with slots: the previous or next **bank**, with the toast "BANK n/N" and the LEDs showing whether there is one.
- **USER** (32 user presets: two banks) has an INIT card: the selected track's sound back to its engine's defaults. SHIFT2 + ITEM marks a slot a favorite. A user preset's LOAD over an edited sound is asked in red.
- Below the cards: the bank's 16 slots as a strip and the item's information. The rows after them: TOOLS (CLRSQ, INIT, MISS, NEW), SLOOP (the SLOOP 2.4 import, in builds with it) and MEMORY (USED as a percentage and FREE, the sections that still fit, of the section log).
- NAME asks for a name before a save: the keys type, SAVE finishes, HOME deletes a character or cancels.

## GLOBAL and SYSTEM

GLOBAL and SYSTEM are settings lists: one setting on a line, the name left and the value right, lines under small section headers, in pages of 4. SELECT moves a page at a time; knob k edits line k and makes it the cursor; PRESETS does nothing here; SAVE is YES on the cursor line (an on / off value toggles, an action line runs); SHIFT + a knob returns a value to its default. GLO tapped switches between the two; each keeps its line.

**GLOBAL**
- **Tempo:** BPM, NUDGE (read-out), SWING, TUNE (the master tuning, -50 to 50 cents).
- **Rec:** MODE, LENGTH, START, CLICK, QUANT.
- **MIDI:** CLK IN (the clock to follow: INT, USB, TRS, AUTO), CLK OUT (ON or OFF; amber when ON but USB is the clock being followed, so nothing is sent), MIDI OUT (one page; three lines in a build without CLK OUT). There is no MIDI IN line: notes come in through the track channels.
- **Channels:** T1, T2, T3, DR (the MIDI channel of each track).

**SYSTEM**
- **INFO, INTERNALS, USB STATS:** read-outs (next section). INFO is always the first page.
- **SCREEN:** COLOR (the palette), BRIGHT (the screen backlight, 1 to 8, in builds with it), KNOB COLORS (ON / OFF).
- **AUDIO:** LOWCUT (OFF, LOWCUT, or BASS+ in builds with it).
- **KEYS:** BUTTONS (OFF, LOW, MID, HIGH) and KEYS (OFF, C, WHITE, ALL), in builds with LIGHTS.
- **USB:** SERIAL, ON or OFF; amber until a restart applies it (builds with the USB console).
- **BLUETOOTH:** BLE, OFF, ON, LINK (connected) or NO CAL (the radio has no stored trims and never starts). SAVE on it opens the list of BLE devices.
- **CALIBRATE:** PANEL (runs the panel calibration, asked first) and HOLD (a button's hold time: 250, 350 or 500 ms).
- **USB AUDIO:** (USB audio builds) CH1 to CH4, the source of each of the four capture channels sent to the computer: the main mix (L, R or sums), T1 to T3, DRUMS, the drum bus, a drum lane tap (DR1 to DR16) or OFF.
- **ABOUT:** VERSION (and the commit it was built from), BUILD (date and time), CHIP (AC791N, 1 core or 2 cores) and the repository address.

## SYSTEM > INFO, INTERNALS and USB STATS

The first SYSTEM pages are read-outs, text only; no knob does anything there. There are three, each drawn whole: INFO first, then INTERNALS, then (in a USB audio build) USB STATS.

**INFO** is what a musician reads:

| Line | Meaning |
|---|---|
| CLOCK | The CPU clock, in MHz. |
| CPU0 (LOAD without a second core) | The audio render load of CPU0, in percent; with a second core it includes CPU0's wait for CPU1. Knob 2 chooses what it shows: NOW, the maximum of the last 5 seconds, or the maximum since boot. Over 100 means a late half. |
| 2ND CORE ON | The share of each audio half CPU1 takes. |
| 2ND CORE OFF, then a reason | CPU0 renders everything. The reason: NO START, TIMEOUT, FAULT, FLASH, MISREAD, CANARY, AFTER CRASH, SETTING or PARKED. In the current default build the second core is PARKED: it is never started (2026-10-15, until the hardware tests settle it), so INFO says 2ND CORE OFF PARKED; a build made with `OPTIMIST_DUAL_PARK=0` starts it. |
| DROPS | How many audio halves were late (a gap in the sound) since boot or the last clear; red for a while after a new one. |
| UNDERRUNS | USB audio's and MIDI in's losses together, and where (`USB3 MIDI1`). |
| GUARD | What the CPU guard is easing now, the voices it capped (`MAX n`) and the voices it shed. In builds with the CPU guard. |
| BATTERY | The battery as bars out of 3 and the raw ADC count, as `n/3` and the count; "USB" in the unit while powered by USB; NO READING before the first sample. There is no calibration, so no volts and no percentage are shown. |

**INTERNALS** is for finding a fault:

| Line | Meaning |
|---|---|
| LAST RESET | How the boot before this one ended: POWER ON, CRASH (CRASH1: CPU1 named), WDT, SOFT, KEY or LOW V, then how long it ran, and a crash address or the step it was in. Debug builds only. |
| FLASH IRQS OFF | The longest time interrupts were off for a flash operation, in microseconds (amber when longer than an audio half). |
| CPU1 JOBS DONE / RE-WAKES | Jobs joined (thousands) / times CPU0 woke CPU1 again for a job it had not picked up (amber when not 0). Builds with two cores. |
| CPU1 LAST STOP | `--`: CPU1 never stopped. Otherwise when and where: `W` the job was never picked up; `J<n>` it was in job n (1 to 4 a part, 5 the drums, 6 the reverb, 0 none begun); `IDLE` no job was posted (a boot check, a flash operation). The time is since CPU1 started (milliseconds under 10 s, then seconds). |
| CPU1 GOT TO / HEARTBEAT | How far CPU1 got: NONE, MAIN, HELLO, SLEEP / SPIN, WOKEN, TOOK JOB, JOB DONE. |
| CPU1 FAULT | An exception about CPU1 (`D` or `E` code, `@` address and the core that took it); none: `IC` and its interrupt setup. |

**USB STATS** (USB audio builds) holds the counters since boot, read-only:

| Line | Meaning |
|---|---|
| USB UNDERRUNS, USB OVERRUNS | Playback underruns and overruns (amber when not 0). An underrun is the computer's audio running dry: the ring empties, the FM-1 resets it and refills 512 frames, so at least about 12 ms is silent. |
| MISSED FRAMES, BAD PACKETS | USB frames that did not arrive, and packets that were bad (amber when not 0). |
| PACKETS IN, RATE ASKED | Packets received, and the feedback rate sent to the computer (Hz). |
| LOWEST BUFFER | The lowest the ring got, in frames (the ring holds 1024; the target is 512); NONE YET before the first reading. |
| LONGEST GAP, GAPS > 0.9 MS | The longest gap between two USB polls (microseconds), and how many gaps were over 0.9 ms. |

These counters exist to find the cause of an occasional glitch while the computer plays audio to the FM-1. The cause is **not yet measured** on hardware. While a computer is streaming, flash writes that can wait are held back.

## Connections

- **BLE MIDI** (experimental; our own Bluetooth stack): SYSTEM > BLUETOOTH above. The manual's note on it is the paragraph "HOME double tap > SYSTEM menu > BLUETOOTH" in [OPTIMIST.md](../../OPTIMIST.md) (written for the SLOOP UI), and the stack is in [docs/BLE-STACK.md](../BLE-STACK.md).
- **USB** carries MIDI, and in a USB audio build the computer's audio: [OPTIMIST.md, USB audio](../../OPTIMIST.md#usb-audio-experimental). The USB port item of the builder chooses console, USB audio or MIDI only.
- **TRS MIDI** in and out, **MIDI clock in and out**, sync: [OPTIMIST.md, MIDI clock and sync](../../OPTIMIST.md#midi-clock-and-sync) and GLOBAL's MIDI page above.

## The local installer and web editor: `make web`

After `make build`, `make web` (or `python3 tools/optimist.py web`) makes the local site from `build/felucca.fwsc`, serves it on 127.0.0.1:8766 (`make web PORT=9000` for another port), prints the installer and editor URLs (`http://localhost:8766/webapp/installer/` and `/webapp/editor/`) and opens the editor. Use Chrome or Edge: Web MIDI is not in Safari or Firefox. Ctrl-C stops the server. Details: [BUILDING.md](../../BUILDING.md).

## The update loader and package ids

The update loader on the FM-1 draws one progress bar: the package check fills the first half, the write the second. The app's last step reads FLASHING. An update refuses another device's package before anything is erased.

A package's identity is `FM-1_700` for development builds and `FM-1_7(X+2)Y` for `--release X.Y`: release 0.1 is `FM-1_721`, 1.0 is `FM-1_730`, up to 4.9, `FM-1_769`. Flashing takes only `FM-1_700` and `FM-1_720` to `FM-1_769`, so a package of another firmware that uses another 7XX number is not taken for Optimist. See [BUILDING.md](../../BUILDING.md), "Package identity".

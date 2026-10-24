# The Optimist UI: controls, pages and read-outs

Every gesture in one list: [shortcuts.md](shortcuts.md). This page says what the controls are and what each page shows. What a build contains depends on its profile: a page or line whose feature is not built is not shown.

## The controls

| Control | What it does |
|---|---|
| ALGORITHM | Picks the track: MASTER, T1, T2, T3, DR, then the drum lanes and the ACCENT lane, on the mixer and every page that has a track. On GLOBAL and SYSTEM it moves the cursor one line at a time |
| SELECT | Moves the cursor, a row a detent, stopping at the ends. On the mixer it pages the knob sets of the selected row. On GLOBAL and SYSTEM it pages four lines |
| PRESETS | The selected track's sound: a synth track's preset, the drum track's kit, a drum lane's own sound. On STEP it scrolls the 16-step window. On GLOBAL and SYSTEM it does nothing |
| the four knobs | Knob 1 to 4 edit card or line 1 to 4 of the page. A turned knob makes its card the cursor and the help line names it |
| SAVE | YES. Tapped where YES has nothing to do (the mixer, pages that are not asking), it opens PROJECT |
| HOME | NO, or back |
| OCT- held | **SHIFT**: off, down, away (a mute, a clear, a default) |
| OCT+ held | **SHIFT2**: pick, on, add (select a track, lock a layer, a hard hit) |
| ENV, LFO, EDIT, FX, SCL, ARP | Tapped: the sound pages of that family. Held: a performance layer (keys and knobs change job; ENV only on an FM6 track) |
| LFO held | The patterns and scenes layer. With a knob: the value in fine steps |
| PLAY | Tapped: start / stop. Held: the tempo page |
| REC | Record the selected track. Held: the ring that clears recorded material |
| GLO | Tapped: GLOBAL; on GLOBAL, SYSTEM; on SYSTEM, GLOBAL. Each keeps its line |
| SEQ | Tapped: STEP. Held (not on STEP): SONG |

SAVE then HOME is undo, HOME then SAVE is redo. SHIFT and SHIFT2 are not buttons of their own: [shortcuts.md](shortcuts.md) lists what each does with a key, a knob or a button.

### OCT-, OCT+ and both

- **The octave.** OCT- or OCT+ tapped (let go with nothing else used meanwhile) changes the keyboard octave by one, between -3 and +3, with the toast "OCTAVE: n". It acts on release, so holding either is a SHIFT, not an octave change. On STEP the piano roll follows the octave. On the drum track a tap does nothing.
- **Both OCT** tapped together: the track and MASTER toggle (back to the track you came from). Not on NAME, under a question, in a layer, or on pages with no track (GLOBAL, SYSTEM, PROJECT, SONG). On a synth track's STEP a quick tap (within 400 ms) toggles **FOLD** instead: the piano roll shows only the rows of the notes the pattern plays, or the scale's notes when a scale is set.
- On PROJECT a tap is the previous or next bank. On a held step OCT- / OCT+ move the step's notes an octave (synth tracks). On the tempo page and GLOBAL's Tempo lines they nudge the clock.

### The LEDs

A button's LED glows only where the button does something. The button of the page you are on is lit fully; a button that would do something now is lit dimly; a button that would do nothing is dark. So LFO is dark on the drum track, OCT- and OCT+ are dark where they have no job, and SAVE is dark on a STEP line that has no YES action. GLO is full on GLOBAL and SYSTEM and dim elsewhere.

PLAY has a second LED, green, which together with PLAY's red one is the transport light:

| State | Light |
|---|---|
| stopped | green dim |
| playing | green full, the red on each beat |
| recording | red full, the green on each beat |

REC's own LED is red: lit while a take records, blinking while armed and waiting for the first note. The count-in pulses the green on each beat.

## The mixer

The mixer is the first screen. The rows are tracks: MASTER above T1, then T1, T2, T3, DR, the drum track's 10 lanes plus the ACCENT lane, and, in a USB audio build, a USB IN row after them. ALGORITHM walks the rows; the panel shows the selected row's group. Each row has its meter and its sequence. SELECT pages the knob sets of the row, in the same order on every row:

- **Mix:** a track VOLUME and PAN; the drum track VOLUME; a lane LEVEL, CUT, DRIVE; the ACCENT lane its ACCENT amount (a velocity).
- **Inserts:** the row's two insert slots, S1 TYPE and amount, S2 TYPE and amount (see [effects.md](effects.md)). MASTER: its two slots, then THRS, RATIO, GAIN, CEIL while DYN is in a slot.
- **Sends:** REV, DLY, CHO (blank where no return holds that send) and card 4 PAN, on synth tracks, the drum track and the drum lanes. MASTER shows the returns, one card for each send a return holds (REV RET, DLY RET, CHO RET), the sends between returns, and DUCK where built.
- **USB IN:** VOLUME, PAN, FX ON, its two insert slots, REV, DLY, CHO. FX tapped on this row shows its inserts.

A muted track or lane is **struck through**: a line through its mixer row name, the header badge on its pages and its volume card. The mute is the sequencer's (the steps stop; what sounds rings out; keys you play still sound) and is saved with the project. There is no solo. On MASTER and USB IN the keys are silent.

## The tempo page: PLAY held

PLAY held past the hold time opens the tempo page; let go and the screen before returns. A short tap of PLAY stays start / stop. The page has TEMPO (BPM, NUDGE, SWING, SYNC), REC (MODE, LENGTH, START, CLICK) and REC 2 (QUANT).

- **BPM** 40 to 240. **SWING** 0 to 100.
- **NUDGE:** OCT- or OCT+ held makes the clock about 3.9 % slower or faster while held. The BPM value never changes, and nothing happens while an external clock is followed.
- **Tap tempo:** with PLAY held, tap a white key; two taps or more set the tempo.
- **SYNC** (the MIDI clock to follow): INT, USB, TRS, BLE or AUTO (AUTO shows the clock it follows, as A:USB). On GLOBAL it is CLK IN.
- **REC:** MODE FREE or TEMPO, LENGTH 1, 2 or 4 bars, START NOTE or COUNT (count-in), CLICK OFF, REC or ON. **QUANT** ON snaps a recorded note to the grid; OFF keeps its start as a nudge and its length as a gate lock.

## STEP

SEQ tapped opens STEP, one screen for synth tracks and the drum track. The 16 white keys are the 16 steps of the window; the window follows the playhead while playing, PRESETS scrolls it. Steps, held steps, the note picker and the pages PATTERN, TOOLS, GEN and GEN OUT are in [the sequencer](sequencer.md).

## PROJECT

PROJECT is where storage lives. A storage row (PROJECT, SNAPSHOT, USER) has up to four cards: **BANK** (only for a store of more than 16 slots), the operation (**LOAD**, the default, **SAVE** or **DELETE**), **ITEM** (the slot; the knob walks all of them and the bank follows) and, on PROJECT, **NEW** (a blank project but for the drum ACCENT lane's four on the floor, touching no slot). A build whose project slots are kept in RAM has no DELETE.

- The 16 white keys are the 16 slots of the current bank: key n selects item n and runs the operation on it, through a confirmation. YES runs it on the item the knob is on. A free slot has nothing to load or clear: a toast, no question. The operation returns to LOAD after each action, when the row changes or the screen is left, so a stray key never saves.
- **OCT- or OCT+ tapped** on a row with slots: the previous or next **bank**, with the toast "BANK n/N".
- **USER** (32 user presets: two banks) has an INIT card: the selected track's sound back to its engine's defaults. SHIFT2 + ITEM marks a slot a favorite. A user preset's LOAD over an edited sound is asked in red.
- The rows after them: TOOLS (CLRSQ, INIT, MISS, NEW), SLOOP (import of a SLOOP autosave, in builds with it) and MEMORY (USED as a percentage and FREE, the sections that still fit).
- NAME asks for a name before a save: the keys type, SAVE finishes, HOME deletes a character or cancels.
- SAVE held on PROJECT opens SONG; see [the sequencer](sequencer.md#sections-and-the-song).

## GLOBAL and SYSTEM

GLOBAL and SYSTEM are settings lists: one setting on a line, the name left and the value right, in pages of 4. SELECT moves a page at a time; knob k edits line k and makes it the cursor; ALGORITHM moves the cursor a line; SAVE is YES on the cursor line (an on / off value toggles, an action line runs); SHIFT + a knob returns a value to its default.

**GLOBAL**
- **Tempo:** BPM, NUDGE (read-out), SWING, TUNE (the master tuning, -50 to 50 cents).
- **Rec:** MODE, LENGTH, START, CLICK, QUANT.
- **MIDI:** CLK IN (the clock to follow: INT, USB, TRS, BLE, AUTO), CLK OUT (ON or OFF; amber when ON but there is nowhere to send it), MIDI OUT; CLK OFS and BT OFS ([midi-usb.md](midi-usb.md#midi-clock)). There is no MIDI IN line: notes come in through the track channels.
- **Channels:** T1, T2, T3, DR (the MIDI channel of each track).

**SYSTEM**
- **INFO:** the read-out below. Always the first page.
- **SCREEN:** BRIGHT (the screen backlight, in builds with it), KNOB COLORS (ON / OFF).
- **AUDIO:** LOWCUT (OFF, LOWCUT, or BASS+ in builds with it); FILT RESO, the resonance of every track's FILTER, 0 (clean) to 10, saved with the project (`TFLT_RESO`).
- **KEYS:** BUTTONS (OFF, LOW, MID, HIGH) and KEYS (OFF, C, WHITE, ALL), the backlight of the buttons and keys, in builds with LIGHTS.
- **USB:** SERIAL, ON or OFF; amber until a restart applies it (builds with the USB console).
- **BT MIDI** (builds with `BLE`): MODE, DEVICE, ACTIVITY, STATE. **BT AUDIO** (builds with `BT_AUDIO`): MODE, DEVICE, FORMAT, STATE. Both can be on at once; MODE, the devices list and SAVE: [midi-usb.md](midi-usb.md#bluetooth-midi-and-bluetooth-audio). A connect not up in 30 s says NO ANSWER.
- **CALIBRATE:** PANEL (teaches the panel's buttons and knobs, asked first) and HOLD (a button's hold time: 250, 350 or 500 ms).
- **USB AUDIO:** (USB audio builds) one line a source: MASTER, T1 to T3, DRUMS, DRUM BUS, DR1 to DR10. Each goes to NONE, 1, 2, 1+2, 3, 4 or 3+4 of the four capture channels sent to the computer. See [midi-usb.md](midi-usb.md).
- **ABOUT:** VERSION (and the commit it was built from), BUILD (date and time) and the repository address. **CREDITS** follows.

## SYSTEM > INFO

INFO is a text page, no knob edits it except KNOB 1. What a musician reads:

| Line | Meaning |
|---|---|
| CPU0 (CPU LOAD without a second core) | The audio render load in percent; with a second core it includes CPU0's wait for CPU1. KNOB 1 chooses what it shows: NOW, the maximum of the last 5 seconds, or the maximum since boot. Over 100 means a late audio half (a gap in the sound) |
| CPU1 | The share of each audio half the second core takes. Instead: 2ND CORE OFF and the reason (NO START, TIMEOUT, FAULT, FLASH, MISREAD, CANARY, AFTER CRASH, SETTING, PARKED, BLUETOOTH, OVERLOAD, SUPPLY). |
| UNDERRUNS | Losses: late audio halves, USB audio's and MIDI in's, and where |
| BATTERY | The cell's volts and charge as `~3.87V 72%`, with the 3 bars in the gauge; "USB" while powered by USB; NO READING before the first sample. Both numbers are estimates: the divider and the ADC reference are not measured (assumed 1:2 divider, 3.3 V reference, the usual Li-ion curve), so check them against a meter at the cell |

SHIFT + SAVE on this page clears the counters. Builds made with the builder item `SYS_DEBUG` add INTERNALS (the last reset, flash interrupt time, the second core's internals), USB STATS (USB audio counters), DROPS, GUARD and CLOCK lines. They are for finding a fault, not for playing, and no ready-made build has them.

## Connections

MIDI in and out, the clock, Bluetooth MIDI and audio, USB audio and the web editor: [midi-usb.md](midi-usb.md).

## The update loader

The update loader on the FM-1 draws one progress bar: the package check fills the first half, the write the second. The app's last step reads FLASHING. An update refuses another device's package before anything is erased. Optimist's packages are `FM-1_700` (development builds) and `FM-1_720` to `FM-1_769` (releases): [BUILDING.md](../../BUILDING.md), "Package identity".

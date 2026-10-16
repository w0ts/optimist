# The sequencer, step by step

How to write, edit and shape a pattern on Optimist: the steps and the STEP page, p-locks, motion recording, chance, conditions, nudge and micro timing, fills, the EUCLID generator, the TOOLS, the arpeggiator, scales and chords, patterns and scenes, sections and the song, snapshots, undo and redo, mute and solo, the performance macros and the drum ACCENT lane.

This is the user's guide for the sequencer. The manual [OPTIMIST.md](../../OPTIMIST.md) is the reference for the whole instrument (it describes SLOOP's way of playing in detail), [PATTERNS-DESIGN.md](../PATTERNS-DESIGN.md) and [SNAPSHOTS.md](../SNAPSHOTS.md) hold the design, and [UI-OPTIMIST-DESIGN.md](../UI-OPTIMIST-DESIGN.md) holds every decision of the Optimist UI.

Two user interfaces share one sequencer, chosen when the firmware is built (builder item `UI`): SLOOP's (hold a function button, touch a key; the default) and the Optimist UI (lists of rows, SELECT / ALGORITHM / PRESETS, SAVE = yes, HOME = no). Each section below says what the sequencer does, then the gesture on each. Many features are builder items; a build without the item has no such page (the builder shows the name, [BUILDER.md](../BUILDER.md)).

**SHIFT and SHIFT2 on the Optimist UI.** SHIFT is OCT- held, SHIFT2 is OCT+ held. OCT- and OCT+ change the octave when they are let go, and only if nothing else was used while they were held.

## Steps and the STEP page

Each of the four tracks (T1 T2 T3 synths, DR the drum track) has a pattern of up to 64 steps. A synth step holds a note or a chord (up to 4 notes), a level, a ratchet and a length (ties). A drum step holds, per drum lane, a hit with a level and a ratchet. The 16 white keys are the 16 steps of the page shown (steps 1-16, 17-32, 33-48, 49-64, up to the track's length).

**The PATTERN row** (SLOOP UI: SEQ with no step held; Optimist UI: STEP with no step held) has four values:

| Value | What it does | Default |
| --- | --- | --- |
| LEN | steps in the loop, 1 to 64. Each track loops on its own length, so polymeters stay in phase | 16 |
| DIV | step size: 1/4, 1/8, 1/16, 1/32, triplets 8T and 16T, and (builder item `DIV_LONG`) 1/2, 1BAR, 2BAR | 1/16 |
| SWING | MPC swing of the track, shown as 50 to 75 %. It adds to the global swing. Not on the triplets | 0 |
| GATE | how long each note sounds inside its step | 64 (of 1 to 127) |

**Entering steps.**

- An empty step: press its key and it is set (drums: with the lane picked; synths: with the note or chord played last).
- A set step: press and let go and it is cleared. Press and hold, or press and turn a knob, and it is edited instead; the step stays.
- A held step shows its cards: drums LEVEL (ghost, soft, norm, hard) and RATCHET (x1 to x4); synths NOTE, LEVEL, RATCHET and LENGTH (a longer note is written as ties; shorter clears its own ties). Several held steps are edited together.
- On the Optimist UI, a step is only toggled by a short tap. A press held past the hold time (SYSTEM > HOLD), or one during which a knob was turned, leaves the step as it was. The window follows the playhead while playing (FOLLOW); PRESETS scrolls it and stops the following, SHIFT + PRESETS follows again.
- On the Optimist UI a step that carries automation (a recorded motion or a p-lock of the sound, not its nudge, chance, fill or condition) is drawn in a pale yellow instead of its lane's or the track's colour: in the STEP grid and the synth roll, and in the mixer's step strips. A plain step keeps its own colour; the playhead stays white. On the drum track a lock of one lane marks that lane's step only, a lock of the track marks the step on every lane.
- On the Optimist UI, **while a step is held, PRESETS moves it left / right** (one step a detent; its locks, motion and extras go with it, the step it passes swaps into its place; the window pages when it crosses the edge; at the pattern's first or last step it stays and says so) and **ALGORITHM moves it up / down**: a synth note a semitone, a drum hit to the lane above or below. A held synth step of several notes: **SELECT picks the note** ALGORITHM moves (shown in a bright cyan on the roll). The edited note is kept in view under the cards. With no step held the encoders do what they always did, and the moves are one undo level.
- On the Optimist UI **REC held** (the ring) on a picked **drum lane** clears only that lane (*Clear SNARE*, then *SNARE CLEARED*), undoable; on a synth track or the whole drum track it clears the track.
- On the Optimist UI a held synth step moved with OCT- / OCT+ shifts its notes an octave. On the drum track, a **tapped drum hit cycles normal, hard, ghost, off** (a soft hit goes straight off). SEQ + a step clears a drum step completely; on a synth track SHIFT + a step clears its p-locks only (below), and SEQ held + keys sets the notes the next tapped steps take.
- The drum track: SHIFT + a key picks the drum lane (the key plays its sound; the last upper key is the ACCENT lane, below). The grid shows the lit steps of the picked lane.

On SLOOP's UI the same edits are on the SEQ layer: KNOB 1 sound or note, KNOB 2 LEVEL, KNOB 3 RATCHET, KNOB 4 LENGTH with a step held; with none held KNOB 2 DIV, KNOB 3 SWING, KNOB 4 LEN. The drum grid on the keys is builder item `DRUM_STEP`.

## P-locks (parameter locks)

A **p-lock** gives one step its own value of a sound parameter. The value holds for that step and the parameter goes back to the track's value at the next step without a lock. Builder item `PLOCK`.

- **Record:** hold a step, then turn the knob of the value to lock.
  - Optimist UI: with the step held, press a page button (ENV, LFO, FX, SCL or ARP): that page's cells become the step's events. A turn writes a lock; a cell that holds one shows a padlock mark. Press the same button again for the family's next page; SELECT pages through the step, its extras and then the sound pages. A held step with no note ("an empty step") takes the lock on the rest; no note is made. A press that is only a short tap on a set step does not change it.
  - SLOOP UI: on the SEQ layer, hold a step and turn PRESETS; ALGORITHM picks which value (the last sound value you turned on a page at first). The title reads *lock flt +12*.
- **Clear:**
  - One value: SHIFT + its knob (Optimist UI). On SLOOP's UI OCT- with the step held clears the step's nudge, locks and fill.
  - All p-locks of a step: **SHIFT + the step** (Optimist UI). The notes, nudge, fill, chance and motion of the step stay.
  - All locks, nudges, fills and motion of the track's pattern: **SHIFT + SEQ** (asked: "Clear T1 locks?").
- **Lock or hold:** with a lock cell selected, YES (SAVE) turns the lock into a hold event at that step (motion: the value goes on until the next event of that value); YES again turns it back into a step-only lock.
- **What can be locked:** every sound value, the ARP and SCL pages' values (ARP MODE to ORD, ROOT, SCALE, QNT, TRN, CHORD and the rest), the insert amounts and the engine's EDIT values. On the drum track the picked lane's TUNE, DECAY, SNAP, CLICK, BEND, CUT, DRIVE and LEVEL (the lane must have a hit on that step: otherwise "NO HIT ON THIS STEP", and removing the hit removes its locks) and the ACCENT amount.
- **What cannot be locked:** the LFO and ENV destination slots (DEST A / B), VOICE, ALLOC, PRIO, GLMODE and VOICES, MUTE, the FX bypass, the INSERTS S1 and S2 types, the drum lanes' SENDS, and the pattern's LEN, DIV and SWING. A lockable value is also recordable as motion and the other way round: they share one list of values.
- **The base value:** the first time a lock or a motion event changes a value, Optimist remembers the value it found (its base). PLAY, STOP, step 0 of a pass, a load, a pattern switch and an engine change put every automated value back. A knob turned while not recording sets the new base at once. A preset, MIDI or editor change made while playing on an automated value goes back at STOP.
- **Limit:** a pattern's events (locks, motion, nudges, fills, chance and conditions) share one list of 128 (AUTO_MAX in the source). A muted track's locks do not apply.

## Motion recording

**Motion recording** (builder item `MOTION`): while a track records (REC pressed while playing), turning a knob of a sound value records it on the step that is playing. When the step comes round again the value is set again, and it holds until the next event of that value. Filter sweeps, send throws, a decay that changes every step. Each pass starts from the sound as it was and STOP puts it back. Record from the Optimist UI by REC and a knob on any page of the track's sound; the macros record too.

- SLOOP UI: SEQ until the MOTION page: PLAY on / off for the track, EVNT (the events), FREE (what is left of the 128), CLEAR (twice).
- Optimist UI: clear the track's automation with SHIFT + SEQ (above); a recording pass is undone with the pass.
- Motion is saved with the project and with each song section.

## Chance and conditions

**Chance.** A step plays with a probability: 0 to 100 % in 5 % steps, default 100 %. When a step does not play it is a rest (its ratchets too). A tie is not rolled. Builder item `CHANCE`. Optimist UI: hold a step, SELECT to the extras page, the CHANCE card (drum and synth steps; on drums the whole step). SLOOP UI: SEQ again opens STEP 2, KNOB 2 PROB (synth tracks).

**Conditions.** A step **condition** a:b plays the step only on loop a of every b passes of its own track: 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4 (builder item `COND`). Each track counts the passes through its length; the count restarts on PLAY, on a song section and when a launched pattern takes over. Held step, extras page, the COND card (`-` is none; SHIFT + its knob clears it). The order on a step: the condition first, then FILL, then the chance. A step that fails any of them plays nothing and applies no lock. SLOOP's UI plays conditions and draws the dot; it has no page to edit them.

## Nudge, micro timing and fills

- **Nudge / micro timing** (`MICRO`): a step plays up to half a step early (-32) or just under half late (+31), in 1/64 of a step; its ratchets move with it. Optimist UI: held step, extras page, the NUDGE card. SLOOP UI: SEQ, hold a step, KNOB 4 NUDGE. Clear the nudge with SHIFT + its knob or OCT- with the step held. This is SLOOP 2.4's micro timing, kept in 2.4's own layout.
- **Fills** (`FILLS`): a step set to FILL ONLY plays only during a fill; NO FILL stays silent during one. Optimist UI: held step, SAVE cycles normal, FILL ONLY, NO FILL (or the FILL card in the extras). SLOOP UI: held step + OCT+. **Fills are played** with GLO + key 9 (a fill while held) and GLO + key 10 (the next bar is a fill; again cancels it). STOP ends both. With fills built the FX bypass moves from GLO + keys 9-12 to GLO + black keys 1-4.

## The note generator: EUCLID

STEP's last two pages (SELECT, or SEQ tapped again) are TOOLS and EUCLID (builder item `SEQTOOLS`, Optimist UI only). EUCLID spreads HITS as evenly as possible over STEPS, turned by ROT, repeated through the track's length: KNOB 1 HITS (default 4), KNOB 2 STEPS (default 16, at most LEN), KNOB 3 ROT (default 0), KNOB 4 ON / OFF. SHIFT + a knob gives its default.

- **ON** writes the notes into the pattern at once (heard). A synth hit keeps the step's own note, otherwise the root near C4; on the drum track only the picked lane's hits are written. Each change of a value writes it again from the pattern as it was when the generator began, and a run of turns is one undo level.
- **OFF asks "KEEP? EUCLID"**: YES **keeps** the generated steps in the pattern, NO **restores** the pattern as it was. One generator runs at a time, and a pattern edited by hand or a scene loaded ends it (the saved copy no longer matches).
- Nothing is saved in a project except the notes: a project saved while the generator is ON holds the generated steps. The values stay until power-off.

## TOOLS

Page TOOLS of STEP: four cards, a turn arms the card and YES (SAVE) does it, with a toast. Each is one undo level. The scope is what ALGORITHM selected: a synth track, the whole kit (DR), or one picked lane.

| Tool | What it does |
| --- | --- |
| REVERSE | mirrors the steps inside LEN. A note moves whole, with its ties |
| RANDOM | synth track: new notes on the same rhythm (a walk through the track's scale; on CHR the minor pentatonic of the root). Drums: a beat, or the picked lane drawn at random |
| COOK | a few changes: some notes a scale step or octave, a level NORM or HARD, a chance of 50 or 75 %; drums: some hits moved, added or taken. "NOTHING TO COOK" on an empty pattern |
| LENGTH | x2 (the pattern copied after itself, at most 64 steps, "PATTERN FULL" or "64 STEPS: NO ROOM" if not) or 1/2 (the steps past the new LEN are kept) |

Every tool works on steps 1 to LEN. The events of a step (locks, motion, chance, conditions, nudges, fills) move with it. The EDIT layer's SHIFT and LENGTH x2 move and copy the events too.

## The arpeggiator

Hold ARP and hold a key (note repeat, locked to the tempo and swing); on the Optimist UI ARP held is a layer with KNOB RATE, ARP tapped opens its pages. Two pages of the track:

- **ARP:** MODE (OFF, UP, DN, UPDN, RND, ORD), RATE (1/4, 1/8, 1/16, 1/32, 8T, 16T; default 1/16), OCT (1 to 4), GATE.
- **ARP 2:** SWG (swing), PROB (probability of a note), HOLD (on / off: keep the notes after the keys are let go) and ORD (NOTE or PLAY order).

The ARP values are p-locks and motion, like the sound values (above). A recorded roll is written as ratchets. On the drum track OCT- / OCT+ (SLOOP UI) make a roll ghost or hard.

## Scale and chords

The SCL page (and the layer: hold SCL, press a key to set the key of the song) holds ROOT, SCALE (16 scales: CHR MAJ MIN DOR MIX PEN MPEN HARM PHRY LYD LOC MEL BLUES WHOLE DIMHW DIMWH), QNT (OFF, SNAP, WHITE, ALL, and SEQ when `QNT_SEQ` is built: sequenced notes follow the scale) and CHORD (OFF, TRIAD, 7TH, 9TH, SUS4, POWER). **SCL 2** has TRN (transpose, -24 to +24 semitones), and with `CHORDPLUS` STRUM (-60 to +60 ms a note; above 0 low to high, below 0 high to low) and VLEAD (each chord voiced nearest the last). With a chord on, the white keys walk the scale and one finger plays the chord, recorded as a chord. These values are p-locks and motion too.

## Patterns and scenes

Builder item `PATTERNS`: each track keeps up to 16 stored patterns, and a **scene** names a pattern per track. The Optimist UI:

- **LFO held** is the patterns layer: a white key n launches the selected track's pattern n at the end of its current pass; SHIFT + n clears slot n (asked); black keys 1-4 pick the track, 5 stops, 9 duplicates the working pattern into the first free slot ("NO FREE PATTERN" if none), and black key 10 then a white key launches a scene. **SAVE + n** stores the working pattern into slot n (asked over a used one).
- SAVE + SEQ stores the working pattern into the slot it came from.
- The SONG screen has a PATTERNS row too.

See [PATTERNS-DESIGN.md](../PATTERNS-DESIGN.md).

## Sections and the song

Sections A to P (16, in four banks of four; a build may have 8 or 4) hold the four tracks: sounds, patterns, kit. **SAVE held** is the scenes layer on the Optimist UI: white key n starts scene A-P at the next bar (stopped: loads it), REC held + n stores the loop into n, SHIFT + n clears n (asked), SAVE + PLAY plays the song from its start, SAVE + REC tapped records the song as you play the sections, and two taps or more of keys while holding SAVE queue a quick chain (`QCHAIN`, up to 8, not saved). The SONG screen edits the chain by hand. The sections are stored compressed in one log; the SONG title shows how full the memory is. On SLOOP's UI see "Song mode" in [OPTIMIST.md](../../OPTIMIST.md).

## Snapshots

A **snapshot** keeps the working project, every section and the song in one slot (builder item `SNAPSHOTS`: 2, 4 or 8 slots, plus B: BEFORE LOAD, the state before your last load). SAVE > SNAPSHOT: KNOB 4 SAVE, KNOB 2 LOAD, KNOB 3 CLEAR (one detent arms, a second within 1.5 s does it). Only when the sequencer is stopped and quiet. Samples, user presets, kits and the FM6 bank are not copied. See [SNAPSHOTS.md](../SNAPSHOTS.md).

## Undo and redo

Every step key pressed, every tool, every recording pass, erase, clear and held-layer edit opens one **undo** level, many levels deep, on every track including the drums. Optimist UI: SAVE then HOME is undo; HOME then SAVE is **redo**; EDIT + OCT- / OCT+ do the same. SLOOP UI: hold EDIT and press OCT- (undo) or OCT+ (redo). Loading a project or playing a section clears the history. How many levels depends on the memory the build leaves free ([OPTIMIST.md](../../OPTIMIST.md), "Undo, clear, save, autosave").

## Mute and solo

There are two kinds of **mute**.

- **The mixer mute** silences the sound (the MIDI out goes on). Optimist UI: SHIFT2 with the knob of a mixer row's volume card toggles it; the muted track is struck through in the mixer, in the header badge and on its volume card. SLOOP UI and the GLO layer: keys 1-4 mute tracks 1-4, keys 5-8 **solo** them (several solos add up). A muted track fades out in a few ms.
- **The sequencer mute** stops the steps from triggering notes, for the internal sound and for MIDI out, while the notes already sounding end by their gates; a muted track's locks do not apply, a muted lane has no locks or motion; keys played live still sound. Optimist UI, on the mixer: **SHIFT + a key**: the 16 white drum keys are the 16 lanes, the black keys F#, G#, A#, C# are T1, T2, T3 and DR, and the last upper key is the ACCENT lane. The ACCENT row's MUTE card does the same. It is saved with the project.

## Performance macros: COLOR, MOTION, SPACE, ENERGY

Four knobs (builder items `MACROS` and `ENERGY`), each from -100 % to +100 %; at 0 % (home) nothing moves. On the Optimist UI they are the last page of the MASTER FX screen, MACRO, named COLOR, MOTN, SPACE and ENRGY (SLOOP UI: GLO until MACRO).

| Macro | Right | Left |
| --- | --- | --- |
| **COLOR** | the tracks' brightness up (each engine's cutoff, index or tone value) and the delay's colour | down |
| **MOTION** | the LFO depths and rate up, a touch of chorus depth | fade to 0 |
| **SPACE** | the chorus, delay and reverb sends up, reverb size, delay feedback, tracks spread | sends fade, pans close, a smaller reverb |
| **ENERGY** | brightness a little up, DIST on tracks 1-3, drum level up; with `ENERGY` the drum track is thinned (left) or thickened (right) in five bands, the pattern itself never changed | the opposite |

They sit on top of your own values: every page still shows your values and a macro back at 0 brings the sound back exactly. They are saved with the project and each song section, and recorded as motion when a track records.

## The drum ACCENT lane

Builder item `DRUM_ACCENT`: a 17th step lane on the drum track, 909-style. It has no sound; its steps raise every hit of that step by the drum track's ACCENT amount (default 27, 0 to 127). It behaves like any lane: steps, levels, ratchets, chance and conditions, TOOLS and EUCLID, patterns, sections and snapshots. Optimist UI: the last upper key picks it (SHIFT + the key); STEP shows an "AC" row; the amount is the fourth card of the SOUND row, locks on a step with an accent hit there and records as motion; ALGORITHM walks on past the 16th lane to the ACCENT row of the mixer, whose MUTE card is the lane's sequencer mute. Off in the build: the lane is kept and edited but not heard.

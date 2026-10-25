# The sequencer, step by step

How to write, edit and shape a pattern: the STEP page, p-locks, motion recording, chance, conditions, nudge, fills, the GEN note generator, the TOOLS, the arpeggiator, scales and chords, patterns and scenes, the song, snapshots, undo, mute, the performance macros and the drum ACCENT lane. Every gesture is also in [shortcuts.md](shortcuts.md).

Many features are builder items; a build without the item has no such page ([BUILDER.md](../BUILDER.md) names them). SHIFT is OCT- held, SHIFT2 is OCT+ held; OCT- and OCT+ change the octave when they are let go, and only if nothing else was used while they were held.

## Steps and the STEP page

Each of the four tracks (T1 T2 T3 synths, DR the drum track) has a pattern of up to 64 steps. A synth step holds a note or a chord (up to 4 notes), a level, a ratchet and a length (ties). A drum step holds, per drum lane, a hit with a level and a ratchet. The 16 white keys are the 16 steps of the window shown (steps 1-16, 17-32, 33-48, 49-64, up to the track's length). The window follows the playhead while playing; PRESETS scrolls it and stops the following, SHIFT2 + PRESETS follows again.

**The PATTERN page** (STEP with no step held) has four values:

| Value | What it does | Default |
| --- | --- | --- |
| LEN | steps in the loop, 1 to 64. Each track loops on its own length, so polymeters stay in phase | 16 |
| DIV | step size, any note value of the tempo (see Note values below) | 1/16 |
| SWING | MPC swing of the track, shown as 50 to 75 %. It adds to the global swing. Not on the triplets | 0 |
| GATE | how long each note sounds inside its step | 64 (of 1 to 127) |

STEP has more pages after PATTERN: TOOLS (builder item `SEQTOOLS`), GEN and GEN OUT (`NOTEGEN` or `GEN_GRIDS`), each only in a firmware built with it. SELECT or SEQ tapped moves on.

**Entering steps.**

- An empty step: tap its key and it is set (drums: on the picked lane; synths: with the note or chord played last). A set step: tap it and it is cleared. A press held past the hold time (SYSTEM > CALIBRATE > HOLD), or one during which a knob was turned, edits the step and leaves it as it was.
- On the drum track a tapped hit goes off, whatever its level. Hold a lane's black key and press a step to cycle that lane's hit there: NORM, HARD, GHOST, NORM; a popup says the level (the held step's LEVEL card shows it too). On an empty drum step, SHIFT + the key sets a ghost hit and SHIFT2 + the key a hard hit.
- A held step shows its cards: drums LEVEL (ghost, soft, norm, hard), RATCHET (x1 to x4), TUNE and DECAY of the lane; synths NOTE, LEVEL, RATCHET and LENGTH (a longer note is written as ties). Several held steps are edited together. SELECT pages through the step, its extras (CHANCE, NUDGE, FILL, COND) and the sound pages as p-locks.
- **While a step is held,** PRESETS moves it left / right (its locks, motion and extras go with it) and ALGORITHM moves it up / down: a synth note a semitone, a drum hit to the lane above or below. OCT- / OCT+ move a synth step's notes an octave. A held synth step of several notes: SELECT picks the note ALGORITHM moves. HOME tapped clears the held step.
- **The cards stay longer.** The page's cards (PATTERN, TOOLS, GEN...) go about 1.5 s after the last turn or page change; SELECT turned or SEQ tapped brings them back. SHIFT2 + SELECT turned keeps them about 30 s after each turn instead, so a generator can be tuned while it plays, until you leave STEP. A held step's cards always go about 1.5 s after it is let go.
- **The note picker.** With no step held and the cards down, KNOB 1 picks the next or previous note, one note at a time: left to right, and in a chord the lowest first (a note held on from the step before is not picked there; drums: hit of the picked lane). KNOB 2 moves it in time (SHIFT held: its nudge), KNOB 3 its pitch (drums: its lane), KNOB 4 its length (drums: its level). SAVE opens its cards; SAVE held, let go: the note is muted (kept, played as a rest); HOME held, let go: the note is deleted.
- **SEQ held + keys** on a synth track: the notes played become the next steps. SEQ held alone and let go: the keys play with STEP up; again: they are steps again.
- **FOLD.** On a synth track's STEP, OCT- and OCT+ tapped together quickly: the piano roll shows only the rows of the notes the pattern plays, or the scale's notes when a scale is set.
- A step that carries a p-lock or recorded motion (not a nudge, chance, fill or condition) is drawn in the automation colour. On the drum track a lock of one lane marks that lane's step only.
- **REC held** (the ring) clears recorded material: on a picked drum lane only that lane (*Clear SNARE*), else the track. Undoable.
- **The drum track.** The 10 lane keys (the black keys F#3 to D#5; F#5 is the ACCENT lane, below) play and pick the lane. The grid shows the steps of the picked lane. See [drums.md](drums.md).

## P-locks (parameter locks)

A **p-lock** gives one step its own value of a sound parameter. The value holds for that step and the parameter goes back to the track's value at the next step without a lock. Builder item `PLOCK`.

- **Record:** hold a step, then press a page button (ENV, LFO, FX, SCL or ARP; drums also EDIT): that page's cells become the step's events. A turn writes a lock; a cell that holds one shows a padlock mark. Press the same button again for the family's next page, or SELECT through the step, its extras and then the sound pages. A held step with no note takes the lock on the rest; no note is made.
- **Clear:** one value: SHIFT + its knob on the step's card. All p-locks of a step: **SHIFT + the step key** (the notes, nudge, fill, chance and motion stay). A value on every step of the track: **REC held + its knob**. All locks, nudges, fills and motion of the track: **SHIFT + REC held** until the ring closes. A drum lane only: pick it first.
- **Lock or hold:** with a lock cell selected, YES (SAVE) turns the lock into a hold event at that step (motion: the value goes on until the next event of that value); YES again turns it back into a step-only lock.
- **What can be locked:** every sound value, the ARP and SCL pages' values, the insert amounts and the engine's EDIT values. On the drum track the picked lane's TUNE, DECAY, SNAP, CLICK, BEND, CUT, DRIVE and LEVEL (the lane must have a hit on that step: otherwise "NO HIT ON THIS STEP", and removing the hit removes its locks) and the ACCENT amount.
- **What cannot be locked:** the LFO and ENV destination slots (DEST A / B), VOICE, ALLOC, PRIO, GLMODE and VOICES, MUTE, the FX bypass, the INSERTS S1 and S2 types, the drum lanes' SENDS, and the pattern's LEN, DIV and SWING. A lockable value is also recordable as motion and the other way round.
- **The base value:** the first time a lock or a motion event changes a value, Optimist remembers the value it found; PLAY, STOP, step 0 of a pass, a load, a pattern switch and an engine change put it back. A knob turned while not recording sets the new base.
- **Limit:** a pattern's events (locks, motion, nudges, fills, chance and conditions) share one list of 128. A muted track's locks do not apply.

## Motion recording

**Motion recording** (builder item `MOTION`): while a track records (REC pressed while playing), turning a knob of a sound value records it on the step that is playing. When the step comes round again the value is set again, and it holds until the next event of that value. Each pass starts from the sound as it was and STOP puts it back. Record by REC and a knob on any page of the track's sound; the macros record too. A recording pass is undone with the pass. Motion is saved with the project and with each song section.

## Chance and conditions

**Chance.** A step plays with a probability: 0 to 100 % in 5 % steps, default 100 %. When a step does not play it is a rest (its ratchets too). A tie is not rolled. Builder item `CHANCE`. Hold a step, SELECT to the extras page, the CHANCE card.

**Conditions.** A step **condition** a:b plays the step only on loop a of every b passes of its own track: 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4 (builder item `COND`). Each track counts the passes through its length; the count restarts on PLAY, on a song section and when a launched pattern takes over. Held step, extras page, the COND card (`-` is none). The order on a step: the condition first, then FILL, then the chance. A step that fails any of them plays nothing and applies no lock.

## Nudge, micro timing and fills

- **Nudge / micro timing** (`MICRO`): a step plays up to half a step early (-32) or just under half late (+31), in 1/64 of a step; its ratchets move with it. Held step, extras page, the NUDGE card; SHIFT + its knob clears it.
- **Fills** (`FILLS`): a step set to FILL ONLY plays only during a fill; NO FILL stays silent during one. Held step, SAVE cycles normal, FILL ONLY, NO FILL (or the FILL card in the extras). Play a fill with the GLO layer: key 9 is a fill while held, key 10 makes the next bar a fill (again cancels it). With fills built the FX bypass is on the GLO layer's black keys 1-4.

## The note generator: GEN

STEP's pages GEN and GEN OUT (builder items `NOTEGEN`: EUCLID, RANDOM, PULSE, and `GEN_GRIDS`: GRIDS, either one alone; apart from the TOOLS page's `SEQTOOLS`) hold a note generator for the selected track (a drum track: the picked lane). It plays over the pattern and writes nothing into it until you say so.

- **GEN** has TYPE (only with more than one type built) and three values. EUCLID spreads HITS as evenly as possible over STEPS, turned by ROT (defaults 4, 16, 0; STEPS at most LEN). RANDOM has DENSITY, SEED and ACCENT. PULSE has EVERY, OFFSET and RATCHET. GRIDS (builder item `GEN_GRIDS`) is the drum map of Mutable Instruments Grids: X and Y (0..127) move over a map of 25 drum patterns and DENS sets how many steps play; the strongest play HARD. On a lane it plays the Grids part of the lane's sound (kicks BD, snares, claps and rims SD, the rest HH; the ACCENT lane where any part is HARD); T1 T2 T3 play BD SD HH. Another TYPE starts at its default values; SHIFT + a knob gives a value its default. A turn is heard at the next step.
- **GRIDS on the whole kit** (`GEN_GRIDS`): on DR itself (ALGORITHM on DR, no lane picked) the GEN page becomes two pages, **GRIDS** (X, Y, CHAOS) and **DENSITY** (BD, SD, HH), and GEN OUT turns the kit's GRIDS on. It plays Grids' three parts on three lanes: BD on the first KICK lane, SD on the first SNARE lane (else the first snare, clap or rim), HH on the CLOSED HAT lane (else the first hat), and the ACCENT lane where a part plays HARD. The other lanes are left alone, and a lane with its own generator keeps it. CHAOS is Grids' random push, a new draw each bar (16 steps), so WRITE keeps the bar playing then. The six values belong to the drum track: they are saved with the project, and turning them while recording records them as motion (a held step's locks do not reach them yet). One bar of 16ths, repeated through LEN.
- **Grids' Euclidean mode** (builder item `GRIDS_EUCLID`): GRIDS gets a fourth card, **OUT** (DRUMS or EUCL). In EUCL the three parts play Euclidean rhythms instead of the drum map, as Grids does in its Euclidean mode: the cards read BD LEN, SD LEN and HH LEN, each part's cycle of 1 to 32 steps (X, Y and CHAOS turned), and DENSITY's BD, SD and HH show how many of those steps hit, spread as evenly as they go (Grids' own table). Each part runs on through the passes on its own length, so a 5-step kick against a 16-step LEN shifts every pass. There is no accent in this mode, as on Grids. OUT is saved with the project.
- **The kit's LFOs** (builder item `GRIDS_LFO`, with the drum editor): on DR itself (no lane picked) the LFO button opens two LFOs of the drum track, **LFO** and **LFO 2**, that move the kit's GRIDS. Each has RATE (free, or a note value of the tempo), WAVE, PHS (where it starts at PLAY) and FADE, and on its DEST page two destinations among X, Y, CHAOS, BD, SD and HH with an amount each. GRIDS plays the moved values step by step, so an LFO on X walks the drum map, one on BD breathes the kick in and out, and in EUCL they move the lengths and fills. The values you set stay as they are, and the LFOs are saved with the project.
- **Grids' velocity** (builder item `GRIDS_VEL`): DENSITY gets a fourth card, **VEL** (ON by default). With VEL on, every hit of the kit's GRIDS plays at the dynamic of its level on the drum map: the main beats HARD or NORM, the in-between ones SOFT or as ghosts, into the drum sounds and MIDI out. The drum steps keep four dynamics, so STEP draws them as such and WRITE keeps them. VEL OFF plays as before, Grids' accent only: HARD where the level is over 192, the other hits NORM (or the step's own level where the lane already has a hit). It works in DRUMS mode; EUCL has no levels.
- **GEN OUT** has GEN (ON / OFF, heard at once), MODE and WRITE. MODE: REPLACE (the generator's hits are the steps), ADD (the pattern's steps and the hits) or MASK (the pattern's steps only where the generator hits). A generated hit on a synth track plays the pattern's note there, else the track's ROOT; on a drum lane the pattern's level and ratchet, else NORM.
- **Off:** GEN OFF and the pattern is exactly as you left it; there is nothing to keep or restore.
- **WRITE** (asked) keeps what plays: it writes the generated steps into the pattern as ordinary steps and turns GEN OFF. One undo (SAVE then HOME) restores the pattern and GEN ON again.
- The generators are saved with the project.

## TOOLS

Page TOOLS of STEP: the TOOL card picks the tool, the ON card says what it works on (the track, or the picked lane). A turn arms the card and YES (SAVE) does it, asked first, with a toast. Each is one undo level. The scope is what ALGORITHM selected: a synth track, the whole kit (DR), or one picked lane.

| Tool | What it does |
| --- | --- |
| REVERSE | mirrors the steps inside LEN. A note moves whole, with its ties |
| RANDOM | synth track: new notes on the same rhythm (a walk through the track's scale; on CHR the minor pentatonic of the root). Drums: a beat, or the picked lane drawn at random |
| COOK | a few changes: some notes a scale step or octave, a level NORM or HARD, a chance of 50 or 75 %; drums: some hits moved, added or taken. "NOTHING TO COOK" on an empty pattern |
| DOUBLE, HALF | the pattern copied after itself (at most 64 steps, "PATTERN FULL" or "64 STEPS: NO ROOM" if not), or LEN halved (the steps past the new LEN are kept) |
| QNT 1, QNT 1/2, QNT 1/4 | QUANTIZE: the nudged starts (REC QUANT OFF, or NUDGE on the STEP page) move to the nearest whole, half or quarter step; QNT 1 puts every note back on the grid. The lengths stay. A nudge belongs to the step, so on DR it is the whole kit even with a lane picked. "ON THE GRID ALREADY" when nothing is nudged |

Every tool works on steps 1 to LEN. The events of a step (locks, motion, chance, conditions, nudges, fills) move with it. The EDIT layer's SHIFT and LENGTH x2 move and copy the events too.

## The arpeggiator

ARP held is a layer: hold a key for note repeat, locked to the tempo and swing, with KNOB RATE. ARP tapped opens its pages. With ARP on, the track's own steps go through the arp too. While recording, only what the keys play through the arp is recorded. The drum track has no arp. Two pages of the track:

- **ARP:** MODE (OFF, UP, DN, UPDN, RND, ORD, BASS: every first note the lowest held, every second one of the others in turn, in the order played), RATE (any note value of the tempo; default 1/16), OCT (1 to 4), GATE (1 to 198 % of the step; past 100 % the note goes on under the next one).
- **ARP 2:** SWG (swing), PROB (probability of a note), HOLD (on / off: keep the notes after the keys are let go) and ORD (NOTE or PLAY order).

The ARP values are p-locks and motion, like the sound values. A recorded roll is written as ratchets.

## Scale and chords

The SCL page (and the layer: hold SCL, press a key to set the key of the song) holds ROOT, SCALE (16 scales: CHR MAJ MIN DOR MIX PEN MPEN HARM PHRY LYD LOC MEL BLUES WHOLE DIMHW DIMWH), QNT (OFF, SNAP, WHITE, ALL, and SEQ when `QNT_SEQ` is built: sequenced notes follow the scale) and CHORD (OFF, TRIAD, 7TH, 9TH, SUS4, POWER). **SCL 2** has TRN (transpose, -24 to +24 semitones), and with `CHORDPLUS` STRUM (-60 to +60 ms a note) and VLEAD (each chord voiced nearest the last). With a chord on, the white keys walk the scale and one finger plays the chord, recorded as a chord. These values are p-locks and motion too. With `CHORDPLUS` the black keys change the chord while held: F# major <-> minor, G# the 7th, A# sus4, C# the 9th, D# an inversion; tapped twice quickly one stays on for the track (lit full); tapped once more it goes off when the key is let go.

## Patterns and scenes

Builder item `PATTERNS`: each track keeps up to 16 stored patterns, and a **scene** names a pattern per track. **LFO held** is the patterns layer:

- A white key n launches the selected track's pattern n at the end of its pass (OCT- + the key: at the next bar; OCT+ + the key: now). SAVE + n stores the working pattern into slot n (asked over a used one). HOME + n clears slot n (asked).
- Black keys 1-4 pick the track, 5 stops the pattern, 9 duplicates the working pattern into the first free slot ("NO FREE PATTERN" if none).
- Black key 10 turns the white keys into scenes A-P (until black 10 again or LFO is let go): the same verbs launch, store and clear a scene. SEQ + n stores the four tracks' patterns as scene n.
- SAVE + SEQ stores the working pattern into the slot it came from. The SONG screen has a PATTERNS row with the same keys.

## Sections and the song

Sections A to P (16; a build may have 8 or 4) hold the four tracks: sounds, patterns, kit. In the LFO layer's scene keys: a white key starts the scene at the next bar (stopped: loads it, asked over changed work), SAVE + n stores the loop into it, HOME + n clears it (asked). Two taps or more of scene keys in one hold while playing queue a quick chain (`QCHAIN`, up to 8, not saved). SEQ + PLAY plays the song from its start, SEQ + REC tapped records the song as you play the sections.

**SONG** is SAVE held on PROJECT, SEQ held (not on STEP) or SHIFT2 + SEQ. It has the scenes (a row each: the pattern each track plays in it; SAVE launches, REC stores the loop, SHIFT + REC or SAVE held clears), the PATTERNS row, MODE (LOOP or SONG, SONG REC, SAVE writes the chain) and the chain's parts (SCENE, BARS, INSERT, DELETE). The sections are stored compressed in one log; PROJECT > MEMORY shows how full it is.

## Snapshots

A **snapshot** keeps the working project, every section and the song in one slot (builder item `SNAPSHOTS`: 2, 4 or 8 slots, plus B: BEFORE LOAD, the state before your last load). It is the SNAPSHOT row of PROJECT: pick LOAD, SAVE or DELETE and the slot. Only when the sequencer is stopped and quiet. Samples, user presets, kits and the FM6 bank are not copied.

## Undo and redo

Every step key pressed, every tool, every recording pass, erase, clear and held-layer edit opens one **undo** level, many levels deep, on every track including the drums. SAVE then HOME is undo; HOME then SAVE is **redo**. Loading a project or playing a section clears the history. How many levels depends on the memory the build leaves free.

## Mute

There is one mute, the sequencer's, and no solo. A muted track or lane plays no more steps, for the internal sound and for MIDI out; notes already sounding end by their gates, a muted track's locks do not apply, and keys played live still sound. Toggle it with SHIFT + a key (the first four white keys are T1 T2 T3 DR; the black keys F#3 to D#5 the 10 lanes; F#5 the ACCENT lane), SHIFT + the volume knob on the mixer, or the GLO layer's keys 1-4. The mixer strikes a muted track through. It is saved with the project. It never touches a track's volume or its sound mute. While SHIFT is held the keys of the tracks and lanes light when a note is playing on them. The last three white keys (E F G, not on STEP, where they are steps) mute the INPUT of returns A B C: no send reaches the return, its bus and tail run on; the RET cards' SHIFT mutes a return's output. These are saved with the project too.

## Performance macros: COLOR, MOTION, SPACE, ENERGY

Four knobs (builder items `MACROS` and `ENERGY`), each from -100 % to +100 %; at 0 % nothing moves. They are the last page of the MASTER FX screen, MACRO, named COLOR, MOTN, SPACE and ENRGY.

- **COLOR:** right, the tracks brighter (each engine's cutoff, index or tone value) and the delay's filter higher; left, darker.
- **MOTION:** right, the LFO depths and rate up and a touch of chorus depth; left, the motion fades to 0.
- **SPACE:** right, the chorus, delay and reverb sends up, a bigger reverb, more delay feedback, tracks spread; left, the sends fade, pans close, a smaller reverb.
- **ENERGY:** right, brightness a little up, DIST on tracks 1-3, drum level up; with `ENERGY` the drum track is also thickened in five bands (left: thinned), the pattern itself never changed.

They sit on top of your own values: every page still shows your values and a macro back at 0 brings the sound back exactly. They are saved with the project and each song section, and recorded as motion when a track records.

## The drum ACCENT lane

An extra step lane on the drum track, 909-style, always there in a firmware with the automation store (motion, locks, chance or conditions: the lane is stored as their events). It has no sound; its steps raise every hit of that step by the drum track's ACCENT amount (default 27, 0 to 127). It behaves like any lane: steps, levels, ratchets, chance and conditions, TOOLS and GEN, patterns, sections and snapshots. The last upper key (F#5) picks it; STEP shows an "AC" row; the amount is card 2 of the drum track's SOUND row ("Lane"), locks on a step with an accent hit there and records as motion; ALGORITHM walks on past the last lane to the ACCENT row of the mixer. Mute it with SHIFT + its key. A default project (power-on with nothing stored, TOOLS > NEW) starts with the lane four on the floor: a NORM accent on every beat, through the whole pattern, and no hit anywhere; the project still counts as empty, so the first take sets the tempo and the click plays as before. Off in the build: the lane is kept and edited but not heard.

## Note values

Every duration that follows the tempo draws from one list, longest first: 4BAR, 3BAR, 2BAR, 1BARD, 1BAR, 1/2D, 1T, 1/2, 1/4D, 2T, 1/4,
1/8D, 4T, 1/8, 1/16D, 8T, 1/16, 1/32D, 16T, 1/32, 32T, 1/64. T is a triplet (two thirds of the value), D dotted (one and a half
times it): 8T is a triplet eighth, 1/8D a dotted eighth, 1BARD a bar and a half. It is the list of the ARP's RATE, the track's DIV,
the delay's TIME, a synced LFO's RATE (turned below its slowest free value), the SLICER's and the GATER's RATE and ROLL. Only the
straight values of a beat or less swing. A delay longer than its line plays at half, or a quarter, of the value, still on the beat.
Projects, presets and backups saved before 2026-10-22 keep their stored numbers, which now name other values: set the values again.
SLOOP projects and the web editor's SLOOP import are converted.

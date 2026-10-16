# Shortcuts: every gesture of the Optimist UI

Every button, knob and key gesture of the Optimist UI (builder item `UI`, value 1), grouped by what you want to do, then the ones that belong to one page. The guides explain them: [interface](interface.md) for the controls and pages, [sequencer](sequencer.md) for steps, p-locks and patterns. A gesture whose feature is not in your build does nothing (the builder item is named in brackets).

## Reading this page

| Word | Means |
|---|---|
| **SHIFT** | OCT- held |
| **SHIFT2** | OCT+ held |
| **tap** | press and let go before the hold time (350 ms; SYSTEM > CALIBRATE > HOLD: 250, 350 or 500 ms) with nothing else used meanwhile. SAVE, HOME, the page buttons and the OCT buttons act **when let go** |
| **held** | kept down past the hold time: a layer's key map shows (it works from the press) |
| **A + B** | A held, then B |
| **A then B** | A pressed first, then B, both down; it acts when one is let go |
| **YES / NO** | SAVE / HOME. A question ("Clear T1 locks?") waits 3 s: SAVE yes, HOME no, anything else cancels it |
| **a lane picked** | a drum lane selected (its black key, ALGORITHM past DR, a lane row on the mixer) |

## Moving around

| Gesture | Where | Does |
|---|---|---|
| ALGORITHM | track pages, mixer, SCOPE, the layers | the track: MASTER, T1, T2, T3, DR, the drum lanes, ACCENT (stops at the ends) |
| ALGORITHM | GLOBAL, SYSTEM | the cursor, one line |
| SELECT | most pages | the cursor, one row |
| SELECT | mixer | the selected row's knob sets (MIX, INSERTS, SENDS) |
| SELECT | GLOBAL, SYSTEM | a page of four lines |
| OCT- and OCT+ tapped together | track pages | the track <-> MASTER (back to the page you left) |
| OCT- and OCT+ tapped together | a synth track's STEP | **FOLD** the piano roll (see Steps) |
| ENV, LFO, EDIT, FX, SCL, ARP tapped | a track | SOUND, that family's rows; again: its next row |
| FX tapped | the mixer's MASTER row | the master FX page; again: its next page |
| SEQ tapped | anywhere | STEP; on STEP: its next page (PATTERN, TOOLS, GEN, GEN OUT) |
| GLO tapped | anywhere | GLOBAL; on GLOBAL: SYSTEM; on SYSTEM: GLOBAL |
| HOME tapped | a page | back to the mixer, on its row |
| HOME tapped | the mixer | SCOPE; HOME again: the mixer |
| SAVE tapped | where YES has nothing to do (the mixer, most pages) | PROJECT |
| SAVE + SELECT turned | anywhere | SONG, on the scene playing |

## Changing values and sounds

| Gesture | Where | Does |
|---|---|---|
| KNOB 1-4 | a page | card 1-4; the turned card is the cursor and the help line names it |
| SHIFT + a knob | a page | the value back to its default |
| LFO + a knob | PATTERN's LEN | by one step, not in powers of two |
| SAVE | a card that is an action, an on / off or a GO | does it (an action asks first) |
| PRESETS | a synth track | its preset (asks "Discard T1 edits?" over edits) |
| PRESETS | DR, a lane on the kit | the kit |
| PRESETS | a lane on its own source | that lane's sound |
| SHIFT + PRESETS | a synth track | the next / previous instrument (engine) |
| SHIFT + PRESETS | a lane on its own source | the next / previous kind (user, sampled, synth, X0X) |
| SHIFT2 + PRESETS | a synth track | the sound a favorite (right) or not (left) [FAVORITES] |
| SHIFT2 + SELECT | a synth track | browse the favorites only (right) or every sound (left) [FAVORITES] |
| SHIFT + ENV | a synth track | INIT the sound (asked over edits) |

## Playing

| Gesture | Where | Does |
|---|---|---|
| OCT- / OCT+ tapped | a synth track | the octave, -3 to +3 |
| OCT- held + keys | the drum track | ghost hits |
| OCT+ held + keys | the drum track | hard hits |
| a black key | the drum track | lanes 1-10 on F#3..D#5, ACCENT on F#5: off STEP it plays and picks the lane; on STEP it picks it (heard when stopped, silent while playing) |
| a white key | the drum track, any page | a step of the selected lane (the 16 steps of the window), as on STEP |
| the keys | MASTER, SCOPE, GLOBAL, SYSTEM, PROJECT, SONG | silent (MASTER keeps the track's keys when you came with both OCT) |

## Transport, tempo and recording

| Gesture | Where | Does |
|---|---|---|
| PLAY tapped | anywhere | start / stop (when let go) |
| PLAY held | anywhere | the TEMPO page while held (BPM, NUDGE, SWING, SYNC; REC: MODE, LENGTH, START, CLICK; QUANT) |
| PLAY + white keys | anywhere | tap tempo (two taps or more) |
| PLAY + OCT- / OCT+ held | TEMPO page, GLOBAL's Tempo lines | nudge the clock 3.9 % slower / faster while held (not on an external clock) |
| SHIFT + PLAY | anywhere | stop, every voice off (ALL SOUND OFF) |
| SAVE + PLAY | anywhere | the song from its start |
| REC tapped | stopped | arm: the first note starts recording (with START = COUNT, PLAY gives a bar of clicks first) [REC_MODES] |
| REC tapped | playing | record the selected track; again: REC OFF |
| REC tapped | a free take (empty project, MODE FREE) | close the take at the next downbeat; PLAY drops it |
| SAVE + REC tapped | anywhere | SONG REC on / off |

## Steps (STEP)

| Gesture | Where | Does |
|---|---|---|
| a white key tapped | STEP | the step set with the pick, or cleared (a cleared synth step remembers its note); drums: NORMAL, HARD, GHOST, off |
| OCT+ + an empty drum step | STEP, drums | a hard hit |
| PRESETS | no step held | scroll the 16-step window (stops following) |
| SHIFT + PRESETS | no step held | follow the playhead again |
| SELECT | no step held | the next card page |
| a step key held | STEP | the cards are the step's: synth NOTE, LEVEL, RATCHET, LENGTH; drums LEVEL, RATCHET, TUNE, DECAY. Several held steps edit together |
| step held + a knob | STEP | that value; SHIFT + the knob: back to NORMAL, x1, one step |
| step held + PRESETS | STEP | move the step in time (its locks and motion go with it) |
| step held + ALGORITHM | synth | the selected note a semitone up / down; folded: to the next row shown |
| step held + ALGORITHM | drums | the hit to the lane above / below |
| step held + SELECT | STEP | a chord's note to move, then the cards' pages (the step, its extras, its p-lock pages) |
| step held + OCT- / OCT+ | synth | its notes an octave down / up |
| step held + SAVE | STEP | on a p-lock card: lock <-> held motion; else the fill condition: NORMAL, FILL ONLY, NO FILL [FILLS] |
| step held + ENV, LFO, FX, SCL, ARP (drums: EDIT too) | STEP | that page's cards as the step's p-locks; again: its next page [PLOCK] |
| SEQ + keys | a synth track's STEP | the notes played become the next steps |
| SEQ held alone, let go | STEP | the keys play with STEP up; again: they are steps |
| OCT- and OCT+ tapped together (let go within 400 ms) | a synth track's STEP | FOLD on / off: only the rows of the pattern's notes, or the scale's notes when a scale is set |
| TOOLS: a knob, then SAVE | STEP's TOOLS page | REVERSE, RANDOM, COOK, LENGTH x2 / half (asked) [SEQTOOLS] |

## Clearing, undo and redo

What each clear removes, from the smallest to the largest. Notes, their levels and their recorded lengths are only removed where the table says so.

| Gesture | Where | Clears |
|---|---|---|
| step held + SHIFT + a knob | STEP, a p-lock card | that step's lock (and motion) of that parameter |
| SHIFT + a step key | a synth track's STEP | that step's p-locks; the note stays |
| SHIFT + SHIFT2 + a knob | any page (not the mixer's volume cards, no step held) | that parameter's motion and p-locks on every step of the track (a lane's value: that lane's) [AUTO] |
| SHIFT + SEQ, YES | a drum lane picked | that lane's motion and p-locks |
| SHIFT + SEQ, YES | a track | the track's motion, p-locks, nudges, fills and chance |
| HOME tapped | a step held | the held steps (notes and their events) |
| SEQ + a step key | the drum track's STEP | that step, every lane |
| EDIT + keys | playing | erase that note or lane as it plays (stopped: from every step) |
| REC held (a ring fills: 0.7 s, then 1.3 s) | a drum lane picked | that lane: hits, locks, motion |
| REC held (a ring fills) | a track | the whole track. Let go before the ring closes: nothing |
| SHIFT + REC, YES | a SONG scene row | that scene |
| SHIFT + a white key, YES | LFO held, or SONG's PATTERNS row | that pattern slot [PATTERNS] |
| SAVE then HOME | anywhere | **undo** (several levels on every track) |
| SHIFT then SAVE | anywhere | **redo** |
| EDIT + OCT- / OCT+ | anywhere | undo / redo |
| SHIFT + SAVE | SYSTEM > INFO | the DROPS, UNDERRUNS and MAX counters (not redo there) |

## Saving, patterns, scenes and the song

| Gesture | Where | Does |
|---|---|---|
| SAVE + ENV, LFO, EDIT, FX, SCL or ARP | a synth track | the sound as a user preset (NAME first) |
| SAVE + one of them | the drum track | its lanes as a user kit |
| SAVE + SEQ | a track | the working pattern into its slot (asked over a used one) [PATTERNS] |
| SAVE held + a white key | anywhere | start scene A-P at the next bar (stopped: load it, asked over changed work); two or more in one hold while playing: a chain [QCHAIN] |
| SAVE + REC + a white key | anywhere | the loop into that scene (asked over a used one) |
| SAVE + SHIFT + a white key | anywhere | clear that scene (asked) |
| LFO held + a white key | anywhere | launch the track's pattern n at the end of its pass; OCT+ + the key: now [PATTERNS] |
| LFO held + SAVE + a white key | anywhere | store the working pattern into slot n [PATTERNS] |
| LFO held + black keys 1-4 / 5 / 9 / 10 then a white key | anywhere | pick the track / stop its pattern / duplicate into a free slot / launch scene n [PATTERNS] |

## The layers (a page button held)

Hold the button: its map shows and the keys and knobs are the layer's. **SHIFT + the button** (or the button, then SHIFT) locks the layer open; any button but PLAY, REC and the OCT buttons lets it go. SAVE is never locked; FX, EDIT and ENV on FM6 lock only with SHIFT pressed first. Inside any layer PRESETS is still the sound and ALGORITHM the track (MASTER: the master's FILT, DUST, DUCK, COMP cards).

| Layer | Keys | Knobs | OCT buttons |
|---|---|---|---|
| **FX** (punch) | the 16 white keys: 16 punch-in effects (latched with PUNCH_LATCH) | FILTER, DUST, DUCK, the track's FILT | OCT-: a latched effect off |
| **EDIT** (erase) | erase that note / lane as it plays; drum SOUND page: picks that sound | SHIFT (rotate), LENGTH (x2 / half), TRANSPOSE | undo / redo |
| **ARP** (roll) | note repeat at the tempo | RATE | drums: ghost / hard rolls |
| **SCL** (key) | the song's key (root) | CHORD, SCALE, QUANT, TRANSPOSE | |
| **GLO** (mix) | 1-4 mute T1-DR, 5-8 solo T1-DR, 16 tap tempo; with FILLS 9 fill while held, 10 next bar a fill, black 1-4 FX bypass T1-DR (without FILLS: 9-12) | LEVEL of T1, T2, T3, DR | |
| **LFO** (patterns) [PATTERNS] | above, Saving | the page under | OCT+ + a key: launch now |
| **SAVE** (scenes) | above, Saving | | |
| **ENV** on an FM6 track (operators) | black keys pick OP1-OP6, PIT, GLO, MONO, POLY; white keys play | the page's four values | previous / next page |

## Mixing: mutes and solo

| Gesture | Where | Does |
|---|---|---|
| SHIFT2 + the volume knob | the mixer (a track's VOLUME, a lane's LEVEL, ACCENT, USB IN) | mute on / off |
| SHIFT2 + a RET card's knob | the mixer's MASTER row | that return muted on / off |
| SHIFT + SHIFT2 + the volume knob | the mixer (not ACCENT, the returns, MASTER) | solo on / off |
| SHIFT + a key | the mixer | the sequencer mute: the lanes' black keys, the first four white keys T1, T2, T3, DR, the last upper key ACCENT |
| GLO + keys 1-4 / 5-8 | anywhere | mute / solo T1-DR |

## Page by page

What only one page has; everything above works there too.

**PROJECT.** KNOB 1 BANK, KNOB 2 LOAD / SAVE / DELETE, KNOB 3 ITEM, KNOB 4 NEW (PROJECT) or INIT (USER). The 16 white keys pick slot n and run the operation on it (asked). SAVE runs it on ITEM. OCT- / OCT+ tapped: the previous / next bank. SHIFT2 + KNOB 3 on USER: that preset a favorite [FAVORITES].

**SONG** (SAVE + SELECT). A SCENE row: the knobs set each track's pattern (stopped), SAVE launches the scene, REC stores the loop into it, SHIFT + REC clears it. The PATTERNS row: KNOB k is track k's next pattern, the keys launch, SAVE + a key stores, SHIFT + a key clears, REC duplicates. MODE: KNOB 1 LOOP / SONG, SONG REC, SAVE writes the chain. A PART row: SCENE, BARS, INSERT, DELETE (SAVE, asked).

**NAME** (before a save). The white keys type like a phone (AB, CD ... the same key within 0.8 s cycles); black keys: F# left, G# space, A# right, C# delete, D# ABC / 123. KNOB 1 the cursor, KNOB 2 the character; SHIFT + a knob clears the name. SAVE done, HOME deletes a character (at the start: cancel), PLAY stops.

**GLOBAL and SYSTEM.** KNOB k edits line k; SAVE is YES on the cursor line; PRESETS does nothing. SYSTEM > INFO: KNOB 2 shows the CPU NOW, MAX 5 S or MAX; SHIFT + SAVE clears the counters. The Bluetooth DEVICES list: SELECT moves, SAVE connects (asked), HOME back, SHIFT + a button forgets the last device (asked).

**SCOPE** (HOME on the mixer). ALGORITHM the source; the knobs LEVEL, PAN, FX, DRIVE of a track, or FILT, THRS, RATIO of MASTER; SAVE the track's FX on / dry.

**The mixer.** See Mixing. The drum track's black keys play and pick their lane (not right after a knob turn); its white keys are steps.

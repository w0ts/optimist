# Effects: insert slots, sends, returns and the master

Everything that changes the sound after the engine: the insert slots, the send buses and their returns, the master, and the punch-in effects. The numbers are the ones in the firmware (ranges, defaults, lists). CPU and memory costs are in [BUILDER.md](../BUILDER.md). What a build contains depends on its builder items: a type that is not built is not in the lists.

## The signal path

1. Each synth track: engine, then the SLICER, then its two **insert slots** S1 and S2 (in that order), then level and pan, then the sends.
2. The drum track: each drum lane runs its own two insert slots; the drum bus then runs its own two insert slots on the sum of the lanes.
3. The sends of every track go to the three send buses (reverb, delay, chorus), which come back through the master's **returns**.
4. The master: the returns, the PUNCH effects, the master's two insert slots S1 and S2, the master volume and the limiter.

## Insert slots

An **insert slot** holds one effect type and runs in series on its track. Two on each synth track (T1, T2, T3), two on the drum bus, two on **every drum lane**, two on the master and two on the USB input track. A type is in at most one slot of a track: picking the type the other slot holds swaps the two slots (and their amounts). A type that is in no slot is not heard, and its amount is kept.

**Where:** the INSERTS page of the track (the track's FX family, and the mixer's inserts set) has S1 TYPE, its amount, S2 TYPE, its amount. Turn the knob of the TYPE card to pick the type; the amount is the next card. A slot set to `----` is empty.

**Defaults:** a new track has S1 DIST and S2 FILT, both at amount 0 (bypassed). A drum lane has S1 DIST and S2 DYN. The master has S1 FILT (0) and S2 DYN.

### The types

The amount is one value per type, kept for the track whatever the slot, so presets, motion, macros and locks keep their meaning when you move a type from S1 to S2. 0 is off for every type.

| Type | What it does | The amount | Own page |
|---|---|---|---|
| DIST | drive | 0..127 | none |
| DYN | a compressor and a look-ahead limiter in one (below) | THRS, 0..127 (0 off, up to -30 dB) | `DYN`, `DYN 2` |
| FILT | the DJ filter | -64..63: below 0 a low-pass closing, above 0 a high-pass opening, 0 off | none |
| DUST | a lo-fi, vinyl treatment of the track | 0..127 | none |
| FOLD | wavefolder | the drive into the folder | none |
| FUZZ | fuzz | the drive into the fuzz | none |
| PHSR | phaser | up to 64 its mix, then its feedback | none |
| FLNG | flanger | up to 64 its mix, then its feedback | none |
| FRMT | a vowel filter | 1..127 sweeps the vowels A E I O U | none |
| WIDE | stereo width | the share of the swept side | none |
| GATER | a tempo-synced gate (below) | DEPTH, 127 a closed step silent | `GATER` |
| CHRS | a two-tap chorus around 7 ms | up to 64 its mix, then its depth | none |
| ECHO | a short single repeat, 75 ms | up to 64 its level, then its feedback | none |
| JUNO | the Juno-106 chorus (below) | MIX, 127 the Juno's chorus on | `JUNO CHO` |
| EQ3 | a three-band DJ equaliser (below) | ON or OFF | `EQ3`, `EQ3 FREQ` |
| SFLD | a sine wavefolder: FOLD with round folds, softer and with fewer aliases | the drive into the folder | none |
| RECT | a rectifier: the octave up (an Octavia) | up to 64 half-wave to full-wave, then its drive | none |
| HARM | a harmonic exciter: the octave and the twelfth over each note | the drive | none |
| RING | a ring modulator | the sine's pitch, 26 Hz .. 2.6 kHz | none |
| SUB | an octave down, for one-note lines (a chord gives a rough sub) | the sub's level | none |
| SLEW | a slew limiter: edges rounded into ramps, a dull crunch | light .. heavy | none |
| TAPE | a worn tape's tone: the highs rolled off (6.5 kHz down to 1.7 kHz along the amount), the mids wandering at random every couple of seconds | how far it wanders, how dark | none |
| SPIT | a resonant band jumping at random (180 Hz .. 5.9 kHz, about 2.6 times a second), driven into a soft clip at the sound's own level | the band's sharpness and the drive | none |

A drum lane can hold DIST, DYN (its compressor, with the drum track's RATIO ATK REL and no CEIL), FILT, DUST, FOLD, FUZZ, PHSR, FRMT, EQ3 and the shapers SFLD, RECT, HARM, RING and SLEW. Its amount belongs to the slot, not to the type: changing the type resets the amount to 0.

Each type is a builder item (names in [BUILDER.md](../BUILDER.md)); TAPE and SPIT are off by default (the Synth build has them). A project that names a type this build lacks keeps it, unheard.

### DYN: compressor and limiter

DYN is one effect with two stages. On a track:

- **THRS** is the slot's amount: the threshold, 0 off.
- **RATIO** 1.5:1, 2:1, 3:1, 4:1, 6:1, 8:1, 20:1, INF (default 2:1).
- **ATK** 0.1 ms, 0.3 ms, 1 ms, 3 ms, 10 ms, 30 ms (default 10 ms).
- **REL** 50 ms, 100 ms, 200 ms, 300 ms, 600 ms, 1.2 s, AUTO (default AUTO).
- **GAIN**, the make-up after the compressor: AUTO (half the static reduction at full scale) or 0 to 15 dB (default AUTO).
- **CEIL**, the look-ahead **limiter** after it: OFF, -0.1, -0.3, -0.5, -1.0, -2.0, -3.0, -6.0 dB. The track default is OFF.

The pages: `DYN` has THRS, RATIO, GAIN, CEIL; `DYN 2` has ATK and REL. They appear while one of the track's slots holds DYN. Each track has its own settings. On the drum track the DYN works on the drum bus, and the drum lanes' own DYN takes the drum track's settings.

On the master DYN is the master compressor and the brickwall limiter: THRS (-30..0 dB, 0 off), RATIO, ATK, REL, then GAIN (0..15 dB, no AUTO) and CEIL (default -3.0 dB). They show as `INS DYN 1` (THRS RATIO ATK REL) and `INS DYN 2` (GAIN CEIL GR, GR being a read-out of the reduction), only while a master slot holds DYN. The mixer's MASTER row shows THRS RATIO GAIN CEIL.

### GATER: a trance gate

GATER chops the track's volume in time with the tempo. Page `GATER`: DEPTH (the slot's amount), PAT, RATE and SHAPE. PAT is one of 16 patterns: 16TH, 8TH, 4TH, HALF, OFF8, OFF16, DOT8, DOT4, TRNC1 to TRNC4, GALOP, RVGAL, CLAVE, TRESL (default TRNC1). RATE is the length of a step: 1/8, 1/16, 1/32, 8T, 16T, 32T (default 1/16). SHAPE goes from hard to smooth. The phase follows the transport clock, so the gate starts on step 0 with PLAY at any tempo, nudge or external clock. On the drum track it gates the drum bus. GATER is off by default (`INS_GATER`) and not available on the master.

### CHRS, ECHO and JUNO: types with a count of instances

These keep a line of memory each, so the builder gives them a count of instances (0 off, 1 to 4: CHRS and ECHO per track, JUNO per slot). A track that picks one when every instance is in use stays dry on that slot until one is free; the INSERTS list shows how many are left.

**JUNO** is the Juno-106's chorus (from fm-106, the MN3009 model): page `JUNO CHO` with MIX (127 = fully on) and MODE (I, II, I+II). A mono track comes out in stereo. On the master it is the page `MST JUNO`, shown as `INS JUNO`.

### EQ3: a three-band DJ equaliser

EQ3 splits the sound at two crossover frequencies and sets the gain of each band. It can sit in any insert slot: a track's, the drum bus', a drum lane's, the master's, the USB input's, or a return's.

- **LO, MID, HI** (page `EQ3`): KILL (the band is cut off completely) or -30 to +6 dB in 1 dB steps; 0 dB is the default.
- **FREQ LO, FREQ HI** (page `EQ3 FREQ`): FREQ LO from 51 Hz to 1 kHz (default 250 Hz), FREQ HI from 625 Hz to 12 kHz (default 2.5 kHz). The bands add up flat at any setting.
- **The amount** is ON or OFF (ON by default). With all three gains at 0 dB nothing runs and the sound passes unchanged.
- On the master the pages are `MST EQ3` and `MST EQ3 F`; on a return, `RET EQ3` and `RET EQ3 F`.

## The master

### Master insert slots

The master has two insert slots, S1 and S2, after the returns and PUNCH and before the master volume. Their page is `MST INS`, shown as `INSERTS` on the master FX screen (tap FX on the mixer's MASTER row). They take every type the build has except GATER; CHRS, ECHO, FLNG and JUNO take an instance from the pool, as on a track. Their amounts: FILT is the master's DJ filter (-64..63), DUST the master DUST, DYN the master compressor's THRS.

### The three returns: A, B, C

The send buses (reverb, delay, chorus) come back into the mix through three **returns**, A, B and C. The page `SLOTS` says which send each return carries (REV, DLY, CHO or empty). Picking a send that another return holds swaps the two. A send in no return is not heard. A return can also hold an insert type that is not a send (any type the build has except GATER): it then works on that return's line, and the page `RET AMT` holds its amount.

The return levels are on `RETURNS`, cards in the order REV, DLY, CHO: 0..127, 127 unity. The effects the returns carry have their own pages: the delay `DLY` / `RET DELAY` (TIME, FDBK 0..120, FREQ, MIX), its filter `DLY F` / `RET DLY F`, the reverb `REV/CHO` / `RET REV` (SIZE, DAMP, then the chorus's CRT and CDP), and `REVERB` / `RET TYPE`, the reverb algorithm.

**Reverb algorithms** (builder: one box each, at least one): ROOM, PLATE, FDN8 (long and lush), SPRING and VTINY (Airwindows VerbTiny). With two or more built, `REVERB` > TYPE picks one.

### The delay's filter

The delay's echoes go through a filter on page `DLY F` (`RET DLY F` on the master FX screen): **TYPE** LP, HP, BP or PEAK, **SLOPE** 12 or 24 dB an octave, **FREQ** 30 Hz to 16 kHz (the same knob as on `DLY`, default about 6 kHz) and **RES**. It sits in the loop: the first echo is filtered once and every repeat once more, so a low-pass darkens the echoes as they fade (tape and dub delays) and a high-pass thins them. RES lifts the cutoff (PEAK: up to +12 dB a stage, 24 dB with two); the feedback is scaled by that lift, so RES colours the echoes without making the delay run away, and FDBK 100 % still holds the echoes for ever. The defaults are LP, 12 dB, RES 0.

### Sends between the returns

A return can send to the returns to its right: **A to B and C, B to C**. A's delay can feed B's reverb, say. A send takes what its return gives back to the mix (after its return level, mono) and adds it to the input of the return it goes to, 0..127 like a track's send (127 is unity), 0 by default. The returns are worked out in the order A, B, C, so nothing can come back from the right: no feedback, no added latency.

**Feedback sends** (builder item `RET_FB`, on by default): a return also sends to the returns on its **left**, so every return sends to the other two and the effects can feed each other in a loop (a delay into a reverb and back into the delay). Such a send takes its return's output of the block before: the loop goes round once every 32 samples (0.73 ms). For safety it runs at a quarter of a forward send's gain (127 is 0.25), any DC is filtered out of it, and it is held under -12 dBFS and soft-clipped, so a loop can sing but never climbs without end.

On the mixer's MASTER row, after the returns: `A SENDS` (the sends to B and C, named by what B and C hold) and `B SENDS` (the send to C); with the feedback sends `B SENDS` has A's card too and `C SENDS` follows (to A and B). A return that holds nothing has no set. A return's sends are not lockable to a step and cannot be recorded as motion.

### DUCK, PUNCH, DUST and the DJ filter

- **DUCK** (0..127) dips the synth tracks on every kick of the drum track and lets them swell back over an eighth note. It is on the master FX screen as `DUCK`.
- **PUNCH**: hold FX and a white key to put the whole mix through one of 16 punch-in effects (latched with `PUNCH_LATCH`; FX + OCT- turns a latched one off). While FX is held the knobs are FILTER, DUST, DUCK and the track's FILT.
- **DUST** and the **DJ filter** are insert types (`FX_DUST`, `FX_DJF`): put them in a master slot to get the master DUST and FILT.

### The sends row

A synth track's sends are on `SENDS`: REV, DLY, CHO (blank where no return carries one) and, as the fourth card, PAN. The drum lanes have the same set, their own levels (0..31 shown as percent). On DR the `SENDS` page ("Kit sends") holds the kit's send levels, which scale every lane's own sends; the drum bus has no send of its own.

## USB audio in, as a track

**USB audio in** is the computer's audio as a mixer track. It exists only in a build with USB audio (`FELUCCA_USB_AUDIO`). It is a stereo track after the drum lanes, named USB IN in the mixer. It has VOLUME (default unity), PAN, FX ON, two insert slots (every insert type a track's slots take; the pooled ones take an instance from the pool, and when none is free the slot reads FULL and is bypassed) and sends REV, DLY, CHO (all 0 by default). It is mixed into the mix bus before the master's inserts. The mute is SHIFT + the VOLUME knob, as on every row. The keys are silent on this row. FX tapped on the row shows its insert slots.

## Effects in projects

- The slots, their types, the amounts and the three returns are saved with the project. A project saved before a feature loads with the defaults of the feature.
- Which values can be recorded as motion or locked to a step is in [the sequencer guide](sequencer.md). The insert types (S1, S2 TYPE) and a drum lane's SENDS are not lockable.

## What is not there

- CHRS, ECHO and GATER cannot go in a master slot.
- FLNG, WIDE, CHRS, ECHO, GATER, JUNO, SUB, TAPE and SPIT cannot go in a drum lane's slots; they run on the track or the drum bus.
- There are three returns and no fourth send bus.
- No effect has a tap tempo or its own clock: GATER follows the transport, the delay time is a note value.

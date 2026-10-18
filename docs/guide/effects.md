# Effects: insert slots, sends, returns and the master

This chapter is about everything that changes the sound after the engine: the insert slots, the send buses and their returns, the master, and the punch-in effects. It is written for a user. The numbers are the ones in the firmware (ranges, defaults, lists); CPU and memory costs are in [BUILDER.md](../BUILDER.md). What a build contains depends on its builder items: a type that is not built is not in the lists. The manual [OPTIMIST.md](../../OPTIMIST.md) has the punch-in effects and the master DUST, DUCK and FILT in its own sections.

Page names below are the firmware's. The Optimist UI shows most of them in its FX family of the track and on the master FX screen, with the short forms in its names table (for example `INSERTS` is shown as "Insert"). SLOOP's UI shows the same pages in its FX layer, and the master's pages in GLO. Where the two differ, the text says so.

## The signal path

1. Each synth track: engine, then the SLICER, then its two **insert slots** S1 and S2 (in that order), then level and pan, then the sends.
2. The drum track: each drum sound runs its own two insert slots; the drum bus then runs its own two insert slots, S1 and S2, on the sum of the sounds.
3. The sends of every track go to the three send buses (reverb, delay, chorus), which come back through the master's **returns**.
4. The master: the returns, the PUNCH effects, the master's two insert slots S1 and S2, the master volume and the limiter.

## Insert slots

An **insert slot** holds one effect type and runs in series on its track. There are:

- two on each synth track (T1, T2, T3), S1 then S2;
- two on the drum bus (the drum track's INSERTS page);
- two on **every drum sound** (the sound's own slots; on the mixer, the drum lane's insert cards);
- two on the master (below);
- two on the USB input track (below).

A type is in at most one slot of a track: picking the type the other slot holds swaps the two slots (and their amounts). A type that is in no slot is not heard, and its amount is kept.

**Where:** the `INSERTS` page of the track (SLOOP UI: FX layer; Optimist UI: the track's FX family, and the mixer's insert cards) has S1 TYPE, its amount, S2 TYPE, its amount. Pick the type with the knob on the TYPE card; the amount is the next card. A slot set to `----` is empty.

**Defaults:** a new track has S1 DIST and S2 FILT, both at amount 0 (bypassed; a bypassed slot costs a compare a block). A drum sound has S1 DIST and S2 DYN. Old projects keep the layout they were saved with and convert on load (below).

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
| WRAP | a wavewrapper: past full scale the wave jumps to the other side, a harsh buzz | the drive into the wrap | none |
| RECT | a rectifier: the octave up (an Octavia) | up to 64 half-wave to full-wave, then its drive | none |
| HARM | a harmonic exciter: the octave and the twelfth over each note | the drive | none |
| RING | a ring modulator | the sine's pitch, 26 Hz .. 2.6 kHz | none |
| SUB | an octave down, for one-note lines (a chord gives a rough sub) | the sub's level | none |
| SLEW | a slew limiter: edges rounded into ramps, a dull crunch | light .. heavy | none |

A drum sound can hold DIST, DYN (its compressor, with the drum track's RATIO ATK REL and no CEIL), FILT, DUST, FOLD, FUZZ, PHSR, FRMT, EQ3 and the shapers SFLD, WRAP, RECT, HARM, RING and SLEW (not SUB). Its amount belongs to the slot, not to the type: changing the type resets the amount to 0.

Each type is a builder item (DIST `FX_DIST`; DYN `MASTER_COMP`; FILT, DJ filter `FX_DJF`; DUST `FX_DUST`; `INS_FOLD`, `INS_FUZZ`, `INS_PHASER`, `INS_FLANGER`, `INS_FORMANT`, `INS_WIDTH`, `INS_GATER`, `INS_CHORUS`, `INS_DELAY`, `INS_JCHORUS`; the shapers `INS_SFOLD`, `INS_WRAP`, `INS_RECT`, `INS_HARM`, `INS_RING`, `INS_SUBOCT`, `INS_SLEW`). Only the types the build has appear in the lists. A project that names a type this build lacks keeps it, unheard.

### DYN: compressor and limiter

DYN is one effect with two stages. On a track:

- **THRS** is the slot's amount: the threshold, 0 off.
- **RATIO** 1.5:1, 2:1, 3:1, 4:1, 6:1, 8:1, 20:1, INF (default 2:1).
- **ATK** 0.1 ms, 0.3 ms, 1 ms, 3 ms, 10 ms, 30 ms (default 10 ms).
- **REL** 50 ms, 100 ms, 200 ms, 300 ms, 600 ms, 1.2 s, AUTO (default AUTO).
- **GAIN**, the make-up after the compressor: AUTO (half the static reduction at full scale) or 0 to 15 dB (default AUTO).
- **CEIL**, the look-ahead **limiter** after it: OFF, -0.1, -0.3, -0.5, -1.0, -2.0, -3.0, -6.0 dB. The track default is OFF. The track's limiter looks ahead 32 samples (0.73 ms).

The pages: `DYN` has THRS, RATIO, GAIN, CEIL; `DYN 2` has ATK and REL. They appear while one of the track's slots holds DYN. Each track has its own RATIO, ATK, REL, GAIN and CEIL. On the drum track the DYN works on the drum bus (the compressor on its five channels, the limiter on the left and right), and the drum sounds' own DYN takes the drum track's settings.

On the master DYN is the master compressor and the brickwall limiter: THRS (-30..0 dB, 0 off), RATIO, ATK, REL, then GAIN (0..15 dB, no AUTO) and CEIL (default -3.0 dB). The Optimist UI shows them as `INS DYN 1` (THRS RATIO ATK REL) and `INS DYN 2` (GAIN CEIL GR, GR being a read-out of the reduction), only while a master slot holds DYN; the mixer's MASTER row shows INS DYN as THRS RATIO GAIN CEIL. SLOOP's UI has the pages `COMP` and `LIMIT`.

### GATER: a trance gate

GATER chops the track's volume in time with the tempo. Pages: `GATER` with DEPTH (the slot's amount), PAT, RATE and SHAPE. It shows while one of the track's slots holds GATER (on the drum track it gates the drum bus). PAT is one of 16 patterns: 16TH, 8TH, 4TH, HALF, OFF8, OFF16, DOT8, DOT4, TRNC1 to TRNC4, GALOP, RVGAL, CLAVE, TRESL (default TRNC1). RATE is the length of a step: 1/8, 1/16, 1/32, 8T, 16T, 32T (default 1/16). SHAPE goes from hard to smooth (default hard). The phase follows the transport clock, so the gate starts on step 0 with PLAY at any tempo, nudge or external clock; stopped, it runs on at the tempo. GATER is built with `INS_GATER` (off by default, any number of tracks). It is not available on the master.

### CHRS, ECHO and JUNO: types with a count of instances

These three keep a line of memory each, so the builder gives them a count of instances (0 off, 1 to 4: CHRS and ECHO per track, JUNO per slot). A track that picks one when every instance is in use stays dry on that slot until one is free; the INSERTS list shows how many are left. A saved project keeps the type, not the instance: on load the tracks get the instances in order T1 T2 T3 DR (the master's JUNO instance is counted in the CPU guard).

**JUNO** is the Juno-106's chorus (from fm-106, the MN3009 model): the `JUNO CHO` page has MIX (127 = the Juno's chorus fully on) and MODE (I, II, I+II). A mono track comes out in stereo. The page shows while one of the track's slots holds JUNO. On the master it is `MST JUNO` (SLOOP UI), `INS JUNO` (Optimist UI), with the same two cards.

### EQ3: a three-band DJ equaliser

EQ3 (builder item `INS_EQ3`) splits the sound at two crossover frequencies and sets the gain of each band, like the three-band DJ equaliser of a mixer. It can sit in any insert slot: a track's, the drum bus', a drum sound's, the master's, the USB input's, or a return's.

- **LO, MID, HI** (page `EQ3`): the gain of each band, KILL (the band is cut off completely) or -30 to +6 dB in 1 dB steps; 0 dB is the default.
- **FREQ LO, FREQ HI** (page `EQ3 FREQ`): the two crossovers, FREQ LO from 51 Hz to 1 kHz (default 250 Hz) and FREQ HI from 625 Hz to 12 kHz (default 2.5 kHz). The bands add up flat at any setting of the frequencies.
- **The amount** is ON or OFF (ON by default). With all three gains at 0 dB, or with the type out of the slot, nothing runs and the sound passes unchanged; entering or leaving that state is a 3 ms crossfade, so there is no click.
- On the master the pages are `MST EQ3` and `MST EQ3 F`; on a return, `RET EQ3` and `RET EQ3 F`. The CPU guard counts an EQ3 only while it is doing something.

## The master

### Master insert slots

The master has two insert slots, S1 and S2, after the returns and PUNCH and before the master volume. The Optimist UI shows them as `INSERTS` on the master FX screen (and the mixer's MASTER row, INSERT); SLOOP's UI has the page `MST INS` (S1, S2 and their amounts).

- They take every type the build has except GATER (it needs a track's tempo values). CHRS, ECHO, FLNG and JUNO take an instance from the pool, as on a track.
- Their amounts: FILT is the master's DJ filter (-64..63), DUST the master DUST, DYN the master compressor's THRS; any other type has its own master amount.
- A new project: S1 FILT (amount 0, bypassed), S2 DYN.
- An old project (saved before the master had slots) converts: the master effects it used take the slots, and when all three of DUST, FILT and DYN were in use, DYN and DUST win and FILT is dropped. PUNCH now runs before the master inserts (it ran between DUST and the DJ filter), which only differs when DUST and a punch effect are used together.

### The three returns: A, B, C

The send buses (reverb, delay, chorus) come back into the mix through three **returns**, A, B and C. The page `SLOTS` says which send each return carries (REV, DLY, CHO or empty; slot 0 has no return). Picking a send that another return holds swaps the two. A send in no return is not heard, and its line can be lent to the CHRS and ECHO insert copies (they use the chorus' and the delay's lines). A return can also hold an insert type that is not a send (any type the build has except GATER): it then works on that return's line, and the page `RET AMT` holds its amount.

The return levels are on `RETURNS` (Optimist UI) with the cards in the order REV, DLY, CHO: 0..127, 127 unity (the values RRET, DRET, CRET). The effects the returns carry have their own pages: the delay `DLY` / `RET DELAY` (TIME, FDBK 0..120, COLR, MIX; the defaults are FDBK 60, COLR 70, MIX 90), the reverb `REV/CHO` / `RET REV` (SIZE, DAMP, then the chorus's CRT and CDP), and `REVERB` / `RET TYPE`, the reverb algorithm.

### Sends between the returns

A return can send to the returns to its right: **A to B and C, B to C** (C sends to none). A's delay can feed B's reverb, say, or A's reverb C's chorus. A send takes what its return gives back to the mix (after its return level, mono) and adds it to the input of the return it goes to, 0..127 like a track's send (127 is unity), 0 by default. The returns are always worked out in the order A, B, C, so a return hears this block's output of the ones to its left and nothing can come back from the right: no feedback, no added latency (the reverb on the second core is one block late as it is heard, and its send is that same output). The sends belong to the positions, not to the effects: swapping two returns' types keeps A's send to B as it was. With every send at 0 nothing of it runs.

**Feedback sends** (builder item `RET_FB`, on by default): a return also sends to the returns on its **left**, B to A, C to A and C to B, so every return sends to the other two and the effects can feed each other in a loop (a delay into a reverb and back into the delay). Such a send cannot be heard in the same block (A has already been worked out when B runs), so it takes its return's output of the block before: the loop goes round once every 32 samples (0.73 ms). For safety it runs at a quarter of a forward send's gain (127 is 0.25), any DC is filtered out of it, and it is held under -12 dBFS (a limiter) and soft-clipped, so a loop can sing but never climbs without end: with every send at 127 and a resonant delay a loop settles at that level, and it dies away when the sends come down. A reverb near its freeze (SIZE 127) or a delay at FDBK 120 already sustain on their own.

In the Optimist UI they are on the mixer's MASTER row, after the returns: `A SENDS` (cards 2 and 3: the sends to B and C, named by what B and C hold; card 1, A itself, and card 4 are blank) and `B SENDS` (card 3: the send to C); with the feedback sends `B SENDS` has A's card too (card 1) and `C SENDS` comes after it (cards 1 and 2: to A and B). The cards follow the track sends' layout (card k is return k). A return that holds nothing has no set and no card. The sends are saved with the project (its FX record, TLV 0x4B: three bytes, six with the feedback sends, written only when one is not 0; a build without them skips the last three); a project from before loads with every send at 0. The web editor reaches them as each return's second slot (editor protocol, command 94).

**Reverb algorithms** (builder: one box each, at least one): ROOM, PLATE, FDN8 (long and lush), SPRING and VTINY (Airwindows VerbTiny). With two or more built, `REVERB` > TYPE picks one. The manual's "Reverb algorithms" section describes each.

### DUCK, PUNCH, DUST and the DJ filter

- **DUCK** (0..127) dips the synth tracks on every kick of the drum track and lets them swell back over an eighth note. The Optimist UI shows it on the master FX screen as `DUCK`.
- **PUNCH**: hold FX and a white key to put the whole mix through one of 16 effects; see the manual's "Punch-in effects".
- **DUST** and the **DJ filter** are insert types now (FX_DUST, FX_DJF): put them in a master slot to get the old MASTER DUST and FILT.

### The sends row

In the Optimist UI the track's sends are on `SENDS`: REV, DLY, CHO (the slots a return holds; blank where no return carries one) and, as the fourth card, PAN. The same set is on the drum track and on each drum lane (the lane's own levels, 0..31 shown as percent). On the drum track `BUS SENDS` holds the drum bus's own sends; the drum sounds' sends are on `DR SENDS`. SLOOP's UI has the shorter `FX` page (the DIST amount and the three sends).

## USB audio in, as a track

**USB audio in** is the computer's audio as a mixer track. It exists only in a build with USB audio (`FELUCCA_USB_AUDIO`). The track is a stereo track after the drum tracks, named USB IN in the mixer. It has VOLUME (default unity), PAN, FX ON, two insert slots (every insert type a track's slots take, EQ3, GATER and the JUNO chorus included; the pooled ones, CHRS, ECHO, FLNG and JUNO, take an instance from the pool, and when none is free the slot reads FULL and is bypassed) and sends REV, DLY, CHO taken from its mid (all 0 by default). It is mixed into the mix bus before the master's inserts. The mute is SHIFT2 on VOLUME, as on every row. Its settings (level, pan, mute, FX ON, the three sends, the two slots and their amounts) are kept in the project's FX record (TLV 0x48, 15 bytes: it was 11 before GATER's and the JUNO chorus's own values were added; written only when something differs from the defaults). In the Optimist UI its row is the mixer's USB row, after the drum tracks; it has no instrument, so the keys are silent on it. By default: unity, centre, no insert, no send, which is how the computer's audio was mixed before the track existed, except that it now goes into the mix bus before the master's inserts.

## Effects in projects

- The slots, their types, the amounts and the three returns are saved with the project (its FX record). A project saved before a feature has no record for it and loads with the defaults of the feature (for the master, the conversion above).
- The CPU guard counts the inserts that are in a slot (including the master's JUNO instance and the drum sounds' slots); a type that is built but sits in no slot costs nothing a block.
- Which values can be recorded as motion or locked to a step is in the sequencer guide: [sequencer.md](sequencer.md). The insert types (S1, S2 TYPE) and a drum lane's SENDS are not lockable.

## What is not there

- CHRS, ECHO and GATER cannot go in a master slot.
- JUNO cannot go in the USB audio in track's slots.
- FLNG, WIDE, CHRS, ECHO, GATER, JUNO and SUB cannot go in a drum sound's slots; they run on the track or the drum bus.
- The master's returns are three; there is no fourth send bus.
- Without the builder item `RET_FB` a return cannot send to a return on its left (B to A, C to A or B): the returns render in one order, A, B, C. With it, those sends are a block late (32 samples) by design.
- A return's sends are not lockable to a step and cannot be recorded as motion.
- No effect has a tap tempo or its own clock: GATER follows the transport, the delay time is a note value.

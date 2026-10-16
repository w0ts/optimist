# Sound: engines, voices, modulation, presets

This chapter is about what makes a synth track sound the way it does, and where each control is. The manual,
[OPTIMIST.md](../../OPTIMIST.md), is the reference for every page of every engine; the builder reference,
[docs/BUILDER.md](../BUILDER.md), lists what each engine costs and how to leave it out. Names in capitals are the labels
on the screen. "The Optimist UI" is the interface chosen with the builder item `UI=1`; SLOOP's UI is the default.

## Engines

A synth track plays one engine at a time. PRESETS on a synth track loads a sound and its engine follows; on the Optimist
UI, SHIFT + PRESETS steps the engine. Every engine is a builder item (`ENG_*`). A build without an engine plays a track
that uses it on a stand-in engine and shows MISSING: DIGITAL stands in for FM6, PHASE for CZ, SAMPLE for GRAIN and SLICE,
ANALOG for the others (docs/BUILDER.md names each stand-in; JUNO and NOISE fall back to ANALOG, CZ to PHASE).

The engine's own values are on the EDIT pages: **EDIT 1** and **EDIT 2**, four cards each, and the page titles the
engine gives them (ANALOG shows OSC and FLT, for example).

| Engine | Builder item | What it is | EDIT 1 / EDIT 2 cards |
|---|---|---|---|
| ANALOG 2 | `ENG_ANALOG` | Two band-limited oscillators with hard sync, a swarm of up to 6 detuned copies, two filters | OSC: WAVE DTN MIX NOIS; FLT: CUT RES DRV KTR. Extra pages OSC 2, SWARM, FLT 2, and ENV2 (below) |
| DIGITAL | `ENG_DIGITAL` | Four-operator FM, 8 algorithms, feedback on operator 4, one modulation INDEX with its own envelope | OPS: ALG R2 R3 R4; MOD: IDX MDEC FB |
| PHASE | `ENG_PHASE` | Phase distortion: a cosine with a bent phase, a second line to mix or ring-modulate | PHS: WAVE WAVE2 DCW ENV; LINE: DTN LINE SUB |
| LOFI | `ENG_LOFI` | Chip voice: pulse, triangle, saw, noise, a 4-bit wavetable, bit crush, a pitch sweep | CHIP: CHIP WAVE DUTY CRSH; MOTN: SWP VIB ARP TONE |
| SAMPLE | `ENG_SAMPLE` | Built-in sample sets and your own USR1 to USR3 samples across the keys, with loops | SET: SET TUNE BITS LOOP; TONE: CUT DRV |
| VOICE | `ENG_FORMANT` | A formant synth that sings vowels | VOWL: VOWL VOWL2 TALK SHIFT; TONE: BUZZ BRTH Q RAND |
| TRIO | `ENG_TRIO` | Three oscillators with ring modulation and hard sync into a multimode filter | OSC: WAVE INT2 INT3 DTN; TONE: MODE CUT RES PW |
| WHEEL | `ENG_DRAWBAR` | A tonewheel organ: nine drawbars per note, 16 registrations | BARS: REG SUB BODY TOP; TONE: PERC CLICK DRV ROTR |
| GRAIN | `ENG_GRAIN` | Granular: clouds of grains cut from the sample sets or your USR slots | GRAN: SRC POS SIZE DENS; SPRY: PTCH SPRD RAND TONE |
| FM6 | `ENG_FM6` | Six-operator FM that plays DX7 voices as Dexed does; its own operator editor (ENV on an FM6 track) and DX7 SysEx | EDIT 1: VOICE MOD M.TIM C.TIM; EDIT 2: ENGINE |
| SLICE | `ENG_SLICE` | A break slicer: cuts a loop into 4, 8, 16 or 32 slices (or one per hit) and plays them from the keys or the sequencer | SLCE: SRC DIV START PTCH; PLAY: MODE REV DCAY TONE |
| PHYS | `ENG_PHYS` | Physical models: modal, string, membrane and sympathetic strings | MODEL, then STRC BRIT DAMP POS and ACC BOW EXC |
| ACID | `ENG_ACID` | A TB-303-style bass voice, monophonic; experimental | EDIT 1: CUT RESO ENV DEC; EDIT 2: ACC WAVE DRV SLD; ACID GEN: DENS ACC SLD GEN |
| CZ | `ENG_CZ` | Casio CZ-1 style tones: two lines with 8-step envelopes | EDIT 1: TONE DCW W.TIM DTN; EDIT 2: A.TIM LINE MOD VIB |
| JUNO | `ENG_JUNO` | The Juno-106 voice: DCO with saw, pulse, sub and noise, a 4-pole ladder filter, a 4-position high-pass | EDIT 1: WAVE PW SUB NOIS; EDIT 2: CUT RES KEY HPF |
| NOISE | `ENG_NOISE` | White, pink and brown noise, crackle, a shift register and metal tones through a resonant filter | EDIT 1: MODE COLR FREQ RES; EDIT 2: TRK DENS DRFT CRSH |

The card names above are read from the engines' tables in `firmware/src/engines` and from the manual's engine
sections; the manual says what each card does. On a JUNO track, ENV is the Juno's envelope and LFO the Juno's LFO (see
OPTIMIST.md, "JUNO, a Juno-106 voice"); the Juno-106 chorus is an insert type, not a part of the engine
([insert effects](../../OPTIMIST.md#insert-effects-generic-slots-builder-items-ins_-fx_)). JUNO has 8 presets of our own,
no Roland factory patches. ACID has 4 presets, NOISE 4, CZ 8, PHYS 9.

## Voices

### VOICES, the voice count

**VOICES** sets how many voices a synth track uses. It runs from 1 to the engine's own maximum and is never AUTO: it
shows the number, and a new track shows the engine's maximum. The engine's maximum is 8 unless the engine caps it: ACID
has 1, PHYS 3 and JUNO 6 (`firmware/src/engines`). On the Optimist UI, VOICES is card 3 of the **VOICE 2** page (ALLOC,
DETUNE, VOICES, DROP). VOICES counts in the POLY and UNISON modes; the CPU guard follows the number. It is stored with
the project, in the FX record, and it is not part of a sound: a preset does not change it, and it cannot be locked.

### Voice modes, glide, priority

The **VOICE** page has four cards: the mode, GLD (glide time), GLMOD and PRIO.

- **POLY** plays each note on its own voice. When the voices are all busy, the lowest held note is protected and the
  oldest released voice is taken first.
- **MONO** plays one voice, and every new note retriggers it; GLD glides to it.
- **LEG (LEGATO)** plays one voice and does not retrigger while a note is held: the new note glides there when GLD is set.
- **UNI (UNISON)** stacks VOICES voices on one note and spreads them in pitch: DETUNE sets the spread, up to about
  plus or minus 40 cents. The voices start at random phases on engines that are not sample based, so they do not cancel.
- **PRIO** (LAST, LOW, HIGH) picks which held note sounds in MONO, LEG and UNI.
- **GLD** is the glide time; **GLMOD** (RATE or TIME) says whether GLD is the time an octave takes (RATE: any interval
  at that speed) or the time of any glide (TIME). A sequencer SLIDE with GLD at 0 glides in a fixed short time.
- **ALLOC** (ROT or REUSE), on **VOICE 2**, picks the voice for a new note in POLY: ROT rotates through the free
  voices; REUSE takes the idle voice whose last pitch is nearest the new note. With GLD on, the nearest-pitch rule is used
  whatever ALLOC says.

## Modulation: two LFOs, two envelopes

### LFO 1 and LFO 2

**LFO** has the cards RATE, WAVE (SIN, TRI, SAW, SQR, S&H), PHS (start phase) and FADE (fade-in). **LFO 2** (a synth
track's second LFO; builder item `LFO2`) has RATE, WAVE, TRIG (NOTE restarts it with a new phrase, FREE) and POL (BI or
UNI). RATE below its free range is a tempo-synced value: 4BAR, 2BAR, 1BAR, 1/2, 1/4, 1/8, 8T, 1/16, 16T, 1/32.

### ENV and ENV 2

**ENV** (ENV 1) is the engine's own envelope, the track's ADSR: ATK, DEC, SUS, REL. It always shapes the amplitude
(there is no AMP knob for it). **ENV 2** (ATK2, DEC2, SUS2, REL2) is a second ADSR of its own, restarted by each note;
REL2 at 0 takes DEC2's time. ENV 2 runs on every engine, and only while one of its slots acts. It exists in builds that
have the ANALOG engine.

### ENV 1 MOD: four depth knobs made for each engine

**ENV 1 MOD** (on the Optimist UI: *ENV mod*) has no destination pickers: its four knobs are fixed for the engine, each
a depth from -64 to 63 (0: nothing), named after what it moves. FLT, SHP and PIT move the voice's filter, shape and
pitch (PIT: up to about 31.5 semitones at full); the others move one of the engine's EDIT values for each voice as ENV
runs. An engine whose EDIT values already shape its own envelope (ACID, CZ, FM6, PHASE's DCW) shows those values here
too: turning them is turning the EDIT value.

| Engine | Card 1 | Card 2 | Card 3 | Card 4 |
|---|---|---|---|---|
| ANALOG (SUPER) | FLT | PIT | SHP (the pulse width) | RES |
| DIGITAL | IDX (the FM index) | PIT | FB | MDEC |
| PHASE | ENV (its EDIT ENV: the DCW envelope) | PIT | DTN | SUB |
| LOFI | TONE | PIT | DUTY | SWP |
| SAMPLE | CUT | PIT | BITS | DRV |
| VOICE | VOWL | PIT | BUZZ | Q |
| TRIO | FLT | PIT | PW | RES |
| WHEEL | PIT | DRV | CLICK | TOP |
| GRAIN | TONE | PIT | POS | DENS |
| FM6 | M.TIM (its EDIT value) | C.TIM (its EDIT value) | MOD | PIT |
| SLICE | TONE | PIT | DCAY | PTCH |
| PHYS | BRIT | PIT | DAMP | EXC |
| ACID | ENV | DEC | ACC | SLD (the 303's own EDIT values) |
| CZ | DCW | W.TIM | A.TIM (its EDIT values) | PIT |
| JUNO | FLT (the VCF ENV depth) | PIT | PW | RES |
| NOISE | FREQ | PIT | COLR | RES |

WHEEL's DRV, CLICK and TOP are the organ's (read once a block), so ENV moves them from the track's newest voice. Every
card can be locked on a step and recorded as motion. The drum track's ENV page (CLICK, DECAY, BEND, LEVEL, per drum
sound) is unchanged.

### Destinations: two free slots on one shared list

LFO, LFO 2 and ENV 2 each have a second page with two free destinations: **DEST A** with **AMT A**, and **DEST B** with
**AMT B** (the pages LFO DEST, LFO2 DEST, ENV2 DEST; on SLOOP's UI the cards read DST A and DST B). All three read
**one shared list of destinations** (`firmware/src/core/core.h`, LT_*):

- PIT, FLT, SHP, AMP: pitch, filter, shape (the engine's SHP value), amplitude (on ENV 2 or an LFO: on top of ENV 1's);
- RES (the engine's resonance), PAN, LVL (the track's level), ATK DEC SUS REL, ATK2 DEC2 SUS2 REL2, and E1 to E8, the
  engine's own EDIT values, named by the engine;
- CHO, DLY, REV (the track's sends) and INS1, INS2 (the amount of the insert in slot S1 or S2; the LFO list names the
  insert type the slot holds);
- the rate and amount of the LFOs and envelopes themselves (L1 RT, L1 AMT, L2 RT, L2 AMT, E1 AMT: ENV 1's FLT and SHP
  depths, with ENV 2 also its PIT; E2 AMT: ENV 2's amounts): this is **modulation of modulation**. Nothing feeds back
  into a runaway: every move starts from the authored value, from the modulators' values of the block before.

An LFO moves the value for the block; an envelope moves it for each voice as that voice renders. An envelope on a track
value (PAN, LVL, the sends, the inserts, an LFO's rate or amount) follows the track's newest voice.

Defaults and rules worth knowing:

- **ENV 2's DEST A is FLT at 0** on a new sound, on INIT and on every factory preset that does not use ENV 2: raise AMT A
  and the filter follows ENV 2. Engines without a filter take their nearest: WHEEL TOP, FM6 SHP (MOD), CZ DCW (E2).
  On ACID, FLT is the 303's CUT and RES its RESO.
- **DROP** is the old fixed pitch drop that ENV DEST PIT gave: a fast fall from each note's start. It is its own value
  (VOICE 2, card 4, on the Optimist UI; VOICE 3 on SLOOP's UI), no longer a destination.
- A project, a section or a user preset made with the old fixed destinations (FLT, SHP, PIT, AMP depths) loads with the
  same sound. One made with ENV DEST's two free slots (2026-10-14 to 16) loads onto ENV 1 MOD: FLT, SHP and PIT become
  their depths, AMP is ENV 1 itself; any other destination moves to ENV 2 when ENV 2 is free (ENV's ATK DEC SUS REL
  copied into ENV 2's), else it is dropped and TOOLS > MISS names it ("ENV DEST T1").
- The accent's filter kick (velocity above 110) still uses ENV.

### What is stored

ENV 1's FLT and SHP depths are stored in the track's values, as before 2026-10-14. Its PIT and moves and the slots of
LFO, LFO 2 and ENV 2 are stored with the project, in the project's modulation record, written only when one is not at
its default. User presets store them in the bank UPB3 (216-byte records, UP_VER 4; the older records and banks UPB2 and
UPB1 are read and converted).

## Presets, favorites, the user bank

**PRESETS** browses the sounds of a synth track by kind (basses, keys, organs, pads, leads, plucks and bells, stabs);
the engine follows. Your own presets come after. There are 32 user presets; PROJECT > USER lists them, with INIT to
start from the engine's defaults. A user preset's LOAD over an edited sound is asked in red.

**Favorites** (builder item `FAVORITES`) narrow the list to the sounds you use:

- On the Optimist UI, **SHIFT2** (OCT+ held) + PRESETS marks the sound that plays (turn right) or clears it (turn left).
  SHIFT2 + SELECT browses only the favorites. On PROJECT > USER, SHIFT2 + the ITEM knob marks a user preset.
- On SLOOP's UI, SAVE > PRESETS: KNOB 3 FAV marks the sound, KNOB 4 LIST shows ALL or only the favorites.
- A favorite shows a star after its name. Up to 64 factory sounds (kept by engine and name, so a mark survives a build
  that leaves engines out) and any of the 32 user presets can be marked. The FM6 and CZ banks and the drum track have none.
- Favorites are kept with the settings (136 bytes at the end of the settings record).

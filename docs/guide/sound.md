# Sound: engines, voices, modulation, presets

What makes a synth track sound the way it does, and where each control is. The builder reference, [BUILDER.md](../BUILDER.md), lists what each engine costs and how to leave it out. Names in capitals are the labels on the screen.

## Engines

A synth track plays one engine at a time. PRESETS on a synth track loads a sound and its engine follows; SHIFT2 + PRESETS steps the engine. Every engine is a builder item (`ENG_*`). A build without an engine plays a track that uses it on a stand-in engine and shows MISSING: DIGITAL stands in for FM6, PHASE for CZ, SAMPLE for GRAIN and SLICE, ANALOG for the others (BUILDER.md names each stand-in).

**EDIT** (tapped) opens the track's sound. Its first row, SOUND, has the ENGINE, PRESET, INIT and SAVE AS cards. Then **MAIN**: the four values that matter most for the engine (CUT and RES first where it has a filter; FM6 has LPF, RES and HPF). Then the engine's own values on **EDIT 1** and **EDIT 2**, four cards each, with the page titles the engine gives them (ANALOG shows OSC and FLT, for example).

| Engine | Builder item | What it is | EDIT 1 / EDIT 2 cards |
|---|---|---|---|
| ANALOG 2 | `ENG_ANALOG` | Two band-limited oscillators with hard sync, a swarm of up to 6 detuned copies, two filters | OSC: WAVE DTN MIX NOIS; FLT: CUT RES DRV KTR. Extra pages OSC 2, SWARM, FLT 2, and ENV2 (below) |
| DIGITAL | `ENG_DIGITAL` | Four-operator FM, 8 algorithms, feedback on operator 4, one modulation INDEX with its own envelope | OPS: ALG R2 R3 R4; MOD: IDX MDEC FB |
| PHASE | `ENG_PHASE` | Phase distortion: a cosine with a bent phase, a second line to mix or ring-modulate; FOLD a sine wavefolder (`PHASE_FOLD`) | PHS: WAVE WAVE2 DCW ENV; LINE: DTN LINE SUB FOLD |
| LOFI | `ENG_LOFI` | Chip voice: pulse, triangle, saw, noise, a 4-bit wavetable, bit crush, a pitch sweep | CHIP: CHIP WAVE DUTY CRSH; MOTN: SWP VIB ARP TONE |
| SAMPLE | `ENG_SAMPLE` | Built-in sample sets and your own USR1 to USR3 samples across the keys, with loops | SET: SET TUNE BITS LOOP; TONE: CUT DRV |
| VOICE | `ENG_FORMANT` | A formant synth that sings vowels | VOWL: VOWL VOWL2 TALK SHIFT; TONE: BUZZ BRTH Q RAND |
| TRIO | `ENG_TRIO` | Three oscillators with ring modulation and hard sync into a multimode filter | OSC: WAVE INT2 INT3 DTN; TONE: MODE CUT RES PW |
| WHEEL | `ENG_DRAWBAR` | A tonewheel organ: nine drawbars per note, 16 registrations | BARS: REG SUB BODY TOP; TONE: PERC CLICK DRV ROTR |
| GRAIN | `ENG_GRAIN` | Granular: clouds of grains cut from the sample sets or your USR slots | GRAN: SRC POS SIZE DENS; SPRY: PTCH SPRD RAND TONE |
| FM6 | `ENG_FM6` | Six-operator FM that plays DX7 voices as Dexed does; its own operator editor (ENV on an FM6 track) and DX7 SysEx in; INIT VOICE and the voice packs are its sounds | PATCH: VOICE; FILT: LPF RES HPF |
| SLICE | `ENG_SLICE` | A break slicer: cuts a loop into 4, 8, 16 or 32 slices (or one per hit) and plays them from the keys or the sequencer | SLCE: SRC DIV START PTCH; PLAY: MODE REV DCAY TONE |
| PHYS | `ENG_PHYS` | Physical models: modal, string, membrane and sympathetic strings | MODEL, then STRC BRIT DAMP POS and ACC BOW EXC |
| ACID | `ENG_ACID` | A TB-303-style bass voice, monophonic | EDIT 1: CUT RESO ENV DEC; EDIT 2: ACC WAVE DRV SLD; ACID GEN: DENS ACC SLD GEN |
| CZ | `ENG_CZ` | Casio CZ-1 style tones: two lines with 8-step envelopes | EDIT 1: TONE DCW W.TIM DTN; EDIT 2: A.TIM LINE MOD VIB |
| JUNO | `ENG_JUNO` | The Juno-106 voice: DCO with saw, pulse, sub and noise, a 4-pole ladder filter, a 4-position high-pass | EDIT 1: WAVE PW SUB NOIS; EDIT 2: CUT RES KEY HPF |
| NOISE | `ENG_NOISE` | White, pink and brown noise, crackle, a shift register and metal tones through a resonant filter | EDIT 1: MODE COLR FREQ RES; EDIT 2: TRK DENS DRFT CRSH |
| DRONE | `ENG_DRONE` | A coupled network of four sines for drones and ambient (after Caravel's, LYRA-8 inspired), with binaural beats | NETWORK: SPREAD COUPLE TOPO CHAR; MOVE: DRIFT TONE BEAT HOLD |
| PLAITS | `ENG_PLAITS` | Four of Mutable Instruments Plaits' models: waveshaping, additive, granular formant, particles | MODEL: MODEL HARM TIMB MORPH; OUT: AUX TONE LPG VEL |

### Engine notes

- **ANALOG 2:** OSC 2 has WAVE2, SEMI (osc 2's interval, +-24 semitones) and SYNC (hard sync; the SHP modulation then sweeps osc 2); SWARM has SWARM (0 to 6 detuned copies, the supersaw), SDTN (their spread) and DRFT (a slow random drift); FLT 2 has FTYP (LP12, LP24, BP, HP). The filter saturates in its loop and self-oscillates at RES 126-127.
- **FM6:** DX7 voices played as Dexed plays them. VOICE: INIT VOICE and the voice packs the build carries (`FM6_PK_*`, 16 voices each, programmed for Optimist: K keys, B basses, M bells, P pads and leads, 8 80s classics), listed in PRESETS after the user presets. FILT is a filter on the part after FM6: LPF closes a low-pass from 20 kHz, RES gives it a peak, HPF opens a high-pass; 0 is off for LPF and HPF, and ENV 1 can sweep the LPF. **ENV held** on an FM6 track is the operator editor: the black keys pick OP1 to OP6, PIT, GLO, MONO, POLY, the knobs edit, the white keys keep playing. **ENV tapped** shows the same pages as rows (OPERATOR, then the six pages of the operator picked: TUNE, RATES, LEVELS, SENS, SCALE, CURVES; PIT; ALGO, LFO, LFO 2, PORTA). On those rows SHIFT2 + a black key picks the operator and SHIFT + an operator's key mutes it. An edited voice is kept as a user preset (SAVE AS) or by the project. **DX7 SysEx in** (`FM6_SYSEX`, receive only): a single voice goes into the selected FM6 track; a 32-voice bank becomes a temporary pick list at the end of PRESETS until power-off.
- **ACID:** one TB-303-style voice; a step's ACCENT is the 303's accent, a SLIDE glides into the next step. **ACID GEN** (EDIT): DENS, ACC, SLD, then GEN twice writes a new acid line into the pattern (in the track's key; undo brings the old one back).
- **JUNO:** the Juno-106 voice, six voices. The track's ENV is the Juno's envelope (ENV > FLT is the VCF ENV amount), its LFO the Juno's LFO (SHP moves the pulse width). The Juno-106 chorus is an insert type ([effects](effects.md)), on any track.
- **CZ:** ATK DEC SUS REL do not shape a CZ voice (its envelopes are the tone's).
- **NOISE:** MODE ANLG, DUST, LFSR (clocked by the key) or META; COLR tilts white to brown; FREQ and RES a filter.
- **DRONE:** a note plays four sines that modulate each other's phase, after Caravel's DRONE (bonszaimusic), itself
  inspired by the LYRA-8. **COUPLE** sets how much (the track's SHP moves it), **TOPO** who modulates whom: PAIRS (1 and
  2, 3 and 4), a CHAIN (1 to 2 to 3 to 4) or a RING; a change of TOPO crossfades. **SPREAD** opens the four from unison
  to 1 : 5/4 : 3/2 : 2, **CHAR** bends each sine toward a soft cubic (more harmonics in the coupling), **DRIFT** wanders
  each pitch slowly (deeper and a little faster as it rises), **TONE** is a dark-to-open low-pass (the track's FLT moves
  it). The track's **DTUNE** (VOICE 2) detunes the four generators. **BEAT** plays a binaural beat: the left ear
  BEAT / 2 below the note and the right ear BEAT / 2 above it, 0.05 to 40 Hz (THETA 6 Hz, ALPHA 10 Hz, DELTA 2 Hz, GAMMA
  40 Hz). It works on headphones only, and only the mid of the two ears goes through the track's inserts and sends.
  With BEAT on the network runs once an ear (twice the CPU). **HOLD ON:** a key latches its note
  and the same key again lets it go; HOLD OFF lets the latched notes go. The sequencer's notes toggle the same way, so
  a sequenced drone keeps HOLD OFF and holds its note with TIEs. One voice: a note transposes the whole network.
- **PLAITS:** four of Mutable Instruments Plaits' synthesis models (Emilie Gillet, MIT), with Plaits' own knobs:
  **HARM** (HARMONICS), **TIMB** (TIMBRE; the track's SHP and ENV 1's TIMB card move it) and **MORPH**. **MODEL WAVSH**:
  a triangle-to-saw slope (MORPH) through five waveshapers (HARM) into a wavefolder (TIMB). **ADDTV**: 24 harmonics
  under a bump spectrum: TIMB moves the brightest harmonic, HARM adds bumps, MORPH goes from a flat, wide spectrum to a
  single peak. **GRFMT**: two formants synced to the note (TIMB the formant, HARM the second one's interval, -2 to +2
  octaves, below the middle the plain carrier bleeds in; MORPH the window's shape). **PRTCL**: random pings of six
  resonant filters, each at a random pitch around the note (TIMB the density, HARM how far the pitches spread, MORPH the
  resonance); a note strikes all six. **AUX** fades to the model's second output, as Plaits' AUX jack: the Serge-like
  folder on a sine (WAVSH), an 8-drawbar organ (ADDTV), the Z oscillator (GRFMT), the raw clicks (PRTCL). **TONE** is a
  low-pass (the track's FLT moves it), **LPG** closes it as the envelope falls (Plaits' low-pass gate), **VEL** lets
  the velocity open TIMB. Four voices a track; PRTCL and ADDTV are the heaviest on the CPU.
- **SAMPLE:** SET plays a built-in set (PIANO, BASS, VIBES, HORNS, STRGS, FLUTE, SCRCH, PERC; the builder chooses which) or your USR1 to USR3 slots ([drums and samples](drums.md#your-samples)).

## Voices

### VOICES, the voice count

**VOICES** sets how many voices a synth track uses, from 1 to the engine's own maximum. It shows the number, and a new track shows the engine's maximum. The engine's maximum is 8 unless the engine caps it: ACID has 1, PHYS 3, DRONE 4 and JUNO 6. VOICES is card 3 of the **VOICE 2** page (ALLOC, DETUNE, VOICES, DROP). It counts in the POLY and UNISON modes. It is stored with the project and is not part of a sound: a preset does not change it, and it cannot be locked.

### Voice modes, glide, priority

The **VOICE** page has four cards: the mode, GLD (glide time), GLMOD and PRIO.

- **POLY** plays each note on its own voice. When the voices are all busy, the lowest held note is protected and the oldest released voice is taken first.
- **MONO** plays one voice, and every new note retriggers it; GLD glides to it.
- **LEG (LEGATO)** plays one voice and does not retrigger while a note is held: the new note glides there when GLD is set.
- **UNI (UNISON)** stacks VOICES voices on one note and spreads them in pitch: DETUNE sets the spread, up to about plus or minus 40 cents.
- **PRIO** (LAST, LOW, HIGH) picks which held note sounds in MONO, LEG and UNI.
- **GLD** is the glide time; **GLMOD** (RATE or TIME) says whether GLD is the time an octave takes (RATE) or the time of any glide (TIME). A sequencer SLIDE with GLD at 0 glides in a fixed short time.
- **ALLOC** (ROT or REUSE), on **VOICE 2**, picks the voice for a new note in POLY: ROT rotates through the free voices; REUSE takes the idle voice whose last pitch is nearest the new note. With GLD on, the nearest-pitch rule is used whatever ALLOC says.

## Modulation: two LFOs, two envelopes

### LFO 1 and LFO 2

**LFO** has the cards RATE, WAVE (SIN, TRI, SAW, SQR, S&H), PHS (start phase) and FADE (fade-in). **LFO 2** (always on) has RATE, WAVE, TRIG (NOTE restarts it with a new phrase, FREE) and POL (BI or UNI). Turn RATE down past its slowest free rate (0.05 Hz) and it locks to the tempo: one cycle is 4BAR, 2BAR, 1BAR, 1/2, 1/4, 1/8, 8T, 1/16, 16T or 1/32. It follows the tempo and an external MIDI clock.

### ENV and ENV 2

**ENV** (ENV 1) is the engine's own envelope, the track's ADSR: ATK, DEC, SUS, REL. It always shapes the amplitude. **ENV 2** (ATK2, DEC2, SUS2, REL2) is a second ADSR of its own, restarted by each note; REL2 at 0 takes DEC2's time. ENV 2 runs on every engine, and only while one of its slots acts. It exists in builds that have the ANALOG engine.

### ENV 1 MOD: four depth knobs made for each engine

**ENV 1 MOD** (shown as *ENV mod*) has no destination pickers: its four knobs are fixed for the engine, each a depth from -64 to 63 (0: nothing), named after what it moves. FLT, SHP and PIT move the voice's filter, shape and pitch (PIT: up to about 31.5 semitones at full); the others move one of the engine's EDIT values for each voice as ENV runs. An engine whose EDIT values already shape its own envelope (ACID, CZ, FM6, PHASE's DCW) shows those values here too: turning them is turning the EDIT value.

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
| FM6 | PIT | - | - | - |
| SLICE | TONE | PIT | DCAY | PTCH |
| PHYS | BRIT | PIT | DAMP | EXC |
| ACID | ENV | DEC | ACC | SLD (the 303's own EDIT values) |
| CZ | DCW | W.TIM | A.TIM (its EDIT values) | PIT |
| JUNO | FLT (the VCF ENV depth) | PIT | PW | RES |
| NOISE | FREQ | PIT | COLR | RES |
| DRONE | TONE | PIT | COUPLE | CHAR |
| PLAITS | TONE | PIT | TIMB | MORPH |

WHEEL's DRV, CLICK and TOP are the organ's, so ENV moves them from the track's newest voice. Every card can be locked on a step and recorded as motion.

### Destinations: two free slots on one shared list

LFO, LFO 2 and ENV 2 each have a second page with two free destinations: **DEST A** with **AMT A**, and **DEST B** with **AMT B** (the pages LFO DEST, LFO2 DEST, ENV2 DEST). All three read **one shared list of destinations**:

- PIT, FLT, SHP, AMP: pitch, filter, shape (the engine's SHP value), amplitude (on ENV 2 or an LFO: on top of ENV 1's);
- RES (the engine's resonance), PAN, LVL (the track's level), ATK DEC SUS REL, ATK2 DEC2 SUS2 REL2, and E1 to E8, the engine's own EDIT values, named by the engine;
- CHO, DLY, REV (the track's sends) and INS1, INS2 (the amount of the insert in slot S1 or S2);
- the rate and amount of the LFOs and envelopes themselves (L1 RT, L1 AMT, L2 RT, L2 AMT, E1 AMT, E2 AMT): this is **modulation of modulation**. Nothing feeds back into a runaway: every move starts from the authored value, from the modulators' values of the block before.

An LFO moves the value for the block; an envelope moves it for each voice as that voice renders. An envelope on a track value (PAN, LVL, the sends, the inserts, an LFO's rate or amount) follows the track's newest voice.

- **ENV 2's DEST A is FLT at 0** on a new sound, on INIT and on every factory preset that does not use ENV 2: raise AMT A and the filter follows ENV 2. Engines without a filter take their nearest: WHEEL TOP, FM6 SHP (MOD), CZ DCW (E2). On ACID, FLT is the 303's CUT and RES its RESO.
- **DROP** is a fast pitch fall from each note's start. It is its own value (VOICE 2, card 4), not a destination.
- The accent's filter kick (velocity above 110) uses ENV.
- A drum lane has its own LFOs (LFO, LFO 2) and second envelope (ENV2), each with two destination slots, per hit.

## Presets, favorites, the user bank

**PRESETS** browses the sounds of a synth track by kind (basses, keys, organs, pads, leads, plucks and bells, stabs); the engine follows. Your own presets come after. There are 32 user presets; PROJECT > USER lists them, with INIT to start from the engine's defaults. A user preset's LOAD over an edited sound is asked in red. SAVE + a page button (ENV, LFO, EDIT, FX, SCL, ARP) stores the current sound as a user preset (NAME first).

About 90 factory starting points (more with the optional engines), each a full patch on one engine, level-matched. A new project starts at 120 BPM. The sampled sets are free recordings (CC0: Versilian Studios VSCO-2 CE and VCSL, Sonic Pi).

**Favorites** (builder item `FAVORITES`) narrow the list to the sounds you use:

- **SAVE + PRESETS** marks the sound that plays (turn right) or clears it (turn left). **SHIFT2 + SELECT** browses only the favorites (right) or every sound (left). On PROJECT > USER, SHIFT2 + the ITEM knob marks a user preset.
- A favorite shows a star after its name. Up to 64 factory sounds and any of the 32 user presets can be marked. The FM6 and CZ banks and the drum track have none.

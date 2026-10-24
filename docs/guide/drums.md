# Drums: lanes, kits, your samples and kits

The drum track (DR) has **10 lanes**. Each lane plays one instrument, and each has its own sound values, inserts, LFOs and sends. The **black keys** F#3 to D#5 are the 10 lanes: a lane key plays the lane and picks it. The 16 white keys are the steps of the picked lane. F#5, the last upper key, is the ACCENT lane ([the sequencer](sequencer.md#the-drum-accent-lane)). Gestures: [shortcuts.md](shortcuts.md).

A lane plays any of 18 instruments, set with INSTR on the drum track's SOUND row (card 3): KICK, KICK 2, SNARE, CLAP, CLOSED HAT, OPEN HAT, PEDAL HAT, RIM, SNARE 2, LOW TOM, HI TOM, CRASH, RIDE, SHAKER, CONGA, COWBELL, MID TOM, CLAVE. A new project's lanes are KICK, SNARE, CLAP, CLOSED HAT, OPEN HAT, LOW TOM, HI TOM, RIM, CRASH, RIDE. The lane is named after its instrument. The closed and pedal hats choke the open one.

Every hit has one of four levels, **GHOST**, **SOFT**, **NORM** and **HARD**, and can repeat x1 to x4 inside its step (a ratchet). MIDI notes on the drum channel (10 by default) play the lane whose instrument that General MIDI note is, else a lane of the same family (kick, snare and clap, hats, toms, cymbals, percussion). The steps, levels, chance, locks and the ACCENT lane are in [the sequencer](sequencer.md).

## The kits

Up to 37 kits, then the X0X kits and KAWA and PLAITS below (the builder can leave kits out), picked with **PRESETS** on DR (or in the web editor). They set what the lanes play. 1 to 5 are a sampled acoustic kit (CC0 studio recordings) and four treatments of it; they share one sample set, so they are one builder choice (`DRUM_SAMPLED`). 6 to 37 are synthesised and cost almost no memory: each sound is a tuned body with a pitch drop, a second partial, a click, filtered noise and drive, and every kit is level-matched.

| # | Kit | # | Kit | # | Kit | # | Kit |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | ACOUSTIC | 11 | TRAP | 21 | DISCO | 31 | CHIP |
| 2 | DEEP | 12 | DRILL | 22 | GARAGE | 32 | ARCADE |
| 3 | TIGHT | 13 | BOOMBAP | 23 | JUNGLE | 33 | GLITCH |
| 4 | BRIGHT | 14 | LO-FI | 24 | DUBSTEP | 34 | INDUSTR |
| 5 | DUST | 15 | PHONK | 25 | DEMBOW | 35 | HYPER |
| 6 | 808 | 16 | HOUSE | 26 | AMAPIANO | 36 | AMBIENT |
| 7 | 909 | 17 | D.HOUSE | 27 | AFRO | 37 | JAZZ |
| 8 | 606 | 18 | TECHNO | 28 | LATIN | | |
| 9 | 80S | 19 | MINIMAL | 29 | TRIBAL | | |
| 10 | VINTAGE | 20 | ELECTRO | 30 | SYNTHWV | | |

The kit is saved with the project and its song sections.

### X0X 909 and X0X 808

Two circuit-modelled kits from X0X by Charles Vestal, builder items `DRUM_X0X909` and `DRUM_X0X808`. They come after JAZZ, each followed by style kits with the same voices: X9 TECH, X9 HOUSE, X9 UKG, X9 ACID; X8 TRAP, X8 BOOM, X8 ELEC, X8 MIAMI.

- **909:** the kick, snares, clap, rim and toms are models; the hats, crash and ride play ER-99's 909 samples (their quality is a builder choice, `X909_CYM`: 8-bit, 6-bit or none). Shaker, conga and cowbell play the synthesised 909's.
- **808:** every sound is the 808's own.
- **Any lane of any kit** can play one of their voices: SRC lists them after the kits (X9 BD ... X8 CY).

A build without them plays the synthesised 909 / 808 instead and says MISSING; the project keeps the kit.

### KAWA and PLAITS

Two more synthesised kits after the X0X ones, each a builder item of its own: three drum synths in all (the 32 kits, KAWA, PLAITS), independent of each other, so a build can have KAWA or PLAITS without the 32 kits:

- **KAWA** (`DS_KAWA`), after Kawa Tracker's kit engine: a kick with an FM beater, a rumble that swells in, a flammed snare, a brush, a clap with its hands, tight-cluster hats, a wide crash, a ride's bell, ring-modulated toms, rim and cowbell, a shaker that swells in, and a guiro on the CLAVE lane.
- **PLAITS** (`DS_PLAITS`): two kicks and two snares from Mutable Instruments Plaits' synthetic bass drum and snare drum; the other lanes are the 909's. EDIT moves them as the other sounds: TUNE, DECAY, CUT + (the kick's TONE, the snare's FM), CLICK (the kick's FM), SNAP (the snare's snap).

Any lane of another kit can play their sounds (SRC lists them after the X0X styles). A build without one plays the 808 (KAWA) or the 909 (PLAITS) for it and says MISSING; the project keeps the kit. A build without the 32 kits lists only the kits it has, starts on the first of them, and plays that kit (a sampled one first, else an X0X kit, else KAWA or PLAITS) for any kit it lacks.

## Edit a lane's sound

Select a lane (its black key, or ALGORITHM), then tap EDIT. Each lane has its own values, as **offsets from the kit** (0 is the kit as it is, so another kit stays musical). They apply from the next hit.

| Page | Values |
| --- | --- |
| **SOUND** row ("Lane") | SRC (the sound's source: the kit, USR1 to USR3, another kit's sound or an X0X voice), ACCENT (the drum track's accent amount), INSTR (the lane's instrument), KIT (the drum track's kit) |
| **MAIN** | TUNE, DECAY, CUT, LEVEL: the four that matter most |
| **Body** (the firmware's SOUND) | TUNE (+-24 st), DECAY, SNAP (noise <-> tone), CLICK (the attack) |
| **Tone** (SOUND 2) | BEND (the pitch drop), CUT, DRIVE, LEVEL (-24 to +6 dB) |
| **Sends** (SOUND 3) | the lane's own REV, DLY, CHO levels (0 to 31) and PAN, on the FX family's SENDS page too |
| **SOURCE** | HIT, START, LEN of a sample, on a lane that plays one |
| **KIT** | SLOT (your kits 1 to 16), SAVE, ERASE, RESET (this sound back to the kit's) |
| **ENV, LFO, LFO 2, ENV2** | the lane's envelope (CLICK, DECAY, BEND, LEVEL) and its LFOs and second envelope, per hit |
| **INSERTS** (FX) | the lane's two insert slots ([effects](effects.md)) |

On a synthesised sound with no tone (closed hat, shaker, clap), TUNE moves its filter and its noise high-pass, as a resampled sample would sound (`DS_NTUNE`). A sampled sound has TUNE, DECAY, CUT and LEVEL only. The edits, sends, sources and samples are saved with the project and its sections. On DR itself the SENDS page ("Kit sends") scales every lane's own sends; the drum bus has none of its own.

## Your samples

Three slots, **USR1**, **USR2** (about 7.4 s each) and **USR3** (about 5.9 s; less with snapshots, which take its end), load from the web editor's **Samples** tab (in a build with live editing, `ED_LIVE`; a backup restore also writes them):

- **Files:** up to 16 WAV per slot; the note each plays at its own pitch is in its name (`KEYS_C4.wav`).
- **CHOP:** cut one recording (WAV, MP3, AIFF...) into up to 16 chops, by tapping while it plays, by its hits, on a grid or in equal parts, then send them to a slot.

A synth track plays a slot with the SAMPLE engine (**SET** USR1 to USR3); a drum lane plays one with SRC. In a build with `PATTERNS`, USR1's flash holds the projects' log and the slot takes no sample.

## Your own kits

Build a kit lane by lane (a kit's sound, edited or not, another kit's sound, your samples), then KIT > **SLOT** and **SAVE** (twice), or SAVE + a page button on DR: it is stored as **KIT n**, one of 16 in the FM-1. Your kits come after the built-in ones in PRESETS. The editor exports a kit with its samples as one file and imports it on another FM-1.

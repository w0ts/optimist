# Drums: kits, sounds, your samples and kits

The drum track (track 4) plays **16 sounds, one per white key**, from the lowest F to the highest G. A black key plays
the sound of the white key on its left (two fingers on one sound, for fast rolls).

| Key | Sound | Key | Sound | Key | Sound | Key | Sound |
| --- | --- | --- | --- | --- | --- | --- | --- |
| F3 | kick | C4 | closed hat | G4 | snare 2 | D5 | ride |
| G3 | kick 2 | D4 | open hat | A4 | low tom | E5 | shaker |
| A3 | snare | E4 | pedal hat | B4 | hi tom | F5 | conga |
| B3 | clap | F4 | rim | C5 | crash | G5 | cowbell |

Every hit has one of four levels, **GHOST**, **SOFT**, **NORM** and **HARD**, and can repeat x1–x4 inside its step
(a ratchet). The closed and pedal hats choke the open one. MIDI notes on the drum channel (10 by default) play the
nearest of the 16 sounds. The steps, levels, chance, locks and the ACCENT lane are in [the sequencer](sequencer.md).

## The kits

Up to 37 kits (the builder can leave kits out), picked with **PRESETS** on the drum track or in the web editor.
1–5 are a sampled acoustic kit (CC0 studio recordings) and four treatments of it; they share one sample set, so they
are one builder choice (`DRUM_SAMPLED`). 6–37 are synthesised and cost almost no memory: each sound is a tuned body
with a pitch drop, a second partial, a click, filtered noise and drive, and every kit is level-matched.

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

Two circuit-modelled kits from X0X by Charles Vestal, builder items `DRUM_X0X909` and `DRUM_X0X808` (in the Synth
build). They come after JAZZ, each followed by style kits with the same voices: X9 TECH, X9 HOUSE, X9 UKG, X9 ACID;
X8 TRAP, X8 BOOM, X8 ELEC, X8 MIAMI.

- **909:** the kick, snares, clap, rim and toms are models; the hats, crash and ride play ER-99's 909 samples (their
  quality is a builder choice, `X909_CYM`: 8-bit, 6-bit or none). Shaker, conga and cowbell play the synthesised 909's.
- **808:** all 16 keys are the 808's own sounds.
- **Any key of any kit** can play one of their voices: SOURCE > SRC lists them after the kits (X9 BD … X8 CY).

A build without them plays the synthesised 909 / 808 instead and says MISSING; the project keeps the kit.

## Edit a sound

Each of the 16 sounds has its own values, as **offsets from the kit** (0 is the kit as it is, so another kit stays
musical). They apply from the next hit.

| Page | Values |
| --- | --- |
| **SOUND** | TUNE (±24 st), DECAY, SNAP (noise ↔ tone), CLICK (the attack) |
| **SOUND 2** | BEND (the pitch drop), CUT, DRIVE, LEVEL (−24..+6 dB) |
| **SOUND 3** | the sound's sends: REV, DLY, CHO (0–31; a kit sends REV 4) |
| **SOURCE** | SRC: the kit, USR1–USR3, another kit's sound for this key or an X0X voice; HIT, START, LEN for a sample |
| **KIT** | SLOT (your kits 1–16), SAVE, ERASE, RESET (this sound back to the kit's) |

A sampled sound has TUNE, DECAY, CUT and LEVEL only. On the Optimist UI each lane also has its own inserts, LFOs and
envelope; see [the interface](interface.md) and [effects](effects.md). The edits, sends, sources and samples are saved
with the project and its sections.

## Your samples

Three slots, **USR1**, **USR2** (about 7.4 s each) and **USR3** (about 5.9 s; less with snapshots, which take its
end), load from the web editor's **Samples** tab:

- **Files:** up to 16 WAV per slot; the note each plays at its own pitch is in its name (`KEYS_C4.wav`).
- **CHOP:** cut one recording (WAV, MP3, AIFF…) into up to 16 chops, by tapping while it plays, by its hits, on a grid
  or in equal parts, then send them to a slot.

A synth track plays a slot with the SAMPLE engine (**SET** USR1–USR3); a drum key plays one with SOURCE > SRC.

## Your own kits

Build a kit key by key (a kit's sound, edited or not, another kit's sound, your samples), then KIT > **SLOT** and
**SAVE** (twice): it is stored as **KIT n**, in a bank of 16 in the FM-1. Your kits come after the built-in ones in
PRESETS. The editor exports a kit with its samples as one file and imports it on another FM-1.

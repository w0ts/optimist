# SLOOP's interface

A build made with the builder item `UI=0` (the builder's default) plays with SLOOP's interface: hold a function
button, touch a key. Of the published flavours, Synth (the default flavour) uses the Optimist UI; drum machine, FM + VA studio and everything fits use SLOOP's. The Optimist UI: [the interface](interface.md),
[every shortcut](shortcuts.md). What the two share (the sound, the sequencer's data, the effects, MIDI) is in the other chapters of this guide.

## Sixty seconds to a beat

1. **ALGORITHM** to track **4** (orange, drums): the white keys play 16 sounds ([drums](drums.md)). **PRESETS** picks a kit.
2. **REC**: *rec ready*. Play a beat at your own tempo, no click. **OCT−** held: ghost notes, **OCT+** held: hard ones.
3. **Press REC on the "1" after your last bar**: the loop closes, its length sets the tempo, the hits snap to the grid.
4. **REC** again while it plays: overdub. Hold **ARP** and a hat key: a 1/16 roll, recorded as ratchets.
5. **ALGORITHM** to track **1**, **REC**, play a bass line. Hold **SCL** and press the key of your song.
6. Hold **FX** and a white key: a punch-in effect. Made a mistake? **EDIT + OCT−**: undo.

The colours: track 1 blue, 2 green, 3 yellow, 4 (drums) orange, as KNOB 1–4. White is what you touch, red is recording.

## Tap and hold: the layers

**Tap** a function button: its pages. **Hold** it: a **layer**, the 16 white keys and KNOB 1–4 change job while it is
held (the screen shows them after 0.35 s; HOME menu > SYSTEM > HOLD sets 250, 350 or 500 ms). **Hold + tap HOME**
locks the layer open; any other button lets it go.

| Hold | Keys | KNOB 1 · 2 · 3 · 4 |
| --- | --- | --- |
| **FX** | a punch-in effect while the key is held | FILTER · DUST · DUCK · — |
| **EDIT** | erase that sound / note (playing: from every step it passes; stopped: from the whole pattern) | SHIFT · LENGTH ×2 / ½ · TRANSPOSE · — |
| **ARP** | note repeat on the grid (1/8 … 1/64), recorded as ratchets | RATE |
| **SEQ** | steps 1–16 of the page (black keys or OCT pick the page); hold a step and turn a knob to edit it | SOUND / NOTE · DIV · SWING · LENGTH |
| **SCL** | the key of the song | CHORD · SCALE · KEYS · TRANSPOSE |
| **GLO** | 1–4 mute, 5–8 solo, 9–12 FX on / off (with fills: 9 fill, 10 fill next bar, FX on black keys 1–4), 16 tap tempo | levels of tracks 1–4 |
| **SAVE** | 1–4 play a section (next bar), 5–8 save the loop into it, OCT the bank, 13 loop / song, 14 SONG REC, 16 the chain | — |
| **ENV** (FM6 track) | black keys: OP1–OP6, PIT, GLO, MONO, POLY; white keys play | the FM6 page's four values |

| Control | Action |
| --- | --- |
| **PLAY** | start / stop (works inside a layer) |
| **REC** | playing: record now / stop; stopped: arm (the first note starts); hold ~2 s: clear the track |
| **EDIT + OCT− / OCT+** | undo / redo |
| **ALGORITHM** · **PRESETS** · **SELECT** | the track · its sound or kit · the tempo |
| **OCT− / OCT+** | octave (both: back to 0); on drums, held: ghost / hard hits |
| **HOME** | the TRACKS screen; held: the menu |

## Recording

Notes land on the step **as you heard them** (the key-to-sound delay is taken back). Chords are kept (up to 4 notes a
step), held notes become ties. On an empty project the first take is a **free take**: play, then press REC on the "1"
after your last bar; Optimist picks 1, 2 or 4 bars at the nearest tempo. With `REC_MODES` the REC screen also offers a
fixed tempo, a loop length and a one-bar count-in. The PLAY light flashes on every beat; GLO > GLOBAL > CLICK adds an
audible click (never recorded). Swing is MPC swing, 50–75 %, global and per track.

## The master and the punch-in effects

FX held, KNOB 1–3 (or GLO > MASTER): **FILTER** (a DJ filter: left low-pass, right high-pass), **DUST** (an old sampler
and a record), **DUCK** (every kick pumps the synth tracks). Hold FX and a white key for a punch-in effect on the whole
mix, in time:

| Key | Effect | Key | Effect | Key | Effect | Key | Effect |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | loop 1/4 | 5 | stutter | 9 | low-pass sweep | 13 | alias |
| 2 | loop 1/8 | 6 | reverse | 10 | high-pass sweep | 14 | gate 1/16 |
| 3 | loop 1/16 | 7 | tape stop | 11 | phone | 15 | echo |
| 4 | loop 1/32 | 8 | half speed | 12 | bit crush | 16 | tape wobble |

## Songs

Sections A–P, four banks of four. Hold **SAVE**: keys 5–8 save the loop into a section (twice over a used one), keys
1–4 play one from the next bar, key 14 records the song as you play the sections, key 13 switches loop / song. The
**SONG screen** (SAVE tapped on TRACKS) edits the chain: KNOB 1 the step, 2 its section, 3 its bars, 4 the number of steps.

## Save, autosave, undo

SAVE + keys 5–8 save the project into a section; SAVE > PROJECT has SLOT, LOAD and SAVE. The working project is kept by
itself when you stop and leave the FM-1 alone for 2.5 s, and comes back at power-on. Undo has many levels on every
track (each pass, erase, clear or held-layer edit is one); loading a project clears the history.

## The HOME menu

Held HOME: COLOR (the palette), LOWCUT (OFF, LOWCUT, or BASS+ for the small speaker), BRIGHT, LIGHTS and KEYS (the
buttons and keys glow, for a dark stage), SYSTEM (the MIDI channels, MIDI OUT and IN, CLOCK OUT, BLUETOOTH and DEVICES,
USB SERIAL), calibration and ABOUT. GLO > SYSTEM > VIEW: ALL shows every page of a family at once, PAGE one at a time.

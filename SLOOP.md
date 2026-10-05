<p align="center"><img src="assets/logo/sloop-logo.png" alt="SLOOP" width="480"></p>

# SLOOP 2.2

**A live groovebox firmware for the M-VAVE FM-1 — for any style.** Four tracks — three synths and a drum machine with 16 sounds on the white keys — ten synthesis engines, 92 sounds, 37 drum kits (808, 909, trap, phonk, house, techno, UK garage, jungle, amapiano, reggaeton, synthwave, chiptune, ambient, jazz…), your own samples, ghost notes and ratchets, note repeat, one-key chords, 16 punch-in effects, a vinyl / sidechain / DJ-filter master, and a teenage-engineering-style screen that always shows what your hands can do next. No factory patterns, nothing to load: everything you hear, you play.

SLOOP is free and open source (GPL-3.0), based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments.

> **Status:** 2.2, running on the FM-1. Still a beta: install at your own risk, and please report what you find (GitHub issues).

### New in 2.2

- **A new drum engine.** Every synthesised drum is now built like on the classic machines: a tuned body with a pitch drop and a hold, a second partial for the drum heads, a click for the attack, noise through a resonant filter, drive. Softer hits are darker as well as quieter. The 32 synthesised kits are rebuilt on it, each with 16 sounds of its own — new: **PHONK** (melodic cowbell), **AMAPIANO** (log drum), **GARAGE**, **D.HOUSE**. Kits 1–5 are now a sampled **ACOUSTIC** kit (CC0 studio recordings) and its treatments. Every kit is level-matched. See [Drum kits](#drum-kits).
- **68 sounds, browsed by kind.** PRESETS goes through basses, keys, organs, pads, leads, plucks and bells, stabs; the kind is shown next to the name. 14 new: 808 SLIDE, ACID 303, PLUGG BASS, SUPERSAW, M1 PIANO, AFRO KEYS, GRAND PNO, KALIMBA, PLUGG BELL, GLASS PAD, SAW PAD, RAVE STAB, DUB CHORD, HOUSE ORGN. Every factory sound is level-matched: the same LEVEL gives the same loudness. See [The sound bank](#the-sound-bank).
- **A real grand piano.** GRAND PNO is a Steinway recorded note by note (CC0); long notes fade as on the real one. DUSTY PNO and LOFI KEYS are the same piano through an old sampler.
- **Lock a layer.** Hold a layer button and tap HOME: the layer stays open with the button let go, both hands free (FX with one hand on the keys and the other on FILTER / DUST / DUCK). Any other button lets it go. See [The panel](#the-panel-tap-hold-layers).
- **Stereo effects.** The chorus is stereo, and the reverb is new: a feedback delay network, dense and wide, with no metallic ring.
- **More reliable.** Saves that fail are retried (*SAVE ERROR: RETRYING*) and everything is saved before an update; the end of a song gives your loop back; swing never plays a step twice; SONG REC counts bars right; a voice retriggered in UNISON, TRIO or PHASE no longer clicks; NEW PROJECT and saving a user preset wait until the song stops; the installer refuses a damaged package before writing it; the button lights no longer flicker.

### New in 2.1

- **Songs, live:** hold SAVE — keys 1–4 play sections A–D on the next bar, keys 5–8 save the loop into them, key 14 records the song as you play it (each section and its bars). See [Song mode](#song-mode).
- **Landmarks on the keys:** while a layer is held, and on the drum track, keys 1, 5, 9 and 13 glow dimly — the first key of each row of the 4 × 4 grid on the screen. What is on (an effect, a step, a sound) stays fully lit.

### New in 2.0

- **Hold a button, touch a key.** Every function button is a *layer*: hold it and the 16 white keys and the four knobs change job, the screen shows how. Tap it and its pages open as before.
- **16 drum sounds on the white keys**, black keys double them. **OCT− / OCT+ held** = ghost / hard hits. Hits keep their level and a **ratchet** (x1–x4) in the pattern.
- **Note repeat** (ARP + key), **erase as it plays** (EDIT + key), **steps under your fingers** (SEQ + key, Elektron style), **one-key chords in the song's key** (SCL), **mute / solo / tap tempo** (GLO).
- **Undo / redo** (EDIT + OCT− / OCT+), **hold REC to clear**, and an **autosave** that brings your beat back at power-on.
- **MPC swing** (50–75 %), a sample-accurate clock (no drift, any tempo), tighter glides for the 808s.
- **Master:** **DUST** (an old sampler and a record: bits, rate, crackle), **DUCK** (the kick pumps the synths), **FILT** (DJ filter: low-pass ← OFF → high-pass).
- **Web editor:** the drum track as a 16-lane grid with levels and ratchets, the kit, the master page.
- **Safer updates:** the installer checks the package's SHA-256 and only resumes SLOOP's own update loader; the loader checks the package CRC before it starts the new firmware, and refuses a flash chip it does not know.

---

## Contents

1. [Install](#install)
2. [Sixty seconds to a beat](#sixty-seconds-to-a-beat)
3. [The colour code](#the-colour-code)
4. [The panel: tap, hold, layers](#the-panel-tap-hold-layers)
5. [The drum track](#the-drum-track)
6. [Recording](#recording)
7. [Layers in detail](#layers-in-detail)
8. [Undo, clear, save, autosave](#undo-clear-save-autosave)
9. [Master: DUST, DUCK, FILT](#master-dust-duck-filt)
10. [Punch-in effects](#punch-in-effects)
11. [Screens](#screens)
12. [The sound bank](#the-sound-bank)
13. [Drum kits](#drum-kits)
14. [Your own samples](#your-own-samples-usr1usr3)
15. [Song mode](#song-mode)
16. [MIDI in: a keyboard on SLOOP](#midi-in-a-keyboard-on-sloop)
17. [MIDI clock and sync](#midi-clock-and-sync)
18. [The web editor](#the-web-editor)
19. [USB audio (experimental)](#usb-audio-experimental)
20. [Sound design pages](#sound-design-pages)
21. [Specifications](#specifications)
22. [Rescue, going back, credits](#rescue-going-back-credits)

---

## Install

1. Double-click **`INSTALL-SLOOP.bat`** in the SLOOP folder. It builds the firmware and opens the installer at `http://localhost:8766/webapp/installer/`.
2. In **Chrome or Edge**, connect the FM-1 to the computer by USB (a data cable, directly — no hub).
3. Press **INSTALL**, allow MIDI access, and wait for *Done*. Keep the black window open until then.

The FM-1 restarts on the SLOOP logo. The editor is at `http://localhost:8766/webapp/editor/` (or **`OPEN-EDITOR.bat`**).

## Sixty seconds to a beat

1. **ALGORITHM** to track **4** (orange, drums). The white keys play 16 sounds: **F3 kick**, G3 kick 2, A3 snare, B3 clap, **C4 hat**, D4 open hat… **PRESETS** picks a kit: try *808* or *BOOMBAP*.
2. Press **REC**: *rec ready*. **Play a beat freely, at your own tempo** — no click, no count-in. Hold **OCT−** while you hit for ghost notes, **OCT+** for hard ones.
3. **Press REC on the "1" after your last bar.** The loop closes: its length sets the tempo, the hits snap to the grid, the loop plays at once.
4. **REC** again while it plays: you record on top (overdub). Hold **ARP** and hold the hat key: a 1/16 hat roll, recorded as ratchets.
5. Turn **ALGORITHM** to track **1** (blue, *808 BOOM*), **REC**, play a bass line. Hold **SCL** and press the key of your song (e.g. D); on track 2 hold SCL and turn **KNOB 1** to *7TH*: every white key is now a chord of the key.
6. Hold **FX** and press a white key for a punch-in effect; still holding FX, turn **KNOB 2** for DUST, **KNOB 3** for DUCK.
7. Made a mistake? Hold **EDIT** and press **OCT−**: undo.

## The colour code

| Colour | Track | Knob |
| --- | --- | --- |
| **blue** | 1 · synth | KNOB 1 |
| **green** | 2 · synth | KNOB 2 |
| **yellow** | 3 · synth | KNOB 3 |
| **orange** | 4 · drums | KNOB 4 |

The four dials at the bottom of the screen show what KNOB 1–4 do now. White always means *what you are touching*. Red always means *recording*.

## The panel: tap, hold, layers

Every function button has two lives. **Tap** it (press and let go, touching nothing else): its pages open, as on any FM-1 firmware. **Hold** it: a **layer** — the 16 white keys and KNOB 1–4 change job while it is held, and after 0.14 s the screen shows the 16 keys as tiles and the knobs as dials. Let go: back to playing.

The tiles are four rows of four, keys 1–4, 5–8, 9–12, 13–16. To find them without looking at the screen, the first key of each row (1, 5, 9, 13) glows dimly while a layer is held, and on the drum track; the keys at full light are what is on.

**Lock a layer:** hold its button and tap **HOME** — the layer stays open when you let the button go, both hands free for the keys and the knobs (*LOCK* on the screen, the button blinks). Any other button lets it go (HOME, the layer's own button, ENV…) and does only that; PLAY, REC and OCT− / OCT+ keep working inside it.

| Hold | Keys | KNOB 1 · 2 · 3 · 4 | Tap |
| --- | --- | --- | --- |
| **FX** — *punch* | a punch-in effect while the key is held | FILTER · DUST · DUCK · — | FX pages |
| **EDIT** — *erase* | erase that sound / note from the pattern | SHIFT · LENGTH ×2 / ½ · TRANSPOSE · — | EDIT pages (drums: grid / kit) |
| **ARP** — *roll* | note repeat on the grid | RATE · — · — · — | ARP pages |
| **SEQ** — *steps* | steps 1–16 of the page | SOUND / NOTE · DIV · SWING · LENGTH | SEQ pages (drums: grid / kit) |
| **SCL** — *key* | the key of the song | CHORD · SCALE · KEYS · TRANSPOSE | SCL pages |
| **GLO** — *mix* | 1–4 mute · 5–8 solo · 9–12 FX on / off · 16 tap tempo | level of tracks 1 · 2 · 3 · 4 | GLO pages |
| **SAVE** — *song* | 1–4 play section A–D (next bar) · 5–8 save the loop into A–D · 13 loop / song · 14 SONG REC · 16 the chain | — | TRACKS: the SONG screen · else the SAVE pages |
| **ENV** — *ops* (FM6 track only) | black keys: OP1–OP6 · PIT · GLO · MONO · POLY; white keys play | the four values of the FM6 page | the FM6 editor, then its next page |

Other controls:

| Control | Action |
| --- | --- |
| **PLAY** | start / stop all four tracks (works inside any layer) |
| **REC** | playing: record now / stop · stopped: arm (the first note starts) · free take: close the loop |
| hold **REC** | clear the selected track (a ring fills: keep holding ~2 s; let go before and nothing happens) |
| **SAVE** | on TRACKS: the SONG screen · elsewhere: the SAVE pages |
| **EDIT + OCT− / OCT+** | undo / redo |
| **ALGORITHM** | select the track (on every page) |
| **PRESETS** | the selected track's sound, or the drum kit |
| **SELECT** | tempo (always, even inside a layer) |
| **OCT− / OCT+** | synth tracks: octave (both: back to 0) · drum track, held: ghost / hard hits |
| **HOME** | the TRACKS screen · hold: menu (colour, low cut, zoom, calibration, about) · tapped while a layer is held: lock it open |
| **ENV / LFO** | their pages (ENV on an FM6 track: the FM6 operator editor) |

## The drum track

Track 4 plays **16 sounds, one per white key** from the lowest F to the highest G; a black key plays the sound of the white key on its left (two fingers on one sound, for fast rolls).

| Key | Sound | Key | Sound | Key | Sound | Key | Sound |
| --- | --- | --- | --- | --- | --- | --- | --- |
| F3 | kick | C4 | hat | G4 | snare 2 | D5 | ride |
| G3 | kick 2 | D4 | open hat | A4 | low tom | E5 | shaker |
| A3 | snare | E4 | pedal hat | B4 | hi tom | F5 | conga |
| B3 | clap | F4 | rim | C5 | crash | G5 | cowbell |

**Levels:** every hit has one of four levels — **GHOST**, **SOFT**, **NORM** (as played), **HARD**. Hold **OCT−** while you hit for ghost notes, **OCT+** for hard hits; they are recorded so. **Ratchets:** a hit can repeat x1–x4 inside its step (ARP rolls record them; SEQ + a step + KNOB 3 sets them). The closed and pedal hats choke the open one.

## Recording

SLOOP records live and layers every pass on top of the last (overdub). Notes go to the nearest step **as you heard it**: the time between a key and its sound (~12 ms) is taken back, so what you play on the beat lands on the beat. Chords are kept on the synth tracks (up to 4 notes a step); held notes become ties.

| When | REC does | Then |
| --- | --- | --- |
| **Playing** | records the selected track **at once** | REC again stops recording, the loop plays on |
| **Stopped, project with notes** | arms (*rec ready*, the REC light blinks) | **your first note starts the loop and is step 1**; PLAY starts it too |
| **Stopped, empty project** | arms (*rec ready* · *play freely*) | a **free take**: see below |

**Free take — the loop follows you.** On an empty project there is no tempo yet, so you set it by playing:

1. REC, then play freely. The screen shows *free take*, the seconds, and the loop it would make right now (*2 bars · 92 bpm*).
2. **Press REC on the "1" after your last bar.** The time from your first note to that press is the loop: SLOOP picks 1, 2 or 4 bars at the tempo nearest the one set (within 3 % the set tempo is kept), writes your notes into it with their lengths and levels, and plays it at once. All four tracks take that length.
3. **PLAY** during a free take drops it. A take closes by itself after 24 s.

- While recording the REC light is solid and the track shows a red *rec*. Turn ALGORITHM and the take moves to the next track without stopping.
- The **PLAY light flashes on every beat**: a visual metronome. An audible click: GLO → GLOBAL → **CLICK** (`OFF`, `REC`, `ON`); it is never recorded.
- **Swing** is MPC swing: 50 % straight to 75 % (GLO → GLOBAL → SWING for all tracks, SEQ + KNOB 3 per track). The swing of a track adds to the global one.

## Layers in detail

### FX — punch

The 16 white keys are the [punch-in effects](#punch-in-effects); they run while the key is held. The knobs drive the [master](#master-dust-duck-filt): **KNOB 1 FILTER** (turn left: low-pass, right: high-pass, centre: off), **KNOB 2 DUST**, **KNOB 3 DUCK**. Keys pressed while FX is held never play or record notes.

### EDIT — erase

Hold EDIT and press a key: that sound (drums) or that note (synths; with CHORD on, the notes of its chord) leaves the selected track's pattern — **while playing**, from every step the playhead passes while you hold the key (MPC style: hold the hat key for one bar and the hats of that bar are gone); **stopped**, from the whole pattern at once. *ERASED* flashes. The knobs reshape the whole pattern:

- **KNOB 1 SHIFT** — every step one later / earlier (turns the groove around).
- **KNOB 2 LENGTH** — right: ×2 (the pattern copied after itself, up to 64 steps); left: ½.
- **KNOB 3 TRANSPOSE** — every note a semitone up / down (synth tracks).
- **OCT− undo · OCT+ redo** (the knob turns of one hold count as one change).

### ARP — roll (note repeat)

Hold ARP and hold a key: it repeats on the grid at the **RATE** of KNOB 1 — 1/8, 1/16, 1/32, 32T, 1/64 — locked to the tempo and the swing, so it always lands in time. On the drum track OCT− / OCT+ make it ghost / hard. While recording, a roll is written as ratchets (a 1/32 roll on a 1/16 track: x2 on each step). Rolls end with their key.

### SEQ — steps (step sequencer)

The 16 white keys are the 16 steps of the page; the lit ones play. The first four black keys (F#3, G#3, A#3, C#4) or **OCT− / OCT+** pick page 1–4 (steps 1–16, 17–32, 33–48, 49–64, up to the track's LENGTH).

- **An empty step:** press its key — it is set at once. Drums: with the sound shown (KNOB 1 picks it, or the last pad you hit); synths: with the note or chord you played last.
- **A set step:** press and let go — it is cleared. Hold it and turn a knob instead — it is edited, and kept: **KNOB 1** sound (drums) / note (synths), **KNOB 2 LEVEL** (ghost, soft, norm, hard), **KNOB 3 RATCHET** (x1–x4), **KNOB 4 LENGTH** (synths: how many steps the note lasts, 1–16 shown, written as ties — longer through empty steps up to the next note, shorter clears its own ties; the tiles show the ties as --). Hold several step keys to edit them together.
- **No step held:** KNOB 1 the sound / note to set · KNOB 2 **DIV** (1/4 … 1/32, triplets) · KNOB 3 **SWING** of the track · KNOB 4 **LENGTH** (1–64 steps; each track loops on its own length, polymeters stay in phase).

### SCL — key and chords

- **Any key** sets the **key of the song**: the root of all three synth tracks (*KEY D*).
- **KNOB 1 CHORD** (selected synth track): OFF, TRIAD, 7TH, 9TH (1-3-7-9, the lo-fi / R&B voicing), SUS4, POWER. With a chord on, **the white keys walk the scale from C4** — C4 is the chord of the key's I, D4 the II, E4 the III… — and one finger plays the whole chord, recorded as a chord. With SCALE on CHR, the chords come from the minor scale.
- **KNOB 2 SCALE** for all synth tracks (16 scales: major, minor, dorian, mixolydian, pentatonics, harmonic, blues…).
- **KNOB 3 KEYS**: OFF (all keys chromatic), SNAP (every key rounded to the scale), WHITE (the white keys walk the scale, the black keys are silent), ALL (every key, white or black, is the next degree of the scale: C4 is the root, C#4 the second degree, D4 the third… — a denser scale keyboard; with a chord on, every key plays one).
- **KNOB 4 TRANSPOSE** the selected track, ±24 semitones.

Changing a sound (PRESETS, a user preset) never changes the key, the chord mode, the pattern or the mix of its track.

### ENV on an FM6 track — the operator editor

The FM-1's black keys are printed OP1–OP6, PIT, GLO, MONO, POLY: on an FM6 track they mean just that. **Hold ENV** and press a black key to pick what the knobs edit; the white keys keep playing, so you hear each turn.

- **OP1–OP6** the operator · **PIT** the pitch envelope · **GLO** algorithm, feedback, LFO, portamento and STORE · **MONO / POLY** the voice mode.
- **Pages**, as everywhere: **tap ENV** for the next one; with ENV held, **OCT−** goes back one, **OCT+** on one. An operator has FREQ, LEVEL (with its ON switch), EG RATE, EG LEVEL, KEY SCALE, CURVES; PIT has RATE and LEVEL; GLO has ALGO, LFO, LFO 2, PORTA, STORE.
- **GLO › STORE**: KNOB 1 picks a user slot U01–U32 (the voice's own, or the first empty one), KNOB 2 **STORE**, KNOB 3 **SEND** (the voice out as a DX7 dump), KNOB 4 **INIT** (the DX7 init voice) — one detent arms, a second one acts.
- The page stays up when ENV is let go: **KNOB 1–4** edit the four values shown (the black keys play notes again). The screen shows the algorithm — the operator picked in white, carriers in yellow, switched-off or silent operators dim, the lit feedback loop — the voice name and the black keys' map.
- Edits go into the track's voice; held notes follow them. A project (and a live section) keeps the voice with its edits; **VOICE** (EDIT 1) or a preset loads a voice over them.

### GLO — mix

- White keys **1–4 mute** tracks 1–4 (a muted track fades out in a few ms and plays no new notes; its pattern runs on in time), keys **5–8 solo** them (several solos add up). The tiles show what is heard.
- Keys **9–12 turn the effects of tracks 1–4 off and on** (FX bypass): the track plays dry — no DIST, no SLICER, no chorus / delay / reverb send (the drum track: its reverb) — and its FX values stay as they are, so turning it back on brings the sound back. The tiles read *fx n* (on) or *dry n*; the TRACKS row says *dry*, the FX page *FX OFF*. The master effects (DUST, DUCK, the filter, the punch-in FX) stay on the whole mix. Saved with the project (projects of SLOOP 2.0 .. 2.2 load with every FX on).
- The last white key (**G5**): **tap tempo** (two taps or more).
- **KNOB 1–4: the levels** of tracks 1–4.

## Undo, clear, save, autosave

- **Undo / redo:** hold EDIT, press OCT− / OCT+. One level: the last recording pass, erase, clear, step or pattern edit; redo takes it back again.
- **Clear a track:** hold REC. After 0.7 s the press is cancelled and a ring fills; keep holding ~1.3 s more and the selected track is cleared (*TRACK 2 CLEARED*). Let go before: nothing. Undo brings it back.
- **Save:** SAVE + keys 5–8 save the loop into section / project A–D (= SLOT 1–4); SAVE → PROJECT has SLOT, LOAD, SAVE too.
- **Autosave:** when the transport is stopped and you have not touched anything for 2.5 s (at most every 20 s), the working project is kept in flash; at power-on SLOOP comes back exactly as you left it.
- **New project:** SAVE → TOOLS → NEW (turn to GO): the four tracks back to their power-on sounds, empty patterns (undoable).

## Master: DUST, DUCK, FILT

On the whole mix, after the tracks' sends (FX + KNOB 1–3, or GLO → MASTER):

- **DUST** 0–100 %: the mix through an old sampler and a record — drive into a soft clip, a lower sample rate (down to ~11 kHz), fewer bits (down to 8), a low-pass closing to ~3 kHz, and while the transport plays a little hiss and crackle (a stopped SLOOP is silent).
- **DUCK** 0–100 %: every kick pumps the synth tracks down and back over an 1/8 note — the sidechain sound, in time at any tempo.
- **FILT**: a DJ filter. Left of centre a low-pass closing, right a high-pass opening, centre OFF. It glides (no zipper noise).
- **ROLL** (GLO → MASTER): the note-repeat rate of ARP + key.

## Punch-in effects

Hold **FX**, then hold a white key — the 16 white keys from the lowest F to the highest G. The effect runs on the whole mix while the key is held and lets go cleanly when you release it. Loops and the gate are locked to the tempo and start on the grid.

| Key | Effect | Key | Effect |
| --- | --- | --- | --- |
| 1 | loop 1/4 | 9 | low-pass sweep |
| 2 | loop 1/8 | 10 | high-pass sweep |
| 3 | loop 1/16 | 11 | phone |
| 4 | loop 1/32 | 12 | bit crush |
| 5 | stutter (1/16 triplets) | 13 | alias (sample-rate drop) |
| 6 | reverse | 14 | gate 1/16 |
| 7 | tape stop | 15 | echo (dotted 1/8) |
| 8 | half speed | 16 | tape wobble |

## Screens

- **TRACKS** (HOME) — the performance view: tempo, swing, transport, bar.beat; each track with its sound, its steps, the playhead, mute / solo / rec badges and its level. Dials: *swing · level · steps · pan* (KNOB 2 on a muted track unmutes it).
- **Layers** — while a layer button is held: 16 tiles (the white keys) and the knobs' dials, in the layer's colour.
- **DRUMS** (EDIT or SEQ tapped on TRACKS with the drum track) — **grid**: the 16 sounds × 16 steps, levels as shades, ratchets as notches; dials *sound · step · hit · level*. **kit**: 16 pads that flash on every hit; dials *kit · level · reverb · pan*. EDIT / SEQ tapped switches grid ↔ kit.
- **REC READY / FREE TAKE** — while REC is armed, and during a free take: the seconds and the loop it makes.
- **Holds** — the ring of REC (clear) while held.
- **SONG** — the section chain.
- **Sound pages** (ENV, LFO, FX, SCL, EDIT, ARP, SEQ, GLO, SAVE) — the full synth, colour-coded. **GLO > SYSTEM > VIEW**: **ALL** (the default) shows every page of the EDIT, ENV, LFO, FX and GLO families at once, a row of four values per page: the page the knobs edit is lit, the others are dimmed, the family button still steps through them (ENV and LFO keep a shorter graph under their two rows; FX drops its send bars). On an FM6 track ENV opens the FM6 operator editor instead (its own screen, not a row of the overview). **PAGE** shows one page at a time with its graph. A device setting, kept with the colour palette.

## The sound bank

92 starting points for any style: house and techno, hip-hop, trap and plugg, drum & bass, amapiano, synthwave, lo-fi, ambient, soul. Every one is a full patch on one of the ten engines: change it, save your own (32 user presets), or load your own samples. **PRESETS** browses them on a synth track **by kind** — basses, keys, organs, pads, leads, plucks and bells, stabs — the kind shown next to the name (the engine follows); your own presets come after. Every sound is level-matched: they all come out as loud at the same LEVEL. SLOOP starts (on a new project) at **90 BPM** with *808 BOOM* on track 1, *RHODES* on track 2, *LOFI FLUTE* on track 3 and the 808 kit on track 4.

| Kind | Sounds (engine) |
| --- | --- |
| **Bass** | 808 BOOM, 808 DIRTY, 808 SLIDE, SUB BASS, PLUGG BASS — they slide between held notes, two octaves under the keys · REESE, WOBBLE, ACID 303 (the resonant acid line, sliding where notes overlap), FUNK BASS, LP24 BASS (ANALOG) · FM BASS (DIGITAL) · CZ BASS (PHASE) · FAT BASS (TRIO) · WOW BASS (VOICE) · GB BASS (LOFI) · UP BASS, DEEP BASS (SAMPLE: a real upright) · SOLID BASS, SAW BASS (FM6) |
| **Keys** | RHODES, DX RHODES, WURLI, M1 PIANO (the house piano), AFRO KEYS (afro house, amapiano), CLAV (DIGITAL) · GRAND PNO (SAMPLE: a Steinway grand; long notes fade as on the real one), DUSTY PNO, LOFI KEYS (the same grand through an old sampler) · SOFT KEYS (PHASE) · TINE EP, CLAVINET (FM6) |
| **Organ** | SOUL ORGAN, GOSPEL, JAZZ ORGAN, DIRTY B3, HOUSE ORGN (the 90s house organ: bass lines and chords) (WHEEL) · DRAWBARS (FM6) |
| **Pad** | WARM PAD, DARK STR, ATMOS PAD (ANALOG) · SAW PAD (TRIO) · GLASS PAD (DIGITAL) · CZ STRING (PHASE) · LOFI CLOUD, VIBE HAZE (GRAIN) · CHOIR AAH, SOUL OOH (VOICE) · SUPER PAD (ANALOG) · STRINGS, FM GLASS (FM6) |
| **Lead** | SUPERSAW (seven detuned saws: trance, EDM), SUPER LEAD, HOOVER SAW, SYNC SWEEP (hard sync), FIFTH LEAD, G-FUNK LD (ANALOG) · SYNC LEAD, HOOVER (TRIO) · TALKBOX (VOICE) · GAME LEAD (LOFI) · LOFI FLUTE (SAMPLE) · FLUTE DUST (GRAIN) · FM SYNC LD, FLUTE (FM6) |
| **Pluck & bell** | TRAP PLUCK, SUPER PLCK (ANALOG) · RESO PLUCK (PHASE) · PLUGG BELL, TRAP BELL, MUSIC BOX, KALIMBA, MARIMBA (DIGITAL) · VIBES (SAMPLE) · 8BIT ARP (LOFI) · BELLS, FM MARIMBA, FM KALIMBA, STEEL DRUM, TUBULAR, HARP (FM6) |
| **Stab** | MIN STAB, MIN7 STAB, RAVE STAB, DUB CHORD (dub techno, into the delay) (TRIO: one key plays the chord) · SUPER CHRD (trance chords), SYN BRASS (ANALOG) · CZ BRASS (PHASE) · HORN STAB, STRING STB (SAMPLE) · BRASS SECT (FM6) |
| **FX** | SCRATCH — scratch, backspin, rewind across the keys · GM KIT (SAMPLE) |

**ANALOG** is the virtual analogue: two band-limited oscillators (SAW, SQR, TRI, SIN, PWM), noise, drive and a resonant filter. EDIT **OSC**: WAVE, DTN (osc 2's detune, cents), MIX (osc 1 against osc 2), NOIS; EDIT **FLT**: CUT, RES, DRV, KTR (key tracking). On an ANALOG track the EDIT family has three more pages (ANALOG 2; at their defaults the sound is the old ANALOG's):
- **OSC 2**: WAVE2 (osc 2's own wave; =1 follows WAVE), SEMI (osc 2's interval, ±24 semitones; DTN fine-tunes on top), SYNC (hard sync: osc 2 restarts with osc 1, band-limited). With SYNC on, the SHP modulation (ENV DEST SHP, LFO DEST SHP) sweeps osc 2's pitch: the classic sync sweep.
- **SWARM**: SWARM (0–6 detuned copies of osc 1 around it, any wave: the supersaw, one voice per note so chords stay chords; it was the SUPER engine), SDTN (their spread, up to 60 cents for the outer ones), DRFT (a slow random drift of the pitch, osc 2 against osc 1; DRFT above 0 also leaves the oscillators free-running: no restart at each note, as on a real analogue — keep it at 0 for 808s). When more than four voices sound (all tracks together) a swarm keeps four copies, above six two, so the CPU keeps up.
- **FLT 2**: FTYP (LP12, LP24, BP, HP), and the filter's own envelope, FATK / FDEC / FENV (attack, decay, amount ±: restarted by each note; ENV DEST FLT still adds the ADSR).

The filter saturates softly inside its loop (resonant peaks round off instead of clipping) and RES goes further than before: from about 120 it rings, at 126–127 it oscillates by itself — a sine at the cutoff, in tune with KTR at 64 (it needs a little input to start: an oscillator, the noise). The cutoff moves smoothly under fast envelopes (its coefficients are worked out every 16 samples and glide sample by sample in between). SUPER's presets (SUPER LEAD, SUPER PAD, SUPER CHRD, SUPER PLCK, HOOVER SAW) are ANALOG presets now, on the swarm; SUPERSAW plays the swarm too, on one voice. A project from before keeps its sound and gets the new values at their defaults; a track that was on the SUPER engine comes back on ANALOG's swarm (the ANALOG version of its preset, with its copies, spread, drift, cutoff, resonance and filter type), and a DX7 track plays FM6 (its VOICE). A user preset stored on SUPER loads on FM6 (SUPER's number): load the SUPER sound again from PRESETS.

**FM6** is Kerem Kilic's six-operator FM engine from Melodee: DX7 voices played sample for sample as Dexed plays them (Dexed's code after MSFA, restated in fixed-point C; `tests/fm6_parity.sh` renders the same scores through Dexed's own sources and FM6 and finds them equal). EDIT 1: **VOICE** (R01–R16, Melodee's factory voices, then the user bank U01–U32), **MOD** (the modulators' levels), **M.TIM / C.TIM** (the modulators' / carriers' envelope times); EDIT 2: **ENGINE** — Dexed's resolutions: **MARK I** (the DX7's log-sine tables, its feedback loops; the default), **MODERN** (MSFA's 24-bit sine), **OPL**. The operators themselves: ENV on an FM6 track (above). 8 voices a part, as every engine in SLOOP. FM6 renders in fixed point; MARK I and MODERN run in hand-written pi32v2 loops (bit-identical to the C).

**DX7 voices and banks over USB-MIDI.** FM6 takes DX7 SysEx on any channel, for the FM6 track that is selected (else the part of the channel, else the first FM6 part), so Dexed or any DX7 librarian can edit a part live and keep banks:

| SysEx | What FM6 does |
| --- | --- |
| `F0 43 0n 00 01 1B` + 155 bytes + checksum `F7` (a voice) | into the FM6 part's voice (the notes stop, as a program change) |
| `F0 43 0n 09 20 00` + 4096 bytes + checksum `F7` (32 voices) | the user bank U01–U32, saved in flash |
| `F0 43 1n gg pp dd F7` (a voice parameter; 155 = the operator switches) | into the FM6 part's voice |
| `F0 43 1n 08 pp dd F7` (a function: 64 mono, 65 bend range, 66 step, 68 glissando, 69 portamento time, 70–77 controllers) | the part's DX7 functions |
| `F0 43 2n 00 F7` / `F0 43 2n 09 F7` (dump requests) | the part's voice / the user bank, sent back |

The user bank lives in one of the three **USR sample slots** (the one that holds it, else the first never used): it shows there as an empty slot, and **a sample uploaded into that slot replaces the bank**. With samples in all three slots a bank (or STORE) is refused: *FM6 BANK: NO USR SLOT*. The web editor imports .syx voices and banks into its library, auditions a voice on the FM6 track and reads / writes the bank. SLOOP has no MIDI pitch bend, wheel, foot, breath or aftertouch input yet: FM6's controller settings are kept (projects, SysEx) but rest.

The sampled sounds (SAMPLE engine, **SET**: PIANO (a grand), BASS, VIBES, HORNS, STRGS, FLUTE, SCRCH, PERC) are free recordings (CC0: Versilian Studios VSCO-2 CE and VCSL, Sonic Pi), retuned and coloured like a record through an old sampler.

## Drum kits

37 kits — **PRESETS** on the drum track, KNOB 1 on the kit page, or the editor. 1–5 are a sampled acoustic kit (CC0 recordings of a real snare, hi-hat, toms and cymbals) and its treatments; 6–37 are synthesised, so they cost almost no memory. Every synthesised kit has 16 sounds of its own, one per white key — KICK 2 and SNARE 2 are other sounds, not the same one retuned (the long 808 in TRAP, the log drum in AMAPIANO, the rumble in TECHNO). Each sound is built like on the classic machines: a tuned body with a pitch drop and a hold before it fades, a second partial for the drum heads, a click for the attack, noise through a resonant filter, drive. Softer hits are darker as well as quieter. The levels are measured: every kit is as loud as the others, each sound at its place in the mix.

| # | Kit | Style | # | Kit | Style |
| --- | --- | --- | --- | --- | --- |
| 1 | ACOUSTIC | studio | 20 | ELECTRO | electro |
| 2 | DEEP | soft | 21 | DISCO | disco |
| 3 | TIGHT | punchy | 22 | GARAGE | UK garage |
| 4 | BRIGHT | bright | 23 | JUNGLE | drum & bass |
| 5 | DUST | dusty | 24 | DUBSTEP | bass music |
| 6 | 808 | hip hop | 25 | DEMBOW | reggaeton |
| 7 | 909 | house | 26 | AMAPIANO | amapiano (log drum) |
| 8 | 606 | acid | 27 | AFRO | afrobeat |
| 9 | 80S | 80s pop | 28 | LATIN | latin |
| 10 | VINTAGE | rhythm box | 29 | TRIBAL | tribal |
| 11 | TRAP | trap | 30 | SYNTHWV | synthwave |
| 12 | DRILL | UK drill | 31 | CHIP | chiptune |
| 13 | BOOMBAP | hip hop | 32 | ARCADE | video game |
| 14 | LO-FI | lo-fi | 33 | GLITCH | glitch |
| 15 | PHONK | phonk (melodic cowbell) | 34 | INDUSTR | industrial |
| 16 | HOUSE | house | 35 | HYPER | hyperpop |
| 17 | D.HOUSE | deep house | 36 | AMBIENT | ambient |
| 18 | TECHNO | techno | 37 | JAZZ | jazz (brushes) |
| 19 | MINIMAL | minimal | | | |

The kit is saved with projects and song sections. MIDI notes in on the drum channel (10) play the nearest of the 16 sounds.

## Your own samples (USR1–USR3)

Three slots of about 7.4 s each hold your own sounds, played by a synth track: engine **SAMPLE**, **SET** = USR1 / USR2 / USR3. Load them from the web editor, tab **Samples**:

- **Files:** up to 16 WAV per slot (any rate, mono or stereo). The note each one plays at its own speed is in its name (`KEYS_C4.wav`, C4 = 60).
- **CHOP:** open or drop a recording (WAV, MP3, AIFF…) and cut it into up to 16 chops, one per key — live with **TAP** (or the space bar) while it plays (*snap to the hit* puts each tap on its attack), **Find hits**, **Grid** or **Equal parts**; then **Send to USR1/2/3**, or **Download WAVs**.

## Song mode

A song is up to 16 steps of 4 sections, **A–D** (each holds the four tracks: sounds, patterns, kit). Make it live, by playing:

1. Make a loop (the verse). Hold **SAVE** and press the **5th white key** (*save A*). Change the loop (the chorus) and save it into **B** with the 6th key, a bridge into **C**, an end into **D**. Saving over a used section asks for the key again within 3 s.
2. **Play the sections live:** hold SAVE and press white key **1–4**. Playing, the section starts on the next bar, every track from its first step, always in time; stopped, it becomes the loop at once.
3. **Record the song as you play it:** SAVE + key **14** (*rec*): from the next bar, every section you play and how many bars it plays are written into the song. Press it again, or STOP, to end: *SONG PARTS 5*. It is saved by itself once you stop.
4. **Play it back:** SAVE + key **13** switches *loop* / *song*; in song mode **PLAY** plays the whole song and stops at the end (your loop is back afterwards).

The **SONG screen** (SAVE tapped on TRACKS, or SAVE + key 16) shows the chain and edits it by hand: **KNOB 1** the step, **KNOB 2** its section, **KNOB 3** its bars, **KNOB 4** the number of steps; **REC** stores the loop into the step's section; **SAVE** (tap) saves the chain; **OCT−** loop / song; **OCT+ twice** loads a section. The four sections are the four project slots.

## MIDI in: a keyboard on SLOOP

Plug a keyboard or a DAW into USB or the TRS MIDI IN. Channels **1–3** play the synth tracks 1–3, the drum channel (GLO › DRUMS, default **10**) the drums, every other channel the selected track. A note always ends on the track it started on, even if you selected another one meanwhile.

- **The key and chords from the keyboard:** MIDI notes go through the track's **SCL** settings like the FM-1's own keys. **KEYS WHITE**: middle C (note 60) is the root, each white key the next degree of the scale, black keys silent — any scale on the white keys. **ALL**: every key the next degree (note 61 the second, 62 the third…). **SNAP**: every note rounded down into the scale. **CHORD** on: one key plays the chord of its degree (C4 = the I), recorded as a chord. TRANSPOSE applies; the FM-1's OCT buttons do not (the keyboard has its own). With KEYS OFF and no chord, notes play as they come. Change the key while holding notes: they still end cleanly.

- **Pitch bend:** ±2 semitones, smoothed. Another range per channel with RPN 0: CC101 = 0, CC100 = 0, then CC6 = semitones (0–24) and CC38 = cents. It is live only: not saved with a sound or a project, not recorded.
- **Mod wheel (CC1):** a vibrato of its own, 5 Hz, up to ±50 cents; the sound's LFO is untouched.
- **Sustain pedal (CC64):** holds the notes you let go; pedal up releases them (not the keys still down). With ARP on, held notes stay in the arp until pedal up. Sustain lengthens what live recording records.
- **Panic:** CC123 (All Notes Off) releases the channel's notes (the pedal still holds them); CC120 (All Sound Off) silences its tracks at once, pedal or not, drums included (reverb and delay tails ring out); CC121 resets bend, wheel and pedal (the bend range stays). The sequencer keeps running.
- Drums ignore bend, wheel and sustain. A synth track has one bend / wheel: channels that play the same track share it.
- **Is anything coming in?** GLO › SYSTEM, the **USB** column: the USB state (MIDI = connected), and **RX** for a quarter of a second whenever MIDI arrives. Its knob switches the column to **TRS** (ON, RX); both inputs stay on.

Pitch bend, the mod wheel, sustain, panic, the RX light and the scale layouts for MIDI notes come from Melodee (see the credits).

## MIDI clock and sync

**GLO > SYSTEM > SYNC** (saved with the project; also in the web editor under *MIDI CLOCK*):

| SYNC | |
| --- | --- |
| **AUTO** (new projects) | follows an external MIDI clock when one comes in: **TRS first, then USB**, else the FM-1's own tempo |
| **INT** | the FM-1's own tempo, clocks ignored (projects from before the clock keep this) |
| **USB** / **TRS** | follows only that input |

With AUTO the column shows what it follows now: **A:TRS**, **A:USB** or **A:INT** (a column fits five
characters; the web editor writes *AUTO:TRS*). A clock counts as there after four pulses a steady period
apart, and as gone half a second after its last pulse. The source changes only while stopped: playing, SLOOP
never switches away from the clock it follows, even when a preferred one appears (it is taken at the next
Stop). Start or Continue from an input that is not followed yet, while stopped, takes that input at once if
AUTO allows it (a host that sends its clock only while playing). MIDI IN on TRS is on by default.

Following a clock: Start begins at step 1 on the first clock pulse after it (the MIDI downbeat). Continue
resumes at the song position the master sent (in 16ths), or where it stopped. Stop stops. PLAY on the FM-1
starts at the next clock pulse, STOP stops. When the clock followed stops while playing, the sequencer stops
half a second later. The BPM shown, the delay, the arp, the rolls and the SLICER follow the measured tempo.
Notes still come in on both inputs. The song arranger restarts from its beginning on Start; Continue and Song
Position place the pattern steps only.

**Timing.** Sound leaves the FM-1 between 5.8 and 11.6 ms after it is rendered: audio is made in half buffers
of 256 samples (5.8 ms), and a half is rendered while the other half plays. When following, SLOOP renders each
step to leave exactly when its clock pulse arrives:

- every pulse is timestamped within 0.1 ms of its arrival (USB and TRS are both looked at 10,000 times a
  second);
- a tracking filter (a least-squares line through the last pulses, its memory growing from 2 to 250 pulses)
  predicts the next pulses: it locks after two pulses, then smooths jitter about five times; a tempo change
  widens it again, a ramp is tracked as a steady acceleration, a single late pulse is held and dropped, a lost
  pulse is counted;
- a step starts at its own sample inside the 32-sample block (not at the block's start);
- the downbeat after Start (and the step after Continue / Song Position) is rendered ahead, at the time of
  the coming pulse, when the master sends Start a pulse ahead as hosts do. If Start comes too close to its
  pulse, that one step is late by the output latency; the next ones are on time.

Measured in the emulator (exact guest time; the I2S output, before the codec), the onset of a hat on every
beat against its clock pulse (ideal time; TRS: the end of the byte); at 96 MHz (312 MHz: the same within
0.03 ms, but for the late start, +8.4 ms):

| External clock | mean | worst after bar 1 | the first step after Start |
| --- | --- | --- | --- |
| USB, 120 BPM steady | +0.01 ms | 0.01 ms | +0.01 ms |
| TRS, 120 BPM steady | +0.03 ms | 0.03 ms | +0.03 ms |
| USB / TRS, 174 BPM steady | +0.03 ms | 0.05 ms | +0.04 ms |
| USB / TRS, 120 BPM, pulses ±1 ms jitter | +0.04 ms | 0.17 ms | −0.3 ms |
| USB, 174 BPM, ±0.5 ms jitter | +0.05 ms | 0.11 ms | −0.14 ms |
| USB, 100 → 140 BPM ramp over 16 beats | +0.02 ms | 0.06 ms | +0.06 ms |
| USB, 100 → 140 BPM jump at the downbeat | +0.04 ms | 0.04 ms | +0.06 ms |
| TRS, one pulse lost / USB, one pulse 3 ms late | +0.03 / +0.01 ms | 0.03 / 0.01 ms | |
| Start and Continue (Song Position 136) a pulse ahead | | | +0.01 ms (TRS +0.03) |
| Start only 0.1 ms before its pulse | +0.01 ms | 0.01 ms | +6.5 ms (late by the latency) |

The host simulation (`tests/clock_sync_test.c`) adds a 140 → 90 BPM ramp over 8 beats with ±1 ms jitter
(worst 0.9 ms, early) and mid-song jumps; it is a hard case for any follower. The previous version (one
fixed-gain loop, steps at block starts, a start on the pulse) measured 0.35–0.47 ms worst steady, 0.7–0.9 ms
with ±1 ms jitter, and its first step after Start was 8–12 ms late.

Measured in the emulator, a note to the first sample out:

| From | To the first sample out | |
| --- | --- | --- |
| a USB MIDI note-on | 5.9 – 10.6 ms, mean 8.4 ms | depends on where in the playing half buffer it lands |
| a TRS MIDI note-on (end of its last byte) | 5.8 – 10.6 ms, mean 8.1 ms | |
| a key press | 9.3 – 14.2 ms, mean 11.7 ms | + the key scan and debounce (~3.3 ms) |
| a step of the internal sequencer (as rendered) | 5.8 – 11.6 ms | the sequencer is ahead by this much: the PLAY light allows for it |

None of this is measured on a real FM-1 yet: the codec's own delay is not known, so it is not included
(`SYNC_DAC_US` in `clock_sync.c`, 0), and the 10 kHz USB check (one register read a tick) is untried on the
hardware. SLOOP does not send MIDI clock.

## The web editor

Open it from the installer page, or with **`OPEN-EDITOR.bat`** (`http://localhost:8766/webapp/editor/`), in Chrome or Edge with the FM-1 on USB, and press **Connect**. It follows the device live (turn a knob on the FM-1, the editor moves).

- **Sound** — every parameter of the selected track, the engines and presets, files.
- **Sequencer** — the pattern settings and the steps. On the **drum track**: a grid of the 16 sounds × the steps, with the **kit**. Choose a **level** (GHOST, SOFT, NORM, HARD) and a **roll** (x1–x4), then click: a hit; click it again (same level and roll): cleared; Shift+click: one level louder.
- **Tracks** — the four channel strips (level, pan, mute; SOLO and REC shown as on the device).
- **Library**, **Samples** (with CHOP), **Projects**, **Settings** (GLOBAL, **MASTER**: DUST, DUCK, FILT, ROLL; DRUMS).

The protocol is documented in [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md) (v5).

## USB audio (experimental)

> **EXPERIMENTAL, not yet tried on a real FM-1.** Ported from [Melodee](https://github.com/keremimo/melodee) (Kerem Kilic), where it was tested on macOS; in SLOOP it has only run in the FM-1 emulator so far.

On the same USB cable as MIDI, the computer sees two class-compliant audio devices (USB Audio Class 1, no driver):

- **SLOOP In** — four mono inputs, one per track: input 1–3 the synth tracks, input 4 the drums. Each is the track after its insert (DIST, SLICER), LEVEL and MUTE / SOLO, *before* pan, the chorus / delay / reverb sends, DUST / DUCK / FILT, the punch-in effects and MASTER: dry stems for the DAW. The shared effects stay in what you hear from the FM-1. A stem at full scale clips: turn the track's LEVEL down.
- **SLOOP Out** — stereo playback from the computer, mixed into the FM-1's output after the master effects and scaled by MASTER. It is never sent back to SLOOP In (no loop when the DAW monitors its input).

Both run at **44.1 kHz** (the FM-1's own rate, no conversion); choose **16 or 24 bit** in the computer's audio settings (on macOS: Audio MIDI Setup). 24 bit adds no precision (the synth is 16 bit inside). For a DAW that wants one device for both directions, make an aggregate device (macOS: Audio MIDI Setup, +, Create Aggregate Device) with drift correction on the one that is not the clock. Avoid saving or loading presets and projects while recording: a flash write pauses the audio.

USB audio replaces the USB serial console (they share the USB endpoints); MIDI, the web editor and updates work as before. A build without it: `FELUCCA_USB_AUDIO=0` (see BUILDING.md).

## Sound design pages

The full Felucca engine is underneath: nine synthesis engines (analog — with SLOOP's osc 2 interval and hard sync, a filter envelope, filter modes, self-oscillation and a supersaw swarm —, 4-op FM, phase distortion, lo-fi chip, sampler, formant voice, three-oscillator, tonewheel organ, granular) plus FM6 (Melodee's six-operator FM that plays DX7 voices as Dexed does, engine 9), envelopes (with a pitch punch for the 808s), LFO, arpeggiator, scales and chords, glide and voice modes, per-track drive and slicer, chorus / delay / reverb sends (a stereo chorus, a tempo delay, a stereo reverb built as a feedback delay network: dense, no metallic ring), 32 user presets, 4 projects.

## Specifications

| | |
| --- | --- |
| Tracks | 3 synth parts (8 voices shared) + drums (16 sounds, 6 voices) |
| Sounds | 92 presets on 10 engines (browsed by kind, level-matched), 8 sampled sets (CC0), 3 slots for your own samples |
| Sequencer | 64 steps per track, own length and division each; chords with a level and ratchet per note; drums with a level and ratchet per sound; ties, slide; MPC swing 50–75 %; one sample-accurate clock for steps, arp, rolls, slicer and song (no drift) |
| Performance | layers (hold a button: keys and knobs change job): punch-in FX, erase, note repeat, step entry, key / chords, mute / solo / tap tempo |
| Drum kits | 37 (5 sampled, 32 synthesised, 16 sounds each) |
| Effects | 16 punch-in effects; master DUST, DUCK, DJ filter; per track drive, slicer, sends to a stereo chorus, a tempo delay and a stereo reverb; master limiter |
| Recording | live, quantised as heard (latency-compensated), overdub; records at once while playing; free take sets loop length and tempo |
| Memory | undo / redo, 4 projects, 32 user presets, autosave of the working project, song of 4 sections × 16 steps × 1–64 bars |
| Audio | 44.1 kHz, fixed-point DSP |
| MIDI | USB class-compliant in / out, TRS MIDI in; channels 1–3 the synths, 10 the drums; pitch bend (RPN 0 range), mod wheel, sustain, CC120 / 121 / 123; MIDI clock in (USB or TRS: start / stop / continue / song position, latency-compensated) |
| USB audio (experimental) | class-compliant (UAC1), 44.1 kHz, 16 / 24 bit: 4 mono track inputs to the computer, stereo playback into the FM-1 |
| Update | over USB from the browser (package SHA-256 and CRC checked) |

## Rescue, going back, credits

- **USB rescue:** hold **OCT−** alone while switching on (*SLOOP USB RESCUE*), then install again.
- **Interrupted install:** the FM-1 stays in update mode; press Install again and it finishes. A damaged package is refused, and the FM-1 keeps waiting for a good one.
- **Back to the official firmware:** M-VAVE's updater, M-UPGRADE, and the FM-1 firmware from m-vave.com.
- **Credits:** SLOOP is based on Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments — engines, sequencer, editor and installer. USB audio from Melodee by Kerem Kilic (Ellic Studio), GPL-3.0. Font: Terminus (SIL OFL 1.1). Samples: Versilian Studios VSCO-2 CE and VCSL, Sonic Pi (all CC0). PHASE: CrispyZebra (GPL). VOICE after klattsch (MIT). MIDI expression, MIDI notes through the scales, the KEYS ALL layout, step note length and the encoder first-click fix: ported from Melodee (keremimo/melodee, GPL-3.0) by Kerem Kilic and ChanceTheMaker. TRS MIDI input and the clock-follow design after Melodee by Kerem Kilic (GPL-3.0). Interface ideas after teenage engineering's pocket operators and EP-133, Elektron's step entry and Akai's MPC (swing, note repeat, erase) — SLOOP is not affiliated with any of them.
- **Licence:** GPL-3.0, no warranty. M-VAVE and FM-1 are trademarks of their owners; SLOOP is not affiliated with them. Drum kit names describe styles; they do not refer to any product.

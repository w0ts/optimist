# Optimist 0.1

*Optimist is the FM-1 firmware formerly built as SLOOP-plus: SLOOP with ANALOG 2, FM6, the drum synth and sounds, MIDI clock, USB audio, the firmware builder (docs/BUILDER.md) and more. This manual still says SLOOP in places where it describes SLOOP's design.*

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
- **Undo / redo** (EDIT + OCT− / OCT+; many levels, see [Undo](#undo-clear-save-autosave)), **hold REC to clear**, and an **autosave** that brings your beat back at power-on.
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
16. [Snapshots](#snapshots)
17. [MIDI in: a keyboard on SLOOP](#midi-in-a-keyboard-on-sloop)
18. [MIDI clock and sync](#midi-clock-and-sync)
19. [The web editor](#the-web-editor)
20. [USB audio (experimental)](#usb-audio-experimental)
21. [Sound design pages](#sound-design-pages)
22. [Optional features (build switches)](#optional-features-build-switches)
23. [Specifications](#specifications)
24. [Rescue, going back, credits](#rescue-going-back-credits)

---

## Install

1. Build the firmware (BUILDING.md), make the local site (`python3 web/make_site.py build/felucca.fwsc dev build/optimist-site`) and serve it (`python3 -m http.server 8766 --bind 127.0.0.1 --directory build/optimist-site`), then open `http://localhost:8766/webapp/installer/`. Or use the hosted installer of the project's GitHub Pages site, if it is up.
2. In **Chrome or Edge**, connect the FM-1 to the computer by USB (a data cable, directly — no hub).
3. Press **INSTALL**, allow MIDI access, and wait for *Done*. Keep the page and the server running until then.

The FM-1 restarts into Optimist: the boot screen (the logo, the version) for about a second, then the UI. The editor is at `http://localhost:8766/webapp/editor/` on the same site.

## Sixty seconds to a beat

1. **ALGORITHM** to track **4** (orange, drums). The white keys play 16 sounds: **F3 kick**, G3 kick 2, A3 snare, B3 clap, **C4 closed hat**, D4 open hat… **PRESETS** picks a kit: try *808* or *BOOMBAP*.
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
| **EDIT** — *erase* | erase that sound / note from the pattern (drums, on a SOUND page: pick the sound to edit) | SHIFT · LENGTH ×2 / ½ · TRANSPOSE · — | EDIT pages (drums: the [SOUND pages](#edit-a-drum-sound-the-sound-pages)) |
| **ARP** — *roll* | note repeat on the grid | RATE · — · — · — | ARP pages |
| **SEQ** — *steps* | steps 1–16 of the page | SOUND / NOTE · DIV · SWING · LENGTH | SEQ pages (drums: grid / kit) |
| **SCL** — *key* | the key of the song | CHORD · SCALE · KEYS · TRANSPOSE | SCL pages |
| **GLO** — *mix* | 1–4 mute · 5–8 solo · 9–12 FX on / off (with FILLS: 9 fill, 10 fill next bar; FX on / off on black keys 1–4) · 16 tap tempo | level of tracks 1 · 2 · 3 · 4 | GLO pages |
| **SAVE** — *song* | 1–4 play a section of the bank (A–D, E–H…; next bar) · 5–8 save the loop into it · OCT− / OCT+ the bank · 13 loop / song · 14 SONG REC · 16 the chain | — | TRACKS: the SONG screen · else the SAVE pages |
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
| F3 | kick | C4 | closed hat | G4 | snare 2 | D5 | ride |
| G3 | kick 2 | D4 | open hat | A4 | low tom | E5 | shaker |
| A3 | snare | E4 | pedal hat | B4 | hi tom | F5 | conga |
| B3 | clap | F4 | rim | C5 | crash | G5 | cowbell |

**Each sound can be edited** (tune, decay, snap, click, pitch drop, cut, drive, level), play one of your samples or another kit's sound for its key, and a whole set of them can be saved as **your own kit**: see [Edit a drum sound](#edit-a-drum-sound-the-sound-pages).

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
- **OCT− undo · OCT+ redo**, many levels (the knob turns of one hold count as one change; see [Undo](#undo-clear-save-autosave)).

### ARP — roll (note repeat)

Hold ARP and hold a key: it repeats on the grid at the **RATE** of KNOB 1 — 1/8, 1/16, 1/32, 32T, 1/64 — locked to the tempo and the swing, so it always lands in time. On the drum track OCT− / OCT+ make it ghost / hard. While recording, a roll is written as ratchets (a 1/32 roll on a 1/16 track: x2 on each step). Rolls end with their key.

### SEQ — steps (step sequencer)

The 16 white keys are the 16 steps of the page; the lit ones play. The first four black keys (F#3, G#3, A#3, C#4) or **OCT− / OCT+** pick page 1–4 (steps 1–16, 17–32, 33–48, 49–64, up to the track's LENGTH).

- **An empty step:** press its key — it is set at once. Drums: with the sound shown (KNOB 1 picks it, or the last pad you hit); synths: with the note or chord you played last.
- **A set step:** press and let go — it is cleared. Hold it and turn a knob instead — it is edited, and kept: **KNOB 1** sound (drums) / note (synths), **KNOB 2 LEVEL** (ghost, soft, norm, hard), **KNOB 3 RATCHET** (x1–x4), **KNOB 4 LENGTH** (synths: how many steps the note lasts, 1–16 shown, written as ties — longer through empty steps up to the next note, shorter clears its own ties; the tiles show the ties as --). Hold several step keys to edit them together.
- **No step held:** KNOB 1 the sound / note to set · KNOB 2 **DIV** (1/4 … 1/32, triplets) · KNOB 3 **SWING** of the track (the straight DIVs; none on the triplets, as SLOOP 2.4) · KNOB 4 **LENGTH** (1–64 steps; each track loops on its own length, polymeters stay in phase).

#### Drum steps on the keys (`FELUCCA_DRUM_STEP`, off by default)

As in SLOOP 2.4 ("Drums with the keys"). On the **DRUMS grid page** (SEQ tapped on TRACKS with the drum track) the 16 white keys are the 16 steps of the sound **KNOB 1** picks: press a key to set its step (you hear the sound), press it again to clear it; the keys light that sound's steps (the playhead blinks). The first four black keys pick the page of steps (1–16 … 49–64, up to the LENGTH). You hear what you pick: the sound when KNOB 1 changes it (on the grid and in the SEQ layer, also with a step held), the step's sounds at their levels when KNOB 2 moves to it. **SELECT** switches grid and kit (it is the tempo there when the switch is off); the kit page's keys play the pads. The SEQ layer (SEQ held) is unchanged: SEQ + step + KNOB 2 / 3 level / ratchet.

Ours: **FOLLOW**. While playing, the page follows the playhead (the grid's page, the SEQ layer's page); a page key turns it off until the next stop; black key 5 (D#3) turns it on or off. The grid shows *page n/m* and a bar under it while it follows. The data is the project's own: nothing new is saved.

After SLOOP 2.4 by isod89 (GPL-3.0); the idea first came from PR #45 of isod89/sloop-fm1 by Erick Buendia Barrientos (Erbubar23).

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
- Keys **9–12 turn the effects of tracks 1–4 off and on** (FX bypass): the track plays dry — no DIST, no SLICER, no chorus / delay / reverb send (the drum track: its sounds' sends) — and its FX values stay as they are, so turning it back on brings the sound back. The tiles read *fx n* (on) or *dry n*; the TRACKS row says *dry*, the FX page *FX OFF*. The master effects (DUST, DUCK, the filter, the punch-in FX) stay on the whole mix. Saved with the project (projects of SLOOP 2.0 .. 2.2 load with every FX on).
- The last white key (**G5**): **tap tempo** (two taps or more).
- **KNOB 1–4: the levels** of tracks 1–4.

## Undo, clear, save, autosave

- **Undo / redo:** hold EDIT, press OCT− / OCT+. **Many levels**, on every track (the drum track too), in the order you made them: each recording pass, erase, clear, step entry, held SEQ / EDIT layer (its knob turns and steps count as one change), free take or NEW is one level. The top bar says where you are and what changed: *UNDO 3/5 TRACK 2* (three changes still in, of five), *REDO 4/5 DRUMS*; *NOTHING TO UNDO* at the start. Redo stays until you change something new. Works while playing: the steps swap between two audio blocks.
- **How many levels:** as many as the memory left over in your build holds — the history keeps only the steps a change touched (plus LEN / DIV), so a one-step edit costs 15 bytes and a recording pass that changed 16 steps about 180. How much that is depends on what else is built in: ~58 KB with the 0.74 s delay line (thousands of small edits, or some 300 full passes), ~5.6 KB when only main RAM is left (~30 full passes). When it is full the oldest levels are forgotten.
- **Loading a project** (or a song section playing) **clears the history**: those are other patterns. NEW is a level you can undo (all four tracks at once).
- **Clear a track:** hold REC. After 0.7 s the press is cancelled and a ring fills; keep holding ~1.3 s more and the selected track is cleared (*TRACK 2 CLEARED*). Let go before: nothing. Undo brings it back.
- **Save:** SAVE + keys 5–8 save the loop into section / project A–D (= SLOT 1–4); SAVE → PROJECT has SLOT, LOAD, SAVE too.
- **Autosave:** when the transport is stopped and you have not touched anything for 2.5 s (at most every 20 s), the working project is kept in flash; at power-on SLOOP comes back exactly as you left it.
- **New project:** SAVE → TOOLS → NEW (turn to GO): the four tracks back to their power-on sounds, empty patterns (undoable).
- **Missing in this build:** a firmware made with the builder (docs/BUILDER.md) may leave out engines, drum kits, sample sets or effects. A project, song section, user preset or user kit that uses one still loads: that part plays a stand-in (PHYS plays ANALOG, the 909 kit the first kit built) and keeps its settings, so it sounds as before on a full build. The top bar says what is missing, e.g. *MISSING: PHYS T2, KIT 909 +2* (T2: track 2; +2: two more), for about 2.5 s. Each item is said once until power-off, so a song that changes sections while playing never repeats it; it never stops the sound. **SAVE → TOOLS → MISS** shows how many items the current project misses; turn it to see them one by one (*MISSING 2/3: USR2 EMPTY*). What is named: engines, drum kits (the track's and another kit on a key), sample sets left out, USR slots a sound or a drum key plays that are empty, FM6 ENGINE modes left out, and the settings of a feature left out that you would hear: DELAY / REVERB / CHORUS sends, DIST, SLICER, the master's FILT / DUST / DUCK, the drum keys' SOUND EDIT, samples (LANE SMP), kits (LANE KIT) and sends (LANE FX), per-step CHANCE. A user preset of an engine left out stays in the bank and says *MISSING: PHYS* instead of loading. Not named: motion recordings, the spring reverb's TYPE. A build without reverb names REVERB once after power-on for almost every project (the factory sounds send to it). Build switch `MISSING_WARN` (on; about 1.1 KB of flash).

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
- **DRUMS** (SEQ tapped on TRACKS with the drum track) — **grid**: the 16 sounds × 16 steps, levels as shades, ratchets as notches; dials *sound · step · hit · level*. **kit**: 16 pads that flash on every hit; dials *kit · level · reverb · pan* (reverb: the REV of the sound picked on the grid; your own kits after the 37). SEQ tapped switches grid ↔ kit; EDIT opens the SOUND pages.
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

**ANALOG** is the virtual analogue: two band-limited oscillators (SAW, SQR, TRI, SIN, PWM), noise, drive and a resonant filter. EDIT **OSC**: WAVE, DTN (osc 2's detune, cents), MIX (osc 1 against osc 2), NOIS; EDIT **FLT**: CUT, RES, DRV, KTR (key tracking). On an ANALOG track the EDIT family has three more pages and the ENV family two (ANALOG 2; at their defaults the sound is the old ANALOG's):
- **OSC 2**: WAVE2 (osc 2's own wave; =1 follows WAVE), SEMI (osc 2's interval, ±24 semitones; DTN fine-tunes on top), SYNC (hard sync: osc 2 restarts with osc 1, band-limited). With SYNC on, the SHP modulation (ENV DEST SHP, LFO DEST SHP) sweeps osc 2's pitch: the classic sync sweep.
- **SWARM**: SWARM (0–6 detuned copies of osc 1 around it, any wave: the supersaw, one voice per note so chords stay chords; it was the SUPER engine), SDTN (their spread, up to 60 cents for the outer ones), DRFT (a slow random drift of the pitch, osc 2 against osc 1; DRFT above 0 also leaves the oscillators free-running: no restart at each note, as on a real analogue — keep it at 0 for 808s). When more than four voices sound (all tracks together) a swarm keeps four copies, above six two, so the CPU keeps up.
- **FLT 2**: FTYP (LP12, LP24, BP, HP).

On an ANALOG track the ENV family has a second envelope, **ENV2**, after ENV1 and ENV1 DEST (one VIEW ALL page of four rows):
- **ENV2**: ATK2, DEC2, SUS2, REL2: an ADSR of its own, restarted by each note (REL2 at 0 takes DEC2's time). At SUS2 0 it is the filter envelope ANALOG 2 had.
- **ENV2 DEST**: how much ENV2 moves each destination, ± and any of them together, as ENV1 DEST: **FLT** (the cutoff), **PIT** (both oscillators and the swarm, up to ±31.5 semitones), **SHP** (the pulse width; with SYNC the sync sweep), **OSC2** (osc 2's pitch alone, ±31.5 semitones). The fifth destination, the swarm's spread, is **ENV2** on the SWARM page (±63). With every amount at 0, ENV2 costs nothing more than its level.

Projects, song sections and user presets saved with the single destination of before (DST2 and its AMT2) load with that amount on that destination and the others at 0: the same sound.

The filter saturates softly inside its loop (resonant peaks round off instead of clipping) and RES goes further than before: from about 120 it rings, at 126–127 it oscillates by itself — a sine at the cutoff, in tune with KTR at 64 (it needs a little input to start: an oscillator, the noise). The cutoff moves smoothly under fast envelopes (its coefficients are worked out every 16 samples and glide sample by sample in between). SUPER's presets (SUPER LEAD, SUPER PAD, SUPER CHRD, SUPER PLCK, HOOVER SAW) are ANALOG presets now, on the swarm; SUPERSAW plays the swarm too, on one voice. A project from before keeps its sound and gets the new values at their defaults; a track that was on the SUPER engine comes back on ANALOG's swarm (the ANALOG version of its preset, with its copies, spread, drift, cutoff, resonance and filter type), and a DX7 track plays FM6 (its VOICE). User presets are converted the same way when they load: one stored on SUPER comes back on ANALOG's swarm, one stored on DX7 on FM6.

**FM6** is Kerem Kilic's six-operator FM engine from Melodee: DX7 voices played sample for sample as Dexed plays them (Dexed's code after MSFA, restated in fixed-point C; `tests/fm6_parity.sh` renders the same scores through Dexed's own sources and FM6 and finds them equal). EDIT 1: **VOICE** (R01–R16, Melodee's factory voices, then the user bank U01–U32), **MOD** (the modulators' levels), **M.TIM / C.TIM** (the modulators' / carriers' envelope times); EDIT 2: **ENGINE** — Dexed's resolutions: **MARK I** (the DX7's log-sine tables, its feedback loops; the default), **MODERN** (MSFA's 24-bit sine), **OPL**. The operators themselves: ENV on an FM6 track (above). 8 voices a part, as every engine in SLOOP. FM6 renders in fixed point; MARK I and MODERN run in hand-written pi32v2 loops (bit-identical to the C).

**DX7 voices and banks over USB-MIDI.** FM6 takes DX7 SysEx on any channel, for the FM6 track that is selected (else the part of the channel, else the first FM6 part), so Dexed or any DX7 librarian can edit a part live and keep banks:

| SysEx | What FM6 does |
| --- | --- |
| `F0 43 0n 00 01 1B` + 155 bytes + checksum `F7` (a voice) | into the FM6 part's voice (the notes stop, as a program change) |
| `F0 43 0n 09 20 00` + 4096 bytes + checksum `F7` (32 voices) | the user bank U01–U32, saved in flash |
| `F0 43 1n gg pp dd F7` (a voice parameter; 155 = the operator switches) | into the FM6 part's voice |
| `F0 43 1n 08 pp dd F7` (a function: 64 mono, 65 bend range, 66 step, 68 glissando, 69 portamento time, 70–77 controllers) | the part's DX7 functions |
| `F0 43 2n 00 F7` / `F0 43 2n 09 F7` (dump requests) | the part's voice / the user bank, sent back |

The user bank has its own place in the flash, next to [your own drum kits](#your-own-kits): it never takes a USR sample slot, and samples never replace it. (Older builds kept it in a free USR slot; the first start of this one moves it to its place and frees that slot.) The web editor imports .syx voices and banks into its library, auditions a voice on the FM6 track and reads / writes the bank. SLOOP has no MIDI pitch bend, wheel, foot, breath or aftertouch input yet: FM6's controller settings are kept (projects, SysEx) but rest.

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

### X0X 909 and X0X 808 (builder options)

Two more kits can be built in with the firmware builder (Drums → *X0X 909 kit*, *X0X 808 kit*; off by default,
EXPERIMENTAL: float DSP, tried in the emulator only): **X0X 909** and **X0X 808**, the circuit-modelled TR-909 and
TR-808 of X0X by Charles Vestal (from 9W9 / 8W8 by athousanddetails and ER-99 by Matthew Cieplak; GPL-3.0). They
come after JAZZ in the kit list (kits 38 and 39), each followed by its **style kits**, the same voices with their own
settings: **X9 TECH** (punchy driven kick, tight hats), **X9 HOUSE** (rounder kick, loose hats), **X9 UKG** (short
high kick, tight bright snare and rim), **X9 ACID** (everything driven); **X8 TRAP** (long low boom, tight hats),
**X8 BOOM** (the longest driven kick), **X8 ELEC** (clicky kick, snappy snare, loud clap and cowbell), **X8 MIAMI**
(deep bass kick, tight hats). They are listed only with their machine built; your SOUND edits add to theirs.

- **909:** KICK, SNARE, CLAP, RIM and the toms are the models; CLOSED HAT, OPEN HAT, PEDAL HAT, CRASH and RIDE play ER-99's
  909 samples. KICK 2 is a longer kick, PEDAL HAT a shorter closed hat, SNARE 2 a brighter snare. The 909 has no
  shaker, conga or cowbell: those keys play the synthesised 909 kit's. Its ride and crash samples are a builder
  choice: 8-bit (as before), 6-bit (−22 KB, a little grainier: 30 dB from the source instead of 42) or none (−93 KB;
  CRASH and RIDE then play the synthesised 909's).
- **808:** all 16 keys are the 808's own sounds — KICK 2 the long boom, SNARE 2 brighter, CRASH and RIDE its
  cymbal (RIDE shorter and higher), SHAKER its maracas, CONGA its mid conga, COWBELL its cowbell. MIDI also
  reaches the mid tom, the low and high congas and the claves (note 75).
- **On any key of any kit:** SOURCE → SRC also lists every voice of the machines built, after the kits: **X9 BD** …
  **X9 RD** (the 909's 11; CR and RD with its cymbal samples) and **X8 BD** … **X8 CY** (the 808's 16, the mid tom,
  the congas and the claves too). So a key of the synthesised 909, or of your own kit, can play the 808's kick and
  another the 909's open hat; the SOUND pages then set that voice's controls, as in its kit. User kits and projects
  keep it; a build without that machine plays the synthesised 909 / 808's sound for the key, and MISSING names it
  (KIT X0X 808). Only the voices in use run: in the emulator the synthesised 909 kit with KICK and CLAP on the
  808's and the hats on the 909's costs 1,268 instructions a sample of drum code against 1,037 for the kit alone
  (fine at 360 MHz; at 96 MHz it runs some halves late, 80 % load).
- One hit per sound at a time, as on the machines: a sound hit again restarts; the closed hat cuts the open one.
- **CPU:** in the emulator at 96 MHz each kit's drum groove alone plays with no late half (the 909 at 70 % of the
  audio time, the 808 at 72 %; our synthesised kits 54–59 %). With three ANALOG SUPER PAD parts on top they
  overload at 96 MHz, as the synthesised kits do; at the firmware's 360 MHz all of it is fine.
- **The SOUND pages** set the model's own controls: TUNE (on the 909 kick: its TUNE knob, the pitch sweep; on the
  808 toms and congas: 1/12 semitone a step, their range), DECAY (the 909 snare: its TONE, the noise; the clap:
  its tail), SNAP (the snares' SNAPPY), CLICK (the attack of the kicks and 909 toms, the maracas), DRIVE, and CUT
  (the 808 kick: its TONE; elsewhere CUT − is a low-pass). BEND is not shown. LEVEL, the sends, user samples and
  other kits' sounds work as on every kit. Velocity: a normal step is X0X's normal hit, HARD its accent.
- A project or kit using them on a build without them plays the synthesised 909 / 808 instead, and keeps the
  kit: back on a build with them, it plays as before.

### Edit a drum sound (the SOUND pages)

On the drum track, **tap EDIT**: the **SOUND** pages of one of the 16 sounds — the one you played last (a key, MIDI, a roll), or the one you pick with **EDIT held + its key** (the key then neither plays nor erases; the screen says *sound · pick a sound*). Tap EDIT again for the next page. The edits are **offsets from the kit's sound**: 0 is the kit as it is, so another kit stays musical with them. They apply **from the next hit** (a hit that is sounding keeps its sound).

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| **SOUND** | TUNE (±24 st) | DECAY (shorter / longer) | SNAP (noise ↔ tone) | CLICK (the attack) |
| **SOUND 2** | BEND (the pitch drop, ±24 st) | CUT (darker / its filter opens) | DRIVE | LEVEL (−24..+6 dB) |
| **SOUND 3** | REV (0–31, 4 as it is) | DLY (0–31) | CHO (0–31) | — |
| **SOURCE** | SRC: KIT, USR1–USR3, any kit's sound for this key, or an X0X voice (X9 BD … X8 CY, with those kits built) | HIT (a user sample's zone) | START | LEN |
| **KIT** | SLOT (your kits 1–16) | SAVE | ERASE | RESET (this sound back to the kit's) |

**SOUND 3: each sound's sends.** REV, DLY and CHO send that sound into the reverb, the tempo delay and the chorus: 16 sounds × 3 knobs, the drums' only sends (there is no drum-track send on top; GLO → DRUMS has CH and LVL). A sound as its kit has it sends REV **4** (as much as the old DRUMS REV default, 16 %) and no delay or chorus. For **reverb on the snare only**, turn every other sound's REV to 0. 31 is as much as a synth track's REV at 127. The sends follow the knob at once (the sound's other values: from its next hit). The FX bypass (GLO + key 12) leaves every sound dry. With the drum track's SLICER on, the sends are taken after it when every sound sends the same (the same REV, no DLY / CHO), else before it (the reverb and the echoes hear the hits unsliced). DRIVE and CUT (SOUND 2) are each sound's own inserts (DRIVE is its distortion, as a synth track's DST). Projects from before (with GLO → DRUMS REV) load with every sound that followed it at its value (the nearest of the 32 levels: the default 16 exactly, so they sound the same); a sound with its own REV keeps it. A user kit saved before loads such sounds at REV 4.

A sampled sound (the ACOUSTIC kits, your samples) has TUNE, DECAY, CUT and LEVEL; the others are not shown. Under the values: the sound's name and source (with its kind: **SYN** the drum synth, **SMP** a sampled kit, **X0X** a model), its level over its length (the time on the right) and its pitch drop in white. **VIEW ALL** shows the pages as rows, **VIEW PAGE** one page with a bigger graph. SAVE, ERASE and RESET act on the second detent (*AGAIN*). The edits, the sends, the sources and the samples on the keys are saved with the project and its song sections, in a record of their own next to the project (projects of older SLOOP versions load with every sound as its kit, the sends at TRK / 0).

### Your samples on the drum keys

Any of the 16 keys can play one of your samples instead of the kit's sound: SOURCE → **SRC** USR1, USR2 or USR3, **HIT** the zone of that slot (a slot holds up to 16: one uploaded recording cut into hits with CHOP, or 16 files), **START** and **LEN** a part of it (in 1/1024 of the hit; turning the knob moves in steps of 1/128). TUNE, DECAY, CUT and LEVEL apply to it. The web editor's **Drum sounds** tab uploads up to 16 WAV hits into a slot and spreads them over the keys in one go.

### Your own kits

Build a kit key by key — a kit's sound, edited or not, its sends, another kit's sound for that key, your samples — then KIT → **SLOT** and **SAVE** (twice): it is stored as **KIT n** in a bank of 16 in the FM-1's data flash (kits have numbers, not names: KIT 1 to KIT 16). Your kits come **after the 37 kits** when you turn PRESETS on the drum track (or KNOB 1 on the DRUMS kit page): choosing one loads all its keys into the project; choosing a built-in kit again plays that kit as it is. A project keeps the kit it loaded even if you erase or change it in the bank. The editor exports a kit with the samples it plays as one file, and imports it on another FM-1. (The bank lives with the FM6 user bank in the last 16 KiB of the USR3 area: USR3 holds 64 KiB, about 5.9 s.)

## Your own samples (USR1–USR3)

Three slots: USR1 and USR2 about 7.4 s each, USR3 about 5.9 s (its last 16 KiB hold the FM6 user bank and [your own drum kits](#your-own-kits); with [snapshots](#snapshots), 4 slots, USR3 is about 3 s) hold your own sounds, played by a synth track (engine **SAMPLE**, **SET** = USR1 / USR2 / USR3) or by [any drum key](#your-samples-on-the-drum-keys). Load them from the web editor, tab **Samples**:

- **Files:** up to 16 WAV per slot (any rate, mono or stereo). The note each one plays at its own speed is in its name (`KEYS_C4.wav`, C4 = 60).
- **CHOP:** open or drop a recording (WAV, MP3, AIFF…) and cut it into up to 16 chops, one per key — live with **TAP** (or the space bar) while it plays (*snap to the hit* puts each tap on its attack), **Find hits**, **Grid** or **Equal parts**; then **Send to USR1/2/3**, or **Download WAVs**. A recording of any length works (SLOOP 2.3): tick the chops to keep and untick the rest (or **K**), shorten any chop (its length slider, or the handle at the bottom of the wave), or press **Fit to slot** to shorten the longest ones just enough; only the kept chops go to the slot or the WAVs, on consecutive keys.

## Song mode

A song is up to 64 steps of 16 sections, **A–P**, in four banks of four (each section holds the four tracks: sounds, patterns, kit). Hold **SAVE** and press **OCT− / OCT+** to pick the bank (A–D, E–H, I–L, M–P); keys 1–4 and 5–8 below work within it. The sections are stored compressed: the song layer's title shows how full the memory is (*mem 34% +12*: the share used, and how many more sections like the last one fit). When it is full, **MEM FULL** refuses a new section; the playing one can always be saved again, and clearing one always works. Each section is stored in one piece, so large ones (four full 64-step tracks) can fill it before 100 %: the title then says *full* early. A build made with 4 sections (, docs/BUILDER.md) has A–D and 16 steps, as before. Make it live, by playing:

1. Make a loop (the verse). Hold **SAVE** and press the **5th white key** (*save A*). Change the loop (the chorus) and save it into **B** with the 6th key, a bridge into **C**, an end into **D**. Saving over a used section asks for the key again within 3 s (*AGAIN: A*, its *save* tile red): let go of the key and press it again, SAVE still held or held again.
2. **Play the sections live:** hold SAVE and press white key **1–4**. Playing, the section starts on the next bar, every track from its first step, always in time; stopped, it becomes the loop at once.
3. **Record the song as you play it:** SAVE + key **14** (*rec*): from the next bar, every section you play and how many bars it plays are written into the song. Press it again, or STOP, to end: *SONG PARTS 5*. It is saved by itself once you stop.
4. **Play it back:** SAVE + key **13** switches *loop* / *song*; in song mode **PLAY** plays the whole song and stops at the end (your loop is back afterwards).

The **SONG screen** (SAVE tapped on TRACKS, or SAVE + key 16) shows the chain and edits it by hand: **KNOB 1** the step, **KNOB 2** its section, **KNOB 3** its bars, **KNOB 4** the number of steps; **REC** stores the loop into the step's section; **SAVE** (tap) saves the chain; **OCT−** loop / song; **OCT+ twice** loads a section. The first start after the update moves your four project slots into A–D.

## Snapshots

A snapshot keeps **everything in the current work** — the working project (four tracks: sounds, patterns, mix, FX,
the drum kit and its sounds, motion, tempo, swing), **every section** and **the song** — in one slot, and brings it
all back at once. Four slots (a build may have 2 or 8, docs/BUILDER.md), plus **B: BEFORE LOAD**, the state just
before your last load. **SAVE → SNAPSHOT** (after PROJECT):

- **KNOB 1** the slot: the list shows each slot's name (made from the work: *120 ABCD* is 120 BPM with sections
  A–D), its size and a dot: **green** saved, **amber** made by another build of the firmware (it loads; MISSING
  says what this build lacks), **red** damaged (it cannot load; CLEAR it). The bottom line is the free room, red
  when the work as it is now would not fit.
- **KNOB 4 SAVE**, **KNOB 2 LOAD**, **KNOB 3 CLEAR**: one detent arms (*AGAIN: SAVE*), a second within 1.5 s does
  it. The message is green when done (*SNAP 2 SAVED*), amber for a notice, red for an error (*SNAPSHOTS FULL*).
- **Stopped and quiet only**, like autosave: a flash write stops the sound for a moment. Playing: *STOP FIRST*; a
  note still ringing: *WAIT FOR SILENCE*.
- **LOAD replaces** the work, the sections and the song. Before, the state is saved to **B**, so a load is never a
  loss: load B to go back (that swaps: B then holds what you just left). If the slots are full, an older B gives
  way; if even then there is no room: *SNAPSHOTS FULL*, clear a slot.
- Your **samples, user presets, kits and FM6 bank are not copied** (they stay as they are, shared by all
  snapshots). If a snapshot uses a sample slot that is now empty, the top bar says *MISSING: USR2 T1*.
- Sections a build does not have (I–P on an 8-section build): kept, not played (*LOADED: I-P NOT IN BUILD*); a
  4-section build loads A–D (*LOADED: E-P SKIPPED*).
- **The web editor** (Projects screen, Snapshots) lists them, saves, loads, renames and clears, and **exports a
  snapshot to a file / imports one into a slot**: on the computer they are unlimited. The **Backup** keeps them too.
- The snapshots take the end of USR3: with 4 slots USR3 holds 32 KiB (about 3 s) instead of 64 KiB (about 6 s). A sample
  uploaded to USR3 by a firmware without snapshots that is longer than that is never overwritten: SAVE says *USR3
  SAMPLE IN THE WAY* until you erase or reload USR3.

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

Open it from the installer page, or at `/webapp/editor/` of the same site (`http://localhost:8766/webapp/editor/` locally), in Chrome or Edge with the FM-1 on USB, and press **Connect**. It follows the device live (turn a knob on the FM-1, the editor moves).

It is laid out like a DAW: a **transport bar** on top (Connect, the sync light: green live, amber polling; **Play / Stop**, the **BPM** and the section playing; the screens; the **theme**, the FM-1 colour editions Classic, Black, Lilac, Orange, Mint, Cream, Blue or plain black and white), and the **mixer** as home: a strip per track and the master. Every editor of a track is **one click from its strip** and opens in a popup over the mixer; its **×**, **Esc** or a click outside closes it, back to the mixer as it was. Colour always means something: a track's strip has its **engine's colour** (the drum track: its kit's kind), the drum sounds the colour of what plays (drum synth, sampled, X0X, your sample, your kit), and green / amber / red are OK / notice / too loud.

- **A synth strip** — engine and sound, then **Sound** (the engine, presets, files and every parameter as a knob, drag up / down or the wheel, Shift for fine steps, double-click resets, laid out in the device's own pages so each engine shows its own: ENV, ENV2, LFO, the engine's pages, VOICE, FX side by side), **Sequence** (the pattern settings and the steps), **Load preset** (every engine's presets, grouped and coloured, and the user presets), **Save preset** (a user slot with a name), **Project**; the steps (the playing one lit), the meter and level, pan, the **FX sends** (DST, CHO, DLY, REV) and FX on / dry, mute.
- **The drum strip** — its **16 sounds** as buttons (KICK … COWBELL, coloured by what plays): one click picks that sound (highlighted; the strip's send row shows its REV / DLY / CHO, in the places of a synth strip's CHO DLY REV, DST greyed, the sound's name above them; turning them sets that sound's sends live), a double click (or its key 1–0 again, or Enter) opens it: its source (grouped: this kit, your samples, sampled kits, synth kits by style, X0X machines), the knobs its source has (TUNE … LEVEL), its REV / DLY / CHO sends, a user sample's hit, start and length, **Play** (the sound's note on the drum channel, GLO › DRUMS CH), reset; a row of the 16 inside it to go from sound to sound; start from any kit, save to KIT 1–16. **Kit** picks the kit (grouped by kind), **User kits** stores, loads, exports and imports kits and puts up to 16 WAV hits into USR1–3. **Sequence**: the grid of the 16 sounds × the steps; choose a **level** (GHOST, SOFT, NORM, HARD) and a **roll** (x1–x4), then click: a hit; click it again: cleared; Shift+click: one level louder.
- **The master strip** — DUST, DUCK, FILT and the BPM as knobs, **Master FX** (the master bus, delay, reverb / chorus, the drums' level and REV, GLOBAL) and **Settings**.
- **Library**, **Samples** (with CHOP), **Projects** and **Settings** are screens of their own (the transport bar).
- **Backup** (Projects screen) — everything the FM-1 keeps in its flash, in one **`.optimist-backup`** file: the four projects / song sections, the working project, the 32 user presets, your drum kits, the drum sounds of the projects, the settings (panel, song) and the FM6 user bank; tick **with the samples** to add USR1–USR3 (up to 232 KiB, the size is shown). **Restore** reads a file and lists what it holds, each with a tick box: what this firmware does not have (a kit bank in a build without kits, say) is shown and not written. Restore only while stopped; each object is written the safe way (the old copy stays until the new one is complete; samples as an upload), then the FM-1 restarts and loads it all, older formats included. The installer asks **"back up first?"** before it installs: OK opens the editor's Backup.

Hover any parameter knob or menu for its tooltip: the same short line the FM-1 shows while that knob turns (one table, tools/param_help.json; the editor has its own copy, so it works with any firmware).

The protocol is documented in [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md) (v5, the drum commands 36–42, and v7: the drum sources, the lanes' values, the pages and the transport, 50–53).

## USB audio (experimental)

> **EXPERIMENTAL, not yet tried on a real FM-1.** Ported from [Melodee](https://github.com/keremimo/melodee) (Kerem Kilic), where it was tested on macOS; in SLOOP it has only run in the FM-1 emulator so far.

On the same USB cable as MIDI, the computer sees two class-compliant audio devices (USB Audio Class 1, no driver):

- **SLOOP In** — four mono inputs, one per track: input 1–3 the synth tracks, input 4 the drums. Each is the track after its insert (DIST, SLICER), LEVEL and MUTE / SOLO, *before* pan, the chorus / delay / reverb sends, DUST / DUCK / FILT, the punch-in effects and MASTER: dry stems for the DAW. The shared effects stay in what you hear from the FM-1. A stem at full scale clips: turn the track's LEVEL down.
- **SLOOP Out** — stereo playback from the computer, mixed into the FM-1's output after the master effects and scaled by MASTER. It is never sent back to SLOOP In (no loop when the DAW monitors its input).

Both run at **44.1 kHz** (the FM-1's own rate, no conversion); choose **16 or 24 bit** in the computer's audio settings (on macOS: Audio MIDI Setup). 24 bit adds no precision (the synth is 16 bit inside). For a DAW that wants one device for both directions, make an aggregate device (macOS: Audio MIDI Setup, +, Create Aggregate Device) with drift correction on the one that is not the clock. Avoid saving or loading presets and projects while recording: a flash write pauses the audio.

USB audio replaces the USB serial console (they share the USB endpoints); MIDI, the web editor and updates work as before. A build without it: `FELUCCA_USB_AUDIO=0` (see BUILDING.md).

## Sound design pages

The full Felucca engine is underneath, ten synthesis engines: ANALOG 2 (analog with SLOOP's osc 2 interval and hard sync, a filter envelope, filter modes, self-oscillation and a supersaw swarm), 4-op FM, phase distortion, lo-fi chip, sampler, formant voice, three-oscillator, tonewheel organ, granular, and FM6 (Melodee's six-operator FM that plays DX7 voices as Dexed does, engine 9); envelopes (with a pitch punch for the 808s), LFO, arpeggiator, scales and chords, glide and voice modes, per-track drive and slicer, chorus / delay / reverb sends (a stereo chorus, a tempo delay, a stereo reverb built as a feedback delay network: dense, no metallic ring), 32 user presets, 4 projects.

## Optional features (build switches)

Features taken from other FM-1 firmwares, each a build switch (`FELUCCA_…=1`, see BUILDING.md; the builder lists them with their cost and origin, `tools/backports.json`). A build without a switch is exactly the firmware without that feature; projects made with it load in every build.

### Snapshots (`FELUCCA_SNAPSHOTS`, 4 by default)

The whole state in a slot ([Snapshots](#snapshots)): 0, 2, 4 or 8 slots. About 8.7 KB of flash and 336 B of RAM;
the slots come from the end of USR3 (24 / 32 / 48 KiB). Off: USR3 is 64 KiB.

### The knob's help line (`FELUCCA_PARAM_HELP`, builder item PARAM_HELP)

While you turn a knob, the top bar says in a few plain words what it changes: *Filter cutoff*, *Reverb send*, *Osc 2 interval*, *Swarm spread (ENV2)*. It shows on every page (also VIEW ALL and the drum SOUND pages), on the TRACKS and drum screens and in the FM6 operator editor (in their header), and on the song screen (over the knob labels); it goes about a second after the knob stops, and never comes from changing page, a button or the tempo knob. The same lines are the web editor's tooltips (hover a knob). About 6 KB of flash, no RAM; on in the everything-that-fits profile, off by default.

### Per-step chance (`FELUCCA_CHANCE`)

On a synth track, **SEQ** a second time opens **STEP 2**: KNOB 1 picks the step, **KNOB 2 (PROB)** its chance, 0–100 % in 5 % steps. Each time the step comes round it plays with that probability; when it does not, it is a rest (its ratchet hits too). A tie is not rolled: it holds whatever sounds. 100 % (the default) is the step as before. The chance is saved with the project. After Felucca 1.0's per-step chance (Leo Kuroshita, GPL-3.0). Not on the drum track (yet).

### Played notes on the keys (`FELUCCA_KEYLIT`, on by default)

On a synth track, the keys light the notes the track plays now: the sequencer's step, the arpeggio's note, and notes held from MIDI. A note lights the lowest key that plays it (the octave, TRANSPOSE and the key layout count; a note no key plays is not shown). Layers keep their own key maps; the drum track keeps its hits. After Felucca 1.0.1 (Leo Kuroshita) and renebohne's SLOOP fork (GPL-3.0).

### Sequenced notes follow the scale (`FELUCCA_QNT_SEQ`)

**SCL > QNT** gets a fifth value, **SEQ**: the keys play as SNAP (every key rounded down onto the scale), and the notes of the pattern snap onto the scale as they play. Change ROOT or SCALE and the pattern follows; the steps keep the notes you wrote, so QNT back to OFF plays them as before. Two notes of a chord that land on the same note play once. Not on the GM KIT sample set. After Felucca 1.0.1's QNT SEQ (Leo Kuroshita, GPL-3.0). A project saved with SEQ opens in a build without this switch with QNT ALL.

### Reverb algorithms (builder: reverb: ROOM, PLATE, FDN8, SPRING)

The builder has one checkbox per reverb algorithm under the reverb bus: **reverb: ROOM**, **reverb: PLATE (Dattorro)**, **reverb: FDN8 (long, lush)** and **reverb: SPRING**. Tick at least one (the builder refuses a reverb bus with none). Every algorithm you tick is on the device: **FX** pressed until **REVERB**, KNOB 1 **TYPE** switches between them while the music plays; the web editor's Reverb popup (the master strip's Reverb icon) has the same **Type** list. With only one ticked there is no TYPE at all: no REVERB page, no control in the editor. SIZE, DAMP, the sends and the level work the same in all of them, and up to SIZE 90 (the default) SIZE gives the same decay time.
- **ROOM** (the default, as before): four delay lines at 44.1 kHz. Its first ~300 ms are sparse: separate echoes before the tail fills in.
- **PLATE**: Dattorro's plate (a figure-of-eight of allpasses and delays), dense within ~50 ms and smooth. It costs about 10 % less CPU than ROOM.
- **FDN8**: long and lush. Eight slowly moving delay lines, each with its own LFO, dense within ~50 ms, the widest and least ringing tail. Above SIZE 90 its decay keeps growing where the others stop at ~4 s: it doubles every 10 steps (about 3 s at 100, 5 s at 110, 10 s at 120, 14 s at 126), and 127 is a near-freeze that rings for about a minute. As SIZE grows the modulation deepens, a short pre-delay opens (up to 10 ms) and the treble lasts as long as DAMP says, so a long tail does not go dull. It costs about 20 % more CPU than ROOM. With REV_HALF the long tails are a little shorter (12.5 s at 126, ~40 s at 127). A project made with FDN8 before this change and SIZE above 90 now rings longer; the **SPACE** macro, which raises SIZE by up to 30, reaches these long tails too.
- **SPRING** (the *SPRNG* on the knob): a spring tank, after Felucca 1.0 (below).

Only one algorithm plays at a time, so they share one line buffer, the size of the largest one ticked (ROOM's 17 KB; PLATE and FDN8 alone 16 KB, 8 KB with REV_HALF). Switching fades the old reverb out over ~6 ms, clears the buffer, and the new one starts from silence: no click, no old tail in the new one. PLATE and FDN8 run at 22.05 kHz, so they keep nothing above ~11 kHz. With two or more algorithms, the reverb's code runs from main RAM instead of RAM code. Measured costs, each added to a ROOM-only build: PLATE +2.9 KB flash, +3.4 KB RAM; FDN8 +4.0 KB flash, +4.3 KB RAM (PLATE and FDN8 together share 1.1 KB of flash and 1.8 KB of RAM); SPRING +1.9 KB flash, +2.3 KB RAM; each saves 0.7–1 KB of RAM code. ROOM left out (with another one ticked): −1.8 KB flash, −2.4 KB RAM, −2 KB pool. PLATE and FDN8 have been measured on the host and in the emulator only, not yet heard on an FM-1.

The project keeps the algorithm (older projects load as they were: ROOM, or SPRING). A project made with an algorithm this build does not have plays the first one ticked and says so: **MISSING: REVERB FDN8**. It keeps the request when you save it, until you turn TYPE yourself.

### Spring reverb (`FELUCCA_SPRING`)

The **SPRNG** choice on REVERB > TYPE: a spring tank — the chirp and the drip of a guitar amp's spring, mono. REV/CHO's **SIZE** sets the spring's length and decay, **DAMP** its brightness. After Felucca 1.0's spring reverb (Leo Kuroshita, GPL-3.0).

### Motion recording (`FELUCCA_MOTION`)

While a track records (REC, playing), turning a knob of a sound parameter on its pages — envelopes, filter / pitch / LFO amounts, sends, pan, glide, the engine's EDIT values — records the value on the step that is playing. When the step comes round again, the value is set again: filter sweeps, send throws, a different decay on every step. Each pass of the loop starts from the sound as it was, and STOP puts it back, so the motion never overwrites your sound; a knob turned while not recording changes the sound under the motion. **SEQ** until **MOTION**: KNOB 1 **PLAY** on / off for the track, **EVNT** its events, **FREE** what is left of the 64 shared by the four tracks, KNOB 4 **CLEAR** (twice). The motion is saved with the project and its sections, with 4, 8 or 16 sections: each song section keeps its own (SAVE + key stores it with the section, loading or playing the section brings it back, a song changes it with each part). With 8 or 16 sections it costs a section 2 bytes and 3 bytes an event in the MEM gauge (none when nothing is recorded), and the four old project slots bring theirs along when they move into A–D. A build without motion recording plays the sections without it and keeps it until the section is saved again. After Felucca 1.0's motion recording (Leo Kuroshita, GPL-3.0).

### Performance macros (`FELUCCA_MACROS`, `FELUCCA_ENERGY`)

**GLO** pressed until **MACRO** (in VIEW ALL: the third row of GLO): four knobs that each move several sounds at once. Each is −100 % … +100 %; at **0 %** (home) nothing moves and the sound is exactly the one you made. Turned, they act on top of your values, which stay as you set them: every page still shows your own values, and turning a macro back to 0 brings the sound back exactly.

| Macro | Turned right (+) | Turned left (−) |
| --- | --- | --- |
| **COLOR** (dark … bright) | each synth track's brightness up: ANALOG / SAMPLE / TRIO / ACID **CUT**, DIGITAL **IDX**, PHASE **DCW**, LOFI / GRAIN / SLICE **TONE**, VOICE **BUZZ**, WHEEL **TOP**, FM6 **MOD**, PHYS **BRIT**; the delay's **COLR** | the same down |
| **MOTN** (still … alive) | the LFO's depths (filter, shape, amplitude, a touch of pitch), its **RATE**, ANALOG's **DRFT**, the chorus depth | the LFO's depths and the chorus depth fade to 0, the LFO slower |
| **SPACE** (close … huge) | the chorus, delay and reverb sends of tracks 1–3, every drum sound's REV (as it moved DRUMS REV), the reverb **SIZE**, the delay **FDBK** (never past 100 %), tracks 1 and 3 spread left and right | the sends fade to 0, the pans close to the centre (mono), a smaller reverb, a shorter echo |
| **ENRGY** (sparse … intense) | brightness a little up, **DIST** on tracks 1–3, the drums' level | brightness down, DIST to 0, the drums softer |

With **`FELUCCA_ENERGY`** ENERGY also arranges the drum track, in five bands taken on the beat (a band changes 3 % past its edge, so a knob resting on one does not flicker): far left, only the kicks, snare and clap on the eighths, every hit softer; left, the other sounds on the eighths only and no ghost notes; the middle, the pattern as written; right, every hit a level harder; far right, the closed and pedal hats and the shaker doubled (x2), and every second pass of the pattern ends with a snare fill. The pattern itself is never changed.

- **Saved** with the project and with each song section: a section can bring its own macros (they are kept in four values of the drum track that it does not use, so the project format is unchanged; a project from before loads with every macro at 0, and a build without the switch keeps them).
- **Motion recording** (`FELUCCA_MOTION`): a macro turned while any track records is recorded on the drum track's steps and played back with it.
- Cost: MACROS 1.3 KB of flash and 400 B of RAM, ENERGY 0.6 KB more (`tools/builder/costs.json`); the audio ISR spends ~65 instructions a sample while a macro is off home (emulator), nothing at home.

After Flowstate's macros and ENERGY arrangement by Zakaria Chowdhury (GPL-3.0): here the mappings are fixed and the same for every project.

### PHYS, physical models (`FELUCCA_ENG_PHYS`)

An engine of its own (engine 11): EDIT **MODEL** picks a bank of resonant modes (**MODAL**: bells, bars, plates), a plucked string (**STRNG**), a drum head (**MEMB**) or a string with three sympathetic strings (**SYMP**); STRC, BRIT, DAMP, POS shape the body, ACC, BOW, EXC the strike or the bow. Nine presets in the bank: BELL TREE, WOOD MRMBA, PLUCK, THUMB PNO, SYMP HARP, BOWED METAL, DRONE STRING, HAND DRUM, TOMS. Three voices per track. It needs 39 KB of memory: builds with the short delay line only. After Felucca 1.0's PHYS (Leo Kuroshita, GPL-3.0), its models ported from DaisySP (Electrosmith, Emilie Gillet) and Rings (Emilie Gillet), MIT.

### ACID, a 303 (`FELUCCA_ENG_ACID`, experimental)

> **EXPERIMENTAL.** Ported from X0X by Charles Vestal (GPL-3.0); in Optimist it has only run in the emulator.

An engine of its own (engine 12): one TB-303-style voice per track — saw or square, the 303's filter, envelope and accent, Devilfish slide and decay ranges, a drive. EDIT 1: **CUT RESO ENV DEC**; EDIT 2: **ACC WAVE DRV SLD**. A step's ACCENT is the 303's accent, a SLIDE glides into the next step. **ACID GEN** (EDIT, on an ACID track): **DENS**, **ACC** and **SLD** in %, KNOB 4 **GEN** twice writes a new acid line into the pattern (TB-3PO; in the track's ROOT, minor; undo brings the old one back). Four presets: ACID LINE, ACID SQR, ACID RAGE, ACID DUB. Pitch bend, the LFO's pitch and TUNE do not reach it. Its voice is X0X's port of Open303 (Robin Schmidt, MIT); its generator after the Phazerville Hemisphere Suite's TB-3PO (GPL-3.0).

### CZ, Casio-style phase distortion (`FELUCCA_ENG_CZ`)

An engine of its own (engine 13; a build without it plays a CZ track on PHASE and says MISSING). It plays tones the way a Casio CZ-1 does: two lines, each with its own 8-step pitch (DCO), timbre (DCW) and volume (DCA) envelope, the CZ waveforms and windows, ring and noise modulation, detune, vibrato, key follow, line levels and velocity sensitivity. EDIT 1: **TONE DCW W.TIM DTN** (the tone; its DCW depth up or down; its DCW envelope slower / faster; line 2's detune in cents); EDIT 2: **A.TIM LINE MOD VIB** (the DCA envelope slower / faster; lines 1, 2, 1+1', 1+2' or the tone's; ring / noise or the tone's; vibrato depth, 0 = the tone's). The envelopes are the tone's: ATK DEC SUS REL do not shape a CZ voice. Eight presets: CZ INIT, PD BASS, RESO SWEEP, GLASS BELL, WIRE BRASS, SOFT PAD, NOISE BREATH, PULSE KEYS — our own tones, not Casio's. A first version: the 8-step envelopes cannot be edited yet (a tone is 128 bytes a track and the project has no room for three), no CZ banks, no .syx or Casio SysEx. From Melodee 0.11 by Kerem Kilic (GPL-3.0); its native playback after Devin Acker's uPD933 model (MAME, BSD-3-Clause). On in everything-that-fits.

### Bass on the small speaker (`FELUCCA_BASSPLUS`)

**HOME held > MENU > LOWCUT** gets a third value: **OFF / LOWCUT / BASS+**. LOWCUT cuts what the FM-1's own speaker cannot play; BASS+ cuts an octave higher and puts the bass back as its harmonics, so a bass line is still heard on the speaker. For headphones and a PA, leave it OFF. After Felucca 1.0's BASS+ (Leo Kuroshita, GPL-3.0).

### Screen brightness (`FELUCCA_BRIGHT`)

**HOME held > MENU > BRIGHT**: the screen's backlight, 1 (dim) to 8 (full, as before). **EXPERIMENTAL, off by default, untested on a real FM-1.** On X0X, the same backlight PWM froze an FM-1 at its lowest level, and because the level was saved it froze again at every start until the firmware was reinstalled (X0X issue #2; the cause is unknown). So here the screen starts at full brightness at every power-on and reset (the level is not kept), and the lowest step is 4/16 instead of 1/16. Ported from X0X by Charles Vestal (GPL-3.0).

### Delay time on the beat (`FELUCCA_DLY_HALVE`, on by default)

A delay time longer than the delay line (a 1/4 note below 40 BPM; below 81 BPM in builds with the short line) plays at half its length, then half again, which stays on the beat; before, it was cut to the line's length. After X0X by Charles Vestal (GPL-3.0).

### From SLOOP 2.3: fixes (on by default)

Ported from SLOOP 2.3 by isod89 (GPL-3.0; many of them after Felucca 1.0 / 1.0.1 by Leo Kuroshita), each its own switch:

- **USB MIDI in loses nothing** (`FELUCCA_USB_FLOW`): when a lot of MIDI arrives at once from a computer, the computer is asked to wait (no note-off dropped, no hanging note); a malformed message is ignored.
- **Overload** (`FELUCCA_SHED_FADE`): when the processor is overloaded twice in a row, one voice fades out at a time, never a track's bass (its lowest POLY note) or its MONO / LEGATO / UNISON lead, instead of the oldest note going into its release.
- **Keys about a millisecond sooner** (`FELUCCA_KEYS_FAST`): a key is read as its column is scanned, a press after two samples closed (host-measured: 3.3 -> 1.7 ms from the contact).
- **No stuck note after a VOICE change** (`FELUCCA_MONO_RELEASE`): a key let go just after POLY / MONO changed no longer comes back.
- **Stricter checks of what is read from the flash** (`FELUCCA_ST_STRICT`): a stored record counts only in the copy it was written to; the calibration table must give each label its own button and knob.
- **Knobs** (`FELUCCA_KNOB_ONEREST`): one rest position a click, as Felucca 1.0 reads them, with Optimist's fast-turn decoder: a click paused half-way no longer makes every later click count twice.
- **TRS MIDI in after line noise** (`FELUCCA_TRS_NOISE`): a stray byte that looks like an empty slot of the input buffer no longer leaves the MIDI IN jack deaf until a restart.
- **Restore checks each object** (`FELUCCA_BK_CHECK`): restoring a backup, the editor's Projects tab writes a project, the settings, a preset bank, the kit bank or the drum records only if the firmware would load them (a project's size and sum, the calibration, the bank's shape); otherwise it stops with "not a valid object" and leaves what is on the FM-1.

### Coming from SLOOP 2.4 (`FELUCCA_SL24_SAFE`, on)

Optimist started on an FM-1 that ran SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f) erases nothing of 2.4's it cannot
read. Before this switch, the first start erased all four of 2.4's project slots, the second autosave wrote over
2.4's autosave, the first drum record saved erased 2.4's FM6 bank, and a user kit or snapshot save could cut the end
of a long USR3 sample. Now:

- 2.4's projects stay in flash, untouched; the PROJECT page lists their sections (A..D) as **SLOOP 2.4**, the editor
  as "SLOOP 2.4 (kept)"; LOAD says SLOOP 2.4 PROJECT and loads nothing. Saving a section A..D of your own keeps
  2.4's project beside it. While they are kept, the section log has fewer sectors (MEM shows the room).
- 2.4's autosave stays; Optimist's own autosave goes to the other copy.
- 2.4's FM6 bank (0xE5000), a USR3 sample longer than Optimist's USR3 and USR4 (0xE7000..) are never erased by a
  store (a save that would: refused, or the other copy).
- 2.4's settings word (MIDI OUT SEQ, IN CLOCK, USB SERIAL, the visualiser) is written back as read; 2.4's user presets
  keep FM6 and SLICE, and their track filter / strum / voice-lead values are no longer read as FX OFF.

### REC modes and count-in (`FELUCCA_REC_MODES`)

Press REC while stopped: the REC screen has three dials. **KNOB 1 mode** (an empty project): **free** (the free take above, the tempo follows your playing) or **tempo** (record at the tempo set; your first note starts the loop). **KNOB 2 length**: the selected track's loop, 1, 2 or 4 bars. **KNOB 3 start**: **note** (your first note starts the loop) or **count**: press PLAY for one bar of clicks (4, 3, 2, 1 on the screen, PLAY blinking), then it records; notes played meanwhile only sound; PLAY again goes back to armed, REC cancels. In a project with notes it always records at the tempo set. Mode and start are settings of the FM-1: they stay when you load a project. With an external MIDI clock, PLAY follows the clock (no count-in). From SLOOP 2.3 (isod89, GPL-3.0).

### Lights for playing in the dark (`FELUCCA_LIGHTS`)

**HOME held > MENU**: **LIGHTS** OFF / LOW / MID / HIGH: every button glows at that level, so the labels can be read on a black FM-1; what is active (the page, PLAY, REC, the octave) stays at full light and still blinks. **KEYS** OFF / C KEYS / WHITE KEYS: the C keys or every white key glow too. **NOTES** ON / OFF (with `FELUCCA_KEYLIT`): the played notes on the keys above, now a setting; on the layers whose keys are tiles (FX, SEQ, GLO) what sounds glows under them, on SCL the scale glows under the notes played. Settings of the FM-1. From SLOOP 2.3 (isod89; Felucca 1.0.1 #35; NOTES by renebohne), GPL-3.0.

**HOME held > MENU > USB SERIAL** (builds with the serial console, USB_MODE 1): **OFF** by default, as SLOOP 2.4. OFF presents the FM-1 as a MIDI-only device, the same as a build without the console; ON adds the serial console, as before. A change applies at the next start (the row shows RESTART until then). (In SLOOP 2.4, with the console on, macOS 13-15 gave the device to Apple's CDC composite driver and hid its USB audio input; Optimist never builds the console and USB audio together.) The editor, the installer and updates use USB-MIDI and need neither. From SLOOP 2.4 (isod89, after Felucca 1.0.3 #67), GPL-3.0.

**MIDI channels and MIDI OUT (SLOOP 2.4, `FELUCCA_MIDI_CH`, `FELUCCA_MIDI_OUT`, `FELUCCA_MIDI_INCLK`; always built, no builder choices: the on / off is in the HOME menu, SYSTEM 1/3 and 2/3).** Each track has its own MIDI channel, 1 to 16 or OFF (HOME menu, SYSTEM 2/3): the channel a note must arrive on to play that track, and the one its keys send on. Defaults as before: parts on 1, 2, 3, the drums on 10 (the old DRUMS CH). A channel no track has plays the selected track, as before; two tracks on one channel: the first plays; OFF is silent in and out. Channels are saved in the project (older projects load with the defaults). **OUT** KEYS / SEQ (HOME menu, SYSTEM 1/3): with SEQ the sequencer, the arp and the rolls go to MIDI OUT too, each on its track's channel; every note is ended, STOP ends what is still on, a note you sent in from MIDI is never sent back. **IN** NOTES / CLOCK: CLOCK takes the clock and start / stop only, no notes. OUT and IN are settings of the FM-1 (settings word bits 14 and 15, as in SLOOP 2.4). From SLOOP 2.4 (isod89, GPL-3.0); the per-track channels are ours.

### No zipper: the mixer glides (`FELUCCA_GLIDE`, experimental)

> **EXPERIMENTAL.** Ported from X0X 0.10.1 by Charles Vestal (GPL-3.0); in Optimist it has run on the host and in the emulator only.

A track's LEVEL, PAN and sends, MASTER, and the drum track's level, pan and per-lane sends move over about 10 ms instead of in steps, so a fast turn of a knob is smooth (no zipper). Nothing changes while a knob is still: a held sound is as before, sample for sample.

### From SLOOP 2.4: the sequencer

Ported from SLOOP 2.4 by isod89 (isod89/sloop-fm1 v2.4, 8d3823f; GPL-3.0, on Felucca by Leo Kuroshita) with the same gestures, so the SLOOP guide and muscle memory carry over. Each is its own switch.

- **Long steps** (`FELUCCA_DIV_LONG`, on by default): SEQ > DIV (and the SEQ layer's KNOB 2) go on past 1/4 to **1/2**, **1BAR** and **2BAR**, a step of 2, 4 or 8 beats, for slow chords and pads. Such steps are never swung. The new values are appended, so old projects are unchanged and SLOOP 2.4 projects play the same.
- **Dotted delays** (`FELUCCA_DLY_DOT`, on by default): FX > DLY > TIME gains **1/8D** and **1/16D** (3/4 and 3/8 of a beat). In builds with the short delay line (0.74 s, user-default) a 1/8D needs 61 BPM or more; below that it plays as 1/16D (still dotted, `FELUCCA_DLY_HALVE`), where SLOOP 2.4 cuts it at the line's length.
- **Micro timing** (`FELUCCA_MICRO`, off by default): on the SEQ layer, hold a step and turn **KNOB 4 NUDGE**: the step plays up to half a step early (-32) or late (+31), in 1/64 of a step; a nudged step shows a dot in its corner, and its ratchets ride with it. With it, the note length of a held synth step moves from KNOB 4 to **SELECT**.
- **Fills** (`FELUCCA_FILLS`, off by default): on the SEQ layer, hold a step and press **OCT+**: it cycles **normal**, **FILL ONLY** (an F on its tile: it plays only during a fill) and **NO FILL** (an x: it is silent during one). **OCT-** with a step held clears its nudge, locks and fill. **GLO + key 9** plays a fill while held; **GLO + key 10** makes the next bar a fill (again: cancelled); STOP ends both. With fills built, the **FX bypass** moves from GLO + 9-12 to **GLO + black keys 1-4** (F#, G#, A#, C#: the same keys as the SEQ layer's pages), and a muted-or-not tile shows a mark when that track is dry.
- **Parameter locks** (`FELUCCA_PLOCK`, off by default): on the SEQ layer, hold a step and turn **PRESETS**: the step gets its own value of a sound parameter, made at the track's value and then moved; **ALGORITHM** picks which (the last sound value you turned on a page, ENV DEST FLT at first); the title reads *lock flt +12* (or *--*). Up to 24 locks a track, several on one step; the value goes back at the next step without a lock on it, and notes still ringing follow. A knob turned during a lock becomes the new value after it; STOP puts every value back. On the drum track the kit locks, but not ENV DEST FLT / PIT / SHP and LFO DEST FLT, where the macros keep their positions. With motion recording, motion plays first on a step and the lock wins on its step.
- **Quick chain** (`FELUCCA_QCHAIN`, on by default): while playing, hold **SAVE** and tap up to 8 sections (A..D of the bank shown; SAVE + OCT moves the bank). The first one plays from the next bar, as before; let go of SAVE and they play in the order tapped, each for the bars of its longest pattern, looped (the title shows *chain A B B C*). One tap is a plain jump and ends the chain; STOP ends it too. Not saved.
- Nudges, locks and fills are kept with every project (`FELUCCA_SL24_XSTEP`, switched on with them), in SLOOP 2.4's own layout.

## Specifications

| | |
| --- | --- |
| Tracks | 3 synth parts (8 voices shared) + drums (16 sounds, 6 voices) |
| Sounds | 92 presets on 10 engines (browsed by kind, level-matched), 8 sampled sets (CC0), 3 slots for your own samples |
| Sequencer | 64 steps per track, own length and division each; chords with a level and ratchet per note; drums with a level and ratchet per sound; ties, slide; MPC swing 50–75 %; one sample-accurate clock for steps, arp, rolls, slicer and song (no drift) |
| Performance | layers (hold a button: keys and knobs change job): punch-in FX, erase, note repeat, step entry, key / chords, mute / solo / tap tempo |
| Drum kits | 37 (5 sampled, 32 synthesised, 16 sounds each) + 16 of your own; every sound editable (8 values) with its own reverb / delay / chorus sends, any key on a user sample |
| Effects | 16 punch-in effects; master DUST, DUCK, DJ filter; per track drive, slicer, sends to a stereo chorus, a tempo delay and a stereo reverb; master limiter |
| Recording | live, quantised as heard (latency-compensated), overdub; records at once while playing; free take sets loop length and tempo |
| Memory | undo / redo (many levels, in the memory the build leaves over), 32 user presets, autosave of the working project, song of 16 sections (A–P, compressed) × 64 steps × 1–64 bars |
| Audio | 44.1 kHz, fixed-point DSP |
| MIDI | USB class-compliant in / out, TRS MIDI in; channels 1–3 the synths, 10 the drums; pitch bend (RPN 0 range), mod wheel, sustain, CC120 / 121 / 123; MIDI clock in (USB or TRS: start / stop / continue / song position, latency-compensated) |
| USB audio (experimental) | class-compliant (UAC1), 44.1 kHz, 16 / 24 bit: 4 mono track inputs to the computer, stereo playback into the FM-1 |
| Update | over USB from the browser (package SHA-256 and CRC checked) |

### Where things live in the flash

The 1 MiB flash holds the firmware (to 0x93000), then SLOOP's data. Every object below is written as two copies (A/B): a save goes to the older copy and its header is written last, so a save cut short leaves the previous one in charge.

| Flash | What |
| --- | --- |
| 0x97000–0x9EFFF | the song sections A–P and the long song chain: one log of 8 × 4 KiB (firmware/src/storage/sections/sec_log.c; compressed records, sec_codec.c). A 4-section build: the 4 projects A–D there (2 × 4 KiB each; format 10, "FUNA": 3,640 B) |
| 0x9F000 + 0xFE000 | the working project (autosave) |
| 0xA0000–0xD9FFF | your samples USR1–USR3 (USR3 72 KiB) |
| 0xD0000–0xD7FFF | with snapshots (4 slots): the snapshots, the end of USR3's range (2 slots: 0xD2000–, 8 slots: 0xCC000–); USR3 ends below it |
| 0xDA000–0xDBFFF | your 16 drum kits (bank "DKB2": the kits, then their sends) |
| 0xDC000–0xDFFFF | the 32 user presets |
| 0xE0000–0xE4FFF | the update loader's staging (USB updates) |
| 0xE5000–0xE6FFF | **the projects' drum records**: each project's 16 sound edits, sources, sample references and sends, 236 B, two per project slot and two for the working project (2,408 B) |
| 0xE7000–0xE8FFF | free |
| 0xFC000–0xFDFFF | settings, the learned panel, the song |

The editor's **Backup** reads each of these by name (SETT, DLNS, PRJ1–4, AUTO, UPR1–2, UKIT, USR1–3; `firmware/src/io/editor/ed_backup.c` lists them) and writes them back through the same A/B saves.

A project names its drum record by a key (a hash of it; 0 = every sound as its kit, then nothing is stored). Saving writes the record first (only when it changed), into the entry the project in flash does not use, then the project: whatever cuts a save short, the slot loads either the old project with its sounds or the new one with its own. Projects of format 8 and 9 (their sounds inside the project) load as before. The drum records sit after USR3's whole range (0xA0000–0xDBFFF), so whatever is carved from USR3's end for the kit bank (today 0xDA000; later the shared kit + FM6 bank, 16 KiB at 0xD8000–0xDBFFF) cannot overlap them; `drum_store.c` and `tests/drum_sends_test.c` check it.

## Rescue, going back, credits

- **USB rescue:** hold **OCT−** alone while switching on (*SLOOP USB RESCUE*), then install again.
- **Interrupted install:** the FM-1 stays in update mode; press Install again and it finishes. A damaged package is refused, and the FM-1 keeps waiting for a good one.
- **Boot guard:** after two start-ups that crash before the UI runs, the FM-1 starts in USB rescue by itself; if the rescue mode crashes too, it drops into the chip's own update mode (UBOOT, "WL80UBOOT1.00" on USB).
- **Back to the official firmware:** in the installer page, *Return to the official V15*: select M-VAVE's FM-1 V15 file (FM-1.fwsc from m-vave.com, unchanged: the page checks its SHA-256 and sends it nowhere). Or M-VAVE's own updater, M-UPGRADE. Save your work in the editor first (project file, user preset bank, FM6 bank, drum kits, samples): the official firmware does not use it. An interrupted return finishes when you press the button again.
- **Last resort, from a Mac, no extra hardware:** `bash tools/fm1_rescue.sh` (from X0X). For an FM-1 in UBOOT (after the boot guard, the SysEx key, or the console's `uboot yes`): it waits for the FM-1, backs up the whole flash first, checks the chip (key 980F, flash 856014), and writes only the 4 KiB sectors of the firmware area that differ from V15, never the bootloader below 0x4000 nor the data above 0x93000, then reads everything back. It needs sudo (macOS holds the device as a disk) and pyusb; the flash loader (kagaimiq's wl82loader.bin, MIT) is downloaded and checked by its hash. Without `--write`, `tools/fm1_rescue.py FM-1.fwsc` only checks and backs up. A Transporter (FM-1-transporter) does the same from its own hardware.
- **Credits:** Optimist is based on SLOOP (isod89/sloop-fm1) and Felucca / Melodee (hugelton/Felucca, keremimo/melodee; FM6 by Kerem Kilic), with parts from X0X (charlesvestal/fm1-x0x, Charles Vestal). SLOOP is based on Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments — engines, sequencer, editor and installer. USB audio from Melodee by Kerem Kilic (Ellic Studio), GPL-3.0. Font: Terminus (SIL OFL 1.1). Samples: Versilian Studios VSCO-2 CE and VCSL, Sonic Pi (all CC0). PHASE: CrispyZebra (GPL). VOICE after klattsch (MIT). MIDI expression, MIDI notes through the scales, the KEYS ALL layout, step note length and the encoder first-click fix: ported from Melodee (keremimo/melodee, GPL-3.0) by Kerem Kilic and ChanceTheMaker. TRS MIDI input and the clock-follow design after Melodee by Kerem Kilic (GPL-3.0). The encoder decoder for fast turns, knob acceleration, the faster screen transfers and the rescue tool from X0X by Charles Vestal (GPL-3.0); the size-optimised build and the return to V15 after Felucca 1.0.1. Optional features ported from Felucca 1.0 / 1.0.1 by Leo Kuroshita (per-step chance, played notes on the keys, QNT SEQ, spring reverb, BASS+, motion recording, the PHYS engine; GPL-3.0; PHYS's models from DaisySP and Rings, MIT), from renebohne's SLOOP fork (played notes on the keys; GPL-3.0) and from X0X by Charles Vestal (screen brightness, delay halving, the ACID engine and its TB-3PO generator; GPL-3.0; Open303 by Robin Schmidt, MIT). Interface ideas after teenage engineering's pocket operators and EP-133, Elektron's step entry and Akai's MPC (swing, note repeat, erase) — Optimist and SLOOP are not affiliated with any of them.
- **Licence:** GPL-3.0, no warranty. M-VAVE and FM-1 are trademarks of their owners; SLOOP is not affiliated with them. Drum kit names describe styles; they do not refer to any product.

# Optimist 0.2

There is more in 0.2 than one firmware can hold: 26 synth engines, 37 kits, the inserts, Bluetooth, the web screen. No single build has it all, and the release flavours are only our picks. Make your own in the web builder: tick what you want, watch the flash and RAM budget, and build it in the cloud in a few minutes.

## Bluetooth

- **Bluetooth audio.** The FM-1 plays through AirPods and other Bluetooth headphones and speakers (A2DP, our own stack): pairing, bonding, reconnect without pairing; SHIFT + volume switches between Bluetooth and the FM-1's own output, or reconnects the last device. Adaptive frequency hopping: the link counts the headset's answers per radio channel and drops the bad channels from the hop set every 30 s, as every phone does.
- **Bluetooth MIDI.** BLE MIDI, our own stack: play the FM-1 from an iPhone or a Mac, or send notes out; long packets asked for.
- **Both at once.** BT MIDI and BT AUDIO are two pages in SYSTEM, each with its own mode (OFF / MAN / AUTO), device and state. **[check: running both at once has not been tested on hardware yet]**

## Sound

- **26 synth engines, ten of them new.** Every one a builder item; the new ones are off by default.
  - **DRONE** (new): Caravel's coupled drone network, LYRA-8 inspired, with binaural BEAT and HOLD (Caravel 0.3.3 by bonszaimusic).
  - **PLAITS** (new): four of Mutable Instruments Plaits' models, waveshaping, additive, granular formant and particles (Plaits by Émilie Gillet).
  - **STRINGS** (new): a 1970s string machine (Clipper 1.1 by 3dSam).
  - **VPS** (new): vector phaseshaping, a sine read through a bent phase (Clipper).
  - **TERRAIN** (new): wave terrain synthesis (Clipper).
  - **WSEQ** (new): Wavestation-style wave sequencing (Clipper).
  - **VECTOR** (new): Prophet VS-style vector synthesis (Clipper).
  - **WAVE** (new): Clipper's wavetables, 8 of its 16 tables, one table shared by every WAVE track; +70 KB of flash and +73 KB of pool, so a build with it needs a shorter delay line.
  - **SID** (new): the Commodore 64's MOS 6581 / 8580, one voice of reSID 0.16 in integer code, three a part (Mini-Tone by Stephane Schmitz, after reSID by Dag Lem).
  - **PROPHET** (new): a Prophet-5 style voice with our controls and presets, no Sequential programs (from Melodee by Kerem Kilic).
  - **ANALOG 2**: the analog-style synth, swarm, two filters, now with four filter models (below).
  - **FM6**: the DX7, bit-exact with Dexed; SysEx voices and banks in; voice packs (from Melodee).
  - **JUNO**: the Juno-106 voice, DCO, OTA ladder filter and HPF (fm-106 / KR-106).
  - **ACID**: the TB-303 bass voice with the TB-3PO line generator (X0X, Open303).
  - **CZ**: Casio CZ-1 style tones, two lines with 8-step envelopes, the CZ-1 tone collection (from Melodee).
  - **PHYS**: physical models, modal, string, membrane and sympathetic strings.
  - **DIGITAL**: 4-operator FM.
  - **PHASE**: phase distortion, with FOLD, a sine wavefolder (Felucca-Engines).
  - **LOFI**: chip sounds and wavetables.
  - **SAMPLE**: the sample sets and your own USR slots.
  - **VOICE**: formant synthesis, sung vowels.
  - **TRIO**: three oscillators.
  - **WHEEL**: a drawbar organ.
  - **GRAIN**: granular synthesis on the sample sets.
  - **SLICE**: a break slicer with its BREAK sample.
  - **NOISE**: colours, crackle and shift-register noise (Felucca-Engines).
- **ANALOG 2 filter models:** LADR (Moog-style ladder), OTA (Juno-style 4-pole), DIOD (303-style diode ladder), MS20 (Sallen-Key); each a builder choice.
- **Three independent drum synths:** the 32 classic kits, KAWA (kit 47, from Kawa Tracker by Guizmoh) and PLAITS (kit 48, Plaits' kick and snare, from Hortator by Leo Kuroshita after Plaits).
- **Inserts:** XDLY, a LYRA-8 style cross-modulated dual delay; GATER; TAPE and SPIT (from Melodee 1.4.4 by Kerem Kilic); the insert slots show how many instances are free (2/4 CHRS).
- **Scene backports**, builder items on by default: track FILTER resonance and drum TUNE on the noise sounds (SLOOP 2.8 by isod89), a track reuses its own released voice first (JIANT 0.9.9 by juanjiant-bit), PHASE's sine wavefolder (Felucca-Engines by Leo Kuroshita). The note generators (NOTEGEN, from Felucca) and USB audio (from Melodee).
- **Levels aligned:** drums and synths at one level; every preset trimmed to the same loudness; drum VOLUME up to +6 dB.
- One list of note values everywhere (4BAR … 1/64, straight, dotted, triplet) for the arp, SEQ DIV, delay TIME, synced LFOs, slicer, gater, ROLL. Arp GATE up to 198 %.

## Sequencer and projects

- **GRIDS note generator** (Mutable Instruments Grids' drum map) and the note generators, apart from the STEP tools. Two builder sub-items, off by default: **[check: host-tested only]**
  - **LFO modulation:** two LFOs on the drum track move the kit's six GRIDS values (X, Y, CHAOS, the three densities), so the pattern drifts on its own.
  - **Euclidean mode** (OUT DRUMS / EUCL): Grids' own Euclidean output, X, Y and CHAOS the three parts' lengths 1..32, BD SD HH their fills, bit for bit Grids' table.
- **Projects own their scenes, patterns and song.** A project is one record; PROJECT LOAD opens it (asks to SAVE first), SAVE saves everything, DELETE / NEW clear it; backup objects PJ01..PJ16.
- **SONG tables:** scenes and parts as tables; SELECT / ALGORITHM navigate, the knobs edit; BARS; edits and clears while playing.
- **Mute mode** (SHIFT + a key) with three key lights; the master channel too; the track LEDs follow the notes.
- **CHORD+ latch:** a black modifier key tapped twice stays on.
- **Live save** (builder item): save while playing.

## Web: everything on one site

- **Web editor.** Every sound, drum, effect and setting as knobs; the drum grid; the samples (with CHOP); the preset library; projects. Backup of the whole FM-1 in one file, now its own Backup tab. Works over USB in Chrome or Edge (Web MIDI).
- **Screen tab.** The FM-1's screen, live in the browser as it is drawn, with the mixer's meters ten times a second; a tap fills the display. Over USB.
- **Builder.** Choose engines, kits, effects and features with a live flash / RAM budget; _Build in the cloud_ returns your firmware in minutes. The builder now knows what each choice really costs (measured on the farm), shares one effects memory between every insert with a delay line, and tells you how many of each fit.
- **Installer.** Install a release flavour, your cloud build or a file; a backup first; stock firmware back any time.
- **Simulator.** The firmware running in the browser with the FM-1's panel and keys: try a build before installing it.

## Thanks

Optimist stands on SLOOP (isod89), Felucca and Felucca-Engines (Leo Kuroshita, Hügelton Instruments), Melodee (Kerem Kilic: FM6, CZ, the PROPHET voice, USB audio, TAPE and SPIT), X0X (Charles Vestal: the 909 and 808 kits, ACID with Open303 and TB-3PO), fm-106 (PatttF, after KR-106 by Kayrock Screenprinting: JUNO), Caravel (bonszaimusic: DRONE), Mutable Instruments (Emilie Gillet: Plaits, Grids), Hortator (Leo Kuroshita: the PLAITS kit), Kawa Tracker (Guizmoh: the KAWA kit), JIANT (juanjiant-bit), SLOOP 2.8 (isod89: the backports), Clipper (3dSam: STRINGS, VPS, TERRAIN, WSEQ, VECTOR, WAVE; GPL-3.0-only), Mini-Tone (Stephane Schmitz: SID, after reSID 0.16 by Dag Lem; GPL-2.0-or-later). Licences in LICENSING.md.

**A word for Émilie Gillet.** PLAITS and GRIDS are hers, from Mutable Instruments, which she closed in 2022, leaving every line of her code open for the rest of us. She asked for nothing and walked away quietly. Her ideas live on in thousands of instruments, and now in this one. We miss her. Thank you, Émilie.

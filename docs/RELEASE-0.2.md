# Optimist 0.2 — release notes (draft, updated as we go)

_Working draft 2026-10-23. Items marked **[check]** are to confirm on hardware or in a store; **[decide]** is the user's call before release._

## Headline

Optimist 0.2 brings the FM-1 to the browser and the phone, plays over Bluetooth, and gives every project its own patterns and scenes. More synths, more drums, more fits in the flash.

## Web: everything on one site

- **Web editor.** Every sound, drum, effect and setting as knobs; the drum grid; the samples (with CHOP); the preset library; projects. Backup of the whole FM-1 in one file, now its own Backup tab. Works over USB in Chrome or Edge (Web MIDI).
- **Screen tab.** The FM-1's screen, live in the browser as it is drawn, with the mixer's meters ten times a second; a tap fills the display. Over USB. **[check: Bluetooth mirroring was tried and set aside as too slow]**
- **Builder.** Choose engines, kits, effects and features with a live flash / RAM budget; _Build in the cloud_ returns your firmware in minutes. The builder now knows what each choice really costs (measured on the farm), shares one effects memory between every insert with a delay line, and tells you how many of each fit.
- **Installer.** Install a release flavour, your cloud build or a file; a backup first; stock firmware back any time.
- **Simulator.** The firmware running in the browser with the FM-1's panel and keys: try a build before installing it.
- The pages link the GitHub repository.

## iPhone app: Optimist Mirror (upcoming)

- The FM-1's screen on an iPhone or iPad over a USB cable, pixel for pixel, with the mixer's meters; the picture turns with the phone.
- Projects, Library and Backup screens, the web editor's tabs, in the app. **[check: App Store / TestFlight status]**

## Bluetooth

- **Bluetooth audio.** The FM-1 plays through AirPods and other Bluetooth headphones and speakers (A2DP, our own stack): pairing, bonding, reconnect without pairing; SHIFT + volume switches between Bluetooth and the FM-1's own output, or reconnects the last device. **[check: XM4 crackles were open]**
- **Bluetooth MIDI.** BLE MIDI, our own stack: play the FM-1 from an iPhone or a Mac, or send notes out; long packets asked for.
- **Both at once.** BT MIDI and BT AUDIO are two pages in SYSTEM, each with its own mode (OFF / MAN / AUTO), device and state. **[check: running both at once has not been tested on hardware yet]**

## Sound

- **Ten new engines.** DRONE (Caravel's coupled network, LYRA-8 inspired, with BEAT and HOLD; Caravel 0.3.3 by bonszaimusic) and PLAITS (four of Mutable Instruments Plaits' models: waveshaping, additive, granular formant, particles; Plaits by Emilie Gillet). Eight from the scene, each a builder item, off by default: STRINGS (a 1970s string machine), VPS (vector phaseshaping), TERRAIN (wave terrain), WSEQ (Wavestation-style wave sequencing), VECTOR (Prophet VS-style vector synthesis) and WAVE (wavetables, 8 of Clipper's 16 tables, one table shared by every WAVE track; +70 KB of flash and +73 KB of pool, so a build with it needs a shorter delay line), all from Clipper 1.1 by 3dSam; SID (the C64's MOS 6581 / 8580) from Mini-Tone by Stephane Schmitz, after reSID 0.16 by Dag Lem; PROPHET (a Prophet-5 style voice from Melodee by Kerem Kilic, with our controls and presets, no Sequential programs). **[check: the eight scene engines are host-tested only, never on an FM-1]**
- **ANALOG 2 filter models:** LADR (Moog-style ladder), OTA (Juno-style 4-pole), DIOD (303-style diode ladder), MS20 (Sallen-Key); each a builder choice.
- **Three independent drum synths:** the 32 classic kits, KAWA (kit 47, from Kawa Tracker by Guizmoh) and PLAITS (kit 48, Plaits' kick and snare, from Hortator by Leo Kuroshita after Plaits).
- **Inserts:** XDLY, a LYRA-8 style cross-modulated dual delay; GATER; TAPE and SPIT (from Melodee 1.4.4 by Kerem Kilic); the insert slots show how many instances are free (2/4 CHRS).
- **Scene backports**, builder items on by default: track FILTER resonance and drum TUNE on the noise sounds (SLOOP 2.8 by isod89), a track reuses its own released voice first (JIANT 0.9.9 by juanjiant-bit), PHASE's sine wavefolder (Felucca-Engines by Leo Kuroshita). The note generators (NOTEGEN, from Felucca) and USB audio (from Melodee).
- **Levels aligned:** drums and synths at one level; every preset trimmed to the same loudness; drum VOLUME up to +6 dB.
- One list of note values everywhere (4BAR … 1/64, straight, dotted, triplet) for the arp, SEQ DIV, delay TIME, synced LFOs, slicer, gater, ROLL. Arp GATE up to 198 %.

## Sequencer and projects

- **Projects own their scenes, patterns and song.** A project is one record; PROJECT LOAD opens it (asks to SAVE first), SAVE saves everything, DELETE / NEW clear it; backup objects PJ01..PJ16.
- **SONG tables:** scenes and parts as tables; SELECT / ALGORITHM navigate, the knobs edit; BARS; edits and clears while playing.
- **GRIDS note generator** (Mutable Instruments Grids' drum map) and the note generators, apart from the STEP tools.
- **Mute mode** (SHIFT + a key) with three key lights; the master channel too; the track LEDs follow the notes.
- **CHORD+ latch:** a black modifier key tapped twice stays on.
- **Live save** (builder item): save while playing.

## Under the hood

- **~38 KB of flash back** on the synth flavour (571 → 533 KB) without losing a feature or a pixel: packed tables and records, a compressed boot image, size-optimised code off the audio path. More fits in every build. **[check: the compressed boot image has not booted on hardware yet]**
- Every delay line in one effects memory; a 48 KB option; the builder's estimate holds the real costs.
- Builder: USB MIDI always on; items for 6-bit 909 hats and a smaller tanh table; BT_DIAG keeps the console quiet.

## Thanks

Optimist stands on SLOOP (isod89), Felucca and Felucca-Engines (Leo Kuroshita, Hügelton Instruments), Melodee (Kerem Kilic: FM6, CZ, the PROPHET voice, USB audio, TAPE and SPIT), X0X (Charles Vestal: the 909 and 808 kits, ACID with Open303 and TB-3PO), fm-106 (PatttF, after KR-106 by Kayrock Screenprinting: JUNO), Caravel (bonszaimusic: DRONE), Mutable Instruments (Emilie Gillet: Plaits, Grids), Hortator (Leo Kuroshita: the PLAITS kit), Kawa Tracker (Guizmoh: the KAWA kit), JIANT (juanjiant-bit), SLOOP 2.8 (isod89: the backports), Clipper (3dSam: STRINGS, VPS, TERRAIN, WSEQ, VECTOR, WAVE; GPL-3.0-only), Mini-Tone (Stephane Schmitz: SID, after reSID 0.16 by Dag Lem; GPL-2.0-or-later). Licences in LICENSING.md.

# Optimist 0.3

## New

- **Ableton Link over Wi-Fi**: tempo, phase and start / stop with Live and other Link apps. Set up in SYSTEM > WI-FI. 
Any device in the session can change the tempo. An FM-1 that joins while playing falls in on the session's next bar. 
An access-point mode for use without a router is available as a builder option (less tested).
- **JAM mode**: FM-1s play together over Wi-Fi with no router.
Set SYSTEM > WI-FI > MODE to JAM and pick the same GROUP (1–9) on each: the rest is automatic. 
The FM-1s of a group talk directly on the group's channel (1, 6 or 11: if it is busy, pick another GROUP), with no host and no waiting: 
any of them can come and go, and Link keeps them in time. STATE shows how many FM-1s are heard, LINK PEERS how many are in time with this one. A phone or Live cannot join a JAM group: for those, use a router or MODE AP.
- The web editor's Wi-Fi tab has the same modes as the FM-1, including JAM and its GROUP.
- **Live patterns**: 16 slots per track, switchable while playing. A free slot can start as a copy of the current pattern or empty.
- **Scenes** A–P on the white keys, launched at the next bar. SAVE stores into the current scene, holding REC clears it.
SONG > MODE chooses between repeating a scene and playing the song.
- **Step entry**: a synth track's STEP page enters notes on a cursor step, playing or stopped. 
A double-tap on SEQ switches that track to MUTE MODE (the keys mute steps) and back; 
each track keeps its mode until power-off.
- **Memory management**
  - Memory layouts in the builder: fewer sample slots and stored projects in exchange for more firmware space (up to 823 KB instead of 581 KB).
  - Engines and effects share memory. FX MEM sets the effects' share per project. If a new engine does not fit, the FM-1 offers to shrink an effect.
  - SYSTEM > TOP shows CPU and RAM use per part. SYSTEM > METERS draws the load at the bottom of the screen.
- **Optimist Mirror is on the App Store**, for iPhone, iPad and Mac: <https://apps.apple.com/app/id6826362906>
- **Mirror over Bluetooth**, in the app and the web editor.
- **Built on the latest fm1-base**, the shared FM-1 platform.
- **An open-source compiler**: the firmware is built with Julius Vitkauskas's ([@jvitkauskas](https://github.com/jvitkauskas)) pi32v2-llvm instead of JieLi's proprietary compiler, natively on Linux and macOS, with no Docker. Builds are 14 to 30 KB smaller. Thank you, Julius!
- USB audio: RET A / B / C as separate sources for recording stems.
- GRAIN has 12 more presets, at least two on each sample set, so every build with a sample set lists some ([#147](https://github.com/w0ts/optimist/issues/147)).
- **SD card mode**: a microSD card on the FM-1's LCD bus holds projects, presets, kits and samples, and samples stream from the card. It is in the release. The card is built with the SD board in `hardware/` or a board like it. Measured on hardware: the card runs at 12 MHz on two FM-1s (10 MHz with the stock resistors on one), reading 1,147 KB/s, and 8 voices play from the card's path. In SD card mode PROJECT has no MEMORY, TOOLS or USER pages. SYSTEM > SD CARD and SD STREAM show the card's state and read rate.

## Changed

- CHO, DLY and REV can go in any effect slot. ROOM and TDLY also work as inserts.
- USB MASTER is the mix at unity, before the volume knob.
- Cursor moves during playback are silent. Notes can be moved into chords and lengthened over other notes. Held steps have a GLIDE card.
- Shortcuts (R SHIFT + GLO, EDIT, FX…) work from the pattern page and SONG.
- Every track starts on pattern 1, so edits are never outside a pattern ([#142](https://github.com/w0ts/optimist/issues/142)).
- The keys play the selected track on pages that don't use them.
- Drum presets are dry by default. Engines and presets are sorted A–Z.
- The speaker low cut is always on; the headphone correction is automatic.
- GLOBAL's first page has TUNE, QUANT and FILTER RESO. The Calibrate page is gone.
- New default project: ANALOG ACID 303, FM6 GLASS EP, DIGITAL GLASS PAD.
- A 16-row screen canvas option frees up to 52 KB of RAM.
- The Mirror scales by whole multiples, with smoothed text. The app opens .fwsc files from Files and AirDrop.
- Installs work from any firmware, resume after a dropped cable, and send only changed sectors.
- The FM-1 is named FM1-XXXX on USB, MIDI, Bluetooth and Wi-Fi, so M-Flasher and other tools find it ([#138](https://github.com/w0ts/optimist/issues/138)). If the FM-1 refuses a package (M-VAVE's firmware over m-bootloader), the installer says nothing was written and switches to download mode.
- With several FM-1s connected, the builder and `make flash` ask which one, or flash them all one after the other.
- KNOB COLORS is on by default. SYSTEM lists Bluetooth and Wi-Fi first.
- The USB serial console (builder option USB_CONSOLE) is on from the first start. The SERIAL switch in SYSTEM is gone.
- Card samples stream through the audio render: the card's blocks arrive in an interrupt, so a dense kit no longer runs dry (195 dry voices in 28 s before, 0 after, on one FM-1).
- Release packages carry GitHub build provenance.
- The web builder shows measured sizes for every option.

## Fixed

- A crash in ANALOG 2.
- An FM6 patch copied to the library and back came out as a plain sine. The library now keeps the DX7 voice both ways, and imported DX7 .syx voices can be dropped onto a user slot ([#133](https://github.com/w0ts/optimist/issues/133)).
- Receiving DX7 voices made the screen flash ([#133](https://github.com/w0ts/optimist/issues/133)).
- Builds with a single synth engine could fail to link.
- Builds with the USB MIDI host failed their final check.
- Restoring a backup from the web editor turned a project's unsaved pattern edits into saved patterns.
- After the metronome had clicked, a faint high tone kept sounding.
- The computer's playback leaking into USB recordings.
- BLE-only builds forgetting Bluetooth audio devices.

## Before installing

A package with a different memory layout than the one installed **erases** the projects in flash, autosave A and the FM6 voices. Kits, presets, settings and autosave B are kept. **Back up in the web editor first.**

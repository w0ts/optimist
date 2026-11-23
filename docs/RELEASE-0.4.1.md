# Optimist 0.4.1

A patch release on 0.4: fixes and a newer base, no new features.

## Fixed

- **USB MIDI no longer freezes while the SD card streams**: the card's read-ahead runs at a lower interrupt priority than USB (fm1-base's 2026-11-18 sync).

## Changed

- Based on fm1-base upstream main (2026-11-22): its Wi-Fi/Link code, USB audio options and app-steps support. Optimist's USB audio input uses upstream's input call.
- The installer and M-Flasher use m-bootloader 3.0 (Julius's rewrite; 2.4 before).
- The release is `FM-1_709000401`; its version on the screen is 0.4.1.

## Before installing

Same as 0.4: a package with a different memory layout than the one installed **erases** the projects in flash, autosave A and the FM6 voices. **Back up in the web editor first.**

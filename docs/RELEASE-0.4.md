# Optimist 0.4

## New

- **SEL CH**: GLOBAL > CHANNELS > SEL CH is the MIDI channel that plays the selected track (default 4; OFF, 1-16, ANY). Other free channels stay silent for other gear ([#265](https://github.com/w0ts/optimist/issues/265)).
- **Screen reader**: the FM-1 says its screen: a page's title and cards when it opens, a card when its knob rests, the messages.
- **ANALOG 2 WIDTH** on the SWARM page: the copies spread across the stereo field (0-100 %).
- **DRONE GLIDE** goes up to 5 minutes.
- **Wi-Fi after the install**: a build with Wi-Fi asks for the network name and password and sends them once. The password is not kept.
- **Release identity FM-1_709**, with the version.

## Changed

- Link: the session clock is filtered, so the tempo wanders less.
- CLOCK OUT's ticks reach USB on time: 4x less jitter at 120 BPM.
- Each FM-1 has its own USB serial number, so macOS keeps their MIDI names apart.
- USB audio input timed by the crystal (fm1-base upstream).
- Cloud builds and fm1-base releases install from the web installer ([#259](https://github.com/w0ts/optimist/issues/259)).

## Fixed

- The newer flash chip UC25HQ80 works with the app, the rescue mode, the update loader and `fm1_rescue.py` ([#261](https://github.com/w0ts/optimist/issues/261)).
- A drum pattern switched while playing no longer plays empty ([#234](https://github.com/w0ts/optimist/issues/234)).
- An update works after an app left a SysEx stream open or the SD card was mid-read; before, a cold boot was needed.
- A SysEx stream an app left open is let go after 250 ms; before, the FM-1 stayed silent on MIDI until a cold boot.

## Before installing

Same as 0.3: a package with a different memory layout than the one installed **erases** the projects in flash, autosave A and the FM6 voices. **Back up in the web editor first.**

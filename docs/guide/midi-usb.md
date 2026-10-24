# MIDI, Bluetooth, USB audio and the web editor

Where each setting is: GLOBAL > MIDI and Channels, SYSTEM > BT MIDI, BT AUDIO and USB AUDIO ([the interface](interface.md)).

## MIDI in

A keyboard or a DAW on USB or the TRS MIDI IN jack plays the FM-1. Each track has a channel (defaults: tracks 1-3 on channels 1-3, the drums on 10; OFF is silent). A channel no track has plays the selected track. A note always ends on the track it started on.

- MIDI notes go through the track's **SCL** settings like the FM-1's own keys (KEYS WHITE, ALL, SNAP; CHORD on: one key plays the chord of its degree). TRANSPOSE applies, the FM-1's OCT buttons do not.
- **Pitch bend** +-2 semitones (RPN 0 sets another range), **mod wheel** (a vibrato of its own), **sustain pedal** (CC64), and the panic messages CC120, CC121 and CC123. Drums ignore bend, wheel and sustain. A drum note plays the lane whose instrument it is ([drums](drums.md)).
- **MIDI OUT** (USB): KEYS sends what you play, SEQ also the sequencer, the arp and the rolls, each on its track's channel.

## MIDI clock

**CLK IN** (saved with the project): **AUTO** follows an external clock when one comes in (TRS first, then USB, then Bluetooth MIDI), else the FM-1's tempo; **INT** ignores clocks; **USB**, **TRS** or **BLE** follows only that input. Start begins at step 1 on the first pulse after it, Continue resumes at the song position sent, Stop stops. The tempo shown, the delay, the arp, the rolls and the slicer follow the measured tempo. The source changes only while stopped. **CLK OFS** plays the steps that much earlier while following a clock, **BT OFS** the same while the audio goes to Bluetooth.

**CLK OUT** (off by default) sends MIDI clock on USB and to a Bluetooth MIDI device that listens (the TRS jack is input only): 24 pulses a beat, also while stopped, Start and Stop with the transport, Song Position and Continue when it continues a song. The pulses never swing. Nothing is sent back to the input being followed.

## Bluetooth MIDI and Bluetooth audio

Two pages, SYSTEM > BT MIDI (builder item `BLE`) and SYSTEM > BT AUDIO (`BT_AUDIO`, headphones and speakers), one radio: both can be on at once. Each page has **MODE**: OFF (the default), MAN (only you connect) or AUTO (also connects to the last device at start-up).

- **BT MIDI:** MAN or AUTO, the FM-1 advertises as "Optimist XXXX" (the last four digits of its address) and a Mac, an iPhone or an iPad connects to it as a Bluetooth MIDI device. With `BLE_CENTRAL` the FM-1 also connects out to BLE-MIDI keyboards and controllers; a device that wants a code gets a 6-digit passkey shown on the FM-1's screen.
- **The devices:** while a page is on screen with no link, the FM-1 scans and the page lists the devices, the remembered ones (up to 4) in green, new ones in amber. KNOB 2 moves the cursor, SAVE connects (or lets go of the link), SAVE held forgets a remembered device. On BT MIDI the list needs `BLE_CENTRAL`.

If the FM-1 restarts by itself within 30 s of starting, the next start leaves the radio off once, so you can set MODE to OFF.

The web editor works over USB. Over Bluetooth MIDI the FM-1 answers only the screen mirror, its buttons and the clip editor (the Optimist Mirror app, `ios/OptimistMirror`).

## USB audio

Builder item `USB_AUDIO`. On the same cable as MIDI, the computer sees two class-compliant audio devices, no driver needed, 44.1 kHz, 16 or 24 bit:

- **Optimist In:** four capture channels to the computer. SYSTEM > USB AUDIO has one line per source: MASTER (the output after the master inserts), T1 to T3 (a track's stem: after its inserts, level and mute, before pan, sends and master), DRUMS (the drum track's lanes summed), DRUM BUS, and each drum lane DR1 to DR10. Each source goes to NONE, 1, 2, 1+2, 3, 4 or 3+4. A stereo source on a pair goes left and right; on one channel, its two sides are averaged. A mono source on a pair is on both. Sources on one channel add up. By default T1 is on 1, T2 on 2, T3 on 3 and DRUMS on 4.
- **Optimist Out:** stereo from the computer into the FM-1's output (also usable as the **USB IN** mixer track, [effects](effects.md)).

A flash write (saving a preset or a project) pauses the audio for a moment: avoid it while recording.

## The web editor

[w0ts.github.io/optimist/webapp/editor/](https://w0ts.github.io/optimist/webapp/editor/#mixer), in Chrome or Edge, with the FM-1 on USB: **Connect**. What the editor can do depends on the builder items of your build. With live editing (`ED_LIVE`) every track, sound, drum sound, effect and setting is a knob or a menu, laid out as a mixer with a strip per track and the master, following the device live. The four flavours have `ED_LIVE` off: their editor does backup, restore, the librarian and the screen mirror (the **Screen** tab, `ED_MIRROR`).

- **Library**, **Samples** (with CHOP), **Projects**, **Settings**: screens of their own (`LIBRARIAN`, `ED_LIVE`).
- **Backup** (its own tab): everything the FM-1 keeps (projects and sections, the working project, user presets, your kits, the settings; tick *with the samples* for USR1 to USR3) in one `.optimist-backup` file. **Restore** lists what the file holds and writes what this firmware can read, then the FM-1 restarts on it. The installer offers a backup before it installs.
- **Simulator** (a tab): the firmware running in the browser, with the same panel. **Load from FM-1** copies your FM-1's memory into it, **Write to FM-1** writes it back. **Get a project from FM-1** copies one FM-1 project into the simulator (same number, opened); **Send this project to FM-1** saves the simulator's open project and writes it into the FM-1 project you pick, under the name you type (the FM-1 and the simulator both need a build with per-project patterns). And **Save to file / Load file** use the backup file format. The editor's device picker connects it to the simulator instead of an FM-1.

The protocol is [web/EDITOR_PROTOCOL.md](../../web/EDITOR_PROTOCOL.md).

# MIDI, Bluetooth, USB audio and the web editor

Where each setting is: GLOBAL > MIDI and Channels, SYSTEM > BT MIDI, BT AUDIO and USB AUDIO ([the interface](interface.md)).

## MIDI in

A keyboard or a DAW on USB or the TRS MIDI IN jack plays the FM-1. Each track has a channel (defaults: tracks 1-3 on channels 1-3, the drums on 10; OFF is silent). A channel no track has plays the selected track. A note always ends on the track it started on.

- MIDI notes go through the track's **SCL** settings like the FM-1's own keys (KEYS WHITE, ALL, SNAP; CHORD on: one key plays the chord of its degree). TRANSPOSE applies, the FM-1's OCT buttons do not.
- **Pitch bend** +-2 semitones (RPN 0 sets another range), **mod wheel** (a vibrato of its own), **sustain pedal** (CC64), and the panic messages CC120, CC121 and CC123. Drums ignore bend, wheel and sustain. A drum note plays the lane whose instrument it is ([drums](drums.md)).
- **MIDI OUT** (USB): KEYS sends what you play, SEQ also the sequencer, the arp and the rolls, each on its track's channel.

## MIDI clock

**CLK IN** (saved with the project): **AUTO** follows an external clock when one comes in (TRS first, then USB), else the FM-1's tempo; **INT** ignores clocks; **USB** or **TRS** follows only that input. Start begins at step 1 on the first pulse after it, Continue resumes at the song position sent, Stop stops. The tempo shown, the delay, the arp, the rolls and the slicer follow the measured tempo. The source changes only while stopped.

**CLK OUT** (off by default) sends MIDI clock on USB (the TRS jack is input only): 24 pulses a beat, also while stopped, Start and Stop with the transport, Song Position and Continue when it continues a song. The pulses never swing. Following a USB clock, nothing is sent back to USB; following TRS, the clock goes on to USB (a TRS-to-USB clock bridge).

## Bluetooth MIDI

Builder item `BLE` (in the Synth build), the page SYSTEM > BT MIDI. **MODE** is OFF by default: the radio does not even start. MAN or AUTO, the FM-1 advertises as "Optimist XXXX" (the last four digits of its address) and a Mac, an iPhone or an iPad connects to it as a Bluetooth MIDI device (tested with macOS Audio MIDI Setup). With `BLE_CENTRAL` the FM-1 can also connect out to a BLE-MIDI keyboard or controller: SAVE on the page opens the devices list, SAVE there connects; AUTO connects to the last one at boot. A device that wants a code shows a 6-digit passkey on the FM-1's screen. If the FM-1 restarts by itself within 30 s of starting, the next start leaves the radio off once, so you can set MODE to OFF.

**Bluetooth audio** (builder item `BT_AUDIO`, the page SYSTEM > BT AUDIO) has the same MODE, devices list and SAVE gestures ([the interface](interface.md#global-and-system)).

**Bluetooth and the second core:** both cores run with the radio on (the radio starts before the audio, in the stock firmware's order).

The web editor talks to the FM-1 over USB only.

## USB audio (experimental)

Builder item `USB_AUDIO` (in the Synth build). On the same cable as MIDI, the computer sees two class-compliant audio devices, no driver needed, 44.1 kHz, 16 or 24 bit:

- **Optimist In:** four capture channels to the computer. SYSTEM > USB AUDIO has one line per source: MASTER (the output after the master inserts), T1 to T3 (a track's stem: after its inserts, level and mute, before pan, sends and master), DRUMS (the drum track's lanes summed), DRUM BUS, and each drum lane DR1 to DR10. Each source goes to NONE, 1, 2, 1+2, 3, 4 or 3+4. A stereo source on a pair goes left and right; on one channel, its two sides are averaged. A mono source on a pair is on both. Sources on one channel add up. By default T1 is on 1, T2 on 2, T3 on 3 and DRUMS on 4.
- **Optimist Out:** stereo from the computer into the FM-1's output (also usable as the **USB IN** mixer track, [effects](effects.md)).

A flash write (saving a preset or a project) pauses the audio for a moment: avoid it while recording.

## The web editor

[w0ts.github.io/optimist/webapp/editor/](https://w0ts.github.io/optimist/webapp/editor/#mixer), in Chrome or Edge, with the FM-1 on USB: **Connect**. What the editor can do depends on the builder items of your build. With live editing (`ED_LIVE`) every track, sound, drum sound, effect and setting is a knob or a menu, laid out as a mixer with a strip per track and the master, following the device live. The Synth build has `ED_LIVE` off: its editor does backup and restore only. The drum machine build has no editor.

- **Library**, **Samples** (with CHOP), **Projects**, **Settings**: screens of their own (`LIBRARIAN`, `ED_LIVE`).
- **Backup** (its own tab): everything the FM-1 keeps (projects and sections, the working project, user presets, your kits, the settings; tick *with the samples* for USR1 to USR3) in one `.optimist-backup` file. **Restore** lists what the file holds and writes what this firmware can read, then the FM-1 restarts on it. The installer offers a backup before it installs.
- **Simulator** (a tab): the firmware running in the browser, with the same panel. **Load from FM-1** copies your FM-1's memory into it, **Write to FM-1** writes it back. **Get a project from FM-1** copies one FM-1 project into the simulator (same number, opened); **Send this project to FM-1** saves the simulator's open project and writes it into the FM-1 project you pick, under the name you type (the FM-1 and the simulator both need a build with per-project patterns). And **Save to file / Load file** use the backup file format. The editor's device picker connects it to the simulator instead of an FM-1.

The protocol is [web/EDITOR_PROTOCOL.md](../../web/EDITOR_PROTOCOL.md).

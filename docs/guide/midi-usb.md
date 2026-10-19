# MIDI, Bluetooth, USB audio and the web editor

Where each setting is on the Optimist UI: GLOBAL > MIDI and Channels, SYSTEM > Bluetooth, Devices and USB AUDIO
([the interface](interface.md)). On SLOOP's interface they are in the HOME menu > SYSTEM ([SLOOP's interface](sloop-ui.md)).

## MIDI in

A keyboard or a DAW on USB or the TRS MIDI IN jack plays the FM-1. Each track has a channel (defaults: tracks 1–3 on
channels 1–3, the drums on 10; OFF is silent). A channel no track has plays the selected track. A note always ends on the
track it started on.

- MIDI notes go through the track's **SCL** settings like the FM-1's own keys (KEYS WHITE, ALL, SNAP; CHORD on: one key
  plays the chord of its degree). TRANSPOSE applies, the FM-1's OCT buttons do not.
- **Pitch bend** ±2 semitones (RPN 0 sets another range), **mod wheel** (a vibrato of its own), **sustain pedal**
  (CC64), and the panic messages CC120, CC121 and CC123. Drums ignore bend, wheel and sustain.
- **MIDI OUT** (USB): KEYS sends what you play, SEQ also the sequencer, the arp and the rolls, each on its track's channel.

## MIDI clock

**CLK IN** (saved with the project): **AUTO** follows an external clock when one comes in (TRS first, then USB), else
the FM-1's tempo; **INT** ignores clocks; **USB** or **TRS** follows only that input. Start begins at step 1 on the
first pulse after it, Continue resumes at the song position sent, Stop stops. The tempo shown, the delay, the arp, the
rolls and the slicer follow the measured tempo. The source changes only while stopped.

**CLK OUT** (off by default) sends MIDI clock on USB (the TRS jack is input only): 24 pulses a beat, also while stopped,
Start and Stop with the transport, Song Position and Continue when it continues a song. The pulses never swing. Following
a USB clock, nothing is sent back to USB; following TRS, the clock goes on to USB (a TRS-to-USB clock bridge).

## Bluetooth MIDI

Builder item `BLE` (in the Synth build). **BLUETOOTH** is OFF by default: the radio does not even start. ON, the FM-1
advertises as "FM-1 XXXX" and a Mac, an iPhone or an iPad connects to it as a Bluetooth MIDI device (tested with macOS
Audio MIDI Setup). With `BLE_CENTRAL` the FM-1 can also connect out to a BLE-MIDI keyboard or controller: **DEVICES**
scans, lists what is nearby and connects; **RECONNECT** finds the last one again by itself. A device that wants a code
shows a 6-digit passkey on the FM-1's screen. If the FM-1 restarts by itself within 30 s of starting, the next start
leaves the radio off once, so you can switch it OFF.

**Bluetooth and the second core:** with the radio on, the FM-1's second core faulted on every boot in the hardware
tests, so in a Bluetooth build it runs only while BLUETOOTH is OFF. Switching it ON hands everything to the first core
until the next start (SYSTEM > INFO: 2ND CORE OFF BLUETOOTH): heavy projects have less headroom then.

The web editor talks to the FM-1 over USB only.

## USB audio (experimental)

Builder item `USB_MODE` 2 (in the Synth build). On the same cable as MIDI, the computer sees two class-compliant audio
devices, no driver needed, 44.1 kHz, 16 or 24 bit:

- **Optimist In:** four mono stems to the computer, one per track by default (after its inserts, level and mute,
  before pan, the sends and the master); SYSTEM > USB AUDIO picks each channel's source (the mix, a track, the drum bus,
  a drum lane).
- **Optimist Out:** stereo from the computer into the FM-1's output, after the master effects (also usable as the
  **USB IN** mixer track, [effects](effects.md)).

A flash write (saving a preset or a project) pauses the audio for a moment: avoid it while recording.

## The web editor

[w0ts.github.io/optimist/webapp/editor/](https://w0ts.github.io/optimist/webapp/editor/#mixer), in Chrome or Edge, with
the FM-1 on USB: **Connect**, then every track, sound, drum sound, effect and setting is a knob or a menu, laid out as a
mixer with a strip per track and the master. It follows the device live. A build made with `ED_LIVE=0` (the Synth build
is one) has the editor's backup and restore only.

- **Library**, **Samples** (with CHOP), **Projects**, **Settings**: screens of their own.
- **Backup** (Projects): everything the FM-1 keeps (projects and sections, the working project, user presets, your kits,
  the settings; tick *with the samples* for USR1–USR3) in one `.optimist-backup` file. **Restore** lists what the file
  holds and writes what this firmware can read, then the FM-1 restarts on it. The installer offers a backup before it
  installs.
- **Simulator** (a tab): the firmware running in the browser, with the same panel. **Load from FM-1** copies your FM-1's
  memory into it, **Write to FM-1** writes it back, and **Save to file / Load file** use the backup file format. The
  editor's device picker connects it to the simulator instead of an FM-1.

The protocol is [web/EDITOR_PROTOCOL.md](../../web/EDITOR_PROTOCOL.md).

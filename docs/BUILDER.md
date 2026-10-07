# Firmware builder

Pick the engines, FX, drum kits, sample sets and features that go into your FM-1 firmware, with a live flash,
RAM, pool and RAMTEXT budget, then build it. Design and inventory: `docs/BUILDER-DESIGN.md`;
the memory budget of the full integration: `docs/MEMORY-BUDGET.md`.

All numbers come from the emulator and host builds: there is no real FM-1 here yet.

## Run it

```sh
python tools/optimist.py setup                    # once: the venv, the SDK files, the toolchain (BUILDING.md)
python tools/optimist.py builder                  # the interactive menu (Textual)
python tools/optimist.py builder --profile drum-machine   # the menu, starting from a profile
python tools/optimist.py builder --config my.config       # the menu on a saved configuration
```

`tools/optimist.py` runs the same on macOS, Linux and Windows. On macOS and Linux `tools/menuconfig` (and
`make builder`) still work and call it.

Keys: `space` / `enter` toggle (a sized item: its next value), `/` search, `p` profiles (shipped + yours), `s` save as your own profile (config/my-profiles, git-ignored; then `--profile NAME` works too), `u` publish the profile or not (CI builds the published ones; one of yours is moved to config/profiles first, to commit), `d` delete a profile (type yes; never user-default), `w` write a .config file, `l` load,
`b` build, `e` build and run the new firmware in the emulator (96 MHz, in the background), `x` / `c` expand / collapse all, `q` quit. The menu opens with every item that has options expanded; what you collapse stays collapsed when the list is rebuilt (a search, a profile change), and a search expands the matches.

The bars show the estimate from the measured deltas (`tools/builder/costs.json`), red with "OVER by n" when a
region overflows; the message panel then names the biggest items of that region. `b` runs the real build
(about 10 s): the exact sizes replace the estimate, and when it fits the package is `build/felucca.fwsc`, with
its web editor sidecar `build/felucca-ui.zip` (index.html, the font and its licence, SOURCE.txt: the commit and
the configuration's hash; the emulator's fm1-ui serves it beside the firmware).

A configuration error marks the lines of the items it concerns as soon as it holds, in red after the item
(`✗ the drum track needs a drum source: …`; an option's error marks its parent too, in case it is folded),
and the message panel lists it; fixing it clears the marks at once. `b` on an invalid configuration still
says CANNOT BUILD in the panel and keeps it there, like a build's result, until the next `b`.
(`configure.validate()` returns each message as an `Issue`: a string with `.keys`, the items it names.)

Without the menu (scripts, tests, CI): `python tools/optimist.py build | package | config ...` (BUILDING.md,
"Quick start"), for example

```sh
python tools/optimist.py package --config my.config --out dist --summary dist/result.json   # exit 0 = built and fits
```

or, as before on macOS and Linux:

```sh
tools/menuconfig --list                               # the registry with the default values
tools/menuconfig --profile fm-va-studio --budget      # estimate
tools/menuconfig --profile fm-va-studio --set FX_PUNCH=0 --write my.config
tools/menuconfig --config my.config --build           # real build: exact sizes, the package if it fits
tools/menuconfig --profile everything-that-fits --fit # drop items (least loss first) until it fits
sh build.sh --config my.config                        # the same build, directly
tools/menuconfig --profile drum-machine --package ~/GitHub/fm1-firmware   # optimist-drum-machine-<date>.fwsc + -ui.zip
```

The old environment switches still work and override the `.config` (`FELUCCA_ICONS=0 sh build.sh`).

## Published profiles

A shipped profile (`config/profiles`) with the line `# publish: yes` is built by CI
(`.github/workflows/build.yml` takes the list from `config --published`); `# publish: no` or no line: not built.
Your own profiles (`config/my-profiles`) are git-ignored, so CI never sees them: share one first (it moves to
`config/profiles`), then publish it, and commit the file.

```sh
make profiles                       # python tools/optimist.py config --profiles
make share PROFILE=my-groove        # config --share my-groove
make publish PROFILE=my-groove      # config --publish my-groove   (unpublish: --unpublish)
make delete PROFILE=my-groove       # config --delete my-groove    (mine first, else the shipped one)
```

In the menu: load the profile (`p`), then `u` (shares one of yours first).

## What is switchable

`tools/builder/registry.py` is the list; `tools/menuconfig --list` prints it. One level only: a top-level
item and its own options (FM6's modes, the sampled kits, the delay length...); an option is ignored while its
parent is off, and no option depends on another item.

| Group | Items |
|---|---|
| Synth engines | ANALOG 2, DIGITAL, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, FM6, SLICE, PHYS, ACID, CZ (at least one) |
| FM6 options | MARK I / MODERN / OPL modes (at least one; ENGINE lists only those built), MARK I tables in flash (off: CPU cost on the FM-1 not measured), black-key editor, its VIEW ALL rows, the algorithm long press, DX7 SysEx, factory voices, user bank STORE, user presets that keep their voice (UP_FM6, Felucca 1.0.3 idea) |
| Drums | drum synth (all synthesised kits: one switch), sampled drums (one switch per kit), sound editor, user samples on lanes, user kits (at least one drum source; each lane's REV / DLY / CHO sends are in every build, no longer an item); the X0X 909 kit (its ride and crash samples: 8-bit, 6-bit or none) and the X0X 808 kit (their voices also on any lane; four style kits each, UIDs 39..46) (EXPERIMENTAL, off by default; see below) |
| Sample sets | PIANO, BASS, VIBES, HORNS, STRINGS, FLUTE, SCRATCH (PERC goes with the sampled kits) |
| FX | DIST, chorus, delay (length; halving when longer than the line), reverb (one checkbox per algorithm: ROOM, PLATE, FDN8, SPRING, at least one; two or more: TYPE on the device and in the web editor; one shared line buffer; half rate at 22.05 kHz; its buffers in the pool instead of main RAM), SLICER (capture), PUNCH (ring; its LATCH, Felucca 1.0.2 #40), DJ filter, DUST, DUCK, BASS+, mixer glides (X0X 0.10.1, EXPERIMENTAL) |
| MIDI & USB | USB port: CDC console / USB audio (EXPERIMENTAL; its resampler) / MIDI only; TRS MIDI IN; MIDI clock; MIDI expression; a MIDI channel for each track, the sequencer to MIDI OUT, MIDI IN clock only (SLOOP 2.4); USB MIDI flow control, TRS input past line noise (SLOOP 2.3) |
| Sequencer | song sections (16 / 8 / 4), snapshots (0 / 2 / 4 / 8 whole-state slots), undo history, per-step chance, QNT SEQ, motion recording (its card mark, Felucca 1.0.2 #63), performance macros (GLO > MACRO; its ENERGY bands), the REC screen's dials and count-in (SLOOP 2.3) |
| UI | boot logo, parameter icons, VIEW ALL overview (4 x 4 PAGEs; its ARP graph), the MISSING message, the knob's help line (PARAM_HELP), chord names on the STEP page (isod89/sloop-fm1 PR #45), the drum step sequencer (DRUM_STEP, off by default), knob acceleration, screen SPI clock, changed-rectangle screen updates, keys lit by the notes played, brightness, LIGHTS / KEYS / NOTES, keys read with their column, the knobs' one rest state (SLOOP 2.3), knobs quiet as a layer is let go, BPM LOCK, divisions in length order (Felucca 1.0.2 #39, #58, #48) |
| System | web editor and firmware updates (OTA), backup / restore, CPU sleep between polls (IDLE), smaller UI and storage code (SIZE), assembly speed-ups (ASM; SIMD: EXPERIMENTAL), stricter flash read-back, the overload fade, no stuck note after a VOICE change, a restore checked object by object (SLOOP 2.3), predictive CPU guard (off; docs/CPU-GUARD.md) |
| Experimental | dual core |

The X0X kits' UIDs (37, 38) and names are in every build, built or not: a project or kit naming one keeps it,
plays a stand-in, and the MISSING warning names it. That base saves 128 B of flash on user-default (581,424 against
581,552): the kits' names and the drum lanes' source names are one table (drums.c DRUM_SRC_NAMES), and the stand-in
choice is one out-of-line drum_kit_of. With DRUM_X0X909 and DRUM_X0X808 at 0, felucca.bin and data.bin are
byte-identical to that base (compared on user-default and drum-machine) but for one word, FELUCCA_CFG_HASH: the
configuration's hash, which now covers the two new items.

Errors: FM6 without an ENGINE mode; no drum source. Warnings the menu gives: FM6 without its editor and
without SysEx is preset-only; MARK I tables in flash without MARK I do nothing; sample sets without SAMPLE or GRAIN
play nowhere; SAMPLE / GRAIN without their presets' sets get a preset of their own (below); OTA off removes the
web editor and the update path; experimental items are emulator-tested only.

Every engine a build has keeps at least one entry on the PRESETS list (and in the web editor's preset list), and
every drum source at least one kit. An engine with no factory preset this build can play (none in its table, or
none whose sample set is built) shows one entry, INIT: loading it sets the engine's and the sound's defaults, as a
fresh track on that engine. INIT is made from the defaults: no preset data, no flash in a build that does not need
it. GRAIN without PIANO, VIBES and FLUTE but with another melodic set gets a real preset instead, GRAIN PAD on the
first of them (its sound beats a silent INIT); SAMPLE with no set at all, or GRAIN with no melodic set, plays only
the USR slots: INIT.

### Every item, in plain words

What each item does for you, what it costs and what you lose when it is off: the text of the menu's details panel
(`desc` in `tools/builder/registry.py`, `tools/builder/backports.py`), one table a group. Sizes are about, from
`tools/builder/costs.json`; a `(sub-item)` is an option of the item above it, ignored while that item is off.
Items marked EXPERIMENTAL are emulator-tested only. The `tests/builder_test.py` check keeps every item described.

#### Synth engines

| Item | Key | What it does |
|---|---|---|
| ANALOG 2 (analog-style synth, swarm, 2 filters) | `ENG_ANALOG` | The main subtractive synth: two oscillators with hard sync, a swarm of up to 6 detuned copies (supersaw), two filters. About 6 KB of flash and 4.7 KB of fast RAM code; it is also the stand-in that most other engines fall back to, so keep it unless you need the space. |
| DIGITAL (4-operator FM) | `ENG_DIGITAL` | Four-operator FM synth: 8 classic algorithms, feedback on operator 4 and one modulation INDEX with its own envelope. About 2 KB of flash; the stand-in for FM6 when FM6 is left out. |
| PHASE (phase distortion) | `ENG_PHASE` | Phase-distortion synth: bends the phase of a cosine wave to get saw, square, pulse and resonant shapes with a second line to mix or ring-modulate. About 1.4 KB of flash; the stand-in for CZ. |
| LOFI (chip sounds, wavetable) | `ENG_LOFI` | Chip voice: pulse, triangle, saw, noise and a 4-bit wavetable, with a few-bit amplitude, a held (low) sample rate and a pitch sweep. About 2 KB of flash. |
| SAMPLE (sample sets, USR slots) | `ENG_SAMPLE` | Plays the built-in sample sets and your own samples (USR1..USR3 slots) across the keyboard, with loops. About 1.4 KB of flash plus the sets you keep (see Sample sets); it is also the stand-in for GRAIN and SLICE. |
| VOICE (formant, sung vowels) | `ENG_FORMANT` | Formant synth that sings vowels from the keyboard (4 voices; BUZZ and BREATH shape the voice). About 2.7 KB of flash; a special sound, leave it out if you need the space. |
| TRIO (3 oscillators) | `ENG_TRIO` | Three oscillators with ring modulation and hard sync into a multimode filter, in the style of early 8-bit home-computer sound chips. About 3.4 KB of flash. |
| WHEEL (drawbar organ) | `ENG_DRAWBAR` | Tonewheel-style organ: nine sine drawbars per note, 16 registrations to choose from. About 2.3 KB of flash and 2.5 KB of RAM. |
| GRAIN (granular on the sample sets) | `ENG_GRAIN` | Granular synth: plays clouds of tiny grains cut from the sample sets or your USR slots. About 4.9 KB of flash and 21 KB of pool; with no sample set built it plays your USR samples only. |
| FM6 (DX7, bit-exact with Dexed) | `ENG_FM6` | Six-operator FM that plays Yamaha DX7 voices the way the Dexed plug-in does, with its own operator editor and DX7 SysEx. The biggest engine: about 32 KB of flash, 10 KB of RAM and 12 KB of pool; its options below trim it down. |
| (sub-item) FM6 mode MARK I (DX7's own tables) | `FM6_MARK1` | The FM6 ENGINE mode that uses the DX7's own log-sine and exponent tables and its feedback behaviour, for the most DX7-like sound. Costs about 3.2 KB of flash and 4.1 KB of RAM; off: voices asking for it play another mode you kept. |
| (sub-item) FM6 MARK I tables in flash | `FM6_MKI_FLASH` | with MARK I: its log-sine and exponent tables as generated const data in flash instead of RAM tables built at boot (4 KB of RAM less, 1.9 KB of flash more). CPU cost on the FM-1 unknown: the emulator models neither the XIP cache nor flash wait states |
| (sub-item) FM6 mode MODERN (clean 24-bit) | `FM6_MODERN` | The FM6 ENGINE mode with Dexed's modern 24-bit maths (MSFA). Costs only about 0.6 KB of flash (the sine table stays: the LFO uses it); keep at least one of the three modes. |
| (sub-item) FM6 mode OPL (grittier, chip resolution) | `FM6_OPL` | The FM6 ENGINE mode with the resolution of Yamaha OPL chips, for a different tone from the DX7 modes. Costs about 1.9 KB of flash and 1.5 KB of RAM (tables). |
| (sub-item) FM6 operator editor on the black keys | `FM6_KEYS` | Edit a DX7 voice on the device: hold ENV and press a black key to pick an operator, the pitch envelope or the global page, then turn the knobs. About 7.8 KB of flash; off: voices can only be changed by DX7 SysEx or the web editor. |
| (sub-item) FM6 DX7 SysEx in / SEND (web editor FM6 tab) | `FM6_SYSEX` | Accepts DX7 voice, bank and parameter changes over USB MIDI, so Dexed or any DX7 librarian can edit a part live, and can SEND the voice back; the web editor's FM6 tab needs it. About 1.2 KB of flash; its 4.1 KB RAM buffer is shared with STORE. |
| (sub-item) FM6 16 factory voices R01..R16 | `FM6_VOICES` | The 16 built-in DX7 voices R01..R16 (about 2 KB of flash). Off: the R slots play INIT VOICE, a plain starting voice; your own bank (U voices) is not affected. |
| (sub-item) FM6 operator editor in VIEW ALL (pages as rows) | `FM6_ALL` | with VIEW ALL: an operator's pages (or PIT, GLO) as rows of 4 x 4 PAGEs; off: one page at a time (saves about 1.2 KB of flash) |
| (sub-item) FM6 algorithm full screen (hold ENV) | `FM6_ALGO` | Holding ENV a while on an FM6 track shows the algorithm full screen, with operators numbered and carriers, modulators and the feedback loop told apart. Only 48 B of flash; off: holding ENV does nothing extra. |
| (sub-item) FM6 user bank STORE (U01..U32 in a USR slot) | `FM6_STORE` | STORE your edited voices into a user bank of 32 (U01..U32), kept in a USR sample slot. About 0.6 KB of flash and 0.5 KB of RAM; off: no user bank (STORE says NO USER BANK). |
| (sub-item) FM6 user presets keep their voice | `UP_FM6` | FM6 user presets keep their whole voice (operator edits included) instead of only the VOICE number. About 0.7 KB of flash plus a 3.6 KB store in flash; a preset written from the web editor drops its kept voice. |
| SLICE (break slicer + its BREAK sample) | `ENG_SLICE` | Break slicer: cuts a loop (the built-in BREAK, 22 KB of samples, or a USR slot) into 4 / 8 / 16 / 32 slices or one per hit and plays them from the keys or the sequencer. About 27 KB of flash and 6 KB of RAM; off by default. |
| PHYS (physical models) | `ENG_PHYS` | Physical-modelling engine (engine 11): modal, string, membrane and sympathetic-string models, 3 voices per part. Heavy: about 9.8 KB of flash and 38.7 KB of pool, and 3 voices of its heaviest preset take about 17 % of the audio budget at 312 MHz (at 96 MHz that was too much and voices were shed). |
| ACID (TB-303 bass + line generator) | `ENG_ACID` | X0X's TB-303-style bass voice (one monophonic voice per part, engine 12) with the TB-3PO line generator. Experimental: floating-point DSP (about 14.4 KB of flash, 1.7 KB of RAM) that ran on a real FM-1 in X0X but has not been tried here; no pitch bend or TUNE on the 303; 4 presets. |
| CZ (CZ-1 style tones) | `ENG_CZ` | Casio CZ-1 style engine from Melodee 0.11 (engine 13): two lines with 8-step envelopes, 8 built-in tones (TONE) changed by the EDIT values; no envelope editing, banks or SysEx yet. About 7 KB of flash and 3.1 KB of RAM; similar CPU to FM6 (emulator). |

#### Drums

| Item | Key | What it does |
|---|---|---|
| drum synth (all 32 synthesised kits) | `DRUM_SYNTH` | The 32 synthesised drum kits (808, 909 and others), generated by the firmware, so they take no sample memory. About 13.7 KB of flash; the drum track needs this or at least one sampled kit. |
| sampled drums (the PERC set, 99 KB) | `DRUM_SAMPLED` | The recorded drum samples (the PERC set, about 99 KB of flash) behind the five sampled kits below. Switching this off drops all of them; the drum track then needs the drum synth. |
| (sub-item) sampled kit ACOUSTIC | `KIT_ACOUSTIC` | The sampled kit as recorded (the basic kit of the PERC set). It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit DEEP | `KIT_DEEP` | The sampled kit tuned down and softened by a low-pass filter: a darker, heavier kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit TIGHT | `KIT_TIGHT` | The sampled kit with every hit cut short by a fast decay: a dry, punchy kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit BRIGHT | `KIT_BRIGHT` | The sampled kit tuned two semitones up: a lighter kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit DUST | `KIT_DUST` | The sampled kit tuned slightly down, filtered and reduced to a coarse bit depth: a gritty lo-fi kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| drum sound editor (EDIT on the drum track) | `DRUM_EDIT` | Per-lane sound tweaks over the kit (TUNE, DECAY, SNAP, CLICK, BEND, CUT, DRIVE, LEVEL on synthesised kits; TUNE, DECAY, CUT, LEVEL on sampled ones). About 1.2 KB of flash; off: kits play as they are and saved tweaks are kept but not applied. |
| user samples on drum lanes | `DRUM_USR` | A drum lane can play one of your own samples (a hit of a USR slot, with start and length) instead of the kit's sound. About 1.2 KB of flash; off: such lanes play the kit sound. |
| user drum kits (bank of 16 in data flash) | `DRUM_KITS` | Lane sources from other kits and the X0X machines, and a bank of 16 user kits (each lane's source and sound tweaks) saved in flash. About 2.8 KB of flash; off: lanes play the kit as it is. |
| X0X 909 kit (circuit-modelled TR-909) (EXPERIMENTAL) | `DRUM_X0X909` | X0X's TR-909 drum kit (kit UID 37; the drum models are circuit-modelled, hi-hats, ride and crash are samples), on the 16 lanes; SHAKER, CONGA, COWBELL play the synthesised 909's. Big: about 151 KB of flash (see its cymbal option) and uses floating point; emulator-tested only. A build without it plays the synthesised 909 for it and keeps the kit |
| (sub-item) X0X 909: ride and crash sample quality | `X909_CYM` | 8-bit block floating point as before; 6-bit: 22 KB less, 30 dB against the 16-bit source instead of 42 (screens x0xdrums-perf-2026-10-06); off: RIDE and CRASH play the synthesised 909's (the hi-hat samples stay) Values: 8-bit (93 KB, 42 dB); 6-bit (70 KB, 30 dB); off. |
| X0X 808 kit (circuit-modelled TR-808) (EXPERIMENTAL) | `DRUM_X0X808` | X0X's TR-808 drum kit (kit UID 38; 16 circuit-modelled sounds) on the 16 lanes; MIDI also plays MT, LC, HC and the claves (note 75). About 30 KB of flash and uses floating point; emulator-tested only. A build without it plays the synthesised 808 for it and keeps the kit |

#### Sample sets

| Item | Key | What it does |
|---|---|---|
| PIANO (Steinway, 44 KB) | `SET_PIANO` | A grand piano, plus the dusty and lo-fi piano variants. Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its flash, and it can still be uploaded to a USR slot. |
| BASS (39 KB) | `SET_BASS` | An upright bass, plus a deep bass variant. Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its flash, and it can still be uploaded to a USR slot. |
| VIBES (33 KB) | `SET_VIBES` | A vibraphone. Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its flash, and it can still be uploaded to a USR slot. |
| HORNS (26 KB) | `SET_HORNS` | Horn stabs. Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its flash, and it can still be uploaded to a USR slot. |
| STRINGS (20 KB) | `SET_STRGS` | String stabs. Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its flash, and it can still be uploaded to a USR slot. |
| FLUTE (31 KB) | `SET_FLUTE` | A lo-fi flute. Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its flash, and it can still be uploaded to a USR slot. |
| SCRATCH (23 KB) | `SET_SCRCH` | Turntable scratch hits. Played by the SAMPLE and GRAIN engines (and their presets); leaving it out frees its flash, and it can still be uploaded to a USR slot. |

#### FX

| Item | Key | What it does |
|---|---|---|
| DIST (per-track drive) | `FX_DIST` | A distortion on each synth track: low cut, drive from 1x to 8x into an asymmetric soft clip, and a tone filter that closes as the drive rises. About 0.4 KB of flash; off: the DIST controls disappear. |
| chorus send bus | `FX_CHORUS` | The chorus effect, fed by each track's CHO send. About 0.4 KB of flash and 4 KB of pool; off: the CHO sends and the chorus settings disappear. |
| delay send bus | `FX_DELAY` | The tempo-synced delay (echo), fed by each track's DLY send. Costs 128 KB of the pool at the full length: shorten it with the length option, or leave it out. |
| (sub-item) delay length (how long an echo can be) | `DLY_LEN` | The longest delay time, which sets the pool memory the delay takes. A time longer than the line is halved (see 'long delay times halve') or cut; shorter lines suit fast tempos only. Values: 1.49 s (128 KB pool); 0.74 s (64 KB); 0.37 s (32 KB). |
| (sub-item) long delay times halve to stay on the beat | `DLY_HALVE` | A delay time longer than the delay line is halved (so it stays on the beat) instead of being cut off; it matters with the shorter delay lengths (with 0.74 s, 1/4 below 81 BPM plays as 1/8). Costs 64 B of flash; off: the time is cut at the line's length. |
| reverb send bus | `FX_REVERB` | The reverb, fed by each track's REV send and the drums' reverb, with its algorithms below (tick at least one). Each one ticked is on the device's FX > REVERB > TYPE and in the web editor's Reverb popup, switched while it plays; with one ticked there is no TYPE. They share one line buffer, sized to the largest. The biggest user of RAM among the FX (about 17 KB with ROOM); off: the REV sends and reverb settings disappear. |
| (sub-item) reverb: ROOM | `REV_ROOM` | Four delay lines at 44.1 kHz, the reverb as it always was: sparse for its first ~300 ms (separate echoes, a grainy start), then the tail. Its 17 KB of lines set the shared buffer's size when it is ticked. Off, with another algorithm ticked: about 1.8 KB of flash, 2.4 KB of RAM and 2 KB of pool saved. |
| (sub-item) reverb: PLATE (Dattorro) | `REV_PLATE` | Dattorro's figure-of-eight plate at 22.05 kHz: dense from ~50 ms, a smooth decay, nothing above ~11 kHz, about 10 % fewer instructions than ROOM. A 16 KB ring (8 KB at half rate) in the shared buffer. Beside ROOM: about 2.9 KB of flash and 3.4 KB of RAM; with two or more algorithms each one's code runs from main RAM. Experimental: measured on the host and in the emulator only, not yet heard on an FM-1. |
| (sub-item) reverb: FDN8 (long, lush) | `REV_FDN8` | Long and lush: eight slowly modulated lines at 22.05 kHz, dense from ~50 ms, the widest and least ringing tail, nothing above ~11 kHz. Up to SIZE 90 the ROOM's decay; above it the decay doubles every 10 steps to ~14 s at 126 and a near-freeze at 127, the treble kept as DAMP says; about 20 % more instructions than ROOM. A 16 KB ring (8 KB at half rate) in the shared buffer. Beside ROOM: about 4.0 KB of flash and 4.3 KB of RAM (with PLATE too, 1.1 KB of flash and 1.8 KB of RAM are shared). Experimental: measured on the host and in the emulator only, not yet heard on an FM-1. |
| (sub-item) reverb: SPRING | `SPRING` | A spring-tank reverb, one of the reverb's algorithms: the chirp and drip of a guitar amp's spring (SPRNG on FX > REVERB > TYPE when two or more are ticked). Its output is mono, and it is a little lighter on CPU than ROOM (emulator). Beside ROOM: about 1.9 KB of flash and 2.3 KB of RAM; ticked alone it is the only reverb. |
| (sub-item) reverb buffers in the pool (saves ~17 KB RAM) | `REV_POOL` | Keeps the reverb's shared line buffer (17 KB with ROOM, 8.7 KB at half rate; PLATE and FDN8 without ROOM 16 KB, 8 KB at half rate; SPRING alone 8 KB) in the pool instead of main RAM: main RAM is the scarcer, and the sound and the code stay the same. Needs that much pool free (the undo history shrinks by it in the pool and grows by it in RAM). |
| (sub-item) half-rate reverb (half the RAM, no top octave) | `REV_HALF` | Runs the ROOM reverb at half the sample rate (22.05 kHz) behind a half-band filter: its memory takes half the RAM (-8.7 KB) and it costs less CPU, with the same decay and room size. PLATE and FDN8 already run at 22.05 kHz; here they get half the ring (8 KB), a smaller tank with more audible modes. The reverb loses its top octave (above ~11 kHz); the dry sound and the other buses are untouched. |
| SLICER (stutter / gate insert) | `FX_SLICER` | A tempo-synced 16-step gate or stutter on each track (not the sample slicer engine): chops the sound to a pattern. About 1.2 KB of flash and 32 KB of pool at the full capture length. |
| (sub-item) SLICER capture length (stutter memory) | `SL_LEN` | How much sound each track's stutter records to repeat: the shorter capture saves 16 KB of pool and limits how long a repeated chunk can be (the gate mode is unaffected). Values: 186 ms (32 KB pool); 93 ms (16 KB). |
| PUNCH (16 punch-in FX) | `FX_PUNCH` | Hold FX and press a white key to put the whole mix through one of 16 effects while it is held (loops, reverse, tape stop, half speed, wobble, echo, filters, crush, gate), beat-synced. About 2.3 KB of flash and 64 KB of pool at the full ring. |
| (sub-item) PUNCH memory length (how much mix it can loop) | `PUNCH_N` | How much of the mix the loop, reverse, tape-stop and echo effects can capture; the shorter ring saves 32 KB of pool and cuts the longest loop in half. Values: 0.74 s (64 KB pool); 0.37 s (32 KB). |
| (sub-item) punch LATCH: FX + key latches its effect | `PUNCH_LATCH` | PUNCH: FX + a key latches its effect so you can let go; the same key or FX + OCT- turns it off, another key switches to its effect, and FX stays lit while one plays. About 80 B of flash. |
| DJ filter (MASTER FILT) | `FX_DJF` | One knob on the master (MASTER > FILT): turn left for a low-pass closing, right for a high-pass opening, centre is off. About 0.7 KB of flash. |
| DUST (vinyl / lo-fi master) | `FX_DUST` | One knob on the master (MASTER > DUST) that runs the mix through an old sampler and a record: drive, lower sample rate, fewer bits, a darker tone, hiss and crackle while playing. About 0.7 KB of flash. |
| DUCK (kick ducks the parts) | `FX_DUCK` | Pumping sidechain effect (MASTER > DUCK): every kick from the drum track dips the synth parts, which swell back over an eighth note. About 0.1 KB of flash; has no effect without a drum kick. |
| COMP + LIMIT (master compressor, brickwall limiter) | `MASTER_COMP` | A compressor and a brickwall limiter on the master (GLO > COMP: THRS, RATIO, ATK, REL; GLO > LIMIT: GAIN, CEIL and a GR readout) to glue the mix and keep its peaks under a ceiling; a Comp button and a GR meter in the web mixer. Projects that leave it off sound as before. About 2 KB of flash and 2.5 KB of RAM. |
| BASS+ speaker mode | `BASSPLUS` | A third MENU > LOWCUT setting (OFF / LOWCUT / BASS+) for the FM-1's small speaker: it adds harmonics of the bass below ~150 Hz, which the speaker can play, and raises the low cut to ~220 Hz. About 0.5 KB of flash and 0.35 KB of RAM; CPU only while BASS+ is on. |
| mixer glides ~10 ms (no zipper) (EXPERIMENTAL) | `GLIDE` | Part level, pan and sends, the master volume and the drum track's level, pan and sends glide over ~10 ms instead of jumping, which removes the zipper noise of a moving knob. About 1.3 KB of flash, 0.3 KB of RAM and 0.7 KB of fast RAM code; the sound changes only while a gain moves. |

#### Sequencer

| Item | Key | What it does |
|---|---|---|
| song sections | `SECTIONS` | 8 / 16: banks of 4 (SAVE + OCT), stored compressed in one 32 KiB log with a MEM gauge; the old slots move in at the first start. 4: the slots as before (needed by motion recording) Values: 16: A..P, compressed log; 8: A..H, compressed log; 4: A..D, the old project slots. |
| snapshots (whole-state slots) | `SNAPSHOTS` | SAVE > SNAPSHOT: the working project, every section and the song in one slot, loaded back whole (the state before is kept in BEFORE LOAD); export / import in the web editor. The flash comes from the end of USR3 (slots + 4 sectors of 4 KiB): the user sample slot USR3 holds that much less (docs/SNAPSHOTS.md) Values: 4 slots: 32 KiB, USR3 keeps 32 KiB; 2 slots: 24 KiB, USR3 keeps 40 KiB; 8 slots: 48 KiB, USR3 keeps 16 KiB; off: USR3 64 KiB. |
| undo / redo history (many levels) | `UNDO_HISTORY` | Undo and redo of pattern edits (EDIT + OCT- / OCT+) over many levels; the history lives in the pool and RAM this build leaves free (at least 1 KiB). About 2.2 KB of flash; off: a single undo level. |
| performance macros (GLO > MACRO) | `MACROS` | COLOR, MOTION, SPACE, ENERGY: four knobs, each moving several sounds' parameters at once (filters and FM index, LFO depths, sends and width, drive and drum level), kept per project and section (the drum track's unused ENV / LFO DEST values: no format change); recorded by motion recording. At home: no change. About 1.4 KB of flash and 0.4 KB of RAM. |
| (sub-item) ENERGY bands thin / thicken the drums | `ENERGY` | ENERGY also thins or thickens the drum pattern in five bands, walked on the beat: core lanes on the eighths, no ghosts, as written, harder hits, hat ratchets and a snare fill every second pass. About 0.6 KB of flash. |
| per-step chance (STEP 2 PROB) | `CHANCE` | Gives each synth step a chance to play (SEQ > STEP 2, KNOB 2: 0 to 100 % in 5 % steps); a step that fails plays as a rest. About 0.5 KB of flash; nothing changes until a step's chance is turned down. Synth tracks only; the web editor does not show it yet. Off: every step plays (chances stay saved). |
| SCL > QNT SEQ (sequenced notes snap) | `QNT_SEQ` | SCL > QNT SEQ: the keys and the sequenced notes snap to the scale as they play, so a pattern follows a change of ROOT or SCALE; the steps keep their notes as written. About 0.3 KB of flash; not on GM KIT or SLICE parts. A build without it plays a project's QNT SEQ as ALL. |
| motion recording (knobs per step) | `MOTION` | Motion recording: while recording, knob turns are stored per step and replayed on every pass (SEQ > MOTION: play on / off per track, clear). 64 events for the four tracks together; not editable from the web editor and edits are not undoable. About 3.4 KB of flash, 0.5 KB of RAM and 1.4 KB of pool. |
| (sub-item) mark the parameters motion recording moves | `MOTION_MARK` | With motion recording: a small square in the track colour marks the parameter cards that the track's motion moves (page and VIEW ALL). About 0.3 KB of flash. |
| no stuck note after a VOICE change | `MONO_RELEASE` | Fixes a stuck note when you let a key go just after a VOICE change (MONO stack). No cost. |
| REC screen dials: mode, length, count-in | `REC_MODES` | REC screen dials: MODE (free / tempo), LENGTH (1 / 2 / 4 bars) and START (note / 4-3-2-1 count-in). The count-in runs on the internal clock only and in 4/4. About 1.2 KB of flash; off: free recording started by a note. |

#### MIDI & USB

| Item | Key | What it does |
|---|---|---|
| USB port | `USB_MODE` | What the USB port offers besides MIDI. USB audio (default, experimental): 4 stems in + stereo out (UAC1); it replaces the console and needs +12 KB pool. CDC console: a read-only serial console for diagnostics. MIDI only: neither (smallest). Values: CDC serial console; USB audio (EXPERIMENTAL); MIDI only. |
| TRS MIDI IN | `UART` | MIDI input on the TRS jack (notes and clock from a keyboard or sequencer). About 0.6 KB of flash; off: the jack hears nothing, and SYNC TRS has no clock. |
| MIDI clock in (SYNC AUTO TRS > USB > INT) | `MIDI_CLOCK` | Follow an external MIDI clock and start / stop (GLO > SYSTEM SYNC: AUTO takes the TRS jack, else USB, else the internal tempo). About 4.2 KB of flash; off: the FM-1 always runs on its own tempo. |
| USB audio: resample to the host clock | `UA_RESAMPLE` | Experimental, only with USB audio: the audio sent to the computer is resampled to follow the computer's clock instead of using packet-size feedback. CPU-heavy (a resampler in the audio path); about 0.4 KB of flash. |
| MIDI expression (bend, mod, sustain, RPN) | `MIDI_EXPR` | Plays more than notes from MIDI: pitch bend, mod wheel, breath, foot, aftertouch, sustain pedal and pitch-bend range (RPN 0). About 0.5 KB of flash; off: only notes and the panic messages work. |
| a MIDI channel for each track (in and out) | `MIDI_CH` | Each track has its own MIDI channel, 1 to 16 or OFF, set in the HOME menu (MIDI CHANNELS): the channel a note must come in on to play that track, and the one its keys (and the sequencer, with MIDI OUT = SEQ) send on. Saved in the project; the defaults are today's (parts 1 2 3, drums 10), so older projects sound as before. OFF means nothing in and nothing out. The drum track's channel is the old DRUMS CH. Off: parts 1 2 3 and the drum channel as before. About 0.7 KB of flash, 16 B of RAM, 4 B of fast RAM code. |
| MIDI OUT = SEQ: the sequencer to MIDI out | `MIDI_OUT` | HOME menu > MIDI OUT: KEYS (as before: only the keys go out) or SEQ: what the sequencer, the arpeggiator and the rolls play goes to MIDI OUT too, on each track's channel (the drums on theirs). Every note is ended, STOP ends what is still on, and notes that came in from MIDI are never sent back. A setting of the FM-1, not of a project. About 1.4 KB of flash, 112 B of RAM, 4 B of fast RAM code (it runs in the audio interrupt). |
| MIDI IN = CLOCK (no notes, clock only) | `MIDI_INCLK` | HOME menu > MIDI IN: NOTES (as before) or CLOCK: MIDI in (USB and TRS) takes the clock and start / stop only, the notes it sends are ignored (the note-offs still end what was held). A setting of the FM-1, not of a project. About 0.7 KB of flash, 48 B of RAM (the settings it shares with the others), 4 B of fast RAM code. |
| USB MIDI in: flow control, malformed ignored | `USB_FLOW` | USB MIDI in: asks the computer to wait when the device is busy instead of dropping messages, and ignores malformed ones. About 0.3 KB of flash; the TRS jack cannot be held back. |
| TRS MIDI in: line noise no longer deafens the jack | `TRS_NOISE` | TRS MIDI in: a noise byte (FD) arriving at the wrong moment no longer blocks the jack until the next restart. About 16 B of flash. |

#### UI

| Item | Key | What it does |
|---|---|---|
| boot logo | `SPLASH` | the Optimist logo (drawn, no bitmap), the name and the version for 0.9 s at power-on (0.3 KB of flash); off: a dark screen until the UI |
| parameter icons | `ICONS` | A small 12 x 12 icon beside each parameter label. About 5.4 KB of flash; off: no icons, and the labels get their full width back. |
| VIEW ALL overview (4 x 4 PAGEs) | `OVERVIEW` | GLO > SYSTEM VIEW ALL: shows a whole page family at once, 4 rows x 4 knobs a PAGE, PAGE n/m. About 2.4 KB of flash; off: one page at a time. |
| (sub-item) ARP graph and ARP in VIEW ALL | `OV_ARP` | Shows the arpeggiator's graph and puts the ARP settings into VIEW ALL. About 0.6 KB of flash; off: the ARP graph and its VIEW ALL rows are not built. |
| say what a project uses and this build lacks | `MISSING_WARN` | 'MISSING: PHYS T2, KIT 909' in the top bar when a project, song section, user preset or kit uses an engine, kit, sample set or FX this build leaves out (once per item until power-off; never stalls the audio); SAVE > TOOLS > MISS lists them again. Off: they play their stand-ins silently (saves 1.5 KB of flash) |
| help line: what the knob changes, in words | `PARAM_HELP` | while a knob turns, the top bar (the live screens' header) names its value in plain words, e.g. 'Filter cutoff', 'Reverb send', until ~1 s after the last detent; only the lines of the features built (tools/param_help.json, also the web editor's tooltips). About 6.4 KB of flash, no RAM |
| chord names on the STEP page | `CHORD_NAMES` | SEQ > STEP shows a step of several notes by its chord in any inversion: C, Am, Bdim, Caug, Dsus2, Dsus4, E5, G7, Fmaj7, F#m7, Bm7b5. Other note sets stay a note and the count ('C4 +2'). About 0.3 KB of flash; off: the step's first note and the count. |
| drum steps on the keys (SLOOP 2.4 'Drums with the keys') | `DRUM_STEP` | On the DRUMS grid page (SEQ tapped on the drum track) the 16 white keys are the 16 steps of the sound KNOB 1 picks: press to set a step (you hear the sound), again to clear it; the first four black keys pick the page of steps. You hear what you pick: the sound when KNOB 1 changes it (on the grid and in the SEQ layer), the step's sounds when KNOB 2 moves to it. SELECT switches grid and kit (it is the tempo there without this item). Extra: while playing, the page follows the playhead (black key 5 turns it on / off). No data change. About 1.4 KB of flash, 16 B of RAM; the audition runs in the audio interrupt (no fast RAM code). Off: the keys play the pads on the grid page, as before. |
| knob acceleration by turn speed | `KNOB_ACCEL` | 1 / 2 / 3 / 5 / 8 steps a detent when turned fast; never on lists (engines, kits, presets) |
| screen data speed (SPI clock) | `LCD_BAUD` | How fast data is sent to the display: 60 MHz / (n + 1). Faster redraws finish sooner (less tearing); the ST7789V takes ~62 MHz, so 60 MHz is the limit. No flash cost; the faster settings are untested here on hardware. Values: 30 MHz; 12 MHz (as before X0X); 60 MHz. |
| screen: send only the changed rectangle | `LCD_DIRTY` | Redraws only the part of the graph strip that changed instead of the whole strip: less data per frame and less tearing (with the 30 MHz screen clock). Costs about 1.5 KB of flash and 0.6 KB of RAM. |
| keys light the notes played | `KEYLIT` | Lights the keys of the notes the selected track plays (its steps, the arpeggiator, held notes), so you see the pattern on the keyboard. About 0.2 KB of flash, LEDs only; with LIGHTS, MENU > NOTES turns it on and off at run time. |
| screen brightness (MENU > BRIGHT) (EXPERIMENTAL) | `BRIGHT` | MENU > BRIGHT: the screen backlight in 8 levels (software PWM, saved with the settings). About 36 B of flash; X0X reports it working on a real FM-1 but it is not tried here on hardware. Off: always full brightness. |
| keys ~1 ms sooner (debounce per column) | `KEYS_FAST` | The keys respond about 1 ms sooner (each key is debounced as its column is read); measured on the host: mean press latency 3.3 to 1.7 ms. About 32 B of flash. |
| menu LIGHTS / KEYS / NOTES (play in the dark) | `LIGHTS` | Lights for playing in the dark: MENU LIGHTS (every button glows OFF / LOW / MID / HIGH), KEYS (C or white keys glow) and NOTES (the note lights of KEYLIT on / off at run time). About 1.2 KB of flash. |
| knobs: one rest state a detent (no double clicks) | `KNOB_ONEREST` | Counts a knob click as one full encoder cycle, so a pause in the middle of a click no longer doubles the clicks after it. About 0.3 KB of flash; assumes the FM-1's full-cycle detents. |
| knobs quiet as a layer button is let go | `LAYER_QUIET` | Knob turns while a layer button is being let go (and for 250 ms after a used layer closes) are ignored, so releasing a layer does not nudge a parameter. About 8 B of flash. |
| BPM LOCK: SELECT is the tempo only with GLO | `BPM_LOCK` | SELECT changes the tempo only while GLO is held, so the tempo cannot slip live; GLO > GLOBAL's BPM knob and tap tempo still work. No menu item: the build switch is the choice. About 64 B of flash. |
| divisions in length order (1/8 8T 1/16 ...) | `DIV_ORDER` | Lists note divisions in length order (1/4 1/8 8T 1/16 16T 1/32) on ARP RATE, SEQ DIV, DELAY TIME and SLICER RATE; stored values are unchanged. About 0.2 KB of flash. |

#### System

| Item | Key | What it does |
|---|---|---|
| web editor and firmware updates (M-UPGRADE) | `OTA` | The USB SysEx side of the device: the web editor (editing, presets, samples, backup / restore, snapshots), firmware updates from the M-UPGRADE tool and the rescue updater. About 18 KB of flash and 6 KB of RAM; leave it on unless you update through the UBOOT rescue path. |
| sleep the CPU between main-loop polls (power, heat) | `IDLE` | Sleep the CPU (wait for an interrupt) between main-loop polls instead of spinning. Saves power and heat on the device and makes the emulator much faster (it skips the idle time); no effect on sound (audio and the panel run from interrupts, 100 us latency at most). No flash cost. Keep on; off only for debugging. |
| backup / restore from the web editor | `BACKUP` | everything in flash to one .optimist-backup file and back (editor cmds 43..48); a restore onto another build reports what it skips and what plays a stand-in |
| smaller UI and storage code (size-optimised) | `SIZE` | Builds the UI, storage and editor code for minimum size, about 6.8 KB of flash less than normal -Os, at no cost to sound: the audio path is never size-optimised (tools/size_fns.py guards it). Choose '-Os everywhere' only to compare. Values: minsize (UI, stores, editor); -Os everywhere. |
| predictive CPU guard (ease back before shedding) | `CPU_GUARD` | Avoids audio dropouts under heavy load by easing quality back step by step before cutting notes. Under overload (the measured load or the one predicted from the voices sounding, over 85 % for 8 halves, or a late half): first ACID without oversampling and ANALOG 2's swarm at 2 copies, then UNISON at 2 voices, then voices shed, never the bass or the lead (MONO / LEGATO / UNISON parts) nor the drums; back after 2 s under 80 % counting what each step saved. The CPU meter shows %G1..%G3. Weights: tools/builder/cpu_costs.py from tests/cpu_baseline.txt (docs/CPU-GUARD.md) |
| hand-written assembly speed-ups (FM6, ANALOG 2) | `ASM` | The hottest loops of FM6 and ANALOG 2 written in the processor's assembly: the same sound, about 8 to 19 % less audio CPU (emulator), for about 0.6 KB of flash and 0.7 KB of fast RAM code. Keep on; off only to compare with the plain C. |
| (sub-item) packed 16-bit sine and swarm maths (EXPERIMENTAL) | `SIMD` | Experimental: uses the processor's packed 16-bit instructions for the sine lookup (DIGITAL, PHASE, drum synth) and ANALOG 2's swarm. It needs 4 KB more RAM, and what those instructions do is inferred, not documented: emulator-tested, never run on a real FM-1. Leave off. |
| stricter checks of saved data when read back | `ST_STRICT` | Stricter checks of what is read back from flash (settings, projects, presets, kits) and a compare after each save, so damaged data is refused instead of loaded. About 0.1 KB of flash. |
| overload: fade a voice, keep bass and lead | `SHED_FADE` | Under overload, fades out one voice at a time and never the bass or the lead, instead of cutting voices; the sound changes only under overload. About 0.1 KB of flash. |
| restore: an object refused unless it would load | `BK_CHECK` | Restoring a backup writes each stored object only if the firmware would load it (otherwise it is refused), so a bad backup cannot leave unloadable data. About 0.4 KB of flash. |
| SLOOP 2.4's data kept safe (never erased) | `SL24_SAFE` | Coming from SLOOP 2.4: Optimist never erases or writes over what 2.4 left that it cannot read: the four project slots and the autosave (shown as SLOOP 2.4 on the PROJECT page and in the editor, not EMPTY), 2.4's FM6 bank and a user sample longer than ours (USR3, USR4); 2.4's settings word and user presets are read right. Off, the first start erases 2.4's projects. About 0.7 KB of flash. |
| big values on pages without a graph (SLOOP 2.4) | `BIGVALS` | Pages without a graph (EDIT, VOICE, the DEST pages, GLOBAL, MASTER, SYSTEM...) use the empty middle of the screen: their four values in large type, placed as the knobs are (KNOB 1 2 above, KNOB 3 4 below), the one you turn in white; a page with one value shows it across the middle. As SLOOP 2.4. Costs 608 B of flash and 112 B of RAM. |

#### Experimental

| Item | Key | What it does |
|---|---|---|
| second CPU core renders parts 2-3 (EXPERIMENTAL) | `DUAL` | Experimental: the FM-1's second CPU core renders synth parts 2 and 3 while the first renders the rest, cutting the first core's load by 40 to 44 % in the emulator, with the same sound. It costs about 1.8 KB of flash, 1.9 KB of RAM and 6 KB of pool, and has never run on a real FM-1. |

### Where an item came from

Items ported from another project carry their provenance (project, author, licence, commit) in the registry;
the menu shows it in the details panel. Items from X0X (`charlesvestal/fm1-x0x`, GPL-3.0-only, by Charles
Vestal) show a NOTICE when selected. What X0X uses only by its author's permission (its break player,
`dsp/breaks*`: no licence) is never offered (`registry.FORBIDDEN`; a `.config` naming it is refused).

Performance macros (MACROS, off by default; after Flowstate, GPL-3.0): COLOR, MOTION, SPACE, ENERGY on GLO > MACRO,
applied in the audio ISR over the authored values (firmware/src/macro.c has the table), kept in four drum-track values
the drum track never reads (no format change), recorded by motion recording. Its option ENERGY thins / thickens the
drum track's steps in five bands. Off: the build is byte-identical to the one without the code; built in and at home:
the goldens and a 4-track mix render bit-identical (tests/macro_test.c). user-default with MACROS: +1,432 B flash
(+1,996 B with ENERGY), +400 B RAM. A parameter a macro moves shows the value that plays (amber, with an M and a gauge
tick) on the device and in the web editor (editor command 65); that costs +2,336 B flash and +16 B RAM on everything-that-fits
with MACROS, ENERGY and MOTION (RAM code unchanged, 32,424 of 32,512).

Constraints the configuration checks (errors): at least one synth engine, at least one FM6 mode, a drum source.
Motion recording works with every SECTIONS: with 4 its data sits beside the four project slots (as before),
with 8 / 16 each section's record carries it.

### Song sections (SECTIONS)

16 (default) or 8: sections A..P / A..H in banks of 4, stored compressed in one 32 KiB log (the old project
slots' area, firmware/src/sec_log.c), the song layer's title shows the MEM gauge ("mem 34% +12": the share used
and how many more sections of the last size fit). SAVE + OCT- / OCT+ changes the bank; the keys play / store
within it. MEM FULL refuses a new section; room for one record of the longest size is always kept, so the playing
section can always be saved; clearing one always works; an empty section costs nothing. The first start moves the
four old project slots in as A..D (cut anywhere, the next start goes on). 4: the four slots as before.
The reserve is exact: a record never spans two sectors, so a byte count alone can say "room" when sectors each
holding one ~3 KB section have none. sec_log.c models its compaction (where each record is, the sectors' order)
and takes a store that is not the playing section's only when, after it, a record of the longest size would still
go in; the gauge's "+n" counts on the same model. A compaction cut while copying can leave a half-written copy
taking the head's room; the next start then empties whichever sector's live records fit there (not only the
oldest), so the log always gets its spare sector back.
Motion recording (MOTION) with 8 / 16: a section with recorded knob moves carries them in its record, a chunk
after the flags byte (count, the PLAY bits, 3 bytes an event: +2 B +3 B an event, in the gauge like the rest);
none recorded, no chunk. Saved, stored while playing (the arena), written, cut, staged for the song: with its
section, in one record and one CRC. A raw record with motion leaves out the project's magic, size and sum
(rebuilt as it loads), so the longest record, raw with 64 events and a drum record, is 4,059 B (4,076 B with
its head, a 4 KiB sector holds 4,080). Every build reads a chunk (one without MOTION plays the section without
it, and keeps it until the section is saved again) and counts it in its longest record, so no build's log ever
finds another's record too long. The first start of an 8 / 16 build moves each old slot's motion (beside it in
its sector) into its section. With 4 the storage is as it was: the 208 B beside each project and the autosave.
The song chain: 64 parts with 8 or 16 sections (16 with 4). The settings record keeps its layout, with the first
16 parts, so an older build plays those. The whole chain is a record of the log (id 16), saved before the
settings record, which names it by a tag. A save cut between the two keeps the old chain whole. The backup
object is `SNG1`. The log's ids are the same in every build (0..15 sections, 16..23 songs), so a build with
fewer sections keeps the other sections' records: going from 16 to 8 sections and back loses nothing.
Record sizes (tests/sec_codec_test.c): the power-on project 70 B, a typical 16-step section 478 B (8 a 4 KiB
sector), random ones 1.1 KB on average, the dense worst case 3,877 B (raw). The stage's decode, emulator: 42,606
instructions for a 524 B section (~0.18 ms at 240 MHz at one instruction a cycle).
Data stored in the app slot's unused end does not survive an update: the package pads the app to the slot with
0xFF and the loader writes the whole range (tools/fm1pkg_make.py, firmware/loader), so only the data areas
(the log, the USR slots) can hold sections.

### Snapshots (SNAPSHOTS)

SAVE > SNAPSHOT keeps the whole state (the working project, every section, the song) in a slot and loads it back;
docs/SNAPSHOTS.md has the design. 4 (default), 2 or 8 slots plus BEFORE LOAD (the state before the last load).
The flash comes from the end of USR3: slots + 4 sectors of 4 KiB, any free sector takes any part of a snapshot,
part 0's header is written last (a cut save leaves the old version; a cut clear never brings an older one back).

| SNAPSHOTS | area | USR3 left | typical snapshots (1 sector) that fit | the largest (8 sectors) |
|---|---|---|---|---|
| 0 | 0 | 64 KiB | none | - |
| 2 | 24 KiB | 40 KiB | 2 + BEFORE LOAD + 3 spare | does not fit (FULL) |
| 4 | 32 KiB | 32 KiB | 4 + BEFORE LOAD + 3 spare | fits an empty area |
| 8 | 48 KiB | 16 KiB | 8 + BEFORE LOAD + 3 spare | fits beside 4 typical ones |

Measured (tests/snapshots_test.c): the power-on state 203 B, three 16-step sections with a song and the work
2,105 B (one sector), three dense sections 3,980 B; the largest a full section log can make is under 29.3 KB.
Cost (costs.json, measured 2026-10-07 on optimist 96f749a): 8,708 B of app and 336 B of RAM, no pool; the slot
count costs nothing more (2: 16 B, 8: 32 B). No snapshot function is in RAM code; the RAM code still moves by a few
dozen bytes with it, the audio path's code generated differently as the rest of the unity build changes (user-default:
-92 B, mix_block 9,242 -> 9,152; everything-that-fits: +68 B, drums_mix 3,598 -> 3,664, 20 B left).
A build without snapshots has a 64 KiB USR3 again: a long USR3 sample uploaded there overwrites the area (the
backup keeps snapshots: object SNAP); a build with them does not write over such a sample (`USR3 SAMPLE IN THE WAY`
until USR3 is erased or loaded again).

### Backup and restore across builds

The editor's backup holds every stored object (cmds 43..48). BK_LIST tells what the device's build holds (engine,
kit and sample-set masks by UID, slot capacities, the section count and record layout); before a restore the
editor shows a report and asks to confirm: objects not in the build or larger than its slot are skipped whole
(never truncated), and each part whose engine, sample set or kit the build leaves out is named ("section C track 2
uses PHYS: plays ANALOG, settings kept": the orphan path keeps its settings). A section's motion travels in its
record (S01..S16, byte for byte); a build without MOTION (BUILD bit 76) is named in the report ("section B has a
motion recording (10 events): this build has none ..."). With 4 sections the motion sits outside the project's
payload, so a PRJ1..PRJ4 / AUTO backup does not hold it (as before this change).

## How a switch works

- `firmware/src/registry.h`: the engines as an X-macro list with their permanent UIDs (FUN7 numbers) and
  fallbacks, and the defaults of every switch. SLOOP is one translation unit: an item left out is a constant
  `0` (`ENG_IS()`, `if (FELUCCA_FX_X)`), so its code, tables, RAM and pool go. Pages and cells of a missing FX
  disappear (`params.c` `page_shown`, `cell_built`).
- `tools/builder/configure.py` turns a `.config` into `build/gen/felucca_config.h` (included first by
  `tools/build.py`) and the generators' environment (sample sets, PERC, SLICE's BREAK).
- Stable IDs: projects, user presets and the drum lanes store engine, kit and sample-set UIDs. A part whose
  engine this build leaves out plays the fallback (FM6 -> DIGITAL -> ANALOG, GRAIN -> SAMPLE, the rest ->
  ANALOG) with its defaults, shows `FM6*` on HOME, and keeps the original engine, preset, EDIT values and FM6
  voice: they are written back on save as long as you leave that part's sound alone. A full build then plays
  it as before (`tests/builder_rt_test.c`). A user preset of a missing engine stays in the bank, not loadable.
  A kit not built plays the other source's first kit; a sample set left out keeps its number (empty).
- Telling the user (`MISSING_WARN`, UI, on; firmware/src/miss.c): after a load (a project, a song section, also
  while playing, a user preset or kit) the main loop scans the working project for what this build lacks and says
  it in the top bar, "MISSING: PHYS T2, KIT 909 +2", once per item until power-off. The audio side only bumps a
  counter (proj_apply). SAVE > TOOLS > MISS counts the items and lists them one by one. Names come from the
  tables already built (ENG_UID_NAME, DRUM_KIT_NAMES, SMP_ALL_NAMES; an FX name only when its switch is off).
  Cost (measured): 1,112 B flash on user-default (1,160 B on the default build), 64 B RAM (from the undo
  ring). `tests/missing_test.c` loads a full build's project, section and user kit on a reduced build and keeps
  the screens (build/host/miss).
- The knob's help line (`PARAM_HELP`, UI, off by default; on in everything-that-fits; firmware/src/param_help.c):
  while a parameter knob turns, the top bar (the live screens' header, the song screen's knob labels) names its
  value in a few plain words ("Filter cutoff", "Reverb send", "Swarm spread (ENV2)"), gone ~1 s after the last
  detent (64 frames; nothing on a page change, a button or the tempo knob). One table, tools/param_help.json
  (320 lines: every page, every engine's EDIT values and mode labels, the drum SOUND pages, the FM6 operator
  editor, TRACKS, the drum grid / kit, SONG, REC), also the web editor's tooltips; tools/gen_param_help.py writes
  build/gen/felucca_param_help.h with each group under its feature's condition, so a build carries the lines of
  what it has. Cost (measured, plain strings in flash): +6,192 B user-default, +5,456 fm-va-studio, +4,720
  drum-machine, +6,352 everything-that-fits, +4,624 x0x-drums, +6,416 the default build; 0 B RAM (the line's
  offset sits in padding of the UI state), 0 B RAM code. A shared word dictionary was measured and not taken: -1.8 KB
  on user-default (-0.4 KB on x0x-drums) for +48 B RAM and a decode buffer, and it pushed everything-that-fits'
  RAM code over by 36 B. `tests/param_help_ui.c` checks every reachable value has a line that fits (232 px).
- The firmware reports what it is: editor protocol v6, INFO adds each slot's engine UID, `BUILD` (49) the
  configuration's name, hash and one bit per registry item.

## Budget

`tools/builder/measure_costs.py` builds every item at every non-default value (measurement builds link past the
slot) and writes `costs.json`: the default build's sizes and each item's delta. The deltas add up within about
0.5 %; the menu's build gives the exact figure. Re-run it after a merge. An item whose cost depends on another's
value is measured with it too (`PAIRS` in measure_costs.py): `costs.json` "pairs" holds what the two cost together
beyond their own deltas, which the estimate adds when the configuration has both (today MOTION=1 with
SECTIONS=4: the motion beside the four slots instead of in the section records). Measured 2026-10-06: MOTION with
16 sections adds 2,992 B app, 496 B RAM, 1,376 B pool, 112 B RAM code; with 4 sections 3,376 B app, 480 B RAM,
1,776 B pool, no RAM code.

**Shared DSP blocks and tables (docs/DSP-SHARED.md).** A block several items use (dsp_common.h, dsp.c, dsp_float.h:
xorshift32, soft_knee, tsvf_tick, ima_nibble, lerp16, ...) is `always_inline`: each item that is built compiles its
own inlined copy into its own functions, and an item left out takes its copy with it. So the per-item model needs no
"shared by A or B" term for them: an item's delta is exactly what it adds, and the deltas did not move when the
blocks were merged (2026-10-07: every function of the six measured builds identical). A shared *table* (IMA_STEP
for SAMPLE / GRAIN / SLICE, SVF_G, PITCH_INC, SINE...) is `static const` in the one unit: the compiler keeps it when
any built item reads it, so it is in the default build's base while any default item uses it, and it is counted in
the delta of an item measured alone only when no default item reads it (e.g. a table only PHYS and CZ read would be
counted in both items' deltas, once too often when both are on: then the item pairs belong in `PAIRS`). No such
table turned up in the inventory (the X0X tables are the 909's alone; ACID and the X0X kits each compile their own copy of dsp_float.h in their own unit); the exact build stays authoritative.

### The profiles (config/profiles/, real links, 2026-10-07: optimist 96f749a + feat/snapshots, SNAPSHOTS 4 in every profile; before it 2026-10-06 with the SLOOP 2.3 fixes on, the large font from the small one, the cheaper X0X kits, X0X voices on lanes and style kits, the editor commands 50..53)

| Profile | Left out to fit | App (of 581,564) | RAM (of 98,304) | Pool (of 335,872) | RAM code (of 32,512) |
|---|---|---|---|---|---|
| user-default | LOFI, VOICE, delay 0.74 s, SCRATCH set, FM6's operators in VIEW ALL | 568,336 | 74,372 | 306,860 | 29,308 |
| fm-va-studio | GRAIN, VOICE, LOFI, PHASE, WHEEL, SCRATCH set | 561,184 | 89,604 | 321,680 | 26,680 |
| drum-machine | FM6, DIGITAL, PHASE, VOICE, TRIO, WHEEL, STRINGS set | 563,288 | 85,688 | 331,028 | 23,712 |
| everything-that-fits | SCRATCH and STRINGS sets, PUNCH ring 0.37 s, changed-rectangle LCD strips (20 B of RAM code left; 88 B without snapshots); with the knob's help line (PARAM_HELP, +6.4 KB) | 567,860 | 77,732 | 327,340 | 32,492 |
| x0x-drums | drum-machine's, plus: the five sampled kits, PIANO, HORNS and FLUTE sets, delay 0.74 s (for the X0X 909 and 808 kits) | 539,976 | 69,776 | 293,484 | 23,868 |

The estimate (`--budget`) was above the real app size by 208 to 708 B for the first four profiles and by 2.5 KB (0.5 %) for x0x-drums. A sample set
left out can still be uploaded to a USR slot.

## Verification

`tools/builder/verify.py [--random N] [--emu]`: every profile and N random configurations (fitted by the
estimate) build, link and fit; no symbol of an item left out stays in the ELF; `tests/regress.c` built with the
configuration renders every present preset bit-identically to the full build's goldens; with `--emu` the
emulator boots each image and the audio stays silent (rms 0); every engine built has a loadable entry on the PRESETS list
(a factory preset, else INIT) and every drum source a kit (`tests/preset_cover_test.c`, also run by `tests/builder_test.py` on the profiles,
named edge cases and 12 random choices of the sound sources: each is refused by validate() or holds). Host tests: `tests/run_tests.sh` (includes the
full -> reduced -> full project round trip).

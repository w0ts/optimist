# Firmware builder

Pick the engines, FX, drum kits, sample sets and features that go into your FM-1 firmware, with a live flash, RAM,
pool and RAMTEXT budget, then build it: in the browser at [w0ts.github.io/optimist](https://w0ts.github.io/optimist/)
(Build tab: a cloud build, nothing to install; docs/WEB.md), or on your computer with `make builder`. The memory map
is `docs/MEMORY-MAP.md`, with the budget behind the trade-offs in its section 3. Sizes come from measured builds
(`tools/builder/costs.json`, "Budget" below); the CPU figures from the emulator.

## Run i

```sh
python tools/optimist.py setup                            # once: the venv, the SDK files, the toolchain (BUILDING.md)
python tools/optimist.py builder                          # the interactive menu (Textual)
python tools/optimist.py builder --profile drum-machine   # the menu, starting from a profile
python tools/optimist.py builder --config my.config       # the menu on a saved configuration
```

`tools/optimist.py` runs the same on macOS, Linux and Windows. `make builder` and `tools/menuconfig` call it.

Keys: `space` / `enter` toggle (a sized item: its next value), `/` search, `p` profiles (shipped + yours), `s` save as
your own profile (`config/my-profiles`, git-ignored), `u` publish the profile or not (CI builds the published ones; one
of yours is moved to `config/profiles` first, to commit), `d` delete a profile (never `synth`), `w`
write a `.config`, `l` load, `o` sort by size (cycles registry order, flash, RAM, pool, RAM code; items without a
measured cost last), `b` build, `e` build and run in the emulator (96 MHz, background), `f` flash to the FM-1, `x` / `c`
expand / collapse all, `left` / `right` page the list, `q` quit.

The bars show the estimate (red with "OVER by n" when a region overflows; the message panel names the biggest items of
that region). `b` runs the real build (10 to 20 s): the exact sizes replace the estimate, and when
it fits the package is `build/felucca.fwsc` (`build/optimist-<version>-*.fwsc`) with its web-editor sidecar
`build/felucca-ui.zip`. A progress bar follows the build's five stages and the flash. A configuration error marks the
items it concerns in red (`✗ the drum track needs a drum source: ...`) and `b` says CANNOT BUILD until it is fixed.
The builder builds the checkout it runs in, at the commit it is on; the top line and every result show
`source: <path> <branch>@<commit>`.

**Flash to FM-1 (`f`).** After a build that fits, `f` reads the connected FM-1's identity (`tools/fm1_install.py
--info`) and opens a confirmation with it, the package, the commit and configuration it was built from, and how to
recover (README.md, "Getting back"). *Cancel* is the first line and escape cancels: nothing is written before you pick
*Flash to FM-1*. It then runs `fm1_install.py PACKAGE --yes` (the web installer's update; never `--force`). `f` flashes
only a package this menu built of the configuration it shows, with the builder code it runs (`tools/builder/stamp.py`).
No build yet, a configuration changed since the build, the package replaced by another build, or the builder's own
files changed on disk since it started (the panel says THIS BUILDER IS OUT OF DATE: quit and start it again): FLASH
REFUSED, nothing read or written. With no current build `f` builds first, then asks. The same without the menu:
`python tools/optimist.py flash [PACKAGE.fwsc]` / `make flash` (BUILDING.md).

Without the menu (scripts, tests, CI):

```sh
python tools/optimist.py package --config my.config --out dist --summary dist/result.json   # exit 0 = built and fits
python tools/optimist.py config --list                                       # the registry with its values
python tools/optimist.py config --profile fm-va-studio --budget               # the estimate
python tools/optimist.py config --profile fm-va-studio --set FX_PUNCH=0 --write my.config
python tools/optimist.py config --profile everything-that-fits --fit          # drop items (least loss first) until it fits
python tools/optimist.py config --config my.config --build                    # real build: exact sizes, the package if it fits
```

`tools/menuconfig ARGS` is the same as `optimist.py config ARGS` on macOS and Linux. A few build-only flags that no item sets still come from the
environment with `sh build.sh` / `tools/build.py` (BUILDING.md, "Build flags").

## Profiles

A profile is a saved configuration. Shipped ones are in `config/profiles` (`make profiles` lists them); yours are in
`config/my-profiles` (git-ignored). The first lines of each file say what it is and what it leaves out.

| Profile | What |
|---|---|
| `synth` | **the default** (Optimist Synth, the maintainer's own build): `build`, `package`, `flash`, `config` and `make` use it without a profile, the menu opens with it the first time, and it is the Pages site's flavour (`web/flavours.json`) |
| `drum-machine`, `everything-that-fits`, `fm-va-studio`, `x0x-drums` | refits for a purpose, each fitting the flash slot |

`synth` cannot be deleted (it is also the reference: the host tests, the gate, `emu-check` and CI's fixtures build it; `user-default` went 2026-10-21). `--defaults` is every registry default (too big for the slot).

A shipped profile with the line `# publish: yes` is built by CI (`.github/workflows/build.yml` takes the list from
`config --published`); `# publish: no` or no line: not built. Today `synth`, `drum-machine`, `everything-that-fits` and
`fm-va-studio` are published.

```sh
make share PROFILE=my-groove      # move one of mine to config/profiles (config --share)
make publish PROFILE=my-groove    # CI builds it (--unpublish to stop); commit the file
make delete PROFILE=my-groove     # mine first, else the shipped one
```

The estimate for each shipped profile (`config --budget`, 2026-10-20; the limits are in `tools/builder/core.py`
`LIMITS`):

| Profile | App flash (of 581,564) | RAM (of 98,304) | Pool (of 344,064) | RAM code (of 32,512) |
|---|---|---|---|---|
| synth | 578,588 | 92,684 | 310,672 | 22,760 |
| fm-va-studio | 579,764 | 85,636 | 306,812 | 27,300 |
| everything-that-fits | 565,180 | 73,924 | 265,800 | 31,892 |
| x0x-drums | 557,708 | 72,316 | 323,508 | 25,720 |
| drum-machine | 526,752 | 91,884 | 300,388 | 24,588 |

## What is switchable

`tools/builder/registry.py` (and `tools/builder/backports.py`) is the list; `config --list` prints it. One level only: a
top-level item and its own options (FM6's voice packs, the sampled kits, the delay length ...). An option is ignored
while its parent is off, and no option depends on another item. A `.config` is `KEY=value` lines; an unknown key warns
and is ignored, a retired key (below) warns "dropped".

Errors the configuration checks (`validate()` in `tools/builder/core.py`): no synth engine; no drum source (the drum
synth or a sampled kit); a reverb with no algorithm; PATTERNS with SECTIONS 4; MICRO, FILLS or PLOCK without SL24_XSTEP; an
the Bluetooth Classic probe without a console or with BLE. Warnings the menu gives: FM6 without its editor and without SysEx has no voice
editing; sample sets without SAMPLE or GRAIN play nowhere; an experimental item says what it has been tested on.

Every engine a build has keeps at least one entry on the PRESETS list, and every drum source at least one kit. An engine
with no factory preset this build can play shows one entry, INIT (the engine's defaults, no preset data). GRAIN withou
PIANO, VIBES and FLUTE but with another melodic set gets a real preset, GRAIN PAD, on the first of them.

### Every item, in plain words

The text of the menu's details panel (`desc` in the registry), one table a group, generated from it:
`tests/builder_test.py` fails when an item is missing or its text differs, so regenerate a row from the registry, do no
edit it here. Sizes are about, from `costs.json`. A `(sub-item)` is an option of the item above it. Items marked
EXPERIMENTAL are emulator-tested only unless their text says more. The builder's reserve entries (a minimum of undo history or of free app flash) were retired 2026-10-21: the estimate shows the undo ring as information.

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
| FM6 (DX7, bit-exact with Dexed) | `ENG_FM6` | Six-operator FM that plays Yamaha DX7 voices the way the Dexed plug-in does (MSFA, bit for bit), with its own operator editor and DX7 SysEx in; INIT VOICE and the voice packs below are its sounds. The biggest engine: about 28 KB of flash and 6 KB of RAM; its options below trim it down. |
| (sub-item) FM6 operator editor on the black keys | `FM6_KEYS` | Edit a DX7 voice on the device: hold ENV and press a black key to pick an operator, the pitch envelope or the global page, then turn the knobs. About 6.3 KB of flash; off: a voice can only be replaced by DX7 SysEx. |
| (sub-item) FM6 DX7 SysEx in (a voice, a 32-voice bank) | `FM6_SYSEX` | Receives DX7 SysEx over USB MIDI, from Dexed or any DX7 librarian: a single voice replaces the selected FM6 track's voice (then SAVE AS keeps it as a user preset), a 32-voice bank becomes a pick list at the end of that track's PRESETS (RAM only, until power-off) to audition and SAVE AS. Nothing is sent back. About 1.3 KB of flash and 4.3 KB of pool RAM for the frame buffers. |
| (sub-item) FM6 voice pack: keys and e-pianos (16) | `FM6_PK_KEYS` | 16 voices of my own for the FM6 track, listed in its PRESETS after the user presets (K01..K16): electric pianos, clavs, organs, a harpsichord. About 2.0-2.3 KB of flash. |
| (sub-item) FM6 voice pack: basses (16) | `FM6_PK_BASS` | 16 bass voices for the FM6 track, listed in its PRESETS after the user presets (B01..B16): slap, fretless, synth, pluck and sub basses. About 2.0-2.3 KB of flash. |
| (sub-item) FM6 voice pack: bells and mallets (16) | `FM6_PK_BELLS` | 16 voices for the FM6 track, listed in its PRESETS after the user presets (M01..M16): bells, marimba, vibes, kalimba, steel drum, glass and chimes. About 2.0-2.3 KB of flash. |
| (sub-item) FM6 voice pack: pads, leads, brass and FX (16) | `FM6_PK_PADS` | 16 voices for the FM6 track, listed in its PRESETS after the user presets (P01..P16): pads, leads, brass, strings and effect sounds. About 2.0-2.3 KB of flash. |
| (sub-item) FM6 voice pack: 80s mega classics (16) | `FM6_PK_80S` | 16 recreations of the famous 80s DX sounds (the e-piano, the bass, the bells, the brass, the marimba, the harmonica ...) for the FM6 track, listed in its PRESETS after the user presets (801..816). All programmed from scratch, none copied from a ROM or a cartridge. About 2.0-2.3 KB of flash. |
| CZ native tone collection in PRESETS | `NATIVE_BANKS` | With CZ built, a CZ track's PRESETS list ends with a collection of 26 CZ-1 tones (the web editor imports and exports Casio .syx). Loading one sets only the engine's values: the track keeps its FX, mix and pattern. The collection takes 8 KiB at USR3's end (USR3 holds that much less). About 2 KB of flash with CZ (measured); no RAM. (The FM6 user bank U01..U32 that this item once listed went 2026-10-18; FM6's own list is its voice packs and a received DX7 bank, with FM6_SYSEX or a pack.) |
| SLICE (break slicer + its BREAK sample) | `ENG_SLICE` | Break slicer: cuts a loop (the built-in BREAK, 22 KB of samples, or a USR slot) into 4 / 8 / 16 / 32 slices or one per hit and plays them from the keys or the sequencer. About 27 KB of flash and 6 KB of RAM; off by default. |
| PHYS (physical models) | `ENG_PHYS` | Physical-modelling engine (engine 11): modal, string, membrane and sympathetic-string models, 3 voices per part. Heavy: about 9.8 KB of flash and 38.7 KB of pool, and 3 voices of its heaviest preset take about 17 % of the audio budget at 312 MHz (at 96 MHz that was too much and voices were shed). |
| ACID (TB-303 bass + line generator) | `ENG_ACID` | X0X's TB-303-style bass voice (one monophonic voice per part, engine 12) with the TB-3PO line generator. Floating-point DSP (about 14.4 KB of flash, 1.7 KB of RAM) that ran on a real FM-1 in X0X; no pitch bend or TUNE on the 303; 4 presets. |
| NOISE (colours, crackle, shift-register noise) | `ENG_NOISE` | Noise engine from Felucca-Engines (engine 15, MIT): white, pink and brown noise, crackle (DUST), a shift register clocked above the key (LFSR) and short-sequence metal tones (META), through a resonant filter that morphs from low-pass to band-pass, with key tracking, a slow random drift and a bit crusher; 4 presets. About 3 KB of flash, no RAM; 1.1..1.3 K host instructions a sample with 8 voices (the heaviest ANALOG preset: 1.7 K). |
| DRONE (drones, ambient, binaural beats) | `ENG_DRONE` | Continuous tones for drones and ambient (engine 16): a stack of up to three oscillators a note (one, a detuned unison, or the key with an octave, fifth, fourth or third), sine to triangle to saw, a soft low-pass, a slow random drift of the whole part and soft noise (AIR). BEAT plays binaural beats: each ear its own pitch, 0.05 to 40 Hz apart (headphones). HOLD latches the keys. 7 presets (theta, alpha, delta and gamma beats among them); 4 voices a part, each about twice an ANALOG voice's CPU (host-measured). About 4.8 KB of flash and 0.6 KB of RAM. |
| CZ (CZ-1 style tones) | `ENG_CZ` | Casio CZ-1 style engine from Melodee 0.11 (engine 13): two lines with 8-step envelopes, 8 built-in tones (TONE) changed by the EDIT values; no envelope editing, banks or SysEx yet. About 7 KB of flash and 3.1 KB of RAM; similar CPU to FM6 (emulator). |
| JUNO (Juno-106 voice) | `ENG_JUNO` | Juno-106 voice from fm-106 / KR-106 (engine 14): the DCO (saw, pulse, sub, noise), the 4-pole OTA ladder filter and the 4-position HPF; the track's ENV and LFO drive it; 6 voices, 8 patches of our own (no Roland factory patches). About 2x the CPU of an ANALOG voice. |
| (sub-item) FM6 user presets keep their voice | `UP_FM6` | FM6 user presets keep their whole voice (operator edits included) instead of only the VOICE number. About 0.7 KB of flash plus a 3.6 KB store in flash; a preset written from the web editor drops its kept voice. |

#### Drums

| Item | Key | What it does |
|---|---|---|
| synthesised kits (32: 808, 909 and others) | `DRUM_SYNTH` | The 32 synthesised drum kits (808, 909 and others), generated by the firmware, so they take no sample memory. About 13.7 KB of flash. They also play the sounds an X0X kit lacks (with neither them nor a sampled kit built, those are silent). |
| X0X 909 kit (circuit-modelled TR-909) | `DRUM_X0X909` | X0X's TR-909 drum kit (kit UID 37; the drum models are circuit-modelled, hi-hats, ride and crash are samples), on the 16 lanes; SHAKER, CONGA, COWBELL play the synthesised 909's. Big: about 151 KB of flash (see its cymbal option) and uses floating point. A build without it plays the synthesised 909 for it and keeps the kit |
| (sub-item) X0X 909: ride and crash sample quality | `X909_CYM` | 8-bit block floating point as before; 6-bit: 22 KB less, 30 dB against the 16-bit source instead of 42 (screens x0xdrums-perf-2026-10-06); off: RIDE and CRASH play the synthesised 909's (the hi-hat samples stay) |
| (sub-item) X0X 909: hi-hat sample quality | `X909_HAT` | The closed and open hat samples as 8-bit block floating point as before, or 6-bit (four mantissas in three bytes, as the ride and crash's 6-bit): about 5 KB less, 30 dB against the 16-bit source instead of 42 (measured by tools/gen_x0x_drums.py; the 909's own cymbals are 6-bit). No other 909 voice reads a sample |
| (sub-item) X0X 909: saturator curve (tanh) resolution | `X0X_TANH` | The 909's saturators read tanh from a table with linear interpolation: 1024 points as before, or 256 points: 3 KB less, within 1e-4 of tanh (80 dB below the signal), no audible change (tests/x0x_drums_test.c measures it) |
| X0X 808 kit (circuit-modelled TR-808) | `DRUM_X0X808` | X0X's TR-808 drum kit (kit UID 38; 16 circuit-modelled sounds) on the 16 lanes; MIDI also plays MT, LC, HC and the claves (note 75). About 30 KB of flash and uses floating point. A build without it plays the synthesised 808 for it and keeps the kit |

| sampled drums (the PERC set, 99 KB) | `DRUM_SAMPLED` | The recorded drum samples (the PERC set, about 99 KB of flash) behind the five sampled kits below. Switching this off drops all of them; the drum track then needs a drum synth. |
| (sub-item) sampled kit ACOUSTIC | `KIT_ACOUSTIC` | The sampled kit as recorded (the basic kit of the PERC set). It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit DEEP | `KIT_DEEP` | The sampled kit tuned down and softened by a low-pass filter: a darker, heavier kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit TIGHT | `KIT_TIGHT` | The sampled kit with every hit cut short by a fast decay: a dry, punchy kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit BRIGHT | `KIT_BRIGHT` | The sampled kit tuned two semitones up: a lighter kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| (sub-item) sampled kit DUST | `KIT_DUST` | The sampled kit tuned slightly down, filtered and reduced to a coarse bit depth: a gritty lo-fi kit. It shares the PERC samples with the other sampled kits, so leaving it out saves no memory (only leaving out all of them does); a project using it plays another kit. |
| drum sound editor (EDIT on the drum track) | `DRUM_EDIT` | Per-lane sound tweaks over the kit (TUNE, DECAY, SNAP, CLICK, BEND, CUT, DRIVE, LEVEL on synthesised kits; TUNE, DECAY, CUT, LEVEL on sampled ones). About 1.2 KB of flash; off: kits play as they are and saved tweaks are kept but not applied. |
| user samples on drum lanes | `DRUM_USR` | A drum lane can play one of your own samples (a hit of a USR slot, with start and length) instead of the kit's sound. About 1.2 KB of flash; off: such lanes play the kit sound. |
| user drum kits (bank of 16 in data flash) | `DRUM_KITS` | Lane sources from other kits and the X0X machines, and a bank of 16 user kits (each lane's source and sound tweaks) saved in flash. About 2.8 KB of flash; off: lanes play the kit as it is. |

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

#### FX: any number (each track its own)

Every track (and the drum bus) can run these at once, each with its own small state: only the CPU limits how many run.

| Item | Key | What it does |
|---|---|---|
| DIST (per-track drive) | `FX_DIST` | A distortion on each synth track: low cut, drive from 1x to 8x into an asymmetric soft clip, and a tone filter that closes as the drive rises. About 0.4 KB of flash; off: the DIST controls disappear. |
| SLICER (stutter / gate insert) | `FX_SLICER` | A tempo-synced 16-step gate or stutter on each track (not the sample slicer engine): chops the sound to a pattern. About 1.2 KB of flash and 32 KB of pool at the full capture length. |
| (sub-item) SLICER capture length (stutter memory) | `SL_LEN` | How much sound each track's stutter records to repeat: the shorter capture saves 16 KB of pool and limits how long a repeated chunk can be (the gate mode is unaffected). |
| DJ filter (MASTER FILT) | `FX_DJF` | One knob on the master (MASTER > FILT): turn left for a low-pass closing, right for a high-pass opening, centre is off. About 0.7 KB of flash. |
| DUST (vinyl / lo-fi master) | `FX_DUST` | One knob on the master (MASTER > DUST) that runs the mix through an old sampler and a record: drive, lower sample rate, fewer bits, a darker tone, hiss and crackle while playing. About 0.7 KB of flash. |
| COMP + LIMIT (master compressor, brickwall limiter) | `MASTER_COMP` | A compressor and a brickwall limiter on the master (GLO > COMP: THRS, RATIO, ATK, REL; GLO > LIMIT: GAIN, CEIL and a GR readout) to glue the mix and keep its peaks under a ceiling; a Comp button and a GR meter in the web mixer. A new project limits at CEIL -3.0 dB (a look-ahead limiter in place of the old one); saved projects keep their own CEIL (OFF: as before). About 2 KB of flash and 2.5 KB of RAM. |
| track FILTER (LP <- off -> HP on each track) | `TRK_FILT` | A DJ-style filter on each track: FX > FILTER (one knob, FILT: left low-pass, right high-pass, centre off) and FX held + KNOB 4 for the selected track. On the drum track it filters the summed drums and all their sends. Saved with the project, kept when the sound changes, recordable with motion. Costs nothing at the centre; engaged, one filter per track (five on the drum bus). |
| FOLD insert (wavefolder) | `INS_FOLD` | A wavefolder type for the tracks' insert slots (INSERTS > S1 / S2: FOLD): the sound driven past full scale folds back, more folds and harmonics as FOLD rises; the drive follows the track's level as DIST's does, the bass stays dry. 0 is off. |
| FUZZ insert | `INS_FUZZ` | A fuzz type for the tracks' insert slots (INSERTS > S1 / S2: FUZZ): a hard-clipped top and a soft, halved bottom (lopsided, buzzy), the drive with FUZZ. 0 is off. |
| PHSR insert (phaser) | `INS_PHASER` | A phaser type for the tracks' insert slots (INSERTS > S1 / S2: PHSR): four all-pass stages swept slowly (~0.5 Hz); PHSR up to the middle mixes it in, past the middle adds feedback. 0 is off. |
| FRMT insert (vowel filter) | `INS_FORMANT` | A vowel filter type for the tracks' insert slots (INSERTS > S1 / S2: FRMT): three formant peaks over the sound; FRMT moves the vowel from A (low) through E, I, O to U (high). 0 is off. |
| WIDE insert (stereo width) | `INS_WIDTH` | A stereo width type for the tracks' insert slots (INSERTS > S1 / S2: WIDE): one side through slowly swept all-pass filters, the other dry, so a centred mono track spreads out; WIDE sets how much. 0 is off. |
| SFLD insert (sine wavefolder) | `INS_SFOLD` | A sine wavefolder type (SFLD): like FOLD but the folds are round (a sine of the drive), so it sings and aliases less; soft saturation at first, then more and more folds as SFLD rises. 0 is off. |
| RECT insert (rectifier, octave up) | `INS_RECT` | A rectifier type (RECT): up to the middle the wave's lower half is folded up (half-wave, then full-wave: the octave up of an Octavia), past the middle it is driven into a soft clip. 0 is off. |
| HARM insert (harmonic exciter) | `INS_HARM` | A harmonic exciter type (HARM): adds the octave and the twelfth over each note, warm rather than buzzy, more as HARM rises. 0 is off. |
| RING insert (ring modulator) | `INS_RING` | A ring modulator type (RING): the sound times a sine from 26 Hz (a rough tremolo) to 2.6 kHz (bells, metal), RING picks its pitch. 0 is off. |
| SUB insert (octave down) | `INS_SUBOCT` | An octave-down type (SUB): a square an octave under the note, following its level, added as SUB rises. For single-note lines (bass, leads): chords give a rough sub. Not on drum sounds. 0 is off. |
| SLEW insert (slew-rate crunch) | `INS_SLEW` | A slew limiter type (SLEW): the wave can only move so fast, so edges round into ramps, a slow op-amp's dull crunch; light at 1, heavy (muffled, gritty) at 127. 0 is off. |
| TAPE insert (tape tone) | `INS_TAPE` | A tape tone type (TAPE): the highs rolled off (from 6.5 kHz at 1 to 1.7 kHz at 127) and the mids moved around at random every couple of seconds, at times inverted against the lows and highs, as a worn tape plays. TAPE sets how far it wanders and how dark it gets. 0 is off. |
| SPIT insert (random resonant band, driven) | `INS_SPIT` | A spitting band type (SPIT): one resonant band jumping at random between 180 Hz and 5.9 kHz a few times a second, driven into a soft clip at the sound's own level and mixed over it. SPIT sharpens the band and the drive. 0 is off. |
| return feedback sends (B to A, C to A, C to B) | `RET_FB` | The master's returns send to the returns on their left too (MASTER > A SENDS, B SENDS, C SENDS: each return to the other two), for feedback between the effects: a delay into a reverb and back. Each such send takes its return's output of the block before (a loop of 32 samples, 0.73 ms), DC-blocked, limited to -12 dBFS and soft-clipped, at a quarter of the gain of a send to the right, so no loop runs away. Without it a return sends only to its right (A to B and C, B to C). About 1.2 KB of RAM; nothing runs while every such send is 0. |
| GATER insert (tempo-synced trance gate) | `INS_GATER` | A rhythmic volume gate type for the tracks' insert slots (INSERTS > S1 / S2: GATER), locked to the tempo and the sequencer's steps (it restarts with PLAY, follows the swing): one of 16 built-in 16-step patterns (trance gates, 1/8 and 1/16 chops, offbeats, dotted, clave), a RATE of 1/8, 1/16, 1/32 or their triplets, DEPTH (the amount: how far a closed step closes, 0 off) and SHAPE (hard to smooth). Every track can run it at once. |

#### FX: limited instances (once, or a set count)

These run once (on the master, or as the one effect each: chorus, delay, reverb, the effects the returns A, B and C hold) or as insert copies with a count of instances (each a line in the pool): a track picking one when all are in use stays dry until one is free.

| Item | Key | What it does |
|---|---|---|
| chorus | `FX_CHORUS` | The chorus effect, fed by each track's CHO send. About 0.4 KB of flash and 4 KB of pool; off: the CHO sends and the chorus settings disappear. |
| delay | `FX_DELAY` | The tempo-synced delay (echo), fed by each track's DLY send. Costs 128 KB of the pool at the full length: shorten it with the length option, or leave it out. |
| (sub-item) delay length (how long an echo can be) | `DLY_LEN` | The longest delay time, which sets the pool memory the delay takes. A time longer than the line is halved until it fits, so it stays on the beat; shorter lines suit fast tempos only. |
| reverb | `FX_REVERB` | The reverb, fed by each track's REV send and the drums' reverb, with its algorithms below (tick at least one). Each one ticked is on the device's FX > REVERB > TYPE and in the web editor's Reverb popup, switched while it plays; with one ticked there is no TYPE. They share one line buffer, sized to the largest. The biggest user of RAM among the FX (about 17 KB with ROOM); off: the REV sends and reverb settings disappear. |
| (sub-item) reverb: ROOM | `REV_ROOM` | Four delay lines at 44.1 kHz, the reverb as it always was: sparse for its first ~300 ms (separate echoes, a grainy start), then the tail. Its 17 KB of lines set the shared buffer's size when it is ticked. Off, with another algorithm ticked: about 1.8 KB of flash, 2.4 KB of RAM and 2 KB of pool saved. |
| (sub-item) reverb: PLATE (Dattorro) | `REV_PLATE` | Dattorro's figure-of-eight plate at 22.05 kHz: dense from ~50 ms, a smooth decay, nothing above ~11 kHz, about 10 % fewer instructions than ROOM. A 16 KB ring (8 KB at half rate) in the shared buffer. Beside ROOM: about 2.9 KB of flash and 3.4 KB of RAM; with two or more algorithms each one's code runs from main RAM. Experimental: measured on the host and in the emulator only, not yet heard on an FM-1. |
| (sub-item) reverb: FDN8 (long, lush) | `REV_FDN8` | Long and lush: eight slowly modulated lines at 22.05 kHz, dense from ~50 ms, the widest and least ringing tail, nothing above ~11 kHz. Up to SIZE 90 the ROOM's decay; above it the decay doubles every 10 steps to ~14 s at 126 and a near-freeze at 127, the treble kept as DAMP says; about 20 % more instructions than ROOM. A 16 KB ring (8 KB at half rate) in the shared buffer. Beside ROOM: about 4.0 KB of flash and 4.3 KB of RAM (with PLATE too, 1.1 KB of flash and 1.8 KB of RAM are shared). Experimental: measured on the host and in the emulator only, not yet heard on an FM-1. |
| (sub-item) reverb: VTINY (Airwindows VerbTiny) | `REV_AIRWIN` | Airwindows' VerbTiny (Chris Johnson, MIT): sixteen lines in four 4 x 4 Householder stages, left and right in one loop, at 22.05 kHz. A plain early-digital texture: peakier and grainier than FDN8 (it is not modulated), dense from ~80 ms, ringing no more than the ROOM; nothing above ~11 kHz. SIZE as the ROOM's decay up to 90, then FDN8's long top (~11 s at 126, a near-freeze at 127); DAMP the ROOM's treble loss. Its lines fill the shared 16 KB ring (8 KB at half rate: a smaller room). About 40 % more instructions than FDN8. Beside ROOM: about 4.5 KB of flash and 6.5 KB of main RAM (the tanks' code runs from RAM), no pool past the shared ring (user-default has no flash left for it). Experimental: measured on the host only, not yet heard on an FM-1. |
| (sub-item) reverb buffers in the pool (saves ~17 KB RAM) | `REV_POOL` | Keeps the reverb's shared line buffer (17 KB with ROOM, 8.7 KB at half rate; PLATE and FDN8 without ROOM 16 KB, 8 KB at half rate; SPRING alone 8 KB) in the pool instead of main RAM: main RAM is the scarcer, and the sound and the code stay the same. Needs that much pool free (the undo history shrinks by it in the pool and grows by it in RAM). |
| (sub-item) half-rate reverb (half the RAM, no top octave) | `REV_HALF` | Runs the ROOM reverb at half the sample rate (22.05 kHz) behind a half-band filter: its memory takes half the RAM (-8.7 KB) and it costs less CPU, with the same decay and room size. PLATE and FDN8 already run at 22.05 kHz; here they get half the ring (8 KB), a smaller tank with more audible modes. The reverb loses its top octave (above ~11 kHz); the dry sound and the other buses are untouched. |
| PUNCH (16 punch-in FX) | `FX_PUNCH` | Hold FX and press a white key to put the whole mix through one of 16 effects while it is held (loops, reverse, tape stop, half speed, wobble, echo, filters, crush, gate), beat-synced. About 2.3 KB of flash and 64 KB of pool at the full ring. |
| (sub-item) PUNCH memory length (how much mix it can loop) | `PUNCH_N` | How much of the mix the loop, reverse, tape-stop and echo effects can capture; the shorter ring saves 32 KB of pool and cuts the longest loop in half. |
| DUCK (kick ducks the parts) | `FX_DUCK` | Pumping sidechain effect (MASTER > DUCK): every kick from the drum track dips the synth parts, which swell back over an eighth note. About 0.1 KB of flash; has no effect without a drum kick. |
| CHRS insert (chorus copies for the tracks) | `INS_CHORUS` | A chorus type for the tracks' insert slots (INSERTS > S1 / S2: CHRS), beside the chorus send: two taps around 7 ms swept apart at ~0.8 Hz; CHRS up to the middle mixes it in, past the middle widens the sweep. On the drum track it turns the drums' mid into a stereo chorus. 0 is off. Each instance is a line in the pool; a track that picks it when every instance is in use stays dry until one is free (the INSERTS list shows how many are left). A saved project keeps the type, not the instance: on load the tracks get them in order T1 T2 T3 DR. |
| ECHO insert (short delay copies for the tracks) | `INS_DELAY` | A slapback type for the tracks' insert slots (INSERTS > S1 / S2: ECHO), beside the delay send: one repeat 75 ms after the sound; ECHO up to the middle sets its level, past the middle adds repeats (feedback up to 60 %). Up to 93 ms of line an instance (the long, tempo-synced delay stays the send's). 0 is off. Each instance is a line in the pool; a track that picks it when every instance is in use stays dry until one is free (the INSERTS list shows how many are left). A saved project keeps the type, not the instance: on load the tracks get them in order T1 T2 T3 DR. |
| JUNO CHORUS insert (the Juno-106 chorus) | `INS_JCHORUS` | The Juno-106's chorus as an insert type (INSERTS > S1 / S2, and the master's insert slots: JUNO): the MN3009 BBD model of fm-106, MODE I, II or I+II, JUNO its mix (127: the Juno's chorus on). A mono track comes out stereo. About 130 host instructions a sample an instance. 0 is off. Each instance is a line in the pool; a track that picks it when every instance is in use stays dry until one is free (the INSERTS list shows how many are left). A saved project keeps the type, not the instance: on load the tracks get them in order T1 T2 T3 DR. |
| FLNG insert (flanger) | `INS_FLANGER` | A flanger type for any insert slot (a track's, the master's, the USB input's) and the returns (FLNG): a short delay swept 0.25 .. 5 ms (~0.25 Hz); FLNG up to the middle mixes it in, past the middle adds feedback. 0 is off. Each instance is a line in the pool; a track that picks it when every instance is in use stays dry until one is free (the INSERTS list shows how many are left). A saved project keeps the type, not the instance: on load the tracks get them in order T1 T2 T3 DR. |
| (sub-item) reverb: SPRING | `SPRING` | A spring-tank reverb, one of the reverb's algorithms: the chirp and drip of a guitar amp's spring (SPRNG on FX > REVERB > TYPE when two or more are ticked). Its output is mono, and it is a little lighter on CPU than ROOM (emulator). Beside ROOM: about 1.9 KB of flash and 2.3 KB of RAM; ticked alone it is the only reverb. |
| BASS+ speaker mode | `BASSPLUS` | A third MENU > LOWCUT setting (OFF / LOWCUT / BASS+) for the FM-1's small speaker: it adds harmonics of the bass below ~150 Hz, which the speaker can play, and raises the low cut to ~220 Hz. About 0.5 KB of flash and 0.35 KB of RAM; CPU only while BASS+ is on. |
| (sub-item) punch LATCH: FX + key latches its effect | `PUNCH_LATCH` | PUNCH: FX + a key latches its effect so you can let go; the same key or FX + OCT- turns it off, another key switches to its effect, and FX stays lit while one plays. About 80 B of flash. |
| (sub-item) delay TIME 1/8D, 1/16D (SLOOP 2.4) | `DLY_DOT` | FX > DLY > TIME gains 1/8D and 1/16D (dotted: 3/4 and 3/8 of a beat), as SLOOP 2.4. With the 0.74 s delay line a 1/8D below 61 BPM halves to 1/16D. Costs 28 B of flash. |

#### Sequencer

| Item | Key | What it does |
|---|---|---|
| CHORD+ (black keys change the chord), STRUM, VLEAD | `CHORDPLUS` | With a chord mode on (SCL > CHORD), the black keys change the chord played: F# major <-> minor, G# adds the 7th, A# sus4, C# the 9th, D# inverts; hold several to combine, or press one while a chord is held. SCL 2 gains STRUM (1-60 ms a note, right low to high, left high to low; keys and chord steps) and VLEAD (each chord voiced nearest the last). Recorded as it sounds; MIDI out sends the chord unstrummed. |
| song sections | `SECTIONS` | 8 / 16: banks of 4 (SAVE + OCT), stored compressed in one 32 KiB log with a MEM gauge; the old slots move in at the first start. 4: the slots as before (needed by motion recording) |
| snapshots (whole-state slots) | `SNAPSHOTS` | SAVE > SNAPSHOT: the working project, every section and the song in one slot, loaded back whole (the state before is kept in BEFORE LOAD); export / import in the web editor. The flash comes from the end of USR3 (slots + 4 sectors of 4 KiB): the user sample slot USR3 holds that much less (docs/SNAPSHOTS.md) |
| per-track patterns and scenes | `PATTERNS` | 16 patterns a track, stored in the section log; a section becomes a scene that names a pattern a track, so scenes share patterns and an unchanged track costs nothing. The old sections become scenes at the first start. LFO held: the PATTERN layer (launch at the end, the next bar or now; store, copy, clear); the web editor shows the slots in the mixer. About 6.1 KB of flash and 0.1 KB of RAM. Off: sections as before (a scene stored with patterns plays as the section it would be). |
| undo / redo history (many levels) | `UNDO_HISTORY` | Undo and redo of pattern edits (EDIT + OCT- / OCT+) over many levels; the history lives in the pool and RAM this build leaves free (at least 1 KiB). About 2.2 KB of flash; off: a single undo level. |
| performance macros (GLO > MACRO) | `MACROS` | COLOR, MOTION, SPACE, ENERGY: four knobs, each moving several sounds' parameters at once (filters and FM index, LFO depths, sends and width, drive and drum level), kept per project and section (the drum track's unused ENV / LFO DEST values: no format change); recorded by motion recording. At home: no change. About 1.4 KB of flash and 0.4 KB of RAM. |
| (sub-item) ENERGY bands thin / thicken the drums | `ENERGY` | ENERGY also thins or thickens the drum pattern in five bands, walked on the beat: core lanes on the eighths, no ghosts, as written, harder hits, hat ratchets and a snare fill every second pass. About 0.6 KB of flash. |
| REC plain: every note played is recorded, the voice mode never cuts it | `REC_PLAIN` | Experimental (2026-10-21, off for now). Recording keeps every note you play: a chord stays a chord, an overdub only touches a note you play again, and a long note you play into keeps going. The voice mode (MONO, LEGATO, UNISON) decides only what the voice plays, as it does for the keys, instead of cutting a recorded step to one note and joining the notes held over each other. A part on an engine that is mono by nature (ACID) records as before; an arp part always records plain. About 0.1 KB of flash less or more. |
| per-step chance (STEP 2 PROB) | `CHANCE` | Gives each synth step a chance to play (SEQ > STEP 2, KNOB 2: 0 to 100 % in 5 % steps); a step that fails plays as a rest. About 0.5 KB of flash; nothing changes until a step's chance is turned down. Synth tracks only; the web editor does not show it yet. Off: every step plays (chances stay saved). |
| SCL > QNT SEQ (sequenced notes snap) | `QNT_SEQ` | SCL > QNT SEQ: the keys and the sequenced notes snap to the scale as they play, so a pattern follows a change of ROOT or SCALE; the steps keep their notes as written. About 0.3 KB of flash; not on GM KIT or SLICE parts. A build without it plays a project's QNT SEQ as ALL. |
| motion recording (knobs per step) | `MOTION` | Motion recording: while recording, knob turns are stored per step and replayed on every pass (SEQ > MOTION: play on / off per track, clear). The events are the automation store's (up to 128 a track with its locks, nudges and fills); undone with the recording pass, editable from the web editor's protocol (AUTO_GET / AUTO_SET). About 3.4 KB of flash, 0.5 KB of RAM and 1.4 KB of pool (measured before the automation store). |
| REC screen dials: mode, length, count-in | `REC_MODES` | REC screen dials: MODE (free / tempo), LENGTH (1 / 2 / 4 bars) and START (note / 4-3-2-1 count-in). The count-in runs on the internal clock only and in 4/4. About 1.2 KB of flash; off: free recording started by a note. |
| step extras storage (nudge, locks, fills; 2.4) | `SL24_XSTEP` | Keeps SLOOP 2.4's per-step extras with every project: each step's nudge (micro timing), its parameter locks and fill condition (the automation store's step-only events, up to 128 a track), stored in 2.4's own layout while it holds them (24 locks a track), so 2.4 projects carry them over. Storage only: the sequencer features that play them come with their own switches. A section with none costs nothing. About 1.6 KB of flash, 0.9 KB of RAM and 4.2 KB of pool. |
| SEQ DIV 1/2, 1BAR, 2BAR (SLOOP 2.4) | `DIV_LONG` | SEQ > DIV (and the SEQ layer's KNOB 2) go on past 1/4 to 1/2, 1BAR and 2BAR: a step of 2, 4 or 8 beats, for slow chords and pads; such steps are never swung. Stored values unchanged (appended), so SLOOP 2.4 projects load as they play there. Costs 172 B of flash. |
| micro timing: nudge a step (SLOOP 2.4) | `MICRO` | Micro timing, as SLOOP 2.4: hold a step on the SEQ layer and turn KNOB 4 to play it up to half a step early or late (1/64 of a step a click; the step shows a dot). With it, KNOB 4 on a held step is the nudge and the note length moves to SELECT. Needs SL24_XSTEP (the storage). 2.1 KB of flash with the editor commands. |
| fills: GLO + 9 / 10, step conditions (SLOOP 2.4) | `FILLS` | Fills, as SLOOP 2.4: hold a step on the SEQ layer and press OCT+ to make it play only in a fill (F) or never in one (x); OCT- clears its nudge, locks and fill. GLO + key 9 plays a fill while held, GLO + key 10 makes the next bar one. The FX bypass moves to GLO + black keys 1-4. Needs SL24_XSTEP. 2.5 KB of flash with the editor commands. |
| parameter locks per step (SLOOP 2.4) | `PLOCK` | Parameter locks, as SLOOP 2.4: hold a step on the SEQ layer and turn PRESETS to give it its own value of a sound parameter (the last one you turned, ALGORITHM picks another; the title shows it), up to 128 a track with the motion and the other step events, several a step; it goes back at the next step without one. Beside motion recording: motion plays first, the lock wins on its step. The drum kit locks; the macros' places do not. Needs SL24_XSTEP. 4.4 KB of flash with the editor commands, 0.5 KB of RAM, 0.1 KB of RAM code. |
| quick chain: SAVE + section taps (SLOOP 2.4) | `QCHAIN` | Quick chain, as SLOOP 2.4: while playing, hold SAVE and tap up to 8 sections (of the bank shown: A..P with 16 sections): the first plays from the next bar, and when SAVE is let go they play in that order, each for the bars of its longest pattern, looped; one tap is a plain jump and stops the chain, STOP ends it. RAM only. 1 KB of flash. |
| step conditions 1:2 .. 4:4 (Felucca 1.5.6) | `COND` | Step conditions from Felucca 1.5.6: a step plays only on loop a of every b of its track (1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4), counted from PLAY, a song section or a launched pattern. With FILL and the chance on the same step: the condition, then FILL, then the chance. A held step's EXTRA page, COND. A step-only event of the automation store (it builds the store); an older build plays such a step always. About 1.4 KB of flash beside the store's other items (11 KB when it builds the store alone). |
| STEP tools and note generators (Felucca 1.5.6) | `SEQTOOLS` | The sequence tools of Felucca 1.5.6 on STEP's last three pages: TOOLS (REVERSE, RANDOM: new notes in the scale or a new beat, COOK: a few small changes, LENGTH x2 / half), each one asking first, and the note generators GEN / GEN OUT (EUCLID, RANDOM, PULSE; REPLACE, ADD or MASK the pattern): played live over the pattern, never written into it but by WRITE, which asks. On a drum lane picked with ALGORITHM, that lane only. Notes keep their exact lengths; locks, chance and conditions move with their steps (the EDIT layer's SHIFT and LENGTH too). About 5.3 KB of flash. |

#### USB & BT & MIDI

| Item | Key | What it does |
|---|---|---|
| USB MIDI (always: notes, clock, the editor, updates) | `USB_MIDI` | USB MIDI is in every build: notes and clock both ways, the web editor, and firmware updates (the installer and the builder flash through it). Shown for the record, it cannot be switched off. |
| USB serial console (logs, diagnostics) | `USB_CONSOLE` | A read-only serial console on the USB port, next to USB MIDI: the logs and the diagnostic commands (with Bluetooth audio, its btinq / btpage traces). Not with USB audio: the USB port has three data endpoints, USB MIDI takes one and USB audio the other two. |
| USB audio (4 stems in, stereo out) | `USB_AUDIO` | 4 stems in + stereo out (UAC1) next to USB MIDI; the stems are resampled to the computer's clock (cubic, after X0X 80b7d40), so a host passing the input live to an output does not drop out. Needs +12 KB pool (a resampler in the audio path is CPU-heavy). It streams on the FM-1 since 2026-10-12 (before, a USB audio build hung and went to the USB rescue); its audio quality is not checked yet. Not with the serial console: the USB port has three data endpoints, USB MIDI takes one and USB audio the other two. |
| TRS MIDI IN | `UART` | MIDI input on the TRS jack (notes and clock from a keyboard or sequencer). About 0.6 KB of flash; off: the jack hears nothing, and SYNC TRS has no clock. |
| MIDI clock in + out | `MIDI_CLOCK` | MIDI clock both ways. In: follow an external MIDI clock and start / stop (GLO > SYSTEM SYNC: AUTO takes the TRS jack, else USB, else the internal tempo). Out: send MIDI clock on USB so a computer, drum machine or sequencer can follow the FM-1: 24 ticks a beat (also while stopped, so the other side has the tempo first), Start, Stop, and Song Position + Continue when the FM-1 itself continues an external song. Each tick leaves with the sound of its beat (latency-compensated, no jitter of its own), follows tempo changes and the nudge, and never swings. Switched on in the HOME menu (CLOCK OUT, off by default). While the FM-1 follows a USB clock nothing is sent back to USB; following the TRS jack, the clock is passed on to USB. The TRS jack is input only (no MIDI out on the FM-1's board). Off: the FM-1 always runs on its own tempo and sends no clock. |
| MIDI expression (bend, mod, sustain, RPN) | `MIDI_EXPR` | Plays more than notes from MIDI: pitch bend, mod wheel, breath, foot, aftertouch, sustain pedal and pitch-bend range (RPN 0). About 0.5 KB of flash; off: only notes and the panic messages work. |
| MIDI note out of the sequencer (GLOBAL MIDI OUT = BOTH or SEQ) | `MIDI_NOTEOUT` | MIDI OUT = BOTH (SLOOP 2.4's SEQ: the keys and the sequencer) or SEQ (the sequencer alone, no keys): what the sequencer, the arp and the rolls play also goes to MIDI out, each track on its channel, every note ended (STOP, a channel changed, back to KEYS). Switched on GLOBAL's MIDI page (MIDI OUT KEYS / BOTH / SEQ). About 1.2 to 2.5 KB of flash, by build; off: the line goes and only the panel's keys go out. |
| Bluetooth LE MIDI, our own stack | `BLE` | BLE MIDI as the stock firmware offers it (named Optimist XXXX, the BLE-MIDI service), from a stack written for Optimist (docs/BLE-STACK.md): BLE in plays the synth, the FM-1's own notes go out with real timestamps. Tested on an FM-1 with macOS Audio MIDI Setup (not yet with iOS or Windows). The radio's start-up tables, captured from the stock V15 firmware, are included: no V15 file is needed (FM1_STOCK_FWSC makes a fresh capture instead, with the same result). Bluetooth starts OFF: switch it on in SYSTEM > Bluetooth. About 32 KB of flash and 6.5 KB of RAM (the menu shows the measured figures; BLE_DIAG below adds more); where the build then overflows, ticking it removes the smallest single item that frees enough (sample sets first, then other items), or the one you pick instead. |
| (sub-item) BLE diagnostics: the console's blell, counters and rings | `BLE_DIAG` | For finding BLE faults: the console's blell command (needs the serial console, USB_CONSOLE) and the counters and rings behind it (link-layer events, the radio's receive and transmit state, the protocol packets, BLE-MIDI in). About 6 KB of flash and 2.3 KB of RAM, and 6 KB of flash more with the console (USB_CONSOLE) for blell itself. Leave it off for normal use: BLE works the same without it; bletrim and blevm stay, and so does the boot breadcrumb. |
| (sub-item) BLE bonding: pairing (Just Works) and an encrypted link | `BLE_BOND` | LE legacy pairing with Just Works (no passkey, as the FM-1 has no screen keypad for it) and bonding, so a Mac or phone that pairs once gets an encrypted link and finds the FM-1 bonded after a power-off. The FM-1 hands out its identity and takes the other side's (its IRK and identity address, kept until power-off for now), which reconnecting to devices with private addresses needs later. Pairing happens only when the other side asks for it. Paired with a Mac on an FM-1 (2026-10-18). About 3.6 KB of flash and 0.4 KB of RAM. |
| (sub-item) BLE devices: connect to BLE-MIDI devices, reconnect the last one | `BLE_CENTRAL` | SYSTEM > Devices lists the BLE-MIDI devices nearby (keyboards, controllers, a Mac or an iPhone app that advertises BLE MIDI, another FM-1) with their names and relative signal bars, scanning only while the list is open (the FM-1 stops advertising meanwhile). Picking one connects to it as a central (pairing with Just Works when it asks, as Apple's devices do), MIDI both ways; it becomes the last device, which the FM-1 reconnects to by itself while Bluetooth is on. Brings the bonding of BLE_BOND with it. Without this item DEVICES shows NONE and the last device only. |
| Bluetooth Classic Audio (A2DP Source, SBC) | `BT_AUDIO` | Bluetooth Classic audio out (A2DP source, SBC 44.1 kHz) to headphones and speakers: SYSTEM > Bluetooth's AUDIO MAN and AUDIO AUTO modes, the radio carrying BLE MIDI or audio, one at a time. Our own stack: inquiry, paging, Secure Simple Pairing (Just Works), E0 encryption, L2CAP, AVDTP and a fixed-point SBC encoder. Host-tested; on the FM-1 the scan has heard no device yet, so no audio has been streamed. |
| (sub-item) Bluetooth audio diagnostics: the console's btrx / bttrace, counters and logs | `BT_DIAG` | For finding Bluetooth audio faults on a console build (USB_CONSOLE): the btrx and bttrace logs, the status histograms and the per-second counters (receive status bits, signal level, gain), and the live btpage pll= / agc= / follow= commands, and every line the Bluetooth stack writes to the console (btinq, btpage). About 14 KB of flash and 6.4 KB of RAM with the console. Leave it off for normal use: the audio works the same without it. |

#### UI

| Item | Key | What it does |
|---|---|---|
| say what a project uses and this build lacks | `MISSING_WARN` | 'MISSING: PHYS T2, KIT 909' in the top bar when a project, song section, user preset or kit uses an engine, kit, sample set or FX this build leaves out (once per item until power-off; never stalls the audio); PROJECT > TOOLS > MISSING lists them again. Off: they play their stand-ins silently (saves 1.5 KB of flash) |
| favorites: mark sounds, browse only them | `FAVORITES` | With the presets: SHIFT2 (OCT+ held) + PRESETS marks the sound playing, SHIFT2 + SELECT browses only the favorites, a star after a favorite's name in the toast and in PROJECT > USER (SHIFT2 + ITEM marks a slot). Up to 64 factory sounds (kept by engine and name: they stay marked over builds that leave engines out) and any of the 32 user presets (an erased preset loses its mark); the FM6 / CZ banks and the drum track have none. Kept with the settings (136 B at the end of the settings record: an older record reads whole with none; a build without this item keeps the rest and forgets them on its next save). About 1 KB of flash, 136 B of pool. Off: no marks. |
| help line: what the knob changes, in words | `PARAM_HELP` | the screen's last line names what a knob changes in plain words, e.g. 'Filter cutoff', 'Reverb send', while a value turns and ~2 s after, and always on GLOBAL, SYSTEM and PROJECT (the cursor line, the hot action, ' -> SAVE' after an action that asks first: the button that runs it); only the lines of the features built (tools/param_help.json, also the web editor's tooltips). About 7.7 KB of flash (the texts packed), no RAM code |
| SYSTEM's debug pages (internals, USB stats) | `SYS_DEBUG` | SYSTEM keeps its pages for finding faults: INTERNALS (the last reset, the flash's interrupts-off time, the second core's internals), USB STATS (the USB audio glitch counters) and on INFO the clock, the second core, the drops and the CPU guard apart. Off: INFO shows what a player wants, CPU LOAD, UNDERRUNS (the drop-outs counted in) and BATTERY, in the large face, and the code of the rest is left out (about 2.9 KB less flash, measured on a USB-audio build). The red / amber mark on the header's rule after a drop-out stays either way. |
| knob acceleration by turn speed | `KNOB_ACCEL` | 1 / 2 / 3 / 5 / 8 steps a detent when turned fast; never on lists (engines, kits, presets) |
| screen data speed (SPI clock) | `LCD_BAUD` | How fast data is sent to the display: 60 MHz / (n + 1). Faster redraws finish sooner (less tearing); the ST7789V takes ~62 MHz, so 60 MHz is the limit. No flash cost; the faster settings are untested here on hardware. |
| screen: send only the changed rectangle | `LCD_DIRTY` | Redraws only the part of the graph strip that changed instead of the whole strip: less data per frame and less tearing (with the 30 MHz screen clock). Costs about 1.5 KB of flash and 0.6 KB of RAM. |
| keys light the notes played | `KEYLIT` | Lights the keys of the notes the selected track plays (its steps, the arpeggiator, held notes), so you see the pattern on the keyboard. About 0.2 KB of flash, LEDs only; with LIGHTS, MENU > NOTES turns it on and off at run time. |
| screen brightness (MENU > BRIGHT) | `BRIGHT` | MENU > BRIGHT: the screen backlight in 8 levels, 4/16 at the lowest (software PWM). The level lasts until power-off: every boot starts at full and a dim level is never saved. About 36 B of flash; X0X reports it working on a real FM-1. Off: always full brightness. |
| menu LIGHTS / KEYS / NOTES (play in the dark) | `LIGHTS` | Lights for playing in the dark: MENU LIGHTS (every button glows OFF / LOW / MID / HIGH), KEYS (C or white keys glow) and NOTES (the note lights of KEYLIT on / off at run time). About 1.2 KB of flash. |
| divisions in length order (1/8 8T 1/16 ...) | `DIV_ORDER` | Lists note divisions in length order (1/4 1/8 8T 1/16 16T 1/32) on ARP RATE, SEQ DIV, DELAY TIME and SLICER RATE; stored values are unchanged. About 0.2 KB of flash. |

#### System

| Item | Key | What it does |
|---|---|---|
| web editor (USB SysEx) | `EDITOR` | The web editor's communication layer: the frames and INFO, BUILD, PING. What it can do comes with its options: BACKUP, LIBRARIAN, live editing (ED_LIVE) and its features. Firmware updates (the installer, M-UPGRADE) and the USB rescue are not part of it: every build has them. |
| (sub-item) backup / restore from the web editor | `BACKUP` | everything in flash to one .optimist-backup file and back (editor cmds 43..48; without live editing the sample slots' 11..15 and the snapshots' 54..57 too, which a restore writes through); a restore onto another build reports what it skips and what plays a stand-in |
| (sub-item) web editor: librarian (projects, presets, patterns, kits) | `LIBRARIAN` | The web editor manages the user data one piece at a time: the user presets (the Library: list, rename, move, erase, files) and the user drum kits (load, store, erase, files), with live editing or without; without it also the sections A..P (load, save, a file each) and the per-track patterns (copy, clear, files), next to Backup. Editor cmds 16..21, 38..41; without live editing 9 and 79..84 too (editor/ed_lite.c). |
| (sub-item) web editor: live editing (else backup / restore and the librarian) | `ED_LIVE` | The web editor edits the FM-1 live: the sounds' values, presets, user presets, tracks, steps, the mixer, kits and the ED_* pages, kept in sync both ways. Off: the editor's side is backup and restore only (the frames, INFO, BUILD, the user sample slots a restore writes, the snapshots, cmds 43..48: editor/ed_lite.c); the web page then shows its Backup (and, with LIBRARIAN, the librarian) alone. The ED_* items do nothing without it. |
| (sub-item) web editor: the mixer (effect slots, drum lanes, solo and mute) | `ED_MIX` | The web editor's mixer beyond levels and pans: every FX slot by owner with its parameters (track inserts, the drum bus and lane inserts, master inserts, returns, the USB input; cmds 86, 87, 94), the drum lanes' pan, mutes, solos and LFOs (cmd 95), track and lane SOLO and MUTE (cmd 104). Off: the editor's Inserts / Returns pop-ups and FX pages, the lane LFOs and the strips' SOLO are not offered; the levels, pans and sends stay. |
| (sub-item) web editor: the sequencer (lane steps, pattern tools, song chain) | `ED_SEQ` | The web editor's sequencer tools beyond the steps themselves: a step held in a drum lane's context with the device's own list of lockable values (cmd 105; with the automation store), the pattern tools REVERSE, RANDOM, COOK, LENGTH and EUCLID run by the device (cmd 98; the Optimist UI and SEQTOOLS), the song chain (cmd 97; the arranger). Off: the editor lists raw parameter ids, has no tools buttons and no song chain. |
| (sub-item) web editor: the device's screens (cards, GLOBAL / SYSTEM, scope) | `ED_SCREENS` | The web editor's Device tab: the Optimist UI's SOUND, MIX, MASTER, PROJECT, STEPS, SONG and TEMPO pages as rows of cards (cmd 99), GLOBAL and SYSTEM as the device's lines with their help (cmd 96) and the oscilloscope (cmd 101). Off: no Device tab; the editor keeps its own Sound / Mixer / Sequence tabs. |
| sound tables and FM6 buffers in the pool, not main RAM | `TAB_IN_POOL` | Moves what the sound loops read from main RAM to the pool: the sine, soft-clip and GRAIN window tables (about 2.5 KB, copied there from flash at start-up) and FM6's sine table, operator buses and voices (about 5.9 KB), so builder items get that much more main RAM. Unverified on hardware: both are the same SRAM as far as is known and the emulator cannot tell a speed difference, so on the FM-1 it may be slower; compare the console's TABBENCH figures and the CPU meter with it off and on. The sound is the same. |
| predictive CPU guard (ease back before shedding) | `CPU_GUARD` | Avoids audio dropouts under heavy load by easing quality back step by step before cutting notes. Under overload (the measured load or the one predicted from the voices sounding, over 85 % for 8 halves, or a late half): first ACID without oversampling and ANALOG 2's swarm at 2 copies, then UNISON at 2 voices, then voices shed, never the bass or the lead (MONO / LEGATO / UNISON parts) nor the drums; back after 2 s under 80 % counting what each step saved; SYSTEM > INFO's GUARD line (SYS_DEBUG) shows the step. Weights: tools/builder/cpu_costs.py from tests/cpu_baseline.txt (docs/CPU-GUARD.md) |
| more memory from the data cache (for the undo history) | `CACHE_RAM` | Uses 2, 4 or 7 of the 8 data-cache ways (4 KB each) as RAM; 7 is what M-VAVE's own FM-1 firmware and the AC79 SDK's flash-only builds do; the instruction cache (the flash's code and constants) keeps all its ways. The memory is tested at start-up; when the test passes it becomes the undo history's second part (28 KB instead of what main RAM has left), else everything is put back as it was and nothing uses it. It costs about 0.4 KB of flash and 0.1 KB of fast RAM code. Off until checked on an FM-1: whether the flash's data reads use the data cache (then the sound's tables in flash could cost CPU) is not known; the console's tabbench (flash 16K) and boot lines show it. |
| hand-written assembly speed-ups (FM6, ANALOG 2) | `ASM` | The hottest loops of FM6 and ANALOG 2 written in the processor's assembly: the same sound, about 8 to 19 % less audio CPU (emulator), for about 0.6 KB of flash and 0.7 KB of fast RAM code. Keep on; off only to compare with the plain C. |
| second CPU core renders parts 2-3 | `DUAL` | The FM-1's second CPU core renders synth parts 2 and 3 while the first renders the rest, cutting the first core's load by 40 to 44 % in the emulator, with the same sound. It costs about 3.3 KB of flash, 3.7 KB of RAM and 6 KB of pool. On some FM-1s the second core misreads RAM at the boot loader's supply: it then runs nothing it misread, is held for the session and the FM-1 goes on with one core (the message CPU1 OFF MISREAD); a boot after one that crashed in its first 30 s stays on one core. |
| (sub-item) raise the core rails to the FM-1's stock values at start-up (for the second core) | `STOCK_SUPPLY` | At start-up, before the second core, the audio and Bluetooth, the FM-1's rails are set as its own M-VAVE firmware sets them: the 1.4 V rail moved from its LDO to the DC-DC, then raised to 1.60 V (VDC14 7), then the core rail to 1.38 V (SYSVDD 15), a step at a time, never lowered (the boot loader leaves 1.40 V and 1.26 V). Read back after: if a value is wrong, the second core stays off (2ND CORE OFF SUPPLY) and the FM-1 runs on one core. SYSTEM > INTERNALS shows the rails (SUPPLY). Second-core builds only; it replaces the earlier DUAL_BOOST (1.50 V / 1.38 V on the LDO). On the second core's test unit both cores crashed within seconds at the boot loader's rails and ran 3 x 3 minutes with 1.50 V / 1.38 V; the stock sequence itself is still being measured. |
| SLOOP's data kept safe (never erased) | `SL24_SAFE` | Coming from SLOOP (2.7): Optimist never erases or writes over what SLOOP left that it cannot read: the four section slots and the autosave (shown as SLOOP on the PROJECT page and in the editor, not EMPTY), SLOOP's FM6 bank and a user sample longer than ours (USR3, USR4); SLOOP's user presets are read right, and one Optimist saves into SLOOP's bank leaves SLOOP's presets there byte for byte. SLOOP's settings (its SYN1..SYN4 drum kits, MIDI LEARN, theme and favourites) are never read as Optimist's and never written over: Optimist keeps its own beside them, so each firmware finds its own after a switch. Off, the first start erases SLOOP's projects. About 0.7 KB of flash. |
| import SLOOP 2.7 projects (LOAD twice) | `SL24_IMPORT` | A SLOOP 2.7 project left in a section slot shows as SLOOP on the PROJECT page; LOAD it twice and it becomes the working project (values, engines, drum kits, steps; nudges, locks and fills with the step extras storage), then SAVE puts it in a section; PROJECT > SLOOP IMPORT (twice) does the same with SLOOP's autosave. The original stays in flash. The drums: each of SLOOP's 16 sounds with hits gets a lane playing it (on its default lane when it has one), with its TUNE and DECAY; more than 10 used: the 10 with the most hits (said DRUMS CUT). Held notes stay held. An FM6 part keeps its factory patch as its voice, with SLOOP's macros in it; a USR1..USR3 kit plays the same user sample slot from the lanes. Lost: the INSERT, ENV SYNC, LFO 2's TRIG / POL, GLO > TEXTURE, a NOISE part (ANALOG), a SYN kit (the default kit), the arp modes past ORD (the nearest, said ARP CUT); the ANALOG filter TYPE goes to ANALOG 2's, LFO 2's SYNC to the LFO's tempo-synced RATE. About 3.5 KB of flash (unmeasured since 2026-10-19). |
| export a project for SLOOP 2.7 (web editor) | `SL24_EXPORT` | Adds Export for SLOOP 2.7 to the web editor's Projects screen: the working project as a SLOOP song file, which SLOOP's editor loads (Song & sections > Load a song file: it replaces the song on that FM-1, never its settings). Nothing is written to the FM-1. The drum lanes go onto SLOOP's sounds with their TUNE and DECAY, held notes stay held. Lossy, and the editor lists what is left out: FX OFF, ANALOG 2, SLICE / ACID / CZ parts (their fallback), an FM6 voice that is not one of SLOOP's F1..F8 (F1), X0X and user kits (808), drum lanes on user samples or sending apart, the reverb type, COMP / LIMIT, the steps' chance, motion, locks on those; sections and samples stay behind. About 1.5 KB of flash (unmeasured since 2026-10-19). |
| import a SLOOP 2.7 song or backup (web editor) | `SL24_EDIMPORT` | Adds Import a SLOOP 2.7 song or backup to the web editor's Projects screen: a project of a SLOOP song file or backup file (its loop, or A..D of a backup) becomes the working project, as LOAD twice does with one left in flash; SAVE then keeps it in a section. Nothing else is written. Needs SL24_IMPORT. About 1.2 KB of flash and 0.4 KB of pool. |

### Where an item came from

Items ported from another project carry their provenance (project, author, licence, commit) in the registry; the menu
shows it in the details panel. Items from X0X (`charlesvestal/fm1-x0x`, GPL-3.0-only) show a NOTICE when selected. Wha
X0X uses only by its author's permission (its break player, no licence) is never offered (`registry.FORBIDDEN`; a
`.config` naming it is refused).

### Retired items

Items taken out of the registry are listed in `registry.RETIRED` with the reason. Their bits are never reused, and a
`.config` that names one loads, warns "dropped" and goes on without it. They include the whole FM6 mode set (`FM6_MARK1`,
`FM6_MODERN`, `FM6_OPL`, `FM6_MKI_FLASH`, `FM6_STORE`, `FM6_VOICES`, `FM6_ALL`, `FM6_ALGO`: FM6 has one synthesis, Dexed's
MODERN, and the on-device operator editor), `UI` (SLOOP's UI is gone; the Optimist UI is the only one), `SIMD` (wrong on
the real FM-1), `KEYS_DMA`, `SPLASH`, `LFO2`, and the old editor pieces (`ED_REC`, `ED_UNDO`, ...). Old data that named a
retired FM6 voice (an R or U number) plays the voice it holds inline, or starts from INIT VOICE; an old MOD / M.TIM /
C.TIM over an inline voice is baked into it once on load, approximately.

### Song sections (SECTIONS)

16 (default) or 8: sections A..P / A..H in banks of 4 (SAVE + OCT- / OCT+), stored compressed in one 32 KiB log
(`firmware/src/storage/sections/sec_log.c`); the song layer's title shows the MEM gauge ("mem 34% +12": the share used and
how many more sections of the last size fit). MEM FULL refuses a new section; room for one record of the longest size
is always kept, so the playing section can always be saved, and clearing always works. A record never spans two sectors,
so `sec_log.c` models its compaction and takes a store only when a record of the longest size would still go in
afterwards. 4: the four project slots as before. The first start of an 8 / 16 build moves the old slots in as A..D.

Each section's record carries its automation (motion, locks, nudges, fills, chance, conditions: the extras record).
The song chain is 64 parts with 8 or 16 sections (16 with 4), a record of the log (backup object `SNG1`). The log's ids
are the same in every build, so going from 16 to 8 sections and back loses nothing. Data stored in the app slot's unused
end does not survive an update (the package pads the app with 0xFF), so only the data areas (the log, the USR slots) can
hold sections. Tests: `tests/sec_codec_test.c`, `tests/sections_test.c`.

### Snapshots (SNAPSHOTS)

SAVE > SNAPSHOT keeps the whole state (the working project, every section, the song) in a slot and loads it back
(`docs/SNAPSHOTS.md` has the design). 4 (default), 2 or 8 slots plus BEFORE LOAD. The flash comes from the end of USR3
(slots + 4 sectors of 4 KiB), so USR3 holds that much less; a build without snapshots has the full 64 KiB USR3 again. A
long USR3 sample uploaded to a build without snapshots is not overwritten by a build with them (`USR3 SAMPLE IN THE
WAY` until USR3 is erased or loaded again). Cost: `SNAPSHOTS` in `costs.json` (the slot count is nearly free).

### Backup and restore across builds

The editor's backup holds every stored object (cmds 43..48). `BK_LIST` tells what the device's build holds (engine, ki
and sample-set masks by UID, slot capacities, the section count and record layout); before a restore the editor shows a
report and asks to confirm. Objects not in the build, or larger than its slot, are skipped whole (never truncated), and
each part whose engine, sample set or kit the build leaves out is named ("section C track 2 uses PHYS: plays ANALOG,
settings kept"). A section's automation travels in its record (S01..S16, byte for byte). With 4 sections the motion sits
outside the project's payload, so a PRJ1..PRJ4 / AUTO backup does not hold it.

## How a switch works

- `firmware/src/core/registry.h`: the engines as an X-macro list with their permanent UIDs and fallbacks, and the
  defaults of every switch. Optimist is one translation unit: an item left out is a constant `0` (`ENG_IS()`,
  `if (FELUCCA_FX_X)`), so its code, tables, RAM and pool go. Pages and cells of a missing FX disappear (`params.c`
  `page_shown`, `cell_built`).
- `tools/builder/configure.py` turns a `.config` into `build/gen/felucca_config.h` (included first by `tools/build.py`)
  and the generators' environment (sample sets, PERC, SLICE's BREAK). `build/builder.config` is the configuration of the
  last build.
- Stable IDs: projects, user presets and the drum lanes store engine, kit and sample-set UIDs. A part whose engine this
  build leaves out plays the fallback (FM6 -> DIGITAL -> ANALOG, GRAIN -> SAMPLE -> ANALOG, SLICE -> SAMPLE, CZ -> PHASE,
  the rest -> ANALOG) with its defaults, shows `FM6*` on HOME, and keeps the original engine, preset and values: they are
  written back on save as long as you leave that part's sound alone, so a full build plays it as before
  (`tests/builder_rt_test.c`). A kit not built plays the other source's first kit; a sample set left out keeps its number
  (empty).
- `MISSING_WARN` (`firmware/src/storage/miss.c`): after a load the main loop scans the working project for what this
  build lacks and says it in the top bar ("MISSING: PHYS T2, KIT 909 +2"), once per item until power-off. SAVE > TOOLS >
  MISS lists them. `tests/missing_test.c` loads a full build's project on a reduced build.
- The knob's help line (`PARAM_HELP`): the texts are `tools/param_help.json` (every page, TRACKS, SONG and REC included,
  every engine's EDIT values; also the web editor's tooltips);
  `tools/gen_param_help.py` writes `build/gen/felucca_param_help.h` with each group under its feature's condition, the
  texts packed with a byte-pair code and unpacked on the stack when drawn (`firmware/src/ui/ph_blob.c`).
  `tests/param_help_pack_test.c` checks every line unpacks to the table's text.
- The firmware reports what it is: `INFO` adds each slot's engine UID, `BUILD` (cmd 49) the configuration's name, hash and
  one bit per registry item (`web/EDITOR_PROTOCOL.md`).

## Budge

`tools/builder/measure_costs.py` builds every item at every non-default value (measurement builds link past the slot)
and writes `costs.json`: the default build's sizes, each item's delta, the pairs, the groups and the check
configurations the estimate is held against. The menu's build gives the exact figure.

**After adding an item or a pair: `make costs`** (`optimist.py costs`, i.e. `measure_costs.py --missing`). It measures
only what `costs.json` lacks and the base again when it moved; `--missing --check` builds nothing and fails when
something lacks. `tests/builder_test.py` and CI's `host-tests` job run that check, so an unmeasured item cannot land. I
does not notice an item whose code changed after it was measured: re-measure it with `--only KEY`. `make costs-all [JOBS=n]` measures everything again
(about 440 builds at 20 s or less; on a fast Linux host with `tools/farm_size.sh` for single builds). Commit `costs.json`
and `web/builder/data/` together (`tools/export_builder_web.py`).

The estimate is **anchored**. The plain sum (base + deltas + pairs + groups + computed terms) drifts the further a
configuration is from the defaults, because the firmware is one LTO unit compiled for size. So `configure.py budget`
starts from the nearest configuration whose real build is known (the defaults, every check configuration and probe in
`costs.json`, every real build made from this source in `build/exact-sizes.json`) and adds what the sum model says the
differing items change, plus `ANCHOR_MARGIN` (256 B of flash per differing item, so it errs over). A configuration buil
once is exact from then on.

What the sum model adds beyond per-item deltas:

- **Pairs** (`PAIRS` in `measure_costs.py`): items whose cost depends on another's value are measured together; the pair
  holds what the build has beyond the estimate without it (the sampled kits sharing PERC, the reverb algorithms, the X0X
  kits, all sample sets off, the web editor's three features, MICRO / FILLS / PLOCK, ...).
- **Groups** (`GROUPS`, from the firmware's own `#define`s): code built with any of a set of items, counted once (the
  insert slots `INS_FX`; the automation store's code `AUTO`).
- **Computed terms** (`configure.py`): the reverb's shared line buffer (`rev_lines`: the largest algorithm built, halved
  by REV_HALF, moved to the pool by REV_POOL), the automation store's arrays (`auto_store`; the store is built with any
  of MOTION, CHANCE, SL24_XSTEP, MICRO, FILLS, PLOCK), and `part_mem`, the synth parts' engine states (a union of FM6,
  PHYS, GRAIN and ACID: the largest one built).
- Built only with another, not an item: the drum ACCENT lane is there wherever the automation store is (it is stored as its events).

**Checked against real builds.** Every run of `measure_costs.py` that builds also makes a measurement build of every
profile, of each configuration in `tools/builder/estimate/` and of each one's probe (4 items changed), the `checks` of
`costs.json`. `tests/builder_test.py` then (1) estimates each of them without its own build (from its probe, 4 to 5 items
away) within 6 KB of flash, 512 B RAM, 1 KB pool and 768 B RAM code (`ESTIMATE_NEAR`), and (2) the **drift** check:
`optimist.py test` first makes a measurement build of every check configuration from the source under test, and each
estimate (`costs.json`'s anchors only) must be within 1 KB of flash and 256 B elsewhere of today's real build
(`ESTIMATE_TOL`). A change that moves the firmware past that fails the test run; `make costs` fixes it. `--python` and
`--no-build` runs have no real build and skip it.

### BLE and the room it needs

BLE costs about 29 KB of flash and 6 KB of RAM from the defaults (`BLE` in `costs.json`), about 18 KB when Bluetooth
Classic Audio is built (they share the Bluetooth stack: the pair `BLE=1,BT_AUDIO=1`); `BLE_DIAG` adds 6 KB and 2.4 KB.
Ticking BLE in the menu (the CLI menu and the web builder) **removes nothing**: like any other tick the meters go over and
the message says it does not fit (the user, 2026-10-21; an earlier rule unticked an item for you, which made BLE on show
more free flash than BLE off). Remove what you want by hand, or let `--fit` choose.

Headless: `--ble-drop ITEM` on `optimist.py build|package` and `configure.py` (with `--set BLE=1`) removes that one item,
which must free enough (`tools/builder/room.py` make_room). `optimist.py test` builds its BLE emulator packages with it.

**Shared DSP blocks (`docs/DSP-SHARED.md`).** A block several items use is `always_inline`, so each built item compiles its
own copy and an item left out takes its copy with it: no "shared by A or B" term is needed. A shared *table* is
`static const` in the one unit and is in the base while any default item reads it.

## Verification

`tools/builder/verify.py [--random N] [--emu]`: every profile and N random configurations (fitted by the estimate)
build, link and fit; no symbol of an item left out stays in the ELF; `tests/regress.c` built with the configuration
renders every present preset bit-identically to the full build's goldens; with `--emu` the emulator boots each image and
the audio stays silent. Every engine built has a loadable PRESETS entry and every drum source a ki
(`tests/preset_cover_test.c`, run by `tests/builder_test.py`). Host tests: `tests/run_tests.sh` (BUILDING.md, "Tests";
it includes the full -> reduced -> full project round trip).

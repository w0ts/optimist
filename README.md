<h1 align="center">Optimist</h1>

<p align="center"><b>An open, modular firmware platform for the M-VAVE FM-1 groovebox: build your own firmware, with the features and the interface you choose, on any computer.</b><br>
Free software (GPL-3.0-only), derived from <a href="https://github.com/isod89/sloop-fm1">SLOOP</a>, with parts of Felucca / Melodee, X0X and other FM-1 firmware projects.</p>

> [!CAUTION]
> **WORK IN PROGRESS. NO GUARANTEE. USE AT YOUR OWN RISK.**
>
> - **Optimist has only been tested in the emulator.** It has **never run on a real FM-1**. No build in this repository has been checked on a device.
> - It is unfinished experimental software, and it changes often. Expect bugs and missing features. Expect formats to change between versions.
> - Installing it on your FM-1 could fail, stop the device from starting, or erase your projects, presets and samples. **Back up everything before you try it**, and read [Installing](#installing-on-an-fm-1) and the recovery steps first.
> - It comes with **no warranty and no support**, as the GPL-3.0 licence says (sections 15 and 16): you use it at your own risk, and nobody is liable for any damage to your device or your data.

## Why Optimist

**1. A modular firmware: you build your own.** The FM-1's flash is small, and no single firmware can hold every engine, kit and effect. Optimist splits them into parts, and the **firmware builder** (`make builder`) lets you choose the ones you want:
- synth engines, drum kits and sample sets;
- effects;
- sequencer features;
- MIDI and USB options;
- the user interface.

The builder shows the flash and RAM budget as you choose, then builds the firmware. Every part is a build-time switch, so a part you leave out costs no flash, RAM or CPU. Ready-made profiles are a starting point (`config/profiles/`). If a project needs a part your build doesn't have, the device warns you when it loads. See [docs/BUILDER.md](docs/BUILDER.md).

**2. More than one user interface.** The interface is also something you choose at build time.
- **Today:** Optimist ships SLOOP's interface (hold a function button, touch a key).
- **In progress:** Optimist's own interface, a second choice in the builder. It has one uniform navigation (SELECT moves the cursor, ALGORITHM picks the track, PRESETS sets the value, SAVE = yes, HOME = no), the 16 keys as steps, and Felucca's look. It is being written and is **not in this release**.

**3. A platform for everybody.** Optimist is meant to be built, changed and extended by anyone, not just used:
- **Free software:** GPL-3.0.
- **One command to build** (`make setup`, then `make builder`) on **macOS, Linux and Windows**. Windows is not verified yet; see [Requirements](#requirements).
- **An emulator,** to try a build without a device.
- **A web editor** for every parameter.
- **A home for work from the whole FM-1 community:** SLOOP, Felucca, Melodee, X0X and others, each ported part credited and licensed (see [Credits](#credits)).

New engines, effects and interfaces are welcome as builder options; see [Contributing](#contributing-and-reporting-issues).

## What it is, and what it is not

Optimist turns the FM-1 into a four-track groovebox that you play live: three synth tracks and a drum track with 16 sounds on the white keys. It started as SLOOP-plus, a fork of SLOOP, and keeps SLOOP's way of playing (hold a function button, touch a key). It adds engines, drum kits, effects, a pattern and scene system, snapshots, MIDI and USB audio, a web editor, and a firmware builder that lets you choose what goes into your build, because the FM-1's flash is small.

- It is **unofficial**. It is not made, endorsed or supported by M-VAVE, and it is not affiliated with SLOOP, Felucca, Melodee or X0X. M-VAVE and FM-1 are trademarks of their owners.
- It is **not** a finished product: version 0.1 (file `VERSION`), tested in the emulator and on the host. **It has not yet been run on a real FM-1.** Treat the first install as an experiment (see [Installing](#installing-on-an-fm-1)).
- It is a **build-it-yourself** project: there is no hosted installer yet, and the repository does not hold the toolchain or the SDK files (`make setup` fetches them).

## Features

Briefly; the manual [OPTIMIST.md](OPTIMIST.md) is the reference. What a build contains depends on its profile (see [Building](#building)).

- **Playing:** every function button is a layer: hold it and the 16 white keys and four knobs change job. Layers: FX (16 punch-in effects), EDIT (erase while playing, shift, double or halve, undo / redo), ARP (note repeat), SEQ (step entry with level and ratchet), SCL (key and one-key chords), GLO (mute, solo, tap tempo), SAVE (song sections).
- **Synth engines:** ANALOG 2, DIGITAL (4-op FM), PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, FM6 (6-operator, DX7 voices, from Melodee), and, as builder options, SLICE (a break slicer), PHYS (physical models, from Felucca) and ACID (TB-303-style bass, from X0X). Presets are browsed by kind; three user sample slots.
- **Drums:** 16 sounds per kit, ghost and hard hits, ratchets, a sampled acoustic kit and synthesised style kits (808, 909, techno, trap, ...), your own kits.
- **Effects and master:** chorus, delay, reverb, drive, slicer, plus DUST, DUCK and a DJ filter.
- **Sequencer:** per-track length and division, swing, per-step chance, motion recording (knob moves per step), SLOOP 2.4's micro timing, fills and parameter locks, sections A to P with a song chain, **per-track patterns and scenes**, and **snapshots** (whole-state slots). Designs: [docs/PATTERNS-DESIGN.md](docs/PATTERNS-DESIGN.md), [docs/SNAPSHOTS.md](docs/SNAPSHOTS.md).
- **MIDI and USB:** USB and TRS MIDI in, MIDI clock, per-track channels, MIDI out; USB audio (experimental).
- **SLOOP 2.4 compatibility:** the editor can export the working project for SLOOP 2.4, and a 2.4 autosave can be offered for import (PROJECT > A24). See [Your data](#your-data-and-sloop-24).
- **Web editor** (Chrome or Edge): every parameter, the drum grid, mixer, preset library, sample upload and backup.

<p align="center"><img src="assets/screens/editor-drums.png" alt="Optimist web editor: the drum track" width="760"></p>

## Requirements

| | |
| --- | --- |
| Hardware | An M-VAVE FM-1 and a USB data cable (no hub). |
| Computer | macOS, Linux or Windows. Python 3.9 or newer. |
| Toolchain | The JieLi toolchain runs natively on Linux x86-64; on macOS, Linux arm64 and Windows `make setup` uses a Docker image (or WSL on Windows). Docker Desktop or Rancher Desktop must be running. |
| Emulator (optional) | git and Rust (`cargo`); on Linux also GUI and audio libraries. |
| Web editor and installer | Chrome or Edge (Web MIDI). |

Verified so far (BUILDING.md): macOS arm64 with Rancher Desktop, and Linux x86-64 in CI. **Windows and Linux arm64 have not been run.**

## Building

```
git clone https://github.com/w0ts/optimist.git
cd optimist
make setup      # check the prerequisites; fetch the SDK files, the toolchain or its image, the builder's venv
make builder    # the firmware builder: pick features, see the flash and RAM budget, build
make build      # build the default profile without the menu: build/optimist-<version>-dev-<commit>.fwsc
```

Without `make` (and on Windows): `python tools/optimist.py setup | builder | build | package | emu | test` (`--help` lists all). `make flash` installs the last build on a connected FM-1 over USB-MIDI (it shows both identities and asks first; needs `make setup` once for mido and python-rtmidi; also `f` in the builder menu). `make package PROFILE=drum-machine` writes the `.fwsc` and its `-ui.zip` into `firmwares/`. The profiles are in `config/profiles/`. Details: [BUILDING.md](BUILDING.md); the builder, its budget and the profiles: [docs/BUILDER.md](docs/BUILDER.md); where flash and RAM go: [docs/MEMORY-MAP.md](docs/MEMORY-MAP.md).

The JieLi toolchain and three SDK files are downloaded on your machine from JieLi's servers and checked by SHA-256; they are not redistributed here (BUILDING.md, "The toolchain's licence").

## Installing on an FM-1

Installing custom firmware is at your own risk. **Back up first** (below).

1. Connect the FM-1 by USB and install the `.fwsc`:
   - command line: `pip install mido python-rtmidi`, then `python tools/fm1_install.py path/to/optimist-<version>.fwsc` (`--info` shows the connected FM-1);
   - or the web installer: the hosted one (see [Releases and the hosted site](#releases-and-the-hosted-site)), or a local copy of the site served on `localhost` (Web MIDI needs a secure context): `python3 web/make_site.py build/felucca.fwsc dev build/optimist-site`, then `python3 -m http.server 8766 --bind 127.0.0.1 --directory build/optimist-site` and open `http://localhost:8766/webapp/installer/` (BUILDING.md, "Installing").
2. Wait for "Done". If the install is cut off, the FM-1 stays in update mode: install again and it finishes.

**Going back and recovery** (OPTIMIST.md, "Rescue, going back, credits"):

- M-VAVE's own updater and the official FM-1 firmware always work as the way back.
- Hold **OCT-** while switching on for the USB rescue mode, then install again. After two start-ups that crash, the firmware enters it by itself.
- Last resort, macOS, no extra hardware: `tools/fm1_rescue.py` (from X0X) writes M-VAVE's stock V15 through the chip's own update mode. It saves the whole flash first, checks the chip, and never writes the bootloader or the data area. Run it without `--write` first. It needs `sudo` and `pyusb`; read its header before use. Its protocol was verified on hardware by FM-1-transporter; this repository does not record a run of it on an FM-1 with Optimist.
- [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) by kurogedelic does the same from its own hardware.

The **web editor** is on the same site (`/webapp/editor/`). Protocol: [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md).

## Your data and SLOOP 2.4

The FM-1 keeps your projects, sections, autosave, user presets, user kits, samples and snapshots in the flash area **above** the firmware (0x097000 to the end); an install writes only the firmware area. The map is in [docs/MEMORY-MAP.md](docs/MEMORY-MAP.md).

- **SLOOP 2.4 data is kept.** With `SL24_SAFE` (on by default), Optimist started on an FM-1 that ran SLOOP 2.4 does not erase what it cannot read: 2.4's projects, autosave, FM6 bank and long samples stay in flash (the PROJECT page lists them as "SLOOP 2.4"; they are not loaded). Before this guard the first start erased all four 2.4 project slots. A simulated-flash test covers it (`tests/sl24_safety_test.c`). OPTIMIST.md, "Coming from SLOOP 2.4". Not verified: this has not been tried on a real FM-1.
- **Other firmware's data** (SLOOP 2.0 to 2.3, Felucca, X0X) is not covered by that guard. Back up before switching.
- **Backup:** the web editor's Backup (Projects screen) writes one `.optimist-backup` file with the projects, working project, presets, kits, settings and FM6 bank, and optionally the samples. Restore checks each object. The installer asks "back up first?".

## Releases and the hosted site

A `v*` tag runs `.github/workflows/release.yml`: it builds every profile in `config/profiles/` and drafts a GitHub release with one `optimist-<version>-<profile>.fwsc` and its `-ui.zip` per flavour, plus `SHA256SUMS.txt`. `.github/workflows/pages.yml` publishes the web installer and editor as a static GitHub Pages site (one firmware inside it: the `user-default` build; install another flavour from a local site made from its package). **Neither workflow has run yet**, and Pages needs to be enabled (Settings > Pages > GitHub Actions) on a public repository or a plan that allows it. Until a release exists, build your own.

## Emulator

`make emu` (or `python tools/optimist.py emu`) runs a firmware from `build/` or `firmwares/` in the FM-1 emulator by Simon Johansson ([simonjohansson/fm1-emulator](https://github.com/simonjohansson/fm1-emulator)); our fork with extra ports is [w0ts/fm1-emulator](https://github.com/w0ts/fm1-emulator), the default (branch `feat/upstream-merge`; `EMU_REPO` and `EMU_BRANCH` choose another, for example upstream's `main`). The first run clones and builds it with Rust. Its saved flash is kept between runs (`FRESH=1` starts clean). Nothing measured in the emulator is a measurement of a real FM-1.

## Documentation

| | |
| --- | --- |
| [OPTIMIST.md](OPTIMIST.md) | the manual |
| [BUILDING.md](BUILDING.md) | building, the toolchain, the host tests, packages |
| [docs/BUILDER.md](docs/BUILDER.md) | the firmware builder and its profiles |
| [docs/MEMORY-MAP.md](docs/MEMORY-MAP.md), [docs/MEMORY-BUDGET.md](docs/MEMORY-BUDGET.md) | flash and RAM |
| [docs/PATTERNS-DESIGN.md](docs/PATTERNS-DESIGN.md), [docs/SNAPSHOTS.md](docs/SNAPSHOTS.md) | patterns, scenes, snapshots |
| [docs/NAMING.md](docs/NAMING.md) | the name, and what kept its old one (`FELUCCA_*` build flags, `felucca.fwsc`) |
| [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md) | the editor's SysEx protocol |

`make test` runs the host tests (audio renders, sequencer timing, UI, storage, update loader, web pages).

## Licence

Code: **GPL-3.0-only** ([LICENSE](LICENSE)). Assets have their own terms (samples CC0, Terminus font SIL OFL 1.1, Fukiai icons MIT): [LICENSING.md](LICENSING.md) and `LICENSES/`. No warranty. If you distribute a build, you must give its source under the same licence. A build's `build/ATTRIBUTION.txt` lists the CC0 samples it carries.

## Credits

Optimist stands on other people's work, each ported part marked in the source and listed in [LICENSING.md](LICENSING.md) and the builder's registry (`tools/builder/registry.py`, `tools/backports.json`):

- **[SLOOP](https://github.com/isod89/sloop-fm1)** by isod89 and contributors: the base of this firmware, including the 2.3 and 2.4 features.
- **[Felucca](https://github.com/hugelton/Felucca)** by Leo Kuroshita (@kurogedelic), Hugelton Instruments: engines, sequencer, editor and installer; motion recording, PHYS and others from Felucca 1.0.
- **[Melodee](https://github.com/keremimo/melodee)** by Kerem Kilic (Ellic Studio) and contributors: FM6, USB audio, TRS MIDI input, clock follow.
- **[X0X](https://github.com/charlesvestal/fm1-x0x)** by Charles Vestal: the rescue tool, knob and display work.
- Others: renebohne's SLOOP fork, Erick Buendia Barrientos (PR #45 of sloop-fm1), CrispyZebra (PHASE), klattsch (VOICE), Open303, DaisySP and Rings, Versilian Studios VSCO-2 CE, VCSL and Sonic Pi samples.
- **[fm1-emulator](https://github.com/simonjohansson/fm1-emulator)** by Simon Johansson.
- Interface ideas after teenage engineering, Elektron and Akai; none of them is affiliated with this project. The full list is in OPTIMIST.md, "Rescue, going back, credits".

## Contributing and reporting issues

Issues and pull requests go to [github.com/w0ts/optimist](https://github.com/w0ts/optimist). A useful report names the build (`build/optimist-<version>-...fwsc`, its profile or `.config`, and the commit in the editor's `SOURCE.txt`), says whether it happened in the emulator or on a device, and how to reproduce it. Run `make test` before sending a change; keep new ports marked with their source and licence (LICENSING.md). There is no CONTRIBUTING file yet.

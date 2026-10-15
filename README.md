<h1 align="center">Optimist</h1>

<p align="center"><b>An open, modular firmware platform for the M-VAVE FM-1 groovebox: build your own firmware, with the features and the interface you choose, on any computer.</b><br>
Free software (GPL-3.0-only), derived from <a href="https://github.com/isod89/sloop-fm1">SLOOP</a>, with parts of Felucca / Melodee, X0X and other FM-1 firmware projects.</p>

> [!CAUTION]
> **WORK IN PROGRESS. NO GUARANTEE. USE AT YOUR OWN RISK.**
>
> - **Hardware testing is very limited.** Since 2026-10-09 Optimist runs on one real FM-1 (the developer's): it installs, starts, plays, and BLE MIDI connects. Most features have only been tested in the emulator and on the host, and the recovery paths (the USB rescue screen, the OCT− + OCT+ update-mode combo, the rescue tool, the return to the official firmware) have **not** been tried on a device yet.
> - It is unfinished experimental software, and it changes often. Expect bugs and missing features. Expect formats to change between versions.
> - Installing it on your FM-1 could fail, stop the device from starting, or erase your projects, presets and samples. **Back up everything before you try it**, and read [Installing](#installing-on-an-fm-1) and the recovery steps first.
> - It comes with **no warranty and no support**, as the GPL-3.0 licence says (sections 15 and 16): you use it at your own risk, and nobody is liable for any damage to your device or your data.

## Why Optimist

**1. A modular firmware: you build your own.** The FM-1's flash is small, and no single firmware can hold every engine, kit and effect. Optimist splits them into parts, and the **firmware builder** (`make builder`) lets you choose synth engines, drum kits, effects, sequencer features, MIDI and USB options and the user interface. It shows the flash and RAM budget as you choose, then builds the firmware. A part you leave out costs no flash, RAM or CPU. Ready-made profiles are in `config/profiles/`. If a project needs a part your build doesn't have, the device warns you when it loads. See [docs/BUILDER.md](docs/BUILDER.md).

**2. More than one user interface.** The interface is a build-time choice. The default is SLOOP's (hold a function button, touch a key). Optimist's own interface (`UI=1`, **experimental**; it runs on the developer's FM-1) has one uniform navigation (SELECT moves the cursor, ALGORITHM picks the track, PRESETS sets the value, SAVE = yes, HOME = no), the 16 keys as steps, and Felucca's look: [docs/guide/interface.md](docs/guide/interface.md).

**3. A platform for everybody.** Free software (GPL-3.0); one command to build (`make setup`, then `make builder`) on macOS, Linux and Windows (Windows is not verified yet); an emulator to try a build without a device; a web editor for every parameter; and a home for work from the whole FM-1 community, each ported part credited and licensed (see [Credits](#credits)). New engines, effects and interfaces are welcome as builder options; see [Contributing](#contributing-and-reporting-issues).

## What it is, and what it is not

Optimist turns the FM-1 into a four-track groovebox that you play live: three synth tracks and a drum track with 16 sounds on the white keys. It started as SLOOP-plus, a fork of SLOOP, and keeps SLOOP's way of playing.

- It is **unofficial**. It is not made, endorsed or supported by M-VAVE, and it is not affiliated with SLOOP, Felucca, Melodee or X0X. M-VAVE and FM-1 are trademarks of their owners.
- It is **not** a finished product: version 0.1 (file `VERSION`), tested in the emulator and on the host, and running on one real FM-1 since 2026-10-09 (see the caution above for what that covers). Treat the first install as an experiment (see [Installing](#installing-on-an-fm-1)).
- It is a **build-it-yourself** project: there is no hosted installer yet, and the repository does not hold the toolchain or the SDK files (`make setup` fetches them).
- It has **no Bluetooth audio out**, on purpose. Most wireless headphones (Sony WH-1000XM4, AirPods and most others) play audio over Classic Bluetooth (A2DP), not BLE, and Optimist's own Bluetooth stack is BLE only, for MIDI. A2DP in the firmware would mean either the vendor's closed stack (about 165 KB of flash and 40 KB of RAM, which do not fit, and closed code cannot ship under the GPL) or our own Classic driver (many months of work). The LE Audio and Auracast route reaches only recent headphones. Either way the delay would be 100–300 ms with A2DP, too much to play live. For wireless listening, plug a cheap Bluetooth transmitter into the headphone out; it needs no firmware. Before buying, check that the product page names your exact headphones: several cheap transmitters do not pair with AirPods Pro 2 or AirPods 4. For lower latency, use a transmitter and headphones that both support aptX Low Latency (about 40 ms). Details: [docs/BT-AUDIO-FEASIBILITY.md](docs/BT-AUDIO-FEASIBILITY.md).

## Features

Briefly; the manual [OPTIMIST.md](OPTIMIST.md) is the reference and the user guides in [docs/guide/](docs/guide/) explain each area. Every engine, effect and option below is a builder item.

- **Playing:** every function button is a layer: hold it and the 16 white keys and four knobs change job (FX punch-ins, EDIT, ARP, SEQ step entry, SCL scales and chords, GLO mute / solo / tap tempo, SAVE song sections).
- **Synth engines:** ANALOG 2, DIGITAL, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, FM6, and, as builder options, SLICE, PHYS, ACID, CZ, NOISE and JUNO (a Juno-106 voice, after fm-106). Two LFOs and two envelopes per sound, each with two free destinations. [docs/guide/sound.md](docs/guide/sound.md).
- **Drums:** 16 sounds per kit, ghost and hard hits, ratchets, a sampled acoustic kit, synthesised style kits (808, 909, techno, trap, ...) and your own kits.
- **Effects:** send buses (reverb, delay, chorus) with three returns, two insert slots on every track, the drum bus, the master and every drum sound, and the computer's audio as a mixer track (USB IN). Inserts include DYN (compressor and limiter), EQ3, filters, folder, fuzz, phaser, flanger, gate, chorus, echo and a Juno chorus. [docs/guide/effects.md](docs/guide/effects.md).
- **Sequencer:** per-track length and division, swing, chance and conditions, motion recording, parameter locks, EUCLID, fills, sections A to P with a song chain, per-track patterns and scenes, and snapshots. [docs/guide/sequencer.md](docs/guide/sequencer.md).
- **MIDI and USB:** USB and TRS MIDI in, MIDI clock in and out, per-track channels, MIDI out; USB audio (experimental); BLE MIDI (our own stack, experimental).
- **Performance:** a predictive CPU guard eases the load instead of dropping voices ([docs/CPU-GUARD.md](docs/CPU-GUARD.md)); the second core is experimental and a DUAL build keeps it parked by default ([docs/DUAL-CORE.md](docs/DUAL-CORE.md)). SYSTEM > INFO, INTERNALS and USB STATS show the load, drop-outs and counters on the device.
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

Without `make` (and on Windows): `python tools/optimist.py setup | builder | build | package | emu | test | web` (`--help` lists all). `make flash` installs the last build on a connected FM-1 over USB-MIDI (it shows both identities and asks first; needs `make setup` once for mido and python-rtmidi; also `f` in the builder menu). `make package PROFILE=drum-machine` writes the `.fwsc` and its `-ui.zip` into `firmwares/`. Details: [BUILDING.md](BUILDING.md); the builder and the profiles: [docs/BUILDER.md](docs/BUILDER.md).

The JieLi toolchain and three SDK files are downloaded on your machine from JieLi's servers and checked by SHA-256; they are not redistributed here (BUILDING.md, "The toolchain's licence").

## Installing on an FM-1

Installing custom firmware is at your own risk. **Back up first** (below).

1. Connect the FM-1 by USB and install the `.fwsc`:
   - command line: `pip install mido python-rtmidi`, then `python tools/fm1_install.py path/to/optimist-<version>.fwsc` (`--info` shows the connected FM-1);
   - or the web installer: the hosted one (see [Releases and the hosted site](#releases-and-the-hosted-site)), or a local copy: after `make build`, `make web` builds the site from the last build, serves it on `127.0.0.1` and opens the editor; the installer is at `http://localhost:8766/webapp/installer/` (Web MIDI needs a secure context; BUILDING.md, "Installing").
2. Wait for "Done". If the install is cut off, the FM-1 stays in update mode: install again and it finishes.

**Going back and recovery** (OPTIMIST.md, "Rescue, going back, credits"):

- M-VAVE's own updater and the official FM-1 firmware always work as the way back.
- Hold **OCT-** while switching on for the USB rescue mode, then install again. After two start-ups that crash, the firmware enters it by itself.
- Last resort, macOS, no extra hardware: `tools/fm1_rescue.py` (from X0X) writes M-VAVE's stock V15 through the chip's own update mode. It saves the whole flash first, checks the chip, and never writes the bootloader or the data area. Run it without `--write` first. It needs `sudo` and `pyusb`; read its header before use. Its protocol was verified on hardware by FM-1-transporter; this repository does not record a run of it on an FM-1 with Optimist.
- [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) by kurogedelic does the same from its own hardware.

## Your data and SLOOP 2.4

The FM-1 keeps your projects, sections, autosave, user presets, user kits, samples and snapshots in the flash area **above** the firmware; an install writes only the firmware area. The map is in [docs/MEMORY-MAP.md](docs/MEMORY-MAP.md).

- **SLOOP 2.4 data is kept.** With `SL24_SAFE` (on by default), Optimist started on an FM-1 that ran SLOOP 2.4 does not erase what it cannot read: 2.4's projects, autosave, FM6 bank and long samples stay in flash (the PROJECT page lists them as "SLOOP 2.4"; they are not loaded). A simulated-flash test covers it. OPTIMIST.md, "Coming from SLOOP 2.4". Not verified: this has not been tried on a real FM-1.
- **Other firmware's data** (SLOOP 2.0 to 2.3, Felucca, X0X) is not covered by that guard. Back up before switching.
- **Formats are Optimist's own.** Optimist owes no compatibility to other firmwares' files; the SLOOP 2.4 import and export stay as they are. Its own older files are converted on load where a converter exists. Expect formats to change between versions.
- **Backup:** the web editor's Backup (Projects screen) writes one `.optimist-backup` file with the projects, working project, presets, kits, settings and FM6 bank, and optionally the samples. Restore checks each object. The installer asks "back up first?".

## Releases and the hosted site

A `v*` tag runs `.github/workflows/release.yml`: it builds every profile in `config/profiles/` and drafts a GitHub release with one `optimist-<version>-<profile>.fwsc` and its `-ui.zip` per flavour, plus `SHA256SUMS.txt`. `.github/workflows/pages.yml` publishes the web installer and editor as a static GitHub Pages site ([docs/PAGES.md](docs/PAGES.md)). **Neither workflow has run yet**, and Pages needs to be enabled (Settings > Pages > GitHub Actions) on a public repository or a plan that allows it. Until a release exists, build your own.

## Emulator

`make emu` (or `python tools/optimist.py emu`) runs a firmware from `build/` or `firmwares/` in the FM-1 emulator by Simon Johansson ([simonjohansson/fm1-emulator](https://github.com/simonjohansson/fm1-emulator)); our fork with extra ports is [w0ts/fm1-emulator](https://github.com/w0ts/fm1-emulator), the default (`EMU_REPO` and `EMU_BRANCH` choose another). The first run clones and builds it with Rust. Nothing measured in the emulator is a measurement of a real FM-1.

## Documentation

| | |
| --- | --- |
| [OPTIMIST.md](OPTIMIST.md) | the manual |
| [docs/guide/](docs/guide/) | user guides: [interface](docs/guide/interface.md), [sound](docs/guide/sound.md), [effects](docs/guide/effects.md), [sequencer](docs/guide/sequencer.md) |
| [BUILDING.md](BUILDING.md), [docs/BUILDER.md](docs/BUILDER.md) | building, the toolchain, packages; the firmware builder and its profiles |
| [docs/MEMORY-MAP.md](docs/MEMORY-MAP.md), [docs/MEMORY-BUDGET.md](docs/MEMORY-BUDGET.md) | where flash and RAM go, and what was saved |
| [docs/UI-OPTIMIST-DESIGN.md](docs/UI-OPTIMIST-DESIGN.md) | the Optimist UI: decisions, screens, gestures |
| [docs/DSP-SHARED.md](docs/DSP-SHARED.md), [docs/ENGINE-PLUGINS.md](docs/ENGINE-PLUGINS.md), [docs/DUAL-CORE.md](docs/DUAL-CORE.md), [docs/CPU-GUARD.md](docs/CPU-GUARD.md) | shared DSP, engines as plug-ins, the second core, the CPU guard |
| [docs/PATTERNS-DESIGN.md](docs/PATTERNS-DESIGN.md), [docs/SNAPSHOTS.md](docs/SNAPSHOTS.md) | patterns, scenes, snapshots |
| [docs/DOC-COVERAGE.md](docs/DOC-COVERAGE.md) | what the product has against what the documents say |
| [docs/NAMING.md](docs/NAMING.md), [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md) | the name and what kept its old one; the editor's SysEx protocol |

`make test` runs the host tests (audio renders, sequencer timing, UI, storage, update loader, web pages).

## Licence

Code: **GPL-3.0-only** ([LICENSE](LICENSE)). Assets have their own terms (samples CC0, Terminus font SIL OFL 1.1, Fukiai icons MIT): [LICENSING.md](LICENSING.md) and `LICENSES/`. No warranty. If you distribute a build, you must give its source under the same licence. A build's `build/ATTRIBUTION.txt` lists the CC0 samples it carries.

## Credits

Optimist stands on other people's work. Each ported part is marked in its source files, keeps its authors' notice,
and is listed with its licence and commit in [LICENSING.md](LICENSING.md) (the builder's registry,
`tools/builder/registry.py` and `tools/backports.json`, names the source of each optional part; the audit behind the
list: [docs/CREDITS-AUDIT.md](docs/CREDITS-AUDIT.md)).

- **[SLOOP](https://github.com/isod89/sloop-fm1)** by isod89 and contributors: the base of this firmware, including the 2.3 and 2.4 features; Erick Buendia Barrientos (Erbubar23, sloop-fm1 PR #45); [renebohne's SLOOP fork](https://github.com/renebohne/sloop-fm1) (played notes on the keys).
- **[Felucca](https://github.com/hugelton/Felucca)** by Leo Kuroshita (@kurogedelic), Hügelton Instruments: the platform, engines, sequencer, editor and installer that SLOOP is built on; from Felucca 1.0 to 1.6: motion recording, PHYS, the spring reverb, BASS+, step conditions, the sequence tools, FOLD / FUZZ / PHSR / FLNG, favorites and more; [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (NOISE, MIT); [CrispyZebra](https://github.com/hugelton/CrispyZebra) (PHASE).
- **[Melodee](https://github.com/keremimo/melodee)** by Kerem Kilic (Ellic Studio) and contributors (ChanceTheMaker): FM6, CZ, USB audio, TRS MIDI input, clock follow, MIDI expression, the FRMT and WIDE inserts, the rescue tool's package checks, the second core's fault handling.
- **[X0X](https://github.com/charlesvestal/fm1-x0x)** by Charles Vestal: the rescue tool, knob, encoder and display work, the ACID engine (Open303 by Robin Schmidt, MIT; TB-3PO by Logarhythm, MIT), the X0X 909 / 808 kits (9W9, credited to athousanddetails and to Charles Vestal by different X0X files; 8W8 by athousanddetails; ER-99 by Matthew Cieplak), the mixer glides, the second core's start and guards.
- **[Flowstate](https://github.com/zakariachowdhury/flowstate-fm1)** by Zakaria Chowdhury: the performance macros, the CPU guard, the scaled large font.
- **[Jangada](https://github.com/zednaked/jangada)** by zednaked: the SUPER engine, the second core's supply levels and AUTO / OFF setting.
- **[fm-106](https://github.com/PatttF/fm-106)** by PatttF, a port of **[Ultramaster KR-106](https://github.com/kayrockscreenprinting/ultramaster_kr106)** by Kayrock Screenprinting: the JUNO engine and JUNO CHORUS.
- DSP and data from others, each with its licence in `LICENSES/`: DaisySP (Electrosmith, Emilie Gillet) and Rings (Emilie Gillet), MAME's uPD933 (Devin Acker, BSD-3-Clause), Airwindows (Chris Johnson), MSFA (Google) and Dexed (Pascal Gauthier), sc808 (Yoshinosuke Horiuchi, Sam Aaron); klattsch (Tony Gies) as a design reference.
- Samples: Versilian Studios VSCO-2 CE and VCSL, and the Sonic Pi sample set (all CC0). Fonts: Terminus (Dimitar Toshkov Zhekov, SIL OFL 1.1), Fukiai (Hügelton Instruments, MIT, web editor).
- **[fm1-emulator](https://github.com/simonjohansson/fm1-emulator)** by Simon Johansson; [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) by kurogedelic and [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) by kagaimiq (the rescue path).
- Interface ideas after teenage engineering, Elektron and Akai. Roland, Yamaha and Casio product names (Juno-106, TB-303, TR-808, TR-909, DX7, CZ) only say what an engine or kit models. None of these companies is affiliated with this project. The full list is in OPTIMIST.md, "Rescue, going back, credits".

## Contributing and reporting issues

Issues and pull requests go to [github.com/w0ts/optimist](https://github.com/w0ts/optimist). A useful report names the build (`build/optimist-<version>-...fwsc`, its profile or `.config`, and the commit in the editor's `SOURCE.txt`), says whether it happened in the emulator or on a device, and how to reproduce it. Run `make test` before sending a change; keep new ports marked with their source and licence (LICENSING.md). There is no CONTRIBUTING file yet.

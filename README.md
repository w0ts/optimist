<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo/optimist-logo-on-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="assets/logo/optimist-logo-on-light.svg">
    <img src="assets/logo/optimist-logo-on-light.svg" alt="Optimist: a dinghy" width="220">
  </picture>
</p>

<h1 align="center">Optimist</h1>

<p align="center"><b>A modular firmware for the M-VAVE FM-1: pick what goes into your FM-1, build it in the browser, try it in
the browser, install it from the browser.</b><br>
Free software (GPL-3.0-only), built on <a href="https://github.com/isod89/sloop-fm1">SLOOP</a>, Felucca, Melodee, X0X and other
FM-1 projects (see <a href="#credits">Credits</a>).</p>

> [!CAUTION]
> **Early access: work in progress, no warranty, use at your own risk.** Optimist runs on real FM-1s, but most features
> are tested in the emulator and on the host, and formats can change between versions. An install could fail or erase
> your data: **back up first** (below), and read [Getting back](#getting-back) before you start.

## Everything happens on one site: [w0ts.github.io/optimist](https://w0ts.github.io/optimist/)

Use **Chrome or Edge** (they have Web MIDI) and a USB data cable, straight into the computer.

| | |
| --- | --- |
| [**Build**](https://w0ts.github.io/optimist/webapp/editor/#build) | Choose the engines, drum kits, effects and features you want, with a live flash and RAM budget, and press *Build in the cloud*: it opens a GitHub issue with your configuration (a GitHub account is needed), and a few minutes later the answer there links your firmware. It is then in the installer's and the simulator's lists. |
| [**Install**](https://w0ts.github.io/optimist/webapp/editor/#install) | Install a release flavour, your cloud build or a `.fwsc` file on your FM-1. It offers a backup first, and can put the official firmware back. |
| [**Simulator**](https://w0ts.github.io/optimist/webapp/editor/#sim) | The firmware running in your browser, with the FM-1's panel and its keyboard shortcuts: try a build before you install it. It can copy your FM-1's memory in, and write it back. |
| [**Editor**](https://w0ts.github.io/optimist/webapp/editor/#mixer) | Every sound, drum, effect and setting as knobs, the drum grid, the samples (with CHOP), the preset library, and the **Backup** of your whole FM-1 in one file. |

The firmware files are also on the [releases page](https://github.com/w0ts/optimist/releases).

## The flavours

Four ready-made builds, in every release; the builder makes any other.

| Flavour | What it is |
| --- | --- |
| **Synth** (the default) | FM6 with five voice packs, ANALOG 2, JUNO, ACID, PHYS, the X0X 909 / 808 drums, Bluetooth MIDI, USB audio, the second core. The web editor does backup and restore only. |
| **drum machine** | Every kit, the drum editor, your samples and kits, SLICE, GRAIN, ANALOG 2 for bass. No web editor: back up with another flavour first. |
| **FM + VA studio** | ANALOG 2, FM6, DIGITAL, SAMPLE, TRIO, every kit. |
| **everything fits** | Every engine but TRIO, and almost every feature that fits. |

## Your first install

1. **Back up.** Open the [Editor](https://w0ts.github.io/optimist/webapp/editor/#mixer), *Connect*, then Projects >
   **Backup**: one `.optimist-backup` file with your projects, presets, kits and settings (tick *with the samples* for
   those too). Coming from another firmware, use its own backup as well.
2. Open [Install](https://w0ts.github.io/optimist/webapp/editor/#install): **Connect** says what your FM-1 runs,
   **Choose** a release and a flavour (or your cloud build, or a file), **Install** and wait for *Done*.
3. The FM-1 restarts into Optimist. If an install is cut off, the FM-1 stays in update mode: press Install again.

Your projects, presets, kits and samples live above the firmware: an install writes only the firmware area. Data that
Optimist cannot read may not be kept, except SLOOP's: SLOOP 2.7 data is kept (below).

**Coming from SLOOP 2.7:** Optimist erases nothing of SLOOP's. Its sections stay in flash and show as *SLOOP* on the
PROJECT page; LOAD twice imports one (SLOOP IMPORT takes its autosave), SAVE keeps the import. The editor can export the
working project as a SLOOP 2.7 song file to go the other way.

## Getting back

- **To the official firmware:** in [Install](https://w0ts.github.io/optimist/webapp/editor/#install), step 4, choose
  M-VAVE's FM-1 V15 file (`FM-1.fwsc` from [m-vave.com](https://www.m-vave.com/download), unchanged) and install it. Or
  use M-VAVE's own updater, M-UPGRADE. The official firmware does not read Optimist's data: back it up first.
- **The FM-1 does not start Optimist:** hold **OCT−** alone while switching on (*OPTIMIST USB RESCUE*), then install again.
  After two start-ups that crash, the FM-1 goes there by itself.
- **Nothing starts at all:** hold **OCT− + OCT+** while switching on and keep them held for **3 s**: the chip's own update mode
  (UBOOT; the screen stays dark). Then M-UPGRADE, or `tools/fm1_rescue.py` from this repository (macOS; from X0X: it
  backs up the whole flash first and writes only the firmware area), or
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## What is in it

- **Four tracks:** three synth tracks and a drum track with 10 lanes on the black keys, ghost and hard hits, ratchets.
- **Up to 17 synth engines:** ANALOG 2, FM6 (plays DX7 voices as Dexed does), JUNO, ACID, CZ, PHYS, DIGITAL, PHASE,
  LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, SLICE, NOISE, DRONE (drones and binaural beats). Two LFOs and two envelopes per sound.
- **Drums:** 37 kits (5 sampled, 32 synthesised), the X0X 909 and 808, every sound editable, your samples and kits.
- **Effects:** two insert slots on every track, drum sound and the master (DYN, EQ3, filters, drive, chorus, echo, the
  Juno chorus and more), reverb, delay and chorus sends with three returns, punch-in effects.
- **Sequencer:** 64 steps a track, parameter locks, motion recording, chance and conditions, note generators (EUCLID, RANDOM, PULSE), pattern tools, fills, patterns and
  scenes, sections A–P and a song chain, snapshots, undo.
- **Connections:** USB and TRS MIDI in, MIDI out, MIDI clock in and out, Bluetooth MIDI, USB audio (experimental).
- **One interface:** rows of values on the screen, SELECT / ALGORITHM / PRESETS, SAVE = yes, HOME = no, and held buttons
  for performance layers. PLAY's two lights show the transport: green dim stopped, green full playing, red full recording.

<p align="center">
  <img src="assets/screens/device-mixer.webp" alt="The FM-1's screen: the mixer with the tracks T1 to T3 and the drum track playing" width="260">
  <img src="assets/screens/device-drums.webp" alt="The FM-1's screen: the drum track's lanes with their steps" width="260">
  <img src="assets/screens/device-master.webp" alt="The FM-1's screen: the master page with its level meter and the three effect returns" width="260">
</p>

## The guide

| | |
| --- | --- |
| [The interface](docs/guide/interface.md) · [every shortcut](docs/guide/shortcuts.md) | the controls, pages, settings and read-outs |
| [Sound](docs/guide/sound.md) · [Drums](docs/guide/drums.md) | the engines, voices, modulation and presets · kits, drum sounds, your samples and kits |
| [Effects](docs/guide/effects.md) · [Sequencer](docs/guide/sequencer.md) | inserts, sends, returns, the master · steps, locks, motion, patterns, the song |
| [MIDI, Bluetooth, USB audio, the web editor](docs/guide/midi-usb.md) | connections and the editor |
| [The builder](docs/BUILDER.md) | every builder item, its cost, the profiles |

## Build it on your computer

For developers, or to build without the cloud: macOS, Linux or Windows, Python 3.9+, and Docker on macOS (the JieLi
toolchain runs natively on Linux x86-64).

```
git clone https://github.com/w0ts/optimist.git && cd optimist
make setup      # fetch the toolchain, the SDK files and the builder's Python packages
make builder    # the builder in the terminal: pick, see the budget, build
make web        # the same site on your machine (installer, editor, simulator)
make test       # the host tests
```

[BUILDING.md](BUILDING.md) has the details: profiles, the emulator (`make emu`), the simulator, flashing from the command
line (`make flash`), releases and the site. The toolchain and SDK files are downloaded from JieLi and checked by SHA-256;
they are not in this repository.

## Credits

Optimist stands on other people's work. Each ported part is marked in its source files, keeps its authors' notice, and is
listed with its licence and commit in [LICENSING.md](LICENSING.md).

- **[SLOOP](https://github.com/isod89/sloop-fm1)** by isod89 and contributors: the base of this firmware; Erick Buendia Barrientos (Erbubar23); [renebohne's SLOOP fork](https://github.com/renebohne/sloop-fm1).
- **[Felucca](https://github.com/hugelton/Felucca)** by Leo Kuroshita (@kurogedelic), Hügelton Instruments: the platform, engines, sequencer, editor and installer SLOOP is built on, and many later features; [Felucca-Engines](https://github.com/hugelton/Felucca-Engines); [CrispyZebra](https://github.com/hugelton/CrispyZebra).
- **[Melodee](https://github.com/keremimo/melodee)** by Kerem Kilic (Ellic Studio) and contributors: FM6, CZ, USB audio, TRS MIDI in, clock follow, MIDI expression, inserts, the rescue tool's checks, the second core's fault handling.
- **[X0X](https://github.com/charlesvestal/fm1-x0x)** by Charles Vestal: the rescue tool, the ACID engine (Open303, TB-3PO), the X0X 909 / 808 kits (9W9, 8W8 by athousanddetails, ER-99 by Matthew Cieplak), the mixer glides, the second core's start.
- **[Flowstate](https://github.com/zakariachowdhury/flowstate-fm1)** by Zakaria Chowdhury: the performance macros, the CPU guard. **[Jangada](https://github.com/zednaked/jangada)** by zednaked: the SUPER engine, the second core's supply settings. **[fm-106](https://github.com/PatttF/fm-106)** by PatttF, after [Ultramaster KR-106](https://github.com/kayrockscreenprinting/ultramaster_kr106): the JUNO engine and chorus.
- DaisySP, Rings, MAME's uPD933, Airwindows, MSFA and Dexed, sc808; samples from Versilian Studios and Sonic Pi (CC0); fonts Terminus and JetBrains Mono (SIL OFL), Fukiai (MIT); [fm1-emulator](https://github.com/simonjohansson/fm1-emulator) by Simon Johansson; [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) by kagaimiq.

Unofficial: not made, endorsed or supported by M-VAVE. M-VAVE and FM-1 are trademarks of their owners. Product names
(Juno-106, TB-303, TR-808, TR-909, DX7, CZ) only say what an engine or kit models.

## Licence and contributing

Code: **GPL-3.0-only** ([LICENSE](LICENSE)); assets have their own terms ([LICENSING.md](LICENSING.md)). No warranty. If
you distribute a build, give its source under the same licence.

Issues and pull requests: [github.com/w0ts/optimist](https://github.com/w0ts/optimist). A useful report names the build
(the flavour and release, or the cloud build's id), says whether it happened in the simulator, the emulator or on an
FM-1, and how to reproduce it. Run `make test` before sending a change, and keep each ported part marked with its source
and licence.

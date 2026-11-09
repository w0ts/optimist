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


## What is in it

- **Four tracks:** three synth, drum with 10 lanes.
- **Many synth engines**
- Two LFOs and two envelopes per sound.
- **Drums:** sampled and synthesised kits, X0X 909 and 808.
- **Effects:** two insert slots on every track, three returns, punch-in effects.
- **Sequencer:** 64 steps a track, parameter locks, motion recording, chance and conditions, note generators, fills, patterns and scenes, undo.
- **Connections:**
  - **[JANK mode]([jank](https://github.com/w0ts/jank))**: Jam syned over Wi-Fi/Ableton Link with no router! 
  - **Ableton Link**, MIDI clock in/out
  - MIDI in: USB, TRS, Bluetooth
  - MIDI out: USB and Bluetooth
  - Bluetooth Audio
- **SD card mode:** projects, presets, kits and samples on a microSD card, samples streamed from it [the SD board](hardware/README.md)


<p align="center">
  <img src="assets/screens/device-mixer.webp" alt="The FM-1's screen: the mixer with the tracks T1 to T3 and the drum track playing" width="260">
  <img src="assets/screens/device-drums.webp" alt="The FM-1's screen: the drum track's lanes with their steps" width="260">
  <img src="assets/screens/device-master.webp" alt="The FM-1's screen: the master page with its level meter and the three effect returns" width="260">
</p>

## The flavours

Optimist is meant to be build using our web installer and customised to your needs. but we provide a few stock builds to try it out.

| Flavour | What it is |
| --- | --- |
| **mots-config** (the default) | My build|
| **synths** | Most of the synths |
| **synths-2** | What did not fit in **synths** build |
| **drum machine** | everything about drums, some synths. |

## Builder, Installer, editor [w0ts.github.io/optimist](https://w0ts.github.io/optimist/)

Use **Chrome or Edge** (they have Web MIDI) and a USB data cable, straight into the computer.

| | |
| --- | --- |
| [**Firmware**](https://w0ts.github.io/optimist/webapp/editor/#install) | Install a release flavour, your own build or a `.fwsc` file on your FM-1. It offers a backup first, and can put the official firmware back. **Build your own…** opens the builder: start from a flavour or a configuration you saved, choose the engines, drum kits, effects and features with a live flash and RAM budget, and press *Build* (no GitHub account needed). The page follows the build, then offers *Flash it* and *Try it in the simulator*. |
| [**Simulator**](https://w0ts.github.io/optimist/webapp/editor/#sim) | The firmware running in your browser, with the FM-1's panel and its keyboard shortcuts: try a build before you install it. It can copy your FM-1's memory in, and write it back. |
| [**Editor**](https://w0ts.github.io/optimist/webapp/editor/#mixer) | The **Backup** of your whole FM-1 in one file, the projects and presets librarian, and the FM-1's screen mirrored. A build with live editing (`ED_LIVE`, not in the four flavours) adds every sound, drum, effect and setting as knobs, the drum grid and the samples (with CHOP). |

The firmware files are also on the [releases page](https://github.com/w0ts/optimist/releases).

**Optimist Mirror**, the app: the FM-1's screen and panel on your iPhone, iPad or Mac, over USB or Bluetooth, and firmware installs.

<a href="https://apps.apple.com/app/id6826362906"><img src="web/style/badges/app-store.svg" alt="Download on the App Store" height="40"></a>
<a href="https://apps.apple.com/app/id6826362906"><img src="web/style/badges/mac-app-store.svg" alt="Download on the Mac App Store" height="40"></a>

## Your first install

1. **Back up.** Open the [Editor](https://w0ts.github.io/optimist/webapp/editor/#backup), *Connect*, then
   **Backup**: one `.optimist-backup` file with your projects, presets, kits and settings (tick *with the samples* for
   those too). Coming from another firmware, use its own backup as well.
2. Open [Install](https://w0ts.github.io/optimist/webapp/editor/#install): **Connect** says what your FM-1 runs,
   **Choose** a release and a flavour (or your cloud build, or a file), **Install** and wait for *Done*.
3. The FM-1 restarts into Optimist. If an install is cut off, the FM-1 stays in update mode: press Install again.

Your projects, presets, kits and samples live above the firmware: an install writes only the firmware area. Data that
Optimist cannot read may not be kept, except SLOOP's: SLOOP 2.7 data is kept.

## The guide

| | |
| --- | --- |
| [The interface](docs/guide/interface.md) · [every shortcut](docs/guide/shortcuts.md) | the controls, pages, settings and read-outs |
| [Sound](docs/guide/sound.md) · [Drums](docs/guide/drums.md) | the engines, voices, modulation and presets · kits, drum sounds, your samples and kits |
| [Effects](docs/guide/effects.md) · [Sequencer](docs/guide/sequencer.md) | inserts, sends, returns, the master · steps, locks, motion, patterns, the song |
| [MIDI, Bluetooth, USB audio, the web editor](docs/guide/midi-usb.md) | connections and the editor |
| [Getting back](docs/guide/getting-back.md) | to the official firmware, and what to do when the FM-1 does not start |
| [The builder](docs/BUILDER.md) | every builder item, its cost, the profiles |

## Build it on your computer

For developers, or to build without the cloud: macOS arm64 or Linux x86-64, Python 3.11+ (the compiler, pi32v2-llvm,
runs natively on both; other hosts build it first: BUILDING.md).

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
- **[Melodee](https://github.com/keremimo/melodee)** by Kerem Kilic (Ellic Studio) and contributors: FM6, CZ, the PROPHET voice, USB audio, TRS MIDI in, clock follow, MIDI expression, inserts, the rescue tool's checks, the second core's fault handling.
- **[Clipper](https://github.com/isod89/clipper-fm1)** by 3dSam: the STRINGS, VPS, TERRAIN, WSEQ, VECTOR and WAVE engines.
- **[Mini-Tone](https://github.com/eldontyrellcorp/Mini-Tone)** by Stephane Schmitz: the SID engine (after reSID 0.16 by Dag Lem).
- **[CS-1](https://github.com/twochannels/CS-1)** by twochannels: the CS80 engine's voice design.
- **[X0X](https://github.com/charlesvestal/fm1-x0x)** by Charles Vestal: the rescue tool, the ACID engine (Open303, TB-3PO), the X0X 909 / 808 kits (9W9, 8W8 by athousanddetails, ER-99 by Matthew Cieplak), the mixer glides, the second core's start.
- **[Flowstate](https://github.com/zakariachowdhury/flowstate-fm1)** by Zakaria Chowdhury: the performance macros, the CPU guard. **[Jangada](https://github.com/zednaked/jangada)** by zednaked: the SUPER engine, the second core's supply settings. **[Caravel](https://github.com/bonszaimusic/caravel)** by bonszaimusic: the DRONE engine's coupled network. **[fm-106](https://github.com/PatttF/fm-106)** by PatttF, after [Ultramaster KR-106](https://github.com/kayrockscreenprinting/ultramaster_kr106): the JUNO engine and chorus.
- DaisySP, Rings, Plaits, MAME's uPD933, Airwindows, MSFA and Dexed, sc808; samples from Versilian Studios and Sonic Pi (CC0); fonts Terminus and JetBrains Mono (SIL OFL), Fukiai (MIT); [fm1-emulator](https://github.com/simonjohansson/fm1-emulator) by Simon Johansson; [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) by kagaimiq; [M-Flasher](https://github.com/jvitkauskas/m-flasher) and [m-bootloader](https://github.com/jvitkauskas/m-bootloader) by Julius Vitkauskas: download-mode installs and the open bootloader ([docs/INSTALL-PATHS.md](docs/INSTALL-PATHS.md)).

Unofficial: not made, endorsed or supported by M-VAVE. M-VAVE and FM-1 are trademarks of their owners. Product names
(Juno-106, TB-303, TR-808, TR-909, DX7, CZ, Prophet-5, Prophet VS, Wavestation, SID) only say what an engine or kit models.

## Licence and contributing

Code: **GPL-3.0-only** ([LICENSE](LICENSE)), the Bluetooth stack and the Wi-Fi code GPL-3.0-only OR MPL-2.0 (the Ableton Link port stays GPL-3.0-only); assets have their own terms ([LICENSING.md](LICENSING.md)). No warranty. If
you distribute a build, give its source under the same licence.

To add an engine, an effect, a note generator or a UI change, read [CONTRIBUTING.md](CONTRIBUTING.md): where the code
lives, how a piece becomes a builder item, three worked examples in the tree, and how to open a pull request.

Issues and pull requests: [github.com/w0ts/optimist](https://github.com/w0ts/optimist). A useful report names the build
(the flavour and release, or the cloud build's id), says whether it happened in the simulator, the emulator or on an
FM-1, and how to reproduce it. Run `make test` before sending a change, and keep each ported part marked with its source
and licence.

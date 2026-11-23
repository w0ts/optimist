# Optimist

> ## [→ w0ts.github.io/optimist](https://w0ts.github.io/optimist/)
> **What Optimist is, the flavours, the builder, the installer, the simulator, the editor and the app: on the site.**
> This README is for developers.

A modular firmware for the M-VAVE FM-1, built in the browser from the parts you pick. Free software (GPL-3.0-only),
built on [SLOOP](https://github.com/isod89/sloop-fm1), Felucca, Melodee, X0X and other FM-1 projects (see
[Credits](#credits)).

The user guide is in [docs/guide/](docs/guide/) and the builder reference in [docs/BUILDER.md](docs/BUILDER.md); the
site links to both.

## Build it on your computer

To build without the cloud: macOS arm64 or Linux x86-64, Python 3.11+ (the compiler, pi32v2-llvm,
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

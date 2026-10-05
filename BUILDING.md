# Building SLOOP

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Windows (WSL)

`INSTALL-SLOOP.bat` builds in a WSL distribution and opens the installer on
`http://localhost:8766/webapp/installer/`. It needs Python 3 with Pillow on Windows, a WSL
distribution with the JieLi toolchain, and the three SDK files (below) in `build/deps/ac79`.
Set `SLOOP_WSL_DISTRO` (default `Ubuntu`) and `SLOOP_TOOLCHAIN` (a Linux path, default
`/root/.jieli/toolchain`) if yours differ.

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

Some Docker setups (Rancher Desktop, for one) cannot read symbolic links inside a mounted
folder, and `pi32v2/bin/clang` is a link to `common/bin/clang` (`stat ...: operation not
permitted`). Point `JIELI_TOOLCHAIN` at a copy with the links resolved:

```
mkdir -p ~/.jieli/toolchain-docker
cp -RL ~/.jieli/toolchain/common ~/.jieli/toolchain/pi32v2 ~/.jieli/toolchain-docker/
JIELI_TOOLCHAIN=~/.jieli/toolchain-docker ./build.sh
```

`./build.sh --release 0.9-beta` makes a release build: the package identity becomes
`FM-1_909` and the version string `0.9-BETA`; the package is `build/felucca-0.9-beta.fwsc`.

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_USB_AUDIO` | 1 | EXPERIMENTAL USB audio (from Melodee): 4 track inputs + stereo playback, UAC1, 44.1 kHz 16/24 bit, next to MIDI; replaces the serial console |
| `FELUCCA_CDC` | 0 (1 without USB audio) | USB serial console; with `FELUCCA_USB_AUDIO=1` a build error (shared endpoints) |
| `FELUCCA_UART` | 1 | TRS MIDI IN: notes and the MIDI clock (not tested on hardware) |
| `FELUCCA_ASM` | 1 | the hot loops in pi32v2 asm (`firmware/hal/fm1_dsp_asm.h`): FM6's operators (MODERN, MARK I) and voice output, ANALOG 2's saw / sine / swarm saws, low-pass filters, drive and output stage; 0 = the C reference (bit-identical output) |
| `FELUCCA_ASM_CHECK` | 0 | verification build: every asm loop also runs the C on a copy; `fm6_asm_check` and `a2_asm_check` count calls and differing blocks, ANALOG 2 also self-tests its kernels at the edges once (not for release) |
| `FELUCCA_FM6_KEYS` | 1 | the FM6 operator editor on the black keys (ENV held); 0 = without it (voices edited by SysEx only), ~5.7 KB less flash |
| `FELUCCA_IDLE` | 1 | the main loop waits for an interrupt (`idle`) between UI frames instead of spinning (lower power on hardware: not measured) |
| `FELUCCA_DUAL` | 0 | EXPERIMENTAL second core (`docs/DUAL-CORE.md`, emulator only): 1 = CPU1 starts and counts, 2 = CPU1 renders parts 2 and 3 (`DUAL_PARTS`); `FELUCCA_DUAL_IDLE` (1) lets CPU1 sleep between jobs; `FELUCCA_BENCH` 1..3 = the emulator load scenarios |
| `FELUCCA_SPLASH` | 0 | the boot logo (`splash.c`, ~5.1 KB of flash) and its 0.9 s on screen; 0 = a dark screen, then straight to the UI (the user's choice for the flash budget, `docs/MEMORY-BUDGET.md`) |
| `FELUCCA_DLY_LEN` | 65536 | the delay line in samples, a power of two (2 bytes each, in the pool): 65536 = 1.49 s (a 1/4 note down to 40 BPM); 32768 = 0.74 s (1/4 down to 81 BPM) and 64 KiB more pool. Longer delay times are cut to the line |
| `FELUCCA_SAMPLES_SKIP` | (empty) | built-in sample sets left out, comma-separated set names (PIANO, BASS, VIBES, HORNS, STRGS, FLUTE, SCRCH; not PERC): a reduced build that fits the flash slot. The sets after a skipped one, and USR1..3, move down a number (projects that name a set see another one), and the skipped set's presets fall back to the first SAMPLE preset. Not for release builds: `docs/MEMORY-BUDGET.md` |
| `FELUCCA_SIZE` | 1 | the UI, stores, editor and console built for size (LLVM `minsize`, `tools/size_fns.py`, from Felucca 1.0.1): about 6 KB less flash; the sound side (ISRs, RAM code, engines, mix, sequencer and whatever they call) is never marked and compiles as before. 0 = `-Os` everywhere; `ir` = the IR round trip without marks (byte-identical to 0: a check) |
| `FELUCCA_SIMD` | 0 | EXPERIMENTAL: `sine_i` and ANALOG 2's swarm saws (two copies a pass) with the packed 16-bit instructions (`firmware/hal/fm1_simd.h`); not known to run on the FM-1 yet |
| `FELUCCA_SIMD_CHECK` | 0 | verification build: every SIMD `sine_i` also runs the C; `simd_check` counts calls and differences (not for release) |
| `FELUCCA_SIMD_PROBE` | 0 | EXPERIMENTAL hardware probe: tests the SIMD forms at boot, shows PASS / FAIL, uses the SIMD `sine_i` only after a PASS (implies `FELUCCA_SIMD`; `FELUCCA_SIMD_PROBE_TEST=1`: emulator test of the trap report) |

FM6 against Dexed, sample by sample: `DEXED_SRC=<dexed checkout>/Source sh tests/fm6_parity.sh` (also run by
`tests/run_tests.sh` when `DEXED_SRC` is set).

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

```
tests/run_tests.sh
```

Runs the host tests (flash storage, user presets, MIDI parser, update entry, update
loader, a DSP render, the 4-track mix, project formats, the SLICER, the regression suite,
the command-line installer) and, with Node.js, the web page tests. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build).

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

On Windows, `INSTALL-SLOOP.bat` builds and opens the web installer (Chrome or Edge). The
`.fwsc` of each release is on the GitHub releases page.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

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

## Shortcuts (make)

```sh
make builder                      # the builder menu: pick features, build (build/felucca.fwsc)
make package PROFILE=drum-machine # build a profile, copy .fwsc + -ui.zip into firmwares/
make emu                          # pick a firmware (build/ or firmwares/) and run it in the emulator
make emu FW=optimist              # run one directly at 96 MHz (CPU=own: the firmware's own clock)
make emu-update                   # fetch and rebuild the emulator
```

Put downloaded firmware (stock, Felucca, X0X... `.fwsc`) in `firmwares/` (git-ignored).
Builds are named after the version in `VERSION`: `build/optimist-0.1-dev-<commit>.fwsc` (`-modified` when the
tree has uncommitted changes), `optimist-0.1.fwsc` for `--release 0.1`, and `make package` writes
`firmwares/optimist-<version>-<profile>.fwsc`. (`build/felucca.fwsc` is the same package under its internal name.) The emulator is cloned on
first use into `.emu/fm1-emulator` (git-ignored) from the private fork `github.com/hdavid/fm1-emulator`
(`feat/upstream-merge`: Simon Johansson's emulator plus our work); needs git and Rust (`cargo`).
`EMU_REPO=https://github.com/simonjohansson/fm1-emulator.git make emu` uses upstream instead.

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
`FM-1_709` and the version string `0.9-BETA`; the package is `build/felucca-0.9-beta.fwsc`.

### Package identity

The identity is the name a package and the running firmware give themselves in the update
protocol (handshake cmd 0x11), and the string the `.fwsc` marker bytes carry. **Optimist is
`FM-1_7XY`**: `FM-1_700` for development builds, `FM-1_7XY` for `--release X.Y`. Its update
loader answers `ota-FM-1_700`.

Why this form, and why 7XX:

- The installers split the identity at `_`: the model before it must be `FM-1`, as the device
  reports it (`fm1ota.js`, `fm1_install.py`), and `fm1_install.py` wants digits after it. The
  stock updater reports itself the same way (`FM-1_015`, its loader `ota-FM-1_015`).
- The `.fwsc` marker holds at most 20 characters (`fm1pkg_make.py`).
- The stock firmware's step 1 refuses only the identity it is already running (Baud Girl's
  hardware finding, via Lunar's notes); package content is not authenticated. `FM-1_0XX`
  (M-VAVE, up to V15 `FM-1_015`) and Baud Girl's `FM-1_020`..`FM-1_09X` install from V15, and so
  do Felucca, SLOOP and X0X at `FM-1_9XX`. 7XX sits between them, so any comparison the
  stock updater might make (different, or greater than its own) treats it as it treats those.
- Numbers in use elsewhere: M-VAVE `0XX`, Baud Girl `020`-`09X`, Lunar Modulator's proposal `5XX`,
  Felucca / SLOOP / X0X `9XX` (all of them `FM-1_900` for development builds). 7XX is free.
- The update loader is Felucca's, with the same protocol: the installers resume a device left in
  update mode when its loader is `ota-FM-1_7XX` or `ota-FM-1_9XX` (SLOOP / Felucca), and refuse
  any other loader (`foreign`). The web installer, `fm1_install.py` and `make_site.py` accept both
  ranges, so SLOOP / Felucca packages and devices keep working with them.
- The USB product string is what the editor and the installers look for in port names: they
  accept `felucca` and `optimist` (and `FM-1`), so the device can be renamed without breaking
  them.

Not verified on hardware: that the stock V15 updater installs a `FM-1_7XY` package, and that
M-VAVE's own updater app goes back to V15 from one. The same holds for every `9XX` package
(Felucca, SLOOP); nothing found suggests a numeric window.

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
| `FELUCCA_UNDO_HISTORY` | 1 | undo / redo with many levels (`firmware/src/undo.c`): a history of the steps each change touched, in a ring the linker sizes from the memory nothing else uses (`app.ld` `_undo_*`: the pool after `.pool` up to its last 8 KiB, then main RAM after `.bss`; build.py prints its size and refuses < 1 KiB); ~1.9 KB of flash. 0 = the single level of SLOOP 2.x |
| `FELUCCA_UNDO_CAP` | 0 | the undo history's ring at most this many bytes (0 = all the memory left over) |
| `FELUCCA_IDLE` | 1 | the main loop waits for an interrupt (`idle`) between UI frames instead of spinning (lower power on hardware: not measured) |
| `FELUCCA_DUAL` | 0 | EXPERIMENTAL second core (`docs/DUAL-CORE.md`, emulator only): 1 = CPU1 starts and counts, 2 = CPU1 renders parts 2 and 3 (`DUAL_PARTS`); `FELUCCA_DUAL_IDLE` (1) lets CPU1 sleep between jobs; `FELUCCA_BENCH` 1..3 = the emulator load scenarios |
| `FELUCCA_SPLASH` | 0 | the boot logo (`splash.c`, ~5.1 KB of flash) and its 0.9 s on screen; 0 = a dark screen, then straight to the UI (the user's choice for the flash budget, `docs/MEMORY-BUDGET.md`) |
| `FELUCCA_DLY_LEN` | 65536 | the delay line in samples, a power of two (2 bytes each, in the pool): 65536 = 1.49 s (a 1/4 note down to 40 BPM); 32768 = 0.74 s (1/4 down to 81 BPM) and 64 KiB more pool. Longer delay times are cut to the line |
| `FELUCCA_SAMPLES_SKIP` | (empty) | built-in sample sets left out, comma-separated set names (PIANO, BASS, VIBES, HORNS, STRGS, FLUTE, SCRCH; not PERC): a reduced build that fits the flash slot. The sets after a skipped one, and USR1..3, move down a number (projects that name a set see another one), and the skipped set's presets fall back to the first SAMPLE preset. Not for release builds: `docs/MEMORY-BUDGET.md` |
| `FELUCCA_SIZE` | 1 | the UI, stores, editor and console built for size (LLVM `minsize`, `tools/size_fns.py`, from Felucca 1.0.1): about 6 KB less flash; the sound side (ISRs, RAM code, engines, mix, sequencer and whatever they call) is never marked and compiles as before. 0 = `-Os` everywhere; `ir` = the IR round trip without marks (byte-identical to 0: a check) |
| `FELUCCA_KNOB_ACCEL` | 1 | knobs turned fast step 2 / 3 / 5 / 8 a detent (16 on the tempo; at most 3 on ranges under 100), never on lists or ranges of 24 or less (`knob_accel.h`, from X0X); 0 = one step a detent |
| `FELUCCA_LCD_BAUD` | 1 | LCD SPI clock = 60 MHz / (value + 1): 1 = 30 MHz (X0X runs 30 and 60 on an FM-1); 4 = the old 12 MHz, if a panel shows garbage |
| `FELUCCA_LCD_DIRTY` | 1 | the graph strips (page and overview) send only the rectangle that changed (`lcd_dirty.c`, from X0X): shorter transfers, less tearing (the panel's tearing-effect line reaches no GPIO); ~0.8 KB flash, ~0.65 KB RAM; 0 = the whole strip |
| `FELUCCA_UA_RESAMPLE` | 0 | USB audio capture: 1 = resample the stems to the host's USB clock and send the plain 44.1 pattern (fixed-point cubic, after X0X), for hosts that pass the input live to another output without drift correction (X0X saw such a host drop out with the default); 0 = the packet-size servo (the host sees the I2S rate, ~44,1xx Hz). Playback keeps its explicit feedback either way. `tests/usb_audio_clock_test.c` |
| `FELUCCA_SIMD` | 0 | EXPERIMENTAL: `sine_i` and ANALOG 2's swarm saws (two copies a pass) with the packed 16-bit instructions (`firmware/hal/fm1_simd.h`); not known to run on the FM-1 yet |
| `FELUCCA_SIMD_CHECK` | 0 | verification build: every SIMD `sine_i` also runs the C; `simd_check` counts calls and differences (not for release) |
| `FELUCCA_SIMD_PROBE` | 0 | EXPERIMENTAL hardware probe: tests the SIMD forms at boot, shows PASS / FAIL, uses the SIMD `sine_i` only after a PASS (implies `FELUCCA_SIMD`; `FELUCCA_SIMD_PROBE_TEST=1`: emulator test of the trap report) |

Backported features (defaults in `firmware/src/backports.h`; source, licence and measured cost of each in
`tools/backports.json`; what they do: OPTIMIST.md, "Optional features"):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_CHANCE` | 0 | per-step chance on the synth tracks (SEQ > STEP 2), after Felucca 1.0; +832 B flash |
| `FELUCCA_KEYLIT` | 1 | the keys light the notes the selected synth track plays, after Felucca 1.0.1 and renebohne; +224 B flash |
| `FELUCCA_QNT_SEQ` | 0 | SCL > QNT SEQ: the sequenced notes snap to the scale as they play, after Felucca 1.0.1; +288 B flash |
| `FELUCCA_SPRING` | 0 | FX > REVERB > TYPE: ROOM or a spring reverb, after Felucca 1.0; +1.8 KB flash, +2.0 KB RAM (both reverb loops run from main RAM, 944 B of RAMTEXT freed) |
| `FELUCCA_BASSPLUS` | 0 | MENU > LOWCUT: OFF / LOWCUT / BASS+ (the small speaker's bass as harmonics), after Felucca 1.0; +244 B flash, +288 B RAM |
| `FELUCCA_BRIGHT` | 0 | MENU > BRIGHT 1..8: the backlight by PWM, after X0X (experimental: not tried on hardware here); +304 B flash |
| `FELUCCA_DLY_HALVE` | 1 | a delay time longer than the line halves (on the beat) instead of being cut, after X0X; -60 B flash |
| `FELUCCA_MOTION` | 0 | knob moves recorded per step (SEQ > MOTION), after Felucca 1.0; stored beside each project in its flash sector (no format change); +3.3 KB flash, +0.5 KB RAM, +1.7 KB pool |
| `FELUCCA_ENG_PHYS` | 0 | the PHYS engine (engine 11), after Felucca 1.0 (DaisySP / Rings parts MIT); +9.6 KB flash, +38.7 KB pool: with `FELUCCA_DLY_LEN=32768` only |
| `FELUCCA_ENG_ACID` | 0 | EXPERIMENTAL: the ACID engine (engine 12), X0X's TB-303 voice and TB-3PO generator; float DSP in its own unit (`firmware/src/acid/`, X0X's FPU flags); +13.8 KB flash, +2.3 KB RAM: reduced builds only |

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

# Building Optimist

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Quick start (macOS, Linux, Windows)

One Python entry point does everything; it needs Python 3.9 or newer and no `sh` or `make`
(on Windows type `py` instead of `python` if that is how Python is installed):

```
python tools/optimist.py setup      # check and fetch what the build needs; says what to install by hand
python tools/optimist.py builder    # the menu: pick features, build (build/optimist-<version>-*.fwsc)
python tools/optimist.py emu        # pick a firmware (build/ or firmwares/) and run it in the emulator
```

| Command | What |
| --- | --- |
| `setup [--check] [--yes] [--no-emu]` | check the prerequisites; fetch the builder's venv (Textual, Pillow), the three SDK files and the toolchain (Linux x86-64) or its Docker image (elsewhere); `--check` fetches nothing, `--yes` does not ask |
| `builder [--profile P \| --config F]` | the interactive builder menu (docs/BUILDER.md) |
| `build [--profile P \| --config F \| --defaults] [--set KEY=V] [--release X.Y] [--measure] [--summary F]` | build without the menu: `build/optimist-<version>-dev-<commit>.fwsc` and its `-ui.zip` |
| `package [... the same ...] [--out DIR] [--summary F]` | build, then copy `optimist-<version>-<profile>.fwsc` and its `-ui.zip` to `DIR` (default `firmwares/`) |
| `costs [measure_costs.py args]` | measure the builder's costs that `tools/builder/costs.json` lacks (new items); run after merging a batch, `make costs` (docs/BUILDER.md) |
| `config ...` | the builder without the menu (`--list`, `--budget`, `--fit`, `--write`; the profiles CI builds: `--profiles`, `--publish`, `--unpublish`, `--share`, `--delete`, docs/BUILDER.md; `tools/builder/configure.py --help`) |
| `emu [FIRMWARE] [--cpu MHZ] [--bg] [--list] [--update] [--rebuild]` | run a firmware in the emulator (`emu --help`) |
| `cpu [SECONDS] [--port P] [--csv F] [--label T]` | the audio load of a real FM-1 over USB (a build with the CDC console; pyserial): `cpu_pct` min / mean / max, the worst audio half against the 85 % shed level, the voices shed (`tools/fm1_cpu.py`, from isod89/sloop-fm1 PR #45) |
| `test [--python] [--no-build]` | the host tests (below) |
| `toolchain` | which toolchain, SDK files and Python a build would use |

`build`, `package` and `test` also take `--in-docker`: the whole command then runs inside the
toolchain image, as on a Linux host. Exit status: 0 done, 1 failed, 2 usage or configuration error.
`--summary FILE` writes the result as JSON (ok, files, sizes, the configuration's hash).

The same profile and configuration give the same `.fwsc` bytes whichever way the toolchain runs
(natively, in the image, with a mounted copy, `--in-docker`): see "Reproducibility" below.

On macOS and Linux the older commands still work and call `tools/optimist.py`: `make builder`,
`make build`, `make package PROFILE=drum-machine`, `make emu FW=optimist` (96 MHz; `CPU=own`: the firmware's own clock; `FRESH=1`: without the flash saved in earlier runs), `make emu-update`,
`make test`, `make setup`, `make profiles`, `make publish PROFILE=name` (`make help` lists them), `tools/menuconfig`, `tools/emu.sh`, `./build.sh`.

Put downloaded firmware (stock, Felucca, X0X... `.fwsc`) in `firmwares/` (git-ignored).
Builds are named after the version in `VERSION`: `build/optimist-0.1-dev-<commit>.fwsc` (`-modified` when the
tree has uncommitted changes), `optimist-0.1.fwsc` for `--release 0.1`, and `package` writes
`firmwares/optimist-<version>-<profile>.fwsc`. (`build/felucca.fwsc` is the same package under its internal
name.) Nothing here makes a hidden folder in the tree: the venv is `tools/builder/venv`, the emulator
`emulator/`.

## Prerequisites

| Host | Toolchain | You install | `setup` fetches |
| --- | --- | --- | --- |
| Linux x86-64 | natively | Python 3 with `venv` (Debian/Ubuntu: `apt install python3-venv`) | the toolchain into `~/.jieli`, the SDK files, the venv |
| Linux arm64 | Docker image | Docker Engine (your user in the `docker` group) and amd64 emulation (`binfmt`/QEMU) | the image, the SDK files, the venv |
| macOS | Docker image | Docker Desktop or Rancher Desktop, started (Rosetta on Apple silicon) | the image, the SDK files, the venv |
| Windows | Docker image or WSL | Docker Desktop (WSL 2 backend), or a WSL distribution | the image (Docker), the SDK files, the venv |

Run so far: macOS arm64 with Rancher Desktop, and Linux x86-64 (in a `python:3.12-bookworm` amd64 container,
the steps CI takes). Linux arm64 and Windows (below) have not been run.

For the emulator: git and Rust (`cargo`, from rustup.rs); on Linux also the GUI and audio
libraries (below). For the C host tests: `sh` and a C compiler (macOS: `xcode-select --install`;
Debian: `apt install build-essential`). Node.js is optional (the web page tests).

The pieces, should you want to set them up by hand:

- **The JieLi Linux toolchain** (clang 4.0.1 and GNU binutils 2.26.51 for pi32v2), Linux x86-64
  binaries, from JieLi's server. `setup` takes the pinned version `20250324.1` and checks its SHA-256
  (`tools/toolchain.py`); `tools/get_toolchain.sh` takes whatever JieLi's link serves now.
- **The JieLi AC79 SDK files** (Apache-2.0). The package uses three files of the SDK
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`) of `AC79NN_SDK_V1.2.1_2023-12-13`;
  they are not part of this tree. `setup` (or `tools/get_sdk_files.sh`) fetches only them into
  `sdk/` in this repo (git-ignored; `AC79_SDK` elsewhere; an existing `~/fw-AC79_AIoT_SDK` is still found) and checks their SHA-256.
- **Python packages**: `tools/requirements.txt` (Textual for the menu, Pillow for the generated font and
  icons), in `tools/builder/venv` (`BUILDER_VENV` elsewhere).

## How the toolchain runs

`tools/toolchain.py` picks the first that works (`python tools/optimist.py toolchain` says which):

1. `JIELI_BACKEND=native|image|docker|wsl` forces one.
2. `JIELI_TOOLCHAIN=<dir>`: that copy, natively on Linux x86-64, else mounted into a `linux/amd64`
   `debian:bookworm-slim` container (`JIELI_DOCKER_IMAGE`; `JIELI_DOCKER=1` uses the container on Linux too).
3. Linux x86-64: `~/.jieli/toolchain`, natively.
4. The toolchain image `optimist-toolchain:20250324.1` (`JIELI_TOOLCHAIN_IMAGE`), when Docker runs and the
   image is there.
5. `~/.jieli/toolchain-docker` or `~/.jieli/toolchain` mounted into a container.
6. Windows: a WSL distribution that has the toolchain (below).

Each tool runs in its own short-lived container. A tool that fails in a container because the mount
lags (Rancher Desktop on macOS sometimes shows a folder made a moment before as missing) or crashes
under emulation runs again, up to four times; the builder also retries a whole build that failed
that way.

### The Docker image

`tools/docker/Dockerfile` (Debian bookworm, `linux/amd64`): the toolchain, the three SDK files, Python
with Pillow (pinned) and gcc for the host tests. `setup` builds it when the host cannot run the
toolchain itself; by hand:

```
docker build --platform linux/amd64 -t optimist-toolchain:20250324.1 tools/docker
```

The toolchain is downloaded from JieLi's server while the image builds, on your machine, and its
SHA-256 is checked. **Never push this image to a registry** (next section). A toolchain you
downloaded yourself works without the image: `JIELI_TOOLCHAIN=<dir>` mounts it.

Some Docker setups (Rancher Desktop, for one) cannot read symbolic links inside a mounted folder,
and `pi32v2/bin/clang` is a link to `common/bin/clang` (`stat ...: operation not permitted`). The
image has no such problem; for a mounted copy, resolve the links:

```
mkdir -p ~/.jieli/toolchain-docker
cp -RL ~/.jieli/toolchain/common ~/.jieli/toolchain/pi32v2 ~/.jieli/toolchain-docker/
```

### The toolchain's licence

What was found (2026-10-06), and what it means here:

- The archive (`jieli-linux-toolchains-20250324.1.tar.xz`) holds no licence, copyright notice or terms
  of use: its only document, `README.md`, is installation notes (in Chinese: the directories, `ulimit -n`).
- JieLi's documentation that links it ("工具链（Linux版）", doc.zh-jieli.com, Tools, other_info and
  dev_tools/dev_env) gives only the download link and "© Copyright 2010-2022, 杰理科技股份有限公司";
  no licence, no redistribution or CI terms.
- The binaries are modified free software: `ld` and `ar` say "GNU Binutils 2.26.51.20160621 ... the GNU
  General Public License version 3", `clang` says "LLVM 4.0.1". JieLi publishes no source for its pi32v2
  changes with the archive.

So nothing grants us the right to pass the toolchain on: the GPL parts could be passed on only
with their corresponding source, which we do not have, and JieLi's own parts carry no licence at all.
This repository never contains it, the Docker image is built locally and never pushed, and CI
downloads it from JieLi's public server on each runner (kept only in the runner's cache), as any user
does. Nothing found forbids that use either; if JieLi publishes terms, they decide.

The AC79 SDK is Apache-2.0 (its repository's licence); its three files are fetched, not stored here.

## Windows

`tools/optimist.py` is written to run on Windows (pathlib paths, no shell, `.gitattributes` keeps
scripts LF), but **nothing has been run on Windows yet**. Two ways to build:

**Docker Desktop** (WSL 2 backend), started. Then, in PowerShell or cmd, in the source folder:

```
py tools\optimist.py setup          # the venv, the SDK files, builds the toolchain image
py tools\optimist.py builder
py tools\optimist.py test --in-docker   # the C host tests need sh and a C compiler: in the image
```

**WSL** (Ubuntu, with `python3-venv`): put the toolchain inside the distribution once, then set up
the Windows side, from the source folder:

```
wsl -- BUILDER_VENV=~/optimist-venv python3 tools/optimist.py setup --yes --no-emu   # in WSL: downloads the toolchain
py tools\optimist.py setup                                 # on Windows: the venv, the SDK files; finds WSL's toolchain
py tools\optimist.py builder                               # each tool runs through wsl.exe
```

(`BUILDER_VENV` keeps WSL's Linux venv out of the Windows one in `tools\builder\venv`.)

`JIELI_WSL_DISTRO` names the distribution (default: WSL's default one) and `JIELI_WSL_TOOLCHAIN` the
toolchain's path inside it (default `$HOME/.jieli/toolchain`). Or work inside WSL entirely: there it is
Linux, and everything above works as on Linux.

For the installer and the editor, make the local site as in "Installing" below. `tools/toolchain.py` also finds SDK files in `build/deps/ac79`.

Untested on Windows (written for it, never run): `setup` (venv under `Scripts\`, the SDK download),
the `image` and `docker` backends from a Windows path (`C:\...:/work` mounts), the `wsl` backend
(`wsl --cd`, `wslpath`), `--in-docker`, `builder` (Textual in Windows Terminal), `emu` (cloning, `cargo build`
of `fm1-ui.exe`, starting it; `--bg` with a detached process), `test --python`, and CRLF checkouts.

## Releases and the hosted site

- `.github/workflows/release.yml`, on a `v*` tag (it must match `VERSION`): builds every profile of `config/profiles/`
  (`python tools/optimist.py package --profile P --out dist`, as `build.yml` does, same cached toolchain) and drafts a
  GitHub release with `optimist-<version>-<profile>.fwsc`, `...-ui.zip` and `SHA256SUMS.txt`. Publish the draft by hand
  after reading it.
- `.github/workflows/pages.yml`, on a `v*` tag or by hand: builds `user-default`, runs
  `web/make_site.py <pkg> <version> site` and deploys `site/` with GitHub Pages. The site is static; it holds one
  firmware (`firmware/optimist-<version>.fwsc`, fetched by the installer page by a relative URL and checked against the
  SHA-256 in the page), the installer and the editor. All its URLs are relative, so it works under `/<repository>/`.
- Neither has run (a tag and a repository where Actions and Pages are on are needed; Pages on a private repository
  needs a plan that allows it). Their YAML is checked with `actionlint`.

## Emulator

`python tools/optimist.py emu` clones the emulator on first use into `emulator/fm1-emulator`
(git-ignored) from our public fork `github.com/w0ts/fm1-emulator` (`feat/upstream-merge`: Simon
Johansson's emulator plus our work), builds `fm1-ui` with `cargo build --release --features gui`, and
starts it on the firmware you pick. Each later run fetches the branch and rebuilds when it moved (offline:
it says so and uses the build it has; `EMU_OFFLINE=1` skips the fetch). `EMU_REPO=https://github.com/simonjohansson/fm1-emulator.git`
uses upstream (branch `main`; `EMU_BRANCH` another), `EMU_DIR=<rust-emulator dir>` an existing
checkout. `--cpu MHZ` sets the emulated clock (default 96: correct sound, faster than real time for our
firmware; `--cpu own`: the firmware's own clock, which stock and Baud Girl may want), `--bg` starts it in
the background (log in `emulator/logs/`).

What the firmware writes to flash (the autosave, projects, presets, kits, settings) is kept between runs,
as on the device after a power cycle: in `emulator/state/<family>.nor` and `<family>.index`, the family
being the package name up to its version (`optimist-0.1-dev-b4586d9.fwsc`: `optimist`, so a new build
starts with the last one's data; `sloop-2.3.fwsc`: `sloop`). The emulator saves it a second after the
last flash write, on closing, on Ctrl+C and before its Restart button; a saved sector that overlaps the
new package's code is dropped (new code, old data, like an update). `--fresh` (`make emu FRESH=1`)
starts from the package alone and replaces the state; the emulator's Flash menu has *Reset flash state*.
An emulator without flash states (upstream's, an older `EMU_DIR`) keeps nothing and gets no state options.

Linux needs, to build it, `pkg-config` and the ALSA headers (Debian/Ubuntu:
`apt install pkg-config libasound2-dev`; `setup` checks them), and to run it a desktop with X11 or
Wayland, xkbcommon and OpenGL. Measured in `rust:1-bookworm` (linux/arm64, 2026-10-06): without
`libasound2-dev` the build stops at `alsa-sys` ("failed to run custom build command"); with it,
`cargo check` and `cargo build --release --features gui --bin fm1-ui` pass. The binary links only
`libasound.so.2` (and libc); at run time it opens `libX11.so.6`, `libX11-xcb.so.1`, `libxcb.so.1`,
`libXcursor.so.1`, `libXi.so.6`, `libxkbcommon.so.0`, `libxkbcommon-x11.so.0`, `libwayland-client.so.0`,
`libwayland-egl.so.1`, `libEGL.so.1`, `libGL.so.1` (Debian: libx11-6 libx11-xcb1 libxcb1 libxcursor1 libxi6
libxkbcommon0 libxkbcommon-x11-0 libwayland-client0 libwayland-egl1 libegl1 libgl1, which a desktop has).
It has not been started on Linux here (no display). On macOS it builds and runs with Rust alone;
Windows is untested.

## Worktrees

In a linked `git worktree` of this repository, `make`, `tools/optimist.py` and `tests/run_tests.sh` use what
the **main checkout** has fetched and installed, and put what is missing there (once), so a new worktree
fetches and builds nothing again:

| shared (the main checkout's) | per worktree |
|---|---|
| `sdk/` (the AC79 SDK files), `tools/builder/venv`, `emulator/fm1-emulator` (clone and cargo build), `build/hiphop-src` (sample downloads) | `build/` (outputs, generated headers, test outputs), `emulator/state` and `emulator/logs`, `firmwares/` |

The toolchain (`~/.jieli`) and the Docker image are per user, not per checkout, so they were always shared.
The main checkout is the parent of `git rev-parse --git-common-dir`. A tool that uses a shared resource says so
on one line (`sdk: using the main checkout's (~/GitHub/optimist/sdk)`). The explicit settings come first and
are never replaced: `AC79_SDK`, `BUILDER_VENV`, `EMU_DIR`, `EMU_REPO`, `EMU_BRANCH`. Several worktrees may run at
once: an install into a shared folder holds `optimist-install.lock` in it (a visible file; the others wait
and find the work done); using it takes no lock. A worktree that has its own `sdk/` or venv and a main
checkout without one keeps using its own. The builds are the same: `make build` in a worktree and in the main
checkout give the same `.fwsc`.

**Profiles.** The builder in a worktree also uses the **main checkout's** `config/`: it lists, loads and
**saves** `config/profiles/*.config` and `config/my-profiles/` there (also `publish`, `share`, `delete`), and the
menu's `config/last-used.txt` and default `config/user.config`. A default saved in the builder is therefore the
one every worktree builds. (The tracked profile files change in the main checkout's working tree: commit them
there.) A branch that changes a profile on purpose sets `OPTIMIST_LOCAL_PROFILES=1` (for example
`OPTIMIST_LOCAL_PROFILES=1 make build PROFILE=drum-machine`) to use the worktree's own `config/` and commit it.
Without it, a profile edited in the worktree is ignored. `build/builder.config` (the configuration of the last
build) stays per worktree. The main checkout without `config/profiles` leaves the worktree on its own.

## Reproducibility

The build is reproducible across hosts: nothing in the `.fwsc` depends on the host, the date or the
path. Checked on 2026-10-06 (commit c14c7ba, clean `build/` each time), SHA-256 of `build/felucca.fwsc`:

| Build | Where | user-default | drum-machine `--release 0.1` |
| --- | --- | --- | --- |
| macOS 27 arm64, Python 3.14, Pillow 12.2.0 | `image` backend (each tool in the image) | `88b925fb...ad915f` | `49bd7720...2de631` |
| the same | `docker` backend (`~/.jieli/toolchain-docker` mounted) | `88b925fb...ad915f` | |
| the same | `--in-docker` (the whole build in the image) | `88b925fb...ad915f` | |
| Linux x86-64 (`python:3.12-bookworm`, amd64 container on that Mac), Pillow 12.3.0 | native, after `setup --yes` | `88b925fb...ad915f` | `49bd7720...2de631` |
| the same, Pillow 10.4.0 | native | `88b925fb...ad915f` | |

The four profiles packaged on the Mac (`image`) and natively on Linux also match byte for byte.
The package names differ only by `-modified` (uncommitted changes) and the commit; the bytes do not.

## CI

`.github/workflows/build.yml` (GitHub Actions, `ubuntu-latest`): on a push to `optimist`, a pull
request or by hand, it packages the four profiles (an artifact each: `.fwsc`, `-ui.zip`, the JSON
summary) and runs the host tests. The toolchain and the SDK files are fetched by `setup` on the
runner and cached there, never stored in the repository or the artifacts.

## Build (the scripts)

`./build.sh` (macOS, Linux) and `python tools/build.py` build every registry default (the measurement
configuration) or `--config FILE`; `tools/optimist.py build` is the usual way. `JIELI_TOOLCHAIN` and
`AC79_SDK` override the locations, as above (the SDK files default to `sdk/` in the repo).

`--release 0.1` makes a release build (it must match `VERSION`): the package identity becomes
`FM-1_701` and the version string `0.1 BETA`; the package is `build/optimist-0.1.fwsc`.

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

The sources in `firmware/src/` are in folders by domain (`core/`, `seq/`, `storage/`, `engines/<engine>/`, `fx/`,
`drums/`, `display/`, `ui/`, `io/`, `system/`; the file-by-file table is docs/SOURCE-LAYOUT.md). `firmware/src/felucca.c`
is still the one translation unit that includes them all.

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
| `FELUCCA_FM6_MKI_FLASH` | 0 | FM6 MARK I's log-sine (a quarter cycle) and exponent tables as generated const data in flash (`tools/gen_tables.py`), not RAM tables built at boot: 4,096 B of RAM less, +1,952 B of flash, the same output; the emulator has no XIP cache nor flash wait states, so the CPU cost on the FM-1 is not measured |
| `FELUCCA_FM6_KEYS` | 1 | the FM6 operator editor on the black keys (ENV held); 0 = without it (voices edited by SysEx only), ~5.7 KB less flash |
| `FELUCCA_UNDO_HISTORY` | 1 | undo / redo with many levels (`firmware/src/seq/undo.c`): a history of the steps each change touched, in a ring the linker sizes from the memory nothing else uses (`app.ld` `_undo_*`: the pool after `.pool` up to its last 8 KiB, then main RAM after `.bss`; build.py prints its size and refuses < 1 KiB); ~1.9 KB of flash. 0 = the single level of SLOOP 2.x |
| `FELUCCA_UNDO_CAP` | 0 | the undo history's ring at most this many bytes (0 = all the memory left over) |
| `FELUCCA_IDLE` | 1 | the main loop waits for an interrupt (`idle`) between UI frames instead of spinning (lower power on hardware: not measured) |
| `FELUCCA_DUAL` | 0 | EXPERIMENTAL second core (`docs/DUAL-CORE.md`, emulator only): 1 = CPU1 starts and counts, 2 = CPU1 renders parts 2 and 3 (`DUAL_PARTS`); `FELUCCA_DUAL_IDLE` (1) lets CPU1 sleep between jobs; `FELUCCA_BENCH` 1..3 = the emulator load scenarios |
| `FELUCCA_SPLASH` | 1 | the boot screen (`splash.c`: the Optimist logo drawn from its geometry, no bitmap; "OPTIMIST" and the version), 368-624 B of flash by profile, 0.9 s on screen; 0 = a dark screen, then straight to the UI |
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

Backported features (defaults in `firmware/src/core/backports.h`; source, licence and measured cost of each in
`tools/backports.json`; what they do: OPTIMIST.md, "Optional features"):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_CHANCE` | 0 | per-step chance on the synth tracks (SEQ > STEP 2), after Felucca 1.0; +832 B flash |
| `FELUCCA_KEYLIT` | 1 | the keys light the notes the selected synth track plays, after Felucca 1.0.1 and renebohne; +224 B flash |
| `FELUCCA_QNT_SEQ` | 0 | SCL > QNT SEQ: the sequenced notes snap to the scale as they play, after Felucca 1.0.1; +288 B flash |
| `FELUCCA_SPRING` | 0 | FX > REVERB > TYPE: ROOM or a spring reverb, after Felucca 1.0; +1.8 KB flash, +2.0 KB RAM (both reverb loops run from main RAM, 944 B of RAMTEXT freed) |
| `FELUCCA_BASSPLUS` | 0 | MENU > LOWCUT: OFF / LOWCUT / BASS+ (the small speaker's bass as harmonics), after Felucca 1.0; +244 B flash, +288 B RAM |
| `FELUCCA_BRIGHT` | 0 | EXPERIMENTAL, keep off: MENU > BRIGHT 1..8, the backlight by PWM, after X0X. On X0X a low level froze a real FM-1 and the saved level froze it again at every boot (X0X issue #2, c6f2bf6); here the screen always boots at full and never goes below 4/16, untested on hardware; +304 B flash |
| `FELUCCA_DLY_HALVE` | 1 | a delay time longer than the line halves (on the beat) instead of being cut, after X0X; -60 B flash |
| `FELUCCA_MOTION` | 0 | knob moves recorded per step (SEQ > MOTION), after Felucca 1.0; stored beside each project in its flash sector (no format change); +3.3 KB flash, +0.5 KB RAM, +1.7 KB pool |
| `FELUCCA_ENG_PHYS` | 0 | the PHYS engine (engine 11), after Felucca 1.0 (DaisySP / Rings parts MIT); +9.6 KB flash, +38.7 KB pool: with `FELUCCA_DLY_LEN=32768` only |
| `FELUCCA_ENG_ACID` | 0 | EXPERIMENTAL: the ACID engine (engine 12), X0X's TB-303 voice and TB-3PO generator; float DSP in its own unit (`firmware/src/engines/acid/`, X0X's FPU flags); +14.7 KB flash, +1.7 KB RAM: reduced builds only |
| `FELUCCA_ENG_CZ` | 0 | the CZ engine (engine 13), from Melodee 0.11 (Kerem Kilic; native playback after the MAME uPD933 model, BSD-3-Clause): two lines with 8-step DCO / DCW / DCA envelopes, 8 tones of our own (no Casio factory tones), 8 EDIT values; +6.9 KB flash, +3.1 KB RAM (everything-that-fits: +7.3 KB, +3.2 KB, +32 B RAM code); on in everything-that-fits |
| `FELUCCA_MONO_RELEASE` | 1 | SLOOP 2.3: a key let go just after a VOICE change leaves no stuck note; 0 B |
| `FELUCCA_ST_STRICT` | 1 | SLOOP 2.3: stricter checks of what is read back from flash (the copy a record was written to, object bounds, the calibration a permutation); +96 B flash |
| `FELUCCA_USB_FLOW` | 1 | SLOOP 2.3: USB MIDI in waits (NAK) instead of dropping when the ring is full; malformed events ignored; +272 B flash |
| `FELUCCA_SHED_FADE` | 1 | SLOOP 2.3: on overload (two halves in a row) one voice fades at a time, never the bass or the lead; +96 B flash |
| `FELUCCA_KEYS_FAST` | 1 | SLOOP 2.3: keys debounced as their column is read (~1.6 ms sooner, host-measured); +32 B flash |
| `FELUCCA_KNOB_ONEREST` | 1 | SLOOP 2.3 (Felucca 1.0 #23) with our X0X decoder: one rest state a detent, whole cycles; +304 B flash, +32 B RAM |
| `FELUCCA_TRS_NOISE` | 1 | SLOOP 2.3 (after Felucca [Salt]): a received FD at the TRS MIDI reader no longer stalls the jack; +16 B flash |
| `FELUCCA_BK_CHECK` | 1 | SLOOP 2.3: a restore writes a storage object only if the firmware would load it (rc 8 otherwise); +432 B flash |
| `FELUCCA_REC_MODES` | 0 | SLOOP 2.3: the REC screen's dials (mode free / tempo, length, start note / count-in 4-3-2-1); +1.2 KB flash, +156 B RAMTEXT |
| `FELUCCA_LIGHTS` | 0 | SLOOP 2.3: MENU LIGHTS / KEYS / NOTES (NOTES = KEYLIT at run time); +1.6 KB flash, +152 B RAMTEXT |
| `FELUCCA_GLIDE` | 0 | EXPERIMENTAL, X0X 0.10.1: the mixer gains glide over ~10 ms (no zipper); +1.4 KB flash, +240 B RAM, +840 B RAMTEXT |
| `FELUCCA_SL24_SAFE` | 1 | Started on flash SLOOP 2.4 (8d3823f) wrote: nothing Optimist cannot read is erased or written over (the project slots and autosave, 2.4's FM6 bank at 0xE5000, a USR3 / USR4 sample longer than ours); 2.4's slots show as SLOOP 2.4; 2.4's settings word and user presets read right (tests/sl24_safety_test.c); +708 B flash, +96 B RAM |
| `FELUCCA_SL24_XSTEP` | 0 | SLOOP 2.4's step extras (stepx.h: nudge, 24 locks a track, fill conditions, 2.4's layout) kept with every project: a store per project buffer, a section log record of their own (ids 88..104) beside each section and the autosave (tests/stepx_store_test.c); +1.6 KB flash, +864 B RAM, +4.2 KB pool |

FM6 against Dexed, sample by sample: `DEXED_SRC=<dexed checkout>/Source sh tests/fm6_parity.sh` (also run by
`tests/run_tests.sh` when `DEXED_SRC` is set).

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

```
python tools/optimist.py test             # the two builds below, then tests/run_tests.sh
python tools/optimist.py test --in-docker # the same in the toolchain image (Linux; also from Windows)
python tools/optimist.py test --python    # the Python tests only (any host, no build, no C compiler)
```

Runs the host tests (flash storage, user presets, MIDI parser, update entry, update
loader, a DSP render, the 4-track mix, project formats, the SLICER, the regression suite,
the command-line installer, the builder and `optimist.py`) and, with Node.js, the web page tests.
The C host tests compile against their own headers, `build/gen-host`, which `tests/run_tests.sh`
regenerates on each run with every sample set (`tools/build.py --host-headers`), never the profile's
`build/gen`: the regression goldens hold whichever profile was built. The target checks read
`build/` as the last builds left it: the installer, update and rescue tests need a package that
fits and its app (`felucca.fwsc`, `felucca.bin`, `loader/ota.bin`), the target-cost check
`felucca.dis` and `build/gen/felucca_config.h`. `test` builds `user-default`, then a measurement
build of the default configuration (every item; it does not fit the slot) so the target cost covers
every render loop (`--no-build`: test the `build/` there is, for example after
`make build PROFILE=...`). The C tests need `sh` and a C compiler.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

The web installer (Chrome or Edge) is on the project's GitHub Pages site once that is enabled (see
"Releases and the hosted site" below), or make it locally, as below. The
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

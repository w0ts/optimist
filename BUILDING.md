# Building Optimist

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

Builds are named after `VERSION`: `build/optimist-0.1-dev-<commit>.fwsc` (`-modified` when the tree has uncommitted
changes), `optimist-0.1.fwsc` for `--release 0.1`, and `package` writes `firmwares/optimist-<version>-<profile>.fwsc`
(`build/felucca.fwsc` is the same package under its internal name), each with a `-ui.zip` (the web editor's sidecar).
Put downloaded firmware (stock, Felucca, X0X ...) in `firmwares/` (git-ignored). Nothing here makes a hidden folder in
the tree: the venv is `tools/builder/venv`, the emulator `emulator/`.

## Quick start (macOS, Linux, Windows)

One Python entry point does everything; it needs Python 3.9 or newer and no `sh` or `make` (on Windows type `py` if
that is how Python is installed):

```
python tools/optimist.py setup      # check and fetch what the build needs; says what to install by hand
python tools/optimist.py builder    # the menu: pick features, build
python tools/optimist.py emu        # pick a firmware (build/ or firmwares/) and run it in the emulator
```

| Command | What |
| --- | --- |
| `setup [--check] [--yes] [--no-emu]` | check the prerequisites; fetch the builder's venv (Textual, Pillow), the three SDK files and the toolchain (Linux x86-64) or its Docker image (elsewhere). `--check` fetches nothing, `--yes` does not ask |
| `builder [--profile P \| --config F]` | the interactive builder menu (docs/BUILDER.md) |
| `build [--profile P \| --config F \| --defaults] [--set KEY=V] [--release X.Y] [--measure] [--summary F] [--classic]` | build without the menu (the release on fm1-base; `--classic`: Optimist's own firmware, any command). Default profile: `synths_USBaudio_BTaudio_BTmidi` |
| `package [... the same ...] [--out DIR]` | build, then copy `optimist-<version>-<profile>.fwsc` and its `-ui.zip` to `DIR` (default `firmwares/`) |
| `flash [PACKAGE.fwsc \| --no-build] [--profile P \| --config F] [--port NAME] [--yes]` | install a build on an FM-1 over USB-MIDI (below, "Install") |
| `config ...` | the builder without the menu (`--list`, `--budget`, `--fit`, `--write`, `--profiles`, `--publish`, `--share`, `--delete`; `tools/builder/configure.py --help`) |
| `cloud-key [CONFIG]` | the cache key of a cloud build and the canonical configuration it hashes (docs/WEB.md) |
| `costs [measure_costs.py args]` | measure what `tools/builder/costs.json` lacks (new items); run after merging a batch (docs/BUILDER.md, "Budget") |
| `emu [FIRMWARE] [--cpu MHZ\|own] [--bg] [--fresh] [--list] [--update] [--rebuild]` | run a firmware in the emulator (below) |
| `web [--port N] [--sim] [--profile P \| --config F] [--no-open]` | the site on `127.0.0.1:8766`: installer, editor, builder and simulator, with no firmware in it (install a `.fwsc` of yours or a cloud build). Offers to build the local simulator when there is none or it is outdated (`--sim`: always; needs Emscripten). Chrome or Edge for Web MIDI |
| `cpu [SECONDS] [--port P] [--csv F] [--label T]` | the audio load of a real FM-1 from the CDC console (pyserial): `cpu_pct` min / mean / max, the worst half against the 85 % shed level, the voices shed (`tools/fm1_cpu.py`) |
| `test [--python] [--no-build] [--in-docker]` | the host tests (below) |
| `gate`, `emu-check` | the integrator's gate and the emulator checks (below) |
| `toolchain` | which toolchain, SDK files and Python a build would use |

`build`, `package` and `test` also take `--in-docker`: the whole command runs inside the toolchain image, as on a Linux
host. Exit status: 0 done, 1 failed, 2 usage or configuration error. `--summary FILE` writes the result as JSON (ok,
files, sizes, the configuration's hash). The same profile and configuration give the same `.fwsc` bytes whichever way the
toolchain runs (see "Reproducibility").

On macOS and Linux `make` has the same shortcuts (`make help` lists them): `make builder`, `build`, `package`, `flash`,
`test`, `classic`, `setup`, `profiles`, `publish` / `share` / `delete PROFILE=name`, `web` (`PORT=`, `SIM=1`), `emu` (`FW=`,
`CPU=`, `FRESH=1`), `emu-list`, `emu-update`, `costs`, `costs-all`, `gate`, `emu-check`; also `tools/menuconfig`,
`tools/emu.sh` and `./build.sh`.

## The release build (Optimist on fm1-base)

The firmware `make`, `make package`, `optimist.py build|package|builder|flash` and the web builder make is the next
release: Optimist as an app of fm1-base (the FM-1 platform: its build, its update loader, its USB, its second core), in
the submodule `fm1base/` (the fork `git@github.com:w0ts/fm1-base.git`; the app is `fm1base/optimist/`, its bsp the nested
submodule `fm1base/bsp`). Optimist's own firmware, what `main` still ships, stays available as `--classic`.

```
git submodule update --init --recursive      # once, and after a pull that moves fm1base
make package [PROFILE=name]                  # the release: firmwares/optimist-<version>-<profile>.fwsc (+ -ui.zip)
make classic [PROFILE=name]                  # Optimist's own firmware (build/), as `make build` was
make test                                    # Optimist's tests, then the release's (below)
make flash PKG=firmwares/optimist-....fwsc   # the user installs it
```

**The fork must be reachable.** It is not public yet, so the submodule fetch needs access to `w0ts/fm1-base` and to the
bsp it points at (a commit of the fork's `felucca-bsp`); until the fork and its bsp commits are pushed, a clone cannot
fetch them and `git submodule update` fails (a checkout that has them locally works). Without the submodule the build says
so (`fm1base/ is empty: run git submodule update --init --recursive`); `--classic` needs none of it.

How a build goes: the builder's chosen profile or `.config` (`--profile`, `--config`, `--set`: the same items and
profiles) is written as `build/builder.config`, and `tools/fm1base.py` runs `fm1base/tools/build.py optimist` with
`OPTIMIST` = this checkout and `OPT_CONFIG` = that file (the app's `gen_optimist.py` makes the configuration header and the
generated tables from it through the builder's own `configure.py`; Bluetooth, Bluetooth audio and CACHE_RAM are the
configuration's own, on fm1-base's: `fm1.toml`'s `ble`, `bt_audio` and `cache_ram_ways` follow it). An SD card mode profile (`LAYOUT=1`) builds with `FM1B_APP_END=0xCE000`, fm1-base's bigger app slot,
by design with fewer flash projects. The toolchain is `JIELI_TOOLCHAIN` (default `~/.jieli/toolchain`), the SDK files
`AC79_SDK` (`make sdk`). The package lands where Optimist's does (`build/felucca.fwsc`, `build/optimist-<version>.fwsc`,
`firmwares/` for `package`); `build/sizes.json` comes from fm1-base's `build/sizes.json` (flash against the same 581,564 B
slot, the packed SRAM). One fm1-base build runs at a time (its build directory is the app's own, `fm1base/optimist/build`).

The builder's size model: `tools/builder/costs-fm1base.json`, measured through this build (`make costs-all`, `JOBS=N` builds
N at once, each in a copy of the app directory, `fm1base/optimist-<n>`, removed afterwards; on the farm, never `ac79-min`).
fm1-base has no measurement mode, so a measurement build uses a linker wrapper (`tools/fm1base_ld.py`: room for any
configuration, native toolchain only) and its `sizes.json` is read although the build then reports it over the slot. The
release builds not every configuration (not the defaults: `ED_LIVE` is not wired, the X0X unit wants a kit), so the deltas are
measured from the defaults with `RELEASE_BASE` (`measure_costs.py`); what the release does not build at all is listed in the
file's `"unbuildable"`. Each kind (classic, release) measures only what it can build: a value the registry marks in `registry.NOT_BUILT` for that kind (with the reason: `ENG_PHYS` "on, code in RAM" in both, `OVERLAY` in the classic) is not offered by the menu or accepted by `core.validate`, and is written as `"not in this build: <reason> [link: <the link error>]"` in that kind's costs file (its delta 0; the run still builds it once to show the error, and reports a marked value that links). A configuration that should build and does not fails the run at the end, loudly (`measure_costs.py` prints each with its error, exit status 1). The file's `"limits"` are the release's slots (the app slot of `sizes.json`, the SD card
mode's bigger one for `LAYOUT 1`, the SRAM of `app.ld`), which the fit checks use (`core.fits`) instead of the classic's. Without
the file the budget uses `costs.json` (Optimist's classic measurements) and says so; a real build is exact and remembered in
`build/exact-sizes-fm1base.json`. The web builder's data (`web/builder/data/costs.json`) is whichever of the two the CLI reads.

`make test` runs Optimist's own tests (they build and test the classic firmware, as ever), then the release's:
`fm1base/optimist/run.sh` (the app on fm1-base's host simulator, with Optimist's source defaults and the dev profile, against
Optimist's own simulator) and `fm1base/tests/run_tests.sh`, with the builder venv's Python first on `PATH`.

`flash`: Optimist's update tool when the FM-1 runs Optimist; otherwise (none answers) fm1-base's
`fm1base/tools/fm1_install.py PACKAGE --port FM1-` (`--port Optimist` for an FM-1 on firmware from before 2026-10-29). It shows both identities and asks; never run it unattended.

Cloud builds (`.github/workflows/cloud-build.yml`) build the release: the checkout fetches the submodules, which needs a
repository secret `FM1BASE_TOKEN` (read access to the fork and its bsp) while the fork is private, and pushed commits for the
gitlinks.

## Prerequisites

| Host | Toolchain | You install | `setup` fetches |
| --- | --- | --- | --- |
| Linux x86-64 | natively | Python 3 with `venv` (Debian/Ubuntu: `apt install python3-venv`) | the toolchain into `~/.jieli`, the SDK files, the venv |
| Linux arm64 | Docker image | Docker Engine (your user in the `docker` group) and amd64 emulation (`binfmt`/QEMU) | the image, the SDK files, the venv |
| macOS | Docker image | Docker Desktop or Rancher Desktop, started (Rosetta on Apple silicon) | the image, the SDK files, the venv |
| Windows | Docker image or WSL | Docker Desktop (WSL 2 backend), or a WSL distribution | the image (Docker), the SDK files, the venv |

Run so far: macOS arm64 with Rancher Desktop, and Linux x86-64 (CI, and a `python:3.12-bookworm` amd64 container). Linux
arm64 and Windows have not been run.

For the emulator: git and Rust (`cargo`, from rustup.rs); on Linux also the GUI and audio libraries (below). For the C
host tests: `sh` and a C compiler (macOS: `xcode-select --install`; Debian: `apt install build-essential`). Node.js is
optional (the web page tests).

The pieces, should you want to set them up by hand:

- **The JieLi Linux toolchain** (clang 4.0.1 and GNU binutils 2.26.51 for pi32v2), Linux x86-64 binaries from JieLi's
  server. `setup` takes the pinned version `20250324.1` and checks its SHA-256 (`tools/toolchain.py`);
  `tools/get_toolchain.sh` takes whatever JieLi's link serves now.
- **Three files of the JieLi AC79 SDK** (Apache-2.0; `AC79NN_SDK_V1.2.1_2023-12-13`: `cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`). They are not part of this tree. `setup` (or `make sdk`) fetches only them into
  `sdk/` (git-ignored; `AC79_SDK` names another place; `~/fw-AC79_AIoT_SDK` is still found) and checks their SHA-256.
- **Python packages**: `tools/requirements.txt` (Textual for the menu, Pillow for the generated font and icons) in
  `tools/builder/venv` (`BUILDER_VENV` elsewhere); `tools/requirements-flash.txt` (mido, python-rtmidi) for `flash`.
- **BLE build** (builder item `BLE`): nothing extra. The radio's start-up tables, captured from the stock V15
  firmware, are in the tree (`firmware/hal/ble_rf_tables_v15.h`). To capture them again, `tools/ble_rf_capture.py` takes
  your own V15 (`FM1_STOCK_FWSC=/path/to/FM-1.fwsc` or `firmwares/FM-1.fwsc`; SHA-256 `db1642b2...`) and the emulator's
  `diagnose` build, and keeps the result in the git-ignored `config/ble/`. Nothing is downloaded. Details:
  `docs/BLE-STACK.md`.

## How the toolchain runs

`tools/toolchain.py` picks the first that works (`python tools/optimist.py toolchain` says which):

1. `JIELI_BACKEND=native|image|docker|wsl` forces one.
2. `JIELI_TOOLCHAIN=<dir>`: that copy, natively on Linux x86-64, else mounted into a `linux/amd64` `debian:bookworm-slim`
   container (`JIELI_DOCKER_IMAGE`; `JIELI_DOCKER=1` uses the container on Linux too).
3. Linux x86-64: `~/.jieli/toolchain`, natively.
4. The toolchain image `optimist-toolchain:20250324.1` (`JIELI_TOOLCHAIN_IMAGE`), when Docker runs and the image is there.
5. `~/.jieli/toolchain-docker` or `~/.jieli/toolchain` mounted into a container.
6. Windows: a WSL distribution that has the toolchain (`JIELI_WSL_DISTRO`, `JIELI_WSL_TOOLCHAIN`).

Each tool runs in its own short-lived container. A tool that fails because the mount lags or crashes under emulation
runs again, up to four times; the builder also retries a whole build that failed that way.

**Stale files in the container (macOS).** The container reaches the tree through the VM's file share, and the share
caches file attributes (Rancher Desktop's reverse-sshfs: 20 s, plus about 1 s in the VM's kernel). A file the host
rewrites within that time can be seen with its old size: the compiler then reads the new text cut short ("expected
'}'") or padded with NULs ("null character ignored"), and a same-size change could compile silently wrong. Measured
2026-10-09 on Rancher Desktop 1.24: up to 15 of 100 fresh compiles failed after a rename; fsync and waiting did not help.
The fix is `tools/toolchain.py sync_view`: before the tools read files the host has just written (`build/gen`,
`build/felucca_size.ll`, and anything under `firmware/` changed in the last 120 s), `tools/build.py` runs a container
that `touch`es each one (which drops the cached attributes) and prints its size and SHA-256 as the container sees it.
If any differs from the host's the build stops with "the container sees stale files" and names them. It costs about
0.7 s of an 11 s build, and is skipped natively, on WSL and with `--in-docker`. To measure it on your machine:
`python3 tools/mount_stress.py "$PWD" --probe cc` (add `--fix` for `sync_view`).

### The Docker image

`tools/docker/Dockerfile` (Debian bookworm, `linux/amd64`): the toolchain, the three SDK files, Python with Pillow and gcc
for the host tests. `setup` builds it when the host cannot run the toolchain itself; by hand:

```
docker build --platform linux/amd64 -t optimist-toolchain:20250324.1 tools/docker
```

The toolchain is downloaded from JieLi's server while the image builds, on your machine, and its SHA-256 is checked.
**Never push this image to a registry** (next section). A toolchain you downloaded yourself works without the image:
`JIELI_TOOLCHAIN=<dir>`. Some Docker setups (Rancher Desktop) cannot read symbolic links inside a mounted folder, and
`pi32v2/bin/clang` is a link; for a mounted copy, resolve them with `cp -RL`
(`~/.jieli/toolchain/{common,pi32v2}` to `~/.jieli/toolchain-docker/`).

### The toolchain's licence

Checked 2026-10-06. The archive holds no licence, copyright notice or terms of use. JieLi's documentation gives only the
download link and a copyright line. The binaries are modified free software (`ld` and `ar`: GNU Binutils 2.26.51, GPL
version 3; `clang`: LLVM 4.0.1) and JieLi publishes no source for its pi32v2 changes. So nothing grants us the right to
pass the toolchain on: this repository never contains it, the Docker image is built locally and never pushed, and CI
downloads it from JieLi's public server on each runner (kept only in the runner's cache), as any user does. If JieLi
publishes terms, they decide. The AC79 SDK is Apache-2.0; its three files are fetched, not stored here.

### The compiler

The builder's item **COMPILER** (bit 442; the console builder lists it, the web builder holds JieLi for now) picks the C
compiler for both builds, Optimist's own (`tools/build.py`) and the release (`tools/fm1base.py`):

- **pi32v2-llvm** (default): Julius Vitkauskas's LLVM with a pi32v2 target
  ([pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm), [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa)),
  used as his *drop-in* for JieLi's toolchain: a directory with the same layout and tool names (`pi32v2/bin/clang`, `ld`,
  `common/bin/objdump`, ...) whose wrapper needs Python 3.11. Measured 2026-10-29: the same firmware 14 KB (Optimist's
  own, default profile) to 30 KB (a full release configuration) smaller, the audio's CPU the same; played on an FM-1.
- **JieLi's clang 4** (`COMPILER=0`): the toolchain above.

`setup` downloads the drop-in into `build/toolchains/pi32v2-llvm-dropin-<version>` of the repository (git-ignored; nothing goes into your home; `PI32V2_LLVM_TOOLCHAIN=<dir>` for another). In a git worktree it is the main checkout's `build/toolchains` (one copy for all), in a plain copy of a tree (the farm's tar copies) `OPTIMIST_TOOLCHAINS=<dir>`, else the nearest `build/toolchains` above the tree (`tools/repo_root.py`; M-Flasher's `build/m-flasher` follows the same rule). An older copy in `~/.jieli/pi32v2-llvm` is still found, with a note to move it: the archive
`pi32v2-llvm-dropin-<llvm>-<isa>.tar.xz`, a release asset of this repository, its SHA-256 pinned in `tools/toolchain.py`
(`LLVM_URL`, `LLVM_SHA256`). On Linux x86-64 it runs natively; elsewhere each tool runs in the toolchain image with the
drop-in mounted at `/opt/jieli` (the image's Python; plain `debian:bookworm-slim` has none), the release build's container
too (`JIELI_DOCKER_IMAGE`). WSL: not yet (use JieLi there).

The archive is made from pinned commits on Linux x86-64 (about an hour on 8 cores):

```
git clone --branch pi32v2 https://github.com/jvitkauskas/pi32v2-llvm LLVM && git -C LLVM checkout 4f631b31b
git clone https://github.com/jvitkauskas/pi32v2-isa ISA && git -C ISA checkout e575321
sh ISA/tools/llvm/package.sh LLVM build prefix/pi32v2-llvm            # clang, ld.lld, the tools; build/runtime
sh ISA/tools/dropin/make_dropin.sh --copy prefix/pi32v2-llvm/bin build/runtime pi32v2-llvm-dropin-4f631b31b-e575321
```

then its licences (`LLVM/LICENSE.TXT`, `ISA/LICENSE`, `ISA/LICENSES/*`) into `licenses/`, a `VERSION` file with the two
commits, and `tar -cJf` of the directory. Licences: Apache-2.0 WITH LLVM-exception (LLVM), MIT-0 (pi32v2-isa's tools), so
unlike JieLi's it may be passed on. The build's checks read the disassembly with `-mcpu=r3` (`tools/build.py`
`OBJDUMP_CPU`: without it the FPU's instructions are not decoded, with either compiler), and the ROM check flags a
constant only where it is used as an address (`tools/romref.py`).

## Windows

`tools/optimist.py` is written to run on Windows (pathlib paths, no shell, `.gitattributes` keeps scripts LF), but
**nothing has been run on Windows yet**. Two ways to build:

**Docker Desktop** (WSL 2 backend), started. In PowerShell or cmd, in the source folder:

```
py tools\optimist.py setup              # the venv, the SDK files, builds the toolchain image
py tools\optimist.py builder
py tools\optimist.py test --in-docker   # the C host tests need sh and a C compiler: in the image
```

**WSL** (Ubuntu with `python3-venv`): put the toolchain inside the distribution once, then set up the Windows side:

```
wsl -- BUILDER_VENV=~/optimist-venv python3 tools/optimist.py setup --yes --no-emu   # in WSL: downloads the toolchain
py tools\optimist.py setup              # on Windows: the venv, the SDK files; finds WSL's toolchain
py tools\optimist.py builder            # each tool runs through wsl.exe
```

(`BUILDER_VENV` keeps WSL's Linux venv out of the Windows one.) Or work inside WSL entirely: there it is Linux.

## Emulator

`python tools/optimist.py emu` clones the emulator on first use into `emulator/fm1-emulator` (git-ignored) from our
public fork `github.com/w0ts/fm1-emulator` (`feat/upstream-merge`: Simon Johansson's emulator plus our work), builds
`fm1-ui` with `cargo build --release --features gui`, and starts it on the firmware you pick. Each later run fetches the
branch and rebuilds when it moved (offline: it uses the build it has; `EMU_OFFLINE=1` skips the fetch). `EMU_REPO`
names another repository (upstream `https://github.com/simonjohansson/fm1-emulator.git` uses branch `main`;
`EMU_BRANCH` another), `EMU_DIR` an existing checkout. `--cpu MHZ` sets the emulated clock (default 96: correct sound,
faster than real time for our firmware; `--cpu own`: the firmware's own clock, which stock and Baud Girl may want),
`--bg` starts it in the background (log in `emulator/logs/`).

What the firmware writes to flash (autosave, projects, presets, kits, settings) is kept between runs, as on the device
after a power cycle: `emulator/state/<family>.nor` and `.index`, the family being the package name up to its version
(`optimist-0.1-dev-b4586d9.fwsc`: `optimist`, so a new build starts with the last one's data). A saved sector that
overlaps the new package's code is dropped (new code, old data, like an update). `--fresh` (`make emu FRESH=1`) starts
from the package alone and replaces the state; the emulator's Flash menu has *Reset flash state*. An emulator without
flash states (upstream's) keeps nothing.

Linux needs, to build it, `pkg-config` and the ALSA headers (`apt install pkg-config libasound2-dev`; `setup` checks
them), and to run it a desktop with X11 or Wayland, xkbcommon and OpenGL (the libraries a desktop has: libx11, libxcb,
libxcursor, libxi, libxkbcommon(-x11), libwayland-client, libwayland-egl, libegl, libgl). It has not been started on
Linux here (no display). On macOS it builds and runs with Rust alone; Windows is untested.

## Worktrees

In a linked `git worktree`, `make`, `tools/optimist.py` and `tests/run_tests.sh` use what the **main checkout** has
fetched and installed, and put what is missing there once (`tools/shared.py`):

| shared (the main checkout's) | per worktree |
|---|---|
| `sdk/`, `tools/builder/venv`, `emulator/fm1-emulator` (clone and cargo build), `build/hiphop-src` (sample downloads) | `build/`, `emulator/state` and `emulator/logs`, `firmwares/` |

The toolchain (`~/.jieli`) and the Docker image are per user. A tool that uses a shared resource says so on one line.
Explicit settings come first and are never replaced: `AC79_SDK`, `BUILDER_VENV`, `EMU_DIR`, `EMU_REPO`, `EMU_BRANCH`.
Several worktrees may run at once (an install into a shared folder holds `optimist-install.lock`). The builds are the
same as in the main checkout.

**Profiles.** The builder in a worktree also uses the **main checkout's** `config/`: it lists, loads and **saves**
`config/profiles/*.config` and `config/my-profiles/` there (also `publish`, `share`, `delete`), and the menu's
`config/last-used.txt` and default `config/user.config`. The tracked profile files therefore change in the main
checkout's working tree: commit them there. A branch that changes a profile on purpose sets `OPTIMIST_LOCAL_PROFILES=1`
to use the worktree's own `config/`. `build/builder.config` (the last build's configuration) stays per worktree.

## Reproducibility

The build is reproducible across hosts: nothing in the `.fwsc` depends on the host, the date or the path. Checked
2026-10-06 (commit c14c7ba, clean `build/` each time): two profiles of that time built with `--release 0.1` gave the same
SHA-256 of `build/felucca.fwsc` on macOS arm64 (the `image`, `docker` and `--in-docker` backends) and on Linux x86-64
natively, with Pillow 10.4.0, 12.2.0 and 12.3.0. The package names differ only by `-modified` and the commit; the bytes
do not. (Not repeated since; the firmware has changed a great deal.)

## CI and releases

- `.github/workflows/build.yml` (`ubuntu-latest`): on a push to `optimist`, a pull request or by hand, it packages the
  published profiles (`config --published`; an artifact each: `.fwsc`, `-ui.zip`, the JSON summary), runs `optimist.py
  test`, and checks that every builder item has a measured cost (`measure_costs.py --missing --check`). The toolchain and
  SDK files are fetched by `setup` on the runner and cached there, never stored in the repository or the artifacts.
- `.github/workflows/release.yml`, on a `v*` tag (it must match `VERSION`): release builds of every profile in
  `config/profiles/` (`package --profile P --release <version>`) in a **draft** release with `SHA256SUMS.txt`. Publish the
  draft after reading it.
- `.github/workflows/pages.yml`, on a push to `main` (and when a release is published): the site at
  <https://w0ts.github.io/optimist/>. How it is made, cutting a release, cloud builds: docs/WEB.md.

## Build scripts, flags and identity

`./build.sh` (macOS, Linux) and `python tools/build.py` build every registry default (the measurement configuration) or
`--config FILE`; `tools/optimist.py build` is the usual way. `JIELI_TOOLCHAIN` and `AC79_SDK` override the locations.
`--release X.Y` makes a release build (it must match `VERSION`): the package identity becomes `FM-1_7(X+2)Y` and the
version string `X.Y BETA`; the package is `build/optimist-X.Y.fwsc`.

The sources in `firmware/src/` are in folders by domain (`core/`, `seq/`, `storage/`, `engines/<engine>/`, `fx/`,
`drums/`, `display/`, `ui/`, `io/`, `system/`, `bt/`); `firmware/src/felucca.c` is the one translation unit that includes
them all.

### Build flags

Every builder item is a `FELUCCA_*` macro (the registry's `flag`): the builder writes them to `build/gen/felucca_config.h`
and docs/BUILDER.md describes each. Defaults for flags no item sets are in `firmware/src/felucca.c` and
`firmware/src/core/registry.h`. `tools/build.py` also reads the environment (`0` or `1`) for flags the configuration does
not set; `tools/optimist.py build` strips `FELUCCA_*` from it, so these are for `sh build.sh` / `tools/build.py`:

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | the update entry and USB rescue (needs `FELUCCA_FLASH`); not a builder item |
| `FELUCCA_USB_AUDIO` / `FELUCCA_CDC` | 1 / `!USB_AUDIO` | USB audio and the serial console share endpoints (both on is a build error); the builder's `USB_CONSOLE` and `USB_AUDIO` set them |
| `FELUCCA_ASM_CHECK` | 0 | verification build: every asm loop also runs the C on a copy (not for release) |
| `FELUCCA_MUL64_CLEAR` | 0 | the asm loops (SVF, PHYS's curved string and sympathetic comb) with no load beside a 64-bit multiply (fm1-base's felucca-bsp docs/CPU.md), the same values, a few instructions more; set with `OPTIMIST_MUL64_CLEAR=1` (either way of building). Optimist as an fm1-base app builds with it |
| `FELUCCA_UNDO_CAP` | 0 | the undo ring at most this many bytes (0 = all the memory left over) |
| `FELUCCA_SAMPLES_SKIP` | empty | built-in sample sets left out, comma-separated (the builder's `SET_*` items set it); a set left out keeps its number (an empty set) |
| `FELUCCA_SAMPLES_CARD` | 0 | 1: SD card mode, every melodic set on the card (none in the image, their presets kept: docs/SD-CARD-DESIGN.md 9) |
| `FELUCCA_DUAL_IDLE` | 0 | the second core spins in RAM between jobs, 1 sleeps (`OPTIMIST_DUAL_IDLE=1` through `optimist.py build`; the wake never reached CPU1 on the FM-1, 2026-10-15); `FELUCCA_BENCH` selects the emulator load scenarios (`docs/DUAL-CORE.md`) |
| `FELUCCA_SIMD` | 0 | keep off: a SIMD build crackles on the real FM-1 (2026-10-12; `firmware/hal/fm1_simd.h`) |

### Package identity

The identity is the name a package and the running firmware give themselves in the update protocol (handshake cmd 0x11)
and the string the `.fwsc` marker bytes carry. **Optimist is `FM-1_700`** for development builds and `FM-1_7(X+2)Y` for
`--release X.Y`: 0.1 is `FM-1_721`, 1.0 `FM-1_730`, up to 4.9 `FM-1_769` (`build.py release_product`). Its update
loader answers `ota-FM-1_700`. `optimist flash` takes only these identities.

- The installers split the identity at `_`: the model before it must be `FM-1` and digits must follow (`web/fm1base/fm1ota.js`,
  `fm1_install.py`). The `.fwsc` marker holds at most 20 characters.
- The stock updater refuses only the identity it is already running; package content is not authenticated. M-VAVE's
  stock is `FM-1_0XX` (V15 is `FM-1_015`), Baud Girl `020`-`09X`, Felucca / SLOOP / X0X `9XX`. 7XX sits between them. In
  use elsewhere (thefirmwarehub.com, 2026-10-14): `FM-1_701`, `710`, `711`, `777` and seven-digit ones, so releases use the
  block 720-769.
- The update loader is Felucca's, with the same protocol: the installers resume a device left in update mode when its
  loader is `ota-FM-1_7XX` or `ota-FM-1_9XX`, and refuse any other (`foreign`). The web installer, `fm1_install.py` and
  `make_site.py` accept both ranges.
- The USB product string is what the editor and installers look for in port names: `FM1-XXXX` (the name since 2026-10-29: four upper-case hex digits, the same four as the Bluetooth name "FM1-XXXX", from the BLE address kept in the settings or else the flash part's unique ID; `firmware/src/system/devname.h`; the manufacturer string stays "Hügelton Instruments"), and for an FM-1 on older firmware `optimist`, `felucca` and `FM-1`.

Not verified on hardware: that the stock V15 updater installs a `FM-1_7XX` package, and that M-VAVE's own updater app
goes back to V15 from one (the same holds for every `9XX` package).

## Samples

The CC0 instrument samples the SAMPLE engine uses are in `assets/samples-cc0/` (Versilian Studios; `ATTRIBUTION.txt`
there); `tools/fetch_cc0.py` downloads them again. Without that folder the build still works and the SAMPLE engine has
only the generated drum kit.

## Tests

```
python tools/optimist.py test             # the builds below, then tests/run_tests.sh, then the release's (fm1base/)
python tools/optimist.py test --in-docker # the same in the toolchain image (Linux; also from Windows)
python tools/optimist.py test --python    # the Python tests only (any host, no build, no C compiler)
```

`test` first makes the builds the tests read, all at once (`JOBS`, default sized to the CPUs and RAM; each in
`build/slots/t-*`): the reference profile, `synths_USBaudio_BTaudio_BTmidi` (the installer, update and rescue tests need a package that fits and its app), a
measurement build of every item (it does not fit the slot; the target-cost check needs `felucca.dis`), two BLE builds
for the emulator tests (`build/ble/`), and a measurement build of each of the builder's check configurations (the drift
check). Then it runs `tests/run_tests.sh`: the C host tests (flash storage, presets, MIDI parser, update entry and
loader, DSP renders, the mix, project formats, the SLICER, the regression suite, the installer, the builder,
`optimist.py`), the Python tests, and with Node.js the web page tests. `--no-build` tests the `build/` there is (for
example after `make build PROFILE=...`). The C tests compile against their own headers (`build/gen-host`, regenerated on
each run with every sample set), so the regression goldens hold whichever profile was built.

`tests/run_tests.sh` runs its tests in parallel: each test with its compile is a unit, `JOBS` of them at once (default
three quarters of the CPUs; `JOBS=1`: one at a time). A unit's output is buffered and printed whole in the script's order,
so the log reads as a serial run's; the slowest (the BLE emulator runs, the builder, the web pages) start first. No test
is timed by the wall clock (the CPU budgets count instructions), so load does not change a result.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a hash of each render with
`tests/golden.txt`; it also checks levels, voices and the CPU cost (`tests/cpu_baseline.txt`,
`tests/target_budget.txt`). After an intended change of the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the
hashes; `BUDGET_UPDATE=1` the cost files. Both run one at a time. FM6 against Dexed, sample by sample:
`DEXED_SRC=<dexed checkout>/Source sh tests/fm6_parity.sh` (also run by `run_tests.sh` when `DEXED_SRC` is set).

### The costs

`tools/builder/costs.json` (the builder's size model) is measured with `measure_costs.py`, which does one measurement
build per item value: ~440 builds for a full run, so it is measured on a fast Linux x86-64 host, not on a laptop
(`tools/farm_size.sh [PROFILE] [KEY=V ...]` sizes one build on such a host over ssh; `FARM_HOST`, default `gpufarm`).
`make costs` measures only what is missing (after adding an item); `make costs-all [JOBS=n]` measures everything again
(JOBS: builds at once; default the CPUs, in Docker the VM's less two, at most 8). Commit `costs.json` and
`web/builder/data/` (`tools/export_builder_web.py`) together. docs/BUILDER.md, "Budget", explains the model and the
checks CI runs against it.

### The emulator checks

```
make emu-check                            # every check on build/felucca.fwsc (the last build)
make emu-check ONLY=boot,upfm6            # some of them (python3 tests/emu/run.py --list)
make emu-check PKG=../other/build/felucca.fwsc   # another build (its ELF beside it)
python tools/optimist.py emu-check [FIRMWARE] [--only C] [--mhz N] [--jobs N]   # the same without make
```

Opt-in, not part of `test` or CI: they need Rust and the emulator (the one `make emu` uses, or `EMU_DIR`, `FM1_EMU`,
`EMU_REPO` naming a local checkout). `tests/emu/drv` is a small driver on the emulator's library, built once into
`build/emu-drv/`; `tests/emu/session.py` keeps it running, so a check plays the panel (keys, knobs, held layers),
records the audio, reads the firmware's own state through the ELF's symbols and reads or seeds the serial NOR. The checks
(`run.py --list`): `boot` (a clean boot: no crash record, the boot guard clears, no late audio half, a note), `autosave`
(an edit autosaves once, a power cycle keeps it), `upfm6` (UP_FM6's voices moved off the SDK VM), `timing`
(`tests/emu_boot_check.py`: no late half at 48, 96 and 192 MHz). Each works in `build/emu-check/<check>/`; they run at
once in processes of their own (`--jobs N`), `timing` alone after them. Exit 1 on any FAIL.

### The gate

`make gate` (`python tools/optimist.py gate`; `tools/gate.py --help`) runs before a push: the builder costs check
(`--missing --check`), every published profile built (`--no-profiles`: the reference profile only), `CONFIG=file` and
`SET="KEY=V ..."` builds if given, the host tests, then the reference profile (`synths_USBaudio_BTaudio_BTmidi`) built again last so `build/` holds its package and
ELF (`EMU=1`: then `make emu-check` on it). Each step logs to `build/gate/`; every step runs even after a failed one.

## Install

The web installer (Chrome or Edge) is at <https://w0ts.github.io/optimist/webapp/installer/>, or on your machine with
`make web`. The `.fwsc` of each release is on the GitHub releases page.

From the build tooling: `make flash` (`python3 tools/optimist.py flash [PACKAGE.fwsc]`), or `f` in the builder menu. With
no package it builds first (a build of the tree as it is, never a stale file; the `==> [3/5] app` lines are the progress);
`--no-build` flashes the newest `build/optimist-*.fwsc` as it is. It shows the identity of the running FM-1 and of the
package, how to recover, and asks (`--yes` does not); it refuses without a build, a package that is not an Optimist one
(stock, SLOOP, Felucca: use `fm1_install.py`), or an FM-1 it cannot find, and never passes `--force`. It runs
`tools/fm1_install.py`, the web installer's update. Needs mido and python-rtmidi (`setup` puts them in the builder venv;
a failed install there only turns `flash` off).

`fm1_install.py` directly (needs `pip3 install mido python-rtmidi`; for stock, SLOOP or other Felucca packages too):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Installing firmware is at your own risk. Back up first and know the way back: README.md, "Getting back".

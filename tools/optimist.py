#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Optimist firmware: one entry point on macOS, Linux and Windows (no sh or make needed; BUILDING.md).

  python tools/optimist.py setup [--check] [--yes]       check and fetch what the build and the emulator need
  python tools/optimist.py builder [--profile P | --config F]   the interactive builder menu
  python tools/optimist.py build [--profile P | --config F] [--set KEY=V ...] [--release X.Y] [--measure]
  python tools/optimist.py package [--profile P | --config F] [--out DIR] [--summary FILE]
  python tools/optimist.py config ...                    the builder without the menu (--list, --budget, --fit,
                                                         --write; tools/builder/configure.py --help)
  python tools/optimist.py cloud-key [PROFILE | FILE] [--commit SHA] [--strict] [--json]
                                                         a cloud build's cache key and the canonical configuration
  python tools/optimist.py flash [PACKAGE.fwsc | --no-build] [--profile P | --config F] [--port NAME] [--yes]
                                                         install a build on an FM-1 over USB-MIDI: builds the
                                                         profile first unless a PACKAGE is given (--no-build: the
                                                         newest build/optimist-*.fwsc as it is); asks first
  python tools/optimist.py emu [FIRMWARE] [--cpu MHZ] [--bg] [--list] [--update]
  python tools/optimist.py web [--port N] [--no-open] [--package F]   the local installer and web editor: builds the site
                                                         from build/felucca.fwsc, serves it on 127.0.0.1:8766, opens the editor
  python tools/optimist.py cpu [SECONDS] [--port P] [--csv F]   the audio load of a real FM-1 (its CDC console)
  python tools/optimist.py test [--python]               the host tests (--python: the Python ones only)
  python tools/optimist.py gate [--config F] [--set K=V] [--emu]   the integrator's gate (tools/gate.py --help)
  python tools/optimist.py emu-check [FIRMWARE] [--only C]   the emulator smoke checks (tests/emu/run.py --help)
  python tools/optimist.py toolchain                     which toolchain and SDK a build would use

build, package and test take --in-docker: the whole command runs inside the toolchain image (a Linux x86-64
container), the same as a Linux host or CI. Exit status 0 = done, 1 = failed, 2 = usage or configuration error.

Non-interactive use (scripts, CI): write a .config (KEY=value lines, tools/builder/registry.py
keys), then `package --config my.config --out DIR --summary result.json`.
"""
import argparse
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "builder"))
import configure as C
import deps
import toolchain as TC

DEFAULT_PROFILE = C.DEFAULT_PROFILE            # Optimist Synth: what build, builder and make use without a profile
REFERENCE_PROFILE = C.REFERENCE_PROFILE        # user-default: the generic profile the host tests, the gate and the emulator checks build
PY_TESTS = ("tests/builder_test.py", "tests/builder_core_test.py", "tests/builder_flash_guard_test.py",
            "tests/builder_progress_test.py", "tests/optimist_cli_test.py", "tests/install_test.py",
            "tests/rescue_test.py", "tests/rescue_any_test.py",
            "tests/icons_test.py", "tests/fm1_cpu_test.py", "tests/ble_rf_capture_test.py",
            "tests/emu_driver_once_test.py", "tools/doc_coverage.py")


# ---- the configuration from the command line

def add_config_args(p):
    g = p.add_mutually_exclusive_group()
    g.add_argument("--profile", help=f"a profile from config/profiles (default {DEFAULT_PROFILE}): "
                   + ", ".join(C.profile_names()))
    g.add_argument("--config", help="a .config file (the menu's save, or written by a script)")
    g.add_argument("--defaults", action="store_true", help="every registry default (what the host tests expect)")
    p.add_argument("--set", action="append", metavar="KEY=V", help="change one item (repeatable)")
    p.add_argument("--name", help="the configuration's name (BUILD SysEx, package name)")
    p.add_argument("--ble-drop", metavar="ITEM", help=C.BLE_DROP_HELP)


def load_config(a):
    """-> (cfg, name); configure.ConfigError for a bad profile, file or --set"""
    defaults = getattr(a, "defaults", False)
    ns = argparse.Namespace(config=a.config, set=a.set, name=a.name, ble_drop=getattr(a, "ble_drop", None),
                            profile=a.profile or (None if a.config or defaults else DEFAULT_PROFILE))
    try:
        return C.resolve_cli(ns)
    except FileNotFoundError as e:
        raise C.ConfigError(f"{e.filename}: no such file")


# ---- commands

def cmd_setup(a):
    import setup_env
    return setup_env.run(fetch=not a.check, yes=a.yes, emulator=not a.no_emu)


def cmd_toolchain(_a):
    try:
        print(f"toolchain  {TC.resolve().describe()}")
        ok = True
    except TC.ToolchainError as e:
        print(f"toolchain  none: {e}")
        ok = False
    sdk = TC.sdk_dir()
    missing = TC.sdk_missing(sdk)
    print(f"SDK files  {sdk}" + (f" (missing {', '.join(missing)})" if missing else ""))
    print(f"Pillow     {'yes' if TC.have_module('PIL') else 'no'} ({sys.executable}); build Python "
          f"{C.build_python()}")
    return 0 if ok and not missing else 1


def cmd_builder(a):
    if not deps.venv_ready():
        print("builder: first run, making the venv (Textual, Pillow)")
        try:
            deps.make_venv()
        except deps.FetchError as e:
            print(f"builder: {e}", file=sys.stderr)
            return 1
    args = [str(deps.venv_python()), str(HERE / "builder" / "menu.py")]
    if a.config:
        args += ["--config", a.config]
    elif a.profile:
        args += ["--profile", a.profile]
    return run_or_exec(args)


def run_or_exec(args):
    sys.stdout.flush()
    if os.name == "nt":
        return subprocess.call(args)
    os.execv(args[0], args)


def cmd_build(a):
    cfg, name = load_config(a)
    started = time.time()
    extra = ["--release", a.release] if a.release else []
    ok, sizes, _ = C.build(cfg, name, measure=a.measure, extra=extra, echo=True)
    if sizes:
        print("exact:   " + ", ".join(f"{r} {sizes[r]:,} of {C.LIMITS[r]:,}" for r in C.REGIONS))
    C._summary(a.summary, {"ok": ok, "name": name, "hash": f"{C.cfg_hash(cfg):08x}", "sizes": sizes,
                           "fwsc": str(named_package(started)) if ok and not a.measure else None})
    return 0 if ok else 1


def named_package(since=0.0):
    """the package build.py just wrote under its user-facing name (build/optimist-<version>...fwsc), else
    build/felucca.fwsc (the same bytes under the internal name)"""
    named = sorted((p for p in (ROOT / "build").glob("optimist-*.fwsc") if p.stat().st_mtime >= since - 1),
                   key=lambda p: p.stat().st_mtime)
    return named[-1] if named else ROOT / "build" / "felucca.fwsc"


def cmd_package(a):
    cfg, name = load_config(a)
    return C.package(cfg, name, Path(a.out).expanduser(), echo=True, summary=a.summary)


def cmd_config(a):
    return C.main(a.rest)


def cmd_cloud_key(a):
    """the cache key of a cloud build (core.cloud_key: the commit and the canonical form) and the canonical form"""
    path = Path(a.what or DEFAULT_PROFILE).expanduser()
    if not path.is_file():
        path = C.profile_path(a.what or DEFAULT_PROFILE)
        if path is None:
            raise C.ConfigError(f"{a.what}: neither a .config file nor a profile ({', '.join(C.profile_names())})")
    commit = a.commit or subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True,
                                        text=True).stdout.strip()
    ev = C.evaluate({"text": path.read_text(), "commit": commit, "strict": a.strict,
                     "costs": C.load_costs() if a.json else None})
    for w in ev.get("parse_warnings", []):
        print(f"warning: {w}", file=sys.stderr)
    if ev["error"]:
        raise C.ConfigError(ev["error"])
    if a.json:
        print(json.dumps(ev, indent=1))
    else:
        print(f"# key {ev['key']}\n# commit {commit}\n# config {path}\n{ev['canonical']}", end="")
    for e in ev["errors"]:
        print(f"ERROR   {e['text']}", file=sys.stderr)
    return 1 if ev["errors"] else 0


def build_for_flash(a):
    """flash without a package builds the chosen configuration first (the stage lines are its progress) -> the
    package it made, or None (the build failed, or the configuration is wrong: said, nothing to flash)"""
    try:
        cfg, name = load_config(a)
    except C.ConfigError as e:
        print(f"flash: {e}", file=sys.stderr)
        return None
    print(f"flash: building {name} first (--no-build flashes the newest build/optimist-*.fwsc as it is)", flush=True)
    started = time.time()
    ok, _, _ = C.build(cfg, name, echo=True)
    if not ok:
        print("flash: the build failed; nothing was flashed", file=sys.stderr)
        return None
    return named_package(started)


def cmd_flash(a):
    """install a build on the connected FM-1 (tools/flash.py -> tools/fm1_install.py): shows the running identity and
    the package's, asks (unless --yes), never --force. With no PACKAGE it builds the chosen configuration first
    (a build of the tree as it is now, not whatever is newest in build/); --no-build uses the newest file instead"""
    import flash
    if a.package:
        pkg = Path(a.package).expanduser()
    elif a.no_build:
        pkg = flash.last_package()
    else:
        pkg = build_for_flash(a)
        if pkg is None:
            return 1
    try:
        plan = flash.preflight(pkg, a.port)
        print("\n".join(plan.lines()))
        if not a.yes:
            try:
                go = input("Flash it? [y/N] ").strip().lower() in ("y", "yes")
            except EOFError:
                go = False
            if not go:
                print("cancelled; nothing was written")
                return 1
        return flash.install(plan, capture=False)[0]
    except flash.FlashError as e:
        print(f"flash: {e}", file=sys.stderr)
        return e.code


def cmd_costs(a):
    sys.path.insert(0, str(ROOT / "tools" / "builder"))
    import measure_costs
    return measure_costs.main(a.rest or ["--missing"])


def cmd_emu(a):
    import emu
    return emu.main(a.rest)


WEB_PORT = 8766
WEB_PKG = ROOT / "build" / "felucca.fwsc"
WEB_SITE = ROOT / "build" / "optimist-site"


def web_urls(port):
    base = f"http://localhost:{port}/webapp"
    return f"{base}/installer/", f"{base}/editor/"


def web_open(url):
    """Open url in the default browser: macOS `open`, Linux `xdg-open`, Windows startfile. True if it was tried."""
    try:
        if sys.platform == "darwin":
            subprocess.Popen(["open", url])
        elif sys.platform.startswith("win"):
            os.startfile(url)
        elif shutil.which("xdg-open"):
            subprocess.Popen(["xdg-open", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            return False
    except OSError:
        return False
    return True


def cmd_web(a):
    """Build the local site (web/make_site.py) from a package and serve it on 127.0.0.1 until Ctrl-C."""
    import functools
    import http.server
    pkg = Path(a.package) if a.package else WEB_PKG
    if not pkg.is_file():
        print(f"optimist: no package at {pkg}. Build one first: `make build` (or `python tools/optimist.py build`), "
              "or pass --package FILE.fwsc", file=sys.stderr)
        return 1
    sys.path.insert(0, str(ROOT / "web"))
    import make_site
    site = Path(a.site) if a.site else WEB_SITE
    try:
        make_site.main(pkg, "dev", site)
    except SystemExit as e:
        print(f"optimist: the site was not built: {e}", file=sys.stderr)
        return 1
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(site))
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), handler)
    except OSError as e:
        print(f"optimist: cannot listen on 127.0.0.1:{a.port}: {e} (another port: --port N or make web PORT=N)",
              file=sys.stderr)
        return 1
    installer, editor = web_urls(a.port)
    print(f"installer: {installer}\neditor:    {editor}\nbuilder:   {installer.replace('/installer/', '/builder/')}\n"
          "Web MIDI needs Chrome or Edge (Safari and Firefox do not have it). Ctrl-C stops the server.", flush=True)
    if not a.no_open and not web_open(editor):
        print("(could not open a browser: open the editor URL yourself)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        srv.server_close()
    return 0


def cmd_gate(a):
    import gate
    return gate.main(a.rest)


def cmd_emu_check(a):
    return subprocess.call([sys.executable, str(ROOT / "tests" / "emu" / "run.py"), *a.rest], cwd=ROOT)


def cmd_cpu(a):
    import fm1_cpu
    return fm1_cpu.main(a.rest)


BLE_FULL_SET = ("BLE=1", "USB_MODE=1", "BLE_BOND=1", "BLE_CENTRAL=1", "BLE_DIAG=1")   # (the FM-1 build)
# items the BLE test packages do not exercise, out of them so that BLE_DIAG and the whole option set fit beside
# user-default's store and the flash the queued branches add (the user, 2026-10-10: "remove anything you want from
# ble test"); the shipped profiles keep all of them. The tests (ble_emu_test.py, ble_emu_central_test.py,
# ble_emu_boot_test.py) run the BLE link, BLE-MIDI both ways (a panel key out, a note in playing the default
# synth), the DEVICES list, bonding and boot; none of them touches:
#  - the automation store (CHANCE, MOTION, SL24_XSTEP, MICRO, FILLS, PLOCK)
#  - the synth engines GRAIN, TRIO, DRAWBAR, DIGITAL, PHASE (the default patch plays none of them)
#  - the SLOOP 2.4 project importer and its safety checks (SL24_IMPORT, SL24_SAFE)
#  - the step conditions (COND, 11.4 KB: on in user-default since 2026-10-13 with SEQTOOLS; the full BLE set was 432 B
#    over the slot after feat/lfo-slots; SEQTOOLS would give nothing here, it is UI=1 only and user-default is UI=0);
#    tests/cond_test.c and the UI tests cover COND in their own builds
# measured 2026-10-11 on user-default with the whole BLE set: flash 560,596 of 581,564 B, 20,968 B free
BLE_TEST_ROOM = ("CHANCE=0", "MOTION=0", "SL24_XSTEP=0", "MICRO=0", "FILLS=0", "PLOCK=0",
                 "ENG_GRAIN=0", "ENG_TRIO=0", "ENG_DRAWBAR=0", "ENG_DIGITAL=0", "ENG_PHASE=0",
                 "SL24_IMPORT=0", "SL24_SAFE=0", "COND=0", "INS_FUZZ=0", "ENERGY=0", "NATIVE_BANKS=0")


def ble_test_configs():
    """-> [(name, cfg)] the two BLE builds prepare_tests makes (the emulator tests' packages): user-default with BLE,
    BLE_DIAG, BLE_CENTRAL (FLUTE dropped where it must) and with the whole option set. Also check configurations of
    the size estimate (tools/builder/measure_costs.py check_configs), so they are anchors with a measured build"""
    def load(sets, ble_drop=None):
        with contextlib.redirect_stdout(io.StringIO()):
            return load_config(argparse.Namespace(profile=REFERENCE_PROFILE, config=None, defaults=False, set=list(sets),
                                                  name=None, ble_drop=ble_drop))[0]
    return [(f"{REFERENCE_PROFILE}+ble-test", load(["BLE=1", "BLE_DIAG=1", "BLE_CENTRAL=1", *BLE_TEST_ROOM], ble_drop="FLUTE")),
            (f"{REFERENCE_PROFILE}+ble-full", load([*BLE_FULL_SET, *BLE_TEST_ROOM, "PATTERNS=0", "ED_FXP=0", "ED_SCRS=0", "ED_TOOLS=0", "ED_SONG=0", "ED_SOLO=0", "ED_REC=0", "ED_UNDO=0", "DUAL=0"]))]


def prepare_tests():
    """build/ as the host tests read it -> 0 ok. First two BLE builds (build/ble/felucca-ble.fwsc for the emulator
    tests tests/ble_emu_test.py and ble_emu_central_test.py; build/ble/felucca-ble-full.fwsc, the whole BLE option
    set, for tests/ble_emu_boot_test.py), then two builds: the target-cost check holds the loops of the
    default configuration (every item: too big for the slot, so a measurement build: felucca.dis), the
    installer, update and rescue tests a package that fits and its app (user-default: felucca.fwsc,
    felucca.bin, loader/ota.bin). The regression goldens need neither: tests/run_tests.sh renders against
    build/gen-host, every sample set whatever the profile (tools/build.py --host-headers)"""
    check_builds()
    print(f"test: building {REFERENCE_PROFILE} with BLE, BLE_DIAG and BLE_CENTRAL (the package tests/ble_emu_test.py "
          "runs in the emulator, which reads the diagnostics block and the DEVICES list's scan table; the builder's "
          "own --ble-drop FLUTE makes room where it must; the radio's tables: the committed ones, "
          "or a capture from FM1_STOCK_FWSC)")
    (_, ble_cfg), (_, full_cfg) = ble_test_configs()
    cfg, name = C.load_profile(REFERENCE_PROFILE)
    ok, _, _ = C.build(ble_cfg, name + " ble", echo=True)
    if not ok:
        print("test: the BLE build failed", file=sys.stderr)
        return 1
    ble = ROOT / "build" / "ble"
    ble.mkdir(exist_ok=True)
    shutil.copy(ROOT / "build" / "felucca.fwsc", ble / "felucca-ble.fwsc")
    shutil.copy(ROOT / "build" / "felucca.elf", ble / "felucca-ble.elf")     # (its symbols: the blell RAM block)
    print(f"test: building {REFERENCE_PROFILE} with the whole BLE option set, as it goes on the FM-1 (BLE, USB_MODE, "
          "BLE_BOND, BLE_CENTRAL, BLE_DIAG: the package tests/ble_emu_boot_test.py starts with BLUETOOTH saved ON)")
    # (ED_FXP=0 and ED_SCRS=0 too since 2026-10-16, batch 163: the web editor's two large commands, no BLE test uses them; 836 B over the slot.)
    # (PATTERNS=0 in this set since 2026-10-15, batch 141 (batch 140 had FM6_SYSEX=0 for a 40 B overflow): the batches' growth
    # (fx-rework, JUNO, env-dests) put the whole set 2,600 B over the slot (ld: region XIP overflowed); --ble-drop FLUTE frees
    # nothing here (user-default has it off). The patterns and scenes (about 5.6 KB) are not used by any BLE test.
    # Before that nothing was dropped: 2,720 B free 2026-10-10, 20,968 B with BLE_TEST_ROOM's later additions)
    ok, _, _ = C.build(full_cfg, name + " ble-full", echo=True)
    if not ok:
        print("test: the full BLE build failed", file=sys.stderr)
        return 1
    shutil.copy(ROOT / "build" / "felucca.fwsc", ble / "felucca-ble-full.fwsc")
    shutil.copy(ROOT / "build" / "felucca.elf", ble / "felucca-ble-full.elf")
    print(f"test: building {REFERENCE_PROFILE} (the package and app the installer and rescue tests read)")
    ok, _, _ = C.build(cfg, name, echo=True)
    app = ROOT / "build" / "felucca.bin"
    if not ok or not app.exists():
        print("test: the build failed", file=sys.stderr)
        return 1
    image = app.read_bytes()
    print("test: a measurement build of the default configuration (the render loops of every item)")
    ok, _, _ = C.build(C.defaults(), "default", measure=True, echo=True)
    app.write_bytes(image)          # (the measurement build's app does not fit; the package's does)
    if not ok:
        print("test: the measurement build failed", file=sys.stderr)
        return 1
    return 0


def check_builds():
    """measurement builds of the builder's check configurations (every profile, tools/builder/estimate/*.config) not
    built from this source yet: tests/builder_test.py holds the size estimate against them (its drift check), so a
    costs.json the firmware moved away from fails the normal test run. ~15 s each; made first, so the builds the
    tests read (the packages, the defaults' felucca.dis) come after"""
    import measure_costs as MC
    ble = dict(ble_test_configs())                      # (built below anyway, as the packages)
    todo = [(n, cfg) for n, cfg in MC.check_configs().items() if n not in ble and not C.exact_sizes(cfg)]
    print(f"test: measurement builds of {len(todo)} builder check configurations (the estimate's drift check)")
    for n, cfg in todo:
        ok, _, _ = C.build(cfg, f"m-chk-{n}"[:16], measure=True)   # (measure_costs.py measure_checks: the same name, the same bytes)
        if not ok:
            print(f"test: the measurement build of check configuration {n} failed (the drift check notes it)")


def cmd_test(a):
    py = [sys.executable] if TC.have_module("PIL") else [C.build_python()]
    env = dict(os.environ, AC79_SDK=str(TC.sdk_dir()))      # (the rescue test reads the SDK's uboot.boot)
    if not a.python:
        # the host tests read build/: a package, app and loader that fit (the installer, update and rescue tests)
        # and the render loops of the default configuration, which holds every item (the target cost; it is
        # too big to link). The goldens render against build/gen-host on any profile. --no-build: test the
        # build/ there is (any profile that links)
        if not a.no_build and prepare_tests():
            return 1
        if not (ROOT / "build" / "felucca.fwsc").exists():
            print("test: no build/felucca.fwsc (python tools/optimist.py build)", file=sys.stderr)
            return 1
        sh = shutil.which("sh") if os.name != "nt" else None     # (Git for Windows' sh: untested; use --in-docker)
        if sh:
            sys.stdout.flush()
            return subprocess.call([sh, "tests/run_tests.sh"], cwd=ROOT, env=env)
        print("test: no sh here: the C host tests need sh and a C compiler (WSL, or test --in-docker); running "
              "the Python tests only")
    fails = 0
    for t in PY_TESTS:
        print(f"== {t}", flush=True)
        fails += subprocess.call([*py, t], cwd=ROOT, env=env) != 0
    print("PYTHON TESTS PASSED" if not fails else f"PYTHON TESTS FAILED ({fails})")
    return 1 if fails else 0


# ---- --in-docker: the same command in the toolchain image

def in_docker(argv):
    image = TC.image_name()
    if not TC.docker_running():
        print("optimist: --in-docker needs Docker running", file=sys.stderr)
        return 1
    if not TC.image_present(image):
        print(f"optimist: no image {image}: python tools/optimist.py setup builds it", file=sys.stderr)
        return 1
    rest = [x for x in argv if x != "--in-docker"]
    tty = ["-t"] if sys.stdout.isatty() else []
    # (git in the container: the mounted tree belongs to another user; the -ui.zip's SOURCE.txt names the commit)
    git_ok = ["-e", "GIT_CONFIG_COUNT=1", "-e", "GIT_CONFIG_KEY_0=safe.directory", "-e", "GIT_CONFIG_VALUE_0=*"]
    cmd = ["docker", "run", "--rm", *tty, "--platform", "linux/amd64", *TC.linux_user(), *git_ok,
           "-e", "HOME=/tmp", "-v", f"{ROOT}:/work", "-w", "/work", image, "python3", "tools/optimist.py", *rest]
    return subprocess.call(cmd)


def parser():
    ap = argparse.ArgumentParser(prog="optimist.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True, metavar="COMMAND")
    p = sub.add_parser("setup", help="check and fetch what the build and the emulator need")
    p.add_argument("--check", action="store_true", help="check only, fetch nothing")
    p.add_argument("--yes", action="store_true", help="fetch without asking")
    p.add_argument("--no-emu", action="store_true", help="skip the emulator's prerequisites (git, Rust)")
    p.set_defaults(fn=cmd_setup)
    sub.add_parser("toolchain", help="which toolchain and SDK a build would use").set_defaults(fn=cmd_toolchain)
    p = sub.add_parser("builder", help="the interactive builder menu (Textual)")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--profile")
    g.add_argument("--config")
    p.set_defaults(fn=cmd_builder)
    p = sub.add_parser("build", help="build a configuration: build/felucca.fwsc and its -ui.zip")
    add_config_args(p)
    p.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_7(X+2)Y, version X.Y")
    p.add_argument("--measure", action="store_true", help="measurement build (links past the slot, no package)")
    p.add_argument("--summary", metavar="FILE", help="write the result as JSON")
    p.add_argument("--in-docker", action="store_true", help="run inside the toolchain image")
    p.set_defaults(fn=cmd_build)
    p = sub.add_parser("package", help="build, then copy optimist-<name>-<date>.fwsc + -ui.zip to --out")
    add_config_args(p)
    p.add_argument("--out", default=str(ROOT / "firmwares"), help="where the package goes (default firmwares/)")
    p.add_argument("--summary", metavar="FILE", help="write the result as JSON")
    p.add_argument("--in-docker", action="store_true", help="run inside the toolchain image")
    p.set_defaults(fn=cmd_package)
    p = sub.add_parser("flash", help="install a build on an FM-1 over USB-MIDI (builds first unless a PACKAGE is "
                       "given; fm1_install.py; asks first)")
    p.add_argument("package", nargs="?", help="PACKAGE.fwsc (default: build the chosen configuration, then flash it)")
    p.add_argument("--no-build", action="store_true",
                   help="no PACKAGE and no build: flash the newest build/optimist-*.fwsc as it is")
    add_config_args(p)                                   # (what to build when there is no PACKAGE)
    p.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    p.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    p.set_defaults(fn=cmd_flash)
    p = sub.add_parser("config", help="the builder without the menu (configure.py arguments)", add_help=False)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_config)
    p = sub.add_parser("cloud-key", help="the cache key of a cloud build and the canonical configuration it hashes")
    p.add_argument("what", nargs="?", help=f"a .config file or a profile (default {DEFAULT_PROFILE})")
    p.add_argument("--commit", help="the full commit SHA the build is made from (default: HEAD)")
    p.add_argument("--strict", action="store_true", help="an unknown key is an error (a cloud build request)")
    p.add_argument("--json", action="store_true", help="the whole evaluation (core.evaluate) as JSON")
    p.set_defaults(fn=cmd_cloud_key)
    p = sub.add_parser("costs", help="measure the items tools/builder/costs.json lacks (measure_costs.py --missing; "
                                     "or its own arguments)", add_help=False)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_costs)
    p = sub.add_parser("emu", help="run a firmware in the emulator (emu --help)", add_help=False)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_emu)
    p = sub.add_parser("web", help="build the local site from build/felucca.fwsc and serve the installer and the "
                                   "web editor on 127.0.0.1")
    p.add_argument("--port", type=int, default=WEB_PORT, help=f"the port (default {WEB_PORT})")
    p.add_argument("--package", help="the .fwsc to build the site from (default build/felucca.fwsc)")
    p.add_argument("--site", help="where to write the site (default build/optimist-site)")
    p.add_argument("--no-open", action="store_true", help="do not open the editor in a browser")
    p.set_defaults(fn=cmd_web)
    p = sub.add_parser("cpu", help="the audio load of a running FM-1 from its serial console (cpu --help)",
                       add_help=False)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_cpu)
    for n, fn, h in (("gate", cmd_gate, "the integrator's gate: costs, the profiles, the host tests, a final "
                      "user-default build (gate --help)"),
                     ("emu-check", cmd_emu_check, "the emulator smoke checks on a build (emu-check --help)")):
        p = sub.add_parser(n, help=h, add_help=False)
        p.add_argument("rest", nargs=argparse.REMAINDER)
        p.set_defaults(fn=fn)
    p = sub.add_parser("test", help="the host tests (builds the default configuration first)")
    p.add_argument("--python", action="store_true", help="the Python tests only (any host, no C compiler)")
    p.add_argument("--no-build", action="store_true", help="test the build/ there is (any profile that links)")
    p.add_argument("--in-docker", action="store_true", help="run inside the toolchain image")
    p.set_defaults(fn=cmd_test)
    return ap


PASSTHROUGH = {"config": cmd_config, "emu": cmd_emu, "cpu": cmd_cpu, "gate": cmd_gate, "emu-check": cmd_emu_check}


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    if argv and argv[0] in PASSTHROUGH:     # (their own parsers: argparse would take their options for ours)
        return PASSTHROUGH[argv[0]](argparse.Namespace(rest=argv[1:]))
    a = parser().parse_args(argv)
    if getattr(a, "in_docker", False) and not os.environ.get("OPTIMIST_IN_CONTAINER"):
        return in_docker(argv)
    try:
        return a.fn(a)
    except C.ConfigError as e:
        print(f"optimist: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())

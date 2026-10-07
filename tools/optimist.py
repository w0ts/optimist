#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Optimist firmware: one entry point on macOS, Linux and Windows (no sh or make needed; BUILDING.md).

  python tools/optimist.py setup [--check] [--yes]       check and fetch what the build and the emulator need
  python tools/optimist.py builder [--profile P | --config F]   the interactive builder menu
  python tools/optimist.py build [--profile P | --config F] [--set KEY=V ...] [--release X.Y] [--measure]
  python tools/optimist.py package [--profile P | --config F] [--out DIR] [--summary FILE]
  python tools/optimist.py config ...                    the builder without the menu (--list, --budget, --fit,
                                                         --write; tools/builder/configure.py --help)
  python tools/optimist.py emu [FIRMWARE] [--cpu MHZ] [--bg] [--list] [--update]
  python tools/optimist.py cpu [SECONDS] [--port P] [--csv F]   the audio load of a real FM-1 (its CDC console)
  python tools/optimist.py test [--python]               the host tests (--python: the Python ones only)
  python tools/optimist.py toolchain                     which toolchain and SDK a build would use

build, package and test take --in-docker: the whole command runs inside the toolchain image (a Linux x86-64
container), the same as a Linux host or CI. Exit status 0 = done, 1 = failed, 2 = usage or configuration error.

Non-interactive use (scripts, CI): write a .config (KEY=value lines, tools/builder/registry.py
keys), then `package --config my.config --out DIR --summary result.json`.
"""
import argparse
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

DEFAULT_PROFILE = "user-default"
PY_TESTS = ("tests/builder_test.py", "tests/optimist_cli_test.py", "tests/install_test.py", "tests/rescue_test.py",
            "tests/icons_test.py", "tests/fm1_cpu_test.py")


# ---- the configuration from the command line

def add_config_args(p):
    g = p.add_mutually_exclusive_group()
    g.add_argument("--profile", help=f"a profile from config/profiles (default {DEFAULT_PROFILE}): "
                   + ", ".join(C.profile_names()))
    g.add_argument("--config", help="a .config file (the menu's save, or written by a script)")
    g.add_argument("--defaults", action="store_true", help="every registry default (what the host tests expect)")
    p.add_argument("--set", action="append", metavar="KEY=V", help="change one item (repeatable)")
    p.add_argument("--name", help="the configuration's name (BUILD SysEx, package name)")


def load_config(a):
    """-> (cfg, name); configure.ConfigError for a bad profile, file or --set"""
    defaults = getattr(a, "defaults", False)
    ns = argparse.Namespace(config=a.config, set=a.set, name=a.name,
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


def cmd_emu(a):
    import emu
    return emu.main(a.rest)


def cmd_cpu(a):
    import fm1_cpu
    return fm1_cpu.main(a.rest)


def prepare_tests():
    """build/ as the host tests read it -> 0 ok. Two builds: the target-cost check holds the loops of the
    default configuration (every item: too big for the slot, so a measurement build: felucca.dis), the
    installer, update and rescue tests a package that fits and its app (user-default: felucca.fwsc,
    felucca.bin, loader/ota.bin). The regression goldens need neither: tests/run_tests.sh renders against
    build/gen-host, every sample set whatever the profile (tools/build.py --host-headers)"""
    print(f"test: building {DEFAULT_PROFILE} (the package and app the installer and rescue tests read)")
    cfg, name = C.load_profile(DEFAULT_PROFILE)
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
    p.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_7XY, version X.Y")
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
    p = sub.add_parser("config", help="the builder without the menu (configure.py arguments)", add_help=False)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_config)
    p = sub.add_parser("emu", help="run a firmware in the emulator (emu --help)", add_help=False)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_emu)
    p = sub.add_parser("cpu", help="the audio load of a running FM-1 from its serial console (cpu --help)",
                       add_help=False)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_cpu)
    p = sub.add_parser("test", help="the host tests (builds the default configuration first)")
    p.add_argument("--python", action="store_true", help="the Python tests only (any host, no C compiler)")
    p.add_argument("--no-build", action="store_true", help="test the build/ there is (any profile that links)")
    p.add_argument("--in-docker", action="store_true", help="run inside the toolchain image")
    p.set_defaults(fn=cmd_test)
    return ap


PASSTHROUGH = {"config": cmd_config, "emu": cmd_emu, "cpu": cmd_cpu}


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

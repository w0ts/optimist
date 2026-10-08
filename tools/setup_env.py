# SPDX-License-Identifier: GPL-3.0-only
"""python tools/optimist.py setup: check what building and the emulator need on this host, fetch what this tree
can fetch (the builder's venv, the SDK files, the toolchain or its Docker image), say how to get the rest.

  setup            check, then fetch what is missing (asks first; --yes does not ask)
  setup --check    check only
"""
import platform
import shutil
import subprocess
import sys

import deps
import toolchain as TC

SYSTEM = platform.system()          # Linux, Darwin, Windows
# what `cargo build --release --features gui` of the emulator needs on Debian / Ubuntu (measured in rust:1-bookworm,
# 2026-10-06: cpal's alsa-sys asks pkg-config for ALSA; fm1-ui links only libasound, while eframe loads X11 or
# Wayland, xkbcommon and OpenGL at run time, which a desktop has; BUILDING.md "Emulator")
LINUX_GUI_PACKAGES = ("pkg-config", "libasound2-dev")


MARK = {True: "ok ", False: "-- ", None: " ? "}     # None: not checked on this host


class Report:
    def __init__(self):
        self.rows, self.todo = [], []

    def add(self, what, ok, detail, fix=None):
        self.rows.append((what, ok, detail))
        if not ok and fix:
            self.todo.append(fix)

    def show(self):
        for what, ok, detail in self.rows:
            print(f"  {MARK[ok]} {what:18s} {detail}")
        return not any(ok is False for _, ok, _ in self.rows)


def confirm(question, yes):
    if yes:
        return True
    if not sys.stdin.isatty():
        print(f"setup: {question} (not asked without a terminal: --yes)")
        return False
    return input(f"setup: {question} [Y/n] ").strip().lower() in ("", "y", "yes")


def check_python(rep):
    ok = sys.version_info >= (3, 9)
    rep.add("Python", ok, f"{platform.python_version()} ({sys.executable})", "Python 3.9 or newer: python.org")


def check_venv(rep, fetch, yes):
    v = deps.venv_dir()
    if not deps.venv_ready() and fetch and confirm(f"make the builder's venv {v} (Textual, Pillow)?", yes):
        try:
            deps.make_venv()
        except deps.FetchError as e:
            print(f"setup: {e}")
    rep.add("builder venv", deps.venv_ready(), str(v), "python tools/optimist.py setup (makes the venv)")


def check_sdk(rep, fetch, yes):
    root = TC.sdk_dir()
    if TC.sdk_missing(root) and fetch and confirm(f"fetch the three AC79 SDK files into {root}?", yes):
        try:
            deps.fetch_sdk(root)
        except deps.FetchError as e:
            print(f"setup: {e}")
    missing = TC.sdk_missing(root)
    rep.add("AC79 SDK files", not missing, f"{root}" + (f" (missing {', '.join(missing)})" if missing else ""),
            "python tools/optimist.py setup (fetches the SDK files; into ./sdk; AC79_SDK=<dir> to put them elsewhere)")


def fetch_toolchain(fetch, yes):
    """what setup can do when no backend resolves: native download, or build the Docker image"""
    if not fetch:
        return
    try:
        if TC.is_native_host():
            if confirm("download the JieLi toolchain (26 MB) into ~/.jieli?", yes):
                deps.fetch_toolchain()
        elif TC.docker_running():
            if confirm(f"build the Docker image {TC.image_name()} (downloads the toolchain inside; a few "
                       "minutes)?", yes):
                deps.build_image()
    except deps.FetchError as e:
        print(f"setup: {e}")


def check_toolchain(rep, fetch, yes):
    try:
        b = TC.resolve()
    except TC.ToolchainError:
        fetch_toolchain(fetch, yes)
        try:
            b = TC.resolve()
        except TC.ToolchainError as e:
            rep.add("toolchain", False, str(e), toolchain_fix())
            return
    rep.add("toolchain", True, b.describe())


def toolchain_fix():
    if TC.is_native_host():
        return "python tools/optimist.py setup --yes (downloads the toolchain into ~/.jieli)"
    docker = {"Darwin": "Docker Desktop or Rancher Desktop (dockerd), started",
              "Windows": "Docker Desktop (WSL 2 backend), started; or a WSL distribution (BUILDING.md, Windows)",
              }.get(SYSTEM, "Docker Engine (docs.docker.com/engine/install), your user in the docker group")
    return f"{docker}; then python tools/optimist.py setup --yes (builds the toolchain image)"


def check_emulator(rep):
    rep.add("git", bool(shutil.which("git")), shutil.which("git") or "not found",
            "git (git-scm.com): clones the emulator")
    cargo = shutil.which("cargo")
    rep.add("Rust (cargo)", bool(cargo), cargo or "not found", "Rust from rustup.rs (builds the emulator)")
    if SYSTEM == "Linux":
        missing = [p for p in LINUX_GUI_PACKAGES if not _deb_installed(p)] if shutil.which("dpkg-query") else None
        if missing is None:
            rep.add("emulator libs", None, "not checked (no dpkg): ALSA, X11 / Wayland, xkbcommon, OpenGL dev files")
        else:
            rep.add("emulator libs", not missing, "the emulator's GUI and audio libraries" +
                    (f" (missing {' '.join(missing)})" if missing else ""),
                    "sudo apt install " + " ".join(missing or ()))


def _deb_installed(pkg):
    p = subprocess.run(["dpkg-query", "-W", "-f=${Status}", pkg], capture_output=True, text=True)
    return p.returncode == 0 and "install ok installed" in p.stdout


def check_host_tests(rep):
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    sh = shutil.which("sh")
    rep.add("host tests", None if SYSTEM == "Windows" else bool(cc and sh),
            f"C compiler {cc or 'not found'}, sh {sh or 'not found'}" +
            (" (on Windows: python tools/optimist.py test --in-docker, or WSL)" if SYSTEM == "Windows" else ""),
            "a C compiler (Xcode command line tools: xcode-select --install; Debian: apt install build-essential)")


def run(fetch=True, yes=False, emulator=True):
    print(f"setup: {SYSTEM} {platform.machine()}, Python {platform.python_version()}")
    rep = Report()
    check_python(rep)
    check_venv(rep, fetch, yes)
    check_sdk(rep, fetch, yes)
    check_toolchain(rep, fetch, yes)
    if emulator:
        check_emulator(rep)
    check_host_tests(rep)
    print()
    ok = rep.show()
    print()
    if rep.todo:
        print("To do:")
        for t in rep.todo:
            print(f"  - {t}")
        print()
    print("Next:" if ok else "Then:")
    print("  python tools/optimist.py builder      pick features, build (build/optimist-<version>-*.fwsc)")
    print("  python tools/optimist.py emu          run a firmware in the emulator")
    print("  python tools/optimist.py --help       everything else")
    return 0 if ok else 1

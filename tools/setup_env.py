# SPDX-License-Identifier: GPL-3.0-only
"""python tools/optimist.py setup: check what building and the emulator need on this host, fetch what this tree
can fetch (the builder's venv, the SDK files, the pi32v2-llvm toolchain), say how to get the rest.

  setup            check, then fetch what is missing (asks first; --yes does not ask)
  setup --check    check only
"""
import platform
import shutil
import subprocess
import sys

import deps
import repo_root
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
    if not (deps.venv_ready() and deps.venv_has_midi()) and fetch and \
            confirm(f"make the builder's venv {v} (Textual, Pillow, mido and python-rtmidi to flash)?", yes):
        try:
            deps.make_venv()
        except deps.FetchError as e:
            print(f"setup: {e}")
    rep.add("builder venv", deps.venv_ready(), str(v), "python tools/optimist.py setup (makes the venv)")
    midi = deps.venv_has_midi()             # (optional: the builder builds without it; not a failure)
    rep.add("flash (mido)", True if midi else None,
            "mido, python-rtmidi in the venv" if midi else "not installed: python tools/optimist.py setup, then flash")


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


def check_toolchain(rep, fetch, yes):
    """pi32v2-llvm, the compiler: its drop-in for this host in the repository's build/toolchains (the main checkout's, in a
    worktree), run natively"""
    d = TC.llvm_dir()
    asset = TC.llvm_asset()
    wrong = TC.has_clang(d) and not TC.runs_here(d)        # (a copy for another host: a Mac's checkout that ran Docker)
    if (not TC.has_clang(d) or wrong) and fetch and asset and confirm(
            f"{'replace the pi32v2-llvm that is for another host with the one for this host' if wrong else 'download pi32v2-llvm'}"
            f" ({asset[2]} MB) into {repo_root.toolchains_dir()}?", yes):
        try:
            deps.fetch_llvm()
        except deps.FetchError as e:
            print(f"setup: {e}")
    try:
        b = TC.resolve()
    except TC.ToolchainError as e:
        rep.add("toolchain", False, str(e), toolchain_fix())
        return
    rep.add("toolchain", True, b.describe())


def check_mflasher(rep, fetch, yes):
    """M-Flasher (third_party/m-flasher): the installers' download mode (docs/INSTALL-PATHS.md); its pinned release"""
    import mflasher as MF
    exe = MF.binary()
    if exe is None and fetch and MF.asset() and confirm(
            f"download M-Flasher v{MF.VERSION} for {MF.machine_label()} ({MF.pick()['size'] / 1e6:.1f} MB; MIT, Julius "
            f"Vitkauskas) into {MF.home()} (download-mode installs)?", yes):
        try:
            exe = MF.fetch()
            for n in MF.notes():
                print(f"setup: {n}")
        except MF.MFlasherError as e:
            print(f"setup: {e}")
    rep.add("M-Flasher", None if exe is None else True, str(exe) if exe else "not installed (only download-mode installs need it)",
            "python tools/fm1_anyflash.py setup")


def toolchain_fix():
    if TC.llvm_asset() is None:
        return "build pi32v2-llvm from the pinned commits (BUILDING.md, 'The compiler'), then PI32V2_LLVM_TOOLCHAIN=<dir>"
    return "python tools/optimist.py setup --yes (downloads pi32v2-llvm into build/toolchains); Python 3.11 or newer as python3"


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
            (" (not checked on Windows: the host tests need sh and a C compiler)" if SYSTEM == "Windows" else ""),
            "a C compiler (Xcode command line tools: xcode-select --install; Debian: apt install build-essential)")


def run(fetch=True, yes=False, emulator=True):
    print(f"setup: {SYSTEM} {platform.machine()}, Python {platform.python_version()}")
    rep = Report()
    check_python(rep)
    check_venv(rep, fetch, yes)
    check_sdk(rep, fetch, yes)
    check_toolchain(rep, fetch, yes)
    check_mflasher(rep, fetch, yes)
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

# SPDX-License-Identifier: GPL-3.0-only
"""Run a firmware in the FM-1 emulator (github.com/simonjohansson/fm1-emulator and our fork).

  python tools/optimist.py emu                      pick a firmware and CPU clock interactively
  python tools/optimist.py emu FIRMWARE [options]   FIRMWARE: a path, or part of a listed name
  python tools/optimist.py emu --list               list the firmware found, then exit
  python tools/optimist.py emu --update             fetch the emulator (and rebuild if it moved), then exit
  python tools/optimist.py emu FIRMWARE --fresh     start from the package alone (forget the saved flash)

What the firmware writes to flash (the autosave, projects, presets, settings) is kept between runs, as on the
device: in emulator/state/<family>.nor and .index (the family: the package name up to its version, so a new
optimist build starts with the last one's data). --fresh (make emu FRESH=1) starts from the package alone and
replaces it; the emulator's Flash menu has Reset flash state too. (An emulator without flash states, e.g.
upstream's: nothing is kept.)

Firmware is looked for in build/ (optimist-*.fwsc, the packages built here) and firmwares/ (firmware you
downloaded: stock, Felucca, SLOOP, X0X...; one folder level down too; git-ignored; IMAGES=<dir> elsewhere).

The emulator is cloned into emulator/fm1-emulator (git-ignored) on the first run; every run then fetches the
branch and rebuilds when it moved (EMU_OFFLINE=1: use it as it is). In a git worktree the clone and its build are
the main checkout's (made there once, under a lock; BUILDING.md, Worktrees); the flash state stays per worktree:
  EMU_REPO    where to clone from (default: our public fork https://github.com/w0ts/fm1-emulator.git;
              upstream: https://github.com/simonjohansson/fm1-emulator.git)
  EMU_BRANCH  the branch (default: feat/upstream-merge for our fork, main for upstream)
  EMU_DIR     use an existing rust-emulator directory instead (no clone, no fetch)
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import shared

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_URL = "https://github.com/simonjohansson/fm1-emulator.git"
FORK_URL = "https://github.com/w0ts/fm1-emulator.git"
EMU_HOME = ROOT / "emulator"                    # the clone and the logs (git-ignored; no hidden folders)
CLONE = EMU_HOME / "fm1-emulator"               # (in a git worktree: clone_dir(), the main checkout's)
STATE = EMU_HOME / "state"                      # the flash kept between runs, per firmware family (visible)
DEFAULT_CPU = 96                                # MHz: correct sound, faster than real time for our firmware
OWN = "own"                                     # --cpu own: the firmware's own clock (realistic, slowest)
NONE = ("no .fwsc in build/ or firmwares/ (build one with 'python tools/optimist.py builder', or put downloaded "
        "firmware in firmwares/)")


class EmuError(Exception):
    pass


def images_dir(env=None):
    env = os.environ if env is None else env
    return Path(env["IMAGES"]).expanduser() if env.get("IMAGES") else ROOT / "firmwares"


def emu_source(env=None):
    """-> (repo, branch)"""
    env = os.environ if env is None else env
    repo = env.get("EMU_REPO") or FORK_URL
    branch = env.get("EMU_BRANCH") or ("main" if "simonjohansson" in repo else "feat/upstream-merge")
    return repo, branch


def exe_name():
    return "fm1-ui.exe" if os.name == "nt" else "fm1-ui"


def git(*args):
    if not shutil.which("git"):
        raise EmuError("git is needed to fetch the emulator")
    return subprocess.run(["git", *args]).returncode == 0


def git_out(*args):
    """-> git's output, or None when it failed (quietly)"""
    try:
        p = subprocess.run(["git", *args], capture_output=True, text=True)
    except OSError:
        return None
    return p.stdout.strip() if p.returncode == 0 else None


def clone_dir(root=None):
    """-> where the emulator is cloned and built: emulator/fm1-emulator; in a linked git worktree the main
    checkout's (tools/shared.py), cloned there once. The flash state and the logs stay in this worktree's emulator/"""
    path, is_shared = shared.resolve("emulator/fm1-emulator", lambda d: (d / ".git").exists(), root or ROOT)
    if is_shared and (path / ".git").exists():
        shared.note("emulator", path)
    return path


def ensure_clone(update, env=None, clone=None):
    """clone once; then every run fetches the branch and checks out what moved (EMU_OFFLINE=1: use the clone as
    it is; offline: say so and go on) -> True when the emulator must be (re)built"""
    env = os.environ if env is None else env
    clone = clone or clone_dir()
    repo, branch = emu_source(env)
    if not (clone / ".git").exists():
        print(f"emu: cloning {repo} ({branch}) into {clone} ...", flush=True)
        clone.parent.mkdir(parents=True, exist_ok=True)
        if not git("clone", "--branch", branch, repo, str(clone)):
            raise EmuError("clone failed")
        return True
    if env.get("EMU_OFFLINE", "0") == "1":
        return False
    before = git_out("-C", str(clone), "rev-parse", "HEAD")
    if git_out("-C", str(clone), "fetch", "-q", repo, branch) is None:
        print(f"emu: could not fetch {branch} (offline?): using the emulator as it is", flush=True)
        return False
    after = git_out("-C", str(clone), "rev-parse", "FETCH_HEAD")
    if before != after:
        n = len((git_out("-C", str(clone), "log", "--oneline", f"{before}..{after}") or "").splitlines())
        print(f"emu: new emulator commits on {branch} ({n}): updating", flush=True)
        if not git("-C", str(clone), "checkout", "-q", "--detach", "FETCH_HEAD"):
            raise EmuError("update failed")
        return True
    if update:
        print(f"emu: the emulator is up to date ({git_out('-C', str(clone), 'log', '--oneline', '-1')})", flush=True)
    return False


def ensure_emulator(rebuild=False, update=False):
    """-> the fm1-ui executable, built when missing. The clone, its update and the build run under the install
    lock of the directory they are in (worktrees share one clone and one build; a second process waits)"""
    if os.environ.get("EMU_DIR"):
        return build_emulator(Path(os.environ["EMU_DIR"]).expanduser(), rebuild)
    clone = clone_dir()
    with shared.install_lock(clone.parent, "emulator"):
        rebuild = ensure_clone(update, clone=clone) or rebuild
        return build_emulator(clone / "rust-emulator", rebuild)


def build_emulator(d, rebuild):
    if not d.is_dir():
        raise EmuError(f"no emulator at {d}")
    exe = d / "target" / "release" / exe_name()
    if rebuild or not exe.exists():
        if not shutil.which("cargo"):
            raise EmuError("Rust (cargo) is needed to build the emulator: https://rustup.rs")
        print("emu: building the emulator (a few minutes the first time) ...", flush=True)
        if subprocess.run(["cargo", "build", "--release", "--features", "gui", "--bin", "fm1-ui"], cwd=d).returncode:
            raise EmuError("build failed (Linux: python tools/optimist.py setup lists the libraries it needs)")
    return exe


def find_firmware(images=None, build=None):
    """-> [(path, origin)], newest first within each origin: build/optimist-*.fwsc (the named packages; not the
    internal felucca.fwsc, the same bytes), then firmwares/*.fwsc and one folder level down"""
    images = images or images_dir()
    build = build or ROOT / "build"
    out = []
    for d, globs in ((build, ("optimist-*.fwsc",)), (images, ("*.fwsc", "*/*.fwsc"))):
        if not d.is_dir():
            continue
        found = [p for g in globs for p in d.glob(g) if p.is_file()]
        out += [(p, f"{d.name}/") for p in sorted(found, key=lambda p: p.stat().st_mtime, reverse=True)]
    return out


def describe(path, origin):
    when = time.strftime("%Y-%m-%d %H:%M", time.localtime(path.stat().st_mtime))
    return f"{path.name:48s} {origin:11s} {when}"


def match(entries, wanted):
    """a path, an exact listed name (with or without .fwsc), or a unique part of one"""
    if Path(wanted).is_file():
        return Path(wanted)
    hits = []
    for path, _ in entries:
        if path.name in (wanted, wanted + ".fwsc"):
            return path
        if wanted in path.name:
            hits.append(path)
    if not hits:
        raise EmuError(f"no firmware matching '{wanted}' (see --list)")
    if len(hits) > 1:
        raise EmuError(f"'{wanted}' matches {len(hits)} files: {' '.join(p.name for p in hits)}")
    return hits[0]


def check_cpu(cpu):
    """-> MHz, or None for the firmware's own clock. Empty = the default (96), 'own' = its own clock"""
    if cpu in (None, ""):
        return DEFAULT_CPU
    if str(cpu).strip().lower() == OWN:
        return None
    if not str(cpu).isdigit() or not 1 <= int(cpu) <= 1000:
        raise EmuError("--cpu takes 1..1000 MHz, or 'own' (the firmware's own clock)")
    return int(cpu)


def pick(entries, cpu):
    if not sys.stdin.isatty():
        raise EmuError("no firmware given and no terminal to ask (try --list)")
    if not entries:
        raise EmuError(NONE)
    print("Firmware:")
    for i, (p, o) in enumerate(entries, 1):
        print(f"  {i:2d}) {describe(p, o)}")
    while True:
        s = input(f"Pick 1-{len(entries)}: ").strip()
        if s.isdigit() and 1 <= int(s) <= len(entries):
            fw = entries[int(s) - 1][0]
            break
    if cpu is None:
        print(f"CPU clock: {DEFAULT_CPU} = correct sound, faster than real time for our firmware (enter),")
        print(f"           {OWN} = the firmware's own clock (realistic, slowest), or any MHz")
        cpu = input(f"CPU MHz [{DEFAULT_CPU}]: ").strip()
    return fw, cpu


def keeps_flash(exe):
    """whether this fm1-ui keeps the flash between runs (--state, --fresh): its source has flash_state.rs"""
    return (Path(exe).resolve().parents[2] / "src" / "flash_state.rs").is_file()


def state_args(exe, fresh):
    """-> fm1-ui's flash state options: emulator/state/ (a folder: <family>.nor in it), --fresh"""
    if not keeps_flash(exe):
        if fresh:
            print("emu: this emulator does not keep the flash between runs: --fresh changes nothing", flush=True)
        return []
    return ["--state", str(STATE) + os.sep, *(["--fresh"] if fresh else [])]


def launch(exe, fw, cpu, background, fresh=False):
    args = [str(exe), str(fw), *([f"--cpu-mhz={cpu}"] if cpu else []), *state_args(exe, fresh)]
    kept = ("no flash kept between runs" if not keeps_flash(exe) else "from the package alone" if fresh
            else f"with the flash it saved ({STATE.relative_to(ROOT)}/)")
    print(f"emu: {fw.name} at {str(cpu) + ' MHz' if cpu else 'its own clock'}, {kept}", flush=True)
    if not background:
        if os.name == "nt":
            return subprocess.call(args)
        os.execv(args[0], args)
    logs = EMU_HOME / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / (fw.stem + ".log")
    detach = ({"creationflags": subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP}
              if os.name == "nt" else {"start_new_session": True})
    with open(log, "wb") as f:
        p = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, **detach)
    Path(str(log) + ".pid").write_text(f"{p.pid}\n")
    print(f"emu: started (pid {p.pid}), log {log}")
    return 0


def parser():
    ap = argparse.ArgumentParser(prog="optimist.py emu", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", nargs="?", help="a path, or part of a listed name")
    ap.add_argument("--cpu", help=f"emulated CPU clock, 1..1000 MHz (default {DEFAULT_CPU}: correct sound, faster "
                    f"than real time for our firmware), or '{OWN}': the firmware's own clock (stock, Baud Girl)")
    ap.add_argument("--bg", action="store_true", help="start in the background (log in emulator/logs/<name>.log)")
    ap.add_argument("--rebuild", action="store_true", help="rebuild the emulator first")
    ap.add_argument("--fresh", action="store_true", help="start from the package alone: forget the flash the firmware "
                    "saved in earlier runs (emulator/state/<family>.nor)")
    ap.add_argument("--list", action="store_true", help="list the firmware found, then exit")
    ap.add_argument("--update", action="store_true", help="fetch the emulator (and rebuild if it moved), then exit")
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    try:
        if a.update:
            ensure_emulator(update=True)
            return 0
        entries = find_firmware()
        if a.list:
            if not entries:
                raise EmuError(NONE)
            for i, (p, o) in enumerate(entries, 1):
                print(f"{i:2d}) {describe(p, o)}")
            return 0
        if a.firmware:
            fw, cpu = match(entries, a.firmware), a.cpu
        else:
            fw, cpu = pick(entries, a.cpu)
        cpu = check_cpu(cpu)
        exe = ensure_emulator(rebuild=a.rebuild)
        return launch(exe, fw, cpu, a.bg, a.fresh)
    except EmuError as e:
        print(f"emu: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: GPL-3.0-only
"""Run a firmware in the FM-1 emulator (github.com/simonjohansson/fm1-emulator and our fork).

  python tools/optimist.py emu                      pick a firmware and CPU clock interactively
  python tools/optimist.py emu FIRMWARE [options]   FIRMWARE: a path, or part of a listed name
  python tools/optimist.py emu --list               list the firmware found, then exit
  python tools/optimist.py emu --update             fetch and rebuild the emulator, then exit

Firmware is looked for in build/ (packages built here) and images/ (firmware you downloaded: stock, Felucca,
SLOOP, X0X...; git-ignored; IMAGES=<dir> elsewhere).

The emulator is cloned into .emu/fm1-emulator (git-ignored) on the first run:
  EMU_REPO    where to clone from (default: our private fork github.com/hdavid/fm1-emulator;
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

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_URL = "https://github.com/simonjohansson/fm1-emulator.git"
FORK_URL = "git@github.com:hdavid/fm1-emulator.git"
CLONE = ROOT / ".emu" / "fm1-emulator"
NONE = "no .fwsc in build/ or images/ (build one with 'python tools/optimist.py builder', or put downloaded " \
       "images in images/)"


class EmuError(Exception):
    pass


def images_dir(env=None):
    env = os.environ if env is None else env
    return Path(env["IMAGES"]).expanduser() if env.get("IMAGES") else ROOT / "images"


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


def ensure_clone(update):
    """clone once; update fetches again -> True when the emulator must be (re)built"""
    repo, branch = emu_source()
    if not (CLONE / ".git").exists():
        print(f"emu: cloning {repo} ({branch}) into .emu/fm1-emulator ...")
        CLONE.parent.mkdir(parents=True, exist_ok=True)
        if not git("clone", "--branch", branch, repo, str(CLONE)):
            raise EmuError("clone failed")
        return True
    if update:
        print(f"emu: fetching {branch} from {repo} ...")
        if not (git("-C", str(CLONE), "fetch", repo, branch) and
                git("-C", str(CLONE), "checkout", "-q", "--detach", "FETCH_HEAD")):
            raise EmuError("fetch failed")
        return True
    return False


def ensure_emulator(rebuild=False, update=False):
    """-> the fm1-ui executable, built when missing"""
    if os.environ.get("EMU_DIR"):
        d = Path(os.environ["EMU_DIR"]).expanduser()
    else:
        rebuild = ensure_clone(update) or rebuild
        d = CLONE / "rust-emulator"
    if not d.is_dir():
        raise EmuError(f"no emulator at {d}")
    exe = d / "target" / "release" / exe_name()
    if rebuild or not exe.exists():
        if not shutil.which("cargo"):
            raise EmuError("Rust (cargo) is needed to build the emulator: https://rustup.rs")
        print("emu: building the emulator (a few minutes the first time) ...")
        if subprocess.run(["cargo", "build", "--release", "--features", "gui", "--bin", "fm1-ui"], cwd=d).returncode:
            raise EmuError("build failed")
    return exe


def find_firmware(images=None):
    """-> [(path, origin)], newest first within each origin (build/, then images/; one folder level down)"""
    out = []
    for d, origin in ((ROOT / "build", "built here"), (images or images_dir(), "downloaded")):
        if not d.is_dir():
            continue
        found = [p for p in (*d.glob("*.fwsc"), *d.glob("*/*.fwsc")) if p.is_file()]
        out += [(p, origin) for p in sorted(found, key=lambda p: p.stat().st_mtime, reverse=True)]
    return out


def describe(path, origin):
    when = time.strftime("%Y-%m-%d %H:%M", time.localtime(path.stat().st_mtime))
    return f"{path.name:42s} {origin:11s} {when}"


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
    if cpu in (None, ""):
        return None
    if not str(cpu).isdigit() or not 1 <= int(cpu) <= 1000:
        raise EmuError("--cpu takes 1..1000 MHz")
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
        print("CPU clock: empty = the firmware's own clock (realistic, slowest),")
        print("           96 = correct sound, faster (our firmware runs above real time)")
        cpu = input("CPU MHz [own]: ").strip() or None
    return fw, cpu


def launch(exe, fw, cpu, background):
    args = [str(exe), str(fw), *([f"--cpu-mhz={cpu}"] if cpu else [])]
    print(f"emu: {fw.name} at {str(cpu) + ' MHz' if cpu else 'its own clock'}")
    if not background:
        if os.name == "nt":
            return subprocess.call(args)
        os.execv(args[0], args)
    logs = ROOT / ".emu" / "logs"
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
    ap.add_argument("--cpu", help="emulated CPU clock, 1..1000 MHz (default: the firmware's own clock; 96 = "
                    "correct sound and faster than real time for our firmware)")
    ap.add_argument("--bg", action="store_true", help="start in the background (log in .emu/logs/<name>.log)")
    ap.add_argument("--rebuild", action="store_true", help="rebuild the emulator first")
    ap.add_argument("--list", action="store_true", help="list the firmware found, then exit")
    ap.add_argument("--update", action="store_true", help="fetch and rebuild the emulator, then exit")
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
        cpu = check_cpu(a.cpu)
        if a.firmware:
            fw = match(entries, a.firmware)
        else:
            fw, cpu = pick(entries, cpu)
            cpu = check_cpu(cpu)
        exe = ensure_emulator(rebuild=a.rebuild)
        return launch(exe, fw, cpu, a.bg)
    except EmuError as e:
        print(f"emu: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: GPL-3.0-only
"""M-Flasher's command line, as Optimist's installers call it (tools/fm1_anyflash.py): where the program is, how
setup fetches the pinned release, and the exact command lines. M-Flasher (Julius Vitkauskas, MIT,
third_party/m-flasher, pinned at v0.9.2 1309094) talks to the chip's USB download mode; its docs/usage.md is the
reference for the commands. Nothing here talks to a device: run() starts the program, the tests replace it with a
fake executable that records its arguments.

  binary()            the M-Flasher to run, or None: $M_FLASHER, the pinned release fetched by setup
                      (build/m-flasher/m-flasher-0.9.2 in the repo), M-Flasher.app in /Applications, m-flasher on the PATH
  fetch()             the pinned release for this system, checked by its SHA-256 (as tools/deps.py does pi32v2-llvm)
  build_from_source() cargo build --release in third_party/m-flasher (when there is no release for this system)
  argv(...)           the command lines: never --wipe-settings (it would erase the region an Optimist or fm1-base
                      package declares as VM: all user data, and the SDK's VM with the radio calibration), never
                      --with-bootloader, never --board (the model is M-Flasher's to identify: refused when uncertain)"""
from __future__ import annotations

import codecs
import io
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

import repo_root

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SUBMODULE = ROOT / "third_party" / "m-flasher"
VERSION = "0.9.2"
COMMIT = "1309094869fc860f57671b466fce7769af7e1a18"
RELEASE = f"https://github.com/jvitkauskas/m-flasher/releases/download/v{VERSION}/"
REPO = "jvitkauskas/m-flasher"
PAGE = f"https://github.com/{REPO}/releases/tag/v{VERSION}"
# the release's files (GitHub's digests, 2026-11-07): the ONE place the version, names, sizes and SHA-256 are written.
# The web installer and the Mirror app get site_data() of it, generated at build time (tools/sync_mflasher.py,
# web/make_site.py); tests/mflasher_get_test.py checks that they agree. os: macos / windows / linux; arch: any (one
# build for the OS) or x86_64 / aarch64; kind: how it installs (dmg, zip, tar.gz run from a folder; deb, rpm are packages)
RELEASE_FILES = (
    {"name": f"M-Flasher-{VERSION}-macos.dmg", "os": "macos", "arch": "any", "kind": "dmg", "size": 7418305,
     "sha256": "f44204478e6fb2237ad1cb98b9c9797fc303d1ae427520656dd2263c4822d61d"},
    {"name": f"M-Flasher-{VERSION}-windows.zip", "os": "windows", "arch": "any", "kind": "zip", "size": 3167039,
     "sha256": "541c1e7eaba809e6c49e5748dd660f4efaa9f75e20dd8524ee9b5e95a689066d"},
    {"name": f"m-flasher_{VERSION}-1_amd64.deb", "os": "linux", "arch": "x86_64", "kind": "deb", "size": 4063902,
     "sha256": "5713540e524107c567e20f2d1a3d8a9e12bec22cd607be23f1d58b7fe11e09c4"},
    {"name": f"m-flasher-{VERSION}-1.x86_64.rpm", "os": "linux", "arch": "x86_64", "kind": "rpm", "size": 4172030,
     "sha256": "56c05b328853339ddf633be3fc81ab17d7b1ae1c3b01892e8a86d52668ca2f37"},
    {"name": f"m-flasher-{VERSION}-linux-x86_64.tar.gz", "os": "linux", "arch": "x86_64", "kind": "tar.gz", "size": 3994454,
     "sha256": "ed152205734312190364c029e2a6bf1413831341ce2b047e31b77d02efc4bb24"},
    {"name": f"m-flasher_{VERSION}-1_arm64.deb", "os": "linux", "arch": "aarch64", "kind": "deb", "size": 3763218,
     "sha256": "69776d4a90584e4c4f165a2c366c19fc54d7362f67b8088d15f791c6b98b2a9f"},
    {"name": f"m-flasher-{VERSION}-1.aarch64.rpm", "os": "linux", "arch": "aarch64", "kind": "rpm", "size": 3837926,
     "sha256": "6f2167aa32a48554c4ab366e9bc19564060c6a7512594b28d5eed1e8a443f500"},
    {"name": f"m-flasher-{VERSION}-linux-aarch64.tar.gz", "os": "linux", "arch": "aarch64", "kind": "tar.gz", "size": 3644334,
     "sha256": "03c789e36e592ee96d511f416cedcdd78a51e5b238827720bc77e3f7ad93dfb4"},
)
SYSTEMS = {"macos": "Darwin", "windows": "Windows", "linux": "Linux"}
OS_LABEL = {"Darwin": "macOS", "Windows": "Windows", "Linux": "Linux"}
# what setup downloads and runs without root: the dmg's app (its bundled CLI), the zip's exe, the tarball's binary
INSIDE = {"dmg": "M-Flasher.app/Contents/MacOS/M-Flasher", "zip": "M-Flasher.exe", "tar.gz": "m-flasher/m-flasher"}
ASSETS = {(SYSTEMS[f["os"]], f["arch"]): (f["name"], f["sha256"], INSIDE[f["kind"]])
          for f in RELEASE_FILES if f["kind"] in INSIDE}
# m-bootloader: Julius's own v2.2 release, as he built it (its SHA256SUMS). 2.0 on has MIDI mode, 2.2 backs up and runs RAM
# apps over MIDI (docs/INSTALL-PATHS.md); it replaces our fork, whose SD card item is in his main. M-Flasher 0.9.2 bundles
# this same version, installers still pass --file so that the file is the one pinned here.
MBOOT_RELEASE = {"version": "2.2", "commit": "64f7c9cad1580bea870fc4a9e9bd61583fee3aef",
                 "fm1-uboot.boot": "cedc9c17648bdeecda5c8d0ffffe2211849f8a36ab615fe49d6cc46d249baf44",
                 "fm1-uboot.fwsc": "cb6128ee753a1661e6399002317464959231854024f259520755621954438981"}
MBOOT_DIR = ROOT / "web" / "fm1base" / "m-bootloader"
MBOOT_BOOT = MBOOT_DIR / f"fm1-uboot-{MBOOT_RELEASE['version']}.boot"
MBOOT_COPIES = {MBOOT_BOOT: "fm1-uboot.boot",
                MBOOT_DIR / f"fm1-uboot-{MBOOT_RELEASE['version']}.fwsc": "fm1-uboot.fwsc"}
COMMANDS = ("find", "download-mode", "info", "backup", "restore", "firmware", "bootloader", "restart")
# the commands that open the unit in download mode: on macOS each run starts M-Flasher's root helper after the system's
# password dialog (its src/macos.rs: one helper per process, AuthorizationExecuteWithPrivileges, nothing kept between
# runs), on Windows each is an administrator prompt. find and download-mode list the units from the I/O registry and
# USB-MIDI without the helper: no dialog. Installers keep these runs as few as their checks allow (fm1_anyflash.py)
OPENS_UNIT = ("info", "backup", "restore", "firmware", "bootloader", "restart")


class MFlasherError(Exception):
    pass


def home() -> Path:
    """where setup keeps the fetched release: inside the repo (build/m-flasher of the main checkout, git-ignored; tools/repo_root.py), never in the user's home
    or the system (the user, 2026-10-30: nothing installed on the Mac)"""
    return repo_root.build_dir("m-flasher", "M_FLASHER_HOME", ROOT) / f"m-flasher-{VERSION}"


def _norm(system: str | None, machine: str | None):
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    return system, {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)


def asset(system: str | None = None, machine: str | None = None):
    """(file, sha256, binary inside) of the pinned release for this system, None when it has none"""
    system, machine = _norm(system, machine)
    return ASSETS.get((system, "any")) or ASSETS.get((system, machine))


def pick(system: str | None = None, machine: str | None = None):
    """the RELEASE_FILES entry setup downloads for this system (None when there is none)"""
    a = asset(system, machine)
    return next((f for f in RELEASE_FILES if a and f["name"] == a[0]), None)


def machine_label(system: str | None = None, machine: str | None = None) -> str:
    """"macOS arm64", "Windows x86_64", "Linux aarch64" (the question's words)"""
    system, machine = _norm(system, machine)
    if system == "Darwin" and machine == "aarch64":
        machine = "arm64"                            # (what Apple calls it)
    return f"{OS_LABEL.get(system, system)} {machine}"


def question(system: str | None = None, machine: str | None = None) -> str:
    f = pick(system, machine)
    if not f:
        raise MFlasherError(f"M-Flasher {VERSION} has no release for {machine_label(system, machine)}")
    return (f"M-Flasher (MIT, Julius Vitkauskas) is needed for this install: download v{VERSION} for "
            f"{machine_label(system, machine)} ({f['size'] / 1e6:.1f} MB)?")


def notes(system: str | None = None) -> list:
    """what M-Flasher's README (docs/usage.md) says about the first run; shown to the user, never worked around"""
    if (system or platform.system()) == "Darwin":
        return ["M-Flasher on macOS is not signed with an Apple certificate: if macOS blocks it, the user opens "
                "System Settings > Privacy & Security > Open Anyway (this installer does not bypass that).",
                "When it finds the unit, it asks for the password in the system's dialog, once per run that opens the unit "
                "(its root helper takes the USB device from macOS's disk driver: an install is two runs, the backup and "
                "the write, a third with m-bootloader) and once whether it may access files on a removable volume: Allow.",
                "Run from Terminal at the Mac itself, not over SSH (macOS asks about the removable volume on the screen)."]
    if (system or platform.system()) == "Windows":
        return ["M-Flasher is not signed: if Windows says it protected the PC, the user chooses More info > Run anyway; "
                "it asks for administrator rights."]
    return []


def site_data() -> dict:
    """the pinned release as the web installer and the Mirror app read it (tools/sync_mflasher.py, web/make_site.py)"""
    return {"version": VERSION, "tag": f"v{VERSION}", "repo": REPO, "page": PAGE, "license": "MIT",
            "author": "Julius Vitkauskas",
            "files": [dict(f, url=RELEASE + f["name"]) for f in RELEASE_FILES]}


def binary() -> Path | None:
    env = os.environ.get("M_FLASHER")
    if env:
        return Path(env) if Path(env).is_file() else None
    a = asset()
    cands = [home() / a[2]] if a else []
    cands += [SUBMODULE / "target" / "release" / ("m-flasher.exe" if os.name == "nt" else "m-flasher"),
              Path("/Applications/M-Flasher.app/Contents/MacOS/M-Flasher")]
    found = shutil.which("m-flasher")
    cands += [Path(found)] if found else []
    return next((c for c in cands if c.is_file()), None)


def _sha256(path: Path) -> str:
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _unpack(archive: Path, dest: Path) -> None:
    name = archive.name
    if name.endswith(".tar.gz"):
        import deps
        try:
            deps.extract(archive, dest)
        except tarfile.TarError as e:
            raise MFlasherError(f"{name}: {e}")
    elif name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for m in z.namelist():
                if m.startswith("/") or ".." in Path(m).parts:
                    raise MFlasherError(f"{name}: unexpected path {m}")
            z.extractall(dest)
    elif name.endswith(".dmg"):                       # macOS: mount read-only, copy the app out, detach
        mnt = Path(tempfile.mkdtemp(dir=dest.parent, prefix="mf-mnt-"))
        try:
            subprocess.run(["hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint", str(mnt), str(archive)],
                           check=True, capture_output=True)
            try:
                shutil.copytree(mnt / "M-Flasher.app", dest / "M-Flasher.app", symlinks=True)
            finally:
                subprocess.run(["hdiutil", "detach", str(mnt)], capture_output=True)
        except (OSError, subprocess.CalledProcessError) as e:
            raise MFlasherError(f"{name}: cannot unpack ({e})")
        finally:
            shutil.rmtree(mnt, ignore_errors=True)
    else:
        raise MFlasherError(f"{name}: unknown archive")


def fetch(download=None) -> Path:
    """the pinned release into home() (kept when it is there), checked by its SHA-256 -> the binary"""
    a = asset()
    if not a:
        raise MFlasherError(f"M-Flasher {VERSION} has no release for {platform.system()} {platform.machine()}: "
                            "build it (python tools/fm1_anyflash.py setup --from-source, needs Rust)")
    file, want, inside = a
    dest = home()
    if (dest / inside).is_file():
        return dest / inside
    if download is None:
        import deps
        download = deps.download
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest.parent) as tmp:
        archive = Path(tmp) / file
        print(f"m-flasher: downloading {file} (M-Flasher {VERSION})")
        try:
            download(RELEASE + file, archive)
        except Exception as e:                       # (deps.FetchError, OSError, a fake's error: nothing written)
            raise MFlasherError(f"cannot download {RELEASE + file}: {e}; nothing installed (offline? "
                                "M_FLASHER=/path/to/m-flasher uses a copy you already have)")
        if _sha256(archive) != want:
            raise MFlasherError(f"{file}: does not match its pinned SHA-256; nothing installed")
        out = Path(tmp) / "out"
        out.mkdir()
        _unpack(archive, out)
        if not (out / inside).is_file():
            raise MFlasherError(f"{file}: no {inside} inside")
        shutil.move(str(out), str(dest))
    return dest / inside


def ensure(ask, say=print, download=None) -> Path:
    """the M-Flasher to run: found, else asked for ONCE (ask(question) -> bool) and fetched, checked, installed in home()"""
    exe = binary()
    if exe:
        return exe
    q = question()
    if not ask(q):
        raise MFlasherError(f"M-Flasher is needed for this install and was not downloaded: python tools/fm1_anyflash.py "
                            f"setup (v{VERSION}), or M_FLASHER=/path/to/m-flasher")
    exe = fetch(download)
    say(f"M-Flasher {VERSION} installed: {exe}")
    for n in notes():
        say("  " + n)
    return exe


def build_from_source() -> Path:
    if not (SUBMODULE / "Cargo.toml").is_file():
        raise MFlasherError("third_party/m-flasher is not checked out: git submodule update --init third_party/m-flasher")
    cargo = shutil.which("cargo")
    if not cargo:
        raise MFlasherError("cargo not found (Rust from rustup.rs) to build M-Flasher")
    subprocess.run([cargo, "build", "--release", "--locked", "--manifest-path", str(SUBMODULE / "Cargo.toml")], check=True)
    exe = SUBMODULE / "target" / "release" / ("m-flasher.exe" if os.name == "nt" else "m-flasher")
    if not exe.is_file():
        raise MFlasherError(f"the build left no {exe}")
    return exe


# ---------------------------------------------------------------- the command lines ---

def argv(exe, cmd: str, *, arg: str | None = None, backup_dir: str | None = None, yes: bool = False,
         restart: bool = False, boot_file: str | None = None, stock: str | None = None) -> list:
    """one M-Flasher command line (docs/usage.md "Command line"); arg: backup's DIR, restore's BACKUP.bin,
    firmware's PACKAGE.fwsc; stock: bootloader's --stock PACKAGE.fwsc (the stock bootloader put back)"""
    if cmd not in COMMANDS:
        raise MFlasherError(f"not an M-Flasher command here: {cmd}")
    if restart and cmd not in ("restore", "firmware", "bootloader"):
        raise MFlasherError("--restart is for restore, firmware and bootloader")
    if cmd in ("backup", "restore", "firmware") and not arg:
        raise MFlasherError(f"{cmd} needs its file or folder")
    if (boot_file or stock) and (cmd != "bootloader" or (boot_file and stock)):
        raise MFlasherError("--file or --stock: one of them, for bootloader")
    out = [str(exe), cmd, *([arg] if arg else [])]
    if boot_file:
        out += ["--file", str(boot_file)]
    if stock:
        out += ["--stock", str(stock)]
    if backup_dir and cmd in ("restore", "firmware", "bootloader"):
        out += ["--backup-dir", str(backup_dir)]
    if yes and cmd in ("restore", "firmware", "bootloader"):
        out.append("--yes")
    if restart:
        out.append("--restart")
    return out


INFO_KEYS = ("model", "bootloader", "firmware", "app", "chip key", "flash", "unit id", "loader")
INFO_RE = re.compile(r"^  (" + "|".join(re.escape(k) for k in INFO_KEYS) + r")\s+(.*?)\s*$")
BACKUP_RE = re.compile(r"^Backup saved: (.+?)\s*$", re.M)


def parse_info(text: str) -> dict:
    """M-Flasher's info lines ("  model       FM-1") -> {"model": "FM-1", ...}"""
    out = {}
    for ln in text.splitlines():
        m = INFO_RE.match(ln)
        if m and m[1] not in out:
            out[m[1]] = m[2]
    return out


def backup_path(text: str) -> str | None:
    m = BACKUP_RE.search(text)
    return m[1] if m else None


def run(args: list, answer: str | None = None, capture: bool = True, on_line=None, decide=None) -> tuple:
    """run one M-Flasher command -> (exit code, its output, "" when not captured). answer: what its "Type yes" prompt
    reads ("no\\n": its plan is shown and nothing is written). decide(output so far) -> bool: called when the program
    asks its "Type yes" question (PROMPT), while it waits with the unit open; True answers "yes", False "no" (nothing
    written). One run then reads the flash, shows its plan and writes: one password dialog on macOS, not two.
    capture=False: the program has this terminal (its own "Type yes" question is asked there, the user answers it)"""
    show = on_line or (lambda s: print(s, end="", flush=True))
    try:
        if not capture:
            return subprocess.run(args).returncode, ""
        p = subprocess.Popen(args, stdin=subprocess.PIPE if answer is not None or decide else subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        raise MFlasherError(f"cannot run {args[0]}: {e}")
    if answer is not None and not decide:
        p.stdin.write(answer.encode())
        p.stdin.close()
    text = _read_answering(p, show, decide)
    return p.wait(), text


PROMPT = 'Type "yes" to write: '


def _answer(p, yes: bool) -> None:
    try:
        p.stdin.write(b"yes\n" if yes else b"no\n")
        p.stdin.close()
    except BrokenPipeError:
        raise MFlasherError("M-Flasher stopped while it waited for its answer: nothing was answered, see its lines above")


def _read_answering(p, show, decide) -> str:
    """the program's output, shown line by line ("\\r  NN%" a line of its own: tools/progress.py reads them); its
    question (printed without a newline) answered by decide"""
    dec = io.IncrementalNewlineDecoder(codecs.getincrementaldecoder("utf-8")(errors="replace"), translate=True)
    text, shown, asked = "", 0, False
    fd = p.stdout.fileno()
    while True:
        chunk = os.read(fd, 4096)
        text += dec.decode(chunk, final=not chunk)
        end = text.rfind("\n") + 1
        if end > shown:
            for ln in text[shown:end].splitlines(keepends=True):
                show(ln)
            shown = end
        if decide and not asked and text.endswith(PROMPT):
            asked = True
            yes = False
            try:
                yes = decide(text)
            finally:                                # (decide raised: "no", nothing written)
                text += "\n"                        # (what a terminal echoes: the next line starts on its own)
                shown = len(text)                   # (the program's question is not shown: the caller asked its own)
                _answer(p, yes)
        if not chunk:
            break
    if decide and not asked and p.stdin:
        p.stdin.close()
    if shown < len(text):
        show(text[shown:])
    return text


def check_mboot_copies() -> list:
    """the m-bootloader files this tree carries that differ from the pinned build: [] when all agree"""
    bad = []
    for path, name in MBOOT_COPIES.items():
        if path.is_file() and _sha256(path) != MBOOT_RELEASE[name]:
            bad.append(f"{path.relative_to(ROOT)}: not m-bootloader {MBOOT_RELEASE['version']}'s {name}")
        elif not path.is_file() and not path.is_relative_to(SUBMODULE):   # (the submodule may not be checked out)
            bad.append(f"{path.relative_to(ROOT)}: missing")
    return bad


if __name__ == "__main__":
    print(binary() or "no M-Flasher", file=sys.stderr)

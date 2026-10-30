# SPDX-License-Identifier: GPL-3.0-only
"""M-Flasher's command line, as Optimist's installers call it (tools/fm1_anyflash.py): where the program is, how
setup fetches the pinned release, and the exact command lines. M-Flasher (Julius Vitkauskas, MIT,
third_party/m-flasher, pinned at v0.7.0 0c5d50f) talks to the chip's USB download mode; its docs/usage.md is the
reference for the commands. Nothing here talks to a device: run() starts the program, the tests replace it with a
fake executable that records its arguments.

  binary()            the M-Flasher to run, or None: $M_FLASHER, the pinned release fetched by setup
                      (~/.jieli/m-flasher-0.7.0), M-Flasher.app in /Applications, m-flasher on the PATH
  fetch()             the pinned release for this system, checked by its SHA-256 (as tools/deps.py does pi32v2-llvm)
  build_from_source() cargo build --release in third_party/m-flasher (when there is no release for this system)
  argv(...)           the command lines: never --wipe-settings (it would erase the region an Optimist or fm1-base
                      package declares as VM: all user data, and the SDK's VM with the radio calibration), never
                      --with-bootloader, never --board (the model is M-Flasher's to identify: refused when uncertain)"""
from __future__ import annotations

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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SUBMODULE = ROOT / "third_party" / "m-flasher"
VERSION = "0.7.0"
COMMIT = "0c5d50fe40a1c913c3408b9dafb33fbb88ad5d69"
RELEASE = f"https://github.com/jvitkauskas/m-flasher/releases/download/v{VERSION}/"
# the release's files (GitHub's digests, 2026-10-30), the binary inside each
ASSETS = {
    ("Darwin", "any"): (f"M-Flasher-{VERSION}-macos.dmg",
                        "a75f2dc0f2a4a5f2545320b925f265303fd8a2d3927dcb181a909a535fb67f0e",
                        "M-Flasher.app/Contents/MacOS/M-Flasher"),
    ("Linux", "x86_64"): (f"m-flasher-{VERSION}-linux-x86_64.tar.gz",
                          "4080858a224a7e2a25948da3b35fb6a2e74c4bd603f5eef73ddcb5dc4177047f", "m-flasher/m-flasher"),
    ("Linux", "aarch64"): (f"m-flasher-{VERSION}-linux-aarch64.tar.gz",
                           "342d9ca6af0fd63c6c46539455c9ab2f507a26b83863938159019ed092b58e2e", "m-flasher/m-flasher"),
    ("Windows", "any"): (f"M-Flasher-{VERSION}-windows.zip",
                         "520b83b01b41b7a24b0ced9ee5ee565d579b56f5649a26f509602d8182214538", "M-Flasher.exe"),
}
# m-bootloader's v1.2 release (22e2559; third_party/m-bootloader is pinned at 64b74d0, two documentation commits
# later): its SHA256SUMS, for the copies in web/fm1base/m-bootloader/ and M-Flasher's bundled build
MBOOT_RELEASE = {"version": "1.2", "commit": "22e2559906f9fc4e077f2d6aee20bf332300439e",
                 "fm1-uboot.boot": "9e5ec0e39dc55c167f8e289cfdac612d7d340b5f46100e1be2540ef4d6ec2f13",
                 "fm1-uboot.fwsc": "0e89141f52d1f2d62198d4f69147a2c15c8360806e64d48002990a6fdab6d6dd"}
MBOOT_COPIES = {ROOT / "web" / "fm1base" / "m-bootloader" / "fm1-uboot-1.2.boot": "fm1-uboot.boot",
                ROOT / "web" / "fm1base" / "m-bootloader" / "fm1-uboot-1.2.fwsc": "fm1-uboot.fwsc",
                SUBMODULE / "data" / "bootloader-fm1.boot": "fm1-uboot.boot"}
COMMANDS = ("find", "download-mode", "info", "backup", "restore", "firmware", "bootloader", "restart")


class MFlasherError(Exception):
    pass


def home() -> Path:
    return Path(os.environ.get("M_FLASHER_HOME") or Path.home() / ".jieli") / f"m-flasher-{VERSION}"


def asset(system: str | None = None, machine: str | None = None):
    """(file, sha256, binary inside) of the pinned release for this system, None when it has none"""
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    machine = {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)
    return ASSETS.get((system, "any")) or ASSETS.get((system, machine))


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
        download(RELEASE + file, archive)
        if _sha256(archive) != want:
            raise MFlasherError(f"{file}: does not match its pinned SHA-256; nothing installed")
        out = Path(tmp) / "out"
        out.mkdir()
        _unpack(archive, out)
        if not (out / inside).is_file():
            raise MFlasherError(f"{file}: no {inside} inside")
        shutil.move(str(out), str(dest))
    return dest / inside


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
         restart: bool = False, boot_file: str | None = None) -> list:
    """one M-Flasher command line (docs/usage.md "Command line"); arg: backup's DIR, restore's BACKUP.bin,
    firmware's PACKAGE.fwsc"""
    if cmd not in COMMANDS:
        raise MFlasherError(f"not an M-Flasher command here: {cmd}")
    if restart and cmd not in ("restore", "firmware", "bootloader"):
        raise MFlasherError("--restart is for restore, firmware and bootloader")
    if cmd in ("backup", "restore", "firmware") and not arg:
        raise MFlasherError(f"{cmd} needs its file or folder")
    out = [str(exe), cmd, *([arg] if arg else [])]
    if cmd == "bootloader" and boot_file:
        out += ["--file", str(boot_file)]
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


def run(args: list, answer: str | None = None, capture: bool = True, on_line=None) -> tuple:
    """run one M-Flasher command -> (exit code, its output, "" when not captured). answer: what its "Type yes" prompt
    reads ("no\\n": its plan is shown and nothing is written). capture=False: the program has this terminal (its own
    "Type yes" question is asked there, the user answers it)"""
    try:
        if not capture:
            return subprocess.run(args).returncode, ""
        p = subprocess.Popen(args, stdin=subprocess.PIPE if answer is not None else subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    except OSError as e:
        raise MFlasherError(f"cannot run {args[0]}: {e}")
    if answer is not None:
        p.stdin.write(answer)
        p.stdin.close()
    lines = []
    for ln in p.stdout:
        lines.append(ln)
        (on_line or (lambda s: print(s, end="", flush=True)))(ln)
    return p.wait(), "".join(lines)


def check_mboot_copies() -> list:
    """the m-bootloader files this tree carries that differ from the pinned v1.2 release: [] when all agree"""
    bad = []
    for path, name in MBOOT_COPIES.items():
        if path.is_file() and _sha256(path) != MBOOT_RELEASE[name]:
            bad.append(f"{path.relative_to(ROOT)}: not m-bootloader {MBOOT_RELEASE['version']}'s {name}")
        elif not path.is_file() and not path.is_relative_to(SUBMODULE):   # (the submodule may not be checked out)
            bad.append(f"{path.relative_to(ROOT)}: missing")
    return bad


if __name__ == "__main__":
    print(binary() or "no M-Flasher", file=sys.stderr)

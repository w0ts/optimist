# SPDX-License-Identifier: GPL-3.0-only
"""Fetch what the build needs, on any host, without sh, curl or tar (python tools/optimist.py setup):

  the builder's venv      tools/builder/venv (BUILDER_VENV) with tools/requirements.txt (Textual, the pinned Pillow)
  the SDK files           three files of JieLi's AC79 SDK (Apache-2.0), checked against their SHA-256
(in a git worktree the venv and the SDK files are the main checkout's, installed there once under a lock:
tools/shared.py)
  the toolchain           pi32v2-llvm's drop-in for this host (pinned archive, SHA-256) into build/toolchains
"""
import hashlib
import http.client
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

import repo_root
import shared
import toolchain as TC

ROOT = TC.ROOT
REQUIREMENTS = ROOT / "tools" / "requirements.txt"
REQUIREMENTS_FLASH = ROOT / "tools" / "requirements-flash.txt"      # mido, python-rtmidi: `flash` (fm1_install.py)


class FetchError(Exception):
    pass


# ---- the builder's venv

def venv_has_python(v):
    return venv_python(v).exists()


def venv_dir(root=None):
    """BUILDER_VENV, else tools/builder/venv; in a linked git worktree the main checkout's (used when it has
    one, else this worktree's own when it has one, else the main checkout's: make_venv makes it there)"""
    if os.environ.get("BUILDER_VENV"):
        return Path(os.environ["BUILDER_VENV"])
    v, is_shared = shared.resolve("tools/builder/venv", venv_has_python, root or ROOT)
    if is_shared and venv_has_python(v):
        shared.note("venv", v)
    return v


def venv_python(venv=None):
    v = venv or venv_dir()
    return v / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def venv_ready(venv=None):
    py = venv_python(venv)
    try:                            # (a mount that refuses to stat a venv's symlinked python: not ready)
        return py.exists() and subprocess.run([str(py), "-c", "import textual, PIL"],
                                              capture_output=True).returncode == 0
    except OSError:
        return False


def venv_has_midi(venv=None):
    """the venv can flash: mido and python-rtmidi import (requirements-flash.txt)"""
    py = venv_python(venv)
    try:
        return py.exists() and subprocess.run([str(py), "-c", "import mido, rtmidi"],
                                              capture_output=True).returncode == 0
    except OSError:
        return False


def make_venv(venv=None):
    """the venv with tools/requirements.txt in it (under the install lock: one process makes it, the others wait
    and find it ready)"""
    v = venv or venv_dir()
    with shared.install_lock(v, "venv"):
        if venv_ready(v):
            install_flash(v)
            return venv_python(v)
        if not venv_python(v).exists():
            print(f"setup: making {v}")
            if subprocess.run([sys.executable, "-m", "venv", str(v)]).returncode:
                raise FetchError(f"python -m venv {v} failed (Debian / Ubuntu: apt install python3-venv)")
        print(f"setup: installing {REQUIREMENTS.relative_to(ROOT).as_posix()} into the venv")
        cmd = [str(venv_python(v)), "-m", "pip", "install", "--quiet", "--disable-pip-version-check",
               "-r", str(REQUIREMENTS)]
        if subprocess.run(cmd).returncode:
            raise FetchError("pip install failed")
        install_flash(v)
    return venv_python(v)


def install_flash(v):
    """mido and python-rtmidi into the venv, when it lacks them (an older venv gets them on the next setup). Not
    fatal: without them the builder works and `flash` says how to get them"""
    if venv_has_midi(v):
        return
    print(f"setup: installing {REQUIREMENTS_FLASH.relative_to(ROOT).as_posix()} (mido, python-rtmidi: flashing an FM-1)")
    cmd = [str(venv_python(v)), "-m", "pip", "install", "--quiet", "--disable-pip-version-check",
           "-r", str(REQUIREMENTS_FLASH)]
    if subprocess.run(cmd).returncode:
        print("setup: mido / python-rtmidi did not install: the builder works, `flash` does not "
              "(pip install mido python-rtmidi, or a C++ compiler for python-rtmidi on this platform)")


# ---- downloads

def download(url, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")
    try:
        with urllib.request.urlopen(url, timeout=60) as r, open(tmp, "wb") as f:
            while True:
                chunk = r.read(1 << 16)
                if not chunk:
                    break
                f.write(chunk)
    except (OSError, http.client.HTTPException) as e:
        tmp.unlink(missing_ok=True)
        raise FetchError(f"download {url}: {e}")
    os.replace(tmp, dest)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _sdk_bad(tools):
    """-> the SDK files under tools (cpu/wl82/tools) that are missing or not the pinned version's"""
    return [rel for rel, want in TC.SDK_SHA256.items()
            if not (tools / rel).is_file() or sha256(tools / rel) != want]


def fetch_sdk(root):
    """the three SDK files into root/cpu/wl82/tools (kept when present and right; under the install lock, so
    worktrees fetching into the same main checkout's sdk/ do it once)"""
    tools = Path(root) / "cpu" / "wl82" / "tools"
    if not _sdk_bad(tools):
        return Path(root)
    with shared.install_lock(root, "sdk"):
        for rel in _sdk_bad(tools):                 # (again under the lock: the one we waited for may have done it)
            dest = tools / rel
            print(f"setup: fetching SDK {rel}")
            download(f"{TC.SDK_URL}/{rel}", dest)
            if sha256(dest) != TC.SDK_SHA256[rel]:
                dest.unlink()
                raise FetchError(f"SDK {rel}: the download does not match {TC.SDK_TAG}")
    return Path(root)


def fetch_llvm(home=None):
    """pi32v2-llvm's drop-in for this host (the pinned archive, tools/toolchain.py LLVM_ASSETS) into home (default: the
    repository's build/toolchains, tools/repo_root.py) as home/pi32v2-llvm-dropin-<version>"""
    asset = TC.llvm_asset()
    if asset is None:
        raise FetchError(TC._missing_hint(None))
    name, want, mb = asset
    home = Path(home or repo_root.toolchains_dir())
    top = f"pi32v2-llvm-dropin-{TC.LLVM_VERSION}"
    if TC.has_clang(home / top) and not TC.runs_here(home / top):
        print(f"setup: the pi32v2-llvm in {home / top} is for another host: replacing it")
        shutil.rmtree(home / top)
    if not TC.has_clang(home / top):
        home.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=home) as tmp:
            archive = Path(tmp) / name
            print(f"setup: downloading pi32v2-llvm {TC.LLVM_VERSION} ({mb} MB) into {home}")
            download(f"{TC.LLVM_RELEASE_URL}/{name}", archive)
            if sha256(archive) != want:
                raise FetchError("the pi32v2-llvm archive does not match its SHA-256")
            try:
                extract(archive, home)
            except tarfile.TarError as e:
                raise FetchError(f"the pi32v2-llvm archive: {e}")
    return home / top


def extract(archive, dest, data_filter=None):
    """untar inside dest only: tarfile's data filter, or (Pythons without it; data_filter=False) a check of
    every member path first"""
    if data_filter is None:
        data_filter = hasattr(tarfile, "data_filter")
    with tarfile.open(archive) as t:
        if data_filter:
            t.extractall(dest, filter="data")
            return
        root = Path(dest).resolve()
        for m in t.getmembers():
            target = (root / m.name).resolve()
            if root not in (target, *target.parents) or m.isdev():
                raise tarfile.TarError(f"{m.name}: outside the destination, or a device")
            if m.issym() and Path(m.linkname).is_absolute():
                raise tarfile.TarError(f"{m.name}: an absolute link")
        t.extractall(dest)

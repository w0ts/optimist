# SPDX-License-Identifier: GPL-3.0-only
"""Fetch what the build needs, on any host, without sh, curl or tar (python tools/optimist.py setup):

  the builder's venv      tools/builder/venv (BUILDER_VENV) with tools/requirements.txt (Textual, the pinned Pillow)
  the SDK files           three files of JieLi's AC79 SDK (Apache-2.0), checked against their SHA-256
  the toolchain           Linux x86-64: JieLi's archive (pinned version, SHA-256) into ~/.jieli
  the toolchain image     elsewhere: docker build tools/docker (the toolchain is downloaded inside, never pushed)
"""
import hashlib
import http.client
import os
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

import toolchain as TC

ROOT = TC.ROOT
REQUIREMENTS = ROOT / "tools" / "requirements.txt"


class FetchError(Exception):
    pass


# ---- the builder's venv

def venv_dir():
    return Path(os.environ.get("BUILDER_VENV", ROOT / "tools" / "builder" / "venv"))


def venv_python(venv=None):
    v = venv or venv_dir()
    return v / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def venv_ready(venv=None):
    py = venv_python(venv)
    try:                            # (some Docker mounts refuse to stat a venv's symlinked python: not ready)
        return py.exists() and subprocess.run([str(py), "-c", "import textual, PIL"],
                                              capture_output=True).returncode == 0
    except OSError:
        return False


def make_venv(venv=None):
    v = venv or venv_dir()
    if not venv_python(v).exists():
        print(f"setup: making {v}")
        if subprocess.run([sys.executable, "-m", "venv", str(v)]).returncode:
            raise FetchError(f"python -m venv {v} failed (Debian / Ubuntu: apt install python3-venv)")
    print(f"setup: installing {REQUIREMENTS.relative_to(ROOT).as_posix()} into the venv")
    cmd = [str(venv_python(v)), "-m", "pip", "install", "--quiet", "--disable-pip-version-check",
           "-r", str(REQUIREMENTS)]
    if subprocess.run(cmd).returncode:
        raise FetchError("pip install failed")
    return venv_python(v)


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


def fetch_sdk(root):
    """the three SDK files into root/cpu/wl82/tools (kept when present and right)"""
    tools = Path(root) / "cpu" / "wl82" / "tools"
    for rel, want in TC.SDK_SHA256.items():
        dest = tools / rel
        if dest.is_file() and sha256(dest) == want:
            continue
        print(f"setup: fetching SDK {rel}")
        download(f"{TC.SDK_URL}/{rel}", dest)
        if sha256(dest) != want:
            dest.unlink()
            raise FetchError(f"SDK {rel}: the download does not match {TC.SDK_TAG}")
    return Path(root)


def fetch_toolchain(home=None):
    """JieLi's archive (the pinned version) into home (~/.jieli), linked as home/toolchain -> the directory"""
    home = Path(home or TC.JIELI_HOME)
    name = f"jieli-linux-toolchains-{TC.TOOLCHAIN_VERSION}"
    if not TC.has_clang(home / name):
        home.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=home) as tmp:
            archive = Path(tmp) / f"{name}.tar.xz"
            print(f"setup: downloading the JieLi toolchain {TC.TOOLCHAIN_VERSION} (26 MB)")
            download(TC.TOOLCHAIN_URL, archive)
            if sha256(archive) != TC.TOOLCHAIN_SHA256:
                raise FetchError("the toolchain archive does not match its SHA-256")
            try:
                extract(archive, home)
            except tarfile.TarError as e:
                raise FetchError(f"the toolchain archive: {e}")
    link = home / "toolchain"
    if link.is_symlink() or not link.exists():
        link.unlink(missing_ok=True)
        link.symlink_to(name)
    return link


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


def build_image(image=None):
    """docker build tools/docker -> the toolchain image (downloads the toolchain inside; a few minutes)"""
    image = image or TC.image_name()
    print(f"setup: docker build -t {image} tools/docker (the toolchain is downloaded from JieLi's server)")
    cmd = ["docker", "build", "--platform", "linux/amd64", "-t", image, str(TC.DOCKERFILE_DIR)]
    if subprocess.run(cmd).returncode:
        raise FetchError("docker build failed")
    return image

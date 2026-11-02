# SPDX-License-Identifier: GPL-3.0-only
"""The build's install settings beside its package: PACKAGE.install.json (tools/build.py writes it, the installers read
it). Today one setting, the builder's BOOTLOADER (keep / m-bootloader: tools/builder/registry.py).

Why a sidecar and not the package: the .fwsc is the image the FM-1's update loaders read (their CRCs, the header's
total size, M-Flasher's parse): a byte added to it is a byte every loader sees. A sidecar changes no package; it
names the package's SHA-256, so a sidecar left beside another package is not taken for it. The web installer reads
the same value from a cloud build's .config (BOOTLOADER=1), which the shelf keeps beside each package.

  {"format": 1, "package": "optimist-0.3.fwsc", "sha256": "...", "bootloader": "keep" | "m-bootloader",
   "config": "mots-config", "config_hash": "1a2b3c4d"}"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

FORMAT = 1
VALUES = {0: "keep", 1: "m-bootloader"}


def sidecar(package) -> Path:
    p = Path(package)
    return p.with_name(p.stem + ".install.json")


def write(package, bootloader: int, config: str = "", config_hash: str = "") -> Path:
    p = Path(package)
    meta = {"format": FORMAT, "package": p.name, "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
            "bootloader": VALUES[int(bootloader)], "config": config, "config_hash": config_hash}
    out = sidecar(p)
    out.write_text(json.dumps(meta, indent=2) + "\n")
    return out


def read(package) -> tuple:
    """-> (bootloader or None, note): None when there is no sidecar, or it is for another package / malformed (the
    note says which)"""
    p = Path(package)
    s = sidecar(p)
    try:
        meta = json.loads(s.read_text())
    except FileNotFoundError:
        return None, "no build settings beside the package"
    except (OSError, ValueError) as e:
        return None, f"{s.name}: unreadable ({e})"
    if not isinstance(meta, dict) or meta.get("format") != FORMAT or meta.get("bootloader") not in VALUES.values():
        return None, f"{s.name}: not an install settings file of this format"
    try:
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
    except OSError as e:
        return None, f"{p.name}: {e}"
    if meta.get("sha256") != digest:
        return None, f"{s.name} is for another package (its SHA-256 differs): ignored"
    return meta["bootloader"], f"from {s.name} ({meta.get('config') or 'its build'})"


def from_config_text(text: str):
    """BOOTLOADER in a builder .config: "keep" / "m-bootloader" (an absent line is the default, m-bootloader)"""
    m = re.search(r"^\s*BOOTLOADER\s*=\s*(-?\d+)\s*$", text, re.M)
    return VALUES.get(int(m[1]), "keep") if m else "m-bootloader"

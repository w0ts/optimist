# SPDX-License-Identifier: GPL-3.0-only
"""Where `make sdcard` writes (tools/sd_card.py): the mounted removable FAT volumes (macOS diskutil, Linux lsblk or
/proc/mounts, Windows drive letters: whichever applies), the safety checks on a destination, the last destinations
used (build/sdcard-last.json, git-ignored: up to five, most recent first, a card known by its volume UUID whatever its
mount name) and the picker that asks which one (the user, 2026-11-04: "it should ask which place to put the stuff",
"remember where was the last locations we used")."""
import ctypes
import json
import os
import plistlib
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAST_FILE = ROOT / "build" / "sdcard-last.json"
LAST_MAX = 5
FAT_TYPES = {"msdos", "vfat", "fat", "fat16", "fat32"}   # (the firmware's FatFs: FAT12/16/32, no exFAT: ffconf.h)


@dataclass(frozen=True)
class Volume:
    path: str                                # its mount point (a folder's path for a plain folder)
    name: str
    uuid: str                                # "" none (a plain folder)
    fs: str
    size: int
    free: int
    removable: bool


def _usage(path):
    """-> (size, free) of the file system holding path (0, 0 when it cannot be read)"""
    try:
        u = shutil.disk_usage(path)
        return u.total, u.free
    except OSError:
        return 0, 0


def _mac_info(path):
    """-> diskutil's plist for path, {} when diskutil cannot tell"""
    try:
        r = subprocess.run(["diskutil", "info", "-plist", str(path)], capture_output=True, check=True, timeout=20)
        return plistlib.loads(r.stdout)
    except (OSError, subprocess.SubprocessError, plistlib.InvalidFileException):
        return {}


def _mac_volume(info):
    """a diskutil plist -> Volume, None when it is not mounted"""
    mp = info.get("MountPoint") or ""
    if not mp:
        return None
    size, free = _usage(mp)
    removable = bool(info.get("RemovableMedia") or info.get("Removable") or info.get("Ejectable")) or \
        not info.get("Internal", True)
    return Volume(mp, info.get("VolumeName") or Path(mp).name, info.get("VolumeUUID") or info.get("DiskUUID") or "",
                  (info.get("FilesystemType") or "").lower(), size, free, removable)


def _mac_volumes():
    out = []
    vols = Path("/Volumes")
    for p in sorted(vols.iterdir()) if vols.is_dir() else []:
        v = _mac_volume(_mac_info(p))
        if v:
            out.append(v)
    return out


def _linux_volumes():
    out = []
    try:
        r = subprocess.run(["lsblk", "-J", "-b", "-o", "NAME,MOUNTPOINT,FSTYPE,RM,HOTPLUG,LABEL,UUID"],
                           capture_output=True, check=True, timeout=20, text=True)
        devs = json.loads(r.stdout).get("blockdevices", [])
    except (OSError, subprocess.SubprocessError, ValueError):
        devs = None
    if devs is not None:
        stack = list(devs)
        while stack:
            d = stack.pop()
            stack += d.get("children") or []
            mp = d.get("mountpoint")
            if mp:
                size, free = _usage(mp)
                out.append(Volume(mp, d.get("label") or Path(mp).name, d.get("uuid") or "", (d.get("fstype") or "").lower(),
                                  size, free, str(d.get("rm")) in ("1", "True", "true") or
                                  str(d.get("hotplug")) in ("1", "True", "true")))
        return sorted(out, key=lambda v: v.path)
    try:
        lines = Path("/proc/mounts").read_text().splitlines()
    except OSError:
        return []
    for ln in lines:
        f = ln.split()
        if len(f) >= 3 and f[2].lower() in FAT_TYPES:
            mp = f[1].replace("\\040", " ")
            size, free = _usage(mp)
            out.append(Volume(mp, Path(mp).name, "", f[2].lower(), size, free, mp.startswith(("/media", "/run/media"))))
    return out


def _windows_volumes():
    out = []
    k32 = ctypes.windll.kernel32                                   # (only on Windows: _platform_volumes)
    mask = k32.GetLogicalDrives()
    for i in range(26):
        if not mask >> i & 1:
            continue
        root = f"{chr(65 + i)}:\\"
        name, fs, serial = ctypes.create_unicode_buffer(261), ctypes.create_unicode_buffer(261), ctypes.c_uint32()
        if not k32.GetVolumeInformationW(root, name, 261, ctypes.byref(serial), None, None, fs, 261):
            continue
        size, free = _usage(root)
        out.append(Volume(root, name.value or root, f"{serial.value:08X}", fs.value.lower(), size, free,
                          k32.GetDriveTypeW(root) == 2))
    return out


def platform_volumes():
    """-> every mounted volume this host lists (its FAT ones are the candidates: candidates())"""
    if sys.platform == "darwin":
        return _mac_volumes()
    if sys.platform.startswith("win"):
        return _windows_volumes()
    return _linux_volumes()


def candidates(volumes=None):
    """-> the mounted removable FAT volumes"""
    vols = platform_volumes() if volumes is None else volumes
    return [v for v in vols if v.fs in FAT_TYPES and v.removable]


def volume_at(path, volumes=None):
    """-> the volume mounted exactly at path, None when path is no volume's root"""
    p = os.path.realpath(str(path))
    vols = platform_volumes() if volumes is None else volumes
    return next((v for v in vols if os.path.realpath(v.path) == p), None)


def check_dest(path, volumes=None, folder_ok=False, home=None, repo=ROOT):
    """a destination's safety -> (ok, message). Refused: not a folder, /, the home, the repo or inside it; else a
    mounted FAT volume's root (folder_ok: a plain folder too, with a warning)"""
    p = Path(os.path.realpath(str(path)))
    home = Path(os.path.realpath(str(home or Path.home())))
    repo = Path(os.path.realpath(str(repo)))
    if not p.is_dir():
        return False, f"{path}: not a folder"
    if p == Path(p.anchor):
        return False, f"{path}: the file system's root, not a card"
    if p == home:
        return False, f"{path}: the home folder, not a card"
    if p == repo or repo in p.parents:
        return False, f"{path}: the repo (or inside it), not a card"
    v = volume_at(p, volumes)
    if v and v.fs in FAT_TYPES:
        return True, f"{v.name} ({v.fs}, {human(v.size)}, {human(v.free)} free)"
    if folder_ok:
        return True, f"warning: {path} is a plain folder, not a FAT card: copy its Optimist folder to a card later"
    if v:
        return False, f"{path}: a {v.fs or 'non-FAT'} volume, the FM-1 reads FAT (FAT16 / FAT32) only"
    return False, f"{path}: not a mounted volume's root (a card's is /Volumes/<name> on a Mac)"


def human(n):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0
    return f"{n} B"


# ---- the last destinations ----
def load_last(file=LAST_FILE):
    """-> [{path, name, uuid, when}], most recent first ([] when none or unreadable)"""
    try:
        data = json.loads(Path(file).read_text())
    except (OSError, ValueError):
        return []
    return [e for e in data if isinstance(e, dict) and isinstance(e.get("path"), str)][:LAST_MAX] \
        if isinstance(data, list) else []


def remember(path, vol, file=LAST_FILE, now=None):
    """path (on volume vol, None a plain folder) first in the list, the same card or folder once, five at most"""
    e = {"path": os.path.realpath(str(path)), "name": vol.name if vol else Path(path).name,
         "uuid": vol.uuid if vol else "", "when": time.strftime("%Y-%m-%d %H:%M", time.localtime(now))}
    same = (lambda o: o.get("uuid") == e["uuid"]) if e["uuid"] else (lambda o: not o.get("uuid") and o["path"] == e["path"])
    rest = [o for o in load_last(file) if not same(o)]
    Path(file).parent.mkdir(parents=True, exist_ok=True)
    Path(file).write_text(json.dumps([e] + rest[:LAST_MAX - 1], indent=1) + "\n")


# ---- the picker ----
@dataclass(frozen=True)
class Choice:
    label: str
    path: str                                # "" not pickable (a past card not mounted now)
    last: bool


def choices(cands, last):
    """-> the picker's rows: the remembered ones first (their card found by UUID under any mount name, a folder by its
    path; not mounted now: shown, not pickable), then the other candidates"""
    rows, used = [], set()
    for k, e in enumerate(last):
        v = next((c for c in cands if e.get("uuid") and c.uuid == e["uuid"]), None)
        if v:
            used.add(v.path)
            rows.append(Choice(_vol_label(v) + f"  (used {e.get('when', '?')})", v.path, k == 0))
        elif not e.get("uuid") and Path(e["path"]).is_dir():
            rows.append(Choice(f"{e['path']}  folder  (used {e.get('when', '?')})", e["path"], k == 0))
        else:
            rows.append(Choice(f"{e.get('name') or e['path']}  not mounted now  (used {e.get('when', '?')})", "", False))
    rows += [Choice(_vol_label(v), v.path, False) for v in cands if v.path not in used]
    return rows


def _vol_label(v):
    has = "has Optimist/" if (Path(v.path) / "Optimist").is_dir() else "no Optimist/ yet"
    return f"{v.name}  {v.path}  {human(v.size)}, {human(v.free)} free, {has}"


def pick(cands, last, ask=input, say=print):
    """the question -> the chosen path (a folder typed: as typed), None cancelled. Enter takes the last one used
    (marked "(last)"); q or 0 cancels"""
    rows = choices(cands, last)
    say("Where should the SD card's content go?")
    for i, r in enumerate(rows, 1):
        say(f"  {i}. {r.label}{'  (last)' if r.last else ''}")
    say(f"  {len(rows) + 1}. another folder (type a path)")
    pre = next((i for i, r in enumerate(rows, 1) if r.last and r.path), 0)
    hint = f"Enter = {pre} (last), " if pre else ""
    while True:
        a = ask(f"Pick 1-{len(rows) + 1} ({hint}q or 0 cancels): ").strip()
        if a == "" and pre:
            return rows[pre - 1].path
        if a.lower() in ("q", "0"):
            return None
        if a.isdigit() and 1 <= int(a) <= len(rows) and rows[int(a) - 1].path:
            return rows[int(a) - 1].path
        if a == str(len(rows) + 1):
            p = ask("The folder's path (Enter cancels): ").strip()
            return os.path.expanduser(p) if p else None
        say("  not one of the choices" + (" (that card is not mounted now)" if a.isdigit() and
                                          1 <= int(a) <= len(rows) else ""))

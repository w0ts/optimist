#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install any FM-1 package over any FM-1 firmware: the running firmware's update loader where it takes the package
(tools/fm1_install.py, the normal way), else the chip's USB download mode through M-Flasher's command line
(third_party/m-flasher, Julius Vitkauskas, MIT). The way is chosen per (running firmware, package, the build's
BOOTLOADER): tools/fm1_route.py, docs/INSTALL-PATHS.md.

  fm1_install.py PACKAGE.fwsc [--keep-bootloader] [--dry-run [--flash-image BACKUP.bin]] [--route download]
                                               an install: the route chosen there, the download-mode part here
  fm1_anyflash.py backup | restore BACKUP.bin | bootloader [--stock PACKAGE.fwsc] | restart   (download mode,
                                               M-Flasher's own plan and question; --dry-run prints the command)
  fm1_anyflash.py setup [--from-source]        M-Flasher: its pinned release (or built from the submodule)
  fm1_anyflash.py paths                        the install-path matrix

Every write in download mode: the model check (an FM-1, from M-Flasher's read of the flash), a full backup first
(its path printed), the plan shown (the sectors, ours and M-Flasher's own, compared), and a typed "yes" (or --yes,
given by a caller that asked already: make flash, the builder's f). --dry-run sends nothing to the FM-1 (no soft key,
no M-Flasher) and prints what would be run and written where; with --flash-image (a backup) the exact sectors.
The bootloader is left as it is unless the build says m-bootloader (and --keep-bootloader does not say keep).

m-bootloader's coming SD card flashing (a .fwsc from the card) is not a path here yet: sd_card_route() is its hook."""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

import fm1_layouts as L
import fm1_rescue
import fm1_route as R
import install_meta
import mflasher as MF

EXIT = {"usage": 2, "badpkg": 2, "notfound": 3, "lost": 4, "noloader": 5, "model": 6, "refused": 7, "nomf": 8,
        "planmismatch": 9, "cancel": 1}


class AnyError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


class Env:
    """what the flow needs from the outside (the tests replace each): M-Flasher, the FM-1 over MIDI, the user"""

    def __init__(self, exe=None, out=sys.stdout, ask=None, soft_key=None, wait_back=None, sleep=time.sleep,
                 run=MF.run):
        self.exe, self.out, self.sleep, self.run_mf = exe, out, sleep, run
        self.ask = ask or ask_tty
        self.soft_key = soft_key            # () -> True when the message was sent (the running firmware's link)
        self.wait_back = wait_back          # (product) -> the identity the FM-1 came back with, or None

    def say(self, s=""):
        print(s, file=self.out, flush=True)

    def mf(self, cmd, **kw):
        return MF.argv(self.exe or "m-flasher", cmd, **kw)


def ask_tty(prompt):
    try:
        return input(prompt).strip().lower()
    except EOFError:
        return ""


# ------------------------------------------------------------------ the package ---

def load(path):
    """-> (raw, PackageInfo, flash.bin or None); AnyError when it is not an FM-1 package"""
    try:
        raw = Path(path).read_bytes()
    except OSError as e:
        raise AnyError("badpkg", f"cannot read {path}: {e.strerror}")
    try:
        flash = fm1_rescue.package_flash_image(raw)
    except fm1_rescue.Fail as e:
        if "no flash entry" not in str(e):
            raise AnyError("badpkg", f"{path}: {e}")
        flash = None
    info = R.package_kind(raw, len(flash) if flash else None)
    if not re.fullmatch(r"FM-1_\d+", info.product):
        raise AnyError("model", f"{path}: identity {info.product!r}: not an FM-1 package")
    bad = None if info.kind == R.P_V15 else fm1_rescue.fm1_install.package_damage(fm1_rescue.fm1_install.logical_image(raw))
    if bad:
        raise AnyError("badpkg", f"{path}: damaged package ({bad}); nothing was written. Download it again")
    return raw, info, flash


def build_setting(package, keep_flag):
    value, note = install_meta.read(package)
    choice, line = R.bootloader_choice(value, keep_flag)
    return choice, line + ("" if value else f" ({note})")


# ------------------------------------------------------------ download mode, by M-Flasher ---

def need_mflasher(env):
    if env.exe is None:
        env.exe = MF.binary()
    if env.exe is None:
        raise AnyError("nomf", "M-Flasher is not installed: python tools/fm1_anyflash.py setup (its pinned release "
                               f"{MF.VERSION}), or put M_FLASHER=/path/to/m-flasher")


def mf(env, cmd, answer=None, capture=True, **kw):
    args = env.mf(cmd, **kw)
    env.say("$ " + " ".join(args))
    rc, text = env.run_mf(args, answer=answer, capture=capture)
    return rc, text


def find_device(env):
    rc, text = mf(env, "find")
    return [ln for ln in text.splitlines() if ln.strip() and not ln.startswith("midi")] if rc == 0 else []


def enter_download_mode(env, route, secs=20.0):
    if find_device(env):
        env.say("the FM-1 is in download mode already")
        return
    if route.soft_key and env.soft_key and env.soft_key():
        env.say("sent the soft key (F0 22 24 35 7D F7) over USB-MIDI; waiting for download mode")
    else:
        env.say(f"put the FM-1 in download mode: {route.enter}")
    end = time.monotonic() + secs
    while time.monotonic() < end:
        env.sleep(0.5)
        if find_device(env):
            return
    raise AnyError("notfound", f"no FM-1 in download mode after {secs:.0f} s ({route.enter}); nothing was written")


def read_info(env):
    rc, text = mf(env, "info")
    info = MF.parse_info(text)
    if rc or not info:
        raise AnyError("lost", f"M-Flasher could not read the FM-1 (exit {rc}); nothing was written")
    model = info.get("model", "unknown")
    if model != "FM-1":
        raise AnyError("model", f"M-Flasher reads the model as {model!r}, not an FM-1: nothing is written "
                                "(an SMK-37 or an uncertain model: use M-Flasher itself, with --board if you know it)")
    return info


def backup(env, folder):
    rc, text = mf(env, "backup", arg=str(folder))
    path = MF.backup_path(text)
    if rc or not path or not Path(path).is_file():
        raise AnyError("lost", f"the backup failed (exit {rc}); nothing was written")
    data = Path(path).read_bytes()
    if len(data) != L.FLASH_SIZE:
        raise AnyError("lost", f"the backup {path} is {len(data)} B, not 1 MiB; nothing was written")
    env.say(f"backup: {path}  (keep it: `fm1_anyflash.py restore {path}` puts this flash back)")
    return Path(path), data


RANGE_RE = re.compile(r"0x([0-9A-F]{5})-0x([0-9A-F]{5})")


def mf_plan_sectors(text):
    """M-Flasher's plan ("Writes N sectors (K KiB): 0x04000-0x04FFF, ...") -> the sector addresses; () for "Nothing
    to write"; None when it printed no plan"""
    if "Nothing to write" in text:
        return ()
    m = re.search(r"^Writes \d+ sectors \(\d+ KiB\): (.*)$", text, re.M)
    if not m:
        return None
    out = []
    for a, b in RANGE_RE.findall(m[1]):
        out += range(int(a, 16), int(b, 16) + 1, L.SECTOR)
    return tuple(out)


def confirm(env, yes, what):
    if yes:
        return True
    return env.ask(f"{what}\nType yes to write (anything else: nothing is written): ") == "yes"


def planned_write(env, cmd, arg, ours, backup_dir, yes, restart):
    """M-Flasher's own plan first (its question answered no: nothing written), compared with ours; then the write"""
    rc, text = mf(env, cmd, arg=arg, answer="no\n", backup_dir=str(backup_dir))
    theirs = mf_plan_sectors(text)
    if rc or theirs is None:
        raise AnyError("refused", f"M-Flasher refused the plan (exit {rc}); nothing was written")
    if tuple(sorted(theirs)) != tuple(sorted(ours)):
        raise AnyError("planmismatch", f"M-Flasher would write {len(theirs)} sectors, this installer planned {len(ours)}: "
                                   "nothing was written (send this output)")
    if not ours:
        env.say("nothing to write: the flash holds this already")
        return False
    if not confirm(env, yes, f"{len(ours)} sectors, as planned above (M-Flasher's plan agrees)"):
        raise AnyError("cancel", "cancelled: nothing was written")
    rc, _ = mf(env, cmd, arg=arg, backup_dir=str(backup_dir), yes=True, restart=restart)
    if rc:
        raise AnyError("lost", f"M-Flasher stopped (exit {rc}): see its lines above. Do not switch the FM-1 off; "
                               "run the same command again, or restore the backup")
    return True


def install_mboot(env, info, backup_dir, yes):
    if info.get("bootloader", "").startswith("m-bootloader"):
        env.say(f"bootloader: {info['bootloader']} found: kept")
        return False
    env.say(f"bootloader: {info.get('bootloader', '?')} -> m-bootloader {MF.MBOOT_RELEASE['version']} "
            "(the boot area 0x00000-0x03FFF only; the unit's configuration in it kept)")
    if not confirm(env, yes, "install m-bootloader now?"):
        raise AnyError("cancel", "cancelled: nothing was written")
    rc, _ = mf(env, "bootloader", backup_dir=str(backup_dir), yes=True)
    if rc:
        raise AnyError("lost", f"M-Flasher stopped (exit {rc}) installing m-bootloader: do not switch the FM-1 off; "
                               "run it again, or restore the backup")
    return True


def write_target(backup_bin, target, pkg_name):
    """BACKUP-install.bin: the whole flash after the plan, beside the backup, with the backup's .json (its unit id:
    M-Flasher restores a backup of the same unit whole)"""
    out = backup_bin.with_name(backup_bin.stem + "-install.bin")
    out.write_bytes(target)
    meta = {}
    try:
        meta = json.loads(backup_bin.with_suffix(".json").read_text())
    except (OSError, ValueError):
        pass
    meta["note"] = f"{backup_bin.name} with {pkg_name} planned by tools/fm1_anyflash.py (not a backup)"
    out.with_suffix(".json").write_text(json.dumps(meta, indent=2) + "\n")
    return out


def firmware_step(env, pkg_path, flash, current, backup_bin, backup_dir, yes, info, wipe=""):
    if fm1_rescue.package_chip_key(flash) != int(info.get("chip key", "0x980F"), 16):
        raise AnyError("model", "the package is built for another chip key than this FM-1's; nothing was written")
    p = L.plan(current, flash, wipe=wipe)
    for ln in p.describe():
        env.say(ln)
    if not p.clears:                                 # M-Flasher's own rule is enough: the package's area
        return planned_write(env, "firmware", str(pkg_path), p.writes, backup_dir, yes, True)
    target = L.target_image(current, flash, p)       # a switch: the whole planned flash, restored
    t = write_target(backup_bin, target, Path(pkg_path).name)
    env.say(f"planned flash: {t}")
    return planned_write(env, "restore", str(t), p.sectors, backup_dir, yes, True)


def download_install(env, route, pkg_path, flash, backup_dir, yes, product, wipe=""):
    """wipe: the new firmware's settings to erase too (fm1_layouts.WIPE key; "" keeps them)"""
    need_mflasher(env)
    enter_download_mode(env, route)
    info = read_info(env)
    for k in ("model", "bootloader", "firmware"):
        env.say(f"  {k:10} {info.get(k, '?')}")
    backup_bin, current = backup(env, backup_dir)
    if route.install_mboot and install_mboot(env, info, backup_dir, yes):
        backup_bin, current = backup(env, backup_dir)          # (the boot area changed: plan on the flash as it is)
    if flash is None:
        mf(env, "restart")
        return info
    firmware_step(env, pkg_path, flash, current, backup_bin, backup_dir, yes, info, wipe)
    if env.wait_back:
        back = env.wait_back(product)
        env.say(f"done: the FM-1 runs {back}" if back else "written and verified; the FM-1 did not show up on "
                "USB-MIDI yet: switch it off and on")
    return info


def sd_card_route(pkg_path):
    """the hook for m-bootloader's coming SD card flashing (a .fwsc read from the card at power-on): not built.
    When it is: copy the package to the card under the name m-bootloader asks for, and a Route("sdcard")"""
    raise AnyError("usage", "m-bootloader's SD card flashing is not released yet: not a path here")


# ------------------------------------------------------------------------ dry run ---

def dry_run(env, route, info, flash, backup_dir, flash_image, wipe=""):
    """(the route's line is describe()'s)"""
    if route.method == "refused":
        return
    for i, s in enumerate(route.steps, 1):
        env.say(f"  {i}. {s}")
    if route.method == "loader":
        env.say(f"writes:   flash.bin [0x04000, {info.flash_len or 0:#07x}) through the running firmware's loader "
                "(its own layout switch clears what the other layout left); the bootloader is not touched")
        return
    exe = env.exe or MF.binary() or "m-flasher"
    cmds = [MF.argv(exe, "find"), MF.argv(exe, "info"), MF.argv(exe, "backup", arg=str(backup_dir))]
    if route.install_mboot:
        cmds.append(MF.argv(exe, "bootloader", backup_dir=str(backup_dir), yes=True) + ["(if not there)"])
    if flash is not None:
        cmds.append(MF.argv(exe, "firmware", arg="PACKAGE", backup_dir=str(backup_dir), yes=True, restart=True)
                    + ["(or restore of the planned flash on a layout switch)"])
    env.say("M-Flasher would run (nothing is run now):")
    for c in cmds:
        env.say("  " + " ".join(map(str, c)))
    if flash is not None and flash_image:
        cur = Path(flash_image).read_bytes()
        p = L.plan(cur, flash, wipe=wipe)
        env.say(f"against {flash_image}:")
        for ln in p.describe():
            env.say("  " + ln)
        env.say(f"  then: {'restore of the planned flash' if p.clears else 'firmware PACKAGE'}")
    elif flash is not None:
        env.say(f"writes at most: [0x04000, {len(flash):#07x}) and a layout switch's clear, up to 0xDA000; never "
                "0x00000-0x03FFF, 0xE0000-0xFFFFF (give --flash-image BACKUP.bin for the exact sectors)")


# --------------------------------------------------------------------------- install ---

def plan_install(package, running_text, port="", keep_flag=False, mboot_present=None):
    """-> (Route, PackageInfo, flash.bin, bootloader line): what install would do, nothing sent"""
    raw, info, flash = load(package)
    choice, line = build_setting(package, keep_flag)
    running = R.running_kind(running_text, port)
    route = R.choose(running, info, choice, mboot_present, running_text or "")
    return route, info, flash, line


def default_backup_dir():
    return Path.home() / "Documents" / "M-Flasher" / "backups"


def describe(env, route, info, running_text, port, line):
    env.say(f"kind:     {R.PACKAGES[info.kind]}")
    env.say(f"running:  {running_text or 'nothing on USB-MIDI'}: {R.FIRMWARES[R.running_kind(running_text, port)][0]}")
    env.say(line)
    env.say(f"route:    {route.method}: {route.why}")


def main(argv=None, env=None):
    """the download-mode services and setup (an install: tools/fm1_install.py, which calls install_route)"""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for q in (sub.add_parser("backup"), sub.add_parser("bootloader"), sub.add_parser("restore"), sub.add_parser("restart")):
        q.add_argument("--dry-run", action="store_true")
        q.add_argument("--yes", action="store_true")
        q.add_argument("--port")
        q.add_argument("--backup-dir", type=Path, default=default_backup_dir())
    sub.choices["restore"].add_argument("backup")
    sub.choices["bootloader"].add_argument("--stock", help="put back the stock bootloader of this package")
    s = sub.add_parser("setup")
    s.add_argument("--from-source", action="store_true")
    sub.add_parser("paths")
    a = ap.parse_args(argv)
    env = env or Env()
    try:
        return run(a, env)
    except (AnyError, MF.MFlasherError, L.PlanError, fm1_rescue.Fail) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT.get(getattr(e, "code", "refused"), 1)


def run(a, env):
    if a.cmd == "paths":
        env.say(R.matrix_markdown())
        return 0
    if a.cmd == "setup":
        env.say(f"M-Flasher: {MF.build_from_source() if a.from_source else MF.fetch()}")
        return 0
    if env.soft_key is None:                         # (no MIDI link of ours here: M-Flasher sends the soft key)
        env.soft_key = lambda: env.run_mf(env.mf("download-mode"), answer=None, capture=True)[0] == 0
    return service(a, env)


def service(a, env):
    """backup, restore, bootloader, restart: download mode, M-Flasher's own plan and question"""
    route = R.Route("download", a.cmd, R.entry_for(R.NONE, None)[1], soft_key=True)
    if a.dry_run:
        exe = env.exe or MF.binary() or "m-flasher"
        arg = {"backup": str(a.backup_dir), "restore": getattr(a, "backup", None)}.get(a.cmd)
        env.say("M-Flasher would run (nothing is run now): " + " ".join(MF.argv(exe, a.cmd, arg=arg)))
        return 0
    need_mflasher(env)
    if a.cmd != "restart":
        enter_download_mode(env, route)
        read_info(env)
    if a.cmd == "backup":
        backup(env, a.backup_dir)
        return 0
    if a.cmd == "restart":
        return mf(env, "restart")[0]
    backup(env, a.backup_dir)
    kw = {"boot_file": None} if a.cmd == "bootloader" else {}
    args = dict(arg=a.backup if a.cmd == "restore" else None, backup_dir=str(a.backup_dir), restart=True, **kw)
    if a.cmd == "bootloader" and a.stock:
        args["arg"] = None
        cmdline = env.mf("bootloader", **args) + ["--stock", a.stock]
        env.say("$ " + " ".join(cmdline))
        return env.run_mf(cmdline, answer=None, capture=False)[0]
    return mf(env, a.cmd, capture=False, **args)[0]       # M-Flasher shows its plan and asks for "yes" itself


if __name__ == "__main__":
    sys.exit(main())

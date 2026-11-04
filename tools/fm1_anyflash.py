#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install any FM-1 package over any FM-1 firmware: the running firmware's update loader where it takes the package
(tools/fm1_install.py, the normal way), else the chip's USB download mode through M-Flasher's command line
(third_party/m-flasher, Julius Vitkauskas, MIT). The way is chosen per (running firmware, package, the build's
BOOTLOADER): tools/fm1_route.py, docs/INSTALL-PATHS.md.

  fm1_install.py PACKAGE.fwsc [--install-mboot] [--dry-run [--flash-image BACKUP.bin]] [--route download]
                                               an install: the route chosen there, the download-mode part here
  fm1_anyflash.py backup | restore BACKUP.bin | bootloader [--stock PACKAGE.fwsc] | restart   (download mode,
                                               M-Flasher's own plan and question; --dry-run prints the command)
  fm1_anyflash.py setup [--from-source]        M-Flasher: its pinned release (or built from the submodule)
  fm1_anyflash.py paths                        the install-path matrix

Every write in download mode: the model check (an FM-1, from M-Flasher's read of the flash), a full backup first
(its path printed), the plan shown (the sectors, ours and M-Flasher's own, compared), and a typed "yes" (or --yes,
given by a caller that asked already: make flash, the builder's f). Each M-Flasher run that opens the unit is one
password dialog on macOS (mflasher.OPENS_UNIT), so the checks share runs: an install is the backup and one write
(fm1_route.unit_runs), a restore or a bootloader one run. --dry-run sends nothing to the FM-1 (no soft key,
no M-Flasher) and prints what would be run and written where; with --flash-image (a backup) the exact sectors.
The bootloader is left as it is, whatever the build's BOOTLOADER says; --install-mboot installs m-bootloader (download mode).

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
                 run=MF.run, may_fetch=None, download=None):
        self.exe, self.out, self.sleep, self.run_mf = exe, out, sleep, run
        self.may_fetch = may_fetch          # (question) -> True: download the pinned M-Flasher; None: never (setup does)
        self.download = download            # (url, dest) for MF.fetch: the tests' fake server
        self.asked = False                  # the download question is asked once per run
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


def build_setting(package, install_mboot=False):
    value, note = install_meta.read(package)
    choice, line = R.bootloader_choice(value, install_mboot)
    return choice, line + ("" if value else f" ({note})")


# ------------------------------------------------------------ download mode, by M-Flasher ---

def need_mflasher(env):
    if env.exe is None:
        env.exe = MF.binary()
    if env.exe is None and env.may_fetch is not None and not env.asked:
        env.asked = True
        try:
            env.exe = MF.ensure(env.may_fetch, env.say, env.download)
        except MF.MFlasherError as e:
            raise AnyError("nomf", str(e))
    if env.exe is None:
        raise AnyError("nomf", "M-Flasher is not installed: python tools/fm1_anyflash.py setup (its pinned release "
                               f"{MF.VERSION}), or put M_FLASHER=/path/to/m-flasher")
    env.say(f"M-Flasher: {env.exe}")
    for n in MF.notes():
        env.say("  " + n)


def mf(env, cmd, answer=None, capture=True, decide=None, **kw):
    """one M-Flasher run; decide: its "Type yes" question answered by the caller (checked_write)"""
    args = env.mf(cmd, **kw)
    env.say("$ " + " ".join(args))
    return env.run_mf(args, answer=answer, capture=capture, decide=decide)


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


def check_model(info, unit=None):
    """the model check (an FM-1 exactly) and, for a write after the first read, the same unit (its unit id)"""
    model = info.get("model", "unknown")
    if model != "FM-1":
        raise AnyError("model", f"M-Flasher reads the model as {model!r}, not an FM-1: nothing is written "
                                "(an SMK-37 or an uncertain model: use M-Flasher itself, with --board if you know it)")
    if unit and info.get("unit id") != unit:
        raise AnyError("model", f"M-Flasher reads another unit ({info.get('unit id', '?')}) than the one backed up "
                                f"({unit}): nothing is written")


def backup(env, folder):
    """the first run that opens the unit (one password dialog on macOS): M-Flasher reads the whole flash, prints what it
    reads (the model, bootloader, firmware, chip key, unit id: `info`'s lines) and saves it -> (info, backup, flash).
    The model is checked from those lines: `info` is not run on its own"""
    rc, text = mf(env, "backup", arg=str(folder))
    info = MF.parse_info(text)
    path = MF.backup_path(text)
    if rc or not info or not path or not Path(path).is_file():
        raise AnyError("lost", f"M-Flasher could not read and back up the FM-1 (exit {rc}); nothing was written")
    check_model(info)
    data = Path(path).read_bytes()
    if len(data) != L.FLASH_SIZE:
        raise AnyError("lost", f"the backup {path} is {len(data)} B, not 1 MiB; nothing was written")
    env.say(f"backup: {path}  (keep it: `fm1_anyflash.py restore {path}` puts this flash back)")
    return info, Path(path), data


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


def checked_write(env, cmd, ours, yes, restart, what, unit=None, **kw):
    """one M-Flasher run that reads the flash, plans, writes and verifies (one password dialog on macOS): without --yes,
    so it stops at its own "Type yes" question with the unit open; there its lines are checked (the model, the unit
    backed up, its plan against ours: `ours` the sector addresses, or a callable that judges them), then the user's
    yes (or the caller's --yes), and only then "yes" is answered. Anything else answers "no": nothing written.
    M-Flasher saves a full backup of the flash it read before its first write (src/ops.rs apply). -> written"""
    seen = {}

    def decide(text):
        try:
            check_model(MF.parse_info(text), unit)
        except AnyError as e:
            seen["stop"] = e
            return False
        theirs = mf_plan_sectors(text)
        agree = theirs is not None and (ours(theirs) if callable(ours) else sorted(theirs) == sorted(ours))
        if not agree:
            seen["stop"] = AnyError("planmismatch", f"M-Flasher would write {len(theirs or ())} sectors, not the "
                                                    f"planned ones: nothing was written (send this output)")
            return False
        if not confirm(env, yes, what(len(theirs)) if callable(what) else what):
            seen["stop"] = AnyError("cancel", "cancelled: nothing was written")
            return False
        seen["yes"] = True
        return True
    rc, text = mf(env, cmd, restart=restart, decide=decide, **kw)
    if "stop" in seen:
        raise seen["stop"]
    if "yes" not in seen:                            # no question: refused, or nothing to write
        theirs = mf_plan_sectors(text)
        if rc or theirs is None:
            raise AnyError("refused", f"M-Flasher refused (exit {rc}): see its lines above; nothing was written")
        if theirs or (not callable(ours) and ours):
            raise AnyError("planmismatch", "M-Flasher has nothing to write where this installer planned sectors: "
                                           "nothing was written (send this output)")
        env.say("nothing to write: the flash holds this already")
        return False
    if rc and "Done: " in text:
        raise AnyError("lost", f"written and verified, then M-Flasher stopped (exit {rc}): switch the FM-1 off and on")
    if rc:
        raise AnyError("lost", f"M-Flasher stopped (exit {rc}): see its lines above. Do not switch the FM-1 off; "
                               "run the same command again, or restore the backup")
    saved = MF.backup_path(text)
    env.say(f"M-Flasher's backup before this write: {saved}" if saved else
            "M-Flasher printed no backup path for this write (the backup taken first is above)")
    return True


def needs_mboot(env, info):
    if info.get("bootloader", "").startswith("m-bootloader"):
        env.say(f"bootloader: {info['bootloader']} found: kept")
        return False
    env.say(f"bootloader: {info.get('bootloader', '?')} -> m-bootloader {MF.MBOOT_RELEASE['version']} "
            "(the boot area 0x00000-0x03FFF only; the unit's configuration in it kept), after the firmware")
    return True


def boot_area_only(theirs):
    return bool(theirs) and all(a < L.APP_LO for a in theirs)


def install_mboot(env, backup_dir, yes, unit, restart):
    return checked_write(env, "bootloader", boot_area_only, yes, restart, "install m-bootloader now (M-Flasher's plan "
                         "above: the boot area only)?", unit, backup_dir=str(backup_dir),
                         boot_file=str(MF.MBOOT_BOOT))


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


def firmware_step(env, pkg_path, flash, current, backup_bin, backup_dir, yes, info, wipe="", restart=True):
    if fm1_rescue.package_chip_key(flash) != int(info.get("chip key", "0x980F"), 16):
        raise AnyError("model", "the package is built for another chip key than this FM-1's; nothing was written")
    p = L.plan(current, flash, wipe=wipe)
    for ln in p.describe():
        env.say(ln)
    what = f"{len(p.sectors)} sectors, as planned above (M-Flasher's plan agrees)"
    unit = info.get("unit id")
    if not p.clears:                                 # M-Flasher's own rule is enough: the package's area
        return checked_write(env, "firmware", p.writes, yes, restart, what, unit, arg=str(pkg_path),
                             backup_dir=str(backup_dir))
    target = L.target_image(current, flash, p)       # a switch: the whole planned flash, restored
    t = write_target(backup_bin, target, Path(pkg_path).name)
    env.say(f"planned flash: {t}")
    return checked_write(env, "restore", p.sectors, yes, restart, what, unit, arg=str(t), backup_dir=str(backup_dir))


def download_install(env, route, pkg_path, flash, backup_dir, yes, product, wipe=""):
    """wipe: the new firmware's settings to erase too (fm1_layouts.WIPE key; "" keeps them). The runs that open the
    unit (a password dialog each on macOS): the backup, the firmware's write, m-bootloader's write when asked for
    (fm1_route.unit_runs). Every question is asked before the first write"""
    need_mflasher(env)
    enter_download_mode(env, route)
    info, backup_bin, current = backup(env, backup_dir)
    for k in ("model", "bootloader", "firmware"):
        env.say(f"  {k:10} {info.get(k, '?')}")
    mboot = route.install_mboot and needs_mboot(env, info)
    if mboot and flash is not None and not confirm(env, yes, "install m-bootloader too, after the firmware?"):
        raise AnyError("cancel", "cancelled: nothing was written")
    unit = info.get("unit id")
    if flash is not None:
        firmware_step(env, pkg_path, flash, current, backup_bin, backup_dir, yes, info, wipe, restart=not mboot)
    if mboot:
        try:
            install_mboot(env, backup_dir, yes or flash is not None, unit, restart=True)
        except AnyError as e:
            if flash is None:
                raise
            raise AnyError(e.code, f"the firmware is written and verified; m-bootloader: {e}")
    elif flash is None:
        mf(env, "restart")
        return info
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
    cmds = [MF.argv(exe, "backup", arg=str(backup_dir)) + ["(also the model check: info's lines)"]]
    if flash is not None:
        cmds.append(MF.argv(exe, "firmware", arg="PACKAGE", backup_dir=str(backup_dir), restart=not route.install_mboot)
                    + ["(its plan compared, then yes typed into it; restore of the planned flash on a layout switch)"])
    if route.install_mboot:
        cmds.append(MF.argv(exe, "bootloader", backup_dir=str(backup_dir), restart=True, boot_file=str(MF.MBOOT_BOOT))
                    + ["(if not there)"])
    elif flash is None:
        cmds.append(MF.argv(exe, "restart"))
    env.say("M-Flasher would run (nothing is run now):")
    for c in cmds:
        env.say("  " + " ".join(map(str, c)))
    env.say(f"runs that open the FM-1 (macOS: the admin password each): {len(R.unit_runs(route))} at most")
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

def plan_install(package, running_text, port="", install_mboot=False, mboot_present=None):
    """-> (Route, PackageInfo, flash.bin, bootloader line): what install would do, nothing sent"""
    raw, info, flash = load(package)
    choice, line = build_setting(package, install_mboot)
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
    """backup, restore, bootloader, restart: download mode, one M-Flasher run each (one password dialog on macOS). A
    restore or bootloader run reads the flash, saves its full backup before its first write (M-Flasher's src/ops.rs
    apply), shows its plan and stops at its question: the model is checked there (an FM-1), the plan shown, the user's
    yes asked here (or --yes), and only then "yes" answered"""
    route = R.Route("download", a.cmd, R.entry_for(R.NONE, None)[1], soft_key=True)
    stock = getattr(a, "stock", None)
    if a.cmd == "restart":
        kw = {}
    elif a.cmd == "backup":
        kw = {"arg": str(a.backup_dir)}
    else:
        kw = {"arg": getattr(a, "backup", None), "backup_dir": str(a.backup_dir), "stock": stock}
        if a.cmd == "bootloader" and not stock:
            kw["boot_file"] = str(MF.MBOOT_BOOT)
    if a.dry_run:
        exe = env.exe or MF.binary() or "m-flasher"
        restart = {"restart": True} if a.cmd in ("restore", "bootloader") else {}
        env.say("M-Flasher would run (nothing is run now): " + " ".join(MF.argv(exe, a.cmd, **kw, **restart)))
        return 0
    need_mflasher(env)
    if a.cmd == "restart":
        return mf(env, "restart")[0]
    enter_download_mode(env, route)
    if a.cmd == "backup":
        backup(env, a.backup_dir)
        return 0
    ours = boot_area_only if a.cmd == "bootloader" else bool
    checked_write(env, a.cmd, ours, a.yes, True, lambda n: f"{n} sectors, M-Flasher's plan above", **kw)
    return 0


if __name__ == "__main__":
    sys.exit(main())

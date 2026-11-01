#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0
"""Start-ups with BLUETOOTH saved ON, in the FM-1 emulator, for a package with the whole BLE option set (BLE,
USB_CONSOLE, BLE_BOND, BLE_CENTRAL: the one that goes on the FM-1). Each boot must reach the UI (its frames
counted, the start-up breadcrumb past the splash), keep the watchdog fed, take no CPU exception and advertise:

  on         BLUETOOTH ON saved by the menu (a flash with the VM, no LAST): advertising from boot, a central connects
  last       LAST stored: an iPhone-like peripheral behind a resolvable private address, its bond authenticated and
             its passkey level kept (as the user's FM-1 had after the authenticated pairing). The phone there: the
             FM-1 reconnects by itself with the stored LTK; the phone away: the search (scan, initiate, advertise)
             runs and the UI with it
  busy       the phone away, an engine whose 0x28038 bit1 never clears after a stop (FM1_BLE_MODEL busy_after_stop_us
             far past any wait): every link stop gives up at its bound (hal/fm1_ble.h fm1_ble_link_stop: 40 ms on the
             main loop's, 3 ms on a connection's end, 0.2 ms before a link opens), the boot is not held, and the
             link-stop breadcrumb (hal/fm1_ble_rf.h fm1_ble_bc.stop) names the last stop, done

  tests/ble_emu_boot_test.py [PACKAGE.fwsc]   (default build/ble/felucca-ble-full.fwsc: tools/optimist.py test)

Skipped (exit 0) without the emulator's BLE models (fm1-emulator feat/ble-engine) or the package. The engine is a
model: this proves the start-up does not depend on how long the engine stays busy, not what a real FM-1 does."""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import ble_emu_test as E              # (run, vm_image, menu_toggle, model_times, ...)
import ble_emu_central_test as C      # (on_image, pick_presses, peripheral)
from tools_path import ROOT

PHONE, SPEC = "iPhone Piano", "midi:iPhone Piano:auth:rpa"
BOOT_STEPS = "450000000"              # ~4.7 s of guest time at 96 MHz
TICKS = 24_000_000                    # the guest's TIMER4 / watchdog ticks per second
STOP_CAP_US = {0: 40_000, 1: 40_000, 2: 40_000, 3: 3_000, 4: 200}   # HWS_ADV .. HWS_OPEN (ble_hw_wl82.c HW_STOP_*)
fails = 0


def check(what, ok, detail=""):
    global fails
    print(f"{what:96s} {'ok' if ok else 'FAIL'}")
    if not ok:
        fails += 1
        if detail:
            print("    " + str(detail).replace("\n", "\n    ")[:3000])


def symbols(fwsc, names):
    """{name: (address, size)} from the ELF beside the package, any section (.noinit too)"""
    elf, nm = fwsc.with_suffix(".elf"), shutil.which("nm") or shutil.which("llvm-nm")
    if not elf.is_file() or not nm:
        return {}
    p = subprocess.run([nm, "-S", str(elf)], capture_output=True, text=True)
    out = {}
    for n in names:
        m = re.search(rf"^([0-9a-f]+) ([0-9a-f]+) \w {n}$", p.stdout, re.M)
        if m:
            out[n] = (int(m.group(1), 16), int(m.group(2), 16))
    return out


def ram(out, addr, size):
    """the bytes of an FM1_DUMP range at the end of a run, or None"""
    data = bytearray()
    for line in re.findall(r"^  ([0-9a-f]{8}): ((?:[0-9a-f?]{2} ?)+)$", out, re.M):
        a = int(line[0], 16)
        if addr <= a < addr + size and a == addr + len(data):
            data += bytes(int(b, 16) if b != "??" else 0 for b in line[1].split())
    return bytes(data[:size]) if len(data) >= size else None


def words(b, n):
    return [int.from_bytes(b[4 * i:4 * i + 4], "little") for i in range(n)] if b else []


def boot(diag, fwsc, tmp, name, flash, syms, script="wait 1\n", **env):
    """a start-up over flash -> (output, air log, felucca_dbg words, fm1_ble_bc words)"""
    dumps = ",".join(f"{a:x}:{n}" for a, n in syms.values())
    e = dict(FM1_BLE_CENTRAL="off", FM1_FLASH_RESTORE=str(flash), FM1_DUMP=dumps)
    e.update(env)
    out, air = E.run(diag, fwsc, tmp, name, script, steps=BOOT_STEPS, **e)
    dbg = words(ram(out, *syms["felucca_dbg"]), 19) if "felucca_dbg" in syms else []
    bc = words(ram(out, *syms["fm1_ble_bc"]), syms["fm1_ble_bc"][1] // 4) if "fm1_ble_bc" in syms else []
    return out, air, dbg, bc


def alive(label, out, dbg):
    """the boot reached the UI, fed the watchdog, took no exception"""
    wd = re.search(r"watchdog: (\d+) feeds; timeout \S+ ticks, (\d+) since the last feed", out)
    frames, stage = (dbg[7], dbg[11]) if len(dbg) > 11 else (0, 0)
    check(f"{label}: the UI runs (frames drawn, the main loop's stage, not a start-up step)",
          frames > 10 and 1 <= stage <= 9, f"ui_frames {frames}, stage {stage:#x}\n{out[-1500:]}")
    check(f"{label}: the watchdog fed all along (the last feed < 0.2 s before the end), no exception",
          bool(wd) and int(wd.group(1)) > 100 and int(wd.group(2)) < TICKS // 5 and
          "instruction limit" in out and "exception" not in out.lower(), wd.group(0) if wd else out[-1500:])


def last_stop(bc):
    """the link-stop breadcrumb (hal/fm1_ble_rf.h fm1_ble_bc.stop, word 10) -> (done, path, the microseconds waited)"""
    w = bc[10] if len(bc) > 10 else 0
    return w >> 24 == 0x5E, w >> 16 & 0xFF, w & 0xFFFF


def stop_bounded(label, bc, dbg):
    """the last stop's wait within its bound plus 100 us plus ONE audio render: the wait is timed in wall ticks, and the
    audio-render interrupt (felucca_dbg.max_us, word 2: the longest half rendered) can land exactly as a bound ends and be
    counted in the wait (measured 2026-10-15: 40,333 us at 92 MHz, 40,302 at 96 MHz, 40,000 at 104 MHz with the same code).
    (Every stop's longest wait per path was blell's stop_us_max, gone with BLE_DIAG, 2026-10-29: the breadcrumb keeps the
    last one.)"""
    render = dbg[2] if len(dbg) > 2 else 1500
    done, path, us = last_stop(bc)
    check(f"{label}: the last link stop done within its bound (path {path}, {us} us)",
          done and path in STOP_CAP_US and us <= STOP_CAP_US[path] + 100 + render, hex(bc[10]) if len(bc) > 10 else bc)


def searches(out):
    """the model's notes of the link starting to initiate or to scan (fm1-emulator ble_engine: the search's steps)"""
    return len(E.model_times(out, "link 0 initiating started")) + len(E.model_times(out, "link 0 scanning started"))


def main():
    diag = E.diagnose_path()
    fwsc = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "ble" / "felucca-ble-full.fwsc"
    if not diag.is_file() or not fwsc.is_file():
        print(f"== BLE start-ups in the emulator: skipped (no {diag} or no {fwsc})")
        return 0
    syms = symbols(fwsc, ("felucca_dbg", "fm1_ble_bc", "ble_store", "smp_passkey"))
    if not all(k in syms for k in ("felucca_dbg", "fm1_ble_bc", "ble_store", "smp_passkey")):
        print(f"== BLE start-ups in the emulator: skipped ({fwsc} lacks BLE_CENTRAL / BLE_BOND symbols)")
        return 0
    passkey_at = f"{syms['smp_passkey'][0]:x}"
    dump = {k: syms[k] for k in ("felucca_dbg", "fm1_ble_bc")}
    with tempfile.TemporaryDirectory(prefix="bleboot-") as tmp:
        on = C.on_image(diag, fwsc, tmp)
        check("a flash with the VM and BLUETOOTH ON saved (the menu's toggle)", on is not None)
        if on is None:
            return 1
        probe, _ = E.run(diag, fwsc, tmp, "probe", "wait 1\n", steps="2000000", FM1_BLE_CENTRAL="off",
                         FM1_BLE_PERIPHERALS="midi:Probe", FM1_FLASH_RESTORE=str(on))
        if "ble_peripheral name=Probe" not in probe or "passkey_waits=" not in probe:
            print(f"== BLE start-ups in the emulator: skipped ({diag} has no virtual peripheral with auth)")
            return 0
        # on: ON saved, no LAST, a central connects
        out, air, dbg, bc = boot(diag, fwsc, tmp, "on", on, dump, script=E.CONNECT_SCRIPT,
                                     FM1_BLE_CENTRAL="script")
        alive("on", out, dbg)
        ok, bad = E.steps_ok(out)
        check("on: advertising from boot (ADV_IND on the air), a central connects, every step passes",
              bool(E.model_times(out, "link 0 advertising started")) and len(ok) == 8 and not bad, f"{ok} {bad}")
        check("on: the BLE breadcrumb: BLUETOOTH ON at boot got to its link steps (0xB1, step >= 7)",
              bool(bc) and bc[0] >> 24 == 0xB1 and (bc[0] >> 16 & 0xFF) >= 7, [hex(w) for w in bc[:1]])
        # last: an iPhone-like LAST (RPA, IRK, authenticated, MITM level) made by the DEVICES pick
        last = Path(tmp) / "last.flash"
        pr, end = C.pick_presses(C.PASSKEY_CONNECT)
        out, _ = E.run(diag, fwsc, tmp, "pick", "wait 1\n", steps=str(end), FM1_BLE_CENTRAL="off",
                       FM1_BLE_PERIPHERALS=SPEC, FM1_BLE_PASSKEY_AT=passkey_at, FM1_PRESS=",".join(pr),
                       FM1_FLASH_RESTORE=str(on), FM1_FLASH_DUMP=str(last))
        p = C.peripheral(out, PHONE)
        check("last: the iPhone-like peripheral paired with the passkey and became LAST (the flash dumped)",
              p.get("authenticated") == "1" and last.is_file(), str(p))
        if not last.is_file():
            return 1
        out, air, dbg, bc = boot(diag, fwsc, tmp, "last-there", last, dump, FM1_BLE_PERIPHERALS=SPEC,
                                     FM1_BLE_PERIPHERAL_BOOT="1", FM1_BLE_PASSKEY_AT=passkey_at,
                                     FM1_BLE_ADVERTISERS="default")
        alive("last, the phone there", out, dbg)
        p = C.peripheral(out, PHONE)
        check("last, the phone there: reconnected by itself with the stored LTK (no pairing), subscribed",
              int(p.get("ltk_reuse", "0") or 0) >= 1 and p.get("subscribed") == "1" and p.get("paired") == "0", str(p))
        out, air, dbg, bc = boot(diag, fwsc, tmp, "last-away", last, dump, FM1_BLE_ADVERTISERS="default")
        alive("last, the phone away", out, dbg)
        check("last, the phone away: the search runs (initiating, scanning) and advertising between its tries",
              searches(out) >= 1 and len(E.model_times(out, "link 0 advertising started")) >= 2, out[-800:])
        stop_bounded("last, the phone away", bc, dbg)
        # busy: bit1 never clears after a stop
        out, air, dbg, bc = boot(diag, fwsc, tmp, "busy", last, dump, FM1_BLE_ADVERTISERS="default",
                                     FM1_BLE_MODEL="busy_after_stop_us=100000000")
        alive("busy (the engine never idle after a stop)", out, dbg)
        done, path, us = last_stop(bc)
        check("busy: the last stop gave up at its bound (the breadcrumb: done, its path, the whole bound waited) and the "
              "search went on (initiating / scanning started again after stops)",
              done and path in STOP_CAP_US and us >= STOP_CAP_US[path] and searches(out) >= 2,
              f"stop {hex(bc[10]) if len(bc) > 10 else bc}, {searches(out)} searches")
        stop_bounded("busy", bc, dbg)
        check("busy: the link-stop breadcrumb names the last stop, finished (0x5E, its path, the wait)",
              done and path < 5, hex(bc[10]) if len(bc) > 10 else bc)
    print("BLE START-UPS PASSED" if not fails else f"BLE START-UPS FAILED ({fails})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

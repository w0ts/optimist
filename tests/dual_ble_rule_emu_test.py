#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The second core with BLE (system/dual.c, stock V15's order since 2026-10-16: the BLUETOOTH rule dropped), in the BLE
emulator:

  off        BLUETOOTH OFF (a flash with the VM, no settings): CPU1 started at boot, jobs flowing
  on         BLUETOOTH ON saved: CPU1 started at boot and running through the radio's start-up, jobs flowing, the radio
             advertises
  switch     BLUETOOTH OFF at boot, switched ON in the menu while CPU1 runs: CPU1 held for the radio's start-up program
             (dual_radio_hold) and started again after it (dual_radio_release), its jobs flowing again; the audio goes on
             (no late half), no exception

  tests/dual_ble_rule_emu_test.py PACKAGE.fwsc     (a DUAL=2 build with BLE; the ELF beside it)

Skipped (exit 0) without the BLE emulator (FM1_BLE_DIAGNOSE / FM1_EMU, as tests/ble_emu_test.py). Exit 1 on a failure."""
import sys
import tempfile
from pathlib import Path

import ble_emu_test as E
import ble_emu_central_test as C
import ble_emu_boot_test as B

fails = 0


def check(what, ok, detail=""):
    global fails
    print(f"{'ok  ' if ok else 'FAIL'}  {what}" + ("" if ok else f"\n      {detail}"))
    fails += 0 if ok else 1


def state(out, syms):
    """-> (dual.up, dual.why, felucca_dbg.late, fm1_dual_mb.done) (system/dual.c fm1_dual_state: the main loop's copy)"""
    d = B.ram(out, *syms["fm1_dual_state"])
    mb = B.words(B.ram(out, *syms["fm1_dual_mb"]), 17)
    dbg = B.words(B.ram(out, *syms["felucca_dbg"]), 6)
    return (d[0] if d else None, d[1] if d else None, dbg[5] if dbg else None, mb[16] if mb else None)


def main():
    diag = E.diagnose_path()
    fwsc = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if not diag.is_file() or not fwsc or not fwsc.is_file():
        print(f"== the BLUETOOTH rule in the emulator: skipped (no {diag} or no package)")
        return 0
    syms = B.symbols(fwsc, ("fm1_dual_state", "fm1_dual_mb", "felucca_dbg"))
    if len(syms) < 3:
        print("== the BLUETOOTH rule in the emulator: skipped (not a DUAL build)")
        return 0
    dump = {k: syms[k] for k in syms}
    with tempfile.TemporaryDirectory(prefix="dualble-") as tmp:
        fresh, _ = E.vm_image(diag, fwsc, tmp)
        on = C.on_image(diag, fwsc, tmp)
        if fresh is None or on is None:
            print("== the BLUETOOTH rule in the emulator: skipped (no VM image)")
            return 0
        out, _, _, _, _ = B.boot(diag, fwsc, tmp, "off", fresh, dump)
        up, why, late, done = state(out, syms)
        check("BLUETOOTH OFF: CPU1 up, its jobs done", up == 1 and why == 0 and (done or 0) > 100,
              f"up {up} why {why} done {done}")
        out, air, _, _, _ = B.boot(diag, fwsc, tmp, "on", on, dump, script=E.CONNECT_SCRIPT, FM1_BLE_CENTRAL="script")
        up, why, late, done = state(out, syms)
        check("BLUETOOTH ON saved: CPU1 up through the radio's start-up, its jobs done", up == 1 and why == 0 and (done or 0) > 100,
              f"up {up} why {why} done {done}")
        check("BLUETOOTH ON saved: the radio advertises", "ADV" in air.upper() or "adv" in out,
              air[-400:] or out[-400:])
        pr, _, end = E.menu_toggle(250_000_000)
        out, air = E.run(diag, fwsc, tmp, "switch", "wait 1\n", steps=str(end + 120_000_000), FM1_BLE_CENTRAL="off",
                         FM1_PRESS=",".join(pr), FM1_FLASH_RESTORE=str(fresh), FM1_DUMP=",".join(
                             f"{a:x}:{n}" for a, n in dump.values()))
        up, why, late, done = state(out, syms)
        check("switched ON with CPU1 running: CPU1 held for the program, then up again, its jobs flowing",
              up == 1 and why == 0 and (done or 0) > 100, f"up {up} why {why} done {done}")
        check("switched ON: no late half, no exception", late == 0 and "exception" not in out.lower(),
              f"late {late}\n{out[-800:]}")
    print(f"dual_ble_rule_emu_test: {'ok' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

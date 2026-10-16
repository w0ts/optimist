#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The second-core test image's `dualcfg bledelay MS` (system/dual.c dual_ble_gate, io/midi/midi_ble.c ble_defer_poll), in
the BLE emulator: BLUETOOTH ON saved, the BLUETOOTH rule off (policy none), CPU1 started at boot. The radio's start-up
program must begin only after CPU1 was seen working, at least MS later, and the radio must then advertise.

  tests/dual_bledelay_emu_test.py PACKAGE.fwsc MS
      (built with OPTIMIST_DUALCFG=1 OPTIMIST_DUALCFG_START=0 OPTIMIST_DUALCFG_POLICY=0 OPTIMIST_DUALCFG_BLEDELAY=MS; the ELF
       beside it)

Skipped (exit 0) without the BLE emulator. Exit 1 on a failure."""
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


def word(out, syms, name):
    w = B.words(B.ram(out, *syms[name]), 1)
    return w[0] if w else None


def main():
    diag = E.diagnose_path()
    fwsc = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    delay = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    if not diag.is_file() or not fwsc or not fwsc.is_file():
        print(f"== bledelay in the emulator: skipped (no {diag} or no package)")
        return 0
    names = ("fm1_dual_state", "fm1_dual_mb", "dual_ok_t4", "ble_ms_radio", "ble_ms_running", "ble_deferred", "fm1_ble_bc")
    syms = B.symbols(fwsc, names)
    if len(syms) < len(names):
        print("== bledelay in the emulator: skipped (not a DUALCFG build with BLE: %s)" % sorted(set(names) - set(syms)))
        return 0
    with tempfile.TemporaryDirectory(prefix="bledelay-") as tmp:
        on = C.on_image(diag, fwsc, tmp)
        if on is None:
            print("== bledelay in the emulator: skipped (no VM image)")
            return 0
        out, air, _, bc, _ = B.boot(diag, fwsc, tmp, "on", on, syms, script=E.CONNECT_SCRIPT, FM1_BLE_CENTRAL="script")
        st = B.ram(out, *syms["fm1_dual_state"])
        mb = B.words(B.ram(out, *syms["fm1_dual_mb"]), 17)
        ok_t4, radio, running, deferred = (word(out, syms, n) for n in ("dual_ok_t4", "ble_ms_radio", "ble_ms_running",
                                                                        "ble_deferred"))
        up, why, done = (st[0] if st else None), (st[1] if st else None), (mb[16] if mb else None)
        print(f"   cpu1 up {up} why {why} done {done}; TIMER4 ms: cpu1 working {ok_t4 and ok_t4 - 1}, "
              f"radio {radio and radio - 1}, running {running and running - 1}; deferred {deferred}; "
              f"BLE step {(bc[0] >> 16) & 0xFF if bc else None}")
        check("BLUETOOTH ON, policy none: CPU1 up at boot, its jobs done", up == 1 and why == 0 and (done or 0) > 100,
              f"up {up} why {why} done {done}")
        check("the radio's start-up began after CPU1 was working, at least %d ms later" % delay,
              bool(ok_t4) and bool(radio) and radio - ok_t4 >= delay, f"cpu1 {ok_t4} radio {radio}")
        check("BLUETOOTH then ran (advertising), nothing left deferred", bool(running) and running >= radio and deferred == 0,
              f"running {running} deferred {deferred}")
        check("the radio advertises", "ADV" in air.upper() or "adv" in out, air[-400:] or out[-400:])
        check("no exception", "exception" not in out.lower(), out[-800:])
    print(f"dual_bledelay_emu_test: {'ok' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

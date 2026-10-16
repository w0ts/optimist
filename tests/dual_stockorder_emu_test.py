#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The second-core test image's `dualcfg stockorder on` (stock V15's start-up order, system/main.c and system/dual.c), in
the BLE emulator, BLUETOOTH ON saved. The order must be: the rails (stock sequence, read back right, the 2.6 V detector on)
-> CPU1 started -> the radio's start-up program -> BLUETOOTH running -> audio_init -> CPU1's first job (>= 400 ms after
BLUETOOTH runs, on fm1_ms); CPU1 then works, the radio advertises, nothing raised an exception. With `fpause stock` built in
(OPTIMIST_DUALCFG_FPAUSE=2) the flash operations of the run, if any, find CPU1 parked (no missing ack).

  tests/dual_stockorder_emu_test.py PACKAGE.fwsc
      (built with OPTIMIST_DUALCFG=1 OPTIMIST_DUALCFG_STOCKORDER=1 [OPTIMIST_DUALCFG_FPAUSE=2], UI=0; the ELF beside it)

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
    if not diag.is_file() or not fwsc or not fwsc.is_file():
        print(f"== stockorder in the emulator: skipped (no {diag} or no package)")
        return 0
    one = ("dual_start_t4", "ble_ms_radio", "ble_ms_running", "dual_audio_t4", "dual_job1_t4", "dual_fp_n", "dual_fp_noack",
           "fm1_p33_timeouts")
    names = ("fm1_dual_state", "fm1_dual_mb") + one
    syms = B.symbols(fwsc, names)
    if len(syms) < len(names):
        print("== stockorder in the emulator: skipped (not a DUALCFG build with BLE: %s)" % sorted(set(names) - set(syms)))
        return 0
    with tempfile.TemporaryDirectory(prefix="stockorder-") as tmp:
        on = C.on_image(diag, fwsc, tmp)
        if on is None:
            print("== stockorder in the emulator: skipped (no VM image)")
            return 0
        out, air, _, _, _ = B.boot(diag, fwsc, tmp, "on", on, syms, script=E.CONNECT_SCRIPT, FM1_BLE_CENTRAL="script")
        st = B.ram(out, *syms["fm1_dual_state"])
        mb = B.words(B.ram(out, *syms["fm1_dual_mb"]), 17)
        v = {n: word(out, syms, n) for n in one}
        up, why, done = (st[0] if st else None), (st[1] if st else None), (mb[16] if mb else None)
        t = {k: (v[k] - 1 if v[k] else None) for k in ("dual_start_t4", "ble_ms_radio", "ble_ms_running", "dual_audio_t4",
                                                       "dual_job1_t4")}
        print(f"   TIMER4 ms: cpu1 start {t['dual_start_t4']}, radio {t['ble_ms_radio']}, running {t['ble_ms_running']}, "
              f"audio_init {t['dual_audio_t4']}, cpu1 job 1 {t['dual_job1_t4']}; cpu1 up {up} why {why} done {done}; "
              f"flash parks {v['dual_fp_n']} noack {v['dual_fp_noack']}; p33 timeouts {v['fm1_p33_timeouts']}")
        seq = [t[k] for k in ("dual_start_t4", "ble_ms_radio", "ble_ms_running", "dual_audio_t4", "dual_job1_t4")]
        check("CPU1 up, no reason down (the supply read back right), its jobs done", up == 1 and why == 0 and (done or 0) > 100,
              f"up {up} why {why} done {done}")
        check("the order: CPU1 start < radio < BLUETOOTH running < audio_init < CPU1's first job",
              None not in seq and seq == sorted(seq) and len(set(seq)) >= 4, f"{seq}")
        check("CPU1's first job >= 400 ms after BLUETOOTH runs",
              None not in seq and seq[4] - seq[2] >= 400, f"{seq}")
        check("no P33 timeout", v["fm1_p33_timeouts"] == 0, f"{v['fm1_p33_timeouts']}")
        check("flash operations found CPU1 parked (fpause stock: no missing ack)", not v["dual_fp_noack"],
              f"parks {v['dual_fp_n']} noack {v['dual_fp_noack']}")
        check("the radio advertises", "ADV" in air.upper() or "adv" in out, air[-400:] or out[-400:])
        check("no exception", "exception" not in out.lower(), out[-800:])
    print(f"dual_stockorder_emu_test: {'ok' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

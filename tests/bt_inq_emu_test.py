#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage 1a of the Bluetooth Classic probe (builder item BT_INQ_PROBE, firmware/src/bt/bt_inq.c) in the FM-1 emulator:
a package with the probe boots over a flash with a VM (the radio's stored trims), the probe starts the radio and the
BR/EDR block ~1.5 s after boot and watches the slot clock and its interrupts for 3 s. Read from its RAM record bti_st
at the end (FM1_DUMP of the ELF's symbol beside the package):

  phase 2 (done)               the radio started (trims found), the block initialised, 3 s measured
  the slot clock               +1,600 slots a second (0x20020, one count per 625 us), within 1 %
  the alarm (IRQ 40 bit 9)     re-armed every 18 slots: about 3,000 ms x 1,600 / 18 / 1,000 = 266 fires, none late
                               by more than a slot
  slot timer 0 (IRQ 41)        every 1,600 slots: 2 or 3 in 3 s
  no unknown IRQ 40 bits, the clock latch polled only a few times

  tests/bt_inq_emu_test.py PACKAGE.fwsc   (its ELF beside it: PACKAGE.elf)

The emulator models the clock, the alarm and the slot timers, not the engine: this proves the probe's clock and
interrupt plumbing, not that an FM-1 transmits. Skipped (exit 0) without the emulator or the package."""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import ble_emu_test as E              # (diagnose_path, vm_image: a flash with a VM)

STEPS = "620000000"                   # ~6.4 s of guest time at 96 MHz
NAMES = ("magic", "phase", "rf_up", "clk0", "clk1", "ms0", "ms1", "fine_max", "irq40", "alarms", "rx_irqs",
         "other40", "unknown40", "irq41", "slot0", "latch_spins", "alarm_late")
fails = 0


def check(what, ok, detail=""):
    global fails
    print(f"{what:96s} {'ok' if ok else 'FAIL'}")
    if not ok:
        fails += 1
        if detail:
            print("    " + str(detail)[:3000])


def symbol(elf, name):
    nm = shutil.which("nm") or shutil.which("llvm-nm")
    p = subprocess.run([nm, "-S", str(elf)], capture_output=True, text=True)
    m = re.search(rf"^([0-9a-f]+) ([0-9a-f]+) \w {name}$", p.stdout, re.M)
    return (int(m.group(1), 16), int(m.group(2), 16)) if m else None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    fwsc = Path(sys.argv[1])
    diag = E.diagnose_path()
    elf = fwsc.with_suffix(".elf")
    if not diag.is_file() or not fwsc.is_file() or not elf.is_file():
        print(f"== BT Classic probe in the emulator: skipped (need {diag}, {fwsc} and {elf})")
        return 0
    st = symbol(elf, "bti_st")
    if not st:
        print("== BT Classic probe: the package has no bti_st (built without BT_INQ_PROBE?)")
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        flash, which = E.vm_image(diag, fwsc, tmp)
        check(f"a flash with a VM ({which})", flash is not None)
        if flash is None:
            return 1
        env = dict(E.os.environ, FM1_CPU_MHZ=E.MHZ, FM1_FLASH_RESTORE=str(flash), FM1_DUMP=f"{st[0]:x}:{st[1]}")
        p = subprocess.run([str(diag), str(fwsc), STEPS], env=env, capture_output=True, text=True, timeout=900)
        out = p.stdout + p.stderr
    raw = E.ram(out, st[0], st[1]) if hasattr(E, "ram") else None
    if raw is None:
        data = bytearray()
        for a, b in re.findall(r"^  ([0-9a-f]{8}): ((?:[0-9a-f?]{2} ?)+)$", out, re.M):
            if int(a, 16) == st[0] + len(data):
                data += bytes(int(x, 16) if x != "??" else 0 for x in b.split())
        raw = bytes(data[:st[1]]) if len(data) >= st[1] else None
    check("bti_st read from the guest's RAM", raw is not None, out[-1500:])
    if raw is None:
        return 1
    w = {n: int.from_bytes(raw[4 * i:4 * i + 4], "little") for i, n in enumerate(NAMES)}
    print("   " + " ".join(f"{k}={v}" for k, v in w.items()))
    ms, slots = w["ms1"] - w["ms0"], (w["clk1"] - w["clk0"]) & 0x07FFFFFF
    rate = slots * 1000 / ms if ms else 0
    check("the record's magic (BTI1)", w["magic"] == 0x31495442)
    check("phase 2: the radio started, the block initialised, 3 s measured", w["phase"] == 2 and w["rf_up"] == 1,
          f"phase {w['phase']:#x}\n" + "\n".join(l for l in out.splitlines() if l.startswith(("diagnose", "stopped", "executed"))))
    check(f"the slot clock: {rate:.1f} slots/s over {ms} ms (1,600 within 1 %)", abs(rate - 1600) <= 16)
    want = ms * 1600 / 18 / 1000
    check(f"the 18-slot alarm: {w['alarms']} fires (about {want:.0f}), late at most {w['alarm_late']} slot",
          abs(w["alarms"] - want) <= max(3, want * 0.03) and w["alarm_late"] <= 1)
    check(f"IRQ 40 entries {w['irq40']} = the alarms (nothing else pending: no engine model)",
          w["irq40"] == w["alarms"] and w["rx_irqs"] == 0 and w["other40"] == 0)
    check(f"slot timer 0 (IRQ 41): {w['slot0']} in {ms} ms (every 1,600 slots: 2 or 3)", 2 <= w["slot0"] <= 3
          and w["irq41"] >= w["slot0"])
    check("no unknown IRQ 40 pending bit", w["unknown40"] == 0, f"{w['unknown40']:#x}")
    check(f"the clock latch: at most {w['latch_spins']} polls", w["latch_spins"] <= 64)
    check("no CPU exception, the watchdog fed", "exception" not in out.lower() or "exceptions: 0" in out.lower(),
          out[-1500:])
    print("bt_inq_emu_test: all ok" if not fails else f"bt_inq_emu_test: {fails} FAILED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

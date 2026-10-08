#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""BLE MIDI end to end in the FM-1 emulator (route C: our stack, its WL82 baseband driver, docs/BLE-STACK.md §11).

Runs a BLE build (FELUCCA_BLE=1) in the emulator's `diagnose` with its BLE engine model and virtual central
(fm1-emulator, branch feat/ble-engine: rust-emulator/README.md "Bluetooth LE (BLE MIDI) model") and checks:
advertising (FM-1_BLE, the BLE-MIDI UUID), connect, version / features, MTU, discovery (GAP, GATT with Service
Changed, BLE-MIDI), the CCCD, our L2CAP parameter request and the update it leads to, a BLE-MIDI note from the
central playing the synth (against a run without it), a key press reaching the central as a BLE-MIDI notification
with a real timestamp, a channel-map update, an interval update, terminate, advertising again; the same with
FM1_BLE_LOSS, with the engine storing repeated SNs (our software SN check), and the supervision timeout after the
central vanishes, and the HOME menu's BLUETOOTH item (menu_checks: the panel driven with FM1_PRESS contacts, HOME held,
SELECT turned as a quadrature encoder, OCT+): OFF terminates a connected central and stops advertising, ON advertises
and takes a connection again, the choice survives a restart (the emulator's flash dump / restore) and a held note of
the central is ended. Every run boots over a flash with a VM at 0x093000 (the emulator's own from
tools/ble_rf_capture.py, else one built from docs/BLE-HW-FACTS.md §14): rf_checks traces the radio's start-up and
compares it, write for write, with stock V15's second boot (but for our VCO scan and the read-back loop left out) and
the AGC table; trim_checks: no VM and no copy -> the radio never starts; a boot with the VM keeps the copy with the
settings; the VM erased -> the copy starts the radio. The engine is a model built from the fact sheet: this proves the
stack and the driver agree with it, not that a real FM-1 transmits.

  tests/ble_emu_test.py [PACKAGE.fwsc]       (default build/ble/felucca-ble.fwsc: tools/optimist.py test makes it)

The emulator: FM1_BLE_DIAGNOSE (the diagnose binary), else $FM1_EMU/rust-emulator/target/release/examples/diagnose
(FM1_EMU default ~/GitHub/fm1-emulator-ble). Skipped (exit 0) when that binary is absent or has no BLE model, and
when there is no BLE package (a --no-build run)."""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ble_vm  # noqa: E402  (the VM format, docs/BLE-HW-FACTS.md §14)
MIDI_UUID = "00 c7 c4 4e e3 6c 51 a7 33 4b e8 ed 5a 0e b8 03"      # 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, LSB first
NAME = "09 09 46 4d 2d 31 5f 42 4c 45"                             # Complete Local Name "FM-1_BLE"
MHZ = "96"
PRESS = "144000000:3:4:9600000"        # a note key held 0.1 s from 1.5 s (matrix column 3, row 4)
STEPS = "330000000"                    # ~3.4 s of guest time at 96 MHz

SCRIPT = """scan 2
connect
version
features
mtu 527
discover
read
subscribe
{midi}
wait 40
{midi_off}
notifications 2
wait 10
chmap 1ff0ffff0f
wait 20
update interval=12 latency=0 timeout=200
wait 40
terminate
"""
# the HOME menu through the panel's matrix contacts (column, row): HOME, OCT+, SELECT's quadrature A and B
HOME, OCT_UP, SEL_A, SEL_B = (7, 1), (1, 4), (0, 0), (1, 0)
PHASE = 1_500_000                      # a quadrature phase: the firmware reads each contact on a few scans
HOLD = 86_000_000                      # HOME held: it acts after 700 ms (ui_input.c btn_hold), then let go
CLICKS = 8                             # SELECT to the right until the last screen, BLUETOOTH's (it stops there)
NOTE_ON = "midi 80 80 90 3c 64"
# a central that holds a note until BLUETOOTH goes OFF and lets it go (a script cannot go on after that: its next steps
# fail at once)
MENU_SCRIPT = """scan 2
connect
version
mtu 527
discover
subscribe
midi 80 80 90 3c 64
wait 3000
"""
# one whole connection, for a boot or a toggle that leaves BLUETOOTH ON (its scan lasts until ON, 4 s at most)
CONNECT_SCRIPT = "scan 2\nconnect\nversion\nmtu 527\ndiscover\nsubscribe\nwait 20\nterminate\n"
VANISH = "scan 2\nconnect\nversion\nmtu 527\ndiscover\nsubscribe\nwait 20\nvanish\n"

fails = 0


def check(what, ok, detail=""):
    global fails
    print(f"{what:96s} {'ok' if ok else 'FAIL'}")
    if not ok:
        fails += 1
        if detail:
            print("    " + detail.replace("\n", "\n    ")[:3000])


def diagnose_path():
    if os.environ.get("FM1_BLE_DIAGNOSE"):
        return Path(os.environ["FM1_BLE_DIAGNOSE"])
    emu = Path(os.environ.get("FM1_EMU", Path.home() / "GitHub" / "fm1-emulator-ble"))
    return emu / "rust-emulator" / "target" / "release" / "examples" / "diagnose"


VM_IMAGE = None                        # the flash with a VM at 0x093000 (vm_image): every run boots over it unless told


def run(diag, fwsc, tmp, name, script, steps=STEPS, vm=True, **env):
    sp = Path(tmp) / f"{name}.script"
    sp.write_text(script)
    air = Path(tmp) / f"{name}-air.txt"
    if vm and VM_IMAGE and "FM1_FLASH_RESTORE" not in env:
        env["FM1_FLASH_RESTORE"] = str(VM_IMAGE)     # (a dump of an earlier run has the VM in it already)
    e = dict(os.environ, FM1_CPU_MHZ=MHZ, FM1_BLE_CENTRAL="script", FM1_BLE_SCRIPT=str(sp), FM1_BLE_LOG=str(air),
             FM1_BLE_ISR="1", **env)
    p = subprocess.run([str(diag), str(fwsc), steps], env=e, capture_output=True, text=True, timeout=900)
    out = p.stdout + p.stderr
    return out, air.read_text() if air.exists() else ""


def contact(at, pos, length):
    return f"{at}:{pos[0]}:{pos[1]}:{length}"


def menu_toggle(t):
    """HOME held (opens the menu), SELECT right CLICKS detents, OCT+ (toggles BLUETOOTH, the cursor is on it), HOME held
    (closes it: the settings are saved). -> (contacts, the step OCT+ is pressed at, the step the menu is closed at)"""
    pr = [contact(t, HOME, HOLD)]
    at = t + HOLD + 4_000_000
    for _ in range(CLICKS):                # clockwise: B closes, A closes, B opens, A opens (fm1_input.h decoder)
        pr += [contact(at, SEL_B, 2 * PHASE), contact(at + PHASE, SEL_A, 2 * PHASE)]
        at += 4 * PHASE
    at += 3_000_000
    toggle = at
    pr.append(contact(at, OCT_UP, 6_000_000))
    at += 15_000_000
    pr.append(contact(at, HOME, HOLD))
    return pr, toggle, at + HOLD + 4_000_000


def air_events(air):
    """(time s, who, what) for each line of the air log"""
    ev = []
    for line in air.splitlines():
        m = re.match(r"\s*([\d.]+) (P->C|C->P) ch\d+ ev +\S+ +(\S+)", line)
        if m:
            ev.append((float(m.group(1)), m.group(2), m.group(3)))
    return ev


def model_times(out, text):
    return [float(m) for m in re.findall(rf"model: ([\d.]+) {text}", out)]


def menu_checks(diag, fwsc, tmp):
    one = ",".join(menu_toggle(150_000_000)[0])      # (one toggle, from a boot with the item OFF it is ON)
    off_pr, off_at, end = menu_toggle(150_000_000)
    on_pr, on_at, end = menu_toggle(end + 10_000_000)
    both = ",".join(off_pr + on_pr)
    limit = str(end + 120_000_000)
    out, air = run(diag, fwsc, tmp, "menu", MENU_SCRIPT, steps=limit, FM1_PRESS=both)
    ok, bad = steps_ok(out)
    check("BLUETOOTH OFF: the central is sent an LL_TERMINATE_IND (reason 0x13) and its wait ends there",
          "connection ended: peripheral LL_TERMINATE_IND, reason 0x13" in out, out[-1500:])
    t_term = next((t for t, who, what in air_events(air) if who == "P->C" and what == "LL_TERMINATE_IND"), 0.0)
    starts, stops = model_times(out, "link 0 advertising started"), model_times(out, "link 0 stopped")
    check("BLUETOOTH OFF at the step the menu was driven to: the terminate comes right after it (<= 0.3 s)",
          0.0 < t_term - off_at / 96e6 < 0.3, f"terminate {t_term:.3f} s, OCT+ {off_at / 96e6:.3f} s")
    check("OFF: the link stops (column 14 = 0), nothing advertises while it is OFF",
          len(stops) >= 1 and len(starts) >= 2 and starts[1] > on_at / 96e6,
          f"advertising started {starts}, stopped {stops}, ON at {on_at / 96e6:.3f} s")
    quiet = [e for e in air_events(air) if t_term + 0.02 < e[0] < starts[1] - 0.001] if len(starts) > 1 else []
    check("OFF: no packet on the air between the terminate and ON (no ADV_IND, nothing from the link)", not quiet,
          str(quiet[:5]))
    if len(starts) > 1:
        after = [e for e in air_events(air) if e[0] >= starts[1] and e[1] == "P->C" and e[2] == "ADV_IND"]
        check("ON: advertising again (ADV_IND on the air) after the item is switched ON", bool(after))
    # a note the central held when BLUETOOTH went OFF is ended: the last second of audio is silent; without OFF it rings
    silent = re.findall(r"BLE run: (\d+) of \d+ recorded audio frames are not silent", out)
    ctl, _ = run(diag, fwsc, tmp, "menu-ctl", MENU_SCRIPT.replace("wait 3000", "wait 2000"),
                 steps=limit)
    ring = re.findall(r"BLE run: (\d+) of \d+ recorded audio frames are not silent", ctl)
    check("no stuck note: the central's note ends with the link (silent at the end), and it rings without OFF",
          bool(silent) and bool(ring) and int(silent[0]) == 0 and int(ring[0]) > 1000, f"{silent} vs {ring}")
    # the choice is kept over a restart: OFF saved, restored -> nothing advertises; ON saved, restored -> advertising
    flash = Path(tmp) / "menu-off.flash"
    out, air = run(diag, fwsc, tmp, "off-save", "scan 1\n", steps="450000000",
                   FM1_PRESS=",".join(menu_toggle(150_000_000)[0]), FM1_FLASH_DUMP=str(flash))
    check("OFF saved when the menu closes (the emulator's flash dumped)", flash.is_file(), out[-800:])
    out, air = run(diag, fwsc, tmp, "off-boot", "scan 1\n", steps="250000000", FM1_FLASH_RESTORE=str(flash))
    check("restart with OFF saved: the radio never advertises", not model_times(out, "link 0 advertising started")
          and not any(e[2] == "ADV_IND" for e in air_events(air)), out[-800:])
    flash2 = Path(tmp) / "menu-on.flash"
    out, air = run(diag, fwsc, tmp, "on-save", CONNECT_SCRIPT, steps="450000000", FM1_PRESS=one,
                   FM1_FLASH_RESTORE=str(flash), FM1_FLASH_DUMP=str(flash2))
    ok, bad = steps_ok(out)
    conn = [t for t, who, what in air_events(air) if who == "C->P" and what == "CONNECT_IND"]
    check("from a boot with OFF saved, the menu's ON: advertising begins, a central connects (after ON, not before)",
          len(ok) == 8 and not bad and len(conn) == 1 and conn[0] > off_at / 96e6, f"{len(ok)} ok, {bad}, {conn}")
    out, air = run(diag, fwsc, tmp, "on-boot", CONNECT_SCRIPT, steps="250000000", FM1_FLASH_RESTORE=str(flash2))
    ok, bad = steps_ok(out)
    check("... ON saved again, restart: advertising from boot, a central connects", len(ok) == 8 and not bad
          and len(model_times(out, "link 0 advertising started")) >= 1, f"{len(ok)} ok, {bad}")


CAP = ROOT / "build" / "gen" / "ble_rf_capture"      # tools/ble_rf_capture.py's side outputs
RF_RANGES = "10000-100ff,11900-1197f,14000-14067,2fc00-2fdff,30000-31fff"
SCAN_REGS = (0x11934, 0x11938, 0x1193C, 0x11968, 0x11978)


def vm_image(diag, fwsc, tmp):
    """the package's own flash (a short run's dump) with a VM laid at 0x093000: the emulator's own from the capture
    (V15's boot 1: build/gen/ble_rf_capture/vm_emu.bin) or, without it, one built here from the format (§14)"""
    base = Path(tmp) / "base.flash"
    subprocess.run([str(diag), str(fwsc), "1000"], env=dict(os.environ, FM1_FLASH_DUMP=str(base)),
                   capture_output=True, timeout=300)
    if not base.is_file():
        return None, "no flash dump from diagnose"
    img = bytearray(base.read_bytes())
    if (CAP / "vm_emu.bin").is_file():
        vm, which = (CAP / "vm_emu.bin").read_bytes(), "the emulator's own (V15's boot 1, ble_rf_capture)"
    else:
        vm = ble_vm.build_area([(106, bytes((11, 11))), (107, bytes((1, 7, 4, 7, 11, 1, 7))), (108, bytes(20)),
                                (187, ble_vm.rec187(bytes(64)))]) + b"\xff" * ble_vm.AREA_SIZE
        which = "synthetic (no build/gen/ble_rf_capture)"
    img[ble_vm.VM_BASE:ble_vm.VM_BASE + len(vm)] = vm
    out = Path(tmp) / "vm.flash"
    out.write_bytes(bytes(img))
    return out, which


def trace_writes(path):
    out = []
    if not path.is_file():
        return out
    for line in path.read_text().splitlines():
        f = line.split()
        if len(f) >= 7 and f[3] == "W":
            out.append((int(f[4], 16), int(f[6], 16)))
    return out


def rf_part(ws):
    """the writes of the radio's start-up before the BT block (as tools/ble_rf_capture.py cuts stock's): the clock
    words left out, up to the first write to 0x14000"""
    end = next((i for i, (a, _) in enumerate(ws) if a == 0x14000), len(ws))
    return [(a, v & ~0x4000 if a == 0x11900 else v) for a, v in ws[:end] if not 0x10000 <= a < 0x10100], end < len(ws)


def rf_checks(diag, fwsc, tmp):
    """rf_init with the captured tables in the emulator: its writes are stock V15's second-boot writes (with the
    emulator's VM laid in, the same trims) but for the VCO scan (ours) and the left-out read-back loop"""
    tr = Path(tmp) / "rf.trace"
    out, air = run(diag, fwsc, tmp, "rf", "scan 2\n", steps="120000000", FM1_MMIO_TRACE=str(tr),
                   FM1_MMIO_RANGES=RF_RANGES, FM1_MMIO_TRACE_LIMIT="4000000")
    ws = trace_writes(tr)
    part, bt = rf_part(ws)
    check("rf_init ran before the BT block (RF writes, then 0x14000) and advertising started", bool(part) and bt
          and bool(model_times(out, "link 0 advertising started")), out[-800:])
    exp = CAP / "expected.txt"
    if exp.is_file() and (CAP / "vm_emu.bin").is_file():
        want, scan_at = [], None
        for line in exp.read_text().splitlines():
            if line.startswith("#"):
                continue
            a, v = line.split()
            if a == "scan":
                scan_at = len(want)
            elif a != "skip":
                want.append((int(a, 16), int(v, 16) & ~0x4000 if int(a, 16) == 0x11900 else int(v, 16)))
        pre, post = want[:scan_at], want[scan_at:]
        mid = part[len(pre):len(part) - len(post)]
        tail = part[len(part) - len(post):]
        first = next((f"before the scan, write {i}: ours {x[0]:05x}={x[1]:08x}, stock {y[0]:05x}={y[1]:08x}"
                      for i, (x, y) in enumerate(zip(part, pre)) if x != y), None) or \
            next((f"after the scan, write {i}: ours {x[0]:05x}={x[1]:08x}, stock {y[0]:05x}={y[1]:08x}"
                  for i, (x, y) in enumerate(zip(tail, post)) if x != y), f"{len(part)} writes vs {len(want)}")
        check(f"rf_init = stock V15's second boot, write for write ({len(pre)} before our VCO scan, {len(post)} after)",
              scan_at is not None and part[:len(pre)] == pre and tail == post, first)
        steps = sum(1 for a, _ in mid if a == 0x11968)
        band = next(((v >> 19) & 0x7F for a, v in reversed(mid) if a == 0x11938), None)
        check(f"between them only our VCO scan's registers ({steps} steps, last band {band})",
              bool(mid) and all(a in SCAN_REGS for a, _ in mid) and 0 < steps <= 7, str(mid[:6]))
    agc = [v for a, v in ws if a == 0x2FD9C]
    hdr = ROOT / "build" / "gen" / "ble_rf_tables.h"
    m = re.search(r"ble_rf_agc\[128\] = \{([^}]*)\}", hdr.read_text()) if hdr.is_file() else None
    table = [int(x.strip().rstrip("u"), 16) for x in m.group(1).split(",")] if m else []
    check("the AGC table: the 128 captured words to 0x2FD9C after 0x2FD98 = 0", len(table) == 128 and agc == table,
          f"{len(agc)} words")


def trim_checks(diag, fwsc, tmp):
    """§15.4's precedence end to end: no VM and no copy -> the radio never starts; a boot with the VM keeps a copy with
    the settings; the VM erased, the copy alone starts the radio"""
    tr = Path(tmp) / "none.trace"
    out, air = run(diag, fwsc, tmp, "none", "scan 1\n", steps="150000000", vm=False, FM1_MMIO_TRACE=str(tr),
                   FM1_MMIO_RANGES="11900-1197f,14000-14067,28000-280ff,3101c-3101c")
    check("no VM, no copy: the radio is never started (no RF or baseband write) and nothing is on the air",
          not trace_writes(tr) and not model_times(out, "link 0 advertising started")
          and not any(e[2] == "ADV_IND" for e in air_events(air)), out[-800:])
    dump = Path(tmp) / "copy.flash"
    out, air = run(diag, fwsc, tmp, "copy-save", "scan 1\n", steps="700000000", FM1_FLASH_DUMP=str(dump))   # (the first save: after 5 s)
    img = bytearray(dump.read_bytes()) if dump.is_file() else bytearray()
    vm = ble_vm.read(bytes(img)) if img else {"latest": {}}
    data = b"".join(vm["latest"].get(i, b"") for i in ble_vm.RF_IDS)
    kept = len(data) == 97 and img.find(data, 0xFC000) >= 0xFC000
    check("a boot with the VM keeps the four records with the settings (0xFC000 / 0xFD000)", kept,
          f"{len(data)} bytes of trims; found at {img.find(data, 0xFC000) if data else -1:#x}")
    if img:
        img[ble_vm.VM_BASE:ble_vm.VM_BASE + 2 * ble_vm.AREA_SIZE] = b"\xff" * (2 * ble_vm.AREA_SIZE)
        wiped = Path(tmp) / "copy-novm.flash"
        wiped.write_bytes(bytes(img))
        out, air = run(diag, fwsc, tmp, "copy-boot", "scan 1\n", steps="150000000", FM1_FLASH_RESTORE=str(wiped))
        check("the VM erased, the copy kept: the radio starts from the copy and advertises",
              bool(model_times(out, "link 0 advertising started")) and any(e[2] == "ADV_IND" for e in air_events(air)),
              out[-800:])


def steps_ok(out):
    ok = re.findall(r"^  step (.*?): Ok", out, re.M)
    bad = re.findall(r"^  step (.*?): Err\((.*)\)$", out, re.M)
    return ok, bad


def isr_line(out, irq):
    m = re.search(rf"IRQ +{irq}: +(\d+) runs, mean +(\d+) instr \( *([\d.]+) us\), max +(\d+) instr \( *([\d.]+) us\)", out)
    return m.groups() if m else None


def main():
    diag = diagnose_path()
    fwsc = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "ble" / "felucca-ble.fwsc"
    if not diag.is_file():
        print(f"== BLE in the emulator: skipped (no {diag}; FM1_BLE_DIAGNOSE or FM1_EMU, fm1-emulator feat/ble-engine, "
              "cargo build --release --example diagnose)")
        return 0
    if not fwsc.is_file():
        print(f"== BLE in the emulator: skipped (no BLE package {fwsc}: python tools/optimist.py test builds it, "
              "test --no-build and make test on an existing build do not)")
        return 0
    global VM_IMAGE
    with tempfile.TemporaryDirectory(prefix="bledrv-") as tmp:
        VM_IMAGE, which = vm_image(diag, fwsc, tmp)
        check(f"a flash with a VM at 0x093000 to boot over: {which}", VM_IMAGE is not None, which)
        midi = "midi 80 80 90 3c 64"
        out, air = run(diag, fwsc, tmp, "e2e", SCRIPT.format(midi=midi, midi_off="midi 80 80 80 3c 00"), FM1_PRESS=PRESS)
        if "BLE engine:" not in out:
            print(f"== BLE in the emulator: skipped ({diag} has no BLE engine model: fm1-emulator feat/ble-engine)")
            return 0
        ok, bad = steps_ok(out)
        check("the central's script: every step passed (connect .. terminate)", len(ok) == 18 and not bad,
              f"{len(ok)} ok, failed: {bad}")
        adv = re.search(r"first ADV: type 0 addr \[([0-9a-f, ]+)\] data \[([0-9a-f, ]+)\]", out)
        adv_data = adv.group(2).replace(",", "") if adv else ""
        check("advertising: ADV_IND with the flags and the BLE-MIDI UUID", adv_data.startswith("02 01 06 11 07 " + MIDI_UUID),
              adv_data)
        addr = adv.group(1).split(", ") if adv else ["0"]
        check("advertising: a random static address (top bits 11)", int(addr[-1], 16) >> 6 == 3, str(addr))
        rsp = re.search(r"first SCAN_RSP: addr \[[0-9a-f, ]+\] data \[([0-9a-f, ]+)\]", out)
        check("SCAN_RSP: the name FM-1_BLE", bool(rsp) and rsp.group(1).replace(",", "") == NAME)
        check("connect: state 7 programmed before the transmit window",
              bool(re.search(r"state 7 programmed \d+ us after the CONNECT_IND, \d+ us before", out)))
        check("version / features: our VersNr 9, company 0xFFFF; DLE / parameter request claimed",
              "version 9, company 0xffff" in out and "features 0x000000000000002e" in out)
        check("MTU: ours 247", "server MTU 247" in out)
        check("discovery: GAP, GATT (Service Changed, indicate), BLE-MIDI (0x16) and two descriptors",
              "service 0x1800" in out and "service 0x1801" in out and "service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700" in out
              and "characteristic 0x2A05 at 0x0009, value 0x000a, properties 0x20" in out
              and "properties 0x16" in out and "2 descriptors" in out)
        check("CCCD: subscribe answered", "step Subscribe: Ok(\"write response\")" in out)
        check("our L2CAP parameter request {6-9, 0, 100} and the central's update applied at its instant",
              "connection parameter update request: interval 6-9, latency 0, timeout 100" in out
              and re.search(r"instant \d+: interval 48 -> 12 slots", out) is not None)
        notes = re.findall(r"notification [\d.]+ handle 0x000e: \[([0-9a-f, ]+)\]", out)
        on = [n.split(", ") for n in notes if n.split(", ")[2:3] == ["90"]]
        stamp = int(on[0][0], 16) & 0x3F if on else 0
        ts = (stamp << 7 | int(on[0][1], 16) & 0x7F) if on else 0
        check("a key press: a BLE-MIDI note on and off notified on handle 0x000E",
              bool(on) and any(n.split(", ")[2:3] == ["80"] for n in notes), str(notes))
        check("its timestamp is the time of the press (1.5 s, 13 bits: 1400..1700 ms)", 1400 <= ts <= 1700, f"{ts} ms")
        check("channel map and interval updates applied, terminate, advertising again",
              "step ChannelMap" in out and "interval 12 -> 24 slots" in out and
              out.count("advertising started") == 2 and "LL_TERMINATE_IND sent and acknowledged" in out)
        for irq in (29, 45):
            s = isr_line(out, irq)
            check(f"IRQ {irq} ran, each under 100 us at {MHZ} MHz", bool(s) and float(s[4]) < 100.0, str(s))
        # the note: the last second of audio, cut just after the note on (0.998 s), against the same run without
        n_on, _ = run(diag, fwsc, tmp, "note", SCRIPT.format(midi=midi, midi_off=""), steps="125000000")
        n_off, _ = run(diag, fwsc, tmp, "silent", SCRIPT.format(midi="", midi_off=""), steps="125000000")
        loud = [int(m) for m in re.findall(r"BLE run: (\d+) of \d+ recorded audio frames are not silent", n_on + n_off)]
        check("a BLE-MIDI note from the central plays the synth (non-silent frames; none without it)",
              len(loud) == 2 and loud[0] > 1000 and loud[1] == 0, str(loud))
        out, _ = run(diag, fwsc, tmp, "loss", SCRIPT.format(midi=midi, midi_off="midi 80 80 80 3c 00"),
                     FM1_PRESS=PRESS, FM1_BLE_LOSS="3")
        ok, bad = steps_ok(out)
        lost = re.search(r"(\d+) peripheral PDUs lost on purpose", out)
        check("FM1_BLE_LOSS=3: every step still passes (retransmissions both ways)",
              len(ok) == 18 and not bad and bool(lost) and int(lost.group(1)) > 10, f"{len(ok)} ok, {bad}, {lost}")
        out, air = run(diag, fwsc, tmp, "store", SCRIPT.format(midi=midi, midi_off="midi 80 80 80 3c 00"),
                       FM1_PRESS=PRESS, FM1_BLE_LOSS="3", FM1_BLE_MODEL="old_sn=store")
        ok, bad = steps_ok(out)
        check("repeated SNs stored by the engine (old_sn=store) are dropped by the driver: every step passes",
              len(ok) == 18 and not bad and "stored anyway" in air, f"{len(ok)} ok, {bad}")
        out, _ = run(diag, fwsc, tmp, "vanish", VANISH)
        m = re.search(r"advertising again (\d+) ms after the central vanished", out)
        check("the central vanishes: supervision timeout (1 s), advertising again", bool(m) and 900 <= int(m.group(1)) <= 1300,
              m.group(0) if m else out[-2000:])
        menu_checks(diag, fwsc, tmp)
        rf_checks(diag, fwsc, tmp)
        trim_checks(diag, fwsc, tmp)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""BLE MIDI as central, end to end in the FM-1 emulator (docs/BLE-DEVICES-DESIGN.md P3 / P4): a package with
BLE_CENTRAL in fm1-emulator's `diagnose` with its engine model's initiating and master states (BLE-HW-FACTS §21.3 /
§21.4) and a virtual BLE-MIDI peripheral (FM1_BLE_PERIPHERALS: a GATT server with the BLE-MIDI service, notifications,
writes, pairing required as Apple's peripherals do, optionally a resolvable private address with an IRK as a Mac's).

  pick       BLUETOOTH ON (saved), HOME > BLUETOOTH > DEVICES driven with the panel's contacts, the peripheral in the
             list, PRESETS to it, OCT+: our CONNECT_IND from the engine, the first master packet in its transmit
             window, the LL procedures, the MTU, discovery, the CCCD refused (Insufficient Authentication), pairing as
             initiator, the CCCD again, its notes playing the synth, a key press reaching it as a Write Command; it
             becomes LAST in the device store (RAM and the flash dump: bonded, its identity)
  reboot     the flash dump of the pick run restored, the peripheral there with the same bond (and a new private
             address): the FM-1 reconnects by itself (LAST), encrypts with the stored LTK (no pairing), subscribes,
             its notes play
  mac        the same two runs with a Mac-like peripheral behind a resolvable private address: found again after the
             reboot by resolving its new address with the IRK the pairing gave
  none       NONE picked while connected: the link left (our terminate), no search afterwards
  iphone     an iPhone-like peripheral (`auth`: its MIDI characteristic needs an authenticated link, as the iPhone
             showed on the FM-1): Just Works first (silent), its Pairing Failed after the keys and Insufficient
             Authentication again -> the FM-1 pairs again on the same link with passkey entry (DisplayOnly, MITM),
             shows the passkey; the virtual phone user reads it (FM1_BLE_PASSKEY_AT: the firmware's smp_passkey) and
             types it -> the encryption paused and restarted with the new key (LL_PAUSE_ENC), authenticated,
             subscribed, notes, one connection; LAST keeps the authenticated bond and that it needs MITM; reboot ->
             it reconnects with that LTK (authenticated, no pairing, no passkey)
  typo       the same peripheral whose user types the passkey wrong: the passkey pairing fails, and nothing connects
             (or prompts) again: one connection in all, no LAST
  link       the engine as the FM-1 showed it (blell-dev3 / dev4; FM1_BLE_MODEL): the event interrupt after the
             CONNECT_IND 4.8 ms late (the first anchor still in the transmit window), the engine busy after a stop
             (waited for), a CONNECT_IND lost (0x3E: the link made again at once, connected, LAST), and the
             iPhone-like peripheral under all of it

  tests/ble_emu_central_test.py [PACKAGE.fwsc]   (default build/ble/felucca-ble.fwsc)

Skipped (exit 0) without the emulator's peripheral model (fm1-emulator feat/ble-engine: FM1_BLE_PERIPHERALS) or a
package without BLE_CENTRAL. The engine and the peripheral are models: this proves the stack, the driver and the UI
agree with them, not that a real FM-1 connects."""
import os
import re
import sys
import tempfile
from pathlib import Path

import ble_emu_test as E              # (its helpers: run, contact, devices_open, elf_symbols, dump_read, ...)
from tools_path import ROOT

SETTLE = 120_000_000                   # ~1.2 s of guest time at 96 MHz: the list hears the peripheral
CONNECT = 450_000_000                  # ~4.5 s: connect, pair, discover, notes
PASSKEY_CONNECT = 900_000_000          # ~9 s: Just Works refused, again with the passkey (typed after ~1.5 s), notes
fails = 0


def check(what, ok, detail=""):
    global fails
    E.check(what, ok, detail)
    if not ok:
        fails += 1


def peripheral(out, name):
    """the emulator's report line of the virtual peripheral -> {key: value}, or {}"""
    m = re.search(rf"^ble_peripheral name={re.escape(name.replace(' ', '_'))} (.*)$", out, re.M)
    if not m:
        return {}
    return dict(kv.split("=", 1) for kv in m.group(1).split() if "=" in kv)


def engine(out):
    m = re.search(r"^ble_engine_central (.*)$", out, re.M)
    return dict(kv.split("=", 1) for kv in m.group(1).split() if "=" in kv) if m else {}


def loud(out):
    m = re.search(r"BLE run: (\d+) of \d+ recorded audio frames are not silent", out)
    return int(m.group(1)) if m else -1


def num(d, k):
    try:
        return int(d.get(k, "0"))
    except ValueError:
        return 0


def on_image(diag, fwsc, tmp):
    """a flash with the VM and BLUETOOTH ON saved (the menu's toggle in a short run, its dump)"""
    fresh, which = E.vm_image(diag, fwsc, tmp)
    if fresh is None:
        return None
    pr, _, end = E.menu_toggle(150_000_000)
    on = Path(tmp) / "on.flash"
    E.run(diag, fwsc, tmp, "on", "wait 1\n", steps=str(end + 20_000_000), FM1_BLE_CENTRAL="off",
          FM1_PRESS=",".join(pr), FM1_FLASH_RESTORE=str(fresh), FM1_FLASH_DUMP=str(on))
    return on if on.is_file() else None


def pick_presses(connect=CONNECT):
    """DEVICES opened, the list left to fill, PRESETS one detent (the first nearby row: no LAST yet), OCT+ (connect);
    the menu closed (HOME twice: the settings saved) once connected, then a note key -> (contacts, the end)"""
    pr, at = E.devices_open(150_000_000)
    at += SETTLE
    pr += [E.contact(at, E.PRE_B, 2 * E.PHASE), E.contact(at + E.PHASE, E.PRE_A, 2 * E.PHASE)]
    at += 4 * E.PHASE + 3_000_000
    pr.append(E.contact(at, E.OCT_UP, 6_000_000))
    at += connect
    pr += E.home_double(at)
    at += E.DOUBLE + 30_000_000
    pr.append(f"{at}:3:4:9600000")     # a note key (matrix column 3, row 4) held 0.1 s
    return pr, at + 60_000_000


def store_read(out, syms):
    """the device store (ble_store: mark, ver, sel, rsv, then the entry) from the run's RAM dump"""
    if "ble_store" not in syms:
        return None
    a, n = syms["ble_store"]
    data = bytearray()
    for line in re.findall(r"^  ([0-9a-f]{8}): ((?:[0-9a-f?]{2} ?)+)$", out, re.M):
        addr = int(line[0], 16)
        if a <= addr < a + n and addr == a + len(data):
            data += bytes(int(b, 16) if b != "??" else 0 for b in line[1].split())
    return bytes(data[:70]) if len(data) >= 70 else None


def pick_and_reboot(diag, fwsc, tmp, on, spec, name, label):
    want = 0x8F if ":rpa" in spec else 0x87        # (used, role C, bonded, random; with the IRK when it has one)
    syms = E.elf_symbols(fwsc, ("ble_store", "ble_dgc"))
    dumps = ",".join(f"{a:x}:{n}" for a, n in syms.values())
    pr, end = pick_presses()
    flash = Path(tmp) / f"{label}.flash"
    out, air = E.run(diag, fwsc, tmp, f"{label}-pick", "wait 1\n", steps=str(end), FM1_BLE_CENTRAL="off",
                     FM1_BLE_PERIPHERALS=spec, FM1_PRESS=",".join(pr), FM1_FLASH_RESTORE=str(on),
                     FM1_FLASH_DUMP=str(flash), FM1_DUMP=dumps)
    p, en = peripheral(out, name), engine(out)
    first_addr = p.get("addr")
    print(f"    ({label} pick: first master packet {p.get('offset_us')} us into the transmit window, event "
          f"{p.get('first_event')}; engine {en})")
    if not p:
        check(f"{label}: the emulator has the peripheral model (FM1_BLE_PERIPHERALS)", False, out[-3000:])
        return
    check(f"{label}: picked in DEVICES: the engine sent our CONNECT_IND, the first master packet in its transmit window",
          num(en, "connect_inds_sent") >= 1 and num(p, "conns") >= 1 and p.get("in_window") == "yes",
          f"engine {en}\nperipheral {p}")
    check(f"{label}: the master's events, our LL procedures (version, features)",
          num(en, "master_events") > 50 and "LL_VERSION_IND" in p.get("ll", "") and "LL_FEATURE_REQ" in p.get("ll", ""),
          f"{en} {p.get('ll')}")
    check(f"{label}: Insufficient Authentication -> paired as initiator, encrypted, subscribed (MTU {p.get('mtu')})",
          p.get("paired") == "1" and p.get("encrypted") == "1" and p.get("subscribed") == "1", str(p))
    check(f"{label}: its notes play the synth ({loud(out)} non-silent frames), the FM-1's key reaches it as a write",
          loud(out) > 1000 and num(p, "ntf") > 3 and num(p, "writes") >= 1 and "90" in p.get("midi", ""), str(p))
    st = store_read(out, syms)
    check(f"{label}: it is LAST: the store's entry used, ours (role C), bonded (its IRK with a private address), chosen",
          st is not None and st[0] == 0xB6 and st[2] == 1 and (st[10] & want) == want and name.encode()[:16] in st,
          st.hex() if st else out[-2000:])
    if not flash.is_file():
        check(f"{label}: the flash dump", False)
        return
    out, air = E.run(diag, fwsc, tmp, f"{label}-reboot", "wait 1\n", steps="450000000", FM1_BLE_CENTRAL="off",
                     FM1_BLE_PERIPHERALS=spec, FM1_BLE_PERIPHERAL_BOOT="1", FM1_FLASH_RESTORE=str(flash),
                     FM1_DUMP=dumps)
    p = peripheral(out, name)
    if ":rpa" in spec:
        check(f"{label}: rebooted, it advertises from a new private address ({first_addr} -> {p.get('addr')}): found by "
              "resolving it with the IRK the pairing gave", p.get("addr") not in (None, first_addr) and num(p, "conns") >= 1,
              str(p))
    check(f"{label}: after the reboot the FM-1 reconnects to LAST by itself, with the stored LTK (no new pairing)",
          num(p, "conns") >= 1 and num(p, "ltk_reuse") >= 1 and p.get("paired") == "0" and p.get("subscribed") == "1",
          str(p))
    check(f"{label}: ... and its notes play ({loud(out)} non-silent frames)", loud(out) > 1000 and num(p, "ntf") > 3,
          str(p))
    return flash


def none_checks(diag, fwsc, tmp, flash, spec, name):
    """NONE picked while connected to LAST (the reboot's flash): our link left, no search after it"""
    pr, at = E.devices_open(250_000_000)            # (the reconnect is done by then: the list opens over the link)
    at += 30_000_000
    pr.append(E.contact(at, E.OCT_UP, 6_000_000))   # (the cursor on NONE: the list's first row)
    out, _ = E.run(diag, fwsc, tmp, "none", "wait 1\n", steps=str(at + 200_000_000), FM1_BLE_CENTRAL="off",
                   FM1_BLE_PERIPHERALS=spec, FM1_BLE_PERIPHERAL_BOOT="1", FM1_FLASH_RESTORE=str(flash),
                   FM1_PRESS=",".join(pr))
    p = peripheral(out, name)
    check("none: NONE while connected: our TERMINATE_IND (0x13), no reconnection after it",
          num(p, "conns") == 1 and "terminated" in p.get("ended", ""), str(p))


def iphone_checks(diag, fwsc, tmp, on):
    """an iPhone-like peripheral (auth): Just Works refused -> passkey entry typed by its user -> authenticated, LAST
    with the level; reboot -> its authenticated LTK; and a passkey typed wrong: one failure, no loop"""
    syms = E.elf_symbols(fwsc, ("ble_store", "smp_passkey"))
    if "smp_passkey" not in syms:
        check("iphone: the package has smp_passkey (the passkey on screen) for the virtual phone user", False)
        return
    at = f"{syms['smp_passkey'][0]:x}"
    dumps = f"{syms['ble_store'][0]:x}:{syms['ble_store'][1]}"
    name, spec = "iPhone Piano", "midi:iPhone Piano:auth"
    pr, end = pick_presses(PASSKEY_CONNECT)
    flash = Path(tmp) / "iphone.flash"
    out, _ = E.run(diag, fwsc, tmp, "iphone-pick", "wait 1\n", steps=str(end), FM1_BLE_CENTRAL="off",
                   FM1_BLE_PERIPHERALS=spec, FM1_BLE_PASSKEY_AT=at, FM1_PRESS=",".join(pr),
                   FM1_FLASH_RESTORE=str(on), FM1_FLASH_DUMP=str(flash), FM1_DUMP=dumps)
    p = peripheral(out, name)
    typed = re.findall(r"^ble_passkey typed=(\d{6})", out, re.M)
    print(f"    (iphone pick: {p})")
    check("iphone: Just Works first (no passkey, nothing to type), its Pairing Failed after the keys, refused again",
          num(p, "late_fails") == 1 and num(p, "auth_errors") >= 2, str(p))
    check("iphone: paired again on the same link with passkey entry; the passkey on the FM-1's screen (smp_passkey) "
          f"typed by the phone's user ({typed})", num(p, "passkey_waits") == 1 and len(typed) == 1 and
          p.get("passkey") == typed[0], str(p))
    check("iphone: the new key through the encryption pause (LL_PAUSE_ENC_REQ / RSP), authenticated (passkey bond), "
          "subscribed; exactly one connection (no reconnection, no pairing loop)",
          num(p, "auth_pairings") == 1 and p.get("authenticated") == "1" and p.get("subscribed") == "1" and
          num(p, "conns") == 1 and num(p, "pauses") == 1 and "LL_PAUSE_ENC_RSP" in p.get("ll", ""), str(p))
    check(f"iphone: its notes play the synth ({loud(out)} non-silent frames), the FM-1's key reaches it",
          loud(out) > 1000 and num(p, "ntf") > 3 and num(p, "writes") >= 1, str(p))
    st = store_read(out, syms)
    check("iphone: LAST, bonded, its bond authenticated and that it needs MITM kept (sec = AUTH | MITM)",
          st is not None and (st[10] & 0x87) == 0x87 and st[69] == 0x03 and name.encode() in st,
          st.hex() if st else out[-2000:])
    if not flash.is_file():
        check("iphone: the flash dump", False)
        return
    out, _ = E.run(diag, fwsc, tmp, "iphone-reboot", "wait 1\n", steps="450000000", FM1_BLE_CENTRAL="off",
                   FM1_BLE_PERIPHERALS=spec, FM1_BLE_PERIPHERAL_BOOT="1", FM1_BLE_PASSKEY_AT=at,
                   FM1_FLASH_RESTORE=str(flash))
    p = peripheral(out, name)
    check("iphone: after the reboot LAST reconnects by itself with the authenticated LTK: no pairing, no passkey, "
          "subscribed, notes", num(p, "ltk_reuse") >= 1 and p.get("authenticated") == "1" and p.get("paired") == "0"
          and num(p, "passkey_waits") == 0 and p.get("subscribed") == "1" and loud(out) > 1000, str(p))
    name, spec = "Typo Phone", "midi:Typo Phone:auth:typo"
    pr, end = pick_presses(PASSKEY_CONNECT)
    out, _ = E.run(diag, fwsc, tmp, "typo-pick", "wait 1\n", steps=str(end + 300_000_000), FM1_BLE_CENTRAL="off",
                   FM1_BLE_PERIPHERALS=spec, FM1_BLE_PASSKEY_AT=at, FM1_PRESS=",".join(pr),
                   FM1_FLASH_RESTORE=str(on), FM1_DUMP=dumps)
    p = peripheral(out, name)
    st = store_read(out, syms)
    check("typo: the passkey typed wrong: the pairing fails once; nothing connects or prompts again (one connection, "
          "no LAST, not subscribed)", num(p, "conns") == 1 and num(p, "passkey_waits") == 1 and
          num(p, "auth_pairings") == 0 and p.get("subscribed") == "0" and st is not None and st[0] == 0xB6 and
          not (st[10] & 0x80), f"{p}\n{st.hex() if st else ''}")


def link_checks(diag, fwsc, tmp, on):
    """the engine as the FM-1 showed it (blell-dev3 / dev4): a late event interrupt after the CONNECT_IND, the engine
    busy after a stop, a CONNECT_IND lost; the old firmware failed with 0x3E and gave up (FAILED: LINK LOST)"""
    syms = E.elf_symbols(fwsc, ("ble_store", "smp_passkey"))
    dumps = f"{syms['ble_store'][0]:x}:{syms['ble_store'][1]}"
    name, spec = "BLE Keys", "midi:BLE Keys:pair"
    pr, end = pick_presses()
    out, _ = E.run(diag, fwsc, tmp, "late-pick", "wait 1\n", steps=str(end), FM1_BLE_CENTRAL="off",
                   FM1_BLE_PERIPHERALS=spec, FM1_PRESS=",".join(pr), FM1_FLASH_RESTORE=str(on), FM1_DUMP=dumps,
                   FM1_BLE_MODEL="init_evt_delay_us=4800,busy_after_stop_us=900")
    p, en = peripheral(out, name), engine(out)
    print(f"    (late: first master packet {p.get('offset_us')} us into the transmit window; engine {en})")
    check("link: the event interrupt 4.8 ms after the CONNECT_IND (0x3E on the FM-1, blell-dev4): the first master "
          "packet still in the transmit window, connected at the first CONNECT_IND, subscribed, notes",
          num(en, "connect_inds_sent") == 1 and p.get("in_window") == "yes" and num(p, "conns") == 1 and
          p.get("subscribed") == "1" and loud(out) > 1000, f"engine {en}\nperipheral {p}")
    check("link: the engine busy 0.9 ms after each stop (0x28038 bit1): waited for, the links made as before",
          num(en, "busy_stops") >= 1 and num(p, "established") == 1, str(en))
    out, _ = E.run(diag, fwsc, tmp, "lost-pick", "wait 1\n", steps=str(end), FM1_BLE_CENTRAL="off",
                   FM1_BLE_PERIPHERALS=spec, FM1_PRESS=",".join(pr), FM1_FLASH_RESTORE=str(on), FM1_DUMP=dumps,
                   FM1_BLE_MODEL="cind_lost=1,busy_after_stop_us=900")
    p, en = peripheral(out, name), engine(out)
    st = store_read(out, syms)
    print(f"    (lost: engine {en})")
    check("link: a CONNECT_IND lost (radio life: 0x3E, 2 in 6 on the FM-1): the link made again at once (no FAILED), "
          "connected, subscribed, LAST", num(en, "cind_lost") == 1 and num(en, "connect_inds_sent") >= 2 and
          num(p, "conns") == 1 and p.get("subscribed") == "1" and loud(out) > 1000 and st is not None and
          (st[10] & 0x87) == 0x87, f"engine {en}\nperipheral {p}\n{st.hex() if st else ''}")
    if "smp_passkey" not in syms:
        return
    name, spec = "iPhone Piano", "midi:iPhone Piano:auth"
    pr, end = pick_presses(PASSKEY_CONNECT)
    out, _ = E.run(diag, fwsc, tmp, "iphone-hw-pick", "wait 1\n", steps=str(end), FM1_BLE_CENTRAL="off",
                   FM1_BLE_PERIPHERALS=spec, FM1_BLE_PASSKEY_AT=f"{syms['smp_passkey'][0]:x}",
                   FM1_PRESS=",".join(pr), FM1_FLASH_RESTORE=str(on), FM1_DUMP=dumps,
                   FM1_BLE_MODEL="init_evt_delay_us=4800,busy_after_stop_us=900,cind_lost=1")
    p, en = peripheral(out, name), engine(out)
    st = store_read(out, syms)
    print(f"    (iphone, as blell-dev4: {p}; engine {en})")
    check("link: the iPhone-like peripheral as on the FM-1 (late event, busy engine, the first CONNECT_IND lost): "
          "connected on the second, Just Works then the passkey on that link, authenticated, subscribed, LAST with "
          "its level", num(en, "cind_lost") == 1 and num(p, "conns") == 1 and num(p, "passkey_waits") == 1 and
          num(p, "pauses") == 1 and p.get("authenticated") == "1" and p.get("subscribed") == "1" and
          loud(out) > 1000 and st is not None and st[69] == 0x03, f"engine {en}\nperipheral {p}")


def main():
    diag = E.diagnose_path()
    fwsc = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "ble" / "felucca-ble.fwsc"
    if not diag.is_file() or not fwsc.is_file():
        print(f"== BLE central in the emulator: skipped (no {diag} or no {fwsc})")
        return 0
    if "ble_store" not in E.elf_symbols(fwsc, ("ble_store", "ble_found")) or \
            "ble_found" not in E.elf_symbols(fwsc, ("ble_found",)):
        print("== BLE central in the emulator: skipped (the package has no BLE_CENTRAL)")
        return 0
    with tempfile.TemporaryDirectory(prefix="blecen-") as tmp:
        on = on_image(diag, fwsc, tmp)
        check("a flash with the VM and BLUETOOTH ON to boot over", on is not None)
        if on is None:
            return 1
        probe, _ = E.run(diag, fwsc, tmp, "probe", "wait 1\n", steps="2000000", FM1_BLE_CENTRAL="off",
                         FM1_BLE_PERIPHERALS="midi:Probe", FM1_FLASH_RESTORE=str(on))
        if "ble_peripheral name=Probe" not in probe:
            print(f"== BLE central in the emulator: skipped ({diag} has no virtual peripheral: fm1-emulator "
                  "feat/ble-engine, FM1_BLE_PERIPHERALS)")
            return 0
        keys = "BLE Keys"
        flash = pick_and_reboot(diag, fwsc, tmp, on, f"midi:{keys}:pair", keys, "keys")
        pick_and_reboot(diag, fwsc, tmp, on, "midi:Mac Studio:pair:rpa", "Mac Studio", "mac")
        if flash:
            none_checks(diag, fwsc, tmp, flash, f"midi:{keys}:pair", keys)
        if "passkey_waits=" in probe:
            iphone_checks(diag, fwsc, tmp, on)
        else:
            print("    (iphone, typo: skipped: this emulator's peripheral has no auth mode)")
        if "cind_lost=" in probe and "pauses=" in probe:
            link_checks(diag, fwsc, tmp, on)
        else:
            print("    (link: skipped: this emulator's engine has no init_evt_delay_us / busy_after_stop_us / "
                  "cind_lost, or its peripheral no encryption pause)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

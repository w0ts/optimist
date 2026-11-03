#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""tools/fm1_install.py against a simulated FM-1 (no hardware, no mido).
The fake device is the one in web/test_web.mjs: identity on the handshake,
then "device asks, host answers" reads of the logical image. Run from the repo root:
  python3 tests/install_test.py
Also checks logical_image/product_of against web/fm1base/fm1pkg.js (needs node and
build/felucca.fwsc, skipped otherwise)."""
import io
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

I.DELAY.update(info=0.2, open=0, start=0.01, reply=0, loader=0.01, reboot=0.01, retry=0.02, poll=0.05, hs=0.1,
               idle_check=0.4, idle_write=0.4, wait_loader=2, wait_reboot=2, resume_poll=0.05, resume_cap=0.5)
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
    failed += not cond


# ---------------------------------------------------------- simulated FM-1 ---

class FakeLink:
    def __init__(self, dev, gen):
        self.dev, self.gen, self.q, self.closed = dev, gen, queue.Queue(), False

    @property
    def lost(self):
        return self.gen != self.dev.gen or not self.dev.connected

    def send(self, pkt):
        if self.lost:
            return False
        self.dev.sent.append(bytes(pkt))
        self.dev.rx(bytes(pkt))
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        self.closed = True


class FakeFM1:
    """a MIDI backend with one FM-1 on it"""

    def __init__(self, image, identity="FM-1_015", name="FM-1", unplug_after=None, after_write="FM-1_900",
                 stall_after=None, bad_addr=None, boot=None, fast=False, comes_back=None, check_frames=False, check_time=0.0,
                 keeps_session=False):
        self.image, self.unplug_after, self.after_write = image, unplug_after, after_write
        self.boot_word, self.infos = boot, 0                 # (boot: INFO tag 0x68's word, an Optimist since 2026-10-31)
        self.stall_after, self.bad_addr = stall_after, bad_addr
        self.fast, self.fast_on, self.fast_words = fast, False, 0     # (the fast install: docs/INSTALL-SPEED.md)
        self.served = self.bad = self.upgrades = 0
        self.stops = self.stops_before_upgrade = 0      # (MIDI STOP 0xFC received; how many came before the first upgrade command)
        self.soft_keys = 0                              # (the download-mode soft key F0 22 24 35 7D F7 received)
        self.sent, self.links, self.gen, self.lock = [], [], 0, threading.Lock()
        self.written = set()                            # (the fast loader's sectors already right: a resumed write skips them)
        # comes_back: [(identity, name, seconds, unplug again after that many more reads or None)], one per drop: the cable
        # back by itself; check_time: seconds the fast loader checks each sector before it asks one (or the end);
        # check_frames: it says so with 0x32 frames (checked, total); check_seen: the frames sent
        self.comes_back, self.check_frames, self.check_time = list(comes_back or []), check_frames, check_time
        self.checked, self.check_seen = 0, []
        # keeps_session: the loader stays in its fast session over a replug (as on the FM-1, 2026-11-01): a new hello and
        # upgrade command mid-session go unheard, and it asks its next read again (ota_read's next try)
        self.keeps_session, self.in_session, self.unheard = keeps_session, False, 0
        self.sector_reads = 0                           # (4 KiB sector reads answered, the table not counted)
        self.boot(identity, name)

    def boot(self, identity, name):
        with self.lock:
            self.gen += 1
            self.identity, self.name, self.connected = identity, name, True
            self.waiting, self.queue = None, []

    def input_names(self):
        return [self.name] if self.connected else []

    output_names = input_names

    def open(self, in_name, out_name):
        if in_name != self.name or not self.connected:
            raise IOError("no such port")
        link = FakeLink(self, self.gen)
        self.links.append(link)
        return link

    def tx(self, pkt):
        for link in self.links:
            if link.gen == self.gen and not link.closed:
                link.q.put(bytes(pkt))

    def rx(self, d):
        if d == I.MIDI_STOP:
            self.stops += 1
            self.stops_before_upgrade += not self.upgrades
        elif d == bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7]):
            self.soft_keys += 1
        elif d == I.ED_INFO:
            self.infos += 1
            if self.boot_word:
                t = b"boot=" + self.boot_word.encode()
                self.tx(bytes([0xF0, 0x7D, 0x46, 0x4C, 1]) + b"FELUCCA 0.2" + bytes([0x66, 2]) + b"ab" +
                        bytes([0x68, len(t)]) + t + b"\xF7")
        elif self.in_session and self.keeps_session and d in (I.FAST_HELLO, I.UPGRADE):
            self.unheard += 1
        elif d == I.FAST_HELLO:
            self.fast_words += 1
            if self.fast and self.identity.startswith("ota-"):
                self.fast_on = True
                self.tx(I.frame(I.FAST_CMD, [1, 0x00, 0x10]))
        elif d == I.HS_QUERY:
            t = self.identity.encode()
            self.tx(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7")
        elif d == I.UPGRADE:
            self.upgrades += 1
            self.checked = 0
            self.in_session = self.identity.startswith("ota-") and self.fast_on
            first = self.bad_addr if self.bad_addr is not None else 0
            n = len(self.image) // I.SECTOR
            self.queue = ([(I.TABLE, n * 6)] + [(k * I.SECTOR, I.SECTOR) for k in range(n) if k * I.SECTOR not in self.written]
                          + [(I.FINISH_WRITE, 8)]
                          if self.identity.startswith("ota-") and self.fast_on
                          else [(first + k * 512, 512) for k in range(6)] + [(I.FINISH_WRITE, 8)]
                          if self.identity.startswith("ota-")
                          else [(0, 64), (0x40, 160), (0x1000, 512), (I.FINISH_CHECK, 8)])
            self.next()
        elif self.waiting:
            u = I.unpack7(d[1:-1])
            addr, n = self.waiting
            fin = addr >= I.FINISH_CHECK
            want = b"success\0" if fin else I.answer(self.image, addr, n, I.FAST_CHUNK)
            if u[14:14 + len(want)] != want or len(u) != 15 + len(want):
                self.bad += 1
            self.waiting = None
            self.served += 1
            if self.fast_on and addr < I.TABLE and n == I.SECTOR:
                self.sector_reads += 1
                self.written.add(addr)
            if self.unplug_after and self.served >= self.unplug_after:
                self.connected = False
                if self.comes_back:
                    who, nm, secs, more = self.comes_back.pop(0)
                    threading.Timer(secs, self.back, (who, nm, more)).start()
                return
            if self.stall_after and self.served >= self.stall_after:
                return
            if addr == I.FINISH_CHECK:
                threading.Timer(0.05, self.boot, ("ota-FM-1_900", "Felucca Update")).start()
            elif addr == I.FINISH_WRITE:
                self.in_session = False
                threading.Timer(0.05, self.boot, (self.after_write, "Felucca")).start()
            else:
                self.advance()

    def back(self, identity, name, more):
        self.unplug_after = self.served + more if more else None
        if self.keeps_session and self.in_session:
            with self.lock:
                self.gen += 1
                self.name, self.connected = name, True
            threading.Timer(0.1, self.reask, (self.gen, None, 0)).start()   # (its read asked again: ota_read's tries)
            return
        self.boot(identity, name)

    def advance(self):
        """the fast loader checks the sectors up to the next one it asks (or all, before the end), then asks"""
        if not (self.fast_on and self.identity.startswith("ota-") and self.queue and (self.check_time or self.check_frames)):
            return self.next()
        addr = self.queue[0][0]
        total = len(self.image) // I.SECTOR
        upto = addr // I.SECTOR if addr < I.TABLE else total
        gen = self.gen

        def run():
            for c in range(self.checked, upto):
                time.sleep(self.check_time)
                if self.check_frames and gen == self.gen:
                    self.check_seen.append(c + 1)
                    self.tx(I.frame(I.CHECK_CMD, [(c + 1) & 0xFF, (c + 1) >> 8, total & 0xFF, total >> 8]))
            self.checked = upto + 1
            if gen == self.gen and self.connected:
                self.next()
        threading.Thread(target=run, daemon=True).start()

    def reask(self, gen, req, tries):
        """the loader's next try of the read it waits for (a real one: every 5 s, three tries)"""
        if gen != self.gen or not self.connected or not self.in_session or tries > 40:
            return
        if req is None and self.waiting is None:
            self.advance()
        elif req is not None and self.waiting is req:
            self.request(*req)
        elif req is not None:
            return                                    # (answered: the session goes on by itself)
        threading.Timer(0.2, self.reask, (gen, self.waiting, tries + 1)).start()

    def next(self):
        if not self.queue:
            return
        self.waiting = self.queue.pop(0)
        self.request(*self.waiting)

    def request(self, addr, n):
        u = bytearray([0, 0x59, 0x30, 0, 0, 0, 0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        u.append(~sum(u[6:14]) & 0xFF)
        self.tx(b"\xF0" + I.pack7(u) + b"\xF7")


# ----------------------------------------------------------------- helpers ---

def package(product="FM-1_900", marker=True, size=0x2000, damage=False):
    """a package with a valid header and file list (one file: 0x400 to the end), as fm1pkg_make writes"""
    img = bytearray((i * 7) & 0xFF for i in range(size))
    img[0x1800:0x1800 + 16] = I.LOADER_MARK if marker else bytes(16)
    data = bytes(img[0x400:])
    e = bytearray(0x50)
    e[0:16] = (0).to_bytes(2, "little") + (0).to_bytes(2, "little") + I._crc16(data).to_bytes(2, "little") + bytes(2) + \
        (0x400).to_bytes(4, "little") + len(data).to_bytes(4, "little")
    e[0x40:0x49] = b"flash.bin"
    lst = I._dec(bytes(e))
    hdr = bytearray(0x40)
    hdr[4:8] = size.to_bytes(4, "little")
    hdr[8:10] = (1).to_bytes(2, "little")
    hdr[2:4] = I._crc16(lst).to_bytes(2, "little")
    hdr[0:2] = I._crc16(bytes(hdr[2:0x40])).to_bytes(2, "little")
    img[0:0x40] = I._dec(bytes(hdr))
    img[0x40:0x90] = lst
    if damage:
        img[0x1000] ^= 1
    raw = bytearray()
    for i in range(I.BLOCKS):
        m = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
        raw += img[i * I.KEEP:(i + 1) * I.KEEP] + bytes([m])
    raw += img[I.BLOCKS * I.KEEP:]
    return bytes(raw)


TMP = Path(tempfile.mkdtemp(prefix="felucca-install-"))


def pkgfile(name, raw):
    p = TMP / name
    p.write_bytes(raw)
    return str(p)


def cli(args, dev, answer=True, ask_text=None, cancel=None):
    out, err = io.StringIO(), io.StringIO()
    old, sys.stderr = sys.stderr, err
    try:
        rc = I.main(args, backend=dev, out=out, ask=lambda _p: answer, ask_text=ask_text, cancel_check=cancel)
    finally:
        sys.stderr = old
    return rc, out.getvalue(), err.getvalue()


# ------------------------------------------------------------------- tests ---

def wire():
    data = bytes(range(256)) * 3
    ok(I.unpack7(I.pack7(data))[:len(data)] == data, "pack7 / unpack7 round trip")
    pkt = I.response(0x12345, b"\x01\x02\x03", fl=5)
    u = I.unpack7(pkt[1:-1])
    ok(u[:3] == b"\x00\x59\x30" and int.from_bytes(u[7:11], "little") == 0x12345 and u[14:17] == b"\x01\x02\x03"
       and ~sum(u[6:17]) & 0xFF == u[17], "response: header, address, data, checksum")
    raw = package("FM-1_906")
    ok(I.product_of(raw) == "FM-1_906" and len(I.logical_image(raw)) == len(raw) - 20, "product_of / logical_image (synthetic)")


def installs():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 2 and dev.served == 11 and "done: the FM-1 runs FM-1_900" in out
       and "100%" in out, f"install: running -> loader -> Felucca ({dev.served} reads)")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 1 and "update mode" in out, "install: device already in update mode -> finishes the write")

    # package identity (docs: BUILDING.md "Package identity"): Optimist is FM-1_700 / 720-769; the loaders of
    # Optimist (ota-FM-1_7XX) and of SLOOP / Felucca / X0X (ota-FM-1_9XX) speak the same protocol and
    # are resumed; any other loader (M-VAVE ota-FM-1_0XX, Lunar's planned 5XX) is foreign
    for ident in ("ota-FM-1_700", "ota-FM-1_712", "ota-FM-1_905"):
        dev = FakeFM1(image, identity=ident, name="Optimist Update")
        rc, out, err = cli([p, "--yes"], dev)
        ok(rc == 0 and dev.bad == 0 and dev.upgrades == 1, f"install: resumes the loader {ident}")
    for nm in ("FM1-1A2B", "Optimist"):                  # (the port name of Optimist's firmware: FM1-XXXX since 2026-10-29, "Optimist" before)
        rc, out, err = cli(["--info"], FakeFM1(image, name=nm))
        ok(rc == 0 and "FM-1" in out, f"the port {nm!r} is found without --port")
    # two FM-1s on the cable (2026-11-01): without --port nothing is probed or written; with it, that one
    ok(I.fm1_devices(["FM1-1A2B", "FM1-1A2B Bluetooth", "FM-1", "IAC Bus 1", "USB-MIDI", "fm1-1a2b"]) == ["FM1-1A2B", "FM-1"],
       "fm1_devices: one key per FM-1 (its FM1-XXXX), Bluetooth and other gear left out")
    two = FakeFM1(image, name="FM1-1A2B")
    two.input_names = two.output_names = lambda: ["FM1-1A2B", "FM1-9C0D"]
    rc, out, err = cli([p, "--yes"], two)
    ok(rc == 9 and two.sent == [] and two.upgrades == 0 and "--port" in err, "two FM-1s, no --port: refused (exit 9, not 3: none found), nothing sent")
    # an FM-1 and a sound card called "USB MIDI Device" (the user's, 2026-11-03): one FM-1, no question; two FM-1s (one
    # still "Optimist", an older build's name): --list gives both, never the sound card
    snd = FakeFM1(image, name="Optimist")
    snd.input_names = snd.output_names = lambda: ["USB MIDI Device Port 1", "USB MIDI Device Port 2", "Optimist"]
    ok(I.several(I.Updater(snd, None)) is None, "an FM-1 beside a sound card: not 'several'")
    two2 = FakeFM1(image, name="Optimist")
    two2.input_names = two2.output_names = lambda: ["USB MIDI Device Port 1", "Optimist", "FM1-620C"]
    rc, out, err = cli(["--list"], two2)
    ok(rc == 0 and out.split() == ["Optimist", "FM1-620C"] and two2.sent == [], "--list: both FM-1s, not the sound card, nothing sent")
    rc, out, err = cli([p, "--yes"], two2)
    ok(rc == 9 and two2.sent == [] and "FM1-620C" in err and "USB MIDI" not in err, "two FM-1s beside a sound card: refused (9), the sound card not named")
    two = FakeFM1(image, name="FM1-1A2B")
    names = two.input_names
    two.input_names = two.output_names = lambda: names() + ["FM1-9C0D"]
    rc, out, err = cli(["--info", "--port", "1A2B"], two)
    ok(rc == 0 and "FM-1" in out, "two FM-1s, --port 1A2B: that one answers")
    dev = FakeFM1(image, name="Launchkey Mini MK3")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 3 and dev.sent == [], "other gear is never probed")
    dev = FakeFM1(image, identity="ota-FM-1_500", name="FM-1 Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "another firmware" in err, "install: ota-FM-1_500 is another firmware's loader")
    raw7 = package("FM-1_712")
    dev = FakeFM1(I.logical_image(raw7), after_write="FM-1_712")
    rc, out, err = cli([pkgfile("optimist.fwsc", raw7), "--yes"], dev)
    ok(rc == 0 and "done: the FM-1 runs FM-1_712" in out, "install: an Optimist package (FM-1_712) from stock V15")

    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.stops_before_upgrade == 1 and dev.stops == 1, "install: one MIDI STOP (0xFC) before the update command")

    dev = FakeFM1(image)
    rc, out, err = cli([p], dev, answer=False)
    ok(rc == 1 and dev.upgrades == 0 and "cancelled" in out, "install: answer no -> nothing sent but the handshake")

    dev = FakeFM1(image, identity="FM-1_905", name="Felucca")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "FM-1_905" in out and "running" in out and dev.upgrades == 0, "--info: identity of the connected FM-1")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "update loader" in out, "--info: device in update mode")


def errors():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    rc, out, err = cli([p, "--yes"], FakeFM1(image, name="IAC Driver Bus 1"))
    ok(rc == 3 and "not found" in err and "IAC Driver" in err, "no FM-1 port -> exit 3, lists the MIDI inputs")
    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes", "--port", "Felucca"], dev)
    ok(rc == 3 and dev.upgrades == 0, "--port that matches nothing -> exit 3")
    dev = FakeFM1(image, name="My Interface")
    rc, out, err = cli(["--info", "--port", "my int"], dev)
    ok(rc == 0 and "FM-1_015" in out, "--port picks a port the default match skips")

    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, unplug_after=2))
    ok(rc == 4 and "disconnected" in err and "nothing was written" in err and "did not come back" in err and time.monotonic() - t0 < 3,
       "unplugged in step 1, never back (--yes: the cap, 0.5 s here) -> exit 4 'lost', nothing written")
    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", unplug_after=3))
    ok(rc == 4 and "run the install again" in err and time.monotonic() - t0 < 3, "unplugged during the write, never back -> exit 4 after the cap")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", stall_after=3))
    ok(rc == 4 and "stopped answering" in err, "loader stops answering -> exit 4 after the idle time")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", bad_addr=len(image)))
    ok(rc == 4 and "outside the package" in err, "read past the package -> exit 4")

    rc, out, err = cli([p, "--yes"], FakeFM1(image, after_write="FM-1_015"))
    ok(rc == 6 and "reports FM-1_015" in err, "another identity after the restart -> exit 6")
    dev = FakeFM1(image, identity="XY-9_001", name="usb-midi")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "another model -> exit 6, nothing sent")
    dev = FakeFM1(image, identity="ota-FM-1_015", name="FM-1 Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "another firmware" in err, "another firmware's update loader -> exit 6, nothing sent")

    class Stuck(FakeFM1):          # the loader never shows up
        def boot(self, identity, name):
            super().boot(identity, name)
            if identity.startswith("ota-"):
                self.connected = False
    rc, out, err = cli([p, "--yes"], Stuck(image))
    ok(rc == 5 and "loader did not appear" in err, "no loader after step 1 -> exit 5")

    plain = pkgfile("plain.fwsc", package(marker=False))
    dev = FakeFM1(image)
    rc, out, err = cli([plain, "--yes"], dev)
    ok(rc == 2 and "no Felucca update loader" in err and dev.sent == [], "package without the loader marker -> exit 2, no MIDI")
    dev = FakeFM1(I.logical_image(package(marker=False)))
    rc, out, err = cli([plain, "--yes", "--force"], dev)
    ok(rc == 0 and dev.bad == 0, "... installs with --force")
    dev = FakeFM1(image)
    rc, out, err = cli([pkgfile("damaged.fwsc", package(damage=True)), "--yes"], dev)
    ok(rc == 2 and "damaged package" in err and dev.sent == [], "damaged package (a file's CRC) -> exit 2, no MIDI")
    stock = ROOT / "web" / "stock" / "FM-1_015.fwsc"   # (M-VAVE's V15: its USR entry fails package_damage's CRC)
    if stock.is_file():
        product, _ = I.load_package(str(stock), False)
        ok(product == "FM-1_015", "M-VAVE's V15 (by its sha256) loads: no loader marker, its USR CRC as M-VAVE made it")

    # an Optimist build running (FM-1_7NN): an fm1-base package (FM-1_79..) or another layout is warned about first
    # (2026-10-29: such a package stopped at the device's loader check, -16)
    own = package("FM-1_700", size=0x93400)              # (flash.bin 0x93000: Optimist's normal layout)
    dev = FakeFM1(I.logical_image(own), identity="FM-1_700", name="Optimist", after_write="FM-1_700")
    rc, out, err = cli([pkgfile("own.fwsc", own), "--yes"], dev)
    ok(rc == 0 and "warning" not in out, "Optimist over Optimist, the normal layout: no warning")
    fm1b = package("FM-1_7900", size=0x93400)
    os.environ["M_FLASHER"] = str(TMP / "no-m-flasher")   # (no M-Flasher here: download mode stops before the soft key)
    dev = FakeFM1(I.logical_image(fm1b), identity="FM-1_700", name="Optimist")
    rc, out, err = cli([pkgfile("fm1b.fwsc", fm1b), "--yes"], dev)
    ok(rc == 8 and "route:    download" in out and "M-Flasher is not installed" in err and dev.upgrades == 0
       and not dev.soft_keys, "fm1-base package over Optimist: download mode; no M-Flasher -> exit 8, nothing sent")
    dev = FakeFM1(I.logical_image(fm1b), identity="FM-1_700", name="Optimist")
    rc, out, err = cli([pkgfile("fm1b.fwsc", fm1b)], dev, answer=False)
    ok(rc == 1 and "cancelled" in out and dev.upgrades == 0 and not dev.soft_keys, "... asked: no -> cancelled")
    # --route loader: the running firmware's loader as before (its warning, --force)
    dev = FakeFM1(I.logical_image(fm1b), identity="FM-1_700", name="Optimist")
    rc, out, err = cli([pkgfile("fm1b.fwsc", fm1b), "--yes", "--route", "loader"], dev)
    ok(rc == 6 and "fm1-base package" in err and "--force" in err and dev.upgrades == 0,
       "--route loader: fm1-base over Optimist, --yes: stops before anything is sent")
    dev = FakeFM1(I.logical_image(fm1b), identity="FM-1_700", name="Optimist", after_write="FM-1_7900")
    rc, out, err = cli([pkgfile("fm1b.fwsc", fm1b), "--yes", "--force", "--route", "loader"], dev)
    ok(rc == 0 and "warning:" in out and dev.upgrades == 2, "... --force installs it anyway")
    odd = package("FM-1_700", size=0xCE400)              # (flash.bin 0xCE000: fm1-base's SD card mode slot)
    dev = FakeFM1(I.logical_image(odd), identity="FM-1_700", name="Optimist")
    rc, out, err = cli([pkgfile("odd.fwsc", odd), "--yes", "--route", "loader"], dev)
    ok(rc == 6 and "0xce000" in err and dev.upgrades == 0, "--route loader: another layout's flash.bin over Optimist: stops")
    dev = FakeFM1(I.logical_image(odd), identity="FM-1_700", name="Optimist")
    rc, out, err = cli([pkgfile("odd.fwsc", odd), "--yes"], dev)
    ok(rc == 8 and "0xce000" in out and dev.upgrades == 0, "auto: that layout over Optimist goes to download mode")
    dev = FakeFM1(I.logical_image(fm1b), identity="FM-1_7900", name="Optimist", after_write="FM-1_7900")
    rc, out, err = cli([pkgfile("fm1b.fwsc", fm1b), "--yes"], dev)
    ok(rc == 0 and "warning" not in out, "fm1-base over fm1-base: no warning")

    rc, out, err = cli([pkgfile("short.fwsc", b"\0" * 100), "--yes"], FakeFM1(image))
    ok(rc == 2 and "too short" in err, "not a package -> exit 2")
    rc, out, err = cli([str(TMP / "missing.fwsc"), "--yes"], FakeFM1(image))
    ok(rc == 2, "missing file -> exit 2")


def fast():
    """the fast install's word, table and 4 KiB reads (docs/INSTALL-SPEED.md); an older loader gets today's protocol"""
    vec = bytes(((i * 31) ^ (i >> 8)) & 0xFF for i in range(8192))
    ok(I.FAST_HELLO.hex() == "f0003245190000400000203807f7" and I.sector_table(vec, 0, 2).hex() == "6204a25f925f5cc59b283583"
       and I.parse_fast(I.frame(I.FAST_CMD, [1, 0, 16])) == 4096, "fast: the word and the table (the web's, the app's, the loader's vectors)")
    ok(I.answer(vec, 0, 4096, I.MAXDATA) is None and I.answer(vec, I.TABLE, 12, I.MAXDATA) is None
       and I.answer(vec, I.TABLE + 4096, 12, I.FAST_CHUNK) is None, "fast: 4 KiB reads and the table only after the word, inside the package")
    raw = package(size=0x6000)
    image = I.logical_image(raw)
    p = pkgfile("fast.fwsc", raw)
    dev = FakeFM1(image, fast=True)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.fast_on and dev.bad == 0 and "fast install" in out, f"fast: a fast loader: table + 4 KiB reads ({dev.served} reads)")
    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.fast_words == 1 and not dev.fast_on and dev.bad == 0 and "today's protocol" in out,
       "fast: an older loader ignores the word: today's protocol")


class Units:
    """a MIDI backend with several FakeFM1s on it (each answers on its own port names only)"""

    def __init__(self, *units):
        self.units = units

    def input_names(self):
        return [n for u in self.units for n in u.input_names()]

    output_names = input_names

    def open(self, in_name, out_name):
        for u in self.units:
            if in_name in u.input_names():
                return u.open(in_name, out_name)
        raise IOError("no such port")


def back(identity="ota-FM-1_900", name="Felucca Update", secs=0.3, more=None):
    """one return of the cable: the FM-1 back by itself after secs (more: dropped again after that many reads)"""
    return (identity, name, secs, more)


def dropped():
    """the cable dropped mid-install (2026-11-01): said, then the install goes on BY ITSELF when the FM-1 is back (the
    user: "the flasher should automatically resume"): step 2 on the loader (only the sectors left), step 1 again on the
    same running firmware; q cancels while it waits; --yes waits too, up to resume_cap; two FM-1s: the same unit only"""
    I.DELAY.update(resume_poll=0.05, resume_cap=3)
    raw = package(size=0x6000)                       # (6 sectors: the fast loader's table, then 4 KiB reads)
    image = I.logical_image(raw)
    p = pkgfile("drop.fwsc", raw)
    n = len(image) // I.SECTOR
    ok(I.DROPPED == "Connection dropped. Check the cable: the install continues by itself when the FM-1 is back. "
                    "Do not power off the FM-1.", "drop: the sentence (no Enter: the install continues by itself; do not power off)")
    polls = []
    never = lambda: polls.append(1) and False         # (interactive, nobody types: the terminal's q never comes)

    dev = FakeFM1(image, fast=True, unplug_after=4 + 1 + 2, comes_back=[back()])  # (step 1's 4 reads, the table, 2 sectors)
    rc, out, err = cli([p], dev, cancel=never)
    ok(rc == 0 and I.DROPPED + " (q to cancel)" in out and dev.bad == 0 and dev.sector_reads == n
       and "found again: ota-FM-1_900" in out and "done: the FM-1 runs FM-1_900" in out and len(polls) > 1,
       f"drop in step 2: said, polled, back by itself: resumed without a key, only the {n - 2} sectors left ({dev.sector_reads} of {n})")
    ok(dev.upgrades == 3, "... step 1 once, step 2 twice (the loader again), never the running firmware twice")

    dev = FakeFM1(image, fast=True, unplug_after=4 + 1 + 1, comes_back=[back(more=2), back()])
    rc, out, err = cli([p], dev, cancel=never)
    ok(rc == 0 and out.count(I.DROPPED) == 2 and dev.sector_reads == n and dev.bad == 0, "two drops: said each time, back by itself each time, done")

    dev = FakeFM1(image, fast=True, unplug_after=6)    # (never back)
    t = []
    rc, out, err = cli([p], dev, cancel=lambda: t.append(1) or len(t) > 3)
    ok(rc == 1 and len(t) == 4 and "cancelled: the FM-1 stays in update mode: keep it on and run the install again to finish" in out
       and dev.upgrades == 2 and all("do not power off the FM-1" in ln for ln in out.replace("\r", "\n").splitlines()
                                     if ln.startswith(("writing", "checking what")) and "%" in ln),
       "drop, q while it waits: cancelled, nothing more sent (the FM-1 stays in update mode, said)")

    dev = FakeFM1(image, fast=True, unplug_after=6, comes_back=[back(secs=0.5)])
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and I.DROPPED in out and "(q to cancel)" not in out and dev.sector_reads == n,
       "drop with --yes: waits too, resumes by itself, done")
    dev = FakeFM1(image, fast=True, unplug_after=6)
    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 4 and "did not come back within 0.05 minutes" in err
       and "Connection dropped. Check the cable, then run the same command again to resume." in err and time.monotonic() - t0 < 8,
       "drop with --yes, never back: exit 4 after the cap (10 min; 3 s here) with the sentence")
    dev.boot("ota-FM-1_900", "Felucca Update")
    dev.unplug_after = None
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.sector_reads == n, "... the same command again: resumed, done, only the rest sent")

    dev = FakeFM1(image, unplug_after=2, comes_back=[back("FM-1_015", "FM-1")])   # (step 1: the firmware gives up)
    rc, out, err = cli([p], dev, cancel=never)
    ok(rc == 0 and dev.upgrades == 3 and dev.bad == 0, "drop in step 1: back running: step 1 again by itself, then done")

    dev = FakeFM1(image, fast=True, unplug_after=6, comes_back=[back("FM-1_015", "FM-1")])
    rc, out, err = cli([p], dev, cancel=never)
    ok(rc == 6 and dev.upgrades == 2 and "FM-1_015" in err, "drop in step 2, back running the old firmware: stops, nothing sent")
    dev = FakeFM1(image, fast=True, unplug_after=6, comes_back=[back("ota-FM-1_015", "FM-1 Update")])
    rc, out, err = cli([p], dev, cancel=never)
    ok(rc == 6 and dev.upgrades == 2 and "another firmware" in err, "drop, back as another firmware's loader: stops, nothing sent")
    dev = FakeFM1(image, fast=True, unplug_after=6, comes_back=[back("FM-1_900", "Felucca")])
    rc, out, err = cli([p], dev, cancel=never)
    ok(rc == 0 and dev.upgrades == 2 and "done: the FM-1 runs FM-1_900" in out,
       "drop as the loader finished (back running the package): done, nothing sent")

    dev = FakeFM1(image, fast=True, unplug_after=6, comes_back=[back()], keeps_session=True)
    rc, out, err = cli([p], dev, cancel=never)
    ok(rc == 0 and dev.unheard == 2 and "it kept its place over the replug" in out and dev.bad == 0 and dev.sector_reads == n
       and "outside the package" not in err,
       "drop, the loader still in its fast session (the new hello unheard): served on in 4 KiB reads, done (FM-1, 2026-11-01)")

    a = FakeFM1(image, fast=True, unplug_after=6, name="FM1-1A2B", comes_back=[back()])
    b = FakeFM1(image, identity="ota-FM-1_900", name="FM-1")   # (another FM-1, in update mode, a port without a token)
    rc, out, err = cli([p, "--port", "1A2B"], Units(a, b), cancel=never)
    ok(rc == 0 and a.sector_reads == n and b.sent == [], "two FM-1s: resumed on the chosen one, the other never asked")
    a = FakeFM1(image, fast=True, unplug_after=6, name="FM1-1A2B", comes_back=[back(name="FM1-1A2B Update")])
    b = FakeFM1(image, identity="ota-FM-1_900", name="FM1-9C0D Update")
    rc, out, err = cli([p, "--port", "1A2B"], Units(a, b), cancel=never)
    ok(rc == 0 and b.sent == [], "two FM-1s with tokens: only a port with the chosen FM1-XXXX")


def loader_poll():
    """after step 1 the loader's port is looked for at once, every loader_poll s (no blind 3 s wait, 2026-11-01)"""
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("poll.fwsc", raw)
    was = dict(I.DELAY)
    I.DELAY.update(loader=3.0, loader_poll=0.05)
    dev, steps = FakeFM1(image), []
    up = I.Updater(dev)
    t = {}
    up.install(up.find(), image, "FM-1_900", lambda k, a=None: t.setdefault(k, time.monotonic()))
    ok(t["write"] - t["loader"] < 1.0 and dev.bad == 0, f"step 1 -> the loader: looked for at once, found in {t['write'] - t['loader']:.2f} s (was 3 s blind)")
    I.DELAY.update(was)


def progress():
    """step 2 of a fast install: the bar moves while the loader checks what is on the FM-1 (its 0x32 frames, else an
    estimate by time that stays behind it), jumps to the first read's address, then follows the write; never back"""
    I.DELAY.update(check_frames=0.15, check_tick=0.02, sector_check=0.06, idle_write=3)   # (the check before the first read: no idle stop)
    raw = package(size=0x10000)                      # (16 sectors)
    image = I.logical_image(raw)
    n = len(image) // I.SECTOR
    ok(I.parse_check(I.frame(I.CHECK_CMD, [3, 0, 16, 0])) == (3, 16) and I.parse_check(I.frame(I.FAST_CMD, [1, 0, 16])) is None
       and I.parse_check(I.frame(I.CHECK_CMD, [17, 0, 16, 0])) is None, "progress: the 0x32 frame [checked, total] read; others not")

    def run(frames):
        dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", fast=True, check_frames=frames, check_time=0.03)
        dev.written = {k * I.SECTOR for k in range(10)}          # (10 sectors right already: the first read is sector 10)
        steps = []
        up = I.Updater(dev)
        up.write(up.find(), image, lambda k, a=None: steps.append((k, a)))
        bars = [a for k, a in steps if k in ("checking", "write") and a is not None]
        checking = [a for k, a in steps if k == "checking"]
        first = next(i for i, (k, a) in enumerate(steps) if k == "write" and a and a >= 10 * 100 // n)
        return dev, steps, bars, checking, first

    dev, steps, bars, checking, first = run(True)
    ok(dev.check_seen and dev.sector_reads == n - 10 and bars == sorted(bars) and len(checking) > 3 and checking[-1] >= 10 * 100 // n - 7,
       f"progress, a loader with 0x32 frames: the bar moves while it checks ({checking}), never back")
    ok(steps[first][1] >= 10 * 100 // n and bars[-1] == 100 and dev.bad == 0,
       f"... at the first read (sector 10 of {n}) the bar is there at once ({steps[first][1]} %), then the write, 100 %")
    dev, steps, bars, checking, first = run(False)
    ok(not dev.check_seen and len(checking) > 3 and bars == sorted(bars) and max(checking) < 10 * 100 // n,
       f"progress, an older loader (no frames): an estimate by time moves ({checking}), behind the loader, never back")
    ok(steps[first][1] >= 10 * 100 // n and bars[-1] == 100, "... then the first read's address at once, the write, 100 %")
    I.DELAY.update(check_frames=0.5, check_tick=0.1, sector_check=0.012, idle_write=0.4)


def against_js():
    clean = ROOT / "build/felucca.fwsc"
    if not shutil.which("node") or not clean.exists():
        print("logical image vs fm1pkg.js: skipped (needs node and build/felucca.fwsc)")
        return
    js = ("import { logicalImage, productOf } from %r; import { readFileSync } from 'node:fs';"
          "const p = readFileSync(process.argv[1]); process.stderr.write(productOf(p));"
          "process.stdout.write(logicalImage(p));") % str(ROOT / "web/fm1base/fm1pkg.js")
    r = subprocess.run(["node", "--input-type=module", "-e", js, str(clean)], capture_output=True, check=True)
    raw = clean.read_bytes()
    ok(I.logical_image(raw) == r.stdout and I.product_of(raw) == r.stderr.decode(),
       f"logical_image / product_of == web/fm1base/fm1pkg.js ({clean.name}, {I.product_of(raw)})")
    ok(I.LOADER_MARK in raw, f"{clean.name} carries the Felucca loader marker")


def interrupts():
    """Ctrl+C is refused while the FM-1 is written (no_interrupt), and the handlers come back"""
    import os
    import signal
    was = signal.getsignal(signal.SIGINT)
    err = io.StringIO()
    got = None
    try:
        with I.no_interrupt(err):
            os.kill(os.getpid(), signal.SIGINT)
            time.sleep(0.05)
            inside = signal.getsignal(signal.SIGINT)
    except KeyboardInterrupt:
        got = "KeyboardInterrupt"
        inside = None
    ok(got is None and "Ctrl+C is ignored" in err.getvalue(), "no_interrupt: SIGINT during the write is said and ignored")
    ok(signal.getsignal(signal.SIGINT) == was and inside != was, "... the handler is back afterwards")
    box = []
    t = threading.Thread(target=lambda: box.append(_thread_guard()))
    t.start()
    t.join()
    ok(box == [True], "... a thread (no signal handlers there): nothing happens, no error")


def _thread_guard():
    try:
        with I.no_interrupt(io.StringIO()):
            return True
    except Exception:
        return False


def boot_info():
    """--info asks a running Optimist which bootloader it has (INFO tag 0x68), and only an Optimist"""
    image = I.logical_image(package())
    dev = FakeFM1(image, identity="FM-1_7900", name="FM1-1A2B", boot="mboot")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and out.strip().endswith("port: FM1-1A2B  boot=mboot") and dev.infos == 1, "--info: boot=mboot at the end")
    dev = FakeFM1(image, identity="FM-1_700", name="FM1-1A2B")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "boot=" not in out and dev.infos == 1, "--info: an Optimist that says nothing (older): no token")
    dev = FakeFM1(image, identity="FM-1_905", name="Felucca", boot="mboot")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "boot=" not in out and dev.infos == 0, "--info: another firmware is not asked")
    dev = FakeFM1(image, identity="ota-FM-1_700", name="Optimist Update", boot="mboot")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and dev.infos == 0, "--info: an update loader is not asked")


def refused_minus5():
    """a -5 (the running Optimist's updater refuses an M-VAVE package: m-bootloader installed) falls back to download mode"""
    I._route_modules()
    R = I.R
    v15 = R.sample_package(R.P_V15)
    own = R.sample_package(R.P_OPTIMIST)
    ok(R.head_refused(R.OPTIMIST, v15, "noloader") and R.head_refused(R.AMBIG_000, v15, "stopped"), "-5 on V15 from Optimist")
    ok(not R.head_refused(R.OPTIMIST, own, "noloader") and not R.head_refused(R.FELUCCA, v15, "noloader")
       and not R.head_refused(R.OPTIMIST, v15, "lost"), "-5 only for M-VAVE packages from Optimist")
    route = R.refused_route(R.OPTIMIST, v15)
    ok(route.method == "download" and any("M-Flasher" in s for s in route.steps)
       and "OCT-" in route.enter and "REC" in route.enter, "the -5 route is download mode with M-Flasher steps")


wire()
boot_info()
refused_minus5()
interrupts()
installs()
errors()
fast()
dropped()
progress()
loader_poll()
against_js()
shutil.rmtree(TMP, ignore_errors=True)
print(f"INSTALL TESTS FAILED ({failed})" if failed else "install tests passed")
sys.exit(1 if failed else 0)

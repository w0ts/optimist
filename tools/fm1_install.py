#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Install a Felucca package (.fwsc) on an FM-1 over USB-MIDI.

The same update as the web installer (web/fm1ota.js): step 1, the
running firmware reads parts of the package and starts the update loader;
step 2, the loader reads the whole package and writes it. In both steps the
device asks (SysEx read requests on the logical image) and we answer. Then
the FM-1 restarts and the installed identity is checked.

  fm1_install.py PACKAGE.fwsc [--port NAME] [--yes] [--force]
  fm1_install.py --info [--port NAME]

If the FM-1 is still in update mode (an earlier install was cut off), the
install finishes the write. Needs mido with python-rtmidi.

Exit codes: 0 done, 1 cancelled or other error, 2 bad arguments or package,
3 FM-1 not found, 4 connection lost or the device stopped, 5 timeout (no
loader / no restart), 6 wrong model, another identity after the install, or the
FM-1 is in the update mode of another firmware (only the Felucca-family loader,
Optimist's ota-FM-1_7XX or SLOOP / Felucca's ota-FM-1_9XX, is resumed).
"""
import argparse
import contextlib
import hashlib
import queue
import re
import signal
import sys
import time
from pathlib import Path

import flash_layout                  # (tools/: the MEMORY steps' one table)

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
UPGRADE = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
MIDI_STOP = bytes([0xFC])      # real-time STOP: the running firmware stops its transport before an update (harmless if it already does)
ED_INFO = bytes([0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7])   # the editor's INFO (web/EDITOR_PROTOCOL.md): its tag 0x68 says which
                                                        # bootloader the FM-1 has (an Optimist since 2026-10-31)
ASKS_BOOT = re.compile(r"FM-1_(7(00|[2-6]\d)|000|79\d{2,})")   # (Optimist's identities, its release's: only these are asked)
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
MAXDATA = 512
# the fast install (docs/INSTALL-SPEED.md): before step 2's upgrade command the host says it can serve 4 KiB reads and the
# per-sector table (cmd 0x31 [1, 4096 LE]); a loader that can answers the same frame, then reads only the sectors that
# differ, 4 KiB at a time, after the table at TABLE + flash.bin's offset. A loader that says nothing gets today's protocol
FAST_CMD, FAST_VERSION, FAST_CHUNK, SECTOR = 0x31, 1, 4096, 4096
TABLE = 0xD0000000
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
LOADER_MARK = b"FELUCCA-LOADER-1"
# M-VAVE's V15 (web/stock/FM-1_015.fwsc), known by its whole hash: its USR entry fails the per-file CRC as
# package_damage computes it (fm1_rescue.py the same)
STOCK_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
# the app slots Optimist's own updater takes (firmware/src/system/ota.c step 1: every MEMORY step's, tools/flash_layout.py)
OWN_SLOTS = flash_layout.own_slots()
PORT_RE = re.compile(r"fm-?1|felucca|optimist|smk-37|ota|composite|sinco|usb-midi", re.I)   # never probe other gear (fm-?1: "FM-1", and "FM1-XXXX",
                                                                                      # Optimist's name since 2026-10-29; "optimist": older firmware)

# seconds; the tests shorten them
DELAY = {"info": 1.0, "open": 0.3, "stop": 0.1, "start": 2.0, "start_fast": 0.05, "fast": 0.5, "reply": 0.01, "loader": 3.0,
         "reboot": 3.0, "retry": 1.0,
         "poll": 1.0, "hs": 1.0, "idle_check": 8.0, "idle_write": 180.0,
         "wait_loader": 30.0, "wait_reboot": 40.0}

EXIT = {"nomf": 8, "refused": 7, "cancel": 1, "usage": 2, "badpkg": 2, "notfound": 3, "several": 3, "model": 6, "lost": 4, "stopped": 4, "badreq": 4,
        "noloader": 5, "noreturn": 5, "mismatch": 6, "foreign": 6}


class InstallError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------ wire format ---

def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def response(addr, data, fl=0):
    n = len(data)
    body = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl])
    body += addr.to_bytes(4, "little") + bytes([n & 0xFF, (n >> 8) & 0xFF, 0]) + bytes(data)
    body.append(~sum(body[6:]) & 0xFF)
    return b"\xF0" + pack7(body) + b"\xF7"


def frame(cmd, body):
    """a host -> device command as ota.c ota_take reads it: 00 59 cmd len24 body ~sum(body), 7-bit packed"""
    u = bytes([0x00, 0x59, cmd, len(body) & 0xFF, (len(body) >> 8) & 0xFF, 0]) + bytes(body) + bytes([~sum(body) & 0xFF])
    return b"\xF0" + pack7(u) + b"\xF7"


FAST_HELLO = frame(FAST_CMD, [FAST_VERSION, FAST_CHUNK & 0xFF, FAST_CHUNK >> 8])


def parse_fast(pkt):
    """the loader's answer to FAST_HELLO -> its read size, or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 10 or u[:6] != bytes([0, 0x59, FAST_CMD, 3, 0, 0]) or ~sum(u[6:9]) & 0xFF != u[9] or u[6] != FAST_VERSION:
        return None
    return u[7] | u[8] << 8


def crc32(data):
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def sector_table(image, off, n):
    """the fast install's table: per 4 KiB sector of image[off:], CRC16 (XMODEM, the UFW's) and CRC-32 (zlib), LE"""
    out = bytearray()
    for k in range(n):
        s = image[off + k * SECTOR:off + (k + 1) * SECTOR]
        out += _crc16(s).to_bytes(2, "little") + crc32(s).to_bytes(4, "little")
    return bytes(out)


def answer(image, addr, n, maxdata):
    """the bytes a read request asks for (a table request included), or None when it is outside the package"""
    if TABLE <= addr < FINISH_CHECK:
        off = addr - TABLE
        if maxdata < FAST_CHUNK or n % 6 or n > maxdata or off + n // 6 * SECTOR > len(image):
            return None
        return sector_table(image, off, n // 6)
    if n > maxdata or addr + n > len(image):
        return None
    return image[addr:addr + n]


def parse_request(pkt):
    """-> (flags, addr, len) or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 15 or u[:3] != b"\x00\x59\x30" or ~sum(u[6:14]) & 0xFF != u[14]:
        return None
    return u[6], int.from_bytes(u[7:11], "little"), int.from_bytes(u[11:14], "little")


class Identity:
    def __init__(self, text, model, version):
        self.text, self.model, self.version = text, model, version

    @property
    def loader(self):
        return self.model.lower().startswith("ota-")


def parse_identity(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) != 34 or d[:3] != b"\x00\x59\x11":
        return None
    txt = d[6:33].rstrip(b"\0").decode("latin-1")
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


# --------------------------------------------------------------- package ---

def product_of(raw):
    """the package identity ("FM-1_906"): one marker byte after each of the first 20 blocks"""
    if len(raw) < BLOCKS * BLK:
        raise InstallError("badpkg", "not an FM-1 package (too short)")
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def logical_image(raw):
    """the image the device reads during an update: the package without the 20 marker bytes"""
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK:])


def model_of(text):
    return text.split("_")[0].removeprefix("ota-")


# ------------------------------------------------------------------ MIDI ---

class MidoLink:
    """one MIDI in/out pair with a SysEx queue"""

    def __init__(self, backend, in_name, out_name):
        import mido
        self.backend, self.name, self.mido = backend, in_name, mido
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.out = mido.open_output(out_name)
        except Exception:
            self.inp.close()
            raise
        time.sleep(DELAY["open"])   # CoreMIDI may drop a message sent just after opening

    def _rx(self, msg):
        if msg.type == "sysex":
            self.q.put(bytes([0xF0, *msg.data, 0xF7]))

    @property
    def lost(self):
        return self.name not in self.backend.input_names()

    def send(self, pkt):
        try:
            if pkt == MIDI_STOP:
                self.out.send(self.mido.Message("stop"))
            else:
                self.out.send(self.mido.Message("sysex", data=pkt[1:-1]))
            return True
        except Exception:
            return False

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        for p in (self.inp, self.out):
            try:
                p.close()
            except Exception:
                pass


class MidoBackend:
    def __init__(self):
        import mido
        self.mido = mido

    def input_names(self):
        return list(dict.fromkeys(self.mido.get_input_names()))

    def output_names(self):
        return list(dict.fromkeys(self.mido.get_output_names()))

    def open(self, in_name, out_name):
        return MidoLink(self, in_name, out_name)


def pair_output(name, outs):
    if name in outs:
        return name
    base = re.sub(r"\s+\d+$", "", name)     # Windows numbers each port
    same = [o for o in outs if re.sub(r"\s+\d+$", "", o) == base]
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


# --------------------------------------------------------------- updater ---

class Device:
    def __init__(self, link, ident, name):
        self.link, self.id, self.name = link, ident, name


def handshake(link, tries=3):
    link.drain()
    for _ in range(tries):
        if not link.send(HS_QUERY):
            return None
        end = time.monotonic() + DELAY["hs"]
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                break
            ident = parse_identity(p)
            if ident:
                return ident
    return None


def ask_boot(dev):
    """the running Optimist's word on its bootloader (INFO tag 0x68): "mboot" | "stock" | "unknown", None when it is not
    asked (a loader, another firmware) or says nothing (an older Optimist, a build without the editor). Writes nothing"""
    if dev is None or dev.id.loader or not ASKS_BOOT.fullmatch(dev.id.text):
        return None
    _route_modules()
    link = dev.link
    link.drain()
    if not link.send(ED_INFO):
        return None
    end = time.monotonic() + DELAY["info"]
    while (left := end - time.monotonic()) > 0:
        p = link.read(left)
        if p is None:
            return None
        if p[:5] == ED_INFO[:5]:
            return R.boot_of_info(p)
    return None


class Updater:
    def __init__(self, backend, port=None):
        self.backend = backend
        self.port = port

    def candidates(self, only_port):
        names = self.backend.input_names()
        if self.port:
            mine = [n for n in names if self.port.lower() in n.lower()]
            if only_port:
                return mine
            return mine + [n for n in names if n not in mine and PORT_RE.search(n)]
        return [n for n in names if PORT_RE.search(n)]

    def find(self, want=None, only_port=True):
        """the first matching port that answers the handshake (and want(identity))"""
        outs = self.backend.output_names()
        for name in self.candidates(only_port):
            out = pair_output(name, outs)
            if not out:
                continue
            try:
                link = self.backend.open(name, out)
            except Exception:
                continue
            ident = handshake(link, 2)
            if ident and (want is None or want(ident)):
                return Device(link, ident, name)
            link.close()
        return None

    def wait_for(self, want, secs):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want, only_port=False)
            if dev:
                return dev
            time.sleep(DELAY["retry"])
        return None

    def serve(self, link, image, finish, idle, progress, maxdata=MAXDATA, pace=None):
        """answer read requests until the device asks for `finish`; -> (served, finished, lost). maxdata/pace: today's
        protocol 512 B and DELAY["reply"] before each answer; the fast install 4 KiB, its table, no pause"""
        pace = DELAY["reply"] if pace is None else pace
        served, last = 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None:
                if link.lost:
                    return served, False, True
                if time.monotonic() - last > idle:
                    return served, False, False
                continue
            r = parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last = time.monotonic()
            if addr in (FINISH_CHECK, FINISH_WRITE):
                link.send(response(addr, b"success\0"))
                if addr == finish:
                    return served, True, False
                continue
            data = answer(image, addr, n, maxdata)
            if data is None:
                raise InstallError("badreq", f"the device asked for {addr:#x}+{n}, outside the package")
            if pace:
                time.sleep(pace)
            if not link.send(response(addr, data, fl)):
                return served, False, True
            served += 1
            if addr < TABLE:
                progress(served, addr + n)

    def check(self, dev, image, step):
        """step 1: the running firmware checks the package and starts the loader"""
        step("start", dev.id.text)
        dev.link.send(MIDI_STOP)        # the transport stops before the update starts (the firmware does it too)
        time.sleep(DELAY["stop"])
        dev.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(dev.link, image, FINISH_CHECK, DELAY["idle_check"],
                                            lambda k, end: step("check", k))
        dev.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the FM-1 {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests; nothing was written")
        step("loader")
        time.sleep(DELAY["loader"])
        ota = self.wait_for(lambda i: i.loader, DELAY["wait_loader"])
        if not ota:
            raise InstallError("noloader", "the update loader did not appear. Replug the USB cable and "
                                           "run the install again: the FM-1 stays in update mode until it is done")
        return ota

    def fast_hello(self, link):
        """the fast install's word to the loader -> True when it answers (docs/INSTALL-SPEED.md); an older loader
        ignores the frame and gets today's protocol"""
        link.drain()
        if not link.send(FAST_HELLO):
            return False
        end = time.monotonic() + DELAY["fast"]
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                return False
            if parse_fast(p) == FAST_CHUNK:
                return True
        return False

    def write(self, ota, image, step):
        """step 2: the loader reads and writes the whole package (only what differs, when it can: fast_hello), then
        restarts"""
        step("write", 0)
        fast = self.fast_hello(ota.link)
        step("fast" if fast else "classic")
        ota.link.send(UPGRADE)
        time.sleep(DELAY["start_fast"] if fast else DELAY["start"])
        served, finished, lost = self.serve(ota.link, image, FINISH_WRITE, DELAY["idle_write"],
                                            lambda k, end: step("write", min(99, end * 100 // len(image))),
                                            FAST_CHUNK if fast else MAXDATA, 0 if fast else None)
        ota.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the loader {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests. Replug the USB cable and run the install again to finish")
        step("write", 100)

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        back = self.wait_for(lambda i: not i.loader, DELAY["wait_reboot"])
        if not back:
            raise InstallError("noreturn", "the FM-1 did not come back: power-cycle it")
        back.link.close()
        if back.id.text != product:
            raise InstallError("mismatch", f"written, but the FM-1 reports {back.id.text} (expected {product})")
        step("done", back.id.text)
        return back.id.text

    def install(self, dev, image, product, step=lambda *a: None):
        """running firmware -> loader -> new firmware; dev from find(). A device already in
        update mode only needs the write."""
        if model_of(dev.id.text) != model_of(product):
            dev.link.close()
            raise InstallError("model", f"the device is {dev.id.text}, the package is for {product}")
        if dev.id.loader and not re.fullmatch(r"ota-FM-1_[79]\d\d", dev.id.text, re.I):
            dev.link.close()           # another firmware's loader: its image layout may differ
            raise InstallError("foreign", f"the FM-1 is in the update mode of another firmware ({dev.id.text}): "
                                          "finish that update with its own updater (M-UPGRADE), then install again")
        ota = dev if dev.id.loader else self.check(dev, image, step)
        self.write(ota, image, step)
        return self.verify(product, step)


# ------------------------------------------------------------------- CLI ---

def load_package(path, force):
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        raise InstallError("badpkg", f"cannot read {path}: {e.strerror}")
    product = product_of(raw)
    if not re.fullmatch(r"[^_]+_\d+", product):
        raise InstallError("badpkg", f"{path}: not an FM-1 package (identity {product!r})")
    stock = hashlib.sha256(raw).hexdigest() == STOCK_SHA256
    if LOADER_MARK not in raw and not force and not stock:
        raise InstallError("badpkg", f"{path}: no Felucca update loader in this package; "
                                     "only Felucca's own packages are installed (--force overrides)")
    image = logical_image(raw)
    bad = None if stock else package_damage(image)
    if bad:
        raise InstallError("badpkg", f"{path}: damaged package ({bad}); nothing was written. "
                                     "Download it again.")
    return product, image


def _crc16(data, crc=0):
    """CRC-16/XMODEM, as the package and the device use"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _dec(b):
    """JieLi ENC stream cipher (its own inverse)"""
    out, key = bytearray(b), 0xFFFF
    for i in range(len(out)):
        out[i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def package_damage(image):
    """what is wrong with the update image (header, file list, each file's CRC), or None; the device
    only checks the loader before it rewrites the firmware, so a damaged file is caught here"""
    if len(image) < 0x40:
        return "too short"
    hdr = _dec(image[0:0x40])
    if _crc16(hdr[2:0x40]) != int.from_bytes(hdr[0:2], "little"):
        return "header CRC"
    count = int.from_bytes(hdr[8:10], "little")
    if not 1 <= count <= 8 or len(image) < 0x40 + 0x50 * count:
        return "file list"
    lst = image[0x40:0x40 + 0x50 * count]
    if _crc16(lst) != int.from_bytes(hdr[2:4], "little"):
        return "file list CRC"
    for i in range(count):
        e = _dec(lst[i * 0x50:(i + 1) * 0x50])
        dcrc = int.from_bytes(e[4:6], "little")
        off = int.from_bytes(e[8:12], "little")
        size = int.from_bytes(e[12:16], "little")
        name = e[0x40:0x50].split(b"\0")[0].decode("ascii", "replace")
        if off + size > len(image):
            return f"{name}: cut short"
        if _crc16(image[off:off + size]) != dcrc:
            return f"{name}: CRC"
    return None


def flash_size(image):
    """the size of the package's flash.bin (its file list), None without one"""
    hdr = _dec(image[0:0x40])
    for i in range(int.from_bytes(hdr[8:10], "little")):
        e = _dec(image[0x40 + 0x50 * i:0x40 + 0x50 * (i + 1)])
        if e[0x40:0x50].split(b"\0")[0] == b"flash.bin":
            return int.from_bytes(e[12:16], "little")
    return None


def platform_warning(running, product, image):
    """why the running firmware may refuse this package, said before anything is sent, or None. Optimist's own
    updater (a running FM-1_7NN) takes its own layouts only and refuses fm1-base's packages (FM-1_79..., the
    release identities): 2026-10-29, an fm1-base SD card mode package (flash.bin 0xCE000) over an Optimist build
    stopped at the device's loader check (-16), nothing written"""
    if running.loader or not re.fullmatch(r"FM-1_7\d\d", running.text):
        return None
    size = flash_size(image)
    if re.fullmatch(r"FM-1_79\d\d+", product):
        why = f"{product} is an fm1-base package"
    elif size is not None and size not in OWN_SLOTS:
        why = f"its flash.bin is {size:#x} B, Optimist's layouts {', '.join(f'{s:#x}' for s in OWN_SLOTS)}"
    else:
        return None
    return (f"the FM-1 runs Optimist's own firmware ({running.text}), which may refuse this package ({why}). "
            "The way over: M-VAVE's V15 first (fm1base/tools/fm1_install.py web/stock/FM-1_015.fwsc), then this package")


FM1_TOKEN = re.compile(r"FM1-[0-9A-F]{4}", re.I)
FM1_NAME = re.compile(r"FM1-[0-9A-F]{4}|fm-1|optimist|felucca", re.I)   # (an FM-1 for sure: not "usb-midi" / "composite")


def fm1_devices(names):
    """the FM-1s among MIDI port names, one key each: its FM1-XXXX (all its ports share it), else the port's name;
    Bluetooth's ports left out (the same FM-1 seen twice)"""
    keys = []
    for n in names:
        if "bluetooth" in n.lower() or not FM1_NAME.search(n):
            continue
        m = FM1_TOKEN.search(n)
        k = m.group(0).upper() if m else n
        if k not in keys:
            keys.append(k)
    return keys


def several(up):
    """more than one FM-1 on the cable and no --port: an install must not pick one by itself (2 FM-1s, 2026-11-01)"""
    if up.port or up.backend is None:                      # (no MIDI at all: a dry run, the download route)
        return None
    devs = fm1_devices(up.backend.input_names())
    if len(devs) < 2:
        return None
    return InstallError("several", f"{len(devs)} FM-1s are connected ({', '.join(devs)}): name the one to use with "
                                   f"--port (a part of its port's name)")


def not_found(up):
    names = up.backend.input_names()
    where = f"no MIDI port matching {up.port!r} answered" if up.port else "FM-1 not found"
    return InstallError("notfound", f"{where} (USB data cable? another app using it?)"
                        f"\n  MIDI inputs: {', '.join(names) if names else '(none)'}")


class Progress:
    def __init__(self, out):
        self.out, self.line = out, False

    def __call__(self, k, a=None):
        if k == "check":
            self.put(f"checking the package ({a} reads)")
        elif k in ("fast", "classic"):
            self.end()
            print("the loader sends only the sectors that differ (fast install)" if k == "fast" else
                  "an older loader: the whole package, twice (today's protocol)", file=self.out, flush=True)
        elif k == "write":
            self.put(f"writing {a:3d}%")
        else:
            self.end()
            msg = {"start": f"starting the update on {a}", "loader": "switching to update mode",
                   "reboot": "restarting", "done": f"done: the FM-1 runs {a}"}[k]
            print(msg, file=self.out, flush=True)

    def put(self, s):
        print(f"\r{s}\033[K", end="", file=self.out, flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(file=self.out, flush=True)
            self.line = False


@contextlib.contextmanager
def no_interrupt(out=None):
    """Ctrl+C is refused while the FM-1 is written: a cut-off write leaves it in update mode (the user, 2026-10-21: "refuse to
    quit with ctrl+c ... so that one cannot by mistake break flash"). SIGINT, and SIGHUP (the terminal closed), are said
    and ignored; the handlers come back afterwards. Only the main thread can set them: elsewhere nothing changes"""
    old = {}
    def refuse(signum, frame):
        print("\nflashing: Ctrl+C is ignored until the write is done (do not unplug the FM-1)", file=out or sys.stderr)
    try:
        for sig in (signal.SIGINT, signal.SIGHUP):
            old[sig] = signal.signal(sig, refuse)
    except (ValueError, OSError, AttributeError):
        pass
    try:
        yield
    finally:
        for sig, h in old.items():
            try:
                signal.signal(sig, h)
            except (ValueError, OSError):
                pass


def run(a, backend, out, ask, ask_text=None):
    _route_modules()
    up = Updater(backend, a.port)
    err = several(up)
    if err:
        raise err
    if a.info:
        dev = up.find()
        if not dev:
            raise not_found(up)
        boot = ask_boot(dev)
        dev.link.close()
        mode = "update loader (update not finished)" if dev.id.loader else "running"
        print(f"{dev.id.text}  [{mode}]  port: {dev.name}" + (f"  boot={boot}" if boot else ""), file=out)
        return 0
    product, image = load_package(a.package, a.force)       # (before any MIDI: a bad package stops here)
    print(f"package: {product}  ({a.package})", file=out)
    dev = up.find() if not (a.dry_run and a.running) else None
    if not dev and a.route != "download" and not (a.dry_run and a.running):
        raise not_found(up)
    text = a.running if a.running else (dev.id.text if dev else None)
    boot = a.boot if a.boot else ask_boot(dev)
    route = choose_route(a, dev, text, out, image, boot)
    if a.dry_run:
        if dev:
            dev.link.close()
        flash = AF.load(a.package)[2] if route.method == "download" else None
        wipe = settings_decision(a, text, route_pkg(a, image), route, out, None)
        AF.dry_run(AF.Env(out=out), route, route_pkg(a, image), flash, a.backup_dir, a.flash_image, wipe)
        return 0
    if route.method == "download":
        return download(a, up, dev, route, out, ask, ask_text or ask_word)
    try:
        return loader_install(a, up, dev, out, ask, product, image)
    except InstallError as e:
        info = route_pkg(a, image)
        if a.route == "loader" or not R.head_refused(R.running_kind(text, dev.name), info, e.code):
            raise
        print(f"the FM-1 did not start the update ({e}). Optimist's updater refuses an M-VAVE package when the "
              "bootloader differs (error -5, as with m-bootloader installed); nothing was written. "
              "Download mode (M-Flasher) instead:", file=out)
        route = R.refused_route(R.running_kind(text, dev.name), info)
        AF.describe(AF.Env(out=out), route, info, text, dev.name, "")
        return download(a, up, None, route, out, ask, ask_text or ask_word)


AF = R = None


def _route_modules():
    """tools/fm1_anyflash.py and fm1_route.py, imported when first needed (fm1_rescue.py, which they use, imports this)"""
    global AF, R
    if AF is None:
        import fm1_anyflash
        import fm1_route
        AF, R = fm1_anyflash, fm1_route


def route_pkg(a, image, raw=None):
    """the package as the route chooser sees it (fm1_route.package_kind: its identity, SHA-256, flash.bin's size)"""
    raw = raw if raw is not None else open(a.package, "rb").read()
    return R.package_kind(raw, flash_size(image if image is not None else logical_image(raw)))


def settings_decision(a, running_text, info, route, out, ask_text):
    """keep or erase the new firmware's settings and pairings (fm1_route.settings_policy, by the running -> package
    pair): printed before anything is written; asked when the pair says so (unless --yes, --keep-settings or
    --wipe-settings answer it) -> the fm1_layouts.WIPE key, "" to keep"""
    if route.method != "download":
        print("settings: kept (the running firmware's update loader writes the firmware area only)", file=out)
        return ""
    policy = R.settings_policy(running_text, info)
    flag = "wipe" if a.wipe_settings else "keep" if a.keep_settings else None
    asked = None
    if flag is None and policy.ask and not a.yes and ask_text is not None:
        sugg = "Y/n" if policy.default == "wipe" else "y/N"
        ans = ask_text(f"{policy.reason}.\nErase the new firmware's settings and pairings? [{sugg}] ")
        asked = (policy.default == "wipe") if ans == "" else ans in ("y", "yes")
    choice, line = R.settings_choice(policy, flag, asked)
    print(line, file=out)
    return policy.target if choice == "wipe" else ""


def choose_route(a, dev, text, out, image, boot=None):
    """the way for this package (tools/fm1_route.py): printed with the build's BOOTLOADER as this install takes it, and
    the bootloader the firmware says it has (boot: ask_boot's word; None: not known)"""
    info = route_pkg(a, image)
    choice, line = AF.build_setting(a.package, a.install_mboot)
    present = R.mboot_present_of(boot)
    if boot:
        line += {"mboot": "; the FM-1 has m-bootloader (the firmware says so)", "stock": "; the FM-1 has M-VAVE's "
                 "bootloader (the firmware says so)", "unknown": "; the firmware does not know its bootloader"}[boot]
    running = R.running_kind(text, dev.name if dev else "")
    route = R.choose(running, info, choice, present, text or "")
    if a.route == "download" and route.method == "loader":
        soft, keys = R.entry_for(running, present)
        route = R.Route("download", "--route download", f"by hand: {keys}" if not soft else
                        "the installer sends the soft key over USB-MIDI", soft, choice == R.MBOOT, route.steps)
    elif a.route == "loader" and route.method == "download" and info.kind != R.P_MBOOT and choice != R.MBOOT:
        route = R.Route("loader", "--route loader (the running firmware may refuse it)")
    AF.describe(AF.Env(out=out), route, info, text, dev.name if dev else "", line)
    if route.method == "refused":
        if dev:
            dev.link.close()
        raise InstallError("foreign", route.why)
    return route


def download(a, up, dev, route, out, ask, ask_text):
    """the chip's download mode through M-Flasher (tools/fm1_anyflash.py): the soft key over this link"""
    def close():
        if dev:
            dev.link.close()

    def soft_key():
        ok = bool(dev) and dev.link.send(R.SOFT_KEY)
        close()
        return ok

    def back(product):
        d = up.wait_for(lambda i: not i.loader, DELAY["wait_reboot"])
        if d:
            d.link.close()
        return d.id.text if d else None
    if not a.yes and not ask("Go on through download mode (each step shows what it writes and asks)? [y/N] "):
        close()
        print("cancelled", file=out)
        return 1
    env = AF.Env(out=out, ask=lambda q: "yes" if a.yes else ask_text(q), soft_key=soft_key, wait_back=back,
                 may_fetch=None if a.yes else (lambda q: ask(q + " [y/N] ")))     # (--yes downloads nothing: setup does)
    try:
        AF.need_mflasher(env)
        _, info, flash = AF.load(a.package)
        wipe = settings_decision(a, dev.id.text if dev else None, route_pkg(a, None, raw=open(a.package, "rb").read()),
                                 route, out, ask_text)
        AF.download_install(env, route, a.package, flash, a.backup_dir, a.yes, info.product, wipe)
    finally:
        close()
    return 0


def loader_install(a, up, dev, out, ask, product, image):
    print(f"device:  {dev.id.text}  ({dev.name})" + ("  in update mode: the write will be finished" if dev.id.loader else ""),
          file=out)
    warn = platform_warning(dev.id, product, image)
    if warn:
        print(f"warning: {warn}", file=out)
        if a.yes and not a.force:
            dev.link.close()
            raise InstallError("foreign", f"{warn}; nothing was sent (--force installs it anyway)")
    if not a.yes and not ask(("Install anyway? " if warn else "Install? ") +
                             "Do not unplug the FM-1 while writing. [y/N] "):
        dev.link.close()
        print("cancelled", file=out)
        return 1
    prog = Progress(out)
    try:
        with no_interrupt():
            up.install(dev, image, product, prog)
    finally:
        prog.end()
    return 0


def ask_word(prompt):
    try:
        return input(prompt).strip().lower()
    except EOFError:
        return ""


def ask_tty(prompt):
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False


def main(argv=None, backend=None, out=sys.stdout, ask=ask_tty, ask_text=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", help="Felucca package (.fwsc)")
    ap.add_argument("--info", action="store_true", help="print the identity of the connected FM-1")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    ap.add_argument("--force", action="store_true", help="install a package without the Felucca loader marker")
    ap.add_argument("--route", choices=("auto", "loader", "download"), default="auto",
                    help="auto (docs/INSTALL-PATHS.md), the running firmware's loader, or download mode (M-Flasher)")
    ap.add_argument("--install-mboot", action="store_true",
                    help="install m-bootloader (download mode, a full backup first); never done otherwise")
    ap.add_argument("--keep-bootloader", action="store_true", help=argparse.SUPPRESS)   # (the default now; older callers)
    ap.add_argument("--dry-run", action="store_true", help="send nothing: print the route and what would be written where")
    st = ap.add_mutually_exclusive_group()
    st.add_argument("--keep-settings", action="store_true", help="download mode: keep the settings and pairings")
    st.add_argument("--wipe-settings", action="store_true", help="download mode: erase the new firmware's settings and "
                    "pairings (never the projects; never M-Flasher's --wipe-settings)")
    ap.add_argument("--running", metavar="IDENTITY", help="--dry-run: the firmware the FM-1 runs (FM-1_700 ...), not asked")
    ap.add_argument("--boot", choices=("mboot", "stock", "unknown"), help="the bootloader the FM-1 has, not asked "
                    "(a running Optimist says so itself: INFO tag 0x68)")
    ap.add_argument("--flash-image", metavar="BACKUP.bin", help="--dry-run: the exact sectors against this full flash image")
    ap.add_argument("--backup-dir", type=Path, default=None, help="download mode: where the backups go "
                    "(default: M-Flasher's, Documents/M-Flasher/backups)")
    a = ap.parse_args(argv)
    _route_modules()
    a.backup_dir = a.backup_dir or AF.default_backup_dir()
    if bool(a.info) == bool(a.package):
        ap.error("give a PACKAGE.fwsc or --info")
    try:
        if backend is None and not (a.dry_run and a.running):
            try:
                backend = MidoBackend()
            except ImportError:
                raise InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        return run(a, backend, out, ask, ask_text)
    except (InstallError, AF.AnyError, AF.MF.MFlasherError, AF.L.PlanError, AF.fm1_rescue.Fail) as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT.get(getattr(e, "code", ""), AF.EXIT.get(getattr(e, "code", ""), 1))
    except KeyboardInterrupt:
        print("\ninterrupted: if the FM-1 is in update mode, run the install again to finish", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())

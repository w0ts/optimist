#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Charles Vestal (fm1-x0x, https://github.com/charlesvestal/fm1-x0x, tools/fm1_rescue.py
# a61701e, 05494ad, 70440e5); the UBOOT protocol and write policy after FM-1-transporter by kurogedelic (MIT,
# https://github.com/kurogedelic/FM-1-transporter, docs/PROTOCOL.md). Ported into SLOOP-plus / Optimist
# unchanged but for this header, the paragraph on when an FM-1 lands in UBOOT, and one comment.
# Writing any known-good .fwsc (not only V15), the package validation and the key / application-header
# checks come from the ideas and code of tools/fm1_recover.py in Melodee 1.3.1 (GPL-3.0-only, Copyright
# (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments; https://github.com/keremimo/melodee,
# tag v1.3.1), reimplemented here on tools/fm1_install.py's and tools/fm1pkg_make.py's package code.
"""fm1_rescue: put stock firmware back on an FM-1 through the chip's own update mode (the
mask-ROM "UBOOT"), from a Mac, with no extra hardware.

For an FM-1 whose firmware crashes at start-up, when the firmware's own way back (OPTIMIST.md
"Recovery": the boot guard's USB rescue, the update loader) is out of reach. The chip's UBOOT shows
up on USB as "WL80UBOOT1.00" (4C4A:8057). macOS then takes it for a
disk, upsets it, and it reboots into the crash a few seconds later; this script waits for it,
takes it away from macOS (hence sudo) and keeps it.

  python3 -m venv build/rescue/env && build/rescue/env/bin/pip install pyusb libusb-package
  sudo build/rescue/env/bin/python tools/fm1_rescue.py FM-1.fwsc            # check and back up, write nothing
  sudo build/rescue/env/bin/python tools/fm1_rescue.py FM-1.fwsc --write    # then write stock
  python3 tools/fm1_rescue.py ANY.fwsc --check                              # validate a package, no USB, no sudo

FM-1.fwsc is M-VAVE's FM-1 V15 (m-vave.com/download, PC Firmware), the default. ANY other known-good
.fwsc (an earlier Optimist or Melodee build that booted) can be written too, but only after it passes the
same checks (identity, header and file-list CRCs, every file's CRC, a flash entry of exactly 0x93000 B,
a chip key and an application header that agree), with the package named before anything is written and
an explicit flag that repeats its hash:  --write --confirm-package <first 8+ hex digits of its sha256>
(--check prints it; with --ask you type it instead). The chip key found in the package must equal the
FM-1's own, or nothing is written. The protocol and the write policy are FM-1-transporter's (github.com/kurogedelic/
FM-1-transporter, docs/PROTOCOL.md), which were verified on hardware:

  1. JieLi's flash loader (wl82loader.bin, from github.com/kagaimiq/jl-uboot-tool, downloaded
     and checked against its hash) is loaded into RAM and started;
  2. the chip key must be 980F and the flash 856014 (1 MiB), or nothing is done;
  3. the whole flash is read and saved as a backup before anything else;
  4. only 4 KiB sectors in [0x4000, 0x93000) that differ from the package are written (the bootloader below
     0x4000 and the device data above 0x93000 are never touched; a difference below 0x4000 stops
     it), each erased, written and read back;
  5. a final full read must equal the expected image; then the FM-1 restarts into V15.
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import time
import urllib.request
from collections import namedtuple

import fm1_install
import fm1pkg_make

VID, PID = 0x4C4A, 0x8057
STOCK_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
# M-VAVE's V15 as this tree keeps it (Apache-2.0: M-VAVE's FM1 Open kit, sdk/firmware/FM-1_015.fwsc, sha256 STOCK_SHA256)
STOCK_IN_TREE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "web", "stock", "FM-1_015.fwsc")
LOADER_URL = ("https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/"
              "c32b28e7a04f873f526ba929cb1bae9f05442fe9/data/loaderblobs/usb/wl82loader.bin")
LOADER_SHA256 = "d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376"
LOADER_ADDR, LOADER_ARG = 0x1C02000, 0x0001

FLASH_SIZE = 0x100000
APP_END = 0x93000          # the .fwsc flash entry is raw flash [0, APP_END)
WRITE_MIN = 0x4000         # flash header, SPL, isd_config: never written
SECTOR = 0x1000
IO = 512

# ROM UBOOT1.00
CMD_WRITE_MEMORY, CMD_READ_MEMORY, CMD_JUMP = 0xFB06, 0xFD07, 0xFB08
# loader
CMD_ERASE_SECTOR, CMD_WRITE_FLASH, CMD_READ_FLASH = 0xFB01, 0xFB04, 0xFD05
CMD_READ_KEY, CMD_GET_ONLINE_DEVICE, CMD_RUN_APP = 0xFC09, 0xFC0A, 0xFC0C


class Fail(Exception):
    pass


# ------------------------------------------------------------------ maths ---
def crc16(data, crc=0):
    """CRC-16/XMODEM (poly 0x1021, no reflection)"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


MAGIC = bytes([0xC3, 0xCF, 0xC0, 0xE8, 0xCE, 0xD2, 0xB0, 0xAE,
               0xC4, 0xE3, 0xA3, 0xAC, 0xD3, 0xF1, 0xC1, 0xD6])


def crc_cipher(buf):
    """JieLi's CrcDecode cipher, key 0xFFFFFFFF (in place)"""
    crc = crc16(b"\xFF\xFF", 0xFFFF)
    for i in range(len(buf)):
        crc = crc16(MAGIC[i % 16:i % 16 + 1], crc)
        buf[i] ^= crc & 0xFF


def enc(buf, key=0xFFFF):
    """JieLi ENC stream cipher (in place)"""
    for i in range(len(buf)):
        buf[i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF


# ---------------------------------------------------------------- package ---
def package_flash_image(raw):
    """The .fwsc's type-0 entry: raw flash [0, APP_END). Layout as tools/fm1pkg_make.py writes it:
    the logical image is the .fwsc without the marker byte after each of its first 20 0x2F-byte
    blocks; it starts with a 0x40-byte header and a list of 0x50-byte entries, each ENC'd."""
    logical = bytearray()
    for i in range(20):
        logical += raw[i * 0x30:i * 0x30 + 0x2F]
    logical += raw[20 * 0x30:]
    hdr = bytearray(logical[0:0x40])
    enc(hdr)
    n = struct.unpack_from("<H", hdr, 8)[0]
    if hdr[16:22] != b"AC791N" or not 0 < n < 16:
        raise Fail("not an FM-1 package (header)")
    for i in range(n):
        e = bytearray(logical[0x40 + i * 0x50:0x40 + (i + 1) * 0x50])
        enc(e)
        typ, _, dcrc, _, off, size = struct.unpack_from("<HHHHII", e, 0)
        if typ == 0:
            data = bytes(logical[off:off + size])
            if len(data) != size or crc16(data) != dcrc:
                raise Fail("package flash entry: bad CRC")
            return data
    raise Fail("package has no flash entry")


Package = namedtuple("Package", "path product sha256 flash key stock")


def package_chip_key(flash):
    """the chip key the package's flash image was built for: the isd_config.ini entry of its head
    (each 32-byte entry ENC'd), cross-checked against the application header at 0x4000 (SFC'd with the
    key). From Melodee's fm1_recover.py Package, on fm1pkg_make's ciphers."""
    mk = fm1pkg_make
    key = None
    for off in range(32, 0x400, 32):
        e = bytearray(flash[off:off + 32])
        mk.enc(e, 0, 32)
        if mk.crc16(e[2:]) != struct.unpack_from("<H", e)[0]:
            raise Fail("package: bad flash-head entry checksum")
        if e[16:].split(b"\0", 1)[0] == b"isd_config.ini":
            addr, size = struct.unpack_from("<II", e, 4)
            if size < 34 or addr < 0x400 or addr + size > WRITE_MIN:
                raise Fail("package: invalid chip-key configuration extent")
            blob = flash[addr:addr + size]
            if mk.crc16(blob) != struct.unpack_from("<H", e, 2)[0] or mk.crc16(blob[:32]) != struct.unpack_from("<H", blob, 32)[0]:
                raise Fail("package: bad chip-key configuration checksum")
            key = mk.chipkey_decode(blob[:32])
            break
        if struct.unpack_from("<H", e, 14)[0]:
            break
    if key is None:
        raise Fail("package has no chip-key configuration")
    app = bytearray(flash[WRITE_MIN:WRITE_MIN + 32])
    mk.sfc(app, 0, 32, 0, key)
    if mk.crc16(app[2:]) != struct.unpack_from("<H", app)[0]:
        raise Fail("package: the application header does not match the package's chip key")
    return key


def load_package(path):
    """Read and validate any .fwsc before it can be written: FM-1 identity, header / file-list / per-file
    CRCs (tools/fm1_install.package_damage), one flash entry of exactly APP_END bytes (its own CRC
    checked), and a chip key that agrees with its application header. Raises Fail."""
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        raise Fail(f"cannot read {path}: {e.strerror}")
    try:
        product = fm1_install.product_of(raw)
        if not re.fullmatch(r"FM-1_\d+", product):
            raise Fail(f"{path}: not an FM-1 package (identity {product!r})")
        bad = fm1_install.package_damage(fm1_install.logical_image(raw))
    except fm1_install.InstallError as e:
        raise Fail(f"{path}: not an FM-1 package ({e})")
    if bad:
        raise Fail(f"{path}: damaged package ({bad}); nothing is written. Get the file again")
    flash = package_flash_image(raw)
    if len(flash) != APP_END:
        raise Fail(f"{path}: the flash entry is {len(flash):#x} B, expected {APP_END:#x}")
    digest = hashlib.sha256(raw).hexdigest()
    return Package(os.path.abspath(path), product, digest, flash, package_chip_key(flash), digest == STOCK_SHA256)


def describe(pkg):
    kind = "M-VAVE stock V15" if pkg.stock else "CUSTOM package (not V15)"
    return (f"  package: {pkg.path}\n  identity {pkg.product}, chip key {pkg.key:04X}, {kind}\n"
            f"  sha256 {pkg.sha256}")


def confirmed(pkg, prefix):
    """--confirm-package: at least 8 hex digits, a prefix of the package's sha256"""
    p = (prefix or "").strip().lower()
    return len(p) >= 8 and pkg.sha256.startswith(p)


# -------------------------------------------------------------- transport ---
class Bot:
    """USB mass storage bulk-only transport: one SCSI-style command per call."""

    def __init__(self, dev, usb):
        self.dev, self.usb, self.tag = dev, usb, 1
        cfg = dev.get_active_configuration() if self._configured() else None
        if cfg is None:
            dev.set_configuration()
            cfg = dev.get_active_configuration()
        intf = cfg[(0, 0)]
        self.intf = intf.bInterfaceNumber
        util = usb.util
        self.ep_out = util.find_descriptor(intf, custom_match=lambda e: util.endpoint_direction(
            e.bEndpointAddress) == util.ENDPOINT_OUT and util.endpoint_type(e.bmAttributes) == util.ENDPOINT_TYPE_BULK)
        self.ep_in = util.find_descriptor(intf, custom_match=lambda e: util.endpoint_direction(
            e.bEndpointAddress) == util.ENDPOINT_IN and util.endpoint_type(e.bmAttributes) == util.ENDPOINT_TYPE_BULK)
        if self.ep_out is None or self.ep_in is None:
            raise Fail("no bulk endpoints")
        util.claim_interface(dev, self.intf)

    def close(self):
        self.usb.util.dispose_resources(self.dev)

    def _configured(self):
        try:
            self.dev.get_active_configuration()
            return True
        except self.usb.core.USBError:
            return False

    def reset_recovery(self):
        """Bulk-only mass storage reset, then clear both halts (what a host does after a stall)."""
        try:
            self.dev.ctrl_transfer(0x21, 0xFF, 0, self.intf, None, timeout=2000)
        except self.usb.core.USBError:
            pass
        for ep in (self.ep_in, self.ep_out):
            try:
                self.dev.clear_halt(ep)
            except self.usb.core.USBError:
                pass

    def xfer(self, cdb, data_out=None, n_in=0, timeout=5000):
        tag = self.tag
        self.tag = (self.tag + 1) & 0xFFFFFFFF
        length = len(data_out) if data_out is not None else n_in
        flags = 0x80 if n_in else 0x00
        cbw = struct.pack("<4sIIBBB", b"USBC", tag, length, flags, 0, len(cdb)) + bytes(cdb).ljust(16, b"\0")
        self.ep_out.write(cbw, timeout)
        got = b""
        if data_out is not None and len(data_out):
            self.ep_out.write(bytes(data_out), timeout)
        elif n_in:
            got = bytes(self.ep_in.read(n_in, timeout))
        csw = bytes(self.ep_in.read(13, timeout))
        sig, ctag, _, status = struct.unpack("<4sIIB", csw)
        if sig != b"USBS" or ctag != tag:
            raise Fail(f"bad status block ({csw.hex()})")
        if status != 0:
            raise Fail(f"command failed (status {status})")
        return got


def cdb_for(cmd, args=b""):
    return (struct.pack(">H", cmd) + bytes(args)).ljust(16, b"\xFF")


# ----------------------------------------------------------------- device ---
class Uboot:
    def __init__(self, bot):
        self.bot = bot

    def inquiry(self):
        r = self.bot.xfer(bytes([0x12, 0, 0, 0, 36, 0]), n_in=36)
        return r[8:16].decode(errors="replace").strip(), r[16:32].decode(errors="replace").strip()

    def cmd(self, cmd, args=b""):
        r = self.bot.xfer(cdb_for(cmd, args), n_in=16)
        if struct.unpack(">H", r[0:2])[0] != cmd:
            raise Fail(f"command {cmd:04X} answered as {r[0:2].hex()}")
        return r[2:]

    def write_block(self, cmd, addr, data):
        args = struct.pack(">IH", addr, len(data)) + b"\0" + struct.pack("<H", crc16(data))
        self.bot.xfer(cdb_for(cmd, args), data_out=data)

    def start_loader(self, blob):
        for off in range(0, len(blob), IO):                  # shipped already ciphered: send raw
            self.write_block(CMD_WRITE_MEMORY, LOADER_ADDR + off, blob[off:off + IO])
        self.cmd(CMD_JUMP, struct.pack(">IH", LOADER_ADDR, LOADER_ARG))

    def chip(self):
        p = self.cmd(CMD_READ_KEY, struct.pack(">I", 0x00AC6900))
        k = bytearray([p[5], p[4]])
        crc_cipher(k)
        key = k[0] | k[1] << 8
        p = self.cmd(CMD_GET_ONLINE_DEVICE)
        return key, p[0], struct.unpack_from("<I", p, 2)[0]

    def read(self, addr, n):
        out = bytearray()
        while len(out) < n:
            k = min(IO, n - len(out))
            out += self.bot.xfer(cdb_for(CMD_READ_FLASH, struct.pack(">IH", addr + len(out), k)), n_in=k)
        return bytes(out)

    def write_sector(self, addr, data):
        if addr < WRITE_MIN or addr >= APP_END or addr % SECTOR or len(data) != SECTOR:
            raise Fail(f"refusing to write {addr:#x}")
        self.cmd(CMD_ERASE_SECTOR, struct.pack(">I", addr))
        for off in range(0, SECTOR, IO):
            self.write_block(CMD_WRITE_FLASH, addr + off, data[off:off + IO])
        if self.read(addr, SECTOR) != data:
            raise Fail(f"sector {addr:#07x} did not verify")

    def run_app(self):
        try:
            self.cmd(CMD_RUN_APP, struct.pack(">I", 1))
        except Exception:
            pass                                             # it may reset before it answers


# ------------------------------------------------------------------- flow ---
def rescue(dev, image, loader, write, backup_dir, say=print, confirm=None, key=None, label="V15"):
    """Everything in one session: the FM-1 leaves UBOOT (and crashes again) as soon as it is let go.
    key: the chip key the package was built for; when given it must equal the FM-1's. label names the
    package in the messages."""
    vendor, product = dev.inquiry()
    say(f"  device: {vendor} {product}")
    if vendor != "WL82":
        raise Fail(f"not a WL82 UBOOT ({vendor!r})")
    dev.start_loader(loader)
    chip_key, typ, fid = dev.chip()
    say(f"  chip key {chip_key:04X}, flash type {typ}, id {fid:06X}")
    if (chip_key, typ, fid) != (0x980F, 3, 0x856014):
        raise Fail("this is not the FM-1's chip and flash (expected key 980F, type 3, id 856014); nothing written")
    if key is not None and key != chip_key:
        raise Fail(f"the package is built for chip key {key:04X}, the FM-1 has {chip_key:04X}; nothing written")
    say("  reading the whole flash ...")
    t0 = time.time()
    now = dev.read(0, FLASH_SIZE)
    path = os.path.join(backup_dir, time.strftime("fm1-backup-%Y%m%d-%H%M%S.bin"))
    with open(path, "wb") as f:
        f.write(now)
    say(f"  backup saved: {path} ({time.time() - t0:.1f} s)")
    diff = [a for a in range(0, APP_END, SECTOR) if image[a:a + SECTOR] != now[a:a + SECTOR]]
    low = [a for a in diff if a < WRITE_MIN]
    if low:
        raise Fail(f"the protected head of the flash differs from {label} (sectors "
                   + " ".join(f"{a:#06x}" for a in low) + "): not writing; send this output and the backup")
    say(f"  {len(diff)} of {APP_END // SECTOR} firmware sectors differ from {label}")
    if not write and confirm is not None:
        write = keep_alive(dev, confirm)
    if not write:
        say(f"  check passed. Nothing written: run again with --write to put {label} back.")
        return False
    # high to low: the firmware's start-up code (low in the app area: its watchdog and its crash
    # counter, which bring it back to a rescue mode) is the last thing replaced if the write is cut short
    for i, a in enumerate(reversed(diff)):
        for attempt in range(3):
            try:
                dev.write_sector(a, image[a:a + SECTOR])
                break
            except Fail as e:
                if attempt == 2:
                    raise Fail(f"{e}; the FM-1 is still in update mode: run the script again")
        say(f"\r  writing {i + 1}/{len(diff)}", end="")
    say("")
    say("  final read ...")
    expect = image + now[APP_END:]
    final = dev.read(0, FLASH_SIZE)
    if final != expect:
        bad = [a for a in range(0, FLASH_SIZE, SECTOR) if final[a:a + SECTOR] != expect[a:a + SECTOR]]
        raise Fail("final read differs in sectors " + " ".join(f"{a:#07x}" for a in bad)
                   + "; run the script again with --write")
    say(f"  flash now holds {label}. Restarting the FM-1 ...")
    dev.run_app()
    return True


def keep_alive(dev, confirm):
    """Ask while the loader is kept busy (it resets the chip after ~3 s without a command)."""
    import threading
    stop, lost = threading.Event(), []

    def run():
        while not stop.wait(1.0):
            try:
                dev.cmd(CMD_GET_ONLINE_DEVICE)
            except Exception as e:
                lost.append(e)
                return
    t = threading.Thread(target=run, daemon=True)
    t.start()
    try:
        answer = confirm()
    finally:
        stop.set()
        t.join()
    if lost:
        raise Fail(f"the FM-1 dropped out while waiting ({lost[0]}); run it again")
    return answer


def get_loader(path):
    if path:
        blob = open(path, "rb").read()
    else:
        print("downloading JieLi's flash loader (wl82loader.bin) ...")
        with urllib.request.urlopen(LOADER_URL, timeout=30) as r:
            blob = r.read()
    if hashlib.sha256(blob).hexdigest() != LOADER_SHA256:
        raise Fail("wl82loader.bin: unexpected contents")
    return blob


def backend(usb):
    """libusb: pip's libusb-package (no Homebrew needed), else the system's or Homebrew's"""
    try:
        import libusb_package
        b = libusb_package.get_libusb1_backend()
        if b is not None:
            return b
    except ImportError:
        pass
    import usb.backend.libusb1 as lb
    for path in (None, "/opt/homebrew/lib/libusb-1.0.dylib", "/usr/local/lib/libusb-1.0.dylib"):
        b = lb.get_backend() if path is None else (lb.get_backend(find_library=lambda _: path)
                                                   if os.path.exists(path) else None)
        if b is not None:
            return b
    raise Fail("libusb not found: pip install libusb-package")


def open_device(usb, timeout):
    """Wait for 4C4A:8057 and take it from macOS's mass storage driver."""
    print("waiting for the FM-1 in update mode (switch it on, or wait for it to cycle) ...")
    be = backend(usb)
    t0 = time.time()
    while time.time() - t0 < timeout:
        dev = usb.core.find(idVendor=VID, idProduct=PID, backend=be)
        if dev is not None:
            print(f"found it after {time.time() - t0:.1f} s")
            try:
                try:
                    if dev.is_kernel_driver_active(0):
                        dev.detach_kernel_driver(0)
                except (NotImplementedError, usb.core.USBError) as e:
                    if getattr(e, "errno", None) not in (None, 2):
                        print(f"  (could not detach macOS's driver: {e})")
                bot = Bot(dev, usb)
                try:
                    Uboot(bot).inquiry()
                except Exception:
                    bot.reset_recovery()
                return bot
            except Exception as e:
                print(f"  could not open it ({e}); waiting for the next cycle")
                time.sleep(1.0)
                continue
        time.sleep(0.02)
    raise Fail("the FM-1 did not appear in update mode")


def make_ask(pkg):
    """the typed confirmation for --ask: yes for V15, the package's hash prefix for anything else"""
    def ask():
        print("\n  Everything checks out, and the backup is saved.")
        if pkg.stock:
            q, want = "  Type yes and press Return to put the stock firmware back: ", "yes"
        else:
            want = pkg.sha256[:8]
            q = f"  This is NOT the stock firmware. Type {want} (its sha256 start) to write {pkg.product}: "
        try:
            return input(q).strip().lower() == want
        except EOFError:
            return False
    return ask


def parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", default=STOCK_IN_TREE,
                    help="M-VAVE's FM-1 V15 .fwsc (the default: the copy this tree keeps, web/stock/FM-1_015.fwsc, "
                         "Apache-2.0), or any known-good FM-1 .fwsc")
    ap.add_argument("--write", action="store_true", help="write the package (without it: check and back up only)")
    ap.add_argument("--confirm-package", metavar="SHA256", help="a package that is not V15 is only written with "
                    "this: the first 8 or more hex digits of its sha256 (--check prints it)")
    ap.add_argument("--check", action="store_true", help="validate the package and stop: no USB, no sudo, no download")
    ap.add_argument("--ask", action="store_true", help="after the check and the backup, ask before writing")
    ap.add_argument("--loader", help="a local wl82loader.bin instead of downloading it")
    ap.add_argument("--backup-dir", help="where the flash backup goes (default: next to the package)")
    ap.add_argument("--wait", type=float, default=120, help="seconds to wait for the FM-1 (default 120)")
    ap.add_argument("--tries", type=int, default=5, help="attempts if it drops out mid-way (default 5)")
    return ap


def main(argv=None, open_dev=None, is_root=None):
    """open_dev() -> a Bot-like transport and is_root: seams for the tests (a fake USB device, no sudo)"""
    args = parser().parse_args(argv)
    try:
        pkg = load_package(args.package)
        print(describe(pkg), flush=True)
        if args.check:
            print("  package checks out (identity, CRCs, flash entry, chip key). No USB used, nothing written.")
            return 0
        if args.confirm_package and not confirmed(pkg, args.confirm_package):
            raise Fail("--confirm-package does not match this package's sha256 (8 or more hex digits); nothing was done")
        if not pkg.stock and args.write and not args.ask and not args.confirm_package:
            raise Fail("this is not the stock V15 package: to write it, add --confirm-package "
                       f"{pkg.sha256[:8]} (or use --ask and type it). Nothing was done.")
        loader = get_loader(args.loader)
        if open_dev is None:
            if (os.geteuid() if is_root is None else not is_root) != 0:
                raise Fail("run it with sudo: macOS has to give the device up")
            try:
                import usb.core
                import usb.util
            except ImportError:
                raise Fail("pyusb is missing: build/rescue/env/bin/pip install pyusb libusb-package")
            errors = (Fail, usb.core.USBError)
            open_dev = lambda: open_device(usb, args.wait)
        else:
            errors = (Fail,)
        backup_dir = args.backup_dir or os.path.dirname(pkg.path)
        label = "V15" if pkg.stock else f"{pkg.product} ({pkg.sha256[:8]})"
        for attempt in range(1, args.tries + 1):
            bot = open_dev()
            try:
                rescue(Uboot(bot), pkg.flash, loader, args.write, backup_dir,
                       say=lambda *a, **k: print(*a, **k, flush=True),
                       confirm=make_ask(pkg) if args.ask else None, key=pkg.key, label=label)
                return 0
            except errors as e:
                print(f"\n  attempt {attempt}: {e}")
                if isinstance(e, Fail) and ("not writing" in str(e) or "not the FM-1" in str(e) or "chip key" in str(e)):
                    return 1
            finally:
                try:
                    bot.close()
                except Exception:
                    pass
        raise Fail("gave up; send this whole output")
    except Fail as e:
        print(f"fm1_rescue: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())

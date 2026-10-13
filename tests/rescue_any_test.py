#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_rescue.py with any known-good .fwsc, on the host, against a fake USB transport (no real device,
no pyusb, no sudo).

  python3 tests/rescue_any_test.py

- load_package(): a synthetic package (made with fm1pkg_make.ufw) is accepted; a damaged one (header, a file CRC,
  a cut-short file), another product, a wrong flash-entry size and a chip key that disagrees with the
  application header are all refused before any USB is opened.
- main(): --check needs no USB; a package that is not V15 is not written without --confirm-package (or --ask);
  a wrong hash is refused; the confirmed write goes through the real Uboot / BOT protocol classes against a
  FakeBot that simulates the chip's ROM UBOOT and its loader, and ends with the flash holding the package;
  a package for another chip key writes nothing."""
import hashlib
import io
import os
import random
import struct
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_rescue as R  # noqa: E402
import fm1pkg_make as M  # noqa: E402

fails = 0


def ok(cond, what):
    global fails
    print(f"{what:70} {'ok' if cond else 'FAIL'}")
    fails += not cond


def synth_flash(key, seed):
    """a flash image [0, APP_END) as fm1pkg_make.flash_image lays the head out, without the SDK files"""
    rnd = random.Random(seed)
    f = bytearray(rnd.getrandbits(8) for _ in range(R.APP_END))
    f[0:0x4000] = b"\xFF" * 0x4000
    isd = M.key_blob(key) + b"[TEST]\r\n"
    f[0x500:0x500 + len(isd)] = isd
    head = bytearray(M.entry(M.crc16(isd), 0x500, len(isd), 0x02, 0x80, 0, "isd_config.ini"))
    M.enc(head, 0, 32)
    f[32:64] = head
    app = bytearray(M.entry(0xFFFF, 0x4000, 0x1000, 0x82, 0xFF, 0, "app.bin"))     # 32 B application header
    M.sfc(app, 0, 32, 0, key)
    f[0x4000:0x4020] = app
    return bytes(f)


def write_pkg(path, flash, product="FM-1_901"):
    Path(path).write_bytes(M.ufw(flash, b"OTA" * 100, product))
    return str(path)


class FakeBot:
    """a chip in ROM UBOOT: Bot.xfer(cdb, data_out, n_in) as Uboot calls it, the same command set"""

    def __init__(self, flash, chip_key=0x980F, flash_id=0x856014):
        self.flash, self.chip_key, self.flash_id = bytearray(flash), chip_key, flash_id
        self.loaded, self.erased, self.sector_writes, self.ran, self.closed = {}, [], [], False, False

    def xfer(self, cdb, data_out=None, n_in=0, timeout=5000):
        if cdb[0] == 0x12:
            return b"\0" * 8 + b"WL82    " + b"UBOOT1.00".ljust(16) + b"\0" * 4
        cmd = struct.unpack(">H", cdb[:2])[0]
        a = cdb[2:]
        if cmd == R.CMD_WRITE_MEMORY:
            addr, n = struct.unpack(">IH", a[:6])
            assert struct.unpack("<H", a[7:9])[0] == R.crc16(data_out), "block CRC"
            self.loaded[addr] = bytes(data_out)
            return b""
        if cmd == R.CMD_JUMP:
            return struct.pack(">H", cmd).ljust(16, b"\0")
        if cmd == R.CMD_READ_KEY:
            k = bytearray([self.chip_key & 255, self.chip_key >> 8])
            R.crc_cipher(k)
            return struct.pack(">H", cmd) + bytes([0, 0, 0, 0, k[1], k[0]]) + b"\0" * 8
        if cmd == R.CMD_GET_ONLINE_DEVICE:
            return struct.pack(">H", cmd) + bytes([3, 0]) + struct.pack("<I", self.flash_id) + b"\0" * 8
        if cmd == R.CMD_READ_FLASH:
            addr, n = struct.unpack(">IH", a[:6])
            return bytes(self.flash[addr:addr + n])
        if cmd == R.CMD_ERASE_SECTOR:
            addr = struct.unpack(">I", a[:4])[0]
            self.erased.append(addr)
            self.flash[addr:addr + R.SECTOR] = b"\xFF" * R.SECTOR
            return struct.pack(">H", cmd).ljust(16, b"\0")
        if cmd == R.CMD_WRITE_FLASH:
            addr, n = struct.unpack(">IH", a[:6])
            self.flash[addr:addr + n] = data_out
            self.sector_writes.append(addr)
            return b""
        if cmd == R.CMD_RUN_APP:
            self.ran = True
            return struct.pack(">H", cmd).ljust(16, b"\0")
        raise AssertionError(f"unexpected command {cmd:04X}")

    def close(self):
        self.closed = True


def run(argv, bot=None):
    out = io.StringIO()
    with redirect_stdout(out):
        rc = R.main(argv, open_dev=(lambda: bot) if bot is not None else (lambda: (_ for _ in ()).throw(AssertionError("USB opened"))))
    return rc, out.getvalue()


def main():
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        loader = t / "wl82loader.bin"
        loader.write_bytes(b"x")
        R.LOADER_SHA256 = hashlib.sha256(b"x").hexdigest()          # the fake loader stands in for the real blob
        data = bytes(random.Random(3).getrandbits(8) for _ in range(R.FLASH_SIZE - R.APP_END))
        stock_like = synth_flash(0x980F, 1)
        theirs = synth_flash(0x980F, 2)                             # "an earlier build that booted"
        broken = synth_flash(0x980F, 9)                             # what is on the FM-1 now
        good = write_pkg(t / "good.fwsc", theirs)
        want = hashlib.sha256(Path(good).read_bytes()).hexdigest()

        pkg = R.load_package(good)
        ok(pkg.flash == theirs and pkg.key == 0x980F and pkg.product == "FM-1_901" and not pkg.stock and pkg.sha256 == want,
           "load_package: a valid non-stock package: flash, key, identity, hash")
        ok(R.load_package(write_pkg(t / "s.fwsc", stock_like)).flash == stock_like, "load_package: the flash entry round-trips")

        def refuses(path, what, frag=None):
            try:
                R.load_package(path)
                ok(False, f"load_package refuses {what}")
            except R.Fail as e:
                ok(frag is None or frag in str(e), f"load_package refuses {what} ({str(e)[:40]}...)")

        raw = bytearray(Path(good).read_bytes())
        for name, mutate, frag in (("a flipped header byte", lambda b: b.__setitem__(0x44, b[0x44] ^ 1), "file list"),
                                   ("a flipped data byte", lambda b: b.__setitem__(0x8000, b[0x8000] ^ 1), "CRC"),
                                   ("a cut-short file", lambda b: b.__delitem__(slice(len(b) - 5000, None)), "cut short")):
            b = bytearray(raw)
            mutate(b)
            (t / "bad.fwsc").write_bytes(b)
            refuses(str(t / "bad.fwsc"), name, frag)
        (t / "junk.fwsc").write_bytes(b"\0" * 5000)
        refuses(str(t / "junk.fwsc"), "a file that is not a package")
        refuses(str(t / "missing.fwsc"), "a missing file", "cannot read")
        refuses(write_pkg(t / "short.fwsc", theirs[:R.APP_END - 0x1000]), "a flash entry that is not 0x93000 B", "expected")
        refuses(write_pkg(t / "other.fwsc", theirs, "XYZ_1"), "another product's package", "identity")
        mism = bytearray(theirs)                                    # key in isd_config != the key the app header is SFC'd with
        mism[0x4000] ^= 0xFF
        refuses(write_pkg(t / "mism.fwsc", bytes(mism)), "an application header that disagrees with the key", "application header")

        rc, out = run([good, "--check"])
        ok(rc == 0 and want in out and "CUSTOM" in out and "No USB used" in out, "--check: names the package, no USB, exit 0")
        rc, out = run([str(t / "bad.fwsc"), "--check"])
        ok(rc == 1 and "damaged" in out, "--check: a damaged package exits 1")

        flash = broken + data
        base = ["--loader", str(loader), "--backup-dir", str(t), "--tries", "1"]
        bot = FakeBot(flash)
        rc, out = run([good, "--write"] + base, bot)
        ok(rc == 1 and not bot.erased and "--confirm-package" in out, "non-stock + --write without the flag: refused, USB never opened")
        rc, out = run([good, "--write", "--confirm-package", "deadbeef"] + base)
        ok(rc == 1 and "does not match" in out, "a wrong --confirm-package: refused")
        rc, out = run([good, "--write", "--confirm-package", want[:6]] + base)
        ok(rc == 1, "a hash prefix under 8 digits: refused")

        bot = FakeBot(flash)
        rc, out = run([good] + base, bot)
        ok(rc == 0 and not bot.erased and not bot.ran and bytes(bot.flash) == flash and "differ from FM-1_901" in out,
           "check and back up (no --write): flash untouched, backup taken")

        bot = FakeBot(flash)
        rc, out = run([good, "--write", "--confirm-package", want[:12]] + base, bot)
        ok(rc == 0 and bytes(bot.flash[:R.APP_END]) == theirs and bytes(bot.flash[R.APP_END:]) == data and bot.ran,
           "confirmed write: the package is in [0, 0x93000), the data above kept, restarted")
        ok(all(R.WRITE_MIN <= a < R.APP_END for a in bot.erased) and bot.closed, "only sectors in [0x4000, 0x93000) erased; transport closed")
        ok(out.index(want) < out.index("reading the whole flash"), "the package is named before the flash is touched")

        bot = FakeBot(flash)
        orig_input = __builtins__["input"] if isinstance(__builtins__, dict) else __builtins__.input
        for typed, writes in (("yes", False), (want[:8], True)):
            bot = FakeBot(flash)
            (__builtins__ if isinstance(__builtins__, dict) else vars(__builtins__))["input"] = lambda q, typed=typed: typed
            try:
                rc, out = run([good, "--ask"] + base, bot)
            finally:
                (__builtins__ if isinstance(__builtins__, dict) else vars(__builtins__))["input"] = orig_input
            ok(rc == 0 and bool(bot.ran) == writes, f"--ask for a non-stock package: typing {'the hash' if writes else 'yes'} "
                                                    f"{'writes' if writes else 'does not write'}")

        other = write_pkg(t / "otherkey.fwsc", synth_flash(0x1234, 5))
        bot = FakeBot(flash)
        rc, out = run([other, "--write", "--confirm-package", hashlib.sha256(Path(other).read_bytes()).hexdigest()[:8]] + base, bot)
        ok(rc == 1 and not bot.erased and "chip key" in out, "a package for another chip key: nothing written")

        bot = FakeBot(flash, chip_key=0x1234)
        rc, out = run([good, "--write", "--confirm-package", want[:8]] + base, bot)
        ok(rc == 1 and not bot.erased, "a chip that is not the FM-1: nothing written")

        # the stock path stays the default: no flag needed for the V15 hash
        stock_pkg = R.Package(pkg.path, pkg.product, R.STOCK_SHA256, stock_like, 0x980F, True)
        ok(R.make_ask(stock_pkg) is not None and "stock" in R.describe(stock_pkg), "stock V15: described as stock, no flag needed")
    print(f"rescue_any: {'ok' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

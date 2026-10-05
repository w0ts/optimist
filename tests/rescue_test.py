#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_rescue.py on the host, against a simulated FM-1 in the chip's UBOOT mode.

  python3 tests/rescue_test.py

- package_flash_image() reads our own .fwsc (build/felucca.fwsc, when built) exactly as
  fm1pkg_make.flash_image() wrote it: the rescue tool and our package agree on the format.
- rescue(): the backup is the whole flash and comes first; without --write nothing is written;
  with it, only the 4 KiB sectors of [0x4000, 0x93000) that differ are written, high to low, the
  device data above 0x93000 is kept, the final read is checked and the FM-1 restarts; a head that
  differs, or another chip / flash, stops it before any write; a failed sector write is retried."""
import os
import random
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_rescue as R  # noqa: E402

fails = 0


def ok(cond, what):
    global fails
    print(f"{what:66} {'ok' if cond else 'FAIL'}")
    fails += not cond


class FakeUboot:
    """the calls rescue() makes on fm1_rescue.Uboot"""

    def __init__(self, flash, chip=(0x980F, 3, 0x856014), vendor="WL82", fail_writes=0):
        self.flash, self.chip_id, self.vendor = bytearray(flash), chip, vendor
        self.writes, self.loader, self.ran, self.fail_writes = [], None, False, fail_writes

    def inquiry(self):
        return self.vendor, "UBOOT1.00"

    def start_loader(self, blob):
        self.loader = blob

    def chip(self):
        return self.chip_id

    def read(self, addr, n):
        return bytes(self.flash[addr:addr + n])

    def write_sector(self, addr, data):
        if self.fail_writes:
            self.fail_writes -= 1
            raise R.Fail("write: no answer")
        assert len(data) == R.SECTOR and addr % R.SECTOR == 0
        self.writes.append(addr)
        self.flash[addr:addr + R.SECTOR] = data

    def run_app(self):
        self.ran = True

    def cmd(self, *a):
        return b""


def images():
    rnd = random.Random(7)
    stock = bytes(rnd.getrandbits(8) for _ in range(R.APP_END))
    ours = bytearray(stock)
    for a in (0x4000, 0x10000, 0x10003, 0x55000, 0x92FFF):     # four sectors differ (two edits share one)
        ours[a] ^= 0x5A
    data = bytes(rnd.getrandbits(8) for _ in range(R.FLASH_SIZE - R.APP_END))   # settings, presets, samples
    return stock, bytes(ours) + data, data


def main():
    out = []
    say = lambda *a, **k: out.append(" ".join(map(str, a)))
    pkg = ROOT / "build" / "felucca.fwsc"
    if pkg.exists() and (ROOT / "build" / "felucca.bin").exists():
        import fm1pkg_make
        want = fm1pkg_make.flash_image((ROOT / "build" / "felucca.bin").read_bytes(), fm1pkg_make.KEY)
        got = R.package_flash_image(pkg.read_bytes())
        ok(got == bytes(want[:len(got)]) and len(got) <= R.APP_END,
           f"package_flash_image(our .fwsc) == fm1pkg_make's flash image ({len(got)} B)")
    else:
        print("rescue: build/felucca.fwsc not built: package format check skipped")
    v15 = os.environ.get("FM1_V15")                          # M-VAVE's FM-1.fwsc, if you have it
    if v15 and Path(v15).exists():
        raw = Path(v15).read_bytes()
        import hashlib
        ok(hashlib.sha256(raw).hexdigest() == R.STOCK_SHA256 and len(R.package_flash_image(raw)) == R.APP_END,
           "FM1_V15: the stock V15 package is the one expected, its flash entry 0x93000 B")
    try:
        R.package_flash_image(b"\0" * 4096)
        ok(False, "package_flash_image: refuses a file that is not a package")
    except R.Fail:
        ok(True, "package_flash_image: refuses a file that is not a package")

    stock, flash, data = images()
    with tempfile.TemporaryDirectory() as tmp:
        dev = FakeUboot(flash)
        r = R.rescue(dev, stock, b"LDR", False, tmp, say=say)
        backups = list(Path(tmp).glob("fm1-backup-*.bin"))
        ok(r is False and not dev.writes and not dev.ran, "check only: nothing written, no restart")
        ok(len(backups) == 1 and backups[0].read_bytes() == flash, "check only: the whole flash backed up first")
        ok(dev.loader == b"LDR", "the flash loader is started in RAM")

        dev = FakeUboot(flash)
        r = R.rescue(dev, stock, b"LDR", True, tmp, say=say)
        ok(r is True and dev.writes == [0x92000, 0x55000, 0x10000, 0x4000],
           f"write: only the differing sectors, high to low ({[hex(a) for a in dev.writes]})")
        ok(bytes(dev.flash[:R.APP_END]) == stock and bytes(dev.flash[R.APP_END:]) == data,
           "write: V15 in the firmware area, the device data above 0x93000 kept")
        ok(dev.ran, "write: the FM-1 restarts once the final read matches")

        head = bytearray(flash)
        head[0x100] ^= 1
        dev = FakeUboot(head)
        try:
            R.rescue(dev, stock, b"LDR", True, tmp, say=say)
            ok(False, "a head (< 0x4000) that differs: refused, nothing written")
        except R.Fail as e:
            ok(not dev.writes and "protected head" in str(e), "a head (< 0x4000) that differs: refused, nothing written")

        for chip, what in (((0x1234, 3, 0x856014), "another chip key"), ((0x980F, 3, 0xEF4014), "another flash")):
            dev = FakeUboot(flash, chip=chip)
            try:
                R.rescue(dev, stock, b"LDR", True, tmp, say=say)
                ok(False, f"{what}: refused before anything is read or written")
            except R.Fail:
                ok(not dev.writes, f"{what}: refused before anything is read or written")

        dev = FakeUboot(flash, fail_writes=2)
        r = R.rescue(dev, stock, b"LDR", True, tmp, say=say)
        ok(r is True and bytes(dev.flash[:R.APP_END]) == stock, "a sector write that fails twice is retried")
    print(f"rescue: {'ok' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

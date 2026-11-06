#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_rescue.py --dongle: tools/fm1_dongle.py against a simulated FM-1-transporter data port.

  python3 tests/rescue_dongle_test.py

- the simulated dongle answers as the transporter's src/transporter_proto.c does with
  tools/dongle/transporter-optimist.patch (wsec up to 0xDA000, status with vendor and product), payload CRCs and
  ranges included;
- a rescue through it: the loader goes up in 4 KiB `ldr` chunks, the backup is one `read` of the whole flash,
  only the differing sectors are written with `wsec`, high to low, the final read is checked, `runapp` at the end;
  without --write nothing is written;
- a read whose END crc32 does not match its bytes is refused;
- a layout switch clears [0x93000, 0xDA000): the patched dongle accepts it, an unpatched one (writes end at
  0x93000) refuses it and the tool stops and says so: the reason the patch exists;
- open_dongle(): the port that answers PONG; a rekey, then polling until update mode (crashed firmware: the user
  switches the FM-1 off and on); the soft key when stock V15 runs; a clean failure without a dongle or an FM-1."""
import contextlib
import hashlib
import io
import random
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
import fm1_dongle as D  # noqa: E402
import fm1_rescue as R  # noqa: E402
import fm1pkg_make as M  # noqa: E402
import rescue_any_test as A  # noqa: E402  (synth_flash, write_pkg)

LOADER = bytes((i * 7) & 0xFF for i in range(24064))
PATCHED_END, STOCK_END = 0xDA000, 0x93000
fails = 0


def ok(cond, what):
    global fails
    print(f"{what:74} {'ok' if cond else 'FAIL'}")
    fails += not cond


class FakeTransporter:
    """the dongle's data port as transporter_proto.c answers it: a pyserial-like object"""

    def __init__(self, flash, uboot=True, v15=False, chip=(0x980F, 3, 0x856014), write_end=PATCHED_END,
                 polls=2, bad_read_crc=False):
        self.flash, self.uboot, self.v15, self.chip, self.write_end = bytearray(flash), uboot, v15, chip, write_end
        self.polls, self.bad_read_crc = polls, bad_read_crc   # status polls after rekey / soft key until uboot=1
        self.armed = False
        self.out, self.inbuf, self.pending = bytearray(), bytearray(), None
        self.loader, self.writes, self.log = bytearray(), [], []
        self.ran = self.closed = False
        self.rekeyed = 0
        self.timeout = 0

    # pyserial's side
    def reset_input_buffer(self):
        self.out.clear()

    def write(self, data):
        self.inbuf += data
        self._pump()

    def read(self, n):
        chunk = bytes(self.out[:n])
        del self.out[:n]
        return chunk

    def readline(self):
        i = self.out.find(b"\n")
        line = bytes(self.out[:i + 1]) if i >= 0 else bytes(self.out)
        del self.out[:len(line)]
        return line

    def close(self):
        self.closed = True

    # the firmware's side
    def _pump(self):
        while True:
            if self.pending:
                line, n = self.pending
                if len(self.inbuf) < n:
                    return
                payload, self.inbuf = bytes(self.inbuf[:n]), self.inbuf[n:]
                self.pending = None
                self._binary(line, payload)
                continue
            i = self.inbuf.find(b"\n")
            if i < 0:
                return
            line, self.inbuf = self.inbuf[:i].decode(), self.inbuf[i + 1:]
            self.log.append(line)
            self._command(line)

    def _reply(self, text):
        self.out += text.encode() + b"\n"

    def _command(self, line):
        a = line.split()
        if a[0] == "ping":
            self._reply("PONG")
        elif a[0] == "status":
            if self.armed:
                if self.polls:
                    self.polls -= 1
                else:
                    self.uboot, self.armed = True, False
            inq = " vendor=WL82 product=UBOOT1.00" if self.uboot else " vendor=- product=-"
            self._reply(f"OK uboot={int(self.uboot)} v15={int(self.v15)} loader={int(len(self.loader) == len(LOADER))} "
                        f"loader_running=0" + inq)
        elif a[0] == "info":
            if not self.uboot:
                self._reply("ERR no-uboot")
            elif len(self.loader) != len(LOADER):
                self._reply("ERR info")
            else:
                self._reply(f"OK key={self.chip[0]:04X} type={self.chip[1]} id={self.chip[2]:06X}")
        elif a[0] == "read" and len(a) == 3:
            self._read(int(a[1], 0), int(a[2], 0))
        elif a[0] in ("wsec", "ldr") and len(a) in (3, 4):
            self.pending = (line, R.SECTOR if a[0] == "wsec" else int(a[2], 0))
        elif a[0] == "runapp":
            self.ran = True
            self._reply("OK")
        elif a[0] == "rekey":
            self._reply("OK rekey")
            self.rekeyed += 1
            self.uboot, self.armed = False, True
        elif a[0] == "uboot":
            if self.v15:
                self._reply("OK softkey")
                self.armed = True
            else:
                self._reply("ERR no-v15")
        else:
            self._reply("ERR unknown")

    def _read(self, addr, n):
        if n == 0 or addr >= R.FLASH_SIZE or n > R.FLASH_SIZE - addr:
            self._reply("ERR range")
        elif not self.uboot:
            self._reply("ERR no-uboot")
        elif len(self.loader) != len(LOADER):
            self._reply("ERR loader")
        else:
            data = bytes(self.flash[addr:addr + n])
            self._reply(f"DATA {n}")
            self.out += data
            self._reply("END 00000000" if self.bad_read_crc else f"END {D.crc32(data):08X}")

    def _binary(self, line, payload):
        a = line.split()
        if int(a[-1], 16) != D.crc32(payload):
            self._reply(f"ERR crc {D.crc32(payload):08X}")
        elif a[0] == "ldr":
            if int(a[1], 0) != len(self.loader) or len(self.loader) + len(payload) > len(LOADER):
                self._reply("ERR loader-range")
            else:
                self.loader += payload
                self._reply("OK")
        else:
            addr = int(a[1], 0)
            if addr < 0x4000 or addr >= self.write_end or addr % R.SECTOR:
                self._reply("ERR range")
            elif not self.uboot:
                self._reply("ERR no-uboot")
            else:
                self.flash[addr:addr + R.SECTOR] = payload
                self.writes.append(addr)
                self._reply("OK")


class Silent:
    """a serial port that is not the dongle (its log console, or something else): never answers"""
    timeout = 0

    def write(self, data):
        pass

    def read(self, n):
        return b""

    def readline(self):
        return b""

    def reset_input_buffer(self):
        pass

    def close(self):
        pass


class FakeSerialModule:
    SerialException = OSError

    def __init__(self, ports):
        self.ports = ports

    def Serial(self, path, baud, timeout=None):
        if path not in self.ports:
            raise OSError(f"no {path}")
        return self.ports[path]


def run(argv, fake):
    """fm1_rescue.main with the dongle already open: (exit code, everything printed)"""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = R.main(argv, open_dev=lambda: D.Dongle(fake))
    return rc, buf.getvalue()


def big_layout_head(flash, key):
    """the device as if a fake-SD-mode app were installed: its app_area_head at 0x4000 (as rescue_test does)"""
    head = bytearray(M.entry(0xFFFF, 0x02000120, 0xCF25B - 0x4000, 0x83, 0xFF, 0, "app_area_head"))
    M.sfc(head, 0, 32, 0, key)
    was = bytearray(flash)
    was[0x4000:0x4020] = head
    return bytes(was)


def main():
    key = 0x980F
    image = A.synth_flash(key, 1)                       # the package's flash image [0, 0x93000)
    device = bytearray(image)
    for a in (0x4000, 0x10000, 0x10003, 0x55000, 0x92FFF):   # four sectors differ on the device
        device[a] ^= 0x5A
    rnd = random.Random(11)
    tail = bytes(rnd.getrandbits(8) for _ in range(R.FLASH_SIZE - R.APP_END))
    flash = bytes(device) + tail
    R.get_loader = lambda path: LOADER                  # no download, no hash of the real blob
    with tempfile.TemporaryDirectory() as tmp:
        pkg = A.write_pkg(Path(tmp) / "ours.fwsc", image)
        sha8 = hashlib.sha256(Path(pkg).read_bytes()).hexdigest()[:8]
        base = [pkg, "--dongle", "--backup-dir", tmp, "--tries", "1"]
        writing = base + ["--write", "--confirm-package", sha8]

        fake = FakeTransporter(flash)
        rc, text = run(base, fake)
        ok(rc == 0 and not fake.writes and not fake.ran, "check only through the dongle: nothing written, no restart")
        ldr = [l for l in fake.log if l.startswith("ldr ")]
        ok(fake.loader == LOADER and len(ldr) == 6 and ldr[0] == f"ldr 0 4096 {D.crc32(LOADER[:4096]):08X}"
           and ldr[-1] == f"ldr 20480 3584 {D.crc32(LOADER[20480:]):08X}",
           "the loader goes up in 4 KiB ldr chunks, each with its crc32")
        ok(f"read 0x0 {R.FLASH_SIZE}" in fake.log, "the backup is one read of the whole flash")
        backups = list(Path(tmp).glob("fm1-backup-*.bin"))
        ok(len(backups) == 1 and backups[0].read_bytes() == flash, "the backup file holds that read")
        ok("device: WL82 UBOOT1.00" in text and "chip key 980F" in text, "identity from the dongle's status and info")
        ok(fake.closed, "the port is closed at the end")

        fake = FakeTransporter(flash)
        rc, text = run(writing, fake)
        wsec = [l.split()[1] for l in fake.log if l.startswith("wsec ")]
        ok(rc == 0 and fake.writes == [0x92000, 0x55000, 0x10000, 0x4000] and wsec == ["0x92000", "0x55000", "0x10000", "0x4000"],
           f"write: only the differing sectors, high to low ({[hex(a) for a in fake.writes]})")
        ok(bytes(fake.flash[:R.APP_END]) == image and bytes(fake.flash[R.APP_END:]) == tail,
           "write: the package in the firmware area, the device data above 0x93000 kept")
        ok(fake.ran and sum(l.startswith("read ") for l in fake.log) == 2, "write: the final read, then runapp")

        fake = FakeTransporter(flash, bad_read_crc=True)
        rc, text = run(writing, fake)
        ok(rc == 1 and "read check failed" in text and not fake.writes,
           "a read whose END crc32 does not match: refused before anything is written")

        fake = FakeTransporter(flash, chip=(0x1234, 3, 0x856014))
        rc, text = run(writing, fake)
        ok(rc == 1 and not fake.writes and "chip key" in text, "another chip key from info: refused")

        was_big = big_layout_head(flash, key)
        fake = FakeTransporter(was_big, write_end=STOCK_END)
        rc, text = run(writing, fake)
        ok(rc == 1 and "ERR range" in text and not fake.writes,
           "an unpatched dongle (writes end at 0x93000) refuses the layout-switch clears; the tool stops")
        fake = FakeTransporter(was_big)
        rc, text = run(writing, fake)
        top = R.flash_layout.CLEAR_TOP
        ok(rc == 0 and bytes(fake.flash[:R.APP_END]) == image
           and bytes(fake.flash[R.APP_END:top]) == b"\xFF" * (top - R.APP_END) and bytes(fake.flash[top:]) == was_big[top:],
           "the patched dongle: [0x93000, 0xDA000) cleared on the layout switch, kits and presets kept")
        ok(PATCHED_END == top, "the patch's write end is tools/flash_layout.py CLEAR_TOP")
        patch = (ROOT / "tools" / "dongle" / "transporter-optimist.patch").read_text()
        ok(f"+#define JL_WRITE_END 0x{PATCHED_END:X}" in patch and f"-#define JL_WRITE_END 0x{STOCK_END:X}" in patch
           and "vendor=%s product=%s" in patch, "transporter-optimist.patch raises JL_WRITE_END and adds vendor/product")

    # open_dongle
    D.PORT_WAIT = D.REKEY_WAIT = 0
    quiet = lambda *a: None
    glob_was = D.glob.glob
    try:
        dev = FakeTransporter(flash, uboot=False, polls=2)
        serial = FakeSerialModule({"/dev/cu.usbmodemA": Silent(), "/dev/cu.usbmodemB": dev})
        D.glob.glob = lambda pat: ["/dev/cu.usbmodemA", "/dev/cu.usbmodemB"]
        sleeps = []
        d = D.open_dongle(None, 30, say=quiet, serial=serial, sleep=sleeps.append)
        ok(isinstance(d, D.Dongle) and d.port is dev and dev.rekeyed == 1 and dev.uboot and len(sleeps) == 2,
           "open_dongle: the port that answers PONG; no update mode: rekey, then polled until it is there")
        dev = FakeTransporter(flash, uboot=False, v15=True, polls=1)
        serial = FakeSerialModule({"/dev/cu.usbmodemB": dev})
        d = D.open_dongle("/dev/cu.usbmodemB", 30, say=quiet, serial=serial, sleep=lambda s: None)
        ok(d.port is dev and dev.rekeyed == 0 and "uboot" in dev.log and dev.uboot,
           "open_dongle: stock V15 running: the soft key, no rekey")
        dev = FakeTransporter(flash)
        d = D.open_dongle("/dev/cu.usbmodemB", 30, say=quiet, serial=FakeSerialModule({"/dev/cu.usbmodemB": dev}),
                          sleep=lambda s: None)
        ok(d.port is dev and dev.rekeyed == 0 and dev.log == ["ping", "status"], "open_dongle: already in update mode: nothing sent")
        try:
            D.open_dongle("/dev/cu.usbmodemB", 30, say=quiet, serial=FakeSerialModule({"/dev/cu.usbmodemB": Silent()}),
                          sleep=lambda s: None)
            ok(False, "open_dongle: no dongle answers: a clear failure")
        except R.Fail as e:
            ok("no dongle" in str(e), "open_dongle: no dongle answers: a clear failure")
        dev = FakeTransporter(flash, uboot=False, polls=10 ** 6)
        try:
            D.open_dongle("/dev/cu.usbmodemB", 0, say=quiet, serial=FakeSerialModule({"/dev/cu.usbmodemB": dev}),
                          sleep=lambda s: None)
            ok(False, "open_dongle: no FM-1 within --wait: a clear failure")
        except R.Fail as e:
            ok("did not enter update mode" in str(e) and dev.closed, "open_dongle: no FM-1 within --wait: a clear failure")
    finally:
        D.glob.glob = glob_was
    print(f"rescue_dongle: {'ok' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

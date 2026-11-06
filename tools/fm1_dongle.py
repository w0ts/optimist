#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fm1_dongle: tools/fm1_rescue.py through the RP2040 recovery dongle (tools/dongle/README.md).

The dongle is FM-1-transporter by kurogedelic (MIT, https://github.com/kurogedelic/FM-1-transporter), built by
tools/dongle/build.sh with tools/dongle/transporter-optimist.patch: an RP2040 board (Seeed XIAO RP2040, Raspberry
Pi Pico) on the FM-1's D+, D- and GND. It keys the chip's mask ROM (USB_KEY 0x16EF), feeds the ROM its 1 ms
pulses and then is the USB host of UBOOT itself, so nothing has to be replugged within the few seconds the ROM
waits. The Mac talks to the dongle over its serial data port (the one of its two CDC ports that answers `ping`
with `PONG`) with the transporter's line protocol (its docs/PROTOCOL.md and src/transporter_proto.c):

  status                       -> OK uboot=0|1 v15=0|1 loader=0|1 loader_running=0|1 vendor=WL82 product=UBOOT1.00
                                  (vendor and product: the patch; the chip's INQUIRY answer, "-" before it)
  ldr <off> <len> <crc32> + bytes  the flash loader, 4 KiB at a time; the dongle starts it at the first command
                                  that needs it
  info                         -> OK key=980F type=3 id=856014
  read <addr> <len>            -> DATA <len>, the bytes, END <crc32>  (END FAIL: a read failed, the bytes are padding)
  wsec <addr> <crc32> + 4096 bytes  erase, write and read back one sector -> OK | ERR range | ERR crc | ERR write
  runapp                       leave UBOOT, boot the installed firmware
  rekey                        reboot the dongle into keying (the port goes away and comes back)
  uboot                        the USB-MIDI soft key, when stock V15 runs on the FM-1

Dongle has the calls fm1_rescue.rescue() makes on fm1_rescue.Uboot, so the package checks, the backup, the write
policy and the final read are the same code; only who holds the USB bus differs. No sudo and no libusb: pyserial
(tools/builder/venv has it).
"""
import glob
import time
import zlib

from fm1_rescue import CLEAR_END, SECTOR, WRITE_MIN, Fail

BAUD = 115200
PORT_GLOB = "/dev/cu.usbmodem*"
READ_CHUNK = 65536
PORT_WAIT = 10          # s for the dongle to answer ping (it is plugged in)
REKEY_WAIT = 20         # s for the port to come back after rekey
POLL = 0.5              # s between status polls while waiting for update mode


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


class Dongle:
    """fm1_rescue.Uboot's calls, over the dongle's data port: pyserial's Serial, or anything with write(), read(n),
    readline(), reset_input_buffer(), close() and a timeout attribute"""

    def __init__(self, port):
        self.port = port

    def close(self):
        self.port.close()

    # --------------------------------------------------------------- lines ---
    def _line(self, text, timeout=30):
        p = self.port
        p.reset_input_buffer()
        p.write(text.encode() + b"\n")
        p.timeout = timeout
        reply = p.readline().decode(errors="replace").strip()
        if not reply:
            raise Fail(f"dongle: no answer to {text.split()[0]!r}")
        return reply

    def _ok(self, text, timeout=30):
        """an OK line, its key=value fields as a dict"""
        reply = self._line(text, timeout)
        if not reply.startswith("OK"):
            raise Fail(f"dongle: {text.split()[0]} failed ({reply})")
        return dict(kv.split("=", 1) for kv in reply.split()[1:] if "=" in kv)

    def _payload(self, head, data, timeout):
        """a command line ending in the crc32 of the raw bytes that follow it"""
        p = self.port
        p.reset_input_buffer()
        p.write(f"{head} {crc32(data):08X}\n".encode() + data)
        p.timeout = timeout
        reply = p.readline().decode(errors="replace").strip()
        if reply != "OK":
            raise Fail(f"dongle: {head.split()[0]} failed ({reply or 'no answer'})")

    def status(self):
        return self._ok("status", 5)

    # ------------------------------------------------------- Uboot's calls ---
    def inquiry(self):
        st = self.status()
        if st.get("uboot") != "1":
            raise Fail("dongle: the FM-1 is not in update mode on the dongle")
        return st.get("vendor", "-"), st.get("product", "-")

    def start_loader(self, blob):
        for off in range(0, len(blob), SECTOR):
            chunk = blob[off:off + SECTOR]
            self._payload(f"ldr {off} {len(chunk)}", chunk, 5)
        # the dongle writes it to the chip's RAM and jumps at the first command that needs it

    def chip(self):
        f = self._ok("info", 30)
        try:
            return int(f["key"], 16), int(f["type"]), int(f["id"], 16)
        except (KeyError, ValueError):
            raise Fail(f"dongle: unexpected info answer ({f})")

    def read(self, addr, n):
        reply = self._line(f"read {addr:#x} {n}", 30)
        if not reply.startswith("DATA "):
            raise Fail(f"dongle: read {addr:#x} failed ({reply})")
        want = int(reply.split()[1])
        if want != n:
            raise Fail(f"dongle: read {addr:#x} announces {want} bytes, {n} asked")
        data = bytearray()
        self.port.timeout = 10
        while len(data) < want:
            chunk = self.port.read(min(READ_CHUNK, want - len(data)))
            if not chunk:
                raise Fail(f"dongle: read stalled at {len(data):#x} of {want:#x}")
            data += chunk
        end = self.port.readline().decode(errors="replace").strip()
        if end != f"END {crc32(data):08X}":
            raise Fail(f"dongle: read check failed ({end}, computed {crc32(data):08X})")
        return bytes(data)

    def write_sector(self, addr, data):
        if addr < WRITE_MIN or addr >= CLEAR_END or addr % SECTOR or len(data) != SECTOR:
            raise Fail(f"refusing to write {addr:#x}")
        self._payload(f"wsec {addr:#x}", data, 20)            # the dongle erases, writes and reads it back

    def cmd(self, cmd, args=b""):
        """rescue's keep-alive heartbeat; the dongle itself keeps the loader alive while idle"""
        self.status()
        return b""

    def run_app(self):
        try:
            self._line("runapp", 5)
        except Fail:
            pass                                             # it may reset before it answers

    def rekey(self):
        """reboot the dongle into keying; its port goes away and comes back"""
        try:
            self._line("rekey", 5)
        except Fail:
            pass
        self.close()


# ------------------------------------------------------------------ port ---
def find_port(serial, explicit=None, settle=0.3):
    """the dongle's data port: the one that answers ping with PONG (its other CDC port is the log console)"""
    for path in [explicit] if explicit else sorted(glob.glob(PORT_GLOB)):
        try:
            s = serial.Serial(path, BAUD, timeout=settle)
        except (serial.SerialException, OSError):
            continue
        try:
            s.reset_input_buffer()
            s.write(b"ping\n")
            buf, deadline = b"", time.time() + 1.0
            while time.time() < deadline and b"PONG" not in buf:
                buf += s.read(64)
            if b"PONG" in buf:
                s.timeout = 5
                return s
        except (serial.SerialException, OSError):
            pass
        s.close()
    return None


def wait_port(serial, explicit, seconds, sleep=time.sleep):
    t0 = time.time()
    while True:
        s = find_port(serial, explicit)
        if s is not None or time.time() - t0 >= seconds:
            return s
        sleep(POLL)


def open_dongle(port_path, wait, say=print, serial=None, sleep=time.sleep):
    """Find the dongle and get the FM-1 into update mode through it: already there, nothing to do; stock V15
    running, the soft key; anything else, the dongle re-keys and the user switches the FM-1 off and on.
    Returns a Dongle for rescue()."""
    if serial is None:
        try:
            import serial
        except ImportError:
            raise Fail("pyserial is missing: tools/builder/venv/bin/python has it, or pip install pyserial")
    s = wait_port(serial, port_path, PORT_WAIT, sleep)
    if s is None:
        raise Fail(f"no dongle answered on {port_path or PORT_GLOB}: plug the RP2040 dongle into the Mac "
                   "(tools/dongle/README.md)")
    d = Dongle(s)
    st = d.status()
    if st.get("uboot") == "1":
        say("  the FM-1 is in update mode on the dongle")
        return d
    if st.get("v15") == "1":
        say("  stock V15 runs on the FM-1: the dongle sends it the soft key ...")
        d._line("uboot", 5)
    else:
        say("  the dongle restarts its keying. Switch the FM-1 OFF, then ON ...")
        d.rekey()
        s = wait_port(serial, port_path, REKEY_WAIT, sleep)
        if s is None:
            raise Fail("the dongle did not come back after rekey: unplug it and plug it in again")
        d = Dongle(s)
    t0 = time.time()
    while time.time() - t0 < wait:
        if d.status().get("uboot") == "1":
            say(f"  update mode after {time.time() - t0:.1f} s")
            return d
        sleep(POLL)
    d.close()
    raise Fail("the FM-1 did not enter update mode through the dongle: is it switched on, and wired as "
               "tools/dongle/README.md shows?")

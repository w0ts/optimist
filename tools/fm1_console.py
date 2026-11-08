#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Send read-only commands to a running FM-1's console and print the replies (a build with the console, the
builder's USB_CONSOLE; firmware/src/io/console.c, the release's fm1base/optimist/ui/opt_console.h).

  tools/builder/venv/bin/python tools/fm1_console.py status dbg [--port PORT]
  tools/builder/venv/bin/python tools/fm1_console.py --midi status reboot [--port FM1-620C]

Two ways in: the serial port (FELUCCA_CDC, a build without USB audio), or --midi: SysEx on the USB MIDI port
(FELUCCA_CON_MIDI, every console build, with USB audio too): a line F0 7D 46 43 'l' <ASCII> F7, the reply in
F0 7D 46 43 'o' <ASCII> F7 frames, then F0 7D 46 43 'e' F7. Without --midi and with no serial port found, MIDI is tried.
Only the commands in READ_ONLY (and `reboot`) are sent; anything else is refused before the port is opened.
Needs pyserial (serial) or python-rtmidi (--midi), both in the builder's venv (tools/requirements-flash.txt).
"""
import argparse
import re
import sys
import time

READ_ONLY = ("status", "dbg", "boot", "crash", "help", "keys", "keylog", "bletrim", "blevm",
             "tabbench", "membench", "far",
             "sd", "sd pins", "sd pins 7", "sd pins 8", "sd pins 9", "sd pins 10",
             "sd bench", "sd stream", "sd rec", "sd rec 5000", "sd rec stop", "sd diag", "sd speed", "sd index",
             "wifiup", "wifiscan", "wifidata")   # (sd bench, sd rec: they read and write the
                                                     # SD card only (a bench file, a take in Optimist/Record), never the FM-1's flash)   # (sd pins: the LCD's lines held one at a
                                                     # time for a multimeter; GPIO only, nothing written)   # (far: the far probe reads a constant and calls a function past 0x93000)
                                                     # (wifiup brings the Wi-Fi side up; wifidata listens after wifijoin)
                                                     # (sd diag, sd speed: the card started again and read, nothing written)
ALSO = ("reboot", "reboot cut")          # (not a read, but no write either: the FM-1 restarts, its project saved first; cut: not saved, tools/fm1_autosave_test.py)
FM1_VID = 0x1209                         # usb.c
QUIET_S = 0.6                            # a reply is over after this long without a byte
SX_HDR = (0x7D, 0x46, 0x43)              # the console's SysEx (the editor's is 7D 46 4C)
SX_WAIT_S = 5.0                          # a command's reply over MIDI: its end frame within this


def serial_ports() -> list[str]:
    from serial.tools import list_ports
    return [p.device for p in list_ports.comports() if p.vid == FM1_VID]


class MidiConsole:
    """the console as SysEx on the FM-1's USB MIDI port"""

    def __init__(self, want: str | None) -> None:
        try:
            import rtmidi
        except ImportError:
            raise SystemExit("fm1_console: --midi needs python-rtmidi (tools/builder/venv/bin/pip install python-rtmidi)")
        self.mi, self.mo = rtmidi.MidiIn(queue_size_limit=8192), rtmidi.MidiOut()
        ni, no = self.mi.get_ports(), self.mo.get_ports()
        port_re = re.compile(r"FM1-[0-9A-F]{4}|FM-1|FM1(?![\w-])|optimist|felucca", re.I)   # (tools/fm1_mirror.py PORT_RE)
        ok = [k for k, n in enumerate(ni) if port_re.search(n) and "bluetooth" not in n.lower()]
        pick = [k for k in ok if want is None or want.lower() in ni[k].lower()]
        pout = [k for k, n in enumerate(no) if pick and n == ni[pick[0]]]
        if not pick or not pout:
            raise SystemExit("fm1_console: no FM-1 MIDI port" + (f" holding {want!r}" if want else "") + ": "
                             + (", ".join(ni) or "no MIDI ports"))
        if len(pick) > 1:
            raise SystemExit("fm1_console: more than one FM-1, pick one with --port: " + ", ".join(ni[k] for k in pick))
        self.name = ni[pick[0]]
        self.frames: list[list[int]] = []
        self.mi.ignore_types(sysex=False, timing=True, active_sense=True)
        self.mi.set_callback(self._take)
        self.mi.open_port(pick[0])
        self.mo.open_port(pout[0])

    def _take(self, ev, _data) -> None:
        m = [b for b in ev[0] if b < 0xF8 or b == 0xF7]          # (the clock may sit inside a SysEx)
        if len(m) >= 6 and m[0] == 0xF0 and tuple(m[1:4]) == SX_HDR:
            self.frames.append(m)

    def ask(self, cmd: str) -> str:
        self.frames.clear()
        self.mo.send_message([0xF0, *SX_HDR, ord("l"), *(b & 0x7F for b in cmd.encode("ascii", "replace")), 0xF7])
        out, t0 = [], time.time()
        while time.time() - t0 < SX_WAIT_S:
            while self.frames:
                f = self.frames.pop(0)
                if f[4] == ord("e"):
                    return bytes(out).decode("ascii", "replace")
                if f[4] == ord("o"):
                    out += f[5:-1]
                    t0 = time.time()
            time.sleep(0.01)
        return bytes(out).decode("ascii", "replace") + "\n(fm1_console: no end of the reply: a build without the console over MIDI?)"

    def close(self) -> None:
        self.mi.close_port()
        self.mo.close_port()


def find_port():
    ports = serial_ports()
    if not ports:
        raise SystemExit("fm1_console: no FM-1 serial console found (USB connected? a build with FELUCCA_CDC=1)")
    return ports[0]


def ask(port, cmd):
    port.write(cmd.encode() + b"\r")
    out, last = b"", time.time()
    while time.time() - last < QUIET_S:
        chunk = port.read(4096)
        if chunk:
            out, last = out + chunk, time.time()
    return out.decode("ascii", "replace")


def main(argv=None):
    ap = argparse.ArgumentParser(description="read-only FM-1 console commands")
    ap.add_argument("commands", nargs="+", help="one of: " + ", ".join(READ_ONLY + ALSO) + " (quote the ones with a space: 'sd pins')")
    ap.add_argument("--port", help="serial: the console's port (default: the first USB device 0x1209); --midi: a part of the "
                    "FM-1's MIDI port name (FM1-620C)")
    ap.add_argument("--midi", action="store_true", help="over USB MIDI (SysEx): works beside USB audio")
    a = ap.parse_args(argv)
    refused = [c for c in a.commands if c not in READ_ONLY + ALSO and not c.startswith(("sd ls", "sd cat ", "sd diag", "sd stats", "sd irq", "sd clock "))]   # (reads; sd irq N picks the reader, sd clock N remounts at N)
    if refused:
        raise SystemExit("fm1_console: refused (not read-only): " + ", ".join(refused))
    if a.midi or (not a.port and not _have_serial_port()):
        con = MidiConsole(a.port if a.midi else None)
        try:
            for cmd in a.commands:
                print(con.ask(cmd))
        finally:
            con.close()
        return 0
    try:
        import serial
    except ImportError:
        raise SystemExit("fm1_console: needs pyserial (tools/builder/venv/bin/pip install -r tools/requirements-flash.txt)")
    with serial.Serial(a.port or find_port(), 115200, timeout=0.05) as port:
        ask(port, "")                    # wake the prompt, drop whatever was pending
        for cmd in a.commands:
            print(ask(port, cmd))
    return 0


def _have_serial_port() -> bool:
    try:
        return bool(serial_ports())
    except ImportError:
        return False


if __name__ == "__main__":
    sys.exit(main())

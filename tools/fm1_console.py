#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Send read-only commands to a running FM-1's serial console and print the replies (a build with the CDC
console, FELUCCA_CDC=1; firmware/src/io/console.c).

  tools/builder/venv/bin/python tools/fm1_console.py status dbg blell [--port PORT]

Only the commands in READ_ONLY are sent; anything else is refused before the port is opened.
Needs pyserial (tools/requirements-flash.txt, in the builder's venv).
"""
import argparse
import sys
import time

READ_ONLY = ("status", "dbg", "boot", "crash", "help", "bletrim", "blevm", "blell", "blell clear")
FM1_VID = 0x1209                         # usb.c
QUIET_S = 0.6                            # a reply is over after this long without a byte


def find_port():
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == FM1_VID]
    if not ports:
        raise SystemExit("fm1_console: no FM-1 serial console found (USB connected? a build with FELUCCA_CDC=1)")
    return ports[0].device


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
    ap.add_argument("commands", nargs="+", help="one of: " + ", ".join(READ_ONLY) + " (quote 'blell clear')")
    ap.add_argument("--port", help="the console's serial port (default: the first USB device 0x1209)")
    a = ap.parse_args(argv)
    refused = [c for c in a.commands if c not in READ_ONLY]
    if refused:
        raise SystemExit("fm1_console: refused (not read-only): " + ", ".join(refused))
    try:
        import serial
    except ImportError:
        raise SystemExit("fm1_console: needs pyserial (tools/builder/venv/bin/pip install -r tools/requirements-flash.txt)")
    with serial.Serial(a.port or find_port(), 115200, timeout=0.05) as port:
        ask(port, "")                    # wake the prompt, drop whatever was pending
        for cmd in a.commands:
            print(ask(port, cmd))
    return 0


if __name__ == "__main__":
    sys.exit(main())

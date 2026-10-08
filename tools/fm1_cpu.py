#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# From isod89/sloop-fm1 PR #45 (tools/fm1_cpu.py, d23e326) by Erick Buendia Barrientos (Erbubar23);
# adapted for Optimist: the CPU guard's keys, the parser apart (tests/fm1_cpu_test.py), `optimist.py cpu`.
"""Measure the audio CPU load of a running FM-1 through its serial console (a build with the CDC console,
FELUCCA_CDC=1; firmware/src/io/console.c `status`, read-only).

  python tools/optimist.py cpu [SECONDS] [--port PORT] [--csv FILE] [--label TEXT]
  python tools/fm1_cpu.py      (the same)

Samples `status` about twice a second and prints, over the samples taken while playing: cpu_pct (the
firmware's smoothed load, song.cpu_q8) min / mean / max; the worst audio half since boot (audio_max_us,
against the 5805 us a 256-frame half lasts at 44.1 kHz; the firmware sheds a voice above 85 % of it: audio.c,
and cpuguard.c's CG_CEIL with the CPU guard); voices shed and given up, and with the CPU guard built its level
and the voices it shed (cpu_guard_shed). audio_max_us is a running maximum from boot: restart the FM-1 before
a measurement that must not include an earlier peak.
Needs pyserial (python3 -m pip install --user pyserial). Exit status 1 when a half went over 85 % or a voice
was shed.
"""
import argparse
import csv
import sys
import time

HALF_US = 256 * 1000000 / 44100          # one I2S half buffer (core.h HALF_FRAMES at FS)
SHED_PCT = 85                            # audio.c, cpuguard.c CG_CEIL (218 / 256): a half over 85 % sheds
FM1_VID = 0x1209                         # usb.c


def parse_status(text):
    """the console's answer to `status` -> {key: value}: one "key value" line each (the echo of the
    command, the prompt and blank lines are left out; a value keeps its spaces: clk, engine, preset)"""
    kv = {}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith(">"):
            line = line[1:].strip()
        parts = line.split(" ", 1)
        if len(parts) == 2 and parts[0] and parts[1].strip():
            kv[parts[0]] = parts[1].strip()
    return kv


def summary(rows):
    """rows: the samples ({key: value}, each with cpu_pct) -> (the report's lines, exit status)"""
    playing = [r for r in rows if r.get("playing") == "1"]
    cpu = [int(r["cpu_pct"]) for r in (playing or rows)]
    last = rows[-1]
    worst = int(last.get("audio_max_us", 0))
    pct = worst * 100 / HALF_US
    shed = int(last.get("voices_shed", 0))
    lines = [f"samples {len(rows)}, playing {len(playing)}",
             f"cpu_pct   min {min(cpu)}  mean {sum(cpu) / len(cpu):.1f}  max {max(cpu)}",
             f"worst half {worst} us = {pct:.0f} % of {HALF_US:.0f} us (shedding above {SHED_PCT} %)",
             f"voices shed {shed}, given up {last.get('voices_given_up', '?')}"]
    guard = 0
    if "cpu_guard_shed" in last:
        guard = int(last["cpu_guard_shed"])
        lines.append(f"CPU guard level {last.get('cpu_guard', '?')}, steps {last.get('cpu_guard_steps', '?')}, "
                     f"voices shed {guard}")
    return lines, 1 if pct > SHED_PCT or shed or guard else 0


def find_port():
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == FM1_VID]
    if not ports:
        raise SystemExit("fm1_cpu: no FM-1 serial console found (USB connected? a build with FELUCCA_CDC=1)")
    return ports[0].device


def status(port):
    port.reset_input_buffer()
    port.write(b"status\r")
    end, buf = time.time() + 1.0, b""
    while time.time() < end and not buf.rstrip().endswith(b">"):
        buf += port.read(port.in_waiting or 1)
    return parse_status(buf.decode("ascii", "replace"))


def main(argv=None):
    ap = argparse.ArgumentParser(prog="optimist.py cpu", description=__doc__.split("\n\n")[0])
    ap.add_argument("seconds", nargs="?", type=float, default=60)
    ap.add_argument("--port", help="the console's serial port (default: the first USB device 0x1209)")
    ap.add_argument("--csv", help="write every sample to this CSV file")
    ap.add_argument("--label", default="", help="a name for the run, printed with the firmware's version")
    a = ap.parse_args(argv)
    try:
        import serial
    except ImportError:
        raise SystemExit("fm1_cpu: needs pyserial (python3 -m pip install --user pyserial)")
    port = serial.Serial(a.port or find_port(), 115200, timeout=0.2)
    port.dtr = True                      # the console answers only with DTR (usb.c cdc.dtr)
    time.sleep(0.3)
    first = status(port)
    if "cpu_pct" not in first:
        raise SystemExit("fm1_cpu: the console did not answer `status`")
    print(f"{first.get('felucca', '')} {a.label}".strip() or "FM-1", f"on {port.port}; {a.seconds:.0f} s, play now")
    rows, t0 = [], time.time()
    while time.time() - t0 < a.seconds:
        kv = status(port)
        if "cpu_pct" in kv:
            rows.append({"t": round(time.time() - t0, 2), **kv})
        time.sleep(0.25)
    port.close()
    if not rows:
        raise SystemExit("fm1_cpu: no answer to `status` while measuring")
    if a.csv:
        keys = sorted({k for r in rows for k in r}, key=lambda k: (k != "t", k))
        with open(a.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, keys)
            w.writeheader()
            w.writerows(rows)
    lines, rc = summary(rows)
    print("\n".join(lines))
    return rc


if __name__ == "__main__":
    sys.exit(main())

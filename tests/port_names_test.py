#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 the Optimist contributors
"""The tools that find the FM-1 by its USB-MIDI port name (ruling 2026-10-29: the name is "FM1-XXXX", four upper-case hex digits;
a device on older firmware is still "Optimist"): tools/fm1_install.py, fm1_mirror.py, fm1_sample_upload.py, and the names
tools/flash.py hands fm1-base's installer. For each: the new name, the old name, and other gear that must not match.
Run by tests/run_tests.sh."""
import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
fails = 0


def check(ok, what):
    global fails
    fails += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} {what}")


NEW = ("FM1-1A2B", "FM1-1A2B MIDI 1", "FM1-ffff 1", "FM1-0000")
OLD = ("Optimist", "Optimist (Felucca)", "Felucca")
OTHER = ("Launchkey Mini MK3", "IAC Driver Bus 1", "SMK-25 II", "Arturia KeyStep", "FM1-12", "FM1-ZZZZ")

for mod in ("fm1_mirror", "fm1_sample_upload"):
    m = importlib.import_module(mod)
    check(all(m.PORT_RE.search(n) for n in NEW), f"{mod}: the new name FM1-XXXX is found")
    check(all(m.PORT_RE.search(n) for n in OLD), f"{mod}: the old names (Optimist, Felucca) are still found")
    check(not any(m.PORT_RE.search(n) for n in OTHER), f"{mod}: other gear is not")

inst = importlib.import_module("fm1_install")
check(all(inst.PORT_RE.search(n) for n in NEW + OLD + ("FM-1", "Melodee", "SLOOP 2.4")), "fm1_install: FM1-XXXX, Optimist and the family are probed")
check(not any(inst.PORT_RE.search(n) for n in ("Launchkey Mini MK3", "IAC Driver Bus 1", "SMK-25 II", "Arturia KeyStep")),
      "fm1_install: other gear is never probed")

flash = importlib.import_module("flash")
check(flash.FM1B_PORTS == ("FM1-", "Optimist"), "flash: fm1-base's tool is given FM1- first, then Optimist")
# the substring each is matched by (fm1_install Updater.candidates: the port name in the input name, any case)
check(all(any(p.lower() in n.lower() for p in flash.FM1B_PORTS) for n in ("FM1-1A2B", "Optimist")), "flash: both port names reach an FM-1")
check(not any(p.lower() in "Launchkey Mini MK3".lower() for p in flash.FM1B_PORTS), "flash: not other gear")

print("port names test FAILED" if fails else "port names test passed")
sys.exit(1 if fails else 0)

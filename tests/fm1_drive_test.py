#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_drive.py without a device: its steps parsed to the 108 PANEL ids of web/EDITOR_PROTOCOL.md, large turns cut
into requests of at most 63 detents, the FM-1s' ports picked by name (a second FM-1 by --port).
  python3 tests/fm1_drive_test.py"""
import importlib.util
import sys
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location("fm1_drive", Path(__file__).resolve().parent.parent / "tools" / "fm1_drive.py")
fd = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(fd)

fails = 0


def check(ok: bool, what: str) -> None:
    global fails
    print(f"fm1_drive: {what:72s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def bad(text: str) -> bool:
    try:
        fd.parse(text)
    except fd.StepError:
        return True
    return False


s = fd.parse("tap home; down seq; key 3; up SEQ; turn k2 -5; turn presets 3; master 1023; wait 200; shot a.png state; "
             "keydown 27; keyup 1; tap OCT+ 400; release; state")
check(s[0] == ("tap", 6, fd.TAP_MS), "tap HOME: button 6, the default hold")
check(s[1] == ("down", 9, fd.TAP_MS) and s[3][:2] == ("up", 9), "down / up SEQ: button 9, any case")
check(s[2] == ("key", 2, fd.TAP_MS), "key 3: note key id 2 (1 = F3 = id 0)")
check(s[4] == ("turn", 4, -5) and s[5] == ("turn", 2, 3), "turn K2 -5: knob role 4; PRESETS role 2")
check(s[6] == ("master", 1023), "master 1023")
check(s[7] == ("wait", 200), "wait 200")
check(s[8] == ("shot", "a.png", True), "shot FILE state")
check(s[9] == ("keydown", 26, fd.TAP_MS) and s[10][:2] == ("keyup", 0), "keydown 27 / keyup 1: ids 26 / 0")
check(s[11] == ("tap", 13, 400), "tap OCT+ 400: button 13 held 400 ms")
check(s[12] == ("release",) and s[13] == ("state",), "release, state")
check(fd.parse("tap sel")[0][1] == 1 and fd.parse("tap scl")[0][1] == 1, "SEL (and the mirror tool's SCL) is button 1")
check(fd.parse(" ; \n tap fx ;; ") == [("tap", 0, fd.TAP_MS)], "empty steps and new lines skipped")
check(all(bad(t) for t in ["tap BOGUS", "key 0", "key 28", "turn K5 1", "turn K1", "master 1024", "wait", "shot",
                           "jump 3", "key x"]), "bad steps refused before anything is sent")
check(fd.turns(5) == [69] and fd.turns(-1) == [63], "a turn: detents + 64")
check(fd.turns(100) == [127, 101] and fd.turns(-70) == [1, 57], "a large turn cut into parts of 63 at most")
check(fd.turns(0) == [], "no turn: no request")
names = ["IAC Bus 1", "FM1-7A3C", "FM1-7A3C Bluetooth", "FM1-11B0", "Optimist"]
check(fd.fm1_ports(names, None) == [1, 3, 4], "the FM-1s on USB (Bluetooth's port left out)")
check(fd.fm1_ports(names, "11b0") == [3], "--port picks the second FM-1 by a part of its name")
check(fd.fm1_ports(names, "nope") == [], "--port matching nothing: none")
sys.exit(1 if fails else 0)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 the Optimist contributors
"""tools/fm1_cpu.py without an FM-1: its parser on console text as firmware/src/console.c prints it (the echo,
CR LF, the clk line, the prompt), the keys it reads printed by the console, and the report's verdict.
Run by tests/run_tests.sh."""
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
fails = 0

# `status` on a build with the CPU guard (con_status: con_kv lines "key value\r\n", then the prompt)
CAPTURE = ("status\r\n"
           "felucca 0.1-dev\r\n"
           "uptime_ms 81234\r\n"
           "cpu_pct 61\r\n"
           "cpu_khz 192000\r\n"
           "clk 00000001 00000000 00000002 00000003 0000a0c0 00000000 00000000 00000000 00000000\r\n"
           "audio_max_us 5120\r\n"
           "voices_shed 0\r\n"
           "voices_given_up 2\r\n"
           "cpu_guard 1\r\n"
           "cpu_guard_steps 3\r\n"
           "cpu_guard_shed 0\r\n"
           "track 1\r\n"
           "batt_raw 3011\r\n"
           "engine ANALOG\r\n"
           "preset SUPER LEAD\r\n"
           "bpm 120\r\n"
           "playing 1\r\n"
           "> ")


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def main():
    spec = importlib.util.spec_from_file_location("fm1_cpu", ROOT / "tools" / "fm1_cpu.py")
    cpu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cpu)

    kv = cpu.parse_status(CAPTURE)
    check("parse: the numbers it reads", (kv["cpu_pct"], kv["audio_max_us"], kv["voices_shed"],
                                          kv["cpu_guard_shed"], kv["playing"]) == ("61", "5120", "0", "0", "1"))
    check("parse: values with spaces kept, the echo and the prompt left out",
          kv["preset"] == "SUPER LEAD" and kv["clk"].count(" ") == 8 and "status" not in kv and ">" not in kv)
    check("parse: no answer, nothing", cpu.parse_status("> ") == {} and cpu.parse_status("") == {})

    src = (ROOT / "firmware" / "src" / "console.c").read_text()
    printed = set(re.findall(r'con_kv\("(\w+)"', src))
    want = {"cpu_pct", "audio_max_us", "voices_shed", "voices_given_up", "playing", "cpu_guard", "cpu_guard_steps",
            "cpu_guard_shed"}
    check("console.c prints every key the tool reads", want <= printed)
    audio = (ROOT / "firmware" / "src" / "audio.c").read_text()
    guard = (ROOT / "firmware" / "src" / "cpuguard.c").read_text()
    ceil = re.search(r"#define CG_CEIL (\d+)u", guard)
    check(f"the shed threshold {cpu.SHED_PCT} %: audio.c's, and the CPU guard's CG_CEIL / 256",
          f"* {cpu.SHED_PCT}u" in audio and ceil is not None and round(int(ceil.group(1)) * 100 / 256) == cpu.SHED_PCT)

    rows = [dict(kv, playing="0", cpu_pct="10"), dict(kv), dict(kv, cpu_pct="70")]
    lines, rc = cpu.summary(rows)
    check("report: cpu_pct over the samples while playing", lines[1] == "cpu_pct   min 61  mean 65.5  max 70")
    check("report: worst half 5120 us = 88 %: over 85 %, status 1", "= 88 %" in lines[2] and rc == 1)
    calm = [dict(kv, audio_max_us="4000")]
    lines, rc = cpu.summary(calm)
    check("report: 69 %, nothing shed: status 0, the guard's line", rc == 0 and "= 69 %" in lines[2] and
          lines[-1] == "CPU guard level 1, steps 3, voices shed 0")
    check("report: a voice the guard shed: status 1", cpu.summary([dict(calm[0], cpu_guard_shed="2")])[1] == 1)
    check("report: a voice audio.c shed: status 1", cpu.summary([dict(calm[0], voices_shed="1")])[1] == 1)
    noguard = {k: v for k, v in calm[0].items() if not k.startswith("cpu_guard")}
    lines, rc = cpu.summary([noguard])
    check("report: no CPU guard built: no guard line", rc == 0 and not lines[-1].startswith("CPU guard"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

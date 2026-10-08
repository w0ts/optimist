# SPDX-License-Identifier: GPL-3.0-only
"""A clean audio start in the emulator, at several CPU clocks: no late half and every half within its 5805 us
(256 frames at 44.1 kHz) over the first seconds after boot; in a FELUCCA_DUAL build, CPU1 takes its jobs
(fm1_dual_mb done == req, both counting). Not part of make test (it needs the emulator):

  python3 tests/emu_boot_check.py FIRMWARE.fwsc [SECONDS] [MHZ ...]     (the ELF next to the package)

FM1_EMU: the emulator checkout (default ~/GitHub/fm1-emulator, branch feat/upstream-merge). Exit 1 on a failure.
Why: a DUAL=2 build's CPU1 slept in `idle; goto idle`, never took its wake interrupt, and CPU0 waited one whole
half for the first job (late 1, max_us 6491 at boot; hal/fm1_dual.h fm1_dual_sleep_ram)."""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HALF_US = 256 * 1000000 // 44100                        # 5804: one DMA half buffer
DBG = ("magic", "halves", "max_us", "nested", "in_audio", "late", "timer_irqs", "ui_frames", "last_us", "cpu_q8",
       "boots")


def play_check():
    emu = os.environ.get("FM1_EMU", os.path.expanduser("~/GitHub/fm1-emulator"))
    man = os.path.join(emu, "rust-emulator", "Cargo.toml")
    subprocess.run(["cargo", "build", "-q", "--release", "--example", "play_check", "--manifest-path", man], check=True)
    return os.path.join(emu, "rust-emulator", "target", "release", "examples", "play_check")


def peeks(out):
    """play_check's peek lines -> {symbol: [words]}"""
    return {m.group(1): [int(v) for v in m.group(2).split()]
            for m in re.finditer(r"^\s*(\S+) @ 0x[0-9a-f]+: ([\d ]+)$", out, re.M)}


def run(pc, fw, secs, mhz, dual):
    st = tempfile.mkdtemp(prefix="emu-boot-")
    try:
        steps = [f"run:{secs}", f"peek:felucca_dbg:{len(DBG)}"] + (["peek:fm1_dual_mb:2"] if dual else [])
        env = dict(os.environ, FM1_CPU_MHZ=str(mhz))
        p = subprocess.run([pc, "--state", st + "/", "--fresh", fw, *steps], env=env, capture_output=True, text=True)
    finally:
        shutil.rmtree(st, ignore_errors=True)
    if p.returncode:
        return [f"play_check failed ({p.returncode}): {(p.stdout + p.stderr).strip()[-400:]}"], ""
    v = peeks(p.stdout)
    if "felucca_dbg" not in v:
        return ["no felucca_dbg in play_check's output"], ""
    d = dict(zip(DBG, v["felucca_dbg"]))
    errs = []
    if d["halves"] < (secs - 1.0) * 150:                 # (audio starts 0.4 s .. 0.7 s after power-on)
        errs.append(f"audio not running: {d['halves']} halves in {secs} s")
    if d["late"]:
        errs.append(f"{d['late']} late half(s)")
    if d["max_us"] > HALF_US:
        errs.append(f"longest half {d['max_us']} us > {HALF_US} us")
    if d["boots"] != 1:
        errs.append(f"boots {d['boots']} (a reset)")
    info = f"halves {d['halves']}, late {d['late']}, max_us {d['max_us']}"
    if dual:
        req, done = v.get("fm1_dual_mb", [0, 0])
        if not (done and done >= req - 1):       # (a job may be in flight when the run stops)
            errs.append(f"CPU1 takes no jobs: req {req}, done {done}")
        info += f", CPU1 jobs {done} of {req}"
    return errs, info


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    fw = os.path.abspath(sys.argv[1])
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0
    clocks = [int(x) for x in sys.argv[3:]] or [48, 96, 192]
    elf = fw[:-5] + ".elf"
    if not os.path.exists(elf):
        sys.exit(f"no ELF next to the package: {elf}")
    dual = b"\0fm1_dual_mb\0" in open(elf, "rb").read()
    pc = play_check()
    bad = 0
    for mhz in clocks:
        errs, info = run(pc, fw, secs, mhz, dual)
        print(("FAIL" if errs else "ok  ") + f"  {mhz} MHz: " + ("; ".join(errs) + (" (" + info + ")" if info else "")
                                                               if errs else info))
        bad += bool(errs)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()

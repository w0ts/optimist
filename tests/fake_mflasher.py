#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A stand-in for M-Flasher's command line (docs/usage.md of third_party/m-flasher) for tests/anyflash_test.py: it
records every command line it is given and plays a unit in a folder instead of USB. No device is ever touched.

  FAKE_MF_DIR/flash.bin   the unit's 1 MiB flash
  FAKE_MF_DIR/state.json  {"download": bool, "model": "FM-1", "bootloader": "...", "firmware": "...", "unit": "...",
                           "fail": "firmware" (that command exits 1), "extra": 0x5000 (a sector added to its plans)}
  FAKE_MF_DIR/calls.jsonl one line per call: its arguments and what it read on stdin

Its plans follow M-Flasher's rules (src/ops.rs): firmware = the package's flash.bin [0x4000, its end), restore = the
whole image, bootloader = the boot area; only the 4 KiB sectors that differ; "Type yes" unless --yes; a full backup
saved in --backup-dir before the first write of the run, as Session::apply does."""
import json
import os
import sys
from pathlib import Path

D = Path(os.environ["FAKE_MF_DIR"])
sys.path.insert(0, os.environ["FAKE_MF_TOOLS"])
SECTOR, SIZE = 0x1000, 0x100000


def state():
    return json.loads((D / "state.json").read_text())


def save(st):
    (D / "state.json").write_text(json.dumps(st))


def log(args, stdin=""):
    with open(D / "calls.jsonl", "a") as f:
        f.write(json.dumps({"args": args, "stdin": stdin}) + "\n")


def ranges(secs):
    out = []
    for a in secs:
        if out and out[-1][1] == a:
            out[-1][1] = a + SECTOR
        else:
            out.append([a, a + SECTOR])
    return ", ".join(f"0x{a:05X}-0x{b - 1:05X}" for a, b in out)


def save_backup(flash, st, folder):
    out = Path(folder)
    out.mkdir(parents=True, exist_ok=True)
    n = len(list(out.glob("fm1-*.bin"))) + 1
    b = out / f"fm1-{n:03d}.bin"
    b.write_bytes(flash)
    b.with_suffix(".json").write_text(json.dumps({"unit id": st["unit"], "model": st["model"]}))
    print(f"Backup saved: {b}")


def apply(target, secs, yes, st, restart, folder):
    print("\nPlan")
    if not secs:
        print("Nothing to write: the flash already holds this.")
        if restart:
            st["download"] = False
            save(st)
        return 0, ""
    print(f"Writes {len(secs)} sectors ({len(secs) * 4} KiB): {ranges(secs)}")
    answer = ""
    if not yes:
        print('Type "yes" to write: ', end="", flush=True)
        answer = sys.stdin.readline()
        if answer.strip() != "yes":
            print("Nothing written.")
            return 0, answer
    cur = bytearray((D / "flash.bin").read_bytes())
    save_backup(bytes(cur), st, folder)
    for a in secs:
        cur[a:a + SECTOR] = target[a:a + SECTOR]
    (D / "flash.bin").write_bytes(bytes(cur))
    print(f"Done: everything written is verified ({len(secs)} sectors)")
    if restart:
        st["download"] = False
        save(st)
        print("Running: the FM-1 is on USB MIDI")
    if st.pop("wrote", None) == "boot":
        st["bootloader"] = "m-bootloader 1.4-8b171dd (fm1)"
        save(st)
    return 0, answer


def main(args):
    st = state()
    pos = [a for a in args if not a.startswith("--")]
    opts = {a for a in args if a.startswith("--")}
    cmd = pos[0] if pos else ""
    if st.get("fail") == cmd:
        log(args)
        print("Refused: the fake says no", file=sys.stderr)
        return 1
    if cmd == "find":
        log(args)
        print("/dev/fake0\tWL82 UBOOT1.00" if st["download"] else "midi\tFM-1 (running; download-mode switches it)")
        return 0
    if cmd == "download-mode":
        log(args)
        st["download"] = True
        save(st)
        print("In download mode: /dev/fake0")
        return 0
    if cmd == "restart":
        log(args)
        st["download"] = False
        save(st)
        return 0
    if not st["download"]:
        log(args)
        print("Refused: the unit is running, not in download mode", file=sys.stderr)
        return 1
    print("Connecting to /dev/fake0")
    for k in ("model", "bootloader", "firmware"):
        print(f"  {k:11} {st[k]}")
    print(f"  {'chip key':11} 0x980F")
    print(f"  {'unit id':11} {st['unit']}")
    flash = (D / "flash.bin").read_bytes()
    if cmd == "info":
        log(args)
        return 0
    if cmd == "backup":
        log(args)
        save_backup(flash, st, pos[1])
        st["unit"] = st.get("next_unit", st["unit"])     # (a test's other unit plugged in after the backup)
        save(st)
        return 0
    yes, restart = "--yes" in opts, "--restart" in opts
    if cmd == "firmware":
        import fm1_rescue
        pkg = fm1_rescue.package_flash_image(Path(pos[1]).read_bytes())
        target = flash[:0x4000] + pkg[0x4000:] + flash[len(pkg):]
        secs = [a for a in range(0x4000, len(pkg), SECTOR) if flash[a:a + SECTOR] != target[a:a + SECTOR]]
    elif cmd == "restore":
        target = Path(pos[1]).read_bytes()
        secs = [a for a in range(0, SIZE, SECTOR) if flash[a:a + SECTOR] != target[a:a + SECTOR]]
    elif cmd == "bootloader":
        target = bytes([0xB0]) * 0x1000 + flash[0x1000:]          # (a new boot area: its first sector here)
        secs = [0] if flash[:SECTOR] != target[:SECTOR] else []
        st["wrote"] = "boot"
    else:
        log(args)
        print(f"Refused: usage ({cmd})", file=sys.stderr)
        return 1
    if st.get("extra") is not None and st["extra"] not in secs:
        secs = sorted(secs + [st["extra"]])
    folder = args[args.index("--backup-dir") + 1] if "--backup-dir" in args else D / "default-backups"
    rc, answer = apply(target, secs, yes, st, restart, folder)
    log(args, answer)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

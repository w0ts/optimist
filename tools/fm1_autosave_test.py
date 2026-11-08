#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The autosave against a power cut, on an FM-1 over USB MIDI, nobody at it (the user, 2026-11-08: "can you auto test?").

  tools/builder/venv/bin/python tools/fm1_autosave_test.py --port FM1-620C

A cut is the console's `reboot cut` (fm1-base optimist/ui/opt_console.h): a restart with nothing saved first, as a power
cut leaves the flash (it does not cut in the middle of a flash write). A part's ATTACK (track 1, P_ATK) is changed over
the editor's TRACK_PARAM (web/EDITOR_PROTOCOL.md, cmd 31), then:
  0.         a plain reboot first: each case then starts from a power-on, whose first autosave AUTOSAVE_MIN_GAP (60 s
             between two saves) does not hold back; the cuts are power-ons too
  1. saved   the change left AUTOSAVE_SETTLE (10 s, storage/autosave.c) and more, then the cut: after the restart the
             change is there
  2. control another change, the cut 2 s after it (before the autosave): after the restart the value of case 1 is back,
             so the cut really saved nothing
The value the FM-1 had is put back at the end (and saved). Needs a build with the console (USB_CONSOLE) and the editor;
python-rtmidi. Never run on someone's FM-1 with work in it: the cut drops whatever was not saved yet.
"""
import argparse
import importlib.util
import sys
import time
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location("fm1_console", Path(__file__).resolve().parent / "fm1_console.py")
con = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(con)

ED_HDR = (0x7D, 0x46, 0x4C)                   # the editor's SysEx (web/EDITOR_PROTOCOL.md)
TRACK_PARAM = 31
P_ATK = 1                                     # firmware/src/core/core.h: P_LEVEL, P_ATK, ...
TRACK = 0
SETTLE_S = 15.0                               # autosave.c AUTOSAVE_SETTLE 10 s, and room
EARLY_S = 2.0                                 # well under it: the cut comes first
BACK_S = 40.0                                 # the restart: the port gone, then back


class Fm1:
    def __init__(self, want: str) -> None:
        import rtmidi
        self.rtmidi, self.want = rtmidi, want
        self.mi = self.mo = None
        self.frames: list[list[int]] = []

    def port(self):
        names = self.rtmidi.MidiIn().get_ports()
        hit = [k for k, n in enumerate(names) if self.want.lower() in n.lower() and "bluetooth" not in n.lower()]
        return (hit[0], names[hit[0]]) if len(hit) == 1 else (None, None)

    def open(self, wait_s: float = BACK_S) -> None:
        t0 = time.time()
        while time.time() - t0 < wait_s:
            k, name = self.port()
            if k is not None:
                self.mi, self.mo = self.rtmidi.MidiIn(queue_size_limit=8192), self.rtmidi.MidiOut()
                self.mi.ignore_types(sysex=False, timing=True, active_sense=True)
                self.mi.set_callback(lambda ev, _d: self.frames.append([b for b in ev[0] if b < 0xF8 or b == 0xF7]))
                self.mi.open_port(k)
                outs = self.mo.get_ports()
                self.mo.open_port(outs.index(name))
                time.sleep(0.5)
                return
            time.sleep(0.5)
        raise SystemExit(f"fm1_autosave_test: no MIDI port holding {self.want!r} after {wait_s:.0f} s")

    def close(self) -> None:
        for p in (self.mi, self.mo):
            if p:
                p.close_port()
        self.mi = self.mo = None

    def ask(self, cmd: int, args: list[int], wait_s: float = 2.0, tries: int = 5) -> list[int]:
        for _ in range(tries):                  # (just after a restart the first request can go unanswered)
            self.frames.clear()
            self.mo.send_message([0xF0, *ED_HDR, cmd, *args, 0xF7])
            t0 = time.time()
            while time.time() - t0 < wait_s:
                for f in list(self.frames):
                    if len(f) > 5 and f[0] == 0xF0 and tuple(f[1:4]) == ED_HDR and f[4] == cmd:
                        return f[5:-1]
                time.sleep(0.01)
        raise SystemExit(f"fm1_autosave_test: no reply to editor cmd {cmd} (a build without the editor?)")

    def get(self) -> int:
        r = self.ask(TRACK_PARAM, [TRACK, P_ATK & 0x7F, P_ATK >> 7])
        return (r[3] | r[4] << 7) - 8192

    def set(self, v: int) -> int:
        w = v + 8192
        r = self.ask(TRACK_PARAM, [TRACK, P_ATK & 0x7F, P_ATK >> 7, w & 0x7F, w >> 7])
        return (r[3] | r[4] << 7) - 8192

    def console(self, line: str) -> str:
        self.frames.clear()
        self.mo.send_message([0xF0, *con.SX_HDR, ord("l"), *line.encode("ascii"), 0xF7])
        t0, out = time.time(), []
        while time.time() - t0 < 3.0:
            for f in list(self.frames):
                if len(f) >= 6 and f[0] == 0xF0 and tuple(f[1:4]) == con.SX_HDR and f[4] == ord("o"):
                    out += f[5:-1]
                    self.frames.remove(f)
            if b"rebooting" in bytes(out):
                return bytes(out).decode("ascii", "replace")
            time.sleep(0.01)
        return bytes(out).decode("ascii", "replace")

    def cut(self, line: str = "reboot cut", says: str = "nothing saved") -> None:
        said = self.console(line)
        if says not in said:
            raise SystemExit(f"fm1_autosave_test: the console did not restart ({said.strip()!r}): a build without `{line}`?")
        self.close()
        t0 = time.time()
        while time.time() - t0 < 10.0 and self.port()[0] is not None:
            time.sleep(0.2)                     # (the port goes, then comes back)
        self.open()
        time.sleep(3.0)                         # (the project loaded)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="the autosave against a power cut (console's reboot cut), on an FM-1")
    ap.add_argument("--port", required=True, help="a part of the FM-1's MIDI port name (FM1-620C)")
    a = ap.parse_args(argv)
    fm = Fm1(a.port)
    fm.open(5.0)
    print("a restart first (reboot: the project saved, then a fresh power-on: its first autosave is not held back by "
          "AUTOSAVE_MIN_GAP, 60 s between two saves, storage/autosave.c)", flush=True)
    fm.cut("reboot", "rebooting")
    v0 = fm.get()
    print(f"track {TRACK + 1} ATTACK now {v0}")
    ok = True
    v1 = (v0 + 23) % 100
    print(f"1. saved: ATTACK {fm.set(v1)}, {SETTLE_S:.0f} s, cut ...", flush=True)
    time.sleep(SETTLE_S)
    fm.cut()
    got = fm.get()
    print(f"   after the restart: {got} ({'ok' if got == v1 else 'FAIL: the autosave did not keep it'})")
    ok &= got == v1
    v2 = (v1 + 31) % 100
    print(f"2. control: ATTACK {fm.set(v2)}, {EARLY_S:.0f} s, cut ...", flush=True)
    time.sleep(EARLY_S)
    fm.cut()
    got = fm.get()
    print(f"   after the restart: {got} ({'ok: the cut saved nothing' if got == v1 else 'FAIL: expected ' + str(v1)})")
    ok &= got == v1
    fm.set(v0)
    time.sleep(SETTLE_S)
    print(f"ATTACK back to {fm.get()} (saved by the autosave)")
    fm.close()
    print("autosave test passed" if ok else "autosave test FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

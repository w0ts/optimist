#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drive an FM-1's panel over USB-MIDI and look at its screen (108 PANEL and 106 MIRROR, web/EDITOR_PROTOCOL.md): buttons,
note keys, knobs and MASTER pressed as a finger would, the screen as a PNG between them. For tests run from the Mac.
  fm1_drive.py ports                                  the FM-1s on USB (their port names: FM1-XXXX)
  fm1_drive.py [--port NAME] run "STEP; STEP; ..."    the steps in order, in one mirror session
  fm1_drive.py [--port NAME] shot OUT.png [state]     the screen now (state: what the panel holds and lights, in words)
--port: a part of the port's name (FM1-7A3C, or 7A3C), needed when more than one FM-1 is on USB.
Steps (names in any case):
  tap BTN [MS]        press and let go (MS held, default 80)       BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC
  down BTN / up BTN   hold / let go (a chord: down SEQ; key 3; up SEQ)                               OCT- OCT+
  key N [MS]          a note key tapped, N 1..27 (1 = F3, 27 = G5)
  keydown N / keyup N a note key held / let go
  turn KNOB N         N detents (+ clockwise, - counter)           KNOB: SELECT ALGO PRESETS K1 K2 K3 K4
  master V            MASTER at V (0..1023) instead of its pot, until the session ends
  release             every key the driver holds up, MASTER back to its pot
  wait MS             pause
  shot FILE [state]   the screen as a PNG (and the panel's state in words)
  state               the panel's state in words
Everything held comes up when the session ends (the FM-1 lets go 3 s after the last request too): a hold spans one run.
The FM-1 needs a firmware with ED_MIRROR (opti_panel_1); MASTER needs opti_panelx_1. Needs python-rtmidi.
"""
import importlib.util
import sys
import time
from collections import deque
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location("fm1_mirror", Path(__file__).resolve().parent / "fm1_mirror.py")
fm = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(fm)

PANEL, MIRROR, HDR = fm.PANEL, fm.MIRROR, fm.HDR
BUTTON_IDS = {"FX": 0, "SEL": 1, "SCL": 1, "SELECT": 1, "ENV": 2, "LFO": 3, "EDIT": 4, "GLO": 5, "HOME": 6, "SAVE": 7,
              "ARP": 8, "SEQ": 9, "PLAY": 10, "REC": 11, "OCT-": 12, "OCT+": 13}
KNOB_IDS = {"SELECT": 0, "ALGO": 1, "ALGORITHM": 1, "PRESETS": 2, "K1": 3, "K2": 4, "K3": 5, "K4": 6}
TAP_MS = 80
ALIVE_S = 1.0            # the mirror asked again this often (it ends 3 s after the last request, every held key with it)
REPLY_S = 1.0
NKEYS = 27


class StepError(ValueError):
    pass


def _int(s: str, what: str) -> int:
    try:
        return int(s)
    except ValueError:
        raise StepError(f"{what}: a number, not {s!r}") from None


def parse(text: str) -> list[tuple]:
    """the steps of a run as tuples (op, args...); StepError on the first bad one"""
    out = []
    for raw in text.replace("\n", ";").split(";"):
        w = raw.split()
        if not w:
            continue
        op, a = w[0].lower(), w[1:]
        if op in ("tap", "down", "up"):
            if not a or a[0].upper() not in BUTTON_IDS:
                raise StepError(f"{raw.strip()!r}: a button ({' '.join(n for n in BUTTON_IDS if n not in ('SCL', 'SELECT'))})")
            ms = _int(a[1], "hold time") if op == "tap" and len(a) > 1 else TAP_MS
            out.append((op, BUTTON_IDS[a[0].upper()], ms))
        elif op in ("key", "keydown", "keyup"):
            n = _int(a[0], "key") if a else 0
            if not 1 <= n <= NKEYS:
                raise StepError(f"{raw.strip()!r}: a key 1..{NKEYS}")
            ms = _int(a[1], "hold time") if op == "key" and len(a) > 1 else TAP_MS
            out.append((op, n - 1, ms))
        elif op == "turn":
            if len(a) != 2 or a[0].upper() not in KNOB_IDS:
                raise StepError(f"{raw.strip()!r}: turn KNOB N (KNOB: {' '.join(KNOB_IDS)})")
            out.append((op, KNOB_IDS[a[0].upper()], _int(a[1], "detents")))
        elif op == "master":
            v = _int(a[0], "level") if a else -1
            if not 0 <= v <= 1023:
                raise StepError(f"{raw.strip()!r}: master 0..1023")
            out.append((op, v))
        elif op in ("release", "state"):
            out.append((op,))
        elif op == "wait":
            out.append((op, _int(a[0], "wait") if a else -1))
            if out[-1][1] < 0:
                raise StepError(f"{raw.strip()!r}: wait MS")
        elif op == "shot":
            if not a:
                raise StepError(f"{raw.strip()!r}: shot FILE")
            out.append((op, a[0], len(a) > 1 and a[1].lower() == "state"))
        else:
            raise StepError(f"{raw.strip()!r}: no such step")
    return out


def turns(detents: int) -> list[int]:
    """a turn as 108 knob values (detents + 64, 1..127 each): a large one in parts"""
    out, left = [], detents
    while left:
        part = max(-63, min(63, left))
        out.append(part + 64)
        left -= part
    return out


def fm1_ports(names: list[str], want: str | None) -> list[int]:
    """the FM-1s' USB ports among `names` (Bluetooth's left out), only those whose name holds `want` when given"""
    return [k for k, n in enumerate(names) if fm.PORT_RE.search(n) and "bluetooth" not in n.lower()
            and (want is None or want.lower() in n.lower())]


class Link:
    """one FM-1 over USB-MIDI: requests and their answers, the mirror's pushes decoded as they come"""

    def __init__(self, want: str | None) -> None:
        try:
            import rtmidi
        except ImportError:
            sys.exit("needs python-rtmidi: pip install python-rtmidi")
        self.mi, self.mo = rtmidi.MidiIn(queue_size_limit=8192), rtmidi.MidiOut()
        ni, no = self.mi.get_ports(), self.mo.get_ports()
        pi, po = fm1_ports(ni, want), fm1_ports(no, want)
        if not pi or not po:
            sys.exit("no FM-1 on USB" + (f" whose port name holds {want!r}" if want else "") + ": " +
                     (", ".join(ni) or "no MIDI ports"))
        if len(pi) > 1 or len(po) > 1:
            sys.exit("more than one FM-1 on USB, pick one with --port: " + ", ".join(ni[k] for k in pi))
        self.name = ni[pi[0]]
        self.dec = fm.Decoder()
        self.pushes: deque = deque()
        self.replies: deque = deque()
        self.last_push = 0.0
        self.alive = 0.0
        self.seq = 0
        self.mi.ignore_types(sysex=False, timing=True, active_sense=True)
        self.mi.set_callback(self._take)
        self.mi.open_port(pi[0])
        self.mo.open_port(po[0])

    def _take(self, ev, _data) -> None:
        m = ev[0]                                  # (the FM-1's clock may sit inside a SysEx: taken out, as fm1_mirror.py does)
        if not m or m[0] != 0xF0:
            return
        f = [b for b in m if b < 0xF8 or b == 0xF7]
        if f[1:4] != HDR or len(f) < 6:
            return
        (self.pushes if f[4] == MIRROR else self.replies).append(f)

    def pump(self) -> None:
        """the pushes come in decoded; the mirror kept on"""
        while self.pushes:
            f = self.pushes.popleft()
            if self.dec.frame(f) and len(f) > 5 and f[5] in (0, 1, 2):
                self.last_push = time.monotonic()
        if time.monotonic() - self.alive > ALIVE_S:
            self.send(MIRROR, [1])

    def send(self, cmd: int, args: list[int]) -> None:
        self.mo.send_message([0xF0, *HDR, cmd, *args, 0xF7])
        self.alive = time.monotonic()

    def sleep(self, s: float) -> None:
        end = time.monotonic() + s
        while time.monotonic() < end:
            self.pump()
            time.sleep(0.003)

    def panel(self, kind: int, ident: int, value: int) -> bool:
        """a 108 request (with seq: sent again when unanswered, never done twice); True: done"""
        self.seq = (self.seq + 1) & 127
        msg = [kind, ident, value, self.seq]
        for _ in range(3):
            self.replies.clear()
            self.send(PANEL, msg)
            end = time.monotonic() + REPLY_S
            while time.monotonic() < end:
                self.pump()
                while self.replies:
                    f = self.replies.popleft()
                    if f[4] == PANEL and len(f) >= 8 and f[5] == kind:
                        return f[6] == 0
                time.sleep(0.002)
        raise SystemExit(f"no answer to 108 PANEL {msg} from {self.name} (a firmware with ED_MIRROR?)")

    def catch_up(self, want_state: bool) -> None:
        """the whole panel again (bit 1), taken once it is in (as fm1_mirror.py shot)"""
        fills, t0 = self.dec.fills, time.monotonic()
        if want_state:
            self.dec.state = None
        self.send(MIRROR, [3 | (4 if want_state else 0)])
        began_at = 0.0
        while True:
            self.pump()
            now = time.monotonic()
            if not began_at and self.dec.fills > fills:
                began_at = now
            if began_at and (now - began_at > fm.CATCH_S or now - self.last_push > fm.QUIET_S) \
                    and (not want_state or self.dec.state is not None):
                return
            if now - t0 > fm.WAIT_S:
                if not began_at:
                    raise SystemExit(f"no screen from {self.name} (a firmware with ED_MIRROR?)")
                return
            time.sleep(0.003)

    def close(self) -> None:
        try:
            self.panel(4, 0, 0)                    # (every key held up, MASTER the pot's: the end of the mirror does it too)
        finally:
            self.send(MIRROR, [0])
            self.mi.close_port()
            self.mo.close_port()


def run(link: Link, steps: list[tuple]) -> int:
    """the steps on the FM-1; the count of requests it refused"""
    refused = 0

    def p(kind: int, ident: int, value: int, what: str) -> None:
        nonlocal refused
        if not link.panel(kind, ident, value):
            refused += 1
            print(f"refused: {what}")

    link.catch_up(False)                           # (the mirror on and the panel whole before the first step)
    for st in steps:
        op = st[0]
        if op in ("tap", "down", "up"):
            name = next(n for n, v in BUTTON_IDS.items() if v == st[1])
            if op != "up":
                p(0, st[1], 1, f"{op} {name}")
            if op == "tap":
                link.sleep(st[2] / 1000)
            if op != "down":
                p(0, st[1], 0, f"{op} {name}")
        elif op in ("key", "keydown", "keyup"):
            if op != "keyup":
                p(1, st[1], 1, f"{op} {st[1] + 1}")
            if op == "key":
                link.sleep(st[2] / 1000)
            if op != "keydown":
                p(1, st[1], 0, f"{op} {st[1] + 1}")
        elif op == "turn":
            for v in turns(st[2]):
                p(2, st[1], v, f"turn {st[1]} {v - 64:+d}")
        elif op == "master":
            p(3, st[1] & 127, st[1] >> 7, f"master {st[1]}")
        elif op == "release":
            p(4, 0, 0, "release")
        elif op == "wait":
            link.sleep(st[1] / 1000)
        elif op in ("shot", "state"):
            want_state = op == "state" or st[2]
            link.sleep(0.05)                       # (the step's redraw under way before the catch-up is asked)
            link.catch_up(want_state)
            if op == "shot":
                fm.write_png(Path(st[1]), link.dec.fb)
                print(f"wrote {st[1]}")
            if want_state:
                print(fm.state_text(link.dec.state) if link.dec.state is not None else "STATE: not sent")
    return refused


def main() -> None:
    a = sys.argv[1:]
    want = None
    if "--port" in a:
        k = a.index("--port")
        if k + 1 >= len(a):
            sys.exit(__doc__)
        want = a[k + 1]
        a = a[:k] + a[k + 2:]
    if a == ["ports"]:
        try:
            import rtmidi
        except ImportError:
            sys.exit("needs python-rtmidi: pip install python-rtmidi")
        names = rtmidi.MidiIn().get_ports()
        found = [names[k] for k in fm1_ports(names, want)]
        print("\n".join(found) if found else "no FM-1 on USB")
        return
    if len(a) == 2 and a[0] == "run":
        text = a[1]
    elif len(a) in (2, 3) and a[0] == "shot":
        text = "shot " + " ".join(a[1:])
    else:
        sys.exit(__doc__)
    try:
        steps = parse(text)
    except StepError as e:
        sys.exit(f"step {e}")
    link = Link(want)
    print(f"driving {link.name}")
    try:
        refused = run(link, steps)
    finally:
        link.close()
    sys.exit(1 if refused else 0)


if __name__ == "__main__":
    main()

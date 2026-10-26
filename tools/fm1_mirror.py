#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1's screen as a PNG, over USB-MIDI (the screen mirrored: web/EDITOR_PROTOCOL.md "The screen mirrored", cmd 106;
after fm1-base's tools/fm1_mirror.py).
  fm1_mirror.py shot OUT.png [--state]     the screen now (and, with --state, what the panel holds and lights)
  fm1_mirror.py decode FRAMES.sysex OUT.png   frames saved before (tests/mirror_test.c MIRROR_FIXTURE): no device
  fm1_mirror.py session [SECONDS]           the test session: the FM-1 restarts as a blank one, its flash never written,
                                            for SECONDS (or until Ctrl-C), then restarts into its own state (USB only)
The device sends the whole panel when asked (a catch-up: black, then every band again); the shot is taken once the catch-up is
in (half a second, or the pushes quiet). The decoder is tests/mirror_dec.h's. Needs python-rtmidi for a device (not mido: its
frames broke where the FM-1's clock sits inside a SysEx); the PNG is written with zlib alone.
"""
import re
import struct
import sys
import time
import zlib
from collections import deque
from pathlib import Path

PORT_RE = re.compile(r"optimist|felucca", re.I)
HDR = [0x7D, 0x46, 0x4C]
MIRROR = 106
W = H = 240
QUIET_S = 0.3            # the pushes quiet this long after the catch-up began: the screen is whole
WAIT_S = 5.0             # no catch-up within this: give up
ASK_S = 0.7              # the request again this often until the catch-up begins
CATCH_S = 0.5            # or this long after it began (a catch-up over USB comes within tens of ms)
BUTTONS = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"]
KNOBS = ["SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"]


class Decoder:
    """the client's side of 106: FILL, BLIT, MORE applied to fb (RGB565, row by row); STATE kept"""

    def __init__(self) -> None:
        self.fb = [0] * (W * H)
        self.rows = [False] * H         # the rows a write covered since the last whole-panel fill (a catch-up's progress)
        self.x = self.y = self.w = self.h = self.at = 0
        self.slot = [0] * 7
        self.rr = 1
        self.state: list[int] | None = None
        self.fills = 0

    def _px(self, c: int, n: int) -> None:
        while n > 0 and self.at < self.w * self.h:
            x, y = self.x + self.at % self.w, self.y + self.at // self.w
            if x < W and y < H:
                self.fb[y * W + x] = c
                self.rows[y] = True
            self.at += 1
            n -= 1

    def frame(self, f: list[int]) -> bool:
        """one SysEx frame (F0 .. F7); False: not a mirror frame, or a bad one"""
        if len(f) < 7 or f[:5] != [0xF0, *HDR, MIRROR] or f[-1] != 0xF7:
            return False
        op, i, n = f[5], 6, len(f) - 1

        def v2(k: int) -> int:
            return f[k] | f[k + 1] << 7

        if op == 4:
            self.state = f[6:n]
            return True
        if len(f) == 12 and op <= 1 and f[6] >= 1:
            return True             # (the answer to the request: on, version, w, h; tests/mirror_test.c host_byte tells it so)
        if op <= 1:
            if i + 8 > n:
                return False
            self.x, self.y, self.w, self.h, self.at = v2(i), v2(i + 2), v2(i + 4), v2(i + 6), 0
            i += 8
        if op == 0:
            if i + 3 > n:
                return False
            self._px(v2(i) | f[i + 2] << 14, self.w * self.h)
            if self.x == 0 and self.y == 0 and self.w == W and self.h == H:
                self.fills += 1
                self.rows = [False] * H     # (a catch-up begins: black, then every band again)
            return True
        if op == 1:
            self.slot, self.rr = [0] * 7, 1
        elif op != 2:
            return op == 3          # (LINK: Bluetooth's, nothing drawn)
        while i < n:
            t = f[i]
            i += 1
            code, s = t >> 3, t & 7
            ln = code + 1
            if code == 13 or (code == 15 and s == 7):
                if i >= n or ((f[i] & 64) and i + 1 >= n):
                    return False
                ln = (14 if code == 13 else 8) + (((f[i] & 63) | f[i + 1] << 6) if f[i] & 64 else f[i])
                i += 2 if f[i] & 64 else 1
            if code == 15:          # the pixels one row up
                ln = s + 1 if s < 7 else ln
                if self.at < self.w:
                    return False
                for _ in range(ln):
                    if self.at >= self.w * self.h:
                        break
                    x, y = self.x + self.at % self.w, self.y + self.at // self.w
                    if x < W and 1 <= y < H:
                        self.fb[y * W + x] = self.fb[(y - 1) * W + x]
                        self.rows[y] = True
                    self.at += 1
                continue
            if s == 7:
                if i + 3 > n:
                    return False
                self.slot[self.rr] = v2(i) | f[i + 2] << 14
                s, self.rr = self.rr, self.rr + 1 if self.rr < 6 else 1
                i += 3
            if code == 14:          # a bitmap: g << 3 | the 0s' slot, then g + 1 bytes of 7 pixels, bit 6 first
                if i >= n or (f[i] & 7) == 7:
                    return False
                g, bg = (f[i] >> 3) + 1, self.slot[f[i] & 7]
                if i + 1 + g > n:
                    return False
                for k in range(g):
                    for j in range(7):
                        self._px(self.slot[s] if (f[i + 1 + k] >> (6 - j)) & 1 else bg, 1)
                i += 1 + g
                continue
            self._px(self.slot[s], ln)
        return True


def frames_of(data: bytes) -> list[list[int]]:
    out, cur = [], None
    for b in data:
        if b == 0xF0:
            cur = [b]
        elif cur is not None:
            cur.append(b)
            if b == 0xF7:
                out.append(cur)
                cur = None
    return out


def write_png(path: Path, fb: list[int]) -> None:
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        for c in fb[y * W:(y + 1) * W]:
            r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
            rows += bytes(((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31))

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def bits7(b: list[int], n: int) -> int:
    return sum(v << (7 * k) for k, v in enumerate(b[:n]))


def names(m: int) -> str:
    """the buttons whose bit is set in m, by label"""
    return " ".join(n for k, n in enumerate(BUTTONS) if m >> k & 1) or "-"


def keyl(m: int) -> str:
    """the note keys whose bit is set in m, 1 = F3"""
    return " ".join(str(k + 1) for k in range(27) if m >> k & 1) or "-"


def state_text(s: list[int]) -> str:
    """106 op 4 STATE in words"""
    if len(s) < 29:
        return "STATE: short"
    held, keys, lit, litk = bits7(s, 2), bits7(s[2:], 4), bits7(s[6:], 3), bits7(s[9:], 4)
    dimb, dimk = bits7(s[13:], 2), bits7(s[15:], 4)
    turns = " ".join(f"{n} {s[19 + k] - 64:+d}" for k, n in enumerate(KNOBS) if s[19 + k] != 64) or "-"
    return "\n".join([f"held:  {names(held)}; keys {keyl(keys)}",
                      f"lit:   {names(lit)}{' PLAY(green)' if lit >> 14 & 1 else ''}; keys {keyl(litk)}",
                      f"dim:   {names(dimb)}; keys {keyl(dimk)}",
                      f"knobs: {turns}",
                      f"MASTER pot {bits7(s[26:], 2)}{' (the client sets it)' if s[28] & 1 else ''}"
                      f"{'; a test session' if s[28] & 2 else ''}"])


def shot(out: Path, want_state: bool, off: bool = True) -> None:
    """the screen now as a PNG (off: the mirror asked off after; a test session keeps it on, or it ends)"""
    try:
        import rtmidi
    except ImportError:
        sys.exit("needs python-rtmidi: pip install python-rtmidi")
    mi, mo = rtmidi.MidiIn(queue_size_limit=8192), rtmidi.MidiOut()
    pi = [k for k, n in enumerate(mi.get_ports()) if PORT_RE.search(n)]
    po = [k for k, n in enumerate(mo.get_ports()) if PORT_RE.search(n)]
    if not pi or not po:
        sys.exit("no MIDI port named Optimist: connect the FM-1 by USB")
    dec = Decoder()
    got: deque = deque()                           # (filled by rtmidi's thread as messages come: polled, its queue overflows
                                                   # under a catch-up and a lost MORE frame puts pixels in the wrong place)

    def take(ev, _data) -> None:
        m = ev[0]                                  # (a real-time byte, the FM-1's clock, may sit inside a SysEx, as MIDI allows:
        if m and m[0] == 0xF0:                     # rtmidi leaves it in, Chrome takes it out; mido's parser broke the frames)
            got.append([b for b in m if b < 0xF8 or b == 0xF7])

    mi.ignore_types(sysex=False, timing=True, active_sense=True)
    mi.set_callback(take)
    mi.open_port(pi[0])
    mo.open_port(po[0])
    try:
        ask = [0xF0, *HDR, MIRROR, 3 | (4 if want_state else 0), 0xF7]
        t0 = last = time.monotonic()
        asked = 0.0
        began, began_at = False, 0.0
        bad = 0
        while True:
            now = time.monotonic()
            if not began and now - asked > ASK_S:   # (the first frame after opening a port can be lost: asked again until it comes)
                mo.send_message(ask)
                asked = now
            whole = began and (now - began_at > CATCH_S or now - last > QUIET_S)   # (a catch-up comes in a burst; a
            if whole and (not want_state or dec.state is not None):                # playing FM-1 is never quiet after it)
                break
            if now - t0 > WAIT_S:
                if not began:
                    sys.exit("no screen from the FM-1 (a firmware with ED_MIRROR?)")
                print(f"(after {WAIT_S:.0f} s: {sum(dec.rows)} of {H} rows drawn since the catch-up began)")
                break
            while got:
                f = got.popleft()
                fills = dec.fills
                ok = dec.frame(f)
                bad += not ok and f[:5] == [0xF0, *HDR, MIRROR]
                if ok and len(f) > 5 and f[5] in (0, 1, 2):
                    last = time.monotonic()
                    if not began and dec.fills > fills:
                        began, began_at = True, last
            time.sleep(0.005)
    finally:
        if off:
            mo.send_message([0xF0, *HDR, MIRROR, 0, 0xF7])
        mi.close_port()
        mo.close_port()
    write_png(out, dec.fb)
    print(f"wrote {out}" + (f" ({bad} mirror frames not decoded: the picture may be wrong there)" if bad else ""))
    if want_state and dec.state is not None:
        print(state_text(dec.state))


PANEL = 108
SESSION_WAIT_S = 30.0    # the FM-1 back on USB after a restart, at most this long
ALIVE_S = 1.0            # the mirror asked again this often (it ends 3 s after the last request; the session with it)


def ports():
    """the FM-1's USB-MIDI in and out (python-rtmidi), or None while it is not there (a restart)"""
    import rtmidi
    mi, mo = rtmidi.MidiIn(queue_size_limit=1024), rtmidi.MidiOut()
    pi = [k for k, n in enumerate(mi.get_ports()) if PORT_RE.search(n) and "bluetooth" not in n.lower()]
    po = [k for k, n in enumerate(mo.get_ports()) if PORT_RE.search(n) and "bluetooth" not in n.lower()]
    if not pi or not po:
        return None
    mi.ignore_types(sysex=False, timing=True, active_sense=True)
    mi.open_port(pi[0])
    mo.open_port(po[0])
    return mi, mo


def request(mi, mo, cmd: int, args: list[int], timeout: float = 1.0) -> list[int] | None:
    """an editor request (F0 7D 46 4C cmd args F7) and its answer's arguments, None: no answer"""
    while mi.get_message():
        pass
    mo.send_message([0xF0, *HDR, cmd, *args, 0xF7])
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        m = mi.get_message()
        if m and m[0][:5] == [0xF0, *HDR, cmd] and m[0][-1] == 0xF7:
            return [b for b in m[0][5:-1] if b < 0x80]
        time.sleep(0.002)
    return None


def wait_back() -> tuple:
    """the FM-1 gone and back (a restart): its ports again"""
    end = time.monotonic() + SESSION_WAIT_S
    time.sleep(2.0)
    while time.monotonic() < end:
        p = ports()
        if p and request(*p, MIRROR, [1]) is not None:
            return p
        if p:
            p[0].close_port(), p[1].close_port()
        time.sleep(0.5)
    sys.exit("the FM-1 did not come back on USB: power-cycle it (its own state is untouched)")


def session(seconds: float | None) -> None:
    """the test session (108 kind 5, opti_mtest_1): the FM-1 as a blank one for `seconds` (None: until Ctrl-C), its own
    flash never written; then the user's own again (a restart either way)"""
    try:
        p = ports()
    except ImportError:
        sys.exit("needs python-rtmidi: pip install python-rtmidi")
    if not p or request(*p, MIRROR, [1]) is None:
        sys.exit("no answer from the FM-1 over USB (a firmware with ED_MIRROR?)")
    r = request(*p, PANEL, [5, 1, 0])
    if not r or r[1] != 0:
        sys.exit("the FM-1 refused the test session (a firmware without opti_mtest_1, or over Bluetooth)")
    print("the FM-1 restarts as a blank one...")
    p[0].close_port(), p[1].close_port()
    p = wait_back()
    time.sleep(1.0)
    shot(Path("fm1-session-blank.png"), True, off=False)     # (the blank FM-1's screen, the mirror left on)
    print("test session: the FM-1 is blank, nothing is written to its flash" +
          (f" ({seconds:.0f} s)" if seconds else " (Ctrl-C ends it)"))
    end = time.monotonic() + seconds if seconds else None
    try:
        while end is None or time.monotonic() < end:
            request(*p, MIRROR, [1], timeout=0.5)       # (the keep-alive: no client, and the session would end)
            time.sleep(ALIVE_S)
    except KeyboardInterrupt:
        pass
    r = request(*p, PANEL, [5, 0, 0])
    print("ended: the FM-1 restarts into its own state" if r and r[1] == 0 else
          "the end was not answered: the session ends by itself within seconds (no client)")
    p[0].close_port(), p[1].close_port()


def main() -> None:
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "shot":
        shot(Path(a[1]), "--state" in a[2:])
    elif a and a[0] == "session":
        session(float(a[1]) if len(a) > 1 else None)
    elif len(a) == 3 and a[0] == "decode":
        dec = Decoder()
        bad = sum(not dec.frame(f) for f in frames_of(Path(a[1]).read_bytes()))
        write_png(Path(a[2]), dec.fb)
        print(f"wrote {a[2]}" + (f" ({bad} frames not decoded)" if bad else ""))
        sys.exit(1 if bad else 0)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()

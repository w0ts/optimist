# SPDX-License-Identifier: GPL-3.0-only
"""The Optimist UI's gestures and state for the panel checks (check_project, check_mute, check_undo, ...): what
docs/UI-OPTIMIST-DESIGN.md describes, driven through tests/emu/session.py, and the UI's own state read back through
the ELF's symbols (op_state.c `ui`, core.h `song` / `trk`, seq_mute.c `smute_trk`, op_name.c `nm`).

The `ui` struct's offsets below follow op_state.c's declaration (bytes, no #if inside it up to `overlay`)."""
import fwfacts as F
import session as S

BTN, white, black = F.BTN, F.white, F.black
UI_MSG, UI_SCR, UI_ROW, UI_HOT, UI_ARM_SCR = 7, 37, 43, 53, 56     # (op_state.c ui: msg[30] at 7, scr, row[SCR_N])
UI_CARDS_T = 124                                                  # (uint16_t cards_t, after toast_t and a pad byte)
SCR = dict(HOME=0, SOUND=1, FX=2, PROJECT=3, SYSTEM=4, STEP=5, SONG=6, TEMPO=7, SCOPE=8, GLOBAL=9)
SCR_NAME = {v: k for k, v in SCR.items()}
ARM_NONE = 0xFF
NM_S = 24                                                         # (op_name.c nm.s: 11 bytes, a pad, three words)


class UI:
    """the panel and the UI state of one session (e: session.Emu, facts: fwfacts.Facts)"""

    def __init__(self, e, facts, shots=None):
        self.e, self.f, self.shots, self.n = e, facts, shots, 0
        self.ui = e.sym("ui")[0]
        self.song = e.sym("song")[0]
        self.trk, size = e.sym("trk")
        self.stride = size // 4
        self.eng_off = 2 * facts.P["P_COUNT"]                  # (track_t: p[P_COUNT] int16, then engine, preset)

    # ---- evidence
    def shot(self, name):
        if self.shots:
            self.n += 1
            self.e.png(f"{self.shots}/{self.n:02d}-{name}.png")

    # ---- state
    def ub(self, off, n=1):
        return self.e.mem(f"{self.ui + off:#x}", n)

    def scr(self):
        return SCR_NAME.get(self.ub(UI_SCR)[0], "?")

    def msg(self):
        return self.ub(UI_MSG, 30).split(b"\0")[0].decode("latin1")

    def armed(self):
        return self.ub(UI_ARM_SCR)[0] != ARM_NONE

    def page(self):
        """the title of the cursor row's page: PAGES[ui.page] (core/params.c page_t: the title first, 12 bytes)"""
        k = self.ub(2)[0]
        a, size = self.e.sym("PAGES")
        if k >= size // 12:
            return "-"
        title = int.from_bytes(self.e.rodata(a + 12 * k, 4), "little")
        return self.e.rodata(title, 16).split(b"\0")[0].decode("latin1")

    def cards_t(self):
        return int.from_bytes(self.ub(UI_CARDS_T, 2), "little")

    def g(self, i):
        v = int.from_bytes(self.e.mem(f"{self.song + 2 * i:#x}", 2), "little")
        return v - 65536 if v >= 32768 else v

    def bpm(self):
        return self.g(0)                                       # (core.h G_BPM: the first global)

    def song_byte(self, k):
        """song_t after g[]: 0 playing, 1 seq_mode, 2 rec, 3 sel, 4 octave"""
        return self.e.mem(f"{self.song + 2 * self.f.G_COUNT + k:#x}", 1)[0]

    def playing(self):
        return self.song_byte(0)

    def sel(self):
        return self.song_byte(3)

    def octave(self):
        v = self.song_byte(4)
        return v - 256 if v >= 128 else v

    def sound(self, t):
        """(engine slot, preset) of track t"""
        b = self.e.mem(f"{self.trk + self.stride * t + self.eng_off:#x}", 2)
        return b[0], b[1]

    def param(self, t, name):
        pid = self.f.P[name]
        v = int.from_bytes(self.e.mem(f"{self.trk + self.stride * t + 2 * pid:#x}", 2), "little")
        return v - 65536 if v >= 32768 else v

    def poke_param(self, t, name, v):
        """track t's value name set to v in RAM (the 16-bit half of its aligned word)"""
        at = self.trk + self.stride * t + 2 * self.f.P[name]
        w = self.e.word(f"{at & ~3:#x}")
        sh = 16 * ((at & 3) // 2)
        self.e.poke(f"{at & ~3:#x}", (w & ~(0xFFFF << sh)) | ((v & 0xFFFF) << sh))

    def smute(self):
        return self.e.mem("smute_trk", 1)[0]

    def name_buf(self):
        return self.e.mem(f"{self.e.sym('nm')[0] + NM_S:#x}", 13).split(b"\0")[0].decode("latin1")

    # ---- gestures (a tap acts when let go: down 0.12 s, then up)
    def tap(self, btn, up=0.35):
        self.e.tap(BTN[btn], down=0.12, up=up)

    def key(self, k, up=0.3):
        self.e.tap(k, down=0.12, up=up)

    def yes(self, up=0.5):
        self.tap("SAVE", up)

    def home(self, up=0.4):
        self.tap("HOME", up)

    def turn(self, knob, n, wait=0.3):
        self.e.turn(knob, n)
        self.e.run(wait)

    def with_held(self, btn, fn, pre=0.15, post=0.3):
        """btn held while fn() runs (a shift: OCTDN SHIFT, OCTUP SHIFT2)"""
        self.e.hold(BTN[btn])
        self.e.run(pre)
        try:
            fn()
        finally:
            self.e.release(BTN[btn])
            self.e.run(post)

    def to_mixer(self):
        for _ in range(4):
            if self.scr() == "HOME" and not self.armed():
                return True
            self.home()
        return self.scr() == "HOME"

    def wait_for(self, cond, timeout=10.0, dt=0.05):
        t0 = self.e.now()
        while self.e.now() - t0 < timeout:
            if cond():
                return True
            self.e.run(dt)
        return cond()

    # ---- audio
    def record(self, path, s):
        """s seconds recorded -> (peak, rms) of both channels"""
        self.e.wavstart()
        self.e.run(s)
        left, right = self.e.wavstop(path)
        x = left + right
        return (max(abs(v) for v in x) if x else 0.0), S.rms(x)

    def health(self):
        """(late halves, boots, crash count) from felucca_dbg and fm1_crash"""
        d = self.e.dbg()
        cr = self.e.peek("fm1_crash", 2)
        return d["late"], d["boots"], (cr[1] if cr[0] == 0x43525348 else 0)

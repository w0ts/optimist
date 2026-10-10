# SPDX-License-Identifier: GPL-3.0-only
"""The panel gestures of SLOOP's UI (FELUCCA_UI 0) for the emulator checks, and the firmware state they read.

Gestures (docs/PATTERNS-DESIGN.md section 6, docs/SNAPSHOTS.md, fx_slots.c):
  SEQ held + white keys          step entry on the selected track (KNOB1 the note, KNOB4 the length)
  LFO held                       the PATTERN layer: black 1..4 the track, white n launch pattern n (at its end;
                                 OCT- held: the next bar, OCT+ held: now), black 6 held + white n STORE,
                                 black 8 held + white n CLEAR (twice: the confirm)
  SAVE held                      the scene layer: white 1..4 launch scene A..D, 5..8 store it, 16 the song page
  GLO held                       white 1..4 mute, 9..12 the FX bypass (or black 1..4 in a FILLS build)
  a family button, then SELECT   its pages (the page's title read from PAGES[ui.page]: no screen matching)
The track memory (trk[]) is read through the ELF's symbols; its layout (the step array, seq_abs, org) is measured
on a fresh boot (layout())."""
import fwfacts as F
from session import EmuError

BTN, white, black = F.BTN, F.white, F.black
FAMILY_BUTTON = {"ENV": "ENV", "LFO": "LFO", "FX": "FX", "SCL": "SEL", "EDIT": "EDIT", "GLO": "GLO", "SAVE": "SAVE",
                 "ARP": "ARP", "SEQ": "SEQ"}


class Layout:
    """track_t as this build lays it out: stride (sizeof track_t), steps (the step array), abs (seq_abs: idx +4,
    notes +6, n +10), org (the pattern origin: the last word, FELUCCA_PATTERNS)"""

    def __init__(self, e):
        a, size = e.sym("trk")
        self.base, self.stride = a, size // 4
        b = e.mem(f"{a:#x}", self.stride)                       # (track 1 of a fresh boot: 64 empty steps)
        rest = bytes([0, 0, 0, 0, 0, 2, 0, 0, 0, 0]) * 64       # (n 0, time ST_REST)
        self.steps = b.find(rest)
        if self.steps < 0:
            raise EmuError("track layout: no empty step array in trk[0] (not a fresh boot?)")
        self.abs = self.steps + 640
        self.org = self.stride - 4

    def at(self, t, off):
        return f"{self.base + self.stride * t + off:#x}"


class Panel:
    def __init__(self, e, facts, layout, shots=None):
        self.e, self.f, self.L, self.shots, self.n = e, facts, layout, shots, 0
        self.P = facts.P

    def shot(self, name):
        if self.shots:
            self.n += 1
            self.e.png(f"{self.shots}/{self.n:02d}-{name}.png")

    # ---- the firmware's state
    def param(self, t, pid):
        name = pid if isinstance(pid, int) else self.P[pid]
        w = self.e.peek(self.L.at(t, (name // 2) * 4))[0]
        v = (w >> (16 * (name & 1))) & 0xFFFF
        return v - 65536 if v >= 32768 else v

    def steps_of(self, t, n=64):
        """the steps of synth track t that hold notes -> {index: first note}"""
        b = self.e.mem(self.L.at(t, self.L.steps), 640)
        return {i: b[10 * i] for i in range(n) if b[10 * i + 4] > 0}

    def seq(self, t):
        """(seq_abs, seq_idx, seq_n, the first sounding note, org)"""
        w = self.e.peek(self.L.at(t, self.L.abs), 3)
        org = self.e.peek(self.L.at(t, self.L.org))[0]
        return w[0], w[1] & 0xFFFF, (w[2] >> 16) & 255, (w[1] >> 16) & 255, org

    def cur(self):
        w = self.e.peek("pat_cur")[0]
        return [(w >> (8 * i)) & 255 for i in range(4)]

    def req(self):
        w = self.e.peek("pat_req")[0]
        return [(w >> (8 * i)) & 255 for i in range(4)]

    def playing(self):
        return self.e.peekb(f"song+{self.song_play_off()}")[0]

    def song_play_off(self):
        return 76                                              # (song_t: playing at 76, seq_mode, rec, sel)

    def slots(self):
        w = self.e.peek("fxs_slot")[0]
        return [(w >> (8 * i)) & 255 for i in range(4)]

    def page(self):
        """the title of the page shown (PAGES[ui.page].title)"""
        k = self.e.peekb("ui")[0]
        a, _ = self.e.sym("PAGES")
        title = self.e.peek(f"{a + 12 * k:#x}")[0]
        return bytes(self.e.peekb(f"{title:#x}", 16)).split(b"\0")[0].decode("latin1")

    def logsyms(self, tracks=(0, 1)):
        s = ["clk_beat:2", "pat_cur:1", "pat_req:1", f"song+{self.song_play_off()}:1"]
        for t in tracks:
            s += [f"{self.L.at(t, self.L.abs)}:3", f"{self.L.at(t, self.L.org)}:1"]
        return s

    def decode(self, row, tracks=(0, 1)):
        """a log row -> the sequencer state: beat, cur/req per track, playing, and per track abs idx n note org"""
        d = dict(t=row["t"], halves=row["halves"], last_us=row["last_us"], late=row["late"], cpu_q8=row["cpu_q8"])
        d["beat"] = row["clk_beat"][0]
        pc, pr = row["pat_cur"][0], row["pat_req"][0]
        d["cur"] = [(pc >> (8 * i)) & 255 for i in range(4)]
        d["req"] = [(pr >> (8 * i)) & 255 for i in range(4)]
        d["playing"] = row[f"song+{self.song_play_off()}"][0] & 255
        d["trk"] = []
        for t in tracks:
            a, b, c = row[self.L.at(t, self.L.abs)]
            d["trk"].append(dict(abs=a, idx=b & 0xFFFF, note=(b >> 16) & 255, n=(c >> 16) & 255,
                                 org=row[self.L.at(t, self.L.org)][0]))
        return d

    # ---- gestures
    def layer(self, btn, keys, shot=None, extra=()):
        """hold a layer button (and extra buttons), then each key: an id (tapped), ("mod", held, tapped...),
        ("knob", KNOB, detents); let go"""
        e = self.e
        e.hold(BTN[btn], *[BTN[x] for x in extra])
        e.run(0.35)
        for k in keys:
            if isinstance(k, tuple) and k[0] == "mod":
                e.hold(k[1])
                e.run(0.12)
                for w in k[2:]:
                    e.tap(w, down=0.12, up=0.25)
                e.release(k[1])
                e.run(0.25)
            elif isinstance(k, tuple) and k[0] == "knob":
                e.click(k[1], k[2])
            else:
                e.tap(k, down=0.12, up=0.3)
        if shot:
            self.shot(shot)
        e.release(BTN[btn], *[BTN[x] for x in extra])
        e.run(0.3)

    def steps(self, keys, note=0, length=0, shot=None):
        """SEQ held: KNOB1 the note (detents), KNOB4 the length, then the step keys (white 1..16) toggled"""
        ks = ([("knob", "KNOB1", note)] if note else []) + ([("knob", "KNOB4", length)] if length else [])
        self.layer("SEQ", ks + [white(n) for n in keys], shot)

    def track(self, n, shot=None):
        self.layer("LFO", [black(n)], shot)

    def launch(self, track, slot, when="end", shot=None):
        mods = {"end": (), "bar": ("OCTDN",), "now": ("OCTUP",)}[when]
        self.layer("LFO", [black(track), white(slot)], shot, extra=mods)

    def pat_store(self, slot, shot=None):
        self.layer("LFO", [("mod", black(6), white(slot))], shot)

    def pat_clear(self, slot, shot=None):
        self.layer("LFO", [("mod", black(8), white(slot))])       # asks
        self.layer("LFO", [("mod", black(8), white(slot))], shot)  # confirmed

    def scene_store(self, n, again=False, shot=None):
        self.layer("SAVE", [white(4 + n)] * (2 if again else 1), shot)

    def scene_launch(self, n, shot=None):
        self.layer("SAVE", [white(n)], shot)

    def play(self, aligned=False):
        if aligned:
            self.e.align()
        self.e.tap(BTN["PLAY"], down=0.12, up=0.3)

    def stop(self):
        for _ in range(3):
            if not self.playing():
                return True
            self.play()
            self.e.run(0.4)
        return not self.playing()

    def home(self):
        self.e.tap(BTN["HOME"], up=0.4)

    def goto(self, family, title, shot=None):
        """the page `title` of a family (its button tapped, then SELECT walks the family's pages) -> found"""
        e = self.e
        if family == "SAVE":                # (SAVE tapped on TRACKS or the drum pages opens the song, on the song
            e.tap(BTN["GLO"], up=0.4)       # page it stores the chain: from a GLO page it opens the SAVE pages)
        seen = []
        for _ in range(8):                  # (a tap opens the family where it was left; tapped again: its next page)
            e.tap(BTN[FAMILY_BUTTON[family]], up=0.4)
            p = self.page()
            if p == title or p in seen:
                break
            seen.append(p)
        for direction in (1, -1):
            for _ in range(14):
                if self.page() == title:
                    if shot:
                        self.shot(shot)
                    return True
                before = self.page()
                e.turn("SELECT", direction)
                e.run(0.25)
                if self.page() == before:
                    break
        return self.page() == title

    def arm_act(self, knob, wait=2.5):
        """an action knob: a detent arms it, a second does it"""
        self.e.turn(knob, 1)
        self.e.run(0.4)
        self.e.turn(knob, 1)
        self.e.run(wait)

    def project_slot(self, n):
        self.e.click("KNOB1", -4)
        self.e.click("KNOB1", n - 1)

    def project_save(self, n, shot=None):
        ok = self.goto("SAVE", "PROJECT")
        self.project_slot(n)
        self.arm_act("KNOB4")
        if shot:
            self.shot(shot)
        self.home()
        return ok

    def project_load(self, n, shot=None):
        ok = self.goto("SAVE", "PROJECT")
        self.project_slot(n)
        self.arm_act("KNOB3")                      # (SLOT A24-or-none LOAD SAVE: params.c PROJECT)
        if shot:
            self.shot(shot)
        self.home()
        return ok

    def wait_for(self, cond, timeout=20.0, dt=0.004):
        t0 = self.e.now()
        while self.e.now() - t0 < timeout:
            if cond():
                return True
            self.e.run(dt)
        return False

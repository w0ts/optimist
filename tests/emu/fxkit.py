# SPDX-License-Identifier: GPL-3.0-only
"""The FX slots at the panel of SLOOP's UI, for the emulator checks (fx_slots.c; docs: FX-SLOTS-INSERTS-DESIGN.md of
fm1-firmware): the types, the SLOTS page (each knob steps through the types this build lists, a type held elsewhere
swaps), the FX page (its four knobs: the amounts of the slots' types on the selected track; on the drum track the
drum bus's), the SOUND 3 page (the picked drum sound's inserts), and the captures the checks compare.

A capture is one held note (or a drum hit) recorded from an audio half boundary, so the same setting gives the same
samples; captures are compared by their largest sample difference and by their level."""
import math

import fwfacts as F
import session as S

FXT = dict(NONE=0, DIST=1, CHO=2, DLY=3, REV=4, COMP=5, FILT=6)
NAME = {v: k for k, v in FXT.items()}
CFG = {1: "FX_DIST", 2: "FX_CHORUS", 3: "FX_DELAY", 4: "FX_REVERB", 5: "MASTER_COMP", 6: "TRK_FILT"}
DSEND_CMP = 4                                    # (drum_sends.c: SOUND 3's value ids, 4 the COMP insert)


def built(facts):
    """the types FX > SLOTS lists, in its order (fx_slots.c fxs_desc: NONE first)"""
    return [0] + [t for t in range(1, 7) if facts.on(CFG[t])]


class Fx:
    def __init__(self, e, P):
        self.e, self.P, self.f = e, P, P.f
        self.list = built(self.f)

    # ---- state
    def slots(self):
        return self.P.slots()

    def names(self, s=None):
        return "[" + " ".join(NAME.get(x, str(x)) for x in (s or self.slots())) + "]"

    def amt(self, t, pid="P_TCOMP"):
        return self.P.param(t, pid)

    def ins(self):
        """the drum sounds' inserts: (DIST [16], COMP [16]) (drum_sends.c dins_amt)"""
        b = self.e.mem("dins_amt", 32)
        return list(b[:16]), list(b[16:])

    def lane(self):
        return self.e.peekb("pen_lane")[0] & 15

    # ---- the pages
    def slots_page(self):
        return self.P.goto("FX", "SLOTS")

    def fx_page(self):
        return self.P.goto("FX", "FX")

    def slot_set(self, k, t):
        """slot k's knob a detent at a time until it holds type t (on the SLOTS page) -> done. Each type passed on
        the way is loaded: one another slot holds swaps with it"""
        for _ in range(2 * len(self.list)):
            cur = self.slots()[k]
            if cur == t:
                return True
            d = self.list.index(t) - (self.list.index(cur) if cur in self.list else 0)
            self.e.turn(f"KNOB{k + 1}", 1 if d > 0 else -1)
            self.e.run(0.3)
        return self.slots()[k] == t

    def step(self, k, d):
        self.e.turn(f"KNOB{k + 1}", d)
        self.e.run(0.3)

    def drop(self, t, depth=0):
        """type t out of the layout (on the SLOTS page): its slot a detent down onto a type no slot holds (a swap
        would only move t); the type below held: that one dropped first -> done"""
        if t not in self.slots():
            return True
        i = self.list.index(t)
        if depth > len(self.list) or i == 0:
            return False
        below = self.list[i - 1]
        if below != FXT["NONE"] and below in self.slots() and not self.drop(below, depth + 1):
            return False
        self.step(self.slots().index(t), -1)
        return t not in self.slots()

    def layout(self, want):
        """the four slots as want, four types this build lists (on the SLOTS page; a pass can undo an earlier
        slot by a swap: again until it holds) -> done"""
        for _ in range(4):
            for k, t in enumerate(want):
                self.slot_set(k, t)
            if self.slots() == list(want):
                return True
        return self.slots() == list(want)

    def scramble(self, t=0):
        """track t's COMP amount 0 (t selected for it, then T1 again), the default layout (DIST CHO DLY REV, those
        this build lists), HOME"""
        if FXT["COMP"] in self.slots():
            if t:
                self.P.track(t + 1)
            self.fx_page()
            self.comp_amount(t, 0, 0)
            if t:
                self.P.track(1)
        self.slots_page()
        self.layout([x if x in self.list else 0 for x in (1, 2, 3, 4)])
        self.P.home()

    def knob_to(self, knob, read, target, tol=2):
        """turn a knob until read() is within tol of target -> the value"""
        for _ in range(60):
            v = read()
            if abs(v - target) <= tol:
                return v
            step = (target - v) // 2 or (1 if target > v else -1)
            self.e.turn(knob, max(-8, min(8, step)))
            self.e.run(0.2)
        return read()

    def comp_amount(self, t, target, tol=2):
        """the FX page (open): the COMP slot's knob until track t's P_TCOMP is target -> the value"""
        k = self.slots().index(FXT["COMP"])
        return self.knob_to(f"KNOB{k + 1}", lambda: self.amt(t), target, tol)

    def sound_comp(self, target, tol=2):
        """the SOUND 3 page (open, the drum track): the COMP slot's knob until the picked sound's COMP insert is
        target -> the value"""
        k = self.slots().index(FXT["COMP"])
        return self.knob_to(f"KNOB{k + 1}", lambda: self.ins()[1][self.lane()], target, tol)

    def bypass(self, t=0):
        """GLO held + white 9..12 (black 1..4 in a FILLS build): track t's FX bypass flipped -> P_FXOFF"""
        v0 = self.P.param(t, "P_FXOFF")
        for k in ((F.black(1 + t),) if self.f.on("FILLS") else ()) + (F.white(9 + t),):
            self.P.layer("GLO", [k])
            if self.P.param(t, "P_FXOFF") != v0:
                break
        return self.P.param(t, "P_FXOFF")

    # ---- captures
    def capture(self, path, key, hold=1.6, tail=0.3):
        """key held from an audio half boundary for hold s -> the left channel"""
        e = self.e
        e.align()
        e.wavstart()
        e.hold(key)
        e.run(hold)
        e.release(key)
        e.run(tail)
        left, _ = e.wavstop(path)
        return left


def resid(a, b, start=0):
    """the largest |difference| of two captures (0.0: the same samples), from frame start"""
    n = min(len(a), len(b))
    return max((abs(x - y) for x, y in zip(a[start:n], b[start:n])), default=1.0)


def level_db(x, start=0):
    return 20 * math.log10(S.rms(x[start:]) + 1e-12)


def peak(x, start=0, n=None):
    s = x[start:start + n] if n else x[start:]
    return max((abs(v) for v in s), default=0.0)

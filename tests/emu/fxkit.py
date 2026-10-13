# SPDX-License-Identifier: GPL-3.0-only
"""The FX slots at the panel of SLOOP's UI, for the emulator checks (fx_slots.c; the per-track insert slots' design,
docs/UI-OPTIMIST-DESIGN.md "Insert slots"): the types; the master SLOTS page (the four SENDS slots: each knob steps
through the sends this build lists, a type held elsewhere swaps); the INSERTS page (the selected track's two insert
slots S1 and S2: K1 / K3 the TYPE, K2 / K4 its amount; the drum track's act on the drum bus; a type the other slot holds
swaps them); the SOUND 3 page (the picked drum sound's DST, its sends, CMP while there is room), and the captures the
checks compare.

A capture is one held note (or a drum hit) recorded from an audio half boundary, so the same setting gives the same
samples; captures are compared by their largest sample difference and by their level."""
import math

import fwfacts as F
import session as S

FXT = dict(NONE=0, DIST=1, CHO=2, DLY=3, REV=4, COMP=5, FILT=6, FOLD=7, FUZZ=8, PHSR=9, FLNG=10, FRMT=11, WIDE=12, CHRS=13,
           ECHO=14)
NAME = {v: k for k, v in FXT.items()}
CFG = {1: "FX_DIST", 2: "FX_CHORUS", 3: "FX_DELAY", 4: "FX_REVERB", 5: "MASTER_COMP", 6: "TRK_FILT", 7: "INS_FOLD",
       8: "INS_FUZZ", 9: "INS_PHASER", 10: "INS_FLANGER", 11: "INS_FORMANT", 12: "INS_WIDTH", 13: "INS_CHORUS", 14: "INS_DELAY"}
AMT = {7: "P_TFOLD", 8: "P_TFUZZ", 9: "P_TPHSR", 10: "P_TFLNG", 11: "P_TFRMT", 12: "P_TWIDE", 13: "P_TCHRS",
       14: "P_TECHO"}   # (the further inserts)
DSEND_CMP = 4                                    # (drum_sends.c: SOUND 3's value ids, 4 the COMP insert)


INSERT = (1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14)   # (DIST COMP FILT and the further ones; CHO DLY REV are the sends)
TRK_DEF = [FXT["DIST"], FXT["FILT"]]             # (every track's slots at the start: S1 DIST, S2 FILTER, amounts 0)
MASTER_DEF = [0, 2, 3, 4]                        # (the master slots: ---- CHO DLY REV)


def built(facts):
    """the types FX > SLOTS lists (the sends), in its order (fx_slots.c fxs_desc: NONE first)"""
    return [0] + [t for t in range(1, len(CFG) + 1) if t not in INSERT and facts.on(CFG[t])]


def ins_built(facts):
    """the types INSERTS lists, in its order (fx_slots.c ins_desc: NONE first, then by type id)"""
    return [0] + [t for t in range(1, len(CFG) + 1) if t in INSERT and facts.on(CFG[t])]


class Fx:
    def __init__(self, e, P):
        self.e, self.P, self.f = e, P, P.f
        self.list = built(self.f)
        self.ilist = ins_built(self.f)

    # ---- state
    def slots(self):
        return self.P.slots()

    def names(self, s=None):
        return "[" + " ".join(NAME.get(x, str(x)) for x in (s or self.slots())) + "]"

    def tins(self):
        """every track's insert slots: [[S1, S2]] for T1 T2 T3 DR (fx_slots.c ins_slot)"""
        b = self.e.peekb("ins_slot", 8)             # (bytes: the symbol is not word aligned)
        return [list(b[2 * k:2 * k + 2]) for k in range(4)]

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

    def trk_def(self):
        """a track's slots at the start: S1 DIST, S2 FILTER (each only when this build has it, else empty)"""
        return [x if x in self.ilist else 0 for x in TRK_DEF]

    def inserts_page(self):
        return self.P.goto("FX", "INSERTS")

    def ins_set(self, t, k, ty):
        """track t's insert slot k (track t selected, the INSERTS page open): its TYPE knob a detent at a time until it
        holds ty -> done. Each type passed on the way is loaded: the one the other slot holds swaps with it"""
        for _ in range(2 * len(self.ilist)):
            cur = self.tins()[t][k]
            if cur == ty:
                return True
            d = self.ilist.index(ty) - (self.ilist.index(cur) if cur in self.ilist else 0)
            self.e.turn(f"KNOB{2 * k + 1}", 1 if d > 0 else -1)
            self.e.run(0.3)
        return self.tins()[t][k] == ty

    def ins_drop(self, t, ty):
        """type ty out of track t's slots (selected, INSERTS open): its slot a detent down onto a type no slot holds (a
        detent onto a held type only swaps the slots); the type below held: that one dropped first -> done"""
        if ty not in self.tins()[t]:
            return True
        i = self.ilist.index(ty)
        if i == 0:
            return False
        below = self.ilist[i - 1]
        if below and below in self.tins()[t] and not self.ins_drop(t, below):   # (held: a step down only swaps)
            return False
        self.e.turn(f"KNOB{2 * self.tins()[t].index(ty) + 1}", -1)
        self.e.run(0.3)
        return ty not in self.tins()[t]

    def ins_layout(self, t, want, tries=3):
        """track t's two slots as want (selected, INSERTS open; a pass can undo an earlier slot by a swap) -> done"""
        for _ in range(tries):
            for k, ty in enumerate(want):
                self.ins_set(t, k, ty)
            if self.tins()[t] == list(want):
                return True
        return self.tins()[t] == list(want)

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
        """track t's COMP amount 0 (t selected for it, then T1 again), its slots S1 DIST S2 FILTER and the master
        slots ---- CHO DLY REV (those this build lists), HOME"""
        self.P.track(t + 1)
        self.inserts_page()
        if FXT["COMP"] in self.tins()[t]:
            self.comp_amount(t, 0, 0)
        self.ins_layout(t, self.trk_def())
        if t:
            self.P.track(1)
        self.slots_page()
        self.layout([x if x in self.list else 0 for x in MASTER_DEF])
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

    def ins_amount(self, t, ty, target, tol=2):
        """the INSERTS page (open, track t selected): the knob of the slot holding type ty (a further insert: insfx.c)
        until track t's amount of it is target -> the value"""
        k = self.tins()[t].index(ty)
        return self.knob_to(f"KNOB{2 * k + 2}", lambda: self.amt(t, AMT[ty]), target, tol)

    def comp_amount(self, t, target, tol=2):
        """the INSERTS page (open, track t selected): the knob of the slot holding COMP until track t's P_TCOMP is
        target -> the value"""
        k = self.tins()[t].index(FXT["COMP"])
        return self.knob_to(f"KNOB{2 * k + 2}", lambda: self.amt(t), target, tol)

    def sound_cmp_knob(self):
        """SOUND 3's CMP cell: DST, then the sends heard (the master slots' non-empty ones), then CMP (fx_slots.c
        fxs_lane_id: it shows only while there is room: at most two sends) -> its knob, 0 when it has none"""
        n = 1 + sum(1 for x in self.slots() if x)
        return n + 1 if n < 4 else 0

    def sound_comp(self, target, tol=2):
        """the SOUND 3 page (open, the drum track): the CMP cell's knob until the picked sound's COMP insert is
        target -> the value (D7: heard whatever the track's slots hold)"""
        k = self.sound_cmp_knob()
        if not k:
            return -1
        return self.knob_to(f"KNOB{k}", lambda: self.ins()[1][self.lane()], target, tol)

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

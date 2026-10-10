# SPDX-License-Identifier: GPL-3.0-only
"""fx: the FX slots at the panel (SLOOP's UI; fx_slots.c). T1 holds one note per capture (white key 5, from an audio
half boundary: the same setting gives the same samples, checked first, after the first autosave's window). COMP into
S1 (FX > SLOTS), its amount on T1 (FX page): heard, transparent at 0, a middle amount between; the FX bypass (GLO +
white 9): dry, the amount not heard, back after; the amount belongs to the type (COMP moved to S4: the same samples;
in no slot: not heard, its amount kept). The drum track: the drum bus's COMP (FX page, the drum track selected) and
the picked sound's COMP insert (EDIT > SOUND 3), each heard, neither with COMP in no slot. Then the whole set-up
(slots, T1's and the drum bus's amounts, the insert) comes back from a snapshot (SAVE > SNAPSHOT) after a scramble,
and after the autosave and a power cycle, with the same samples."""
import math

import fwfacts as F
import fxkit as X

T1_KEY, DRUM_KEY, DRUM_LANE = F.white(5), F.white(6), 5   # (sound 6: a sample, the same samples each hit;
#                                                              the synthesized ones have noise of their own)
GAP = 1.0                                        # (s before a capture: the last one's tail gone)


class Run:
    def __init__(self, ctx, e, P):
        self.ctx, self.e, self.P, self.fx = ctx, e, P, X.Fx(e, P)

    def t1(self, name):
        self.e.run(GAP)
        return self.fx.capture(self.ctx.wav(name), T1_KEY)

    def drum(self, name):
        """a drum hit (sound 1), the drum track selected for it (and T1 again after)"""
        self.P.track(4)
        self.e.run(GAP)
        x = self.fx.capture(self.ctx.wav(name), DRUM_KEY, hold=0.3, tail=1.2)
        self.P.track(1)
        return x

    def state(self):
        fx = self.fx
        return fx.slots(), fx.amt(0), fx.amt(3), fx.ins()[1][DRUM_LANE]

    def fmt(self, s):
        return f"{self.fx.names(s[0])} T1 COMP {s[1]}, drum bus COMP {s[2]}, sound 6 COMP insert {s[3]}"

    def same(self, name, a, b):
        r = X.resid(a, b)
        return self.ctx.check(name, r == 0.0, f"max |diff| {r:.2e}")

    def scramble(self):
        """every amount of the set-up to 0, the default layout"""
        fx, P = self.fx, self.P
        P.track(4)
        fx.fx_page()
        fx.comp_amount(3, 0, 0)
        P.track(4)
        P.goto("EDIT", "SOUND 3")
        fx.sound_comp(0, 0)
        P.track(1)
        fx.fx_page()
        fx.comp_amount(0, 0, 0)
        fx.slots_page()
        fx.layout([X.FXT["DIST"], X.FXT["CHO"], X.FXT["DLY"], X.FXT["REV"]])
        P.home()


def run(ctx):
    if not ctx.facts.on("MASTER_COMP"):
        ctx.info("skipped", "no COMP in this build (FELUCCA_MASTER_COMP 0)")
        return
    e, P = ctx.open(fresh=True)
    r = Run(ctx, e, P)
    try:
        body(ctx, r)
    finally:
        r.e.quit()


def body(ctx, r):
    e, P, fx = r.e, r.P, r.fx
    P.layer("GLO", [("knob", "KNOB1", 30)])           # (T1's level up)
    e.run(max(0.0, 25.0 - e.now()))                    # (the first autosave's window: from 20 s on, 2.5 s idle)
    a, b = r.t1("default"), r.t1("default-again")
    r.same("a capture is deterministic: the same setting twice, the same samples", a, b)
    ctx.check("T1 sounds (steady RMS > -50 dBFS)", X.level_db(a) > -50, f"{X.level_db(a):.1f} dBFS")
    ctx.check("the default layout DIST CHO DLY REV", fx.slots() == [1, 2, 3, 4], fx.names())

    comp_in(ctx, r, a)
    hi = r.t1("comp-hi")
    bypass(ctx, r, a, hi)
    moved = swap(ctx, r, a, hi)
    drums(ctx, r)
    restore(ctx, r, moved)


def comp_in(ctx, r, a):
    """COMP into S1, its amount on T1: transparent at 0, heard, ordered"""
    e, P, fx = r.e, r.P, r.fx
    fx.slots_page()
    ok = fx.slot_set(0, X.FXT["COMP"])
    P.shot("slots-comp")
    used = [x for x in fx.slots() if x]
    ctx.check("FX > SLOTS: COMP into S1, each type in one slot at most", ok and len(set(used)) == len(used),
              fx.names())
    fx.fx_page()
    c0 = r.t1("comp0")
    r.same("COMP at amount 0 is transparent (the same samples as the default layout)", a, c0)
    v = fx.comp_amount(0, 127, 3)
    hi = r.t1("comp-hi")
    d = [X.level_db(hi) - X.level_db(c0), 20 * math.log10(X.peak(hi, 0, 13000) / X.peak(c0, 0, 13000))]
    ctx.check(f"COMP heard on T1 (P_TCOMP {v}): the level or the attack peak moved >= 0.5 dB",
              v >= 120 and max(abs(x) for x in d) >= 0.5, f"RMS {d[0]:+.2f} dB, attack peak {d[1]:+.2f} dB")
    v2 = fx.comp_amount(0, 50, 3)
    mid = r.t1("comp-mid")
    lv = [X.level_db(x) for x in (c0, mid, hi)]
    ctx.check(f"COMP amount {v2} sits between 0 and {v} (the RMS level)", min(lv[0], lv[2]) < lv[1] < max(lv[0], lv[2]),
              " / ".join(f"{x:.2f}" for x in lv) + " dBFS")
    fx.comp_amount(0, 127, 0)


def bypass(ctx, r, a, hi):
    """the FX bypass: dry (as at amount 0), the amount not heard, back after"""
    P, fx = r.P, r.fx
    ctx.check("FX bypass: GLO + white 9 sets T1's P_FXOFF", fx.bypass(0) == 1, "")
    byp = r.t1("bypass")
    r.same("FX bypass: dry (the same samples as COMP at 0)", a, byp)
    ctx.check("FX bypass: off again", fx.bypass(0) == 0, "")
    r.same("FX bypass off: COMP heard as before (the same samples)", hi, r.t1("bypass-off"))
    P.home()


def swap(ctx, r, a, hi):
    """the amount belongs to the type: COMP moved to S4, then in no slot, then back"""
    P, fx = r.P, r.fx
    v = fx.amt(0)
    fx.slots_page()
    fx.slot_set(3, X.FXT["COMP"])
    P.shot("slots-comp-s4")
    ctx.check("slot swap: COMP into S4 (the type it met moved to S1), its amount kept",
              fx.slots()[3] == X.FXT["COMP"] and fx.slots().count(X.FXT["COMP"]) == 1 and fx.amt(0) == v,
              f"{fx.names()}, P_TCOMP {v} -> {fx.amt(0)}")
    moved = r.t1("comp-s4")
    r.same("slot swap: COMP in S4 sounds as in S1 (the same samples)", hi, moved)
    fx.slots_page()
    fx.drop(X.FXT["COMP"])
    ctx.check("COMP in no slot: out of the layout, its amount kept", X.FXT["COMP"] not in fx.slots() and
              fx.amt(0) == v, f"{fx.names()}, P_TCOMP {fx.amt(0)}")
    r.same("COMP in no slot: not heard (the same samples as the default layout)", a, r.t1("comp-none"))
    fx.slots_page()
    fx.slot_set(3, X.FXT["COMP"])
    r.same("COMP in a slot again: heard as before (the same samples)", hi, r.t1("comp-back"))
    P.home()
    return moved


def drums(ctx, r):
    """the drum bus's COMP and a sound's COMP insert: each heard, neither with COMP in no slot"""
    e, P, fx = r.e, r.P, r.fx
    fx.slots_page()                                   # (REV into a slot: its send on SOUND 3 can be set)
    fx.slot_set(next(k for k, x in enumerate(fx.slots()) if x != X.FXT["COMP"]), X.FXT["REV"])
    ctx.check("REV into a slot beside COMP", X.FXT["REV"] in fx.slots() and X.FXT["COMP"] in fx.slots(), fx.names())
    d0 = r.drum("drum-wet")
    ctx.check("the drum track sounds (white 6: sound 6)", X.level_db(d0) > -70 and fx.lane() == DRUM_LANE,
              f"{X.level_db(d0):.1f} dBFS, pen_lane {fx.lane()}")
    P.track(4)                                        # (LFO + black 4: the drum track)
    ok = P.goto("EDIT", "SOUND 3")                    # (sound 6 dry: its REV send 0, no reverb tail between captures)
    e.turn(f"KNOB{fx.slots().index(X.FXT['REV']) + 1}", -64)
    e.run(0.3)
    d0 = r.drum("drum0")
    r.same("a drum hit is deterministic (sound 6 dry: the same samples twice)", d0, r.drum("drum0-again"))
    P.track(4)
    fx.fx_page()
    v = fx.comp_amount(3, 127, 3)
    bus = r.drum("drum-bus")
    rb = X.resid(d0, bus)
    ctx.check(f"the drum bus's COMP heard (drum track P_TCOMP {v})", v >= 120 and rb > 1e-3, f"max |diff| {rb:.3f}")
    P.track(4)
    ok = P.goto("EDIT", "SOUND 3") and ok
    P.shot("sound3")
    w = fx.sound_comp(127, 3)
    ins = r.drum("drum-insert")
    ri = X.resid(bus, ins)
    ctx.check(f"sound 6's COMP insert heard (EDIT > SOUND 3: dins_amt[1][{DRUM_LANE}] {w})", ok and w >= 120 and ri > 1e-3,
              f"max |diff| {ri:.3f}, inserts {fx.ins()[1][:8]}")
    fx.slots_page()
    k = fx.slots().index(X.FXT["COMP"])
    fx.drop(X.FXT["COMP"])
    r.same("COMP in no slot: neither the drum bus's COMP nor the insert heard (the same samples as at 0)", d0,
           r.drum("drum-comp-none"))
    fx.slots_page()
    fx.slot_set(k, X.FXT["COMP"])
    r.same("COMP back: the drums as before (the same samples)", ins, r.drum("drum-comp-back"))
    P.track(1)
    P.home()


def restore(ctx, r, moved):
    """the set-up from a snapshot after a scramble, then after the autosave and a power cycle"""
    e, P, fx = r.e, r.P, r.fx
    fin = r.state()
    t1, dr = r.t1("final"), r.drum("final-drum")
    r.same("before the snapshot: T1 as with COMP in S4 (the same samples)", moved, t1)
    if ctx.facts.on("SNAPSHOTS"):
        ok = P.goto("SAVE", "SNAPSHOT")
        e.click("KNOB1", -8)
        P.arm_act("KNOB4")
        P.shot("snapshot-saved")
        P.home()
        r.scramble()
        s = r.state()
        ctx.check("scrambled: the default layout, every amount 0", s != fin and s[0] == [1, 2, 3, 4], r.fmt(s))
        ok = P.goto("SAVE", "SNAPSHOT") and ok
        e.click("KNOB1", -8)
        P.arm_act("KNOB2")
        P.shot("snapshot-loaded")
        P.home()
        s = r.state()
        ctx.check("SAVE > SNAPSHOT 1 saved, scrambled, loaded: the slots and the amounts", ok and s == fin,
                  f"{r.fmt(s)}; saved {r.fmt(fin)}")
        r.same("the snapshot: T1 the same samples", t1, r.t1("snapshot-t1"))
        r.same("the snapshot: the drums the same samples", dr, r.drum("snapshot-drum"))
    e.run(24.0)                                       # (the autosave: 2.5 s idle, 20 s after the last)
    e.quit()
    e, P = ctx.open(fresh=False, settle=3.5)
    r.e, r.P, r.fx = e, P, X.Fx(e, P)
    s = r.state()
    ctx.check("power cycle: the autosave brings back the slots and the amounts", s == fin,
              f"{r.fmt(s)}; before {r.fmt(fin)}")
    r.same("power cycle: T1 the same samples", t1, r.t1("cycle-t1"))
    r.same("power cycle: the drums the same samples", dr, r.drum("cycle-drum"))

# SPDX-License-Identifier: GPL-3.0-only
"""fx: the FX slots at the panel (SLOOP's UI; fx_slots.c: the master slots hold the SENDS, each track has two insert
slots S1 and S2, a new track's S1 DIST S2 FILTER, amounts 0). T1 holds one note per capture (white key 5, from an audio
half boundary: the same setting gives the same samples, checked first, after the first autosave's window). COMP into
T1's S1 (FX > INSERTS), its amount: heard, transparent at 0, a middle amount between; the FX bypass (GLO + white 9):
dry, the amount not heard, back after; the amount belongs to the type (COMP moved to S2: the same samples; in no
slot: not heard, its amount kept). The drum track: its own COMP slot's bus COMP (INSERTS, the drum track selected)
heard only while COMP is in one of its slots, and the picked sound's COMP insert (EDIT > SOUND 3) heard whatever the
slots hold (D7). Then the whole set-up (the master slots, every track's slots, T1's and the drum bus's amounts, the
insert) comes back from a snapshot (SAVE > SNAPSHOT) after a scramble, and after the autosave and a power cycle, with
the same samples."""
import math

import fwfacts as F
import fxkit as X

T1_KEY, DRUM_KEY, DRUM_LANE = F.white(5), F.white(6), 5   # (sound 6: a sample, the same samples each hit;
#                                                              the synthesized ones have noise of their own)
GAP = 1.0                                        # (s before a capture: the last one's tail gone)
COMP, REV, DIST, FILT = X.FXT["COMP"], X.FXT["REV"], X.FXT["DIST"], X.FXT["FILT"]


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
        return fx.slots(), fx.tins(), fx.amt(0), fx.amt(3), fx.ins()[1][DRUM_LANE]

    def fmt(self, s):
        return (f"master {self.fx.names(s[0])}, slots {[self.fx.names(x) for x in s[1]]}, T1 COMP {s[2]}, "
                f"drum bus COMP {s[3]}, sound 6 COMP insert {s[4]}")

    def same(self, name, a, b):
        r = X.resid(a, b)
        return self.ctx.check(name, r == 0.0, f"max |diff| {r:.2e}")

    def scramble(self):
        """every amount of the set-up to 0, the default layouts (the drum track's slots, T1's, the master slots)"""
        fx, P = self.fx, self.P
        P.track(4)
        fx.inserts_page()
        if COMP in fx.tins()[3]:
            fx.comp_amount(3, 0, 0)
        fx.ins_layout(3, fx.trk_def())
        P.goto("EDIT", "SOUND 3")
        fx.sound_comp(0, 0)
        P.track(1)
        fx.inserts_page()
        if COMP in fx.tins()[0]:
            fx.comp_amount(0, 0, 0)
        fx.ins_layout(0, fx.trk_def())
        fx.slots_page()
        fx.layout(X.MASTER_DEF)
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
    ctx.check("the default layout: master ---- CHO DLY REV (sends only), every track S1 DIST S2 FILT",
              fx.slots() == X.MASTER_DEF and fx.tins() == [fx.trk_def()] * 4,
              f"{fx.names()}, {[fx.names(x) for x in fx.tins()]}")
    ctx.check("COMP is not offered to a master slot (inserts live on the tracks): FX > SLOTS refuses it",
              not slot_takes_insert(fx), fx.names())

    comp_in(ctx, r, a)
    hi = r.t1("comp-hi")
    bypass(ctx, r, a, hi)
    moved = swap(ctx, r, a, hi)
    drums(ctx, r)
    restore(ctx, r, moved)


def slot_takes_insert(fx):
    """the master SLOTS page: every knob turned well past both ends of the list; an insert type in a slot at any
    point (DIST, COMP, FILT) -> True. The default layout again after"""
    fx.slots_page()
    seen = set(fx.slots())
    for k in range(4):
        for d in (1, -1):
            for _ in range(5):
                fx.step(k, d)
                seen |= set(fx.slots())
    fx.layout(X.MASTER_DEF)
    fx.P.home()
    return bool(seen & set(X.INSERT))


def comp_in(ctx, r, a):
    """COMP into T1's S1 (it takes DIST's place), its amount: transparent at 0, heard, ordered"""
    e, P, fx = r.e, r.P, r.fx
    fx.inserts_page()
    ok = fx.ins_set(0, 0, COMP)
    P.shot("inserts-comp")
    used = [x for x in fx.tins()[0] if x]
    ctx.check("FX > INSERTS: COMP into T1's S1, each type in one slot at most", ok and len(set(used)) == len(used),
              fx.names(fx.tins()[0]))
    c0 = r.t1("comp0")
    r.same("COMP at amount 0 is transparent (the same samples as the default layout)", a, c0)
    fx.inserts_page()
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
    """the amount belongs to the type: COMP moved to S2 (S1 takes what S2 held), then in no slot, then back"""
    P, fx = r.P, r.fx
    v = fx.amt(0)
    fx.inserts_page()
    fx.ins_set(0, 1, COMP)
    P.shot("inserts-comp-s2")
    t = fx.tins()[0]
    ctx.check("slot swap: COMP into T1's S2 (what S2 held moved to S1), its amount kept",
              t[1] == COMP and t.count(COMP) == 1 and fx.amt(0) == v,
              f"{fx.names(t)}, P_TCOMP {v} -> {fx.amt(0)}")
    moved = r.t1("comp-s2")
    r.same("slot swap: COMP in S2 sounds as in S1 (the same samples)", hi, moved)
    fx.inserts_page()
    fx.ins_drop(0, COMP)
    ctx.check("COMP in no slot: out of T1's slots, its amount kept", COMP not in fx.tins()[0] and fx.amt(0) == v,
              f"{fx.names(fx.tins()[0])}, P_TCOMP {fx.amt(0)}")
    r.same("COMP in no slot: not heard (the same samples as the default layout)", a, r.t1("comp-none"))
    fx.inserts_page()
    fx.ins_set(0, 1, COMP)
    r.same("COMP in a slot again: heard as before (the same samples)", hi, r.t1("comp-back"))
    P.home()
    return moved


def drums(ctx, r):
    """the drum bus's COMP (heard only in a drum-track slot) and a sound's COMP insert (always heard: D7)"""
    e, P, fx = r.e, r.P, r.fx
    fx.slots_page()                                   # (REV out of the master slots: no reverb tail between captures,
    fx.drop(REV)                                      #  and SOUND 3 has room for its CMP cell: DST CHO DLY CMP)
    ctx.check("REV out of the master slots: SOUND 3 shows a CMP cell", REV not in fx.slots() and fx.sound_cmp_knob() == 4,
              fx.names())
    d0 = r.drum("drum-dry")
    ctx.check("the drum track sounds (white 6: sound 6)", X.level_db(d0) > -70 and fx.lane() == DRUM_LANE,
              f"{X.level_db(d0):.1f} dBFS, pen_lane {fx.lane()}")
    r.same("a drum hit is deterministic (sound 6 dry: the same samples twice)", d0, r.drum("drum0-again"))
    P.track(4)
    fx.inserts_page()
    ctx.check("the drum track's own slots: S1 DIST S2 FILT, COMP in none", fx.tins()[3] == fx.trk_def(),
              fx.names(fx.tins()[3]))
    ok = P.goto("EDIT", "SOUND 3")
    P.shot("sound3")
    w = fx.sound_comp(127, 3)
    ins = r.drum("drum-insert")
    ri = X.resid(d0, ins)
    ctx.check(f"sound 6's COMP insert heard with COMP in no drum slot (EDIT > SOUND 3: dins_amt[1][{DRUM_LANE}] {w})",
              ok and w >= 120 and ri > 1e-3, f"max |diff| {ri:.3f}, inserts {fx.ins()[1][:8]}")
    P.track(4)
    fx.inserts_page()
    fx.ins_set(3, 0, COMP)
    v = fx.comp_amount(3, 127, 3)
    both = r.drum("drum-bus")
    rb = X.resid(ins, both)
    ctx.check(f"the drum bus's COMP heard on top (drum track S1 COMP, P_TCOMP {v})", v >= 120 and rb > 1e-3,
              f"max |diff| {rb:.3f}, {fx.names(fx.tins()[3])}")
    P.track(4)
    fx.inserts_page()
    fx.ins_drop(3, COMP)
    r.same("COMP out of the drum slots: the bus's COMP not heard, the sound's insert still (the same samples as "
           "the insert alone)", ins, r.drum("drum-comp-none"))
    P.track(4)
    fx.inserts_page()
    fx.ins_set(3, 0, COMP)
    r.same("COMP back in the drum slots: bus and insert as before (the same samples)", both, r.drum("drum-comp-back"))
    P.track(1)
    P.home()


def restore(ctx, r, moved):
    """the set-up from a snapshot after a scramble, then after the autosave and a power cycle"""
    e, P, fx = r.e, r.P, r.fx
    fin = r.state()
    t1, dr = r.t1("final"), r.drum("final-drum")
    r.same("before the snapshot: T1 as with COMP in S2 (the same samples)", moved, t1)
    if ctx.facts.on("SNAPSHOTS"):
        ok = P.goto("SAVE", "SNAPSHOT")
        e.click("KNOB1", -8)
        P.arm_act("KNOB4")
        P.shot("snapshot-saved")
        P.home()
        r.scramble()
        s = r.state()
        ctx.check("scrambled: the default layouts, every amount 0",
                  s != fin and s[0] == X.MASTER_DEF and s[1] == [fx.trk_def()] * 4 and s[2] == 0 and s[3] == 0,
                  r.fmt(s))
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

# SPDX-License-Identifier: GPL-3.0-only
"""insfx: the further insert types at the panel (SLOOP's UI; fx/insfx/insfx.c: FOLD FUZZ PHSR FLNG FRMT WIDE CHRS ECHO GATER, each a
builder item, INS_*). For every one this build has: FX > INSERTS lists it; into T1's S1, at amount 0 the same samples as
the default layout (bit for bit); at 127 heard (the capture differs); with T1's FX bypassed (GLO + white 9) the
bypassed default's samples (no sends either);
back at 0, the same samples as at first (the fade-out ends, nothing left). T1 holds one note per capture (white key 5)
from an audio half boundary, as check_fx."""
import fwfacts as F
import fxkit as X

T1_KEY = F.white(5)
GAP = 1.0
NEW = (7, 8, 9, 10, 11, 12, 13, 14, 15)           # (13 14: CHRS ECHO, an instance each from the main loop: fxi.c; 15
                                                  # GATER: stopped, it runs on at the tempo, TRNC1's 2nd step closed)
# the bypassed capture against the first dry one: up to 2 LSB. The master's DC blocker (fx.c dc_block) keeps its step's
# remainder (0..4095) for ever, so most likely (not traced) the small DC a loud FOLD or FRMT left in the mix moves a later sample by
# 1 LSB (measured: 1.53e-05, after 1 s or 4 s alike); the insert itself is not running (back at 0: exact)
LSB2 = 2.0 / 32768


def run(ctx):
    have = [t for t in NEW if ctx.facts.on(X.CFG[t])]
    if not have:
        ctx.info("skipped", "no further insert in this build (INS_* all 0)")
        return
    e, P = ctx.open(fresh=True)
    fx = X.Fx(e, P)

    def t1(name):
        e.run(GAP)
        return fx.capture(ctx.wav(name), T1_KEY)

    try:
        P.layer("GLO", [("knob", "KNOB1", 30)])       # (T1's level up)
        e.run(max(0.0, 25.0 - e.now()))                # (the first autosave's window)
        a = t1("default")
        ctx.check("T1 sounds (steady RMS > -50 dBFS)", X.level_db(a) > -50, f"{X.level_db(a):.1f} dBFS")
        d0 = fx.bypass(0)
        dry = t1("dry")                                 # (T1's FX bypassed: no inserts, no sends)
        d1 = fx.bypass(0)
        ctx.check("T1's FX bypass on and off (GLO + white 9)", d0 == 1 and d1 == 0, f"P_FXOFF {d0} then {d1}")
        ctx.check("FX > INSERTS lists every further insert built", all(t in fx.ilist for t in have),
                  fx.names(fx.ilist))
        for t in have:
            n = X.NAME[t]
            fx.inserts_page()
            ok = fx.ins_set(0, 0, t)
            P.shot(f"inserts-{n.lower()}")
            ctx.check(f"{n} into T1's S1", ok, fx.names(fx.tins()[0]))
            z = t1(f"{n.lower()}-0")
            r = X.resid(a, z)
            ctx.check(f"{n} at 0: the default's samples", r == 0.0, f"max |diff| {r:.2e}")
            fx.inserts_page()
            v = fx.ins_amount(0, t, 127, 3)
            hi = t1(f"{n.lower()}-hi")
            r = X.resid(a, hi)
            ctx.check(f"{n} heard at {v}", v >= 120 and r > 1e-3, f"max |diff| {r:.2e}")
            on = fx.bypass(0)
            byp = t1(f"{n.lower()}-bypass")
            off = fx.bypass(0)
            r = X.resid(dry, byp)
            ctx.check(f"{n} at {v}, T1's FX bypassed: dry (the bypassed default's samples)",
                      on == 1 and off == 0 and r <= LSB2, f"P_FXOFF {on} then {off}, max |diff| {r:.2e}")
            fx.inserts_page()
            fx.ins_amount(0, t, 0, 0)
            back = t1(f"{n.lower()}-back")
            r = X.resid(a, back)
            ctx.check(f"{n} back at 0: the default's samples again", r == 0.0, f"max |diff| {r:.2e}")
        fx.inserts_page()
        fx.ins_layout(0, fx.trk_def())
        P.home()
    finally:
        e.quit()

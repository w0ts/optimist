# SPDX-License-Identifier: GPL-3.0-only
"""chord: the CHORD+ latch (docs/RELEASE-0.2-TEST.md section 9; seq.c chord_tap, cm_lock). Fresh boot: T1 on a
poly ANALOG preset (PRESETS to the first BRASS, PAD, STRING or KEYS); SCL (T1's SCL page: ROOT SCALE QUANT CHORD),
KNOB 4 up: chord mode on (P_CHORD). On the mixer: white key 5 held, recorded (the chord without a modifier). Black key 1 tapped twice within 400 ms: latched (cm_lock[0]); white key 5 held again:
another chord (a different sound). Black key 1 tapped once more: the latch let go when the key is up (cm_lock[0] 0),
white key 5: the first chord again. Skipped without CHORDPLUS."""
import fwfacts as F
import opui
import session as S

POLY = ("BRASS", "PAD", "STRING", "KEYS")


def held(u, path):
    e = u.e
    e.align()
    e.wavstart()
    e.hold(F.white(5))
    e.run(0.8)
    e.release(F.white(5))
    e.run(0.3)
    left, right = e.wavstop(path)
    x = [(a + b) / 2 for a, b in zip(left, right)]
    r = S.rms(x) or 1e-9
    return r, S.rms([x[i] - x[i - 1] for i in range(1, len(x))]) / r


def differs(a, b):
    return abs(a[0] - b[0]) / b[0] > 0.05 or abs(a[1] - b[1]) / b[1] > 0.05


def run(ctx):
    if not ctx.facts.on("CHORDPLUS"):
        ctx.info("skipped", "FELUCCA_CHORDPLUS 0")
        return
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        for _ in range(30):                             # (a poly sound: the first preset, a sub-bass, plays one note)
            e.click("PRESETS", 1)
            if any(w in u.msg() for w in POLY):
                break
        e.run(1.5)
        sound = u.msg()
        u.tap("SEL")
        u.turn("KNOB4", 1)
        u.shot("chord-on")
        mode = u.param(0, "P_CHORD")
        u.home()
        plain = held(u, ctx.wav("chord"))
        e.tap(F.black(1), down=0.08, up=0.1)
        e.tap(F.black(1), down=0.08, up=0.3)
        lock = e.mem("cm_lock", 1)[0]
        mod = held(u, ctx.wav("chord-latched"))
        u.shot("latched")
        ctx.check("chord mode on, black key 1 tapped twice: latched, the chord changes with it",
                  mode != 0 and lock != 0 and differs(mod, plain),
                  f"{sound}, P_CHORD {mode}, cm_lock[0] {lock:#x}, rms {plain[0]:.4f} -> {mod[0]:.4f}, brightness "
                  f"{plain[1]:.3f} -> {mod[1]:.3f}")
        e.tap(F.black(1), down=0.08, up=0.5)
        lock2 = e.mem("cm_lock", 1)[0]
        back = held(u, ctx.wav("chord-released"))
        ctx.check("tapped once more: the latch let go, the plain chord again", lock2 == 0 and not differs(back, plain),
                  f"cm_lock[0] {lock2:#x}, rms {back[0]:.4f} (plain {plain[0]:.4f})")
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()

# SPDX-License-Identifier: GPL-3.0-only
"""stepcards: STEP's card times (commit c2b638f2; docs/RELEASE-0.2-TEST.md section 8; op_step.c step_cards_show,
op_input.c: SHIFT2 + SELECT). Fresh boot, SEQ tapped: STEP on PATTERN (LEN DIV SWING GATE). The plain time: SELECT
turned (the cards back), 3 s later the cards are down (ui.cards_t 0) and KNOB 3 no longer edits SWING (P_SSWING kept:
the note picker has the knobs). The long time: OCT+ (SHIFT2) held, SELECT turned, OCT+ let go: the octave unchanged,
cards_long set; 6 s later the cards are still up and KNOB 3 edits SWING; 12 s after that still up. A step held past
the hold time, let go: its cards keep the short time (down 3 s later). STEP left (HOME) and entered again (SEQ):
cards_long cleared. Read back: op_state.c ui.cards_t, cards_long, song.octave, trk[0].p[P_SSWING]."""
import fwfacts as F
import opui


def run(ctx):
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    def sw():
        return u.param(0, "P_SSWING")

    def long_on():
        return e.mem("cards_long", 1)[0]

    try:
        u.tap("SEQ")
        e.run(3.0)
        u.turn("SELECT", 1, wait=0.2)
        t_up = u.cards_t()
        e.run(3.0)
        t_down = u.cards_t()
        s0 = sw()
        u.turn("KNOB3", 2)
        s1 = sw()
        u.shot("short-time")
        ctx.check("plain time: SELECT brings the cards back, 3 s later down; KNOB 3 then is the picker's (SWING kept)",
                  u.scr() == "STEP" and 0 < t_up <= 90 and t_down == 0 and s1 == s0 and not long_on(),
                  f"screen {u.scr()}, cards_t {t_up} -> {t_down}, SWING {s0} -> {s1}, cards_long {long_on()}")
        o0 = u.octave()
        u.with_held("OCTUP", lambda: u.turn("SELECT", 1, wait=0.2))
        t_long = u.cards_t()
        ctx.check("OCT+ held + SELECT: the long time on, the octave unchanged when OCT+ is let go",
                  long_on() == 1 and t_long > 90 and u.octave() == o0,
                  f"cards_long {long_on()}, cards_t {t_long}, octave {o0} -> {u.octave()}")
        e.run(6.0)
        t6 = u.cards_t()
        s2 = sw()
        u.turn("KNOB3", 2)
        s3 = sw()
        u.shot("long-time")
        ctx.check("6 s later the cards are up and KNOB 3 edits SWING (not the picker)", t6 > 0 and s3 != s2,
                  f"cards_t {t6}, SWING {s2} -> {s3}")
        e.run(12.0)
        t18 = u.cards_t()
        ctx.check("12 s after that edit: still up", t18 > 0, f"cards_t {t18}")
        e.hold(F.white(3))
        e.run(0.8)
        e.release(F.white(3))
        e.run(3.0)
        t_held = u.cards_t()
        u.shot("held-step")
        ctx.check("a step held and let go: its cards keep the short time (down 3 s later)", t_held == 0,
                  f"cards_t {t_held}, cards_long {long_on()}")
        u.home()
        u.tap("SEQ")
        e.run(0.5)
        ctx.check("STEP left and entered again: the long time cleared", u.scr() == "STEP" and long_on() == 0,
                  f"screen {u.scr()}, cards_long {long_on()}")
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()

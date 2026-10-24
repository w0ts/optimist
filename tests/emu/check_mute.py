# SPDX-License-Identifier: GPL-3.0-only
"""mute: the sequencer mute (SHIFT + a key; docs/RELEASE-0.2-TEST.md section 9, seq_mute.c, op_input.c shift_keys) and
step entry with undo / redo (STEP, docs/UI-OPTIMIST-DESIGN.md 2.6 and 4.2). Fresh boot: SEQ tapped (STEP), white keys
1 5 9 13 tapped set four steps on T1 (the pick); SAVE then HOME (undo) takes the last one away, HOME then SAVE (redo)
brings it back. PLAY: T1 heard. HOME (the mixer), SHIFT + white key 1: T1's sequencer muted (smute_trk bit 0), its
VOLUME (P_LEVEL) untouched, the pattern silent; T1's VOLUME card turned (KNOB 1): still muted and silent; SHIFT + white
key 1 again: heard again. Read back: the step array of trk[0] (panel.Layout), smute_trk, P_LEVEL; levels from the WAVs."""
import fwfacts as F
import opui

STEPS = (1, 5, 9, 13)
QUIET = 0.002                                           # (peak: nothing but the tails of what played before)


def chord_tap(u, first, second):
    """first held, second held, second let go, first let go (SAVE + HOME: undo; HOME + SAVE: redo)"""
    e = u.e
    e.hold(opui.BTN[first])
    e.run(0.15)
    e.hold(opui.BTN[second])
    e.run(0.15)
    e.release(opui.BTN[second])
    e.run(0.1)
    e.release(opui.BTN[first])
    e.run(0.4)


def run(ctx):
    e, P = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    vol = "P_LEVEL"                                     # (the mixer's VOLUME card: core.h P_LEVEL)
    try:
        u.tap("SEQ")
        for k in STEPS:
            u.key(F.white(k))
        set1 = sorted(P.steps_of(0))
        u.shot("steps")
        ctx.check("STEP: white keys 1 5 9 13 set T1's steps 0 4 8 12", set1 == [k - 1 for k in STEPS],
                  f"screen {u.scr()}, steps {set1}")
        chord_tap(u, "SAVE", "HOME")
        set2 = sorted(P.steps_of(0))
        ctx.check("SAVE then HOME (undo): the last step taken away", set2 == [k - 1 for k in STEPS[:-1]], f"steps {set2}")
        chord_tap(u, "HOME", "SAVE")
        set3 = sorted(P.steps_of(0))
        u.shot("redo")
        ctx.check("HOME then SAVE (redo): it is back", set3 == set1, f"steps {set3}")
        u.to_mixer()
        u.tap("PLAY", up=1.0)
        heard = u.record(ctx.wav("heard"), 2.0)
        ctx.check("PLAY: T1's pattern heard", u.playing() and heard[0] > 0.01, f"playing {u.playing()}, peak {heard[0]:.4f}")
        v0 = u.param(0, vol)
        u.with_held("OCTDN", lambda: u.key(F.white(1)))
        e.run(0.6)
        m = u.smute()
        u.shot("muted")
        muted = u.record(ctx.wav("muted"), 2.0)
        ctx.check("SHIFT + white key 1: T1's sequencer muted, its VOLUME untouched, the pattern silent",
                  m & 1 and u.param(0, vol) == v0 and muted[0] < QUIET,
                  f"smute_trk {m:#x}, {vol} {v0} -> {u.param(0, vol)}, peak {muted[0]:.4f}")
        u.turn("KNOB1", 3)
        v1 = u.param(0, vol)
        turned = u.record(ctx.wav("volume-turned"), 2.0)
        ctx.check("T1's VOLUME card turned while muted: still muted and silent",
                  v1 != v0 and u.smute() & 1 and turned[0] < QUIET,
                  f"{vol} {v0} -> {v1}, smute_trk {u.smute():#x}, peak {turned[0]:.4f}")
        u.with_held("OCTDN", lambda: u.key(F.white(1)))
        e.run(0.6)
        back = u.record(ctx.wav("unmuted"), 2.0)
        u.shot("unmuted")
        ctx.check("SHIFT + white key 1 again: heard again", not u.smute() & 1 and back[0] > 0.01,
                  f"smute_trk {u.smute():#x}, peak {back[0]:.4f}")
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()

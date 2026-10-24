# SPDX-License-Identifier: GPL-3.0-only
"""project: a project saved, then loaded after a power cycle (docs/RELEASE-0.2-TEST.md sections 2 and 9; op_prjflow.c,
op_name.c). Fresh boot: GLOBAL's BPM up (KNOB 1), T1's sound changed (PRESETS +2 on the mixer; on ANALOG with LADR
built also FLT 2's MODEL, LADR LP24, set in RAM: P_A2FTYP, checked after the load too); SAVE tapped (PROJECT),
ACTION SAVE (KNOB 2), white key 2 asks, YES opens NAME (prefilled PROJECT 2), SHIFT + KNOB 1 clears it, the keys type
EMU, YES saves (the toast SAVED). Power cycle. PROJECT > NEW (KNOB 4, YES, YES): the tempo and T1's sound back to a new
project's. White key 2 (ACTION LOAD), YES: the tempo, T1's engine and preset come back as saved; SAVE on slot 2 again
opens NAME prefilled with the stored name EMU (cancelled: HOME held). Read back: song.g[G_BPM], trk[0]'s engine and
preset, op_name.c nm.s; the screens in shots/."""
import fwfacts as F
import opui

SLOT_KEY = 2                                            # (white key 2: project slot 2)
NAME = ((3, 1), (6, 2), (10, 2))                        # (white key, taps: E, M, U; op_name.c NM_ABC)
A2_LADR_LP24 = 1 * 5 + 1                                # (P_A2FTYP: model x 5 + mode, as tests/emu/sweep.py --a2)


def save_named(u, key, typed):
    """PROJECT, ACTION SAVE, key -> asked, YES -> NAME, cleared, typed, YES -> (the name typed, the message)"""
    u.tap("SAVE")
    u.turn("KNOB2", 1)                                  # (ACTION: LOAD -> SAVE)
    u.key(F.white(key))
    u.yes()
    u.with_held("OCTDN", lambda: u.turn("KNOB1", 1))    # (SHIFT + a knob: the name cleared)
    for k, n in typed:
        for _ in range(n):
            u.key(F.white(k), up=0.15)
        u.e.run(1.0)                                     # (a new key after 0.8 s: the next character)
    name = u.name_buf()
    u.shot("named")
    u.yes(up=2.0)
    return name, u.msg()


def run(ctx):
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        b0, s0 = u.bpm(), u.sound(0)
        u.tap("GLO")
        u.turn("KNOB1", 5)
        u.home()
        u.turn("PRESETS", 2, wait=1.0)
        b1, s1 = u.bpm(), u.sound(0)
        ctx.check("the work changed: BPM and T1's preset", b1 != b0 and s1 != s0, f"BPM {b0} -> {b1}, T1 {s0} -> {s1}")
        a2 = ctx.facts.on("A2_LADR") and u.sound(0)[0] == 0      # (T1 on ANALOG: FLT 2's MODEL, LADR LP24)
        if a2:
            u.poke_param(0, "P_A2FTYP", A2_LADR_LP24)
        name, msg = save_named(u, SLOT_KEY, NAME)
        u.shot("saved")
        ctx.check("PROJECT SAVE on slot 2 through NAME: typed EMU, saved", name == "EMU" and msg.startswith("SAVED"),
                  f"name {name!r}, message {msg!r}")
        u.to_mixer()
        e.run(1.0)
    finally:
        e.quit()
    e, _ = ctx.open(fresh=False, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        u.shot("power-on")
        u.tap("SAVE")
        u.turn("KNOB4", 1)                              # (NEW hot)
        u.yes()
        armed = u.armed()
        u.yes(up=1.5)
        b2, s2 = u.bpm(), u.sound(0)
        new_ftyp = u.param(0, "P_A2FTYP")
        u.shot("new")
        ctx.check("after a power cycle, PROJECT NEW (asked): a blank work", armed and (b2, s2) != (b1, s1),
                  f"asked {armed}, BPM {b2}, T1 {s2}")
        u.key(F.white(SLOT_KEY))                        # (ACTION is LOAD again: key 2 asks to load slot 2)
        armed = u.armed()
        u.shot("load-asked")
        u.yes(up=2.0)
        b3, s3 = u.bpm(), u.sound(0)
        u.shot("loaded")
        ctx.check("LOAD slot 2 (asked): the tempo and T1's engine and preset as saved", armed and b3 == b1 and s3 == s1,
                  f"asked {armed}, BPM {b3} (saved {b1}), T1 {s3} (saved {s1}), message {u.msg()!r}")
        if a2:
            ctx.check("... and ANALOG 2's FLT 2 MODEL (LADR LP24, set in RAM before the save)",
                      u.param(0, "P_A2FTYP") == A2_LADR_LP24 and new_ftyp != A2_LADR_LP24,
                      f"P_A2FTYP {u.param(0, 'P_A2FTYP')} (saved {A2_LADR_LP24}, after NEW {new_ftyp})")
        u.turn("KNOB2", 1)
        u.key(F.white(SLOT_KEY))
        u.yes()
        stored = u.name_buf()
        u.shot("name-stored")
        ctx.check("the project's name kept: SAVE on slot 2 opens NAME with EMU", stored == "EMU", f"NAME holds {stored!r}")
        e.hold(opui.BTN["HOME"])
        e.run(0.9)
        e.release(opui.BTN["HOME"])
        e.run(0.5)
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()

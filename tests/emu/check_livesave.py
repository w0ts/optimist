# SPDX-License-Identifier: GPL-3.0-only
"""livesave: LIVE_SAVE, saving while the pattern plays (docs/RELEASE-0.2-TEST.md section 7; op_name.c, the builder's
LIVE_SAVE). Fresh boot: T1 steps 1 5 9 13 (STEP), T1's sound edited (EDIT: MAIN, KNOB 1 -8, CUT), PLAY, 2 s recorded (the
level while playing). Playing: SAVE held + ENV tapped (NAME), YES: the user preset saved (SAVED ..); then SAVE tapped
(PROJECT), ACTION SAVE, white key 3, YES, NAME, YES: project 3 saved. Each recorded through the save: no STOP BEFORE
SAVE, still playing, no late half, the halves at 172 a second, the level within LEVEL_RATIO of the playing level.
Then stopped: another tempo saved as project 4 (NEW would clear the open project 3), LOAD project 3: the tempo and
the sound back; SHIFT + ENV (INIT), PROJECT > USER, white key 1, YES: T1's sound values (trk[0].p[P_LEVEL .. P_E7])
as saved. Skipped in a build without LIVE_SAVE."""
import fwfacts as F
import opui

STEPS = (1, 5, 9, 13)
LEVEL_RATIO = 0.5
RATE = 44100 / 256


def sound_values(u):
    lo, hi = u.f.P["P_LEVEL"], u.f.P["P_E7"]
    return [u.param(0, n) for n in sorted(u.f.P, key=u.f.P.get) if lo <= u.f.P[n] <= hi]


def through(ctx, u, what, gesture, base_rms):
    """gesture() while playing, recorded with 1.5 s after it -> checks"""
    e = u.e
    h0, t0 = e.dbg()["halves"], e.now()
    e.wavstart()
    e.run(0.5)
    gesture()
    msg = u.msg()
    e.run(1.5)
    left, right = e.wavstop(ctx.wav(what.replace(" ", "-")))
    h1, t1 = e.dbg()["halves"], e.now()
    rate = (h1 - h0) / (t1 - t0)
    lvl = (sum(v * v for v in left + right) / max(1, len(left + right))) ** 0.5
    late = u.health()[0]
    u.shot(what.replace(" ", "-"))
    ctx.check(f"{what} while playing: saved, still playing, the audio on",
              msg.startswith("SAVED") and u.playing() == 1 and late == 0 and abs(rate - RATE) < 2.0 and
              lvl >= LEVEL_RATIO * base_rms,
              f"message {msg!r}, playing {u.playing()}, late {late}, {rate:.1f} halves/s, level {lvl:.4f} "
              f"(playing {base_rms:.4f})")


def run(ctx):
    if not ctx.facts.on("LIVE_SAVE"):
        ctx.info("skipped", "FELUCCA_LIVE_SAVE 0")
        return
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    user_row = 2 if ctx.facts.on("SNAPSHOTS") else 1
    try:
        u.tap("SEQ")
        for k in STEPS:
            u.key(F.white(k))
        u.home()
        u.tap("EDIT")
        u.turn("KNOB1", -8)                             # (MAIN's CUT down: a new project starts at the top)
        u.home()
        saved_sound = sound_values(u)
        u.tap("GLO")
        u.turn("KNOB1", 3)
        u.home()
        saved_bpm = u.bpm()
        u.tap("PLAY", up=1.0)
        base = u.record(ctx.wav("playing"), 2.0)[1]

        def preset():
            e.hold(opui.BTN["SAVE"])
            e.run(0.2)
            u.tap("ENV")
            e.release(opui.BTN["SAVE"])
            e.run(0.5)
            u.yes(up=0.3)

        def project():
            u.tap("SAVE")
            u.turn("KNOB2", 1)
            u.key(F.white(3))
            u.yes()
            u.yes(up=0.3)

        through(ctx, u, "user preset SAVE", preset, base)
        u.to_mixer()
        through(ctx, u, "project SAVE", project, base)
        u.to_mixer()
        u.tap("PLAY", up=1.0)
        ctx.check("stopped", u.playing() == 0, f"playing {u.playing()}")
        # (NEW would clear the open project, project 3 (op_screens.c G_NEWPRJ): another tempo saved as project 4
        #  instead, which opens it; then project 3 loaded)
        u.tap("GLO")
        u.turn("KNOB1", 3)
        u.home()
        other = u.bpm()
        u.tap("SAVE")
        u.turn("KNOB2", 1)
        u.key(F.white(4))
        u.yes()
        u.yes(up=2.0)
        u.key(F.white(3))
        asked = u.armed()
        u.yes(up=2.0)
        ctx.check("stopped, project 4 saved at another tempo, then LOAD project 3: the tempo and T1's sound as "
                  "saved while playing", asked and other != saved_bpm and u.bpm() == saved_bpm and
                  sound_values(u) == saved_sound, f"asked {asked}, BPM {u.bpm()} (saved {saved_bpm}, project 4 "
                  f"{other}), message {u.msg()!r}")
        u.to_mixer()
        u.with_held("OCTDN", lambda: u.tap("ENV"))      # (SHIFT + ENV: INIT the sound)
        if u.armed():
            u.yes(up=1.0)
        blank = sound_values(u)
        u.tap("SAVE")
        u.turn("SELECT", user_row)
        u.shot("user-row")
        u.key(F.white(1))
        u.yes(up=1.5)
        e.run(1.0)
        got = sound_values(u)
        diff = [i for i, (a, b) in enumerate(zip(got, saved_sound)) if a != b]
        ctx.check("INIT, then PROJECT > USER, LOAD U01: T1's sound values as saved while playing",
                  not diff and blank != saved_sound,
                  f"row {u.ub(opui.UI_ROW + 3)[0]}, values differing {len(diff)}, INIT differed {blank != saved_sound}, "
                  f"message {u.msg()!r}")
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()

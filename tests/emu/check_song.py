# SPDX-License-Identifier: GPL-3.0-only
"""song: patterns and scenes, and a project that owns them (docs/RELEASE-0.2-TEST.md section 9: "Projects own their
scenes and patterns", SONG; op_combos.c save_pattern, op_layers.c, op_song.c). Fresh boot, stopped throughout.
T1's steps 1 and 5 set (STEP); SAVE held + SEQ: stored into its pattern slot (pat_cur[0]); SHIFT2 + SEQ (SONG), SAVE
held + white key 1: the four tracks' patterns stored as scene A. LFO held + white key 2: T1 on pattern 2 (empty);
steps 3 7 11 set, SAVE + SEQ, SONG, SAVE + white key 2: scene B. On SONG, white key 1 (scene A, stopped: loaded):
T1 plays pattern 1 with steps 1 5; white key 2: pattern 2 with 3 7 11. Saved as project 1 (a project owns
the scenes it was open for); power cycle; saved as 10 (which opens 10: its own scenes, reported); LOAD 1; SONG,
white key 1: steps 1 5 again, white key 2: 3 7 11.
Read back: pat_cur, T1's step array (panel.Layout)."""
import fwfacts as F
import opui

A_STEPS, B_STEPS = (1, 5), (3, 7, 11)


def chord(u, held, btn=None, key=None):
    """held down, then a button or a key tapped, held let go"""
    e = u.e
    e.hold(opui.BTN[held])
    e.run(0.2)
    if btn:
        u.tap(btn, up=0.2)
    else:
        u.key(key, up=0.2)
    e.release(opui.BTN[held])
    e.run(0.5)
    if u.armed():                                       # (asked over a used slot: YES)
        u.yes()


def set_steps(u, keys):
    u.tap("SEQ")
    for k in keys:
        u.key(F.white(k))
    u.home()


def to_song(u):
    u.with_held("OCTUP", lambda: u.tap("SEQ", up=0.2))
    u.e.run(0.3)
    return u.scr() == "SONG"


def scene(u, n):
    """on SONG, white key n: scene n loaded (stopped)"""
    if u.scr() != "SONG":
        to_song(u)
    u.key(F.white(n), up=0.8)


def run(ctx):
    if not ctx.facts.on("PATTERNS"):
        ctx.info("skipped", "FELUCCA_PATTERNS 0")
        return
    e, P = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    want = (sorted(k - 1 for k in A_STEPS), sorted(k - 1 for k in B_STEPS))
    try:
        set_steps(u, A_STEPS)
        chord(u, "SAVE", btn="SEQ")
        pa = P.cur()[0]
        ok = to_song(u)
        chord(u, "SAVE", key=F.white(1))
        u.shot("scene-a")
        u.home()
        e.hold(opui.BTN["LFO"])
        e.run(0.5)
        u.key(F.white(2))
        e.release(opui.BTN["LFO"])
        e.run(0.6)
        pb = P.cur()[0]
        empty = sorted(P.steps_of(0))
        set_steps(u, B_STEPS)
        chord(u, "SAVE", btn="SEQ")
        to_song(u)
        chord(u, "SAVE", key=F.white(2))
        u.shot("scene-b")
        ctx.check("patterns 1 and 2 on T1 stored, scenes A and B stored on SONG",
                  ok and pa != pb and not empty and u.msg() != "NO FREE PATTERN",
                  f"SONG reached {ok}, T1 pattern {pa} then {pb} (empty {empty}), message {u.msg()!r}")
        scene(u, 1)
        sa, ca = sorted(P.steps_of(0)), P.cur()[0]
        scene(u, 2)
        sb, cb = sorted(P.steps_of(0)), P.cur()[0]
        u.shot("scene-b-loaded")
        ctx.check("SONG, scene A then B (stopped: loaded): T1's pattern and steps follow",
                  (sa, sb) == want and (ca, cb) == (pa, pb), f"A: pattern {ca} steps {sa}; B: pattern {cb} steps {sb}")
        u.home()
        open1 = e.mem("prj_cur", 1)[0]
        msg1 = save(u, 1)
    finally:
        e.quit()
    e, P = ctx.open(fresh=False, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        set_steps(u, (16,))
        msg10 = save(u, 10)
        scene(u, 1)
        s10 = sorted(P.steps_of(0))
        u.shot("project-10-scene-a")
        ctx.info("SAVE into another slot (10)", f"the scenes there: scene A loads T1 steps {s10} (sections.c "
                 "project_scope_set: the new slot's own scenes and patterns, not the open project's)")
        u.to_mixer()
        u.tap("SAVE")
        u.key(F.white(1))
        asked = u.armed()
        u.yes(up=2.0)
        msg = u.msg()
        u.to_mixer()
        scene(u, 1)
        sa = sorted(P.steps_of(0))
        scene(u, 2)
        sb = sorted(P.steps_of(0))
        u.shot("reloaded")
        ctx.check("saved as project 1, power cycle, project 10 saved (opens it), LOAD 1: scenes A and "
                  "B bring their patterns back",
                  msg1.startswith("SAVED") and msg10.startswith("SAVED") and asked and (sa, sb) == want,
                  f"open at boot prj_cur {open1}, saves {msg1!r} {msg10!r}, asked {asked}, load {msg!r}; A steps {sa}, "
                  f"B steps {sb}")
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()


def save(u, key):
    u.to_mixer()
    u.tap("SAVE")
    u.turn("KNOB2", 1)
    u.key(F.white(key))
    u.yes()
    u.yes(up=2.0)
    return u.msg()

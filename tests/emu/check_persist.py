# SPDX-License-Identifier: GPL-3.0-only
"""persist: save, power cycle (the emulator restarted on the flash it saved), load (SLOOP's UI). Session 1: COMP into
S1 with an amount on T2, then the content of the patterns check (T1 p1..p3, T2 p1..p2 stored, scenes A..C, so the
scenes carry COMP), the song chain A B C (2 bars each, stored: SAVE tapped on the song page), scene C playing as the
working copy, PROJECT > SAVE slot 4, idle for the autosave. Session 2, on that flash: a clean boot; the autosaved
working copy (its pattern sources and steps, its FX); each stored pattern; scenes A..C (their patterns, scene A its
FX); the song chain; the work scrambled, then PROJECT > LOAD slot 4: the working copy and its FX again; the song
played from the flash (A, B, C in order, then it stops)."""
import check_patterns as CP
import fxkit as X

SCENE = {1: (0, 0), 2: (1, 1), 3: (2, 0)}             # (scene: T1, T2 pattern slots, as check_patterns.build)
CHAIN = ((0, 2), (1, 2), (2, 2))
PROJECT_SLOT = 4


def run(ctx):
    e, P = ctx.open(fresh=True)
    s = {"e": e}
    try:
        exp, work = session1(ctx, e, P)
        e.quit()
        e, P = ctx.open(fresh=False, settle=4.0)
        s["e"] = e
        session2(ctx, e, P, exp, work)
    finally:
        s["e"].quit()


def fx_of(fx):
    return fx.slots(), fx.amt(1)


def fmt(fx, st):
    return f"{fx.names(st[0])} T2 COMP {st[1]}"


def session1(ctx, e, P):
    fx = X.Fx(e, P)
    has_comp = ctx.facts.on("MASTER_COMP")
    if has_comp:
        P.track(2)
        fx.slots_page()
        fx.slot_set(0, X.FXT["COMP"])
        fx.fx_page()
        fx.comp_amount(1, 100, 8)
        P.track(1)
        P.home()
    scene_fx = fx_of(fx)
    exp = CP.build(ctx, e, P)
    ab = CP.song_chain(e, P, CHAIN)
    e.tap(X.F.BTN["SAVE"], up=1.5)                    # (SAVE on the song page: the chain stored)
    P.home()
    ctx.check("session 1: the song chain set (A, B, C, 2 bars each)", ab[0] == 3 and list(ab[4:10]) ==
              [0, 2, 1, 2, 2, 2], f"count {ab[0]}, parts {list(ab[4:10])}")
    P.scene_launch(3)
    e.run(1.0)
    work = dict(cur=P.cur()[:2], fx=fx_of(fx), scene_fx=scene_fx)
    ctx.check("session 1: scene C is the working copy (T1 p3, T2 p1)" + (", COMP in S1 with T2's amount" if has_comp
              else ""), work["cur"] == [2, 0] and (not has_comp or work["fx"][0][0] == X.FXT["COMP"] and
                                                    work["fx"][1] > 50), f"cur {work['cur']}, {fmt(fx, work['fx'])}")
    ok = P.project_save(PROJECT_SLOT, shot="project-saved")
    ctx.check(f"session 1: PROJECT > SAVE slot {PROJECT_SLOT}", ok, P.page())
    e.run(30.0)                                       # (the autosave: 2.5 s idle, 20 s after the last)
    return exp, work


def session2(ctx, e, P, exp, work):
    fx = X.Fx(e, P)
    d, cr = e.dbg(), e.peek("fm1_crash", 2)
    ctx.check("session 2: boots clean on the saved flash (no crash record, no late half)",
              (cr[0] != 0x43525348 or cr[1] == 0) and d["late"] == 0, f"late {d['late']}, crash {cr}")
    c = P.cur()[:2]
    m0, m1 = CP.mem_is(P, 0, exp[0][2]), CP.mem_is(P, 1, exp[1][0])
    ctx.check("session 2: the autosaved working copy: its pattern sources (T1 p3, T2 p1) and steps",
              c == work["cur"] and m0[0] and m1[0], f"cur {c}; T1 {m0[1]}; T2 {m1[1]}")
    f2 = fx_of(fx)
    ctx.check("session 2: the autosaved working copy's FX slots and amount", f2 == work["fx"],
              f"{fmt(fx, f2)}, before {fmt(fx, work['fx'])}")
    for t, slots in exp.items():
        for slot, want in slots.items():
            P.launch(t + 1, slot + 1)
            ok, dt = CP.mem_is(P, t, want)
            ctx.check(f"session 2: pattern T{t + 1} {slot + 1} from the flash", ok and P.cur()[t] == slot, dt)
    for n, (a, b) in SCENE.items():
        P.scene_launch(n)
        c = P.cur()[:2]
        ok = c == [a, b] and CP.mem_is(P, 0, exp[0][a])[0] and CP.mem_is(P, 1, exp[1][b])[0]
        ctx.check(f"session 2: scene {'ABC'[n - 1]} from the flash (T1 p{a + 1}, T2 p{b + 1})", ok, f"cur {c}")
        if n == 1:
            sf = fx_of(fx)
            ctx.check("session 2: scene A's FX slots and amount (stored with COMP in S1)", sf == work["scene_fx"],
                      f"{fmt(fx, sf)}, stored {fmt(fx, work['scene_fx'])}")
    ab = e.mem("arrangement", 12)
    ctx.check("session 2: the song chain from the flash", ab[0] == 3 and list(ab[4:10]) == [0, 2, 1, 2, 2, 2],
              f"count {ab[0]}, parts {list(ab[4:10])}")
    P.scene_launch(1)                                 # (the work scrambled: scene A, the default layout)
    fx.scramble(1)
    ok = P.project_load(PROJECT_SLOT, shot="project-loaded")
    c, f3 = P.cur()[:2], fx_of(fx)
    ctx.check(f"session 2: PROJECT > LOAD slot {PROJECT_SLOT}: the working copy (T1 p3, T2 p1) and its FX",
              ok and c == work["cur"] and CP.mem_is(P, 0, exp[0][2])[0] and f3 == work["fx"],
              f"cur {c}, {fmt(fx, f3)}")
    song(ctx, e, P)


def song(ctx, e, P):
    """the song from the flash: A, B, C in order, then it stops"""
    P.layer("SAVE", [CP.W(16)])
    e.tap(X.F.BTN["OCTDN"], up=0.6)
    ctx.check("session 2: song mode on (the song page, OCT-)", e.peekb("arrangement_enabled")[0] == 1, "")
    e.log_on(*P.logsyms((0, 1)))
    e.tap(X.F.BTN["PLAY"], up=0.3)
    e.run(17.5)
    rows = [P.decode(r, (0, 1)) for r in e.take()]
    e.log_off()
    on = next((i for i, r in enumerate(rows) if r["playing"]), None)
    seq, last = [], None
    for r in rows[on:] if on is not None else []:
        if not r["playing"]:
            break
        k = tuple(r["cur"][:2])
        if k != last:
            seq.append(k)
            last = k
    ctx.check("session 2: the song plays A, B, C from the flash, then stops", seq == [(0, 0), (1, 1), (2, 0)] and
              not P.playing(), f"{seq}, playing {P.playing()}")
    P.stop()
    e.tap(X.F.BTN["OCTDN"], up=0.4)
    P.home()

# SPDX-License-Identifier: GPL-3.0-only
"""patterns: the patterns of docs/PATTERNS-DESIGN.md at the panel (SLOOP's UI). Builds T1 p1..p3 and T2 p1..p2 with
SEQ held, stores them (LFO held + black 6 + white n) and scenes A..C (SAVE held + white 5..7), then plays and logs
the sequencer per audio half: a launch at the pattern's end (the default), at the next bar (OCT- held) and now
(OCT+ held), a second track launched while the first plays, a scene launch (on the bar, both tracks together), the
song chain (A B C, 2 bars each, a launch inside part B ends with the part) and CLEAR. Every step played is checked
against the pattern in force (a hit where the pattern has one, its note, its index), and the audio onsets against
the hits (END, BAR, NOW)."""
import fwfacts as F
import session as S

W, BK = F.white, F.black
STEP_S = 60 / 90 / 4                                   # a step at 90 BPM (the default tempo)


def events(P, rows, t):
    """the steps track t played, at each change of its seq_abs, from the first row of a PLAY on"""
    out, last, started = [], None, False
    for r in rows:
        k = r["trk"][t]
        if not started:
            if r["playing"] and k["abs"] <= 1:
                started = True
            else:
                continue
        if k["abs"] != last:
            out.append(dict(k, beat=r["beat"], cur=r["cur"][t], time=r["t"]))
            last = k["abs"]
    return out


def play_ok(P, rows, t, exp):
    """every step event of track t: a hit iff the pattern in force (cur) has one at (abs - org) % LEN, its note,
    its index -> (steps checked, [what differs])"""
    bad, n = [], 0
    for ev in events(P, rows, t):
        if ev["abs"] == 0xFFFFFFFF or ev["cur"] == 255 or ev["cur"] not in exp[t]:
            continue
        ln, hits = exp[t][ev["cur"]]
        i = (ev["abs"] - ev["org"]) % ln
        n += 1
        if i != ev["idx"]:
            bad.append(("idx", ev["abs"], i, ev["idx"]))
        elif (ev["n"] > 0) != (i in hits):
            bad.append(("hit", ev["abs"], i, ev["n"], ev["cur"]))
        elif ev["n"] and ev["note"] != hits[i]:
            bad.append(("note", ev["abs"], i, ev["note"], hits[i]))
    return n, bad


def first_change(rows, t, field="cur"):
    for i in range(1, len(rows)):
        if rows[i][field][t] != rows[i - 1][field][t]:
            return i
    return None


def onsets(P, rows, t, wav, t0):
    """the hits of track t (step events with notes) against the onsets of the recorded audio -> (hits, onsets,
    hits with no onset within 120 ms, onsets with no hit)"""
    left, _ = S.read_wav(wav)
    win = int(0.004 * 44100)
    env = [S.rms(left[i:i + win]) for i in range(0, len(left) - win, win)]
    ons = []
    for i in range(3, len(env) - 3):
        if max(env[i:i + 3]) > 2.5 * max(env[i - 3:i]) + 0.003 and (not ons or i - ons[-1] > 12):
            ons.append(i)
    on_t = [t0 + o * win / 44100 for o in ons]
    hits = [ev["time"] for ev in events(P, rows, t) if ev["n"] and ev["time"] > t0 + 0.2]
    missed = [h for h in hits if not any(-0.012 <= o - h <= 0.12 for o in on_t)]
    extra = [o for o in on_t if o > t0 + 0.2 and not any(-0.012 <= o - h <= 0.12 for h in hits)]
    return len(hits), len(on_t), missed, extra


def build(ctx, e, P):
    """the content, by the panel -> EXP {track: {slot: (LEN, {idx: note})}}"""
    P.steps([1, 5, 9, 13], shot="t1p1")
    n0 = P.steps_of(0).get(0, 60)
    P.pat_store(1)
    P.steps([3, 7, 11, 15], note=12, shot="t1p2")
    P.pat_store(2)
    P.launch(1, 1)
    P.steps([], length=-4, shot="t1p3")
    P.pat_store(3)
    P.track(2)
    P.steps([2, 10], shot="t2p1")
    m0 = P.steps_of(1).get(1, 72)
    P.pat_store(1)
    P.steps([2, 10, 1, 5], length=-8, shot="t2p2")
    P.pat_store(2)
    exp = {0: {0: (16, {0: n0, 4: n0, 8: n0, 12: n0}), 1: (16, {0: n0, 2: n0 + 12, 4: n0, 6: n0 + 12, 8: n0,
                                                               10: n0 + 12, 12: n0, 14: n0 + 12}),
               2: (12, {0: n0, 4: n0, 8: n0})},
           1: {0: (16, {1: m0, 9: m0}), 1: (8, {0: m0, 4: m0})}}
    for w, (a, b) in ((1, (1, 1)), (2, (2, 2)), (3, (3, 1))):      # scenes A B C (stopped: a launch loads at once)
        P.launch(1, a)
        P.launch(2, b)
        P.scene_store(w, shot=f"scene-{'ABC'[w - 1]}")
    return exp


def mem_is(P, t, want):
    ln, hits = want
    got = P.param(t, "P_SLEN"), P.steps_of(t)
    return got[0] == ln and {k: v for k, v in got[1].items() if k < ln} == hits, f"LEN {got[0]} steps {got[1]}"


def run(ctx):
    e, P = ctx.open(fresh=True)
    try:
        body(ctx, e, P)
    finally:
        e.quit()


def body(ctx, e, P):
    exp = build(ctx, e, P)
    for t, slot in ((0, 0), (0, 1), (0, 2), (1, 0), (1, 1)):
        P.launch(t + 1, slot + 1)
        ok, d = mem_is(P, t, exp[t][slot])
        ctx.check(f"stored: T{t + 1} pattern {slot + 1} reads back (a stopped launch loads at once)", ok, d)
    for w, (a, b) in ((1, (0, 0)), (2, (1, 1)), (3, (2, 0))):
        P.scene_launch(w)
        c = P.cur()[:2]
        ok = c == [a, b] and mem_is(P, 0, exp[0][a])[0] and mem_is(P, 1, exp[1][b])[0]
        ctx.check(f"scene {'ABC'[w - 1]} loads its patterns (T1 {a + 1}, T2 {b + 1})", ok, f"cur {c}")
    tracks = (0, 1)

    def start(t1, t2, mute_t2):
        P.launch(1, t1 + 1)
        P.launch(2, t2 + 1)
        if bool(P.param(1, "P_MUTE")) != mute_t2:
            P.layer("GLO", [W(2)])
        e.log_on(*P.logsyms(tracks))
        P.play()

    def stop():
        P.stop()
        e.log_off()
        e.take()

    def rows_now():
        return [P.decode(r, tracks) for r in e.take()]

    # 1. END (default): p3 (LEN 12) -> p1 when p3 wraps
    start(2, 0, True)
    e.wavstart()
    w0 = e.now()
    e.run(0.8)
    P.wait_for(lambda: P.seq(0)[1] == 2)
    P.launch(1, 1, shot="end")
    e.run(4.5)
    rows = rows_now()
    e.wavstop(ctx.wav("end"))
    evs = events(P, rows, 0)
    sw = next((v for v in evs if v["cur"] == 0), None)
    prev = [v for v in evs if sw and v["abs"] < sw["abs"]][-1:] or [None]
    ctx.check("END: p1 starts at its step 1 when p3 wrapped (idx 11 before, org = abs)",
              sw and sw["idx"] == 0 and sw["org"] == sw["abs"] and prev[0] and prev[0]["idx"] == 11,
              f"first p1 step {sw and {k: sw[k] for k in ('abs', 'idx', 'org')}}, before {prev[0] and prev[0]['idx']}")
    ctx.check("END: not bar aligned (p3 is 12 steps)", sw and sw["abs"] % 16 != 0, f"abs {sw and sw['abs']}")
    n, bad = play_ok(P, rows, 0, exp)
    ctx.check(f"END: every step played matches its pattern ({n} steps)", n and not bad, str(bad[:3]))
    nh, no, missed, extra = onsets(P, rows, 0, ctx.wav("end"), w0)
    ctx.check(f"END: audio onsets at the hits ({nh} hits, {no} onsets)", nh and not missed and not extra,
              f"missed {missed[:3]}, extra {extra[:3]}")
    stop()

    # 2. BAR (OCT- held): p3 -> p1 on the next bar
    start(2, 0, True)
    e.wavstart()
    w0 = e.now()
    e.run(0.5)
    P.wait_for(lambda: P.seq(0)[0] >= 12 and P.seq(0)[1] == 0)
    P.launch(1, 1, when="bar", shot="bar")
    e.run(4.5)
    rows = rows_now()
    e.wavstop(ctx.wav("bar"))
    evs = events(P, rows, 0)
    iq = first_change(rows, 0, "req")
    areq = rows[iq]["trk"][0]["abs"] if iq is not None else None
    sw = next((v for v in evs if v["cur"] == 0), None)
    want = (areq // 16 + 1) * 16 if areq is not None else None
    ctx.check("BAR: p1 starts on the first bar after the request, at its step 1",
              sw and sw["abs"] == want and sw["idx"] == 0 and sw["org"] == sw["abs"],
              f"request at abs {areq}, switch at {sw and sw['abs']}, want {want}")
    cut = [v for v in evs if sw and v["abs"] < sw["abs"]][-1:] or [None]
    ctx.check("BAR: p3 left before its own end", cut[0] and cut[0]["idx"] != 11, f"last p3 idx {cut[0] and cut[0]['idx']}")
    n, bad = play_ok(P, rows, 0, exp)
    ctx.check(f"BAR: every step played matches its pattern ({n} steps)", n and not bad, str(bad[:3]))
    nh, no, missed, extra = onsets(P, rows, 0, ctx.wav("bar"), w0)
    ctx.check(f"BAR: audio onsets at the hits ({nh} hits, {no} onsets)", nh and not missed and not extra,
              f"missed {missed[:3]}, extra {extra[:3]}")
    stop()

    # 3. NOW (OCT+ held): p1 -> p2 at the next step, the position kept
    start(0, 0, True)
    e.wavstart()
    w0 = e.now()
    e.run(0.5)
    P.wait_for(lambda: P.seq(0)[1] == 5)
    P.launch(1, 2, when="now", shot="now")
    e.run(4.0)
    rows = rows_now()
    e.wavstop(ctx.wav("now"))
    evs = events(P, rows, 0)
    iq = first_change(rows, 0, "req")
    areq = rows[iq]["trk"][0]["abs"] if iq is not None else None
    sw = next((v for v in evs if v["cur"] == 1), None)
    org0 = evs[0]["org"] if evs else None
    ctx.check("NOW: p2 from the next step, the origin kept, the position going on",
              sw and areq is not None and sw["abs"] - areq <= 2 and sw["org"] == org0 and
              sw["idx"] == (sw["abs"] - org0) % 16, f"request at abs {areq}, switch {sw and sw['abs']}, org {org0}")
    n, bad = play_ok(P, rows, 0, exp)
    ctx.check(f"NOW: every step played matches its pattern ({n} steps)", n and not bad, str(bad[:3]))
    nh, no, missed, extra = onsets(P, rows, 0, ctx.wav("now"), w0)
    ctx.check(f"NOW: audio onsets at the hits ({nh} hits, {no} onsets)", nh and not missed and not extra,
              f"missed {missed[:3]}, extra {extra[:3]}")
    stop()

    # 4. T2 launched at its end while T1 plays on
    start(0, 0, False)
    e.run(0.4)
    P.wait_for(lambda: P.seq(0)[1] == 3)
    P.layer("LFO", [BK(2), W(2)], "t2-end")
    e.run(5.0)
    rows = rows_now()
    stop()
    sw = next((v for v in events(P, rows, 1) if v["cur"] == 1), None)
    ctx.check("two tracks: T2 p1 -> p2 at its end, T1 untouched",
              sw and sw["idx"] == 0 and sw["org"] == sw["abs"] and first_change(rows, 0) is None,
              f"T2 switch {sw and sw['abs']}, T1 changes at row {first_change(rows, 0)}")
    n0, b0 = play_ok(P, rows, 0, exp)
    n1, b1 = play_ok(P, rows, 1, exp)
    ctx.check(f"two tracks: both play their patterns (T1 {n0}, T2 {n1} steps)", n0 and n1 and not b0 + b1,
              str((b0 + b1)[:3]))

    # 5. a scene launched while playing: both tracks on the bar, at their step 1
    start(0, 0, False)
    e.run(0.4)
    P.wait_for(lambda: P.seq(0)[1] == 3)
    P.scene_launch(2, shot="scene-B-live")
    e.run(5.5)
    rows = rows_now()
    stop()
    i, i2 = first_change(rows, 0), first_change(rows, 1)
    ev0 = [v for v in events(P, rows, 0) if v["cur"] == 1][:1]
    ev1 = [v for v in events(P, rows, 1) if v["cur"] == 1][:1]
    ctx.check("scene: both tracks switch in the same half", i is not None and i == i2, f"T1 row {i}, T2 row {i2}")
    ctx.check("scene: on a bar, both at their step 1",
              ev0 and ev1 and ev0[0]["idx"] == 0 and ev1[0]["idx"] == 0 and i is not None and rows[i]["beat"] % 4 == 0,
              f"T1 {ev0[:1] and ev0[0]['idx']}, T2 {ev1[:1] and ev1[0]['idx']}, beat {i is not None and rows[i]['beat']}")
    n0, b0 = play_ok(P, rows, 0, exp)
    n1, b1 = play_ok(P, rows, 1, exp)
    ctx.check(f"scene: every step played matches ({n0} + {n1} steps)", n0 and n1 and not b0 + b1, str((b0 + b1)[:3]))

    song(ctx, e, P, exp, tracks)
    clear(ctx, e, P, exp)


def song_chain(e, P, parts, shot="song-page"):
    """the song page (SAVE held + white 16): the chain set, parts [(scene 0.., bars)] (KNOB4 the length, KNOB1 the
    entry, KNOB2 its scene, KNOB3 its bars), the page left open -> the arrangement's first 12 bytes (count, loop, -,
    -, then scene, bars a part)"""
    P.layer("SAVE", [W(16)], shot=shot)
    e.run(0.4)
    e.click("KNOB4", -20)
    e.click("KNOB4", len(parts) - 1)
    for ent, (sc, bars) in enumerate(parts):
        e.click("KNOB1", -20)
        e.click("KNOB1", ent)
        e.click("KNOB2", -20)
        e.click("KNOB2", sc)
        e.click("KNOB3", -64)
        e.click("KNOB3", bars - 1)
    return e.mem("arrangement", 12)


def song(ctx, e, P, exp, tracks):
    """6. the chain A B C, 2 bars each, in song mode, played once; a NOW launch inside part B lasts until the part ends"""
    ab = song_chain(e, P, ((0, 2), (1, 2), (2, 2)))
    ctx.check("song: the chain as set on the song page (A 2 bars, B 2, C 2)", ab[0] == 3 and list(ab[4:10]) ==
              [0, 2, 1, 2, 2, 2], f"count {ab[0]}, entries {list(ab[4:10])}")
    e.tap(F.BTN["OCTDN"], up=0.6)
    P.shot("song-mode")
    ctx.check("song: OCT- on the song page turns song mode on", e.peekb("arrangement_enabled")[0] == 1,
              f"arrangement_enabled {e.peekb('arrangement_enabled')[0]}")
    e.log_on(*(P.logsyms(tracks) + ["arrangement_clock:2"]))
    e.tap(F.BTN["PLAY"], up=0.3)
    P.wait_for(lambda: e.peekb("arrangement_clock+4")[0] == 1, timeout=8)
    P.wait_for(lambda: P.cur()[0] == 1, timeout=12)
    P.wait_for(lambda: P.seq(0)[1] == 4, timeout=4)
    P.launch(1, 1, when="now", shot="song-launch")
    e.run(14.0)
    raw = e.take()
    rows = [dict(P.decode(r, tracks), part=r["arrangement_clock"][1] >> 8 & 255) for r in raw]
    on = next((i for i, r in enumerate(rows) if r["playing"]), None)
    end = next((i for i, r in enumerate(rows) if on is not None and i > on and not r["playing"]), None)
    parts, last = [], None
    for r in rows[on:end] if on is not None else []:
        k = (r["cur"][0], r["cur"][1], r["part"])
        if k != last:
            parts.append((round(r["t"], 3), k))
            last = k
    ctx.check("song: A, B, then T1 p1 inside B, then C (the launch ends with the part)",
              [k for _, k in parts] == [(0, 0, 0), (1, 1, 1), (0, 1, 1), (2, 0, 2)], f"(T1, T2, part) {parts}")
    # (the chain does not loop (arr_defaults: loop 0): after its last part the song stops and seq_stop's
    # song_restore brings back the loop that played before it: scene B, from section 5)
    back = rows[end] if end is not None else None
    ctx.check("song: after C it stops, the loop before it back (scene B: T1 p2, T2 p2)",
              back is not None and back["cur"][:2] == [1, 1] and not e.peekb("arrangement_clock+4")[0],
              f"stopped at {back and back['t']}, cur {back and back['cur'][:2]}")
    bounds = [t for i, (t, k) in enumerate(parts) if i and parts[i - 1][1][2] != k[2]] + ([back["t"]] if back else [])
    gaps = [round(b - a, 3) for a, b in zip(bounds, bounds[1:])]
    ctx.check("song: each part 2 bars (5.333 s at 90 BPM, within a half)",
              gaps and all(abs(g - 32 * STEP_S) < 0.015 for g in gaps), f"gaps {gaps}")
    n0, b0 = play_ok(P, rows, 0, exp)
    n1, b1 = play_ok(P, rows, 1, exp)
    ctx.check(f"song: every step played matches the pattern in force ({n0} + {n1} steps)", n0 and not b0 + b1,
              str((b0 + b1)[:3]))
    e.log_off()
    e.take()
    P.stop()
    e.tap(F.BTN["OCTDN"], up=0.4)                        # (loop mode again)
    P.home()


def clear(ctx, e, P, exp):
    """7. CLEAR (stopped): a stored slot, then a slot a scene plays (the scene plays the track empty)"""
    P.track(1)
    P.launch(1, 1)
    P.pat_store(5)
    ctx.check("clear: T1 slot 5 stored (a copy of p1)", P.cur()[0] == 4, f"cur {P.cur()}")
    P.pat_clear(5, shot="clear-p5")
    P.launch(1, 5)
    ctx.check("clear: slot 5 is empty after CLEAR (launching it plays nothing)", not P.steps_of(0),
              f"T1 steps {P.steps_of(0)}")
    P.pat_clear(3, shot="clear-p3")
    P.scene_launch(3)
    ctx.check("clear: scene C after CLEAR of T1 p3: T1 empty, T2 its p1",
              not P.steps_of(0) and P.steps_of(1) == exp[1][0][1], f"T1 {P.steps_of(0)}, T2 {P.steps_of(1)}")

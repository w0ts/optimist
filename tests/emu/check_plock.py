# SPDX-License-Identifier: GPL-3.0-only
"""plock: a p-lock on one step, and LFO 2 to the filter (docs/RELEASE-0.2-TEST.md section 2, its P-lock and LFO 2
lines, on T1's ANALOG; op_step.c held steps, voice.c LFO 2). Fresh boot, T1 on a bright ANALOG preset (PRESETS to the
first BRASS, LEAD or SAW). STEP: steps 1 5 9 13; step 5 held, ENV tapped (its ENV page: ATK DEC SUS REL as
p-locks), KNOB 1 up (ATK), step 5 let go (the track's own values, trk[0].p[], untouched). PLAY, 4 s recorded with
clk_beat logged (each 16th's time in the WAV): each note's level (RMS of its first 100 ms); step 5's differs on every
pass, apart from steps 1 9 13's. Stopped, the steps cleared (each step key tapped again), white key 5 held 2.5 s: the
brightness (the first difference's RMS over the RMS) over 40 ms windows, its spread; LFO tapped, SELECT to LFO2
DEST, KNOB 1 to FLT (DST A), KNOB 2 up (AMT A): the same note again, the spread grows (the filter moves)."""
import fwfacts as F
import opui
import session as S

BRIGHT = ("BRASS", "LEAD", "SAW")
STEPS = (1, 5, 9, 13)
FS = 44100
BEAT_U = 44100 * 60                                     # (core.h: a beat in clock units)


def mono(path):
    left, right = S.read_wav(path)
    return [(a + b) / 2 for a, b in zip(left, right)]


def bright(x):
    r = S.rms(x) or 1e-9
    return S.rms([x[i] - x[i - 1] for i in range(1, len(x))]) / r


def names(u):
    return [n for n in sorted(u.f.P, key=u.f.P.get) if n != "P_COUNT"]


def values(u):
    return [u.param(0, n) for n in names(u)]


def pick(u, words):
    for _ in range(30):
        u.e.click("PRESETS", 1)
        if any(w in u.msg() for w in words):
            break
    u.e.run(1.5)
    return u.msg()


def run(ctx):
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        sound = pick(u, BRIGHT)
        plock(ctx, u, sound)
        lfo2(ctx, u)
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()


def plock(ctx, u, sound):
    e = u.e
    u.tap("SEQ")
    for k in STEPS:
        u.key(F.white(k))
    e.hold(F.white(5))
    e.run(0.6)
    u.tap("ENV", up=0.3)                                # (a step held + a page button: its family as locks; EDIT is
    v0 = values(u)                                      #  not one on a synth track: op_input.c op_press)
    u.turn("KNOB1", 20)                                 # (ATK up: a slow start on that step)
    u.shot("step5-locked")
    e.release(F.white(5))
    e.run(0.4)
    v1 = values(u)
    u.home()
    u.tap("PLAY", up=0.3)
    e.run(0.5)
    e.log_on("clk_beat:2")
    t0 = e.now()
    u.record(ctx.wav("plock"), 4.0)
    rows = e.take()
    e.log_off()
    u.tap("PLAY", up=0.5)
    x = mono(ctx.wav("plock"))
    start = {}                                          # (the 16th of the transport grid -> its first time in the WAV)
    for r in rows:
        s = r["clk_beat"][0] * 4 + r["clk_beat"][1] * 4 // BEAT_U
        start.setdefault(s, int((r["t"] - t0) * FS))
    lv = {s: S.rms(x[o:o + int(0.1 * FS)]) for s, o in start.items() if s % 4 == 0 and o + int(0.1 * FS) < len(x)}
    lk = [v for s, v in lv.items() if s % 16 == 4]
    ot = [v for s, v in lv.items() if s % 16 != 4]
    ok = len(lk) >= 2 and len(ot) >= 4
    apart = ok and (max(lk) < 0.5 * min(ot) or min(lk) > 1.5 * max(ot))     # (the two ranges apart)
    changed = [n for n, a, b in zip(names(u), v0, v1) if a != b and n != "P_VOICES"]   # (0 written as the engine's max
    #  when a page with VOICES is described: params.c page_desc, the same count)
    ctx.check("step 5 p-locked (ENV, ATK): its note apart from the others on every pass; the track's own "
              "values untouched", apart and not changed,
              f"{sound}, steps 5: {[round(v, 4) for v in lk]}, steps 1 9 13: {[round(v, 4) for v in ot]}, "
              f"apart {apart}, track values changed {changed}")
    u.tap("SEQ")
    for k in STEPS:
        u.key(F.white(k))
    u.home()


def held_note(u, path):
    e = u.e
    e.align()
    e.wavstart()
    e.hold(F.white(5))
    e.run(2.5)
    e.release(F.white(5))
    e.run(0.2)
    e.wavstop(path)
    x = mono(path)[int(0.3 * FS):int(2.5 * FS)]
    win = int(0.04 * FS)
    w = [bright(x[i:i + win]) for i in range(0, len(x) - win, win)]
    m = sum(w) / len(w)
    return (sum((v - m) ** 2 for v in w) / len(w)) ** 0.5 / m


def lfo2(ctx, u):
    still = held_note(u, ctx.wav("lfo2-off"))
    u.tap("LFO")
    for _ in range(4):
        if u.page() == "LFO2 DEST":
            break
        u.turn("SELECT", 1, wait=0.25)
    page = u.page()
    for _ in range(6):
        if u.param(0, "P_L2DA") == FLT:
            break
        u.e.click("KNOB1", 1)
    u.turn("KNOB2", 12)
    u.shot("lfo2-dest")
    da, aa = u.param(0, "P_L2DA"), u.param(0, "P_L2AA")
    u.home()
    moving = held_note(u, ctx.wav("lfo2-on"))
    ctx.check("LFO 2 to FLT (LFO2 DEST: DST A, AMT A up): the held note's brightness moves", page == "LFO2 DEST" and
              da == FLT and aa != 0 and moving > 2 * still,
              f"page {page!r}, P_L2DA {da}, P_L2AA {aa}, brightness spread {still:.3f} -> {moving:.3f}")


FLT = 2                                                 # (LFO DEST's list: OFF PIT FLT ...: core/params.c LT_*)

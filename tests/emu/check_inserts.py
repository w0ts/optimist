# SPDX-License-Identifier: GPL-3.0-only
"""inserts: every insert type this build has, set in T1's slot S1 from the mixer, changes the sound; S1 back to ----
gives the dry sound again; the effects memory's limited types (CHRS, JUNO CHORUS, FLNG, XDLY, ECHO) on T1, T2 and T3:
how many land (docs/RELEASE-0.2-TEST.md sections 2 and 7; fx_slots.c ins_slot / ins_list, op_cells.c ins_skip).

Fresh boot, the mixer, T1 on PRESETS +3 (a sound with highs), SELECT +1 (T1's insert set: S1 TYPE, amount, S2 TYPE,
amount). KNOB 1 to ---- (the list's first entry), a 3-note chord held 1.2 s and 0.6 s of tail recorded twice (dry, and
dry again: the method's own spread). Then KNOB 1 one detent at a time through the list: for each type (ins_slot[0][0],
named from ins_names), KNOB 2 +12 (its amount up), the chord recorded; changed when one of RMS, brightness (the first
difference's RMS over the RMS), the stereo side (RMS of L-R over L+R), the level's movement (10 ms blocks), the tail
or the waveform itself (RMS of the difference to the dry take over the dry's: the emulator replays an aligned take
sample for sample) moves past its bar (CHANGE; the waveform WAVE_CHANGE) and past 3 x the dry spread. Last, KNOB 1
back to ----: within the bar (the dry sound again).
The limited types: on T1, T2, T3 in turn (ALGORITHM walks the mixer's rows), KNOB 1 stepped until S1 holds the type
or the list ends: the count that landed (expected from the builder's notice, docs/RELEASE-0.2-TEST.md: A 0, B 9 at
once, C CHRS / JUNO 2, FLNG 5) is printed; the check asserts that a refused type never lands in a slot and the
audio stays clean."""
import fwfacts as F
import opui
import session as S

CHORD = (F.white(1), F.white(3), F.white(5))
CHANGE = 0.12                                           # (the bar of RMS, brightness, side, movement, tail)
WAVE_CHANGE = 0.05                                      # (the waveform's: 5 % of the dry take's RMS)
LIMITED = ("CHRS", "JUNO", "FLNG", "XDLY", "ECHO")
NEUTRAL = ("EQ3",)                                      # (LO MID HI at 0 dB: no change until its page is set)


def metrics(left, right, n_tail):
    both = [v for p in zip(left, right) for v in p]
    body_l, body_r = left[:-n_tail], right[:-n_tail]
    mono = [(a + b) / 2 for a, b in zip(body_l, body_r)]
    side = [(a - b) / 2 for a, b in zip(body_l, body_r)]
    r = S.rms(mono) or 1e-9
    diff = S.rms([mono[i] - mono[i - 1] for i in range(1, len(mono))])
    blocks = [S.rms(mono[i:i + 441]) for i in range(0, len(mono) - 441, 441)]
    mean = sum(blocks) / len(blocks) if blocks else 1e-9
    move = (sum((b - mean) ** 2 for b in blocks) / len(blocks)) ** 0.5 / (mean or 1e-9) if blocks else 0.0
    return dict(rms=r, bright=diff / r, side=S.rms(side) / r, move=move,
                tail=S.rms(left[-n_tail:] + right[-n_tail:]) / r, peak=max(abs(v) for v in both),
                full=sum(1 for v in both if abs(v) >= 0.999), x=both)


def play(u, path):
    e = u.e
    e.align()
    e.wavstart()
    e.hold(*CHORD)
    e.run(1.2)
    e.release(*CHORD)
    e.run(0.6)
    left, right = e.wavstop(path)
    return metrics(left, right, int(0.5 * 44100))


def distance(a, b):
    """the largest relative change of the metrics (tail and side: against the body's level, absolute)"""
    out = {}
    for k in ("rms", "bright"):
        out[k] = abs(a[k] - b[k]) / max(abs(b[k]), 1e-9)
    for k in ("side", "move", "tail"):
        out[k] = abs(a[k] - b[k])
    n = min(len(a["x"]), len(b["x"]))                   # (the waveform itself: a replay after align() is sample-exact,
    out["wave"] = S.rms([a["x"][i] - b["x"][i] for i in range(n)]) / (S.rms(b["x"][:n]) or 1e-9)   # dry vs dry: 0)
    score = {k: v / (WAVE_CHANGE if k == "wave" else CHANGE) for k, v in out.items()}
    k = max(score, key=score.get)
    return score[k], k, out[k]                          # (score 1: the change at its metric's bar)


def slot(e, k):
    return e.mem("ins_slot", 8)[2 * k]


def list_names(e):
    """ins_list (FXT ids) and their names (ins_names: pointers to the const labels)"""
    a, size = e.sym("ins_list")
    ids = list(e.mem(f"{a:#x}", size))
    pa, _ = e.sym("ins_names")
    names = []
    for i in range(len(ids)):
        p = e.word(f"{pa + 4 * i:#x}")
        names.append(e.rodata(p, 8).split(b"\0")[0].decode("latin1") if p else "?")
    return ids, names


def to_none(u, k):
    for _ in range(40):
        if slot(u.e, k) == 0:
            return True
        u.e.click("KNOB1", -1)
    return slot(u.e, k) == 0


def step_to(u, k, t, ids):
    """KNOB 1 +1 a detent from ---- until track k's S1 holds type t (True) or stops moving (False); None: KNOB 1
    does not reach track k's S1 (not its insert set)"""
    if not to_none(u, k):
        return None
    for _ in range(len(ids) + 2):
        before = slot(u.e, k)
        u.e.click("KNOB1", 1)
        now = slot(u.e, k)
        if now == t:
            return True
        if now == before:
            return False
    return slot(u.e, k) == t


def run(ctx):
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        u.turn("PRESETS", 3, wait=1.0)
        u.turn("SELECT", 1)
        ids, names = list_names(e)
        ctx.info("the insert list", " ".join(names))
        to_none(u, 0)
        dry = play(u, ctx.wav("dry"))
        dry2 = play(u, ctx.wav("dry2"))
        spread, _, _ = distance(dry2, dry)
        ctx.info("dry", f"rms {dry['rms']:.4f} bright {dry['bright']:.3f} side {dry['side']:.3f} move {dry['move']:.3f} "
                 f"tail {dry['tail']:.3f}; dry again: spread {spread:.3f} of the bar")
        limit = max(1.0, 3 * spread)
        seen = [0]
        for _ in range(len(ids)):
            e.click("KNOB1", 1)
            t = slot(e, 0)
            if t == seen[-1]:
                break                                   # (the list's end)
            seen.append(t)
            name = names[ids.index(t)] if t in ids else f"#{t}"
            u.turn("KNOB2", 12)
            m = play(u, ctx.wav(f"ins-{name}"))
            d, why, raw = distance(m, dry)
            u.shot(f"ins-{name}")
            if name in NEUTRAL:                         # (flat by default: its settings are on its own page)
                ctx.info(f"S1 {name}", f"neutral at its defaults, as built: largest change {why} {raw:.3f}, "
                         f"full-scale {m['full']}")
                continue
            ctx.check(f"S1 {name}: the sound changes", d > limit and m["full"] == 0,
                      f"largest change {why} {raw:.3f} ({d:.1f} x its bar, {limit:.1f} needed), rms {m['rms']:.4f}, "
                      f"peak {m['peak']:.3f}, full-scale {m['full']}")
        skipped = [names[i] for i, t in enumerate(ids) if t not in seen]
        ctx.info("types KNOB 1 passed over (not available to S1 now: ins_skip)", " ".join(skipped) or "none")
        to_none(u, 0)
        off = play(u, ctx.wav("off"))
        d, why, raw = distance(off, dry)
        ctx.check("S1 back to ----: the dry sound again", d <= limit,
                  f"largest change {why} {raw:.3f} ({d:.2f} x its bar)")
        limited(ctx, u, ids, names)
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()


def limited(ctx, u, ids, names):
    e = u.e
    for name in LIMITED:
        if name not in names:
            continue
        t = ids[names.index(name)]
        landed = []
        for k in range(3):
            if k:
                u.turn("ALGORITHM", 1, wait=0.4)        # (the mixer's next row: T2, T3)
            if u.sel() != k:
                ctx.check(f"{name}: ALGORITHM to T{k + 1}", False, f"sel {u.sel()}")
                break
            landed.append(step_to(u, k, t, ids))
            u.shot(f"{name}-T{k + 1}")
        held = [slot(e, k) == t for k in range(3)]
        ctx.info(f"{name} on T1 T2 T3", f"landed {landed} (S1 holds it: {held})")
        ctx.check(f"{name}: KNOB 1 reaches each track's S1; a refused one never lands in a slot",
                  None not in landed and held == landed + [False] * (3 - len(landed)), f"landed {landed}, held {held}")
        for k in range(u.sel(), -1, -1):                # (back to ---- on each, T3 first, ALGORITHM back to T1)
            to_none(u, k)
            if k:
                u.turn("ALGORITHM", -1, wait=0.4)

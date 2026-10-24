# SPDX-License-Identifier: GPL-3.0-only
"""grids: GRIDS over the drum kit (FELUCCA_GEN_GRIDS; docs/RELEASE-0.2-TEST.md section 5; seq/gen.c). The pattern the
generator plays is read step by step from the firmware: gen_dcur (the drum step it made: lanes on, levels) logged
every audio half with clk_beat and clk_pos (the transport grid's 16th), so a pass is the 16 lane masks it played.

Fresh boot, ALGORITHM +3 (DR), SEQ (STEP: PATTERN, TOOLS, Grids, Density, GEN out). PLAY on the empty pattern:
nothing. GEN out: KNOB 1 (GEN ON): the kit plays a pattern; with CHAOS 0 every pass the same; VEL (Density KNOB 4):
ON, soft and ghost hits among them; OFF, none (NORM, HARD over 192). Grids: X +20, then Y +20: each a different pattern. CHAOS
+40: the passes differ; SHIFT + KNOB 3 (CHAOS back to 0): the same again. Density: BD, SD, HH each down and up:
that part's hits fewer and more (its lane: the one whose count follows), then back to its default (SHIFT + the
knob). OUT (Grids KNOB 4) EUCL (gr_out 1), X 12 detents down and Y 10 up from their defaults: the kick's hits repeat
with the length X sets (gen.c gr_eu_len), the snare's with Y's. Last, the project with GRIDS on,
EUCL and X set saved as 7, another X as 8, 7 loaded: GEN on, EUCL and X back. Skipped without GEN_GRIDS."""
import opui

GRIDS_ROW, DENSITY_ROW, GENOUT_ROW = 2, 3, 4
STEP_S = 60 / 120 / 4                                   # (a 16th at 120 BPM)
BEAT_U = 44100 * 60                                     # (core.h: a beat in clock units)
LV_NORM, LV_GHOST, LV_SOFT, LV_HARD = range(4)          # (core.h)


def row(u, n):
    for _ in range(12):
        r = u.ub(opui.UI_ROW + opui.SCR["STEP"])[0]
        if r == n and u.cards_t() > 0:
            return True
        u.turn("SELECT", 1 if n > r else -1 if n < r else 1, wait=0.25)
    return u.ub(opui.UI_ROW + opui.SCR["STEP"])[0] == n


def reset(u, n, k):
    """SHIFT + KNOB k on STEP row n (the cards up): the value back to its default"""
    row(u, n)
    u.with_held("OCTDN", lambda: u.turn(f"KNOB{k}", 1, wait=0.25))


def knob(u, n, k, d):
    """KNOB k turned by d on STEP row n (the cards up)"""
    row(u, n)
    u.turn(f"KNOB{k}", d, wait=0.25)


def steps(u, bars):
    """bars x 16 steps played -> {seq_abs: (lanes on, levels word)}: the last value seen while that step played"""
    e = u.e
    e.log_on("gen_dcur:2", "clk_beat:2")
    e.run(bars * 16 * STEP_S + 0.05)
    rows = e.take()
    e.log_off()
    out = {}
    for r in rows:
        a = r["clk_beat"][0] * 4 + r["clk_beat"][1] * 4 // BEAT_U      # (the 16th of the transport grid)
        w0, w1 = r["gen_dcur"][0], r["gen_dcur"][1]
        out[a] = (w0 & 0xFFFF, (w0 >> 16) | ((w1 & 0xFFFF) << 16))
    return out


def passes(st):
    """{abs: (on, lvl)} -> the whole passes of 16 as tuples of lane masks, in order"""
    keys = sorted(k for k in st if k < 0x7FFFFFFF)
    if not keys:
        return []
    first = (keys[0] + 15) // 16 * 16
    out = []
    while all(first + i in st for i in range(16)):
        out.append(tuple(st[first + i][0] for i in range(16)))
        first += 16
    return out


def hits(ps, lane):
    return sum(1 for p in ps for m in p if m >> lane & 1)


def period(seq):
    for p in range(1, len(seq) // 2 + 1):
        if all(seq[i] == seq[i + p] for i in range(len(seq) - p)):
            return p
    return None


def run(ctx):
    if not ctx.facts.on("GEN_GRIDS"):
        ctx.info("skipped", "FELUCCA_GEN_GRIDS 0")
        return
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        main(ctx, e, u)
    finally:
        e.quit()


def main(ctx, e, u):
    u.turn("ALGORITHM", 3, wait=0.5)
    u.tap("SEQ")
    u.tap("PLAY", up=0.5)
    off = passes(steps(u, 4))
    ctx.check("DR, PLAY, GEN off: the empty pattern plays nothing", off and all(m == 0 for p in off for m in p),
              f"{len(off)} passes, masks {off[:1]}")
    knob(u, GENOUT_ROW, 1, 1)
    gl = e.word("gen_lanes")
    u.shot("gen-on")
    p0 = passes(steps(u, 4))
    ctx.check("GEN out: GEN ON: a GRIDS pattern plays, the same each pass (CHAOS 0)",
              gl and p0 and any(p0[0]) and all(p == p0[0] for p in p0),
              f"gen_lanes {gl:#x}, {len(p0)} passes, hits/pass {sum(bin(m).count('1') for m in p0[0]) if p0 else 0}")
    base = p0[0] if p0 else ()
    vel(ctx, u)
    knob(u, GRIDS_ROW, 1, 20)
    px = passes(steps(u, 3))
    knob(u, GRIDS_ROW, 2, 20)
    py = passes(steps(u, 3))
    u.shot("grids-xy")
    ctx.check("Grids X +20, then Y +20: each a different pattern", px and py and px[-1] != base and py[-1] != px[-1],
              f"X {u.param(3, 'P_E2')}, Y {u.param(3, 'P_E3')}")
    knob(u, GRIDS_ROW, 3, 40)
    pc = passes(steps(u, 4))
    reset(u, GRIDS_ROW, 3)
    c0 = u.param(3, "P_E4")
    pz = passes(steps(u, 4))
    ctx.check("CHAOS up: the passes differ; CHAOS back to 0 (SHIFT + KNOB 3): every pass the same",
              len(set(pc)) > 1 and c0 == 0 and pz and len(set(pz)) == 1,
              f"{len(set(pc))} different of {len(pc)} with CHAOS, CHAOS now {c0}, {len(set(pz))} of {len(pz)} after")
    density(ctx, u)
    eucl(ctx, u)
    saved(ctx, u)
    late, boots, crash = u.health()
    ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
              f"late {late}, boots {boots}, crash {crash}")


def density(ctx, u):
    for k, part in ((1, "BD"), (2, "SD"), (3, "HH")):
        knob(u, DENSITY_ROW, k, -40)
        lo = passes(steps(u, 3))
        knob(u, DENSITY_ROW, k, 80)
        hi = passes(steps(u, 3))
        u.shot(f"density-{part}")
        reset(u, DENSITY_ROW, k)
        d = [(hits(hi, lane) - hits(lo, lane)) for lane in range(10)]
        lane = max(range(10), key=lambda i: d[i])
        ctx.check(f"Density {part}: down then up: fewer then more hits in its part", d[lane] > 0,
                  f"lane {lane + 1}: {hits(lo, lane)} -> {hits(hi, lane)} hits over {len(hi)} passes; all lanes {d}")


def levels(st):
    """the distinct levels of the hits played: lane k's 2 bits in the level word"""
    out = set()
    for on, lv in st.values():
        for k in range(8):
            if on >> k & 1:
                out.add(lv >> (2 * k) & 3)
    return out


def vel(ctx, u):
    if not ctx.facts.on("GRIDS_VEL"):
        ctx.info("VEL", "not in this build")
        return
    on = levels(steps(u, 2))
    knob(u, DENSITY_ROW, 4, -1)
    off_flag = u.e.mem("gr_vel_off", 1)[0]
    off = levels(steps(u, 2))
    u.shot("vel-off")
    knob(u, DENSITY_ROW, 4, 1)
    soft = {LV_GHOST, LV_SOFT}
    ctx.check("VEL ON: soft / ghost hits among the others; VEL OFF: none (NORM, and HARD over 192: gen.c "
              "gr_kit_merge)", on & soft and len(on) > 1 and not off & soft and off_flag,
              f"levels ON {sorted(on)}, OFF {sorted(off)} (0 NORM 1 GHOST 2 SOFT 3 HARD; gr_vel_off {off_flag})")
    if len(off) > 1:
        ctx.info("VEL OFF", "NORM and HARD both play (the checklist's 'OFF plays all hits HARD' is not what gen.c does)")


def eucl(ctx, u):
    if not ctx.facts.on("GRIDS_EUCLID"):
        ctx.info("EUCL", "not in this build")
        return
    knob(u, GRIDS_ROW, 4, 1)
    out = u.e.mem("gr_out", 1)[0]
    for k, d in ((1, -12), (2, 10)):                    # (X and Y from their defaults: two different lengths)
        reset(u, GRIDS_ROW, k)
        row(u, GRIDS_ROW)
        u.e.click(f"KNOB{k}", d)
    x, y = u.param(3, "P_E2"), u.param(3, "P_E3")
    st = steps(u, 6)
    u.shot("eucl")
    keys = sorted(k for k in st if k < 0x7FFFFFFF)
    seqs = {lane: [st[k][0] >> lane & 1 for k in keys] for lane in range(10)}

    def eu_len(v):
        g = 0 if v <= 0 else 255 if v >= 127 else v * 2 + (v >> 6)
        return (g >> 3) + 1
    per = {lane: period(s) for lane, s in seqs.items() if any(s)}
    want = (eu_len(x), eu_len(y))
    ctx.check("OUT EUCL: the parts loop with the lengths X and Y set (kick, snare)", out == 1 and
              want[0] in per.values() and want[1] in per.values(),
              f"gr_out {out}, X {x} -> {want[0]} steps, Y {y} -> {want[1]} steps; periods by lane {per}")


def saved(ctx, u):
    def save(key):
        u.home()
        u.tap("SAVE")
        u.turn("KNOB2", 1)
        u.key(opui.white(key))
        u.yes()
        u.yes(up=2.0)
        return u.msg()
    u.tap("PLAY", up=0.5)
    x = u.param(3, "P_E2")
    m7 = save(7)
    u.to_mixer()
    u.tap("SEQ")
    knob(u, GRIDS_ROW, 1, -15)
    x8 = u.param(3, "P_E2")
    m8 = save(8)
    u.key(opui.white(7))
    asked = u.armed()
    u.yes(up=2.0)
    gl, out = u.e.word("gen_lanes"), u.e.mem("gr_out", 1)[0]
    ctx.check("saved as project 7 (GEN on, EUCL, X), another X as 8, LOAD 7: GEN on, EUCL and X back",
              m7.startswith("SAVED") and m8.startswith("SAVED") and asked and gl and
              out == (1 if ctx.facts.on("GRIDS_EUCLID") else 0) and u.param(3, "P_E2") == x and x8 != x,
              f"saves {m7!r} {m8!r}, asked {asked}, gen_lanes {gl:#x}, gr_out {out}, X {u.param(3, 'P_E2')} (saved {x}, "
              f"project 8 {x8})")

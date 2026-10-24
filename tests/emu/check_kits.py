# SPDX-License-Identifier: GPL-3.0-only
"""kits: the drum kits KAWA (47) and PLAITS (48), and the X0X 909 / 808 where built: every lane sounds, the lanes
differ; drum EDIT on a PLAITS lane changes it; a project saved with KAWA comes back with it (docs/RELEASE-0.2-TEST.md
section 4; drums.c, the kit: the drum track's P_E0).

Fresh boot, the mixer, ALGORITHM +3 (DR). PRESETS walks the kits one detent at a time (the toast names each: KIT
KAWA ...) to each kit wanted. Stopped, each black key 1..10 tapped picks and previews its lane: 0.7 s recorded per
lane, its peak (> SOUNDS) and its brightness / level; no two lanes alike. PLAITS: lane 1 picked, EDIT (the lane's
MAIN: TUNE DECAY CUT LEVEL), KNOB 1..3 each turned and the preview recorded again: each changes it. KAWA: saved as
project 5, another kit saved as project 6 (which opens it), project 5 loaded (asked): the kit is KAWA again."""
import fwfacts as F
import opui
import session as S

SOUNDS = 0.005
WANT = ("KAWA", "PLAITS", "X0X 909", "X0X 808")


def kit(u):
    return u.param(3, "P_E0")


def walk(u):
    """every kit PRESETS reaches from the first: {name: number}"""
    e, found = u.e, {}
    for _ in range(80):
        e.click("PRESETS", -1)
    last = None
    for _ in range(80):
        k = kit(u)
        if k == last:
            break
        found.setdefault(u.msg().replace("KIT ", "", 1), k)
        last = k
        e.click("PRESETS", 1)
    return found


def to_kit(u, n):
    for _ in range(80):
        k = kit(u)
        if k == n:
            return True
        u.e.click("PRESETS", 1 if n > k else -1)
    return kit(u) == n


def preview(u, path, lane):
    e = u.e
    e.wavstart()
    e.tap(F.black(lane), down=0.12, up=0.6)
    left, right = e.wavstop(path)
    x = [(a + b) / 2 for a, b in zip(left, right)]
    r = S.rms(x) or 1e-9
    d = S.rms([x[i] - x[i - 1] for i in range(1, len(x))])
    return max(abs(v) for v in x), r, d / r


def lanes(ctx, u, name):
    out = []
    for lane in range(1, 11):
        out.append(preview(u, ctx.wav(f"{name.replace(' ', '')}-lane{lane}"), lane))
    u.shot(f"kit-{name.replace(' ', '')}")
    silent = [i + 1 for i, m in enumerate(out) if m[0] <= SOUNDS]
    alike = [(i + 1, j + 1) for i in range(10) for j in range(i + 1, 10)
             if abs(out[i][1] - out[j][1]) < 1e-4 and abs(out[i][2] - out[j][2]) < 1e-3]
    ctx.check(f"kit {name}: all 10 lanes sound, no two alike", not silent and not alike,
              "peaks " + " ".join(f"{m[0]:.3f}" for m in out) + f"; silent {silent}, alike {alike}")
    return out


def edit_plaits(ctx, u):
    e = u.e
    base = preview(u, ctx.wav("plaits-edit-base"), 1)
    u.tap("EDIT")
    u.shot("plaits-lane1-edit")
    for k, name in ((1, "TUNE"), (2, "DECAY"), (3, "CUT")):
        u.turn(f"KNOB{k}", -6)
        m = preview(u, ctx.wav(f"plaits-edit-{name}"), 1)
        ch = max(abs(m[1] - base[1]) / base[1], abs(m[2] - base[2]) / base[2])
        ctx.check(f"PLAITS lane 1: EDIT KNOB {k} ({name}) changes the sound", ch > 0.05,
                  f"rms {base[1]:.4f} -> {m[1]:.4f}, brightness {base[2]:.3f} -> {m[2]:.3f}")
        u.turn(f"KNOB{k}", 6)
    u.home()
    e.run(0.3)


def save_slot(u, key):
    u.tap("SAVE")
    u.turn("KNOB2", 1)
    u.key(F.white(key))
    u.yes()
    u.yes(up=2.0)
    msg = u.msg()
    u.to_mixer()
    return msg


def run(ctx):
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        u.turn("ALGORITHM", 3, wait=0.5)
        ctx.check("ALGORITHM +3: DR selected", u.sel() == 3, f"sel {u.sel()}")
        kits = walk(u)
        ctx.info("kits", ", ".join(f"{k} {v}" for k, v in sorted(kits.items(), key=lambda x: x[1])))
        for name in WANT:
            if name not in kits:
                ctx.info(f"kit {name}", "not in this build")
                continue
            to_kit(u, kits[name])
            lanes(ctx, u, name)
            if name == "PLAITS":
                edit_plaits(ctx, u)
        if "KAWA" in kits:
            to_kit(u, kits["KAWA"])
            m5 = save_slot(u, 5)
            u.turn("ALGORITHM", 3 - u.sel(), wait=0.4)
            to_kit(u, max(v for v in kits.values() if v != kits["KAWA"]))   # (another kit this build has)
            m6 = save_slot(u, 6)
            other = kit(u)
            u.tap("SAVE")
            u.key(F.white(5))
            asked = u.armed()
            u.yes(up=2.0)
            ctx.check("KAWA saved as project 5, another kit as 6, LOAD 5: KAWA again",
                      m5.startswith("SAVED") and m6.startswith("SAVED") and asked and kit(u) == kits["KAWA"] and
                      other != kits["KAWA"], f"saves {m5!r} {m6!r}, project 6's kit {other}, asked {asked}, "
                      f"kit now {kit(u)}, message {u.msg()!r}")
        late, boots, crash = u.health()
        ctx.check("no late half, one boot, no crash record", late == 0 and boots == 1 and crash == 0,
                  f"late {late}, boots {boots}, crash {crash}")
    finally:
        e.quit()

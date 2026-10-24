# SPDX-License-Identifier: GPL-3.0-only
"""missing: a project made in another package, loaded in this one, which lacks its engine and kit (docs/RELEASE-0.2-
TEST.md section 9, the MISSING warning; core/miss.c). Needs EMU_OTHER=<the other package.fwsc> (its ELF beside it);
skipped without it. The checklist moves the project with a backup file through the web editor; here the flash
itself is carried: the other package's flash state after the save becomes this package's.

The other package, fresh boot: SHIFT2 + PRESETS steps T1's engine until one this package lacks; ALGORITHM to DR,
PRESETS to a kit this package lacks (the first of X0X 909, X0X 808, KAWA, PLAITS it has and this one does not);
the project saved as 11 (a plain project 12 saved before and loaded after it, so the work autosaved is plain).
This package booted on that flash (the boot's message reported); LOAD project 11 (asked): the
message names what is missing (MISSING ...); T1 plays a note (the stand-in), nothing stalls (late 0, one boot, no
crash record)."""
import os
import shutil
from pathlib import Path

import fwfacts as F
import opui
import session as S
import sweep

KITS = ("X0X 909", "X0X 808", "KAWA", "PLAITS")
FROM, PLAIN = 11, 12                                    # (the project carried; a plain one left open after it)


def save(u, key):
    u.to_mixer()
    u.tap("SAVE")
    u.turn("KNOB2", 1)
    u.key(F.white(key))
    u.yes()
    u.yes(up=2.0)
    msg = u.msg()
    u.to_mixer()
    return msg


def names_of(e):
    return sweep.Names(e).names


def make(ctx, other, mine):
    """the project in the other package -> (engine name, kit name or None, its flash state's dir)"""
    wd = ctx.out / "from"
    pkg = S.stage(other, wd)
    facts = F.Facts(other)
    e = S.Emu(pkg, wd, fresh=True, mhz=ctx.mhz)
    try:
        e.run(3.0)
        u = opui.UI(e, facts, str(ctx.out / "shots"))
        save(u, PLAIN)                                  # (a plain project first: the one left open at the end)
        theirs = names_of(e)
        eng = None
        for _ in range(len(theirs)):
            name = theirs[u.sound(0)[0]]
            if name not in mine:
                eng = name
                break
            u.with_held("OCTUP", lambda: u.turn("PRESETS", 1, wait=1.0))
        u.shot("from-engine")
        u.turn("ALGORITHM", 3, wait=0.5)
        kit = None
        mine_kits = ctx.mine_kits
        for _ in range(80):
            e.click("PRESETS", 1)
            k = u.msg().replace("KIT ", "", 1)
            if k in KITS and k not in mine_kits:
                kit = k
                break
            if u.msg() == "KIT PLAITS":
                break
        u.shot("from-kit")
        msg = save(u, FROM)
        u.tap("SAVE")
        u.key(F.white(PLAIN))                           # (LOAD the plain one: open, and the autosaved work, at the end)
        u.yes(up=2.0)
        e.run(15.0)
        ctx.check(f"the other package: project {FROM} saved with an engine and a kit this package lacks; project "
                  f"{PLAIN} (plain) loaded after it", eng and msg.startswith("SAVED") and u.msg() == "LOADED",
                  f"engine {eng}, kit {kit}, messages {msg!r} {u.msg()!r}")
    finally:
        e.quit()
    return eng, kit, wd


def kits_of(ctx):
    """this package's kits by name (PRESETS walked on DR)"""
    e, _ = ctx.open(fresh=True, settle=3.0)
    u = opui.UI(e, ctx.facts)
    out = set()
    try:
        u.turn("ALGORITHM", 3, wait=0.5)
        last = None
        for _ in range(80):
            e.click("PRESETS", 1)
            if u.msg() == last:
                break
            last = u.msg()
            out.add(last.replace("KIT ", "", 1))
        return out, names_of(e)
    finally:
        e.close()


def run(ctx):
    other = os.environ.get("EMU_OTHER")
    if not other:
        ctx.info("skipped", "EMU_OTHER (the package the project comes from) not set")
        return
    other = Path(other).resolve()
    ctx.mine_kits, mine = kits_of(ctx)
    eng, kit, wd = make(ctx, other, mine)
    if not eng:
        return
    st = S.state_dir(ctx.out)
    if st.exists():
        shutil.rmtree(st)
    shutil.copytree(S.state_dir(wd), st)
    e, _ = ctx.open(fresh=False, settle=3.0)
    u = opui.UI(e, ctx.facts, str(ctx.out / "shots"))
    try:
        boot_msg = u.msg()
        u.shot("booted")
        u.tap("SAVE")
        u.key(F.white(FROM))
        asked = u.armed()
        u.yes(up=0.6)
        msgs = [u.msg()]
        for _ in range(8):
            e.run(0.3)
            msgs.append(u.msg())
        u.shot("loaded")
        miss = [m for m in msgs if "MISS" in m]
        ctx.check(f"LOAD project {FROM} here: the header says MISSING and names what (the plain work at boot may "
                  "miss an engine of the other package's defaults already: its own message)",
                  asked and miss and (eng in " ".join(miss) or (kit and kit in " ".join(miss))),
                  f"at boot {boot_msg!r}; after LOAD {sorted(set(msgs))}; missing {eng} / {kit}")
        u.to_mixer()
        u.turn("ALGORITHM", -u.sel(), wait=0.5)        # (T1: the project was saved with DR selected)
        e.align()
        e.wavstart()
        e.tap(F.white(5), down=0.8, up=0.3)
        left, _ = e.wavstop(ctx.wav("standin"))
        r = S.rms(left)
        late, boots, crash = u.health()
        ctx.check("T1 plays its stand-in, nothing stalls (late 0, one boot, no crash record)",
                  r > 0.001 and late == 0 and boots == 1 and crash == 0,
                  f"RMS {r:.4f}, late {late}, boots {boots}, crash {crash}, T1 engine {u.sound(0)}")
    finally:
        e.quit()

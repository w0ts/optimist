# SPDX-License-Identifier: GPL-3.0-only
"""autosave: the working project kept across a power cycle without any SAVE (project.c autosave_tick / autosave_resume;
fix/autosave). Stopped, KNOB 2 turned (a value of the page shown, either UI): the autosave follows once the work has
not changed for 10 s (AUTOSAVE_SETTLE), also just after another autosave and in the first seconds after power-on
(before the fix: never within 20 s of the last save nor of power-on; a power-off there lost the edit). Then a power
cycle: the work comes back as it was saved (its change hash: the project's sum with its automation, FX and LFO
records), and idle on it writes nothing again (before the fix the hash set at power-on left the FX and LFO records
out: with FX in use every power-on wrote the work again). Last, the work kept changing (KNOB 2 +1 / -1 every
4 s, never 10 s the same): one save once it has been unsaved for 2 min (AUTOSAVE_MAX_WAIT), none before."""

KNOB = "KNOB2"
WAIT = 13.0                                           # (the 10 s settle + the 1 s checks + margin)
MAX_WAIT = 120.0                                      # (AUTOSAVE_MAX_WAIT)
STEP = 4.0                                            # (a change every 4 s: never settled)


def edit_saved(ctx, e, d, what):
    """KNOB 2 by d, then WAIT s idle -> the hash autosaved (checked: it changed)"""
    h0 = e.peek("autosave_hash")[0]
    e.turn(KNOB, d)
    t = e.now()
    e.run(WAIT)
    h1 = e.peek("autosave_hash")[0]
    ctx.check(f"{what} (edit at {t:.1f} s): autosaved within {WAIT:.0f} s", h1 != h0, f"hash {h0:#x} -> {h1:#x}")
    return h1


def power_on(ctx):
    e, _ = ctx.open(fresh=False, settle=1.0)
    return e


def run(ctx):
    e, _ = ctx.open(fresh=True, settle=3.0)
    try:
        edit_saved(ctx, e, -3, "an edit after power-on")
        edit_saved(ctx, e, 3, "an edit just after that autosave")
    finally:
        e.quit()
    e = power_on(ctx)
    try:
        e.run(1.0)
        h = edit_saved(ctx, e, -3, "after a power cycle, an edit 2 s after power-on")
        e.png(str(ctx.out / "shots" / "before-power-off.png"))
    finally:
        e.quit()
    e = power_on(ctx)
    try:
        e.run(2.0)
        r = e.peek("autosave_hash")[0]
        ctx.check("power cycle: the work back as last autosaved (its change hash, every record in it)", r == h,
                  f"resumed {r:#x}, saved {h:#x}")
        e.png(str(ctx.out / "shots" / "after-power-on.png"))
        e.run(WAIT + 2.0)
        r2 = e.peek("autosave_hash")[0]
        ctx.check(f"idle {WAIT + 2.0:.0f} s after power-on: nothing written again (nothing changed)", r2 == r,
                  f"hash {r:#x} -> {r2:#x}")
        changing(ctx, e)
    finally:
        e.quit()


def changing(ctx, e):
    """KNOB 2 every STEP s (+1, then -1 / +1 between 1 and 2 off the saved value): never settled, never back to the
    saved work -> the saves (autosave_put_ms moving) and when, from the first change"""
    p0, h0 = e.peek("autosave_put_ms")[0], e.peek("autosave_hash")[0]
    e.turn(KNOB, 1)
    t0, saves, d = e.now(), [], 1
    while e.now() - t0 < MAX_WAIT + 3 * STEP:
        e.turn(KNOB, d)
        d = -1 if d > 0 else 1
        e.run(STEP)
        p = e.peek("autosave_put_ms")[0]
        if p != p0:
            saves.append(round(e.now() - t0, 1))
            p0 = p
    h1 = e.peek("autosave_hash")[0]
    ctx.check(f"changing every {STEP:.0f} s for {e.now() - t0:.0f} s: one save, after {MAX_WAIT:.0f} s unsaved, none before",
              len(saves) == 1 and MAX_WAIT <= saves[0] <= MAX_WAIT + STEP + 2 and h1 != h0,
              f"saves at {saves} s, hash {h0:#x} -> {h1:#x}")

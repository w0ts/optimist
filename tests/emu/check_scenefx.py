# SPDX-License-Identifier: GPL-3.0-only
"""scenefx (F1): a scene stored from the SAVE layer keeps its FX slots and amounts. COMP into S1 with an amount on
T1; scene A stored stopped (SAVE held + white 5), scene B while playing (+ white 6, another amount: through the
pending arena); each loaded back (SAVE held + white 1 / 2) after a scramble, before and after the log is written
(when quiet), and after a power cycle. The section log is read through the ELF's symbols (slg): scene s's FX record
at its own id (fx_rec_log.c FXR_ID0 + s), nothing at the step extras' id SEC_ID_PAT0 + NTRK x PAT_N + s, where the
builds before ac354af put it (and the scene's own write then cleared FXR_ID0 + s; a scene read from the log also
missed its FX record: its key was hashed after the patterns were read over the record)."""
import fxkit as X

AMT = {1: 100, 2: 60}                                 # (scene A's T1 COMP amount, scene B's)


class Log:
    """the section log's index (sec_log.c slg: seq, sseq, aseq[N], at[N] u16, alen[N] u16)"""

    def __init__(self, ctx, e):
        f = ctx.facts
        self.e = e
        self.n = f.define("storage/sections/sec_log.c", "SLG_IDS")
        self.fx0 = f.define("fx/fx_rec_log.c", "FXR_ID0")
        self.stray0 = f.define("storage/sections/sec_log.c", "SEC_ID_PAT0") + f.on("NTRK") * 16   # (PAT_N 16)

    def alen(self, i):
        b = self.e.mem("slg", 8 + 8 * self.n)[8 + 4 * self.n:]
        at = int.from_bytes(b[2 * i:2 * i + 2], "little")
        return int.from_bytes(b[2 * self.n + 2 * i:2 * self.n + 2 * i + 2], "little") if at else 0

    def show(self, scenes=(0, 1)):
        return ", ".join(f"FX {self.fx0 + s}: {self.alen(self.fx0 + s)} B, {self.stray0 + s}: "
                         f"{self.alen(self.stray0 + s)} B" for s in scenes)

    def ok(self, scenes=(0, 1)):
        return all(self.alen(self.fx0 + s) > 0 and self.alen(self.stray0 + s) == 0 for s in scenes)


def run(ctx):
    if not ctx.facts.on("MASTER_COMP") or not ctx.facts.on("SECTIONS"):
        ctx.info("skipped", "needs COMP and the sections (FELUCCA_MASTER_COMP, FELUCCA_SECTIONS)")
        return
    e, P = ctx.open(fresh=True)
    s = {"e": e}
    try:
        body(ctx, s, e, P)
    finally:
        s["e"].quit()


def state(fx):
    return fx.slots(), fx.amt(0)


def fmt(fx, st):
    return f"{fx.names(st[0])} T1 COMP {st[1]}"


def loads(ctx, fx, P, want, when):
    """each scene scrambled, then launched (SAVE held + white n): its slots and amount"""
    for n in (1, 2):
        fx.scramble()
        s0 = state(fx)
        P.scene_launch(n, shot=f"load-{'AB'[n - 1]}-{when.split()[0]}")
        P.e.run(1.0)
        st = state(fx)
        ctx.check(f"{when}: scene {'AB'[n - 1]} loads its FX slots and amount", st == want[n] and s0 != want[n],
                  f"{fmt(fx, st)} (scrambled {fmt(fx, s0)}), stored {fmt(fx, want[n])}")


def body(ctx, s, e, P):
    fx = X.Fx(e, P)
    fx.slots_page()
    fx.slot_set(0, X.FXT["COMP"])
    fx.fx_page()
    want = {}
    for n in (1, 2):
        fx.fx_page()
        fx.comp_amount(0, AMT[n], 8)        # (the knob steps several values a detent)
        P.home()
        want[n] = state(fx)
        if n == 2:
            P.play()
            e.run(1.0)
        P.scene_store(n, shot=f"stored-{'AB'[n - 1]}")
        if n == 2:
            P.stop()
        e.run(1.0)
    ctx.check("set up: COMP in S1; T1's amount high in scene A (stored stopped), lower in B (stored playing)",
              all(w[0][0] == X.FXT["COMP"] for w in want.values()) and want[1][1] >= 80 and 30 <= want[2][1] <= 75,
              f"A {fmt(fx, want[1])}, B {fmt(fx, want[2])}")
    loads(ctx, fx, P, want, "stored")
    fx.scramble()
    e.run(24.0)                                       # (the log written when quiet; the autosave too)
    log = Log(ctx, e)
    ctx.check("written when quiet: each scene's FX record at its own id, none at the step extras' id", log.ok(),
              log.show())
    loads(ctx, fx, P, want, "from the log")
    e.quit()
    e, P = ctx.open(fresh=False, settle=3.5)
    s["e"], fx = e, X.Fx(e, P)
    log = Log(ctx, e)
    ctx.check("power cycle: the log as written", log.ok(), log.show())
    loads(ctx, fx, P, want, "after a power cycle")

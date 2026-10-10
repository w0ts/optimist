# SPDX-License-Identifier: GPL-3.0-only
"""boot: a clean boot. No guest fault, no crash record (fm1_crash), the boot guard counted no failed boot and is
cleared once the app reports a healthy boot (30 s), the audio DMA at 44100 / 256 halves a second with no late half
and no reset, the screen drawn, the UI loop alive, and a note sounds (T1, white key 5)."""
import fwfacts as F
import session as S

CRASH_MAGIC, GUARD_MAGIC = 0x43525348, 0x42475232
RATE = 44100 / 256                                   # 172.27 halves a second


def run(ctx):
    e, _ = ctx.open(fresh=True, settle=3.0)
    try:
        ctx.check("boots: the guest runs 3 s with no fault", True, e.boot[0][:60] if e.boot else "")
        cr = e.peek("fm1_crash", 2)
        ctx.check("no crash record", cr[0] != CRASH_MAGIC or cr[1] == 0, f"fm1_crash magic {cr[0]:#x} count {cr[1]}")
        bg = e.peek("bootguard", 3)
        ctx.check("boot guard: a normal boot, no failed boot", bg[0] == GUARD_MAGIC and bg[1] == 0,
                  f"magic {bg[0]:#x} failed {bg[1]} pending {bg[2]}")
        d0, t0 = e.dbg(), e.now()
        e.run(2.0)
        d1, t1 = e.dbg(), e.now()
        rate = (d1["halves"] - d0["halves"]) / (t1 - t0)
        ctx.check("audio running: 172.3 halves a second", abs(rate - RATE) < 1.5, f"{rate:.2f} halves/s")
        ctx.check("no late half, one boot", d1["late"] == 0 and d1["boots"] == 1,
                  f"late {d1['late']}, boots {d1['boots']}, max_us {d1['max_us']} of {S.HALF_US}")
        lit = e.lit()
        ctx.check("the screen is drawn", lit > 1500, f"{lit} lit pixels")
        e.png(str(ctx.out / "shots" / "boot.png"))
        e.align()
        e.wavstart()
        e.tap(F.white(5), down=0.8, up=0.2)
        left, _ = e.wavstop(ctx.wav("note"))
        r = S.rms(left)
        ctx.check("a note sounds (T1, white key 5)", r > 0.001, f"RMS {r:.4f} over {len(left)} frames")
        e.run(max(0.0, 34.0 - e.now()))
        bg, d2 = e.peek("bootguard", 3), e.dbg()
        ctx.check("after 34 s: the boot guard cleared (pending 0, failed 0)", bg[1] == 0 and bg[2] == 0,
                  f"failed {bg[1]} pending {bg[2]}")
        cr = e.peek("fm1_crash", 2)
        ctx.check("after 34 s: no late half, no reset, no crash record",
                  d2["late"] == 0 and d2["boots"] == 1 and (cr[0] != CRASH_MAGIC or cr[1] == 0),
                  f"late {d2['late']}, boots {d2['boots']}, max_us {d2['max_us']}, cpu_q8 {d2['cpu_q8']}")
        ctx.check("the UI loop runs (ui_frames advance)", d2["ui_frames"] > d1["ui_frames"] > 0,
                  f"ui_frames {d1['ui_frames']} -> {d2['ui_frames']}")
    finally:
        e.quit()

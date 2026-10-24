#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""emu-pass: the kit's GRIDS on DR (GEN_GRIDS) in the emulator.

  python3 tests/emu/grids.py BUILD_DIR OUT_DIR [--mhz 192]

Fresh boot; ALGORITHM to DR (no lane); PLAY on the empty pattern 3 s recorded (expected silent); STEP's GEN OUT page,
KNOB1 one detent (GEN ON); 4 s recorded; GRIDS page KNOB1 (X) +20 detents, 3 s recorded; stop. Prints the page titles
reached, the levels and felucca_dbg's late / boots; screenshots in OUT_DIR."""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))                       # (tests/emu first on the path)
import fwfacts as F          # noqa: E402
import panel as PN           # noqa: E402
import session as S          # noqa: E402


def level(e, path, s):
    e.wavstart()
    e.run(s)
    left, right = e.wavstop(path)
    x = left + right
    return max(abs(v) for v in x), S.rms(x), sum(1 for v in x if abs(v) >= 0.999)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("out")
    ap.add_argument("--mhz", type=int, default=192)
    ap.add_argument("--pages", type=int, default=0, help="only shoot N STEP pages (SELECT +1 each) and stop")
    ap.add_argument("--genout", type=int, default=4, help="SELECT detents from STEP to GEN OUT")
    ap.add_argument("--grids", type=int, default=2, help="SELECT detents from STEP to GRIDS")
    a = ap.parse_args()
    bd, out = Path(a.build).resolve(), Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    facts = F.Facts(bd / "felucca.fwsc")
    pkg = S.stage(bd / "felucca.fwsc", out / "run")
    e = S.Emu(pkg, out / "run", fresh=True, mhz=a.mhz)
    try:
        e.run(3.0)
        p = PN.Panel(e, facts, None, str(out))
        e.turn("ALGORITHM", 3)
        e.run(0.4)
        p.shot("dr-selected")
        print("page after ALGORITHM +3:", p.page())
        p.play()
        print("empty pattern, GEN off: peak %.4f rms %.5f full %d" % level(e, str(out / "off.wav"), 3.0))
        e.tap(F.BTN["SEQ"], up=0.4)
        for k in range(a.pages):
            p.shot(f"step-page{k}")
            e.turn("SELECT", 1)
            e.run(0.3)
        if a.pages:
            return
        e.turn("SELECT", a.genout)            # (the STEP page: SELECT +genout = GEN OUT, measured from the shots)
        e.run(0.3)
        p.shot("gen-out")
        e.turn("KNOB1", 1)
        e.run(0.3)
        p.shot("gen-on")
        print("GEN on: peak %.4f rms %.5f full %d" % level(e, str(out / "on.wav"), 4.0))
        e.turn("SELECT", a.grids - a.genout)
        e.run(0.3)
        p.shot("grids")
        e.turn("KNOB1", 20)
        e.run(0.3)
        p.shot("grids-x")
        print("X +20: peak %.4f rms %.5f full %d" % level(e, str(out / "x.wav"), 3.0))
        d = e.dbg()
        cr = e.peek("fm1_crash", 2)
        print(f"late {d['late']} boots {d['boots']} max_us {d['max_us']} crash {cr}")
    except S.EmuError as x:
        print("FAULT", x, "|", " | ".join(e.err[-10:]))
    finally:
        e.close()


if __name__ == "__main__":
    main()

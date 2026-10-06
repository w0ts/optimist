# SPDX-License-Identifier: GPL-3.0-only
"""Measure every registry item: one measurement build per non-default value (tools/build.py --measure links past
the slot, so the full configuration measures too). Writes tools/builder/costs.json: the default build's sizes
and, per item and value, the bytes it adds to it (flash, RAM, pool, RAMTEXT). Options are measured with their
parent on; PAIRS (an item whose cost depends on another's value) are measured together as well. The deltas add
up within ~0.5 % (docs/BUILDER-DESIGN.md); the menu's Build gives exact numbers.

  python3 tools/builder/measure_costs.py [--only KEY ...] [--log DIR]
About 10 s a build (Docker); the whole registry takes ~12 minutes."""
import argparse
import json
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import configure as C  # noqa: E402
import registry as R  # noqa: E402

REG = ("flash", "ram", "pool", "ramtext")
# items whose cost depends on another item's value: measured together too, the rest beyond their own deltas goes
# into costs.json "pairs" (configure.py budget adds it when the configuration has all of them)
PAIRS = [{"MOTION": 1, "SECTIONS": 4}]   # (motion beside the four project slots vs inside the section records)


def measure(cfg, name, log):
    ok, sizes, out = C.build(cfg, name, measure=True, log=log)
    if not ok or not sizes:
        raise SystemExit(f"measure_costs: {name} failed (log {log})\n{out[-1500:]}")
    return {r: sizes[r] for r in REG}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", nargs="*", help="items to (re)measure; the others keep their figures")
    ap.add_argument("--log", default=str(C.ROOT / "build" / "measure"), help="build logs")
    a = ap.parse_args()
    logd = Path(a.log)
    logd.mkdir(parents=True, exist_ok=True)
    old = C.load_costs() or {}
    t0 = time.time()
    base = measure(C.defaults(), "measure-base", logd / "base.log")
    out = {"base": base, "deltas": dict(old.get("deltas", {})) if a.only else {},
           "measured": time.strftime("%Y-%m-%d %H:%M")}
    for k, it in R.ITEMS.items():
        if a.only and k not in a.only:
            continue
        values = [c[0] for c in it.choices] if it.is_choice else [0, 1]
        out["deltas"][k] = {}
        for v in values:
            if v == it.default:
                continue
            cfg = C.defaults()
            cfg[k] = v
            if it.parent:
                cfg[it.parent] = 1 if not R.ITEMS[it.parent].is_choice else R.ITEMS[it.parent].default
            err, _, _ = C.validate(cfg)
            if err:                                     # (e.g. the last FM6 mode: measured with another off)
                print(f"  {k}={v}: skipped ({'; '.join(err)})")
                continue
            s = measure(cfg, f"m-{k}-{v}"[:16], logd / f"{k}-{v}.log")
            out["deltas"][k][str(v)] = {r: s[r] - base[r] for r in REG}
            print(f"  {k}={v}: {out['deltas'][k][str(v)]}  ({time.time() - t0:.0f} s)", flush=True)
        C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    out["pairs"] = dict(old.get("pairs", {})) if a.only else {}
    for pair in PAIRS:                                  # (items whose cost depends on another's value)
        name = ",".join(f"{k}={v}" for k, v in pair.items())
        if a.only and not any(k in a.only for k in pair):
            continue
        cfg = dict(C.defaults(), **pair)
        s = measure(cfg, f"m-pair-{len(out['pairs'])}", logd / f"pair-{name.replace(',', '-')}.log")
        own = [out["deltas"].get(k, {}).get(str(v), {}) for k, v in pair.items()]
        out["pairs"][name] = {r: s[r] - base[r] - sum(d.get(r, 0) for d in own) for r in REG}
        print(f"  {name}: {out['pairs'][name]} beyond the items' own deltas  ({time.time() - t0:.0f} s)", flush=True)
    C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    print(f"measure_costs: {C.COSTS} ({time.time() - t0:.0f} s)")


if __name__ == "__main__":
    main()

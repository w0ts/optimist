# SPDX-License-Identifier: GPL-3.0-only
"""Measure every registry item: one measurement build per non-default value (tools/build.py --measure links past
the slot, so the full configuration measures too). Writes tools/builder/costs.json: the default build's sizes
and, per item and value, the bytes it adds to it (flash, RAM, pool, RAMTEXT); its "cpu" section (the emulator's
scale, docs/CPU-GUARD.md) stays, and the CPU guard's model is generated again (cpu_costs.py). Options are measured with their
parent on; PAIRS (an item whose cost depends on another's value) are measured together as well. The deltas add
up within ~0.5 % (docs/BUILDER-DESIGN.md); the menu's Build gives exact numbers.

  python3 tools/builder/measure_costs.py [--only KEY ...] [--missing [--check]] [--log DIR]
--missing measures only what costs.json lacks (new items, new pairs; the base again when it moved by more than
DRIFT bytes in a region) and keeps the rest; --missing --check builds nothing and fails when something is lacking
(CI). About 10 s a build (Docker); the whole registry takes ~12 minutes, --missing a few builds.
Not detected: an item whose code changed after it was measured (no item-to-source map): re-measure it with --only."""
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
DRIFT = 256                                             # --missing: the base moved by more than this (bytes) in a region
HINT = "run python3 tools/builder/measure_costs.py --missing"
# items whose cost depends on another item's value: measured together too, the rest beyond their own deltas goes
# into costs.json "pairs" (configure.py budget adds it when the configuration has all of them)
PAIRS = [{"MOTION": 1, "SECTIONS": 4},   # (motion beside the four project slots vs inside the section records)
         # the reverb's algorithms share their line buffer, the half-rate filters and the code that switches them
         {"REV_PLATE": 1, "REV_FDN8": 1, "SPRING": 0}, {"REV_PLATE": 1, "REV_FDN8": 1, "SPRING": 1},
         {"REV_ROOM": 0, "REV_PLATE": 0, "REV_FDN8": 1},
         {"ENG_CZ": 1, "NATIVE_BANKS": 1},       # (the CZ tone collections share the engine's tone code)
         # the five sampled kits share the PERC samples: each kit's own delta is ~0, all five off together drop them
         {f"KIT_{k}": 0 for k in ("ACOUSTIC", "DEEP", "TIGHT", "BRIGHT", "DUST")}]
# a value that is not valid alone (the last reverb algorithm off): measured with these set too, less their own deltas
# (so those are measured first)
WITH = {("REV_ROOM", 0): {"REV_PLATE": 1},
        ("MICRO", 1): {"SL24_XSTEP": 1}, ("FILLS", 1): {"SL24_XSTEP": 1}, ("PLOCK", 1): {"SL24_XSTEP": 1}}   # (their storage)


def measure(cfg, name, log):
    ok, sizes, out = C.build(cfg, name, measure=True, log=log)
    if not ok or not sizes:
        raise SystemExit(f"measure_costs: {name} failed (log {log})\n{out[-1500:]}")
    return {r: sizes[r] for r in REG}


def unmeasured(costs):
    """-> [(key, value)] every item value besides its default that costs.json has no entry for"""
    out = []
    for k, it in R.ITEMS.items():
        values = [c[0] for c in it.choices] if it.is_choice else [0, 1]
        out += [(k, v) for v in values if v != it.default and str(v) not in costs.get("deltas", {}).get(k, {})]
    return out


def missing_pairs(costs):
    return [p for p in PAIRS if ",".join(f"{k}={v}" for k, v in p.items()) not in costs.get("pairs", {})]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", nargs="*", help="items to (re)measure; the others keep their figures")
    ap.add_argument("--missing", action="store_true",
                    help="measure only the items and pairs costs.json lacks (and the base if it drifted)")
    ap.add_argument("--check", action="store_true", help="with --missing: build nothing, exit 1 if anything lacks")
    ap.add_argument("--log", default=str(C.ROOT / "build" / "measure"), help="build logs")
    a = ap.parse_args(argv)
    logd = Path(a.log)
    old = C.load_costs() or {}
    if a.check and not a.missing:
        ap.error("--check goes with --missing")
    if a.missing:
        need = unmeasured(old) if old else [(k, 0) for k in R.ITEMS]
        stale = [k for k in old.get("deltas", {}) if k not in R.ITEMS]
        mp = missing_pairs(old) if old else PAIRS
        for line in [f"  not measured: {k}={v}" for k, v in need] + [f"  not measured: pair {p}" for p in mp] + \
                    [f"  costs.json has an item the registry lacks: {k}" for k in stale]:
            print(line)
        if a.check:
            if need or mp or stale:
                print(f"measure_costs: costs.json is incomplete; {HINT}")
                return 1
            print("measure_costs: every item and pair has a measured cost")
            return 0
        a.only = sorted({k for k, _ in need})
        new_pairs = {",".join(f"{k}={v}" for k, v in p.items()) for p in mp}
    else:
        new_pairs = set()
    logd.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    base = measure(C.defaults(), "measure-base", logd / "base.log")
    if a.missing and old.get("base") and all(abs(base[r] - old["base"][r]) <= DRIFT for r in REG):
        base = old["base"]                              # (not moved: the stored figures stay exact for the deltas)
    elif a.missing and old.get("base"):
        print(f"  base moved: {old['base']} -> {base} (the deltas stay as measured; a full run refreshes them)")
    out = {"base": base, "deltas": dict(old.get("deltas", {})) if a.only is not None else {},
           "measured": time.strftime("%Y-%m-%d %H:%M"), "cpu": old.get("cpu", {}),
           "pairs": dict(old.get("pairs", {}))}         # (the interim writes keep the pairs: an aborted run loses none)   # (cpu: the emulator's scale)
    later = [k for k in R.ITEMS if any(w[0] == k for w in WITH)]
    for k in [k for k in R.ITEMS if k not in later] + later:
        it = R.ITEMS[k]
        if a.only is not None and k not in a.only:
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
            cfg.update(WITH.get((k, v), {}))
            err, _, _ = C.validate(cfg)
            if err:                                     # (e.g. the last FM6 mode: measured with another off)
                print(f"  {k}={v}: skipped ({'; '.join(err)})")
                continue
            s = measure(cfg, f"m-{k}-{v}"[:16], logd / f"{k}-{v}.log")
            par = {}                                    # (an option of a parent off by default: its own bytes only)
            if it.parent and cfg[it.parent] != R.ITEMS[it.parent].default:
                par = out["deltas"].get(it.parent, {}).get(str(cfg[it.parent]), {})
            par = dict(par)
            for wk, wv in WITH.get((k, v), {}).items():      # (the items it was measured with)
                for r, n in out["deltas"].get(wk, {}).get(str(wv), {}).items():
                    par[r] = par.get(r, 0) + n
            out["deltas"][k][str(v)] = {r: s[r] - base[r] - par.get(r, 0) for r in REG}
            print(f"  {k}={v}: {out['deltas'][k][str(v)]}  ({time.time() - t0:.0f} s)", flush=True)
        C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    out["pairs"] = dict(old.get("pairs", {})) if a.only is not None else {}
    for pair in PAIRS:                                  # (items whose cost depends on another's value)
        name = ",".join(f"{k}={v}" for k, v in pair.items())
        if a.only is not None and name not in new_pairs and not any(k in a.only for k in pair):
            continue
        cfg = dict(C.defaults(), **pair)
        s = measure(cfg, f"m-pair-{len(out['pairs'])}", logd / f"pair-{name.replace(',', '-')}.log")
        own = [out["deltas"].get(k, {}).get(str(v), {}) for k, v in pair.items()]
        out["pairs"][name] = {r: s[r] - base[r] - sum(d.get(r, 0) for d in own) for r in REG}
        print(f"  {name}: {out['pairs'][name]} beyond the items' own deltas  ({time.time() - t0:.0f} s)", flush=True)
    C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    print(f"measure_costs: {C.COSTS} ({time.time() - t0:.0f} s)")
    import cpu_costs                                    # the CPU guard's model follows the CPU baseline (CPU_GUARD)
    cpu_costs.main([])


if __name__ == "__main__":
    sys.exit(main() or 0)

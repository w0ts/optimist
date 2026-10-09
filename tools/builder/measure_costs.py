# SPDX-License-Identifier: GPL-3.0-only
"""Measure every registry item: one measurement build per non-default value (tools/build.py --measure links past
the slot, so the full configuration measures too). Writes tools/builder/costs.json: the default build's sizes
and, per item and value, the bytes it adds to it (flash, RAM, pool, RAMTEXT); its "cpu" section (the emulator's
scale, docs/CPU-GUARD.md) stays, and the CPU guard's model is generated again (cpu_costs.py). Options are measured with their
parent on; PAIRS (an item whose cost depends on another's value) are measured together as well, and the check
configurations (every profile, tools/builder/estimate/*.config) once each, for tests/builder_test.py to hold the
estimate against (docs/BUILDER.md, Budget); the menu's Build gives exact numbers.

  python3 tools/builder/measure_costs.py [--only KEY ...] [--missing [--check]] [--log DIR]
--missing measures only what costs.json lacks (new items, new pairs; the base again whenever it moved at all) and keeps the rest; --missing --check builds nothing and fails when something is lacking
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
HINT = "run python3 tools/builder/measure_costs.py --missing"
# items whose cost depends on another item's value: measured together too; what the build has beyond the estimate
# without it (the items' own deltas, the computed terms and the earlier pairs it contains) goes into costs.json
# "pairs" (configure.py budget adds it when the configuration has all of them). A pair that contains an earlier
# one (all its conditions) comes after it
SETS = ("PIANO", "BASS", "VIBES", "HORNS", "STRGS", "FLUTE", "SCRCH")   # (registry SET_*)
PAIRS = [{"MOTION": 1, "SECTIONS": 4},   # (motion beside the four project slots vs inside the section records)
         # the reverb's algorithms share their line buffer, the half-rate filters and the code that switches them
         # (the buffer itself is computed: configure.py rev_lines)
         {"REV_PLATE": 1, "REV_FDN8": 1, "SPRING": 0}, {"REV_PLATE": 1, "REV_FDN8": 1, "SPRING": 1},
         {"REV_ROOM": 0, "REV_PLATE": 0, "REV_FDN8": 1},
         {"REV_ROOM": 0, "REV_HALF": 1},         # (REV_HALF's half-band filters and half-rate tank are the ROOM's)
         {"ENG_CZ": 1, "NATIVE_BANKS": 1},       # (the CZ tone collections share the engine's tone code)
         # the five sampled kits share the PERC samples: each kit's own delta is ~0, all five off together drop them
         {f"KIT_{k}": 0 for k in ("ACOUSTIC", "DEEP", "TIGHT", "BRIGHT", "DUST")},
         {"DRUM_X0X909": 1, "DRUM_X0X808": 1},   # (the X0X drum voice code and its state, once for both kits)
         # every sample set off: the set tables' code goes; with the sampled drums off too, the sample voice's
         {f"SET_{k}": 0 for k in SETS}, dict({"DRUM_SAMPLED": 0}, **{f"SET_{k}": 0 for k in SETS}),
         # SLOOP 2.4's step features share their step-extras code (measured with SL24_XSTEP, their storage)
         {"MICRO": 1, "FILLS": 1}, {"MICRO": 1, "PLOCK": 1}, {"FILLS": 1, "PLOCK": 1},
         {"MICRO": 1, "FILLS": 1, "PLOCK": 1}]
# a pair whose values are not valid alone: measured with these set too (their cost is in the estimate it is
# measured against)
PAIR_WITH = {"REV_ROOM=0,REV_HALF=1": {"REV_PLATE": 1}, "MICRO=1,FILLS=1": {"SL24_XSTEP": 1},
             "MICRO=1,PLOCK=1": {"SL24_XSTEP": 1}, "FILLS=1,PLOCK=1": {"SL24_XSTEP": 1},
             "MICRO=1,FILLS=1,PLOCK=1": {"SL24_XSTEP": 1}}
# a value that is not valid alone (the last reverb algorithm off): measured with these set too, less their own deltas
# (so those are measured first)
WITH = {("REV_ROOM", 0): {"REV_PLATE": 1},
        ("MICRO", 1): {"SL24_XSTEP": 1}, ("FILLS", 1): {"SL24_XSTEP": 1}, ("PLOCK", 1): {"SL24_XSTEP": 1}}   # (their storage)


def measure(cfg, name, log):
    ok, sizes, out = C.build(cfg, name, measure=True, log=log)
    if not ok or not sizes:
        raise SystemExit(f"measure_costs: {name} failed (log {log})\n{out[-1500:]}")
    return {r: sizes[r] for r in REG}


def pair_name(pair):
    return ",".join(f"{k}={v}" for k, v in pair.items())


def beyond(sizes, cfg, costs, name):
    """what a pair's measurement build has beyond the estimate without that pair: the base, the items' deltas, the
    computed terms and the pairs listed before it in PAIRS (the ones it contains)"""
    earlier = [pair_name(p) for p in PAIRS[:[pair_name(q) for q in PAIRS].index(name)]]
    known = dict(costs, pairs={n: d for n, d in costs.get("pairs", {}).items() if n in earlier})
    est = C.budget(cfg, known, use_exact=False)["total"]
    return {r: sizes[r] - est[r] for r in REG}


def unmeasured(costs):
    """-> [(key, value)] every item value besides its default that costs.json has no entry for"""
    out = []
    for k, it in R.ITEMS.items():
        values = [c[0] for c in it.choices] if it.is_choice else [0, 1]
        out += [(k, v) for v in values if v != it.default and str(v) not in costs.get("deltas", {}).get(k, {})]
    return out


# the estimate checked against real builds (tests/builder_test.py): every shipped profile and the configurations in
# tools/builder/estimate/ (diverse ones: a user's, the reverb in the pool at half rate, the X0X kits, SLOOP 2.4's
# step features...). Measured in every run that builds (the same source as the deltas): costs.json "checks"
CHECK_DIRS = (C.PROFILES, HERE / "estimate")


def check_configs():
    """-> {name: cfg} the configurations the estimate is checked against"""
    out = {}
    for d in CHECK_DIRS:
        for p in sorted(d.glob("*.config")):
            out[p.stem] = C.parse(p.read_text())[0]
    return out


def missing_checks(costs):
    """-> [name] check configurations costs.json has no measurement of (or of an older version of the file)"""
    have = costs.get("checks", {})
    return [n for n, cfg in check_configs().items() if have.get(n, {}).get("hash") != f"{C.cfg_hash(cfg):08x}"]


def measure_checks(logd):
    """a measurement build of every check configuration -> costs.json "checks" ({"hash", "sizes"}; "failed": a
    configuration that does not build, which is the firmware's problem (tools/builder/verify.py), not the estimate's)"""
    out = {}
    for name, cfg in check_configs().items():
        ok, sizes, text = C.build(cfg, f"m-chk-{name}"[:16], measure=True, log=logd / f"check-{name}.log")
        out[name] = {"hash": f"{C.cfg_hash(cfg):08x}"}
        if ok and sizes:
            out[name]["sizes"] = {r: sizes[r] for r in REG}
        else:
            out[name]["failed"] = (text.strip().splitlines() or ["?"])[-1][:200]
            print(f"  check {name}: the build failed ({out[name]['failed']}; log {logd / f'check-{name}.log'})")
        print(f"  check {name}: {out[name].get('sizes')}", flush=True)
    return out


def missing_pairs(costs):
    return [p for p in PAIRS if pair_name(p) not in costs.get("pairs", {})]


def refreshed_base(old_base, measured):
    """--missing: the base to store. Any move is taken (no tolerance): the estimate is base + deltas, so a stale base
    is an error in every estimate, and tests/builder_test.py holds the estimate to the real build within 256 B."""
    if old_base and measured != old_base:
        print(f"  base moved: {old_base} -> {measured} (refreshed; the deltas stay as measured, "
              "a full run refreshes them)")
    return measured


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
        mc = missing_checks(old)
        for line in [f"  not measured: {k}={v}" for k, v in need] + [f"  not measured: pair {p}" for p in mp] + \
                    [f"  costs.json has an item the registry lacks: {k}" for k in stale] + \
                    [f"  not measured: the real build of check configuration {n}" for n in mc]:
            print(line)
        if a.check:
            if need or mp or stale or mc:
                print(f"measure_costs: costs.json is incomplete; {HINT}")
                return 1
            print("measure_costs: every item and pair has a measured cost")
            return 0
        a.only = sorted({k for k, _ in need})
        new_pairs = {pair_name(p) for p in mp}
    else:
        new_pairs = set()
    logd.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    base = measure(C.defaults(), "measure-base", logd / "base.log")
    if a.missing:
        base = refreshed_base(old.get("base"), base)
    out = {"base": base, "deltas": dict(old.get("deltas", {})) if a.only is not None else {},
           "measured": time.strftime("%Y-%m-%d %H:%M"), "cpu": old.get("cpu", {}), "checks": old.get("checks", {}),
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
            if it.no_image:                             # (a reserve: nothing in the image, so 0, never built)
                out["deltas"][k][str(v)] = {r: 0 for r in REG}
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
            terms = C.model_terms(cfg)                  # (computed, not a delta: the reverb's line buffer)
            out["deltas"][k][str(v)] = {r: s[r] - base[r] - par.get(r, 0) - terms[r] for r in REG}
            print(f"  {k}={v}: {out['deltas'][k][str(v)]}  ({time.time() - t0:.0f} s)", flush=True)
        C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    out["pairs"] = dict(old.get("pairs", {})) if a.only is not None else {}
    for pair in PAIRS:                                  # (items whose cost depends on another's value)
        name = pair_name(pair)
        if a.only is not None and name not in new_pairs and not any(k in a.only for k in pair):
            continue
        cfg = dict(C.defaults(), **pair, **PAIR_WITH.get(name, {}))
        s = measure(cfg, f"m-pair-{PAIRS.index(pair)}", logd / f"pair-{name.replace(',', '-')}.log")
        out["pairs"][name] = beyond(s, cfg, out, name)
        print(f"  {name}: {out['pairs'][name]} beyond the estimate without it  ({time.time() - t0:.0f} s)", flush=True)
    out["checks"] = measure_checks(logd)
    C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    print(f"measure_costs: {C.COSTS} ({time.time() - t0:.0f} s)")
    import cpu_costs                                    # the CPU guard's model follows the CPU baseline (CPU_GUARD)
    cpu_costs.main([])


if __name__ == "__main__":
    sys.exit(main() or 0)

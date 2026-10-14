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
         # (the buffer itself is computed: configure.py rev_lines; the automation store: auto_store)
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
         # (the automation store's code, built with any of MOTION, CHANCE, SL24_XSTEP, COND: the group AUTO below;
         # its arrays are computed: configure.py auto_store)
         {"MICRO": 1, "FILLS": 1}, {"MICRO": 1, "PLOCK": 1}, {"FILLS": 1, "PLOCK": 1},
         {"MICRO": 1, "FILLS": 1, "PLOCK": 1},
         # BLE with the CDC console (USB_MODE 1): the console's blevm / bletrim commands; with BLE_DIAG blell's printing
         {"BLE": 1, "USB_MODE": 1}, {"BLE": 1, "BLE_DIAG": 1, "USB_MODE": 1},
         # the USB audio resampler is only compiled with USB audio (MIDI only by default since 2026-10-12)
         {"USB_MODE": 2, "UA_RESAMPLE": 1},
         # found 2026-10-12 (the "mots" check 4.7 KB under its real build; ~/costs bisection, each pair 0.3-1.6 KB of
         # flash beyond its items' deltas): the CPU guard's parts in the ACID engine and the X0X drums (cpuguard.c,
         # eng_acid.c, drum_x0x.c: built only with both); GLIDE with PATTERNS and MACROS with USB_MODE 0 measured
         # so (+1.6 KB, +1.2 KB), the code behind it not traced
         {"CPU_GUARD": 1, "ENG_ACID": 1}, {"CPU_GUARD": 1, "DRUM_X0X909": 1}, {"CPU_GUARD": 1, "DRUM_X0X808": 1},
         {"CPU_GUARD": 1, "DRUM_X0X909": 1, "DRUM_X0X808": 1},
         {"GLIDE": 1, "PATTERNS": 1}, {"USB_MODE": 0, "MACROS": 1},
         # BLE's bond and central together share code each one's delta pays (-3.5 KB flash, -0.35 KB RAM); the central
         # with the diagnostics has more (+1.9 KB, +0.4 KB RAM); and each with the CDC console (the BLE test builds,
         # tools/optimist.py BLE_FULL_SET: 7 KB under without these)
         {"BLE": 1, "BLE_BOND": 1, "BLE_CENTRAL": 1}, {"BLE": 1, "BLE_CENTRAL": 1, "BLE_DIAG": 1},
         {"BLE": 1, "BLE_BOND": 1, "BLE_CENTRAL": 1, "BLE_DIAG": 1},
         {"BLE": 1, "BLE_BOND": 1, "BLE_CENTRAL": 1, "USB_MODE": 1}, {"BLE": 1, "BLE_CENTRAL": 1, "BLE_DIAG": 1, "USB_MODE": 1},
         {"BLE": 1, "BLE_BOND": 1, "BLE_CENTRAL": 1, "BLE_DIAG": 1, "USB_MODE": 1},
         # SLOOP 2.4's guard and import: core.h SL24_AUTO (PROJECT > A24) and project.c's shared reader are built
         # with both / either, so the two off together save 3.9 KB less than their deltas
         {"SL24_SAFE": 0, "SL24_IMPORT": 0},
         # PLOCK's lock base is motion.c's (pool 292 B with PLOCK alone, none beyond MOTION's 900 with both: measured
         # 2026-10-15, user-default 292 B of pool over)
         {"MOTION": 1, "PLOCK": 1},
         # the user sample zones (eng_sample.c SMP_USR_ZONES, 1.3 KB of RAM and their code) are built with any of SAMPLE,
         # GRAIN, SLICE and the drums' user samples: only all four off drop them (2026-10-14, a user's configuration
         # 1,344 B of RAM over without this)
         {"ENG_SAMPLE": 0, "ENG_GRAIN": 0, "ENG_SLICE": 0, "DRUM_USR": 0}]
# items off by default that share code and state built with any of them: each one's delta pays it, so the group's
# shared part is measured (all of them on: (sum of their deltas - what that build adds) / (n - 1)) and counted once
# (configure.py group_delta). Found 2026-10-14: each insert beyond the first was 0.76 KB of RAM over
def _any_of(header, macro):
    """-> (keys) the registry items of a firmware macro defined as an OR of FELUCCA_ switches (core.h INS_FX), so a
    new member joins its group without an edit here"""
    import re
    m = re.search(r"^#define " + macro + r" \((.*?)\)\s*$", (C.ROOT / "firmware" / "src" / header).read_text(), re.M)
    flags = re.findall(r"FELUCCA_\w+", m.group(1)) if m else []
    by_flag = {it.flag: k for k, it in R.ITEMS.items() if it.flag}
    return tuple(by_flag[f] for f in flags if f in by_flag and not R.ITEMS[by_flag[f]].default)


GROUPS = {"INS_FX": _any_of("core/core.h", "INS_FX"),   # (the insert slots)
          # the automation store's code (seq/auto.c, storage/auto_proj.c: ~7 KB of flash, a little RAM and pool):
          # as pairs (MOTION+CHANCE, ...) a change of one of four left out the combinations without MOTION (9 KB over
          # for CHANCE off in a user's configuration, 2026-10-14)
          "AUTO": _any_of("core/backports24seq.h", "FELUCCA_AUTO")}
# each group is measured with the Optimist UI too ("NAME|UI=1", after the pairs: what they share differs with it)
GROUP_WHEN = {"UI": 1}
# a pair whose values are not valid alone: measured with these set too (their cost is in the estimate it is
# measured against)
PAIR_WITH = {"REV_ROOM=0,REV_HALF=1": {"REV_PLATE": 1}, "MICRO=1,FILLS=1": {"SL24_XSTEP": 1},
             "MICRO=1,PLOCK=1": {"SL24_XSTEP": 1}, "FILLS=1,PLOCK=1": {"SL24_XSTEP": 1},
             "MICRO=1,FILLS=1,PLOCK=1": {"SL24_XSTEP": 1}, "MOTION=1,PLOCK=1": {"SL24_XSTEP": 1}}
# a value that is not valid alone (the last reverb algorithm off): measured with these set too, less their own deltas
# (so those are measured first)
WITH = {("REV_ROOM", 0): {"REV_PLATE": 1},
        ("MICRO", 1): {"SL24_XSTEP": 1}, ("FILLS", 1): {"SL24_XSTEP": 1}, ("PLOCK", 1): {"SL24_XSTEP": 1},   # (their storage)
        ("SEQTOOLS", 1): {"UI": 1}}   # (the Optimist UI's: nothing without it)
# an item the firmware switches off without the automation store (configure.py NEEDS_AUTO: DRUM_ACCENT, 0 B on the
# defaults, +148 B of flash with the store): measured with MOTION on, and counted only where the store is built
for _k in C.NEEDS_AUTO:
    for _v in ([c[0] for c in R.ITEMS[_k].choices] if R.ITEMS[_k].is_choice else [0, 1]):
        if _v != R.ITEMS[_k].default:
            WITH.setdefault((_k, _v), {"MOTION": 1})
# the Optimist UI (UI=1) is a second firmware for many items: their UI code is its own, or not built at all
# (PARAM_HELP, FM6_KEYS, the engines' pages...: 0.4-2.5 KB of flash each away from their SLOOP-UI deltas, a user's
# configuration 17.7 KB under its real build, 2026-10-14). So every item value is measured with UI=1 as well (a pair
# {UI: 1, item: value}), after the pairs above, and then those pairs again with UI=1; each as the delta was (an
# option with its parent on, WITH's values set too). The items the Optimist UI needs (WITH ... UI) are measured
# with it already
_BASE_PAIRS = list(PAIRS)


def _ui_pairs():
    out = []
    for k, it in R.ITEMS.items():
        if k == "UI" or it.no_image or WITH.get((k, 1), {}).get("UI"):
            continue
        for v in ([c[0] for c in it.choices] if it.is_choice else [0, 1]):
            if v == it.default:
                continue
            pair = {"UI": 1, k: v}
            with_ = dict(WITH.get((k, v), {}))
            if it.parent and not R.ITEMS[it.parent].default:
                with_[it.parent] = parent_on(R.ITEMS[it.parent])
            if C.validate(dict(C.defaults(), **pair, **with_))[0]:
                continue
            out.append((pair, with_))
    for pair in _BASE_PAIRS:
        with_ = PAIR_WITH.get(pair_name(pair), {})
        ui = dict({"UI": 1}, **pair)
        if not C.validate(dict(C.defaults(), **ui, **with_))[0]:
            out.append((ui, with_))
    return out


def parent_on(p):
    """the value an option is measured under: a bool parent on; a choice parent at its default, or at its first
    value that builds it when the default leaves it out (DUAL's 0: DUAL_BOOST measured 0 B under it)"""
    if not p.is_choice:
        return 1
    return p.default if p.default else next(v for v, _ in p.choices if v)


def measure(cfg, name, log):
    ok, sizes, out = C.build(cfg, name, measure=True, log=log)
    if not ok or not sizes:
        raise SystemExit(f"measure_costs: {name} failed (log {log})\n{out[-1500:]}")
    check_terms(cfg, sizes, name)
    return {r: sizes[r] for r in REG}


def measure_part_mem(logd):
    """--part-mem: engines.c part_mem's size with each of its members alone (the others off) -> printed as
    configure.py PART_MEM; 1 when a build fails"""
    logd.mkdir(parents=True, exist_ok=True)
    out = {}
    for k in [*C.PART_MEM, None]:
        cfg = dict(C.defaults(), **{m: 0 for m in C.PART_MEM})
        if k:
            cfg[k] = 1
        ok, sizes, text = C.build(cfg, f"m-pm-{k or 'none'}"[:16], measure=True, log=logd / f"part_mem-{k}.log")
        if not ok or not sizes or "symbols" not in sizes:
            print(f"measure_costs: part_mem with {k or 'none'}: the build failed (log {logd})")
            return 1
        out[k or "none"] = sizes["symbols"].get("part_mem", 0)   # (0: not in the image)
    print("PART_MEM = {" + ", ".join(f'"{k}": {n}' for k, n in out.items() if k != "none") + "}")
    print(f"PART_MEM_NONE = {out['none']}")
    return 0


def check_terms(cfg, sizes, name):
    """the computed terms (configure.py model_terms) against the objects' real sizes in this build (sizes.json
    "symbols", tools/build.py SIZED_SYMBOLS): a structure that grew is a wrong estimate everywhere, so it stops here"""
    real = (sizes.get("symbols") or {}).get("part_mem")
    if real is not None and real != C.part_mem(cfg)["pool"]:
        alone = [k for k in C.PART_MEM if C.built(cfg, k)]
        raise SystemExit(f"measure_costs: {name}: engines.c part_mem is {real} B, configure.py part_mem says "
                         f"{C.part_mem(cfg)['pool']} B (built: {', '.join(alone) or 'none'}): update configure.py "
                         "PART_MEM (python3 tools/builder/measure_costs.py --part-mem measures each member alone)")


def pair_name(pair):
    return ",".join(f"{k}={v}" for k, v in pair.items())


for _pair, _with in _ui_pairs():
    PAIRS.append(_pair)
    if _with:
        PAIR_WITH[pair_name(_pair)] = _with


def beyond(sizes, cfg, costs, name):
    """what a pair's measurement build has beyond the estimate without that pair: the base, the items' deltas, the
    computed terms and the pairs listed before it in PAIRS (the ones it contains)"""
    earlier = [pair_name(p) for p in PAIRS[:[pair_name(q) for q in PAIRS].index(name)]]
    known = dict(costs, pairs={n: d for n, d in costs.get("pairs", {}).items() if n in earlier})
    est = C.budget(cfg, known, use_exact=False, anchor=False)["total"]
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


def check_configs(probes=False):
    """-> {name: cfg} the configurations the estimate is checked against; probes: each one's probe too (probe)"""
    out = {}
    for d in CHECK_DIRS:
        for p in sorted(d.glob("*.config")):
            out[p.stem] = C.parse(p.read_text())[0]
    import optimist                                     # (tools/: the BLE builds optimist.py test makes)
    out.update(optimist.ble_test_configs())
    if probes:
        out.update({f"{n}{PROBE}": probe(n, cfg) for n, cfg in list(out.items())})
    return out


# each check configuration has a probe: the same with PROBE_ITEMS items changed, the items and values picked by a
# hash of the names (a user's few changes from a configuration built before, the case the menu meets most). Measured
# with the checks (costs.json "checks" NAME~): tests/builder_test.py estimates each check configuration without its
# own build, so a probe and its configuration are each other's nearest anchor, PROBE_ITEMS apart
PROBE, PROBE_ITEMS = "~", 4


def probe(name, cfg):
    """-> cfg with PROBE_ITEMS items changed (a valid configuration; the same every run for the same name and cfg)"""
    out, n = dict(cfg), 0
    for k in sorted((k for k, it in R.ITEMS.items() if not it.no_image), key=lambda k: C.fnv32(f"{name}:{k}")):
        it = R.ITEMS[k]
        if it.parent and not C.built(out, it.parent):
            continue
        alt = [v for v in ([c[0] for c in it.choices] if it.is_choice else [0, 1]) if v != out[k]]
        trial = dict(out, **{k: alt[C.fnv32(f"{name}={k}") % len(alt)]})
        if C.validate(trial)[0] or not C.distance(trial, out):
            continue
        out, n = trial, n + 1
        if n == PROBE_ITEMS:
            break
    return out


def missing_checks(costs):
    """-> [name] check configurations (and probes) costs.json has no measurement of (or of an older version)"""
    have = costs.get("checks", {})
    return [n for n, cfg in check_configs(probes=True).items()
            if have.get(n, {}).get("hash") != f"{C.cfg_hash(cfg):08x}"]


def measure_checks(logd):
    """a measurement build of every check configuration -> costs.json "checks" ({"hash", "sizes"}; "failed": a
    configuration that does not build, which is the firmware's problem (tools/builder/verify.py), not the estimate's)"""
    out = {}
    for name, cfg in check_configs(probes=True).items():
        ok, sizes, text = C.build(cfg, f"m-chk-{name}"[:16], measure=True, log=logd / f"check-{name}.log")
        out[name] = dict({"hash": f"{C.cfg_hash(cfg):08x}"}, **C.anchor_record(cfg))
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
    ap.add_argument("--part-mem", action="store_true",
                    help="measure engines.c part_mem with each engine that keeps a state there alone (configure.py PART_MEM)")
    a = ap.parse_args(argv)
    logd = Path(a.log)
    if a.part_mem:
        return measure_part_mem(logd)
    old = C.load_costs() or {}
    if a.check and not a.missing:
        ap.error("--check goes with --missing")
    if a.missing:
        need = unmeasured(old) if old else [(k, 0) for k in R.ITEMS]
        stale = [k for k in old.get("deltas", {}) if k not in R.ITEMS]
        mp = (missing_pairs(old) if old else PAIRS) + \
            [{"group": n} for g in GROUPS for n in (g, g + "|" + pair_name(GROUP_WHEN)) if n not in old.get("groups", {})]
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
        new_pairs = {pair_name(p) for p in mp if "group" not in p}   # (a missing group is measured: GROUPS below)
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
                cfg[it.parent] = parent_on(R.ITEMS[it.parent])
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
            terms = C.model_terms(cfg)                  # (computed, not a delta: the reverb's line buffer, the automation store)
            out["deltas"][k][str(v)] = {r: s[r] - base[r] - par.get(r, 0) - terms[r] for r in REG}
            print(f"  {k}={v}: {out['deltas'][k][str(v)]}  ({time.time() - t0:.0f} s)", flush=True)
        C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    out["groups"] = {n: g for n, g in old.get("groups", {}).items() if n.split("|")[0] in GROUPS} \
        if a.only is not None else {}
    for gname, members in GROUPS.items():               # (before the pairs: their builds may hold several members)
        if a.only is not None and gname in out["groups"] and not any(k in a.only for k in members):
            continue
        out["groups"].pop(gname, None)
        cfg = dict(C.defaults(), **{k: 1 for k in members})
        s = measure(cfg, f"m-grp-{gname}"[:16], logd / f"group-{gname}.log")
        est = C.budget(cfg, dict(out, pairs={}), use_exact=False, anchor=False)["total"]
        out["groups"][gname] = {"members": list(members),
                                "shared": {r: round((est[r] - s[r]) / (len(members) - 1)) for r in REG}}
        print(f"  group {gname}: {out['groups'][gname]['shared']} shared  ({time.time() - t0:.0f} s)", flush=True)
    names = {pair_name(p) for p in PAIRS}               # (a pair no longer listed is dropped, not kept stale)
    out["pairs"] = {n: d for n, d in old.get("pairs", {}).items() if n in names} if a.only is not None else {}
    for pair in PAIRS:                                  # (items whose cost depends on another's value)
        name = pair_name(pair)
        if a.only is not None and name not in new_pairs and not any(k in a.only for k in pair):
            continue
        cfg = dict(C.defaults(), **pair, **PAIR_WITH.get(name, {}))
        s = measure(cfg, f"m-pair-{PAIRS.index(pair)}", logd / f"pair-{name.replace(',', '-')}.log")
        out["pairs"][name] = beyond(s, cfg, out, name)
        print(f"  {name}: {out['pairs'][name]} beyond the estimate without it  ({time.time() - t0:.0f} s)", flush=True)
    for gname, members in GROUPS.items():               # (the groups with the Optimist UI: after its pairs)
        vname = gname + "|" + pair_name(GROUP_WHEN)
        if a.only is not None and vname in out["groups"] and not any(k in a.only for k in members):
            continue
        out["groups"].pop(vname, None)
        cfg = dict(C.defaults(), **GROUP_WHEN, **{k: 1 for k in members})
        s = measure(cfg, f"m-grp-{gname}-ui"[:16], logd / f"group-{gname}-ui.log")
        est = C.budget(cfg, dict(out, groups={n: g for n, g in out["groups"].items()
                                                  if n != gname and not n.startswith(gname + "|")}), use_exact=False,
                       anchor=False)["total"]
        out["groups"][vname] = {"members": list(members), "when": dict(GROUP_WHEN),
                                "shared": {r: round((est[r] - s[r]) / (len(members) - 1)) for r in REG}}
        print(f"  group {vname}: {out['groups'][vname]['shared']} shared  ({time.time() - t0:.0f} s)", flush=True)
    out["checks"] = measure_checks(logd)
    C.COSTS.write_text(json.dumps(out, indent=1) + "\n")
    print(f"measure_costs: {C.COSTS} ({time.time() - t0:.0f} s)")
    import cpu_costs                                    # the CPU guard's model follows the CPU baseline (CPU_GUARD)
    cpu_costs.main([])


if __name__ == "__main__":
    sys.exit(main() or 0)

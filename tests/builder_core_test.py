#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The builder's core (tools/builder/core.py): it imports with no UI, no file read, no git; the cache key is stable,
independent of order and of what the image does not hold; the JSON API round-trips; configure.py's names are the
core's.

  python3 tests/builder_core_test.py                    # the checks above (run by tests/run_tests.sh)
  python3 tests/builder_core_test.py --against REF      # also: every profile and N random configurations give the
                                                        # same results as configure.py at REF (git), e.g. the
                                                        # commit before core.py was extracted
"""
import argparse
import contextlib
import io
import json
import random
import subprocess
import sys
import types

from builder_path import BUILDER, ROOT
import configure as C
import core as K
import optimist as O
import registry as R

fails = 0


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


# ---- the core imports with nothing a browser (Pyodide) lacks: no UI, no subprocess, no file read
CHILD = f"""
import builtins, io, pathlib, sys
sys.path.insert(0, {str(BUILDER)!r})
def refuse(*a, **k):
    raise AssertionError("a file read at import")
builtins.open = io.open = refuse
pathlib.Path.open = pathlib.Path.read_text = pathlib.Path.read_bytes = refuse
import core
bad = [m for m in ("textual", "rich", "curses", "subprocess", "configure", "shared", "backports") if m in sys.modules]
print("BAD", bad) if bad else print("CLEAN", len(core.describe()["registry"]["items"]))
"""
p = subprocess.run([sys.executable, "-c", CHILD], capture_output=True, text=True)
check("core imports with no UI, subprocess, configure or file read", p.stdout.startswith("CLEAN"))
if not p.stdout.startswith("CLEAN"):
    print(p.stdout + p.stderr)

# ---- configure.py re-exports the core: one implementation
check("configure's rules are the core's (validate, parse, flags, effective, cfg_hash, fits)",
      all(getattr(C, n) is getattr(K, n) for n in ("validate", "parse", "flags", "effective", "cfg_hash", "fits",
                                                    "savings_of", "ConfigError", "canonical", "cloud_key")))
costs = C.load_costs()
REAL_EXACT = C.exact_sizes, C.exact_builds
C.exact_sizes = lambda cfg: None                        # (the estimate only: no real build's sizes from build/)
C.exact_builds = lambda: {}
d = C.defaults()
check("configure.budget without real builds = core.budget",
      C.budget(d, costs) == K.budget(d, costs) and C.budget(d, costs, anchor=False) == K.budget(d, costs, anchor=False))

# ---- the key
SHA, SHA2 = "0123456789abcdef" * 2 + "01234567", "f" * 40
key = K.cloud_key(d, SHA)
check("key: sha256 hex, the same twice", len(key) == 64 and key == K.cloud_key(dict(d), SHA))
check("key: independent of the order of the values", K.cloud_key(dict(reversed(list(d.items()))), SHA) == key)
check("key: another commit, another key", K.cloud_key(d, SHA2) != key)
try:
    K.cloud_key(d, "HEAD")
    check("key: a commit that is not a full SHA is refused", False)
except K.ConfigError:
    check("key: a commit that is not a full SHA is refused", True)
opt = next(k for k, it in R.ITEMS.items() if it.parent and not R.ITEMS[it.parent].is_choice and not it.no_image)
par, alt = R.ITEMS[opt].parent, int(not R.ITEMS[opt].default) if not R.ITEMS[opt].is_choice else next(
    c[0] for c in R.ITEMS[opt].choices if c[0] != R.ITEMS[opt].default)
off = dict(d, **{par: 0})
check(f"key: an option of an item left out ({opt} of {par}) does not change it",
      K.cloud_key(off, SHA) == K.cloud_key(dict(off, **{opt: alt}), SHA))
on = dict(d, **{par: 1})
check("key: ... and does with its item built", K.cloud_key(on, SHA) != K.cloud_key(dict(on, **{opt: alt}), SHA))
reserve = [k for k, it in R.ITEMS.items() if it.no_image]
check("key: the reserve items (not in the image) do not change it",
      reserve and K.cloud_key(dict(d, **{k: 8 for k in reserve}), SHA) == key)
check("canonical: sorted KEY=value lines, the retired items at their image value, no reserve item",
      K.canonical(d).splitlines() == sorted(K.canonical(d).splitlines()) and
      all(f"{k}={v}" in K.canonical(d).splitlines() for k, v in R.HASH_AS.items()) and
      not any(ln.split("=")[0] in reserve for ln in K.canonical(d).splitlines()))
text = C.dump(dict(d, **{par: 1, opt: alt}), "a name").splitlines()
shuffled = text[:]
random.Random(3).shuffle(shuffled)
e1 = K.evaluate({"text": "\n".join(text), "commit": SHA})
e2 = K.evaluate({"text": "\n".join(shuffled), "commit": SHA})
check("key: a .config's lines in any order give the same key", e1["key"] == e2["key"] and e1["key"])
for n in C.profile_names():
    with contextlib.redirect_stderr(io.StringIO()):
        cfg, _ = C.load_profile(n)
        ev = K.evaluate({"text": C.dump(cfg), "commit": SHA, "costs": costs})
    if ev["key"] != K.cloud_key(cfg, SHA) or ev["canonical"] != K.canonical(cfg):
        check(f"evaluate: profile {n}: its key and canonical form", False)

# ---- savings (core.savings_of): an item's options already off are not saved again when it goes off
lite = dict(C.defaults(), UI=1, ED_LIVE=0)
lite_off = dict(lite, **{k: 0 for k in R.ITEMS if k.startswith("ED_") and k != "ED_LIVE"})
ed_live = K.savings_of(dict(C.defaults(), UI=1), costs)["EDITOR"]["flash"]
check("savings: the web editor with live editing off saves under half of what it saves with it, the ED_* items "
      "on or off (2026-10-18: 24.5 KB shown, 7.2 KB measured)",
      all(0 < K.savings_of(c, costs)["EDITOR"]["flash"] < ed_live // 2 for c in (lite, lite_off)))


# ---- proposals (core.proposals): several ways to fit, each with its changes and its result
def everything_on():
    cfg = C.defaults()
    for k, it in R.ITEMS.items():
        if k not in ("RESERVE_UNDO_KB", "RESERVE_FLASH_KB"):
            cfg[k] = max(c[0] for c in it.choices) if it.is_choice else 1
    return cfg


tight = dict(C.defaults(), ENG_PHYS=1, RESERVE_FLASH_KB=200)
with contextlib.redirect_stderr(io.StringIO()):
    fitting = C.load_profile("synth")[0]
check("proposals: a configuration that fits has none", K.fits(K.budget(fitting, costs)["total"], fitting) and not any(o > 0 for _, _, o in K.fits(K.budget(fitting, costs)["total"], fitting).values()) and K.proposals(fitting, costs) == [])
with contextlib.redirect_stderr(io.StringIO()):
    P1, P2 = K.proposals(tight, costs), K.proposals(everything_on(), costs)
    P1b = K.proposals(tight, costs)
check("proposals: over budget: 1 to 4 of them, the same twice (deterministic), JSON-serialisable",
      P1 and len(P1) <= K.PROPOSAL_MAX and P2 and P1 == P1b and json.loads(json.dumps(P1)) == P1)
check("proposals: each one's values are the configuration with exactly its changes, no error it did not have, "
      "its 'fits' is no gauge over",
      all(p["fits"] == (not any(o > 0 for _, _, o in K.fits(K.budget(p["values"], costs)["total"], p["values"]).values())) and not [e for e in K.validate(p["values"])[0]
          if e not in K.validate(tight)[0]] and {c["key"] for c in p["changes"]} <= {k for k in R.ITEMS if p["values"][k] != tight[k]}
          for p in P1))
least = next((p for p in P1 if p["id"] == "least"), None)
fitted, _ = K.fit(tight, costs)
check("proposals: 'least loss' is core.fit's result (the CLI's) when that fits", fitted is None or (least and least["values"] == fitted))
check("proposals: fitting ones come first; when none fits the closest are shown (fits False)",
      [p["fits"] for p in P1] == sorted((p["fits"] for p in P1), reverse=True) and P2 and any(not p["fits"] for p in P2) == (not any(p["fits"] for p in P2)) or any(p["fits"] for p in P2))
check("proposals: the reserves proposal lowers RESERVE_FLASH_KB first",
      any(p["id"] == "reserves" and p["changes"][0]["key"] == "RESERVE_FLASH_KB" and p["values"]["RESERVE_FLASH_KB"] == 0 for p in P1))
check("proposals: 'keep my engines' leaves every built engine as it was",
      all(p["values"][k] == base[k] for base, ps in ((tight, P1), (everything_on(), P2)) for p in ps if p["id"] == "engines"
          for k, it in R.ITEMS.items() if it.group == "Synth engines"))
check("proposals: a dropped item's dependants are changed with it (none left with a new error)",
      all(not [e for e in K.validate(p["values"])[0] if e not in K.validate(everything_on())[0]] for p in P2))

# ---- the JSON API
ev = K.evaluate({"config": {"ENG_FM6": 1, "MIDI_CLKOUT": 1}, "commit": SHA, "costs": costs})
check("evaluate: JSON in, JSON out, the merged key warned about, the budget and the fit",
      json.loads(json.dumps(ev)) == ev and ev["error"] is None and any("merged" in w for w in ev["parse_warnings"])
      and ev["budget"] and set(ev["fits"]) == set(K.REGIONS) and ev["cfg_hash"] == f"{K.cfg_hash(ev['config']):08x}")
check("evaluate: an unknown key: a warning, strict: an error",
      K.evaluate({"config": {"NOPE_X": 1}})["parse_warnings"] and
      "NOPE_X" in (K.evaluate({"config": {"NOPE_X": 1}, "strict": True})["error"] or ""))
check("evaluate: a forbidden key, a bad line, a non-integer value: an error, not an exception",
      all(K.evaluate(r)["error"] for r in ({"config": {next(iter(R.FORBIDDEN)): 1}}, {"text": "ENG_FM6 = yes"},
                                           {"config": {"ENG_FM6": "1"}}, {"config": {"ENG_FM6": True}}, [1])))
bad = K.evaluate({"config": {k: 0 for k, it in R.ITEMS.items() if it.group == "Synth engines" and not it.parent}})
check("evaluate: no synth engine: an error naming the engines, not ok",
      not bad["ok"] and any("ENG_FM6" in e["keys"] for e in bad["errors"]))
check("describe: JSON with the registry and NEEDS_AUTO as configure read them",
      json.loads(json.dumps(K.describe()))["needs_auto"] == list(C.NEEDS_AUTO) and
      len(K.describe()["registry"]["items"]) == len(R.ITEMS))
out = io.StringIO()
with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
    rc = O.main(["cloud-key", "--commit", SHA, "user-default"])
    ud = C.load_profile("user-default")[0]
check("optimist.py cloud-key PROFILE: the key, then the canonical form",
      rc == 0 and out.getvalue().startswith(f"# key {K.cloud_key(ud, SHA)}\n") and
      out.getvalue().endswith(K.canonical(ud)))


# ---- before / after: configure.py at REF against this one
def old_configure(ref, real=False):
    """configure.py as it was at ref, run against this tree's registry, backports and costs"""
    src = subprocess.run(["git", "-C", str(ROOT), "show", f"{ref}:tools/builder/configure.py"], capture_output=True,
                         text=True, check=True).stdout
    mod = types.ModuleType("configure_old")
    mod.__file__ = str(BUILDER / "configure.py")         # (its paths: this tree's)
    exec(compile(src, f"{ref}:tools/builder/configure.py", "exec"), mod.__dict__)
    if not real:                                        # (as here: the estimate only)
        mod.exact_sizes, mod.exact_builds = C.exact_sizes, C.exact_builds
    return mod


def results(M, cfg):
    """everything the rules say about cfg, as plain data"""
    with contextlib.redirect_stderr(io.StringIO()):
        err, warn, note = M.validate(cfg)
        b, plain = M.budget(cfg, costs), M.budget(cfg, costs, use_exact=False, anchor=False)
        out = {"validate": [[(str(i), i.keys) for i in x] for x in (err, warn, note)],
               "conflicts": {k: [str(e) for e in v] for k, v in M.conflicts(cfg).items()},
               "flags": M.flags(cfg), "header": M.header(cfg, "x"), "hash": M.cfg_hash(cfg), "bits": M.cfg_bits(cfg),
               "effective": M.effective(cfg), "dump": M.dump(cfg, "n"), "reparse": M.parse(M.dump(cfg, "n")),
               "budget": b, "plain": plain, "fits": M.fits(b["total"], cfg), "terms": M.model_terms(cfg),
               "savings": M.savings_of(cfg, costs), "over": M.over_any(cfg, costs),
               "undo": (M.undo_ring(b["total"]), M.undo_short(cfg, b["total"])), "fit": M.fit(cfg, costs)}
    return json.loads(json.dumps(out, default=list))


def random_cfg(rng):
    cfg = C.defaults()
    for k, it in R.ITEMS.items():
        r = rng.random()
        if r < 0.3:
            cfg[k] = rng.choice([c[0] for c in it.choices]) if it.is_choice else int(not cfg[k])
        elif r < 0.31:
            cfg[k] = 7                                  # (an invalid value: validate's errors)
    return cfg


def differential(ref, n, seed):
    old = old_configure(ref)
    with contextlib.redirect_stderr(io.StringIO()):
        cfgs = [(f"profile {p}", C.load_profile(p)[0]) for p in C.profile_names() + C.my_profile_names()]
    rng = random.Random(seed)
    cfgs += [(f"random {i}", random_cfg(rng)) for i in range(n)]
    diff = [name for name, cfg in cfgs if results(old, cfg) != results(C, cfg)]
    check(f"{ref}: {len(cfgs)} configurations ({n} random): validate, flags, budget, fit ... identical",
          not diff)
    if diff:
        print("  differ: " + ", ".join(diff[:10]))
    check(f"{ref}: each item's own delta, the kits' shared part, the defaults: identical", once(old) == once(C))
    # real builds' sizes (build/exact-sizes.json): two made-up builds, one exact hit each and anchors for the rest
    fake = {}
    for _, cfg in cfgs[:2]:
        fake[f"{C.cfg_hash(cfg):08x}-{C.source_state()}"] = dict(
            {r: v + 1000 for r, v in K.budget(cfg, costs)["total"].items()}, **C.anchor_record(cfg))
    saved = C.exact_sizes, C.exact_builds
    C.exact_sizes = REAL_EXACT[0]                       # (the real lookup again, on the made-up builds)
    old = old_configure(ref, real=True)
    for M in (C, old):
        M.exact_builds = lambda: fake
    with contextlib.redirect_stderr(io.StringIO()):
        same = all(old.budget(cfg, costs) == C.budget(cfg, costs) and old.fit(cfg, costs) == C.fit(cfg, costs)
                   for _, cfg in cfgs[:40])
        hit = C.budget(cfgs[0][1], costs)["exact"]
    C.exact_sizes, C.exact_builds = saved
    check(f"{ref}: with real builds' sizes (exact, anchors): budget and fit identical", same and hit)


def once(M):
    """what does not depend on a configuration"""
    return json.loads(json.dumps(
        {"alone": {f"{k}={v}": M.item_delta_alone(costs, k, v) for k, it in R.ITEMS.items()
                   for v in ([c[0] for c in it.choices] if it.is_choice else (0, 1))},
         "kits": M.kit_shared(costs), "defaults": M.defaults(), "needs_auto": list(M.NEEDS_AUTO)}))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--against", metavar="REF", help="compare with configure.py at this git ref")
    ap.add_argument("--random", type=int, default=200, help="random configurations (with --against)")
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    if a.against:
        differential(a.against, a.random, a.seed)
    print("builder core test " + ("FAILED" if fails else "passed"))
    sys.exit(1 if fails else 0)

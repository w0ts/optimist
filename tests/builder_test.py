#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's plain module (tools/builder: registry.py, backports.py, configure.py): the registry's
rules, provenance and the X0X notices, items never offered, every profile valid, the header, parsing, the fit
solver. No build here (tools/builder/verify.py builds); run by tests/run_tests.sh."""
import argparse
import contextlib
import io
import sys
from pathlib import Path

from builder_path import ROOT
import configure as C  # noqa: E402
import optimist as O  # noqa: E402
import registry as R  # noqa: E402
import room as RM  # noqa: E402
from ble_room_rule import expected_room, off_value, with_ble  # (tests/: the rule, from costs.json alone)

_real_builds = C.exact_builds                            # (the drift check below reads the real builds)


def real_sizes(cfg):
    e = _real_builds().get(C.exact_key(cfg))
    return {r: e[r] for r in C.REGIONS} if isinstance(e, dict) and all(r in e for r in C.REGIONS) else None


C.exact_sizes = lambda cfg: None                        # (these checks are of the estimate: a real build's sizes in build/ must not replace it)
C.exact_builds = lambda: {}                             # (nor anchor it: budget's anchors are costs.json's alone here)
fails = 0


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


items = R.ITEMS
check("registry: unique stable bits, one level of options",
      len({it.bit for it in items.values()}) == len(items) and
      all(not it.parent or not items[it.parent].parent for it in items.values()))
x0x = [it for it in items.values() if it.provenance and "fm1-x0x" in (it.provenance.project + it.provenance.url)]
check(f"every X0X-derived item ({len(x0x)}) names Charles Vestal and shows a notice",
      len(x0x) >= 5 and all(it.notice and "Charles Vestal" in it.notice and it.provenance.author == "Charles Vestal"
                            for it in x0x))
check("X0X's break player (used by permission only, no licence) is never offered",
      "X0X_BREAKS" in R.FORBIDDEN and not any("break" in (it.label + it.desc).lower() and it in x0x
                                               for it in items.values()))
try:
    C.parse("X0X_BREAKS=1\n")
    refused = False
except C.ConfigError:
    refused = True
check("... a .config naming it is refused", refused)
# the dropped visualiser (registry.py RETIRED): a .config that still names VIS loads, says "dropped", builds as VIS = 0
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nVIS=1\nICONS=0\n")
_h, _ = C.flags(_cfg)
check("a .config naming the dropped VIS loads, warns \"dropped\" and keeps its other keys",
      "VIS" not in _cfg and "VIS" not in R.ITEMS and "dropped" in _w.getvalue() and "VIS" in _w.getvalue() and
      _cfg["ICONS"] == 0)
check("... and builds as FELUCCA_VIS 0 (the header has no such switch on)", not _h.get("FELUCCA_VIS", 0))
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _c2, _ = C.parse("VIS=1\n", strict=False)
    _a = argparse.Namespace(config=None, profile=None, set=["VIS=1"], name=None, ble_drop=None)
    _c3, _ = C.resolve_cli(_a)[:2]
check("... also non-strict and as --set VIS=1: a warning, no error", _w.getvalue().count("warning:") == 2 and "VIS" not in _c3)
# SIMD (bit 57) withdrawn 2026-10-12, wrong on the real FM-1: no longer offered; a .config or --set with SIMD=1 loads,
# says why, and builds without it (FELUCCA_SIMD not in the header: the firmware's default, 0)
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nSIMD=1\nASM=1\nICONS=0\n")
    _a = argparse.Namespace(config=None, profile=None, set=["SIMD=1"], name=None, ble_drop=None)
    _c3, _ = C.resolve_cli(_a)[:2]
_h, _ = C.flags(_cfg)
check("SIMD: not offered (no item, not in the menu's JSON), its bit 57 not reused, no cost entry",
      "SIMD" not in R.ITEMS and "SIMD" in R.RETIRED and all(it.bit != 57 for it in R.ITEMS.values()) and
      all(d["key"] != "SIMD" for d in R.to_json()["items"]) and "SIMD" not in C.load_costs().get("deltas", {}))
check("SIMD=1 in a .config and as --set: loads, warns \"wrong on the real FM-1, 2026-10-12\" each time, built without it",
      "SIMD" not in _cfg and "SIMD" not in _c3 and _cfg["ICONS"] == 0 and
      _w.getvalue().count("SIMD: wrong on the real FM-1, 2026-10-12") == 2 and "FELUCCA_SIMD" not in _h and
      "FELUCCA_SIMD " not in C.header(_cfg))
check("no shipped profile or estimate configuration names SIMD",
      not [p.name for d in (C.PROFILES, ROOT / "tools" / "builder" / "estimate") for p in d.glob("*.config")
           if any(ln.startswith("SIMD=") for ln in p.read_text().splitlines())])
# the web editor's v13 and v14 commands are twelve builder items (ED_*): options of OTA (the editor), on by default, each its own flag and INFO tag
ED = {"ED_FXP": "FELUCCA_ED_FXP", "ED_DLAN": "FELUCCA_ED_DLAN", "ED_SCRN": "FELUCCA_ED_SCRN", "ED_SONG": "FELUCCA_ED_SONG",
      "ED_TOOLS": "FELUCCA_ED_TOOLS", "ED_SCRS": "FELUCCA_ED_SCRS", "ED_FAVS": "FELUCCA_ED_FAVS", "ED_SCOPE": "FELUCCA_ED_SCOPE",
      "ED_UNDO": "FELUCCA_ED_UNDO", "ED_NOTE": "FELUCCA_ED_NOTE", "ED_SOLO": "FELUCCA_ED_SOLO", "ED_STEPLANE": "FELUCCA_ED_STEPLANE"}
check("web editor items: the twelve ED_* exist, options of OTA, on by default, one flag each",
      all(k in R.ITEMS and R.ITEMS[k].parent == "OTA" and R.ITEMS[k].default == 1 and R.ITEMS[k].flag == f for k, f in ED.items()))
check("... the shipped profiles set them only where the slot asks: user-default leaves SCOPE out (the Optimist UI build links), x0x-drums (it fills the slot to the byte) leaves FXP, FAVS, SCOPE and SONG out",
      {p.stem: sorted(ln.strip() for ln in p.read_text().splitlines() if ln.split("=")[0] in ED) for p in C.PROFILES.glob("*.config")
       if any(ln.split("=")[0] in ED for ln in p.read_text().splitlines())} ==
      {"user-default": ["ED_SCOPE=0"], "x0x-drums": ["ED_FAVS=0", "ED_FXP=0", "ED_SCOPE=0", "ED_SONG=0"]})
_fl, _ = C.flags(dict(C.defaults()))
check("... the default build has every flag on", all(_fl[f] == 1 for f in ED.values()))
_fl, _ = C.flags(dict(C.defaults(), ED_FXP=0, ED_SCOPE=0))
check("... ED_FXP=0 ED_SCOPE=0 switch only those two flags off", _fl["FELUCCA_ED_FXP"] == 0 and _fl["FELUCCA_ED_SCOPE"] == 0 and
      all(_fl[f] == 1 for k, f in ED.items() if k not in ("ED_FXP", "ED_SCOPE")))
check("... the header carries them (-D from the generated config: FELUCCA_ED_FXP 0)", "#define FELUCCA_ED_FXP 0" in C.header(dict(C.defaults(), ED_FXP=0), "t"))
_fl, _ = C.flags(dict(C.defaults(), OTA=0, ED_FXP=0))
check("... with the editor (OTA) left out its options are ignored (their default, no effect)", _fl["FELUCCA_OTA"] == 0 and _fl["FELUCCA_ED_FXP"] == 1)
check("... the commands that read the Optimist UI cost nothing without it (NEEDS), ED_FAVS nothing without the favorites",
      all(C.NEEDS[k].get("UI") == 1 for k in ("ED_SCRN", "ED_SCRS", "ED_SCOPE", "ED_TOOLS")) and C.NEEDS["ED_TOOLS"].get("SEQTOOLS") == 1 and
      C.NEEDS["ED_FAVS"].get("FAVORITES") == 1 and
      not C.counted(dict(C.defaults(), UI=0), "ED_SCRS") and C.counted(dict(C.defaults(), UI=1), "ED_SCRS"))
check("... their bits are the provisional 144..149, 156, 157 (150 is retired, 151 REV_ROOM) and v14's 158, 159, 165, 166 (160 CHORD_NAMES, 161..164 and 175.. the backports')",
      sorted(R.ITEMS[k].bit for k in ED) == [144, 145, 146, 147, 148, 149, 156, 157, 158, 159, 165, 166])
# SIZE (bit 70) and IDLE (bit 55) are retired, always built: an old .config naming them loads, warns, builds as ever
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nSIZE=0\nIDLE=0\nICONS=0\n")
_h, _e = C.flags(_cfg)
check("a .config with the retired SIZE=0 / IDLE=0 loads, warns \"dropped\" for each and keeps its other keys",
      "SIZE" not in R.ITEMS and "IDLE" not in R.ITEMS and "SIZE" not in _cfg and "IDLE" not in _cfg and
      _w.getvalue().count("dropped") == 2 and _cfg["ICONS"] == 0)
check("... and the build has no switch for them (FELUCCA_IDLE, FELUCCA_SIZE)", "FELUCCA_IDLE" not in _h and "FELUCCA_SIZE" not in _e)
# an item this builder does not know (a newer builder's, or one removed without a RETIRED line): ignored with a
# warning, never an error, so a saved .config always loads
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nNO_SUCH_ITEM=1\nICONS=0\n")
check("a .config naming an item this builder does not know loads, warns once and keeps its other keys",
      "NO_SUCH_ITEM" not in _cfg and _cfg["ICONS"] == 0 and _w.getvalue().count("warning:") == 1 and
      "NO_SUCH_ITEM" in _w.getvalue())
ported =[it for it in items.values() if it.provenance]
check(f"ported items ({len(ported)}) carry project, author and licence",
      all(it.provenance.project and it.provenance.author and it.provenance.licence for it in ported))
# every item explains itself to the user: a real description (the menu shows it) and a label that is more than the
# flag's name (docs/BUILDER.md; tools/builder/registry.py and backports.py)
MIN_DESC = 40
short = [k for k, it in items.items() if len(it.desc.strip()) < MIN_DESC]
check(f"every item ({len(items)}) has a description of at least {MIN_DESC} characters" + (f": {short}" if short else ""),
      not short)
named = [k for k, it in items.items()
         if not it.label.strip() or it.label.strip().lower() in {k.lower(), it.flag.lower(),
                                                                  it.flag.lower().replace("felucca_", "")}]
check("no label is just the item's key or flag name" + (f": {named}" if named else ""), not named)
doc = (ROOT / "docs" / "BUILDER.md").read_text()
undoc = [k for k, it in items.items() if f"`{k}`" not in doc or it.desc.strip() not in doc.replace("\n", " ")]
check("docs/BUILDER.md lists every item with the menu's description" + (f": {undoc}" if undoc else ""), not undoc)
ok = True
for p in C.profile_names():
    cfg, name = C.load_profile(p)
    err, _, _ = C.validate(cfg)
    ok &= not err and bool(name)
check(f"every profile ({', '.join(C.profile_names())}) is valid", ok and len(C.profile_names()) >= 4)
cfg = C.defaults()
cfg["ENG_FM6"] = 0
h = C.header(cfg, "test")
check("the header: a switch off, the name, the hash, the bits",
      "#define FELUCCA_ENG_FM6 0" in h and 'FELUCCA_CFG_NAME "test"' in h and "FELUCCA_CFG_BITS {" in h)
bad = dict(C.defaults(), **{k: 0 for k in items if items[k].group == "Synth engines" and not items[k].parent})
check("no synth engine: an error", any("synth engine" in e for e in C.validate(bad)[0]))
bad = dict(C.defaults(), FM6_MARK1=0, FM6_MODERN=0, FM6_OPL=0)
check("FM6 without a mode: an error", any("FM6" in e for e in C.validate(bad)[0]))
mf = dict(C.defaults(), FM6_MKI_FLASH=1)
check("MARK I tables in flash: off by default, its notice says the hardware cost is not measured",
      not C.defaults()["FM6_MKI_FLASH"] and any("not measured" in n for n in C.validate(mf)[2]) and
      "XIP" in items["FM6_MKI_FLASH"].desc and not C.validate(mf)[0])
check("... without MARK I: a warning", any("MARK I tables" in w for w in C.validate(dict(mf, FM6_MARK1=0))[1]))
bad = dict(C.defaults(), DRUM_SYNTH=0, DRUM_SAMPLED=0)
check("no drum source: an error", any("drum source" in e for e in C.validate(bad)[0]))
w = dict(C.defaults(), FM6_KEYS=0, FM6_SYSEX=0)
check("FM6 without editor and SysEx: a warning (preset-only)", any("preset-only" in x for x in C.validate(w)[1]))
check("DRUM_STEP is on by default (the user-default build has the drum steps on the keys), the store's switches stay off",
      C.defaults()["DRUM_STEP"] == 1 and not any(C.defaults().get(k) for k in ("MICRO", "FILLS", "PLOCK", "CHANCE", "MOTION", "SL24_XSTEP")))
sx = dict(C.defaults(), MICRO=1, FILLS=1, PLOCK=1, CHANCE=1, MOTION=1)
check("the store's switches need SL24_XSTEP: the validation says why (the CLI prints it, it used to exit 1 silently)",
      any("SL24_XSTEP" in e for e in C.validate(sx)[0]) and not C.validate(dict(sx, SL24_XSTEP=1))[0])
_err = io.StringIO()
with contextlib.redirect_stderr(_err):
    _r = C.build(sx, "x", echo=True)
check("build() of that invalid configuration builds nothing and says why on stderr", not _r[0] and "SL24_XSTEP" in _err.getvalue())
if "MOTION" in items:
    check("motion recording with 4, 8 or 16 sections: valid (its data in the section records)",
          all(not C.validate(dict(C.defaults(), MOTION=1, SECTIONS=s))[0] for s in (4, 8, 16)))
# each error names the items it concerns (the menu marks their lines in red as soon as it holds)
eng = [k for k in items if items[k].group == "Synth engines" and not items[k].parent]
cases = [("no synth engine", {k: 0 for k in eng}, set(eng)),
         ("FM6 without a mode", dict(FM6_MARK1=0, FM6_MODERN=0, FM6_OPL=0), {"ENG_FM6", "FM6_MARK1", "FM6_MODERN", "FM6_OPL"}),
         ("no drum source", dict(DRUM_SYNTH=0, DRUM_SAMPLED=0), {"DRUM_SYNTH", "DRUM_SAMPLED"}),
         ("a value out of range", dict(SECTIONS=5), {"SECTIONS"})]
ok = True
for what, ch, want in cases:
    errs = C.validate(dict(C.defaults(), **ch))[0]
    ok &= len(errs) == 1 and want <= set(errs[0].keys) and set(errs[0].keys) <= set(items)
    ok &= all(k in C.conflicts(dict(C.defaults(), **ch)) for k in want)
check("every error names its items (" + ", ".join(c[0] for c in cases) + ")", ok)
kit = next(k for k in items if k.startswith("KIT_"))
cf = C.conflicts(dict(C.defaults(), DRUM_SYNTH=0, DRUM_SAMPLED=0))
check("... an option named by an error marks its parent too (it may be folded away)",
      kit in cf and items[kit].parent in cf and not C.conflicts(C.defaults()))
check("... an error is still a plain message (str: printed, joined, searched)",
      all(isinstance(e, str) for e in C.validate(dict(C.defaults(), DRUM_SYNTH=0, DRUM_SAMPLED=0))[0]))
costs = C.load_costs()
if costs and "MOTION" in items:
    m16 = C.budget(dict(C.defaults(), MOTION=1), costs, anchor=False)["total"]
    m4 = C.budget(dict(C.defaults(), MOTION=1, SECTIONS=4), costs, anchor=False)["total"]
    s4 = C.budget(dict(C.defaults(), SECTIONS=4), costs, anchor=False)["total"]
    base = C.budget(C.defaults(), costs, anchor=False)["total"]
    pair = costs.get("pairs", {}).get("MOTION=1,SECTIONS=4")
    t4, t16 = C.model_terms(dict(C.defaults(), MOTION=1, SECTIONS=4)), C.model_terms(dict(C.defaults(), MOTION=1))
    check("MOTION measured at SECTIONS 16 and 4 (costs.json: its delta and the MOTION=1,SECTIONS=4 pair; the "
          "store's slots at SECTIONS 4 are a computed term, C.auto_store)",
          "1" in costs["deltas"].get("MOTION", {}) and pair is not None and
          all(m4[r] - s4[r] == m16[r] - base[r] + pair[r] + t4[r] - t16[r] for r in C.REGIONS) and
          not C.budget(dict(C.defaults(), MOTION=1), costs)["unmeasured"])
    sv = C.savings_of(dict(C.defaults(), MOTION=1, SECTIONS=4), costs)
    check("... switching MOTION off at SECTIONS 4 saves its delta and the pair's",
          all(sv["MOTION"][r] == m4[r] - s4[r] for r in C.REGIONS))
text = C.dump(dict(C.defaults(), ICONS=0, DLY_LEN=32768), "x")
back, name = C.parse(text)
check("a .config round trip (only what differs is written)", back["ICONS"] == 0 and back["DLY_LEN"] == 32768 and
      name == "x" and "ENG_ANALOG" not in text)
costs = C.load_costs()
if costs:
    fitted, ch = C.fit(C.defaults(), costs)
    check("fit: the default build made to fit by the estimate", fitted is not None and
          all(o <= 0 for _, _, o in C.fits(C.budget(fitted, costs)["total"]).values()))

# every engine built has a preset on the PRESETS list and every drum source built a kit, on any configuration
# validate() lets through (tools/builder/verify.py preset_cover: tests/preset_cover_test.c with the configuration's
# header and sample header). Named cases, then random ones over the sound sources (PRESET_COVER_N, _SEED)
import os  # noqa: E402
import random  # noqa: E402
import shutil  # noqa: E402
import verify as V  # noqa: E402

sets = [k for k in items if k.startswith("SET_")]
cover_cases = [(p, C.load_profile(p)[0]) for p in C.profile_names()] + [
    ("no sample set at all", dict(C.defaults(), DRUM_SAMPLED=0, **{k: 0 for k in sets})),
    ("no set of GRAIN's presets", dict(C.defaults(), SET_PIANO=0, SET_VIBES=0, SET_FLUTE=0)),
    ("only PERC (the sampled kits)", dict(C.defaults(), **{k: 0 for k in sets})),
    ("SLICE, PHYS, CZ, FM6 without its voices", dict(C.defaults(), ENG_SLICE=1, ENG_PHYS=1, ENG_CZ=1, FM6_VOICES=0)),
    ("one sampled kit, no drum synth", dict(C.defaults(), DRUM_SYNTH=0, KIT_ACOUSTIC=0, KIT_DEEP=0, KIT_TIGHT=0,
                                           KIT_BRIGHT=0)),
    ("the X0X kits", dict(C.defaults(), DRUM_X0X909=1, DRUM_X0X808=1)),
    ("ACID with the X0X kits", dict(C.defaults(), ENG_ACID=1, DRUM_X0X909=1, DRUM_X0X808=1))]
rng = random.Random(int(os.environ.get("PRESET_COVER_SEED", "1")))
cover_cases += [(f"random {i}", V.random_sources(rng)) for i in range(int(os.environ.get("PRESET_COVER_N", "12")))]
if shutil.which("cc"):
    built_n = refused = 0
    for what, cfg in cover_cases:
        if C.validate(cfg)[0]:
            refused += 1                              # (validate() refuses it: never built)
            continue
        ok, out = V.preset_cover(cfg)
        built_n += 1
        if not ok:
            print(f"preset cover: {what}: " + C.dump(cfg, what).replace("\n", " ") + "\n" + out)
        check(f"a preset per engine, a kit per drum source: {what}", ok)
        if ok and not what.startswith("random") and (ROOT / "build" / "gen").exists():
            # (the named cases: every kit on that list also makes sound, each lane hit alone: kits_sound_test.c)
            ok, out = V.kits_sound(cfg, ROOT / "build" / "host" / "gen_cover")
            if not ok:
                print(out)
            check(f"... and every kit of it is heard: {what}", ok)
    check(f"... {built_n} configurations checked, {refused} refused by validate()", built_n >= 10)
    if (ROOT / "build" / "gen").exists():
        # verify.goldens' regress binary compiles with a configuration's header (it raised NameError once)
        exe, err = V.regress_bin(C.defaults(), "builder_test")
        if not exe:
            print(err)
        check("verify: tests/regress.c compiles with the defaults' header (goldens' regress_bin)", bool(exe))
else:
    print("preset cover: skipped (no C compiler)")

# "# publish: yes" marks the shipped profiles CI builds (.github/workflows/build.yml reads config --published)
import io  # noqa: E402
import json  # noqa: E402
import tempfile  # noqa: E402
from contextlib import redirect_stderr, redirect_stdout  # noqa: E402

pub = C.published_profiles()
check(f"published profiles ({', '.join(pub)}): shipped ones, user-default among them",
      "user-default" in pub and set(pub) <= set(C.profile_names()))
out = io.StringIO()
with redirect_stdout(out):
    rc = C.main(["--published"])
check("config --published: the list as JSON (CI's matrix)", rc == 0 and json.loads(out.getvalue()) == pub)
out = io.StringIO()
with redirect_stdout(out):
    rc = C.main(["--profiles"])
rows = out.getvalue().splitlines()[1:]
check("config --profiles: every shipped profile once, the published ones marked",
      rc == 0 and len(rows) >= len(C.profile_names()) and
      all(any(r.split()[:2] == ["published", n] for r in rows) for n in pub))
shipped, mine = C.PROFILES, C.MY_PROFILES
with tempfile.TemporaryDirectory() as tmp:
    C.PROFILES, C.MY_PROFILES = Path(tmp) / "profiles", Path(tmp) / "my-profiles"
    C.PROFILES.mkdir()
    (C.PROFILES / "a.config").write_text("# name: A\n# what it is\nICONS=0\n")
    check("a profile without the flag: not published", C.published_profiles() == [])
    C.set_published("a", True)
    text = (C.PROFILES / "a.config").read_text()
    check("set_published on: the flag after the name, the rest kept",
          C.published_profiles() == ["a"] and text.startswith("# name: A\n# publish: yes\n# what it is\nICONS=0")
          and C.load_profile("a")[0]["ICONS"] == 0)
    C.set_published("a", False)
    check("... off: the same line says no", C.published_profiles() == [] and
          "# publish: no\n" in (C.PROFILES / "a.config").read_text())
    C.save_my_profile(dict(C.defaults(), ICONS=0), "b")
    try:
        C.set_published("b", True)
        refused = False
    except C.ConfigError:
        refused = True
    check("my profiles (git-ignored: CI never sees them) cannot be published", refused)
    p = C.share_profile("b")
    check("share_profile: mine moves to the shipped ones (to commit), then it can be published",
          p == C.PROFILES / "b.config" and not (C.MY_PROFILES / "b.config").exists() and "b" in C.profile_names()
          and C.set_published("b", True) and C.published_profiles() == ["b"])
    C.save_my_profile(C.defaults(), "c")
    (C.PROFILES / "c.config").write_text("ICONS=0\n")
    try:
        C.share_profile("c")
        refused = False
    except C.ConfigError:
        refused = True
    check("... never over a shipped profile of that name", refused and (C.MY_PROFILES / "c.config").exists())
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        rc_on, rc_bad = C.main(["--publish", "a"]), C.main(["--publish", "nope"])
        rc_off = C.main(["--unpublish", "b"])
    check("config --publish / --unpublish NAME (2: no such shipped profile)",
          rc_on == 0 and rc_off == 0 and rc_bad == 2 and C.published_profiles() == ["a"])
    C.save_my_profile(C.defaults(), "gone")
    p_mine, p_shipped = C.delete_profile("gone"), C.delete_profile("a")
    check("delete_profile: one of mine, or a shipped one (git has it back)",
          p_mine == C.MY_PROFILES / "gone.config" and p_shipped == C.PROFILES / "a.config" and
          "gone" not in C.my_profile_names() and "a" not in C.profile_names())
    (C.PROFILES / "user-default.config").write_text("ICONS=0\n")
    refused = []
    for n in ("user-default", "nope"):
        try:
            C.delete_profile(n)
            refused.append(False)
        except C.ConfigError:
            refused.append(True)
    check("... never user-default (make's and the CLI's default), nor a name with no profile",
          all(refused) and "user-default" in C.profile_names())
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        rc_del, rc_bad = C.main(["--delete", "b"]), C.main(["--delete", "b"])
    check("config --delete NAME (2: no such profile)", rc_del == 0 and rc_bad == 2 and "b" not in C.profile_names())
C.PROFILES, C.MY_PROFILES = shipped, mine
# the reverb's algorithms (firmware/src/fx/reverb/rev_type.c): one checkbox each under the reverb bus, at least one; their costs
revs = ("REV_ROOM", "REV_PLATE", "REV_FDN8", "SPRING")
check("reverb: one checkbox per algorithm (ROOM, PLATE, FDN8, SPRING) under the reverb bus, in that order, ROOM on",
      all(k in items and not items[k].is_choice and items[k].parent == "FX_REVERB" for k in revs) and
      items["FX_REVERB"].children[:4] == list(revs) and [items[k].default for k in revs] == [1, 0, 0, 0] and
      [items[k].label for k in revs] == ["reverb: ROOM", "reverb: PLATE (Dattorro)", "reverb: FDN8 (long, lush)",
                                         "reverb: SPRING"] and
      [items[k].flag for k in revs] == ["FELUCCA_REV_ROOM", "FELUCCA_REV_PLATE", "FELUCCA_REV_FDN8", "FELUCCA_SPRING"])
check("reverb: the one-tank choice REVERB is gone, its bit 150 not reused; ROOM 151, PLATE 152, FDN8 153",
      "REVERB" not in items and 150 not in {it.bit for it in items.values()} and
      [items[k].bit for k in revs[:3]] == [151, 152, 153])
check("reverb: REV_POOL and REV_HALF relabelled",
      items["REV_POOL"].label == "reverb buffers in the pool (saves ~17 KB RAM)" and
      items["REV_HALF"].label == "half-rate reverb (half the RAM, no top octave)")
none = dict(C.defaults(), REV_ROOM=0)
e = C.validate(none)[0]
check("reverb: the bus with no algorithm ticked: an error naming the bus and the four",
      any("reverb bus needs an algorithm" in x and set(x.keys) == {"FX_REVERB", *revs, "REV_AIRWIN"} for x in e))
check("reverb: ... no error with one of them (PLATE alone, SPRING alone) or with the bus off",
      not C.validate(dict(none, REV_PLATE=1))[0] and not C.validate(dict(none, SPRING=1))[0] and
      not C.validate(dict(none, FX_REVERB=0))[0])
f4, _ = C.flags(dict(C.defaults(), REV_PLATE=1, REV_FDN8=1, SPRING=1))
check("reverb: all four ticked: each its FELUCCA_ switch at 1 in the header",
      all(f4[items[k].flag] == 1 for k in revs))
check("reverb: an older .config's REVERB=2 (FDN8 alone) reads as the checkboxes",
      [C.parse("REVERB=2\n")[0][k] for k in revs[:3]] == [0, 0, 1] and
      [C.parse("REVERB=0\n")[0][k] for k in revs[:3]] == [1, 0, 0])
costs = C.load_costs()
four = dict(C.defaults(), REV_PLATE=1, REV_FDN8=1, SPRING=1)
b4 = C.budget(four, costs) if costs else None
check("reverb: every algorithm's cost measured (costs.json: ROOM off, PLATE, FDN8, SPRING on; the pairs)",
      bool(costs) and all(C.item_delta(costs, k, v) for k, v in (("REV_ROOM", 0), ("REV_PLATE", 1), ("REV_FDN8", 1),
                                                                   ("SPRING", 1))) and
      not [k for k in b4["unmeasured"] if k in revs] and "REVERB" not in costs["deltas"])
# every registry item (the backports' too) has a measured cost for each value it can be set to besides its default: an
# entry in costs.json "deltas", all zeros when it was measured and costs nothing (a missing entry is "not measured",
# which the builder shows as free). Fix: python3 tools/builder/measure_costs.py --missing (make costs).
def unmeasured_items(costs):
    out = []
    for k, it in items.items():
        vals = [c[0] for c in it.choices] if it.is_choice else [0, 1]
        out += [f"{k}={v}" for v in vals if v != it.default and str(v) not in costs.get("deltas", {}).get(k, {})]
    return out


um = unmeasured_items(costs)
check("costs.json: every registry and backport item has a measured cost",
      bool(costs) and not um)
if um:
    print(f"  not measured: {', '.join(um)}\n  fix: run python3 tools/builder/measure_costs.py --missing")
import measure_costs as MC  # noqa: E402
check("costs.json: every pair measure_costs.py defines is measured (run measure_costs.py --missing)",
      not MC.missing_pairs(costs))
_old = {"flash": 1000, "ram": 200, "pool": 50, "ramtext": 400}
with redirect_stdout(io.StringIO()):
    _moved = MC.refreshed_base(_old, dict(_old, ramtext=488))       # (88 B: under the 256 B fit margin, was kept)
    _same = MC.refreshed_base(_old, dict(_old))
    _first = MC.refreshed_base(None, dict(_old))
check("measure_costs --missing: a base that moved by even 88 B is refreshed",
      _moved["ramtext"] == 488 and _same == _old and _first == _old)
check("costs.json: no cost for an item the registry no longer has", not [k for k in costs["deltas"] if k not in items])
check("costs.json: every region of every measured entry present",
      all(set(C.REGIONS) <= set(e) for d in costs["deltas"].values() for e in d.values()))
check("builder: a default configuration has nothing unmeasured", not C.budget(C.defaults(), costs)["unmeasured"])
# the five sampled kits share the PERC samples: each kit alone costs next to nothing, all five off drop them (costs.json
# "pairs"), so the estimate with every kit off equals the one with the sampled drums off, to ~1 %
kits = C.kit_keys()
shared = C.kit_shared(costs)
check("sampled kits: the pair for all five off is measured and the shared samples are tens of KB",
      len(kits) == 5 and shared is not None and shared["flash"] > 50000)
if shared:
    all_on = C.budget(C.defaults(), costs, anchor=False)["total"]["flash"]
    kits_off = C.budget(dict(C.defaults(), **{k: 0 for k in kits}), costs, anchor=False)["total"]["flash"]
    sampled_off = C.budget(dict(C.defaults(), DRUM_SAMPLED=0), costs, anchor=False)["total"]["flash"]
    check("sampled kits: all five off saves the shared samples in the estimate",
          abs((all_on - kits_off) - shared["flash"]) <= 1 and abs(kits_off - sampled_off) < 0.01 * all_on)
    check("sampled kits: with the sampled drums off the kits' pair is not counted twice",
          C.budget(dict(C.defaults(), DRUM_SAMPLED=0, **{k: 0 for k in kits}), costs, anchor=False)["total"] == C.budget(
              dict(C.defaults(), DRUM_SAMPLED=0), costs, anchor=False)["total"])
    two = dict(C.defaults(), **{k: 0 for k in kits[1:]})
    sv = C.savings_of(two, costs)
    check("sampled kits: the last ticked one's saving is the samples, the others' next to nothing",
          abs(sv[kits[0]]["flash"] - shared["flash"]) < 1024 and C.is_last_kit(two, kits[0]) and
          abs(C.savings_of(C.defaults(), costs)[kits[1]]["flash"]) < 1024 and not C.is_last_kit(C.defaults(), kits[0]))
# BLE replaces samples (tools/builder/room.py), BUILDER.md "Making room for BLE": ticking BLE where the build would
# overflow removes ONE item, the smallest single item that frees enough, the sample sets first and then the other items
# (FLUTE, the default choice, when it alone is enough); the message offers the others; another pick brings the first
# one back when the build then fits; a profile where BLE fits loses nothing; BLE off gives back what was removed unless
# the user changed it by hand; --ble-drop is the same choice headless. Nothing here is pinned: every figure comes from
# costs.json as it is (BLE's measured cost, with and without BLE_DIAG), and the expected item is computed per profile
# below from the budget alone, not from room.py, so the checks hold whatever BLE and the profiles cost.
dflt_real, _ = C.load_profile("user-default")
check("BLE, measured cost (costs.json): user-default with BLE fits as it is",
      not C.over_any(dict(dflt_real, BLE=1), costs))


def cli(profile, sets, **kw):
    ns = dict(config=None, profile=profile, set=sets, name=None, ble_drop=None)
    return C.resolve_cli(argparse.Namespace(**dict(ns, **kw)))[0]


def refused(profile, sets, drop):
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            cli(profile, sets, ble_drop=drop)
    except C.ConfigError:
        return True
    return False


def room_scenarios(prof, diag):
    tag = f"BLE room, {prof}{' + BLE_DIAG' if diag else ''}"
    base, _ = C.load_profile(prof)
    sets = ["BLE=1"] + (["BLE_DIAG=1"] if diag else [])
    withble = with_ble(base, diag)
    over, order, chosen = expected_room(withble, costs)
    cfg1, rm = RM.after_toggle(base, withble, "BLE", RM.NONE, costs)
    real = (costs.get("checks", {}).get(prof) or {}).get("sizes", {})     # (the profile's own measured build, without BLE)
    check(f"{tag}: the profile builds without BLE (its measured flash, RAM and pool are inside the device's)",
          bool(real) and all(real[r] <= C.LIMITS[r] for r in ("flash", "ram", "pool")))
    if not over:
        check(f"{tag}: BLE fits as it is: nothing is removed, no message",
              cfg1 == withble and not rm.dropped and not rm.note and cli(prof, sets) == withble)
        with contextlib.redirect_stdout(io.StringIO()):
            check(f"{tag}: --ble-drop where BLE fits as it is: nothing removed", cli(prof, sets, ble_drop="FLUTE") == withble)
        return
    if chosen is None:
        check(f"{tag}: BLE overflows ({sorted(over)}) and no single item frees enough: nothing is removed and the message says so",
              cfg1 == withble and not rm.dropped and "no single item frees enough" in rm.note)
        check(f"{tag}: --ble-drop FLUTE is refused (it frees too little); without it nothing is removed",
              refused(prof, sets, "FLUTE") and cli(prof, sets) == withble)
        return
    others = [k for k in order if k != chosen]
    check(f"{tag}: BLE overflows {sorted(over)}; the rule removes {chosen} ({'FLUTE, enough alone' if chosen == 'SET_FLUTE' else 'the smallest set or item that frees enough'}) and nothing else, and then it fits",
          {k for k in cfg1 if cfg1[k] != withble[k]} == {chosen} and cfg1[chosen] == off_value(chosen) and
          not C.over_any(cfg1, costs) and rm.keys() == (chosen,) and
          (chosen == "SET_FLUTE" or "SET_FLUTE" not in order))
    check(f"{tag}: the message names what BLE needs and {RM.short(chosen)}, and lists the other items that free enough",
          "BLE needs ~" in rm.note and f"{RM.short(chosen)} removed to make room" in rm.note and
          all(RM.short(k) in rm.note.split("instead")[1] for k in others[:RM.OFFERS]) and
          (RM.short(chosen) + " " + RM.kb(C.savings_of(withble, costs)[chosen]["flash"])) not in rm.note.split("instead")[-1] and
          ("pick another to remove instead" in rm.note) == bool(others))
    check(f"{tag}: every item the message offers alone frees enough and keeps the configuration valid",
          all(not C.over_any(dict(withble, **{k: off_value(k)}), costs) for k in order) and
          [k for k, _ in RM.candidates(withble, costs, over)] == order)
    if others:
        pick = others[0]
        hand = dict(cfg1, **{pick: off_value(pick)})
        cfg2, rm2 = RM.after_toggle(cfg1, hand, pick, rm, costs)
        fits_back = not C.over_any(dict(hand, **{chosen: base[chosen]}), costs)
        check(f"{tag}: {RM.short(pick)} picked instead: {RM.short(chosen)} " + ("comes back, the pick stays off, it fits"
              if fits_back else "stays removed (the build would overflow with it)"),
              (cfg2[chosen] == base[chosen] and cfg2[pick] == off_value(pick) and not rm2.dropped and
               "restored" in rm2.note and not C.over_any(cfg2, costs)) if fits_back else
              (cfg2 == hand and rm2.dropped == rm.dropped))
    small = [k for k, v in C.savings_of(withble, costs).items()
             if k not in order and k != "BLE" and not R.ITEMS[k].is_choice and 0 < v["flash"] and not C.validate(dict(withble, **{k: 0}))[0]]
    if small:
        cfg2b, rm2b = RM.after_toggle(cfg1, dict(cfg1, **{small[0]: 0}), small[0], rm, costs)
        check(f"{tag}: a pick too small to make room ({small[0]}) leaves {RM.short(chosen)} removed",
              cfg2b[chosen] == off_value(chosen) and rm2b.dropped == rm.dropped)
    cfg4, rm4 = RM.after_toggle(cfg1, dict(cfg1, BLE=0, **({"BLE_DIAG": 1} if diag else {})), "BLE", rm, costs)
    check(f"{tag}: BLE unticked: {RM.short(chosen)} is back, the configuration is the profile's again",
          cfg4[chosen] == base[chosen] and {k for k in cfg4 if cfg4[k] != base[k]} <= {"BLE_DIAG"} and
          f"{RM.short(chosen)} restored" in rm4.note and not rm4.dropped)
    byhand = dict(cfg1, **{chosen: base[chosen]})
    cfgh, rmh = RM.after_toggle(cfg1, byhand, chosen, rm, costs)
    cfg5, _ = RM.after_toggle(cfgh, dict(cfgh, BLE=0), "BLE", rmh, costs)
    check(f"{tag}: {RM.short(chosen)} ticked by hand again, then BLE off: the hand's choice stands, the overflow is shown",
          not rmh.dropped and cfgh[chosen] == base[chosen] and bool(C.over_any(cfgh, costs)) and cfg5[chosen] == base[chosen])
    mine = [k for k in sorted(R.ITEMS) if k.startswith("SET_") and k != chosen and base[k]]
    if mine:
        cfgm, rmm = RM.after_toggle(cfg1, dict(cfg1, **{mine[0]: 0}), mine[0], rm, costs)
        cfg6, _ = RM.after_toggle(cfgm, dict(cfgm, BLE=0), "BLE", rmm, costs)
        check(f"{tag}: {mine[0]} switched off by hand stays off after BLE off (never restored or touched)",
              cfg6[mine[0]] == 0 and cfg6[chosen] == base[chosen])
    with contextlib.redirect_stdout(io.StringIO()) as said:
        c_chosen = cli(prof, sets, ble_drop=chosen)
    check(f"{tag}: --ble-drop {chosen} gives the menu's configuration and prints its message",
          c_chosen == cfg1 and RM.short(chosen) + " removed to make room" in said.getvalue())
    check(f"{tag}: without --ble-drop nothing is removed (explicit stays explicit)", cli(prof, sets) == withble)
    if others:
        with contextlib.redirect_stdout(io.StringIO()):
            c_other = cli(prof, sets, ble_drop=others[0])
        check(f"{tag}: --ble-drop {others[0]}: that one goes, {chosen} stays, it fits",
              c_other[others[0]] == off_value(others[0]) and c_other[chosen] == base[chosen] and not C.over_any(c_other, costs))
    bad = ([small[0]] if small else []) + ["NOPE"]
    check(f"{tag}: --ble-drop with an item that frees too little or an unknown name: refused with a ConfigError",
          all(refused(prof, sets, b) for b in bad))


PROFILES_BLE = ("user-default", "drum-machine", "everything-that-fits", "fm-va-studio", "x0x-drums")
for _prof in PROFILES_BLE:
    for _diag in (0, 1):
        room_scenarios(_prof, _diag)
_removing = [(p, d) for p in PROFILES_BLE for d in (0, 1)
             if expected_room(with_ble(C.load_profile(p)[0], d), costs)[2]]
check("BLE room: at least one profile has BLE overflow and a single item that frees enough (the rule is exercised): "
      + ", ".join(f"{p}{'+diag' if d else ''}" for p, d in _removing), bool(_removing))
try:
    with contextlib.redirect_stdout(io.StringIO()):
        C.resolve_cli(argparse.Namespace(config=None, profile="x0x-drums", set=[], name=None, ble_drop="FLUTE"))
    _off = False
except C.ConfigError:
    _off = True
check("--ble-drop with BLE off: refused with a ConfigError", _off)
check("tools/optimist.py: build and package take --ble-drop, and the BLE test package is made with it",
      O.parser().parse_args(["build", "--set", "BLE=1", "--ble-drop", "FLUTE"]).ble_drop == "FLUTE" and
      'ble_drop="FLUTE"' in (ROOT / "tools" / "optimist.py").read_text() and
      '"BLE_DIAG=1"' in (ROOT / "tools" / "optimist.py").read_text() and "SET_FLUTE=0" not in
      (ROOT / "tools" / "optimist.py").read_text())
# the estimate against real builds (docs/BUILDER.md, Budget). configure.py budget: the real build of the nearest
# configuration whose build is known (an anchor: the defaults, costs.json "checks", a real build from this source)
# plus what the measured deltas say the items that differ change. costs.json "checks" holds measurement builds of
# every profile, of the configurations in tools/builder/estimate/ (a user's own among them) and of each one's probe
# (measure_costs.py probe: PROBE_ITEMS items changed), made in the same measure_costs.py run as the deltas.
# 1. self: each of them estimated WITHOUT its own build (its probe, PROBE_ITEMS items away, is then its nearest
#    anchor, or another check): within ESTIMATE_NEAR. What the menu meets: a few changes from a configuration
#    built before. Measured 2026-10-14: 4-5 items from an anchor, flash -2.3 to +4.8 KB, RAM within 0.3 KB, pool
#    within 0.7 KB (LTO shares and inlines code by what else is built: no sum of per-item figures does better).
#    Then without its probe either (the nearest other check or the defaults, 20-30 items away): printed only,
#    that is the sum model alone (-13 to +28 KB of flash for the two users' configurations); a configuration
#    built once is exact from then on (build/exact-sizes.json is an anchor too)
# 2. drift: costs.json against the real builds `optimist.py test` has just made from this very source
#    (prepare_tests: every check configuration, user-default, its two BLE builds and the defaults; configure.build
#    remembers their sizes in build/exact-sizes.json, keyed by configuration and source). The firmware moves
#    under costs.json at every merge; the self checks compare costs.json with itself (its own builds, as old as
#    it is), so a stale base or delta passes them. Here each estimate must be within ESTIMATE_TOL of today's real
#    build: a failure means python3 tools/optimist.py costs (measure_costs.py --missing: the base and the checks
#    again) or, after a change to an item's code, measure_costs.py --only KEY
ESTIMATE_TOL = {"flash": 1024, "ram": 256, "pool": 256, "ramtext": 256}
ESTIMATE_NEAR = {"flash": 6144, "ram": 512, "pool": 1024, "ramtext": 768}
chk = (costs or {}).get("checks", {})
check("costs.json: a real build of every check configuration and probe (run measure_costs.py --missing)",
      not MC.missing_checks(costs))
built_checks = {n: e for n, e in chk.items() if e.get("sizes") and C.anchor_cfg(e)}
check("estimate: at least four check configurations built, each with its probe",
      sum(1 for n in built_checks if not n.endswith(MC.PROBE) and n + MC.PROBE in built_checks) >= 4)
for n, e in sorted(chk.items()):
    if e.get("failed"):
        print(f"  note: check configuration {n} did not build ({e['failed']}): tools/builder/verify.py's to fix")


def _err(est, real):
    return {r: est[r] - real[r] for r in C.REGIONS}


def _within(err, tol):
    return all(abs(err[r]) <= tol[r] for r in C.REGIONS)


def _fmt(err):
    return ", ".join(f"{r} {err[r]:+,}" for r in C.REGIONS)


_tol = "+-" + "/".join(str(ESTIMATE_TOL[r]) for r in C.REGIONS) + " B"
for n, e in sorted(built_checks.items()):
    cfg, real = C.anchor_cfg(e), e["sizes"]
    b = C.budget(cfg, costs, use_exact=False, exclude=(n,))
    err = _err(b["total"], real)
    check(f"estimate of {n} without its own build (from {b['anchor']}, {b['distance']} items away): within "
          f"+-{'/'.join(str(ESTIMATE_NEAR[r]) for r in C.REGIONS)} B", _within(err, ESTIMATE_NEAR))
    print(f"  {n}: estimate - real {_fmt(err)}")
    if n.endswith(MC.PROBE):
        continue
    far = C.budget(cfg, costs, use_exact=False, exclude=(n, n + MC.PROBE))
    ferr = _err(far["total"], real)
    print(f"  {n} without its probe either (from {far['anchor']}, {far['distance']} items away): estimate - real "
          f"{_fmt(ferr)}")


def gate_configs():
    """-> [(name, cfg)] the configurations prepare_tests builds (tools/optimist.py): the defaults, then the check
    configurations (check_builds; the BLE test builds, optimist.py ble_test_configs, among them)"""
    return [("defaults (the base)", C.defaults())] + sorted(MC.check_configs().items())


_built = [(n, cfg, real_sizes(cfg)) for n, cfg in (gate_configs() if costs else [])]
_have = [x for x in _built if x[2]]
for n, cfg, real in _built:
    if real:
        err = _err(C.budget(cfg, costs, use_exact=False)["total"], real)
        check(f"drift: the estimate of {n} vs its real build from this source: within {_tol} "
              "(else: python3 tools/optimist.py costs)", _within(err, ESTIMATE_TOL))
        print(f"  {n}: estimate - today's real build {_fmt(err)}")
    elif _have:     # (some built from this source, not this one: measure_costs.py makes the checks only)
        print(f"  note: drift of {n}: no real build of it from this source (optimist.py test makes it)")
if not _have:
    print("  note: drift: no real build from this source in build/exact-sizes.json (optimist.py test makes them; "
          "--python / --no-build do not): the estimate is not held against today's firmware")
# ---- the Reserve items (RESERVE_UNDO_KB, RESERVE_FLASH_KB): headroom kept, nothing in the image
sys.path.insert(0, str(ROOT / "tools"))
import build as BUILD  # noqa: E402  (its check() is the real build's refusal)
ures, fres = items["RESERVE_UNDO_KB"], items["RESERVE_FLASH_KB"]
check("reserve: two items, first in the menu, default 0, no flag, no image, no cost",
      R.GROUPS[0] == "Reserve" and [it.key for it in R.top_level("Reserve")] == ["RESERVE_UNDO_KB", "RESERVE_FLASH_KB"] and
      ures.default == fres.default == 0 and not ures.flag and not fres.flag and ures.no_image and fres.no_image)
rcfg = dict(C.defaults(), RESERVE_UNDO_KB=32, RESERVE_FLASH_KB=64)
check("reserve: not in the header, hash or BUILD bits: the image is the same",
      C.header(rcfg) == C.header(C.defaults()) and C.cfg_hash(rcfg) == C.cfg_hash(C.defaults()) and
      C.cfg_bits(rcfg) == C.cfg_bits(C.defaults()))
check("reserve: part of the saved configuration (dump / parse round trip, in the Reserve group first)",
      C.parse(C.dump(rcfg, "r"))[0] == rcfg and C.dump(rcfg).index("# Reserve") < C.dump(dict(rcfg, ENG_FM6=0)).index("ENG_FM6=0"))
check("reserve: costs.json has a 0 delta for every value, so --missing --check passes",
      not MC.unmeasured(costs) and all(not any(d.values()) for k in ("RESERVE_UNDO_KB", "RESERVE_FLASH_KB")
                                       for d in costs["deltas"][k].values()) and
      all(len(costs["deltas"][k]) == len(R.ITEMS[k].choices) - 1 for k in ("RESERVE_UNDO_KB", "RESERVE_FLASH_KB")))
check("reserve: the budget's totals do not move", C.budget(rcfg, costs)["total"] == C.budget(C.defaults(), costs)["total"])
tot = {"flash": 500000, "ram": 80000, "pool": 300000, "ramtext": 30000}
ring = (98304 - 80000) + (344064 - 8192 - 300000)
check("reserve: the estimate shows the undo ring, (98,304 - RAM) + (344,064 - 8,192 - pool)",
      C.undo_ring(tot) == ring == 54176 and C.undo_ring(dict(tot, ram=100000, pool=400000)) == 0)
check("reserve undo: no shortfall at or above N, the shortfall below it, none with no minimum",
      C.undo_short(dict(C.defaults(), RESERVE_UNDO_KB=32), tot) == 0 and
      C.undo_short(dict(C.defaults(), RESERVE_UNDO_KB=0), tot) == 0 and
      C.undo_short(dict(C.defaults(), RESERVE_UNDO_KB=32), dict(tot, pool=330000)) == 32768 - ((98304 - 80000) + (344064 - 8192 - 330000)) and
      "undo ring" in C.fmt_budget(dict(C.defaults(), RESERVE_UNDO_KB=32), dict(costs, base=dict(costs["base"], **tot))) and
      "BELOW" in C.fmt_budget(dict(C.defaults(), RESERVE_UNDO_KB=32), dict(costs, base=dict(costs["base"], pool=330000, ram=80000))))
check("reserve undo: asking for it without the undo history is an error", C.validate(dict(C.defaults(), RESERVE_UNDO_KB=8, UNDO_HISTORY=0))[0]
      and not C.validate(dict(C.defaults(), UNDO_HISTORY=0))[0])
f0 = C.fits(tot)["flash"]
check("reserve flash: the slot limit shrinks by N KB (the fit test and the bars), the other regions do not",
      C.fits(tot, dict(C.defaults(), RESERVE_FLASH_KB=64))["flash"][1] == f0[1] - 65536 and
      C.fits(tot, dict(C.defaults(), RESERVE_FLASH_KB=64))["pool"] == C.fits(tot)["pool"] and
      C.fits(dict(tot, flash=C.LIMITS["flash"] - 70000), dict(C.defaults(), RESERVE_FLASH_KB=64))["flash"][2] < 0 and
      C.fits(dict(tot, flash=C.LIMITS["flash"] - 60000), dict(C.defaults(), RESERVE_FLASH_KB=64))["flash"][2] > 0)
check("reserve flash: over_any counts it (a configuration that fits the slot but not the slot less N)",
      "flash" in C.over_any(dict(C.defaults(), RESERVE_FLASH_KB=64), dict(costs, base=dict(costs["base"], flash=C.LIMITS["flash"] - 40000, ram=0, pool=0, ramtext=0)),
                            margin={r: 0 for r in C.REGIONS}) and
      "flash" not in C.over_any(C.defaults(), dict(costs, base=dict(costs["base"], flash=C.LIMITS["flash"] - 40000, ram=0, pool=0, ramtext=0)),
                                margin={r: 0 for r in C.REGIONS}))
# the real build's refusal (tools/build.py check): the same functions it calls at the end of every non-measurement link
BUILD.RESERVE.clear()
check("build.py: no reserve, nothing refused", BUILD.reserve_check(BUILD.APP_SLOT, 0) == [])
BUILD.RESERVE.update(RESERVE_FLASH_KB=16, RESERVE_UNDO_KB=8)
check("build.py: at the reserve is accepted", BUILD.reserve_check(BUILD.APP_SLOT - 16384, 8192) == [])
bad = BUILD.reserve_check(BUILD.APP_SLOT - 16383, 8191)
check("build.py: one byte under either reserve is refused, each by its own message",
      len(bad) == 2 and "flash" in bad[0] and "undo" in bad[1] and len(BUILD.reserve_check(BUILD.APP_SLOT - 16383, 9000)) == 1
      and len(BUILD.reserve_check(BUILD.APP_SLOT - 100, 8192)) == 1)
check("build.py: a build with no undo history has no ring to keep", BUILD.reserve_check(BUILD.APP_SLOT - 20000, None) == [])
BUILD.RESERVE.clear()

# the boot order (tools/build.py boot_order): CPU1 starts only after the UBOOT check and the boot guard, never from recovery.
# A synthetic disassembly: {function: [callee, ...]} in address order
def fake_dis(funcs):
    out, pc = [], 0x2000000
    for f, calls in funcs.items():
        out.append(f + ":")
        for c in calls + ["rts"]:
            out.append(f" {pc:x}:    00 00             \t" + (f"call 4 <{c} : 2001000 >" if c != "rts" else "rts"))
            pc += 4
    return "\n".join(out) + "\n"
GOOD = {"fm1_cstart": ["boot_hold", "bootguard_begin", "recovery_main", "fm1_main"], "boot_hold": [], "bootguard_begin": [],
        "recovery_main": ["x"], "x": [], "fm1_main": ["dual_boot"], "dual_boot": ["fm1_dual_start"], "fm1_dual_start": ["fm1_cpu1_entry"],
        "fm1_cpu1_entry": []}
check("build.py boot order: the real order passes", BUILD.boot_order(fake_dis(GOOD)) == [])
early = dict(GOOD, fm1_cstart=["boot_hold", "dual_boot", "bootguard_begin", "recovery_main", "fm1_main"])
check("build.py boot order: CPU1 started from fm1_cstart is refused", any("dual_boot is reached" in e for e in BUILD.boot_order(fake_dis(early))))
early = dict(GOOD, fm1_cstart=["fm1_dual_start", "boot_hold", "bootguard_begin", "recovery_main", "fm1_main"])
check("build.py boot order: the start before the checks is refused", any("fm1_dual_start is reached" in e for e in BUILD.boot_order(fake_dis(early))))
early = dict(GOOD, fm1_cstart=["bootguard_begin", "fm1_main", "boot_hold", "recovery_main"])
check("build.py boot order: fm1_main before the UBOOT hold is refused", any("boot_hold before fm1_main" in e for e in BUILD.boot_order(fake_dis(early))))
early = dict(GOOD, fm1_cstart=["boot_hold", "recovery_main", "bootguard_begin", "fm1_main"])
check("build.py boot order: recovery before the boot guard is refused", any("bootguard_begin before recovery_main" in e for e in BUILD.boot_order(fake_dis(early))))
rec = dict(GOOD, recovery_main=["x", "dual_boot"])
check("build.py boot order: dual_boot from recovery is refused", any("recovery_main can reach dual_boot" in e for e in BUILD.boot_order(fake_dis(rec))))
rec = dict(GOOD, recovery_main=["x"], x=["fm1_main"])
check("build.py boot order: recovery reaching fm1_main (and dual_boot behind it) is refused", any("can reach dual_boot" in e for e in BUILD.boot_order(fake_dis(rec))))
inl = {k: v for k, v in GOOD.items() if k != "dual_boot"}
check("build.py boot order: an inlined dual_boot is refused", any("not found" in e for e in BUILD.boot_order(fake_dis(inl))))
real = ROOT / "build" / "felucca.dis"
if real.exists() and "fm1_dual_start:" in real.read_text():
    check("build.py boot order: the real DUAL build passes", BUILD.boot_order(real.read_text()) == [])

# CPU1's stack (tools/build.py cpu1_stack): the deepest path from the wake interrupt, an engine's hook through mix_part's
# indirect call, a FAR call through the address its function loads. A synthetic disassembly: {function: [line, ...]}
def stack_dis(funcs):
    out, pc = [], 0x1c00000
    for f, lines in funcs.items():
        out.append(f + ":")
        for ln in lines + ["rts"]:
            out.append(f" {pc:x}:    00 00             \t{ln}")
            pc += 4
    return "\n".join(out) + "\n"
C1 = {"isr_c1_wake": ["[--sp] = {psr, rets, reti}", "[--sp] = {r3-r0}", "call 4 <fm1_cpu1_wake : 1c00100 >"],
      "fm1_cpu1_wake": ["[--sp] = {rets, r7-r4}", "call 4 <dual_job : 1c00200 >"],
      "dual_job": ["[--sp] = {rets, r7-r4}", "call 4 <mix_part : 1c00300 >"],
      "mix_part": ["[--sp] = {rets, r15-r4}", "sp += -172", "r3 = 1 <tcomp_far : 1c00500 >", "call r3", "sp += 172"],
      "phys_render": ["[--sp] = {rets, r7-r4}", "sp += -948"], "tcomp_far": ["[--sp] = {rets}", "sp += -60"],
      "ui_draw": ["sp += -9000"]}
d, path = BUILD.cpu1_stack(stack_dis(C1))
check("build.py CPU1 stack: interrupt 28 + 20 + 20 + mix_part 224 + an engine's hook (PHYS 968): 1260 B",
      d == 1260 and path[-1] == "phys_render")
C1["tcomp_far"] = ["[--sp] = {rets}", "sp += -2000"]
d, path = BUILD.cpu1_stack(stack_dis(C1))
check("build.py CPU1 stack: a FAR call (the address mix_part loads) counts; a function nothing reaches (ui_draw) not",
      d == 2296 and path[-1] == "tcomp_far")
C1["phys_render"] = ["[--sp] = {rets, r7-r4}", "sp += -4000"]
d, _ = BUILD.cpu1_stack(stack_dis(C1))
check("build.py CPU1 stack: an engine deeper than CPU1_STACK_ROOM is over it", d > BUILD.CPU1_STACK_ROOM)
C2 = {"isr_c1_wake": C1["isr_c1_wake"], "fm1_cpu1_wake": C1["fm1_cpu1_wake"],   # (dual.c dual_run, mix_part inlined)
      "dual_job": ["[--sp] = {rets, r7-r4}", "call 4 <dual_run : 1c00300 >"],
      "dual_run": ["[--sp] = {rets, r15-r4}", "sp += -172", "call r3", "sp += 172"],
      "phys_render": ["[--sp] = {rets, r7-r4}", "sp += -948"]}
d, path = BUILD.cpu1_stack(stack_dis(C2))
check("build.py CPU1 stack: mix_part inlined into dual_run, its indirect call reaches an engine's hook (1260 B)",
      d == 1260 and path[-1] == "phys_render")
if real.exists() and "dual_job:" in real.read_text():
    d, path = BUILD.cpu1_stack(real.read_text())
    check(f"build.py CPU1 stack: the real DUAL build's ({d} B, {path[-1]}) within {BUILD.CPU1_STACK_ROOM} B",
          0 < d <= BUILD.CPU1_STACK_ROOM)
print("builder test " + ("FAILED" if fails else "passed"))
sys.exit(1 if fails else 0)

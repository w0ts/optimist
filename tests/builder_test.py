#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's plain module (tools/builder: registry.py, backports.py, configure.py): the registry's
rules, provenance and the X0X notices, items never offered, every profile valid, the header, parsing, the fit
solver. No build here (tools/builder/verify.py builds); run by tests/run_tests.sh."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import configure as C  # noqa: E402
import registry as R  # noqa: E402

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
ported = [it for it in items.values() if it.provenance]
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
    m16 = C.budget(dict(C.defaults(), MOTION=1), costs)["total"]
    m4 = C.budget(dict(C.defaults(), MOTION=1, SECTIONS=4), costs)["total"]
    s4 = C.budget(dict(C.defaults(), SECTIONS=4), costs)["total"]
    base = C.budget(C.defaults(), costs)["total"]
    pair = costs.get("pairs", {}).get("MOTION=1,SECTIONS=4")
    check("MOTION measured at SECTIONS 16 and 4 (costs.json: its delta and the MOTION=1,SECTIONS=4 pair)",
          "1" in costs["deltas"].get("MOTION", {}) and pair is not None and
          all(m4[r] - s4[r] == m16[r] - base[r] + pair[r] for r in C.REGIONS) and
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
# the reverb's algorithms (firmware/src/rev_type.c): one checkbox each under the reverb bus, at least one; their costs
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
      any("reverb bus needs an algorithm" in x and set(x.keys) == {"FX_REVERB", *revs} for x in e))
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
print("builder test " + ("FAILED" if fails else "passed"))
sys.exit(1 if fails else 0)

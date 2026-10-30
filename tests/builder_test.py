#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's plain module (tools/builder: registry.py, backports.py, configure.py): the registry's
rules, provenance and the X0X notices, items never offered, every profile valid, the header, parsing, the fit
solver. No build here (tools/builder/verify.py builds); run by tests/run_tests.sh."""
import argparse
import contextlib
import io
import re
import subprocess
import sys
from pathlib import Path

from builder_path import ROOT
import configure as C  # noqa: E402
import core as K
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
check("registry: unique stable bits, one level of options (two under a group's head: DRUMS)",
      len({it.bit for it in items.values()}) == len(items) and
      all(not it.parent or not items[it.parent].parent or items[items[it.parent].parent].head for it in items.values()))
_om, _ = C.parse("# name: old\nLAYOUT=1\nSDCARD=1\nFAR_OK=0\nRAM_PACKED=1\nMISSING_WARN=0\n")
_od = C.dump(_om)
check("memory menu: an old .config with LAYOUT SDCARD RAM_PACKED and a retired FAR_OK=0 loads, MEMORY reads SD card mode, the dump names the two, not MEMORY or FAR_OK",
      _om["MEMORY"] == 2 and (_om["LAYOUT"], _om["SDCARD"], _om["RAM_PACKED"]) == (1, 1, 1) and "FAR_OK" not in _om
      and all(f"{k}=1" in _od.splitlines() for k in ("LAYOUT", "SDCARD")) and "MEMORY" not in _od and "FAR_OK" not in _od)
check("... the menu lists MEMORY alone in the group's top level, SD_RING / SD_REC under it; the five others are hidden but real items",
      [it.key for it in R.top_level("Memory management") if it.key in ("MEMORY", "RAM_PACKED", "SDCARD", "LAYOUT")] == ["MEMORY"]
      and R.ITEMS["MEMORY"].menu_children == ["SD_RING", "SD_REC"] and R.ITEMS["SDCARD"].menu_children == []
      and all(R.ITEMS[k].hidden for k in ("RAM_PACKED", "SDCARD", "LAYOUT")) and "FAR_OK" not in R.ITEMS and "FAR_OK" in R.RETIRED and R.ITEMS["RAM_PACKED"].bit == 437
      and R.ITEMS["SD_RING"].parent == "SDCARD" and R.ITEMS["SD_RING"].shown_under == "MEMORY")
check("backported items (SPRING, PUNCH_LATCH, UP_FM6, GRIDS_*) are in their parent's menu_children, like any option, in the children's order",
      all(k in R.ITEMS[R.ITEMS[k].parent].menu_children for k in ("SPRING", "PUNCH_LATCH", "UP_FM6", "GRIDS_EUCLID", "GRIDS_LFO", "GRIDS_VEL"))
      and [c for c in R.ITEMS["FX_REVERB"].children if c in R.ITEMS["FX_REVERB"].menu_children] == R.ITEMS["FX_REVERB"].menu_children)
_nd = dict(C.defaults(), DRUMS=0, LANE_FX=1, DRUM_X0X909=1)
_nf = C.flags(_nd)[0]
check("DRUMS off: every drum switch 0 (its options' options too), the drum-only items elsewhere left out, no drum source "
      "asked for, PERC not built",
      _nf["FELUCCA_DRUMS"] == 0 and _nf["FELUCCA_DRUM_X909"] == 0 and _nf["FELUCCA_X909_CYM"] == 0 and
      _nf["FELUCCA_LANE_FX"] == 0 and _nf["FELUCCA_DRUM_SYNTH"] == 0 and not C.built(_nd, "LANE_FX") and
      not C.built(_nd, "X909_CYM") and not any("drum source" in e for e in C.validate(_nd)[0]) and
      "PERC" in C.flags(_nd)[1]["FELUCCA_SAMPLES_SKIP"])
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
    _cfg, _ = C.parse("# name: old\nVIS=1\nMISSING_WARN=0\n")
_h, _ = C.flags(_cfg)
check("a .config naming the dropped VIS loads, warns \"dropped\" and keeps its other keys",
      "VIS" not in _cfg and "VIS" not in R.ITEMS and "dropped" in _w.getvalue() and "VIS" in _w.getvalue() and
      _cfg["MISSING_WARN"] == 0)
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
    _cfg, _ = C.parse("# name: old\nSIMD=1\nASM=1\nMISSING_WARN=0\n")
    _a = argparse.Namespace(config=None, profile=None, set=["SIMD=1"], name=None, ble_drop=None)
    _c3, _ = C.resolve_cli(_a)[:2]
_h, _ = C.flags(_cfg)
check("SIMD: not offered (no item, not in the menu's JSON), its bit 57 not reused, no cost entry",
      "SIMD" not in R.ITEMS and "SIMD" in R.RETIRED and all(it.bit != 57 for it in R.ITEMS.values()) and
      all(d["key"] != "SIMD" for d in R.to_json()["items"]) and "SIMD" not in C.load_costs().get("deltas", {}))
check("SIMD=1 in a .config and as --set: loads, warns \"wrong on the real FM-1, 2026-10-12\" each time, built without it",
      "SIMD" not in _cfg and "SIMD" not in _c3 and _cfg["MISSING_WARN"] == 0 and
      _w.getvalue().count("SIMD: wrong on the real FM-1, 2026-10-12") == 2 and "FELUCCA_SIMD" not in _h and
      "FELUCCA_SIMD " not in C.header(_cfg))
check("no shipped profile names SIMD",
      not [p.name for d in (C.PROFILES,) for p in d.glob("*.config")
           if any(ln.startswith("SIMD=") for ln in p.read_text().splitlines())])
# LFO2 (bit 91) retired 2026-10-16: LFO 2 is always built. An old .config or --set naming it (=0 or =1) loads, says "dropped",
# and keeps its other keys; the header has no FELUCCA_LFO2 switch; no shipped profile or test configuration names it
for _v in (0, 1):
    _w = io.StringIO()
    with contextlib.redirect_stderr(_w):
        _cfg, _ = C.parse(f"# name: old\nLFO2={_v}\nMISSING_WARN=0\n")
        _a = argparse.Namespace(config=None, profile=None, set=[f"LFO2={_v}"], name=None, ble_drop=None)
        _c3, _ = C.resolve_cli(_a)[:2]
    check(f"LFO2={_v} in an old .config and as --set: loads, warns \"dropped\" each time, keeps its other keys, ignored",
          "LFO2" not in _cfg and "LFO2" not in _c3 and _cfg["MISSING_WARN"] == 0 and _w.getvalue().count("LFO2 was dropped") == 2 and
          "FELUCCA_LFO2" not in C.header(_cfg))
check("LFO2: no item, retired (its bit 91 not reused), counted ON in the configuration hash, no shipped profile or test configuration names it",
      "LFO2" not in R.ITEMS and "LFO2" in R.RETIRED and R.HASH_AS.get("LFO2") == 1 and all(it.bit != 91 for it in R.ITEMS.values()) and
      not [p.name for d in (C.PROFILES, ROOT / "tests" / "profiles") for p in d.glob("*.config")
           if any(ln.startswith("LFO2=") for ln in p.read_text().splitlines())])
# SLOOP's project import / export (2026-10-29): SL24_IMPORT, SL24_EXPORT, SL24_EDIMPORT are gone; SL24_SAFE (SLOOP's data kept
# safe) stays, on by default; an old .config naming the three loads, each said dropped, nothing offered
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nSL24_IMPORT=0\nSL24_EXPORT=1\nSL24_EDIMPORT=1\nMISSING_WARN=0\n")
check("SL24_IMPORT, SL24_EXPORT, SL24_EDIMPORT: no item, retired (bits 163, 164, 181 not reused); SL24_SAFE (bit 161) an item, on by default, an old SL24_SAFE=0 kept, an old .config naming them loads, "
      "each said dropped, its other keys kept",
      all(k not in R.ITEMS and k in R.RETIRED for k in ("SL24_IMPORT", "SL24_EXPORT", "SL24_EDIMPORT")) and
      R.ITEMS["SL24_SAFE"].default == 1 and R.ITEMS["SL24_SAFE"].bit == 161 and "SL24_SAFE" not in R.RETIRED and
      all(it.bit not in (163, 164, 181) for it in R.ITEMS.values()) and _cfg["MISSING_WARN"] == 0 and
      all(f"{k} was dropped" in _w.getvalue() for k in ("SL24_IMPORT", "SL24_EXPORT", "SL24_EDIMPORT")) and
      "FELUCCA_SL24_IMPORT" not in C.header(_cfg))
# BLE_CENTRAL became a choice of levels (2026-10-24; firmware/src/ble/ble_cfg.h): a .config from before says
# BLE_CENTRAL=1 and loads as level 1 (FULL), unchanged, its header FELUCCA_BLE_CENTRAL 1 as before; 3 (MINIMAL) is taken; 2 (a Just Works
# level, removed again) and 4 are refused
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nBLE=1\nBLE_CENTRAL=1\nMISSING_WARN=0\n")
_err, _, _ = C.validate(_cfg)
_h, _ = C.flags(_cfg)
check("BLE_CENTRAL=1 in an old .config: loads as level 1 (FULL) unchanged, valid, no warning, the header's FELUCCA_BLE_CENTRAL 1",
      _cfg["BLE_CENTRAL"] == 1 and not _err and "warning" not in _w.getvalue() and _h.get("FELUCCA_BLE_CENTRAL") == 1 and
      "#define FELUCCA_BLE_CENTRAL 1" in C.header(_cfg))
check("BLE_CENTRAL: a choice of off / FULL / MINIMAL (0, 1, 3), off by default",
      items["BLE_CENTRAL"].is_choice and [c[0] for c in items["BLE_CENTRAL"].choices] == [0, 1, 3] and
      items["BLE_CENTRAL"].default == 0)
for _v, _ok in ((2, False), (3, True), (4, False)):
    _c = dict(_cfg, BLE_CENTRAL=_v)
    _err, _, _ = C.validate(_c)
    check(f"BLE_CENTRAL={_v}: " + ("valid, the header carries the level" if _ok else "refused"),
          (not _err and f"#define FELUCCA_BLE_CENTRAL {_v}" in C.header(_c)) if _ok else bool(_err))
# the web editor's live features are three builder items (2026-10-19, the user: "group them in logical features"): options of
# EDITOR, on by default, each the switch of its commands' own flags (firmware/src/core/registry.h)
ED = {"ED_MIX": ("FELUCCA_ED_FXS", "FELUCCA_ED_FXP", "FELUCCA_ED_DLAN", "FELUCCA_ED_SOLO"),
      "ED_SEQ": ("FELUCCA_ED_STEPLANE", "FELUCCA_ED_TOOLS", "FELUCCA_ED_SONG"),
      "ED_SCREENS": ("FELUCCA_ED_SCRS", "FELUCCA_ED_SCRN", "FELUCCA_ED_SCOPE")}
ED_OLD = {"ED_FXP": "ED_MIX", "ED_DLAN": "ED_MIX", "ED_SOLO": "ED_MIX", "ED_STEPLANE": "ED_SEQ", "ED_TOOLS": "ED_SEQ", "ED_SONG": "ED_SEQ",
          "ED_SCRS": "ED_SCREENS", "ED_SCRN": "ED_SCREENS", "ED_SCOPE": "ED_SCREENS"}
_rh = (ROOT / "firmware" / "src" / "core" / "registry.h").read_text()
check("web editor items: ED_MIX, ED_SEQ, ED_SCREENS exist, options of EDITOR, on by default, bits 353..355; each command's flag follows its feature",
      all(k in R.ITEMS and R.ITEMS[k].parent == "EDITOR" and R.ITEMS[k].default == 1 and R.ITEMS[k].flag == "FELUCCA_" + k for k in ED) and
      sorted(R.ITEMS[k].bit for k in ED) == [353, 354, 355] and
      all(re.search(rf"#define {f} +FELUCCA_{k}\b", _rh) for k, fs in ED.items() for f in fs))
check("... the thirteen ED_* of before are gone: nine merged into their feature, UNDO / FAVS / NOTE part of live editing, REC dropped",
      all(o not in R.ITEMS and R.MERGED.get(o, ("",))[0] == g for o, g in ED_OLD.items()) and
      all(o not in R.ITEMS and o in R.RETIRED for o in ("ED_UNDO", "ED_FAVS", "ED_NOTE", "ED_REC")) and
      not re.search(r"FELUCCA_ED_REC\b", _rh) and all(it.bit not in (144, 145, 146, 147, 148, 149, 156, 157, 158, 159, 165, 166, 168)
                                                       for it in R.ITEMS.values()))
check("... an older .config with a feature's parts all off loads it off, with one of them on it on (a survivor it does not name)",
      C.parse("ED_FXP=0\nED_DLAN=0\nED_SOLO=0\n")[0]["ED_MIX"] == 0 and C.parse("ED_FXP=0\nED_DLAN=1\n")[0]["ED_MIX"] == 1 and
      C.parse("ED_TOOLS=0\n")[0]["ED_SEQ"] == 0 and C.parse("ED_MIX=1\nED_FXP=0\n")[0]["ED_MIX"] == 1)
_ED_OFF = ["ED_MIX=0", "ED_SCREENS=0", "ED_SEQ=0"]
check("... the shipped profiles that name them (live editing off in each) set all three off",
      all(sorted(ln.strip() for ln in p.read_text().splitlines() if ln.split("=")[0] in ED) == _ED_OFF
          for p in C.PROFILES.glob("*.config") if any(ln.split("=")[0] in ED for ln in p.read_text().splitlines())))
_fl, _ = C.flags(dict(C.defaults()))
check("... the default build has every feature on", all(_fl["FELUCCA_" + k] == 1 for k in ED))
_fl, _ = C.flags(dict(C.defaults(), ED_MIX=0))
check("... ED_MIX=0 switches only the mixer off", _fl["FELUCCA_ED_MIX"] == 0 and _fl["FELUCCA_ED_SEQ"] == 1 and _fl["FELUCCA_ED_SCREENS"] == 1)
check("... the header carries it (-D from the generated config: FELUCCA_ED_MIX 0)", "#define FELUCCA_ED_MIX 0" in C.header(dict(C.defaults(), ED_MIX=0), "t"))
_fl, _ = C.flags(dict(C.defaults(), EDITOR=0, ED_MIX=0))
check("... with the editor (EDITOR) left out its options are ignored (their default, no effect)", _fl["FELUCCA_EDITOR"] == 0 and _fl["FELUCCA_ED_MIX"] == 1)
# 2026-10-17: a build without the editor was a build without updates (one item, OTA): the FM-1 then answered no installer
check("updates and the USB rescue are no builder item: no flag of any item is FELUCCA_OTA, so every build answers the installer",
      all(it.flag != "FELUCCA_OTA" for it in R.ITEMS.values()) and "FELUCCA_OTA" not in C.flags(C.defaults())[0])
_cfg, _ = C.parse("OTA=0\n")
check("... an old .config's OTA=0 loads as EDITOR=0 (the editor out, updates kept), OTA=1 as EDITOR=1",
      _cfg["EDITOR"] == 0 and C.parse("OTA=1\n")[0]["EDITOR"] == 1 and "OTA" not in _cfg)
check("... every web editor feature costs nothing without live editing (NEEDS); the device screens counted in every "
      "build (the Optimist UI, the only one)", all(C.NEEDS[k].get("ED_LIVE") == 1 for k in ED) and
      "UI" not in C.NEEDS.get("ED_SCREENS", {}) and C.counted(dict(C.defaults(), ED_LIVE=1), "ED_SCREENS"))
check("... but the screen mirrored (ED_MIRROR) costs its flash with live editing off too: ed_lite.c answers it, lcd.c calls it "
      "(the user's mots.config, ED_LIVE=0 ED_MIRROR=1, showed no change, 2026-10-22)",
      "ED_LIVE" not in C.NEEDS.get("ED_MIRROR", {}) and C.counted(dict(C.defaults(), ED_LIVE=0, ED_MIRROR=1), "ED_MIRROR")
      and C.counted(dict(C.defaults(), ED_LIVE=1, ED_MIRROR=1), "ED_MIRROR"))
# UI (bit 251) is retired with SLOOP's UI (2026-10-19), and the items only it used: an old .config naming them loads,
# warns "dropped", builds the Optimist UI; a cost measured "with UI=1" (costs.json) applies to every build
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nUI=0\nICONS=1\nOVERVIEW=1\nDRUM_STEP=1\nSEL_PAGES=1\nMISSING_WARN=0\n")
check("a .config naming UI=0 and SLOOP's UI's items loads, each said dropped, its other keys kept; the Optimist UI built",
      not any(k in _cfg or k in R.ITEMS for k in ("UI", "ICONS", "OVERVIEW", "DRUM_STEP", "SEL_PAGES")) and
      _w.getvalue().count("dropped") == 5 and _cfg["MISSING_WARN"] == 0 and "FELUCCA_UI" not in C.flags(_cfg)[0] and
      K.holds(_cfg, "UI", 1) and not K.holds(_cfg, "UI", 0))
# SIZE (bit 70) and IDLE (bit 55) are retired, always built: an old .config naming them loads, warns, builds as ever
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nSIZE=0\nIDLE=0\nMISSING_WARN=0\n")
_h, _e = C.flags(_cfg)
check("a .config with the retired SIZE=0 / IDLE=0 loads, warns \"dropped\" for each and keeps its other keys",
      "SIZE" not in R.ITEMS and "IDLE" not in R.ITEMS and "SIZE" not in _cfg and "IDLE" not in _cfg and
      _w.getvalue().count("dropped") == 2 and _cfg["MISSING_WARN"] == 0)
check("... and the build has no switch for them (FELUCCA_IDLE, FELUCCA_SIZE)", "FELUCCA_IDLE" not in _h and "FELUCCA_SIZE" not in _e)
# an item this builder does not know (a newer builder's, or one removed without a RETIRED line): ignored with a
# warning, never an error, so a saved .config always loads
_w = io.StringIO()
with contextlib.redirect_stderr(_w):
    _cfg, _ = C.parse("# name: old\nNO_SUCH_ITEM=1\nMISSING_WARN=0\n")
check("a .config naming an item this builder does not know loads, warns once and keeps its other keys",
      "NO_SUCH_ITEM" not in _cfg and _cfg["MISSING_WARN"] == 0 and _w.getvalue().count("warning:") == 1 and
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
check("FM6's modes are gone: MARK I / MODERN / OPL / MKI_FLASH, STORE, VOICES, ALL, ALGO are retired (their bits 10, 11, 12, 89, 16, 15, 80, 81 never reused), no item",
      all(k not in items and k in R.RETIRED for k in ("FM6_MARK1", "FM6_MODERN", "FM6_OPL", "FM6_MKI_FLASH", "FM6_STORE",
                                                    "FM6_VOICES", "FM6_ALL", "FM6_ALGO")) and
      not {10, 11, 12, 15, 16, 80, 81, 89} & {it.bit for it in items.values()})
for _k in ("FM6_MARK1", "FM6_MODERN", "FM6_OPL", "FM6_MKI_FLASH", "FM6_STORE", "FM6_VOICES", "FM6_ALL", "FM6_ALGO"):
    _warns = []
    _cfg, _nm = C.parse("%s=0\n" % _k, warn=_warns.append)
    check("an old .config's %s=0 loads, warns \"dropped\" and is ignored" % _k,
          any("dropped" in x for x in _warns) and _k not in _cfg)
check("the FM6 voice packs: five items (bits 93..97), off by default, each a flag of its own",
      [items[k].bit for k in ("FM6_PK_KEYS", "FM6_PK_BASS", "FM6_PK_BELLS", "FM6_PK_PADS", "FM6_PK_80S")] == [93, 94, 95, 96, 97] and
      not any(C.defaults()[k] for k in ("FM6_PK_KEYS", "FM6_PK_BASS", "FM6_PK_BELLS", "FM6_PK_PADS", "FM6_PK_80S")) and
      all(items[k].flag == "FELUCCA_" + k for k in ("FM6_PK_KEYS", "FM6_PK_BASS", "FM6_PK_BELLS", "FM6_PK_PADS", "FM6_PK_80S")))
NO_DS = dict(DRUM_SYNTH=0, DS_KAWA=0, DS_PLAITS=0)   # (the three drum synths, each a drum source on its own)
bad = dict(C.defaults(), DRUM_SAMPLED=0, **NO_DS)
# the three drum synths independent (the user, 2026-10-22, "3 drum synths, independent of each other"): the 32 kits,
# KAWA, PLAITS; the code they share (drum_synth.c, the kit tables) is built with any of them
check("the drum synths: DRUM_SYNTH, DS_KAWA, DS_PLAITS each an item of its own (an option of DRUMS only), on by default",
      all(items[k].parent == "DRUMS" and C.defaults()[k] == 1 for k in NO_DS))
for _k in NO_DS:
    _one = dict(C.defaults(), DRUM_SAMPLED=0, **dict(NO_DS, **{_k: 1}))
    _f, _ = C.flags(_one)
    check(f"{_k} alone (no other drum synth, no sampled kit): a drum source, valid, built with only its own switch",
          not C.validate(_one)[0] and C.built(_one, _k) and
          all(_f[items[k].flag] == (k == _k) for k in NO_DS))
check("KAWA on does not need or switch on the 32 kits (and the other way)",
      not C.built(dict(C.defaults(), DRUM_SYNTH=0), "DRUM_SYNTH") and C.built(dict(C.defaults(), DRUM_SYNTH=0), "DS_KAWA")
      and C.built(dict(C.defaults(), DS_KAWA=0), "DRUM_SYNTH"))
check("no drum source: an error", any("drum source" in e for e in C.validate(bad)[0]))
w = dict(C.defaults(), FM6_KEYS=0, FM6_SYSEX=0)
check("FM6 without editor and SysEx: a warning (no voice editing)", any("no voice editing" in x for x in C.validate(w)[1]))
w = dict(C.defaults(), FM6_SYSEX=0, FM6_PK_KEYS=0, FM6_PK_80S=0)
check("FM6 without a pack and without SysEx: a warning (INIT VOICE only)", any("INIT VOICE is its only voice" in x for x in C.validate(w)[1]))
check("the automation store's switches stay off by default",
      not any(C.defaults().get(k) for k in ("MICRO", "FILLS", "PLOCK", "CHANCE", "MOTION", "SL24_XSTEP")))
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
         ("no drum source", dict(DRUM_SAMPLED=0, **NO_DS), {"DRUM_SYNTH", "DS_KAWA", "DS_PLAITS", "DRUM_SAMPLED"}),
         ("a value out of range", dict(SECTIONS=5), {"SECTIONS"})]
ok = True
for what, ch, want in cases:
    errs = C.validate(dict(C.defaults(), **ch))[0]
    ok &= len(errs) == 1 and want <= set(errs[0].keys) and set(errs[0].keys) <= set(items)
    ok &= all(k in C.conflicts(dict(C.defaults(), **ch)) for k in want)
check("every error names its items (" + ", ".join(c[0] for c in cases) + ")", ok)
kit = next(k for k in items if k.startswith("KIT_"))
cf = C.conflicts(dict(C.defaults(), DRUM_SAMPLED=0, **NO_DS))
check("... an option named by an error marks its parent too (it may be folded away)",
      kit in cf and items[kit].parent in cf and not C.conflicts(C.defaults()))
check("... an error is still a plain message (str: printed, joined, searched)",
      all(isinstance(e, str) for e in C.validate(dict(C.defaults(), DRUM_SAMPLED=0, **NO_DS))[0]))
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
text = C.dump(dict(C.defaults(), MISSING_WARN=0, DLY_LEN=32768), "x")
back, name = C.parse(text)
check("a .config round trip (only what differs is written)", back["MISSING_WARN"] == 0 and back["DLY_LEN"] == 32768 and
      name == "x" and "ENG_ANALOG" not in text)
costs = C.load_costs()
if costs:
    fitted, ch = C.fit(C.load_profile(C.REFERENCE_PROFILE)[0], costs)   # (not the raw defaults: nearly every item on, 97 KB over
                                                                  # 2026-10-19, never meant to fit)
    check("fit: the reference profile made to fit by the estimate", fitted is not None and
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
    ("SLICE, PHYS, CZ, FM6 without a voice pack", dict(C.defaults(), ENG_SLICE=1, ENG_PHYS=1, ENG_CZ=1, FM6_PK_KEYS=0, FM6_PK_80S=0)),
    ("one sampled kit, no drum synth", dict(C.defaults(), **NO_DS, KIT_ACOUSTIC=0, KIT_DEEP=0, KIT_TIGHT=0,
                                           KIT_BRIGHT=0)),
    ("KAWA alone (no 32 kits, no sampled kit)", dict(C.defaults(), DRUM_SAMPLED=0, **dict(NO_DS, DS_KAWA=1))),
    ("PLAITS alone", dict(C.defaults(), DRUM_SAMPLED=0, **dict(NO_DS, DS_PLAITS=1))),
    ("KAWA and PLAITS, sampled kits, no 32 kits", dict(C.defaults(), DRUM_SYNTH=0)),
    ("PLAITS with the X0X kits", dict(C.defaults(), DRUM_SAMPLED=0, DRUM_X0X909=1, DRUM_X0X808=1, **dict(NO_DS, DS_PLAITS=1))),
    ("the X0X kits", dict(C.defaults(), DRUM_X0X909=1, DRUM_X0X808=1)),
    ("ACID with the X0X kits", dict(C.defaults(), ENG_ACID=1, DRUM_X0X909=1, DRUM_X0X808=1))]
rng = random.Random(int(os.environ.get("PRESET_COVER_SEED", "1")))
cover_cases += [(f"random {i}", V.random_sources(rng)) for i in range(int(os.environ.get("PRESET_COVER_N", "12")))]
if shutil.which("cc"):
    from concurrent.futures import ThreadPoolExecutor  # noqa: E402

    def cover_one(n, what, cfg):
        """one case, its own files (cover_<n>): -> (preset cover ok, its output, kits heard ok or None, its output)"""
        ok, out = V.preset_cover(cfg, f"cover_{n}")
        if not ok or what.startswith("random") or not (ROOT / "build" / "gen").exists():
            return ok, out, None, ""
        # (the named cases: every kit on that list also makes sound, each lane hit alone: kits_sound_test.c)
        return (ok, out, *V.kits_sound(cfg, ROOT / "build" / "host" / f"gen_cover_{n}", f"kits_{n}"))

    if not (ROOT / "build" / "gen-host" / "felucca_tables.h").exists():   # (preset_cover's headers: once, not by every case)
        import subprocess  # noqa: E402
        subprocess.run([sys.executable, "tools/build.py", "--host-headers"], cwd=ROOT, capture_output=True)
    built = [(n, what, cfg) for n, (what, cfg) in enumerate(cover_cases) if not C.validate(cfg)[0]]
    refused = len(cover_cases) - len(built)              # (validate() refuses them: never built)
    # each case compiles and runs two programs (~2.5 s): all at once, JOBS of them (run_tests.sh's; else every CPU)
    with ThreadPoolExecutor(int(os.environ.get("JOBS") or os.cpu_count() or 4)) as pool:
        results = list(pool.map(lambda c: cover_one(*c), built))
    for (n, what, cfg), (ok, out, kok, kout) in zip(built, results):
        if not ok:
            print(f"preset cover: {what}: " + C.dump(cfg, what).replace("\n", " ") + "\n" + out)
        check(f"a preset per engine, a kit per drum source: {what}", ok)
        if kok is not None:
            if not kok:
                print(kout)
            check(f"... and every kit of it is heard: {what}", kok)
    built_n = len(built)
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
_REF = "synths_USBaudio_BTaudio_BTmidi"
check(f"published profiles ({', '.join(pub)}): shipped ones, {_REF} (the default and reference) among them",
      _REF in pub and set(pub) <= set(C.profile_names()))
# the default profile is also the reference profile (the user, 2026-10-24: synths_USBaudio_BTaudio_BTmidi for both)
_syn, _syn_name = C.load_profile(C.DEFAULT_PROFILE)
_syn_errs, _, _ = C.validate(_syn)
check(f"the default and the reference profile are {_REF} (configure, optimist.py: make, the tests, the gate)",
      C.DEFAULT_PROFILE == _REF and C.REFERENCE_PROFILE == _REF and O.DEFAULT_PROFILE == _REF and
      O.REFERENCE_PROFILE == _REF and _syn_name == "synths-USB-BTA-BTM")
_ed_default = {k: v for k, v in C.defaults().items() if k.startswith("ED_")}
check("the default profile validates (its contents are the user's: config/profiles is theirs since 2026-10-24)",
      not _syn_errs and _ed_default)
_syn_bud = C.budget(_syn, use_exact=False)                        # (costs.json's check of it: a real build, the anchor; or the last real build here)
check("the default profile fits (the builder's budget from its real build: costs.json's check of it, the measurement build)",
      _syn_bud is not None and _syn_bud["distance"] == 0 and not any(over > 0 for _, _, over in C.K.fits(_syn_bud["total"], _syn).values()))
out = io.StringIO()
with redirect_stdout(out):
    rc = C.main(["--published"])
check("config --published: the list as JSON (CI's matrix)", rc == 0 and json.loads(out.getvalue()) == pub)
_bd_env = C.os.environ.pop("OPTIMIST_BUILD_DIR", None)
_bd = [C.build_dir(), C.build_dir("x/y")]
C.os.environ["OPTIMIST_BUILD_DIR"] = "worktrees/pk/build"
_bd.append(C.build_dir())
if _bd_env is None:
    del C.os.environ["OPTIMIST_BUILD_DIR"]
else:
    C.os.environ["OPTIMIST_BUILD_DIR"] = _bd_env
check("build_dir: build/, an outdir, or OPTIMIST_BUILD_DIR's (package copies the package from where build.py wrote it)",
      _bd == [C.ROOT / "build", C.Path("x/y"), C.ROOT / "worktrees/pk/build"])
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
    (C.PROFILES / "a.config").write_text("# name: A\n# what it is\nMISSING_WARN=0\n")
    check("a profile without the flag: not published", C.published_profiles() == [])
    C.set_published("a", True)
    text = (C.PROFILES / "a.config").read_text()
    check("set_published on: the flag after the name, the rest kept",
          C.published_profiles() == ["a"] and text.startswith("# name: A\n# publish: yes\n# what it is\nMISSING_WARN=0")
          and C.load_profile("a")[0]["MISSING_WARN"] == 0)
    C.set_published("a", False)
    check("... off: the same line says no", C.published_profiles() == [] and
          "# publish: no\n" in (C.PROFILES / "a.config").read_text())
    C.save_my_profile(dict(C.defaults(), MISSING_WARN=0), "b")
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
    (C.PROFILES / "c.config").write_text("MISSING_WARN=0\n")
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
    # saving into the shipped profiles too (the user, 2026-10-24: "edit all profiles ... select when i save where it is
    # saved (mine or stock)"): a stock profile saved over keeps its leading comments (its name, publish flag, what it is)
    (C.PROFILES / "st.config").write_text("# name: Stock One\n# publish: yes\n# what it holds\nMISSING_WARN=0\n")
    p = C.save_profile(dict(C.defaults(), MISSING_WARN=0, DLY_LEN=32768), "st", stock=True)
    text = p.read_text()
    check("save_profile stock over a shipped one: config/profiles, its header kept, the new values, still published",
          p == C.PROFILES / "st.config" and text.startswith("# name: Stock One\n# publish: yes\n# what it holds\n") and
          text.count("# name:") == 1 and C.load_profile("st")[0]["DLY_LEN"] == 32768 and "st" in C.published_profiles())
    p = C.save_profile(dict(C.defaults(), MISSING_WARN=0), "newstock", stock=True)
    check("... a new name: a new shipped profile, not published", p == C.PROFILES / "newstock.config" and
          "newstock" in C.profile_names() and "newstock" not in C.published_profiles())
    refused = []
    for name, stock in (("st", False), ("c", True)):
        try:
            C.save_profile(C.defaults(), name, stock=stock)
            refused.append(False)
        except C.ConfigError:
            refused.append(True)
    check("... never mine over a shipped name, nor stock over one of mine (one would hide the other)", refused == [True, True])
    C.save_my_profile(C.defaults(), "gone")
    p_mine, p_shipped = C.delete_profile("gone"), C.delete_profile("a")
    check("delete_profile: one of mine, or a shipped one (git has it back)",
          p_mine == C.MY_PROFILES / "gone.config" and p_shipped == C.PROFILES / "a.config" and
          "gone" not in C.my_profile_names() and "a" not in C.profile_names())
    refused = []
    (C.PROFILES / f"{C.DEFAULT_PROFILE}.config").write_text("MISSING_WARN=0\n")
    (C.PROFILES / f"{C.REFERENCE_PROFILE}.config").write_text("MISSING_WARN=0\n")
    for n in (C.DEFAULT_PROFILE, C.REFERENCE_PROFILE, "nope"):
        try:
            C.delete_profile(n)
            refused.append(False)
        except C.ConfigError:
            refused.append(True)
    check(f"... never {C.DEFAULT_PROFILE} (make's and the CLI's default, also the reference profile), nor a name with no profile",
          all(refused) and {C.DEFAULT_PROFILE, C.REFERENCE_PROFILE} <= set(C.profile_names()))
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
check("reverb: REV_POOL relabelled; the half-rate reverb (REV_HALF, bit 123) and its steep filter (REV_HB_LONG, bit 430) are dropped, an old .config loads",
      items["REV_POOL"].label == "reverb buffers in the pool (saves ~17 KB RAM)" and
      all(k not in items and k in R.RETIRED for k in ("REV_HALF", "REV_HB_LONG")) and
      C.parse("REV_HALF=1\nREV_HB_LONG=1\n", warn=lambda m: None)[0]["REV_POOL"] == C.defaults()["REV_POOL"])
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
# a value whose lines no pool holds (INS_XDLY 3, 4: the link says "region SRAM overflowed by N" even in the measurement link) is
# its pool exactly and the rest of the value before it (the lines are the pool: the same code), not a stop of the whole run
_like = {"flash": 100, "ram": 20, "pool": 300, "ramtext": 5}
_run_build, MC.run_build = MC.run_build, lambda cfg, name, log: (False, None, "ld: region `SRAM' overflowed by 45588 bytes\n")
try:
    _got = MC.measure({}, "m-fake-3", Path("/nonexistent.log"), like=_like)
    check("measure_costs: a value no pool holds is its pool (the measurement link's SRAM + the overflow, less RAM and RAM code), the rest as the value before",
          _got["pool"] == MC.MEASURE_SRAM + 45588 - _like["ram"] - _like["ramtext"] and all(_got[r] == _like[r] for r in ("flash", "ram", "ramtext")))
    _stop = False
    try:
        MC.measure({}, "m-fake-1", Path("/nonexistent.log"))               # (nothing before it to copy: still a failure)
    except SystemExit:
        _stop = True
    check("measure_costs: ... but the first value of an item that does not build still stops the run", _stop)
finally:
    MC.run_build = _run_build
# the effects memory (fx/insfx/fxi_mem.h, fxi.c, 2026-10-23): a computed term (configure.py dly_arena: the own room, the
# instance tables, the units' bitmap), exact for every type, line length and size; measure_costs.py holds it against the
# real fxa_ins, fxa_bm and ifx_choi .. ifx_tdly of every build it makes
def _da(**kv):
    return C.dly_arena(dict(C.defaults(), **kv))
def _fm(**kv):
    return C.fx_memory(dict(C.defaults(), **kv))
_four = dict(INS_DELAY=1, INS_JCHORUS=1, INS_FLANGER=1, DLY_LEN=32768)   # (with the chorus, the delay and the reverb: built)
_none = dict(FX_CHORUS=0, FX_DELAY=0, FX_REVERB=0)
check("the effects memory: auto is the delay's line and one line of each insert built (ECHO 8, JUNO 4, FLNG 1, XDLY 2 "
      "lines: 64 or 32 KB; CHO DLY REV none of their own), always (DLY_ARENA is retired); nothing without an insert built",
      _da(INS_DELAY=1)["units"] == 8 and _da(**_four)["units"] == 13 and _da(INS_XDLY=1)["units"] == 64
      and _da(INS_XDLY=1, XDLY_LEN=8192)["units"] == 32 and _da(INS_DELAY=1, DLY_LEN=32768)["units"] == 8
      and _da(**_none)["pool"] == 0 and _da()["units"] == 0)
check("... its pool: the own room, the instance tables (an owner each at most, no more than the memory holds) and the "
      "bitmap: one XDLY beside the delay's 64 KB line: 64 KB of its own, two XDLY states (its own, the delay's line "
      "lent), the CHO REV DLY copies the room and the delay's line hold, 20 B of bitmap",
      _da(INS_XDLY=1, DLY_LEN=32768)["pool"] == 64 * 1024 + 2 * C.XD_STATE + 9 * C.FXM_STATE["FX_CHORUS"][1]
      + 6 * C.FXM_STATE["FX_REVERB"][1] + 2 * C.FXM_STATE["FX_DELAY"][1] + 20
      and _da(**_four)["tables"]["ifx_choi"] == 9 * C.FXM_STATE["FX_CHORUS"][1])
_m = _fm(**_four)
check("... up to N at once in the memory chosen (each type alone, the sends in their master slots: the own room), plus "
      "what the delay's and the chorus's lines lend while out of the slots, capped at the 9 owners; DLY and REV: on a "
      "send's line only",
      _m["types"]["FX_CHORUS"] == {"kb": 4, "max": 9, "own": 3, "dly": 16, "cho": 1, "arena": 0}
      and _m["types"]["INS_DELAY"]["own"] == 1 and _m["types"]["INS_DELAY"]["dly"] == 8
      and _m["types"]["INS_FLANGER"]["own"] == 9 and _m["types"]["FX_REVERB"]["own"] == 0
      and _m["types"]["FX_REVERB"]["max"] == 3 and _m["types"]["FX_DELAY"]["max"] == 1
      and "CHO up to 3 (+6 while the delay" in C.fx_memory_text(_m)
      and "ECHO up to 1 (+8 while the delay is in no master slot)" in C.fx_memory_text(_m))
_ma = _fm(ARENA=1, **_four)
check("... with ARENA: each CHO DLY REV copy a block of the arena (8 at most), not in the own room (13 KB: ECHO JUNO FLNG)",
      _ma["own"] == 13 and all(_ma["types"][k]["arena"] == 8 for k in ("FX_CHORUS", "FX_DELAY", "FX_REVERB"))
      and _ma["types"]["FX_REVERB"]["max"] == 9 and "more in the arena's free RAM" in C.fx_memory_text(_ma))
_rn = []
_e, _w, _n = C.validate(dict(C.defaults(), **_four))
check("... the builder says so: a notice on the effects memory (on the inserts built), no error; DLY and REV: on the delay's "
      "line only without ARENA, said",
      not _e and any("INS_XDLY" not in n.keys and "INS_DELAY" in n.keys and "up to" in n for n in _n)
      and any("REV plays only while the delay is in no master slot" in n for n in _n)
      and not any("REV plays only" in n for n in C.validate(dict(C.defaults(), ARENA=1, **_four))[2]))
check("the insert copies' items retired (the CHO DLY REV inserts built with their effect): INS_CHORUS, INS_REVERB, "
      "INS_TDELAY gone, in RETIRED; an old .config naming them loads, said, no item",
      all(k not in R.ITEMS and k in R.RETIRED for k in ("INS_CHORUS", "INS_REVERB", "INS_TDELAY"))
      and (lambda n, c: not [k for k in ("INS_CHORUS", "INS_REVERB", "INS_TDELAY") if k in c] and
           sum("dropped" in m for m in n) == 3)(_rn, C.parse("INS_CHORUS=1\nINS_REVERB=1\nINS_TDELAY=1\n", warn=_rn.append)[0]))
# (the user, 2026-10-29: DLY_ARENA removed, the effects memory always auto) an old .config's DLY_ARENA loads, the number is
# ignored with a notice; the build gets no FELUCCA_DLY_ARENA (0: auto, fxi_mem.h) whatever it said; the memory is the
# delay's line and one line of each insert built (the user's screenshot: an old DLY_ARENA=64 with DLY_LEN 65536: just auto)
_notes = []
_old, _ = C.parse("INS_XDLY=1\nDLY_LEN=65536\nDLY_ARENA=64\n", warn=_notes.append)
_e, _w, _n = C.validate(_old)
check("an old .config's DLY_ARENA=64 (with DLY_LEN 65536) loads: ignored with a notice, no item, no flag, no error; the "
      "memory is auto (the delay's 128 KB line and one XDLY line, 64 KB)",
      "DLY_ARENA" not in R.ITEMS and "DLY_ARENA" not in _old and any("DLY_ARENA" in m and "ignored" in m for m in _notes)
      and not _e and "FELUCCA_DLY_ARENA" not in C.flags(_old)[0] and C.fx_memory(_old)["own"] == 64
      and C.fx_memory(_old)["send"] == 128 and "DLY_ARENA" in R.RETIRED)
# the FM-1, 2026-10-28 (the fm1-next release, ARENA 32 in the SD card mode): no reverb, no chorus at all. The arena
# holds the sends' lines while a project uses them (fx/fx_arena.c), and a new project has all three in its master
# slots: a 32 KB delay line and a 32 KB reverb line never fit in 32 KB together, the refused ones bypassed (MEM FULL)
_sd = dict(LAYOUT=1, ENG_SAMPLE=0, DRUM_USR=0, SNAPSHOTS=0, PATTERNS=1, FX_PUNCH=0, DLY_LEN=16384, REV_FDN8=1, REV_POOL=1,
           REV_ROOM=0)   # (the user's profile's sends; the fake SD mode's needs, no PUNCH ring: its 64 KB block alone)
# (the user, 2026-10-29: "arena first, undo gets the rest") ARENA is an on/off switch: the arena is the SRAM the link
# leaves free (engines.c ARN_FREE); its flag 1, an older configuration's size reads as on; no memory error, a notice with
# what a new project needs of it together (the send lines, the inserts' own room, a part's block, the largest buffer)
_a = C.arena_need(dict(C.defaults(), ARENA=1, **_sd))
_e, _w, _n = C.validate(dict(C.defaults(), ARENA=1, **_sd))
_old, _ = C.parse("ARENA=96\n", warn=lambda m: None)
check("ARENA on: the free RAM (its flag 1), no error; a notice with what a new project needs of it together (the send "
      "lines 68 KB and more); an older .config's ARENA=96 loads as on",
      not _e and C.flags(dict(C.defaults(), ARENA=1, **_sd))[0]["FELUCCA_ARENA_KB"] == 1 and _a["kb"] >= 68
      and any("ARENA" in n.keys and "needs about %d KB" % _a["kb"] in n for n in _n) and _old["ARENA"] == 1
      and not R.ITEMS["ARENA"].is_choice)
check("... outside the SD card mode too (the extras in every ARENA build): the same notice, the send lines in its need",
      not C.validate(dict(C.defaults(), ARENA=1, **dict(_sd, LAYOUT=0)))[0]
      and "the delay's line" in C.arena_need(dict(C.defaults(), ARENA=1, **dict(_sd, LAYOUT=0)))["what"])
check("... its pool: the fixed memories it replaces given back (none of its own: the free SRAM)",
      K.arena_mem(dict(C.defaults(), ARENA=1, **_sd))["pool"] < 0)
check("... an arena needing more than the SRAM can give it: the one error left, naming the biggest consumers",
      any("ARENA" in e.keys and "would need" in e and "the delay's line" in e
          for e in C.validate(dict(C.defaults(), ARENA=1, **dict(_sd, DLY_LEN=65536, INS_XDLY=1, XDLY_LEN=16384,
                                                                      INS_DELAY=1, INS_JCHORUS=1,
                                                                      INS_FLANGER=1, REV_FDN8=1, REV_ROOM=1, REV_PLATE=1, SPRING=1,
                                                                      REV_AIRWIN=1, ENG_WAVE=1)))[0]))
_cfg, _ = C.parse("INS_DELAY=4\nINS_JCHORUS=3\nINS_FLANGER=1\nINS_XDLY=2\n", warn=lambda m: None)
check("an old .config's instance counts (INS_DELAY=4 ...) load as built: no setting lost, no error",
      all(_cfg[k] == 1 for k in C.FXM_TYPES if k in R.WAS_COUNT) and not [e for e in C.validate(_cfg)[0] if set(e.keys) & set(C.FXM_TYPES)]
      and all(not R.ITEMS[k].is_choice for k in C.FXM_TYPES))
# where each engine's hot code runs (registry.py FAST_NAMES, 2026-10-29): its item off / on / on, code in RAM (0 / 2 / 3);
# an old .config's 1 reads as its place by default; the header's FAST_RAM_<name>; the hash of a default place unchanged
_d = C.defaults()
check("engines: each FAST_NAMES item a choice off / on / on code in RAM, its default its place (RAM: ANALOG 2 PHASE "
      "DIGITAL TRIO FORMANT DRAWBAR LOFI SAMPLE; the rest on, from the flash) or off",
      all([c[0] for c in R.ITEMS[k].choices] == [R.ENG_OFF, R.ENG_ON, R.ENG_RAM] and
          R.ITEMS[k].default in (R.ENG_OFF, R.eng_place(k)) for k in R.FAST_NAMES)
      and {k for k in R.FAST_NAMES if R.eng_place(k) == R.ENG_RAM} == set(R.FAST_RAM)
      and _d["ENG_ANALOG"] == R.ENG_RAM and _d["ENG_FM6"] == R.ENG_ON)
for _k in R.FAST_NAMES:
    _vals = {}
    for _v in (R.ENG_OFF, R.ENG_ON, R.ENG_RAM):
        _f = C.flags(dict(_d, **{_k: _v}))[0]
        _vals[_v] = (_f[R.ITEMS[_k].flag], tuple(_f[f"FAST_RAM_{n}"] for n in R.FAST_NAMES[_k]))
    check(f"engines: {_k} off / on / code in RAM -> its switch 0 / 1 / 1, FAST_RAM 0 / 0 / 1",
          _vals[R.ENG_OFF][0] == 0 and _vals[R.ENG_ON] == (1, (0,) * len(R.FAST_NAMES[_k]))
          and _vals[R.ENG_RAM] == (1, (1,) * len(R.FAST_NAMES[_k])))
_old, _ = C.parse("ENG_ANALOG=1\nENG_FM6=1\nENG_ACID=1\nENG_PHASE=0\n", warn=lambda m: None)
check("engines: an old .config's 1 reads as the engine's place (ANALOG in RAM, FM6 and ACID on), 0 stays off",
      (_old["ENG_ANALOG"], _old["ENG_FM6"], _old["ENG_ACID"], _old["ENG_PHASE"]) == (R.ENG_RAM, R.ENG_ON, R.ENG_ON, 0))
check("engines: an old .config's hash (and so its image) as before the choice: the defaults' place hashes as 1",
      C.cfg_hash(_old) == C.cfg_hash(dict(_old, ENG_ANALOG=1, ENG_FM6=1, ENG_ACID=1))
      and C.cfg_hash(dict(_old, ENG_ANALOG=R.ENG_ON)) != C.cfg_hash(_old))
_rt, _ = C.parse(C.dump(dict(_d, ENG_ANALOG=R.ENG_ON, ENG_FM6=R.ENG_RAM)), warn=lambda m: None)
check("engines: a dump round-trips the places (ANALOG on from the flash, FM6 in RAM: 2 and 3 written)",
      (_rt["ENG_ANALOG"], _rt["ENG_FM6"]) == (R.ENG_ON, R.ENG_RAM))
_env = C.apply_env(_d, {"FELUCCA_ENG_ANALOG": "1", "FELUCCA_ENG_FM6": "3"})
check("engines: the old switch FELUCCA_ENG_X=1 in the environment is the place by default, 3 code in RAM",
      (_env["ENG_ANALOG"], _env["ENG_FM6"]) == (R.ENG_RAM, R.ENG_RAM))
_co = C.load_costs()
# (costs.json measures every place now: the estimate's fallback is tried on a copy without the two entries it stands in for)
_co_nm = dict(_co, deltas=dict(_co["deltas"], ENG_ANALOG={k: v for k, v in _co["deltas"]["ENG_ANALOG"].items() if k != str(R.ENG_ON)},
                               ENG_FM6={k: v for k, v in _co["deltas"]["ENG_FM6"].items() if k != str(R.ENG_RAM)}))
_a2, _f3 = K.item_delta(_co_nm, "ENG_ANALOG", R.ENG_ON), K.item_delta(_co_nm, "ENG_FM6", R.ENG_RAM)
_s = R.FAST_BYTES
check("engines: the estimate of a place costs.json has not measured: ANALOG on from the flash takes its render path "
      "out of the RAM code and adds what it saved to the flash (measured, else a third), FM6 in RAM the other way",
      _a2["ramtext"] == -_s["ENG_ANALOG"] and _a2["flash"] == R.fast_flash_saved("ENG_ANALOG") > 0
      and _f3["ramtext"] == _s["ENG_FM6"] and _f3["flash"] == -R.fast_flash_saved("ENG_FM6")
      and R.fast_flash_saved("ENG_TRIO") == round(_s["ENG_TRIO"] * (1 - R.FAST_FLASH_RAM))
      and K.item_delta(_co_nm, "ENG_ANALOG", R.ENG_RAM) == {r: 0 for r in C.REGIONS})
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
# BLE and the room it needs (tools/builder/room.py, BUILDER.md "BLE and the room it needs"): ticking BLE in a menu removes
# NOTHING, like any other tick (the user, 2026-10-21: the meters go over and say it does not fit; the old rule unticked an
# item and made BLE on show more free flash than off). `--ble-drop ITEM` is the explicit headless choice: it removes that
# one item (make_room: the named one, or the smallest single item that frees enough); the checks below hold it to the
# same figures as before. Nothing here is pinned: every figure comes from costs.json as it is (BLE's measured cost, with
# and without its option BLE_BOND), and the expected item is computed per profile from the budget alone, not from room.py.
dflt_real, _ = C.load_profile(C.REFERENCE_PROFILE)
check("BLE, measured cost (costs.json): the reference profile (synths_USBaudio_BTaudio_BTmidi, BLE on) with BLE fits as it is",
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


def room_scenarios(prof, opt):
    tag = f"BLE room, {prof}{' + BLE_BOND' if opt else ''}"
    base, _ = C.load_profile(prof)
    if base["BLE"]:                                     # (the rule is BLE ticked on a profile without it)
        print(f"{tag}: the profile has BLE on already, not a case")
        return
    sets = ["BLE=1"] + (["BLE_BOND=1"] if opt else [])
    withble = with_ble(base, opt)
    over, order, chosen = expected_room(withble, costs)
    cfg0, rm0 = RM.after_toggle(base, withble, "BLE", RM.NONE, costs)
    check(f"{tag}: BLE ticked in a menu: nothing is removed, no message, whether it fits or not (like any other tick)",
          cfg0 == withble and not rm0.dropped and not rm0.note)
    cfgu, rmu = RM.after_toggle(withble, base, "BLE", RM.NONE, costs)
    check(f"{tag}: BLE unticked: the configuration is the profile's again, nothing to give back", cfgu == base and not rmu.dropped)
    cfg1, rm = RM.make_room(withble, costs)             # (the explicit rule, --ble-drop's)
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
    small = [k for k, v in C.savings_of(withble, costs).items()
             if k not in order and k != "BLE" and not R.ITEMS[k].is_choice and 0 < v["flash"] and not C.validate(dict(withble, **{k: 0}))[0]]
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


PROFILES_BLE = tuple(C.profile_names())     # (the ones with BLE on already are no case: synths and synths-2 are)
for _prof in PROFILES_BLE:
    for _opt in (0, 1):
        room_scenarios(_prof, _opt)
_removing = [(p, d) for p in PROFILES_BLE for d in (0, 1)
             if expected_room(with_ble(C.load_profile(p)[0], d), costs)[2]]
check("BLE room: at least one profile has BLE overflow and a single item that frees enough (the rule is exercised): "
      + ", ".join(f"{p}{'+bond' if d else ''}" for p, d in _removing), bool(_removing))
try:
    with contextlib.redirect_stdout(io.StringIO()):
        C.resolve_cli(argparse.Namespace(config=None, profile="synths", set=[], name=None, ble_drop="FLUTE"))
    _off = False
except C.ConfigError:
    _off = True
check("--ble-drop with BLE off: refused with a ConfigError", _off)
check("tools/optimist.py: build and package take --ble-drop, and the BLE test package is made with it",
      O.parser().parse_args(["build", "--set", "BLE=1", "--ble-drop", "FLUTE"]).ble_drop == "FLUTE" and
      'ble_drop="FLUTE"' in (ROOT / "tools" / "optimist.py").read_text() and
      '"BLE_CENTRAL=1"' in (ROOT / "tools" / "optimist.py").read_text() and "SET_FLUTE=0" not in
      (ROOT / "tools" / "optimist.py").read_text())
# the estimate against real builds (docs/BUILDER.md, Budget). configure.py budget: the real build of the nearest
# configuration whose build is known (an anchor: the defaults, costs.json "checks", a real build from this source)
# plus what the measured deltas say the items that differ change. costs.json "checks" holds measurement builds of
# every profile, of the BLE test builds (optimist.py ble_test_configs) and of each one's probe
# (measure_costs.py probe: PROBE_ITEMS items changed), made in the same measure_costs.py run as the deltas.
# 1. self: each of them estimated WITHOUT its own build (its probe, PROBE_ITEMS items away, is then its nearest
#    anchor, or another check): within ESTIMATE_NEAR. What the menu meets: a few changes from a configuration
#    built before. Measured 2026-10-14: 4-5 items from an anchor, flash -2.3 to +4.8 KB, RAM within 0.3 KB, pool
#    within 0.7 KB (LTO shares and inlines code by what else is built: no sum of per-item figures does better).
#    Then without its probe either (the nearest other check or the defaults, 20-30 items away): printed only,
#    that is the sum model alone (-13 to +28 KB of flash for the two users' configurations, 2026-10-14); a configuration
#    built once is exact from then on (build/exact-sizes.json is an anchor too)
# 2. drift: costs.json against the real builds `optimist.py test` has just made from this very source
#    (prepare_tests: every check configuration, the reference profile, its two BLE builds and the defaults; configure.build
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
    configurations (optimist.py prepare_tests; the BLE test builds, optimist.py ble_test_configs, among them)"""
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
# 3. monotonic (the user, 2026-10-16: "removing a feature INCREASES the flash figure"): in a shipped profile (and the
#    defaults) switching an item off, with the dependants that must go with it (core._drop), never raises the estimated
#    flash, RAM or pool. The causes it found: a margin per changed item (ANCHOR_MARGIN) that a 40 B item did not
#    cover, and the pairs measured with the Optimist UI against a UI cost that had since grown 10 KB (each "UI=1,ED_X=0"
#    pair then carried the difference: switching an ED_* item off read 5-7 KB more)
#    Allowed by name (the user, 2026-10-19, "named allowances"), each measured: the items that move memory between
#    regions (off, REV_POOL's reverb line goes back to main RAM; SECTIONS 0 keeps motion in the
#    project slots, in the pool), DIV_ORDER (its build is 284 B smaller in flash on than off) and FX_DJF (beside
#    TRK_FILT the FILTER insert's code stays: off saves nothing, +124 B). RAM moves by 16-32 B of alignment as flash does.
#    FX_DELAY off and the shorter XDLY_LEN change the limited inserts' instance tables (core.dly_arena): measured
#    2026-10-24 on synths_USBaudio_BTaudio_BTmidi (then a fixed DLY_ARENA 64), FX_DELAY off pool +4 B, XDLY_LEN 8192
#    pool +40 B, as the estimate says. DRUMS off makes track 4 a
#    4th synth part (bit 424), whose engine state is in the pool: measured 2026-10-27 on synths_USBaudio_BTaudio_BTmidi
#    (gpufarm, d9a3d172), pool +5,120 B (the estimate +4,168: the part's +12,888 less the X0X machines' states)
_MONO_MOVES = ("REV_POOL", "SECTIONS")
_MONO_ALLOW = {"DIV_ORDER": "flash", "FX_DJF": "flash", "FX_DELAY": "pool", "XDLY_LEN": "pool", "DRUMS": "pool"}
_MONO_NOISE = {"flash": K.ALIGN_NOISE, "ram": 32, "pool": 0}
_mono_bad = []
if costs:
    for _pn in (C.REFERENCE_PROFILE, None):
        _mcfg = C.defaults() if _pn is None else C.load_profile(_pn)[0]
        _b0 = C.budget(_mcfg, costs, use_exact=False)["total"]
        for _k, _it in items.items():
            if _it.no_image or (_it.parent and not C.built(_mcfg, _it.parent)) or _k in _MONO_MOVES:
                continue
            _v = K._off_value(_mcfg, _k)                # (core's: configure does not re-export it)
            _t = K._drop(_mcfg, _k, _v) if _v is not None and _mcfg[_k] else None
            if _t is None:
                continue
            _b1 = C.budget(_t, costs, use_exact=False)["total"]
            _up = {r: _b1[r] - _b0[r] for r in ("flash", "ram", "pool")    # (a few bytes: alignment, _MONO_NOISE)
                   if _b1[r] > _b0[r] + _MONO_NOISE[r] and _MONO_ALLOW.get(_k) != r}
            if _up:
                _mono_bad.append(f"{_pn or 'defaults'}: {_k}=0 {_up}")
for _line in _mono_bad:
    print(f"  not monotonic: {_line}")
check("estimate is monotonic: switching an item off (with its dependants) never raises the flash, RAM or pool, "
      f"in {C.REFERENCE_PROFILE} and the defaults", not _mono_bad)
if costs:
    _d0 = C.defaults()
    _near = K.nearest_anchor(dict(_d0, ENG_FM6=0, ENG_PHASE=0), costs, exclude=["defaults"] + list(costs.get("checks", {})),
                             extra=[("a~", dict(_d0, ENG_FM6=0), costs["base"]), ("b", _d0, costs["base"])])
    check(f"estimate's anchor: a configuration's own build up to {K.PROBE_PENALTY} items further wins over a probe (NAME~, "
          "4 random items off, where the sum model is least right)", _near and _near[0] == "b")
# ---- the undo history's ring, shown as information (the builder's reserve entries were retired 2026-10-21)
sys.path.insert(0, str(ROOT / "tools"))
import build as BUILD  # noqa: E402  (its check() is the real build's refusal)
check("reserve: the retired items are not offered", "RESERVE_UNDO_KB" not in items and "RESERVE_FLASH_KB" not in items and
      "RESERVE_UNDO_KB" in R.RETIRED and "RESERVE_FLASH_KB" in R.RETIRED)
tot = {"flash": 500000, "ram": 80000, "pool": 300000, "ramtext": 30000}
sram = K.limits_in_use()["sram"]                         # (the SRAM of the firmware the costs are of: 474,880 classic, app.ld's 475,136 the release's)
ring = sram - 8192 - (80000 + 300000 + 30000)
check(f"undo ring: the estimate shows it, {sram:,} - 8,192 - (RAM + pool + RAM code): the packed SRAM's rest",
      C.undo_ring(tot) == ring == sram - 8192 - 410000 and C.undo_ring(dict(tot, ram=100000, pool=400000)) == 0 and
      "undo ring" in C.fmt_budget(C.defaults(), dict(costs, base=dict(costs["base"], **tot))))
check("a .config naming a retired reserve item still loads (the setting is ignored, with a warning)",
      "RESERVE_UNDO_KB" not in C.parse("RESERVE_UNDO_KB=8\nRESERVE_FLASH_KB=16\n", strict=False)[0])

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

# Wi-Fi (2026-10-28): WIFI_LINK (bit 434) needs BLE (and the console on the classic firmware), WIFI_AP (bit 435) is its option; FELUCCA_LINK follows
# FELUCCA_WIFI; both off by default, target only (the host tests keep their own default)
_wd = K.defaults()
_wl = dict(_wd, BLE=1, USB_CONSOLE=1, WIFI_LINK=1)
_wf = K.flags(_wl)[0]
check("Wi-Fi: WIFI_LINK 434 (FELUCCA_WIFI) and its option WIFI_AP 435 (FELUCCA_WIFI_AP), off by default, target only",
      items["WIFI_LINK"].bit == 434 and items["WIFI_AP"].bit == 435 and items["WIFI_AP"].parent == "WIFI_LINK" and
      items["WIFI_LINK"].flag == "FELUCCA_WIFI" and items["WIFI_AP"].flag == "FELUCCA_WIFI_AP" and
      items["WIFI_LINK"].default == 0 and items["WIFI_AP"].default == 0 and
      items["WIFI_LINK"].target_only and items["WIFI_AP"].target_only)
check("Wi-Fi: WIFI_AP is honest about the device: experimental, its description says not tested",
      items["WIFI_AP"].experimental and "not tested" in items["WIFI_AP"].desc.lower())
check("Wi-Fi: with BLE and the console it is valid, FELUCCA_WIFI and FELUCCA_LINK 1 in the header, the AP flag 0",
      not K.validate(_wl)[0] and _wf["FELUCCA_WIFI"] == 1 and _wf["FELUCCA_LINK"] == 1 and _wf["FELUCCA_WIFI_AP"] == 0 and
      "#define FELUCCA_LINK 1" in K.header(_wl))
check("Wi-Fi: WIFI_AP on sets FELUCCA_WIFI_AP; with Wi-Fi off its flag stays at the default 0",
      K.flags(dict(_wl, WIFI_AP=1))[0]["FELUCCA_WIFI_AP"] == 1 and
      K.flags(dict(_wd, WIFI_AP=1))[0]["FELUCCA_WIFI_AP"] == 0 and K.flags(_wd)[0]["FELUCCA_LINK"] == 0)
_hook = K.CLASSIC_HOOK
K.CLASSIC_HOOK = lambda: True
_classic_no_con = bool(K.validate(dict(_wl, USB_CONSOLE=0))[0])
K.CLASSIC_HOOK = lambda: False
_release_no_con = not K.validate(dict(_wl, USB_CONSOLE=0))[0] and bool(K.validate(dict(_wl, BLE=0, USB_CONSOLE=0))[0])
K.CLASSIC_HOOK = _hook
check("Wi-Fi: without BLE the build is refused; without the console too on Optimist's own firmware, not in the release "
      "(fm1-base's own Wi-Fi stack: no console)",
      bool(K.validate(dict(_wl, BLE=0))[0]) and _classic_no_con and _release_no_con and not K.validate(_wd)[0])
# the USB MIDI host (2026-10-28): USB_HOST (bit 438, FELUCCA_USB_HOST), Connectivity, off by default, experimental with
# what it was tested on; the release's only (fm1-base's host): refused on Optimist's own firmware; the trial's host
# build gets it too (not target only: the flag outside __PI32V2__)
_uh = items["USB_HOST"]
_ud = C.defaults()
check("USB host: USB_HOST 438 (FELUCCA_USB_HOST) in Connectivity, off by default, experimental, its tests said, an "
      "off warning, not target only",
      _uh.bit == 438 and _uh.flag == "FELUCCA_USB_HOST" and _uh.group == "Connectivity" and _uh.default == 0 and
      _uh.experimental and _uh.tested == "host-tested only; USB host IN ran on Melodee, OUT not validated on a device"
      and _uh.off_warning and not _uh.target_only and not _uh.parent)
check("USB host: no other item has bit 438", [k for k, it in items.items() if it.bit == 438] == ["USB_HOST"])
K.CLASSIC_HOOK = lambda: True
_uh_classic = [e for e in K.validate(dict(_ud, USB_HOST=1))[0] if "USB_HOST" in e.keys]
K.CLASSIC_HOOK = lambda: False
_uh_release = [e for e in K.validate(dict(_ud, USB_HOST=1))[0] if "USB_HOST" in e.keys]
_uh_warn = [w for w in K.validate(dict(_ud, USB_HOST=1))[1] if "USB_HOST" in w.keys]
K.CLASSIC_HOOK = _hook
check("USB host: refused on Optimist's own firmware, valid in the release (where it says EXPERIMENTAL and its tests)",
      bool(_uh_classic) and not _uh_release and any("EXPERIMENTAL" in w for w in _uh_warn))
check("USB host: on sets FELUCCA_USB_HOST 1 in the header, outside __PI32V2__; off 0",
      K.flags(dict(_ud, USB_HOST=1))[0]["FELUCCA_USB_HOST"] == 1 and K.flags(_ud)[0]["FELUCCA_USB_HOST"] == 0 and
      "#ifdef __PI32V2__\n#ifndef FELUCCA_USB_HOST" not in K.header(dict(_ud, USB_HOST=1)))
# ---- the arena's size and what fits in it (the user's ruling 2026-10-29: the builder shows how much RAM the arena gets)
# No real ARENA build is recorded in build/exact-sizes.json yet, so the estimate is held against a stand-in real build: a
# known configuration with sizes whose free SRAM is known (anchor), and a neighbour a few items away
_ar_cfg = dict(C.defaults(), ARENA=1, **_sd)
_ar_real = {"flash": 600000, "ram": 90000, "pool": 150000, "ramtext": 30000}     # (a real build's sizes: 475,136 - 8,192 - 270,000 free)
_ar_free = K.undo_ring(_ar_real)
_ar_a = K.arena_size(_ar_cfg, _ar_real, exact=True)
check("arena: the real build's free SRAM less the undo's 16 KB keep, exact, in whole units (8 B)",
      _ar_a["free"] == _ar_free == sram - 8192 - 270000 and _ar_a["keep"] == 16384 and _ar_a["bytes"] == (_ar_free - 16384) & ~7
      and _ar_a["kb"] == _ar_a["bytes"] // 1024 and _ar_a["exact"] and K.arena_size(dict(_ar_cfg, ARENA=0), _ar_real) is None)
check("arena: nothing when the sizes leave no free SRAM (never negative)", K.arena_size(_ar_cfg, dict(_ar_real, pool=900000))["bytes"] == 0)
check("arena: the cache RAM gives the SRAM it frees to the arena (2 ways +8 KB, 4 or 7 ways +15 KB: the undo keeps 1 KB)",
      [K.arena_undo_keep(dict(_ar_cfg, CACHE_RAM=w)) for w in (0, 2, 4, 7)] == [16384, 8192, 1024, 1024]
      and K.arena_size(dict(_ar_cfg, CACHE_RAM=4), _ar_real)["bytes"] - _ar_a["bytes"] == 15 * 1024)
_ar_b = K.budget(_ar_cfg, costs, extra_anchors=[("stand-in", _ar_cfg, _ar_real)])
_ar_e = K.arena_size(_ar_cfg, _ar_b["total"])
check(f"arena: the estimate of a configuration whose real build is known as an anchor is that build's (within {K.ARENA_ESTIMATE_KB} KB)",
      abs(_ar_e["kb"] - _ar_a["kb"]) <= K.ARENA_ESTIMATE_KB and not _ar_e["exact"])
_ar_n = dict(_ar_cfg, FX_PUNCH=1)
_ar_nb = K.budget(_ar_n, costs, extra_anchors=[("stand-in", _ar_cfg, _ar_real)])
check(f"... a neighbour one item away: within {K.ARENA_ESTIMATE_KB} KB of the anchor's arena",
      abs(K.arena_size(_ar_n, _ar_nb["total"])["kb"] - _ar_a["kb"]) <= K.ARENA_ESTIMATE_KB)
_ar_f = {n: (b, c) for n, b, c, _ in K.arena_fits(_ar_cfg)["rows"]}
_ar_bl = K.arena_blocks(_ar_cfg)                    # (as arena_fits takes them: the effects memory always auto, 58568ef9)
check("arena fits: the three sends together = the builder's send lines; a part's engine state x the parts; the PUNCH ring",
      _ar_f["the 3 sends' lines"] == (sum(K.arena_sends(_ar_bl).values()), 1)
      and any(n.startswith("a part's") and c == K.nparts(_ar_cfg) and b == _ar_bl["a part's engine state"] for n, (b, c) in _ar_f.items())
      and _ar_f.get("the PUNCH ring", (0, 0)) == (2 * _ar_cfg["PUNCH_N"], 1) or "the PUNCH ring" not in _ar_f)
_ar_w = dict(_ar_cfg, ENG_WAVE=1, DRUM_X0X808=1, FX_SLICER=1)
_ar_wf = {n: b for n, b, c, _ in K.arena_fits(_ar_w)["rows"]}
check("arena fits: WAVE's table, the X0X 808 state, a track's SLICER recording are rows",
      _ar_wf.get("WAVE's table") == K.WAVE_MEM and _ar_wf.get("the X0X 808's state") == K.X808_STATE
      and _ar_wf.get("a track's SLICER recording") == 2 * K.flags(_ar_w)[0]["FELUCCA_SL_LEN"])
_ar_sd = dict(_ar_cfg, LAYOUT=1, SDCARD=1, SD_RING=4, ENG_SAMPLE=1, ENG_GRAIN=1, ENG_STRETCH=1)
_ar_sf = {n: (b, c) for n, b, c, _ in K.arena_fits(_ar_sd)["rows"]}
check("arena fits, SD card mode: GRAIN 33,280 B and STRETCH 16,640 B a part, the sample heads and the rings; none outside it",
      _ar_sf["GRAIN's card cache"] == (33280, 3) and _ar_sf["STRETCH's card cache"] == (16640, 3)
      and "a card sample's head" in _ar_sf and "a card stream's ring" in _ar_sf
      and not any("card" in n for n in _ar_f))
_ar_t = K.arena_text(_ar_cfg, _ar_b["total"])
check("arena text: the size marked as an estimate, a row a line, a verdict, within 78 columns; nothing with ARENA off",
      _ar_t[0].startswith("arena ~") and "estimate" in _ar_t[0] and all(len(x) <= 78 for x in _ar_t)
      and any("all at once" in x for x in _ar_t) and any("MEM FULL" in x or "fits" in x for x in _ar_t)
      and K.arena_text(dict(_ar_cfg, ARENA=0), _ar_real) == [] and K.arena_text(_ar_cfg, _ar_real, True)[0].startswith("arena ")
      and "~" not in K.arena_text(_ar_cfg, _ar_real, True)[0])
check("arena: the budget summary (fmt_budget) names it",
      "arena ~" in C.fmt_budget(_ar_cfg, costs) and "arena" not in C.fmt_budget(dict(_ar_cfg, ARENA=0), costs))
import web_api as WA  # noqa: E402
WA.init(costs)
_ar_v = WA.view(_ar_cfg)["arena"]
_ar_pb = K.budget(dict(_ar_cfg, **WA.WEB_FIXED), costs)   # (the page builds with its fixed compiler, JieLi's: counted since 2026-10-30)
check("web builder: the view carries the arena (kb, free, keep, the panel's lines and the rows) the same as the menu's",
      _ar_v and _ar_v["kb"] == K.arena_size(_ar_cfg, _ar_pb["total"])["kb"] and _ar_v["lines"] == K.arena_text(_ar_cfg, _ar_pb["total"])
      and _ar_v["rows"] and WA.view(dict(_ar_cfg, ARENA=0))["arena"] is None)
_ar_x = subprocess.run([sys.executable, str(ROOT / "tools" / "export_builder_web.py"), "--check"], capture_output=True, text=True)
check("web builder data export: web/builder/data is what the builder has now (export_builder_web.py --check)",
      _ar_x.returncode == 0)
if _ar_x.returncode:
    print("  " + (_ar_x.stdout + _ar_x.stderr).strip()[-600:])
print("builder test " + ("FAILED" if fails else "passed"))
sys.exit(1 if fails else 0)

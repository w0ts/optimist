#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The release build's wiring (tools/fm1base.py, tools/builder/configure.py build, tools/flash.py): the submodule is
found or refused with what to run, the builder's configuration reaches fm1-base's build as OPT_CONFIG with the app slot its
flash layout needs, a build's outputs come back under Optimist's names, an over-the-slot build still reports its sizes,
and --classic keeps Optimist's own build and its own exact-sizes file. No toolchain: fm1-base's build is replaced by a fake."""
import json
import os
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "builder"))
os.environ.pop("OPTIMIST_CLASSIC", None)
import configure as C  # noqa: E402
import fm1base  # noqa: E402
import flash  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(f"{what:96s} {'ok' if ok else 'FAIL'}")
    fails += not ok


cfg, name = C.load_profile(C.REFERENCE_PROFILE)
with tempfile.TemporaryDirectory(dir=ROOT / "build" if (ROOT / "build").is_dir() else None) as d:
    d = Path(d)
    normal, sd = d / "normal.config", d / "sd.config"
    # (the reference profile may have any MEMORY step; SYS_TOP off: with it the build reads the app's ELF for TOP's table
    # (fm1base.py top_table), which the fake builds below do not make: a checkout with no earlier real build has none)
    normal.write_text(C.dump(dict(cfg, LAYOUT=0, SDCARD=0, SYS_TOP=0), name))
    sd_cfg = dict(cfg, SDCARD=1, LAYOUT=1)
    sd.write_text(C.dump(sd_cfg, "sd"))
    check("release is the default flavour, classic() false", not C.classic())
    check("a normal layout builds on fm1-base's normal slot (no FM1B_APP_END)", fm1base.app_end(normal) is None)
    check("the SD card mode (LAYOUT 1) builds with FM1B_APP_END=0xCE000", fm1base.app_end(sd) == "0xCE000")
    import flash_layout as _FL
    _steps_ok = True
    for _k in (2, 3, 4, 5, 6, 7, 8):
        _p = d / f"layout{_k}.config"
        _p.write_text(C.dump(dict(cfg, LAYOUT=_k, SDCARD=0), f"l{_k}"))
        _steps_ok &= fm1base.app_end(_p) == f"0x{_FL.STEPS[_k].release_end:X}"
    check("every other MEMORY step (LAYOUT 2..8) builds with FM1B_APP_END = its end (flash_layout.py release_end)", _steps_ok)
    if fm1base.BUILD_PY.is_file():
        try:
            env = fm1base.env_for(sd)
            check("env: OPTIMIST is this checkout, OPT_CONFIG the file, the tree copied, the slot set",
                  env["OPTIMIST"] == str(ROOT) and env["OPT_CONFIG"] == str(sd.resolve()) and env["OPT_TREE_COPY"] == "1"
                  and env["FM1B_APP_END"] == "0xCE000")
        except fm1base.FM1BaseError as e:
            print(f"(env check skipped: {e})")
    # the outputs of a (fake) fm1-base build come back under Optimist's names, an over-the-slot one keeps its sizes
    out_dir = fm1base.OUT
    saved = {p: p.read_bytes() for p in (out_dir / "optimist.fwsc", out_dir / "sizes.json") if p.is_file()}
    bdir = d / "out"
    sizes = {"flash": 1, "ram": 2, "pool": 3, "ramtext": 4, "app_slot": 581564}

    def fake_ok(cmd, env):
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "optimist.fwsc").write_bytes(b"PKG")
        (out_dir / "sizes.json").write_text(json.dumps(sizes))
        return 0, "built"

    def fake_over(cmd, env):
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "sizes.json").write_text(json.dumps(dict(sizes, flash=999999)))
        return 1, "image exceeds the app slot"
    try:
        if fm1base.BUILD_PY.is_file():
            rc, _ = fm1base.build(normal, bdir, sys.executable, fake_ok, None, "optimist-x")
            check("a build: rc 0, felucca.fwsc and optimist-x.fwsc in the build directory, sizes.json copied",
                  rc == 0 and (bdir / "felucca.fwsc").read_bytes() == b"PKG" and (bdir / "optimist-x.fwsc").is_file()
                  and json.loads((bdir / "sizes.json").read_text()) == sizes)
            check("its web editor sidecar comes with it (SOURCE.txt names the configuration)",
                  (bdir / "optimist-x-ui.zip").is_file() and flash.package_source(bdir / "optimist-x.fwsc") is not None)
            bdir2 = d / "over"
            rc, _ = fm1base.build(normal, bdir2, sys.executable, fake_over, None, "optimist-x")
            check("over the slot: rc 1, no package, the sizes still reported",
                  rc == 1 and not (bdir2 / "felucca.fwsc").exists() and json.loads((bdir2 / "sizes.json").read_text())["flash"] == 999999)
    finally:
        for p in (out_dir / "optimist.fwsc", out_dir / "sizes.json"):
            p.unlink(missing_ok=True)
        for p, data in saved.items():
            p.write_bytes(data)
check("the release's exact sizes are kept apart from the classic's", C.exact_file() == C.EXACT_RELEASE != C.EXACT)
C.CLASSIC = True
check("--classic: Optimist's own exact-sizes file and costs.json", C.exact_file() == C.EXACT and C.costs_file() == C.COSTS)
C.CLASSIC = False
check("without costs-fm1base.json the release's budget reads costs.json (and says so)",
      C.COSTS_RELEASE.is_file() or (C.costs_file() == C.COSTS and not C.costs_measured()))
check("flash accepts the release's identity FM-1_7900 and FM-1_79010203, not FM-1_781",
      all(flash.OPTIMIST_ID.fullmatch(x) for x in ("FM-1_7900", "FM-1_79010203", "FM-1_700", "FM-1_731"))
      and not flash.OPTIMIST_ID.fullmatch("FM-1_781"))
import core as K  # noqa: E402
import fm1base_ld  # noqa: E402
total = {"flash": 700000, "ram": 100000, "pool": 100000, "ramtext": 30000}
C.CLASSIC = True
check("the classic slots (its build system's: flash_layout, build.py): LAYOUT 1 its whole slot (no far probe), LAYOUT 3 "
      "720,828 B, normal 581,564 B",
      K.fits(total, {"LAYOUT": 1})["flash"][1] == 831420 and K.fits(total, {"LAYOUT": 3})["flash"][1] == 720828
      and K.fits(total, {})["flash"][1] == 581564
      and K.fits(total, {})["sram"][1] == 474880 - K.SPARE["sram"])
C.CLASSIC = False
if fm1base.BUILD_PY.is_file():
    _t = tomllib.loads((fm1base.APP / "fm1.toml").read_text())   # (fm1.toml main_stack / irq_stack: the SRAM up to them)
    _sram = 0x7C000 - _t.get("main_stack", 24320) - _t.get("irq_stack", 7936) - 512
    check("the release's slots (fm1-base's fm1pkg_make app_slot of fm1.toml's app_end and the SD card mode's): LAYOUT 1 "
          "823,228 B, normal 581,564 B, SRAM app.ld's up to fm1.toml's stacks",
          K.fits(total, {"LAYOUT": 1})["flash"][1] == 823228 and K.fits(total, {})["flash"][1] == 581564
          and K.fits(total, {})["sram"][1] == _sram - K.SPARE["sram"] and K.describe()["limits"]["sram"] == _sram)
    check("a release build's sizes.json app_slot (fm1-base writes its normal APP_SLOT for every app_end) changes no limit",
          K.fits(dict(total, app_slot=581564), {"LAYOUT": 1})["flash"][1] == 823228)
_hook = K.LIMITS_HOOK
K.setup(limits_hook=None, limits={"flash": 1000, "sram": 2000, "flash_by_layout": {"1": 3000}})
check("without a build system (the page) the costs' limits", K.fits(total, {"LAYOUT": 1})["flash"][1] == 3000
      and K.fits(total, {})["sram"][1] == 2000 - K.SPARE["sram"])
K.setup(limits_hook=_hook, limits=None)
if fm1base.BUILD_PY.is_file():
    ld = (fm1base.DIR / "device" / "app_packed.ld").read_text()
    got = fm1base_ld.measure_script(ld)
    check("a measurement build links fm1-base's app.ld with room (XIP, SRAM, NOINIT beyond it, no stack-guard assert)",
          got is not None and "LENGTH = 0x800000" in got and "LENGTH = 0x180000" in got and "ORIGIN = 0x01E00000" in got
          and "ASSERT(_sram_end" not in got)
    check("another linker script (the update loader's) is left as it is", fm1base_ld.measure_script("MEMORY { RAM : ORIGIN = 0, LENGTH = 1 }") is None)
    import flash_layout as FLT
    lim = fm1base.limits({"app_slot": 581564})
    _stk = tomllib.loads((fm1base.APP / "fm1.toml").read_text())   # (main_stack / irq_stack move the SRAM's end)
    check("the release's limits: fm1pkg_make's app_slot of fm1.toml's app_end and of the SD card mode's, app.ld's SRAM "
          "up to the stacks fm1.toml sizes",
          lim == {"flash": 581564, "flash_by_layout": {str(k): FLT.STEPS[k].release_end - 0x4000 - 0x120 - 0xF24 for k in FLT.STEPS if k},
                  "sram": 0x7C000 - _stk.get("main_stack", 24320) - _stk.get("irq_stack", 7936) - 512})
    _toml = tomllib.loads((fm1base.APP / "fm1.toml").read_text())
    check("the release's ends are the step table's (tools/flash_layout.py STEPS release_end: L1 gen_optimist.py's SD_APP_END), "
          "fm1.toml app_steps every known end with its step and R (the update loader's switch rule), app_clear_top CLEAR_TOP",
          all(fm1base.release_end(k) == FLT.STEPS[k].release_end for k in FLT.STEPS if k)
          and fm1base.release_end(1) == fm1base.sd_app_end() and fm1base.release_end(0) is None
          and {e: (s, r) for e, s, r in _toml["app_steps"]} == FLT.known_ends()
          and _toml["app_clear_top"] == FLT.CLEAR_TOP
          and not any(__import__("registry").not_built("LAYOUT", k, "release") for k in FLT.STEPS))
    try:
        fm1base.limits({"app_slot": 581564 + 4})
        _refused = False
    except fm1base.FM1BaseError:
        _refused = True
    check("a measurement base whose app slot is not what fm1-base's rules say is refused", _refused)
print("fm1base: all passed" if not fails else f"fm1base: {fails} FAILED")
sys.exit(1 if fails else 0)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The flash layouts (docs/SD-MODE-DESIGN.md) say the same everywhere: tools/flash_layout.py against
firmware/hal/fm1_flash_map.h, the builder's FLASH_BY_LAYOUT, app.ld's normal XIP length, the package tool's normal
slot; and the layout switch's table (what an install after the other layout clears).
  python3 tests/flash_layout_test.py"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import flash_layout as L  # noqa: E402
import core as K  # noqa: E402

bad = 0


def check(what, ok):
    global bad
    print(f"{what:<96} {'ok' if ok else 'FAIL'}")
    bad += not ok


def _raises(f, *a):
    try:
        f(*a)
    except ValueError:
        return True
    return False


hdr = (ROOT / "firmware" / "hal" / "fm1_flash_map.h").read_text()


def c_hex(name):
    m = re.search(rf"#define\s+{name}\s+0x([0-9A-Fa-f]+)u", hdr)
    return int(m.group(1), 16) if m else None


check("fm1_flash_map.h FL_APP_HI_NORMAL / FL_APP_HI_BIG == flash_layout.APP_HI",
      c_hex("FL_APP_HI_NORMAL") == L.APP_HI[0] and c_hex("FL_APP_HI_BIG") == L.APP_HI[1])
ld = (ROOT / "firmware" / "app.ld").read_text()
check("app.ld's XIP length is the normal layout's app slot (581,564 B)",
      f"LENGTH = 0x{L.app_slot(0):X}" in ld and L.app_slot(0) == 581564)
check("the builder's flash limit is the normal slot, FLASH_BY_LAYOUT the others' code room once the far probe passed",
      K.LIMITS["flash"] == L.app_slot(0) and all(K.FLASH_BY_LAYOUT[k] == L.code_limit(k, True) for k in K.FLASH_BY_LAYOUT)
      and set(K.FLASH_BY_LAYOUT) == set(L.APP_HI) - {0})
check("the builder's fits(): LAYOUT 1 alone (a probe build) the normal slot, with FAR_OK the larger room",
      K.fits({r: 0 for r in K.REGIONS}, {"LAYOUT": 1})["flash"][1] == L.app_slot(0) - K.SPARE["flash"]
      and K.fits({r: 0 for r in K.REGIONS}, {"LAYOUT": 1, "FAR_OK": 1})["flash"][1] == L.code_limit(1, True) - K.SPARE["flash"])
check("layout_of: a flash.bin's length names its layout; another length is refused",
      L.layout_of(0x93000) == 0 and L.layout_of(0xD0000) == 1 and
      all(_raises(L.layout_of, n) for n in (0x92FFF, 0x9D000, 0xB7000)))
check("no layout's app reaches the update staging (0xE0000) or the kits / presets (0xDA000..)",
      all(v <= L.BIG_STORE_HI - 0xA000 for v in L.APP_HI.values()) and L.BIG_STORE_HI <= L.STORE_TOP)
normal_end, big_end = 0x4000 + 0x8E000, 0x4000 + 0xCB25B      # (app area blocks as the two packages have them)
check("switch: normal after normal, big after big: nothing cleared",
      L.switch_clear(normal_end, 0) == (0, 0) and L.switch_clear(big_end, 1) == (0, 0))
check("switch: normal after big clears the old app's tail [0x93000, 0xD0000)", L.switch_clear(big_end, 0) == (0x93000, 0xD0000))
check("switch: big after normal clears the squeezed store [0xD0000, 0xDA000)", L.switch_clear(normal_end, 1) == (0xD0000, 0xDA000))
check("switch: big after no header of ours (end 0) clears the squeezed store too", L.switch_clear(0, 1) == (0xD0000, 0xDA000))
ldr = (ROOT / "firmware" / "loader" / "ldr_core.c").read_text()
check("ldr_core.c clears the same ranges (FL_APP_HI_NORMAL..FL_APP_HI_BIG, FL_APP_HI_BIG..FL_UPF_HI)",
      "lo = FL_APP_HI_NORMAL, hi = FL_APP_HI_BIG" in ldr and "lo = FL_APP_HI_BIG, hi = FL_UPF_HI" in ldr
      and re.search(r"#define\s+FL_UPF_HI\s+0x000DA000u", hdr) is not None and L.BIG_STORE_HI == 0xDA000)



print("flash layout test " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)

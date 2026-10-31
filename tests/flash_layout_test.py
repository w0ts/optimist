#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The MEMORY steps (docs/SD-MODE-DESIGN.md section 9) say the same everywhere: tools/flash_layout.py's STEPS is the one
table; firmware/hal/fm1_steps.h is generated from it (checked byte for byte), the other copies are checked against it:
the builder's (registry.py LAYOUT_STEPS, the classic limits, fits()), the installers' size lists (fm1_route.py,
fm1_install.py, web/install_route.js, the iOS app's InstallRoute.swift), app.ld's normal XIP length; and the switch
rule against the matrix written out (9.3), every pair.
  python3 tests/flash_layout_test.py"""
import re
import sys

from builder_path import ROOT
import configure as C
import core as K
import flash_layout as L
import fm1_install
import fm1_route
import fm1pkg_make
import registry as R

C.CLASSIC = True                                     # (the classic firmware's layouts: tools/flash_layout.py is its)

bad = 0


def check(what, ok):
    global bad
    print(f"{what:<110} {'ok' if ok else 'FAIL'}")
    bad += not ok


def _raises(f, *a):
    try:
        f(*a)
    except (ValueError, SystemExit):
        return True
    return False


N, S0, L1 = 0, 3, 1
hal = ROOT / "firmware" / "hal"
check("fm1_steps.h is what tools/flash_layout.py --header makes (the one table, generated)",
      (hal / "fm1_steps.h").read_text() == L.c_header())
fmap = (hal / "fm1_flash_map.h").read_text()
check("fm1_flash_map.h takes the steps from fm1_steps.h, defines no app end of its own",
      '#include "fm1_steps.h"' in fmap and not re.search(r"#define\s+FL_(APP_HI|UPF_LO|DATA_LO)\b", fmap))
check("the table: N 0x93000, S0 0xB5000 (R 0xD8000), L1 0xD0000 (R 0xDA000, the release 0xCE000)",
      [(L.STEPS[k].end, L.STEPS[k].reloc, L.STEPS[k].release_end) for k in (N, S0, L1)]
      == [(0x93000, 0x93000, 0x93000), (0xB5000, 0xD8000, 0xB5000), (0xD0000, 0xDA000, 0xCE000)])
check("S0's store, as the design has it: FM6 voices 0xB5000, autosave A 0xB7000, a 32-sector log 0xB8000..0xD7FFF, 16 projects",
      (L.STEPS[S0].upf, L.STEPS[S0].autosave_a, L.STEPS[S0].log_lo, L.STEPS[S0].log_sectors, L.STEPS[S0].projects)
      == (0xB5000, 0xB7000, 0xB8000, 32, 16) and L.STEPS[S0].slots == 0 and not L.STEPS[S0].release_built)
check("S0 is the grid's stack (no sample slot, 16 projects): FM6 voices, autosave A, the log under 0xD8000",
      L.stacked(3, "S0", "no user samples", 0, 16) == L.STEPS[S0])
check("the projects by step: N 16, S0 16, L1 2 (the user, 2026-10-31: PROJECTS 2, 4 or 16; SD card mode keeps 1)",
      [L.STEPS[k].projects for k in (N, S0, L1)] == [16, 16, 2] and "FELUCCA_SDCARD && FELUCCA_LAYOUT == 1 ? 1u : FL_PROJ_SLOTS"
      in (ROOT / "firmware" / "src" / "core" / "registry.h").read_text())
check("app.bin's room: N 581,564, S0 720,828, L1 831,420 B (the far probe's reserve gone: code_limit == app_slot)",
      [L.app_slot(k) for k in (N, S0, L1)] == [581564, 720828, 831420] and all(L.code_limit(k) == L.app_slot(k) for k in L.STEPS))
ld = (ROOT / "firmware" / "app.ld").read_text()
check("app.ld's XIP length is the normal step's app slot", f"LENGTH = 0x{L.app_slot(0):X}" in ld)
build_py = (ROOT / "tools" / "build.py").read_text()
check("tools/build.py: no far probe (.text_far, far_off, FAR_RESERVE), the XIP length from the table for any step",
      ".text_far" not in build_py and "far_off" not in build_py and "FAR_RESERVE" not in build_py
      and "flash_layout.app_slot(layout())" in build_py and not hasattr(L, "far_off") and not hasattr(L, "FAR_RESERVE"))
check("no far probe in the firmware (far_probe.c gone, the console's far, fm1_guard.h's _xip_code_end)",
      not (ROOT / "firmware" / "src" / "system" / "far_probe.c").exists()
      and "con_far" not in (ROOT / "firmware" / "src" / "io" / "console.c").read_text()
      and "_xip_code_end" not in (hal / "fm1_guard.h").read_text())

_lim = K.limits_in_use()
check("the builder's classic limits come from here: the normal slot, every other step's whole slot",
      _lim["flash"] == L.app_slot(0) and _lim["flash_by_layout"] == {k: L.app_slot(k) for k in L.STEPS if k})
check("the builder's fits(): each step its own slot",
      all(K.fits({r: 0 for r in K.REGIONS}, {"LAYOUT": k})["flash"][1] == L.app_slot(k) - K.SPARE["flash"] for k in L.STEPS))
check("registry.py LAYOUT_STEPS (the web builder's copy) is the table's: sample slots, projects, app slot",
      set(R.LAYOUT_STEPS) == set(L.STEPS)
      and all(R.LAYOUT_STEPS[k] == (L.STEPS[k].slots, L.STEPS[k].projects, L.app_slot(k)) for k in L.STEPS))
check("LAYOUT's choices are the table's layouts; SAMPLE SLOTS' the table's slots (and 3, PATTERNS off), PROJECTS' its projects",
      sorted(v for v, _ in R.ITEMS["LAYOUT"].choices) == sorted(L.STEPS)
      and [v for v, _ in R.ITEMS["SMP_SLOTS"].choices] == sorted({s.slots for s in L.STEPS.values()} | {3})
      and [v for v, _ in R.ITEMS["PROJECTS"].choices] == sorted({s.projects for s in L.STEPS.values()}))
_pat = dict(K.defaults(), PATTERNS=1)
check("the fronts <-> LAYOUT: each step reads back as its (slots, projects); picking its projects from its slots, or its "
      "slots from its projects, gives the step",
      all(K.derive_fronts(dict(_pat, LAYOUT=k)) == {"SD_MODE": 0, "SMP_SLOTS": L.STEPS[k].slots, "PROJECTS": L.STEPS[k].projects}
          and K.set_front(dict(_pat, LAYOUT=k), "PROJECTS", L.STEPS[k].projects)["LAYOUT"] == k
          and K.set_front(dict(_pat, LAYOUT=k), "SMP_SLOTS", L.STEPS[k].slots)["LAYOUT"] == k for k in L.STEPS))
check("every (slots, projects) pair is one step: no two steps share it",
      len({(s.slots, s.projects) for s in L.STEPS.values()}) == len(L.STEPS))
_pairs = [(o, n) for o in L.STEPS for n in L.STEPS]
check("the builder's switch loss (core.switch_loss) is the table's (flash_layout.switch_loss) for every pair of steps",
      all(K.switch_loss(o, n) == L.switch_loss(L.app_hi(o), L.app_hi(n)) for o, n in _pairs))
check("the backup warning: nothing for the same step or own <-> release in one step; every other pair names the projects",
      all(not L.switch_warning(L.app_hi(k), L.app_hi(k)) for k in L.STEPS) and not L.switch_warning(0xD0000, 0xCE000)
      and all("the projects in flash" in L.switch_warning(L.app_hi(o), L.app_hi(n)) for o, n in _pairs if o != n)
      and L.switch_warning(None, 0x93000).startswith("If the FM-1 has another memory layout"))
check("... the slots a switch gives up: normal -> no sample slot: USR2, USR3 and the snapshots; no slot -> normal: none",
      "the user samples (USR2, USR3) and the snapshots" in L.switch_warning(0x93000, 0xB5000)
      and "user samples" not in L.switch_warning(0xB5000, 0x93000))

check("layout_of: a flash.bin's length names its step; another length is refused (the release's 0xCE000 too)",
      L.layout_of(0x93000) == N and L.layout_of(0xB5000) == S0 and L.layout_of(0xD0000) == L1 and
      all(_raises(L.layout_of, n) for n in (0x92FFF, 0x9D000, 0xB7000, 0xCE000, 0xB4000)))
check("no step's app or store reaches the kits (0xDA000) or the update staging (0xE0000)",
      all(s.end <= s.reloc <= L.CLEAR_TOP for s in L.STEPS.values()) and L.CLEAR_TOP == 0xDA000 and L.STORE_TOP == 0xE0000)
check("the package tool: every step's end makes a flash.bin, another end is refused",
      all(not _raises(fm1pkg_make.flash_layout.layout_of, L.app_hi(k)) for k in L.STEPS)
      and _raises(fm1pkg_make.flash_layout.layout_of, 0xC0000))

# the switch matrix (9.3, 9.7.2), written out: old end -> {new layout: (lo, hi)}; 0: no header of ours; 0xCE000 the
# release's L1. N, S0 and L1 written by hand; the grid's later steps' rows from the rule (the memopts steps' tool)
MATRIX = {
    0x93000: {N: (0, 0), S0: (0xB5000, 0xD8000), L1: (0xD0000, 0xDA000)},
    0xB5000: {N: (0x93000, 0xD8000), S0: (0, 0), L1: (0xD0000, 0xDA000)},
    0xD0000: {N: (0x93000, 0xDA000), S0: (0xB5000, 0xDA000), L1: (0, 0)},
    0xCE000: {N: (0x93000, 0xDA000), S0: (0xB5000, 0xDA000), L1: (0, 0)},
    0: {N: (0, 0), S0: (0xB5000, 0xD8000), L1: (0xD0000, 0xDA000)},
    0x5000: {N: (0, 0), S0: (0xB5000, 0xD8000), L1: (0xD0000, 0xDA000)},
    0xC0000: {N: (0x93000, 0xDA000), S0: (0xB5000, 0xDA000), L1: (0xD0000, 0xDA000)},
}
# matrix-steps:begin
MATRIX.setdefault(0x0, {}).update({2: (0xa5000, 0xc8000), 4: (0xba000, 0xc8000), 5: (0xbe000, 0xc8000), 6: (0xa6000, 0xb4000), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0x5000, {}).update({2: (0xa5000, 0xc8000), 4: (0xba000, 0xc8000), 5: (0xbe000, 0xc8000), 6: (0xa6000, 0xb4000), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0xc0000, {}).update({2: (0xa5000, 0xda000), 4: (0xba000, 0xda000), 5: (0xbe000, 0xda000), 6: (0xa6000, 0xda000), 8: (0xca000, 0xda000)})
MATRIX.setdefault(0x93000, {}).update({2: (0xa5000, 0xc8000), 4: (0xba000, 0xc8000), 5: (0xbe000, 0xc8000), 6: (0xa6000, 0xb4000), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0xa5000, {}).update({0: (0x93000, 0xc8000), 1: (0xd0000, 0xda000), 2: (0x0, 0x0), 3: (0xb5000, 0xd8000), 4: (0xba000, 0xc8000), 5: (0xbe000, 0xc8000), 6: (0xa6000, 0xc8000), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0xa6000, {}).update({0: (0x93000, 0xb4000), 1: (0xd0000, 0xda000), 2: (0xa5000, 0xc8000), 3: (0xb5000, 0xd8000), 4: (0xba000, 0xc8000), 5: (0xbe000, 0xc8000), 6: (0x0, 0x0), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0xb5000, {}).update({2: (0xa5000, 0xd8000), 4: (0xba000, 0xd8000), 5: (0xbe000, 0xd8000), 6: (0xa6000, 0xd8000), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0xba000, {}).update({0: (0x93000, 0xc8000), 1: (0xd0000, 0xda000), 2: (0xa5000, 0xc8000), 3: (0xb5000, 0xd8000), 4: (0x0, 0x0), 5: (0xbe000, 0xc8000), 6: (0xa6000, 0xc8000), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0xbe000, {}).update({0: (0x93000, 0xc8000), 1: (0xd0000, 0xda000), 2: (0xa5000, 0xc8000), 3: (0xb5000, 0xd8000), 4: (0xba000, 0xc8000), 5: (0x0, 0x0), 6: (0xa6000, 0xc8000), 8: (0xca000, 0xd8000)})
MATRIX.setdefault(0xca000, {}).update({0: (0x93000, 0xd8000), 1: (0xd0000, 0xda000), 2: (0xa5000, 0xd8000), 3: (0xb5000, 0xd8000), 4: (0xba000, 0xd8000), 5: (0xbe000, 0xd8000), 6: (0xa6000, 0xd8000), 8: (0x0, 0x0)})
MATRIX.setdefault(0xce000, {}).update({2: (0xa5000, 0xda000), 4: (0xba000, 0xda000), 5: (0xbe000, 0xda000), 6: (0xa6000, 0xda000), 8: (0xca000, 0xda000)})
MATRIX.setdefault(0xd0000, {}).update({2: (0xa5000, 0xda000), 4: (0xba000, 0xda000), 5: (0xbe000, 0xda000), 6: (0xa6000, 0xda000), 8: (0xca000, 0xda000)})
# matrix-steps:end
check("the switch matrix covers every step and every installed end", set(MATRIX) == {0, 0x5000, 0xC0000, *L.known_ends()}
      and all(set(row) == set(L.STEPS) for row in MATRIX.values()))
for old, row in MATRIX.items():
    for new, want in row.items():
        for o in ((old, old - 0xD70) if old else (old,)):   # (the head's end, inside the slot's last sector)
            got = L.switch_clear(o, new)
            check(f"switch to {L.STEPS[new].key} after {'no header' if not old else hex(o)}: clears "
                  f"[{want[0]:#x}, {want[1]:#x})", got == want)
check("switch, own <-> release in one step: L1 0xD0000 -> the release's 0xCE000 clears the old code's tail only",
      L.switch_clear_ends(0xD0000, 0xCE000) == (0xCE000, 0xD0000) and L.switch_clear_ends(0xCE000, 0xD0000) == (0, 0))
check("switch: no clear ever reaches 0xDA000", all(L.switch_clear_ends(o, n)[1] <= L.CLEAR_TOP
                                                   for o in [0, 0xC0000, *L.known_ends()] for n in L.known_ends()))

ldr = (ROOT / "firmware" / "loader" / "ldr_core.c").read_text()
check("ldr_core.c clears by the table (FL_ENDS, FL_RELOC_HI, FL_CLEAR_TOP), no layout constant of its own",
      "FL_ENDS(LDR_END_STEP)" in ldr and "FL_RELOC_HI" in ldr and "FL_CLEAR_TOP" in ldr and "FL_APP_HI_BIG" not in ldr)
ota = (ROOT / "firmware" / "src" / "system" / "ota.c").read_text()
check("ota.c step 1 takes every step's size from the table (FL_STEPS); the official loader normal only",
      "FL_STEPS(OTA_STEP_END)" in ota and "!ota_step_size(fl_len)" in ota and "official && fl_len != FL_APP_HI_NORMAL" in ota)

own = L.own_slots()
check("the installers' size lists are the table's: fm1_route.OWN_SLOTS, fm1_install.OWN_SLOTS",
      own == tuple(sorted(s.end for s in L.STEPS.values())) and own[0] == 0x93000 and fm1_route.OWN_SLOTS == own and fm1_install.OWN_SLOTS == own)


def _hex_list(text, pattern):
    m = re.search(pattern, text)
    return tuple(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))) if m else None


js = (ROOT / "web" / "install_route.js").read_text()
swift = (ROOT / "ios" / "OptimistMirror" / "Sources" / "Flash" / "InstallRoute.swift").read_text()
check("web/install_route.js OWN_SLOTS is the table's", _hex_list(js, r"export const OWN_SLOTS = \[([^\]]*)\]") == own)
_ends_js = re.search(r"export const LAYOUT_ENDS = (\[.*?\]\]);", js)
check("web/install_route.js LAYOUT_ENDS is the table's: every known end, its step, slots and name",
      _ends_js is not None and [[int(e, 16), int(k), int(s), n] for e, k, s, n in
                                re.findall(r'\[0x([0-9A-Fa-f]+), (\d+), (\d+), "([^"]+)"\]', _ends_js.group(1))]
      == [[e, k, L.STEPS[k].slots, L.STEPS[k].name] for e, (k, _) in L.known_ends().items()])
check("ios InstallRoute.swift ownSlots is the table's, and its refusal names them",
      _hex_list(swift, r"static let ownSlots = \[([^\]]*)\]") == own
      and f"takes the layouts {', '.join(f'0x{e:X}' for e in own[:-1])} and 0x{own[-1]:X} only" in swift)

print("flash layout test " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)

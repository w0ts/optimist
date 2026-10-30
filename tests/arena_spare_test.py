#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The arena runs up to the stacks' first guard band with no spare at run time (engines.c ARN_SPARE 0, 2026-10-30): that
holds only while nothing else writes the free SRAM and a stack that goes past its bottom traps in its guard band
instead of reaching the arena's top. Checked in the sources, both firmwares:
  - the arena's end is the guard band's start less ARN_SPARE, and ARN_SPARE is 0;
  - the stack guard and the write limit are armed at every boot (Optimist's system/main.c, fm1-base's
    device/main_fm1.c: fm1_guard_enable with FM1_GUARD_STACK and FM1_GUARD_WRITE), the write limit over the guard band
    (fm1_guard.h: _guard0 .. _ustack_lo - 1);
  - the free SRAM's two ends (_pool_end, _guard0) are read nowhere else but the arena's boot, the undo history's
    bounds, the stack tools and the boot's zeroing of the pool (up to its end)."""
import re
import sys

from tools_path import ROOT

FIRM = ROOT / "firmware"
FB = ROOT / "fm1base"
ALLOWED = {"src/engines/engines.c", "hal/fm1_stack.h", "hal/fm1_guard.h", "src/seq/undo.c", "src/system/stackmark.h",
           "src/system/main.c", "sim/sim.c"}   # (main.c: the pool zeroed at boot, to its end; sim.c: the stand-ins)
FB_ALLOWED = {"optimist/core/opt_core.c", "bsp/chip/fm1_guard.h", "device/main_fm1.c", "device/stacks.c",
              "optimist/app.c"}   # (main_fm1.c: the pool zeroed; app.c: the boot model under _guard0, before the arena boots)


def main():
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(f"{'ok  ' if ok else 'FAIL'} {what}")
        fails += not ok

    eng = (FIRM / "src" / "engines" / "engines.c").read_text()
    m = re.search(r"#define ARN_SPARE (\d+)u", eng)
    check(m is not None and m.group(1) == "0", "engines.c: ARN_SPARE 0 (the arena to the guard band)")
    check("arn_boot(fm1_stk_pool_end, fm1_stk_guard0 - ARN_SPARE)" in eng, "engines.c: the arena from the pool's end to "
          "the guard band less ARN_SPARE")
    for p in (FIRM / "src" / "system" / "main.c", FB / "device" / "main_fm1.c"):
        t = p.read_text()
        armed = re.search(r"fm1_guard_enable\([^)]*FM1_GUARD_STACK[^)]*FM1_GUARD_WRITE", t)
        check(armed is not None, f"{p.relative_to(ROOT)}: the stack guard and the write limit armed at boot")
    for p in (FIRM / "hal" / "fm1_guard.h", FB / "bsp" / "chip" / "fm1_guard.h"):
        t = p.read_text()
        check("FM1_WR_LIMIT_L(0) = (uint32_t)(uintptr_t)_guard0" in t and "_ustack_lo - 1u" in t,
              f"{p.relative_to(ROOT)}: the write limit over the guard band below the main stack")
    users = []
    for base, allowed in ((FIRM, ALLOWED), (FB, FB_ALLOWED)):
        for p in sorted(base.rglob("*.[chS]")):
            rel = p.relative_to(base).as_posix()
            if "build" in rel.split("/") or rel.startswith(("tests/", "bsp/examples/", "host/")):
                continue
            t = re.sub(r"/\*.*?\*/", "", p.read_text(errors="replace"), flags=re.S)
            if re.search(r"\b_pool_end\b|\b_guard0\b|fm1_stk_pool_end|fm1_stk_guard0", t) and rel not in allowed:
                users.append(f"{base.name}/{rel}")
    check(not users, "the free SRAM's ends read nowhere else" + (f": {users}" if users else ""))
    print(f"arena_spare_test: {'FAILED' if fails else 'passed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

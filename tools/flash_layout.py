# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 flash layouts (docs/SD-MODE-DESIGN.md): where the app slot ends. The same numbers as
firmware/hal/fm1_flash_map.h (FL_APP_HI_NORMAL, FL_APP_HI_BIG), for the build (app.ld's XIP length, the size limit),
the package (fm1pkg_make.py) and the rescue tool. tests/flash_layout_test.py checks the two agree.

  0  normal         the app slot [0x4000, 0x93000)
  1  fake SD mode   the app slot [0x4000, 0xD0000), the store squeezed into 0xD0000..0xDFFFF
"""

APP_LO = 0x4000
APP_HEAD = 0x120          # the app area's own directory before app.bin (fm1pkg_make.py app_area_head)
APP_TAIL = 0xF24          # after app.bin: the SDK's cfg_tool.bin and the cfg directory (eq_cfg_hw.bin)
STORE_TOP = 0xE0000       # the update staging starts here: no layout's app or store passes it

APP_HI = {0: 0x93000, 1: 0xD0000}
BIG_STORE_HI = 0xDA000    # the fake SD mode's squeezed store ends here (kits and presets above, as in the normal layout)
NAMES = {0: "normal", 1: "fake SD mode"}


def app_hi(layout: int) -> int:
    """the app slot's end: flash.bin is [0, app_hi)"""
    if layout not in APP_HI:
        raise ValueError(f"flash layout {layout}: one of {sorted(APP_HI)}")
    return APP_HI[layout]


def app_slot(layout: int) -> int:
    """app.bin's room (app.ld's XIP length, the build's flash limit): 581,564 B in the normal layout"""
    return app_hi(layout) - APP_LO - APP_HEAD - APP_TAIL


def layout_of(flash_len: int) -> int:
    """the layout whose flash.bin is flash_len bytes long (a package's flash entry)"""
    for k, v in APP_HI.items():
        if v == flash_len:
            return k
    raise ValueError(f"a flash.bin of {flash_len:#x} B matches no layout ({', '.join(hex(v) for v in APP_HI.values())})")


def switch_clear(old_end: int, new_layout: int) -> tuple:
    """the sectors [lo, hi) a switch of layout clears before the new app is written (ldr_core.c
    ldr_layout_switch): after a big app a normal one clears the old app's tail; after a normal app a big one
    clears its squeezed store (the normal layout's user samples and snapshots there). old_end: where the installed
    app area ends (its app_area_head). (0, 0): the same layout, nothing to clear."""
    normal, big = APP_HI[0], APP_HI[1]
    if new_layout == 0 and old_end > normal:
        return normal, big
    if new_layout == 1 and old_end <= normal:
        return big, BIG_STORE_HI
    return 0, 0

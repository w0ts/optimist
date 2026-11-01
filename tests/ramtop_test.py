#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/ramtop.py (SYSTEM > TOP's build-time table, op_top.c TOP_RAM) on a made-up symbol table: the SRAM below the
stacks only, by module (an engine's directory its own name, a function in the SRAM RAM CODE, *stack* STACKS, a symbol no
source defines OTHER), the 12 largest then OTHER (the rest folded in), always 13 entries; the header as op_top.c reads
it; tests/top_ram_test.h (the UI test's table) is this generator's output for TABLE_SYMS."""
import sys
from pathlib import Path

from tools_path import TOOLS  # (tools/ on sys.path)
import ramtop as T

HERE = Path(__file__).resolve().parent
# (14 modules, each its own size, and an unknown symbol; RAM code; a stack; a flash symbol and one in the stacks: not counted)
SYMS = """01c00000 00003000 t hot_render
01c03000 00000400 b ba_pcm
01c03400 00000200 b ble_rx
01c03600 00000300 b pl_state
01c03900 00000180 b an_state
01c03a80 00000100 b ui_buf
01c03b80 00000080 b fx_line
01c03c00 00000070 b rv_tank
01c03c70 00000060 b seq_q
01c03cd0 00000050 b voices
01c03d20 00000040 b mir_ring
01c03d60 00000030 b wf_rx
01c03d90 00000020 b usb_ring
01c03db0 00000010 b sd_buf
01c03dc0 00000008 b core_x
01c03dc8 00000100 b mystery
01c03ec8 00000800 b cpu2_stack
02000120 00001000 T flash_only
01c74100 00000400 b in_the_stack_band
"""
IDX = {"ba_pcm": "firmware/src/bt/bt_acl.c", "ble_rx": "fm1base/ble/ll.c", "pl_state": "firmware/src/engines/plaits/eng.c",
       "an_state": "firmware/src/engines/analog/a.c", "ui_buf": "firmware/src/ui/optimist/op_x.c",
       "fx_line": "firmware/src/fx/fx.c", "rv_tank": "firmware/src/fx/reverb/r.c", "seq_q": "firmware/src/seq/seq.c",
       "voices": "firmware/src/core/voice.c", "mir_ring": "firmware/src/io/editor/ed_mirror.c",
       "wf_rx": "fm1base/wifi/w.c", "usb_ring": "firmware/src/io/usb/u.c", "sd_buf": "firmware/src/storage/sd/s.c",
       "core_x": "firmware/src/core/model.c", "cpu2_stack": "fm1base/bsp/chip/c.c"}


def main():
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(f"{'ok  ' if ok else 'FAIL'} {what}")
        fails += not ok

    check(Path(T.__file__).resolve() == (TOOLS / "ramtop.py").resolve(), "tools/ramtop.py the one tested")
    syms = T.parse(SYMS)
    check(len(syms) == 19, "the nm -S lines parsed")
    rows = T.rows(syms, IDX)
    names = [n for n, _ in rows]
    check(len(rows) == T.N == 13 and rows[-1][0] == "OTHER", "13 entries, OTHER last")
    check(rows[0] == ("RAM CODE", 0x3000) and rows[1] == ("STACKS", 0x800) and rows[2] == ("BT AUDIO", 0x400),
          "the largest first: RAM CODE (a function in the SRAM), STACKS (*stack*), BT AUDIO (bt/)")
    check("PLAITS" in names and "ANALOG" in names and "BLE" in names and "MIRROR" in names and "REVERB" in names,
          "an engine's directory its own module (PLAITS, ANALOG); fm1-base's ble/ BLE; ed_mirror MIRROR; fx/reverb REVERB")
    check(all(a[1] >= b[1] for a, b in zip(rows[:-1], rows[1:-1])), "sorted by size")
    total = 0x3000 + 0x400 + 0x200 + 0x300 + 0x180 + 0x100 + 0x80 + 0x70 + 0x60 + 0x50 + 0x40 + 0x30 + 0x20 + 0x10 + 8 + 0x100 + 0x800
    check(sum(n for _, n in rows) == total, "every byte of the SRAM below the stacks counted once (flash, the stacks' band: not)")
    check(rows[-1][1] == 0x100 + 0x20 + 0x10 + 8 + 0x30 and "USB" not in names and "CORE" not in names,
          "OTHER: the unknown symbol and the modules past the 12 largest (WI-FI, USB, STORAGE, CORE)")
    few = T.rows(T.parse("01c00000 00000100 b ba_pcm\n"), IDX)
    check(len(few) == 13 and few[0] == ("BT AUDIO", 0x100) and few[1] == ("", 0) and few[-1] == ("OTHER", 0),
          "fewer modules: the empty entries before OTHER, still 13")
    objd = T.parse("01c00000 g     F .ram_text\t00000100 hot_render\n01c00100 l     O .bss\t00000040 ba_pcm\n")
    check(objd == [(0x01C00000, 0x100, "t", "hot_render"), (0x01C00100, 0x40, "b", "ba_pcm")],
          "objdump -t lines too (the classic build's own table): a function t, an object b")
    h = T.header(rows, "fake.elf")
    check("#define TOP_RAM_DEF 1" in h and h.count('{"') == 13 and '{"RAM CODE", 12288u},' in h and "_Static_assert" in h,
          "the header: TOP_RAM_DEF, 13 entries {name, bytes}, its count asserted")
    check(T.header([("a\"b/c very long name", 1)] + rows[1:], "x").count('{"AB/C VERY", ') == 1,
          "a name upper case, quotes dropped, 9 characters at most (op_top.c nm[10])")
    fake = (HERE / "top_ram_test.h").read_text()
    check(fake == T.header(rows, "tests/ramtop_test.py SYMS"),
          "tests/top_ram_test.h (the UI test's table) is the generator's output for this table")
    print(f"ramtop: {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SYSTEM > TOP's build-time table (builder item SYS_TOP; firmware/src/ui/optimist/op_top.c TOP_RAM): the SRAM the link
gives each module of the firmware, the N - 1 largest and OTHER, as a small C header (13 entries of a name and a size:
208 B of flash). The symbols in the SRAM below the stacks (tools/ramsize.py: each byte once, an alias or a member of
llvm's merged globals inside its group) by the source that defines them (ramsize.owners: Optimist's firmware/ and
fm1-base's), grouped into modules by MODULES (an engine's directory its own: PLAITS, ANALOG ..); a function in the SRAM
is RAM CODE, a symbol named *stack* STACKS, a symbol no source defines OTHER.

The build writes it between two links (tools/build.py, tools/fm1base.py): the first link has an empty table of the same
size, the second the table of the first's symbols (its RAM the same: only the table's bytes in flash differ).

  python3 tools/ramtop.py ELF --out HEADER [--nm PATH]     from an ELF (llvm-nm -S, or the toolchain's nm)
  python3 tools/ramtop.py --syms FILE --out HEADER           from nm -S or objdump -t text (the tests)
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ramsize as R  # noqa: E402  (the symbol table, the owners, the SRAM's bounds)

N = 13                                  # the entries: op_top.c TOP_RAM_N
NAME = 9                                # a name's characters at most (op_top.c top_ram_t nm[10])
HEADER = "sys_top_ram.h"                # its name in the generated headers (op_top.c __has_include)
# an owner's path (relative to the tree) -> its module, the first prefix that matches; None: the engine's directory
MODULES = (
    ("firmware/src/engines/engines.c", "ENGINES"),
    ("firmware/src/engines/", None),
    ("firmware/src/drums/", "DRUMS"),
    ("firmware/src/fx/insfx/", "INSERTS"),
    ("firmware/src/fx/reverb/", "REVERB"),
    ("firmware/src/fx/", "FX"),
    ("firmware/src/seq/undo", "UNDO"),
    ("firmware/src/seq/", "SEQUENCER"),
    ("firmware/src/core/voice", "VOICES"),
    ("firmware/src/core/", "CORE"),
    ("firmware/src/ui/", "UI"),
    ("firmware/src/display/", "SCREEN"),
    ("firmware/src/io/editor/ed_mirror", "MIRROR"),
    ("firmware/src/io/editor/", "EDITOR"),
    ("firmware/src/io/usb/", "USB"),
    ("firmware/src/io/midi/", "MIDI"),
    ("firmware/src/io/", "CONSOLE"),
    ("firmware/src/bt/", "BT AUDIO"),
    ("fm1base/bt/", "BT AUDIO"),
    ("firmware/src/ble/", "BLE"),
    ("fm1base/ble/", "BLE"),
    ("firmware/src/wifi/", "WI-FI"),
    ("fm1base/wifi/", "WI-FI"),
    ("firmware/src/link/", "LINK"),
    ("fm1base/link/", "LINK"),
    ("firmware/src/storage/", "STORAGE"),
    ("firmware/src/system/", "SYSTEM"),
    ("firmware/src/dsp/", "DSP"),
    ("fm1base/device/gfx", "SCREEN"),                  # (fm1-base's canvas: the screen's pixels)
    ("fm1base/device/storage", "STORAGE"),
    ("fm1base/device/wifi", "WI-FI"),
    ("fm1base/device/bt_own", "BT AUDIO"),
    ("fm1base/device/ble", "BLE"),
    ("fm1base/bsp/chip/usb", "USB"),
    ("fm1base/bsp/chip/fm1_ble", "BLE"),
    ("firmware/hal/", "PLATFORM"),
    ("fm1base/device/", "PLATFORM"),
    ("fm1base/bsp/", "PLATFORM"),
    ("fm1base/include/", "PLATFORM"),
    ("fm1base/optimist/", "APP"),
    ("fm1base/lib/", "LIBS"),
)


def module(owner, name, typ):
    """a symbol's module: RAM CODE (a function), STACKS (*stack*), by its owner's path, else OTHER"""
    if typ in "tT":
        return "RAM CODE"
    if "stack" in name.lower():
        return "STACKS"
    for pre, mod in MODULES:
        if owner.startswith(pre):
            return mod or owner[len(pre):].split("/")[0].upper()[:NAME]
    return "OTHER"


def parse_objdump(text):
    """objdump -t lines ("addr flags section<TAB>size name") -> [(addr, size, type, name)], type t for a function or a
    symbol of a RAM code section, b else (the build's own symbol table: tools/build.py)"""
    out = []
    for ln in text.splitlines():
        m = re.match(r"^([0-9a-fA-F]{8})\s(.{7})\s(\S+)\s+([0-9a-fA-F]+)\s+(\S+)$", ln)
        if m:
            fn = "F" in m.group(2) or m.group(3).startswith(".ram_")
            out.append((int(m.group(1), 16), int(m.group(4), 16), "t" if fn else "b", m.group(5)))
    return out


def parse(text):
    """nm -S or objdump -t text -> the symbols"""
    return R.parse_nm(text) or parse_objdump(text)


def rows(syms, idx):
    """-> [(module, bytes)]: N - 1 entries, the largest first ("" 0 where there are fewer), then OTHER"""
    R.stacks_of(syms)
    top_of = R.STACKS[0][1]                             # (the stacks' start: the SRAM the page counts ends there)
    tot = {}
    for a, n, t, name in R.unique(syms):
        if not R.SRAM[0] <= a < top_of:
            continue
        mod = module(R.owner_of(name, idx), name, t)
        tot[mod] = tot.get(mod, 0) + n
    other = tot.pop("OTHER", 0)
    best = sorted(tot.items(), key=lambda x: (-x[1], x[0]))
    other += sum(n for _, n in best[N - 1:])
    best = best[:N - 1]
    return best + [("", 0)] * (N - 1 - len(best)) + [("OTHER", other)]


def header(table, src):
    """the C header op_top.c includes"""
    lines = [f"/* generated by tools/ramtop.py from {src}: SYSTEM > TOP's table (op_top.c TOP_RAM), the SRAM by module */",
             "#define TOP_RAM_DEF 1",
             "static const top_ram_t TOP_RAM[TOP_RAM_N] = {"]
    for name, n in table:
        nm = re.sub(r"[^A-Z0-9 +./-]", "", name.upper())[:NAME]
        lines.append(f'    {{"{nm}", {n}u}},')
    lines += ["};", f'_Static_assert(sizeof TOP_RAM / sizeof TOP_RAM[0] == {N}u, "tools/ramtop.py N: op_top.c TOP_RAM_N");', ""]
    return "\n".join(lines)


def symbols_of(elf, nm=None):
    """an ELF's symbols through nm -S (llvm-nm, or nm given: the toolchain's)"""
    tool = nm or R.find_nm(None)
    return R.parse_nm(subprocess.run([tool, "-S", str(elf)], capture_output=True, text=True, check=True).stdout)


def write(out, syms, src, idx=None):
    """the header at out (rewritten only when it changes) -> the table"""
    table = rows(syms, R.owners() if idx is None else idx)
    text = header(table, src)
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.is_file() or out.read_text() != text:
        out.write_text(text)
    return table


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", nargs="?")
    ap.add_argument("--syms", help="nm -S or objdump -t text instead of an ELF")
    ap.add_argument("--out", required=True)
    ap.add_argument("--nm")
    a = ap.parse_args(argv)
    if bool(a.elf) == bool(a.syms):
        ap.error("an ELF or --syms")
    syms = parse(Path(a.syms).read_text()) if a.syms else symbols_of(a.elf, a.nm)
    table = write(a.out, syms, Path(a.elf or a.syms).name)
    for name, n in table:
        if n:
            print(f"{n:9,}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

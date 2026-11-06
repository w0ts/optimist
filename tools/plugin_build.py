# SPDX-License-Identifier: GPL-3.0-only
"""Build an engine as a plugin file (docs/SD-MODE-DESIGN.md section 10; firmware/src/core/plugin.h: the format).

  python tools/plugin_build.py SID                     -> <build dir>/plugins/SID.opl
  python tools/plugin_build.py SID --place 0x82000 -o placed.bin
                                                       the file as the loader would place it at that flash offset

The plugin is firmware/src/plugin/plug_unit.c (felucca.c's prelude, the engine's file, its descriptor exported) compiled
with the firmware's flags and the current build's configuration (build/gen/felucca_config.h: the same OPTIMIST_BUILD_DIR
as the firmware build), linked at address 0 with its relocations kept (ld -q). The file: the 48-byte header, the image
(.text and .rodata: code and constants, nothing writable), then one word per relocation (the kind in the top 4 bits,
the image offset in the rest). The loader (core/plugin.c plug_install) adds the image's XIP address to each one; place()
below does the same, for a package that carries the plugin from the start (tools/build.py OPTIMIST_PLUGIN_PRELOAD).

Measured on SID (2026-11-06): 1,594 B of code, 43,200 B of tables, 73 relocations, no symbol taken from the firmware."""
import argparse
import os
import re
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build                                    # noqa: E402  (OUT, GEN, CFLAGS, tc: the firmware build's own)
import toolchain as TC                          # noqa: E402

SRC = build.SRC
FW = build.FW
PLUG_MAGIC = 0x474C504F
PLUG_API = 2                                    # core/plugin.h PLUG_API
PLUG_KIND_ENGINE = 0
PLUG_HDR_LEN = 48
XIP_BASE = 0x02000000 - 0x4000                  # hal/fm1_xip.h FM1_XIP: flash offset -> CPU address
HDR = "<IHBB12sIIIIII"                          # magic api kind uid name len desc nrel crc base abi (44 B), then hcrc
REL_KINDS = {"R_PI32V2_ABS32": 0, "R_PI32V2_MOV_ABS32": 1}
REL_API = 2                                     # core/plugin.h PLUG_REL_API: the word set to the API table's address
API_SYM = "plugin_api"                          # plug_unit.c: that word
API_TABLE = "plug_api_tab"                      # core/plugin.c: the firmware's table (felucca.elf)
REL_PCREL = {"R_PI32V2_LONG_JUMP_23M2"}        # PC-relative: the same at every address (measured 2026-11-02)
ENTRY = "plugin_desc"
SAMPLE_ENGINES = {"SAMPLE", "GRAIN", "SLICE", "STRETCH"}   # SD card mode: the card's material (plug_unit.c PLUG_SAMPLES)


class PluginError(Exception):
    pass


def engine_info(name: str) -> tuple[int, str, str]:
    """(uid, the name the lists show, the engine's file relative to firmware/src) from core/registry.h and engines.c"""
    reg = (FW / "src" / "core" / "registry.h").read_text()
    m = re.search(rf'X\((\d+), {re.escape(name)}, \d+, "([^"]+)"\)', reg)
    if not m:
        raise PluginError(f"{name}: not an engine of core/registry.h ENGINE_LIST")
    eng = (FW / "src" / "engines" / "engines.c").read_text()
    f = re.search(rf'#include "([^"]*/eng_{name.lower()}\.c)"', eng)
    if not f:
        raise PluginError(f"{name}: engines.c does not include an eng_{name.lower()}.c")
    return int(m.group(1)), m.group(2), "engines/" + f.group(1)


def header(uid: int, name: str, image: bytes, nrel: int, desc: int, base: int = 0, abi: int = 0) -> bytes:
    h = struct.pack(HDR, PLUG_MAGIC, PLUG_API, PLUG_KIND_ENGINE, uid, name.encode()[:12].ljust(12, b"\0"),
                    len(image), desc, nrel, zlib.crc32(image), base, abi)
    return h + struct.pack("<I", zlib.crc32(h))


def parse(opl: bytes) -> tuple[dict, bytes, list[int]]:
    """(the header's fields, the image, the relocation words) of a plugin file"""
    if len(opl) < PLUG_HDR_LEN:
        raise PluginError("not a plugin file: too short")
    fields = struct.unpack(HDR, opl[:44])
    h = dict(zip(("magic", "api", "kind", "uid", "name", "len", "desc", "nrel", "crc", "base", "abi"), fields))
    if h["magic"] != PLUG_MAGIC or struct.unpack("<I", opl[44:48])[0] != zlib.crc32(opl[:44]):
        raise PluginError("not a plugin file: the magic or the header's CRC")
    if h["api"] != PLUG_API:
        raise PluginError(f"a plugin for API {h['api']}, this firmware's is {PLUG_API}")
    image = opl[PLUG_HDR_LEN:PLUG_HDR_LEN + h["len"]]
    rel = opl[PLUG_HDR_LEN + h["len"]:PLUG_HDR_LEN + h["len"] + 4 * h["nrel"]]
    if len(image) != h["len"] or len(rel) != 4 * h["nrel"] or zlib.crc32(image) != h["crc"]:
        raise PluginError("a plugin file cut short, or its image's CRC")
    return h, image, list(struct.unpack(f"<{h['nrel']}I", rel))


def patch(image: bytearray, rel: int, base: int, api: int = 0) -> None:
    """one relocation applied in place (core/plugin.c plug_patch); api: the firmware's API table's address (kind 2)"""
    kind, off = rel >> 28, rel & 0x0FFFFFFF
    if kind == REL_API:
        if off + 4 > len(image):
            raise PluginError(f"relocation at {off:#x} past the image")
        struct.pack_into("<I", image, off, api & 0xFFFFFFFF)
    elif kind == 0:
        if off + 4 > len(image):
            raise PluginError(f"relocation at {off:#x} past the image")
        struct.pack_into("<I", image, off, (struct.unpack_from("<I", image, off)[0] + base) & 0xFFFFFFFF)
    elif kind == 1:
        if off + 8 > len(image):
            raise PluginError(f"relocation at {off:#x} past the image")
        w0, w1 = struct.unpack_from("<II", image, off)
        v = ((w0 >> 16) | ((w1 & 0xFFFF) << 16)) + base & 0xFFFFFFFF
        struct.pack_into("<II", image, off, (w0 & 0xFFFF) | (v << 16) & 0xFFFFFFFF, (w1 & 0xFFFF0000) | (v >> 16))
    else:
        raise PluginError(f"relocation kind {kind}")


def place(opl: bytes, flash_off: int, api: int = 0) -> bytes:
    """the plugin file as the loader places it at flash offset flash_off (a sector boundary): the header with the
    image's XIP address and the CRC of the patched image, then the image. api: the firmware's API table's address
    (its felucca.elf symbol plug_api_tab), written into the plugin's plugin_api word"""
    if flash_off & 0xFFF:
        raise PluginError(f"placing at {flash_off:#x}: not a sector boundary")
    h, image, rels = parse(opl)
    base = XIP_BASE + flash_off + PLUG_HDR_LEN
    img = bytearray(image)
    for r in rels:
        patch(img, r, base, api)
    return header(h["uid"], h["name"].rstrip(b"\0").decode(), bytes(img), 0, h["desc"], base, h["abi"]) + bytes(img)


def firmware_flags() -> list[str]:
    """the firmware's compile flags for this build directory (tools/build.py build_app's, without the environment's
    FELUCCA_* switches: the configuration header carries the build's)"""
    gen = build.GEN / "felucca_config.h"
    if not gen.exists():
        raise PluginError(f"{gen}: no configuration; build the firmware first (the same OPTIMIST_BUILD_DIR)")
    return [*build.CFLAGS, "-Ifirmware/hal", "-Ifirmware/src", "-I" + build.GEN_REL, "-include", f"{build.GEN_REL}/felucca_config.h"]


def section_sizes(elf: Path) -> dict[str, int]:
    out = build.tc("common/bin/objdump", "-h", elf)
    sizes = {}
    for m in re.finditer(r"^ +\d+ +(\S+) +([0-9a-f]{8})", out, re.M):
        sizes[m.group(1)] = int(m.group(2), 16)
    return sizes


def relocations(elf: Path) -> list[int]:
    """the relocation words of the linked image (objdump -r: the kept .rela sections), sorted by offset"""
    out = build.tc("common/bin/objdump", "-r", elf)
    rels, sec = [], None
    for line in out.splitlines():
        m = re.match(r"RELOCATION RECORDS FOR \[(\S+)\]", line)
        if m:
            sec = m.group(1)
            continue
        m = re.match(r"^([0-9a-f]+) +(R_PI32V2_\w+)", line)
        if not m or sec is None:
            continue
        off, kind = int(m.group(1), 16), m.group(2)
        if kind in REL_PCREL:
            continue
        if kind not in REL_KINDS:
            raise PluginError(f"relocation {kind} at {off:#x} in {sec}: not one the loader patches (core/plugin.h)")
        rels.append(REL_KINDS[kind] << 28 | off)
    return sorted(rels, key=lambda r: r & 0x0FFFFFFF)


def symbol(elf: Path, name: str) -> int:
    out = build.tc("pi32v2/bin/nm", elf)
    m = re.search(rf"^([0-9a-f]+) \w {re.escape(name)}$", out, re.M)
    if not m:
        raise PluginError(f"{name}: not a symbol of the linked plugin")
    return int(m.group(1), 16)


def build_plugin(name: str, out_dir: Path | None = None) -> Path:
    """the engine NAME (registry.h) built as <out_dir>/NAME.opl"""
    uid, shown, rel_file = engine_info(name)
    out_dir = out_dir or build.OUT / "plugins"
    out_dir.mkdir(parents=True, exist_ok=True)
    obj, elf, ld = out_dir / f"{name}.o", out_dir / f"{name}.elf", out_dir / "plugin.ld"
    ld.write_text("SECTIONS { . = 0; .text : { *(.text*) } .rodata : { *(.rodata*) . = ALIGN(4); } "
                  ".data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.comment) *(.note*) } }\n")
    sym = f"ENG_{name}"
    build.tc("cc", *firmware_flags(), f"-DFELUCCA_ENG_{name}=1", f'-DPLUG_ENGINE_FILE="{rel_file}"',
             f"-DPLUG_ENGINE_SYM={sym}", f"-DPLUG_ENG_{name}=1",
             *(["-DPLUG_SAMPLES=1"] if name in SAMPLE_ENGINES else []), "-c", FW / "src" / "plugin" / "plug_unit.c", "-o", obj)
    build.tc("pi32v2/bin/ld", "--gc-sections", "-q", "-e", ENTRY, f"--undefined={API_SYM}", "-T", ld, obj, "-o", elf)
    sizes = section_sizes(elf)
    if sizes.get(".data", 0) or sizes.get(".bss", 0):
        raise PluginError(f"{name}: {sizes.get('.data', 0)} B of .data and {sizes.get('.bss', 0)} B of .bss: a plugin has "
                          "no RAM of its own (its state is the voice's and the part's memory)")
    img = out_dir / f"{name}.bin"
    build.tc("common/bin/objcopy", "-O", "binary", elf, img)
    image = img.read_bytes()
    image += b"\0" * (-len(image) % 4)
    rels = relocations(elf)
    api_at = symbol(elf, API_SYM)               # (kept by --undefined: every plugin has the word, linked as 0)
    if api_at + 4 > len(image) or any(r & 0x0FFFFFFF == api_at for r in rels):
        raise PluginError(f"{API_SYM} at {api_at:#x}: outside the image, or relocated as an address")
    rels = sorted(rels + [REL_API << 28 | api_at], key=lambda r: r & 0x0FFFFFFF)
    desc = symbol(elf, sym)
    abi_at = symbol(elf, "plugin_abi")
    if desc + 4 > len(image) or abi_at + 4 > len(image):
        raise PluginError(f"{sym} at {desc:#x} or plugin_abi at {abi_at:#x}: outside the image")
    abi = struct.unpack_from("<I", image, abi_at)[0]
    opl = header(uid, shown, image, len(rels), desc, 0, abi) + image + struct.pack(f"<{len(rels)}I", *rels)
    out = out_dir / f"{name}.opl"
    out.write_bytes(opl)
    print(f"plugin   {out}  {len(opl)} B: UID {uid} {shown}, code {sizes.get('.text', 0)} B, constants "
          f"{sizes.get('.rodata', 0)} B, {len(rels)} relocations, descriptor at {desc:#x}, ABI {abi:08x}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("engine", help="the engine's name in core/registry.h ENGINE_LIST (SID, PROPHET, ...), or a .opl file to place")
    ap.add_argument("--place", type=lambda s: int(s, 0), metavar="FLASH_OFF", help="write the file as placed at this flash offset")
    ap.add_argument("-o", "--out", type=Path, help="the placed file (with --place)")
    ap.add_argument("--elf", type=Path, help="the firmware's felucca.elf (with --place: its API table's address; else 0)")
    a = ap.parse_args()
    try:
        if a.engine.endswith(".opl"):
            opl = Path(a.engine)
        else:
            build.backend()                             # (the compiler alone: no Pillow, no SDK needed here)
            opl = build_plugin(a.engine)
        if a.place is not None:
            api = symbol(a.elf, API_TABLE) if a.elf else 0
            placed = place(opl.read_bytes(), a.place, api)
            out = a.out or opl.with_suffix(f".{a.place:x}.bin")
            out.write_bytes(placed)
            print(f"placed   {out}  {len(placed)} B at 0x{a.place:x} (image at 0x{XIP_BASE + a.place + PLUG_HDR_LEN:x})")
    except (PluginError, TC.ToolchainError) as e:
        print(f"plugin_build: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

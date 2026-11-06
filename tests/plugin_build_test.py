# SPDX-License-Identifier: GPL-3.0-only
"""The plugin file tool (tools/plugin_build.py) without the toolchain: the header, parse(), the two relocation kinds,
place() against the loader's result (tests/plugin_test.c's first vector: the same synthetic file, the same CRC of the
placed image), the refusals. python3 tests/plugin_build_test.py"""
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import plugin_build as pb  # noqa: E402

FL_PLUG_LO = 0x82000                    # the normal layout's 64 KB region (plugin_test.c)
IMG_LEN = 10000
REL = [0x10, 0x1000, (1 << 28) | 0x6E, (2 << 28) | 0x200, 0x1FCE, (1 << 28) | 0xFCA]
API = 0x02ABCDE0                        # plugin_test.c PLUG_API_ADDR
C_TEST_CRC = 0x690C1FFA                 # what plugin_test.c printed for its first vector (2026-11-06, with kind 2)
fails = 0


def check(cond: bool, what: str) -> None:
    global fails
    if not cond:
        fails += 1
        print("FAIL", what)


def vector1() -> bytes:
    """plugin_test.c mk_file(23, IMG_LEN, REL, NREL, 0x100)"""
    img = bytearray()
    for i in range(0, IMG_LEN, 4):
        img += struct.pack("<I", (i * 0x01010101 + 7) & 0xFFFFFFFF)
    for r in REL:
        if r >> 28 == 1:
            struct.pack_into("<II", img, r & 0x0FFFFFFF, 0x3456FFC8, 0x18BA0012)
    h = pb.header(23, "TEST", bytes(img), len(REL), 0x100)
    return h + bytes(img) + struct.pack(f"<{len(REL)}I", *REL)


def main() -> int:
    f = vector1()
    h, image, rels = pb.parse(f)
    check(h["uid"] == 23 and h["len"] == IMG_LEN and h["desc"] == 0x100 and rels == REL and len(f) == 48 + IMG_LEN + 4 * len(REL), "parse")
    placed = pb.place(f, FL_PLUG_LO, API)
    base = pb.XIP_BASE + FL_PLUG_LO + 48
    ph, pimg, prels = pb.parse(placed)
    check(ph["base"] == base and ph["nrel"] == 0 and prels == [] and len(placed) == 48 + IMG_LEN, "the placed header")
    check(zlib.crc32(pimg) == C_TEST_CRC, f"the placed image's CRC {zlib.crc32(pimg):08x} = the loader's {C_TEST_CRC:08x}")
    check(struct.unpack_from("<I", pimg, 0x10)[0] == (0x10 * 0x01010101 + 7 + base) & 0xFFFFFFFF, "ABS32 patched")
    check(struct.unpack_from("<I", pimg, 0x200)[0] == API, "API word set")
    w0, w1 = struct.unpack_from("<II", pimg, 0x6E)
    check(((w0 >> 16) | (w1 & 0xFFFF) << 16) == (0x123456 + base) & 0xFFFFFFFF and w0 & 0xFFFF == 0xFFC8 and w1 >> 16 == 0x18BA, "MOV32 patched")
    try:
        pb.place(f, FL_PLUG_LO + 10)
        check(False, "an unaligned place refused")
    except pb.PluginError:
        pass
    bad = bytearray(f)
    bad[0] ^= 1
    try:
        pb.parse(bytes(bad))
        check(False, "a wrong magic refused")
    except pb.PluginError:
        pass
    try:
        pb.patch(bytearray(8), (1 << 28) | 4, 0)
        check(False, "a relocation past the image refused")
    except pb.PluginError:
        pass
    try:
        pb.patch(bytearray(8), (3 << 28) | 0, 0)
        check(False, "an unknown kind refused")
    except pb.PluginError:
        pass
    print(f"{fails} failed")
    return fails


if __name__ == "__main__":
    sys.exit(main())

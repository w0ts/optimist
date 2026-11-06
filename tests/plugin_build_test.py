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
C_TEST_CRC = 0xB84DAF86                 # what plugin_test.c printed for its first vector (2026-11-06, the 64-byte header)
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
    check(h["uid"] == 23 and h["len"] == IMG_LEN and h["desc"] == 0x100 and rels == REL and len(f) == pb.PLUG_HDR_LEN + IMG_LEN + 4 * len(REL), "parse")
    placed = pb.place(f, FL_PLUG_LO, API)
    base = pb.XIP_BASE + FL_PLUG_LO + pb.PLUG_HDR_LEN
    ph, pimg, prels = pb.parse(placed)
    check(ph["base"] == base and ph["nrel"] == 0 and prels == [] and len(placed) == pb.PLUG_HDR_LEN + IMG_LEN, "the placed header")
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
    cfg = {"FELUCCA_CPU_GUARD": "1", "FELUCCA_A2_LADR": "1", "FELUCCA_A2_OTA": "0"}
    acid = pb.extra_units("ACID", cfg)
    check(len(acid) == 1 and acid[0][0].name == "acid_dsp.c" and "-DFELUCCA_CPU_GUARD=1" in acid[0][1]
          and "-DFAST_RAM_acid=0" in acid[0][1], "ACID: acid_dsp.c, the guard, its code not in RAM")
    check("-DFELUCCA_CPU_GUARD=1" not in pb.extra_units("ACID", {})[0][1], "ACID: no guard define without the config's")
    a2 = pb.extra_units("ANALOG", cfg)
    check(a2[0][0].name == "a2_models.c" and "-DFELUCCA_A2_LADR=1" in a2[0][1] and "-DFELUCCA_A2_OTA=0" in a2[0][1]
          and "-DFELUCCA_A2_MS20=0" in a2[0][1], "ANALOG: a2_models.c with the config's A2 flags")
    p5 = pb.extra_units("PROPHET", cfg)
    check(p5[0][0].name == "p5_dsp.c" and p5[0][1] == ["-DFAST_RAM_prophet=0"], "PROPHET: p5_dsp.c")
    check(all(p.exists() for n in ("ACID", "ANALOG", "PROPHET") for p, _ in pb.extra_units(n, cfg)), "the unit files exist")
    check(pb.extra_units("SID", cfg) == [], "SID: no extra unit")
    print(f"{fails} failed")
    return fails


if __name__ == "__main__":
    sys.exit(main())

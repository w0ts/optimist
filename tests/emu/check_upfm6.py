# SPDX-License-Identifier: GPL-3.0-only
"""upfm6: FELUCCA_UP_FM6's voices (OBJ_UPFM6) off the stock firmware's SDK VM, at 0x95000 / 0x96000 (716bf52;
hal/fm1_flash_map.h FL_UPF). The flash is seeded with what an FM-1 can hold: a synthetic SDK VM at 0xE8000 (magic
55 AA AA 55 and a record log), BTIF bytes at 0xE9000, and an old OBJ_UPFM6 copy A at 0xE7000 (where the builds
before the fix kept it: U01's voice kept). Start 1: the old copy moved to 0x95000 (st_upf_move), U01's voice kept;
then a user preset SAVE into U01 (SAVE > USER, KNOB4 twice; SLOOP's UI: an OBJ_UPFM6 write, copy B 0x96000). A power
cycle: not moved again, nothing else written. Each time, and in the flash state saved at the end: 0xE7000..0xE9FFF
byte-identical to the seed (read from the serial NOR as the chip holds it, not through the XIP cache)."""
import struct
import zlib

import session as S

NEW_A, NEW_B = 0x95000, 0x96000                   # (FL_UPF_LO: the final place, whatever the build under test says)
OLD_A = 0xE7000
SYS_LO, SYS_HI = 0xE7000, 0xEA000                 # (the retired copy A, the SDK VM, BTIF: never written)
ST_MAGIC, OBJ_UPFM6, PAYLOAD_OFF = 0x554C4546, 10, 256   # (storage.c: "FELU"; OBJ_UPFM6 the 11th object; the payload)
UPF_MAGIC, UPF_VB = 0x36465055, 112               # (upreset.c: "UPF6", a packed voice)


def seed(slots):
    """-> (the 1 MiB image, its sectors, the OBJ_UPFM6 payload at 0xE7000)"""
    nor = bytearray(b"\xff" * S.NOR_SIZE)
    vm = bytes([0x55, 0xAA, 0xAA, 0x55, 0x91, 0x6A, 0x20, 0x00, 0x0B, 0x0B, 0x2C, 0x6B, 0x70, 0x00, 0x01, 0x07])
    nor[0xE8000:0xE8010] = vm
    for i in range(16, 0xAD):                     # (a record log after the magic)
        nor[0xE8000 + i] = (i * 37 + 11) & 0xFF
    for i in range(10):
        nor[0xE9000 + i] = 0xB0 + i
    pay = struct.pack("<II", UPF_MAGIC, 1) + bytes((i * 13 + 5) & 0x7F for i in range(slots * UPF_VB))
    h = struct.pack("<IHHIII", ST_MAGIC, OBJ_UPFM6, 0, 3, len(pay), zlib.crc32(pay)) + b"\xff" * 8
    h += struct.pack("<I", zlib.crc32(h))
    nor[OLD_A + PAYLOAD_OFF:OLD_A + PAYLOAD_OFF + len(pay)] = pay
    nor[OLD_A:OLD_A + 32] = h
    return nor, (0xE7000, 0xE8000, 0xE9000), pay


def head(e, off):
    """the object head at off -> (an OBJ_UPFM6 head, its slot, seq, len) or None"""
    m, t, s, q, n = struct.unpack_from("<IHHII", e.nor(off, 16))
    return (s, q, n) if m == ST_MAGIC and t == OBJ_UPFM6 else None


def run(ctx):
    f = ctx.facts
    if not f.on("UP_FM6"):
        ctx.info("skipped", "FELUCCA_UP_FM6 0")
        return
    image, sectors, pay = seed(f.on("UP_SLOTS"))
    S.seed_state(ctx.out, image, sectors)
    sys0 = bytes(image[SYS_LO:SYS_HI])
    e, P = ctx.open(fresh=False, settle=3.0)
    try:
        a = head(e, NEW_A)
        ctx.check("start 1: the old copy moved to 0x95000 (OBJ_UPFM6, copy A, the payload's length)",
                  a is not None and a[0] == 0 and a[2] == len(pay), f"head at 0x95000 {a}, at 0xE7000 {head(e, OLD_A)}")
        ctx.check("... its payload as it was at 0xE7000", e.nor(NEW_A + PAYLOAD_OFF, len(pay)) == pay, "")
        used = e.peek("upf_used")[0]
        ctx.check("... U01's voice kept (upf_used)", used == 1, f"upf_used {used:#x}")
        ctx.check("... 0xE7000..0xE9FFF as seeded (the old copy, the SDK VM, BTIF)", e.nor(SYS_LO, SYS_HI - SYS_LO) ==
                  sys0, "")
        if f.ui == 0:
            ok = P.goto("SAVE", "USER")
            e.click("KNOB1", -40)                         # (U01)
            P.arm_act("KNOB4")
            P.shot("user-saved")
            P.home()
            b = head(e, NEW_B)
            ctx.check("a user preset SAVE into U01: OBJ_UPFM6 written as copy B at 0x96000 (the next seq)",
                      ok and a is not None and b is not None and b[0] == 1 and b[1] == a[1] + 1,
                      f"head at 0x96000 {b}, A {a}")
            ctx.check("... 0xE7000..0xE9FFF still as seeded", e.nor(SYS_LO, SYS_HI - SYS_LO) == sys0, "")
        else:
            ctx.info("the user preset save", "skipped: SLOOP's UI gestures (this build has the Optimist UI)")
        used1, heads = e.peek("upf_used")[0], (head(e, NEW_A), head(e, NEW_B))
        e.quit()
        e, P = ctx.open(fresh=False, settle=3.0)
        h2 = (head(e, NEW_A), head(e, NEW_B))
        ctx.check("start 2 (a power cycle): not moved again, the copies as left", h2 == heads, f"{h2}, left {heads}")
        ctx.check("... upf_used as saved", e.peek("upf_used")[0] == used1, f"{e.peek('upf_used')[0]:#x}, {used1:#x}")
        ctx.check("... 0xE7000..0xE9FFF still as seeded", e.nor(SYS_LO, SYS_HI - SYS_LO) == sys0, "")
    finally:
        e.quit()
    saved = S.read_state(ctx.out)
    ctx.check("the saved flash state: 0xE7000..0xE9FFF byte-identical to the seed",
              saved is not None and saved[SYS_LO:SYS_HI] == sys0, "")

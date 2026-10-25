#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The RAMTEXT overlays' link (tools/ovl_link.py, docs/SD-MODE-DESIGN.md 6): the sections found, the plan (the least
RAMTEXT; a tie: the larger slot), the linker script's edits of app.ld (the layout's), the table the firmware reads
(core/overlay.c rto_rom_t, rto_ent_t).
  python3 tests/ovl_link_test.py"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ovl_link as O  # noqa: E402

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


H = """Idx Name          Size      Address          Type
  5 .ram_hot.isr_c1_wake 00000010 0000000000000000 TEXT DATA
  7 .ovl.analog.analog_render 000006d0 0000000000000000 TEXT DATA
  8 .ovl.analog.a2_saw 00000082 0000000000000000 TEXT DATA
  9 .ovh.analog   00000014 0000000000000000 DATA
 10 .ovl.digital.digital_render 00000522 0000000000000000 TEXT DATA
 11 .ovh.digital  00000014 0000000000000000 DATA
 12 .ovx.strings.strings_render 000005b6 0000000000000000 TEXT DATA
 13 .ovh.strings  00000014 0000000000000000 DATA
"""
k = O.kinds(H)
check("kinds: each engine with a hooks' table, OVL or OVLX", k == {"analog": "ovl", "digital": "ovl", "strings": "ovx"})
check("kinds: code without its table refused", _raises(O.kinds, H.replace(".ovh.digital", ".data.x")))
check("kinds: both kinds for one engine refused", _raises(O.kinds, H + "  14 .ovx.analog.x 00000002 0 TEXT\n"))

# the plan: three slots; OVL larger than a slot stays in RAMTEXT (it costs), OVLX stays in XIP (it costs nothing)
sizes = {"analog": 5160, "digital": 1336, "phase": 2232}
kind = {"analog": "ovl", "digital": "ovl", "phase": "ovl"}
p = O.plan(sizes, kind, 3)
check("plan: today's three engines: no slot pays (3 x 1336 + the rest > all resident)", p.slot_len == 0 and p.slots == []
      and p.ramtext == 5160 + 1336 + 2232)
many = {"analog": 5160, "digital": 1336, "phase": 2232, "trio": 2100, "formant": 1900, "drawbar": 1800, "lofi": 1500}
p = O.plan(many, {n: "ovl" for n in many}, 3)
check("plan: seven engines: slots of PHASE's size, ANALOG resident (the least RAMTEXT)",
      p.slot_len == 2232 and "analog" not in p.slots and len(p.slots) == 6 and p.ramtext == 5160 + 3 * 2232)
p = O.plan({"a": 1000, "b": 1000, "c": 1000, "d": 1000}, {"a": "ovl", "b": "ovl", "c": "ovl", "d": "ovl"}, 3)
check("plan: four equal engines: the slots (3000 B instead of 4000)", p.slot_len == 1000 and p.ramtext == 3000)
p = O.plan({"a": 1000, "b": 1000, "c": 1000}, {"a": "ovl", "b": "ovl", "c": "ovx"}, 3)
check("plan: an OVLX engine alone does not pay for slots", p.slot_len == 0 and p.ramtext == 2000)
p = O.plan(sizes, kind, 3, force="2400")
check("plan: forced (OPTIMIST_OVL_SLOT): the engines that fit, the slot no larger than they need",
      p.slots == ["digital", "phase"] and p.slot_len == 2232)
check("plan: forced 0: no slots", O.plan(sizes, kind, 3, force="0").slot_len == 0)

LD = ("    .text : { KEEP(*(.text.crt0)) *(.text .text.*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP\n"
      "    .text_far (ORIGIN(XIP) + 0x90000) : { KEEP(*(.text_far .text_far.*)) } > XIP\n"
      "    _lz_load = _etext;\n"
      "    .ram_hot : { _rh_start = .; KEEP(*(.ram_hot .ram_hot.*)) . = ALIGN(4); _rh_end = .; } > RAMTEXT AT > LOAD\n"
      "    _rh_load = LOADADDR(.ram_hot);\n")
k3 = {"analog": "ovl", "digital": "ovl", "strings": "ovx"}
p = O.Plan(1336, 3, ["digital"], 0)
s1 = O.script(LD, k3, p, 1, 0x40)
check("script: the slot engines one OVERLAY at slot k's address, NOCROSSREFS",
      "OVERLAY _ovl_slots + 0x538 : NOCROSSREFS" in s1 and ".ovl_digital {" in s1 and ".ovl_analog {" not in s1)
check("script: an OVL engine larger than a slot in .ram_hot, an OVLX one in .text",
      ".ovl.analog .ovl.analog.*" in s1.split(".ram_hot :")[1].split("\n")[0]
      and ".ovx.strings .ovx.strings.*" in s1.split(".text :")[1].split("\n")[0])
check("script: the table after the far probe, the boot stream where it was",
      "_ovl_rom = ALIGN(ADDR(.text_far) + SIZEOF(.text_far), 4); _ovl_rom_end = _ovl_rom + 0x40;" in s1
      and "_lz_load = _etext;" in s1)
check("script: the slots' span asserted inside RAMTEXT", "_ovl_slots_end = _ovl_slots + 0xfa8;" in s1
      and "ASSERT(_ovl_slots_end <= ORIGIN(RAMTEXT)" in s1)
s0 = O.script(LD, k3, p, 0, 0x40)
check("script: two slots' links differ only in the OVERLAY's address",
      [a for a, b in zip(s0.splitlines(), s1.splitlines()) if a != b] == ["    OVERLAY _ovl_slots + 0x0 : NOCROSSREFS AT (0x10080000)"])
check("script: an app.ld without its anchors refused", _raises(O.script, LD.replace("_rh_end = .;", ""), k3, p, 0, 0))
sp = O.script(LD, k3, None, 0, 0, probe=True)
check("script: the probe: every engine in the OVERLAY, no table, no span", ".ovl_analog {" in sp and ".ovl_strings {" in sp
      and "_ovl_slots_end = _ovl_slots + 0x0;" in sp and "ASSERT(_ovl_slots_end" not in sp)

# the table
dig = [struct.pack("<I", 0x4F560001) + bytes([k]) * 1330 for k in range(3)]       # 1334 B: rounded to 1336
szs = {"analog": 5160, "digital": 1334, "strings": 1462}
res = {"analog": (0x01C04E08, 0x4F560000), "strings": (0x02012340, 0x4F560012)}
p = O.Plan(1336, 3, ["digital"], 0)
r = O.rom(p, k3, szs, {"digital": dig}, res, 0x01C06208)
magic, size, n, nslot, slots, slot_len = O.HEAD.unpack_from(r)
ents = [O.ENT.unpack_from(r, O.HEAD.size + i * O.ENT.size) for i in range(n)]
check("rom: the head (magic, size, entries, slots, slot 0's address, a slot's bytes)",
      (magic, size, n, nslot, slots, slot_len) == (O.MAGIC, len(r), 3, 3, 0x01C06208, 1336)
      and size == O.rom_size(p, szs, k3))
check("rom: resident ANALOG in RAM, STRINGS in XIP, at their tables' addresses, the slot from the tag",
      ents[0] == (0, O.KIND_RAM, 5160, 0x01C04E08) and ents[2] == (0x12, O.KIND_XIP, 1462, 0x02012340))
e = ents[1]
at = e[3]
check("rom: DIGITAL in the slots: a copy a slot, each rounded to 4, slot k's at + k x the stride",
      e[:3] == (1, O.KIND_SLOT, 1334) and r[at:at + 1334] == dig[0] and r[at + 1336:at + 1336 + 1334] == dig[1]
      and r[at + 2 * 1336:at + 2 * 1336 + 1334] == dig[2])
check("rom: a copy of the wrong length refused", _raises(O.rom, p, k3, szs, {"digital": [dig[0][:-2]] * 3}, res, 0))
check("rom: a hooks' table without OVL_TAG refused",
      _raises(O.rom, p, k3, szs, {"digital": [b"\0" * 1334] * 3}, res, 0))
print("ovl_link test", "FAILED" if bad else "passed")
sys.exit(1 if bad else 0)

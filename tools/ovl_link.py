# SPDX-License-Identifier: GPL-3.0-only
"""The RAMTEXT overlays' link (FELUCCA_OVERLAY; docs/SD-MODE-DESIGN.md 6, firmware/src/core/overlay.c).

Each engine's hot code is a section of its own (core.h OVL / OVLX: .ovl.NAME / .ovx.NAME, clang adds .FUNCTION with
-ffunction-sections; its hooks' table .ovh.NAME). tools/build.py links the firmware once per part's slot: the engines
put in the slots are one GNU ld OVERLAY at slot k's address in link k, nothing else moves, so each engine's code runs
from any slot as linked. The resident image must come out the same in every link (nothing resident names an overlay's
code; build.py compares). The table at _ovl_rom (XIP, after the far probe's .text_far: the big layout only) tells
the firmware where each engine's code is.

  kinds(objdump_h)                -> {name: "ovl" | "ovx"} (the engines with a hooks' table)
  plan(sizes, kinds, nslot, force) -> Plan: the slot size and the engines in the slots (the least RAMTEXT)
  script(ld_text, kinds, plan, k, rom_size, probe=False) -> the linker script of slot k's link
  rom(plan, kind, sizes, blobs, resident, slots_at) -> the table's bytes
"""
import re
import struct
from dataclasses import dataclass, field

MAGIC = 0x314C564F                  # "OVL1" (overlay.c OVL_MAGIC)
KIND_RAM, KIND_XIP, KIND_SLOT = 0, 1, 2
HEAD = struct.Struct("<IIHHII")     # magic, size, n, nslot, slots, slot_len (overlay.c ovl_rom_t)
ENT = struct.Struct("<BBHI")        # eng, kind, len, at (ovl_ent_t)
TAG_HI = 0x4F56                     # core.h OVL_TAG: "OV" << 16 | the engine's slot
PROBE_LMA = 0x10080000              # the slots' load addresses: in the dummy LOAD region, past the RAM copies'
NAME = re.compile(r"^\s*\d+\s+\.(ovl|ovx|ovh)\.([A-Za-z0-9_]+)(?:\.\S*)?\s+([0-9a-fA-F]+)\s", re.M)


@dataclass
class Plan:
    slot_len: int                   # a slot's bytes (0: no slots, every engine where it runs without them)
    nslot: int
    slots: list = field(default_factory=list)   # the engines in the slots, sorted
    ramtext: int = 0                # the engines' RAMTEXT: the slots + the resident OVL engines


def kinds(objdump_h):
    """objdump -h of the objects -> {name: "ovl" | "ovx"}; raises ValueError for code without a hooks' table, a table
    without code, or an engine with both kinds"""
    code, hdr = {}, set()
    for kind, name, _size in NAME.findall(objdump_h):
        if kind == "ovh":
            hdr.add(name)
        elif code.setdefault(name, kind) != kind:
            raise ValueError(f"overlay {name}: both OVL and OVLX code (one kind an engine)")
    if set(code) != hdr:
        raise ValueError(f"overlay code and hooks' tables disagree: code {sorted(code)}, tables {sorted(hdr)}")
    return code


def plan(sizes, kind, nslot, force=None):
    """sizes {name: bytes in a slot} -> the slot size with the least RAMTEXT for the engines: nslot x the slot, plus
    every OVL engine larger than a slot (resident in RAMTEXT; an OVLX one larger stays in XIP: none). A tie: the larger
    slot (more engines run from RAM). force: the slot's bytes (OPTIMIST_OVL_SLOT, a test build), 0 no slots"""
    def cost(s):
        return nslot * s + sum(n for name, n in sizes.items() if kind[name] == "ovl" and n > s)
    if force is not None:
        s = (int(force) + 3) & ~3
    else:
        s = min(sorted({0, *sizes.values()}), key=lambda c: (cost(c), -c))
    slots = sorted(name for name, n in sizes.items() if s and n <= s)
    s = (max(sizes[n] for n in slots) + 3) & ~3 if slots else 0     # (a forced slot: no larger than its engines)
    return Plan(s, nslot, slots, cost(s))


def _keep(name, kind, hdr=True):
    k = f"KEEP(*(.ovh.{name} .ovh.{name}.*)) " if hdr else ""
    return f"{k}*(.{kind}.{name} .{kind}.{name}.*) . = ALIGN(4);"


def script(ld_text, kind, p, k, rom_size, probe=False):
    """app.ld's text (the layout's edits made) -> slot k's link: the resident engines into .ram_hot (OVL) or .text
    (OVLX), the slots' engines one OVERLAY at slot k (probe: every engine there, its size measured; no table)"""
    inslot = sorted(kind) if probe else p.slots
    res = [n for n in sorted(kind) if n not in inslot]
    xip = " ".join(_keep(n, "ovx") for n in res if kind[n] == "ovx")
    ram = " ".join(_keep(n, "ovl") for n in res if kind[n] == "ovl")
    out = ld_text
    for old, new in (("_etext = .; } > XIP", f"{xip} . = ALIGN(4); _etext = .; }} > XIP"),
                     ("KEEP(*(.ram_hot .ram_hot.*)) . = ALIGN(4); _rh_end = .;",
                      f"KEEP(*(.ram_hot .ram_hot.*)) . = ALIGN(4); {ram} . = ALIGN(4); _rh_end = .;"),
                     (".text_far .text_far.*)) } > XIP",
                      ".text_far .text_far.*)) } > XIP\n    _ovl_rom = ALIGN(ADDR(.text_far) + SIZEOF(.text_far), 4);"
                      f" _ovl_rom_end = _ovl_rom + {rom_size:#x};\n"
                      '    ASSERT(_ovl_rom_end <= ORIGIN(XIP) + LENGTH(XIP), "the overlay table does not fit the app slot")')):
        if out.count(old) != 1:
            raise ValueError(f"app.ld: {old!r} not found once")
        out = out.replace(old, new)
    span = 0 if probe else p.nslot * p.slot_len
    slots = [f"_ovl_slots = ALIGN(_rh_end, 4); _ovl_slots_end = _ovl_slots + {span:#x};"]
    if inslot:
        at = "_ovl_slots" if probe else f"_ovl_slots + {k * p.slot_len:#x}"
        body = "\n".join(f"        .ovl_{n} {{ {_keep(n, kind[n])} }}" for n in inslot)
        slots.append(f"    OVERLAY {at} : NOCROSSREFS AT ({PROBE_LMA:#x})\n    {{\n{body}\n    }}")
    if not probe:
        slots.append('    ASSERT(_ovl_slots_end <= ORIGIN(SRAM) + LENGTH(SRAM), "the overlay slots do not fit SRAM")')
    anchor = "_rh_load = LOADADDR(.ram_hot);"
    if out.count(anchor) != 1:
        raise ValueError(f"app.ld: {anchor!r} not found once")
    return out.replace(anchor, anchor + "\n    " + "\n".join(slots))


def rom_size(p, sizes, kind):
    """the table's bytes: its head, an entry an engine, the slots' copies (each rounded to 4, once per slot)"""
    return HEAD.size + ENT.size * len(kind) + sum(p.nslot * ((sizes[n] + 3) & ~3) for n in p.slots)


def tag_slot(word, name):
    """a hooks' table's first word -> the engine's slot (ENGINES[] index)"""
    if word >> 16 != TAG_HI:
        raise ValueError(f"overlay {name}: its hooks' table starts {word:#010x}, not OVL_TAG")
    return word & 0xFF


def rom(p, kind, sizes, blobs, resident, slots_at):
    """-> the table's bytes. blobs {name: [slot 0's code, slot 1's, ...]}; resident {name: (address, tag word)};
    slots_at: slot 0's address"""
    ents, data = [], b""
    at = HEAD.size + ENT.size * len(kind)
    for n in sorted(kind):
        if n in p.slots:
            copies = blobs[n]
            if len(copies) != p.nslot or any(len(c) != sizes[n] for c in copies):
                raise ValueError(f"overlay {n}: {len(copies)} copies of {[len(c) for c in copies]} B, not "
                                 f"{p.nslot} of {sizes[n]} B")
            stride = (sizes[n] + 3) & ~3
            ents.append(ENT.pack(tag_slot(struct.unpack_from("<I", copies[0])[0], n), KIND_SLOT, sizes[n], at + len(data)))
            data += b"".join(c + b"\xff" * (stride - len(c)) for c in copies)
        else:
            addr, word = resident[n]
            ents.append(ENT.pack(tag_slot(word, n), KIND_RAM if kind[n] == "ovl" else KIND_XIP, sizes[n], addr))
    total = HEAD.size + len(b"".join(ents)) + len(data)
    return HEAD.pack(MAGIC, total, len(kind), p.nslot, slots_at, p.slot_len) + b"".join(ents) + data

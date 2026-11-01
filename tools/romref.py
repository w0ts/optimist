# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The check that nothing calls or reads the chip's ROM (0xFFC00000..0xFFCFFFFF), over the image's disassembly
(objdump's pi32v2 form; the same module as fm1-base's tools/romref.py). A direct call into the window is a reference. A constant in
the window is one only when the code uses it as an address: loaded into a register that one of the next USE_WINDOW
instructions of the same function reads or writes through ('[rN', 'call rN', 'goto rN') before writing the register
again. A constant kept as a number is not: 2026-10-29, pi32v2-llvm keeps -3,230,250 (0xFFCEB5D6) in a register for a
range check, which the old check (any such constant) refused. skip: functions not checked (fm1-base: fm1.toml rom_check_skip)."""
import re

LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")
LABEL = re.compile(r"^([A-Za-z_.$][\w.$]*):$")
CALL = re.compile(r"\bcall -?\d+ <[^:>]*: ([0-9a-f]+) >")
LOAD = re.compile(r"^\s*(r\d+) = (-?\d+) <")
ROM_LO, ROM_HI = 0xFFC00000, 0xFFD00000
USE_WINDOW = 4


def in_rom(val):
    return ROM_LO <= val & 0xFFFFFFFF < ROM_HI


def used_as_address(reg, after):
    """reg is read or written through in the instructions `after` (a function's next ones), before being set again"""
    through = re.compile(r"\[" + reg + r"\b|\b(?:call|goto) " + reg + r"\b")
    sets = re.compile(r"^\s*" + reg + r"\s*=[^=]")
    for text in after[:USE_WINDOW]:
        if through.search(text):
            return True
        if sets.search(text):
            return False
    return False


def rom_refs(dis, skip=()):
    """-> the ROM addresses the code calls or uses as an address, in order (skip: function names not checked)"""
    fns, fn = [], None
    for ln in dis.splitlines():
        m = LINE.match(ln)
        if m and fn is not None:
            fn[1].append(m.group(3))
        elif not m:
            lab = LABEL.match(ln.strip())
            if lab:
                fn = (lab.group(1), [])
                fns.append(fn)
    out = []
    for name, texts in fns:
        if name in skip:
            continue
        for i, text in enumerate(texts):
            for a in CALL.findall(text):
                if in_rom(int(a, 16)):
                    out.append(int(a, 16))
            m = LOAD.match(text)
            if m and in_rom(int(m.group(2))) and used_as_address(m.group(1), texts[i + 1:]):
                out.append(int(m.group(2)) & 0xFFFFFFFF)
    return out

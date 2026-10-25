# SPDX-License-Identifier: GPL-3.0-only
"""The SD board's schematic (sd-board.kicad_sch), from KiCad's own symbols, with the nets of make_board.py: every pin
carries a net label (no wires to draw), so the drawing reads as the netlist and KiCad's ERC checks it.

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 make_schematic.py
"""
import os
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
SYMS = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"
NS = uuid.UUID("6f1c2a4e-5d3b-4e8a-9c7f-2b1e0d4a6c11")   # (fixed: the file is the same each run)
ROOT = str(uuid.uuid5(NS, "root"))


def uid(*key):
    return str(uuid.uuid5(NS, "/".join(map(str, key))))


def block(text, name):
    """the top-level (symbol "name" ...) of a .kicad_sym, balanced by parentheses"""
    i = text.index(f'(symbol "{name}"')
    depth = 0
    for j in range(i, len(text)):
        depth += text[j] == "("
        depth -= text[j] == ")"
        if depth == 0:
            return text[i:j + 1]
    raise ValueError(name)


LIBS = {"Device": ["R", "C"], "74xGxx": ["74LVC1G04"], "Connector": ["Micro_SD_Card_Det1"],
        "Connector_Generic_MountingPin": ["Conn_01x10_MountingPin"], "power": ["PWR_FLAG"]}
lib_symbols = []
for lib, names in LIBS.items():
    text = open(os.path.join(SYMS, f"{lib}.kicad_sym")).read()
    for n in names:
        b = block(text, n)
        lib_symbols.append(b.replace(f'(symbol "{n}"', f'(symbol "{lib}:{n}"', 1))

# pin connection points in the symbol (y up) and their direction (where the pin points from that point)
PINS = {
    "Device:R": {"1": (0, 3.81, 270), "2": (0, -3.81, 90)},
    "Device:C": {"1": (0, 3.81, 270), "2": (0, -3.81, 90)},
    "74xGxx:74LVC1G04": {"1": (-7.62, 2.54, 0), "2": (-15.24, 0, 0), "3": (-5.08, -10.16, 90), "4": (12.7, 0, 180),
                         "5": (-5.08, 10.16, 270)},
    "Connector:Micro_SD_Card_Det1": {"1": (-22.86, 10.16, 0), "2": (-22.86, 7.62, 0), "3": (-22.86, 5.08, 0),
                                     "4": (-22.86, 2.54, 0), "5": (-22.86, 0, 0), "6": (-22.86, -2.54, 0),
                                     "7": (-22.86, -5.08, 0), "8": (-22.86, -7.62, 0), "9": (-22.86, -12.7, 0),
                                     "SH": (20.32, -12.7, 180)},
    "Connector_Generic_MountingPin:Conn_01x10_MountingPin": {**{str(n): (-5.08, 10.16 - 2.54 * (n - 1), 0)
                                                              for n in range(1, 11)}, "MP": (0, -17.78, 90)},
    "power:PWR_FLAG": {"1": (0, 0, 90)},
}
LABEL_ANGLE = {0: 180, 180: 0, 90: 270, 270: 90}   # (the label away from the body)

LCD10 = {"1": "GND", "2": "MISO", "3": "CLK", "4": "MOSI", "5": "LCD_CS", "6": "3V3", "7": "LCD_RST", "8": "LCD_LEDA",
         "9": "LCD_LEDK", "10": "GND", "MP": "GND"}   # (as LCD_PINS in make_board.py: PROVISIONAL)
# (ref, lib_id, value, footprint, LCSC, x, y, {pin: net})
PARTS = [
    ("J1", "Connector:Micro_SD_Card_Det1", "TF-01A push-push", "sdboard:TF-SMD_TF-01A", "C91145", 170, 90,
     {"1": "DAT2", "2": "CARD_CS", "3": "MOSI", "4": "3V3", "5": "CLK", "6": "GND", "7": "DAT0", "8": "DAT1",
      "SH": "GND"}),
    ("U1", "74xGxx:74LVC1G04", "74LVC1G04", "sdboard:SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "C7827", 70, 70,
     {"2": "LCD_CS", "3": "GND", "4": "CARD_CS", "5": "3V3"}),
    ("R1", "Device:R", "220", "sdboard:R0603", "C22962", 110, 125, {"1": "MISO", "2": "DAT0"}),
    ("R2", "Device:R", "47k", "sdboard:R0603", "C25819", 125, 125, {"1": "3V3", "2": "DAT2"}),
    ("R3", "Device:R", "47k", "sdboard:R0603", "C25819", 140, 125, {"1": "3V3", "2": "DAT1"}),
    ("R4", "Device:R", "47k", "sdboard:R0603", "C25819", 45, 70, {"1": "LCD_CS", "2": "3V3"}),
    ("C1", "Device:C", "10uF", "sdboard:C0805", "C15850", 60, 125, {"1": "3V3", "2": "GND"}),
    ("C2", "Device:C", "100nF", "sdboard:C0603", "C14663", 75, 125, {"1": "GND", "2": "3V3"}),
    # (v5: J2 from J12, J3 the LCD's cable, every line through; the pin order PROVISIONAL until J12 is measured)
    ("J2", "Connector_Generic_MountingPin:Conn_01x10_MountingPin", "FPC 10P 0.5mm (IN)",
     "sdboard:FPC-SMD_10P-P0.50_HC-FPC-05-09-10RLTAG", "C5213750", 230, 90, LCD10),
    ("J3", "Connector_Generic_MountingPin:Conn_01x10_MountingPin", "FPC 10P 0.5mm (LCD)",
     "sdboard:FPC-SMD_10P-P0.50_HC-FPC-05-09-10RLTAG", "C5213750", 260, 90, LCD10),
    ("#FLG01", "power:PWR_FLAG", "PWR_FLAG", "", "", 40, 150, {"1": "3V3"}),
    ("#FLG02", "power:PWR_FLAG", "PWR_FLAG", "", "", 55, 150, {"1": "GND"}),
]


def prop(name, value, x, y, hide=False):
    h = " (hide yes)" if hide else ""
    return (f'(property "{name}" "{value}" (at {x} {y} 0) (effects (font (size 1.27 1.27)){h}))')


out = ['(kicad_sch (version 20250114) (generator "make_schematic.py") (generator_version "1.0")',
       f'(uuid "{ROOT}") (paper "A4")',
       '(title_block (title "FM-1 SD board") (comment 1 "docs/SD-CARD-DESIGN.md; hardware/sd-board/README.md"))',
       "(lib_symbols", *lib_symbols, ")"]
for ref, lib, value, fp, lcsc, x, y, pins in PARTS:
    x, y = round(round(x / 2.54) * 2.54, 2), round(round(y / 2.54) * 2.54, 2)   # (on KiCad's connection grid)
    pins_txt = " ".join(f'(pin "{p}" (uuid "{uid(ref, "pin", p)}"))' for p in PINS[lib])
    flag = ref.startswith("#")
    out.append(f'(symbol (lib_id "{lib}") (at {x} {y} 0) (unit 1) (exclude_from_sim no) (in_bom {"no" if flag or ref.startswith("H") else "yes"})'
               f' (on_board {"no" if flag else "yes"}) (dnp no) (uuid "{uid(ref)}")'
               f' {prop("Reference", ref, x, y - 14, flag)} {prop("Value", value, x, y + 14, flag)}'
               f' {prop("Footprint", fp, x, y, True)} {prop("Datasheet", "", x, y, True)} {prop("LCSC", lcsc, x, y, True)}'
               f' {pins_txt} (instances (project "sd-board" (path "/{ROOT}" (reference "{ref}") (unit 1)))))')
    for p, net in pins.items():
        px, py, ang = PINS[lib][p]
        lx, ly = round(x + px, 2), round(y - py, 2)
        if flag:                                  # (a flag on the net: a label at its pin)
            out.append(f'(label "{net}" (at {lx} {ly} 0) (effects (font (size 1.27 1.27)) (justify left bottom))'
                       f' (uuid "{uid(ref, "lbl", p)}"))')
            continue
        a = LABEL_ANGLE[ang]
        just = "right" if a == 180 else "left" if a == 0 else "left"
        out.append(f'(label "{net}" (at {lx} {ly} {a}) (effects (font (size 1.27 1.27)) (justify {just} bottom))'
                   f' (uuid "{uid(ref, "lbl", p)}"))')
    for p in PINS[lib]:                           # (the pins on no net: marked, so the ERC does not ask)
        if p not in pins and not flag:
            px, py, _ = PINS[lib][p]
            out.append(f'(no_connect (at {round(x + px, 2)} {round(y - py, 2)}) (uuid "{uid(ref, "nc", p)}"))')
out.append(f'(sheet_instances (path "/" (page "1")))')
out.append(")")
path = os.path.join(HERE, "sd-board.kicad_sch")
open(path, "w").write("\n".join(out) + "\n")
print("saved", path)

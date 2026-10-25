# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 SD board (docs/SD-CARD-DESIGN.md): a push-push microSD socket, the chip-select inverter, the series and
pull-up resistors, the decoupling, and seven 2.54 mm pads for the wires to the FM-1. Generates sd-board.kicad_pcb with
KiCad's own Python (pcbnew), so the board is reviewed as code:

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 make_board.py

Then make_fab.sh checks it (DRC) and writes the JLCPCB files (Gerbers, drill, BOM, placement).

Nets (microSD pins: 1 DAT2, 2 DAT3/CS, 3 CMD, 4 VDD, 5 CLK, 6 VSS, 7 DAT0, 8 DAT1, 9 card detect, 10-13 shell):
  LCD_CS   pad 1 -> U1 A (74LVC1G04 in); R4 47k pull-up (the line floats while the FM-1 resets)
  CARD_CS  U1 Y  -> J1 2
  MOSI     pad 3 -> J1 3 (CMD)
  3V3      pad 4 -> J1 4, U1 VCC, C1 10 uF, C2 100 nF, the pull-ups
  CLK      pad 5 -> J1 5
  GND      pad 6 -> J1 6, the shell, U1 GND
  MISO     pad 7 -> R1 1k -> J1 7 (DAT0): the FM-1's PC8 is the LCD's D/C too (the 1k takes any overlap)
  DAT1/DAT2  47k pull-ups (unused in SPI mode, not left floating)
"""
import os

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "lib", "sdboard.pretty")
KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
MM = pcbnew.FromMM

X0, Y0, W, H = 98.0, 100.0, 24.0, 30.0           # the board outline (mm): the card's opening on the top edge
TRACK, POWER = 0.25, 0.4

board = pcbnew.BOARD()
nets = {}


def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]


def place(lib, name, ref, value, x, y, rot=0.0, lcsc=""):
    fp = pcbnew.FootprintLoad(lib, name)
    fp.SetFPID(pcbnew.LIB_ID("sdboard" if lib == LIB else os.path.basename(lib)[:-len(".pretty")], name))
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    fp.SetOrientationDegrees(rot)
    if lcsc:
        fp.SetField("LCSC", lcsc)
    fp.Reference().SetVisible(False)            # (JLC places by the placement file; the labels only crowd the silk)
    fp.Value().SetVisible(False)
    for f in fp.GetFields():
        if f.GetName() == "LCSC":
            f.SetVisible(False)
    board.Add(fp)
    return fp


def pad(fp, num):
    for p in fp.Pads():
        if p.GetNumber() == str(num):
            return p
    raise KeyError(f"{fp.GetReference()} has no pad {num}")


def at(fp, num):
    p = pad(fp, num).GetPosition()
    return pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)


def connect(fp, num, name):
    pad(fp, num).SetNet(net(name))


def route(name, pts, width=TRACK, layer=pcbnew.F_Cu):
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(MM(ax), MM(ay)))
        t.SetEnd(pcbnew.VECTOR2I(MM(bx), MM(by)))
        t.SetWidth(MM(width))
        t.SetLayer(layer)
        t.SetNet(net(name))
        board.Add(t)


def via(name, x, y):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    v.SetDrill(MM(0.3))
    v.SetWidth(MM(0.6))
    v.SetNet(net(name))
    board.Add(v)


# ---- the parts
J1 = place(LIB, "TF-SMD_TF-01A", "J1", "TF-01A push-push", 110.0, 109.6, 180, "C91145")
U1 = place(LIB, "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "U1", "74LVC1G04", 104.0, 120.6, 180, "C7827")
R1 = place(LIB, "R0603", "R1", "1k", 116.0, 118.5, 90, "C21190")
R2 = place(LIB, "R0603", "R2", "47k", 106.6, 117.0, 90, "C25819")
R3 = place(LIB, "R0603", "R3", "47k", 117.6, 117.0, 90, "C25819")
R4 = place(LIB, "R0603", "R4", "47k", 100.6, 118.6, 90, "C25819")
C1 = place(LIB, "C0805", "C1", "10uF", 120.0, 121.5, 90, "C15850")
C2 = place(LIB, "C0603", "C2", "100nF", 103.9, 118.2, 0, "C14663")
P1 = place(os.path.join(KICAD_FP, "Connector_PinHeader_2.54mm.pretty"), "PinHeader_1x07_P2.54mm_Vertical", "P1",
           "to the FM-1", 103.65, 127.5, 90)

# ---- the nets
for num, name in ((1, "DAT2"), (2, "CARD_CS"), (3, "MOSI"), (4, "3V3"), (5, "CLK"), (6, "GND"), (7, "DAT0"),
                  (8, "DAT1"), (10, "GND"), (11, "GND"), (12, "GND"), (13, "GND")):
    connect(J1, num, name)
for num, name in ((2, "LCD_CS"), (3, "GND"), (4, "CARD_CS"), (5, "3V3")):
    connect(U1, num, name)
# (pad 1 / pad 2; turned 90 degrees a resistor's pad 2 is the upper one: the end nearer the socket)
for fp, a, b in ((R1, "MISO", "DAT0"), (R2, "3V3", "DAT2"), (R3, "3V3", "DAT1"), (R4, "LCD_CS", "3V3"),
                 (C1, "3V3", "GND"), (C2, "GND", "3V3")):
    connect(fp, 1, a)
    connect(fp, 2, b)
for num, name in ((1, "LCD_CS"), (3, "MOSI"), (4, "3V3"), (5, "CLK"), (6, "GND"), (7, "MISO")):
    connect(P1, num, name)


# ---- the copper, top (signals, straight down in the socket's order: no crossing)
route("LCD_CS", [at(P1, 1), (103.65, 125.0), (100.6, 121.95), (100.6, 120.6), at(U1, 2)])
route("LCD_CS", [(100.6, 120.6), at(R4, 1)])
route("CARD_CS", [at(J1, 2), (108.86, 118.4), (105.71, 121.55), at(U1, 4)])
route("DAT2", [at(J1, 1), (107.76, 115.6), at(R2, 2)])
route("MOSI", [at(J1, 3), (109.96, 124.5), (108.73, 125.73), at(P1, 3)])
route("3V3", [at(J1, 4), (111.06, 125.0), (111.27, 125.21), at(P1, 4)], POWER)
route("CLK", [at(J1, 5), (112.16, 124.0), (113.81, 125.65), at(P1, 5)])
route("GND", [at(J1, 6), (113.26, 117.2)])
route("DAT0", [at(J1, 7), (114.36, 116.4), (115.71, 117.75), at(R1, 2)])
route("MISO", [at(R1, 1), (116.0, 124.61), at(P1, 7)])
route("DAT1", [at(J1, 8), (115.46, 115.6), (116.11, 116.25), at(R3, 2)])
route("3V3", [at(U1, 5), (104.6, 118.9), at(C2, 2)])
route("3V3", [at(U1, 5), (106.0, 119.0)])
route("3V3", [at(R2, 1), (105.7, 117.35)])
route("3V3", [at(R3, 1), (118.5, 118.4)])
route("3V3", [at(R4, 2), (100.6, 116.9)])
route("3V3", [at(C1, 1), (120.0, 123.8)])
route("GND", [at(C2, 1), (103.2, 117.2)])
route("GND", [at(U1, 3), (103.5, 122.4)])
route("GND", [at(C1, 2), (121.2, 120.5)])

# ---- the vias and the 3V3 bus on the bottom (the side parts' supply under the signal traces)
for x, y in ((100.6, 116.9), (105.7, 117.35), (106.0, 119.0), (111.06, 116.6), (118.5, 118.4), (120.0, 123.8)):
    via("3V3", x, y)
for x, y in ((113.26, 117.2), (103.2, 117.2), (103.5, 122.4), (121.2, 120.5)):
    via("GND", x, y)
B = pcbnew.B_Cu
route("3V3", [(100.6, 116.9), (101.3, 116.2), (104.55, 116.2), (105.7, 117.35)], POWER, B)
route("3V3", [(105.7, 117.35), (106.0, 117.65), (106.0, 119.0)], POWER, B)
route("3V3", [(105.7, 117.35), (106.45, 116.6), (111.06, 116.6)], POWER, B)
route("3V3", [(111.06, 116.6), (111.66, 116.0), (117.9, 116.0), (118.5, 116.6), (118.5, 118.4)], POWER, B)
route("3V3", [(118.5, 118.4), (118.5, 122.3), (120.0, 123.8)], POWER, B)

# ---- the outline, the ground pours, the labels
def edge(ax, ay, bx, by):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(MM(ax), MM(ay)))
    s.SetEnd(pcbnew.VECTOR2I(MM(bx), MM(by)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)


for a, b in (((X0, Y0), (X0 + W, Y0)), ((X0 + W, Y0), (X0 + W, Y0 + H)), ((X0 + W, Y0 + H), (X0, Y0 + H)),
             ((X0, Y0 + H), (X0, Y0))):
    edge(*a, *b)
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net("GND"))
    z.SetLocalClearance(MM(0.25))
    z.SetMinThickness(MM(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    o = z.Outline()
    o.NewOutline()
    for x, y in ((X0 + 0.3, Y0 + 0.3), (X0 + W - 0.3, Y0 + 0.3), (X0 + W - 0.3, Y0 + H - 0.3), (X0 + 0.3, Y0 + H - 0.3)):
        o.Append(MM(x), MM(y))
    board.Add(z)


def label(text, x, y, size=0.8, rot=0.0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetTextAngleDegrees(rot)
    t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(0.12))
    board.Add(t)


for i, name in enumerate(("CS", "", "MOSI", "3V3", "CLK", "GND", "MISO")):
    if name:                                    # (upright, beside each pad: four letters fit the 2.54 mm pitch so)
        label(name, 103.65 + 2.54 * i, 124.6, 0.8, 90)
label("OPTIMIST SD", 110.0, 129.2, 0.8)

board.GetDesignSettings().SetAuxOrigin(pcbnew.VECTOR2I(MM(X0), MM(Y0 + H)))   # (the lower left: the Gerbers, the drill
pcbnew.ZONE_FILLER(board).Fill(board.Zones())                                   # files and cpl.csv share it)
out = os.path.join(HERE, "sd-board.kicad_pcb")
board.Save(out)
print("saved", out)

# ---- JLCPCB's assembly files: the parts list (BOM) and the placement (CPL); P1 is not assembled (the wires go in it)
FAB = os.path.join(HERE, "fab")
os.makedirs(FAB, exist_ok=True)
parts = [fp for fp in board.GetFootprints() if fp.GetReference() != "P1"]
groups = {}
for fp in parts:
    lcsc = fp.GetFieldText("LCSC")
    groups.setdefault((fp.GetValue(), fp.GetFPID().GetLibItemName().wx_str(), lcsc), []).append(fp.GetReference())
with open(os.path.join(FAB, "bom.csv"), "w") as f:
    f.write("Comment,Designator,Footprint,LCSC Part #\n")
    for (value, footprint, lcsc), refs in sorted(groups.items(), key=lambda g: sorted(g[1])[0]):
        f.write(f'"{value}","{",".join(sorted(refs))}","{footprint}","{lcsc}"\n')
with open(os.path.join(FAB, "cpl.csv"), "w") as f:
    f.write("Designator,Mid X,Mid Y,Layer,Rotation\n")
    for fp in sorted(parts, key=lambda fp: fp.GetReference()):
        x, y = pcbnew.ToMM(fp.GetPosition().x) - X0, Y0 + H - pcbnew.ToMM(fp.GetPosition().y)   # (from the
        f.write(f"{fp.GetReference()},{x:.3f}mm,{y:.3f}mm,Top,{fp.GetOrientationDegrees() % 360:.0f}\n")  # lower left)
print("wrote", os.path.join(FAB, "bom.csv"), os.path.join(FAB, "cpl.csv"))

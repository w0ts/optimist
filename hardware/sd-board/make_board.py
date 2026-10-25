# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 SD board (docs/SD-CARD-DESIGN.md), v2: a push-push microSD socket, the chip-select inverter, the series and
pull-up resistors, the decoupling, and a 6-pin 0.5 mm FFC socket: a flat cable from the tap at J12 (the LCD's
connector) reaches it around the FM-1 board's edge, no soldering (the user, 2026-10-25). 20 x 45.5 mm for the 20 x 60
mm free on the FM-1's back above the battery; the card's socket set back 5.5 mm so two holes fit beside its opening,
two more beside the FFC socket. Generates sd-board.kicad_pcb with KiCad's own Python (pcbnew):

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 make_board.py

Then make_fab.sh checks it (DRC, the schematic's nets) and writes the JLCPCB files (Gerbers, drill, BOM, placement).

Nets (microSD pins: 1 DAT2, 2 DAT3/CS, 3 CMD, 4 VDD, 5 CLK, 6 VSS, 7 DAT0, 8 DAT1, 9 card detect, 10-13 shell;
FFC J2 pins 1..6, 7/8 its tabs):
  LCD_CS   J2 1 -> U1 A (74LVC1G04 in); R4 47k pull-up (the line floats while the FM-1 resets)
  CARD_CS  U1 Y  -> J1 2
  MOSI     J2 2 -> J1 3 (CMD)
  3V3      J2 3 -> J1 4, U1 VCC, C1 10 uF, C2 100 nF, the pull-ups
  CLK      J2 4 -> J1 5
  GND      J2 5 -> J1 6, the shell, U1 GND, J2's tabs
  MISO     J2 6 -> R1 1k -> J1 7 (DAT0): the FM-1's PC8 is the LCD's D/C too (the 1k takes any overlap)
  DAT1/DAT2  47k pull-ups (unused in SPI mode, not left floating)
"""
import os
import os

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "lib", "sdboard.pretty")
KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
MM = pcbnew.FromMM

X0, Y0, W, H = 100.0, 94.5, 20.0, 45.5           # the board outline (mm): the card's end on top, the FFC's below
TRACK, POWER = 0.2, 0.3                          # (0.2 / 0.3: the FFC's 0.5 mm pitch)

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
J1 = place(LIB, "TF-SMD_TF-01A", "J1", "TF-01A push-push", 110.0, 109.6, 180, "C91145")   # (its opening at y 100)
J2 = place(LIB, "FPC-SMD_6P-P0.50_XUNPU_FPC-05F-6PH20", "J2", "FFC 6P 0.5mm", 110.0, 136.09, 0, "C2856796")
U1 = place(LIB, "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "U1", "74LVC1G04", 103.5, 120.5, 180, "C7827")
R1 = place(LIB, "R0603", "R1", "1k", 115.8, 119.0, 90, "C21190")
R2 = place(LIB, "R0603", "R2", "47k", 106.6, 117.6, 90, "C25819")
R3 = place(LIB, "R0603", "R3", "47k", 117.4, 117.6, 90, "C25819")
R4 = place(LIB, "R0603", "R4", "47k", 103.4, 127.6, 0, "C25819")
C1 = place(LIB, "C0805", "C1", "10uF", 118.6, 122.5, 90, "C15850")
C2 = place(LIB, "C0603", "C2", "100nF", 104.1, 117.6, 0, "C14663")
# four holes to fix it (an M2 screw, a zip tie, a rivet: the user, 2026-10-25: "to easily attach it to something"): two
# beside the card's opening (the card, 11 mm wide, passes between their screw heads), two beside the FFC socket
HOLES = [place(os.path.join(KICAD_FP, "MountingHole.pretty"), "MountingHole_2.2mm_M2", f"H{i + 1}", "M2", x, y)
         for i, (x, y) in enumerate(((102.5, 97.3), (117.5, 97.3), (102.5, 137.4), (117.5, 137.4)))]

# ---- the nets (pad 1 / pad 2; turned 90 degrees a resistor's pad 2 is the upper one)
for num, name in ((1, "DAT2"), (2, "CARD_CS"), (3, "MOSI"), (4, "3V3"), (5, "CLK"), (6, "GND"), (7, "DAT0"),
                  (8, "DAT1"), (10, "GND"), (11, "GND"), (12, "GND"), (13, "GND")):
    connect(J1, num, name)
for num, name in ((1, "LCD_CS"), (2, "MOSI"), (3, "3V3"), (4, "CLK"), (5, "GND"), (6, "MISO"), (7, "GND"), (8, "GND")):
    connect(J2, num, name)
for num, name in ((2, "LCD_CS"), (3, "GND"), (4, "CARD_CS"), (5, "3V3")):
    connect(U1, num, name)
for fp, a, b in ((R1, "MISO", "DAT0"), (R2, "3V3", "DAT2"), (R3, "3V3", "DAT1"), (R4, "LCD_CS", "3V3"),
                 (C1, "3V3", "GND"), (C2, "GND", "3V3")):
    connect(fp, 1, a)
    connect(fp, 2, b)

# ---- the copper, top: the socket's contacts (1.1 mm apart) straight down, then each steps over to the FFC's
# 0.5 mm pitch at its own height (one at a time: no two diagonals side by side at 0.5 mm)
P = {n: at(J2, n) for n in range(1, 7)}
TOP = P[1][1] - 0.625                               # (the FFC pads' upper end)
route("MOSI", [at(J1, 3), (109.96, 129.5), (P[2][0], 129.5), P[2]])
route("3V3", [at(J1, 4), (111.06, 130.3), (P[3][0], 130.3), P[3]], POWER)
route("CLK", [at(J1, 5), (112.16, 131.1), (P[4][0], 131.1), P[4]])
route("GND", [at(J1, 6), (113.26, 131.9), (P[5][0], 131.9), P[5]])
route("DAT0", [at(J1, 7), (114.36, 116.6), (115.8, 118.04), at(R1, 2)])
route("MISO", [at(R1, 1), (115.8, 132.7), (P[6][0], 132.7), P[6]])
route("LCD_CS", [P[1], (P[1][0], 129.0), (105.25, 126.0), (101.0, 126.0), (101.0, 120.5), at(U1, 2)])
route("LCD_CS", [at(R4, 1), (102.65, 126.0)])
route("CARD_CS", [at(J1, 2), (108.86, 118.6), (105.95, 121.51), at(U1, 4)])
route("DAT2", [at(J1, 1), (107.76, 116.2), (107.11, 116.85), at(R2, 2)])
route("DAT1", [at(J1, 8), (115.46, 115.9), (116.41, 116.85), at(R3, 2)])
route("3V3", [at(C2, 2), at(U1, 5)], POWER)
route("3V3", [at(R2, 1), (105.6, 118.35), (104.8, 118.35)], POWER)
route("3V3", [at(R3, 1), (117.4, 128.0)], POWER)
route("3V3", [at(C1, 1), (118.6, 124.6), (117.4, 124.6)], POWER)
route("3V3", [at(R4, 2), (105.0, 127.6), (105.0, 128.0)], POWER)
route("GND", [at(C2, 1), (103.4, 116.6)])
route("GND", [at(U1, 3), (102.2, 122.6)])
route("GND", [at(C1, 2), (119.0, 119.9)])

# ---- the vias and the 3V3 bus on the bottom (the side parts' supply under the signal traces)
for x, y in ((105.6, 118.35), (111.06, 117.2), (105.0, 128.0), (111.06, 128.0), (117.4, 128.0)):
    via("3V3", x, y)
for x, y in ((103.4, 116.6), (102.2, 122.6), (119.0, 119.9), (106.5, 96.8), (113.5, 96.8), (106.6, 134.4),
             (113.4, 134.4)):                       # (the last four: the top pour in front of the socket and around the
    via("GND", x, y)                               # FFC socket's tabs joined to the bottom's)
B = pcbnew.B_Cu
route("3V3", [(105.6, 118.35), (106.75, 117.2), (111.06, 117.2)], POWER, B)
route("3V3", [(105.0, 128.0), (117.4, 128.0)], POWER, B)

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
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)   # (a pour island touching nothing: removed)
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


label("OPTIMIST SD", 104.4, 131.0, 0.8)

board.GetDesignSettings().SetAuxOrigin(pcbnew.VECTOR2I(MM(X0), MM(Y0 + H)))   # (the lower left: the Gerbers, the drill
pcbnew.ZONE_FILLER(board).Fill(board.Zones())                                   # files and cpl.csv share it)
out = os.path.join(HERE, "sd-board.kicad_pcb")
board.Save(out)
print("saved", out)

# ---- JLCPCB's assembly files: the parts list (BOM) and the placement (CPL); the holes are not parts
FAB = os.path.join(HERE, "fab")
os.makedirs(FAB, exist_ok=True)
parts = [fp for fp in board.GetFootprints() if not fp.GetReference().startswith("H")]
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

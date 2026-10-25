# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 SD board (docs/SD-CARD-DESIGN.md), v3: a push-push microSD socket, the chip-select inverter, the series and
pull-up resistors, the decoupling, and a 6-pin 0.5 mm FFC socket. The user's plan (2026-10-25, a photo): an L along
the left edge of the FM-1 board's back: a 10 mm strip from where the flat cable comes round the board's edge (the FFC
socket, its cable side on the board's edge) down to a 31 x 26.5 mm foot where the battery was (the battery moves), the
card's opening on the left edge. The sizes are read off the photo (the 704060 battery = 40 x 60 mm as the scale):
UNMEASURED. Generates sd-board.kicad_pcb with KiCad's own Python (pcbnew):

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

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "lib", "sdboard.pretty")
KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
MM = pcbnew.FromMM

X0, Y0 = 100.0, 100.0                            # the board's top left (mm)
SW, FY, FW, YB = 10.0, 135.0, 31.0, 161.5       # the strip's width, the foot's top, its width, the board's bottom
OUTLINE = ((X0, Y0), (X0 + SW, Y0), (X0 + SW, FY), (X0 + FW, FY), (X0 + FW, YB), (X0, YB))
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


# ---- the parts: J2 at the strip's top, its cable side on the left edge (the cable comes round the FM-1 board's edge
# there); J1 in the foot, its opening on the left edge; the small parts in the foot right of the card
J2 = place(LIB, "FPC-SMD_6P-P0.50_XUNPU_FPC-05F-6PH20", "J2", "FFC 6P 0.5mm", 104.0, 111.5, 270, "C2856796")
J1 = place(LIB, "TF-SMD_TF-01A", "J1", "TF-01A push-push", 109.62, 148.0, 270, "C91145")
R1 = place(LIB, "R0603", "R1", "1k", 105.4, 119.5, 270, "C21190")
R3 = place(LIB, "R0603", "R3", "47k", 127.6, 142.54, 180, "C25819")
R4 = place(LIB, "R0603", "R4", "47k", 127.6, 144.6, 0, "C25819")
C1 = place(LIB, "C0805", "C1", "10uF", 127.6, 147.2, 180, "C15850")
U1 = place(LIB, "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "U1", "74LVC1G04", 122.0, 150.1, 0, "C7827")
R2 = place(LIB, "R0603", "R2", "47k", 117.6, 152.0, 90, "C25819")
C2 = place(LIB, "C0603", "C2", "100nF", 122.0, 154.0, 0, "C14663")
# four holes to fix it (an M2 screw, a zip tie, a rivet): the strip's top, the foot's corners
HOLES = [place(os.path.join(KICAD_FP, "MountingHole.pretty"), "MountingHole_2.2mm_M2", f"H{i + 1}", "M2", x, y)
         for i, (x, y) in enumerate(((105.0, 103.2), (128.4, 137.6), (128.4, 158.9), (102.6, 158.9)))]

# ---- the nets
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

# ---- the copper, top. The FFC's lines go down the strip side by side (0.7 mm apart), turn into the foot above the
# card socket and come down right of its contacts, each turning left into its own: in the card's pin order already
# (DAT0, CLK, 3V3, MOSI, then CS through U1), so nothing crosses. GND goes down a via under the FFC socket's body.
P = {n: at(J2, n) for n in range(1, 7)}
LANE = {"LCD_CS": 109.2, "MOSI": 108.5, "3V3": 107.8, "CLK": 107.1, "MISO": 106.4}     # (down the strip)
BAND = {"LCD_CS": 136.0, "MOSI": 136.7, "3V3": 137.4, "CLK": 138.1, "DAT0": 138.8}      # (across, above the card)
DOWN = {"DAT0": 117.0, "CLK": 117.7, "3V3": 118.4, "MOSI": 119.1, "LCD_CS": 125.0}      # (down, right of the card)
C = {n: at(J1, n) for n in range(1, 9)}
route("LCD_CS", [P[1], (LANE["LCD_CS"], P[1][1]), (LANE["LCD_CS"], BAND["LCD_CS"]), (DOWN["LCD_CS"], BAND["LCD_CS"]),
                 (DOWN["LCD_CS"], at(U1, 2)[1]), at(U1, 2)])
for name, pin, card in (("MOSI", 2, 3), ("3V3", 3, 4), ("CLK", 4, 5)):
    route(name, [P[pin], (LANE[name], P[pin][1]), (LANE[name], BAND[name]), (DOWN[name], BAND[name]),
                 (DOWN[name], C[card][1]), C[card]], POWER if name == "3V3" else TRACK)
route("MISO", [P[6], (LANE["MISO"], P[6][1]), (LANE["MISO"], 116.5), (105.4, 117.5), at(R1, 1)])
route("DAT0", [at(R1, 2), (105.4, BAND["DAT0"]), (DOWN["DAT0"], BAND["DAT0"]), (DOWN["DAT0"], C[7][1]), C[7]])
route("GND", [P[5], (103.9, P[5][1])])
route("CARD_CS", [at(U1, 4), C[2]])
route("DAT2", [C[1], (117.6, C[1][1]), at(R2, 2)])
route("DAT1", [C[8], (116.3, C[8][1])])
route("DAT1", [(125.9, C[8][1]), at(R3, 2)])
route("LCD_CS", [at(R4, 1), (DOWN["LCD_CS"], at(R4, 1)[1])])
BUS = 129.6                                         # (3V3 down the foot's right edge, fed from below)
route("3V3", [(BUS, 141.0), (BUS, 152.6), (117.6, 152.6)], POWER)
route("3V3", [at(R3, 1), (BUS, at(R3, 1)[1])], POWER)
route("3V3", [at(R4, 2), (BUS, at(R4, 2)[1])], POWER)
route("3V3", [at(C1, 1), (BUS, at(C1, 1)[1])], POWER)
route("3V3", [at(U1, 5), (at(U1, 5)[0], 152.6)], POWER)
route("3V3", [at(C2, 2), (at(C2, 2)[0], 152.6)], POWER)
route("GND", [at(C1, 2), (126.6, 148.6)])
route("GND", [at(U1, 3), (123.3, 147.9)])
route("GND", [at(C2, 1), (120.0, 154.0)])
route("GND", [C[6], (116.3, C[6][1])])

# ---- the vias and the bottom: 3V3 from its lane to the bus, DAT1 under the lanes to R3
B = pcbnew.B_Cu
for x, y in ((118.4, 141.0), (BUS, 141.0)):
    via("3V3", x, y)
route("3V3", [(118.4, 141.0), (BUS, 141.0)], POWER, B)
for x, y in ((116.3, C[8][1]), (125.9, C[8][1])):
    via("DAT1", x, y)
route("DAT1", [(116.3, C[8][1]), (125.9, C[8][1])], TRACK, B)
for x, y in ((103.9, P[5][1]), (116.3, C[6][1]), (126.6, 148.6), (123.3, 147.9), (120.0, 154.0),
             (102.0, 122.0), (102.0, 130.0), (112.0, 158.5), (124.5, 158.5), (108.5, 102.0)):   # (the last five: the
    via("GND", x, y)                                                                              # pours stitched)

# ---- the outline, the ground pours, the labels
def edge(ax, ay, bx, by):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(MM(ax), MM(ay)))
    s.SetEnd(pcbnew.VECTOR2I(MM(bx), MM(by)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)


for a, b in zip(OUTLINE, OUTLINE[1:] + OUTLINE[:1]):
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
    inset = ((X0 + 0.3, Y0 + 0.3), (X0 + SW - 0.3, Y0 + 0.3), (X0 + SW - 0.3, FY + 0.3), (X0 + FW - 0.3, FY + 0.3),
             (X0 + FW - 0.3, YB - 0.3), (X0 + 0.3, YB - 0.3))
    for x, y in inset:
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


label("OPTIMIST SD", 120.2, 158.6, 0.8)

board.GetDesignSettings().SetAuxOrigin(pcbnew.VECTOR2I(MM(X0), MM(YB)))   # (the lower left: the Gerbers, the drill
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
        x, y = pcbnew.ToMM(fp.GetPosition().x) - X0, YB - pcbnew.ToMM(fp.GetPosition().y)   # (from the
        f.write(f"{fp.GetReference()},{x:.3f}mm,{y:.3f}mm,Top,{fp.GetOrientationDegrees() % 360:.0f}\n")  # lower left)
print("wrote", os.path.join(FAB, "bom.csv"), os.path.join(FAB, "cpl.csv"))

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

POLE_D, POLE_GAP = 5.2, 0.3                      # the case's poles: diameter (photo: UNMEASURED), the hole's play
POLE_PITCH = 38.1 - POLE_D                       # 38.1 over the two poles' outer edges (calipers, the user, 2026-10-25)
POLES = ((103.8, 135.0), (104.4, 135.0 - POLE_PITCH))     # where they pass through: the L's corner (photo), the one above
X0, Y0 = 100.0, POLES[1][1] - (POLE_D + POLE_GAP) / 2 - 1.5   # the board's top left (mm): 1.5 mm above the upper pole
SW, FY, FW, YB = 10.0, 135.0, 22.0, 157.6       # the strip's width, the foot's top, its width, the board's bottom
SD_OUT = 1.0                                     # the card socket past the left edge, nearer the slot in the case wall (unmeasured)
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
# there); U1 and its parts in the strip under it (the user, 2026-10-25: the foot as small as can be, so the battery
# moves less); J1 in the foot, its opening on the left edge, R1 at its contacts, R2 and R3 right of them
J2 = place(LIB, "FPC-SMD_6P-P0.50_XUNPU_FPC-05F-6PH20", "J2", "FFC 6P 0.5mm", 104.0, 111.5, 270, "C2856796")
J1 = place(LIB, "TF-SMD_TF-01A", "J1", "TF-01A push-push", X0 + 9.62 - SD_OUT, 148.0, 270, "C91145")
U1 = place(LIB, "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "U1", "74LVC1G04", 103.5, 119.0, 0, "C7827")
R4 = place(LIB, "R0603", "R4", "47k", 105.85, 121.6, 0, "C25819")
C2 = place(LIB, "C0603", "C2", "100nF", 103.4, 123.9, 0, "C14663")
C1 = place(LIB, "C0805", "C1", "10uF", 105.3, 126.6, 180, "C15850")
R1 = place(LIB, "R0603", "R1", "1k", 115.0, 143.64, 180, "C21190")
R3 = place(LIB, "R0603", "R3", "47k", 120.3, 144.2, 90, "C25819")
R2 = place(LIB, "R0603", "R2", "47k", 120.3, 149.0, 270, "C25819")
# the case's two poles pass through it and hold it (the user, 2026-10-25)
def pole_hole(ref, x, y):
    """a plain hole (not plated) the case's pole passes through"""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("sdboard", "PoleHole"))
    fp.SetReference(ref)
    fp.SetValue("pole")
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    d = MM(POLE_D + POLE_GAP)
    h = pcbnew.PAD(fp)
    h.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
    h.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    h.SetSize(pcbnew.VECTOR2I(d, d))
    h.SetDrillSize(pcbnew.VECTOR2I(d, d))
    h.SetLayerSet(pcbnew.LSET.AllCuMask())
    fp.Add(h)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    board.Add(fp)
    return fp


HOLES = [pole_hole(f"H{i + 1}", x, y) for i, (x, y) in enumerate(POLES)]

# ---- the nets. J2's order is free (the tap board at J12 is not designed yet): the three lines to the card on top, so
# they go straight to their lanes, GND, then 3V3 and LCD_CS at the bottom, straight down to the parts under it
J2_PINS = ((1, "MOSI"), (2, "CLK"), (3, "MISO"), (4, "GND"), (5, "3V3"), (6, "LCD_CS"), (7, "GND"), (8, "GND"))
for num, name in ((1, "DAT2"), (2, "CARD_CS"), (3, "MOSI"), (4, "3V3"), (5, "CLK"), (6, "GND"), (7, "DAT0"),
                  (8, "DAT1"), (10, "GND"), (11, "GND"), (12, "GND"), (13, "GND")):
    connect(J1, num, name)
for num, name in J2_PINS:
    connect(J2, num, name)
for num, name in ((2, "LCD_CS"), (3, "GND"), (4, "CARD_CS"), (5, "3V3")):
    connect(U1, num, name)
for fp, a, b in ((R1, "MISO", "DAT0"), (R2, "3V3", "DAT2"), (R3, "3V3", "DAT1"), (R4, "LCD_CS", "3V3"),
                 (C1, "3V3", "GND"), (C2, "GND", "3V3")):
    connect(fp, 1, a)
    connect(fp, 2, b)

# ---- the copper, top. MOSI, CLK, MISO go down the strip's right side, turn into the foot above the card and come down
# right of its contacts, each turning left into its own (MISO through R1). CS and 3V3 reach the card on the bottom,
# from the strip round the right of the lower screw; DAT1 and DAT2 go on the bottom to R3 and R2.
P = {n: at(J2, n) for n in range(1, 7)}
C = {n: at(J1, n) for n in range(1, 9)}
LANE = {"MOSI": 108.9, "CLK": 108.2, "MISO": 107.5}       # (down the strip)
BAND = {"MOSI": 136.0, "CLK": 136.7, "MISO": 137.4}       # (across, above the card)
DOWN = {"MISO": 116.9, "CLK": 117.6, "MOSI": 118.3}       # (down, right of the card)
END = {"MOSI": C[3], "CLK": C[5], "MISO": at(R1, 1)}
for name, pin in (("MOSI", 1), ("CLK", 2), ("MISO", 3)):
    route(name, [P[pin], (LANE[name], P[pin][1]), (LANE[name], BAND[name]), (DOWN[name], BAND[name]),
                 (DOWN[name], END[name][1]), END[name]])
route("DAT0", [at(R1, 2), C[7]])
route("GND", [P[4], (103.9, P[4][1])])                     # (a via under the FFC socket's body)
TRUNK = 106.6                                              # (3V3 down the parts' right side)
route("3V3", [P[5], (TRUNK, P[5][1]), (TRUNK, 128.4)], POWER)
route("LCD_CS", [P[6], (105.3, 116.4), (105.9, 117.0), (105.9, at(U1, 2)[1]), at(U1, 2)])
route("LCD_CS", [(105.9, at(U1, 2)[1]), (105.9, 120.4), (105.1, 121.2), at(R4, 1)])
route("3V3", [(TRUNK, 122.6), (at(U1, 5)[0], 122.6), at(U1, 5)], POWER)
route("3V3", [at(C2, 2), (at(C2, 2)[0], 122.6)], POWER)
route("GND", [at(U1, 3), (104.8, 116.9)])
route("GND", [at(C2, 1), (102.6, 125.2)])
route("GND", [at(C1, 2), (103.3, 127.8)])
route("CARD_CS", [(101.6, 129.4), (100.9, 128.7), (100.9, at(U1, 4)[1]), at(U1, 4)])
route("3V3", [at(R3, 1), at(R2, 1)], POWER)
route("DAT1", [(120.3, C[8][1]), at(R3, 2)])
route("DAT2", [(120.3, 150.65), at(R2, 2)])
VIA_AT = {"DAT1": (115.0, C[8][1]), "GND": (115.0, C[6][1]), "3V3": (115.0, C[4][1]), "CARD_CS": (115.0, C[2][1]),
          "DAT2": (115.9, C[1][1])}                        # (beside the card's contacts)
for name, card in (("DAT1", 8), ("GND", 6), ("3V3", 4), ("CARD_CS", 2), ("DAT2", 1)):
    route(name, [C[card], VIA_AT[name]], POWER if name == "3V3" else TRACK)

# ---- the bottom: CS and 3V3 west under the card, north right of the lower screw, into the strip; DAT1, 3V3, DAT2 east
# under the lanes to R3 and R2
B = pcbnew.B_Cu
route("3V3", [VIA_AT["3V3"], (108.3, C[4][1]), (108.3, 128.4), (TRUNK, 128.4)], POWER, B)
route("CARD_CS", [VIA_AT["CARD_CS"], (107.5, C[2][1]), (107.5, 129.4), (101.6, 129.4)], TRACK, B)
route("DAT1", [VIA_AT["DAT1"], (120.3, C[8][1])], TRACK, B)
route("3V3", [VIA_AT["3V3"], (120.3, C[4][1])], POWER, B)
route("DAT2", [VIA_AT["DAT2"], (120.3, C[1][1]), (120.3, 150.65)], TRACK, B)
for name, (x, y) in VIA_AT.items():
    via(name, x, y)
for name, x, y in (("3V3", TRUNK, 128.4), ("CARD_CS", 101.6, 129.4), ("DAT1", 120.3, C[8][1]), ("3V3", 120.3, C[4][1]),
                   ("DAT2", 120.3, 150.65)):
    via(name, x, y)
for x, y in ((103.9, P[4][1]), (104.8, 116.9), (102.6, 125.2), (103.3, 127.8),
             (101.3, 106.3), (112.0, 138.5), (116.0, 153.0)):   # (the last three: the pours stitched)
    via("GND", x, y)

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


label("OPTIMIST SD", 108.4, 103.0, 0.8, 90)

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

# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 SD board (docs/SD-CARD-DESIGN.md), v4: a push-push microSD socket, the chip-select inverter, the series and
pull-up resistors, the decoupling, and a 6-pin 0.5 mm FFC socket. The user's plan (2026-10-25): a small board on the
FM-1 board's back right of the battery, by its plug (the battery stays); the card goes in through the bottom wall,
flush with the outside, between the pegs that close the case; the cover's pole there holds it; the flat cable from a
tap at J12 comes round the board's edge between the two jacks and down the back to the FFC socket. Every size is
measured (MEASUREMENTS.md). Generates sd-board.kicad_pcb with KiCad's own Python (pcbnew):

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 make_board.py

Then make_fab.sh checks it (DRC, the schematic's nets) and writes the JLCPCB files (Gerbers, drill, BOM, placement).

Nets (microSD pins: 1 DAT2, 2 DAT3/CS, 3 CMD, 4 VDD, 5 CLK, 6 VSS, 7 DAT0, 8 DAT1, 9 card detect, 10-13 shell;
FFC J2 pins 1..6, 7/8 its tabs):
  3V3      J2 1 -> J1 4, U1 VCC, C1 10 uF, C2 100 nF, the pull-ups
  LCD_CS   J2 2 -> U1 A (74LVC1G04 in); R4 47k pull-up (the line floats while the FM-1 resets)
  CARD_CS  U1 Y  -> J1 2
  MOSI     J2 3 -> J1 3 (CMD)
  GND      J2 4 -> J1 6, the shell, U1 GND, J2's tabs
  CLK      J2 5 -> J1 5
  MISO     J2 6 -> R1 1k -> J1 7 (DAT0): the FM-1's PC8 is the LCD's D/C too (the 1k takes any overlap)
  DAT1/DAT2  47k pull-ups (unused in SPI mode, not left floating)
"""
import math
import os

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "lib", "sdboard.pretty")
KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
MM = pcbnew.FromMM

# The board sits on the FM-1 board's back in the corner right of the battery, by the battery's plug (the user's plan,
# 2026-10-25; every size measured with calipers, MEASUREMENTS.md). Placed in the FM-1 board's own millimetres: u from
# its left edge, v up from its bottom edge, both seen from the back; T() turns them into KiCad's page (y down).
def T(u, v):
    return 100.0 + u, 200.0 - v


BATTERY_R = 72.6                                 # k: the battery's right side
PLUG_L = BATTERY_R + 30.0                        # o: the plug's left side (above p, 23.5 mm up)
POLE = (78.0, 40.0)                              # l, m: the cover's pole there (6.9 mm wide, i)
COLLAR_R = (6.9 + 0.4) / 2 + 1.0                # the tray's collar round the pole (make_tray.py): 0.2 play, 1 mm wall
POLE_R = COLLAR_R + 0.3                          # (the board's seat round the collar; it stops the board sliding in)
TRAY_RAIL = 1.2                                  # the tray's rails along the board's sides (a groove each: the user,
                                                 # 2026-10-25, the board slides into the tray, held up and down)
WALL_OUT = -3.0                                  # x: the bottom wall's outside, 3 mm below the board's bottom edge
CARD_L = 84.0 - 1.6                              # the card's left side 84 mm from the left wall's inside (the user):
                                                 # 82.4 from the board's edge (the wall's inside at 1.6, MEASUREMENTS)
U0 = BATTERY_R + 0.6 + TRAY_RAIL                 # the board's left edge: its tray's rail 0.6 mm clear of the battery
U1_ = CARD_L + 5.5 + 7.76 + 0.6 + 0.5            # its right edge 0.5 mm past the card socket's right pads (the user:
                                                 # "cut it at the edge of the SD card reader")
V0, V1 = -1.5, POLE[1]                           # its bottom 1.5 mm into the gap to the bottom wall; its top at the
                                                 # pole's centre (the seat round its lower half), 4 mm under the speaker
                                                 # plug (w2: 44): the board must not rest against it (the user)
X0, YB = T(U0, V0)                               # (the lower left: the Gerbers' and the placement's origin)
TRACK, POWER = 0.2, 0.3


def notch(cx, cy, a0, a1, n=24):
    """the points of the seat round the pole's lower half, from angle a0 to a1 (degrees, 0 = right, 90 = down)"""
    return [(cx + POLE_R * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + POLE_R * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


PX, PY = T(*POLE)
SEAT_END = math.degrees(math.acos((U0 - POLE[0]) / POLE_R))   # (where the seat meets the board's left edge)
OUTLINE = tuple([T(U0, V0), T(U1_, V0), T(U1_, V1), (PX + POLE_R, PY),
                 *notch(PX, PY, 0, SEAT_END)[1:]])


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


def P(lib, name, ref, value, u, v, rot, lcsc):
    return place(lib, name, ref, value, *T(u, v), rot, lcsc)


def A(fp, num):
    x, y = at(fp, num)
    return round(x - 100.0, 3), round(200.0 - y, 3)


def R(name, pts, width=TRACK, layer=pcbnew.F_Cu):
    route(name, [T(*p) for p in pts], width, layer)


def V(name, u, v):
    via(name, *T(u, v))


# ---- the parts: the card socket at the bottom, its opening on the bottom wall's outside (the card flush, the user);
# the FFC socket's cable side 5 mm under the board's top (the ribbon comes down over the 4 mm speaker plug, the
# user), its
# cable from between the jacks (r: 90 mm); U1 and its parts right
CARD_C = CARD_L + 5.5                            # (the card, 11 mm wide, taken as centred in its socket: UNMEASURED)
J1 = P(LIB, "TF-SMD_TF-01A", "J1", "TF-01A push-push", CARD_C, WALL_OUT + 9.62, 0, "C91145")
J2 = P(LIB, "FPC-SMD_6P-P0.50_XUNPU_FPC-05F-6PH20", "J2", "FFC 6P 0.5mm", 91.0, 31.0, 180, "C2856796")
U1 = P(LIB, "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "U1", "74LVC1G04", 93.6, 22.5, 90, "C7827")
R4 = P(LIB, "R0603", "R4", "47k", 94.35, 25.7, 0, "C25819")
C1 = P(LIB, "C0805", "C1", "10uF", 94.2, 16.0, 180, "C15850")
C2 = P(LIB, "C0603", "C2", "100nF", 94.0, 19.1, 0, "C14663")
R1 = P(LIB, "R0603", "R1", "1k", CARD_C - 4.36, 22.0, 270, "C21190")
R2 = P(LIB, "R0603", "R2", "47k", 94.3, 14.0, 180, "C25819")
R3 = P(LIB, "R0603", "R3", "47k", 80.2, 14.2, 0, "C25819")

# ---- the nets. J2's order is free (the tap board at J12 is not designed yet): left to right the card's own order
J2_PINS = ((1, "3V3"), (2, "LCD_CS"), (3, "MOSI"), (4, "GND"), (5, "CLK"), (6, "MISO"), (7, "GND"), (8, "GND"))
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

# ---- the copper, top. MISO, CLK, MOSI go down from J2, turn left one under the other and come down onto their
# contacts; LCD_CS and 3V3 turn right, to U1 and to a 3V3 rail down the right edge; U1's output comes down onto CS.
P2 = {n: A(J2, n) for n in range(1, 7)}
C = {n: A(J1, n) for n in range(1, 9)}
RAIL = 95.8
for name, pin, card, turn in (("MISO", 6, 7, 26.5), ("CLK", 5, 5, 25.7), ("MOSI", 3, 3, 24.9)):
    R(name, [P2[pin], (P2[pin][0], turn), (C[card][0], turn), A(R1, 1) if name == "MISO" else C[card]])
R("DAT0", [A(R1, 2), C[7]])
R("GND", [P2[4], (P2[4][0], 30.9)])               # (a via under the FFC socket's body)
R("LCD_CS", [P2[2], (P2[2][0], 28.1), (93.6, 26.25), A(U1, 2)])
R("3V3", [P2[1], (P2[1][0], 28.7), (RAIL, 28.7), (RAIL, A(R2, 1)[1]), A(R2, 1)], POWER)
R("3V3", [A(C1, 1), (RAIL, A(C1, 1)[1])], POWER)
R("3V3", [A(R4, 2), (RAIL, A(R4, 2)[1])], POWER)
R("3V3", [A(U1, 5), (RAIL, A(U1, 5)[1])], POWER)
R("3V3", [A(C2, 2), (RAIL, A(C2, 2)[1])], POWER)
R("CARD_CS", [A(U1, 4), (C[2][0], A(U1, 4)[1]), C[2]])
R("DAT2", [C[1], (C[1][0], A(R2, 2)[1]), A(R2, 2)])
R("DAT1", [C[8], (C[8][0], 14.2), A(R3, 2)])
R("GND", [C[6], (C[6][0], 15.2)])
R("3V3", [C[4], (C[4][0], 14.2)], POWER)
R("3V3", [A(R3, 1), (78.4, 14.2)], POWER)
R("GND", [A(U1, 3), (91.9, 25.1)])
R("GND", [A(C2, 1), (92.3, A(C2, 1)[1])])
R("GND", [A(C1, 2), (92.0, A(C1, 2)[1])])

# ---- the bottom: 3V3 across, from the rail to the card's 3V3 contact and to R3
B = pcbnew.B_Cu
R("3V3", [(78.4, 14.2), (RAIL, 14.2), (RAIL, 16.8)], POWER, B)
for u, v in ((78.4, 14.2), (C[4][0], 14.2), (RAIL, 16.8)):
    V("3V3", u, v)
for u, v in ((P2[4][0], 30.9), (C[6][0], 15.2), (91.9, 25.1), (92.3, A(C2, 1)[1]), (92.0, A(C1, 2)[1]),
             (76.0, 5.0), (76.0, 25.0), (79.0, 25.0), (95.9, 6.0), (76.0, 34.0)):   # (the last five: the pours)
    V("GND", u, v)

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
    for x, y in OUTLINE:                                        # (the fill keeps off the edge)
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


label("OPTIMIST SD", *T(77.5, 18.0), 0.8, 90)

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

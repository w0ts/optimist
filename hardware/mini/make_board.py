# SPDX-License-Identifier: GPL-3.0-only
"""The short FM-1 SD board (DESIGN.md): m-card's place under the jacks (Julius Vitkauskas, https://github.com/jvitkauskas/m-card,
ec63784, CC0-1.0) and its hinged socket, redrawn for short lines (the user, 2026-11-04): 26 mm wide, the card socket top
left, its contact tails facing J2; J2 (the cable from J12) behind the socket, its opening toward the card as v6's J2, its
centre over J3, in line, 5.5 mm from the left edge (DESIGN.md says why not 15); J3 (the LCD's cable) on the bottom side, under the socket, its opening at the
jacks. Same circuit as v6/v7. Generates sd-board.kicad_pcb with KiCad's own Python:

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 make_board.py

Positions are in mm on the FM-1 board's back: u from its left edge, v up from its bottom edge. T() turns them into KiCad's
page. "Top" of this board = the J3 side, toward the jacks (m-card's top).

Why every line runs as it does (J2 pads = pins n left to right, wired by place):
  A line leaves its J2 pad heading north, under J2's body, and reaches the Hirose tails (rot 0, tails south, west to east
  DAT1 DO VSS CLK VDD DI CS DAT2) in order: DO, CLK and DI never cross. The through-lines dive north of J2 (one via each) and
  run on the bottom to J3. The series resistors sit between J2's pads and the tails; ESD and the small parts are on the
  bottom under them (the bottom side is assembled anyway for J3), tied to the tails through a via each.
"""
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True                  # (no lib/__pycache__ in the repository)
sys.path.insert(0, os.path.join(HERE, "lib"))
from stitch import stitch   # (lib/stitch.py -> ../lib: after m-card's stitch(), CC0-1.0)
LIB = os.path.join(HERE, "lib", "sdboard.pretty")
KICAD_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
RTUNE = os.path.join(KICAD_FP, "Resistor_SMD.pretty")
MM = pcbnew.FromMM
THICK = 1.0                                      # (m-card's advice: 0.6 mm of height back; the cable's loop needs it)


def T(u, v):
    return 100.0 + u, 200.0 - v


LEFT, RIGHT, BOT, TOP = 84.5, 110.5, 51.7, 76.7  # m-card's left and top; 26 mm wide (the user); the bottom at J2's opening
OUTLINE = (T(LEFT, BOT), T(RIGHT, BOT), T(RIGHT, TOP), T(LEFT, TOP))
X0, YB = T(LEFT, BOT)                            # (the lower left: the Gerbers' and the placement's origin)
TRACK, POWER = 0.2, 0.3
B = pcbnew.B_Cu

board = pcbnew.BOARD()
board.GetDesignSettings().SetBoardThickness(MM(THICK))
nets = {}
ALIAS = {"SD_DO": "MISO", "SD_CSIN": "LCD_CS", "SD_CLK": "CLK_SD", "SD_DI": "MOSI_SD", "SD_VIN": "3V3"}   # (as v7)


def net(name):
    name = ALIAS.get(name, name)
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]


def finish_part(fp, ref, value, lcsc):
    fp.SetReference(ref)
    fp.SetValue(value)
    if lcsc:
        fp.SetField("LCSC", lcsc)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    for f in fp.GetFields():
        if f.GetName() == "LCSC":
            f.SetVisible(False)
    return fp


def P(name, ref, value, u, v, rot=0.0, lcsc="", bottom=False):
    fp = pcbnew.FootprintLoad(LIB, name)
    fp.SetFPID(pcbnew.LIB_ID("sdboard", name))
    finish_part(fp, ref, value, lcsc)
    fp.SetPosition(pcbnew.VECTOR2I(*[MM(c) for c in T(u, v)]))
    fp.SetOrientationDegrees(rot)
    board.Add(fp)
    if bottom:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    return fp


def PH(ref, value, u, v, rot, lcsc):
    """A series resistor on KiCad's 0603 hand-solder pads: the values the bench may still change."""
    name = "R_0603_1608Metric_Pad0.98x0.95mm_HandSolder"
    fp = pcbnew.FootprintLoad(RTUNE, name)
    fp.SetFPID(pcbnew.LIB_ID("Resistor_SMD", name))
    finish_part(fp, ref, value, lcsc)
    fp.SetPosition(pcbnew.VECTOR2I(*[MM(c) for c in T(u, v)]))
    fp.SetOrientationDegrees(rot)
    for item in list(fp.GraphicalItems()):       # (the courtyard round the pads: the three sit 1.15 mm apart, 0.2 mm of
        if item.GetLayer() == pcbnew.F_CrtYd:    # copper between them; KiCad's own courtyard would ask for 1.46)
            fp.Remove(item)
    box = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_RECT)
    a, c = (0.55, 1.4) if rot in (90, 270) else (1.4, 0.55)
    box.SetStart(pcbnew.VECTOR2I(MM(u - a + 100.0), MM(200.0 - v - c)))
    box.SetEnd(pcbnew.VECTOR2I(MM(u + a + 100.0), MM(200.0 - v + c)))
    box.SetLayer(pcbnew.F_CrtYd)
    box.SetWidth(MM(0.05))
    fp.Add(box)
    board.Add(fp)
    return fp


def dm3cs():
    """Hirose DM3CS-SF, hinged microSD socket: the land pattern of m-card's dm3cs() (Hirose's recommended mounting pattern,
    CC0-1.0), its pads renumbered to the schematic's symbol: 1-8 contacts, 9 the card switch, 10-13 the shell. Footprint
    axes: y from the hinge end (his y minus 6.95: the origin on the body's centre), x with his 0.2 taken off."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("sdboard", "microSD_Hirose_DM3CS-SF_hinged"))
    fp.SetAttributes(pcbnew.FP_SMD)

    def pad(num, x, y, w, h):
        p = pcbnew.PAD(fp)
        p.SetNumber(str(num))
        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetShape(pcbnew.PAD_SHAPE_RECT)
        p.SetLayerSet(p.SMDMask())
        p.SetSize(pcbnew.VECTOR2I(MM(w), MM(h)))
        p.SetPosition(pcbnew.VECTOR2I(MM(x - 0.2), MM(y - 6.95)))
        fp.Add(p)

    for n in range(1, 9):
        pad(n, 3.2 - 1.1 * (n - 1), 13.4, 0.7, 2.0)
    pad(9, 4.85, 0.6, 1.2, 1.2)                  # (his pad 10: the card switch)
    pad(10, -6.6, 12.9, 1.0, 2.6)
    pad(11, 6.75, 12.9, 1.5, 2.6)
    pad(12, -3.1, 0.7, 2.7, 1.4)
    pad(13, 1.85, 0.7, 2.7, 1.4)
    for x0, y0, x1, y1 in ((-5.05, 2.9, 3.75, 5.4), (-6.9, 7.8, -6.3, 11.6), (6.7, 7.8, 7.3, 11.6)):
        z = pcbnew.ZONE(fp)                      # (Hirose's "no conductive traces" areas)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowZoneFills(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        z.SetLayer(pcbnew.F_Cu)
        o = z.Outline()
        o.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            o.Append(MM(x - 0.2), MM(y - 6.95))
        fp.Add(z)
    # (the body, then the locked card: its end at y 15.45 reaches past the tails, the courtyard stops there)
    for layer, (x0, y0, x1, y1), width in ((pcbnew.F_Fab, (-6.7, 0.0, 7.1, 13.9), 0.1),
                                          (pcbnew.F_Fab, (-5.5, 4.45, 5.5, 15.45), 0.1),
                                          (pcbnew.F_CrtYd, (-7.35, -0.25, 7.75, 14.9), 0.05)):
        s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_RECT)
        s.SetStart(pcbnew.VECTOR2I(MM(x0 - 0.2), MM(y0 - 6.95)))
        s.SetEnd(pcbnew.VECTOR2I(MM(x1 - 0.2), MM(y1 - 6.95)))
        s.SetLayer(layer)
        s.SetWidth(MM(width))
        fp.Add(s)
    finish_part(fp, "J1", "DM3CS-SF hinged", "C202111")
    return fp


def pad(fp, num):
    for p in fp.Pads():
        if p.GetNumber() == str(num):
            return p
    raise KeyError(f"{fp.GetReference()} has no pad {num}")


def connect(fp, num, name):
    pad(fp, num).SetNet(net(name))


def A(fp, num):
    p = pad(fp, num).GetPosition()
    return round(pcbnew.ToMM(p.x) - 100.0, 3), round(200.0 - pcbnew.ToMM(p.y), 3)


def R(name, pts, width=TRACK, layer=pcbnew.F_Cu):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(*[MM(c) for c in T(*a)]))
        t.SetEnd(pcbnew.VECTOR2I(*[MM(c) for c in T(*b)]))
        t.SetWidth(MM(width))
        t.SetLayer(layer)
        t.SetNet(net(name))
        board.Add(t)


def V(name, u, v):
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(pcbnew.VECTOR2I(*[MM(c) for c in T(u, v)]))
    via.SetDrill(MM(0.3))
    via.SetWidth(MM(0.6))
    via.SetNet(net(name))
    board.Add(via)


# ---- the parts. The hinge at the top edge, 0.5 mm in; the card's centre 7.6 mm from the left edge
FPC_TOP = "FPC-SMD_10P-P0.50_XUNPU_FPC-0.5AS-10PWBH20"
FPC_BOT = "FPC-SMD_10P-P0.50_XUNPU_FPC-05F-10PH20"
SOT6, SOT5 = "SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR"
CARD_C, HINGE = 92.1, 76.2
J1 = dm3cs()
J1.SetPosition(pcbnew.VECTOR2I(*[MM(c) for c in T(CARD_C + 0.2, HINGE - 6.95)]))
board.Add(J1)
# J2: top side, rot 180 as v6's own J2 (the same part, tested): its opening faces the card, its contacts the bottom edge; the
# J12 cable arrives from above, over the socket, gold DOWN. Turned, its own pin 1 is the right end: pad 11 - n is line n
J2_C, J2_V = 90.0, 54.15                  # (straight over J3, the user; its pads as low as the bottom edge allows,
                                                 # so its opening, 58.28, stays as far from the card's end, 60.75, as it can)
J2 = P(FPC_BOT, "J2", "FPC 10P 0.5mm bottom contact", J2_C, J2_V, 180, "C2856798")
J2_AT = {n: 11 - n for n in range(1, 11)}
# J3: bottom side, its opening up at the jacks as v6's, 76.4 mm up (72.3 + 4.1), centred on the jacks' gap as m-card's
J3_C, J3_V = 90.0, 72.3
J3 = P(FPC_BOT, "J3", "FPC 10P 0.5mm bottom contact", J3_C, J3_V, 180, "C2856798", bottom=True)
assert J3.GetLayer() == B
R1 = PH("R1", "91", 88.1, 59.7, 90, "to confirm")           # (standing between J2's opening and the tails: pad 1, the bus, down)
R5 = PH("R5", "240", 90.9, 59.7, 270, "to confirm")          # (pad 2, the bus, down)
R6 = PH("R6", "240", 93.6, 59.7, 270, "to confirm")
# the bottom side, east of the through-lines: ESD, the inverter, the pull-ups, the decoupling
U3 = P(SOT6, "U3", "USBLC6-2SC6", 94.2, 67.7, 0, "C7519", bottom=True)
U2 = P(SOT6, "U2", "USBLC6-2SC6", 97.0, 63.1, 0, "C7519", bottom=True)
U1 = P(SOT5, "U1", "74LVC1G04", 95.4, 54.4, 0, "C7827", bottom=True)
R7 = P("R0603", "R7", "47k", 96.6, 68.7, 0, "C25819", bottom=True)
R8 = P("R0603", "R8", "47k", 93.4, 62.9, 0, "C25819", bottom=True)
R3 = P("R0603", "R3", "47k", 96.3, 70.65, 90, "C25819", bottom=True)
R4 = P("R0603", "R4", "47k", 92.85, 55.7, 270, "C25819", bottom=True)
R2 = P("R0603", "R2", "47k", 97.0, 65.6, 180, "C25819", bottom=True)
C1 = P("C0805", "C1", "10uF", 99.6, 60.0, 180, "C15850", bottom=True)
C2 = P("C0603", "C2", "100nF", 99.2, 53.45, 180, "C14663", bottom=True)
C3 = P("C0603", "C3", "100nF", 92.85, 59.9, 270, "C14663", bottom=True)
FB1 = P("R0603", "FB1", "600R@100MHz", 96.9, 57.0, 180, "C1002", bottom=True)
TP5 = pcbnew.FootprintLoad("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/TestPoint.pretty",
                           "TestPoint_Pad_D1.5mm")
TP5.SetFPID(pcbnew.LIB_ID("TestPoint", "TestPoint_Pad_D1.5mm"))
finish_part(TP5, "TP5", "J12 DET", "")
TP5.SetPosition(pcbnew.VECTOR2I(*[MM(c) for c in T(101.2, 75.2)]))
board.Add(TP5)

LINES = {1: "GND", 2: "MISO", 3: "LCD_CS", 4: "CLK", 5: "MOSI", 6: "LCD_6", 7: "3V3", 8: "LCD_8", 9: "LCD_9", 10: "GND"}
for n, name in LINES.items():                    # (by place: line n on the n-th contact from the left, both sockets)
    assert abs(A(J2, J2_AT[n])[0] - (J2_C + (n - 5.5) * 0.5)) < 0.005 and abs(A(J3, n)[0] - (J3_C + (n - 5.5) * 0.5)) < 0.005, n
    connect(J2, J2_AT[n], name)
    connect(J3, n, name)
for fp in (J2, J3):
    connect(fp, 11, "GND")
    connect(fp, 12, "GND")
    for n in (1, 10):
        pad(fp, n).SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
for num, name in ((1, "DAT2"), (2, "CARD_CS"), (3, "SD_DI"), (4, "SD_VDD"), (5, "SD_CLK"), (6, "GND"), (7, "DAT0"),
                  (8, "DAT1"), (9, "CARD_DET"), (10, "GND"), (11, "GND"), (12, "GND"), (13, "GND")):
    connect(J1, num, name)
for num, name in ((2, "SD_CSIN"), (3, "GND"), (4, "CARD_CS"), (5, "SD_VDD")):
    connect(U1, num, name)
for fp, a, b in ((R1, "SD_DO", "DAT0"), (R2, "DAT2", "SD_VDD"), (R3, "SD_VDD", "DAT1"), (R4, "GND", "SD_CSIN"),
                 (C1, "SD_VDD", "GND"), (C2, "SD_VDD", "GND"), (R5, "CLK_SD", "CLK"), (R6, "MOSI_SD", "MOSI"),
                 (R7, "SD_VDD", "DAT0"), (C3, "SD_VDD", "GND"), (FB1, "SD_VIN", "SD_VDD"), (R8, "SD_DI", "SD_VDD")):
    connect(fp, 1, a)
    connect(fp, 2, b)
for fp, east, west in ((U3, "DAT0", "SD_CLK"), (U2, "CARD_CS", "SD_DI")):
    for num, name in ((1, east), (6, east), (3, west), (4, west), (2, "GND"), (5, "SD_VDD")):
        connect(fp, num, name)
connect(TP5, 1, "CARD_DET")

# ---- J2 straight over J3: each line north from its J2 pad, under J2's body, to its via; the vias a staircase, 0.6 mm across
# and 0.55 mm up from line 9 to line 2, so a line on either layer passes the next via; then straight north on the bottom to J3.
TAIL = {n: A(J1, n) for n in range(1, 9)}
TAIL_N = {n: (TAIL[n][0], TAIL[n][1] + 1.0) for n in TAIL}      # (the tails' ends under the socket)
LVL = {n: 90.0 + (n - 5.5) * 0.6 for n in range(2, 10)}
BUSVIA = {n: (round(LVL[n], 2), round(54.0 + (9 - n) * 0.55, 2)) for n in range(2, 10)}
LANE = {2: 87.7, 3: 88.2, 4: 88.7, 5: 89.2, 6: 90.4, 7: 90.9, 8: 91.4, 9: 91.9}   # (past the staircase: a lane left for GUARD)
GUARD = 89.8
for n, (u, v) in BUSVIA.items():
    p = A(J2, J2_AT[n])
    R(LINES[n], [p, (p[0], 53.55), (u, 53.95), (u, v)])
    V(LINES[n], u, v)
    a = A(J3, n)
    R(LINES[n], [(u, v), (u, 58.6), (LANE[n], 59.4)], TRACK, B)
    R(LINES[n], [(LANE[n], 59.4), (LANE[n], 68.9)], POWER if n == 7 else TRACK, B)
    R(LINES[n], [(LANE[n], 68.9), (LANE[n], 69.0), (a[0], 69.9), a], TRACK, B)
# the guard: the card's ground contact sits over the lanes, so a ground lane between lines 5 and 6 joins it to the top pour
VSS_V, GUARD_N = (GUARD, 61.3), (GUARD, 68.6)
R("GND", [TAIL[6], VSS_V])
V("GND", *VSS_V)
R("GND", [VSS_V, GUARD_N], TRACK, B)
V("GND", *GUARD_N)
# J2's ground contacts to its tabs
for n, tab in ((1, min(A(J2, 11)[0], A(J2, 12)[0])), (10, max(A(J2, 11)[0], A(J2, 12)[0]))):
    g = A(J2, J2_AT[n])
    R("GND", [g, (g[0], 52.35), (tab, 52.35), (tab, J2_V + 0.2)])
# the bus to the resistors, top: DI and CLK east along their levels, under J2's lip
LEVEL_DI, LEVEL_CLK = 57.7, 58.1
R("MOSI", [BUSVIA[5], (BUSVIA[5][0], LEVEL_DI), (A(R6, 2)[0], LEVEL_DI), A(R6, 2)])
R("CLK", [BUSVIA[4], (BUSVIA[4][0], LEVEL_CLK), (A(R5, 2)[0], LEVEL_CLK), A(R5, 2)])
R("MISO", [BUSVIA[2], A(R1, 1)])
R("DAT0", [A(R1, 2), (TAIL[7][0], 61.2), TAIL[7]])
R("SD_CLK", [A(R5, 1), TAIL[5]])
R("SD_DI", [A(R6, 1), TAIL[3]])
# 3.3 V off line 7's via, top, over J2's right tab, to the bead on the bottom
V3 = (95.75, 57.1)
R("3V3", [BUSVIA[7], (91.3, 55.5), (92.6, 57.1), V3], POWER)
V("3V3", *V3)
R("CARD_DET", [A(J1, 9), (A(TP5, 1)[0], A(J1, 9)[1] - 0.4), A(TP5, 1)])
# the card's lines to the bottom's small parts. South ends: DI, CS, VDD. North ends, under the socket, east past the tails: CLK, DO, DAT1
DI_V, CS_V, VDD_V = (93.8, 61.2), (94.75, 61.05), (92.6, 61.3)
R("SD_DI", [A(R6, 1), DI_V])
V("SD_DI", *DI_V)
R("CARD_CS", [TAIL[2], CS_V])
V("CARD_CS", *CS_V)
R("SD_VDD", [TAIL[4], (TAIL[4][0], 61.7), VDD_V], POWER)
V("SD_VDD", *VDD_V)
CLK_V, DO_V = (93.0, 64.35), (93.95, 65.0)
R("SD_CLK", [TAIL[5], TAIL_N[5], (TAIL_N[5][0], CLK_V[1]), CLK_V])
V("SD_CLK", *CLK_V)
R("DAT0", [TAIL[7], (TAIL[7][0], DO_V[1]), DO_V])
V("DAT0", *DO_V)
DAT1_V = (96.9, 71.7)
R("DAT1", [TAIL[8], (TAIL[8][0], 65.65), (DAT1_V[0], 65.65), DAT1_V])
V("DAT1", *DAT1_V)
DAT2_V = (95.2, 64.4)
R("DAT2", [TAIL[1], DAT2_V])
V("DAT2", *DAT2_V)
# the supply, bottom: bead, a rail up the right, across the top, down to the tail's via
COL, TOPRAIL = 98.6, 69.8
R("3V3", [V3, A(FB1, 1)], POWER, B)
R("SD_VDD", [A(FB1, 2), (COL, 57.5)], POWER, B)
R("SD_VDD", [(A(U1, 5)[0], A(U1, 5)[1]), (COL, A(U1, 5)[1]), (COL, TOPRAIL), (92.4, TOPRAIL)], POWER, B)
R("SD_VDD", [(92.4, TOPRAIL), (92.4, 61.8), VDD_V], TRACK, B)
R("SD_VDD", [A(C2, 1), (COL, A(C2, 1)[1])], TRACK, B)
R("SD_VDD", [VDD_V, A(C3, 1)], TRACK, B)
R("SD_VDD", [A(C1, 1), (COL, A(C1, 1)[1])], TRACK, B)
R("SD_VDD", [A(R2, 2), (COL, A(R2, 2)[1])], TRACK, B)
R("SD_VDD", [A(R7, 1), (COL, A(R7, 1)[1])], TRACK, B)
R("SD_VDD", [A(R3, 1), (A(R3, 1)[0], TOPRAIL)], TRACK, B)
R("SD_VDD", [A(R8, 2), (92.4, A(R8, 2)[1])], TRACK, B)
R("SD_VDD", [A(U3, 5), (A(U3, 5)[0], TOPRAIL)], TRACK, B)
R("SD_VDD", [A(U2, 5), (A(R2, 2)[0], A(R2, 2)[1])], TRACK, B)
# ESD: U3 on DO and CLK, U2 on DI and CS, each pair of pins tied under the chip
R("SD_CLK", [CLK_V, (93.1, 64.5), (93.1, 65.6), A(U3, 3), A(U3, 4)], TRACK, B)
R("DAT0", [DO_V, (94.7, DO_V[1]), (A(U3, 1)[0], 65.45), A(U3, 1), A(U3, 6)], TRACK, B)
R("DAT0", [A(U3, 6), A(R7, 2)], TRACK, B)
R("SD_DI", [DI_V, (94.2, 61.75), (A(U2, 3)[0], 61.75), A(U2, 3), A(U2, 4)], TRACK, B)
R("SD_DI", [(A(R8, 1)[0], 61.75), A(R8, 1)], TRACK, B)
R("CARD_CS", [CS_V, (A(U2, 1)[0] - 0.3, CS_V[1]), A(U2, 1), A(U2, 6)], TRACK, B)
R("DAT2", [DAT2_V, (95.5, 64.95), A(R2, 1)], TRACK, B)
R("DAT1", [DAT1_V, A(R3, 2)], TRACK, B)
GND_U2, GND_U3 = (A(U2, 2)[0], 63.1), (A(U3, 2)[0], 67.7)
R("GND", [A(U2, 2), GND_U2], TRACK, B)
V("GND", *GND_U2)
R("GND", [A(U3, 2), GND_U3], TRACK, B)
V("GND", *GND_U3)
# the inverter under J2: LCD_CS below the staircase, on the bottom; its output up to the card's CS via
R("LCD_CS", [BUSVIA[3], (BUSVIA[3][0], 56.2), (91.2, 53.35), (92.75, 53.35), (93.2, 53.8), (93.2, A(U1, 2)[1]), A(U1, 2)], TRACK, B)
R("LCD_CS", [(93.2, A(U1, 2)[1]), A(R4, 2)], TRACK, B)
R("CARD_CS", [A(U1, 4), (95.0, 56.55), (95.0, 60.65), CS_V], TRACK, B)
for u, v in ((104.0, 75.0), (109.0, 75.0), (109.0, 53.5), (85.4, 54.5), (86.3, 66.0), (105.0, 60.0), (85.6, 72.3), (97.3, 73.0)):
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
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    o = z.Outline()
    o.NewOutline()
    for x, y in OUTLINE:
        o.Append(MM(x), MM(y))
    board.Add(z)


def label(text, u, v, size=0.8, rot=0.0, bottom=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(*[MM(c) for c in T(u, v)]))
    t.SetTextAngleDegrees(rot)
    t.SetLayer(pcbnew.B_SilkS if bottom else pcbnew.F_SilkS)
    t.SetMirrored(bottom)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(0.12))
    board.Add(t)


label("OPTIMIST SD", 105.5, 72.0, 0.9)
label("short", 105.5, 70.6, 0.9)
label("J12 cable in", 106.3, 62.0, 0.6)
label("gold DOWN at J2", 106.3, 61.0, 0.6)
label("LCD gold UP", 100.0, 73.5, 0.6, 0, True)
label("DET", 99.0, 74.2, 0.6)
label("after m-card, CC0", 105.0, 66.0, 0.6)

STITCHED = stitch(board, lambda x, y: V("GND", x - 100.0, 200.0 - y), OUTLINE)
print(f"{len(STITCHED)} stitching vias added")

board.GetDesignSettings().SetAuxOrigin(pcbnew.VECTOR2I(MM(X0), MM(YB)))
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
out = os.path.join(HERE, "sd-board.kicad_pcb")
board.Save(out)
print("saved", out)

# ---- JLCPCB's assembly files: the parts list (BOM) and the placement (CPL)
FAB = os.path.join(HERE, "fab")
os.makedirs(FAB, exist_ok=True)
parts = [fp for fp in board.GetFootprints() if not fp.GetReference().startswith(("H", "JP", "TP"))]
groups = {}
for fp in parts:
    groups.setdefault((fp.GetValue(), fp.GetFPID().GetLibItemName().wx_str(), fp.GetFieldText("LCSC")), []).append(fp.GetReference())
with open(os.path.join(FAB, "bom.csv"), "w") as f:
    f.write("Comment,Designator,Footprint,LCSC Part #\n")
    for (value, footprint, lcsc), refs in sorted(groups.items(), key=lambda g: sorted(g[1])[0]):
        f.write(f'"{value}","{",".join(sorted(refs))}","{footprint}","{lcsc}"\n')
with open(os.path.join(FAB, "cpl.csv"), "w") as f:
    f.write("Designator,Mid X,Mid Y,Layer,Rotation\n")
    for fp in sorted(parts, key=lambda fp: fp.GetReference()):
        x, y = pcbnew.ToMM(fp.GetPosition().x) - X0, YB - pcbnew.ToMM(fp.GetPosition().y)
        side = "Bottom" if fp.GetLayer() == B else "Top"
        f.write(f"{fp.GetReference()},{x:.3f}mm,{y:.3f}mm,{side},{fp.GetOrientationDegrees() % 360:.0f}\n")
print("wrote", os.path.join(FAB, "bom.csv"), os.path.join(FAB, "cpl.csv"))

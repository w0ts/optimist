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
v6: J2 (IN, from J12) and J3 (the LCD's cable), 10 pins each, every line through, J2 pin n = J12 pin n = J3's n-th
contact from the left (J3 is turned: its own numbering runs the other way, J3_AT);
J12's pin map as Julius probed it, 2026-10-27, docs/SD-CARD-DESIGN.md section 3; pin 7's 3.3 V still to confirm):
  GND      J2 1, 10 -> J1 6, the shell, U1 GND, R4, the sockets' tabs
  MISO     J2 2 (PC8, the LCD's D/C) -> R1 100 -> J1 7 (DAT0): R1 takes any overlap with the D/C drive
  LCD_CS   J2 3 (PC7) -> U1 A (74LVC1G04 in); R4 47k pull-DOWN (the card deselected while the FM-1 resets)
  CLK      J2 4 (PC9) -> R5 100 -> CLK_SD -> J1 5
  MOSI     J2 5 (PC10) -> R6 100 -> MOSI_SD -> J1 3 (CMD)
  3V3      J2 7 -> J1 4, U1 VCC, C1 10 uF, C2 100 nF, the pull-ups
  CARD_CS  U1 Y  -> J1 2
  DAT1/DAT2  47k pull-ups (unused in SPI mode, not left floating)
  LCD_6, LCD_8, LCD_9: the LCD's own lines (not probed: reset, backlight), only through
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


PLUG_L = 72.6 + 30.0                             # k + o: the plug's left side (above p, 23.5 mm up)
BATTERY_R = 72.6 - 3.0                           # k, but the user's test fit puts the left side (the pole, the room by
                                                 # the battery) 3 mm further left than measured: the board 3 mm wider
POLE = (75.0, 40.0)                              # l, m: the cover's pole there (6.9 mm wide, i); 3 mm left of the
                                                 # measured 78 (the user, after a test fit)
COLLAR_R = (6.9 + 0.4) / 2 + 1.0                # the tray's collar round the pole (make_tray.py): 0.2 play, 1 mm wall
POLE_R = COLLAR_R + 0.3                          # (the board's seat round the collar; it stops the board sliding in)
TRAY_RAIL = 1.2                                  # the tray's rails along the board's sides (a groove each: the user,
                                                 # 2026-10-25, the board slides into the tray, held up and down)
WALL_OUT = -3.0                                  # x: the bottom wall's outside, 3 mm below the board's bottom edge
U0 = BATTERY_R + 0.6 + TRAY_RAIL                 # the board's left edge: its tray's rail 0.6 mm clear of the battery
U1_ = 96.76                                      # its right edge (cut at the card socket when the card was at 84 mm)
CARD_L = (74.4 + U1_) / 2 - 5.5                  # the card socket where it was, in the middle of the board before it
                                                 # grew 3 mm left (the slot in the wall stays): the card's left
                                                 # side 80.1 mm from the board's edge, 81.7 from the left wall's inside
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
# the column and the head (v5): up the right of the speaker plug (85.6 to 90 across, 44 to 51 up: C, D, w1, w2) to
# under the jacks (from 76.8 up: F), where J3 and J2 sit; no tray there, the board 0.8 mm over the FM-1's small parts
COL_L, HEAD_BOT, HEAD_L, HEAD_TOP = 91.5, 51.5, 82.4, 76.7  # (the top right against the jacks; the head one
                                                 # width, from 82.4 for J3 moved left: the user)
OUTLINE = tuple([T(U0, V0), T(U1_, V0), T(U1_, HEAD_TOP), T(HEAD_L, HEAD_TOP), T(HEAD_L, HEAD_BOT),
                 T(COL_L, HEAD_BOT), T(COL_L, V1), (PX + POLE_R, PY),
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
# U1 and its parts above it on the right, 2 mm or more inside the tray's lips (KEEP_OUT below). The board is also the
# splitter (the user, 2026-10-25): two 10-pin 0.5 mm sockets in its head under the jacks, J3 for the LCD's own cable
# (right under the gap between the black jacks, against them: the cable only just reaches) and J2 for a cable from J12
# (lower: the user gets a longer cable). J3 is single-sided, top contact (the LCD's cable arrives gold up: the user's
# ruling, 2026-10-25 review): a cable turned over does not connect. J2 stays the HCTL part (contacts on both faces)
# until the breakout's mirror check picks its face (README, Before ordering).
CARD_C = CARD_L + 5.5                            # (the card, 11 mm wide, taken as centred in its socket: UNMEASURED)
BUS = 13.3                                       # (the 3V3 line across, on the bottom)
FPC10 = "FPC-SMD_10P-P0.50_HC-FPC-05-09-10RLTAG"
FPC10_TOP = "FPC-SMD_10P-P0.50_XUNPU_FPC-0.5AS-10PWBH20"
CARD_PROUD = 0.5                                 # (the card 0.5 mm out past the case's outside, to grip: the user)
J1 = P(LIB, "TF-SMD_TF-01A", "J1", "TF-01A push-push", CARD_C, WALL_OUT - CARD_PROUD + 9.62, 0, "C91145")
J3 = P(LIB, FPC10_TOP, "J3", "FPC 10P 0.5mm top contact (LCD)", 88.0, 72.3, 180, "C3446068")   # (its body 10.8
                                                 # mm wide: 1.5 mm right of v5's J3 to stay on the head; turned so
                                                 # its opening faces the top edge, as v5's, the pins below the body)
J3_AT = {n: 11 - n for n in range(1, 11)}        # (turned, XUNPU's own pin 1 is the right end: J3 is wired by place,
                                                 # line n where v5's pin n was, left to right as on J2)
J2 = P(LIB, FPC10, "J2", "FPC 10P 0.5mm (IN)", 92.4, 57.0, 180, "C5213750")   # (5 mm lower for a 6 cm cable,
                                                 # the user; 1 mm right so the column's lines reach it)
U1 = P(LIB, "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "U1", "74LVC1G04", 91.0, 17.5, 90, "C7827")
R4 = P(LIB, "R0603", "R4", "47k", 89.6, 21.4, 0, "C25819")
C1 = P(LIB, "C0805", "C1", "10uF", 76.0, 17.5, 270, "C15850")
C2 = P(LIB, "C0603", "C2", "100nF", 93.4, 17.0, 90, "C14663")
R1 = P(LIB, "R0603", "R1", "100", CARD_C - 4.36, 17.5, 270, "C22775")   # (100 ohm: 220 likely misses 20 MHz,
                                                 # SD-CARD-DESIGN.md section 4; the D/C overlap is under half a us)
R2 = P(LIB, "R0603", "R2", "47k", 89.6, 12.9, 0, "C25819")
R3 = P(LIB, "R0603", "R3", "47k", CARD_C - 7.6, BUS, 0, "C25819")
R5 = P(LIB, "R0603", "R5", "100", 94.25, 49.0, 90, "C22775")    # (CLK and MOSI in series where the SD branch leaves
R6 = P(LIB, "R0603", "R6", "100", 95.05, 45.5, 90, "C22775")    # the through-lines, at the J2 end of the column)
# ESD at the card's contacts: two USBLC6-2SC6, each line straight through one of its columns (pins 1 and 6 are one
# line, 3 and 4 the other: flow-through, no stub); U3 on DAT0 and CLK with its GND pin right over the card's ground
# contact, U2 on CMD (DI) and CS, turned so its VBUS pin sits over the bottom 3V3 bus
SOT6 = "SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL"
U3 = P(LIB, SOT6, "U3", "USBLC6-2SC6", 82.32, 14.2, 0, "C7519")
U2 = P(LIB, SOT6, "U2", "USBLC6-2SC6", 86.57, 16.6, 180, "C7519")
# the swap block (Julius and the user, 2026-10-27): between J2 and the SD part, each line's two pads joined by a thin
# copper bridge (a solder jumper, bridged: no part to place). If a line from J12 is not what we think, cut its bridge
# and wire any J12 line (a jumper's right pad, or the test pads TP1-TP3 for J12's unprobed pins 6, 8, 9) to the SD
# part's input (a jumper's left pad). Staggered, each row's left pad clear of the rows below it
JUMPER = os.path.join(KICAD_FP, "Jumper.pretty")
TPAD = os.path.join(KICAD_FP, "TestPoint.pretty")
SJ = "SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm"
SWAP = (("JP1", "MISO", "SD_DO", 83.6, 38.6, "DO"), ("JP2", "LCD_CS", "SD_CSIN", 84.8, 35.9, "CS"),
        ("JP3", "CLK_SD", "SD_CLK", 86.0, 33.2, "CLK"), ("JP4", "MOSI_SD", "SD_DI", 87.2, 30.5, "DI"),
        ("JP5", "3V3", "SD_VDD", 88.4, 27.8, "3V3"))
JP = {ref: P(JUMPER, SJ, ref, "cut to swap", u, v, 0, "") for ref, _, _, u, v, _ in SWAP}
TPS = (("TP1", "LCD_6", 83.6, 64.2, "6"), ("TP2", "LCD_8", 83.6, 66.6, "8"), ("TP3", "LCD_9", 83.6, 69.0, "9"),
       ("TP4", "GND", 83.6, 61.8, "G"))
TP = {ref: P(TPAD, "TestPoint_Pad_D1.0mm", ref, f"J12 {lab}", u, v, 0, "") for ref, _, u, v, lab in TPS}

# the tray's lips run over the board's edges (make_tray.py): no part within 2 mm of the left edge, nor of the right
# edge below the right post's top (RIGHT_END there); every placed part's box checked
KEEP_OUT, RIGHT_END = 2.0, 24.0
for fp in board.GetFootprints():
    bb = fp.GetBoundingBox(False)
    left, right = pcbnew.ToMM(bb.GetLeft()) - 100.0, pcbnew.ToMM(bb.GetRight()) - 100.0
    low = 200.0 - pcbnew.ToMM(bb.GetBottom())
    assert left >= U0 + KEEP_OUT or low > V1, f"{fp.GetReference()} on the tray's left lip ({left:.2f})"
    assert right <= U1_ - KEEP_OUT or low >= RIGHT_END, f"{fp.GetReference()} on the tray's right lip ({right:.2f})"

# ---- the nets: J12's pin map as probed (Julius, 2026-10-27), the same on J2 and J3 (every line straight through, J3
# pin n = J2 pin n; the cable from J12 must arrive unmirrored: the breakout's check). Pin 7 as 3V3 is still to confirm
# (a reset line reads 3.3 V as well): the swap block lets a wrong guess be rewired by hand
LCD_PINS = {1: "GND", 2: "MISO", 3: "LCD_CS", 4: "CLK", 5: "MOSI", 6: "LCD_6", 7: "3V3", 8: "LCD_8",
            9: "LCD_9", 10: "GND", 11: "GND", 12: "GND"}
for num, name in ((1, "DAT2"), (2, "CARD_CS"), (3, "SD_DI"), (4, "SD_VDD"), (5, "SD_CLK"), (6, "GND"), (7, "DAT0"),
                  (8, "DAT1"), (10, "GND"), (11, "GND"), (12, "GND"), (13, "GND")):
    connect(J1, num, name)
for num, name in LCD_PINS.items():
    connect(J2, num, name)
    connect(J3, J3_AT.get(num, num), name)
for fp in (J2, J3):
    for num in (1, 10):                          # (the 0.5 mm pitch leaves room for one thermal spoke only: the
        pad(fp, num).SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)   # ground pins join the pour solid)
for num, name in ((2, "SD_CSIN"), (3, "GND"), (4, "CARD_CS"), (5, "SD_VDD")):
    connect(U1, num, name)
for fp, a, b in ((R1, "SD_DO", "DAT0"), (R2, "DAT2", "SD_VDD"), (R3, "SD_VDD", "DAT1"), (R4, "GND", "SD_CSIN"),
                 (C1, "SD_VDD", "GND"), (C2, "SD_VDD", "GND"), (R5, "CLK_SD", "CLK"), (R6, "MOSI_SD", "MOSI")):
    connect(fp, 1, a)
    connect(fp, 2, b)
for fp, io1, io2 in ((U3, "DAT0", "SD_CLK"), (U2, "CARD_CS", "SD_DI")):
    for num, name in ((1, io1), (6, io1), (3, io2), (4, io2), (2, "GND"), (5, "SD_VDD")):
        connect(fp, num, name)
for ref, jside, sdside, _, _, _ in SWAP:        # (pad 1 left: the SD part's side; pad 2 right: J12's)
    connect(JP[ref], 1, sdside)
    connect(JP[ref], 2, jside)
for ref, name, _, _, _ in TPS:
    connect(TP[ref], 1, name)

# ---- the column (right of the speaker plug), top layer, one lane each from J2 down to the swap block. The order is
# J2's (no crossing): MISO, LCD_CS, a ground guard, CLK, MOSI, 3V3: MISO's neighbour is the quiet chip select, CLK's
# edges are a ground lane away from it (the 2026-10-25 review); the bottom layer under the column is solid ground
LANE = {"MISO": 92.1, "LCD_CS": 92.75, "GUARD": 93.45, "CLK": 94.25, "MOSI": 95.05, "3V3": 95.9}
PIN = {name: num for num, name in LCD_PINS.items() if name in LANE}
ROW = {jside: (ref, v) for ref, jside, _, _, v, _ in SWAP}
SLANT = 52.4                                     # (from here up, every lane slants left at 45 degrees onto its J2 pin)
for name in ("MISO", "LCD_CS", "CLK", "MOSI", "3V3"):
    x, tx = LANE[name], A(J2, PIN[name])
    top = [(tx[0], SLANT + x - tx[0]), tx]
    w = POWER if name == "3V3" else TRACK
    if name in ("CLK", "MOSI"):                  # (the column's top through R5 / R6, the SD side below them)
        r = R5 if name == "CLK" else R6
        R(name, [A(r, 2), (x, SLANT), *top], w)
        ref, v = ROW[name + "_SD"]
        R(name + "_SD", [A(r, 1), (x, v), A(JP[ref], 2)], w)
    else:
        ref, v = ROW[name]
        R(name, [A(JP[ref], 2), (x, v), (x, SLANT), *top], w)
GUARD_V = (34.5, 39.0, 44.0, 51.0)               # (the guard's ends and its vias to the bottom pour, about 5 mm apart;
R("GND", [(LANE["GUARD"], GUARD_V[0]), (LANE["GUARD"], GUARD_V[-1])])   # none beside R5's pads)

# ---- from the swap block down to the SD part, top: each left pad drops in turn (the rows staggered so no drop
# crosses a lower row); SD_CSIN alone crosses CLK and DI, on the bottom (a via each end: it changes only at a card
# session's start and end); 3V3 (SD_VDD after its jumper) goes back right to the rail down the right edge
C = {n: A(J1, n) for n in range(1, 9)}
L = {ref: A(JP[ref], 1) for ref in JP}
R("SD_DO", [L["JP1"], (L["JP1"][0], 29.0), (C[7][0], 27.27), A(R1, 1)])
R("DAT0", [A(R1, 2), (A(U3, 1)[0], A(R1, 2)[1]), A(U3, 6), A(U3, 1), (A(U3, 1)[0], C[7][1])])
R("SD_CLK", [L["JP3"], (L["JP3"][0], 27.4), (A(U3, 3)[0], 25.3), A(U3, 4), A(U3, 3), (A(U3, 3)[0], C[5][1])])
R("SD_DI", [L["JP4"], (L["JP4"][0], 26.5), (A(U2, 3)[0], 25.57), A(U2, 4), A(U2, 3), C[3]])
CS_IN, CS_VIA = (L["JP2"][0], 34.5), (91.0, 21.4)
R("SD_CSIN", [L["JP2"], CS_IN])
R("SD_CSIN", [CS_IN, (CS_VIA[0], CS_IN[1] - (CS_VIA[0] - CS_IN[0])), CS_VIA], TRACK, pcbnew.B_Cu)
R("SD_CSIN", [A(R4, 2), CS_VIA, A(U1, 2)])
R("CARD_CS", [A(U1, 4), (88.3, A(U1, 4)[1]), (88.3, 18.6), (A(U2, 6)[0], 18.6), A(U2, 6), A(U2, 1),
              (A(U2, 1)[0], 13.6), (C[2][0], 12.8), C[2]])
R("DAT2", [C[1], (C[1][0], A(R2, 1)[1]), A(R2, 1)])
RAIL = LANE["3V3"]
R("SD_VDD", [L["JP5"], (L["JP5"][0], 26.2), (RAIL, 26.2), (RAIL, 16.2), A(C2, 1), A(U1, 5)], POWER)
R("SD_VDD", [(RAIL, 16.2), (RAIL, A(R2, 2)[1]), A(R2, 2)], POWER)
R("DAT1", [C[8], (C[8][0], BUS), A(R3, 2)])
R("GND", [C[6], A(U3, 2)])
R("SD_VDD", [C[4], (C[4][0], BUS)], POWER)
R("SD_VDD", [A(U3, 5), (A(U3, 5)[0], 16.5)])
R("SD_VDD", [A(U2, 5), (A(U2, 5)[0], 14.2)])
R("GND", [A(U2, 2), (A(U2, 2)[0], 19.3)])
R("SD_VDD", [A(R3, 1), (76.0, BUS)], POWER)
R("SD_VDD", [A(C1, 1), (76.0, 19.8)], POWER)
R("GND", [A(U1, 3), (89.3, 19.6)])
R("GND", [A(R4, 1), (88.0, 21.4)])
R("GND", [A(C2, 2), (93.4, 18.7)])
R("GND", [A(C1, 2), (77.4, A(C1, 2)[1])])

# ---- J3 to J2: every line straight through (down from J3's pins, a step right, down onto J2's pins from above), the
# steps 1 mm apart for a via on the unprobed lines (6, 8, 9: to their test pads); the ground pins join the sockets'
# tabs (above J3's pins) and the pours
TURN = {n: 69.0 - (9 - n) * 1.0 for n in range(2, 10)}
for n in range(2, 10):
    a, b = A(J3, J3_AT[n]), A(J2, n)
    R(LCD_PINS[n], [a, (a[0], TURN[n]), (b[0], TURN[n]), b], POWER if LCD_PINS[n] == "3V3" else TRACK)
for n in (1, 10):
    g = A(J3, J3_AT[n])
    tab = min((A(J3, 11), A(J3, 12)), key=lambda t: abs(t[0] - g[0]))
    R("GND", [g, (g[0], 72.5), (tab[0], 72.5)])
B = pcbnew.B_Cu
TP_VIA = 84.9                                    # (each test pad's via; the bottom runs from the line's step to it)
for (ref, name, u, v, _), jog in zip(TPS[:3], (86.3, 85.6, None)):
    n = int(name[-1])
    mid = ((A(J3, J3_AT[n])[0] + A(J2, n)[0]) / 2, TURN[n])
    path = [mid, (jog, TURN[n]), (jog, v), (TP_VIA, v)] if jog else [mid, (TP_VIA, v)]
    R(name, path, TRACK, B)
    V(name, *mid)
    V(name, TP_VIA, v)
    R(name, [(TP_VIA, v), A(TP[ref], 1)])

# ---- the bottom: 3V3 (SD_VDD) across, from the rail to the card's VDD contact, the ESD chips' VBUS and R3
BUS_X = (76.0, A(U3, 5)[0], C[4][0], A(U2, 5)[0], RAIL)
R("SD_VDD", [(76.0, 19.8), *[(x, BUS) for x in BUS_X]], POWER, B)
R("SD_VDD", [(A(U3, 5)[0], 16.5), (A(U3, 5)[0], BUS)], TRACK, B)
R("SD_VDD", [(A(U2, 5)[0], 14.2), (A(U2, 5)[0], BUS)], TRACK, B)
for u, v in ((76.0, 19.8), (76.0, BUS), (C[4][0], BUS), (RAIL, BUS), (A(U3, 5)[0], 16.5), (A(U2, 5)[0], 14.2)):
    V("SD_VDD", u, v)
V("SD_CSIN", *CS_IN)
V("SD_CSIN", *CS_VIA)
for v in GUARD_V:
    V("GND", LANE["GUARD"], v)
for u, v in ((89.3, 19.6), (88.0, 21.4), (93.4, 18.7), (77.4, A(C1, 2)[1]), (A(U2, 2)[0], 19.3),
             (76.0, 5.0), (76.0, 25.0), (79.0, 25.0), (95.9, 6.0), (76.0, 34.0),   # (the pours stitched, the
             (87.0, 53.5), (83.3, 71.0), (95.9, 53.5), (95.9, 64.0), (95.7, 74.5)):   # head's too)
    V("GND", u, v)
R("GND", [A(J2, 1), (A(J2, 1)[0], 59.2), (88.6, 59.2)])
R("GND", [A(J2, 10), (A(J2, 10)[0], 59.2), (95.4, 59.2)])
V("GND", 88.6, 59.2)
V("GND", 95.4, 59.2)

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


label("OPTIMIST SD", *T(77.5, 29.0), 0.8, 90)
for _, _, _, u, v, lab in SWAP:                  # (the swap block: each row's line; J12's side right, the card's left)
    label(lab, *T(u - 2.2, v), 0.6)
label("CUT+WIRE", *T(88.6, 39.2), 0.6)
for _, _, u, v, lab in TPS:
    label(lab, *T(u, v + 1.0), 0.6)
# which way each cable goes in: J3 touches the top face only (the LCD's cable arrives gold up, the user); J2 still
# touches both, so a cable turned over there connects MIRRORED (pin 1 on pin 10) until its face is picked
label("LCD: gold UP", *T(89.6, 69.9), 0.7)
label("IN: gold DOWN", *T(92.2, 53.0), 0.7)

board.GetDesignSettings().SetAuxOrigin(pcbnew.VECTOR2I(MM(X0), MM(YB)))   # (the lower left: the Gerbers, the drill
pcbnew.ZONE_FILLER(board).Fill(board.Zones())                                   # files and cpl.csv share it)
out = os.path.join(HERE, "sd-board.kicad_pcb")
board.Save(out)
print("saved", out)

# ---- JLCPCB's assembly files: the parts list (BOM) and the placement (CPL); the holes are not parts
FAB = os.path.join(HERE, "fab")
os.makedirs(FAB, exist_ok=True)
parts = [fp for fp in board.GetFootprints() if not fp.GetReference().startswith(("H", "JP", "TP"))]
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

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
v6: J2 (IN, from J12) and J3 (the LCD's cable), 10 pins each, every line through, J12 pin n = J2's and J3's n-th
contact from the left (both turned: their own numbering runs the other way, J2_AT, J3_AT);
J12's pin map as Julius probed it, 2026-10-27, docs/SD-CARD-DESIGN.md section 3; pin 7's 3.3 V confirmed 2026-10-28; J12 counts from the right):
  GND      J2 1, 10 -> J1 6, the shell, U1 GND, R4, the sockets' tabs
  MISO     J2 2 (PC8, the LCD's D/C) -> R1 91 -> J1 7 (DAT0): R1 takes any overlap with the D/C drive; R7 47k
           pulls DAT0 up at the card (an empty slot reads 0xFF without PC8's own pull-up)
  LCD_CS   J2 3 (PC7) -> U1 A (74LVC1G04 in); R4 47k pull-DOWN (the card deselected while the FM-1 resets)
  CLK      J2 4 (PC9) -> R5 240 -> CLK_SD -> J1 5
  MOSI     J2 5 (PC10) -> R6 240 -> MOSI_SD -> J1 3 (CMD)
  3V3      J2 7 -> (JP5) SD_VIN -> FB1 ferrite -> SD_VDD: J1 4, C3 100 nF by it, U1 VCC, C1 10 uF, C2 100 nF, the
           pull-ups (the card's hot insertion kept off the LCD's rail: Julius's review, 2026-10-28)
  CARD_CS  U1 Y  -> J1 2
  DAT1/DAT2  47k pull-ups (unused in SPI mode, not left floating)
  CARD_DET J1 9 -> TP5 (the socket's card switch, unused by the firmware: a pad to try it)
  LCD_6, LCD_8, LCD_9: the LCD's own lines (not probed: reset, backlight), only through
"""
import math
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True                  # (no lib/__pycache__ in the repository)
sys.path.insert(0, os.path.join(HERE, "lib"))
from stitch import stitch   # (lib/stitch.py: after m-card's stitch(), CC0-1.0)
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


# v7 (the user, 2026-11-04): the swap block's solder jumpers are gone, each line joined by plain copper: the card side's net
# names are the J12 side's (the schematic's the same, make_schematic.py)
ALIAS = {"SD_DO": "MISO", "SD_CSIN": "LCD_CS", "SD_CLK": "CLK_SD", "SD_DI": "MOSI_SD", "SD_VIN": "3V3"}


def net(name):
    name = ALIAS.get(name, name)
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
    for p in fp.Pads():                          # (every pad of that number: the twins' pads are a top pad plus a hole)
        if p.GetNumber() == str(num):
            p.SetNet(net(name))


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
# ruling, 2026-10-25 review): a cable turned over does not connect. J2 is single-sided too, bottom contact (2026-10-28):
# at J12 the cable's contacts face the FM-1 board, and round the edge they still face it, so at J2 they face our board
# (gold DOWN); with J12 counting from the right and both openings facing the jacks' edge, J12 pin 1 lands on J2's
# first contact from the left (no mirror, type A cable; README, The FFC sockets' pins).
CARD_C = CARD_L + 5.5                            # (the card, 11 mm wide, taken as centred in its socket: UNMEASURED)
DY = 1.0                                         # (v7, the minor update, 2026-11-04, the user: the card socket 1 mm inward, away
                                                 # from the bottom edge and wall, and the SD part beside it with it)
BUS = 13.3 + DY                                       # (the 3V3 line across, on the bottom)
FPC10_BOT = "FPC-SMD_10P-P0.50_XUNPU_FPC-05F-10PH20"
FPC10_TOP = "FPC-SMD_10P-P0.50_XUNPU_FPC-0.5AS-10PWBH20"
CARD_PROUD = 0.5                                 # (the card 0.5 mm out past the case's outside, to grip: the user)
J1 = P(LIB, "TF-SMD_TF-01A", "J1", "TF-01A push-push", CARD_C, WALL_OUT - CARD_PROUD + 9.62 + DY, 0, "C91145")
J3_V = 71.7                                      # (v7, the user, 2026-11-04: J3 lower again, as v5: 0.6 mm under v6's 72.3, whose
                                                 # opening sat 0.6 mm higher than v5's; how low it may go is for the mockup)
J3 = P(LIB, FPC10_TOP, "J3", "FPC 10P 0.5mm top contact", 88.0, J3_V, 180, "C3446068")   # (its body 10.8
                                                 # mm wide: 1.5 mm right of v5's J3 to stay on the head; turned so
                                                 # its opening faces the top edge, as v5's, the pins below the body)
J3_AT = {n: 11 - n for n in range(1, 11)}        # (turned, XUNPU's own pin 1 is the right end: J3 is wired by place,
                                                 # line n where v5's pin n was, left to right as on J2)
# v7 (the user, 2026-11-04): J2 on the board's BOTTOM, at its old height (58.5 mm up, toward the card reader, the measure
# of v6) and in J3's alignment (x 88.0, moved left from v6's 91.3): the same footprint turned over (left to right), its
# opening to the top edge as J3's, and the SAME part as J3 (the user: top and bottom connectors alike). The cable from J12
# runs straight under the board (no wrap round the edge), so its gold keeps facing the FM-1 board, away from ours: a
# top-contact part on the bottom face has its contacts down. Seen from the top, the flipped part's pad n is at J3's pin n in x.
J2_V = 58.5
J2 = P(LIB, FPC10_TOP, "J2", "FPC 10P 0.5mm top contact", 88.0, J2_V, 180, "C3446068")
J2.Flip(J2.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
assert J2.GetLayer() == pcbnew.B_Cu, "J2 is not on the bottom"
J2_AT = {n: n for n in range(1, 13)}             # (flipped: its pad n is under J3's pin n in x, wired by number)
for n in range(1, 11):
    assert abs(at(J3, J3_AT[n])[0] - at(J2, J2_AT[n])[0]) < 0.005, f"J2 pad {n} is not in line with J3's pin {n}"
U1 = P(LIB, "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BR", "U1", "74LVC1G04", 91.0, 17.5 + DY, 90, "C7827")
R4 = P(LIB, "R0603", "R4", "47k", 89.6, 21.4 + DY, 0, "C25819")
C1 = P(LIB, "C0805", "C1", "10uF", 76.0, 17.5 + DY, 270, "C15850")
C2 = P(LIB, "C0603", "C2", "100nF", 93.4, 17.0 + DY, 90, "C14663")
# R1, R5, R6: the values the bench may change (Julius: room to retune them), on KiCad's 0603 hand-solder pads (longer,
# an iron reaches each end; the same JLC parts)
RTUNE = os.path.join(KICAD_FP, "Resistor_SMD.pretty")
R_HAND = "R_0603_1608Metric_Pad0.98x0.95mm_HandSolder"
R1 = P(RTUNE, R_HAND, "R1", "150", CARD_C - 4.36, 17.6 + DY, 270, "C22808")   # (91 ohm, 2026-11-06 evening: 15 MHz on v6 bodged to 89 ohm; 150 was the 12 MHz value, v6 with R1 150, 2026-11-05; v6 and Julius had 220 and 270 against a card left in SPI mode
# 2026-11-09 (the user will not hand-solder SMD on this board; the values are under test): the fab places the highest values proven
# screen-safe at 60 MHz, 330 / 330 / 150 (cold boot clean on pink 2026-11-06, card at 12 MHz), JLC basic parts; the twins only
# lower a value with a leaded part, so the sweep result (BENCH-SHEET test 7) is reached from here without touching an 0603
                                                 # driving DO against D/C, 15 mA at 220; cold boot with the card in passed at 330/150, 2026-11-06; 240/91 needs the LCD at 30 MHz)
R7 = P(LIB, "R0603", "R7", "47k", 79.2, 17.5 + DY, 270, "C25819")   # (DAT0's pull-up at the card, the SD spec's 10-100k)
R8 = P(LIB, "R0603", "R8", "47k", 88.6, 23.4 + DY, 0, "C25819")   # (the minor update, 2026-11-04, as m-card's R8: CMD's
                                                 # pull-up at the card, 47k to SD_VDD (the SD spec's 10-100k), on the
                                                 # card side of R6; the same part as the other pull-ups)
C3 = P(LIB, "C0603", "C3", "100nF", 84.52, 19.9 + DY, 90, "C14663")   # (the card's own 100 nF, over its VDD contact's line)
FB1 = P(LIB, "R0603", "FB1", "600R@100MHz", 93.5, 25.3, 0, "C1002")   # (GZ1608D601TF, 0.45 ohm, 200 mA: in
                                                 # series from JP5, the card's supply apart from the LCD's; JieLi's
                                                 # AC791N guide has 1 ohm there, the bead the same footprint)
R2 = P(LIB, "R0603", "R2", "47k", 89.6, 12.9 + DY, 0, "C25819")
R3 = P(LIB, "R0603", "R3", "47k", CARD_C - 7.6, BUS, 0, "C25819")
R5 = P(RTUNE, R_HAND, "R5", "330", 94.25, 49.0, 90, "C23138")    # (CLK and MOSI in series where the SD branch leaves
R6 = P(RTUNE, R_HAND, "R6", "330", 95.05, 45.5, 90, "C23138")    # the through-lines, at the J2 end of the column
                                                 # (stock firmware with a card in its native mode: blank with CLK direct, distorted at
                                                 # 150 and fine at 510 on Julius prototype; 330 passed a cold boot at 60 MHz LCD; 245 bodged: black at 60 MHz, fine at 30 MHz, 2026-11-06)
# ESD at the card's contacts: two USBLC6-2SC6, each line straight through one of its columns (pins 1 and 6 are one
# line, 3 and 4 the other: flow-through, no stub); U3 on DAT0 and CLK with its GND pin right over the card's ground
# contact, U2 on CMD (DI) and CS, turned so its VBUS pin sits over the bottom 3V3 bus
SOT6 = "SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL"
U3 = P(LIB, SOT6, "U3", "USBLC6-2SC6", 82.32, 14.2 + DY, 0, "C7519")
U2 = P(LIB, SOT6, "U2", "USBLC6-2SC6", 86.57, 16.6 + DY, 180, "C7519")
# the swap block (Julius and the user, 2026-10-27): between J2 and the SD part, each line's two pads joined by a thin
# copper bridge (a solder jumper, bridged: no part to place). If a line from J12 is not what we think, cut its bridge
# and wire any J12 line (a jumper's right pad, or the test pads TP1-TP3 for J12's unprobed pins 6, 8, 9) to the SD
# part's input (a jumper's left pad). Staggered, each row's left pad clear of the rows below it
JUMPER = os.path.join(KICAD_FP, "Jumper.pretty")
TPAD = os.path.join(KICAD_FP, "TestPoint.pretty")
SJ = "SolderJumper-2_Cut_P3.0mm_Pad1.5x2.0mm"      # (lib/: 1.5 x 2 mm pads, a 1.5 mm bare bridge to cut; the user)
SWAP = (("JP1", "MISO", "SD_DO", 83.6, 38.4, "DO"), ("JP2", "LCD_CS", "SD_CSIN", 85.1, 35.7, "CS"),
        ("JP3", "CLK_SD", "SD_CLK", 86.6, 33.0, "CLK"), ("JP4", "MOSI_SD", "SD_DI", 88.1, 30.3, "DI"),
        ("JP5", "3V3", "SD_VIN", 89.6, 27.6, "3V3"))
JPU = {ref: (u, v) for ref, _, _, u, v, _ in SWAP}   # (v7: no jumper parts; the old pads' places, 1.5 mm each side, are the join's ends)
# The twins (the user, 2026-11-08): for each tuning resistor a pair of plain top pads (1.6 x 2.4 mm, the old cut-and-wire
# pads' size) wired in parallel with it, so a value is trimmed by soldering a leaded part flat onto them, never onto
# the 0603's ends. No hole, no via, no line of their own (the user, that evening: the leaded part's own legs do the
# wiring, so each pad only has to sit where its net already is on the top). Not placed by the fab (no LCSC part, left
# out of the BOM and the placement). RT1 (across R1, DO) stands left of the swap block above R7 and R1, the lib
# footprint (7.62 mm pitch, pad 1 the lower pad), both pads wired on the top. RT5 (across R5, CLK) and RT6 (across R6,
# MOSI) have their two pads apart: pad 1 in the head on its lane above R5 (CLK's at 53.0 on the CLK lane; MOSI's at
# 56.2, where its lane is stepped right to HEAD_MOSI and 3V3's to HEAD_3V3 so a 1.6 mm pad fits between its
# neighbours), pad 2 on the swap block's row of the SD net, right of the row's end where only that line runs (CLK_SD's
# row at 33.0, MOSI_SD's at 30.3). The part lies along the column's right edge over the masked lanes, 20 and 26 mm pad
# to pad (a 1/4 W part has 28 mm of leg each side; a sleeve on the long leg keeps the two parts' legs apart). Each
# footprint is written here (twins.pretty, one per twin: the pad spacing is the board's). Earlier that day: all three
# in a row left of the swap block (6 vias), RT5/RT6 in the head's right strip (2 vias), lying between the swap block's
# rows under the column (4 vias, 8 once the user had the holes taken out of the pads); the user: fewer vias.
TWIN = "R_twin_P7.62"
RT1 = P(LIB, TWIN, "RT1", "parallel to R1", 80.6, 24.0, 90, "")   # (the lower pad; the upper one 7.62 higher, 31.62)
TWINS = os.path.join(HERE, "twins.pretty")
TWIN_AT = {"RT5": ((94.25, 53.0), (91.5, 33.0)),   # (pad 1 in the head: on the CLK lane / where MOSI's lane is stepped to;
           "RT6": ((94.85, 56.2), (92.0, 30.3))}   # pad 2 on CLK_SD's / MOSI_SD's row, 0.75 and 0.9 left of the guard's lane)
HEAD_MOSI, HEAD_3V3, STEP_V = 95.4, 96.05, 50.75   # (in the head MOSI's lane runs 0.35 further right and 3V3's 0.15, stepping
                                                 # above R5's top pad: 0.25 from RT5's and RT6's pads, 3V3 0.56 from the edge)


def twin_mod(ref):
    """The footprint of RT5 or RT6: pad 1 at the origin (1.6 wide, 2.4 along the lane), pad 2 where the board needs it
    (2.4 wide along the row), a courtyard for the mockup where a 1/4 W body lies, left of pad 2 along the row."""
    (u1, v1), (u2, v2) = TWIN_AT[ref]
    du, dy = u2 - u1, v1 - v2                    # (footprint y points down)
    return "\n".join((
        f'(footprint "R_twin_{ref}" (version 20240108) (generator "make_board.py") (layer "F.Cu")',
        f'  (descr "{ref}: two plain 1.6 x 2.4 mm top pads {((du ** 2 + dy ** 2) ** 0.5):.1f} mm apart for a leaded resistor in'
        f' parallel with R{ref[2:]}: pad 1 in the head on the lane, pad 2 on the row of the swap block; the legs of the part do the wiring'
        f' (the user, 2026-11-08). Written by make_board.py. Not placed by the fab.")',
        '  (tags "resistor parallel tuning pads")',
        "  (attr exclude_from_bom exclude_from_pos_files)",
        f'  (fp_text reference "REF**" (at 0 -2 0) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.12))))',
        f'  (fp_text value "VAL**" (at {du:.3f} {dy + 2:.3f} 0) (layer "F.Fab") (effects (font (size 0.8 0.8) (thickness 0.12))))',
        f'  (fp_rect (start {du - 8.0:.3f} {dy - 1.25:.3f}) (end {du - 1.5:.3f} {dy + 1.25:.3f}) (stroke (width 0.05) (type solid))'
        ' (fill none) (layer "F.CrtYd"))',
        '  (pad "1" smd rect (at 0 0) (size 1.6 2.4) (layers "F.Cu" "F.Mask"))',
        f'  (pad "2" smd rect (at {du:.3f} {dy:.3f}) (size 2.4 1.6) (layers "F.Cu" "F.Mask"))',
        ")", ""))


os.makedirs(TWINS, exist_ok=True)
for ref in TWIN_AT:
    with open(os.path.join(TWINS, f"R_twin_{ref}.kicad_mod"), "w") as f:
        f.write(twin_mod(ref))
RT5 = P(TWINS, "R_twin_RT5", "RT5", "parallel to R5", *TWIN_AT["RT5"][0], 0, "")
RT6 = P(TWINS, "R_twin_RT6", "RT6", "parallel to R6", *TWIN_AT["RT6"][0], 0, "")
assert abs(A(RT1, 2)[1] - A(RT1, 1)[1] - 7.62) < 0.01, "RT1: pad 2 is not the upper pad"
for fp in (RT5, RT6):
    for num in (1, 2):
        want = TWIN_AT[fp.GetReference()][num - 1]
        assert all(abs(a - b) < 0.01 for a, b in zip(A(fp, num), want)), f"{fp.GetReference()} pad {num} is not at {want}"
    assert JPU["JP5"][1] + POWER / 2 + 0.4 <= A(fp, 2)[1] - 0.8 and A(fp, 2)[1] + 0.8 + 0.4 <= JPU["JP1"][1] - TRACK / 2


def JPA(ref, num):
    u, v = JPU[ref]
    return (u - 1.5 if num == 1 else u + 1.5, v)
TPS = (("TP5", "CARD_DET", 74.9, 7.2 + DY, "DET"),)   # (v7: the user dropped the J12 test pads TP1-TP4, 2026-11-04; TP5 is the card switch)
TP = {ref: P(TPAD, "TestPoint_Pad_D1.5mm", ref, f"J12 {lab}", u, v, 0, "") for ref, _, u, v, lab in TPS}

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
# pin n = J2 pin n; the cable from J12 must arrive unmirrored: the breakout's check). Pin 7 is 3V3 (Julius's prototype
# runs the card from it, 2026-10-28); the swap block still lets any line be rewired by hand
LCD_PINS = {1: "GND", 2: "MISO", 3: "LCD_CS", 4: "CLK", 5: "MOSI", 6: "LCD_6", 7: "3V3", 8: "LCD_8",
            9: "LCD_9", 10: "GND", 11: "GND", 12: "GND"}
for num, name in ((1, "DAT2"), (2, "CARD_CS"), (3, "SD_DI"), (4, "SD_VDD"), (5, "SD_CLK"), (6, "GND"), (7, "DAT0"),
                  (8, "DAT1"), (9, "CARD_DET"), (10, "GND"), (11, "GND"), (12, "GND"), (13, "GND")):
    connect(J1, num, name)
for num, name in LCD_PINS.items():
    connect(J2, J2_AT.get(num, num), name)
    connect(J3, J3_AT.get(num, num), name)
for fp in (J2, J3):
    for num in (1, 10):                          # (the 0.5 mm pitch leaves room for one thermal spoke only: the
        pad(fp, num).SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)   # ground pins join the pour solid)
for num, name in ((2, "SD_CSIN"), (3, "GND"), (4, "CARD_CS"), (5, "SD_VDD")):
    connect(U1, num, name)
for fp, a, b in ((R1, "SD_DO", "DAT0"), (R2, "DAT2", "SD_VDD"), (R3, "SD_VDD", "DAT1"), (R4, "GND", "SD_CSIN"),
                 (C1, "SD_VDD", "GND"), (C2, "SD_VDD", "GND"), (R5, "CLK_SD", "CLK"), (R6, "MOSI_SD", "MOSI"),
                 (R7, "SD_VDD", "DAT0"), (C3, "SD_VDD", "GND"), (FB1, "SD_VIN", "SD_VDD"), (R8, "SD_DI", "SD_VDD"),
                 (RT1, "DAT0", "SD_DO"), (RT5, "CLK", "CLK_SD"), (RT6, "MOSI", "MOSI_SD")):   # (the twins: RT1's pad 1
                                                 # below, RT5's and RT6's pad 1 on the left, the J12 side)
    connect(fp, 1, a)
    connect(fp, 2, b)
for fp, io1, io2 in ((U3, "DAT0", "SD_CLK"), (U2, "CARD_CS", "SD_DI")):
    for num, name in ((1, io1), (6, io1), (3, io2), (4, io2), (2, "GND"), (5, "SD_VDD")):
        connect(fp, num, name)
for ref, name, _, _, _ in TPS:
    connect(TP[ref], 1, name)

# ---- the column (right of the speaker plug), top layer, one lane each from J2 down to the swap block. The order is
# J2's (no crossing): MISO, LCD_CS, a ground guard, CLK, MOSI, 3V3: MISO's neighbour is the quiet chip select, CLK's
# edges are a ground lane away from it (the 2026-10-25 review); the bottom layer under the column is solid ground
LANE = {"MISO": 92.1, "LCD_CS": 92.75, "GUARD": 93.45, "CLK": 94.25, "MOSI": 95.05, "3V3": 95.9}
PIN = {name: num for num, name in LCD_PINS.items() if name in LANE}
ROW = {jside: (ref, v) for ref, jside, _, _, v, _ in SWAP}
# v7: J3's signal pins 2-9 fan out on the top (45 degrees, the pad pitch 0.5 spread to 1.0) to a column each, which runs
# down over J2's place (J2's body is on the bottom: the top is free) to a via on one row (V_ROW2, just above J2's tabs);
# J2's pads, on the bottom, fan out the same way up to that via (no via in a pad). The lanes end at the same vias, coming
# up from below: each slants left at 45 degrees (x + v constant, the slants 1.0 mm apart across) and ends 1 mm under the row
V_ROW, V_ROW2 = 67.2, 62.9                       # (J3's fan ends at V_ROW, its deepest knee, 2.25 mm lower, 0.47 mm under J3's
                                                 # pads; V_ROW2: J2's deepest knee 2.25 mm lower, its tabs top out at 61.04)
VIA_X = {n: 88.0 + (n - 5.5) * 1.0 for n in range(2, 10)}   # (no two vias or columns closer than 0.4 mm, centred on J3)
for name in ("MISO", "LCD_CS", "CLK", "MOSI", "3V3"):
    x, X = LANE[name], VIA_X[PIN[name]]
    vs = V_ROW2 - 1.0 - (x - X)                  # (from here up, the lane slants left onto its via)
    top = [(X, V_ROW2 - 1.0), (X, V_ROW2)]
    w = POWER if name == "3V3" else TRACK
    if name == "CLK":                            # (the column's top through R5, up through RT5's head pad, slanting left from it)
        R(name, [A(R5, 2), (x, vs), *top], w)
    elif name == "MOSI":                         # (through R6; stepped right above R5 into RT6's head pad, straight on to its via's foot)
        R(name, [A(R6, 2), (x, STEP_V + 0.15), (HEAD_MOSI, STEP_V + 0.5), (HEAD_MOSI, A(RT6, 1)[1]), A(RT6, 1), *top], w)
    elif name == "3V3":                          # (stepped right with it, up past RT6's pad, straight on to its via's foot)
        ref, v = ROW[name]
        R(name, [JPA(ref, 2), (x, v), (x, STEP_V), (HEAD_3V3, STEP_V + 0.15), (HEAD_3V3, 58.8), *top], w)
    else:
        ref, v = ROW[name]
        R(name, [JPA(ref, 2), (x, v), (x, vs), *top], w)
    if name in ("CLK", "MOSI"):                  # (the SD side below R5 / R6, down the lane onto its row)
        ref, v = ROW[name + "_SD"]
        R(name + "_SD", [A(R5 if name == "CLK" else R6, 1), (x, v), JPA(ref, 2)], w)
GUARD_V = (34.5, 39.0, 44.0, 51.0)               # (the guard's ends and its vias to the bottom pour, about 5 mm apart; none
R("GND", [(LANE["GUARD"], GUARD_V[0]), (LANE["GUARD"], GUARD_V[-1])])   # beside R5's pads; the 34.5 via 0.4 above RT5's row pad)

# ---- from the swap block down to the SD part, top: each left pad drops in turn (the rows staggered so no drop
# crosses a lower row); SD_CSIN alone crosses CLK and DI, on the bottom (a via each end: it changes only at a card
# session's start and end); 3V3 (SD_VDD after its jumper) goes back right to the rail down the right edge
C = {n: A(J1, n) for n in range(1, 9)}
L = {ref: JPA(ref, 1) for ref in JPU}
for ref, jside, _, _, _, _ in SWAP:              # (v7: the copper that replaces each jumper, J12's side to the card's)
    R(jside, [JPA(ref, 1), JPA(ref, 2)], POWER if jside == "3V3" else TRACK)
# (2026-11-08: SD_DO drops straight down the swap block's left side to just above R1 before it steps left onto R1's pad:
# the twins' column is left of that drop, RT1's upper pad reaches it, its lower pad goes down between R7 and R1 to DAT0)
R("SD_DO", [L["JP1"], (L["JP1"][0], A(R1, 1)[1] + (L["JP1"][0] - A(R1, 1)[0])), A(R1, 1)])
R("SD_DO", [A(RT1, 2), (L["JP1"][0], A(RT1, 2)[1])])
DAT0_X = 80.2                                    # (between R7's and R1's pads, 0.4 mm clear of each)
_d7, _d1 = A(R7, 2), A(R1, 2)
R("DAT0", [A(RT1, 1), (DAT0_X, A(RT1, 1)[1] - 0.4),
           (DAT0_X, round(_d7[1] + (_d1[1] - _d7[1]) * (DAT0_X - _d7[0]) / (_d1[0] - _d7[0]), 3))])   # (onto R7-R1's DAT0 link)
R("DAT0", [A(R1, 2), (A(U3, 1)[0], A(R1, 2)[1]), A(U3, 6), A(U3, 1), (A(U3, 1)[0], C[7][1])])
R("SD_CLK", [L["JP3"], (L["JP3"][0], 27.2), (A(U3, 3)[0], 27.2 - (L["JP3"][0] - A(U3, 3)[0])), A(U3, 4), A(U3, 3), (A(U3, 3)[0], C[5][1])])
R("SD_DI", [L["JP4"], (L["JP4"][0], 26.3), (A(U2, 3)[0], 26.3 - (L["JP4"][0] - A(U2, 3)[0])), A(U2, 4), A(U2, 3), C[3]])
CS_IN, CS_VIA = (L["JP2"][0], 26.8), (91.0, 21.4 + DY)   # (2026-11-08: CS drops on the top to just above SD_CLK's knee, then
R("SD_CSIN", [L["JP2"], CS_IN])                           # one straight bottom hop under the three drops: the bottom above
R("SD_CSIN", [CS_IN, CS_VIA], TRACK, pcbnew.B_Cu)         # 26.8 is free for the twins' lines)
R("SD_CSIN", [A(R4, 2), CS_VIA, A(U1, 2)])
R("CARD_CS", [A(U1, 4), (88.3, A(U1, 4)[1]), (88.3, 18.6 + DY), (A(U2, 6)[0], 18.6 + DY), A(U2, 6), A(U2, 1),
              (A(U2, 1)[0], 13.6 + DY), (C[2][0], 12.8 + DY), C[2]])
R("DAT2", [C[1], (C[1][0], A(R2, 1)[1]), A(R2, 1)])
RAIL = LANE["3V3"]
R("SD_VIN", [L["JP5"], (L["JP5"][0], A(FB1, 1)[1]), A(FB1, 1)], POWER)
R("SD_VDD", [A(FB1, 2), (RAIL, A(FB1, 2)[1]), (RAIL, 16.2 + DY), A(C2, 1), A(U1, 5)], POWER)
R("SD_VDD", [(RAIL, 16.2 + DY), (RAIL, A(R2, 2)[1]), A(R2, 2)], POWER)
R("DAT1", [C[8], (C[8][0], BUS), A(R3, 2)])
R("GND", [C[6], A(U3, 2)])
R("SD_VDD", [C[4], (C[4][0], BUS)], POWER)
R("SD_VDD", [A(U3, 5), (A(U3, 5)[0], 16.5 + DY)])
R("SD_VDD", [A(U2, 5), (A(U2, 5)[0], 14.2 + DY)])
R("GND", [A(U2, 2), (A(U2, 2)[0], 19.3 + DY)])
R("SD_VDD", [A(R3, 1), (76.0, BUS)], POWER)
R("SD_VDD", [A(C1, 1), (76.0, 19.8 + DY)], POWER)
R("GND", [A(U1, 3), (89.3, 19.6 + DY)])
R("GND", [A(R4, 1), (88.0, 21.4 + DY)])
R("GND", [A(C2, 2), (93.4, 18.7 + DY)])
R("GND", [A(C1, 2), (77.4, A(C1, 2)[1])])
R("DAT0", [A(R7, 2), A(R1, 2)])                  # (R7: DAT0 up to the card's supply, by C1's)
R("SD_VDD", [A(R7, 1), (A(R7, 1)[0], 19.8 + DY), (76.0, 19.8 + DY)])
R("SD_DI", [A(R8, 1), (A(U2, 3)[0], A(R8, 1)[1])])      # (R8: CMD up to the card's supply: its left pad onto the
R("SD_VDD", [A(R8, 2), (RAIL, A(R8, 2)[1])], POWER)      # DI line above U2, its right pad onto the 3V3 rail)
R("SD_VDD", [A(C3, 1), C[4]], POWER)             # (C3 down the VDD contact's own line, between U3 and U2)
R("GND", [A(C3, 2), (A(C3, 2)[0], 21.8 + DY)])
DET_VIA = (A(J1, 9)[0], 12.3 + DY)
R("CARD_DET", [A(J1, 9), DET_VIA])              # (J1 9 up onto a via under DAT1's line, the bottom under the socket
R("CARD_DET", [DET_VIA, (DET_VIA[0], 9.0 + DY), (74.9, 9.0 + DY)], TRACK, pcbnew.B_Cu)   # to TP5 left of it)
R("CARD_DET", [(74.9, 9.0 + DY), A(TP["TP5"], 1)])

# ---- J3 to J2: every line straight through (down from J3's pins, a step right, down past J2's body onto its pins), the
# steps 1 mm apart for a via on the unprobed lines (6, 8, 9: to their test pads); the ground pins join the sockets'
# tabs (above J3's pins) and the pours
B = pcbnew.B_Cu
for n in range(2, 10):                           # (v7: J3's pin n: straight down, 45 degrees out to the column, down it to
    a, b = A(J3, J3_AT[n]), A(J2, J2_AT[n])      # the via; J2's pad n, on the bottom: straight up, 45 degrees out to it)
    X, name = VIA_X[n], LCD_PINS[n]
    w = POWER if name == "3V3" else TRACK
    R(name, [a, (a[0], V_ROW + abs(X - a[0])), (X, V_ROW), (X, V_ROW2)], w)
    R(name, [b, (b[0], V_ROW2 - abs(X - b[0])), (X, V_ROW2)], w, B)
    V(name, X, V_ROW2)
for n in (1, 10):
    g = A(J3, J3_AT[n])
    tab = min((A(J3, 11), A(J3, 12)), key=lambda t: abs(t[0] - g[0]))
    R("GND", [g, (g[0], J3_V + 0.2), (tab[0], J3_V + 0.2)])

# ---- the bottom: 3V3 (SD_VDD) across, from the rail to the card's VDD contact, the ESD chips' VBUS and R3
BUS_X = (76.0, A(U3, 5)[0], C[4][0], A(U2, 5)[0], RAIL)
R("SD_VDD", [(76.0, 19.8 + DY), *[(x, BUS) for x in BUS_X]], POWER, B)
R("SD_VDD", [(A(U3, 5)[0], 16.5 + DY), (A(U3, 5)[0], BUS)], TRACK, B)
R("SD_VDD", [(A(U2, 5)[0], 14.2 + DY), (A(U2, 5)[0], BUS)], TRACK, B)
for u, v in ((76.0, 19.8 + DY), (76.0, BUS), (C[4][0], BUS), (RAIL, BUS), (A(U3, 5)[0], 16.5 + DY), (A(U2, 5)[0], 14.2 + DY)):
    V("SD_VDD", u, v)
V("SD_CSIN", *CS_IN)
V("CARD_DET", *DET_VIA)
V("CARD_DET", 74.9, 9.0 + DY)
V("SD_CSIN", *CS_VIA)
for v in GUARD_V:
    V("GND", LANE["GUARD"], v)
for u, v in ((A(C3, 2)[0], 21.8 + DY), (89.3, 19.6 + DY), (88.0, 21.4 + DY), (93.4, 18.7 + DY), (77.4, A(C1, 2)[1]), (A(U2, 2)[0], 19.3 + DY),
             (76.0, 5.0), (76.1, 28.0), (79.1, 28.0), (95.9, 6.0),   # (the pours stitched, the head's too; 2026-11-08:
             (84.5, 53.5), (95.9, 62.0), (95.9, 66.0), (95.7, 74.5)):   # (76, 25) and (79, 25) moved up beside RT1,
    V("GND", u, v)                               # (76, 34) and (87, 53.5) gone, 2026-11-08)
for n in (1, 10):                                # (J2's ground pins up to its tabs, above the pins: on the bottom)
    g = A(J2, J2_AT[n])
    tab = min((A(J2, 11), A(J2, 12)), key=lambda t: abs(t[0] - g[0]))
    R("GND", [g, (g[0], J2_V + 0.5), (tab[0], J2_V + 0.5), tab], TRACK, B)   # (J3's footprint: its tabs start 0.34 under the part's centre)

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


def label(text, x, y, size=0.8, rot=0.0, bottom=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetTextAngleDegrees(rot)
    t.SetLayer(pcbnew.B_SilkS if bottom else pcbnew.F_SilkS)
    t.SetMirrored(bottom)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(0.12))
    board.Add(t)


label("OPTIMIST SD", *T(84.3, 57.0), 0.8, 90)   # (2026-11-08: into the head's foot, top face over J2's body; the twins took its place)
# which resistor each twin doubles and what it is in (the user, 2026-11-08 evening: label what R1, R5, R6 do): under
# RT1's lower pad, beside each of RT5's and RT6's pads; and the signal beside each 0603 itself, R5's and
# R6's left of them over the column's masked lanes, R1's right of it (nothing is exposed there)
label("R1 DO", *T(A(RT1, 1)[0], A(RT1, 1)[1] - 1.75), 0.6)
for fp, txt in ((RT5, "R5"), (RT6, "R6")):      # (RT5's and RT6's pads are apart: each says whose it is, left of the pad)
    label(txt, *T(A(fp, 1)[0] - 1.4, A(fp, 1)[1]), 0.6, 90)
    label(txt, *T(A(fp, 2)[0] - 2.3, A(fp, 2)[1]), 0.6)
for fp, txt, du in ((R5, "CLK", -1.85), (R6, "MOSI", -2.65), (R1, "DO", 1.4)):
    label(txt, *T(A(fp, 1)[0] + du, (A(fp, 1)[1] + A(fp, 2)[1]) / 2), 0.6, 90)
for _, _, u, v, lab in TPS:
    label(lab, *T(u, v + 1.4), 0.6)                # (above its pad, clear of it)
# which way each cable goes in: J3 touches the top face only (the LCD's cable arrives gold up, the user), J2 the
# bottom face only (the cable from J12 arrives gold down): a cable turned over does not connect
label("LCD: gold UP", *T(94.6, 69.0), 0.8, 90)
label("IN: gold DOWN", *T(94.6, 62.0), 0.8, 90, bottom=True)   # (v7: J2 on the bottom: gold away from the board, as at J12)

# ---- ground stitching (the minor update, 2026-11-04): more GND vias between the two pours where they fit clear of
# everything and of the fast lines (lib/stitch.py); the hand-placed ones above stay
STITCHED = stitch(board, lambda x, y: via("GND", x, y), OUTLINE)
print(f"{len(STITCHED)} stitching vias added")

board.GetDesignSettings().SetAuxOrigin(pcbnew.VECTOR2I(MM(X0), MM(YB)))   # (the lower left: the Gerbers, the drill
pcbnew.ZONE_FILLER(board).Fill(board.Zones())                                   # files and cpl.csv share it)
out = os.path.join(HERE, "sd-board.kicad_pcb")
board.Save(out)
print("saved", out)

# ---- JLCPCB's assembly files: the parts list (BOM) and the placement (CPL); the holes are not parts
FAB = os.path.join(HERE, "fab")
os.makedirs(FAB, exist_ok=True)
parts = [fp for fp in board.GetFootprints() if not fp.GetReference().startswith(("H", "JP", "TP", "RT"))]   # (RT: the twins' bare holes)
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
        bottom = fp.GetLayer() == pcbnew.B_Cu; side = "Bottom" if bottom else "Top"      # (J2 is the only bottom part: JLC reads a bottom part's rotation as 180 minus
        rot = (180 - fp.GetOrientationDegrees()) if bottom else fp.GetOrientationDegrees()   # KiCad's (2026-11-09: JLC's own
        f.write(f"{fp.GetReference()},{x:.3f}mm,{y:.3f}mm,{side},{rot % 360:.0f}\n")   # footprint for C3446068 fits J2's pads
                                                 # at 180 with 0.00 mm error, misses by 2.32 at 0: the jank session's review; still check the preview)
print("wrote", os.path.join(FAB, "bom.csv"), os.path.join(FAB, "cpl.csv"))

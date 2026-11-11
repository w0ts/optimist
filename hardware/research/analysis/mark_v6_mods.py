#!/usr/bin/env python3
"""One picture per bench attempt on the v6 SD board (hardware/research/archive-v6-tested): where to probe, solder or cut.

  kicad-cli pcb render --side top --background transparent --quality high --width 1600 --height 4935 --zoom 1 \
      -o OUT/render.png hardware/research/archive-v6-tested/sd-board.kicad_pcb
  tools/builder/venv/bin/python hardware/research/analysis/mark_v6_mods.py OUT

The pad places are KiCad's (pcbnew: pad centres, mm, the board's own origin). The render's scale and origin were fitted
on the five round test pads (TP1-TP4 at 183.7 mm across, 2.7 mm apart; TP5 DET), found as yellow discs of 92 px:
60.84 px/mm, (171.174, 122.09) mm at the top left; DET lands within 3 px. Render at another size: fit again.
Each picture: the whole board small on the left with the zoomed area framed, the zoomed area on the right with the
spots marked, the steps underneath. Top side, as render-top.png: the LCD socket at the top, the card at the bottom.
"""
import sys
import textwrap

from PIL import Image, ImageDraw, ImageFont

ORIGIN = (171.174, 122.09)                      # mm at render.png's (0, 0): fitted on the test pads TP1-TP5 (see main)
PX_MM = 60.84                                   # render.png's scale, from the same fit
FONT = "/System/Library/Fonts/Supplemental/Arial.ttf"
FONT_B = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

# pad centres, mm (pcbnew dump of 2026-11-05)
P = {
    "JP1.sd": (182.1, 161.6), "JP1.bus": (185.1, 161.6),      # DO: SD_DO (to R1) | MISO bus
    "JP2.sd": (183.6, 164.3), "JP2.bus": (186.6, 164.3),      # CS: SD_CSIN (U1 in) | LCD_CS bus
    "JP3.sd": (185.1, 167.0), "JP3.bus": (188.1, 167.0),      # CLK: SD_CLK (to the card) | CLK_SD (after R5)
    "JP4.sd": (186.6, 169.7), "JP4.bus": (189.6, 169.7),      # DI: SD_DI (card) | MOSI_SD (after R6)
    "JP5.sd": (188.1, 172.4), "JP5.bus": (191.1, 172.4),      # 3V3: SD_VIN (to FB1) | 3V3 bus
    "R5.sd": (194.25, 151.91), "R5.bus": (194.25, 150.09),    # CLK
    "R6.sd": (195.05, 155.41), "R6.bus": (195.05, 153.59),    # MOSI
    "R1.sd": (181.22, 181.49), "R1.card": (181.22, 183.31),   # DO: SD_DO | DAT0
    "U1.in": (191.0, 181.2), "U1.gnd": (190.05, 181.2), "U1.out": (190.05, 183.8), "U1.vcc": (191.95, 183.8),
    "TP4": (183.7, 139.1),                                     # the G pad at the top
    "C2.gnd": (193.4, 182.3), "FB1.vdd": (194.25, 174.7),
    "J3": (188.0, 125.5), "J2": (191.3, 141.5), "J1": (185.6, 196.0),   # the LCD's socket, the socket for J12's cable, the card
}
RED, BLUE, GREEN, ORANGE, GREY = (220, 30, 30), (30, 90, 220), (20, 150, 60), (240, 140, 0), (90, 90, 90)


def px(mm):
    return ((mm[0] - ORIGIN[0]) * PX_MM, (mm[1] - ORIGIN[1]) * PX_MM)


class Pic:
    def __init__(self, board, area_mm, title):
        """area_mm = (x0, y0, x1, y1) of the zoomed part."""
        self.board, self.title = board, title
        x0, y0 = px(area_mm[:2])
        x1, y1 = px(area_mm[2:])
        self.box = (int(x0), int(y0), int(x1), int(y1))
        self.zoom = board.crop(self.box).convert("RGB")
        self.draw = ImageDraw.Draw(self.zoom)
        self.font = ImageFont.truetype(FONT_B, 34)
        self.notes = []

    def at(self, name):
        x, y = px(P[name])
        return (x - self.box[0], y - self.box[1])

    def spot(self, name, colour, label, where=(1, -1), r=38):
        x, y = self.at(name)
        self.draw.ellipse((x - r, y - r, x + r, y + r), outline=colour, width=7)
        lx, ly = x + where[0] * 150, y + where[1] * 120
        self.draw.line((x + where[0] * r * 0.7, y + where[1] * r * 0.7, lx, ly), fill=colour, width=5)
        self.text(label, lx, ly, colour, where[0])

    def text(self, label, x, y, colour, side):
        w = max(self.draw.textlength(s, font=self.font) for s in label.split("\n"))
        h = 40 * len(label.split("\n"))
        if side < 0:
            x -= w
        x = min(max(x, 10), self.zoom.width - w - 10)          # (kept inside the picture)
        y = min(max(y, h / 2 + 8), self.zoom.height - h / 2 - 8)
        self.draw.rectangle((x - 8, y - h / 2 - 6, x + w + 8, y + h / 2 + 6), fill=(255, 255, 255), outline=colour, width=3)
        self.draw.multiline_text((x, y - h / 2), label, fill=colour, font=self.font, spacing=4)

    def wire(self, a, b, colour, label=None):
        (x0, y0), (x1, y1) = self.at(a), self.at(b)
        self.draw.line((x0, y0, x1, y1), fill=colour, width=10)
        if label:
            self.text(label, (x0 + x1) / 2 + 40, (y0 + y1) / 2, colour, 1)

    def part(self, a, b, colour, label, off=(0, 0), side=1):
        """a resistor body (two legs and a box) from pad a to pad b, shifted by off px; its label at the box"""
        (x0, y0), (x1, y1) = self.at(a), self.at(b)
        x0 += off[0]; x1 += off[0]; y0 += off[1]; y1 += off[1]
        mx, my = (x0 + x1) / 2, (y0 + y1) / 2
        dx, dy = x1 - x0, y1 - y0
        L = (dx * dx + dy * dy) ** 0.5 or 1
        ux, uy = dx / L, dy / L
        bx0, by0, bx1, by1 = mx - ux * 45, my - uy * 45, mx + ux * 45, my + uy * 45
        self.draw.line((x0, y0, bx0, by0), fill=colour, width=8)
        self.draw.line((bx1, by1, x1, y1), fill=colour, width=8)
        px, py = -uy * 22, ux * 22
        self.draw.polygon([(bx0 + px, by0 + py), (bx1 + px, by1 + py), (bx1 - px, by1 - py), (bx0 - px, by0 - py)], fill=(245, 222, 160), outline=colour)
        self.text(label, mx + side * 70, my, colour, side)

    def cut(self, a, b):
        (x0, y0), (x1, y1) = self.at(a), self.at(b)
        mx, my = (x0 + x1) / 2, (y0 + y1) / 2
        self.draw.line((mx - 30, my - 50, mx + 30, my + 50), fill=RED, width=9)
        self.draw.line((mx + 30, my - 50, mx - 30, my + 50), fill=RED, width=9)

    def note(self, s):
        self.notes.append(s)

    def save(self, path):
        zoom = self.zoom
        zh = 1500
        zoom = zoom.resize((int(zoom.width * zh / zoom.height), zh), Image.LANCZOS)
        small = self.board.convert("RGB")
        sh = zh
        small = small.resize((int(small.width * sh / small.height), sh), Image.LANCZOS)
        d = ImageDraw.Draw(small)
        k = sh / self.board.height
        d.rectangle([c * k for c in self.box], outline=RED, width=5)
        f, fb = ImageFont.truetype(FONT, 30), ImageFont.truetype(FONT_B, 40)
        width = max(small.width + 40 + zoom.width + 40, 1500)
        cols = int(width / 15)                                 # about 15 px a character at 30 px Arial
        title = textwrap.wrap(self.title, int(cols * 0.72))
        notes = [s for n in self.notes for s in textwrap.wrap(n, cols, subsequent_indent="    ")]
        th = 40 + 50 * len(title) + 42 * len(notes)
        out = Image.new("RGB", (width, 40 + zh + th), (255, 255, 255))
        out.paste(small, (20, 20))
        out.paste(zoom, (small.width + 40, 20))
        d = ImageDraw.Draw(out)
        y = 40 + zh
        for s in title:
            d.text((20, y), s, fill=(0, 0, 0), font=fb)
            y += 50
        y += 10
        for s in notes:
            d.text((20, y), s, fill=(0, 0, 0), font=f)
            y += 42
        out.save(path)
        print(path, out.size)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    board = Image.open(out + "/render.png").convert("RGBA")
    jumpers = (180.0, 147.5, 196.8, 176.5)     # the swap block and R5/R6

    p = Pic(board, (180.0, 136.0, 196.8, 176.5), "Attempt 1: scope, nothing soldered. Is the clock really 12 MHz?")
    p.spot("TP4", GREY, "scope ground clip:\nthe G pad", (1, 0))
    p.spot("R5.bus", BLUE, "PC9 CLK as the FM-1\nsends it (before R5)", (-1, -1))
    p.spot("JP3.bus", BLUE, "CLK as the card\ngets it (after R5)", (1, 1))
    p.spot("JP1.bus", GREEN, "PC8 DO, the card's\nanswer at the FM-1", (1, -1))
    p.spot("JP2.sd", ORANGE, "PC7 LCD CS: high\n= card selected", (-1, 1))
    p.note("Claude runs `sd ceil` (idle) while User measures the clock's period on CLK: 83 ns = 12 MHz, 125 ns = 8 MHz.")
    p.note("Then CLK after R5 next to CLK before R5: how round the edges are through 330 Ohm (what the gate would remove).")
    p.note("Then PC7: a glitch on it during a run would be the CS theory. Pads are 1.5 x 2 mm, fine for a probe tip.")
    p.save(out + "/attempt-1-scope.png")

    p = Pic(board, jumpers, "Attempt 2: loopback, bench sheet test 1 (the chip's own speed). CARD OUT.")
    p.spot("JP1.bus", RED, "100 Ohm 0603 standing\non JP1's right pad (bus side)", (-1, -1))
    p.spot("R6.bus", RED, "wire to R6's upper pad\n(MOSI bus side, before R6)", (-1, 1))
    p.wire("JP1.bus", "R6.bus", RED)
    p.note("User: USB off, power off, card out. 100 Ohm from JP1's right pad, a short wire from its top to R6's upper pad.")
    p.note("Meter: JP1 right pad to R1's upper end 0 Ohm; wire end to JP4's right pad reads R6 (330). Then power on, Claude runs `sd loop`.")
    p.note("TAKE IT OFF afterwards (it joins two FM-1 outputs through 100 Ohm). Fails at 15 MHz = the chip; passes at 20-30 = the board.")
    p.save(out + "/attempt-2-loopback.png")

    p = Pic(board, (176.0, 147.5, 196.8, 186.0), "Attempt 3: a resistor in parallel with R1 (then R5, R6), one leg on a big jumper pad, the other on the resistor's far pad.")
    p.wire("JP1.sd", "R1.card", ORANGE)
    p.spot("JP1.sd", ORANGE, "330 Ohm, leg 1: the DO jumper's\nLEFT pad (same net as R1's top)", (1, -1))
    p.spot("R1.card", ORANGE, "leg 2: R1's LOWER pad\n(the card's DO)", (1, 1))
    p.wire("JP3.bus", "R5.bus", BLUE)
    p.spot("JP3.bus", BLUE, "later, R5: 330 from the CLK jumper's\nRIGHT pad to R5's UPPER pad", (-1, 1))
    p.wire("JP4.bus", "R6.bus", BLUE)
    p.spot("JP4.bus", BLUE, "later, R6: 330 from the DI jumper's\nRIGHT pad to R6's UPPER pad", (1, 1))
    p.note("ORANGE first. Power off. A 330 Ohm with one leg on the DO jumper's LEFT pad (big) and the other on R1's LOWER pad: that is 330 in")
    p.note("parallel with R1's 150 = 103 Ohm. The wire may be a few cm; keep it off the CLK pads. Power on: Claude runs `sd diag`, `sd speed`.")
    p.note("BLUE only if the orange one changes something: the same trick for R5 (CLK) and R6 (MOSI), each 330 in parallel with 330 = 165 Ohm,")
    p.note("one at a time, with a screen check (stock boot and SLOOP): these two also protect the LCD from a card in its native mode.")
    p.save(out + "/attempt-3-resistors.png")

    p = Pic(board, (178.0, 147.5, 196.8, 186.0), "R1 today and the new one: what is on the board already (grey) and the wire to add (orange).")
    p.part("R1.sd", "R1.card", GREY, "there already: 470 across R1\n(220 || 470 = 150 Ohm)", off=(-70, 0), side=-1)
    p.part("R5.bus", "R5.sd", GREY, "there already: 1k across R5\n(510 || 1k = 338 Ohm)", off=(-70, 0), side=-1)
    p.part("R6.bus", "R6.sd", GREY, "there already: 1k across R6", off=(60, 0), side=1)
    p.part("JP1.sd", "R1.card", ORANGE, "ADD: 150 Ohm, one leg on the DO jumper's\nLEFT pad, the other on R1's LOWER pad\n(150 || 150 = 75 Ohm)", off=(0, 0), side=1)
    p.note("Grey: the resistors soldered earlier across R1, R5 and R6 (the 150 / 338 Ohm we measure with). They stay.")
    p.note("Orange: the new one. A 150 Ohm (or a 470 for a smaller step, 114 Ohm): leg 1 on the big DO jumper pad, left of the '2'; leg 2 on R1's")
    p.note("lower pad, the end towards the card socket, next to the grey one's leg. Power off while soldering. Power on: Claude reads the clock steps.")
    p.save(out + "/attempt-3-r1-now.png")

    p = Pic(board, (178.0, 158.0, 190.0, 186.0), "Step 1 of 3: R1. Add a 150 Ohm from the DO jumper's LEFT pad to R1's LOWER pad (the 470 stays).")
    p.part("R1.sd", "R1.card", GREY, "470 already there\n(220 || 470 = 150)", off=(-70, 0), side=-1)
    p.part("JP1.sd", "R1.card", ORANGE, "ADD 150 Ohm\nleg 1: DO jumper, LEFT pad\nleg 2: R1, LOWER pad\n(result 56 Ohm)", off=(0, 0), side=1)
    p.note("Power off. Solder. Power on. Claude reads sd diag and sd speed. No screen risk on this one (it is the card's answer line).")
    p.save(out + "/step-1-r1.png")

    p = Pic(board, (182.0, 147.5, 196.8, 170.0), "Step 2 of 3: R5 (CLK). Add a 470 Ohm from the CLK jumper's RIGHT pad to R5's UPPER pad (the 1k stays).")
    p.part("R5.bus", "R5.sd", GREY, "1k already there\n(510 || 1k = 338)", off=(-70, 0), side=-1)
    p.part("JP3.bus", "R5.bus", BLUE, "ADD 470 Ohm\nleg 1: CLK jumper, RIGHT pad\nleg 2: R5, UPPER pad\n(result 194 Ohm)", off=(0, 0), side=1)
    p.note("Power off. Solder. Power on. Claude reads sd diag and sd speed; User checks the screen (stock boot and SLOOP): this one is the clock line the LCD shares.")
    p.save(out + "/step-2-r5.png")

    p = Pic(board, (182.0, 147.5, 196.8, 172.0), "Step 3 of 3: R6 (MOSI). Add a 470 Ohm from the DI jumper's RIGHT pad to R6's UPPER pad (the 1k stays).")
    p.part("R6.bus", "R6.sd", GREY, "1k already there\n(510 || 1k = 338)", off=(70, 0), side=1)
    p.part("JP4.bus", "R6.bus", BLUE, "ADD 470 Ohm\nleg 1: DI jumper, RIGHT pad\nleg 2: R6, UPPER pad\n(result 194 Ohm)", off=(0, 0), side=-1)
    p.note("Power off. Solder. Power on. Claude reads sd diag and sd speed; User checks the screen: this is the data line the LCD shares.")
    p.save(out + "/step-3-r6.png")

    p = Pic(board, (182.0, 147.5, 196.8, 172.0), "The spare board with R5 and R6 gone: wire the test values straight in. 150 Ohm on each (then a plain wire = 0 Ohm for the extreme).")
    p.spot("R5.bus", BLUE, "R5's UPPER pad (CLK from the FM-1):\nleg 2 of the CLK resistor", (-1, -1))
    p.spot("R6.bus", BLUE, "R6's UPPER pad (MOSI from the FM-1):\nleg 2 of the MOSI resistor", (-1, 1))
    p.part("JP3.bus", "R5.bus", BLUE, "150 Ohm: leg 1 on the CLK jumper's\nRIGHT pad (= R5's lower pad's net)", off=(0, 0), side=1)
    p.part("JP4.bus", "R6.bus", BLUE, "150 Ohm: leg 1 on the DI jumper's\nRIGHT pad (= R6's lower pad's net)", off=(0, 0), side=1)
    p.note("With R5 and R6 gone, the wired resistor IS the series value: 150 each is the step we want (today 338); a bare wire is 0 Ohm, the extreme,")
    p.note("test only (no protection for the LCD from a card in native mode). If an UPPER pad is torn, that net's other end is the FPC pin: stop there.")
    p.note("Then: power off, move the LCD cable (J3), the J12 cable (J2) and the card from black's board to this one, power on. Claude reads sd diag,")
    p.note("sd speed; User checks the screen (stock boot and SLOOP). R1 on this board: whatever it has (220 stock, or 150 if a 470 was added): say which.")
    p.save(out + "/spare-board-r5-r6.png")

    p = Pic(board, (171.5, 123.3, 196.8, 201.5), "Swap the boards: the spare one (R5 = R6 = 150 Ohm, R1 = 220 stock) takes black's two cables and the card.")
    p.spot("J3", ORANGE, "1. the LCD's own cable: here,\ngold side UP (silkscreen: LCD: gold UP)", (-1, 1), r=60)
    p.spot("J2", BLUE, "2. the cable from the FM-1's J12: here,\ngold side DOWN (IN: gold DOWN)", (-1, 1), r=60)
    p.spot("J1", GREEN, "3. the card, contacts down,\npush until it clicks", (-1, -1), r=80)
    p.note("Power off (USB and battery). On black's board: eject the card (push-push), lift the two FPC latches, slide both cables out.")
    p.note("On the spare board: slide each cable in to the stop and close its latch: the LCD's cable into the TOP socket, J12's cable into the socket")
    p.note("BELOW it. Card in. Power on. Screen must come up as usual; then Claude reads sd diag and sd speed, User checks SLOOP too.")
    p.note("What it tests: CLK and MOSI at 150 Ohm (black has 338) with R1 at 220 (black has 150). 15 MHz passing here = CLK/MOSI were the limit.")
    p.save(out + "/swap-boards.png")

    p = Pic(board, (182.0, 147.5, 196.8, 172.0), "Meter check on the spare board (power off, meter on Ohms): two measurements, each must read about 150.")
    p.wire("JP3.bus", "R5.bus", ORANGE, "METER 1: expect ~150 Ohm")
    p.spot("JP3.bus", ORANGE, "red probe: CLK jumper,\nRIGHT pad", (-1, 1))
    p.spot("R5.bus", ORANGE, "black probe: R5's\nUPPER pad", (-1, -1))
    p.wire("JP4.bus", "R6.bus", GREEN, "METER 2: expect ~150 Ohm")
    p.spot("JP4.bus", GREEN, "red probe: DI jumper,\nRIGHT pad", (1, 1))
    p.spot("R6.bus", GREEN, "black probe: R6's\nUPPER pad", (1, -1))
    p.note("About 150 on both = the resistors are connected; the no-card fault is elsewhere on this board, stop the board work.")
    p.note("Open (OL, or megohms) on one = that solder joint: re-flow it, measure again, then the board goes back into black and Claude reads the clock.")
    p.save(out + "/meter-spare-board.png")

    p = Pic(board, (180.0, 147.5, 196.8, 176.5), "LOOPBACK, the whole procedure. Patched board, the one in black. Your 150 Ohm. Card OUT.")
    p.spot("JP1.bus", ORANGE, "150 Ohm leg 1: the DO jumper's\nRIGHT pad (next to the '2')", (-1, 1))
    p.spot("R6.bus", ORANGE, "150 Ohm leg 2 (a short wire):\nR6's UPPER pad", (-1, 1))
    p.wire("JP1.bus", "R6.bus", ORANGE)
    p.note("STEP 1  Flash black with the diag build (only it has `sd loop`). In a terminal, in ~/GitHub/optimist:")
    p.note("        make flash PKG=/Users/mots/GitHub/optimist/worktrees/sdloop-scratch/sd-loop-test.fwsc FM1=FM1-E040")
    p.note("STEP 2  Take the CARD OUT. Tell Claude 'step 2'. Claude runs the test without the wire (every line must say wrong).")
    p.note("STEP 3  Power off. Solder the 150 Ohm as drawn: one leg on the DO jumper's RIGHT pad, the other through a short wire to R6's UPPER pad.")
    p.note("STEP 4  Power on, card still out. Tell Claude 'step 4'. Claude runs the test: the highest clock that reads right is the chip's own limit.")
    p.note("STEP 5  Power off. Take the 150 off. Card back in. Done. (Fails at 15 MHz = no board change will ever help. Passes 20-30 = the board matters.)")
    p.save(out + "/loopback-steps.png")

    p = Pic(board, (178.0, 147.5, 196.8, 186.0), "TEST BOARD 245/89: one STOCK v6 board (510/220 as built), three parts, each from a jumper pad to a resistor pad. Board OFF the device.")
    p.part("JP3.bus", "R5.bus", BLUE, "470 Ohm\nleg 1: CLK jumper, RIGHT pad\nleg 2: R5, UPPER pad\n(510 || 470 = 245 Ohm)", off=(0, 0), side=1)
    p.part("JP4.bus", "R6.bus", BLUE, "470 Ohm\nleg 1: DI jumper, RIGHT pad\nleg 2: R6, UPPER pad\n(510 || 470 = 245 Ohm)", off=(0, 0), side=-1)
    p.part("JP1.sd", "R1.card", ORANGE, "150 Ohm\nleg 1: DO jumper, LEFT pad\nleg 2: R1, LOWER pad\n(220 || 150 = 89 Ohm)", off=(0, 0), side=1)
    p.note("Why: 330/150 is proven (test 4). 245/89 is the next step down with your parts: it may make 12 MHz solid; it must still cold-boot stock and SLOOP clean.")
    p.note("Then, when pink is free: pink OFF, cable out, pink's own board off the ribbon, this board on, CARD IN, cable in, on. Claude: sd diag at 10/12, a heavy play for CRC errors.")
    p.note("User: stock V15 cold boot, screen clean? SLOOP cold boot, screen clean? (Claude installs each; you look.) Afterwards pink's own board goes back.")
    p.save(out + "/test-board-245.png")

    p = Pic(board, (180.0, 136.0, 196.8, 176.5), "Attempt 4: CLK gate (needs a 74LVC1G08, SOT-23-5, on a breakout). Not before the part is here.")
    p.cut("JP3.sd", "JP3.bus")
    p.spot("JP3.bus", BLUE, "gate pin 1 (A):\nCLK after R5", (1, -1))
    p.spot("JP2.sd", ORANGE, "gate pin 2 (B):\nPC7, high = card selected", (-1, -1))
    p.spot("JP3.sd", GREEN, "gate pin 4 (Y):\nto the card's CLK", (-1, 1))
    p.spot("JP5.sd", RED, "gate pin 5 (VCC):\n3.3 V before the bead", (1, 1))
    p.spot("TP4", GREY, "gate pin 3 (GND): the G pad,\nor the ground copper scraped bare", (1, 0))
    p.note("Cut JP3's bridge (the CLK jumper, made for cutting). Gate: A = JP3 right pad, B = JP2 left pad, Y = JP3 left pad, VCC = JP5 left pad,")
    p.note("GND = the G pad at the top or the ground copper scraped bare next to the block; 100 nF across VCC/GND at the gate. Card CLK = CLK AND PC7.")
    p.note("Caveat: the card then gets no clocks while deselected (the 74 init clocks, the 8 after each command): the firmware must send those")
    p.note("with PC7 high. Claude adds that switch before this is tried. Gain on the clock limit: small (330 Ohm's edge minus the gate's 3 ns).")
    p.save(out + "/attempt-4-clk-gate.png")

    p = Pic(board, (186.0, 176.0, 196.8, 188.0), "Attempt 5: Schmitt chip select (needs a 74LVC1G14, same SOT-23-5 pinout). Only if attempt 1 shows glitches on PC7.")
    p.spot("U1.in", ORANGE, "U1 pin 2: PC7 in", (1, -1))
    p.spot("U1.out", GREEN, "U1 pin 4: card CS out\n(scope here too)", (-1, 1))
    p.note("Swap U1 (74LVC1G04 inverter) for a 74LVC1G14 (Schmitt inverter): same pins, 1 NC, 2 A, 3 GND, 4 Y, 5 VCC. Hot air or two irons.")
    p.note("Removes a slow or noisy PC7 edge as a cause of a mid-run deselect. Not expected to change the clock limit.")
    p.save(out + "/attempt-5-schmitt-cs.png")


if __name__ == "__main__":
    main()

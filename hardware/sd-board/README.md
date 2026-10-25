# The FM-1 SD board (v3)

An L-shaped board (a 10 mm strip, 71 mm long, with a 22 x 22.6 mm foot) with everything the SD card mod needs (docs/SD-CARD-DESIGN.md): a push-push microSD socket, the
chip-select inverter, the 1 kOhm on the card's data out, the pull-ups, the decoupling, and a 6-pin 0.5 mm flat-cable
(FFC) socket. It sits on the back of the FM-1's board along its left edge (the user's plan, 2026-10-25): the flat cable
from a tap at J12 (the LCD's connector, on the front) runs between the buttons and the encoders, round the board's left
edge into the FFC socket at the strip's top; the strip runs down the edge to the foot, where the battery was (the
battery moves right); the card goes in from the left edge. **No soldering.**
Status (2026-10-25): designed and checked (KiCad: no design-rule error, the schematic's nets equal the board's);
**most sizes measured (MEASUREMENTS.md), a few still from a photo**; **no board has been made yet**; **the tap board at J12 is not designed
yet** (it needs J12's pin order, metered).

![top](render-top.png)

## The kit

1. **This board**, ordered assembled (below).
2. **The tap board at J12** (to come): the LCD's flex and J12 pass through it, six of their lines go out on the FFC.
3. **A 6-wire 0.5 mm FFC**, about 3.5 mm wide, long enough from J12 along the strip between the buttons and the
   encoders, round the board's left edge, to this board (measure; 100-150 mm are standard lengths). Which contact side ("same side" / "opposite side") follows from how the
   two sockets face once the tap board exists.

## Order it (JLCPCB, assembled)

1. jlcpcb.com > Order now > upload `fab/sd-board-gerbers.zip` (2 layers, 22 x 71 mm, 1.6 mm; JLC prices the L by its box; the defaults are fine).
2. Tick **PCB Assembly**, top side, Economic.
3. Upload `fab/bom.csv` (the parts) and `fab/cpl.csv` (where they go). Every part is a JLC part (LCSC numbers in the BOM).
4. Check the placement preview: the card socket's and the FFC socket's openings on the long straight edge,
   U1's pin 1 dot as on the render.
5. Order. Rough cost (an estimate from JLC's usual prices, not a quote): about 20 USD for 2 boards, 22 for 5, 26 for 10,
   32 for 20, plus shipping and VAT. Three parts are JLC "extended" parts (the two sockets, the inverter): 3 USD each
   per order.

The same board can be shared on OSHWLab (JLC's open-hardware site) so anyone orders it from a page: import the KiCad
project (`sd-board.kicad_pcb`, `sd-board.kicad_sch`) into EasyEDA Pro (File > Import > KiCad), publish it.

## The FFC socket's pins

| J2 pin | Net | On the FM-1 |
|---|---|---|
| 1 | MOSI | PC10, the LCD's data out (the chip's pin 29) |
| 2 | CLK | PC9, the LCD's clock (pin 30) |
| 3 | MISO | PC8, the LCD's D/C (pin 31) |
| 4 | GND | ground |
| 5 | 3V3 | 3.3 V (the LCD's VCC) |
| 6 | CS (in) | PC7, the LCD's CS (pin 32) |

(The order suits this board's routing; the tap board at J12 is designed to match it.)

It is held by the back cover's two poles (6.9 mm wide, 6.8 mm high, measured): they stand over the FM-1 board's edge,
so the board has two U-notches open to the left around them, and the cover holds it in place when it closes. All the
sizes are in `MEASUREMENTS.md`. Check the fit first with `mockup.stl` (3D print) or `template-1to1.pdf` (print at 100 %).

## The design

| Ref | Part | LCSC |
|---|---|---|
| J1 | microSD socket, push-push (Korean Hroparts TF-01A) | C91145 |
| J2 | FFC socket, 6 pins, 0.5 mm, hinged lid, 2 mm high (XUNPU FPC-05F-6PH20) | C2856796 |
| U1 | 74LVC1G04 single inverter, SOT-23-5 (TI SN74LVC1G04DBVR) | C7827 |
| R1 | 1 kOhm 0603: the card's DO to the FM-1's PC8 | C21190 |
| R2, R3 | 47 kOhm 0603: DAT2, DAT1 pulled up (unused in SPI mode) | C25819 |
| R4 | 47 kOhm 0603: the inverter's input pulled up while the FM-1 resets | C25819 |
| C1 | 10 uF 0805 | C15850 |
| C2 | 100 nF 0603 at U1 | C14663 |

The schematic: `sd-board-schematic.pdf` (and `sd-board.kicad_sch`, KiCad's own symbols, a net label on every pin).

Regenerate everything with `sh make_fab.sh` (KiCad 10): `make_board.py` builds the board from code (parts, nets, the
copper, the ground pours), `make_schematic.py` the schematic; `make_fab.sh` runs the design-rule check (board) and the
electrical rule check (schematic), checks that both have the same nets pin for pin (`check_nets.py`), and writes `fab/`.
The footprints in `lib/` are LCSC's own (EasyEDA's library, converted with easyeda2kicad), so they are the ones JLC
assembles on; the card socket's 3D model is moved 5.09 mm to sit on its pads. The DRC's remaining notes are silkscreen
only.

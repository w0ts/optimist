# The FM-1 SD board (v4)

A 24.4 x 44.5 mm board with everything the SD card mod needs (docs/SD-CARD-DESIGN.md): a push-push microSD socket, the
chip-select inverter, the 1 kOhm on the card's data out, the pull-ups, the decoupling, and a 6-pin 0.5 mm flat-cable
(FFC) socket. It sits on the back of the FM-1's board, right of the battery, by the battery's plug (the user's plan,
2026-10-25; the battery stays where it is). The card goes in through the case's bottom wall, flush with the outside,
86 mm from the left wall (between the pegs that close the case). It slides into a printed tray (`sd-tray.stl`, `make_tray.py`) that sits round the cover's pole there; the battery's cable passes over both.
A flat cable from a tap at J12 (the LCD's connector, on the front) comes round the board's edge between the two jacks
and down the back to the FFC socket: **no soldering**.
Status (2026-10-25): designed and checked (KiCad: no design-rule error, the schematic's nets equal the board's); the
sizes measured with calipers (`MEASUREMENTS.md`); **no board has been made yet**; **the tap board at J12 is not
designed yet** (it needs J12's pin order, metered, and u, v: the room beside the LCD's cable between the jacks).

![top](render-top.png)

## The kit

1. **This board**, ordered assembled (below).
2. **The tap board at J12** (to come): the LCD's flex and J12 pass through it, six of their lines go out on the FFC.
3. **A 6-wire 0.5 mm FFC**, about 3.5 mm wide, from J12 between the jacks, round the board's edge and down the back
   to this board (measure; 100-150 mm are standard lengths).
4. **A slot in the bottom wall** for the card: about 15 x 2.5 mm, its left end 84.5 mm from the left wall's inside.

## Order it (JLCPCB, assembled)

1. jlcpcb.com > Order now > upload `fab/sd-board-gerbers.zip` (2 layers, 24.4 x 44.5 mm, 1.6 mm; the defaults are fine).
2. Tick **PCB Assembly**, top side, Economic.
3. Upload `fab/bom.csv` (the parts) and `fab/cpl.csv` (where they go). Every part is a JLC part (LCSC numbers in the BOM).
4. Check the placement preview: the card socket's opening past the bottom edge, the FFC socket's opening at the top,
   U1's pin 1 dot as on the render.
5. Order. Rough cost (an estimate from JLC's usual prices, not a quote): about 20 USD for 2 boards, 22 for 5, 26 for 10,
   32 for 20, plus shipping and VAT. Three parts are JLC "extended" parts (the two sockets, the inverter): 3 USD each
   per order.

The same board can be shared on OSHWLab (JLC's open-hardware site) so anyone orders it from a page: import the KiCad
project (`sd-board.kicad_pcb`, `sd-board.kicad_sch`) into EasyEDA Pro (File > Import > KiCad), publish it.

Check the fit first: `mockup-printable.stl` (3D print, one piece: the board and its parts as blocks), or
`template-1to1.pdf` (print at 100 %).

## The FFC socket's pins

| J2 pin | Net | On the FM-1 |
|---|---|---|
| 1 | 3V3 | 3.3 V (the LCD's VCC) |
| 2 | CS (in) | PC7, the LCD's CS (the chip's pin 32) |
| 3 | MOSI | PC10, the LCD's data out (pin 29) |
| 4 | GND | ground |
| 5 | CLK | PC9, the LCD's clock (pin 30) |
| 6 | MISO | PC8, the LCD's D/C (pin 31) |

(The order follows the card's contacts, so nothing crosses; the tap board at J12 is designed to match it.)

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

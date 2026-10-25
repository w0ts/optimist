# The FM-1 SD board

A 24 x 30 mm board with everything the SD card mod needs (docs/SD-CARD-DESIGN.md): a push-push microSD socket, the
chip-select inverter, the 1 kOhm on the card's data out, the pull-ups and the decoupling. Order it assembled, stick it
inside the case, solder six wires to the FM-1. Status (2026-10-25): designed and checked (KiCad's design-rule check: no
error); **no board has been made yet**.

![top](render-top.png)

## Order it (JLCPCB, assembled)

1. jlcpcb.com > Order now > upload `fab/sd-board-gerbers.zip` (2 layers, 24 x 30 mm, 1.6 mm; the defaults are fine).
2. Tick **PCB Assembly**, top side, Economic.
3. Upload `fab/bom.csv` (the parts) and `fab/cpl.csv` (where they go). Every part is a JLC part (LCSC numbers in the BOM);
   P1, the row of seven holes, is not assembled.
4. Check the placement preview: the socket's opening on the board's top edge, U1's pin 1 dot as on the render.
5. Order. Rough cost (an estimate from JLC's usual prices, not a quote): about 17 USD for 2 boards, 19 for 5, 23 for 10,
   29 for 20, plus shipping and VAT. Two parts are JLC "extended" parts (the socket, the inverter): 3 USD each per order.

The same board can be shared on OSHWLab (JLC's open-hardware site) so anyone orders it from a page: import
`sd-board.kicad_pcb` into EasyEDA Pro (File > Import > KiCad), publish the project.

## Wire it

| Board pad | FM-1 | Where on the FM-1 |
|---|---|---|
| CS | PC7 (the LCD's CS) | J12, the LCD connector (meter it: the chip's pin 32) |
| MOSI | PC10 (the LCD's data out) | J12 (pin 29) |
| 3V3 | 3.3 V | J12's VCC or another 3.3 V point |
| CLK | PC9 (the LCD's clock) | J12 (pin 30) |
| GND | ground | any ground |
| MISO | PC8 (the LCD's D/C) | J12 (pin 31) |

The pad between CS and MOSI is unused. The board sits on the back of the FM-1's board (the free area beside the
battery), on double-sided tape; the wires go around the board's edge.

## The design

| Ref | Part | LCSC |
|---|---|---|
| J1 | microSD socket, push-push (Korean Hroparts TF-01A) | C91145 |
| U1 | 74LVC1G04 single inverter, SOT-23-5 (TI SN74LVC1G04DBVR) | C7827 |
| R1 | 1 kOhm 0603: the card's DO to the FM-1's PC8 | C21190 |
| R2, R3 | 47 kOhm 0603: DAT2, DAT1 pulled up (unused in SPI mode) | C25819 |
| R4 | 47 kOhm 0603: the inverter's input pulled up while the FM-1 resets | C25819 |
| C1 | 10 uF 0805 | C15850 |
| C2 | 100 nF 0603 at U1 | C14663 |

The schematic: `sd-board-schematic.pdf` (and `sd-board.kicad_sch`, KiCad's own symbols, a net label on every pin).

Regenerate everything with `sh make_fab.sh` (KiCad 10): `make_board.py` builds the board from code (parts, nets, the
copper, the ground pours), `make_schematic.py` the schematic; `make_fab.sh` runs the design-rule check (board) and the
electrical rule check (schematic), checks that both have the same nets pin for pin (`check_nets.py`), and writes `fab/`. The footprints in `lib/` are LCSC's
own (EasyEDA's library, converted with easyeda2kicad), so they are the ones JLC assembles on; the socket's 3D model is
moved 5.09 mm to sit on its pads. The DRC's remaining notes are silkscreen only (the small parts' outlines touching).

# The FM-1 SD board (v5)

A board with everything the SD card mod needs (docs/SD-CARD-DESIGN.md), and the splitter too: a push-push microSD
socket, the chip-select inverter, the 1 kOhm on the card's data out, the pull-ups, the decoupling, and two 10-pin
0.5 mm flat-cable sockets (the user, 2026-10-25). It sits on the back of the FM-1's board, right of the battery, by
the battery's plug (the battery stays). Its lower part (25.4 x 41.5 mm, the card socket) slides into a printed tray
round the cover's pole there; a column goes up the right of the speaker plug to a head right under the audio jacks.
- **J3**, at the top, under the gap between the black jacks: the LCD's own cable plugs in here instead of into J12.
- **J2**, below it: a standard 10-pin cable from J12 (the FM-1's LCD socket), round the board's edge between the right
  black jack and the USB socket.
- All 10 lines go straight from J2 to J3 (the LCD works as before); the SD part takes the six it needs.

The card goes in through the case's bottom wall, flush with the outside, 81.7 mm from the left wall. **No soldering.**
Status (2026-10-25): designed and checked (KiCad: no design-rule error, the schematic's nets equal the board's); sizes
measured with calipers (`MEASUREMENTS.md`); **NOT READY TO ORDER: which J12 pin is which line is a PROVISIONAL guess**
(`LCD_PINS` in `make_board.py`), and whether the LCD cable arrives mirrored is still to check. The pin finder and a
breakout will tell; then the lines are re-routed.

![top](render-top.png)

**Before ordering** (the review, 2026-10-25), with the re-route once J12's pin map is known:
- 33 Ohm 0603 (C23140) in series on CLK and MOSI where the SD branch leaves J2 (keeps the card's stub off the LCD's
  fast lines; the LCD runs at 60 MHz on the user's FM-1);
- ESD protection at the card socket: two USBLC6-2SC6 (C7519) on CLK, CMD, CS, DAT0 (the slot is open to the outside);
- check the mirror: the sockets touch both faces, a cable turned over connects pin 1 to pin 10 (marked on the board:
  "LCD: gold UP", "IN: gold DOWN");
- firmware: the LCD leaves D/C high when idle (so R1 can be 220 Ohm and the card read at 20 MHz), SD reads by DMA.

## The kit

1. **This board**, ordered assembled (below).
2. **A 10-pin 0.5 mm cable**, 60 or 80 mm (the user has both types, A and B: the sockets take either), from J12 to J2.
3. **The tray** (`sd-tray.stl`, 3D print): the board slides in from the bottom end until it meets the collar.
4. **A slot in the bottom wall** for the card: about 15 x 2.5 mm, the card's left side 81.7 mm from the left wall's
   inside (the socket's 2 mm further left).

## Order it (JLCPCB, assembled)

1. jlcpcb.com > Order now > upload `fab/sd-board-gerbers.zip` (2 layers, 25.4 x 78.2 mm overall, 1.6 mm; the defaults are fine).
2. Tick **PCB Assembly**, top side, Economic.
3. Upload `fab/bom.csv` (the parts) and `fab/cpl.csv` (where they go). Every part is a JLC part (LCSC numbers in the BOM).
4. Check the placement preview: the card socket's opening past the bottom edge, both 10-pin sockets' openings facing the top,
   U1's pin 1 dot as on the render.
5. Order. Rough cost (an estimate from JLC's usual prices, not a quote): about 20 USD for 2 boards, 22 for 5, 26 for 10,
   32 for 20, plus shipping and VAT. Four parts are JLC "extended" parts (the card socket, the 10-pin socket, the inverter, and so on): 3 USD each
   per order.

The same board can be shared on OSHWLab (JLC's open-hardware site) so anyone orders it from a page: import the KiCad
project (`sd-board.kicad_pcb`, `sd-board.kicad_sch`) into EasyEDA Pro (File > Import > KiCad), publish it.

Check the fit first: `mockup-printable.stl` (3D print, one piece: the board and its parts as blocks), or
`template-1to1.pdf` (print at 100 %).

## The FFC socket's pins

| J2 / J3 pin | Line (PROVISIONAL, until J12 is measured) |
|---|---|
| 1, 10 | GND |
| 2 | MISO = the LCD's D/C (PC8) |
| 3 | CLK (PC9) |
| 4 | MOSI (PC10) |
| 5 | LCD CS (PC7) |
| 6 | 3.3 V |
| 7, 8, 9 | the LCD's reset and backlight: only through |

## The design

| Ref | Part | LCSC |
|---|---|---|
| J1 | microSD socket, push-push (Korean Hroparts TF-01A) | C91145 |
| J2, J3 | FPC socket, 10 pins, 0.5 mm, contacts on both sides, flip lock, 1 mm high (HCTL HC-FPC-05-09-10RLTAG; JLCPCB stock, SMT assembly) | C5213750 |
| U1 | 74LVC1G04 single inverter, SOT-23-5 (TI SN74LVC1G04DBVR) | C7827 |
| R1 | 220 Ohm 0603: the card's DO to the FM-1's PC8 (shared with the LCD's D/C) | C22962 |
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

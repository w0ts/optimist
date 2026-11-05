# The FM-1 SD board, v7: J2 on the top side

> [!WARNING]
> ## 🚧 UNTESTED: only v6 (`../archive-v6-tested/`) has been built and works 🚧
>
> This variant has **never been built or tested**. It is v6 with the flat-cable sockets moved (below). Every other
> fact (the circuit, the J12 pin map, the card, the tray, the measurements, the bench tests) is v6's: read
> [`../archive-v6-tested/DESIGN.md`](../archive-v6-tested/DESIGN.md) and `../fm1/MEASUREMENTS.md`.

## Minor update (2026-11-04), all three boards, same folders, no new version

- **R8, 47 kOhm 0603 (C25819, the pull-ups' own part): the card's CMD pull-up**, as m-card's R8 (Julius Vitkauskas, <https://github.com/jvitkauskas/m-card>, ec63784, CC0-1.0). Left pad on the card's DI net (J1 pin 3 and U2's DI line, behind R6), right pad on the 3V3 rail at 95.9 mm across; R8 at 88.6 mm across, 24.4 mm up (23.4 on v6), nothing else moved. Card side: R8's net is the one behind R6 (510 Ohm), so the LCD bus (MOSI, in front of R6) sees it only through 510 Ohm: 70 uA at most.
- **Ground stitching vias**, `lib/stitch.py` (after m-card's `stitch()`): 33 more GND vias on a 1 mm grid, 3 mm apart and from the hand-placed ones, 0.25 mm clear of all copper, not in a part's box or under text, at least 1.5 mm from any CLK, MOSI, MISO or DAT0 copper (the bottom pour under those lines stays unbroken). The hand-placed vias stay.
- **The card socket J1 and the SD part beside it (U1, U2, U3, R1, R2, R3, R4, R7, R8, C1, C2, C3) are 1 mm further from the bottom edge and wall** (the user, 2026-11-04; v6 stays). Outline, FB1, J2, J3, R5, R6 and the other routes stay. Same tray.
- The order readme is now one file for the three boards, [`../README.md`](../README.md); this file keeps the design detail.

## What is different from v6

- **J2** (the cable from J12) is on the **top side**, in line with J3 (88.0 mm across, 58.5 mm up, the place v6's J2
  had in height), the **same part as J3** (XUNPU FPC-0.5AS-10PWBH20, LCSC C3446068, slide lock, top contact), turned
  the same way, not flipped. Its contacts face up, away from the board: the cable from J12 arrives gold UP
  (silkscreen "IN: gold UP"). J3's lines 2 to 9 fan out and in to J2's pads on the top; the five SD lanes reach J2's
  pads from below on the top too, so J2 needs **no vias** (the vias left are v6's: the ground stitching, the card's supply, the card detect and the chip-select crossing).
- **J3** (the LCD's cable) is 0.6 mm lower (71.7 mm up).
- The **swap jumpers JP1 to JP5** and the **J12 test pads TP1 to TP4** are gone (the card side's nets are the J12
  side's). TP5 (the card switch) stays.
- Nets: 68 pins on the board, 68 in the schematic; DRC 0 violations. The outline, the card socket, the ESD, the
  resistors and the tray are v6's.

## Not checked

- The cable: that it can arrive gold up on top of the board (v6's DESIGN.md says the cable wraps round the board's edge
  from J12 with its contacts facing the FM-1's board, which would put them down here), and its left-to-right order
  (J12 pin n on J2's n-th contact). Check on a paper cable or a printed mockup before ordering.
- The room above J2 and J3 under the case's cover (the sockets are 2 mm tall; not measured for the new place).

## Order it (JLCPCB)

Top side only: **Assembly Side: Top Side**, upload `fab/sd-board-gerbers.zip`, `fab/bom.csv`, `fab/cpl.csv`. JLC
offered only Standard assembly for these files (Economic was greyed out). Live quote of 2026-11-04 (before shipping and
VAT): 10 boards 68.52 USD, 15 boards 81.43 USD (boards 12.10, setup 25.75, stencil 8.27, parts 17.09, feeders 15.50,
assembly 2.20, packaging 0.52). v6's same quote: 69.81 and 82.46 USD (one more part type: its J2 was another part).

## Build

`sh make_fab.sh` (KiCad 10 in /Applications/KiCad): the board and the schematic, DRC, the net check, the Gerbers, the
BOM and placement, the renders. `lib/` is a link to `../lib` (the footprints and 3D models are shared).

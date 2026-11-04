# The FM-1 SD board, v7: J2 on the bottom side

> [!WARNING]
> ## 🚧 UNTESTED: only v6 (`../sd-board/`) has been built and works 🚧
>
> This variant has **never been built or tested**. It is v6 with the flat-cable sockets moved (below). Every other
> fact (the circuit, the J12 pin map, the card, the tray, the measurements, the bench tests) is v6's: read
> [`../sd-board/DESIGN.md`](../sd-board/DESIGN.md) and `../sd-board/MEASUREMENTS.md`.

## Minor update (2026-11-04), all three boards, same folders, no new version

- **R8, 47 kOhm 0603 (C25819, the pull-ups' own part): the card's CMD pull-up**, as m-card's R8 (Julius Vitkauskas, <https://github.com/jvitkauskas/m-card>, ec63784, CC0-1.0). Left pad on the card's DI net (J1 pin 3 and U2's DI line, behind R6), right pad on the 3V3 rail at 95.9 mm across; R8 at 88.6 mm across, 24.4 mm up (23.4 on v6), nothing else moved. Card side: R8's net is the one behind R6 (510 Ohm), so the LCD bus (MOSI, in front of R6) sees it only through 510 Ohm: 70 uA at most.
- **Ground stitching vias**, `lib/stitch.py` (after m-card's `stitch()`): 35 more GND vias on a 1 mm grid, 3 mm apart and from the hand-placed ones, 0.25 mm clear of all copper, not in a part's box or under text, at least 1.5 mm from any CLK, MOSI, MISO or DAT0 copper (the bottom pour under those lines stays unbroken). The hand-placed vias stay.
- **The card socket J1 and the SD part beside it (U1, U2, U3, R1, R2, R3, R4, R7, R8, C1, C2, C3) are 1 mm further from the bottom edge and wall** (the user, 2026-11-04; v6 stays). Outline, FB1, J2, J3, R5, R6 and the other routes stay. Same tray.
- The order readme is now one file for the three boards, [`../README.md`](../README.md); this file keeps the design detail.

## What is different from v6

- **J2** (the cable from J12) is on the **bottom side**, in line with J3 (88.0 mm across, 58.5 mm up, the place v6's
  J2 had in height), the **same part as J3** (XUNPU FPC-0.5AS-10PWBH20, LCSC C3446068, slide lock, top contact).
  Flipped to the bottom, its contacts face away from the board: the cable from J12 arrives gold DOWN (silkscreen
  "IN: gold DOWN"). Each of the lines 2 to 9 fans out from J3's pad to a via and back to J2's pad on the bottom.
- **J3** (the LCD's cable) is 0.6 mm lower (71.7 mm up).
- The **swap jumpers JP1 to JP5** and the **J12 test pads TP1 to TP4** are gone (the card side's nets are the J12
  side's). TP5 (the card switch) stays.
- Nets: 68 pins on the board, 68 in the schematic; DRC 0 violations. The outline, the card socket, the ESD, the
  resistors and the tray are v6's.

## Not checked

- The room under J2: it is 2 mm tall, and the board hovers 0.8 mm over the FM-1's small parts there (not measured).
- The cable: that it can arrive gold down on this route, and its left-to-right order (J12 pin n on J2's n-th
  contact). Check on a paper cable or a printed mockup before ordering.
- JLCPCB's placement of the bottom part: `fab/cpl.csv` says Bottom, rotation 0 (KiCad's); check the preview.

## Order it (JLCPCB)

Both sides are assembled, so JLC offers only Standard assembly: **Assembly Side: Both Sides**, upload
`fab/sd-board-gerbers.zip`, `fab/bom.csv`, `fab/cpl.csv`. Live quote of 2026-11-04 (before shipping and VAT): 10 boards
102.54 USD, 15 boards 115.45 USD (boards 12.10, setup 51.50, stencil 16.54, parts 17.09, feeders 15.50, assembly 2.20,
packaging 0.52). That is about 34 USD more than the top-side variant, all of it the second side's setup and stencil.

## Build

`sh make_fab.sh` (KiCad 10 in /Applications/KiCad): the board and the schematic, DRC, the net check, the Gerbers, the
BOM and placement, the renders. `lib/` is a link to `../sd-board/lib` (the footprints and 3D models are shared).

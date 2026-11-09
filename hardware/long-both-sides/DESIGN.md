# The FM-1 SD board, v7: J2 on the bottom side

> [!WARNING]
> ## 🚧 UNTESTED: only v6 (`../archive-v6-tested/`) has been built and works 🚧
>
> This variant has **never been built or tested**. It is v6 with the flat-cable sockets moved (below). Every other
> fact (the circuit, the J12 pin map, the card, the tray, the measurements, the bench tests) is v6's: read
> [`../archive-v6-tested/DESIGN.md`](../archive-v6-tested/DESIGN.md) and `../fm1/MEASUREMENTS.md`.

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
BOM and placement, the renders. `lib/` is a link to `../lib` (the footprints and 3D models are shared).

## The twins (2026-11-08): pads in parallel with R1, R5, R6

The user's ruling after the v6 bench days: tuning a value by soldering a part onto a 0603's ends is out. Each tuning
resistor has a **twin**, a pair of plain 1.6 x 2.4 mm top pads (the old cut-and-wire pads' size, the user) wired in
parallel with it: RT1 across R1 (DO), RT5 across R5 (CLK), RT6 across R6 (MOSI). A leaded resistor is soldered flat
onto the two pads. No hole, no via and no line of their own (the user, that evening: the part's own legs do the
wiring, so a pad only has to sit where its net already is on the top; a version with a plated hole in each pad and
one with vias beside the pads were thrown out the same evening). Not placed by the fab: no LCSC part, left out of
`fab/bom.csv` and `fab/cpl.csv` (refs `RT*`), `in_bom no` in the schematic. The board ships at 240 / 240 / 91 as before;
a value is lowered by soldering a leaded part across a pair (240 || 1k = 194, || 470 = 159, || 240 = 120; 91 || 470 = 76).

**Where.** RT1 (`lib/sdboard.pretty/R_twin_P7.62`, 7.62 mm pitch) stands left of the swap block above R7 and R1, lower
pad at 24 mm up, both pads wired on the top: the upper on the SD_DO drop, the lower down between R7 and R1 onto the
DAT0 link. RT5 and RT6 have their two pads apart (`twins.pretty/`, one footprint each, written by `make_board.py`):
the LCD-side pad in the head on its own lane just above the column, the SD-side pad on the swap block's copper row of
its SD net, right of the row's end where only that line runs. RT5: CLK at (94.25, 53.0) on the CLK lane, CLK_SD at
(91.5, 33.0) on JP3's row. RT6: MOSI at (94.85, 56.2), MOSI_SD at (92.0, 30.3) on JP4's row. For the MOSI pad to fit
between its neighbours, MOSI's lane runs 0.35 further right in the head (95.4) and 3V3's 0.15 (96.05, 0.56 from the
edge), both stepping above R5's top pad; from the pad (MOSI) and from 58.8 (3V3) they go straight to their vias' feet
instead of the 45-degree slant. Pad to pad 20 and 26 mm: a 1/4 W part has 28 mm of leg each side. The part lies along
the column: RT5's leg down the column's left side over the MISO and LCD_CS lanes (1.3 mm left of R5's pads), RT6's down
the right edge over 3V3's lane; everything under them is solder mask, and the two legs stay apart. The mockup has a
1/4 W body lying along each SD-side row, 2.5 mm high.

**Why apart, and not next to R5 and R6.** The column is six lanes in 4.3 mm of copper width; both nets of a twin exist
only in its own 0.8 mm lane there, so a 1.6 mm pad fits a lane only where its neighbours can give way: above the column,
in the head, for the LCD side; on the swap block's rows for the SD side. Side by side in one place they always needed
bottom lines and vias (earlier the same day: all three in a row left of the swap block, 6 vias; RT5/RT6 in the head's
right strip, 2; lying between the swap block's rows right under the column, 4, then 8 with the vias beside the pads).

So no via and no line for the twins; the guard lane and the swap block's rows are back where they were before them.
Silkscreen says what each is in: "R1 DO" at RT1, "R5" / "R6" beside each of RT5's and RT6's pads, "DO", "CLK", "MOSI"
beside the 0603s (R5's and R6's over the column's masked lanes). Not measured: a board with the twins has not been
built. `make_fab.sh` now stops when
`make_board.py` fails (it used to go on with the stale board). DRC 0, ERC clean, nets 76 = 76.

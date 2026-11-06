# The short FM-1 SD board

> [!WARNING]
> **Never built, never fitted, never tested.** Only v6 (`../archive-v6-tested/`) has run on an FM-1.

m-card's place (Julius Vitkauskas, <https://github.com/jvitkauskas/m-card>, ec63784, CC0-1.0) and hinged socket, redrawn for
short lines. 26 x 25.0 mm, u 84.5-110.5, v 51.7-76.7 in the FM-1 frame, inside m-card's outline. 1.0 mm board. "Top" is
the J3 side, toward the jacks, as on his board.

| Part | Place | From m-card |
|---|---|---|
| J1, Hirose DM3CS-SF | top left, hinge 0.5 mm from the top edge, tails facing J2 | his land pattern; turned 180, moved from the right to the left |
| J2, C2856798, bottom contact | top side, behind the socket, opening up toward the card at 58.3 mm, centre u 90, 5.5 mm from the left | same part and direction as his, and as v6's J2; moved down |
| J3, C2856798 too, bottom contact | **bottom side**, straight under J2's line, opening up at 76.4 mm, under the socket, centre u 90 | same place as his, other side; was C3446068 |
| R1, R5, R6 | top, standing between J2's opening and the tails, hand-size 0603 | his are 0805 over the tails |
| ESD, U1, pull-ups, C1-C3, FB1 | bottom, right of the through-lines | added ESD and FB1; moved to the bottom |

## What the user asked, and what differs

- **J2 and J3 are in line, as the user asked, but 5.5 mm from the left, not 15.** J3 has to stay at the jacks' gap, u 90, for
  the LCD cable. 15 mm from the left would then put the left edge at u 75 and the socket straight over the 10 V capacitor (u 78-83.7,
  v 59.8-69.9, a photo, +-1 mm) and the board's lower left on the 16 V caps (to u 78, v 57.3). Shifting the board does not solve that,
  unless that capacitor is under about 3.9 mm tall: its height has never been measured. Keeping u 84.5 and J at 15 mm puts J3 at
  u 99.5, 9.5 mm off the cable, behind the right jack: no.
- **J3 keeps its opening toward the jacks.** Turned 180 in plan, its opening would face away from the cable, which would have
  to pass J3 and loop back: about 14 mm more than the cable has (it reaches only to the socket at 76.4, m-card and v6). Seen
  on the board's underside, turned over top to bottom, J3 does look turned 180. If that is not what the user means, this
  needs a ruling.
- **J2's opening faces the card** (the user, 2026-11-04: the first version here, turned the other way with a looped cable, was
  wrong), so the J12 cable comes down over the socket, as on v6. Its pads are at the bottom edge and DO, CLK, DI leave them
  northward, under its body, between its tabs, to the tails: no crossing among them, as before.
- Both sides are assembled.

## Pin by pin

Same order as the tested v6: line n on the n-th contact from the left, seen from the board's top, at J2 and J3; each line has the same
u at both. J2 is the same part as v6's J2, turned the same way, so from J12 to J2 nothing changes from the tested board.

| Line (J12 pin) | Signal | J2 pad (rot 180, as v6) | J3 pad (bottom, flipped) | u at J2 | u at J3 |
|---|---|---|---|---|---|
| 1, 10 | GND | 10, 1 | 1, 10 | 87.75, 92.25 | 87.75, 92.25 |
| 2 | MISO / D/C, PC8 | 9 | 2 | 88.25 | 88.25 |
| 3 | LCD CS, PC7 | 8 | 3 | 88.75 | 88.75 |
| 4 | CLK, PC9 | 7 | 4 | 89.25 | 89.25 |
| 5 | MOSI, PC10 | 6 | 5 | 89.75 | 89.75 |
| 6 | through | 5 | 6 | 90.25 | 90.25 |
| 7 | 3.3 V | 4 | 7 | 90.75 | 90.75 |
| 8, 9 | through | 3, 2 | 8, 9 | 91.25, 91.75 | 91.25, 91.75 |

- **J12 cable: type A, gold DOWN at J2**, bottom-contact socket, as v6. Length used about 35 + 5 + 18.5 + 3 = 62 mm (estimate).
- **LCD cable: gold UP**, as at v6's J3. It now passes under the board, so the contact is on the board side: the same
  bottom-contact part, on the bottom side.
- Crossings among DO, CLK, DI: none, on either layer. Vias on each of them: one where the through-line dives to J3, and one behind the
  resistor to the ESD chip on the bottom. Vias on the through-lines are a staircase, 0.6 mm across and 0.55 mm up, under J2's body.

## Lengths, routed, pad to pad (mm)

Through: J2 pad to J3 pad. Stub: off the through-line to the series part. Behind: series part to the card's pin. Vias count as the
board's thickness. Measured on the three `.kicad_pcb` files. v6 includes 3 mm for each swap jumper.

| Line | Through | | | Bus stub | | | Behind R | | |
|---|---|---|---|---|---|---|---|---|---|
| | short | m-card | v6 | short | m-card | v6 | short | m-card | v6 |
| DO (MISO) | 19.5 | 13.9 | 17.2 | **1.0** | 27.1 | 50.2 | **2.4** | 3.2 | 5.3 |
| CLK | 19.4 | 13.9 | 17.2 | **3.9** | 23.5 | 8.9 | **2.2** | 2.8 | 46.6 |
| DI (MOSI) | 19.5 | 14.1 | 17.2 | **6.5** | 21.9 | 12.5 | **2.2** | 3.8 | 42.0 |
| CS (to U1, then to the card) | 19.4 | 13.5 | 17.2 | 8.7 | 14.9 | 55.0 | 9.5 | 14.3 | 12.5 |

ESD stubs, tail to the chip's first pin: DO 10.9, CLK 6.9, DI 6.4, CS 6.7 (they hang behind the resistors, not on the bus).

Through-lines stay longer than m-card's because J3 is held at the top edge by the LCD cable and J2's pads at the bottom edge by its
opening; they run straight, one via each. All copper on the LCD bus (through + stub) is 20-26 mm against m-card's 36-41 and v6's 26-67.

## Read budget, an estimate

Against v6, by the circuit model of `../NEXT-BOARD.md` (0.05-0.06 pF/mm solved; the older 0.1 pF/mm here was about double):
1.1-1.3 ns of a read loop measured at 33-42 ns, worth a few hundred kHz. It is not a clock step. The 2026-11-04 resistor change on v6 gained 2 MHz (10 to 12); shorter copper
does not repeat that. What it may also help, and this estimate does not count, is ringing: v6's 50 mm stub on DO is gone.

## Values

**Next order, decided 2026-11-06 evening: R1 91 Ohm, R5 and R6 240 Ohm (E24 for 89 and 245), with the LCD clock at 30 MHz; LCSC codes to confirm. 15 MHz passed on a v6 bodged to 245 / 89, the panel black at 60 MHz (`NEXT-BOARD.md`).** The text below is the 330 / 150 reasoning: R1 150 Ohm, R5 and R6 330 Ohm, hand-size 0603, nearest E24 to the parallel values measured on v6: 12 MHz, 20 of 20 runs, CRC 0,
793 KB/s, with SCK HD on; 15 MHz fails. **The screen at a cold boot with a card in passed at 330 / 150 (stock M-VAVE V15 and SLOOP 2.8.5, clean after a minute, 2026-11-06, on a hand-tuned v6: 338 / 150 Ohm).**
Julius's test failed lower on CLK (150 Ohm distorted, stock firmware); not seen at 330. R7 stays (DO's pull-up, removing it
lost a step). R8, the CS pull-down, the inverter, the 100 nF by the card's VDD, 10 uF and the ferrite are as v7.

## Fit: nothing here is measured on this board

- **Height.** 2.0 (J3) + 1.0 + 2.0 (J2) + about 0.3 of cable = 5.3 mm, under the 6.8 mm of the cover if the board stands on J3
  alone; 1.5 mm to spare for FM-1 parts under it. The socket is 1.83 mm (LCSC's table, not Hirose's drawing). The board is 14 mm from
  the top wall, away from the 4.8 mm band.
- **Under J3 and under the bottom parts (up to 1.5 mm): what stands on the FM-1 there is not known.** The board needs a spacer at
  the far end: it no longer lies on tape as m-card.
- **LCD cable:** it reached v6's opening at 76.4 from above the board; below the board is untested.
- **J12 cable, check on a paper strip:** (1) J2's opening is 2.5 mm from the card's end and 3.5 mm from the socket's tails: the cable's
  stiffened end has to enter from over a 1.83 mm socket into a slot about 1 mm up, in that gap. Not known to fit. (2) It has to move 12 mm
  across, from the gap at u 102 to u 90, in the 18 mm from the jacks to J2, right across the socket: a fold or two on top of it.
  (3) 80 mm is about 18 mm more than the path; v6 had about 8 spare: a 60 or 70 mm cable is likely better.
- **Card:** the cover has to come off. The cable lies across the socket: unplug it to open the lid. The lid opens from
  the J2 end, its hinge against the jacks; the opening angle is not known (Hirose's page refused).

## Trade-offs in the copper

Fast lines over solid ground is not fully kept: the through-lines run on the bottom under the socket, the top pour above them, a ground
lane between lines 5 and 6. The card's ground contact reaches the pour through that lane and two vias, because the lines pass under it. Stitching vias: 18 added, as `lib/stitch.py` keeps 1.5 mm from fast copper. The ESD
chips hang off the stubs above, not flow-through. The 3.3 V line narrows to 0.2 mm at its ends; the card's supply to its tail is 0.2 mm on one
side of the through-lines. Their capacitance was not read (ST's page timed out).
R1, R5, R6 keep a tighter courtyard than KiCad's. J1's courtyard stops at v 61.3, past the card's end at 60.75: the
resistors' upper pads, not their bodies, lie under the card's end by about 0.35 mm, and the card's height over them is not known. The socket has no 3D model: the renders show its pads only.

## Order and build

`sh make_fab.sh` (KiCad 10). Both sides: **Assembly Side: Both Sides**, 1.0 mm, `fab/sd-board-gerbers.zip`, `fab/bom.csv`, `fab/cpl.csv`.
The bottom paste layer is in the zip. Check J1, J2, J3 and every bottom part's rotation in JLC's preview. Not quoted: expect about
v7-j2-bottom's 115.45 USD for 15 (the same number of extended part types) and the difference in the socket's price (not found).

`lib` is a link to `../lib`. Credit: see [`../../LICENSING.md`](../../LICENSING.md).

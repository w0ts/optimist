# FM-1 measurements for the SD board

What is measured on a real FM-1 (calipers, the user, 2026-10-25) and what is still read off a photo. The board
(`make_board.py`) uses these; each value is a constant there. The letters are those of `what-to-measure.pdf`.

## Measured

| | What | Value |
|---|---|---|
| | The back cover's poles for the screws, high | 6.8 mm ("7 mm is fine") |
| | The two poles in the board's area, between their facing edges (first noted as "outer edges"; 45 - 6.9 = 38.1 says facing) | 38.1 mm |
| a | The lower screw hole's centre to the FM-1 board's left edge | 3.4 mm |
| b | The upper screw hole's centre to the board's left edge ("logically 3.4 too") | 3.4 mm |
| c | The upper screw hole's centre to the board's top edge ("about, hard: it is curved") | 3.4 mm |
| d | The upper screw hole to the lower one, centre to centre, on the board | 46.1 mm |
| e | The screw holes' diameter in the board ("hard to measure, about") | 2.5 mm |
| f | The board's left edge to the first part on the back (the diodes D42, D43) | 10.5 mm |
| g | The lower screw hole's centre to the board's bottom edge | 40.8 mm |
| h | The board's left edge to the outside of the case's wall | 2.5 mm |
| i | A pole's outside diameter | 6.9 mm |
| j | Pole to pole, centre to centre, on the cover | 45 mm |
| | The case's wall to the battery | 12 mm |
| | The case's wall's inside to the battery, the battery moved as far as it goes | 14 mm |
| | The board's left edge to the battery (moved) | 10.4 mm |

## Measured: the corner by the battery's leads (sheet 2, 2026-10-25)

| | What | Value |
|---|---|---|
| k | The board's left edge to the battery's right side | 72.6 mm |
| l | The hole by the battery's corner (a pole of the cover): its centre to the board's left edge | 78 mm |
| m | The same hole's centre to the board's bottom edge | 40 mm |
| n | The battery's bottom side to the board's bottom edge ("approx, the battery is not straight") | 8 mm |
| o | The battery's right side to the battery plug's left side | 28 mm |
| p | The plug's bottom end to the board's bottom edge | 24 mm |
| q | The gap between the two black jacks | 5.5 to 6 mm |
| r | The board's left edge to the middle of that gap | 90 mm |
| s | Between the jacks: the board's top edge to the wall's inside ("zero, but I would dig there") | 0 mm |
| t | The tallest on the cable's way: the battery, about 6 mm; the cover's inside is tapered along its edges (a 4 mm band, a couple of mm lower) | 6 mm |
| u, v | J12 to the jacks' edge; the free width beside the LCD's cable | not measured yet |

## How the board uses them

- The poles (6.9 mm, centres 3.4 mm in) reach over the board's edge, so they cannot pass through holes: the board has two
  U-notches open to the left, radius 3.45 + 0.6 mm.
- d (46.1, the board) and j (45, the cover) should be the same distance (each pole stands on its screw hole) but differ
  by 1.1 mm. The notches sit at the middle, 45.55 mm apart; their 0.6 mm play takes 0.55 mm either way.
- The strip is 10 mm wide (f: 10.5 free). Its top is 0.4 mm inside the FM-1 board's top edge (c).
- The foot starts 5.5 mm above the lower pole (no thin neck at the notch) with a brace from the strip's edge 9 mm
  above the pole (1 mm under the diode D42, its height from the photo) to the foot's corner; it reaches 22.6 mm below
  the pole (g: 40.8 free).
- The battery moves right by about 11 mm (the foot is 22 mm wide; 12 mm are free between the wall and the battery).
  A board that fits the 12 mm needs the card turned to go in along the edge and a socket under 12 mm wide; none was
  found (LCSC's push-push ones are 14 to 15 mm; JAE ST11-H 13.4; Molex 502579 13.8). The user: "we will move the
  battery" (2026-10-25).
- The card socket stands 1 mm past the strip's edge; the case's outside is 2.5 mm from the board's edge (h): the card's
  slot in the wall is about 1.5 mm in front of the socket.

## Still from the photo (2026-10-25)

| What | Photo value | In make_board.py |
|---|---|---|
| The lower pole's place along the edge, relative to the card | 13 mm above the card's middle | `POLES`, J1 |
| Where the flat cable comes round the edge (the FFC socket's middle, above the lower pole) | 23.5 mm | J2 |
| The foot's width, free once the battery moves right | 22 mm | `FW` |

A 3D-printed mockup (`mockup.stl`, 1.6 mm thick, the outline) or the 1:1 template (`template-1to1.pdf`, printed at
100 %) checks the fit. `make_fab.sh` writes both.

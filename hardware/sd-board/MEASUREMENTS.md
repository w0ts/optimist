# FM-1 measurements for the SD board

What is measured on a real FM-1 (calipers) and what is still read off a photo. The board (`make_board.py`) uses these;
each photo value is a constant there, to replace once measured.

## Measured

| What | Value | How | Date |
|---|---|---|---|
| The poles for the screws on the back of the case | 6.8 mm high ("7 mm is fine") | calipers, the user | 2026-10-25 |
| The two poles in the board's area, apart | 38.1 mm over their OUTER edges (centre to centre = 38.1 minus one pole's diameter, still to measure) | calipers, the user | 2026-10-25 |

## Still from the photo (2026-10-25, the 704060 battery = 40 x 60 mm as the scale; perhaps 2 mm off)

| What | Photo value | In make_board.py |
|---|---|---|
| The free strip along the left edge of the FM-1 board's back | 10 mm wide | `SW` |
| The foot (where the battery was): 22 mm wide, to 22.6 mm past the lower screw (the card socket needs 17) | the user's cover sketch: about 23 x 13 | `FW`, `YB` |
| A pole's outside diameter (the holes are this plus 0.3 mm) | 5.2 mm, so the centres are 38.1 - 5.2 = 32.9 mm apart | `POLE_D` |
| The lower pole, at the L's inner corner: from the strip's left edge, and where along it | 3.8 mm; 13 mm above the card's middle | `POLES` |
| The upper pole: from the strip's left edge | 4.4 mm | `POLES` |
| The strip's top above the upper pole's hole | 1.5 mm | `Y0` |
| Where the flat cable comes round the edge (the FFC socket's middle, above the lower pole) | 23.5 mm | J2 |

A 3D-printed mockup of the board (`mockup.stl`, 1.6 mm thick, the outline and the holes) or the 1:1 paper template
(`template-1to1.pdf`, printed at 100 %) checks all of these at once. `make_fab.sh` writes both.

The letters on `what-to-measure.pdf` (on the Desktop copy) name these: a to h on the board's back, i and j on the cover.

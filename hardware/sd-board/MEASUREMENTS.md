# FM-1 measurements for the SD board

What is measured on a real FM-1 (calipers) and what is still read off a photo. The board (`make_board.py`) uses these;
each photo value is a constant there, to replace once measured.

## Measured

| What | Value | How | Date |
|---|---|---|---|
| The poles for the screws on the back of the case | 6.8 mm high ("7 mm is fine") | calipers, the user | 2026-10-25 |
| The two poles in the board's area, apart | 38.1 mm (taken as centre to centre; the photo said 42.6) | calipers, the user | 2026-10-25 |

## Still from the photo (2026-10-25, the 704060 battery = 40 x 60 mm as the scale; perhaps 2 mm off)

| What | Photo value | In make_board.py |
|---|---|---|
| The free strip along the left edge of the FM-1 board's back | 10 mm wide | `SW` |
| The foot (where the battery was): 22 mm wide, to 22.6 mm past the lower screw (the card socket needs 17) | the user's cover sketch: about 23 x 13 | `FW`, `YB` |
| The lower screw, at the L's inner corner: from the strip's left edge, and where along it | 3.8 mm; 13 mm above the card's middle | `SCREWS` |
| The upper screw: from the strip's left edge (38.1 mm above the lower one, measured) | 4.4 mm | `SCREWS` |
| The strip's top above the upper screw | 3.4 mm | `Y0` |
| Where the flat cable comes round the edge (the FFC socket's middle, above the lower screw) | 23.5 mm | J2 |
| The screws' diameter and their heads' | unknown (holes 2.7 mm, for M2 to M2.5) | `HOLES` |

A 3D-printed mockup of the board (`mockup.stl`, 1.6 mm thick, the outline and the holes) or the 1:1 paper template
(`template-1to1.pdf`, printed at 100 %) checks all of these at once. `make_fab.sh` writes both.

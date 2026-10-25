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

## How the board uses them

- The poles (6.9 mm, centres 3.4 mm in) reach over the board's edge, so they cannot pass through holes: the board has two
  U-notches open to the left, radius 3.45 + 0.6 mm.
- d (46.1, the board) and j (45, the cover) should be the same distance (each pole stands on its screw hole) but differ
  by 1.1 mm. The notches sit at the middle, 45.55 mm apart; their 0.6 mm play takes 0.55 mm either way.
- The strip is 10 mm wide (f: 10.5 free). Its top is 0.4 mm inside the FM-1 board's top edge (c).
- The foot reaches 22.6 mm below the lower pole (g: 40.8 free).
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

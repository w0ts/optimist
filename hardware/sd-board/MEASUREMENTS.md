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
| o | The battery's right side to the battery plug's left side (sheet 3: to the plug's leftmost point, about) | 30 mm (sheet 2: 28) |
| p | The plug's lowest point to the board's bottom edge (sheet 3, about) | 23.5 mm (sheet 2: 24) |
| q | The gap between the two black jacks | 5.5 to 6 mm |
| r | The board's left edge to the middle of that gap | 90 mm |
| s | Between the jacks: the board's top edge to the wall's inside ("zero, but I would dig there") | 0 mm |
| t | The tallest on the cable's way: the battery, about 6 mm; the cover's inside is tapered along its edges (a 4 mm band, a couple of mm lower) | 6 mm |
| w1 | The white speaker plug's left side (first taken for a switch) to the board's left edge | 85.6 mm |
| w2 | The white speaker plug's lower end to the board's bottom edge | 44 mm |
| x | Below the plug: the board's bottom edge to the OUTSIDE of the case's bottom wall (about) | 3 mm |
| | The cover's pole by the battery plug, high | about 6 mm |
| | The battery's cable crosses over the board: about 8 mm wide, some 10 mm below the speaker plug | 30 to 38 mm up |
| | The LCD's cable can reach a splitter's socket about 3 to 4 mm above J12 (the user: "it would totally work") | yes |
| v | The 6-pin cable (about 3.5 mm wide) fits between the jacks beside the LCD's cable (the user) | yes |
| A | Sheet 4: from the board's top edge down the back, as far as the LCD cable's end reaches | 40 mm (to 50.3 mm up, the board's top edge at 90.3: c + d + g) |
| B | The LCD cable's middle there, from the board's left edge | 90 mm |
| C | The speaker plug's top end, from the board's bottom edge | 51 mm (so the plug is 44 to 51 mm up) |
| D | The speaker plug's right side, from the board's left edge | 90 mm (so 85.6 to 90 mm across) |
| E | Free right of the speaker plug, to the nearest part | 11 mm (to 101 mm) |
| F | The board's top edge down to the jacks' lower end | 13.5 mm (the jacks from 76.8 mm up) |
| u | J12's top side to the board's edge at the jacks (the user: "to the top of the board"; the photos put J12 about 37 mm from the jacks' edge, 53 from the LEDs' edge) | 35 mm |
| | So the 6-pin cable: 35 on the front + about 5 round the edge (the user) + about 54 down the back to J2 = about 94 mm: a standard 10 cm cable (6 mm slack) | |
| | J12: the LCD's cable is 5.5 mm wide, 10 pins (the user): so 0.5 mm pitch (a standard 10-pin 0.5 mm cable is 5.5 mm wide) | 10 pins, 0.5 mm |
| | J12: the LCD cable's contacts face the FM-1 board (the user): J12 is bottom-contact; a cable round the edge keeps them facing the board, so type A (same side) | bottom contact |
| | J12: pin map (chip pins 29 to 32, 3.3 V, ground) | not measured yet: with a 10-pin breakout (on order) and a pin-finder firmware (to build, the user: once the breakout is here) |
| | Where the card goes through the bottom wall: the card's left side from the left wall's INSIDE (pegs that close the case's bottom stand left and right of it) | 86 mm, then 84 (the user), then the socket centred on the board (the user): 81.7 mm (80.1 from the board's left edge) |

## How the board uses them (v4, by the battery's plug)

- The board spans 74.4 to 96.8 mm from the left edge (its tray's rail 0.6 mm clear of the battery, k; it ends 0.5 mm
  past the card socket, the user) and from
  1.5 mm below the bottom edge (into the gap to the wall) up to 40 mm, the pole's centre: 4 mm under the
  speaker plug (w2), which the board must not rest against (the user).
- The cover's pole (l, m; 6.9 mm, i) sits in a half-round seat in the board's top left, 0.6 mm play.
- The card socket in the board's middle (the user): the card's left side at 80.1 mm (81.7 from the wall's inside; was 84, the wall's inside taken 1.6 mm out: from h and the
  left-wall figures); the card taken as centred in its socket (UNMEASURED: check on the mockup). The socket's front at
  the bottom wall's outside (x: 3 mm below the board's edge), the card flush.
- The FFC socket at 91 mm (the jacks' gap at r = 90 mm), its cable side at 30 mm up: 10 mm under the board's top
  and 14 mm under the speaker plug (4 mm tall), so the ribbon comes down over the plug and bends down to it (the user).
- The board slides into a printed tray (`make_tray.py`, `sd-tray.stl`): a 0.8 mm floor, a rail with a groove along
  each side (the board 22.4 mm wide; the tray 73.2 to 98 mm), a collar round the pole that stops it, posts toward the cover.
  Only the collar and three posts (all 5.6 mm, the pole is 6; rail-wide, long along the rails, clear of the cover's edge band and the cable's) reach up; the rails stand 3.35 mm: the battery's cable passes
  over, between the posts (24 to 40 mm up; in its band the parts stand at most 4.4 mm with the floor, leaving about 1.6 to 2.3 mm under the cover).
- The cover's edge band (t, about 4.8 mm left, taken): the floor, the board and the socket stand 4.25 mm there.
- v3 (the L along the left edge) did not fit: 10.4 mm free beside the battery; it is in git (5af03500).

## Still unmeasured

| What | In make_board.py |
|---|---|
| Where the card sits across its socket (taken as centred) | `CARD_C` |
| The bottom wall's inside (taken 1.6 mm out, as the left wall's) | `V0`, `CARD_L` |
| u, v: the room beside the LCD's cable between the jacks, for the flat cable | (the tap board) |

A 3D-printed mockup (`mockup-printable.stl`, one piece) or the 1:1 template (`template-1to1.pdf`, printed at
100 %) checks the fit. `make_fab.sh` writes both.

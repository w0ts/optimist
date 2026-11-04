# The FM-1 SD board

A small add-on board (25.4 x 78.2 mm) that gives the **M-VAVE FM-1** a microSD card slot. **No soldering to the FM-1 is
needed.** It sits on the back of the FM-1's board right of the battery, with the card slot in the case's bottom wall. It
plugs in between the FM-1's LCD and its main board, so the card shares the LCD's SPI bus.

![top](sd-board/render-top.png)

## The four boards

One line each. The first three have the same circuit and outline and the same tray (`sd-board/sd-tray.stl`, it fits all three);
they differ in J2, the socket for the cable from J12. `sd-board-short` is a smaller redesign with no tray.

| Folder | What is different | Zip to upload | Built? |
|---|---|---|---|
| [`sd-board/`](sd-board/) (v6) | J2 on the top, its own part, cable arrives gold down; card sits 0.5 mm proud of the wall's outside | `sd-board/fab/sd-board-gerbers.zip` | **Yes, tested: ran a card in an FM-1 (2026-11-04)** |
| [`sd-board-v7-j2-top/`](sd-board-v7-j2-top/) | J2 on the top, same part as J3, in line with it, cable arrives gold up; card socket 1 mm further in | `sd-board-v7-j2-top/fab/sd-board-gerbers.zip` | No, never built or tested |
| [`sd-board-v7-j2-bottom/`](sd-board-v7-j2-bottom/) | J2 on the bottom, same part as J3, cable arrives gold down; card socket 1 mm further in | `sd-board-v7-j2-bottom/fab/sd-board-gerbers.zip` | No, never built or tested |
| [`sd-board-short/`](sd-board-short/) | 26 x 25 mm after m-card, under the jacks: hinged socket top left, J2 behind it as v6's J2, J3 under it on the bottom, in line; both sides assembled; order and fit in its `DESIGN.md`, no tray, no slot in the case | `sd-board-short/fab/sd-board-gerbers.zip` | No, never built or tested |

In plain words: v7 is v6's design after it was tested on an FM-1, with the five swap jumpers (JP1 to JP5) and the four
test pads (TP1 to TP4) removed; J2's place and part is what tells the v7 boards apart. The board with one connector on
each side (`sd-board-v7-j2-bottom`) should be easier to wire, but might be harder to fit because of its height.

> **Status:** only v6 has been built and works. The 2026-11-04 minor update is in all three folders and has been
> built for none, so the v6 files you order now differ from the tested board by one resistor (R8) and extra ground vias.
> Design-rule check and the schematic's net check pass on all three.

**Card slot.** On v6 the slot is as before: about 15 x 2.5 mm in the case's bottom wall, the card's left side 81.7 mm from
the left wall's inside. On the two v7 boards the socket is 1 mm further from the wall (the slot's size and left-right
place stay): the latched card's end is 0.5 mm *inside* the wall's outside face (v6: 0.5 mm proud of it), about 0.4 mm
short of the wall's inside face if the wall is 0.9 mm thick (taken from the tray's notes, not measured). The card (11 x
1 mm) clears a 15 x 2.5 mm slot by 2 mm each side and 1.5 mm in height. To release it, push it in past the latch: with
the 1.5 mm of travel a push-push socket typically has (assumed, not measured), the end goes to 2.0 mm inside the outside
face (v6: 1.0 mm), so a fingernail or a thin tool has to go about 2 mm into the slot (v6: 1 mm). It then springs out
about 3.5 mm past the outside face (v6: 4.5 mm; the mock-up's ejected-card figure, an estimate). Check it on
`mockup-printable-card-out.stl` before cutting a case.

## I just want to order a board

You don't have to install KiCad or run anything. Everything is in the board's own `fab/` folder.

### 1. Upload the zip to JLCPCB

Go to [JLCPCB](https://jlcpcb.com) > Order now, and upload the board's zip from the table. The size is read from the
files, and the design rules are standard (0.2 mm tracks and spacing, 0.3 mm holes), so the defaults work:

| Setting | Choice |
|---|---|
| Layers | 2 |
| PCB thickness | 1.6 mm |
| Surface finish | HASL (the cheap default) |
| PCB Assembly | on: **assembly side Top** for `sd-board` and `sd-board-v7-j2-top`, **Both sides** for `sd-board-v7-j2-bottom` and `sd-board-short` |
| Assembly type | Standard (at the last quote JLC did not offer Economic for these files) |
| BOM file | `fab/bom.csv`, next to the zip |
| CPL (placement) file | `fab/cpl.csv`, next to the zip |

Every part is a JLC part (LCSC numbers are in the BOM), so you buy nothing else for the board. In JLC's placement
preview check that the card socket's opening is past the bottom edge, both 10-pin sockets' openings face the top, and
U1's pin 1 dot is as on the render. J1, J2 and J3 are "extended" parts at JLC, which adds a small fee per part type. On
`sd-board-v7-j2-bottom`, J2 is on the bottom: check its rotation in the preview.

JLC's quotes of 2026-11-04 for 15 boards assembled, before shipping and VAT: v6 82.46 USD, v7-j2-top 81.43 USD,
v7-j2-bottom 115.45 USD (the bottom variant assembles both sides: about 34 USD more whatever the quantity). **Not
re-quoted since the minor update** (no quote tool here); R8 is the same part as four others on the board, so no new
part type is added.

### 2. Buy the cable

A **10-pin, 0.5 mm pitch FFC cable, type A** (contacts on the same side at both ends), about 80 mm, from the FM-1's
J12 to the board's J2, for example <https://www.aliexpress.com/item/4000022050815.html>. Use the shortest one that
reaches. The silkscreen by J2 says which way the contacts go ("IN: gold DOWN" or "IN: gold UP"): a cable turned over
does not connect. You also need a microSD card.

### 3. Fit it

1. Check the fit first with the 1:1 paper template `template-1to1.pdf` in the board's folder (print at 100 %) or the
   printable mockup `sd-board/mockup-printable.stl`, and print the tray, [`sd-board/sd-tray.stl`](sd-board/sd-tray.stl)
   (all three boards use it).
2. Cut a slot about 15 x 2.5 mm in the case's bottom wall for the card, its left side 81.7 mm from the left wall's inside.
3. Open the FM-1 and disconnect the battery. Slide the board into the tray, right of the battery, by the battery's plug.
4. Unplug the LCD's cable from J12 and plug it into the board's **J3** (contacts up).
5. Connect the FM-1's J12 to the board's **J2** with the cable.

The card also needs FM-1 firmware that knows about it (Optimist's SD card mode, or the SD stage of
[m-bootloader](https://github.com/jvitkauskas/m-bootloader)).

## What the minor update changed (2026-11-04)

Same folders, no new version. R8, 47 kOhm 0603 (C25819), is the card's CMD pull-up as in m-card, on the card side of R6.
31 (v6), 33 (v7-top) and 35 (v7-bottom) ground stitching vias were added, kept 1.5 mm from the clock and data lines.
On the v7 boards the card socket and the parts beside it moved 1 mm inward. No assembly PDF (the boards are ordered
populated); the old per-board readmes are in each folder's `DESIGN.md`.

## Changing the design

Each board is generated from Python (`make_board.py`, `make_schematic.py`) and checked and written out by one command from
the board's folder: `sh make_fab.sh` (KiCad 10 in /Applications/KiCad). It runs the design-rule check (0 errors), the
schematic's electrical rule check, `check_nets.py` (the schematic and the board have the same nets pin for pin), and writes
`fab/`: the Gerbers and drill files zipped for JLCPCB, `bom.csv` and `cpl.csv`. `sd-board/lib/stitch.py` (shared by the
three) places the ground vias.

## Credit and licence

Taken from **m-card by Julius Vitkauskas, https://github.com/jvitkauskas/m-card, ec63784, CC0-1.0**: the shape of the
order readme ("upload the zip to JLCPCB" and a settings table), the 47 kOhm pull-up on the card's CMD line (his R8), and
the idea and parameters of his `stitch()` for the ground vias (`lib/stitch.py` is our own code for them, with a keep-out
round the fast lines). No file of his is copied. See [`../LICENSING.md`](../LICENSING.md).

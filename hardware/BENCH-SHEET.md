# SD speed bench sheet (pink FM-1, FM1-620C)

Why the card read stops at 12 MHz. Tests are numbered as in `NEXT-BOARD.md`. **Nothing here has run on an FM-1 yet.**

**Package:** `worktrees/sdloop-scratch/sd-loop-test.fwsc` (git-ignored; Optimist, fm1base and bsp branch `sd-loop-test`). User flashes it, only the pink plugged in:
`make flash PKG=/Users/mots/GitHub/optimist/worktrees/sdloop-scratch/sd-loop-test.fwsc FM1=FM1-620C`. Check the name the tool shows. Way back: the same with `PKG=/Users/mots/GitHub/optimist/worktrees/sdpink-scratch/pink-mots-sd-a015e367-fm1base-f3ad315.fwsc`. The console is on whatever USB SERIAL says.
Keep only the pink FM-1 plugged in. Claude reads every result over `tools/fm1_console.py`.

## Where to touch (v6 board, all on the top face)

Hold the board with the FPC sockets J3 and J2 at the top and the card socket J1 at the bottom. Millimetres are from the board's top-left
corner (right, down). From `archive-v6-tested/`; each pad's net checked against its tracks.

| Point | Net | Right | Down | Note |
|---|---|---|---|---|
| R6, the pad nearer J2 | MOSI (PC10), bus side | 23.65 | 30.29 | the other pad, 32.11 down, is the card side |
| R5, the pad nearer J2 | CLK (PC9), bus side | 22.85 | 26.79 | |
| JP1, right pad | MISO (PC8), bus side | 13.70 | 38.30 | 1.5 x 2 mm, the easy place |
| R1, the pad nearer J2 | SD_DO, bus side | 9.82 | 58.19 | the other pad, 60.01 down, is DAT0, the card side |
| JP1, left pad | SD_DO | 10.70 | 38.30 | |
| JP4, right pad | MOSI_SD, card side of R6 | 18.20 | 46.40 | used for the check below |
| TP4 | GND | 12.30 | 15.80 | |

R1's bus pad is on net SD_DO, not MISO: JP1's bridge joins the two. JP1's right pad is the same node with no bridge in the way, 20 mm
nearer R6, so use it. The pink's resistors are hand-tuned and may be stacked: solder to the end of the part that sits on the bus pad.

## In order

| # | Who | Do | What it means |
|---|---|---|---|
| 0 | User | Flash, card in, boot. | |
| 0 | Claude | `sd diag` | Should still say 12 MHz. If not, stop: the base has moved. |
| 3 | Claude | For N = 0, 1, 3, 2: `sd hd N`, `sd clock auto`, `sd diag`. 0 neither, 1 HD0, 2 HD (today), 3 both | 3 reaches 15 MHz: a free gain. Same as 2: drive is used up. Then `sd hd` must show both PC9 lines 0: the LCD has its drive back. |
| 2 | User | Card OUT. | |
| 2 | Claude | `sd rc` | Time to fall and rise on PC8, PC9, PC10: this is RC, not C yet. |
| 2 | User | Multimeter on mA or uA, black on TP4, red on JP1's right pad. | |
| 2 | Claude | `sd rc hold 8`: PC8 held pulled up 20 s, screen frozen. Wait the full 20 s. | User reads I. R = 3.3 V / I. Repeat with `9` on R5's pad and `10` on R6's pad. |
| 2 | Claude | C = (rise time - driven) / (0.69 x R) | Replaces the 35 pF guess. 0.69 assumes a threshold of half the rail, not measured. |
| 2 | User, Claude | Card in, `sd rc` again, card OUT. | The difference is the card's load. NO EDGE on PC8: the selected card holds DO, the card-out number stands. |
| 1 | Claude | Card out, no wire: `sd loop` | Every line must say `wrong`. If not, something else joins the two lines. |
| 1 | User | USB off, power off. Solder **a 100 Ohm 0603 on JP1's right pad, standing, and a short wire from its top to R6's bus pad.** | See the warning. |
| 1 | User | Meter, power off: wire end at R6 to JP4's right pad reads R6 (330-340). JP1's right pad to R1's bus end reads 0. | The wire is on the bus side of both. |
| 1 | User, Claude | Power on, card stays OUT. `sd loop` | Three tables: MOSI pad normal, HD, HD + HD0. Screen may look wrong while the part is on. |
| 1 | User | **USB off, power off, take the wire and the 100 Ohm off.** Meter: JP1's right pad to R6's bus pad must read open. | |
| 1 | User, Claude | Card in, boot, `sd diag`. | Back to 12 MHz: the board is as it was. |
| 4 | User | Cold boot at 330/150, stock and SLOOP, card in. Screen right? | Whether 330 can ship. Blocks every board. |
| 5 | User solders, Claude reads | R5/R6 150, R1 100, then R1 47. `sd clock auto`, `sd diag`, screen. | Checks the model's largest gain on real copper. |
| 6 | User swaps, Claude reads | Ribbons 50, 60, 80 mm. | Checks "ribbon does not matter". |

## Reading `sd loop`

The fastest line that says `right` with 256/256 is the chip's own limit. No card, R5, R1 or R6 are in that path, only the 100 Ohm.

| MHz | 6 | 6.67 | 7.5 | 8.57 | 10 | 12 | 15 | 20 | 30 | 60 |
|---|---|---|---|---|---|---|---|---|---|---|
| BAUD | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| Half a clock, ns | 83 | 75 | 67 | 58 | 50 | 42 | 33 | 25 | 17 | 8 |

- **Fails at 15:** stop. No board helps; take the tuned resistors.
- **Passes at 20-30:** the CLK gate is worth drawing (test 7).
- `one bit late` as on the card (`byte gap` is the card's pattern, b7 b7 b6..b1): the chip's own path has run out of half a clock. `wrong` at
  every clock with a wire on: the wire is off the node, or SPI1 cannot read while it sends.
- 20, 12, 8.57 and 6.67 MHz are even BAUDs, so odd dividers: their two half clocks may differ, and a line can look out of order (test 8).
- The 100 Ohm adds about 100 Ohm x the PC8 load from test 2: 1-3 ns, under one step. Claude subtracts it.
- `dma last bytes` is one byte per DMA send, not a full-duplex run: DMA has one address register. `wrong` on every line while the bytes are
  `right` only says BUF does not fill during DMA.

## Warning

**The wire joins two outputs.** Outside the test, PC10 sends the screen's data while PC8 drives the LCD's D/C, for as long as the FM-1 is on. A
bare wire shorts them at every bit where they differ. The 100 Ohm limits that (roughly 20 mA, an estimate, the pins' rating not checked)
and should leave the screen readable (not tested). Keep power-on time short and **take it off afterwards**: left on, the screen and the
card both fail.

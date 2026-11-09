# The SD board

A microSD slot for the M-VAVE FM-1. **No soldering to the FM-1:** it plugs in between the LCD and the main board.

![The board](long-both-sides/render-top.png)

> **Not final yet:** the board is not built, and its resistor values may change. Only v6 ([`archive-v6-tested/`](archive-v6-tested/)) has run in an FM-1.

## Order it

- Upload `long-both-sides/fab/sd-board-gerbers.zip` to [JLCPCB](https://jlcpcb.com).
- 2 layers, 1.6 mm, HASL, assembly on **both sides**, with `fab/bom.csv` and `fab/cpl.csv`.
- About 100 USD for 10 boards.
- Also buy a 10-pin, 0.5 mm FFC cable, type A, about 80 mm, and a microSD card.

## Fit it

1. Print the tray, [`tray/sd-tray.stl`](tray/sd-tray.stl).
2. Cut a 15 x 2.5 mm slot in the case's bottom wall, 81.7 mm from the left wall's inside.
3. Battery off. Board into the tray, right of the battery.
4. LCD cable into **J3**, gold up. The new cable from J12 into **J2**, gold down.

It needs Optimist's SD card mode or [m-bootloader](https://github.com/jvitkauskas/m-bootloader).

## More

- [`NEXT-BOARD.md`](NEXT-BOARD.md): speed and resistor values.
- [`BENCH-SHEET.md`](BENCH-SHEET.md): the bench tests.
- Each board's `DESIGN.md`: how it is made.

Based on [m-card](https://github.com/jvitkauskas/m-card) by Julius Vitkauskas (CC0).

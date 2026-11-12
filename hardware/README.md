# SD card for the FM-1

A small board that adds a microSD slot to the M-VAVE FM-1. **No soldering on the FM-1:** it plugs in between the LCD
cable and the main board.

![The SD board](sd-board/render-top.png)

## What it brings to Optimist

- Projects, presets, kits and samples on the card.
- Samples streamed from the card: 8 voices at once, measured on an FM-1.
- Browse the card's samples on the FM-1.
- More room for code: your data leaves the FM-1's flash.
- Install firmware from the card with [m-bootloader](https://github.com/jvitkauskas/m-bootloader).

Turn on **SD card mode** in the builder. If the screen stays black with the board in, build with `LCD_BAUD=1`
(screen at 30 MHz).

> **Not built yet:** this board is not tested, and its resistor values may still change. The board before it
> ([`research/archive-v6-tested/`](research/archive-v6-tested/)) runs in an FM-1.

## Order it on JLCPCB

1. Download [`sd-board-gerbers.zip`](https://github.com/w0ts/optimist/raw/main/hardware/sd-board/fab/sd-board-gerbers.zip)
   and upload it on [jlcpcb.com](https://jlcpcb.com).
2. Change these settings. Leave the rest at the defaults.

   | Setting | Value |
   |---|---|
   | Layers | 2 |
   | Thickness | 1.6 mm |
   | Surface finish | HASL |
   | PCB Assembly | On, Standard, **Both Sides** |
   | BOM | [`bom.csv`](https://github.com/w0ts/optimist/raw/main/hardware/sd-board/fab/bom.csv) |
   | Pick and place | [`cpl.csv`](https://github.com/w0ts/optimist/raw/main/hardware/sd-board/fab/cpl.csv) |

3. In the preview, check J2 sits on the bottom side.

Also buy a [10-pin, 0.5 mm FFC cable](https://www.aliexpress.com/item/4000022050815.html) (type A, about 80 mm) and a microSD card.

## Fit it

1. Print the tray, [`tray/sd-tray.stl`](tray/sd-tray.stl).
2. Cut a 15 x 2.5 mm slot in the case's bottom wall, 81.7 mm from the left wall's inside.
3. Cut the plastic under the audio jacks, so the ribbon cable can pass.
4. Battery off. Board into the tray, right of the battery.
5. LCD cable into **J3**, gold up. The new cable from J12 into **J2**, gold down.

## Folders

- [`sd-board/`](sd-board/): the KiCad board and the order files.
- [`tray/`](tray/): the printed tray.
- [`research/`](research/): measurements, bench tests, older boards.

Based on [m-card](https://github.com/jvitkauskas/m-card) by Julius Vitkauskas (CC0).

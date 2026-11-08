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
| 0 | Claude | `sd diag` | Should still say 12 MHz. If not, stop: the base has moved. **2026-11-06 10:50, pink, diag package, a 16 GB card (15,193 MB, cluster 8 KB): 10 MHz, "12 MHz: timeout or refused", 0 crc fails. The base is 10 today, which card this is must be settled first. 11:35, the same board back after the loopback, a card of 15,193 MB with 32 KB clusters: 12 MHz pass, 15 refused, 0 crc fails. Two cluster sizes at the same size = two cards of the same brand (the user swapped between them, not sure which is which): the card is worth one clock step at this margin.** |
| C | done on black, 2026-11-05 | `sd ceil` idle, under `heavy`, at 6 / 7.5 / 10 / 12 MHz (`NEXT-BOARD.md`, "Dense kits") | Every slow wait had a render inside, none without; idle 8-block runs 1,015 KB/s; block time follows the clock, so BAUD is right. Use a folder of contiguous files (Piano): the kit's files are one cluster apart. |
| I | done on black, 2026-11-05 | `sd irq 1`: `sd ceil` under `heavy`, `sd stats` with the heavy kit playing, off then on | The interrupt reader: 702 KB/s at 8 a run under heavy (polled 366); the kit 0 dry voices against 195. The fix is firmware; the rows below are about the clock margin (black fell to 10 MHz twice on CRC under load). |
| 3 | Claude | For N = 0, 1, 3, 2: `sd hd N`, `sd clock auto`, `sd diag`. 0 neither, 1 HD0, 2 HD (today), 3 both | 3 reaches 15 MHz: a free gain. Same as 2: drive is used up. **2026-11-06 pink: hd 3 and hd 2 both 10 MHz, 12 refused either way: the drive is used up (at the 10 MHz base of that day). 11:50, sd-loop-test-mosi.fwsc (`sd hd` +4 HD0 / +8 HD on PC10, MOSI, applied in the session): first probe after boot 12 pass then one CRC fail and a step down to 10; then hd 10 (MOSI HD), hd 14 (HD + HD0) and hd 2 (control) all "12 MHz: CRC failed", 10 kept. The MOSI drive, which moved the loopback from 8.6 to 15, moves the card path not at all: the card's own delay sets it. 12 is marginal on pink as on black; 10 is the safe clock.** Then `sd hd` must show both PC9 lines 0: the LCD has its drive back. |
| 2 | User | Card OUT. | |
| 2 | Claude | `sd rc` | Time to fall and rise on PC8, PC9, PC10: this is RC, not C yet. **2026-11-06 pink, tuned board, card out: to pull-up PC8 1001 ns, PC9 751, PC10 751 (resolution 50 ns; "driven rise" printed 102.5 us on all three, an artefact of the probe, not read). No meter step yet, so no C.** |
| 2 | User | Multimeter on mA or uA, black on TP4, red on JP1's right pad. | |
| 2 | Claude | `sd rc hold 8`: PC8 held pulled up 20 s, screen frozen. Wait the full 20 s. | User reads I. R = 3.3 V / I. Repeat with `9` on R5's pad and `10` on R6's pad. |
| 2 | Claude | C = (rise time - driven) / (0.69 x R) | Replaces the 35 pF guess. 0.69 assumes a threshold of half the rail, not measured. |
| 2 | User, Claude | Card in, `sd rc` again, card OUT. | The difference is the card's load. NO EDGE on PC8: the selected card holds DO, the card-out number stands. |
| 1 | Claude | Card out, no wire: `sd loop` | Every line must say `wrong`. If not, something else joins the two lines. **2026-11-06 pink, tuned board: every line wrong, got FF FF.. at all 10 clocks and all 3 MOSI pads: nothing joins them.** |
| 1 | User | USB off, power off. Solder **a 100 Ohm 0603 on JP1's right pad, standing, and a short wire from its top to R6's bus pad.** | See the warning. |
| 1 | User | Meter, power off: wire end at R6 to JP4's right pad reads R6 (330-340). JP1's right pad to R1's bus end reads 0. | The wire is on the bus side of both. |
| 1 | User, Claude | Power on, card stays OUT. `sd loop` | Three tables: MOSI pad normal, HD, HD + HD0. Screen may look wrong while the part is on. **2026-11-06, pink's chip, a STOCK v6 board (510/220 as built; neither R6 nor R1 is in the loop, which runs from R6's bus pad through the 150 Ohm to JP1's right pad, so the board's values do not enter this result) with 150 Ohm from JP1's right pad to R6's upper pad, SCK drive 2: MOSI pad normal: right through 8.6 MHz, 10 MHz 131/256 right (late 9), 12+ wrong. MOSI pad HD: right 256/256 through 15 MHz, 20 MHz 20/256 (late 4), 30 and 60 wrong. HD + HD0: the same, 20 MHz 40/256. Reading: the chip's own path (output, 150 Ohm, the stubs, input) passes 15 and fails 20 at every drive; the MOSI pad's drive alone moves the limit from 8.6 to 15. No board change reaches 20 MHz; the card's 5-14 ns on top of this puts the card path at 12-15 at best. No CLK gate. `sd rc` on this board: PC8 50 ns (not valid, the loop ties it to PC10), PC9 751, PC10 851 ns.** |
| 1 | User | **USB off, power off, take the wire and the 100 Ohm off.** Meter: JP1's right pad to R6's bus pad must read open. | |
| 1 | User, Claude | Card in, boot, `sd diag`. | Back to 12 MHz: the board is as it was. |
| 4 | User | Cold boot at 330/150, stock and SLOOP, card in. Screen right? | Whether 330 can ship. Blocks every board. **2026-11-06, pink's 338/150 board, card in: stock V15 clean after a full cold boot and a minute of use (user). SLOOP 2.8.5 (the hub's current release; no 2.7.x listed) clean the same way. PASSED: 330/150 ships (the first install was overwritten by another session's flash; the V15 install also put the stock bootloader back on pink).** |
| 5 | User solders, Claude reads | **2026-11-06 16:10, a STOCK v6 board with 470 from the CLK jumper's right pad to R5's upper pad, 470 from the DI jumper's right pad to R6's upper pad, 150 from the DO jumper's left pad to R1's lower pad (510 \|\| 470 = 245 on R5/R6, 220 \|\| 150 = 89 on R1), on pink, the 16 GB card: `sd diag` 10, 12 and 15 MHz pass, 20 refused, 15 kept, 0 crc fails, twice. One step over pink's own 338/150 board (12 marginal). BUT the screen: after a plain power cycle the panel stays black while the firmware runs (console up, the card at 15 MHz); m-bootloader's own splash did come through earlier. The LCD, sharing CLK and MOSI through R5/R6, loses the firmware's drawing at 245 Ohm. Card OUT: still black, so not the card's load. A diag build with the LCD clock halved (device/lcd.c LCD_BAUD 1 = 30 MHz instead of 0 = 60): the screen comes back on the same board. So 245/89 buys the card 15 MHz and costs the LCD half its clock; at 60 MHz the LCD's edges through the 245 Ohm into the card socket's load no longer make it. Then the user ruled the LCD at 30 MHz fine, and the cold boots at 245/89 WITH THE CARD IN: stock V15 clean, SLOOP 2.8.5 clean (their own LCD clocks, unchanged). ACCEPTED 2026-11-06 evening: the order goes to 240 / 240 / 91 Ohm (E24 single parts for 245/89), Optimist's LCD at 30 MHz (device/lcd.c LCD_BAUD 1); the streaming effect of the slower LCD measured: the diag build (lcdslow, the old 6-voice reader) with the heavy stress on the Acoustic kit at 15 MHz card / 30 MHz LCD, 40 s: 140 KB/s, 0 stalls, 0 crc, but 10 dry, 150 held frames, lead min 0 ms, CPU 72 % (the day before at 10 MHz / 60 MHz LCD: 366 KB/s, 0 dry, 3 held, lead >= 14). The slower LCD holds the bus longer between the card's runs with THAT reader. Main's reader (lcd30-main.fwsc: main 17f2e696 with LCD_BAUD 1), `heavy 0` (ten card lanes every step, no inserts), 30 s on the same board at 15 MHz / 30 MHz LCD: 291 KB/s, 0 stalls, 0 crc, 0 dry, 0 held frames, lead min 35 ms (the morning on the 338/150 board at 10-12 MHz / 60 MHz LCD: 399 KB/s, 0 dry, lead min 28), CPU0 39 %, halves 131 / 129 us, no shedding. VERDICT: with main's reader the slower LCD costs nothing measurable and the lead improves; 240/91 + LCD 30 MHz stands. The clean proof of the clock (the lcdslow / lcd30 builds only changed lcd.c's fallback, which the builder's LCD_BAUD=0 may override, and the screen was seen after warm restarts only): lcd1-main.fwsc, LCD_BAUD=1 set in the builder's config, COLD boot on the 245/89 board: the firmware's screen (the 60 MHz diag build: black at the same cold boot). The builder refuses LCD_BAUD 0 with SDCARD since this evening (tools/builder/core.py validate). (Julius saw 150 distorted at a cold boot; 245 is the floor the screen still takes at 30 MHz.)** A resistor across R1 (330 = 103 Ohm, or 150 = 75), then across R5 and R6 (330 = 165 Ohm), one at a time (pad to pad, no desoldering: `worktrees/sdloop-scratch/mods/attempt-3-resistors.png`). `sd clock auto`, `sd diag`, screen. | Checks the model's largest gain on real copper; whether 12 MHz gets margin or 15 passes. |
| 6 | User swaps, Claude reads | Ribbons 50, 60, 80 mm. | Checks "ribbon does not matter". |
| 7 | User | **Rework the stock v6 board to 510 / 338 / 220 (Julius's 430 / 330 / 220, the nearest with 150 / 470 / 750 / 1k in the drawer: parallel parts only lower a value, so the 330/150 board cannot go there). Planned 2026-11-08, pictures `worktrees/sdcard-pcb-scratch/v6-rework-step0..4.png`.** Power off, cable out. Step 1: unsolder the 470 from the CLK jumper's right pad to R5's upper pad, nothing in its place (R5 alone, 510). Step 2: unsolder the 470 from the DI jumper's right pad to R6's upper pad, solder a 1k on the same two pads (510 \|\| 1k = 338; optional: left at 245, MOSI never moved the card clock). Step 3: unsolder the 150 from the DO jumper's left pad to R1's lower pad, nothing in its place (R1 alone, 220). | The v6 as built, with Julius's MOSI. Julius saw CLK at 150 distort the screen at a cold boot and 510 clean; 220 on DO caps a card/chip clash at ~15 mA. |
| 7 | User, Claude | Board in, card in, power on. Claude flashes the 60 MHz screen build on the test FM-1 (`worktrees/sdcard-pcb-scratch/sd60.fwsc`: sd-card-mots with LCD_BAUD 0; the builder now only warns about 60 MHz with SDCARD, 2026-11-08) and runs `sd diag`; then a COLD boot (power cycle) with the card in, the screen checked. | Expected: 10 or 12 MHz as the stock v6 measured on pink on 2026-11-06 (10 on one card, 12 on the other, 0 CRC), the screen right at 60 MHz. Screen wrong or black: build with LCD_BAUD 1 instead, nothing to unsolder. |

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

# SD card on the FM-1 (builder item `SDCARD`)

A microSD card wired to the LCD's SPI bus, read and written by the firmware with Petit FatFs. Status (2026-10-25): the
firmware is written and host-tested (`tests/sd_test.c`); **no FM-1 has the mod yet**, so everything marked *unverified*
below is still to be checked on the bench.

## 1. Why the LCD bus

The FM-1's chip is an AC7911B-class part in a QFN48 (6 x 6 mm, 0.4 mm pitch; the AC7911B datasheet's pinout matches
every pin the firmware uses). Its flash is a die inside the package, and **every GPIO the package bonds out is already
used** (some pads even carry two port bits: PC5/PC2, PB8/PC0, PH7/PH8, PH4/PH6). There is no free pin for a chip
select, and none reachable without soldering to the QFN's edge.

The LCD's SPI1 already has a clock (PC9) and data out (PC10), and SPI1's data **in** is PC8 (group B, the same group
as the clock and data out), which the LCD uses as D/C. An ST7789 ignores D/C while its CS is high. So:

- the card's CLK and CMD (DI) share the LCD's clock and data;
- the card's DAT0 (DO) drives PC8 through a series resistor (R1, 220 Ohm on the SD board) while the LCD is deselected;
- the card's CS is the LCD's CS **inverted**: the card is selected exactly when the LCD is not.

Six wires, one inverter, two resistors, no pin of the main chip touched.

## 2. Parts

> **Superseded (2026-10-25):** the SD board (`hardware/archive-v6-tested/`, v5: the board is the splitter, two 10-pin
> sockets, no soldering) replaces the hand-wired kit and the "v2" tap board below. The 1 kOhm figures below belong to
> that kit; the board uses 220 Ohm (section 4). The board's README is the current parts list.

**For everyone: the SD board (hardware/archive-v6-tested/, 2026-10-25).** One small board with the socket (push-push), the
inverter, the resistors and the capacitors, ordered assembled from JLCPCB with the files in `hardware/archive-v6-tested/fab/` (or
from OSHWLab once published there). v2 (20 x 45.5 mm, the back of the board above the battery) takes a 6-pin 0.5 mm flat cable from a tap board at J12, so nothing is soldered; the tap board is still to design (J12's pin map is in section 3; pin 7's supply confirmed
2026-10-28). The taps on the FM-1's board: `hardware/fm1/j12-wiring-points.png`; the chip's pins 28-32, J12's numbering
and the two 74HC595s' VCC / GND: `hardware/fm1/j12-chip-pins.png`. See `hardware/README.md`.


**The kit (2026-10-25, for every user: off the shelf, through-hole, no board to order):**

| Part | Why | Where |
|---|---|---|
| Adafruit **4682** "Micro SD SPI or SDIO Card Breakout - 3V ONLY" (or a clone, about EUR 2) | the card's socket with nothing in the way: "no level shifting or power regulator" (adafruit.com/product/4682). Its socket may not be push-push (the user: a minor downside) | adafruit.com/product/4682; AliExpress "4682 Adafruit Micro SD" (nl.aliexpress.com/item/1005012308647078.html, 2026-10-25) |
| **74HC04** in **DIP-14** | the chip-select inverter (one of its six gates; the other inputs tied to GND): big legs, works at 3.3 V, switches in 10-20 ns | any shop, "74HC04 DIP" |
| 1 kOhm through-hole resistor | in series on the card's DO, at the card's end | any kit |
| 100 nF capacitor | across the 74HC04's VCC (pin 14) and GND (pin 7) | any kit |
| thin wire (0.1-0.2 mm enamelled or 30 AWG Kynar) | the six taps | |

A transistor inverter is not enough: it lets go too slowly, and the first bits of an LCD transfer would reach a card
still selected. The 74HC04 pinout: gate 1 is pin 1 in (from the LCD's CS, PC7), pin 2 out (to the card's CS); pin 7
GND, pin 14 3.3 V; tie the unused inputs (pins 3, 5, 9, 11, 13) to GND.

The rest of this section: the parts list before the kit was chosen (the SOT-23 inverter, other breakouts).

| Part | What | Notes |
|---|---|---|
| microSD breakout, 3.3 V, **no regulator, no level shifter** | the socket on a small board with CS, DI/MOSI, SCK, DO/MISO, VCC, GND pins | e.g. the "D1 mini micro SD shield" style board. **Avoid** the common "Micro SD Card Adapter" with an AMS1117 (SOT-223) and a 74LVC125 (14 pins): made for 5 V, its regulator drops the card's supply at 3.3 V in. Look at the photos before buying. Fallback: a bare push-push TF socket ("TF-01A", 8 pins) |
| 74LVC1G04, SOT-23-5 | the inverter for CS | SN74LVC1G04DBVR or equivalent; a 74LVC1G14 (Schmitt) works the same |
| SOT-23-5 to DIP adapter board | a carrier for the inverter | optional: the inverter can also sit dead-bug on the breakout |
| 1 kOhm, 0805 or 0603 | in series on the card's DO (to PC8) | limits the current if the card and the FM-1 ever drive PC8 at once |
| 47 kOhm x 2 | pull-ups on the card's DAT1 and DAT2 | unused in SPI mode; floating they can draw current. Many breakouts have them already |
| 10 uF + 100 nF, 0805 | at the card's VCC | a card draws 50-100+ mA peaks while writing |
| 0.1-0.2 mm enamelled (magnet) wire or 30 AWG Kynar | the six taps | solderable polyurethane enamel burns off with the iron |
| Kapton tape, no-clean flux | fixing the wires, clean joints | |
| microSDHC 4-32 GB, SanDisk or Samsung | the card | FAT32 as sold. Buy from the brand's official store; fakes are common. Check a new card with f3 or H2testw |

microSD breakouts seen in AliExpress search results on 2026-10-25 (the board on each page not checked: look for the
AMS1117 / 74LVC125 in the photos before ordering):
[D1 mini micro SD shield, €1.47](https://nl.aliexpress.com/item/1005006458248548.html),
[SPI 3.3 V TF module, €2.29](https://nl.aliexpress.com/item/1005012591708160.html),
[micro SD shield 1-10 pcs, €1.43](https://nl.aliexpress.com/item/1005005591145849.html).

AliExpress search phrases that find these: "D1 mini micro SD shield", "SN74LVC1G04DBVR SOT-23-5",
"SOT23 to DIP adapter 5pin 6pin", "0.1mm enameled copper wire solderable", "30AWG Kynar wire wrap",
"0805 resistor assortment kit", "0805 capacitor assortment kit", "SanDisk Ultra microSDHC 32GB" (official store only).

## 3. Schematic

```
                         FM-1 (at the LCD connector J12 or its traces)
                         ───────────────────────────────────────────────
   LCD CLK   PC9  ──────────────────────────────────────────┐
   LCD DO    PC10 ─────────────────────────────────────┐    │
   LCD D/C   PC8  ──────────[ 1 kΩ ]────────────┐      │    │
   LCD CS    PC7  ──┐                           │      │    │
   3V3       ──┬────┼──────────┬────────────┐   │      │    │
   GND       ──┼──┬─┼──────────┼──────┐     │   │      │    │
               │  │ │  74LVC1G04     │     │   │      │    │
               │  │ │  ┌───────┐     │     │   │      │    │
               │  │ └──┤2 A  Y 4├──┐  │     │   │      │    │      microSD (SPI mode)
               └──┼────┤5 VCC   │  │  │     │   │      │    │      ┌──────────────────┐
                  └────┤3 GND   │  │  │     ├───┼──────┼────┼──────┤4 VDD             │
                       │1 NC    │  │  └─────┼───┼──────┼────┼──────┤6 VSS             │
                       └───────┘   └────────┼───┼──────┼────┼──────┤2 DAT3 / CS       │
                                            │   └──────┼────┼──────┤7 DAT0 / DO       │
                                            │          └────┼──────┤3 CMD  / DI       │
                                            │               └──────┤5 CLK             │
                            47 kΩ x 2 ──────┴──────────────────────┤8 DAT1, 1 DAT2    │
                            10 µF + 100 nF across VDD / VSS        └──────────────────┘
```

| Net | FM-1 | Card (microSD pin) | Through |
|---|---|---|---|
| CLK | PC9 (LCD CLK) | 5 CLK | wire |
| MOSI | PC10 (LCD DO) | 3 CMD / DI | wire |
| MISO | PC8 (LCD D/C) | 7 DAT0 / DO | 1 kOhm, at the card's end |
| CS | PC7 (LCD CS) | 2 DAT3 / CS | 74LVC1G04 (A = PC7, Y = card CS) |
| 3V3 | a 3.3 V point with headroom | 4 VDD, inverter VCC | |
| GND | ground | 6 VSS, inverter GND | |
| | | 8 DAT1, 1 DAT2 | 47 kOhm each to 3V3 |

**Finding the taps (User, a multimeter, 2 minutes).** J12 is the LCD's 10-pin flat-cable connector. With the FM-1 off
and the battery unplugged, beep each J12 leg against the main chip's pins 29 (PC10), 30 (PC9), 31 (PC8) and 32 (PC7)
(the QFN's pin 1 is at its dot; pins count anticlockwise, 12 a side). Mark the four J12 legs. Find a 3.3 V point
(the LCD's own VCC leg on J12 is the first candidate) and a ground. Where the 3.3 V comes from and how much current it
has to spare is **unverified**: the chip's own LDO is rated 200 mA in all (datasheet table 2-2), so a card's write
peaks should not ride on it if a larger regulator is on the board.

**J12 as probed (Julius, 2026-10-27, meter only, not yet tried with a second SPI device).** Pin 1 is the leg next to
the board's dot: J12 counts **from the right** with its opening up (Julius). The capacitor pads are test points to ground, not
parts in series.

| J12 | Net | Chip pin | Test point |
|---|---|---|---|
| 1 | GND | | |
| 2 | PC8, LCD D/C (the card's DO) | 31 | C82 outer pad |
| 3 | PC7, LCD CS | 32 | |
| 4 | PC9, CLK | 30 | C105 outer pad |
| 5 | PC10, MOSI | 29 | C106 outer pad |
| 6 | not probed (reset?) | | |
| 7 | 3.3 V (confirmed 2026-10-28: Julius's working prototype powers the card from it) | | |
| 8, 9 | not probed (backlight?) | | |
| 10 | GND | | |

All six taps sit on pins 1-7; the tap board passes all ten through to the LCD.

## 4. How the bus is shared

The firmware's screen runs from the main loop with polled DMA (`src/display/lcd.c`), never from an interrupt, so the SD
code takes the bus between two screen transfers:

1. **`sd_begin`** (`src/storage/sd/sdcard.c`): `lcd_cmd(0x00)` waits for the last pixel transfer, sends the panel a
   **NOP** and deselects it. A NOP ends any memory write (RAMWR), so the bytes the panel sees later with D/C high are
   data with no command waiting: ignored [ST7789V datasheet; *unverified on this panel*]. Then PC8 is made readable
   (input enabled, a pull-up so an absent card reads 0xFF) and SPI1 is set to the card's clock.
2. **Select the card** = the LCD's CS high, then PC8 an input (the card drives it).
3. **Deselect the card** = PC8 driven high first (D/C = data), then the LCD's CS low. Every byte clocked in this state
   reaches the panel; ChaN's driver only ever clocks 0xFF here (the "release" and dummy clocks), and the host test
   checks exactly that (`LCD sees only 0xFF`). That test's card model proves the firmware's sequencing, **not the
   panel's behaviour**: that the ST7789 ignores those bytes is the datasheet's claim, unverified on this panel.
4. **`sd_end`**: the card deselected, the LCD selected and idle (D/C high, no clock), PC8 driven again, SPI1's control
   word and the LCD's clock (`LCD_BAUD`) put back.
5. **While the screen idles** (2026-10-25 review): SD builds idle the LCD **selected** (its CS low, `hal/fm1_lcd_hw.h`
   `FM1_LCD_IDLE_CS`; the ST7789 allows CS tied low), so the card (CS inverted) is deselected and lets go of PC8 between
   its sessions. An LCD command's D/C low then never meets the card's DO; the overlap exists only inside an SD session,
   where the panel is not listening. The update loader of an SD build idles the same way. A firmware that idles CS high
   (a normal build, the stock update loader) selects the card: **the card comes out before such an update**.
6. **At boot** the card is put in SPI mode (`sd_mount_boot`: the set read mode once, `main.c`) before the screen's first command: a card still in
   its native mode (fresh, or hot-plugged) parses the LCD's traffic on CMD, may find a valid command in it and answer on
   CMD (open-drain) against PC10. The socket's card-detect reaches only a test pad (TP5), so a hot-plugged card is not seen; for
   streaming, a periodic CMD13 (status) checks the card and only a CMD13 with no answer sends CMD0 (CMD0 resets a card
   in use). The board's MOSI branch gets 510 Ohm in series (R6; CLK the same, R5), which bounds that fight to about
   6 mA. Other firmware never sends the card to SPI mode: Julius's prototype (2026-10-28) lost the stock screen with a
   card in from a cold boot with CLK wired direct, had it distorted at 150 Ohm and fine at 510.

The card's start-up runs at 400 kHz (SPI1 BAUD 149: 60 MHz / 150), then the floor, 6 MHz (BAUD 9), then the clock the detection keeps (below). The screen runs at 60 MHz
(`LCD_BAUD` 0 in every profile; 30 MHz, BAUD 1, is the fallback if the column's stub disturbs it). PC8 can be driven
by the FM-1 and the card at once for a few clocks inside a session (the card not yet let go when D/C goes high):
R1 (220 Ohm on the board) keeps that to about 15 mA.

**Timing, estimates (the 2026-10-25 reviews):** the card runs at **20 MHz** (the user). The card's DO changes up to
14 ns (tODLY, SD default speed) after CLK's falling edge; the FM-1 samples on the rising edge, 25 ns later at 20 MHz.
The DO / D/C net (the PC8 pad, the panel's D/C input through its flex, the 80 mm cable, the 55 mm column, the socket)
is about 35 pF. R1 220 Ohm: a 7.7 ns time constant, about 15 ns to a clean level: 14 + 15 = 29 ns, past 25 before the
host's (unknown) setup time. 100 Ohm: 14 + 7 = 21 ns, a marginal pass; 47 Ohm: 17 ns, comfortable. The board still
takes **R1 220 Ohm** (Julius's review and the user, 2026-10-28): a card left in SPI mode by an earlier session drives DO
against D/C whenever a firmware idles the LCD's CS high, up to 33 mA at 100 Ohm, 15 at 220. Real cards (5-8 ns)
reach 20 MHz through 220; the spec's slowest does not, and the auto clock (below) steps down for it. `sd bench`'s
20 MHz CRC rows decide. Julius's hand wiring (510 / 510 / 270 Ohm at the card end of a 10 cm stub) read clean CRCs up
to 6 MHz and failed from 8; the board's short traces are expected to do better (unmeasured). 30 MHz is not expected.

**The clock: detected at the mount** (the user, 2026-11-04: "this is sandisk ultra, it should go higher, we should
have a speed detection feature"; it replaces the 2026-10-25 choice of two clocks). Measured on the black FM-1 with a
SanDisk Ultra 16 GB SDHC (47524040): 20 and 30 MHz gave garbage past the start-up, 7.5 MHz read 733 KB/s with no CRC
failure, 6 MHz is the floor. Whether the limit is the card or the prototype board's wiring (the card shares the LCD's
bus) is not measured, so the detection is built to be safe on either.

The steps: SPI1's clock is 60 MHz / (BAUD + 1) with BAUD a whole number, so the clocks from the high-speed limit down
to the floor are exactly these (`hal/fm1_sd_clock.h`):

| BAUD | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| MHz | 30 (high speed only) | 20 | 15 | 12 | 10 | 8.57 | 7.5 | 6.67 | **6, the floor** |

60 MHz (BAUD 0) is past high speed's 50 and never tried; 30 is past default speed's 25 and tried only on a card that
took CMD6's high speed.

The walk (`sd_clock_probe`, sd_mmc.c), in a bus session after the mount and the DMA probe:

1. The start-up (CMD0, CMD8, ACMD41, CMD58, CMD59 CRC on) is always at 400 kHz, the boot record at the floor; only data
   transfer moves faster. SDv1, MMC, a card whose CSD gave no size, and the bit-bang read mode stay at the floor.
2. A reference pass at the floor: the CSD (CMD9) and the CID (CMD10), each data block's CRC16 checked and the
   register's own CRC7 noted; 8 blocks by CMD17 spread over the card; 16 blocks from sector 0 by one CMD18, every
   block's CRC16 checked. Its fingerprint is a CRC16 over the registers' bytes and every block's CRC16. The floor failing
   its own pass: the floor, nothing else tried.
3. From the floor up, one step at a time, each step gets **two** such passes, each with no CRC failure, no timeout and
   the floor's fingerprint (a line stuck low reads zeros whose CRC16 is right: the fingerprint catches that). CMD6 is
   asked before 30 MHz only, and switched only when its status, CRC right, says high speed is supported; else the walk
   ends there.
4. **The first step that fails ends the walk.** The clock goes back to the floor before the CMD12 of a CMD18 left
   running, the card is checked by a pass at the floor and started again (400 kHz, the start-up) when that fails too; a
   card that no longer reads at the floor ends it, the mount says no card. The step below the failure, the fastest that
   passed, is kept and checked once more at its clock (that check failing: the floor).
5. No step failed (the top reached, or the time spent): **the step below the fastest that passed**, the margin no
   failure showed. So a card that reads everything runs at 20 MHz, one that refuses high speed at 15.

So a card and board that read at 8.57 MHz and not at 10 run at 8.57; garbage from 15 MHz up keeps 12. (Until 2026-11-04
the walk went down from the fastest and a timeout ended it at the floor: garbage at 20 MHz looks like a timeout, so the
black FM-1 would have landed at 6 MHz, below the 7.5 measured.) The time is bounded well under the 8 s watchdog, which is
fed at every pass and before a start-up again: every wait in the probe is 20 ms, so a pass is 0.56 s at most; no step
starts after 1 s; ~7.2 s at the very worst, every wait at its limit, ~1.7 s between feeds. On time, a card reaches the top
in 17 passes. The real time on the FM-1 is not measured yet (`sd diag` prints it). Every disk read and write feeds the
watchdog too: a card's first index pass (`sdx_update`) holds one main loop pass for as long as the card takes.

At run time a block whose CRC16 fails above the floor drops the clock one step at once (this session and every later
one, counted) and the block is read again there, so nothing is lost; at the floor it is read once more and a second
failure is an error, as before (`sd_crc_retry`).

Where to read it: **SYSTEM > SD CARD**, its second page, `CLOCK 7.5 MHZ` (read-only; `-` with no card); the
console's `sd` and **`sd diag`** print `clock 7.5 MHz (auto: fastest passed 8.6 MHz, probe ms N) crc fails N steps
down N` and one line per step: `pass`, `CRC failed`, `data differs from the floor's`, `timeout or refused`,
`skipped (no high speed)` or `not tried`, the step kept marked. `sd clock MHZ|auto` sets the fastest step at or under
MHZ, unchecked, and mounts again; `sd speed` reads the CSD at every clock with its CRC16 checked. Host-tested on the
card model (`tests/sd_clock_test.c`: wrong above each step in turn, timeouts, zeros with a right CRC, a block in 30
wrong, one step alone wrong, noise past the start-up, the card gone, the run-time step down by bytes and by DMA; `tests/sd_test.c` clocks
per card type). The emulator cannot run an SD build past the boot mount (it models SPI1 as the LCD only). This is the
classic firmware's driver (sd_mmc.c on hal/fm1_sd_bus.h); the release on fm1-base has the same walk on the same steps
in fm1-base's own driver (device/sd.c, device/sd_clock.h, `tests/sd_clock_test.c` there), its CLOCK line the clock kept.

**The screen at 60 MHz over the longer path is a bench result, not a design assumption.** The ST7789V's serial write
cycle is 16 ns at least (SCL high and low 7 ns each): 60 MHz (16.7 ns) has no datasheet margin today and works because
the stock flex is short. The mod's path is J12, the 80 mm cable, J2, the pass-through, J3 and the LCD's flex: about
150 mm and four contact sets, with only pins 1 and 10 grounded. Ringing that narrows a clock pulse under 7 ns or
crosses the threshold twice is a double clock: shifted bytes, stripes. **30 MHz (`LCD_BAUD` 1) is the design's
guaranteed mode** (a full frame 30.7 ms instead of 15.4); 60 MHz is kept if the bench's soak passes (section 6).

**A risk to watch:** idling with CS low loses the ST7789's resync on CS's rising edge between the card's sessions. A
single glitched clock edge would shift the panel's bytes until the next card session raises CS. None is expected;
the soak looks for corruption that persists and clears on a card access. The 10 kHz tick
never touches SPI1 here (`fm1_lcd_bl_scan` drives PA2 only), so it cannot clear the done bit the SD code polls.

**Which way SPI1 reads is unverified.** The flash driver reads SPI0 with CON bit 12 (DIR = receive) after writing
0xFF (`hal/fm1_flash.h` `fl_rx`); whether SPI1's group-B data-in follows PC8 in transmit mode as well is not known.
`hal/fm1_sd_bus.h` has three ways, picked at run time with the console's `sd mode`:

| Mode | How a byte is read |
|---|---|
| `duplex` (default) | sent in transmit mode, the shifted-in byte read back from BUF |
| `dirrx` | as `fl_rx`: CON b3 + b12 set, 0xFF written, BUF read |
| `bitbang` | PC9 / PC10 / PC8 as plain GPIO (SPI1 off its pins for the session: IOMAP_CON1 b4 cleared, *unverified*) |

In `dirrx` mode whether the data-out line stays high (the card needs 0xFF on CMD while it answers) is also
unverified; a logic analyser on CLK / DO / PC8 settles both questions at once. `sd` (the mount) tries the set mode
first and then the other two, and prints the one that found the card.

**Blocks by DMA (2026-10-25, unverified on an FM-1).** A 512-byte block of a CMD17 / CMD18 read can come in by SPI1's
DMA: CON b3 + b12 (receive, as `dirrx`), then `SPI_ADR` and `SPI_CNT`, which starts it, as the SDK's own SPI receive does
(`ac79-sdk` `apps/wifi_story_machine/video/spi_recv.c`: b12, ADR, then CNT "starts the hardware receive"; that one is a
slave, ours is the master clocking the bytes in: unverified). The main loop polls CON b15 as `lcd_sync` does; the CPU is
free while the block comes in, but **the bus is not**: the screen waits until the read is over. PC10 is pulled up for
the session in case SPI1 lets DO float while it receives (a card ignores DI during a read unless it looks like a
command). After the mount, `sd` reads sector 0 by bytes and by DMA, checks both CRC16s and compares them, and turns the
DMA on only when it read right (`dma ok` / `dma off` in its answer; `sd dma on|off` overrides). Writes (`sd write`)
send a block by DMA the LCD's own way (transmit DMA, proven by the screen) when the DMA is on.

**Estimates (not measured):** a block is 4,096 bits: 205 us of wire at 20 MHz, 137 us at 30 MHz. CMD18 by DMA: about
2.2-2.4 MB/s at 20 MHz and 3.2-3.5 MB/s at 30 MHz, less the card's gap before each block (a few to tens of us); the CPU
a block: the token poll, the DMA's start and end, the 2 CRC bytes, a few us. By bytes (`duplex`, `dirrx`): every byte
a register round trip, the CPU busy the whole block, roughly 1 MB/s. `sd bench` measures all of these.

## 5. Firmware

| File | What |
|---|---|
| `hal/fm1_sd_bus.h` | the bus: select / deselect through the LCD's CS, byte in / out (three modes), clocks |
| `src/storage/sd/sd_mmc.c` | ChaN's SPI-mode card driver (Petit FatFs `generic/mmcbbp.c`): MMCv3, SDv1, SDv2 byte or block addressed; the waits timed on TIMER4; the CSD read for the capacity. Added: the polled block reader `sd_rd_*` (CMD17, CMD18 stopped by CMD12), `disk_readblocks`, `disk_writeblock` (CMD24), the CRC16; a whole sector into an aligned buffer by DMA in `disk_readp`. CRCs on (2026-10-25 review): every command's real CRC7 and CMD59, so the card refuses a garbled command (a stray CMD12 on DI during a CMD18 run among them); every block written carries its CRC16; every block read is checked (`SD_RD_CRC` 1, a 32-byte nibble table) |
| `src/storage/sd/pff.c`, `pff.h`, `pffconf.h`, `diskio.h` | Petit FatFs R0.03a (ChaN, 1-clause BSD: `LICENSES/BSD-1-Clause-FatFs.txt`), its own compile unit (`tools/build.py`); configured: read, write, seek, directories, FAT16 + FAT32. Every build but the big layout's (the bring-up) |
| `src/storage/sd/fatfs/` | full FatFs R0.16 (ChaN, the same licence), the big layout's (LAYOUT 1: `FELUCCA_SD_FATFS`), its own units: long names in UTF-8 (one static buffer), code page 437 for the aliases, `f_expand`, fast seek, `FF_FS_TINY`, exFAT off (`ffconf.h` says what differs from R0.16: ffunicode.c without its DBCS tables, an `f_expand` fix, `f_unlink_entry`, `ff_string.h` for the freestanding build) |
| `src/storage/sd/sd_diskio.c` | FatFs' disk layer over `sd_mmc.c`: CMD18 runs with every CRC16 checked (and the auto clock's retry), DMA into aligned buffers only, a bus session per read or write (so FatFs is called directly), a card that fails fails every later call at once; `get_fattime` a placeholder date; `sd_map_run`: a file's contiguous runs from its link map |
| `src/storage/sd/sd_layout.c`, `sd_hash.c`, `sd_alloc.c`, `sd_index.c`, `sd_browse.c`, `sd_save.c` | the card's layout of section 8 (big layout only; host-tested, `tests/sd_layout_test.c`) |
| `src/storage/sd/sd_mode.c`, `src/ui/optimist/op_sdcard.c` | SD card mode on the device (section 9): the boot's card, the projects, presets, kits and samples by name, the PROJECT page's lists |
| `src/storage/sd/sd_stream.h`, `sd_stream.c`, `sd_stream_plan.c` | samples streamed from the card (builder `SD_RING`, section 7): the rings the voices read, the main loop's CMD18 runs between the screen's strips, the heads and rings per project in the arena |
| `src/storage/sd/sdcard.c` | the API the rest calls (`sd_mount`, `sd_open`, `sd_read`, `sd_write`, `sd_seek`, `sd_opendir`, `sd_readdir`), each call taking and giving back the bus |
| `src/io/console_sd.c` | the `sd` commands (console builds only; included by `src/io/console.c`) |
| `tests/sd_card_model.h`, `sd_test.c`, `sd_fs_test.c`, `sd_layout_test.c` | the host tests: a byte-level card model (with power cuts) on FAT16 / FAT32 images from a real formatter (`tests/fixtures/sd/make_images.sh`): the driver and Petit FatFs; full FatFs; the card's layout |

**Filesystem limits (Petit FatFs, every build but the big layout's):** FAT16 and FAT32 only (exFAT would need full FatFs with long names, roughly 15-20 KB
of flash, *estimate*); 8.3 names (a long name shows as its alias, `LONGFI~1.TXT`); one open file at a time; files are
never created or grown, a write stays inside the file's size and starts on a sector boundary. The way to use it:
a computer makes fixed-size files (`BACKUP01.BIN`, `REC01.WAV`, ...), the FM-1 fills them.

**Cost:** 5,032 B of flash, 20 B of RAM and 556 B of pool with the console (measured 2026-10-25 on the default
profile with `USB_CONSOLE` instead of `USB_AUDIO`; the FATFS state and the console's sector buffer are in the pool).
That build is 2,316 B over the app slot: a console + SD test build has to leave something out (the user's call).

**Full FatFs (the big layout, 2026-10-26; local measurement builds of `sd-card-mots` + `SDCARD=1`, not gpufarm):** the
mount only +1,000 B of flash, +516 B of RAM (the long name buffer), +524 B of pool against Petit FatFs; with the console
+7,076 B of flash (the console build then 764,508 of 765,696 B); FatFs' functions the layout uses all reached: +14,092 B
over no SD at all; with the layout's modules reached too: +20,152 B (764,004 of 765,696: 1.7 KB left in that profile).
The builder's pairs (`costs.json`) hold the first two until `measure_costs.py --missing` measures them.

## 6. Bring-up

Each step names who does it. Claude never writes to the FM-1.

1. **User**: orders the parts (section 2), meters J12 (section 3).
2. **User**: wires the breakout and the inverter to the six taps; checks with the meter that no tap is shorted to its
   neighbour, then that the card's VDD is 3.3 V with the FM-1 on.
3. **Claude**: builds a console test image with `SDCARD=1` and `USB_CONSOLE=1` (and whatever the user picks to leave
   out to fit) and hands over the `.fwsc` path. **User** installs it.
4. **User**, at the serial console (`tools/fm1_console.py`), with a FAT32 card holding `HELLO.TXT`:
   - `sd`: should print `sd ready  SDHC  MB ...`, `fat 32`. `sd no card` with a card in: try `sd mode dirrx`, then
     `sd mode bitbang`, each followed by `sd`.
   - `sd raw 0`: the first sector; `55 AA` should end it (`sd raw` shows only 64 bytes: look for the partition entry).
   - `sd ls`, `sd cat HELLO.TXT`.
   - `sd`: the line `read mode ... dma ok|off (probe: bytes crc 1 dma crc 1 same 1)` says which read mode found the card
     and whether the DMA read sector 0 right.
   - `sd bench` (no file): one line a mode (`duplex`, `dirrx`, `dma` by CMD18 and by CMD17, at 20 and 30 MHz, then
     `bitbng`) over 64 blocks of the card: `KB/s`, `cpu_ns/blk` (the CPU inside the driver), `us/blk` (wall),
     `free_us/blk` (the DMA running, the CPU free), `crc_bad N/64` (0: read right) and `sum` (the same on every row
     that read the same data). A row with `ERR` or `crc_bad` above 0 does not read on this FM-1 (or at that clock).
   - `sd bench BIG.BIN` (a file of a few MB made on the computer): the read speed through Petit FatFs (`read KB/s`),
     then a cluster per CMD18 (`stream KB/s`).
   - `sd write DATA.BIN` (a file made on the computer, its contents lost): written block by block, read back,
     `wrong B 0`.
   - The screen must look the same after each command (no stray pixels, no lock-up).
   - **The screen soak** (the board fitted, the screen at 60 MHz): a static full-screen pattern for minutes, with the
     card in and out, then normal use; any stripe or shifted bytes: the profile goes to `LCD_BAUD` 1 (30 MHz). Also
     watch for corruption that persists and clears on a card access (section 4, CS idle low).
5. **Claude**: from what the console printed, fixes the mode / timing and makes the working mode the default.

**Measured on a hand-built prototype** (Julius, 2026-10-28: a microSD breakout, a 74HC04 and through-hole resistors on
the probed J12 pins; SD reads, a FAT32 reader and a flash-slot loader in m-bootloader on it):
- Card reads work with SPI1 CON = 0x4029 (the LCD's 0x4021 plus bit 3: separate DI and DO), PC8 an input with its
  pull-up; CON bit 12 is not needed. That is `FM1_SD_DUPLEX` (`hal/fm1_sd_bus.h`). The LCD NOP before each session works.
- The stock firmware runs the LCD at about 12 MHz and keeps its CS low while idle, with high gaps of up to about
  2.8 ms and no clock edges while CS is high.
- Clean reads (CRC16 checked) up to 6 MHz with 510 Ohm on CLK and DI and 270 on DO at the card end of a ~10 cm wire
  stub; they fail from 8 MHz. That says nothing yet about the board's 20 MHz.
- The stock screen went blank with a card in (cold boot: the card in native mode) with CLK direct and DI through
  510 Ohm, while the LCD traffic looked identical on a logic analyzer; 150 Ohm on CLK distorted the image, 510 fixed
  it (the board: R5, R6 510, section 4). m-bootloader's own screen worked in every case.
- The card stays in SPI mode across a warm reset until it loses power: after a bootloader-driven firmware switch (a
  chip reset) the next firmware starts with the card already in SPI mode.

## 7. What comes after the bring-up

- **Backups to the card**: the backup stream (the web editor's `.optimist-backup`) into a fixed file.
- **Samples from the card**: the slicer and the sample sets read from files, M8-style (the start of each sample kept in
  RAM, the rest streamed; roughly 2-4 KB a playing voice, *estimate*). Moving the built-in sample sets off the flash
  frees about half the image for code.
- **Recording**: into a preallocated WAV; 176 KB/s at 44.1 kHz stereo is about 8 % of a 20 MHz bus, but card stalls
  (100 ms and more while writing) need a buffer of about 32 KB in the pool.
- **Plugins** (engines and effects loaded from the card into the pool, per project): about 60-120 KB of code and state
  per project once the pool is shared out again (*estimate*; whether code runs as fast from the pool as from RAMTEXT
  is unmeasured).
- **Faster reads**: built (CMD18 and the DMA, section 4); what is left is the streaming itself: a main-loop task that
  takes the bus between screen strips into the voices' buffers. **Bus budget (estimates):** CMD18 by DMA at 20 MHz is
  about 1.9 MB/s; eight mono voices at 48 kHz 16-bit are 768 KB/s, 40 % of the bus, and the screen cannot draw during
  a CMD18 run (deselecting the card aborts it). A 32 KB cluster run is about 16 ms of bus, a visible UI stall: the run
  length is a parameter (8 blocks, 4 KB, about 2.2 ms), runs interleaved with screen strips, measured with
  `fm1_in.ui_gap_max`. The card's presence: a periodic CMD13, CMD0 only when CMD13 gets no answer (section 4, 6).
- **Streaming's buffers** (the user, 2026-10-26): (1) the PRE-BUFFER, each sample's head kept in RAM so a note starts at
  once while the card's first block comes (a few ms, 100 ms and more while the card is busy): ONE GLOBAL SETTING in ms
  (25 / 50 / 100 / 200, default 50; the KB it costs shown beside it: 50 ms at 48 kHz 16-bit mono = 4.7 KB a sample,
  stereo twice), the heads placed in the arena per project for the samples the project uses (refused + drops proposed
  when they do not fit); a per-sample override later if wanted; (2) the RING per playing voice, the stream's lookahead
  filled in runs between screen strips: a builder value (2-3 runs, about 12 KB a voice at most); (3) a voice whose
  stream runs dry fades out at once (no click) and the console counts it, so the setting is chosen by measurement.
- **Streaming, built (2026-10-26, host-tested only; builder `SD_RING`, `FELUCCA_SD_STREAM`: full FatFs, SAMPLE and
  ARENA too).** `src/storage/sd/sd_stream.h` (the audio's side), `sd_stream.c` (the main loop's runs, the sources),
  `sd_stream_plan.c` (the memory per project), SAMPLE's CARD set (`engines/sample/eng_sample.c` `smp_card_*`):
  - **A source** is a WAV the project plays (PCM 8 or 16 bits, mono or stereo, up to 4 x 44.1 kHz; a stereo one
    mixed to the voice's mono): its data's place, its format, its runs on the card (FatFs' fast-seek map converted
    once at the load into (file sector, card sector, count) runs: streaming never reads the FAT), its head.
  - **A stream** (one a sounding voice, `SST_N` 8) is a ring of `SD_RING` KB in 512-byte slots, each slot tagged
    (generation << 24 | the file's sector + 1) by the main loop after its data: the audio reads a slot only when its
    tag is the sector it wants under its own note's generation, so a note started again in the middle of a run never
    reads the old note's block (the tags are cleared at the start, the generation checked before a tag is written and
    by the reader). The reader's fast path is one compare a sample (`sst_at`: the window, the head or one slot);
    leaving it gives the slot back. Play order with loops: after the loop's last sector, the sector of its start (or of
    the head's end: a loop starting in the head reads the head again, from RAM).
  - **The runs** (`sst_service` in the main loop's idle wait and once a pass, `sst_strip` between two of the screen's
    strips, `lcd_blit`): the stream with the least time left that has a free slot gets one CMD18 of at most `SST_RUN`
    (8) blocks, contiguous on the card and in play order; between strips only a stream with less than `SST_URGENT_MS`
    left (2/3 of a ring, 40 to 64 ms; the screen draws on otherwise). A block's token not come in 3 ms (a busy card): CMD12,
    the bus back to the screen, the card asked again 2 ms later, so a 150 ms stall never freezes the screen (tested: the
    longest wait before a strip stays under `SST_STRIP_MS` + 3.6 ms through it). A block's CRC16 failing: read again (`sd_crc_retry`). Three runs failing
    in a row: the card is gone (`sd_gone`), the voices run dry and fade. A voice ended without its engine knowing
    (a release's end, a steal) frees its stream with its active flag; one no longer rendered at all (its beat still
    for 50 ms) is taken back.
  - **The card before the screen** (the user, 2026-11-05: "sd card should have prio over screen", after "when hold record and
    the ring to clear track fills in, audio is dropping" and "switching to arp also drop sample voices"). Every page change
    costs the same whatever the page: a clear of 115,200 B (21 ms of the bus, no turn for the card in it) and 16 strips of
    about 7,170 B (`tests/ui_optimist_buscost.h`, `BUS_COST=1`). The rule: before each strip and each 16 rows of a clear the
    streams with less than `SST_URGENT_MS` left get their runs, up to `SST_STRIP_MS` (8 ms), then that strip goes whatever
    the rings: the screen slows, it never stops. A frame that would start with a stream short is not drawn at all, up to
    `SST_HOLD_MS` (100 ms) since the last one (`sst_frame_late`, counted: the console's `sd stream`, "frames held for the
    card"): the main loop gets the panel and the card. A card gone, busy or writing gives no run, so no wait. On fm-1base a
    long turn reads the panel (`opt_in_scan`), its edges kept for the next frame. `tests/sd_stream_bus_test.c`, 8 voices of
    44.1 kHz mono at 1,147 KB/s (12 MHz), a strip's drawing assumed 1 ms and 2.5 ms (not measured), rings of 8 KB:

    | | before: dry of 8 | now: dry | paints a second now | longest paint now | least lead now |
    |---|---|---|---|---|---|
    | the REC ring's whole panel every frame | 1, 2 | 0, 0 | 26.5, 16 | 40, 72 ms | 24 ms |
    | a page change every 300 ms | 3, 4 | 0, 0 | 5 (each one), 4.5 | 132, 221 ms | 24 ms |
    | the whole screen every frame | | 0, 0 | 13.5, 8.5 | 90, 152 ms | 24 ms |

    Over the bus (300 KB/s) voices run dry, counted, and the screen goes on (13.5 paints a second). The REC ring and the
    held key's ring draw the panel once, then only the box round the arc that moved (`op_draw.c ring_step`): 59,040 B a
    frame became 338 B at most, the same pixels as a whole repaint (`tests/ui_optimist_buscost.h`).
  - **No ring free.** A note that finds no ring is silent (never its head alone) and counted (`sst_nostream`, NO RING). The
    rings are the plan's count (`sst_nring`, at most `SST_N`, 8), not `SD_RING`, which is the KB of each. A ring is freed by
    the voice's active flag at the next note, by the audio itself, so the main loop's speed does not touch it, and no ring
    stays held (`tests/sd_kit_card_test.c`). The kit's files last 0.25 to 1.6 s, so a sampled kit on its 3 rings silenced
    212 of 328 hits of a busy pattern, 16ths at 120 BPM, 8 bars: 4 rings 148, 5 rings 118, 6 rings 0 (a ring a drum voice,
    `NDRUM`). Now: 6 rings for the kit; when the arena has no room (`sst_place_rings`, the user: "i would say 5 is min", "or
    we can use shorter ring") 5, then every ring shorter, 2 KB a step down to 4 KB (`sst_k`, one length for all the rings:
    the streams share them, so a sample part's shorten too), then refused: MEM FULL CARD, as ever (the drop offered, the
    pre-buffer that fits said, no card sample plays until it fits). A hit with no ring takes the oldest card hit's (`lane_card.c dlc_ring_free`, counted as "tails cut for a ring": 198 tails on 3 rings, none
    on 6). Still silent: a part's 5th note (`SST_PART_RINGS`) and the 909's third.
  - **Dry**: a voice whose next slot is not there fades out at once over 64 samples (1.5 ms) from the sample it was
    at, then ends; `sd stream` counts it (`dry`). A note with no ring free is silent and counted (`no ring`).
  - **The memory** (`sd_stream_plan.c`): each part's CARD sample by its hash (`sst_part_set(part, hash)`: the sample
    picker and the project's load call it), looked up in the index; one arena block (`SST_ARN_K`, after the other
    buffers: `engines.c`) holds the rings (as many as the CARD parts can sound at once: a mono part 2, else the
    engine's voices, 8 at most) and each source's runs and head. Placed again when the parts' samples, their voice
    counts or the pre-buffer change (the CARD voices let go first, their sources not ready: they fade; the old block
    given back 20 ms later), and at every mount (a run is a sector number: another card's would be garbage). No room:
    refused (`sstp.state` 3): the project's card samples silent, MEM FULL CARD for the UI to say once, the proposal
    `sstp.drop` (the parts any one of whose engine memory would make room, one drop at a time as model.c mem_offer
    proposes) and `sstp.fit_ms` (the longest pre-buffer that fits as it is); tried again every 200 ms, placed by
    itself once it fits. A hash not in the index: that part MISSING (`sstp.missing`), never another sound.
  - **The setting**: `sst_pre_ms` 25 / 50 / 100 / 200 (default 50), `sst_pre_kb(ms)` the KB it costs with the
    project's samples (for the page to show beside it). Kept in the settings: not yet (the UI's).
  - **Pitch**: a card sample plays at most an octave up (`SST_UP16`: each octave doubles its reads); the root is C4.
  - **Cost** (local measurement builds, 2026-10-26, `sd-card-mots` + SDCARD + ARENA 96, PROPHET off to stay in the
    slot in both): SD_RING 8 with SAMPLE on: flash +13,580 B, RAM +620 B, pool +3,696 B, RAM code +1,820 B. SAMPLE
    alone is about 1.2 KB of flash and 0.6 KB of RAM code (costs.json); most of the rest is FatFs' and the index's
    code the streams are the first to reach (the sample picker reaches it too). `sd-card-mots` + SDCARD + ARENA 96 +
    SAMPLE + SD_RING 8 is 3,244 B over its slot (768,940 of 765,696 B): something has to go in that profile. At run
    time the block is in the arena: 8 KB rings x 8 voices = 64 KB plus the heads (4.4 KB a 50 ms mono 16-bit sample),
    only while the project plays card samples; SD_RING 4 halves the rings.
  - Tests: `tests/sd_stream_test.c` (the card model with an interrupt-driven audio clock: every sample checked
    against its file; stalls at a start and while playing, loops, restarts from the interrupt, a CRC glitch, the card
    pulled, too much for the bus), `tests/sd_stream_plan_test.c` (the arena: placement, setting, refusal and
    proposal, retry, rings by voices, missing), `tests/sd_stream_engine_test.c` (SAMPLE's CARD set in the host
    render: pitch, octave cap, the dry fade without a click, LOOP, a source let go, rings by notes).
  - Drum lanes from the card (built 2026-10-26, FELUCCA_SD_LANES = SD_STREAM with the drums): a lane's SRC CARD plays
    its card sample as a one-shot through the same reader (`drums/lane_card.c`; a hit cuts the lane's hit before, on
    its ring; TUNE an octave up at most, DECAY, CUT, LEVEL as a flash sample's). USE on the SAMPLES list with a drum
    lane picked sets it; the project's samples record holds the lanes' after the parts' (a record of before reads as
    the parts'), the plan gives a lane its slot only while it is on CARD, and a kit saved to the card carries its
    lanes' samples ("LCRD" after the kit). Tested: `tests/sd_stream_engine_test.c`.
  - SLICE on the card (`storage/sd/sd_slice.c`, SLICE's SRC CARD): a slicer jumps between slices, so one head a
    sample cannot start it at once, and a head per slice costs more than the sample (32 x 50 ms of 16-bit: 141 KB).
    So the part's card sample goes into the streams' arena block whole, as mono IMA ADPCM at its own rate (a one-bar
    break of 2 s at 44.1 kHz: 44 KB), and is scanned as a user slot (grid, AUTO onsets): REV, AUTO, LOOP play as
    from the flash. USE with a part on SLICE keeps SLICE and sets SRC CARD (another part: the sampler popup, UI-OPTIMIST-DESIGN.md 4.14); the plan places again when a part goes on
    or off SLICE CARD (no ring, no head for it). Too big for the arena: refused as any card sample (MEM FULL CARD);
    nothing placed: silent, never BREAK. Tested: `tests/sd_stream_engine_test.c` (the slices by key, REV, ADPCM
    25 dB or better), `tests/sd_stream_plan_test.c` (the block, the rings).
  - A card WAV's "smpl" chunk (before or after the data, `sst_wav`): its unity note is the note that plays it at its
    rate (C4 when none), and its first loop is what SAMPLE's LOOP repeats (the whole sample when none, or when the
    loop lies outside the data). LOOP off plays to the end either way. Tested: `tests/sd_stream_test.c` (the parse),
    `tests/sd_stream_engine_test.c` (root, loop held, LOOP off).
- **Random access: GRAIN and STRETCH on a card set (built 2026-10-29, host-tested only;** the user: "GRAIN and STRETCH
  must stream card sets"). A grain starts anywhere in its zone (POS, SPRD, reversed grains), STRETCH's 4096-sample window
  moves slowly forward and wraps into the loop: a linear stream cannot follow either. `storage/sd/sd_stream_rc.h` (the
  audio's side), `sd_stream_rc.c` (the main loop's), the engines' card paths (`eng_grain.c`, `eng_stretch.c`, the zones
  from the card: `eng_sample.c smp_cz`):
  - **A cache** per part on GRAIN or STRETCH with a card set (`SST_PART_RAND`): slots of one 512-byte card sector, any
    sector of any of the part's sources in any slot, each tagged (source << 24 | file sector + 1). GRAIN's is
    `SST_RAND_KB` (8 x SD_RING, 16 to 32 KB: 32 with SD_RING 4), STRETCH's 16 KB; in the streams' arena block after the
    rings (33,280 B and 16,640 B with the tags), placed and refused as the rings are (MEM FULL CARD). A zone only they
    play has no head (the pre-buffer is a stream's start); SAMPLE on the same set shares the zones with their heads.
  - **Wants** (the audio's, each a sector range of one source by a due on the audio's clock): GRAIN's PLAN (`gr_plan`,
    the block hook) knows the part's next grains up to 60 ms early, exactly: their positions are the part's generator's
    next numbers and the countdowns run as the render runs them, so their sectors are wanted by the block they start in;
    STRETCH wants its next window as soon as it read this one (a frame, 46 ms, early) and the one after. The scheduler
    (`sst_pick`) weighs a cache's most urgent want against the streams' leads, least time left first (a stream first on a
    tie), between the screen's strips only under 40 ms as a stream's; a full cache takes a want only 30 ms before its
    due. When nothing is wanted, the REGION (where any grain can start: POS +- SPRD / 2 and the span) is filled from
    slots no one used for 300 ms.
  - **Pins**: a sounding grain pins 25 ms of its reading ahead of the reader (the rest wanted by when the reader gets
    there: a grain streams), a reversed one its range, STRETCH the window it reads and the last one; the main loop never
    takes a pinned slot (the audio pins, then looks at the tags; the main loop clears a tag, then looks at the pins).
    A victim: an empty slot, else one no want holds (used longest ago), else one held for a later due.
  - **Late and missed**: a grain whose sectors are not there waits for them silent (its window not begun) up to 20 ms,
    then is given up; one that loses a sector while it sounds (never while pinned: a placement taken away) holds its
    sample and fades out over 4 blocks (2.9 ms). STRETCH's reading waits up to 16 blocks (its frame's work paced into
    the rest), then reads the last window again with its scan held (a freeze) or, with none, silence (the overlap-add
    fades out). Counted per cache: at once, late, missed (the console's `sd stream`); misses an eighth of the reads or
    more, 4 or more in 2 s: "T1 CARD SLOW" once (`sd_mode.c sdm_slow_tick`), said again after the part's cache went.
  - Tested (`tests/sd_grain_test.c`, the byte-level card model at 1.9 MB/s, full FatFs, the real scheduler, the audio
    an 8-block interrupt in the middle of the runs): with an ideal card every GRAIN preset on PIANO / VIBES / FLUTE and
    every STRETCH preset bit-identical to the flash's; in time: GRAIN 223-335 grains at once, 4-6 late, 0 missed, 45-61
    KB/s (2.4-3.2 % of the bus); STRETCH 0-2 windows late, 0 missed, bit-identical, 2 KB/s; stalling 100 ms every
    600 ms: GRAIN 7-11 missed, STRETCH 1-2 frozen frames, never a click (the second difference within the flash's +25 %);
    SAMPLE streaming VIBES beside GRAIN on PIANO never dry. Host CPU of the render (wall ns, noisy): GRAIN -7..-1 % on
    gpufarm, +7..+20 % on a Mac; STRETCH -5..+1 %
    against the flash's (`sd_grain_test` prints it). 24 KB for GRAIN misses some grains of these presets, 16 KB many.
  - Not built: GRAIN / STRETCH on a part's CARD sample (a user's WAV: their SRC has no CARD value); the cache's memory
    model on a dual-core FM-1 relies on in-order stores (compiler barriers only), as the streams' does.
- **Streaming on the bench** (when an FM-1 has the card; a console build: USB_CONSOLE, which needs room in the
  profile): 1. `sd`, then `sd bench` and `sd bench Optimist/Samples/x.wav`: the clock, the DMA, KB/s by CMD18 (the budget the
  streams share). 2. The index pass, then `sd stream part 1 Samples/x.wav` (and parts 2, 3), SAMPLE on those parts,
  SET CARD; `sd stream`: plan 2 (placed), each source's runs (1 = contiguous), the need. 3. `sd stream reset`, play
  for a minute: chords of 8 notes, then a held loop an octave up, with the screen busy (turning a knob on a page that
  redraws); `sd stream`: dry 0, KB/s, `longest run us` (the screen's longest wait: about 2,200 at 20 MHz, 1,500 at 30
  expected), `least lead ms` (the margin left: over 20 is comfortable), `stalls`, `crc retries`; `keys` for
  `keys_ui_gap_max_ms` (the main loop's longest pass, with the runs). 4. Each pre-buffer (`sd stream pre 25`, 50,
  100, 200) with notes started fast (a drum-like pattern of short one-shots): dry counts per setting decide the
  default. 5. The same on a cheap card and a good one, FAT32 formatted by the SD Association's formatter (32 KB
  clusters) and by a computer (small clusters: `runs` per source). 6. `profile` / the CPU guard's numbers with 8 CARD
  voices against 8 SAMPLE (flash) voices: the reader's cost a sample (expected small: one compare, a slot change
  every 256 samples). What decides: the ring size (SD_RING) from `least lead ms` and the dry count under load; the
  run length (`SST_RUN`) from the screen's smoothness against `longest run us`.
- **Recording (built 2026-10-26, host-tested only: builder item SD_REC, bit 416)**: the master output, 16-bit stereo
  at 44.1 kHz (176 KB/s), into `Record/rec-nnnn.wav`. The writer is the mirror of `sd_rd_*`: `sd_mmc.c sd_wr_*`, a
  polled CMD25 that sends one block a call and never waits on the card's busy; each call selects the card and lets it
  go, so the screen keeps the bus between blocks and while the card programs. Deselecting during a multi-block write
  keeps it open by the SD spec; **an FM-1 has not shown it yet** (the bench's first check once the board exists).
  The recorder (`storage/sd/sd_rec.c`):
  - **Audio into the card.** The audio interrupt puts each block's frames into a ring of SD_REC KB (32 KB covers
    186 ms of stalls; `core/audio.c` after the mix). The main loop's passes and its idle wait send whole blocks.
  - **A full ring.** A frame it cannot hold is dropped and counted, never noise.
  - **The file.** One contiguous run (`sda_create`, SDA_RECORD: the length asked for, else a minute). A take that
    reaches the run's end stops there (FULL).
  - **The header.** It is block 0, written first with no length and again at the end with the take's. A JUNK chunk
    pads it to 512 B, so the data starts at byte 512.
  - **The card is held.** While a take writes, the card takes no other command: FatFs' reads and writes are refused
    (`sd_diskio.c`: RES_NOTRDY), and the sample streams wait.
  - **Starting a take.** REC tapped, then pressed again at once and held (the user, 2026-10-26: "short plus long
    click on rec"; op_input.c `crec_*`; docs/UI-OPTIMIST-DESIGN.md section 6): `Optimist/Record/rec-nnnn.wav` (shown REC-0001.WAV), the
    first free name; REC flashes while it records. The console: `sd rec [ms]`, `sd rec stop`, `sd rec` (the state,
    blocks, frames lost, the ring's most blocks waiting).
  Tests: `tests/sd_wr_test.c` (the writer: 64 blocks by bytes and by DMA, SDHC and SDSC, the screen's bytes between
  them, a 150 ms stall, a 600 ms one, a power cut), `tests/sd_rec_test.c` (2 s checked frame by frame and the header,
  a 150 ms stall covered, 300 ms dropping frames, a take filling its run, FatFs refused while it writes). Cost on
  sd-card-mots: SD_REC 32, about 1.2 KB of flash and the ring's 32 KB of the pool.
- **Card formats safe over a power cut**: a project or backup written in place destroys the old copy before the new
  one is whole (no journal, fixed-size files). The card's formats get A/B slots with a generation number and a CRC,
  as the settings in flash already do.
- **Full FatFs once the flash allows** (LAYOUT gives the room: 15-20 KB): files created and grown, long names, several
  files open, a sector cache, fast seek with a cluster-link map (a fragmented sample does not stall at every cluster).
  `diskio`'s layer stays, so `sd_mmc.c` is reused; Petit FatFs' limits must not shape the user's workflow.
- **What a card stall touches** (checked, 2026-10-25 review): USB is serviced from the TIMER5 ISR, the sequencer from
  the audio ISR, the key scan from TIMER5; a card stall in the main loop freezes only the UI, the screen and the
  autosave, never audio, notes, MIDI or USB. The codec's port C lines are written at audio init only (no ISR does a
  read-modify-write on the port the SD code toggles).
- **The monitor (2026-11-05, host-tested only)**: SYSTEM > SD STREAM, after SD CARD's two pages, four lines: READ (KB/s of
  the last half second, the bar against the kept clock's carry: kHz x 0.098, 840 KB/s at 8.57 MHz; the tick the 10 s
  peak), STREAMS (sounding now / the most in 10 s), BUFFER (the least ring fill of the streams the card feeds, 10 s;
  amber under 25 %, red at 0), DRY (streams that ran dry, red for 10 s after one; GRAIN's / STRETCH's late and missed beside
  it; NO RING, CRC FAILS and STEPS DOWN take turns on the line once not 0, the knob steps them and holds 30 s). A knob of
  another line, YES and SAVE clear the counts. `storage/sd/sd_mon.c` samples in the main loop (`sst_go`), the audio only
  counts as before; the console's `sd stream` prints the rate, its peak, the most streams and the least fill.

## 8. The card's layout (a proposal, 2026-10-25; the user: "fully for a fixed folder structure")

### What the M8 does (Dirtywave M8 operation manual v6.0.0, 2025-06-21: read, not tried)

- **Folders**: `/Songs` (the browser opens there; `/Songs/Demos`), `/Samples` (subfolders allowed), instrument presets
  (`.m8i`), `/Bundles`, `/Renders`, `/Scales` (`/Scales/Factory`), `/System` (the STARTUP song, `Metronome.m8i`), themes.
- **A song file holds no samples**, only their paths: moving or deleting a sample breaks the song ("failed to load").
  So the M8 has **BUNDLE**: a sub-folder in `/Bundles` with the project file, its samples and its instruments (to share
  or archive a song before moving samples). Paths up to 127 characters.
- **Saving**: LOAD / SAVE / NEW and a NAME; the manual describes no autosave, no versions, no backup copies.
- **Samples are streamed from the card, never held in RAM** ("no memory / length limitation"). The cost: the card's
  **random-read speed** is critical (most cards are tuned for one sequential file; a tested-card list), the card must be
  formatted with the SD Association's formatter (aligned clusters), and a card overworked stops playback with "CPU TOO
  BUSY". Their advice: mono where stereo is not needed, 16 or 8 bits, avoid playing a sample an octave or more above its
  recorded pitch (each octave doubles the data a second); notes above C-4 are limited by bit depth and channels.
- **Recording / rendering**: samples recorded in the sample editor (SAVE / OVERWRITE with a NAME), song selections
  rendered to a sample (a new instrument), renders as 16-bit 44.1 kHz stereo WAV in `/Renders`; loop and up to 128
  slice markers stored inside the WAV file.

### The user's rulings (2026-10-25)

- **A real filesystem with names** (not slot-numbered files): "we have a file system! we use names". The folders are
  fixed, the files inside them are named freely (long names), created and grown by the FM-1. That means **full FatFs**
  (long names, create / grow / rename, several files, a cluster-link map for fast seek): about 15-20 KB of flash
  (estimate), affordable with LAYOUT; `diskio` stays, so `sd_mmc.c` is reused. Petit FatFs stays only for the bring-up.
- **Names mapped onto the 16 keys**: a list of files is shown as **pages of 16 slots** (the white keys), the page
  picked by the knob; the same list can be **sorted / filtered by recent, name, folder** (and more views later).
- **Versions per project, N configurable** (a setting: how many earlier saves each project keeps).
- **Samples identified by a hash of the file** (the user: "Elektron has a nice approach"): a project stores each
  sample's content hash (and its last known name and folder as a hint), never only a path. A sample renamed or moved
  inside the card is still found by its hash; one missing is named MISSING with its last name, never silently swapped.

### The layout

The user's rulings (2026-11-04): folders capitalised, files lowercase; presets and projects in banks of 16. The card is
the SD card zip unzipped at its root (section 9); there is no layout of before to read ("there are no old cards").

```
/Optimist/                    the FM-1's folder (anything else on the card is left alone)
  Projects/<L>/               a bank of 16 projects: A, B, ... Z
    <nn> - <name>/            a project's folder: its slot nn (01 .. 16) and its name, lowercase
      <nn> - <name>.opp       the project (written to a new file, then renamed over the old: a power cut leaves the
                              old one whole; a CRC and a save counter inside)
      Versions/<nn> - <name>-0007.opp   the earlier saves, newest N kept (N: the setting), numbered by the save counter
  Presets/Factory/<Engine>/<L>/<nn> - <name>.ops   every factory preset (the zip's): 16 a bank in the engine's table
                              order (preset 18 is B/03)
  Presets/User/<Engine>/<L>/<nn> - <name>.ops      the presets saved on the FM-1, in their engine's banks
  Samples/                    samples, in any sub-folders the user makes; WAV (16-bit mono by default, the M8's
                              speed advice); loop points and slice markers inside the WAV (as the M8 does)
    <Set>/z0.wav ..           the ticked sample sets (Piano, Bass, Vibes, Horns, Strgs, Flute, Scrch), each zone
                              from its source at the source's rate (Piano, Flute 44.1 kHz; the others 22.05 kHz)
    Kits/Acoustic/<nn> - <name>.wav   the sampled kits' 16 sounds (01 - kick .. 16 - wood block), 44.1 kHz
    X0X/909/hh.wav crash.wav ride.wav  the X0X 909's hats and cymbals, 44.1 kHz
    Hiphop/<Sound>/<file>.wav  the hip-hop pack (Bass, Horns, Scratch, Strings, Upright, Vibes), 22.05 kHz
  Kits/Factory/<L>/<nn> - <name> (<instruments>).opk   every factory kit (the zip's): 16 a bank in the kit list's
                              order, its drum instruments named (909, 808, synth, kawa, plaits, sampled)
  Kits/User/<L>/<nn> - <name> (<instruments>).opk      the kits saved on the FM-1, in banks of 16 as the presets
  Record/rec-0001.wav         recordings (a new file each, the first free number)
  Renders/                    song / stem renders
  Backup/settings.opd         the settings backup (SYSTEM > SETTINGS); whole-device backups later
  System/index.bin            the index: a sample's path, size, date -> hash (rebuilt for files whose size or date
                              changed: hashing every sample at each mount would be slow); a preset's or kit's -> its id
```

- **Names.** A name typed on the FM-1 (upper case, the NAME page) is written lowercase (`sd_layout.c sdl_lower`); a
  name read from the card is shown upper case (`sdl_upper`; the screen's small font has lower case glyphs, so this is
  done, not left to the font). The firmware's own compares are without case (ASCII: `sdl_cmp`, `sdl_ncmp`,
  `sdl_ends`), as FatFs looks names up (FF_USE_LFN): a typed BASS 1 finds `01 - bass 1.ops`. The console's `sd`
  commands take the names as typed.
- **The engines' folders** (`storage/sd/sd_names.h`, one table: `tools/sd_factory.c` builds the zip from it): the
  registry's names capitalised (Acid, Analog, Digital, Drawbar, Drone, Formant, Grain, Juno, Lofi, Noise, Phase, Phys,
  Plaits, Prophet, Sample, Slice, Stretch, Strings, Super, Terrain, Trio, Vector, Wave), a digit or a known acronym kept
  upper case (FM6, CS80, CZ, SID, VPS, WSEQ). An engine without its `SDN_<NAME>` does not compile;
  `tests/sd_zip_test.py` checks the rule and the zip's folders against the table.
- **Banks** (`sd_browse.c SDB_BANK`, `sd_mode.c`): a bank's folder is shown as its 16 numbered slots, slot nn - 1 the
  file numbered nn (`"nn - "`), a free number an empty slot; a file without a number is not in a bank. The PROJECTS
  list opens on the bank last shown (A at first), the PRESETS list on the selected part's engine's User bank; HOME goes
  up to the banks' folders (and on to Presets/ with Factory, User and any DX7 / CZ files there). **The black keys pick
  the banks**: the FM-1's 27 keys run chromatically from F (`fx/punch/punch.c punch_key`: the white keys are the 16
  slots), so 11 are black (F# G# A# C# D# twice, the top F#); the SD lists used none of them (`sdc_key` dropped a
  black key), and now black key b is bank A + b, A .. K, in key order (`op_prjflow.c prj_key`, `op_sdcard.c
  sdc_bank_key`; the bank's black key lit). A bank is one page, so OCT- / OCT+ and the BANK knob (the first card) step
  one bank either way, to Z. The bank said ("BANK C").
- **A save** (`sd_mode.c sdm_save_place`; the user, 2026-11-05, on the FM-1: "it always saves on slot 1, whatever the
  white note button I click or the card item says"): a SAVE asked on slot N of a bank shown, by a white key or the ITEM,
  is `NN - name`, whatever the bank holds under that name (a copy under another number: a project's NAME is prefilled
  with the work's own). A slot in use comes past its question (*SAVE <it>?*, red): the same name a new version (a
  project) or the file again; another name replaces it, the old file or project with its versions gone once the new
  one is whole. A preset goes into its engine's User folder, the bank's letter the one shown, a kit into Kits/User: a
  Factory bank's slot is that number in User when free. No slot asked (a project's *SAVE <work>?* before a LOAD): back
  to its own place (`sdm_work`, "B/03 - p18"), else the bank's file of that name (a preset or kit: NAME TAKEN unless it
  is the one asked over), else the bank's lowest free number; none: BANK FULL (NAME stays).

### Every sample on the card, the drums from it, ids (2026-11-04)

The user's rulings: "the xox samples etc should also be on the sd card, in highest resolution we have", "the drum kits
samples as well", "Highest for all now"; a kit's file names its instruments; "like samples, kits and presets should
have a unique id so that if they are moved a project finds them back", "the ids should be the same for the factory
presets for all devices".

- **The samples** (`tools/sd_samples.py`, no space limit; since 2026-11-04 the builder's ticks choose them: below,
  "The builder's ticks choose the card's samples"): each sample source of the repo at its own rate, **16-bit mono**. The streams read 16-bit and mix a stereo frame to mono anyway
  (`sd_stream.h sst_frame`), so a stereo or 24-bit source (FLUTE: 44.1 kHz 24-bit stereo; the CC0 kit's tambourine,
  shaker, conga, claves, wood block: stereo; the 909's hat: 24-bit) becomes 16-bit mono: half the bus and the card's RAM
  for the same sound, nothing the FM-1 could play lost. A set's zone is the flash zone's processing (`gen_samples.py
  cc0_entries card=True`: its onset trim, PIANO's steady loop, FLUTE's crossfaded sustain loop, the one-shots' end
  fade, the level), never ADPCM-coded, resampled or cut short: its length the source's from its onset. A kit sound is
  the flash kit's processing at the source's rate and to the source's end (the kick's and toms' damped body, the
  balance, a 30 ms end fade). The 909's three and the hip-hop pack are the sources' own samples, their roots and keys
  added (smpl, inst).
- **The streams at 44.1 kHz** (`tests/sd_stream_bus_test.c`, the card model with the screen's strips, SD_RING 4: a
  4 KB ring is 46 ms of 44.1 kHz 16-bit mono, a 50 ms head 4,412 B): a voice reads 88.2 KB/s at its pitch, twice an
  octave up. Voices with none dry: 8 at 1.9 MB/s (20 MHz by DMA), 7 at 937 KB/s, 6 at 733 KB/s (the measured 7.5 MHz).
  Over the bus a voice whose ring runs dry fades out over 1.5 ms and is counted (`sst_dry`, the console's `sd stream`),
  its stream given back: at 733 KB/s 8 voices at once lose 3 and the other 5 play to their end; the next notes play
  again (never garbage, never a hang). GRAIN on a 44.1 kHz set (PIANO, FLUTE) spreads its grains over twice the bytes,
  more than its 32 KB cache holds: some grains are skipped (counted late / missed, no click; `tests/sd_grain_test.c`).
- **The drums from the card** (SD card mode: the builder sets `FELUCCA_KITS_CARD`, and `FELUCCA_X909_CARD` with the
  909, `core.py flags`): the five sampled kits (ACOUSTIC, DEEP, TIGHT, BRIGHT, DUST, kit UIDs 0..4) are kits of every
  SD card mode build whatever KIT_* say, and play `Samples/Kits/Acoustic/` (`drums/kit_card.c`: a GM note -> its file
  and root, the flash kit's map; the kit's treatment and the lane's TUNE / DECAY / CUT / LEVEL as before) through the
  lanes' card streams (`lane_card.c dlc_go`); the 909's hats, crash and ride play `Samples/X0X/909/` through a stream
  each voice (`drums/x0x/x0x_card.c`, read from drum909.c by integers only). The plan (`sd_stream_plan.c`) places only
  the files the lanes play (`kc_want`, `x9c_want`), by path, their heads 100 ms (`SST_KIT_HEAD_MS`), PRE-BUFFER's if longer (halved down to 25 ms, `SST_KIT_PRE_MS`, when there is no room, after the rings), 6 rings (5 at the least) for
  the kit's hits and 2 for the 909's (of the 8). A file not on the card: that hit silent, "KIT SAMPLES MISSING" /
  "909 SAMPLES MISSING" once (`sd_mode.c sdm_kit_tick`). Synthesised kits stay synthesised. sd-card-mots: 803,812 B of
  flash (817,688 before: 13,876 B less, the 909's 6-bit hats out of the flash), SRAM +2,192 B (the 32 sources).
- **GRAIN and STRETCH on a card WAV** (the user, 2026-11-05; SRC's last value CARD, as SAMPLE's SET; the browser's USE popup
  lists all four samplers built, `sd_mode.c sdm_samplers`): the part's own WAV, saved with the project as SAMPLE's is (hash
  and hint, SEC_ID_SDSMP), SRC showing the file's name or MISSING (`op_cells.c`, `sdm_card_label`), read as a card set of
  one zone over every key (`sst_bank_free`). The plan (`sd_stream_plan.c sst_free_pick`): a file of 128 KB at most
  (`SST_WHOLE_MAX`) goes whole into the block when it fits (no bus at all, no skipped grain, a cache of 1 KB for the
  pins); any other through the part's cache (GRAIN 32 KB, STRETCH 16 KB, 32 KB for a stereo file: its window's bytes),
  whatever its length: up to 2^30 frames, and only as many of its sectors as the grains or the window read. Not
  playable, said once: "T1 FILE IN PIECES" (more than 31 runs), "T1 NOT 8/16-BIT PCM", "T1 FILE TOO LONG"; gone: "T1 SAMPLE
  MISSING"; no room for even the cache: MEM FULL CARD. A USE of a file over 256 KB the index has not hashed yet does not
  hash it there (seconds of a frozen main loop): the index's pass runs, "CARD INDEXING: USE AGAIN". Measured at 840 KB/s
  (8.57 MHz; `tests/sd_grain_test.c`, the screen's strips drawing, 44.1 kHz mono unless said; of about 130 grains in 4.5 s):
  GRAIN frozen 0 missed, 4 KB/s of the bus; SPRD 8 with short grains 0, 198 KB/s; SPRD 35 9 missed, 417 KB/s; SPRD 127 0,
  211 KB/s; SPRD 127 at DENS 127 half missed (173 of 420), 600 KB/s, over the bus; 22.05 kHz SPRD 35 16 missed, 274 KB/s;
  stereo SPRD 35 94 of 213 missed, 583 KB/s. A missed grain is skipped or fades (counted, the part's CARD SLOW said; a
  corner under -36 dBFS, no click). The length changes none of it: a grain costs its own bytes. STRETCH: 89 KB/s at 1x
  (176 stereo), a few KB/s slow, 0 missed but a first window.
- **A kit's file name** (`drum_kits.c ukit_card_instr`): `<nn> - <name> (<instruments>).opk`, the instruments its
  lanes name, in the order 909, 808, synth (the 32 synthesised kits), kawa, plaits, sampled (the sampled kits, a USR or
  CARD sample): the family of each lane's source by its UID, whatever the build has of it (a stand-in is not named);
  the factory kits' (`tools/sd_factory.c`) and a kit saved on the FM-1 the same way. The FM-1 shows the name only, the
  part before " (" (`sdm_strip`; a 5-letter tile has no room for more), and finds and loads a kit whatever the
  parentheses say (renamed on a computer too). Saved over its name with other instruments: the old file goes.
- **Ids** (`storage/sd/sd_ids.h`): every .ops / .opk file's header (version 2, `sd_mode.c sdm_fh`, 24 bytes) carries
  a 64-bit id. A **factory** id is XXH64 of "OPID", the kind, the engine's UID and the preset's name (its nth of that
  name in the engine's list when it repeats), or the kit's UID: the same on every machine and every zip
  (`tests/sd_zip_test.py` recomputes each by a second XXH64 and builds the zip again elsewhere); its bank slot, a
  device, a time, a path never enter it. What changes it: the preset's name or its engine's UID (a kit: its UID); its
  values retuned keep it. A **user** id (bit 63 set) is made at the first save: 31 bits of the FM-1's flash part's
  factory unique ID's hash (one a chip: two FM-1s never share it), the device record's 16-bit count, 16 bits of the
  milliseconds; saved over by its name it is kept, a save as another name gets a new one. The index (`sd_index.c`)
  holds every .ops under Presets/ and .opk under Kits/ with its id (`SDX_F_ID`), the same pass, the same kept-when-
  unchanged rule. A project keeps each part's preset and its kit by id and path (the SEC_ID_SDSMP record, after the
  samples): at its load the path first, then the id (`sdx_find_id`): found elsewhere, the path updated (on the card at
  the project's next save); a copied file: the first found, "SAME ID TWICE" said; gone: "P1 PRESET MISSING" /
  "KIT MISSING".
- **make sdcard** (`tools/sd_card.py`, `tools/sd_card_volumes.py`; the builder menu's `c` the same steps on its screen):
  prepares a mounted card from the zip of the configuration (`CONFIG=` / `PROFILE=`, else build/builder.config).
  No CARD: it lists the mounted removable FAT volumes (macOS diskutil, Linux lsblk or /proc/mounts, Windows drive
  letters) with size, free space and whether they have Optimist/, the last five used first (build/sdcard-last.json,
  a card known by its volume UUID under any mount name; Enter takes the last, q or 0 cancels; another folder can be
  typed), shows "card X: N files to add, M to replace, K factory files to remove; user content kept" and asks
  "Go? [y/N]" (`--yes` skips it; no terminal and no CARD: refused). CARD= / `--card` must be a mounted FAT volume's
  root, never /, the home or the repo. The factory trees (Presets/Factory, Kits/Factory, the factory sample folders,
  readme.txt) are mirrored; everything else is the user's and is never written or removed; the FM-1's folders made when
  missing (a factory folder only when the zip has a file in it); plain writes (a ._ twin of a written file removed),
  os.sync, "eject in Finder". The user runs it.

### How it works

- **The 16-key browser**: a folder's entries (or a view across folders: recent, by name) as pages of 16; a key picks
  an entry, the knob turns pages; recent = the files the projects and the FM-1 used last (kept in the index).
- **Hash**: computed once per file (the audio data, so a header edit or a rename keeps it) and **kept with the file's
  date and size** in the index; a project's load looks its samples up by hash, then by the hint if the hash is not
  indexed yet. Which hash: a fast 64-bit one is enough (not a security hash); to decide when built.
- **When to hash again** (the user, 2026-10-25): the FM-1 has no clock, other systems do. So:
  - a sample **the FM-1 changes** (records, crops, normalises, writes) is hashed again right away by the FM-1 itself,
    whatever date it writes (it knows it changed it);
  - a file whose **date or size differs from the index** was changed elsewhere (a computer, a phone): hashed again at
    the next index pass;
  - the date the FM-1 stamps on its own writes (FatFs `get_fattime`, no RTC) is only a placeholder; a later idea: the
    web / iOS app sets the FM-1's date when it connects, so its files carry real dates while it stays powered.
- **Speed** (the M8's lesson: random reads matter most): streamed files are best contiguous; the FM-1 grows files by
  whole preallocated runs (FatFs `f_expand`) when it records, the computer side copies whole files; the fast seek map
  (FatFs `CREATE_LINKMAP`) turns a fragmented file's cluster chain into runs once at open, so streaming never walks the
  FAT; mono 16-bit by default, the run length a parameter (section 7), a pitch ceiling per voice like the M8's.
- **No fragmentation: space wasted on purpose** (the user, 2026-10-25: "cards are cheap, most users will use 16 GB+
  cards they will never fill up; this can be a setting"). Every file the FM-1 writes is allocated as **one contiguous
  run, rounded up generously**, never grown cluster by cluster:
  - the slack **depends on the kind of file**, never one size for all:
    - presets, kits, the index, small files: **none**: a file within one cluster (32 KB on a 16-32 GB card) cannot
      fragment, so it is written as it is;
    - bigger files: the run sized by a **best guess of the file's final size** (the user: "we can take best
      guesses"), then rounded up to whole clusters, the setting scaling the guesses (less slack on a nearly full card):
      - a project: its size now x 1.5 (it grows as scenes and patterns are added; the next saves reuse the run);
      - a recording: the length asked for, else the song or pattern loop's length at the tempo (x 2 for a second
        take), else a minute; past the run, a new run of the same guess;
      - a sample the FM-1 edits or resamples: its new length exactly (it is known before writing);
      - a render: the song's length at the tempo;
  - each run allocated contiguous in one go (FatFs `f_expand`), the real length kept inside the file (WAV length, the
    project's header), the file never truncated: the slack is the point;
  - recordings start with a run of the step x a few (or the take's length limit) and continue into a new run when
    longer; a take never interleaves with another file;
  - project saves (temp + rename) get a run of the step, so the new file is contiguous too;
  - the card formatted with large clusters (the SD Association formatter's defaults: 32 KB on 16-32 GB SDHC, more
    on SDXC) so even files from a computer come in few runs; the fast-seek map covers the rest;
  - the FM-1 says how much room the card has left counting the slack, and the setting can be lowered for a nearly
    full card.
- **Safe saves, with the FM-1's own flash as the staging copy** (the user, 2026-10-25: "we have flash for temp /
  buffer"): the working project is already whole in the internal flash (the autosave, and the 1-project store of the
  fake SD mode), so a save to the card is:
  1. the project made whole in flash first (as the autosave does) and a "card save pending: <name>" record written
     in flash (the settings' log);
  2. the card file written from that flash copy (no RAM buffer the size of a project: it streams sector by sector) to
     a temporary name, read back and checked, renamed over the old one; the previous one moved into VERSIONS, the
     oldest past N dropped;
  3. the pending record cleared.
  A power cut anywhere: the card holds the old project whole (or the new one, past the rename), and at the next boot
  a pending record makes the FM-1 finish the save from the flash copy. The card never holds the only copy of the
  work in progress. (The flash is not a streaming buffer: a 4 KB erase stalls tens of ms; recordings buffer in RAM.)
- **Sharing**: a project names its samples by hash, so the web / iOS app exports the project with exactly those files
  and an import puts them back under SAMPLES (whatever their names).

### Built (2026-10-26, host-tested only; on the screen: section 9)

- **The hash: XXH64** (`sd_hash.c`): 64 bits, a published specification and vectors (the test checks five), 88 B of
  state; its speed does not matter (the card is the bottleneck). A WAV's hash is its `data` chunk's.
- **The folders**: the FM-1 makes `Optimist/`, its eight folders, `Presets/Factory` and `Presets/User` where missing
  (`sdl_prepare`, at a mount).
- **The index** (`sd_index.c`): 256-byte records, a pass that hashes only new and changed files, `sdx_note_written`
  for the FM-1's own writes, the recent view's use numbers; rewritten to `index.tmp` and renamed.
  **The pass runs in slices across main-loop passes** (2026-11-04: one pass in one frame froze the FM-1 for 10 s and the
  second core's starve check, 1.5 s, took it down to OVERLOAD): `sdx_pass_step` runs units (a directory entry, an old
  record, an `f_open`, a 512-byte chunk of a sample's hash, a record written) for 8 ms, at most every 16 ms
  (`SDM_IX_SLICE_MS`); the unit that ends a slice runs over, ~20 ms a pass in all. The card stays usable meanwhile: the
  lists and lookups read the old index. Whoever needs the whole index waits: a project's presets and kit are looked up
  once the pass ends (never a false MISSING), the streams' plan looks up again then, and `NOT ON CARD` is not said
  meanwhile. A file the FM-1 writes mid-pass drops the pass and starts it again; a use mid-pass is not kept in the
  recent view; a recording holds the pass (no other command while its CMD25 is open); a card pulled cancels it, and
  `index.bin` is only ever replaced by a whole pass. SYSTEM > SD CARD's SAMPLES line says how far (`INDEX 12 %`).
  The factory preset names follow, an engine a pass. A mount of an unchanged card walks nothing: the pass leaves FSINFO's
  free count and next-free hint in the index's header record (`sdx_sig_t`, outside its check, so older indexes and
  builds are unaffected), and a mount that finds them equal skips the pass. Any new chain written elsewhere moves them; so
  do the FM-1's own later writes (a walk at the next mount, in slices, the hashes kept). A file overwritten in place
  by a computer does not: SAMPLES INDEX forces the walk. FAT16 has no FSINFO and always walks. On the model
  (`tests/sd_index_pass_test.c`, the 0.3 zip's 449 files, 1.66 MB, 512-byte clusters) a first pass costs 8.0 s of
  card time at 840 KB/s, ~17 s in slices with the screen and audio sharing the bus; a walk with the hashes kept ~3 s
  of card time; the real card's 32 KB clusters read fewer FAT sectors. Not measured on an FM-1.
- **The 16-key pages** (`sd_browse.c`): views folder / by name / recent; the UI needs a page that draws `slot[0..n-1]`
  (name, folder mark, cut mark), the page number of `pages`, the view; the knob `sdb_turn(+-1)`, a key `sdb_path` (or
  `sdb_into` for a folder), a way up (`sdb_up`) and a view switch; every list (projects, samples, kits, presets) is a
  folder view of its own folder.
- **The save** (`sd_save.c`): as described above, with `sdp_keep` versions and the spare run; the FM-1 side still to
  wire: the hooks (the staged copy in flash, the pending record in the settings' log) and `sdp_boot` after the boot's
  mount. A cut rename (FatFs writes the new entry, then removes the old one) leaves one chain under two names: found by
  the cut tests, completed by `sdl_move` / `sdp_dedupe` with `f_unlink_entry` (the entry only).
- **The runs** (`sd_alloc.c`): by kind as above, `sda_scale` the setting; no run free: the bytes it needs, else refused.
  A recording's continuation past its run (a new run) waits for the recorder.

Open: the views beyond recent / name / folder.

## 9. SD card mode on the device (2026-10-26, host-tested only)

The user's ruling (2026-10-26): with SD card mode on, projects are saved and loaded **by name** on the card, and **every
list (projects, samples, presets, kits) is shown as banks of 16 on the white keys**. Built behind `FELUCCA_SD_UI`
(registry.h: `FELUCCA_SD_MODE && FELUCCA_SD_FATFS`, the mode's layout with the card under full FatFs; `FELUCCA_SD_MODE`
alone once the mode's one builder flag builds the card in). Every other build is byte-identical (the default profile
checked: only the build time and the commit differ).

**The device side** (`storage/sd/sd_mode.c`, included after the stores and the UI; main loop only):

- **Boot**: the boot's mount (main.c, before the LCD) as before; then `sdm_poll` (every main-loop pass): the device record
  read, `sdl_prepare`, a pending save finished (`sdp_boot`: CARD SAVE FINISHED), *SD card: samples* said, then the
  sample index's pass (`sdx_update`, one per mount; it holds the screen while it hashes new files, never the audio).
  No card: *NO SD CARD* once, the flash store as before. A card that fails in use: *SD CARD GONE*, the lists empty; a
  later mount (PROJECT entered, SYSTEM > SD card's YES) takes it again (no card: a few CMD0s).
- **The working project stays the one-project flash store's** (project 0). A card SAVE is the flash SAVE first
  (`prj_save_go`), the project's name (its first 12 characters: the flash project's), what waits in RAM flushed, then
  `sdp_save` from that copy: the staged bytes are project 0's records as one stream (the backup's PJ01 format: u8 id,
  u16 length, the record, for every id that is not device-wide; read in order by a cursor, no second copy). The save
  number is the card's copy's + 1. A card LOAD verifies the file (header and payload hash), clears project 0, writes the
  file's records into it (each id checked: in range, not device-wide, a record's length) and opens it; a load that does
  not fit (MEM FULL) clears it again, never half a project. A save still pending (its only copy the flash one) is
  finished before any LOAD, else the LOAD is refused (*CARD SAVE PENDING*).
- **The device record**: the log's id 22 (`SEC_ID_SDCARD`, device-wide as the names: `SLG_DEV`, `SLG_GLOBAL`): VERSIONS
  (`sdp_keep`, 0..16, default 8), SLACK (`sda_scale`, 0..400 % by 25, default 100), the pending save (`sdp_hooks`). A
  pending save whose flash copy is now another project's (NEW, the project's name changed) is dropped, never finished
  with other work; one still the project's is finished with the copy as it is (saved again without the card: that
  newer copy). The settings are written when SYSTEM is left and the audio is quiet.
- **Samples**: the project's id 23 (`SEC_ID_SDSMP`): each synth part's sample, its XXH64 (the index's), size and a name
  hint (its path under `Samples/`, cut at 51 bytes); a project file on the card carries it. For the player (streaming:
  another helper): `sdm_sample_of(part, &hash, &hint)` (1: a sample), `sdm_sample_path(part, out, cap)` (its path now,
  found by the hash: a renamed or moved file too; 0 MISSING), `sdm_smp_gen` bumped at each change. A file not indexed
  yet (or changed since the pass) is hashed when picked (`sdx_note_written`); the pick marks it used (RECENT). With
  streaming built (`FELUCCA_SD_STREAM`, section 7) a pick and every read of the record (a load, NEW, the boot) give each
  part's hash to the streams' plan (`sst_part_set`: the part's CARD sample).
- **Streaming's memory** (`FELUCCA_SD_STREAM`): the pre-buffer (`sst_pre_ms` 25 / 50 / 100 / 200, default 50) is the
  device record's too, shown with what the project's heads take (`sst_pre_kb`). The plan refused (`sstp` SSTP_REFUSED):
  *MEM FULL CARD*, then one drop at a time with the one confirm, red (*Drop T2 FM6?*: a part of `sstp.drop`, whose
  memory alone makes room, never one asked, as model.c `mem_offer`; YES: the part a silent placeholder, the plan places
  by itself once its memory is back); none left: *MEM FULL CARD: 25 MS FITS* (`sstp.fit_ms`, the longest pre-buffer that
  fits), or *MEM FULL CARD*. Said once a refusal.
- **Presets and kits**: one small file each, `Presets/User/<Engine>/<L>/<nn> - <name>.ops` ("OPPR": a user preset's record, `upreset.c up_card_*`
  through slot `UP_CARD` = `UP_SLOTS`, RAM only) and `Kits/User/<L>/<nn> - <name>.opk` ("OPKT": a user kit and its lanes' sends,
  `drum_kits.c ukit_card_*`), a 16-byte header (magic, version, length, the payload's XXH64), a sector at most
  (written whole), allocated exact (`SDA_EXACT`). A preset of an FM6 sound keeps its own voice too, the operator edits
  and all (`fm6_pack`'s 128 B after the record, as the flash slots' `UP_FM6`; a file of before loads its VOICE). Saved into the folder's top; loaded from any
  sub-folder.
- **DX7 banks** (the FM6 collections; the user, 2026-10-26): a `.SYX` of 32 voices (4104 B, any case of the extension)
  anywhere under `Presets/` is a folder of its voices in the PRESETS list, 16 a page (OCT for the second); a voice's
  LOAD reads its 128 B from the file and puts it into the selected part's FM6 voice (`sd_mode.c sdm_dx7_*`,
  `fm6_unpack`; the part on FM6 first when it plays another engine; refused like any engine change when memory is
  short); HOME leaves the bank. Only the page's 16 names are in RAM; no received bank (FM6_SYSEX) is needed. A
  voice kept with its edits goes into a preset (`.ops`, above). CZ tones (built 2026-10-26): a Casio CZ single-tone
  `.SYX` (264 B: F0 44 00 00 7n 20 pp, 256 nibbles low first, F7) under PRESETS, its key pressed: its 128 synthesis
  bytes into the selected part's tone (`sdm_cz_load`; the part on CZ first), as a single-tone dump: kept until the part's
  TONE or engine changes (a project keeps the TONE number).

**The sample sets on the card** (the user, 2026-10-29: "in SD card mode, ticking a sample set must NOT grow the flash";
built 2026-10-29, host-tested only). SD card mode (the builder's `core.card_sets`: LAYOUT 1 with SDCARD) builds no set
into the image, ticked or not (`gen_samples.py`, FELUCCA_SAMPLES_CARD=1: each of PIANO BASS VIBES HORNS STRGS FLUTE
SCRCH an empty set, its number and its presets kept, its bit in `SMP_CARD_SETS`; PERC stays with the sampled kits); the
estimate counts the sets 0 B there (`core.py image_cfg`) and the menu says "on the card" for a ticked set, "not on the
card" for the others. The SD card zip carries the ticked sets (2026-11-04: `core.py card_content`):
`Optimist/Samples/<Set>/z0.wav ..` (`gen_samples.py --card`, the folder the set's name capitalised), one file a zone in key order, the very
ADPCM the flash would hold decoded to 16-bit mono PCM at 22.05 kHz, its root and loop in "smpl" (the flash loop restarts
from the state stored at ls, so the card's loop is ls - 1 .. le - 1), its keys in "inst" (`sst_wav` reads them). SAMPLE
on a card set (`eng_sample.c smp_bank_on`): the zone whose keys hold the note, streamed as a CARD sample is (its head
the pre-buffer, a ring a voice); the plan (`sd_stream_plan.c`, `SST_PART_BANK`) finds the zones by path (no index
needed; the parts on one set share them) and counts the part's rings as a CARD part's. A set not on the card: the part
silent, "PIANO NOT ON CARD" said once (`sd_mode.c sdm_bank_tick`), never another set; MISSING after a load is not said
for it (`miss.c`). A card set plays two octaves above a zone's root at most (a 22.05 kHz file reads as a 44.1 kHz one an
octave up), the flash's three: within that, `tests/sd_sets_test.c` renders every set's factory presets from the card
bit-identical to the flash's. GRAIN and STRETCH play a card set too (the user, 2026-10-29), through a cache read in
any order (section 7, "Random access"); a load no longer says MISSING for them.

**Factory presets on the card; the SD card zip** (the user, 2026-10-26: "all presets (user or factory) go on the card",
"card only in SD mode"): SD card mode carries no factory preset tables (core.h `PRESETS_CARD`, `PRESETS_PTR`: each
engine's table named only inside sizeof, its count kept; BANK, the level trims and ANALOG 2's extras left out with them:
8,740 B of flash on sd-card-mots, 4,160 B of pool for the names). The presets are files,
`Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops` (16 a bank, the table's order), made by `tools/sd_factory.c` from the firmware's own sources
(each preset applied as the device applies it and taken as the card's preset record; `tests/sd_factory_test.c` loads
every one back and compares every value) and zipped with the card's folders by `tools/sd_zip.py`;
every build with SDCARD (`make build`, the builder menu's Build, `optimist.py build` and `package`, the cloud build)
writes `optimist-sd-card.zip` beside its package (`tools/builder/configure.py sd_card_zip`: build/, and `package`'s
`--out`; the build-OK panel and the CLI say where; the cloud build attaches `<stem>-sd-card.zip` and links it); `make
sd-zip` makes it alone (build/optimist-sd-card.zip; `PROFILE=` or `CONFIG=`, else the builder menu's
build/builder.config), and the builder menu's `c` makes it for the configuration shown. It holds every factory preset
of every engine and every factory kit, and the samples and FM6 packs the configuration ticks (below). Cached by its
inputs' SHA-256 and the ticks (the build directory's sd-zip-cache/: the firmware's sources, the host headers, the
generators, the samples; the last six kept): about 4 s made, 0.05 s from the cache (measured on a Mac, 2026-11-04).
Unzipped at the card's root; its `readme.txt` says what is there.
- **The builder's ticks choose the card's samples and FM6's packs** (the user, 2026-11-04: "In SD card mode, the samples
  we select in the builder just go on the SD card and not in flash, and that's it"; "same for preset (fm6)"). In SD card
  mode (`core.py card_content`, the zip's `sd_zip.Content`): a ticked sample set (`SET_*`) is on the card, an unticked
  one is not; the sampled kits' 16 sounds when `DRUM_SAMPLED` and a `KIT_*` are ticked (the five share them); the 909's
  `hh.wav` with the 909 and `crash.wav`, `ride.wav` with `X909_CYM` on; the drum samples only with the card's streams
  (`card_drums`: else they are in the flash, not on the card); a ticked FM6 pack (`FM6_PK_*`) is FM6 factory presets on
  the card (its 16 voices numbered on after FM6's own, each .ops carrying its voice) and is never compiled into the
  flash in SD card mode (`flags()`: every `FELUCCA_FM6_PK_*` 0; `image_cfg`: 0 B), an unticked pack is on neither. None
  of them is in the flash (0 B; the menu: "on the card" / "not on the card"). Every factory preset (FM6: its own and the
  ticked packs') and every factory kit is in whatever the ticks say: a kit whose samples are not on the card says KIT
  SAMPLES MISSING / 909 SAMPLES MISSING when loaded (`sd_mode.c sdm_kit_tick`), a set's preset NOT ON CARD. The hip-hop
  pack (no builder item: the USR slots') is in every zip. `make sdcard` mirrors the factory folders, so a set unticked
  since the card was last prepared goes from it (its folder too), the user's own files stay. The release build
  (fm1-base, `fm1base/optimist/core/opt_core.c`) keeps the engines' preset tables in the flash in SD card mode
  (`PRESETS_CARD 0`, its choice: presets without a card); mots-sd-config with them out (PRESETS_CARD 1, a measurement):
  5,444 B less flash, 2,256 B more RAM and 5,824 B more pool (2026-11-04).
- **Every sound flash mode offers as a preset is a factory preset file** (the user, 2026-11-04: "ALL", "they should just
  be presets in sd card"): after an engine's own presets, numbered on in the order flash mode lists them, the sounds of
  its collections: FM6's five packs (KEYS, BASS, BELLS, PADS, 80S: 80 voices, `eng_fm6.c FM6_PACK`), each a fresh
  track's sound with the voice (as `nbank.c nb_load` sets one), so FM6 has 83 (`FM6/A/04 - stage ep.ops` .. `F/03`). A
  file carries the voice's own bytes (`upreset.c up_card_take`: the record, then `fm6_pack`'s 128 B), so a build
  without the packs plays it the same (`tests/sd_factory_test.c` with the zip's files, no pack built). On the card
  there is no pack: SD card mode lists no pack voices in FM6's collection (`nbank.c`: the R bank received over MIDI
  only). Not on the card: the CZ collection (U01..: the user's own flash store), the R bank (received, RAM), the
  drum kits are files of their own (below). 368 presets in all (2026-11-04).
- **Factory kits on the card** (the user, 2026-11-04: "the factory kits go on the card like presets"):
  `Optimist/Kits/Factory/<L>/<nn> - <name>.opk`, every kit UID of drums.c in the kit list's order (the 5 sampled, the
  32 synthesised, X0X's 909 and 808 and their 8 styles, KAWA, PLAITS: 49, A/01 acoustic .. D/01 plaits), each the kit
  picked on fresh lanes taken as a user kit is (`drum_kits.c ukit_card_take`: the lanes, each lane's source by
  reference to the built-in drum sounds, the sends). The KITS list is in banks as the presets' (Kits/User/<L> for
  the saves, Kits/Factory beside it; the black keys and OCT the same). A kit file names its sounds, it does not hold
  them: the drum sources are the build's (the builder's DRUM_SYNTH, DS_KAWA, DS_PLAITS, DRUM_X0X909 / 808, X909_CYM,
  the sampled KIT_* items). **Not in every SD build**: sd-card-mots has no sampled kits (KIT_* 0: ACOUSTIC .. DUST)
  and no 909 cymbals (X909_CYM 0); a default profile has no X0X kits. A kit the build lacks plays its stand-in
  (`drums.c drum_kit_of`), as a project naming it does, and MISSING is said (`miss.c`). Tested:
  `tests/sd_factory_test.c --kits` with every drum source built and with sd-card-mots' drums: one file a kit UID,
  named as the kit list does, each loaded from its file plays as the flash's kit, every lane's hit sample for sample
  (each way rendered in a child of one process: X0X's and KAWA's filters keep state between hits).
- At a mount the names are read once (`sd_mode.c sdm_factory_scan`): every built engine's banks, A, B .. up to the
  first not there, its count the last numbered file's place + 1 (core.h `ENG_NPRE`, the table's count until a card is
  read), then the names; the PRESETS lists (made again), the console and the web editor show them; a missing file "?".
- An engine picked, a preset chosen, a fresh track: the engine's defaults at once (the interrupts may be off there), the
  preset's file on the main loop's next pass (`sdm_factory_ask` / `sdm_factory_poll`: loaded as a user preset loads, the
  preset's place kept). A project's load or a user preset over it drops the ask. No card: the defaults (*NO SD CARD:
  PRESETS INIT*, once).
- The PRESETS knob's whole list goes engine by engine in the tables' order (no BANK: the by-kind order of the flash
  builds is not kept); its label is the engine's name.
- Tested: `tests/ui_optimist_test.c` built with `PRESETS_CARD=1` (`sdu_factory_tests`): the names from the card, a
  preset read on the next pass, an ask dropped, a missing file and no card said.
- **No favorites in SD card mode** (the user, 2026-11-04: the card's banks are the organisation): registry.h turns
  FAVORITES off with `FELUCCA_SD_UI` (no FAVORITES filter in the preset list, no marks); the builder's FAVORITES item
  is as it was in flash mode (`core.py built`: not counted in SD card mode).

**The UI** (`ui/optimist/op_sdcard.c`; docs/UI-OPTIMIST-DESIGN.md 4.14): PROJECT's rows PROJECT (the card's projects
while a card is ready, else the flash slot with *No SD card: in flash*), PRESETS, KITS, SAMPLES; four cards (BANK,
ITEM, ACTION, VIEW), two big ones at a time, the pair of the knob last turned, over the page: two columns of eight names, as the white keys sit. A page is read in slices of the main loop's, *LOADING* past 200 ms (the 1.5 s `DUAL_STARVE_MS` rule: one pass in a frame is the second core down on a big card). ALGORITHM on PRESETS and SAMPLES, the PRESETS
knob's engine filter (ALL: every engine's banks in a row), a sample's sampler popup: UI-OPTIMIST-DESIGN.md 4.14. SYSTEM > SD card: CARD (READY and
its size; YES mounts again), VERSIONS, SLACK, SAMPLES INDEX. SYSTEM > SETTINGS: BACKUP writes the device's settings record (`persist_t`, as
`settings_save` makes it) to `Optimist/Backup/settings.opd` ("OPST"); RESTORE (asked, red) writes it back into flash and
restarts the FM-1 a second later (the settings are read at power-on). This unit's Bluetooth address and its copy of the
radio's calibration always stay; its pairings too when the file comes from another FM-1 (another address).

**Cost** (local measurement builds, not gpufarm). `sd-card-mots` is the SD card mode's build now (the user, 2026-10-26:
"we are in dev mode: drop whatever you want, but keep the code slim"): SDCARD, ARENA 96, SAMPLE, SD_RING 4, and to fit,
`PARAM_HELP` off (the help line: 17,064 B) and `ENG_PROPHET` off (10,772 B; both back on: 789,056 B, 23,360 B over):
flash 760,880 of 765,696 B (4.8 KB left), RAM 98,532 B, pool 284,632 B, RAM code 61,904 B. Before streaming (the same
profile with SDCARD only, PARAM_HELP off): 757,892 B against 726,400 B without the card: the card, FatFs, the layout and
this side +31.5 KB of flash, +968 B of RAM, +6.6 KB of pool; this side (the wiring and the UI) about 11 KB of it
(*estimate*: against the modules-reached measurement of section 5).

**Tests**: `tests/ui_optimist_sdcard.h` (`UI_SDCARD`, run on the FAT32 image with the card model): the boot's folders
and index; PROJECT's list driven by keys and knobs (SAVE by name, a version kept, SAVES listed and loaded, SAVE
*work*? before a LOAD and HOME to the LOAD, DELETE, 20 projects on two pages: the knob, OCT, ITEM across the pages);
PRESETS and KITS by name (a name of another file refused, a / refused); SAMPLES (folders first, into and up, USE by hash,
found again after a move, RECENT); SYSTEM > SD card; a full card (the save pending in flash, finished at the next
mount); the card pulled; no card at the boot; a power cut at every block write of a save made from the UI (the next boot
finishes it: the card's project the staged one, the record cleared); a pending save of another project dropped; with
streaming built (`ui_optimist_sdstream_test`): the pick given to the plan, PRE-BUFFER and its KB, MEM FULL CARD's drop
and the pre-buffer that fits. Not run on an FM-1; the emulator stops at the
boot's mount in SD builds.

Open: a card LOAD while playing says STOP FIRST (the flash slots stop, load and play again); projects with names the
NAME page cannot type (lower case from a computer, past 12 characters) load, and save again as their first 12
characters; the FM6 voice of a card preset; a save into a sub-folder.

## 10. Flashing from the card (2026-11-04, host-tested only, never run on an FM-1)

m-bootloader's SD item (jvitkauskas/m-bootloader PR #7, not merged: its docs/SPEC.md 11.5) loads `/MBOOT/MENU.BIN` from a
FAT32 card and calls it. Ours is `firmware/stage2/` (`make stage2`, in `build/stage2/`): it lists the packages in
`/FIRMWARES` and writes the one picked.

- The card: `/MBOOT/MENU.BIN`, and `.fwsc` packages in `/FIRMWARES` (found by its short name, `FIRMWA~1`: nine letters
  are past Petit FatFs' 8.3). The list shows the long names without `.fwsc`, cut at
  20 characters: a hook in Petit FatFs' directory scan (`PF_DIR_HOOK`, stage 2's build only) passes the long-name entries
  to `s2_lfn_feed`; files open by their 8.3 name. FAT32 only: m-bootloader reads no other.
- m-bootloader stays in RAM and BACK returns to it, but its code has no fixed addresses, so `MENU.BIN` compiles in the
  same `core.c`, `platform.c`, `display.c`, `board.c` from `third_party/m-bootloader`, with Optimist's SD driver and
  Petit FatFs at the driver's floor, 6 MHz.
- Before anything is erased: the package's format, model (`FM-1_`), whole `flash.bin` CRC, chip key, and the slot.
- Written: `[0x4000, flash.bin's end)`, the sectors that differ, each read back, the first app sector last. Never the boot
  area, never the store above the slot. HOLD SELECT 2 s confirms.
- A package for another layout is refused (`LAYOUT CHANGE: USE USB`): a switch needs the clears `tools/fm1_layouts.py`
  plans. With no firmware that mounts there is nothing to compare, so it writes, and that is the way back from a write
  that broke off: the boot area is intact, so m-bootloader's menu and the card still work.
- Open on hardware: everything. TIMER4 is started here as the firmware does, but has never run under m-bootloader: if it
  does not count there, the card's timeouts do not end.

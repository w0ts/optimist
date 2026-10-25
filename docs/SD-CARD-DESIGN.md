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
- the card's DAT0 (DO) drives PC8 through 1 kOhm while the LCD is deselected;
- the card's CS is the LCD's CS **inverted**: the card is selected exactly when the LCD is not.

Six wires, one inverter, two resistors, no pin of the main chip touched.

## 2. Parts

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
   checks exactly that (`LCD sees only 0xFF`).
4. **`sd_end`**: the card selected and idle (the LCD deselected, as `lcd.c` leaves it), PC8 driven again, SPI1's
   control word and the LCD's clock (`LCD_BAUD`) put back.

The card's start-up runs at 400 kHz (SPI1 BAUD 149: 60 MHz / 150), then 20 MHz (BAUD 2); the screen runs at 30 MHz
(BAUD 1). PC8 can be driven by the FM-1 and the card at once in two short moments (the LCD's D/C set just before its CS
goes low; a card that has not let go yet): the 1 kOhm keeps that to 3.3 mA.

**Which way SPI1 reads is unverified.** The flash driver reads SPI0 with CON bit 12 (DIR = receive) after writing
0xFF (`hal/fm1_flash.h` `fl_rx`); whether SPI1's group-B data-in follows PC8 in transmit mode as well is not known.
`hal/fm1_sd_bus.h` has three ways, picked at run time with the console's `sd mode`:

| Mode | How a byte is read |
|---|---|
| `duplex` (default) | sent in transmit mode, the shifted-in byte read back from BUF |
| `dirrx` | as `fl_rx`: CON b3 + b12 set, 0xFF written, BUF read |
| `bitbang` | PC9 / PC10 / PC8 as plain GPIO (SPI1 off its pins for the session: IOMAP_CON1 b4 cleared, *unverified*) |

In `dirrx` mode whether the data-out line stays high (the card needs 0xFF on CMD while it answers) is also
unverified; a logic analyser on CLK / DO / PC8 settles both questions at once.

## 5. Firmware

| File | What |
|---|---|
| `hal/fm1_sd_bus.h` | the bus: select / deselect through the LCD's CS, byte in / out (three modes), clocks |
| `src/storage/sd/sd_mmc.c` | ChaN's SPI-mode card driver (Petit FatFs `generic/mmcbbp.c`): MMCv3, SDv1, SDv2 byte or block addressed; the waits timed on TIMER4; the CSD read for the capacity |
| `src/storage/sd/pff.c`, `pff.h`, `pffconf.h`, `diskio.h` | Petit FatFs R0.03a (ChaN, 1-clause BSD: `LICENSES/BSD-1-Clause-FatFs.txt`), its own compile unit (`tools/build.py`); configured: read, write, seek, directories, FAT16 + FAT32 |
| `src/storage/sd/sdcard.c` | the API the rest calls (`sd_mount`, `sd_open`, `sd_read`, `sd_write`, `sd_seek`, `sd_opendir`, `sd_readdir`), each call taking and giving back the bus |
| `src/io/console.c` | the `sd` commands (console builds only) |
| `tests/sd_test.c` | the host test: a byte-level card model on FAT16 / FAT32 images from a real formatter (`tests/fixtures/sd/make_images.sh`) |

**Filesystem limits (Petit FatFs):** FAT16 and FAT32 only (exFAT would need full FatFs with long names, roughly 15-20 KB
of flash, *estimate*); 8.3 names (a long name shows as its alias, `LONGFI~1.TXT`); one open file at a time; files are
never created or grown, a write stays inside the file's size and starts on a sector boundary. The way to use it:
a computer makes fixed-size files (`BACKUP01.BIN`, `REC01.WAV`, ...), the FM-1 fills them.

**Cost:** 5,032 B of flash, 20 B of RAM and 556 B of pool with the console (measured 2026-10-25 on the default
profile with `USB_CONSOLE` instead of `USB_AUDIO`; the FATFS state and the console's sector buffer are in the pool).
That build is 2,316 B over the app slot: a console + SD test build has to leave something out (the user's call).

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
   - `sd bench BIG.BIN` (a file of a few MB made on the computer): the read speed.
   - `sd write DATA.BIN` (a file made on the computer, its contents lost): written, read back, `wrong B 0`.
   - The screen must look the same after each command (no stray pixels, no lock-up).
5. **Claude**: from what the console printed, fixes the mode / timing and makes the working mode the default.

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
- **Faster reads**: multi-block reads (CMD18) and SPI1's DMA in receive mode, once the read direction is known.

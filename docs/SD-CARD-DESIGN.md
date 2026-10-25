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

> **Superseded (2026-10-25):** the SD board (`hardware/sd-board/`, v5: the board is the splitter, two 10-pin
> sockets, no soldering) replaces the hand-wired kit and the "v2" tap board below. The 1 kOhm figures below belong to
> that kit; the board uses 220 Ohm (section 4). The board's README is the current parts list.

**For everyone: the SD board (hardware/sd-board/, 2026-10-25).** One small board with the socket (push-push), the
inverter, the resistors and the capacitors, ordered assembled from JLCPCB with the files in `hardware/sd-board/fab/` (or
from OSHWLab once published there). v2 (20 x 45.5 mm, the back of the board above the battery) takes a 6-pin 0.5 mm flat cable from a tap board at J12, so nothing is soldered; the tap board is still to design (it needs J12 metered). See `hardware/sd-board/README.md`.


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
   CMD (open-drain) against PC10. The socket's card-detect is not wired, so a hot-plugged card is not seen; for
   streaming, a periodic CMD13 (status) checks the card and only a CMD13 with no answer sends CMD0 (CMD0 resets a card
   in use). The board's MOSI branch gets 100 Ohm in series, which bounds that fight to about 15 mA.

The card's start-up runs at 400 kHz (SPI1 BAUD 149: 60 MHz / 150), then 20 MHz (BAUD 2). The screen runs at 60 MHz
(`LCD_BAUD` 0 in every profile; 30 MHz, BAUD 1, is the fallback if the column's stub disturbs it). PC8 can be driven
by the FM-1 and the card at once for a few clocks inside a session (the card not yet let go when D/C goes high):
R1 (220 Ohm on the board) keeps that to about 15 mA.

**Timing, estimates (the 2026-10-25 reviews):** the card runs at **20 MHz** (the user). The card's DO changes up to
14 ns (tODLY, SD default speed) after CLK's falling edge; the FM-1 samples on the rising edge, 25 ns later at 20 MHz.
The DO / D/C net (the PC8 pad, the panel's D/C input through its flex, the 80 mm cable, the 55 mm column, the socket)
is about 35 pF. R1 220 Ohm: a 7.7 ns time constant, about 15 ns to a clean level: 14 + 15 = 29 ns, past 25 before the
host's (unknown) setup time. 100 Ohm: 14 + 7 = 21 ns, a marginal pass; 47 Ohm: 17 ns, comfortable. So the board is
ordered with **R1 100 Ohm** and 47 Ohm on the shelf; `sd bench`'s 20 MHz CRC rows decide. 30 MHz is not expected.

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
| `src/storage/sd/pff.c`, `pff.h`, `pffconf.h`, `diskio.h` | Petit FatFs R0.03a (ChaN, 1-clause BSD: `LICENSES/BSD-1-Clause-FatFs.txt`), its own compile unit (`tools/build.py`); configured: read, write, seek, directories, FAT16 + FAT32 |
| `src/storage/sd/sdcard.c` | the API the rest calls (`sd_mount`, `sd_open`, `sd_read`, `sd_write`, `sd_seek`, `sd_opendir`, `sd_readdir`), each call taking and giving back the bus |
| `src/io/console_sd.c` | the `sd` commands (console builds only; included by `src/io/console.c`) |
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
- **Recording needs its own writer**: `pf_write` goes byte by byte through `disk_writep` (one CMD24 a sector) and the
  sector's end waits for the card's busy in the main loop, up to 500 ms (real cards hit 100-250 ms now and then).
  Recording needs the mirror of `sd_rd_*`: a polled CMD25 multi-block writer from a ring, never blocking on busy
  (CMD25 is also several times faster than CMD24 on most cards). Today's write path is backup-grade.
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

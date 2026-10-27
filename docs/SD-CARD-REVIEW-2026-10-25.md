# FM-1 SD card mod: what was done, the idea, and why

> Snapshot of 2026-10-25, written for an adversarial review. Kept as history: the current state is in `SD-CARD-DESIGN.md`.

Written 2026-10-25 at the end of the "sdcard" session, for an adversarial review. Everything is on the branch `sdcard`
(worktree `~/GitHub/optimist/worktrees/sdcard`, about 67 commits on top of `main`, **not pushed**). The FM-1 has been
put back together; **no FM-1 has the mod yet**. Nothing in this document has run on the real hardware unless it says
"measured on the FM-1".

How sure each claim is, marked in brackets:
- **[measured]**: calipers or a check on the real FM-1 by the user, or a build or test number.
- **[datasheet]**: read from a datasheet or the vendor SDK.
- **[estimate]**: worked out, never measured.
- **[unverified]**: a design assumption still to be proven on the bench.

---

## 1. The goal

The FM-1 (an 8-track FM/VA groovebox, JieLi AC7911B8 chip, running our Optimist firmware) is out of room:

| Region | Size | Free today | [measured, default profile, 2026-10-25] |
|---|---|---|---|
| Flash, app slot | 568 KiB | ~2 KiB | code, tables, built-in sounds |
| Flash, user data | ~340 KiB | fixed areas | projects, presets, kits, user samples, snapshots |
| RAMTEXT (fast code RAM) | 32 KB | 6 KB | hot DSP code |
| RAM | 96 KB | 5 KB | state |
| Pool | 336 KB | 7 KB | delay lines, reverbs, slicer, canvas |

What the user wants from an SD card:
- more code: the user-data flash becomes code flash;
- user data (projects, backups) on the card;
- later, samples streamed from the card and recording;
- later still, plugins loaded from the card.

Hard limits from the user:
- **no soldering on the FM-1** (tiny pins);
- it must be a mod **other owners can order and fit**;
- it must close inside the original case.

## 2. Why the card goes on the LCD's bus (the core idea)

**There are no free pins.** The chip is a QFN48 (6 × 6 mm, 0.4 mm pitch), and every GPIO it brings out is used. Some
pads even carry two port bits (PC5/PC2, PB8/PC0, PH7/PH8, PH4/PH6) [datasheet + firmware pin use]. The flash is a die
inside the package, so a bigger flash chip cannot be swapped in. No bigger AC79 variant was found for sale [web search
only].

**The trick:** share the LCD's SPI bus (SPI1). The LCD connector J12 already carries what an SD card in SPI mode
needs:

| Card line | LCD line it shares | How |
|---|---|---|
| CLK | LCD clock PC9 | direct |
| CMD / DI | LCD data out PC10 | direct |
| DAT0 / DO | LCD **D/C** PC8 (also SPI1's data-in pin, group B) | through a series resistor R1 |
| CS | LCD **CS** PC7, **inverted** | 74LVC1G04 |
| 3V3, GND | from J12 | |

The card is selected exactly when the LCD is not. The ST7789 panel ignores D/C while its own CS is high
[datasheet]. The firmware takes the bus between screen transfers: the screen is drawn by polled DMA from the main
loop, never from an interrupt [code].

**Why not other ways** (each considered and dropped):
- **A bigger flash chip:** inside the package, impossible.
- **Spare 74HC595 outputs for a CS:** all used [code: the scan word drives bits p and 11+p].
- **Sharing the 595's SCK/SER lines plus a key-matrix row as MISO:** still needs a CS pin and soldering on SOIC legs.
- **Code over USB/BT into RAM:** RAM is the scarcest thing. Code in flash costs no RAM.
- **A custom flex cable tap at J12:** quoted about $75–130 for a few. The user: "it costs more than the device".
- **An off-the-shelf 10-pin Y-splitter:** none exists. So **the SD board is itself the splitter** (user's ruling).
- **A breadboard test rig first:** the user dropped it. The assembled board costs about the same as the rig.

## 3. The board (v5): what it is and where it goes

Files: `hardware/sd-board/`, generated from code. `make_board.py` and `make_schematic.py` are run by KiCad 10's
Python, and `make_fab.sh` runs the design-rule and electrical-rule checks, a schematic-vs-board net compare, Gerbers,
BOM and CPL. All DRC/ERC checks pass and the nets match pin for pin [measured: tool output].

**Parts** (all JLCPCB-assemblable, LCSC numbers):

| Ref | Part | LCSC |
|---|---|---|
| J1 | microSD socket, push-push (TF-01A) | C91145 |
| J2, J3 | 10-pin 0.5 mm FFC socket, contacts on BOTH faces, 1 mm tall (HCTL HC-FPC-05-09-10RLTAG) | C5213750 |
| U1 | 74LVC1G04 inverter (CS) | C7827 |
| R1 | 220 Ω, the card's DO → PC8 | C22962 |
| R2–R4 | 47 kΩ pull-ups (DAT1, DAT2, the inverter's input during reset) | C25819 |
| C1, C2 | 10 µF, 100 nF | C15850, C14663 |

**How it is wired in, with no soldering:**
1. The LCD's own cable is unplugged from J12 and plugged into **J3** on our board.
2. A standard 10-pin 0.5 mm cable (80 mm, type A) goes from **J12** round the board's edge into **J2**.
3. All 10 lines pass straight through from J2 to J3, so the LCD works as before. The SD part taps the six lines it
   needs.

**Where it sits** (back of the FM-1's main board, all positions [measured] with calipers, MEASUREMENTS.md):
- The lower part (the card socket) is right of the battery, by its plug. The battery moves about 3 mm (the most it
  can: "4 mm maybe 3").
- A narrow column runs up beside the white speaker plug (85.6–90 mm across, 44–51 mm up) to a head under the audio
  jacks.
- **J3** sits right under the gap between the black jacks, as close to them as possible. The LCD cable "just about
  reaches the back of the jack" [measured: user test fit, 86.5 mm].
- **J2** is lower. The J12 cable comes between the right jack and the USB socket; the 5.5 mm cable fits there
  [measured: user].
- **The card** goes through a slot the user cuts in the case's bottom wall. It is 81.7 mm from the left wall's inside
  and sticks out 0.5 mm.

**Holding it:** a 3D-printed tray (`make_tray.py`, `sd-tray.stl`):
- a floor, rails with a lip the board slides into, and a collar round the cover's screw pole (6.9 mm, at 75/40 mm);
- one post on each side, reaching the back cover;
- the battery's wires pass over the tray, only the posts reach the cover;
- tray height 6.4 mm, chosen after the user printed and compared versions at +0.5 and +1.0 mm [measured: print test].

The user also printed `mockup-printable.stl`, the board and its parts as solid blocks.

**Board size:** 25.4 × 78.2 mm overall, an odd shape (lower block, column, head), 2 layers, 1.6 mm.

## 4. The firmware already written (branch `sdcard`, host-tested only)

| Piece | What | Status |
|---|---|---|
| `SDCARD` builder item (bit 409) | bus sharing in `hal/fm1_sd_bus.h`, ChaN's SPI card driver, Petit FatFs (FAT16/32, 8.3 names, read/write in place, no file create or grow), console `sd` commands | host tests: `tests/sd_test.c`, 439 checks, with a byte-level card model on real FAT images; the model checks the LCD only ever sees 0xFF |
| D/C idle high (1cec4636) | the LCD's deselect leaves D/C high, so the FM-1 never holds PC8 low against the card driving it | code + test |
| DMA block reads (f9c56fb7) | CMD17/CMD18 by SPI1 receive DMA, CRC16 checked, falls back to byte reads if the DMA probe fails | **[unverified]** whether SPI1 as master receives by DMA this way. The pattern is copied from an SDK slave example |
| `sd bench`, `sd write`, `sd mode` | measure every read mode at 20 and 30 MHz: KB/s, CPU time a block, CRC errors | to run on the bench |
| `LAYOUT` item (bit 410), the fake SD mode | app slot 568 → 816 KiB by squeezing user data to 64 KiB (1–2 projects, 4 presets, 4 kits, no user samples); the loader clears the other layout's sectors on a switch | host tests + emulator boot; not on the FM-1 |
| Arena (`core/arena.c`) | the parts' engine memories laid out per project instead of fixed; a 5-level undo reserve kept free | tests (300,000 random switches) |
| Memory short (user's ruling) | refuse, then propose what to drop ("DROP T2 FM6?"); a dropped part becomes a **silent placeholder** (keeps its sequence, plays nothing, shown dimmed) | tests |

**Speed** [estimate]:
- 20 MHz is about 2.2–2.4 MB/s by DMA, 30 MHz about 3.2–3.5 MB/s, before the card's gaps between blocks.
- Byte-by-byte reads are about 1 MB/s with the CPU busy the whole time.
- The user needs speed for sample streaming ("all the time used for transfer is time we won't have for processing").
- The screen runs at 60 MHz on the user's FM-1 [measured: user].

Ready-made packages for the bench, in `~/GitHub/optimist/worktrees/sd-packages/`:
- `3-sd-dma-bench.fwsc`, built with the ACID engine left out to fit (the user OK'd that for the test);
- the earlier probe packages.

Claude never installs anything; the user flashes.

## 5. The review done so far, and its four changes

The user asked: will it really work (pin sharing, two devices, current, cable length, signal loss, impedance)? The
conclusions [estimate, reasoning only]:
1. **Sharing through the inverter is sound.** At 20 MHz the CS edge (a few ns) is far shorter than a bit.
2. **R1: 1 kΩ became 220 Ω.** 1 kΩ with PC8's input capacitance is too slow for 20 MHz reads. With D/C now idle-high,
   the only drive fight is short and limited to about 15 mA.
3. **To add:** 33 Ω in series on CLK and MOSI where the SD branch leaves the through-lines (C23140). This damps the
   stub to the card, so it disturbs the LCD's 60 MHz lines less.
4. **To add:** ESD protection at the card slot, two USBLC6-2SC6 (C7519). The slot is open to the outside world.
5. **Marked on the board:** "LCD: gold UP", "IN: gold DOWN". The double-sided sockets connect a cable put in upside
   down **mirrored** (pin 1 ↔ pin 10).

Known risks that remain:
- Only pins 1 and 10 of the cable are ground, so there is crosstalk on about 10 cm of flat cable at 60 MHz.
- The 3.3 V supply's spare current is unknown (a card draws peaks of 50–100+ mA when writing; the chip's LDO is
  200 mA in all [datasheet]).

## 6. What is NOT known yet (the blockers)

1. **J12's pin map is a guess.** `LCD_PINS` in `make_board.py` is PROVISIONAL: 1 GND, 2 MISO, 3 CLK, 4 MOSI, 5 CS,
   6 3V3, 7 RST, 8 LEDA, 9 LEDK, 10 GND. Next:
   - the user has ordered a 10-pin breakout and cables;
   - Claude builds a pin-finder firmware (one of PC7–PC10 high at a time);
   - the user meters it;
   - Claude re-routes the column and J2.
   **The board must not be ordered before that.**
2. **Whether the LCD cable arrives mirrored** at J3.
3. **Where 3.3 V comes from on J12 and how much current it can give.**
4. **Which SPI1 read mode works** (`duplex`, `dirrx`, `bitbang`) and whether DMA receive works as master.
5. **Whether the panel really ignores the bytes it sees** while the card talks. The firmware sends a NOP first and
   only ever clocks 0xFF with the panel selected.
6. **Mechanical tolerances on a real assembled board** (the printed mockup and tray fit; a real PCB has not).

## 7. Cost (JLCPCB quote, 2026-10-25) [measured: the quote page]

Assembled boards (2 layers, Economic PCBA, before the ESD chip and the 33 Ω resistors):

| Boards | Price | Per board |
|---|---|---|
| 2 assembled of 5 bare | $26.99 | $13.50 |
| 5 | $28.99 | $5.80 |
| 10 | $33.31 | $3.33 |
| 20 | $42.86 | $2.14 |
| 50 | $65.56 | $1.31 |

- About $19 of each order is fixed: setup $8.24, stencil $1.55, 3 extended parts $9.27.
- The ESD chip is about +$3.50.
- Shipping to Belgium: $8.16 (Global Standard, 12–16 days) up to $23.40 (DHL).
- The user's sweet spot: 5 to 10 boards. About $41–45 delivered.

The kit per user: the board, one 80 mm 10-pin type A cable, the printed tray, a slot cut in the case.

## 8. Things a reviewer should know are out of date

- `docs/SD-CARD-DESIGN.md` §2 and §3 still describe the earlier plans:
  - the Adafruit 4682 + 74HC04 hand-wired kit;
  - a "v2" 6-pin cable and tap board;
  - **1 kΩ** for R1 (now 220 Ω), with the "3.3 mA" contention figure that went with it (now ~15 mA).

  §4 onwards (bus sharing, DMA) is current.
- `MEASUREMENTS.md` has a line saying "both our sockets top-contact, the J12 cable type B". That was replaced: the
  sockets are now double-sided and the user bought type A cables.
- `MEASUREMENTS.md` "l = 78 mm" (the pole): the test fit put it at **75 mm**, which the board uses.

## 9. Questions worth attacking

- Is sharing PC8 (the LCD's D/C) as the card's DO safe in every state, including power-up, a card left mid-transfer by
  a reset, and a hot-plugged card?
- Can 220 Ω plus the pin capacitance really make 20 MHz reads? Should the order be 33 Ω on DO too, with R1 removed?
- Does the 74LVC1G04's propagation delay open a window where both devices are selected?
- Will a card's write current dip the 3.3 V the LCD and the chip share?
- Is the 10-pin pass-through with a stub at 60 MHz acceptable? Could the stub break the screen when no card is in?
- Is the tray plus pole-collar retention enough against insertion force (push-push needs a firm push) with the cover
  closed?
- Is the fake SD layout (app to 816 KiB) safe against the official updater and the boot ROM's reads past 0x93000?
  (The "far probe" exists to test that; not run.)
- Is anything here worth the risk compared with simply leaving out features to fit?

## 10. Where everything is

- Design and code: `~/GitHub/optimist/worktrees/sdcard` (branch `sdcard`), `hardware/sd-board/`, `docs/SD-CARD-DESIGN.md`,
  `docs/SD-MODE-DESIGN.md`, `firmware/src/storage/sd/`, `firmware/hal/fm1_sd_bus.h`, `firmware/src/core/arena.c`.
- This folder (`~/Desktop/FM-1 SD board/`): renders, STLs (mockup, tray), measuring sheets, the shopping list,
  copies of README.md and MEASUREMENTS.md.
- Bench packages: `~/GitHub/optimist/worktrees/sd-packages/`.

# SD board: speed review and plan for the next version

2026-11-05. Nothing here has changed a board. Numbers are marked **measured** (on an FM-1), **solved** (2D field solve,
`analysis/field_solve.py`, about +-10 %) or **model** (circuit model, `analysis/path_delay.py`: its pad, card and LCD values
are assumptions, so read differences between rows, not absolute numbers).

## The variants (the same-side long board was dropped 2026-11-08 and the mini on 2026-11-09, the user's rulings)

| Folder | Plugs | Built? |
|---|---|---|
| `sd-board/` | one on each side | no |

## The short answer

1. **The limit is read timing, not signal quality.** Above the limit the data arrives exactly one bit late (**measured**), which
   is what a read loop slower than half a clock does. Noise and ringing would give random errors.
2. **Most of the loop is not on the board.** The loop is 33-42 ns today. The board accounts for about 8-12 ns of it (**model**).
   The other 21-34 ns is the chip plus the card, and no layout touches that.
3. **Ribbon length and impedance do not matter for the card.** 40 to 100 mm of ribbon moves the loop by under 0.2 ns (**model**).
4. **A smaller board is not needed for speed.** The 26 x 25 mm mini (dropped 2026-11-09) gained about 1 ns, less than a clock step.
5. **15 MHz may be reachable with a board change, 20 MHz probably not.** The test that settles it is test 1 below.

## What is measured

The FM-1 samples half a clock after the card's data changes, so each clock has a limit on the read loop (`T`):

| Clock | 8.57 | 10 | 12 | 15 | 20 MHz |
|---|---|---|---|---|---|
| Half a clock | 58.3 | 50 | 41.7 | 33.3 | 25 ns |

| Setup | Passes | Fails | So `T` is |
|---|---|---|---|
| R5/R6 510, R1 220, normal SCK drive | 8.57 | 10 | 50-58 ns |
| The same, SCK HD | 10 | 12 | 42-50 ns |
| R5/R6 330-340, R1 150, SCK HD | 12 | 15 | **33-42 ns** |
| R5/R6 245, SCK HD | 12 | 15 | 33-42 ns |

The hand-tuned v6 reads 20 of 20 at 12 MHz on pink and black. SCK HD on v6 at 7.5 MHz gave one step; DI drive gave nothing.

This table replaces the "50-58 ns loop" in the dropped mini's design notes, which is the first row only. Its explanation also needs
retracting: the 50 ns in the SD spec is the card's delay in identification mode. In data transfer the spec's limit is 14 ns, at
default speed as well, so "high speed would drop the card from 50 to 14 ns" was wrong.

## Where the loop goes

| Part of the loop | ns | Source |
|---|---|---|
| CLK: R5 into the card's input, ESD and trace | 4-6 | model, 340 Ohm |
| DO: R1 into the bus | 4-6 | model, 150 Ohm |
| Flight on all copper and ribbon, there and back | about 2 | solved, included above |
| Card output delay | 5-14 | 14 is the spec's limit; 5-8 is an old estimate, not measured |
| Chip: pad, input path, sampling | the rest, about 7-29 | **unknown, the biggest open number** |

If the chip's SPI block samples through a 60 MHz input stage, one or two ticks are 17-33 ns on their own. That is a guess, and test 1
measures it.

## What each change is worth (model)

Changes to the sum of the CLK and DO delays, against tuned v6 (8.2 ns at 50 %, 12.0 at the card's and chip's real thresholds):

| Change | Gain | Comment |
|---|---|---|
| R5 340 to 245 | 0.8-1.6 ns | **measured**: no step |
| Mini board's copper, same resistors | 1.1-1.3 ns | not a clock step |
| Ribbon 80 to 40 mm | 0.2 ns | nothing |
| Ribbon impedance 80 to 160 Ohm | 1 ns | not something we can choose |
| ESD 2.5 pF to 0.5 pF | 0.5-0.9 ns | cheap if a part fits the layout |
| A card with 5 pF instead of 8 | 0.7-1.4 ns | not ours to choose |
| R5/R6 100, R1 47 | 3.6-6.5 ns | **the only large one**, unsafe without a gate (below) |
| Stronger SCK (35 to 20 Ohm) | 0.5 ns | firmware; HD + HD0 on SCK gave nothing over HD on the 510/220 board and none at 330/150 (test 3, 2026-11-06): the drive is used up |

The model under-predicts: 510 to 340 gave more than the model's 3.3-5 ns, as the table of steps needs. So take these gains as a
lower bound, for example the card's or ESD's real capacitance may be larger.

## Impedance and traces

| Line | Width, ground | C pF/mm | Z0 Ohm | Delay ps/mm |
|---|---|---|---|---|
| Today, 1.6 mm board | 0.2, pour 0.25 away | 0.057 | 92 | 5.2 |
| Today, in the column | 0.2, no top pour beside it | 0.041 | 130 | 5.4 |
| Mini, 1.0 mm board | 0.2, pour 0.25 away | 0.058 | 90 | 5.3 |
| Mini, no top pour | 0.2 | 0.046 | 118 | 5.5 |
| 1.0 mm, 0.5 wide | pour 0.2 away | 0.085 | 63 | 5.3 |
| FFC, CLK, other lines driven | J12 pins | 0.060 | 80 | 4.8 |
| FFC, CLK, GND and 3V3 only | J12 pins 1, 7, 10 | 0.025 | 158 | 4.0 |

- **Copper is cheap in time.** All of v6's 47 mm behind R5 is 2.7 pF, about half what the dropped mini's design notes assumed
  (0.1 pF/mm). The 35 pF on the DO net in `docs/SD-CARD-DESIGN.md` also looks high: the same sum gives about 20 pF. Test 2 measures it.
- **Do not chase 50 Ohm.** At 15 MHz with 1-3 ns edges, a 25 mm board is lumped. Widening traces adds capacitance and slows the
  RC paths. Keep 0.2 mm.
- **Keep some resistance at the card.** With 1 ns edges the model overshoots 11 % at 100 Ohm, 26 % at 47 and 47 % at none, because
  nothing terminates the bus at either end. Keep at least 47-100 Ohm, or a gate's output with a 22-33 Ohm series resistor.
- **The ribbon's real risk is crosstalk.** It has two grounds in ten lines, and CS (pin 3) sits right next to CLK (pin 4). U1, the
  inverter on CS, has no hysteresis. A Schmitt part (74LVC1G14, footprint to check) is a cheap guard. It would be for
  robustness, not speed; no fault has been traced to this.
- **Ribbon length matters for the screen,** which runs at 60 MHz with no datasheet margin, so the shortest cable that reaches stays the rule.

## The resistors, and why they are stuck

R5/R6 are not there for signal quality. They protect the LCD bus from a card that hears LCD traffic: with a card in native mode,
stock firmware lost the screen with CLK direct, was distorted at 150 Ohm and was fine at 510 (Julius, cold boot). So tuning down is
capped, and 330 was proven at a cold boot on 2026-11-06 (test 4: stock M-VAVE V15 and SLOOP 2.8.5, screens clean after a minute, on a hand-tuned v6 with 338 / 150).

**The way out: gate CLK with the card's select.** A 74LVC1G08 (AND) on CLK, enabled by the LCD's CS being high, passes clock only
when the LCD is deselected. Stock firmware sends no clock while CS is high (Julius, logic analyzer), so a card never sees LCD
traffic, never answers, and R5/R6 stop being needed. R1 could then drop to 47-100, keeping only the short overlap protection.

| | Tuned resistors | CLK gate, 22-33 Ohm after it, R1 47-100 |
|---|---|---|
| CLK delay | 4-6 ns | about 3.5 ns (gate 2-3 typical, datasheet to confirm for the chosen part) |
| DO delay | 4-6 ns | 2-3 ns |
| Load on the LCD's CLK | card, ESD and copper through R5 | one gate input |
| Cold boot with stock firmware | open | safe by design (to confirm) |
| Parts | none added | one SOT-23-5, one basic resistor |

Gain over tuned v6: about 3-5 ns. That can turn 12 into 15 MHz if `T` sits in the lower half of 33-42. It does not reach 20 MHz unless
the chip's share is far smaller than it looks. Worth about 1,430 KB/s against 1,147 (estimate, the same 2.3 clocks a byte).

Gating DO as well is possible and removes the DO stub from the bus, but it adds a gate delay on the return path. Only worth drawing if test 1 shows it pays.

**A branch, not for the current board (the user, 2026-11-08 evening: "a third chip whose only job is to protect the card and do the
switching"; the sd-board board stays as designed).** The same idea as the gate, done with one bus switch instead of logic: a
quad analog bus switch (74CBTLV3126 class: 3.3 V, about 5 Ohm on, 0.25 ns, individual enables; part and LCSC code to confirm) between
the bus and the card on CLK, MOSI and DO, all three enabled by the LCD's select being high. Under stock firmware the card is physically
off the bus (its side held by the pull-ups R7/R8 and a pull-down on CLK); under ours it is connected the moment the LCD is deselected,
in both directions, with no gate delay on the return path. The bus sees a few pF of off-capacitance instead of the card, the ESD parts
and the copper, so R5/R6 go and R1 drops to the overlap value. It does not regenerate edges (a gate does), and the "no clock with CS
high" premise still has to be checked on our units with a logic analyser before any board relies on it. A small MCU or CPLD that
recognises our firmware and then closes the switch would make the enable independent of that premise, at the cost of a part with
its own boot and code; only worth it if the check fails. **Dropped the same evening (the user):** the loopback already showed the
chip's own path failing at 20 MHz, the screen only writes while a card read is a round trip inside one bit, so no switch, gate
or layout gets past about 15 MHz; what remains is picking the resistor values on the board as designed, with the twins.

## Tests on the boards we have

Target: pink (FM1-620C), the test unit, with its hand-tuned v6. In order, each decides something. The user's pocket scope (a DSM2xx) can do test 8 only as far as its bandwidth allows: duty and edges, not ns delays.

| # | Test | Who | Decides |
|---|---|---|---|
| 1 | **Loopback.** Card out, 100 Ohm plus a wire from R6's bus side to JP1's right pad (`BENCH-SHEET.md`; a bare wire would short PC10 against PC8), new firmware test reads back its own MOSI at each clock | Claude builds and runs; user solders the wire | The chip's own share. If it fails at 15 MHz, stop: no board helps, take the tuned resistors. If it passes at 20-30, the gate is worth it |
| 2 | **Bus load.** PC8 and PC9 driven high, released to a pull, time to fall counted by the CPU | Claude | Replaces the 35 pF guess, firmware only |
| 3 | **SCK HD0 + HD at the tuned resistors.** Strongest drive on SCK | Claude | Free gain, firmware only |
| 4 | **Cold boot at 330/150.** Stock and SLOOP, card in, screen right? | User | Whether 330 can ship. Blocks every board |
| 5 | **Lower resistors.** R5/R6 150, R1 100, then 47; read clock and screen | User solders, Claude reads | Checks the model's largest gain on real copper before spending on a gate |
| 6 | **Ribbon 50, 60, 80 mm** (the cables on the shelf) | User swaps, Claude reads | Checks "ribbon does not matter" directly |
| 7 | **Bodged gate.** A 74LVC1G08 on v6's CLK with R5 bridged | User, if test 1 says go | Proves the gate before ordering |
| 8 | **Scope,** if one is at hand: CLK at both ends of R5, DO at both ends of R1, CLK's duty at 12 MHz | User probes | Direct read of each part of the loop. BAUD 4 (12 MHz) is an odd divider, so its two half clocks may differ: if so the limit is not what the first table says |

## Dense kits: what limits streaming (added 2026-11-05)

| Where | Same pattern: 6 files of the Acoustic kit, 8 blocks a run | Source |
|---|---|---|
| Mac, built-in reader, FM1-BLACK's 16 GB card | 0.48 ms a run (median), 0.64 at p95, max 3-7 ms; 7,953 KB/s | **measured**, `analysis/sd_mac_bench.py` |
| FM-1 black, 12 MHz, a track playing | 26 % of first blocks 3-6 ms, 10 % of later blocks 1-3 ms; about 380 KB/s at DRY 0 | **measured**, not a limit test |
| FM-1 black idle, the polled reader, 12 MHz, 4 contiguous files (`sd ceil`) | 1 block a run 615 KB/s, 8 a run 1,015, 64 a run 1,059; first block 0.75 ms (max 1.8), later blocks 455 us | **measured 2026-11-05** |
| FM-1 black under `heavy` (render 3.9 of 5.8 ms, CPU 65 %), polled, 10 MHz | 8 a run 366 KB/s, 64 a run 404; every wait over 1 ms had a render inside, none without | **measured** |
| the same with the reader in SPI1's interrupt (`sd irq 1`, a run ahead) | 8 a run 702 KB/s, 64 a run 1,116 | **measured** |
| the heavy kit playing 28 s at 10 MHz, polled against the interrupt reader | 218 KB/s, 195 dry voices, 68 held frames, lead min 0; then 366 KB/s (its demand), 0 dry, 3 held, lead min 14 ms | **measured**, `sd stats` |

Measured 2026-11-05 on black (`sd ceil`, `sd stats`, the sd-loop-test package): the card is not the limit and neither is the board.
Idle, 8-block runs already give 1,015 KB/s at 12 MHz, close to the bus's 1,150. Every wait over 1 ms, idle or under load, had a
render inside it, and none did without: the reader was polled from the main loop, and the render (ALNK0, priority 3; 1.1 ms idle,
3.9 ms under a dense kit, every 5.8 ms) stopped it for its whole length. The card's own first-block wait is 0.75 ms (max 1.8, a few
times 3-4 ms). Block time follows the clock set (6 / 7.5 / 10 / 12 MHz: 797 / 711 / 458 / 390 us a block, against 683 / 546 / 410 /
341 nominal), so BAUD gives the clock it says. The fix is firmware: fm1-base `device/sd_irq.h` takes a run's blocks in SPI1's
interrupt (IRQ 16, priority 5, nesting over the render works), a whole run ahead of the main loop. Its vector must be attached at boot:
`fm1_guard_lock_top` write-protects the vectors afterwards, and the first try reset the FM-1 at its first run. With it the heavy kit
plays with no dry voice where the polled reader had 195 in 28 s.

The clock still matters at the margin: black stepped from 12 to 10 MHz twice that day on a CRC failure under load, so 12 MHz is
thin there, and the board tests (the loopback, resistors in parallel across R1 / R5 / R6) now decide whether 12 can be made safe or
15 reached, not the streaming rate. The kit files on black's card sit one 32 KB cluster apart (each file in runs of 64 blocks), which
does not shorten 8-block runs but is why `sd ceil` skipped them until it took 16 KB first runs. The figure of 1,430 KB/s above is a
sequential one.

**2026-11-06, pink, the board tests done** (`BENCH-SHEET.md` has each run): test 4 passed, 330/150 ships; test 3 and the MOSI
drive gave nothing on the card path; the loopback (test 1) put the chip's own path at 15 MHz pass, 20 fail, so no gate and no board
change reaches 20, and with the card's 5-14 ns on top the card path is 10-12 MHz, which is what both units measure (12 marginal,
one CRC step down to 10; two cards of one brand sit one step apart). **The SD code the same day, main with the interrupt reader,
10 voices and the drums split over both cores:** ten card lanes on every step with no inserts (`heavy 0`) stream 399 KB/s with 0
stalls, 0 CRC, 0 dry voices, 1 held frame, the buffer never under 28 ms; core 0 at 40 %, the drum halves 129 and 128 us a block
on the two cores, no shedding (the day before, 9 samples: 75 % on core 0, nothing on core 1, buffer empty). With the full stress
project (two inserts on every lane and three chord tracks) core 0 saturates at 78 % and sheds, 62 dry voices in 25 s: that is CPU,
the stream itself stayed clean (202 KB/s, 0 stalls). The board is out of the streaming question for good.

**Decision 2026-11-06 evening: R5/R6 240, R1 91, LCD at 30 MHz.** Measured on pink with a stock v6 bodged to 510 || 470 = 245 Ohm on R5 and R6
and 220 || 150 = 89 Ohm on R1: the card clock reaches 15 MHz (10, 12 and 15 pass, 20 is refused, 0 CRC errors), one step above the 330/150
board (12 MHz, marginal). At the firmware's normal LCD clock (60 MHz, `device/lcd.c` `LCD_BAUD` 0) the panel stays black on that board,
card in or out; with the LCD clock halved (`LCD_BAUD` 1, 30 MHz) the screen works, and the user ruled 30 MHz fine for the screen. Stock V15
and SLOOP 2.8.5 cold-boot with clean screens on that board with the card in (their own LCD clocks). So the next order uses R5 = R6 = 240 and
R1 = 91 (E24 single parts for 245 and 89) and Optimist drives the LCD at 30 MHz. The chip's own path caps the card at 15 MHz (loopback: 20
fails with no board resistor in the loop), so there is no clock gate, ever. 330/150 stays the value proven at the LCD's 60 MHz.

**The order (the user, 2026-11-06 evening): `sd-board`** (J2 on the bottom, one socket on each side), at 240/91; the mini is out (and its folder removed 2026-11-09); printable mockups of both long boards were made for the fit (`mockup-printable*.stl`).

**Measured 2026-11-06 late, the cost of the LCD at 30 MHz (the user's own project, a 10-lane kit with long tails, pink on the 245/89 board at 15 MHz, main with the user's profile):** the SD STREAM page over 8 s at each PRE-BUFFER: 25 ms 10/10 streams, buffer least 0, about 100 dry voices; 50 ms 5/10, about 100 dry; 100 ms 7/10, about 100 dry; 200 ms 5/5 (the planner has room for five rings only), about 25 dry. The dry count climbs on every page, least on the mixer (33 in 8 s), most while a page repaints; CPU0 32 %, CPU1 10 %, no underruns, the arena 129 of 156 KB. Reading: at 30 MHz each screen strip holds the shared bus twice as long, and ten continuous voices wait behind it; this morning's ten short hits every step did not show it. Open for the firmware: hold frames earlier when the buffer is low, fewer meter repaints, longer card runs; or the clock gate after all (the LCD back at 60 MHz with the card at 15). The user hears the trade: 25 ms plays more voices, more of them cut; 200 ms fewer voices, fewer cuts.

**Tried the same evening, audio first in the stream code** (sd_stream.c SST_STRIP_MS 8 -> 64, SST_HOLD_MS 100 -> 1000, uncommitted, worktrees/sdloop-scratch/audiofirst-main.fwsc): the card reads more (465-565 KB/s on the SD STREAM page, 250-440 before) but the dry count still climbs about 6 a second on every page (47 in 8 s static, 48 on the mixer; 70 and 33 before), buffer least 0, CPU0 32 %, CPU1 12 %. So the screen's share of the bus is part of it, not the cause. Next: the console's counters (runs, blocks a run, stalls, held, lead min, cut, nostream) on this project, which needs USB_CONSOLE in the user's profile build; suspects: ten rings filled from empty at a hit on every lane (ten runs of about 3 ms against a 25 ms head), or the reader's pick order.

## Findings to fix whatever is decided

- **The long boards ordered 510/220.** Done 2026-11-06 after test 4 passed: both now order R5/R6 330 and R1 150.
- Done 2026-11-06: `archive-v6-tested/DESIGN.md` and `docs/SD-CARD-DESIGN.md` now say 10-12 MHz at 330/150 and the polled reader, not 20 MHz.
- **The twins, 2026-11-08 (sd-board only):** a pair of 1.6 x 2.4 mm pads in parallel with each of R1, R5, R6 (RT1, RT5, RT6, not placed by the fab), so a value is trimmed with a leaded resistor on them, never on the 0603's ends (the user). Plain pads, no via and no line of their own (the user: the part's legs do the wiring). RT1 left of the swap block; RT5 and RT6 have their pads apart, the LCD-side pad in the head on its lane, the SD-side pad on the swap block's row of its SD net, the part lying along the column; silk says what each is in (DO, CLK, MOSI). Fab files regenerated, DRC 0. Details in `sd-board/DESIGN.md`.
- **sd-board back to 330 / 330 / 150, 2026-11-09:** JLC had 240 and 91 out of stock and the user will not hand-solder SMD on this board; it places the highest values proven screen-safe at 60 MHz (basic parts C23138, C22808), the twins trim them down after the sweep (test 7).
- **Bottom parts' rotation in cpl.csv, 2026-11-09:** sd-board now writes 180 minus KiCad's angle for a bottom part (J2: 180, was 0); the jank session fitted JLC's own footprint for C3446068 on J2's pads: 0.00 mm at 180, 2.32 mm off at 0. Still check JLC's preview. The cable labels went from 0.7 to 0.8 mm (JLC's minimum); nine 0.6 mm labels stay.
- **The BOM moved again, 2026-11-06 evening: R5/R6 240, R1 91** on the long boards (`make_board.py`, `make_schematic.py`, `fab/bom.csv`, `fab/sd-board.net.xml`, `.kicad_pcb`, `.kicad_sch` edited by hand, KiCad generators not re-run). The condition: the LCD at 30 MHz (`LCD_BAUD` 1), because the panel stays black at 60 MHz on 245 / 89. The LCSC codes for 240 and 91 are to confirm (the 330/150 codes C23138 and C22808 are removed from the files); gerbers, CPL and the DRC/ERC reports to regenerate with KiCad.

## Still to clean up

The folders were renamed on 2026-11-05 (`hardware/README.md` lists them). Left for after the design settles: one generator with a
variant argument, since each board has its own `make_board.py` and the long ones differ by about 50-110 lines.

# SD board: speed review and plan for the next version

2026-11-05. Nothing here has changed a board. Numbers are marked **measured** (on an FM-1), **solved** (2D field solve,
`analysis/field_solve.py`, about +-10 %) or **model** (circuit model, `analysis/path_delay.py`: its pad, card and LCD values
are assumptions, so read differences between rows, not absolute numbers).

## The three variants

| Folder | Plugs | Built? |
|---|---|---|
| `long-same-side/` | both on the top | no; `archive-v6-tested/` is its tested ancestor |
| `long-both-sides/` | one on each side | no |
| `mini/` | one on each side | no |

## The short answer

1. **The limit is read timing, not signal quality.** Above the limit the data arrives exactly one bit late (**measured**), which
   is what a read loop slower than half a clock does. Noise and ringing would give random errors.
2. **Most of the loop is not on the board.** The loop is 33-42 ns today. The board accounts for about 8-12 ns of it (**model**).
   The other 21-34 ns is the chip plus the card, and no layout touches that.
3. **Ribbon length and impedance do not matter for the card.** 40 to 100 mm of ribbon moves the loop by under 0.2 ns (**model**).
4. **The mini board is not needed for speed.** It gains about 1 ns, less than a clock step. Choose it for fit or cost only.
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

This table replaces the "50-58 ns loop" in `mini/DESIGN.md`, which is the first row only. Its explanation also needs
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
| Stronger SCK (35 to 20 Ohm) | 0.5 ns | firmware; HD + HD0 on SCK gave nothing over HD on the 510/220 board, not tried at 330/150 |

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

- **Copper is cheap in time.** All of v6's 47 mm behind R5 is 2.7 pF, about half what `mini/DESIGN.md` assumed
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
capped, and **330 is not yet proven at a cold boot**. That is still open on all boards.

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

## Findings to fix whatever is decided

- **The long boards still order 510/220.** Only the mini has 330/150. An order today gets a board that needs hand rework to reach
  12 MHz. Update after test 4.
- `archive-v6-tested/DESIGN.md` and `docs/SD-CARD-DESIGN.md` still speak of 20 MHz with 220 Ohm, and "real cards reach 20 MHz".
- `mini/DESIGN.md`: the 50-58 ns loop and 0.1 pF/mm are out of date.
- The mini's other open fit items stand on their own: the height under J3, the cable across the socket, the lid.

## Still to clean up

The folders were renamed on 2026-11-05 (`hardware/README.md` lists them). Left for after the design settles: one generator with a
variant argument, since each board has its own `make_board.py` and the long ones differ by about 50-110 lines.

# FM-1 emulator: upstream pull requests (draft, local only)

Target: [simonjohansson/fm1-emulator](https://github.com/simonjohansson/fm1-emulator)
(GPL-3.0-only, Simon Johansson), `main` at `81b9ed9` ("Record the remaining JIT
performance work", fetched 2026-10-05). Nothing has been pushed, opened or sent.
The branches live in this machine's clone of the emulator (`~/GitHub/fm1-emulator-*`).

Source of the work: our fork `feat/boot-fixes` at `697fa61`, plus `feat/a2asm-emu`
`f51017e` (signed read-modify-write offsets). Every change was re-implemented on
upstream's current code, because upstream rewrote the decoder (`decode.rs`
First/Extended/Wide tables), the time model and the GUI (worker thread) in
parallel with us. No branch is a cherry-pick of the fork.

## 1. What upstream did since our fork point (`f0cc63b`, 77 commits, all 2026-10-05)

- **Official (stock) firmware boot**, driven by hardware measurements: Simon
  flashes small probe firmwares (`tools/build_*_probe.py`) to physical FM-1s
  (FM-1_981 to FM-1_998) and encodes the captured results as tests. Measured:
  carry/overflow flags of all register arithmetic, float min/max and float
  compare-branches, repeat loops (count >= 32 behaviour), interrupt deferral in
  repeat and conditional blocks, IRQ context/priority arbitration (ICFG), idle
  wake latency, register-list memory order, RF/Bluetooth startup, PMU/ADC
  bandgap and temperature, USB PHY handoff.
- **Peripherals**: `wireless.rs` (BT/RF register transactions), `uart.rs` (UART
  MIDI RX/TX), `shift_spi.rs` (key matrix over SPI2), NOR erase/program, LCD SPI
  completion interrupts, factory presets from FWSC packages, CDC console input
  (terminal stdin reaches the guest).
- **Time model**: hardware time advances from the guest CPU clock; one
  instruction per cycle of the firmware's system clock (Felucca runs at about
  420 M instructions per guest second). Two cores share hardware time.
- **Performance**: first-word decode table and wide-word cache, a basic-block
  cache, and an ARM64/x86-64 JIT that still returns to the scheduler after each
  instruction. `PERFORMANCE.md` plans native batching next. The GUI now runs the
  guest on a worker thread.
- **Style**: imperative, capitalised subjects without prefixes ("Decode ...",
  "Match ... to physical FM-1 results"); bodies say what changed, why, and how
  it was validated (hardware capture, test counts, firmware regressions), often
  ending with `Prompt:` lines (his own AI sessions). Tests live mostly in
  `tests/runtime.rs` with a `cpu(&[halfwords])` helper; firmware tests are
  `#[ignore]` and read `FELUCCA_FWSC`, `FM1_STOCK_FWSC`, `FELUCCA_ELF`. CI
  (`.github/workflows/emulator.yml`): `cargo fmt --check`, then
  `cargo test --locked --release --features gui` on Linux amd64/arm64, macOS
  arm64 and Windows, plus the pinned Felucca 0.9-beta boot test. Policy visible
  in the code: unmeasured exceptional cases fault explicitly rather than guess
  (e.g. NaN in float compares).

### Overlaps: our fixes upstream already has (not in any PR)

| Fork commit(s) | Upstream | Note |
|---|---|---|
| `7c10d9f`, `9845bbf`, `b8f1212` carry of add/sub-immediate, NZV of addc/subc | `0f57a02` | All register arithmetic sets V/C/Z/N via `Cpu::arithmetic`, measured |
| `a840af5`, `899c884` register-list order | `c3e2b47` | measured |
| `379d818` three-operand and-not | `76e9749` | |
| `455f05f`, `c4c1389` signed > conditional, kind E3 | `dbf2142` | |
| `379d818`/SLOOP.md kind CB plain imm12 | `c1a8144` | |
| `ccbfe7c` repeat count latch | `be288a6` | upstream measured counts >= 32 differently; our test dropped |
| `f9c871f` no IRQ inside repeat/conditional | `d4c5ea8`, `76e789c` | measured |
| `8421d44`, `7f13eab`, `203d500`, `71eae48` 64-bit MAC and pair shifts | `ec711a2`, `acc83fc` | |
| `3433d4f` rotate right by immediate | `8e587ba` | |
| `0cf86d0`, `f092145`, `8582b9f` post-increment and negative byte offsets (part) | `c0f9501`, `f5c3ac6`, `d242699`, `3d54958` | the rest is in pr/cpu-fixes |
| `6439a4d` halfword post-increment store | `a4782b1` | |
| `fa64637`, `5d74aba` E868 subtract, E86D shift | `13c0e9d`, `f9f945c` | offsets were still unsigned: fixed in pr/cpu-fixes |
| `20c5598`, `f1df6a0` float compare-branches, FPU ops | `ab454f1`, `6df9ae6` | measured; we add the remaining rows |
| `0ca626d`, `cf9d6bd` masked push/pop, {psr, reti} frames | `cf0768c`, `eb65237` | pr/cpu-fixes generalises the frames |
| `2b97dfc` NOR program/erase | `0a63beb` | |
| `54b41de` JL_RAND, `f8a8924`/`0601235` radio stubs | `81cac6b`, `7879fe1`.. `f12e251` | upstream models the radio from captures |
| `57b5023` idle waits for an interrupt | `18523d3` | measured wake latency |
| `74692b8` both cores share one step of guest time | `9cbb326` | |
| `3455af3` selectable CPU clock | firmware clock model | pr/ui-audio-knobs adds an opt-in override |
| `feat/stock-works` `56cf199` factory presets, `3125a7a` SPI2 key matrix | `f7b9105`, `1e29802` | same subject line even; stock work should rebase on upstream |
| perf/decode-cache, perf/event-devices | `7b162c1`, `9711bc9`, `ebf8cb3` | different design (see section 4) |

### Conflicting semantics found (fixed in pr/cpu-fixes, with vendor objdump evidence)

- **E1F6 signed 64/32 division**: upstream decoded x bit 0 as signed and required
  an even destination field, so `E1F6 3620` (Felucca/Jangada/SLOOP formant engine)
  was rejected. Objdump: `e1f6 1040` = `r1_r0 = r5_r4 / r0 (s)`, `e1f6 0041` unknown.
- **ED5X halfword stores**: upstream took the store offset as `signed(h & 7, 3)`;
  objdump: `ed53 0f2b` = `h[r2+-6] = r0`, `ed55 0f2b` = `h[r2+506] = r0.h`.
- **Compare-branch immediates** `jae/jb/ja/jbe`: unsigned imm10 (SLOOP
  `fm1_delay_us`: `f9f1 81fc` = `if (r1 < 960)`); upstream sign-extended, so
  SLOOP waited forever.
- **E866/E868/E86C/E86D** offsets are signed (`e868 12fc` = `[r1+-4] += r2`).
- **EF00-EFFF memory masks**: signed six-bit offset and an xor row (`ef3f 0400` =
  `[r0+-4] |= 0x80000000`, `ef40` = `^=`); upstream decoded only part of the range.
- **NaN/Inf**: upstream faults on unordered float compares; we kept that policy
  for every new float form (our fork took the objdump `u` prefix as "unordered
  enters"; unmeasured, so not proposed).

## 2. Pull requests, order and status

All branches: authored `henri <***REMOVED***>`, trailers
`Co-Authored-By: Claude Opus 5.5` and `Claude-Session: ***REMOVED***`.
Each commit was checked out alone and passed `cargo fmt --check`,
`cargo test --release --features gui` and clippy with no warning that upstream
`main` does not already have (16 pre-existing). All six branches merged together
apply without conflicts and pass 285 tests (upstream `main`: 198).

```
origin/main 81b9ed9
 └─ pr/cpu-fixes          (3 commits)  ~/GitHub/fm1-emulator-pr-cpu
     ├─ pr/simd           (1)          ~/GitHub/fm1-emulator-pr-simd      optional
     ├─ pr/op-scan        (2)          ~/GitHub/fm1-emulator-pr-opscan
     └─ pr/ui-audio-knobs (5)          ~/GitHub/fm1-emulator-pr-ui
         ├─ pr/profiler-tools (2)      ~/GitHub/fm1-emulator-pr-tools
         └─ pr/web-midi       (3)      ~/GitHub/fm1-emulator-pr-web
```

Open them in this order; each later one is opened against `main` and says
"depends on #N" (or is opened after the previous merged). pr/ui-audio-knobs
needs pr/cpu-fixes only for its Felucca encoder test (the PRESETS path stores
with `EE53`); its code does not depend on it.

### PR 1: `pr/cpu-fixes` - Decode the remaining compiler forms of Felucca, Jangada and SLOOP

Commits: `044fe6c` Decode signed offsets and increment forms of memory accesses;
`e36dbe0` Complete compare-branches and conditional blocks; `139f08d` Execute
signed wide division, conversions and special-register frames.

**What.** About 40 encodings that faulted as unsupported or computed the wrong
address/condition: signed byte/halfword/word offsets and their pre/post-increment
forms (EE5X, EED1-5, ED5X stores and `.h` stores, EDD0-7, ECD9-ECDF, ECDE/EDDE/EEDE,
EDDC stores), register-pair forms (EC50 write-back, EC58-EC5F, EC5C), register
lists with `++` (EB1X/EB3X), stack words of r8-r15 (also in the block cache),
signed RMW and memory-mask offsets, unsigned compare-branch imm10, conditional
rows 9A/C2/EA, float rows of every register conditional block and compare-branch,
six-byte compare-branches skipped whole by a conditional block, E1F6 signed by
bit 12, E1C8 mode 1, sextra, sat16, the ftoi/ftou/ftof rounding modes and
binary16 conversions, 04C0-04FF/0480-04BF special-register masks (generalising
the measured 04E1/04E8/04E9 frames), E958/E950 masked frames, `trigger`,
`flush [rN]`.

**Why.** Upstream `main` cannot run Jangada (faults at `0xee53`, 39 M
instructions) or SLOOP (hangs in `fm1_delay_us` before its first interrupt), and
the op-scan (PR 3) finds reachable unsupported encoding groups (first word and
low nibble of the extension) in all five images we have: Felucca 0.9-beta 7,
Jangada 0.1-alpha 8, SLOOP 2.2 10, stock FM-1 28, Baud Girl FM-1_093 46. After
this PR: 0 in each.

**Tests.** `tests/compiler_forms.rs`, 35 tests with the halfwords, objdump text
and firmware address of each form. The published Felucca package test and the
official FM-1 package test (`FM1_STOCK_FWSC`) pass.

**Measured effect** (`examples/diagnose`, upstream time model, same host):

| Firmware | main @ 200 M | PR @ 200 M | main @ 2,000 M | PR @ 2,000 M |
|---|---|---|---|---|
| Felucca 0.9-beta | LCD 184762, 21004 frames | same | LCD 4235962, 241504 frames | same |
| Jangada 0.1-alpha | fault `0xee53` at 39.3 M | LCD 184762, 21003 frames | (fault) | LCD 4235962, 241503 frames |
| SLOOP 2.2 | 0 interrupts, LCD 0 (stuck) | LCD 168960 (splash), 18752 frames | 0 interrupts, LCD 0 | LCD 284160 (home), 239252 frames |
| stock FM-1 | LCD 0, 8165 frames | LCD 0, 8165 frames | (see section 5) | (see section 5) |

Felucca's counters are identical on both, which is the regression check. The 284160 / 4235962 LCD counts equal
our fork's own baselines.

**Risks.** Semantics come from the vendor objdump and the firmware C sources,
not hardware captures; the commit messages say so. Exceptional float values
still fault. `PushIrqFrame`/`PopIrqFrame`/`PushReti`/`PopReturnRegister` became
one mask loop (`PushSpecial`/`PopSpecial`), which changes their `step()` names.

### PR 2: `pr/simd` - Execute the packed 16-bit forms (optional)

Commit `50596d7`. E500, E541/E543, E551/E553, E404, E511+4k, E561+4k/E563+4k,
`src/simd.rs`, `tests/simd.rs` (6 tests, including a packed-table sine kernel
against its C interpolation). **Risk:** semantics are inferred (Blackfin
conventions: high lane first, ssat, Q15 x2); no published firmware uses them.
Offer it as a draft for a hardware probe; fine to drop.

### PR 3: `pr/op-scan` - Scan firmware for instructions the decoder does not cover

Commits `4743147` Describe decoded instructions for external tools
(`fm1_emu::describe`, `tests/describe.rs`); `b2192cf` Scan firmware...
(`examples/op_scan`, `examples/extract`, `scripts/op-scan.sh`,
`scripts/pi32-objdump.sh`, README section). Needs Docker and the JieLi
toolchain for objdump; nothing in CI runs it. Effect: the reachable-gap counts
above (before PR 1 / after PR 1). Risk: none for the emulator; `describe`
mirrors execution lengths and must be kept in step with new forms (its test
covers the lengths).

### PR 4: `pr/ui-audio-knobs` - Rotary encoders, MASTER, host audio and a sharp LCD

Commits: `dced9c3` Draw the LCD without dropping guest pixels; `ee9e708` Turn
the rotary encoders and MASTER from the panel; `4abeee2` Allow an instruction
clock override for real-time playback; `1eee61c` Play guest audio through the
host output device; `4027abd` Pass the instruction clock through the launcher.

**What.** `fm1_emu::encoders` drives the seven encoders' A/B contacts in the key
matrix (Felucca `FM1_ENC` wiring), each quadrature phase held until the guest has
scanned both columns three times (`Gpio::column_scans`); the worker advances it
between batches. Knobs turn by dragging around them, scrolling or keys; MASTER is
the ADC pot. `Bus::set_instruction_clock` issues one instruction per N Hz of
guest time instead of the firmware clock (toolbar, `--cpu-mhz`, `./emulator
--cpu-mhz`). `cpal` (in the `gui` feature) plays the ALNK0 stream at its real
level (24-bit, 2^23 full scale) and paces the worker by the playback queue.

**Tests.** LCD placement at 1x/1.5x/2x; circling gesture (6 clicks per quarter
turn, MASTER pinned at 1023, values reach the worker); a synthetic matrix scan of
one detent; worker pacing by the audio queue; 24-bit scaling and stall bound;
command line; instruction clock vs TIMER4. Ignored: with `FELUCCA_FWSC`, PRESETS
+2 redraws 686 LCD pixels.

**Measured effect.** At 24 MHz the Felucca and Jangada LCD counts after 200 M
instructions equal the fork's (4235962, 4211962). Headless at 24 MHz on this Mac:
Felucca 83%, Jangada 78%, SLOOP 77% of real time; `play_check` sessions 78-81%.
So audio still breaks up on upstream until his batching work lands (our fork's
interpreter does the same work about 3x faster: section 4).

**Risks.** A new dependency (cpal 0.15) and `libasound2-dev` on Linux CI (the
workflow change is in the PR). Not visually verified in this port (the GUI logic
is the fork's, verified there; the window was not opened here). The clock
override is opt-in; the default stays the firmware clock.

### PR 5: `pr/profiler-tools` - Script panel sessions and profile guest instructions

Commits `bf77446` Read sized function symbols from ELF images;
`030e57e` Script panel sessions and profile guest instructions.

`fm1_emu::player` (headless panel: keys, encoders, guest seconds by oscillator
ticks, fault trace), `fm1_emu::profile` (per-PC counts, per-function and
hot-range reports), `png`, `examples/play_check` (run/hold/release/turn/level/
wav/png/words/halves, `FM1_HOT=N`, `hot:on/off/print`, `peek`, `FM1_HOT_DUMP`),
`examples/preset_sweep`. `Bus::oscillator_ticks`, `Cpu::halted`,
`Cpu::in_interrupt`. Tests: `tests/elf_symbols.rs`, `tests/player.rs`, profile
unit test. Example: a SLOOP build holding a chord spends 62% in interrupt
handlers and 42% in `mix_block` (one 1456-byte range is 27%): the kind of data
his `PERFORMANCE.md` profiling step needs. Risk: low (additive).

### PR 6: `pr/web-midi` - USB-MIDI host and the web editor bridge

Commits `2db1b30` Encode and decode USB-MIDI event packets; `2871638` Let the
USB host act as a USB-MIDI host; `fda9c3d` Serve a firmware's web editor and
bridge it to USB-MIDI.

**What.** `usb_midi` codec (no deps). `Usb::enable_midi_host` (off by default:
CDC enumeration unchanged) finds the MIDI streaming endpoints, skips the CDC line
state without a console, reads iProduct, delivers `Bus::usb_midi_send` packets
under the console's NAK rules and collects MIDI IN. `fm1_emu::web` (new `web`
feature: tungstenite + zip; `gui` includes it, the core stays dependency-free)
serves `FIRMWARE-ui.zip` or `--ui DIR` on `127.0.0.1:8765`, injects a Web MIDI
shim, and bridges `/midi` WebSocket frames; Host and Origin must be loopback.
"Open editor" button and status line; `scripts/make-ui-sidecar.sh`.

**Tests.** Codec (10), USB-MIDI host unit tests (descriptor parse, both
directions, held packet, optional requests), web server/shim/WebSocket/hub,
editor sidecar handling. Ignored with `MIDI_FWSC` (24 MHz clock): host note-on
renders audio and the INFO SysEx gets its reply on Felucca 0.9-beta, Jangada
0.1-alpha and SLOOP 2.2; a WebSocket client plays a note and reads "FELUCCA
0.9-BETA" / "FELUCCA SLOOP 2.2".

**Risks.** A local network listener (loopback only, Host/Origin checked) and two
dependencies; it builds upon upstream's own CDC-OUT delivery. Firmware tests use
the 24 MHz clock to keep boot times short.

## 3. What was not prepared, and why

- **pr/real-firmware-boot**: nothing left to send. With PR 1, Felucca, Jangada
  and SLOOP boot to their home screens on upstream, and stock / Baud Girl
  device modelling upstream is ahead of ours (measured on hardware). The fork's
  boot logs (`SLOOP.md`, `BAUD-GIRL.md`, `PRESET-SWEEP.md`) are firmware notes,
  not emulator changes.
- **pr/idle-skip-smp**: the SMP half is upstream already (`9cbb326`). Skipping
  halted time needs to know the next device event; our fork has event-scheduled
  devices (`perf/event-devices`), upstream advances devices every instruction and
  plans its own batching. Better raised as an issue next to `PERFORMANCE.md`.
- **pr/usb-audio-host**: the UAC1 host is only useful with interrupt nesting
  (SLOOP/Melodee USB audio builds), and our nesting used the pre-ICFG priority
  model; upstream has since measured ICFG and does not nest. Needs a hardware
  capture of nested entry first. The SIMD half is PR 2.
- **Performance work** (decode cache, `run_steps`, event-scheduled devices, code
  cache): conflicts with upstream's decode table, block cache and JIT. Data point
  worth offering (section 5): at the same 24 MHz instruction clock the fork runs
  Felucca's first 200 M instructions in 3.15 s, the PR stack in 10.11 s, with
  the same LCD result.
- **midi-timing** (TRS UART model, onset probe, `examples/latency.rs`):
  overlaps upstream's measured `uart.rs`; revisit after PR 6.
- **Diagnose helpers** (`FM1_MEMWATCH`, `FM1_RAM`, `FM1_MMIO`, ...): written
  against fork internals (MMIO stats, event scheduler).
- **Firmware-side test** `90ba92f` (CIN 0xF SysEx bytes): tests a SLOOP firmware
  fix, not the emulator; dropped from PR 6.
- **Our NaN semantics** for float compares (objdump `u` prefix): unmeasured;
  upstream's explicit fault is kept.

## 4. Verification log (2026-10-05)

| Branch / commit | fmt | tests (release, gui) | new clippy |
|---|---|---|---|
| upstream `main` `81b9ed9` | clean | 198 passed | (16 existing) |
| `pr/cpu-fixes` `044fe6c` Decode signed offsets and increment forms of memory accesses | clean | 213 passed, 0 failed, 4 ignored | 0 |
| `pr/cpu-fixes` `e36dbe0` Complete compare-branches and conditional blocks | clean | 222 passed, 0 failed, 4 ignored | 0 |
| `pr/cpu-fixes` `139f08d` Execute signed wide division, conversions and special-register frames | clean | 233 passed, 0 failed, 4 ignored | 0 |
| `pr/simd` `50596d7` Execute the packed 16-bit forms | clean | 239 passed, 0 failed, 4 ignored | 0 |
| `pr/op-scan` `4743147` Describe decoded instructions for external tools | clean | 237 passed, 0 failed, 4 ignored | 0 |
| `pr/op-scan` `b2192cf` Scan firmware for instructions the decoder does not cover | clean | 237 passed, 0 failed, 4 ignored | 0 |
| `pr/ui-audio-knobs` `dced9c3` Draw the LCD without dropping guest pixels | clean | 234 passed, 0 failed, 4 ignored | 0 |
| `pr/ui-audio-knobs` `ee9e708` Turn the rotary encoders and MASTER from the panel | clean | 239 passed, 0 failed, 5 ignored | 0 |
| `pr/ui-audio-knobs` `4abeee2` Allow an instruction clock override for real-time playback | clean | 240 passed, 0 failed, 5 ignored | 0 |
| `pr/ui-audio-knobs` `1eee61c` Play guest audio through the host output device | clean | 243 passed, 0 failed, 5 ignored | 0 |
| `pr/ui-audio-knobs` `4027abd` Pass the instruction clock through the launcher | clean | 243 passed, 0 failed, 5 ignored | 0 |
| `pr/profiler-tools` `bf77446` Read sized function symbols from ELF images | clean | 244 passed, 0 failed, 5 ignored | 0 |
| `pr/profiler-tools` `030e57e` Script panel sessions and profile guest instructions | clean | 250 passed, 0 failed, 5 ignored | 0 |
| `pr/web-midi` `2db1b30` Encode and decode USB-MIDI event packets | clean | 253 passed, 0 failed, 5 ignored | 0 |
| `pr/web-midi` `2871638` Let the USB host act as a USB-MIDI host | clean | 256 passed, 0 failed, 7 ignored | 0 |
| `pr/web-midi` `fda9c3d` Serve a firmware's web editor and bridge it to USB-MIDI | clean | 268 passed, 0 failed, 8 ignored | 0 |
| all six merged together (scratch merge, not a branch) | clean | 285 passed | 0 |

Each row is that commit checked out alone (`cargo fmt --check`,
`cargo test --release --features gui`, `cargo clippy --release --features gui
--all-targets` compared by message and file with `main`). Ignored tests run
separately with firmware: `FELUCCA_FWSC` (published Felucca boot and note,
encoder test), `FM1_STOCK_FWSC` (official package), `MIDI_FWSC` (Felucca,
Jangada, SLOOP): all pass. `FELUCCA_ELF` was not run (no Felucca ELF here).

Firmware packages: `~/GitHub/fm1-firmware/` (felucca-0.9-beta, jangada-0.1-alpha,
sloop-2.2, FM-1 = stock FM-1_015, FM-1_093 = Baud Girl).

## 5. Stock and Baud Girl (diagnose, 3,000 M instructions)

| Image | branch | executed (both cores) | LCD pixels | audio frames | watchdog feeds |
|---|---|---|---|---|---|
| stock FM-1 | main | 5,852,495,259 | 3,001,440 | 351,165 | 988,024 |
| stock FM-1 | pr/cpu-fixes | 5,852,496,504 | 3,001,440 | 351,165 | 987,892 |
| Baud Girl FM-1_093 | main | 5,853,149,076 | 2,217,391 | 351,165 | 987,916 |
| Baud Girl FM-1_093 | pr/cpu-fixes | 5,853,213,203 | 2,217,391 | 351,165 | 987,932 |

Upstream already boots both images to their screens; the PR leaves the LCD and
audio counters unchanged. The total instruction count and watchdog feeds differ
slightly (+1,245 and +64,127 instructions out of 5.85 G), so some form whose
result the PR changes runs during these boots; which one was not traced. The
op-scan found 28 (stock) and 46 (Baud Girl) reachable encodings that `main`
cannot execute; they are on paths these 3,000 M instructions do not reach.

Speed on this Mac (Apple silicon, idle machine), Felucca 0.9-beta, first 200 M
instructions: fork `697fa61` 3.15 s (24 MHz model, LCD 4235962); upstream code
with all six PRs at a 24 MHz instruction clock 10.11 s (LCD 4235962, the same
guest result); upstream at its firmware clock 5.77 to 5.94 s (LCD 184762: less
guest time passes). The fork differs mainly by its event-scheduled devices
and batched run loop; which part buys how much was not profiled here. This is
the comparison to offer next to his batching plan.

## 6. Refresh before opening

1. `git fetch origin` in any PR worktree; if `main` moved, rebase the chain in
   order (`pr/cpu-fixes`, then the three on it, then the two on
   `pr/ui-audio-knobs`) and rerun `cargo fmt --check`,
   `cargo test --locked --release --features gui`, clippy, and
   `scripts/op-scan.sh` on the five packages.
2. `feat/boot-fixes` moved on after `697fa61` (another agent is merging
   `feat/a2asm-emu`, whose `f51017e` is already re-implemented in PR 1, and
   `fe2ac23` which merges simd): port any newer CPU fix into `pr/cpu-fixes`.
3. `feat/stock-works` (`~/GitHub/fm1-emulator-stock`): most of it duplicates
   upstream (`f7b9105` presets, `1e29802` SPI2 matrix); when it is done, rebase
   it on upstream `main` rather than offering it as is.
4. If upstream answers on the NaN policy or measures nesting, revisit the float
   `u` rows and the USB audio host.

## 7. Intro message to Simon (draft)

> Hi Simon, I'm Henri. I've been running Felucca, Jangada and SLOOP (a GPL
> Felucca fork I work on) in a fork of your emulator, and I've ported what we
> found to your current `main`, re-implemented on your decoder tables and the
> GUI worker. In order: (1) CPU fixes: about 40 compiler encodings, each with
> the vendor objdump text and the firmware address it came from; with them
> Jangada and SLOOP boot to their home screens on `main` and an objdump-driven
> scan finds no reachable unsupported forms in Felucca, Jangada, SLOOP, the
> stock image or Baud Girl. (2) That scanner as a tool. (3) Rotary encoders,
> MASTER and host audio in the window, with an opt-in instruction clock for
> real-time playback. (4) A headless session runner and instruction profiler.
> (5) A USB-MIDI host and a loopback bridge for the firmwares' web editors.
> Optional: the packed 16-bit forms (inferred semantics). We have no FM-1 to
> measure on, so nothing here has hardware captures; where your measured
> behaviour and ours differed, I kept yours (NaN faults, repeat counts) and say
> so in the commits. All of it passes fmt, the GUI test suite and clippy on
> each commit. Happy to split, reorder or drop anything, and to send them one
> at a time.

## 8. cliph's issue #1, and four more branches from the scene survey (2026-10-05, late)

### 8.1 Issue #1: "Felucca 1.0 / 1.0.1 stop at boot: two halfword encodings and USB EP4"

The issue was opened by cliph on 2026-10-05 and is still open. It was read
through the API only; nothing was posted.

- **No patch is public.** The issue has no comments, upstream has no pull
  requests and 0 forks, and cliph has no fork of the emulator. The issue
  says "I have the patch with tests on a branch and can open a PR". All we
  can compare is his description.
- **His three gaps:**
  1. `0xeddc` with `x & 15 == 1` is a store (`h[++r2=r11] = r0`, 6 words in
     the 1.0.1 image).
  2. `0xedd3 3fbf` is `h[r11++=-2] = r3`, with `h & 3` taken as the step's
     signed high bits.
  3. USB EP4 has its own CNT/TADR/RADR at 0x11834/38/3C. He notes that
     widening `endpoints` alone would make `send()` read EP0's address for
     EP4.

| cliph | Ours | Overlap / conflict |
|---|---|---|
| 1. EDDC `x&15 == 1` store | **pr/cpu-fixes** `044fe6c` decodes EDDC `x&15 == 1 \| 3`: the low-half store and the `.h` store. Vendor objdump: `eddc 4651` = `h[++r5=r6] = r4`, `eddc 4653` = `h[++r5=r6] = r4.h`. Test `halfword_register_preincrement_stores_the_low_or_high_half`. | Same semantics for `x&15 == 1`; objdump confirms his word `eddc 0b21` = `h[++r2=r11] = r0`. Ours also covers `x&15 == 3`. Both touch the same decoder row, so expect a textual conflict if both are merged. |
| 2. EDD3 signed step | **pr/cpu-fixes** `044fe6c` decodes EDD0–EDD7 whole: a signed ten-bit increment `(h&3):x[11:8]:x[3:1]`, and h bit 2 is a signed load **or a `.h` store**. Test `halfword_postincrement_immediates_are_signed_ten_bit`. | The step agrees (objdump `edd3 3fbf` = `h[r11++=-2] = r3`, `edd1` = +510, `edd2` = −258). **Semantic conflict:** he writes that "`h & 4` stays the load-only sign bit", but objdump decodes `edd5 3fbf` = `h[r11++=510] = r3.h` and `edd7 3fbf` = `h[r11++=-2] = r3.h`. With x bit 0 set (a store), h bit 2 selects the high half. If his patch stores the low half there, or rejects it, ours is the objdump reading. He also writes that he did not check the vendor disassembler. |
| 3. USB EP4 | **pr/usb-ep4** `d5c5102` (new, on origin/main): 5 endpoints, INDEX ≤ 4, count/tx_address/rx_address helpers, EP4 at 0x11834/38/3C. The mapping is from vendor WL82.h `JL_USB`, as well as Felucca's `fm1_usb.h`. The commit credits his report. | The same change. With pr/cpu-fixes + pr/usb-ep4, Felucca 1.0.1 at 300M gives **exactly his figures**: 535,200 LCD pixels, 21 watchdog feeds, 37 CDC bytes, 40 ADC conversions, 259 DMA halves. |

**Suggestion (user decides; nothing sent).** Before opening PR 1, comment on #1
so that our PRs and his don't land as duplicates. Draft:

> Thanks for the write-up. I have a branch series for Simon covering the
> same ground, checked against the vendor objdump: EDDC x&15 = 1 and 3 (the
> second is the `.h` store), EDD0–EDD7 with the ten-bit signed step, where
> bit 2 is a signed load or a `.h` store (objdump: `edd5 3fbf` =
> `h[r11++=510] = r3.h`), and EP4 with its own CNT/TADR/RADR. With them,
> 1.0.1 gives your exact 300M figures. Happy to rebase onto yours if you
> open the PR first, or to credit you in mine. Which do you prefer?

### 8.2 New local branches (not pushed)

All four are authored henri, with trailers `Co-Authored-By: Claude Opus 5.5`
and `Claude-Session: ***REMOVED***`. Each was built and
tested alone with `cargo fmt --check`, `cargo test --release --features gui`,
and clippy compared by message with its base: no new warnings.

| Branch | Worktree | Base | Commit | Tests |
|---|---|---|---|---|
| `pr/usb-ep4` | `~/GitHub/fm1-emulator-pr-ep4` | origin/main | `d5c5102` Model USB endpoint 4 with its own count and DMA registers | 199 (main 198) |
| `pr/perf-counters` | `~/GitHub/fm1-emulator-pr-perf` | origin/main | `371e143` Model the corex2 cycle counters and their DBG_CON enables | 200 |
| `pr/float-compare-branch` | `~/GitHub/fm1-emulator-pr-fcmp` | pr/cpu-fixes `139f08d` | `4e08e75` Compare floats in six-byte register compare-branches with x bit 7 | 234 (cpu-fixes 233); the new test fails without the change |
| `pr/divide-trap` | `~/GitHub/fm1-emulator-pr-trap` | pr/cpu-fixes `139f08d` | `6f29a14` Raise the CPU exception on a divide by zero when EMU_CON arms it | 238 |

**What each does:**

- **usb-ep4:** see 8.1.
- **perf-counters:** the corex2 block at 0x1EEE200 (per-core IF/RD/WR_UACNT
  and TL_CKCNT, 64-bit). TL_CKCNT counts issued cycles only while DBG_CON
  bits 0–2 (core 0) or 8–10 (core 1) are set: X0X measured it frozen at
  DBG_CON 0 on hardware. X0X 0.9 stopped reading 0x1EEE218.
- **float-compare-branch:** `FF4x` with extension bit 7 is `iff` (objdump
  `ff42 7380` = `iff (r7 u>= r3)`). It was executed as an unsigned integer
  compare, so X0X's limiter clipped silence to −0.78 (a constant −0.39 FS).
  Non-finite operands still fault, as in pr/cpu-fixes' conditional blocks.
- **divide-trap:**
  - With EMU_CON bit 2 armed, integer (E1F4/E1F6) and float (E53F op 3)
    divides by zero latch EMU_MSG bit 2 and enter vector 1 synchronously,
    with the FM-1_989 ICFG layout. They do so even inside handlers, repeat
    blocks and conditional blocks.
  - An armed trap with vector 1 disabled faults explicitly.
  - With the trap off, integer division still faults (upstream policy).
  - **Policy change to flag to Simon:** with the trap off, a finite float
    over zero gives the IEEE value instead of faulting. X0X needs this: its
    limiter divides 1 by a silent sample every sample (clang hoisted the
    guarded divide, `master_process` 0x0201fefc). The quotient is discarded
    and unmeasured.

**Conflicts with the existing chain**, checked on a scratch merge of
`test/upstream-plus-prs` + the four branches:

| Branch | Conflicts with | Where | Resolution |
|---|---|---|---|
| pr/usb-ep4 | pr/web-midi | `usb.rs` | Adjacent additions: the const next to `MIDI_RECEIVED_MAX`, the tests module, the helpers next to `send()`. Keep both; web-midi's MIDI paths also go through the helpers. |
| pr/perf-counters | pr/ui-audio-knobs | `clock.rs` (struct field next to `issue_override`; `instruction_ticks`, where `self.cycles += 1` goes before its override match), `lib.rs` (mod list) | Keep both. |
| pr/float-compare-branch, pr/divide-trap | — | — | Merge cleanly onto the chain. |

Either stack pr/usb-ep4 on pr/web-midi and pr/perf-counters on
pr/ui-audio-knobs, or rebase whichever lands second.

**Merged result:** 294 tests pass (the test branch has 285). Felucca 0.9 /
Jangada / SLOOP 2.2 counters at 300M are identical to the test branch's.
Felucca 1.0.1 reaches its console banner.

**X0X 0.9-beta on that merge** passes the perf counter, EP4 (once
pr/web-midi's CDC-less enumeration is in) and the limiter divide. It then
stops at **0x02005fc6, `ftou` of −32.3**: an out-of-range conversion, which
upstream faults on by policy. This is X0X's `disc()` anti-aliasing
(`ui.c`): `fm_clampf(r - d + 0.5f, 0, 1)` is guarded in C, but clang
converts before the lower-bound select and discards the result. It is the
same class as the hoisted divide. Whether upstream should give speculative
out-of-range conversions a value (our fork saturates, as Rust's `as` does;
hardware is unmeasured) is a question for Simon. No branch was prepared for
it.

### 8.3 Fork-only fixes that upstream already has

- **0x0800–0x0FFF loads/stores with a register post-increment** (X0X
  `0881` = `r1 = [r0++=r9]`): upstream decodes 0x0800/0x0C00/0x1000 already
  (decode.rs:330). Fork commit `d342c3c` on `feat/scene-fixes`.
- **Signed imm12 of the six-byte `==`/`!=`/signed compare-branches:**
  already in pr/cpu-fixes `e36dbe0`. The fork lacked it until `36e8336`.

### 8.4 One upstream test is vacuous

`stock_pixel_stores_advance_by_two_bytes_without_loading_the_buffer` ends
with `assert!(cpu(&[0xedd4, 0x00d3]).step().is_err())`. On pr/cpu-fixes it
fails only because r13 = 0 makes the store hit unmapped address 0. Objdump
decodes the word as `h[r13++=2] = r0.h`. Worth mentioning in PR 1, or
replacing with a positive test.

## 9. The merged emulator and six more branches (2026-10-06)

Our integration now sits on upstream: `feat/upstream-merge`
(`~/GitHub/fm1-emulator-merge`, HEAD `657caa5`), a real merge of upstream
`main` 81b9ed9 (through `test/upstream-plus-prs`, so it contains PRs 1-6),
then our remaining work re-ported onto upstream's code, then the four branches
of section 8 and `feat/scene-fixes`. Nothing pushed. Everything ours that
upstream lacks and that the merge touched now exists as a branch for Simon:

| Branch | Worktree | Base | Commits | Tests |
|---|---|---|---|---|
| `pr/idle-skip` | (scratch, removed) | pr/profiler-tools `030e57e` | `7759e99` Advance devices by their events and skip time cores spend in idle | 260 |
| `pr/nested-irqs` | (scratch, removed) | origin/main | `b881589` Allow interrupts to nest by priority as an opt-in | 201 |
| `pr/usb-audio-host` | (scratch, removed) | pr/web-midi `44b279e` | `dc6bfa3` (pr/nested-irqs' commit, re-applied), `e09e244` Let the USB host stream USB audio (UAC1) with a firmware | 272, 288 |
| `pr/uart-midi-in` | (scratch, removed) | pr/ui-audio-knobs `4027abd` | `38516e6` Receive MIDI on UART1 from the host | 246 |
| `pr/stock-usr-copy` | (scratch, removed) | origin/main | `438d497` Install a USR copied from another package where its CRC verifies | 200 |
| `pr/emu-tools` | (scratch, removed) | pr/idle-skip + merges of pr/usb-audio-host and pr/uart-midi-in | `dd8cc0a` Add diagnostics, latency measurement and more scripted panel steps | 308 |

Each commit was checked out alone: `cargo fmt --check`, `cargo test --release
--features gui` (counts above, 0 failed) and clippy compared by message with
upstream `main`: no new warnings.

**Order.** After PRs 1-6 (and section 8's four):
7. `pr/stock-usr-copy` (independent; Baud Girl's factory voices).
8. `pr/nested-irqs` (independent, opt-in).
9. `pr/uart-midi-in` (needs PR 4 only for its test's instruction clock).
10. `pr/idle-skip` (on PR 5: it uses `Bus::oscillator_ticks`, `Cpu::halted`).
11. `pr/usb-audio-host` (on PR 6 and 8).
12. `pr/emu-tools` (on 9, 10 and 11; its base is a local merge, to be
    rebased once those land).

**What each does** (details in the commit messages):

- **idle-skip** (the fork's perf/event-devices + feat/idle-skip, re-done on
  upstream's time model). Devices report the oscillator ticks until their
  next event and are otherwise advanced lazily (at the event, before every
  register write, on `Bus::sync`); counters read in between are computed
  for the current tick. `Cpu::run_steps` skips spans in which every issuing
  core is halted in `idle` with no deliverable interrupt and no event due;
  the clock's fractional phase advances exactly. Also inline SRAM/XIP reads
  (an XIP view rebuilt on every change, so reads stay live), cached guard
  limits and device clocks, and the Bluetooth clock-event index resolved at
  compile time (it was a linear search in every interrupt check). One
  semantic change to flag: RAND words become a hash of elapsed device time
  instead of one xorshift step per advance call (else lazy spans would change
  them). Guest-visible counters unchanged (section 9.2).
- **nested-irqs**: opt-in nesting by ICFG priority for firmware whose
  handlers re-enable interrupts (SLOOP USB audio). In this mode an entry
  clears the global enable so the handler's `sti` decides (the fork's
  pre-ICFG model); unmeasured, off by default; INTPRI is not written.
- **usb-audio-host**: the fork's UAC1 host (descriptor checks, feedback
  clock, isochronous IN/OUT per 1 ms frame) on upstream's USB host, off by
  default.
- **uart-midi-in**: bytes on UART1's RX line at 31250 baud into the RX DMA
  ring, RDC latch into HRXCNT, RX pending; no RX interrupt or OT timeout
  (unmeasured).
- **stock-usr-copy**: `5676bf5` from feat/stock-works: Baud Girl FM-1_093
  keeps FM-1_015's USR ciphertext at another package position; it is
  installed where its CRC verifies and the first voice name is printable.
  On upstream + PRs Baud Girl shows "001" with no voice name and its notes
  are silent; with this, "001 PIANO 1" and notes sound.
- **emu-tools**: diagnose switches (FM1_CPU_MHZ, FM1_IDLE_SKIP,
  FM1_NESTED_IRQ, FM1_WATCHDOG_OFF, FM1_RECENT, FM1_HOT/CALLERS, FM1_WATCH,
  FM1_MEMWATCH, FM1_MMIO, FM1_OP, FM1_DUMP/DUMPW, FM1_RAM, FM1_PNG/PNG_RAM),
  play_check `click`, `align`, `cores`, `master`, knob_check, and the latency
  tool with the audio onset probe.

### 9.1 What the merge did (resolution policy)

- Code tree = upstream + PR ports; upstream's measured modelling replaces our
  stubs: radio.rs -> wireless.rs, rng.rs -> RAND in devices.rs, spi2.rs ->
  shift_spi.rs, LRCT, PMU/ADC channels, husb (upstream answers 0x16800),
  JL_SRC (audio.rs answers 0x14300), register-arithmetic flags, repeat
  counts, interrupt deferral, idle wake latency (four slots: an idle loop
  needs four more instructions before it re-idles).
- Our extras re-ported on top: idle skip + event-scheduled devices (above),
  diagnose, optional nested IRQs, the USB audio host, TRS MIDI IN + latency
  tool, play_check steps, knob_check, the GUI's 192/312 MHz choices,
  `--cpu-mhz=N`, FM1_NESTED_IRQ in the window. From feat/stock-works: the USR
  relocation (`5676bf5`), play_check `master:` (`944a3e9`) and the
  compare-branch test of `8f83b49` (the fix itself is already in
  pr/cpu-fixes). Upstream already has its presets and SPI2 matrix.
- Dropped as superseded or unmeasured: our NaN "unordered" compare semantics
  (upstream faults; our tests now expect the fault), LCD log/owner tools,
  the SPI2/radio/HS-USB stub counters, our decode cache (upstream has its
  decode table and block cache).
- Not ported (follow-up): feat/stock-works `d35c232` spin-loop skip. It
  records loops through bus-access hooks and replays TIMER4/5 in closed form
  for the fork's integer timers; upstream's timers count fractional clocks,
  so the replay needs rewriting. Stock runs 0.05x real time without it
  (upstream: 0.04x).

### 9.2 Verification of the merged branch

- `cargo test --release --features gui`: 400 passed, 0 failed, 11 ignored;
  `cargo fmt --check` clean; clippy: the same warnings as upstream main.
  Ignored tests with firmware: FELUCCA_FWSC (boot + note, encoder),
  FM1_STOCK_FWSC (official package, GUI worker latency), MIDI_FWSC
  (SLOOP 2.2: host note-on, SysEx reply, WebSocket), USB_AUDIO_ELF (16/24
  bit sessions): all pass.
- Baselines, `diagnose` 200M:

  | Firmware | fixed 24 MHz clock (fork baseline) | firmware clock (= upstream+PRs) |
  |---|---|---|
  | Felucca 0.9-beta | LCD 4235962, 318151 frames | LCD 184762, 21004 frames, 4389 IRQs |
  | Jangada 0.1-alpha | 4211962, 318136 | 184762, 21003, 4380 |
  | SLOOP 2.2 | 284160, 315581 | 168960, 18752, 3877 |

  Under upstream's clock model fewer guest seconds pass per instruction
  (Felucca's system clock is about 420 MHz, SLOOP's 360 MHz), hence the
  smaller counts; `FM1_CPU_MHZ=24` reproduces the fork's numbers exactly.
- All 30 `fm1-firmware/sloop-*.fwsc` boot silent (rms 0.0000 over 0.5 s
  after 3 s) and play (held note rms 0.0297-0.0298; the SIMD probe build
  0.0595), at their firmware clock.
- `scripts/op-scan.sh` on the 38 packages in fm1-firmware: no reachable
  unsupported encoding, except one `vendor-unknown` word in Felucca 1.0
  (`fffe 0000 f491` at the first word of a RAM copy, 0x01c08000, which the
  vendor objdump does not decode either).
- Official firmware at its own clock (play_check: 8 s boot, hold key 26,
  PRESETS +1): stock FM-1_015 reaches "001 PIANO 1", the key plays 348.3 Hz,
  PRESETS +1 shows "002 ORGAN 1": identical instruction counts, audio and
  pitch to upstream+PRs. Baud Girl FM-1_093 reaches "001 PIANO 1", plays
  349.0 Hz and shows "002 ORGAN 1"; on upstream+PRs its note is silent
  (rms 0: its USR is not installed).
- Speed, play_check: a 2 s run after booting, guest seconds per host second
  (loaded machine; the fork's default clock is a fixed 24 MHz, the others'
  is the firmware clock):

  | Firmware | Clock | Fork (`7a220f4`) | Merged | Upstream + PRs |
  |---|---|---|---|---|
  | Felucca 0.9-beta | default | 3.43x (24 MHz) | 0.12x (firmware clock) | 0.07x |
  | Felucca 0.9-beta | 96 MHz | 0.78x | 0.45x | 0.24x |
  | sloop-drumkit | default | 4.34x (24 MHz) | 1.79x (firmware clock, idles) | 0.21x |
  | sloop-drumkit | 96 MHz | 3.10x | 1.89x | 0.55x |
  | stock FM-1_015 | default | n/a (24 MHz: watchdog expires) | 0.05x | 0.05x |
  | stock FM-1_015 | 96 MHz | 0.26x | 0.16x | 0.14x |

  The merged interpreter is upstream's (decode table, block cache, JIT);
  the remaining gap to the fork (about 1.5-1.7x at equal clocks) is the
  fork's per-PC decode cache and run loop. Measured on this workload,
  upstream's block cache + JIT costs time rather than saving it (Felucca at
  24 MHz: 5.85 s with them off, 6.4 s on), a data point for Simon's batching
  plan. The fork's spin-loop skip for stock is not ported (section 9.1).

**Launcher.** `jangada/scripts/emu.sh` runs the merged build with
`--emu ~/GitHub/fm1-emulator-merge/rust-emulator` (or
`FM1_EMULATOR_DIR=...`): it then passes `--cpu-mhz=N` (24 by default, a
multiple of 24), which the merged fm1-ui accepts; the window's clock menu
also offers "Firmware clock". For the firmware clock from the command line,
the launcher's `upstream` mode logic (no `--cpu`) is what applies.

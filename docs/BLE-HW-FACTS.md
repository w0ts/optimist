# BLE baseband hardware facts for route C (AC791N / WL82)

Written 2026-10-08 by the clean-room reader for route C of [BLE-MIDI-FEASIBILITY.md](BLE-MIDI-FEASIBILITY.md) §9.3:
our own GPL link layer on the JieLi AC791N (WL82) BLE baseband. The implementers must not read vendor code,
vendor IR or vendor disassembly. They work from the Bluetooth Core specification and this sheet.

Update 2026-10-08 (RF start-up preparation): §14 the VM record format (U3), §15 Optimist's and SLOOP 2.4's flash
use against the stock trims, §16 the RF start-up split into constants, stored trims and live calibration, §17 the
build-time capture of the long tables, §18 the hardware steps, §19 the method. Corrections in §1, §2.2 and §11:
stock V15's VM is 16 KiB at `0x093000–0x096FFF` in the emulator, not demo_ble's 64 KiB. **On a real FM-1 the VM
is at `0x0E8000` (§14.1, read on one unit); `0x093000–0x096FFF` is `FF` there.**

Update 2026-10-09: §20, which of §16's constants V15 keeps as tables and whether the update loader can keep them
from the user's own V15 at install.

Update 2026-10-09 (central role): §21, scanning, initiating, the master connection, running several link instances,
directed advertising and address resolution, from the vendor IR; with the list of what only hardware can settle.

This sheet contains facts only: addresses, bit fields, values, sequences, timing, interrupts, RAM layouts and
observed behaviour. It contains no vendor code or pseudo-code. Long vendor tables (AGC, RF-die LUT, BBP and Wi-Fi
analog initialization) are described by location, size and capture method, not transcribed.

## 0. Sources, tags and caveats

**Tags.** Every fact carries one tag:

| Tag | Meaning |
|---|---|
| **[M:t]** | measured: an emulator MMIO trace of a reference firmware (order, values, timing) |
| **[M:d]** | measured: a RAM or flash image taken from the emulator after the reference firmware ran |
| **[M:c]** | measured: the reference firmware's own console log (demo_ble only) |
| **[M:s]** | value seen: a constant, address or field offset read from the vendor binary or its debug info |
| **[I]** | inferred from names, structure or consistency; plausible, not proven |

Several facts are [M] for the *value* and [I] for the *meaning*; the text says which.

**Reference firmwares (oracles).**

- **Stock V15**: `fm1-firmware/FM-1.fwsc`, SHA-256 `db1642b2…`. Run unchanged in the emulator. It advertises as
  `FM-1_BLE` (§9.2 of the feasibility doc).
- **SDK demo_ble**: the AC79 SDK `apps/demo/demo_ble`, built unchanged (variant b0 of the size study, SDK tag
  `AC79NN_SDK_V1.2.13_2026-04-20` per its own boot log [M:c]), packaged for the FM-1 with
  `tools/fm1pkg_make.py` and the Optimist update loader. It boots in the emulator only with two experiment stubs
  (§13). Its application source is readable. It is the simpler oracle: BLE only, no classic Bluetooth.
- **Vendor libraries** (`btctrler.a`, `wl_rf_common.a`): read by the reader only, to name what the traces show.

**Emulator caveats.** The emulator is `fm1-emulator` with the trace hook on branch `feat/ble-trace` (§13). Its
wireless model stores configuration words and models only: the Bluetooth slot clock and its alarms, the eight slot
timers, the PLL lock comparator (thresholds measured on one FM-1) and the filter-calibration result (a fixed value).
It does **not** model the radio, the BLE engine's events or interrupts, the BBP calibration results or the RF-die
read-back. So:

- every value a firmware **writes** is genuine firmware behaviour [M:t];
- every value a firmware **reads** from the radio is a model output, and every write that depends on such a read
  (calibration results) shows the model, not a real FM-1;
- nothing after "advertising enabled" happens in the emulator: no packet is sent, no event interrupt fires. Connection
  facts (§7–§10) therefore come from the vendor binary [M:s]/[I], not from traces.

Nothing in this sheet ran on hardware.

---

## 1. Memory map

| Range | What | Tag |
|---|---|---|
| `0x10008`, `0x10010`, `0x10014` | system clock/power control words touched by the BT bring-up (§5.1) | M:t |
| `0x11900–0x1197C` | Wi-Fi/RF analog block ("WL analog"): PLL, filters, PA, DC/IQ trims; `0x11968` filter calibration, `0x11978` sample strobe/result | M:t, meaning I |
| `0x13B00`, `0x13B04` | random-number source; read 16–20 times at first boot when no MAC is stored, not read on later boots | M:t |
| `0x14000–0x14064` | wireless control: `0x14000` BT/RF enable, `0x14028–0x14034` RF-die SPI port, `0x14040–0x1405C` Wi-Fi clock/reset | M:t |
| `0x20000–0x201FF` | BR/EDR baseband and the 625 µs Bluetooth clock (`0x20000` control, `0x2001C/20/24` clock sample) | M:t |
| `0x28000–0x28054` | **BLE baseband registers** (§2.1) | M:t, M:s |
| `0x2FC00–0x2FCA0` | BT analog/configuration block (§2.2) | M:t |
| `0x2FC80`, `0x2FC84`, `0x2FCBC` | BLE enable, **baseband RAM base**, **baseband RAM end** | M:t |
| `0x2FD40–0x2FD60` | slot timer: 8 alarms in 625 µs slots (§10) | M:t |
| `0x2FD80–0x2FD9C` | BT AGC configuration and the AGC table port | M:t |
| `0x30000–0x31FFF` | Wi-Fi MAC/BBP window; `0x3101C` is the BBP byte-access command port; `0x30F00/04` radio config | M:t |
| SRAM `0x01C00000–0x01C7FFFF` | baseband RAM block (§4) lives in ordinary SRAM, anywhere the firmware chooses | M:d |
| Flash `0x093000–0x096FFF` | (emulator; **on an FM-1 the VM is at `0x0E8000`**, §14.1 [M:hw]) **stock V15**'s SDK "VM" key/value store: area A `0x093000`, area B `0x095000`, 8 KiB each, holding the RF trims (§11, §14). demo_ble runs a 64 KiB VM instead (`0x093000–0x0A2FFF`, 32 KiB halves) | M:d, M:c |
| Flash `0x0E9000` | BTIF sector: the generated BT (classic) address, as VM-format record 102 (§14.5) | M:d |
| Flash `0x0FF000` | "key_mac" sector, read for a factory MAC (all `0xFF` in the package, so the firmware generates one) | M:c |

All BLE baseband registers are accessed as 32-bit words [M:t]. The engine's per-link registers are **not** MMIO:
they are a 324-byte block in SRAM that the engine reads and writes by DMA (§3).

---

## 2. Register reference

Confidence: **H** value and meaning agree across stock V15, demo_ble and the vendor binary; **M** value certain, meaning
inferred; **L** only seen in the binary or meaning unknown.

### 2.1 BLE baseband (`0x28000–0x28054`)

| Addr | Access | Bits and meaning | Observed values | Conf. |
|---|---|---|---|---|
| `0x28000` | RW | bit0 engine enable (written 1 first) [M:t]; bit13, bit14 set during init (meaning unknown) [M:t]; bit4 cleared at init [M:s]; bit6 = engine soft reset (set by the vendor's RF reset routine) [M:s]/[I]; bit2 = hold while a fixed test channel is programmed (set, 10 µs, clear) [M:s]/[I] | `1` → `0x2001` → `0x6001` [M:t] | M |
| `0x28008` | RW | [9:0] = 70, [15:12] = 0xF | `0x0000F046` [M:t] | M |
| `0x2800C` | RW | [9:0] = 70. Software subtracts it from 625 when computing the bit offset inside a slot, so it is a timing offset in µs (TX or RX path delay) [M:s]/[I] | `0x46` [M:t] | M |
| `0x28010` | RW | [7:0] = 62, [15:8] = 62 | `0x3E3E` [M:t] | L |
| `0x28014` | RW | [7:0] = 30. Also subtracted from 625 in the same bit-offset computation [M:s]/[I] | `0x1E` [M:t] | M |
| `0x28018` | RW | [7:0] = 20, [15:8] = 20 | `0x1414` [M:t] | L |
| `0x2801C` | W | **link-column command**: `(column << 10) | (link << 4) | op`; op 5 = write the data word to the column, op 2 = read the column into `0x28024`. Columns 0–16 exist (§2.3) | `0x0005 … 0x4005` [M:t] | H |
| `0x28020` | W | link-column data word for op 5 (write it before `0x2801C`; the vendor issues a `csync` between the two) | see §2.3 | H |
| `0x28024` | R | link-column read result for op 2 | — | M |
| `0x28028` | RW | interrupt enables per link n: bit n = **event** interrupt (IRQ 45), bit 8+n = **RX** interrupt (IRQ 29) | `0` → `0x101` when advertising starts [M:t] | H |
| `0x2802C` | RW | interrupt acknowledge per link: write 1 to bit n (event) or bit 8+n (RX) to clear the pending flag; also written with the enable bits just before enabling [M:s]/[I]. Read-back semantics unknown | `0x001`, `0x101` [M:t] | M |
| `0x28030` | R | pending flags: bit n event, bit 8+n RX. A source is serviced when (`0x28028` AND `0x28030`) has its bit [M:s] | — | M |
| `0x28034` | RW | bit0 = 1, [15:8] = a parameter whose meaning is unknown; another runtime path rewrites [15:8] | stock `0x0A01`, demo_ble `0x0901` [M:t] | L |
| `0x28038` | RW | bit1 = engine busy (software polls it to 0 after disabling a link) [M:s]; bit7 = a global flag checked at the end of the event ISR, acknowledged by writing bit6 [M:s]. Read once, value 0, during advertising set-up [M:t] | — | M |
| `0x2803C–0x28048` | RW | used only by the vendor's low-power suspend/resume and multi-link distance code | not touched by either oracle at boot [M:t] | L |
| `0x2804C` | RW | second interrupt group, per link: bit n enable, bit 8+n acknowledge-or-enable (written 1 on enable and to acknowledge) [M:s]/[I]. Serviced inside the IRQ 45 handler. Called "anchor-count timeout" / "primary event end" by the vendor [M:s] | `0x100` → `0x101` [M:t] | M |
| `0x28050` | R | second-group pending flags (bit n) | — | M |
| `0x28054` | R | read only by the vendor's low-power code | — | L |

### 2.2 BT analog and configuration block (`0x2FC00–0x2FCBC`, `0x2FD80–0x2FD9C`)

Values are the first-boot values of stock V15; demo_ble writes the same values except where noted [M:t].

| Addr | Write order and value | Notes | Conf. |
|---|---|---|---|
| `0x2FC40` | RMW set bit10 → `0x400`; later `0xFFFF`, then `0xFCFD` (bits 1, 8, 9 cleared) | BT block enable/config [I] | M |
| `0x2FC78` | `0x0F`, then RMW `0x3FF` | [I] | L |
| `0x2FC88` | a **SRAM address**: base of an 81-entry × 12-byte PLL channel table (§2.4) | V15 `0x01C09994`, demo `0x01C03038` | H |
| `0x2FD98` | `0` | resets the AGC table write index | H |
| `0x2FD9C` | 128 writes | **AGC table** data port: 64 entries × 2 words. Identical in V15 and demo_ble [M:t]. Not transcribed: capture with the trace hook (§13) | H |
| `0x2FC20` | `0`, later `0x36555537` | [I] AGC/analog | L |
| `0x2FD80` | `0x000F0000`, later `0x000F0001` (twice) | AGC enable in bit0 [I] | L |
| `0x2FD84`, `0x2FD88`, `0x2FD8C` | `0x1872BF14`, `0x1872BF55`, `0x1872BF00` | AGC thresholds [I] | L |
| `0x2FD90`, `0x2FD94` | `0`, `0` | | L |
| `0x2FC7C` | RMW bit0 → `1` | | L |
| `0x2FC48` | `0xB9`; later `0xAD` (after the BR/EDR baseband init) | | L |
| `0x2FC00` | `0x00FFD144` | | M |
| `0x2FC04` | `0x4E143CDF` | | M |
| `0x2FC08` | `0x03100000`; [9:0] and [19:10] are trim fields (I and Q offsets) rewritten from calibration results [M:s]; in V15's second boot they come from BBP read-backs, not the VM (§16.2) [M:t] | 0 in the emulator | M |
| `0x2FC0C` | `0x80808080` | | M |
| `0x2FC10` | `0x80008080`, then three RMW passes → `0x80000000`; bytes [7:0], [15:8], [23:16] are trim fields rewritten from calibration results [M:s]; in V15's second boot they come from BBP read-backs, not the VM (§16.2) [M:t] | emulator values | M |
| `0x2FC14` | `0x80` | | L |
| `0x2FC18`, `0x2FC1C` | `0`, `0` | `0x2FC18` is also written by a frequency-offset adjustment routine [M:s] | L |
| `0x2FC24` | `0x00034041` | | L |
| `0x2FC28` | `0x0005C000` | | L |
| `0x2FC98` | built by RMW to `0x02360236` | constant, not calibration-dependent [M:s] | M |
| `0x2FC9C` | built by RMW to `0x36360008` | constant [M:s] | M |
| `0x2FCA0` | built by RMW to `0x00003636` | constant [M:s] | M |
| `0x2FC44` | a SRAM address (`0x01C0A05C` in V15): BR/EDR exchange memory base | not needed for BLE-only [I] | M |
| `0x2FC80` | RMW bit0 → `1`, immediately before `0x28000 = 1` | BLE block enable [I] | H |
| `0x2FC84` | **baseband RAM base** (SRAM address of §4) | V15 `0x01C26480`, demo `0x01C10680` | H |
| `0x2FCBC` | **baseband RAM end** = base + block size | V15 `0x01C272E2` (3,682 B), demo `0x01C114FE` (3,710 B) | H |

### 2.3 Link columns (through `0x2801C/20/24`)

Each link n (0 for a single peripheral) has 17 column registers reached through the command port. All values are
16-bit unless noted [M:t, M:s]. Column meanings are inferred from the vendor's names for the writers [I]; the values
are measured.

| Col | Meaning [I] | Format | Observed |
|---|---|---|---|
| 0 | anchor slot counter, low 16 bits (read: probably slots until the next event, §21.5) | 625 µs slots | written 0 (twice) at start [M:t] |
| 1 | event interval, low 16 bits | 625 µs slots; advertising: `advInterval`; connection: `connInterval × 2` | stock `0x00A0` (100 ms), demo `0x0320` (500 ms) [M:t] |
| 2 | link state and latency | `state << 12 | latencyEnable << 11 | latency` (§2.5 for states) | advertising `0x2000` [M:t] |
| 3 | connection event counter | software reads it and subtracts 1 to get the current event counter; written 0 at connection set-up [M:s] | — |
| 4 | transmit-window offset | `0x8000 | slots`; 0 = none [M:s] | written 0 at connection set-up |
| 5 | instant | the event counter at which a parameter or channel-map update takes effect [M:s] | — |
| 6 | channel selection | advertising: `0x4000 | hop << 8 | flags`; fixed channel (test mode): `0x2000 | channel`; connection: `0x8000 | hopIncrement << 8 | hopIncrement` | advertising `0x41A5` (first channel 37, hop 1, all three channels) [M:t] |
| 7 | bit offset inside the slot (µs, 12-bit signed) and bit15 = widening-drift enable | | written 0 at start [M:t] |
| 8 | advertising random delay (advDelay) | `0xC000` = on, 0 = off | `0xC000` [M:t] |
| 9 | latency status | bit0 = set to 1 at the end of the initiating state (written right after the initiating link starts, §21.3); bit6 = set when slave latency is enabled; [15:7] = latency counter (read) [M:s] | — |
| 10 | extended-mode bits | bit6 = channel selection algorithm #2 for this link; bits 8–9 = link timeout interrupt enable [M:s] | — |
| 11, 13 | extended advertising / PHY | not used for a 1M legacy peripheral | — |
| 12 | — | never written by either oracle | — |
| 14 | anchor slot counter, high bits | `0x8000 | (slots >> 16)` = **start**; 0 = **stop** (this is how a link is disabled) | `0x8000` [M:t] |
| 15 | event interval, high bits and event enable | `eventEnable << 15 | (slots >> 16)` | `0x8000` [M:t] |
| 16 | widening-drift slot count | | — |

Reading columns 0 and 14 with op 2 gives a 24-bit running slot count (col 0 low 16 bits, col 14 bits [7:0] as bits
[23:16]) [M:s]; the vendor uses it as "the link's clock".

### 2.4 PLL channel table (SRAM, pointed to by `0x2FC88`)

81 entries of 3 words, one per 1 MHz channel [M:s]. In stock V15 after boot, entry i = `{i | i << 8, 0, 0}` for
i = 0…80 [M:d]. The vendor's library has a second pattern with words 1 and 2 = `0xFFFFAAAB` [M:s]; V15 uses the
zero form. Meaning of the words: unknown [I: per-channel VCO/divider config the engine fetches by DMA].

### 2.5 Link states (column 2 bits [14:12])

| Value | State [M:s names] | Notes |
|---|---|---|
| 1 | scanning | |
| 2 | advertising | used by both oracles [M:t] |
| 3 | initiating | |
| 6 | central (master) connection; also used by the direct test mode transmitter | |
| 7 | peripheral (slave) connection | the state for an FM-1 BLE-MIDI link |

---

## 3. The per-link control block (`ble_param`, 324 bytes, in SRAM)

The engine reads and writes this block by DMA. Field names come from the vendor's debug info [M:s]; offsets are
identical in both oracles [M:d]. All TX/RX pointer fields are **16-bit byte offsets from the baseband RAM base**
(`0x2FC84`), not addresses: an offset must be below 65,535 [M:s].

| Off | Field | Size | Meaning and known bits | Stock V15 while advertising [M:d] |
|---|---|---|---|---|
| 0x000 | ANCHOR | 16 | bit15 set at connection set-up; low bits = "dead time" 4 for the 1M PHY [M:s] | `0000` |
| 0x002 | BITOFF | 16 | 0 at init | `0000` |
| 0x004 | ADVIDX | 16 | [15:14] = advertising PDUs per event (3 = channels 37, 38, 39); [13:0] = spacing between those PDUs in µs | `C4E2` (3, 1,250 µs); init value `D388` |
| 0x006 | FORMAT | 16 | bit2 set for advertising; bit3 set when the local address is programmed; bit8 = ignore SCAN_REQ; bits 10–15 select test-mode payload generators (`0x0C00`, `0x4C00`, `0xCC00`, `0x2C00`) [M:s]/[I] | `000C` |
| 0x008 | OPTCNTL | 16 | init `0x28`, then bit9 set → `0x228`; bit3 = remote-address match disable, bit4 = local-address match disable (cleared to enable matching); connection set-up: bit2 set, bit9 cleared, bits 10–12 set [M:s] | `0228` |
| 0x00A | BDADDR0 | 16 | **access address** bits [15:0] | `BED6` |
| 0x00C | BDADDR1 | 16 | access address bits [31:16] | `8E89` (AA `0x8E89BED6`) |
| 0x00E | TXTOG | 16 | bit0 = the buffer the engine is on / takes next; engine-owned, software only reads bit0 (§8.2) [M:s]/[I]; bit1 cleared at connection set-up; bits 1–3 engine state, unused by software | `0000` |
| 0x010 | RXTOG | 16 | bit0 = which RX buffer the engine fills next [I]; **the filled RX buffer is the one RXTOG has moved PAST** (advertising: 54 of 54 packets on an FM-1, §8.1) [M:hw] | `0000` |
| 0x012 | TXPTR0 | 16 | offset of TX buffer 0 **payload** | `08BC` |
| 0x014 | TXPTR1 | 16 | offset of TX buffer 1 payload | `090C` |
| 0x016 | TXAHDR0 | 16 | header byte 0 for TX buffer 0: [3:0] PDU type, bit4 TxAdd, bit5 RxAdd [M:s]/[I] | `0000` (ADV_IND, public) |
| 0x018 | TXAHDR1 | 16 | same for buffer 1 | `0004` (SCAN_RSP) |
| 0x01A | TXDHDR0 | 16 | data header for buffer 0: [1:0] LLID, bit2 SN (kept by the engine; software preserves it), bit3 MD, [15:8] payload length [M:s]/[I] | `2200` (length 34) |
| 0x01C | TXDHDR1 | 16 | same for buffer 1 | `1000` (length 16) |
| 0x01E | RXPTR0 | 16 | offset of RX buffer 0 payload | `02DC` |
| 0x020 | RXPTR1 | 16 | offset of RX buffer 1 payload | `0400` |
| 0x022 | WINCNTL0 | 16 | receive window, µs, bits [15:0] | `0032` (50) |
| 0x024 | WINCNTL1 | 16 | receive window, µs, bits [31:16] | `0000` |
| 0x026 | RXAHDR0 | 16 | received header byte 0, buffer 0: [3:0] type, bit5 ChSel, bit6 TxAdd, bit7 RxAdd (Core spec layout) [M:s] | — |
| 0x028 | RXAHDR1 | 16 | same, buffer 1 | — |
| 0x02A | RXDHDR0 | 16 | received data header, buffer 0: [1:0] LLID, bit2 NESN, bit3 SN, bit4 MD, [15:8] length (Core spec layout) [M:s] | — |
| 0x02C | RXDHDR1 | 16 | same, buffer 1 | — |
| 0x02E | CHMAP0 | 16 | channel map bits 0–15 | `FFFF` |
| 0x030 | CHMAP1 | 16 | channel map bits 16–31 | `FFFF` |
| 0x032 | CHMAP2 | 16 | [4:0] channel map bits 32–36; [15:5] number of used channels | `04BF` (37 used) |
| 0x034 | LASTCHMAP | 16 | index of the channel of the last received packet (engine-written; software copies it as the RX channel) [M:s] | `0000` |
| 0x036 | RXMAXBUF | 16 | maximum RX payload | `00FF` |
| 0x038 | RXSTAT0 | 16 | RX status, buffer 0: [3:0] = 1 means a good packet; bit2 and bit3 = two error kinds (the packet is then treated as length 0, invalid); bit8 = the peer acknowledged our last TX [M:s]/[I]. On an FM-1: [3:0] = 1 on every good packet (470 in a connection, 0 errors), `0x9805` (bit2) on a dropped SCAN_REQ, bits 15 and 12 always set, bits 11:10 cycling 1 → 2 → 3 between fills while advertising (§8.1) [M:hw] | — |
| 0x03A | RXSTAT1 | 16 | same, buffer 1 | — |
| 0x03C | FILTERCNTL | 16 | bit0 whitelist enable, bit3 scan-request filter, bit4 connect-request filter, bit8 whitelist address type [M:s]/[I] | `0000` |
| 0x03E | WHITELIST0L/M/U | 3×16 | one whitelist/target address, little-endian 16-bit halves | `0` |
| 0x044 | CRCWORD0 | 16 | CRC init bits [15:0] | `5555` |
| 0x046 | CRCWORD1 | 16 | CRC init bits [23:16] in [7:0] | `0055` |
| 0x048 | WIDEN0 | 16 | window widening, whole 625 µs slots | `0000` |
| 0x04A | WIDEN1 | 16 | `0x5000 | (widening mod 625)` in µs | `51E7` (init value) |
| 0x04C | TARGETADRL/M/U | 3×16 | peer address (set from SCAN_REQ/CONNECT_IND or for directed modes) | `0` |
| 0x052 | LOCALADRL/M/U | 3×16 | own device address, little-endian halves | `<address>` (6-byte device address, little-endian 16-bit halves) |
| 0x058 | EVTCOUNT | 16 | event counter of the last RX (engine-written); zeroed at connection set-up | `0000` |
| 0x05A | RXBIT | 16 | | `0000` |
| 0x05C | IFSCNT | 16 | inter-frame spacing control [I: T_IFS] | `8295` (constant) |
| 0x05E | RFPRIOSTAT | 16 | radio-arbitration priority (shared radio with BR/EDR and Wi-Fi) | `001A` (26) |
| 0x060 | RFPRIOCNTL | 16 | same; values 17, 19, 26, 28, 30 used by the vendor; init `0x101` | `001A` |
| 0x062 | INTFRAME | 16 | bit1, bit2 set at init; bit3 = TX disable [I]; bits [5:4] = `01` in a connection; bit6 = MD of the PDU in the TX buffer | `0006` |
| 0x064 | TXBUF0CNTL | 8 | bit0 = "empty" (vendor debug name `empty0`): 1 = empty / the PDU software put there is finished, 0 = software handed a PDU to the engine; software sets 1 only at connection set-up and clears it last when loading; the engine sets it back to 1 (§8.2) [M:s]. The 1 = "loaded" reading of §8.1 is withdrawn | `00` |
| 0x065 | TXBUF1CNTL | 8 | same, buffer 1 | `00` |
| 0x066 | RXBUF0CNTL | 8 | bit0: 1 = engine filled the buffer, 0 = armed by software [I]. **On an FM-1 not a "filled" flag while advertising: it stays 00** (54 RX IRQs) [M:hw]; in a connection bit0 = 1 did mark the filled buffer (470 packets, no desync) [M:hw] (§8.1) | `00` |
| 0x067 | RXBUF1CNTL | 8 | same, buffer 1 | `00` |
| 0x068 | FRQ_IDX0 | 40 B | channel index i at [i], i = 0…39 | `00 … 27` |
| 0x090 | FRQ_IDX1 | 40 B | used data channels, packed (remapping table), then 37–39 at [37…39] | |
| 0x0B8 | FRQ_TBL0 | 40 B | RF frequency of channel index i as **MHz − 2402** (data channels: 2,4,…,22,26,…,76; [37]=0, [38]=24, [39]=78) | measured, see §6 |
| 0x0E0 | FRQ_TBL1 | 40 B | frequencies of FRQ_IDX1, same encoding | |
| 0x108 | EXTPHYDRF0/1, EXTIFSDRF0/1, EXTPKTDRF | 5×16 | 2M/coded PHY, unused for 1M | `0` |
| 0x112 | WINCNTL2 | 16 | normal receive window, µs | `0032` (50) |
| 0x114 | EXTCTL0/1, EXTADI | 3×16 | extended advertising | `0` |
| 0x11A | (unused) | 6 B | | |
| 0x120 | TX_PWER | 32 | TX power = level + 16 (level 0 … the board maximum) [M:s] | `00000016` (level 6) |
| 0x124 | RX_GAIN0 | 32 | | `0` |
| 0x128 | TX_SET | 32 | written with the same value as TX_PWER | `00000016` |
| 0x12C | RX_SET | 32 | | `0` |
| 0x130 | PLL_COMP | 32 | carrier-offset compensation = −(offset × 2^20) / 24,000, offset from the stored frequency-offset record (VM 110) [M:s] | `0` |
| 0x134 | MDM_SET | 32 | | `0` |
| 0x138 | RSSI0–RSSI3 | 4×16 | engine-written RSSI words. RSSI in dBm = 21 − (RSSI2 & 63) − T[(RSSI2 >> 8) & 63], T a 64-entry gain table (not transcribed; §12) [M:s] | `0` |
| 0x140 | ANL_OUT | 32 | | `0` |

Decoded dumps of both oracles: [ble-traces/v15-ble_param.txt](ble-traces/v15-ble_param.txt),
[ble-traces/demo_ble-ble_param.txt](ble-traces/demo_ble-ble_param.txt).

---

## 4. Baseband RAM layout

The firmware allocates one contiguous SRAM block, zeroes it, and programs `0x2FC84` = start and `0x2FCBC` = start +
size [M:t, M:d]. Everything the engine touches by DMA (link control blocks, TX and RX payload buffers) must lie
inside it, because the engine addresses it with 16-bit offsets [M:s].

| Offset | Size | Content | Stock V15 [M:d] |
|---|---|---|---|
| 0x000 | 16 B | **instance table**: 8 × u16, entry n = `0x8000 | offset of link n's control block`; 0 = link unused. The vendor's routine that writes it is named "hw inst set" [M:s]; that the engine reads it is [I] | `8040 0000 …` (link 0 at +0x40) |
| 0x010 | 20 B | software pointers and counters (not engine-visible) [I] | |
| 0x024 | 28 B × links | software per-link records [I] | |
| 0x040 | per link: 324 B control block (§3) + software state | link entity | V15 616 B, demo_ble 636 B per link (library versions differ) |
| after the links | `rx_count × 294 + 35` B | RX payload buffers | V15 1,501 B at +0x2A8 |
| then | rest | TX payload buffers | V15 1,501 B at +0x885 |

- Each RX or TX buffer is a 20-byte software header followed by the payload; the TX/RX pointer fields point at the
  **payload**, 20 bytes into the buffer [M:s, M:d]. Two RX and two TX buffers per link alternate (ping-pong) [M:s].
- Advertising RX buffers are allocated with room for 263 payload bytes, connection RX buffers 255 [M:s].
- The advertising TX buffers hold AdvA (6 bytes) followed by AdvData or ScanRspData [M:d]; the header and length are
  not in the buffer but in TXAHDR/TXDHDR [M:d, M:s].
- At connection set-up the two advertising TX buffers are reused as the connection's two fixed TX buffers [M:s].
- Measured sizes: V15 block 3,682 B at `0x01C26480`; demo_ble 3,710 B at `0x01C10680` [M:t].

---

## 5. Init sequence

Order measured in stock V15's first boot [M:t]; demo_ble follows the same order with the same values unless noted.
Times are emulated guest time and only show order and relative length.

### 5.1 Clocks and power (t ≈ 0.0002–0.038 s, then 0.159 s)

1. `0x10010`: bit16 set then cleared, bit10 set → `0x400` (system clock) [M:t].
2. Wi-Fi clock/reset block: `0x14040–0x14050` cleared; `0x14040` built up to `0x14F`; `0x1405C` → `0x47000005`;
   `0x1404C` = `0x01000000` [M:t].
3. Before the BT block (t = 0.159 s): `0x10010` bits 14–15 cleared (BT domain power/reset; set again when BT closes)
   [M:s]; a 240-unit delay [M:s]; `0x14000` bit7 set, bits [20:16] = `0x0C`, then bit0 set → `0x000C0081` [M:t];
   delays between steps [M:s].
4. `0x2FC40` bit10 set; `0x20000` = `0xC0000000`; `0x2FC40` = `0xFFFF` then `0xFCFD`; `0x2FC78` = `0x0F` then `0x3FF` [M:t].

### 5.2 RF analog: the Wi-Fi front end comes first (t ≈ 0.038–0.155 s)

The BT RF init calls the shared Wi-Fi RF init before touching anything Bluetooth-specific [M:s], and the trace
shows all Wi-Fi activity before the first BT write [M:t]. It cannot be skipped by a BLE-only firmware [M, size study].

| Step | Registers | Volume (first boot) | Constant or data-dependent |
|---|---|---|---|
| radio config + BBP/MAC init | `0x30F00/04`, `0x30308–0x31348`, BBP port `0x3101C` | ≈3,300 BBP byte writes | constant table [I]; capture only |
| Wi-Fi analog init | `0x11900–0x11964` | ≈250 writes | mostly constant; some fields from calibration |
| **PLL VCO bank scan** | `0x11934`, `0x11938`, `0x1193C`, sampled through `0x11968`/`0x11978` | ≈1,560 + 3,260 accesses | **data-dependent** (loop length depends on the comparator) |
| filter / DC / IQ / TX-LO calibration | `0x11968`, `0x11978`, `0x1191C`, `0x11920`, BBP `0x3101C` | ≈96,000 BBP writes, 65,000 sample strobes | **data-dependent** |
| RF-die LUT load | SPI port `0x14028` (control) / `0x1402C` (data) | 2 × 256 words | constant table, except entries 128–255 patched with DC-trim and PA results [M:s] |

RF-die SPI port protocol [M:s, M:t]: wait until `0x14028` bits [22:20] read 0 (writing 0 to `0x14034` while waiting);
write the data word to `0x1402C`; write `0x14028` = `0x10000 | address << 8 | command`; then toggle `0x14028`
bit4 five times high and five times low (the "kick"). Commands: `0xF` register write, `0x6` register read (result in
`0x1402C`), `0xE` LUT word 0 write, `0xD` LUT word 1 write. Stock V15 uses only commands `0xE` and `0xD`, 256
addresses each [M:t]. Its LUT equals demo_ble's in all but one address (`0x73`) [M:t].

BBP byte port `0x3101C` [M:t, emulator model]: bit17 = start, bit16 = read, [15:8] = BBP register, [7:0] = data.

### 5.3 Calibration and the stored-trim shortcut (measured)

Both oracles store their calibration results in the flash VM and reuse them on the next boot [M:c, M:t]:

| | Stock V15 boot 1 | Stock V15 boot 2 | demo_ble boot 1 | demo_ble boot 2 |
|---|---|---|---|---|
| RF init duration (emulated) | ≈117 ms | ≈17 ms | ≈110 ms | ≈16 ms |
| BBP port `0x3101C` accesses | 340,806 | 31,152 | — | — |
| sample strobes `0x11978` | 65,480 | 3,850 | — | — |
| VCO bank scan (`0x11938`) | 3,270 | 3,258 (**still runs**) | runs ("bank: 37 … setbak: 36") | runs |
| RF-die LUT load | yes | yes | yes | yes |
| VM records written | yes | none | 106, 107, 108, 113, 109, 187 | none |

Facts behind the table: demo_ble logs `vm_write idx:187 … idx_len:0x48` about 1 s after boot (72-byte RF init
record) and nothing on the second boot [M:c]. Stock V15 writes VM records in the same format at the same flash
address (§11) [M:d]. On the second boot the VCO bank scan and the RF-die LUT load still run [M:t].

Not known: whether the stored record is enough on real hardware, or whether the live VCO scan is the part that
matters. See §12. Which write comes from which stored byte, and which stays live on the second boot: §16.

### 5.4 BT analog block (t = 0.159 s, before the BLE baseband)

Order [M:t]: §5.1 step 4 → `0x2FC88` (PLL table pointer, table filled in SRAM before) → AGC table (`0x2FD98` = 0,
128 words to `0x2FD9C`) → AGC config (`0x2FC20`, `0x2FD80–0x2FD94`, `0x2FD80` bit0, `0x2FC7C`) → `0x2FC48` →
`0x2FC00–0x2FC28` (§2.2) → stored-trim RMW of `0x2FC10` and `0x2FC08` → `0x2FC98`/`0x2FC9C`/`0x2FCA0` → BR/EDR
baseband (`0x2FC44`, `0x2000C … 0x20168`, `0x20000` = `0x107` then `0x80000107`) → slot timer `0x2FD40` = `0xFF00`.

The demo logs at this point [M:c]: `BT_TX_IQ: 0 0 0`, `BT_OF: 0 0` (BT IQ and offset trims, zero in the emulator),
`PA_C_I: 1,7,4,7,11,1,7`, `xosc_l : 11 xosc_r : 11`.

### 5.5 BLE baseband (t = 0.159 s)

[M:t] in this order:

1. `0x2FC80` |= 1
2. `0x28000` = 1
3. `0x28034` = `0x0A01` (V15) / `0x0901` (demo_ble)
4. software allocates and zeroes the baseband RAM block; `0x2FC84` = base; `0x2FCBC` = base + size
5. interrupts registered: IRQ 45 (event) and IRQ 29 (RX), priority 2 [M:s]; stock runs both on CPU 0 (§10)
6. `0x28000` |= `0x2000`, |= `0x4000`, &= ~`0x10` → `0x6001`
7. `0x28008` = (x & ~0x3FF) | 70, then |= `0xF000`; `0x2800C` = 70; `0x28010` = 62 | 62 << 8; `0x28014` = 30;
   `0x28018` = 20 | 20 << 8

Then, when a link is opened, all 17 columns of the link are written 0 (op 5, columns 0…16, data 0), column 14
written 0 again, and the IRQ enables cleared (`0x28028`, `0x2804C` = 0) [M:t].

The link control block is initialised in SRAM with [M:s]: ANCHOR 0, BITOFF 0, ADVIDX `0xD388`, WIDEN0 0, WIDEN1
`0x51E7`, access address `0x8E89BED6`, CRC init `0x555555`, OPTCNTL `0x28` then |`0x200`, TXPTR0/1 = 1 (placeholder),
INTFRAME |= 2, |= 4, RXBUF0/1CNTL bit0 cleared, TX power and AGC fields set, PLL_COMP from the stored crystal offset,
RFPRIOCNTL `0x101`, RFPRIOSTAT 1, TXTOG/RXTOG 0, TXAHDR0/1 0, TXDHDR0/1 1, RXMAXBUF 255, IFSCNT `0x8295`, FILTERCNTL 0,
target address 0, and the channel tables for an all-ones channel map (§6).

---

## 6. Advertising set-up

Measured order, stock V15, after the link is opened (t = 0.3714 s) [M:t] and the control-block contents after it
[M:d]; demo_ble is identical except the interval and the order of the last two column writes.

1. Control block (SRAM): RFPRIO 26 (or 19 when classic Bluetooth is in sniff); CRC init `0x555555`; WINCNTL0 = 50,
   WINCNTL1 = 0, WINCNTL2 = 50 [M:s, M:d].
2. Column 1 = interval in 625 µs slots (stock `0xA0`), column 15 = `0x8000` (event enable) [M:t].
3. FILTERCNTL from the filter policy: policy 0 → no bits; policy 1 → bit3; 2 → bit4; 3 → bits 3 and 4 [M:s].
4. ADVIDX = `packets << 14 | spacing_µs` = `0xC4E2` (3 packets, 1,250 µs apart) [M:d].
5. Column 8 = `0xC000` (random advDelay on) [M:t].
6. Column 6 = `0x41A5` [M:t]. Field meaning [M:s]/[I]: bit14 = advertising-channel mode; [12:8] = hop step between the
   enabled advertising channels (1); [5:0] = first channel (37); bit7 and bit6 = flags derived from the
   37/38/39 enable mask.
7. Two RX buffers armed: RXPTR0/1 = buffer offsets, RXBUF0/1CNTL bit0 = 0 [M:d].
8. TX buffer 0 = ADV_IND: payload AdvA + AdvData, TXAHDR0 = type 0 (TxAdd 0, public), TXDHDR0 = length << 8
   (`0x2200`, 34 bytes). TX buffer 1 = SCAN_RSP: AdvA + ScanRspData, TXAHDR1 = 4, TXDHDR1 = `0x1000` (16 bytes) [M:d].
9. OPTCNTL bit4 cleared (local-address match on) [M:s]; LOCALADR = own address [M:d]; FORMAT = `0x000C` [M:d].
10. Column 2 = `0x2000` (state 2) [M:t].
11. IRQs on: `0x2802C` = `0x001`, `0x101`; `0x2804C` = `0x100`; `0x28028` = `0x001`, `0x101`; `0x2804C` = `0x101` [M:t].
12. Start: column 7 = 0, column 0 = 0, column 14 = 0, column 0 = 0 (start slot − 1 = 0), column 14 = `0x8000` [M:t].

The engine then advertises on its own [I]. Answering a SCAN_REQ within T_IFS needs no software: the scan response
sits in TX buffer 1 and FORMAT bit8 (ignore SCAN_REQ) is clear [I, strong: software cannot meet 150 µs].

Changing AdvData while advertising: the vendor sets a "flush" flag and copies the new data into TX buffer 0 or 1
inside the event ISR, then rewrites the header [M:s].

Frequency tables (FRQ_*, CHMAP*) for an all-channels map, as measured [M:d]: FRQ_IDX0[i] = i; FRQ_TBL0[i] =
2 × (i + 1) for i ≤ 10, 2 × (i + 1) + 2 for 11 ≤ i ≤ 36, and 0, 24, 78 for channels 37, 38, 39 (MHz − 2402);
FRQ_IDX1/FRQ_TBL1 = the used channels packed in order, then 37–39 at their own indices; CHMAP0/1/2 =
map bits and the used-channel count.

---

## 7. Connection set-up from CONNECT_IND

None of this ran in the emulator (no events). Source: the vendor binary [M:s] and structure [I].

**Where it happens.** The CONNECT_IND is handled in the **RX interrupt** (IRQ 29) of the advertising link, before
the ISR returns [M:s]. The same link n turns from state 2 into state 7; no new link is opened [M:s]. So the whole
set-up must finish before the first anchor: transmitWindowDelay 1.25 ms + transmitWindowOffset after the end of the
CONNECT_IND [Core spec]. With the audio ISR at higher priority this is the tightest timing in the design (feasibility
doc §9.5).

**Input.** The 22 LLData bytes of the CONNECT_IND are used verbatim, in Core-spec order: AA (4), CRCInit (3),
WinSize (1), WinOffset (2), Interval (2), Latency (2), Timeout (2), ChM (5), Hop (5 bits) + SCA (3 bits) [M:s].

**Window widening** (computed before programming, µs) [M:s]:
widening = 2 + 2 × ⌊interval_µs × (masterSCA_ppm + 200) × (latency + 1) / 1,000,000⌋, with
interval_µs = Interval × 1,250 and masterSCA_ppm from the SCA field. (200 ppm is the local clock accuracy assumed.)

**Register writes, in the vendor's order** [M:s] (values for a peripheral):

1. Control block: RFPRIO 28; ANCHOR = `0x8000`; TXTOG bit1 cleared; RXTOG = 0.
2. BDADDR0/1 = AA; CRCWORD0/1 = CRCInit.
3. OPTCNTL |= 4; &= ~`0x200`; |= `0xC00`; |= `0x1000`.
4. INTFRAME = (INTFRAME & ~`0x30`) | `0x10`.
5. TXBUF0CNTL bit0 = 1; TXBUF1CNTL bit0 = 1 (both TX buffers empty).
6. ANCHOR = (ANCHOR & `0x8000`) | 4 (1M PHY dead time).
7. Column 5 = 0 (instant cleared).
8. Column 4 = `0x8000 | (2 × WinOffset − 1)` if WinOffset > 0, else 0 (offset in 625 µs slots).
9. Column 2 = `7 << 12 | latencyEnable << 11 | Latency` (latency forced 0 when more than one link is active).
10. WIDEN0 = widening / 625; WIDEN1 = `0x5000 | widening mod 625`.
11. Column 8 = 0 (advDelay off).
12. Column 3 = 0; EVTCOUNT = 0.
13. WINCNTL0/1 = WinSize × 1,250 + 1,250 µs (the first receive window); WINCNTL2 = 50.
14. Column 1 = 2 × Interval (slots), column 15 = `0x8000 | (2 × Interval) >> 16`.
15. Supervision timeout (Timeout × 10 ms) is a **software** timer [M:s].
16. Channel tables and CHMAP from ChM (as in §6); column 6 = `0x8000 | Hop << 8 | Hop`.
17. TX headers: TXAHDR0/1 = 0; TXDHDR0 = `((TXTOG & 1) << 2) | 1`; TXDHDR1 = that value XOR 5 (LLID 1, empty;
    the two buffers start with opposite bit2) [M:s].
18. The connection's TX buffers are the two advertising TX buffers (address = base + TXPTRn − 20) [M:s].
19. If the CONNECT_IND header has ChSel = 1 and the feature is enabled, column 10 bit6 = 1 (channel selection #2) [M:s].

The anchor counter (columns 0/14) is **not** rewritten for a peripheral; the engine continues from the CONNECT_IND
timing [I]. (For a central the vendor programs column 0/14 with 2 × WinOffset + 4.)

---

## 8. Per-event servicing (IRQ 45 event, IRQ 29 RX)

Both handlers loop over links 0…N−1 and service link n when its enable and pending bits are both set [M:s].

**IRQ 29 (RX), per link n with (`0x28028` & `0x28030`) bit 8+n** [M:s]:

1. Write 1 to `0x2802C` bit 8+n (acknowledge).
2. In a connection (states 1, 6, 7): read RXTOG bit0 and both RXBUFnCNTL bit0 values, repeating until RXTOG reads
   the same twice (a consistency loop). The buffer selected by RXTOG holds the packet if its RXBUFCNTL bit0 = 1.
3. For a filled buffer: take it, point RXPTRn at a fresh buffer, clear its RXBUFnCNTL bit0 (re-arm).
4. Decode from the control block, not from the buffer: RXAHDRn/RXDHDRn (header), RXSTATn (status, ack bit8),
   EVTCOUNT (event counter), LASTCHMAP (channel), RSSI2/RSSI3 (RSSI). Status [3:0] ≠ 1 with bit2 or bit3 → an
   errored packet: length 0, marked invalid.
5. TX side, in the same ISR: read TXTOG bit0 and TXBUF0/1CNTL bit0 twice; the transition between the two reads tells
   which buffer the engine finished. For that buffer: release the sent PDU (it was acknowledged) and, if another PDU
   is queued, copy it into the fixed TX buffer, set TXDHDRn = length << 8 | MD << 3 | LLID (keeping bit2), set
   INTFRAME bit6 = MD, and clear TXBUFnCNTL bit0. If nothing is queued, INTFRAME bit6 = 0 and the buffer stays empty;
   the engine then sends an empty PDU [I].

**IRQ 45 (event), per link n with (`0x28028` & `0x28030`) bit n** [M:s]:

1. Write 1 to `0x2802C` bit n.
2. Advertising (state 2): if AdvData or ScanRspData changed, copy them into TX buffer 0/1 and rewrite the headers;
   then re-assert both TX buffers (TXTOG decides which is 0); set RFPRIO 26.
3. Peripheral (state 7): read the event counter (column 3 − 1); if an instant is pending and the counter passed it,
   clear it; run per-event callbacks; manage slave latency (columns 2 and 9); adjust the receive window
   (WINCNTL0/1) from the measured drift ("window auto-zoom"); clear the RX-valid flags.
4. Second group: if (`0x2804C` & `0x28050`) bit n, set `0x2804C` bit 8+n (acknowledge) and handle the link timeout.
5. After the loop: if `0x28038` bit7 is set, write bit6 of `0x28038`.

Lock: both ISRs take one spinlock byte in SRAM with interrupts disabled for the whole handler [M:s]. Their duration
is not measured (§12).

### 8.1 Measured on an FM-1 [M:hw]

Optimist's own driver (route C, `optimist` `feat/ble-routec` 9a90c7d, console `blell`, 2026-10-08), a Mac connecting
19 times (interval 24, WinSize 3, WinOffset 22, timeout 72, 37 channels, hop 13, SCA 1):

- **Advertising RX**: RXBUF0/1CNTL read `00` on every RX IRQ (54); the SCAN_REQ / CONNECT_IND was always in the
  buffer **RXTOG had moved past** (RXTOG bit0 = the buffer filled next, so the filled one is the other), payload at
  RXPTRn, header in RXAHDRn / RXDHDRn (e.g. `2285` / `2200` for a CONNECT_IND). Step 2's "RXTOG selects the filled
  buffer, CNTL bit0 = 1" does not hold while advertising.
- **Connection RX**: RXBUFnCNTL bit0 = 1 on the buffer the driver expected for all 470 packets (no desync); the first
  data packet 26–29 ms after the CONNECT_IND.
- **TX**: connection set-up wrote TXBUF0/1CNTL bit0 = 1 (§7 step 5); afterwards the engine had bit0 = 0 on TXTOG's
  buffer with nothing loaded by software: the engine **clears** bit0 when done with a buffer, so "clear bit0 to load"
  (step 5 above) is the wrong direction on hardware; bit0 = 1 = loaded.
- **The slot clock** (columns 0 / 14, op 2) stepped **backwards** inside a connection (about 267 slots once): not
  usable as a monotonic software time base.

`optimist` `feat/ble-routec` 5008663 (2026-10-09, one Mac connection, interval 24, WinSize 3, WinOffset 20, hop 5):

- **TX, first PDU out**: TXDHDR0 read `0005`, so §7 step 17 found TXTOG bit0 = 1 at set-up (advertising left it
  there) and wrote TXDHDR0 `0005`, TXDHDR1 `0001` [I: from the formula]. At the first received packet: TXTOG `0007`, TXBUF0CNTL `01`,
  TXBUF1CNTL `00`, TXDHDR1 `0000`: the engine had sent buffer 1 (TXTOG's, set-up's empty PDU) and cleared its bit0.
  Software loaded the VERSION_IND into buffer 1 (bit0 = 1, TXDHDR1 `0603`); one event later TXTOG `0005` (bit1 now
  0, bit0 still 1) and TXBUF1CNTL `00`: sent and acknowledged (the Mac answered with LL_FEATURE_REQ). So bit0 = 1
  is "loaded" and the engine clears it on the buffer it transmits; TXTOG bit0 did **not** move after the empty PDU
  (buffer 1 went out twice).
- **TX, the stale buffer**: afterwards TXTOG bit0 read 0 at every interrupt and TXBUF0CNTL stayed `01` (TXDHDR0
  `0005`) for the 7 s until the Mac terminated (0x13): the engine never sent nor cleared set-up's bit0 = 1 on the
  buffer TXTOG was not pointing at (§7 step 5's "both TX buffers empty" is "both loaded" in this direction). A
  driver waiting for that bit to clear never sends again. Writing bit0 = 0 on a buffer software has not loaded is
  the remedy implemented in 0475aa5 [I: not yet measured on hardware].

`optimist` `feat/ble-routec` 0475aa5 (2026-10-09, one Mac connection, interval 24, WinSize 3, WinOffset 9, hop 5;
`hw-logs/blell6.txt`):

- **Freeing set-up's stale bit works**: TXBUF0CNTL written `00` at the first packet; the VERSION_IND left buffer 1
  and was acknowledged at the next event (TXTOG `0005`, TXBUF1CNTL `00`).
- **Buffer 0 is never sent**: at the Mac's LL_FEATURE_REQ (one event later) TXTOG read `0002` (bit0 = 0); the
  FEATURE_RSP put into buffer 0 (TXDHDR0 `0907`, TXBUF0CNTL `01`) stayed there through 265 events (TXTOG `000E` from
  event 4 on) until the Mac terminated (0x13, 8 s after its FEATURE_REQ). Set-up's empty PDU and every PDU that left
  went out of buffer 1. So TXTOG bit0 is **not** "the buffer the engine sends next", and TXBUFnCNTL bit0 = 1 alone
  does not make the engine send a buffer [M:hw]. What selects it (TXTOG bits 1–3, the SN / NESN state against
  TXDHDRn bit2, which is 1 in buffer 0 and 0 in buffer 1 after §7 step 17, or a one-buffer mode) is open [I].
- **The engine sets TXBUF1CNTL bit0 itself**: it read `01` one event after the VERSION_IND was acknowledged, with no
  software write to it [M:hw]; whether that is a retransmission, a re-arm or something else is open.
- TXPTR0 is the ADV_IND buffer (sent on every advertising event, so the offset is valid) and does not overlap the RX
  buffers in Optimist's layout; that the engine reads TXPTR0 in a connection is not measured.
- Remedy implemented in `ble-tx0` (cbb94d1) [I: not yet measured on hardware]: a PDU goes into the buffer the engine
  last took a PDU from (buffer 1 here); one PDU at a time until the engine has taken from both; a PDU still loaded 2
  events later is moved to the other buffer. Its txsnaps add TXPTR0/1 and the central's last RXDHDR (NESN / SN).

`optimist` `ble-tx0` cbb94d1 (2026-10-09, one Mac connection, interval 24; `hw-logs/blell8.txt`, also blell7):

- **The FEATURE_RSP was received though its bit0 never cleared**: the Mac's FEATURE_REQ at 62.116 s, the FEATURE_RSP
  loaded into buffer 1 (TXDHDR1 `0903`, bit0 = 1), and the Mac's LL_LENGTH_REQ at 62.266 s, 5 events later (a central
  starts no procedure before the current one completes: it had the FEATURE_RSP). TXBUFnCNTL bit0 of the FEATURE_RSP's
  buffer never read 0 at any interrupt; cbb94d1 moved it between the buffers to the end (65 moves, 61 force-frees),
  never loaded the LENGTH_RSP behind it, and the Mac terminated (0x13) 7.47 s after its LENGTH_REQ [M:hw]. Only the
  first data PDU's (the VERSION_IND's) bit0 has ever read cleared (blell4, blell6, blell8) [M:hw].
- **The engine sets bit0 again**: a buffer software had just written bit0 = 0 on (moving the PDU off it) read `01`
  within 4 events, both buffers in turn, as blell6's TXBUF1CNTL the event after the VERSION_IND was acknowledged
  [M:hw].
- **TXDHDRn bit2 never changes**: `0907` in buffer 0 and `0903` in buffer 1 through 65 moves (software keeps bit2;
  the engine never wrote it), i.e. the opposite SN bits of §7 step 17 stay for the whole connection [M:hw]. TXPTR0/1
  `03D0` / `04E4` in a connection (distinct, inside the block) [M:hw].
- **The central's header** (RXDHDR, its NESN bit2 / SN bit3): `0005` / `0009` in snapshots 4 events apart, consistent
  with both bits alternating every event (every packet acknowledged both ways) [M:hw].
- Model [I] (used by `ble-tx0`'s fix and its host test, not yet measured): the engine keeps SN / NESN per Core Vol 6
  Part B 4.5.9 and sends the buffer whose TXDHDR bit2 equals its transmitSeqNum (SN fixed per buffer: the other
  buffer after each acknowledgement, the same one until acknowledged); bit0 = 1 "loaded", cleared by the engine only
  in the case above, set again on the buffer the central acknowledges. So bit0 is not the acknowledgement; the peer's
  NESN is: a PDU with SN s is acknowledged once a header received after it was loaded asked for s (NESN = s) and a
  later one has NESN = !s. Not explained by this model: blell6's FEATURE_RSP in buffer 0 (SN 1) that the Mac never
  answered with a LENGTH_REQ. **Superseded by §8.2**: the vendor's contract is the opposite polarity (bit0 = 1 is
  "empty"), and it explains blell6, blell8 and blell9.

### 8.2 TX buffers: definitive semantics

Read from the vendor library's LLVM IR (`btctrler.a`, `RF_ble.c`: `__hw_tx_process`, `__hw_tx_buf_proess`,
`ble_hw_tx_update_header`, `le_hw_send_packet`, `le_hw_send_packet_high`, `le_hw_ioctrl` case 6 = connection set-up,
`ble_rx_irq_handler`, `ble_event_irq_handler`, `__set_hw_frame_init_4_2`) and its debug info. Tags here: **[IR]** = what
the vendor code does, read from the IR (a form of [M:s]); **[M:emu]** = stock V15's own code driving the emulator's
engine model (only the *software* side is evidence; the engine is our model); **[M:hw]** = the FM-1 logs of §8.1;
**[I]** = inferred. No vendor code is quoted.

**The one-line answer.** TXBUFnCNTL bit0 is the vendor's **"empty"** flag: 1 = the buffer is empty (software may fill
it, and a PDU software had put there is finished), 0 = software has handed a PDU to the engine. Software loads a buffer
and then **clears** bit0; it never sets bit0 after the connection set-up. Optimist's driver since 5008663 uses the
inverted polarity (1 = loaded), which is why its PDUs were ignored or sent by accident (point 6).

1. **TXTOG / RXTOG.**
   - The vendor's TX service reads TXTOG **bit0 only** [IR]; the debug info names the snapshot bit `txtog` next to
     `empty0` / `empty1` (a union `_tx_soft_read`) [IR]. Software writes TXTOG only twice: the whole register = 0 in
     the frame init, and bit1 cleared (bit0 kept) at connection set-up [IR]. It never flips bit0 and never reads bits
     1–3 [IR]. So bit0 is the engine's; bits 1–3 (the 7 → 5 → 2 → E → 4 → C → D sequence) are engine state with no
     software meaning [IR for "unused", meaning unknown].
   - What bit0 selects, from the way the vendor uses it [IR]: in advertising the primary PDU (ADV_IND) goes into the
     buffer numbered TXTOG bit0 and the SCAN_RSP into the other; at connection set-up the buffer numbered TXTOG bit0
     gets TXDHDR bit2 = 0 and the other bit2 = 1 (the §7 step 17 formula); in a connection the service handles the
     TXTOG buffer **first**, then the other. So bit0 names the buffer the engine is on / takes next [I], and it is
     the engine, never software, that moves it [IR for software; the engine side I].
   - RXTOG: software writes it 0 (frame init and connection set-up) and otherwise only reads bit0 [IR]. In a
     connection the RX ISR takes the RXTOG buffer first, then the other, each only if its RXBUFnCNTL bit0 = 1, and
     clears that bit0 after taking it [IR]. While advertising it takes the buffer **other** than RXTOG's without
     looking at RXBUFnCNTL [IR] (matches §8.1 [M:hw]).
2. **TXBUFnCNTL.** bit0 = `empty0` / `empty1` in the vendor's debug info; the service passes it as a parameter named
   `idle` [IR].
   - **Software sets bit0 = 1** on both buffers at connection set-up, and nowhere else [IR, M:emu: stock's only
     1-writes are the two set-up writes].
   - **Software clears bit0 = 0** as the last step of loading a PDU into that buffer [IR, M:emu: stock clears buffer 0,
     then 1, then 0 … one per PDU].
   - **The engine sets bit0 = 1** when it is finished with the PDU in that buffer: the vendor treats 1 on a buffer it
     loaded as "done", calls the PDU's `ack_callback` and frees it [IR for the software; that the engine sets it on the
     peer's acknowledgement is I, consistent with §8.1's "the engine sets bit0 back to 1 by itself" [M:hw]].
   - The other bits are preserved by read-modify-write; software gives them no meaning [IR]. Unexplained [M:hw]: at
     the first event the engine cleared bit0 of the TXTOG buffer with nothing loaded (and TXDHDR1 read `0000`);
     the vendor's rule tolerates this (point 3: a 0 buffer without a PDU of its own is simply skipped).
3. **Loading a data PDU** [IR]. PDUs wait in two software queues: LL control PDUs (`le_hw_send_packet_high`) and
   ACL data (`le_hw_send_packet`); the control queue is served first, each queue can be paused by a block flag
   (`tx_acl_c_block`, `tx_acl_block`). The service runs (a) in task context right after a PDU is queued (with a
   re-entry lock, radio priority 28/30 and slave latency held) and (b) **at the end of every RX interrupt** in a
   connection (states 1, 6, 7), after both RX buffers are handled. The event interrupt (IRQ 45) never runs it.
   One service pass:
   1. Read TXTOG bit0 and both empty bits, twice; if TXTOG bit0 changed between the reads use the second reading,
      otherwise the first (a consistency snapshot).
   2. Handle buffer T = TXTOG bit0, then buffer 1 − T, each with its own empty bit:
      - **empty = 1:** if a PDU of ours is recorded in that buffer, it is finished: call its acknowledgement
        callback and free it. Then take the next PDU (control queue first). If there is none: INTFRAME bit6 = 0, the
        buffer is recorded as having no PDU, **the empty bit stays 1** (the engine then has nothing to send from it). If
        there is one: MD = "another PDU is queued"; pass it through the vendor's encryption step (before the
        copy, so the buffer receives the final bytes); record it in that buffer; copy the payload to the buffer's fixed payload area (the TXPTRn offset; TXPTRn
        itself is **not** rewritten in a connection); TXAHDRn = 0; TXDHDRn = length << 8 | MD << 3 | LLID, **bit2 kept
        as read**; INTFRAME bit6 = MD; **then clear the empty bit** (the last write); then the PDU's "sent" callback.
      - **empty = 0:** the engine owns the buffer: payload, length and the empty bit are not touched. Only if the PDU
        recorded there has MD = 0 and more PDUs are now queued, MD is set to 1 and TXDHDRn is rewritten (bit2 kept).
   3. So one pass can load **both** buffers (e.g. right after set-up, when both are empty): the first queued PDU into
      the TXTOG buffer, the second into the other. A PDU never moves between buffers.
4. **Acknowledgement** [IR]. The only signal the vendor uses to free a TX PDU is the empty bit reading 1 again on
   the buffer it loaded. It does not compare NESN (RXDHDR bit2 and bit3 are only copied into the RX record), does not
   use TXTOG as an ack, and does not use EVTCOUNT for TX. RXSTAT bit8 is stored (`rx_ack`, plus a "packet valid"
   flag) and used only by the slave-latency decision in the event ISR: latency is held while either buffer holds a
   PDU of ours or that flag is clear.
5. **SN / NESN** are the engine's [IR: software keeps none]. A software field `tx_seqn` is zeroed at set-up and never
   read; TXDHDR bit2 is written only at set-up (0 on the TXTOG buffer, 1 on the other) and preserved by every later
   header write; there is no NESN field in TXDHDR. Vendor TXDHDR layout: [1:0] LLID, bit2 engine/buffer bit (the
   set-up value), bit3 MD, bit4 the coded-PHY S=2/S=8 flag (only with the extended-PHY feature compiled in), [15:8]
   length. That bit2 *is* the SN is [I] (it never changed on the FM-1 [M:hw]); software must treat it as opaque.
   TXAHDR: [3:0] PDU type, bit4 TxAdd, bit5 RxAdd (advertising; Core bits 6/7 moved down), bit9 ChSel (extended
   advertising only); 0 for every data PDU. RXDHDR is the Core layout: bit2 NESN, bit3 SN, bit4 MD.
6. **Why buffer 0 "was never sent"** [I from IR + M:hw]. Nothing arms a buffer except clearing its empty bit:
   INTFRAME bit6 is MD only; INTFRAME bit3 (`__set_hw_tx_enable` clears it) is set only in a vendor test mode
   (`config_vendor_le_bb`, 0 in the SDK) and by privacy set-up, never by the connection data path; OPTCNTL and
   TXTOG are not written per PDU. Optimist's driver marked a loaded buffer with bit0 = 1, which to the engine means
   "empty": blell6 / blell9's FEATURE_RSP in buffer 0 with TXBUF0CNTL `01` was an empty buffer for 265 events. In
   blell8 the FEATURE_RSP *did* go out because each "move" wrote bit0 = 0 on the buffer it left, which still held
   the FEATURE_RSP's header and payload: that is the vendor's "load" signal; the engine later set the bit back to 1
   (finished), which is the "engine sets bit0 to 1 by itself" of §8.1. The 9a90c7d driver (blell3, vendor polarity)
   stalled for another reason: it would load only the TXTOG buffer, whose bit0 the engine had cleared, while the
   vendor would have loaded the other buffer (empty = 1). Not explained: blell6's VERSION_IND, written into buffer 1
   (the engine's current buffer, bit0 0 at that moment) with bit0 = 1, still reached the Mac.
7. **Connection set-up, TX side** [IR], in the vendor's order: RFPRIO 28; ANCHOR `0x8000`; TXTOG bit1 cleared
   (bit0 kept); RXTOG = 0; AA, CRC, OPTCNTL; INTFRAME [5:4] = `01`; **both empty bits = 1**; dead times; instant
   cleared; … (the rest of §7); then `tx_seqn` = 0, TXAHDR0/1 = 0, TXDHDR0/1 from TXTOG bit0 (§7 step 17), the
   fixed payload areas taken from TXPTR0/1, and **no PDU recorded in either buffer**. Nothing else TX-related is
   written; in particular no TXPTR change, no TXTOG bit0 write. (With the vendor test mode off, FORMAT bit1 and
   INTFRAME bit3 stay clear.) Compared with Optimist's `ble_hw_conn_start` the register writes match; what differs is
   only the per-PDU protocol below.

**Stock V15 against the engine model** [M:emu, 2026-10-09, `fm1-emulator-ble` `feat/ble-engine`, script
scan/connect/version/features/mtu/discover, memwatch on the TXBUF0/1CNTL + RXBUF0/1CNTL word]: stock wrote TXBUF0CNTL
= 1 and TXBUF1CNTL = 1 once at set-up, then **cleared** bit0 on buffer 0, then buffer 1, alternately, once per PDU it
sent; every bit0 = 1 after that was the model's (the "finished" signal), and with nothing queued both bits stayed 1
while the model sent empty PDUs. Stock also cleared RXBUFnCNTL bit0 after each received packet. The model follows the
vendor polarity (1 = consumed), which is why stock connected, discovered and played BLE-MIDI on it.

**The rule for an implementer** (vendor contract; [IR] unless marked):

- Set-up: both TXBUFnCNTL bit0 = 1; TXAHDR0/1 = 0; TXDHDR of the TXTOG buffer = LLID 1 with bit2 = 0, the other
  with bit2 = 1 (§7 step 17); no PDU recorded in either buffer.
- Service at the end of every connection RX interrupt (and, optionally, right after queuing a PDU, with the ISR
  excluded): take a consistent snapshot of TXTOG bit0 and both bit0s; handle the TXTOG buffer, then the other.
- A buffer with **bit0 = 1** is free. If a PDU of ours is recorded there, it is **acknowledged**: release it. Then,
  if a PDU is queued: copy its payload to the buffer's TXPTRn area, TXAHDRn = 0, TXDHDRn = length << 8 | MD << 3 |
  LLID with **bit2 kept**, INTFRAME bit6 = MD, **then write bit0 = 0** (last), and record the PDU there. If nothing is
  queued, leave bit0 = 1 and INTFRAME bit6 = 0; the engine sends empty PDUs.
- A buffer with **bit0 = 0** belongs to the engine: do not touch its payload, length or bit0 (setting MD on it is the
  only change the vendor makes).
- Never write bit0 = 1 after set-up, never write TXTOG, never change TXDHDR bit2, never move a PDU to the other
  buffer, never "force-free" a buffer. Acknowledgement = bit0 back to 1 on the buffer you loaded; nothing else.
- Open [M:hw needed]: whether the engine also clears bit0 itself on the buffer it is transmitting (seen at the
  first event), and what TXTOG bits 1–3 count. The rule above does not depend on either.

---

## 9. Connection-parameter and channel-map update with an instant

[M:s] unless marked.

- On receiving LL_CONNECTION_UPDATE_IND or LL_CHANNEL_MAP_IND, software writes column 5 = instant and remembers the
  update type; RFPRIO 30.
- **One event before the instant** (event counter = instant − 1), software applies the new values:
  - channel map: rewrite FRQ_IDX1/FRQ_TBL1 and CHMAP0/1/2 (as in §6);
  - connection update: WINCNTL0/1 = WinSize × 1,250 + 625 µs (+ the widening offset) and WINCNTL2 = 50; column 2
    with the new latency; WIDEN0/1 from the new widening; column 4 = `0x8000 | (2 × WinOffset − 1)` (or 0);
    column 1/15 = the new interval in slots.
- At the instant the engine applies them [I: the column-5 instant is engine-visible, so the engine likely gates the
  switch on it].
- After the instant the procedure is completed in software (event two after the instant: window size reset).

---

## 10. Interrupts and priorities

| IRQ | Source | Priority, core | Tag |
|---|---|---|---|
| 45 | BLE event (end of an advertising/connection event); also the second group (`0x2804C/50`) | 2, CPU 0 in stock | M (feasibility §9.2), M:s |
| 29 | BLE RX (a packet received, per link) | 2, CPU 0 in stock | M (§9.2), M:s |
| 40 | Bluetooth 625 µs clock alarm (`0x200E4` alarm, `0x2000C` bit9 enable, `0x20018` bit9 acknowledge, `0x20010` bit9 pending) | 2 in stock | M (§9.2), M:t |
| 41 | slot timers (`0x2FD40`: bits 0–7 enable, write bit 8+k to acknowledge, bits 16–23 pending; `0x2FD44 + 4k` alarm slot, 27-bit) | 2 in stock | M (§9.2), M:t |
| 11 | audio (not BLE) | 3 in stock | M (§9.2) |

The stock BR/EDR stack keeps the Bluetooth clock and two slot timers busy every 0.5 s (32–34 BR/EDR register writes
and 23 slot-timer writes per period) [M:t]; demo_ble (BLE only) does not [M:t]. A BLE-only link layer probably
needs neither; whether the BLE engine needs the BT clock running (`0x20000` bit0) is unknown (§12).

---

## 11. Stored trim and MAC data (flash)

**VM store.** The SDK key/value store ("VM") sits at `0x093000`, length `0x10000` in demo_ble (area A `0x093000`,
area B `0x09B000`, 32 KiB each) [M:c]. **Stock V15 uses only 16 KiB: area A `0x093000`, area B `0x095000`, 8 KiB
each** [M:d, §14.1] in the emulator. **On a real FM-1 the VM is at `0x0E8000`** (one 4 KiB sector before BTIF; the
second area unknown) [M:hw, §14.1]. Both write the same record bytes for the shared records [M:d]. The area starts with the magic
`55 AA AA 55` [M:d]. The record format is decoded in §14. Stock also writes sectors `0x0F2000`, `0x0F3000` and
`0x0FB000` (its own settings) [M:d].

The "Size" column below counts the 4-byte record header (the VM's own log does: `idx_len`); the data lengths are
2 (106), 7 (107), 20 (108), 34 (109), 14 (113) and 68 (187) bytes [M:d, §14.5].

| Id | Name in the SDK headers [M:s] | Size | Written by | Content |
|---|---|---|---|---|
| 102 | BT MAC | 6 | — | classic address (if provisioned) |
| 104 | BLE MAC | 6 | — | BLE address (if provisioned) |
| 106 | XOSC index | 6 | demo boot 1 [M:c] | crystal load trim (`xosc_l`, `xosc_r`; 11/11 in the emulator) |
| 107 | Wi-Fi PA data | 11 | demo boot 1 [M:c] | PA calibration (`PA_C_I`) |
| 108 | Wi-Fi PA MCS digital gain | 24 | demo boot 1 [M:c] | |
| 109 | BLE local info | 38 | demo boot 1 [M:c] | host-side keys/identity |
| 110 | BT frequency offset | 6 (4 + CRC16) | vendor RF code [M:s] | carrier offset → PLL_COMP |
| 113 | BLE remote DB info | 18 | demo boot 1 [M:c] | host bonding table |
| 187 | **Wi-Fi RF init info** | 72 | demo, ≈1 s after boot 1 [M:c] | the calibration cache that shortens the next boot (§5.3) |
| 197 | RF production-test results | 30 (28 + CRC16) | vendor RF code [M:s] | |
| 601, 615 | TX power settings (1 byte each) | 1 | read by stock [§9.2] | [I] |

**BT address.** With no factory address in the key_mac sector (`0x0FF000`, all `0xFF` in the package), both oracles
generate one from the random source at first boot and store the classic address at `0x0E9000`: 4 header bytes
(byte 0 varies, then `66 60 00`) followed by the 6 address bytes [M:d]. The BLE address shares the upper 3 bytes and
differs in the lower 3 (stock: classic `<6-byte address>`, BLE `<6-byte address>`, LSB first; both sharing the upper 3 bytes) [M:d].

---

## 12. Unknowns and the experiment for each

Only hardware answers most of these. "Sniffer" = an nRF52840 dongle with the nRF Sniffer, or an nRF board running
the Direct Test Mode (DTM) receiver.

| # | Unknown | Proposed experiment |
|---|---|---|
| U1 | Does reloading the stored trims (VM 106/107/108/187/110) without the live BBP calibration give a clean carrier? | On hardware with VM populated by stock: our firmware runs only §5.1, the RF-die LUT load and the VCO scan, then transmits DTM packets on channel 19 using the fixed-channel mode (column 6 = `0x2000 | ch`, state 6, AA `0x71764129`, CRC `0x555555`). Count PER at the DTM receiver; repeat with the full calibration path. |
| U2 | Real calibration results (VCO bank, DC/IQ, PA) and VM 187 contents on a real FM-1 | One unit read 2026-10-08: the VM at `0x0E8000`, 106 / 107 / 108 / 187 there, 187's CRC holds (§14.1). Still: a second unit, and the second area. Read flash `0x0E8000` (and the backup, §18.1), decode with §14.3. |
| U3 | ~~VM record format~~ **answered in the emulator, §14** (header, CRC-16/XMODEM check byte, append/supersede, 60 % compaction into the other area, V15's 8 KiB areas) | Confirm on one hardware dump (§18.1) that the records decode the same way. |
| U4 | Factory address in `0x0FF000` on real units | Read that sector on two units. |
| U5 | Timing constants `0x28008–0x28018`, `0x28034` [15:8] (9 vs 10), IFSCNT `0x8295` | With a sniffer, measure T_IFS (ADV_IND → SCAN_RSP gap) and the advertising PDU spacing on stock; then vary one register at a time from our firmware. |
| U6 | Does the engine send the SCAN_RSP and stop advertising on CONNECT_IND by itself? Where is the first anchor? | Stock V15 + sniffer + a phone connecting: timestamps of CONNECT_IND, first central packet and first peripheral reply. |
| U7 | Exact meaning of RXSTAT bits 2/3, TXBUF/RXBUF CNTL bit0 direction, `0x2802C` read-back, `0x28038` bits 6/7 | DTM receive on our firmware with a DTM transmitter sending good packets, then packets with a wrong CRC init, then nothing (sync timeout); log the status words. |
| U8 | Event and RX ISR durations and when each IRQ fires relative to the radio event | Toggle a GPIO in both ISRs of our link layer; logic analyser plus sniffer. Later in the emulator once it models the engine. |
| U9 | RSSI gain table (64 entries) | Sweep the received power with a step attenuator from a DTM transmitter; record RSSI2 against the input level. |
| U10 | TX power level → dBm (`TX_PWER` = level + 16; stock uses level 6) | Measure DTM output power per level with a spectrum analyser or a calibrated receiver. |
| U11 | Whether RFPRIO (radio arbitration with BR/EDR and Wi-Fi) matters when BR/EDR and Wi-Fi are off | Hold RFPRIOCNTL/STAT constant (26) in our firmware and compare PER with stock. |
| U12 | Whether the BLE engine needs the BT 625 µs clock (`0x20000` bit0) and the BR/EDR baseband init | Emulator first (does the advertising set-up still read sane column values without them?), then hardware: advertise with and without that step. |
| U13 | Temperature drift: is a periodic re-trim needed? Stock RMWs `0x11900` about every 2 ms from one routine (meaning unknown) | Leave a link running from cold to warm (and in a fridge/hot box) and watch PER and the carrier offset. |
| U14 | Carrier offset and PLL_COMP sign/scale | Single-carrier mode (the vendor has a single-carrier test path), measure the carrier with a spectrum analyser, compare with VM 110. |
| U15 | Column 6 flag bits for advertising (bits 6, 7) and column 10/9 details | Sniffer: advertise with each channel mask (37, 38, 39 subsets) and compare channel order. |
| U16 | Where stock V15's updater stages the update loader (and its record) during a stock → Optimist/SLOOP install; (one unit: the VM at `0x0E8000` survived stock's updater installing Optimist [M:hw], §15.2) | After the first install: console `flr 0xe8000 256` (§18.3 step 2); or compare UBOOT backups before and after. |
| U17 | BBP indirect windows: do `0xCB`/`0xCF`/`0xD3`/`0xD7` read back the entry addressed by `0xC9`/`0xCD`/`0xD1`/`0xD5`? The emulator returns 0 for them, so every read-modify-write through them (§16.2) is unverified | Read-back build (§18.3 step 3): write an entry, read it back through the window. |
| U18 | Byte 2 of the BLE address V15 derives from the classic address (bytes 0–1 = CRC-16/XMODEM of it, §14.5) | Not needed for our firmware (it picks its own address); one more emulator case if it ever matters. |
| U19 | Bit 14 of `0x11900`, toggled by a periodic routine (§16.2; also U13) | Read-back build: watch `0x11900` over time on hardware. |

What only hardware can tell, in short: everything that depends on the radio (U1, U2, U5–U11, U13, U14). U3 and U12
start in the emulator.

---

## 13. How to reproduce the measurements

Emulator: `fm1-emulator`, branch **`feat/ble-trace`** (commit `cadc6b1`, worktree used for this work). It adds to
`examples/diagnose` (default behaviour unchanged):

| Variable | Effect |
|---|---|
| `FM1_MMIO_TRACE=PATH`, `FM1_MMIO_RANGES=LO-HI[,…]`, `FM1_MMIO_READS=1`, `FM1_MMIO_TRACE_LIMIT=N` | ordered log of MMIO accesses in the ranges: instruction count, tick, PC, R/W, address, size, value |
| `FM1_FLASH_DUMP=PATH` | save the serial flash after the run and list the sectors written |
| `FM1_FLASH_RESTORE=PATH` | lay a saved flash over the package before boot (a second boot of the same device) |
| `FM1_STUB_MMIO=LO-HI[:MASK],…`, `FM1_STUB_CONSOLE=ADDR` | experiments only: unmapped MMIO in the ranges reads back the last write OR'ed with MASK; bytes written to ADDR are printed |

Ranges used: `10000-100ff,11900-1197f,13b00-13b0f,14000-14067,20000-201ff,28000-280ff,2fc00-2fdff,30000-31fff`.

- Stock V15: `diagnose FM-1.fwsc 3000000000` (8.3 s emulated). Second boot: add `FM1_FLASH_RESTORE` with the first
  run's dump.
- demo_ble: build the SDK's `apps/demo/demo_ble` (board wl82); make `app.bin` the way the SDK's `download.sh` does
  (`.text` + `.data` + `.dynamic_data` + `.ram0_data` + `.cache_ram_data`); package with
  `tools/fm1pkg_make.py app.bin build/loader/ota.bin demo_ble.fwsc --sdk <SDK>`; run with
  `FM1_STUB_MMIO=12200-122ff:8000,12f00-12fff FM1_STUB_CONSOLE=1220c FM1_WATCHDOG_OFF=1`. The stubs stand in for
  the SDK board's debug UART (`0x12200`, TX-done flag bit15) and audio DAC (`0x12F00`), which the FM-1 build does not
  use. Without them the demo stops at the first UART log line. With them it boots, prints its log, calibrates, stores
  its VM records and starts advertising (`JL-AC79XX-A33E(BLE)`, interval 500 ms) [M:c].
- Exported traces (BLE baseband, BT config, BT clock, `0x10010`, `0x14000`; the AGC table data omitted):
  [ble-traces/v15-boot-ble-mmio.txt](ble-traces/v15-boot-ble-mmio.txt) (first 0.45 s) and
  [ble-traces/demo_ble-boot-ble-mmio.txt](ble-traces/demo_ble-boot-ble-mmio.txt).

The full traces (20 MB for V15) stay outside the repository; regenerate them with the commands above.

### Next emulator step (not done)

A behavioural model of the engine in `wireless.rs` (feasibility doc §9.7): when column 14 starts a link in state 2,
raise IRQ 45 every interval and IRQ 29 with a scripted SCAN_REQ or CONNECT_IND written into an RX buffer. That would
let both oracles run §7–§9 in the emulator and turn most [M:s]/[I] facts there into [M:t].

---

## 14. The SDK VM store: record format and behaviour (U3, measured)

Measured 2026-10-08 in the emulator (`fm1-emulator` `feat/ble-engine` 6531e20) with stock V15 and demo_ble, using flash
images built by hand and restored with `FM1_FLASH_RESTORE` (§13, §19). Every statement below is [M:d] (flash dump after
the run) or [M:t] (which writes changed), unless tagged otherwise. demo_ble also prints the VM's own log lines
(`[VM]…`) [M:c]; they agree with the dumps. **[M:hw]**: read on a real FM-1 with the console's `flr` (§14.1); everything else in §14 is the emulator's.

### 14.1 Where the VM is: V15 differs from demo_ble, and the FM-1 differs from the emulator

**On a real FM-1 the VM is at `0x0E8000`, not `0x093000`** [M:hw, one unit, 2026-10-08: the console's `flr` (SPI
reads) from Optimist, on a unit booted on stock V15 and then updated to Optimist through stock's own updater]:

- `0x093000–0x096FFF` read **all `FF`** (both areas of the emulator's layout below);
- `0x0E8000` starts `55 AA AA 55`, then records in the §14.2 format: `0xE8004` id 106 len 2, `0xE800A` 107 len 7,
  `0xE8015` 187 len 68, `0xE805D` 108 len 20, `0xE8075` 113 len 14, `0xE8087` 109 len 34; the log ends at `0xE80AD`
  (the same ids, order and lengths as the emulator's V15 VM). 106 = `0B 0B`, 107 = `01 07 04 07 0B 01 07`; 187's
  inner CRC holds;
- `0x0E9000` is BTIF (record 102, the classic BT address, as §14.5), so a VM area at `0x0E8000` is at most 4 KiB;
- a scan of every 4 KiB sector head from `0x093000` to `0x0FFFFF` found data only at `0x097000`, `0x0E6000`,
  `0x0E8000`, `0x0E9000`, `0x0EA000–0x0F1000`, `0x0FA000–0x0FB000` and `0x0FC000–0x0FE000` (the rest `FF`,
  `0x0E7000` included; `0x097000` and `0x0FC000+` are Optimist's own data).
- **Unknown**: where stock's second area is on hardware (`0x0E7000`, the erased sector before it, or elsewhere) and
  the area size there; whether the emulator differs from the FM-1 because of its flash size, the package, or
  something else. The emulator rows below remain what the emulator does.

The emulator [M:d]:

| Firmware | VM region | Area A | Area B | Erased at a first boot |
|---|---|---|---|---|
| **Stock V15** | `0x093000–0x096FFF` (16 KiB) | `0x093000–0x094FFF` (8 KiB) | `0x095000–0x096FFF` (8 KiB) | `0x093000–0x096FFF` only [M:d: sectors touched] |
| demo_ble | `0x093000–0x0A2FFF` (64 KiB) | `0x093000` (32 KiB) | `0x09B000` (32 KiB) | `0x093000–0x0A2FFF` [M:d, M:c `vm_area_a/b`] |

The VM length is the firmware's own setting, not the package's: both packages declare the same `VM` entry
(`0x093000`, 352,256 B, `fm1pkg_make.py:133`), and demo_ble logs `VM size: 0x56000 @ 0x93000` for it but runs its VM
with `len:0x10000` [M:c]. V15's 8 KiB halves are measured: a V15 log filled past `0x094FFF` was cut at the last record
that ends inside `0x095000`, and the compaction wrote area B at `0x095000` (§14.4). **Before this update §1 and §11 said
"32 KiB halves, B at `0x09B000`": that is demo_ble only.** For a stock-only FM-1 the VM is the 16 KiB at
`0x093000–0x096FFF` **in the emulator**; on hardware see the [M:hw] list above.

### 14.2 Area and record layout

- **Area header**: the first 4 bytes of the area in use are `55 AA AA 55`. An erased area reads `FF FF FF FF`.
- **Records** follow from area + 4, packed back to back with no padding or alignment (records start at odd
  addresses). Each record is a 4-byte header and its data:

| Byte | Content |
|---|---|
| 0 | check: the **low byte of CRC-16/XMODEM** (polynomial `0x1021`, initial value 0, no reflection, no final XOR) computed over the **data bytes only** |
| 1 | id, bits [7:0] |
| 2 | bits [3:0] = id bits [11:8] [I: only ids below 256 were seen, all with 0 here]; bits [7:4] = length bits [3:0] |
| 3 | length bits [11:4] |
| 4 … 4+len−1 | data |

Evidence: the CRC rule is the only one of the CRC-8/CRC-16 variants, sums and XORs tried that fits all 8 records
of the two oracles (ids 102, 106–109, 113, 187) [M:d]; it also explains demo_ble's own log at the end of the records,
`vm_crc = 0xff`, `crc_c = 0x1f`: an erased header (`FF FF FF FF`) reads as length `0xFFF`, and the CRC-16/XMODEM of
4,095 `FF` bytes is `0x0E1F` [M:c, computed]. Records hand-built with this rule were accepted by both firmwares
(§14.4) [M:t]. The VM log calls the record length "idx_len" and counts the header in it: `vm_write idx:187 …
idx_len:0x48` is a 68-byte data record [M:c]. **§11's "Size" column counted the 4-byte header; the data lengths
are in §14.5.**

- **End of the log**: the first header whose check byte does not match its data (an erased header always fails, see
  above), or a record that would end past the area end, ends the scan.
- **Order**: a write appends a record at the end of the log; nothing is rewritten in place.

### 14.3 Reading one id from a raw flash image (procedure)

1. Read the candidate areas: on an FM-1 `0x0E8000` (measured) and wherever its second area turns out to be; in the
   emulator `0x093000` and `0x095000` (V15). The area whose first 4 bytes are `55 AA AA 55` is the live one; a reader
   that also looks where other firmware keeps data (Optimist moves UP_FM6's voices to `0x093000`, §15.2) should
   require a valid first record as well. In the emulator: If both are,
   V15 used **A** and erased B at boot [M:d, one case]. If neither is, there is no VM (V15 erases the region and
   starts a new one at its next boot, then recalibrates).
2. Walk the records from area + 4 as in §14.2, stopping at the end rule.
3. For each id keep the **last** record whose check byte matches. A newer record supersedes an older one: with two
   records 106 in the log, demo_ble used the later one (it logged `xosc_l : 12`, the later value) and its compaction
   kept only the later one [M:c, M:d].
4. A record whose check fails ends the log: every record after it is invisible. V15, given a log with a bad record
   after id 106, used 106, did not see 107–187 behind the bad record, recalibrated, and compacted (§14.4) [M:t, M:d].
5. For records with an inner check (187, and per the vendor library 110 and 197 [M:s]), verify it too (§14.5).

A reader in our firmware must do the same and must never write the area (§15).

### 14.4 Compaction ("defrag") and erase

- At boot the VM compacts when the end of the log is past **60 % of the area**: demo_ble logs `warning_adr = 0x4ccc`
  (60 % of `0x8000`) and `vm_warning_line_check: arrived_warning_line` [M:c]. For V15 the threshold is probably
  `0x1333` (60 % of 8 KiB) [I]; measured only that a log ending at area offset `0x3ED5` compacted at boot.
- Compaction [M:d, both firmwares]: the other area is erased, the **latest record of each id** is copied into it in
  **ascending id order** (V15: 106, 107, 108, 109, 113, 187, then the filler id 200), the magic `55 AA AA 55` is
  written **last** (demo_ble logs `defrag_over_resume_magic`), and then the old area is erased in full.
- So after a compaction the live VM is area B (`0x095000` in V15) and area A is erased. On a well-used stock unit
  either area can be the live one.
- First boot with both areas erased: V15 erases `0x093000–0x096FFF`, writes the magic at `0x093000`, then appends its
  records (§5.3) [M:d]. demo_ble logs `vm_area_all_unused ------> vm_eraser` [M:c].
- Record 187 with a bad inner CRC (§14.5): V15 recalibrates in full and **appends** a new 187 after the old records
  (the old one stays in the log, superseded) [M:d].

### 14.5 The records an RF bring-up needs, and what each byte drives

Data lengths (without the 4-byte header) [M:d]; "drives" = the writes that changed when only that byte was changed
in a V15 second boot (§19.2) [M:t]. Register meanings stay [I] as in §2.

| Id | Data | Layout | Drives (V15 second boot) |
|---|---|---|---|
| 106 | 2 B | byte 0 = `xosc_l`, byte 1 = `xosc_r` (crystal load trims; demo_ble logs them, 11/11 in the emulator) | `0x11930` bits [16:13] ← byte 0, bits [22:19] ← byte 1, 4 bits each (`0x1F` stored lands as `0xF`); three RMW writes during the Wi-Fi analog init |
| 107 | 7 B | the seven `PA_C_I` values (demo_ble logs `PA_C_I: 1,7,4,7,11,1,7` = the bytes) | byte 0 → `0x11924` bits [26:24]; byte 1 → `0x11928` [2:0]; byte 2 → `0x11928` [11:9]; byte 3 → `0x11928` [17:15]; byte 4 → `0x11928` [25:22]; byte 5 → `0x1192C` [2:0] (widths from a `0x0F` probe); byte 6 → no analog register. **Every byte also changes 32 words of the RF-die LUT**: together they patch LUT entries `0xE0–0xFF` of both tables (word 0 and word 1, 64 words) |
| 108 | 20 B | Wi-Fi PA per-rate digital gain | 12 BBP window-D entries (§16.3), byte → entry map in §16.4; bytes 4, 6, 8, 10, 13, 15, 17, 18 drive nothing at boot. An all-zero record gives the firmware's defaults (entries get `0x20`, `0x2B` or `0x26`) |
| 187 | 68 B | 64 B payload + **CRC-16/XMODEM of the 64 B, little-endian, at bytes 64–65** + 2 bytes `00 00` | window-D/D' entries, `0x11908`, `0x1191C`, `0x11920`, `0x1195C`, `0x11960` and RF-die LUT words: §16.4. Any payload change with the inner CRC left stale makes V15 discard the record, recalibrate in full and append a fresh 187 [M:t, M:d] |
| 109 | 34 B | random-looking, different on every first boot (V15 `a1 55 5b 5b …`, demo_ble `a1 55 86 86 …`) | no RF write. Regenerated when lost [M:d]. The SDK name "BLE local info" (§11) fits [I] |
| 113 | 14 B | `06 1A 00 00` then 10 × `FF` (V15); `07 1A …` (demo_ble) | no RF write [I: host bonding table] |
| 110 | — | not written by V15 or demo_ble in the emulator | adding a record 110 (4-byte offset + CRC-16, in the VM or in BTIF) changed no write and left PLL_COMP at 0 [M:t, M:d]. Whether V15 reads it on hardware: unknown (§12 U14) |
| 102, 104 | — | not in the VM of either oracle. V15 keeps the classic address as **record 102 in the BTIF store at `0x0E9000`**, in the same record format but **without** an area magic (record at `0x0E9000` itself) [M:d] | a hand-made VM 102 or 104 changed nothing [M:t]. Changing the BTIF 102 record changed the BLE address: BLE address bytes 0–1 = CRC-16/XMODEM of the 6 classic-address bytes (little-endian), bytes 3–5 = classic bytes 3–5, byte 2 = not explained (2 cases) [M:d] |

Raw extract of the V15 VM after a first boot, and the decoded walk: [ble-traces/v15-vm-trim-map.txt](ble-traces/v15-vm-trim-map.txt).

---

## 15. Optimist's flash use against the stock VM and the factory data

Sources: `docs/MEMORY-MAP.md` and the code at optimist `3318260` (read only): `firmware/hal/fm1_flash.h` (the store's
allow-list `FL_STORE_OK`, the RAM driver's `FL_RANGE_OK`), `firmware/src/storage/storage.c` (object addresses),
`firmware/src/storage/sections/sec_log.c` and `sections.c` (the section log), `firmware/loader/ldr_core.c` and
`loader.c` (the update loader), `firmware/src/system/ota.c` (Optimist's own update entry), `tools/fm1_rescue.py`.
Tags here: **[V]** read in that source, **[M:d]** measured as in §14, **[M:hw]** read on a real FM-1 (§14.1).

### 15.1 Map

| Range | Stock V15 | Optimist | Overlap |
|---|---|---|---|
| `0x004000–0x092FFF` | app | app (the loader writes only here) [V `ldr_core.c` LDR_APP_HI] | replaced at install, by design |
| `0x093000–0x094FFF` | emulator: **VM area A**; FM-1: `FF` [M:hw] | free at `3318260`: outside `FL_STORE_OK` and the loader's write range [V]; UP_FM6's voices move here (`fix/upfm6-off-vm`, see §15.2) | none on hardware |
| `0x095000–0x096FFF` | emulator: **VM area B**; FM-1: `FF` [M:hw] | free, same | none |
| `0x097000–0x09EFFF` | — (demo_ble's 64 KiB VM would reach here; V15's does not) [M:d] | section log (projects) [V `sec_log.c` SEC_LOG_BASE] | none for V15 |
| `0x09F000` | — | autosave copy A | none |
| `0x0A0000–0x0DFFFF` | — | user samples, snapshots, banks, presets | none |
| `0x0E0000–0x0E4FFF` | — | Optimist's update staging, its record at `0x0E4F00` [V `ota.c`] | none |
| `0x0E7000` | FM-1: `FF` [M:hw] (a candidate for the VM's second area, not measured) | UP_FM6 copy A [V `storage.c` `OBJ_UPFM6`] | unknown (no VM seen there) |
| `0x0E8000` | **FM-1: the VM** (RF trims, §14.1) [M:hw] | **UP_FM6 copy B** [V `storage.c` `st_sector`: `0xE7000 + copy × 4 KiB`], inside `FL_STORE_OK` | **yes**: an FM6 user-preset save to copy B erases the VM |
| `0x0E9000` | **BTIF: record 102 = classic BT address** (§14.5) | never written (`FL_UPF_HI` / `FL_DLANE_HI` stop below it) [V] | none |
| `0x0EA000–0x0FBFFF` | stock's own settings (`0x0F2000`, `0x0F3000`, `0x0FB000` written at first boot) [M:d] | unused [V] | none (their content was not examined) |
| `0x0FC000–0x0FEFFF` | — | settings A/B, autosave B | none |
| `0x0FF000` | **key_mac** (factory address, if any) | never written (`FL_GLOB_HI` = `0x0FF000`, exclusive) [V] | none |

### 15.2 Verdict: which stock calibration records an Optimist install overwrites, and when

**Retracted: "None, in the code as it is" / "no Optimist path writes or erases there".** That verdict assumed the
emulator's VM location (`0x093000–0x096FFF`), where indeed no Optimist path writes. On a real FM-1 the VM is at
`0x0E8000` [M:hw, §14.1], and Optimist (at `3318260` and on the BLE branch) **does** write there:

- **UP_FM6** (the user presets' FM6 voices; `UP_FM6=1` in Optimist's user-default profile, and `CZ_NUSER` uses the
  same object) keeps A/B copies at `0x0E7000` / `0x0E8000` [V `storage.c` `OBJ_UPFM6`]. A save of an FM6 user preset
  that lands in copy B erases sector `0x0E8000`: the VM and every RF trim in it (106, 107, 108, 187; 109, 113).
  Whether this has happened on any unit was not measured; on the unit read, the VM was intact.
- `FL_STORE_OK` includes `0x0E5000–0x0E8FFF` [V], so the store's own guard allowed it.
- **Decision** (the user, 2026-10-08; optimist branch `fix/upfm6-off-vm`, in progress when written): UP_FM6's voices
  move to `0x093000–0x094FFF`; `0x0E8000–0x0E8FFF` is the SDK's VM and is never written; `0x0E9000` (BTIF) stays the
  SDK's; a build check and a runtime guard refuse writes to `0x0E8000–0x0E9FFF`.

The other paths, as before [V]:

- **At install** (stock → Optimist through M-UPGRADE): Optimist's update loader writes only `[0x4000, 0x93000)` and
  erases a sector in `[0x93000, 0xFC000)` (which includes `0x0E8000`) only when that sector's **last 256 bytes** hold
  a valid update record (`"TA"` tag at +6 and a matching CRC-16 over 78 bytes) [V `ldr_records_drop`]. A VM sector
  ends in erased bytes unless its log has grown to the last 256 bytes; even then a false match needs the tag and the
  CRC to match by chance (about 2⁻³²) [I]. On the unit read, the VM was there after stock's updater installed
  Optimist [M:hw].
- **At first boot**: the section log opens its first sector at `0x097000` [V `slg_boot` → `slg_open(0)`].
- **Settings, projects, samples**: inside `FL_STORE_OK`; of these only UP_FM6 (above) reaches `0x0E8000` [V].
- **The rescue tool** writes only `[0x4000, 0x93000)` [V `fm1_rescue.py`].

What can still lose them:

1. **Partly answered: the stock updater's own step 1.** On one unit the VM at `0x0E8000` survived stock's updater
   installing Optimist [M:hw]; where stock stages its loader is still not measured. When stock V15 starts the install it stages the package's update loader
   and writes its own update record somewhere in flash before resetting (Optimist does the same at `0x0E0000` /
   `0x0E4F00` when Optimist updates Optimist, `ota.c`). Where stock V15 stages it was not measured (the emulator does
   not run the M-UPGRADE exchange). **U16**, one unit: the VM survived.
2. **Optimist's plans for `0x093000–0x096FFF`** (UP_FM6's new home, app-slot growth, plug-ins): on the unit read these
   sectors hold no VM [M:hw], so they cost no calibration there. What still needs care is any future claim on
   `0x0E7000–0x0E9FFF` and on the as yet unknown second VM area.
3. **Returning to stock and back**: stock V15 keeps the VM; a stock restore that erases the package's declared VM
   region (`0x093000`, 352 KiB) would make V15 recalibrate at its next boot and write a new VM. Not measured which
   restore paths erase it [I].
4. A unit that ran a firmware with a larger VM (demo_ble-style 64 KiB at `0x093000–0x0A2FFF`) has VM data in
   `0x097000+`, which Optimist erases at first boot (`0x097000`) or later as the section log grows. V15 uses 16 KiB, so
   this concerns only units that ran such a firmware (older stock versions were not examined).

### 15.3 SLOOP 2.4 (a unit that went stock → SLOOP 2.4)

Source: `refs/sloop-upstream` at tag `v2.4.1` (read only): `firmware/hal/fm1_flash.h`, `firmware/src/storage.c`,
`firmware/src/eng_sample.c`, `firmware/src/editor.c`, `firmware/src/felucca.c`, `firmware/loader/*`; and
`fm1-firmware/docs/SLOOP24-BACKPORT.md`. All [V]; nothing measured.

| Range | SLOOP 2.4 use | Written when |
|---|---|---|
| `0x004000–0x092FFF` | app | install (same loader design as Optimist: app area only, plus the update-record sweep of §15.2) |
| `0x093000–0x096FFF` (the emulator's V15 VM; `FF` on the FM-1 read [M:hw]) | **not used**: outside `FL_STORE_OK` (`0x097000–0x0DFFFF`, `0x0E5000–0x0E6FFF`, `0x0E7000–0x0FAFFF`, `0x0FC000–0x0FEFFF`) and the OTA staging (`0x0E0000–0x0E4FFF`) | never |
| `0x097000–0x09EFFF` | projects, A/B object pairs (`storage.c`: `0x97000 + slot × 2 sectors`) | first save of each slot |
| `0x09F000` / `0x0FE000` | autosave copy A / B | autosave |
| `0x0A0000–0x0DBFFF` | USR1–USR3 samples (80 KiB each from `0xA0000`) | sample upload |
| `0x0DC000–0x0DFFFF` | user preset banks | preset save |
| `0x0E0000–0x0E4FFF` | update staging (`ota.c`) | an update started from SLOOP |
| `0x0E5000–0x0E6FFF` | FM6 patch bank (A/B) | FM6 bank save |
| `0x0E7000–0x0FAFFF` | **USR4** sample slot (80 KiB = `0x14000`) — **covers the FM-1's VM `0x0E8000` and BTIF `0x0E9000`** | see below |
| `0x0FC000–0x0FDFFF` | settings A/B | settings save |
| `0x0FF000` (key_mac) | not used (`FL_GLOB_HI` = `0x0FF000`, exclusive) | never |

USR4 and BTIF: `ED_SMP_BEGIN` erases only the slot's header sector `0x0E7000`; each `ED_SMP_WRITE` erases a sector
the first time data lands at its start; `ED_SMP_ERASE` erases the whole slot [V `editor.c` `ed_smp_erase`,
`ED_SMP_WRITE`]. So, by the same rule, **`0x0E8000` (the FM-1's VM, §14.1) is erased when a USR4 sample longer than 4,096 − 512 =
3,584 bytes of data is uploaded (or restored), or when USR4 is erased** [V rule, the VM's place M:hw], and
**`0x0E9000` (the BTIF record 102, the classic BT address) is erased when a USR4 sample longer
than 8,192 − 512 bytes of data is uploaded (or restored from a backup), or when USR4 is erased**. The backport notes
already flag this clash (SLOOP24-BACKPORT.md, "flash map clash … 2.4 writes USR4 straight across 0xE9000").

Verdict for a stock → SLOOP 2.4 unit:

- **RF calibration (VM 106, 107, 108, 187; 109, 113): retracted "very probably still there".** That assumed the
  emulator's `0x093000`. On an FM-1 the VM is at `0x0E8000` [M:hw], inside USR4: it is **lost if USR4 has ever held a
  sample over ~3.5 KiB or was erased**, else still there. No record 110 or 104 exists to lose (V15 does not write
  them, §14.5).
- **The classic address (BTIF record 102 at `0x0E9000`)**: intact unless USR4 has held a sample over ~7.5 KiB or was
  erased. Losing it costs no calibration: V15 generates a new random address when the record is missing (§11), and our
  firmware will choose its own address anyway. **key_mac `0x0FF000`**: untouched.
- **Reading it out**: put the FM-1 in UBOOT and run `tools/fm1_rescue.py FM-1.fwsc` **without** `--write`: it reads
  the whole 1 MiB, saves it, and writes nothing (§18.1); decode with §14.3. From SLOOP itself, its console has the
  same `flr OFFSET [LEN<=256]` command as Optimist (`console.c` at `v2.4.1`, range `0x93000–0xFFFFF`): 16 × `flr`
  from `0xE8000` read the VM's 4 KiB without UBOOT. Going from SLOOP 2.4 to Optimist: see §15.2 (UP_FM6).

### 15.4 Capture-and-keep plan (design note)

Goal: our BLE firmware always has the factory-calibrated trims, even after a later layout change.

1. **Read in place, never write.** At BLE start (or once at boot), scan the candidate areas per §14.3 (on an FM-1
   `0x0E8000` first; `0x093000` / `0x095000` for the emulator) through the
   plaintext flash window (`fl_plain_window_init`; data above `0x93000` is not SFC-encrypted, `SFCENC_UNENC_L`
   [V `fm1_flash.h`]). Take the latest valid 106, 107, 108, 187 (inner CRC checked). Never write or erase the VM: a
   user going back to V15 then finds its own calibration intact.
2. **Keep a copy in Optimist's store** the first time a complete set is found: about 101 data bytes (2 + 7 + 20 +
   66 + a version byte and a CRC). Candidate homes: the settings object (A/B at `0x0FC000` / `0x0FD000`) if its
   payload has the room (its limit is `ST_PAYLOAD_MAX` = 3,840 B per object [V]; the current size was not checked),
   or a new A/B object. The copy is used when the VM no longer has the records.
3. **Precedence**: VM (fresh) → Optimist's copy → none. With none, the firmware must run the live calibration (§16.2,
   not possible without the full sequence) or BLE stays off with a message; the VCO scan runs live in every case.
4. **The installer and loader are not the place**: the stock updater gives the host no flash read (the device pulls
   the package; nothing is pushed back), so the host installer cannot read the VM before the install; Optimist's
   loader could read it but writes only the app by design, and the VM is not at risk at install (§15.2). A backup of
   the whole flash before the first install is still worth recommending: `tools/fm1_rescue.py FM-1.fwsc` without
   `--write` reads and saves all 1 MiB from UBOOT (§18.1).
5. **Never reuse `0x0E8000–0x0E9FFF`** (the FM-1's VM and BTIF), nor the VM's second area once it is known
   (§15.2). Before any other layout change that could reach the VM, the build must first copy the trims (step 2) in a
   release that runs at least once before the region is erased.

---

## 16. RF start-up from the traces: constants, stored trims, live calibration

Stock V15, emulator `feat/ble-engine` 6531e20. "Boot 1" = fresh flash (full calibration, writes the VM); "boot 2" =
over boot 1's flash (stored trims present): the path our `rf_init` should follow. Three ways of telling the groups
apart, all [M:t] (method in §19):

- **constant**: identical writes in boot 1 and boot 2, and unchanged when the VM bytes or the model's analog outputs
  change;
- **from a stored trim**: changes when one VM byte changes (one run per byte, the other bytes and the model fixed);
- **live calibration**: changes when the emulator's analog model outputs change (the PLL comparator thresholds, the
  filter-calibration result, the BBP read-backs), with the VM fixed.

Boot 2 writes 34,274 words to the RF/BT ranges up to the BLE baseband init (two runs identical): 20,819 to the Wi-Fi
MAC/BBP window, 7,023 to the WL analog block, 6,194 to the WL control block (mostly the RF-die SPI port), 187 to the
BT configuration block, 31 to BR/EDR, 13 to the BLE baseband.

### 16.1 Boot-2 sequence, group by group

| # | Group | Volume (boot 2) | Kind | What `rf_init` must do, and where the values come from |
|---|---|---|---|---|
| 1 | Clocks (`0x10010`, `0x10008`) | 5 | constant | §5.1 (in the sheet already) |
| 2 | First analog word `0x11900` | 8 | constant (bit 14: see 16.2) | captured table (§17) |
| 3 | Wi-Fi clock/reset and radio config: `0x10010`, `0x14040–0x14050`, `0x1405C`, `0x30F00`/`0x30F04` | 37 | constant | captured table |
| 4 | **BBP + MAC first load**: 3,300 BBP transactions through `0x3101C` (6,600 port writes: command, then command with bit 17) + 24 MAC-window words (`0x30308`, `0x3030C`, `0x31004`, `0x31100`, `0x31104`, `0x31330–0x31348`) | 6,624 | constant: the first 3,300 transactions are identical in boot 1 and boot 2 | captured table. Structure in §16.3 |
| 5 | Crystal trim `0x11930` | 14 + 2 | **VM 106** | §14.5: bits [16:13] ← byte 0, [22:19] ← byte 1 (a 4-bit field each: a stored `0x1F` lands as `0xF`) |
| 6 | **Wi-Fi analog init**, `0x11900–0x11964` (20 registers) | 240 + 29 | constant, except bit 14 of `0x11900` | captured table; keep bit 14 of `0x11900` as found (16.2) |
| 7 | **VCO bank scan**: per step `0x11934`/`0x11938`/`0x1193C` set, `0x11968` start, `0x11978` strobed 8–9 times, comparator read from `0x11978` bits 17/18 | ≈380 steps in boot 2 | **live, every boot**: shifting the model's comparator thresholds changed 675 of the `0x11938` writes and 337 of `0x1193C` | must run live (an algorithm to write from the measured behaviour: §5.2's registers, the comparator bits; the sheet gives no vendor code). Its result also patches the RF-die LUT (group 11) |
| 8 | Post-scan analog set-up: final `0x11934–0x11940`, `0x11954`; `0x1191C`/`0x11920`; `0x11908`; `0x11904`/`0x11910`/`0x11920`/`0x1195C`/`0x11960`; analog re-init (28 words); `0x11910`/`0x11914` | ≈90 | mixed: **VM 187** (§16.4) into `0x1191C`, `0x11920`, `0x11908`, `0x1195C`, `0x11960`, `0x11910`; the rest constant | captured table + the 187 fields |
| 9 | PA trims `0x11924`/`0x11928`/`0x1192C` (written at the post-scan step and again at the LUT load) | 6 | **VM 107** | §14.5 table, bit positions: byte 0 → `0x11924` [26:24]; byte 1 → `0x11928` [2:0]; byte 2 → [11:9]; byte 3 → [17:15]; byte 4 → [25:22]; byte 5 → `0x1192C` [2:0] (widths from a `0x0F` probe: 3 bits, byte 4 4 bits) |
| 10 | **BBP second phase**: 7,048 transactions: the four window tables loaded again (identical to group 4's), the **VM 187** window entries (§16.4), and a window-D read-modify-write block with 539 read-backs through `0xD3` (entries `0x51`, `0x58`, `0x5C` ×384, `0x5D` ×128, `0x61`, `0x62`) | 13,901 port writes | tables constant; the RMW block **depends on the read-backs** (live on hardware; the emulator returns 0) | captured table for the reload; the RMW block as operations: read the entry, change a field, write (16.2) |
| 11 | **RF-die LUT**, SPI port `0x14028`/`0x1402C`: 2 × 256 words (cmd `0xE` word 0, cmd `0xD` word 1) | 512 words, ≈6,100 port writes with the kick toggles | constant base, three patch sets: **VM 107** → word 0 and word 1 entries `0xE0–0xFF`; **VCO scan** → word-1 entries in `0x00–0x7F` (bits about [15:9], a code falling with the entry index: 71 entries changed when the comparator was shifted); **VM 187** bytes 48–63 → 5 to 47 words each | captured base table; apply the three patches at run time |
| 12 | `0x11950` ×32, `0x11924` ×5, `0x1405C` ×3–5, `0x11938`, `0x11964` (around the LUT load) | ≈50 | constant except `0x11924` (VM 107) | captured table |
| 13 | **VM 108** → BBP window D entries | 36 transactions (12 entries) | **VM 108** | §16.4 |
| 14 | BT block (§5.4) incl. `0x2FC08` [9:0]/[19:10] and `0x2FC10` bytes 0–2 | 187 | constant, except the BT TX trims: **live** from BBP read-backs (`0xD3`) on every boot | §5.4 values; the trims from the read-backs (16.2) |

### 16.2 What stays live on the second boot (needs the real radio)

- **VCO bank scan** (group 7) and the LUT word-1 entries `0x00–0x7F` it patches.
- **Window-D read-modify-writes** (group 10): with the read-backs forced to `0x00`, `0x01`, `0x55`, `0xFF` the
  number of BBP port writes stayed 20,768 (no change of control flow) and only the written values changed: entry `0x61`
  is written with 2-bit fields cleared one after the other (`0xFC`, `0xF3`, `0xCF`, `0x3F` with a read-back of
  `0xFF`), `0x62` likewise, `0x51` and `0x58` similar, `0x5C` cycles a field through `0x10`, `0x20`, `0x00`
  (128 times), and `0x5D` receives bit 7 of a read-back (128 times). Read: a 128-step measurement loop whose results
  are read back and written; [I] DC/IQ trimming.
- **BT TX trims**: `0x2FC10` bytes 0 and 1 = a read-back shifted right by one (read-backs `0x01`, `0x55`, `0xFF` gave
  `0x00`, `0x2A`, `0x7F`); `0x2FC10` byte 2 = the negated read-back (`0x01` → `0xFF`, `0x55` → `0xAB`; `0xFF` → `0x00`,
  unexplained); `0x2FC08` [9:0] and [19:10] = the read-back (`0x01`, `0x55`; `0xFF` → 0). These are the `BT_TX_IQ` /
  `BT_OF` values demo_ble logs as 0 [M:c]. **Correction to §2.2**: in V15's boot 2 they do not come from the VM.
- **Bit 14 of `0x11900`**: set or clear depending on when a periodic routine last ran (also U13); not a trim.
- **Filter calibration**: live in boot 1 only. In boot 1, the model's filter result (`0x11978` bits [15:8] when
  `0x11968` bit 0 starts it) lands in `0x1195C`/`0x11960` bytes 1–3 and in `0x11910`/`0x11920` fields; in boot 2 a
  different model result changes nothing, and those registers come from VM 187 bytes 40–47 (§16.4).

### 16.3 BBP indirect windows

All BBP traffic of groups 4, 10 and 13 goes to 17 BBP registers. Four of them form each of four windows [M:t]:

| Window | Commit (always written 1, last) | Entry address | Data | Read-back (read before ever written) | Entries in the first load |
|---|---|---|---|---|---|
| C | `0xC8` | `0xC9` (0x00–0x7A) | `0xCA` | `0xCB` (boot 1 only) | 893 transactions |
| C' | `0xCC` | `0xCD` (0x00–0x47) | `0xCE` | — | 73 |
| D | `0xD0` | `0xD1` (0x00–0x5F) | `0xD2` | `0xD3` (boot 1 and 2) | 101 |
| D' | `0xD4` | `0xD5` (0x00–0x1C) | `0xD6` | `0xD7` (boot 1: 13,740 reads) | 27 |

Order in the trace: address, data, commit. Direct registers in the first load: `0x15`, `0x16`, `0x19`, `0x54`, `0x6A`.
That the read-back registers return the addressed entry is [I] (the emulator returns 0 for them): U17.

### 16.4 The stored trims, byte by byte (VM 187 and 108)

**VM 187**, 64-byte payload (inner CRC fixed after each change; xor `0x03` per byte) [M:t]:

| Bytes | Drives |
|---|---|
| 0, 4, 8, 12 | window-D entry `0x61`, 2-bit fields [1:0], [3:2], [5:4], [7:6] |
| 16, 20 | window-D entry `0x62`, fields [1:0], [3:2] |
| 1, 5, 9, 13, 17, 21 | window-D entries `0x04`, `0x05`, `0x06`, `0x07`, `0x08`, `0x09`, bits [7:6] |
| 2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23 | nothing (the payload looks like 16-bit or 32-bit little-endian words with only the low byte(s) used) [I] |
| 24 | `0x1191C` bits [9:8] (three writes) |
| 25 | `0x11920` bits [1:0] |
| 26, 27, 28, 29 | window-D' entries `0x09`, `0x0B`, `0x0A`, `0x0C` |
| 30, 31, 34, 35, 38, 39 | nothing |
| 32 | `0x11908` field at bit 7 (seven writes) |
| 33 | `0x11908` field at bit 15 |
| 36 | `0x11908` field at bit 17 |
| 37 | `0x11908` field at bit 25 |
| 40, 41, 42, 43 | `0x1195C` bytes 0, 1, 2, 3 (byte 42 also `0x11910` bits [15:14] and `0x11920` bits [11:10]) |
| 44, 45, 46, 47 | `0x11960` bytes 0, 1, 2, 3 |
| 48–51, 52–55, 56–59, 60–63 | RF-die LUT words (each group of four changes 5, 47, 6 and 38 LUT words respectively) |

In the emulator's boot 1 the payload is mostly zero, `0x40 0x40` at 22–23 (unused at boot 2), `FF` × 8 at 40–47 (the
model's filter result 255) and `00 02` × 8 at 48–63: these are emulator results, not an FM-1's (U2).

**VM 108** (20 bytes): each non-zero byte replaces a window-D entry's default; a zero byte keeps the default.
Measured with increasing and with decreasing marker values (same map both times), and with `0x20 0x7F 0x80` (written
verbatim):

| Byte | 0 | 1 | 2 | 3 | 5 | 7 | 9 | 11 | 12 | 14 | 16 | 19 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Window-D entry | `0x0B` | `0x0D` | `0x0F` | `0x11` | `0x1B` | `0x1D` | `0x1F` | `0x21` | `0x13` | `0x15` | `0x17` | `0x19` |
| Default (byte 0) | `0x20` | `0x20` | `0x20` | `0x20` | `0x20` | `0x2B` | `0x26` | `0x20` | `0x20` | `0x2B` | `0x26` | `0x20` |

Bytes 4, 6, 8, 10, 13, 15, 17 and 18 changed no write at boot.

### 16.5 What the emulator cannot show, and the smallest hardware experiment for each

| Not shown | Why | Minimal experiment |
|---|---|---|
| VCO bank chosen, LUT word-1 `0x00–0x7F` | comparator thresholds are one FM-1's, measured once | read-back build (§18.3 step 3): after the scan, read `0x11934–0x1193C` and the LUT (SPI cmd `0x6`) |
| Window read-backs (`0xCB`, `0xD3`, `0xD7`) and every RMW on them, the BT TX trims | model returns 0 (U17) | read-back build: write a window entry, read it back; then dump `0x2FC08`/`0x2FC10` after `rf_init` |
| Real 187/106/107/108 contents | the emulator's calibration results are model outputs | §18.1 on two units |
| Whether stored trims + live VCO scan give a clean carrier | no radio | §18.3 step 4 (U1) |
| Filter-calibration result | fixed at 255 in the model | boot-1 path on hardware only if a unit has no VM (§18.3 step 7) |

---

## 17. Capturing the long tables at build time (no transcription)

The constant parts of §16 are long: about 3,300 BBP transactions for the first BBP load and 3,411 for the reload,
240 analog-init words, 128 AGC words and 2 × 256 RF-die LUT words. The repository carries none of them. Proposed:

### 17.1 The capture tool (to be written by an implementer from this description)

`tools/ble_rf_capture.py` (name proposed), run by the build when BLE is selected:

1. **Input**: the user's own `FM-1.fwsc` (stock V15). Refuse anything whose SHA-256 is not
   `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` (the value `tools/fm1_rescue.py` already
   checks). The tool never downloads it.
2. **Run** the emulator's `examples/diagnose` twice with the trace variables of §13: a first boot (fresh flash, writes
   the VM), then a second boot over the first boot's flash (`FM1_FLASH_RESTORE`), with
   `FM1_MMIO_RANGES=10000-100ff,11900-1197f,14000-14067,2fc00-2fdff,30000-31fff` and about 2 × 10⁸ instructions. Use the second
   boot: it is the path our firmware follows when the stored trims are present (§16.1).
3. **Cut the boot-2 write trace into the groups of §16.1 by register patterns, not by vendor PCs**:
   - BBP: decode every `0x3101C` write with bit 17 set into (register, data, read/write); fold each indirect window
     (§16.3) into (window, entry, value) triples in order. The first load ends at the first write to `0x11930`; the
     reload is the next run of window-C transactions after the VCO scan.
   - Analog init: the writes to `0x11900–0x11964` between the first BBP load and the first VCO-scan strobe
     (`0x11978` = 1); mask bit 14 of `0x11900` (§16.2) and the `0x11930` trim fields (§14.5).
   - AGC: the 128 data words written to `0x2FD9C` after `0x2FD98` = 0.
   - RF-die LUT: pair each `0x1402C` data word with the next `0x14028` command (`0x10000 | entry << 8 | cmd`, cmd
     `0xE` = word 0, `0xD` = word 1) → 2 × 256 words; mark word-0/word-1 entries `0xE0–0xFF` as "patched from VM 107" and
     word-1 entries `0x00–0x7F` as "patched from the VCO scan" (§16.2) so the firmware applies those at run time.
4. **Output**: `build/generated/ble_rf_tables.h` (git-ignored), as plain arrays of (address, value), (window, entry,
   value) and LUT words, plus each group's count. The firmware's `rf_init` includes it only in BLE builds; without
   it the BLE build fails with a message naming the tool (no silent fallback).
5. **Self-check**: the repository may keep the SHA-256 of each extracted group (a hash is not the table). A mismatch
   means the emulator or the trace format changed; the tool stops.

The emulator reproduces the firmware's writes exactly (§0): the table values are the stock firmware's own writes,
captured the same way on every machine. Two boot-2 runs gave identical write sequences (34,707 writes) [M:t].

### 17.2 Note on why (not legal advice)

- **What it changes**: the GPL repository then contains no vendor-derived table, only a description of where the
  values come from and a tool that observes the user's own copy of the stock firmware running in an emulator. That
  is cleaner for the **source** than transcribing the values into it.
- **What it does not change**: a compiled BLE firmware still contains the values. Whoever distributes BLE binaries
  distributes them. Options: publish source only for BLE builds and let the builder capture on the user's machine
  (Optimist already builds locally), or get advice before shipping BLE binaries.
- Whether register initialisation values like these are protected at all (functional data needed for
  interoperability with the chip, versus a compilation) depends on the jurisdiction; several have interoperability
  exceptions. This is a question for a lawyer; the tool keeps the option open either way.

---

## 18. Hardware: reading the VM, capturing on the device, bring-up order

Additions for the hardware test plan, BLE-MIDI-FEASIBILITY.md §9.6 (§18.4 says what to add there). Nothing here ran on
hardware.

### 18.1 Reading the VM of a stock-only FM-1 (before anything else is installed)

1. **Whole-flash backup through UBOOT, writing nothing**: put the FM-1 in UBOOT (stock V15's soft key
   `F0 22 24 35 7D F7`, as reported in docs/FM1-SCENE-2026-10.md §"Ways into UBOOT" [R there], or the other ways
   listed there), then `sudo …/python tools/fm1_rescue.py FM-1.fwsc` **without** `--write`. It checks the chip
   (key `980F`, flash `856014`), reads all 1 MiB and saves `fm1-backup-YYYYMMDD-HHMMSS.bin` next to the package
   [V `fm1_rescue.py`]. Power-cycle to leave UBOOT [I].
2. **Decode** the backup with §14.3: the live area (on an FM-1 `0x0E8000`, §14.1; the emulator: `0x093000` or `0x095000`), records 106, 107, 108, 187 (inner CRC),
   plus BTIF `0x0E9000` (record 102) and `0x0FF000` (key_mac: all `FF`, or a factory address, U4). Keep the backup:
   it is also the only copy of the factory state.
3. A unit already on SLOOP 2.4 (or Optimist): the same UBOOT backup works whatever firmware runs; without UBOOT, the
   CDC console's `flr 0xe8000 256` … `flr 0xe8f00 256` (16 calls) reads the VM's 4 KiB (§14.1).
4. Record per unit: which area is live, the data of 106/107/108/187, whether 187's inner CRC holds, the log length
   (how close to the 60 % compaction line). Two units at least, to see whether the trims differ per unit (U2).

### 18.2 Capture on the device

There is no MMIO trace of stock on hardware: the FM-1 exposes no debug port in the documented paths, and stock has no
console. What exists:

- **Flash reads**: §18.1 (UBOOT, any firmware), and in Optimist the CDC console's `flr OFFSET [LEN<=256]`, which reads
  `0x093000–0x0FFFFF` over SPI [V `console.c` `con_flr`]; 16 calls from `0xe8000` dump the VM's 4 KiB.
- **Our own firmware's view of the radio**: a BLE build can add read-only console commands (proposed, not existing):
  a word peek limited to `0x11900–0x1197C`, `0x14000–0x14064`, `0x2FC00–0x2FCBC`, `0x2FD80–0x2FD9C`,
  `0x28000–0x28054`; a BBP window read (write the entry address, read the window's read-back register, §16.3); an
  RF-die register read (SPI command `0x6`, §5.2). After `rf_init`, dump them to compare with the captured tables and
  to read what the emulator cannot give: the VCO bank chosen, the window-D read-backs (§16.2), the BT trims in
  `0x2FC08`/`0x2FC10`.

### 18.3 Order of the RF bring-up experiments

Each step only after the previous passed. "Sniffer" as in §12.

1. **Stock baseline** (feasibility §9.6 as written) and the **flash backup** (§18.1) on a unit still on V15.
2. **Install Optimist (no BLE)**, then `flr 0xe8000 256`: the VM must still be there (U16; one unit: it was). If it
   is gone, the current tools cannot put it back (the rescue tool never writes above `0x93000`): note it, and use
   the backup's values through the copy of §15.4.
3. **Read-back build**: Optimist + `rf_init` (captured tables, trims from the VM) + the read-only console commands,
   **no transmission**. Check that every register reads back what was written, that the window read-back register
   returns the entry written (U17), and that the VCO scan ends with a bank (compare `0x11934–0x1193C` with the
   emulator's).
4. **Carrier**: fixed-channel transmit (column 6 = `0x2000 | ch`, state 6; §12 U1) on channel 19 (2,440 MHz) with
   the trims from the VM; a DTM receiver or spectrum view: carrier present, frequency offset, PER. Then the same with
   the VM trims replaced by the all-zero / default values, to see what each trim buys (U1, U14).
5. **Advertising**: ADV_IND on 37/38/39 seen by nRF Connect; RSSI at 1 m against stock's (§9.6 step 1).
6. **Scan response, connection, data**: U5, U6, U7 with the sniffer, then BLE MIDI.
7. **Soak**: link from cold to warm (U13); a fresh-flash unit (no VM) to test the "none" path of §15.4.

### 18.4 What to add to BLE-MIDI-FEASIBILITY.md §9.6

- A step **0** before step 1: "Back up the whole flash through UBOOT with `tools/fm1_rescue.py FM-1.fwsc` (no
  `--write`) and keep the file; decode the VM per BLE-HW-FACTS §14.3 (§18.1)."
- A step after the first Optimist install: "`flr 0xe8000 256` on the console: it starts `55 AA AA 55` and the
  records 106/107/108/187 are there (BLE-HW-FACTS §14.1, §15.2, U16)."
- A pointer: "RF bring-up on our own firmware follows BLE-HW-FACTS §18.3."

---

## 19. How §14–§16 were measured

Emulator `fm1-emulator` branch `feat/ble-engine` at 6531e20, `examples/diagnose`, with the variables of §13
(`FM1_MMIO_TRACE`, `FM1_MMIO_RANGES` as in §13, `FM1_FLASH_DUMP`, `FM1_FLASH_RESTORE`, `FM1_RAM`). Stock V15 runs:
2 × 10⁸ instructions (boot 2, ≈10 s wall time each) or 10⁹ (boot 1).

### 19.1 VM format (§14)

- Walk the dumps of §13 (V15 and demo_ble, first boot) and fit the header: id and length fields from the known ids
  and demo_ble's logged `idx_len`; the check byte by trying CRC-8 (all polynomials, both initial values,
  reflected or not), CRC-16 low/high byte (`0x1021`, `0x8005`, `0x3D65`, `0x0589`, `0x8BB7`), sums and XOR over
  data, header + data, header only. One rule fits all 8 records.
- Hand-built images restored over the package, then the dump compared: a log filled to 60–99 % of area A (both
  firmwares), two records with the same id, both area magics valid, a bad check byte in the middle of the log, a
  dropped 187, a 187 with a stale inner CRC.

### 19.2 Byte → register map (§14.5, §16.4)

Base: V15's boot-1 flash. For each run one VM byte is changed (xor `0x03`; for 187 the inner CRC is recomputed;
for 106/107/108 also whole-record marker values), the check byte fixed, V15 booted over it, and its write sequence
compared with the unmodified boot 2 register by register (n-th write to an address against the n-th). BBP
transactions are compared with the window entry address as context (§16.3). 76 + 65 runs.

### 19.3 Live calibration (§16.2)

A local, uncommitted change to the emulator's wireless model (one file, in a scratch worktree) adds
`FM1_RF_PERTURB=bbp=HEX,filter=N,pll=N`: XOR the value returned by BBP registers the firmware never wrote (the window
read-backs) with HEX; replace the filter-calibration result 255 by N; shift both PLL comparator thresholds by N.
Boot 1 and boot 2 were run with `filter=200`, `pll=3`, `bbp=0x01`, `bbp=0x55`, `bbp=0xFF` (boot 1 with `bbp=0x15` ran
away into a 1.9-million-transaction loop and was not used). Writes that change are "live"; the rest are constants
or stored trims.

Scripts (scratch, not in the repository): VM walker and CRC search, image builder, per-byte patcher, write-sequence
diff, BBP window diff, LUT extractor. Each is under 150 lines and reproducible from this description.

---

## 20. Keeping V15's radio tables at install

The idea: the user installs stock V15, then Optimist over it; before it writes anything, our update loader
(`firmware/loader`) copies the radio tables out of V15's app area into a flash sector that later updates leave alone,
and `rf_init` reads them from there. Answer: **partly feasible**. About half of §17's constant data is stored in V15
as six plain tables (3,800 B) and can be kept this way; the other half exists only as immediates in V15's code.

Method: the boot-2 write trace (`tools/ble_rf_capture.py --keep`, which records the writing PC) gives the code that
writes each group; its table references were read in the V15 disassembly; each candidate table was then compared word
for word with the generated `ble_rf_tables.h` (SHA-256 `30ba97ea…`, the pinned one) and decrypted from the
emulator's installed flash image with the loader's own SFC routine. Tags as in §0.

**Addresses.** XIP `0x02000000` = flash `0x4000` (the 0x120-byte app-area head); `app.bin` offset 0 = XIP
`0x02000120` = flash `0x4120`. So flash = XIP − `0x02000000` + `0x4000`, and `app.bin` offset = XIP − `0x02000120` [M:s].

### 20.1 What V15 stores as tables

| # | Table | XIP | flash | `app.bin` | Size | Format, and how it reaches the radio | CRC-32 (fingerprint) |
|---|---|---|---|---|---|---|---|
| T1 | RF-die LUT (group 11), osc ≠ 40 MHz | `0x02052CB4` | `0x56CB4` | `0x52B94` | 2,048 B | 256 entries × (word 0, word 1), u32 LE: word 0 goes with SPI cmd `0xE`, word 1 with cmd `0xD`, entries 0→255. Base verbatim, patched as 20.2 | `8c7d5afb` |
| (T1') | same, osc = 40 MHz | `0x020534B4` | `0x574B4` | `0x53394` | 2,048 B | the variant chosen when the clock named `osc` is 40,000,000 Hz; the emulator boot used T1 (FM-1 crystal 24 MHz [I]). Not needed | `48b5818d` |
| T2 | AGC (`0x2FD9C`) | `0x0205C1D4` | `0x601D4` | `0x5C0B4` | 512 B | 128 u32 LE, verbatim; written as 64 pairs from the last pair to the first, each pair in order (pair 63's two words first). 128/128 words match | `fb84c808` |
| T3 | BBP window C load (group 4, replayed in group 10) | `0x0204D9C4` | `0x519C4` | `0x4D8A4` | 512 B | 128 u32 LE. For entry *i*: window C `0x2F` ← *i*; `0x2B` ← byte 0 clamped to −64…63 (signed); `0x4E`, `0x54`, `0x55` ← bytes 1, 2, 3 verbatim; `0x2F` ← *i* \| `0x80`. This is the first 768 of window C's 893 transactions in the first load | `6216b488` |
| T4 | window-D read-modify-write loop (group 10, the part §17 leaves out) | `0x0204D7C4` | `0x517C4` | `0x4D6A4` | 512 B | 128 u32 LE, one per loop step; each is limited to 511, its bits [7:0] go to window-D entry `0x5E` (128 writes in boot 2 [M:t]; §16.2 did not list this entry) and bit 8 into another field of the same step | `394893f8` |
| T5 | VM 108 defaults (group 13) | `0x0204EA6A` | `0x52A6A` | `0x4E94A` | 20 B | the 20-byte image a zero VM-108 byte leaves in place (§16.4, "Default") | `5908ed2e` |
| T6 | analog bits of `0x11910` / `0x11914` (group 8) | `.data` `0x01C080C4`, image at `0x0208C124` | `0x90124` | `0x8C004` | 4 B per index; indices 0–48 used, so ≥ 196 B [I] | byte 0 → `0x11910` bits 22, 23, 24; byte 1 → bits 25, 26; byte 2 → `0x11914` bits [17:15], 12, 13, 14; byte 3 → `0x11914` [7:5]. Boot 2 uses one index (one call, 10 writes) | `16297b90` (196 B) |

Total to keep: T1–T6 = **3,800 B** (5,848 B with T1'). All six decrypt from the raw flash with the chip key, at the
offsets above, to exactly `app.bin`'s bytes [M:d, emulator flash image].

### 20.2 The LUT's run-time patches (needed once T1 is used unpatched)

T1 is the base; V15 patches it while loading it [M:s, field positions]. The generated header holds the LUT with the
emulator's patches already in (`BLE_RF_LUT_TRIMMED`); with T1, `rf_init` applies them itself:

| Word, entries | Patch |
|---|---|
| word 0, `0x00–0x7F` | none (all 128 match T1) |
| word 0, `0x80–0xDF` | bits [11:0] kept; [31:22] ← bits [9:0] of one 16-bit value, [21:12] ← bits [9:0] of another; one pair per entry range `0x80–0x84`, `0x85–0xB3`, `0xB4–0xB9`, `0xBA–0xDF`. The range sizes 5 / 47 / 6 / 38 are §16.4's "5, 47, 6 and 38 LUT words" of VM 187 bytes 48–51 / 52–55 / 56–59 / 60–63 [I: the pair = those 4 bytes] |
| word 0, `0xE0–0xFF` | bits [5:0] ← (byte 5) \| (byte 6 << 3) of the VM-107 copy [I: 107] |
| word 1, `0x00–0x7F` | a code from the VCO scan OR-ed in at bit 9, looked up by bits [28:21] of the word (§16.2) |
| word 1, `0xE0–0xFF` | kept bits `0xC06071FE`; byte 0 at bit 9, byte 1 at 15, byte 2 at 18, byte 3 at 23, byte 4 at 26 of the VM-107 copy |

Measured against the header: word 0 differs only at `0x80–0xFF` (xor `0x80200000` on 96 entries, `0x00000001` on 32),
word 1 only at `0x00–0x7F` and `0xE0–0xFF` [M:d].

### 20.3 What exists only as code

Everything else in §16.1 is produced by V15's code as instruction immediates: single register writes and
read-modify-writes with immediate masks, and calls to the four window helpers with immediate (entry, value) pairs
(window C' alone has 73 call sites, one per transaction) [M:s]. No smaller data part exists in V15 to capture or
regenerate from: the immediates *are* the data. Counted in the generated program (boot 2):

| Group | Writes from code |
|---|---|
| 2, 3: first analog word, radio config `0x1404x`/`0x30F0x` | 48 |
| 4: MAC-window words; direct BBP; window C (125 of 893), C' (73), D (101), D' (27) | 20 + 18 + 326 |
| 5–8: crystal trim, Wi-Fi analog init, scan set-up, post-scan analog | 16 + 267 + 10 + 22 |
| 10: analog re-init and MAC words, window writes outside the replay | 86 + 73 + 8 |
| 11: around the LUT load (`0x11950`, `0x11924`, `0x1405C`…) | 53 |
| 14: BT block (§5.4; not in the generated header) | 187 |

That is 947 values in groups 2–11 plus the BT block (≈ 4 KB in the header's program encoding; packed values only, ≈ 2–3 KB [I]) that would still
have to be shipped or captured at build time. A loader could only get them by decoding immediates out of V15's
instruction stream at pinned code offsets (≈ 1,000 sites, the read-modify-write semantics re-implemented): possible
since V15 is fixed, but large and brittle; not recommended. Running V15's init on the device does not help: its writes
go to the real registers and cannot be trapped.

### 20.4 The install path from stock V15

1. Stock V15's updater runs first (step 1, stock code): it pulls the package, stages our `ota.bin` in its own
   loader/VM flash area, writes the update record and soft-resets; the SPL then loads our loader into RAM and runs it
   (FM-1-RE `docs/io/11-ota-protocol.md`; Optimist `BUILDING.md` "Package identity": installing a `FM-1_7XY` package
   from V15 is **not verified on hardware**).
2. **The app area is intact when our loader starts.** Step 1 writes only the loader and the record: in FM-1-RE's
   2026-07-21 hardware runs (stock V13), step 1 and a loader that then wrote nothing left the stock app booting
   unchanged ("nothing was actually flashed"). Where stock stages the loader and the record is not recorded; our
   loader sweeps all of `[0x93000, 0xFC000)` for it (`ldr_records_drop`). For V15 the same flow is [I].
3. **Our loader can read it.** `fl_read_ram` is a raw SPI read (`0x0B`, SFC switched off) and returns the encrypted
   bytes. The decrypt is already in the loader: `ldr_sfc` (≈ 12 lines; the block offset is relative to flash
   `0x4000`, blocks of 32 B, so a read starts at a 32-byte boundary of that offset) and `ldr_chip_key` (the key from
   `isd_config.ini` in the flash head; `0x980F` on the FM-1). Verified: the port of `ldr_sfc` turns the raw sectors of
   the emulator's flash image into T1–T6 exactly. No extra C code is needed for the cipher.
4. **XIP view**: not needed, and not known to be set up for the app area while the loader runs (the SPL started the
   loader, not the app; unmeasured).
5. Our loader overwrites the app area sector by sector from `0x4000` up (equal sectors skipped). T1–T6 lie in sectors
   `0x51000–0x52FFF`, `0x56000–0x56FFF` (`0x57000` with T1'), `0x60000` and `0x90000`. So the copy must be complete and
   verified **before the first sector is written**.

### 20.5 Where to keep the copy

**`0x94000–0x94FFF`** (one 4 KiB sector): the upper half of the app slot's 8 KiB room to grow (docs/MEMORY-MAP.md
in optimist), so the slot can still grow by 4 KiB contiguously into `0x93000`; below UP_FM6 (`0x95000`); outside the
store's allow-list; inside the loader's allowed range (`FL_RANGE_OK` `[0x93000, 0xFC000)`); away from `0xE7000–0xE9FFF`.
Content: a storage object of ours (the "FELU" header with its CRC-32, so `ldr_ours()` keeps the record sweep off it)
holding T1–T6 (3,800 B + 32 B header < 4,096 B), written plain; the app reads it through the plain XIP window that
starts at `0x93000` (`fl_plain_window_init`). Needed elsewhere: `fm1pkg_make.py` limits `FLASH_SIZE` below `0x94000`
instead of `0x95000`; MEMORY-MAP gets the row. Stock's own staging area being unknown (20.4.2), the loader must not
write the sector when its last 256 B hold a valid update record (the test `ldr_records_drop` uses).

The copy does not need to survive a trip back to stock: the next install over V15 makes it again.

### 20.6 Verdict

- **Feasible for T1–T6** (LUT base, AGC, window-C load table, window-D loop table, VM-108 defaults, the `0x11910` /
  `0x11914` byte table): 3,800 B, about half of §17's constant data and every *long* table except the program.
  Loader work: before the first write, if `0x94000` holds no valid copy, read the six regions raw, decrypt
  (`ldr_sfc`), check each against its pinned CRC-32 above (this is how the loader knows the app area is V15; any
  mismatch: no copy, the install goes on, BLE reports "no radio tables"), write and verify the object, then start the
  normal pass. Idempotent across the loader's retries and a power loss (the app area is untouched until the copy
  verifies). About 150 lines in the loader (+≈ 0.6 KB; `loader.bin` is 8,000 B, `ota.bin` 6,739 B, against the 19,456 B FM-1-RE saw stock step 1 stage),
  ≈ 100 in `rf_init` (reader + 20.2), tests in `tests/ldr_test.c`, and the capture tool drops those groups: 2–3 days
  plus one hardware install from V15.
- **Not feasible for the rest** (20.3): ≈ 950 register / window values (plus the BT block) exist only as V15 code immediates. They still
  come from the build-time capture, so a BLE binary still contains vendor values; this idea alone does not reach "no
  vendor bytes in our binaries".
- **A unit already on Optimist** has no V15 app area to copy from. It must go back to V15 once (Optimist stages the
  stock loader, which writes the V15 package; back up first, MEMORY-MAP "back to stock V15") and then install
  Optimist again. A lighter alternative for those units [I]: the installer, on the user's computer, decrypts T1–T6
  from the user's own `FM-1.fwsc` (it already parses the package) and serves them to the loader; nothing vendor-made
  would be in our repository or binaries that way either.

---

## 21. Central role (scan / initiate / master) and reconnection

Read 2026-10-09 for the goal "the FM-1 connects to a BLE-MIDI device chosen from a HOME-menu list, remembers it and
reconnects". Sources: the vendor library's LLVM IR (`btctrler.a`: `RF_ble.c`, `RF_ble5.c`, `ll_scan.c`, `ll_init.c`,
`ll_master.c`, `link_layer.c`, `ll_adv.c`, `ll_resolve_list.c`, `multilink_schedule.c`; `lib_ccm_aes.a`: `aes.c`) with
its debug info, and the SDK's application configuration source. Tags: **[IR]** as in §8.2 (what the vendor code does,
no code quoted); **[SDK]** = the SDK's readable application source (`apps/common/config/log_config/lib_btctrler_config.c`,
`apps/demo/demo_ble/include/app_config.h`); **[I]** = inferred. Nothing in this section ran in the emulator or on an
FM-1. "Instance n" = one link: its control block (§3), its columns (`(column << 10) | (n << 4)`, §2.3), its event bit
n and RX bit 8 + n (§2.1), its instance-table entry (§4).

### 21.1 Summary

- No new registers. A central uses the same control block, column port and interrupts as the peripheral; states 1
  (scan), 3 (initiate) and 6 (master) in column 2 [IR].
- The engine answers within T_IFS by itself: a **SCAN_REQ** after an ADV_IND / ADV_SCAN_IND when FORMAT bit8 is clear,
  and the pre-loaded **CONNECT_IND** after an ADV_IND / ADV_DIRECT_IND from the programmed target. Software never touches
  a TX buffer, header or column between those receptions and its own reaction [IR]; that the engine sends them is [I,
  strong: 150 µs, as §6 for SCAN_RSP].
- Software does everything else: the CONNECT_IND contents (AA, CRCInit, Hop, WinOffset), all of it **before** initiating
  starts; the scan-channel rotation; the active-scan backoff; whitelist, duplicate and address filtering beyond one
  address; RPA resolution; supervision timeout; every LL procedure [IR].
- The hardware "whitelist" is **one address** (WHITELIST0L/M/U + FILTERCNTL bit8 for its type). There is no IRK or
  resolving list in the engine [IR].
- Master differs from slave only in the set-up (anchor counter written, no widening), the steady receive window and the
  update-instant column 4 formula. The RX rule (§8) and the TX rule (§8.2) are the same code for states 6 and 7 [IR].

### 21.2 Scanning (state 1)

**Start, in the vendor's order** [IR]:

1. RFPRIO 17. (The vendor's priority setter always writes the same value to RFPRIOCNTL and RFPRIOSTAT.)
2. Column 8 = 0 (advDelay off).
3. Window and interval in 625 µs slots (the HCI units). Software adjusts them first: if window + 4 > interval, window =
   interval − 4 (interval > 4), else interval 5 / window 1; it keeps idle = interval − window for the scheduler (21.5).
4. WINCNTL0/1 = window × 625 µs (low / high 16 bits), WINCNTL2 = 50; column 1 = interval, column 15 = interval >> 16
   with **bit15 = 0** (advertising and connections write bit15 = 1; the meaning of bit15 is not known, the scan still
   repeats every interval, 21.9).
5. FILTERCNTL bit3 = 1 for scanning filter policy 1 or 3, else 0 (other bits kept). The hardware whitelist entry is not
   programmed for scanning: the vendor filters the reports against its whitelist list in software.
6. Active scan: FORMAT bit8 cleared (SCAN_REQ allowed); passive (or privacy on): FORMAT bit8 set. It is the same bit as
   the advertiser's "ignore SCAN_REQ" (§3).
7. Column 6 = `0x2100 | 37` (first channel 37; `0x2000 | ch` is the fixed-channel test mode).
8. Two RX buffers with room for 263 payload bytes, RXPTR0/1 = their payload offsets (§3, §4).
9. Two TX buffers (20 payload bytes) both holding a **SCAN_REQ**: type 3, TxAdd = own address type, length 12, payload =
   ScanA (own address) followed by **six zero bytes** where AdvA goes; TXPTR0/1, TXAHDRn (type | TxAdd << 4) and TXDHDRn
   (12 << 8) set as for advertising. No software path ever writes the AdvA bytes, so the engine must insert the AdvA
   of the advertiser it answers [I, 21.9].
10. Column 2 = `0x1000` (state 1).
11. OPTCNTL: bit9 cleared, bits 10 and 11 set (as at connection set-up).
12. Active only: the backoff state reset (below).
13. Interrupt bits enabled as §6 step 11; when it is the only open instance, the anchor counter is started at once
    (column 7 = 0, 0 = 0, 14 = 0, 0 = 0, 14 = `0x8000`); otherwise the scheduler starts it (21.5).

**Each event (IRQ 45, state 1)** [IR]: column 6 = `0x2100 | ch` with the software's current channel, then the channel
advances 37 → 38 → 39 → 37 (not in fixed-channel mode). The first event writes 37 again, so a value written in the event
ISR is for the next window [I]. RFPRIO: 17, and 26 on every 6th event (30 if a vendor flag is set; it is 0). The vendor
handles this event only when both the scan and the initiate roles are compiled in.

**RX (IRQ 29, state 1)** [IR]: the **connection** path of §8 (RXTOG consistency loop; the RXTOG buffer first, then the
other; each only if its RXBUFnCNTL bit0 = 1; bit0 cleared after taking it), **not** the advertising path. That bit0
marks a filled buffer while scanning is unmeasured; §8.1 found it does not while advertising. The payload is copied out
of the fixed buffer; RXPTRn is not rewritten. A report is built from the control block: RXAHDRn [3:0] PDU type, bit5
ChSel, bit6 TxAdd, bit7 RxAdd; RXDHDRn [15:8] length; payload at RXPTRn = AdvA (6 bytes) then AdvData; RXSTATn
[3:0] = 1 good, bit2 or bit3 = errored (length 0), any other value = dropped silently; RSSI from RSSI2 (§3);
LASTCHMAP = channel. Software drops PDU types 3 (SCAN_REQ) and 5 (CONNECT_IND) in state 1; the TX service of §8.2 is
called but returns at once outside states 6 / 7, so the SCAN_REQ buffers and their TXBUFnCNTL are never touched while
scanning. Duplicate filtering is a software list.

**Active-scan backoff** (Core Vol 6 Part B 4.4.3.2, in software) [IR]: state upperLimit = 1, backoffCount = 1, and
counters of consecutive successes / failures. Each received ADV_IND or ADV_SCAN_IND decrements backoffCount; at 0: if
the previous SCAN_REQ got no SCAN_RSP it counts a failure (two in a row: upperLimit doubles, at most 256, and a new
random count is drawn), it marks a request as sent and reloads the count. After each such packet FORMAT bit8 = 0 only
when backoffCount = 1, else 1: the engine's automatic SCAN_REQ is armed for exactly the next ADV_IND / ADV_SCAN_IND.
A SCAN_RSP counts a success (two in a row: upperLimit halves, at least 1).

**Stop** [IR]: as any link: column 14 = 0; optionally poll `0x28038` bit1 until 0; the instance's IRQ enable and
acknowledge bits cleared; buffers freed; all 17 columns written 0; instance-table entry = 0.

A vendor "fixed scan" option (passive, window = interval, one instance) forces interval 2 slots, `0x2800C` [9:0] = 62,
OPTCNTL |= `0x3F` and resets the RF after every received packet; it is off by default and not needed [IR].

### 21.3 Initiating (state 3)

**The CONNECT_IND is complete before the link starts** (task context) [IR]:

- AA: random, re-drawn until it has no run of more than 6 equal bits, differs from the advertising AA in more than one
  bit, has at most 24 transitions and enough transitions in its top 6 bits (the Core rules); the vendor also rejects an
  AA whose four octets XOR to 0.
- **CRCInit: the vendor keeps a fixed template value `0x1983AE`** (bytes `AE 83 19`); its random CRC generator is not
  called on this path. The Core spec asks for a random value; peers do not check, but ours should be random.
- WinSize 2 (2.5 ms). WinOffset = a random value in [Interval / 2, Interval − 1] (source `0x13B00`). Interval = the
  HCI maximum. Latency and Timeout from HCI. ChM = the host channel map (all 37 with AFH off). Hop = a random 5…16
  (`0x13B04` mod 17, values below 5 raised to 5). SCA field 0 (251–500 ppm).
- Target: the HCI peer address, or with initiator filter policy 1 only the **first** entry of the software whitelist.

**Engine programming, in the vendor's order** [IR]:

1. RFPRIO 26.
2. Window and interval exactly as scanning (21.2 steps 3–4), column 6 = `0x2100 | 37`, column 8 = 0.
3. Own address: LOCALADRL/M/U = own address, FORMAT bit3 set. OPTCNTL bit4 cleared (local-address match on) unless
   privacy is on (then set). The local match is what an ADV_DIRECT_IND's TargetA is compared with [I].
4. Target: WHITELIST0L/M/U = peer address (little-endian 16-bit halves); FILTERCNTL = (FILTERCNTL & `0x7E`) | 1 |
   (peer type << 8), then bit4 set; OPTCNTL bit3 cleared (remote-address match on); TARGETADRL/M/U = peer address.
   Read from use [I]: FILTERCNTL bit0 = whitelist entry valid, bit4 = send CONNECT_IND only to it, bit8 = its type.
5. Two RX buffers (263 B), as scanning.
6. Two TX buffers with 263 payload bytes, **both** holding the same CONNECT_IND: type 5, TxAdd = own type, RxAdd = peer
   type, length 34, payload InitA (6) + AdvA (6) + the 22 LLData bytes; TXAHDRn = type | TxAdd << 4 | RxAdd << 5,
   TXDHDRn = 34 << 8. ChSel is set only when channel selection #2 is compiled in (it is not in the SDK).
7. Column 2 = `0x3000` (state 3).
8. Interrupts on, anchor counter start 1 (or the scheduler, 21.5), then **column 9 = 1** (the vendor calls it "init
   end"; §2.3's latency-status column). Meaning [I]: the engine leaves the initiating state by itself once it has sent
   the CONNECT_IND.

**Reception** [IR]: the RX ISR in state 3 takes the **advertising** path (the buffer other than RXTOG's, RXBUFnCNTL
not read). Software then checks the packet against the target: types 3 and 5 dropped; ADV_DIRECT_IND kept only if
TargetA (payload bytes 6–11) and RxAdd equal our address and type; ADV_IND, ADV_NONCONN_IND, SCAN_RSP and ADV_SCAN_IND
kept only if AdvA and TxAdd equal the target. For ADV_IND / ADV_DIRECT_IND it records the received ChSel bit (the
channel selection algorithm of the new connection) and creates the connection record (role central, peer = AdvA /
TxAdd, LLData as sent).

**Switch to master (state 6)** [IR]: in the **event** interrupt (IRQ 45) of the initiating instance that follows that RX
interrupt, not in the RX ISR as for the slave (§7). The same instance becomes the master link; the task later frees the
initiating bookkeeping but keeps the instance. Writes:

1. The part shared with §7: RFPRIO 28; ANCHOR = `0x8000`; TXTOG bit1 cleared; RXTOG = 0; BDADDR0/1 = AA; CRCWORD0/1 =
   CRCInit; OPTCNTL |= 4, &= ~`0x200`, |= `0xC00`, |= `0x1000`; INTFRAME [5:4] = `01`; TXBUF0/1CNTL bit0 = 1; ANCHOR low
   bits = 4 (1M dead time); column 5 = 0, instant cleared.
2. Master only: column 4 = 0; **anchor counter = 2 × WinOffset + 4 slots** (column 7 = 0, column 0 = 0, column 14 = 0,
   column 0 = (2 × WinOffset + 3) & `0xFFFF`, column 14 = `0x8000` | the high bits; written twice with the same value on
   1M; 2 × WinOffset + 11 / + 8 for coded S8 / S2); column 2 = `0x6000 | Latency` (latency-enable bit 11 = 0). No
   WIDEN0/1 write.
3. The shared tail: column 8 = 0; column 3 = 0; EVTCOUNT = 0; WINCNTL0/1 = WinSize × 1,250 + 1,250 µs, WINCNTL2 = 50;
   column 1 = 2 × Interval slots, column 15 = `0x8000` | high bits; supervision timeout = a software timer of Timeout ×
   10 ms; channel tables and CHMAP from ChM (§6); column 6 = `0x8000 | Hop << 8 | Hop`; TXAHDR0/1 = 0, TXDHDR of the
   TXTOG buffer = LLID 1 with bit2 = 0 and the other with bit2 = 1; the fixed TX payload areas are the two CONNECT_IND
   buffers (263 B each); no PDU recorded in either (§8.2 point 7).
4. At the first packet received in the connection: WINCNTL0/1 = 0, WINCNTL2 = 30 µs (the slave gets 2 × widening + 50
   / 50 at the same point).

**First anchor** [I]: 2 × WinOffset + 4 slots = WinOffset × 1.25 ms + 2.5 ms after the counter is written. With the event
interrupt arriving just after the CONNECT_IND, that falls in the transmit window [1.25 ms + WinOffset × 1.25 ms,
+ WinSize × 1.25 ms] after the CONNECT_IND that WinSize 2 opens. It depends on what the counter counts from (21.5) and
when the event interrupt fires (21.9).

A vendor multi-master option (off in the SDK) rewrites WinSize (payload byte 19) and WinOffset (bytes 20–21) in both
TX buffers while initiating, to place the new link between existing ones [IR].

### 21.4 Master connection (state 6) against slave (state 7)

| | Slave (7) | Master (6) |
|---|---|---|
| First to transmit in an event | the central | the engine, at the anchor [I: Core spec; state 6 is also the DTM transmitter, §2.5] |
| Column 2 | `0x7000` \| latencyEnable << 11 \| Latency | `0x6000` \| Latency, latency enable never set [IR] |
| Anchor counter at set-up | not written | 2 × WinOffset + 4 (21.3) [IR] |
| Column 4 at set-up | `0x8000 \| (2 × WinOffset − 1)` or 0 | 0 [IR] |
| Widening | WIDEN0/1 and the event ISR's window auto-zoom | none [IR] |
| Receive window after the first packet | WINCNTL0/1 = 2 × widening + 50, WINCNTL2 = 50 | WINCNTL0/1 = 0, WINCNTL2 = 30 [IR] |
| RX ISR | §8 connection path | the same code (states 1, 6, 7) [IR] |
| TX service | §8.2 | the same code (states 6, 7 only); §8.2's rule holds unchanged [IR] |
| Event ISR | counter (column 3 − 1), instant check, callbacks, latency, auto-zoom | a channel callback with LASTCHMAP first, then the same counter, instant check and callbacks; no auto-zoom [IR] |
| Supervision timeout | software timer | software timer [IR] |
| Update applied at instant − 1 | column 4 = `0x8000 \| (2 × WinOffset − 1)` (0 if WinOffset 0); WINCNTL0/1 = WinSize × 1,250 + 625 + widening offset; WIDEN0/1 | column 4 = `0x8000 \| 2 × WinOffset`; WINCNTL0/1 = WinSize × 1,250 + 625; column 2 = `0x6000 \| Latency`; no widening [IR] |

Both then write WINCNTL2 = 50 and the new interval in columns 1 / 15 (bit15 = 1). The instant check in the event ISR
treats the instant as reached when (instant − counter − 1) as a 16-bit value exceeds 32,765; it then clears it and sets
RFPRIO 28 [IR].

**Updates we start as master** [IR]:

- LL_CONNECTION_UPDATE_IND: instant = event counter + 8…11 (random) when Latency = 0, else + 6 × Latency + 6. Plain
  case: WinSize 1, WinOffset = new Interval / 2 (a connection-parameter-request path with a reference event computes an
  offset and WinSize 2 or 4). When the PDU is acknowledged (§8.2), column 5 = instant and RFPRIO 30; at instant − 1 the
  master values of the table; the completion event at the instant; the receive window back to 0 / 30 two events later.
- LL_CHANNEL_MAP_IND: instant = counter + 6 + (Latency ≠ 0 ? 6 × Latency : random 0–3); column 5 = instant on the
  acknowledgement; at instant − 1 FRQ_IDX1 / FRQ_TBL1 / CHMAP rewritten (§6).

**Data length and MTU as master** [IR]: no engine write depends on the role. Connection RX buffers hold 255 B
(RXMAXBUF 255); the master's TX payload areas are the 263 B CONNECT_IND buffers, so a 251 B PDU fits (a slave's are the
263 B advertising buffers). The SDK builds the controller with an ACL packet length of 27 [SDK]; the length procedure is
link-layer software.

### 21.5 Instances: a peripheral and a central together

- [IR] Each role runs on its own instance; an instance changes state only by its set-up (2 → 7, 3 → 6) or by close and
  reopen. Opening a role takes the first free instance (instance-table entry 0) below `config_btctler_le_hw_nums`; both
  ISRs loop over links 0 … hw_nums − 1; the table has 8 entries; one instance's software entity is 636 B. Each instance
  needs its own control block and buffers inside the baseband RAM block (16-bit offsets, §4).
- [SDK] The SDK compiles roles = ADV | SCAN | INIT | SLAVE | MASTER (all five); features = encryption only (no privacy,
  no channel selection #2, no 2M / coded, no extended advertising); AFH off; master multi-link 0; slave multi-link 1;
  `le_hw_nums` = 1 by default (demo_ble), 2 with `BT_NET_CENTRAL_EN` or `APP_NONCONN_24G`, 3 with mesh, slaves + masters
  (2 + 2) with `TRANS_MULTI_BLE_EN`; RX pool and ACL pool 5 × hw_nums; `config_vendor_le_bb` 0. As the IR tests the role
  word: bits 0–1 the connection roles, 4 advertise, 8 scan, 16 initiate [IR]. Stock V15's values are not known.
- **Advertising while scanning needs two instances** (hw_nums ≥ 2). With one instance the SDK cannot do both; the
  second open fails [IR].
- **The vendor never lets two instances overlap in the radio; it time-slices in software** [IR]:
  - no connection open: at the end of every event interrupt a scheduler reads each advertising / connection instance's
    columns 0 and 14 (op 2) as "slots until its next event" (minus 3 as margin), suspends the scanning / initiating
    instances (column 14 = 0) and resumes one (anchor counter start = the idle part of its interval) only if the gap is
    at least its scan interval; with both a scanner and an initiator they take turns; nothing starts in a gap under 4
    slots;
  - connections open (slave multi-link, the SDK's setting): another scheduler, run from each connection's event
    interrupt, starts the other instances with software timers placed after the event and stops them with a
    window-close routine (column 14 = 0).
  Whether the engine itself could arbitrate two overlapping instances is not known [21.9].
- From that use [I]: a column 0 / 14 read is a **countdown to the instance's next anchor** that reloads every interval.
  That explains §8.1's "slot clock stepped backwards".

### 21.6 Radio priority and timing for central

- RFPRIO [IR]: scanning starts at 17 and runs 17 / 26 (every 6th event); initiating starts at 26 and runs 26 / 30
  (every 6th event); the master set-up writes 28; in a connection the per-event choice is the same function for states
  6 and 7 (30 around instants and with control PDUs queued, 28 with data queued, 19 in BR/EDR sniff, otherwise a value
  that decays toward 19); a vendor flag (off) holds 28 for state 6.
- Widening: none on the master side; the master's receive window after its own TX is WINCNTL2 = 30 µs once the
  connection runs (WINCNTL0/1 = WinSize × 1,250 + 1,250 µs before the first packet) [IR].
- SCA: the vendor's CONNECT_IND carries SCA 0 (251–500 ppm) [IR]. A central may send its true SCA (Core).
- None of the timing registers `0x28008–0x28018`, `0x28034` or IFSCNT is written on any central path [IR].

### 21.7 Directed advertising and the whitelist for reconnecting (peripheral side)

- ADV_DIRECT_IND [IR]: type 1, length 12, payload AdvA (own) + TargetA (peer), TxAdd = own type, RxAdd = peer type; both
  TX buffers hold it (no SCAN_RSP). The advertiser also writes TARGETADRL/M/U = peer and the single whitelist entry
  exactly as initiating does (21.3 step 4: WHITELIST0 = peer, FILTERCNTL bit0, bit4, bit8 = type, OPTCNTL bit3
  cleared). A 1.28 s software timeout runs (high duty cycle).
- After the engine has taken a CONNECT_IND while directed advertising, software also compares InitA / TxAdd with the
  peer and drops a mismatch; since the slave set-up is software (§7), a dropped CONNECT_IND gives no connection [IR].
- Undirected advertising with a filter policy (§6 step 3): FILTERCNTL bit3 = SCAN_REQ only from the whitelist, bit4 =
  CONNECT_IND only from the whitelist [IR for the bits, I for the meaning]. With the hardware whitelist being one
  address, "only my last central" maps directly onto WHITELIST0 [I].
- A bonded peer with a known IRK: the vendor generates a fresh RPA from the peer's IRK and uses it as TargetA and as
  the whitelist entry [IR]. Whether macOS and iOS answer directed advertising is a host question, not a hardware one.

### 21.8 Address resolution (RPA, IRK)

- **Software only.** No control-block field, column or register holds a key; every hardware match is a 48-bit compare
  against WHITELIST0, TARGETADR or LOCALADR [IR].
- The vendor's privacy mode (feature bit 64, off in the SDK) resolves in two passes [IR]:
  1. Privacy on: INTFRAME bit3 set (TX disabled; the bit §8.2 point 6 found unused by the data path) and FORMAT bit8
     set (no SCAN_REQ).
  2. The first ADV_IND / ADV_DIRECT_IND (initiating), ADV_IND / ADV_SCAN_IND (active scanning) or SCAN_REQ (advertising)
     reaches the RX ISR unanswered; software resolves AdvA (or ScanA) against its resolving list: ah(IRK, prand)
     with AES-128, compared with the hash (Core Vol 3 Part H 2.2.2).
  3. On a match: WHITELIST0 = that RPA, FILTERCNTL bit8 set (random) and bit4 (initiating) or bit3 (scanning,
     advertising), OPTCNTL bit3 cleared, then INTFRAME bit3 cleared (initiating, advertising) or FORMAT bit8 cleared
     (scanning). The engine answers the **next** packet from that RPA. In state 7 a CONNECT_IND re-enables TX after the
     check.
  4. Our own RPA as InitA is written straight into bytes 0–5 of both TX buffers and into LOCALADR (FORMAT bit3 cleared).
- AES for ah(): the vendor uses a separate AES-128 block at MMIO `0x41200–0x41218` (`aes128_start_enc` in
  `lib_ccm_aes.a`), not the BLE engine [IR; its register protocol was not extracted]. One software AES-128 per address
  works as well.
- For "remember the last device" [I, from the Core spec]: keep the peer's identity address and IRK from SMP key
  distribution (IdKey set in both key-distribution fields). While scanning, resolve each RPA-type AdvA (TxAdd = 1, top
  two bits `01`) in software against the stored IRK, then initiate to the literal RPA just seen (it stays valid for the
  peer's RPA period, typically 15 minutes). As a peripheral, accept any central and resolve its address after the
  connection, or use the two-pass trick for directed or filtered advertising.

### 21.9 Only hardware can settle

| # | Open | Experiment |
|---|---|---|
| C1 | Does the engine fill AdvA into the SCAN_REQ (the TX buffer holds zeros there)? | Sniffer: active scan of a known advertiser; check the SCAN_REQ's AdvA and that a SCAN_RSP comes back. |
| C2 | While scanning, does RXBUFnCNTL bit0 mark the filled buffer (the vendor's connection-path rule) or stay 0 as when advertising (§8.1)? | Log RXTOG, RXBUF0/1CNTL and RXAHDR0/1 at each RX interrupt while scanning. |
| C3 | Does the engine send the CONNECT_IND by itself, only to WHITELIST0 / TARGETADR with FILTERCNTL bits 0 / 4 / 8 and OPTCNTL bit3 = 0, and to which PDU types (ADV_IND only, or also ADV_DIRECT_IND)? What happens with the filter off? | Sniffer + one peripheral advertising; vary FILTERCNTL / OPTCNTL bit3 / the target. |
| C4 | When does the event interrupt fire in state 3 relative to the CONNECT_IND, and does column 9 = 1 stop the initiating link by itself? | GPIO in both ISRs + sniffer. |
| C5 | What does the anchor counter (2 × WinOffset + 4) count from? Is the first master packet inside the transmit window? | Sniffer: CONNECT_IND end to first master packet, for several WinOffsets. |
| C6 | Master event: does the engine transmit first at the anchor, and do WINCNTL0/1 = 0, WINCNTL2 = 30 µs catch the peripheral's reply? | Sniffer + RX interrupt count over a few hundred events. |
| C7 | Is a column 0 / 14 read a countdown to the next anchor (21.5)? | Read it at several points between two events of an advertising link. |
| C8 | Meaning of column 15 bit15 (0 for scanning / initiating, 1 for advertising / connections). | Scan with bit15 = 1 and compare the window pattern on a sniffer or with a DTM transmitter. |
| C9 | FILTERCNTL bit3 while scanning (policy 1 / 3): what does the engine filter? | Scan with bit3 set and an empty / one-entry WHITELIST0. |
| C10 | Can two instances overlap in time (advertise and scan without the software scheduler), and what wins? | Two instances with overlapping anchors; sniffer. |
| C11 | INTFRAME bit3: does it suppress the engine's automatic responses (SCAN_RSP, SCAN_REQ, CONNECT_IND)? | Advertise with bit3 set and send SCAN_REQs from a phone. |
| C12 | Does the scan-channel value written in the event ISR apply to the next window? | Sniffer: SCAN_REQ channels in sequence. |
| C13 | Real BLE-MIDI peers as peripherals (Mac in advertising mode, iPhone apps, another FM-1): connection with the vendor-like parameters (WinSize 2, random CRCInit of ours). | One try each. |

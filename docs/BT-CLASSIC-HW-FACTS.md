# BR/EDR (Bluetooth Classic) baseband hardware facts (AC791N / WL82)

Written 2026-10-18 by the clean-room reader, as stage 0 of our own Classic stack (goal: A2DP source, FM-1 →
AirPods, SBC, GPL-clean, no JieLi library linked). It does for the BR/EDR engine what
[BLE-HW-FACTS.md](BLE-HW-FACTS.md) did for the LE engine, and it continues route C of
[BT-AUDIO-FEASIBILITY.md](BT-AUDIO-FEASIBILITY.md) §3.3. The implementers must not read vendor code, vendor IR or
vendor disassembly. They work from the Bluetooth Core Specification (Vol 2 Part B, Baseband) and this sheet.

This sheet contains facts only: addresses, bit fields, values, sequences, timing, interrupts, RAM layouts and
observed behaviour. It contains no vendor code and no pseudo-code. Where a vendor routine is named, the name is
there to say where a fact came from, not how the routine is written.

## 0. Sources, tags and caveats

**Tags.** Every fact carries one tag:

| Tag | Meaning |
|---|---|
| **[M:t]** | measured: an emulator MMIO trace of stock V15 (order, values, timing) |
| **[M:d]** | measured: a RAM image taken from the emulator after stock V15 ran |
| **[M:s]** | value seen: a constant, address, bit mask, field offset or field name read from the vendor library's LLVM IR and its debug info |
| **[M:c]** | computed from the Core spec and checked equal to [M:s]/[M:d] values (two independent test vectors) |
| **[S]** | from the Bluetooth Core specification |
| **[I]** | inferred from names, structure or consistency; plausible, not proven |

Many facts are [M:s] for the *value* and [I] for the *meaning*; the text says which.

**Sources.**

- **Vendor library** `cpu/wl82/liba/btctrler.a` of the AC79 SDK (`~/GitHub/ac79-sdk`, V1.2.13), 106 LLVM bitcode
  members. Disassembled with Homebrew LLVM 23 `llvm-dis --disable-auto-upgrade-debug-info` (without the flag the
  debug info is dropped as "invalid" and the field names are lost). Members read: `bredr_frame.c` (the register
  writers), `bredr_link.c` (procedures and interrupt handlers), `bredr_slot_timer.c`, `bredr_low_power.c`,
  `bredr_fcc.c`, `lmp_utils.c`, `authentication.c`, `RF.c`, `analog.c`, `RF_ble.c`, `link_task.c`. Source paths in the
  debug info: `lib/btctrler/port/wl82/baseband/classic/*.c`.
- **Stock V15** (`fm1-firmware/FM-1.fwsc`) run unchanged in the emulator (`fm1-emulator-ble`, `examples/diagnose`,
  900 M CPU0 instructions = 2.5 s emulated) with the MMIO trace on `0x20000–0x201FF`, `0x2FC40–0x2FC4F`,
  `0x2FD40–0x2FD67` and a dump of its BR/EDR table at `0x01C0A05C` (§13).
- The BLE sheet's first-boot trace ([ble-traces/v15-boot-ble-mmio.txt](ble-traces/v15-boot-ble-mmio.txt)), which
  already contained the BR/EDR init writes.

**Emulator caveat** (as in BLE-HW-FACTS §0): the emulator models the Bluetooth slot clock (`0x2001C/20/24`), its
alarm and the eight slot timers, and stores every other write. It has **no BR/EDR engine model**: no packet is sent,
no RX interrupt fires. Every value V15 *writes* is genuine; nothing after "scan enabled" happens in the emulator.

**Nothing in this sheet ran on hardware.**

---

## 1. Summary of the architecture

- The BR/EDR engine is a **frame engine driven by SRAM control blocks**, like the LE engine (BLE-HW-FACTS §3): one
  4,196-byte table (`bredr_table`, §3) holds a 16-entry **exchange table**, four hop **frequency tables**, four
  180-byte **frame** control blocks (`ctrl_frame`, §4) and RX buffers. Register `0x2FC44` holds its address
  (the "BR/EDR exchange memory base"); every pointer field in a frame is a **16-bit offset from that base** [M:s].
- A frame describes **one procedure on one piconet**: access code (LAP + 34 parity bits), the clock offset, a
  procedure code (inquiry, page, scans, responses, master/slave connection), two ping-pong TX buffers with packet
  type and payload header, two RX buffers, RF power/gain, AFH table choice, encryption key [M:s].
- The hardware does the per-slot work: hop selection (no hop kernel exists in software), access-code correlation,
  header/payload coding, inquiry/page ID trains, SEQN/ARQN, and E0 encryption. Software touches a running
  procedure only on an RX interrupt or on a watchdog alarm every 18 slots (11.25 ms) [I, strong: §6, §7, §9].
- Software does: the procedure state machines (in the RX interrupt), the sync-word parity from the LAP
  (verified against the spec, §5.3), FHS packing and parsing (spec layout, §8.4), clock-offset tracking, the
  A/B train switch every 2.56 s / 1.28 s, AFH channel tables, power and AGC, and time-sharing the radio between
  procedures by swapping frames in the exchange table [M:s].
- **Stock V15 runs Classic.** In the emulator it alternates inquiry scan (name "FM-1", class 0x240404) and page
  scan with its own BD_ADDR, every 0.5 s (§11) [M:t, M:d]. It is a dual-mode build; on a real FM-1 this is
  unverified.

---

## 2. Memory map and register reference

All registers are 32-bit words [M:s]. "Writer" names the vendor routine; "when" says in which procedure.

### 2.1 Clock, control and interrupts (`0x20000–0x2005C`)

| Addr | Bits and meaning | Values, writer, when | Conf. |
|---|---|---|---|
| `0x20000` | **BR/EDR control.** bit8 = engine run (cleared then set again to restart the engine on a frame swap, cleared before relocating the table) [M:s]/[I]; bits [2:0] set at init (meaning unknown) [M:s]; bit31 = set when a procedure stops (inquiry/page/scan suspend, PLL-comp reset, close) [M:s], meaning [I] "stop / radio off request"; bit30 set together with bit31 in the RF power-up (`0xC0000000`) [M:t]; bit10 set on scan suspend and low-power entry [M:s, M:t]; bit11 set after an EIR reception and on inquiry disable [M:s]; bits 15, 16, 19 set for the fixed-frequency test mode (§9.4) [M:s] | RF power-up `0xC0000000`; init `0x107`, then bit31, bit8 → `0x80000107` [M:t] | M |
| `0x2000C` | **IRQ 40 enables.** bit1 RX/frame, bit9 alarm (`0x200E4`), bit11, bit2 (low-power wake) [M:s]; init writes 0 then `0xFFFF0802` [M:t]; bits 14, 16, 18 … 30 are masked off for a moment by the vendor's "wait 2 slots" routine (mask `0x55554000`) [M:s] | `0xFFFF0802`; `0xFFFF0A02` while an alarm is armed [M:t] | M |
| `0x20010` | **IRQ 40 pending (masked).** bit0, bit1 (RX: some frame's `rx_clkn` changed, §7.1), bit2, bit9 (alarm), bit10, bit11 [M:s] | `0x200` read in V15 when the alarm fires [M:t] | M |
| `0x20014` | raw status; bit0 polled by the "wait for the 2-slot rise" routine (a 1.25 ms master-slot-pair edge [I from name]) | — | L |
| `0x20018` | **IRQ 40 acknowledge**: write 1 to the pending bit (bit0, 1, 2, 9, 10, 11). Init writes `0xFFFF` [M:s, M:t] | — | H |
| `0x2001C` | **clock latch**: write 1, poll until it reads 0, then read `0x20020` [M:s, M:t] | V15 polls 4–8 times [M:t] | H |
| `0x20020` | **CLKN in slots** (native clock bits [27:1], 625 µs units; wraps at 2^27 slots — software uses 2^27 in its wrap arithmetic) [M:s] | `0x162` at 0.381 s in V15 [M:t] | H |
| `0x20024` | fine count: µs elapsed inside the current slot (0–624) [M:s]/[I] (anchor time = slots × 625 − this) | — | M |
| `0x20028` | a receive-window constant: written 46, read back and +20 used as the frame's `wincntl` for slave timing [M:s] | — | L |
| `0x2002C` | `0x140` (320) at init [M:t] | — | L |
| `0x20030`, `0x20040`, `0x20044` | low-power (sleep) control: wake time in slots and fine count, bit31 = arm [M:s] | not needed for stage 1 | L |
| `0x20050` | written `0x87` then `0xAC` in the BT pre-init [M:s] | — | L |
| `0x20058`, `0x2005C` | `0x4000`, `0` at init [M:t] | — | L |

### 2.2 Radio, test, EIR and alarm (`0x2007C–0x200F4`)

| Addr | Bits and meaning | Values | Conf. |
|---|---|---|---|
| `0x2007C` | `0xA04` at init [M:t] | — | L |
| `0x20080` | `0x11480A48` at init [M:t]; a "radio power up/down reset" sets bits [11:8] [M:s]. Radio power-sequencing timing [I] | — | L |
| `0x20084` | 0 at init; `0x15B`/`0x15F` and bit9 for the TWS monitor mode (not needed) [M:s] | — | L |
| `0x20090 + 12·t`, t = 0…2 | **AFH channel map t** (3 words): word0 = channel bits 0–31, word1 = 32–63, word2 = [15:0] channels 64–78, [31:24] = number of used channels [M:s] | written per connection by the AFH set-up | H |
| `0x200C0` | **EIR TX** (inquiry-scan side): [31:20] length, [19:16] packet type, [15:0] buffer offset [M:s] | V15 `0x019A10C4`: 25 bytes, type 10 (DM3), offset `0x10C4` [M:t] | H |
| `0x200C4` | **EIR RX** (inquiry side): [15:0] offset of the EIR receive buffer, bit31 = EIR reception armed (cleared after an FHS without EIR or after the EIR packet, set again when the inquiry resumes) [M:s] | — | M |
| `0x200D0` | test modes: bit0 = 1 for normal modulation, 0 for single carrier; bit1 cleared [M:s] | — | L |
| `0x200E4` | **alarm**: the CLKN slot value at which IRQ 40 bit9 fires (written as CLKN + n; enable `0x2000C` bit9, acknowledge `0x20018` bit9) [M:s, M:t] | V15: CLKN + 24, then + 18 [M:t] | H |
| `0x200F0`, `0x200F4` | 0 at init [M:t] | — | L |

### 2.3 SCO/eSCO ports (`0x20120–0x20168`)

Two identical groups, channel 0 at `0x20120` and channel 1 at `0x20150` [M:s]. Not needed for A2DP (ACL only); listed
so that the init writes make sense.

| Ch0 | Ch1 | Meaning | Init [M:t] |
|---|---|---|---|
| `0x20120` | `0x20150` | port control: [31:24] T_sco, bit16 PCM out enable [M:s] | `0x06000000` |
| `0x20124` | `0x20154` | | `0x8015` |
| `0x20128` | `0x20158` | | `0xC0` |
| `0x2012C`, `0x20130` | `0x2015C`, `0x20160` | two RX buffer offsets (hi:lo halves both = offset of the table's eSCO RX buffer, `0x0EE0`) [M:s, M:t] | `0x0EE00EE0` |
| `0x20134` | `0x20164` | LT control: bit4/5 EDR, [7:3] type, [2:0] LT_ADDR [M:s] | `8` |
| `0x20138` | `0x20168` | TR control: [31:22] m→s length, [21:12] s→m length, [11:8] m→s type, [7:4] s→m type [M:s] | V15 `0x14050770`, SDK `0x0280A550` (library versions differ) |

### 2.4 Shared BT block (already in BLE-HW-FACTS §2.2)

| Addr | Classic role | Tag |
|---|---|---|
| `0x2FC40` | bit10 = BR/EDR block enable (set before init, cleared on close); bit6 written by a debug ISR (IRQ 42) [M:s] | M |
| `0x2FC44` | **BR/EDR table base**: the SRAM address of `bredr_table` (§3); all frame offsets are relative to it [M:s, M:t] | H |
| `0x2FC48` | `0xB9`, then `0xAD` written by the BR/EDR init [M:t] | L |
| `0x2FC88` | the PLL channel table pointer, **shared** with BLE (written once) [M:t] | H |
| `0x2FD40–0x2FD60` | slot timers, IRQ 41 (§6.3) | H |
| `0x2FD8C` | AGC readback used for the real-time RSSI of an inquiry response (§10.3) [M:s] | M |

---

## 3. The BR/EDR table (`bredr_table`, 4,196 bytes, in SRAM)

The vendor places it in section `.bd_base` and zeroes it before writing `0x2FC44` [M:s]. V15 has it at `0x01C0A05C`
[M:t]. Offsets [M:s] (debug info), confirmed by V15's dump [M:d]:

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 16 × u16 | `exchg_table` | **exchange table** (§3.1) |
| 32 | 4 × 80 B | `frq_table` | hop index → RF channel tables; [0] = basic hopping, [1–3] = AFH tables (§9) |
| 352 | 16 B | `encryp_work` | engine work area for encryption [I] |
| 368 | 4 × 180 B | `frame` | the four frames (§4); frame n at 368 + 180·n |
| 1088 | 2×2×170 words | `link_rx_bulk` | ACL RX buffers |
| 3808 | 80 B | `esco_rx_buf` | the eSCO default RX buffer (§2.3) |
| 3888 | 20 B | `bt_updata` | software (firmware-update hand-over) |
| 3908 | 4 | `cur_ctrl_frame` | software: the frame now in the exchange table |
| 3912 | 3 | `afhtable_used` | software: which frame owns AFH table 1, 2, 3 |
| 3916 | 4 × u32 | `last_clkn` | software: last `rx_clkn` seen per frame (§7.1) |
| 3932 | 4 × u8 | `bredr_running`, `eninv_flag`, `frame_used` (bit n = frame n allocated), `current_frame` (the odd/even flag, §3.1) | software |
| 3936 | 20 B | `rx_fhs` | the FHS RX buffer used by inquiry and page scan |
| 3956 | 240 B | `rx_eir` | the EIR RX buffer used by inquiry |

**Constraints** [M:s]: a frame's offset must be below `0x800` (11-bit field in the exchange table), TX pointer offsets
below `0x7FFF` (bit15 of the RX pointer fields is a flag), the EIR TX offset below `0xFFFF`. So the frames must be in
the table's first 2 KB, and every TX/RX buffer within 32 KB above the base. Other buffers may be outside the
4,196 bytes: V15's inquiry-scan FHS and EIR buffers are at offsets `0x10B0` and `0x10C4` [M:d].

### 3.1 Exchange table (16 × u16)

| Bits | Meaning | Tag |
|---|---|---|
| [10:0] | offset of the frame (from `0x2FC44`) | M:s |
| 11 | eSCO "reserved slot" flag | M:s (name) |
| 12 | **inhibit**: 1 = this entry is closed, 0 = open | M:s |
| [14:13] | a 2-bit channel selector (vendor writes 3 when an eSCO link closes) | M:s |
| 15 | eSCO "next" flag | M:s |

- Enabling a frame writes its offset into **all 16 entries** (keeping bits [15:11]) and records it as the current
  frame; disabling it sets bit12 in all 16 [M:s]. The vendor never puts two frames in the table at once: the engine
  works on one frame at a time and software time-shares procedures by swapping frames [M:s]/[I].
- An "odd/even" flag chooses which entries open: 0 = even entries open and odd ones closed, 1 = the reverse,
  2 = all open [M:s]. Inquiry and page use 0; page scan switches to 2 after it hears an ID; a slave connection
  uses the parity of its clock offset [M:s]. Meaning [I]: entry i governs the slots whose slot number mod 16 is i,
  and an open entry lets an exchange (TX slot + reply slot) start there; a master starts exchanges on even slots.
- V15 at 2.5 s (scans suspended): all 16 entries `0x1224` = frame 1 (offset `0x224`), inhibited [M:d].

---

## 4. The frame control block (`ctrl_frame`, 180 bytes)

Field names and offsets from the debug info [M:s]; values from V15's two frames (inquiry scan, page scan) [M:d]
and from the vendor's frame allocator [M:s]. Index 0/1 fields belong to the two ping-pong buffers; the engine's
`txtog`/`rxtog` bit0 say which one it is on [M:s].

| Off | Field | Size | Meaning and known bits | Allocator default / V15 |
|---|---|---|---|---|
| 0 | `bdaddr0/1/2` | 3×16 | the **BD_ADDR whose access code and hop sequence the frame uses**, little-endian halves: LAP in bytes 0–2, UAP byte 3, NAP 4–5 [M:s]. Inquiry/inquiry scan: GIAC LAP `0x9E8B33`, UAP/NAP 0 (UAP 0 = the spec's DCI) [M:s, M:d]; page: the remote's address; page scan: our own; connection: the master's [M:s] | V15 page scan `994C F41D D906` (its BD_ADDR) |
| 6 | `bchl`, `bchm`, `bchh` | 16, 16, 8 | the **34 parity bits of the sync word** (bits 0–33, little-endian; `bchh` holds bits 32–33) (§5.3) | V15 GIAC `3AE2 331A 02` |
| 11 | `cx` | 8 | unknown | 0 |
| 12 | `miscntl` | 8 | [2:0] LT_ADDR; bit3 = **packet type table** (0 = basic rate, 1 = EDR 2/3 Mb/s types); bit4 TWS monitor; [7:6] **hop table**: 0 = `frq_table[0]`, 1–3 = AFH table 1–3 [M:s] | 0 |
| 13 | `format` | 8 | [4:0] **procedure code** (§5.1); bit5 eSCO clock align; bit6 **AFH enable**; bit7 **A/B train** select (inquiry and page) [M:s] | V15 `0x0B` inquiry scan, `0x05` page scan |
| 14 | `rxtog` | 8 | bit0 = RX buffer the engine fills next; the filled one is `rxtog ^ 1` when the RX interrupt runs [M:s]; software writes 0 to restart [M:s] | 0 |
| 15 | `txtog` | 8 | bit0 = TX buffer the engine sends next [M:s] | 0 |
| 16 | `wincntl` | 16 | receive window. Values: inquiry 70, page 80, page scan after ID 440, slave connection `rd(0x20028)` + 20; scans `0x8022` (bit15 + 34) [M:s, M:d]. Units µs [I]; bit15 [I] "window opens and stays open" | V15 `0x8022` |
| 18 | `rxmaxbuf` | 16 | maximum RX payload | `0x300` (768) [M:s, M:d] |
| 20 | `txpwr` | 8 | | 0 |
| 21 | `txfreq` | 8 | TX hop index (fixed-frequency mode: written by software, §9.4) [M:s]; normal mode [I] engine-written | 0 |
| 22 | `rxrssi` | 8 | | 0 |
| 23 | `rxfreq` | 8 | RX hop index, as `txfreq`; the RX handler uses it for per-channel AFH statistics [M:s] | 0 |
| 24 | `txptr0/1` | 2×16 | offsets of the two TX payloads | V15 inquiry scan: both `0x10B0` (its FHS) |
| 28 | `rxptr0/1` | 2×16 | offsets of the two RX buffers; bit15 = a FLOW control bit, set or cleared on both words by the vendor's flow routine (direction of the meaning [I]); a null buffer is written as `0x8000` [M:s] | V15 page scan: both `0x0F60` (`rx_fhs`) |
| 32 | `linkcntl0/1` | 2×16 | **per-TX-buffer control** (§5.2) | allocator `0x4010` |
| 36 | `txphdr0/1` | 2×16 | TX payload header: [1:0] LLID, bit2 FLOW, [12:3] length [M:s] | allocator `0x6007`; ID/POLL/NULL/FHS use 2 |
| 40 | `rxstat0/1` | 2×16 | **RX status** (§5.4) | — |
| 44 | `rxphdr0/1` | 2×16 | received payload header, same layout as `txphdr` [M:s] | — |
| 48 | `clk_offsetl/h` | 2×16 | **clock offset** (slots, 28-bit two's complement, bit 27 sign) added to CLKN to get the clock the frame hops and times with [M:s] | 0 |
| 52 | `rx_clknl/h` | 2×16 | CLKN (slots) latched at the last reception [M:s] | — |
| 56 | `bitoff` | 16 | slave timing: offset of the remote's slot start inside our slot, µs, 0–624 [M:s]/[I] | 0 |
| 58 | `rxbit` | 16 | [9:0] µs position of the received sync word in the slot; [15:13] a clock-drift count [M:s]/[I] | 0 |
| 60 | `cryptk` | 8×16 | **encryption key** Kc′ (16 bytes, little-endian halves) [M:s] | 0 |
| 76 | `escorxstat0/1` | 2×16 | eSCO RX status | `0x83` |
| 80 | `lmprxptr0/1` | 2×16 | offsets of the RX buffers for LMP/control packets (the vendor points them at the same buffers as `rxptr` for inquiry and page) [M:s] | V15 page scan `0x0F60` |
| 84 | `acldmpriocntl` | 16 | **radio-arbitration priority** for ACL: prio \| prio << 8 \| step << 13 [M:s] | `0x1414` (20) |
| 86 | `escodmpriocntl` | 16 | same for eSCO | `0x1D1D` (29) |
| 88 | `bufcntl` | 16 | bit8 = **master role** (set by page completion and fixed TX, not by page scan or fixed RX) [M:s]/[I]; [1:0] set to 3 at connection start; bits [11:8] cleared first; bit5 cleared [M:s] | `0x202C` |
| 90 | `txbuf0cntl/1` | 8, 8 | bit0 = "TX empty": software clears it to hand a loaded buffer to the engine; the engine sets it back [M:s]/[I], as BLE's TXBUFnCNTL (BLE-HW-FACTS §8.2) | 0 |
| 92 | `rxbuf0cntl/1` | 8, 8 | bit0 = RX full; software clears it (and sets `linkcntl` bit14) to re-arm the buffer [M:s] | 0 |
| 94 | `rxbuf0stat/1` | 8, 8 | | 0 |
| 96 | `twscntl`, `twsword0–3`, `twssta0/1` | | TWS only | 0 |
| 108 | `TXPWER` | 32 | TX power = level + 16, level 0–11 [M:s] | V15 `0x1B` (11) |
| 112 | `RXGAIN0` | 32 | AGC step 0–15 [M:s] | `0x0F` |
| 116 | `TXSET` | 32 | same value as `TXPWER` [M:s] | `0x1B` |
| 120 | `RXSET` | 32 | | 0 |
| 124 | `PLL_COMP` | 32 | carrier-offset compensation = −(offset × 2^20) / 24,000 (same formula as BLE) [M:s] | 0 |
| 128 | `mdm_set` | 32 | modem setting [I] | 0 |
| 132 | `RSSI0–3` | 4×16 | engine-written RSSI words; RSSI1 drives the AGC (step +1 below 800, −1 above 1,100) [M:s] | — |
| 140 | `anl_out` | 32 | | 0 |
| 144–179 | `…_t` copies | 36 B | TWS copies of 108–143 | — |

**Bytes 108–143 have the same order and meaning as the LE control block's 0x120–0x140**
(`TX_PWER … ANL_OUT`, BLE-HW-FACTS §3) [M:s]: the radio fields are common to both engines [I].

The allocator also sets RXGAIN0 from an AGC step of 15 and the power from the board's level [M:s].

---

## 5. The packet engine

### 5.1 Procedure codes (`format` [4:0])

| Code | Procedure | Who sets it | Tag |
|---|---|---|---|
| 2 | connection, **master** (also fixed TX test) | page completion; role switch | M:s |
| 3 | connection, **slave** (also fixed RX test) | page scan completion | M:s |
| 4 | **page** (ID trains with the remote's access code) | page start, page re-arm | M:s |
| 5 | **page scan** | page-scan start | M:s, M:d |
| 6 | **master page response** (send FHS) | RX handler after the slave's ID | M:s |
| 7 | **slave page response** | page-scan RX handler after the master's ID | M:s |
| 8 | **inquiry** (ID trains with the GIAC) | inquiry start | M:s |
| 11 | **inquiry scan** (answers with the FHS in TX buffer, then EIR from `0x200C0`) | inquiry-scan start | M:s, M:d |
| 0, 1, 9, 10, 12–31 | not used by the library | — | M:s |

Meaning of each code = the Core spec's state of the same name [S]; that the hardware generates the matching hop
sequence and timing for each is [I, strong]: nothing in software computes hops or ID timing.

### 5.2 TX control (`linkcntl0/1`)

| Bits | Meaning | Tag |
|---|---|---|
| [3:0] | **packet type code** for this TX buffer, the spec's 4-bit TYPE (0 NULL/ID, 1 POLL, 2 FHS, 3 DM1, 4 DH1/2-DH1, 10 DM3/2-DH3, 11 DH3, 14 DM5/2-DH5, 15 DH5) [M:s]; the table in use is chosen by `miscntl` bit3 | M:s, S |
| 4 | SEQN; software writes it only for eSCO/TWS, never for ACL: the engine keeps SEQN/ARQN for ACL [M:s]/[I] | M |
| 5 | set on both buffers in the fixed-frequency test mode [M:s]; [I] whitening off (the spec's test mode sends unwhitened) | L |
| 7 | **TX encryption** on | M:s |
| 8 | set in V15's inquiry-scan buffers (`0x4102`, `0x4112`) [M:d]; [I] "an EIR packet follows" | L |
| [13:9] | a "full status" packet field (written as value << 9 on both buffers) [M:s] | L |
| 14 | **RX interrupt enable** for this buffer pair; set in every normal write (`0x4000`); cleared while the RX handler reads a buffer, set again on re-arm [M:s] | M |
| 15 | **RX decryption** on | M:s |

Writing a packet type preserves bits 4, 5, 7 and 15 and sets bit14 [M:s].

Values the library writes [M:s]: inquiry `0x4000` (ID); page `0x4002` on the current buffer (FHS), `0x4001` on the
other (POLL); slave page response / connection start `0x4010` and `0x4000`; master connection start (FHS acked)
`0x4011` and `0x4001`.

### 5.3 Access code (sync word)

- The frame holds the 24-bit LAP (in `bdaddr`) and the 34 parity bits (`bch`); software computes the parity [M:s].
- **Verified [M:c]:** the spec's construction (Vol 2 Part B §6.3.3: LAP + 6 Barker bits, XOR with PN bits 34–63, the
  remainder modulo the degree-34 BCH generator (octal 260534236651), XOR with PN bits 0–33, PN =
  `0x83848D96BBCC54FC` with bit 0 = p0) gives exactly the stored bytes for two independent LAPs:
  GIAC `0x9E8B33` → `E2 3A 1A 33 02` (the library's constant) and V15's own LAP `0x1D994C` → `08 D9 39 52 02`
  (from V15's page-scan frame in RAM). The bytes are parity bits s0–s33, little-endian, s0 = bit 0 of byte 0.
  The check script is in §13.
- So the engine builds the 64-bit sync word from `bch` (s0–s33) and the LAP (s34–s57) plus the Barker bits, adds
  preamble and trailer, and correlates on it [I, strong]. The FHS payload starts with the same 34 bits (§8.4).

### 5.4 RX status (`rxstat0/1`)

| Bits | Meaning | Tag |
|---|---|---|
| [3:0] | received packet TYPE (0 = ID/NULL, 2 = FHS, 3/4/10/11/14/15 = DM/DH data) | M:s |
| [15:8] = `0x8F` | **a good new packet** (the analog code's "RX ok" test is the same value) | M:s |
| [15:8] = `0xAF` | a good packet that repeats the last SEQN (a retransmission; counted as a SEQN error) | M:s |
| bit9 | sync word found (used for timing even when the packet failed) [I] | L |
| bit7, bit4 | software ORs `0x80` (page scan, before the FHS) or `0x90` (page, at the switch to connection) into both words [M:s]; meaning unknown | L |

### 5.5 Payload and header: hardware vs software

| Item | Who | Tag |
|---|---|---|
| access code, sync correlation | hardware, from `bdaddr` + `bch` | I strong |
| packet header (LT_ADDR, TYPE, FLOW, ARQN, SEQN, HEC) | hardware from `miscntl`/`linkcntl`; HEC/CRC initialised with the UAP from `bdaddr` [S]/[I] | I strong |
| ARQN / SEQN for ACL | hardware (software never writes SEQN for ACL; the engine flags repeats as `0xAF`) | I strong |
| payload header | `txphdr` (LLID, FLOW, length) written by software; received in `rxphdr` | M:s |
| payload CRC, FEC 1/3 and 2/3, whitening | hardware: no software CRC/FEC/whitening exists for BR/EDR | I strong |
| FHS payload content | **software** builds the 18 bytes in spec order; the clock field (bytes 14 bits 3–7 to 17) is left 0 and filled by the engine at TX time [M:s, M:d] | M:s / I |
| EIR | software builds the bytes; TX through `0x200C0`, RX through `0x200C4` | M:s |
| ID packet | hardware: format 8/4 with packet type 0 | M:s / I |
| TX buffer hand-over | write `txptr`, `txphdr`, `linkcntl`, then clear `txbufNcntl` bit0 with interrupts off [M:s] | M:s |
| RX re-arm | clear `rxbufNcntl` bit0 and set `linkcntl` bit14 [M:s] | M:s |

Packet types by payload size, as the library's tables give them (max bytes, TYPE, slots) [M:s]: basic rate
DM1 17/3/1, DH1 27/4/1, DM3 121/10/3, DH3 183/11/3, DM5 224/14/5, DH5 339/15/5; EDR 2-DH1 54/4/1, 2-DH3 367/10/3,
2-DH5 679/14/5 (= the spec's sizes [S]). 3-DH types are switchable by an SDK call; not traced.

---

## 6. Clock and timing

### 6.1 CLKN

- `0x20020` (after the `0x2001C` latch) is CLKN[27:1]: one count per 625 µs slot [M:s]; the spec's CLKN[0]
  (312.5 µs) is not exposed there [I]. V15 latches and reads it 63 times in 2.5 s [M:t].
- `0x20024` is the µs position inside the slot [M:s]/[I].
- Every frame has a 28-bit **clock offset** in slots; the frame's clock = CLKN + offset [M:s]. Inquiry and a fresh
  page scan use 0. A page uses the remote's estimated offset (from an inquiry result: remote CLK from its FHS minus
  `rx_clkn`) [M:s]. A slave connection uses (master CLK from the FHS) − `rx_clkn`, then corrects ±1 slot from
  `rxbit`/`bitoff` drift [M:s].
- The FHS carries CLK[27:2]; software turns it into slots as CLK[27:2] × 2 [M:s].

### 6.2 IRQ 40: RX and the alarm

- IRQ 40, priority 2 [M:s], handler order [M:s]: ack bit0; if bit1: ack it, then for each allocated frame compare its
  `rx_clkn` with the last seen value and run that frame's RX handler when it changed; ack bit10; if bit9: ack and
  run the **alarm handler** (§6.4); ack bit11; ack bit2.
- There is **no per-slot software interrupt** in inquiry, page or scans: the engine runs the trains alone [I strong].

### 6.3 IRQ 41: slot timers (`0x2FD40`)

As in BLE-HW-FACTS §10 [M:s, M:t]: `0x2FD40` bits 0–7 enable, write bit 8+k to clear pending k, bits 16–23 pending;
`0x2FD44 + 4k` = alarm slot (compared in 16 bits modulo, the software treats differences below 32,767 as "past").
Init writes `0xFF00`. The IRQ 41 handler also runs the link supervision timeout check. Used for timeouts (e.g. "EIR
not received within 7 slots") and the scheduler.

### 6.4 The alarm `0x200E4`

- Arm: ack `0x20018` bit9, read CLKN, write CLKN + n to `0x200E4`, set `0x2000C` bit9. Disarm: ack bit9, clear
  `0x2000C` bit9 [M:s, M:t].
- n used: 18 after a procedure (re)starts and as the inquiry/page watchdog; 8 after an FHS; 16 after a page-response
  ID; 32 after the FHS ack; 512 after an inquiry result; 20, 50 in page scan [M:s]. V15: +24 then +18 [M:t].
- The alarm handler does the per-procedure housekeeping: page and inquiry switch the A/B train and re-arm, page
  scan steps its state, inquiry scan ends its window [M:s].

### 6.5 Timing the software must meet [I]

- In page, the master learns of the slave's ID response in IRQ 40 and reprograms the frame (format 6, FHS) for the
  next master TX slot; after the slave's second ID it switches to format 2 with POLL. If the hardware does not
  send the FHS by itself, the IRQ must finish within about one slot (625 µs). Unknown which (§12, U5).
- Optimist's audio ISR can hold CPU 0 for up to ≈4.9 ms (feasibility §5), so IRQ 40 must run above the audio ISR or
  on CPU 1.

---

## 7. Frequency hopping

### 7.1 Hop tables

- `frq_table[0]` (80 bytes, 79 used) maps the hop kernel's output index to the RF channel: indices 0–39 hold the even
  channels 0, 2, …, 78 and 40–78 the odd channels 1, 3, …, 77 [M:s, M:d] — exactly the spec's final mapping
  stage (Vol 2 Part B §2.6.2, "register bank") [S]. So the **hardware runs the hop selection kernel** and uses the
  table only for the last step [I, strong].
- The init writes the table with offset 0 (values = channel number k, RF = 2402 + k MHz [S]); the single-carrier test
  writes it with +2 [M:s]. The shared PLL table at `0x2FC88` turns the code into a synthesiser setting
  (BLE-HW-FACTS §2.4) [I].
- In fixed-frequency mode, `txfreq`/`rxfreq` hold the **index** into this table (channel k → k/2 if k even,
  40 + k/2 if odd) [M:s].
- The rate and sequence (inquiry/page trains of 32 frequencies, A/B halves, response hopping, the basic
  79-channel connection hop) follow from `format`, the address in `bdaddr` (LAP and UAP[3:0]) and the frame clock
  [S]/[I]. The A/B train is switched by software via `format` bit7: inquiry every 4,096 slots (2.56 s = 256 × 10 ms),
  page every 2,048 slots (1.28 s) [M:s] — the spec's N_inquiry = 256 and N_page for R1 [S].
- The library also offers a crystal-error sweep for inquiry/page: on each failed train it changes `PLL_COMP` through
  a 9-step table (signed −120, −80, −40, −20, 0, +20, +40, +80, −120; `PLL_COMP` = (step − stored offset) × 2^20 /
  24,000) [M:s], behind a config switch.

### 7.2 AFH

- Three AFH tables (`frq_table[1–3]`) and three register triples (`0x20090 + 12t`) [M:s].
- Set-up for a connection [M:s]: pick a free table t; write `frq_table[t+1]` = the used channels in the same
  even-then-odd order, packed; write the 79-bit map and the used count into `0x20090 + 12t`; set `miscntl` [7:6] =
  t + 1 and `format` bit6. Disable: clear `format` bit6 and `miscntl` bit6 [M:s].
- So the engine does the spec's AFH remapping (unused channel → used list modulo N) [I, strong].

---

## 8. Inquiry and page as the library drives them

All register writes below are frame fields (§4) unless an address is given; "arm alarm n" is §6.4.

### 8.1 Inquiry (stage 1)

Start [M:s]:

1. Take a free frame (allocator defaults, §4); TX power from the board's "inquiry" level.
2. `0x200C4` = offset of the EIR RX buffer | bit31.
3. `wincntl` = 70; `lmprxptr0/1` and `rxptr0/1` = the FHS RX buffer (20 bytes).
4. `clk_offset` = 0; `bdaddr` = GIAC LAP `33 8B 9E` + `00 00 00`; `bch` = `E2 3A 1A 33 02`.
5. `format` = 8 with the current A/B bit; `linkcntl[txtog]` = `0x4000` (ID).
6. Timeout = CLKN + ms × 1000 / 625; result count; a slot timer reserved for the EIR timeout.
7. Hand the task to the scheduler; when it runs: AGC step from a 6-step cycle (8, 8, 21, 21, 27, 27; values above 15 are clamped to 15), enable the frame
   in the exchange table (even entries), arm alarm 18.

On IRQ 40 RX (frame = inquiry) [M:s]:

- Read `rxstat[rxtog^1]`, `rxphdr[…]`. If the status is not `0x8F` and an EIR was awaited, give up the EIR.
- TYPE 2 (FHS): parse the FHS (§8.4) from the FHS buffer; remote clock offset = remote CLK − `rx_clkn`; RSSI from
  `0x2FD8C` (§10.3). If the FHS's EIR bit is set, wait for the EIR (sub-state 1); else clear `0x200C4` bit31 and
  report. Arm alarm 8.
- In sub-state 1, a DM1/DH1/DM3/DH3/DM5/DH5 (TYPE 3, 4, 10, 11, 14, 15) is the EIR: length from `rxphdr` [12:3],
  bytes in the EIR buffer; clear `0x200C4` bit31, set `0x20000` bit11.
- Duplicates are filtered by a CRC-16 of the 6-byte address (30 kept); a result is reported, then alarm 512.

On the alarm [M:s]: every 4,096 slots flip the A/B train (`format` bit7); after a result, return to sub-state 0, set
`0x200C4` bit31 again, arm alarm 18; stop at the timeout.

### 8.2 Inquiry scan (what V15 does; for reference)

Frame [M:d, M:s]: GIAC address and parity, `format` = 11, `wincntl` = `0x8022`, TX pointers = the FHS payload (V15's:
parity `08 D9 39 52`, its address, class `0x240404`, clock field 0), `txphdr` = EIR length 25 << 3 | 3, `linkcntl`
`0x4102`/`0x4112`, EIR via `0x200C0` = 25 bytes, DM3: `06 09 'F' 'M' '-' '1' 00` (complete local name "FM-1")
followed by the SDK's default manufacturer data. The engine answers an ID with the FHS by itself [I].

### 8.3 Page (stage 2)

Start [M:s]:

1. Refused if two connections already have frames. Take a frame; TX power from the "page" level.
2. `lmprxptr0/1`, `rxptr0/1` = a 20-byte buffer inside the page record.
3. Compute the parity bits for the remote's LAP and for our own (§5.3); reserve an LT_ADDR (1–7).
4. A train-repetition limit from the remote's page-scan mode R0/R1/R2 = 1/128/256 is stored [M:s] (the alarm
   handler's switch uses a fixed 2,048 slots, §7.1). Clock offset: the HCI-style clock-offset argument; when its
   bit15 (valid) is set, offset = bits [14:0] × 2 slots, else 0 [M:s].
5. Build our FHS (§8.4) with that LT_ADDR into the TX buffer.
6. When scheduled: `clk_offset` = estimate; `bdaddr` = remote; `bch` = remote parity; `miscntl` = 0; `wincntl` = 80;
   `txptr[txtog]` = our FHS; `format` = 4 with the A/B bit; `linkcntl[txtog]` = `0x4002` (FHS), the other `0x4001`
   (POLL); both TX buffers marked empty; both `txphdr` = 2; AGC step; enable (even entries); arm alarm 18.

On IRQ 40 RX (frame = page; only TYPE 0 counts) [M:s]:

| Sub-state | Event | Action |
|---|---|---|
| 0 | the slave's ID (page response) | next TX = FHS (type 2), `format` = 6; arm alarm 16 |
| 1 | the slave's second ID (FHS acknowledged) | `clk_offset` = 0, `bdaddr` = **our** address, `bch` = our parity, `rxtog` = 0, OR `0x90` into both `rxstat`; next TX = POLL (type 1), `format` = 2; arm alarm 32 |
| 2 | the slave's reply in the connection | `bufcntl`: clear [11:8], set bit8 (master), clear bit5, set [1:0]; both TX buffers empty; create the connection |

In every sub-state: write `miscntl` LT_ADDR, `format` (with the A/B bit), `linkcntl[txtog]` = type | `0x4010`, the
other = type | `0x4000`, mark `txtog`'s buffer loaded. On the alarm: A/B switch every 2,048 slots, take a new
LT_ADDR, rebuild the FHS, back to `format` 4 and sub-state 0, arm alarm 18; report a timeout at the deadline.

### 8.4 FHS payload (18 bytes, spec layout)

Both directions are packed in the spec's order [M:s, M:d]: bytes 0–3 and bits 0–1 of byte 4 = the 34 parity bits;
LAP (24 bits from byte 4 bit 2); EIR bit; reserved; SR (2 bits); SP; UAP; NAP; class of device (24 bits, bytes
11–13); LT_ADDR (3 bits of byte 14); CLK[27:2] (26 bits from byte 14 bit 3); page-scan mode (3 bits) [S]. V15's
FHS in RAM: `08 D9 39 52 32 65 76 94 F4 06 D9 04 04 24 00 00 00 00` [M:d].

### 8.5 After the connection exists

The master frame stays in `format` 2 with `bdaddr`/`bch` = the master's address, the AFH map applied when LMP sets
it (§7.2), ACL buffers from `link_rx_bulk`, `get_packet_type` choosing DM/DH/2-DH by length and the peer's
max-slot [M:s]. ACL TX: payload in a buffer, `txptr`, `txphdr` (LLID, FLOW = 1, length), `linkcntl` type, then clear
`txbufNcntl` bit0 [M:s]. RX: `rxstat` `0x8F` / `0xAF`, length and LLID from `rxphdr`, LLID 3 = LMP [M:s].

---

## 9. Encryption, coexistence, test modes

### 9.1 Encryption

- E0 runs in the engine: software loads Kc′ (16 bytes) into `cryptk` and sets `linkcntl` bit7 (TX) and bit15 (RX)
  on both buffers [M:s]; no software E0 exists in the library [M:s, absence] → hardware [I, strong].
- Key generation is software (SAFER+ E1/E21/E22/E3, the key-length reduction) [M:s, names]; SSP uses
  **P-192** (`uECC_secp192r1`) [M:s]. No BR/EDR AES-CCM (Secure Connections) path was found [M:s, absence]: the
  stack does legacy SSP with E0. Our stack can do the same; AirPods' acceptance of P-192 SSP is a hardware/interop
  question (§12).

### 9.2 Shared radio with BLE

- Both engines carry a radio-arbitration priority: BLE `RFPRIOCNTL`/`RFPRIOSTAT` (5-bit values 17–30), BR/EDR
  `acldmpriocntl` = 20 and `escodmpriocntl` = 29 by default, raised to 30 (`0x1E1E`) while an ACL link must be
  protected [M:s]. A "hold BLE priority" option fixes BLE's value [M:s]. [I]: a hardware arbiter grants the radio
  to the higher number, per event/slot.
- The two engines have separate control registers, tables and interrupts (BLE `0x28000`, IRQ 45/29; BR/EDR
  `0x20000`, IRQ 40/41) and share `0x2FC40`, the PLL table and the AGC [M:s, M:t].
- Software schedules BR/EDR procedures among themselves by swapping frames (§3.1); a task scheduler gives each a
  run length in slots (inquiry 32, page 32, inquiry scan 26) [M:s].

### 9.3 Interrupts

| IRQ | Source | Priority (library) | Tag |
|---|---|---|---|
| 40 | BR/EDR: RX/frame (bit1), alarm `0x200E4` (bit9), others (§6.2) | 2 | M:s |
| 41 | slot timers `0x2FD40` | 2 | M:s |
| 42 | debug (sets `0x2FC40` bit6) | 0 | M:s |

### 9.4 Fixed-frequency test mode

A frame with `format` 2 (TX) or 3 (RX), `miscntl` = 7 (+ bit3 for EDR types), `linkcntl` bit5 on both buffers,
`txfreq` = `rxfreq` = the hop index, `0x20000` bits 15, 19 and 16 set, `0x200D0` bit0 = 1 (modulated) [M:s]. The
TX buffer holds a payload of one repeated byte; both TX buffers point at it [M:s]. Useful for a first RF check with
an SDR, not for a phone.

---

## 10. RF beyond the BLE driver

### 10.1 What Classic shares with BLE

- The RF start-up is the same routine for both (Wi-Fi front end, VCO scan, calibration, RF-die LUT, BT analog
  block, AGC table) [M:s]: what BLE-HW-FACTS §5, §16 describe, and what Optimist's BLE driver already does
  (`firmware/hal/fm1_ble.h`, which stops before the BR/EDR init).
- The per-frame radio words (§4 bytes 108–143) are the LE block's (TX power = level + 16, PLL_COMP formula) [M:s].
- The LE-test modem switches (`0x2FC24` = `0x6A4040` "2M" / `0x6B4040` "normal") are called only by the LE DTM code,
  not by BR/EDR [M:s]. Nothing BR/EDR-specific in the analog block was found beyond `0x2FC48` = `0xAD` [M:t].

### 10.2 What the BR/EDR init adds (in order) [M:t, matches the IR]

1. `0x2FC40` |= `0x400` (already set by the RF start-up).
2. Zero the table; `0x2FC44` = its address.
3. `0x2000C` = 0, then `0xFFFF0802`; `0x20018` = `0xFFFF`; `0x2002C` = `0x140`; `0x20058` = `0x4000`; `0x2005C` = 0;
   `0x200F0` = `0x200F4` = 0.
4. eSCO ports (§2.3): `0x20120` = `0x06000000`, `0x20128` = `0xC0`, `0x20134` = 8, `0x20120` &= ~`0x10000`,
   `0x20124` = `0x8015`; the same at `0x20150/58/64/50/54`.
5. `0x20000` = `0x107`; `0x20080` = `0x11480A48`; `0x2FC48` = `0xAD`; `0x20084` = 0; `0x2007C` = `0xA04`.
6. eSCO default RX buffers (`0x20138`, `0x20168`, `0x20160`, `0x2015C`, `0x20130`, `0x2012C`).
7. `frq_table[0]` (offset 0); read `0x1400C`; `0x20000` |= `0x80000000`, |= `0x100`.
8. IRQ 40 (priority 2); slot timers `0x2FD40` = `0xFF00`, IRQ 41.

### 10.3 RSSI and AGC

- Real-time RSSI (inquiry results) from `0x2FD8C`: dBm = −1 − (bits 5:0) − T[bits 13:8], as a signed 8-bit result,
  T the AGC dBm table (declared with 33 entries in the BR/EDR code; not transcribed); the per-frame RSSI uses `RSSI2` the same way [M:s] (the same shape as
  BLE-HW-FACTS §3 `RSSI0–3`, with a different constant).
- `RXGAIN0` = AGC step 0–15; software steps it from `RSSI1` per packet [M:s].

### 10.4 Not seen

- Modulation index differences between BR (GFSK h ≈ 0.32) and LE (h ≈ 0.5), and the EDR DPSK modem, have no
  software-visible switch in BR/EDR code [M:s, absence]. Either the engine selects them by itself or `mdm_set`
  does it (0 in V15) [I]. Only hardware can tell (§12, U7).

---

## 11. Stock V15 in the emulator (Classic)

- Init exactly as §10.2 at t = 0.159 s; table at `0x01C0A05C` [M:t].
- From t = 0.38 s, every **0.5 s (800 slots)** [M:t]: inquiry scan enabled (exchange table written, EIR TX
  register, alarm CLKN + 24), its window ends ≈ 14 ms later (alarm, `0x20000` bit31); page scan resumed (`0x20000`
  bit8 cleared and set, alarm + 18), its window ends ≈ 20 ms later (`0x20000` bit10 set, alarm off); the slot timers
  carry the 800-slot period (`0x2FD44` `0x182` → `0x4A2` → `0x7C2` …).
- Frames at 2.5 s [M:d]: frame 0 = inquiry scan (GIAC, `format` 11, FHS + EIR "FM-1", class `0x240404`); frame 1 =
  page scan (`format` 5, its own address `D9:06:F4:1D:99:4C` as LAP `1D994C`, UAP `F4`, NAP `D906`, parity
  `08 D9 39 52 02`), RX into `rx_fhs`, `TXPWER` 11 + 16. So V15 is **discoverable and connectable** on BR/EDR
  (in the emulator; its A2DP sink record is in BT-AUDIO-FEASIBILITY §2.4). The address is generated per unit
  (BLE-HW-FACTS §11, §14.5) [I].
- Consequence: a second FM-1 on stock V15 (or a phone) is a known inquiry target for stage 1.

---

## 12. Unknowns and how to settle each

| # | Unknown | Why it matters | How to find it |
|---|---|---|---|
| U1 | Does the engine transmit at all with our init (the BLE-only RF start-up + §10.2)? | stage 1 | the probe (§14): any FHS received proves TX (IDs) and RX |
| U2 | Exchange-table indexing (slot mod 16?) and odd/even semantics | master/slave scheduling | probe: open only even entries vs all, count results; later a two-device test |
| U3 | `wincntl` units and bit15 | RX windows | probe: inquiry with 70 vs 0x8022; page scan reference from V15 |
| U4 | `0x20000` bits 0–2, 10, 11, 30, 31; `0x2000C` upper bits; `0x20010` bits 0, 2, 10, 11 | clean stop/restart, interrupt load | probe: count each pending bit per second; toggle one bit at a time |
| U5 | Page: does the engine send the FHS by itself after the slave's ID, or must IRQ 40 set it in time? | ISR deadline, CPU 1 vs CPU 0 | stage 2: log the CLKN of the ID IRQ and of the next RX; try with the IRQ delayed |
| U6 | `linkcntl` bits 5, 8, [13:9]; `rxstat` bits 4, 7, 9; `bufcntl` bits | data path | stage 2/3 experiments; compare with V15 frames dumped while a phone connects (needs a real FM-1 or an engine model) |
| U7 | BR modulation index / EDR modem selection (`mdm_set`) | link quality, EDR | SDR capture of our fixed-TX mode (§9.4) vs stock; or EDR packets acknowledged by the peer |
| U8 | `PLL_COMP` / crystal error on a real unit (the inquiry/page sweep exists for a reason) | page success rate | probe: inquiry success vs the offset table steps |
| U9 | Whether stock V15 on hardware is really discoverable as "FM-1" | a free test target, an RF oracle | a phone's Bluetooth list near a stock FM-1 (no flash needed) |
| U10 | AirPods and P-192 legacy SSP + E0 | the whole A2DP plan | stage 3; a phone-side HCI log of an AirPods pairing shows what they accept |
| U11 | IRQ 40 cost and frequency during a connection | audio underruns | stage 2: ISR timing with the CDC console |
| U12 | Coexistence with our BLE link (arbiter numbers) | BLE MIDI jitter | stage 2+: both running, count missed BLE events |

An emulator engine model for BR/EDR (like the LE one on `feat/ble-engine`) would turn U2–U6 into [M:t] against stock
V15, as the BLE model did. It is larger than the LE one (hop kernel, trains) and not needed for stage 1.

---

## 13. How this was measured (reproducible, scratch only)

Scratch: `~/GitHub/optimist-work/btclassic/` (never `/tmp`).

- IR: `llvm-ar x …/cpu/wl82/liba/btctrler.a` into `ar/`; `llvm-dis --disable-auto-upgrade-debug-info` into `ll/`;
  `strip.py` (bodies with names, MMIO addresses annotated), `structs.py` (debug-info structs with offsets),
  `fn.py` (one function), `mmio.py` (every MMIO access per function) into `s/`.
- V15: `diagnose FM-1.fwsc 900000000` with `FM1_MMIO_TRACE`, `FM1_MMIO_RANGES=20000-201ff,2fc40-2fc4f,2fd40-2fd67`,
  `FM1_MMIO_READS=1`, `FM1_DUMP=01c0a05c:4196` (and a second run dumping `0x01C0B10C` and `0x01C0B120`);
  `decode_bd.py` decodes the table. Output in `v15/`.
- Sync word: `syncword.py`, written from the Core spec, checks the two vectors of §5.3.

---

## 14. Stage 1 proposal: an inquiry probe build

**Goal.** Prove that our own code can make the BR/EDR engine transmit inquiry IDs and receive FHS (and EIR)
responses, and print who answered. No connection, no LMP. Builder item `BT_INQ_PROBE` (EXPERIMENTAL, default off),
console-only, BLE disabled in that build (one engine at a time for the first test).

**Stage 1a (emulator first, then FM-1): clock and interrupts.**

1. After the existing RF start-up, run §10.2 with our own 4,196-byte table (aligned, in RAM; frames at +368).
2. Attach IRQ 40 and IRQ 41 at a priority above the audio ISR (or on CPU 1).
3. Print once a second: CLKN (`0x20020`, expect +1,600/s), the fine count, IRQ 40 counts per pending bit, IRQ 41
   count; arm a slot timer every 1,600 slots and the `0x200E4` alarm every 18 slots and count both.
   The emulator models these registers, so 1a can be tested before any hardware run.

**Stage 1b (FM-1): inquiry.**

1. Frame 0 with the allocator defaults of §4 (`linkcntl` `0x4010`, `escorxstat` `0x83`, `rxmaxbuf` `0x300`,
   `txphdr` `0x6007`, prio `0x1414`/`0x1D1D`, `bufcntl` `0x202C`, `TXPWER` = `TXSET` = 16 + level, `RXGAIN0` = 15,
   `PLL_COMP` from the stored crystal offset or 0).
2. §8.1 start steps 2–5 (GIAC, parity `E2 3A 1A 33 02`, `format` 8, `wincntl` 70, ID); exchange table: all 16 entries
   = frame offset, even entries open; alarm 18.
3. IRQ 40 bit1: read `rxstat[rxtog^1]`; on `0x8F` with TYPE 2 parse the 18-byte FHS (spec layout) and print once
   per address; on TYPE 3/4/10/11/14/15 after an FHS with the EIR bit, print the EIR's name field. Re-arm the RX
   buffer (`rxbufNcntl` bit0 clear, `linkcntl` bit14 set). Keep a histogram of `rxstat` high bytes.
4. Alarm: re-arm 18; flip `format` bit7 every 4,096 slots; stop after 10.24 s (the spec's default inquiry length).

**Console output (CDC).**

```
btinq: up clk=0x00001a2c fine=311 irq40{rx=0 alm=0 b0=0 b2=0 b10=0 b11=0} irq41=0
btinq: 1s clk+1600 rx=12 alm=89 stat{8f:3 af:0 other:9} fhs=1 eir=1 train=A
btinq: FHS 4c:99:1d:f4:06:d9 cod=0x240404 sr=1 clk=0x0123456 dclk=-5321 rssi=-48 eir=1
btinq: EIR 4c:99:1d:f4:06:d9 name="FM-1"
btinq: done 10.24s results=2
```

**Targets.** A phone on its Bluetooth settings page (discoverable); AirPods in pairing mode (case open, button held);
a second FM-1 on stock V15 (§11, if U9 holds). Success = at least one correct BD_ADDR printed and confirmed against
the device's own settings page. Failure modes and what they tell: CLKN not running → init (U4); alarms but no RX →
TX/RF (U1, U7); `rxstat` with sync but bad status → modulation/offset (U7, U8).

**Size and risk.** About 1–2 KB of flash and 4.3 KB of RAM (the table), no change to the audio path when the item is
off. Asks the user before any flash (one OK = one flash).

### 14.1 As built (2026-10-18)

Builder item `BT_INQ_PROBE` (bit 280, EXPERIMENTAL, default off; needs `USB_MODE` 1, no `BLE`): `firmware/hal/fm1_bredr.h`
(the registers, the frame, the init list, the clock, the alarm, the slot timers, IRQ 40/41 entries) and
`firmware/src/bt/bt_inq.c` (1a at boot ~1.5 s, `btclk [run]`; 1b `btinq [SECONDS|stop]`), the portable part
`firmware/src/bt/bt_inq_core.c` (sync-word parity, FHS, EIR name; host test `tests/bt_inq_test.c`). Stage 1a passes in
the emulator (`tests/bt_inq_emu_test.py PACKAGE.fwsc`, fm1-emulator `feat/upstream-merge`: 1,599.5 slots/s over
3,006 ms, 264 alarms of ~267, 3 slot-timer fires, no unknown IRQ 40 bit). Two emulator notes: `0x200E4` has no
readback (the probe keeps the armed slot itself), and `0x200C4` / `0x20090` are not modelled, so 1b cannot run there.
The `fm1-emulator-ble` (`feat/ble-engine`) binary stops current builds at boot (an SPI NOR command it lacks); its
`feat/upstream-merge` sibling also stopped once at a store-immediate form (`0xEA7B`, a negative offset) the compiler
emitted for the init, now written as a list of plain stores. Not run on an FM-1.

---

## 15. Verdict for driving BR/EDR ourselves

- **Mapped with confidence:** the table and frame layout, the register writers and their fields, the init sequence
  (IR and V15 trace agree), the slot clock, alarm and slot timers, the sync-word parity (verified against the spec),
  the procedure codes and the inquiry/page/scan sequences, AFH tables, the TX/RX hand-over, encryption key loading,
  and the interrupt structure.
- **Main unknowns:** exact semantics of a dozen control bits (U2–U6), whether the engine sends the page-response FHS
  by itself (U5, the ISR deadline), BR/EDR modem settings (U7), and everything above the baseband (LMP, SSP with
  P-192, L2CAP, AVDTP, SBC) — which is the large part of the work.
- **View: go for stage 1 and 2; no promise yet for A2DP.** The hardware does the hard per-slot work (hop kernel,
  trains, coding, ARQ, E0, AFH remap), and the software interface is the same kind of SRAM frame engine that our
  BLE route C mastered. Inquiry is a cheap, decisive experiment. The real cost and risk sit in the host-side
  protocols and interoperability with AirPods (BT-AUDIO-FEASIBILITY §3.3: 9–18 person-months), not in the
  baseband; that decision can wait for stage 2's result (a paged ACL link with LMP version exchange).

# BLE MIDI, route C: our own stack (link layer, host, BLE-MIDI)

Status: **EXPERIMENTAL. Host-tested, and end to end in the emulator against its model of the BLE engine (§11).
Never run on an FM-1: the radio's start-up now follows stock V15's second boot with the stored trims (§12), but
parts of it stay TODO(hardware) (§12.6), so no packet has left a device.** Build flag `FELUCCA_BLE` (default 0 =
off; the image with it off is byte-identical to one without this code, but for the builder's configuration hash). A
BLE build needs the user's own stock V15 and the emulator to capture the radio's tables (§12.3). Builder item `BLE`
(Experimental group). Background: `BLE-MIDI-FEASIBILITY.md` §9 (route C, the stock firmware's behaviour in §9.2).

Tags: **[M]** measured this session (host tests, the JieLi toolchain). **[S]** from a published
specification. **[I]** inference, not measured. **[HW?]** needs the hardware fact sheet or a device.

## 1. Provenance (clean room)

Written from the Bluetooth Core Specification v5.x (Vol 3 Parts A, C, F, G, H; Vol 6 Parts B, C, E; LE 1M
only) and the MIDI Association's "Specification for MIDI over Bluetooth Low Energy" 1.0. No vendor code, no
vendor libraries, LLVM IR or disassembly were read; no Cordio source. The ideas (not the code) of the route B
study's minimal ATT host were reused. Test vectors and their sources are named in each test file's header.
The baseband driver (§11) was written from `docs/BLE-HW-FACTS.md` (branch `feat/ble-facts`, 41b7374, another
author), the Core spec and the answers of the emulator's engine model (itself clean-room code from the fact sheet),
against the interface in §3; no vendor IR, disassembly or SDK library was read for it either.

## 2. Layout

| File | What |
| --- | --- |
| `firmware/src/ble/ble_cfg.h` | every setting (name, intervals, data length, MTU, ring sizes, encryption on/off) |
| `ble_prim.c` | CRC24, whitening, CSA #1 with the channel map, access-address rules (Vol 6 Part B) |
| `ble_aes.c` | AES-128 (encryption only) and the LL's AES-CCM; `BLE_AES_HW=1` takes a hardware `ble_aes128()` |
| `ble_hw.h` | **the baseband driver's interface** (§3) |
| `ble_ll.c` | link layer, peripheral only, one connection (§4) |
| `ble_host.c` | GAP advertising data, L2CAP fixed channels, LE signalling, SMP ("Pairing Not Supported") |
| `ble_att.c` | ATT server, the static GATT database, the BLE-MIDI data path (§5) |
| `ble_midi.c` | BLE-MIDI packets to / from USB-MIDI event packets (§6) |
| `ble.h` | the firmware's view: `ble_init`, `ble_enable`, `ble_midi_ready`, four callbacks |
| `ble_stack.c` | the unity-build wrapper: every function `static` (`BLE_API`), unused ones dropped |
| `ble_hw_wl82.c` | **the baseband driver** on the AC791N / WL82 engine (§11): the RAM block, control block, events |
| `firmware/hal/fm1_ble.h` | its registers: the column port, interrupts, the radio's start-up, the IRQ entries |
| `ble_hw_stub.c` | a stand-in driver that never calls back (`FELUCCA_BLE_STUB=1`: the stack alone) |
| `ble_vm.c` | the radio's stored trims: stock V15's VM read in place, Optimist's copy, VM → copy → none (§12.2) |
| `firmware/hal/fm1_ble_rf.h` | the radio's start-up program runner, the BBP windows, the RF-die SPI port, the VCO scan (§12.1) |
| `tools/ble_rf_capture.py`, `tools/ble_vm.py` | the build-time capture of the start-up tables from stock V15 in the emulator (§12.3) |
| `firmware/src/io/midi/midi_ble.c` | the MIDI router side (§7) |

Portable C, no OS calls, no `malloc`, no C library (the firmware builds `-fno-builtin` freestanding).

## 3. The driver interface (`ble_hw.h`)

Designed for a link-controller engine that does each event's timing in hardware: advertising on 37/38/39
with an automatic SCAN_RSP; anchors and the transmit window; CSA #1 hopping; the access-address match; CRC
(software gives the CRC init); SN/NESN with acknowledgement and retransmission from two alternating TX
buffers and two RX buffers; the connection event counter and the instant compare.

**One context.** Every `ble_ll_hw_*` call comes from the driver's BLE interrupts (the event IRQ and the RX
IRQ at one priority, never nested in each other), and the link layer calls `ble_hw_*` only from inside them
(and `ble_hw_adv_start` once at start-up, before those interrupts are on). The host and the BLE-MIDI pump run
in that context too; the rest of the firmware talks to it through single-producer rings only.

The link layer asks (the driver implements):

| Call | Meaning |
| --- | --- |
| `ble_hw_adv_start(&{adv PDU, scan-rsp PDU, interval, channels})` | connectable undirected advertising; a CONNECT_IND to our AdvA goes to `ble_ll_hw_connect_ind` |
| `ble_hw_adv_stop()` | |
| `ble_hw_conn_start(&{AA, CRC init, WinSize, WinOffset, Interval, Latency = 0, Timeout, ChM, Hop, SCA})` | called inside `ble_ll_hw_connect_ind`: leave advertising, listen in the transmit window; event 0 is the first |
| `ble_hw_conn_update(&{WinSize, WinOffset, Interval, Latency, Timeout, Instant})` | LL_CONNECTION_UPDATE_IND: the engine switches at the instant |
| `ble_hw_chmap_update(ChM, Instant)` | LL_CHANNEL_MAP_IND |
| `ble_hw_set_lengths(max_tx, max_rx)` | the data length in use after LL_LENGTH_REQ/RSP (27..251) |
| `ble_hw_tx_kick()` | something was queued: fill a free TX buffer now or at the next event |
| `ble_hw_conn_stop()` | leave the connection |
| `ble_hw_addr(addr)` | the device address; returns 0 public, 1 random static |
| `ble_hw_time_us()` | a free-running microsecond clock (supervision and procedure timeouts) |
| `ble_hw_rand(out, n)` | random octets (SKDs, IVs; only with `BLE_LL_ENC`) |

The driver reports (the link layer implements):

| Call | Meaning |
| --- | --- |
| `ble_ll_hw_connect_ind(pdu, len)` | a CONNECT_IND to our AdvA; returns 1 when taken |
| `ble_ll_hw_rx(pdu, len)` | a new data PDU: CRC good, not a retransmission; header octet as received |
| `ble_ll_hw_tx(pdu)` | a TX buffer is free: the LL writes header (LLID, MD) and payload, returns 2 + length or 0 (empty PDU); the PDU is final (already encrypted), the engine owns SN/NESN |
| `ble_ll_hw_tx_acked()` | the oldest PDU handed out was acknowledged |
| `ble_ll_hw_event_end(counter, rx_ok)` | a connection event closed (every event, also those with nothing received) |

PDUs cross the interface as on air without AA and CRC: 2-octet header + payload (up to 251, + 4 MIC when
encrypted).

## 4. Link layer

- **Advertising:** ADV_IND = AdvA + flags (`02 01 06`) + the BLE-MIDI 128-bit UUID (`11 07 …`), ChSel 0 (we
  only do CSA #1); SCAN_RSP = `09 09 "FM-1_BLE"` (stock §9.2, without stock's "sinco" manufacturer data).
  100 ms, 37/38/39.
- **CONNECT_IND checks:** our AdvA and RxAdd; Interval 6..3200; Timeout 10..3200 and > (1 + Latency) ×
  Interval × 2; WinSize 1..min(8, Interval − 1); WinOffset ≤ Interval; Hop 5..16; ≥ 2 channels; not the
  advertising AA. Anything else is ignored and advertising goes on.
- **Timers:** the connection fails (0x3E) without a packet within the window + 6 intervals; supervision
  timeout (0x08) from the last good packet; 40 s LL response timeout (0x22) for our procedures and for the
  central's when we wait on it; our LL_TERMINATE_IND gives up after one supervision timeout.
- **Procedures:** version exchange (answered once); feature exchange (octet 0 = both; we start
  LL_PERIPHERAL_FEATURE_REQ after 6 events if the central has not); data length (we start it when both have
  DLE; LL_LENGTH_REQ answered); connection update and channel map with instants (an instant already passed
  ends the link, 0x28); LL_CONNECTION_PARAM_REQ accepted when valid, else LL_REJECT_EXT_IND 0x1E; PHY_REQ
  answered 1M / 1M, a PHY update to anything but 1M ends the link (0x1A); ping; termination both ways;
  LL_ENC_REQ refused (0x1A) unless `BLE_LL_ENC=1`, which runs the encryption start with a key from the host
  (no pairing method yet, so 0x06 without one); LL_UNKNOWN_RSP for unknown or malformed control PDUs.
- **Features claimed:** connection parameters request, extended reject, peripheral-initiated feature
  exchange, data length extension (and encryption + ping with `BLE_LL_ENC`). Version 9 (5.0), company 0xFFFF.
- **Queues:** control PDUs go first; L2CAP frames wait whole in a byte ring (`BLE_LL_TX_RING`, 1 KiB) and
  are cut to the data length when the engine pulls them. A frame is queued whole or not at all.

## 5. Host and GATT

| Handle | Attribute |
| --- | --- |
| 1 | GAP service 0x1800 |
| 2, 3 | Device Name (read): `BLE_DEVICE_NAME` |
| 4, 5 | Appearance (read): `BLE_APPEARANCE` (0, unknown) |
| 6, 7 | Peripheral Preferred Connection Parameters (read): 6, 9, 0, 100 |
| 8 | GATT service 0x1801 |
| 9, 10, 11 | Service Changed (indicate) and its CCCD |
| 12 | BLE-MIDI service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700` |
| 13, 14, 15 | MIDI I/O `7772E5DB-3868-4112-A1A9-F2669D106BF3` (read: empty, write without response, notify; Write Request accepted too) and its CCCD |

- ATT: MTU exchange (ours 247: one 251-octet LL PDU), Find Information, Find By Type Value, Read By Type
  (16-bit UUIDs also in 128-bit base form), Read, Read Blob, Read By Group Type, Write Request and Command,
  Handle Value Confirmation. Request Not Supported for the rest; commands ignored. No attribute needs
  security.
- Service Changed: indicated for 0x0001..0xFFFF when a client turns its indications on
  (`BLE_SC_ON_SUBSCRIBE`): without bonding we cannot know what a client cached, and stock has no Service
  Changed at all (§9.2), which iOS is known to cache against [I].
- When a client turns the MIDI notifications on and the interval is outside 6..9, an L2CAP Connection
  Parameter Update Request {6, 9, 0, 100} goes out; after a reject, once more {12, 12, 0, 100} (stock's
  values and order, §9.2).
- Signalling: Command Reject (not understood) to every request but the response we wait for. SMP: Pairing
  Failed "Pairing Not Supported" to a Pairing Request. `BLE_SMP_LEGACY` stops the build with an `#error`
  until legacy Just Works is written (c1/s1 on `ble_aes128`, then the key slot `ble_host_set_key` feeds the
  LL's encryption).

## 6. BLE-MIDI

- **Out:** one notification per connection event (up to `BLE_MIDI_PER_EVENT` = 2 with a backlog), each
  packing as many events as fit in MTU − 3. Real 13-bit millisecond timestamps (stock sends `80 80`);
  running status inside a packet; the high timestamp part may step once inside a packet, a bigger jump starts
  a new one; SysEx over packets (continuation packets start with data; F7 after a timestamp). Notifications
  leave room for one full ATT response in the TX ring.
- **In:** decoded to USB-MIDI event packets with their timestamps: running status with or without a
  timestamp octet, low-part wrap, real time anywhere (also inside SysEx), SysEx over packets, a status
  cutting SysEx short, system common. A message cut at a packet's end is dropped.

## 7. Into the firmware (`io/midi/midi_ble.c`)

- **In:** channel messages only (stock: BLE in → the synth). The BLE interrupt puts them in `ble_in_q`; the
  TIMER5 tick moves them into `midi_in_q` (whose producers must all run in TIMER5), where `events_block`
  plays them as USB's. Each one's time is the arrival less its distance to the packet's newest timestamp.
  Clock and transport from BLE are not passed on (clock_sync.c follows USB and TRS).
- **Out:** `midi_out_event` (the audio ISR; the FM-1's own notes, the stream USB gets) also queues into
  `ble_out_q` with `fm1_ms` while a client listens. Nothing is bridged between USB / TRS and BLE.
- `ble_midi_route`: bit 0 in, bit 1 out (both on). BLE starts advertising at boot, as stock.

## 8. Sizes [M]

JieLi clang 4.0.1, `-Os -ffunction-sections -fno-builtin`, each file as its own object (all functions
kept):

| Object | .text | .rodata | .bss |
| --- | --- | --- | --- |
| `ble_prim.c` | 454 | 0 | 0 |
| `ble_ll.c` | 3,172 | 26 | 1,300 |
| `ble_host.c` | 774 | 27 | 260 |
| `ble_att.c` | 2,010 | 242 | 280 |
| `ble_midi.c` | 1,460 | 16 | 0 |
| **stack, no encryption** | **8,181 B flash** | | **1,840 B RAM** |
| `ble_aes.c` (`BLE_LL_ENC=1`) | 866 | 256 | 0 |
| `ble_ll.c` with `BLE_LL_ENC=1` | 3,584 | 26 | 1,624 |
| **stack with LL encryption** | **9,715 B flash** | | **2,164 B RAM** |

Against the 20 KB target that leaves room for the driver and SMP. In the firmware with the driver, see §11.6:
user-default + `BLE=1` is **+13,144 B flash and +5,504 B RAM**, 10,552 B more than the app slot holds, so a BLE
build must leave something out (the emulator test drops the FLUTE sample set, 31 KB). The builder's costs.json:
+12,836 B flash, +5,488 B RAM on its base. With `FELUCCA_BLE=0` the code is identical: only the builder's
configuration hash (it covers the registry) differs, 4 words, same size.

## 9. Tests (`tests/run_tests.sh`, ASan + UBSan where the compiler has them)

- `ble_prim_test.c`: AES-128 (FIPS-197 C.1; the Core spec's session key SK = e(LTK, SKD)); AES-CCM (the
  Core spec's sample packets, Vol 6 Part C 1: the central's LL_START_ENC_RSP decrypted, the peripheral's data
  packet encrypted); CRC24 (two packets as scapy frames them); whitening (channel 37's published sequence);
  CSA #1; the access-address rules.
- `ble_stack_test.c`: a simulated central through a fake `ble_hw`: advertise → CONNECT_IND (and the invalid
  ones) → version / feature / length / PHY / ping / parameter request → MTU → discovery as CoreBluetooth
  runs it → Service Changed → CCCD → connection parameter request → MIDI notify and write → connection update
  and channel map with instants → terminate (both ways), supervision timeout, failed establishment, instant
  passed, 40 s procedure timeout → advertising again. Built twice: `BLE_LL_ENC=0` (encryption refused) and
  `BLE_LL_ENC=1` (the Core spec's encryption sample end to end: SK, the central's encrypted START_ENC_RSP, our
  data packet byte for byte, a MIC failure ending the link).
- `ble_midi_test.c`: decoder and encoder edge cases and a 20,000-event round trip through packets of random
  size.
- `ble_vm_test.c`, `ble_rf_capture_test.py`: the stored trims and the capture tool (§12.5).

## 10. Open questions for the hardware fact sheet

Facts the driver needs that `docs/BLE-HW-FACTS.md` (41b7374) does not settle; most are its U-list items. §11.2 says
what the driver assumes for each (from the engine model's answers, which come from stock V15 running in the model,
not from hardware):

1. **Retransmissions on RX:** does the engine filter a repeated packet (same SN) before the RX IRQ, or must the
   driver compare SN itself? The link layer expects only new PDUs.
2. **An event IRQ for every connection event,** also when nothing was received (a lost link): the supervision
   timeout runs in `ble_ll_hw_event_end`. Does IRQ 45 fire then, or only the second group (`0x2804C`, "anchor
   count timeout")?
3. **TX "finished" = acknowledged?** The TXTOG / TXBUFnCNTL transition is read as the peer's acknowledgement
   (RXSTAT bit 8 also says so): which one is authoritative, and is a NAKed buffer resent by the engine alone (U7)?
4. **Long packets:** the RX buffers hold 255, RXMAXBUF = 0xFF; does the engine time 251-octet PDUs (2,120 µs) on
   1M without other settings (window, IFS)? Until known, `BLE_LL_MAX_OCTETS` can be set to 27.
5. **MD:** does the engine keep a connection event open while MD is set on either side, and how many packet
   pairs per event?
6. **The first anchor** after CONNECT_IND, and whether the engine needs anything else to stop advertising (U6).
7. **Instant switch:** is the column-5 instant really what the engine gates the new interval / map on, and must
   the software writes land in event instant − 1 exactly (9 says so) — what if an update arrives later than that?
8. **Address:** VM 104's record format (U3), and whether a unit without it should use the generated address at
   `0x0E9000` (stock's BLE address there is the classic one with the low 3 octets changed).
9. **AES block:** registers and byte order of the hardware AES the feasibility doc mentions (for `BLE_AES_HW`);
   not in the sheet.
10. **ISR budget and priority** against audio (U8), and whether IRQ 45 / 29 may run above the audio IRQ.
11. **Time base:** whether the 24-bit slot count (columns 0 / 14) or TIMER4 should drive `ble_hw_time_us`.

## 11. The baseband driver (`ble/ble_hw_wl82.c`, `hal/fm1_ble.h`)

Clean room, from the fact sheet ("HW §n": `docs/BLE-HW-FACTS.md` on `feat/ble-facts`, 41b7374), the Core spec and the
engine model's answers (fm1-emulator `feat/ble-engine` 6531e20, which stock V15 runs in). Every register write in
`hal/fm1_ble.h` and every control-block write in the driver names its HW section. `FELUCCA_BLE=1` builds it;
`FELUCCA_BLE_STUB=1` the stand-in instead. The register accesses live in `hal/` (build.py's register check).

### 11.1 What it does

- **Baseband RAM block** (HW §4), one static 1,508-byte block, base / end in `0x2FC84` / `0x2FCBC`:

  | Offset | Size | What |
  | --- | --- | --- |
  | 0x000 | 16 | instance table: entry 0 = `0x8040` (link 0's control block), the rest 0 |
  | 0x010 | 48 | software area (unused) |
  | 0x040 | 324 | link 0's control block (HW §3; a `struct ble_cb` with `_Static_assert`ed offsets) |
  | 0x184 | 2 × 284 | RX buffers: 20-byte software header + 264 payload (advertising needs 263, HW §4) |
  | 0x3BC | 2 × 276 | TX buffers: 20-byte header + 256 payload (251 + a MIC) |

  The pointers (TXPTRn / RXPTRn) are the payloads' offsets. The last two octets of each software header hold the PDU
  header, so the link layer reads and writes PDUs in place (no copy): `ble_ll_hw_rx` gets RXDHDRn rebuilt in front of
  the payload, `ble_ll_hw_tx` writes its header there and the driver moves it into TXDHDRn.
- **Start-up** (HW §5.5): `0x2FC80` bit 0, `0x28000` = 1, `0x28034` = `0x0A01` (stock V15's), the window, the
  timing words `0x28008`–`0x28018`, then IRQ 45 / 29.
- **Advertising** (HW §5.5 end, §6, in stock's order): link opened (17 columns 0, IRQs off, the control block as
  stock initialises it), RFPRIO 26, interval in columns 1 / 15, ADVIDX `0xC4E2`, column 8 `0xC000` (advDelay),
  column 6 `0x41A5` (37 / 38 / 39), RX armed, ADV_IND in TX buffer 0 and SCAN_RSP in TX buffer 1 (TxAdd moved from
  the Core's bit 6 to TXAHDR bit 4), local-address match on, LOCALADR, FORMAT `0x000C`, state 2, the IRQ enables in
  stock's order, the start (column 7, 0, 14, 0, 14 = `0x8000`).
- **CONNECT_IND** (HW §7): in the RX interrupt; the PDU (RXAHDRn and the payload) goes to `ble_ll_hw_connect_ind`,
  which checks it and calls `ble_hw_conn_start` before returning: HW §7 steps 1–17 in the vendor's order (column 4
  before column 2 = state 7, which is what leaves advertising; window widening from SCA; the first window
  WinSize × 1.25 ms + 1.25 ms; the channel tables; empty PDUs with opposite SN in the two TX buffers). A CONNECT_IND
  the link layer refuses restarts advertising (the engine stopped it).
- **Per event** (HW §8): IRQ 29 delivers new packets in the engine's buffer order with **a software SN check** (a
  repeat is dropped, never delivered twice), re-arms the buffer, then checks the acknowledgements and refills. A TX
  buffer the driver loaded (TXBUFnCNTL bit 0 cleared) is acknowledged when the engine sets bit 0 again; it loads the
  buffer TXTOG names, and the other one only behind it, so two PDUs can be in flight in order. IRQ 45 first takes a
  pending reception of the same event, then reads the counter (column 3 − 1, op 2), applies the instants, narrows
  the receive window to WINCNTL2's 50 µs (and clears column 4) after the first packet of a new anchor, and calls
  `ble_ll_hw_event_end` with rx_ok = an RX interrupt in this event or EVTCOUNT = the counter (a repeat the engine
  dropped still counts as a reception). Supervision is the link layer's.
- **Instants** (HW §9): column 5 = instant when the update arrives (RFPRIO 30); in event instant − 1's IRQ 45 the
  new window (WinSize × 1.25 ms + 625 µs), latency, widening, column 4 and columns 1 / 15, or the channel tables. An
  update the driver sees late is applied at once and counted (`ble_hw_stat.late_instant`).
- **Time base**: the 24-bit link clock (columns 0 / 14, op 2, HW §2.3) extended to 32-bit microseconds, 625 µs
  resolution. It stops while no link runs and restarts at 0 with advertising; nothing in the link layer measures
  across that.
- **Random numbers**: `0x13B00` / `0x13B04` (HW §1).

### 11.2 The open questions of §10, as the driver takes them

1. Repeats: the engine drops them (model default) and the driver checks SN as well; EVTCOUNT still counts a repeat
   as a reception. Tested with `old_sn=store` (the engine stores repeats: the driver drops them) and `old_sn=irq`.
2. IRQ 45 after every connection event, received or not (model default, from stock). `event_irq_every_event=0`
   also passes the end-to-end run.
3. TXBUFnCNTL bit 0 back to 1 = acknowledged (model). The engine resends a NAKed PDU (model default);
   `engine_retransmits=0` passes too, because the driver leaves a loaded buffer alone until it is acknowledged.
4. Long packets: nothing is programmed for the data length (RXMAXBUF 255, TX room 255). Stock never uses DLE; our LL
   asks for it and the virtual central answers 27. Unknown on hardware.
5. One pair per event by default; `pairs=4` passes.
6. First anchor = the CONNECT_IND's end + 1.25 ms + WinOffset; leaving advertising = column 2 from state 2 to 7
   (model). The driver programs state 7 8–16 µs after the CONNECT_IND in the emulator (1,234 µs before the window).
7. New interval / map written in event instant − 1 (model default `instant_gating=1`; `instant_gating=0` passes).
8. Address: the VM can be read now (§12.2), but stock V15 writes no 104 (HW §14.5), so the driver offers a random
   static address and the firmware keeps it (§11.5).
9. AES: not used (`BLE_LL_ENC=0`); the block at `0x41200` is not touched.
10. Priority: §11.3.
11. Time base: the link clock (above).

The engine must report empty PDUs (IRQ 29 for every packet: the model's answer from stock). With
`store_empty=0` the driver never sees the central's empty PDUs, so the link layer counts no reception and drops
the link after six intervals: not a behaviour stock suggests, but the driver depends on it.

### 11.3 Interrupts and priority

IRQ 45 (event) and IRQ 29 (RX) at **priority 2**, on CPU 0: below the audio (IRQ 11, 3) and the TIMER5 tick (63, 4),
stock's own choice (HW §5.5, §10). Both at one priority, so they never nest in each other: the one context
`ble_hw.h` asks for. Why below the audio: the engine does every radio deadline in hardware (T_IFS, the anchors,
acknowledgements, retransmissions); what is left for software is long: state 7 before the transmit window (≥ 1.25 ms
after the CONNECT_IND), a TX refill and the instant writes within one interval (≥ 7.5 ms). An audio half (≤ 0.78 ms at
96 MHz in the emulator, ≤ 0.39 ms at 192 MHz) fits inside them, so BLE can wait for the audio and the audio never
waits for more than one BLE handler (§11.7). The rings to the rest of the firmware are single-producer
(`midi_ble.c`), so the TIMER5 tick and the audio preempting a BLE handler are safe. The handlers run from flash
(no `.ram_hot` room is needed); every flash write disables all interrupts (`hal/fm1_flash.h`), during which the
engine runs on alone and missed events are the central's retransmissions.

### 11.4 The radio's start-up (`fm1_ble_rf_init`), unverified

Since the fact sheet's §14–§19 (d907ce3) the Wi-Fi front end, the stored trims and the AGC table are done: §12.
Then, in the fact sheet's order: HW §5.1 step 3 (`0x10010` bits 14–15, `0x14000` → `0x000C0081`, with delays of
unknown length: `FM1_BLE_STEP_DELAY_US` = 100 µs is a guess), step 4 (`0x2FC40`, `0x20000`, `0x2FC78`), HW §5.4's
PLL channel table (stock's `{i | i << 8, 0, 0}`, 972 B of RAM), the captured AGC table, the AGC configuration
words, `0x2FC48`, `0x2FC00`–`0x2FC28` with stock's first-written trim fields, `0x2FC98`–`0x2FCA0`. Not done: PLL_COMP
from VM 110 (V15 writes no 110, HW §14.5; U14); the BR/EDR baseband and slot timer (U12); the BT TX trims (§12.6).
Values the sheet marks unknown are named constants (`FM1_BLE_STEP_DELAY_US`, `FM1_BLE_T34_VALUE`,
`FM1_BLE_BUSY_POLLS`). The emulator models none of the radio's analog side beyond the PLL comparator and the filter
result, so in it the start-up's writes can be compared with stock's (§12.4), not its effect.

### 11.5 In the firmware

- **Order** (`system/main.c`; the settings are read first, `persist_boot`, so the saved BLUETOOTH is known): `audio_init`, `usb_start`, `uart_midi_init`, then `ble_midi_init`: the radio, the
  baseband, the address, advertising set up; the interrupts come on with the rest just after (`fm1_irq_enable_all`).
- **Address**: VM 104 (public) when the driver can read it (not yet, §11.2.8); else a random static address made
  once from the random source and kept with the settings (`persist_t.ble_addr`, appended last: a build without BLE
  reads the rest as its own, and when it saves its settings the address is gone, so the next BLE build makes a new
  one; a record from a build without BLE reads as "no address yet"). It is saved like a setting changed while playing: once the FM-1 is quiet. Emulator: the first boot
  saves it (sector 0xFC000), the second boot reads it and writes nothing.
- **On / off at run time**: **HOME held > MENU > SYSTEM (last screen) > BLUETOOTH**, ON / OFF, only in a build with
  `FELUCCA_BLE=1` (the builder item BLE decides what is in the image; the switch is the HOME menu's, as for every
  run-time feature). ON is the default, so BLE still starts advertising at every boot, as stock does. It is a setting of
  the FM-1, kept in the settings word (`storage/settings_word.c`, **bit 21, inverted**: 1 = OFF). No settings-record
  change and no version bump: the word is part of the record in every build, and a record from before the bit (or one
  SLOOP 2.3 / 2.4 wrote) has it 0 = ON; a build without BLE keeps the bit as read (`bp23_kept`) and writes it back, so
  OFF survives a round trip through such a build. The record is saved when the menu closes (`menu_close`), as for the
  other menu settings. With `FELUCCA_BLE=0` none of it is compiled: no menu row, no state, no new code.
  - `ble_midi_set(on)` (`io/midi/midi_ble.c`, main loop): holds the two BLE interrupts at the interrupt controller
    (`fm1_ble_irqs_hold`: the link layer's state is otherwise changed by the RX interrupt too), sets the flag, calls
    `ble_enable(on)`, lets them go.
  - **OFF while advertising**: `ble_hw_adv_stop` (link stopped, its two interrupts disabled in the baseband, §11.1);
    nothing is on the air and the interrupts stay quiet until ON. **OFF while connected**: `ble_ll_disconnect`
    sends `LL_TERMINATE_IND` (reason 0x13, remote user terminated); the link closes when the central acknowledges it
    (or after the supervision timeout), and because the layer is no longer enabled it does not advertise after it.
    The central sees a normal terminate. **ON**: `ble_enable(1)` starts advertising again (while a terminate is still in
    flight, the advertising starts when the link closes).
  - **No stuck notes**: notes a central had on (`ble_held`, a bit per channel and note, 256 B of RAM; filled in the
    TIMER5 poll as the notes go to the router) are ended with a note-off through the same ring when the link goes
    (`ble_app_state` sees the connection end, from any cause: terminate, timeout, the central vanishing) and when
    BLUETOOTH goes OFF. Nothing else is touched: notes from USB or TRS keep sounding. Notes the central sends after OFF
    (before the terminate is acknowledged) are dropped (`ble_on` in `ble_app_midi_in`). The BLE out ring is emptied
    whenever no central listens (`ble_app_state`). The in ring is drained into the router first, so a note-on
    already queued is ended too. There is no USB-disconnect precedent to match: USB's notes are not ended on a
    disconnect.
  - Menu row: ON / OFF in large type, "VISIBLE" (advertising) or "CONNECTED" under it.

### 11.6 Sizes [M]

| Build | Flash | RAM | RAMTEXT |
| --- | --- | --- | --- |
| user-default, BLE off | 578,972 | 80,728 | 30,744 |
| user-default, BLE on (measurement link) | 592,116 (+13,144; 10,552 over the slot) | 86,232 (+5,504) | 30,672 (−72) |
| user-default, BLE on, FLUTE set out (the emulator test's) | 560,612 | 86,232 | 30,672 |
| the same, with HOME > BLUETOOTH (`tools/optimist.py build --set BLE=1 --ble-drop FLUTE`, the builder's exact sizes: 502,000 -> 502,364 flash, 84,696 -> 84,968 RAM) | +364 | +272 (256 of them the held-note table) | 0 |
| user-default, BLE off | unchanged: nothing of it is compiled | | |

The driver alone (`ble_hw_wl82.c` with `hal/fm1_ble.h`, JieLi clang `-Os -ffunction-sections`, its own object):
.text 3,530 B, .rodata 21 B, .bss 2,584 B (the block 1,508, the PLL table 972, state 68, counters 36), plus the two
IRQ entries (7 instructions each). In the unity build the stack's RX path inlines into `hw_rx_service`, so per-symbol
sizes there are not per file. The rest of the +13.1 KB is the stack the stand-in let the compiler drop (§8).

### 11.7 End to end in the emulator [M: the emulator's model, not hardware]

`tests/ble_emu_test.py` (run by `tests/run_tests.sh`; `tools/optimist.py test` builds the package
`build/ble/felucca-ble.fwsc`: user-default + BLE, FLUTE out). It runs `diagnose` with the virtual central
(`FM1_BLE_CENTRAL=script`, `FM1_CPU_MHZ=96`) and passes: ADV_IND `02 01 06` + the BLE-MIDI UUID from a random static
address; SCAN_RSP `09 09 "FM-1_BLE"`; connect; version 9 / 0xFFFF; features 0x2E; MTU 247; discovery (GAP 1–7, GATT
8–11 with Service Changed, BLE-MIDI 12–15); the MIDI read; the CCCD; our L2CAP request {6–9, 0, 100} and the
central's update to 7.5 ms at its instant; a note written by the central plays the synth (about 13,000 non-silent
frames of the last second against 0 without it); a key held at 1.5 s arrives as notifications `8b e2 90 1d 64` and
`8c cb 80 1d 00` (timestamp 1,506 ms: real); a channel map and an interval update at their instants; terminate both
ways; advertising again. The same with `FM1_BLE_LOSS=3` (53 of our PDUs lost: retransmissions both ways), with the
engine storing repeats (`old_sn=store` + loss: the driver drops all 53), and the supervision timeout when the
central vanishes (advertising again 1,005 ms later, timeout 1 s). By hand also: `old_sn=irq`, `pairs=4`,
`engine_retransmits=0` (+ loss), `instant_gating=0`, `event_irq_every_event=0`, `stop_adv=0`,
`rx_needs_armed=1`, `adv_irq_on_connect=0`, `scan_req_irq=1`, `FM1_BLE_WINDOW_DELAY_US=2000`, `FM1_NESTED_IRQ=1`,
192 MHz: all steps pass; `store_empty=0` fails (§11.2).

HOME > BLUETOOTH (`menu_checks` in the same file): the panel is driven through the engine's matrix contacts
(`FM1_PRESS`: HOME held 0.86 s, SELECT as a quadrature encoder, eight detents right to the last screen, OCT+, HOME held
to close). Checked: OFF while a central is connected and holds a note: `LL_TERMINATE_IND` reason 0x13 within 0.3 s
of OCT+, the link stops (column 14 = 0), no packet of any kind on the air until ON, then ADV_IND again; the last
second of audio is silent (the same run without OFF rings: about 42,000 of 44,100 non-silent frames; with the release call removed it rings too: 42,529); OFF saved in the emulator's flash (`FM1_FLASH_DUMP`) and a
second boot with it (`FM1_FLASH_RESTORE`) never advertises; the menu's ON from that boot advertises and a central
connects (all steps pass, its CONNECT_IND after OCT+); ON saved, a third boot advertises from the start. Host tests:
`tests/menu_ui.c` (the row, the knob, OCT+, the radio told once) and `tests/midi_seq_test.c` (bit 21, older words
read ON), both built with `FELUCCA_BLE=1` in `tests/run_tests.sh`.

Interrupt handlers (`FM1_BLE_ISR=1`, nesting off as the emulator's default, 96 MHz; 192 MHz in brackets):

| | runs | mean | max |
| --- | --- | --- | --- |
| IRQ 29 (RX) | 175 | 2.3 µs | 34.2 µs, 3,281 instructions (17.1 µs) |
| IRQ 45 (event, incl. advertising) | 190 | 2.4 µs | 35.6 µs, 3,419 instructions (17.8 µs) |
| IRQ 11 (audio), same key press, BLE off → on | 525 | 439.1 → 446.0 µs | 774.9 → 779.3 µs |

- A BLE interrupt waited at most 773 µs to start (334 µs at 192 MHz), almost all of it behind an audio half; the
  CONNECT_IND's state 7 still went in 16 µs (8 µs) after the packet, 1.23 ms before the window.
- The worst delay BLE can add to the audio, with the emulator's no-nesting rule, is one BLE handler: ≤ 35.6 µs at
  96 MHz (17.8 µs at 192 MHz) [I: the bound from the longest handler; the profiler measures the BLE entry delays, not
  the audio's]. On hardware the audio's higher priority should preempt BLE instead (`FM1_NESTED_IRQ=1` ran clean,
  but the profiler's durations are not meaningful with nesting on).
- The audio handler itself is 1.6 % longer on average (+7 µs, also with nothing sounding and no connection) in the
  BLE build. BLE code does not run in it (no nesting; `ble_midi_out` only on an event); the render functions differ
  in size between the two builds (`mix_block` 9,416 vs 9,520 B, different global layout) [I: code generation, not BLE
  work].

### 11.8 What needs a real FM-1

Everything radio: the start-up of §11.4 (and whether the stored-trim shortcut works, U1–U3), the timing words
(U5), TX power and RSSI (U9, U10), RFPRIO (U11), drift (U13, U14). And every engine behaviour the model only assumes:
the instance table being read, the RX length of advertising PDUs (taken from RXDHDRn [15:8], 34 for a CONNECT_IND
when 0), TXBUFnCNTL / RXBUFnCNTL directions, column 6 bits 6 / 7 for other channel masks (only all three channels are
programmed), whether column 4 and the WINCNTL0/1 window are what the engine uses after the first anchor, the empty
PDUs' LLID in TX buffer 1 (stock's `^ 5` gives LLID 0 there), 251-octet PDUs, the ISR durations (U8). The first
hardware step is a sniffer on channel 37–39: an ADV_IND from our address means the radio and the baseband start.

## 12. The radio's start-up: captured tables, stored trims (`hal/fm1_ble_rf.h`, `ble/ble_vm.c`)

From `docs/BLE-HW-FACTS.md` §14–§19 (feat/ble-facts d907ce3; "HW §n"). Clean room as §1: no vendor IR, disassembly or
SDK library. What was read besides the sheet: the boot-2 MMIO trace the capture tool makes (§12.3), i.e. stock V15's
register writes and reads in the emulator, for four details the sheet leaves open: the VCO step's register order
(§12.1), a window read being address, commit 2, then the read-back register, bit 19 being set in every BBP command,
and the kick's values. Measured register behaviour, not code. Unverified on hardware in every part.

### 12.1 What runs, in stock's second-boot order (HW §16.1)

`fm1_ble_rf_init(106, 107, 108, 187)` runs `fm1_ble_rf_run` (the program in `build/gen/ble_rf_tables.h`), then HW §5.1
steps 3–4 and §5.4 (§11.4). The program, as captured: 522 register writes (`0x11900`–`0x11964`, `0x14040`–`0x1405C`,
`0x30F00`/`04`, the MAC window; bit 14 of `0x11900` kept as found, HW §16.2), 1,179 window writes and 18 window reads
(HW §16.3; the reload of group 10 is a replay of group 4's block), 26 direct BBP registers, 512 RF-die LUT
words (SPI `0x14028` / `0x1402C`, the five-high five-low kick, HW §5.2), 101 trim marks, 4 delays (stock's gaps of
20 µs or more), one VCO scan and one left-out block. The clock words of group 1 are not replayed (`0x10010` bit 10 is
our UART's clock, `0x10008` bit 3 the second core's start).

- **Trims, byte by byte** (HW §14.5, §16.4): `0x11930` [16:13] / [22:19] ← 106; `0x11924` [26:24], `0x11928` [2:0] /
  [11:9] / [17:15] / [25:22], `0x1192C` [2:0] ← 107; window-D entries `0x0B`…`0x21` ← 108 (a non-zero byte replaces
  the default); window-D `0x61` / `0x62` 2-bit fields ← 187 bytes 0, 4, 8, 12, 16, 20 (read-modify-write on the
  read-back, as stock), `0x04`–`0x09` [7:6] ← 1, 5, …, 21, window-D' `0x09`/`0x0B`/`0x0A`/`0x0C` ← 26–29, `0x1191C`
  [9:8] ← 24, `0x11920` [1:0] ← 25 and [11:10] ← 42, `0x11910` [15:14] ← 42, `0x11908` [8:7] ← 32, bit 15 ← 33,
  [18:17] ← 36, bit 25 ← 37, `0x1195C` / `0x11960` bytes ← 40–47. Which writes carry a field is found by the
  capture (a second boot with every trim byte changed), not guessed; the widths of the 1- and 2-bit fields are [I]
  (the sheet's xor 03 probe).
- **VCO scan** (group 7): ours. With the fine code stock starts from, a bisection over bands 0–63 in `0x11938`
  [25:19]: the band, then `0x11938` bits 16 / 18 and `0x11934` bit 24 pulsed low and high, `0x11968` bit 28, eight
  strobes of `0x11978` and 0, the comparator in `0x11978` (bit 17 = low → a higher band, bit 18 = high → a lower
  one; the meaning [I]). The result is kept for the console; stock's final band and fine code follow as captured.
- **Left out** (TODO(hardware)): the window-D read-back loop (3,537 transactions, HW §16.2).

### 12.2 The stored trims (HW §14, §15.4)

- At every boot (`midi_ble.c ble_midi_init`) `ble_vm_scan` reads the VM in place through SPI reads (`st_read`):
  the live area of `0x093000` / `0x095000` (both marked: A), the records to the first failing check, the last valid
  106 / 107 / 108 / 187 with their lengths, 187's inner CRC. It never writes or erases there (the reader has no write
  path; the emulator's dumps after our boots have the VM sectors unchanged).
- **The copy**: 100 bytes (`ble_rf_copy_*`: a mark with the area it came from, the four records' data, CRC-16/XMODEM)
  appended to the settings record (`persist_t.ble_rf`, after `ble_addr`, as the address was). Written with the
  settings, once quiet, when the VM has a complete set the copy does not hold yet. A build without BLE that saves its
  settings drops it (as it drops the address); the VM itself is untouched by every Optimist build (HW §15.2).
- **Precedence**: the VM's complete set → the copy → none. **None**: the radio is never started (no RF, BT or
  baseband write), nothing advertises, HOME > SYSTEM > BLUETOOTH shows **NO RF CAL** under ON / OFF (the switch only
  changes the setting), and the console says why.
- **Console** (CDC builds only: `USB_MODE` 1; user-default has USB audio and no console): `blevm` prints both areas'
  first words, every valid record (offset, id, length), the live area, the log end, which records are there, 187's
  CRC and their data; `bletrim` the source in use, the VM's summary, the copy (and its data), the tables' SHA-256 and
  what the start-up did (ops, trims, LUT words, delays, the skipped count, BBP / SPI timeouts, the scan's band, steps
  and last comparator word). `flr 0x93000 256` still reads the raw bytes. Both read only.

### 12.3 The capture (`tools/ble_rf_capture.py`, HW §17)

Input: the user's `FM-1.fwsc` (`--stock`, `FM1_STOCK_FWSC`, or `firmwares/FM-1.fwsc`), refused unless its SHA-256 is
stock V15's; the emulator's `diagnose` (`--diagnose`, `FM1_BLE_DIAGNOSE`, or `FM1_EMU`, as `tests/ble_emu_test.py`).
It runs a first boot (1e9 instructions, the flash dumped), a second boot over it traced in the RF ranges, and a second
boot with every byte of 106 / 107 / 108 / 187 xor 03 (187's inner CRC and the check bytes fixed). The trace is cut by
register patterns only: up to the first write to `0x14000`; BBP port pairs folded into window transactions; LUT words
by their kick; the scan from its first step to its last strobe; the read-back loop as the longest window-D run with
≥ 64 reads; the AGC table as the 128 words after `0x2FD98` = 0. A write the perturbed boot changes is mapped to its
field; a change no field of §12.1 explains stops the tool. Self-checks: the program, expanded with each run's VM,
gives that run's writes exactly (outside the scan and the loop); the result's SHA-256 must be the pinned one
(`1d585b01…`, two runs identical); else nothing is written. About 30 s.

Output `build/gen/ble_rf_tables.h` (git-ignored with `build/`), and `build/gen/ble_rf_capture/` (the emulator's own VM
and the expected writes, for §12.4). **The build** (`tools/build.py`, `FELUCCA_BLE=1`): a header of the pinned
capture is used as it is; missing or another one, the capture runs when the stock firmware and the emulator are
found, otherwise the build stops with the command to run. Chosen over a silent fallback: the sheet's rule (no BLE
build without the tables) and the repository's way with inputs it may not carry (`make sdk` fetches the SDK files;
here nothing can be fetched, so the user's copy is used). The repository carries no vendor-derived table: the tool,
the field list of the sheet and a hash. A compiled BLE firmware does contain them (HW §17.2).

### 12.4 In the emulator [M: emulator model]

`tests/ble_emu_test.py` boots every run over a flash with a VM (the capture's, or one built from §14) and passes all
its earlier checks unchanged, and: **rf_init = stock V15's second boot, write for write**, 6,961 writes before our
scan and 13,399 after (the same VM laid in, the read-backs the model's 0); between them only the scan's registers
(5 steps, band 61, in range); the 128 AGC words; with no VM and no copy, no write to the RF / BT / baseband ranges
and nothing on the air; the copy found in the settings sector after the first save (after 5 s) and, with the VM then
erased, the radio starting from the copy and advertising. Host: `tests/ble_vm_test.c` (every rule of §14 on built
images, the V15 and demo_ble extracts of `docs/ble-traces/v15-vm-trim-map.txt`, the copy and the precedence, a scan
leaving the image unchanged) and `tests/ble_rf_capture_test.py` (the cut on synthetic traces).

### 12.5 Sizes [M]

| user-default, BLE on (fits: nothing dropped) | Flash | RAM | RAMTEXT |
| --- | --- | --- | --- |
| before (7b8770e) | 502,364 | 84,968 | 30,836 |
| now | 515,768 (+13,404) | 85,224 (+256) | 30,880 (+44) |
| of which the generated tables | 11,578 (program 10,686, addresses 172, AGC 512, fields 208) | | |
| user-default, BLE off | `felucca.bin` byte-identical to 7b8770e's | | |

The RAM: the copy (100 B) and the settings record's two copies growing by it, the scan's statistics. The slot is
581,564 B: 65,796 B left with BLE on.

### 12.6 What still needs a real FM-1 (with HW §16.5 / §18.3)

1. **The VM's real contents** (U2): 106 / 107 / 108 / 187 of two units, which area is live, 187's CRC (`blevm`, or
   the UBOOT backup decoded with `tools/ble_vm.py`, HW §18.1). And whether stock's updater left the VM at install
   (U16: `blevm` after the first install).
2. **The window read-back** (U17): does `0xD3` (and `0xCB`, `0xD7`) return the entry addressed? It decides the
   187 read-modify-writes and the left-out loop. Then the loop itself (3,537 transactions: entries `0x51`, `0x58`,
   `0x5C`, `0x5D`, `0x61`, `0x62`): what it measures and writes (HW §16.2, §18.3 step 3).
3. **The RF-die LUT words a trim or the scan changes**: 160 words (107 → entries `0xE0`–`0xFF` of both words, 64;
   187 bytes 48–63, 96) and the scan's word-1 entries `0x00`–`0x7F`. Written as captured (the emulator's
   calibration); their mapping is unknown. Read the LUT back (SPI command `0x6`) after stock's boot on a unit.
4. **The VCO**: the band stock settles on, whether its final `0x11934`–`0x11940` writes depend on the scan (kept as
   captured), whether bits 17 / 18 mean low / high, and the step's settle times (2 µs, 50 µs here [I]).
5. **The BT TX trims**: `0x2FC08` [9:0] / [19:10] and `0x2FC10` bytes 0–2, live from BBP read-backs on every boot
   (HW §16.2); stock's first-written values are kept.
6. **The field widths** marked [I] in §12.1 (187 bytes 32, 33, 36, 37; the window-D' bytes taken whole).
7. **Timing**: the four captured delays are stock's gaps in the emulator; `FM1_BLE_STEP_DELAY_US` (100 µs) is a
   guess; the BBP start bit polled to 0 as "done" is [I], as is bit 19 of every BBP command (constant, meaning
   unknown).
8. **The carrier** (U1, U14): stored trims + this sequence → a clean carrier on channel 19 (HW §18.3 step 4), then
   advertising (step 5).

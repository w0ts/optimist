# BLE MIDI, route C: our own stack (link layer, host, BLE-MIDI)

Status: **EXPERIMENTAL. Host-tested, end to end in the emulator against its model of the BLE engine (§11), and
tested on an FM-1: a Mac (macOS Audio MIDI Setup) connects and MIDI goes both ways (not yet tried with iOS or
Windows).** Parts of the radio's start-up stay TODO(hardware) (§12.6). Build flag `FELUCCA_BLE` (default 0 = off; the
image with it off is byte-identical to one without this code, but for the builder's configuration hash). A BLE build
needs the user's own stock V15 once, to capture the radio's tables (§12.3). Builder items `BLE` (Experimental group)
and its options `BLE_DIAG` (the `blell` diagnostics, off by default, §12.7), `BLE_BOND` (bonding, §5.1) and `BLE_CENTRAL`
(the DEVICES list, connecting out to BLE-MIDI devices and reconnecting the last one, §13).

Note: a firmware built with BLE contains radio tables captured from M-VAVE's V15 (information only). Background: `BLE-MIDI-FEASIBILITY.md` §9 (route C, the stock firmware's behaviour in §9.2).

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
| `ble_ll_central.c`, `ble_hw_wl82_central.c` | the central role's link layer (scanner, initiator, the master's procedures) and its driver half (states 1 / 3 / 6, HW §21), each included by its parent (§13) |
| `ble_gattc.c`, `ble_central.c`, `ble_smp_init.c` | the ATT client (BLE-MIDI discovery, CCCD), the host side of a connection out (security, failures, RPA), SMP as initiator (§13) |
| `ble_store.c`, `ble_scan.c` | the one remembered device (LAST) and the nearby devices' table (docs/BLE-DEVICES-DESIGN.md) |
| `firmware/src/io/midi/ble_devices.c`, `ble_connect.c` | DEVICES (NONE / LAST / nearby), a pick, LAST and its search (§13.4) |

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
  security (unless `BLE_MIDI_NEED_ENC`, §5.1).
- Service Changed: the characteristic is there (indicate, its CCCD readable and writable) but **never indicated**:
  the database does not change while we run. Until 2026-10-09 (`BLE_SC_ON_SUBSCRIBE=1`, now 0) it was indicated for
  0x0001..0xFFFF whenever a client turned its indications on; on the FM-1 the Mac confirmed that indication and then
  never discovered the MIDI characteristic (§11.10). The handles are unchanged (1..15), so a Mac that cached them
  from an earlier build still finds the same layout. Should the layout ever change, add the Database Hash (0x2B2A,
  Core 5.1 Vol 3 Part G 7.3), which macOS reads at every connection [I: §11.10], rather than indicating blindly.
- When a client turns the MIDI notifications on and the interval is outside 6..9, an L2CAP Connection
  Parameter Update Request {6, 9, 0, 100} goes out; after a reject, once more {12, 12, 0, 100} (stock's
  values and order, §9.2).
- Signalling: Command Reject (not understood) to every request but the response we wait for. SMP: §5.1.

### 5.1 Security: SMP and LL encryption (`ble_smp.c`, `ble_aes.c`; off by default)

- **Default** (`BLE_SMP_LEGACY=0`): a Pairing Request gets Pairing Failed, Pairing Not Supported; LL_ENC_REQ is
  refused (0x1A); no attribute needs security. This is what the FM-1 ran in blell10-12; the Mac never sent a Pairing
  Request or an LL_ENC_REQ there (`tx_queued` = our responses exactly, no `03` in `ctl_rx_last`).
- **`BLE_SMP_LEGACY=1`** (needs `BLE_LL_ENC=1`): LE legacy pairing, Just Works (Core Vol 3 Part H 2.3.5.2: our IO
  capability NoInputNoOutput, TK = 0, no Secure Connections bit, so a Mac asking for SC and MITM still ends in legacy
  Just Works). Pairing Response (bonding if the central bonds; we take its LTK / IRK / CSRK offers and hand out our
  LTK), Sconfirm = c1(TK, Srand, preq, pres, iat, ia, rat, ra), Mrand checked against Mconfirm (else Pairing Failed
  0x04), STK = s1(TK, Srand, Mrand) cut to the key size. The central starts the LL encryption with the STK (EDIV 0,
  Rand 0: `ble_host_ltk` asks `ble_smp_stk` first); once it runs (`ble_host_encrypted`), our Encryption Information
  (LTK) and Master Identification (EDIV, Rand), all three fresh from the radio's RNG, then the central's keys (read
  and dropped: a returning central is found by EDIV / Rand, not by its resolvable address). The bond is one key slot
  (`ble_host_set_key`) and goes to the firmware (`ble_app_bond`): `midi_ble.c` keeps it in the settings record
  (`persist_t.ble_bond`, 28 B after `ble_rf`; never in the VM area 0xE7000-0xE9FFF), saved once the FM-1 is quiet,
  restored after `ble_init`. A returning Mac encrypts with it straight away. Not done: the 30 s SMP timeout,
  Secure Connections, passkey, signing, LL_PAUSE_ENC (a re-pairing on an already encrypted link gets
  LL_UNKNOWN_RSP).
- **`BLE_SMP_SEC_REQ=1`**: an SMP Security Request (bonding) at every connection start, as stock V15 does (the
  emulator's README: stock "sends an SMP Security Request, and if the central answers Pairing Failed it terminates the
  link"). Apple's Accessory Design Guidelines (58.10) advise the opposite: "The accessory should not request pairing
  until an ATT request is rejected using the Insufficient Authentication error code".
- **`BLE_MIDI_NEED_ENC=1`**: the MIDI I/O value and its CCCD need an encrypted link; until then reads and writes get
  Insufficient Authentication (0x05), the way 58.10 asks a peripheral to start pairing (discovery stays open, 58.9).
- **Builds**: `OPTIMIST_BLE_SMP=1` (pairing available), `=2` (+ Security Request), `=3` (+ MIDI needs encryption) in
  the environment of `tools/optimist.py build --set BLE=1` (tools/build.py; not `FELUCCA_*`, which the builder strips).
  Each adds about 2.7 KB of flash and 0.4 KB of RAM (user-default + BLE + USB_MODE, 2026-10-09: 532,572 -> 535,272 B
  flash, 88,744 -> 89,176 B RAM).
- **Cost on the interrupts**: the LL encrypts a PDU when the TX service loads it (the end of the RX interrupt) and
  decrypts in the RX interrupt, in software (`ble_aes128`, no tables but the S-box). CCM takes 3 + 2 x ceil(n / 16)
  AES blocks for n payload octets: 7 for a MIDI notification or an ATT response up to 32 octets, 35 for a full
  251-octet PDU; empty PDUs are not encrypted. On the host (tests/ble_prim_test.c, -Os) one block takes 169 ns
  [M: host]; on the FM-1 it is unmeasured: at an assumed 2,000-4,000 cycles a block at 240 MHz, 60-120 us for a MIDI
  notification and 0.3-0.6 ms for a 251-octet PDU [I], against a connection interval of 7.5 ms or more, and the BLE
  interrupts sit below the audio's (§11.3). `blell` prints `isr_max_us` (the longest BLE interrupt): read it with
  `enc_on` 1 to measure it. The chip's AES block (`BLE_AES_HW`) stays optional.

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
  CSA #1; the access-address rules; LE legacy pairing's c1 and s1 (the Core spec's samples, Vol 3 Part H 2.2.3 /
  2.2.4, also checked with Python's `cryptography` AES); the CCM block count per PDU and this host's time per block.
- `ble_stack_test.c`: a simulated central through a fake `ble_hw`: advertise → CONNECT_IND (and the invalid
  ones) → version / feature / length / PHY / ping / parameter request → MTU → discovery as CoreBluetooth
  runs it → the Service Changed CCCD on, no indication → CCCD → connection parameter request → MIDI notify and
  write → connection update and channel map with instants → terminate (both ways), supervision timeout, failed
  establishment, instant passed, 40 s procedure timeout → advertising again; blell's protocol ring. Built three
  times: `BLE_LL_ENC=0` (encryption refused), `BLE_LL_ENC=1` (the Core spec's encryption sample end to end: SK, the
  central's encrypted START_ENC_RSP, our data packet byte for byte, a MIC failure ending the link) and
  `BLE_SMP_LEGACY=1` (a Mac-like Pairing Request, Sconfirm checked with c1, the STK's encryption, our LTK / EDIV /
  Rand and the bond handed over, the central's keys, a reconnection encrypted with the bond, an unknown Rand
  refused with 0x06, a confirm that does not match: 0x04; a short PDU 0x0A, Secure Connections' public key 0x07).
- `ble_driver_test.c`: the WL82 driver (`ble_hw_wl82.c`) with the whole stack against a fake engine built from the
  FM-1's measurements (§11.9; `tests/ble_fake/fm1_ble.h` stands in for `hal/fm1_ble.h`), and a Mac-like central that
  runs macOS's GATT sequence of blell10-12 and then CoreMIDI's (§11.10): it stalls, as the FM-1 did, if a Service
  Changed indication comes (with `-DBLE_SC_ON_SUBSCRIBE=1` the test fails exactly there), else it reads the MIDI
  characteristic (empty), subscribes, answers our parameter request and plays MIDI both ways. Built again with
  `BLE_MIDI_NEED_ENC=1`: the Mac pairs on Insufficient Authentication and the link runs encrypted through the driver
  (with packet loss too).
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
  repeat is dropped, never delivered twice), re-arms the buffer, then runs the TX service (HW §8.2, §11.9's rule):
  TXBUFnCNTL bit 0 = 1 is an empty buffer, 0 one handed to the engine; a buffer we loaded reading 1 again is
  acknowledged, and an empty buffer takes the next PDU, bit 0 cleared as the last write; the TXTOG buffer first, then
  the other, so two PDUs can be in flight in order. The event interrupt never touches TX. IRQ 45 first takes a
  pending reception of the same event, then reads the counter (column 3 − 1, op 2), applies the instants, narrows
  the receive window to WINCNTL2's 50 µs (and clears column 4) after the first packet of a new anchor, and calls
  `ble_ll_hw_event_end` with rx_ok = an RX interrupt in this event or EVTCOUNT = the counter (a repeat the engine
  dropped still counts as a reception). Supervision is the link layer's.
- **Instants** (HW §9): column 5 = instant when the update arrives (RFPRIO 30); in event instant − 1's IRQ 45 the
  new window (WinSize × 1.25 ms + 625 µs), latency, widening, column 4 and columns 1 / 15, or the channel tables. An
  update the driver sees late is applied at once and counted (`ble_hw_stat.late_instant`).
- **Time base**: TIMER4 (24 MHz, a plain load) extended to 32-bit microseconds (`ble_hw_diag_now`), monotonic. The
  engine's 24-bit slot clock (columns 0 / 14) is no longer read by the driver: on the FM-1 it stepped backwards
  (§11.9). The engine keeps its anchors on its own clock.
- **Random numbers**: `0x13B00` / `0x13B04` (HW §1).

### 11.2 The open questions of §10, as the driver takes them

1. Repeats: the engine drops them (model default) and the driver checks SN as well; EVTCOUNT still counts a repeat
   as a reception. Tested with `old_sn=store` (the engine stores repeats: the driver drops them) and `old_sn=irq`.
2. IRQ 45 after every connection event, received or not (model default, from stock). `event_irq_every_event=0`
   also passes the end-to-end run.
3. TXBUFnCNTL bit 0 back to 1 = acknowledged (model); the FM-1 clears it instead (§11.9), and the driver learns which. The engine resends a NAKed PDU (model default);
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
11. Time base: TIMER4 (above); the slot clock steps backwards on the FM-1 (§11.9).

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

- **Order** (`system/main.c`; the settings are read first, `persist_boot`, so the saved BLUETOOTH is known): `audio_init`,
  `usb_start`, `uart_midi_init`, then `ble_midi_init`: the VM scan (flash reads only) and the trims chosen (§12.2), the
  two BLE vectors attached and masked at the interrupt controller. **Only when ON was saved** does it go on: the stack,
  the address, `rf_init` and the baseband (`ble_radio_start`), advertising, the two interrupts let go. With OFF (the
  default) nothing of the radio runs at boot: no `rf_init`, no write to the RF, BT or baseband registers, so a radio
  start-up that hangs on a real FM-1 cannot stop it booting.
- **The safety net for ON saved** (`ble_boot_radio`, `ble/ble_vm.c`): a boot the boot guard counts as following a
  failed start-up leaves the radio off too, for that one boot, with ON kept saved; the menu can then switch it OFF.
  The count (`system/bootguard.h`, `main.c`) is of warm resets within a boot's first 30 s: a watchdog reset after a
  radio start-up that hung is one [I: that a hang on hardware ends in the watchdog's reset is unmeasured]. A
  power-off / on clears the count, so after one the radio starts again; two failed start-ups in a row go to USB rescue,
  as for any crash. `bletrim` shows `boot_failed`, and the row shows ON with nothing under it. `tests/ble_vm_test.c`
  runs the decision against the real boot guard (a simulated count: power-on, a watchdog reset, 30 s, a power-on).
- **Address**: VM 104 (public) when the driver can read it (not yet, §11.2.8); else a random static address made
  once from the random source and kept with the settings (`persist_t.ble_addr`, appended last: a build without BLE
  reads the rest as its own, and when it saves its settings the address is gone, so the next BLE build makes a new
  one; a record from a build without BLE reads as "no address yet"). It is saved like a setting changed while playing: once the FM-1 is quiet. Emulator: the first boot
  saves it (sector 0xFC000), the second boot reads it and writes nothing.
- **On / off at run time**: **HOME held > MENU > SYSTEM (last screen) > BLUETOOTH**, ON / OFF, only in a build with
  `FELUCCA_BLE=1` (the builder item BLE decides what is in the image; the switch is the HOME menu's, as for every
  run-time feature). **OFF is the default** (the user's ruling of 2026-10-08): a fresh unit, a settings record from
  before the bit and one SLOOP 2.3 / 2.4 wrote all read OFF. The choice is a setting of the FM-1, kept in the settings
  word (`storage/settings_word.c`, **bit 21, 1 = ON**) and saved when the menu closes (`menu_close`), so ON or OFF
  survives a restart. No settings-record change and no version bump: the word is part of the record in every build; a
  build without BLE keeps the bit as read (`bp23_kept`) and writes it back, so ON survives a round trip through such a
  build. (Before this ruling the bit was inverted, 1 = OFF, ON by default; no released firmware wrote it, so a word an
  earlier experimental BLE build saved with OFF now reads ON once.) With `FELUCCA_BLE=0` none of it is compiled: no
  menu row, no state, no new code.
  - **The radio starts the first time it is ON**: at boot when ON was saved, else when the menu switches it ON
    (`ble_midi_set` → `ble_radio_start`: the address, the stack, `rf_init`, the baseband, in the main loop with the two
    BLE interrupts masked; once per boot). Without stored trims it never starts (§12.2): the switch only changes the
    setting and the row shows NO RF CAL.
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

OFF by default (`off_checks`, first): a fresh unit (the VM there, no settings) boots with no write to the RF, BT or
baseband ranges and nothing on the air; the menu's ON from that boot runs `rf_init` then (its first RF write after
OCT+), advertises and takes a connection; the flash dumped with ON saved is what every later run boots over.

HOME > BLUETOOTH (`menu_checks` in the same file): the panel is driven through the engine's matrix contacts
(`FM1_PRESS`: HOME held 0.86 s, SELECT as a quadrature encoder, eight detents right to the last screen, OCT+, HOME held
to close). Checked: OFF while a central is connected and holds a note: `LL_TERMINATE_IND` reason 0x13 within 0.3 s
of OCT+, the link stops (column 14 = 0), no packet of any kind on the air until ON, then ADV_IND again; the last
second of audio is silent (the same run without OFF rings: about 42,000 of 44,100 non-silent frames; with the release call removed it rings too: 42,529); OFF saved in the emulator's flash (`FM1_FLASH_DUMP`) and a
second boot with it (`FM1_FLASH_RESTORE`) never advertises; the menu's ON from that boot advertises and a central
connects (all steps pass, its CONNECT_IND after OCT+); ON saved, a third boot advertises from the start. Host tests:
`tests/menu_ui.c` (OFF by default, the row, the knob, OCT+, the radio told once) and `tests/midi_seq_test.c` (bit 21
= ON; a fresh, an older or a SLOOP word reads OFF; ON written and read back), both built with `FELUCCA_BLE=1` in `tests/run_tests.sh`.

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

### 11.9 What the FM-1 measured: the first connection (blell3, 9a90c7d, 2026-10-08), the first TX (blell4, 5008663, 2026-10-09), buffer 0 never sent (blell6, 0475aa5), the FEATURE_RSP acknowledged unseen (blell8, cbb94d1) [M:hw]

**Superseded for TX by the rule at the end of this section** (HW §8.2, `ble-tx0` after 51792b7). The TX readings below
(bit0 = 1 "loaded", the polarity learnt per connection, stale / force-free, the move rule, acknowledgement by the
central's NESN) are **retired**: they inverted the vendor's polarity. They stay as the record of what was measured;
the measurements themselves (registers read on the FM-1) still hold, only their reading changed. RX is unchanged.

The Mac connected 19 times (`cind_ok` 19: interval 24 = 30 ms, WinSize 3, WinOffset 22, timeout 72 = 720 ms, 37
channels, hop 13, SCA 1). The first data packet came 26–29 ms after the CONNECT_IND; `rx_good` 470, `rx_empty` 451,
`rx_crc_bad` 0, `rx_desync` 0. Every connection then closed after 0.2–2.1 s with 0x22 (LL response timeout,
`close_by` 4), and nothing was ever sent (`ctl_tx` 0, `tx_queued` 0). Three findings:

- **RX while advertising**: RXBUFnCNTL stays 00 (never set by the engine); every packet (54 of 54) was in the
  buffer **RXTOG has moved past**, payload at RXPTR, header in RXAHDR / RXDHDR (`rxf_tog_prev` 50 + 4 found by the
  event ISR, `rxl_cb` 54). `hw_adv_find` now tries that rule first; RXBUFnCNTL on `rx_next` / on the other buffer and
  the content of RXTOG's own buffer stay as counted fallbacks.
- **RX in a connection**: RXBUFnCNTL bit 0 = 1 **does** mark the filled buffer there (all 470 packets came through
  that rule, `rx_desync` 0), so the connection path keeps it; `rxc_tog_past` / `rxc_tog_at` now count whether RXTOG
  had moved past that buffer too. RXSTAT: [3:0] = 1 a good packet (all 470; the advertising buffer that held the
  packet read `0x9401` / `0x9801` / `0x9C01`), `0x9805` (bit 2 set) a dropped SCAN_REQ (`adv_drop_stat`), bits 15 and
  12 always set; bits 11:10 cycle 1 → 2 → 3 between the two buffers' last fills while advertising [I: the advertising
  channel 37 / 38 / 39 of the fill].
- **TX**: `tx_none` 38 = 2 per connection: the refill asked the link layer twice (nothing queued yet), then never
  again in 486 events, though a VERSION_IND was waiting to be answered and our feature exchange queued (`ll_lproc`
  1). conn_start sets both TXBUFnCNTL bit 0 to 1 (HW §7 step 5, empty PDUs); the refill required bit 0 = 1 (the
  sheet's "empty") and found 0: **the engine clears bit 0** when it is done with a buffer (we loaded nothing). So
  TXBUFnCNTL bit 0 is the same full flag as RXBUFnCNTL in a connection: 1 = loaded, the engine's to send. The driver
  learns the direction per connection (`tx_pol` 1: the engine clears it, as the FM-1; 2: it stays 1 for three events
  with packets heard, the sheet and the emulator's model) and loads nothing before it knows; `txsnap` records each
  decision (TXTOG, TXBUF0/1CNTL, TXDHDR0/1, INTFRAME).
- **The clock**: `clk_step_max` 16777215 and `close_since_start_us` 1,895,854,158 for a connection that lasted
  195.7 ms (TIMER4: `cind_ok` 83.343 s, `close` 83.539 s). 2^24 × 625 mod 2^32 = 1,895,825,408, so the link layer's
  clock had jumped by (2^24 − k) slots: the slot clock read through columns 0 / 14 stepped **back** by k ≈ 267 slots
  (195.7 ms − 28.75 ms = 625 µs × 267) and the modular step read it as ~10,486 s forward. With rx every event the
  supervision timer (720 ms) is refreshed in the same call (`close_since_rx_us` 0), but the 40 s procedure timer of
  the pending feature exchange (started at event 6) fired: 0x22, `close_by` 4. `ble_hw_time_us` now runs on TIMER4,
  which also takes three column reads (op 2) per call off the interrupts.

**blell4** (5008663: TIMER4 time base, the TX direction learnt; one Mac connection, `hw-logs/blell4.txt`): the
CONNECT_IND taken, first RX at 26 ms, `rx_good` 232, `clk_step_max` 30,486 µs (the TIMER4 fix holds). `tx_pol` 1 at
the first packet: TXTOG `0007`, TXBUF0CNTL `01`, TXBUF1CNTL `00` (the engine had sent conn_start's empty PDU from
buffer 1 and cleared its bit). Our VERSION_IND was loaded in buffer 1 (bit 0 = 1, TXDHDR1 `0603`) and acknowledged
one event later (TXTOG `0005`, TXBUF1CNTL `00`: `tx_queued` 1, `tx_acked` 1); the Mac then sent LL_FEATURE_REQ
(17.02 s). Nothing more left: `tx_busy` 467, `tx_none` 2, TXBUF0CNTL `01` for the whole connection, our FEATURE_RSP
and LENGTH_REQ (`ll_lproc` 2) queued, and at 24.07 s the Mac terminated (0x13, `peer_terms` 1). Reading: TXTOG
moved to buffer 0 after the acknowledged data PDU (not after the empty one: buffer 1 went out twice) and stayed there;
buffer 0 still had conn_start's bit 0 = 1 (the sheet's "empty", HW §7 step 5), which in the FM-1's direction is
"loaded", and the engine never sent or cleared it (it clears only the buffer it transmits, in TXTOG order). Every
refill wanted TXTOG's buffer (buffer 0) and found it "loaded".

The fix (`hw_tx_free`, `hw_tx_stuck`): once the direction is known nothing of ours is loaded, so a bit that reads
"loaded" is stale: it is written "free" (`tx_stale_clr`, a `stale` txsnap); with the bit clear the engine sends its
own empty PDU, as through blell3's 486 events, and the next PDU goes into TXTOG's buffer. Fallback: when the buffer
the refill wants (never one of our queued PDUs) still reads "loaded" for 2 events while the link layer has data
(`ble_ll_hw_tx_pending`), it is freed and loaded (`tx_force_free`, a `force` txsnap). Of the three readings the brief
weighed, this is the one both the FM-1 and the emulator's model (`tx_pol` 2: its bits are 1 = free at set-up, so
nothing is stale and nothing changes there) agree with; loading the non-TXTOG buffer or trusting TXTOG + NESN over
bit 0 would contradict the FM-1's own "busy" bit on the buffer it had just sent.

`tests/ble_driver_test.c` runs the driver with the whole stack against a fake engine that does exactly this (RX by
RXTOG while advertising, CNTL in a connection; TX: advertising leaves TXTOG on buffer 1, the engine sends TXTOG's
buffer while bit 0 = 1 and clears only that buffer's bit, TXTOG `7` → `5` and moving one event after a data PDU, never
after an empty one, conn_start's other buffer never sent nor cleared; a slot clock stepping back 267 slots every 97
events, TIMER4 wrapping). With a Mac-like central (VERSION_IND, FEATURE_REQ with DLE, Exchange MTU, Read By Group
Type) our VERSION_IND, FEATURE_RSP, LENGTH_REQ and both ATT responses go out and the link lives 63 s; a stale bit
put on TXTOG's free buffer mid-connection is forced free within 2 events (`tx_force_free` 1); a central that waits
gets our PERIPHERAL_FEATURE_REQ; a silent one still ends in 0x22 at 40.2 s; the sheet's TX direction works too.
5008663's driver against the same fake reproduces blell4 (FEATURE_RSP never out, the stale buffer busy for every
refill). The emulator's model still has the sheet's RX and TX CNTL semantics (TODO(model) in `tests/ble_emu_test.py`).

**blell6** (0475aa5: the stale bit freed, the force-free fallback; one Mac connection, `hw-logs/blell6.txt`): the
stale bit was freed (`tx_stale_clr` 1: TXBUF0CNTL `01` → `00`), our VERSION_IND went out of buffer 1 and was
acknowledged at event 0 (TXTOG `0005`, TXBUF1CNTL `00`). At event 1 the Mac's LL_FEATURE_REQ: TXTOG read `0002`
(bit0 = 0), so the FEATURE_RSP went into buffer 0 (TXDHDR0 `0907`, TXBUF0CNTL `01`); TXBUF1CNTL read `01` though
nothing of ours was in it (the engine set it). At event 4 (TXTOG `000E`) buffer 1 was force-freed and our LENGTH_REQ
put there (TXDHDR1 `0903`). Nothing more was acknowledged: TXBUF0CNTL stayed `01` for 265 events, the FEATURE_RSP
never left, and the Mac terminated 8 s after its FEATURE_REQ (0x13). `tx_queued` 3, `tx_acked` 1, `ctl_tx_last` 0C 09
14. So **the engine sent from buffer 1 only**, set-up's empty PDU included; buffer 0, loaded (bit0 = 1) with TXTOG
bit0 = 0, was never taken. TXTOG bit0 is not "the buffer sent next" (HW §8.1), and bit0 = 1 alone does not make the
engine send a buffer. The candidates, against the data:
- TXPTR0 bad: unlikely: TXPTR0 is the ADV_IND buffer, set once at link open and sent on every advertising event
  (the Mac scanned and connected); the RX buffers come before both TX buffers in `struct ble_bb`, no overlap. Not
  measured in a connection: txsnap now records TXPTR0/1.
- Software owns TXTOG / SN-NESN selects the buffer / one buffer per direction / TXDHDR's SN bit (bit2: 1 in buffer 0,
  0 in buffer 1, kept): none can be told apart from blell6 (TXTOG 7, 5, 2, E is not a plain toggle; bit1 is cleared
  by set-up, bits 2–3 are the engine's). txsnap now records the central's last RXDHDR (its NESN bit2, SN bit3), so the
  next log shows which SN the peer expects against TXDHDRn bit2.

The fix (`hw_tx_move`, the refill's buffer choice), relying only on what was measured, i.e. that a buffer the engine
takes a PDU from has its bit cleared: a PDU goes into the buffer the engine last took one of ours from (`tx_last`; at
first the one set-up's empty PDU left), the other one only while that one reads busy with nothing of ours in it; one
PDU at a time until the engine has taken PDUs from both buffers, then two (in order); a PDU not taken 2 events after it
was put in buffer b is moved to the other buffer, b freed first so both are never loaded at once (`tx_moved`, a `move`
txsnap); the other buffer, if busy with nothing of ours, is force-freed first (`tx_force_free`). With the sheet's
direction (`tx_pol` 2, the emulator) TXTOG's buffer and two at once, as before. The SN bit of the buffer a PDU goes
into is kept. Not measured on hardware yet; on the next run `tx_moved` and the `move` / `ack` txsnaps with `rxh`
show whether a PDU moved to buffer 1 is acknowledged and answered (the Mac's LENGTH_RSP, ATT).

`tests/ble_driver_test.c`'s fake engine now does what blell6 shows: it sends only from the buffer set-up's empty PDU
left (buffer 1), clears that buffer's bit as it sends it, never sends buffer 0; TXTOG 7 → 5 → 2 → E; the event after
the first data PDU it sets buffer 1's bit again and sits on it (as on a stale bit) until software frees it; the Mac
sends FEATURE_REQ two events after its VERSION_IND. 0475aa5's driver against it reproduces blell6 (FEATURE_RSP never
out, LENGTH_REQ out, `tx_queued` 3, `tx_acked` 1, `tx_force_free` 1); the fixed driver gets everything out (one move,
one force-free per re-armed bit) and the link lives 63 s.

**blell8** (cbb94d1: the move rule; one Mac connection, `hw-logs/blell8.txt`, also blell7): the Mac's VERSION_IND, our
VERSION_IND (acknowledged: TXBUF1CNTL cleared), the Mac's FEATURE_REQ at 62.116 s, our FEATURE_RSP loaded (buffer 1,
TXDHDR1 `0903`), and **the Mac's LL_LENGTH_REQ at 62.266 s**, 5 events later: a central starts no new procedure before
the current one has completed, so the Mac had received our FEATURE_RSP. TXBUFnCNTL bit0 of its buffer never read 0
again, so cbb94d1 took it as not sent and moved it back and forth to the end (`tx_moved` 65, `tx_force_free` 61,
`tx_busy` 301; txsnaps 124–131 alternate `move` / `force` with TXDHDR0/1 `0907` / `0903`); the LENGTH_RSP queued behind
it never went into a buffer (`tx_queued` 2, `tx_acked` 1, no 0x15 in `ctl_tx_last`) and the Mac terminated (0x13) 7.47 s
after its LENGTH_REQ, at event 256. The buffer cbb94d1 moved a PDU off read `01` again within 4 events (the engine set
it, as blell6's TXBUF1CNTL the event after the VERSION_IND was acknowledged). TXPTR0/1 `03D0` / `04E4` (distinct, in
our block). `rxh` (the Mac's last header) `0005` / `0009`: its NESN and SN alternate every event, sampled every 4.

What follows [I, the model the fix and the test use]: the engine keeps SN / NESN (Core Vol 6 Part B 4.5.9) and the SN a
TX buffer goes out with is its TXDHDR bit2, fixed per buffer: §7 step 17 gives the two buffers opposite bits and
nothing ever changed them (`0907` / `0903` through 65 moves; neither our writes, which keep bit2, nor the engine's). So
the engine sends buffer `sn_buf[s]` while its transmitSeqNum is s: the other buffer after each acknowledgement, the
same one again until acknowledged (the emulator's "SN fixed per TX buffer"). Bit0 is "loaded": the engine cleared it on
the VERSION_IND's buffer only and sets it again on a buffer the central has just acknowledged, so bit0 cleared is not
the acknowledgement, and a buffer left "loaded" goes out again as a new PDU. Not explained by it: blell6's FEATURE_RSP
in buffer 0 (SN 1), which the Mac never answered with a LENGTH_REQ (the model would have sent it at event 3 or 4, after
a second VERSION_IND out of the re-set buffer 1).

The fix (`ble-tx0`, `hw_tx_nesn` / `hw_tx_clears`): acknowledgement by the central's NESN, read from the RXDHDR the RX
path already reads (no register more). A PDU goes into `sn_buf[NESN ^ 1]` (NESN: the central's last; the engine has
answered that header from the other buffer already); it is in flight once a header received after the load asks for
its SN (NESN = s), and acknowledged when a later header has NESN = !s; the buffer is then freed (bit0 = 0, against the
engine's set-again) and the next PDU loaded in the same interrupt. A second PDU may wait in the other buffer while the
first is in flight (two in order). Nothing is moved or force-freed (the retransmission is the engine's); a "loaded"
bit on a buffer with nothing of ours is freed (`tx_rearm_clr`); the engine clearing bit0 is only counted
(`tx_cntl_clr`); `tx_ack_evt_max` is the longest load-to-acknowledgement in events. The sheet's direction (`tx_pol` 2,
the emulator) keeps bit0-back-to-1 as the acknowledgement. The LL already answers LENGTH_REQ (LENGTH_RSP), PHY, PING
and ATT; they were only stuck behind the FEATURE_RSP.

`tests/ble_driver_test.c`'s fake engine is now that model: SN / NESN per the spec on both sides, ping-pong by TXDHDR
bit2, bit0 cleared on the buffer sent for the first data PDU only and set again on the buffer acknowledged; a Mac that
sends VERSION_IND, FEATURE_REQ two events later, LENGTH_REQ once it has our FEATURE_RSP, then Exchange MTU, Read By
Group Type, the MIDI CCCD write, and terminates (0x13) 249 events after an unanswered LENGTH_REQ. cbb94d1's driver
against it fails as on the FM-1 (`tx_queued` 2, `tx_acked` 1, `tx_moved` 64, `tx_force_free` 63, `tx_busy` 315, closed
by the peer 0x13 at event 255); the fixed driver gets VERSION_IND, FEATURE_RSP, LENGTH_RSP (1 event after the
LENGTH_REQ), the MTU / group / write responses and a MIDI notification (1 event after the app's note) out, each once,
with no PDU of ours new twice, also with packets lost both ways and a stale bit injected, and the link lives 63 s. On
the next hardware run: `tx_acked` should follow `tx_queued`, `ctl_tx_last` should show 0x15, `tx_ack_evt_max` should
be 2–3; if `tx_acked` stays at 1 with `tx_queued` 2, the engine does not send by TXDHDR bit2 and the `load` / `ack`
txsnaps with `rxh` say which buffer it does.

**The TX rule** (`docs/BLE-HW-FACTS.md` §8.2, a3b2f06: the vendor's contract, read from its library's IR by the
fact-sheet agent; implemented here from the sheet alone). TXBUFnCNTL bit0 is the buffer's **empty** flag: 1 = empty
(software may fill it; a PDU software put there is finished), 0 = handed to the engine. Every driver from 5008663 to
51792b7 had it backwards, which explains blell4–9 (HW §8.2 point 6): a FEATURE_RSP marked bit0 = 1 was an empty buffer
to the engine for 265 events (blell6), and cbb94d1's "moves" got it out only because each move wrote bit0 = 0 on the
buffer it left, the vendor's load signal (blell8). What `ble_hw_wl82.c` does now:

- **Set-up** (`ble_hw_conn_start`, HW §7): both bit0 = 1 (the only 1-writes software ever makes), TXAHDR0/1 = 0,
  TXDHDR of the TXTOG-bit0 buffer = `0001` (LLID 1, bit2 0), the other `0005` (bit2 1); no PDU recorded in either.
  (§7 step 17 reads "XOR 5", which would give one buffer LLID 0; the driver writes LLID 1 on both, as §8.2's rule.)
- **Service** (`hw_tx_service`) at the end of every connection RX interrupt, after the RX buffers; never from the
  event interrupt (`ble_hw_tx_kick` stays empty: the link layer queues only from inside our interrupts, and the next
  RX interrupt loads). A snapshot: TXTOG bit0 and both bit0s read twice; if TXTOG bit0 changed between the reads the
  second reading, else the first. Then the TXTOG buffer, then the other:
  - **bit0 = 1**: a PDU of ours recorded there is acknowledged: released, `ble_ll_hw_tx_acked()`, an `ack` txsnap.
    Then the next PDU (control first: `ble_ll_hw_tx`): payload into the buffer's fixed TXPTRn area, TXAHDRn = 0,
    TXDHDRn = length << 8 | MD << 3 | LLID with bit2 kept, INTFRAME bit6 = MD (MD: another PDU still queued), then
    **bit0 = 0 as the last write**; recorded there, a `load` txsnap. Nothing queued: bit0 stays 1, INTFRAME bit6 = 0
    (`tx_none`); the engine sends empty PDUs.
  - **bit0 = 0**: the engine's. Untouched, except MD set on our own PDU there when more was queued since. With no PDU
    of ours in it (the FM-1 cleared bit0 on the TXTOG buffer at the first event with nothing loaded, blell4) it is
    only counted (`tx_eng_held`).
- **Never**: bit0 = 1 after set-up, a TXTOG write, a TXDHDR bit2 change, a PDU moved between the buffers, a buffer
  force-freed, an acknowledgement by NESN or by bit0 clearing. The acknowledgement is bit0 back to 1 on a buffer we
  loaded, nothing else. Gone with the old rule: `tx_pol`, `hw_tx_polarity`, `hw_tx_free` / `hw_tx_stuck` (stale /
  force), the move, `hw_tx_nesn` / `hw_tx_clears`, the refill from the event interrupt, and their counters.

`tests/ble_driver_test.c`'s fake engine is now §8.2's: it transmits only a buffer with bit0 = 0, the TXTOG buffer
first (else the other, TXTOG moving to it), keeps SN / NESN itself and retransmits its copy until acknowledged, sets
bit0 = 1 on the buffer whose PDU the central acknowledged and moves TXTOG bit0 to the other buffer; optionally the
FM-1's first-event quirk (bit0 of the TXTOG buffer cleared with nothing loaded). It also watches the driver: no bit0 = 1
write after set-up, no TXTOG write, no bit2 change, no write to a buffer the engine holds (MD excepted), no load from
the event interrupt. With the Mac-like central (VERSION_IND, FEATURE_REQ, LENGTH_REQ after our FEATURE_RSP, Exchange
MTU, Read By Group Type, the MIDI CCCD write) everything goes out once and in order, the LENGTH_RSP 1 event after the
LENGTH_REQ, a MIDI notification 2 events after the app's note, acknowledgements within 3 events (4 with loss), the link
up 63 s, also with packets lost both ways, TIMER4 wrapping, with and without the quirk; a waiting central gets our
PERIPHERAL_FEATURE_REQ; a silent one ends in 0x22 at 40.2 s. The previous drivers' polarity (bit0 = 1 written on a
loaded buffer) stalls against it: no data PDU ever leaves, as on the FM-1. The emulator's engine model already follows
§8.2 (bit0 = 0 sent, set to 1 on the acknowledgement, TXTOG moved by the engine; stock V15 runs on it), so
`tests/ble_emu_test.py` now checks the §8.2 txsnaps (the first a load into an empty buffer, acknowledgements within
8 events) instead of the learnt polarity.

On the FM-1 [not yet measured]: `tx_acked` should follow `tx_queued`, `ctl_tx_last` show 0x15 (LENGTH_RSP), `tx_ack_evt_max`
1–3, `tx_eng_held` small (the first event); `load` txsnaps show `snap` with the loaded buffer's bit set (empty) and
`cntl` 0 after; `ack` txsnaps the bit back to 1. If `tx_acked` stalls with a buffer at bit0 0 that never returns to 1,
the engine is not finishing it (TXTOG bits 1–3 and the `rxh` in the txsnaps are what to look at, HW §8.2's open points).

### 11.10 The Mac links but Audio MIDI Setup never shows Connected (blell10-12, c86fcb9, 2026-10-09) [M:hw + I]

**Measured** (`optimist-ble/hw-logs/blell10.txt`, `11`, `12`): the link is up (5,479 events, `tx` 27 / 27 acknowledged
within 2 events, VERSION / FEATURE / LENGTH done, the Mac's CONNECTION_UPDATE_INDs applied at their instants). The Mac
connected twice. First connection: `att_rx 10`, the last eight `10 10 08 08 04 12 08 1E`; `tx_queued 14` = 4 LL
control PDUs + 9 ATT responses + 1 Service Changed indication, so **no SMP and no signalling PDU left us** (no
Pairing Request ever came: we would have answered it) and no `03` (LL_ENC_REQ) in `ctl_rx_last`. Second connection
(`att_rx` 19 in all): `… 10 10 10 08 04 12 08 1E`; it ended 85 s later by the Mac (0x13). After the `1E` nothing
more, in either connection.

**Reading** [I]: our database is GAP 1-7, GATT 8-11 (Service Changed: value 10, CCCD 11), MIDI 12-15. The `1E`
(Handle Value Confirmation) needs an indication, and we only indicate after a write of 2 to the Service Changed CCCD
(`BLE_SC_ON_SUBSCRIBE`), so the one `12` was that write, and the sequence is: MTU (in the two opcodes before the
ring), primary services (`10` x3: 1-FFFF, from 12, from 16 → Not Found), the GATT service's characteristics (`08`,
range 8-11) and descriptors (`04`), its CCCD on (`12`), a Read By Type over everything (`08`: Core 5.1's "Read Using
Characteristic UUID" of the Database Hash 0x2B2A, Vol 3 Part G 7.3, the way a robust-caching client reads it [I: the
UUID was not logged]), then the confirmation of the indication "0x0001..0xFFFF changed" we had sent straight after the
write. The MIDI service's characteristics (`08` on 12-15) were never asked for: whoever wanted them (CoreMIDI) lost
them when the indication invalidated every handle, and nothing re-discovered. Apple's Accessory Design Guidelines
(2026-09-21, 58.12.2): "The accessory shall implement the Service Changed characteristic only if the accessory has
the ability to change its services during its lifetime. The device may use the Service Changed characteristic to
determine if it can rely on previously read (cached) information from the device." Our services never change
while we run, so the indication was spurious; stock V15 has no 0x1801 at all (§9.2 of the feasibility study).

**Fix**: no Service Changed indication (`BLE_SC_ON_SUBSCRIBE` 0); the characteristic and the handles stay as they
were. `tests/ble_driver_test.c`'s Mac runs this sequence and stalls on an indication as the FM-1 did; with the fix it
goes on to CoreMIDI's part (MIDI characteristic, descriptors, the Read, the CCCD on, MIDI both ways).

**What else was checked** (`ble_att.c` against Core Vol 3 Parts F / G and the BLE-MIDI spec), all as they should be:
Read By Group Type (6-octet entries for 1800 / 1801, then a 20-octet entry for the 128-bit MIDI service in its own
response), Read By Type 0x2803 (the MIDI declaration 21 octets: properties 0x16 = read, write without response, notify;
value handle 14; UUID least significant octet first: `F3 6B 10 9D 66 F2 A9 A1 12 41 68 38 DB E5 72 77` =
7772E5DB-3868-4112-A1A9-F2669D106BF3; the service `00 C7 C4 4E E3 6C 51 A7 33 4B E8 ED 5A 0E B8 03` =
03B80E5A-EDE8-4B33-A751-6CE34EC4C700), Find Information (format 1, one 0x2902 at 15), the CCCD write answered with a
Write Response (0x13), the Read of MIDI I/O returning an empty value (BLE-MIDI 1.0), CCCDs readable and writable
without security, the MTU (we answer 247 to the Mac's 517 and use the smaller; 58.11 says "should select an MTU
equal to or greater than the device's request": a should, kept to save RAM), the Data Length update before the MTU
exchange (58.11: the Mac does it).

**Does Apple need pairing for BLE-MIDI?** Not that any source says. The Accessory Design Guidelines' MIDI chapter (44)
asks only for the MIDI Association's BLE-MIDI 1.0a and testing in Audio MIDI Setup; 58.9: "The accessory should not
require special permissions, such as pairing, authentication, or encryption to discover services and
characteristics. It may require special permissions only for access to a characteristic value or a descriptor
value"; 58.10: "The accessory should not request pairing until an ATT request is rejected using the Insufficient
Authentication error code." The BLE-MIDI spec marks encryption "recommended" for the MIDI characteristic [S: as widely
quoted; the spec itself is members-only], a Silicon Labs MIDI-over-BLE example says the characteristic "shall require
encryption" (docs.silabs.com, "MIDI over BLE"), and an Infineon forum thread says Apple's specification wants an
encrypted connection; stock V15 sends a Security Request and pairs Just Works. So pairing is offered, not forced:
`BLE_SMP_LEGACY` with `BLE_MIDI_NEED_ENC` (Apple's way) or `BLE_SMP_SEC_REQ` (stock's way) are the next builds to try
if the Service Changed fix alone does not get "Connected" (§5.1).

**Instrumentation** (blell, RAM only): a ring of the last 64 protocol PDUs both ways (`pdu N: t=.. evt=.. rx|tx
att|sig|smp|ll n=LEN: first 8 octets`): every ATT PDU but MIDI's notifications and Write Commands (counted in
`att_ntf` / `att_wcmd`), every L2CAP signalling and SMP PDU, and the LL's encryption PDUs (LL_ENC_REQ shown as `03`,
EDIV, Rand's low 5; ENC_RSP, START_ENC, PAUSE_ENC, rejects); `enc_req` (LL_ENC_REQs seen), `enc_on` (encryptions
started), `isr_max_us`. With it the next session shows the Read By Type's UUID, the handle of every write, and any
SMP or encryption attempt.

Sources: Apple, Accessory Design Guidelines for Apple Devices, 2026-09-21 (developer.apple.com/accessories/
Accessory-Design-Guidelines.pdf), chapters 44 (MIDI) and 58.6-58.12; Bluetooth Core Specification v5.x, Vol 3 Part F
(ATT), Part G 2.5.2 / 7.1 / 7.3 (Service Changed, Database Hash), Part H 2.2.3 / 2.2.4 / 2.3.5 / 3.5-3.6 (c1, s1,
Just Works, key distribution), Vol 6 Part B 5.1.3 (encryption start), Part E (CCM); docs.silabs.com/bluetooth/2.13
"MIDI over BLE"; community.infineon.com "Apple's MIDI over Bluetooth Spec"; fm1-emulator `rust-emulator/README.md`
(stock's Security Request).

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
20 µs or more), one VCO scan, one left-out block and 10 section markers (10,706 bytes). The clock words of group 1 are
not replayed (`0x10010` bit 10 is our UART's clock, `0x10008` bit 3 the second core's start).

- **Section markers**: the capture marks where each §16.1 group starts, by register pattern only (`sections()` in the
  tool): 2 the first analog word, 3 the radio configuration, 4 the BBP first load, 5 the crystal trim (106), 6 the
  analog init, 7 the VCO scan, 8 the post-scan set-up, 10 the BBP second phase, 11 the RF-die LUT, 13 the 108 entries;
  `fm1_ble_rf_init` adds 14 (the BT block) and 15 (done). V15's capture has all ten. `bletrim` prints the last one
  entered (`rf_section`) and the set (`rf_sections`): on a unit where the start-up stops, the group it stopped in
  (the value survives as long as the FM-1 runs; a crash resets it).

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

- **Where the VM is** (measured on an FM-1, 2026-10-08, with the console's `flr` on a unit booted on stock V15 and
  then updated to Optimist through stock's own updater):
  - `0x093000`–`0x096FFF` (the two 8 KiB areas the emulator shows, HW §14.1) read **all FF** on that unit;
  - **`0x0E8000` holds the VM**: `55 AA AA 55`, then records in the §14.2 format: `0xE8004` 106 (2 B), `0xE800A` 107
    (7 B), `0xE8015` 187 (68 B), `0xE805D` 108 (20 B), `0xE8075` 113 (14 B), `0xE8087` 109 (34 B); the log ends at
    `0xE80AD`. 106 = `0B 0B`, 107 = `01 07 04 07 0B 01 07` (the emulator's values), 187's inner CRC holds (`8F 5A`);
  - `0x0E9000` is BTIF (record 102, the classic BT MAC), so an area at `0x0E8000` is at most 4 KiB;
  - the 4 KiB sector heads from `0x093000` to `0x0FFFFF` hold data at `0x097000`, `0x0E6000`, `0x0E8000`,
    `0x0E9000`, `0x0EA000`–`0x0F1000`, `0x0FA000`–`0x0FB000` and `0x0FC000`–`0x0FE000`; every other head is FF
    (`0x0E7000` included).
  - **Not known**: where stock's second VM area is on hardware (`0x0E7000`, before it, or elsewhere) and the area
    size there. The reader does not guess: it tries a short list of candidates.
- At every boot (`midi_ble.c ble_midi_init`) `ble_vm_scan` reads the VM in place through SPI reads (`st_read`). The
  live area is the first candidate, in this order, that starts `55 AA AA 55` **and** whose first record (length > 0,
  inside the area) passes its check: **`0x0E8000`** (4 KiB, measured), `0x0E7000` (4 KiB, a candidate only),
  `0x093000` then `0x095000` (8 KiB each: the emulator's layout, kept so its runs find their VM; both marked:
  `0x093000`). The magic alone is not enough because other data may sit at a candidate (§12.8: UP_FM6's voices move
  to `0x093000`). Then the records to the first failing check or the area's end, the last valid 106 / 107 / 108 / 187
  with their lengths, 187's inner CRC. `bletrim` / `blevm` print the area used (`vm_area` / `live`) and its size.
  The reader never writes or erases there (it has no write path; the emulator's dumps after our boots have the VM
  sectors unchanged); what else in Optimist writes there is §12.8.
- **The copy**: 100 bytes (`ble_rf_copy_*`: a mark with the area it came from, `0xAC` `0x0E8000`, `0xAD` `0x0E7000`,
  `0xAA` `0x093000`, `0xAB` `0x095000`; the four records' data, CRC-16/XMODEM)
  appended to the settings record (`persist_t.ble_rf`, after `ble_addr`, as the address was). Written with the
  settings, once quiet, when the VM has a complete set the copy does not hold yet. A build without BLE that saves its
  settings drops it (as it drops the address). The settings live at `0x0FC000` / `0x0FD000`, as does the BLE address
  (`persist_t.ble_addr`): nothing of BLE is stored in `0x0E7000`–`0x0E9FFF`.
- **Precedence**: the VM's complete set → the copy → none. **None**: the radio is never started (no RF, BT or
  baseband write), nothing advertises, HOME > SYSTEM > BLUETOOTH shows **NO RF CAL** under ON / OFF (the switch only
  changes the setting), and the console says why.
- **Console**: read-only commands, in CDC builds (`USB_MODE` 1; user-default has USB audio and no console) with BLE;
  the exact hardware session is §12.7. `blevm` prints every candidate's first word (in the order tried), every
  valid record (offset, id, length), the live area and its size, the log end, which records are there, 187's CRC and
  their data, as the firmware reads them; `blevmdump` every candidate area raw (`0x0E8000` and `0x0E7000`, 4 KiB
  each, then `0x093000`–`0x096FFF`: 24 KiB, 1,536 lines in `flr`'s format, then `end`; `tools/ble_vm.py LOG` decodes
  a saved log, `tools/ble_vm.py --base 0xe6000 DUMP.bin` a raw partial dump, a 1 MiB backup as it is); `bletrim` the source in use, BLUETOOTH and whether the radio was started
  this boot, the VM's summary, Optimist's copy raw (100 bytes) and decoded, the tables' SHA-256 and what the start-up
  did (ops, trims, LUT words, delays, the skipped count, BBP / SPI timeouts, the scan's band, steps and last comparator
  word, the last section and the set). `flr OFFSET [LEN<=256]` still reads any raw bytes from `0x093000` up (the VM:
  `flr 0xe8000 256`). None of them
  writes memory, flash or a register.

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
(`30ba97ea…` since the section markers; two runs identical); else nothing is written. About 30 s.

Output, kept **locally and git-ignored** so it survives a clean of `build/`: `config/ble/ble_rf_tables.h`,
`config/ble/ble_rf_tables.json` (the V15 SHA-256 it was captured from, this tool's format version and the tables'
pinned SHA-256) and `config/ble/ble_rf_capture/` (the emulator's own VM and the expected writes, for §12.4). **The
build** (`tools/build.py`, `FELUCCA_BLE=1`, `ble_rf_capture.ensure`) uses that cache as it is when it matches (the
format, the pinned hash, the header's own lines, and the firmware file if `FM1_STOCK_FWSC` names one: its SHA-256
equals the recorded one) and copies it into `build/gen/`, where the compiler and `tests/ble_emu_test.py` read it. The
capture runs again only when the cache is missing or stale, or when `FM1_STOCK_FWSC` names another file than the
recorded one (which must still be stock V15, else the build stops: a wrong file is never ignored). With no cache and no
firmware the build stops and says how: give your own stock V15 as `FM1_STOCK_FWSC=/path/to/FM-1.fwsc` (or
`firmwares/FM-1.fwsc`), with the emulator's `diagnose` (§12.4) that once; later builds need neither. Chosen over a
silent fallback: the sheet's rule (no BLE build without the tables) and the repository's way with inputs it may not carry
(`make sdk` fetches the SDK files; here nothing can be fetched, so the user's copy is used). The repository carries no
vendor-derived table: the tool, the field list of the sheet and a hash; nothing in `config/ble/` or `build/` is tracked
(`.gitignore`). A compiled BLE firmware does contain them (HW §17.2).

### 12.4 In the emulator [M: emulator model]

`tests/ble_emu_test.py` boots every run over a flash with a VM (the capture's, or one built from §14) and, since
BLUETOOTH is OFF by default, with ON saved (§11.7 `off_checks`); it passes all its earlier checks unchanged, and: **rf_init = stock V15's second boot, write for write**, 6,961 writes before our
scan and 13,399 after (the same VM laid in, the read-backs the model's 0); between them only the scan's registers
(5 steps, band 61, in range); the 128 AGC words; with no VM and no copy (ON saved), no write to the RF / BT / baseband
ranges and nothing on the air; the copy found in the settings sector after the first save (after 5 s, BLUETOOTH OFF)
and, with the VM then erased and ON saved, the radio starting from the copy and advertising. `tests/ble_rf_capture_test.py`
also checks the section markers (the groups in order on a synthetic trace) and `tools/ble_vm.py`'s decoding of a
`blevmdump` / `flr` log. Host: `tests/ble_vm_test.c` (every rule of §14 on built
images, the V15 and demo_ble extracts of `docs/ble-traces/v15-vm-trim-map.txt`, the FM-1's first 0xB0 bytes at
`0x0E8000`, the candidates' order, the 4 KiB bound (BTIF never read), non-VM bytes at `0x093000` never taken for a VM,
the copy and the precedence, a scan leaving the image unchanged) and `tests/ble_rf_capture_test.py` (the cut on
synthetic traces; the same rules in `tools/ble_vm.py`, and the FM-1's 16 KiB dump from `0x0E6000` decoded:
`tests/ble_vm_fm1_e6000.bin`, a copy with BTIF's record 102, the unit's MAC, zeroed).

### 12.5 Sizes [M]

Exact builds (`tools/optimist.py build`, 2026-10-08, after the merge of optimist 27dc239):

| user-default | Flash | RAM | RAMTEXT | Pool |
| --- | --- | --- | --- | --- |
| BLE off | 488,788 | 79,192 | 30,872 | 307,376 |
| BLE on (fits: nothing dropped) | 516,472 (+27,684) | 85,256 (+6,064) | 30,880 (+8) | 307,376 |
| BLE on, `USB_MODE=1` (the console, §12.7; re-measured after the VM candidates, 2026-10-08) | 519,416 | 86,440 | 30,848 | 295,088 |
| of which the generated tables | 11,598 (program 10,706, addresses 172, AGC 512, fields 208) | | | |

The slot is 581,564 B: 65,092 B left with BLE on. The builder's measured cost of the item (`costs.json`, measurement
link, `measure_costs.py --only BLE BLE_DIAG`, 2026-10-09, after the merge with the Optimist UI): BLE alone +29,624 B flash,
+6,176 B RAM, +136 B RAMTEXT; its option BLE_DIAG +5,768 B flash, +2,336 B RAM (35,392 B / 8,512 B with both).

**BLE off** (`FELUCCA_BLE=0`): `felucca.bin` is the optimist branch's own build (27dc239) byte for byte except the
four copies of `FELUCCA_CFG_HASH` (`0x05DB7ADE` here, `0x7D5B8C24` there): `configure.cfg_hash` hashes every registry
item's value, and this branch's registry has the item BLE (`BLE=0` in the hashed text). Swapping those four words
gives identical files (checked); no BLE code or data is in it.

### 12.6 What still needs a real FM-1 (with HW §16.5 / §18.3)

1. **The VM's real contents** (U2): one unit read (§12.2: `0x0E8000`, the six records, 187's CRC holds, the VM
   still there after stock's updater installed Optimist, U16). Still open: a second unit; where stock's second area
   is and its size (a compaction on hardware); `blevm` on a BLE build reading it in place.
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
9. **Whether a hang ends in the watchdog's reset** (§11.5: the boot guard then leaves the radio off for one boot):
   not measured on hardware; the decision itself is host-tested with a simulated count (`tests/ble_vm_test.c`).
10. **Register read-backs after `rf_init`** (HW §18.2): the console has no RF register peek yet (`memr` reads RAM and
    XIP only); a read-only peek of `0x11900`–`0x1197C`, `0x2FC00`–`0x2FCBC` and the BBP window read-back is the next
    console step for items 2, 4 and 5.

### 12.7 On a real FM-1: the console session (read only)

**BLE_DIAG.** The `blell` command and what feeds it (the link layer's counters, the event, RX, TX and protocol rings
of `ble_diag.h`, BLE-MIDI in's counters in `midi_ble.c`) are a builder option of BLE, `BLE_DIAG` (`FELUCCA_BLE_DIAG`,
off by default). Off, they are not compiled (every recording is `BLE_DG(statement)`, which is nothing); the radio code,
`bletrim`, `blevm`, `blevmdump` and the boot breadcrumb are the same as with it on. Measured (2026-10-09, `costs.json`):
+5,768 B flash and +2,336 B RAM; with the console (`USB_MODE=1`) another 6.1 KB of flash for `blell`'s printing. The
emulator test needs the block: `tools/optimist.py test` builds its BLE package with `BLE_DIAG=1`; the stack's host tests
keep it on and build once with `-DBLE_DIAG=0` as well. The sessions below need `--set BLE_DIAG=1` for `blell`.

Only the `flr` reads have run on hardware so far (2026-10-08, a build without BLE: §12.2's location); `blevm`,
`blevmdump`, `bletrim` and the radio's start have not. The commands only read (flash over SPI, RAM); none writes memory, flash or a
register. Paths below are relative to the repository.

1. **Build** user-default with BLE and the serial console (it fits as it is: 519,416 B of 581,564, nothing dropped):

   `FM1_STOCK_FWSC=/path/to/FM-1.fwsc python3 tools/optimist.py build --set BLE=1 --set USB_MODE=1 --set BLE_DIAG=1`

   (`FM-1.fwsc` = your stock V15 package; the build captures the tables from it once, §12.3, about 30 s, and keeps
   them in `config/ble/`. `BLE_DIAG=1` for `blell`; without it the other commands stay.)
   Install `build/felucca.fwsc` with the web installer or `python3 tools/fm1_install.py build/felucca.fwsc`.
2. **Console on**: HOME held > MENU > **USB SERIAL** > ON, then power the FM-1 off and on (the row shows RESTART until
   then). **BLUETOOTH stays OFF** (the default): nothing of the radio runs, so steps 3–4 are pure reads.
3. **Open the console** (macOS; the port name differs per machine: `ls /dev/cu.usbmodem*`). One terminal records
   everything the FM-1 prints:

   `cat /dev/cu.usbmodemXXXX | tee fm1-console.log`

   and a second one sends each command (a carriage return ends a line):

   `printf 'blevm\r' > /dev/cu.usbmodemXXXX`

   (An interactive terminal works too: `screen /dev/cu.usbmodemXXXX`, then type the commands; leave with Ctrl-A K.)
4. **With BLUETOOTH OFF**, in this order:
   - `help` (the list must include `blevm  blevmdump  bletrim  blell [clear|regs]`);
   - `blevm`: every candidate's first word (`area 0E8000: 55AAAA55` on the unit read so far), `live 000E8000`,
     `size 00001000`, the records (`@0E8004 id 106 len 2` … `@0E8087 id 109 len 34`), `log_end 000000AD`, `have`
     (`F` = 106, 107, 108 and 187 all there), `crc187 1`, `complete 1`, and the four records' bytes;
   - `blevmdump`: every candidate area (24 KiB: 1,536 lines, then `end`; about 85 KB of text, the panel waits while it
     prints). Then, on the computer: `python3 tools/ble_vm.py fm1-console.log` (the same decoding: every
     candidate's first word, the live area, every record, the RF set complete or not). Keep the log: it is this
     unit's calibration, and it holds no MAC (BTIF, `0x0E9000`, is not in it);
   - `bletrim`: `source VM`, `bluetooth_on 0`, `radio_started 0`, `rf_ran 0`, and `copy_raw` (Optimist's copy: 100
     bytes, made from the VM and saved with the settings once the FM-1 was quiet a few seconds after boot).
   - A build **without** BLE has no `blevm*` / `bletrim`: `flr 0xe8000 256`, `flr 0xe8100 256`, … `flr 0xe8f00 256`
     (16 reads, the VM's 4 KiB; add `flr 0xe7000 256` … `flr 0xe7f00 256` for the other candidate) give the same
     bytes, and `tools/ble_vm.py` decodes that log too (a candidate the log does not cover shows `(not read)`).
5. **Only if `blevm` says `complete 1`**, the radio's first start (HW §18.3 step 3, the read-back stage): HOME held >
   MENU > SYSTEM (last screen) > BLUETOOTH > ON. This runs `rf_init` once, now, in the main loop. Then `bletrim`
   again: `radio_started 1`, `rf_ran 1`, `rf_section 15` (done), `rf_sections 0000EDFC` (groups 2–8, 10, 11, 13, 14,
   15), `rf_bbp_timeouts 0` and `rf_spi_timeouts 0` (else the BBP port or the RF-die SPI does not answer as the
   emulator's model does), `rf_bad_op 00`, `vco_found` / `vco_band` / `vco_steps` / `vco_result` (our scan's
   result: §12.6 item 4). Whether it advertises: nRF Connect or a sniffer (HW §18.3 step 5).
   - **ON is saved when the menu closes**, so the radio then starts at every boot. If it hangs the FM-1 at a boot,
     **wait** (do not switch it off): the watchdog should restart it [I: unmeasured on hardware], the boot guard counts
     that start-up as failed, and the next boot leaves the radio off (`boot_failed 1` in `bletrim`, the row shows ON
     with nothing under it): switch BLUETOOTH OFF there (§11.5). A power-off / on instead clears the count and the
     radio starts again. Should even that not come up, power on with
     OCT− and OCT+ held 3 s (UBOOT, OPTIMIST.md). A hang in the menu's first ON is not saved (the menu has not closed).
6. Send back: `fm1-console.log` (steps 4 and 5) and what nRF Connect saw.
7. **A connection** (BLUETOOTH ON, the radio started): `blell clear`, then connect from the central (Audio MIDI Setup >
   Bluetooth, or nRF Connect), then `blell`, and `blell` again a few seconds later: the second shows what is still
   moving (advertising events, connection events). `blell` reads RAM only. `blell regs` adds the engine's columns
   (op 2, HW §2.1) and its interrupt registers, read with the two BLE interrupts held for a few microseconds: op 2 is
   known from static analysis only, so use `regs` last, once the RAM counters are saved. `blell clear` zeroes the
   counters (RAM only). Nothing is written to the engine or to flash. Its last block is the protocol ring (§11.10):
   `pdus`, then up to 64 `pdu N: …` lines, oldest first; with Audio MIDI Setup, send the whole output back (it shows
   every ATT request and its answer, the signalling, and any SMP or encryption attempt).
8. **After a freeze** (the panel stops, the console does not answer): **wait** for the restart (the watchdog, a few
   seconds), **do not power-cycle** (a power cycle clears the RAM the breadcrumb lives in), then run `dbg` and
   `bletrim`. `dbg` ends with `prev_ble` / `prev_ble_irqs` (the breadcrumb below); `prev_rst 00000004` is a watchdog
   reset.

#### The BLE breadcrumb (`dbg`: `prev_ble`, `prev_ble_irqs`, `ble_step`, `ble_irqs`, `prev_ble_op` / `gop` / `addr`)

`hal/fm1_ble_rf.h` keeps a word in `.noinit` (it survives a watchdog reset, not a power cycle) that every step of the
BLUETOOTH ON path writes, and a count of the BLE interrupts taken since that ON. At boot `main.c` moves them to
`prev_ble` / `prev_ble_irqs` (0 when the word was not valid). `prev_ble` reads `B1SSGGRR`: `SS` the step, `GG` rf_ops / 256
at the last rf_init group, `RR` that group (§12.1: 15 = rf_init done).

| `SS` | step (reached, not yet past) |
|---|---|
| 01 | the menu's ON taken (`ble_midi_set`) |
| 02 | the stack's init (`ble_init`) |
| 03 | `rf_init` (`RR` its group) |
| 04 | the baseband block and `fm1_ble_bb_init` |
| 05 | radio and baseband started |
| 06 | `ble_enable`: advertising being set up |
| 07 | stopping the link (`0x28038` busy poll, bounded) |
| 08 | the control block reset |
| 09 | advertising programmed (HW §6 steps 1-12) |
| 0A | advertising started (column 14 = 0x8000 written) |
| 0B | the BLE interrupts about to be let go |
| 0C | running: ON done, back in the main loop (a hang here with a huge `prev_ble_irqs`: an interrupt storm) |
| 0D | OFF done |

A hang at step 0C with `prev_ble_irqs` small points at the main loop; with it in the hundreds of thousands, at the BLE
interrupts. Steps 07-0A repeat for every advertising restart (after a connection), so they can also show up later.

**2026-10-08, 1f0ab85**: the menu's first ON froze the FM-1 until the watchdog reset it (`prev_rst 04`, `prev_stage 9`:
the main loop, the menu's ON runs in its `ui_input`), where 1cc6e04 advertised. The only new engine accesses on that
path were three column reads (op 2) issued straight after the start command (column 14 = 0x8000) in
`hw_adv_program`, plus a column-0 read in the CONNECT_IND interrupt and one after state 7. They are gone: the start
path and the interrupts issue the same register accesses as 1cc6e04, and the columns are read only by `blell regs`.
[Hypothesis, not measured: op 2 straight after a start command hangs the engine or the bus; the breadcrumb above
says where, should it hang again.]

**2026-10-08, 3a153c3** (and probably 1f0ab85 above, which had no breadcrumb yet): `prev_ble B1030405`, twice, once
after a cold power-on: rf_init stopped in §16.1 group 5, the crystal register `0x11930` ramp (1, 3, 7, 0xF, 0x1F,
0x5F, 0x25F × 7, then the VM 106 fields), ops 1187-1207. The rf_init code is the same in all four builds (disassembly
compared, addresses aside) and so is the RAM layout of 510e616 and 1f0ab85; what separates them is where
`ble_rf_prog` (flash, read through the data cache) starts: at 24 mod 32 in 1cc6e04 and 510e616, which ran, and at 8
mod 32 in 1f0ab85 and 3a153c3, which hung. With 32-byte lines the first new line of the group's program bytes is
fetched from flash right after the write of 0x5F in the builds that ran and right after the write of 3 in the builds
that hung. [Hypothesis, the best the four builds support, not measured on the chip: the ramp's first steps leave the
flash path unusable until it is complete, so a flash fetch there never returns.] Since then group 5 is decoded first
and written in one burst by `fm1_rf_burst_run` (`.ram_text`, interrupts off): nothing touches flash between its first
write and its last, wherever the build puts the table (`bletrim`: `rf_burst 16`). `tools/build.py` refuses a build
where that function is not in `.ram_text`. The breadcrumb gained `prev_ble_op` (rf_ops when the last op began),
`prev_ble_gop` (its index in the group; in the burst, the write 1-16) and `prev_ble_addr` (the last access begun: a
register address, `0xBB00rrdd` a BBP transaction, `0x5B00ccaa` an RF-die SPI command), so the next hang names the
access.

#### `blell`: the fields

Counters count from the radio's start or the last `blell clear`. Decimal unless the value is printed as hex (fixed
width). Times: `now_us` and the `ev` times are microseconds since the first BLUETOOTH ON (TIMER4; a radio left OFF
longer than 179 s loses whole wraps).

| Field | Meaning |
|---|---|
| `ll_state` | the link layer: `off`, `adv` (advertising, or believes it is), `conn` |
| `ll_enabled`, `ll_established`, `ll_interval`, `ll_lproc`, `ll_rproc` | BLUETOOTH ON; a packet heard in this connection; connInterval in use (x 1.25 ms); our / the central's procedure waiting (1 feature, 2 length, 3 terminate, 4 update, 5 PHY, 6 encryption) |
| `now_us` | the time of the last recorded event |
| `hw_state` | the driver: `off`, `adv`, `conn` (`blell regs` only, on the FM-1, with the radio started; so are the rows down to `wincntl0`) |
| `col0` … `col6`, `col14`, `col15` | the link-0 columns read now (HW §2.3): `col0`/`col14` the slot clock (`col14` bit15 = the link runs), `col1` interval, `col2` state and latency, `col3` event counter + 1, `col4` window offset, `col5` instant, `col6` channel selection, `col15` bit15 = events enabled |
| `col2_state` | `col2` bits 14:12: **2 advertising, 7 peripheral, 0 stopped** (HW §2.5) |
| `clock` | the 24-bit slot clock (`col14` [7:0] : `col0`) |
| `ien`, `ipnd`, `g2en`, `g2pnd`, `bbstat` | `0x28028` enables (bit0 event, bit8 RX), `0x28030` pending, `0x2804C` / `0x28050` the second group, `0x28038` (bit1 busy) |
| `txtog`, `rxtog`, `rx_next`, `rx_sn`, `tx_n`, `win_wide` | the engine's TX / RX buffer toggles; the RX buffer the driver waits on next, the SN it expects, its TX buffers loaded, the widened window still in use |
| `txbufcntl`, `rxbufcntl` | buffers 0 and 1 (two hex digits each): TX bit0 1 = empty / acknowledged; RX bit0 1 = filled by the engine |
| `rxstat`, `rxahdr`, `rxdhdr`, `txdhdr` | buffer 0 then buffer 1 (four hex digits each) |
| `intframe`, `format`, `optcntl`, `evtcount`, `wincntl0` | control-block words (HW §3) |
| `adv_starts` | advertising (re)programmed: boot / ON, after each connection, after a refused CONNECT_IND |
| `adv_events` | event interrupts (IRQ 45) while advertising: **must keep growing** between two `blell` while advertising |
| `adv_rx`, `scan_req`, `adv_drop` | RX buffers taken while advertising; SCAN_REQs among them; packets neither a SCAN_REQ nor a good CONNECT_IND (dropped **without restarting advertising**) |
| `adv_drop_stat`, `adv_drop_hdr` | the last dropped one's RXSTAT and RXAHDR |
| `busy_max`, `busy_timeouts` | the longest wait for `0x28038` bit1 after stopping the link (polls), and waits that gave up (the engine still busy while the driver reprogrammed it) |
| `cind_rx`, `cind_ok`, `cind_rej`, `cind_rej_why` | CONNECT_INDs handed to the link layer, taken, refused; the last reason: 0 taken, 1 not advertising, 2 format / length, 3 RxAdd, 4 AdvA, 5 interval / latency / timeout, 6 WinSize, 7 WinOffset, 8 hop, 9 channel map, 10 AA |
| `cind_hdr`, `cind_aa`, `cind_crc`, `cind_win_size`, `cind_win_off`, `cind_interval`, `cind_latency`, `cind_timeout`, `cind_chm`, `cind_hop`, `cind_sca` | the last CONNECT_IND's header octet and LLData as read (AA and CRC init as 32 / 24-bit numbers, the map as 10 hex digits, bit 36 first) |
| `cind_isr_us` | the interrupt's entry to state 7 written (us): the software part of the deadline (1.25 ms + WinOffset after the CONNECT_IND) |
| `first_evt`, `first_rx_us`, `first_rx_evt` | the first event counter seen, state 7 -> the first RX interrupt of the connection (us), EVTCOUNT then (65535: none) |
| `evt_irqs`, `rx_irqs` | IRQ 45 and IRQ 29 taken (all states) |
| `conn_events`, `c3_zero`, `evt_same`, `last_evt` | connection events handed to the link layer; event interrupts with `col3` still 0; with the same counter again; the last counter |
| `rx_good`, `rx_crc_bad`, `rx_bad_stat`, `rx_repeat`, `rx_empty` | new data PDUs; RXSTAT [3:0] != 1 (CRC / sync errors) and the last such status; repeated SN (dropped); empty PDUs among the new |
| `rx_nothing`, `rx_desync` | RX interrupts with RXBUFnCNTL bit0 = 0 on `rx_next` (advertising: whatever the other rules then found); with the **other** buffer filled than the one the driver waits on |
| `rxf_cntl`, `rxf_cntl_other`, `rxf_tog_prev`, `rxf_tog_cur`, `rxf_wait`, `rxf_late`, `rxf_none` | advertising RX (`hw_adv_find`): found by RXBUFnCNTL bit0 on `rx_next` (the model), on the other buffer, by its content alone (our AdvA in it) in the buffer RXTOG has moved past / still points at; found only after the RX ISR polled RAM up to 600 us; found by the event ISR; nothing at all |
| `rxl_cb`, `rxl_buf`, `rxl_none`, `rxh_synth` | where the PDU was: payload at RXPTR with the header in RXAHDR/RXDHDR (the sheet); the 2 header bytes at RXPTR; no PDU to our AdvA though CNTL said filled; RXAHDR held no type 3 / 5 (the header rebuilt from the content) |
| `rx_stat_zero`, `rx_stat_bad_valid`, `rx_wait_us_max` | a PDU to us with RXSTAT 0 (passed on) / RXSTAT [3:0] not 0 or 1 (dropped); the longest poll that found one |
| `rxsnaps`, `rxsnap_first`, `rxsnap N: ...` | advertising RX snapshots (RAM only): `w` 0 RX ISR entry, 1 after its poll, 2 event ISR; `f` the rule (0 none, 1 CNTL, 2 CNTL other, 3 content RXTOG-past, 4 content RXTOG-current); `nx` rx_next; `lay` 0/1/2 as `rxl_*`; `wait` us; RXTOG, RXBUF0/1CNTL, RXSTAT0/1, RXAHDR0/1, RXDHDR0/1, IFSCNT; `b0`/`b1` the first 4 bytes at RXPTR0/1. `rxsnap_first`: the first one that found a packet; then the last 8 |
| `tx_queued`, `tx_acked`, `tx_none` | PDUs put in a TX buffer, acknowledged, refills with nothing to send (the engine sends an empty PDU) |
| `clk_step_max` | the largest step of the link layer's clock (TIMER4) between two reads in a connection (us; about one interval). Builds before 2026-10-08: the slot clock's, in slots |
| `rxc_tog_past`, `rxc_tog_at` | connection RX (RXBUFnCNTL bit 0 found it): RXTOG had moved past that buffer / still pointed at it |
| `tx_eng_held` | TX service steps (§11.9's rule, HW §8.2) that found a buffer with bit0 = 0 and no PDU of ours in it (the engine's own; the FM-1's first event) |
| `tx_ack_evt_max` | the most events from loading a PDU to its buffer's bit0 reading 1 again (its acknowledgement) |
| `txsnaps`, `txsnap_first`, `txsnap N: ...` | TX steps (RAM only): `evt`, `what` (load / ack), `snap` the service's snapshot (bit0 TXTOG bit0, bit1 / bit2 TXBUF0 / 1CNTL bit0), `b` the buffer, `n` (bit b: a PDU of ours in buffer b), TXTOG, TXBUF0/1CNTL after the step, TXDHDR0/1, INTFRAME, `ptr0` / `ptr1` TXPTR0/1, `rxh` the central's last RXDHDR (NESN bit2, SN bit3). `txsnap_first`: the first load; then the last 8. (Builds 5008663..51792b7 printed `tx_pol`, `tx_busy`, `tx_tog_wait`, `tx_stale_clr`, `tx_force_free`, `tx_moved`, `tx_cntl_clr`, `tx_rearm_clr` and `pol=`: retired with the inverted polarity) |
| `ctl_rx`, `ctl_rx_last`, `ctl_tx`, `ctl_tx_last` | LL control PDUs received / sent, and the last 8 opcodes, oldest first (Core Vol 6 Part B 2.4.2) |
| `att_rx`, `att_rx_last` | ATT PDUs received, the last 8 opcodes |
| `closes`, `close_reason`, `close_by`, `close_evt`, `close_since_rx_us`, `close_since_start_us` | connections ended; the last one's reason (hex, Core Vol 1 Part F), by: 0 us (our TERMINATE acknowledged), 1 the central (LL_TERMINATE_IND), 2 supervision timeout, 3 never established (0x3E), 4 procedure timeout, 5 a protocol error (instant passed, parameters, MIC, PHY), 6 our TERMINATE never acknowledged; its event counter; the time since the last packet heard and since the CONNECT_IND |
| `sup_timeouts`, `estab_fails`, `peer_terms` | those endings counted |
| `events`, `ev T NAME ARG` | the events recorded, and the last 32: `enable` (ON/OFF, LL state << 8), `adv_start` (the interval), `adv_stop`, `adv_drop` (RXSTAT, RXAHDR << 8), `cind_rx` (header, length << 8), `cind_ok` (interval), `cind_rej` (reason), `conn_set` (`cind_isr_us`), `first_evt`, `first_rx` (its RXSTAT), `rx_bad` (RXSTAT, the first 4), `rx_desync` (RXTOG, rx_next << 4, state << 8, the first 4), `c3_zero`, `ctl_rx` (opcode, length << 8), `ctl_tx`, `instant`, `close` (reason, by << 8), `busy` |

### 12.8 The VM and Optimist's own flash map

The VM's measured home, `0x0E8000`–`0x0E8FFF`, was one of the two sectors Optimist's flash map gave to **UP_FM6**
(the user presets' FM6 voices, `OBJ_UPFM6`: copies A / B at `0x0E7000` / `0x0E8000`, `storage.c st_sector`;
docs/MEMORY-MAP.md), and `UP_FM6=1` in user-default (`config/profiles/user-default.config`; `CZ_NUSER` uses the same
object). An FM6 user-preset save to copy B would erase the VM, and with it the radio's calibration (only a BLE build's
copy in the settings would remain).

**Decision** (the user, via the coordinating session; branch **`fix/upfm6-off-vm`** on optimist, in progress when
this was written; not on this branch):
- UP_FM6's voices move to **`0x093000`–`0x094FFF`** on hardware;
- `0x0E8000`–`0x0E8FFF` is the SDK's VM and is never written; `0x0E9000` (BTIF) stays the SDK's;
- a build check and a runtime guard refuse writes to `0x0E8000`–`0x0E9FFF`.

What this branch does about it: the reader takes a candidate as a VM only with the magic **and** a valid first record
(§12.2), so UP_FM6's voices at `0x093000` (the emulator's area A) are never parsed as a VM (tested in both readers).
BLE itself writes nothing in `0x0E7000`–`0x0E9FFF`: the trim copy and the BLE address are in the settings record
(`0x0FC000` / `0x0FD000`). Until `fix/upfm6-off-vm` is merged here, this branch still has `OBJ_UPFM6` at
`0x0E7000` / `0x0E8000`.

## 13. The central role: connecting out (`BLE_CENTRAL`, round 2 of docs/BLE-DEVICES-DESIGN.md)

Status: **round 2 ran on an FM-1 against an iPhone** (§13.8: the link, Just Works and discovery work; the iPhone
wants an authenticated key), **authenticated pairing (§13.9) host- and emulator-tested, not yet on hardware**. Builder item
`BLE_CENTRAL` (bit 254), which now brings bonding (`BLE_LL_ENC`, `BLE_SMP_LEGACY`) with it: Apple's BLE-MIDI
peripherals ask for pairing (QA1831), so connecting out needs the SMP initiator. Written from the Core Specification,
the BLE-MIDI specification and the fact sheet's §21 (central role); no vendor code, IR or disassembly.

### 13.1 Link layer and driver (`ble_ll_central.c`, `ble_hw_wl82_central.c`, HW §21.3 / §21.4)

- **Our CONNECT_IND** (built before the engine starts initiating; `ble_ll_connect`): a random access address by the
  Core's rules (`ble_aa_valid`), a **random CRCInit** (the vendor's fixed `0x1983AE` is not used), WinSize 2, WinOffset
  random in [Interval / 2, Interval − 1] (the vendor's), Hop random 5..16, all 37 channels, latency 0, timeout 200
  (2 s), SCA 0 (251–500 ppm: unmeasured on the FM-1, the vendor's value; it only widens the peer's window by a few µs),
  and **interval 9 (11.25 ms)**: the top of the 7.5–11.25 ms our peripheral asks for (as stock V15 asks), ~89 BLE-MIDI
  packets a second each way at one PDU pair per event; 7.5 ms would add a third more event and RX interrupts while
  the software AES-CCM's cost in them is unmeasured (`ble_cfg.h BLE_CENTRAL_*`).
- **State 3** in the vendor's order (§21.3): RFPRIO 26, column 8 = 0, window / interval as scanning (64 / 60 slots),
  column 6 = `0x2100 | 37`, LOCALADR + FORMAT bit3 + OPTCNTL bit4 0, WHITELIST0 = TARGETADR = the target, FILTERCNTL
  bit0 | bit4 | type << 8, OPTCNTL bit3 0, both TX buffers the CONNECT_IND (TXAHDR `5 | TxAdd << 4 | RxAdd << 5`,
  length 34), column 2 = `0x3000`, the start, then **column 9 = 1**. The event interrupt moves the channel 37 → 38 →
  39 (RFPRIO 26, 30 every 6th). The RX interrupt takes each buffer by RXBUFnCNTL bit0 or by RXTOG having moved past a
  written header (both counted) and marks a **hit** on the target's ADV_IND (AdvA + TxAdd) or an ADV_DIRECT_IND to us.
  The engine is expected to have sent our CONNECT_IND T_IFS after it (C3).
- **The switch to master** in the event interrupt after the hit (§21.3): AA / CRC, OPTCNTL, both TX buffers empty,
  column 4 = 0, **the anchor counter**: column 7 = 0, 0 = 0, 14 = 0, then column 0 = 2 × WinOffset + 3 with column
  14 = `0x8000`, twice; column 2 = `0x6000`; WINCNTL = WinSize × 1,250 + 1,250 µs; interval and hop columns; TXDHDR of
  the TXTOG buffer bit2 0, the other 1. No column read (op 2) on the initiating path or the switch.
- **The master's events** run on the peripheral's code: the RX rule of §8, the TX rule of §8.2 (unchanged), the event
  service (column 3 − 1). Differences (§21.4): after the first event with a packet WINCNTL = 0 and WINCNTL2 = 30 µs;
  an update of ours writes at instant − 1 column 4 = `0x8000 | 2 × WinOffset`, WINCNTL = WinSize × 1,250 + 625, column
  2 = `0x6000`, columns 1 / 15, and the 0 / 30 window again two events after the instant; no widening.
- **The master's procedures**: we start the version exchange, the feature exchange, the encryption the host asks
  (LL_ENC_REQ: Rand, EDIV, SKDm, IVm; SK = e(LTK, SKDs ‖ SKDm), directionBit 1 for ours) and the data length; we
  answer the peripheral's feature exchange (LL_PERIPHERAL_FEATURE_REQ), its PHY request (LL_PHY_UPDATE_IND, no change),
  its LL_CONNECTION_PARAM_REQ and its L2CAP Connection Parameter Update Request (both with an LL_CONNECTION_UPDATE_IND,
  WinSize 1, WinOffset = Interval / 2, instant = counter + 8..11, §21.4), and LL_CHANNEL_MAP_IND of ours at counter +
  7..10 (`ble_ll_chmap_update`, unused while AFH is off). The supervision timeout is the link layer's, on TIMER4
  (`ble_hw_time_us`), as for the peripheral; a master that hears nothing in six intervals closes with 0x3E.

### 13.2 Host: GATT client, security (`ble_gattc.c`, `ble_central.c`, `ble_smp_init.c`)

- **GATT client**: MTU (247; the smaller is used both ways), Find By Type Value for the BLE-MIDI service (a server
  without it: Read By Group Type over the primary services), Read By Type 0x2803 in its range for the MIDI I/O
  characteristic (notify among its properties) and its end, Find Information for its CCCD, Write Request 0x0001 →
  **ready**. Notifications on its value are MIDI in (the decoder of today); MIDI out goes as **Write Without Response**
  from the same encoder and ring as our notifications. One request outstanding, the 30 s timeout.
- **Security** (only when the peer asks, at the level it proves it needs: §13.9): a bonded LAST is encrypted with
  its LTK right after the connection (before it has to ask); an Insufficient Authentication / Encryption / Key Size
  error, or the peripheral's SMP Security Request, encrypts with the bond or else **pairs as initiator** (legacy
  Just Works, or legacy passkey entry with the FM-1 displaying when the peer is known to need MITM; bonding, keys
  both ways: its LTK / EDIV / Rand and IRK + identity address are the firmware's, `ble_app_central_keys`, with
  `BLE_KEYS_AUTH` after a passkey; ours are sent after its, never used); the request that asked goes again once
  paired or encrypted. A bond the peripheral lost (LL_REJECT, Key Missing) pairs afresh. The 30 s SMP timeout
  (from our last SMP command) ends the link.
- **Endings** (`ble_central_fail` / `_code`): LOST (the LL's reason, e.g. 0x3E, 0x08, 0x13), NO MIDI SERVICE,
  PAIRING (the SMP reason), AUTH (refused although encrypted at the level it needed, or Authentication Requirements),
  NEED_MITM (it needs a passkey and refused the pairing with MITM on the link: the firmware connects again once with
  a passkey), GATT ERROR. A failure leaves the link once the queued PDUs went (so our Pairing Failed reaches the peer).
  A link that is not established (0x3E) or lost before any pairing started on it is made again at once by
  `ble_connect.c`, up to 6 attempts per user action (`CONNECTING (TRY n/6) <name>`, FAILED only after the last).
- **RPA**: `ble_rpa_resolve(irk, addr)` = ah (Core Vol 3 Part H 2.2.2) with the software AES, checked against the
  Core sample (D.7).

### 13.3 One link, roles and MIDI

The router is unchanged: `ble_midi_ready()` is "its notifications are on" as central, "the central subscribed" as
peripheral; MIDI out is a Write Command or a notification accordingly; MIDI in is the same decoder. While a central
link is up the FM-1 does not advertise (one link); when it ends, advertising (or the DEVICES scan) comes back.

### 13.4 The firmware: a pick, LAST and its search (`io/midi/ble_connect.c`)

- **A pick** (OCT+ on a nearby row): connect (`ble_central_connect`); **ready → it becomes LAST and the choice**
  (`ble_store_set_last`, `sel` = LAST, saved with the settings once quiet), the pairing's bond and identity with it
  (the identity address replaces the AdvA). 10 s unheard: "FAILED: NOT FOUND"; otherwise "FAILED: <why>" (§13.2).
- **LAST** (the ruling, design §0.1): while BLUETOOTH is ON, LAST is the choice, DEVICES is closed and no link of
  either role is up, the FM-1 searches for it: initiate 2 s, advertise 1 s, for 30 s; then initiate 1 s every 10 s.
  A LAST with an IRK (iOS, macOS: private addresses) is found by **scanning and resolving** each ADV_IND's AdvA with
  its IRK (the engine has no resolving list, HW §21.8), then initiating to the address just heard. A central that
  connects to us meanwhile is accepted and never changes LAST; the search waits for its link to end.
- **NONE** leaves our central link and stops the search (a Mac connected to us stays). FORGET does the same and
  drops the entry.
- The SLOOP menu (`ui_menu.c`): BLUETOOTH's status `CONNECTING` / `SEARCHING` / `PAIRING` / `CONNECTED` / `FAILED`;
  DEVICES' rows tag `CONNECTING` / `PAIRING` on the device picked and `CONNECTED` on LAST while our link to it is up;
  the status area (`ble_connect_status`) `CONNECTING <name>`, `PAIRING <name>`, `ENTER THIS CODE ON THE PHONE` over
  the passkey in the large font, `CONNECTED <name>`, `LOST <name>`, `FAILED: <why>` (kept until the user acts); LAST
  heard nearby is not listed twice.

### 13.5 Tests [M: host; the emulator's models]

`tests/ble_central_test.c` (the stack against a simulated BLE-MIDI peripheral: the CONNECT_IND's fields, the master's
procedures and updates, the GATT client, SMP as initiator with every failure, a reused / lost bond, ah's Core sample,
blell), `tests/ble_central_driver_test.c` (the driver against the fake engine: state 3 and the switch to state 6 write
for write, the master's events and update), `tests/ui_pages_test.c` (DEVICES: a pick connecting, ready → LAST with its
keys, NONE), `tests/ble_emu_central_test.py` (fm1-emulator `feat/ble-engine` bbd2f22: its engine model of states 3 / 6
and a virtual BLE-MIDI peripheral, `FM1_BLE_PERIPHERALS`): DEVICES driven with the panel's contacts → pick → our
CONNECT_IND sent by the engine model, the first master packet 1,251 µs into the transmit window → version, features,
MTU, discovery → Insufficient Authentication → pairing → the CCCD again → its notes play the synth (~38,000 non-silent
frames), a key press reaches it as a Write Command → LAST in the store (bonded); **reboot** (the flash dump) → the FM-1
reconnects by itself with the stored LTK, no new pairing, notes play; the same with a Mac-like peripheral whose new
private address after the reboot is resolved with its IRK; NONE while connected → our terminate, no search after; an
iPhone-like peripheral (fm1-emulator `auth`, 63bf79e) → §13.9's escalation, the passkey read off `smp_passkey` and
typed by its virtual user, authenticated, LAST with `sec` = AUTH | MITM, the reboot reconnecting with that LTK; its
user typing it wrong → one failure, two connections in all, no LAST. FM-1 to FM-1: `tests/ble_f2f_test.c` (two and
three whole stacks, each its own object as in the firmware's unity build): an open FM-1 connects with no SMP, no
encryption and no passkey; one with `BLE_MIDI_NEED_ENC=1` gets a silent Just Works pairing.

### 13.6 Round 1 on the FM-1 (b821e3e, the user's session, 2026-10-09) [M:hw]

DEVICES opened with an iPhone app and a Mac advertising: `scan_events` 1,235, `scan_rx_irqs` 5,612, reports by
RXBUFnCNTL bit0 (`scan_rxf_cntl` 6,301, `scan_rxf_tog` 0), `scan_adv_ind` 2,555, `scan_scan_rsp` 2,100,
`scan_req_armed` 2,412, `scan_rsp_ok` 1,935, no ring overflow; the raw RSSI word ≈ `0x7316`. So **HW §21.9 C1 = yes**
(the engine sends SCAN_REQ and fills AdvA: scan responses came back), **C2 = yes** (RXBUFnCNTL bit0 marks a report
while scanning), **C8: scanning runs** with column 15 bit15 = 0. A pick showed "CONNECT NOT YET" (round 1), and an
iPhone (midimittr) connected **to** the FM-1 as central, which works. (To be recorded in the fact sheet's §21.9.)

### 13.7 What the hardware run must check (round 2; round 3 adds §13.9's lines) [HW?]

`blell` after a pick (the `cen_*`, `init_*`, `m_*`, `gc_*`, `si_*`, `rc_*` lines, `ble_diag.c bd_central`):

| Question | Read | Means |
| --- | --- | --- |
| The target heard while initiating | `init_rx_target` > 0, `init_rxf_cntl` / `init_rxf_tog` | which RX rule state 3 follows |
| C3: the engine sends our CONNECT_IND by itself | `master_starts` > 0 and `m_events_rx` > 0; `m_estab_fails` (0x3E right after a switch) | yes / no CONNECT_IND reached the peer (or a wrong anchor) |
| C4: the event IRQ after the CONNECT_IND | `c4_rx_to_evt_us` / `_max` | ~ the CONNECT_IND's 352 µs + T_IFS: it comes right after it; a window's length (~37 ms): at the window's end (then the anchor is late) |
| C5: the anchor counter (2 × WinOffset + 4) | `m_first_rx_us`, `m_first_rx_evt`, `m_first_evt` | the peer answered our first anchor (event 0): inside its transmit window |
| C6: the master transmits first; the 30 µs RX window | `m_events_rx` / `m_events` ≈ 1 | the peer answers every event |
| C7: column 0 / 14 a countdown | `blell regs` twice between events (console only, never in an ISR) | col 0 decreasing |
| LL procedures | `m_ver_rx`, `m_feat_rsp`, `m_len_done`, `m_param_req_rx`, `m_l2_upd_rx`, `m_upd_tx` | |
| Encryption | `m_enc_req_tx`, `m_enc_rsp_rx`, `m_start_enc_rx`, `m_enc_on`, `m_enc_rej` (`_err` 06: the bond lost) | |
| GATT client | `gc_state` (7 ready, 8 failed), `gc_mtu`, `gc_svc`, `gc_val`, `gc_cccd`, `gc_last_err`, `gc_auth_errs`, `gc_retries` | where discovery stopped |
| Pairing | `si_pair_req`, `si_pair_rsp`, `si_confirm_ok`, `si_stk_enc`, `si_keys_rx` / `_tx`, `si_done`, `si_fail_rx` / `_tx`, `si_last_fail` | |
| Authenticated pairing (§13.9) | `si_rsp_io` / `si_rsp_auth` (the responder's IO capability, AuthReq), `si_mitm_req`, `si_passkey`, `si_auth_done`, `si_fail_late`, `cen_need_mitm` | 04 / 01 from the iPhone; a passkey shown and accepted |
| MIDI | `gc_ntf_rx` (notes in), `gc_wcmd_tx` (MIDI out) | |
| LAST | `rc_picks`, `rc_tries`, `rc_scans`, `rc_rpa_seen` / `rc_rpa_ok` (an iPhone's address resolved), `rc_ok`, `rc_fails`, `rc_last_fail` | |

Test order (design §5 P3 / P4): BluePiano LE (or AUM, midimittr in peripheral mode) advertising on the iPhone, then a
Mac with Audio MIDI Setup's Advertise on: DEVICES → pick → `CONNECTED <name>` (iOS / macOS may ask to confirm the pairing [I]); play
keys both ways; lock the iPhone / close the app (no stuck note, `LOST`); power-cycle the FM-1 with the app
advertising: it reconnects by itself (`rc_rpa_ok` for iOS); NONE leaves it.

### 13.8 Round 2 on the FM-1 (90d2eeb, the user's session, 2026-10-09; `hw-logs/blell-dev3.txt`) [M:hw]

An iPhone app advertising BLE-MIDI, picked six times in DEVICES:

- **HW §21.9 C3 = yes**: the engine sends our CONNECT_IND by itself (`master_starts` 6, `cen_connects` 6,
  `m_events` 1,130 with `m_events_rx` 1,114). **C4**: `c4_rx_to_evt_us` 510 (max 1,883): the event interrupt comes
  right after the CONNECT_IND. **C5 / C6 OK**: the peer answered our first anchor (`m_first_rx_evt` 0,
  `m_first_rx_us` 13,272) and every event after it (`m_estab_fails` 2 of 6 attempts, no supervision timeout).
- LL procedures: version, features, data length done (`m_ver_rx` / `m_feat_rsp` / `m_len_done` 4); LL encryption as
  master works (`m_enc_req_tx` = `m_enc_rsp_rx` = `m_start_enc_rx` = `m_enc_on` 4, no reject).
- **Just Works completes and encrypts** (`si_pair_req` / `si_pair_rsp` / `si_confirm_ok` / `si_stk_enc` / `si_done`
  4, keys 16 each way). The iPhone's Pairing Response is `02 04 00 01 10 03 03`: **KeyboardDisplay**, no OOB, AuthReq
  `01` (bonding only, no MITM, no SC bit: it mirrored our request), key size 16, keys both ways. About 1.4 s pass
  between our Mconfirm and its Sconfirm (the iOS pairing prompt answered [I]).
- Discovery finds the BLE-MIDI service (`gc_svc` 0039–003D, value 003B, CCCD 003D, MTU 247).
- Then **the iPhone sends Pairing Failed `05 08` (Unspecified Reason) right after the key distribution** (two
  events after our keys; `si_fail_rx` 2) and **refuses the CCCD write with 0x05 Insufficient Authentication** on the
  encrypted link (`gc_last_err` 12003D05, `gc_auth_errs` 8, `gc_state` 8 failed). Round 2's code then paired again
  on every attempt (a pairing prompt on the phone each time) and fell back to SCANNING without saying why: what the
  user saw as "connected briefly, then scanning".

Reading: an encrypted, unauthenticated (Just Works) key does not satisfy the iPhone app's MIDI characteristic; it
needs an **authenticated** (MITM) key, security mode 1 level 3 (Core Vol 3 Part C 10.2.1). The same symptom with the
roles the other way is documented for TI peripherals with `GATT_PERMIT_AUTHEN_*` characteristics paired by Just
Works: Insufficient Authentication and a re-pairing prompt every time [S: e2e.ti.com thread 302987]. The Pairing
Failed after the keys is the iPhone refusing that bond [I]. **Not ruled out [I, unmeasured]**: the iPhone already held
a bond for the FM-1's identity address from round 1 (it connected **to** the FM-1 as central, `BLE_BOND`), and our
initiator hands out a new random IRK with that same identity address at each pairing; iOS may refuse to replace a
bond silently. Before round 3's hardware test: **forget "FM-1 XXXX" in the iPhone's Settings > Bluetooth** if it is
listed, so that cause is out of the way.

### 13.9 Authenticated pairing, no dialog unless necessary (round 3: `ble_smp_init.c`, `ble_central.c`, `ble_connect.c`)

**Method: LE legacy passkey entry, the FM-1 displaying** (Core Vol 3 Part H 2.3.5.3; Table 2.8: initiator
DisplayOnly, responder KeyboardDisplay → passkey entry, the initiator displays and the responder's user types).
It is the smallest method that gives an authenticated key against the iPhone's KeyboardDisplay: no P-256, no new
crypto (TK = the passkey through the c1 / s1 the Just Works path has; flash: +0.8 KB for round 3 in all). Secure
Connections is **not** implemented: it would need P-256 ECDH (micro-ecc, BSD-2-Clause, GPL-compatible, about 5–8 KB
of flash [I]) run in the main loop. Whether iOS accepts an authenticated **legacy** key for the app's characteristic
is [HW?]: if the hardware shows `FAILED: AUTH` after a passkey pairing (refused again with 0x05, `si_auth_done` 1),
SC Numeric Comparison / Passkey is the next step. Legacy passkey entry is open to a passive eavesdropper on the
pairing itself (the 6-digit TK is brute-forced from the exchange) and to the pairing-mode-confusion MITM
(CVE-2022-25836); for MIDI between a synth and a phone that is accepted here.

Rules (the user's: no dialog unless necessary):

1. **Nothing is asked of anyone until the peer asks**: no Pairing Request of ours without its Security Request or
   an ATT 0x05 / 0x0F / 0x0C. FM-1 to FM-1 (and any controller that does not ask) connects with no SMP at all
   (`tests/ble_f2f_test.c`); a peer that only wants encryption gets Just Works, silent on our screen (NoInputNoOutput).
2. **Passkey only when the peer proves it needs MITM**: refused again with Insufficient Authentication after a Just
   Works bond, or its Security Request has the MITM bit. Since 2026-10-09 the pairing with MITM is made **on the same
   link** (Core Vol 3 Part H 2.4: a pairing on an encrypted link): Pairing Request **DisplayOnly, Bonding + MITM, no
   SC, no keypress**, the passkey, then the STK through the LL's **encryption pause** as master (Vol 6 Part B 5.1.3.2:
   our LL_PAUSE_ENC_REQ encrypted, its LL_PAUSE_ENC_RSP encrypted, ours plain, then LL_ENC_REQ with the STK; blell
   `si_repair`, `m_pause_tx`, `m_pause_rsp_rx`). No disconnection, so no new link to establish (the reconnection
   the FM-1 failed with 0x3E, blell-dev4). Only a peer that refuses that pairing before any passkey is shown (Pairing
   Failed, or no Pairing Response in 5 s) gets `BLE_CF_NEED_MITM`: the link is left and `ble_connect.c` connects again
   at once, once, to the same address with `BLE_PEER_MITM` and without the Just Works bond (`si_repair_fallback`). A
   refusal after the passkey was shown (the pause refused) is `FAILED: PAIRING`: one prompt per user action. A
   responder that cannot type (its IO capability is not KeyboardOnly / KeyboardDisplay) would turn it into Just Works:
   we fail it at once with Authentication Requirements (0x03) → `FAILED: AUTH`.
3. The passkey: 0..999,999 from the hardware RNG (`ble_hw_rand`, 32 bits mod 10⁶), shown from the Pairing Response
   until Srand checks out (`ble_central_passkey`; `smp_passkey`, a symbol of its own so the emulator's virtual
   phone user can read it). The 30 s SMP timer restarts with each command of ours (3.4), so the user has 30 s from our
   Mconfirm.
4. **One attempt at the needed level, no loop**: any failure of the passkey pairing (a wrong passkey: Confirm Value
   Failed; the timeout; Authentication Requirements) or a refusal after it ends the attempt: `FAILED: PAIRING` /
   `FAILED: AUTH`, the search held (`RC_HELD`): nothing connects or prompts the phone again until the user acts (a
   pick, LAST, NONE, FORGET, BLUETOOTH OFF).
5. **The level is remembered with LAST** (`struct ble_dev.sec`, the record's last octet, 0 in older records):
   `BLE_DEV_SEC_MITM` once it needed a passkey, `BLE_DEV_SEC_AUTH` when the stored bond came from one. The next
   connection to it encrypts with the authenticated LTK (no dialog); if the phone lost the bond, the new pairing goes
   straight to the passkey (no Just Works first). A device picked that needed a passkey but did not become LAST is
   remembered in RAM (its address, and its IRK from the Just Works keys): picked again, it pairs with the passkey at
   once.
6. **Our keys go before the request that asked**: after a pairing, the GATT request is sent again only once our keys
   were handed to the engine (a later event), not in the same burst. The iPhone's Pairing Failed 0x08 came three
   events after our last key, with the refusal of the CCCD (blell-dev4 pdus 31-37); our key distribution is the
   Core's (the responder's keys first, then ours: EncKey, IdKey, as it asked in InitKeyDist 03), so the 0x08 is read
   as iOS refusing an unauthenticated bond for a characteristic that needs MITM, not as an order of ours.

A Pairing Failed after our pairing ended (what the iPhone did after Just Works) is counted (`si_fail_late`) and
otherwise ignored: the ATT refusal that follows decides.

Screens (SLOOP DEVICES, the design's §9.2 rules, its own two-band draw): the header's right `CONNECTING` /
`PAIRING` / `CONNECTED` / `FAILED` (amber); the picked row tagged `CONNECTING` / `PAIRING` in the bars' place; the
status area `CONNECTING <name>`, `PAIRING <name>` (Just Works, nothing to type), `ENTER THIS CODE ON THE PHONE` with
the six digits in the large font in place of the keys' help, `CONNECTED <name>` once subscribed, `FAILED: <why>`
kept until the user acts.

Tests: `tests/ble_prim_test.c` (the passkey's TK; c1 / s1 with TK 123456 and 999999, the reference values from an
independent c1 / s1 over Python's `cryptography` AES that reproduces the Core samples with TK 0),
`tests/ble_central_test.c` (an iPhone-like peer: Just Works → late Pairing Failed → 0x05 → the passkey pairing on
the same link, the encryption pause; a peer that refuses it (NEED_MITM), never answers it (5 s), refuses the pause;
the passkey pairing with the user typing 4.5 s; the authenticated bond reused; a lost bond straight to the passkey; a wrong
passkey; a peer that cannot type; a peer refusing even an authenticated key; the 30 s timeout; a Security Request
with MITM), `tests/ble_f2f_test.c`, `tests/ble_store_test.c` (`sec`), `tests/menu_ui.c` (the states, the passkey
screen, the reconnection with the passkey, the failure held for a minute with no new attempt, the user acting; the
link retries, CONNECTING (TRY n/6), no retry after a prompt, the passkey at once for a device that needed it),
`tests/ble_emu_central_test.py` (§13.5).

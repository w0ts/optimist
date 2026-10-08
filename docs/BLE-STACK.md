# BLE MIDI, route C: our own stack (link layer, host, BLE-MIDI)

Status: **EXPERIMENTAL. Host-tested only. There is no baseband driver yet, so nothing here has sent a
packet.** Build flag `FELUCCA_BLE` (default 0 = off; the image with it off is byte-identical to one without
this code). Builder item `BLE` (Experimental group). Background: `BLE-MIDI-FEASIBILITY.md` §9 (route C, the
stock firmware's behaviour in §9.2).

Tags: **[M]** measured this session (host tests, the JieLi toolchain). **[S]** from a published
specification. **[I]** inference, not measured. **[HW?]** needs the hardware fact sheet or a device.

## 1. Provenance (clean room)

Written from the Bluetooth Core Specification v5.x (Vol 3 Parts A, C, F, G, H; Vol 6 Parts B, C, E; LE 1M
only) and the MIDI Association's "Specification for MIDI over Bluetooth Low Energy" 1.0. No vendor code, no
vendor libraries, LLVM IR or disassembly were read; no Cordio source. The ideas (not the code) of the route B
study's minimal ATT host were reused. Test vectors and their sources are named in each test file's header.
The baseband driver will be written from `docs/BLE-HW-FACTS.md` (branch `feat/ble-facts`, another author)
against the interface in §3.

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
| `ble_hw_stub.c` | a stand-in driver that never calls back (until the real one exists) |
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

Against the 20 KB target that leaves room for the driver and SMP. In the firmware (user-default + `BLE=1`,
measurement link): +560 B flash and +1,600 B RAM today, because with the stand-in driver nothing calls the link
layer's RX / TX entry points and the compiler drops them; expect about the table's numbers plus the driver once a
real one calls them [I]. The builder's costs.json has the same measurement (+892 B flash, +1,616 B RAM on its own
base). With `FELUCCA_BLE=0` the code is identical: only the builder's configuration hash (it covers the registry,
which gained the item) differs, 4 words, same size.

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

## 10. Open questions for the hardware fact sheet

Facts the driver needs that `docs/BLE-HW-FACTS.md` (41b7374) does not settle; most are its U-list items:

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

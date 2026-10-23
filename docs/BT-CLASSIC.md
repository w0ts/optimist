# Bluetooth Classic (BR/EDR): the hardware, our probe, and the A2DP plan

Status 2026-10-19: **research and an EXPERIMENTAL probe, paused.** No Classic code is in a normal build. The
builder item `BT_INQ_PROBE` (bit 280, dropped 2026-10-20: its stack is `BT_AUDIO`'s) built a probe
that, from the CDC console, scans, pages a Raspberry Pi, sets up the link, and reads its A2DP record. Pairing and
encryption (the step before audio) are not done. Nothing here plays audio.

Goal of the whole line: FM-1 -> AirPods over A2DP (SBC), with our own GPL-3.0 BR/EDR stack and no JieLi library.

## 1. How the hardware works

Written clean-room from the Bluetooth Core Specification (Vol 2 Part B) and measurements; the implementers did not
read vendor code. Section numbers below are cited by the code as `BR §n` (`firmware/hal/fm1_bredr.h`,
`firmware/src/bt/*.c`), so they are kept stable. Tags: **[S]** from the spec, **[M]** measured (emulator trace of
stock V15, or the FM-1), **[I]** inferred, not proven.

- The BR/EDR engine is a **frame engine driven by SRAM control blocks**, like the LE engine. One 4,196-byte table
  holds a 16-entry exchange table, four hop tables, four 180-byte frames and RX buffers. `0x2FC44` holds its address;
  every pointer in a frame is a 16-bit offset from it.
- A **frame** is one procedure on one piconet: access code, clock offset, procedure code, two ping-pong TX buffers,
  two RX buffers, RF power/gain, AFH table, key.
- The **hardware** does per-slot work: hop selection, access-code correlation, header, CRC, FEC, whitening, SEQN/ARQN,
  inquiry/page ID trains, E0 encryption [I, strong]. **Software** runs the procedure state machines (in the RX
  interrupt and an 18-slot alarm), builds the FHS/EIR, tracks clock offsets, switches the A/B train, and time-shares
  the radio by swapping frames.
- Stock V15 is dual-mode: in the emulator it alternates inquiry scan (name "FM-1", class `0x240404`) and page scan
  every 0.5 s [M]. The emulator models only the slot clock, the alarm and the slot timers; it has no BR/EDR engine.

### 2. Registers

All 32-bit. Base `0x20000`; the shared BT block is `0x2FC40..`.

| Addr | Meaning |
|---|---|
| `0x20000` | control: bit8 engine run; bit31 stop request [I]; bit10 set on scan suspend; bit11 after an EIR. Init: `0x107`, then bit31, bit8 |
| `0x2000C` | IRQ 40 enables: bit1 RX/frame, bit9 alarm, bit11, bit2. Init `0xFFFF0802` |
| `0x20010` / `0x20018` | IRQ 40 pending / acknowledge (write 1): bits 0, 1, 2, 9, 10, 11 |
| `0x2001C` / `0x20020` / `0x20024` | clock latch (write 1, poll to 0), CLKN in slots (bits [27:1], 625 us, wraps at 2^27), us inside the slot |
| `0x20090 + 12t` | AFH channel map t (three words; word 2 [31:24] = channels used) |
| `0x200C0` / `0x200C4` | EIR TX ([31:20] length, [19:16] type, [15:0] offset) / EIR RX (offset, bit31 armed) |
| `0x200E4` | alarm: the CLKN slot at which IRQ 40 bit9 fires (write only; arm = ack bit9, write CLKN + n, enable bit9) |
| `0x20120..0x20168` | two SCO/eSCO port groups; init values only, not needed for ACL |
| `0x2FC40` bit10 | BR/EDR block enable. `0x2FC44` table base. `0x2FC88` PLL channel table (shared with BLE). `0x2FD8C` AGC readback |
| `0x2FD40` | slot timers (IRQ 41): bits 0-7 enable, write bit 8+k to clear, bits 16-23 pending; `0x2FD44 + 4k` the slot. Init `0xFF00` |

The init list (BR §10.2, in `fm1_br_init`, `fm1_bredr.h`): `0x2FC40` |= `0x400`; zero the table; write `0x2FC44`;
`0x2000C` 0 then `0xFFFF0802`; `0x20018` `0xFFFF`; `0x2002C` `0x140`; `0x20058` `0x4000`; the eSCO port words;
`0x20000` `0x107`; `0x20080` `0x11480A48`; `0x2FC48` `0xAD`; `0x2007C` `0xA04`; `frq_table[0]`; `0x20000` bit31 then
bit8; slot timers `0xFF00`; IRQ 40 and 41 at priority 2. It runs after the BLE radio start-up (`fm1_ble.h`), which
Classic shares (RF front end, calibration, AGC).

Interrupts: IRQ 40 = RX (bit1) and alarm (bit9); IRQ 41 = slot timers. No per-slot software interrupt exists in
inquiry, page or scans. Our probe runs both at priority 5 (above the audio); change the ICFG priority field only,
because rewriting the vectors after boot faults (the RAM top is write-guarded).

### 3. The table (`bredr_table`, 4,196 B)

| Offset | Size | Field |
|---|---|---|
| 0 | 16 x u16 | exchange table |
| 32 | 4 x 80 B | `frq_table`: [0] = basic hopping, [1-3] = AFH |
| 368 | 4 x 180 B | frames (frame n at 368 + 180 n) |
| 1088 | 2 x 2 x 170 words | ACL RX buffers |
| 3808 / 3936 / 3956 | 80 / 20 / 240 B | eSCO RX, FHS RX, EIR RX buffers |

Frames must lie below offset `0x800`; TX/RX buffers within 32 KB of the base.

**3.1 Exchange table entry:** [10:0] frame offset, bit12 inhibit (1 = closed), [15:13] eSCO flags. Enabling a frame
writes its offset into all 16 entries; software never puts two frames in at once. An odd/even flag chooses open
entries (0 = even open; 1 = odd; 2 = all). Inquiry and page use even; a slave connection uses its clock offset's parity.

### 4. The frame (`struct fm1_br_frame`, 180 B, offsets asserted in `fm1_bredr.h`)

| Off | Field | Meaning |
|---|---|---|
| 0 | `bdaddr[3]` | address whose access code and hop sequence the frame uses (LAP in bytes 0-2, UAP 3, NAP 4-5). Inquiry: GIAC `0x9E8B33`; page: the remote; page scan: our own; connection: the master's |
| 6 | `bch_lo/mid/hi` | the 34 parity bits of the sync word (software computes them, §5.3) |
| 12 | `miscntl` | [2:0] LT_ADDR; bit3 EDR types; [7:6] hop table (0 basic, 1-3 AFH) |
| 13 | `format` | [4:0] procedure code (§5.1); bit6 AFH; bit7 A/B train |
| 14, 15 | `rxtog`, `txtog` | bit0 = the buffer the engine uses next; the filled RX buffer is `rxtog ^ 1` |
| 16 | `wincntl` | receive window (inquiry 70, page 80, scans `0x8022`) |
| 18 | `rxmaxbuf` | `0x300` |
| 21, 23 | `txfreq`, `rxfreq` | hop index (software writes it only in fixed-frequency test mode) |
| 24, 28 | `txptr[2]`, `rxptr[2]` | buffer offsets; bit15 of an RX pointer is a flow bit; null = `0x8000` |
| 32 | `linkcntl[2]` | per-TX-buffer control (§5.2); default `0x4010` |
| 36 | `txphdr[2]` | payload header: [1:0] LLID, bit2 FLOW, [12:3] length; default `0x6007` |
| 40, 44 | `rxstat[2]`, `rxphdr[2]` | RX status (§5.4), received payload header |
| 48, 52 | `clk_offset`, `rx_clkn` | 28-bit clock offset in slots (frame clock = CLKN + offset); CLKN latched at the last RX |
| 56, 58 | `bitoff`, `rxbit` | slave timing, us |
| 60 | `cryptk[8]` | Kc' |
| 84, 86 | `acldmpriocntl`, `escodmpriocntl` | radio-arbitration priority (`0x1414`, `0x1D1D`) |
| 88 | `bufcntl` | bit8 = master role; default `0x202C` |
| 90, 92 | `txbufNcntl`, `rxbufNcntl` | bit0 = "TX empty" (software clears it to send) / "RX full" (software clears it to re-arm) |
| 108-143 | `TXPWER`, `RXGAIN0`, `TXSET`, `RXSET`, `PLL_COMP`, `mdm_set`, `RSSI0-3`, `anl_out` | the radio words, the same as the LE block's. TX power = level + 16 (0-11); RX gain = AGC step 0-15; `PLL_COMP` = -(offset x 2^20) / 24,000 |

### 5. The packet engine

**5.1 Procedure codes (`format` [4:0]):** 2 connection master; 3 connection slave; 4 page; 5 page scan; 6 master page
response (send FHS); 7 slave page response; 8 inquiry; 11 inquiry scan. No others are used.

**5.2 `linkcntl`:** [3:0] packet TYPE (0 NULL/ID, 1 POLL, 2 FHS, 3 DM1, 4 DH1, 10 DM3, 11 DH3, 14 DM5, 15 DH5);
bit7 TX encryption; bit14 RX interrupt enable (set on every normal write, cleared while the RX handler reads);
bit15 RX decryption. Bits 4, 5, 8, [13:9] are not understood (§12, U6).

**5.3 Sync word:** the engine builds it from the LAP (`bdaddr`) and the 34 parity bits (`bch`). The parity follows the
spec construction (Vol 2 Part B 6.3.3), checked against two vectors: GIAC `0x9E8B33` -> `E2 3A 1A 33 02` and V15's own
LAP `0x1D994C` -> `08 D9 39 52 02` [M]. Code: `firmware/src/bt/bt_inq_core.c`, test `tests/bt_inq_test.c`.

**5.4 `rxstat`:** [3:0] received TYPE; high byte `0x8F` = a good new packet, `0xAF` = a good repeat (retransmission);
`0x83` = sync found but header or payload rejected; `0xB1` = header or sync failed [I].

**5.5 Hardware vs software:** access code, header, ARQN/SEQN for ACL, CRC, FEC, whitening: hardware. Software: the
payload header, the FHS (18 bytes, spec layout; the engine fills the clock field), the EIR. **TX hand-over:** write
`txptr`, `txphdr`, `linkcntl`, then clear `txbufNcntl` bit0 with interrupts off. **RX re-arm:** clear `rxbufNcntl`
bit0 and set `linkcntl` bit14. Payload sizes (max bytes): DM1 17, DH1 27, DM3 121, DH3 183, DM5 224, DH5 339.

### 6. Clock and timing

**6.1** CLKN counts one per 625 us. A page uses the remote's clock estimate (its FHS CLK minus our `rx_clkn`) **plus 1 slot**
(the FHS is heard in our odd RX slot and lacks CLK1; the probe's default, option `padj`): at the Pi's default page
scan the page connected on the first answer 4 of 4 times; +0, +2, -2 and +4 failed at the POLL step [M]. Without an
estimate (both trains) it connects too. The exchange entries follow the estimate's parity during the page; after the
FHS the piconet takes our own clock (offset 0) and the even entries.
**6.2** IRQ 40 handler order: ack bit0; if bit1, ack and run each frame's RX handler whose `rx_clkn` changed; ack bit10;
if bit9, ack and run the alarm handler; ack bit11, bit2.
**6.3** Slot timers (IRQ 41) serve timeouts and the scheduler.
**6.4** Alarm: arm n slots ahead: 18 after a (re)start and as the watchdog, 8 after an FHS, 16 after a page-response
ID, 32 after the FHS ack, 512 after an inquiry result. The alarm handler switches the A/B train and steps scan states.
**6.5** Optimist's audio ISR can hold CPU 0 for ~4.9 ms, so IRQ 40 must outrank it (priority 5 in the probe) or run on CPU 1.

### 7. Hopping and AFH

`frq_table[0]` maps the hop kernel's output (hardware) to an RF channel: indices 0-39 = even channels 0..78, 40-78 =
odd 1..77. The A/B train is flipped by software via `format` bit7: inquiry every 4,096 slots, page every 2,048. A
crystal-error sweep exists in the vendor library (9 steps of `PLL_COMP`: -120, -80, -40, -20, 0, +20, +40, +80, -120);
the probe uses it. AFH: three tables and register triples; set `miscntl` [7:6] = t+1 and `format` bit6.

**AFH as the audio link's master (BT_AFH, `bt_afh.c`, `bt_page.c` bp_sec_tick / bp_afh_send / bp_alarm):** the link
counts the device's good answers per RF channel (`rxfreq` is the hop index: 0-39 the even channels, 40-78 the odd
[M: the inquiry's answers sit on 18 consecutive indexes, the kernel's consecutive bank entries]); every 30 s the
channels under half the used channels' mean leave the map, the worst first, never back within the link, at least 20
stay (Part B 2.6.4.2). The map goes to `frq_table[1]` as the used channels packed in the basic order and to the map
registers `0x20090` and `0x2009C` (bits 0-78, the count in word 2 [31:24]) [I: the LE block's packed table and count,
`ble_hw_wl82.c` cb_channels, taken as the model; `btpage afh=2` lays the table out in full instead, every kernel output
mapped itself, should the engine index it that way]; `LMP_set_AFH` (the instant an even slot 320 slots ahead, mode 1,
the map) is queued and the 18-slot alarm is re-armed onto the instant, where `miscntl` [7:6] = 1 and `format` bit6 are
set. Why (2026-10-23, logs/xm4-2026-10-23.log): a WH-1000XM4's answers on RF channels 4, 10 and 17 fell from ~80 to
9-29 per 30 s as the radio warmed while the other 76 stayed flat, the stream stalled on them and crackled; every phone's
controller skips such channels. Hardware-untested at the time of writing.

### 8. Inquiry, page, scan as the probe drives them

**8.1 Inquiry:** frame with GIAC address and parity, `format` 8, `wincntl` 70, ID (`linkcntl` `0x4000`), EIR RX armed
(`0x200C4` bit31), even entries, alarm 18, AGC step starts at 8 and follows RSSI1. On RX: status `0x8F` + TYPE 2 = FHS
(parse it, remote clock offset = remote CLK - `rx_clkn`); if the FHS says EIR follows, the next DM/DH packet is the
EIR. Stop at 10.24 s.
**8.2 Inquiry scan** (what V15 does, and our `btscan`): `format` 11, `wincntl` `0x8022`, TX = our FHS, EIR via
`0x200C0`. The engine answers an ID with the FHS by itself.
**8.3 Page:** LT_ADDR 1-7 reserved; clock estimate (§6.1); `format` 4, `wincntl` 80, `linkcntl` `0x4002` (FHS) and
`0x4001` (POLL). Sub-states on the RX interrupt: slave ID -> send FHS (`format` 6, alarm 16); second ID -> own address,
POLL (`format` 2, alarm 32); slave reply -> connection (`bufcntl` bit8 master).
**8.4 FHS** (18 bytes, spec layout): 34 parity bits, LAP, EIR bit, SR, UAP, NAP, class of device, LT_ADDR, CLK[27:2].
**8.5 Connection:** master frame stays in `format` 2; ACL goes out as DM1 fragments through the TX hand-over; LLID 3 = LMP.

### 9. Encryption, coexistence, test mode

E0 runs in the engine: load Kc' into `cryptk`, set `linkcntl` bit7/15 on both buffers. Key generation (SAFER+
E1/E21/E22/E3) is software. The vendor stack uses legacy SSP with **P-192** and no Secure Connections; ours would do
the same. Both radios (BLE `0x28000`, BR/EDR `0x20000`) have a priority word and share `0x2FC40`, the PLL table and
the AGC; no software scheduler arbitrates them for us (not tested, U12). Fixed-frequency test mode: `format` 2/3,
`miscntl` 7, `linkcntl` bit5, `txfreq` = `rxfreq`, `0x20000` bits 15, 16, 19.

#### 9.1 BLE MIDI and Bluetooth audio at the same time (2026-10-23)

Before: one at a time. Choosing an AUDIO mode switched BLE MIDI off (`bta_start` called `ble_midi_set(0)`), and the
BR/EDR bring-up ran the radio's start-up again (`fm1_ble_rf_init`, guarded only by its own flag) and, with `o_ble`
on (the default), `fm1_ble_bb_init` with a 64-byte dummy window, which moves `0x2FC84` off BLE's control blocks and
clears BLE's interrupt enables (`0x28028`, `0x2804C`). So MIDI after AUDIO in the same boot was probably broken too
[I].

Now (`firmware/src/ble/rf_share.h`, one byte of RAM):

- **One start-up per boot**, by whichever side needs the radio first (`rfs_rf_up`): BLE's `ble_hw_wl82_start` and
  BR/EDR's `bti_radio_up` both go through it; the second finds it up and starts nothing (no retune under the other's
  link). The trims are the same VM set either way (BLE may use Optimist's copy of it).
- **The LE block is BLE's.** BR/EDR's "LE enable beside BR/EDR" (`bti_le_up`, BR §14) runs only while BLE has not set
  its own window (`rfs_le_br`); over a live BLE it is skipped (the block is enabled already). BLE after BR/EDR always
  runs its `fm1_ble_bb_init` (`rfs_le_ble`): its window replaces the dummy one, which never held a link.
- **The BR/EDR init after BLE** (`fm1_br_init`) still writes its own registers, among them `0x2FC48` = `0xAD` (BLE's
  start-up wrote `0xB9`): stock V15 runs BLE with `0xAD` after its BR/EDR init, so this is stock's dual-mode state
  [I], but our BLE has not been measured with it.
- **The screen:** two pages, BT MIDI and BT AUDIO, each its own MODE, connect and timer (`ui/optimist/op_bt.c`);
  `bta_start` leaves BLE MIDI alone.
- **Air-time arbitration** (the priority words, `bt_inq.c` `BTI_ACL_PRIO`): stock's values kept, so the order is
  stock's: eSCO 29 > BLE connection event 28 (30 around an instant) > BLE advertising 26 > BR/EDR ACL 20 > BLE
  scanning 17. BLE MIDI wins a slot both want: a connection event is a packet or two per 7.5-30 ms and has no second
  chance before the next interval, while an A2DP packet lost to it is resent by the baseband's ARQ and the SBC ring has
  room. Which number wins (higher or lower) is inferred from stock's use, not measured.
- **Interrupts:** BLE's two at priority 2 (its engine is hardware-timed), BR/EDR's 40/41 at 5 during a run (a page
  answer must be reprogrammed within the slot): a BR/EDR interrupt may pre-empt BLE's handler, never the reverse.
- **Longer BLE packets:** the screen mirror's peripheral now asks for data length extension (LL_LENGTH_REQ, up to 251
  bytes, about 2.1 ms a packet at 1M) after 6 events (`ble_ll.c`, a separate change). A long BLE packet takes more
  air per connection event while A2DP streams: more ACL slots lost to BLE, more ARQ resends of SBC.

Untested on an FM-1 (U12 stays open): both links up at once (AirPods playing + a phone or a Mac on BLE MIDI); whether
the engines' arbitration actually follows the priority words; crackles or SBC ring underruns from ACL slots lost to
BLE; BLE supervision timeouts while a BR/EDR inquiry (10 s, the AUDIO devices list) or a page runs; the `0x2FC48`
value under BLE; BLE first then audio, and audio first then BLE, each in one boot. Host-tested only:
`tests/bt_coex_test.c` (the bring-up order) and the two pages (`tests/ui_optimist_ble.h`, build with BLE and
BT_AUDIO).

### 10. RF

RF start-up, calibration and the per-frame radio words are the BLE driver's (`firmware/hal/fm1_ble.h`). `0x2FD8C` gives
the inquiry RSSI; `RSSI1` drives the AGC (step +1 below 800, -1 above 1,100). No switch for the BR modulation index or the
EDR modem was found; `mdm_set` is 0 in V15 (U7).

### 11. Stock V15 in the emulator

Init as §10.2 at 0.159 s; from 0.38 s every 0.5 s an inquiry scan (window about 14 ms) and a page scan (about 20 ms).
Frame 0 = inquiry scan (FHS + EIR "FM-1", class `0x240404`), frame 1 = page scan with its own address (generated per
unit). A second FM-1 on stock V15 is therefore a possible inquiry target (unverified on hardware, U9).

### 12. Unknowns

| # | Unknown |
|---|---|
| U2 | exchange-table indexing (slot mod 16?) and odd/even semantics |
| U3 | `wincntl` units and bit15 |
| U4 | `0x20000` bits 0-2, 10, 11, 30, 31; `0x20010` bits 0, 2, 10, 11 |
| U5 | does the engine send the page-response FHS itself, or must IRQ 40 do it in time |
| U6 | `linkcntl` bits 5, 8, [13:9]; `rxstat` bits 4, 7, 9; `bufcntl` bits |
| U7 | BR modulation index / EDR modem selection (`mdm_set`) |
| U8 | `PLL_COMP` on a real unit (RX quality is the weak point, §14) |
| U9 | whether stock V15 on hardware is discoverable as "FM-1" |
| U10 | whether AirPods accept P-192 legacy SSP + E0 (the whole A2DP plan hangs on it) |
| U11, U12 | IRQ 40 cost during a connection; coexistence with our BLE link (built 2026-10-23, §9.1; not run on an FM-1) |

## Where the code is

| File | What |
|---|---|
| `firmware/hal/fm1_bredr.h` | registers, frame struct, init list, clock, alarm, slot timers, IRQ 40/41 entries |
| `firmware/src/bt/bt_inq.c`, `bt_inq_con.c` | stage 1: radio and clock check (`btclk`), inquiry (`btinq`), inquiry scan (`btscan`), `bttrace`, `btrx`; the console half is `bt_inq_con.c` |
| `firmware/src/ble/rf_share.h` | the radio shared with BLE: one start-up, the LE block BLE's (§9.1; host test `tests/bt_coex_test.c`) |
| `firmware/src/bt/bt_inq_core.c` | portable: sync-word parity, FHS parse/build, EIR name (host test `tests/bt_inq_test.c`) |
| `firmware/src/bt/bt_page.c`, `bt_lmp.c` | stage 2: `btpage ADDR` pages as master, minimal LMP (host test `tests/bt_lmp_test.c`) |
| `firmware/src/bt/bt_acl.c`, `bt_l2cap.c`, `bt_avdtp.c` | stages 3-4: L2CAP over DM1 fragments, an SDP client, an AVDTP initiator (host tests `bt_l2cap_test`, `bt_avdtp_test`) |
| `firmware/src/bt/bt_safer.h` | API header only (SAFER+, E1/E21/E22/E3, SSP f1-f3/g, P-192); **no `bt_safer.c` exists and `tests/bt_crypto_test.c` does not exist** |
| `tests/bt_inq_emu_test.py` | stage 1a in the emulator: 1,599.5 slots/s, 264 alarms of ~267 |

## 14. What was measured on an FM-1 (2026-10-18, against a Raspberry Pi `D8:3A:DD:DE:2A:93` about 1 m away)

- **1a (clock):** CLKN 1,600 slots/s, alarms and slot timers as §2 and §6 say, no unknown IRQ bit.
- **Inquiry scan works:** the Pi listed "OPTIMIST BT" (class `0x240404`, RSSI -64 dBm): our sync word, FHS and EIR are
  right on air, and the engine answers IDs by itself.
- **Inquiry works:** FHS and EIR received from nearby devices. The hop tables, even-entry parity and GIAC code are
  confirmed (`frq=+-2` or `odd` lose everything).
- **RX quality is the weak point.** Many receptions end `0x83xx` (payload rejected) or `0xB1xx` (header/sync failed).
  `PLL_COMP` beyond +-300 loses sync; -120 to -180 was best; the library's 9-step sweep (default on) finds devices each
  run. Enabling the LE block (`0x2FC80` bit0, `0x28000` and the LE timing words) helped in one run and made no
  measurable difference in an A/B at run priority 5. It stays on by default; `noble` skips it.
- **Page connects.** Master, LT_ADDR 1: the Pi's ID, our FHS, its second ID, our POLL, its NULL. The link ran 40 s on the
  engine's own POLL/NULL (about 22 good packets a second, 3 % bad) until we sent LMP_detach (the Pi logged reason 0x13).
- **LMP works:** features, version (the Pi is LMP 5.0), host connection, setup complete both ways, name request answered.
- **SDP works:** ServiceSearchAttribute for 0x110B returned the Pi's 83-byte record: PSM `0x0019`, AVDTP 1.3, A2DP 1.4,
  "Audio Sink".
- **AVDTP is blocked by security:** L2CAP to PSM 0x19 is answered pending (authentication), then `0x0003` security block.
  Our features claimed no SSP or encryption, so the Pi's controller fell back to a PIN, had none, and failed (HCI Auth
  Complete 0x06).

## 15. Status and next steps

State at HEAD (`3242a360`): the SSP skeleton is committed. `bt_lmp.c` `btl_features` now advertises encryption and SSP
(byte 0 bit3; byte 2 bits 0 and 5), and `btl_answer` has placeholder handlers for IO capability (NoInputNoOutput, Just
Works), the P-192 key exchange, confirm/random, start-encryption and link-key request. They return dummy values or
NOT_ACCEPTED: **a pairing attempt will fail at the placeholders.** The crypto they call is not written.

Next, in order: (1) SAFER+ and E1/E21/E22/E3 (`bt_safer.c`, tested against Core Vol 2 Part G section 10); (2) SSP with
micro-ecc P-192, HMAC-SHA-256 for f1/f2/f3/g; LK = f2(DHKey, N_central, N_peripheral, "btlk", BD_ADDR_C, BD_ADDR_P),
all in LMP air order (LSB first); (3) E0: `cryptk` + `linkcntl` bits 7/15 (master RX decrypt on at
START_ENCRYPTION_REQ, TX on at ACCEPTED); (4) AVDTP configure, open, start; (5) the SBC encoder and the audio ring;
(6) then AirPods (page scan for reconnects, AVRCP taps). Do not flash an FM-1 without asking.

## 16. The A2DP plan: sizes, codecs, latency

Estimates [I] unless marked [M] (measured: link map, emulator instruction count, host build).

**Stack size (own code, one link, master role, SBC only):** about 36-58 KB of flash, 3-6 KB of main RAM and 4-8 KB of
pool buffers. By layer: baseband driver 6-10 KB, inquiry/page/scan 3-5, LMP subset 8-12, pairing and encryption
(micro-ecc P-192 3.7 KB [M], SHA-256 1.1 KB [M], SAFER+ about 2.9 KB) 7-9, L2CAP 2-4, SDP 1-2, AVDTP + A2DP source 4-7,
glue 1-2, UI 2-4; AVRCP +2-4 optional. The probe build today costs 33,364 B of flash, 2,304 B of RAM and 14,848 B of
pool (`tools/builder/costs.json`, `BT_INQ_PROBE`). For scale, `BLE` costs 32,116 B flash and 6,640 B RAM. Effort for
interoperability across headphones: 9-18 person-months.

**Why encryption is not optional:** SSP devices use security mode 4; every L2CAP service but SDP needs an encrypted
link. AirPods need SSP + E0. Secure Connections (P-256 + AES-CCM) is not needed if we do not advertise it.

**Rejected route:** the JieLi SDK's closed `btctrler.a` + `btstack.a` (about 165 KB of flash, 40 KB of RAM, closed, not
shippable under GPL-3.0).

**Codecs (AirPods take SBC or AAC; no aptX):** measured with JieLi clang on the emulator, instructions per second as a
share of the 360 MHz core (lower bounds: no cache or flash waits):

| Codec | Flash (code + tables) | State RAM | CPU | Licence |
|---|---|---|---|---|
| SBC (Bluedroid), bitpool 53, 328 kbit/s | 5.5-6.2 KB | 1.7 KB | **3.1 %** | Apache-2.0, fits GPL-3.0 |
| AAC-LC (vo-aacenc), 256 kbit/s | 60-65 KB | 49 KB (pool) | 17-20 % | Apache-2.0; AAC patents separate |
| aptX / aptX HD | 9-10 KB | 4.4 KB | 24-25 % | LGPL via libfreeaptx; AirPods cannot decode it |

SBC fits beside the audio ISR on CPU 0 (the ISR takes up to ~85 % of each half). AAC and aptX do not fit there; they
need CPU 1 (see `DUAL-CORE.md`, which now renders parts 2 and 3) or a lighter patch. fdk-aac is GPL-incompatible;
FAAC needs 254 % of the core. An SBC frame is 119 bytes; basic-rate DH5 carries up to 723 kbit/s, so EDR is not needed.

**Fitting it:** about 50 KB of flash and 4-9 KB of RAM would have to be freed from the synth or user-default profile
(drop the X0X 909 kit, FM6 MARK I and halve the delay, or the web editor, BLE device scanning and COMP+LIMIT). These
figures date from 2026-10-18 and were not re-checked against today's profiles.

**Latency:** FM-1 side about 12-25 ms with SBC (output buffer 5.8-11.6 ms, framing, retransmission slack). The AirPods'
own buffer is 126-274 ms from Apple devices and similar or worse from others. Expect 150-300 ms end to end: fine for
listening and practice, unusable for playing keys or pads.

Scratch material (IR dumps, traces, run logs, flash list) lives in `~/GitHub/optimist-work/btclassic/` and
`~/GitHub/optimist-work/btaudio/`; the pairing hand-off notes are `btclassic/PAUSED.md` and
`btclassic/SSP-INTEGRATION-GUIDE.md`.

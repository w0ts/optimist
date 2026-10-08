# Bluetooth audio on the FM-1: feasibility study

Status: analysis and measurement only. Nothing was flashed or run on hardware, and no firmware was changed.
Date: 2026-10-08. Companion to [BLE-MIDI-FEASIBILITY.md](BLE-MIDI-FEASIBILITY.md) (§9 there: routes A/B/C for BLE MIDI).

Scope: could Optimist on the FM-1 (JieLi AC791N / WL82, dual-core pi32v2) do Bluetooth audio, in which direction,
and how? Classic A2DP (SBC) and LE Audio (LC3, CIS/BIS, Auracast) are both covered.

Tags:

- **[M]** measured in this study: a link map, an emulator instruction count, a disassembly, or a byte search of the
  stock image. The text says which.
- **[I]** inferred or estimated, not measured. Treat it as a hypothesis.
- **[S]** published source, with a URL in §10.

The scratch files of this study (SDK build copies, benchmark sources, maps) are throwaways and are not part
of this repository. The method is in §9 so that the numbers can be reproduced.

---

## 0. Summary

| Question | Answer |
| --- | --- |
| Can the chip do Classic Bluetooth audio? | Yes. The WL82 controller library has BR/EDR with EDR (2-DH5, a "3 Mbit/s ACL" switch) [M]. The SDK host has A2DP source and sink, AVDTP, AVRCP, HFP, SDP and RFCOMM [M]. The chip has a **hardware SBC decoder**; its hardware encoder does mSBC only [M]. |
| Does stock V15 do Classic audio? | Its image contains an **A2DP Sink SDP record ("JL_A2DP")**, the SBC hardware driver and an `sbc_encoder` task name [M, byte search]. Whether a phone can connect to it is untested; the M-VAVE spec lists only BLE MIDI [S]. |
| Can it do LE Audio? | The controller library has CIS (both roles) and BIG-creation code [M, IR names], but the SDK has **no LE Audio host (BAP/PACS/ASCS), no LC3, and no demo that enables ISO** [M]. Unproven on this chip. |
| A2DP latency | 100–300 ms end to end with SBC, set mostly by the headphones' buffer [S]. Musicians want ≤10 ms for playing and tolerate up to ~20–40 ms in some setups [S]. **A2DP source is unusable for live playing.** |
| LE Audio latency | Typically quoted at 20–40 ms [S, vendor figures]. Still above the playing threshold. |
| Route A size (SDK, A2DP source only) | BT code in the link: **125.1 KB** (controller 71.7, host 42.4, crypto 6.6, RF 4.4) [M], plus SBC encoder 5–8 KB [M], plus OS/glue ≈35 KB [I] → **≈165 KB**. Static RAM: **≈40 KB** (35.1 KB controller incl. 28.6 KB ACL pools, 4.1 KB host) [M] plus heap and stacks [I]. Optimist has ≈93 KB flash and ≈13.6 KB RAM free without BLE. **It does not fit.** |
| SBC encoder cost | 28,056 pi32v2 instructions per 128-sample stereo frame (Bluedroid, Apache-2.0, `-O2`) [M, emulator] = 9.7 M instr/s = **≈2.7 % of a 360 MHz core**. Code 5.4–6.1 KB [M]. |
| LC3 cost (stereo, 48 kHz, 10 ms, 96 kbit/s per channel) | Encode ≈627,500 instructions per 10 ms = **≈17 % of a core**; decode ≈221,600 = ≈6 % [M, emulator]. Code 28–37 KB plus 86 KB of tables (all rates) [M]. |
| GPL-clean host stacks with A2DP | Zephyr (Apache-2.0) has A2DP source and sink [S]. BTstack is non-commercial only, so it is not GPL-compatible [S]. NimBLE is LE-only. BlueZ is Linux. |
| Recommendation | **Don't build A2DP source into the firmware.** Use an external low-latency transmitter on the audio out, or USB audio, for wireless monitoring. Keep **A2DP sink** (phone → FM-1, play-along and sampling) on file as the only direction where BT audio makes musical sense, and revisit it only after BLE MIDI route B or C works. Do not pursue LE Audio. §8 has the details. |

---

## 1. Use cases, directions and latency

### 1.1 What latency musicians accept

- Wessel and Wright's guideline for musical interaction is ≤10 ms of latency with ≈±1 ms jitter. Jack et al. found
  10 ms acceptable for percussive playing and 20 ms, or 10 ± 3 ms, noticeably worse (both as summarised in
  arXiv 2503.11562) [S].
- Lester and Boley (AES) report acceptable monitoring latency from about 1.4 ms (in-ear) to 42 ms (wedge
  monitors), depending on instrument and monitoring [S, abstract only].
- Mäki-Patola and Hämäläinen: theremin players noticed latency only at 20–30 ms [S].
- Optimist's own path already adds 5.8–11.6 ms (two 256-frame halves at 44.1 kHz, `HALF_FRAMES 256`, `FS 44100` in
  `firmware/src/core/core.h`) [M]. Any wireless delay adds to that.

### 1.2 What Bluetooth audio costs in latency

| Link | Typical latency | Source |
| --- | --- | --- |
| A2DP, SBC | 100–220 ms codec and transport; 300+ ms whole-chain on phones | [S] several vendor pages and SoundGuys tests via a DEV cross-post; figures vary with the sink |
| A2DP, aptX Low Latency | ≈40 ms (Qualcomm's own encoder-side figure) | [S] aptx.com |
| LE Audio (LC3 over CIS/BIS) | 20–30 ms often quoted; Auracast 20–50 ms | [S] vendor pages; not SIG measurements |

Where the A2DP delay comes from [I, from the spec structure and our numbers]: one SBC frame is 128 samples
(2.9 ms at 44.1 kHz). A 2-DH5 packet carries five 119-byte frames, about 14.5 ms of audio. The source side can stay
near one packet. The sink's jitter buffer is the big part, and the **sink**, not the FM-1, chooses it. A source
cannot make SBC headphones low-latency.

### 1.3 The directions

| Direction | Musical use | Latency matters? | Verdict |
| --- | --- | --- | --- |
| **(a) FM-1 → BT headphones/speaker (A2DP source)** | Wireless monitoring while playing | **Yes, critically.** 100–300 ms makes keys, pads and knob moves unplayable | Realistic technically, **useless for playing**. Only usable for listening to a running pattern. An analog aptX-LL transmitter on the headphone out does better (≈40 ms codec) with zero firmware [S/I] |
| **(b) Phone → FM-1 (A2DP sink)** | Play along with a backing track; sample a phone source into the slicer/sampler | **No.** The phone's audio is one-way; its delay only shifts the backing track | **The one direction that makes sense.** Hardware SBC decode exists [M]. Rate matching (phone clock vs. I2S clock) is needed, as USB audio already does [M, `usb_audio_stream.c` ring servo]. Wired alternative today: USB audio playback into Optimist (UAC1 stereo, host → FM-1) [M source] |
| **(c) LE Audio / Auracast** either way | Lower-latency monitoring; broadcast to several listeners | Yes for monitoring | 20–40 ms is still above the playing threshold. The controller has ISO code, but nothing above it exists in the SDK, and LC3 encode takes ≈17 % of a core [M]. **Not realistic for this project** |

---

## 2. Hardware and controller

### 2.1 BR/EDR and EDR

- `btctrler.a` holds the Classic link controller: `lmp.c.o` (603 KB of bitcode), `bredr_link.c.o` (394 KB),
  `bredr_frame.c.o`, `bredr_RF.c.o`, `bredr_slot_timer.c.o`, `lmp_hci*`, `esco_codec.c.o` [M, archive members].
- **EDR:** the controller API has `PKT_TYPE_2DH5_EU` and `bredr_link_vendor_support_packet_enable()`
  ("configure whether TX supports a packet type, supported by default"), and `set_bt_data_rate_acl_3mbs_mode()`
  (`include_lib/btctrler/btcontroller_modules.h:270-288`) [M]. The LMP debug info has a packet-type-table field
  `ptt` with `link_conn_set_ptt`/`lc_write_ptt` [M, IR names]. So 2 Mbit/s EDR is supported and 3 Mbit/s is
  switchable [I from names; no over-the-air test].
- The SDK config chooses the modules through `config_btctler_modules = BT_MODULE_CLASSIC | BT_MODULE_LE` when both
  are enabled (`apps/common/config/log_config/lib_btctrler_config.c`) [M]. Dual mode is a supported configuration.
- Encryption: the IR has `ENCRYPTION_BREDR_E0_LE_AES_CCM` and `__write_reg_encry`, so BR/EDR E0 appears to be
  in hardware [I].

### 2.2 LE ISO (CIS/BIS) and LE Audio

- `btctrler.a` has `ll_cis.c.o`, `ll_cis_master.c.o` (125 KB bitcode), `ll_cis_slave.c.o`, `ll_common_iso.c.o`,
  `ll_bis.c.o`, `ll_bis_slave.c.o` (54 KB) and `ll_bis_master.c.o` (3.8 KB, **one function**) [M]. Many internal
  names begin with `lctr`, Packetcraft/Arm Cordio style [M names; provenance I].
- BIG: `ll_bis_slave` defines `llcp_slv_create_big` and `hci_le_create_big_complete_event` (the broadcaster's
  "Create BIG Complete" event) and `ll_bis_setup_iso_data_path` [M]. No function name for BIG **sync** (receiving
  a broadcast) was found in any member [M, name search]. So Auracast **transmit** code may exist; receive was not
  found [I].
- `include_lib/btctrler/ble/ll_config.h` defines `LE_FEATURES_CIS`, `LE_FEATURES_BIS`, `LE_FEATURES_ISO`,
  `LE_2M_PHY`, `LE_CODED_PHY` [M]. **No app or demo in the SDK sets them**; the only other hit is a datatype header of
  the RF FCC tool [M, grep of `apps/`]. The shipped config uses `LE_ENCRYPTION` only.
- Host side: `btstack.a` has no BAP, PACS, ASCS, BASS, CAP or ISO member [M, member list]. There is no LC3 in any
  SDK library [M, archive search]; the SDK's own low-latency codec is JieLi's `jla_codec_lib.a`, which is proprietary.

**Conclusion:** LE Audio on the WL82 would mean an unproven controller feature, plus a whole host profile stack,
plus LC3. Nobody has shown it working on this chip in public that we know of [I].

### 2.3 Audio codec hardware on the WL82

- `WL82.h` defines a `JL_SBC` block at `0x13D00` (`CON0`, `DEC_SRC_ADR`, `DEC_DST_ADR`, `DEC_PCM_WCNT`,
  `DEC_INBUF_LEN`, `ENC_SRC_ADR`, `ENC_DST_ADR`, `DEC_DST_BASE`) and `IRQ_SBC_IDX 46` [M].
- The SDK driver (`lib/driver/cpu/wl82/audio/sbc.c`, 930 B linked) uses it [M]:
  - **Encode is mSBC only.** The disassembly of `sbc_codec_encode_frame` checks for 240 PCM bytes in and a 58-byte
    frame out, the mSBC (wideband speech) sizes [M].
  - **Decode is general SBC.** In the A2DP sink build (§3.1, e1) the decoder links only the thin `sbc_decoder`
    wrapper (≈1 KB) and no software synthesis filter [M], so A2DP SBC decoding runs in hardware [I, strongly
    suggested].
- The A2DP **source** SBC encoder is software: `sbc_analyze_eight_simd`, `sbc_calc_scalefactors_j`,
  `sbc_encoder_process_input_s8_internal` (8.2 KB in the e2 build) [M]. Those are BlueZ libsbc's function names [M
  names; provenance I].
- There is no LC3 hardware.

### 2.4 Stock V15 (`fm1-firmware/FM-1.fwsc`)

Byte search of the decrypted application (`app.bin`, loaded at `0x02000120`) [M]:

| Finding | Where | Meaning |
| --- | --- | --- |
| SDP record: ServiceClass **AudioSink 0x110B**, L2CAP, **AVDTP 0x0019 v1.3**, profile **A2DP 0x110D v1.3**, features 0x0001, name **"JL_A2DP"** | `0x02073FA0`–`0x02073FED` | The SDK's `sdp_a2dp_service_data` (sink), which `bt_profile_config.c` registers when `USER_SUPPORT_PROFILE_A2DP=1` [M bytes; registration I] |
| No AudioSource 0x110A, no AVRCP 0x110C/0x110E, no SPP 0x1101, no HFP 0x111E/0x111F | — | No source, remote-control, serial or hands-free records [M] |
| 5 loads of `0x13D00` (the SBC block) | around `0x0203E370` | The SBC hardware driver is linked [M] |
| Strings `sbc`, `msbc`, `cvsd`, `opus`, `audio_server`, `audio_encoder`, `sbc_encoder`, `btencry` | `0x0204E2xx`, `0x02055059` | SDK audio server and task table entries [M]; whether the tasks run is unknown |

So stock V15 is a dual-mode SDK build that **contains** an A2DP sink. With earlier findings (stock registers
BR/EDR IRQs 40/41 and appears to enable page/inquiry scan, BLE-MIDI-FEASIBILITY §9.2 and this study's brief) it
might accept a phone's A2DP connection. **Untested.** The M-VAVE spec lists only BLE MIDI [S]. Hardware check: §8
step 1.

---

## 3. Routes

### 3.1 Route A: the SDK stack (btctrler + btstack with A2DP), measured

`apps/demo/demo_edr` is the SDK's Classic demo: A2DP sink (`bt_music.c`, `bt_decode.c`), A2DP source
"emitter" (`bt_emitter.c`, with inquiry and name filters), SPP, HFP-AG [M]. It was built with the JieLi clang 4.0.1
LTO link (`-Oz -flto`, the SDK Makefile), as in the BLE-MIDI study [M].

| Variant | What | `.text` (flash) | `.ram0_data` | `.ram0_bss` |
| --- | --- | ---: | ---: | ---: |
| e0 | `demo_edr` unchanged (source + sink, HFP-AG, SPP, AAC, AEC, DNS, debug log), 4 MB flash config | 565,600 | 21,684 | 57,956 |
| e1 | A2DP source + sink, AVCTP, SBC enc/dec only, no HFP/SPP, debug off, 1 MB config | 318,528 | 13,096 | 57,612 |
| **e2** | **A2DP source only** (`USER_SUPPORT_PROFILE_A2DP 0` → `a2dp_profile_support = 2`, AVCTP 0, SBC encoder only) | **314,496** | 13,084 | 57,608 |

All [M] from `objdump -h`. Bluetooth's share of e2, attributed through DWARF line rows as in the BLE study [M]:

| Part | Flash (B) | Static RAM (B) |
| --- | ---: | ---: |
| Controller (`btctrler`, incl. `.classic_rf_const`, `.classic_lmp_auth_const`) | 71,665 | 35,102 (`BTCTLER_RAM_TOTAL`), of which **28,560 are the ACL pools** (`acl_tx_pool` 10,752 + `acl_rx_pool` 17,808, from `CONFIG_BT_TX/RX_BUFF_SIZE`) and 4,196 `.bd_base` |
| Host (`btstack`: L2CAP, SDP, AVDTP, A2DP source) | 42,442 | 4,110 (`btstack_data` 204 + `btstack_bss` 3,906) |
| Crypto (SSP P-256, AES) | 6,609 | — |
| RF init and trim (`wl_rf_common`, `.rf_trim`) | 4,372 | 1,129 |
| **Bluetooth subtotal** | **125,088** | **≈40,300** |
| SDK SBC encoder (BlueZ-style, software) | 8,208 | — |
| SDK audio server framework (only needed for the SDK's own capture path) | 31,722 | — |

Facts that matter for a port [M]:

- The A2DP source library **pulls already-encoded SBC frames from the application**:
  `a2dp_sbc_encoder_init(sbc_t *)` and `a2dp_sbc_encoder_get_data(packet, len, &frame_size)`
  (`apps/demo/demo_edr/bt_decode.c:852-955`). So Optimist could feed its own encoder and drop the 31.7 KB audio
  server.
- Sink removal saves little: e1 → e2 is −4,032 B of flash. The cost is the Classic controller and host, not the
  profile.
- The ACL pools are linker-sized and could shrink for a source-only build (RX) [I].

**Fit for Optimist** [I]: 125.1 KB of BT + ~6 KB encoder + the OS shim and glue. In the BLE study the gap between
the direct BT buckets and the difference-measured BT share was ≈36 KB (83.9 vs. 47.9 KB, s9 build), so **≈165 KB**
for A2DP source alone. A dual-mode build with BLE MIDI adds the LE controller and ATT parts, probably +25–40 KB
[I]. Optimist's user-default has ≈93 KB flash free without BLE and ≈79.6 KB with BLE, and ≈13.6 KB of RAM free
with BLE (current exact builds) [M, the project's build numbers]. Route A needs about **twice the free flash and
three times the free RAM**. It would be an "A2DP edition" without one or two big features (for scale: PERC → DRUM
saved 67,812 B, ENGINE-PLUGINS §4.1) and with buffers moved into the pool. It also brings the closed-library
licence problem (§6).

### 3.2 Route B: closed controller, open host

The JieLi controller has an in-memory HCI boundary (BLE study §9.3) [M]. A Classic host on top needs L2CAP, SDP,
AVDTP, A2DP and SSP pairing. Open hosts:

| Host | Classic A2DP? | Licence | GPL-3.0-compatible? | Notes |
| --- | --- | --- | --- | --- |
| Zephyr Bluetooth host | Yes: A2DP source and sink samples, HFP, HFP-AG (`samples/bluetooth/classic/`) [S] | Apache-2.0 | Yes | Tied to the Zephyr kernel APIs; porting the host alone onto our shim is real work [I]. Also has LE Audio (BAP broadcast source sample) [S] |
| BTstack (BlueKitchen) | Yes | BSD-style plus "solely for personal benefit … not for any commercial purpose" [S] | **No** (the non-commercial clause is an extra restriction) [I, not legal advice] | Commercial licence by quote |
| Apache NimBLE | No (LE only) | Apache-2.0 | Yes | Useless for A2DP |
| BlueZ | Yes | GPL/LGPL | Yes | Linux only |
| Bluedroid/Fluoride (Android, ESP-IDF port) | Yes | Apache-2.0 | Yes | Large; ESP-IDF's port runs on FreeRTOS [I] |

Size [I]: the controller part of e2 is ≈82 KB with RF and crypto (§3.1); a Zephyr Classic host with A2DP source is
probably 40–60 KB. That is **≈120–150 KB**: no better than route A, still a closed controller, and it needs
`config_btctler_hci_standard = 1` to work for BR/EDR, which is unproven. **Not worth it.**

### 3.3 Route C: our own BR/EDR baseband driver

- The BR/EDR engine is documented in the IR much like the BLE engine was [M]:
  - register-writer names such as `__write_reg_packet_type`, `__write_reg_txinfo`, `__write_reg_bch`,
    `__write_reg_clkoffset`, `__write_reg_encry`, `__write_reg_txptr`, `__write_reg_rxptr`, `bredr_freq_table_init`,
    `radio_set_exchg_table`, `READ_SLOT_CLK`;
  - 35 distinct MMIO addresses in `0x20000–0x20168` used by `bredr_frame` and `bredr_link`;
  - 50+ struct types with field names (`link_page`, `link_page_scan`, `link_inquiry_scan`, `lmp_simple_pair`,
    `lmp_encrypt`, `link_afh`, `bredr_tx_bulk`, `bredr_rx_bulk`, …).
- What a clean-room A2DP source would need [I]: page and inquiry (as central, to reach headphones), the hop
  sequences, ARQ and flow, LMP (mandatory set, Secure Simple Pairing with P-256, E0 or AES-CCM encryption, AFH,
  role switch), L2CAP, SDP, AVDTP, A2DP, and an SBC encoder. BR/EDR has no "link-controller engine" convenience
  comparable to BLE's advertising engine that we know of. The emulator models no BR/EDR [M, brief].
- **Estimate [I]: 40–70 KB flash, 8–15 KB RAM, 9–18 person-months**, about three times the BLE route C, with
  interoperability testing against many headphones. It is GPL-clean. **Not a hobby-scale project.**

### 3.4 Route D: external module or external box

| Option | How | Latency | Firmware cost | Notes |
| --- | --- | --- | --- | --- |
| **D0: off-the-shelf BT transmitter on the headphone/line out** | Analog | ≈40 ms codec with aptX LL on both ends [S]; 100–300 ms with SBC headphones | **0** | Works today. The only way to get near-playable wireless monitoring |
| **D1: phone → FM-1 over USB audio** | Optimist's UAC1 stereo playback (host → FM-1) | wired, a few ms [I] | 0 (exists) | Covers play-along from a phone with a cable, if the phone outputs class-compliant USB audio [I] |
| D2: original ESP32 (Classic) as A2DP source/sink | I2S or UART to the FM-1 | as A2DP | I2S needs free pins and a second I2S/ALNK path; UART cannot carry 1.4 Mbit/s PCM [I] | **The ESP32-S3 has no Classic Bluetooth** [S], so Felucca's USB-companion route (ESP32-S3 helper over USB host) **cannot do A2DP**. The original ESP32 has no USB OTG [I]. Needs a hardware modification |
| D3: ESP32-S3 helper (USB host) with LE Audio | USB host bulk | LE Audio | The companion protocol (~10 KB, ENGINE-PLUGINS §4.3) plus an audio stream | Espressif says the S3 does not support LE Audio either [S]. Dead end |

---

## 4. Codecs

### 4.1 Measured cost on pi32v2

Method [M]: each codec was compiled with the JieLi clang (`-target pi32v2 -mcpu=r3 -mfprev1 -ffp-contract=off`,
the flags Optimist uses for its float engine) at `-O2` and `-Os`, linked into a bare emulator image, and run in
`fm1-emu boot --until bench_done` (branch `feat/ble-engine` release build) with N = 0 and N = 32 frames. The table
gives the difference per frame, minus the test-signal generator (3,466 instructions per 128 stereo frames at `-O2`, 4,745 at `-Os`,
measured separately). The emulator counts **instructions, not cycles**; it does not model cache misses or XIP wait states.
At one instruction per cycle on a 360 MHz core (the clock the SPL leaves, from the emulator's register decode,
`rust-emulator/CLOCK.md`) [M/I], the percentages are **lower bounds**.

| Codec (licence) | Work | Instr. per frame `-O2` (`-Os`) | Per second | Share of 360 MHz | Code + tables (`-Os`/`-O2`) | State RAM |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| SBC encoder, Android Bluedroid via BTstack 3rd-party (Apache-2.0) | 44.1 kHz, joint stereo, 16 blocks, 8 subbands, bitpool 53 (≈328 kbit/s, 119-byte frames) | 28,056 (28,493) | 9.67 M | **≈2.7 %** | 5.4 / 6.1 KB + 0.1 KB | ≈1.7 KB |
| SBC encoder, BlueZ libsbc (LGPL-2.1-or-later) | same | 30,691 (32,882) | 10.57 M | ≈2.9 % | 13.3 / 19.7 KB + 2.4 KB (enc and dec together; the link kept both) | 5.6 KB |
| SBC decoder, BlueZ libsbc | same stream | 52,192 (59,299) | 17.98 M | ≈5.0 % | (same object) | 5.6 KB |
| SBC decoder, WL82 hardware | A2DP sink | — | — | ≈0 % CPU [I] | ≈1 KB driver wrapper [M] | — |
| LC3 encoder, google/liblc3 (Apache-2.0) | 48 kHz, 10 ms, 2 channels, 120 B per channel (96 kbit/s each) | ≈627,500 (≈640,100) per 10 ms | 62.8 M | **≈17.4 %** | 28.2 / 37.3 KB code + **86.1 KB tables** (all rates, incl. high-resolution) | 2 enc + 2 dec = 28,960 B |
| LC3 decoder, liblc3 | same | 221,597 (254,213) per 10 ms | 22.2 M | ≈6.2 % | (same) | (same) |

Checks [M]: SBC output was 119 bytes per frame (`bench_sink` = 32 × 119); the LC3 build linked no soft-float
library calls (only `sqrtf`, `floorf` and similar from libm), so the FPU was used.

Notes [I]:
- LC3's tables could be cut to 48 kHz/10 ms only, likely to 15–25 KB. Optimist runs at 44.1 kHz, so LC3 at 48 kHz
  also needs a resampler, or LC3's 44.1 kHz mode.
- XIP misses could make real cycles noticeably higher; Optimist's hot audio code runs from RAMTEXT for that reason
  (MEMORY-BUDGET §1).

### 4.2 Where it would run

- The audio ISR uses up to ~85 % of each 5.8 ms half on CPU 0 [brief, CPU-GUARD]. **SBC encoding (≈3 %, ≈56,000
  instructions per 256-frame half, ≈0.16 ms)** could run on CPU 0 below the audio ISR, in the main loop or a
  soft-IRQ, reading a ring the ISR fills [I]. It would compete with the UI, not with audio.
- **LC3 (≈17 % encode)** would not fit beside a busy audio ISR on CPU 0. It would need CPU 1, which today is an
  emulator-only prototype, untested on hardware (DUAL-CORE.md) [M doc].
- A2DP sink: decoding is in hardware; the CPU work is the rate matcher and mixing [I].

---

## 5. Coexistence with BLE MIDI and with audio timing

- **One radio, both modes:** the SDK supports Classic + LE together (`config_btctler_modules`, §2.1) [M], and stock
  V15 runs BLE MIDI with the BR/EDR IRQs 40/41 registered and an A2DP sink record (§2.4) [M]. The controller has
  `ble_vendor_set_hold_prio(role, enable)`, documented as "lock BLE priority so ACL does not lower it; the SDK
  adjusts automatically by default" [M, header comment translated]. So the controller time-shares slots between the
  A2DP ACL link and BLE connection events [I].
- **Effect on BLE MIDI** [I]: an A2DP stream at 328 kbit/s fills a large part of the BR/EDR slots (one 2-DH5 every
  ≈14.5 ms, plus retransmissions). BLE events may be delayed or skipped, which adds jitter to BLE MIDI unless BLE
  gets priority. Unmeasured.
- **Effect on audio timing** [I]: BR/EDR adds the `IRQ_BREDR` (40) and `IRQ_BT_CLKN` (41) handlers. Stock runs them
  at priority 2, below its audio ISR (3), on CPU 0 [M, BLE study §9.2]. BR/EDR slots are 625 µs; Optimist's audio
  ISR can hold CPU 0 for up to ≈4.9 ms. If the baseband needs service within a few slots, BT would have to run above
  audio and take time from the audio budget, or move to CPU 1. ISR durations are unmeasured, and the emulator models
  no BR/EDR, so only hardware can answer this.
- **Flash writes** stop XIP with interrupts off (BLE study §3.2). An A2DP stream would underrun during a project save
  [I]. Acceptable for listening, but audible.

---

## 6. Licence and qualification

- **The same GPL issue as BLE (BLE study §7, §9.4):** routes A and B link closed `btctrler.a` (and, for A,
  `btstack.a`). An Optimist image with them cannot be distributed with its complete Corresponding Source. The SDK
  declares Apache-2.0 "for this SDK" (`README-en.md:496`, root `LICENSE`) [M]. Whether that covers the binary
  libraries, and whether source exists, is a question for JieLi. A GPL section 7 linking exception from all
  copyright holders would still be the clean way for binary-only parts [I, not legal advice].
- **Codecs are fine:** Bluedroid SBC and liblc3 are Apache-2.0, and libsbc is LGPL-2.1-or-later (checked in the
  file headers) [M]. All are GPL-3.0-compatible.
- **Hosts:** Zephyr is Apache-2.0 (compatible). BTstack's non-commercial clause is not [S/I].
- **Bluetooth SIG qualification (note only):** using the Bluetooth trademark for a product needs SIG membership
  (Adopter is free) and a paid declaration per product, reported at roughly $4,000–11,000 [S, third-party figures].
  M-VAVE's qualification covers their own firmware, not a modified one [I]. A hobby firmware that a user flashes
  onto their own device and that does not market itself as "Bluetooth" is outside what the SIG polices in practice
  [I, not legal advice]. Do not use the Bluetooth logo or word mark in Optimist's marketing.

---

## 7. Route table

| Route | Direction | Flash | RAM | CPU | Latency | Licence (distributable under GPL?) | Effort [I] |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **A: SDK stack** | source | ≈165 KB (125.1 BT [M] + 6 enc [M] + ≈35 glue [I]) | ≈40 KB static [M] + ≈10–15 KB heap and stacks [I] | SBC ≈3 % [M]; stack unmeasured | 100–300 ms [S] | No (closed libs) | 3–5 weeks after the BLE shim exists |
| A: SDK stack | sink | ≈165 KB [I] (e1: +4 KB vs. e2 [M]) | ≈40 KB + jitter buffer | HW decode [M] + resampler [I] | n/a (one-way) | No | as above, plus rate matching |
| A + BLE MIDI | dual | ≈190–205 KB [I] | ≈50 KB [I] | — | — | No | — |
| B: controller + Zephyr host | source/sink | ≈120–150 KB [I] | ≈35–45 KB [I] | as A | as A | No (closed controller) | 2–3 months [I] |
| C: own BR/EDR | source/sink | 40–70 KB [I] | 8–15 KB [I] | as A | as A | **Yes** | 9–18 person-months [I] |
| D0: analog BT transmitter | out | 0 | 0 | 0 | ≈40 ms with aptX LL [S] | Yes | none |
| D1: USB audio from phone | in | 0 (exists) | 0 | exists | wired | Yes | none |
| D2: original ESP32 over I2S | both | ≈2–5 KB [I] | ≈4–8 KB ring [I] | small | as A | Yes (ESP-IDF is Apache-2.0) | hardware mod + 2–4 weeks [I] |
| LE Audio (any route) | both | ≥100 KB host + LC3 ≈45–125 KB [M codec, I host] | ≥45 KB [I] | LC3 enc ≈17 % [M] | 20–40 ms [S] | controller closed | not realistic |

---

## 8. Recommendation

**Do not implement A2DP source (FM-1 → headphones) in the firmware.** It is technically possible on this chip,
but its 100–300 ms latency makes it useless while playing, it needs about twice Optimist's free flash and three
times its free RAM, and it brings the same closed-library distribution problem as BLE route A. For wireless
monitoring, recommend an external aptX-Low-Latency transmitter on the headphone out (route D0) in the manual.

**Do not pursue LE Audio.** The controller has ISO code, but nothing above it exists in the SDK, nothing shows it
working on the WL82, LC3 takes ≈17 % of a core to encode, and even then 20–40 ms is above what players accept.

**Keep A2DP sink (phone → FM-1) on file as the one musically sensible direction**, for play-along and sampling.
Latency does not matter there, and the WL82 decodes SBC in hardware. Conditions before any work:

1. **Hardware check (15 minutes, together with BLE-MIDI §9.6 step 2):** with stock V15, does a phone see a
   Classic "FM-1" device and play to it? If it does, stock already has the feature and its behaviour is the
   reference (rate matching, mixing, coexistence with BLE MIDI).
2. **Only after BLE MIDI route B or C runs on hardware.** The OS shim and IRQ policy are shared, and the BLE work
   will show whether the closed controller can live beside the audio ISR.
3. **Measure first:** BR/EDR ISR durations and the IRQ priority they need (`request_irq` wrapper, as in BLE Phase 1).
   The ACL RX pool (17.8 KB) and the jitter buffer must fit in the pool, not in main RAM.
4. **Meanwhile, document D1:** USB audio playback from a phone already gives play-along with a cable.

If the user still wants FM-1 → headphones without an external box, the honest answer is a builder "BT edition" on
route A, private only, that drops a large feature (≈70–110 KB of samples or engines) and moves buffers into the
pool. It is not worth the cost for 100–300 ms monitoring.

---

## 9. Method (reproducible, scratch only)

- **SDK builds:** an APFS clone of the BLE study's SDK copy (`ac79-sdk` commit `e30b1ee`, SDK V1.2.13), with the BLE
  study's edits to shared config files reverted. `demo_edr` was built in `ble-measure-img` (= `optimist-toolchain:20250324.1`
  + make). e1: `CONFIG_DEBUG_ENABLE` off; PCM, AEC, DNS, AAC, mSBC, CVSD, virtual-device and M4A codecs off;
  `SPP_TRANS_DATA_EN 0`; `__FLASH_SIZE__` 1 MB; `USER_SUPPORT_PROFILE_HFP_AG 0`. e2: e1 plus
  `USER_SUPPORT_PROFILE_A2DP 0`, `USER_SUPPORT_PROFILE_AVCTP 0`, SBC decoder off. Attribution: the BLE study's
  DWARF-line script, plus buckets for the codecs. Linker totals (`BTCTLER_CODE_TOTAL` 0x11C60,
  `BTSTACK_CODE_TOTAL_SIZE` 0x9888, `BTCTLER_RAM_TOTAL` 0x891E in e2) agree in scale [M].
- **Stock V15:** a byte search of the decrypted `app.bin` for SDP UUID data elements (`19 11 0A/0B/0C/0E/01/1E/1F`,
  `19 00 19`) and strings, plus a grep of the earlier V15 disassembly for `0x13D00` [M].
- **IR:** the BLE study's `llvm-dis` output of `btctrler.a` members (function, enum and struct names; MMIO
  constants) [M].
- **Codec bench:** sources google/liblc3 `efe84d3`, BlueZ sbc `b3deb8a`, BTstack `e385539` (`3rd-party/bluedroid/encoder`);
  a 30-line start file (copy `.data`, clear `.bss`, `call bench_main`, spin at `bench_done`) and Felucca's
  `app.ld`; `fm1-emu boot ELF --until bench_done --inspect bench_sink:1`. Emulator only; never flashed.
- **Not done:** no hardware, no over-the-air test, no dual-mode (Classic + BLE) SDK link (`demo_edr` has no BLE app
  code; a dual build needs the BLE demo sources), no Zephyr host size measurement.

---

## 10. Sources

- Musicians' latency: arXiv 2503.11562, "Designing Neural Synthesizers for Low-Latency Interaction" (summarises
  Wessel & Wright and Jack et al.) https://arxiv.org/pdf/2503.11562 ·
  Lester & Boley, AES e-library https://aes2.org/publications/elibrary-page/?id=14256 ·
  Mäki-Patola & Hämäläinen, ICMC https://users.aalto.fi/~hamalap5/publications/icmcarticlefinal10.pdf ·
  High Fidelity, "How much latency can live musicians tolerate?"
  https://www.highfidelity.com/blog/how-much-latency-can-live-musicians-tolerate-da8e2ebe587a
- A2DP / aptX latency: Qualcomm aptX Low Latency https://www.aptx.com/aptX-low-latency ·
  SoundStage, "Latency: a new concern for audiophiles"
  https://www.soundstagesolo.com/index.php/features/185-latency-a-new-concern-for-audiophiles ·
  DEV cross-post citing SoundGuys whole-chain tests
  https://dev.to/keyboard-testerclick/your-bluetooth-headphones-are-300-ms-late-measure-it-in-one-browser-tab-2idp ·
  RF Essentials A2DP glossary https://rfessentials.com/resources/rf-glossary/a2dp/
- LE Audio latency (vendor figures): RF Essentials https://rfessentials.com/resources/rf-glossary/bluetooth-audio/ ·
  Avantree https://avantree.com/blogs/auracast/how-do-auracast-devices-use-bluetooth-technology-seamlessly ·
  Vieta https://vieta.es/en/blogs/vieta/auracast
- Zephyr Classic A2DP: https://docs.zephyrproject.org/latest/samples/bluetooth/classic/index.html ·
  https://docs.zephyrproject.org/latest/samples/bluetooth/classic/a2dp_source/README.html ·
  Zephyr BAP broadcast source: https://docs.zephyrproject.org/4.2.0/samples/bluetooth/bap_broadcast_source/README.html
- BTstack licence: https://raw.githubusercontent.com/bluekitchen/btstack/master/LICENSE (condition 4)
- liblc3: https://github.com/google/liblc3 (Apache-2.0)
- libsbc licensing: Debian copyright file https://metadata.ftp-master.debian.org/changelogs/main/s/sbc/unstable_copyright
  (and the `SPDX-License-Identifier: LGPL-2.1-or-later` header in `sbc/sbc.c`)
- ESP32 Classic support: Espressif ESP-ADF Bluetooth audio
  https://docs.espressif.com/projects/esp-adf/en/latest/solution-center/bluetooth-audio.html ·
  Bluepad32 FAQ https://bluepad32.readthedocs.io/en/latest/FAQ/
- SIG qualification fees (third-party): TÜV SÜD https://www.tuvsud.com/en-us/services/testing/bluetooth-qualification ·
  Nordic DevZone https://devzone.nordicsemi.com/f/nordic-q-a/126424/bluetooth-sig-fees
- M-VAVE FM-1 spec (BLE MIDI only): as cited in BLE-MIDI-FEASIBILITY §9.1.

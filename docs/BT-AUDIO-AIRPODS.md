# An "AirPods edition": Bluetooth Classic A2DP source, measured

Status: research and measurement only. Nothing was flashed, no firmware was changed. Date: 2026-10-18.
Builds on [BT-AUDIO-FEASIBILITY.md](BT-AUDIO-FEASIBILITY.md) (route A/B/C/D, the first codec bench). Context:
[BLE-STACK.md](BLE-STACK.md), [BLE-HW-FACTS.md](BLE-HW-FACTS.md).

Goal: FM-1 → AirPods over A2DP. AirPods take **SBC or AAC** from non-Apple sources and **do not support aptX** [S1].
The user would not ship JieLi's closed stack, so **route C (our own BR/EDR stack, no JieLi library, as BLE MIDI)
is the main route**. Route A (the SDK's `btctrler.a` + `btstack.a`) stays for comparison.

Tags as in the feasibility study: **[M]** measured here (link map, emulator count, build), **[I]** inferred or
estimated, **[S]** published source (§8).

---

## 0. Summary

| Part | Flash | Main RAM | CPU (360 MHz core) | Licence, shippable in GPL-3.0? |
| --- | ---: | ---: | ---: | --- |
| **Route C stack** (own BR/EDR: LC, LMP, SSP, L2CAP, SDP, AVDTP, A2DP source, glue, UI) | **≈36–58 KB** [I] (+AVRCP 2–4) | **≈3–6 KB** + 4–8 KB of buffers in the pool [I] | not measured (baseband ISR) | **yes** (our code; micro-ecc BSD-2, SHA-256 public domain) |
| Route A stack (JieLi SDK, for comparison) | 125.1 KB BT + ≈35 KB glue ≈ **165 KB** [M/I] | **≈40 KB** static (28.6 KB ACL pools) [M] + heap [I] | — | **no** (closed `btctrler.a`/`btstack.a`) |
| **SBC** encoder (Bluedroid) | **5.5 / 6.2 KB** (`-Os`/`-O2`) [M] | **1.7 KB** state [M] | **3.1 %** (32,108 instr per 128-sample stereo frame) [M] | **yes** (Apache-2.0) |
| **AAC-LC** encoder (vo-aacenc, 256 kbit/s) | **59.8–65.5 KB** (35.5–41.2 code + 24.3 tables) [M] | **49.1 KB** state (pool-able) [M] | **16.7 %** `-O2` … **≈19.7 %** `-Oz` [M] | **yes** (Apache-2.0); AAC patent licensing separate [I] |
| AAC-LC, FAAC 1.31.1 | 43.4–50.8 KB (incl. 10.6 KB writable tables) [M] | **216 KB** heap [M] | **254 %** (soft double) [M] | yes (LGPL-2.1+), but **cannot run** |
| AAC-LC, fdk-aac | not measured | — | — | **no** (FSF: GPL-incompatible) [S7] |
| AAC-LC, JieLi `lib_aac_enc.a` | 51.0 KB (`-Oz` from its bitcode) [M] | — | — | no (closed; it is VisualOn's encoder, §3.2) |
| **aptX** (libopenaptx/libfreeaptx) | **9.1 / 9.8 KB** [M] | **4.4 KB** [M] | **23.7 %** (247,979 instr per 128 frames) [M] | **yes** via libfreeaptx (LGPL-2.1+) or FFmpeg; **AirPods can't decode it** |
| **aptX HD** | same object [M] | 4.4 KB [M] | **24.5 %** [M] | as aptX; **not for AirPods** |

Budget today: `synth` 578,588 / 581,564 B flash (2,976 free), RAM 97,724 / 98,304 (580 free), pool 331,884 /
335,872 (3,988 free); `user-default` 574,092 B (7,472 free), RAM 95,932 (2,372 free), pool 304,932 (30,940 free)
[M, costs.json checks].

**Fits:** route C + SBC fits after dropping about **50 KB of features and 4–9 KB of RAM** (C1, C2 in §5). Route C
+ SBC + AAC fits flash/RAM in C3 (135 KB freed), but **AAC's 17–20 % of a core does not fit beside the audio
ISR on CPU 0** (up to ~85 %); it needs CPU 1 or a lighter patch. Route A needs ≈160 KB and still runs short of
main RAM unless its ACL pools move to the pool (A1, A2), and it cannot be distributed under GPL-3.0.

**Latency:** AirPods measure ≈126–274 ms on Apple devices with their own optimisations [S2]; from non-Apple
sources over SBC/AAC reports are in the same range or higher (≈190–300 ms) [S3, S4]. The FM-1 side adds ≈12–25 ms
with SBC, ≈55–65 ms with AAC (§6). **Monitoring while playing is not realistic over any A2DP link**; this is a
listening/practice feature.

---

## 1. Route A redone (the SDK link)

The SDK checkout (`~/GitHub/ac79-sdk`, commit `e30b1ee`, SDK V1.2.13) is still there, sparse-checked-out; the
study's e2 copy was gone. A scratch clone (full checkout) rebuilt `demo_edr` as e2: A2DP source only
(`USER_SUPPORT_PROFILE_A2DP 0`, `AVCTP 0`, `HFP_AG 0`), debug off, PCM/AEC/DNS/AAC/SBC-dec/mSBC/CVSD/M4A codecs off,
SPP off, 1 MB config; JieLi clang `-Oz -flto`, built on gpufarm [M].

| | This build [M] | Study's e2 |
| --- | ---: | ---: |
| `.text` | 330,880 | 314,496 |
| `.ram0_data` / `.ram0_bss` | 13,148 / 57,616 | 13,084 / 57,608 |
| `BTCTLER_CODE_TOTAL` (controller) | 72,848 | 72,800 |
| `BTCTLER_RAM_TOTAL` | 35,102 | 35,102 |
| `btstack` code (`btstack_code_end − start`) | 54,884 | 39,048 (`BTSTACK_CODE_TOTAL`) |

The controller reproduces exactly. The host is 15.8 KB larger here: HFP, AT commands, RFCOMM, SPP, AVCTP/AVRCP and
SCO functions are still linked (≈19.9 KB by symbol name) because the SDK's profile switches are runtime `u8`
variables, so LTO cannot drop the library code [M symbol list]. Which extra setting the study used is not known; the
study's ≈165 KB / ≈40 KB stand, marked [M/I] as there.

What this link adds for route C, as **sizes of a production BR/EDR controller** (closed, `-Oz`) [M, symbols in
the controller range]: LMP 24.7 KB (+ auth 0.9, encryption 0.4, role switch 1.8, sniff 2.7, AFH 0.8);
link controller ≈18 KB (`bredr_rx_irq_handler` alone 5.6 KB, connection RX/TX 2.8 KB); link set-up 6.6 KB;
page 2.4 KB, inquiry 1.8 KB; eSCO 2.2 KB (not needed); RF init 5.4 KB (`bt_rf_init`, shared with BLE). The host
parts by name: L2CAP 5.7 KB, SDP 5.2 KB, A2DP 4.4 KB, AVDTP 2.8 KB.

---

## 2. Route C: our own BR/EDR stack, per layer

No open BR/EDR link controller or link manager exists to reuse: the only one found is the OpenCores baseband
project, alpha, last updated 2009 [S5]. Every row below is our code.

### 2.1 What the WL82 BR/EDR hardware is known to do

Known [M, BLE-HW-FACTS §1, §5, §9 and the controller's IR names]:

- Register block `0x20000–0x201FF` (control at `0x20000`, the 625 µs clock sampled at `0x2001C/20/24`); the
  stock init writes `0x2000C … 0x20168`, then `0x20000 = 0x107`, then `0x80000107`; 31 writes of the BT
  configuration block go to BR/EDR; exchange memory (frame descriptors) is ordinary SRAM, base at `0x2FC44`
  (stock `0x01C0A05C`; the SDK's `bd_base` is 4,196 B); IRQ 40 is the Bluetooth clock alarm (`0x200E4`, enable
  `0x2000C` bit 9); the slot timer at `0x2FD40` is shared with BLE; `RFPRIOCNTL/STAT` arbitrate the radio between
  BR/EDR, BLE and Wi-Fi.
- The SDK baseband driver programs the frame engine through small register writers [M, IR names and sizes]:
  `__write_reg_txinfo`, `packet_type`, `bch` (the sync word), `bdaddr`, `clkoffset`, `lt_addr`, `seqn`, `flow`,
  `encry`, `txptr`/`rxptr`/`lmprxptr`, `format`, `linkcntl`, `rxint`, `txphdr`; `bdhw_set_frq_table` (154 B),
  `bdhw_set_afh` (322 B), `radio_set_channel`. Packet types include 2-DH5 and a 3 Mbit/s switch.
- SAFER+ (E1, E21, E22, E3, Ar) is **software** in the SDK (`authentication.c.o`, ≈2.9 KB with tables); SSP's f1–f3
  and g, and P-256 ECDH (uECC), are software too (`btencry_task`). E0 has only a 42-byte register writer, so the
  cipher is very likely in hardware [I, strong].

Unknown, to map as BLE was (emulator traces of stock V15 and `demo_edr`, then hardware):

- Bit layouts of every register above, the descriptor format in exchange memory, the RX status words.
- Whether the **79-channel hop selection** runs in hardware: no hop-kernel-sized function is linked (the only
  hop function is 16 B), and `bdhw_set_frq_table` writes a table, so it is probably hardware with a software-set
  channel table [I]; unproven.
- Whether whitening, FEC 1/3 and 2/3, HEC and CRC are all in hardware (likely, as in BLE [I]).
- How EDR (π/4-DQPSK, 8DPSK) is selected: the radio modulates; software selects the packet type [I]. **EDR is not
  needed for SBC or AAC**: a basic-rate DH5 link carries up to 723 kbit/s one way, SBC at bitpool 53 needs 328
  kbit/s, AAC 256 kbit/s [I from the core spec's rates]. aptX HD (529 kbit/s) would want 2-DH5.
- Whether BR/EDR and our BLE engine can share the radio without the SDK's scheduler (RFPRIO, slot timer).
- The emulator models no BR/EDR [M doc], so unlike BLE there is no emulator oracle for the air interface.

### 2.2 Layers, estimated

Flash and RAM are estimates for a single link, central/master role, SBC only, our coding style [I]; the
references are measured on pi32v2 [M] (BTstack's host built here with JieLi clang `-Os`, `--gc-sections`, from
`example/a2dp_source_demo.c`; JieLi's controller from §1).

| Layer | What AirPods need | Flash [I] | RAM [I] | Reference [M] |
| --- | --- | ---: | ---: | --- |
| RF + BR/EDR baseband bring-up | the BLE RF init we have, plus the BR/EDR register replay | 1–3 KB | — | JieLi `bt_rf_init` 5.4 KB (shared) |
| Link controller driver | slot clock and IRQ, descriptors, TX/RX, packet types DM1/DH1/DH3/DH5, ARQN/SEQN/flow, sync word (BCH from the LAP), clock offset, hop and AFH tables | 6–10 KB | 1.5–3 KB (descriptors, a few baseband buffers) | JieLi LC ≈18 KB + link 6.6 KB (multi-link, eSCO, sniff) |
| Inquiry, page (as master), page scan (AirPods reconnecting), FHS | find and connect headphones | 3–5 KB | <0.5 KB | JieLi page 2.4 + inquiry 1.8 KB |
| LMP subset | features, version, name, host connection, setup complete, max slots, packet type table, timing, clock offset, AFH, supervision, detach; IO capability and SSP PDUs; au_rand/sres; encryption mode, key size, start; reply to (or reject) role switch and sniff | 8–12 KB | 0.5–1 KB | JieLi LMP 24.7 KB + 6.6 KB of auth/encryption/role/sniff/AFH |
| Pairing and encryption | SSP Just Works (NoInputNoOutput) with P-192 or P-256 ECDH, f1–f3 and g (HMAC-SHA-256), E1 and E3 (SAFER+), E0 in hardware, link key in flash | 7–9 KB | 0.5 KB + ≈1 KB stack while pairing | micro-ecc (BSD-2), one curve: **3.7 KB** [M]; SHA-256 (public domain): **1.1 KB** [M]; JieLi SAFER+ ≈2.9 KB |
| L2CAP | basic mode, signalling, 3 channels (SDP, AVDTP signalling, media), segmentation | 2–4 KB | 0.3 KB | BTstack 6.9 KB |
| SDP | server with the A2DP Source record (+AVRCP); client optional | 1–2 KB | 0.2 KB | BTstack 5.0 KB (client + server) |
| AVDTP + A2DP source | discover, get capabilities, set configuration (SBC), open, start, suspend, close, abort; RTP media | 4–7 KB | 0.3 KB | BTstack AVDTP 14.0 + A2DP 3.6 KB |
| AVRCP (optional) | AirPods taps (play/pause), absolute volume | 2–4 KB | 0.2 KB | BTstack controller + target 14.0 KB |
| Audio glue | ring from the audio ISR, SBC frames into DH5 payloads (2 frames per DH5, 5 per 2-DH5), back-pressure, flush | 1–2 KB | 3–6 KB buffers, pool | — |
| UI and settings | scan list, pair, forget, reconnect at start-up | 2–4 KB | 0.3 KB | BLE_CENTRAL item 17.6 KB (costs.json) |
| **Total, SBC, no AVRCP** | | **≈36–58 KB** | **≈3–6 KB main + 4–8 KB pool** | BTstack host alone (HCI 16.6, L2CAP 6.9, SDP 5.0, AVDTP 14.0, A2DP 3.6, util 2.9) ≈49 KB, + AVRCP 63 KB, with 8.4 KB of RAM |
| + SBC encoder | | 5.5–6.2 KB | 1.7 KB | [M] §3.1 |

For scale: our BLE stack is 8.2 KB (9.7 KB with LL encryption) against tens of KB in the SDK, and the `BLE`
item costs 24.9 KB of flash and 6.1 KB of RAM in the firmware [M, BLE-STACK §8, costs.json]. Route C Classic
is about **two to three times BLE's size**. The study's effort estimate (9–18 person-months, interoperability
testing against several headphones) still holds [I]. Encryption is needed in practice: SSP devices run security
mode 4, where every L2CAP service but SDP needs an encrypted link [I, core spec structure], so SSP + E0 are not
optional for AirPods. Secure Connections (P-256 + AES-CCM) is not needed if we do not advertise it [I].

---

## 3. Codecs

### 3.1 Method

Each encoder was built with JieLi clang (`-target pi32v2 -mcpu=r3 -mfprev1 -ffp-contract=off`, `-O2` and `-Os`,
`-ffunction-sections --gc-sections`) on gpufarm, linked into a bare emulator image (a small start file, a pool
allocator), and run with `fm1-emu boot --until bench_done` in three variants: init only, 32 frames, 32 frames
of signal generation only. Per frame = (32 frames − generator only) / 32. The signal is two triangle waves with
noise, 44.1 kHz stereo. Every output was **compared byte for byte with the same driver built on the host**
(x86-64 gcc, arm64 clang): SBC, vo-aacenc, aptX and aptX HD are identical [M]. Code + tables = the linked ELF minus
an empty harness (it includes the few libc functions pulled in). Instructions, not cycles: no cache or XIP wait
states, so the percentages are lower bounds (as in the study).

**The emulator needed four fixes to run these** (scratch copy only, `~/GitHub/optimist-work/btaudio/emu`, not
committed anywhere) [M]:

1. **A real bug: `E16x` AND with a replicated-byte immediate.** `r1 = r1 & 0xFF00FF` (`61 E1 FF 11`) was executed
   as `& 0x1FF`: the AND form takes `x & 1023` whenever bits 10–11 are zero, ignoring the byte-pattern bits 8–9
   (`extended.rs`, `Wide::LogicImmediate`). JieLi clang emits this pattern (it loads 10-bit masks such as `0x1FF`
   into a register instead). It made vo-aacenc's bit counter wrong and its frames twice too long. **Any emulator
   result that used `& 0x00XX00XX` or `& 0xXX00XX00` masks is suspect** (the SBC encoder and the study's numbers
   produced correct output, so they are not affected).
2. `E1D8 …3`: `r5_r4 >>>= r1`, the arithmetic form of the register-pair shift (aptX).
3. `E1FE`: `r9_r8 -= r5 * r6 (s)`, the subtracting wide multiply (aptX).
4. `E8D0/E8D1`: pop a register list and `rets` without returning, before a tail call (FAAC's libm).

### 3.2 Results

| Encoder (version, licence) | Setting | Instr. per frame `-O2` (`-Os`) | Per second | Share of 360 MHz | Code + tables `-Os` / `-O2` | State RAM |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| SBC, Bluedroid via BTstack `e385539` (Apache-2.0) | joint stereo, 16 blocks, 8 subbands, bitpool 53, 119-byte frames (328 kbit/s) | 32,108 (32,541) | 11.1 M | **3.1 %** | 5.5 / 6.2 KB + 0.1 KB | 1,700 B |
| AAC-LC, vo-aacenc `a277487` (Apache-2.0, fixed point) | 256 kbit/s CBR, 1024-sample frames, 743 B each | 1,396,896 (1,419,408); `-Oz` ≈1.65 M | 60.2 M | **16.7 %** (`-Oz` ≈19.7 %) | 62.6 / 65.5 KB (`-Oz` 59.8 KB) | 49,056 B (heap) |
| AAC-LC, FAAC 1.31.1 (LGPL-2.1+, double precision) | 256 kbit/s target, `MAX_CHANNELS` cut to 2 | 21.25 M | 915 M | **254 %** | 43.4 / 50.8 KB, of which 10.6 KB writable tables | 215,840 B heap |
| aptX, libopenaptx 0.2.1 (code = libfreeaptx, FFmpeg `aptx.c`) | 4:1, 128 stereo samples per call | 247,979 (253,574) | 85.4 M | **23.7 %** | 9.1 / 9.8 KB | 4,432 B |
| aptX HD, same | 6:1 on 24-bit | 255,461 (260,849) | 88.0 M | **24.5 %** | same | 4,432 B |

Notes:

- FAAC's output was not bit-identical with x86 (whose quantiser takes an SSE path); 20,429 vs 20,470 bytes for
  32 frames. Init alone is 52.9 M instructions [M]. It uses soft-float `__adddf3`/`__muldf3` and double libm, so it
  is out regardless.
- **JieLi's `lib_aac_enc.a` is VisualOn's encoder**: its members match vo-aacenc file for file (`aacenc_core`,
  `qc_main`, `psy_main`, `adj_thr`, `bit_cnt`, `basicop2`, `oper_32b`, …), it exports `voAACEncInit`, and it is
  tagged `br23_aac_enc Jul 6 2021` [M]. Its bitcode compiled at `-Oz` is 26.6 KB of code + 24.4 KB of tables.
  The open vo-aacenc is the same codec under Apache-2.0, so the closed one brings nothing. The SDK's e0 "AAC" is a
  decoder (`CONFIG_AAC_DEC_ENABLE`, A2DP sink) [M].
- AAC over A2DP is carried in LATM (MPEG-4) framing, about 0.5 KB more code [I]. AAC's 49 KB of state can live in
  the pool [I].
- The CPU budget: the audio ISR uses up to ~85 % of each half on CPU 0 (CPU-GUARD). **SBC (3 %) fits there**,
  below the ISR, as the study said. **AAC (17–20 %) and aptX (24 %) do not**; they would need CPU 1, which is an
  emulator prototype today (DUAL-CORE.md), or a patch that leaves 20 % free [I].
- FFmpeg's native AAC encoder (LGPL-2.1+, float, needs libavcodec's framework) and fdk-aac (C++, GPL-incompatible
  licence [S7]) were not built.

### 3.3 Licences and patents

| Codec | Copyright licence | GPL-3.0 firmware? | Patents |
| --- | --- | --- | --- |
| SBC (Bluedroid) | Apache-2.0 [M header] | yes | part of A2DP; no separate licence known [I] |
| vo-aacenc | Apache-2.0 [M COPYING] | yes (Apache-2.0 → GPL-3.0) | AAC patent licensing (Via LA pool) is separate from copyright; not evaluated [I, not legal advice] |
| FAAC | LGPL-2.1-or-later [M headers] | yes | as AAC |
| fdk-aac | Fraunhofer FDK licence | **no**: the FSF lists it as free but GPL-incompatible (clause 3) [S7] | as AAC |
| libopenaptx ≥ 0.2.1 | GPL-3.0-or-later **plus a README that forbids use by named projects** [M README] | the GPL part yes, but the extra restrictions conflict with GPL-3.0 §7/§10; avoid [I] | — |
| libfreeaptx (fork of 0.2.0) | LGPL-2.1-or-later [S6] | yes, same code | aptX (EP0398973B1) and aptX HD (US9398620B1) are reported expired by an openSUSE review, not confirmed [S6] |
| FFmpeg `aptx.c` | LGPL-2.1-or-later | yes | written from the expired aptX patent and reverse engineering, per its author [S6] |

---

## 4. Pool and RAM facts that shape the editions

- The pool (344,064 B, 8 KiB kept spare) holds the FX lines; **`DLY_LEN` 32768 → 16384 frees 32 KB of pool**
  [M costs.json]. Baseband buffers, the PCM ring and AAC's state belong there; the BLE study's rule (RAM is the
  hard limit) holds.
- Main RAM savers are few: `ENG_FM6` 10.5 KB, `MASTER_COMP` 6.1 KB, `BLE` 6.1 KB, `FM6_MARK1` 4.1 KB, `REV_FDN8`
  3.6 KB, `ENG_DRAWBAR` 2.5 KB, `INS_EQ3` 2.1 KB [M costs.json]. Route A's 40 KB of static RAM cannot come from
  these alone.

---

## 5. Candidate AirPods editions

Totals from the builder's own model (`configure.budget`, the plain sum of costs.json deltas) applied to the
profile's real sizes; the sum model is a few KB off for far-apart configurations (LTO sharing), so leave the
builder's 1 KB / 512 B margins on top [M model, I accuracy]. Script:
`~/GitHub/optimist-work/btaudio/tools/editions.py`.

| Edition | Base | Items off | Frees (flash / RAM / pool) | Free after (flash / RAM / pool) | Needs | Fits? |
| --- | --- | --- | ---: | ---: | --- | --- |
| **C1** route C, SBC | synth | X0X 909 kit, FM6 MARK I, delay 0.74 → 0.37 s | 48.1 KB / 4.1 KB / 39.0 KB | 51.1 KB / 4.7 KB / 43.0 KB | 42–64 KB / 3–6 KB / 4–8 KB | at the low half of the estimate; RAM tight |
| **C2** route C, SBC | user-default | web editor, BLE devices (central), COMP + LIMIT, delay 0.37 s | 49.2 KB / 9.2 KB / 34.8 KB | 56.7 KB / 11.6 KB / 65.7 KB | as C1 | **yes**, keeps BLE MIDI and the 909 |
| **C3** route C, SBC + AAC | user-default | X0X 909 + 808 kits, web editor, BLE devices, FM6 MARK I, COMP + LIMIT, delay 0.37 s | 127.9 KB / 14.9 KB / 44.4 KB | 135.3 KB / 17.3 KB / 75.3 KB | +60–66 KB code, 49 KB state (pool) | flash/RAM yes; **CPU no** on CPU 0 (§3.2) |
| C1b (variant of C1) | synth | web editor, BLE devices, COMP + LIMIT, delay 0.37 s | 52.1 KB / 9.2 KB / 34.8 KB | 55.0 KB / 9.8 KB / 38.7 KB | as C1 | yes, keeps the 909 |
| A1 route A (comparison) | synth | X0X 909, web editor, BLE MIDI, BLE devices, FM6, COMP + LIMIT, delay 0.37 s | 159.5 KB / 25.2 KB / 41.3 KB | 162.5 KB / 25.7 KB / 45.3 KB | ≈165 KB / ≈40 KB + heap | **no**: flash ≈2.5 KB short, RAM short unless the 28.6 KB ACL pools move to the pool; closed libraries |
| A2 route A (comparison) | user-default | as A1 | 157.6 KB / 25.1 KB / 51.6 KB | 165.1 KB / 27.5 KB / 82.5 KB | as A1 | borderline flash; RAM only with the ACL pools in the pool; closed libraries |

C2 is the recommended shape if this is built: it keeps every engine, both X0X kits and BLE MIDI, and gives up the
USB web editor, BLE-MIDI device scanning (central) and the master compressor. Whether BLE MIDI and an A2DP stream
can share the radio in our own stacks is unknown (§2.1).

---

## 6. Latency

| Stage | SBC | AAC | Source |
| --- | ---: | ---: | --- |
| Optimist's output buffer (two 256-frame halves at 44.1 kHz) | 5.8–11.6 ms | same | [M, feasibility §1.1] |
| Codec framing at the FM-1 | 2.9 ms per frame; 2 frames per DH5 (5.8 ms), 5 per 2-DH5 (14.5 ms) | 23.2 ms per frame + ≈23 ms MDCT overlap ≈ 46 ms | [I, frame sizes] |
| Retransmission slack at the source | a few ms | same | [I] |
| **FM-1 side total** | **≈12–25 ms** | **≈55–65 ms** | [I] |
| AirPods' jitter buffer and decode (the sink decides) | AirPods 1st gen 274 ms, 2nd gen 178 ms, Pro 144 ms, Pro 2 126 ms, all from Apple devices with Apple's tuning | | [S2] |
| From non-Apple sources | RTINGS' AirPods Pro on PC ≈188 ms (second-hand); Android SBC/AAC links in general 300–370 ms whole chain | | [S3, S4] |

A source cannot shorten the AirPods' buffer. Expect **≈150–300 ms** end to end: fine for listening to a pattern,
practice with headphones, demos; unusable for playing keys and pads (musicians want ≤10–20 ms, feasibility §1.1).

---

## 7. What was not measured, and why

- **Route C's sizes are estimates.** Nothing of it exists; the references (BTstack's host on pi32v2, JieLi's
  controller, micro-ecc, SHA-256, our BLE stack) are measured, the layer figures are not.
- **The BR/EDR hardware's register layouts, hop selection, whitening/FEC, EDR selection and coexistence with
  BLE**: the emulator has no BR/EDR model; mapping needs emulator traces of stock and `demo_edr`, then hardware.
- **Route A's host size as the study had it** (39 KB): this rebuild links 15.8 KB more host code (§1); the
  setting difference was not found.
- **Cycles**: the emulator counts instructions; XIP misses would add, so CPU shares are lower bounds.
- **ECDH time while pairing**, the baseband ISR's CPU share: not measured.
- **fdk-aac and FFmpeg's AAC encoder**: not built (licence; framework size).
- **Real AirPods latency from a non-Apple source**: only second-hand and general Android figures were found.

Scratch (not in the repo): `~/GitHub/optimist-work/btaudio/` (bench sources, run and size scripts, SDK clone,
patched emulator, editions script); gpufarm `~/btaudio-mots/` (builds).

---

## 8. Sources

- [S1] AirPods codecs SBC and AAC only, no aptX/LDAC: Notebookcheck,
  https://www.notebookcheck.net/Apple-clarifies-lack-of-high-definition-Bluetooth-codec-support-for-AirPods-Max-and-AirPods-Pro-2.670484.0.html ·
  https://www.notebookcheck.net/Apple-could-address-shortcomings-of-the-AirPods-3-AirPods-Pro-and-AirPods-Max-with-future-update.631469.0.html
- [S2] AirPods latency on Apple devices (Stephen Coyle's tests): 9to5Mac
  https://9to5mac.com/2019/12/22/airpods-latency-test/ · https://9to5mac.com/2022/11/15/airpods-pro-2-latency-test/ ·
  https://stephencoyle.net/airpods-pro-2
- [S3] AirPods Pro on PC 188 ms, attributed to RTINGS (second-hand): ResetEra
  https://www.resetera.com/threads/how-bad-is-the-latency-using-air-pods-pro-with-a-switch.812670/ ·
  RTINGS review pages (values locked): https://www.rtings.com/headphones/reviews/apple/airpods-pro-truly-wireless
- [S4] Android Bluetooth latency by codec (SoundGuys): https://www.soundguys.com/android-bluetooth-latency-22732/
- [S5] No open BR/EDR controller: OpenCores Bluetooth baseband (alpha, 2009) https://opencores.org/projects/bluetooth ·
  BOSS is LE-only https://music.lab.vcu.edu/projects/boss/
- [S6] aptX: FFmpeg's implementation from the expired patent EP0398973B1
  https://lists.ffmpeg.org/pipermail/ffmpeg-devel/2017-November/219333.html · openSUSE review (aptX HD
  US9398620B1, "now expired", not a lawyer) https://build.opensuse.org/request/show/905123 · libfreeaptx
  (LGPL-2.1+, fork of libopenaptx 0.2.0) https://github.com/iamthehorker/libfreeaptx · libopenaptx README
  https://sources.debian.org/src/libopenaptx/0.2.0-6/README
- [S7] fdk-aac: FSF considers the FDK AAC licence free but GPL-incompatible (clause 3), as cited by Fedora/RPM Fusion
  https://lists.rpmfusion.org/archives/list/rpmfusion-developers@lists.rpmfusion.org/thread/LLXIHSB2TRHTKUWSSIFXQIP66WUZMK57 ·
  FFmpeg's LICENSE.md https://code.ffmpeg.org/Traneptora/FFmpeg/src/commit/13b8ba8c9d1fe3cec854a98b13b025578aa7ac6a/LICENSE.md
- Zephyr's Classic A2DP source exists but publishes no footprint:
  https://docs.zephyrproject.org/latest/samples/bluetooth/classic/a2dp_source/README.html

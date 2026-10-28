# SOLVED 2026-10-27: this handoff is history
Root cause: `wl_txb` / `link_tx_buf` were `IN_POOL` (0x01C20000..). The Wi-Fi frame DMA fetches garbage from the pool bank, so every data frame went out as garbage with `tx_err 0`. Handshake, probe and RX buffers were main RAM, so they always worked. The "weak -86 dBm TX" was the same artifact (-48 dBm with main-RAM buffers).
Fix (try25/26): both buffers in main RAM; hardware CCMP at WCID 1 (software CCMP behind `wifiswc 1`); keep the ExtIV (0x6008=0x20000001) and KeyTabFlag (0x6804=0x09) fixes.
Result: DHCP lease, IGMP join, Link ALIVE on 224.76.78.75:20808, Live synced. try27: tick catch-up after stalls, no tick dropped on a full ring, LINK first in SYNC AUTO.

---
(original handoff below, for the record)

<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Wi-Fi CCMP transmit — focused handoff (2026-10-27)

One thing is left to make Link-over-Wi-Fi work: **the FM-1's hardware-encrypted (CCMP) data frames don't reach the
AP.** Everything up to and including the key install is proven correct on hardware. This note is the scoped TX-path
problem, with the evidence, so it can be picked up cold.

## The symptom (measured, reliably)

FM-1 associated to the AP (WPA2-PSK, confirmed), sending UDP frames every second for 40 s. On the AP (a UniFi UDR,
`mca-dump`) the station counter stays **`rx_packets: 2, rx_bytes: 248`** the entire time, and nothing from the FM-1
reaches the bridge (`br0` tcpdump = 0 frames). Those 2 packets are the two **unprotected** EAPOL handshake frames
(M2, M4). **Every protected data frame after association is rejected at the AP's receiver** (the counter never moves),
even though the FM-1 reports `tx 18, tx_err 0`. Frame size is not the cause (70 B and 296 B both fail).

So: **unprotected frames transmit fine; the moment the 802.11 Protected bit is set and HW CCMP is invoked, the frame is
malformed on air and the AP drops it before counting.**

## What is PROVEN CORRECT (do not re-investigate)

- **Join + 4-way handshake**: auth, assoc (aid 1), EAPOL M2/M4 valid (the AP accepts them), keys installed. Reliable
  from a fresh boot. (The earlier "handshake regression" theory was wrong — EAPOL is unprotected, so the TX-encryption
  code can't affect it.)
- **TX power**: fine at the stock 6 (swept 6/15/31 via `wifipow`). We don't set a Wi-Fi-specific power; it rides the
  shared BT RF trims, and that's enough.
- **Key install — VERIFIED BY READBACK on hardware** (`wifikey` console cmd, reads the regs back):
  `0x1808` = BSSID, `0x4020`/`0x4024` = pairwise TK present, `0x6804` = `0x09`, `0x6008` = `0x20000001`, `0x600C` = 0.
  Every value reads back exactly what we wrote and **exactly matches the vendor SDK**. The key-table address window
  decodes fine; the TK is in the MAC; the WCID attribute and IV are correct.
- **RX** works (beacons, scan, unicast decrypt seen).

## Two real bugs found + fixed this session (KEEP THESE)

Both verified against the vendor SDK IR (see "Pointers") and confirmed landing by readback:

1. **CCMP ExtIV bit.** `wifi_sta_install_hw_keys` wrote the WCID IV (`0x6008`) = 0. The vendor
   (`RTMPSetWcidSecurityInfo`, AES branch) writes `((KeyID<<6)|0x20)<<24 | 1` = **`0x20000001`** — the `0x20` is the
   mandatory CCMP ExtIV bit. Fixed.
2. **WCID attribute KeyTabFlag.** We wrote `0x6804` = `0x08` (KeyTabFlag bit0 = 0 = shared key table). The vendor
   (`WPAInstallPairwiseKey` → `RTMPSetWcidSecurityInfo(..., KeyTabFlag=1)`) sets bit0 = 1 = **pairwise** key table, where
   our TK actually lives. Fixed to **`0x09`**.

Neither fix changed the symptom (`rx_packets` still 2) — because, as the readback proves, key setup was not the blocker.

## THE REMAINING TASK: the TX submission path

The MAC's TX **encryption engine is not being engaged** by our transmit path, even though the key/WCID/Protected-bit are
all set. Our TX is a single-shot frame DMA:

- `firmware/hal/fm1_wifi.h` → `fm1_wifi_tx_prepare` (builds the 24-byte TX header: a length word, a 4-byte TXINFO =
  `buf[1]` = `4`, then a 16-byte TXWI = `buf[2..5]`), and `fm1_wifi_tx` (writes `0x30F10`/`0x30F14`, waits for the DMA
  done bit). This descriptor layout came from an **emulator capture of the SDK's raw (unprotected) send**
  (`wifi_send_data`), so it is only known-good for unprotected frames.

Likely causes to chase (in the SDK IR, `wl_wifi_sta.a`):
- The SDK's **data** path (`STAHardTransmit` → the WPDMA TX ring enqueue) may differ from the raw single-shot DMA — HW
  encryption may only happen for frames submitted through the proper TX ring / TX pipeline, not our direct FIFO DMA.
- The **TXINFO** our raw descriptor copies (`buf[1]=4`) may be wrong for an encrypted data frame. `RTMPWriteTxInfo`
  (`ll/rtusb_data.c.ll`) takes `bWiv` (Wireless-Info-Valid, bit 24 in the USB TXINFO) and `QueueSel` (bits 25-26). Find
  the `bWiv`/`QueueSel` the **data** path passes for an encrypted frame and compare to ours. (Our core's TXINFO bit
  layout differs from USB — the emulator capture put QSEL at bits 1-2 — so map carefully.)
- The **TXWI IV/EIV words** (`buf[4]/buf[5]`, currently 0): confirm whether the wl30 TX engine fills them from the WCID
  IV table (`0x6008`) or expects the driver to write them in the TXWI.
- `RTMPWriteTxWI` (`ll/cmm_data.c.ll`) has **no** cipher-enable bit — encryption is purely WCID+key+Protected-bit — so
  the trigger is the submission path / TXINFO, not a TXWI flag.

## Session update (2026-10-27): the TXINFO/bWiv theory is refuted by the SDK itself

Went looking for `RTMPWriteTxWI`/`STAHardTransmit` in `worktrees/link-ref/ac79-liba` as this doc pointed to — neither
name resolves in `ll/cmm_data.c.ll` as written (a stale pointer below, corrected), but the **real**, actually-defined
functions are there and settle the open question:

- **`RTMPWriteTxWI_Data`** (`ll/cmm_data.c.ll:2042`, not `RTMPWriteTxWI`) is the vendor's real per-frame TXWI builder.
  Its bitfield layout: WCID at i64 bit 40 (= TXWI word1 byte 1, i.e. bits 8-15 — exactly where our
  `fm1_wifi_tx_prepare` already puts `WCID 1 << 8`), MPDU length (`MpduHeaderLen + SrcBufLen`) at i64 bits 48-59 (=
  TXWI word1 bits 16-27 — exactly our `air_len << 16`). **There is no cipher/encrypt-enable bit anywhere in this
  function** — it never reads `pTxBlk->CipherAlg` at all. Our TXWI layout already matches it field-for-field.
- **`STAFindCipherAlgorithm`** (`ll/rtmp_data.c.ll:2608`) resolves the cipher per packet and stores it into
  `pTxBlk->CipherAlg`/`pKey`/`KeyIdx` — fields consumed in exactly two places downstream: (1)
  `duplicate_pkt_with_TKIP_MIC`, gated on `CipherAlg == 3` (TKIP only — CCMP's enum value never takes this path, so
  there is no software IV/MIC insertion for AES/CCMP, consistent with our plaintext-IV-reserved approach), and (2)
  `STABuildCache802_11Header`, where `CipherAlg != 0` ORs bit 14 into the cached Frame Control word — bit 14 of a
  16-bit FC is exactly the Protected Frame bit (byte 1, `0x40`) we already set from `wip->protect`.
- **`RTMPWriteTxInfo`'s `bWiv`** (`ll/rtusb_data.c.ll`) is real, but every call site that passes `bWiv=1` is a
  **management/null-frame USB bulk-transfer header** (`RtmpUSBMgmtKickOut`, `RtmpUSBNullFrameKickOut`,
  `MlmeHardTransmit`-style EAPOL sends) — i.e. "this URB carries a driver-built TXWI", a USB bulk-endpoint
  bookkeeping bit, not a per-frame cipher trigger. It has no counterpart in our SDIO/wl30 single-shot DMA path at
  all (there's no equivalent register at `0x30F10`/`0x30F14`), and `QueueSel` there just selects a USB bulk
  endpoint/TID, irrelevant to our one-frame MAC queue.

**Net result: the SDK gives zero evidence for a missing TXINFO/bWiv/QueueSel trigger.** Every mechanism the vendor
driver uses to distinguish protected from unprotected frames — WCID assignment and the Protected bit in the 802.11
header — is already implemented identically in our code (`firmware/hal/fm1_wifi.h`, `firmware/src/wifi/wifi_ip.c`).
No code change made this session: the evidence argues against the "remaining task" hypothesis above rather than
for a specific fix, so nothing here should be modified speculatively. If the on-air garbling persists, the next
concrete, testable step is still external (`wifibig` + an AP/bridge capture, per the earlier entries in
`WIFI-LINK-HANDOVER.md`) to see what differs about the *air* behavior itself, since the register-level description
of our TX path now matches the vendor's own description field-for-field.

## How to reproduce / test (the loop)

- Build: `OPTIMIST_WIFI=1 PATH=<builder venv>:$PATH python3 tools/optimist.py package --profile bt-debug --set
  ENG_PHYS=0 --set ENG_JUNO=0 --name link-tryN --out firmwares` (PHYS+JUNO off to fit). The user flashes; never flash
  from here.
- Console (drive over serial; passphrase on the command line only, never stored): `wifijoin mots <pass>
  1c:0b:8b:48:ec:60` (the `[bssid]` arg pins the UDR so the AP is measurable), then `wifilink`, `wifibig [N]`,
  `wifikey`, `wifipow [N]`, `wifiovh [N]`. Helper: `worktrees/link-ref/try16_pin.py` and friends.
- Measure on the AP (UDR, `ssh root@192.168.24.1`): `mca-dump` for the station's `rx_packets` (NOT `wl` — it returns
  empty on this box), and `tcpdump -i br0 -n -s0 -e ...` for the decrypted/bridged frames. Keep the second "mots" AP
  (`8C:ED:E1` at .20) off, or pin the BSSID, so the FM-1 is on the AP you can see. **The win condition:** `rx_packets`
  climbs past 2 and `wifilink` gets a DHCP lease.

## Pointers

- Full chronological log: `worktrees/link-ref/ap-capture-finding.md`.
- Vendor SDK IR for reading the TX path: `worktrees/link-ref/ac79-liba/` — `./fn.sh ll/<file>.c.ll <FunctionName>`
  dumps one function's LLVM IR with MMIO constants as `@0xADDR`. Start at `STAHardTransmit`, `RTMPWriteTxInfo`
  (`ll/rtusb_data.c.ll`), `RTMPWriteTxWI` (`ll/cmm_data.c.ll`), and the WPDMA/USB enqueue.
- Our code: `firmware/hal/fm1_wifi.h` (TX descriptor), `firmware/src/wifi/wifi_sta.c` (`wifi_sta_install_hw_keys` — the
  verified-correct key install), `firmware/src/wifi/wifi_ip.c` (`build_80211_data_hdr`), `firmware/src/wifi/wifi_link.c`.
- All of try12..try19 is uncommitted on branch `link`.

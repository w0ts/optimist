# Wi-Fi on the FM-1 (AC791N): feasibility (2026-10-26)

What a Wi-Fi driver of our own would take, read from JieLi's AC79 SDK the way the Bluetooth facts were
(docs/BLE-HW-FACTS.md): the SDK's prebuilt libraries are LLVM bitcode, readable as IR with names and debug lines.
For Link (docs/LINK-DESIGN.md) only a station is needed: join a WPA2-PSK network, DHCP, UDP multicast.
Nothing here is written for the firmware yet. **Feasibility notes, not a design.**

## 1. Sources

| What | Licence | Use |
| --- | --- | --- |
| `AC79NN_SDK_V1.2.1_2023-12-13` (gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK), `cpu/wl82/liba/wl_rf_common.a`, `wl_wifi_sta.a`, `wl_wifi.a` | Apache-2.0 (the SDK's `LICENSE`) | facts: registers, sequences, tables (as for BT, `LICENSING.md`) |
| Linux `rt2800lib` / `rt2800usb` (drivers/net/wireless/ralink/rt2x00) | GPL-2.0-only | documentation of the same MAC / BBP registers only: **no code taken** (GPL-2.0-only cannot become GPL-3.0) |

Read locally (git-ignored): `worktrees/link-ref/ac79-liba/` (the three `.a`, `ll/*.ll` from `llvm-ar x` + `llvm-dis`,
`obj/` the bitcode compiled with our `pi32v2` clang `-Os`).

## 2. What the hardware is (read from the libraries)

- **The Wi-Fi MAC is Ralink's RT3070, the USB chip, put on-chip.** `wl_wifi_sta.a` (90 objects, all bitcode) is
  Ralink's RT30xx STA driver with its file names unchanged: `rt30xx.c`, `rtmp_chip.c`, `rtmp_init.c`, `mlme.c`,
  `sync.c`, `connect.c`, `assoc.c`, `auth.c`, `cmm_wpa.c`, `cmm_data.c`, `rtusb_io.c`, `rtusb_bulk.c`, `rtusb_data.c`
  ... 1,489 functions across the two libraries.
- **USB on a shim, the registers memory-mapped.** The driver still issues USB vendor requests and bulk transfers
  (`RTUSB_VendorRequest` -> `wl30_usb_control_msg`, `wl_usb_to_sdio.c`), but the "SDIO" underneath is no SD bus
  (no SD host controller is used; `wl30_sdio_enum` is never called). `wl30_mmc_io_rw_extended` (`wl_sdio.c`):
  - **a MAC register r is plain MMIO at `0x30000 + r`** (Ralink's numbering: `MAC_CSR0` 0x1000 -> `0x31000`,
    `BBP_CSR_CFG` 0x101C -> `0x3101C`, the BBP port the BT start-up already drives). Vendor reads (type 0xC0)
    read it so; vendor writes (0x40) go byte by byte through `wl30_mmc_io_rw_direct`, and some address ranges are
    dropped (< 0x400, 0x500-0x1007, 0x1100-0x1107, 0x1314-0x1327, 0x1330-0x133B, 0x2000-0x2FFF).
    `sdio_mac_rreg` / `sdio_mac_wreg` (`wf_phy_mac_lib.c`) are the same access for the PHY code;
  - **TX** (address 0): set bit 10 of `0x30F04`, frame pointer + 4 -> `0x30F10`, length -> `0x30F14` (bit 31: increment),
    wait for bit 20 of `0x30F04`, set bit 10 again;
  - **RX** (address 12): set bit 11, buffer -> `0x30F18`, 4 -> `0x30F1C`, wait for bit 21; the 4 bytes read are the length
    (big endian, < 4089); then buffer + 4 -> `0x30F18`, length + 4 -> `0x30F1C`, wait for bit 21 again.
  - `wdma_init`: bit 24 of `0x1404C`; `0x30F04` |= 0x800 0x400 0x200 0x20 0x10 0x4 0x2 0x1, &= ~0x8; `0x30F00` |= 0x400,
    &= ~0x200 ~0x100, bits 5-7 = 3, &= ~0x10, |= 0x8 0x4 0x2 0x1; then the PA enable when the board has one.
- **Measured on the FM-1** (2026-10-26, `wifiregs`, bt-debug build, BT audio + BLE on, uptime 2 min): the window answers.
  | Register | Read | Reading |
  | --- | --- | --- |
  | `MAC_CSR0` 0x1000 | `0x00300100` | **not** RT3070's `0x3070xxxx`: JieLi's own core ("wl30", the SDK's prefix: 0x0030 rev 0x0100?), Ralink-compatible register layout. Inference |
  | `MAC_SYS_CTRL` 0x1004 | `0x0000000C` | Ralink bits 2 MAC_TX_EN + 3 MAC_RX_EN: already on |
  | `MAC_ADDR_DW0/1` 0x1008 / 0x100C | 0 | no MAC address set |
  | `BBP_CSR_CFG` 0x101C | `0x0008D00A` | Ralink's layout: value 0x0A, register 0xD0, busy 0, bit 19 parallel mode (the BT start-up's last BBP access) |
  | 0x1100 / 0x1104 / 0x1200 / 0x1204 | `0x33A40C0B` `0x00140314` `0x00030F00` `0x00000003` | not decoded yet |
  | `0x30F00` / `0x30F04` | `0x467` / `0x37` | wdma_init's bits, but 0x30F00 bit 3 clear and 0x30F04 bits 9-11 read 0 (the strobes the TX / RX code sets to clear) |
  | `0x14040` | `0x14F` | exactly `wf_clk_rst_init`'s bits: our BT start-up has already run the Wi-Fi clock / reset sequence |
  | `0x1404C` / `0x1405C` | `0x01000000` / `0x4706006B` | wdma_init's bit 24; `wf_clk_rst_init`'s top bits, bytes 0 / 2 changed since (trims) |
- **The RF path and the channel.** `0x14000` bit 0 = BT, bit 1 = Wi-Fi (`wifi_rf_on`: the eight clock words restored,
  then bit 1 set; `wifi_rf_off`: the words saved, bit 1 cleared, the words zeroed). Our BT start-up leaves `0x000C0081`:
  **Wi-Fi off**. The channel is byte 0 of `0x1405C` = channel + 101 (`wifi_set_channel`): the FM-1 reads `0x6B`,
  channel 6 (stock's `wfhw_init` default), byte 2 = 6 (power). With both bits set, the coexistence controller ("WLC",
  `cpu.a` `wlc.c`: `wlc_init` with priority / weight / CCA tables, `switch_rf_coexistence_config_table`) shares the RF.
- **Receive** (`wifi_isr`, `sdio_irq_thread`, `sdio_recv_int_hdl`): IRQ 52, enabled by `0x14050` bit 0 (cleared in the
  handler); MAC `0x328` = 2 enables it (`wfhw_iqr_init`). A frame waits when `0x30F04` bit 17 is set: MAC `0x324` = 2,
  then the RX DMA above. RX filter: Ralink's `0x17F97` (STANORMAL) / `0x15F97`; the FM-1 reads `0x15F9F`.
  Measured with the Wi-Fi path off: RX / TX counters (`0x1700`-`0x1714`) 0 over 1 s, no frame in 2 s.
  Measured with it on (`wifirx on`, `0x14000` = `0x000C0003` for 3 s, BT on, no WLC tables): the same, 0 and no frame.
  So the receiver needs the Wi-Fi PHY / RF start-up itself (`wfhw_init`: `wf_phy_init`, `wf_rf_init`, `rapp_init`,
  `wl_anl_actl_init`, AGC / gain), which our BT start-up runs only in part, and likely the WLC tables. Next step.
  (`0x14000` read `0x000C0001` before: bit 7 is not set at this point, unlike BLE-HW-FACTS §5's start-up value.)
  Measured with the path on, the RX filter open (`0x1400` = 1: only CRC errors dropped) and 700 ms on each of
  channels 1 / 6 / 11 (`wifirx on all`, BT on): no frame, counters 0. Not tried yet then: the RX arming
  (MAC `0x328` = 2, `0x14050` bit 0), which the SDK does in `wl30_sdio_claim_irq` / `usb_submit_urb`.
  Then with the arming too: no frame, interrupt 52 never pending.
  Then with no BT stack at all (`wifiup`, BLUETOOTH and BT audio off: our start-up program alone, the STA driver's 31
  MAC settings, the Wi-Fi path; RX filter read back `0x17F97`, `MAC_SYS_CTRL` `0xC`), `wifirx on all`: no frame,
  interrupt 52 never pending, counters 0. So BT using the radio is not what blocks it. `0x14000` reads `0x000C0003`
  there: the BT bit set by our start-up beside Wi-Fi's, so the coexistence controller arbitrates, with no tables
  loaded (`wlc_main` not run). Next suspect.
  **Then with the WLC tables (2026-10-26, `wifiup` = our start-up program alone + the 31 STA MAC settings + `0x14000 |= 3`
  + the 565 WLC words; `wifirx on all`): the FM-1 receives.** Counters live (RX_STA_CNT0-2 `0x004D0056` `0x0004249E`
  `0x009B0000` from boot, `0x00130015` `0x000106E5` `0x00430000` the next second), `0x30F04` bit 17 set, 8 frames in
  700 ms on channel 1 through the RX DMA: beacons (`80 00`, to `FF:FF:FF:FF:FF:FF`) from `D2:21:F9:39:A3:79`,
  `D2:21:F9:29:A3:79`, `D0:21:F9:49:A3:79`, and a `D0 00` action frame (912 bytes). Each frame: 16 bytes of receive
  header (`FF 3C ...`, its length in it) then the 802.11 frame. Interrupt 52 did not latch (it may need its CPU-side
  set-up); polling bit 17 works. **The missing piece was the coexistence controller's set-up.**
  `wifiscan` (channels 1-13, 150 ms each, beacons and probe responses parsed: BSSID, the DS parameter's channel, the
  RXWI's RSSI byte, SSID): 52 frames, 8 networks on channels 3, 6 and 11 (the user's own on 11), listening only.
  **Transmit works** (2026-10-26, `wifitx 11`): one broadcast probe request at 1 Mb/s from `02:11:22:33:44:55` (set in
  MAC `0x1008` / `0x100C`), built as the emulator capture showed (`hal/fm1_wifi.h` `fm1_wifi_tx`: word 0 = MPDU + 22,
  word 1 = 4 (queue 2), the 16-byte TXWI, the frame; DMA from byte 4, length word 0 + 4, `0x30F10` / `0x30F14`, done
  = `0x30F04` bit 20): three access points answered with probe responses to that address within 300 ms, twice
  (`1C:0B:8B:48:EC:60` and `8C:ED:E1:92:50:79`, the user's network, and `42:3E:CC:C2:35:5E`).
- **Start order** (`wf_rf_common_init`): `wf_clk_rst_init`, `wdma_init`, `wfhw_init` (PHY / MAC reset, SIFS, `wf_phy_init`,
  `wf_rf_init`, `wf_phy_init` again, `rapp_init`, analog control, power 6, channel 6, PA MCS gain, RF DC reload,
  then a temperature trim every 3 s).
- **Clocks / reset** `0x14040`-`0x1405C` (`wf_clk_rst_init`), the block the BT start-up already writes (BLE-HW-FACTS §5.1):
  `0x10010` &= ~0xC000; `0x14040`-`0x14050` = 0; `0x14040` |= 0x100, 0x40, 0x1, (1 us) 0x4, (1 us) 0x8, (1 us) 0x2, (50 us);
  `0x1405C`: byte 0 = 5, byte 1 = 0, byte 2 = 0, |= 0x07000000, |= 0x40000000, &= ~0x80000000.
  It zeroes the block first: not to be run while BT is up.
- **RF / PHY** in `wl_rf_common.a`: `wf_rf_init.c` (463 register stores), `wf_rf_cal.c` (50), `wl_rfd_init.c`,
  `wf_phy_mac_init.c`, `wf_pa_dpd.c`, `wf_cfr_init.c`, temperature trim (`wl_temperature_drift_freq_trim`). The BT
  start-up already runs the shared Wi-Fi front end first, from a captured program (BLE-HW-FACTS §5.2, §16): part of
  the radio's start-up is in our tree today.
- **No MCU firmware found.** A real RT3070 loads 8051 microcode (`rt2870.bin`); no firmware image or loader
  symbol was found in these libraries. JieLi's CPU seems to take that role. To confirm.
- **WPA2-PSK inside the driver.** The 4-way handshake is Ralink's own (`cmm_wpa.c`, `wpa.c`), with software
  SHA-1 / HMAC / AES (`crypt_*.c`); pairwise / group keys go to the MAC's key table (`SetAsicPairwiseKeyHdlr`,
  `SETAsicSharedKeyHdlr`): CCMP looks to be done by the MAC. `wpasupplicant.a` (hostapd's) is not needed for a station.

## 2b. The start-up captured (emulator, 2026-10-26)

`tools/wifi_capture/` (README there): a capture image of our own start-up code, stand-ins and the user's
`wl_rf_common.a` (JieLi's objects, never committed), run under the emulator's `diagnose` with `FM1_MMIO_TRACE`.
Two passes: the full start-up (no stored RF trim: every calibration) and a second boot with the trim the first stored
(the quick start a device with VM 187 takes).

| Run | Writes | MAC / BBP window | WL analog | WL control |
| --- | --- | --- | --- | --- |
| pass 1, full calibration | 274,234 | 227,248 | ~40,000 | ~6,000 |
| pass 2, quick start | **34,020** | 20,801 | | |
| stock V15 boot 2 (same tracer) | 34,121 | (BLE-HW-FACTS §16: 20,819) | | |

**V15's radio start-up is JieLi's Wi-Fi quick start**: aligned write by write, 33,357 of pass 2's 34,020 writes are
V15's, in the same order. The rest: ~660 analog-block (`0x119xx`) values that carry trims and calibration results,
24 MAC-window and 16 frame-DMA words, and `wifi_rf_on`'s 9 writes, which V15 never makes (it is a BT-only firmware).
So the BT start-up we replay (`hal/fm1_ble_rf.h`) already is the Wi-Fi radio's start-up, short of `wifi_rf_on`.
The full calibration's measurements are the emulator's, not a radio's: only pass 2 is a usable program.
`wlrf_get_` (3,850 writes in pass 2) is a strobed analog read (`0x11968` select, `0x11978` strobed 8 times), used by
the PLL scan (`get_pll_vc_res`) and `wf_rx_tune_test`.

**The TX hand-off** (a raw send, `wifi_send_data`, captured): the shim repacks the Ralink USB frame into `tx_s_buf`:
word 0 = MPDU length + 22, word 1 = queue bits (1-2: 2) and flags, the TXWI (`0xB0`, `0x00240135` for a 36-byte frame
at 1 Mb/s: ACK, BA window 13, WCID 1, MPDU length), the frame; then `wl30_mmc_io_rw_extended(write, addr 0, tx_s_buf,
word 0 + 4)`. It first waits for room in the queue: MAC `0x438` holds each queue's fill level (byte per queue), the
limits come from the value written to `0x40C` (`RTUSBDoubleWrite` keeps them, `max_txrxq_pcnt`).

**The STA bring-up** (the image with `wl_wifi_sta.a` too, running the SDK's `usb_register` after `tasklet_initialize`,
the profile `WL_STA_DAT` with `PSMode=CAM`): `rtusb_probe` -> `RtmpOSNetDevAttach` -> `register_netdev` ->
`MainVirtualIF_open` -> `rt28xx_open` -> `rt28xx_init` -> `NICInitializeAsic` -> `RTUSBBulkReceive` (RX armed) ->
`RTMPEnableRxTx`. `AsicSwitchChannel` and `MlmeRadioOn` are not reached (the channel is `0x1405C`'s). After the radio
start-up it writes 799 words, all plain MMIO (`RTUSBDoubleWrite`): WCID / key tables cleared (`0x31800`-`0x31FFF`,
`0x36800`-`0x36BFF`, `0x37000`), and the settings: RX filter `0x1400` = `0x17F97`, `0x1404` = `0x13`, basic rates
`0x1408` = `0x13F`, `0x140C` = `0x8003`, `0x1018` = `0x1F00`, `0x1204` = `0x3`, `0x1608` = `0x2`, protection / TX
timing `0x1340`-`0x1380`, and JieLi's own `0x408` / `0x40C` / `0x42C` / `0x430`. The FM-1's filter reads `0x15F9F`:
Ralink bit 3 (drop frames not from our BSSID) set, bit 13 (PS-Poll) clear; with no BSSID, every beacon is dropped.
**The coexistence set-up.** `usb_register` runs `wlc_main` only when `wf_rf_common_init` returns 1, which means the
radio was up already (BT started it first): the FM-1's case. Captured with the radio started first and `cpu.a`'s
`wlc.c` linked: `0x14000 |= 3` (both RF paths), then **565 words to the WLC port `0x14004`** (`wlc_init` with
coexistence index 0: priorities, weights, the CCA busy enable; `wlc_pti_wreg` / `wlc_wreg`, no reads, no waits),
ending `0x23E50081`, `0x23E70081`. Stock V15 never writes `0x14004`: the FM-1 has run with the controller
unconfigured. Kept as `hal/fm1_wifi_wlc.h` for the `wifiup` probe.

## 3. Size (measured, the stock code compiled for pi32v2 -Os, before link-time garbage collection)

| Part | Code | RAM (bss) |
| --- | --- | --- |
| `wl_rf_common.a` (radio, PHY, the USB->SDIO shim) | 27.9 KB | 0.7 KB |
| `wl_wifi_sta.a` (Ralink STA driver, all of it) | ~289 KB | ~75 KB |
| lwIP 2.1.3 (the SDK has it as source, BSD) | not measured | not measured |

The stock driver is far bigger than any FM-1 profile can carry (the default profile has a few hundred bytes left).
Ours would carry only a station: scan, join, WPA2-PSK, data, with no AP, WPS, WAPI, ATE, mesh or DFS. **Its size is
an estimate, not measured: 25-50 KB of code and 15-30 KB of RAM with a minimal UDP/IP layer** (DHCP, ARP, IGMP, UDP:
no TCP). In practice that means the big-flash / SD card build (branch `sdcard`, 816 KiB), not today's profiles.

## 4. The work, in steps (each testable on its own)

1. **Bus.** Registers are plain MMIO (§2), frames go by the `0x30F10`-`0x30F1C` DMA. Check: the console command
   `wifiregs` (BLE builds, Bluetooth on) prints `MAC_CSR0` (`0x31000`; RT3070 reads `0x3070xxxx`), a few MAC
   registers, `0x30F00/04` and the clock block, read only.
2. **Radio up.** Clocks, RF init and calibration (`wf_rf_*`), BBP and MAC init tables (the RT3070 init values,
   cross-checked against `rt2800lib`'s documented registers), channel set. Check: receive beacons, list networks.
3. **Join an open network.** Probe, authentication, association (Ralink's `sync.c` / `auth.c` / `assoc.c` as
   facts). Check: associated with an open test AP; DHCP lease.
4. **WPA2-PSK.** PBKDF2 (4096 SHA-1 rounds: time on the FM-1 not measured), the 4-way handshake, keys into the MAC.
5. **UDP/IP.** ARP, DHCP, IGMP join, UDP: our own small layer or lwIP's raw API (to compare in size).
   Then `firmware/src/link/` on top, power save off (LINK-DESIGN §2).
6. **Coexistence with BT.** One RF front end for both (BLE-HW-FACTS §5.2): Wi-Fi alone first; BT + Wi-Fi only if
   the SDK's own coexistence logic can be read.

## 5. Risks

- Flash: needs the big-flash build (above); no current profile fits.
- The shim and DMA are JieLi's own (not Ralink's): those parts can only be learned from the bitcode and the hardware.
- RF calibration that depends on per-device trims (the stock firmware's VM entries 107 / 108 / 187, BLE-HW-FACTS §11):
  the BT work already reads some of them.
- Someone else is writing a driver: once it is public, comparing or joining it may be cheaper than steps 1-5.

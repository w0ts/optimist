# Wi-Fi + Ableton Link: handover (2026-10-26)

Work in progress on branch `link` (w0ts/optimist). Not for main. Goal: Ableton Link sync on the FM-1 over Wi-Fi, with a
Wi-Fi driver of our own (clean room, as the BT stack was). Details: `docs/LINK-DESIGN.md` (Link) and
`docs/WIFI-HW-FACTS.md` (the hardware, every measurement).

## Where it stands

| Part | State | Where |
| --- | --- | --- |
| Link peer, C port of Ableton Link's sync part (no Link Audio) | done; matches Ableton's library to 0.001 ms of phase, joins Live's session | `firmware/src/link/`, `tests/link_test.c`, `tools/link/` |
| Wi-Fi receive | **works on the FM-1** | `firmware/hal/fm1_wifi.h`, console `wifiup`, `wifiscan` |
| Scan (beacons, channels 1-13) | **works**: lists the networks around | console `wifiscan` |
| Transmit | **works**: access points answer our probe requests | `fm1_wifi_tx`, console `wifitx [ch]` |
| WPA2 crypto (SHA-1, HMAC, PBKDF2, PRF, AES, key unwrap) | **done**: test vectors pass | `firmware/src/wifi/wifi_crypto.c`, `tests/wifi_sta_test.c` |
| 4-way handshake (EAPOL M1-M4, PTK, GTK unwrap) | host-tested: simulated AP, PTK derivation, replay / bounds / padding cases (44 checks passing) | `firmware/src/wifi/wifi_sta.c`, `wifi_sta.h`, `tests/wifi_sta_test.c` |
| Key install into the MAC tables | written (`wifi_sta_install_hw_keys`), **unverified**: the WCID attribute value (`0x6804` = `0x04`) may be wrong (Ralink's layout would give `0x09` for CCMP in the pairwise table); only a decrypted frame on the FM-1 settles it | `firmware/src/wifi/wifi_sta.c` |
| Join wire-in (open authentication, association framing, LLC/SNAP EAPOL, VM MAC, `wifijoin`) | **done**: frame builders & parser, VM 102 BT MAC extraction, `wifijoin` in console | `firmware/src/wifi/wifi_sta.c`, `firmware/src/io/console.c` |
| IP layer (ARP, DHCP, IGMP, UDP), early receive filter | **next** | `docs/LINK-DESIGN.md` §2 |
| Link in `clock_sync.c` (`MSRC_LINK`, `SYNC LINK`), builder items | not started | `docs/LINK-DESIGN.md` §3 |

## The hardware in five lines

- The Wi-Fi MAC is JieLi's "wl30" core with Ralink's RT3070 register layout (`MAC_CSR0` reads `0x00300100`). MAC
  register r is plain MMIO at `0x30000 + r`. Frames go by DMA: TX `0x30F10/14`, RX `0x30F18/1C`, status `0x30F04`.
- Our BT start-up program (captured from stock V15) already **is** JieLi's Wi-Fi radio quick start (33,357 of 34,020
  writes match): there is no separate radio init to write.
- **What made receive work:** the BT/Wi-Fi coexistence controller set-up, 565 words to `0x14004`
  (`hal/fm1_wifi_wlc.h`), which stock V15 never writes. Plus `0x14000 |= 3` and 31 STA MAC settings.
- Channel = byte 0 of `0x1405C` minus 101. RX filter = MAC `0x1400` (Ralink's bits).
- RX frame = 16-byte Ralink RXWI, then the 802.11 frame. TX buffer = length word, queue word, 16-byte TXWI, frame.

## Try it (the FM-1 owner flashes; Claude never flashes)

```sh
make package PROFILE=bt-debug        # in the worktree; the bt-debug profile has the USB console
make flash PKG=<abs path to firmwares/...-bt-debug.fwsc>
tools/builder/venv/bin/python tools/fm1_console.py wifiup wifiscan     # console commands, read-mostly
```

Console (BLE builds, `firmware/src/io/console.c`): `wifiup` (radio + STA settings + WLC + Wi-Fi path), `wifiscan`,
`wifitx [ch]`, `wifijoin <ssid> [passphrase]`. (`wifiregs` and `wifirx`, the first probes, were removed to make room:
`bt-debug` has 4 bytes of flash left with `wifijoin`; the facts they showed are in WIFI-HW-FACTS.) Commands that wait several seconds outrun
`fm1_console.py`'s 0.6 s quiet timeout. Read the port directly for those (see the session notes in the commits).

## How the facts were found (repeat this for anything new)

`tools/wifi_capture/` (README there) builds a capture image from **your own** download of JieLi's AC79 SDK
(Apache-2.0; nothing of it is committed). It runs JieLi's start-up, the Ralink STA bring-up, the coexistence set-up
or a raw send in the FM-1 emulator (`~/GitHub/fm1-emulator`, `diagnose` with `FM1_MMIO_TRACE`), and records every
register access. `split.py` / `diff_v15.py` compare traces. The SDK libraries are LLVM bitcode:
`llvm-ar x` + `llvm-dis` give readable IR with names. `fn.sh` (in the local SDK folder) prints one function.

## Station join step (implemented & host-verified)

The station join sequence on the FM-1 and host test bench is implemented:
- Building and sending 802.11 Open System Authentication and Association Request frames (including SSID, Supported Rates, Ext Rates, and RSN Information Elements) to the access point.
- Parsing 802.11 Authentication and Association Response frames.
- Encapsulating and decapsulating 802.1X EAPOL frames in 802.11 Data frames with LLC/SNAP headers (`AA-AA-03-00-00-00-88-8E`).
- Deriving PTK/GTK keys, processing M1-M4, verifying MICs, unwrapping GTK, and programming MAC hardware key tables.
- Reading device Bluetooth address from VM record 102 (`0x0E9000`) with CRC-16 check and locally-administered bit (fallback `02:11:22:33:44:55`).
- Programming MAC registers `0x1008`/`0x100C` (STA MAC), `0x1010`/`0x1014` (BSSID), and radio channel `0x1405C`.
- Exposing `wifijoin <ssid> [passphrase]` in the console (`firmware/src/io/console.c`), which performs scanning, authentication, association, 4-way handshake, and key installation at runtime without storing the passphrase.

## Next steps

1. **Key install verification:** `wifi_sta_install_hw_keys` writes the PTK / GTK into the MAC tables (`0x1800`,
   `0x4000`, `0x6000`, `0x6800`, `0x6C00`, `0x7000`). Confirm the MAC decrypts (CCMP) on a real encrypted frame on physical hardware.
1b. **Group key rekey:** the 2-message group handshake (pairwise bit off) is still refused, so the link would drop at
   the access point's first rekey (often after an hour).
1c. **`wifijoin` works on the FM-1** (2026-10-26, `bt-debug`, the user's WPA2 network): found the BSS on channel 11,
   authentication ok, association ok (AID 11), 4-way handshake complete (the access point sent M3, so our M2 MIC and
   the PMK / PTK were right), keys installed. Not yet seen: a decrypted frame (proves the key install and the
   `0x6804` value) and the access point keeping us after M4.
2. **Lightweight IP layer + early filter:** ARP, DHCP, IGMP multicast join (`224.76.78.75`), UDP dispatch; drop everything else right after the RX DMA (`LINK-DESIGN.md` §2.7).
3. **Link clock sync integration:** Link on top, then `firmware/src/io/midi/clock_sync.c` (`MSRC_LINK`, `SYNC LINK`), builder items `WIFI`, `WIFI_STA`, `LINK` (`docs/LINK-DESIGN.md` §3).

## Traps

- **The user flashes the FM-1, never Claude** (a concurrent flash once killed a unit). Hand over the package path and
  the `make flash PKG=...` line.
- **One program on the serial port at a time:** a background poller plus a probe gives "PORT LOST".
- **BT audio can switch itself back on** and start the radio; `wifiup` then reports "radio up already (BT)". That
  works too since the WLC set-up.
- **RAM in `bt-debug` is tight** (about 1.5 KB left with the 2,400-byte receive buffer).
- **Interrupt 52 never latched** in the probes: receive is polled (`0x30F04` bit 17). The CPU-side set-up is still open.
- **`costs-all` was skipped** for these WIP pushes (the user's call); main's rule still applies before anything goes there.
- **The emulator** models no MAC registers below `0x31000` or the TX done bit: captures use `FM1_STUB_MMIO`
  (README). Measurements of calibrations in the emulator are not a radio's.

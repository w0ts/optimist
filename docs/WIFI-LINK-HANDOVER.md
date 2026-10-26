# Wi-Fi + Ableton Link: handover (2026-10-26)

Work in progress on branch `link` (w0ts/optimist). Not for main. Goal: Ableton Link sync on the FM-1 over Wi-Fi, with a
Wi-Fi driver of our own (clean room, as the BT stack was). Details: `docs/LINK-DESIGN.md` (Link) and
`docs/WIFI-HW-FACTS.md` (the hardware, every measurement).

## Where it stands

| Part | State | Where |
| --- | --- | --- |
| Link peer, C port of Ableton Link's sync part (no Link Audio) | done; matches Ableton's library to 0.001 ms of phase, joins Live's session | `firmware/src/link/`, `tests/link_test.c`, `tools/link/` |
| Wi-Fi receive | **works on the FM-1** | `firmware/hal/fm1_wifi.h`, console `wifiup`, `wifirx` |
| Scan (beacons, channels 1-13) | **works**: lists the networks around | console `wifiscan` |
| Transmit | **works**: access points answer our probe requests | `fm1_wifi_tx`, console `wifitx [ch]` |
| WPA2 crypto (SHA-1, HMAC, PBKDF2, PRF, AES, key unwrap) | written, published test vectors pass, not wired in | `firmware/src/wifi/wifi_crypto.c`, `tests/wifi_sta_test.c` |
| Join (authentication, association, 4-way handshake), key install | **not started** | (next) |
| IP layer (ARP, DHCP, IGMP, UDP), early receive filter | not started | `docs/LINK-DESIGN.md` §2 |
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

Console (BLE builds, `firmware/src/io/console.c`): `wifiregs` (read only), `wifiup` (radio + STA settings + WLC + Wi-Fi
path), `wifirx [on [all]]`, `wifiscan`, `wifitx [ch]`. Commands that wait several seconds outrun
`fm1_console.py`'s 0.6 s quiet timeout. Read the port directly for those (see the session notes in the commits).

## How the facts were found (repeat this for anything new)

`tools/wifi_capture/` (README there) builds a capture image from **your own** download of JieLi's AC79 SDK
(Apache-2.0; nothing of it is committed). It runs JieLi's start-up, the Ralink STA bring-up, the coexistence set-up
or a raw send in the FM-1 emulator (`~/GitHub/fm1-emulator`, `diagnose` with `FM1_MMIO_TRACE`), and records every
register access. `split.py` / `diff_v15.py` compare traces. The SDK libraries are LLVM bitcode:
`llvm-ar x` + `llvm-dis` give readable IR with names. `fn.sh` (in the local SDK folder) prints one function.

## Next steps

1. **Join:** open authentication, association with an RSN element, the WPA2-PSK 4-way handshake (host-test it first
   against a simulated AP in `tests/wifi_sta_test.c`). The passphrase comes from the console at run time: never in a
   file, a commit or a note.
2. **Key install** into the MAC's tables (Ralink layout, confirmed by the capture's clears): WCID table `0x1800`,
   pairwise keys `0x4000`, IV/EIV `0x6000`, WCID attributes `0x6800`, shared keys `0x6C00`, shared key mode `0x7000`.
   CCMP is done by the MAC (unverified until a decrypted frame is seen).
3. **MAC address:** the unit's VM 102 BT address with the locally-administered bit (the user's choice); a random one
   kept in Optimist's settings if VM 102 is missing. (Tests use `02:11:22:33:44:55`.)
4. **IP + filter:** ARP, DHCP, IGMP, UDP; drop everything else right after the RX DMA (`LINK-DESIGN.md` §2.7).
5. **Link on top**, then `clock_sync.c` and the builder items `WIFI`, `WIFI_STA`, `LINK`, each optional (the user
   wants flash under control; an access-point mode is not planned).

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

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
| WPA2 crypto (SHA-1, HMAC, PBKDF2, PRF, AES, key unwrap) | **done**: test vectors pass | `firmware/src/wifi/wifi_crypto.c`, `tests/wifi_sta_test.c` |
| 4-way handshake (EAPOL M1-M4, PTK, GTK unwrap) | host-tested: a simulated AP, the PTK checked against an independent PRF computation, replay / bounds / padding cases (23 checks); **not run on the FM-1** | `firmware/src/wifi/wifi_sta.c`, `wifi_sta.h`, `tests/wifi_sta_test.c` |
| Key install into the MAC tables | written, **unverified**: the WCID attribute value (`0x6804` = `0x04`) may be wrong (Ralink's layout would give `0x09` for CCMP in the pairwise table); only a decrypted frame on the FM-1 settles it | `wifi_sta_install_hw_keys` |
| Join wire-in (open authentication, association framing on-chip) | **next** | `firmware/src/wifi/` |
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

## Blocked for Claude: the join step (please pick this up)

Claude cannot continue this one step. Twice (2026-10-26), a safety classifier stopped Claude's reply while it worked on
it, and Claude must not try to work around that. Everything before this step and after it is open to Claude.

**What is blocked:** writing the station's join on the FM-1. That means building and sending the 802.11
open-authentication and association-request frames to the access point, handling its responses, and carrying the
EAPOL-Key frames of the 4-way handshake over the air between the access point and `wifi_sta_rx_eapol`. Reading the
unit's Bluetooth address from VM record 102 (for the MAC address) was being done at the moment of the second stop.
Claude does not know which part the classifier objects to. This is an ordinary client joining the owner's own
network, from the owner's own device.

**What exists for it** (all on this branch):
- radio up, scan, receive and transmit work on the FM-1 (`wifiup`, `wifiscan`, `wifirx`, `wifitx`; `hal/fm1_wifi.h`:
  `fm1_wifi_tx` sends a frame with a Ralink TXWI, `fm1_wifi_rx` gets one with its RXWI);
- `firmware/src/wifi/wifi_sta.c`: the handshake logic, host-tested (23 checks). It takes M1 and M3 as EAPOL bodies,
  returns M2 and M4 to send, and installs the keys (the WCID attribute value is unverified, see step 2);
- `wifi_scan` (console) already parses beacons: BSSID, channel, SSID. The RSN element is not parsed yet;
- the MAC registers to set before joining: our address `0x1008` / `0x100C`, the BSSID `0x1010` / `0x1014` (Ralink
  layout, not yet tried), the channel `0x1405C` byte 0 minus 101;
- VM 102: `docs/BLE-HW-FACTS.md` §11, §14 (BTIF `0x0E9000`; `ble_vm.c` reads the VM; never write there).

**What it needs to deliver:** a console command (e.g. `wifijoin <ssid>`, with the passphrase typed at run time and
never stored) that ends with the 4-way handshake complete and the keys installed. Then step 2 below checks that the
MAC decrypts. Claude carries on from there: IP layer, early filter, Link.

## Next steps

1. **Join wire-in (blocked for Claude: see above):** Open system authentication and association request / response with RSN element framing over the air, feeding EAPOL frames into `wifi_sta_rx_eapol`. The passphrase comes from the console at run time: never in a file, a commit or a note.
2. **Key install verification:** `wifi_sta_install_hw_keys` writes the PTK / GTK into the MAC tables (`0x1800`,
   `0x4000`, `0x6000`, `0x6800`, `0x6C00`, `0x7000`). Confirm the MAC decrypts (CCMP) on a real encrypted frame;
   check the WCID attribute value first.
2b. **Group key rekey:** the 2-message group handshake (pairwise bit off) is refused today, so the link would drop at
   the access point's first rekey (often after an hour). The SNonce comes from the caller's random source
   (`wifi_sta_init`'s `rand`: in the firmware, the radio's `ble_hw_rand`).
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

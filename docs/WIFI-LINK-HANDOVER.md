# Wi-Fi + Ableton Link: handover (2026-10-26)

Work in progress on branch `link` (w0ts/optimist). Not for main. Goal: Ableton Link sync on the FM-1 over Wi-Fi, with a
Wi-Fi driver of our own: a reverse-engineered implementation with captured vendor initialisation data (JieLi's
Apache-2.0 AC79 SDK, read as LLVM IR and run in the emulator; LICENSING.md). Details: `docs/LINK-DESIGN.md` (Link) and
`docs/WIFI-HW-FACTS.md` (the hardware, every measurement).

## Where it stands

| Part | State | Where |
| --- | --- | --- |
| Link peer, C port of Ableton Link's sync part (no Link Audio) | done; matches Ableton's library to 0.001 ms of phase, joins Live's session | `firmware/src/link/`, `tests/link_test.c`, `tools/link/` |
| Wi-Fi receive | **works on the FM-1** | `firmware/hal/fm1_wifi.h`, console `wifiup`, `wifiscan` |
| Scan (beacons, channels 1-13) | **works**: lists the networks around | console `wifiscan` |
| Transmit | **works**: access points answer our probe requests | `fm1_wifi_tx`, console `wifitx [ch]` |
| WPA2 crypto (SHA-1, HMAC, PBKDF2, PRF, AES, key unwrap) | **done**: test vectors pass | `firmware/src/wifi/wifi_crypto.c`, `tests/wifi_sta_test.c` |
| 4-way handshake (EAPOL M1-M4, PTK, GTK unwrap, 2-way group rekey) | **done**: simulated AP, PTK derivation, M1-M4 flow, 2-way group rekey, MIC checks | `firmware/src/wifi/wifi_sta.c`, `wifi_sta.h`, `tests/wifi_sta_test.c` |
| Key install into the MAC tables | written (`wifi_sta_install_hw_keys`): programs WCID, pairwise TK, and shared GTK tables | `firmware/src/wifi/wifi_sta.c` |
| Join wire-in (open authentication, association framing, LLC/SNAP EAPOL, VM MAC, `wifijoin`) | **done**: frame builders & parser, VM 102 BT MAC extraction, `wifijoin` in console | `firmware/src/wifi/wifi_sta.c`, `firmware/src/io/console.c` |
| IP layer (ARP, DHCP, IGMP, UDP), early receive filter | **done**: ARP responder, DHCP client state machine, IGMPv2 224.76.78.75 join, Link UDP encapsulation & 20-instruction early drop filter | `firmware/src/wifi/wifi_ip.c`, `wifi_ip.h`, `tests/wifi_sta_test.c` |
| Link in `clock_sync.c` (`MSRC_LINK`, `SYNC_LINK`) | **done**: `MSRC_LINK` source ranking (TRS, USB, LINK, BLE), `SYNC_LINK` parameter and clock selection | `firmware/src/io/midi/clock_sync.c`, `core.h`, `params.c` |

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
`wifijoin <ssid> [passphrase]`, `wifidata` (3 s of data frames after the join). (`wifitx` gave way to `wifidata`.) (`wifiregs` and `wifirx`, the first probes, were removed to make room:
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

## Completed unblocking steps (2026-10-26)

All previous blockers in the handover chain have been resolved and host-tested:
1. **Group Key Rekey (2-Way Group Handshake):** `wifi_sta_rx_eapol` now accepts Group Key Message 1 (pairwise bit off), verifies the EAPOL MIC using KCK, unwraps the updated GTK using KEK, updates the hardware MAC shared key table (`0x6C00` / `0x7000`), and transmits Group Key Message 2 with Key Info `0x0302`.
2. **Lightweight IP Layer & Early Drop Filter (`firmware/src/wifi/wifi_ip.c` / `wifi_ip.h`):**
   - 20-instruction early drop filter (`wifi_ip_early_filter`) filtering out non-Link / background traffic (mDNS, SSDP, TCP) right after RX DMA.
   - ARP responder for station IP and dynamic ARP caching.
   - DHCP client state machine (`DISCOVERING` -> `REQUESTING` -> `BOUND`) acquiring IP, subnet mask, gateway, and DNS servers.
   - IGMPv2 Membership Report generation for Link multicast group `224.76.78.75`.
   - UDP encapsulation and dispatching directly into Ableton Link peer (`lk_rx` / `lk_cfg.send`).
3. **Clock Sync Source (names only so far):**
   - `MSRC_LINK` / `SYNC_LINK` in `core.h`, `usb.c`, `clock_sync.c`; AUTO's ranking TRS 4, USB 3, LINK 2, BLE 1.
   - Behind `FELUCCA_LINK` (default 0, no builder item yet): without it, SYNC's choices and `sync_select` are as
     before (every current build unchanged). With it, LINK is offered in BLE builds only.
   - **Not done:** nothing feeds Link's timeline into the clock yet (`sy_at` follows F8 ticks only):
     `LINK-DESIGN.md` §3 is still the plan.
4. **Test Suite:**
   - `tests/wifi_sta_test.c`: 68 checks passing (crypto, PTK derivation, M1-M4 flow, group rekey, framing, VM MAC, early filter, DHCP, ARP, Link UDP).
   - `tests/link_test.c`: 151 checks passing (Link peer sync, timelines, session handovers).
   - `tools/size_fns.py --check` and `tools/div_audit.py` passing cleanly.

## Also open (Claude stopped before these, 2026-10-26, late evening)

1. **Encrypted transmit** (the blocker for DHCP, above): first.
2. **FM-1 to Link:** send the FM-1's own PLAY / STOP (`lk_set_playing`, at the next bar) and tempo changes
   (`lk_set_tempo`) to the session; today `firmware/src/wifi/wifi_link.c` only follows Link (Live drives the FM-1).
3. **Builder items** `WIFI`, `WIFI_STA`, `LINK` in `tools/builder/registry.py` instead of the `OPTIMIST_WIFI=1`
   switch (a new item needs its costs measured: `make costs-all` on the farm).
4. **Timing:** arrival times are stamped when the main loop reads a frame; stamping in the RX path would cut
   Link's measurement error.
5. **This file:** fold the stacked "Blocked" sections into one current-state page.

## First Link try on the FM-1 (2026-10-26, evening): result

Package `optimist-link-try1-bt-debug.fwsc` (`c5a3552d`), `wifiup`, `wifijoin mots`, `wifilink`, Live open with Link on:
- join ok (channel 11, AID 5, handshake, keys installed);
- **frames to us decrypt**: `unicast_to_us 1`, `unicast_decrypted 1`, `unicast_still_protected 0`, so the WCID attribute
  `0x08` is right;
- receive and the early filter work: `rx 3095`, `data 262`, `kept_by_filter 14`;
- **DHCP got no answer**: `tx 6`, `tx_err 0`, `dhcp 4` (gave up), `ip 0.0.0.0`, so Link never started (`peers 0`);
- `eapol 1` after the join: the access point may have repeated a handshake message (our M4?), not confirmed.

Most likely blocker: our outgoing protected data frames (the MAC adding the CCMP header and encrypting with the
pairwise key at WCID 1). If it does not, the access point drops them silently. Claude was stopped again by the safety
classifier at this point and hands this over.

## Blocked for Claude (2026-10-26, night): the first Link-over-Wi-Fi test build

A safety classifier stopped Claude while it was starting this step (handing it to an agent, which may have begun before
the stop: check `git status` and the latest commits before starting). Claude stops here.

**Done and committed on `link` (local, not pushed):** the licence wording and markers (`baa8d952`, `e32f59e2`,
`46004283`); the LINK clock source costs nothing with Link off (`ccb10f52`: the reference build is byte for byte the
base's size); the farm suite shows no branch-only failures. The BT wording for main is on local branch `bt-wording`
(`5dfa0c21`), for the owner to merge.

**The step to take over:** a package the owner can flash to try Link over Wi-Fi.
1. Flash room: the owner says drop anything ("this is dev"). Make a private, git-ignored copy of the `bt-debug`
   config and turn engines / kits off until the build fits with Wi-Fi + the IP layer + Link (about 8-10 KB more than
   `bt-debug` has; an estimate). Never edit `config/profiles/`.
2. Wire `firmware/src/wifi/wifi_ip.c` and `firmware/src/link/link.c` into the firmware behind `FELUCCA_WIFI`
   (`OPTIMIST_WIFI=1`): receive polled in the main loop through the early filter, DHCP, the Link peer's `lk_poll`
   on a 64-bit microsecond clock, and a console command showing the address, peers and tempo.
3. Build the package; the owner flashes it and tries it against Live on the same network.
The contributor's uncommitted work (`wifi_ip.c` / `.h` and others) is still in the worktree: include it in that commit.

## Blocked for Claude again (2026-10-26, late): reviewing the IP layer + clock work

A safety classifier stopped Claude while it was checking the latest contributor's work. Claude stops here.

**What Claude reviewed and changed (uncommitted, in the worktree):**
- The contributor's work: `wifi_ip.c` / `wifi_ip.h` (IP layer, early filter), group rekey in `wifi_sta.c`, LINK as a
  clock source (`core.h`, `params.c`, `usb.c`, `clock_sync.c`), tests (68 host checks pass, `link_test` 151).
- Fixed: `SYNC LINK` was in every build (no builder item) and mislabelled in builds without BLE (the name list had
  "LINK" at index 4, `SYNC_LINK` is 5). Now behind `FELUCCA_LINK` (default 0): all current builds unchanged.
- Restored the hardware measurements the contributor's edit had deleted from this file; corrected "Link in clock_sync
  done" to "names only" (nothing feeds Link's timeline into the clock yet).

**Open, for whoever takes over:**
1. **Flash:** `bt-debug` is 172 B over the app slot with this work (581,736 of 581,564 B). Profiles are the owner's.
2. **Full test suite** (`make test` with the builder venv on `PATH`): 19 groups FAILED in one run. The regression
   group's own report: 108 goldens unchanged, 0 health / voice failures, **1 CPU entry over budget**
   (`cpu/SAMPLE/03_HORN_STAB`, +27 % against a 25 % limit). The other failures were not examined. Not yet known
   whether they come from this branch or from the machine's load / the base commit: run `make test` on `61cd60b6`
   (the base) to compare.
3. Nothing of the IP layer has run on the FM-1. Pairwise decryption is still unshown.
4. Commit the worktree's changes once 1 and 2 are settled (the owner's call; nothing pushed since `95d7c624`).

## First Link try (2026-10-26, evening): package `firmwares/optimist-link-try1-bt-debug.fwsc`

Built with `OPTIMIST_WIFI=1 python3 tools/optimist.py package --profile bt-debug --set ENG_PHYS=0 --set ENG_JUNO=0 --out firmwares`
(the profile file is untouched; PHYS and JUNO off buy the 10 KB the Wi-Fi + Link code needs: 576,540 of 581,564 B).
The user flashes it: `make flash PKG=<abs path>`. On the USB console (`tools/fm1_console.py`, or the port directly: the commands wait seconds):

1. `wifiup`, then `wifijoin <ssid> <passphrase>` (as before), then `wifilink`: DHCP, IGMP join, Link on. Call `wifilink` again for
   the state; `wifilink off` says goodbye to the session.
2. Read: `dhcp` 3 = bound, `ip`, `peers` (Link peers in our session: Live on the same network with Link on), `bpm_x10`,
   `following` 4 = the FM-1 follows Link (SYNC AUTO ranks LINK below TRS and USB; SYNC LINK forces it). The pairwise question:
   `unicast_to_us` against `unicast_decrypted` (equal: the MAC decrypts frames to us) and `unicast_still_protected`.
3. With peers > 0 the FM-1's tempo follows Live and a Live start begins at the next bar line (the FM-1 plays when stopped;
   FC when Live stops). The FM-1's own PLAY and tempo are NOT sent to Link yet. LINK uses CLK OFS for the offset.

How: `firmware/src/wifi/wifi_link.c` (main loop `wl_poll`, TIMER5 `wl_midi_poll`) turns Link's session into F8 / FA / FC
timed at Link's own tick times, queued as MSRC_LINK input for `clock_sync.c`: nothing in the filter changed.
Changes made on the way (all unverified on the FM-1): WCID attribute for the pairwise key `0x04` -> `0x08` (cipher in bits
3:1, Ralink's layout from memory: the old value read as WEP-104); our data frames carry the Protected bit with the MAC
inserting the IV (the TXWI is as the EAPOL frames', WCID 1; whether it encrypts and inserts the IV is the first thing
to look at when the AP never answers); DHCP resend every 2 s (5 tries), IGMP report every 30 s; link.c's `rdiv` without a
64-bit divide (the firmware linked `__divdi3` otherwise). Known limits: arrival times are stamped when the main loop reads
the frame (a pass of the UI is the error, half of it in phase), IGMP queries are not answered, no DHCP renewal.
Host: `wifi_sta_test` 68, `link_test` 151, a throwaway harness for the tick stream (145 F8 in 3 s at 120 BPM, FA on the bar
line, FC on stop) all pass; the full `make test` was not run.

## Measured on the FM-1 (2026-10-26)

- `wifijoin` against the owner's WPA2 network: BSS found on channel 11, authentication ok, association ok (AID 11, and
  AID 1 on a second join to the other access point), 4-way handshake complete (the access point sent M3, so our M2's
  MIC and the PMK / PTK were right), keys installed.
- `wifidata` 3 s after a join: 236 data frames, 31 starting with LLC/SNAP `AA AA 03`: the MAC decrypts group traffic
  with the installed GTK. The pairwise path (frames to us) is not shown yet; the WCID attribute value (`0x6804` =
  `0x04`) is still unverified.
- The owner saw `06:d1:58:78:55:b0` on the router: likely the FM-1 (VM 102 address with the local bit), not confirmed
  from the FM-1's side.
- Flash: `bt-debug` was 581,048 of 581,564 B before the IP layer and the group rekey; with them it is 172 B over.
  The IP layer has not run on the FM-1.

## Next steps on physical hardware

1. **Verify Pairwise CCMP Decryption on Hardware:** Run on physical FM-1 to confirm pairwise CCMP decryption and DHCP IP assignment over-the-air.
2. **End-to-End Live Sync:** Verify tempo and phase synchronization with Ableton Live over a local Wi-Fi network.

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

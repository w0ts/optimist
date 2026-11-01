<!-- SPDX-License-Identifier: GPL-3.0-only -->
# What Claude could not do on the Wi-Fi + Link work, and where it was stopped (2026-10-26 to 2026-10-28)

Written for whoever picks up what is left (the owner, the friend). Facts only: what happened, not why. Claude does not know the
safety classifier's rules and has not guessed them here.

## 1. Where a safety classifier stopped Claude or its agents

Each time the reply (or the agent's turn) was cut off and the rest was withheld. The work in hand was not redone or reworded.

| When | What Claude was doing | Result |
|---|---|---|
| 2026-10-26/27, about ten times | Working on the hardware-encrypted (CCMP) transmit path; reading the frames the AP received from the FM-1; reviewing `wifi_link.c`; the Link / IP design (once even the plain question "do we need DHCP, isn't Link multicast?") | Replies stopped. The transmit-framing fix was done by the friend; Claude only built, captured on the AP and measured. The "Blocked for Claude" sections in `docs/WIFI-LINK-HANDOVER.md` list them |
| 2026-10-28 | About to look at the router's neighbour (ARP) table for the FM-1's address, while the router was unreachable | One reply stopped; not retried |
| 2026-10-28 | The agent writing **MODE AP**: the FM-1 as an access point (beacons, the 4-way handshake as the authenticator, a DHCP server). It had made one edit (an AES key-wrap function the authenticator's M3 needs) | The agent was stopped; Claude removed that one edit again (`wifi_crypto.c`, `.h`). The owner then said everything should be unblocked; the AP code (`wifi_ap.c`, `wifi_ap_dhcp.c`) turned up written by someone else at 07:02 that morning, and Claude took it over at the owner's word. Nothing was stopped after that: Claude finished the radio side (`wifi_apd.c`), the page, the settings, the web command and the builder item |

Not stopped, and done: the Link peer, the WPA2 station (join, handshake as supplicant, key install), the IP layer, the software CCMP
(host-verified against RFC 3610; unused, the MAC encrypts), the connection manager, the saved networks, the page, the password
entry, the web editor command, the builder items, the tests, the docs, building and capturing.

## 2. What is not done (state 2026-10-28, later)
- **MODE AP is written and host-tested, not tried on a radio.** `wifi_ap.c` / `wifi_ap_dhcp.c` (the pure parts), `wifi_apd.c` (the
  radio: beacons, clients, per-client keys, group frames), the page (MODE AP), the store (version 2), the editor command (ops 5, 6,
  7), the builder item `WIFI_AP`. Tests: `wifi_ap_test` 87, `wifi_apip_test` 38, `wifi_apd_test` 73 (our own station is the client over
  a stand-in radio), `wifi_nets_test` 50, `ed_wifi_test` 70 / 89 (without / with AP), the Optimist UI test with and without AP.
  The first flash (try40) put the dev build with the `wifiap` console command on the FM-1: the AP came up, 86 beacons went out
  with no TX error and the probe requests of the neighbours were counted; no client has joined yet.
- Hardware unknowns for the AP, none settled without a client: whether a phone sees the beacon, the MAC acknowledging frames to
  its own BSSID, the clients' data decrypted at their WCIDs (the counters `data_decrypted` / `data_still_protected` say), the
  group key at a WCID other than 0 (`mc`; the default sends each client its own copy), a phone in power save.
- Not done: reconnecting when the router drops (the station), a client that leaves without a deauth keeps its slot (4 slots), no
  WMM / HT / ERP elements in the beacon, only 1 Mb/s on air.
- Measured costs of the builder items: `costs-all` on the farm has not run; the descriptions quote the two builds made by hand
  (Wi-Fi + Link 27.7 KB flash, 2.0 KB RAM, 3.9 KB pool; the AP on top 8.1 KB flash, 0 RAM, 1.8 KB pool).

## 3. Other things Claude cannot do (not the classifier)

- **Flash.** Only the one build the owner has just named, asked explicitly each time (the owner's rule, 2026-10-28). Never retry a
  broken write. The first rule (2026-10-23): two writers killed a device.
- **See the device's screen.** There is no command-line Mirror tool in the repo; the owner has to look at the page and the web tab.
  Nothing in the page has been seen on the panel by Claude.
- **Read the settings flash.** The console exposes RAM and part of the XIP window only, so what is really stored cannot be proved
  from here (the saved list was checked by power-cycling).
- **Measure the costs of the new builder items.** That is `costs-all` on the build farm; `costs.json` is stale (7 builder tests
  fail the same way on a clean checkout) until it is run.
- **Commit or push.** Only when the owner says so. Everything is uncommitted on branch `link`.
- **Edit the profiles** (`config/profiles/`): the owner's. No shipped profile has the flash for Wi-Fi today (the best has 212 B).
- **Find why the FM-1's transmit is weak.** At 3-4 m the AP hears the FM-1 about 20 dB below what it should; joins succeed about
  6 times in 8. The RF / PA calibration was tried once (it made things worse), then the hunt turned out to be the transmit-buffer
  bug, but the uplink asymmetry was never explained. Needs a second receiver (monitor mode) to measure.
- **Measure on the AP when the router is down or the Mac is on another network.** The AP-side counters (`mca-dump`) and the capture
  on `br0` need the Mac on the "mots" network.
- **Know what an agent's report really did.** The agents' reports are their own words; Claude reviewed the password path in
  `ed_wifi.c` and ran the host tests, but nothing an agent wrote has run on the device except through Claude's flashes.

## 4. Traps found, for the next person

- A transmit buffer in the pool (`IN_POOL`, 0x01C20000 and up) goes out as garbage with no error. Every Wi-Fi DMA buffer is main RAM.
- A frame sent at a WCID that has a key is encrypted by the MAC; at a WCID with no key it is dropped. After a join, clear the key
  (`wifi_sta_clear_hw_keys`) before the next one, and deauthenticate when leaving.
- The main loop must not wait more than 1.5 s: CPU1 is stopped for the rest of the boot ("CPU1 overload").
- The receive routine drops a frame larger than the buffer (1,024 B) unread; the join needs about 24 frames drained a pass.
- Settings reach flash only when the device is quiet (not playing, 1.5 s idle, 5 s gap): wait about 10 s after a change.
- Rows on the page: one per network; a join by name goes to the strongest AP of that name.

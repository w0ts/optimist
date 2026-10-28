<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Wi-Fi on the device: choosing a network, typing the password, keeping a few

Status 2026-10-28: the saved list, the non-blocking manager (`wifi_conn.c`), the SYSTEM > WI-FI page, the password entry and the
web tab work on the device (try36 to try39). MODE AP is written and host-tested (see its section); the first radio test is next.
The user's rulings are quoted.

## What is stored
Up to 4 networks: SSID + the 32-byte WPA2 key (PBKDF2-HMAC-SHA1 of password and SSID), most recently used first, in the
CRC-checked settings record (`persist_t.wifi_nets`, `wifi/wifi_saved.c`, logic and tests in `wifi_nets.c` /
`tests/wifi_nets_test.c`). The password itself is never kept: the key is all a join needs, and it is not shown. A network
is saved only after a join completed (a real handshake accepted the key), so a wrong password never lands in the list.
(The user, 2026-10-27: the derived key, not the password.)

## Choosing (the SYSTEM > WI-FI page, as the Bluetooth pages: "it should be very similar", the user, 2026-10-27)
MODE is OFF, MAN or AUTO, as BT's: OFF the radio stays off; MAN joins when told (SAVE on the page); AUTO joins the best
saved network heard, at start-up and whenever none is joined. Four networks are kept (the 'saved' rows, green); the others
heard are amber; SAVE held forgets the row under the cursor, as BT.
The page scans and lists the access points heard, strongest first, one row an AP (SSID, RSSI, channel; two APs with the
same SSID are two rows, so the AP can be chosen); the saved networks first, then the new ones; a network that is not
WPA2-PSK with CCMP (open, WPA3-only, TKIP group, PMF required) is shown grey and cannot be joined. The scan takes the AP's
own channel from its beacon (the DS parameter): the old join used the channel it was listening on.

## Typing a password (the user, 2026-10-27)
- KNOB 1: the character inside the family. KNOB 2: the family (lower case, upper case, digits, signs, accents ...).
- KNOB 3: the cursor, left / right.
- SAVE tapped: add the character. SAVE held: save (join, and keep the network once it joined).
- HOME tapped: erase. HOME held: cancel.
- Case-sensitive, up to 63 characters, any printable character (NAME's 12 upper-case characters do not fit).

## From the computer
The web editor can send a network (SSID + password); the FM-1 derives the key and joins like a typed one.

## MODE AP (the user, 2026-10-27: "maybe we add mode AP"; built 2026-10-28, host-tested, NOT yet tried on the radio)
The FM-1 as the access point, so a phone, a laptop or Live joins it directly, with no router (a gig). Two more MODE values on the
SYSTEM > WI-FI page: OFF, MAN, AUTO, **AP MAN** (`wn_st.mode` 4), **AP AUTO** (3). AP AUTO starts at power-on once a key is kept and stays up; AP MAN is off at power-on and SAVE on the page starts / stops it (STATE says SAVE: START; with no key SAVE opens the password entry, and a typed password starts it); the web editor has a Start / Stop button (op 8); the switch is never kept. Builder item `WIFI_AP` (bit 435, `FELUCCA_WIFI_AP`).
- The page, in AP: NAME (the network's name: the automatic `FM1-` and the last four digits of the radio's address, e.g.
  `FM1-55B0`, or the one the web editor set), CHANNEL (its knob, 1..13, default 6; the AP restarts 1.2 s after the knob rests),
  STATE (NO PASSWORD, STARTING, ACTIVE, n CLIENTS, NO CAL). SAVE opens the password entry for the AP's key (the same entry,
  8..63 characters); SAVE held asks CLEAR KEY first. **No access point runs without a key**: a network that anyone can join
  is never offered. Switching to AP with no key kept says "SAVE: PASSWORD".
- Stored (`wn_store` version 2, 336 octets: the 268 of version 1 and `wn_apcfg`, 68): the name (length 0 = automatic), the
  channel, the WPA2 key of that name (PBKDF2 of password and name; never the password). A version 1 record reads as before
  (the settings record grows; a shorter old one is zero-extended); a build without MODE AP reads a stored AP mode as OFF.
- The key is derived a slice of the main loop at a time (`wc_pmk_step`, 256 rounds a pass: no 1.5 s starve), kept in the
  store, and the AP starts. A new password stops the AP in the air first.
- `wifi/wifi_ap.c` is the pure part (beacon every 102 ms, probe / auth / assoc answers with the rate set, the authenticator:
  M1 .. M4, M3 carrying the RSN element byte for byte as in the beacon and the group key; host-tested against our own
  station). `wifi/wifi_ap_dhcp.c` is the DHCP server (192.168.4.1 is the AP, clients get .2 to .5, lease an hour, the AP is
  router and DNS). `wifi/wifi_apd.c` is the radio side: up to 4 clients, one handshake record each (M1 20 ms after the
  association answer, again after 1 s, the client let go with reason 15 after 4 sends), each client's keys at its own WCID
  (i + 1: address 0x1800 + 8 n, pairwise key 0x4000 + 32 n, attribute, IV), everything that is not data (beacons,
  management, EAPOL) at WCID 8, which nobody keys (the MAC encrypts whatever it sends at a keyed WCID).
- Group-addressed frames (Link's multicast 224.76.78.75): sent once to each keyed client as a unicast frame carrying the
  group address in its IP header (the pairwise path is the one proven on this MAC); `wifiap .. <mc>` in the dev build tries
  the group key at WCID N instead (WCID 0 is the MAC's drop entry: not usable). Link runs as on the station
  (`wl_start_ap`): the FM-1 is 192.168.4.1, no DHCP client, ARP / ping / DHCP answered by the IP layer's AP mode.
- Unknown until the radio says (the first flash): whether the MAC acknowledges frames to its own BSSID, beacons from the
  main loop keep their time, a client's data is decrypted at its WCID, a phone in power save, whether the unicast copies of
  a multicast are accepted by a laptop's IP stack. The dev build's `wifiap` console command (`wifiap <name> <password>
  [channel] [mc]`, `wifiap off`, `wifiap`) prints the counters for each step (beacons, probes, auth, assoc, M2, M4,
  data decrypted or still protected, DHCP replies, tx errors).
- From the computer: the web editor's Wi-Fi tab (cmd 112, ops 5 password and name, 6 channel, 7 clear key; the state ends
  with the AP block).

## Builder items (the user, 2026-10-27)
Two: **Wi-Fi + Link** (the client, the SYSTEM > WI-FI page, saved networks, Link as a clock source; replaces the
`OPTIMIST_WIFI=1` build switch and its dropped engines) and **Wi-Fi AP** (MODE AP, child of the first). Each needs a
registry entry (`tools/builder/registry.py`, ids reserved up front), a measured cost (`costs-all` on the farm) and a
builder test before any push.

Status 2026-10-28: both are in the registry, `WIFI_LINK` (bit 434, `FELUCCA_WIFI`, `FELUCCA_LINK` follows it; needs `BLE` and
`USB_CONSOLE`, else the builder refuses the build) and `WIFI_AP` (bit 435, `FELUCCA_WIFI_AP`, option of the first; not tested on
the FM-1), both off by default, no `OPTIMIST_WIFI` needed (`OPTIMIST_WIFI=1` stays for the dev console tools and the software
CCMP). Their measured cost (`costs-all` on the farm) is still to come: the descriptions quote the figures below.

## What it costs (measured 2026-10-28: the same bt-debug configuration built with and without Wi-Fi)
| | flash | RAM | pool |
|---|---|---|---|
| no Wi-Fi | 528,668 | 89,960 | 296,264 |
| Wi-Fi + Link + page + web commands, slim (`OPTIMIST_WIFI=2`) | 555,264 (+26.6 KB) | 91,896 (+1.9 KB) | 299,980 (+3.7 KB) |
| the dev build (`OPTIMIST_WIFI=1`: the console's test commands, software CCMP) | 565,120 (+36.5 KB) | 92,504 (+2.5 KB) | 300,484 (+4.2 KB) |
Slimmed on 2026-10-28: the console's Wi-Fi commands and the software CCMP are compiled out of the slim build
(`FELUCCA_WIFI_CON`, `FELUCCA_WIFI_SWCCMP`; -9.9 KB flash, -0.6 KB RAM); one transmit buffer for the manager, the IP layer and
Link instead of three (-0.9 KB RAM); the receive buffer 1,024 B in place of 2,400 (-1.4 KB RAM; a larger frame is dropped
unread, none we need is larger: Link 512 B, DHCP, EAPOL, beacons); the coexistence table packed (-0.6 KB flash, host-tested
word for word). Where the rest goes (flash): the manager and the saved-network store about 6 KB, the crypto 3.5 KB, the IP
layer 3.2 KB, the radio tables 2.5 KB, the Link peer 2.9 KB, the station 2.5 KB, the page 2 KB or more, the web command 0.7 KB,
and strings.

## Open
- The key derivation (8192 HMAC-SHA1) must not freeze the main loop: slice it across passes.
- A non-blocking scan and join for the page (the console's are blocking): `wifi_conn.c`.
- Flash: the Wi-Fi test build is at its limit (581,380 of 581,564 B with PHYS and JUNO off); the page and the entry need
  more engines dropped in the dev build, or a builder item with a measured cost.

## Status 2026-10-27 (evening)
Done and verified on the FM-1: the saved list survives a power-off; `wifigo` / the manager join with the saved key (no
password); the manager (`wifi_conn.c`) scans, joins and starts Link without holding the main loop (CPU1 stays up; the
console's blocking scan and join used to stop it after 1.5 s, "CPU1 overload"); deauth before a join and on leaving, the
MAC's key cleared at the start of every join; whole-join retries (a corrupted frame on the weak uplink: a re-join succeeds
about 6 of 8; a handshake check that fails is retried and called WRONG PASSWORD only after every attempt).
In hand (two agents, 2026-10-27): the SYSTEM > WI-FI page with the password entry (KNOB 1 character, KNOB 2 family, KNOB 3
cursor, SAVE tap add / hold done, HOME tap erase / hold cancel); the web editor's Wi-Fi section.
Next: the first flash of both, then the fixes from what the screen shows; the two builder items (Wi-Fi + Link, Wi-Fi AP)
with measured costs; MODE AP.

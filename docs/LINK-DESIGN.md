# Ableton Link on the FM-1 (design, 2026-10-26)

Tempo, beat phase and start / stop shared with Live, the iOS apps and any Link peer on the same Wi-Fi network.
Sync only: Link Audio is not ported. This waits for an open-source Wi-Fi driver for the AC791N (being written
outside this project); everything above the driver is ready and tested on a computer.

## 1. What exists (branch `link`)

| Part | File | State |
| --- | --- | --- |
| Link peer: discovery, measurement, sessions, tempo, start / stop | `firmware/src/link/link.c`, `link.h` | written; host-tested |
| Simulated network test (3 nodes, own clocks, delay + jitter) | `tests/link_test.c` (in `run_tests.sh`) | 151 checks pass |
| The peer over real sockets on a Mac / Linux | `tools/link/link_host.c` | builds |
| A reference peer on Ableton's own library | `tools/link/ref_peer.cpp` | agrees with ours to 0.003 ms of phase in Live's session (§4) |

`link.c` is a C port of Ableton's library (GPL-2.0-or-later, taken as GPL-3.0-only; `LICENSING.md`): the
same wire format (`_asdp_v` 1 discovery on 224.76.78.75:20808, `_link_v` 1 ping / pong), the same timings
(ALIVE every 250 ms with ttl 5 s, pings 50 ms apart, 100 points then the median, a new measurement every 30 s), and the
same session rules (the older session wins by ghost time, ties within 0.5 s go to the smaller id; the later
beat origin wins; the later start / stop time stamp wins). The file each rule comes from is named in the code.

Firmware fit, as measured: `pi32v2` clang `-Os` compiles it with no runtime calls but `memcpy` (no 64-bit
division: its own 64/32 shift divide); about 6 KB of code as a single object (before the unity build), and
`struct lk_node` is 1,888 B (on the host). No floats, no allocation, no threads: one `lk_poll` from the main loop.

## 2. What the Wi-Fi side must provide

The peer needs an IPv4 UDP layer and nothing more. Contract (to agree with the driver's author):

1. **Address.** Associated + DHCP (or static): our IPv4 address; `lk_set_addr(ip, port)` when it changes, 0 when gone.
2. **Two UDP sockets.** One bound to port 20808 and joined to group 224.76.78.75 (the MAC's multicast filter and
   an IGMP join, so a switch forwards it); one unicast socket on any port. Every datagram is sent from the unicast
   one (multicast goes out with TTL 1, no loopback). Both deliver into `lk_rx(now, src_ip, src_port, data, len)`.
3. **Send.** `send(ip, port, data, len)`, without blocking; up to ~110 B now and then, 512 B at most.
4. **Time stamps.** `now` in `lk_rx` is the **arrival** time, ideally stamped in the driver's receive interrupt,
   and a ping should leave as soon as it is sent. Each data point assumes the two directions take equal time:
   time added on one side only (a receive queue read later, a send held back) moves the session's beat by half of it.
   This is the only timing need. Everything else runs from the main loop.
5. **Power save off while Link is on.** In power-save mode the access point holds multicast until the next DTIM
   (100-300 ms) and unicast until the station wakes: discovery still works, but the measurement goes lopsided.
6. **ARP** (unicast replies to the peers' measurement endpoints) and the usual ICMP. No TCP, no DNS, no mDNS.

Wi-Fi and BT share one RF front end (docs/BLE-HW-FACTS.md §5.2). Whether BLE MIDI or BT audio can run at the
same time as Wi-Fi is for the driver to answer. Link does not need them.

## 3. Into the firmware (not written yet: after the driver)

**Clock.** A 64-bit microsecond host clock from TIMER4 (24 MHz, wraps every 179 s): extended in the main loop,
which runs much more often than that.

**Following** (`io/midi/clock_sync.c`). Link becomes a fourth clock source, `MSRC_LINK`, and SYNC gets `LINK`
appended after BLE (stored values unchanged; AUTO's order: TRS, USB, LINK, BLE). The F8 filter is not needed:
Link already gives a line, so `sy_at(x)` becomes
`(lk_beat_at(us(x)) - S) x BEAT_U / 1e6`, S the session beat our song position 0 sits on. Everything after
`sy_at` stays as it is: the output latency (`sync_out_t`, LIMIT, the offset), sample-exact steps (`ev_map`),
never moving back, a relock beyond a beat. The tempo shown is `lk_mpb`.

**Phase.** Peers agree on the beat modulo a quantum, not on the beat itself. Quantum = the project's bar
(4 beats by default). A start takes S = the next quantum boundary at or after the session's start beat
(`lk_play_beat`); joining a session that is already playing takes the boundary before now, so the bar lines up at once.

**Start / stop sync** (on, as Live's default). Session playing -> arm at S; stopped -> stop. PLAY on the FM-1
-> `lk_set_playing(1, next bar)`, STOP -> `lk_set_playing(0)`. TEMPO turned while following -> `lk_set_tempo`
(Link has no leader: any peer can change the tempo).

**Offset.** A third offset beside CLK OFS / BT OFS, `LINK OFS` (ms), for the network and the device on the
other end; BT audio's own delay is added on top as for the others.

**MIDI clock out.** `clock_out.c` already sends F8 at the tempo it follows, so with Link the FM-1 also bridges
the session to MIDI over TRS / USB.

**Builder.** `LINK`, off by default, needs the Wi-Fi driver's item. UI: SYSTEM > SYNC shows LINK and the number
of peers (`lk_peers`), the only thing a user needs to know whether it joined.

## 4. Tests

- `tests/link_test.c` (in `make test`): the ALIVE's bytes field by field; the rounding against Link's `llround`
  of doubles; a newer node joins the older session (both orders); tempo from either node; start / stop both ways;
  beats agree to < 0.2 ms of beat time with host clocks 10 s apart; BYEBYE and ttl expiry. A deliberately wrong
  offset (+1 ms) in the measurement fails 5 checks.
- With Ableton's library and Live, on one Mac (2026-10-26; `tools/link/ref_peer` and `tools/link/link_host` on
  127.0.0.1, as Link puts loopback peers):
  - Live open with Link on, ref_peer and link_host passive: link_host joined Live's session within 0.6 s.
    Over the 547 samples after that, its phase matched ref_peer's to 0.003 ms (beats modulo 4), with the same
    tempo throughout.
  - `tools/link/interop.sh` with Live closed (tempo changes from both sides, start / stop): phase mean 0.000 ms,
    max 0.001 ms over 345 settled samples, 0 tempo mismatches, playing reached ours, one peer each: passes.
    (A first version of the script misread the bar line, 4 -> 0, between two samples and reported up to 20 ms.)
- Still to do: against Live on another computer over the LAN (real network delay, Wi-Fi in between).
- On the FM-1, once the driver exists: phase against Live measured with a microphone or the TRS clock out on a
  scope, Wi-Fi alone and with BT on.

## 5. Open

- A driver of our own: docs/WIFI-HW-FACTS.md (the MAC is an on-chip RT3070; stock driver ~317 KB, so big-flash only).
- The other driver: its API, where it lives (`firmware/src/wifi/`?), how much flash and RAM it takes (an IP stack such
  as lwIP is tens of KB: unmeasured), and how it shares the radio with BT.
- Ableton's Link guidelines (the name and logo in a product, a compatibility check): to read before a release
  says "Link".

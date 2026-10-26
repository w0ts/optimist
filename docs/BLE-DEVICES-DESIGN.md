<!-- SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 -->
# BLE MIDI devices: one list, up to four remembered devices

How the FM-1 chooses and remembers a Bluetooth MIDI device: the devices list (NONE, LAST, nearby devices), connecting out, LAST
and its reconnection, the pairing a phone or Mac asks for, and what is stored. The stack under it is in
[BLE-STACK.md](BLE-STACK.md) (section 9 is the central role); the radio is in [BLE-HW-FACTS.md](BLE-HW-FACTS.md). The UI is the
SYSTEM > BT MIDI page (`docs/UI-OPTIMIST-DESIGN.md` section 4.7, `ui/optimist/op_bt.c`).

## 1. What it is

- **One list, three kinds of row:** `NONE` (stay visible as a peripheral; connect to nothing by ourselves), `LAST` (the one
  remembered device, only when there is one), then the **nearby** BLE-MIDI devices a scan finds. The page's MODE AUTO is
  LAST chosen (`ble_store.sel`): reconnect to it by itself while the radio is on; MAN is NONE; OFF is the radio off.
- **One link, one role at a time:** a peripheral for a Mac or phone that connects to the FM-1, a central for a device the FM-1
  picked. Never both.
- **LAST is the device the FM-1 itself connected to, as a central** (the user's ruling, 2026-10-09: "last is for automatic
  reconnection initiated from us, not whether to accept a connection"). A Mac or phone that connects to the visible FM-1 is
  accepted as always, whatever the choice, and **never** becomes LAST; its bond stays `persist_t.ble_bond`.
  Choosing NONE never drops a Mac that is connected to us.
- **Name:** the FM-1 advertises **`Optimist XXXX`**, XXXX = the last four hex digits of its address (`BLE_DEVICE_NAME`; it was
  `FM-1 XXXX` until 2026-10-16, stock's `FM-1_BLE`). Kept short because a scanned name is cut to `BLE_NAME_MAX` = 16 characters.
  macOS keeps the name it first saw per address: after a rename forget the device in System Settings > Bluetooth and in Audio
  MIDI Setup. A scanned `Optimist` or `FM-1` name counts as KIND FM-1.
- Builder items: `BLE_CENTRAL` (the scan, connecting out, LAST; it brings `BLE_BOND` with it). Without it the list shows NONE and
  the last device only.

| MODE | The FM-1 | STATE |
|---|---|---|
| OFF | radio off | OFF |
| MAN (NONE, or no LAST) | advertises, waits for a central | ADVERTISING / CONNECTED |
| AUTO (LAST), list closed | **searches** for it (section 4.3); between tries it advertises, so a Mac can still connect | SEARCHING / CONNECTING / PAIRING / CONNECTED |
| on, the list open, or the BT MIDI page on screen with no link and no connect under way (BLE_CENTRAL) | scans (advertising paused) | SCANNING |

A pick while a Mac is connected to us says "BUSY: A HOST IS CONNECTED".

## 2. The UI (SYSTEM > BT MIDI; `ui/optimist/op_bt.c`, `op_project.c`)

The page's lines are MODE, DEVICE (the device of the link as advertised; HOST for a computer or phone that connected to us;
NONE), ACTIVITY (IN / OUT while messages pass) and STATE. SAVE on any line lets a link go; SAVE held forgets (`bt_forget`).
BT AUDIO's page has the same pattern, with up to 4 remembered devices (2026-10-24, `bt/bta_mem.c`): the last one first, each
with its link key; AUTO pages the last one, then the others in turn, and the one that answers becomes the last (`NO DEVICE
ANSWERED` after 30 s). The settings record keeps the other three (`persist_t.bt_kept`).

**The page's own devices (2026-10-24, `op_bt.c` `bt_pg_*`).** The user: "we scan when not connected and on the screen. we show
the remembered devices in one color and the new ones in another color. save connect/disconnect and forget (long press)", and "i
would have expected the knob k2 to scroll thru the list". While a Bluetooth page is on screen, its MODE is MAN or AUTO, its side
has no link and no connect under way, the FM-1 scans (BT MIDI: the BLE scan, `ble_devices_open`; BT AUDIO: the inquiry, run
again when its 10 s end, the devices heard kept) and the page's two middle lines (DEVICE, ACTIVITY / FORMAT) give way to the
devices, MODE above and STATE under them:

- **Rows:** the remembered devices first (LAST, then KNOWN: MIDI `ble_devices.c` order, AUDIO `bta_rows`), then the new ones
  heard (scan table, 8 at most for AUDIO), four rows shown, a thin bar at the right edge when there are more. A MIDI row carries
  the relative signal bars (as the overlay list did); a remembered one its tag (LAST, KNOWN). A device a remembered one is not
  listed twice.
- **Colour is the only mark** (the UI rule: no text on a fill, unreadable on the FM-1): the remembered devices are `C_OK` (the
  status green), the new ones `C_WARN` (the status amber), both fixed status colours that every palette keeps, so the two read
  apart in all five palettes (the palette's own steps are one hue). The cursor row is a 3 px bar at the left edge in the track's
  colour. Names as advertised.
- **K2** (DEVICE's knob) moves the cursor through the rows, stopping at the ends (it was a read-out; it is a value while the
  list shows). SELECT is the SYSTEM page's, as everywhere.
- **SAVE** connects to the cursor row (it becomes LAST, as a pick always did: `bt_pick`); connected or connecting it lets go
  (DISCONNECTED). Nothing heard: `NONE FOUND`. **SAVE held** forgets the cursor row's remembered device (`bt_pg_forget`: AUDIO
  `bta_forget`, MIDI `ble_dev_forget` for LAST, `ble_dev_forget_row` for the others; its keys go too); on a new, not remembered
  row it says `NOT REMEMBERED` and does nothing.
- **The scan follows the page:** it starts when the page comes on screen with no link and no connect of its side under way, and
  stops with a link up, with SAVE (a connect starts), with MODE OFF and when the page is left (`bt_pg_sync`, every pass of the
  main loop). The cursor starts on the first row each time. A link gone brings the list and the scan back.
- **One radio (`ble/rf_share.h`, BT-CLASSIC.md §9.1):** the page's scan never runs while the other side has a connect under way
  (AUTO's paging at boot, a pick), so an inquiry is never started over a BLE initiate and a BLE scan never over a BR/EDR page;
  the list is shown and the scan starts when that connect ends (a link, 30 s, SAVE). AUTO is not touched: it connects at boot
  and when chosen as before, and the page scans only while that is not going on. A link already up on the other side is not
  a reason to wait (a BLE connection event outranks a scan; the inquiry beside an A2DP stream is the old list's case, not
  measured here).
- **Visibility:** BT MIDI's scan replaces its advertising (`ble_ll.c`), so with BLE_CENTRAL the FM-1 is not visible to a Mac or a
  phone while its BT MIDI page is on screen with no link; it advertises again as soon as the page is left or a link is made.
- **Without BLE_CENTRAL** BT MIDI is as it was: no scan, no list, K2 inert, SAVE connects to LAST, else opens the overlay list
  below (NONE and LAST only). BT MIDI's `DEVICES` line (`SI_BLELIST`, in no page table since the Bluetooth pages were split) and
  the overlay list below are the same list in another place; with the central or BT AUDIO nothing opens the overlay any more
  (`dev_open` stays for BT MIDI without the central).

**The list** is SYSTEM's rows while it is open: the cursor row first is `NONE (VISIBLE)`, then LAST (tagged `LAST`, `PAIRED` when
its keys are kept, `CONNECTED` while our link to it is up), then the nearby devices (new ones at the bottom so the list does not
jump under the cursor; a device not heard for 10 s goes; only devices whose advertisement carries the BLE-MIDI service UUID are
listed; a nearby device that is LAST appears only as LAST), each with three signal bars. The bars are **relative** (the strongest
heard is 3) because the RSSI gain table is not known (HW §12 U9). A nearby row being connected carries `CONNECTING` or `PAIRING`
in the bars' place. Under the rows a status line, in sentence case, a device's name always as advertised.

| Control | Does |
|---|---|
| SELECT | the cursor through the list (stops at the ends) |
| SAVE (YES) | asks first ("Connect?" over the name), then: NONE chosen; LAST chosen (and connected to at once when the list hears it, else searched for once the list closes); a nearby device: connect, and once ready it becomes LAST and the choice |
| HOME (NO) | back to SYSTEM (the scan stops, advertising goes on) |

Status words: `CONNECTING <name>`, `CONNECTING (TRY n/6) <name>`, `PAIRING <name>`, `CONNECTED <name>`, `SEARCHING FOR LAST`,
`SCANNING n FOUND`, `VISIBLE`, `CONNECTED: NO SCAN`, `BLUETOOTH IS OFF`, `FAILED: <why>`. **The passkey shows over any screen**
(a framed box "Bluetooth code", the six digits large, "Type it on <name>"; `op_overlay 4`, `op_graph.c draw_code`) because the
search for LAST runs with the list closed and a Mac that refuses LAST's bond is paired afresh there at once: the code was once drawn
only inside the list and a Mac asked for a PIN with nothing on the FM-1 (2026-10-18). It goes when the pairing ends. The list
logic is shared code (`io/midi/ble_devices.c`).

## 3. What is stored

The **device store** is 70 B at the end of the settings record (`persist_t.ble_dev`, A / B at `0x0FC000` / `0x0FD000`, CRC-checked;
never `0xE7000-0xE9FFF`), decoded at boot (`ble_store.c`). A record from before it, a foreign or a damaged one reads "nothing
remembered, NONE". A build without BLE never sees it.

```
mark 0xB6 | ver 1 | sel (0 NONE, 1 LAST) | rsv          4 B
dev:  addr[6] (identity address once bonded, else its AdvA) | info | name[16] | ltk[16] | rand[8] | ediv[2] | irk[16] | sec   66 B
info: bit0 addr random, bit1 role C (always set for LAST), bit2 bonded (ltk valid), bit3 irk valid, bits 4-5 KIND (MIDI, FM-1), bit7 used
sec:  bit0 AUTH (the bond came from a passkey pairing), bit1 MITM (it needs an authenticated link: pair with a passkey at once)
```

Up to four remembered devices (2026-10-24): LAST here, and `persist_t.ble_kept` (198 B, three `struct ble_dev` as they are, the most recent first). A new link of ours to another device makes it LAST and the old LAST the first of the others (the oldest drops at five); a remembered one picked, or found by the search, becomes LAST with its own bond. The search for LAST (§4.3) tries LAST, then each other one in turn, one per try; the list shows them after LAST (KNOWN), SAVE held on one forgets it, FORGET LAST makes the next one LAST. The save is the settings save:
**once the FM-1 is quiet** (a flash write stops the audio); the pairing's bond is saved at once unless a song plays. The scan list is
never saved. The settings word keeps BLUETOOTH ON (bit 24) and the settings record both pages' MODEs (`op_bt.c` `bt_rec`); the choice NONE / LAST is `sel`.

## 4. Connecting out (`io/midi/ble_connect.c`)

### 4.1 A pick
SAVE on a nearby row connects (`ble_central_connect`: initiator, master link, pairing when asked, BLE-MIDI subscribed). **Ready:** it
becomes LAST and the choice, with the pairing's bond, identity (the identity address replaces the AdvA) and security level. Not heard
in 10 s: `FAILED: NOT FOUND`; any other end: `FAILED: <why>` (LOST, NO MIDI SERVICE, PAIRING, AUTH, KEY, GATT ERROR).

### 4.2 Security: nothing asked until the peer asks
- No Pairing Request of ours without the peer's Security Request or an ATT 0x05 / 0x0F / 0x0C: FM-1 to FM-1 and quiet controllers
  connect with no SMP at all. A peer that asks gets **Just Works** (silent) if it cannot type, or a **passkey** (shown on the FM-1,
  typed on the phone or Mac) if it can: the Pairing Request is always DisplayOnly + MITM and the responder's IO capability picks
  (BLE-STACK.md 9.2). The passkey is asked in the first pairing, not after a Just Works one (macOS drops a second alert).
- A bond the phone had from a Just Works pairing and now refuses with 0x05 is paired again with MITM on the same link; a peer that
  refuses that before a code is shown is left and connected to again once with the passkey from the start.
- **One attempt per level, no loop:** a failed passkey pairing (wrong code, 30 s timeout, Authentication Requirements) or a refusal
  after it ends the attempt: `FAILED: PAIRING` / `FAILED: AUTH` stays and nothing connects or prompts again until the user acts (a
  pick, LAST, NONE, FORGET, MODE OFF).
- **The level is kept with LAST** (`sec`): the next connection encrypts with the authenticated LTK, no dialog; if the phone lost the
  bond, the new pairing goes straight to the passkey. A device that needed a passkey but did not become LAST is remembered in RAM.
- **Link attempts are retried, prompts are not:** a link not made (0x3E) or lost before any pairing is made again at once, up to 6
  attempts per user action (`CONNECTING (TRY n/6)`, then `FAILED: NO LINK (6 TRIES)`). LAST leaving while our encryption with its bond
  starts (MIC Failure / Key Missing: it forgot the bond) is tried on 3 links, then the bond is dropped and `FAILED: KEY` held.

### 4.3 LAST and its search
While the radio is on, LAST is the choice (MODE AUTO), the list is closed and no link of either role is up, the FM-1 searches for it: initiate
2 s, advertise 1 s, for 30 s; then initiate 1 s every 10 s, until found or NONE / OFF. Whatever link comes first wins; a central that
connects to us meanwhile is accepted and the search waits. A LAST with an IRK (iOS and macOS use resolvable private addresses) is
found by **scanning and resolving** each ADV_IND's AdvA with the IRK (the engine has no resolving list), then initiating to the
address just heard. NONE leaves our central link and stops the search; FORGET does the same and drops the entry.

### 4.4 FM-1 to FM-1
One FM-1 is visible, the other picks `Optimist XXXX` from the devices list (KIND FM-1). MIDI flows both ways on the one link. Notes that came
in from MIDI are not sent back out; one loop remains [I]: the arpeggiator plays held MIDI notes and its notes go out, so two FM-1s
with the arp on and MIDI OUT = SEQ can ping-pong. If both pick each other at once, the first CONNECT_IND wins and the loser keeps the
link as peripheral. MIDI clock and transport go over the link as over USB (CLK OUT, CLK IN BLE: `clock_out.c`,
`clock_sync.c`). Tested with two whole stacks in one process
(`tests/ble_f2f_test.c`) and in the emulator; not on two FM-1s.

### 4.5 Two levels (the builder's `BLE_CENTRAL`, 2026-10-24)
Off, 1 FULL or 3 MINIMAL. `ble/ble_cfg.h` derives `BLE_CEN_PAIR` (level 1) from it; `#if BLE_CENTRAL` means any level.
`BLE_BOND` (the peripheral's pairing as responder) is independent at every level. Flash and RAM against `BLE_CENTRAL=0`
(measured 2026-10-24):

| Level | What it does | Fails cleanly with | Flash | RAM |
|---|---|---|---|---|
| 1 FULL | everything above: Just Works or the passkey, the pairing again with MITM on the link, the new link with MITM, LAST behind a private address | | +21,924 B | +2,288 B |
| 3 MINIMAL | no SMP initiator, no LL encryption as master, no bonds: scan, the list, connect, GATT, MIDI both ways, up to 4 remembered devices by address | a Security Request (answered Pairing Failed, Pairing Not Supported) or ATT 0x05 / 0x0F / 0x0C: `FAILED: NEEDS PAIRING`, held | +13,420 B | +1,584 B |

Controllers that ask for nothing and FM-1 to FM-1 work at every level (`tests/ble_f2f_test.c` builds the picking FM-1 at each). A
device whose address changes (iOS, macOS) is found again only at level 1; at 3 it is picked from the list again. The settings
record keeps its BLE fields (bonds, `ble_kept`) at every level: a level 1 bond read by a level 3 build is just not used.

## 5. Where it is in the code, and tests

`io/midi/ble_devices.c` (rows, picks, status line, `ble_dev_*`), `ble_connect.c` (pick, retries, search, `ble_connect_status`),
`ble/ble_store.c` (the store), `ble/ble_scan.c` (the AD parser: flags, 128-bit UUID lists, names; 8 entries, ageing 10 s, relative
bars), `ui/optimist/op_project.c` (`dev_*`, `ble_*_cell`). Tests: `ble_store_test`, `ble_scan_test`, `ble_central_test`,
`ble_f2f_test`, `tests/ui_optimist_ble.h` (inside `ui_optimist_test.c`: the lines, every state, MODE, FORGET, the passkey over a screen, the
retries), `ble_emu_central_test.py`. `tests/run_tests.sh` builds `ble_central_test`, `ble_f2f_test` and the UI's BLE tests at level 3 too (§4.5).

## 6. Open

- Whether macOS / iOS reconnect the FM-1 as a peripheral by themselves after a power cycle (measured only for the Mac as LAST, where
  the FM-1 reconnects); directed advertising was never built.
- The FM-1 as central to an iPhone with the passkey, on hardware (BLE-STACK.md 12).
- Real signal bars (the RSSI gain table); the arp ping-pong between two FM-1s; a second FM-1 to test with.
- Not built: two simultaneous links.

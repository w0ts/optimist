# BLE MIDI devices: one list, one remembered device

How the FM-1 chooses and remembers a Bluetooth MIDI device: the DEVICES list (NONE, LAST, nearby devices), connecting out, LAST
and its reconnection, the pairing a phone or Mac asks for, and what is stored. The stack under it is in
[BLE-STACK.md](BLE-STACK.md) (section 9 is the central role); the radio is in [BLE-HW-FACTS.md](BLE-HW-FACTS.md). The UI follows
`docs/UI-OPTIMIST-DESIGN.md` section 39.

## 1. What it is

- **One list, three kinds of row:** `NONE` (stay visible as a peripheral; connect to nothing by ourselves), `LAST` (the one
  remembered device, only when there is one), then the **nearby** BLE-MIDI devices a scan finds. There is no separate AUTO
  setting: LAST chosen means "reconnect to it by itself while BLUETOOTH is ON".
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
- Builder items: `BLE_CENTRAL` (the scan, connecting out, LAST; it brings `BLE_BOND` with it). Without it DEVICES shows NONE and
  the last device only.

| BLUETOOTH | Choice | The FM-1 | STATE |
|---|---|---|---|
| OFF | any | radio off | OFF |
| ON | NONE (or no LAST) | advertises, waits for a central | ADVERTISING / CONNECTED |
| ON | LAST, list closed | **searches** for it (section 4.3); between tries it advertises, so a Mac can still connect | SEARCHING / CONNECTING / PAIRING / CONNECTED |
| ON | the list open | scans (advertising paused) | SCANNING |

YES on a row while BLUETOOTH is OFF switches it ON, then acts. A pick while a Mac is connected to us says "BUSY: A HOST IS
CONNECTED".

## 2. The UI (Optimist UI, SYSTEM page; `ui/optimist/op_project.c`)

Two sections of SYSTEM (UI-OPTIMIST-DESIGN section 39):

| Section | Line | What it is |
|---|---|---|
| Bluetooth | BLUETOOTH | ON / OFF / NO CAL (amber: no stored RF trims, the radio never starts); knob or SAVE toggles |
| | STATE | OFF, STARTING, ADVERTISING, SCANNING, CONNECTING, SEARCHING, PAIRING, CONNECTED (green), FAILED (red, kept until the user acts), NO CAL |
| | DEVICE | the device of the link (connecting, pairing, connected, searched for) as advertised; HOST for a computer or phone that connected to us; NONE |
| | MIDI | IN / OUT / IN OUT for 0.3 s after a message passes over Bluetooth, else -- |
| Devices | DEVICES | action: SAVE opens the list |
| | RECONNECT | the list's NONE / LAST as a line: OFF or LAST's name; right LAST, left NONE, SAVE the other (`BLE_CENTRAL`) |
| | FORGET LAST | action, asks first, naming the device |

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
| SHIFT + a button | FORGET LAST, asked (the modal; SAVE yes): the entry and its keys go, NONE is chosen |

Status words: `CONNECTING <name>`, `CONNECTING (TRY n/6) <name>`, `PAIRING <name>`, `CONNECTED <name>`, `SEARCHING FOR LAST`,
`SCANNING n FOUND`, `VISIBLE`, `CONNECTED: NO SCAN`, `BLUETOOTH IS OFF`, `FAILED: <why>`. **The passkey shows over any screen**
(a framed box "Bluetooth code", the six digits large, "Type it on <name>"; `op_overlay 4`, `op_graph.c draw_code`) because the
search for LAST runs with the list closed and a Mac that refuses LAST's bond is paired afresh there at once: the code was once drawn
only inside the list and a Mac asked for a PIN with nothing on the FM-1 (2026-10-18). It goes when the pairing ends. The list
logic is shared code (`io/midi/ble_devices.c`); the SLOOP UI that once had its own DEVICES screens is gone.

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

One entry: a new link of ours replaces it (its bond kept only when it is the same device again). The save is the settings save:
**once the FM-1 is quiet** (a flash write stops the audio); the pairing's bond is saved at once unless a song plays. The scan list is
never saved. The settings word keeps only BLUETOOTH ON (bit 24); the choice NONE / LAST is `sel`.

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
  pick, LAST, NONE, FORGET, BLUETOOTH OFF).
- **The level is kept with LAST** (`sec`): the next connection encrypts with the authenticated LTK, no dialog; if the phone lost the
  bond, the new pairing goes straight to the passkey. A device that needed a passkey but did not become LAST is remembered in RAM.
- **Link attempts are retried, prompts are not:** a link not made (0x3E) or lost before any pairing is made again at once, up to 6
  attempts per user action (`CONNECTING (TRY n/6)`, then `FAILED: NO LINK (6 TRIES)`). LAST leaving while our encryption with its bond
  starts (MIC Failure / Key Missing: it forgot the bond) is tried on 3 links, then the bond is dropped and `FAILED: KEY` held.

### 4.3 LAST and its search
While BLUETOOTH is ON, LAST is the choice, the list is closed and no link of either role is up, the FM-1 searches for it: initiate
2 s, advertise 1 s, for 30 s; then initiate 1 s every 10 s, until found or NONE / OFF. Whatever link comes first wins; a central that
connects to us meanwhile is accepted and the search waits. A LAST with an IRK (iOS and macOS use resolvable private addresses) is
found by **scanning and resolving** each ADV_IND's AdvA with the IRK (the engine has no resolving list), then initiating to the
address just heard. NONE leaves our central link and stops the search; FORGET does the same and drops the entry.

### 4.4 FM-1 to FM-1
One FM-1 is visible, the other picks `Optimist XXXX` from DEVICES (KIND FM-1). MIDI flows both ways on the one link. Notes that came
in from MIDI are not sent back out; one loop remains [I]: the arpeggiator plays held MIDI notes and its notes go out, so two FM-1s
with the arp on and MIDI OUT = SEQ can ping-pong. If both pick each other at once, the first CONNECT_IND wins and the loser keeps the
link as peripheral. Clock and transport are not passed over BLE. Tested with two whole stacks in one process
(`tests/ble_f2f_test.c`) and in the emulator; not on two FM-1s.

## 5. Where it is in the code, and tests

`io/midi/ble_devices.c` (rows, picks, status line, `ble_dev_*`), `ble_connect.c` (pick, retries, search, `ble_connect_status`),
`ble/ble_store.c` (the store), `ble/ble_scan.c` (the AD parser: flags, 128-bit UUID lists, names; 8 entries, ageing 10 s, relative
bars), `ui/optimist/op_project.c` (`dev_*`, `ble_*_cell`). Tests: `ble_store_test`, `ble_scan_test`, `ble_central_test`,
`ble_f2f_test`, `tests/ui_optimist_ble.h` (inside `ui_optimist_test.c`: the lines, every state, RECONNECT, FORGET, the passkey over a screen, the
retries), `ble_emu_central_test.py`.

## 6. Open

- Whether macOS / iOS reconnect the FM-1 as a peripheral by themselves after a power cycle (measured only for the Mac as LAST, where
  the FM-1 reconnects); directed advertising was never built.
- The FM-1 as central to an iPhone with the passkey, on hardware (BLE-STACK.md 12).
- Real signal bars (the RSSI gain table); the arp ping-pong between two FM-1s; a second FM-1 to test with.
- Not built: several remembered devices, two simultaneous links, clock / transport over BLE.

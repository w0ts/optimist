# BLE MIDI devices: one list (NONE, LAST, nearby), one remembered device (design)

Status: **DESIGN; rounds 1 and 2 built** (§9: round 1 the DEVICES list, the name `FM-1 XXXX`, the one-device store,
bonding with the identity keys, the scan, run on an FM-1; round 2 connecting out, LAST and its reconnection, host- and
emulator-tested, not yet on an FM-1). Branch `feat/ble-devices` (from 1e0ad67, route C as in
`BLE-STACK.md`). Revised to the user's rulings of 2026-10-09: *"one list: Last = none, last device, and then allow the
nearby devices. simple."* and *"last is for automatic reconnection initiated from us… not whether to accept a
connection or not."* (§0.1). The devices can be controllers, keyboards, a Mac, a phone, or another FM-1.

Tags as in `BLE-STACK.md`: **[M]** measured, **[S]** from a published specification, **[I]** inference, **[HW?]**
needs the hardware fact sheet or a device, **[E]** an estimate (sizes: from the measured sizes of the parts that exist
today, `BLE-STACK.md` §5.1, §8, §11.6; not compiled).

Inputs: `BLE-STACK.md` (this branch), `BLE-HW-FACTS.md` (feat/ble-facts d0b4bb7, which ends at §20: **§21, central
role, has not landed when this was written**; everything below that needs it is marked [HW? §21]),
`UI-OPTIMIST-DESIGN.md` (optimist main f3aacb2), `ui/sloop/ui_menu.c` (this branch), `ui/optimist/op_project.c` /
`op_draw.c` (main), fm1-emulator-ble `feat/ble-engine` 6531e20.

## 0. Summary

- **One DEVICES list, three kinds of row**: `NONE` (stay visible as a peripheral: today's behaviour), `LAST` (the
  one remembered device, only when there is one), then the **nearby** BLE-MIDI devices a scan finds. Picking a row
  connects to it; picking NONE disconnects and stays visible. There is **no separate AUTO setting**: LAST selected
  means "reconnect to it by itself while BLUETOOTH is ON".
- **One link, one role at a time** (a peripheral for a Mac / phone that connects to us, a central for a device we
  picked from the list). Never both connected at once.
- **Exactly one remembered device, LAST = the device the FM-1 itself connected to (as a central) and reconnects to**
  (§0.1). A Mac or phone that connects to the visible FM-1 is accepted as always and **never** becomes LAST. The store
  is **one entry**: address + type, name, role (always C), and its bond key + IRK when bonded. **70 B** at the end of
  the settings record (header 4 B + entry 66 B), after `ble_bond[28]`, which stays the peripheral role's bond; RAM 140 B
  with the saved copy. Never 0xE7000-0xE9FFF.
- **Advertised name `FM-1 XXXX`**, XXXX = the last four hex digits of the FM-1's address (the stock `FM-1_BLE` goes).
- **Settings word**: BLUETOOTH ON is **bit 24** (bit 23 CARDS, bits 21-22 HOLD). **No new settings-word bit.** The
  NONE / LAST choice is one byte in the store header.
- **Test hardware is a Mac and an iPhone / iPad only.** The central role is tested on hardware against an **iOS app
  that advertises a BLE-MIDI peripheral** (§5 P3; recommended: BluePiano LE, AUM as the second choice). FM-1 to FM-1
  waits for a second unit; it stays testable in the emulator (two machines on one virtual air).
- **Four phases for the user's devices, a fifth for FM-1 to FM-1** (§5): P1 bonding + LAST as peripheral, P2 scan
  list, P3 central connect + MIDI (with the SMP initiator: an iOS peripheral will probably ask for encryption),
  P4 auto-reconnect, P5 FM-1 to FM-1. About **10-13.5 KB flash and ~1.2 KB RAM for all of it [E]** (was 11.4-14.9 KB
  / 1.6 KB), against ~50 KB / ~10 KB free in user-default + BLE. **Two builder items**, `BLE_BOND` and
  `BLE_CENTRAL` (§4.7).

### 0.1 The ruling on LAST (2026-10-09)

*"last is for automatic reconnection initiated from us… not whether to accept a connection or not."* So:

- **LAST** is the one device the FM-1 connected to **as a central** (picked from DEVICES, connected in the next round)
  and the one it reconnects to by itself while BLUETOOTH is ON and LAST is the choice.
- **Incoming connections** (a Mac, a phone, a central FM-1 connecting to the visible FM-1) are always accepted, as
  today, whatever the choice is, and **never change LAST**. Their bond stays where it is today (`persist_t.ble_bond`,
  EDIV / Rand lookup); their IRK and identity address (SMP phase 3) are kept in RAM only (`midi_ble.c ble_peer_id`).
- **NONE** = stay visible as a peripheral and connect to nothing by ourselves (today's behaviour). Picking NONE never
  drops a Mac that is connected to us.
- Wherever the text below still speaks of a role P LAST (a central that connected to us becoming LAST, directed
  advertising to it, recognising it by its IRK), it is superseded by this ruling; those parts are not built.

## 1. User stories and roles

### 1.1 FM-1 as peripheral (today), with bonding and LAST

*"I connect my Mac once in Audio MIDI Setup; after that, when I power the FM-1 on, the Mac finds it again, and the
FM-1 shows MacBook Pro as its last device."*

- Today: the FM-1 advertises `FM-1_BLE` with the BLE-MIDI UUID from a random static address made once and kept with
  the settings (`persist_t.ble_addr`), so its identity is stable across power-offs [M]. A Mac connects, MIDI goes
  both ways [M:hw]. Bonding exists, opt-in (`OPTIMIST_BLE_SMP=1..3`): LE legacy Just Works, one key slot found by
  EDIV / Rand; the central's IRK and identity address are read and **dropped** (`ble_smp.c smp_their_key`) [M].
- New: the name becomes **`FM-1 XXXX`** (§3.4). *(Superseded by §0.1: the central that connects does **not** become
  LAST.)* Its name could be read with one GATT client request (Read By Type, GAP Device Name 0x2A00, on the
  central's own GATT server) [I: macOS and iOS both expose a GAP service with the name; check on hw]. When it bonds,
  its IRK and identity address are kept, so the next connection from it is recognised even from a resolvable private
  address (RPA) [S: Core Vol 3 Part H 2.4.2.2].
- Reconnect is the **central's** decision in this role. What the FM-1 can do: advertise from boot with the same
  address (it already does), and, if the hardware shows a Mac / iPhone answers it, **directed advertising**
  (ADV_DIRECT_IND to the bonded central's identity address) for a few seconds first. Whether macOS / iOS reconnect a
  BLE-MIDI peripheral by themselves after a power cycle is **unmeasured** [I]; P1's hardware test measures it with
  and without bonding.

### 1.2 FM-1 as central, connecting to BLE-MIDI peripherals

*"I switch my KeyStep / nanoKEY / CME / WIDI on; on the FM-1 I open DEVICES, it is in the list, I press YES and play
it; next time the FM-1 connects to it by itself."*

- The FM-1 scans (active: it sends SCAN_REQ to get the name, which most controllers put in the scan response [I]),
  lists the advertisers whose AD carries the BLE-MIDI service UUID (`03B80E5A-...`) **and nothing else** (decided),
  connects as central, discovers the MIDI characteristic, turns its notifications on (CCCD), and from then:
  notifications in = MIDI in (the decoder of today), MIDI out = Write Without Response to the characteristic (the
  encoder of today).
- A Mac whose Audio MIDI Setup *Bluetooth Configuration* has **Advertise** on [S: Apple's Audio MIDI Setup guide,
  support.apple.com/guide/audio-midi-setup/ams33f013765], or an iOS app that advertises a BLE-MIDI peripheral (§5
  P3), shows up in the same list and is connected the same way [I: iOS advertises from an RPA, so it is only found
  again after a bond, §3.3].
- Some peripherals ask for encryption (Insufficient Authentication on the CCCD write) [I: a minority of controllers;
  Apple's own peripheral role enforces pairing, QA1831]. That needs SMP **as initiator** (§4.3), which is therefore
  part of P3; without it the FM-1 shows *NEEDS PAIRING* and stays disconnected.

### 1.3 FM-1 to FM-1 (when a second unit is available; the emulator first)

- One FM-1 is VISIBLE (peripheral, ON, NONE or a Mac as LAST); the other opens DEVICES, sees `FM-1 A1B2` (the name
  of §3.4; KIND FM-1 = the `FM-1 ` prefix) and picks it: it becomes the central. MIDI flows both ways on the one
  link, as with a Mac.
- No echo: notes that came in from MIDI are never sent back out (`seq_midi.c` rules) [M: code]. **One loop
  remains**: the arp plays held MIDI notes and its notes go out, so two FM-1s with the arp on and MIDI OUT = SEQ can
  ping-pong [I]. Test in P5; a fix if needed: arp output from a BLE-held note not sent back to BLE.
- Clock and transport are not passed on over BLE today (`midi_ble.c`, `clock_sync.c`): **later** (decided).
- If both FM-1s pick each other at once: the first CONNECT_IND wins. The loser's link comes up as **peripheral** to the
  very device it was searching for; it counts that as success (the peer's identity address matches LAST), stops
  initiating and keeps the link.

### 1.4 What the FM-1 does when

| BLUETOOTH | DEVICES choice (store `sel`) | The FM-1 | Shown |
| --- | --- | --- | --- |
| OFF | (any) | radio off (today: never started at boot when OFF) | `OFF` |
| ON | NONE (or no LAST) | advertises, waits for a central (§0.1: an incoming connection is always taken, LAST unchanged) | `VISIBLE` / `CONNECTED` |
| ON | LAST = a device we connected to (role C) | **SEARCHING** it: initiates to its address; between tries it advertises, so a Mac can still connect (§1.4.1) | `SEARCHING` / `CONNECTING` / `CONNECTED <name>` |
| ON | DEVICES screen open | scans continuously (advertising paused while the list is open) | `SCANNING` |

Connecting while BLUETOOTH is OFF (YES on a row) **switches it ON** (decided).

#### 1.4.1 Advertise while scanning?

Whether the engine can run two links at once (one advertising, one scanning / initiating) is not known [HW? §21: the
control block is per link and the emulator models 8 links, but no oracle ran two]. The design does not need it:
**software time-slicing on one link** (stop advertising, initiate for a window, back to advertising) works with any
answer, because the link layer already starts and stops advertising cheaply (`ble_hw_adv_start/stop`).

- **DEVICES open**: scan only (100 % of the radio: the list fills fast); advertising resumes when the screen closes.
- **SEARCHING for LAST** (decided: **retry 30 s, then every 10 s while ON**): initiate 2 s, advertise 1 s, repeat for
  30 s, then initiate 1 s every 10 s (the rest advertising) until found or BLUETOOTH OFF / NONE picked. Whatever link
  comes up first wins; the other activity stops.
- If §21 shows two links work, the same state machine runs both at once; nothing in the UI changes.

## 2. The UI

### 2.1 The screen and the fonts

240 x 240 px, RGB565. One font, Terminus 8 x 16 (`FONT_S`, Latin-1: names with lower case and accents), and
`FONT_L`, the same glyphs drawn 2 x 2 (16 x 32, ASCII 32..95: capitals, digits, signs only) [M: tools/gen_font.py].
So the mockups below are exact on a grid of **30 columns x 15 lines** (1 character = 8 x 16 px); a `FONT_L` word takes
2 columns and 2 lines a letter. Device names are always `FONT_S` (they have lower case), cut to fit with `~`.

Signal bars: `RSSI2` gives dBm only through a 64-entry gain table that is not transcribed (HW §2 0x138, U9) [M:s].
Until U9 is measured, the bars are **relative** (the raw RSSI ranked: the strongest of the list 3 bars) and drawn
dim (decided); the list order never uses them. Rows with no bars (LAST not heard in this scan) show `--`.

Two UIs exist; a build has one (builder `UI`): SLOOP's HOME-held menu (`ui/sloop/ui_menu.c`) and the Optimist UI
(`ui/optimist`, main). The BLE rows go into both.

### 2.2 Optimist UI (main): SYSTEM > BLUETOOTH, and the DEVICES list

Its grammar (UI-OPTIMIST-DESIGN.md §2): SELECT = the cursor row, KNOB 1..4 = the row's four cells, PRESETS = the hot
cell fine / scrolls a list, **SAVE tapped = YES** (enter a ▸ row, toggle, do), **HOME tapped = NO** (back), **HOME
held + a knob = clear** the cell (here: FORGET), a confirm is a modal box (*HOME no* left, *SAVE yes* right, red frame
when it destroys). Layout (`op_draw.c`): header y 0..24, four cards y 28..72, the panel y 76..239 in 20 px rows (8
rows; 5 with CARDS 2x2), no footer.

SYSTEM gains one row, **BLUETOOTH** (after USB; only in a BLE build). Three cells; K4 stays empty:

| Cell | Label | Value | Knob / YES |
| --- | --- | --- | --- |
| K1 | BLE | ON / OFF (bit 24) | right ON, left OFF; YES toggles |
| K2 | DEVICES | ▸ | YES opens the DEVICES list (an action cell) |
| K3 | STATE | VISIBLE · SCANNING · SEARCHING · CONNECTING · CONNECTED · NEEDS PAIRING · NO RF CAL (read-out) | — |

SYSTEM, cursor on BLUETOOTH, connected as central (30 x 15 grid; `[ ]` = a card, `>` = the cursor bar):

```
DRUMS   SYSTEM  BLUETOOTH  120
                              
[BLE  ][DEVICES][STATE   ]    
[ ON  ][   >   ][CONNECTED]   
[=====][       ][KeySte~  ]   
 CHANNELS  1     2     3   10 
 USB       OFF                
>BLUETOOTH ON    >  CONN      
 CPU       23%   240          
 CALIBRATE PANEL 350          
 ABOUT     OPTIMIST           
                              
  CONNECTED  KeyStep 37       
                              
```

DEVICES (a list screen like PROJECT's slots: the cursor row is the item). Rows, in this order and no other:
**`NONE`**, **the LAST device** (if there is one), then **the nearby devices** the scan finds (strongest first at the
moment they appear; new ones go to the bottom so the list does not jump under the cursor). The cards show the cursor
row:

| Cell | Label | Value |
| --- | --- | --- |
| K1 | NAME | the name (FONT_S in the card), or the address `C4:7F:..` when it has none |
| K2 | SIGNAL | bars (dim until U9), `--` when not heard in this scan |
| K3 | KIND | `MIDI` (a BLE-MIDI peripheral) · `FM-1` (name starts `FM-1 `) (§0.1: no `HOST` row: a central that connected to us is not listed) |
| K4 | STATE | `LAST` · `CONNECTED` · `NEW` |

```
DRUMS  BLUETOOTH DEVICES  SCAN
                              
[NAME   ][SIGNAL][KIND][STATE]
[KeySte~][ ▮▮▮  ][MIDI][ LAST]
[       ][      ][    ][=====]
 NONE        stay visible     
>KeyStep 37        LAST   ▮▮▮ 
 FM-1 A1B2                ▮▮  
 nanoKEY Studio           ▮▮  
 WIDI Master              ▮   
 MD-BT01                  ▮   
                              
                              
```

Keys on DEVICES:

| Control | Does |
| --- | --- |
| SELECT / PRESETS | the cursor through the list (stops at the ends) |
| **YES** (SAVE tapped) | on a device row: **connect** to it (as central; the LAST row of a HOST: advertise and wait, a central must connect to us); on the connected row: **disconnect** (it stays LAST, and NONE is picked); on **NONE**: no auto-connect, stay VISIBLE (a Mac connected to us stays). A device **we** connect to **becomes LAST** (the one remembered device, replacing the old); one that connects to us never does (§0.1) |
| **NO** (HOME tapped) | back to SYSTEM (the scan stops, advertising resumes) |
| **HOME held + a knob** (on the LAST row) | **FORGET**: modal *FORGET KeyStep 37?*, red frame (drops the bond; for a bonded Mac the toast says *FORGET IT ON THE MAC TOO*); NONE is picked |
| ALGORITHM | the track, as everywhere (nothing here) |

The header shows `SCAN` while scanning (blinking dot), `SEARCH` while searching, and the toast says *CONNECTED
KeyStep 37* / *LOST KeyStep 37* / *NEEDS PAIRING*.

### 2.3 SLOOP's HOME-held menu (this branch and SLOOP-UI builds)

Its grammar (`ui_menu.c`): SELECT = the screen before / after, KNOB 1..4 = the rows, PRESETS = the cursor, **OCT+ =
OK** (step a setting or open an action), **OCT- = close / back**. The BLUETOOTH screen (SYSTEM's last) gets two
rows instead of one (`MI_DY` 38 px a row, values in `FONT_L` at the right, a status line under):

```
MENU              SYSTEM 5/5  
------------------------------
SCREEN LIGHTS AUDIO [SYSTEM]  
|K1 BLUETOOTH          ON     
|                             
|  CONNECTED KeyStep 37       
|K2 DEVICES                   
|  OCT+ OPENS                 
|                             
|                             
|                             
|                             
SELECT SECTION  KNOB SETS     
OCT+ OK   OCT- CLOSE          
                              
```

DEVICES (a sub-screen, `ui.menu = 3`, as ABOUT is 2): NONE, LAST, nearby.

```
DEVICES             SCANNING  
------------------------------
  NONE (VISIBLE)              
> KeyStep 37       LAST  ▮▮▮  
  FM-1 A1B2              ▮▮   
  nanoKEY Studio         ▮▮   
  WIDI Master            ▮    
  MD-BT01                ▮    
                              
                              
------------------------------
CONNECTED  KeyStep 37         
PRESETS MOVE   OCT+ CONNECT   
K4 FORGET      OCT- BACK      
                              
```

- PRESETS (or SELECT) moves the cursor; OCT+ connects / disconnects / picks NONE; OCT- back to the menu.
- **FORGET**: KNOB 4 turned on the LAST row arms it (*OCT+ FORGETS KeyStep 37?* in amber on the status line), OCT+
  within 3 s does it, anything else lets it go (SLOOP's menu has no modal; HOME held is the menu itself).

### 2.4 How the existing BLUETOOTH ON / OFF fits

- ON / OFF keeps its meaning and its default (**OFF**, the 2026-10-08 ruling), at settings-word **bit 24** on main.
  OFF = radio off, nothing on the air, a link ended with no stuck note (today's code, unchanged).
- ON = the radio does what §1.4 says. The first ON of a boot still starts the radio then (`ble_radio_start`), with
  the boot guard (`ble_boot_radio`) as today.
- Opening DEVICES while OFF switches nothing on: the list shows NONE, LAST and *BLUETOOTH IS OFF*; YES on a row
  switches ON and connects (decided).

## 3. Persistence

### 3.1 The one-device store

Where: **appended to the settings record** (`persist_t` in `storage/project.c`, `OBJ_SETTINGS`, A / B at
0xFC000 / 0xFD000, CRC-checked, `st_save` / `st_load`), as `ble_addr` and `ble_rf` are, **after `ble_bond[28]`**,
which stays the peripheral role's bond (§0.1: an incoming central's bond is not LAST's). A record from before the store
reads "nothing remembered, NONE" (built: `persist_t.ble_dev`, `ble/ble_store.c`).
Not its own object: two more sectors would have to
come out of the flash allow-list. **Never 0xE7000-0xE9FFF** (FL_NEVER, the SDK's VM and BTIF). A build without BLE
never sees the field (appended last: it reads the rest as its own, as today).

```
struct ble_dev_store {                  /* 4 + 66 = 70 B */
    uint8_t mark;                       /* 0xB6: valid; else "nothing remembered", NONE */
    uint8_t ver;                        /* 1 */
    uint8_t sel;                        /* 0 NONE (stay visible), 1 LAST (reconnect it while ON) */
    uint8_t rsv;
    struct ble_dev {                    /* 66 B, the only entry */
        uint8_t addr[6];                /* identity address (public / random static), least significant first */
        uint8_t info;                   /* bit 0 addr random, bit 1 role C (we connected to it) / 0 role P (it to us),
                                         * bit 2 bonded (ltk valid), bit 3 irk valid, bits 4-5 KIND (MIDI, FM-1, HOST),
                                         * bit 7 entry used */
        char name[16];                  /* from the AD / scan response or GAP Device Name, NUL-padded */
        uint8_t ltk[16];                /* the bond: the key that encrypts a reconnection (ours handed out as
        uint8_t rand[8];                 * responder, the peer's taken as initiator), with its EDIV / Rand */
        uint8_t ediv[2];
        uint8_t irk[16];                /* the peer's IRK: recognise / find it behind an RPA */
        uint8_t rsv;                    /* (65 B of fields: one spare to make the 66) */
    } dev;
};
```

- **One entry** (decided): no LRU, no use counter, no `want` index (was 280 B; now 70 B, and 42 B more than the
  `ble_bond[28]` it replaces).
- **Fields**: address + type; name; role (who connected to whom: tells reconnect how); KIND; bond (LTK, EDIV, Rand);
  IRK.
- **RAM**: the live copy (70 B) plus `persist_saved`'s copy (70 B, the change test) = **140 B** (was 560 B).
- **Writes**: a settings save erases and writes a 4 KB sector and stops the audio, so the entry is saved like every
  setting changed while playing: **once the FM-1 is quiet** (`settings_later`, as the bond and the address are). The
  scan list itself is never saved.

### 3.2 What is kept, per role

| | Role P (it connected to us: Mac, phone, a central FM-1) | Role C (we connected to it: controller, advertising Mac / phone, FM-1) |
| --- | --- | --- |
| address | its InitA, or its identity address after a bond (an RPA is useless next time) | its AdvA (controllers and FM-1s: public or static [I]); its identity address after a bond |
| name | GAP Device Name read from it (one ATT request) | AD type 0x09 / 0x08 from ADV_IND or SCAN_RSP |
| bond | our LTK / EDIV / Rand (responder, today's `ble_smp.c`) | its LTK / EDIV / Rand (initiator, §4.3) |
| IRK | its IRK, if it distributes one (Macs and phones do [I]) | its IRK, if it does |

*(§0.1 supersedes this table's role P column: an incoming central is never stored as LAST; its bond is `ble_bond`, its
identity is kept in RAM.)* A new link **we** start replaces the entry (built: `ble_store_set_last`, its bond kept only
when it is the same device again).

### 3.3 Auto-reconnect = LAST is selected

When: at boot when ON was saved, when BLUETOOTH is switched ON, and when a link to LAST is lost (supervision
timeout, the peer powered off). Only with `sel` = LAST; NONE (or no entry) just advertises.

- **LAST is role C**: initiate to its address. The engine's initiating state with the target address (TARGETADR /
  WHITELIST0, FILTERCNTL bit 0) connects on the first ADV_IND from it with no scan reports for software to handle
  [HW? §21: whether the engine sends CONNECT_IND in T_IFS by itself; software cannot]. If the entry has an IRK (the
  peer advertises from an RPA, as iOS does), the engine cannot match it (no resolving list known), so the FM-1 scans
  instead, resolves each AdvA with `ah(IRK, prand)` in the main loop and initiates to the RPA that matches.
  Time-sliced with advertising (§1.4.1); 30 s, then every 10 s.
- **LAST is role P, or NONE**: advertise undirected as today (the same static address and name, so a Mac that knows
  the FM-1 can reconnect). If P1's hardware test shows a Mac or iPhone reconnects faster to it: **ADV_DIRECT_IND**
  (high duty, 1.28 s max, Core Vol 6 Part B 4.4.2.4.3) to the bonded central's identity address first, then
  undirected [HW? §21 for the engine's directed mode, FORMAT / TARGETADR]. A whitelist is **not** used to keep others
  out: centrals with RPAs would not match it, and anyone may connect while VISIBLE (as today).
- Recognising who connected (role P): InitA equal to the entry's address, or resolved by its IRK, makes it LAST
  again (its name shown at once, its bond used).

### 3.4 The advertised name

`FM-1 XXXX`, XXXX = the last four hex digits of the device address (`persist_t.ble_addr`), upper case: `FM-1 A1B2`.
Eight characters, in the AD or the scan response as today (+~0.05 KB for the hex). Macs and phones that cached the
stock `FM-1_BLE` show the new name after the next scan; a Mac's saved Audio MIDI Setup entry is by address, so it
keeps working [I: check in P1].

### 3.5 The settings word

BLUETOOTH ON is **bit 24**; bit 23 is CARDS, bits 21-22 HOLD. **This design adds no settings-word bit** (NONE / LAST
is `sel` in the store header). **Risk, kept**: a settings word written by an *older Optimist UI build that used bits
24-25 for HOLD* reads as BLUETOOTH ON when it was HOLD in that build (and bit 25 is left unused here). The store's
`mark` keeps a stale or absent store from reading as LAST, so the worst case is the radio coming ON once; to be
cleared by the settings version check or documented in the release notes when such a record is loaded.

## 4. Stack work by layer, sized

Today [M]: stack without encryption 8.2 KB flash / 1.8 KB RAM, driver 3.5 KB / 2.6 KB, user-default + BLE +13.1 KB /
+5.5 KB; SMP + LL encryption opt-in +2.7 KB / +0.4 KB (`BLE-STACK.md` §5.1, §8, §11.6). Estimates below are
[E] from the sizes of the comparable existing code (`ble_ll.c` 3.2 KB for the peripheral LL, `ble_att.c` 2.0 KB for
the server, `ble_smp.c` responder ~1.1 KB). The central-role engine facts are still pending (§7).

### 4.1 Link layer: central

| Part | What | Flash | RAM |
| --- | --- | --- | --- |
| Scanner (LL + driver) | link in state 1 (HW §2.5), scan interval / window, active scan (SCAN_REQ by the engine? [HW? §21]), each ADV_IND / SCAN_RSP → a 4-entry ring of raw reports (addr, type, RSSI word, AD ≤ 31 B) in the RX IRQ; the main loop parses and merges (duplicates, ageing) | 1.2-1.8 KB | ring 160 B |
| AD parser + scan list | flags, 128-bit UUID list (0x06 / 0x07: the MIDI UUID, the only filter), names (0x08 / 0x09), 8 entries (addr 7, name 16, RSSI, seen, kind) | 0.3-0.4 KB | 220 B |
| Initiator | CONNECT_IND built by us: AA by the spec's rules (`ble_prim.c` has the checks; a generator ~100 B), CRC init from `ble_hw_rand`, WinSize 2, WinOffset 0, **our parameters: interval 6-9 (7.5-11.25 ms), latency 0, timeout 100 (1 s)**, all 37 channels, Hop 5..16 random, our SCA; link to state 3 with the target, then state 6 (column 9 bit 0 marks the end of initiating, HW §2.3) | 0.5-0.7 KB | 40 B |
| Master connection | the LL's procedures from the central's side: we start version / feature / length exchange; we **answer** LL_CONNECTION_PARAM_REQ and L2CAP Connection Parameter Update Requests with an LL_CONNECTION_UPDATE_IND at an instant (+6 events); LL_ENC_REQ (master's SKD / IV) for a bonded or pairing link; the rest (ack, queues, supervision, terminate) is shared with the peripheral code. **Channel-map updates as master: none at first** | 1.0-1.5 KB | 30 B |
| Driver, master side | state 3 / 6 set-up in the control block, the first anchor 1.25 ms + WinOffset after our CONNECT_IND, master TX first in each event (the engine's timing [HW? §21]), widening of the peer's SCA (HW §7 formula) | 0.6-1.0 KB | 20 B |
| **LL central total** | | **3.6-5.4 KB** | **~0.5 KB** |

### 4.2 GATT client

| Part | What | Flash | RAM |
| --- | --- | --- | --- |
| ATT client core | one request outstanding, the 30 s ATT timeout, Error Response handling; MTU exchange as client (247) | 0.3 KB | 16 B |
| Device name | Read By Type 0x2A00 on 1..FFFF (role P too: the Mac's name, P1) | 0.1 KB | — |
| BLE-MIDI discovery | Find By Type Value (primary service = MIDI UUID) → range; Read By Type 0x2803 in it → the MIDI I/O characteristic's value handle (128-bit UUID match); Find Information after it → its CCCD | 0.5-0.7 KB | 8 B handles |
| Subscribe and data | Write Request CCCD = 0x0001; Handle Value Notifications on the value handle → `ble_midi` decoder (as writes are today); MIDI out as **Write Without Response** from the same encoder and ring as notifications (`att_midi_out` with a `client` switch) | 0.3-0.4 KB | — |
| **GATT client total** | the server stays as it is. Core + name (0.4 KB) come with P1; discovery + subscribe (0.8-1.1 KB) with P3 | **1.2-1.5 KB** | **~30 B** |

### 4.3 SMP

| Part | What | Flash | RAM |
| --- | --- | --- | --- |
| Responder (exists, opt-in) | legacy Just Works, bonding; **change**: keep the central's IRK + identity address (Identity Information / Identity Address Information) into the entry instead of dropping them; hand out our Identity Address Information (our static address) | +0.15 KB | — |
| Initiator | Pairing Request (NoInputNoOutput, bonding, keys: their LTK + IRK), Mconfirm / Mrand, check Sconfirm with `c1`, STK `s1` (all exist), the LL encryption started by us (master side of 4.1), take their LTK / EDIV / Rand / IRK / address; the SMP 30 s timeout | 1.0-1.3 KB | 60 B (shares the responder's state) |
| When | responder: a Mac / phone pairing (P1). Initiator: **P3**, because an iOS or macOS peripheral enforces pairing [S: Apple QA1831] and so the iPhone test needs it; also for controllers that answer Insufficient Authentication, and to keep an RPA peer's IRK. **Not needed for FM-1 to FM-1** (static addresses, open link) | | |

LE Secure Connections (P-256) stays out: ~4-6 KB and a big-number multiply on a 240 MHz core between audio
blocks [E]; legacy Just Works is what the stack already does and what a passkey-less instrument can offer anyway.
Pairing as peripheral (decided, P1): ask with **Insufficient Authentication**, the way Apple's guidelines say, so a
bond gives the IRK and the reconnect (`BLE_MIDI_NEED_ENC`).

### 4.4 RPA resolution

`ah(k, r) = e(k, 0^104 || r) mod 2^24` (Core Vol 3 Part H 2.2.2): one AES-128 block with `ble_aes128` (there with
`BLE_LL_ENC`). An RPA (top bits 01) resolves against the entry's IRK: one block per report, in the main loop (never
in the IRQ). Also generating an RPA for directed advertising to a central that wants one (`ah` with its IRK).
**~0.1 KB flash** (the AES comes with `BLE_BOND`).

### 4.5 The MIDI router

**One link at a time** (decided; the simplest, and what RAM and the radio's one-link time-slicing want):
`midi_ble.c` keeps its two rings; the stack sends the out ring as notifications (role P) or as Write Without
Response (role C); in is the same decoder either way. The note release on link loss (`ble_held`) is unchanged. A
second simultaneous link would need a second LL context (~0.7 KB RAM), a second engine link [HW? §21], routing rules
and UI: **not in this design**. **~0.1-0.2 KB.**

### 4.6 UI, store and glue

| Part | Flash | RAM |
| --- | --- | --- |
| Store: load / save / migrate the old bond / FORGET (no LRU) | 0.2-0.3 KB | 140 B (§3.1) |
| Reconnect state machine (search, time-slice, 30 s / 10 s, peripheral match) | 0.3-0.5 KB | 16 B |
| Optimist UI: BLUETOOTH row + DEVICES list (NONE, LAST, nearby; YES; HOME-held FORGET; the modal) | 0.7-1.0 KB (0.5-0.7 with NONE / LAST only, P1) | 16 B |
| SLOOP menu: two rows + DEVICES sub-screen | 0.6-0.9 KB (0.4-0.6 with NONE / LAST only, P1) | 8 B |

### 4.7 Totals and builder options (two items)

The builder split is **merged from three items to two**: with one remembered device there is no store worth building
alone, and the SMP initiator is needed by the first central test (an iPhone peripheral), so `BLE_PAIR_OUT` folds into
`BLE_CENTRAL`; `BLE_RSSI` (the 64-entry gain table, 0.1 KB) is not an item: it is a later change inside
`BLE_CENTRAL` once U9 is measured.

| Builder item | Contains | Flash [E] | RAM [E] |
| --- | --- | --- | --- |
| `BLE` (exists) | peripheral, today | (13.1 KB) | (5.5 KB) |
| `BLE_BOND` (new; makes `OPTIMIST_BLE_SMP=1` a builder option) | SMP responder + LL encryption + AES (2.7 KB / 0.4 KB measured) + keeping the IRK 0.15, ATT client core + name read 0.4, RPA 0.1, the store 0.2-0.3, NONE / LAST UI 0.4-0.7, name `FM-1 XXXX` | 4.0-4.4 KB | ~0.6 KB (140 B store, 16 B client, 0.4 KB SMP) |
| `BLE_CENTRAL` (new, needs `BLE_BOND`) | scanner, parser + list, initiator, master LL and driver side (3.6-5.4), GATT discovery + subscribe (0.8-1.1), SMP initiator (1.0-1.3), reconnect (0.3-0.5), nearby rows in the UI (0.3-0.5) | 6.0-8.8 KB | ~0.6 KB |
| FM-1 to FM-1 extras (P5) | the arp loop guard if needed | 0-0.3 KB | — |
| **all** | | **10.0-13.5 KB** | **~1.2 KB** |

A build with `BLE_BOND` only is useful alone (P1: LAST and the bond as peripheral). Without `BLE_BOND` nothing is
remembered across power-offs beyond today's address.

## 5. Phases

Each phase ends with host tests (`tests/run_tests.sh`, ASan / UBSan), the emulator (fm1-emulator-ble, `diagnose`),
and a hardware session the user runs (agents never touch the FM-1, the serial port or MIDI). **Hardware available: a
Mac and an iPhone / iPad. No BLE-MIDI controller, no second FM-1.**

### P1: bonding, `FM-1 XXXX` and LAST as peripheral

- **Build**: `BLE_BOND`; the one-device store (§3.1) replacing `ble_bond`; keep the IRK / identity; GATT client core +
  name read; the name `FM-1 XXXX`; SYSTEM > BLUETOOTH row with STATE showing `CONNECTED MacBook Pro`; DEVICES with
  NONE + LAST only (no scan yet); FORGET. Pairing: ask with Insufficient Authentication (`BLE_MIDI_NEED_ENC`).
- **Host**: the store's migration (an old `ble_bond` record becomes the entry), FORGET; a Mac-like central that pairs
  with an RPA and distributes IRK + identity, reconnects from a new RPA and is recognised; the name request.
- **Emulator**: the virtual central exists (`ble_central/gatt.rs`, its pairing); **add** to it: a GAP server answering
  Read By Type 0x2A00 with a name, an RPA InitA from a fixed IRK that changes per connection, Identity Information
  distribution. Checks: name shown in the menu screenshot, the second connection recognised, a reboot keeps the
  entry (`FM1_FLASH_DUMP` / `RESTORE`).
- **Hardware (Mac and iPhone / iPad, both as centrals)**: pair, power-cycle the FM-1: does the Mac / iPhone reconnect
  by itself? With `BLE_MIDI_NEED_ENC` and without. Measure the time to reconnect. The new name shows. Then the
  directed-advertising build (if §21 gives the engine's directed mode) the same way: keep it only if it helps.
- **Risks**: macOS / iOS may never auto-reconnect a BLE-MIDI peripheral whatever we do (then LAST in role P is only
  the name and the bond, and the host's own UI does the reconnect); pairing on hardware is untested (LL encryption in
  software, `isr_max_us` unmeasured with encryption on, §5.1).
- **Size**: +4.0-4.4 KB flash, +0.6 KB RAM.

### P2: the scan list (central scan only, names)

- **Build**: scanner, AD parser, scan list (BLE-MIDI UUID filter), DEVICES with nearby devices, relative dim bars.
- **Host**: a fake `ble_hw` feeding ADV_IND / SCAN_RSP reports (BLE-MIDI and not, names in ADV or SCAN_RSP,
  duplicates, a device that goes away).
- **Emulator**: **add** the engine's state 1 (scan window / interval on 37 / 38 / 39, an ADV report into an RX buffer +
  RX IRQ, RSSI words, SCAN_REQ sent by the engine on an ADV_IND or not: a config flag until §21 says) and several
  **virtual advertisers** on the air (`ble_air`): BLE-MIDI controllers with names in SCAN_RSP, a non-MIDI beacon, an
  RPA advertiser. Checks: the list in a screenshot, the beacon filtered out, the IRQ rate and the audio handler's
  time while scanning (`FM1_BLE_ISR=1`).
- **Hardware**: DEVICES opened with the **iPhone / iPad app of P3 advertising** (it must appear by name) and with the
  Mac's *Advertise* on: names right, the list stable, no audio click while scanning (RX IRQ per report).
- **Risks**: §21 (scan state, SCAN_REQ by the engine); IRQ load in a crowded room (hundreds of reports a second: scan
  only while DEVICES is open or searching, drop non-MIDI reports early in the IRQ); RSSI meaningless until U9.
- **Size**: +1.8-2.7 KB flash, +0.4 KB RAM.

### P3: connect as central to a BLE-MIDI peripheral, MIDI both ways

- **Build**: initiator, master LL, driver state 3 / 6, GATT client discovery + CCCD + notifications, out as Write
  Without Response; the router's role switch; **the SMP initiator** (an iOS / macOS peripheral enforces pairing, so
  the first hardware test needs it); NEEDS PAIRING when the CCCD write fails and pairing does not complete.
- **Host**: `ble_stack_test.c` gets a **simulated peripheral** (the mirror of today's simulated central): CONNECT_IND
  fields valid, our procedures, its LL_CONNECTION_PARAM_REQ answered with an update at an instant, discovery against a
  controller-shaped database (MIDI service not first, extra services), notifications decoded, writes encoded, pairing
  as initiator, link loss → notes released.
- **Emulator**: **add** the engine's state 3 (on an ADV_IND from the target, the CONNECT_IND from a TX buffer sent
  T_IFS later, column 9 bit 0) and state 6 (master: TX at the anchor, then RX; SN / NESN the same machinery mirrored),
  and a **virtual BLE-MIDI peripheral** (`ble_peripheral`: advertiser, slave LL, GATT server with the MIDI service, a
  scripted keyboard sending notes, optionally "needs encryption" and an RPA address like iOS). Checks: a note from the
  virtual keyboard plays the synth (the non-silent frame count, as `ble_emu_test.py` does for the central), the FM-1's
  notes reach it, a connection update at its instant, the peripheral vanishing → notes off, back to the previous
  state.
- **Hardware (the only test device is the iPhone / iPad): an iOS app that advertises the BLE-MIDI peripheral
  service.** Recommended, in this order (sources in §8):
  1. **BluePiano LE** (virtual Bluetooth MIDI keyboard): advertises the Bluetooth MIDI service as soon as it
     launches, and its on-screen keys send MIDI to any connected client: one app does both the peripheral role and
     the notes. The first choice.
  2. **AUM** (Kymatica): MIDI routing > Bluetooth icon > role **Peripheral** > **Advertise MIDI Service**; it routes
     the iPad's other apps and can send notes back, so both directions are testable.
  3. **midimittr**: a free BLE-MIDI-to-iOS-apps utility; its listing does **not** say it advertises, so it is a
     candidate only after trying: it appears in the FM-1's list or it does not.
  4. Any app that wraps Apple's `CABTMIDILocalPeripheralViewController` (the system way to advertise an iOS device as
     a BLE-MIDI peripheral) is the same thing underneath; a **Mac with Advertise on** is a second peripheral to try.
  Tests: it shows in DEVICES (P2), YES pairs (SMP initiator) and connects, keys played in the app play the FM-1's
  synth, the FM-1's MIDI out reaches the app, switching the app off or locking the iPad drops the link with no stuck
  note, a long session for drops. **No controller is needed for P3**; a real controller test (CME / KeyStep / WIDI)
  waits until the user has one (not a phase gate).
- **Risks**: the master timing is the engine's and unmeasured [HW? §21]; our SCA claim and the PLL accuracy; **iOS
  advertises from an RPA** (the connect works from the RPA seen in the scan; finding it again is P4 and needs the
  IRK from the bond) and an iOS app stops advertising when it is not in the foreground [I: unmeasured]; some
  controllers want a long connection interval; Wi-Fi / BT coexistence of the shared radio (RFPRIO, U11).
- **Size**: +3.9-5.6 KB flash (incl. the SMP initiator 1.0-1.3 KB), +0.2 KB RAM.

### P4: auto-reconnect

- **Build**: LAST selected = reconnect; the search state machine with time-slicing (§1.4.1), 30 s then every 10 s;
  initiating with the target address; RPA peers by scan + resolve; the peripheral-side match (P1) into the same `sel`
  logic.
- **Host**: the state machine on a fake clock: found at once, found after 20 s, never (slow retry), a Mac connecting
  during the search (it wins), BLUETOOTH OFF during a search, a link lost and found again.
- **Emulator**: boot with a flash dump that has a role C entry and `sel` = LAST: the virtual peripheral appears after
  3 s, the FM-1 connects with no key pressed; the peripheral disappears and comes back; a boot with the peer absent
  keeps the FM-1 advertising (a virtual central can still connect).
- **Hardware (iPhone / iPad app, Mac)**: bond once, power-cycle the FM-1 and the app in turn, both orders; count the
  seconds to MIDI. The iPhone case also measures whether the IRK resolves iOS's RPA.
- **Risks**: an iOS app that is not advertising (backgrounded) is not found: long searches; the radio time spent
  searching forever (every 10 s costs ~1 s of 10); a flash save on every connection (avoided: §3.1, only when quiet).
- **Size**: +0.3-0.5 KB flash, ~0 RAM.

### P5: FM-1 to FM-1 (emulator first; hardware when a second unit is available)

- **Build**: KIND = FM-1 from the `FM-1 ` name; the "both pick each other" rule (§1.3); the arp loop guard if the test
  shows the loop. BLE clock and transport are **later**, a separate piece.
- **Host**: two stacks linked through two fake drivers in one process (the simulated central and peripheral become
  real stacks): connect, MIDI both ways, both initiating at once.
- **Emulator (the test of record until a second FM-1 exists)**: **add** two machines on one virtual air in one process
  (`diagnose` with `FM1_BLE_PEER=<fwsc>`): both 24 MHz time bases stepped in lock-step, the air shared. Before that
  exists, the virtual peripheral of P3 configured with the FM-1's own advertising and GATT layout stands in.
- **Hardware**: **deferred: when a second FM-1 is available.** Until then the Mac / iPhone cover each role separately
  (P1 as peripheral, P3 as central).
- **Risks**: the two-machine emulator is the biggest emulator task here; the arp ping-pong; both FM-1s searching each
  other while one also advertises.
- **Size**: 0-0.3 KB.

### Order and effort

P1 and P2 are independent (P1 is peripheral-only and useful alone; P2 needs §21 for the scan state). P3 needs P2; P4
needs P1 + P3; P5 needs P3 (P4 for reconnect). With one remembered device the store, its UI and its tests shrank
(no LRU, no multi-entry migration, no AUTO cell); the effort that remains is the emulator: the engine's states 1 / 3 /
6 and a virtual peripheral are each about the size of today's advertising / connection model (`advertising.rs` 303
lines, `connection.rs` 460, `ble_central/` ~1,700) [M: line counts].

## 6. Decisions and open items

### 6.1 Decided (user rulings and the recommended answers taken)

| # | Question | Decided |
| --- | --- | --- |
| 1 | How many remembered devices | **One** (the last) |
| 2 | Searching for the last device | **30 s, then every 10 s while ON** |
| 3 | Auto-connect at boot | **LAST selected = reconnect while ON**; no AUTO setting; BLUETOOTH stays OFF by default |
| 4 | Advertised name | **`FM-1 XXXX`** (last 4 hex digits of the address) |
| 5 | Pairing as peripheral | **Insufficient Authentication**, per Apple's guideline (P1) |
| 6 | Links | **One at a time** |
| 7 | Bars before U9 | **Relative, dim** |
| 8 | YES while BLUETOOTH is OFF | **Switches ON and connects** |
| 9 | Clock / transport between FM-1s | **Later** |
| 10 | Test devices | **Mac + iPhone / iPad only**; central tested against an iOS BLE-MIDI peripheral app (P3); FM-1 to FM-1 in the emulator, hardware when a second unit exists |
| 11 | Scan list filter | **BLE-MIDI devices only** |
| 12 | Builder split | **Two items**: `BLE_BOND`, `BLE_CENTRAL` (§4.7) |
| 13 | Settings word | **Bit 24 = BLUETOOTH ON; no new bits** |

### 6.2 Still open

0. *(Answered by the ruling, §0.1: a device that connects to us never replaces LAST.)*
1. **Which iOS app does the user have or want to install** for P3: BluePiano LE (recommended) or AUM? A one-line
   answer; nothing else about P3 depends on it.
2. ~~When a different device connects to us while LAST is a central-role device~~: **decided** (§0.1), it never
   becomes LAST.
3. Measurements, not decisions (they come out of P1 / P3 and may change the design): whether macOS / iOS reconnect an
   FM-1 peripheral by themselves (§1.1); whether the iOS peripheral app accepts a connection without pairing (§4.3,
   assumed it does not); whether directed advertising helps (§3.3); whether the engine can run two links (§1.4.1).

## 7. What this design needs from the fact sheet (§21, pending)

- Scanning (state 1): the scan window / interval columns; whether the engine sends SCAN_REQ by itself (active scan)
  and where SCAN_RSP lands; the RX buffer layout of advertising reports; whether FILTERCNTL can drop non-matching
  reports.
- Initiating (state 3): how the CONNECT_IND is supplied (a TX buffer, filled before) and sent T_IFS after the target's
  ADV_IND (it must be the engine); TARGETADR vs WHITELIST0; column 9 bit 0; how the link turns to state 6 and where
  the first anchor is (the vendor programs column 0 / 14 with 2 x WinOffset + 4 for a central, HW §7).
- Master (state 6): TX at the anchor first, the event IRQ, the MD rules as master.
- Directed advertising: ADV_DIRECT_IND's type and TargetA (FORMAT, TARGETADR), high duty cycle.
- Two links at once (advertising + scanning): possible or not (§1.4.1 works either way).
- RSSI gain table (U9) for real bars.

## 8. Sources (P3's iOS peripheral apps; read 2026-10-09 through a web search, not tried on hardware)

- BluePiano LE, App Store listing: https://apps.apple.com/app/id1114357838 (a virtual Bluetooth MIDI keyboard that
  advertises the Bluetooth MIDI service on launch).
- AUM, peripheral role and *Advertise MIDI Service* (a user guide on the Audiobus forum):
  https://forum.audiob.us/discussion/comment/1210005/ ; Bluetooth LE MIDI added in AUM 1.21:
  https://synthanatomy.com/2017/09/kymatica-updated-aum-to-v-1-21-with-midi-clock-send-bluetooth-le-midi-more.html
- midimittr, App Store listing (no peripheral mode stated): https://apps.apple.com/app/midimittr/id925495245
- Apple, `CABTMIDILocalPeripheralViewController` (advertises an iOS device as a Bluetooth MIDI peripheral):
  https://developer.apple.com/documentation/coreaudiokit/cabtmidilocalperipheralviewcontroller
- Apple, Technical Q&A QA1831 (central / peripheral roles for Bluetooth MIDI, pairing enforced):
  https://developer.apple.com/library/ios/qa/qa1831/_index.html
- Apple, Audio MIDI Setup guide (Bluetooth configuration on the Mac): https://support.apple.com/guide/audio-midi-setup/ams33f013765

## 9. Round 1, built (branch `feat/ble-devices`)

What exists after round 1 (host- and emulator-tested; nothing ran on an FM-1). Connecting out (the initiator, the
master link, the GATT client, the SMP initiator, auto-reconnect) is the next round.

| Part | Where | Notes |
| --- | --- | --- |
| Name `FM-1 XXXX` | `ble/ble_host.c gap_name`, `ble_att.c` (GAP Device Name from RAM) | XXXX = address octets 1, 0 in hex; scan response 0x09 |
| One-device store | `ble/ble_store.c`, `persist_t.ble_dev` (70 B, after `ble_bond`) | mark 0xB6, version 1, `sel` NONE / LAST; LAST only with role C |
| DEVICES list (SLOOP menu) | `ui/sloop/ui_menu.c` (BLUETOOTH screen K2 DEVICES, `ui.menu` 3), `io/midi/ble_devices.c` | NONE, LAST, nearby; PRESETS / SELECT move, OCT+ picks, KNOB 4 on LAST arms FORGET (OCT+ in 3 s), OCT- back. LAST picked: kept, "RECONNECT NOT YET"; a nearby device: the pending choice (RAM), "CONNECT NOT YET". Only with BLE built in. The Optimist UI (§2.2) is not in this code base |
| Scanner | `ble/ble_ll.c` (`ble_ll_scan`, the 8-report ring), `ble/ble_hw_wl82.c` (state 1, HW §21.2), `ble/ble_scan.c` (AD parser, 8-entry table, ageing 10 s, relative bars) | one link, time-sliced: scanning only while DEVICES is open and nothing is connected; advertising again when it closes; active with the Core backoff; channel 37 / 38 / 39 moved in the event IRQ; RX by RXBUFnCNTL bit0, else by RXTOG having moved past (both counted) |
| Bonding | `ble/ble_smp.c` (builder item `BLE_BOND`, off by default) | the responder now also distributes our identity (Identity Information + our static address) when asked and hands the central's IRK + identity address to the firmware (`ble_app_peer_id`, RAM only, §0.1) |
| Settings word | `storage/settings_word.c SETTINGS_BLE_ON_BIT` | 23 on this branch (main: 24): the merge changes that one line |
| Builder | `BLE_BOND` (bit 253), `BLE_CENTRAL` (bit 254), both children of `BLE`, off by default | `BLE_CENTRAL` does not need `BLE_BOND` in round 1 (the SMP initiator that needs it is round 2). Measured (costs.json, user-default + BLE): `BLE_BOND` +6.7 KB flash / +0.7 KB RAM, `BLE_CENTRAL` +6.7 KB / +1.0 KB (§4.7 estimated 4.0-4.4 and the scan part of 6.0-8.8) |
| blell | `ble_dgs` (its own block: `ble_dg`'s layout unchanged) | `scan_*` lines: starts, events, RX found by CNTL / TOG / none, reports by type, ring overflow, SCAN_REQ armed / failed, SCAN_RSPs, the last RSSI / headers / CNTL / TOG |
| Emulator | fm1-emulator-ble `feat/ble-engine`: `ble_engine/scanning.rs`, `FM1_BLE_ADVERTISERS=default` | state 1 windows, the engine's SCAN_REQ, virtual advertisers (BLE-MIDI with the name in the ADV_IND or the scan response, another FM-1, an RPA iPad app, a beacon, a non-connectable BLE-MIDI one); `FM1_BLE_MODEL=scan_cntl=0` / `scan_adva=0` for C2 / C1 the other way |

**Hardware session for round 1** (the user's; with `BLE BLE_BOND BLE_CENTRAL BLE_DIAG USB_MODE=1`): open DEVICES with an
iPhone / iPad app advertising BLE MIDI (§5 P3) and with the Mac's Audio MIDI Setup *Advertise* on; `blell` after a few
seconds. It settles HW §21.9 **C1** (`scan_scan_rsp` > 0 and names that only the scan response carries: the engine
filled AdvA in the SCAN_REQ), **C2** (`scan_rxf_cntl` against `scan_rxf_tog`: whether RXBUFnCNTL bit0 marks a report
while scanning), **C8** (column 15 bit15 = 0 while scanning: the scan repeats, `scan_events` grows), **C12** (reports on
all three channels: `scan_last_ch` over several reads), and what `RSSI2` looks like (U9, `scan_last_rssi`). Also: the
list closes into advertising again (the Mac still finds `FM-1 XXXX`), no audio click while scanning.

**Round 1 on the FM-1** (b821e3e, the user's session, 2026-10-09) [M:hw]: DEVICES opened with an iPhone app and the
Mac advertising: `scan_events` 1,235, `scan_rx_irqs` 5,612, reports by RXBUFnCNTL bit0 (`scan_rxf_cntl` 6,301,
`scan_rxf_tog` 0), `scan_adv_ind` 2,555, `scan_scan_rsp` 2,100, `scan_req_armed` 2,412, `scan_rsp_ok` 1,935, no ring
overflow; the raw RSSI word about `0x7316`. So HW §21.9 **C1 = yes** (the engine sends the SCAN_REQ and fills AdvA),
**C2 = yes** (RXBUFnCNTL bit0 marks a report while scanning), **C8: scanning runs** (column 15 bit15 = 0). Picking a
device showed "CONNECT NOT YET" (round 1). An iPhone (midimittr) connected **to** the FM-1 as central: that works too.
(To be recorded in the fact sheet's §21.9 through the docs session.)

### 9.1 Round 2, built (branch `feat/ble-devices`): connecting out

What exists after round 2 (host- and emulator-tested; nothing ran on an FM-1). Details: `BLE-STACK.md` §13.

| Part | Where | Notes |
| --- | --- | --- |
| Initiator (state 3) | `ble/ble_ll_central.c ble_ll_connect`, `ble/ble_hw_wl82_central.c` (HW §21.3) | our CONNECT_IND: random AA (Core rules), random CRCInit, WinSize 2, WinOffset [Interval / 2, Interval − 1], Hop 5..16, all channels, **interval 9 (11.25 ms)**, latency 0, timeout 2 s, SCA 0; the target in WHITELIST0 / TARGETADR, FILTERCNTL bits 0 / 4 / 8; column 9 = 1; the target's ADV_IND seen in the RX IRQ, the switch to state 6 in the next event IRQ |
| Master link (state 6) | the same files, `ble_ll.c`, `ble_hw_wl82.c` (`drv.master`) | anchor counter 2 × WinOffset + 4, column 2 `0x6000`, WINCNTL 0 / 30 µs after the first packet; the §8 RX and §8.2 TX rules unchanged; version / features / length started by us; the peripheral's feature exchange, PHY request, connection parameters request and L2CAP update request answered (an update at counter + 8..11); a channel map update of ours at + 7..10; LL encryption as master; supervision on TIMER4 |
| GATT client | `ble/ble_gattc.c` | MTU, Find By Type Value (or Read By Group Type), Read By Type, Find Information, CCCD = 1; notifications in, Write Without Response out |
| SMP initiator | `ble/ble_smp_init.c`, `ble/ble_central.c` | legacy Just Works with bonding, keys both ways; on a Security Request, on Insufficient Authentication / Encryption, or on a bond the peer lost; a bonded LAST encrypted at once with its LTK; the 30 s timeout |
| LAST and reconnection | `io/midi/ble_connect.c` | a pick becomes LAST (and the choice) once ready, with the pairing's bond and identity; the search (2 s initiate / 1 s advertise for 30 s, then 1 s every 10 s); LAST with an IRK found by scanning and resolving (ah); incoming connections accepted, never LAST; NONE leaves our link |
| UI (SLOOP menu) | `ui/sloop/ui_menu.c` | `CONNECTING` / `SEARCHING` / `CONNECTED` states; the row tags `CONNECTING` / `CONNECTED`; `CONNECTED <name>`, `LOST <name>`, `FAILED: <why>`; LAST not listed twice |
| blell | `ble_dgc` (`ble_diag.c bd_central`) | every step of the initiator, the master, its procedures, the GATT client, SMP and the search; HW §21.9 C3-C6 points (`BLE-STACK.md` §13.7) |
| Builder | `BLE_CENTRAL` (bit 254) | now brings bonding (`BLE_LL_ENC`, `BLE_SMP_LEGACY`) with it |
| Emulator | fm1-emulator-ble `feat/ble-engine` (33f481b, bbd2f22) | the engine model's states 3 / 6 (flags `init_cind`, `init_evt_after_cind`, `master_anchor_from_counter`, `master_tx_first` for C3-C6) and virtual BLE-MIDI peripherals (`FM1_BLE_PERIPHERALS`: pairing like Apple, Security Request, an RPA with an IRK like a Mac); `tests/ble_emu_central_test.py`: pick → connect → pair → discover → CCCD → MIDI both ways → LAST; reboot → LAST reconnects by itself (LTK; a Mac-like RPA resolved); NONE leaves |

**Merge note**: on main, `BLE_DIAG` is builder bit 253 and BLUETOOTH settings bit 24; this branch's `BLE_BOND` is bit
253 (a collision to settle when merging; not renumbered here) and its BLUETOOTH bit is 23 (`settings_word.c`).

**Round 2 on the FM-1** (90d2eeb, 2026-10-09, `hw-logs/blell-dev3.txt`; details `BLE-STACK.md` §13.8) [M:hw]:
against an iPhone app advertising BLE-MIDI, **HW §21.9 C3 = yes** (the engine sends our CONNECT_IND: `master_starts`
6, `m_events` 1,130 / `m_events_rx` 1,114), **C4** 510 µs (max 1,883), **C5 / C6 OK** (first RX at event 0, the peer
answers every event). LL encryption as master, **Just Works pairing completes and encrypts** (`si_done` 4,
`m_enc_on` 4), discovery finds the MIDI service (0039–003D, value 003B, CCCD 003D, MTU 247). But **the iPhone refuses
the CCCD write with 0x05 Insufficient Authentication even after Just Works** (and sends Pairing Failed 0x08 right
after the key distribution): it needs an **authenticated (MITM) key**. Round 2 then re-paired on every attempt (a
prompt on the phone each time) and fell back to scanning with no reason on screen. Round 3 (§9.3) answers that.

### 9.2 UI rules (the "optimist ui" session, which owns both UIs and reviews the DEVICES screens)

- No generic list widget exists: `draw_menu` draws up to 4 fixed rows in two bands of at most 124 rows each (the
  canvas limit); a scrolling list has its own small draw (`draw_devices`) that keeps the two-band rule and the menu's
  head + rule look.
- The menu's conventions: capitals as in the MI names; OCT+ acts, OCT- goes back, PRESETS / SELECT move the cursor;
  KNOB 4 then OCT+ within 3 s for FORGET.
- Signal bars are graphic (bars, not numbers). NONE and LAST stay pinned at the top. Nothing wraps: the cursor stops
  at the first and last rows.
- No new gestures in `ui_input.c` (the tempo stream changes `layers_input` / `btn_hold` there).
- Later, not this round: the Optimist UI version goes inside `op_project.c` only, minimal (it is frozen): sentence case
  for words, capitals only for short labels and acronyms (BLE, MIDI), no footer, the centred confirm modal for FORGET,
  SAVE = yes / right, HOME = no / left.

### 9.3 Round 3, built (branch `feat/ble-devices`): security only when asked, the passkey when proven necessary

The user's requirement: **no dialog unless necessary**. Details and sources: `BLE-STACK.md` §13.9.

| Rule | Where | |
| --- | --- | --- |
| No pairing, no prompt, unless the peer asks (Security Request, ATT 0x05 / 0x0F / 0x0C) | `ble/ble_central.c bcen_secure` | FM-1 to FM-1 and quiet controllers: no SMP at all (`tests/ble_f2f_test.c`) |
| Just Works first, silent on the FM-1 (NoInputNoOutput) | `ble/ble_smp_init.c ble_smp_pair(0)` | another FM-1 with `BLE_MIDI_NEED_ENC`, most controllers |
| A passkey only when the peer proves it needs MITM: 0x05 again after a Just Works bond, or MITM in its Security Request | `BLE_CF_NEED_MITM` → `io/midi/ble_connect.c rc_escalate`: the link left, made again once to the same address | LE legacy passkey entry, the FM-1 displays (DisplayOnly + MITM), the phone's user types; a peer that cannot type: `FAILED: AUTH` at once |
| One attempt per level, no loop | `rc_link` → `RC_HELD` | `FAILED: PAIRING` / `FAILED: AUTH` stays; nothing connects or prompts again until a pick, LAST, NONE, FORGET or BLUETOOTH OFF |
| The level kept with LAST | `ble/ble_store.c` `struct ble_dev.sec` (`BLE_DEV_SEC_MITM`, `BLE_DEV_SEC_AUTH`) | reconnects with the authenticated LTK, no dialog; a lost bond pairs straight with the passkey |
| Screens | `ui/sloop/ui_menu.c draw_devices`, `ble_connect_status` | header `CONNECTING` / `PAIRING` / `CONNECTED` / `FAILED`; the row tag `CONNECTING` / `PAIRING`; status `CONNECTING <name>`, `PAIRING <name>`, `ENTER THIS CODE ON THE PHONE` + the six digits large, `CONNECTED <name>`, `FAILED: <why>` kept |
| blell | `ble_dgc` | `si_rsp_io`, `si_rsp_auth`, `si_mitm_req`, `si_passkey`, `si_auth_done`, `si_fail_late`, `cen_need_mitm`, `rc_phase` 6 (held) |
| Emulator | fm1-emulator-ble `feat/ble-engine` 63bf79e | `FM1_BLE_PERIPHERALS=midi:NAME:auth[:typo]` (iPhone-like), `FM1_BLE_PASSKEY_AT` (its user reads `smp_passkey` and types it); `tests/ble_emu_central_test.py` iphone / typo |

**Hardware test for round 3** (the user's; build `BLE USB_MODE=1 BLE_BOND BLE_CENTRAL BLE_DIAG`): first **forget
"FM-1 XXXX" in the iPhone's Settings > Bluetooth** if it is listed (round 1's bond from the iPhone connecting to the
FM-1: an inferred second cause of the refusal, §13.8). Then the app advertising, DEVICES → pick: `PAIRING` briefly
(Just Works; the phone may ask to pair: accept), then `ENTER THIS CODE ON THE PHONE` and six digits; type them in the
phone's prompt → `CONNECTED <name>`, notes both ways. `blell`: `si_rsp_io` 04, `si_mitm_req` 1, `si_passkey` 1,
`si_auth_done` 1, `cen_need_mitm` 1, `gc_subscribed` 1. Power-cycle the FM-1: it reconnects by itself with no
prompt (`si_ltk_enc`). If it shows `FAILED: AUTH` after the passkey instead, iOS wants LE Secure Connections (§13.9).

# BLE MIDI devices: a list to connect from, the last device remembered, reconnect (design)

Status: **DESIGN, nothing built.** Branch `feat/ble-devices` (from 1e0ad67, route C as in `BLE-STACK.md`). The user's
sketch: *"last device: none, MyLastDevice (last), device one, device 2, etc"*; the devices can be controllers,
keyboards, a Mac, a phone, or another FM-1.

Tags as in `BLE-STACK.md`: **[M]** measured, **[S]** from a published specification, **[I]** inference, **[HW?]**
needs the hardware fact sheet or a device, **[E]** an estimate (sizes: from the measured sizes of the parts that exist
today, `BLE-STACK.md` §5.1, §8, §11.6; not compiled).

Inputs: `BLE-STACK.md` (this branch), `BLE-HW-FACTS.md` (feat/ble-facts d0b4bb7, which ends at §20: **§21, central
role, has not landed when this was written**; everything below that needs it is marked [HW? §21]),
`UI-OPTIMIST-DESIGN.md` (optimist main f3aacb2), `ui/sloop/ui_menu.c` (this branch), `ui/optimist/op_project.c` /
`op_draw.c` (main), fm1-emulator-ble `feat/ble-engine` 6531e20.

## 0. Summary

- **One link, one role at a time.** The FM-1 is either a peripheral (VISIBLE, today: a Mac or phone connects to it)
  or a central (it connects to a device it picked from a list: a controller, a Mac or phone that advertises
  BLE-MIDI, another FM-1). Never both connected at once (MIDI router and RAM stay as they are).
- **A DEVICES list**: `NONE` (stay visible, today's behaviour), the remembered devices (the last one first, marked
  LAST), then what a scan finds, named from the advertising data or the scan response, with signal bars once RSSI is
  calibrated. **YES / OCT+ connects** to the cursor row, **FORGET** removes a remembered one (a confirm: it drops the
  bond).
- **A small known-device store** (4 entries x 68 B + an 8 B header = 280 B), appended to the settings record
  (`OBJ_SETTINGS`, 0xFC000 / 0xFD000), replacing today's `ble_bond[28]`. Never 0xE7000-0xE9FFF.
- **Auto-reconnect**: at boot (when BLUETOOTH is ON) and when BLUETOOTH is switched ON, the FM-1 goes back to the
  last device in the role it had: as central it initiates to that address (with the stored IRK when it is
  resolvable); as peripheral it advertises (directed advertising to a bonded central only if the hardware test says a
  Mac answers it).
- **Five phases**, each tested in the emulator and on the FM-1: P1 bonding + last device as peripheral, P2 scan list,
  P3 central connect + MIDI, P4 auto-reconnect, P5 FM-1 to FM-1. About **11-15 KB of flash and 1.2-1.6 KB of RAM for
  all of it [E]** against ~50 KB / ~10 KB free in user-default + BLE; split into builder options (§4.7).
- **Settings word**: BLUETOOTH ON is **bit 24** on main (bit 23 is the Optimist UI's CARDS, 21-22 HOLD); this design
  adds **no** settings-word bit: the new switches (AUTO, the last device) live in the device store's header (§3.1).

## 1. User stories and roles

### 1.1 FM-1 as peripheral (today), with bonding and "last device"

*"I connect my Mac once in Audio MIDI Setup; after that, when I power the FM-1 on, the Mac finds it again, and the FM-1
shows MacBook Pro as its last device."*

- Today: the FM-1 advertises `FM-1_BLE` with the BLE-MIDI UUID from a random static address made once and kept with
  the settings (`persist_t.ble_addr`), so its identity is stable across power-offs [M]. A Mac connects, MIDI goes
  both ways [M:hw]. Bonding exists, opt-in (`OPTIMIST_BLE_SMP=1..3`): LE legacy Just Works, one key slot found by
  EDIV / Rand; the central's IRK and identity address are read and **dropped** (`ble_smp.c smp_their_key`) [M].
- New: the central that connects becomes the **last device** (role P = it connected to us). Its name is read with
  one GATT client request (Read By Type, GAP Device Name 0x2A00, on the central's own GATT server) [I: macOS and iOS
  both expose a GAP service with the name; check on hw]. When it bonds, its IRK and identity address are kept, so
  the next connection from it is recognised even when it uses a resolvable private address (RPA) [S: Core Vol 3
  Part H 2.4.2.2].
- Reconnect is the **central's** decision in this role. What the FM-1 can do: advertise from boot with the same
  address (it already does), and, if the hardware shows a Mac answers it, **directed advertising** (ADV_DIRECT_IND to
  the bonded central's identity address) for a few seconds first. Whether macOS / iOS reconnect a BLE-MIDI
  peripheral by themselves after a power cycle is **unmeasured** [I]; P1's hardware test measures it with and
  without bonding.

### 1.2 FM-1 as central, connecting to BLE-MIDI peripherals

*"I switch my KeyStep / nanoKEY / CME / WIDI on; on the FM-1 I open DEVICES, it is in the list, I press OK and play
it; next time the FM-1 connects to it by itself."*

- The FM-1 scans (active: it sends SCAN_REQ to get the name, which most controllers put in the scan response [I]),
  lists the advertisers whose AD carries the BLE-MIDI service UUID (`03B80E5A-...`), connects as central, discovers
  the MIDI characteristic, turns its notifications on (CCCD), and from then: notifications in = MIDI in (the decoder
  of today), MIDI out = Write Without Response to the characteristic (the encoder of today).
- A Mac whose Audio MIDI Setup *Bluetooth Configuration* has **Advertise** on, or an iOS app that advertises a
  BLE-MIDI peripheral, shows up in the same list and is connected the same way [I: macOS advertises the MIDI UUID
  then; iOS advertises from an RPA, so it is only found again after a bond, §1.5].
- Some peripherals ask for encryption (Insufficient Authentication on the CCCD write) [I: a minority of
  controllers]. That needs SMP **as initiator** (§4.3); without it the FM-1 shows *NEEDS PAIRING* and stays
  disconnected.

### 1.3 FM-1 to FM-1

*"Two FM-1s: one plays the other's synth, or both play each other."*

- One FM-1 is VISIBLE (peripheral, ON, nothing picked: today's behaviour); the other opens DEVICES, sees it
  (`FM-1 A1B2`, §6 Q4 about the name) and picks it: it becomes the central. MIDI flows both ways on the one link:
  each FM-1's own notes (the stream USB gets) out, the other's in to its synth, as today.
- No echo: notes that came in from MIDI are never sent back out (`seq_midi.c` rules) [M: code]. **One loop
  remains**: the arp plays held MIDI notes and its notes go out, so two FM-1s with the arp on and MIDI OUT = SEQ can
  ping-pong [I]. Test in P5; a fix if needed: arp output from a BLE-held note not sent back to BLE.
- Clock: BLE clock and transport are not passed on today (`midi_ble.c`, `clock_sync.c` follows USB and TRS). Tempo
  sync between two FM-1s over BLE is the obvious next wish (open question Q9).
- If both FM-1s pick each other at once: the first CONNECT_IND wins. The loser's link comes up as **peripheral** to
  the very device it was searching for; it counts that as success (the peer's identity address matches the entry),
  stops initiating and keeps the link.

### 1.4 Both roles possible: what the FM-1 does when

| BLUETOOTH | The picked device (store header `want`) | The FM-1 | Shown |
| --- | --- | --- | --- |
| OFF | (any) | radio off (today: never started at boot when OFF) | `OFF` |
| ON | NONE, or a central (role P) | advertises, waits for a central | `VISIBLE` / `CONNECTED <name>` |
| ON | a peripheral (role C), AUTO on | **SEARCHING** it: initiates to its address; between tries it advertises, so a Mac can still connect (§1.4.1) | `SEARCHING <name>` / `CONNECTING` / `CONNECTED <name>` |
| ON | DEVICES screen open | scans continuously (advertising paused while the list is open) | `SCANNING` |

#### 1.4.1 Advertise while scanning?

Whether the engine can run two links at once (one advertising, one scanning / initiating) is not known [HW? §21:
the control block is per link and the emulator models 8 links, but no oracle ran two]. The design does not need it:
**software time-slicing on one link** (stop advertising, initiate for a window, back to advertising) works with any
answer, because the link layer already starts and stops advertising cheaply (`ble_hw_adv_start/stop`). Proposal:

- **DEVICES open**: scan only (100 % of the radio: the list fills fast); advertising resumes when the screen closes.
- **SEARCHING the last device**: initiate 2 s, advertise 1 s, repeat for 30 s, then initiate 1 s every 10 s (the
  rest advertising) until found or BLUETOOTH OFF / NONE picked (Q2). Whatever link comes up first wins; the other
  activity stops.
- If §21 shows two links work, the same state machine runs both at once; nothing in the UI changes.

## 2. The UI

### 2.1 The screen and the fonts

240 x 240 px, RGB565. One font, Terminus 8 x 16 (`FONT_S`, Latin-1: names with lower case and accents), and
`FONT_L`, the same glyphs drawn 2 x 2 (16 x 32, ASCII 32..95: capitals, digits, signs only) [M: tools/gen_font.py].
So the mockups below are exact on a grid of **30 columns x 15 lines** (1 character = 8 x 16 px); a `FONT_L` word takes
2 columns and 2 lines a letter. Device names are always `FONT_S` (they have lower case), cut to fit with `~`.

Signal bars: `RSSI2` gives dBm only through a 64-entry gain table that is not transcribed (HW §2 0x138, U9) [M:s].
Until U9 is measured, the bars are **relative** (the raw RSSI ranked: the strongest of the list 3 bars) and drawn
dim; the list order never uses them (§6 Q7).

Two UIs exist; a build has one (builder `UI`): SLOOP's HOME-held menu (`ui/sloop/ui_menu.c`) and the Optimist UI
(`ui/optimist`, main). The BLE rows go into both.

### 2.2 Optimist UI (main): SYSTEM > BLUETOOTH, and the DEVICES list

Its grammar (UI-OPTIMIST-DESIGN.md §2): SELECT = the cursor row, KNOB 1..4 = the row's four cells, PRESETS = the hot
cell fine / scrolls a list, **SAVE tapped = YES** (enter a ▸ row, toggle, do), **HOME tapped = NO** (back), **HOME
held + a knob = clear** the cell (here: FORGET), a confirm is a modal box (*HOME no* left, *SAVE yes* right, red frame
when it destroys). Layout (`op_draw.c`): header y 0..24, four cards y 28..72, the panel y 76..239 in 20 px rows (8
rows; 5 with CARDS 2x2), no footer.

SYSTEM gains one row, **BLUETOOTH** (after USB; only in a BLE build):

| Cell | Label | Value | Knob / YES |
| --- | --- | --- | --- |
| K1 | BLE | ON / OFF (bit 24) | right ON, left OFF; YES toggles |
| K2 | DEVICES | ▸ | YES opens the DEVICES list (an action cell) |
| K3 | STATE | VISIBLE · SCANNING · SEARCHING · CONNECTING · CONNECTED · NEEDS PAIRING · NO RF CAL (read-out) | — |
| K4 | AUTO | LAST / OFF (store header) | reconnect the last device when ON (§3.3) |

SYSTEM, cursor on BLUETOOTH, connected as central (30 x 15 grid; `[ ]` = a card, `>` = the cursor bar):

```
DRUMS   SYSTEM  BLUETOOTH  120
                              
[BLE  ][DEVICES][STATE  ][AUTO]
[ ON  ][   >   ][CONNECT][LAST]
[=====][       ][KeySte~][====]
 CHANNELS  1     2     3   10 
 USB       OFF                
>BLUETOOTH ON    >  CONN  LAST
 CPU       23%   240          
 CALIBRATE PANEL 350          
 ABOUT     OPTIMIST           
                              
  CONNECTED  KeyStep 37       
                              
```

DEVICES (a list screen like PROJECT's slots: the cursor row is the item). Rows: `NONE`, the remembered devices
(LAST first, then by last use), then the scan's finds that are not remembered (strongest first, new ones at the
bottom so the list does not jump under the cursor). The cards show the cursor device:

| Cell | Label | Value |
| --- | --- | --- |
| K1 | NAME | the name (FONT_S in the card), or the address `C4:7F:..` when it has none |
| K2 | SIGNAL | bars (dim until U9), `--` when not heard in this scan |
| K3 | KIND | `MIDI` (a BLE-MIDI peripheral) · `FM-1` (an FM-1: its name, §6 Q4) · `HOST` (a central that connected to us: Mac, phone) |
| K4 | STATE | `LAST` · `BONDED` · `CONNECTED` · `NEW` |

```
DRUMS  BLUETOOTH DEVICES  SCAN
                              
[NAME   ][SIGNAL][KIND][STATE ]
[KeySte~][ ▮▮▮  ][MIDI][ LAST ]
[       ][      ][    ][======]
 NONE        stay visible     
>KeyStep 37        LAST   ▮▮▮ 
 MacBook Pro       BONDED  -- 
 FM-1 A1B2                ▮▮  
 nanoKEY Studio           ▮▮  
 WIDI Master              ▮   
 MD-BT01                  ▮   
                              
```

Keys on DEVICES:

| Control | Does |
| --- | --- |
| SELECT / PRESETS | the cursor through the list (stops at the ends) |
| **YES** (SAVE tapped) | on a device: **CONNECT** (as central to a MIDI / FM-1 device; to a HOST row: *WAITING* — a central must connect to us, so YES makes it the wanted one and advertises, directed if P1 says so); on the connected one: **DISCONNECT** (amber confirm); on NONE: drop the link we made and stay VISIBLE |
| **NO** (HOME tapped) | back to SYSTEM (the scan stops, advertising resumes) |
| **HOME held + a knob** (on a remembered row) | **FORGET**: modal *FORGET KeyStep 37?*, red frame (the bond is lost; for a bonded Mac the Mac must forget the FM-1 too: the toast says *FORGET IT ON THE MAC TOO*) |
| ALGORITHM | the track, as everywhere (nothing here) |

The header shows `SCAN` while scanning (blinking dot), `SEARCH` while searching, and the toast says *CONNECTED
KeyStep 37* / *LOST KeyStep 37* / *NEEDS PAIRING* (passive status stays in the header).

### 2.3 SLOOP's HOME-held menu (this branch and SLOOP-UI builds)

Its grammar (`ui_menu.c`): SELECT = the screen before / after, KNOB 1..4 = the rows, PRESETS = the cursor, **OCT+ =
OK** (step a setting or open an action), **OCT- = close / back**. The BLUETOOTH screen (SYSTEM's last) gets three
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
|K3 AUTO              LAST    
|                             
|                             
|                             
SELECT SECTION  KNOB SETS     
OCT+ OK   OCT- CLOSE          
                              
```

DEVICES (a sub-screen, `ui.menu = 3`, as ABOUT is 2):

```
DEVICES             SCANNING  
------------------------------
  NONE (VISIBLE)              
> KeyStep 37       LAST  ▮▮▮  
  MacBook Pro      BOND   --  
  FM-1 A1B2              ▮▮   
  nanoKEY Studio         ▮▮   
  WIDI Master            ▮    
                              
                              
------------------------------
CONNECTED  KeyStep 37         
PRESETS MOVE   OCT+ CONNECT   
K4 FORGET      OCT- BACK      
                              
```

- PRESETS (or SELECT) moves the cursor; OCT+ connects / disconnects / picks NONE; OCT- back to the menu.
- **FORGET**: KNOB 4 turned on a remembered row arms it (*OCT+ FORGETS KeyStep 37?* in amber on the status line),
  OCT+ within 3 s does it, anything else lets it go (SLOOP's menu has no modal; HOME held is the menu itself, so the
  Optimist UI's HOME-held clear does not exist here).

### 2.4 How the existing BLUETOOTH ON / OFF fits

- ON / OFF keeps its meaning and its default (**OFF**, the 2026-10-08 ruling), now at settings-word **bit 24** on
  main. OFF = radio off, nothing on the air, a link ended with no stuck note (today's code, unchanged).
- ON = the radio does what §1.4 says. The first ON of a boot still starts the radio then (`ble_radio_start`), with
  the boot guard (`ble_boot_radio`) as today.
- Opening DEVICES while OFF switches nothing on: the list shows the remembered devices and *BLUETOOTH IS OFF*;
  YES on a device switches ON and connects (one gesture, §6 Q8 asks whether that is wanted).

## 3. Persistence

### 3.1 The known-device store

Where: **appended to the settings record** (`persist_t` in `storage/project.c`, `OBJ_SETTINGS`, A / B at
0xFC000 / 0xFD000, CRC-checked, `st_save` / `st_load`), as `ble_addr` and `ble_rf` are, **replacing `ble_bond[28]`**
(its one bond becomes entry 0 when an older record is read). Not its own object: two more sectors would have to come
out of the flash allow-list, and the record has ~3.4 KB of payload room. **Never 0xE7000-0xE9FFF** (FL_NEVER, the
SDK's VM and BTIF). A build without BLE never sees the field (appended last: it reads the rest as its own, as today).

```
struct ble_dev_store {                  /* 8 + 4 x 68 = 280 B */
    uint8_t mark;                       /* 0xB6: valid; else "nothing remembered", AUTO off */
    uint8_t ver;                        /* 1 */
    uint8_t want;                       /* the picked entry: 0..3, 0xFF = NONE (stay visible) */
    uint8_t flags;                      /* bit 0 AUTO (reconnect the last device), bit 1 directed adv for a bonded central */
    uint8_t seq;                        /* the use counter: the next entry used gets seq + 1 (no clock on the FM-1) */
    uint8_t rsv[3];
    struct ble_dev {                    /* 68 B */
        uint8_t addr[6];                /* identity address (public / random static), least significant first */
        uint8_t info;                   /* bit 0 addr random, bit 1 role C (we connected to it) / 0 role P (it to us),
                                         * bit 2 bonded (ltk valid), bit 3 irk valid, bits 4-5 KIND (MIDI, FM-1, HOST),
                                         * bit 7 entry used */
        uint8_t used;                   /* seq when last connected: the largest is LAST */
        char name[16];                  /* from the AD / scan response or GAP Device Name, NUL-padded */
        uint8_t ltk[16];                /* the bond: the key that encrypts a reconnection (ours handed out as
        uint8_t rand[8];                 * responder, the peer's taken as initiator), with its EDIV / Rand */
        uint8_t ediv[2];
        uint8_t irk[16];                /* the peer's IRK: recognise / find it behind an RPA */
    } dev[4];
};
```

- **4 entries** (Q1): the last device and three more; a fifth device replaces the least recently used unbonded
  one, else the least recently used. 8 entries would be 552 B.
- **Fields**: address + type; name; role (who connected to whom: tells reconnect how); KIND; bond (LTK, EDIV, Rand),
  IRK; last use as a counter (there is no clock; the order is all the list needs).
- **RAM**: the live table (280 B) plus `persist_saved`'s copy (280 B, the change test) = **560 B** (today's bond:
  2 x 28). Down to ~300 B if the saved copy keeps a CRC of the table instead [E].
- **Writes**: a settings save erases and writes a 4 KB sector and stops the audio, so the table is saved like every
  setting changed while playing: **once the FM-1 is quiet** (`settings_later`, as the bond and the address are). A
  connection only bumps `used` / `want`; a lost save costs only the order. The scan list itself is never saved.
- **The settings word**: BLUETOOTH ON stays **bit 24** (main). AUTO and the picked device live in the store's
  header, not in the word: no new bit (21-24 are taken; bit 25 stays free, and a word written by the earlier Optimist
  UI build that used 24-25 for its HOLD could read as ON with AUTO: the header's `mark` avoids that for AUTO).

### 3.2 What is kept, per role

| | Role P (it connected to us: Mac, phone, a central FM-1) | Role C (we connected to it: controller, advertising Mac, FM-1) |
| --- | --- | --- |
| address | its InitA, or its identity address after a bond (an RPA is useless next time) | its AdvA (controllers and FM-1s: public or static [I]); its identity address after a bond |
| name | GAP Device Name read from it (one ATT request) | AD type 0x09 / 0x08 from ADV_IND or SCAN_RSP |
| bond | our LTK / EDIV / Rand (responder, today's `ble_smp.c`) | its LTK / EDIV / Rand (initiator, §4.3) |
| IRK | its IRK, if it distributes one (Macs and phones do [I]) | its IRK, if it does |

### 3.3 Auto-reconnect

When: at boot when ON was saved, when BLUETOOTH is switched ON, and when a link to the wanted device is lost
(supervision timeout, the peer powered off). Only with AUTO = LAST (Q3: the default).

- **As central (want = a role C entry)**: initiate to its address. The engine's initiating state with the target
  address (TARGETADR / WHITELIST0, FILTERCNTL bit 0) connects on the first ADV_IND from it with no scan reports for
  software to handle [HW? §21: whether the engine sends CONNECT_IND in T_IFS by itself; software cannot]. If the
  entry has an IRK (the peer advertises from an RPA), the engine cannot match it (no resolving list known), so the
  FM-1 scans instead, resolves each AdvA with `ah(IRK, prand)` in the main loop and initiates to the RPA that
  matches. Time-sliced with advertising (§1.4.1); gives up to the slow retry after 30 s (Q2).
- **As peripheral (want = a role P entry, or NONE)**: advertise undirected as today (the same static address and
  name, so a Mac that knows the FM-1 can reconnect). If P1's hardware test shows a Mac or iPhone reconnects faster
  to it: **ADV_DIRECT_IND** (high duty, 1.28 s max, Core Vol 6 Part B 4.4.2.4.3) to the bonded central's identity
  address first, then undirected [HW? §21 for the engine's directed mode, FORMAT / TARGETADR]. A whitelist is **not**
  used to keep others out: centrals with RPAs would not match it, and anyone may connect while VISIBLE (as today).
- Recognising who connected (role P): InitA equal to an entry's address, or resolved by an entry's IRK, makes it
  that entry (its `used` bumped, its name shown at once, its bond used).

## 4. Stack work by layer, sized

Today [M]: stack without encryption 8.2 KB flash / 1.8 KB RAM, driver 3.5 KB / 2.6 KB, user-default + BLE +13.1 KB /
+5.5 KB; SMP + LL encryption opt-in +2.7 KB / +0.4 KB (`BLE-STACK.md` §5.1, §8, §11.6). Estimates below are
[E] from the sizes of the comparable existing code (`ble_ll.c` 3.2 KB for the peripheral LL, `ble_att.c` 2.0 KB for
the server, `ble_smp.c` responder ~1.1 KB).

### 4.1 Link layer: central

| Part | What | Flash | RAM |
| --- | --- | --- | --- |
| Scanner (LL + driver) | link in state 1 (HW §2.5), scan interval / window, active scan (SCAN_REQ by the engine? [HW? §21]), each ADV_IND / SCAN_RSP → a 4-entry ring of raw reports (addr, type, RSSI word, AD ≤ 31 B) in the RX IRQ; the main loop parses and merges (duplicates, ageing) | 1.2-1.8 KB | ring 160 B |
| AD parser + scan list | flags, 128-bit UUID list (0x06 / 0x07: the MIDI UUID), names (0x08 / 0x09), 8 entries (addr 7, name 16, RSSI, seen, kind) | 0.3-0.4 KB | 220 B |
| Initiator | CONNECT_IND built by us: AA by the spec's rules (`ble_prim.c` has the checks; a generator ~100 B), CRC init from `ble_hw_rand`, WinSize 2, WinOffset 0, **our parameters: interval 6-9 (7.5-11.25 ms), latency 0, timeout 100 (1 s)**, all 37 channels, Hop 5..16 random, our SCA; link to state 3 with the target, then state 6 (column 9 bit 0 marks the end of initiating, HW §2.3) | 0.5-0.7 KB | 40 B |
| Master connection | the LL's procedures from the central's side: we start version / feature / length exchange; we **answer** LL_CONNECTION_PARAM_REQ and L2CAP Connection Parameter Update Requests with an LL_CONNECTION_UPDATE_IND at an instant (+6 events); LL_ENC_REQ (master's SKD / IV) for a bonded or pairing link; the rest (ack, queues, supervision, terminate) is shared with the peripheral code. **Channel-map updates as master: none at first** (all 37; no channel-quality data to act on) | 1.0-1.5 KB | 30 B |
| Driver, master side | state 3 / 6 set-up in the control block, the first anchor 1.25 ms + WinOffset after our CONNECT_IND, master TX first in each event (the engine's timing [HW? §21]), widening of the peer's SCA (HW §7 formula) | 0.6-1.0 KB | 20 B |
| **LL central total** | | **3.6-5.4 KB** | **~0.5 KB** |

### 4.2 GATT client

| Part | What | Flash | RAM |
| --- | --- | --- | --- |
| ATT client core | one request outstanding, the 30 s ATT timeout, Error Response handling; MTU exchange as client (247) | 0.3 KB | 16 B |
| Device name | Read By Type 0x2A00 on 1..FFFF (role P too: the Mac's name, P1) | 0.1 KB | — |
| BLE-MIDI discovery | Find By Type Value (primary service = MIDI UUID) → range; Read By Type 0x2803 in it → the MIDI I/O characteristic's value handle (128-bit UUID match); Find Information after it → its CCCD | 0.5-0.7 KB | 8 B handles |
| Subscribe and data | Write Request CCCD = 0x0001; Handle Value Notifications on the value handle → `ble_midi` decoder (as writes are today); MIDI out as **Write Without Response** from the same encoder and ring as notifications (`att_midi_out` with a `client` switch) | 0.3-0.4 KB | — |
| **GATT client total** | the server stays as it is (a central FM-1's peer may discover us too) | **1.2-1.5 KB** | **~30 B** |

### 4.3 SMP

| Part | What | Flash | RAM |
| --- | --- | --- | --- |
| Responder (exists, opt-in) | legacy Just Works, bonding; **change**: keep the central's IRK + identity address (Identity Information / Identity Address Information) into the entry instead of dropping them; hand out our Identity Address Information (our static address) | +0.15 KB | — |
| Initiator | Pairing Request (NoInputNoOutput, bonding, keys: their LTK + IRK), Mconfirm / Mrand, check Sconfirm with `c1`, STK `s1` (all exist), the LL encryption started by us (master side of 4.1), take their LTK / EDIV / Rand / IRK / address; the SMP 30 s timeout | 1.0-1.3 KB | 60 B (shares the responder's state) |
| When | responder: a Mac / phone pairing (P1). Initiator: only for peripherals that answer Insufficient Authentication, or to keep an RPA peer's IRK; **not needed for FM-1 to FM-1** (static addresses, open link) | | |

LE Secure Connections (P-256) stays out: ~4-6 KB and a big-number multiply on a 240 MHz core between audio
blocks [E]; legacy Just Works is what the stack already does and what a passkey-less instrument can offer anyway.

### 4.4 RPA resolution

`ah(k, r) = e(k, 0^104 || r) mod 2^24` (Core Vol 3 Part H 2.2.2): one AES-128 block with `ble_aes128` (there with
`BLE_LL_ENC`). An RPA (top bits 01) resolves against each entry with an IRK: 4 blocks at most, in the main loop
(never in the IRQ). Also generating an RPA for directed advertising to a central that wants one (`ah` with its IRK).
**~0.1 KB flash** (+0.9 KB for AES in a build that has no encryption otherwise).

### 4.5 The MIDI router

**One link at a time** (the simplest, and what RAM and the radio's one-link time-slicing want): `midi_ble.c` keeps
its two rings; the stack sends the out ring as notifications (role P) or as Write Without Response (role C); in is
the same decoder either way. The note release on link loss (`ble_held`) is unchanged. A second simultaneous link (a
controller in + a Mac out) would need a second LL context (~0.7 KB RAM: the TX ring, the control queue), a second
engine link [HW? §21], routing rules and UI: **not in this design** (Q6). **~0.1-0.2 KB.**

### 4.6 UI and glue

| Part | Flash | RAM |
| --- | --- | --- |
| Device store: load / save / migrate the old bond / LRU / FORGET | 0.4-0.6 KB | 560 B (§3.1) |
| Reconnect state machine (search, time-slice, give up, peripheral match) | 0.4-0.7 KB | 16 B |
| Optimist UI: BLUETOOTH row + DEVICES list screen (rows, cells, YES, HOME-held FORGET, the modal) | 0.9-1.3 KB | 16 B |
| SLOOP menu: three rows + DEVICES sub-screen | 0.8-1.2 KB | 8 B |

### 4.7 Totals and builder options

| Builder item | Contains | Flash [E] | RAM [E] |
| --- | --- | --- | --- |
| `BLE` (exists) | peripheral, today | (13.1 KB) | (5.5 KB) |
| `BLE_BOND` (new; makes `OPTIMIST_BLE_SMP=1` a builder option) | SMP responder + LL encryption + AES (2.7 KB measured), the device store, the name read, IRK keeping, RPA resolution, the last-device UI | 4.3-5.0 KB | 0.95 KB |
| `BLE_CENTRAL` (new, needs `BLE`) | scanner, list, initiator, master LL and driver side, GATT client, reconnect, DEVICES list | 6.0-8.5 KB | 0.55 KB |
| `BLE_PAIR_OUT` (new, needs both) | SMP initiator | 1.0-1.3 KB | 0.06 KB |
| `BLE_RSSI` (later, after U9) | the 64-entry gain table, dBm bars | 0.1 KB | — |
| **all** | | **11.4-14.9 KB** | **~1.6 KB** |

Without `BLE_BOND` a `BLE_CENTRAL` build still remembers devices (the store without keys, same layout: the key
fields stay zero) and reconnects to peripherals with fixed addresses, which is what controllers and FM-1s use [I].

## 5. Phases

Each phase ends with host tests (`tests/run_tests.sh`, ASan / UBSan), the emulator (fm1-emulator-ble, `diagnose`),
and a hardware session the user runs (agents never touch the FM-1, the serial port or MIDI).

### P1: bonding and "last device" as peripheral

- **Build**: `BLE_BOND`; the store (§3.1) replacing `ble_bond`; keep the IRK / identity; GATT client core + name read;
  SYSTEM > BLUETOOTH row with STATE showing `CONNECTED MacBook Pro`; DEVICES list with NONE + remembered entries only
  (no scan yet); FORGET. Pairing trigger: `BLE_MIDI_NEED_ENC` (Apple's 58.10 way) — or none, and the bond only when
  the Mac asks (Q5).
- **Host**: the store's migration (an old `ble_bond` record becomes entry 0), LRU, FORGET; a Mac-like central that
  pairs with an RPA and distributes IRK + identity, reconnects from a new RPA and is recognised; the name request.
- **Emulator**: the virtual central exists (`ble_central/gatt.rs`, its pairing); **add** to it: a GAP server answering
  Read By Type 0x2A00 with a name, an RPA InitA from a fixed IRK that changes per connection, Identity Information
  distribution. Checks: name shown in the menu screenshot, the second connection recognised, a reboot keeps the
  entry (`FM1_FLASH_DUMP` / `RESTORE`).
- **Hardware**: Mac (and an iPhone if at hand): pair, power-cycle the FM-1: does the Mac reconnect by itself? With
  `BLE_MIDI_NEED_ENC` and without. Measure the time to reconnect. Then the directed-advertising build (if §21 gives
  the engine's directed mode) the same way: keep it only if it helps.
- **Risks**: macOS / iOS may never auto-reconnect a BLE-MIDI peripheral whatever we do (then "last device" in role P
  is only the name and the bond, and the Mac's Audio MIDI Setup does the reconnect); pairing on hardware is untested
  (LL encryption in software, `isr_max_us` unmeasured with encryption on, §5.1).
- **Size**: +4.3-5.0 KB flash, +0.95 KB RAM.

### P2: the scan list (central scan only, names)

- **Build**: scanner, AD parser, scan list, DEVICES with found devices, signal bars relative (dim).
- **Host**: a fake `ble_hw` feeding ADV_IND / SCAN_RSP reports (BLE-MIDI and not, names in ADV or SCAN_RSP,
  duplicates, a device that goes away).
- **Emulator**: **add** the engine's state 1 (scan window / interval on 37 / 38 / 39, an ADV report into an RX buffer +
  RX IRQ, RSSI words, SCAN_REQ sent by the engine on an ADV_IND or not: a config flag until §21 says) and several
  **virtual advertisers** on the air (`ble_air`): BLE-MIDI controllers with names in SCAN_RSP, a non-MIDI beacon, an
  RPA advertiser. Checks: the list in a screenshot, the beacon filtered out, the IRQ rate and the audio handler's
  time while scanning (`FM1_BLE_ISR=1`).
- **Hardware**: DEVICES opened next to a controller, nRF Connect on a phone advertising the MIDI UUID, a Mac with
  *Advertise* on: names right, the list stable, no audio click while scanning (RX IRQ per report).
- **Risks**: §21 (scan state, SCAN_REQ by the engine); IRQ load in a crowded room (hundreds of reports a second: scan
  only while DEVICES is open or searching, drop non-MIDI reports early in the IRQ); RSSI meaningless until U9.
- **Size**: +1.8-2.6 KB flash, +0.4 KB RAM.

### P3: connect as central to a BLE-MIDI controller, MIDI in

- **Build**: initiator, master LL, driver state 3 / 6, GATT client discovery + CCCD + notifications, out as Write
  Without Response; the router's role switch; NEEDS PAIRING when the CCCD write fails with 0x05 (P3b: SMP initiator).
- **Host**: `ble_stack_test.c` gets a **simulated peripheral** (the mirror of today's simulated central): CONNECT_IND
  fields valid, our procedures, its LL_CONNECTION_PARAM_REQ answered with an update at an instant, discovery against a
  controller-shaped database (MIDI service not first, extra services), notifications decoded, writes encoded, link
  loss → notes released.
- **Emulator**: **add** the engine's state 3 (on an ADV_IND from the target, the CONNECT_IND from a TX buffer sent
  T_IFS later, column 9 bit 0) and state 6 (master: TX at the anchor, then RX; SN / NESN the same machinery mirrored),
  and a **virtual BLE-MIDI peripheral** (`ble_peripheral`: advertiser, slave LL, GATT server with the MIDI service,
  a scripted keyboard sending notes, optionally "needs encryption"). Checks: a note from the virtual keyboard plays the
  synth (the non-silent frame count, as `ble_emu_test.py` does for the central), the FM-1's notes reach it, a
  connection update at its instant, the peripheral vanishing → notes off, back to the previous state.
- **Hardware**: one or two real controllers (Q10: which ones the user has). Latency by ear, a long session for drops.
- **Risks**: the master timing is the engine's and unmeasured [HW? §21]; our SCA claim and the PLL accuracy; some
  controllers want encryption or a long connection interval; Wi-Fi / BT coexistence of the shared radio (RFPRIO,
  U11).
- **Size**: +3.9-5.5 KB flash, +0.2 KB RAM (+1.0-1.3 KB for P3b).

### P4: auto-reconnect

- **Build**: AUTO; the search state machine with time-slicing (§1.4.1); initiating with the target address; RPA peers
  by scan + resolve; the peripheral-side match (P1) into the same `want` logic; give up / slow retry.
- **Host**: the state machine on a fake clock: found at once, found after 20 s, never (slow retry), a Mac connecting
  during the search (it wins), BLUETOOTH OFF during a search, a link lost and found again.
- **Emulator**: boot with a flash dump that has a role C entry and AUTO: the virtual peripheral appears after 3 s,
  the FM-1 connects with no key pressed; the peripheral disappears and comes back; a boot with the peer absent keeps
  the FM-1 advertising (a virtual central can still connect).
- **Hardware**: power-cycle each side in turn, both orders; count the seconds to MIDI.
- **Risks**: battery-powered controllers that advertise slowly or only after a key press (long searches); the radio
  time spent searching forever (Q2); a flash save on every connection (avoided: §3.1, only when quiet).
- **Size**: +0.4-0.7 KB flash, ~0 RAM.

### P5: FM-1 to FM-1

- **Build**: a name that tells FM-1s apart (Q4), KIND = FM-1 from it; the "both pick each other" rule (§1.3); the arp
  loop guard if P5's test shows the loop; BLE clock if Q9 says yes (another phase of its own).
- **Host**: two stacks linked through two fake drivers in one process (the simulated central and peripheral become
  real stacks): connect, MIDI both ways, both initiating at once.
- **Emulator**: **add** two machines on one virtual air in one process (`diagnose` with `FM1_BLE_PEER=<fwsc>`): both
  24 MHz time bases stepped in lock-step, the air shared. Before that exists, the virtual peripheral of P3 configured
  with the FM-1's own advertising and GATT layout stands in.
- **Hardware**: needs a second FM-1 (Q10). Without one: a Mac app or a phone as the peer covers each role separately.
- **Risks**: the two-machine emulator is the biggest emulator task here; the arp ping-pong; both FM-1s searching each
  other while one also advertises.
- **Size**: +0.2-0.4 KB.

### Order and effort

P1 and P2 are independent (P1 is peripheral-only and useful alone; P2 needs §21 for the scan state). P3 needs P2; P4
needs P1 + P3; P5 needs P3 (P4 for reconnect). Emulator work is on the critical path from P2 on: the engine's states
1 / 3 / 6 and a virtual peripheral are each about the size of today's advertising / connection model
(`advertising.rs` 303 lines, `connection.rs` 460, `ble_central/` ~1,700) [M: line counts].

## 6. Open questions for the user

1. **How many remembered devices?** 1 (only the last) · **4 (recommended: 280 B)** · 8 (552 B).
2. **Searching for the last device when it is not there**: try 30 s then retry slowly every 10 s while ON
   (recommended) · keep trying all the time · try 30 s then give up until the next ON / boot.
3. **Does the last device auto-connect at boot by default?** Yes, AUTO = LAST by default (recommended; BLUETOOTH itself
   stays OFF by default) · No, AUTO off by default, the user turns it on.
4. **The FM-1's advertised name**: keep `FM-1_BLE` (stock's; Macs that know it see the same) · **`FM-1 XXXX`, the last
   four hex digits of its address (recommended: two FM-1s are told apart)** · a name typed on the NAME screen
   (+ ~0.2 KB, Optimist UI only).
5. **Pairing as peripheral**: only when the central asks (today, no prompt on the Mac) · **ask with Insufficient
   Authentication, the way Apple's guidelines say (recommended for P1's test: a bond gives the IRK and the reconnect)**
   · never (no bonds; "last device" by address only).
6. **One link at a time** (recommended) · two links (a controller in and a Mac out at once: more RAM, §4.5, and
   hardware unknowns) later.
7. **Signal bars before RSSI is calibrated (U9)**: show relative bars dim (recommended) · hide them until U9.
8. **YES on a device while BLUETOOTH is OFF**: switch ON and connect (recommended) · do nothing, ask to switch ON first.
9. **Clock and transport between FM-1s over BLE** (today ignored from BLE): a later phase (recommended) · in P5 · never.
10. **Test devices**: which BLE-MIDI controllers do you have, and is a second FM-1 available for P5?
11. **Scan list filter**: only devices that advertise BLE-MIDI (recommended: phones, headphones and beacons stay out) ·
    everything with a name.
12. **Builder split**: three items `BLE_BOND`, `BLE_CENTRAL`, `BLE_PAIR_OUT` (recommended) · one item `BLE_DEVICES`
    with everything.

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

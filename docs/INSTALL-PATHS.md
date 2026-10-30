# Installing over any firmware

The FM-1 installers (`make flash` / `python tools/optimist.py flash`, the builder's `f`, `tools/fm1_install.py`) never
send anyone back to M-VAVE's V15 to switch firmware. For each install they find out what runs on the FM-1, what the
package is and what the build says about the bootloader, and take one of two ways:

- **the running firmware's update loader**, the normal way, wherever it takes the package. Every firmware that ever ran
  on an FM-1 has one, so this stays retro-compatible: nothing new is needed on the computer or the FM-1;
- **the chip's USB download mode, through M-Flasher**, where that loader would refuse the package (Optimist's own
  firmware and an fm1-base package, or the other way's m-bootloader installer), where nothing answers on USB-MIDI (a
  firmware that does not start), and when the build's `BOOTLOADER` is m-bootloader.

The choice is `tools/fm1_route.py`; the download-mode steps are `tools/fm1_anyflash.py`, which runs M-Flasher's command
line (`tools/mflasher.py`). Every table below is made by that code and checked against this page
(`tests/anyflash_test.py`).

## M-Flasher and m-bootloader

[M-Flasher](https://github.com/jvitkauskas/m-flasher) (Julius Vitkauskas, MIT) backs up, restores and writes the flash
through the chip's download mode, writes only the 4 KiB sectors that change and verifies each (its `docs/safety.md`).
[m-bootloader](https://github.com/jvitkauskas/m-bootloader) (the same author, MIT) is an open bootloader: REC held while
switching on enters download mode, PLAY a recovery menu, even when a firmware does not start.

Both are git submodules of Optimist, pinned: `third_party/m-flasher` at v0.7.0 (`0c5d50f`), `third_party/m-bootloader` at
`64b74d0` (v1.2, `22e2559`, and two documentation commits). They sit in Optimist, not in fm1-base: the callers are
Optimist's command-line installers; fm1-base's installer is the web page, which reaches the FM-1 over Web MIDI only and
cannot use M-Flasher. Optimist's code calls M-Flasher's command line; none of its protocol is copied here.

**Getting M-Flasher.** `python tools/fm1_anyflash.py setup` fetches M-Flasher's v0.7.0 release for this system (the
macOS disk image, the Linux tarball for x86-64 or ARM64, the Windows zip) into `~/.jieli/m-flasher-0.7.0/`, checked by
its SHA-256 as `tools/mflasher.py` pins it, as setup does with the pi32v2-llvm toolchain. `--from-source` builds the
submodule with cargo instead. The installers also take `M_FLASHER=/path/to/m-flasher`, the app in `/Applications`, or
`m-flasher` on the PATH. Why the release rather than a build: it is the build its author tested on each system
(macOS's root helper, Windows' SCSI pass-through, Linux's udev rule come with it), and it needs no Rust toolchain.

**The bundled m-bootloader.** `web/fm1base/m-bootloader/fm1-uboot-1.2.boot` and `.fwsc` are m-bootloader v1.2's release
files: `tools/mflasher.py` pins their SHA-256 (the release's `SHA256SUMS`) and the test checks them and M-Flasher's own
bundled build (`third_party/m-flasher/data/bootloader-fm1.boot`) against it. The release files are not in the
submodule's source; the pin is the check.

On macOS M-Flasher asks for an administrator's password (only root may take a USB disk from macOS's driver); on Linux
its udev rule and `sg` module (its README); on Windows an administrator prompt.

## The install paths

`fm1_install.py --dry-run --running FM-1_700 PACKAGE.fwsc` prints the way for one package without asking the FM-1;
`python tools/fm1_anyflash.py paths` prints this matrix. With m-bootloader: the build's `BOOTLOADER` is m-bootloader.

| running \ package | M-VAVE's V15, unchanged (its SHA-256) | an Optimist build of its own (FM-1_7NN) | a Felucca / SLOOP package (FM-1_9NN) | an fm1-base package (Optimist's release FM-1_79.., any FM-1_0 + 6 digits) | m-bootloader's installer (FM-1_099) |
|---|---|---|---|---|---|
| M-VAVE's stock firmware (FM-1_0NN: V15 015, V16 016) | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | download mode |
| M-VAVE's FM1 Open firmware (FM1_OFFICIAL_OPEN, FM1_COMMUNITY_N) | update loader; with m-bootloader: download mode | download mode | download mode | download mode | download mode |
| Felucca (FM-1_9NN) | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | download mode | download mode |
| SLOOP (FM-1_9NN, its port named SLOOP) | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | download mode | download mode |
| Optimist's own firmware (FM-1_700, FM-1_720 .. 769) | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | download mode | download mode |
| FM-1_000: Optimist in its USB rescue, or an fm1-base development build | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | download mode | download mode |
| an fm1-base firmware (FM-1_0 + 6 digits, Optimist's release FM-1_79..) | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | download mode |
| Baud Girl (FM-1_020 .. 098) | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | update loader; with m-bootloader: download mode | download mode |
| nothing answers on USB-MIDI (off, no data cable, a firmware that does not start, already in download mode) | download mode | download mode | download mode | download mode | download mode |

What each cell rests on:

- **Optimist's own updater** (`firmware/src/system/ota.c`) takes a flash.bin of 0x93000 or 0xD0000 B with the Felucca
  loader or M-VAVE's official one, and refuses the rest before anything is erased (-15, -7; 2026-10-29: an fm1-base SD
  card mode package stopped there with -16). Its official-loader branch checks that the package's head equals the
  FM-1's (-5): the return to V15 is README.md's "Getting back"; if this updater refuses it, `--route download` takes it.
  It refuses m-bootloader's installer too (no flash.bin: -3).
- **fm1-base's updater** (felucca-bsp `chip/ota.c`) takes any package whose writer is well made and whose head is
  bootable, m-bootloader's installer included; the web installer installs m-bootloader that way after the firmware.
- **The stock updater** takes any package but the identity it runs (V15 over V15: download mode).
- **Felucca's and SLOOP's** updaters are not read here: the cells assume Optimist's rules (which came from Felucca 1.6.1)
  for fm1-base packages, the loader for the rest. **Baud Girl's** is assumed stock-like. If a loader refuses,
  nothing is written; `--route download` takes the other way.
- m-bootloader's installer always goes through download mode here (`fm1_anyflash.py bootloader`): a full backup first,
  which no update loader makes.

## Entering download mode

The installer sends the soft key, SysEx `F0 22 24 35 7D F7`, over the USB-MIDI port it found the FM-1 on, when the
running firmware is known to answer it (m-bootloader's SPEC 4.2); else it names the keys and waits 20 s for M-Flasher to
see the FM-1 (`m-flasher find`).

| running | soft key over USB-MIDI | keys |
|---|---|---|
| M-VAVE's stock firmware (FM-1_0NN: V15 015, V16 016) | yes (seen on 015, M-Flasher docs) | none of its own: the soft key, or m-bootloader's REC at power-on |
| Baud Girl (FM-1_020 .. 098) | not known | not known: m-bootloader's REC at power-on |
| M-VAVE's FM1 Open firmware (FM1_OFFICIAL_OPEN, FM1_COMMUNITY_N) | not known | not known: m-bootloader's REC at power-on |
| Felucca (FM-1_9NN) | yes (M-Flasher docs) | OCT- + OCT+ held 5 s |
| SLOOP (FM-1_9NN, its port named SLOOP) | not known | OCT- + OCT+ held 5 s |
| Optimist's own firmware (FM-1_700, FM-1_720 .. 769) | yes (io/usb/usb.c) | OCT- + OCT+ held 5 s while stopped; or held while switching on, 3 s |
| FM-1_000: Optimist in its USB rescue, or an fm1-base development build | yes (both) | OCT- + OCT+ held while switching on, 3 s (Optimist); REC while switching on (fm1-base) |
| an fm1-base firmware (FM-1_0 + 6 digits, Optimist's release FM-1_79..) | yes (felucca-bsp chip/usb.c) | REC held while switching on |
| another firmware (FM-1_5NN Lunar, FM-1_701 ...) | not known | not known: m-bootloader's REC at power-on |
| nothing answers on USB-MIDI (off, no data cable, a firmware that does not start, already in download mode) | no | m-bootloader: REC while switching on; Optimist: OCT- + OCT+ while switching on, 3 s; else the USB_KEY dongle |
| m-bootloader installed (any firmware) | - | hold REC while switching on (m-bootloader's download mode; PLAY: its menu, DOWNLOAD MODE) |

"yes" for Optimist and fm1-base is read from their sources, not tried on an FM-1 by this work. In download mode the
screen stays as it was; switching the FM-1 off and on leaves it.

## The download-mode steps

1. Download mode (the soft key or the keys above).
2. `m-flasher info`: the model must read **FM-1** exactly (not an SMK-37, not "probably"), else nothing is written.
3. `m-flasher backup DIR`: the whole flash, its path printed (default `~/Documents/M-Flasher/backups`, `--backup-dir`).
4. With the build's m-bootloader, and none on the FM-1: `m-flasher bootloader` (the boot area 0x00000-0x03FFF only,
   the unit's configuration in it kept), after a yes; then a new backup.
5. The firmware: this installer plans the sectors (below), asks M-Flasher for its own plan of the same command (its
   question answered "no": nothing written) and stops if the two differ; then a typed `yes`, then M-Flasher writes with
   `--yes --restart`. M-Flasher verifies every run of sectors by the chip's CRC32 and stops on a mismatch
   (its `docs/safety.md`); the backup's path is in its message.
6. The installer waits for the FM-1 on USB-MIDI and prints what it runs.

Never: `--wipe-settings` (it erases what a package declares as VM: in Optimist's and fm1-base's packages that is
[app end, 0xE9000), all user data and the SDK's VM with the radio calibration), `--with-bootloader` (the bootloader
stays as it is unless the build says m-bootloader), `--board` (the model is M-Flasher's to read). `--dry-run` sends
nothing (no soft key, no M-Flasher) and prints the commands; with `--flash-image BACKUP.bin` the exact sectors.

The other commands of `tools/fm1_anyflash.py` (`backup`, `restore BACKUP.bin`, `bootloader [--stock PACKAGE.fwsc]`,
`restart`) enter download mode through M-Flasher's own `download-mode`, back up first, and let M-Flasher show its plan
and ask for `yes` itself.

**The SD card (to come).** m-bootloader is to flash a `.fwsc` from the SD card at power-on. That is not released;
`fm1_anyflash.sd_card_route()` is the hook (it refuses for now), so the route chooser can grow a third way without
touching the other two.

## The build's BOOTLOADER

The builder item `BOOTLOADER` (System, bit 443, `keep` by default, or `m-bootloader`) is a flash-step setting: it
changes nothing in the firmware, its hash or its costs (`no_image`). The build writes it beside the package:
`optimist-X.Y.install.json` (`tools/install_meta.py`: the package's name and SHA-256, `"bootloader"`, the configuration's
name and hash), from Optimist's own build (`tools/build.py`) and the release's (`tools/fm1base.py`).

Why a sidecar and not the package: the `.fwsc` is the image the update loaders read (their CRCs, the header's size,
M-Flasher's parse); a byte added to it is a byte every loader sees. The sidecar changes no package, and it names the
package's SHA-256, so one left beside another package is ignored. The web installer reads the value from a cloud
build's `.config`, which the cloud shelf keeps beside each package (`BOOTLOADER=1`).

Every installer shows it before it writes:

- `bootloader: m-bootloader (from the build): installed if the FM-1 does not have it yet, after a full backup and your yes`
- `bootloader: keep (from the build): not touched`
- `bootloader: keep (no build setting with this package): not touched`
- `bootloader: keep (--keep-bootloader for this install; the build says m-bootloader)`

The override goes one way only: `--keep-bootloader` (`make flash KEEP_BOOTLOADER=1`, the builder's "Flash, but keep the
bootloader this time", the web installer's checkbox) turns the build's m-bootloader into keep for one install, for a
package installed on someone else's FM-1 or an m-bootloader that misbehaves on a unit. Nothing installs m-bootloader
when the build says keep, or says nothing. `python tools/fm1_anyflash.py bootloader` installs it on its own, asked.

## Layouts

| layout | flash.bin | user data |
|---|---|---|
| Optimist's own, MEMORY normal (`own-normal`) | [0x4000, 0x93000) | 0x95000-0xDFFFF: FM6 voices, sections / projects, autosave A, USR1-3, snapshots, kits, presets |
| Optimist's own, MEMORY more flash or SD card mode (LAYOUT 1) (`own-big`) | [0x4000, 0xd0000) | 0xD0000-0xDFFFF: the squeezed store (one project, autosave A, FM6 voices), kits, presets; SD mode: the card |
| fm1-base (Optimist's release), the normal slot (`base-normal`) | [0x4000, 0x93000) | the platform's data area 0x97000-0xDFFFF (Optimist's store, OBJ_PANEL 0xD8000), XDATA0 0x93000-0x96FFF |
| fm1-base, app_end 0xCE000 (the release's SD card mode, 823,228 B) (`base-sd`) | [0x4000, 0xce000) | OBJ_PANEL 0xCE000 / 0xCF000, Optimist's store from 0xD0000, the rest on the card |

The builder's MEMORY gives Optimist's own firmware two flash layouts (normal; more flash and SD card mode share LAYOUT
1's 0xD0000 slot, the card holding the SD mode's data). fm1-base's `app_end` can be any 4 KiB boundary up to 0xE0000;
the release uses 0x93000 and 0xCE000. Kept in every layout and every switch, never written by an install (the code's
`KEEP`, asserted on every plan):

- 0x00000-0x03FFF the boot area (only the explicit bootloader step);
- 0xE0000-0xE4FFF the update staging, fm1-base's backup journal (0xE1000) and crash ring (0xE2000-0xE3FFF);
- 0xE5000-0xE6FFF Optimist's drum records of the projects, fm1-base's XDATA1 start;
- 0xE7000-0xE9FFF the SDK's system flash: VM with the settings and RF calibration (0xE8000), BTIF with the pairings;
- 0xEA000-0xFBFFF the SDK's USR area; 0xFC000-0xFEFFF the settings A/B, autosave B (XDATA2); 0xFF000- key_mac.

**What M-Flasher writes.** `m-flasher firmware` takes any `.fwsc` in M-VAVE's format (Optimist's and fm1-base's are)
and writes its flash.bin [0x4000, its end), the sectors that differ only; it checks the model and that the app
decrypts with the unit's chip key. It knows no layout but the package's length: it does not clear what another layout
left, and its warning about different regions is the only sign of a switch. Its flash map names regions from the
package's table, so an Optimist or fm1-base package shows [app end, 0xE9000) as one SETTINGS region (the VM descriptor
our packages declare), BTIF and USR as the stock firmware's: right for the system areas, coarse for our data.

**A switch.** The update loaders clear the other layout's leftovers themselves (Optimist's `ldr_layout_switch`,
fm1-base's `LDR_SLOT_SWITCH` / `app_clear_end`). In download mode this installer does it around M-Flasher: from the
fresh backup it builds the whole planned flash (the package's area, the clear, everything else as it was), writes it
beside the backup as `BACKUP-install.bin` with the backup's `.json` (the unit's ID: M-Flasher restores a backup of the
same unit whole) and runs `m-flasher restore` on it. M-Flasher then writes exactly the planned sectors and verifies
them; its plan is compared with ours first. With no clear needed, `m-flasher firmware` is run as it is.

| from \ to | own-normal | own-big | base-normal | base-sd |
|---|---|---|---|---|
| own-normal | same layout: writes 0x4000-0x92fff | writes 0x4000-0xcffff; overwrites data 0x93000-0xcffff; clears 0xd0000-0xd9fff | writes 0x4000-0x92fff | writes 0x4000-0xcdfff; overwrites data 0x93000-0xcdfff; clears 0xce000-0xd9fff |
| own-big | writes 0x4000-0x92fff; clears 0x93000-0xcffff | same layout: writes 0x4000-0xcffff | writes 0x4000-0x92fff; clears 0x93000-0xcffff | writes 0x4000-0xcdfff; clears 0xce000-0xcffff |
| base-normal | writes 0x4000-0x92fff | writes 0x4000-0xcffff; overwrites data 0x93000-0xcffff; clears 0xd0000-0xd9fff | same layout: writes 0x4000-0x92fff | writes 0x4000-0xcdfff; overwrites data 0x93000-0xcdfff; clears 0xce000-0xd9fff |
| base-sd | writes 0x4000-0x92fff; clears 0x93000-0xcdfff | writes 0x4000-0xcffff; overwrites data 0xce000-0xcffff | writes 0x4000-0x92fff; clears 0x93000-0xcdfff | same layout: writes 0x4000-0xcdfff |

What is lost, kept or moved:

- **From normal to a bigger slot** the new code covers the normal layout's user data from 0x93000 (FM6 voices, the
  section log and projects at 0x97000-0x9FFFF, USR1-3 from 0xA0000, the snapshots) and the clear takes the rest up to
  0xDA000. Back the projects up first (the editor's Backup); the full backup holds them too, for a way back by
  `fm1_anyflash.py restore`. The kits (0xDA000) and presets (0xDC000) stay where both layouts keep them.
- **From a bigger slot to normal** the old code above 0x93000 is cleared, so the new firmware finds an empty data area
  there; the bigger layout's store above the slot (kits, presets) is kept.
- **Between Optimist's own and fm1-base in the same slot** nothing is cleared: the data area is kept as it is. Whether
  the other firmware reads it (Optimist's store objects on fm1-base's data window) is not checked here; what it does not
  recognise it ignores or rewrites. A clean way back to a firmware is to restore a backup made while it ran.
- **Never moved:** nothing of the user's data is copied to another place by an install; moving a project between
  layouts is the editor's job (Backup, then Restore in the new firmware).

`m-flasher firmware`'s "write only what changes, verify" is enough within one layout. A switch needs the clear, which
M-Flasher cannot express on its own; the planned-image `restore` above does it with the flags it has. The requests below
would let it do this itself.

### Drafted requests for M-Flasher (not sent)

Drafts for Julius Vitkauskas, for when the user decides to send them:

1. **A layout switch in `firmware`.** "When the installed app's slot (its app_area_head) ends elsewhere than the
   package's flash.bin, clear what the other layout left: the old code's tail on a smaller slot, and [package end,
   CLEAR_TOP) on a bigger one, as the Felucca-family loaders do (`ldr_layout_switch`, fm1-base `LDR_SLOT_SWITCH`).
   `--clear START-END` (repeatable) would be enough: the caller knows its layouts."
2. **`--keep START-END`** for `firmware` and `restore`: refuse a plan that touches the range (0xE7000-0xE9FFF, the
   SDK's VM and BTIF, for Optimist and fm1-base), as an extra guard beside the factory ranges.
3. **A plan-only switch** (`--plan`, or `--dry-run`): print the plan and exit 0 without asking, so a caller need not
   answer the question "no" to read it, and the exit code says whether there was anything to write.
4. **A package manifest**: if a `.fwsc` carries a region list (a `layout.json` file in its UFW list, ignored by the
   loaders), `firmware` could read the data regions and the clears from it instead of the caller.

## Settings and pairings

M-Flasher 0.7.0's window ticks "Also erase settings and Bluetooth pairings" by default; its command line erases them only
with `--wipe-settings`, which this installer never passes: it erases what the package declares as VM and BTIF, and an
Optimist or fm1-base package declares [app end, 0xE9000) as VM: the projects, and the SDK's VM with the radio
calibration. **With M-Flasher's own window, for Optimist to Optimist, untick "Also erase settings and Bluetooth
pairings".**

This installer keeps or erases by the (running, package) pair (`fm1_route.settings_policy`), shows the choice before
anything is written, asks where the pair says so, and takes `--keep-settings` / `--wipe-settings` (its own flags, the
matrix's default otherwise; the builder offers both when it asks). Projects and sequences are kept either way.

| running -> package | default | asked |
|---|---|---|
| Optimist -> Optimist, the same build (Optimist's own -> own, the release -> the release; any version, any layout) | keep | no |
| Optimist's own <-> its release on fm1-base | keep | yes: the same code (storage.c) at the same place, not tried on an FM-1 |
| another firmware (stock V15, SLOOP, Felucca, Melodee, custom) <-> Optimist, or nothing answering | erase | yes |

An erase is this installer's own, in the planned flash (`fm1_layouts.WIPE`), the new firmware's settings only:

- **Optimist** (own or release): its settings record A/B, 0xFC000-0xFDFFF (with the BLE bonds and the Wi-Fi networks it
  keeps there); autosave B (0xFE000), the projects, BTIF and the system flash stay;
- **Felucca / SLOOP**: their globals, the same sectors;
- **stock**: BTIF 0xE9000 (its pairings); its settings share 0xE8000 with the radio calibration and are never erased.

With the update loader (the normal way) nothing is erased: the loader writes the firmware area only. Not checked by a
host test here: that every Optimist build reads every older Optimist record, and that the release reads the own build's
records and the other way; hence the question on that pair.

## Each platform

M-Flasher reaches download mode as a **USB mass-storage device** (the ROM's "WL82 UBOOT1.00" disk, 4C4A:8057) that takes
vendor SCSI commands over Bulk-Only Transport: memory writes and a jump to start its flash loader, then the loader's flash
commands (its `docs/development.md`, `src/jl.rs`, after jl-uboot-tool). Not HID, not MIDI. So:

| platform | download mode | what it does |
|---|---|---|
| a computer (macOS, Linux, Windows) | yes, through M-Flasher (macOS: an administrator's password; Linux: its udev rule; Windows: an administrator prompt) | everything on this page |
| a browser (the web installer) | no: WebUSB refuses mass-storage interfaces (a protected class), the device is not HID, Web MIDI only reaches a running firmware | the update loader; the soft key; the next step named |
| iOS / iPadOS (the Mirror app) | no: no raw USB to a mass-storage device, no IOKit, no libusb | the same as the browser |
| macOS (the Mirror app, sandboxed, from the App Store) | no: macOS's disk driver holds the device, and only root can take it (M-Flasher's root helper); a sandboxed app cannot | the same as the browser |

Where download mode is out of reach, the web installer and the Mirror app's Flash tab show the build's BOOTLOADER the
same way, with the KEEP-only override, install through the running firmware's loader as before, and when a package
needs download mode (or m-bootloader is to be installed: its backup first, which only M-Flasher makes) they say so:
"this needs M-Flasher on a computer", the command (`python tools/fm1_install.py PACKAGE.fwsc`, or M-Flasher's
Install firmware with "Also erase settings and Bluetooth pairings" unticked for Optimist to Optimist), the key combo for
the firmware that runs, and a button that sends the soft key, for when M-Flasher runs on the same computer. They never
write m-bootloader themselves.

## Hardware test plan

The user does every write; Claude writes nothing to an FM-1. Before each step: a USB data cable straight to the
computer (no hub), the FM-1 charged, `python tools/fm1_anyflash.py setup` done once, and the projects backed up from the
editor (Backup). Each step says how to come back if it goes wrong.

1. **Backup and restore only.**
   - User runs `python tools/fm1_anyflash.py backup` (Optimist running: M-Flasher sends the soft key; else OCT- + OCT+
     held while switching on, 3 s). Expect `backup: .../fm1-YYYYMMDD-HHMMSS.bin` and a `.json` beside it.
   - User runs it a second time and compares: `cmp first.bin second.bin` must say nothing (two reads agree).
   - User runs `python tools/fm1_anyflash.py restore FIRST.bin`: M-Flasher shows "Nothing to write" (the flash is
     the backup); answer anything but `yes`. Then `python tools/fm1_anyflash.py restart`.
   - Recover: nothing was written. If the FM-1 stays dark, switch it off and on.
2. **Optimist's own → fm1-base** (the release package, `build/optimist-X.Y.fwsc` from `make build`).
   - User runs `python tools/fm1_install.py --dry-run --running FM-1_700 PACKAGE.fwsc` and reads the route
     (download mode) and the commands; then with `--flash-image FIRST.bin` for the sectors.
   - User runs `python tools/fm1_install.py PACKAGE.fwsc`: yes to download mode, then the plan (ours and
     M-Flasher's agree), a typed `yes`. Expect "done: the FM-1 runs FM-1_79...".
   - Recover: `python tools/fm1_anyflash.py restore BACKUP.bin` (the backup it printed) puts Optimist's own back
     whole; if the FM-1 does not start, OCT- + OCT+ held while switching on, 3 s, reaches download mode (or the USB_KEY
     dongle), then the same restore.
3. **fm1-base → Optimist's own** (`make classic`, `build/optimist-*.fwsc` FM-1_700).
   - The same as 2: the dry run first (`--running FM-1_7900`: the update loader is expected, fm1-base's updater takes
     it), then the install. If fm1-base's updater refuses it, `--route download`.
   - Recover: restore the backup of step 2's end, as above. fm1-base's firmware: REC held while switching on enters
     download mode.
4. **m-bootloader** (the builder's BOOTLOADER = m-bootloader, or `python tools/fm1_anyflash.py bootloader`).
   - User runs `python tools/fm1_anyflash.py bootloader`: backup, then M-Flasher shows the boot-area sectors and asks.
   - Expect: switching off and on boots the firmware as before; REC held while switching on: download mode
     (`m-flasher find` shows it); PLAY held: m-bootloader's menu.
   - Then one install with the build's m-bootloader: the installer says "found: kept" and writes the firmware only.
   - Recover: `python tools/fm1_anyflash.py bootloader --stock web/stock/FM-1_015.fwsc` puts M-VAVE's bootloader back
     (the boot area only); or restore the backup made before it. If neither firmware nor bootloader starts, the USB_KEY
     dongle (m-bootloader SPEC 4.3) reaches download mode whatever the flash holds.

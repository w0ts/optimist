<!-- SPDX-License-Identifier: GPL-3.0-only -->
# fm1-base: adoption and backports

Optimist moving onto [charlesvestal/fm1-base](https://github.com/charlesvestal/fm1-base) (the MPL-2.0 base the other FM-1
firmwares share: CTL-1, Radio, LOOM-8 ...), what we take from it, and what we give back. The aim (the user, 2026-10-26): be
part of the family that builds the base everybody uses, and propose changes to it where it is wrong. Branch `fm1base-port`
(worktree `worktrees/fm1base-port`); fm1-base itself cloned in `worktrees/fm1-base` (and on gpufarm in
`~/optimist/worktrees/fm1-base`). Nothing pushed, nothing flashed.

## 1. Where the two stand (measured 2026-10-26)

| | fm1-base (934e40d) | Optimist (main b5eb998a) |
| --- | --- | --- |
| Hardware layer | felucca-bsp (submodule), `plat.h` (68 functions), app hooks `app_init / app_frame / app_render / APP_*` | its own HAL (`firmware/hal/fm1_*.h`), loader, simulator |
| Licence | MPL-2.0 file by file; JieLi's Bluetooth / WiFi libraries linked as binaries | GPL-3.0-only; the Bluetooth stack GPL-3.0-only OR MPL-2.0 (3274ce56) |
| Bluetooth | JieLi's stack (peripheral, central); PR #13 a clean-room NimBLE peripheral | our own LE stack (peripheral, central, bonds) and BR/EDR A2DP audio, no vendor library |
| Memory map | XIP 0x02000120 / 0x8DFBC, RAMTEXT 0x8000, RAM 96K, POOL 0x54000, NOINIT | the same, RAMTEXT 0x7F00, plus LOAD (the LZB boot stream) and CACHE 28K |
| Flash data | windows 0x97000-0xE0000, 0x93000-0x97000, 0xE5000-0xFC000 | our store, UP_FM6, drum records in exactly those windows |
| Timers | TIMER4 the 24 MHz clock, TIMER5 the tick | the same |
| Size of the platform layer | template without Bluetooth: 82,152 B, about 44 KB of it fonts, about 30 KB the platform | the parts fm1-base would replace: about 25-27 KB (by symbol, 94 KB of the image unattributed); `dual.c` 13 KB and `cpuguard.c` 5 KB stay ours |

So a port is about flash-neutral (unmeasured exactly: that needs Optimist built on fm1-base). Optimist's app code touches the
hardware through few calls: the engines, sequencer, effects and drums mostly `fm1_irq_on / off` (-> `plat_lock / unlock`) and
`fm1_ticks` (-> `plat_ticks24`); 164 distinct `fm1_*` functions in all, most in ble/, bt/, system/, io/.

**Licences.** fm1-base's MPL-2.0 files (no Exhibit B) can go into Optimist, which stays GPL-3.0-only. Only what hdavid wrote
alone, not derived from SLOOP, Felucca, Melodee or other GPL code, can go to fm1-base (their rule: no GPL code, each file's history
checked). JieLi's binary libraries (fm1-base's default Bluetooth, its WiFi) cannot be linked into a GPL firmware: Optimist keeps its
own Bluetooth and has no WiFi.

## 2. Adoption: Optimist as an fm1-base app

1. **Talk to Charles first**: the intent, the gaps below, and how our Bluetooth stack relates to PR #13 (Julius Vitkauskas's
   NimBLE peripheral). The user's call when.
2. **A shim inside Optimist**: the app's hardware calls renamed to `plat_*` on our own HAL. Costs nothing; shows the real surface.
3. **Gaps fm1-base needs for us** (each a PR upstream, proposed one at a time):
   - Bluetooth: `ble = "optimist"` beside `ble = "open"`: LE peripheral and central with bonds, then two links at once (issue #10:
     neither open stack has it yet; ours: "Not built: two simultaneous links").
   - Bluetooth audio: a `plat_bt_audio_*` group and a tap after MASTER (nothing like it upstream).
   - Render control: our commit-aside render, the CPU guard's prediction and shedding, an ISR in RAM (fm1-base has `APP_OVERLOAD`
     only): an "app owns the block" hook.
   - Drawing: a build without fm1-base's fonts (we draw our own), or Optimist on its canvas; the mirror reads that canvas.
   - USB: our own `io/usb` (USB audio from Melodee, GPL) vs felucca-bsp's `usb.c`; the editor's SysEx, backup and console through
     `APP_SYSEX` / `APP_USB_CMD` (not checked yet).
   - Storage: existing users' globals at 0xFC000 migrated to fm1-base's store format.
4. **Optimist built on fm1-base** on a local branch, every test passing, then the user's tests on the FM-1 (the user flashes).
5. **Users' upgrade path**: the package identity, the settings carried over.

## 3. Backports from fm1-base into Optimist

| What | Licence | State |
| --- | --- | --- |
| The mirror's features ours lacked (section 4) | ours, written again | done on `fm1base-port`, host-tested |
| Installer: finds the FM-1 under any port name, checks the package's CRCs, checks after resume (`web/fm1ota.js`, `fm1pkg.js`, from Chris Farrell's fm-static) | MPL-2.0 | port names: done d0eb6aa2 (we had the hole: cached names were never probed); package CRCs: done de53991e (we had none); the return check after resume: we had it |
| m-bootloader in the installer | MIT | open |
| USB MIDI host (felucca-bsp `FELUCCA_USB_HOST`) | MPL-2.0 | open: a port onto our USB code |
| Storage: an unreadable A/B copy refuses the save instead of counting as blank (PR #13) | MPL-2.0 | done cd92ef91 (we had it: a save with both copies unread was lost on the next load) |
| Screen reader (`announce.c`, eSpeak in the emulator) | MPL-2.0 / GPL | open, if wanted |
| Key velocity and pressure (`lib/touch.c`, `sense.c`) | MPL-2.0 | open: not compared with ours |
| WiFi | JieLi binaries | no (GPL) |

## 4. The screen mirror: one protocol

The user's ruling (2026-10-26): theirs unless ours is much better; then: put every feature theirs has into ours, test and compare,
then decide.

**Measured** on the UI's whole host run (197,551 blits, 24,786 fills: the same writes through both encoders):

| | bytes |
| --- | --- |
| ours (`ed_mirror_enc.h`, version 2) | 68.2 MB |
| fm1-base's (`device/mirror.c`, version 3, its row-segment hashes on) | 267.4 MB |
| ours with fm1-base's segment skipping added | 67.4 MB (-1.1 %) |
| ours, then Huffman (order 0) / an adaptive range coder with a byte of context | 58.1 MB / 43.1 MB (estimates) |
| ours, then zlib / lzma over the whole stream | 7.6 MB / 1.0 MB |

So ours is about 4x smaller on Optimist's flat UI (its two-colour bitmaps and "the row above"); not measured on fm1-base's
anti-aliased screens. Entropy coding gains little; the real redundancy is pages that come back after others overwrote them, which a
cache on the client (the device keeps a small table of hashes it sent, sends "this band = hash H") could take: an idea, unmeasured.

**fm1-base's features, now in ours** (`fm1base-port`, uncommitted; the shipped iOS app unaffected: new names, not new versions):

| Feature (theirs) | Ours | State |
| --- | --- | --- |
| Input report (cmd 0x53): held, encoders, MASTER, lit LEDs | 106 op 4 STATE, asked with bit 2 (`opti_mstate_1`): held buttons and keys, lit and dim LEDs (PLAY's green lamp too), knob detents, MASTER's pot and whether the client's plays | done, host-tested |
| Remote MASTER (op 4), let go (op 0), seq with idempotent answers | 108 kinds 3 and 4, a fourth byte seq (`opti_panelx_1`) | done, host-tested; the web Mirror tab uses seq (a lost key-up is sent again) and let go |
| Row-segment hashes: what the viewer has is not sent again | `MIR_SKIP` in `ed_mirror_enc.h`, for any blit (theirs: full-width ones only) | done, host-tested; -1.1 % on our UI, 7.7 KB of pool |
| Frame seq numbers (a gap: the viewer asks a refresh) | not needed: our device never leaves a gap (it drops and catches up whole) | by design |
| Test session (op 5): a blank FM-1 that never writes its flash | 108 kind 5 (`opti_mtest_1`): a restart into a blank FM-1 (felucca.c `st_blank`: Optimist's own data reads erased, writes are no-ops, sample uploads refused; the user's samples still sound), ended by a restart (asked, the mirror off, no client in 15 s, a crash); USB only | done; tried on an FM-1 twice (`tools/fm1_mirror.py session`): blank while it lasts, the screen before and after identical |
| PNG grab tool (`tools/fm1_mirror.py`) | `tools/fm1_mirror.py shot OUT.png [--state]`, `decode` (tests/fm1_mirror_test.py) | done, host-tested; never run against an FM-1 |
| Lit keys on a client's keyboard | none: the iOS app has no on-screen keyboard (its keys are the Mac's keyboard as the panel); the app takes seq and let go instead (a lost key-up sent again; keys let go when the app leaves the front) | done; 71 app tests pass (an App Store submission when shipped) |

**Ours, not in theirs** (to propose upstream once the protocol is chosen): Bluetooth transport, the bitmap and row-above coding,
the meters, touch navigation (109 NAV), the clip (111 CLIP) as app operations.

## 5. From the sdcard work (local only)

The sdcard session's suggestions for fm1-base (its branch stays local; nothing from it goes upstream without the user's OK): one
packed SRAM region in place of RAMTEXT / RAM / POOL (measured: one speed everywhere); the CPU guard's window ending at the real end
of code, not `_etext`; an app slot up to 0xD0000 with the loader and the flash driver never writing the SDK VM (the radio's
calibration at 0xE8000) or key_mac. The arena, overlays, the LZB boot stream and the SD code stay Optimist's.

## 6. Open

- The mirror work on an FM-1: no profile fits yet (default 1.9 KB of flash over, synths-2 the pool 120 B short of its headroom:
  the segment table is 7.7 KB of pool for -1.1 %); every host test of it passes, the three failures left are size checks (the
  builder's drift, the rescue test's slot, the render-loop cost on a profile without those engines).
- The first exact number: Optimist built on fm1-base, its flash and RAM.
- The talk with Charles, and with Julius about the two open Bluetooth stacks.

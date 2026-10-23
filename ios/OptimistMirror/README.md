# FM-1 Mirror (iPhone)

Shows the FM-1's screen on an iPhone, pixel for pixel, over a USB cable or Bluetooth MIDI.

## The firmware

Build it with the builder item `ED_MIRROR` (System, under the web editor). It is off by default. It works with or without live
editing and adds about 300 B of flash. The protocol is cmd 106 in `web/EDITOR_PROTOCOL.md` ("The screen mirrored").

## Install on the phone

```
cd ios/OptimistMirror && open OptimistMirror.xcodeproj    # (after a change of project.yml: xcodegen)
```

Run the `OptimistMirror` scheme on the iPhone. Signing is automatic; put your team in `Local.xcconfig` next to this file
(git-ignored), one line `DEVELOPMENT_TEAM = <your team ID>`, or pick the team in Xcode's Signing tab. The tests (`⌘U`, or `xcodebuild test` on a simulator) decode frames the firmware's own encoder
wrote (`Tests/Fixtures`, from `MIRROR_FIXTURE=… build/host/mirror_test`).

## Use

Plug the FM-1 into the iPhone (straight into USB-C, or through the Lightning camera adapter) and open the app. Or, without a
cable: turn on SYSTEM > Bluetooth on the FM-1, tap **Bluetooth…** in the app and connect the FM-1 in iOS's sheet. The app uses
the USB port when there is one, else the Bluetooth one; the small icon in the corner shows which (tap it for the sheet). It asks
for the mirror every second and draws what comes. The mixer's meters come over USB 10 times a second (not over Bluetooth). The screen stays on while the app is in
front.

Over Bluetooth the link is slower: small changes come as they happen; when the screen changes faster than the link carries, the
FM-1 drops what it draws and sends the whole screen again once the link is clear (at most 4 times a second, no flash on the
device). How fast that is on the real link has not been measured yet.

## Projects, Library, Backup

The button in the top-left corner of the mirror opens the web editor's Projects, Library and Backup tabs as a sheet. They send the
editor's SysEx commands (`web/EDITOR_PROTOCOL.md`) over the same CoreMIDI ports, one request at a time; the mirror's request pauses
while one of them works. Files are the web editor's: a `.optimist-backup` (full backup, or one section), `felucca-library` /
`felucca-patch` JSON (the library, the bank, one patch) and `optimist-pattern` JSON open in the web editor and the other way round.
Export goes through iOS's share sheet (Save to Files, AirDrop ...), import through the Files picker.

- **Projects**: the sections A.. (PROJECT 9: load, save; a section as a file through BK_*), the per-track patterns (PAT_* 79..84) when
  the firmware has them.
- **Library**: the 32 user presets (UP_* 16..21: load on the track, store the current sound, erase, copy to the library, write
  from it, export the bank) and the app's library of patches (a `felucca-library` file in the app's folder).
- **Backup**: every stored object (BK_* 43..48, the sample slots through SMP_* 11..13) into one file; restore with the objects ticked.
  The snapshots area is restored in the web editor only; DX7 `.syx` and the user drum kits (UKIT) are not in the app.

These need the USB cable: over Bluetooth the firmware answers the mirror's request only (`ed_mirror.c` `mir_ble_rx`), the app says so.

`Tests/Fixtures/make_fixtures.mjs` regenerates the web-written files the tests read (`web_*`) and checks the other direction: the tests
write the app's files into `$FM1_EXPORT_DIR` (`TEST_RUNNER_FM1_EXPORT_DIR=<folder> xcodebuild test ...`), and
`node ios/OptimistMirror/Tests/Fixtures/make_fixtures.mjs read <folder>` opens them with the web editor's functions.

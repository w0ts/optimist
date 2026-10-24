# Optimist Mirror (iPhone, iPad, Mac)

Shows the FM-1's screen pixel for pixel over a USB cable or Bluetooth MIDI, with the panel's buttons and knobs by touch,
a piano roll for a synth track's clip, and the web editor's Projects, Presets and Backup. One app from one source: the
Mac version is Mac Catalyst.

## The firmware

Build it with the builder item `ED_MIRROR` (System, under the web editor; off by default, with or without live editing).
The protocol is in `web/EDITOR_PROTOCOL.md`: cmd 106 (the screen), 108 PANEL (buttons and knobs), 109 NAV (the touch
navigation), 111 CLIP (the clip).

## Build and run

```
cd ios/OptimistMirror && open OptimistMirror.xcodeproj    # (after a change of project.yml: xcodegen)
```

Run the `OptimistMirror` scheme. Signing is automatic; put your team in `Local.xcconfig` next to this file (git-ignored),
one line `DEVELOPMENT_TEAM = <your team ID>`, or pick the team in Xcode's Signing tab. The tests (`⌘U`, or
`xcodebuild test` on a simulator) decode frames the firmware's own encoder wrote (`Tests/Fixtures`, from
`MIRROR_FIXTURE=… build/host/mirror_test`).

## Use

The app connects only when asked: plug the FM-1 in (USB-C, or the Lightning camera adapter) and tap **Connect**, or set
SYSTEM > BT MIDI > MODE to MAN or AUTO on the FM-1 and tap **Connect with Bluetooth** to pick it in the system's sheet. The tabs:

- **Screen**: the FM-1's screen. With a firmware that has them, the panel's buttons are drawn around it (the round button
  at the top right switches to the screen alone) and a drag on the screen turns the knobs. A hardware keyboard (a Mac's, or one on an iPad)
  plays the panel too (`Sources/PanelKeys.swift`). On a synth track's STEPS page, the piano-keys button shows the
  clip as notes: a tap adds or removes a note, a drag moves it, a drag of its end stretches it, a swipe turns the page.
- **Projects, Presets, Backup**: the web editor's tabs, with the same files (`.optimist-backup`, `felucca-library` /
  `felucca-patch`, `optimist-pattern` JSON). Export through the share sheet, import through the Files picker. The snapshots
  area is restored in the web editor only; DX7 `.syx` and the user drum kits are not in the app.
- **Connection**: the port in use, another one, Bluetooth, Disconnect.

Over Bluetooth the FM-1 answers the screen, the buttons and knobs, the navigation and the clip only (`ed_mirror.c`
`mbr_take`): Projects, Presets and Backup need the USB cable. The mixer's meters come over USB only.

`Tests/Fixtures/make_fixtures.mjs` regenerates the web-written files the tests read (`web_*`) and checks the other
direction: the tests write the app's files into `$FM1_EXPORT_DIR` (`TEST_RUNNER_FM1_EXPORT_DIR=<folder> xcodebuild test ...`),
and `node ios/OptimistMirror/Tests/Fixtures/make_fixtures.mjs read <folder>` opens them with the web editor's functions.

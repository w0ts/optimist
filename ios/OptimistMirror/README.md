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

- **Mirror**: the FM-1's screen. With a firmware that has them, the panel's buttons are drawn around it (the round button
  at the top right switches to the screen alone) and a drag on the screen turns the knobs. A hardware keyboard (a Mac's, or one on an iPad)
  plays the panel too (`Sources/PanelKeys.swift`). On a synth track's STEPS page, the piano-keys button shows the
  clip as notes: a tap adds or removes a note, a drag moves it, a drag of its end stretches it, a swipe turns the page.
- **Library, Projects, Backup**: the web editor's tabs, with the same files (`.optimist-backup`, `felucca-library` /
  `felucca-patch`, `optimist-pattern` JSON). Export through the share sheet, import through the Files picker. The snapshots
  area is restored in the web editor only; DX7 `.syx` and the user drum kits are not in the app.
- **Editor, Samples, Kits, Snapshots**: the web editor's own pages, in a web view
  (one page, kept); their MIDI goes through the app's USB and Bluetooth ports (`Sources/Web/WebMIDIBridge.swift`: WKWebView
  has no Web MIDI).
- **Disconnect** (the button top right): back to the home page, where the connection is made (USB, Bluetooth, the port), Install Firmware and the
  Simulator.

**Simulator** (the home screen): the web Simulator, no FM-1 needed.

**Install Firmware** (the home screen): an official release (version, flavour), your last build or a .fwsc
file, installed over the USB cable; Build Your Own opens the web builder.

**Open a .fwsc with the app** (Finder double-click or Open With, Files, AirDrop, Mail): the app declares the package type
(`com.w0ts.optimist.fwsc`, Info.plist from `project.yml`), checks the file and keeps a copy in Documents/Firmware (visible in Files), opens
Install Firmware with it ready (file, identity, size). Nothing is written to the FM-1 until Install is tapped and confirmed; an invalid file
gets an error and is not kept. The kept packages are listed under Install to pick again.

Over Bluetooth the FM-1 answers the screen, the buttons and knobs, the navigation and the clip only (`ed_mirror.c`
`mbr_take`): Projects, Presets and Backup need the USB cable. The mixer's meters come over USB only.

`Tests/Fixtures/make_fixtures.mjs` regenerates the web-written files the tests read (`web_*`) and checks the other
direction: the tests write the app's files into `$FM1_EXPORT_DIR` (`TEST_RUNNER_FM1_EXPORT_DIR=<folder> xcodebuild test ...`),
and `node ios/OptimistMirror/Tests/Fixtures/make_fixtures.mjs read <folder>` opens them with the web editor's functions.

## Other fm1-base apps

An FM-1 that does not answer Optimist's INFO but answers fm1-base's companion capability query (cmd 0x60, `docs/COMPANION.md` in
fm1-base) is shown as the fm1-base app it is (name, version, identity, services) instead of the Optimist tabs. Over USB only; an
Optimist firmware answers INFO first and is treated exactly as before. For such an app the app offers:

- **Pages**: fm1-base's own mirror, backup, installer and companion pages, bundled (`Resources/Fm1Base`, MPL-2.0, unchanged; see its
  README for the origin commit), filled in for the app and run through the same Web MIDI bridge as the editor's pages
  (`Sources/Companion/Fm1Base*.swift`). The backup page takes the backup's magic, version and file extension from the firmware's capability answer (protocol 2;
  an older firmware's own `backup_magic`, `backup_version`, `backup_ext` extras tags are read only as a fallback); the installer needs the `.fwsc` chosen in the app.
- **Files**: the app's files, saved one by one or all in one backup file (each checked against the FM-1's CRC-32).
- **Parameters**: the app's parameters by group, read and set.

`Tests/CompanionTests.swift` replays a transcript the fm1-base host simulator recorded (`Tests/Fixtures/companion_transcript.txt`).

The Web tabs run the web editor from the app itself: `tools/sync_ios_editor.py` (a build phase, see `project.yml`) copies `web/editor.html`, the shared style, the fonts and the notices into the bundle's `OptimistEditor/`, and `Sources/Web/EditorSite.swift` serves them as `optimistedit://app/`. No copy is committed. The Install, Build and Simulator modules are not bundled (the Flash tab, the web builder and the Simulator need the internet); the editor's saved settings and libraries live under this scheme's own storage, not the site's.

# FM-1 Mirror (iPhone)

Shows the FM-1's screen on an iPhone, pixel for pixel, over a USB cable.

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

Plug the FM-1 into the iPhone: straight into USB-C, or through the Lightning camera adapter. Open the app. It finds the FM-1's
USB MIDI port ("Felucca" / "Optimist", never the Bluetooth one), asks for the mirror every second and draws what comes. The
mixer's VU meters are not sent. The screen stays on while the app is in front.

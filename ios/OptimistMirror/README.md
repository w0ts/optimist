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
for the mirror every second and draws what comes. The mixer's VU meters are not sent. The screen stays on while the app is in
front.

Over Bluetooth the link is slower: small changes come as they happen; when the screen changes faster than the link carries, the
FM-1 drops what it draws and sends the whole screen again once the link is clear (at most 4 times a second, no flash on the
device). How fast that is on the real link has not been measured yet.

# Optimist Mirror on the App Store

What App Review will look at, and what is done. The app is free.

## Done

- **Privacy policy:** https://motscousus.com/optimist/privacy.html (source: `privacy.html` here; the app collects nothing).
- **Name:** "Optimist Mirror" (not "FM-1 ...": a name led by another company's product, M-VAVE's FM-1, can be flagged).
- **Icon:** the FM-1 Emulator's lilac panel (`Resources/Assets.xcassets/AppIcon.appiconset`).
- **Bluetooth permission text:** in `project.yml` (`NSBluetoothAlwaysUsageDescription`).
- **iPhone, iPad and Mac:** every orientation, a launch screen; the Mac as a Mac Catalyst app (and "Designed for iPad" on Apple Silicon).
- **A first screen that is not empty:** with no FM-1 connected the app shows its icon and the ways to connect.

## To do before submitting

1. **A demo video for the reviewer.** The reviewer has no FM-1; "we could not test the app" is the usual rejection for hardware
   companions. In App Review Information > Notes: a link to a short video of the app with an FM-1 (USB and Bluetooth: the screen,
   Projects, Presets, Backup) and one line saying it needs an FM-1 running the Optimist firmware.
2. **Say plainly that it needs the Optimist firmware,** in the description and its first line, with a link to where the firmware
   comes from; and that it is not affiliated with M-VAVE. Otherwise: one-star reviews, and a reviewer may count it as broken.
3. **App Privacy form** in App Store Connect: "Data Not Collected"; the privacy policy URL above.
4. **Screenshots** for iPhone (6.9" and 6.5"), iPad (13") and Mac: the emulator and the mirror can make them.
5. **Age rating** questionnaire (no objectionable content: 4+).
6. **Support URL** (required): a page on motscousus.com or the GitHub repository.
7. **Category:** Music.
8. **Copyright** line and the seller name (the developer account).
9. **Bluetooth-only use:** Projects, Presets and Backup need the USB cable (over Bluetooth the firmware answers only the screen
   mirror); the app says so on those screens. Mention it in the review notes so it is not read as a bug.

## Notes

- Release builds are signed with the team in `Local.xcconfig` (git-ignored; never commit a team ID).
- Archive: Xcode > Product > Archive (an iOS device or "Any iOS Device" selected), then Distribute App > App Store Connect.
  The Mac version is the same app record ("Mac Catalyst" on the target), uploaded from an archive made with "My Mac" selected.

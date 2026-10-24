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

## Store text (paste into App Store Connect)

**Name:** Optimist Mirror
**Subtitle** (30 max): FM-1 screen, presets, backups
**Category:** Music (secondary: Utilities) · **Price:** Free · **Age rating:** 4+ (every question "None" / "No")
**Support URL:** https://github.com/w0ts/optimist · **Privacy Policy URL:** https://motscousus.com/optimist/privacy.html
**Copyright:** 2026 Henri David

**Promotional text** (170 max):
See your FM-1's screen big on your iPhone, iPad or Mac, play it from the keyboard, and keep your projects, presets and backups safe.

**Description:**
Optimist Mirror is a companion for the M-VAVE FM-1 running the free Optimist firmware.

- See the FM-1's screen, pixel for pixel, as large as your device allows, over USB or Bluetooth. Tap to hide everything but the screen.
- Play the FM-1 from your Mac's keyboard (USB): the buttons, the keys and the knobs.
- Projects: load, save, export and import song sections and patterns.
- Presets: the FM-1's 32 user presets, and your own collection on your device, with search and tags.
- Backup: a full backup of everything stored on the FM-1, and restore.
Files are the same as the Optimist web editor's.

Requires an FM-1 running the Optimist firmware (https://github.com/w0ts/optimist). The FM-1's stock firmware is not supported. Projects, Presets and Backup need the USB cable; the screen also works over Bluetooth.

Optimist Mirror is an independent project, not affiliated with M-VAVE.

**Keywords** (100 max): synth,synthesizer,FM-1,MIDI,mirror,backup,presets,editor,remote,screen,sequencer,optimist

## App Privacy (App Store Connect > App Privacy)

"Data Not Collected". No tracking.

## App Review Information > Notes

This app needs an M-VAVE FM-1 synthesizer running the Optimist firmware (open source: https://github.com/w0ts/optimist).
Without one the app shows its connection screen. A video of the app in use with the device: <LINK TO THE VIDEO>.
- Screen: the FM-1's display, over the USB cable or Bluetooth MIDI ("Connect with Bluetooth" opens iOS's Bluetooth MIDI sheet).
- Projects, Presets and Backup talk to the FM-1 over the USB cable only; over Bluetooth those tabs say so.
- On the Mac, the keyboard presses the FM-1's buttons (USB).
No account, no data collected, no network use.

## Screenshots

App Store Connect asks for iPhone "Dynamic Island (medium display)" (1206 x 2622: the iPhone 18 Pro simulator), iPad 13"
(2064 x 2752 or 2752 x 2064) and Mac (2880 x 1800). Build 2's set: the mixer, LFO and ENV pages with the touch columns.
Made from a DEBUG build: `MIRROR_DEMO` = a 240 x 240 RGB565 file (the UI test's `opt-cardspic-*.ppm`, converted),
`MIRROR_DEMO_NAV` = "screen,mode,row,track" (the columns as that page), `MIRROR_QUARTER=1` for the iPad in landscape (the
simulator saves it sideways: rotate 90), favourites seeded with `simctl spawn <dev> defaults write com.w0ts.optimist.mirror
favorites -data <hex JSON>`. Mac: the files copied into the app container's tmp (sandbox), the window captured with
`screencapture -o -l <id>` and fitted to 2880 x 1800.

**Upload from the command line** works when run outside Claude Code's sandbox: `xcodebuild archive` (generic iOS, then Mac
Catalyst), then `xcodebuild -exportArchive` with `method app-store-connect`, `destination upload`.

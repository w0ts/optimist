# fm-1-base's web installer code, vendored

`fm1ota.js` (the FM-1 USB-MIDI update protocol over Web MIDI), `fm1pkg.js` (the package identity, the logical image, the
CRC check) and `fm1ports.js` (finding FM-1s on Web MIDI: usual names, then the screen reader's HELLO) are fm-1-base's own
files, MPL-2.0 (their headers say so and name their authors), as the fork has them, with one change of ours in `fm1ports.js`:
a Bluetooth MIDI port (`IS_BLUETOOTH`) is never listed, asked or tried (`livePorts`; `fm1ota.js` `askable` too):

- from: fm-1-base, web/fm1ota.js, web/fm1pkg.js and web/fm1ports.js on the fork's branch `up-fm1base` at 59d0559 (fm-1-base's
  upstream main 02f422c7 merged into the fork's integration line; our four installer branches
  feat/web-skip-bluetooth, fix/install-port-names (family port names: now answered by the HELLO instead), foreign-loader,
  stock-checksum are in it)
- `m-bootloader/`: our m-bootloader 1.4 (w0ts/m-bootloader, branch optimist, 8b171dd; by Julius Vitkauskas, MIT; its LICENSE is beside it)

Do not edit these files here. A change goes into fm-1-base (a branch there, then a new copy here). What Optimist adds is in
`../fm1ota_optimist.js` (GPL-3.0-only), which imports these files and wraps them; `../installer/installer.js` uses both.
Replace by copying the two files again and running `node web/test_web.mjs`.

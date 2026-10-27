# fm-1-base's web installer code, vendored

`fm1ota.js` (the FM-1 USB-MIDI update protocol over Web MIDI) and `fm1pkg.js` (the package identity, the logical image, the
CRC check) are fm-1-base's own files, MPL-2.0 (their headers say so and name their authors), byte for byte:

- from: fm-1-base, web/fm1ota.js and web/fm1pkg.js on the fork's integration branch `trial/optimist-integ` at 99151ca (it
  carries fm-1-base's origin/main 934e40d plus our four installer branches: feat/web-skip-bluetooth,
  fix/install-port-names (family port names), foreign-loader, stock-checksum)
- `m-bootloader/`: web/m-bootloader/ of the same commit (m-bootloader 1.2 by Julius Vitkauskas, MIT; its LICENSE is beside it)

Do not edit these files here. A change goes into fm-1-base (a branch there, then a new copy here). What Optimist adds is in
`../fm1ota_optimist.js` (GPL-3.0-only), which imports these files and wraps them; `../installer/installer.js` uses both.
Replace by copying the two files again and running `node web/test_web.mjs`.

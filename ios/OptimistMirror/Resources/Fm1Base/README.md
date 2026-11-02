# fm1-base pages in Optimist Mirror

These files are fm1-base's own web pages, copied unchanged (MPL-2.0, their headers kept; the licence text is
`LICENSE-MPL-2.0`). The rest of this app is GPL-3.0-only: the two licences stay apart, file by file.

Origin: the fork's branch `up-fm1base` (upstream fm-1-base main 02f422c7 merged into our line), commit `59d0559`, the `web/` folder
(2026-11-02; `fm1ports.js` is ours with a Bluetooth-port exclusion, see `web/fm1base/README.md`):

| Here | fm1-base | last commit touching it |
| --- | --- | --- |
| `installer.html` | `web/installer.html` | e8dc228 |
| `fm1ota.js`, `fm1pkg.js`, `fm1ports.js` | `web/fm1ota.js`, `web/fm1pkg.js`, `web/fm1ports.js` | see `git log` there |
| `backup/index.html` | `web/backup/index.html` | 887eddf |
| `mirror/index.html` | `web/mirror/index.html` | 2bb8570 |
| `companion/index.html`, `companion/companion.js` | `web/companion/` | 4a410dc |

**One change from fm1-base, as fm1-base's `web/fonts` + the `<link>` in `mirror`, `backup`, `companion`:** the pages take
Barlow from `fonts/fonts.css` (woff2 files and `OFL.txt` beside it, SIL OFL 1.1, sources and sha256 in `fonts/FROM.txt`), not
from fonts.googleapis.com: the app works offline and asks no outside host (macOS's firewall asked). `Fm1BaseSite.font` serves
`/fonts/`. fm1-base: `feat/local-fonts` (upstream main) / `230e423` on `fm1-next-fb`. `tests/outside_hosts_test.py` keeps it so.

To check they are still unchanged (apart from that `<link>`): `diff -r Resources/Fm1Base/backup <fm1-base>/web/backup` and so on.

## How the app uses them

fm1-base's `tools/make_site.py` fills each page in for one firmware (`{{NAME}}`, `/*META*/`, `/*LIB*/` ...). The app does the
same at load time, from what the connected FM-1 says about itself in the companion capability query (0x60), so the files here
stay as they are: `Sources/Companion/Fm1BaseSite.swift` fills them in and serves them to the web view as `fm1base://app/<page>/`
(`Fm1BaseWeb.swift`), with Web MIDI from the app's own `WebMIDIBridge`.

- `mirror/`, `companion/`: need only the app's name.
- `backup/`: also needs the app's backup magic, version and file extension (app.h `APP_BACKUP_MAGIC`, `APP_BACKUP_VERSION`,
  fm1.toml `backup_ext`), which the capability query carries from protocol 2 on (fm1-base `docs/COMPANION.md`). From an older
  firmware (protocol 1) they are read from the app's companion extras (`APP_COMPANION_EXTRAS`) as a fallback:
  `backup_magic=ABCD;backup_version=1;backup_ext=bak`. Without either the app's files can still be saved natively (Files tab).
- `installer.html`: the package is not on any site here: the user chooses the `.fwsc` in the app (Pages tab), which serves it to the page.

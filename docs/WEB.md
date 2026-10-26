# The web site, the web builder, cloud builds and the simulator

Everything a user reaches at <https://w0ts.github.io/optimist/>: how the site is made and published, the builder that
runs in the browser, the cloud builds it requests, releases, and the firmware compiled for the browser's Simulator tab.
The user's view of all of it is the README.

## The site

The root redirects to the installer (`webapp/installer/`); the editor is at `webapp/editor/` (its tabs include Install,
Build and Simulator) and the builder at `webapp/builder/`. Every link is relative, so it works under the `/optimist/` base
path. Chrome or Edge is needed for Web MIDI.

### What is published

`web/make_site.py` builds the site (`make web` builds it locally into `build/optimist-site/`, with no firmware in it: the
installer then offers cloud builds and a `.fwsc` of yours). The published installer offers, newest first, each published
release (not a draft, not a pre-release) with its published flavours (the profiles marked `publish: yes`), then this
commit's development build of each flavour in `web/flavours.json`. `pages.yml` downloads the releases' packages into the
site (`firmware/r/<tag>/`) because a page cannot fetch a GitHub release file itself (no CORS header). The site holds:

- `webapp/installer/`: the standalone installer. `index.html` (from `web/installer/index.html`) is one offline file: the
  shared header and style and the installer module, inlined. Beside it `installer.js` (the same module as one file, for
  the editor's Install tab) and `site.json` (the packages, simulators and repository it lists).
- `webapp/editor/`: the web editor (`web/editor.html`, its font `fukiai.ttf` and the licences).
- `webapp/builder/`: the web builder (below), its data exported fresh by `tools/export_builder_web.py`, and
  `build-info.json` (the commit for the cloud key, the repository). `python3 web/make_site.py --builder OUT` makes only
  this part.
- `firmware/optimist-<version>-<id>.fwsc` (and `.wasm`, the flavour's simulator): one per flavour; `firmware/r/<tag>/`: the
  releases; `firmware/FM-1_015.fwsc`: M-VAVE's official V15, unchanged (`web/stock/`, Apache-2.0), the installer's return
  to stock. The installer checks each download against the SHA-256 written into its page.
- the dinghy icon files (`assets/logo/`; the PNGs are rendered by `tools/gen_web_icon.py` and committed), `manifest.webmanifest`,
  `LICENSE`, `LICENSING.md`, `LICENSES/`.

All pages share one look and header (docs/WEB-STYLE.md): `web/style/optimist.css`, which `make_site.py` inlines.

### The installer module

`web/installer/installer.js` exports `mountInstaller(container, { packages, version, source, onNavigate, links, midi })`:
it renders four steps (1 Connect, 2 Choose: a release and its flavours, a cloud build, or a `.fwsc` of yours; 3 Install:
the back-up question, the four phases and the bar; 4 When something goes wrong: the rescue paths and the return to V15)
and returns `{ unmount(), busy }` (`busy` while writing: the host must not unload or hide it). `onNavigate(where)` lets
the host open its own tab instead of following a link; `midi` replaces `navigator.requestMIDIAccess` (the tests). The
update itself is `web/fm1ota.js` and `web/fm1pkg.js`. The module's CSS uses only the shared tokens, so it takes the look of
the page that holds it.

```js
import { mountInstaller } from "../installer/installer.js";
const site = await fetch("../installer/site.json").then((r) => r.json());
const inst = mountInstaller(document.getElementById("tab-install"), { ...site, onNavigate: (where) => openTab(where) });
```

Tests: `web/test_web.mjs` (identities, package checks, the bar, the messages, the texts against the README and the
firmware) and `web/e2e_installer.mjs` (headless Chrome with a simulated FM-1, `web/installer/mock_fm1.js`: connect,
choose, install, a pulled cable and the resume, no FM-1, a wrong file, light / dark, 390 px, no Web MIDI; no real MIDI port
is opened; `--shots DIR` saves screenshots).

### The flavour manifest

`web/flavours.json` lists the firmware packages: `id` (lowercase, part of the file name), `title` and `description` (shown
by the installer), `profile` (the `config/profiles/` profile CI builds), `file` (a glob of the package it produces:
`dist/optimist-*-<name>.fwsc`, the slug of the profile's `# name:`) and `sim` (the simulator `.wasm` built for it). `"default": true` marks the flavour the
web builder opens with (without one: the first). Today there is one flavour, `default` (the `synths_USBaudio_BTaudio_BTmidi` profile). To add one, add an entry; CI builds
every listed profile. `python3 web/make_site.py --profiles` prints the profiles; `--flavours web/flavours.json <version>
<out>` builds the site from packages already in `dist/`.

### How the firmware gets there

`.github/workflows/pages.yml` runs on a push to `main` and by hand (Actions > pages > Run workflow). A published release
asks for a run on `main` (its `redeploy` job), because the `github-pages` environment deploys from `main` only. It
downloads the JieLi toolchain and SDK files on the runner (`tools/optimist.py setup`; nothing of them is stored or
published), builds each listed profile and its simulator (Emscripten, `tools/sim.py build --wasm`), makes the site and
deploys it with `actions/deploy-pages` (actions pinned to commit SHAs; permissions: contents read, pages write, id-token
write).

**Enable it (once).** Settings > Pages > Source: **GitHub Actions**; the default branch (`main`) is allowed in the
`github-pages` environment already. Push or run the workflow; the URL is printed by the `deploy` job (if `configure-pages`
fails with "Get Pages site failed", the first step is missing).

### Cut a release

1. Set `VERSION` (X.Y, one digit each: it is the package identity, `FM-1_7(X+2)Y`) and commit it.
2. Tag that commit `v<VERSION>` (a suffix after a dash is allowed, `v0.2-rc1`) and push the tag: `release.yml` builds every
   published profile (`# publish: yes`) in `config/profiles/` as a release build (`optimist-<version>-<profile>.fwsc`) and creates a **draft** release
   with them and `SHA256SUMS.txt`. A tag that does not match `VERSION` fails at once.
3. Read the draft and publish it: the release event asks for a Pages run on `main` and the installer lists the release.

## The web builder

`webapp/builder/` is the firmware builder of `make builder` in a browser: the same items, groups, rules, size estimate and
fit. It does not reimplement them: the page runs the builder's own Python (`tools/builder/core.py`, `registry.py`,
`backports.py`, `room.py`) with [Pyodide](https://pyodide.org) in a Web Worker; `tools/builder/web_api.py` gives the page
the menu's views of that code.

### What it does

- **The list:** grouped as in the menu: check boxes for on/off items, choice lists for sized items, options under their
  parent (greyed while it is off), an `experimental` tag, and an (i) popover with an item's notes.
- **Costs:** chips in the menu's words (`flash +1.2K`), red when the item holds part of an overflow; `◀ off = fits` marks an
  item that alone would make everything fit. Rule errors show on the items they name and in the messages.
- **Layout:** the item tree on the left; on the right cards that stay in view: **Size** (the gauges), **Your
  configuration** (download, hash and key, import, the canonical form), the ways to fit and the messages, the **Info** card of
  the selected item. At 900 px and narrower the cards stack above the list with the Size card compact at the top.
- **Gauges:** FLASH, RAM, POOL and RAMTEXT show used / limit in KiB (exact bytes on hover), the overflow in red with the
  limit marked on the bar, the UNDO ring, and the configuration's key (a click copies it). It is the estimate
  (`configure.budget`, from the nearest real build in `costs.json`) and only informs: nothing is refused because of it.
- **Make it fit:** the button exists only while a gauge is over. It opens up to four proposals from `core.proposals` (the
  least loss, the single item whose removal fits, keep every engine, keep every effect), each
  with the exact changes, the four gauges after it and whether it fits. **Accept** applies it (a toast offers **Undo**),
  **Reject** dismisses it, **Preview** (or hovering) shows the result without applying.
- **Messages:** only what the user must act on, one line each: red for an error or an overflow, amber for each experimental
  item that is on. The rest (provenance, "tested in the emulator only", credits) is in the (i) popovers and a closed
  "Notes (n)" section.
- **Search, sort, folding:** search matches label, key, group and description; sort works like the menu's `o`; sections and
  parent rows fold (remembered by the browser). Keys outside text fields: `/` search, `o` sort, `x` expand all, `c` collapse
  all.
- **Presets:** the flavours, every shipped profile, the registry defaults and, when the browser kept it, your last
  configuration. The page opens with the manifest's default flavour; **Reset to default** goes back to it.
- **Import and export:** load or paste a `.config` (read with `core.parse`, unknown keys warned about as on the CLI);
  **Download .config** writes `core.dump`.
- **The key:** `core.cloud_key` needs the full commit, which `make_site.py` writes to `build-info.json`; a site built from a
  modified tree has none and says the key is computed at build time.
- **Build in the cloud** (below): offers a build of the same key made before, else opens the cloud-build issue form filled in.
  Off while the configuration has errors.

It needs no storage (it keeps the last configuration in `localStorage` when it can), follows the system's light / dark, and
works on a phone.

### The module

`web/builder/builder.js` exports `mountBuilder(container, { onNavigate, pyodideBase })`, so the editor can show it as a tab:

```js
import { mountBuilder } from "../builder/builder.js";
const b = mountBuilder(container, { onNavigate, pyodideBase });
b.activate();      // the tab is shown: keyboard shortcuts on, Python started the first time
b.deactivate();    // hidden: shortcuts off, state and worker kept
b.destroy();       // gone
```

- `container` gets the classes `ob-root` and `o-ui`; nothing outside it is touched (every CSS rule is under `.ob-root`, ids do
  not collide with the editor's).
- `onNavigate(name)` is called instead of following a `data-nav` link. `pyodideBase` is another Pyodide directory (tests).
- **Python is not loaded before the first `activate()`.** The handle also has `state`, `ready`, `readyFlag`, `call(fn, ...)`,
  `choosePreset(id)`, `importText(text, source)` and `setItem(key, value)` (the tests use them).
- The host must include `web/style/optimist.css` and set `--menubar-h` if it has a sticky bar.
- Files, all in `webapp/builder/`: `builder.js`, `builder.css`, `logic.js`, `py-worker.js`, `data/`, `build-info.json`;
  `index.html` + `ui.js` are the standalone page (`window.optimistBuilder` is the handle).

### Pyodide and the data

Pyodide is pinned in `web/builder/logic.js` (`PYODIDE_VERSION`, 314.0.7, Python 3.14) and loaded from jsDelivr; only this
page loads it (about 13 MB the first time, cached after). `tools/export_builder_web.py` writes `web/builder/data/`, which
is committed: `python.json` (the Python files, `tools/backports.json`, the `needs_auto` list read from the firmware
headers), `registry.json`, `costs.json`, `profiles.json` (the shipped profiles and the flavours). After any change to the
registry, costs, core, backports or a profile, run `python3 tools/export_builder_web.py` and commit the result;
`node web/test_builder.mjs` (part of `tests/run_tests.sh`) fails while it is stale. `make_site.py` exports fresh data into
the site either way.

### Tests

- `node web/test_builder.mjs`: no browser, no network. The drift check; the exported Python run by an isolated CPython
  (`python3 -I`, standing in for Pyodide) compared with the CLI builder (`tests/builder_web_ref.py`) for every profile and
  seeded random edits (evaluate, budget, fits, key, errors, fit, proposals, BLE); `logic.js` against the menu's rules.
- `node web/e2e_builder.mjs [--shots DIR]`: the same in headless Chrome with the real Pyodide from the CDN, then the page
  (toggling, search, sort, import, download, messages, layout at 1440 and 390 px, Make it fit, folding, light and dark).
  Not part of `make test` (needs Chrome and the network). `PYODIDE_DIR=path` serves a local copy instead of the CDN.

Not in the web builder: the menu's local actions (flash, the emulator, saving and publishing profiles) and exact sizes
before a build (the cloud build's answer has them).

## Cloud builds

Anyone with a GitHub account can build a firmware from the web builder without installing anything: **Build in the
cloud** sends the configuration to this repository's GitHub Actions, which build it with the same toolchain as CI.

1. Open the builder (the Firmware tab's **Build your own…**, or <https://w0ts.github.io/optimist/webapp/builder/>) and make
   a configuration.
2. **Build**. Through the relay (below) it needs no GitHub account and the page follows it to the end; without the relay
   it opens a filled-in GitHub issue: press **Submit new issue**. The same cloud key (the site's commit and the canonical
   form) built before is ready at once.
3. A workflow answers in the issue: "Building ..." with a link to the log, then a few minutes later the **build id** (the
   key's first 16 hex digits), an **Install it** link, a link to the configuration in the builder, and the downloads
   `optimist-<name>-<key16>.fwsc` and its `-ui.zip`; it closes the issue. A refused configuration (unknown key, rule not
   met) is answered at once and closed; a failed build (most often: it does not fit, the page's size is an estimate) is
   answered with the log and stays open.
4. Install it: the page's **Flash it**, the issue's **Install it** link (`?build=<id>`), Firmware's *Your build*, or download
   the `.fwsc` and `python3 tools/optimist.py flash FILE`. There is no list of everyone's builds: a page offers yours.

| Part | What it does |
|---|---|
| `web/builder/cloud.js`, `logic.js` | Build from (`?refs`), the request (the relay, else `cloudIssueUrl`: the form with `commit` and `config` filled in), the build followed (`?status=`), your build kept |
| `web/make_site.py` | `build-info.json`: `commit` (the site's) and `repo` (`$GITHUB_REPOSITORY`, so a fork's site asks its own repository) |
| `.github/ISSUE_TEMPLATE/cloud-build.yml` | the form: *Commit* (empty = the default branch's latest) and *Configuration*; label `cloud-build` |
| `.github/workflows/cloud-build.yml` | `request` > `build` > `publish`, `failed` on a failure, `prune` daily |
| `tools/cloud_build.py` | reads the body, checks the commit, the verdict, the replies (`tests/cloud_build_test.py`) |
| the `cloud-builds` pre-release | the builds, one `.fwsc` + `-ui.zip` per cloud key |
| `tools/cloud_shelf.sh` | the `cloud-builds` **branch**: the same files, `<stem>.config` and `index.json`, one commit, replaced |
| `web/installer/installer.js` | *Your build*: `loadCloud` (the branch's commit from the API, then `index.json` and the `.fwsc` from raw.githubusercontent.com), the build found by its id, the SHA-256 checked before install |

The branch exists because a release download ends on a host with no CORS header, so a web page cannot fetch it;
raw.githubusercontent.com sends `Access-Control-Allow-Origin: *`.

**The workflow.**
- **request** (reads the repository, writes issues): refuses an author past `MAX_PER_DAY` (5) builds that ran in 24 hours (a refused request does not count; the reply says when the next is possible), then runs
  `tools/cloud_build.py` from the default branch, so a request cannot change how it is read. The issue body reaches it
  through an environment variable only. The commit must be a full SHA on a branch of this repository (only maintainers push
  branches, so only published code is built). That commit's own builder checks the configuration (`optimist.py cloud-key
  --strict --json`) and gives the key; the asset stem is `optimist-<name slug>-<first 16 hex of the key>`. If the release
  already has that `.fwsc`, the build is skipped.
- **build** (read only, no token in the checkout): checks out that commit, restores the toolchain cache `build.yml` saves
  (never saves it), runs `optimist.py package --config`, uploads the two files as an artifact.
- **publish** (writes the release and issues): attaches them to `cloud-builds` (a pre-release, never latest; an existing copy
  is kept), answers with the links and the package's SHA-256, closes the issue, then `tools/cloud_shelf.sh add` puts the build
  on the `cloud-builds` branch.
- **prune** (daily at 03:17 UTC, or by hand): `tools/cloud_shelf.sh prune`. **failed**: answers with the run's link and leaves
  the issue open.

A configuration is only integers for registry keys (`core.parse` refuses anything else), so a request can switch features
and nothing more. The checker's message is quoted in a code block, stripped to plain characters, so a request's text never
becomes a link or a mention. The token is only in the steps that call `gh`, never next to code of the requested commit.
Each issue has its own concurrency group: reopening an issue builds it again.

**Set up once (maintainer).** Settings > General > Features: **Issues** on. Create the label (`gh label create cloud-build
--description "a web builder cloud build" --color 0e8a16`). Push the default branch with the workflow and the form and let
`pages.yml` deploy (the builder must come from a commit on GitHub, or its requests name a commit the workflow refuses). No
secret is needed (`GITHUB_TOKEN` with per-job permissions).

**Housekeeping.** A build not asked for again in `RETAIN_DAYS` (30) leaves the branch and the release (the daily prune and
each publish). The branch is always one commit, replaced (force-pushed with a lease), and deleted when its last build
expires; the prune also deletes release assets of no build on the branch and, when the release is empty, the release and
its tag. Hand-over artifacts are kept 1 day. By hand: `gh release delete-asset cloud-builds NAME`. If requests are abused,
limit who can open issues (Settings > Moderation) or disable the workflow.

### Cloud builds without a GitHub account (the relay)

**The flow (the site's Firmware tab and the Mirror app alike).** Firmware's first page (the installer) offers, in
this order: **your build** (the last one finished with Build, or `?build=<id>`; picked at once when it is under 6 hours
old), the **releases** with their published flavours, a **`.fwsc` file**, and **Build your own…**. That opens the builder
(the editor's `build` view: no tab of its own, the Firmware tab stays lit; its `← Firmware` goes back). In the builder:

1. **Start from** a flavour, a published profile, your last configuration or one of **your configurations** (Save keeps
   the current one under the Name field's name, Delete removes it; `localStorage` `optimist-builder-saved`, which the
   Mirror app's web view keeps too). No development profile and no "registry defaults" are offered.
2. **Build from** main (the default), a release tag or a branch (the relay's `?refs`). The options are main's: a build
   from an older tag can be refused for an option that tag does not have; the refusal says which.
3. **Build** (`web/builder/cloud.js`). When `TURNSTILE_SITEKEY` is set (`logic.js`), it needs no GitHub account:
   - Cloudflare Turnstile is loaded at that click only (no third party on the page before) and checks the visitor is
     not a robot, usually without asking anything;
   - the page posts `{turnstile, commit, config, name}` to the relay (`CLOUD_RELAY`), which opens the cloud-build issue
     (the form's headings, then `### Sent by`); the workflow reads and checks it like any other issue;
   - the page then follows it: `GET ?status=<issue>` every 20 s gives the stage (queued, building, done, refused,
     failed, error), read from the marker the workflow ends each comment with (`tools/cloud_build.py marker`,
     `<!-- optimist-cloud {...} -->`; only the github-actions bot's count). Its steps show Sent, Building, Ready, with a
     link to the issue on GitHub and to the run's log; a refusal or a failure shows its reason;
   - done, the build reaches the `cloud-builds` branch; then **Flash it** (Firmware, the build picked) and **Try it in
     the simulator** (when it has one), and it is kept as **your build** (`localStorage` `optimist-my-build`).
   The same configuration from the site's own commit built before is ready at once. A refusal by the relay (its limits)
   is shown and the form is not opened; no relay or no check (a blocked script) opens the GitHub form instead.
4. The Simulator lists the site's builds and your own (no list of everyone's), with **Flash it**.

The page tells its host every stage: `opts.onBuild(ev)` and, in the Mirror app's web view, the `optimist` message
handler, `ev = {type: "cloud-build", stage, issue, url, id?, run?, name?, sim?, reason?}` (stage sent, building, done,
refused, failed or error); the two buttons call `opts.onNavigate("install" | "sim", {build: id})`. `mountBuilder(...,
{app: true})` is the app's page (`web/builder/app.html`): the Build card first and in view, without the files and the
keys.

What the relay holds and refuses (`cloud-relay-config.php`, outside the web root, never in git):

- **The GitHub token**: fine-grained, only this repository, **Issues: read and write** and nothing else. A leak can open
  and close issues, nothing more. The workflow, not the relay, decides what is built.
- **The robot check**: Cloudflare's `siteverify` must answer success for action `cloud-build` on a hostname in
  `turnstile_hostnames`. It is made last, only for a request that would be built.
- **`GET ?status=<issue>`** answers only for issues the relay opened (a day), cached 15 s; **`GET ?refs`** lists the
  branches and tags (not `cloud-builds`), cached 5 minutes. Both count apart from the requests to build
  (`polls_per_ip_per_hour`, 400).
- **Flooding**, under one file lock (requests are handled one at a time), per address = `sha256(salt . IP)`:
  - before anything else, the browser's `Origin` must be the site, and the request must be JSON (so it needs a CORS
    preflight) and at most 20 KB; nginx also limits that path to 10 requests a minute per IP;
  - every request counts: `per_ip_per_hour`;
  - **one build queued per address**: refused while that address's last issue is open (the workflow closes it when the
    build is done or refused), for `queue_max_seconds` at most (a failed run leaves its issue open);
  - `min_gap_seconds` between two builds, `per_ip_per_day` builds; all addresses together, `all_per_day`;
  - the same commit and configuration again from that address within a day answers its first issue.
- The workflow's own limit (5 a day per issue author) skips the maintainers (owner, members, collaborators): the relay's
  issues are authored by the token's account, a maintainer's, and the relay counts them per address instead.

Set up: PHP 7.4+ with curl. `cloud-build.php` and `cloud-relay-config.php` (from `cloud-relay-config.example.php`) go
outside the web root, with a `data_dir` writable by PHP. An exact nginx `location = /optimist/cloud-build.php` runs that
one file through php-fpm (`fastcgi_param SCRIPT_FILENAME` pointing at it). On motscousus.com, they are in
`/home/mots/cloud-relay/`. A Turnstile widget (dash.cloudflare.com > Turnstile, free) for the hostname `w0ts.github.io`
gives the site key (`logic.js`) and the secret (`turnstile_secret`). `tests/cloud_build_test.py` checks that the
relay's issue body reads as the form's.

## The simulator

Optimist's own firmware (`firmware/src/felucca.c`, the one compilation unit the FM-1 runs) built as WebAssembly for the
browser's Simulator tab (`web/sim/`). Not an emulator of the chip: the same C, compiled for the host, on a host HAL.

### What runs

Everything above the registers: the boot (`fm1_main` up to its loop), the main loop (UI, editor SysEx, autosave, storage),
the TIMER5 tick (keys, USB poll, `fm1_ms`) and the audio interrupt (`core/audio.c`, every engine, drums, effects).
`firmware/sim/hal/fm1_*.h` replace the chip's headers of the same names (found first through `-I`; the three that `src/`
includes by a relative path, `fm1_usb.h`, `fm1_uart.h`, `fm1_xip.h`, through a clang VFS overlay `tools/sim.py` writes).

- **Time** is simulated: a 64-bit count of 24 MHz ticks (`hal/sim_core.h`). Code takes no simulated time; a timer read moves
  the clock one tick, `fm1_delay_*` moves it by the delay (due interrupts run), the watchdog feed 10 us.
- **The scheduler** (`firmware/sim/sim.c`, `sim_run`) moves the clock from event to event: the 10 kHz TIMER5 tick, the audio
  DMA's switch every 256 frames at 44.1 kHz, and between them the main loop (a pass every 15 ms). Handlers run when the clock
  passes their event with interrupts on; no nesting. `system/main.c` splits `fm1_main` with four macros under
  `FELUCCA_SIM`; the device's code is unchanged.
- **Flash**: a 1 MiB RAM image at the FM-1's offsets (`hal/fm1_flash_map.h`), blank (0xFF) at start, with the device
  driver's range checks (an erase sets 0xFF, a program ANDs). A backup made on an FM-1 restores here and back.
- **Screen**: a model of the ST7789's address and memory write: a 240 x 240 RGB565 buffer, a dirty mask of 15 bands of 16
  rows, a revision.
- **USB MIDI**: a model of the SIE registers `io/usb/usb.c` uses; notes, clock, the editor's SysEx, backup and restore
  go through usb.c's own EP1 paths.
- **Panel**: `fm1_in` as the matrix scan leaves it, fed by the page (one change per key per scan frame, ~1.1 ms); knob steps
  added at once; the MASTER pot from the page; the battery reads full.

Switched off whatever the configuration says (`tools/sim.py` `SIM_FORCE` and `SIM_FLAGS`): the second core (`DUAL`),
Bluetooth (`BLE`, `BT_AUDIO`), the cache RAM, USB audio and the serial console (`USB_CONSOLE=0`), SIMD, the asm kernels
(their C runs), the stock supply start-up and the bench. No TRS MIDI IN, no CPU clock measured, no watchdog, no crash report
from a CPU exception (a wasm trap is thrown to the page).

### API

`firmware/sim/sim.c`, exported by name (`SIM_EXPORT`); every integer is 32 bit; pointers are offsets in the exported
`memory`, which never grows.

| Export | |
|---|---|
| `sim_flash()`, `sim_flash_size()`, `sim_flash_rev()` | the flash image (write a saved one before `sim_boot`), 1 MiB, erases + programs so far |
| `sim_boot()` | the start-up |
| `sim_run(frames)` | advance by `frames` at 44.1 kHz; returns `sim_status()` |
| `sim_status()` | bit 0 reboot requested, bit 1 UBOOT / update requested, bit 2 fatal; when set the run has ended: start a fresh instance on `sim_flash()`'s bytes |
| `sim_audio()`, `sim_audio_take()` | interleaved L, R int16; the frames ready (up to 16384), count reset |
| `sim_pixels()`, `sim_frame_rev()`, `sim_dirty()` | 240 x 240 RGB565; revision; band mask (bit b = rows 16b..16b+15), cleared on read |
| `sim_key(i, down)` | note key 0..26 (F3 .. G5) |
| `sim_button(i, down)` | a button by label, `ui/panel.c` order: FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ |
| `sim_enc(i, delta)` | a knob by role: SELECT ALGORITHM PRESETS KNOB 1..4; + = clockwise |
| `sim_master(v)` | the MASTER pot, 0..1023 |
| `sim_leds()`, `sim_led_count()` | 41 bytes: the buttons by label, then the 27 keys; 0 off, 1 dim, 2 lit |
| `sim_midi_buf()`, `sim_midi_in(len)` | raw MIDI in (16 KiB: SysEx, short messages, running status) |
| `sim_midi_out_buf()`, `sim_midi_out()` | raw MIDI the device sent; the bytes ready, count reset |
| `sim_info(what)` | 0 keys, 1 buttons, 2 knobs, 3 frames a half buffer (256), 4 sample rate, 5 backlight 0..16, 6 MIDI bytes lost, 7 audio frames lost, 8 `fm1_ms` |
| `sim_button_names()`, `sim_enc_names()` | the labels, `\n`-separated, 0-terminated |

Instantiate with no imports, then call `_initialize` (the reactor's start) once:

```js
const { instance } = await WebAssembly.instantiate(bytes, {});
instance.exports._initialize();
```

### Build, test and the local site

```sh
tools/builder/venv/bin/python tools/sim.py build --wasm [--config FILE] [--out build/sim/sim.wasm]
node tests/sim_test.mjs [build/sim/sim.wasm]
```

Needs the venv's Python (the generators need Pillow) and `emcc` (Emscripten; CI uses 6.0.12). The test boots, runs 3 s,
checks the screen, sound after a key, the editor's INFO / GET / SET / DESC, PROJECT save and load across a restart,
BK_LIST, a backup restored into a blank flash, and the UBOOT SysEx.

`make web` serves the local simulator in its Simulator tab (`build/sim/web/optimist-sim.wasm`, the default profile or
`PROFILE=`): built with `make web SIM=1`, or when `make web` asks because there is none (a terminal only; with no `emcc`
or a failed build the site goes on without it). It asks again when the simulator is outdated: its fingerprint (the
firmware, `firmware/sim`, `firmware/hal`, the generators, the builder's rules and the configuration: `sim_fingerprint`)
differs from now, or it was built for another profile; the Simulator tab shows "outdated". `tools/optimist.py web --sim
[--profile P | --config F]`.

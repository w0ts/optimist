# The web site, the web builder, cloud builds and the simulator

Everything a user reaches at <https://w0ts.github.io/optimist/>: how the site is made and published, the builder that
runs in the browser, the cloud builds it requests, the releases, and the firmware compiled for the browser's Simulator
tab. The user's view of all of it is the README.

## The site

<https://w0ts.github.io/optimist/> (the root redirects to the installer; the editor is at
`webapp/editor/`, the builder at `webapp/builder/`). Every link in the site is relative, so it works under the `/optimist/` base path.

### What is published

`web/make_site.py` builds the site (`make web` builds it locally into `build/optimist-site/`, with no firmware in it:
its installer offers cloud builds and a `.fwsc` of yours). The published site's installer offers, newest first, each
published release (not a draft, not a pre-release) with its published flavours (the profiles marked `publish: yes`),
then this commit's development build of each flavour in `web/flavours.json`; a release picker above them. `pages.yml`
downloads the releases' packages into the site (`firmware/r/<tag>/`): a page cannot fetch a GitHub release file
itself (no CORS header), and a published release redeploys the site:

- `webapp/installer/`: the web installer (Chrome or Edge, Web MIDI), with the firmware package choice. Its page
  (`index.html`, from `web/installer/index.html`) is a thin wrapper: the shared header and style, and the installer
  module mounted in it, everything inlined (one offline file). Next to it, for the one-page app (the installer as a tab of
  the editor): `installer.js`, the same module as one file (`web/installer/installer.js` with `web/fm1ota.js`,
  `web/fm1pkg.js` and `web/installer/installer.css` put in), and `site.json`, the packages it installs.

### The installer module

`web/installer/installer.js` exports `mountInstaller(container, { packages, version, source, onNavigate, links, midi })`:
it renders the four steps (1 Connect: Find the FM-1, what it runs, what to do when it is not found; 2 Choose: a
release (each with its date; the development build last), then its flavours with their descriptions, a `.fwsc` of yours, a link to the builder; 3 Install: the back-up question,
the four phases and the bar, the errors in words; 4 When something goes wrong: the rescue paths and the return to the
official V15) and returns `{ unmount(), busy }` (`busy` while writing: the host must not unload or hide it then).
`packages` and `version` / `source` are `site.json`'s; `onNavigate(where)` (`"build"`, `"backup"`, `"editor"`,
`"about"`) lets the host open its own tab or About box instead of following the link; `midi` replaces
`navigator.requestMIDIAccess` (the tests). A host mounts it so (the editor's page is at the same depth as the
installer's, so the packages' relative paths hold):

```js
import { mountInstaller } from "../installer/installer.js";
const site = await fetch("../installer/site.json").then((r) => r.json());
const inst = mountInstaller(document.getElementById("tab-install"), { ...site, onNavigate: (where) => openTab(where) });
```

The update is `web/fm1ota.js` and `web/fm1pkg.js`, unchanged. Its styles use only the shared tokens (docs/WEB-STYLE.md),
so it takes the look of the page it is mounted in. Tests: `web/test_web.mjs` (the logic: identities, the package
checks, the bar, the messages, the texts against OPTIMIST.md, README.md and the firmware) and `web/e2e_installer.mjs`
(headless Chrome, a simulated FM-1, `web/installer/mock_fm1.js`, behind `requestMIDIAccess`: connect, choose, install,
done, a pulled cable and the resume, no FM-1, a wrong file, the keyboard, light / dark, 390 px, no Web MIDI, mounted by a
host page; downloads refused, no real MIDI port opened). `node web/e2e_installer.mjs --shots DIR [--style FILE.css]`.
- `webapp/editor/`: the web editor.
- `webapp/builder/`: the web firmware builder (below): the builder of `make builder` run in the browser
  by Pyodide, its data exported fresh by `tools/export_builder_web.py`, `build-info.json` the commit for the cloud key.
  `python3 web/make_site.py --builder OUT` makes only this part.
- The installer and the builder share one look and one header (docs/WEB-STYLE.md): `web/style/optimist.css`, which
  `make_site.py` inlines into each page, so every page stays a standalone file.
- `firmware/optimist-<version>-<id>.fwsc`: one package per flavour; the installer checks the download against the
  SHA-256 written into its page.
- `icon.svg`, `favicon.ico`, `apple-touch-icon.png`, `icon-*.png`, `manifest.webmanifest`: the dinghy icon
  (`assets/logo/`; the PNGs are rendered by `tools/gen_web_icon.py` and committed).
- `LICENSE`, `LICENSING.md`, `LICENSES/Apache-2.0.txt`.

### The flavour manifest

`web/flavours.json` lists the firmware packages: `id` (lowercase, becomes part of the file name), `title` and
`description` (shown by the installer; with more than one entry it shows a choice), `profile` (the
`config/profiles/` profile CI builds) and `file` (a glob, relative to the repository, of the package that profile
produces: `dist/optimist-*-<profile>.fwsc`). An optional `"default": true` marks the flavour the web builder opens with (without one: the first). To add a flavour, add an entry. CI builds every listed profile and
publishes whatever the manifest lists. `python3 web/make_site.py --profiles` prints the profiles;
`python3 web/make_site.py --flavours web/flavours.json <version> <out>` builds the site from packages already in `dist/`.

### How the firmware gets there

`.github/workflows/pages.yml` runs on a push to `main` and by hand (Actions > pages > Run workflow); a published release
asks for a run on `main` (its `redeploy` job), since the github-pages environment deploys from `main` only. It downloads the JieLi toolchain and SDK files on the runner (`tools/optimist.py setup`, the same as
`build.yml`; nothing of them is stored in the repository or published), builds each listed profile, makes the site and
deploys it with `actions/upload-pages-artifact` and `actions/deploy-pages` (actions pinned to commit SHAs;
permissions: contents read, pages write, id-token write). Without that toolchain a CI build is not possible; the
fallback would be a site with the editor only and a link to the release's packages.

### Enable it (once)

1. Settings > Pages > Build and deployment > Source: **GitHub Actions**.
2. Settings > Environments > github-pages > Deployment branches and tags: nothing to add. The default branch (`main`) is
   allowed already, and the workflow deploys from it only (a run on a tag or another branch would be refused).
3. Push (or run the workflow by hand: Actions > pages > Run workflow). The URL is printed by the `deploy` job. If the
   `site` job fails in `configure-pages` with "Get Pages site failed", step 1 is missing.

### Cut a release

1. Set `VERSION` (X.Y, one digit each: it is the package identity, FM-1_7(X+2)Y) and commit it.
2. Tag that commit `v<VERSION>` (a suffix after a dash is allowed, `v0.2-rc1`) and push the tag: `release.yml` builds
   every profile in `config/profiles/` as a release build (`package --release`, files named
   `optimist-<version>-<profile>.fwsc`) and creates a **draft** release with them and `SHA256SUMS.txt`. A tag that does
   not match `VERSION` fails at once.
3. Read the draft and publish it: the release event asks for a Pages run on `main` (the site's `redeploy` job), and the
   installer lists the new release with its published flavours.

## The web builder

`webapp/builder/` on the Pages site (above) is the firmware builder of `make builder` in a browser: the same
items, groups, options, rules, size estimate and fit. It does not reimplement them. The page runs the builder's own
Python code (`tools/builder/core.py`, `registry.py`, `backports.py`, `room.py`) with
[Pyodide](https://pyodide.org) in a Web Worker. Glue code in `tools/builder/web_api.py` gives the page the menu's views
of that code.

### What it does

- **The list:** grouped as in the menu, with check boxes for on/off items and choice lists for sized items. An option
  sits under its parent and is greyed while the parent is off. An experimental item has an `experimental` tag, amber
  only while the item is on. An item with notes (how it was tested, its notice, where it comes from, what turning it
  off loses) has an (i) button that opens them in a popover.
- **What each item costs:** the cost chips use the menu's words (`flash +1.2K`), and a chip turns red when the item
  holds part of an overflow. `◀ off = fits` marks an item that alone would make everything fit. The sampled kits say
  they share the PERC samples. Errors from `validate()` show on the items they name (`✗ ...`) and in the messages, in
  plain words.
- **The layout:** two columns on a desktop. The item tree is on the left. On the right, a column of cards that stays in view
  while the list scrolls (`position: sticky`, with its own scroll when it is taller than the screen): first the
  **Size** card (the gauges), then **Your configuration** (download, hash and key, import, the canonical form), then the
  ways to fit and the errors and warnings (only when there are any), then the **Info** card of the selected item (its
  description, cost table, notes; the item's (i) text is the same). On a phone (900 px and narrower) the cards stack
  above the list and the Size card stays at the top of the screen, compact.
- **The gauges:** FLASH, RAM, POOL and RAMTEXT show used / limit in KiB with the exact bytes on hover, and, only when
  over, the overflow in red (`+12.3K`) with the limit marked on the bar; the card also holds the UNDO ring when the undo
  history is built, the configuration's key (a click copies it) and an (i) that says where the estimate comes from.
  This is the estimate: the same as `configure.budget(..., use_exact=False)`, taken from the nearest real build in
  `costs.json`. The size is only shown. Nothing on the page is refused because of it.
- **Make it fit:** the button exists only while some gauge is over (nothing, not a greyed button, when everything fits).
  It opens **Ways to fit**: up to four proposals from `core.proposals`, each a small card with a title, the exact
  changes (what is switched off or lowered, with the first sentence of the registry description of what you lose), the
  four gauges after it and whether it fits. **Accept** applies it (a toast offers **Undo**, which restores the
  configuration as it was), **Reject** dismisses the card, **Preview** (or hovering the card) shows the result on the
  gauges without applying. The panel and the button go as soon as the configuration fits. The ways are, in this order
  when they fit: the least loss (`core.fit`, the same as the CLI's), the single item whose removal alone fits (the two
  cheapest), the reserves lowered to 0 first and then the least loss (only when a reserve is set), keep every synth
  engine, keep every effect. An item is never dropped without what depends on it (a rule's error names the dependants,
  which go with it and are listed), and engines are never a dependant. When nothing fits, the page says so and shows
  the closest. All of them are deterministic; about 0.15 to 0.3 s in CPython on the everything-on configuration
  (measured 2026-10-16).
- **Messages:** the page shows only what the user must act on, one line each: in red an error (a rule that is not met,
  the undo ring too small) and an overflow (ONE line for every region over, with the biggest items and the legend of
  the list's red sizes and `◀ off = fits`); in amber one line per experimental item that is on. Everything else (the
  NOTICE texts: provenance, "tested in the emulator only", credits; the hints; what is not measured) is in the item's
  (i) and in a "Notes (n)" section, closed by default, each text once with the items it concerns.
- **BLE:** ticking BLE makes room the way the menu does (`room.py`), and a line on the Bluetooth row names what it
  removed and the alternatives.
- **Search, sort and folding:** search matches the label, key, group and description. Sort works like the menu's `o`
  (default order, then flash, ram, pool or ramtext, largest first). A section opens and closes by clicking its head (a
  chevron, Enter or Space when focused, `aria-expanded`); so does a parent item's row. The folded sections are
  remembered by the browser. Two small icons expand and collapse everything. Keys outside the text fields: `/` search,
  `o` sort, `x` expand all, `c` collapse all.
- **Presets:** the flavours (`web/flavours.json`), every shipped profile (`config/profiles/`), the registry defaults
  and, when the browser kept it, your last configuration. The page always opens with the manifest's default flavour:
  the entry marked `"default": true`, or the first one if none is marked. **Reset to default** goes back to it.
- **Import and export:** you can load a `.config` file or paste one. It is read with `core.parse`, and unknown keys
  are warned about as on the CLI. **Download .config** writes `core.dump`.
- **The configuration hash, the canonical form and the cloud key:** the key is `core.cloud_key`. It needs the full
  commit, which `web/make_site.py` writes to `build-info.json`. A site built from a modified tree has no commit, and the
  page then says the key will be computed at build time.
- **Build in the cloud:** offers the download of a build of the same cloud key made before, else opens the
  cloud-build issue form filled in with the site's commit and the `.config`; a workflow builds it and answers in the issue
  ([Cloud builds](#cloud-builds)). It is off while the configuration has errors. `build-info.json`'s `repo` names the repository
  (the site's workflow writes it; default `w0ts/optimist`).

The page needs no storage. It keeps the last configuration in `localStorage` when it can, offered as a preset, and works without it.
Light and dark follow the system. It works on a phone (16 px gutters, no sideways scroll). Its look is the shared web
style (docs/WEB-STYLE.md), put in by `web/make_site.py` with the header.

### The module

The builder is an ES module so the editor can show it as a tab. `web/builder/builder.js` exports one function:

```js
import { mountBuilder } from "../builder/builder.js";      // from webapp/editor/: the site ships webapp/builder/ next to it
const b = mountBuilder(container, { onNavigate, pyodideBase });
b.activate();      // the tab is shown
b.deactivate();    // the tab is hidden
b.destroy();       // gone
```

- `container`: an element the builder fills with its whole UI. It gets the classes `ob-root` and `o-ui`. Nothing outside it
  is touched: no `<body>` write, no global CSS (`builder.css` puts every rule under `.ob-root`; the module adds one
  `<link>` to it, once), and the ids inside do not collide with the editor's (`bpreset`, `bpop`, `bfile`
  and `bside`, where the plain word is an id of the editor).
- `opts.onNavigate(name)`: called, instead of following the link, when something inside the container with
  `data-nav="installer"` (or any tab name) is clicked. Optional.
- `opts.pyodideBase`: another Pyodide directory (tests); the default is the pinned CDN.
- `activate()`: attaches the keyboard shortcuts (`/`, `o`, `x`, `c`, outside text fields) to the document, and the first
  time loads the data and starts Pyodide. **Python is not loaded before the first `activate()`**, so a host that mounts
  the tab lazily pays nothing until the user opens it. `deactivate()` detaches the shortcuts; the state, the worker
  and the page stay, so coming back is instant. `destroy()` also ends the worker and empties the container.
- The handle also has `state` (the page's state), `ready` (a promise: Python started), `readyFlag`, `call(fn, ...args)`
  (the Python functions of `web_api.py`), `choosePreset(id)`, `importText(text, source)` and `setItem(key, value)`; the
  tests use them.
- The host document must include the shared style (`web/style/optimist.css`: the tokens and the `o-` components, which
  are the editor's own look) and have `--menubar-h` set on the root if it has a sticky bar (the right column sticks
  below it; the default is the shared header's height).
- Files: `builder.js`, `builder.css`, `logic.js`, `py-worker.js`, `data/`, `build-info.json`, all next to each other
  (`webapp/builder/`); the module finds them with `import.meta.url`. `index.html` + `ui.js` are the standalone page: a
  heading, `mountBuilder(#app)` and `activate()`; it sets `window.optimistBuilder` to the handle.

### Pyodide

Pyodide is pinned to version `314.0.7` (released 2026-09-14, Python 3.14) in `web/builder/logic.js`
(`PYODIDE_VERSION`) and loaded from `https://cdn.jsdelivr.net/pyodide/v314.0.7/full/`. Only this page loads it: the
installer and the editor do not. The first visit downloads about 13 MB (the wasm and the standard library), and the
browser caches it after that. No Pyodide packages are needed.

### The data

`tools/export_builder_web.py` writes `web/builder/data/`, which is committed:

- `python.json`: the Python files above, `tools/backports.json`, and the `needs_auto` list (read from the firmware
  headers, which the browser does not have);
- `registry.json`: `web_api.registry_view()`;
- `costs.json`;
- `profiles.json`: the shipped profiles and the flavours.

After any change to the builder (the registry, costs, core, backports or a profile), run
`python3 tools/export_builder_web.py` and commit the result. `node web/test_builder.mjs` (in `tests/run_tests.sh`)
fails while the data is stale. `make_site.py` exports fresh data into the site either way.

### Tests

- `node web/test_builder.mjs` needs no browser and no network:
  - the drift check;
  - the exported Python run alone by an isolated CPython (`python3 -I`, standing in for Pyodide), compared with the
    CLI builder (`tests/builder_web_ref.py`: `configure.py` and `room.py`) for every profile and seeded random edits.
    The comparison covers evaluate, budget, fits, key, savings, the errors per item, fit, the proposals and the BLE toggles;
  - `logic.js` against the menu's rules.
- `node web/e2e_builder.mjs [--shots DIR]` runs the same comparison in headless Chrome with the real Pyodide from the
  CDN, plus `tools/optimist.py cloud-key --json` on the page's own download, then the page itself: toggling, errors,
  search, sort, import, download, the notices policy (no NOTICE on the page with the default flavour, one line for an
  overflow, the (i) popover, the BLE line), the layout (the gauges card right of the list and above the info card at 1440 px, stacked with the gauges card
  sticky at 390 px), Make it fit (absent when it fits; proposals, preview, reject, accept, undo), folding by the section
  head, the downloads (into the test's own folder, none in `~/Downloads`, none before the click), light and dark, and a 390 px
  phone. `E2E_PORT` sets its server's first port (8800). It is not part of `make test`, because it needs
  Chrome and the network. `PYODIDE_DIR=path` serves a local copy of the same Pyodide release instead of the CDN (for
  example the npm package `pyodide@314.0.7`).

### Not yet

- The menu's local actions: flash, the emulator, saving and publishing profiles (building is the cloud build).
- Exact sizes before a build: the page shows the estimate; the cloud build's answer has the exact sizes.

## Cloud builds

Anyone with a GitHub account can build a firmware from the web builder (above) without installing
anything: **Build in the cloud** sends the configuration to this repository's GitHub Actions, which build it with the
same toolchain as CI and answer with the download.

### How a user does it

1. Open the builder (<https://w0ts.github.io/optimist/webapp/builder/>, or the editor's Build tab) and make a configuration.
2. **Build in the cloud**. If the same configuration (the same cloud key: the site's commit and the canonical form) was
   built before, the page shows its download at once. Otherwise it opens a GitHub issue, filled in: press
   **Submit new issue**.
3. A workflow answers in the issue: "Building …" with a link to the log, then, a few minutes later, the **build id**
   (the key's first 16 hex digits), an **Install it** link (the installer opened on that build), a link to its
   configuration in the builder, and the downloads `optimist-<name>-<key16>.fwsc` and its `-ui.zip`; it closes the issue. A refused configuration (an unknown key, a
   rule that is not met) is answered at once and closed; a failed build (it does not fit, most often: the page's size
   is an estimate) is answered with the log and stays open.
4. Install it: the issue's **Install it** link, or the web installer's *Choose* > *A cloud build*: the list of the
   builds kept (name, build id, date, commit, flash size), searched by a pasted build id or a name, each with
   *copy id* and *configuration* (the builder opened on it: `webapp/builder/?cloud=<stem>`). Or download the `.fwsc`
   and `python3 tools/optimist.py flash FILE`.

### The parts

| Part | What it does |
|---|---|
| `web/builder/builder.js`, `logic.js` | the button: the cached build (the public releases API, no login), else `cloudIssueUrl` (the form, with `commit` and `config` filled in) |
| `web/make_site.py` | `build-info.json`: `commit` (the site's, for the key) and `repo` (`$GITHUB_REPOSITORY`, so a fork's site asks its own repository) |
| `.github/ISSUE_TEMPLATE/cloud-build.yml` | the form: *Commit* (empty = the default branch's latest) and *Configuration*; label `cloud-build` |
| `.github/workflows/cloud-build.yml` | `request` → `build` → `publish`, `failed` on a failure |
| `tools/cloud_build.py` | reads the body, checks the commit, the verdict, the replies (tests: `tests/cloud_build_test.py`) |
| the `cloud-builds` pre-release | the builds, one `.fwsc` + `-ui.zip` per cloud key |
| `tools/cloud_shelf.sh` | the `cloud-builds` **branch**: the same files, `<stem>.config` and `index.json`; one commit, replaced (tests: `tests/cloud_build_test.py`, a bare repository) |
| `web/installer/installer.js` | *A cloud build*: `loadCloud` (the branch's commit from the API, then `index.json` and the `.fwsc` from raw.githubusercontent.com by that commit), `matchCloud`, the SHA-256 checked before install |

### The workflow

- **request** (read the repository, write issues): refuses an author's request past `MAX_PER_DAY` (5) in 24 hours, then runs `tools/cloud_build.py` from the default branch, so a request
  cannot change how it is read. The issue body reaches it through an environment variable only, never a shell line. The
  commit must be a full SHA on a branch of this repository (only maintainers push branches, so only published code is
  built). That commit's own builder checks the configuration (`optimist.py cloud-key --strict --json`: an unknown key or
  a broken rule refuses it) and gives the key; the asset stem is `optimist-<name slug>-<first 16 hex of the key>`. If
  the release already has that `.fwsc`, the build is skipped.
- **build** (read only, no token in the checkout): checks out that commit, downloads the toolchain (it only restores
  the cache `build.yml` saves, never saves it: `release.yml` and `pages.yml` read that key), `optimist.py
  package --config`, and uploads the two files as an artifact.
- **publish** (write the release and issues): attaches them to `cloud-builds` (created as a pre-release on first use,
  never marked latest; a copy already there from an identical request is kept, never overwritten), answers with the
  links and the package's SHA-256 and closes the issue.
  Then `tools/cloud_shelf.sh add` puts the build (or, answered from the cache, the release's copy, its date renewed)
  onto the `cloud-builds` branch. Why a branch: a release download ends on a host that sends no CORS header, so a web
  page cannot fetch it; raw.githubusercontent.com sends `Access-Control-Allow-Origin: *` (both measured 2026-10-18).
- **prune** (daily at 03:17 UTC, or Actions > cloud-build > Run workflow): `tools/cloud_shelf.sh prune`.
- **failed**: on a failed build or publish, answers with the run's link and leaves the issue open for a maintainer.

A configuration is only integers for registry keys (`core.parse` refuses anything else), so a request can switch
features on and off and nothing more. The checker's own message is quoted in a code block, stripped to plain
characters, so a request's text never becomes a link or a mention in the bot's answer. The token is only in the
steps that call `gh`, never next to code of the requested commit; every action is pinned to a commit SHA. Each issue has its own concurrency group: reopening an issue builds it again.

### Set up once (maintainer)

1. Settings > General > Features: **Issues** on (the form lives in the issue tracker).
2. Create the label: `gh label create cloud-build --description "a web builder cloud build" --color 0e8a16`.
3. Push the default branch with the workflow and the form, and let `pages.yml` deploy the site (the builder must come
   from a commit on GitHub, or its requests name a commit the workflow refuses).
4. Actions need no secret: the workflow uses `GITHUB_TOKEN` with the permissions listed per job.

### Housekeeping

Automatic: a build not asked for again in `RETAIN_DAYS` (30, the workflow's env) leaves the branch and the release
(the daily **prune**, and each **publish**). The branch is always one commit, replaced (force-pushed with a lease; a
run that loses the race starts again), so its history does not grow, and it is deleted when its last build expires.
The daily prune also deletes the release assets of no build on the branch (a day old or more: a build being
published uploads first) and, once the release holds nothing, the release and its tag. The jobs' hand-over
artifacts (`request`, `firmware`) are kept 1 day. So nothing is left behind but the toolchain cache the builds share.
A later request of an expired configuration simply builds it again. By hand: `gh release delete-asset cloud-builds NAME`
(the next prune leaves the branch's copy until it expires). A user can open as many requests as they like; if that
is abused, limit who can open issues (Settings > Moderation) or turn the workflow off (Actions > cloud-build > Disable).

## The simulator

Optimist's own firmware (`firmware/src/felucca.c`, the one compilation unit the FM-1 runs) built as WebAssembly for the
browser simulator (`web/sim/`). Not an emulator of the chip: the same C, compiled for the host, on a host HAL.

### What runs

Everything the firmware does above the registers: the boot (`fm1_main` up to its loop), the main loop (UI, editor SysEx,
autosave, storage), the TIMER5 tick (keys, USB poll, `fm1_ms`) and the audio interrupt (`core/audio.c`, every engine,
the drums, the effects). `firmware/sim/hal/fm1_*.h` replace the chip's headers of the same names (found first through
`-I`; the three that `src/` includes by a relative path, `fm1_usb.h`, `fm1_uart.h`, `fm1_xip.h`, through a clang VFS
overlay that `tools/sim.py` writes): same names, types and constants, no registers.

- **Time** is simulated: a 64-bit count of 24 MHz ticks (`hal/sim_core.h`). Code takes no simulated time; a timer read
  moves the clock one tick (a spin on the timer ends), `fm1_delay_*` moves it by the delay (the interrupts due on the way
  run), the watchdog feed 10 us (the firmware feeds it while it waits for an interrupt's work).
- **The scheduler** (`firmware/sim/sim.c`, `sim_run`) moves the clock from event to event: the 10 kHz TIMER5 tick, the
  audio DMA's switch every 256 frames at 44.1 kHz (the half just played goes to the output, the audio handler renders the
  next), and between them the main loop: a pass every 15 ms (`fm1_main_frame`), the turns of its wait
  (`fm1_main_idle_step`). Handlers run when the clock passes their event with the interrupts on; no nesting.
  `system/main.c` splits `fm1_main` with four macros under `FELUCCA_SIM`; the device's code is token for token as before
  (synth profile flash 580,352 bytes before and after, `text.bin` identical but for the build time).
- **Flash**: a 1 MiB RAM image at the FM-1's offsets (`hal/fm1_flash_map.h`), blank (0xFF) at start. The same range
  checks as the device's driver; an erase sets 0xFF, a program ANDs (NOR). A backup made on an FM-1 restores here and back.
- **Screen**: a model of the ST7789's column / row address and memory write: a 240 x 240 RGB565 buffer (native order),
  a dirty mask of 15 bands of 16 rows, a revision.
- **USB MIDI**: a model of the SIE registers `io/usb/usb.c` uses, the device configured at boot (`ep1_config`, as a
  SET_CONFIGURATION). usb.c's own EP1 paths carry the MIDI in and out: notes, clock, the editor's SysEx, backup and
  restore (`io/editor/ed_backup.c`).
- **Panel**: `fm1_in` as the matrix scan leaves it, fed by the page: presses and releases queued, taken one change per key
  a scan frame (~1.1 ms); knob steps added at once; the MASTER pot from the page; the battery reads full.

### Switched off

Whatever the configuration says (`tools/sim.py` `SIM_FORCE`): the second core (`DUAL`), Bluetooth (`BLE`,
`BT_INQ_PROBE`), the cache RAM, the key scan's DMA, USB audio and the USB serial console (`USB_MODE=0`); and SIMD, the
asm kernels (their C), the stock supply start-up, the bench. No TRS MIDI IN (the UART reads nothing), no CPU clock
measured (`cpu_khz` 0), no watchdog, no crash report from a CPU exception (a wasm trap is thrown to the page).

### API

`firmware/sim/sim.c`, exported by name (`export_name`); every integer is 32 bit. Pointers are offsets in the exported
`memory`, which never grows.

| Export | |
| --- | --- |
| `sim_flash()`, `sim_flash_size()`, `sim_flash_rev()` | the flash image (write a saved one before `sim_boot`), 1 MiB, erases + programs so far |
| `sim_boot()` | the start-up (the splash's delays just move the clock) |
| `sim_run(frames)` | advance by `frames` at 44.1 kHz; returns `sim_status()` |
| `sim_status()` | bit 0 reboot requested, bit 1 UBOOT / update requested, bit 2 fatal; set, the run has ended: start a fresh instance on `sim_flash()`'s bytes |
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

### Build and test

```sh
tools/builder/venv/bin/python tools/sim.py build --wasm [--config FILE] [--out build/sim/sim.wasm]
node tests/sim_test.mjs [build/sim/sim.wasm]
```

The venv's Python (the generators need Pillow) and `emcc` (Emscripten). The test boots, runs 3 s, checks the screen,
sound after a key, the editor's INFO, GET / SET / DESC, PROJECT save and load across a restart, BK_LIST, a backup of
the working project restored into a blank flash (and the restart it asks for), the UBOOT SysEx, and prints the wall time
a simulated second takes.

### The local site

`make web` serves the local simulator in its Simulator tab (`build/sim/web/optimist-sim.wasm`, the default profile or
`PROFILE=`): built with `make web SIM=1`, or when `make web` asks because there is none (a terminal only; no, no `emcc` or a
failed build: the site goes on without it). It asks again when the simulator is outdated: its fingerprint (the
firmware, `firmware/sim`, `firmware/hal`, the generators, the builder's rules and the configuration: `sim_fingerprint`)
differs from now, or it was built for another profile; kept otherwise, shown as "outdated" in the Simulator tab.
`tools/optimist.py web --sim [--profile P | --config F]`.

# The web builder

`webapp/builder/` on the Pages site (docs/PAGES.md) is the firmware builder of `make builder` in a browser: the same
items, groups, options, rules, size estimate and fit. It does not reimplement them. The page runs the builder's own
Python code (`tools/builder/core.py`, `registry.py`, `backports.py`, `room.py`) with
[Pyodide](https://pyodide.org) in a Web Worker. Glue code in `tools/builder/web_api.py` gives the page the menu's views
of that code.

## What it does

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
  (docs/CLOUD-BUILD.md). It is off while the configuration has errors. `build-info.json`'s `repo` names the repository
  (the site's workflow writes it; default `w0ts/optimist`).

The page needs no storage. It keeps the last configuration in `localStorage` when it can, offered as a preset, and works without it.
Light and dark follow the system. It works on a phone (16 px gutters, no sideways scroll). Its look is the shared web
style (docs/WEB-STYLE.md), put in by `web/make_site.py` with the header.

## The module

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

## Pyodide

Pyodide is pinned to version `314.0.7` (released 2026-09-14, Python 3.14) in `web/builder/logic.js`
(`PYODIDE_VERSION`) and loaded from `https://cdn.jsdelivr.net/pyodide/v314.0.7/full/`. Only this page loads it: the
installer and the editor do not. The first visit downloads about 13 MB (the wasm and the standard library), and the
browser caches it after that. No Pyodide packages are needed.

## The data

`tools/export_builder_web.py` writes `web/builder/data/`, which is committed:

- `python.json`: the Python files above, `tools/backports.json`, and the `needs_auto` list (read from the firmware
  headers, which the browser does not have);
- `registry.json`: `web_api.registry_view()`;
- `costs.json`;
- `profiles.json`: the shipped profiles and the flavours.

After any change to the builder (the registry, costs, core, backports or a profile), run
`python3 tools/export_builder_web.py` and commit the result. `node web/test_builder.mjs` (in `tests/run_tests.sh`)
fails while the data is stale. `make_site.py` exports fresh data into the site either way.

## Tests

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

## Not yet

- The menu's own actions: build, flash, the emulator, saving and publishing profiles.
- Exact sizes: there is no real build in a browser, so the page always shows the estimate.

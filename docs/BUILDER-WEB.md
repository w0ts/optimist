# The web builder

`webapp/builder/` on the Pages site (docs/PAGES.md) is the firmware builder of `make builder` in a browser: the same
items, groups, options, rules, size estimate and fit. It does not reimplement them. The page runs the builder's own
Python code (`tools/builder/core.py`, `registry.py`, `backports.py`, `room.py`) with
[Pyodide](https://pyodide.org) in a Web Worker. Glue code in `tools/builder/web_api.py` gives the page the menu's views
of that code.

## What it does

- **The list:** grouped as in the menu, with check boxes for on/off items and choice lists for sized items. An option
  sits under its parent and is greyed while the parent is off. EXPERIMENTAL is a red tag and NOTICE is an orange one
  (shown while the item is on).
- **What each item costs:** the cost chips use the menu's words (`flash +1.2K`), and a chip turns red when the item
  holds part of an overflow. `◀ off = fits` marks an item that alone would make everything fit. The sampled kits say
  they share the PERC samples. Errors from `validate()` show on the items they name (`✗ ...`) and in the messages, in
  plain words.
- **The bars:** FLASH, RAM, POOL and RAMTEXT show used / capacity and the bytes free, or `OVER by N B`, plus the UNDO
  ring when the undo history is built. This is the estimate: the same as `configure.budget(..., use_exact=False)`,
  taken from the nearest real build in `costs.json`. The size is only shown. Nothing on the page is refused because of
  it.
- **Make it fit:** proposes the changes `core.fit` would make (its fixed order of least loss). You choose whether to
  apply them.
- **BLE:** ticking BLE makes room the way the menu does (`room.py`), and the message names what it removed and the
  alternatives.
- **Search, sort and folding:** search matches the label, key, group and description. Sort works like the menu's `o`
  (default order, then flash, ram, pool or ramtext, largest first). Keys outside the text fields: `/` search, `o` sort,
  `x` expand all, `c` collapse all.
- **Presets:** the flavours (`web/flavours.json`), every shipped profile (`config/profiles/`), the registry defaults
  and, when the browser kept it, your last configuration. The page always opens with the manifest's default flavour:
  the entry marked `"default": true`, or the first one if none is marked. **Reset to default** goes back to it.
- **Import and export:** you can load a `.config` file or paste one. It is read with `core.parse`, and unknown keys
  are warned about as on the CLI. **Download .config** writes `core.dump`.
- **The configuration hash, the canonical form and the cloud key:** the key is `core.cloud_key`. It needs the full
  commit, which `web/make_site.py` writes to `build-info.json`. A site built from a modified tree has no commit, and the
  page then says the key will be computed at build time.
- **Build in the cloud:** this button is disabled for now ("Cloud builds are not live yet"). The cloud-build workflow
  is a later step.

The page needs no storage. It keeps the last configuration in `localStorage` when it can, offered as a preset, and works without it.
Light and dark follow the system. It works on a phone (16 px gutters, no sideways scroll).

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
    The comparison covers evaluate, budget, fits, key, savings, the errors per item, fit and the BLE toggles;
  - `logic.js` against the menu's rules.
- `node web/e2e_builder.mjs [--shots DIR]` runs the same comparison in headless Chrome with the real Pyodide from the
  CDN, plus `tools/optimist.py cloud-key --json` on the page's own download, then the page itself: toggling, errors,
  search, sort, import, download, light and dark, and a 390 px phone. It is not part of `make test`, because it needs
  Chrome and the network. `PYODIDE_DIR=path` serves a local copy of the same Pyodide release instead of the CDN (for
  example the npm package `pyodide@314.0.7`).

## Not yet

- Cloud builds (the issue-form workflow and the build cache: optimist-work `cloudbuild/DESIGN.md`).
- The menu's own actions: build, flash, the emulator, saving and publishing profiles.
- Exact sizes: there is no real build in a browser, so the page always shows the estimate.

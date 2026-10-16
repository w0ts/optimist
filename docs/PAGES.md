# The web site on GitHub Pages

URL once published: <https://w0ts.github.io/optimist/> (the root redirects to the installer; the editor is at
`webapp/editor/`, the builder at `webapp/builder/`). Every link in the site is relative, so it works under the `/optimist/` base path.

## What is published

`web/make_site.py` builds the site (`make web` builds the same thing locally into `build/optimist-site/`):

- `webapp/installer/`: the web installer (Chrome or Edge, Web MIDI), with the firmware package choice. Its page
  (`index.html`, from `web/installer/index.html`) is a thin wrapper: the shared header and style, and the installer
  module mounted in it, everything inlined (one offline file). Next to it, for the one-page app (the installer as a tab of
  the editor): `installer.js`, the same module as one file (`web/installer/installer.js` with `web/fm1ota.js`,
  `web/fm1pkg.js` and `web/installer/installer.css` put in), and `site.json`, the packages it installs.

## The installer module

`web/installer/installer.js` exports `mountInstaller(container, { packages, version, source, onNavigate, links, midi })`:
it renders the four steps (1 Connect: Find the FM-1, what it runs, what to do when it is not found; 2 Choose: the
manifest's flavours with their descriptions, a `.fwsc` of yours, a link to the builder; 3 Install: the back-up question,
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
- `webapp/builder/`: the web firmware builder (docs/BUILDER-WEB.md): the builder of `make builder` run in the browser
  by Pyodide, its data exported fresh by `tools/export_builder_web.py`, `build-info.json` the commit for the cloud key.
  `python3 web/make_site.py --builder OUT` makes only this part.
- The installer and the builder share one look and one header (docs/WEB-STYLE.md): `web/style/optimist.css`, which
  `make_site.py` inlines into each page, so every page stays a standalone file.
- `firmware/optimist-<version>-<id>.fwsc`: one package per flavour; the installer checks the download against the
  SHA-256 written into its page.
- `icon.svg`, `favicon.ico`, `apple-touch-icon.png`, `icon-*.png`, `manifest.webmanifest`: the dinghy icon
  (`assets/logo/`; the PNGs are rendered by `tools/gen_web_icon.py` and committed).
- `LICENSE`, `LICENSING.md`, `LICENSES/Apache-2.0.txt`.

## The flavour manifest

`web/flavours.json` lists the firmware packages: `id` (lowercase, becomes part of the file name), `title` and
`description` (shown by the installer; with more than one entry it shows a choice), `profile` (the
`config/profiles/` profile CI builds) and `file` (a glob, relative to the repository, of the package that profile
produces: `dist/optimist-*-<profile>.fwsc`). An optional `"default": true` marks the flavour the web builder opens with (without one: the first). To add a flavour, add an entry. CI builds every listed profile and
publishes whatever the manifest lists. `python3 web/make_site.py --profiles` prints the profiles;
`python3 web/make_site.py --flavours web/flavours.json <version> <out>` builds the site from packages already in `dist/`.

## How the firmware gets there

`.github/workflows/pages.yml` runs on a push to `main` or `optimist`, on a push of a `v*` tag, and by hand (Actions > pages > Run
workflow). It downloads the JieLi toolchain and SDK files on the runner (`tools/optimist.py setup`, the same as
`build.yml`; nothing of them is stored in the repository or published), builds each listed profile, makes the site and
deploys it with `actions/upload-pages-artifact` and `actions/deploy-pages` (actions pinned to commit SHAs;
permissions: contents read, pages write, id-token write). Without that toolchain a CI build is not possible; the
fallback would be a site with the editor only and a link to the release's packages.

## Enable it (once)

1. Settings > Pages > Build and deployment > Source: **GitHub Actions**.
2. Settings > Environments > github-pages > Deployment branches and tags: the default branch is allowed already; add the
   tag pattern `v*` (a tag run is refused otherwise) and, if you push from a branch that is not the repository default
   (the workflow runs on `main` and on `optimist`), add that branch too. Which branch is the default is shown in
   Settings > Branches; today the only branch on GitHub is `main`, and it is the default.
3. Push (or run the workflow by hand: Actions > pages > Run workflow). The URL is printed by the `deploy` job. If the
   `site` job fails in `configure-pages` with "Get Pages site failed", step 1 is missing.

## Cut a release

Tag the release and push the tag (`git tag v0.1 && git push origin v0.1`): `release.yml` attaches the packages of
every published profile to the release, and `pages.yml` republishes the site with the page's Source link pointing at
that tag. Bump `VERSION` first; the version is in the package file names and the installer's status line.

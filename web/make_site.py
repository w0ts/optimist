#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Builds the GitHub Pages site. What OUT_DIR receives:
  index.html                  a page that forwards to the installer
  firmware/optimist-VER-ID.fwsc  the development packages: one per flavour in web/flavours.json, built from this commit
  firmware/r/TAG/optimist-VER-PROFILE.fwsc  each published release's packages (--releases): its published flavours
  firmware/r/TAG/optimist-VER-PROFILE.wasm  their simulators, when pages.yml built them from the tag's source (the latest
                              release); site.json "latest": {tag, sha, date}, where the builder and the simulator start
  firmware/FM-1_015.fwsc      M-VAVE's official V15, unchanged (web/stock/, Apache-2.0): the installer's return to stock
  icon.svg, favicon.ico, apple-touch-icon.png, icon-*.png, manifest.webmanifest   the icon (assets/logo/)
  firmware/m-bootloader-X.Y.fwsc  m-bootloader (MIT, web/fm1base/m-bootloader/), which the installer offers after the firmware
  webapp/installer/index.html the standalone installer (web/installer/index.html): the installer module with fm-1-base's
                              fm1pkg.js and fm1ota.js (web/fm1base/), fm1ota_optimist.js and its CSS, the site's packages
                              (site.json) and the shared style inlined
  webapp/installer/installer.js  the same module as one file (mountInstaller), for the one-page app; site.json the packages
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt), with a link to the builder; beside it what its Install and Build
                              tabs import: style/optimist.css, fm1ota_optimist.js, fm1base/, installer/ (installer.js, .css, site.json), builder/
  webapp/builder/             the web builder (web/builder/; data/ exported fresh by tools/export_builder_web.py,
                              build-info.json: the commit for the cloud key, none when the tree is modified)
  LICENSE, LICENSING.md, LICENSES/*.txt            the notices the package needs
  src/                        left alone
Usage:
  web/make_site.py --flavours web/flavours.json X.Y OUT_DIR [--releases DIR]
                                                               the packages the manifest lists (each "file" is a glob,
                                                               relative to the repository; the newest match is used), and
                                                               the releases in DIR (DIR/TAG/release.json + its .fwsc files,
                                                               what pages.yml downloads): the installer picks a release,
                                                               then one of its flavours
  web/make_site.py --local X.Y OUT_DIR                         no firmware at all (make web): the installer offers cloud
                                                               builds and a .fwsc of yours
  web/make_site.py --profiles                                  the manifest's profiles, one per line (CI builds them)
  web/make_site.py --release-sims DIR                          "TAG SHA PROFILE" per package of the latest release in DIR
                                                               (CI builds their simulators from the tag's source)
  web/make_site.py --builder OUT_DIR                           only OUT_DIR/webapp/builder (no package needed)
  web/make_site.py --release-site X.Y OUT_DIR PKG              only the release's own site, OUT_DIR (fm1-base's make_site on
                                                               the app: install page with m-bootloader, manual, emulator; the
                                                               front page, name, tagline and repo are fm1base/optimist's, the
                                                               manual is built from docs/guide); PKG: the release build's package
  --release-pkg PKG                                            with --local or --flavours: also that, in OUT_DIR/release/
Each package names its own identity (Optimist FM-1_7xx, Felucca / SLOOP FM-1_9xx), read here from its bytes; once
installed, the device reports the same name.
"""
import base64, glob, hashlib, json, os, re, shutil, subprocess, sys   # (stdlib only: CI runs it with a bare Python)
import pathlib
Path = pathlib.Path

HERE = Path(os.path.realpath(__file__)).parent      # web/
ID_MARKS = 20            # the identity: one character hidden in each of the first 20 blocks of the package
ID_STRIDE = 0x30         # a block on disk: 0x2F image bytes, then the marker byte
ID_AT = 0x2F
ID_NONE = 0x7D           # a marker that carries no character


def as_plain_script(src):
    """an ES module's text as plain script: no import lines, no export keywords (to inline it in a page)"""
    without_imports = re.sub(r"^import .*?;\n", "", src, flags=re.M)
    return re.sub(r"^export\s+", "", without_imports, flags=re.M)


def package_identity(raw):
    """the identity a package carries, e.g. FM-1_906 (the same decoding as fm1pkg.js productOf)"""
    chars = []
    for k in range(ID_MARKS):
        mark = raw[k * ID_STRIDE + ID_AT]
        if mark != ID_NONE:
            chars.append(chr((mark - (k + 1)) % 256))
    return "".join(chars)


ICONS = ("icon.svg", "favicon.ico", "apple-touch-icon.png", "icon-32.png", "icon-192.png", "icon-512.png")
NOTICES = ("LICENSE", "LICENSING.md") + tuple(sorted("LICENSES/" + p.name for p in (HERE.parent / "LICENSES").glob("*.txt")))   # every distribution carries these


def source_ref():
    """the tag (or, without one, the commit) the package was built from, for the flasher's Source link:
    $OPTIMIST_SOURCE_REF (CI sets it to the release tag), else git's exact tag, else HEAD's hash, else ''"""
    ref = os.environ.get("OPTIMIST_SOURCE_REF", "").strip()
    for args in (["describe", "--tags", "--exact-match"], ["rev-parse", "HEAD"]):
        if ref:
            break
        try:
            p = subprocess.run(["git", "-C", str(HERE.parent), *args], capture_output=True, text=True)
        except OSError:
            continue
        ref = p.stdout.strip() if p.returncode == 0 else ""
    return ref if re.fullmatch(r"[A-Za-z0-9._-]+", ref) else ""


BUILDER_FILES = ("index.html", "app.html", "logic.js", "ui.js", "builder.js", "cloud.js", "builder.css", "py-worker.js")


INSTALLER_IMPORTS = re.compile(r'^import \{[^}]*\} from "\.\./(?:fm1base/)?(?:fm1ota|fm1pkg|fm1ota_optimist|install_route)\.js";\n', re.M)
# fm-1-base's three files (web/fm1base/, MPL-2.0, unchanged), then Optimist's extras: the order they are inlined in
LIB_FILES = ("fm1base/fm1pkg.js", "fm1base/fm1ports.js", "fm1base/fm1ota.js", "fm1ota_optimist.js", "install_route.js")
MBOOT_VERSION = "2.2"                              # web/fm1base/m-bootloader/fm1-uboot-X.Y.fwsc (MIT): the installer's extra choice
MBOOT_NAME = f"m-bootloader-{MBOOT_VERSION}.fwsc"
CSS_MARK = "const BUNDLED_CSS = null;"


STOCK_NAME = "FM-1_015.fwsc"                       # web/stock/: M-VAVE's FM-1 V15, unchanged (its FM1 Open kit, Apache-2.0)
STOCK_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"   # (web/fm1base/fm1pkg.js STOCK_FILES, tools/fm1_rescue.py)


def installer_module(inline):
    """web/installer/installer.js as one file: web/fm1base/*.js and web/fm1ota_optimist.js in place of its imports (their exports
    dropped), web/installer/installer.css in BUNDLED_CSS. inline=False: a module that still exports mountInstaller and
    its helpers (webapp/installer/installer.js, for the one-page app); inline=True: the page's own <script type=module>"""
    src = (HERE / "installer" / "installer.js").read_text(encoding="utf-8")
    if len(INSTALLER_IMPORTS.findall(src)) != 3 or src.count(CSS_MARK) != 1:
        raise SystemExit("web/installer/installer.js: its three imports or BUNDLED_CSS moved; update make_site.py")
    lib = "".join(as_plain_script((HERE / f).read_text(encoding="utf-8")) + "\n" for f in LIB_FILES)
    css = (HERE / "installer" / "installer.css").read_text(encoding="utf-8")
    src = INSTALLER_IMPORTS.sub("", src).replace(CSS_MARK, f"const BUNDLED_CSS = {json.dumps(css)};")
    if not inline:
        return lib + src
    return (lib + re.sub(r"^export\s+", "", src, flags=re.M)).replace("</", "<\\/")   # (an inline script holds no closing tag)


def logo_svg():
    """an Optimist logo, inline (assets/logo/optimist-logo.svg; none: the name alone)"""
    logo = HERE.parent / "assets" / "logo" / "optimist-logo.svg"
    return logo.read_text(encoding="utf-8") if logo.exists() else ""


STYLE_MARK = "/*OPTIMIST-CSS*/"                    # where a page takes web/style/optimist.css (docs/WEB-STYLE.md)
HEADER_MARK = "<!--OPTIMIST-HEADER-->"             # where a page takes the shared header
REPO = os.environ.get("GITHUB_REPOSITORY") or "w0ts/optimist"   # (the header's GitHub link; a fork's cloud build: its own)
APPS = (("builder", "Builder"), ("installer", "Installer"), ("editor", "Editor"))
GITHUB_ICON = ('<svg class="o-ic" width="16" height="16" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">'   # (the GitHub mark, MIT: primer/octicons mark-github)
               '<path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8z"/></svg>')
EDITOR_ICON = ('<svg class="o-ic" width="14" height="14" viewBox="0 0 14 14" fill="currentColor" aria-hidden="true">'
               '<rect x="2" y="1" width="1.5" height="12"/><rect x="6.2" y="1" width="1.5" height="12"/><rect x="10.5" y="1" width="1.5" height="12"/>'
               '<rect x="0.5" y="8" width="4.5" height="2.5"/><rect x="4.7" y="3" width="4.5" height="2.5"/><rect x="9" y="6" width="4.5" height="2.5"/></svg>')


APP_STORE = "https://apps.apple.com/app/id6826362906"   # (Optimist Mirror: one record, iPhone, iPad and Mac)
BADGES_MARK = "<!--APP-BADGES-->"                  # where the editor's menu bar takes the App Store badges


def app_badges():
    """Apple's badges, as Apple gives them (web/style/badges/), inlined: no request to Apple from the pages"""
    imgs = []
    for f, alt, w in (("app-store.svg", "Download on the App Store", 78), ("mac-app-store.svg", "Download on the Mac App Store", 102)):
        data = base64.b64encode((HERE / "style" / "badges" / f).read_bytes()).decode("ascii")
        imgs.append(f'<a href="{APP_STORE}" target="_blank" rel="noopener" style="padding:0;background:none;display:inline-flex">'
                    f'<img src="data:image/svg+xml;base64,{data}" alt="{alt}" height="26" width="{w}"></a>')
    return f'<span class="o-badges" style="display:inline-flex;gap:6px;align-items:center;flex:none;margin-left:auto">{"".join(imgs)}</span>'


def shared_css():
    """the shared tokens and components (web/style/optimist.css)"""
    css = (HERE / "style" / "optimist.css").read_text(encoding="utf-8")
    font = base64.b64encode((HERE / "style" / "terminus.ttf").read_bytes()).decode("ascii")   # (Terminus, inlined as the editor does)
    return css.replace('url("terminus.ttf") format("truetype")', f'url(data:font/ttf;base64,{font}) format("truetype")')


def shared_header(app):
    """the header of the three apps, the editor's menu bar: the dinghy and OPTIMIST (to the site's root), the apps as
    tabs with a separator between them (the current one aria-current)"""
    items = [(f"../{a}/", a, t) for a, t in APPS]
    items.insert(2, ("../editor/#sim", "simulator", "Simulator"))   # (a tab of the editor's page, no page of its own: web/sim/)
    links = '<span class="o-sep" role="separator" aria-orientation="vertical"></span>'.join(
        f'<a href="{href}"' + (' aria-current="page"' if a == app else "") + f">{(EDITOR_ICON + ' ' if a == 'editor' else '')}{t}</a>"
        for href, a, t in items)
    return (f'<header class="o-head"><a class="o-brand" href="../../" title="Optimist">{logo_svg()}<b>Optimist</b></a>'
            f'<nav class="o-nav" aria-label="Optimist apps"><span class="o-sep" role="separator" aria-orientation="vertical"></span>{links}{app_badges()}'
            f'<a class="o-gh" style="margin-left:8px" href="https://github.com/{REPO}" target="_blank" rel="noopener" title="Optimist on GitHub" aria-label="Optimist on GitHub">{GITHUB_ICON}</a></nav></header>')


def styled(html, app, page):
    """a page with the shared style and header put in (each mark exactly once, else the build stops)"""
    for mark in (STYLE_MARK, HEADER_MARK):
        if html.count(mark) != 1:
            raise SystemExit(f"{page} must contain {mark} once; update make_site.py")
    return html.replace(STYLE_MARK, shared_css()).replace(HEADER_MARK, shared_header(app))


def editor_page():
    """web/editor.html as it is: its Install and Build tabs load the modules from beside it (editor_modules)"""
    html = (HERE / "editor.html").read_text(encoding="utf-8")
    if html.count(BADGES_MARK) != 1:
        raise SystemExit(f"web/editor.html must contain {BADGES_MARK} once; update make_site.py")
    return html.replace(BADGES_MARK, app_badges())


def site_commit():
    """the full commit the site is made from, for the builder's cloud key: None when the tracked files differ from it
    (the key would name a source nobody can fetch) or there is no git"""
    def git(*args):
        try:
            p = subprocess.run(["git", "-C", str(HERE.parent), *args], capture_output=True, text=True)
        except OSError:
            return None
        return p.stdout.strip() if p.returncode == 0 else None
    sha, dirty = git("rev-parse", "HEAD"), git("status", "--porcelain", "--untracked-files=no")
    return sha if sha and re.fullmatch(r"[0-9a-f]{40}", sha) and dirty == "" else None


def builder_site(out, dst=None):
    """webapp/builder/: the web builder (web/builder/, docs/WEB.md), its data exported fresh from this source
    (tools/export_builder_web.py) and build-info.json (the commit, for the cloud key). dst: another directory (the
    editor's builder/, for its Build tab: the same files, no index.html page needed but harmless)"""
    dst = Path(dst) if dst else Path(out) / "webapp" / "builder"
    (dst / "data").mkdir(parents=True, exist_ok=True)
    shutil.copy(HERE / "TERMINUS-LICENSE.txt", dst / "TERMINUS-LICENSE.txt")      # (the style's font)
    for f in BUILDER_FILES:
        text = (HERE / "builder" / f).read_text(encoding="utf-8")
        if f == "index.html":
            text = styled(text, "builder", "web/builder/index.html")
        elif f == "app.html":                           # (the Mirror app's page: the style, no header; the app adds its look)
            if text.count(STYLE_MARK) != 1:
                raise SystemExit(f"web/builder/app.html must contain {STYLE_MARK} once; update make_site.py")
            text = text.replace(STYLE_MARK, shared_css())
        (dst / f).write_text(text, encoding="utf-8")
    sys.path.insert(0, str(HERE.parent / "tools"))
    import export_builder_web
    export_builder_web.write(dst / "data")
    commit = site_commit()
    info = dict({"commit": commit} if commit else {}, repo=os.environ.get("GITHUB_REPOSITORY") or "w0ts/optimist")
    (dst / "build-info.json").write_text(json.dumps(info) + "\n", encoding="utf-8")
    return dst


def editor_modules(ed, out, site):
    """what the editor's Install and Build tabs import, next to webapp/editor/index.html (editor.html loads ./style/optimist.css,
    ./installer/installer.js + installer.css + site.json and ./builder/builder.js on first use; installer.js imports ../fm1base/fm1pkg.js
    and ../fm1ota_optimist.js): the shared style and its font, fm1ota_optimist.js, fm1base/ (the two files it imports), installer/, builder/
    (with its data)"""
    (ed / "style").mkdir(parents=True, exist_ok=True)
    (ed / "installer").mkdir(parents=True, exist_ok=True)
    for f in ("optimist.css", "terminus.ttf"):
        shutil.copy(HERE / "style" / f, ed / "style" / f)
    (ed / "fm1base").mkdir(parents=True, exist_ok=True)
    for f in LIB_FILES:
        shutil.copy(HERE / f, ed / f)
    shutil.copy(HERE / "fm1base" / "README.md", ed / "fm1base" / "README.md")
    for f in ("installer.js", "installer.css"):
        shutil.copy(HERE / "installer" / f, ed / "installer" / f)
    (ed / "sim").mkdir(parents=True, exist_ok=True)
    for f in sorted(x.name for x in (HERE / "sim").iterdir() if x.suffix in (".js", ".css")):   # the Simulator tab (web/sim/: every
        # module it imports, bk.js too; docs/WEB.md)
        shutil.copy(HERE / "sim" / f, ed / "sim" / f)
    (ed / "installer" / "site.json").write_text(json.dumps(site, indent=1) + "\n", encoding="utf-8")
    builder_site(out, ed / "builder")


def flavours_of(manifest):
    """the manifest's entries as [{id, title, description, profile, pkg}], pkg the newest file matching its "file" glob"""
    out = []
    for f in json.loads(Path(manifest).read_text(encoding="utf-8"))["flavours"]:
        if not re.fullmatch(r"[a-z0-9-]+", f["id"]):
            raise SystemExit(f"{manifest}: flavour id {f['id']!r} must be lowercase letters, digits and -")
        hits = sorted(map(Path, glob.glob(str(HERE.parent / f["file"]))), key=lambda p: p.stat().st_mtime)
        if not hits:
            raise SystemExit(f"{manifest}: flavour {f['id']}: no package matches {f['file']} (build its profile first)")
        sims = sorted(map(Path, glob.glob(str(HERE.parent / f["sim"]))), key=lambda p: p.stat().st_mtime) if f.get("sim") else []
        out.append({**f, "pkg": hits[-1], "sim": sims[-1] if sims else None})   # (its simulator: tools/sim.py --wasm)
    return out


def published_profiles():
    """the profiles marked "# publish: yes" (config/profiles): the flavours a release offers on the site -> {id: its
    "# name:" (else the id)}"""
    out = {}
    for f in (HERE.parent / "config" / "profiles").glob("*.config"):
        text = f.read_text(encoding="utf-8")
        if re.search(r"^#\s*publish:\s*yes\b", text, re.M | re.I):
            m = re.search(r"^#\s*name:\s*(.+?)\s*$", text, re.M)
            out[f.stem] = m.group(1) if m else f.stem
    return out


# a release's packages left off the site: v0.1's user-default was a personal default, not a flavour (the user, 2026-10-25)
HIDDEN_RELEASE_PROFILES = {"user-default"}


SHA_RE = re.compile(r"[0-9a-f]{40}")
RELEASE_PKG = re.compile(r"optimist-[0-9][0-9A-Za-z.]*-([A-Za-z0-9_-]+)")   # (a profile id: mots-config)


def releases_of(folder, manifest=HERE / "flavours.json"):
    """DIR/TAG/release.json ({tag, date, sha}) + DIR/TAG/optimist-VER-PROFILE.fwsc (release.yml's names) -> the releases,
    newest first: [{tag, date, sha, flavours: [{id, title, description, pkg, sim}]}]: every package the release carries (a
    release is what it shipped: v0.1's profiles were renamed since, and today's "# publish" marks hid it, the user
    2026-10-25), titled as web/flavours.json titles a flavour of that profile, else by the profile's "# name:", else its
    id. sha: the tag's commit ("" when unknown); sim: DIR/TAG/optimist-sim-PROFILE.wasm when pages.yml built it from the
    tag's source (else None)"""
    listed = json.loads(Path(manifest).read_text(encoding="utf-8"))["flavours"]
    titled = {f["profile"]: f for f in listed}
    rank = {f["profile"]: k for k, f in enumerate(listed)}   # (the manifest's flavours first, in its order: the default first)
    published = published_profiles()
    out = []
    for d in sorted(Path(folder).iterdir()) if Path(folder).is_dir() else []:
        meta = d / "release.json"
        if not meta.is_file():
            continue
        info = json.loads(meta.read_text(encoding="utf-8"))
        tag, date = str(info.get("tag", "")), str(info.get("date", ""))
        sha = str(info.get("sha", ""))
        if not re.fullmatch(r"v[0-9A-Za-z._-]{1,32}", tag):
            raise SystemExit(f"{meta}: tag {tag!r} is not a release tag (v0.1, v0.2-rc1)")
        flavours = []
        for f in sorted(d.glob("optimist-*.fwsc")):
            m = RELEASE_PKG.fullmatch(f.stem)
            if not m or m.group(1) in HIDDEN_RELEASE_PROFILES:
                continue                                 # (not a release package, or one left off)
            t = titled.get(m.group(1), {})
            sim = d / f"optimist-sim-{m.group(1)}.wasm"
            flavours.append({"id": m.group(1), "title": t.get("title", published.get(m.group(1), m.group(1))),
                             "description": t.get("description", ""), "pkg": f, "sim": sim if sim.is_file() else None})
        if flavours:
            flavours.sort(key=lambda f: (rank.get(f["id"], len(rank)), f["id"]))
            out.append({"tag": tag, "date": date, "sha": sha if SHA_RE.fullmatch(sha) else "", "flavours": flavours})
    return sorted(out, key=lambda r: r["date"], reverse=True)


def latest_of(releases):
    """the latest published release (releases_of: newest first) as site.json's "latest", {tag, sha, date}: where the
    builder and the simulator start (the user, 2026-10-26: "by default it should pick the last release tag, not main");
    None when there is none or its commit is unknown (the pages then start from main, the site's own copy)"""
    if not releases or not SHA_RE.fullmatch(releases[0].get("sha") or ""):
        return None
    r = releases[0]
    return {"tag": r["tag"], "sha": r["sha"], "date": r["date"]}


def release_sims(folder):
    """what pages.yml builds from the latest release's own source: [(tag, sha, profile)] for each of its packages
    (releases_of: the ones the site lists); none when the latest release's commit is unknown"""
    rs = releases_of(folder)
    latest = latest_of(rs)
    return [(latest["tag"], latest["sha"], f["id"]) for f in rs[0]["flavours"]] if latest else []


# the identities a site's package may have: Optimist's own (FM-1_7NN), Felucca's (FM-1_9NN), and the release on fm1-base's
# (FM-1_79NN, FM-1_79NNNNNN: tools/flash.py OPTIMIST_ID; 2026-10-29 the 3-digit check refused the user's FM-1_7900 build)
SITE_ID = re.compile(r"FM-1_[79]\d\d|FM-1_79\d{2}(\d{4})?")


def mflasher_release():
    """the pinned M-Flasher release (tools/mflasher.py: the one source of truth) for the installer's download-mode help"""
    sys.path.insert(0, str(HERE.parent / "tools"))
    import mflasher
    return mflasher.site_data()


def package_entry(path, name, flavour, release="", date=""):
    """one package of the site: checked (an Optimist / Felucca identity, the update loader in it) -> its site.json entry"""
    raw = Path(path).read_bytes()
    product = package_identity(raw)
    if not SITE_ID.fullmatch(product):
        raise SystemExit(f"{path}: identity {product!r} is not an Optimist (FM-1_7xx, FM-1_79..) or Felucca (FM-1_9xx) "
                         "package")
    if raw.find(b"FELUCCA-LOADER-1") < 0:              # the update loader (firmware/loader) carries this tag
        raise SystemExit(f"{path}: no Felucca update loader in it")
    digest = hashlib.sha256(raw).hexdigest()
    entry = {"id": flavour["id"], "title": flavour["title"], "description": flavour.get("description", ""),
             "pkg": "../../firmware/" + name, "product": product, "release": release, "date": date,
             "sha256": digest}   # the page checks the download against it
    boot = install_bootloader(path, digest)
    return {**entry, "bootloader": boot} if boot else entry


def install_bootloader(path, digest):
    """the builder's BOOTLOADER from PACKAGE.install.json beside the package (tools/install_meta.py), when it is for this very
    package (its SHA-256); None otherwise (the page then shows: no build setting)"""
    side = Path(path).with_name(Path(path).stem + ".install.json")
    try:
        meta = json.loads(side.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    ok = isinstance(meta, dict) and meta.get("sha256") == digest and meta.get("bootloader") in ("keep", "m-bootloader")
    return meta["bootloader"] if ok else None


def main(version, out, flavours=None, releases=None, sims=None):
    """flavours: the development packages, flavours_of(manifest) (this commit's build); releases: releases_of(DIR); neither:
    a site with no firmware (the local one). sims: [{id, title, sim}] simulators with no package (the local one, make web):
    the Simulator tab lists them, the installer does not"""
    out = Path(out)
    flavours, releases, sims = flavours or [], releases or [], sims or []
    html = (HERE / "installer" / "index.html").read_text(encoding="utf-8")
    ver = re.sub(r"[^A-Za-z0-9.-]", "-", version)
    packages, files = [], []
    for r in releases:                                  # the releases first, newest first: the installer's default pick
        for fl in r["flavours"]:
            name = f"r/{r['tag']}/{Path(fl['pkg']).name}"
            packages.append(package_entry(fl["pkg"], name, fl, r["tag"], r["date"]))
            files.append((Path(fl["pkg"]), name))
            if fl.get("sim"):                           # its simulator, built from the tag's source (pages.yml)
                packages[-1]["sim"] = "../../firmware/" + name.replace(".fwsc", ".wasm")
                files.append((Path(fl["sim"]), name.replace(".fwsc", ".wasm")))
    for fl in flavours:
        name = f"optimist-{ver}-{fl['id']}.fwsc"
        packages.append(package_entry(fl["pkg"], name, fl))
        files.append((Path(fl["pkg"]), name))
        if fl.get("sim"):                               # the flavour's simulator (the Simulator tab: web/sim/sim.js)
            packages[-1]["sim"] = "../../firmware/" + name.replace(".fwsc", ".wasm")
            files.append((Path(fl["sim"]), name.replace(".fwsc", ".wasm")))
    stock = HERE / "stock" / STOCK_NAME              # M-VAVE's V15 (Apache-2.0): the installer's return to stock, no download
    stock_sha = hashlib.sha256(stock.read_bytes()).hexdigest()
    if stock_sha != STOCK_SHA256:
        raise SystemExit(f"{stock}: sha256 {stock_sha}, not M-VAVE's V15 ({STOCK_SHA256})")
    files.append((stock, STOCK_NAME))
    boot = HERE / "fm1base" / "m-bootloader" / f"fm1-uboot-{MBOOT_VERSION}.fwsc"   # m-bootloader (MIT): offered after the firmware
    files.append((boot, MBOOT_NAME))
    files.append((HERE / "fm1base" / "m-bootloader" / "LICENSE", "m-bootloader-LICENSE.txt"))
    sim_list = []
    for x in sims:                                      # (a simulator alone: no firmware to install from it)
        name = f"optimist-sim-{x['id']}.wasm"
        sim_list.append({"id": x["id"], "title": x["title"], "sim": "../../firmware/" + name})
        files.append((Path(x["sim"]), name))
    site = {"version": version, "source": source_ref(), "packages": packages, "sims": sim_list,
            "repo": os.environ.get("GITHUB_REPOSITORY") or "w0ts/optimist",     # (the cloud builds' list: installer.js)
            "stock": {"pkg": "../../firmware/" + STOCK_NAME, "sha256": stock_sha},
            "boot": {"pkg": "../../firmware/" + MBOOT_NAME, "version": MBOOT_VERSION, "sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}}
    site["mflasher"] = mflasher_release()
    latest = latest_of(releases)
    if latest:                                          # (the builder's and the simulator's default: builder.js, sim.js)
        site["latest"] = latest
    meta = json.dumps(site)
    missing = [m for m in ("/*INSTALLER*/", "/*META*/") if html.count(m) != 1]
    if missing:          # each placeholder exactly once, or the page and this script disagree
        raise SystemExit(f"web/installer/index.html must contain {missing[0]} once; update make_site.py")
    html = html.replace("/*INSTALLER*/", installer_module(inline=True)).replace("/*META*/", meta)
    html = styled(html, "installer", "web/installer/index.html")
    webapp, fw = out / "webapp", out / "firmware"
    inst, ed = webapp / "installer", webapp / "editor"
    for folder in (inst, ed, fw):
        folder.mkdir(parents=True, exist_ok=True)
    stale = ("felucca-*.fwsc", "sloop-*.fwsc", "optimist-*.fwsc", "optimist-*.wasm")    # only the current packages stay
    for old in [hit for pattern in stale for hit in fw.glob(pattern)]:
        os.remove(old)
    shutil.rmtree(fw / "r", ignore_errors=True)
    (inst / "index.html").write_text(html, encoding="utf-8")   # the standalone installer
    (inst / "installer.js").write_text(installer_module(inline=False), encoding="utf-8")   # for the one-page app
    (inst / "site.json").write_text(json.dumps(site, indent=1) + "\n", encoding="utf-8")
    for src, name in files:
        (fw / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(src, fw / name)
    (ed / "index.html").write_text(editor_page(), encoding="utf-8")
    builder_site(out)
    editor_modules(ed, out, site)
    shutil.copy(HERE / "TERMINUS-LICENSE.txt", inst / "TERMINUS-LICENSE.txt")      # (the style's font)
    fonts = ("fukiai.ttf", "FUKIAI-LICENSE.txt", "TERMINUS-LICENSE.txt", "JETBRAINSMONO-LICENSE.txt")
    for name in filter(lambda n: (HERE / n).exists(), fonts):     # the editor's font files, those present
        shutil.copy(HERE / name, ed / name)
    for f in ICONS:                                 # the icon: the site root, linked relatively by every page
        shutil.copy(HERE.parent / "assets" / "logo" / f, out / f)
    (out / "manifest.webmanifest").write_text(json.dumps({
        "name": "Optimist", "short_name": "Optimist", "start_url": "webapp/editor/", "display": "standalone",
        "background_color": "#141617", "theme_color": "#141617",
        "icons": [{"src": "icon-192.png", "sizes": "192x192", "type": "image/png"},
                  {"src": "icon-512.png", "sizes": "512x512", "type": "image/png"}]}, indent=2) + "\n", encoding="utf-8")
    for f in NOTICES:                               # the site root: ../../LICENSE etc. from the installer page
        dst = out / f
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(HERE.parent / f, dst)
    landing = "".join((
        '<!doctype html><meta charset="utf-8"><title>Optimist</title>',
        '<link rel="icon" href="icon.svg" type="image/svg+xml"><link rel="icon" href="favicon.ico" sizes="32x32">',
        '<link rel="apple-touch-icon" href="apple-touch-icon.png"><link rel="manifest" href="manifest.webmanifest">',
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">',          # straight on to the installer
        '<a href="webapp/installer/">Optimist installer</a>\n'))
    (out / "index.html").write_text(landing, encoding="utf-8")
    print(f"site: {out}: webapp/installer, webapp/editor, webapp/builder, " + ", ".join(f"firmware/{n}" for _, n in files))


def release_site(version, out, pkg, emu=True):
    """the release's own site (the release build: Optimist as an fm1-base app) into out: its front page, install page (the
    package, m-bootloader offered after it), manual, emulator and package (tools/fm1base.py site)"""
    sys.path.insert(0, str(HERE.parent / "tools"))
    import fm1base
    try:
        print(fm1base.site(pkg, version, out, emu=emu))
    except fm1base.FM1BaseError as e:
        raise SystemExit(f"release site: {e}")


if __name__ == "__main__":
    args = sys.argv[1:]
    release_pkg = None
    if "--release-pkg" in args:                 # (beside --local or --flavours: the release's site under OUT_DIR/release)
        k = args.index("--release-pkg")
        release_pkg = args[k + 1] if k + 1 < len(args) else sys.exit(__doc__)
        del args[k:k + 2]
    if args[:1] == ["--builder"] and len(args) == 2:
        Path(args[1]).mkdir(parents=True, exist_ok=True)
        for f in ICONS:                             # (the page's icon links: ../../icon.svg ...)
            shutil.copy(HERE.parent / "assets" / "logo" / f, Path(args[1]) / f)
        print(f"builder: {builder_site(args[1])}")
    elif args[:1] == ["--profiles"]:
        print("\n".join(f["profile"] for f in json.loads((HERE / "flavours.json").read_text(encoding="utf-8"))["flavours"]))
    elif args[:1] == ["--release-sims"] and len(args) == 2:
        print("\n".join(" ".join(x) for x in release_sims(args[1])))
    elif args[:1] == ["--flavours"] and len(args) in (4, 6) and (len(args) == 4 or args[4] == "--releases"):
        main(args[2], args[3], flavours_of(args[1]), releases_of(args[5], args[1]) if len(args) == 6 else None)
    elif args[:1] == ["--local"] and len(args) == 3:
        main(args[1], args[2])
    elif args[:1] == ["--release-site"] and len(args) == 4:
        release_site(args[1], args[2], args[3])
        release_pkg = None
    else:
        sys.exit(__doc__)          # a usage error: the docstring is the usage
    if release_pkg and args[:1] in (["--local"], ["--flavours"]):
        version = args[1] if args[0] == "--local" else args[2]
        release_site(version, str(Path(args[-1] if args[0] == "--local" else args[3]) / "release"), release_pkg)

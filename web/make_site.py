#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer
  firmware/optimist-VER[-ID].fwsc  the packages (one per flavour in web/flavours.json)
  icon.svg, favicon.ico, apple-touch-icon.png, icon-*.png, manifest.webmanifest   the icon (assets/logo/)
  webapp/installer/index.html the standalone installer (web/installer/index.html): the installer module with fm1pkg.js,
                              fm1ota.js and its CSS, the site's packages (site.json) and the shared style inlined
  webapp/installer/installer.js  the same module as one file (mountInstaller), for the one-page app; site.json the packages
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt), with a link to the builder
  webapp/builder/             the web builder (web/builder/; data/ exported fresh by tools/export_builder_web.py,
                              build-info.json: the commit for the cloud key, none when the tree is modified)
  LICENSE, LICENSING.md, LICENSES/*.txt            the notices the package needs
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR          one package
  web/make_site.py --flavours web/flavours.json X.Y OUT_DIR    the packages the manifest lists (each "file" is a glob,
                                                               relative to the repository; the newest match is used)
  web/make_site.py --profiles                                  the manifest's profiles, one per line (CI builds them)
  web/make_site.py --builder OUT_DIR                           only OUT_DIR/webapp/builder (no package needed)

The package identity (Optimist FM-1_7xx, Felucca / SLOOP FM-1_9xx) is read from the package; the device reports it
after the install.
"""
import glob
import hashlib
import json
import re
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


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


BUILDER_FILES = ("index.html", "logic.js", "ui.js", "py-worker.js")
EDITOR_INSTALLER_LINK = '<a class="dim inst" href="../installer/" data-t="installer"></a>'
EDITOR_BUILDER_LINK = '<a class="dim inst" href="../builder/">Firmware builder</a>'


INSTALLER_IMPORTS = re.compile(r'^import \{[^}]*\} from "\.\./(fm1ota|fm1pkg)\.js";\n', re.M)
CSS_MARK = "const BUNDLED_CSS = null;"


def installer_module(inline):
    """web/installer/installer.js as one file: web/fm1pkg.js and web/fm1ota.js in place of its imports (their exports
    dropped), web/installer/installer.css in BUNDLED_CSS. inline=False: a module that still exports mountInstaller and
    its helpers (webapp/installer/installer.js, for the one-page app); inline=True: the page's own <script type=module>"""
    src = (HERE / "installer" / "installer.js").read_text(encoding="utf-8")
    if len(INSTALLER_IMPORTS.findall(src)) != 2 or src.count(CSS_MARK) != 1:
        raise SystemExit("web/installer/installer.js: its two imports or BUNDLED_CSS moved; update make_site.py")
    lib = "".join(strip_module((HERE / f).read_text(encoding="utf-8")) + "\n" for f in ("fm1pkg.js", "fm1ota.js"))
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
APPS = (("installer", "Installer"), ("editor", "Editor"), ("builder", "Builder"))


def shared_css():
    """the shared tokens and components (web/style/optimist.css)"""
    return (HERE / "style" / "optimist.css").read_text(encoding="utf-8")


def shared_header(app):
    """the header of the three apps: the dinghy and the name (to the site's root), the app, the links between them
    (the current one aria-current), the four track colours under it"""
    links = "".join(f'<a href="../{a}/"' + (' aria-current="page"' if a == app else "") + f">{t}</a>" for a, t in APPS)
    return (f'<header class="o-head"><div class="o-head-in"><a class="o-brand" href="../../" title="Optimist">{logo_svg()}'
            f'<b>Optimist</b><span>{app}</span></a><nav class="o-nav" aria-label="Optimist apps">{links}</nav></div></header>'
            '<div class="o-tracks" aria-hidden="true"><i></i><i></i><i></i><i></i></div>')


def styled(html, app, page):
    """a page with the shared style and header put in (each mark exactly once, else the build stops)"""
    for mark in (STYLE_MARK, HEADER_MARK):
        if html.count(mark) != 1:
            raise SystemExit(f"{page} must contain {mark} once; update make_site.py")
    return html.replace(STYLE_MARK, shared_css()).replace(HEADER_MARK, shared_header(app))


def editor_page():
    """editor.html with a link to the builder after its installer link (the site only: web/editor.html is the editor's
    own file); said on stderr when the editor no longer has that link"""
    html = (HERE / "editor.html").read_text(encoding="utf-8")
    if html.count(EDITOR_INSTALLER_LINK) != 1:
        print("make_site: the editor's installer link moved: no builder link in the editor (update make_site.py)",
              file=sys.stderr)
        return html
    return html.replace(EDITOR_INSTALLER_LINK, EDITOR_INSTALLER_LINK + "\n    " + EDITOR_BUILDER_LINK)


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


def builder_site(out):
    """webapp/builder/: the web builder (web/builder/, docs/BUILDER-WEB.md), its data exported fresh from this source
    (tools/export_builder_web.py) and build-info.json (the commit, for the cloud key)"""
    dst = Path(out) / "webapp" / "builder"
    (dst / "data").mkdir(parents=True, exist_ok=True)
    for f in BUILDER_FILES:
        text = (HERE / "builder" / f).read_text(encoding="utf-8")
        if f == "index.html":
            text = styled(text, "builder", "web/builder/index.html")
        (dst / f).write_text(text, encoding="utf-8")
    sys.path.insert(0, str(HERE.parent / "tools"))
    import export_builder_web
    export_builder_web.write(dst / "data")
    commit = site_commit()
    (dst / "build-info.json").write_text(json.dumps({"commit": commit} if commit else {}) + "\n", encoding="utf-8")
    return dst


def flavours_of(manifest):
    """the manifest's entries as [{id, title, description, profile, pkg}], pkg the newest file matching its "file" glob"""
    out = []
    for f in json.loads(Path(manifest).read_text(encoding="utf-8"))["flavours"]:
        if not re.fullmatch(r"[a-z0-9-]+", f["id"]):
            raise SystemExit(f"{manifest}: flavour id {f['id']!r} must be lowercase letters, digits and -")
        hits = sorted(map(Path, glob.glob(str(HERE.parent / f["file"]))), key=lambda p: p.stat().st_mtime)
        if not hits:
            raise SystemExit(f"{manifest}: flavour {f['id']}: no package matches {f['file']} (build its profile first)")
        out.append({**f, "pkg": hits[-1]})
    return out


def main(pkg, version, out, flavours=None):
    """pkg: one package file (the single flavour "Optimist"), or None with flavours = flavours_of(manifest)"""
    out = Path(out)
    flavours = flavours or [{"id": "", "title": "Optimist", "description": "", "pkg": Path(pkg)}]
    html = (HERE / "installer" / "index.html").read_text(encoding="utf-8")
    ver = re.sub(r"[^A-Za-z0-9.-]", "-", version)
    packages, files = [], []
    for fl in flavours:
        raw = Path(fl["pkg"]).read_bytes()
        product = product_of(raw)
        if not re.fullmatch(r"FM-1_[79]\d\d", product):
            raise SystemExit(f"{fl['pkg']}: identity {product!r} is not an Optimist (FM-1_7xx) or Felucca (FM-1_9xx) package")
        if b"FELUCCA-LOADER-1" not in raw:              # marker of firmware/loader
            raise SystemExit(f"{fl['pkg']}: no Felucca update loader in it")
        name = f"optimist-{ver}{'-' + fl['id'] if len(flavours) > 1 else ''}.fwsc"
        packages.append({"id": fl["id"], "title": fl["title"], "description": fl.get("description", ""),
                         "pkg": "../../firmware/" + name, "product": product,
                         "sha256": hashlib.sha256(raw).hexdigest()})   # the page checks the download against it
        files.append((Path(fl["pkg"]), name))
    site = {"version": version, "source": source_ref(), "packages": packages}
    meta = json.dumps(site)
    for mark in ("/*INSTALLER*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"web/installer/index.html must contain {mark} once; update make_site.py")
    html = html.replace("/*INSTALLER*/", installer_module(inline=True)).replace("/*META*/", meta)
    html = styled(html, "installer", "web/installer/index.html")
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("optimist-*.fwsc")):   # only the current packages
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    (inst / "installer.js").write_text(installer_module(inline=False), encoding="utf-8")   # for the one-page app
    (inst / "site.json").write_text(json.dumps(site, indent=1) + "\n", encoding="utf-8")
    for src, name in files:
        shutil.copy(src, fw / name)
    (ed / "index.html").write_text(editor_page(), encoding="utf-8")
    builder_site(out)
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "TERMINUS-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
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
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Optimist</title>'
        '<link rel="icon" href="icon.svg" type="image/svg+xml"><link rel="icon" href="favicon.ico" sizes="32x32">'
        '<link rel="apple-touch-icon" href="apple-touch-icon.png"><link rel="manifest" href="manifest.webmanifest">'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">Optimist installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer, webapp/editor, webapp/builder, " + ", ".join(f"firmware/{n}" for _, n in files))


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--builder"] and len(args) == 2:
        Path(args[1]).mkdir(parents=True, exist_ok=True)
        for f in ICONS:                             # (the page's icon links: ../../icon.svg ...)
            shutil.copy(HERE.parent / "assets" / "logo" / f, Path(args[1]) / f)
        print(f"builder: {builder_site(args[1])}")
    elif args[:1] == ["--profiles"]:
        print("\n".join(f["profile"] for f in json.loads((HERE / "flavours.json").read_text(encoding="utf-8"))["flavours"]))
    elif args[:1] == ["--flavours"] and len(args) == 4:
        main(None, args[2], args[3], flavours_of(args[1]))
    elif len(args) == 3 and args[0] != "--flavours":
        main(*args)
    else:
        sys.exit(__doc__)

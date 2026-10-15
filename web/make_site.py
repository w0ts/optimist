#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer
  firmware/optimist-VER[-ID].fwsc  the packages (one per flavour in web/flavours.json)
  icon.svg, favicon.ico, apple-touch-icon.png, icon-*.png, manifest.webmanifest   the icon (assets/logo/)
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js and the metadata inlined
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  LICENSE, LICENSING.md, LICENSES/Apache-2.0.txt   the notices the package needs
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR          one package
  web/make_site.py --flavours web/flavours.json X.Y OUT_DIR    the packages the manifest lists (each "file" is a glob,
                                                               relative to the repository; the newest match is used)
  web/make_site.py --profiles                                  the manifest's profiles, one per line (CI builds them)

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
NOTICES = ("LICENSE", "LICENSING.md", "LICENSES/Apache-2.0.txt")   # every distribution carries these


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
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
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
    meta = json.dumps({"version": version, "packages": packages})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    ref = source_ref()
    if html.count("<!--SRCREF-->") != 1:
        raise SystemExit("index_pkg.html must contain <!--SRCREF--> once; update make_site.py")
    html = html.replace("<!--SRCREF-->", f"/tree/{ref}" if ref else "")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    logo = HERE.parent / "assets" / "logo" / "optimist-logo.svg"  # an Optimist logo, inline (none yet: the name)
    html = html.replace("<!--LOGO-->", logo.read_text(encoding="utf-8") if logo.exists() else "")
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    guide = HERE.parent / "output/pdf/Studio-0.3-guide-rapide-FR.pdf"
    guide_link = ""
    if guide.exists() and "--studio-guide" in sys.argv:   # LIVE: that guide describes the removed RYTHME tab
        shutil.copy(guide, out / guide.name)
        guide_link = '<p lang="fr"><a href="../../' + guide.name + '">Guide rapide illustré (PDF, 4 pages)</a></p>'
    html = html.replace("<!--STUDIO_GUIDE-->", guide_link)
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("optimist-*.fwsc")):   # only the current packages
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    for src, name in files:
        shutil.copy(src, fw / name)
    shutil.copy(HERE / "editor.html", ed / "index.html")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "TERMINUS-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    for f in ICONS:                                 # the icon: the site root, linked relatively by every page
        shutil.copy(HERE.parent / "assets" / "logo" / f, out / f)
    (out / "manifest.webmanifest").write_text(json.dumps({
        "name": "Optimist", "short_name": "Optimist", "start_url": "webapp/editor/", "display": "standalone",
        "background_color": "#16161a", "theme_color": "#16161a",
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
    print(f"site: {out}: webapp/installer, webapp/editor, " + ", ".join(f"firmware/{n}" for _, n in files))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--studio-guide"]
    if args[:1] == ["--profiles"]:
        print("\n".join(f["profile"] for f in json.loads((HERE / "flavours.json").read_text(encoding="utf-8"))["flavours"]))
    elif args[:1] == ["--flavours"] and len(args) == 4:
        main(None, args[2], args[3], flavours_of(args[1]))
    elif len(args) == 3 and args[0] != "--flavours":
        main(*args)
    else:
        sys.exit(__doc__)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                  redirect to the installer
  firmware/optimist-VER.fwsc  the package
  webapp/installer/index.html index_pkg.html with fm1pkg.js, fm1ota.js and the metadata inlined
  webapp/editor/index.html    editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  LICENSE, LICENSING.md, LICENSES/Apache-2.0.txt   the notices the package needs
  src/                        not touched

  web/make_site.py build/felucca-X.Y.fwsc X.Y OUT_DIR

The package identity (Optimist FM-1_7xx, Felucca / SLOOP FM-1_9xx) is read from the package; the device reports it
after the install.
"""
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


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_[79]\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not an Optimist (FM-1_7xx) or Felucca (FM-1_9xx) package")
    if b"FELUCCA-LOADER-1" not in raw:              # marker of firmware/loader
        raise SystemExit(f"{pkg}: no Felucca update loader in it")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"optimist-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest()})   # the page checks the download against it
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    ref = source_ref()
    if html.count("<!--SRCREF-->") != 1:
        raise SystemExit("index_pkg.html must contain <!--SRCREF--> once; update make_site.py")
    html = html.replace("<!--SRCREF-->", f"/tree/{ref}" if ref else "")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    logo = HERE.parent / "assets" / "logo" / "optimist-logo.svg"  # an Optimist logo, inline (none yet: the name)
    html = html.replace("<!--LOGO-->", logo.read_text(encoding="utf-8") if logo.exists() else "<b>Optimist</b>")
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    guide = HERE.parent / "output/pdf/Studio-0.3-guide-rapide-FR.pdf"
    guide_link = ""
    if guide.exists() and "--studio-guide" in sys.argv:   # LIVE: that guide describes the removed RYTHME tab
        shutil.copy(guide, out / guide.name)
        guide_link = '<p lang="fr"><a href="../../' + guide.name + '">Guide rapide illustré (PDF, 4 pages)</a></p>'
    html = html.replace("<!--STUDIO_GUIDE-->", guide_link)
    for old in list(fw.glob("felucca-*.fwsc")) + list(fw.glob("sloop-*.fwsc")) + list(fw.glob("optimist-*.fwsc")):   # one package: the current one
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    shutil.copy(HERE / "editor.html", ed / "index.html")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "TERMINUS-LICENSE.txt"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    for f in NOTICES:                               # the site root: ../../LICENSE etc. from the installer page
        dst = out / f
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(HERE.parent / f, dst)
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Optimist</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">Optimist installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--studio-guide"]
    if len(args) != 3:
        sys.exit(__doc__)
    main(*args)

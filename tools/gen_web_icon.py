#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the web icons in assets/logo/ from the SVG drawings (the SVGs are the sources; the PNGs and favicon.ico
are generated and committed so that `make web` needs no image tools):

  icon-tile.svg -> icon-32.png, apple-touch-icon.png (180), icon-192.png, icon-512.png, favicon.ico (16, 32, 48)

Needs rsvg-convert (librsvg) and ImageMagick (`magick`). Run: python3 tools/gen_web_icon.py
"""
import shutil
import subprocess
import sys
from pathlib import Path

LOGO = Path(__file__).resolve().parent.parent / "assets" / "logo"
PNGS = {"icon-32.png": 32, "apple-touch-icon.png": 180, "icon-192.png": 192, "icon-512.png": 512}


def main():
    for tool in ("rsvg-convert", "magick"):
        if not shutil.which(tool):
            sys.exit(f"gen_web_icon: {tool} is not installed")
    tile = LOGO / "icon-tile.svg"
    for name, px in PNGS.items():
        subprocess.run(["rsvg-convert", "-w", str(px), "-h", str(px), str(tile), "-o", str(LOGO / name)], check=True)
    subprocess.run(["magick", str(LOGO / "icon-512.png"), "-define", "icon:auto-resize=48,32,16",
                    str(LOGO / "favicon.ico")], check=True)
    print("wrote", ", ".join([*PNGS, "favicon.ico"]))


if __name__ == "__main__":
    main()

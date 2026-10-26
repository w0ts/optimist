# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip (the user, 2026-10-26: "build a SDcard artifact ... the user in the builder/flasher gets the zip and
installs it on their SD card ... all presets (user or factory) go on the card"; docs/SD-CARD-DESIGN.md 9): what a card
needs for SD card mode, unzipped at its root.

  OPTIMIST/                     the card's folders (sd_layout.c SDL_DIR: what the FM-1 would make at its first mount)
  OPTIMIST/PRESETS/FACTORY/<ENGINE>/<nnn> <NAME>.OPS   every factory preset of every engine (tools/sd_factory.c: built
                                from the firmware's own sources, each preset applied and taken as the device does);
                                SD card mode reads them here, it carries no preset tables (sd_mode.c sdm_factory_*)
  OPTIMIST/README.TXT           what this is

  python3 tools/sd_zip.py OUT.zip        (the host headers: tools/build.py --host-headers, made when missing)
Every engine and every FM6 pack is built in, whatever the profile: one zip serves every SD card mode build."""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
FW = ROOT / "firmware" / "src"
DIRS = ("PROJECTS", "SAMPLES", "KITS", "PRESETS", "RECORD", "RENDERS", "BACKUP", "SYSTEM")   # (sd_layout.c SDL_DIR)
README = """OPTIMIST: the SD card of an FM-1 in SD card mode (docs/SD-CARD-DESIGN.md).
Unzip at the card's root (FAT32). PRESETS/FACTORY holds every factory preset: SD card mode reads them here.
Your own presets, kits, projects and samples go in the folders beside it; the FM-1 makes what is missing.
"""


def engines():
    """-> the engines' names in registry.h ENGINE_LIST (X(uid, NAME, ...)), but the contributors' example"""
    text = (FW / "core" / "registry.h").read_text()
    return sorted({m for m in re.findall(r"X\(\d+, ([A-Z0-9_]+),", text)} - {"SQUARE"})


def build_gen(work):
    """the generator, on the host, every engine and FM6 pack in -> its path"""
    hgen = ROOT / "build" / "gen-host"
    if not (hgen / "felucca_tables.h").exists():
        subprocess.run([sys.executable, str(ROOT / "tools" / "build.py"), "--host-headers"], check=True, cwd=ROOT)
    exe = work / "sd_factory"
    flags = [f"-DFELUCCA_ENG_{e}=1" for e in engines()]
    flags += [f"-DFELUCCA_FM6_PK_{p}=1" for p in ("KEYS", "BASS", "BELLS", "PADS", "80S")]
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, "-O1", "-w", f"-I{hgen}", f"-I{FW}", "-DFELUCCA_FLASH=1", "-DUP_CARD_ON=1", *flags,
                    "-o", str(exe), str(ROOT / "tools" / "sd_factory.c"), "-lm"], check=True)
    return exe


def make(out_zip):
    work = ROOT / "build" / "sdzip"
    shutil.rmtree(work, ignore_errors=True)
    tree = work / "card"
    tree.mkdir(parents=True)
    exe = build_gen(work)
    res = subprocess.run([str(exe), str(tree)], check=True, capture_output=True, text=True)
    for d in DIRS:
        (tree / "OPTIMIST" / d).mkdir(parents=True, exist_ok=True)
    (tree / "OPTIMIST" / "README.TXT").write_text(README)
    out_zip = pathlib.Path(out_zip)
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(tree.rglob("*")):
            rel = p.relative_to(tree).as_posix()
            if p.is_dir():
                z.writestr(rel + "/", "")
            else:
                z.write(p, rel)
    total = res.stdout.strip().splitlines()[-1]
    print(f"sd zip: {out_zip} ({out_zip.stat().st_size:,} B, {total} presets)")
    return out_zip


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: sd_zip.py OUT.zip")
    make(sys.argv[1])

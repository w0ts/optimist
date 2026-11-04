# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip (the user, 2026-10-26: "build a SDcard artifact ... the user in the builder/flasher gets the zip and
installs it on their SD card ... all presets (user or factory) go on the card"; 2026-11-04: it holds everything, whatever
the builder's ticks, and every build with SDCARD makes it; docs/SD-CARD-DESIGN.md 8, 9): what a card needs for SD card
mode, unzipped at its root. Folders capitalised, files lowercase (the user, 2026-11-04):

  Optimist/                     the card's folders (sd_layout.c SDL_DIR: what the FM-1 makes at a mount)
  Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops   every factory preset of every engine, 16 a bank (A, B ..)
                                in the engine's table order (tools/sd_factory.c: built from the firmware's own sources,
                                each preset applied and taken as the device does; the engines' folders sd_names.h's);
                                SD card mode reads them here, it carries no preset tables (sd_mode.c sdm_factory_*)
  Optimist/Presets/User/        the presets saved on the FM-1 go to User/<Engine>/<L>/ (empty here)
  Optimist/Samples/<Set>/z<k>.wav   every sample set (PIANO's folder Piano ...; the user, 2026-10-29: in SD card mode a
                                set lives on the card, never in the flash): each zone of the set, in key order, 16-bit
                                mono PCM at 22.05 kHz, its root and loop in "smpl", its keys in "inst"
                                (tools/gen_samples.py card_files: the flash set's very samples, decoded); SAMPLE and its
                                presets play them from here through the streams (eng_sample.c smp_bank_on)
  Optimist/readme.txt           what this is

  python3 tools/sd_zip.py OUT.zip      (the host headers: tools/build.py --host-headers, made when missing)
Every engine, every FM6 pack and every set is in, whatever the profile: one zip serves every SD card mode build. It is
cached by its inputs' hash (build/sd-zip-cache/: the firmware's sources, the generator, the samples), so a build whose
inputs did not change copies the zip it made before (a fresh one takes about 3 s: measured 2026-11-04 on an M-series
Mac)."""
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
FW = ROOT / "firmware" / "src"
NAME = "optimist-sd-card.zip"
DIRS = ("Projects", "Samples", "Kits", "Presets", "Record", "Renders", "Backup", "System")   # (sd_layout.c SDL_DIR)
SUBDIRS = ("Presets/Factory", "Presets/User")                                               # (sd_layout.c sdl_prepare)
CACHE = ROOT / "build" / "sd-zip-cache"
README = """Optimist: the SD card of an FM-1 in SD card mode (docs/SD-CARD-DESIGN.md).
Unzip at the card's root (FAT32): this Optimist folder is the FM-1's.
  Presets/Factory/<Engine>/A, B ...   every factory preset, 16 a bank ("01 - name.ops" .. "16 - name.ops"):
                                      SD card mode reads them here. Keep the names: the number is the key it is on.
  Presets/User/<Engine>/A, B ...      the presets you save on the FM-1, in banks of 16 the same way
  Projects/A, B ...                   your projects, 16 a bank ("01 - name/01 - name.opp", earlier saves in Versions/)
  Samples/<Set>/z0.wav, z1.wav ...    the sample sets ({sets}): SAMPLE and its presets play them from the card (in SD card
                                      mode no set is in the flash). 16-bit mono, 22.05 kHz, the keys in the inst chunk,
                                      root and loop in smpl. Keep the names: the FM-1 finds a set by its folder. Your own
                                      samples go anywhere else under Samples/.
  Kits, Record, Renders, Backup, System   the FM-1's own; it makes whatever is missing.
On the FM-1 names show upper case; on the card the files are lowercase.
"""


def engines():
    """-> the engines' names in registry.h ENGINE_LIST (X(uid, NAME, ...)), but the contributors' example"""
    text = (FW / "core" / "registry.h").read_text()
    return sorted({m for m in re.findall(r"X\(\d+, ([A-Z0-9_]+),", text)} - {"SQUARE"})


def host_headers():
    """the host headers sd_factory.c compiles against (tools/build.py --host-headers), made when missing"""
    hgen = ROOT / "build" / "gen-host"
    if not (hgen / "felucca_tables.h").exists():
        subprocess.run([sys.executable, str(ROOT / "tools" / "build.py"), "--host-headers"], check=True, cwd=ROOT)
    return hgen


def build_gen(work):
    """the generator, on the host, every engine and FM6 pack in -> its path"""
    hgen = host_headers()
    exe = work / "sd_factory"
    flags = [f"-DFELUCCA_ENG_{e}=1" for e in engines()]
    flags += [f"-DFELUCCA_FM6_PK_{p}=1" for p in ("KEYS", "BASS", "BELLS", "PADS", "80S")]
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, "-O1", "-w", f"-I{hgen}", f"-I{FW}", "-DFELUCCA_FLASH=1", "-DUP_CARD_ON=1", *flags,
                    "-o", str(exe), str(ROOT / "tools" / "sd_factory.c"), "-lm"], check=True)
    return exe


def inputs():
    """-> the files the zip is made from (its cache key): the firmware's sources, the host headers, the generators, the
    sample sets' sources"""
    files = [ROOT / "tools" / "sd_zip.py", ROOT / "tools" / "sd_factory.c", ROOT / "tools" / "gen_samples.py",
             ROOT / "tests" / "hostsim.c"]
    for top, pats in ((FW, ("*.c", "*.h")), (host_headers(), ("*.h",)), (ROOT / "assets" / "samples-cc0", ("*",))):
        files += [p for pat in pats for p in top.rglob(pat) if p.is_file()]
    return sorted(set(files))


def key():
    """-> the inputs' SHA-256 (their paths and bytes), the cache's name"""
    h = hashlib.sha256()
    for p in inputs():
        h.update(p.relative_to(ROOT).as_posix().encode() + b"\0")
        h.update(p.read_bytes())
    return h.hexdigest()[:24]


def generate(out_zip):
    """the zip made afresh at out_zip -> (presets, {set: zone files})"""
    work = ROOT / "build" / "sdzip"
    shutil.rmtree(work, ignore_errors=True)
    tree = work / "card"
    tree.mkdir(parents=True)
    exe = build_gen(work)
    res = subprocess.run([str(exe), str(tree)], check=True, capture_output=True, text=True)
    for d in DIRS + SUBDIRS:
        (tree / "Optimist" / d).mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT / "tools"))
    import gen_samples
    made = gen_samples.card_files(tree, list(gen_samples.CARD_SETS))
    sets = ", ".join(gen_samples.card_set_dir(s) for s in made)
    (tree / "Optimist" / "readme.txt").write_text(README.format(sets=sets))
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(tree.rglob("*")):
            rel = p.relative_to(tree).as_posix()
            if p.is_dir():
                z.writestr(rel + "/", "")
            else:
                z.write(p, rel)
    shutil.rmtree(work, ignore_errors=True)
    return int(res.stdout.strip().splitlines()[-1].split()[-1]), made


def make(out_zip, echo=True):
    """the zip at out_zip (its folder made), from the cache when its inputs did not change -> its path"""
    out_zip = pathlib.Path(out_zip)
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    k = key()
    cached = CACHE / f"{k}.zip"
    if not cached.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        for old in CACHE.glob("*.zip"):              # (one kept: the inputs of before are gone)
            old.unlink()
        tmp = CACHE / f"{k}.part"
        presets, made = generate(tmp)
        tmp.replace(cached)
        how = f"{presets} presets, {len(made)} sample sets ({sum(len(v) for v in made.values())} zones)"
    else:
        how = "cached: its inputs unchanged"
    if cached.resolve() != out_zip.resolve():
        shutil.copyfile(cached, out_zip)
    if echo:
        print(f"sd zip: {out_zip} ({out_zip.stat().st_size:,} B, {how}): unzip it at the SD card's root")
    return out_zip


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: sd_zip.py OUT.zip")
    make(sys.argv[1])

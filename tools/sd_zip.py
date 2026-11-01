# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip (the user, 2026-10-26: "build a SDcard artifact ... the user in the builder/flasher gets the zip and
installs it on their SD card ... all presets (user or factory) go on the card"; docs/SD-CARD-DESIGN.md 9): what a card
needs for SD card mode, unzipped at its root.

  OPTIMIST/                     the card's folders (sd_layout.c SDL_DIR: what the FM-1 would make at its first mount)
  OPTIMIST/PRESETS/FACTORY/<ENGINE>/<nnn> <NAME>.OPS   every factory preset of every engine (tools/sd_factory.c: built
                                from the firmware's own sources, each preset applied and taken as the device does);
                                SD card mode reads them here, it carries no preset tables (sd_mode.c sdm_factory_*)
  OPTIMIST/SAMPLES/<SET>/Z<k>.WAV   the sample sets ticked in the builder (SET_PIANO ...; the user, 2026-10-29: in SD
                                card mode a set lives on the card, never in the flash): each zone of the set, in key
                                order, 16-bit mono PCM at 22.05 kHz, its root and loop in "smpl", its keys in "inst"
                                (tools/gen_samples.py --card: the flash set's very samples, decoded); SAMPLE and its
                                presets play them from here through the streams (eng_sample.c smp_bank_on). Only the
                                ticked sets: a set not ticked is not on the card (its presets silent, said once)
  OPTIMIST/README.TXT           what this is

  python3 tools/sd_zip.py OUT.zip [SET,SET,...]   (the host headers: tools/build.py --host-headers, made when missing)
Every engine and every FM6 pack is built in, whatever the profile: one zip's presets serve every SD card mode build;
its sets are the profile's ticks (tools/optimist.py package: core.card_set_names)."""
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
README_SETS = """SAMPLES/<SET> holds the sample sets ticked when this firmware was built ({sets}): SAMPLE and its presets
play them from the card (in SD card mode no set is in the flash). Each is Z0.WAV, Z1.WAV ... (16-bit mono, 22.05 kHz;
the keys in the inst chunk, root and loop in smpl): keep the names, the FM-1 finds a set by its folder. A set not here
plays silence, and the FM-1 says "<SET> NOT ON CARD" once.
"""
README_NO_SETS = """No sample set was ticked when this firmware was built: SAMPLE's PIANO, BASS ... presets play silence
(the FM-1 says "<SET> NOT ON CARD" once). Tick them in the builder for a zip that carries them.
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


def make(out_zip, sets=()):
    """the zip at out_zip; sets: the sample sets' names to carry (gen_samples.py CARD_SETS: the builder's ticks)"""
    work = ROOT / "build" / "sdzip"
    shutil.rmtree(work, ignore_errors=True)
    tree = work / "card"
    tree.mkdir(parents=True)
    exe = build_gen(work)
    res = subprocess.run([str(exe), str(tree)], check=True, capture_output=True, text=True)
    for d in DIRS:
        (tree / "OPTIMIST" / d).mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT / "tools"))
    import gen_samples
    made = gen_samples.card_files(tree, list(sets)) if sets else {}
    (tree / "OPTIMIST" / "README.TXT").write_text(README + (README_SETS.format(sets=", ".join(made)) if made
                                                            else README_NO_SETS))
    out_zip = pathlib.Path(out_zip)
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(tree.rglob("*")):
            rel = p.relative_to(tree).as_posix()
            if p.is_dir():
                z.writestr(rel + "/", "")
            else:
                z.write(p, rel)
    total = res.stdout.strip().splitlines()[-1]
    zones = sum(len(v) for v in made.values())
    print(f"sd zip: {out_zip} ({out_zip.stat().st_size:,} B, {total} presets, "
          f"{len(made)} sample sets ({zones} zones){': ' + ', '.join(made) if made else ''})")
    return out_zip


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: sd_zip.py OUT.zip [SET,SET,...]")
    make(sys.argv[1], [x for x in sys.argv[2].split(",") if x] if len(sys.argv) == 3 else ())

# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip (the user, 2026-10-26: "build a SDcard artifact ... the user in the builder/flasher gets the zip and
installs it on their SD card ... all presets (user or factory) go on the card"; 2026-11-04: every build with SDCARD
makes it, and "the samples we select in the builder just go on the SD card and not in flash, and that's it": the
samples and FM6's packs follow the builder's ticks, every factory preset and kit is in whatever they say;
docs/SD-CARD-DESIGN.md 8, 9): what a card needs for SD card mode, unzipped at its root. Folders capitalised, files
lowercase (the user, 2026-11-04):

  Optimist/                     the card's folders (sd_layout.c SDL_DIR: what the FM-1 makes at a mount)
  Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops   every factory preset of every engine, 16 a bank (A, B ..)
                                in the engine's table order, then every other sound flash mode lists as its preset (FM6:
                                the voices of its ticked packs, 16 a pack, each file carrying the voice itself; the user,
                                2026-11-04: "just presets", "same for preset (fm6)"); tools/sd_factory.c: built from the firmware's own
                                sources, each applied and taken as the device does; the engines' folders sd_names.h's;
                                SD card mode reads them here, it carries no preset tables (sd_mode.c sdm_factory_*)
  Optimist/Presets/User/        the presets saved on the FM-1 go to User/<Engine>/<L>/ (empty here)
  Optimist/Kits/Factory/<L>/<nn> - <name> (<instruments>).opk   every factory kit (drums.c: every kit UID, the sampled, the 32
                                synthesised, X0X's and their styles, KAWA, PLAITS), 16 a bank in the kit list's order,
                                each as a user kit is saved (drum_kits.c ukit_card_take: the lanes, their sources by
                                reference to the built-in drum sounds, the sends); Kits/User/<L>/ the user's (empty here)
  Optimist/Samples/...          the samples the builder ticked, at the highest resolution the repo holds (the user,
                                2026-11-04; tools/sd_samples.py: each from its source at the source's own rate, 16-bit
                                mono, root and loop in "smpl", keys in "inst"): <Set>/z<k>.wav a ticked melodic set
                                (PIANO's folder Piano ...; in SD card mode a set lives on the card, never in the flash),
                                Kits/Acoustic/<nn> - <name>.wav the sampled kits' sounds (a sampled kit ticked),
                                X0X/909/hh.wav the 909's hats (the 909 ticked), crash.wav ride.wav its cymbals (X909_CYM
                                on): the drum samples only with the card's streams (core.py card_drums), else they are
                                in the flash; Hiphop/<Sound>/ the hip-hop pack (for the USR slots: always, no tick)
  Optimist/readme.txt           what this is
  MBOOT/MENU.BIN                the SD flasher (firmware/stage2): m-bootloader's SD CARD item runs it (docs/SD-CARD-DESIGN.md 10)
  FIRMWARES/                    empty: the .fwsc packages it lists go here

  python3 tools/sd_zip.py OUT.zip [--config F | --profile P | --all]
                                (the configuration's ticks: --config, --profile, else build/builder.config, the builder
                                menu's; --all every set, kit sound, 909 sample and pack. The host headers:
                                tools/build.py --host-headers, made when missing)
What goes in is core.py card_content(cfg). It is cached by its inputs' hash and that content (build/sd-zip-cache/, the
build directory's: OPTIMIST_BUILD_DIR; the firmware's sources, the generator, the samples, the ticks), the last
CACHE_KEEP zips kept, so a build whose inputs did not change copies the zip it made before."""
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile
from dataclasses import dataclass

ROOT = pathlib.Path(__file__).resolve().parent.parent
FW = ROOT / "firmware" / "src"
NAME = "optimist-sd-card.zip"
STAGE2_REL = "MBOOT/MENU.BIN"                # (the card's root: m-bootloader's path, its docs/SPEC.md 11.5)
FIRMWARES = "FIRMWARES"                      # (firmware/stage2/s2_card.c: found by its short name)
DIRS = ("Projects", "Samples", "Kits", "Presets", "Record", "Renders", "Backup", "System")   # (sd_layout.c SDL_DIR)
SUBDIRS = ("Presets/Factory", "Presets/User", "Kits/Factory", "Kits/User")   # (sd_layout.c sdl_prepare)
BUILD = ROOT / (os.environ.get("OPTIMIST_BUILD_DIR") or "build")   # (tools/build.py's: a parallel build's own)
CACHE = BUILD / "sd-zip-cache"
CACHE_KEEP = 6                               # (zips kept: one a configuration's ticks, the oldest removed)
PACKS = ("KEYS", "BASS", "BELLS", "PADS", "80S")   # (FM6's voice packs, eng_fm6.c FM6_PACK's order; registry FM6_PK_*)
README_HEAD = """Optimist: the SD card of an FM-1 in SD card mode (docs/SD-CARD-DESIGN.md).
Unzip at the card's root (FAT32): this Optimist folder is the FM-1's.
  Presets/Factory/<Engine>/A, B ...   every factory preset, 16 a bank ("01 - name.ops" .. "16 - name.ops"; FM6's
                                      packs' voices after its own: {packs}):
                                      SD card mode reads them here. Keep the names: the number is the key it is on.
  Presets/User/<Engine>/A, B ...      the presets you save on the FM-1, in banks of 16 the same way
  Projects/A, B ...                   your projects, 16 a bank ("01 - name/01 - name.opp", earlier saves in Versions/)
"""
README_SETS = """  Samples/<Set>/z0.wav, z1.wav ...    the sample sets the build ticked ({sets}): SAMPLE and its presets play them
                                      from the card (in SD card mode no set is in the flash). 16-bit mono at the
                                      recordings' own rate (Piano and Flute 44.1 kHz, the others 22.05 kHz), the keys in
                                      the inst chunk, root and loop in smpl. Keep the names: the FM-1 finds a set by its
                                      folder. A set the build did not tick is not here: its presets say it is missing.
"""
README_KITS = """  Samples/Kits/Acoustic/              the sampled kits' sounds ("01 - kick.wav" ..), 44.1 kHz: ACOUSTIC, DEEP, TIGHT,
                                      BRIGHT and DUST play them from the card
"""
README_X909 = """  Samples/X0X/909/                    the X0X 909's {x909}, 44.1 kHz: the 909 plays them from the card
"""
README_TAIL = """  Samples/Hiphop/<Sound>/             the hip-hop pack's sounds (22.05 kHz), for the USR slots or a part's CARD sample
                                      The folders above are the factory's (make sdcard replaces them); your own samples
                                      go anywhere else under Samples/.
  Kits/Factory/A, B ...               every factory kit (the sampled, the synthesised, X0X, KAWA, PLAITS), 16 a bank
                                      ("01 - acoustic (sampled).opk" ..: its drum instruments in the parentheses, 909,
                                      808, synth, kawa, plaits, sampled), the kit list's order; a kit this firmware
                                      leaves out plays its stand-in, as a project naming it does; a kit whose samples
                                      are not on the card says so when loaded
  Kits/User/A, B ...                  the kits you save on the FM-1, in banks of 16 the same way
  Every preset and kit file carries an id: move or rename one (into User/ too) and a project still finds it.
  Record, Renders, Backup, System     the FM-1's own; it makes whatever is missing.
On the FM-1 names show upper case; on the card the files are lowercase.
"""


@dataclass(frozen=True)
class Content:
    """what the zip carries of the builder's ticks (core.py card_content): every factory preset and kit is in whatever
    they say"""
    sets: tuple = ()                         # the melodic sets (gen_samples.py CARD_SETS names: PIANO ..)
    kits: bool = False                       # the sampled kits' sounds (Samples/Kits/Acoustic)
    x909: tuple = ()                         # the 909's files (sd_samples.py X909_FILES: hh.wav, crash.wav, ride.wav)
    fm6_packs: tuple = ()                    # FM6's voice packs (PACKS: KEYS ..) as FM6 presets

    def key(self):
        return repr((sorted(self.sets), self.kits, sorted(self.x909), sorted(self.fm6_packs)))

    def line(self):
        """one line: what the card gets"""
        def names(v):
            return ", ".join(v) if v else "none"
        return (f"sets: {names([gen_set_dir(s) for s in self.sets])}; sampled kits' sounds: "
                f"{'yes' if self.kits else 'no'}; 909: {names(self.x909)}; FM6 packs: {names(self.fm6_packs)}")


def gen_set_dir(name):
    return name[:1].upper() + name[1:].lower()      # (gen_samples.py card_set_dir, without importing it here)


def _all():
    sys.path.insert(0, str(ROOT / "tools"))
    import gen_samples
    import sd_samples
    return Content(tuple(gen_samples.CARD_SETS), True, tuple(sd_samples.X909_FILES), PACKS)


def content_of(cfg):
    """a builder configuration -> its Content (core.py card_content)"""
    sys.path.insert(0, str(ROOT / "tools" / "builder"))
    import core
    c = core.card_content(cfg)
    return Content(tuple(c["sets"]), bool(c["kits"]), tuple(c["x909"]), tuple(c["fm6_packs"]))


def load_cfg(config=None, profile=None):
    """-> (cfg, name, where it came from): --config F, --profile P, else build/builder.config (the builder menu's)"""
    sys.path.insert(0, str(ROOT / "tools" / "builder"))
    import configure
    if profile:
        cfg, name = configure.load_profile(profile)
        return cfg, name, f"profile {profile}"
    path = pathlib.Path(config) if config else ROOT / "build" / "builder.config"
    if not path.is_file():
        raise SystemExit(f"sd zip: {path}: no such .config (make sd-zip CONFIG=file or PROFILE=name)")
    cfg, name = configure.load(str(path))
    return cfg, name or path.stem, str(path)


README_STAGE2 = """
Beside this folder, at the card's root:
  MBOOT/MENU.BIN                      the SD flasher: with m-bootloader's SD CARD item it lists FIRMWARES and writes the
                                      .fwsc you pick (make sdcard replaces it)
  FIRMWARES/                          your .fwsc packages (make sdcard leaves them alone)
"""


def readme(c):
    text = README_HEAD.format(packs=", ".join(c.fm6_packs) or "none ticked")
    if c.sets:
        text += README_SETS.format(sets=", ".join(gen_set_dir(s) for s in c.sets))
    if c.kits:
        text += README_KITS
    if c.x909:
        text += README_X909.format(x909=", ".join(c.x909))
    return text + README_TAIL + README_STAGE2




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


def build_gen(work, packs=PACKS):
    """the generator, on the host, every engine and the FM6 packs given in -> its path"""
    hgen = host_headers()
    exe = work / "sd_factory"
    flags = [f"-DFELUCCA_ENG_{e}=1" for e in engines()]
    flags += [f"-DFELUCCA_FM6_PK_{p}={int(p in packs)}" for p in PACKS]
    flags += ["-DFELUCCA_DRUM_X909=1", "-DFELUCCA_DRUM_X808=1"]   # (every kit built: X0X's off by default)
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, "-O1", "-w", f"-I{hgen}", f"-I{FW}", "-DFELUCCA_FLASH=1", "-DUP_CARD_ON=1", "-DUK_CARD_ON=1", *flags,
                    "-o", str(exe), str(ROOT / "tools" / "sd_factory.c"), "-lm"], check=True)
    return exe


def build_stage2(work):
    """-> MENU.BIN's bytes, built with the repository's pi32v2 toolchain from firmware/stage2 into work"""
    sys.path.insert(0, str(ROOT / "tools"))
    import toolchain
    llvm = pathlib.Path(toolchain.llvm_dir()) / "llvm"
    if not (llvm / "bin" / "clang").is_file():
        raise SystemExit(f"sd zip: MENU.BIN needs the pi32v2 toolchain, not found in {llvm}: make setup")
    if not (ROOT / "third_party" / "m-bootloader" / "src" / "core.c").is_file():
        raise SystemExit("sd zip: MENU.BIN needs third_party/m-bootloader: git submodule update --init --recursive")
    out = work / "stage2"
    subprocess.run(["make", "-s", "-C", str(ROOT / "firmware" / "stage2"), f"TOOLCHAIN={llvm}", f"OUT={out}"],
                   check=True, capture_output=True)
    return (out / "MENU.BIN").read_bytes()


def inputs():
    """-> the files the zip is made from (its cache key): the firmware's sources, the host headers, the generators, the
    sample sets' sources, MENU.BIN's"""
    files = [ROOT / "tools" / n for n in ("sd_zip.py", "sd_factory.c", "gen_samples.py", "sd_samples.py",
                                           "gen_x0x_drums.py", "sampleio.py", "stage2_pack.py", "toolchain.py")]
    files += [ROOT / "tests" / "hostsim.c", ROOT / "firmware" / "stage2" / "Makefile"]
    mboot = ROOT / "third_party" / "m-bootloader"
    for top, pats in ((ROOT / "firmware" / "stage2", ("*.c", "*.h")), (mboot / "src", ("*.c",)), (mboot / "include", ("*.h",))):
        files += [p for pat in pats for p in top.glob(pat) if p.is_file()]
    assets = ROOT / "assets"
    for top, pats in ((FW, ("*.c", "*.h")), (host_headers(), ("*.h",)), (assets / "samples-cc0", ("*",)),
                      (assets / "x0x909", ("*",)), (assets / "hiphop-pack", ("*",))):
        files += [p for pat in pats for p in top.rglob(pat) if p.is_file()]
    return sorted(set(files))


def key(c):
    """-> the inputs' SHA-256 (their paths and bytes) and the content c, the cache's name"""
    h = hashlib.sha256(c.key().encode() + b"\0")
    for p in inputs():
        h.update(p.relative_to(ROOT).as_posix().encode() + b"\0")
        h.update(p.read_bytes())
    return h.hexdigest()[:24]


def generate(out_zip, c=None):
    """the zip of content c (every tick when None) made afresh at out_zip -> (presets, kits, {set: zone files}, other
    sample files)"""
    c = _all() if c is None else c
    work = BUILD / f"sdzip-{os.getpid()}"                # (its own: two builds at once never share it)
    shutil.rmtree(work, ignore_errors=True)
    tree = work / "card"
    tree.mkdir(parents=True)
    try:
        exe = build_gen(work, c.fm6_packs)
        res = subprocess.run([str(exe), str(tree)], check=True, capture_output=True, text=True)
        for d in DIRS + SUBDIRS:
            (tree / "Optimist" / d).mkdir(parents=True, exist_ok=True)
        sys.path.insert(0, str(ROOT / "tools"))
        import sd_samples
        smp = sd_samples.write_all(tree, sets=c.sets, kits=c.kits, x909=c.x909)
        (tree / "Optimist" / "readme.txt").write_text(readme(c))
        menu = tree / STAGE2_REL
        menu.parent.mkdir()
        menu.write_bytes(build_stage2(work))
        (tree / FIRMWARES).mkdir()
        with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
            for p in sorted(tree.rglob("*")):
                rel = p.relative_to(tree).as_posix()
                if p.is_dir():
                    z.writestr(rel + "/", "")
                else:
                    z.write(p, rel)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    tally = dict(ln.split()[:2] for ln in res.stdout.splitlines() if ln.split()[:1] in (["total"], ["kits"]))
    return int(tally["total"]), int(tally["kits"]), smp["sets"], len(smp["files"])


def cached(c=None):
    """-> (the cache's zip of content c, how it was had): made when its inputs or c changed, the last CACHE_KEEP kept"""
    c = _all() if c is None else c
    k = key(c)
    path = CACHE / f"{k}.zip"
    if path.exists():
        os.utime(path)                                   # (used: the newest, kept longest)
        return path, "cached: its inputs unchanged"
    CACHE.mkdir(parents=True, exist_ok=True)
    tmp = CACHE / f"{k}.{os.getpid()}.part"
    presets, nkits, made, nother = generate(tmp, c)
    tmp.replace(path)
    for old in sorted(CACHE.glob("*.zip"), key=lambda q: q.stat().st_mtime, reverse=True)[CACHE_KEEP:]:
        old.unlink()
    return path, (f"{presets} presets, {nkits} kits, {len(made)} sample sets "
                  f"({sum(len(v) for v in made.values())} zones), {nother} kit, X0X and pack samples")


def make(out_zip, c=None, echo=True):
    """the zip of content c (Content; every tick when None) at out_zip (its folder made), from the cache when its
    inputs and c did not change -> its path"""
    out_zip = pathlib.Path(out_zip)
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    c = _all() if c is None else c
    path, how = cached(c)
    if path.resolve() != out_zip.resolve():
        shutil.copyfile(path, out_zip)
    if echo:
        print(f"sd zip: {out_zip} ({out_zip.stat().st_size:,} B, {how}; {c.line()}): unzip it at the SD card's root")
    return out_zip


def wav_bytes(path):
    """-> the bytes a source WAV takes on the card as 16-bit mono (its frames x 2; its header read only): what
    estimate() adds up"""
    d = pathlib.Path(path).read_bytes()
    i, ch, bits, n = 12, 1, 16, 0
    while i + 8 <= len(d):
        cid, sz = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
        if cid == b"fmt ":
            ch, bits = int.from_bytes(d[i + 10:i + 12], "little"), int.from_bytes(d[i + 22:i + 24], "little")
        elif cid == b"data":
            n = sz // max(1, ch * bits // 8)
        i += 8 + sz + (sz & 1)
    return 2 * n + 120


def estimate(c):
    """-> [(what, bytes at most)] the card's samples of content c, from the sources' headers (a set's zones trimmed at
    their onset are a little less): the info panel's list before the zip is made"""
    assets = ROOT / "assets"
    out = [(f"set {gen_set_dir(s)}", sum(wav_bytes(f) for f in sorted((assets / "samples-cc0" / s).glob("*.wav"))))
           for s in c.sets]
    if c.kits:
        sys.path.insert(0, str(ROOT / "tools"))
        import sd_samples
        out.append(("the sampled kits' sounds (Kits/Acoustic, 16)",
                    sum(wav_bytes(f) for f in sd_samples.kit_sources().values())))
    out += [(f"909 {n}", wav_bytes(assets / "x0x909" / n)) for n in c.x909]
    return out


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(prog="sd_zip.py", description="the SD card zip of a configuration's ticks")
    ap.add_argument("out", help="the zip to write")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--config", help="a .config (default build/builder.config: the builder menu's)")
    g.add_argument("--profile", help="a profile (config/profiles, config/my-profiles)")
    g.add_argument("--all", action="store_true", help="every set, kit sound, 909 sample and FM6 pack")
    a = ap.parse_args(argv)
    if a.all:
        c, where = _all(), "every tick (--all)"
    else:
        cfg, name, where = load_cfg(a.config, a.profile)
        c = content_of(cfg)
        where = f"{name} ({where})"
        sys.path.insert(0, str(ROOT / "tools" / "builder"))
        import core
        if not core.card_sets(cfg):
            print(f"sd zip: {where} is not SD card mode: its samples and packs are in its flash, none on the card")
    print(f"sd zip: the ticks of {where}")
    make(a.out, c)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

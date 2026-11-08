# SPDX-License-Identifier: GPL-3.0-only
"""`make sdcard`: prepare a mounted SD card for SD card mode (the user, 2026-11-04): every folder the FM-1 needs and the
factory content on it, user content never touched.

  python3 tools/sd_card.py [--card PATH] [--dry-run] [--yes] [--config F | --profile P]
                                (tools/optimist.py sdcard, make sdcard [CARD=..] [CONFIG=.. | PROFILE=..]; the builder
                                menu's c: the same steps on its screen, tools/builder/menu.py SdCard)

The content is the SD card zip's of the configuration (tools/sd_zip.py, from its cache: every factory preset and kit,
the samples and FM6 packs it ticks; --config, --profile, else build/builder.config, the builder menu's). The FACTORY trees are mirrored: a file there is added
or replaced when its bytes differ, a factory file the zip no longer has is removed, only inside those trees (FACTORY:
Optimist/Presets/Factory, Optimist/Kits/Factory, the factory sample folders under Optimist/Samples:
tools/sd_samples.py factory_dirs), Optimist/readme.txt and MBOOT/MENU.BIN. Everything else is the user's and is never written or
removed (Presets/User, Kits/User, Projects, Record, Renders, Backup, System, FIRMWARES, anything else under Samples or elsewhere);
the FM-1's folders are only made when missing. Files are written plainly (no ._ AppleDouble twins: one the OS makes for a
file written here is removed), the card flushed at the end (os.sync).

With no --card it asks (tools/sd_card_volumes.py pick: the mounted removable FAT volumes, the last ones used first, or
another folder); with no terminal it refuses. Then it shows the plan in one line and asks "Go? [y/N]" (--yes, or no
terminal with --card: no confirmation).
--card must be a mounted FAT volume's root (never /, the home or the repo); a typed folder only warns. The destination
is remembered after a write that succeeded (build/sdcard-last.json).

Your sample library (the user, 2026-11-08: "copy everything to the currently plugged sd card"): --samples LIB, a library
sorted by type by tools/sd_sort_samples.py sort (LIB/<Type>/<name>.wav), goes to Optimist/Samples/<Type>/: a file added
when missing, replaced when its size differs, never removed (it is user content: a sample deleted from the library stays
on the card). LIB is remembered (build/sdcard-samples.txt), so a later `make sdcard` copies it again; --no-samples skips
it. A type folder named as a factory sample folder (Bass, Piano ..) is refused: make sdcard mirrors those."""
import argparse
import os
import shutil
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sd_card_volumes as V  # noqa: E402  (tools/: on the path above)
import sd_samples  # noqa: E402
import sd_zip  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TOP = "Optimist"
FACTORY = [f"{TOP}/Presets/Factory", f"{TOP}/Kits/Factory"] + [f"{TOP}/Samples/{d}" for d in sd_samples.factory_dirs()]
FACTORY_FILES = [f"{TOP}/readme.txt", sd_zip.STAGE2_REL]
USER_DIRS = [f"{TOP}/{d}" for d in sd_zip.DIRS + sd_zip.SUBDIRS] + [sd_zip.FIRMWARES]
SAMPLES_FILE = ROOT / "build" / "sdcard-samples.txt"   # (the library last copied)


@dataclass
class Plan:
    add: list = field(default_factory=list)          # (rel) of the zip's files not on the card
    replace: list = field(default_factory=list)      # (rel) whose bytes differ
    remove: list = field(default_factory=list)       # (rel) factory files the zip no longer has
    mkdirs: list = field(default_factory=list)       # (rel) folders made
    unchanged: int = 0


def zip_entries(zpath):
    """-> {path in the zip: bytes} of its files"""
    with zipfile.ZipFile(zpath) as z:
        return {n: z.read(n) for n in z.namelist() if not n.endswith("/")}


def is_factory(rel):
    """a path under the card's root inside a factory tree (or a factory file)"""
    return rel in FACTORY_FILES or any(rel == t or rel.startswith(t + "/") for t in FACTORY)


def make_plan(root, entries):
    """what a sync of entries onto root would do"""
    root = Path(root)
    plan = Plan()
    bad = [n for n in entries if not is_factory(n)]
    if bad:
        raise SystemExit(f"sd_card: the zip holds files outside the factory trees, never written: {bad[:3]}")
    for rel, data in sorted(entries.items()):
        p = root / rel
        if not p.is_file():
            plan.add.append(rel)
        elif p.stat().st_size != len(data) or p.read_bytes() != data:
            plan.replace.append(rel)
        else:
            plan.unchanged += 1
    for t in FACTORY:
        for p in sorted((root / t).rglob("*")) if (root / t).is_dir() else []:
            rel = p.relative_to(root).as_posix()
            if p.is_file() and not p.name.startswith(".") and rel not in entries:
                plan.remove.append(rel)
    dirs = set(USER_DIRS) | {str(Path(n).parent.as_posix()) for n in entries}   # (a factory folder the zip has no
                                                                                # file in is not made: a set not ticked)
    plan.mkdirs = sorted(d for d in dirs if not (root / d).is_dir())
    return plan


def _drop_twin(p):
    """an AppleDouble twin (._name) the OS made beside a file written here: removed"""
    t = p.with_name("._" + p.name)
    if t.is_file():
        t.unlink()


def apply(root, plan, entries):
    """the plan carried out -> bytes written"""
    root = Path(root)
    written = 0
    for d in plan.mkdirs:
        (root / d).mkdir(parents=True, exist_ok=True)
    for rel in plan.add + plan.replace:
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "wb") as f:
            f.write(entries[rel])
        written += len(entries[rel])
        _drop_twin(p)
    for rel in plan.remove:
        p = root / rel
        p.unlink()
        _drop_twin(p)
    for t in FACTORY:                                # (folders a removal left empty, inside the trees only, and a
        base = root / t                              # factory sample folder itself: a set no longer ticked)
        inner = sorted((x for x in base.rglob("*") if x.is_dir()), key=lambda x: len(x.parts), reverse=True) \
            if base.is_dir() else []
        for d in inner + ([base] if base.is_dir() and t not in USER_DIRS else []):
            if not any(d.iterdir()):
                d.rmdir()
    if hasattr(os, "sync"):
        os.sync()
    return written


def lib_plan(root, lib):
    """the library's copy -> ([(source, rel on the card)] to add, [..] to replace, unchanged count, bytes)"""
    lib = Path(lib)
    factory = {d.lower() for d in sd_samples.factory_dirs()}
    add, rep, same, size = [], [], 0, 0
    for t in sorted(d for d in lib.iterdir() if d.is_dir() and not d.name.startswith(".")):
        if t.name.lower() in factory:
            raise SystemExit(f"sd_card: {t} is named as a factory sample folder (mirrored by make sdcard): rename it")
        for f in sorted(x for x in t.iterdir() if x.is_file() and x.suffix.lower() == ".wav" and not x.name.startswith(".")):
            rel = f"{TOP}/Samples/{t.name}/{f.name}"
            p = Path(root) / rel
            n = f.stat().st_size
            if not p.is_file():
                add.append((f, rel))
            elif p.stat().st_size != n:
                rep.append((f, rel))
            else:
                same += 1
                continue
            size += n
    return add, rep, same, size


def lib_apply(root, todo, say):
    """the library's files written plainly (no ._ twin), with progress -> bytes"""
    written = 0
    for i, (src, rel) in enumerate(todo, 1):
        p = Path(root) / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_name(p.name + ".part")
        with open(src, "rb") as fi, open(tmp, "wb") as fo:
            shutil.copyfileobj(fi, fo, 1 << 20)
        os.replace(tmp, p)
        _drop_twin(p)
        written += src.stat().st_size
        if i % 250 == 0 or i == len(todo):
            say(f"  samples: {i} / {len(todo)}")
    return written


def lib_choose(args, say):
    """-> the library to copy, or None: --samples (remembered), else the one remembered, unless --no-samples"""
    if args.no_samples:
        return None
    lib = args.samples or (SAMPLES_FILE.read_text().strip() if SAMPLES_FILE.is_file() else "")
    if not lib:
        return None
    lib = os.path.expanduser(lib)
    if not os.path.isdir(lib):
        say(f"sdcard: the sample library {lib} is not there: samples skipped")
        return None
    return lib


def summary_line(name, plan):
    return (f"card {name}: {len(plan.add)} files to add, {len(plan.replace)} to replace, {len(plan.remove)} factory "
            f"files to remove; user content kept")


def content(zpath=None, c=None):
    """-> the entries of the zip at zpath, else of the cache's zip of content c (sd_zip.Content; made when its inputs or
    c changed)"""
    return zip_entries(Path(zpath) if zpath else sd_zip.cached(c)[0])


def choose(args, ask, say, tty, volumes, last_file):
    """-> (destination, its volume or None) or None: --card checked, else asked"""
    if args.card:
        ok, msg = V.check_dest(args.card, volumes)
        say(msg)
        return (args.card, V.volume_at(args.card, volumes)) if ok else None
    if not tty:
        say("sdcard: no terminal to ask in: name the card (make sdcard CARD=/Volumes/<name>, or --card PATH)")
        return None
    dest = V.pick(V.candidates(volumes), V.load_last(last_file), ask, say)
    if not dest:
        say("cancelled: nothing written")
        return None
    ok, msg = V.check_dest(dest, volumes, folder_ok=True)
    say(msg)
    return (dest, V.volume_at(dest, volumes)) if ok else None


def run(argv=None, ask=input, say=print, tty=None, volumes=None, last_file=V.LAST_FILE, zpath=None):
    """the command -> its exit status (0 done or nothing to do, 1 refused or cancelled)"""
    ap = argparse.ArgumentParser(prog="sdcard", description="prepare a mounted SD card for SD card mode")
    ap.add_argument("--card", help="the card's root (a mounted FAT volume, e.g. /Volumes/OPTIMIST): no questions")
    ap.add_argument("--dry-run", action="store_true", help="list what it would write and remove, write nothing")
    ap.add_argument("--yes", action="store_true", help="no confirmation")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--config", help="the configuration whose ticks the card gets (default build/builder.config)")
    g.add_argument("--profile", help="a profile instead")
    s = ap.add_mutually_exclusive_group()
    s.add_argument("--samples", help="your library sorted by type (tools/sd_sort_samples.py sort): to Optimist/Samples/, "
                                     "remembered for the next time")
    s.add_argument("--no-samples", action="store_true", help="the remembered library not copied this time")
    args = ap.parse_args(argv)
    c = None
    if not zpath:
        cfg, name, where = sd_zip.load_cfg(args.config, args.profile)
        c = sd_zip.content_of(cfg)
        say(f"the SD card of {name} ({where}): every factory preset and kit; {c.line()}")
    tty = sys.stdin.isatty() and sys.stdout.isatty() if tty is None else tty
    got = choose(args, ask, say, tty, volumes, last_file)
    if not got:
        return 1
    dest, vol = got
    entries = content(zpath, c)
    plan = make_plan(dest, entries)
    line = summary_line(vol.name if vol else dest, plan)
    lib = lib_choose(args, say)
    ladd, lrep, lsame, lsize = lib_plan(dest, lib) if lib else ([], [], 0, 0)
    if lib:
        free = shutil.disk_usage(dest).free
        line += (f"; samples from {lib}: {len(ladd)} to add, {len(lrep)} to replace, {lsame} unchanged, "
                 f"{lsize / 1e6:,.0f} MB ({free / 1e6:,.0f} MB free)")
        if lsize > free:
            say(line)
            say("sdcard: not enough room on the card for the samples: nothing written")
            return 1
    if args.dry_run:
        for tag, rels in (("add", plan.add), ("replace", plan.replace), ("remove", plan.remove), ("mkdir", plan.mkdirs),
                          ("add", [r for _, r in ladd]), ("replace", [r for _, r in lrep])):
            for r in rels:
                say(f"  {tag:8s} {r}")
        say(line + f" ({plan.unchanged} unchanged); dry run: nothing written")
        return 0
    say(line)
    if not args.yes and tty:                         # (no terminal: only with --card, which names the card itself)
        if ask("Go? [y/N] ").strip().lower() not in ("y", "yes"):
            say("cancelled: nothing written")
            return 1
    written = apply(dest, plan, entries)
    if lib:
        written += lib_apply(dest, ladd + lrep, say)
        if hasattr(os, "sync"):
            os.sync()
        if args.samples:
            SAMPLES_FILE.parent.mkdir(parents=True, exist_ok=True)
            SAMPLES_FILE.write_text(os.path.abspath(lib) + "\n")
    V.remember(dest, vol, last_file)
    say(f"done: {len(plan.add)} added, {len(plan.replace)} replaced, {len(plan.remove)} removed, {plan.unchanged} "
        f"unchanged; {written:,} B written. Eject the card (Finder: the eject button beside it) before pulling it out.")
    return 0


if __name__ == "__main__":
    sys.exit(run())

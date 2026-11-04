# SPDX-License-Identifier: GPL-3.0-only
"""`make sdcard`: prepare a mounted SD card for SD card mode (the user, 2026-11-04): every folder the FM-1 needs and the
factory content on it, user content never touched.

  python3 tools/sd_card.py [--card PATH] [--dry-run] [--yes]      (tools/optimist.py sdcard, make sdcard [CARD=..])

The content is the SD card zip's (tools/sd_zip.py, from its cache). The FACTORY trees are mirrored: a file there is added
or replaced when its bytes differ, a factory file the zip no longer has is removed, only inside those trees (FACTORY:
Optimist/Presets/Factory, Optimist/Kits/Factory, the factory sample folders under Optimist/Samples:
tools/sd_samples.py factory_dirs) and Optimist/readme.txt. Everything else is the user's and is never written or
removed (Presets/User, Kits/User, Projects, Record, Renders, Backup, System, anything else under Samples or elsewhere);
the FM-1's folders are only made when missing. Files are written plainly (no ._ AppleDouble twins: one the OS makes for a
file written here is removed), the card flushed at the end (os.sync).

With no --card it asks (tools/sd_card_volumes.py pick: the mounted removable FAT volumes, the last ones used first, or
another folder); with no terminal it refuses. Then it shows the plan in one line and asks "Go? [y/N]" (--yes, or no
terminal with --card: no confirmation).
--card must be a mounted FAT volume's root (never /, the home or the repo); a typed folder only warns. The destination
is remembered after a write that succeeded (build/sdcard-last.json)."""
import argparse
import os
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
FACTORY_FILES = [f"{TOP}/readme.txt"]
USER_DIRS = [f"{TOP}/{d}" for d in sd_zip.DIRS + sd_zip.SUBDIRS]


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
    dirs = set(USER_DIRS) | set(FACTORY) | {str(Path(n).parent.as_posix()) for n in entries}
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
    for t in FACTORY:                                # (folders a removal left empty, inside the trees only)
        base = root / t
        for d in sorted((x for x in base.rglob("*") if x.is_dir()), key=lambda x: len(x.parts), reverse=True) \
                if base.is_dir() else []:
            if not any(d.iterdir()):
                d.rmdir()
    if hasattr(os, "sync"):
        os.sync()
    return written


def summary_line(name, plan):
    return (f"card {name}: {len(plan.add)} files to add, {len(plan.replace)} to replace, {len(plan.remove)} factory "
            f"files to remove; user content kept")


def content(zpath=None):
    """-> the zip's entries (made from its cache when its inputs did not change)"""
    z = Path(zpath) if zpath else ROOT / "build" / sd_zip.NAME
    if not zpath:
        sd_zip.make(z, echo=False)
    return zip_entries(z)


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
    args = ap.parse_args(argv)
    tty = sys.stdin.isatty() and sys.stdout.isatty() if tty is None else tty
    got = choose(args, ask, say, tty, volumes, last_file)
    if not got:
        return 1
    dest, vol = got
    entries = content(zpath)
    plan = make_plan(dest, entries)
    line = summary_line(vol.name if vol else dest, plan)
    if args.dry_run:
        for tag, rels in (("add", plan.add), ("replace", plan.replace), ("remove", plan.remove), ("mkdir", plan.mkdirs)):
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
    V.remember(dest, vol, last_file)
    say(f"done: {len(plan.add)} added, {len(plan.replace)} replaced, {len(plan.remove)} removed, {plan.unchanged} "
        f"unchanged; {written:,} B written. Eject the card (Finder: the eject button beside it) before pulling it out.")
    return 0


if __name__ == "__main__":
    sys.exit(run())

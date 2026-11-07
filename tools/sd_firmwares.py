#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The latest firmwares onto an SD card, in /FIRMWARES, for MENU.BIN (firmware/stage2) to flash from m-bootloader's
SD CARD item. The sources: The Firmware Hub's catalogue (M-VAVE's open source firmware too), Optimist's latest GitHub
release, M-VAVE's download page for its stock firmware, the author's install page for Baud Girl. A download is checked against its SHA-256; M-VAVE and
Baud Girl publish none, so theirs must read as an FM-1 package. Each is read back from the card.

  python3 tools/sd_firmwares.py                       download into build/sd-firmwares/, and bring the card's /FIRMWARES
                                                      up to date: the one SD card mounted is found (several: it asks)
  python3 tools/sd_firmwares.py --card /Volumes/CARD  that card
  python3 tools/sd_firmwares.py --download-only       no card
  python3 tools/sd_firmwares.py --add ID [--add ID]   also these catalogue ids (--list shows them all)
  python3 tools/sd_firmwares.py --file PKG.fwsc       also a local package, a build of your own
  python3 tools/sd_firmwares.py --no-optimist         without Optimist's flavours

What the card's folder keeps: only this tool's files are ever removed (its _manifest.json names them), so a package
copied there by hand stays. MENU.BIN lists 32 packages at most. It refuses a package whose flash layout differs from
the installed one ("LAYOUT CHANGE: USE USB"), or whose chip key is not this FM-1's: the LAYOUT column shows each
package's end of flash, and only the same end can be flashed from the card."""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
CACHE = ROOT / "build" / "sd-firmwares"
HUB = "https://thefirmwarehub.com"
OPTIMIST_RELEASE = "https://api.github.com/repos/w0ts/optimist/releases/latest"
AGENT = "optimist-sd-firmwares"
FOLDER = "FIRMWARES"                    # (s2_card.c)
MANIFEST = "_manifest.json"             # (a name s2_is_firmware does not list)
MAX_FILES = 32                          # (s2_main.c)
LABEL_MAX = 20                          # (s2_flash.h S2_LABEL_MAX: the list shows the name without .fwsc)
MIN_FREE = 8 * 1024 * 1024              # left on the card after a copy

MVAVE_PAGE = "https://www.m-vave.com/download"
MVAVE_PACKAGE = "https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc"
BAUDGIRL = "https://baudgirl.com"
BAUDGIRL_PAGE = BAUDGIRL + "/work/FM-1+VA/install"

CHOSEN = ("sloop", "skiff", "melodee", "x0x", "amb", "felucca", "jiant", "choralroot", "loom", "clipper", "obervave")


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=60) as reply:
        return reply.read()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def label(name, version):
    """the file's name without .fwsc: what the FM-1's list shows, in its columns"""
    version = str(version or "").lstrip("vV")
    keep = LABEL_MAX - len(version) - 1
    text = f"{name[:keep].rstrip()} {version}".strip() if version else name[:LABEL_MAX]
    return re.sub(r"[^A-Za-z0-9 ._+-]", "-", text)


def hub_item(entry, hub):
    """a catalogue entry -> what to fetch; its pkg must be one of the hub's own firmware paths"""
    pkg = entry.get("pkg") or ""
    if not re.fullmatch(r"fw(/[A-Za-z0-9._+-]+)+\.fwsc", pkg) or ".." in pkg or not entry.get("sha256"):
        raise SystemExit(f"sd_firmwares: {entry.get('id')}: the catalogue's entry has no usable package ({pkg!r})")
    return {"id": entry["id"], "label": label(entry.get("name") or entry["id"], entry.get("version")),
            "url": f"{hub}/{pkg}", "sha256": entry["sha256"].lower(), "size": entry.get("size"),
            "product": entry.get("product"), "beta": bool(entry.get("beta"))}


def hub_items(catalogue, wanted, hub):
    entries = {e["id"]: e for e in catalogue["firmwares"]}
    items = []
    for name in wanted:
        if name not in entries:
            raise SystemExit(f"sd_firmwares: no firmware '{name}' in the catalogue (--list shows them)")
        items.append(hub_item(entries[name], hub))
    return items


def mvave_open_item(catalogue, hub):
    """M-VAVE's open source firmware (m-vave/fm1_open), the catalogue's "stock": the hub carries it with its SHA-256.
    Its layout is the common one, so unlike the latest stock firmware it can be flashed from the card"""
    stock = catalogue["stock"]
    return hub_item({**stock, "id": "mvave-open", "name": "M-VAVE open", "version": stock["product"].rsplit("_")[-1]},
                    hub)


def mvave_item(page):
    """M-VAVE's own latest stock firmware: its version from the download page. The makers publish no SHA-256"""
    block = re.search(r"<h3>FM-1</h3>.*?Version</span>\s*V(\d+)", page, re.S)
    if not block or MVAVE_PACKAGE not in page:
        raise SystemExit(f"sd_firmwares: {MVAVE_PAGE} has changed: FM-1's version or package not found")
    return {"id": "stock", "label": label("M-VAVE stock", "V" + block.group(1)), "url": MVAVE_PACKAGE, "sha256": None,
            "size": None, "product": "FM-1_0" + block.group(1), "beta": False}


def baudgirl_item(page):
    """the newest build the author's install page offers. Baud Girl isn't open source: it's fetched from the author
    for this card alone, never put in a repository or a shared archive. No SHA-256 is published either"""
    builds = {int(n): path for path, n in re.findall(r"(/fm1/firmware/FM-1\+VA-(\d+)\.fwsc)", page)}
    if not builds:
        raise SystemExit(f"sd_firmwares: {BAUDGIRL_PAGE} has changed: no package found")
    newest = max(builds)
    return {"id": "baudgirl", "label": label("Baud Girl", newest), "url": BAUDGIRL + builds[newest], "sha256": None,
            "size": None, "product": None, "beta": False}


def optimist_items(release):
    items = []
    version = release["tag_name"]
    for asset in release["assets"]:
        m = re.fullmatch(r"optimist-[^-]+-(.+)\.fwsc", asset["name"])
        if not m:
            continue
        digest = asset.get("digest") or ""
        if not digest.startswith("sha256:"):
            raise SystemExit(f"sd_firmwares: {asset['name']}: the release gives no SHA-256")
        # (the configuration's own name, whole: the file may be longer than the list's columns, which show its start)
        items.append({"id": "optimist-" + m.group(1), "label": f"Optimist {m.group(1)} {version.lstrip('vV')}",
                      "url": asset["browser_download_url"], "sha256": digest[7:].lower(), "size": asset["size"],
                      "product": "FM-1_79", "beta": False})
    if not items:
        raise SystemExit("sd_firmwares: Optimist's latest release has no .fwsc")
    return items


def file_item(path):
    p = Path(path)
    data = p.read_bytes()
    return {"id": "file-" + p.stem, "label": p.stem[:LABEL_MAX], "url": p.resolve().as_uri(), "sha256": sha256(data),
            "size": len(data), "product": None, "beta": False}


def download_unhashed(item, cache):
    """a source that publishes no SHA-256: the package must at least read as an FM-1 package"""
    cache.mkdir(parents=True, exist_ok=True)
    data = fetch(item["url"])
    digest = sha256(data)
    temp = cache / f"{digest}.part"
    temp.write_bytes(data)
    product, end = package_facts(temp)
    if not end or not (product or "").startswith("FM-1_"):
        temp.unlink()
        raise SystemExit(f"sd_firmwares: {item['id']}: {item['url']} is not an FM-1 package: not kept")
    path = cache / f"{digest}.fwsc"
    temp.replace(path)
    return {**item, "sha256": digest, "size": len(data), "product": product}, path


def download(item, cache):
    """the package in the cache, checked; a cached copy is checked too and fetched again when it is damaged"""
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / f"{item['sha256']}.fwsc"
    if path.exists() and sha256(path.read_bytes()) == item["sha256"]:
        return path
    data = fetch(item["url"])
    if sha256(data) != item["sha256"]:
        raise SystemExit(f"sd_firmwares: {item['id']}: SHA-256 differs from what {item['url']} promises: not kept")
    if item.get("size") and len(data) != item["size"]:
        raise SystemExit(f"sd_firmwares: {item['id']}: {len(data)} B, expected {item['size']}: not kept")
    temp = path.with_suffix(".part")
    temp.write_bytes(data)
    temp.replace(path)
    return path


def package_facts(path):
    """the package's (identity, end of flash) as tools/fm1_install.py reads them; (None, None): not a package.
    The end may be one flash_layout doesn't know: M-VAVE's V16 ends at 0xa9000"""
    import flash
    raw = Path(path).read_bytes()
    try:
        return flash.FI.product_of(raw), flash.FI.flash_size(flash.FI.logical_image(raw))
    except (flash.FI.InstallError, ValueError, IndexError):
        return None, None


def check_card(card):
    card = Path(card)
    if not card.is_dir():
        raise SystemExit(f"sd_firmwares: {card} is not mounted")
    if os.path.realpath(card) == "/" or not os.path.ismount(card):
        raise SystemExit(f"sd_firmwares: {card} is not a mounted volume: give the card's own folder, /Volumes/NAME")
    return card


def find_card(volumes=None, ask=input):
    """the card without being told: the one mounted removable FAT volume; several: asked (the last used first, as
    make sdcard does); none: None"""
    import sd_card_volumes as V
    cards = V.candidates(volumes)
    if not cards:
        return None
    if len(cards) == 1:
        print(f"the card: {cards[0].name} ({cards[0].path})")
        return cards[0].path
    return V.pick(cards, V.load_last(), ask)


def read_manifest(folder):
    path = folder / MANIFEST
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text())
    except ValueError as e:
        raise SystemExit(f"sd_firmwares: {path} is damaged ({e}): nothing removed; delete it to start again") from e


def sync_card(card, chosen):
    """chosen: (item, cached path). Old versions of this tool's files go first, so a small card has the room"""
    folder = card / FOLDER
    folder.mkdir(exist_ok=True)
    old = read_manifest(folder)
    names = {f"{item['label']}.fwsc" for item, _ in chosen}
    if len(names) != len(chosen):
        raise SystemExit("sd_firmwares: two packages would take one name on the card")
    kept_by_hand = [p.name for p in folder.glob("*.fwsc")   # (not macOS's ._ sidecars: s2_is_firmware skips them too)
                    if not p.name.startswith("_") and not p.name.startswith(".")
                    and p.name not in old and p.name not in names]
    if len(names) + len(kept_by_hand) > MAX_FILES:
        raise SystemExit(f"sd_firmwares: {len(names)} + {len(kept_by_hand)} packages: MENU.BIN lists {MAX_FILES}")
    for name in old:
        if name not in names:
            (folder / name).unlink(missing_ok=True)
            (folder / ("._" + name)).unlink(missing_ok=True)
            print(f"  removed {name}")
    manifest = {}
    for item, path in chosen:
        target = folder / f"{item['label']}.fwsc"
        if not (target.exists() and old.get(target.name) == item["sha256"]
                and sha256(target.read_bytes()) == item["sha256"]):
            if shutil.disk_usage(card).free < path.stat().st_size + MIN_FREE:
                raise SystemExit(f"sd_firmwares: the card is full before {target.name}")
            temp = folder / (target.name + ".tmp")     # (a name MENU.BIN does not list)
            shutil.copyfile(path, temp)
            with open(temp, "rb+") as f:
                os.fsync(f.fileno())
            temp.replace(target)
            if sha256(target.read_bytes()) != item["sha256"]:
                target.unlink()
                raise SystemExit(f"sd_firmwares: {target.name} reads back differently: is the card failing?")
            print(f"  wrote {target.name}")
        manifest[target.name] = item["sha256"]
    (folder / MANIFEST).write_text(json.dumps(manifest, indent=1) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", help="the card's mount point, /Volumes/NAME (default: found by itself)")
    ap.add_argument("--download-only", action="store_true", help="write to no card")
    ap.add_argument("--add", action="append", default=[], metavar="ID", help="a catalogue id (repeatable)")
    ap.add_argument("--file", action="append", default=[], metavar="PKG", help="a local .fwsc (repeatable)")
    ap.add_argument("--no-optimist", action="store_true", help="without Optimist's flavours")
    ap.add_argument("--no-makers", action="store_true", help="without M-VAVE's stock firmware and Baud Girl")
    ap.add_argument("--list", action="store_true", help="print the catalogue's ids and stop")
    ap.add_argument("--hub", default=HUB, help=argparse.SUPPRESS)
    ap.add_argument("--release-url", default=OPTIMIST_RELEASE, help=argparse.SUPPRESS)
    ap.add_argument("--cache", default=str(CACHE), help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.download_only and a.card:
        raise SystemExit("sd_firmwares: --card or --download-only, not both")
    path = a.card or (None if a.download_only or a.list else find_card())
    card = check_card(path) if path else None

    catalogue = json.loads(fetch(f"{a.hub}/catalog.json"))
    if a.list:
        for e in sorted(catalogue["firmwares"], key=lambda e: -(e.get("stars") or 0)):
            print(f"{e['id']:<18} {e.get('version') or '':<12} {e.get('stars') or 0:>4}  {e.get('name')}")
        return
    items = [] if a.no_optimist else optimist_items(json.loads(fetch(a.release_url)))
    items += hub_items(catalogue, list(dict.fromkeys([*CHOSEN, *a.add])), a.hub)
    if not a.no_makers:
        items.append(mvave_open_item(catalogue, a.hub))
    items += [file_item(f) for f in a.file]
    unhashed = [] if a.no_makers else [mvave_item(fetch(MVAVE_PAGE).decode("utf-8", "replace")),
                                       baudgirl_item(fetch(BAUDGIRL_PAGE).decode("utf-8", "replace"))]
    if len(items) + len(unhashed) > MAX_FILES:
        raise SystemExit(f"sd_firmwares: {len(items) + len(unhashed)} packages: MENU.BIN lists {MAX_FILES}")

    chosen = [(item, download(item, Path(a.cache))) for item in items]
    chosen += [download_unhashed(item, Path(a.cache)) for item in unhashed]
    print(f"{'ON THE CARD':<44}  {'PRODUCT':<14} {'LAYOUT':<8} SIZE")
    for item, path in chosen:
        product, end = package_facts(path)
        print(f"{item['label']:<44}  {product or '':<14} {hex(end) if end else '?':<8} "
              f"{path.stat().st_size:,}{'  beta' if item['beta'] else ''}")
    if card:
        sync_card(card, chosen)
        print(f"{len(chosen)} packages in {card / FOLDER}; eject the card before taking it out")
    elif a.download_only:
        print("downloaded only")
    else:
        print("no SD card found: downloaded only (insert one, FAT32, and run this again)")


if __name__ == "__main__":
    main()

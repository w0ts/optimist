#!/usr/bin/env python3
"""tools/sd_firmwares.py without a network or a card: names, checks and what the sync removes"""
import hashlib
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import sd_firmwares as S  # noqa: E402


def refused(fn, *args):
    try:
        fn(*args)
    except SystemExit:
        return True
    return False


def item(name, data):
    return {"id": name, "label": name, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data),
            "url": "", "product": None, "beta": False}


def test_labels():
    assert S.label("SLOOP", "v2.8.5") == "SLOOP 2.8.5"
    assert len(S.label("A very long firmware name", "10.6c")) <= S.LABEL_MAX
    assert "/" not in S.label("a/b", "1")
    assert S.label("GR!MM", "2") == "GR-MM 2"


def test_catalogue_paths():
    good = {"id": "a", "pkg": "fw/a-1.fwsc", "sha256": "AB", "name": "A", "version": "1"}
    entry = S.hub_item(good, "https://hub")
    assert entry["url"] == "https://hub/fw/a-1.fwsc" and entry["sha256"] == "ab"
    for pkg in ("../evil.fwsc", "fw/../../evil.fwsc", "https://x/a.fwsc", "fw/a.exe", ""):
        assert refused(S.hub_item, {**good, "pkg": pkg}, "https://hub"), pkg
    assert refused(S.hub_item, {**good, "sha256": None}, "https://hub")


def test_optimist_release():
    asset = {"name": "optimist-0.2-synths_USBaudio_BTaudio_BTmidi.fwsc", "digest": "sha256:AA", "size": 3,
             "browser_download_url": "u"}
    other = {"name": "optimist-0.2-synths-ui.zip", "digest": "sha256:BB", "size": 3, "browser_download_url": "v"}
    items = S.optimist_items({"tag_name": "v0.2", "assets": [asset, other]})
    assert [i["label"] for i in items] == ["Optimist synths_USBaudio_BTaudio_BTmidi 0.2"], items
    assert items[0]["sha256"] == "aa"
    flavours = ("drum-machines", "synths", "synths-2", "synths_USBaudio_BTaudio_BTmidi")
    names = [S.optimist_items({"tag_name": "v0.2", "assets": [{**asset, "name": f"optimist-0.2-{f}.fwsc"}]})[0]["label"]
             for f in flavours]
    assert names[0] == "Optimist drum-machines 0.2", names
    assert len({n[:S.LABEL_MAX] for n in names}) == len(names), names   # (the FM-1's list tells them apart)
    assert refused(S.optimist_items, {"tag_name": "v0.2", "assets": [other]})
    assert refused(S.optimist_items, {"tag_name": "v0.2", "assets": [{**asset, "digest": None}]})


def test_mvave_open():
    cat = {"stock": {"pkg": "fw/mvave/FM-1_015.fwsc", "sha256": "AB", "size": 9, "product": "FM-1_015"}}
    item_ = S.mvave_open_item(cat, "https://hub")
    assert item_["label"] == "M-VAVE open 015" and item_["sha256"] == "ab"
    assert item_["url"] == "https://hub/fw/mvave/FM-1_015.fwsc"


def test_makers_pages():
    page = f'<h3>FM-1</h3> <p><span data-i18n="x">Version</span> V16 · 2026_10_21</p> <a href="{S.MVAVE_PACKAGE}">'
    stock = S.mvave_item(page)
    assert stock["label"] == "M-VAVE stock 16" and stock["sha256"] is None and stock["product"] == "FM-1_016"
    assert refused(S.mvave_item, "<h3>FM-1</h3> nothing here")
    page = '"/fm1/firmware/FM-1+VA-97.fwsc" "/fm1/firmware/FM-1+VA-102.fwsc" "/fm1/firmware/FM-1+VA-83.fwsc"'
    girl = S.baudgirl_item(page)
    assert girl["label"] == "Baud Girl 102" and girl["url"].endswith("/fm1/firmware/FM-1+VA-102.fwsc")
    assert refused(S.baudgirl_item, "no packages")


def test_find_card():
    import sd_card_volumes as V
    vol = lambda p, fs="msdos", removable=True: V.Volume(p, p, "", fs, 1, 1, removable)  # noqa: E731
    one, two = vol("/Volumes/A"), vol("/Volumes/B")
    V.load_last = lambda: []               # (not this computer's own list of cards)
    assert S.find_card([]) is None
    assert S.find_card([vol("/", "apfs", False), vol("/Volumes/X", "exfat"), vol("/Volumes/D", "msdos", False)]) is None
    assert S.find_card([one, vol("/", "apfs", False)]) == "/Volumes/A"
    assert S.find_card([one, two], ask=lambda _: "2") in ("/Volumes/A", "/Volumes/B")
    assert S.find_card([one, two], ask=lambda _: "q") is None


def test_download_refuses_a_bad_hash():
    with tempfile.TemporaryDirectory() as t:
        src = Path(t) / "a.fwsc"
        src.write_bytes(b"real")
        good = S.file_item(src)
        assert S.download(good, Path(t) / "c").read_bytes() == b"real"
        assert refused(S.download, {**good, "sha256": "0" * 64}, Path(t) / "c")
        assert not list((Path(t) / "c").glob("0*"))


def test_sync_keeps_what_is_not_its_own():
    with tempfile.TemporaryDirectory() as t:
        card, cache = Path(t) / "card", Path(t) / "cache"
        (card / S.FOLDER).mkdir(parents=True)
        cache.mkdir()
        hand = card / S.FOLDER / "by hand.fwsc"
        hand.write_bytes(b"mine")
        v1, v2 = cache / "1", cache / "2"
        v1.write_bytes(b"one")
        v2.write_bytes(b"two")
        S.sync_card(card, [(item("A 1", b"one"), v1)])
        assert (card / S.FOLDER / "A 1.fwsc").read_bytes() == b"one"
        S.sync_card(card, [(item("A 2", b"two"), v2)])
        names = sorted(p.name for p in (card / S.FOLDER).glob("*.fwsc"))
        assert names == ["A 2.fwsc", "by hand.fwsc"], names
        assert not list((card / S.FOLDER).glob("*.tmp"))
        assert json.loads((card / S.FOLDER / S.MANIFEST).read_text()) == {"A 2.fwsc": item("A 2", b"two")["sha256"]}


def test_sync_ignores_macos_sidecars():
    with tempfile.TemporaryDirectory() as t:
        card, src = Path(t) / "card", Path(t) / "s"
        folder = card / S.FOLDER
        folder.mkdir(parents=True)
        src.write_bytes(b"x")
        S.sync_card(card, [(item("Old", b"x"), src)])
        (folder / "._Old.fwsc").write_bytes(b"sidecar")
        for i in range(S.MAX_FILES):                     # (as many as a full card has: none of them a package)
            (folder / f"._k{i}.fwsc").write_bytes(b"sidecar")
        S.sync_card(card, [(item("New", b"x"), src)])
        assert (folder / "New.fwsc").exists() and not (folder / "Old.fwsc").exists()
        assert not (folder / "._Old.fwsc").exists()


def test_sync_replaces_a_damaged_file():
    with tempfile.TemporaryDirectory() as t:
        card, src = Path(t) / "card", Path(t) / "s"
        card.mkdir()
        src.write_bytes(b"one")
        S.sync_card(card, [(item("A", b"one"), src)])
        (card / S.FOLDER / "A.fwsc").write_bytes(b"bad")
        S.sync_card(card, [(item("A", b"one"), src)])
        assert (card / S.FOLDER / "A.fwsc").read_bytes() == b"one"


def test_sync_refuses_too_many_and_a_damaged_manifest():
    with tempfile.TemporaryDirectory() as t:
        card, src = Path(t) / "card", Path(t) / "s"
        (card / S.FOLDER).mkdir(parents=True)
        src.write_bytes(b"x")
        for i in range(S.MAX_FILES):
            (card / S.FOLDER / f"h{i}.fwsc").write_bytes(b"x")
        assert refused(S.sync_card, card, [(item("A", b"x"), src)])
        (card / S.FOLDER / S.MANIFEST).write_text("{")
        assert refused(S.read_manifest, card / S.FOLDER)


def test_card_must_be_a_volume():
    with tempfile.TemporaryDirectory() as t:
        assert refused(S.check_card, t)
    assert refused(S.check_card, "/")
    assert refused(S.check_card, "/Volumes/no such card")


for name, fn in sorted(globals().copy().items()):
    if name.startswith("test_"):
        fn()
        print("ok", name)
print("sd_firmwares_test: passed")

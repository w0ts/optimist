# SPDX-License-Identifier: GPL-3.0-only
"""`make sdcard` (tools/sd_card.py, tools/sd_card_volumes.py) on a temporary folder standing in for the card, the
volumes faked: a first run fills it (the FM-1's folders, the factory trees), a second writes nothing, a changed factory
file is replaced, a stale one removed (and an AppleDouble twin of a written file), the user's presets, projects and own
samples kept byte for byte; the destination's checks (/, the home, the repo, a folder that is no volume); the picker (a
fixed volume list, scripted answers, cancel, no terminal, Go? no); the last destinations (most recent first, Enter takes
the last, a card known by its UUID under a new mount name, five at most, nothing stored on a cancel); every file of the
real zip inside a factory tree.
  python3 tests/sd_card_sync_test.py OUTDIR [the real zip]"""
import importlib
import json
import pathlib
import shutil
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
S = importlib.import_module("sd_card")
V = importlib.import_module("sd_card_volumes")

fails = 0


def check(what, ok):
    global fails
    print(f"{what:110s} {'ok' if ok else 'FAIL'}")
    fails += not ok


FAKE = {"Optimist/readme.txt": b"readme 1\n",
        "Optimist/Presets/Factory/Analog/A/01 - init.ops": b"OPPR analog init",
        "Optimist/Presets/Factory/Analog/A/02 - bass.ops": b"OPPR analog bass",
        "Optimist/Kits/Factory/A/01 - acoustic.opk": b"OPKT acoustic",
        "Optimist/Samples/Piano/z0.wav": b"RIFF piano z0",
        "Optimist/Samples/Kits/Acoustic/01 - kick.wav": b"RIFF kick",
        "Optimist/Samples/X0X/909/hh.wav": b"RIFF hh",
        "Optimist/Samples/Hiphop/Bass/00_e1.wav": b"RIFF bass"}
USER = {"Optimist/Presets/User/Analog/A/01 - mine.ops": b"OPPR my own",
        "Optimist/Kits/User/A/01 - my kit.opk": b"OPKT my kit",
        "Optimist/Projects/A/01 - song/01 - song.opp": b"my project",
        "Optimist/Samples/Mine/kick.wav": b"my own sample",
        "Optimist/Samples/Piano Extra/z0.wav": b"not the factory's Piano",
        "notes.txt": b"something else on the card"}


def write_zip(path, entries):
    with zipfile.ZipFile(path, "w") as z:
        for n, b in entries.items():
            z.writestr(n, b)


class Script:
    """scripted answers; what was said"""
    def __init__(self, answers):
        self.answers, self.said = list(answers), []

    def ask(self, prompt):
        self.said.append(prompt)
        return self.answers.pop(0) if self.answers else "q"

    def say(self, text):
        self.said.append(text)

    def text(self):
        return "\n".join(self.said)


def vol(path, name, uuid, fs="msdos"):
    return V.Volume(str(path), name, uuid, fs, 8 << 30, 4 << 30, True)


def sync_tests(out):
    card = out / "CARD"
    zp, last = out / "fake.zip", out / "last.json"
    write_zip(zp, FAKE)
    vols = [vol(card, "OPTIMIST", "UUID-1")]
    card.mkdir()
    for rel, b in USER.items():
        (card / rel).parent.mkdir(parents=True, exist_ok=True)
        (card / rel).write_bytes(b)
    sc = Script([])
    r = S.run(["--card", str(card)], sc.ask, sc.say, False, vols, last, zp)
    check("a first run fills it: every factory file, the FM-1's folders", r == 0 and all(
        (card / n).read_bytes() == b for n, b in FAKE.items()) and all((card / d).is_dir() for d in S.USER_DIRS))
    check("its summary: 8 added, the bytes written, eject in Finder", "8 added, 0 replaced, 0 removed" in sc.text() and
          f"{sum(map(len, FAKE.values())):,} B written" in sc.text() and "Eject" in sc.text())
    sc = Script([])
    S.run(["--card", str(card)], sc.ask, sc.say, False, vols, last, zp)
    check("a second run writes nothing", "0 files to add, 0 to replace, 0 factory files to remove" in sc.text() and
          " 0 B written" in sc.text() and "8 unchanged" in sc.text())
    changed = dict(FAKE, **{"Optimist/Samples/Piano/z0.wav": b"RIFF piano z0, made again",
                            "Optimist/readme.txt": b"readme 2\n"})
    del changed["Optimist/Presets/Factory/Analog/A/02 - bass.ops"]
    write_zip(zp, changed)
    (card / "Optimist/Kits/Factory/A/01 - acoustic.opk").write_bytes(b"damaged")
    (card / "Optimist/Presets/Factory/Old/A/01 - gone.ops").parent.mkdir(parents=True)
    (card / "Optimist/Presets/Factory/Old/A/01 - gone.ops").write_bytes(b"OPPR stale")
    (card / "Optimist/Samples/Piano/._z0.wav").write_bytes(b"AppleDouble")
    sc = Script([])
    S.run(["--card", str(card)], sc.ask, sc.say, False, vols, last, zp)
    check("changed factory files replaced (a sample, the readme, a damaged kit)", "3 replaced" in sc.text() and
          all((card / n).read_bytes() == b for n, b in changed.items()))
    check("stale factory files removed (one the zip dropped, one it never had) and their empty folders",
          "2 removed" in sc.text() and not (card / "Optimist/Presets/Factory/Analog/A/02 - bass.ops").exists() and
          not (card / "Optimist/Presets/Factory/Old").exists())
    check("the ._ twin of a file written removed", not (card / "Optimist/Samples/Piano/._z0.wav").exists())
    check("the user's presets, kits, project, own samples and other files kept byte for byte",
          all((card / n).read_bytes() == b for n, b in USER.items()))
    sc = Script([])
    before = sorted(p.as_posix() for p in card.rglob("*"))
    (card / "Optimist/Samples/Piano/z0.wav").write_bytes(b"x")
    S.run(["--card", str(card), "--dry-run"], sc.ask, sc.say, False, vols, last, zp)
    check("--dry-run lists what it would replace and writes nothing", "replace  Optimist/Samples/Piano/z0.wav" in
          sc.text() and (card / "Optimist/Samples/Piano/z0.wav").read_bytes() == b"x" and
          sorted(p.as_posix() for p in card.rglob("*")) == before)


def check_tests(out):
    home = out / "home"
    home.mkdir()
    plain = out / "plain"
    plain.mkdir()
    vols = [vol(out / "CARD", "OPTIMIST", "UUID-1"), vol(out / "home", "Macintosh HD", "U-H", "apfs")]
    check("refused: /", not V.check_dest("/", vols, True, home)[0])
    check("refused: the home", not V.check_dest(home, vols, True, home)[0])
    check("refused: the repo and a folder inside it", not V.check_dest(ROOT, vols, True, home, ROOT)[0] and
          not V.check_dest(ROOT / "build", vols, True, home, ROOT)[0])
    check("refused: a folder that is no volume (--card)", not V.check_dest(plain, vols, False, home)[0])
    ok, msg = V.check_dest(plain, vols, True, home)
    check("a typed plain folder: allowed with a warning", ok and msg.startswith("warning"))
    check("a FAT volume's root: allowed", V.check_dest(out / "CARD", vols, False, home)[0])
    nf = out / "apfsvol"
    nf.mkdir()
    check("refused: a volume not FAT", not V.check_dest(nf, [vol(nf, "X", "U-X", "apfs")], False, home)[0])
    check("the candidates: removable FAT volumes only", [v.name for v in V.candidates(vols)] == ["OPTIMIST"])


def picker_tests(out):
    a, b = out / "A", out / "B"
    a.mkdir()
    b.mkdir()
    zp, last = out / "pick.zip", out / "pick-last.json"
    write_zip(zp, FAKE)
    vols = [vol(a, "CARD A", "UA"), vol(b, "CARD B", "UB")]
    sc = Script(["2", "y"])
    r = S.run([], sc.ask, sc.say, True, vols, last, zp)
    check("the picker: the volumes listed with size, free and Optimist/; 2 picks B, Go? y writes", r == 0 and
          "CARD A" in sc.text() and "free" in sc.text() and "no Optimist/ yet" in sc.text() and (b / "Optimist").is_dir())
    check("the plan in one line before Go?", "card CARD B: 8 files to add, 0 to replace, 0 factory files to remove; "
          "user content kept" in sc.text() and "Go? [y/N] " in sc.said)
    rows = V.choices(V.candidates(vols), V.load_last(last))
    check("remembered: B first, marked (last)", rows[0].path == str(b) and rows[0].last and "has Optimist/" in rows[0].label)
    snap = last.read_text()
    for answers, what in ((["q"], "q"), (["0"], "0"), (["1", "n"], "Go? no")):
        sc = Script(answers)
        r = S.run([], sc.ask, sc.say, True, vols, last, zp)
        check(f"cancel ({what}): nothing written, nothing remembered", r == 1 and not (a / "Optimist").exists() and
              last.read_text() == snap and "cancelled" in sc.text())
    sc = Script([""])
    p = V.pick(V.candidates(vols), V.load_last(last), sc.ask, sc.say)
    check("Enter takes the last one (preselected)", p == str(b) and "Enter = 1 (last)" in sc.text())
    b2 = out / "B renamed"
    b2.mkdir()
    moved = [vol(a, "CARD A", "UA"), vol(b2, "CARD B 1", "UB")]
    sc = Script([""])
    p = V.pick(V.candidates(moved), V.load_last(last), sc.ask, sc.say)
    check("a remembered card under a new mount name: known by its UUID, Enter takes its new path", p == str(b2))
    gone = [vol(a, "CARD A", "UA")]
    sc = Script(["1", "q"])
    p = V.pick(V.candidates(gone), V.load_last(last), sc.ask, sc.say)
    check("a past card not mounted now: listed so, not pickable", p is None and "not mounted now" in sc.text() and
          "that card is not mounted now" in sc.text())
    sc = Script([str(len(V.choices(V.candidates(vols), V.load_last(last))) + 1), str(out / "plain2")])
    (out / "plain2").mkdir()
    p = V.pick(V.candidates(vols), V.load_last(last), sc.ask, sc.say)
    check("another folder (type a path)", p == str(out / "plain2"))
    sc = Script([])
    r = S.run([], sc.ask, sc.say, False, vols, last, zp)
    check("no terminal and no card: refused, said clearly", r == 1 and "no terminal" in sc.text())
    many = out / "many.json"
    for k in range(7):
        d = out / f"f{k}"
        d.mkdir()
        V.remember(d, None, many, now=1_700_000_000 + k)
    got = [pathlib.Path(e["path"]).name for e in json.loads(many.read_text())]
    check("five at most, the most recent first", got == ["f6", "f5", "f4", "f3", "f2"])
    V.remember(out / "f3", None, many)
    got = [pathlib.Path(e["path"]).name for e in json.loads(many.read_text())]
    check("one used again moves to the front, once", got == ["f3", "f6", "f5", "f4", "f2"])


def real_zip_test(z):
    entries = S.zip_entries(z)
    check(f"the real zip ({len(entries)} files): every file inside a factory tree (FACTORY, readme.txt)",
          entries and all(S.is_factory(n) for n in entries))
    check("the factory sample folders: every folder under Optimist/Samples in the zip, and nothing else",
          {n.split("/")[2] for n in entries if n.startswith("Optimist/Samples/")} ==
          set(S.sd_samples.factory_dirs()))


def main(out, real=None):
    out = pathlib.Path(out).resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    V.REPO = out / "a-repo"                           # (OUTDIR may be in the checkout, build/host: the real repo checked
    V.REPO.mkdir()                                    # alone, check_tests' repo=ROOT)
    sync_tests(out)
    check_tests(out)
    picker_tests(out)
    if real:
        real_zip_test(real)
    shutil.rmtree(out, ignore_errors=True)
    print("sd_card_sync_test:", "FAILED" if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "sd-card-sync-test"),
                  sys.argv[2] if len(sys.argv) > 2 else None))

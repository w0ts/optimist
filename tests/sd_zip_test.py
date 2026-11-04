# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip (tools/sd_zip.py; the user, 2026-11-04: it holds everything whatever the builder's ticks; folders
capitalised, files lowercase; presets and projects in banks of 16): every sample set's zones under
Optimist/Samples/<Set>/z<k>.wav (the generator's bytes), every factory preset of every engine under
Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops (16 a bank, in the table's order), the engines' folder names
the firmware's own table (firmware/src/storage/sd/sd_names.h) and that table the rule (capitalised; a digit or a known
acronym upper case), the cache (a second make copies the first's bytes).
  python3 tests/sd_zip_test.py OUTDIR"""
import importlib
import pathlib
import re
import sys
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
gen_samples = importlib.import_module("gen_samples")   # (tools/: on the path above)
sd_zip = importlib.import_module("sd_zip")
ACRONYMS = {"CZ", "SID", "VPS", "WSEQ"}                # (the user, 2026-11-04: kept upper case, as names with a digit)

fails = 0


def check(what, ok):
    global fails
    print(f"{what:110s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def names_table():
    """-> {registry name: folder} from sd_names.h's SDN_<NAME> lines"""
    text = (ROOT / "firmware" / "src" / "storage" / "sd" / "sd_names.h").read_text()
    return dict(re.findall(r'#define SDN_([A-Z0-9_]+) "([^"]+)"', text))


def rule(name):
    """the user's rule for an engine's folder: capitalised, but a name with a digit or a known acronym upper case"""
    return name if any(c.isdigit() for c in name) or name in ACRONYMS else name[:1] + name[1:].lower()


def main(out):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    sd_zip.CACHE = out / "cache"                        # (the test's own cache: a fresh zip first)
    t0 = time.time()
    z = sd_zip.make(out / sd_zip.NAME)
    t_new = time.time() - t0
    t0 = time.time()
    z2 = sd_zip.make(out / "again.zip")
    t_cached = time.time() - t0
    print(f"sd zip: {z.stat().st_size:,} B; made in {t_new:.1f} s, from the cache in {t_cached:.2f} s")
    check("the cache: a second make copies the first's bytes", z.read_bytes() == z2.read_bytes() and t_cached < t_new)
    ref = out / "sd-zip-ref"
    made = gen_samples.card_files(ref, list(gen_samples.CARD_SETS))
    table = names_table()
    with zipfile.ZipFile(z) as f:
        names = set(f.namelist())
        files = [n for n in names if not n.endswith("/")]
        sets = {m.group(1) for n in names if (m := re.match(r"Optimist/Samples/([^/]+)/z\d\.wav$", n))}
        check("every sample set, whatever the ticks: " + ", ".join(sorted(sets)),
              sets == {gen_samples.card_set_dir(s) for s in gen_samples.CARD_SETS})
        want = {f"Optimist/Samples/{gen_samples.card_set_dir(s)}/{n}" for s, v in made.items() for n in v}
        check(f"every zone of each ({len(want)}), as z0.wav .. in key order", want <= names and
              len([n for n in names if n.endswith(".wav")]) == len(want))
        check("each file the generator's own (the flash set's samples decoded)",
              all(f.read(n) == (ref / "Optimist" / "Samples" / n.split("/", 2)[2]).read_bytes() for n in want))
        check("each a WAV with fmt, smpl, inst and data", all(
            f.read(n)[:4] == b"RIFF" and all(c in f.read(n)[:200] for c in (b"fmt ", b"smpl", b"inst", b"data"))
            for n in want))
        check("the folders: Optimist/ and its eight, Presets/Factory and Presets/User",
              all(f"Optimist/{d}/" in names for d in sd_zip.DIRS + sd_zip.SUBDIRS))
        check("every file's name lowercase", all(n.rsplit("/", 1)[-1] == n.rsplit("/", 1)[-1].lower() for n in files))
        check("every folder's name capitalised (a bank's a letter)",
              all(p[:1].isupper() for n in names for p in n.split("/")[:-1]))
        pre = [m.groups() for n in files if (m := re.match(r"Optimist/Presets/Factory/([^/]+)/([A-Z])/(\d\d) - (.+)\.ops$", n))]
        check(f"the factory presets: {len(pre)} files, each <Engine>/<L>/<nn> - <name>.ops",
              len(pre) == len([n for n in files if n.startswith("Optimist/Presets/Factory/")]) and len(pre) > 200)
        by = {}
        for eng, bank, nn, _ in pre:
            by.setdefault(eng, set()).add((ord(bank) - 65) * 16 + int(nn) - 1)
        check("each engine's presets numbered 0 .. n - 1 with no gap: A/01 .. A/16, B/01 ..",
              all(v == set(range(len(v))) for v in by.values()) and all(1 <= int(nn) <= 16 for _, _, nn, _ in pre))
        check("the engines' folders are sd_names.h's (the firmware's table): " + " ".join(sorted(by)),
              set(by) <= set(table.values()))
        heads = [f.read(n)[:4] for n in files if n.startswith("Optimist/Presets/Factory/")]
        check("each a card preset file (\"OPPR\")", heads and all(h == b"OPPR" for h in heads))
        readme = f.read("Optimist/readme.txt").decode()
        check("the readme names the sets and the banks", "Piano" in readme and "16 a bank" in readme)
    reg = set(re.findall(r"X\(\d+, ([A-Z0-9_]+),", (ROOT / "firmware" / "src" / "core" / "registry.h").read_text()))
    check("sd_names.h names every engine of registry.h ENGINE_LIST", reg <= set(table))
    bad = {k: v for k, v in table.items() if v != rule(k)}
    check("sd_names.h follows the rule: capitalised, a digit or a known acronym upper case" + (f": {bad}" if bad else ""),
          not bad and table.get("FM6") == "FM6" and table.get("CS80") == "CS80" and table.get("ACID") == "Acid")
    print("sd_zip_test:", "FAILED" if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "sd-zip-test")))

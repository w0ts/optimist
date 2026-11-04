# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip's sample sets (tools/sd_zip.py, tools/gen_samples.py --card; the user, 2026-10-29: in SD card mode a
ticked set goes on the card, never in the flash): the zip made with two sets ticked holds exactly their zones under
OPTIMIST/SAMPLES/<SET>/Z<k>.WAV (each zone of the flash set, in key order: the same bytes card_files writes), no other
set, and its README names them; the factory presets beside them.
  python3 tests/sd_zip_test.py OUTDIR"""
import importlib
import pathlib
import re
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
gen_samples = importlib.import_module("gen_samples")   # (tools/: on the path above)
sd_zip = importlib.import_module("sd_zip")

fails = 0


def check(what, ok):
    global fails
    print(f"{what:96s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def main(out):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    z = sd_zip.make(out / "sd-card.zip", ["PIANO", "FLUTE"])
    ref = out / "sd-zip-ref"
    made = gen_samples.card_files(ref, ["PIANO", "FLUTE"])
    with zipfile.ZipFile(z) as f:
        names = set(f.namelist())
        sets = {m.group(1) for n in names if (m := re.match(r"OPTIMIST/SAMPLES/([^/]+)/Z\d\.WAV$", n))}
        check("the ticked sets, no other: PIANO and FLUTE", sets == {"PIANO", "FLUTE"})
        want = {f"OPTIMIST/SAMPLES/{s}/{n}" for s, v in made.items() for n in v}
        check(f"every zone of each ({len(want)}), as Z0.WAV .. in key order", want <= names and
              len([n for n in names if n.endswith(".WAV")]) == len(want))
        check("each file the generator's own (the flash set's samples decoded)",
              all(f.read(n) == (ref / n).read_bytes() for n in want))
        check("each a WAV with fmt, smpl, inst and data", all(
            f.read(n)[:4] == b"RIFF" and all(c in f.read(n)[:200] for c in (b"fmt ", b"smpl", b"inst", b"data"))
            for n in want))
        readme = f.read("OPTIMIST/README.TXT").decode()
        check("the README names the sets", "PIANO, FLUTE" in readme and "NOT ON CARD" in readme)
        check("the factory presets beside them", any(n.startswith("OPTIMIST/PRESETS/FACTORY/SAMPLE/") for n in names))
    print("sd_zip_test:", "FAILED" if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "sd-zip-test")))

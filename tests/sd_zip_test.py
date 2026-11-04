# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip (tools/sd_zip.py; the user, 2026-11-04: it holds everything whatever the builder's ticks; folders
capitalised, files lowercase; presets and projects in banks of 16): every sample set's zones under
Optimist/Samples/<Set>/z<k>.wav (the generator's bytes), every sample source of the repo on the card at its own rate
and length (tools/sd_samples.py: the sets, the sampled kits' sounds, the X0X 909's, the hip-hop pack; 16-bit mono;
the 909's and the pack's samples the sources' own, sample for sample), every factory preset of every engine under
Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops (16 a bank, in the table's order), the engines' folder names
the firmware's own table (firmware/src/storage/sd/sd_names.h) and that table the rule (capitalised; a digit or a known
acronym upper case), the cache (a second make copies the first's bytes).
  python3 tests/sd_zip_test.py OUTDIR"""
import importlib
import json
import pathlib
import struct
import re
import shutil
import sys
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
gen_samples = importlib.import_module("gen_samples")   # (tools/: on the path above)
sd_zip = importlib.import_module("sd_zip")
sd_samples = importlib.import_module("sd_samples")
sampleio = importlib.import_module("sampleio")
sys.path.insert(0, str(ROOT / "tools" / "builder"))
R = importlib.import_module("registry")                # (the builder's items: FM6_PK_*)
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


def wav_info(data):
    """-> (format tag, channels, rate, bits, frames, the data's bytes) of a WAV's bytes"""
    i, fmt, body = 12, None, b""
    while i + 8 <= len(data):
        cid, sz = data[i:i + 4], struct.unpack("<I", data[i + 4:i + 8])[0]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", data[i + 8:i + 24])
        elif cid == b"data":
            body = data[i + 8:i + 8 + sz]
        i += 8 + sz + (sz & 1)
    tag, ch, rate, _, _, bits = fmt
    return tag, ch, rate, bits, len(body) // (ch * bits // 8), body


def source_frames(path, trim):
    """-> (rate, frames) of a source as the card holds it: its frames, from its onset less 16 when trim (the sets' and
    kits' trim, gen_samples.py cc0_entries)"""
    sr, x = sampleio.read_any_wav(path)
    return sr, len(x) - (max(0, sampleio.onset(x) - 16) if trim else 0)


def check_sources(f, names):
    """every sample source in the repo on the card, at its rate and length, 16-bit mono"""
    cc0 = ROOT / "assets" / "samples-cc0"
    wavs = {n: wav_info(f.read(n)) for n in names if n.endswith(".wav")}
    check(f"every card sample 16-bit mono PCM ({len(wavs)})", all(w[0] == 1 and w[1] == 1 and w[3] == 16
                                                                    for w in wavs.values()))
    for s in gen_samples.CARD_SETS:
        d = gen_samples.card_set_dir(s)
        got = sorted(w[2:5:2] for n, w in wavs.items() if n.startswith(f"Optimist/Samples/{d}/"))
        want = sorted(source_frames(p, True) for p in (cc0 / s).glob("*.wav"))
        check(f"set {d}: every source ({len(want)}) at its rate and length: "
              + ", ".join(sorted({f"{r} Hz" for r, _ in want})), got == want)
    kit = sd_samples.kit_sources()
    got = sorted(w[2:5:2] for n, w in wavs.items() if n.startswith("Optimist/Samples/Kits/Acoustic/"))
    want = sorted(source_frames(p, True) for p in kit.values())
    check(f"the sampled kits' sounds: every CC0 kit file ({len(want)} = {len(list((cc0 / 'KIT').glob('*.wav')))}) at "
          "its rate and length, as <nn> - <name>.wav", got == want and len(want) == len(list((cc0 / "KIT").glob("*.wav")))
          and all(f"Optimist/Samples/Kits/Acoustic/{sd_samples.kit_file(k)}" in wavs for k in range(len(want))))
    ok = True
    for nm in sd_samples.X909_FILES:
        pcm, _ = sd_samples.gx.read_wav(ROOT / "assets" / "x0x909" / nm)
        w = wavs.get(f"Optimist/Samples/X0X/909/{nm}")
        ok = ok and w is not None and w[2] == 44100 and w[5] == struct.pack(f"<{len(pcm)}h", *pcm)
    check("the X0X 909's hh, crash, ride: 44.1 kHz, every sample the source's (the 24-bit hat rounded to 16 bits)", ok)
    pack = json.loads((ROOT / "assets" / "hiphop-pack" / "pack.json").read_text())
    ok, n = True, 0
    for snd in pack["sounds"]:
        for z in snd["zones"]:
            src = ROOT / "assets" / "hiphop-pack" / z["file"]
            card = f"Optimist/Samples/Hiphop/{sd_samples.hiphop_folder(z['file'])}/{z['file'].split('/', 1)[1].lower()}"
            sw, w = wav_info(src.read_bytes()), wavs.get(card)
            ok = ok and w is not None and w[2] == sw[2] == 22050 and w[5] == sw[5] and sw[1] == 1 and sw[3] == 16
            n += 1
    check(f"the hip-hop pack: every zone ({n}) at 22.05 kHz, the source's samples", ok and n > 20)


def main(out):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    sd_zip.CACHE = out / "cache"                        # (the test's own cache, emptied: a fresh zip first)
    shutil.rmtree(sd_zip.CACHE, ignore_errors=True)
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
        check(f"every zone of each ({len(want)}), as z0.wav .. in key order", want <= names)
        check("each file the generator's own (the sources at their rate)",
              all(f.read(n) == (ref / "Optimist" / "Samples" / n.split("/", 2)[2]).read_bytes() for n in want))
        check("each a WAV with fmt, smpl, inst and data", all(
            f.read(n)[:4] == b"RIFF" and all(c in f.read(n)[:200] for c in (b"fmt ", b"smpl", b"inst", b"data"))
            for n in want))
        check_sources(f, names)
        check("the folders: Optimist/ and its eight, Presets/Factory and Presets/User",
              all(f"Optimist/{d}/" in names for d in sd_zip.DIRS + sd_zip.SUBDIRS))
        check("every file's name lowercase", all(n.rsplit("/", 1)[-1] == n.rsplit("/", 1)[-1].lower() for n in files))
        check("every folder's name capitalised (a bank's a letter, a model number its digits: X0X/909)",
              all(p[:1].isupper() or p.isdigit() for n in names for p in n.split("/")[:-1]))
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
        # every collection flash mode lists as presets (the user, 2026-11-04: ALL, "just presets"): FM6's packs, each
        # pack's 16 voices in eng_fm6.c FM6_PACK's order, after FM6's own presets; their names the packs' own
        fw = ROOT / "firmware" / "src" / "engines" / "fm6"
        order = re.findall(r"\(FM6_PK\((\w+)\)\)", (fw / "eng_fm6.c").read_text())   # (FIF(..)(FM6_PK(KEYS)) ..)
        packs = [re.findall(r'"([^"]*)"', re.search(rf"FM6_PK_{p}_N\[16\]\[11\] = \{{(.*?)\}};",
                                                     (fw / f"eng_fm6_pk_{p.lower()}.h").read_text()).group(1))
                 for p in order]
        pk_items = [k for k in R.ITEMS if k.startswith("FM6_PK_")]
        check(f"FM6's packs: {len(packs)} ({', '.join(order)}), each 16 voices, one a builder item",
              len(packs) == len(pk_items) == 5 and all(len(v) == 16 for v in packs))
        fm6 = sorted((b, int(nn), nm) for e, b, nn, nm in pre if e == "FM6")
        own = len(fm6) - 16 * len(packs)
        want_pk = [re.sub(r'[/\\:*?"<>|]', "_", v.rstrip()).lower() for p in packs for v in p]
        check(f"FM6: its {own} own presets, then the {16 * len(packs)} pack voices in order, named as the packs name them",
              own == 3 and [nm for _, _, nm in fm6[own:]] == want_pk)
        print("presets per engine: " + ", ".join(f"{e} {len(v)}" for e, v in sorted(by.items())) +
              f"; {len(pre)} in all")
        kit = sorted((ord(m.group(1)) - 65) * 16 + int(m.group(2)) - 1 for n in files
                     if (m := re.match(r"Optimist/Kits/Factory/([A-Z])/(\d\d) - .+\.opk$", n)))
        check(f"the factory kits: {len(kit)} files, Kits/Factory/<L>/<nn> - <name>.opk numbered 0 .. n - 1 with no gap, "
              "each a card kit file (\"OPKT\"; their count and names against drums.c's: sd_factory_test --kits)",
              len(kit) > 40 and kit == list(range(len(kit))) and
              all(f.read(n)[:4] == b"OPKT" for n in files if n.startswith("Optimist/Kits/Factory/")) and
              len(kit) == len([n for n in files if n.startswith("Optimist/Kits/")]))
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

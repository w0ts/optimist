# SPDX-License-Identifier: GPL-3.0-only
"""The SD card zip (tools/sd_zip.py; the user, 2026-11-04: folders capitalised, files lowercase; presets and projects in
banks of 16; "the samples we select in the builder just go on the SD card and not in flash, and that's it"): the ticks
(core.card_content: a set only when ticked, the sampled kits' sounds and the 909's only when ticked and with the card's
streams, FM6's packs only when ticked; every factory preset and kit whatever they say; the cache keyed by the ticks
too); the zip of every tick: every sample set's zones under
Optimist/Samples/<Set>/z<k>.wav (the generator's bytes), every sample source of the repo on the card at its own rate
and length (tools/sd_samples.py: the sets, the sampled kits' sounds, the X0X 909's, the hip-hop pack; 16-bit mono;
the 909's and the pack's samples the sources' own, sample for sample), the factory ids (a pure function of each sound:
sd_ids.h, recomputed here by a second XXH64; a second build elsewhere gives the same), every factory preset of every engine under
Optimist/Presets/Factory/<Engine>/<L>/<nn> - <name>.ops (16 a bank, in the table's order), the engines' folder names
the firmware's own table (firmware/src/storage/sd/sd_names.h) and that table the rule (capitalised; a digit or a known
acronym upper case), the cache (a second make copies the first's bytes).
  python3 tests/sd_zip_test.py OUTDIR"""
import importlib
import json
import os
import pathlib
import struct
import subprocess
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


M64 = (1 << 64) - 1
P1, P2, P3, P4, P5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9, 0x85EBCA77C2B2AE63,
                      0x27D4EB2F165667C5)


def xxh64(data, seed=0):
    """XXH64 (xxhash.com doc/xxhash_spec.md), written here again from the specification: the firmware's sd_hash.c is
    not used, so the ids are checked by a second hand"""
    def rotl(x, r):
        return ((x << r) | (x >> (64 - r))) & M64

    def rnd(acc, lane):
        return (rotl((acc + lane * P2) & M64, 31) * P1) & M64

    def merge(h, v):
        return ((h ^ rnd(0, v)) * P1 + P4) & M64
    n, i = len(data), 0
    if n >= 32:
        v = [(seed + P1 + P2) & M64, (seed + P2) & M64, seed, (seed - P1) & M64]
        while i + 32 <= n:
            for k in range(4):
                v[k] = rnd(v[k], int.from_bytes(data[i + 8 * k:i + 8 * k + 8], "little"))
            i += 32
        h = (rotl(v[0], 1) + rotl(v[1], 7) + rotl(v[2], 12) + rotl(v[3], 18)) & M64
        for k in range(4):
            h = merge(h, v[k])
    else:
        h = (seed + P5) & M64
    h = (h + n) & M64
    while i + 8 <= n:
        h = (rotl(h ^ rnd(0, int.from_bytes(data[i:i + 8], "little")), 27) * P1 + P4) & M64
        i += 8
    if i + 4 <= n:
        h = (rotl(h ^ (int.from_bytes(data[i:i + 4], "little") * P1 & M64), 23) * P2 + P3) & M64
        i += 4
    while i < n:
        h = (rotl(h ^ (data[i] * P5 & M64), 11) * P1) & M64
        i += 1
    h ^= h >> 33
    h = (h * P2) & M64
    h ^= h >> 29
    h = (h * P3) & M64
    return h ^ (h >> 32)


def factory_id(kind, uid, name=b"", nth=1):
    """sd_ids.h sdi_mix: XXH64("OPID", kind, uid LE32, name[, "#" nth LE32]) with bit 63 clear"""
    d = b"OPID" + kind + uid.to_bytes(4, "little") + name + (b"#" + nth.to_bytes(4, "little") if nth > 1 else b"")
    return xxh64(d) & ~(1 << 63)


def file_ids(zpath):
    """-> {path: (header version, id, payload)} of a zip's .ops / .opk files"""
    out = {}
    with zipfile.ZipFile(zpath) as f:
        for n in f.namelist():
            if n.endswith((".ops", ".opk")):
                b = f.read(n)
                out[n] = (struct.unpack("<H", b[4:6])[0], struct.unpack("<Q", b[16:24])[0], b[24:])
    return out


def check_ids(z, out):
    """the factory ids: a pure function of each sound (sd_ids.h), the same in every build"""
    ids = file_ids(z)
    vals = [i for _, i, _ in ids.values()]
    check(f"every factory preset and kit file carries an id (header version 2), none twice ({len(ids)} files)",
          ids and all(v == 2 for v, _, _ in ids.values()) and all(vals) and len(set(vals)) == len(vals) and
          not any(i >> 63 for i in vals))
    ok, seen = True, {}
    for n in sorted(ids):
        _, i, pay = ids[n]
        m = re.match(r"Optimist/Kits/Factory/([A-Z])/(\d\d) - ", n)
        if m:
            ok = ok and i == factory_id(b"K", (ord(m.group(1)) - 65) * 16 + int(m.group(2)) - 1)
            continue
        eng = n.split("/")[3]
        name = pay[5:17].split(b"\0")[0]
        nth = seen.setdefault(eng, []).count(name) + 1
        seen[eng].append(name)
        ok = ok and i == factory_id(b"P", pay[2], name, nth)
    check("each id what identifies the sound and nothing else: a preset's engine UID and name (its nth of that name), "
          "a kit's UID; never its bank slot, a device, a time, a path (recomputed here by a second XXH64)", ok)
    # a second build elsewhere: another working folder, another OPTIMIST_BUILD_DIR, no cache
    other = out / "other-build"
    other.mkdir(exist_ok=True)
    env = dict(os.environ, OPTIMIST_BUILD_DIR=str(other / "b"))
    z2 = other / "again.zip"
    code = f"import sys; sys.path.insert(0, {str(ROOT / 'tools')!r}); import sd_zip; sd_zip.generate(sys.argv[1])"
    subprocess.run([sys.executable, "-c", code, str(z2)], cwd=other, env=env, check=True, capture_output=True)
    ids2 = file_ids(z2)
    check("a second zip built in another folder with another build dir: every file's id byte-identical",
          {n: i for n, (_, i, _) in ids.items()} == {n: i for n, (_, i, _) in ids2.items()})


def sd_cfg(**ticks):
    """a builder configuration in SD card mode (core.set_front SD_MODE 1) with every set, sampled kit and FM6 pack off,
    the card's streams on (SD_RING, ARENA: card_drums) unless ticks say otherwise, then ticks"""
    C = importlib.import_module("configure")
    cfg = C.K.set_front(C.defaults(), "SD_MODE", 1)
    off = {k: 0 for k in R.ITEMS if k.startswith(("SET_", "KIT_", "FM6_PK_"))}
    return dict(cfg, SD_RING=8, ARENA=1, DRUM_SAMPLED=0, DRUM_X0X909=0, X909_CYM=0, **off) | ticks


def zip_parts(zpath):
    """-> (the sample folders: Samples/<a>/<b> of each sample file, but the pack's; the preset files by engine; the kit
    files) of a zip"""
    with zipfile.ZipFile(zpath) as f:
        files = [n for n in f.namelist() if not n.endswith("/")]
    smp = sorted({n for n in files if n.startswith("Optimist/Samples/") and "/Hiphop/" not in n})
    pre = {}
    for n in files:
        if n.startswith("Optimist/Presets/Factory/"):
            pre[n.split("/")[3]] = pre.get(n.split("/")[3], 0) + 1
    kits = [n for n in files if n.startswith("Optimist/Kits/Factory/")]
    hip = [n for n in files if "/Samples/Hiphop/" in n]
    return smp, pre, kits, hip


def check_ticks(out, full):
    """the ticks choose the samples and FM6's packs; every preset and kit is in whatever they say"""
    K = importlib.import_module("core")
    content = sd_zip.content_of
    every = {"sets": list(gen_samples.CARD_SETS), "kits": True, "x909": list(sd_samples.X909_FILES),
             "fm6_packs": list(sd_zip.PACKS)}
    ticked = sd_cfg(**{k: 1 for k in R.ITEMS if k.startswith(("SET_", "KIT_", "FM6_PK_"))}, DRUM_SAMPLED=1,
                    DRUM_X0X909=1, X909_CYM=1)
    check("ticks: everything ticked, the card's streams on: every set, the kits' sounds, the 909's three, every pack",
          K.card_content(ticked) == every)
    check("ticks: the same without the card's streams (SD_RING 0): the drum samples in the flash, not on the card",
          K.card_content(dict(ticked, SD_RING=0)) == dict(every, kits=False, x909=[]))
    check("ticks: not SD card mode (the defaults): nothing on the card (it is all in the flash)",
          K.card_content(importlib.import_module("configure").defaults()) == {"sets": [], "kits": False, "x909": [],
                                                                               "fm6_packs": []})
    check("ticks: a sampled kit ticked but DRUM_SAMPLED off: no kit sounds; DRUM_SAMPLED on but no kit: none either",
          not K.card_content(sd_cfg(KIT_DEEP=1))["kits"] and not K.card_content(sd_cfg(DRUM_SAMPLED=1))["kits"]
          and K.card_content(sd_cfg(DRUM_SAMPLED=1, KIT_DEEP=1))["kits"])
    check("ticks: the 909 with X909_CYM off: its hats only; on: hats, crash and ride; no 909: none",
          K.card_content(sd_cfg(DRUM_X0X909=1))["x909"] == ["hh.wav"] and
          K.card_content(sd_cfg(DRUM_X0X909=1, X909_CYM=2))["x909"] == ["hh.wav", "crash.wav", "ride.wav"] and
          K.card_content(sd_cfg(X909_CYM=1))["x909"] == [])
    check("ticks: FM6 off: its packs are not on the card, ticked or not",
          K.card_content(sd_cfg(ENG_FM6=0, FM6_PK_KEYS=1))["fm6_packs"] == [])
    a, b = content(sd_cfg(SET_PIANO=1)), content(sd_cfg(SET_BASS=1))
    check("the cache: keyed by the ticks too (two contents, two keys; the same content, one)",
          sd_zip.key(a) != sd_zip.key(b) and sd_zip.key(a) == sd_zip.key(content(sd_cfg(SET_PIANO=1))))
    smp_all, pre_all, kits_all, hip_all = zip_parts(full)
    zp = sd_zip.make(out / "piano.zip", content(sd_cfg(SET_PIANO=1, DRUM_X0X909=1, FM6_PK_KEYS=1)), echo=False)
    smp, pre, kits, hip = zip_parts(zp)
    check("PIANO only ticked (and the 909, CYM off, and FM6's KEYS): only Samples/Piano of the sets, the 909's hh.wav, "
          "no kit sounds", {n.split("/")[2] for n in smp} == {"Piano", "X0X"} and
          [n for n in smp if "/X0X/" in n] == ["Optimist/Samples/X0X/909/hh.wav"])
    check("... Piano's files the full zip's, byte for byte", all(
        zipfile.ZipFile(zp).read(n) == zipfile.ZipFile(full).read(n) for n in smp))
    check("... every factory kit (as the full zip), every engine's presets (FM6: its own 3 and KEYS' 16, not the other "
          "packs)", kits == kits_all and pre.get("FM6") == 3 + 16 and
          {e: n for e, n in pre.items() if e != "FM6"} == {e: n for e, n in pre_all.items() if e != "FM6"})
    with zipfile.ZipFile(zp) as f:
        names = f.namelist()
        fm6 = sorted(n for n in names if "/Presets/Factory/FM6/" in n and n.endswith(".ops"))
        check("... FM6's KEYS voices numbered on after its own (A/04 ..), no gap", len(fm6) == 19 and
              fm6[3].startswith("Optimist/Presets/Factory/FM6/A/04 - ") and fm6[-1].startswith(
                  "Optimist/Presets/Factory/FM6/B/03 - "))
        readme = f.read("Optimist/readme.txt").decode()
        check("... the readme names what is there: Piano, the 909's hh.wav, the KEYS pack; no kit sounds line",
              "(Piano)" in readme and "hh.wav" in readme and "KEYS" in readme and "Kits/Acoustic" not in readme)
    check("... the hip-hop pack in every zip (no builder item: the USR slots')", hip == hip_all and len(hip) > 20)
    zn = sd_zip.make(out / "nostream.zip", content(sd_cfg(SD_RING=0, DRUM_SAMPLED=1, KIT_ACOUSTIC=1, DRUM_X0X909=1,
                                                          X909_CYM=1)), echo=False)
    smp, pre, kits, _ = zip_parts(zn)
    check("no set ticked, the kits and the 909 ticked without the card's streams: no sample but the pack's, every "
          "preset (FM6 its own 3) and kit", smp == [] and kits == kits_all and pre.get("FM6") == 3)
    zk = sd_zip.make(out / "kits.zip", content(sd_cfg(DRUM_SAMPLED=1, KIT_TIGHT=1, DRUM_X0X909=1, X909_CYM=1)),
                     echo=False)
    smp, _, _, _ = zip_parts(zk)
    check("a sampled kit and the 909 with its cymbals ticked, with the streams: the 16 kit sounds and hh, crash, ride",
          smp == sorted([f"Optimist/Samples/Kits/Acoustic/{sd_samples.kit_file(k)}" for k in range(16)] +
                        [f"Optimist/Samples/X0X/909/{n}" for n in sd_samples.X909_FILES]))


def main(out):
    out = pathlib.Path(out).resolve()
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
        check("every tick (--all): every sample set: " + ", ".join(sorted(sets)),
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
        fam = ["909", "808", "synth", "kawa", "plaits", "sampled"]
        ins = [m.group(1).split(", ") for n in files if n.startswith("Optimist/Kits/Factory/")
               if (m := re.search(r" \(([a-z0-9, ]+)\)\.opk$", n))]
        check("each kit's name says its instruments: <nn> - <name> (909, sampled).opk, of 909 808 synth kawa plaits "
              "sampled in that order", len(ins) == len(kit) and all(i and all(x in fam for x in i) and
                                                                    i == sorted(i, key=fam.index) for i in ins))
        heads = [f.read(n)[:4] for n in files if n.startswith("Optimist/Presets/Factory/")]
        check("each a card preset file (\"OPPR\")", heads and all(h == b"OPPR" for h in heads))
        readme = f.read("Optimist/readme.txt").decode()
        check("the readme names the sets and the banks", "Piano" in readme and "16 a bank" in readme)
    check_ids(z, out)
    check_ticks(out, z)
    reg = set(re.findall(r"X\(\d+, ([A-Z0-9_]+),", (ROOT / "firmware" / "src" / "core" / "registry.h").read_text()))
    check("sd_names.h names every engine of registry.h ENGINE_LIST", reg <= set(table))
    bad = {k: v for k, v in table.items() if v != rule(k)}
    check("sd_names.h follows the rule: capitalised, a digit or a known acronym upper case" + (f": {bad}" if bad else ""),
          not bad and table.get("FM6") == "FM6" and table.get("CS80") == "CS80" and table.get("ACID") == "Acid")
    print("sd_zip_test:", "FAILED" if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "sd-zip-test")))

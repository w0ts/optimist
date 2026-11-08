#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sort sample folders by type for the SD card (docs/SD-CARD-DESIGN.md 8: Optimist/Samples/ is the user's).

Reads any number of source folders (the originals are never touched), names each WAV's type from its path and name,
and writes a card-ready copy under OUT:

    OUT/<Type>/<pack> <name>.wav            the drums: Kick, 808, Snare, Clp, Rim, HH (closed), OH (open), Tom,
                                            Crash, Ride, Shkr, Conga, Bell, Clave, Perc (the user's short names,
                                            2026-11-08: "kick, snare, hh, clp, etc."); the others: Basses, Synth,
                                            Keys, Pad, Chord, Wavetable, Vocal, Fx, Loops,
                                            Found (field recordings)
    OUT/Unsorted/<pack> <name>.wav          what no rule names (look at sort-report.csv)

Each file is 16-bit mono 44.1 kHz (sox, repeatable dither): the streams read 16-bit and mix stereo to mono anyway
(sd_stream.h sst_frame), and a mono file is half the card's bus. Folders capitalised, files lowercase (the user's
ruling 2026-11-04). The FM-1's drum lanes open <their instrument's type folder> in PROJECT > SAMPLES
(op_sdcard.c sdc_lane_follow): `make sdcard SAMPLES=LIB` copies them into the card's Optimist/Samples/.

A byte-identical source met twice (a pack copied to two places) is written once. OUT/sort-report.csv lists every
source, its type and the file written, so a wrong guess can be fixed by a rule here and the tool run again.

    python3 tools/sd_sort_samples.py sort SRC [SRC ...] --out LIB [--dry-run]   sort and convert (run again: new files only)
    make sdcard SAMPLES=LIB                                                     copy to the FM-1's card (the user runs it)
    python3 tools/sd_sort_samples.py dn2 LIB --out DIR [--dry-run]              1,900, shortened, for the DN2 (make dn2-samples SRC=DIR)
"""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

DRUMS = ("Kick", "808", "Snare", "Clp", "Rim", "HH", "OH", "Tom", "Crash", "Ride",
         "Shkr", "Conga", "Bell", "Clave", "Perc")
KINDS = ("Basses", "Synth", "Keys", "Pad", "Chord", "Wavetable", "Vocal", "Fx", "Loops", "Found")

# (type, pattern) in order: the first that matches wins. Matched against a normalised text (lowercase, words split at
# anything not a letter, letters split from digits: "Hat1closed" -> "hat 1 closed"), the file's name first, then its
# folders from the nearest up. Word-bounded unless the pattern says otherwise.
RULES = (
    ("Loops", r"loops?|\d+ ?bpm|bpm"),
    ("808", r"808 s|bass drum 808|^808$"),
    ("OH", r"open ?hats?|hats? ?open|oh|ohh|opn hh|hh ?op(en)?|open"),
    ("HH", r"closed ?hats?|hats? ?closed|ch|chh|pedal|hh|hi ?hats?|hihats?|hats?|hat"),
    ("Ride", r"ride|rd"),
    ("Crash", r"crash|china|splash|cym|cymb|cymbals?|crsh"),
    ("Shkr", r"shaker|shk|maracas?|tamb|tambourine|tambo"),
    ("Conga", r"conga|cng|bongo|timbale|timb"),
    ("Bell", r"cowbell|cb|agogo|bell"),
    ("Clave", r"claves?|wood ?block|block"),
    ("Clp", r"claps?|clp|snaps?|clappy|clappington|massiveclap|massivclap|eightyclap"),
    ("Rim", r"rim|rims|rimshots?|rs|side ?stick|stick"),
    ("Snare", r"snares?|snr|sd|snaredrums?|snare ?drum"),
    ("Kick", r"kicks?|kik|bd|bassdrums?|basedrums?|bass ?drum|base ?drum|kck"),
    ("Tom", r"toms?"),
    ("Fx", r"fx|sfx|rise|rises|riser|falls?|laser|glitch|noise|scratch|scrt|sweep|zap|impact|various"),
    ("Wavetable", r"wavetables?|single ?cycle|cycle"),
    ("Vocal", r"vocals?|vox|vocalpack|voice"),
    ("Basses", r"bass|sub"),  # (not "Bass": the factory PIANO..BASS set's folder, mirrored by make sdcard)
    ("Pad", r"pads?|strings?|texture|static|movement|drone"),
    ("Chord", r"chords?|stab"),
    ("Keys", r"keys|piano|organ|rhodes"),
    ("Synth", r"synth|lead|pluck|seq|arp|buzz"),
    ("Perc", r"perc|percs|percussion|synth ?perc|percussion ?synth|timpani|triangle|drum|drums|one ?shots?|dry|wet"),
)
_RULES = [(t, re.compile(r"(?:^| )(?:%s)(?: |$)" % p)) for t, p in RULES]

# a pack's short name in the files' names (its top folder under a source; anything else: its initials)
PACKS = (
    (r"ultimate boom bap|ultboom", "ubb"), (r"tr-?909", "909"), (r"tr-?808", "808"), (r"tr-?707", "707"),
    (r"tr-?606", "606"), (r"tr-?626", "626"), (r"tr-?505", "505"), (r"drumwizardry|^lego ?-", "lego"),
    (r"drumnibus", "drumnibus"), (r"quasimidi", "sirius"), (r"obmatrix", "matrix"), (r"juno", "juno"),
    (r"dx200", "dx200"), (r"korg er1", "er1"), (r"datadrive80", "dd80"), (r"basement drums 2", "basement2"),
    (r"basement drums", "basement"), (r"tekno roots", "roots"), (r"tekno 1-0-1", "tekno101"),
    (r"tekno pumping", "pumping"), (r"keinseier", "keinseier"), (r"super glue", "glue"), (r"vocalpack", "vocal"),
    (r"single cycle", "cycle"), (r"wavetable", "wt"), (r"aotearoa", "aotearoa"), (r"vintage underground", "vuh"),
)
# words a file's name repeats from its pack (dropped from the written name)
NOISE = re.compile(r"^(legowelt|tr-?\d{3}|roland|jeunelys|jlb)[ _-]+", re.I)
REPORT = "sort-report.csv"                     # (in the library; never copied to a card: lib_wavs reads WAVs only)
MAX_NAME = 40                                   # the browser keeps SDB_NAME - 1 = 47 bytes; room for " 2.wav"


def norm(s):
    s = re.sub(r"([a-z])([A-Z])", r"\1 \2", s)
    s = s.lower()
    s = re.sub(r"([a-z])(\d)", r"\1 \2", s)
    s = re.sub(r"(\d)([a-z])", r"\1 \2", s)
    return " ".join(re.split(r"[^a-z0-9]+", s)).strip()


def classify(rel):
    """the type of a source at rel (its path under its source folder): the name first, then each folder up"""
    parts = rel.split(os.sep)
    texts = [norm(os.path.splitext(parts[-1])[0])] + [norm(p) for p in reversed(parts[:-1])]
    for text in texts:
        for t, rx in _RULES:
            if rx.search(text):
                return t
    return "Unsorted"


# a pack whose loose files no rule names: what they are (the pack's own folder said so)
PACK_TYPE = {"matrix": "Synth", "sirius": "Synth", "juno": "Synth", "aotearoa": "Found"}


def pack_of(rel, src):
    top = rel.split(os.sep)[0] if os.sep in rel else os.path.basename(os.path.normpath(src))
    low = rel.lower()
    for rx, tag in PACKS:
        if re.search(rx, low):
            return tag
    words = re.findall(r"[a-z0-9]+", top.lower())
    return "".join(w[0] for w in words)[:6] or "x"


def out_name(pack, rel):
    stem = NOISE.sub("", os.path.splitext(os.path.basename(rel))[0])
    stem = re.sub(r"[^a-z0-9]+", " ", stem.lower()).strip() or "sample"
    if stem.startswith(pack + " "):
        stem = stem[len(pack) + 1:]
    return (pack + " " + stem)[:MAX_NAME].strip()


def folder_of(t):
    return t


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def plan(srcs):
    """[(source, rel, type, out rel path)] for every new WAV under srcs, the duplicates dropped"""
    seen, taken, out = set(), set(), []
    for src in srcs:
        for root, dirs, files in os.walk(src):
            dirs.sort()
            for fn in sorted(files):
                if fn.startswith(".") or not fn.lower().endswith(".wav"):
                    continue
                path = os.path.join(root, fn)
                digest = sha1(path)
                if digest in seen:
                    continue
                seen.add(digest)
                rel = os.path.relpath(path, src)
                pack = pack_of(rel, src)
                t = classify(rel)
                t = PACK_TYPE.get(pack, t) if t == "Unsorted" else t
                base = out_name(pack, rel)
                name, n = base, 2
                while os.path.join(folder_of(t), name).lower() in taken:
                    name, n = "%s %d" % (base, n), n + 1
                taken.add(os.path.join(folder_of(t), name).lower())
                out.append((path, rel, t, os.path.join(folder_of(t), name + ".wav")))
    return out


def convert(job):
    path, dst = job
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".part"
    r = subprocess.run(["sox", "-R", "-q", path, "-t", "wav", "-e", "signed-integer", "-b", "16", "-c", "1",
                        tmp, "rate", "-v", "44100"], capture_output=True, text=True)
    if r.returncode:
        if os.path.exists(tmp):
            os.remove(tmp)
        return path, r.stderr.strip() or "sox failed"
    os.replace(tmp, dst)
    return path, ""


def cmd_sort(a):
    out = os.path.abspath(a.out)
    for s in a.src:
        if not os.path.isdir(s):
            sys.exit("not a folder: %s" % s)
        if os.path.commonpath([os.path.abspath(s), out]) in (os.path.abspath(s), out):
            sys.exit("--out must be outside the sources: %s" % s)
    if not a.dry_run and not shutil.which("sox"):
        sys.exit("sox is needed (brew install sox)")
    jobs = plan(a.src)
    os.makedirs(out, exist_ok=True)
    fails = {}
    if not a.dry_run:
        todo = [(p, os.path.join(out, d)) for p, _, _, d in jobs if not os.path.exists(os.path.join(out, d))]
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
            for path, err in ex.map(convert, todo):
                if err:
                    fails[path] = err
    with open(os.path.join(out, REPORT), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["type", "written", "source", "error"])
        for path, _, t, d in jobs:
            w.writerow([t, d, path, fails.get(path, "")])
    counts = {}
    for _, _, t, _ in jobs:
        counts[t] = counts.get(t, 0) + 1
    for t in DRUMS + KINDS + ("Unsorted",):
        if t in counts:
            print("%5d  %s" % (counts[t], folder_of(t)))
    print("%5d  files (%d failed); report: %s" % (len(jobs), len(fails), os.path.join(out, REPORT)))
    return 1 if fails else 0


def lib_wavs(lib):
    """[(type folder, file name)] of a sorted library, in order"""
    out = []
    for t in sorted(os.listdir(lib)):
        d = os.path.join(lib, t)
        if os.path.isdir(d) and not t.startswith("."):
            out += [(t, f) for f in sorted(os.listdir(d)) if f.lower().endswith(".wav") and not f.startswith(".")]
    return out


# The DN2 (dn2mk2's +Drive library, its session 2026-11-08: csrc/sampler/lib.c, tools/dn2samples.py): 2,048 samples at
# most, 128 folders, names up to 35 characters shown in 7 in the project's pool, 16-bit mono WAV at its own rate; a
# project's pool loads at power-on at ~0.7 MB/s, so the samples are cut to what is heard. DN2_QUOTA: how many of each
# type (spread over the packs, round robin), DN2_CODE: the name's first letters, DN2_SECONDS: the length kept.
DN2_CAP = 1900                                  # (the library holds 2,048: the rest left for the DN2's own recordings)
DN2_QUOTA = {"Kick": 280, "808": 23, "Snare": 280, "Clp": 140, "Rim": 70, "HH": 220, "OH": 100, "Tom": 106,
             "Crash": 60, "Ride": 29, "Shkr": 47, "Conga": 44, "Bell": 22, "Clave": 14, "Perc": 130, "Basses": 90,
             "Synth": 70, "Keys": 10, "Pad": 40, "Chord": 30, "Wavetable": 20, "Vocal": 1, "Fx": 50, "Loops": 24,
             "Found": 0, "Unsorted": 0}
DN2_CODE = {"Kick": "BD", "808": "8B", "Snare": "SD", "Clp": "CP", "Rim": "RS", "HH": "HH", "OH": "OH", "Tom": "TM",
            "Crash": "CR", "Ride": "RD", "Shkr": "SH", "Conga": "CG", "Bell": "CB", "Clave": "CL", "Perc": "PC",
            "Basses": "BS", "Synth": "SY", "Keys": "KY", "Pad": "PD", "Chord": "CH", "Wavetable": "WT", "Vocal": "VX",
            "Fx": "FX", "Loops": "LP"}
# (the user, 2026-11-08: 487 MB "too big. drop 150mb": the lengths cut, the count kept)
DN2_SECONDS = {"Loops": 8.0, "Pad": 4.0, "Synth": 4.0, "Keys": 4.0, "Vocal": 4.0, "Chord": 3.0, "Basses": 3.0,
               "Fx": 3.0, "Crash": 2.5, "Ride": 2.5, "808": 2.5, "OH": 1.5, "HH": 0.8, "Rim": 0.8, "Clave": 0.8,
               "Shkr": 0.8}
DN2_DRUM_SECONDS = 1.2
DN2_NAME = 11                                   # (the user, 2026-11-08: "file names are too long on dn2"; dn2mk2
                                                # smp_page.c: +DRIVE rows 21 characters, 11 with its action menu up,
                                                # the pool's list 14: "BDUBB 042" fits in all of them)
DN2_PACK = {"ubb": "UBB", "lego": "LEGO", "drumnibus": "DNIB", "sirius": "SIRI", "matrix": "OBMX", "juno": "JUNO",
            "dx200": "DX2", "er1": "ER1", "dd80": "DD80", "basement": "BSMT", "basement2": "BSM2", "roots": "ROOT",
            "tekno101": "T101", "pumping": "PUMP", "keinseier": "KEIN", "glue": "GLUE", "vocal": "VOX",
            "cycle": "CYC", "wt": "WT", "aotearoa": "AOTE", "vuh": "VUH"}   # (a pack's code; else its first 4)


def dn2_pick(items):
    """{type: [file]}: each type's quota, the packs taken in turn (the first word of the name is the pack)"""
    by = {}
    for t, f in items:
        by.setdefault(t, {}).setdefault(f.split(" ")[0], []).append(f)
    picked = {}
    for t, packs in by.items():
        want, out, rows = DN2_QUOTA.get(t, 0), [], [packs[k] for k in sorted(packs)]
        i = 0
        while len(out) < want and any(i < len(r) for r in rows):
            out += [r[i] for r in rows if i < len(r)][:want - len(out)]
            i += 1
        if out:
            picked[t] = sorted(out)
    return picked


def dn2_convert(job):
    src, dst, secs = job
    tmp = dst + ".part"
    # 16-bit mono at the file's rate, the trailing silence cut (under -60 dB), then no longer than secs, a 10 ms fade
    r = subprocess.run(["sox", "-R", "-q", src, "-t", "wav", "-e", "signed-integer", "-b", "16", "-c", "1", tmp,
                        "reverse", "silence", "1", "0.005", "-60d", "reverse", "trim", "0", "%.3f" % secs,
                        "fade", "t", "0", "-0", "0.01"], capture_output=True, text=True)
    if r.returncode:
        if os.path.exists(tmp):
            os.remove(tmp)
        return src, r.stderr.strip() or "sox failed"
    os.replace(tmp, dst)
    return src, ""


def cmd_dn2(a):
    lib, out = os.path.abspath(a.lib), os.path.abspath(a.out)
    picked = dn2_pick(lib_wavs(lib))
    total = sum(len(v) for v in picked.values())
    if total > DN2_CAP:
        sys.exit("%d samples picked, the DN2 holds %d: lower DN2_QUOTA" % (total, DN2_CAP))
    jobs = []
    for t, files in picked.items():
        packs = {}
        for f in files:                             # (numbered within type and pack: "BDUBB 042", "SD909 07")
            packs.setdefault(f.split(" ")[0], []).append(f)
        for pk, pf in packs.items():
            code = DN2_CODE.get(t, "XX") + DN2_PACK.get(pk, re.sub(r"[^a-z0-9]", "", pk)[:4]).upper()
            width = 2 if len(pf) < 100 else 3
            for n, f in enumerate(pf, 1):
                name = ("%s %0*d" % (code, width, n))[:DN2_NAME]
                jobs.append((os.path.join(lib, t, f), os.path.join(out, t, name + ".wav"),
                             DN2_SECONDS.get(t, DN2_DRUM_SECONDS)))
    print("%d samples in %d folders (at most %d: the DN2's library holds 2,048, the rest for its recordings)" % (len(jobs), len(picked), DN2_CAP))
    if a.dry_run:
        return 0
    return dn2_write(out, jobs, a.jobs)


DN2_MAP = ".dn2-map.json"                         # (in OUT: each file written -> its source and the source's size and
                                                # date: a name whose sample changed is written again)


def dn2_write(out, jobs, nj):
    """OUT made a mirror of jobs: a file written when new or its source changed (a library grown renumbers a pack:
    "BD909 03" can become another sound), a WAV no longer picked removed; -> exit status"""
    if not shutil.which("sox"):
        sys.exit("sox is needed (brew install sox)")
    mp = os.path.join(out, DN2_MAP)
    try:
        with open(mp) as f:
            old = json.load(f)
    except (OSError, ValueError):
        old = {}
    def sig(src):
        st = os.stat(src)
        return [src, st.st_size, int(st.st_mtime)]
    want = {os.path.relpath(d, out): (s_, d, secs) for s_, d, secs in jobs}
    todo = [j for rel, j in want.items() if not os.path.exists(j[1]) or old.get(rel) != sig(j[0])]
    gone = []
    for root, _, files in os.walk(out):
        for fn in files:
            rel = os.path.relpath(os.path.join(root, fn), out)
            if fn.lower().endswith(".wav") and rel not in want:
                gone.append(os.path.join(root, fn))
    for p in gone:
        os.remove(p)
    for t in {os.path.dirname(j[1]) for j in jobs}:
        os.makedirs(t, exist_ok=True)
    for root, dirs, files in os.walk(out, topdown=False):   # (a type no longer picked: its empty folder gone)
        if root != out and not os.listdir(root):
            os.rmdir(root)
    fails = 0
    with concurrent.futures.ThreadPoolExecutor(nj) as ex:
        for src, err in ex.map(dn2_convert, todo):
            if err:
                fails += 1
                print("failed: %s: %s" % (src, err))
    new = {rel: sig(j[0]) for rel, j in want.items() if os.path.exists(j[1])}
    with open(mp, "w") as f:
        json.dump(new, f, indent=0, sort_keys=True)
    size = sum(os.path.getsize(j[1]) for j in jobs if os.path.exists(j[1]))
    print("%s: %d written, %d removed, %d kept; %.0f MB" % (out, len(todo) - fails, len(gone),
                                                         len(want) - len(todo), size / 1e6))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("sort", help="sources -> a library sorted by type, 16-bit mono 44.1 kHz")
    p.add_argument("src", nargs="+", help="folders to read (never written)")
    p.add_argument("--out", required=True, help="the sorted library")
    p.add_argument("--dry-run", action="store_true", help="the report only, no audio written")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    p.set_defaults(fn=cmd_sort)
    p = sub.add_parser("dn2", help="the library -> 1,900 shortened samples for the DN2 (then make dn2-samples SRC=OUT)")
    p.add_argument("lib")
    p.add_argument("--out", required=True)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    p.set_defaults(fn=cmd_dn2)
    a = ap.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: GPL-3.0-only
"""Every sample the firmware has, as the SD card holds it (tools/sd_zip.py; the user, 2026-11-04: "the xox samples etc
should also be on the sd card, in highest resolution we have", "the drum kits samples as well"). Each from its source
in the repo at the source's own rate, 16-bit mono PCM (a 24-bit or stereo source made 16-bit mono: the streams read
16-bit and mix a stereo frame to mono anyway, so a mono file is half the bus and the card's RAM for the same sound),
its root and loop in "smpl", its keys in "inst":

  Optimist/Samples/<Set>/z<k>.wav            the melodic sets (gen_samples.py card_files: PIANO and FLUTE 44.1 kHz,
                                             the others 22.05 kHz), SAMPLE and its presets play them (eng_sample.c)
  Optimist/Samples/Kits/Acoustic/<nn> - <name>.wav   the sampled kits' sounds (assets/samples-cc0/KIT, 44.1 kHz): the
                                             five sampled kits (ACOUSTIC and its treatments DEEP, TIGHT, BRIGHT, DUST)
                                             play these through the drum lanes' card streams (drums/kit_card.c)
  Optimist/Samples/X0X/909/hh.wav, crash.wav, ride.wav   the X0X 909's hats and cymbals (assets/x0x909, 44.1 kHz;
                                             hh.wav 24-bit, rounded to 16 bits as tools/gen_x0x_drums.py does)
  Optimist/Samples/Hiphop/<Sound>/<file>.wav the hip-hop pack (assets/hiphop-pack, 22.05 kHz) for the USR slots,
                                             unchanged, the pack's roots and keys added

  python3 tools/sd_samples.py DIR       (writes DIR/Optimist/Samples/...; prints each file)
"""
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_samples as gs  # noqa: E402  (tools/: on the path above)
import gen_x0x_drums as gx  # noqa: E402
from sampleio import key_split, onset, peak, read_any_wav  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
KIT_DIR = "Kits/Acoustic"                    # (drums/kit_card.c KC_DIR)
# the sampled kits' sounds on the card, in this order ("01 - kick.wav" ..): gen_samples.py GM_KIT's roles (the CC0 kit
# has every one but the generic tom, which its low and high toms replace). drums/kit_card.c KC_FILE is this list
KIT_ROLES = [("kick", "kick"), ("rim", "rim"), ("snare", "snare"), ("clap", "clap"), ("chh", "closed hat"),
             ("ohh", "open hat"), ("tomlo", "low tom"), ("tomhi", "high tom"), ("crash", "crash"), ("ride", "ride"),
             ("tamb", "tambourine"), ("cowbell", "cowbell"), ("conga", "conga"), ("shaker", "shaker"),
             ("claves", "claves"), ("wood", "wood block")]
X909_DIR = "X0X/909"
X909_FILES = ("hh.wav", "crash.wav", "ride.wav")   # (drums/x0x/x0x_card.c)
HIPHOP = ROOT / "assets" / "hiphop-pack"
HIPHOP_DIR = "Hiphop"


def kit_file(k):
    """the sampled kits' sound k's file name (KIT_ROLES order)"""
    return f"{k + 1:02d} - {KIT_ROLES[k][1]}.wav"


def card_kit_entry(role, path):
    """a sampled kit's sound for the card: gen_samples.py gm_kit_entry at the source's own rate and to its end (the
    same trim, body shape, balance and level; its end faded over 30 ms) -> (rate, int16 samples)"""
    sr, x = read_any_wav(path)
    x = x[max(0, onset(x) - 16):]
    if role in gs.GM_SHAPE:
        hold, tau = gs.GM_SHAPE[role]
        h = int(hold * sr)
        x = [v * (1.0 if i < h else math.exp(-(i - h) / (tau * sr))) for i, v in enumerate(x)]
    fade = min(len(x) // 4, int(0.03 * sr))
    for i in range(fade):
        x[len(x) - fade + i] *= 1 - i / fade
    g = 30000 / peak(x) * 10 ** (gs.GM_GAIN.get(role, 0) / 20)
    return sr, [max(-32768, min(32767, int(v * g))) for v in x]


def kit_sources():
    """-> {role: the CC0 kit's file}: gen_samples.py gm_kit_sources' choice, the CC0 kit only"""
    src = {}
    for p in sorted((gs.CC0 / "KIT").glob("*.wav")):
        r = gs.gm_role(p.name)
        if r and r not in src:
            src[r] = p
    return src


def write_kits(base):
    """the sampled kits' sounds -> [(card path, source, rate, frames)]"""
    src, out = kit_sources(), []
    missing = [r for r, _ in KIT_ROLES if r not in src]
    if missing:
        raise SystemExit(f"sd_samples: the CC0 kit lacks {missing}")
    d = base / KIT_DIR
    d.mkdir(parents=True, exist_ok=True)
    for k, (role, _) in enumerate(KIT_ROLES):
        sr, pcm = card_kit_entry(role, src[role])
        lo, hi, root = gs.GM_KIT[role][0]
        z = dict(n=len(pcm), ls=0, le=0, looped=False, sr=sr, root16=root * 16, lo=lo, hi=hi)
        (d / kit_file(k)).write_bytes(gs.card_wav(z, pcm))
        out.append((f"{KIT_DIR}/{kit_file(k)}", src[role], sr, len(pcm)))
    return out


def wav_rate(path):
    """a WAV's rate (its fmt chunk)"""
    d = path.read_bytes()
    i = 12
    while i + 8 <= len(d):
        cid, sz = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"fmt ":
            return struct.unpack("<I", d[i + 12:i + 16])[0]
        i += 8 + sz + (sz & 1)
    raise SystemExit(f"{path}: no fmt chunk")


def write_x909(base):
    """the 909's hats and cymbals, unchanged but the 24-bit hat rounded to 16 bits -> [(card path, source, rate, frames)]"""
    d = base / X909_DIR
    d.mkdir(parents=True, exist_ok=True)
    out = []
    for name in X909_FILES:
        p = gx.ASSETS / name
        pcm, _ = gx.read_wav(p)
        sr = wav_rate(p)
        z = dict(n=len(pcm), ls=0, le=0, looped=False, sr=sr, root16=60 * 16, lo=0, hi=127)
        (d / name).write_bytes(gs.card_wav(z, pcm))
        out.append((f"{X909_DIR}/{name}", p, sr, len(pcm)))
    return out


def hiphop_folder(sound_file):
    """a pack zone's folder on the card: its own folder's name capitalised (UPRIGHT: Upright)"""
    return gs.card_set_dir(sound_file.split("/")[0])


def write_hiphop(base):
    """the hip-hop pack: each sound's zones in key order, the pack's roots (and keys where it gives them, else split as
    a set's) -> [(card path, source, rate, frames)]"""
    pack = json.loads((HIPHOP / "pack.json").read_text())
    out = []
    for snd in pack["sounds"]:
        zones = sorted(snd["zones"], key=lambda z: z["root"])
        split = key_split([z["root"] for z in zones])
        for z, (lo, hi) in zip(zones, split):
            p = HIPHOP / z["file"]
            pcm, bits = gx.read_wav(p)
            sr = wav_rate(p)
            zd = dict(n=len(pcm), ls=0, le=0, looped=False, sr=sr, root16=int(z["root"]) * 16,
                      lo=int(z.get("lo", lo)), hi=int(z.get("hi", hi)))
            rel = f"{HIPHOP_DIR}/{hiphop_folder(z['file'])}/{z['file'].split('/', 1)[1].lower()}"
            (base / rel).parent.mkdir(parents=True, exist_ok=True)
            (base / rel).write_bytes(gs.card_wav(zd, pcm))
            out.append((rel, p, sr, len(pcm)))
    return out


def write_all(out):
    """every sample folder under out/Optimist/Samples -> {"sets": {set: [files]}, "files": [(path under Samples/,
    source, rate, frames)] of the kits, the 909 and the pack}"""
    base = Path(out) / "Optimist" / "Samples"
    sets = gs.card_files(out, list(gs.CARD_SETS))
    files = write_kits(base) + write_x909(base) + write_hiphop(base)
    return {"sets": sets, "files": files}


def factory_dirs():
    """the factory sample folders under Optimist/Samples (tools/sd_card.py mirrors exactly these, never another)"""
    return [gs.card_set_dir(s) for s in gs.CARD_SETS] + ["Kits", "X0X", HIPHOP_DIR]


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: sd_samples.py DIR")
    r = write_all(sys.argv[1])
    for path, src, sr, n in r["files"]:
        print(f"{path}: {sr} Hz, {n} frames ({src.relative_to(ROOT)})")
    print(f"sets: {', '.join(f'{k} {len(v)}' for k, v in r['sets'].items())}")

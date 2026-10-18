#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FM6's voice packs, listened to by numbers: builds tests/fm6_pack_check.c with every pack, renders each voice through the
host engine (the code the firmware runs, bit for bit with Dexed's MSFA) and reads the measurements.

  tools/fm6_packs_check.py            the table of every voice and its flags; exit 1 when a voice FAILs
  tools/fm6_packs_check.py --apply    also set tools/fm6_packs/levels.json so that each voice's phrase comes out at the
                                      loudness of the factory presets (-15 LUFS, tools/level_presets.py), then write the
                                      packs' headers again (tools/gen_fm6_packs.py) and measure once more
  options: --root DIR (a source tree with build/gen-host: the repository by default), --wav FILE (the C4 notes, to listen),
           --sync CMD (a shell command run before each build)

Checks (FAIL stops, warn is told):
  FAIL  a single note over +6 dBFS (past the factory presets' loudest, +4.7: the master saturates there; all of them are
        measured before it), a note that is silent (peak under -50 dBFS) on any of C2 C3 C4 C5 C6, loudness more than 1.2 LU
        from the aim (a voice limited by the +3 dBFS cap is told, not failed)
  warn  a note with a DC offset over -60 dB of its peak, a release that has not died 60 dB in 2.5 s, energy above 10 kHz
        within 35 dB of the total (aliasing), a voice that does not decay at all although it is not a pad
"""
import json
import math
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "fm6_packs"))
import gen_fm6_packs as G  # noqa: E402

AIM = -15.0                                                 # LUFS: tools/level_presets.py
ROLE_AIM = {"bass": 0.0, "held": -1.0, "comp": 0.0, "melody": 0.0}
ROLE_COL = {"bass": 0, "held": 1, "comp": 2, "melody": 3}
STEP_DB = 0.75                                             # an output level step above 20
PEAK_CAP = 3.0                                             # dBFS: the loudest single note on any key at velocity 100, before the master
                                                           # (the factory presets reach +0.5 .. +4.7: the master saturates, tests/fm6_pack_check.c --ref 0)
KEYS = ("C2", "C3", "C4", "C5", "C6")


def build(root, work, packs):
    exe = work / "fm6_pack_check"
    PACK_FLAGS = ["-DFELUCCA_FM6_PK_%s=1" % p.upper() for p, _, _ in packs]
    cmd = ["cc", "-O2", "-w", f"-I{root}/build/gen-host", f"-I{root}/firmware/src", f"-I{root}/firmware/hal"] + PACK_FLAGS + \
          ["-o", str(exe), str(root / "tests/fm6_pack_check.c"), "-lm"]
    subprocess.check_call(cmd)
    return exe


def run(exe, work, wav=None):
    tsv = work / "fm6_pack_check.tsv"
    subprocess.check_call([str(exe), str(tsv)] + ([str(wav)] if wav else []))
    rows = []
    for ln in tsv.read_text().splitlines():
        f = ln.split("\t")
        v = {"idx": int(f[0]), "name": f[1].strip(), "notes": []}
        for k in range(5):
            peak, dc, clip, t40, rel = f[2 + 5 * k:7 + 5 * k]
            v["notes"].append(dict(peak=float(peak), dc=float(dc), clip=int(clip), t40=float(t40), rel=float(rel)))
        v["f0"], v["cent"], v["hi"], v["wob"] = float(f[27]), float(f[28]), float(f[29]), float(f[30])
        v["lufs"] = [float(f[31 + 2 * r]) for r in range(4)]
        v["ppeak"] = [float(f[32 + 2 * r]) for r in range(4)]
        rows.append(v)
    return rows


def judge(rows, roles):
    out = []
    for v in rows[1:]:                                       # (INIT VOICE is the engine's)
        role = roles[v["name"]]
        col = ROLE_COL[role]
        fail, warn = [], []
        for k, n in enumerate(v["notes"]):
            if n["peak"] > PEAK_CAP + 3.0:
                fail.append(f"{n['peak']:.1f} dBFS on {KEYS[k]}")
            if n["peak"] < -50:
                fail.append(f"silent on {KEYS[k]}")
            if n["dc"] > -60:
                warn.append(f"DC {n['dc']:.0f} dB on {KEYS[k]}")
        if v["notes"][2]["rel"] >= 2.5 and role != "held":
            warn.append("release over 2.5 s")
        if v["hi"] > -35:
            warn.append(f"top end {v['hi']:.0f} dB")
        if v["wob"] > 5:
            warn.append(f"level beats {v['wob']:.0f} dB while held")
        d = v["lufs"][col] - (AIM + ROLE_AIM[role])
        pk = max(n["peak"] for n in v["notes"])
        if d < -1.2 and pk > PEAK_CAP - 1.0:
            warn.append(f"peak-limited ({pk:.1f} dBFS), {d:+.1f} LU")
        elif abs(d) > 1.2:
            fail.append(f"{d:+.1f} LU off")
        if max(n["peak"] for n in v["notes"]) - min(n["peak"] for n in v["notes"]) > 14:
            warn.append("levels differ by key")
        out.append((v, role, fail, warn, d))
    return out


def table(judged):
    print(f"{'#':>2} {'name':<10} {'role':<6} {'LUFS':>6} {'dLU':>5} {'pk C4':>6} {'phr pk':>6} {'t40 C4':>6} {'rel':>5} "
          f"{'f0':>6} {'cent':>6} {'hi':>5} {'wob':>4}  flags")
    for v, role, fail, warn, d in judged:
        n = v["notes"][2]
        print(f"{v['idx']:>2} {v['name']:<10} {role:<6} {v['lufs'][ROLE_COL[role]]:>6.1f} {d:>+5.1f} {n['peak']:>6.1f} "
              f"{max(v['ppeak']):>6.1f} {n['t40']:>6.2f} {n['rel']:>5.2f} {v['f0']:>6.1f} {v['cent']:>6.0f} {v['hi']:>5.0f} {v['wob']:>4.0f}  "
              + ("FAIL " + "; ".join(fail) + " " if fail else "") + ("warn " + "; ".join(warn) if warn else ""))


def main():
    args = sys.argv[1:]
    root = Path(args[args.index("--root") + 1]) if "--root" in args else HERE.parent
    wav = args[args.index("--wav") + 1] if "--wav" in args else None
    work = Path(root / "build/host")
    work.mkdir(parents=True, exist_ok=True)
    packs = G.load()
    roles = {v.name.strip(): v.role for _, _, vs in packs for v in vs}
    levels_path = HERE / "fm6_packs" / "levels.json"
    sync = args[args.index("--sync") + 1] if "--sync" in args else None
    for rnd in range(8 if "--apply" in args else 1):
        if sync:
            subprocess.check_call(sync, shell=True)           # (copies the packs' headers to the tree built in)
        exe = build(root, work, packs)
        rows = run(exe, work, wav)
        judged = judge(rows, roles)
        if "--apply" not in args:
            break
        levels = json.loads(levels_path.read_text()) if levels_path.exists() else {}
        moved = 0
        for v, role, fail, warn, d in judged:
            pack = next(p for p, _, vs in packs if any(x.name.strip() == v["name"] for x in vs))
            pk = max(n["peak"] for n in v["notes"])
            step = min(-round(d / STEP_DB), math.floor((PEAK_CAP - pk) / STEP_DB))   # (the loudest key under the cap first)
            if step:
                levels.setdefault(pack, {})[v["name"]] = levels.get(pack, {}).get(v["name"], 0) + step
                moved += 1
        levels_path.write_text(json.dumps(levels, indent=1, sort_keys=True) + "\n")
        print(f"round {rnd + 1}: {moved} voices moved")
        if not moved:
            break
        subprocess.check_call([sys.executable, str(HERE / "gen_fm6_packs.py")])
    table(judged)
    return 1 if any(j[2] for j in judged) else 0


if __name__ == "__main__":
    sys.exit(main())
